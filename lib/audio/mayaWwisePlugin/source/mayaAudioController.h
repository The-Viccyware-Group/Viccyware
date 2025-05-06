//
//  MayaAudioController.h
//  AnkiMayaWWisePlugIn
//
//  Created by Jordan Rivas on 1/09/18.
//  Copyright © 2016 Anki, Inc. All rights reserved.
//

#ifndef __Anki_MayaAudioController_H__
#define __Anki_MayaAudioController_H__

#include "audioEngine/audioEngineController.h"
#include "audioEngine/soundbankLoader.h"
#include <memory>


class MayaAudioController : public Anki::AudioEngine::AudioEngineController
{
public:
  
  MayaAudioController(char* soundbanksPath);
  
  bool PostAnimEvent(std::string eventName, float volume);
  
  
private:
  
  std::unique_ptr<Anki::AudioEngine::SoundbankLoader> _soundbankLoader;
  
  Anki::AudioEngine::AudioGameObject _gameObj = Anki::AudioEngine::kInvalidAudioGameObject;
  
};


#endif /* __Anki_MayaAudioController_H__ */
