/*
 * File: AnkiPluginInterface.h
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

#ifndef __AnkiAudio_AnkiPluginInterface_H__
#define __AnkiAudio_AnkiPluginInterface_H__


#include "audioEngine/audioExport.h"
#include "audioEngine/audioTools/standardWaveDataContainer.h"
#include "util/helpers/noncopyable.h"
#include <cstdint>
#include <functional>
#include <memory.h>

namespace Anki {
namespace AudioEngine {

class AudioEngineController;

namespace PlugIns {
class HijackAudioPlugIn;
class WavePortalPlugIn;

class AUDIOENGINE_EXPORT AnkiPluginInterface : private Anki::Util::noncopyable {
  
public:
  // WavePortal Interface
  // TODO: At some point there will be more then 1 instance of the WavePortal Plug-in they will be tracked by their Id
  AnkiPluginInterface();
  
  ~AnkiPluginInterface();

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Wave Portal Plugin
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  bool SetupWavePortalPlugIn();
  
  WavePortalPlugIn* const GetWavePortalPlugIn() const { return _wavePortalPlugIn.get(); }
  
  // Give Audio Data ownership to plugin for playback
  void GiveWavePortalAudioDataOwnership(const StandardWaveDataContainer* audioData);
  
  // Clear the Audio Data from the plugin
  // Note: This is used to release audio data that is not going to be used
  void ClearWavePortalAudioData();
  
  // Check if the plugin has Audio Data
  bool WavePortalHasAudioDataInfo() const;
  
  // Check if plugin is in uses
  bool WavePortalIsActive() const;
  
  // Set plugin life cycle callback functions
  // Note: If the plugin is compiled out the callback functions will not be called
  using PluginCallbackFunc = std::function<void( const AnkiPluginInterface* pluginInstance )>;
  void SetWavePortalInitCallback( PluginCallbackFunc callback );
  void SetWavePortalTerminateCallback( PluginCallbackFunc callback );
  
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Hijack Audio Plugin
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  bool SetupHijackAudioPlugInAndRobotAudioBuffers( uint32_t resampleRate, uint16_t samplesPerFrame );
  
  HijackAudioPlugIn* const GetHijackAudioPlugIn() const { return _hijackAudioPlugIn.get(); }


private:
  
//  AudioEngineController& _parentAudioController;
  
  // Custom Plugins Instance
  std::unique_ptr<HijackAudioPlugIn>  _hijackAudioPlugIn;
  std::unique_ptr<WavePortalPlugIn>   _wavePortalPlugIn;
  
  // WavePortal
  PluginCallbackFunc    _wavePortalInitFunc   = nullptr;    // Callback when the plugin's init method is completed
  PluginCallbackFunc    _wavePortalTermFunc   = nullptr;    // Callback when plugin is going to be destroyed
  
};

} // PlugIns
} // AudioEngine
} // Anki

#endif /* __Basestation_Audio_AnkiPluginInterface_H__ */
