#ifndef __Anki_Vision_TfliteModel_H__
#define __Anki_Vision_TfliteModel_H__

#include "coretech/common/shared/types.h"

#include <memory>
#include <string>
#include <vector>

namespace cv {
  class Mat;
}

namespace Anki {
namespace Vision {

enum class TfliteDataType
{
  Float32,
  UInt8,
  Int8,
};

struct TfliteTensor
{
  void*            data       = nullptr;
  TfliteDataType   type       = TfliteDataType::Float32;
  std::vector<s32> dims;
  f32              quantScale = 1.f;
  s32              zeroPoint  = 0;

  s32 NumElements() const;
  bool IsValid() const { return (nullptr != data); }
};

struct TfliteConfig
{
  std::string      modelPath;
  s32              numThreads    = 2;
  bool             useGpu        = true;
  bool             allowFp16     = true;
  std::vector<s32> inputDims;
  std::string      gpuCacheDir   = "/data/data/com.anki.victor/cache/gpuCache";
  std::string      modelCacheDir = "/anki/data/assets/cozmo_resources/config/vision/model-cache";
};

class TfliteModel
{
public:
  TfliteModel();
  ~TfliteModel();

  TfliteModel(const TfliteModel&) = delete;
  TfliteModel& operator=(const TfliteModel&) = delete;

  Result Load(const TfliteConfig& config);

  bool IsLoaded() const;
  bool IsUsingGpu() const;

  s32 GetNumInputs() const;
  s32 GetNumOutputs() const;

  TfliteTensor GetInput(s32 index) const;
  TfliteTensor GetOutput(s32 index) const;

  Result Invoke();

  static void WriteGray(const TfliteTensor& tensor, const cv::Mat& gray, f32 mean, f32 scale);
  static void Read(const TfliteTensor& tensor, std::vector<f32>& out);

private:
  struct Impl;
  std::unique_ptr<Impl> _impl;
};

}
}

#endif
