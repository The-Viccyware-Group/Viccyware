#ifndef __Anki_Vision_FaceAlbum_H__
#define __Anki_Vision_FaceAlbum_H__

#include "coretech/common/shared/types.h"

#include <map>
#include <vector>

namespace Anki {
namespace Vision {

class FaceAlbum
{
public:
  using Feature = std::vector<f32>;

  static constexpr s32 kMaxScore = 1000;

  FaceAlbum(s32 maxUsers, s32 maxDataPerUser);

  s32  GetMaxUsers()       const { return _maxUsers; }
  s32  GetMaxDataPerUser() const { return _maxDataPerUser; }

  Result RegisterData(const Feature& feature, s32 userId, s32 dataId);
  bool   GetFeature(s32 userId, s32 dataId, Feature& feature) const;
  bool   IsRegistered(s32 userId, s32 dataId) const;

  s32 GetRegisteredUserNum() const;
  s32 GetRegisteredUserDataNum(s32 userId) const;
  s32 GetRegisteredAllDataNum() const;

  void ClearData(s32 userId, s32 dataId);
  void ClearUser(s32 userId);
  void Clear();

  s32  Verify(const Feature& feature, s32 userId) const;

  void Identify(const Feature& feature, s32 maxResults,
                std::vector<s32>& userIds, std::vector<s32>& scores, s32& numResults) const;

  Result Serialize(std::vector<u8>& buffer) const;
  Result Deserialize(const std::vector<u8>& buffer);

  static s32 ComputeScore(const Feature& a, const Feature& b);

private:
  using UserData = std::map<s32, Feature>;

  s32 _maxUsers;
  s32 _maxDataPerUser;
  std::map<s32, UserData> _users;
};

}
}

#endif
