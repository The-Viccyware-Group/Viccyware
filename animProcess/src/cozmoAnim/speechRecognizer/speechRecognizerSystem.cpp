/**
 * File: speechRecognizerSystem.cpp
 *
 * Author: Jordan Rivas
 * Created: 10/23/2018
 *
 * Description: Speech Recognizer System handles high level speech features, such as, locale and multiple triggers
 *
 * Copyright: Anki, Inc. 2018
 *
 */

#include "cozmoAnim/speechRecognizer/speechRecognizerSystem.h"

#include "audioUtil/speechRecognizer.h"
#include "cozmoAnim/alexa/alexa.h"
#include "cozmoAnim/alexa/media/alexaPlaybackRecognizerComponent.h"
#include "cozmoAnim/animContext.h"
#include "cozmoAnim/micData/micDataSystem.h"
#include "cozmoAnim/robotDataLoader.h"
#include "speechRecognizerPicovoice.h"                        // swapped in Picovoice
#include "cozmoAnim/speechRecognizer/speechRecognizerPryonLite.h"
#include "cozmoAnim/micData/notchDetector.h"
#include "util/console/consoleInterface.h"
#include "util/console/consoleFunction.h"
#include "util/cpuProfiler/cpuProfiler.h"
#include "util/environment/locale.h"
#include "util/fileUtils/fileUtils.h"
#include "util/logging/logging.h"
#include <list>

#include <fcntl.h>
#include <unistd.h>


namespace Anki {
namespace Vector {

// VIC-13319 remove
CONSOLE_VAR_EXTERN(bool, kAlexaEnabledInUK);
CONSOLE_VAR_EXTERN(bool, kAlexaEnabledInAU);
  
namespace {
#define LOG_CHANNEL "SpeechRecognizer"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Console Vars
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#if ANKI_DEV_CHEATS
#define CONSOLE_GROUP_VECTOR "SpeechRecognizer.Vector"
#define CONSOLE_GROUP_ALEXA "SpeechRecognizer.Alexa"

using MicConfigModelType = MicData::MicTriggerConfig::ModelType;
struct TriggerModelTypeData
{
  Util::Locale          locale;
  MicConfigModelType    modelType;
  int                   searchFileIndex;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Sendory Truly Hands Free recognizer models
// NOTE: This enum needs to EXACTLY match the number and ordering of the kThfTriggerModelDataList array below
enum class SupportedThfLocales
{
  enUS_1mb, // default
  enUS_500kb,
  enUS_250kb,
  enUS_Alt_1mb,
  enUS_Alt_500kb,
  enUS_Alt_250kb,
  enUK_1mb,
  enUK_500kb,
  enAU_1mb,
  enAU_500kb,
  frFR,
  deDE,
  Count
};

// NOTE: This array needs to EXACTLY match the number and ordering of the SupportedThfLocales enum above
const TriggerModelTypeData kThfTriggerModelDataList[] =
{
  // Easily selectable values for consolevar dropdown. Note 'Count' and '-1' values indicate to use default
  // We are using delivery 1 as our defualt enUS model
  { .locale = Util::Locale("en","US"), .modelType = MicConfigModelType::size_1mb, .searchFileIndex = -1 },
  { .locale = Util::Locale("en","US"), .modelType = MicConfigModelType::size_500kb, .searchFileIndex = -1 },
  { .locale = Util::Locale("en","US"), .modelType = MicConfigModelType::size_250kb, .searchFileIndex = -1 },
  // This is a hack to add a second en_US model, it will appear in console vars as `enUS_Alt_1mb`
  // This is delivery 2 model
  { .locale = Util::Locale("en","ZW"), .modelType = MicConfigModelType::size_1mb, .searchFileIndex = -1 },
  { .locale = Util::Locale("en","ZW"), .modelType = MicConfigModelType::size_500kb, .searchFileIndex = -1 },
  { .locale = Util::Locale("en","ZW"), .modelType = MicConfigModelType::size_250kb, .searchFileIndex = -1 },
  // Other Locales
  { .locale = Util::Locale("en","GB"), .modelType = MicConfigModelType::size_1mb, .searchFileIndex = -1 },
  { .locale = Util::Locale("en","GB"), .modelType = MicConfigModelType::size_500kb, .searchFileIndex = -1 },
  { .locale = Util::Locale("en","AU"), .modelType = MicConfigModelType::size_1mb, .searchFileIndex = -1 },
  { .locale = Util::Locale("en","AU"), .modelType = MicConfigModelType::size_500kb, .searchFileIndex = -1 },
  { .locale = Util::Locale("fr","FR"), .modelType = MicConfigModelType::Count, .searchFileIndex = -1 },
  { .locale = Util::Locale("de","DE"), .modelType = MicConfigModelType::Count, .searchFileIndex = -1 },
};
constexpr size_t kThfTriggerDataListLen = sizeof(kThfTriggerModelDataList) / sizeof(kThfTriggerModelDataList[0]);
static_assert(kThfTriggerDataListLen == (size_t) SupportedThfLocales::Count, "Need trigger data for each supported locale");

const char* kThfRecognizerModelStr = "enUS_1mb, enUS_500kb, enUS_250kb, \
                                      enUS_Alt_1mb, enUS_Alt_500kb, enUS_Alt_250kb, \
                                      enUK_1mb, enUK_500kb, enAU_1mb, enAU_500kb, frFR, deDE";
const char* kThfRecognizerModelSensitivityStr = "default,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20";

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Pryon recognizer models
// NOTE: This enum needs to EXACTLY match the number and ordering of the kPryonTriggerModelDataList array below
enum class SupportedPryonLocales
{
  enUS, // default
  enUK,
  enAU,
  frFR,
  deDE,
  Count
};
// NOTE: This array needs to EXACTLY match the number and ordering of the SupportedPryonLocales enum above
const TriggerModelTypeData kPryonTriggerModelDataList[] =
{
  // Easily selectable values for consolevar dropdown. Note 'Count' and '-1' values indicate to use default
  // We are using delivery 1 as our defualt enUS model
  { .locale = Util::Locale("en","US"), .modelType = MicConfigModelType::Count, .searchFileIndex = -1 },
  { .locale = Util::Locale("en","GB"), .modelType = MicConfigModelType::Count, .searchFileIndex = -1 },
  { .locale = Util::Locale("en","AU"), .modelType = MicConfigModelType::Count, .searchFileIndex = -1 },
  { .locale = Util::Locale("fr","FR"), .modelType = MicConfigModelType::Count, .searchFileIndex = -1 },
  { .locale = Util::Locale("de","DE"), .modelType = MicConfigModelType::Count, .searchFileIndex = -1 },
};
constexpr size_t kPryonTriggerDataListLen = sizeof(kPryonTriggerModelDataList) / sizeof(kPryonTriggerModelDataList[0]);
static_assert(kPryonTriggerDataListLen == (size_t) SupportedPryonLocales::Count, "Need trigger data for each supported locale");
const char* kPryonRecognizerModelStr = "enUS, enUK, enAU, frFR, deDE";
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
size_t _vectorRecognizerModelTypeIndex = (size_t) SupportedThfLocales::enUS_500kb;
CONSOLE_VAR_ENUM(size_t, kVectorRecognizerModel, CONSOLE_GROUP_VECTOR, _vectorRecognizerModelTypeIndex, kThfRecognizerModelStr);

int _vectorTriggerModelSensitivityIndex = 0;
CONSOLE_VAR_ENUM(int, kVectorRecognizerModelSensitivity, CONSOLE_GROUP_VECTOR, _vectorTriggerModelSensitivityIndex,
                 kThfRecognizerModelSensitivityStr);
  
size_t _alexaRecognizerModelTypeIndex = (size_t) SupportedPryonLocales::enUS;
CONSOLE_VAR_ENUM(size_t, kAlexaRecognizerModel, CONSOLE_GROUP_ALEXA, _alexaRecognizerModelTypeIndex, kPryonRecognizerModelStr);

#define CONSOLE_GROUP_ALEXA_PLAYBACK "SpeechRecognizer.AlexaPlayback"
size_t _alexaPlaybackRecognizerModelTypeIndex = (size_t) SupportedPryonLocales::enUS;
CONSOLE_VAR_ENUM(size_t, kAlexaPlaybackRecognizerModel, CONSOLE_GROUP_ALEXA_PLAYBACK,
                 _alexaPlaybackRecognizerModelTypeIndex, kPryonRecognizerModelStr);

std::list<Anki::Util::IConsoleFunction> sConsoleFuncs;

#endif // ANKI_DEV_CHEATS

CONSOLE_VAR(bool, kSaveRawMicInput, CONSOLE_GROUP_ALEXA, false);
// 0: don't run; 1: compute power as if _notchDetectorActive; 2: analyze power every tick
CONSOLE_VAR_RANGED(unsigned int, kForceRunNotchDetector, CONSOLE_GROUP_ALEXA, 0, 0, 2);
  
CONSOLE_VAR_RANGED(uint, kPlaybackRecognizerSampleCountThreshold, CONSOLE_GROUP_ALEXA_PLAYBACK, 5000, 1000, 10000);
  
bool AlexaLocaleEnabled(const Util::Locale& locale)
{
  if (locale.GetCountry() == Util::Locale::CountryISO2::US) {
    return true;
  }
  else if (locale.GetCountry() == Util::Locale::CountryISO2::GB) {
    return kAlexaEnabledInUK;
  }
  else if (locale.GetCountry() == Util::Locale::CountryISO2::AU) {
    return kAlexaEnabledInAU;
  }
  else {
    return false;
  }
}
bool AlexaLocaleUsesVad(const Util::Locale& locale)
{
  
  if ((locale.GetCountry() == Util::Locale::CountryISO2::GB) || (locale.GetCountry() == Util::Locale::CountryISO2::AU)) {
    // the smaller model we currently use for GB and AU has a problematic VAD. For certain utterances, after alexa
    // finishes responding, the VAD indicator flickers on, off, and back on. If you then play a new alexa wake word,
    // the VAD indicator switches off, and the wake word is ignored. There's no evidence of this happening for the
    // larger US model.
    // TODO (VIC-13413): Have amazon fix the VAD. Maybe a larger model would help too.
    return false;
  }
  else {
    return true;
  }
}

} // namespace

void SpeechRecognizerSystem::SetupConsoleFuncs()
{
#if ANKI_DEV_CHEATS
  // Update Recognizer Model with kRecognizerModel & kRecognizerModelSensitivity enums
  auto updateVectorRecognizerModel = [this](ConsoleFunctionContextRef context) {
    if (!_victorTrigger) {
      context->channel->WriteLog("'Hey Vector' Trigger is not active");
      return;
    }
    std::string result = UpdateRecognizerHelper(_vectorRecognizerModelTypeIndex, kVectorRecognizerModel,
                                                _vectorTriggerModelSensitivityIndex, kVectorRecognizerModelSensitivity,
                                                kThfTriggerModelDataList, *_victorTrigger.get());
    context->channel->WriteLog("Update Vector Recognizer %s", result.c_str());
  };

  auto updateAlexaRecognizerModel = [this](ConsoleFunctionContextRef context) {
    if (!_alexaTrigger) {
      context->channel->WriteLog("'Alexa' Trigger is not active");
      return;
    }
    int tmpTriggerModelSensitivityIndex = 0;
    int tmpNewTriggerModelSensitivityIndex = 0;
    std::string result = UpdateRecognizerHelper(_alexaRecognizerModelTypeIndex, kAlexaRecognizerModel,
                                                tmpTriggerModelSensitivityIndex, tmpNewTriggerModelSensitivityIndex,
                                                kPryonTriggerModelDataList, *_alexaTrigger.get());
    context->channel->WriteLog("Update Alexa Recognizer %s", result.c_str());
  };

  auto updateAlexaPlaybackRecognizerModel = [this](ConsoleFunctionContextRef context) {
    if (!_alexaPlaybackTrigger) {
      context->channel->WriteLog("'Alexa' Playback Trigger is not active");
      return;
    }
    int tmpTriggerModelSensitivityIndex = 0;
    int tmpNewTriggerModelSensitivityIndex = 0;
    std::string result = UpdateRecognizerHelper(_alexaPlaybackRecognizerModelTypeIndex, kAlexaPlaybackRecognizerModel,
                                                tmpTriggerModelSensitivityIndex, tmpNewTriggerModelSensitivityIndex,
                                                kPryonTriggerModelDataList, *_alexaPlaybackTrigger.get());
    context->channel->WriteLog("Update Alexa Playback Recognizer %s", result.c_str());
  };

  sConsoleFuncs.emplace_front("Update Vector Recognizer", std::move(updateVectorRecognizerModel),
                              CONSOLE_GROUP_VECTOR, "");
  sConsoleFuncs.emplace_front("Update Alexa Recognizer", std::move(updateAlexaRecognizerModel),
                              CONSOLE_GROUP_ALEXA, "");
  sConsoleFuncs.emplace_front("Update Alexa Playback Recognizer", std::move(updateAlexaPlaybackRecognizerModel),
                              CONSOLE_GROUP_ALEXA_PLAYBACK, "");
#endif
  _micDataSystem->GetSpeakerLatency_ms(); // Fix compiler error when ANKI_DEV_CHEATS is not enabled
}

template <class SpeechRecognizerType>
std::string SpeechRecognizerSystem::UpdateRecognizerHelper(size_t& inOut_modelIdx, size_t new_modelIdx,
                                                           int& inOut_searchIdx, int new_searchIdx,
                                                           const TriggerModelTypeData modelTypeDataList[],
                                                           TriggerContext<SpeechRecognizerType>& trigger)
{
  std::string result;
#if ANKI_DEV_CHEATS
  if ((inOut_modelIdx != new_modelIdx) ||
      (inOut_searchIdx != new_searchIdx))
  {
    inOut_modelIdx = new_modelIdx;
    inOut_searchIdx = new_searchIdx;
    const auto& newTypeData = modelTypeDataList[new_modelIdx];
    _micDataSystem->SetLocaleDevOnly(newTypeData.locale); // FIXME: Don't think we want this since there are multiple recognizers that use different locales
    const int sensitivitySearchFileIdx = (new_searchIdx == 0) ?
                                         newTypeData.searchFileIndex : new_searchIdx;
    
    const bool success = UpdateTriggerForLocale(trigger,
                                                newTypeData.locale,
                                                newTypeData.modelType,
                                                sensitivitySearchFileIdx);
    if (success && (trigger.nextTriggerPaths._netFile.empty())) {
      result = "Recognizer modle or search file was not found, therefore, recognizer was cleared";
    }
    else {
      result = (success ? "success!" : "fail :(");
    }
  }
#endif
  return result;
}

SpeechRecognizerSystem::SpeechRecognizerSystem(const Anim::AnimContext* context,
                                               MicData::MicDataSystem* micDataSystem,
                                               const std::string& triggerWordDataDir)
: _context(context)
, _micDataSystem(micDataSystem)
, _triggerWordDataDir(triggerWordDataDir)
, _notchDetector(std::make_shared<NotchDetector>())
{
  SetupConsoleFuncs();
}

SpeechRecognizerSystem::~SpeechRecognizerSystem()
{
  if (_victorTrigger) {
    _victorTrigger->recognizer->Stop();
  }
  if (_alexaTrigger) {
    _alexaTrigger->recognizer->Stop();
  }
  
  // Best way to destroy Alexa recognizer and component
  DisableAlexa();
}

void SpeechRecognizerSystem::InitVector(const Anim::RobotDataLoader& dataLoader,
                                        const Util::Locale& locale,
                                        TriggerWordDetectedCallback callback)
{
  if (_victorTrigger) {
    LOG_WARNING("SpeechRecognizerSystem.InitVector", "Victor Recognizer is already running");
    return;
  }
  
  const bool useVad = true;
  _victorTrigger = std::make_unique<TriggerContext<SpeechRecognizerPicovoice>>("Vector", useVad);
  _victorTrigger->recognizer->Init();  
  _victorTrigger->recognizer->SetCallback(callback);
  //_victorTrigger->recognizer->Start();
  _victorTrigger->micTriggerConfig->Init("hey_vector_thf", dataLoader.GetMicTriggerConfig());
  
#if ANKI_DEVELOPER_CODE
  const auto& triggerDataList = _victorTrigger->micTriggerConfig->GetAllTriggerModelFiles();
  for (const auto& filePath : triggerDataList) {
    const auto& fullFilePath = Util::FileUtils::FullFilePath( {_triggerWordDataDir, filePath} );
    if (Util::FileUtils::FileDoesNotExist(fullFilePath)) {
      LOG_WARNING("SpeechRecognizerSystem.InitVector.MicTriggerConfigFileMissing","%s",fullFilePath.c_str());
    }
  }
#endif
  
  UpdateTriggerForLocale(locale, RecognizerTypeFlag::VectorMic);
}

void SpeechRecognizerSystem::ToggleNotchDetector(bool active)
{
  _notchDetectorActive = active;
}

void SpeechRecognizerSystem::UpdateNotch(const AudioUtil::AudioSample* audioChunk, unsigned int audioDataLen)
{
  {
    std::lock_guard<std::mutex> lg{_notchMutex};
    const bool analyzeSamples = _notchDetectorActive || (kForceRunNotchDetector != 0);
    _notchDetector->AddSamples(audioChunk, audioDataLen/MicData::kNumInputChannels, analyzeSamples);
    if ( kForceRunNotchDetector == 2 ) {
      _notchDetector->HasNotch();
    }
  }
  
  if( ANKI_DEV_CHEATS ) {
    static int pcmfd = -1;
    if( (pcmfd < 0) && kSaveRawMicInput ) {
      const auto path = "/data/data/com.anki.victor/cache/speechRecognizerRaw.pcm";
      pcmfd = open( path, O_CREAT|O_RDWR|O_TRUNC, 0644 );
    }
    
    if( pcmfd >= 0 ) {
      std::vector<short> toSave;
      toSave.resize(audioDataLen/MicData::kNumInputChannels);
      for( unsigned int i=0, idx=0; i<audioDataLen; i+=MicData::kNumInputChannels, ++idx ) {
        toSave[idx] = audioChunk[i];
      }
      (void) write( pcmfd, toSave.data(), toSave.size() * sizeof(short) );
      if( !kSaveRawMicInput ) {
        close( pcmfd );
        pcmfd = -1;
      }
    }
  }
}

void SpeechRecognizerSystem::Update(const AudioUtil::AudioSample * audioData, unsigned int audioDataLen, bool vadActive)
{
  if (_isPendingLocaleUpdate) {
    ApplyLocaleUpdate();
  }
  // Update recognizer
  if (_victorTrigger && (vadActive || !_victorTrigger->useVad)) {
    _victorTrigger->recognizer->Update(audioData, audioDataLen);
  }
  
  if (_isAlexaActive) {
    if (!_isDisableAlexaPending) {
      _alexaComponent->AddMicrophoneSamples(audioData, audioDataLen);
      _alexaTrigger->recognizer->Update(audioData, audioDataLen);
    }
    else {
      if (_alexaTrigger) {
        _alexaTrigger->recognizer->Stop();
        _alexaTrigger.reset();
      }
      UpdateAlexaActiveState();
      ASSERT_NAMED(!_isAlexaActive, "SpeechRecognizerSystem.DisableAlexa._isAlexaActive.IsTrue");
      _isDisableAlexaPending = false;
      LOG_INFO("SpeechRecognizerSystem.Update", "Alexa mic recognizer has been disabled");
    }
  }
}

bool SpeechRecognizerSystem::UpdateTriggerForLocale(const Util::Locale& newLocale, RecognizerTypeFlag recognizerFlags)
{
  // Set local using defualt locale settings
  bool success = false;
  // if (_victorTrigger &&
  //     (RecognizerTypeFlag::VectorMic & recognizerFlags) == RecognizerTypeFlag::VectorMic) {
  //   success = UpdateTriggerForLocale(*_victorTrigger.get(), newLocale, MicData::MicTriggerConfig::ModelType::Count, -1);
  // }
  
  if (AlexaLocaleEnabled(newLocale)) {
    if (_alexaTrigger &&
        ((RecognizerTypeFlag::AlexaMic & recognizerFlags) == RecognizerTypeFlag::AlexaMic)) {
      _alexaTrigger->useVad = AlexaLocaleUsesVad(newLocale);
      success &= UpdateTriggerForLocale(*_alexaTrigger.get(), newLocale, MicData::MicTriggerConfig::ModelType::Count, -1);
    }
    
    if (_alexaPlaybackTrigger &&
        ((RecognizerTypeFlag::AlexaPlayback & recognizerFlags) == RecognizerTypeFlag::AlexaPlayback)) {
      success &= UpdateTriggerForLocale(*_alexaPlaybackTrigger.get(), newLocale, MicData::MicTriggerConfig::ModelType::Count, -1);
      if (_alexaPlaybackRecognizerComponent) {
        _alexaPlaybackRecognizerComponent->PendingLocaleUpdate();
      }
      else {
        LOG_ERROR("SpeechRecognizerSystem.UpdateTriggerForLocale._alexaPlaybackRecognizerComponent.isNull", "");
      }
    }
  }
  
  return success;
}

void SpeechRecognizerSystem::ActivateAlexa(const Util::Locale& locale, AlexaTriggerWordDetectedCallback callback)
{
  if (_isAlexaActive) {
    LOG_WARNING("SpeechRecognizerSystem.ActivateAlexa",
                "Alexa is already active, must call DisableAlexa() to change state");
    return;
  }
  
  _alexaComponent = _context->GetAlexa();
  
  InitAlexa(locale, callback);
  
  _alexaPlaybackRecognizerComponent.reset(new AlexaPlaybackRecognizerComponent(_context, *this));
  
  const auto playbackRecognizerCallback = [this](const AudioUtil::SpeechRecognizerCallbackInfo& info)
  {
    _playbackTrigerSampleIdx = _alexaComponent->GetMicrophoneSampleIndex();
  };
  InitAlexaPlayback(locale, playbackRecognizerCallback);

  if (!_alexaPlaybackRecognizerComponent->Init()) {
    _alexaPlaybackRecognizerComponent.reset();
    LOG_ERROR("SpeechRecognizerSystem.ActivateAlexa._alexaPlaybackRecognizerComponent.Init.Failed", "");
  }
  
  UpdateAlexaActiveState();
}

void SpeechRecognizerSystem::DisableAlexa()
{
  _isDisableAlexaPending = true;
  
  if (_alexaPlaybackRecognizerComponent) {
    _alexaPlaybackRecognizerComponent.reset();
  }
  
  if (_alexaPlaybackTrigger) {
    _alexaPlaybackTrigger->recognizer->Stop();
    _alexaPlaybackTrigger.reset();
  }
}

void SpeechRecognizerSystem::SetAlexaSpeakingState(bool isSpeaking)
{
  if (_alexaPlaybackRecognizerComponent) {
    _alexaPlaybackRecognizerComponent->SetRecognizerActivate(isSpeaking);
  }
}

void SpeechRecognizerSystem::InitAlexa(const Util::Locale& locale,
                                       const AlexaTriggerWordDetectedCallback callback)
{
  if (_alexaTrigger) {
    LOG_WARNING("SpeechRecognizerSystem.InitAlexa", "Alexa Recognizer is already running");
    return;
  }
  
  const auto wrappedCallback = [callback=std::move(callback), this](const AudioUtil::SpeechRecognizerCallbackInfo& info)
  {
    AudioUtil::SpeechRecognizerIgnoreReason ignoreReason;
    if (_notchDetectorActive || kForceRunNotchDetector) {
      std::lock_guard<std::mutex> lg{_notchMutex};
      ignoreReason.notch = _notchDetector->HasNotch();
    }
    const auto diff = info.endSampleIndex - _playbackTrigerSampleIdx;
    ignoreReason.playback = (diff <= kPlaybackRecognizerSampleCountThreshold);
    
    if (ignoreReason) {
      LOG_INFO("SpeechRecognizerSystem.InitAlexaCallback.Ignored",
               "Alexa wake word contained a notch '%c' or playback recognizer '%c'"
               " samples between playback and user recognizers %llu samples | %llu ms",
               ignoreReason.notch ? 'Y' : 'N', ignoreReason.playback ? 'Y' : 'N', diff, (diff/16));
    }
    callback(info, ignoreReason);
  };
  
  _alexaComponent = _context->GetAlexa();
  const auto dataLoader = _context->GetDataLoader();
  ASSERT_NAMED(_alexaComponent != nullptr, "SpeechRecognizerSystem.InitAlexa._context.GetAlexa.IsNull");
  
  const bool useVad = AlexaLocaleUsesVad(locale);
  _alexaTrigger = std::make_unique<TriggerContext<SpeechRecognizerPryonLite>>("Alexa", useVad);
  _alexaTrigger->recognizer->SetCallback(wrappedCallback);
  _alexaTrigger->micTriggerConfig->Init("alexa_pryon", dataLoader->GetMicTriggerConfig());
  _alexaTrigger->recognizer->Start();
  
  UpdateTriggerForLocale(locale, RecognizerTypeFlag::AlexaMic);
}

void SpeechRecognizerSystem::InitAlexaPlayback(const Util::Locale& locale,
                                               TriggerWordDetectedCallback callback)
{
  if (_alexaPlaybackTrigger) {
    LOG_WARNING("SpeechRecognizerSystem.InitAlexaPlayback", "Alexa Playback Recognizer is already running");
    return;
  }
  
  const bool useVad = true;
  
  const auto dataLoader = _context->GetDataLoader();
  _alexaPlaybackTrigger = std::make_unique<TriggerContext<SpeechRecognizerPryonLite>>("AlexaPlayback", useVad);
  _alexaPlaybackTrigger->recognizer->SetCallback(callback);
  _alexaPlaybackTrigger->recognizer->SetDetectionThreshold(1);
  _alexaPlaybackTrigger->micTriggerConfig->Init("alexa_pryon", dataLoader->GetMicTriggerConfig());
  
  UpdateTriggerForLocale(locale, RecognizerTypeFlag::AlexaPlayback);
  
  ApplySpeechRecognizerLocaleUpdate(*_alexaPlaybackTrigger.get());
  _alexaPlaybackTrigger->recognizer->Start();
}

void SpeechRecognizerSystem::UpdateAlexaActiveState()
{
  _isAlexaActive = (_alexaComponent != nullptr &&
                    _alexaTrigger &&
                    _alexaTrigger->recognizer->IsReady());
}

template <class SpeechRecognizerType>
bool SpeechRecognizerSystem::UpdateTriggerForLocale(TriggerContext<SpeechRecognizerType>& trigger,
                                                    const Util::Locale newLocale,
                                                    const MicData::MicTriggerConfig::ModelType modelType,
                                                    const int searchFileIndex)
{
  std::lock_guard<std::mutex> lock(_triggerModelMutex);
  trigger.nextTriggerPaths = trigger.micTriggerConfig->GetTriggerModelDataPaths(newLocale, modelType, searchFileIndex);
  bool success = false;
  
  if (!trigger.nextTriggerPaths.IsValid()) {
    LOG_WARNING("SpeechRecognizerSystem.UpdateTriggerForLocale.NoPathsFoundForLocale",
                "recognizer: %s locale: %s modelType: %d searchFileIndex: %d",
                trigger.name.c_str(), newLocale.ToString().c_str(), (int) modelType, searchFileIndex);
  }
  
  if (trigger.currentTriggerPaths != trigger.nextTriggerPaths) {
    _isPendingLocaleUpdate = true;
    success = true;
  }
  return success;
}

void SpeechRecognizerSystem::ApplyLocaleUpdate()
{
  std::lock_guard<std::mutex> lock(_triggerModelMutex);
  
  if (_victorTrigger) {
    ApplySpeechRecognizerLocaleUpdate(*_victorTrigger.get());
  }
  
  if (_alexaTrigger) {
    ApplySpeechRecognizerLocaleUpdate(*_alexaTrigger.get());
  }
  
  UpdateAlexaActiveState();
  _isPendingLocaleUpdate = false;
}

template <class SpeechRecognizerType>
void SpeechRecognizerSystem::ApplySpeechRecognizerLocaleUpdate(TriggerContext<SpeechRecognizerType>& aTrigger)
{
  MicData::MicTriggerConfig::TriggerDataPaths &currentTrigPathRef = aTrigger.currentTriggerPaths;
  MicData::MicTriggerConfig::TriggerDataPaths &nextTrigPathRef    = aTrigger.nextTriggerPaths;
  
  if ( currentTrigPathRef != nextTrigPathRef ) {
    currentTrigPathRef = nextTrigPathRef;
    const bool success = UpdateRecognizerModel( aTrigger );
    const std::string netFilePath = currentTrigPathRef.GenerateNetFilePath( _triggerWordDataDir );
    const std::string searchFilePath = currentTrigPathRef.GenerateSearchFilePath( _triggerWordDataDir );
    
    if (success) {
      LOG_INFO("SpeechRecognizerSystem.UpdateTriggerForLocale.SwitchTriggerSearch",
               "Switched speechRecognizer '%s' to netFile: %s searchFile %s",
               aTrigger.name.c_str(), netFilePath.c_str(), searchFilePath.c_str());
    }
    else {
      currentTrigPathRef = MicData::MicTriggerConfig::TriggerDataPaths{};
      nextTrigPathRef = MicData::MicTriggerConfig::TriggerDataPaths{};
      LOG_WARNING("SpeechRecognizerSystem.UpdateTriggerForLocale.FailedSwitchTriggerSearch",
                  "Failed to add speechRecognizer '%s' netFile: %s searchFile %s",
                  aTrigger.name.c_str(), netFilePath.c_str(), searchFilePath.c_str());
    }
    
    if (!currentTrigPathRef.IsValid()) {
      LOG_WARNING("SpeechRecognizerSystem.UpdateTriggerForLocale.ClearTriggerSearch",
                  "Cleared speechRecognizer '%s' to have no search", aTrigger.name.c_str());
    }
  }
}

bool SpeechRecognizerSystem::UpdateRecognizerModel(TriggerContext<SpeechRecognizerPicovoice>& aTrigger)
{
  // bool success = false;
  // SpeechRecognizerPicovoice* recognizer = static_cast<SpeechRecognizerPicovoice*>(aTrigger.recognizer.get());
  // MicData::MicTriggerConfig::TriggerDataPaths& currentTrigPathRef = aTrigger.currentTriggerPaths;
  // recognizer->SetRecognizerIndex( AudioUtil::SpeechRecognizer::InvalidIndex );
  // const AudioUtil::SpeechRecognizer::IndexType singleSlotIndex = 0;
  // recognizer->RemoveRecognitionData( singleSlotIndex );
  
  // if (currentTrigPathRef.IsValid()) {
  //   const std::string netFilePath = currentTrigPathRef.GenerateNetFilePath( _triggerWordDataDir );
  //   const std::string searchFilePath = currentTrigPathRef.GenerateSearchFilePath( _triggerWordDataDir );
  //   const bool isPhraseSpotted = true;
  //   const bool allowsFollowUpRecog = false;
  //   success = recognizer->AddRecognitionDataFromFile( singleSlotIndex, netFilePath, searchFilePath,
  //                                                     isPhraseSpotted, allowsFollowUpRecog );
  //   if ( success ) {
  //     recognizer->SetRecognizerIndex( singleSlotIndex );
  //   }
  // }

  // return success;
  return true;
}

bool SpeechRecognizerSystem::UpdateRecognizerModel(TriggerContext<SpeechRecognizerPryonLite>& aTrigger)
{
  bool success = false;
  SpeechRecognizerPryonLite* recognizer = aTrigger.recognizer.get();
  MicData::MicTriggerConfig::TriggerDataPaths& currentTrigPathRef = aTrigger.currentTriggerPaths;
  recognizer->Stop();
  
  if ( currentTrigPathRef.IsValid() ) {
    const std::string netFilePath = currentTrigPathRef.GenerateNetFilePath( _triggerWordDataDir );
    success = recognizer->InitRecognizer( netFilePath, aTrigger.useVad );
    if ( success && (_alexaComponent != nullptr) ) {
      recognizer->SetAlexaMicrophoneOffset( _alexaComponent->GetMicrophoneSampleIndex() );
      recognizer->Start();
    }
  }
  
  return success;
}

} // end namespace Vector
} // end namespace Anki
