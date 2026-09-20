#include "coretech/vision/engine/faceAlbum.h"

#include "util/logging/logging.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define LOG_CHANNEL "FaceRecognizer"

namespace Anki {
namespace Vision {

namespace {
  const u32 kSerializationMagic   = 0x31414656;
  const u32 kSerializationVersion = 1;

  template<typename T>
  void Append(std::vector<u8>& buffer, const T& value)
  {
    const u8* bytes = reinterpret_cast<const u8*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
  }

  template<typename T>
  bool Read(const std::vector<u8>& buffer, size_t& index, T& value)
  {
    if(index + sizeof(T) > buffer.size()) {
      return false;
    }
    memcpy(&value, buffer.data() + index, sizeof(T));
    index += sizeof(T);
    return true;
  }
}

FaceAlbum::FaceAlbum(s32 maxUsers, s32 maxDataPerUser)
: _maxUsers(maxUsers)
, _maxDataPerUser(maxDataPerUser)
{
}

s32 FaceAlbum::ComputeScore(const Feature& a, const Feature& b)
{
  if(a.empty() || a.size() != b.size()) {
    return 0;
  }

  f32 dotProduct = 0.f;
  for(size_t i = 0; i < a.size(); ++i) {
    dotProduct += a[i]*b[i];
  }

  const s32 score = (s32)std::round(dotProduct * (f32)kMaxScore);
  return std::min(kMaxScore, std::max(0, score));
}

Result FaceAlbum::RegisterData(const Feature& feature, s32 userId, s32 dataId)
{
  if(feature.empty()) {
    LOG_WARNING("FaceAlbum.RegisterData.EmptyFeature", "User:%d Data:%d", userId, dataId);
    return RESULT_FAIL;
  }

  if(userId < 0 || userId >= _maxUsers || dataId < 0 || dataId >= _maxDataPerUser) {
    LOG_WARNING("FaceAlbum.RegisterData.OutOfRange", "User:%d Data:%d", userId, dataId);
    return RESULT_FAIL;
  }

  auto userIter = _users.find(userId);
  if(userIter == _users.end() && (s32)_users.size() >= _maxUsers) {
    LOG_WARNING("FaceAlbum.RegisterData.AlbumFull", "%zu users", _users.size());
    return RESULT_FAIL;
  }

  _users[userId][dataId] = feature;
  return RESULT_OK;
}

bool FaceAlbum::GetFeature(s32 userId, s32 dataId, Feature& feature) const
{
  auto userIter = _users.find(userId);
  if(userIter == _users.end()) {
    return false;
  }
  auto dataIter = userIter->second.find(dataId);
  if(dataIter == userIter->second.end()) {
    return false;
  }
  feature = dataIter->second;
  return true;
}

bool FaceAlbum::IsRegistered(s32 userId, s32 dataId) const
{
  auto userIter = _users.find(userId);
  if(userIter == _users.end()) {
    return false;
  }
  return (userIter->second.find(dataId) != userIter->second.end());
}

s32 FaceAlbum::GetRegisteredUserNum() const
{
  return (s32)_users.size();
}

s32 FaceAlbum::GetRegisteredUserDataNum(s32 userId) const
{
  auto userIter = _users.find(userId);
  if(userIter == _users.end()) {
    return 0;
  }
  return (s32)userIter->second.size();
}

s32 FaceAlbum::GetRegisteredAllDataNum() const
{
  s32 total = 0;
  for(auto const& user : _users) {
    total += (s32)user.second.size();
  }
  return total;
}

void FaceAlbum::ClearData(s32 userId, s32 dataId)
{
  auto userIter = _users.find(userId);
  if(userIter == _users.end()) {
    return;
  }
  userIter->second.erase(dataId);
  if(userIter->second.empty()) {
    _users.erase(userIter);
  }
}

void FaceAlbum::ClearUser(s32 userId)
{
  _users.erase(userId);
}

void FaceAlbum::Clear()
{
  _users.clear();
}

s32 FaceAlbum::Verify(const Feature& feature, s32 userId) const
{
  auto userIter = _users.find(userId);
  if(userIter == _users.end()) {
    return 0;
  }

  s32 best = 0;
  for(auto const& data : userIter->second) {
    best = std::max(best, ComputeScore(feature, data.second));
  }
  return best;
}

void FaceAlbum::Identify(const Feature& feature, s32 maxResults,
                         std::vector<s32>& userIds, std::vector<s32>& scores, s32& numResults) const
{
  numResults = 0;

  std::vector<std::pair<s32,s32>> matches;
  matches.reserve(_users.size());
  for(auto const& user : _users)
  {
    s32 best = 0;
    for(auto const& data : user.second) {
      best = std::max(best, ComputeScore(feature, data.second));
    }
    matches.emplace_back(best, user.first);
  }

  std::sort(matches.begin(), matches.end(),
            [](const std::pair<s32,s32>& a, const std::pair<s32,s32>& b) {
              return (a.first != b.first) ? (a.first > b.first) : (a.second < b.second);
            });

  numResults = std::min((s32)matches.size(), maxResults);
  for(s32 i = 0; i < numResults; ++i)
  {
    if(i >= (s32)userIds.size() || i >= (s32)scores.size()) {
      numResults = i;
      break;
    }
    scores[i]  = matches[i].first;
    userIds[i] = matches[i].second;
  }
}

Result FaceAlbum::Serialize(std::vector<u8>& buffer) const
{
  buffer.clear();

  Append(buffer, kSerializationMagic);
  Append(buffer, kSerializationVersion);
  Append(buffer, (u32)_users.size());

  for(auto const& user : _users)
  {
    Append(buffer, (s32)user.first);
    Append(buffer, (u32)user.second.size());
    for(auto const& data : user.second)
    {
      Append(buffer, (s32)data.first);
      Append(buffer, (u32)data.second.size());
      const u8* bytes = reinterpret_cast<const u8*>(data.second.data());
      buffer.insert(buffer.end(), bytes, bytes + data.second.size()*sizeof(f32));
    }
  }

  return RESULT_OK;
}

Result FaceAlbum::Deserialize(const std::vector<u8>& buffer)
{
  _users.clear();

  size_t index = 0;
  u32 magic = 0, version = 0, numUsers = 0;
  if(!Read(buffer, index, magic) || !Read(buffer, index, version) || !Read(buffer, index, numUsers)) {
    LOG_WARNING("FaceAlbum.Deserialize.TooShort", "%zu bytes", buffer.size());
    return RESULT_FAIL;
  }

  if(kSerializationMagic != magic || kSerializationVersion != version) {
    LOG_WARNING("FaceAlbum.Deserialize.BadHeader", "Magic:%08X Version:%u", magic, version);
    return RESULT_FAIL;
  }

  for(u32 iUser = 0; iUser < numUsers; ++iUser)
  {
    s32 userId = 0;
    u32 numData = 0;
    if(!Read(buffer, index, userId) || !Read(buffer, index, numData)) {
      LOG_WARNING("FaceAlbum.Deserialize.TruncatedUser", "User %u of %u", iUser, numUsers);
      return RESULT_FAIL;
    }

    for(u32 iData = 0; iData < numData; ++iData)
    {
      s32 dataId = 0;
      u32 featureSize = 0;
      if(!Read(buffer, index, dataId) || !Read(buffer, index, featureSize)) {
        LOG_WARNING("FaceAlbum.Deserialize.TruncatedData", "User:%d", userId);
        return RESULT_FAIL;
      }

      const size_t numBytes = (size_t)featureSize * sizeof(f32);
      if(index + numBytes > buffer.size()) {
        LOG_WARNING("FaceAlbum.Deserialize.TruncatedFeature", "User:%d Data:%d", userId, dataId);
        return RESULT_FAIL;
      }

      Feature feature(featureSize);
      memcpy(feature.data(), buffer.data() + index, numBytes);
      index += numBytes;

      _users[userId][dataId] = std::move(feature);
    }
  }

  LOG_INFO("FaceAlbum.Deserialize.Success", "%zu users, %d features from %zu bytes",
           _users.size(), GetRegisteredAllDataNum(), buffer.size());

  return RESULT_OK;
}

}
}
