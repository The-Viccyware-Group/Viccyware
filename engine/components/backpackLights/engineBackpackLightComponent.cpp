/**
 * File: engineBackpackLightComponent.cpp
 *
 * Author: Al Chaussee
 * Created: 1/23/2017
 *
 * Description: Manages various lights on Vector's body.
 *              Currently this includes the backpack lights
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/components/backpackLights/engineBackpackLightComponent.h"

#include "coretech/common/engine/utils/data/dataPlatform.h"
#include "coretech/common/engine/utils/timer.h"
#include "engine/engineTimeStamp.h"
#include "engine/components/battery/batteryComponent.h"
#include "engine/components/visionComponent.h"
#include "engine/cozmoContext.h"
#include "engine/events/ankiEvent.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/robot.h"
#include "engine/robotDataLoader.h"
#include "engine/robotInterface/messageHandler.h"
#include "engine/robotManager.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "util/console/consoleInterface.h"
#include "util/fileUtils/fileUtils.h"

#include "osState/osState.h"
#include <cstdlib>

#define DEBUG_LIGHTS 0

namespace Anki {
namespace Vector {

// Backpack lights can be played as a trigger or manually defined
// animations. In order to support both in this struct an anonymous
// union is used
struct BackpackLightData
{
  bool _isTrigger = false;
  union {
    BackpackLightAnimation::BackpackAnimation _lightConfiguration{};
    BackpackAnimationTrigger _trigger;
  };
};
  
BackpackLightComponent::BackpackLightComponent()
: IDependencyManagedComponent(this, RobotComponentID::BackpackLights)
{  
  static_assert((int)LEDId::NUM_BACKPACK_LEDS == 3, "BackpackLightComponent.WrongNumBackpackLights");
}

void BackpackLightComponent::InitDependent(Vector::Robot* robot, const RobotCompMap& dependentComps)
{
  _robot = robot;

  // Subscribe to messages
  if( _robot->HasExternalInterface() ) {
    auto helper = MakeAnkiEventUtil(*_robot->GetExternalInterface(), *this, _eventHandles);
    using namespace ExternalInterface;
    helper.SubscribeGameToEngine<MessageGameToEngineTag::SetBackpackLEDs>();
  }
}

void BackpackLightComponent::UpdateDependent(const RobotCompMap& dependentComps)
{  
  BackpackLightDataRefWeak bestNewConfig = BackpackLightDataRef{};
  if(!_backpackLightList.empty())
  {
    bestNewConfig = *_backpackLightList.begin();
  }
  
  auto newConfig = bestNewConfig.lock();
  auto curConfig = _curBackpackLightConfig.lock();

  // Prevent spamming of off trigger while both configs are null
  static bool sBothConfigsWereNull = false;

  // If the best config at this time is different from what we had, change it
  if (newConfig != curConfig)
  {
    sBothConfigsWereNull = false;
    
    // If the best config is still a thing, use it. Otherwise use the off config
    if (newConfig != nullptr)
    {
      if(newConfig->_isTrigger)
      {
        SetBackpackAnimationInternal(newConfig->_trigger);
      }
      else
      {
        SetBackpackAnimationInternal(newConfig->_lightConfiguration);
      }
    }
    else
    {
      CheckStimAndConfidence();
      if (_currentConfidence < -0.29) {
        SetBackpackAnimationInternal(BackpackAnimationTrigger::On_Frustrated);
      } else {
        if (_currentStim <= 0.8) {
          SetBackpackAnimationInternal(BackpackAnimationTrigger::On_High_Stim);
        }
        else if (_currentStim < 0.20 && _currentStim > 0.80) {
          SetBackpackAnimationInternal(BackpackAnimationTrigger::On_Med_Stim);
        }
        else if (_currentStim <= 0.20) {
          SetBackpackAnimationInternal(BackpackAnimationTrigger::On_Low_Stim);
        }
        else {
          // We should never get here but just in case we do...
          SetBackpackAnimationInternal(BackpackAnimationTrigger::On_High_Stim);
        }
      }
    }
    
    _curBackpackLightConfig = bestNewConfig;
  }
  // Else if both new and cur configs are null then turn lights off
  else if(newConfig == nullptr && curConfig == nullptr && !sBothConfigsWereNull)
  {
    sBothConfigsWereNull = true;
    CheckStimAndConfidence();
    if (_currentConfidence < -0.29) {
      SetBackpackAnimationInternal(BackpackAnimationTrigger::On_Frustrated);
    } else {
      if (_currentStim <= 0.8) {
        SetBackpackAnimationInternal(BackpackAnimationTrigger::On_High_Stim);
      }
      else if (_currentStim < 0.20 && _currentStim > 0.80) {
        SetBackpackAnimationInternal(BackpackAnimationTrigger::On_Med_Stim);
      }
      else if (_currentStim <= 0.20) {
        SetBackpackAnimationInternal(BackpackAnimationTrigger::On_Low_Stim);
      }
      else {
        // We should never get here but just in case we do...
        SetBackpackAnimationInternal(BackpackAnimationTrigger::On_High_Stim);
      }
    }
  }
}

void BackpackLightComponent::CheckStimAndConfidence()
{
  // Helper lambda to read command output with timeout
  auto getCommandOutput = [](const char* cmd) -> float {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
      PRINT_NAMED_WARNING("BackpackLightComponent.CheckStimAndConfidence.PopenFailed",
                          "Failed to open pipe");
      return 0.0f;
    }
    
    char buffer[128];
    std::string result = "";
    
    // Set a timeout on the file descriptor
    int fd = fileno(pipe);
    fd_set readfds;
    struct timeval timeout;
    
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    
    // 100ms timeout
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    
    int select_result = select(fd + 1, &readfds, NULL, NULL, &timeout);
    
    if (select_result > 0) {
      // Data is available
      if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = buffer;
      }
    } else if (select_result == 0) {
      PRINT_NAMED_WARNING("BackpackLightComponent.CheckStimAndConfidence.Timeout",
                          "Command timed out");
      pclose(pipe);
      return 0.0f;
    } else {
      PRINT_NAMED_WARNING("BackpackLightComponent.CheckStimAndConfidence.SelectError",
                          "Select error");
      pclose(pipe);
      return 0.0f;
    }
    
    pclose(pipe);
    
    // Parse result
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    
    if (result.empty()) {
      return 0.0f;
    }
    
    try {
      return std::stof(result);
    } catch (...) {
      PRINT_NAMED_WARNING("BackpackLightComponent.CheckStimAndConfidence.ParseError",
                          "Failed to parse: %s", result.c_str());
      return 0.0f;
    }
  };
  
  // Add timeout to curl commands (--max-time 0.1 = 100ms)
  _currentStim = getCommandOutput("curl --max-time 0.1 -s 'http://localhost:8888/getmoodvalues' | awk -F '[:,]' '/\"Stimulated\"/ { gsub(\"[[:space:]\"]+\", \"\", $2); print $2 }'");
  _currentConfidence = getCommandOutput("curl --max-time 0.1 -s 'http://localhost:8888/getmoodvalues' | awk -F '[:,]' '/\"Confident\"/ { gsub(\"[[:space:]\"]+\", \"\", $2); print $2 }'");
}

void BackpackLightComponent::SetBackpackAnimation(const BackpackLightAnimation::BackpackAnimation& lights)
{
  StartLoopingBackpackAnimationInternal(lights, _sharedLightConfig);
}

void BackpackLightComponent::SetBackpackAnimation(const BackpackAnimationTrigger& trigger)
{
  StopLoopingBackpackAnimation(_sharedLightConfig);

  _backpackLightList.emplace_front(new BackpackLightData{._isTrigger = true,
                                                         ._trigger = trigger});
  
  BackpackLightDataLocator result{};
  result._listIter = _backpackLightList.begin();
  result._dataPtr = std::weak_ptr<BackpackLightData>(*result._listIter);
  
  _sharedLightConfig = std::move(result);
}

void BackpackLightComponent::SetBackpackAnimationInternal(const BackpackAnimationTrigger& trigger)
{
  _robot->SendRobotMessage<RobotInterface::TriggerBackpackAnimation>(trigger);
}

void BackpackLightComponent::StartLoopingBackpackAnimation(const BackpackLightAnimation::BackpackAnimation& lights,
                                                           BackpackLightDataLocator& lightLocator_out)
{
  StartLoopingBackpackAnimationInternal(lights, lightLocator_out);
}

bool BackpackLightComponent::StopLoopingBackpackAnimation(const BackpackLightDataLocator& lightDataLocator)
{
  if (!lightDataLocator.IsValid())
  {
    PRINT_CH_INFO("BackpackLightComponent", "BackpackLightComponent::RemoveBackpackLightConfig.InvalidLocator",
                  "Trying to remove an invalid locator.");
    return false;
  }
  
  if(!_backpackLightList.empty())
  {
    _backpackLightList.erase(lightDataLocator._listIter);
  }
  else
  {
    PRINT_NAMED_WARNING("BackpackLightComponent.StopLoopingBackpackAnimation.NoLocators",
                        "Trying to remove supposedly valid locator but locator list is empty");
    return false;
  }
  
  return true;
}
  
template<>
void BackpackLightComponent::HandleMessage(const ExternalInterface::SetBackpackLEDs& msg)
{
  const BackpackLightAnimation::BackpackAnimation lights = {
    .onColors               = msg.onColor,
    .offColors              = msg.offColor,
    .onPeriod_ms            = msg.onPeriod_ms,
    .offPeriod_ms           = msg.offPeriod_ms,
    .transitionOnPeriod_ms  = msg.transitionOnPeriod_ms,
    .transitionOffPeriod_ms = msg.transitionOffPeriod_ms,
    .offset                 = msg.offset
  };
  
  SetBackpackAnimation(lights);
}

Result BackpackLightComponent::SetBackpackAnimationInternal(const BackpackLightAnimation::BackpackAnimation& lights)
{
  return _robot->SendMessage(RobotInterface::EngineToRobot(lights.ToMsg()));
}
 
  
void BackpackLightComponent::StartLoopingBackpackAnimationInternal(const BackpackLightAnimation::BackpackAnimation& lights,
                                                                   BackpackLightDataLocator& lightLocator_out)
{
  StopLoopingBackpackAnimation(lightLocator_out);

  _backpackLightList.emplace_front(new BackpackLightData{._isTrigger = false,
                                                         ._lightConfiguration = std::move(lights)});
  
  BackpackLightDataLocator result{};
  result._listIter = _backpackLightList.begin();
  result._dataPtr = std::weak_ptr<BackpackLightData>(*result._listIter);
  
  lightLocator_out = std::move(result);
}  

}
}
