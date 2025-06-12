/**
 * File: neuralNetModel_tflite.cpp
 *
 * Author: Andrew Stein
 * Date:   12/5/2017
 *
 * Description: Implementation of ObjectDetector Model class which wraps TensorFlow Lite.
 *              Updated for TF Lite 2.19: GPU delegate via InterpreterBuilder,
 *              and uses C++ API for tensor access.
 *
 * Copyright: Anki, Inc. 2017
 **/

#ifdef ANKI_NEURALNETS_USE_TFLITE

#include "coretech/neuralnets/neuralNetModel_tflite.h"
#include "coretech/vision/engine/image.h"
#include <list>
#include <queue>
#include <sys/stat.h>

#include "util/fileUtils/fileUtils.h"
#include "util/logging/logging.h"

#include "opencv2/imgcodecs/imgcodecs.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#include "tensorflow/lite/model.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/interpreter_builder.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/string_util.h"
#include "tensorflow/lite/profiling/profiler.h"
#include "tensorflow/lite/profiling/profile_buffer.h"
#include "tensorflow/lite/delegates/gpu/delegate.h"

namespace Anki {
namespace NeuralNets {

#define LOG_CHANNEL "NeuralNets"
constexpr int kNumThreads = 1;

namespace {
TfLiteGpuDelegateOptionsV2 g_gpu_opts = TfLiteGpuDelegateOptionsV2Default();
TfLiteDelegate*           g_gpu_delegate = nullptr;
}

TFLiteModel::TFLiteModel() = default;

TFLiteModel::~TFLiteModel()
{
  if (g_gpu_delegate) {
    TfLiteGpuDelegateV2Delete(g_gpu_delegate);
    g_gpu_delegate = nullptr;
  }
  LOG_DEBUG("TFLiteModel.Destructor", "");
}

Result TFLiteModel::LoadModelInternal(const std::string& modelPath,
                                     const Json::Value& config)
{
  DEV_ASSERT(!modelPath.empty(), "TFLiteModel.LoadModelInternal.EmptyModelPath");

  const std::string graphFile = Util::FileUtils::FullFilePath({modelPath, _params.graphFile});
  if (!Util::FileUtils::FileExists(graphFile)) {
    LOG_ERROR("TFLiteModel.LoadModelInternal.GraphFileDoesNotExist", "%s", graphFile.c_str());
    return RESULT_FAIL;
  }

  _model = tflite::FlatBufferModel::BuildFromFile(graphFile.c_str());
  if (!_model) {
    LOG_ERROR("TFLiteModel.LoadModelInternal.FailedToBuildFromFile", "%s", graphFile.c_str());
    return RESULT_FAIL;
  }
  LOG_INFO("TFLiteModel.LoadModelInternal.Success", "Loaded: %s", graphFile.c_str());

#ifdef TFLITE_CUSTOM_OPS_HEADER
  tflite::MutableOpResolver resolver;
  RegisterSelectedOps(&resolver);
#else
  tflite::ops::builtin::BuiltinOpResolver resolver;
#endif

  tflite::InterpreterBuilder builder(*_model, resolver);

  // the GPU delegate actually works!
  // however, models take FOREVER to load
  // we will use CPU for now

  // if (!g_gpu_delegate) {
  //   g_gpu_opts.inference_preference  = TFLITE_GPU_INFERENCE_PREFERENCE_FAST_SINGLE_ANSWER;
  //   g_gpu_opts.inference_priority1   = TFLITE_GPU_INFERENCE_PRIORITY_MIN_LATENCY;
  //   g_gpu_opts.inference_priority2   = TFLITE_GPU_INFERENCE_PRIORITY_AUTO;
  //   g_gpu_opts.inference_priority3   = TFLITE_GPU_INFERENCE_PRIORITY_AUTO;
  //   _interpreter = nullptr; // ensure reset
  //   g_gpu_delegate = TfLiteGpuDelegateV2Create(&g_gpu_opts);
  // }
  // if (g_gpu_delegate) {
  //   builder.AddDelegate(g_gpu_delegate);
  // }

  if (builder(&_interpreter) != kTfLiteOk || !_interpreter) {
    LOG_ERROR("TFLiteModel.LoadModelInternal.DelegateBuilderFailed", "");
    return RESULT_FAIL;
  }

  if (kNumThreads != -1) {
    _interpreter->SetNumThreads(kNumThreads);
  }
  _interpreter->SetAllowFp16PrecisionForFp32(true);

  const int input = _interpreter->inputs()[0];
  std::vector<int> sizes = {1, _params.inputHeight, _params.inputWidth, 3};
  _interpreter->ResizeInputTensor(input, sizes);
  if (_interpreter->AllocateTensors() != kTfLiteOk) {
    LOG_ERROR("TFLiteModel.LoadModelInternal.FailedToAllocateTensors", "");
    return RESULT_FAIL;
  }

  const std::string labelsFile = Util::FileUtils::FullFilePath({modelPath, _params.labelsFile});
  const Result rl = ReadLabelsFile(labelsFile, _labels);
  if (rl != RESULT_OK) return rl;

  return RESULT_OK;
}

void TFLiteModel::ScaleImage(Vision::ImageRGB& img)
{
  DEV_ASSERT(_interpreter, "TFLiteModel.ScaleImage.NullInterpreter");
  const int idx = _interpreter->inputs()[0];
  const auto method = Vision::ResizeMethod::Linear;

  if (_params.useFloatInput) {
    float* data = _interpreter->typed_tensor<float>(idx);
    if (img.GetNumRows() != _params.inputHeight || img.GetNumCols() != _params.inputWidth) {
      img.Resize(_params.inputHeight, _params.inputWidth, method);
    }
    cv::Mat cvMat(_params.inputHeight, _params.inputWidth, CV_32FC3, data);
    img.get_CvMat_().convertTo(cvMat, CV_32FC3, 1.f/_params.inputScale, _params.inputShift);
  } else {
    uint8_t* data = _interpreter->typed_tensor<uint8_t>(idx);
    Vision::ImageRGB tensorImg(_params.inputHeight, _params.inputWidth, data);
    img.Resize(tensorImg, method);
  }
}

Result TFLiteModel::Detect(Vision::ImageRGB& img,
                           std::list<Vision::SalientPoint>& salientPoints)
{
  ScaleImage(img);
  if (_interpreter->Invoke() != kTfLiteOk) {
    LOG_ERROR("TFLiteModel.Detect.FailedToInvoke", "");
    return RESULT_FAIL;
  }

  switch (_params.outputType) {
    case NeuralNetParams::OutputType::Classification: {
      if (_params.useFloatInput) {
        const float* output = _interpreter->typed_output_tensor<float>(0);
        ClassificationOutputHelper(output, img.GetTimestamp(), salientPoints);
      } else {
        const uint8_t* output = _interpreter->typed_output_tensor<uint8_t>(0);
        ClassificationOutputHelper(output, img.GetTimestamp(), salientPoints);
      }
      break;
    }
    case NeuralNetParams::OutputType::BinaryLocalization: {
      if (_params.useFloatInput) {
        const float* output = _interpreter->typed_output_tensor<float>(0);
        LocalizedBinaryOutputHelper(output, img.GetTimestamp(), 1.f, 0, salientPoints);
      } else {
        const uint8_t* output = _interpreter->typed_output_tensor<uint8_t>(0);
        TfLiteTensor* t = _interpreter->tensor(_interpreter->outputs()[0]);
        LocalizedBinaryOutputHelper(output, img.GetTimestamp(), t->params.scale, t->params.zero_point, salientPoints);
      }
      break;
    }
    default:
      LOG_ERROR("TFLiteModel.Detect.OutputTypeNotSupported", "");
      return RESULT_FAIL;
  }
  return RESULT_OK;
}

} // namespace NeuralNets
} // namespace Anki

#endif // ANKI_NEURALNETS_USE_TFLITE
