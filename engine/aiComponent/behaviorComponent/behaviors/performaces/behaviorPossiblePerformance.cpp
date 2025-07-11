/**
 * File: BehaviorPossiblePerformance.cpp
 *
 * Author: Brad
 * Created: 2019-04-23
 *
 * Description: Checks face requirement and a special long cooldown to decide if it's time to do the
 * (specified) performance
 *
 * Copyright: Anki, Inc. 2019
 *
 **/


#include "engine/aiComponent/behaviorComponent/behaviors/performaces/behaviorPossiblePerformance.h"

#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/utils/timer.h"
#include "engine/actions/animActions.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/faceWorld.h"
#include "util/cladHelpers/cladFromJSONHelpers.h"
#include "util/console/consoleInterface.h"

#define LOG_CHANNEL "Behaviors"
#define CONSOLE_GROUP "PerformanceAnims"

CONSOLE_VAR(bool, kOverridePerformanceCooldowns, CONSOLE_GROUP, false);
CONSOLE_VAR(bool, kOverridePerformanceFaceNeeded, CONSOLE_GROUP, true);

namespace Anki {
namespace Vector {

namespace {
const char* kCooldownKey = "cooldown_hours";
const char* kAnimTriggerKey = "animTrigger";
const char* kProbabilityKey = "probability";
const char* kPeriodKey = "period_hours";

static const u32 kMaxTimeSinceSeenFaceToAct_ms = 2000;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorPossiblePerformance::InstanceConfig::InstanceConfig()
  : animTrigger(AnimationTrigger::Count)
  , cooldown_s(-1.0f)
  , probability(1.0f)
  , rerollPeriod_s(1000.0f)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorPossiblePerformance::BehaviorPossiblePerformance(const Json::Value& config)
 : ICozmoBehavior(config)
{
  _iConfig.cooldown_s = 60.0f * 60.0f * JsonTools::ParseFloat(config, kCooldownKey,
                                                              "BehaviorPossiblePerformance.ConfigError");
  
  _iConfig.probability = JsonTools::ParseFloat(config, kProbabilityKey, "BehaviorPossiblePerformance.ConfigError");
  
  _iConfig.rerollPeriod_s = 60.0f * 60.0f * JsonTools::ParseFloat(config, kPeriodKey,
                                                                  "BehaviorPossiblePerformance.ConfigError");
  
  _iConfig.animTrigger = AnimationTriggerFromString( JsonTools::ParseString(config,
                                                                            kAnimTriggerKey,
                                                                            "BehaviorPossiblePerformance.ConfigError") );

  // TEMP: load remaining cooldown from disk
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorPossiblePerformance::~BehaviorPossiblePerformance()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPossiblePerformance::InitBehavior()
{
  const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  
  if( _pVars.onCooldownUntil_s < 0.0f ) {
    // no cooldown specified, so randomly roll it so that we don't always land X time after boot
    const float cooldownLength_s = _iConfig.cooldown_s;
    const float cooldownTime_s = GetRNG().RandDbl( cooldownLength_s );
   
    LOG_WARNING("BehaviorPossiblePerformance.InitCoolodwn", "%s: cooldown in range [0, %f) -> %f",
                GetDebugLabel().c_str(),
                cooldownLength_s,
                cooldownTime_s);

    _pVars.onCooldownUntil_s = currTime_s + cooldownTime_s;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorPossiblePerformance::WantsToBeActivatedBehavior() const
{
  // TEMP: check for recent face
  const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  const bool onCooldown = currTime_s <= _pVars.onCooldownUntil_s; // TEMP: console var override
  const bool ready = kOverridePerformanceCooldowns || ( !onCooldown && _pVars.lastRollPassed );

  const auto& faceWorld = GetBEI().GetFaceWorld();
  const bool requireRecognizableFace = true;
  const bool hasFace = kOverridePerformanceFaceNeeded ||
    faceWorld.HasAnyFaces(GetRecentFaceTime(), requireRecognizableFace);
  
  return ready && hasFace;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
RobotTimeStamp_t BehaviorPossiblePerformance::GetRecentFaceTime() const
{
  const RobotTimeStamp_t lastImgTime = GetBEI().GetRobotInfo().GetLastImageTimeStamp();
  const RobotTimeStamp_t recentTime = lastImgTime > kMaxTimeSinceSeenFaceToAct_ms ?
                                      ( lastImgTime - kMaxTimeSinceSeenFaceToAct_ms ) :
                                      0;
  return recentTime;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPossiblePerformance::GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const
{
  modifiers.wantsToBeActivatedWhenOnCharger = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPossiblePerformance::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
  const char* list[] = {
    kCooldownKey,
    kAnimTriggerKey,
    kProbabilityKey,
    kPeriodKey,
  };
  expectedKeys.insert( std::begin(list), std::end(list) );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPossiblePerformance::OnBehaviorActivated() 
{
  // Start the animation
  DelegateIfInControl( new TriggerAnimationAction( _iConfig.animTrigger ) );

  // reset cooldown
  const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  _pVars.onCooldownUntil_s = currTime_s + _iConfig.cooldown_s;

  // Make sure we know a performance is happening
  _pVars.isPerformingAnimation = true;

  // Max possible time for a performance
  float estimatedAnimDuration_s = 26.0f;
  _pVars.animationEndTime_s = currTime_s + estimatedAnimDuration_s;
  
  LOG_WARNING("BehaviorPossiblePerformance.Activated",
              "%s: Starting performance, estimated end time: %f",
              GetDebugLabel().c_str(),
              _pVars.animationEndTime_s);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPossiblePerformance::BehaviorUpdate()
{
  const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  
  if( IsActivated()) {

    bool cancel = false;

    // Check if our animation has completed based on the timer
    if (_pVars.isPerformingAnimation && currTime_s >= _pVars.animationEndTime_s) {
      // Animation should be finished by now, mark our performance as complete and deactivate
      LOG_WARNING("BehaviorPossiblePerformance.Update.CompletingPerformance",
                "%s: Animation complete, deactivating behavior",
                GetDebugLabel().c_str());
      _pVars.isPerformingAnimation = false;
      cancel = true;
    }

    if (cancel) {
      CancelSelf();
    }

  } else {
    // Only check for reroll if not currently performing
    if (!_pVars.isPerformingAnimation) {
      if (currTime_s > _pVars.onCooldownUntil_s) {
        // off cooldown, check if we should re-roll the probability
        if (currTime_s > _pVars.nextProbRerollAfter_s) {
          // re-roll!
          _pVars.lastRollPassed = GetRNG().RandBool(_iConfig.probability);

          // reset next re-roll time
          _pVars.nextProbRerollAfter_s = currTime_s + _iConfig.rerollPeriod_s;
          
          LOG_WARNING("BehaviorPossiblePerformance.Update.ReRoll",
                      "%s: rerolled: %s, next reroll at t=%f",
                      GetDebugLabel().c_str(),
                      _pVars.lastRollPassed ? "PASS" : "FAIL",
                      _pVars.nextProbRerollAfter_s);
        }
      }
    }
  }
}

}
}
