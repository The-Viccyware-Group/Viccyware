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
#include "coretech/vision/engine/tfliteModel.h"
#include <list>
#include <queue>

#include "util/fileUtils/fileUtils.h"
#include "util/logging/logging.h"

#include "opencv2/imgcodecs/imgcodecs.hpp"
#include "opencv2/imgproc/imgproc.hpp"

namespace Anki {
namespace NeuralNets {

#define LOG_CHANNEL "NeuralNets"
constexpr int kNumThreads = 1;

TFLiteModel::TFLiteModel() = default;

TFLiteModel::~TFLiteModel()
{
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

  Vision::TfliteConfig tfConfig;
  tfConfig.modelPath  = graphFile;
  tfConfig.numThreads = kNumThreads;
  tfConfig.inputDims  = {1, _params.inputHeight, _params.inputWidth, 3};

  _model = std::make_unique<Vision::TfliteModel>();
  if (_model->Load(tfConfig) != RESULT_OK) {
    _model.reset();
    return RESULT_FAIL;
  }

  const std::string labelsFile = Util::FileUtils::FullFilePath({modelPath, _params.labelsFile});
  const Result rl = ReadLabelsFile(labelsFile, _labels);
  if (rl != RESULT_OK) return rl;

  return RESULT_OK;
}

void TFLiteModel::ScaleImage(Vision::ImageRGB& img)
{
  DEV_ASSERT(_model, "TFLiteModel.ScaleImage.NullModel");
  const auto method = Vision::ResizeMethod::Linear;
  const Vision::TfliteTensor input = _model->GetInput(0);

  if (_params.useFloatInput) {
    if (img.GetNumRows() != _params.inputHeight || img.GetNumCols() != _params.inputWidth) {
      img.Resize(_params.inputHeight, _params.inputWidth, method);
    }
    cv::Mat cvMat(_params.inputHeight, _params.inputWidth, CV_32FC3, input.data);
    img.get_CvMat_().convertTo(cvMat, CV_32FC3, 1.f/_params.inputScale, _params.inputShift);
  } else {
    Vision::ImageRGB tensorImg(_params.inputHeight, _params.inputWidth, (u8*)input.data);
    img.Resize(tensorImg, method);
  }
}

Result TFLiteModel::Detect(Vision::ImageRGB& img,
                           std::list<Vision::SalientPoint>& salientPoints)
{
  ScaleImage(img);
  if (_model->Invoke() != RESULT_OK) {
    LOG_ERROR("TFLiteModel.Detect.FailedToInvoke", "");
    return RESULT_FAIL;
  }

  const Vision::TfliteTensor output = _model->GetOutput(0);

  switch (_params.outputType) {
    case NeuralNetParams::OutputType::Classification: {
      if (_params.useFloatInput) {
        ClassificationOutputHelper((const float*)output.data, img.GetTimestamp(), salientPoints);
      } else {
        ClassificationOutputHelper((const uint8_t*)output.data, img.GetTimestamp(), salientPoints);
      }
      break;
    }
    case NeuralNetParams::OutputType::BinaryLocalization: {
      if (_params.useFloatInput) {
        LocalizedBinaryOutputHelper((const float*)output.data, img.GetTimestamp(), 1.f, 0, salientPoints);
      } else {
        LocalizedBinaryOutputHelper((const uint8_t*)output.data, img.GetTimestamp(),
                                    output.quantScale, output.zeroPoint, salientPoints);
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
