//
//  UpdateMarkerDataCmd.cpp
//  AnkiMayaWWisePlugIn
//
//  Created by Jordan Rivas on 2/1/16.
//  Copyright © 2016 Anki, Inc. All rights reserved.
//

#include "pluginCommands/updateMarkerDataCmd.hpp"


const char* UpdateMarkerDataCmd::mayaCommand = "AnkiMayaWWisePlugIn_UpdateEventData";

MDoubleArray UpdateMarkerDataCmd::time_prob;
MDoubleArray UpdateMarkerDataCmd::time_result;
MDoubleArray UpdateMarkerDataCmd::time_volume;
MDoubleArray UpdateMarkerDataCmd::val_prob;
MDoubleArray UpdateMarkerDataCmd::val_result;
MDoubleArray UpdateMarkerDataCmd::val_volume;
MStringArray UpdateMarkerDataCmd::enum_array;
std::function< void (void) > UpdateMarkerDataCmd::updateEventDataCompleteFunc;

// Event Data
std::unordered_map< uint64_t, UpdateMarkerDataCmd::AudioEventInfo > UpdateMarkerDataCmd::_frameEvents;



UpdateMarkerDataCmd::UpdateMarkerDataCmd()
{
//  MGlobal::displayInfo("UpdateMarkerDataCmd CONSTRUCTOR");
};

UpdateMarkerDataCmd::~UpdateMarkerDataCmd()
{
//  MGlobal::displayInfo("UpdateMarkerDataCmd DESTRUCTOR");
};


void* UpdateMarkerDataCmd::creator()
{
  return new UpdateMarkerDataCmd();
}


// This class is created and destroyed immediately after
MStatus UpdateMarkerDataCmd::doIt( const MArgList& args )
{
  MStatus stat = MS::kSuccess;
  
  updateEventData(this);
  
  if (UpdateMarkerDataCmd::updateEventDataCompleteFunc != nullptr) {
    UpdateMarkerDataCmd::updateEventDataCompleteFunc();
  }
  
  return stat;
}

void UpdateMarkerDataCmd::cleanUp()
{
  // Clean up old static data
  UpdateMarkerDataCmd::time_prob.clear();
  UpdateMarkerDataCmd::time_result.clear();
  UpdateMarkerDataCmd::time_volume.clear();
  UpdateMarkerDataCmd::val_prob.clear();
  UpdateMarkerDataCmd::val_result.clear();
  UpdateMarkerDataCmd::val_volume.clear();
  UpdateMarkerDataCmd::enum_array.clear();
  UpdateMarkerDataCmd::_frameEvents.clear();
}

void UpdateMarkerDataCmd::updateEventData(void* data)
{
  // Clean up old static data
  cleanUp();
  
  // TODO: use attribute keyframes on node instead of empty audio nodes?
  // TODO: do this whenever audio is "dirtied" instead of just on export.
  // Builds the audio files from maya based on empty wavs
  MItDependencyNodes it(MFn::kDependencyNode);
  
  for (; !it.isDone(); it.next())
  {
    MFnDependencyNode fn(it.item());
    
    // x:AnkiAudioNode
    // AnkiAudioNode_WwiseIdEnum
    // Prints out the audio as it is on trax
    /*if (fn.typeName() == "audio")
     {
     MStatus ret;
     const MPlug plug = fn.findPlug("offset", &ret);
     MTime timeOffset;
     ret = plug.getValue(timeOffset);
     g_AudioNodes.push_back(RelevantMayaAudioInfo(fn.name().asChar(),timeOffset.as(MTime::kMilliseconds)));
     MString debugOut("Added audio ");
     debugOut += fn.name();
     debugOut += " - ";
     debugOut += timeOffset.as(MTime::kMilliseconds);
     MGlobal::displayInfo(debugOut);
     }*/
  }
  
  // Time at which an audio event happen
  MString time_cmd("keyframe -attribute \"WwiseIdEnum\" -query -timeChange \"x:AnkiAudioNode\"");
  MStatus time_status = MGlobal::executeCommand(time_cmd, UpdateMarkerDataCmd::time_result);
  
  // Index into the below enum array
  MString val_cmd("keyframe -attribute \"WwiseIdEnum\" -query -valueChange \"x:AnkiAudioNode\"");
  MStatus val_status = MGlobal::executeCommand(val_cmd, UpdateMarkerDataCmd::val_result);

  // Time at which an audio event probability happens
  MString timeProb_cmd("keyframe -attribute \"probability\" -query -timeChange \"x:AnkiAudioNode\"");
  MStatus timeProb_status = MGlobal::executeCommand(timeProb_cmd, UpdateMarkerDataCmd::time_prob);

  // List of probabilities
  MString valProb_cmd("keyframe -attribute \"probability\" -query -valueChange \"x:AnkiAudioNode\"");
  MStatus valProb_status = MGlobal::executeCommand(valProb_cmd, UpdateMarkerDataCmd::val_prob);

  // Time at which an audio volume event happen
  MString timeVolume_cmd("keyframe -attribute \"volume\" -query -timeChange \"x:AnkiAudioNode\"");
  MStatus timeVolume_status = MGlobal::executeCommand(timeVolume_cmd, UpdateMarkerDataCmd::time_volume);
  
  // List of volume events
  MString valVolume_cmd("keyframe -attribute \"volume\" -query -valueChange \"x:AnkiAudioNode\"");
  MStatus valVolume_status = MGlobal::executeCommand(valVolume_cmd, UpdateMarkerDataCmd::val_volume);
  
  // Lookup table of the string names the maya enum index actually map to.
  MString enum_cmd("attributeQuery -node \"x:AnkiAudioNode\" -listEnum \"WwiseIdEnum\"");
  MStringArray str_array;
  MStatus enum_status = MGlobal::executeCommand(enum_cmd, str_array);
  if( enum_status == MStatus::kSuccess && str_array.length() > 0) {
    //MGlobal::displayInfo("enum query passed");
    enum_status = str_array[0].split(':', UpdateMarkerDataCmd::enum_array);
  }
  else {
    enum_status = MStatus::kFailure;
    //MGlobal::displayInfo("enum query failed");
  }
  
  // Check if values are updated
  if(MStatus::kSuccess == time_status
     && MStatus::kSuccess == val_status
     && MStatus::kSuccess == enum_status
     && MStatus::kSuccess == timeProb_status
     && MStatus::kSuccess == valProb_status
     && MStatus::kSuccess == valVolume_status
     && MStatus::kSuccess == timeVolume_status) {
    
    // Add every audio event to _frameEvent map
    AddEventToMapping(UpdateMarkerDataCmd::val_result, UpdateMarkerDataCmd::time_result,
                      UpdateMarkerDataCmd::val_prob, UpdateMarkerDataCmd::time_prob);

    // Some audio keyframes have multiple events for variation, get the rest of that data here
    int suffix = VARIANT_ATTR_SUFFIX_START_INDEX;
    while((MStatus::kSuccess == time_status) &&
          (MStatus::kSuccess == val_status) &&
          (MStatus::kSuccess == timeProb_status) &&
          (MStatus::kSuccess == valProb_status) &&
          (UpdateMarkerDataCmd::time_result.length() > 0) &&
          (UpdateMarkerDataCmd::val_result.length() > 0)) {

      MString time_cmd("keyframe -attribute \"WwiseIdEnum");
      time_cmd += suffix;
      time_cmd += "\" -query -timeChange \"x:AnkiAudioNode\"";
      time_status = MGlobal::executeCommand(time_cmd, UpdateMarkerDataCmd::time_result);

      MString val_cmd("keyframe -attribute \"WwiseIdEnum");
      val_cmd += suffix;
      val_cmd += "\" -query -valueChange \"x:AnkiAudioNode\"";
      val_status = MGlobal::executeCommand(val_cmd, UpdateMarkerDataCmd::val_result);

      MString timeProb_cmd("keyframe -attribute \"probability");
      timeProb_cmd += suffix;
      timeProb_cmd += "\" -query -timeChange \"x:AnkiAudioNode\"";
      timeProb_status = MGlobal::executeCommand(timeProb_cmd, UpdateMarkerDataCmd::time_prob);

      MString valProb_cmd("keyframe -attribute \"probability");
      valProb_cmd += suffix;
      valProb_cmd += "\" -query -valueChange \"x:AnkiAudioNode\"";
      valProb_status = MGlobal::executeCommand(valProb_cmd, UpdateMarkerDataCmd::val_prob);

      AddEventToMapping(UpdateMarkerDataCmd::val_result, UpdateMarkerDataCmd::time_result,
                        UpdateMarkerDataCmd::val_prob, UpdateMarkerDataCmd::time_prob);

      suffix += 1;
    }

    // Update audio event volumes
    UpdateAudioEventVolumes();
  }
  else {
    MGlobal::displayInfo("Audio Export Mel Commands failed from C++");
  }
}

void UpdateMarkerDataCmd::AddEventToMapping(MDoubleArray eventVals, MDoubleArray eventTimes,
                                            MDoubleArray probVals, MDoubleArray probTimes)
{
  if(eventVals.length() != eventTimes.length()) {
    MGlobal::displayError("Audio keyframes out of sync");
    return;
  }

  if((probVals.length() != probTimes.length()) || (probTimes.length() != eventTimes.length())) {
    MGlobal::displayWarning("Audio event probabilities out of sync, so ignoring them");
    probVals.clear();
    probTimes.clear();
  }
  else {
    // compare event keyframe times to probability keyframe times to confirm they all match
    for(unsigned int i = 0; i < eventTimes.length(); i++) {
      if(probTimes[i] != eventTimes[i]) {
        MGlobal::displayWarning("Audio event probabilities out of sync, so ignoring them");
        probVals.clear();
        probTimes.clear();
        break;
      }
    }
  }

  // Add every audio event to _frameEvent map
  unsigned int num_keyframes = eventVals.length();
  for(unsigned int i = 0; i < num_keyframes; ++i) {
    int enum_index = (int)eventVals[i];
    if( enum_index < UpdateMarkerDataCmd::enum_array.length()) {

      // frames to ms, grab this somewhere... Not sure if we're at 33.3333 or 30 so this needs to be looked at a lot
      //        double time_in_ms = eventTimes[i] * 33.3333333333;
      //        g_AudioNodes.push_back(RelevantMayaAudioInfo(enum_array[enum_index].asChar(),time_in_ms));

      float eventProb;
      if(probVals.length() > 0) {
        eventProb = probVals[i];
      }
      else {
        // If audio event probabilities were out of sync above and ignored, then the event gets
        // a probability of 100 (at the risk of masking other events in the same audio keyframe)
        eventProb = 100.0;
      }

      unsigned int valTime = static_cast<unsigned int>(eventTimes[i]);

      MString eventName = UpdateMarkerDataCmd::enum_array[enum_index];

      const auto it = UpdateMarkerDataCmd::_frameEvents.find(valTime);
      if(it == UpdateMarkerDataCmd::_frameEvents.end()) {
        // this is the first event found for this time
        float volume = 1.0; // default value
        MStringArray eventNames;
        eventNames.append(eventName);
        MDoubleArray eventProbs;
        eventProbs.append(eventProb);
        const AudioEventInfo info { eventNames, eventProbs, volume };
        UpdateMarkerDataCmd::_frameEvents.emplace(valTime, std::move(info));
      }
      else {
        // already have at least one event for this time, so append this event
        it->second.AddName(eventName);
        it->second.AddProb(eventProb);
      }

      MString debugOut("Added audio key @ ");
      debugOut += valTime;
      debugOut += " - ";
      debugOut += eventName;
      MGlobal::displayInfo(debugOut);
    }
  }
}

void UpdateMarkerDataCmd::UpdateAudioEventVolumes()
{
  // Update audio event volumes
  for (unsigned int volumeTsIdx = 0; volumeTsIdx < UpdateMarkerDataCmd::time_volume.length(); ++volumeTsIdx) {
    // Get volume event time
    const auto time = static_cast<unsigned int>(time_volume[volumeTsIdx]);
    // Find event for time in map
    const auto it = UpdateMarkerDataCmd::_frameEvents.find(time);
    if ( it != UpdateMarkerDataCmd::_frameEvents.end() ) {
      // Set volume
      it->second.volume = UpdateMarkerDataCmd::val_volume[volumeTsIdx] / 100.0;  // convert int [0 - 100] in to scaler

      MString debugOut("Update audio key @ ");
      debugOut += time;
      debugOut += " - ";
      debugOut += it->second.Description().c_str();
      MGlobal::displayInfo(debugOut);
    }
  }
}

const UpdateMarkerDataCmd::AudioEventInfo* UpdateMarkerDataCmd::FrameEvent(uint64_t frame)
{
  const auto it = UpdateMarkerDataCmd::_frameEvents.find(frame);
  if ( it != UpdateMarkerDataCmd::_frameEvents.end() ) {
    return &it->second;
  }
  return nullptr;
}


