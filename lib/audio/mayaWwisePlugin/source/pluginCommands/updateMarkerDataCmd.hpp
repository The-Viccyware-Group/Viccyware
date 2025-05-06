//
//  UpdateMarkerDataCmd.hpp
//  AnkiMayaWWisePlugIn
//
//  Created by Jordan Rivas on 2/1/16.
//  Copyright © 2016 Anki, Inc. All rights reserved.
//

#define VARIANT_ATTR_SUFFIX_START_INDEX 2

#ifndef UpdateMarkerDataCmd_hpp
#define UpdateMarkerDataCmd_hpp

#include "mayaIncludes.h"
#include <unordered_map>

class UpdateMarkerDataCmd : public MPxCommand {
  
public:
  
  struct AudioEventInfo {
    MStringArray names;
    MDoubleArray probabilities;
    float volume;
    
    const std::string Description() const
    {
      std::string des = names[0].asChar();
      des += " (" + std::to_string(probabilities[0]) + ")";
      for(int i=1; i<names.length(); i++) {
        des += " or ";
        des += names[i].asChar();
        des += " (" + std::to_string(probabilities[i]) + ")";
      }
      des += " (vol = " + std::to_string(volume) + ")";
      return des;
    }

    MStatus AddName(MString nextName)
    {
      MStatus status = names.append(nextName);
      return status;
    }

    MStatus AddProb(const float nextProb)
    {
      MStatus status = probabilities.append(nextProb);
      return status;
    }
  };
  
  
  static const char* mayaCommand;
  
  // Marker event times
  static MDoubleArray time_result;

  // Marker event volume times
  static MDoubleArray time_volume;

  // Marker event probability times
  static MDoubleArray time_prob;

  // Marker event value
  static MDoubleArray val_result;

  // Marker event volume value
  static MDoubleArray val_volume;

  // Marker event probability value
  static MDoubleArray val_prob;

  // All Enum Names list
  static MStringArray enum_array;
  
  // This is called after the MayaCommand has been called and the event data has been updated
  static std::function<void(void)> updateEventDataCompleteFunc;
  
  
  UpdateMarkerDataCmd();
  
  virtual ~UpdateMarkerDataCmd();
  
  virtual MStatus doIt(const MArgList&);
  
  static void* creator();
  
  static void cleanUp();
  
  static const AudioEventInfo* FrameEvent(uint64_t frame);
  
  
private:
  
  void updateEventData(void* data);

  void AddEventToMapping(MDoubleArray eventVals, MDoubleArray eventTimes, MDoubleArray probVals, MDoubleArray probTimes);

  void UpdateAudioEventVolumes();
  
  static std::unordered_map< uint64_t, AudioEventInfo > _frameEvents;
  
};


#endif /* UpdateMarkerDataCmd_hpp */
