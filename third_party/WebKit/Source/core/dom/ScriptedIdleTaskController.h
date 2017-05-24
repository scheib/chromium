// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ScriptedIdleTaskController_h
#define ScriptedIdleTaskController_h

#include "core/dom/IdleDeadline.h"
#include "core/dom/SuspendableObject.h"
#include "platform/Timer.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Vector.h"

namespace blink {
namespace internal {
class IdleRequestCallbackWrapper;
}

class ExecutionContext;
class IdleRequestCallback;
class IdleRequestOptions;

class CORE_EXPORT ScriptedIdleTaskController
    : public GarbageCollectedFinalized<ScriptedIdleTaskController>,
      public SuspendableObject {
  USING_GARBAGE_COLLECTED_MIXIN(ScriptedIdleTaskController);

 public:
  static ScriptedIdleTaskController* Create(ExecutionContext* context) {
    return new ScriptedIdleTaskController(context);
  }
  ~ScriptedIdleTaskController();

  DECLARE_TRACE();

  using CallbackId = int;

  int RegisterCallback(IdleRequestCallback*, const IdleRequestOptions&);
  void CancelCallback(CallbackId);

  // SuspendableObject interface.
  void ContextDestroyed(ExecutionContext*) override;
  void Suspend() override;
  void Resume() override;

  void CallbackFired(CallbackId,
                     double deadline_seconds,
                     IdleDeadline::CallbackType);

 private:
  friend class internal::IdleRequestCallbackWrapper;
  explicit ScriptedIdleTaskController(ExecutionContext*);

  void ScheduleCallback(RefPtr<internal::IdleRequestCallbackWrapper>,
                        long long timeout_millis);

  int NextCallbackId();

  bool IsValidCallbackId(int id) {
    using Traits = HashTraits<CallbackId>;
    return !Traits::IsDeletedValue(id) &&
           !WTF::IsHashTraitsEmptyValue<Traits, CallbackId>(id);
  }

  void RunCallback(CallbackId,
                   double deadline_seconds,
                   IdleDeadline::CallbackType);

  WebScheduler* scheduler_;  // Not owned.
  HeapHashMap<CallbackId, Member<IdleRequestCallback>> callbacks_;
  Vector<CallbackId> pending_timeouts_;
  CallbackId next_callback_id_;
  bool suspended_;
};

}  // namespace blink

#endif  // ScriptedIdleTaskController_h
