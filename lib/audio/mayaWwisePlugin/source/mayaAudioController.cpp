//
//  MayaAudioController.cpp
//  AnkiMayaWWisePlugIn
//
//  Created by Jordan Rivas on 1/09/18.
//  Copyright © 2016 Anki, Inc. All rights reserved.
//


#include "mayaAudioController.h"
#include "mayaIncludes.h"
#include <sstream>
#include <string>


namespace AE = Anki::AudioEngine;
// Setup Ak Logging callback
static void AudioEngineLogCallback( uint32_t, const char*, AE::ErrorLevel, AE::AudioPlayingId, AE::AudioGameObject );

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MayaAudioController::MayaAudioController(char* soundbanksPath)
: _soundbankLoader( new AE::SoundbankLoader(*this, std::string(soundbanksPath)) )
{
  // Config Engine
  AE::SetupConfig config{};
  // Read/Write Asset path
  config.assetFilePath = std::string(soundbanksPath);
  
  // Cozmo uses default audio locale regardless of current context.
  // Locale-specific adjustments are made by setting GameState::External_Language
  // below.
  config.audioLocale = AE::AudioLocaleType::EnglishUS;

  // Engine Memory
  // NOTE: This is huge! Okay for desktop development not shipping Apps
  config.defaultMemoryPoolSize      = ( 16 * 1024 * 1024 );   // 16 MB
  config.defaultLEMemoryPoolSize    = ( 16 * 1024 * 1024 );   // 16 MB
  config.ioMemorySize               = (  8 * 1024 * 1024 );   //  8 MB
  config.defaultMaxNumPools         = 30;
  config.enableGameSyncPreparation  = true;
  config.enableStreamCache          = true;


  // Start your Engines!!!
  InitializeAudioEngine( config );

  if (IsInitialized()) {
    // Setup Engine Logging callback
    SetLogOutput( AE::ErrorLevel::All, &AudioEngineLogCallback );

    // Load soundbanks
    _soundbankLoader->LoadDefaultSoundbanks();

    // Register Game Object
    _gameObj = 1;
    bool success = RegisterGameObject(_gameObj, "MayaGameObj");
    if (!success) {
      MGlobal::displayError("Failed to Register Maya Game Object");
    }
    else {
      MGlobal::displayInfo("Successfully Registered Maya Game Object!");
      SetDefaultListeners( { _gameObj } );
    }
  }
  else {
    MGlobal::displayError("Failed to Initialize WWise SDK");
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool MayaAudioController::PostAnimEvent(std::string eventName, float volume)
{
 bool success = false;
 if ( IsInitialized() ) {
   // Post Event
   AE::AudioPlayingId playingId = PostAudioEvent(eventName, _gameObj);
   if (playingId != AE::kInvalidAudioPlayingId) {
     // Set Volume RTPC on play event, this is project specific parameter.
     static const AE::AudioParameterId parameterId = GetAudioIdFromString("Event_Volume");
     success = SetParameterWithPlayingId(parameterId, volume, playingId);
   }
 }
 return success;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Setup Ak Logging callback
void AudioEngineLogCallback( uint32_t akErrorCode,
                            const char* errorMessage,
                            AE::ErrorLevel errorLevel,
                            AE::AudioPlayingId playingId,
                            AE::AudioGameObject gameObjectId )
{
  std::ostringstream logStream;
  logStream << "ErrorCode: " << akErrorCode << " Message: '" << ((nullptr != errorMessage) ? errorMessage : "")
  << "' LevelBitFlag: " << (uint32_t)errorLevel << " PlayingId: " << playingId << " GameObjId: " << gameObjectId;
  
  if (((uint32_t)errorLevel & (uint32_t)AE::ErrorLevel::Message) == (uint32_t)AE::ErrorLevel::Message) {
    MString debugOut("AudioEngineLog ");
    debugOut += logStream.str().c_str();
    MGlobal::displayInfo(debugOut);
  }
  
  if (((uint32_t)errorLevel & (uint32_t)AE::ErrorLevel::Error) == (uint32_t)AE::ErrorLevel::Error) {
    MString debugOut("AudioEngineError ");
    debugOut += logStream.str().c_str();
    MGlobal::displayError(debugOut);
  }
}
