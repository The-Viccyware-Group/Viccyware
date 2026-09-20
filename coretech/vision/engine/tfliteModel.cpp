#include "coretech/vision/engine/tfliteModel.h"

#include "util/fileUtils/fileUtils.h"
#include "util/logging/logging.h"

#include "opencv2/core.hpp"

#include "tensorflow/lite/delegates/gpu/delegate.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/interpreter_builder.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model_builder.h"

#include <cmath>
#include <cstring>
#include <mutex>

#define LOG_CHANNEL "NeuralNets"

namespace Anki {
namespace Vision {

namespace {

const char* const kSeedExtension  = "bin";

std::string MakeCacheToken(const std::string& modelPath)
{
  std::string token = modelPath;
  for(auto & c : token) {
    if('/' == c || '\\' == c) {
      c = '_';
    }
  }
  return token;
}

std::once_flag s_seedOnceFlag;

void SeedGpuCache(const std::string& modelCacheDir, const std::string& gpuCacheDir)
{
  if(modelCacheDir.empty() || gpuCacheDir.empty()) {
    return;
  }

  if(!Util::FileUtils::DirectoryExists(modelCacheDir)) {
    return;
  }

  const std::vector<std::string> toCopy = Util::FileUtils::FilesInDirectory(modelCacheDir, false,
                                                                            kSeedExtension);

  if(toCopy.empty()) {
    return;
  }

  if(!Util::FileUtils::CreateDirectory(gpuCacheDir, false, true)) {
    LOG_WARNING("TfliteModel.SeedGpuCache.CreateDirFailed", "%s", gpuCacheDir.c_str());
    return;
  }

  s32 numCopied = 0;
  for(auto const& file : toCopy)
  {
    const std::string dst = Util::FileUtils::FullFilePath({gpuCacheDir, file});
    if(Util::FileUtils::FileExists(dst)) {
      continue;
    }

    const std::string src = Util::FileUtils::FullFilePath({modelCacheDir, file});
    const std::vector<u8> contents = Util::FileUtils::ReadFileAsBinary(src);
    if(contents.empty()) {
      LOG_WARNING("TfliteModel.SeedGpuCache.ReadFailed", "%s", src.c_str());
      continue;
    }

    if(!Util::FileUtils::WriteFileAtomic(dst, contents)) {
      LOG_WARNING("TfliteModel.SeedGpuCache.WriteFailed", "%s", dst.c_str());
      continue;
    }

    ++numCopied;
  }

  if(numCopied > 0) {
    LOG_INFO("TfliteModel.SeedGpuCache.Seeded", "Copied %d prebuilt kernel cache file(s) into %s",
             numCopied, gpuCacheDir.c_str());
  }
}

TfliteDataType GetDataType(TfLiteType type)
{
  switch(type)
  {
    case kTfLiteUInt8: return TfliteDataType::UInt8;
    case kTfLiteInt8:  return TfliteDataType::Int8;
    default:           return TfliteDataType::Float32;
  }
}

TfliteTensor MakeTensor(TfLiteTensor* tensor)
{
  TfliteTensor out;
  if(nullptr == tensor) {
    return out;
  }

  out.data       = tensor->data.data;
  out.type       = GetDataType(tensor->type);
  out.quantScale = (tensor->params.scale > 0.f ? tensor->params.scale : 1.f);
  out.zeroPoint  = tensor->params.zero_point;

  out.dims.reserve(tensor->dims->size);
  for(s32 i = 0; i < tensor->dims->size; ++i) {
    out.dims.push_back(tensor->dims->data[i]);
  }

  return out;
}

}

struct TfliteModel::Impl
{
  std::unique_ptr<tflite::FlatBufferModel> model;
  std::unique_ptr<tflite::Interpreter>     interpreter;
  TfLiteDelegate*                          gpuDelegate = nullptr;
  std::string                              cacheToken;
  bool                                     isUsingGpu  = false;

  ~Impl()
  {
    interpreter.reset();
    if(nullptr != gpuDelegate) {
      TfLiteGpuDelegateV2Delete(gpuDelegate);
    }
  }

  bool ShouldUseGpu(const TfliteConfig& config) const
  {
#   ifdef STANDALONE_SIM
    {
      return false;
    }
#   else
    {
      if(!config.useGpu) {
        return false;
      }

      const char* const kOpenCLLibPath = "/lib/libOpenCL.so";
      if(!Util::FileUtils::FileExists(kOpenCLLibPath))
      {
        LOG_WARNING("TfliteModel.ShouldUseGpu.NoOpenCL",
                    "!!! %s is missing, running %s on the CPU. Inference will be slow. !!!",
                    kOpenCLLibPath, config.modelPath.c_str());
        return false;
      }

      return true;
    }
#   endif
  }

  bool CreateGpuDelegate(const TfliteConfig& config)
  {
    TfLiteGpuDelegateOptionsV2 options = TfLiteGpuDelegateOptionsV2Default();
    options.inference_preference = TFLITE_GPU_INFERENCE_PREFERENCE_SUSTAINED_SPEED;
    options.inference_priority1  = TFLITE_GPU_INFERENCE_PRIORITY_MIN_LATENCY;
    options.experimental_flags  |= TFLITE_GPU_EXPERIMENTAL_FLAGS_CL_ONLY;
    options.experimental_flags  |= TFLITE_GPU_EXPERIMENTAL_FLAGS_ENABLE_QUANT;

    if(!config.gpuCacheDir.empty() &&
       Util::FileUtils::CreateDirectory(config.gpuCacheDir, false, true))
    {
      cacheToken = MakeCacheToken(config.modelPath);
      options.serialization_dir   = config.gpuCacheDir.c_str();
      options.model_token         = cacheToken.c_str();
      options.experimental_flags |= TFLITE_GPU_EXPERIMENTAL_FLAGS_ENABLE_SERIALIZATION;
    }
    else if(!config.gpuCacheDir.empty())
    {
      LOG_WARNING("TfliteModel.CreateGpuDelegate.NoCacheDir",
                  "Kernels will be recompiled on every start: %s", config.gpuCacheDir.c_str());
    }

    gpuDelegate = TfLiteGpuDelegateV2Create(&options);
    if(nullptr == gpuDelegate) {
      LOG_WARNING("TfliteModel.CreateGpuDelegate.CreateFailed", "%s", config.modelPath.c_str());
      return false;
    }

    if(kTfLiteOk != interpreter->ModifyGraphWithDelegate(gpuDelegate)) {
      LOG_WARNING("TfliteModel.CreateGpuDelegate.ModifyGraphFailed", "%s", config.modelPath.c_str());
      return false;
    }

    return true;
  }

  bool BuildInterpreter(const TfliteConfig& config, bool useGpu)
  {
    interpreter.reset();

    if(useGpu)
    {
      tflite::ops::builtin::BuiltinOpResolverWithoutDefaultDelegates resolver;
      if(kTfLiteOk != tflite::InterpreterBuilder(*model, resolver)(&interpreter) || !interpreter) {
        return false;
      }
    }
    else
    {
      tflite::ops::builtin::BuiltinOpResolver resolver;
      if(kTfLiteOk != tflite::InterpreterBuilder(*model, resolver)(&interpreter) || !interpreter) {
        return false;
      }
    }

    interpreter->SetNumThreads(config.numThreads);
    interpreter->SetAllowFp16PrecisionForFp32(config.allowFp16);

    if(!config.inputDims.empty())
    {
      const std::vector<int> sizes(config.inputDims.begin(), config.inputDims.end());
      if(kTfLiteOk != interpreter->ResizeInputTensor(interpreter->inputs()[0], sizes)) {
        LOG_ERROR("TfliteModel.BuildInterpreter.ResizeInputFailed", "%s", config.modelPath.c_str());
        return false;
      }
    }

    return true;
  }
};

s32 TfliteTensor::NumElements() const
{
  s32 numElements = 1;
  for(auto dim : dims) {
    numElements *= dim;
  }
  return numElements;
}

TfliteModel::TfliteModel()
: _impl(new Impl())
{
}

TfliteModel::~TfliteModel() = default;

bool TfliteModel::IsLoaded()   const { return (nullptr != _impl->interpreter); }
bool TfliteModel::IsUsingGpu() const { return _impl->isUsingGpu; }

Result TfliteModel::Load(const TfliteConfig& config)
{
  _impl->interpreter.reset();
  _impl->isUsingGpu = false;

  if(!Util::FileUtils::FileExists(config.modelPath)) {
    LOG_ERROR("TfliteModel.Load.ModelDoesNotExist", "%s", config.modelPath.c_str());
    return RESULT_FAIL;
  }

  _impl->model = tflite::FlatBufferModel::BuildFromFile(config.modelPath.c_str());
  if(nullptr == _impl->model) {
    LOG_ERROR("TfliteModel.Load.BuildFromFileFailed", "%s", config.modelPath.c_str());
    return RESULT_FAIL;
  }

  bool useGpu = _impl->ShouldUseGpu(config);

  if(useGpu)
  {
    std::call_once(s_seedOnceFlag, SeedGpuCache, config.modelCacheDir, config.gpuCacheDir);

    if(!_impl->BuildInterpreter(config, true) || !_impl->CreateGpuDelegate(config))
    {
      LOG_WARNING("TfliteModel.Load.GpuFallback", "Falling back to CPU for %s",
                  config.modelPath.c_str());
      useGpu = false;
    }
  }

  if(!useGpu && !_impl->BuildInterpreter(config, false)) {
    LOG_ERROR("TfliteModel.Load.InterpreterBuildFailed", "%s", config.modelPath.c_str());
    return RESULT_FAIL;
  }

  if(kTfLiteOk != _impl->interpreter->AllocateTensors()) {
    LOG_ERROR("TfliteModel.Load.AllocateTensorsFailed", "%s", config.modelPath.c_str());
    return RESULT_FAIL;
  }

  _impl->isUsingGpu = useGpu;

  LOG_INFO("TfliteModel.Load.Success", "%s: %zu nodes in execution plan, running on %s",
           config.modelPath.c_str(), _impl->interpreter->execution_plan().size(),
           (useGpu ? "GPU" : "CPU"));

  return RESULT_OK;
}

s32 TfliteModel::GetNumInputs() const
{
  return (IsLoaded() ? (s32)_impl->interpreter->inputs().size() : 0);
}

s32 TfliteModel::GetNumOutputs() const
{
  return (IsLoaded() ? (s32)_impl->interpreter->outputs().size() : 0);
}

TfliteTensor TfliteModel::GetInput(s32 index) const
{
  if(!IsLoaded() || index < 0 || index >= GetNumInputs()) {
    return TfliteTensor();
  }
  return MakeTensor(_impl->interpreter->input_tensor(index));
}

TfliteTensor TfliteModel::GetOutput(s32 index) const
{
  if(!IsLoaded() || index < 0 || index >= GetNumOutputs()) {
    return TfliteTensor();
  }
  return MakeTensor(_impl->interpreter->output_tensor(index));
}

Result TfliteModel::Invoke()
{
  if(!IsLoaded() || kTfLiteOk != _impl->interpreter->Invoke()) {
    LOG_WARNING("TfliteModel.Invoke.Failed", "");
    return RESULT_FAIL;
  }
  return RESULT_OK;
}

void TfliteModel::WriteGray(const TfliteTensor& tensor, const cv::Mat& gray, f32 mean, f32 scale)
{
  if(!tensor.IsValid()) {
    return;
  }

  const s32 numPixels = gray.rows * gray.cols;

  if(TfliteDataType::Float32 == tensor.type)
  {
    f32* dst = (f32*)tensor.data;
    for(s32 i = 0; i < numPixels; ++i)
    {
      const f32 value = ((f32)gray.data[i] - mean) * scale;
      dst[3*i + 0] = value;
      dst[3*i + 1] = value;
      dst[3*i + 2] = value;
    }
  }
  else if(TfliteDataType::UInt8 == tensor.type)
  {
    u8* dst = (u8*)tensor.data;
    for(s32 i = 0; i < numPixels; ++i)
    {
      const f32 value = ((f32)gray.data[i] - mean) * scale / tensor.quantScale + (f32)tensor.zeroPoint;
      const u8 quantized = (u8)std::min(255.f, std::max(0.f, std::round(value)));
      dst[3*i + 0] = quantized;
      dst[3*i + 1] = quantized;
      dst[3*i + 2] = quantized;
    }
  }
  else
  {
    s8* dst = (s8*)tensor.data;
    for(s32 i = 0; i < numPixels; ++i)
    {
      const f32 value = ((f32)gray.data[i] - mean) * scale / tensor.quantScale + (f32)tensor.zeroPoint;
      const s8 quantized = (s8)std::min(127.f, std::max(-128.f, std::round(value)));
      dst[3*i + 0] = quantized;
      dst[3*i + 1] = quantized;
      dst[3*i + 2] = quantized;
    }
  }
}

void TfliteModel::Read(const TfliteTensor& tensor, std::vector<f32>& out)
{
  out.clear();

  if(!tensor.IsValid()) {
    return;
  }

  const s32 numElements = tensor.NumElements();
  out.resize(numElements);

  if(TfliteDataType::Float32 == tensor.type) {
    memcpy(out.data(), tensor.data, numElements*sizeof(f32));
  } else if(TfliteDataType::UInt8 == tensor.type) {
    const u8* src = (const u8*)tensor.data;
    for(s32 i = 0; i < numElements; ++i) {
      out[i] = ((f32)src[i] - (f32)tensor.zeroPoint) * tensor.quantScale;
    }
  } else {
    const s8* src = (const s8*)tensor.data;
    for(s32 i = 0; i < numElements; ++i) {
      out[i] = ((f32)src[i] - (f32)tensor.zeroPoint) * tensor.quantScale;
    }
  }
}

}
}
