/*
 * File: AnkiPluginInterface.cpp
 *
 * Author: Jordan Rivas
 * Created: 4/05/2016
 *
 * Description: This class wraps Audio Engine custom plugins to allow the app’s objects to interface with them. The
 *              interface is intended to remove the complexities of compile issues since the Audio Engine can be
 *              complied out of the project.
 *
 * Copyright: Anki, Inc. 2016
 *
 */

#include "audioEngine/plugins/ankiPluginInterface.h"
#include "audioEngine/audioEngineController.h"
#include "util/math/numericCast.h"


#ifndef EXCLUDE_ANKI_AUDIO_LIBS

#define USE_AUDIO_ENGINE 1
#include "audioEngine/plugins/hijackAudioPlugIn.h"
#include "audioEngine/plugins/wavePortalPlugIn.h"
#include "audioEngine/plugins/wavePortalFxTypes.h"
#else

namespace Anki {
namespace AudioEngine {
namespace PlugIns {
  class WavePortalPlugIn{};
  class HijackAudioPlugIn{};
}
}
}
// If we're excluding the audio libs, don't link any of the audio engine
#define USE_AUDIO_ENGINE 0
#endif


namespace Anki {
namespace AudioEngine {
namespace PlugIns {

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AnkiPluginInterface::AnkiPluginInterface()
{
}
  
AnkiPluginInterface::~AnkiPluginInterface()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool AnkiPluginInterface::SetupWavePortalPlugIn()
{
  bool success = false;
#if USE_AUDIO_ENGINE
  using namespace PlugIns;
  // Register Wave file
  _wavePortalPlugIn.reset( new WavePortalPlugIn() );
  success = _wavePortalPlugIn->RegisterPlugIn();
#endif
  return success;
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AnkiPluginInterface::GiveWavePortalAudioDataOwnership(const StandardWaveDataContainer* audioData)
{
#if USE_AUDIO_ENGINE
  if ( (nullptr != _wavePortalPlugIn) && (nullptr != audioData) ) {
    const AudioDataStream* dataStream = new AudioDataStream(audioData->sampleRate,
                                                            audioData->numberOfChannels,
                                                            audioData->ApproximateDuration_ms(),
                                                            audioData->audioBuffer,
                                                            Anki::Util::numeric_cast<uint32_t>(audioData->bufferSize));

    _wavePortalPlugIn->SetAudioDataOwnership(dataStream);
  }
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AnkiPluginInterface::ClearWavePortalAudioData()
{
#if USE_AUDIO_ENGINE
  if ( _wavePortalPlugIn != nullptr ) {
    _wavePortalPlugIn->ClearAudioData();
  }
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool AnkiPluginInterface::WavePortalHasAudioDataInfo() const
{
#if USE_AUDIO_ENGINE
  if ( _wavePortalPlugIn != nullptr ) {
    return _wavePortalPlugIn->HasAudioDataInfo();
  }
#endif
  return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool AnkiPluginInterface::WavePortalIsActive() const
{
#if USE_AUDIO_ENGINE
  if ( _wavePortalPlugIn != nullptr ) {
    return _wavePortalPlugIn->IsActive();
  }
#endif
  return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AnkiPluginInterface::SetWavePortalInitCallback( PluginCallbackFunc callback )
{
  _wavePortalInitFunc = callback;
#if USE_AUDIO_ENGINE
  if ( _wavePortalPlugIn != nullptr ) {
    // Wrap the Plug-in interface callback inside of the plugin instance's callback
    if ( _wavePortalInitFunc != nullptr ) {
      _wavePortalPlugIn->SetInitCallback( [this] (const WavePortalPlugIn* pluginInstance)
      {
        if ( _wavePortalInitFunc != nullptr ) {
          _wavePortalInitFunc( this );
        }
      });
    }
    else {
      _wavePortalPlugIn->SetInitCallback( nullptr );
    }
  }
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AnkiPluginInterface::SetWavePortalTerminateCallback( PluginCallbackFunc callback )
{
  _wavePortalTermFunc = callback;
#if USE_AUDIO_ENGINE
  if ( _wavePortalPlugIn != nullptr ) {
    // Wrap the Plug-in interface callback inside of the plugin instance's callback
    if ( _wavePortalTermFunc != nullptr ) {
      _wavePortalPlugIn->SetTerminateCallback( [this] (const WavePortalPlugIn* pluginInstance)
      {
        if ( _wavePortalTermFunc != nullptr ) {
          _wavePortalTermFunc( this );
        }
      });
    }
    else {
      _wavePortalPlugIn->SetTerminateCallback( nullptr );
    }
  }
#endif
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool AnkiPluginInterface::SetupHijackAudioPlugInAndRobotAudioBuffers( uint32_t resampleRate, uint16_t samplesPerFrame )
{
  bool success = false;
#if USE_AUDIO_ENGINE
  using namespace PlugIns;
  // Setup CozmoPlugIn & RobotAudioBuffer
  _hijackAudioPlugIn.reset( new HijackAudioPlugIn( resampleRate, samplesPerFrame ) );
  success = _hijackAudioPlugIn->RegisterPlugin();
#endif // USE_AUDIO_ENGINE
  return success;
}

} // PlugIns
} // AudioEngine
} // Anki

