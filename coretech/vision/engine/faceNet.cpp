#include "coretech/vision/engine/faceNet.h"
#include "coretech/vision/engine/tfliteModel.h"

#include "util/fileUtils/fileUtils.h"
#include "util/logging/logging.h"

#include "opencv2/calib3d.hpp"
#include "opencv2/imgproc.hpp"

#include <cmath>
#include <cstring>

#define LOG_CHANNEL "FaceRecognizer"

namespace Anki {
namespace Vision {

namespace {

const s32 kPriorMinSizes[4][3] = {{10,16,24},{32,48,0},{64,96,0},{128,192,256}};
const s32 kPriorNumSizes[4]    = {3,2,2,3};
const s32 kPriorSteps[4]       = {8,16,32,64};
const f32 kCenterVariance      = 0.1f;
const f32 kSizeVariance        = 0.2f;

const f32 kNmsThreshold        = 0.3f;
const f32 kRefinePadFraction   = 0.35f;
const s32 kMinRefineSize_pix   = 16;

const f32 kAlignedRefPoints[kNumFaceLandmarks][2] = {
  {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f},
  {41.5493f, 92.3655f}, {70.7299f, 92.2041f}
};

const s32 kAlignedFaceSize = 112;

const f32 kEmbedderMean  = 127.5f;
const f32 kEmbedderScale = 1.f/128.f;

cv::Mat EstimateSimilarityTransform(const std::vector<cv::Point2f>& src,
                                    const std::vector<cv::Point2f>& dst)
{
  const s32 numPoints = (s32)src.size();
  if(numPoints < 2 || src.size() != dst.size()) {
    return cv::Mat();
  }

  cv::Point2f srcMean(0.f, 0.f), dstMean(0.f, 0.f);
  for(s32 i = 0; i < numPoints; ++i) {
    srcMean += src[i];
    dstMean += dst[i];
  }
  srcMean *= (1.f/(f32)numPoints);
  dstMean *= (1.f/(f32)numPoints);

  f64 srcVar = 0.;
  cv::Mat cov = cv::Mat::zeros(2, 2, CV_64F);
  for(s32 i = 0; i < numPoints; ++i)
  {
    const cv::Point2f s = src[i] - srcMean;
    const cv::Point2f d = dst[i] - dstMean;
    srcVar += (f64)s.x*s.x + (f64)s.y*s.y;
    cov.at<f64>(0,0) += (f64)d.x*s.x;
    cov.at<f64>(0,1) += (f64)d.x*s.y;
    cov.at<f64>(1,0) += (f64)d.y*s.x;
    cov.at<f64>(1,1) += (f64)d.y*s.y;
  }
  srcVar /= (f64)numPoints;
  cov    /= (f64)numPoints;

  if(srcVar <= 0.) {
    return cv::Mat();
  }

  cv::Mat u, w, vt;
  cv::SVD::compute(cov, w, u, vt);

  cv::Mat s = cv::Mat::eye(2, 2, CV_64F);
  if(cv::determinant(u)*cv::determinant(vt) < 0.) {
    s.at<f64>(1,1) = -1.;
  }

  const cv::Mat rot = u * s * vt;
  const f64 scale = (w.at<f64>(0)*s.at<f64>(0,0) + w.at<f64>(1)*s.at<f64>(1,1)) / srcVar;

  cv::Mat transform(2, 3, CV_64F);
  transform.at<f64>(0,0) = scale*rot.at<f64>(0,0);
  transform.at<f64>(0,1) = scale*rot.at<f64>(0,1);
  transform.at<f64>(1,0) = scale*rot.at<f64>(1,0);
  transform.at<f64>(1,1) = scale*rot.at<f64>(1,1);
  transform.at<f64>(0,2) = dstMean.x - (transform.at<f64>(0,0)*srcMean.x + transform.at<f64>(0,1)*srcMean.y);
  transform.at<f64>(1,2) = dstMean.y - (transform.at<f64>(1,0)*srcMean.x + transform.at<f64>(1,1)*srcMean.y);

  return transform;
}

void NonMaximaSuppression(std::vector<FaceDetection>& detections, f32 threshold)
{
  std::sort(detections.begin(), detections.end(),
            [](const FaceDetection& a, const FaceDetection& b) { return a.score > b.score; });

  std::vector<FaceDetection> kept;
  std::vector<bool> suppressed(detections.size(), false);

  for(size_t i = 0; i < detections.size(); ++i)
  {
    if(suppressed[i]) {
      continue;
    }
    kept.push_back(detections[i]);

    const cv::Rect2f rectI = detections[i].rect.get_CvRect_();
    for(size_t j = i+1; j < detections.size(); ++j)
    {
      if(suppressed[j]) {
        continue;
      }
      const cv::Rect2f rectJ = detections[j].rect.get_CvRect_();
      const f32 intersection = (rectI & rectJ).area();
      const f32 unionArea    = rectI.area() + rectJ.area() - intersection;
      if(unionArea > 0.f && (intersection/unionArea) > threshold) {
        suppressed[j] = true;
      }
    }
  }

  std::swap(detections, kept);
}

}

struct FaceDetectorNet::Impl
{
  struct Prior { f32 cx, cy, sw, sh; };

  TfliteModel net;
  s32   inputWidth  = 0;
  s32   inputHeight = 0;
  f32   inputScale  = 1.f;
  s32   scaleOutput[4] = {0,1,2,3};
  std::vector<Prior> priors;
  bool  isInitialized = false;

  static bool IsTruncated(const TfliteModel& model)
  {
    if(4 != model.GetNumOutputs()) {
      return false;
    }
    for(s32 i = 0; i < 4; ++i) {
      const TfliteTensor tensor = model.GetOutput(i);
      if(4 != tensor.dims.size() || 0 != (tensor.dims[3] % 17)) {
        return false;
      }
    }
    return true;
  }

  void BuildPriors()
  {
    const s32 halfHeight = ((inputHeight+1)/2)/2;
    const s32 halfWidth  = ((inputWidth+1)/2)/2;

    priors.clear();
    for(s32 iScale = 0; iScale < 4; ++iScale)
    {
      const s32 divisor    = (1 << (iScale+1));
      const s32 numRows    = halfHeight / divisor;
      const s32 numCols    = halfWidth  / divisor;

      for(s32 i = 0; i < numRows; ++i) {
        for(s32 j = 0; j < numCols; ++j) {
          for(s32 iSize = 0; iSize < kPriorNumSizes[iScale]; ++iSize) {
            Prior prior;
            prior.sw = (f32)kPriorMinSizes[iScale][iSize] / (f32)inputWidth;
            prior.sh = (f32)kPriorMinSizes[iScale][iSize] / (f32)inputHeight;
            prior.cx = ((f32)j + 0.5f) * (f32)kPriorSteps[iScale] / (f32)inputWidth;
            prior.cy = ((f32)i + 0.5f) * (f32)kPriorSteps[iScale] / (f32)inputHeight;
            priors.push_back(prior);
          }
        }
      }
    }
  }

  bool DecodeOutputs(std::vector<f32>& loc, std::vector<f32>& conf, std::vector<f32>& iou)
  {
    const s32 numPriors = (s32)priors.size();
    loc.assign(numPriors*14, 0.f);
    conf.assign(numPriors*2, 0.f);
    iou.assign(numPriors, 0.f);

    s32 iPrior = 0;
    std::vector<f32> output;
    for(s32 iScale = 0; iScale < 4; ++iScale)
    {
      const TfliteTensor tensor = net.GetOutput(scaleOutput[iScale]);
      TfliteModel::Read(tensor, output);

      const s32 numRows     = tensor.dims[1];
      const s32 numCols     = tensor.dims[2];
      const s32 numChannels = tensor.dims[3];
      const s32 numAnchors  = numChannels / 17;

      for(s32 i = 0; i < numRows; ++i) {
        for(s32 j = 0; j < numCols; ++j) {
          for(s32 iAnchor = 0; iAnchor < numAnchors; ++iAnchor)
          {
            if(iPrior >= numPriors) {
              return false;
            }
            const f32* src = &output[(size_t)(i*numCols + j)*numChannels + (size_t)iAnchor*17];
            for(s32 k = 0; k < 14; ++k) {
              loc[(size_t)iPrior*14 + k] = src[k];
            }
            const f32 maxLogit = std::max(src[14], src[15]);
            const f32 exp0 = std::exp(src[14] - maxLogit);
            const f32 exp1 = std::exp(src[15] - maxLogit);
            conf[(size_t)iPrior*2 + 0] = exp0 / (exp0 + exp1);
            conf[(size_t)iPrior*2 + 1] = exp1 / (exp0 + exp1);
            iou[iPrior] = src[16];
            ++iPrior;
          }
        }
      }
    }

    return (iPrior == numPriors);
  }

  Result Run(const cv::Mat& gray, f32 minScore, std::vector<FaceDetection>& detections)
  {
    detections.clear();

    if(gray.empty()) {
      return RESULT_FAIL;
    }

    const f32 scale = std::min((f32)inputWidth/(f32)gray.cols, (f32)inputHeight/(f32)gray.rows);
    const s32 scaledWidth  = std::max(1, (s32)std::round(gray.cols*scale));
    const s32 scaledHeight = std::max(1, (s32)std::round(gray.rows*scale));
    const s32 offsetX = (inputWidth  - scaledWidth)/2;
    const s32 offsetY = (inputHeight - scaledHeight)/2;

    cv::Mat canvas(inputHeight, inputWidth, CV_8UC1, cv::Scalar(0));
    cv::Mat resized = canvas(cv::Rect(offsetX, offsetY, scaledWidth, scaledHeight));
    cv::resize(gray, resized, cv::Size(scaledWidth, scaledHeight), 0, 0, cv::INTER_LINEAR);

    TfliteModel::WriteGray(net.GetInput(0), canvas, 0.f, inputScale);

    if(RESULT_OK != net.Invoke()) {
      LOG_WARNING("FaceDetectorNet.Run.InvokeFailed", "");
      return RESULT_FAIL;
    }

    std::vector<f32> loc, conf, iou;
    if(!DecodeOutputs(loc, conf, iou)) {
      LOG_WARNING("FaceDetectorNet.Run.OutputSizeMismatch", "");
      return RESULT_FAIL;
    }

    const s32 numPriors = (s32)priors.size();
    for(s32 i = 0; i < numPriors; ++i)
    {
      const f32 clampedIou = std::min(1.f, std::max(0.f, iou[i]));
      const f32 score = std::sqrt(conf[i*2 + 1] * clampedIou);
      if(score < minScore) {
        continue;
      }

      const Prior& prior = priors[i];
      const f32* l = &loc[i*14];

      const f32 centerX = (prior.cx + l[0]*kCenterVariance*prior.sw) * (f32)inputWidth;
      const f32 centerY = (prior.cy + l[1]*kCenterVariance*prior.sh) * (f32)inputHeight;
      const f32 width   = prior.sw * std::exp(l[2]*kSizeVariance) * (f32)inputWidth;
      const f32 height  = prior.sh * std::exp(l[3]*kSizeVariance) * (f32)inputHeight;

      FaceDetection detection;
      detection.score = score;
      detection.rect  = Rectangle<f32>(centerX - 0.5f*width, centerY - 0.5f*height, width, height);
      for(s32 k = 0; k < kNumFaceLandmarks; ++k)
      {
        detection.landmarks[k].x() = (prior.cx + l[4 + k*2]*kCenterVariance*prior.sw) * (f32)inputWidth;
        detection.landmarks[k].y() = (prior.cy + l[5 + k*2]*kCenterVariance*prior.sh) * (f32)inputHeight;
      }
      detections.push_back(detection);
    }

    NonMaximaSuppression(detections, kNmsThreshold);

    for(auto & detection : detections)
    {
      detection.rect = Rectangle<f32>((detection.rect.GetX() - offsetX)/scale,
                                      (detection.rect.GetY() - offsetY)/scale,
                                      detection.rect.GetWidth()/scale,
                                      detection.rect.GetHeight()/scale);
      for(auto & landmark : detection.landmarks)
      {
        landmark.x() = (landmark.x() - offsetX)/scale;
        landmark.y() = (landmark.y() - offsetY)/scale;
      }
    }

    return RESULT_OK;
  }
};

FaceDetectorNet::FaceDetectorNet()
: _impl(new Impl())
{
}

FaceDetectorNet::~FaceDetectorNet() = default;

bool FaceDetectorNet::IsInitialized() const { return _impl->isInitialized; }
bool FaceDetectorNet::IsUsingGpu()    const { return _impl->net.IsUsingGpu(); }

Result FaceDetectorNet::Init(const FaceNetConfig& config)
{
  _impl->isInitialized = false;

  TfliteConfig tfConfig;
  tfConfig.modelPath   = config.modelPath;
  tfConfig.numThreads  = config.numThreads;
  tfConfig.useGpu      = config.useGpu;
  tfConfig.gpuCacheDir = config.gpuCacheDir;

  if(tfConfig.useGpu)
  {
    TfliteConfig probeConfig = tfConfig;
    probeConfig.useGpu     = false;
    probeConfig.numThreads = 1;

    TfliteModel probe;
    if(RESULT_OK != probe.Load(probeConfig)) {
      return RESULT_FAIL;
    }
    if(!Impl::IsTruncated(probe)) {
      LOG_WARNING("FaceDetectorNet.Init.NotTruncated",
                  "%s has a fused detection head, falling back to CPU",
                  config.modelPath.c_str());
      tfConfig.useGpu = false;
    }
  }

  if(RESULT_OK != _impl->net.Load(tfConfig)) {
    return RESULT_FAIL;
  }

  const TfliteTensor input = _impl->net.GetInput(0);
  _impl->inputHeight = input.dims[1];
  _impl->inputWidth  = input.dims[2];
  _impl->inputScale  = (TfliteDataType::Float32 == input.type ? 1.f : 1.f/255.f);

  if(!Impl::IsTruncated(_impl->net)) {
    LOG_ERROR("FaceDetectorNet.Init.UnexpectedOutputs",
              "Expected four head outputs from %s", config.modelPath.c_str());
    return RESULT_FAIL;
  }

  std::vector<std::pair<s32,s32>> outputsByHeight;
  for(s32 i = 0; i < 4; ++i) {
    outputsByHeight.emplace_back(_impl->net.GetOutput(i).dims[1], i);
  }
  std::sort(outputsByHeight.begin(), outputsByHeight.end(),
            [](const std::pair<s32,s32>& a, const std::pair<s32,s32>& b) { return a.first > b.first; });
  for(s32 i = 0; i < 4; ++i) {
    _impl->scaleOutput[i] = outputsByHeight[i].second;
  }

  _impl->BuildPriors();

  LOG_INFO("FaceDetectorNet.Init.Success", "%dx%d input, %zu priors",
           _impl->inputWidth, _impl->inputHeight, _impl->priors.size());

  _impl->isInitialized = true;
  return RESULT_OK;
}

Result FaceDetectorNet::Detect(const Image& gray, f32 cropFactor, f32 minScore,
                               std::vector<FaceDetection>& detections) const
{
  if(!_impl->isInitialized) {
    return RESULT_FAIL;
  }

  const cv::Mat& fullImage = gray.get_CvMat_();

  s32 cropOffsetX = 0;
  cv::Mat searchImage = fullImage;
  if(cropFactor < 1.f)
  {
    const s32 croppedWidth = std::max(1, (s32)std::round(cropFactor * fullImage.cols));
    cropOffsetX = (fullImage.cols - croppedWidth)/2;
    searchImage = fullImage(cv::Rect(cropOffsetX, 0, croppedWidth, fullImage.rows));
  }

  Result result = _impl->Run(searchImage, minScore, detections);

  if(RESULT_OK == result && cropOffsetX > 0)
  {
    for(auto & detection : detections)
    {
      detection.rect = Rectangle<f32>(detection.rect.GetX() + cropOffsetX, detection.rect.GetY(),
                                      detection.rect.GetWidth(), detection.rect.GetHeight());
      for(auto & landmark : detection.landmarks) {
        landmark.x() += cropOffsetX;
      }
    }
  }

  return result;
}

bool FaceDetectorNet::Refine(const Image& gray, const Rectangle<f32>& rect, f32 minScore,
                             FaceLandmarks& landmarks) const
{
  if(!_impl->isInitialized) {
    return false;
  }

  const cv::Mat& fullImage = gray.get_CvMat_();

  const s32 pad = (s32)std::round(kRefinePadFraction * std::max(rect.GetWidth(), rect.GetHeight()));
  cv::Rect roi((s32)std::round(rect.GetX()) - pad,
               (s32)std::round(rect.GetY()) - pad,
               (s32)std::round(rect.GetWidth())  + 2*pad,
               (s32)std::round(rect.GetHeight()) + 2*pad);
  roi &= cv::Rect(0, 0, fullImage.cols, fullImage.rows);

  if(roi.width < kMinRefineSize_pix || roi.height < kMinRefineSize_pix) {
    return false;
  }

  std::vector<FaceDetection> detections;
  if(RESULT_OK != _impl->Run(fullImage(roi), minScore, detections) || detections.empty()) {
    return false;
  }

  for(s32 k = 0; k < kNumFaceLandmarks; ++k)
  {
    landmarks[k].x() = detections[0].landmarks[k].x() + roi.x;
    landmarks[k].y() = detections[0].landmarks[k].y() + roi.y;
  }

  return true;
}

struct FaceEmbedderNet::Impl
{
  TfliteModel net;
  s32   inputWidth    = kAlignedFaceSize;
  s32   inputHeight   = kAlignedFaceSize;
  s32   embeddingSize = 0;
  bool  isInitialized = false;
};

FaceEmbedderNet::FaceEmbedderNet()
: _impl(new Impl())
{
}

FaceEmbedderNet::~FaceEmbedderNet() = default;

bool FaceEmbedderNet::IsInitialized()   const { return _impl->isInitialized; }
bool FaceEmbedderNet::IsUsingGpu()      const { return _impl->net.IsUsingGpu(); }
s32  FaceEmbedderNet::GetEmbeddingSize() const { return _impl->embeddingSize; }

Result FaceEmbedderNet::Init(const FaceNetConfig& config)
{
  _impl->isInitialized = false;

  TfliteConfig tfConfig;
  tfConfig.modelPath   = config.modelPath;
  tfConfig.numThreads  = config.numThreads;
  tfConfig.useGpu      = config.useGpu;
  tfConfig.gpuCacheDir = config.gpuCacheDir;

  if(RESULT_OK != _impl->net.Load(tfConfig)) {
    return RESULT_FAIL;
  }

  const TfliteTensor input = _impl->net.GetInput(0);
  _impl->inputHeight = input.dims[1];
  _impl->inputWidth  = input.dims[2];

  const TfliteTensor output = _impl->net.GetOutput(0);
  _impl->embeddingSize = output.dims.back();

  LOG_INFO("FaceEmbedderNet.Init.Success", "%dx%d input, %d-d embedding",
           _impl->inputWidth, _impl->inputHeight, _impl->embeddingSize);

  _impl->isInitialized = true;
  return RESULT_OK;
}

Result FaceEmbedderNet::ComputeEmbedding(const Image& gray, const FaceLandmarks& landmarks,
                                         std::vector<f32>& embedding) const
{
  embedding.clear();

  if(!_impl->isInitialized) {
    return RESULT_FAIL;
  }

  std::vector<cv::Point2f> src, dst;
  src.reserve(kNumFaceLandmarks);
  dst.reserve(kNumFaceLandmarks);
  for(s32 i = 0; i < kNumFaceLandmarks; ++i)
  {
    src.emplace_back(landmarks[i].x(), landmarks[i].y());
    dst.emplace_back(kAlignedRefPoints[i][0], kAlignedRefPoints[i][1]);
  }

  cv::Mat transform = EstimateSimilarityTransform(src, dst);
  if(transform.empty()) {
    LOG_WARNING("FaceEmbedderNet.ComputeEmbedding.AlignmentFailed", "");
    return RESULT_FAIL;
  }

  cv::Mat aligned;
  cv::warpAffine(gray.get_CvMat_(), aligned, transform,
                 cv::Size(kAlignedFaceSize, kAlignedFaceSize), cv::INTER_LINEAR);

  if(aligned.cols != _impl->inputWidth || aligned.rows != _impl->inputHeight) {
    cv::resize(aligned, aligned, cv::Size(_impl->inputWidth, _impl->inputHeight));
  }
  if(!aligned.isContinuous()) {
    aligned = aligned.clone();
  }

  TfliteModel::WriteGray(_impl->net.GetInput(0), aligned, kEmbedderMean, kEmbedderScale);

  if(RESULT_OK != _impl->net.Invoke()) {
    LOG_WARNING("FaceEmbedderNet.ComputeEmbedding.InvokeFailed", "");
    return RESULT_FAIL;
  }

  TfliteModel::Read(_impl->net.GetOutput(0), embedding);

  f64 norm = 0.;
  for(auto value : embedding) {
    norm += (f64)value * (f64)value;
  }
  norm = std::sqrt(norm);
  if(norm <= 0.) {
    LOG_WARNING("FaceEmbedderNet.ComputeEmbedding.ZeroNorm", "");
    embedding.clear();
    return RESULT_FAIL;
  }
  for(auto & value : embedding) {
    value = (f32)((f64)value / norm);
  }

  return RESULT_OK;
}

}
}
