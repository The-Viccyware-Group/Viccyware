#include "coretech/vision/engine/debugImageList.h"
#include "coretech/vision/engine/eyeContact.h"
#include "coretech/vision/engine/faceNet.h"
#include "coretech/vision/engine/faceTracker.h"
#include "coretech/vision/engine/profiler.h"
#include "coretech/vision/engine/trackedFace.h"

#include "clad/types/loadedKnownFace.h"

#include "faceRecognizer_yunet.h"

#include <list>
#include <set>

namespace Anki {
namespace Vision {

  class CompressedImage;

  class FaceTracker::Impl : public Profiler
  {
  public:
    Impl(const Camera&        camera,
         const std::string&   modelPath,
         const Json::Value&   config);
    ~Impl();

    void SetRecognitionIsSynchronous(bool isSynchronous);

    Result Update(const Vision::Image&        frameOrig,
                  const float                 cropFactor,
                  std::list<TrackedFace>&     faces,
                  std::list<UpdatedFaceID>&   updatedIDs,
                  DebugImageList<CompressedImage>& debugImages);

    void AddAllowedTrackedFace(const FaceID_t faceID);
    bool HaveAllowedTrackedFaces() const { return !_allowedTrackedFaceID.empty(); }
    void ClearAllowedTrackedFaces();

    void EnableDisplay(bool enabled) { }

    static bool IsRecognitionSupported() { return true; }
    static float GetMinEyeDistanceForEnrollment();

    void SetFaceEnrollmentMode(Vision::FaceID_t forFaceID,
                               s32 numEnrollments,
                               bool forceNewID);

    void EnableEmotionDetection(bool enable) { _detectEmotion        = enable; }
    void EnableSmileDetection(bool enable)   { _detectSmiling        = enable; }
    void EnableGazeDetection(bool enable)    { _detectGaze           = enable; }
    void EnableBlinkDetection(bool enable)   { _detectBlinks         = enable; }
    void EnableRecognition(bool enable)      { _isRecognitionEnabled = enable; }

    bool IsEmotionDetectionEnabled() const   { return _detectEmotion;        }
    bool IsSmileDetectionEnabled()   const   { return _detectSmiling;        }
    bool IsGazeDetectionEnabled()    const   { return _detectGaze;           }
    bool IsBlinkDetectionEnabled()   const   { return _detectBlinks;         }
    bool IsRecognitionEnabled()      const   { return _isRecognitionEnabled; }

    bool     CanAddNamedFace() const;
    Result   AssignNameToID(FaceID_t faceID, const std::string& name, FaceID_t mergeWithID);
    Result   EraseFace(FaceID_t faceID);
    void     EraseAllFaces();
    Result   RenameFace(FaceID_t faceID, const std::string& oldName, const std::string& newName,
                        Vision::RobotRenamedEnrolledFace& renamedFace);

    std::vector<LoadedKnownFace> GetEnrolledNames() const;

    Result LoadAlbum(const std::string& albumName, std::list<LoadedKnownFace>& loadedFaces);
    Result SaveAlbum(const std::string& albumName);

    Result GetSerializedData(std::vector<u8>& albumData,
                             std::vector<u8>& enrollData);

    Result SetSerializedData(const std::vector<u8>& albumData,
                             const std::vector<u8>& enrollData,
                             std::list<LoadedKnownFace>& loadedFaces);

#if ANKI_DEVELOPER_CODE
    Result DevAddFaceToAlbum(const Image& img, const TrackedFace& face, int albumEntry);
    Result DevFindFaceInAlbum(const Image& img, const TrackedFace& face, int& albumEntry, float& score) const;
    Result DevFindFaceInAlbum(const Image& img, const TrackedFace& face, const int maxMatches,
                              std::vector<std::pair<int, float>>& matches) const;
    float DevComputePairwiseMatchScore(int faceID1, int faceID2) const;
    float DevComputePairwiseMatchScore(int faceID1, const Image& img2, const TrackedFace& face2) const;
#endif

#if ANKI_DEV_CHEATS
    void SaveAllRecognitionImages(const std::string& imagePathPrefix);
    void DeleteAllRecognitionImages();
#endif

  private:

    using TrackingID_t = FaceRecognizer::TrackingID_t;

    struct Track
    {
      TrackingID_t   id = 0;
      Rectangle<f32> rect;
      TimeStamp_t    lastSeenTime = 0;
      TimeStamp_t    lastRecognizedTime = 0;
      s32            numFramesSeen = 0;
    };

    TrackingID_t AssociateWithTrack(const Rectangle<f32>& rect, TimeStamp_t timestamp, bool& isBeingTracked);
    void PruneStaleTracks(TimeStamp_t timestamp);
    Track* FindTrack(TrackingID_t trackingID);

    void SetFaceFeatures(const FaceLandmarks& landmarks, TrackedFace& face) const;

    Result SetFacePoseFromLandmarks(const s32 nrows, const s32 ncols, const FaceLandmarks& landmarks,
                                    TrackedFace& face, f32& intraEyeDist) const;
    Result SetFacePoseWithoutLandmarks(const s32 nrows, const s32 ncols, TrackedFace& face, f32& intraEyeDist) const;

    bool DetectEyeContact(const TrackedFace& face, const TimeStamp_t& timestamp);

    bool IsEnrollable(const FaceDetection& detection, const TrackedFace& face, const f32 intraEyeDist) const;

    void Reset();

    bool _isInitialized = false;
    bool _detectEmotion = false;
    bool _detectSmiling = false;
    bool _detectGaze    = false;
    bool _detectBlinks  = false;

    bool _isRecognitionEnabled = true;

    const Camera& _camera;
    const std::string _modelPath;

    FaceDetectorNet _detector;
    FaceRecognizer  _recognizer;

    std::list<Track> _tracks;
    TrackingID_t     _nextTrackingID = 1;

    std::map<FaceID_t, EyeContact> _facesEyeContact;

    std::set<FaceID_t> _allowedTrackedFaceID;
  };

}
}
