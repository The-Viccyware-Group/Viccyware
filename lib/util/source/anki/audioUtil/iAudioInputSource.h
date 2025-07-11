/**
* File: iAudioInputSource.h
*
* Author: Lee Crippen
* Created: 06/14/2017
*
* Description: Defines the base functionality that audio input sources are expected to implement.
*
* Copyright: Anki, Inc. 2017
*
*/

#ifndef __Anki_AudioUtil_IAudioInputSource_H_
#define __Anki_AudioUtil_IAudioInputSource_H_

#include "audioDataTypes.h"

#include <functional>
#include <mutex>

namespace Anki {
namespace AudioUtil {

class IAudioInputSource
{
public:
  IAudioInputSource() = delete;
  virtual ~IAudioInputSource() { }
  IAudioInputSource(const IAudioInputSource&) = delete;
  IAudioInputSource& operator=(const IAudioInputSource&) = delete;
  IAudioInputSource(IAudioInputSource&& other) = delete;
  IAudioInputSource& operator=(IAudioInputSource&& other) = delete;
  
  using DataCallback = std::function<void(const AudioSample* samples, uint32_t size)>;
  void SetCallback(DataCallback newCallback)
  {
    std::lock_guard<std::mutex> lock(_callbackMutex);
    _dataCallback = newCallback;
  }
  
  DataCallback GetDataCallback() const
  {
    std::lock_guard<std::mutex> lock(_callbackMutex);
    return _dataCallback;
  }
  
private:
  DataCallback        _dataCallback;
  mutable std::mutex  _callbackMutex;
  
}; // class IAudioInputSource

} // end namespace AudioUtil
} // end namespace Anki


#endif //__Anki_AudioUtil_IAudioInputSource_H_
