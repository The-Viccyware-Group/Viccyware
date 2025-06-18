/**
 * File: BehaviorPetDetection.h
 *
 * Author: Wire
 * Created: 2025-06-12
 *
 * Description: React to dogs and cats.
 *
 * Copyright: Anki, Inc. 2025
 *
 **/

#ifndef __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorPetDetection__
#define __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorPetDetection__
#pragma once

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"
#include <chrono>

namespace Anki {
namespace Vector {

class BehaviorPetDetection : public ICozmoBehavior
{
public: 
  virtual ~BehaviorPetDetection() { }

protected:

  friend class BehaviorFactory;
  explicit BehaviorPetDetection(const Json::Value& config);  

  virtual void GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const override {
    modifiers.behaviorAlwaysDelegates               = true;
    modifiers.wantsToBeActivatedWhenOnCharger       = true;
    modifiers.wantsToBeActivatedWhenOffTreads       = true;
  }

  virtual void GetAllDelegates(std::set<IBehavior*>& delegates) const override;

  virtual void InitBehavior() override;
  virtual void PlayAnimation();

  virtual void GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const override;
  
  virtual bool WantsToBeActivatedBehavior() const override;
  virtual void OnBehaviorActivated() override;
  // virtual void BehaviorUpdate() override;
  virtual void AlwaysHandleInScope(const EngineToGameEvent& event) override;

private:

  struct InstanceConfig {
    InstanceConfig();
    ICozmoBehaviorPtr driveOffChargerBehavior;
  };

  InstanceConfig _iConfig;

  bool _activate;
  bool _isDog;
  int  _activationCount = 0;
  std::chrono::steady_clock::time_point _cooldownUntil;

  
};

} // namespace Vector
} // namespace Anki

#endif // __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorPetDetection__
