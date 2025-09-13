/**
 * File: BehaviorPetDetection.cpp
 *
 * Author: Wire
 * Created: 2025-06-12
 *
 * Description: React to dogs and cats.
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/victor/behaviorPetDetection.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "engine/actions/animActions.h"
#include "engine/aiComponent/beiConditions/beiConditionFactory.h"
#include "engine/aiComponent/beiConditions/iBEICondition.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"


#include <chrono>

namespace Anki {
namespace Vector {

using Clock = std::chrono::steady_clock;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorPetDetection::BehaviorPetDetection(const Json::Value& config)
 : ICozmoBehavior(config)
 , _activate(false)
 , _isDog(false)
 , _activationCount(0)
 , _cooldownUntil(Clock::now())
{
  SubscribeToTags({
    ExternalInterface::MessageEngineToGameTag::RobotObservedPet,
  });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorPetDetection::InstanceConfig::InstanceConfig()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPetDetection::InitBehavior()
{
  const auto& BC = GetBEI().GetBehaviorContainer();

  _iConfig.driveOffChargerBehavior = BC.FindBehaviorByID( BEHAVIOR_ID( DriveOffChargerFace ) );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPetDetection::GetAllDelegates(std::set<IBehavior*>& delegates) const
{
  delegates.insert( _iConfig.driveOffChargerBehavior.get() );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorPetDetection::WantsToBeActivatedBehavior() const
{
  return _activate;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPetDetection::GetBehaviorJsonKeys(std::set<const char*>&) const
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPetDetection::OnBehaviorActivated()
{

  // the first three events have a 10 second cooldown
  // the next one has a 100-200 second cooldown
  CancelDelegates();
  _activationCount++;

  if (_activationCount >= 3) {
    _activationCount = 0;
    int randSec = GetRNG().RandIntInRange(150, 200);
    _cooldownUntil = Clock::now() + std::chrono::seconds(randSec);
  } else {
    _cooldownUntil = Clock::now() + std::chrono::seconds(15);
  }

  if( GetBEI().GetRobotInfo().IsOnChargerPlatform() &&
      _iConfig.driveOffChargerBehavior->WantsToBeActivated() ) {
      DelegateIfInControl(_iConfig.driveOffChargerBehavior.get(), &BehaviorPetDetection::PlayAnimation);
  } else {
    PlayAnimation();
  }

  _activate = false;
}

void BehaviorPetDetection::PlayAnimation() {
  CancelDelegates();
  AnimationTrigger trig = _isDog ? AnimationTrigger::PetDetectionDog
                                 : AnimationTrigger::PetDetectionCat;
  auto* action = new TriggerLiftSafeAnimationAction(trig);

  DelegateIfInControl(action, [](const ActionResult& result) {
    ANKI_VERIFY( result == ActionResult::SUCCESS,
                 "BehaviorPetDetection.OnBehaviorActivated.AnimFail",
                 "Could not play animation" );
  });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPetDetection::AlwaysHandleInScope(const EngineToGameEvent& ev)
{
  // don't want two at once
  if (_activate) {
    return;
  }

  if (ev.GetData().GetTag() != ExternalInterface::MessageEngineToGameTag::RobotObservedPet) {
    return;
  }

  if (Clock::now() < _cooldownUntil) {
    return;
  }

  const auto& petMsg = ev.GetData().Get_RobotObservedPet();
  // okao hits us with a burst of events whenever a pet is seen.
  // let's account for false-positives a little bit by needing 6 events
  if (!_activate) {
    if (petMsg.numTimesObserved >= 6) {
      if (petMsg.img_rect.height > 100) {
        _isDog = (petMsg.petType == Anki::Vision::PetType::Dog);
        _activate = true;
      } else {
        PRINT_NAMED_INFO("BehaviorPetDetection", "height below 100");
      }
    }
  } else {
    PRINT_NAMED_INFO("BehaviorPetDetection", "tried to activate while already activated");
  }
}

} // namespace Vector
} // namespace Anki
