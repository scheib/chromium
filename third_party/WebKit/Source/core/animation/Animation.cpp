/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/animation/Animation.h"

#include "core/animation/AnimationTimeline.h"
#include "core/animation/CompositorPendingAnimations.h"
#include "core/animation/KeyframeEffectReadOnly.h"
#include "core/animation/css/CSSAnimations.h"
#include "core/dom/DOMNodeIds.h"
#include "core/dom/Document.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/ExecutionContext.h"
#include "core/dom/StyleChangeReason.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/events/AnimationPlaybackEvent.h"
#include "core/frame/UseCounter.h"
#include "core/inspector/InspectorTraceEvents.h"
#include "core/paint/PaintLayer.h"
#include "core/probe/CoreProbes.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/ScriptForbiddenScope.h"
#include "platform/WebTaskRunner.h"
#include "platform/animation/CompositorAnimationPlayer.h"
#include "platform/heap/Persistent.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/wtf/MathExtras.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/Platform.h"
#include "public/platform/WebCompositorSupport.h"

namespace blink {

namespace {

static unsigned NextSequenceNumber() {
  static unsigned next = 0;
  return ++next;
}
}

Animation* Animation::Create(AnimationEffectReadOnly* effect,
                             AnimationTimeline* timeline) {
  if (!timeline) {
    // FIXME: Support creating animations without a timeline.
    return nullptr;
  }

  Animation* animation = new Animation(
      timeline->GetDocument()->ContextDocument(), *timeline, effect);

  if (timeline) {
    timeline->AnimationAttached(*animation);
    animation->AttachCompositorTimeline();
  }

  return animation;
}

Animation::Animation(ExecutionContext* execution_context,
                     AnimationTimeline& timeline,
                     AnimationEffectReadOnly* content)
    : ContextLifecycleObserver(execution_context),
      play_state_(kIdle),
      playback_rate_(1),
      start_time_(NullValue()),
      hold_time_(0),
      sequence_number_(NextSequenceNumber()),
      content_(content),
      timeline_(&timeline),
      paused_(false),
      held_(false),
      is_paused_for_testing_(false),
      is_composited_animation_disabled_for_testing_(false),
      outdated_(false),
      finished_(true),
      compositor_state_(nullptr),
      compositor_pending_(false),
      compositor_group_(0),
      current_time_pending_(false),
      state_is_being_updated_(false),
      effect_suppressed_(false) {
  if (content_) {
    if (content_->GetAnimation()) {
      content_->GetAnimation()->cancel();
      content_->GetAnimation()->setEffect(0);
    }
    content_->Attach(this);
  }
  probe::didCreateAnimation(timeline_->GetDocument(), sequence_number_);
}

Animation::~Animation() {
  // Verify that m_compositorPlayer has been disposed of.
  DCHECK(!compositor_player_);
}

void Animation::Dispose() {
  DestroyCompositorPlayer();
  // If the AnimationTimeline and its Animation objects are
  // finalized by the same GC, we have to eagerly clear out
  // this Animation object's compositor player registration.
  DCHECK(!compositor_player_);
}

double Animation::EffectEnd() const {
  return content_ ? content_->EndTimeInternal() : 0;
}

bool Animation::Limited(double current_time) const {
  return (playback_rate_ < 0 && current_time <= 0) ||
         (playback_rate_ > 0 && current_time >= EffectEnd());
}

void Animation::setCurrentTime(double new_current_time, bool is_null) {
  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand);

  if (PlayStateInternal() == kIdle)
    paused_ = true;

  current_time_pending_ = false;
  play_state_ = kUnset;
  SetCurrentTimeInternal(new_current_time / 1000, kTimingUpdateOnDemand);

  if (CalculatePlayState() == kFinished)
    start_time_ = CalculateStartTime(new_current_time);
}

void Animation::SetCurrentTimeInternal(double new_current_time,
                                       TimingUpdateReason reason) {
  DCHECK(std::isfinite(new_current_time));

  bool old_held = held_;
  bool outdated = false;
  bool is_limited = Limited(new_current_time);
  held_ = paused_ || !playback_rate_ || is_limited || std::isnan(start_time_);
  if (held_) {
    if (!old_held || hold_time_ != new_current_time)
      outdated = true;
    hold_time_ = new_current_time;
    if (paused_ || !playback_rate_) {
      start_time_ = NullValue();
    } else if (is_limited && std::isnan(start_time_) &&
               reason == kTimingUpdateForAnimationFrame) {
      start_time_ = CalculateStartTime(new_current_time);
    }
  } else {
    hold_time_ = NullValue();
    start_time_ = CalculateStartTime(new_current_time);
    finished_ = false;
    outdated = true;
  }

  if (outdated) {
    SetOutdated();
  }
}

// Update timing to reflect updated animation clock due to tick
void Animation::UpdateCurrentTimingState(TimingUpdateReason reason) {
  if (play_state_ == kIdle)
    return;
  if (held_) {
    double new_current_time = hold_time_;
    if (play_state_ == kFinished && !IsNull(start_time_) && timeline_) {
      // Add hystersis due to floating point error accumulation
      if (!Limited(CalculateCurrentTime() + 0.001 * playback_rate_)) {
        // The current time became unlimited, eg. due to a backwards
        // seek of the timeline.
        new_current_time = CalculateCurrentTime();
      } else if (!Limited(hold_time_)) {
        // The hold time became unlimited, eg. due to the effect
        // becoming longer.
        new_current_time =
            clampTo<double>(CalculateCurrentTime(), 0, EffectEnd());
      }
    }
    SetCurrentTimeInternal(new_current_time, reason);
  } else if (Limited(CalculateCurrentTime())) {
    held_ = true;
    hold_time_ = playback_rate_ < 0 ? 0 : EffectEnd();
  }
}

double Animation::startTime(bool& is_null) const {
  double result = startTime();
  is_null = std::isnan(result);
  return result;
}

double Animation::startTime() const {
  return start_time_ * 1000;
}

double Animation::currentTime(bool& is_null) {
  double result = currentTime();
  is_null = std::isnan(result);
  return result;
}

double Animation::currentTime() {
  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand);

  if (PlayStateInternal() == kIdle || (!held_ && !HasStartTime()))
    return std::numeric_limits<double>::quiet_NaN();

  return CurrentTimeInternal() * 1000;
}

double Animation::CurrentTimeInternal() const {
  double result = held_ ? hold_time_ : CalculateCurrentTime();
#if DCHECK_IS_ON()
  // We can't enforce this check during Unset due to other
  // assertions.
  if (play_state_ != kUnset) {
    const_cast<Animation*>(this)->UpdateCurrentTimingState(
        kTimingUpdateOnDemand);
    DCHECK_EQ(result, (held_ ? hold_time_ : CalculateCurrentTime()));
  }
#endif
  return result;
}

double Animation::UnlimitedCurrentTimeInternal() const {
#if DCHECK_IS_ON()
  CurrentTimeInternal();
#endif
  return PlayStateInternal() == kPaused || IsNull(start_time_)
             ? CurrentTimeInternal()
             : CalculateCurrentTime();
}

bool Animation::PreCommit(
    int compositor_group,
    const Optional<CompositorElementIdSet>& composited_element_ids,
    bool start_on_compositor) {
  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand,
                                    kDoNotSetCompositorPending);

  bool soft_change =
      compositor_state_ &&
      (Paused() || compositor_state_->playback_rate != playback_rate_);
  bool hard_change =
      compositor_state_ && (compositor_state_->effect_changed ||
                            compositor_state_->start_time != start_time_);

  // FIXME: softChange && !hardChange should generate a Pause/ThenStart,
  // not a Cancel, but we can't communicate these to the compositor yet.

  bool changed = soft_change || hard_change;
  bool should_cancel = (!Playing() && compositor_state_) || changed;
  bool should_start = Playing() && (!compositor_state_ || changed);

  if (start_on_compositor && should_cancel && should_start &&
      compositor_state_ && compositor_state_->pending_action == kStart) {
    // Restarting but still waiting for a start time.
    return false;
  }

  if (should_cancel) {
    CancelAnimationOnCompositor();
    compositor_state_ = nullptr;
  }

  DCHECK(!compositor_state_ || !std::isnan(compositor_state_->start_time));

  if (!should_start) {
    current_time_pending_ = false;
  }

  if (should_start) {
    compositor_group_ = compositor_group;
    if (start_on_compositor) {
      if (IsCandidateForAnimationOnCompositor(composited_element_ids))
        CreateCompositorPlayer();

      if (MaybeStartAnimationOnCompositor(composited_element_ids))
        compositor_state_ = WTF::WrapUnique(new CompositorState(*this));
      else
        CancelIncompatibleAnimationsOnCompositor();
    }
  }

  return true;
}

void Animation::PostCommit(double timeline_time) {
  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand,
                                    kDoNotSetCompositorPending);

  compositor_pending_ = false;

  if (!compositor_state_ || compositor_state_->pending_action == kNone)
    return;

  switch (compositor_state_->pending_action) {
    case kStart:
      if (!std::isnan(compositor_state_->start_time)) {
        DCHECK_EQ(start_time_, compositor_state_->start_time);
        compositor_state_->pending_action = kNone;
      }
      break;
    case kPause:
    case kPauseThenStart:
      DCHECK(std::isnan(start_time_));
      compositor_state_->pending_action = kNone;
      SetCurrentTimeInternal(
          (timeline_time - compositor_state_->start_time) * playback_rate_,
          kTimingUpdateForAnimationFrame);
      current_time_pending_ = false;
      break;
    default:
      NOTREACHED();
  }
}

void Animation::NotifyCompositorStartTime(double timeline_time) {
  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand,
                                    kDoNotSetCompositorPending);

  if (compositor_state_) {
    DCHECK_EQ(compositor_state_->pending_action, kStart);
    DCHECK(std::isnan(compositor_state_->start_time));

    double initial_compositor_hold_time = compositor_state_->hold_time;
    compositor_state_->pending_action = kNone;
    compositor_state_->start_time =
        timeline_time + CurrentTimeInternal() / -playback_rate_;

    if (start_time_ == timeline_time) {
      // The start time was set to the incoming compositor start time.
      // Unlikely, but possible.
      // FIXME: Depending on what changed above this might still be pending.
      // Maybe...
      current_time_pending_ = false;
      return;
    }

    if (!std::isnan(start_time_) ||
        CurrentTimeInternal() != initial_compositor_hold_time) {
      // A new start time or current time was set while starting.
      SetCompositorPending(true);
      return;
    }
  }

  NotifyStartTime(timeline_time);
}

void Animation::NotifyStartTime(double timeline_time) {
  if (Playing()) {
    DCHECK(std::isnan(start_time_));
    DCHECK(held_);

    if (playback_rate_ == 0) {
      SetStartTimeInternal(timeline_time);
    } else {
      SetStartTimeInternal(timeline_time +
                           CurrentTimeInternal() / -playback_rate_);
    }

    // FIXME: This avoids marking this animation as outdated needlessly when a
    // start time is notified, but we should refactor how outdating works to
    // avoid this.
    ClearOutdated();
    current_time_pending_ = false;
  }
}

bool Animation::Affects(const Element& element, CSSPropertyID property) const {
  if (!content_ || !content_->IsKeyframeEffectReadOnly())
    return false;

  const KeyframeEffectReadOnly* effect =
      ToKeyframeEffectReadOnly(content_.Get());
  return (effect->Target() == &element) &&
         effect->Affects(PropertyHandle(property));
}

double Animation::CalculateStartTime(double current_time) const {
  return timeline_->EffectiveTime() - current_time / playback_rate_;
}

double Animation::CalculateCurrentTime() const {
  if (IsNull(start_time_) || !timeline_)
    return 0;
  return (timeline_->EffectiveTime() - start_time_) * playback_rate_;
}

void Animation::setStartTime(double start_time, bool is_null) {
  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand);

  if (start_time == start_time_)
    return;

  current_time_pending_ = false;
  play_state_ = kUnset;
  paused_ = false;
  SetStartTimeInternal(start_time / 1000);
}

void Animation::SetStartTimeInternal(double new_start_time) {
  DCHECK(!paused_);
  DCHECK(std::isfinite(new_start_time));
  DCHECK_NE(new_start_time, start_time_);

  bool had_start_time = HasStartTime();
  double previous_current_time = CurrentTimeInternal();
  start_time_ = new_start_time;
  if (held_ && playback_rate_) {
    // If held, the start time would still be derrived from the hold time.
    // Force a new, limited, current time.
    held_ = false;
    double current_time = CalculateCurrentTime();
    if (playback_rate_ > 0 && current_time > EffectEnd()) {
      current_time = EffectEnd();
    } else if (playback_rate_ < 0 && current_time < 0) {
      current_time = 0;
    }
    SetCurrentTimeInternal(current_time, kTimingUpdateOnDemand);
  }
  UpdateCurrentTimingState(kTimingUpdateOnDemand);
  double new_current_time = CurrentTimeInternal();

  if (previous_current_time != new_current_time) {
    SetOutdated();
  } else if (!had_start_time && timeline_) {
    // Even though this animation is not outdated, time to effect change is
    // infinity until start time is set.
    ForceServiceOnNextFrame();
  }
}

void Animation::setEffect(AnimationEffectReadOnly* new_effect) {
  if (content_ == new_effect)
    return;
  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand,
                                    kSetCompositorPendingWithEffectChanged);

  double stored_current_time = CurrentTimeInternal();
  if (content_)
    content_->Detach();
  content_ = new_effect;
  if (new_effect) {
    // FIXME: This logic needs to be updated once groups are implemented
    if (new_effect->GetAnimation()) {
      new_effect->GetAnimation()->cancel();
      new_effect->GetAnimation()->setEffect(0);
    }
    new_effect->Attach(this);
    SetOutdated();
  }
  SetCurrentTimeInternal(stored_current_time, kTimingUpdateOnDemand);
}

const char* Animation::PlayStateString(AnimationPlayState play_state) {
  switch (play_state) {
    case kIdle:
      return "idle";
    case kPending:
      return "pending";
    case kRunning:
      return "running";
    case kPaused:
      return "paused";
    case kFinished:
      return "finished";
    default:
      NOTREACHED();
      return "";
  }
}

Animation::AnimationPlayState Animation::PlayStateInternal() const {
  DCHECK_NE(play_state_, kUnset);
  return play_state_;
}

Animation::AnimationPlayState Animation::CalculatePlayState() {
  if (paused_ && !current_time_pending_)
    return kPaused;
  if (play_state_ == kIdle)
    return kIdle;
  if (current_time_pending_ || (IsNull(start_time_) && playback_rate_ != 0))
    return kPending;
  if (Limited())
    return kFinished;
  return kRunning;
}

void Animation::pause(ExceptionState& exception_state) {
  if (paused_)
    return;

  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand);

  double new_current_time = CurrentTimeInternal();
  if (CalculatePlayState() == kIdle) {
    if (playback_rate_ < 0 &&
        EffectEnd() == std::numeric_limits<double>::infinity()) {
      exception_state.ThrowDOMException(
          kInvalidStateError,
          "Cannot pause, Animation has infinite target effect end.");
      return;
    }
    new_current_time = playback_rate_ < 0 ? EffectEnd() : 0;
  }

  play_state_ = kUnset;
  paused_ = true;
  current_time_pending_ = true;
  SetCurrentTimeInternal(new_current_time, kTimingUpdateOnDemand);
}

void Animation::Unpause() {
  if (!paused_)
    return;

  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand);

  current_time_pending_ = true;
  UnpauseInternal();
}

void Animation::UnpauseInternal() {
  if (!paused_)
    return;
  paused_ = false;
  SetCurrentTimeInternal(CurrentTimeInternal(), kTimingUpdateOnDemand);
}

void Animation::play(ExceptionState& exception_state) {
  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand);

  double current_time = this->CurrentTimeInternal();
  if (playback_rate_ < 0 && current_time <= 0 &&
      EffectEnd() == std::numeric_limits<double>::infinity()) {
    exception_state.ThrowDOMException(
        kInvalidStateError,
        "Cannot play reversed Animation with infinite target effect end.");
    return;
  }

  if (!Playing()) {
    start_time_ = NullValue();
  }

  if (PlayStateInternal() == kIdle) {
    held_ = true;
    hold_time_ = 0;
  }

  play_state_ = kUnset;
  finished_ = false;
  UnpauseInternal();

  if (playback_rate_ > 0 && (current_time < 0 || current_time >= EffectEnd())) {
    start_time_ = NullValue();
    SetCurrentTimeInternal(0, kTimingUpdateOnDemand);
  } else if (playback_rate_ < 0 &&
             (current_time <= 0 || current_time > EffectEnd())) {
    start_time_ = NullValue();
    SetCurrentTimeInternal(EffectEnd(), kTimingUpdateOnDemand);
  }
}

void Animation::reverse(ExceptionState& exception_state) {
  if (!playback_rate_) {
    return;
  }

  SetPlaybackRateInternal(-playback_rate_);
  play(exception_state);
}

void Animation::finish(ExceptionState& exception_state) {
  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand);

  if (!playback_rate_) {
    exception_state.ThrowDOMException(
        kInvalidStateError,
        "Cannot finish Animation with a playbackRate of 0.");
    return;
  }
  if (playback_rate_ > 0 &&
      EffectEnd() == std::numeric_limits<double>::infinity()) {
    exception_state.ThrowDOMException(
        kInvalidStateError,
        "Cannot finish Animation with an infinite target effect end.");
    return;
  }

  // Avoid updating start time when already finished.
  if (CalculatePlayState() == kFinished)
    return;

  double new_current_time = playback_rate_ < 0 ? 0 : EffectEnd();
  SetCurrentTimeInternal(new_current_time, kTimingUpdateOnDemand);
  paused_ = false;
  current_time_pending_ = false;
  start_time_ = CalculateStartTime(new_current_time);
  play_state_ = kFinished;
  ForceServiceOnNextFrame();
}

ScriptPromise Animation::finished(ScriptState* script_state) {
  if (!finished_promise_) {
    finished_promise_ =
        new AnimationPromise(ExecutionContext::From(script_state), this,
                             AnimationPromise::kFinished);
    if (PlayStateInternal() == kFinished)
      finished_promise_->Resolve(this);
  }
  return finished_promise_->Promise(script_state->World());
}

ScriptPromise Animation::ready(ScriptState* script_state) {
  if (!ready_promise_) {
    ready_promise_ = new AnimationPromise(ExecutionContext::From(script_state),
                                          this, AnimationPromise::kReady);
    if (PlayStateInternal() != kPending)
      ready_promise_->Resolve(this);
  }
  return ready_promise_->Promise(script_state->World());
}

const AtomicString& Animation::InterfaceName() const {
  return EventTargetNames::AnimationPlayer;
}

ExecutionContext* Animation::GetExecutionContext() const {
  return ContextLifecycleObserver::GetExecutionContext();
}

bool Animation::HasPendingActivity() const {
  bool has_pending_promise =
      finished_promise_ &&
      finished_promise_->GetState() == ScriptPromisePropertyBase::kPending;

  return pending_finished_event_ || has_pending_promise ||
         (!finished_ && HasEventListeners(EventTypeNames::finish));
}

void Animation::ContextDestroyed(ExecutionContext*) {
  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand);

  finished_ = true;
  pending_finished_event_ = nullptr;
}

DispatchEventResult Animation::DispatchEventInternal(Event* event) {
  if (pending_finished_event_ == event)
    pending_finished_event_ = nullptr;
  return EventTargetWithInlineData::DispatchEventInternal(event);
}

double Animation::playbackRate() const {
  return playback_rate_;
}

void Animation::setPlaybackRate(double playback_rate) {
  if (playback_rate == playback_rate_)
    return;

  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand);

  SetPlaybackRateInternal(playback_rate);
}

void Animation::SetPlaybackRateInternal(double playback_rate) {
  DCHECK(std::isfinite(playback_rate));
  DCHECK_NE(playback_rate, playback_rate_);

  if (!Limited() && !Paused() && HasStartTime())
    current_time_pending_ = true;

  double stored_current_time = CurrentTimeInternal();
  if ((playback_rate_ < 0 && playback_rate >= 0) ||
      (playback_rate_ > 0 && playback_rate <= 0))
    finished_ = false;

  playback_rate_ = playback_rate;
  start_time_ = std::numeric_limits<double>::quiet_NaN();
  SetCurrentTimeInternal(stored_current_time, kTimingUpdateOnDemand);
}

void Animation::ClearOutdated() {
  if (!outdated_)
    return;
  outdated_ = false;
  if (timeline_)
    timeline_->ClearOutdatedAnimation(this);
}

void Animation::SetOutdated() {
  if (outdated_)
    return;
  outdated_ = true;
  if (timeline_)
    timeline_->SetOutdatedAnimation(this);
}

void Animation::ForceServiceOnNextFrame() {
  timeline_->Wake();
}

bool Animation::CanStartAnimationOnCompositor(
    const Optional<CompositorElementIdSet>& composited_element_ids) const {
  if (is_composited_animation_disabled_for_testing_ || EffectSuppressed())
    return false;

  // FIXME: Timeline playback rates should be compositable
  if (playback_rate_ == 0 || (std::isinf(EffectEnd()) && playback_rate_ < 0) ||
      (timeline() && timeline()->PlaybackRate() != 1))
    return false;

  if (!timeline_ || !content_ || !content_->IsKeyframeEffectReadOnly())
    return false;

  // If the optional element id set has no value we must be in SPv1 mode in
  // which case we trust the compositing logic will create a layer if needed.
  if (composited_element_ids.has_value()) {
    DCHECK(RuntimeEnabledFeatures::slimmingPaintV2Enabled());
    Element* target_element =
        ToKeyframeEffectReadOnly(content_.Get())->Target();
    if (!target_element)
      return false;

    if (target_element->GetLayoutObject() &&
        target_element->GetLayoutObject()->IsBoxModelObject() &&
        target_element->GetLayoutObject()->HasLayer()) {
      PaintLayer* paint_layer =
          ToLayoutBoxModelObject(target_element->GetLayoutObject())->Layer();
      CompositorElementId target_element_id =
          CompositorElementIdFromPaintLayerId(
              paint_layer->UniqueId(), CompositorElementIdNamespace::kPrimary);
      if (!composited_element_ids->Contains(target_element_id))
        return false;
    } else {
      return false;
    }
  }

  return Playing();
}

bool Animation::IsCandidateForAnimationOnCompositor(
    const Optional<CompositorElementIdSet>& composited_element_ids) const {
  if (!CanStartAnimationOnCompositor(composited_element_ids))
    return false;

  return ToKeyframeEffectReadOnly(content_.Get())
      ->IsCandidateForAnimationOnCompositor(playback_rate_);
}

bool Animation::MaybeStartAnimationOnCompositor(
    const Optional<CompositorElementIdSet>& composited_element_ids) {
  if (!CanStartAnimationOnCompositor(composited_element_ids))
    return false;

  bool reversed = playback_rate_ < 0;

  double start_time = timeline()->ZeroTime() + StartTimeInternal();
  if (reversed) {
    start_time -= EffectEnd() / fabs(playback_rate_);
  }

  double time_offset = 0;
  if (std::isnan(start_time)) {
    time_offset =
        reversed ? EffectEnd() - CurrentTimeInternal() : CurrentTimeInternal();
    time_offset = time_offset / fabs(playback_rate_);
  }
  DCHECK_NE(compositor_group_, 0);
  return ToKeyframeEffectReadOnly(content_.Get())
      ->MaybeStartAnimationOnCompositor(compositor_group_, start_time,
                                        time_offset, playback_rate_);
}

void Animation::SetCompositorPending(bool effect_changed) {
  // FIXME: KeyframeEffect could notify this directly?
  if (!HasActiveAnimationsOnCompositor()) {
    DestroyCompositorPlayer();
    compositor_state_.reset();
  }
  if (effect_changed && compositor_state_) {
    compositor_state_->effect_changed = true;
  }
  if (compositor_pending_ || is_paused_for_testing_) {
    return;
  }
  if (!compositor_state_ || compositor_state_->effect_changed ||
      compositor_state_->playback_rate != playback_rate_ ||
      compositor_state_->start_time != start_time_) {
    compositor_pending_ = true;
    timeline()->GetDocument()->GetCompositorPendingAnimations().Add(this);
  }
}

void Animation::CancelAnimationOnCompositor() {
  if (HasActiveAnimationsOnCompositor())
    ToKeyframeEffectReadOnly(content_.Get())->CancelAnimationOnCompositor();

  DestroyCompositorPlayer();
}

void Animation::RestartAnimationOnCompositor() {
  if (HasActiveAnimationsOnCompositor())
    ToKeyframeEffectReadOnly(content_.Get())->RestartAnimationOnCompositor();
}

void Animation::CancelIncompatibleAnimationsOnCompositor() {
  if (content_ && content_->IsKeyframeEffectReadOnly())
    ToKeyframeEffectReadOnly(content_.Get())
        ->CancelIncompatibleAnimationsOnCompositor();
}

bool Animation::HasActiveAnimationsOnCompositor() {
  if (!content_ || !content_->IsKeyframeEffectReadOnly())
    return false;

  return ToKeyframeEffectReadOnly(content_.Get())
      ->HasActiveAnimationsOnCompositor();
}

bool Animation::Update(TimingUpdateReason reason) {
  if (!timeline_)
    return false;

  PlayStateUpdateScope update_scope(*this, reason, kDoNotSetCompositorPending);

  ClearOutdated();
  bool idle = PlayStateInternal() == kIdle;

  if (content_) {
    double inherited_time = idle || IsNull(timeline_->CurrentTimeInternal())
                                ? NullValue()
                                : CurrentTimeInternal();

    // Special case for end-exclusivity when playing backwards.
    if (inherited_time == 0 && playback_rate_ < 0)
      inherited_time = -1;
    content_->UpdateInheritedTime(inherited_time, reason);
  }

  if ((idle || Limited()) && !finished_) {
    if (reason == kTimingUpdateForAnimationFrame && (idle || HasStartTime())) {
      if (idle) {
        const AtomicString& event_type = EventTypeNames::cancel;
        if (GetExecutionContext() && HasEventListeners(event_type)) {
          double event_current_time = NullValue();
          pending_cancelled_event_ = AnimationPlaybackEvent::Create(
              event_type, event_current_time, timeline()->currentTime());
          pending_cancelled_event_->SetTarget(this);
          pending_cancelled_event_->SetCurrentTarget(this);
          timeline_->GetDocument()->EnqueueAnimationFrameEvent(
              pending_cancelled_event_);
        }
      } else {
        const AtomicString& event_type = EventTypeNames::finish;
        if (GetExecutionContext() && HasEventListeners(event_type)) {
          double event_current_time = CurrentTimeInternal() * 1000;
          pending_finished_event_ = AnimationPlaybackEvent::Create(
              event_type, event_current_time, timeline()->currentTime());
          pending_finished_event_->SetTarget(this);
          pending_finished_event_->SetCurrentTarget(this);
          timeline_->GetDocument()->EnqueueAnimationFrameEvent(
              pending_finished_event_);
        }
      }
      finished_ = true;
    }
  }
  DCHECK(!outdated_);
  return !finished_ || std::isfinite(TimeToEffectChange());
}

double Animation::TimeToEffectChange() {
  DCHECK(!outdated_);
  if (!HasStartTime() || held_)
    return std::numeric_limits<double>::infinity();

  if (!content_)
    return -CurrentTimeInternal() / playback_rate_;
  double result = playback_rate_ > 0
                      ? content_->TimeToForwardsEffectChange() / playback_rate_
                      : content_->TimeToReverseEffectChange() / -playback_rate_;

  return !HasActiveAnimationsOnCompositor() &&
                 content_->GetPhase() == AnimationEffectReadOnly::kPhaseActive
             ? 0
             : result;
}

void Animation::cancel() {
  PlayStateUpdateScope update_scope(*this, kTimingUpdateOnDemand);

  if (PlayStateInternal() == kIdle)
    return;

  held_ = false;
  paused_ = false;
  play_state_ = kIdle;
  start_time_ = NullValue();
  current_time_pending_ = false;
  ForceServiceOnNextFrame();
}

void Animation::BeginUpdatingState() {
  // Nested calls are not allowed!
  DCHECK(!state_is_being_updated_);
  state_is_being_updated_ = true;
}

void Animation::EndUpdatingState() {
  DCHECK(state_is_being_updated_);
  state_is_being_updated_ = false;
}

void Animation::CreateCompositorPlayer() {
  if (Platform::Current()->IsThreadedAnimationEnabled() &&
      !compositor_player_) {
    DCHECK(Platform::Current()->CompositorSupport());
    compositor_player_ = CompositorAnimationPlayerHolder::Create(this);
    DCHECK(compositor_player_);
    AttachCompositorTimeline();
  }

  AttachCompositedLayers();
}

void Animation::DestroyCompositorPlayer() {
  DetachCompositedLayers();

  if (compositor_player_) {
    DetachCompositorTimeline();
    compositor_player_->Detach();
    compositor_player_ = nullptr;
  }
}

void Animation::AttachCompositorTimeline() {
  if (compositor_player_) {
    CompositorAnimationTimeline* timeline =
        timeline_ ? timeline_->CompositorTimeline() : nullptr;
    if (timeline)
      timeline->PlayerAttached(*this);
  }
}

void Animation::DetachCompositorTimeline() {
  if (compositor_player_) {
    CompositorAnimationTimeline* timeline =
        timeline_ ? timeline_->CompositorTimeline() : nullptr;
    if (timeline)
      timeline->PlayerDestroyed(*this);
  }
}

void Animation::AttachCompositedLayers() {
  if (!compositor_player_)
    return;

  DCHECK(content_);
  DCHECK(content_->IsKeyframeEffectReadOnly());

  ToKeyframeEffectReadOnly(content_.Get())->AttachCompositedLayers();
}

void Animation::DetachCompositedLayers() {
  if (compositor_player_ && compositor_player_->Player()->IsElementAttached())
    compositor_player_->Player()->DetachElement();
}

void Animation::NotifyAnimationStarted(double monotonic_time, int group) {
  timeline()
      ->GetDocument()
      ->GetCompositorPendingAnimations()
      .NotifyCompositorAnimationStarted(monotonic_time, group);
}

Animation::PlayStateUpdateScope::PlayStateUpdateScope(
    Animation& animation,
    TimingUpdateReason reason,
    CompositorPendingChange compositor_pending_change)
    : animation_(animation),
      initial_play_state_(animation_->PlayStateInternal()),
      compositor_pending_change_(compositor_pending_change) {
  DCHECK_NE(initial_play_state_, kUnset);
  animation_->BeginUpdatingState();
  animation_->UpdateCurrentTimingState(reason);
}

Animation::PlayStateUpdateScope::~PlayStateUpdateScope() {
  AnimationPlayState old_play_state = initial_play_state_;
  AnimationPlayState new_play_state = animation_->CalculatePlayState();

  animation_->play_state_ = new_play_state;
  if (old_play_state != new_play_state) {
    bool was_active = old_play_state == kPending || old_play_state == kRunning;
    bool is_active = new_play_state == kPending || new_play_state == kRunning;
    if (!was_active && is_active)
      TRACE_EVENT_NESTABLE_ASYNC_BEGIN1(
          "blink.animations,devtools.timeline,benchmark,rail", "Animation",
          animation_, "data", InspectorAnimationEvent::Data(*animation_));
    else if (was_active && !is_active)
      TRACE_EVENT_NESTABLE_ASYNC_END1(
          "blink.animations,devtools.timeline,benchmark,rail", "Animation",
          animation_, "endData",
          InspectorAnimationStateEvent::Data(*animation_));
    else
      TRACE_EVENT_NESTABLE_ASYNC_INSTANT1(
          "blink.animations,devtools.timeline,benchmark,rail", "Animation",
          animation_, "data", InspectorAnimationStateEvent::Data(*animation_));
  }

  // Ordering is important, the ready promise should resolve/reject before
  // the finished promise.
  if (animation_->ready_promise_ && new_play_state != old_play_state) {
    if (new_play_state == kIdle) {
      if (animation_->ready_promise_->GetState() ==
          AnimationPromise::kPending) {
        animation_->RejectAndResetPromiseMaybeAsync(
            animation_->ready_promise_.Get());
      } else {
        animation_->ready_promise_->Reset();
      }
      animation_->ResolvePromiseMaybeAsync(animation_->ready_promise_.Get());
    } else if (old_play_state == kPending) {
      animation_->ResolvePromiseMaybeAsync(animation_->ready_promise_.Get());
    } else if (new_play_state == kPending) {
      DCHECK_NE(animation_->ready_promise_->GetState(),
                AnimationPromise::kPending);
      animation_->ready_promise_->Reset();
    }
  }

  if (animation_->finished_promise_ && new_play_state != old_play_state) {
    if (new_play_state == kIdle) {
      if (animation_->finished_promise_->GetState() ==
          AnimationPromise::kPending) {
        animation_->RejectAndResetPromiseMaybeAsync(
            animation_->finished_promise_.Get());
      } else {
        animation_->finished_promise_->Reset();
      }
    } else if (new_play_state == kFinished) {
      animation_->ResolvePromiseMaybeAsync(animation_->finished_promise_.Get());
    } else if (old_play_state == kFinished) {
      animation_->finished_promise_->Reset();
    }
  }

  if (old_play_state != new_play_state &&
      (old_play_state == kIdle || new_play_state == kIdle)) {
    animation_->SetOutdated();
  }

#if DCHECK_IS_ON()
  // Verify that current time is up to date.
  animation_->CurrentTimeInternal();
#endif

  switch (compositor_pending_change_) {
    case kSetCompositorPending:
      animation_->SetCompositorPending();
      break;
    case kSetCompositorPendingWithEffectChanged:
      animation_->SetCompositorPending(true);
      break;
    case kDoNotSetCompositorPending:
      break;
    default:
      NOTREACHED();
      break;
  }
  animation_->EndUpdatingState();

  if (old_play_state != new_play_state) {
    probe::animationPlayStateChanged(animation_->timeline()->GetDocument(),
                                     animation_, old_play_state,
                                     new_play_state);
  }
}

void Animation::AddedEventListener(
    const AtomicString& event_type,
    RegisteredEventListener& registered_listener) {
  EventTargetWithInlineData::AddedEventListener(event_type,
                                                registered_listener);
  if (event_type == EventTypeNames::finish)
    UseCounter::Count(GetExecutionContext(), UseCounter::kAnimationFinishEvent);
}

void Animation::PauseForTesting(double pause_time) {
  SetCurrentTimeInternal(pause_time, kTimingUpdateOnDemand);
  if (HasActiveAnimationsOnCompositor())
    ToKeyframeEffectReadOnly(content_.Get())
        ->PauseAnimationForTestingOnCompositor(CurrentTimeInternal());
  is_paused_for_testing_ = true;
  pause();
}

void Animation::SetEffectSuppressed(bool suppressed) {
  effect_suppressed_ = suppressed;
  if (suppressed)
    CancelAnimationOnCompositor();
}

void Animation::DisableCompositedAnimationForTesting() {
  is_composited_animation_disabled_for_testing_ = true;
  CancelAnimationOnCompositor();
}

void Animation::InvalidateKeyframeEffect(const TreeScope& tree_scope) {
  if (!content_ || !content_->IsKeyframeEffectReadOnly())
    return;

  Element& target = *ToKeyframeEffectReadOnly(content_.Get())->Target();

  if (CSSAnimations::IsAffectedByKeyframesFromScope(target, tree_scope))
    target.SetNeedsStyleRecalc(kLocalStyleChange,
                               StyleChangeReasonForTracing::Create(
                                   StyleChangeReason::kStyleSheetChange));
}

void Animation::ResolvePromiseMaybeAsync(AnimationPromise* promise) {
  if (ScriptForbiddenScope::IsScriptForbidden()) {
    TaskRunnerHelper::Get(TaskType::kDOMManipulation, GetExecutionContext())
        ->PostTask(BLINK_FROM_HERE,
                   WTF::Bind(&AnimationPromise::Resolve<Animation*>,
                             WrapPersistent(promise), WrapPersistent(this)));
  } else {
    promise->Resolve(this);
  }
}

void Animation::RejectAndResetPromise(AnimationPromise* promise) {
  promise->Reject(DOMException::Create(kAbortError));
  promise->Reset();
}

void Animation::RejectAndResetPromiseMaybeAsync(AnimationPromise* promise) {
  if (ScriptForbiddenScope::IsScriptForbidden()) {
    TaskRunnerHelper::Get(TaskType::kDOMManipulation, GetExecutionContext())
        ->PostTask(BLINK_FROM_HERE,
                   WTF::Bind(&Animation::RejectAndResetPromise,
                             WrapPersistent(this), WrapPersistent(promise)));
  } else {
    RejectAndResetPromise(promise);
  }
}

DEFINE_TRACE(Animation) {
  visitor->Trace(content_);
  visitor->Trace(timeline_);
  visitor->Trace(pending_finished_event_);
  visitor->Trace(pending_cancelled_event_);
  visitor->Trace(finished_promise_);
  visitor->Trace(ready_promise_);
  visitor->Trace(compositor_player_);
  EventTargetWithInlineData::Trace(visitor);
  ContextLifecycleObserver::Trace(visitor);
}

Animation::CompositorAnimationPlayerHolder*
Animation::CompositorAnimationPlayerHolder::Create(Animation* animation) {
  return new CompositorAnimationPlayerHolder(animation);
}

Animation::CompositorAnimationPlayerHolder::CompositorAnimationPlayerHolder(
    Animation* animation)
    : animation_(animation) {
  compositor_player_ = CompositorAnimationPlayer::Create();
  compositor_player_->SetAnimationDelegate(animation_);
}

void Animation::CompositorAnimationPlayerHolder::Dispose() {
  if (!animation_)
    return;
  animation_->Dispose();
  DCHECK(!animation_);
  DCHECK(!compositor_player_);
}

void Animation::CompositorAnimationPlayerHolder::Detach() {
  DCHECK(compositor_player_);
  compositor_player_->SetAnimationDelegate(nullptr);
  animation_ = nullptr;
  compositor_player_.reset();
}
}  // namespace blink
