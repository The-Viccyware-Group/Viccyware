/**
 * File: textToSpeechComponent.cpp
 *
 * Author: Various Artists
 *
 * Description: Component wrapper to generate, cache and use wave data from a given string and style.
 * This class provides a platform-independent interface to separate engine & audio libraries from
 * details of a specific text-to-speech implementation.
 *
 * Copyright: Anki, Inc. 2016-2018
 *
 */

#include "textToSpeechComponent.h"
#include "textToSpeechProvider.h"

#include "cozmoAnim/animContext.h"
#include "cozmoAnim/animProcessMessages.h"
#include "cozmoAnim/audio/cozmoAudioController.h"
#include "cozmoAnim/robotDataLoader.h"

#include "clad/robotInterface/messageRobotToEngine.h"
#include "clad/robotInterface/messageEngineToRobot.h"

#include "coretech/common/engine/utils/data/dataPlatform.h"

#include "audioEngine/audioCallback.h"
#include "audioEngine/audioTypeTranslator.h"
#include "audioEngine/plugins/ankiPluginInterface.h"
#include "audioEngine/plugins/streamingWavePortalPlugIn.h"
#include "util/console/consoleInterface.h"
#include "util/dispatchQueue/dispatchQueue.h"
#include "util/fileUtils/fileUtils.h"
#include "util/logging/logging.h"
#include "util/time/universalTime.h"

#include <memory>
#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <signal.h>

// Log options
#define LOG_CHANNEL "TextToSpeech"

#define TTS_SOCKET_PATH "/tmp/akalsasink_audio.sock"

namespace {

  // TTS audio always plays on robot device
  constexpr Anki::AudioMetaData::GameObjectType kTTSGameObject = Anki::AudioMetaData::GameObjectType::TextToSpeech;

  constexpr Anki::AudioEngine::PlugIns::StreamingWavePortalPlugIn::PluginId_t kTtsPluginId = 0;

   // How many frames do we need before utterance is playable?
  CONSOLE_VAR_RANGED(u32, kMinPlayableFrames, "TextToSpeech", 8192, 0, 65536);

  // Enable write to /tmp/tts.pcm?
  CONSOLE_VAR(bool, kWriteTTSFile, "TextToSpeech", false);

}

namespace Anki {
namespace Vector {

static int tts_socket_connect()
{
  signal(SIGPIPE, SIG_IGN);

  int sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    LOG_WARNING("TextToSpeechComponent.SocketConnect", "socket() failed: errno=%d (%s)", errno, strerror(errno));
    return -1;
  }

  struct sockaddr_un addr;
  ::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  ::strncpy(addr.sun_path, TTS_SOCKET_PATH, sizeof(addr.sun_path) - 1);

  if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    LOG_WARNING("TextToSpeechComponent.SocketConnect", "connect() failed: errno=%d (%s)", errno, strerror(errno));
    ::close(sock);
    return -1;
  }

  LOG_INFO("TextToSpeechComponent.SocketConnect", "Connected to %s", TTS_SOCKET_PATH);
  return sock;
}

static int tts_send_all(int sock, const void* buf, size_t len)
{
  const uint8_t* ptr = static_cast<const uint8_t*>(buf);
  size_t remaining = len;
  while (remaining > 0) {
    ssize_t n = ::send(sock, ptr, remaining, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) { continue; }
      LOG_WARNING("TextToSpeechComponent.SendAll", "send() failed: errno=%d (%s)", errno, strerror(errno));
      return -1;
    }
    ptr       += static_cast<size_t>(n);
    remaining -= static_cast<size_t>(n);
  }
  return 0;
}

static int tts_send_pcm(int sock, const int16_t* samples, size_t numSamples)
{
  if (numSamples == 0) { return 0; }
  return tts_send_all(sock, samples, numSamples * sizeof(int16_t));
}

static void tts_send_trailing_silence(int sock, int sampleRate)
{
  const int kSilenceMs = 250;
  const size_t numSamples = static_cast<size_t>((sampleRate * kSilenceMs) / 1000);
  if (numSamples == 0) { return; }

  std::vector<int16_t> silence(numSamples, 0);
  tts_send_pcm(sock, silence.data(), numSamples);
}

static bool AppendAudioData(int sock,
                            const TextToSpeech::TextToSpeechProviderData& ttsData,
                            bool done)
{
  // Enable this to inspect raw PCM
  if (kWriteTTSFile) {
    const auto num_samples = ttsData.GetNumSamples();
    const auto samples = ttsData.GetSamples();
    static int _fd = -1;
    if (_fd < 0) {
      const auto path = "/data/data/com.anki.victor/cache/tts.pcm";
      _fd = open(path, O_CREAT|O_RDWR|O_TRUNC, 0644);
    }
    if (num_samples > 0) {
      (void) write(_fd, samples, num_samples * sizeof(short));
    }
    if (done) {
      close(_fd);
      _fd = -1;
    }
  }

  if (ttsData.GetNumSamples() > 0) {
    const int16_t* samples  = ttsData.GetSamples();
    const size_t numSamples = ttsData.GetNumSamples();

    if (tts_send_pcm(sock, samples, numSamples) < 0) {
      LOG_ERROR("TextToSpeechComponent.AppendAudioData", "Socket send failed");
      return false;
    }
  }

  if (done) {
    const int sampleRate = ttsData.GetNumSamples() > 0 ? ttsData.GetSampleRate() : 16000;
    tts_send_trailing_silence(sock, sampleRate);
    LOG_DEBUG("TextToSpeechComponent.AppendAudioData", "Done producing data");
  }

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TextToSpeechComponent::TextToSpeechComponent(const Anim::AnimContext* context)
: _activeTTSID(kInvalidTTSID)
, _dispatchQueue(Util::Dispatch::Create("TtSpeechComponent"))
{
  DEV_ASSERT(nullptr != context, "TextToSpeechComponent.InvalidContext");
  DEV_ASSERT(nullptr != context->GetAudioController(), "TextToSpeechComponent.InvalidAudioController");

  _audioController  = context->GetAudioController();

  const Json::Value& tts_config = context->GetDataLoader()->GetTextToSpeechConfig();
  _pvdr = std::make_unique<TextToSpeech::TextToSpeechProvider>(context, tts_config);

} // TextToSpeechComponent()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TextToSpeechComponent::~TextToSpeechComponent()
{
  Util::Dispatch::Stop(_dispatchQueue);
  Util::Dispatch::Release(_dispatchQueue);
} // ~TextToSpeechComponent()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TextToSpeechComponent::PushEvent(const EventTuple & event)
{
  std::lock_guard<std::mutex> lock(_event_mutex);
  _event_queue.push_back(event);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TextToSpeechComponent::PopEvent(EventTuple & event)
{
  std::lock_guard<std::mutex> lock(_event_mutex);
  if (_event_queue.empty()) {
    return false;
  }
  event = std::move(_event_queue.front());
  _event_queue.pop_front();
  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Return a SWAG estimate of duration for a given text.
// Estimates are generous to avoid premature timeout.
f32 TextToSpeechComponent::GetEstimatedDuration_ms(const std::string & text)
{
  return (text.size() * 1000.f / 4.0f);
}

f32 TextToSpeechComponent::GetDuration_ms(const StreamingWaveDataPtr & waveData)
{
  return (waveData ? waveData->GetApproximateTimeReceived_sec() * 1000.f : 0.f);
}

f32 TextToSpeechComponent::GetDuration_ms(const BundlePtr & bundle)
{
  return (bundle ? GetDuration_ms(bundle->waveData) : 0.f);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result TextToSpeechComponent::CreateSpeech(const TTSID_t ttsID,
                                           const TextToSpeechTriggerMode triggerMode,
                                           const std::string& text,
                                           const AudioTtsProcessingStyle style,
                                           const float durationScalar,
                                           const float pitchScalar)
{
  // Prepare to generate TTS on other thread
  LOG_INFO("TextToSpeechComponent.CreateSpeech",
           "ttsID %d triggerMode %s text '%s' style '%s' durationScalar %.2f pitchScalar %.2f",
           ttsID, EnumToString(triggerMode), Util::HidePersonallyIdentifiableInfo(text.c_str()),
           EnumToString(style),
           durationScalar,
           pitchScalar);

  // Add Acapela Silence tag to remove trailing silence at end of audio stream
  // Trim white space
  std::size_t firstScan = text.find_first_not_of(' ');
  std::size_t first     = firstScan == std::string::npos ? text.length() : firstScan;
  std::size_t last      = text.find_last_not_of(' ');
  std::string ttsStr = text.substr(first, last-first+1);
  // Check punctuation . ! ?
  char lastChar = ttsStr[ttsStr.size() - 1];
  if (!(lastChar == '.' || lastChar == '?' || lastChar == '!')) {
    lastChar = '.'; // Set default
  }
  else {
    ttsStr.pop_back();
  }
  // Set trailing silence pause to 10 ms and add punctuation to the end of the string
  ttsStr += " \\pau=10\\";
  ttsStr.push_back(lastChar);

  // Get an empty data instance
  auto waveData = AudioEngine::PlugIns::StreamingWavePortalPlugIn::CreateDataInstance();

  {
    std::lock_guard<std::mutex> lock(_lock);
    const auto it = _bundleMap.emplace(ttsID, std::make_shared<TtsBundle>());
    if (!it.second) {
      LOG_ERROR("TextToSpeechComponent.CreateSpeech", "ttsID %d already in cache", ttsID);
      return RESULT_FAIL_INVALID_PARAMETER;
    }

    // Set initial state
    BundlePtr bundle = it.first->second;
    bundle->state = AudioCreationState::Preparing;
    bundle->triggerMode = triggerMode;
    bundle->style = style;
    bundle->waveData = waveData;
  }

  // Dispatch work onto another thread
  Util::Dispatch::Async(_dispatchQueue, [this, ttsID, ttsStr, durationScalar, pitchScalar, waveData]
  {
    int sock = tts_socket_connect();
    if (sock < 0) {
      LOG_ERROR("TextToSpeechComponent.CreateSpeech",
                "Unable to connect to TTS socket for ttsID %d", ttsID);
      PushEvent({ttsID, TextToSpeechState::Invalid, 0.f});
      return;
    }

    // Have we sent TextToSpeechState::Playable for this utterance?
    bool sentPlayable = false;
    bool done         = false;

    TextToSpeech::TextToSpeechProviderData ttsData;
    Result result = _pvdr->GetFirstAudioData(ttsStr, durationScalar, pitchScalar, ttsData, done);

    if (RESULT_OK != result) {
      LOG_ERROR("TextToSpeechComponent.CreateSpeech", "Unable to get first audio data (error %d)", result);
      ::close(sock);
      PushEvent({ttsID, TextToSpeechState::Invalid, 0.f});
      return;
    }

    if (!AppendAudioData(sock, ttsData, done)) {
      LOG_ERROR("TextToSpeechComponent.CreateSpeech",
                "Socket send failed on first chunk for ttsID %d", ttsID);
      ::close(sock);
      PushEvent({ttsID, TextToSpeechState::Invalid, 0.f});
      return;
    }

    size_t framesSent = ttsData.GetNumSamples();

    {
      std::lock_guard<std::mutex> lock(_lock);
      const auto bundle = GetBundle(ttsID);
      if (!bundle) {
        LOG_DEBUG("TextToSpeechComponent.CreateSpeech", "TTSID %d has been cancelled", ttsID);
        ::close(sock);
        return;
      }
      if (!sentPlayable && framesSent >= kMinPlayableFrames) {
          LOG_DEBUG("TextToSpeechComponent.CreateSpeech", "TTSID %d audio is ready to play", ttsID);
          const f32 duration_ms = GetEstimatedDuration_ms(ttsStr) * durationScalar;
          PushEvent({ttsID, TextToSpeechState::Playable, duration_ms});
          sentPlayable = true;
      }
    }

    while (result == RESULT_OK && !done) {
      ttsData = TextToSpeech::TextToSpeechProviderData{};

      result = _pvdr->GetNextAudioData(ttsData, done);
      if (RESULT_OK != result) {
        LOG_ERROR("TextToSpeechComponent.CreateSpeech", "Unable to get next audio data (error %d)", result);
        ::close(sock);
        PushEvent({ttsID, TextToSpeechState::Invalid, 0.f});
        return;
      }

      if (!AppendAudioData(sock, ttsData, done)) {
        LOG_ERROR("TextToSpeechComponent.CreateSpeech",
                  "Socket send failed mid-stream for ttsID %d", ttsID);
        ::close(sock);
        PushEvent({ttsID, TextToSpeechState::Invalid, 0.f});
        return;
      }

      framesSent += ttsData.GetNumSamples();

      {
        std::lock_guard<std::mutex> lock(_lock);
        const auto bundle = GetBundle(ttsID);
        if (!bundle) {
          LOG_DEBUG("TextToSpeechComponent.CreateSpeech", "TTSID %d has been cancelled", ttsID);
          ::close(sock);
          return;
        }
        if (!sentPlayable && framesSent >= kMinPlayableFrames) {
          LOG_DEBUG("TextToSpeechComponent.CreateSpeech", "TTSID %d audio is ready to play", ttsID);
          bundle->state = AudioCreationState::Playable;
          PushEvent({ttsID, TextToSpeechState::Playable, duration_ms});
          sentPlayable = true;
        }
      }
    }

    // Finalize data instance
    ::close(sock);
    LOG_DEBUG("TextToSpeechComponent.CreateSpeech", "Socket closed for ttsID %d", ttsID);

    {
      std::lock_guard<std::mutex> lock(_lock);
      auto bundle = GetBundle(ttsID);
      if (!bundle) {
        LOG_DEBUG("TextToSpeechComponent.CreateSpeech", "TTSID %d has been cancelled", ttsID);
        return;
      }

      const f32 duration_ms = (framesSent > 0 && ttsData.GetSampleRate() > 0)
        ? static_cast<f32>(framesSent) / static_cast<f32>(ttsData.GetSampleRate()) * 1000.f
        : GetEstimatedDuration_ms(ttsStr) * durationScalar;

      if (!sentPlayable) {
        LOG_DEBUG("TextToSpeechComponent.CreateSpeech", "TTSID %d audio is ready to play", ttsID);
        bundle->state = AudioCreationState::Playable;
        PushEvent({ttsID, TextToSpeechState::Playable, duration_ms});
        sentPlayable = true;
      }

      LOG_DEBUG("TextToSpeechComponent.CreateSpeech", "TTSID %d audio is complete", ttsID);
      bundle->state = AudioCreationState::Prepared;
      PushEvent({ttsID, TextToSpeechState::Prepared, duration_ms});
    }
  });

  return RESULT_OK;
} // CreateSpeech()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
// Deliver audio data to wwise audio engine
//
bool TextToSpeechComponent::PrepareAudioEngine(const TTSID_t ttsID,
                                               float& out_duration_ms)
{
  const auto ttsBundle = GetBundle(ttsID);
  if (nullptr == ttsBundle) {
    LOG_ERROR("TextToSpeechComponent.PrepareAudioEngine", "ttsID %u not found", ttsID);
    return false;
  }

  const auto state = ttsBundle->state;

  if (AudioCreationState::None == state) {
    LOG_WARNING("TextToSpeechComponent.PrepareAudioEngine.NoAudio", "ttsID %d audio not found", ttsID);
    return false;
  }

  if (AudioCreationState::Preparing == state) {
    LOG_WARNING("TextToSpeechComponent.PrepareAudioEngine.AudioPreparing", "ttsID %d audio not ready", ttsID);
    return false;
  }

  out_duration_ms = GetDuration_ms(ttsBundle);

  SetAudioProcessingStyle(ttsBundle->style);

  _activeTTSID = ttsID;

  return true;
} // PrepareAudioEngine()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TextToSpeechComponent::CleanupAudioEngine(const TTSID_t ttsID)
{
  LOG_INFO("TextToSpeechComponent.CleanupAudioEngine", "Clean up ttsID %d", ttsID);

  if (ttsID == _activeTTSID){
    StopActiveTTS();
    ClearActiveTTS();
  }

  // Clear operation data if needed
  if (kInvalidTTSID != ttsID) {
    ClearOperationData(ttsID);
  }
} // CleanupAudioEngine()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TextToSpeechComponent::ClearOperationData(const TTSID_t ttsID)
{
  LOG_INFO("TextToSpeechComponent.ClearOperationData", "Clear ttsID %u", ttsID);

  std::lock_guard<std::mutex> lock(_lock);
  const auto it = _bundleMap.find(ttsID);
  if (it != _bundleMap.end()) {
    const auto & bundle = it->second;
    const auto & waveData = bundle->waveData;
    if (waveData && waveData->IsPlayingStream()) {
      waveData->DoneProducingData();
    }
    _bundleMap.erase(it);
  }
} // ClearOperationData()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TextToSpeechComponent::ClearAllLoadedAudioData()
{
  LOG_INFO("TextToSpeechComponent.ClearAllLoadedAudioData", "Clear all data");

  std::lock_guard<std::mutex> lock(_lock);
  _bundleMap.clear();
} // ClearAllLoadedAudioData()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result TextToSpeechComponent::GetFirstAudioData(const std::string & text,
                                                float durationScalar,
                                                float pitchScalar,
                                                const StreamingWaveDataPtr & /*data*/,
                                                bool & done)
{
  TextToSpeech::TextToSpeechProviderData ttsData;
  const Result result = _pvdr->GetFirstAudioData(text, durationScalar, pitchScalar, ttsData, done);

  if (RESULT_OK != result) {
    LOG_ERROR("TextToSpeechComponent.GetFirstAudioData", "Unable to get first audio data (error %d)", result);
    return result;
  }

  return RESULT_OK;
} // GetFirstAudioData()

Result TextToSpeechComponent::GetNextAudioData(const StreamingWaveDataPtr & /*data*/, bool & done)
{
  TextToSpeech::TextToSpeechProviderData ttsData;
  const Result result = _pvdr->GetNextAudioData(ttsData, done);

  if (RESULT_OK != result) {
    LOG_ERROR("TextToSpeechComponent.GetNextAudioData", "Unable to get next audio data (error %d)", result);
    return result;
  }

  return RESULT_OK;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TextToSpeechComponent::BundlePtr TextToSpeechComponent::GetBundle(const TTSID_t ttsID)
{
  const auto iter = _bundleMap.find(ttsID);
  if (iter != _bundleMap.end()) {
    return iter->second;
  }
  return nullptr;
} // GetTtsBundle()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
// Set audio processing switch for next utterance
//
void TextToSpeechComponent::SetAudioProcessingStyle(AudioTtsProcessingStyle style)
{
  const auto switchGroup = AudioMetaData::SwitchState::SwitchGroupType::Robot_Vic_External_Processing;
  _audioController->SetSwitchState(
    static_cast<AudioEngine::AudioSwitchGroupId>(switchGroup),
    static_cast<AudioEngine::AudioSwitchStateId>(style),
    static_cast<AudioEngine::AudioGameObject>(kTTSGameObject)
  );
}

//
// Send a TextToSpeechEvent message from anim to engine.
// This is called on main thread for thread-safe access to comms.
//
static bool SendAnimToEngine(uint8_t ttsID, TextToSpeechState state, float expectedDuration = 0.0f)
{
  LOG_DEBUG("TextToSpeechComponent.SendAnimToEngine", "ttsID %hhu state %hhu", ttsID, state);
  TextToSpeechEvent evt;
  evt.ttsID = ttsID;
  evt.ttsState = state;
  evt.expectedDuration_ms = expectedDuration; // Used only on TextToSpeechState::Delivered messages
  return AnimProcessMessages::SendAnimToEngine(std::move(evt));
}

//
// Send audio trigger event for this utterance
//
bool TextToSpeechComponent::PostAudioEvent(uint8_t ttsID)
{
  const auto callbackFunc = std::bind(&TextToSpeechComponent::OnUtteranceCompleted, this, ttsID);
  auto * audioCallbackContext = new AudioEngine::AudioCallbackContext();

  // Set callback flags
  audioCallbackContext->SetCallbackFlags( AudioEngine::AudioCallbackFlag::Complete );
  // Execute callbacks synchronously (on main thread)
  audioCallbackContext->SetExecuteAsync( false );
  // Register callbacks for event
  audioCallbackContext->SetEventCallbackFunc ( [ callbackFunc = std::move(callbackFunc) ]
                                              ( const AudioEngine::AudioCallbackContext* thisContext,
                                                const AudioEngine::AudioCallbackInfo& callbackInfo )
                                              {
                                                callbackFunc();
                                              } );

  using AudioEvent = AudioMetaData::GameEvent::GenericEvent;
  const auto eventID = AudioEngine::ToAudioEventId( AudioEvent::Play__Robot_Vic__External_Voice_Text );
  const auto gameObject = static_cast<AudioEngine::AudioGameObject>( kTTSGameObject );
  const auto playingID = _audioController->PostAudioEvent(eventID, gameObject, audioCallbackContext);

  if (AudioEngine::kInvalidAudioPlayingId == playingID) {
    LOG_ERROR("TextToSpeechComponent.PostAudioEvent", "Failed to post eventID %u for ttsID %d", eventID, ttsID);
    return false;
  }

  LOG_DEBUG("TextToSpeechComponent.PostAudioEvent", "eventID %u ttsID %d playingID %d", eventID, ttsID, playingID);

  return true;
}

//
// Stop the currently playing tts
//
void TextToSpeechComponent::StopActiveTTS()
{
  LOG_DEBUG("TextToSpeechComponent.StopActiveTTS", "Stop active TTS");
  _audioController->StopAllAudioEvents(static_cast<AudioEngine::AudioGameObject>(kTTSGameObject));
}

//
// Clear data from currently playing tts
//
void TextToSpeechComponent::ClearActiveTTS()
{
  LOG_DEBUG("TextToSpeechComponent.ClearActiveTTS", "Clear active TTS");
  auto * plugin = _audioController->GetPluginInterface()->GetStreamingWavePortalPlugIn();
  if (plugin != nullptr) {
    plugin->ClearAudioData(kTtsPluginId);
  }
}

//
// Handle a callback from the AudioEngine indicating that the TtS Utterance has finished playing
//
void TextToSpeechComponent::OnUtteranceCompleted(uint8_t ttsID)
{
  _activeTTSID = kInvalidTTSID;

  LOG_DEBUG("TextToSpeechComponent.UtteranceCompleted", "Completion callback received for ttsID %hhu", ttsID);
  SendAnimToEngine(ttsID, TextToSpeechState::Finished);
  ClearOperationData(ttsID); // Cleanup operation's memory
}

//
// Called on main thread to handle incoming TextToSpeechPrepare
//
void TextToSpeechComponent::HandleMessage(const RobotInterface::TextToSpeechPrepare & msg)
{
  using Anki::Util::HidePersonallyIdentifiableInfo;

  // Unpack message fields
  const auto ttsID = msg.ttsID;
  const auto triggerMode = msg.triggerMode;
  const auto style = msg.style;
  const auto durationScalar = msg.durationScalar;
  const auto pitchScalar = msg.pitchScalar;
  const std::string text( reinterpret_cast<const char*>(msg.text) );

  LOG_DEBUG("TextToSpeechComponent.TextToSpeechPrepare",
            "ttsID %d triggerMode %s style %s durationScalar %.2f pitchScalar %.2f text %s",
            ttsID, EnumToString(triggerMode), EnumToString(style), durationScalar, pitchScalar,
            HidePersonallyIdentifiableInfo(text.c_str()));

  // Enqueue request on worker thread
  const Result result = CreateSpeech(ttsID, triggerMode, text, style, durationScalar, pitchScalar);
  if (RESULT_OK != result) {
    LOG_ERROR("TextToSpeechComponent.TextToSpeechPrepare", "Unable to create ttsID %d (result %d)", ttsID, result);
    SendAnimToEngine(ttsID, TextToSpeechState::Invalid);
    return;
  }

  // Execution continues in Update() when worker thread posts state change back to main thread

}

//
// Called on main thread to handle incoming TextToSpeechPlay
//
void TextToSpeechComponent::HandleMessage(const RobotInterface::TextToSpeechPlay& msg)
{
  const auto ttsID = msg.ttsID;

  LOG_DEBUG("TextToSpeechComponent.TextToSpeechPlay", "ttsID %d", ttsID);

  // Validate bundle
  const auto bundle = GetBundle(ttsID);
  if (!bundle) {
    LOG_ERROR("TextToSpeechComponent.TextToSpeechPlay", "ttsID %d not found", ttsID);
    SendAnimToEngine(ttsID, TextToSpeechState::Invalid);
    return;
  }

  // Validate trigger mode
  const auto triggerMode = bundle->triggerMode;
  if (triggerMode != TextToSpeechTriggerMode::Manual && triggerMode != TextToSpeechTriggerMode::Keyframe) {
    LOG_ERROR("TextToSpeechComponent.TextToSpeechPlay", "ttsID %d has unplayable trigger mode %s",
      ttsID, EnumToString(bundle->triggerMode));
    SendAnimToEngine(ttsID, TextToSpeechState::Invalid);
    ClearOperationData(ttsID);
    return;
  }

  // Enqueue audio
  float duration_ms = 0.f;
  if (!PrepareAudioEngine(ttsID, duration_ms)) {
    LOG_ERROR("TextToSpeechComponent.TextToSpeechDeliver", "Unable to prepare audio engine for ttsID %d", ttsID);
    SendAnimToEngine(ttsID, TextToSpeechState::Invalid);
    ClearOperationData(ttsID);
    return;
  }

  LOG_INFO("TextToSpeechComponent.TextToSpeechPlay", "ttsID %d will play for %.2f ms", ttsID, duration_ms);

  // Post audio event? For manual triggers, post event now and notify engine that playback is in progress.
  // For keyframe events, event will be posted by AnimationAudioClient and engine will be notified by
  // callback to OnAudioPlaying.
  if (triggerMode == TextToSpeechTriggerMode::Manual) {
    if (!PostAudioEvent(ttsID)) {
      LOG_ERROR("TextToSpeechComponent.TextToSpeechPlay", "Unable to post audio event for ttsID %d", ttsID);
      SendAnimToEngine(ttsID, TextToSpeechState::Invalid);
      CleanupAudioEngine(ttsID);
      return;
    }
    SendAnimToEngine(ttsID, TextToSpeechState::Playing, duration_ms);
  }

}

//
// Called on main thread to handle incoming TextToSpeechCancel
//
void TextToSpeechComponent::HandleMessage(const RobotInterface::TextToSpeechCancel& msg)
{
  const auto ttsID = msg.ttsID;

  LOG_DEBUG("TextToSpeechComponent.HandleMessage.TextToSpeechCancel", "ttsID %d", ttsID);

  CleanupAudioEngine(ttsID);

  // Notify engine that request is now invalid
  SendAnimToEngine(ttsID, TextToSpeechState::Invalid);
}

//
// Called by Update() in response to event from worker thread
//
void TextToSpeechComponent::OnStateInvalid(const TTSID_t ttsID)
{
  LOG_DEBUG("TextToSpeechComponent.OnStateInvalid", "ttsID %d", ttsID);

  // Notify engine that tts request has failed
  SendAnimToEngine(ttsID, TextToSpeechState::Invalid);

  // Clean up request state
  ClearOperationData(ttsID);
}

//
// Called by Update() in response to event from worker thread
//
void TextToSpeechComponent::OnStatePreparing(const TTSID_t ttsID)
{
  LOG_DEBUG("TextToSpeechComponent.OnStatePreparing", "ttsID %d", ttsID);

  const auto bundle = GetBundle(ttsID);
  if (!bundle) {
    LOG_DEBUG("TextToSpeechComponent.OnStatePreparing", "ttsID %d has been cancelled", ttsID);
    return;
  }

  // Notify engine that tts request is being prepared.
  SendAnimToEngine(ttsID, TextToSpeechState::Preparing);
}

//
// Called by Update() in response to event from worker thread
//
void TextToSpeechComponent::OnStatePlayable(const TTSID_t ttsID, f32 duration_ms)
{
  LOG_DEBUG("TextToSpeechComponent.OnStatePlayable", "ttsID %d duration %.2f", ttsID, duration_ms);

  const auto bundle = GetBundle(ttsID);
  if (!bundle) {
    LOG_DEBUG("TextToSpeechComponent.OnStatePlayable", "ttsID %d has been cancelled", ttsID);
    return;
  }

  // Notify engine that tts request is now playable.
  SendAnimToEngine(ttsID, TextToSpeechState::Playable, duration_ms);

  //
  // For immediate triggers, enqueue audio for playback and post trigger event
  // as soon as audio becomes playable.
  //
  // Audio generation continues on the worker thread.  New audio frames are
  // added to the data instance as they become available.
  //
  // When audio playback is complete, the audio engine invokes a callback
  // to clean up operation data.
  //
  if (bundle->triggerMode == TextToSpeechTriggerMode::Immediate) {
    if (!PrepareAudioEngine(ttsID, duration_ms)) {
      LOG_ERROR("TextToSpeechComponent.OnStatePlayable", "Unable to prepare audio for ttsID %d", ttsID);
      SendAnimToEngine(ttsID, TextToSpeechState::Invalid);
      ClearOperationData(ttsID);
      return;
    }
    if (!PostAudioEvent(ttsID)) {
      LOG_ERROR("TextToSpeechComponent.OnStatePlayable", "Unable to post audio event for ttsID %d", ttsID);
      SendAnimToEngine(ttsID, TextToSpeechState::Invalid);
      ClearOperationData(ttsID);
      return;
    }
    LOG_INFO("TextToSpeech.OnStatePlayable", "ttsID %d will play for at least %.2f ms", ttsID, duration_ms);
    SendAnimToEngine(ttsID, TextToSpeechState::Playing, duration_ms);
  }
}

//
// Called by Update() in response to event from worker thread
//
void TextToSpeechComponent::OnStatePrepared(const TTSID_t ttsID, f32 duration_ms)
{
  LOG_DEBUG("TextToSpeechComponent.OnStatePrepared", "ttsID %d duration %.2f", ttsID, duration_ms);

  const auto bundle = GetBundle(ttsID);
  if (!bundle) {
    LOG_DEBUG("TextToSpeechComponent.OnStatePrepared", "ttsID %d has been cancelled", ttsID);
    return;
  }

  // Notify engine that tts request has been prepared
  SendAnimToEngine(ttsID, TextToSpeechState::Prepared, duration_ms);

}

//
// Called by main thread (once per tick) to handle events posted by worker thread
//
void TextToSpeechComponent::Update()
{
  EventTuple event;

  // Process events posted by worker thread
  while (PopEvent(event)) {
    const auto ttsID = std::get<0>(event);
    const auto ttsState = std::get<1>(event);
    const auto duration_ms = std::get<2>(event);

    LOG_DEBUG("TextToSpeechComponent.Update", "Event ttsID %d state %hhu duration %f", ttsID, ttsState, duration_ms);

    switch (ttsState) {
      case TextToSpeechState::Invalid:
        OnStateInvalid(ttsID);
        break;
      case TextToSpeechState::Preparing:
        OnStatePreparing(ttsID);
        break;
      case TextToSpeechState::Playable:
        OnStatePlayable(ttsID, duration_ms);
        break;
      case TextToSpeechState::Prepared:
        OnStatePrepared(ttsID, duration_ms);
        break;
      default:
        //
        // We don't expect any other events from the worker thread.  Transition to Delivering/Delivered are
        // handled by the main thread.
        //
        LOG_ERROR("TextToSpeechComponent.Update.UnexpectedState", "Event ttsID %d unexpected state %u",
                  ttsID, static_cast<uint8_t>(ttsState));
        break;
    }
  }
}

void TextToSpeechComponent::SetLocale(const std::string & locale)
{
  //
  // Perform callback on worker thread so locale is changed in sync with TTS processing.
  // Any TTS operations queued before SetLocale() will be processed with old locale.
  // Any TTS operations queued after SetLocale() will be processed with new locale.
  //
  LOG_DEBUG("TextToSpeechComponent.SetLocale", "Set locale to %s", locale.c_str());

  const auto & task = [this, locale = std::string(locale)] {
    DEV_ASSERT(_pvdr != nullptr, "TextToSpeechComponent.SetLocale.InvalidProvider");
    LOG_DEBUG("TextToSpeechComponent.SetLocale", "Setting locale to %s", locale.c_str());
    const Result result = _pvdr->SetLocale(locale);
    if (result != RESULT_OK) {
      LOG_ERROR("TextToSpeechComponent.SetLocale", "Unable to set locale to %s (error %d)", locale.c_str(), result);
    }
  };

  Util::Dispatch::Async(_dispatchQueue, task);

}

//
// Called by audio engine to handle keyframe playback start
//
void TextToSpeechComponent::OnAudioPlaying(const TTSID_t ttsID)
{
  LOG_DEBUG("TextToSpeechComponent.OnAudioPlaying", "Now playing ttsID %d", ttsID);
  auto bundle = GetBundle(ttsID);
  if (!bundle) {
    LOG_ERROR("TextToSpeechComponent.OnAudioPlaying", "ttsID %d not found", ttsID);
    return;
  }

  // Notify engine that TTS is now playing
  SendAnimToEngine(ttsID, TextToSpeechState::Playing, GetDuration_ms(bundle));

}

//
// Called by audio engine to handle keyframe playback complete
//
void TextToSpeechComponent::OnAudioComplete(const TTSID_t ttsID)
{
  LOG_DEBUG("TextToSpeechComponent.OnAudioComplete", "Finished playing ttsID %d", ttsID);
  auto bundle = GetBundle(ttsID);
  if (!bundle) {
    LOG_ERROR("TextToSpeechComponent.OnAudioComplete", "ttsID %d not found", ttsID);
    return;
  }

  // Notify engine that TTS is complete
  SendAnimToEngine(ttsID, TextToSpeechState::Finished);
  ClearOperationData(ttsID);

}

//
// Called by audio engine to handle keyframe playback error
//
void TextToSpeechComponent::OnAudioError(const TTSID_t ttsID)
{
  LOG_DEBUG("TextToSpeechComponent.OnAudioError", "Error playing ttsID %d ", ttsID);
  auto bundle = GetBundle(ttsID);
  if (!bundle) {
    LOG_ERROR("TextToSpeechComponent.OnAudioError", "ttsID %d not found", ttsID);
    return;
  }

  SendAnimToEngine(ttsID, TextToSpeechState::Invalid);
  ClearOperationData(ttsID);

}

} // end namespace Vector
} // end namespace Anki
