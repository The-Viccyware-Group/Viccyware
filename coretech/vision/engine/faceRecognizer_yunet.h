#ifndef __Anki_Vision_FaceRecognizerYuNet_H__
#define __Anki_Vision_FaceRecognizerYuNet_H__

#include "coretech/vision/engine/debugImageList.h"
#include "coretech/vision/engine/enrolledFaceEntry.h"
#include "coretech/vision/engine/faceAlbum.h"
#include "coretech/vision/engine/faceNet.h"
#include "coretech/vision/engine/profiler.h"
#include "coretech/vision/engine/trackedFace.h"

#include "clad/types/loadedKnownFace.h"

#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <thread>

namespace Json {
  class Value;
}

namespace Anki {
namespace Vision {

  class CompressedImage;

  class FaceRecognizer : public Profiler
  {
  public:
    using TrackingID_t = EnrolledFaceEntry::TrackingID_t;

    FaceRecognizer(const Json::Value& config);

    ~FaceRecognizer();

    Result Init(const FaceNetConfig& netConfig);
    Result Shutdown();

    void SetIsSynchronous(bool isSynchronous);

    bool     CanAddNamedFace() const;
    Result   AssignNameToID(FaceID_t faceID, const std::string& name, FaceID_t mergeWithID);
    Result   EraseFace(FaceID_t faceID);
    void     EraseAllFaces();

    std::vector<Vision::LoadedKnownFace> GetEnrolledNames() const;

    Result RenameFace(FaceID_t faceID, const std::string& oldName, const std::string& newName,
                      Vision::RobotRenamedEnrolledFace& renamedFace);

    bool SetNextFaceToRecognize(const Vision::Image& img,
                                TrackingID_t trackingID,
                                const Rectangle<f32>& faceRect,
                                const FaceLandmarks& landmarks,
                                const bool enableEnrollment,
                                const bool isVerification);

    void SetAllowedEnrollments(s32 N, FaceID_t forFaceID, bool forceNewID = false);

    TrackingID_t GetEnrollmentTrackID() const { return _enrollmentTrackID; }
    FaceID_t     GetEnrollmentID()      const { return _enrollmentID; }

    void ClearAllTrackingData();

    EnrolledFaceEntry GetRecognitionData(TrackingID_t forTrackingID, s32& enrollmentCountReached,
                                         DebugImageList<CompressedImage>& debugImages);

    bool HasRecognitionData(TrackingID_t forTrackingID) const;
    bool HasName(TrackingID_t forTrackingID) const;

    Result LoadAlbum(const std::string& albumName, std::list<LoadedKnownFace>& loadedFaces);
    Result SaveAlbum(const std::string& albumName);

    Result GetSerializedData(std::vector<u8>& albumData,
                             std::vector<u8>& enrollData);

    Result SetSerializedData(const std::vector<u8>& albumData,
                             const std::vector<u8>& enrollData,
                             std::list<LoadedKnownFace>& loadedFaces);

    bool GetFaceIDFromTrackingID(const TrackingID_t trackingID, FaceID_t& faceID) const;

    std::string GetBestGuessNameForTrackingID(const TrackingID_t trackingID) const;

    static bool GetLandmarksFromFace(const TrackedFace& face, FaceLandmarks& landmarks);

#if ANKI_DEVELOPER_CODE
    Result DevAddFaceToAlbum(const Image& img, const TrackedFace& face, int albumEntry);

    Result DevFindFaceInAlbum(const Image& img, const TrackedFace& face, int& albumEntry, float& score) const;

    Result DevFindFaceInAlbum(const Image& img, const TrackedFace& face, const int maxMatches,
                              std::vector<std::pair<int, float>>& matches) const;

    float DevComputePairwiseMatchScore(int faceID1, int faceID2) const;
    float DevComputePairwiseMatchScore(int faceID1, const Image& img, const TrackedFace& face) const;
    float DevComputePairwiseMatchScore(const Image& img1, const TrackedFace& face1,
                                       const Image& img2, const TrackedFace& face2);
#endif

#if ANKI_DEV_CHEATS
    void SaveAllRecognitionImages(const std::string& imagePathPrefix);
    void DeleteAllRecognitionImages();
#endif

  private:

    using AlbumEntryID_t   = EnrolledFaceEntry::AlbumEntryID_t;
    using RecognitionScore = EnrolledFaceEntry::RecognitionScore;
    using EnrollmentData   = std::map<FaceID_t,EnrolledFaceEntry>;
    using Feature          = FaceAlbum::Feature;

    using AlbumEntryToFaceID = std::map<AlbumEntryID_t, FaceID_t>;

    void ExtractFeatures();

    void VerifyTrackedFace();

    AlbumEntryID_t GetNextAlbumEntryToUse();
    FaceID_t GetNextFaceID();

    Result RegisterNewUser(const Feature& feature, FaceID_t& faceID);

    Result UpdateExistingAlbumEntry(AlbumEntryID_t albumEntry, const Feature& feature, RecognitionScore score);

    Result RecognizeFace(FaceID_t& faceID, RecognitionScore& recognitionScore,
                         DebugImageList<CompressedImage>& debugImages);

    Result UpdateRecognitionData(const FaceID_t recognizedID,
                                 const RecognitionScore score);

    void   UpdateBestGuessName(const std::vector<AlbumEntryID_t>& matchingAlbumEntries,
                               const std::vector<RecognitionScore>& scores,
                               const int resultNum);

    bool   IsMergingAllowed(FaceID_t toFaceID) const;

    s32    GetNumNamedFaces() const;

    Result MergeFaces(FaceID_t keepID, FaceID_t mergeID);

    Result RemoveUser(FaceID_t userID);
    EnrollmentData::iterator RemoveUser(EnrollmentData::iterator userIter);

    void RemoveTrackingID(TrackingID_t trackerID);

    void ClearAllTrackingDataInternal();

    Result GetSerializedAlbum(std::vector<u8>& serializedAlbum) const;

    Result GetSerializedEnrollData(std::vector<u8>& serializedEnrollData);

    static Result SetSerializedEnrollData(const std::vector<u8>& serializedEnrollData,
                                          EnrollmentData& newEnrollmentData,
                                          FaceID_t& newNextFaceID);

    static Result SanityCheckBookkeeping(const FaceAlbum& album,
                                         const EnrollmentData& enrollmentData,
                                         const AlbumEntryToFaceID& albumEntryToFaceID);

    Result UseLoadedAlbumAndEnrollData(FaceAlbum& loadedAlbumData,
                                       EnrollmentData& loadedEnrollmentData);

    FaceID_t GetFaceIDforAlbumEntry(AlbumEntryID_t albumEntry) const;

    void CancelExistingEnrollment();

    Result ComputeFeaturesFromFace(const Image& img, const TrackedFace& face, Feature& feature) const;

    static constexpr s32 kMaxNamedFacesInAlbum       = (s32)FaceRecognitionConstants::MaxNumFacesInAlbum;
    static constexpr s32 kMaxAlbumEntriesPerFace     = (s32)FaceRecognitionConstants::MaxNumAlbumEntriesPerFace;
    static constexpr s32 kMaxEnrollDataPerAlbumEntry = (s32)FaceRecognitionConstants::MaxNumEnrollDataPerAlbumEntry;

    static constexpr s32 kMaxTotalAlbumEntries = 1000;
    static constexpr s32 kMinSessionOnlyFaces = 10;
    static_assert( (kMaxNamedFacesInAlbum+kMinSessionOnlyFaces)*kMaxAlbumEntriesPerFace <= kMaxTotalAlbumEntries,
                  "Combination of min/max face parameters too large for the album.");

    bool _isInitialized = false;

    FaceEmbedderNet _embedder;
    FaceAlbum       _album{kMaxTotalAlbumEntries, kMaxEnrollDataPerAlbumEntry};

    enum class ProcessingState : u8 {
      Idle,
      HasNewImage,
      ExtractingFeatures,
      FeaturesReady
    };
    std::mutex      _mutex;
    std::condition_variable _newImageCondition;
    std::thread     _featureExtractionThread;
    bool            _isRunningAsync = true;
    bool            _isEnrollmentCancelled = false;
    bool            _isVerification = false;
    ProcessingState _state = ProcessingState::Idle;
    void StartThread();
    void Run();
    void StopThread();

    Image          _img;
    TrackingID_t   _trackingID = UnknownFaceID;
    Rectangle<f32> _faceRect;
    FaceLandmarks  _landmarks;
    Feature        _feature;

    std::map<TrackingID_t, FaceID_t>     _trackingToFaceID;
    std::map<TrackingID_t, s32>          _trackingIDtoVerifyFailures;
    std::map<TrackingID_t, std::string>  _trackingIDtoBestGuessName;
    AlbumEntryToFaceID                   _albumEntryToFaceID;
    bool                                 _shouldClearAllTrackingData = false;

    FaceID_t       _nextFaceID     = 1;
    AlbumEntryID_t _nextAlbumEntry = 0;

    bool      _isEnrollmentEnabled = true;
    bool      _forceNewEnrollment = false;
    FaceID_t  _enrollmentID = UnknownFaceID;
    FaceID_t  _enrollmentTrackID = UnknownFaceID;
    s32       _enrollmentCount = -1;
    s32       _origEnrollmentCount = -1;

    EnrollmentData _enrollmentData;

    std::map<AlbumEntryID_t,std::array<Vision::Image, kMaxEnrollDataPerAlbumEntry>> _enrollmentImages;
    void SetEnrollmentImage(AlbumEntryID_t albumEntry, s32 dataEntry);

    void AddDebugInfo(FaceID_t matchedID, RecognitionScore score,
                      std::list<FaceRecognitionMatch>& newDebugInfo) const;

  };

}
}

#endif
