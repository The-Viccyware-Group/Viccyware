/**
 * File: CladFromJSONHelpers.h
 *
 * Author: Kevin M. Karol
 * Created: 4/3/18
 *
 * Description: Helper functions for parsing CLAD types from JSON
 *
 * Copyright: Anki, Inc. 2018
 *
 **/


#ifndef __Cozmo_Basestation_Events_CladFromJSONHelpers_H__
#define __Cozmo_Basestation_Events_CladFromJSONHelpers_H__


#include "clad/types/animationTrigger.h"
#include "json/json-forwards.h"


namespace Anki {  
namespace JsonTools {

template<class CladEnum>
bool ParseCladEnumFromJSON(const Json::Value& json, CladEnum& value,
                           const std::string& debugName, bool shouldAssertIfMissing = true)
{
  if(!json.isString()){
    std::string debugMsgStr = debugName + ".GetCladEnumFromJSON.ParseString.ValueNotString";
    const char* debugMsg = debugMsgStr.c_str();
    DEV_ASSERT(!shouldAssertIfMissing, debugMsg);
    return false;
  }
  std::string str = json.asString();
  // Note - this functionality can only be used in engine until VIC-2545 is imlemented
  const bool foundValue = Vector::EnumFromString(str.c_str(), value);
  if(!foundValue){
    std::string debugMsgStr = debugName + "GetCladEnumFromJSON.ParseString.InvalidValue";
    const char* debugMsg = debugMsgStr.c_str();
    DEV_ASSERT(!shouldAssertIfMissing, debugMsg);
  }

  return foundValue;
}

template<class CladEnum>
bool GetCladEnumFromJSON(const Json::Value& config, const std::string& key,  CladEnum& value, 
                         const std::string& debugName, bool shouldAssertIfMissing = true)
{
  const Json::Value& child = config[key];
  if(child.isNull()){
    std::string debugMsgStr = debugName + ".GetCladEnumFromJSON.ParseString.KeyMissing";
    const char* debugMsg = debugMsgStr.c_str();
    DEV_ASSERT_MSG(!shouldAssertIfMissing, debugMsg, "%s", key.c_str());
    return false;
  }
  return ParseCladEnumFromJSON(child, value, debugName, shouldAssertIfMissing);
}

} // namespace Vector
} // namespace Anki


#endif // __Cozmo_Basestation_Events_CladFromJSONHelpers_H__

