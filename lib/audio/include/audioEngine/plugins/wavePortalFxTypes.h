/*
 * File: wavePortalFxTypes.h
 *
 * Author: Jordan Rivas
 * Created: 3/24/2016
 *
 * Description: WavePortal plug-in types
 *
 * Copyright: Anki, Inc. 2016
 */

#ifndef __AnkiAudio_PlugIns_WavePortalFxTypes_H__
#define __AnkiAudio_PlugIns_WavePortalFxTypes_H__

#include <cstdint>


namespace Anki {
namespace AudioEngine {
namespace PlugIns {

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Continuous stream of audio data
struct __attribute__((visibility("default"))) AudioDataStream
{
  // Samples per second
  const uint32_t sampleRate       = 0;
  // Channel Count
  const uint16_t numberOfChannels = 0;
  // Duration MilliSeconds
  const float    duration_ms      = 0.0f;
  // Audio sample data buffer
  // Note: Always allocate memory using new[]
  const float*   audioBuffer      = nullptr;
  // Number of samples in audio buffer
  const uint32_t bufferSize       = 0;
  
  AudioDataStream() {}
  
  // Note: Constructor doesn't copy audio buffer
  AudioDataStream(uint32_t sampleRate,
                  uint16_t numberOfChannels,
                  float    duration_ms,
                  float*   audioBuffer,
                  uint32_t bufferSize)
  : sampleRate(sampleRate)
  , numberOfChannels(numberOfChannels)
  , duration_ms(duration_ms)
  , audioBuffer(audioBuffer)
  , bufferSize(bufferSize)
  {}
  
  ~AudioDataStream()
  {
    delete[] audioBuffer;
    audioBuffer = nullptr;
  }
};


} // PlugIns
} // AudioEngine
} // Anki

#endif /* __AnkiAudio_PlugIns_WavePortalFxTypes_H__ */
