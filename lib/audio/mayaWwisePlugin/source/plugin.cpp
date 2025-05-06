//
//  plugin.cpp
//  AnkiMayaWWisePlugIn
//
//  Created by Molly Jameson on 1/19/16.
//    Copyright © 2016 Molly Jameson. All rights reserved.
//


#include "plugin.h"

#include "mayaAudioController.h"
#include "pluginCommands/updateMarkerDataCmd.hpp"
#include "pluginCommands/stopPlaybackCmd.hpp"
#include "pluginCommands/reloadSoundBanksCmd.hpp"

#include <functional>

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Global functions to actually initialize things. Called as soon
// as plugin is loaded/unloaded in maya
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

MayaAudioController* _audioController = nullptr;
void InitializeAudioController(char* soundbanksPath) { _audioController = new MayaAudioController(soundbanksPath); }
void UninitializeAudioController() { delete _audioController; _audioController = nullptr; }
MCallbackIdArray _callbackIDs;


MStatus initializePlugin (MObject obj) {
	#pragma EXPORT
	MFnPlugin plugin (obj, "Anki_Maya_Wwise", "0.0.1", "Any");
  
  // Init Audio Controller, a.k.a. AudioEngine, a.k.a. Wwise SDK
  bool success = LoadAudioSoundBanks();
  if (!success) {
    return MS::kFailure;
  }

  // Register Update Marker Data Command
  MStatus status = UpdateMarkerDataCmd_Register(plugin);
  if (status.statusCode() == MS::kFailure) {
    MGlobal::displayError("Register UpdateMarkerData Command Failed");
    return status;
  }
  
  // Register Stop Playback Command
  status = StopPlaybackCmd_RegisterCommand(plugin);
  if (status.statusCode() == MS::kFailure) {
    MGlobal::displayError("Register StopPlayback Command Failed");
    return status;
  }
  
  // Register Update Sound Bank Command
  status = ReloadSoundBanksCmd_RegisterCommand(plugin);
  if (status.statusCode() == MS::kFailure) {
    MGlobal::displayError("Register Reload Update Sound Bank Command Failed");
    return status;
  }

  srand(time(NULL));

  EnableTimeChangedCallbacks();

  return status;
}


MStatus uninitializePlugin (MObject obj) {
	#pragma EXPORT
	//- this method is called when the plug-in is unloaded from Maya. It 
	//- deregisters all of the services that it was providing.
	//-		obj - a handle to the plug-in object (use MFnPlugin to access it)
	MFnPlugin plugin (obj) ;

  DisableTimeChangedCallbacks();

  // Unregister Update Marker Data Command
  MStatus markerStatus = UpdateMarkerDataCmd_Unregister(plugin);
  if (markerStatus.statusCode() == MS::kFailure) {
    MGlobal::displayError("Unregister UpdateMarkerData Command Failed");
  }

  // Unregister Stop Playback Command
  MStatus stopPlaybackStatus = StopPlaybackCmd_UnregisterCommand(plugin);
  if (stopPlaybackStatus.statusCode() == MS::kFailure) {
    MGlobal::displayError("Unregister StopPlayback Command Failed");
  }
  
  MStatus updateSoundBankStatus = ReloadSoundBanksCmd_UnregisterCommand(plugin);
  if (updateSoundBankStatus.statusCode() == MS::kFailure) {
    MGlobal::displayError("Unregister Reload Sound Banks Command Failed");
  }
  
  UninitializeAudioController();
  
  if (markerStatus != MS::kSuccess &&
      stopPlaybackStatus != MS::kSuccess &&
      updateSoundBankStatus != MS::kSuccess) {
    return MS::kFailure;
  }
  
  return MS::kSuccess;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Enable / Disable On Time Changed callback
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void EnableTimeChangedCallbacks()
{
  MCallbackId callbackId = MEventMessage::addEventCallback("timeChanged", (MMessage::MBasicFunction)HandleTimeChanged);
  _callbackIDs.append(callbackId);
}


void DisableTimeChangedCallbacks()
{
  if (_callbackIDs.length() != 0)
  {
    // Remove the MEventMessage callback
    MEventMessage::removeCallbacks(_callbackIDs);
    _callbackIDs.clear();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Update Marker Data Register / Unregister
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

MStatus UpdateMarkerDataCmd_Register(MFnPlugin& plugin)
{
  MStatus	status = plugin.registerCommand(UpdateMarkerDataCmd::mayaCommand , UpdateMarkerDataCmd::creator);
  if (!status) {
    status.perror("UpdateMarkerDataCmd_Register");
  }
  return status;
}


MStatus UpdateMarkerDataCmd_Unregister(MFnPlugin& plugin)
{
  MStatus status = plugin.deregisterCommand(UpdateMarkerDataCmd::mayaCommand);
  if (!status) {
    status.perror("UpdateMarkerDataCmd_Unregister");
  }
  return status;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Stop Playback Register / Unregister
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

MStatus StopPlaybackCmd_RegisterCommand(MFnPlugin& plugin)
{
  MStatus	status = plugin.registerCommand(StopPlaybackCmd::callbackName, StopPlaybackCmd::creator);
  if (!status) {
    status.perror("StopPlaybackCmd_RegisterCommand");
  }
  
  // Register callback for when playback stops
  MString val_cmd("scriptJob -cc \"playingBack\" ");
  val_cmd += StopPlaybackCmd::callbackName;
  int val_result;
  MStatus val_status = MGlobal::executeCommand(val_cmd,val_result);
  // Hold on to job Number
  if (val_status) {
    StopPlaybackCmd::jobNumber = val_result;
  }
  else {
    MGlobal::displayError("ScriptJob for Stop Playback callback Failed");
  }
  
  // Setup callback function
  StopPlaybackCmd::doItFunc = [] {
    _audioController->StopAllAudioEvents();
    _audioController->Update();
    
    // Do an audition update
    MString val_cmd("AnkiMayaWWisePlugIn_UpdateEventData");
    int val_result;
    MGlobal::executeCommand(val_cmd,val_result);
  };

//  std::string restultStr = "Playback resultVal: ";
//  restultStr += std::to_string(val_result);
//  restultStr += "  Success: ";
//  restultStr += val_status.statusCode() == MStatus::MStatusCode::kSuccess ? "Yes" : "No";
//  MGlobal::displayInfo(restultStr.c_str());

  return  status;
}


MStatus StopPlaybackCmd_UnregisterCommand(MFnPlugin& plugin)
{
  // Unregister playback stop callback
  MString val_cmd("scriptJob -kill ");
  val_cmd += StopPlaybackCmd::jobNumber;
  MStatus val_status = MGlobal::executeCommand( val_cmd );
  if (val_status.statusCode() != MStatus::MStatusCode::kSuccess) {
    MGlobal::displayError( val_status.errorString() );
  }
  StopPlaybackCmd::jobNumber = 0;
  
  MStatus status = plugin.deregisterCommand(StopPlaybackCmd::callbackName);
  if (!status) {
    status.perror("StopPlaybackCmd_UnregisterCommand");
  }
  return status;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Reload Soundbanks Register / Unregister
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

MStatus ReloadSoundBanksCmd_RegisterCommand(MFnPlugin& plugin)
{
  MStatus	status = plugin.registerCommand(ReloadSoundBanksCmd::mayaCommand, ReloadSoundBanksCmd::creator);
  if (!status) {
    status.perror("ReloadSoundBanksCmd_RegisterCommand");
  }
  
  // Setup callback function
  ReloadSoundBanksCmd::doItFunc = [] {
    const bool success = LoadAudioSoundBanks();
    if (!success) {
      MGlobal::displayError("Unable to reload sound banks");
    }
    else {
      MGlobal::displayInfo("Successfully reloaded sound banks");
    }
  };
  
  return status;
}

MStatus ReloadSoundBanksCmd_UnregisterCommand(MFnPlugin& plugin)
{
  MStatus status = plugin.deregisterCommand(ReloadSoundBanksCmd::mayaCommand);
  if (!status) {
    status.perror("ReloadSoundBanksCmd_UnregisterCommand");
  }
  return status;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool LoadAudioSoundBanks()
{
  if ( _audioController != nullptr ) {
    UninitializeAudioController();
  }

  // Get sound bank path
  char* soundbanksPath = getenv("ANKI_SB_WORKING");
  MString debugOut("Soundbanks Path = ");
  debugOut += soundbanksPath;
  MGlobal::displayInfo(debugOut);

  // Init Audio Controller, a.k.a. AudioEngine, a.k.a. Wwise SDK
  InitializeAudioController(soundbanksPath);
  return _audioController->IsInitialized();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MString GetAudioEventToPlay(const UpdateMarkerDataCmd::AudioEventInfo* audioEvent)
{
  MString emptyString("");
  int numEvents = audioEvent->names.length();
  int numProbs = audioEvent->probabilities.length();
  if (numEvents < 1) {
    // return an empty string if there are no audio event candidates
    return emptyString;
  }
  if ((numProbs > 0) && (numEvents == numProbs)) {
    // take probabilities into account
    unsigned int randInt = rand() % 101; // yields a random integer between 0 and 100, inclusive
    unsigned int randRangeMin = 0;
    for (int idx=0; idx<numProbs; idx++) {
      unsigned int probVal = audioEvent->probabilities[idx];
      if (probVal < 1) {
        continue;
      }
      unsigned int randRangeMax = randRangeMin + probVal;
      if ((randRangeMin <= randInt) && (randInt <= randRangeMax)) {
        return audioEvent->names[idx];
      }
      randRangeMin = randRangeMax;
    }
    return emptyString;
  }
  else {
    // no probabilities or the wrong number of probabilities, so pick an audio event at random
    if (numEvents == 1) {
      return audioEvent->names[0];
    }
    unsigned int randIdx = rand() % numEvents;
    return audioEvent->names[randIdx];
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameAudioEvent(const MTime time)
{
//  MString debugOut("Checking frame ");
//  debugOut += time.value();
//  debugOut += " for an event...";
//  MGlobal::displayInfo(debugOut);

  // Perform frame audio event
  const UpdateMarkerDataCmd::AudioEventInfo* audioEvent = UpdateMarkerDataCmd::FrameEvent( (uint64_t)time.value() );
  if ( audioEvent != nullptr && audioEvent->names.length() > 0 ) {
    MString audioEventName = GetAudioEventToPlay(audioEvent);
    if ( audioEventName.length() > 0 ) {
      MString debugOut("Found Event ");
      debugOut += time.value();
      debugOut += " - ";
      debugOut += audioEventName;
      debugOut += " : Volume ";
      debugOut += audioEvent->volume;
      MGlobal::displayInfo(debugOut);
      const bool success = _audioController->PostAnimEvent( audioEventName.asChar(), audioEvent->volume );
      if ( !success ) {
        MString errorMsg("Failed to Play Event '");
        errorMsg += audioEventName;
        errorMsg += "' Frame#: ";
        errorMsg += time.value();
        MGlobal::displayError(errorMsg);
      }
    }
  }

  // Need to call update every frame to process the events
  _audioController->Update();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void HandleTimeChanged()
{
  const MTime time = MAnimControl::currentTime();

//  MString debugOut("HandleTimeChange! ");
//  debugOut += time.as(MTime::kMilliseconds);
//  debugOut += " ms - value: ";
//  debugOut += std::to_string(time.value()).c_str();
//  MGlobal::displayInfo( debugOut );

  if ( MAnimControl::isPlaying() ) {
    FrameAudioEvent(time);
  }
  else {
    /*
    When Time Slider -> Playback -> Looping is set to "Once" and the current frame is the
    last frame of the time slider, then pressing play seems to cause a jump to the first
    frame of the time slider and then playback starting at the next frame. For example, if
    the time slider is 0 to 10 and the current frame is 10, then pressing play causes a
    jump to frame 0 followed by playback begining at frame 1. This block accounts for that
    scenario and allows us to process audio events on that first frame.
    */

    MAnimControl::PlaybackMode mode = MAnimControl::playbackMode();
    if ( mode == MAnimControl::kPlaybackOnce ) {
      MTime minTime = MAnimControl::minTime();
      if ( time.value() == minTime.value() ) {
        FrameAudioEvent(time);
      }
    }
  }
}

