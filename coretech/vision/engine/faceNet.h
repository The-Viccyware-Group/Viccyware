#ifndef __Anki_Vision_FaceNet_H__
#define __Anki_Vision_FaceNet_H__

#include "coretech/common/shared/math/point.h"
#include "coretech/common/shared/math/rect.h"
#include "coretech/common/shared/types.h"
#include "coretech/vision/engine/image.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace Anki {
namespace Vision {

constexpr s32 kNumFaceLandmarks = 5;
using FaceLandmarks = std::array<Point2f, kNumFaceLandmarks>;

struct FaceDetection
{
  Rectangle<f32> rect;
  FaceLandmarks  landmarks;
  f32            score = 0.f;
};

struct FaceNetConfig
{
  std::string modelPath;
  std::string gpuCacheDir;
  bool        useGpu     = true;
  s32         numThreads = 2;
};

class FaceDetectorNet
{
public:
  FaceDetectorNet();
  ~FaceDetectorNet();

  Result Init(const FaceNetConfig& config);
  bool   IsInitialized() const;
  bool   IsUsingGpu() const;

  Result Detect(const Image& gray, f32 cropFactor, f32 minScore,
                std::vector<FaceDetection>& detections) const;

  bool   Refine(const Image& gray, const Rectangle<f32>& rect, f32 minScore,
                FaceLandmarks& landmarks) const;

private:
  struct Impl;
  std::unique_ptr<Impl> _impl;
};

class FaceEmbedderNet
{
public:
  FaceEmbedderNet();
  ~FaceEmbedderNet();

  Result Init(const FaceNetConfig& config);
  bool   IsInitialized() const;
  bool   IsUsingGpu() const;

  s32    GetEmbeddingSize() const;

  Result ComputeEmbedding(const Image& gray, const FaceLandmarks& landmarks,
                          std::vector<f32>& embedding) const;

private:
  struct Impl;
  std::unique_ptr<Impl> _impl;
};

}
}

#endif
