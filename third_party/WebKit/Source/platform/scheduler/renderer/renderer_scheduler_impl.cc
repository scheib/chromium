// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/scheduler/renderer/renderer_scheduler_impl.h"

#include "base/bind.h"
#include "base/debug/stack_trace.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram.h"
#include "base/metrics/histogram_macros.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_event_argument.h"
#include "cc/output/begin_frame_args.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/scheduler/base/real_time_domain.h"
#include "platform/scheduler/base/task_queue_impl.h"
#include "platform/scheduler/base/task_queue_selector.h"
#include "platform/scheduler/base/time_converter.h"
#include "platform/scheduler/base/trace_helper.h"
#include "platform/scheduler/base/virtual_time_domain.h"
#include "platform/scheduler/child/scheduler_tqm_delegate.h"
#include "platform/scheduler/renderer/auto_advancing_virtual_time_domain.h"
#include "platform/scheduler/renderer/task_queue_throttler.h"
#include "platform/scheduler/renderer/web_view_scheduler_impl.h"
#include "platform/scheduler/renderer/webthread_impl_for_renderer_scheduler.h"
#include "public/platform/Platform.h"

namespace blink {
namespace scheduler {
namespace {
// The run time of loading tasks is strongly bimodal.  The vast majority are
// very cheap, but there are usually a handful of very expensive tasks (e.g ~1
// second on a mobile device) so we take a very pessimistic view when estimating
// the cost of loading tasks.
const int kLoadingTaskEstimationSampleCount = 1000;
const double kLoadingTaskEstimationPercentile = 99;
const int kTimerTaskEstimationSampleCount = 1000;
const double kTimerTaskEstimationPercentile = 99;
const int kShortIdlePeriodDurationSampleCount = 10;
const double kShortIdlePeriodDurationPercentile = 50;
// Amount of idle time left in a frame (as a ratio of the vsync interval) above
// which main thread compositing can be considered fast.
const double kFastCompositingIdleTimeThreshold = .2;
constexpr base::TimeDelta kThreadLoadTrackerReportingInterval =
    base::TimeDelta::FromMinutes(1);
constexpr base::TimeDelta kThreadLoadTrackerWaitingPeriodBeforeReporting =
    base::TimeDelta::FromMinutes(2);
// We do not throttle anything while audio is played and shortly after that.
constexpr base::TimeDelta kThrottlingDelayAfterAudioIsPlayed =
    base::TimeDelta::FromSeconds(5);
constexpr base::TimeDelta kQueueingTimeWindowDuration =
    base::TimeDelta::FromSeconds(1);
// Maximal bound on task duration for reporting.
constexpr base::TimeDelta kMaxTaskDurationForReporting =
    base::TimeDelta::FromMinutes(1);

void ReportForegroundRendererTaskLoad(base::TimeTicks time, double load) {
  if (!blink::RuntimeEnabledFeatures::timerThrottlingForBackgroundTabsEnabled())
    return;

  int load_percentage = static_cast<int>(load * 100);
  DCHECK_LE(load_percentage, 100);

  UMA_HISTOGRAM_PERCENTAGE("RendererScheduler.ForegroundRendererMainThreadLoad",
                           load_percentage);
  TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
                 "RendererScheduler.ForegroundRendererLoad", load_percentage);
}

void ReportBackgroundRendererTaskLoad(base::TimeTicks time, double load) {
  if (!blink::RuntimeEnabledFeatures::timerThrottlingForBackgroundTabsEnabled())
    return;

  int load_percentage = static_cast<int>(load * 100);
  DCHECK_LE(load_percentage, 100);

  UMA_HISTOGRAM_PERCENTAGE("RendererScheduler.BackgroundRendererMainThreadLoad",
                           load_percentage);
  TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
                 "RendererScheduler.BackgroundRendererLoad", load_percentage);
}

}  // namespace

RendererSchedulerImpl::RendererSchedulerImpl(
    scoped_refptr<SchedulerTqmDelegate> main_task_runner)
    : helper_(main_task_runner),
      idle_helper_(&helper_,
                   this,
                   "RendererSchedulerIdlePeriod",
                   base::TimeDelta()),
      idle_canceled_delayed_task_sweeper_(&helper_,
                                          idle_helper_.IdleTaskRunner()),
      render_widget_scheduler_signals_(this),
      control_task_queue_(helper_.ControlTaskQueue()),
      compositor_task_queue_(
          helper_.NewTaskQueue(TaskQueue::Spec(TaskQueue::QueueType::COMPOSITOR)
                                   .SetShouldMonitorQuiescence(true))),
      compositor_task_queue_enabled_voter_(
          compositor_task_queue_->CreateQueueEnabledVoter()),
      delayed_update_policy_runner_(
          base::Bind(&RendererSchedulerImpl::UpdatePolicy,
                     base::Unretained(this)),
          helper_.ControlTaskQueue()),
      seqlock_queueing_time_estimator_(
          QueueingTimeEstimator(this, kQueueingTimeWindowDuration, 20)),
      main_thread_only_(this,
                        compositor_task_queue_,
                        helper_.scheduler_tqm_delegate().get(),
                        helper_.scheduler_tqm_delegate()->NowTicks()),
      policy_may_need_update_(&any_thread_lock_),
      weak_factory_(this) {
  task_queue_throttler_.reset(new TaskQueueThrottler(this));
  update_policy_closure_ = base::Bind(&RendererSchedulerImpl::UpdatePolicy,
                                      weak_factory_.GetWeakPtr());
  end_renderer_hidden_idle_period_closure_.Reset(base::Bind(
      &RendererSchedulerImpl::EndIdlePeriod, weak_factory_.GetWeakPtr()));

  suspend_timers_when_backgrounded_closure_.Reset(
      base::Bind(&RendererSchedulerImpl::SuspendTimerQueueWhenBackgrounded,
                 weak_factory_.GetWeakPtr()));

  default_loading_task_queue_ =
      NewLoadingTaskQueue(TaskQueue::QueueType::DEFAULT_LOADING);
  default_timer_task_queue_ =
      NewTimerTaskQueue(TaskQueue::QueueType::DEFAULT_TIMER);

  TRACE_EVENT_OBJECT_CREATED_WITH_ID(
      TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"), "RendererScheduler",
      this);

  helper_.SetObserver(this);
  helper_.AddTaskTimeObserver(this);

  // Register a tracing state observer unless we're running in a test without a
  // task runner. Note that it's safe to remove a non-existent observer.
  if (base::ThreadTaskRunnerHandle::IsSet()) {
    base::trace_event::TraceLog::GetInstance()->AddAsyncEnabledStateObserver(
        weak_factory_.GetWeakPtr());
  }
}

RendererSchedulerImpl::~RendererSchedulerImpl() {
  TRACE_EVENT_OBJECT_DELETED_WITH_ID(
      TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"), "RendererScheduler",
      this);

  for (auto& pair : loading_task_runners_) {
    pair.first->RemoveTaskObserver(
        &GetMainThreadOnly().loading_task_cost_estimator);
  }
  for (auto& pair : timer_task_runners_) {
    pair.first->RemoveTaskObserver(
        &GetMainThreadOnly().timer_task_cost_estimator);
  }

  if (virtual_time_domain_)
    UnregisterTimeDomain(virtual_time_domain_.get());

  helper_.RemoveTaskTimeObserver(this);

  base::trace_event::TraceLog::GetInstance()->RemoveAsyncEnabledStateObserver(
      this);

  // Ensure the renderer scheduler was shut down explicitly, because otherwise
  // we could end up having stale pointers to the Blink heap which has been
  // terminated by this point.
  DCHECK(GetMainThreadOnly().was_shutdown);
}

RendererSchedulerImpl::MainThreadOnly::MainThreadOnly(
    RendererSchedulerImpl* renderer_scheduler_impl,
    const scoped_refptr<TaskQueue>& compositor_task_runner,
    base::TickClock* time_source,
    base::TimeTicks now)
    : loading_task_cost_estimator(time_source,
                                  kLoadingTaskEstimationSampleCount,
                                  kLoadingTaskEstimationPercentile),
      timer_task_cost_estimator(time_source,
                                kTimerTaskEstimationSampleCount,
                                kTimerTaskEstimationPercentile),
      idle_time_estimator(compositor_task_runner,
                          time_source,
                          kShortIdlePeriodDurationSampleCount,
                          kShortIdlePeriodDurationPercentile),
      background_main_thread_load_tracker(
          now,
          base::Bind(&ReportBackgroundRendererTaskLoad),
          kThreadLoadTrackerReportingInterval,
          kThreadLoadTrackerWaitingPeriodBeforeReporting),
      foreground_main_thread_load_tracker(
          now,
          base::Bind(&ReportForegroundRendererTaskLoad),
          kThreadLoadTrackerReportingInterval,
          kThreadLoadTrackerWaitingPeriodBeforeReporting),
      current_use_case(UseCase::NONE),
      timer_queue_suspend_count(0),
      navigation_task_expected_count(0),
      expensive_task_policy(ExpensiveTaskPolicy::RUN),
      renderer_hidden(false),
      renderer_backgrounded(false),
      renderer_suspended(false),
      timer_queue_suspension_when_backgrounded_enabled(false),
      timer_queue_suspended_when_backgrounded(false),
      was_shutdown(false),
      loading_tasks_seem_expensive(false),
      timer_tasks_seem_expensive(false),
      touchstart_expected_soon(false),
      have_seen_a_begin_main_frame(false),
      have_reported_blocking_intervention_in_current_policy(false),
      have_reported_blocking_intervention_since_navigation(false),
      has_visible_render_widget_with_touch_handler(false),
      begin_frame_not_expected_soon(false),
      in_idle_period_for_testing(false),
      use_virtual_time(false),
      is_audio_playing(false),
      rail_mode_observer(nullptr),
      wake_up_budget_pool(nullptr),
      task_duration_per_queue_type_histogram(base::Histogram::FactoryGet(
          "RendererScheduler.TaskDurationPerQueueType",
          1,
          static_cast<int>(TaskQueue::QueueType::COUNT),
          static_cast<int>(TaskQueue::QueueType::COUNT) + 1,
          base::HistogramBase::kUmaTargetedHistogramFlag)) {
  foreground_main_thread_load_tracker.Resume(now);
}

RendererSchedulerImpl::MainThreadOnly::~MainThreadOnly() {}

RendererSchedulerImpl::AnyThread::AnyThread()
    : awaiting_touch_start_response(false),
      in_idle_period(false),
      begin_main_frame_on_critical_path(false),
      last_gesture_was_compositor_driven(false),
      default_gesture_prevented(true),
      have_seen_a_potentially_blocking_gesture(false),
      waiting_for_meaningful_paint(false),
      have_seen_input_since_navigation(false) {}

RendererSchedulerImpl::AnyThread::~AnyThread() {}

RendererSchedulerImpl::CompositorThreadOnly::CompositorThreadOnly()
    : last_input_type(blink::WebInputEvent::kUndefined),
      main_thread_seems_unresponsive(false) {}

RendererSchedulerImpl::CompositorThreadOnly::~CompositorThreadOnly() {}

void RendererSchedulerImpl::Shutdown() {
  base::TimeTicks now = tick_clock()->NowTicks();
  GetMainThreadOnly().background_main_thread_load_tracker.RecordIdle(now);
  GetMainThreadOnly().foreground_main_thread_load_tracker.RecordIdle(now);

  task_queue_throttler_.reset();
  helper_.Shutdown();
  idle_helper_.Shutdown();
  GetMainThreadOnly().was_shutdown = true;
  GetMainThreadOnly().rail_mode_observer = nullptr;
}

std::unique_ptr<blink::WebThread> RendererSchedulerImpl::CreateMainThread() {
  return base::MakeUnique<WebThreadImplForRendererScheduler>(this);
}

scoped_refptr<base::SingleThreadTaskRunner>
RendererSchedulerImpl::DefaultTaskRunner() {
  return helper_.DefaultTaskQueue();
}

scoped_refptr<base::SingleThreadTaskRunner>
RendererSchedulerImpl::CompositorTaskRunner() {
  helper_.CheckOnValidThread();
  return compositor_task_queue_;
}

scoped_refptr<SingleThreadIdleTaskRunner>
RendererSchedulerImpl::IdleTaskRunner() {
  return idle_helper_.IdleTaskRunner();
}

scoped_refptr<base::SingleThreadTaskRunner>
RendererSchedulerImpl::LoadingTaskRunner() {
  helper_.CheckOnValidThread();
  return default_loading_task_queue_;
}

scoped_refptr<base::SingleThreadTaskRunner>
RendererSchedulerImpl::TimerTaskRunner() {
  helper_.CheckOnValidThread();
  return default_timer_task_queue_;
}

scoped_refptr<TaskQueue> RendererSchedulerImpl::DefaultTaskQueue() {
  return helper_.DefaultTaskQueue();
}

scoped_refptr<TaskQueue> RendererSchedulerImpl::CompositorTaskQueue() {
  helper_.CheckOnValidThread();
  return compositor_task_queue_;
}

scoped_refptr<TaskQueue> RendererSchedulerImpl::LoadingTaskQueue() {
  helper_.CheckOnValidThread();
  return default_loading_task_queue_;
}

scoped_refptr<TaskQueue> RendererSchedulerImpl::TimerTaskQueue() {
  helper_.CheckOnValidThread();
  return default_timer_task_queue_;
}

scoped_refptr<TaskQueue> RendererSchedulerImpl::ControlTaskQueue() {
  helper_.CheckOnValidThread();
  return helper_.ControlTaskQueue();
}

scoped_refptr<TaskQueue> RendererSchedulerImpl::VirtualTimeControlTaskQueue() {
  helper_.CheckOnValidThread();
  return virtual_time_control_task_queue_;
}

scoped_refptr<TaskQueue> RendererSchedulerImpl::NewLoadingTaskQueue(
    TaskQueue::QueueType queue_type) {
  helper_.CheckOnValidThread();
  scoped_refptr<TaskQueue> loading_task_queue(helper_.NewTaskQueue(
      TaskQueue::Spec(queue_type)
          .SetShouldMonitorQuiescence(true)
          .SetTimeDomain(GetMainThreadOnly().use_virtual_time
                             ? GetVirtualTimeDomain()
                             : nullptr)));
  auto insert_result = loading_task_runners_.insert(std::make_pair(
      loading_task_queue, loading_task_queue->CreateQueueEnabledVoter()));
  insert_result.first->second->SetQueueEnabled(
      GetMainThreadOnly().current_policy.loading_queue_policy.is_enabled);
  loading_task_queue->SetQueuePriority(
      GetMainThreadOnly().current_policy.loading_queue_policy.priority);
  if (GetMainThreadOnly()
          .current_policy.loading_queue_policy.time_domain_type ==
      TimeDomainType::THROTTLED) {
    task_queue_throttler_->IncreaseThrottleRefCount(loading_task_queue.get());
  }
  loading_task_queue->AddTaskObserver(
      &GetMainThreadOnly().loading_task_cost_estimator);
  AddQueueToWakeUpBudgetPool(loading_task_queue.get());
  return loading_task_queue;
}

scoped_refptr<TaskQueue> RendererSchedulerImpl::NewTimerTaskQueue(
    TaskQueue::QueueType queue_type) {
  helper_.CheckOnValidThread();
  // TODO(alexclarke): Consider using ApplyTaskQueuePolicy() for brevity.
  scoped_refptr<TaskQueue> timer_task_queue(helper_.NewTaskQueue(
      TaskQueue::Spec(queue_type)
          .SetShouldMonitorQuiescence(true)
          .SetShouldReportWhenExecutionBlocked(true)
          .SetTimeDomain(GetMainThreadOnly().use_virtual_time
                             ? GetVirtualTimeDomain()
                             : nullptr)));
  auto insert_result = timer_task_runners_.insert(std::make_pair(
      timer_task_queue, timer_task_queue->CreateQueueEnabledVoter()));
  insert_result.first->second->SetQueueEnabled(
      GetMainThreadOnly().current_policy.timer_queue_policy.is_enabled);
  timer_task_queue->SetQueuePriority(
      GetMainThreadOnly().current_policy.timer_queue_policy.priority);
  if (GetMainThreadOnly().current_policy.timer_queue_policy.time_domain_type ==
      TimeDomainType::THROTTLED) {
    task_queue_throttler_->IncreaseThrottleRefCount(timer_task_queue.get());
  }
  timer_task_queue->AddTaskObserver(
      &GetMainThreadOnly().timer_task_cost_estimator);
  AddQueueToWakeUpBudgetPool(timer_task_queue.get());
  return timer_task_queue;
}

scoped_refptr<TaskQueue> RendererSchedulerImpl::NewUnthrottledTaskQueue(
    TaskQueue::QueueType queue_type) {
  helper_.CheckOnValidThread();
  scoped_refptr<TaskQueue> unthrottled_task_queue(helper_.NewTaskQueue(
      TaskQueue::Spec(queue_type)
          .SetShouldMonitorQuiescence(true)
          .SetTimeDomain(GetMainThreadOnly().use_virtual_time
                             ? GetVirtualTimeDomain()
                             : nullptr)));
  unthrottled_task_runners_.insert(unthrottled_task_queue);
  return unthrottled_task_queue;
}

std::unique_ptr<RenderWidgetSchedulingState>
RendererSchedulerImpl::NewRenderWidgetSchedulingState() {
  return render_widget_scheduler_signals_.NewRenderWidgetSchedulingState();
}

void RendererSchedulerImpl::OnUnregisterTaskQueue(
    const scoped_refptr<TaskQueue>& task_queue) {
  if (task_queue_throttler_)
    task_queue_throttler_->UnregisterTaskQueue(task_queue.get());

  if (loading_task_runners_.find(task_queue) != loading_task_runners_.end()) {
    task_queue->RemoveTaskObserver(
        &GetMainThreadOnly().loading_task_cost_estimator);
    loading_task_runners_.erase(task_queue);
  } else if (timer_task_runners_.find(task_queue) !=
             timer_task_runners_.end()) {
    task_queue->RemoveTaskObserver(
        &GetMainThreadOnly().timer_task_cost_estimator);
    timer_task_runners_.erase(task_queue);
  } else if (unthrottled_task_runners_.find(task_queue) !=
             unthrottled_task_runners_.end()) {
    unthrottled_task_runners_.erase(task_queue);
  }
}

bool RendererSchedulerImpl::CanExceedIdleDeadlineIfRequired() const {
  return idle_helper_.CanExceedIdleDeadlineIfRequired();
}

void RendererSchedulerImpl::AddTaskObserver(
    base::MessageLoop::TaskObserver* task_observer) {
  helper_.AddTaskObserver(task_observer);
}

void RendererSchedulerImpl::RemoveTaskObserver(
    base::MessageLoop::TaskObserver* task_observer) {
  helper_.RemoveTaskObserver(task_observer);
}

void RendererSchedulerImpl::WillBeginFrame(const cc::BeginFrameArgs& args) {
  TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::WillBeginFrame", "args", args.AsValue());
  helper_.CheckOnValidThread();
  if (helper_.IsShutdown())
    return;

  EndIdlePeriod();
  GetMainThreadOnly().estimated_next_frame_begin =
      args.frame_time + args.interval;
  GetMainThreadOnly().have_seen_a_begin_main_frame = true;
  GetMainThreadOnly().begin_frame_not_expected_soon = false;
  GetMainThreadOnly().compositor_frame_interval = args.interval;
  {
    base::AutoLock lock(any_thread_lock_);
    GetAnyThread().begin_main_frame_on_critical_path = args.on_critical_path;
  }
}

void RendererSchedulerImpl::DidCommitFrameToCompositor() {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::DidCommitFrameToCompositor");
  helper_.CheckOnValidThread();
  if (helper_.IsShutdown())
    return;

  base::TimeTicks now(helper_.scheduler_tqm_delegate()->NowTicks());
  if (now < GetMainThreadOnly().estimated_next_frame_begin) {
    // TODO(rmcilroy): Consider reducing the idle period based on the runtime of
    // the next pending delayed tasks (as currently done in for long idle times)
    idle_helper_.StartIdlePeriod(
        IdleHelper::IdlePeriodState::IN_SHORT_IDLE_PERIOD, now,
        GetMainThreadOnly().estimated_next_frame_begin);
  }

  GetMainThreadOnly().idle_time_estimator.DidCommitFrameToCompositor();
}

void RendererSchedulerImpl::BeginFrameNotExpectedSoon() {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::BeginFrameNotExpectedSoon");
  helper_.CheckOnValidThread();
  if (helper_.IsShutdown())
    return;

  GetMainThreadOnly().begin_frame_not_expected_soon = true;
  idle_helper_.EnableLongIdlePeriod();
  {
    base::AutoLock lock(any_thread_lock_);
    GetAnyThread().begin_main_frame_on_critical_path = false;
  }
}

void RendererSchedulerImpl::BeginMainFrameNotExpectedUntil(
    base::TimeTicks time) {
  helper_.CheckOnValidThread();
  if (helper_.IsShutdown())
    return;

  base::TimeTicks now(helper_.scheduler_tqm_delegate()->NowTicks());
  TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::BeginMainFrameNotExpectedUntil",
               "time_remaining", (time - now).InMillisecondsF());

  if (now < time) {
    // End any previous idle period.
    EndIdlePeriod();

    // TODO(rmcilroy): Consider reducing the idle period based on the runtime of
    // the next pending delayed tasks (as currently done in for long idle times)
    idle_helper_.StartIdlePeriod(
        IdleHelper::IdlePeriodState::IN_SHORT_IDLE_PERIOD, now, time);
  }
}

void RendererSchedulerImpl::SetAllRenderWidgetsHidden(bool hidden) {
  TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::SetAllRenderWidgetsHidden", "hidden",
               hidden);

  helper_.CheckOnValidThread();

  if (helper_.IsShutdown() || GetMainThreadOnly().renderer_hidden == hidden)
    return;

  end_renderer_hidden_idle_period_closure_.Cancel();

  if (hidden) {
    idle_helper_.EnableLongIdlePeriod();

    // Ensure that we stop running idle tasks after a few seconds of being
    // hidden.
    base::TimeDelta end_idle_when_hidden_delay =
        base::TimeDelta::FromMilliseconds(kEndIdleWhenHiddenDelayMillis);
    control_task_queue_->PostDelayedTask(
        FROM_HERE, end_renderer_hidden_idle_period_closure_.GetCallback(),
        end_idle_when_hidden_delay);
    GetMainThreadOnly().renderer_hidden = true;
  } else {
    GetMainThreadOnly().renderer_hidden = false;
    EndIdlePeriod();
  }

  // TODO(alexclarke): Should we update policy here?
  CreateTraceEventObjectSnapshot();
}

void RendererSchedulerImpl::SetHasVisibleRenderWidgetWithTouchHandler(
    bool has_visible_render_widget_with_touch_handler) {
  helper_.CheckOnValidThread();
  if (has_visible_render_widget_with_touch_handler ==
      GetMainThreadOnly().has_visible_render_widget_with_touch_handler)
    return;

  GetMainThreadOnly().has_visible_render_widget_with_touch_handler =
      has_visible_render_widget_with_touch_handler;

  base::AutoLock lock(any_thread_lock_);
  UpdatePolicyLocked(UpdateType::FORCE_UPDATE);
}

void RendererSchedulerImpl::OnRendererBackgrounded() {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::OnRendererBackgrounded");
  helper_.CheckOnValidThread();
  if (helper_.IsShutdown() || GetMainThreadOnly().renderer_backgrounded)
    return;

  GetMainThreadOnly().renderer_backgrounded = true;

  UpdatePolicy();

  base::TimeTicks now = tick_clock()->NowTicks();
  GetMainThreadOnly().foreground_main_thread_load_tracker.Pause(now);
  GetMainThreadOnly().background_main_thread_load_tracker.Resume(now);

  if (!GetMainThreadOnly().timer_queue_suspension_when_backgrounded_enabled)
    return;

  suspend_timers_when_backgrounded_closure_.Cancel();
  base::TimeDelta suspend_timers_when_backgrounded_delay =
      base::TimeDelta::FromMilliseconds(
          kSuspendTimersWhenBackgroundedDelayMillis);
  control_task_queue_->PostDelayedTask(
      FROM_HERE, suspend_timers_when_backgrounded_closure_.GetCallback(),
      suspend_timers_when_backgrounded_delay);
}

void RendererSchedulerImpl::OnRendererForegrounded() {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::OnRendererForegrounded");
  helper_.CheckOnValidThread();
  if (helper_.IsShutdown() || !GetMainThreadOnly().renderer_backgrounded)
    return;

  GetMainThreadOnly().renderer_backgrounded = false;
  GetMainThreadOnly().renderer_suspended = false;

  UpdatePolicy();

  base::TimeTicks now = tick_clock()->NowTicks();
  GetMainThreadOnly().foreground_main_thread_load_tracker.Resume(now);
  GetMainThreadOnly().background_main_thread_load_tracker.Pause(now);

  suspend_timers_when_backgrounded_closure_.Cancel();
  ResumeTimerQueueWhenForegroundedOrResumed();
}

void RendererSchedulerImpl::OnAudioStateChanged() {
  bool is_audio_playing = false;
  for (WebViewSchedulerImpl* web_view_scheduler :
       GetMainThreadOnly().web_view_schedulers) {
    is_audio_playing = is_audio_playing || web_view_scheduler->IsAudioPlaying();
  }

  if (is_audio_playing == GetMainThreadOnly().is_audio_playing)
    return;

  GetMainThreadOnly().last_audio_state_change =
      helper_.scheduler_tqm_delegate()->NowTicks();
  GetMainThreadOnly().is_audio_playing = is_audio_playing;

  UpdatePolicy();
}

void RendererSchedulerImpl::SuspendRenderer() {
  helper_.CheckOnValidThread();
  if (helper_.IsShutdown())
    return;
  if (!GetMainThreadOnly().renderer_backgrounded)
    return;
  suspend_timers_when_backgrounded_closure_.Cancel();

  UMA_HISTOGRAM_COUNTS("PurgeAndSuspend.PendingTaskCount",
                       helper_.GetNumberOfPendingTasks());

  // TODO(hajimehoshi): We might need to suspend not only timer queue but also
  // e.g. loading tasks or postMessage.
  GetMainThreadOnly().renderer_suspended = true;
  SuspendTimerQueueWhenBackgrounded();
}

void RendererSchedulerImpl::ResumeRenderer() {
  helper_.CheckOnValidThread();
  if (helper_.IsShutdown())
    return;
  if (!GetMainThreadOnly().renderer_backgrounded)
    return;
  suspend_timers_when_backgrounded_closure_.Cancel();
  GetMainThreadOnly().renderer_suspended = false;
  ResumeTimerQueueWhenForegroundedOrResumed();
}

void RendererSchedulerImpl::EndIdlePeriod() {
  if (GetMainThreadOnly().in_idle_period_for_testing)
    return;
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::EndIdlePeriod");
  helper_.CheckOnValidThread();
  idle_helper_.EndIdlePeriod();
}

void RendererSchedulerImpl::EndIdlePeriodForTesting(
    const base::Closure& callback,
    base::TimeTicks time_remaining) {
  GetMainThreadOnly().in_idle_period_for_testing = false;
  EndIdlePeriod();
  callback.Run();
}

bool RendererSchedulerImpl::PolicyNeedsUpdateForTesting() {
  return policy_may_need_update_.IsSet();
}

// static
bool RendererSchedulerImpl::ShouldPrioritizeInputEvent(
    const blink::WebInputEvent& web_input_event) {
  // We regard MouseMove events with the left mouse button down as a signal
  // that the user is doing something requiring a smooth frame rate.
  if ((web_input_event.GetType() == blink::WebInputEvent::kMouseDown ||
       web_input_event.GetType() == blink::WebInputEvent::kMouseMove) &&
      (web_input_event.GetModifiers() &
       blink::WebInputEvent::kLeftButtonDown)) {
    return true;
  }
  // Ignore all other mouse events because they probably don't signal user
  // interaction needing a smooth framerate. NOTE isMouseEventType returns false
  // for mouse wheel events, hence we regard them as user input.
  // Ignore keyboard events because it doesn't really make sense to enter
  // compositor priority for them.
  if (blink::WebInputEvent::IsMouseEventType(web_input_event.GetType()) ||
      blink::WebInputEvent::IsKeyboardEventType(web_input_event.GetType())) {
    return false;
  }
  return true;
}

void RendererSchedulerImpl::DidHandleInputEventOnCompositorThread(
    const blink::WebInputEvent& web_input_event,
    InputEventState event_state) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::DidHandleInputEventOnCompositorThread");
  if (!ShouldPrioritizeInputEvent(web_input_event))
    return;

  UpdateForInputEventOnCompositorThread(web_input_event.GetType(), event_state);
}

void RendererSchedulerImpl::DidAnimateForInputOnCompositorThread() {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::DidAnimateForInputOnCompositorThread");
  base::AutoLock lock(any_thread_lock_);
  GetAnyThread().fling_compositor_escalation_deadline =
      helper_.scheduler_tqm_delegate()->NowTicks() +
      base::TimeDelta::FromMilliseconds(kFlingEscalationLimitMillis);
}

void RendererSchedulerImpl::UpdateForInputEventOnCompositorThread(
    blink::WebInputEvent::Type type,
    InputEventState input_event_state) {
  base::AutoLock lock(any_thread_lock_);
  base::TimeTicks now = helper_.scheduler_tqm_delegate()->NowTicks();

  // TODO(alexclarke): Move WebInputEventTraits where we can access it from here
  // and record the name rather than the integer representation.
  TRACE_EVENT2(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::UpdateForInputEventOnCompositorThread",
               "type", static_cast<int>(type), "input_event_state",
               InputEventStateToString(input_event_state));

  base::TimeDelta unused_policy_duration;
  UseCase previous_use_case =
      ComputeCurrentUseCase(now, &unused_policy_duration);
  bool was_awaiting_touch_start_response =
      GetAnyThread().awaiting_touch_start_response;

  GetAnyThread().user_model.DidStartProcessingInputEvent(type, now);
  GetAnyThread().have_seen_input_since_navigation = true;

  if (input_event_state == InputEventState::EVENT_CONSUMED_BY_COMPOSITOR)
    GetAnyThread().user_model.DidFinishProcessingInputEvent(now);

  switch (type) {
    case blink::WebInputEvent::kTouchStart:
      GetAnyThread().awaiting_touch_start_response = true;
      // This is just a fail-safe to reset the state of
      // |last_gesture_was_compositor_driven| to the default. We don't know
      // yet where the gesture will run.
      GetAnyThread().last_gesture_was_compositor_driven = false;
      GetAnyThread().have_seen_a_potentially_blocking_gesture = true;
      // Assume the default gesture is prevented until we see evidence
      // otherwise.
      GetAnyThread().default_gesture_prevented = true;
      break;

    case blink::WebInputEvent::kTouchMove:
      // Observation of consecutive touchmoves is a strong signal that the
      // page is consuming the touch sequence, in which case touchstart
      // response prioritization is no longer necessary. Otherwise, the
      // initial touchmove should preserve the touchstart response pending
      // state.
      if (GetAnyThread().awaiting_touch_start_response &&
          GetCompositorThreadOnly().last_input_type ==
              blink::WebInputEvent::kTouchMove) {
        GetAnyThread().awaiting_touch_start_response = false;
      }
      break;

    case blink::WebInputEvent::kGesturePinchUpdate:
    case blink::WebInputEvent::kGestureScrollUpdate:
      // If we see events for an established gesture, we can lock it to the
      // appropriate thread as the gesture can no longer be cancelled.
      GetAnyThread().last_gesture_was_compositor_driven =
          input_event_state == InputEventState::EVENT_CONSUMED_BY_COMPOSITOR;
      GetAnyThread().awaiting_touch_start_response = false;
      GetAnyThread().default_gesture_prevented = false;
      break;

    case blink::WebInputEvent::kGestureFlingCancel:
      GetAnyThread().fling_compositor_escalation_deadline = base::TimeTicks();
      break;

    case blink::WebInputEvent::kGestureTapDown:
    case blink::WebInputEvent::kGestureShowPress:
    case blink::WebInputEvent::kGestureScrollEnd:
      // With no observable effect, these meta events do not indicate a
      // meaningful touchstart response and should not impact task priority.
      break;

    case blink::WebInputEvent::kMouseDown:
      // Reset tracking state at the start of a new mouse drag gesture.
      GetAnyThread().last_gesture_was_compositor_driven = false;
      GetAnyThread().default_gesture_prevented = true;
      break;

    case blink::WebInputEvent::kMouseMove:
      // Consider mouse movement with the left button held down (see
      // ShouldPrioritizeInputEvent) similarly to a touch gesture.
      GetAnyThread().last_gesture_was_compositor_driven =
          input_event_state == InputEventState::EVENT_CONSUMED_BY_COMPOSITOR;
      GetAnyThread().awaiting_touch_start_response = false;
      break;

    case blink::WebInputEvent::kMouseWheel:
      GetAnyThread().last_gesture_was_compositor_driven =
          input_event_state == InputEventState::EVENT_CONSUMED_BY_COMPOSITOR;
      GetAnyThread().awaiting_touch_start_response = false;
      GetAnyThread().have_seen_a_potentially_blocking_gesture = true;
      // If the event was sent to the main thread, assume the default gesture is
      // prevented until we see evidence otherwise.
      GetAnyThread().default_gesture_prevented =
          !GetAnyThread().last_gesture_was_compositor_driven;
      break;

    case blink::WebInputEvent::kUndefined:
      break;

    default:
      GetAnyThread().awaiting_touch_start_response = false;
      break;
  }

  // Avoid unnecessary policy updates if the use case did not change.
  UseCase use_case = ComputeCurrentUseCase(now, &unused_policy_duration);

  if (use_case != previous_use_case ||
      was_awaiting_touch_start_response !=
          GetAnyThread().awaiting_touch_start_response) {
    EnsureUrgentPolicyUpdatePostedOnMainThread(FROM_HERE);
  }
  GetCompositorThreadOnly().last_input_type = type;
}

void RendererSchedulerImpl::DidHandleInputEventOnMainThread(
    const WebInputEvent& web_input_event,
    WebInputEventResult result) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::DidHandleInputEventOnMainThread");
  helper_.CheckOnValidThread();
  if (ShouldPrioritizeInputEvent(web_input_event)) {
    base::AutoLock lock(any_thread_lock_);
    GetAnyThread().user_model.DidFinishProcessingInputEvent(
        helper_.scheduler_tqm_delegate()->NowTicks());

    // If we were waiting for a touchstart response and the main thread has
    // prevented the default gesture, consider the gesture established. This
    // ensures single-event gestures such as button presses are promptly
    // detected.
    if (GetAnyThread().awaiting_touch_start_response &&
        result == WebInputEventResult::kHandledApplication) {
      GetAnyThread().awaiting_touch_start_response = false;
      GetAnyThread().default_gesture_prevented = true;
      UpdatePolicyLocked(UpdateType::MAY_EARLY_OUT_IF_POLICY_UNCHANGED);
    }
  }
}

bool RendererSchedulerImpl::IsHighPriorityWorkAnticipated() {
  helper_.CheckOnValidThread();
  if (helper_.IsShutdown())
    return false;

  MaybeUpdatePolicy();
  // The touchstart, synchronized gesture and main-thread gesture use cases
  // indicate a strong likelihood of high-priority work in the near future.
  UseCase use_case = GetMainThreadOnly().current_use_case;
  return GetMainThreadOnly().touchstart_expected_soon ||
         use_case == UseCase::TOUCHSTART ||
         use_case == UseCase::MAIN_THREAD_GESTURE ||
         use_case == UseCase::MAIN_THREAD_CUSTOM_INPUT_HANDLING ||
         use_case == UseCase::SYNCHRONIZED_GESTURE;
}

bool RendererSchedulerImpl::ShouldYieldForHighPriorityWork() {
  helper_.CheckOnValidThread();
  if (helper_.IsShutdown())
    return false;

  MaybeUpdatePolicy();
  // We only yield if there's a urgent task to be run now, or we are expecting
  // one soon (touch start).
  // Note: even though the control queue has the highest priority we don't yield
  // for it since these tasks are not user-provided work and they are only
  // intended to run before the next task, not interrupt the tasks.
  switch (GetMainThreadOnly().current_use_case) {
    case UseCase::COMPOSITOR_GESTURE:
    case UseCase::NONE:
      return GetMainThreadOnly().touchstart_expected_soon;

    case UseCase::MAIN_THREAD_GESTURE:
    case UseCase::MAIN_THREAD_CUSTOM_INPUT_HANDLING:
    case UseCase::SYNCHRONIZED_GESTURE:
      return compositor_task_queue_->HasTaskToRunImmediately() ||
             GetMainThreadOnly().touchstart_expected_soon;

    case UseCase::TOUCHSTART:
      return true;

    case UseCase::LOADING:
      return false;

    default:
      NOTREACHED();
      return false;
  }
}

base::TimeTicks RendererSchedulerImpl::CurrentIdleTaskDeadlineForTesting()
    const {
  return idle_helper_.CurrentIdleTaskDeadline();
}

void RendererSchedulerImpl::RunIdleTasksForTesting(
    const base::Closure& callback) {
  GetMainThreadOnly().in_idle_period_for_testing = true;
  IdleTaskRunner()->PostIdleTask(
      FROM_HERE, base::Bind(&RendererSchedulerImpl::EndIdlePeriodForTesting,
                            weak_factory_.GetWeakPtr(), callback));
  idle_helper_.EnableLongIdlePeriod();
}

void RendererSchedulerImpl::MaybeUpdatePolicy() {
  helper_.CheckOnValidThread();
  if (policy_may_need_update_.IsSet()) {
    UpdatePolicy();
  }
}

void RendererSchedulerImpl::EnsureUrgentPolicyUpdatePostedOnMainThread(
    const tracked_objects::Location& from_here) {
  // TODO(scheduler-dev): Check that this method isn't called from the main
  // thread.
  any_thread_lock_.AssertAcquired();
  if (!policy_may_need_update_.IsSet()) {
    policy_may_need_update_.SetWhileLocked(true);
    control_task_queue_->PostTask(from_here, update_policy_closure_);
  }
}

void RendererSchedulerImpl::UpdatePolicy() {
  base::AutoLock lock(any_thread_lock_);
  UpdatePolicyLocked(UpdateType::MAY_EARLY_OUT_IF_POLICY_UNCHANGED);
}

void RendererSchedulerImpl::ForceUpdatePolicy() {
  base::AutoLock lock(any_thread_lock_);
  UpdatePolicyLocked(UpdateType::FORCE_UPDATE);
}

void RendererSchedulerImpl::UpdatePolicyLocked(UpdateType update_type) {
  helper_.CheckOnValidThread();
  any_thread_lock_.AssertAcquired();
  if (helper_.IsShutdown())
    return;

  base::TimeTicks now = helper_.scheduler_tqm_delegate()->NowTicks();
  policy_may_need_update_.SetWhileLocked(false);

  base::TimeDelta expected_use_case_duration;
  UseCase use_case = ComputeCurrentUseCase(now, &expected_use_case_duration);
  GetMainThreadOnly().current_use_case = use_case;

  base::TimeDelta touchstart_expected_flag_valid_for_duration;
  // TODO(skyostil): Consider handlers for all types of blocking gestures (e.g.,
  // mouse wheel) instead of just touchstart.
  bool touchstart_expected_soon = false;
  if (GetMainThreadOnly().has_visible_render_widget_with_touch_handler) {
    touchstart_expected_soon = GetAnyThread().user_model.IsGestureExpectedSoon(
        now, &touchstart_expected_flag_valid_for_duration);
  }
  GetMainThreadOnly().touchstart_expected_soon = touchstart_expected_soon;

  base::TimeDelta longest_jank_free_task_duration =
      EstimateLongestJankFreeTaskDuration();
  GetMainThreadOnly().longest_jank_free_task_duration =
      longest_jank_free_task_duration;

  bool loading_tasks_seem_expensive = false;
  bool timer_tasks_seem_expensive = false;
  loading_tasks_seem_expensive =
      GetMainThreadOnly().loading_task_cost_estimator.expected_task_duration() >
      longest_jank_free_task_duration;
  timer_tasks_seem_expensive =
      GetMainThreadOnly().timer_task_cost_estimator.expected_task_duration() >
      longest_jank_free_task_duration;
  GetMainThreadOnly().timer_tasks_seem_expensive = timer_tasks_seem_expensive;
  GetMainThreadOnly().loading_tasks_seem_expensive =
      loading_tasks_seem_expensive;

  // The |new_policy_duration| is the minimum of |expected_use_case_duration|
  // and |touchstart_expected_flag_valid_for_duration| unless one is zero in
  // which case we choose the other.
  base::TimeDelta new_policy_duration = expected_use_case_duration;
  if (new_policy_duration.is_zero() ||
      (touchstart_expected_flag_valid_for_duration > base::TimeDelta() &&
       new_policy_duration > touchstart_expected_flag_valid_for_duration)) {
    new_policy_duration = touchstart_expected_flag_valid_for_duration;
  }

  // Do not throttle while audio is playing or for a short period after that
  // to make sure that pages playing short audio clips powered by timers
  // work.
  if (GetMainThreadOnly().last_audio_state_change &&
      !GetMainThreadOnly().is_audio_playing) {
    base::TimeTicks audio_will_expire =
        GetMainThreadOnly().last_audio_state_change.value() +
        kThrottlingDelayAfterAudioIsPlayed;

    base::TimeDelta audio_will_expire_after = audio_will_expire - now;

    if (audio_will_expire_after > base::TimeDelta()) {
      if (new_policy_duration.is_zero()) {
        new_policy_duration = audio_will_expire_after;
      } else {
        new_policy_duration =
            std::min(new_policy_duration, audio_will_expire_after);
      }
    }
  }

  if (new_policy_duration > base::TimeDelta()) {
    GetMainThreadOnly().current_policy_expiration_time =
        now + new_policy_duration;
    delayed_update_policy_runner_.SetDeadline(FROM_HERE, new_policy_duration,
                                              now);
  } else {
    GetMainThreadOnly().current_policy_expiration_time = base::TimeTicks();
  }

  // Avoid prioritizing main thread compositing (e.g., rAF) if it is extremely
  // slow, because that can cause starvation in other task sources.
  bool main_thread_compositing_is_fast =
      GetMainThreadOnly().idle_time_estimator.GetExpectedIdleDuration(
          GetMainThreadOnly().compositor_frame_interval) >
      GetMainThreadOnly().compositor_frame_interval *
          kFastCompositingIdleTimeThreshold;

  Policy new_policy;
  ExpensiveTaskPolicy expensive_task_policy = ExpensiveTaskPolicy::RUN;
  new_policy.rail_mode = v8::PERFORMANCE_ANIMATION;

  switch (use_case) {
    case UseCase::COMPOSITOR_GESTURE:
      if (touchstart_expected_soon) {
        new_policy.rail_mode = v8::PERFORMANCE_RESPONSE;
        expensive_task_policy = ExpensiveTaskPolicy::BLOCK;
        new_policy.compositor_queue_policy.priority = TaskQueue::HIGH_PRIORITY;
      } else {
        // What we really want to do is priorize loading tasks, but that doesn't
        // seem to be safe. Instead we do that by proxy by deprioritizing
        // compositor tasks. This should be safe since we've already gone to the
        // pain of fixing ordering issues with them.
        new_policy.compositor_queue_policy.priority =
            TaskQueue::BEST_EFFORT_PRIORITY;
      }
      break;

    case UseCase::SYNCHRONIZED_GESTURE:
      new_policy.compositor_queue_policy.priority =
          main_thread_compositing_is_fast ? TaskQueue::HIGH_PRIORITY
                                          : TaskQueue::NORMAL_PRIORITY;
      if (touchstart_expected_soon) {
        new_policy.rail_mode = v8::PERFORMANCE_RESPONSE;
        expensive_task_policy = ExpensiveTaskPolicy::BLOCK;
      } else {
        expensive_task_policy = ExpensiveTaskPolicy::THROTTLE;
      }
      break;

    case UseCase::MAIN_THREAD_CUSTOM_INPUT_HANDLING:
      // In main thread input handling scenarios we don't have perfect knowledge
      // about which things we should be prioritizing, so we don't attempt to
      // block expensive tasks because we don't know whether they were integral
      // to the page's functionality or not.
      new_policy.compositor_queue_policy.priority =
          main_thread_compositing_is_fast ? TaskQueue::HIGH_PRIORITY
                                          : TaskQueue::NORMAL_PRIORITY;
      break;

    case UseCase::MAIN_THREAD_GESTURE:
      // A main thread gesture is for example a scroll gesture which is handled
      // by the main thread. Since we know the established gesture type, we can
      // be a little more aggressive about prioritizing compositing and input
      // handling over other tasks.
      new_policy.compositor_queue_policy.priority = TaskQueue::HIGH_PRIORITY;
      if (touchstart_expected_soon) {
        new_policy.rail_mode = v8::PERFORMANCE_RESPONSE;
        expensive_task_policy = ExpensiveTaskPolicy::BLOCK;
      } else {
        expensive_task_policy = ExpensiveTaskPolicy::THROTTLE;
      }
      break;

    case UseCase::TOUCHSTART:
      new_policy.rail_mode = v8::PERFORMANCE_RESPONSE;
      new_policy.compositor_queue_policy.priority = TaskQueue::HIGH_PRIORITY;
      new_policy.loading_queue_policy.is_enabled = false;
      new_policy.timer_queue_policy.is_enabled = false;
      // NOTE this is a nop due to the above.
      expensive_task_policy = ExpensiveTaskPolicy::BLOCK;
      break;

    case UseCase::NONE:
      // It's only safe to block tasks that if we are expecting a compositor
      // driven gesture.
      if (touchstart_expected_soon &&
          GetAnyThread().last_gesture_was_compositor_driven) {
        new_policy.rail_mode = v8::PERFORMANCE_RESPONSE;
        expensive_task_policy = ExpensiveTaskPolicy::BLOCK;
      }
      break;

    case UseCase::LOADING:
      new_policy.rail_mode = v8::PERFORMANCE_LOAD;
      // TODO(skyostil): Experiment with increasing loading and default queue
      // priorities and throttling rendering frame rate.
      break;

    default:
      NOTREACHED();
  }

  // TODO(skyostil): Add an idle state for foreground tabs too.
  if (GetMainThreadOnly().renderer_hidden)
    new_policy.rail_mode = v8::PERFORMANCE_IDLE;

  if (expensive_task_policy == ExpensiveTaskPolicy::BLOCK &&
      (!GetMainThreadOnly().have_seen_a_begin_main_frame ||
       GetMainThreadOnly().navigation_task_expected_count > 0)) {
    expensive_task_policy = ExpensiveTaskPolicy::RUN;
  }

  switch (expensive_task_policy) {
    case ExpensiveTaskPolicy::RUN:
      break;

    case ExpensiveTaskPolicy::BLOCK:
      if (loading_tasks_seem_expensive)
        new_policy.loading_queue_policy.is_enabled = false;
      if (timer_tasks_seem_expensive)
        new_policy.timer_queue_policy.is_enabled = false;
      break;

    case ExpensiveTaskPolicy::THROTTLE:
      if (loading_tasks_seem_expensive) {
        new_policy.loading_queue_policy.time_domain_type =
            TimeDomainType::THROTTLED;
      }
      if (timer_tasks_seem_expensive) {
        new_policy.timer_queue_policy.time_domain_type =
            TimeDomainType::THROTTLED;
      }
      break;
  }
  GetMainThreadOnly().expensive_task_policy = expensive_task_policy;

  if (GetMainThreadOnly().timer_queue_suspend_count != 0 ||
      GetMainThreadOnly().timer_queue_suspended_when_backgrounded) {
    new_policy.timer_queue_policy.is_enabled = false;
    // TODO(alexclarke): Figure out if we really need to do this.
    new_policy.timer_queue_policy.time_domain_type = TimeDomainType::REAL;
  }

  if (GetMainThreadOnly().renderer_suspended) {
    new_policy.loading_queue_policy.is_enabled = false;
    DCHECK(!new_policy.timer_queue_policy.is_enabled);
  }

  if (GetMainThreadOnly().use_virtual_time) {
    new_policy.compositor_queue_policy.time_domain_type =
        TimeDomainType::VIRTUAL;
    new_policy.default_queue_policy.time_domain_type = TimeDomainType::VIRTUAL;
    new_policy.loading_queue_policy.time_domain_type = TimeDomainType::VIRTUAL;
    new_policy.timer_queue_policy.time_domain_type = TimeDomainType::VIRTUAL;
  }

  new_policy.should_disable_throttling =
      ShouldDisableThrottlingBecauseOfAudio(now) ||
      GetMainThreadOnly().use_virtual_time;

  // TODO(altimin): Consider adding default timer tq to background time
  // budget pool.
  if (GetMainThreadOnly().renderer_backgrounded &&
      RuntimeEnabledFeatures::timerThrottlingForBackgroundTabsEnabled()) {
    new_policy.timer_queue_policy.time_domain_type = TimeDomainType::THROTTLED;
  }

  // Tracing is done before the early out check, because it's quite possible we
  // will otherwise miss this information in traces.
  CreateTraceEventObjectSnapshotLocked();
  TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"), "use_case",
                 use_case);
  TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"), "rail_mode",
                 new_policy.rail_mode);
  TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
                 "touchstart_expected_soon",
                 GetMainThreadOnly().touchstart_expected_soon);
  TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
                 "expensive_task_policy", expensive_task_policy);
  TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
                 "RendererScheduler.loading_tasks_seem_expensive",
                 GetMainThreadOnly().loading_tasks_seem_expensive);
  TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
                 "RendererScheduler.timer_tasks_seem_expensive",
                 GetMainThreadOnly().timer_tasks_seem_expensive);

  // TODO(alexclarke): Can we get rid of force update now?
  if (update_type == UpdateType::MAY_EARLY_OUT_IF_POLICY_UNCHANGED &&
      new_policy == GetMainThreadOnly().current_policy) {
    return;
  }

  ApplyTaskQueuePolicy(
      compositor_task_queue_.get(), compositor_task_queue_enabled_voter_.get(),
      GetMainThreadOnly().current_policy.compositor_queue_policy,
      new_policy.compositor_queue_policy);

  for (const auto& pair : loading_task_runners_) {
    ApplyTaskQueuePolicy(
        pair.first.get(), pair.second.get(),
        GetMainThreadOnly().current_policy.loading_queue_policy,
        new_policy.loading_queue_policy);
  }

  for (const auto& pair : timer_task_runners_) {
    ApplyTaskQueuePolicy(pair.first.get(), pair.second.get(),
                         GetMainThreadOnly().current_policy.timer_queue_policy,
                         new_policy.timer_queue_policy);
  }
  GetMainThreadOnly().have_reported_blocking_intervention_in_current_policy =
      false;

  // TODO(alexclarke): We shouldn't have to prioritize the default queue, but it
  // appears to be necessary since the order of loading tasks and IPCs (which
  // are mostly dispatched on the default queue) need to be preserved.
  ApplyTaskQueuePolicy(helper_.DefaultTaskQueue().get(), nullptr,
                       GetMainThreadOnly().current_policy.default_queue_policy,
                       new_policy.default_queue_policy);
  if (GetMainThreadOnly().rail_mode_observer &&
      new_policy.rail_mode != GetMainThreadOnly().current_policy.rail_mode) {
    GetMainThreadOnly().rail_mode_observer->OnRAILModeChanged(
        new_policy.rail_mode);
  }

  if (new_policy.should_disable_throttling !=
      GetMainThreadOnly().current_policy.should_disable_throttling) {
    if (new_policy.should_disable_throttling) {
      task_queue_throttler()->DisableThrottling();
    } else {
      task_queue_throttler()->EnableThrottling();
    }
  }

  DCHECK(compositor_task_queue_->IsQueueEnabled());
  GetMainThreadOnly().current_policy = new_policy;
}

void RendererSchedulerImpl::ApplyTaskQueuePolicy(
    TaskQueue* task_queue,
    TaskQueue::QueueEnabledVoter* task_queue_enabled_voter,
    const TaskQueuePolicy& old_task_queue_policy,
    const TaskQueuePolicy& new_task_queue_policy) const {
  if (task_queue_enabled_voter &&
      old_task_queue_policy.is_enabled != new_task_queue_policy.is_enabled) {
    task_queue_enabled_voter->SetQueueEnabled(new_task_queue_policy.is_enabled);
  }

  // Make sure if there's no voter that the task queue is enabled.
  DCHECK(task_queue_enabled_voter || old_task_queue_policy.is_enabled);

  if (old_task_queue_policy.priority != new_task_queue_policy.priority)
    task_queue->SetQueuePriority(new_task_queue_policy.priority);

  if (old_task_queue_policy.time_domain_type !=
      new_task_queue_policy.time_domain_type) {
    if (old_task_queue_policy.time_domain_type == TimeDomainType::THROTTLED) {
      task_queue->SetTimeDomain(real_time_domain());
      task_queue_throttler_->DecreaseThrottleRefCount(task_queue);
    } else if (new_task_queue_policy.time_domain_type ==
               TimeDomainType::THROTTLED) {
      task_queue->SetTimeDomain(real_time_domain());
      task_queue_throttler_->IncreaseThrottleRefCount(task_queue);
    } else if (new_task_queue_policy.time_domain_type ==
               TimeDomainType::VIRTUAL) {
      DCHECK(virtual_time_domain_);
      task_queue->SetTimeDomain(virtual_time_domain_.get());
    } else {
      task_queue->SetTimeDomain(real_time_domain());
    }
  }
}

RendererSchedulerImpl::UseCase RendererSchedulerImpl::ComputeCurrentUseCase(
    base::TimeTicks now,
    base::TimeDelta* expected_use_case_duration) const {
  any_thread_lock_.AssertAcquired();
  // Special case for flings. This is needed because we don't get notification
  // of a fling ending (although we do for cancellation).
  if (GetAnyThread().fling_compositor_escalation_deadline > now &&
      !GetAnyThread().awaiting_touch_start_response) {
    *expected_use_case_duration =
        GetAnyThread().fling_compositor_escalation_deadline - now;
    return UseCase::COMPOSITOR_GESTURE;
  }
  // Above all else we want to be responsive to user input.
  *expected_use_case_duration =
      GetAnyThread().user_model.TimeLeftInUserGesture(now);
  if (*expected_use_case_duration > base::TimeDelta()) {
    // Has a gesture been fully established?
    if (GetAnyThread().awaiting_touch_start_response) {
      // No, so arrange for compositor tasks to be run at the highest priority.
      return UseCase::TOUCHSTART;
    }

    // Yes a gesture has been established.  Based on how the gesture is handled
    // we need to choose between one of four use cases:
    // 1. COMPOSITOR_GESTURE where the gesture is processed only on the
    //    compositor thread.
    // 2. MAIN_THREAD_GESTURE where the gesture is processed only on the main
    //    thread.
    // 3. MAIN_THREAD_CUSTOM_INPUT_HANDLING where the main thread processes a
    //    stream of input events and has prevented a default gesture from being
    //    started.
    // 4. SYNCHRONIZED_GESTURE where the gesture is processed on both threads.
    if (GetAnyThread().last_gesture_was_compositor_driven) {
      if (GetAnyThread().begin_main_frame_on_critical_path) {
        return UseCase::SYNCHRONIZED_GESTURE;
      } else {
        return UseCase::COMPOSITOR_GESTURE;
      }
    }
    if (GetAnyThread().default_gesture_prevented) {
      return UseCase::MAIN_THREAD_CUSTOM_INPUT_HANDLING;
    } else {
      return UseCase::MAIN_THREAD_GESTURE;
    }
  }

  // Occasionally the meaningful paint fails to be detected, so as a fallback we
  // treat the presence of input as an indirect signal that there is meaningful
  // content on the page.
  if (GetAnyThread().waiting_for_meaningful_paint &&
      !GetAnyThread().have_seen_input_since_navigation) {
    return UseCase::LOADING;
  }
  return UseCase::NONE;
}

base::TimeDelta RendererSchedulerImpl::EstimateLongestJankFreeTaskDuration()
    const {
  switch (GetMainThreadOnly().current_use_case) {
    case UseCase::TOUCHSTART:
    case UseCase::COMPOSITOR_GESTURE:
    case UseCase::LOADING:
    case UseCase::NONE:
      return base::TimeDelta::FromMilliseconds(kRailsResponseTimeMillis);

    case UseCase::MAIN_THREAD_CUSTOM_INPUT_HANDLING:
    case UseCase::MAIN_THREAD_GESTURE:
    case UseCase::SYNCHRONIZED_GESTURE:
      return GetMainThreadOnly().idle_time_estimator.GetExpectedIdleDuration(
          GetMainThreadOnly().compositor_frame_interval);

    default:
      NOTREACHED();
      return base::TimeDelta::FromMilliseconds(kRailsResponseTimeMillis);
  }
}

bool RendererSchedulerImpl::CanEnterLongIdlePeriod(
    base::TimeTicks now,
    base::TimeDelta* next_long_idle_period_delay_out) {
  helper_.CheckOnValidThread();

  MaybeUpdatePolicy();
  if (GetMainThreadOnly().current_use_case == UseCase::TOUCHSTART) {
    // Don't start a long idle task in touch start priority, try again when
    // the policy is scheduled to end.
    *next_long_idle_period_delay_out =
        std::max(base::TimeDelta(),
                 GetMainThreadOnly().current_policy_expiration_time - now);
    return false;
  }
  return true;
}

SchedulerHelper* RendererSchedulerImpl::GetSchedulerHelperForTesting() {
  return &helper_;
}

TaskCostEstimator*
RendererSchedulerImpl::GetLoadingTaskCostEstimatorForTesting() {
  return &GetMainThreadOnly().loading_task_cost_estimator;
}

TaskCostEstimator*
RendererSchedulerImpl::GetTimerTaskCostEstimatorForTesting() {
  return &GetMainThreadOnly().timer_task_cost_estimator;
}

IdleTimeEstimator* RendererSchedulerImpl::GetIdleTimeEstimatorForTesting() {
  return &GetMainThreadOnly().idle_time_estimator;
}

WakeUpBudgetPool* RendererSchedulerImpl::GetWakeUpBudgetPoolForTesting() {
  return GetMainThreadOnly().wake_up_budget_pool;
}

void RendererSchedulerImpl::SuspendTimerQueue() {
  GetMainThreadOnly().timer_queue_suspend_count++;
  ForceUpdatePolicy();
#ifndef NDEBUG
  DCHECK(!default_timer_task_queue_->IsQueueEnabled());
  for (const auto& runner : timer_task_runners_) {
    DCHECK(!runner.first->IsQueueEnabled());
  }
#endif
}

void RendererSchedulerImpl::ResumeTimerQueue() {
  GetMainThreadOnly().timer_queue_suspend_count--;
  DCHECK_GE(GetMainThreadOnly().timer_queue_suspend_count, 0);
  ForceUpdatePolicy();
}

void RendererSchedulerImpl::SetTimerQueueSuspensionWhenBackgroundedEnabled(
    bool enabled) {
  // Note that this will only take effect for the next backgrounded signal.
  GetMainThreadOnly().timer_queue_suspension_when_backgrounded_enabled =
      enabled;
}

std::unique_ptr<base::trace_event::ConvertableToTraceFormat>
RendererSchedulerImpl::AsValue(base::TimeTicks optional_now) const {
  base::AutoLock lock(any_thread_lock_);
  return AsValueLocked(optional_now);
}

void RendererSchedulerImpl::CreateTraceEventObjectSnapshot() const {
  TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID(
      TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"), "RendererScheduler",
      this, AsValue(helper_.scheduler_tqm_delegate()->NowTicks()));
}

void RendererSchedulerImpl::CreateTraceEventObjectSnapshotLocked() const {
  TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID(
      TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"), "RendererScheduler",
      this, AsValueLocked(helper_.scheduler_tqm_delegate()->NowTicks()));
}

// static
const char* RendererSchedulerImpl::ExpensiveTaskPolicyToString(
    ExpensiveTaskPolicy expensive_task_policy) {
  switch (expensive_task_policy) {
    case ExpensiveTaskPolicy::RUN:
      return "RUN";
    case ExpensiveTaskPolicy::BLOCK:
      return "BLOCK";
    case ExpensiveTaskPolicy::THROTTLE:
      return "THROTTLE";
    default:
      NOTREACHED();
      return nullptr;
  }
}

std::unique_ptr<base::trace_event::ConvertableToTraceFormat>
RendererSchedulerImpl::AsValueLocked(base::TimeTicks optional_now) const {
  helper_.CheckOnValidThread();
  any_thread_lock_.AssertAcquired();

  if (optional_now.is_null())
    optional_now = helper_.scheduler_tqm_delegate()->NowTicks();
  std::unique_ptr<base::trace_event::TracedValue> state(
      new base::trace_event::TracedValue());
  state->SetBoolean(
      "has_visible_render_widget_with_touch_handler",
      GetMainThreadOnly().has_visible_render_widget_with_touch_handler);
  state->SetString("current_use_case",
                   UseCaseToString(GetMainThreadOnly().current_use_case));
  state->SetBoolean("loading_tasks_seem_expensive",
                    GetMainThreadOnly().loading_tasks_seem_expensive);
  state->SetBoolean("timer_tasks_seem_expensive",
                    GetMainThreadOnly().timer_tasks_seem_expensive);
  state->SetBoolean("begin_frame_not_expected_soon",
                    GetMainThreadOnly().begin_frame_not_expected_soon);
  state->SetBoolean("touchstart_expected_soon",
                    GetMainThreadOnly().touchstart_expected_soon);
  state->SetString("idle_period_state",
                   IdleHelper::IdlePeriodStateToString(
                       idle_helper_.SchedulerIdlePeriodState()));
  state->SetBoolean("renderer_hidden", GetMainThreadOnly().renderer_hidden);
  state->SetBoolean("have_seen_a_begin_main_frame",
                    GetMainThreadOnly().have_seen_a_begin_main_frame);
  state->SetBoolean("waiting_for_meaningful_paint",
                    GetAnyThread().waiting_for_meaningful_paint);
  state->SetBoolean("have_seen_input_since_navigation",
                    GetAnyThread().have_seen_input_since_navigation);
  state->SetBoolean("have_reported_blocking_intervention_in_current_policy",
                    GetMainThreadOnly()
                        .have_reported_blocking_intervention_in_current_policy);
  state->SetBoolean(
      "have_reported_blocking_intervention_since_navigation",
      GetMainThreadOnly().have_reported_blocking_intervention_since_navigation);
  state->SetBoolean("renderer_backgrounded",
                    GetMainThreadOnly().renderer_backgrounded);
  state->SetBoolean(
      "timer_queue_suspended_when_backgrounded",
      GetMainThreadOnly().timer_queue_suspended_when_backgrounded);
  state->SetInteger("timer_queue_suspend_count",
                    GetMainThreadOnly().timer_queue_suspend_count);
  state->SetDouble("now", (optional_now - base::TimeTicks()).InMillisecondsF());
  state->SetDouble(
      "fling_compositor_escalation_deadline",
      (GetAnyThread().fling_compositor_escalation_deadline - base::TimeTicks())
          .InMillisecondsF());
  state->SetInteger("navigation_task_expected_count",
                    GetMainThreadOnly().navigation_task_expected_count);
  state->SetDouble(
      "last_idle_period_end_time",
      (GetAnyThread().last_idle_period_end_time - base::TimeTicks())
          .InMillisecondsF());
  state->SetBoolean("awaiting_touch_start_response",
                    GetAnyThread().awaiting_touch_start_response);
  state->SetBoolean("begin_main_frame_on_critical_path",
                    GetAnyThread().begin_main_frame_on_critical_path);
  state->SetBoolean("last_gesture_was_compositor_driven",
                    GetAnyThread().last_gesture_was_compositor_driven);
  state->SetBoolean("default_gesture_prevented",
                    GetAnyThread().default_gesture_prevented);
  state->SetDouble("expected_loading_task_duration",
                   GetMainThreadOnly()
                       .loading_task_cost_estimator.expected_task_duration()
                       .InMillisecondsF());
  state->SetDouble("expected_timer_task_duration",
                   GetMainThreadOnly()
                       .timer_task_cost_estimator.expected_task_duration()
                       .InMillisecondsF());
  state->SetBoolean("is_audio_playing", GetMainThreadOnly().is_audio_playing);

  state->BeginDictionary("web_view_schedulers");
  for (WebViewSchedulerImpl* web_view_scheduler :
       GetMainThreadOnly().web_view_schedulers) {
    state->BeginDictionaryWithCopiedName(
        trace_helper::PointerToString(web_view_scheduler));
    web_view_scheduler->AsValueInto(state.get());
    state->EndDictionary();
  }
  state->EndDictionary();

  state->BeginDictionary("policy");
  GetMainThreadOnly().current_policy.AsValueInto(state.get());
  state->EndDictionary();

  // TODO(skyostil): Can we somehow trace how accurate these estimates were?
  state->SetDouble(
      "longest_jank_free_task_duration",
      GetMainThreadOnly().longest_jank_free_task_duration.InMillisecondsF());
  state->SetDouble(
      "compositor_frame_interval",
      GetMainThreadOnly().compositor_frame_interval.InMillisecondsF());
  state->SetDouble(
      "estimated_next_frame_begin",
      (GetMainThreadOnly().estimated_next_frame_begin - base::TimeTicks())
          .InMillisecondsF());
  state->SetBoolean("in_idle_period", GetAnyThread().in_idle_period);

  state->SetString(
      "expensive_task_policy",
      ExpensiveTaskPolicyToString(GetMainThreadOnly().expensive_task_policy));

  GetAnyThread().user_model.AsValueInto(state.get());
  render_widget_scheduler_signals_.AsValueInto(state.get());

  state->BeginDictionary("task_queue_throttler");
  task_queue_throttler_->AsValueInto(state.get(), optional_now);
  state->EndDictionary();

  return std::move(state);
}

void RendererSchedulerImpl::TaskQueuePolicy::AsValueInto(
    base::trace_event::TracedValue* state) const {
  state->SetBoolean("is_enabled", is_enabled);
  state->SetString("priority", TaskQueue::PriorityToString(priority));
  state->SetString("time_domain_type",
                   TimeDomainTypeToString(time_domain_type));
}

void RendererSchedulerImpl::Policy::AsValueInto(
    base::trace_event::TracedValue* state) const {
  state->BeginDictionary("compositor_queue_policy");
  compositor_queue_policy.AsValueInto(state);
  state->EndDictionary();

  state->BeginDictionary("loading_queue_policy");
  loading_queue_policy.AsValueInto(state);
  state->EndDictionary();

  state->BeginDictionary("timer_queue_policy");
  timer_queue_policy.AsValueInto(state);
  state->EndDictionary();

  state->BeginDictionary("default_queue_policy");
  default_queue_policy.AsValueInto(state);
  state->EndDictionary();

  state->SetString("rail_mode", RAILModeToString(rail_mode));
  state->SetBoolean("should_disable_throttling", should_disable_throttling);
}

void RendererSchedulerImpl::OnIdlePeriodStarted() {
  base::AutoLock lock(any_thread_lock_);
  GetAnyThread().in_idle_period = true;
  UpdatePolicyLocked(UpdateType::MAY_EARLY_OUT_IF_POLICY_UNCHANGED);
}

void RendererSchedulerImpl::OnIdlePeriodEnded() {
  base::AutoLock lock(any_thread_lock_);
  GetAnyThread().last_idle_period_end_time =
      helper_.scheduler_tqm_delegate()->NowTicks();
  GetAnyThread().in_idle_period = false;
  UpdatePolicyLocked(UpdateType::MAY_EARLY_OUT_IF_POLICY_UNCHANGED);
}

void RendererSchedulerImpl::AddPendingNavigation(NavigatingFrameType type) {
  helper_.CheckOnValidThread();
  if (type == NavigatingFrameType::kMainFrame) {
    GetMainThreadOnly().navigation_task_expected_count++;
    UpdatePolicy();
  }
}

void RendererSchedulerImpl::RemovePendingNavigation(NavigatingFrameType type) {
  helper_.CheckOnValidThread();
  DCHECK_GT(GetMainThreadOnly().navigation_task_expected_count, 0);
  if (type == NavigatingFrameType::kMainFrame &&
      GetMainThreadOnly().navigation_task_expected_count > 0) {
    GetMainThreadOnly().navigation_task_expected_count--;
    UpdatePolicy();
  }
}

void RendererSchedulerImpl::OnNavigationStarted() {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::OnNavigationStarted");
  base::AutoLock lock(any_thread_lock_);
  ResetForNavigationLocked();
}

void RendererSchedulerImpl::OnFirstMeaningfulPaint() {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
               "RendererSchedulerImpl::OnFirstMeaningfulPaint");
  base::AutoLock lock(any_thread_lock_);
  GetAnyThread().waiting_for_meaningful_paint = false;
  UpdatePolicyLocked(UpdateType::MAY_EARLY_OUT_IF_POLICY_UNCHANGED);
}

void RendererSchedulerImpl::SuspendTimerQueueWhenBackgrounded() {
  DCHECK(GetMainThreadOnly().renderer_backgrounded);
  if (GetMainThreadOnly().timer_queue_suspended_when_backgrounded)
    return;

  GetMainThreadOnly().timer_queue_suspended_when_backgrounded = true;
  ForceUpdatePolicy();
  Platform::Current()->RequestPurgeMemory();
}

void RendererSchedulerImpl::ResumeTimerQueueWhenForegroundedOrResumed() {
  DCHECK(!GetMainThreadOnly().renderer_backgrounded ||
         (GetMainThreadOnly().renderer_backgrounded &&
          !GetMainThreadOnly().renderer_suspended));
  if (!GetMainThreadOnly().timer_queue_suspended_when_backgrounded)
    return;

  GetMainThreadOnly().timer_queue_suspended_when_backgrounded = false;
  ForceUpdatePolicy();
}

void RendererSchedulerImpl::ResetForNavigationLocked() {
  helper_.CheckOnValidThread();
  any_thread_lock_.AssertAcquired();
  GetAnyThread().user_model.Reset(helper_.scheduler_tqm_delegate()->NowTicks());
  GetAnyThread().have_seen_a_potentially_blocking_gesture = false;
  GetAnyThread().waiting_for_meaningful_paint = true;
  GetAnyThread().have_seen_input_since_navigation = false;
  GetMainThreadOnly().loading_task_cost_estimator.Clear();
  GetMainThreadOnly().timer_task_cost_estimator.Clear();
  GetMainThreadOnly().idle_time_estimator.Clear();
  GetMainThreadOnly().have_seen_a_begin_main_frame = false;
  GetMainThreadOnly().have_reported_blocking_intervention_since_navigation =
      false;
  for (WebViewSchedulerImpl* web_view_scheduler :
       GetMainThreadOnly().web_view_schedulers) {
    web_view_scheduler->OnNavigation();
  }
  UpdatePolicyLocked(UpdateType::MAY_EARLY_OUT_IF_POLICY_UNCHANGED);
}

void RendererSchedulerImpl::SetTopLevelBlameContext(
    base::trace_event::BlameContext* blame_context) {
  // Any task that runs in the default task runners belongs to the context of
  // all frames (as opposed to a particular frame). Note that the task itself
  // may still enter a more specific blame context if necessary.
  //
  // Per-frame task runners (loading, timers, etc.) are configured with a more
  // specific blame context by WebFrameSchedulerImpl.
  control_task_queue_->SetBlameContext(blame_context);
  DefaultTaskQueue()->SetBlameContext(blame_context);
  default_loading_task_queue_->SetBlameContext(blame_context);
  default_timer_task_queue_->SetBlameContext(blame_context);
  compositor_task_queue_->SetBlameContext(blame_context);
  idle_helper_.IdleTaskRunner()->SetBlameContext(blame_context);
}

void RendererSchedulerImpl::SetRAILModeObserver(RAILModeObserver* observer) {
  GetMainThreadOnly().rail_mode_observer = observer;
}

bool RendererSchedulerImpl::MainThreadSeemsUnresponsive(
    base::TimeDelta main_thread_responsiveness_threshold) {
  base::TimeTicks now = tick_clock()->NowTicks();
  base::TimeDelta estimated_queueing_time;

  bool can_read = false;

  base::subtle::Atomic32 version;
  seqlock_queueing_time_estimator_.seqlock.TryRead(&can_read, &version);

  // If we fail to determine if the main thread is busy, assume whether or not
  // it's busy hasn't change since the last time we asked.
  if (!can_read)
    return GetCompositorThreadOnly().main_thread_seems_unresponsive;

  QueueingTimeEstimator::State queueing_time_estimator_state =
      seqlock_queueing_time_estimator_.data.GetState();

  // If we fail to determine if the main thread is busy, assume whether or not
  // it's busy hasn't change since the last time we asked.
  if (seqlock_queueing_time_estimator_.seqlock.ReadRetry(version))
    return GetCompositorThreadOnly().main_thread_seems_unresponsive;

  QueueingTimeEstimator queueing_time_estimator(queueing_time_estimator_state);

  estimated_queueing_time =
      queueing_time_estimator.EstimateQueueingTimeIncludingCurrentTask(now);

  bool main_thread_seems_unresponsive =
      estimated_queueing_time > main_thread_responsiveness_threshold;
  GetCompositorThreadOnly().main_thread_seems_unresponsive =
      main_thread_seems_unresponsive;

  return main_thread_seems_unresponsive;
}

void RendererSchedulerImpl::RegisterTimeDomain(TimeDomain* time_domain) {
  helper_.RegisterTimeDomain(time_domain);
}

void RendererSchedulerImpl::UnregisterTimeDomain(TimeDomain* time_domain) {
  helper_.UnregisterTimeDomain(time_domain);
}

base::TickClock* RendererSchedulerImpl::tick_clock() const {
  return helper_.scheduler_tqm_delegate().get();
}

void RendererSchedulerImpl::AddWebViewScheduler(
    WebViewSchedulerImpl* web_view_scheduler) {
  GetMainThreadOnly().web_view_schedulers.insert(web_view_scheduler);
}

void RendererSchedulerImpl::RemoveWebViewScheduler(
    WebViewSchedulerImpl* web_view_scheduler) {
  DCHECK(GetMainThreadOnly().web_view_schedulers.find(web_view_scheduler) !=
         GetMainThreadOnly().web_view_schedulers.end());
  GetMainThreadOnly().web_view_schedulers.erase(web_view_scheduler);
}

void RendererSchedulerImpl::BroadcastIntervention(const std::string& message) {
  helper_.CheckOnValidThread();
  for (auto* web_view_scheduler : GetMainThreadOnly().web_view_schedulers)
    web_view_scheduler->ReportIntervention(message);
}

void RendererSchedulerImpl::OnTriedToExecuteBlockedTask(
    const TaskQueue& queue,
    const base::PendingTask& task) {
  if (GetMainThreadOnly().current_use_case == UseCase::TOUCHSTART ||
      GetMainThreadOnly().longest_jank_free_task_duration <
          base::TimeDelta::FromMilliseconds(kRailsResponseTimeMillis) ||
      GetMainThreadOnly().timer_queue_suspend_count ||
      GetMainThreadOnly().timer_queue_suspended_when_backgrounded) {
    return;
  }
  if (!GetMainThreadOnly().timer_tasks_seem_expensive &&
      !GetMainThreadOnly().loading_tasks_seem_expensive) {
    return;
  }
  if (!GetMainThreadOnly()
           .have_reported_blocking_intervention_in_current_policy) {
    GetMainThreadOnly().have_reported_blocking_intervention_in_current_policy =
        true;
    TRACE_EVENT_INSTANT0("renderer.scheduler",
                         "RendererSchedulerImpl::TaskBlocked",
                         TRACE_EVENT_SCOPE_THREAD);
  }

  if (!GetMainThreadOnly()
           .have_reported_blocking_intervention_since_navigation) {
    {
      base::AutoLock lock(any_thread_lock_);
      if (!GetAnyThread().have_seen_a_potentially_blocking_gesture)
        return;
    }
    GetMainThreadOnly().have_reported_blocking_intervention_since_navigation =
        true;
    BroadcastIntervention(
        "Blink deferred a task in order to make scrolling smoother. "
        "Your timer and network tasks should take less than 50ms to run "
        "to avoid this. Please see "
        "https://developers.google.com/web/tools/chrome-devtools/profile/"
        "evaluate-performance/rail"
        " and https://crbug.com/574343#c40 for more information.");
  }
}

void RendererSchedulerImpl::WillProcessTask(TaskQueue* task_queue,
                                            double start_time) {
  base::TimeTicks start_time_ticks =
      MonotonicTimeInSecondsToTimeTicks(start_time);
  GetMainThreadOnly().current_task_start_time = start_time_ticks;
  seqlock_queueing_time_estimator_.seqlock.WriteBegin();
  seqlock_queueing_time_estimator_.data.OnTopLevelTaskStarted(start_time_ticks);
  seqlock_queueing_time_estimator_.seqlock.WriteEnd();
}

void RendererSchedulerImpl::DidProcessTask(TaskQueue* task_queue,
                                           double start_time,
                                           double end_time) {
  // TODO(scheduler-dev): Remove conversions when Blink starts using
  // base::TimeTicks instead of doubles for time.
  base::TimeTicks start_time_ticks =
      MonotonicTimeInSecondsToTimeTicks(start_time);
  base::TimeTicks end_time_ticks = MonotonicTimeInSecondsToTimeTicks(end_time);

  seqlock_queueing_time_estimator_.seqlock.WriteBegin();
  seqlock_queueing_time_estimator_.data.OnTopLevelTaskCompleted(end_time_ticks);
  seqlock_queueing_time_estimator_.seqlock.WriteEnd();

  task_queue_throttler()->OnTaskRunTimeReported(task_queue, start_time_ticks,
                                                end_time_ticks);

  // We want to measure thread time here, but for efficiency reasons
  // we stick with wall time.
  GetMainThreadOnly().foreground_main_thread_load_tracker.RecordTaskTime(
      start_time_ticks, end_time_ticks);
  GetMainThreadOnly().background_main_thread_load_tracker.RecordTaskTime(
      start_time_ticks, end_time_ticks);

  // TODO(altimin): Per-page metrics should also be considered.
  RecordTaskMetrics(task_queue->GetQueueType(),
                    end_time_ticks - start_time_ticks);
}

void RendererSchedulerImpl::RecordTaskMetrics(TaskQueue::QueueType queue_type,
                                              base::TimeDelta duration) {
  UMA_HISTOGRAM_CUSTOM_COUNTS("RendererScheduler.TaskTime",
                              duration.InMicroseconds(), 1, 1000 * 1000, 50);

  // TODO(altimin): See whether this metric is still useful after
  // adding RendererScheduler.TaskDurationPerQueueType.
  UMA_HISTOGRAM_ENUMERATION("RendererScheduler.NumberOfTasksPerQueueType",
                            static_cast<int>(queue_type),
                            static_cast<int>(TaskQueue::QueueType::COUNT));

  RecordTaskDurationPerQueueType(queue_type, duration);
}

void RendererSchedulerImpl::RecordTaskDurationPerQueueType(
    TaskQueue::QueueType queue_type,
    base::TimeDelta duration) {
  duration = std::min(duration, kMaxTaskDurationForReporting);

  // Report only whole milliseconds to avoid overflow.
  base::TimeDelta& unreported_duration =
      GetMainThreadOnly()
          .unreported_task_duration[static_cast<int>(queue_type)];
  unreported_duration += duration;
  int64_t milliseconds = unreported_duration.InMilliseconds();
  if (milliseconds > 0) {
    unreported_duration -= base::TimeDelta::FromMilliseconds(milliseconds);
    GetMainThreadOnly().task_duration_per_queue_type_histogram->AddCount(
        static_cast<int>(queue_type), static_cast<int>(milliseconds));
  }
}

void RendererSchedulerImpl::OnBeginNestedRunLoop() {
  seqlock_queueing_time_estimator_.seqlock.WriteBegin();
  seqlock_queueing_time_estimator_.data.OnBeginNestedRunLoop();
  seqlock_queueing_time_estimator_.seqlock.WriteEnd();
}

void RendererSchedulerImpl::AddTaskTimeObserver(
    TaskTimeObserver* task_time_observer) {
  helper_.AddTaskTimeObserver(task_time_observer);
}

void RendererSchedulerImpl::RemoveTaskTimeObserver(
    TaskTimeObserver* task_time_observer) {
  helper_.RemoveTaskTimeObserver(task_time_observer);
}

void RendererSchedulerImpl::OnQueueingTimeForWindowEstimated(
    base::TimeDelta queueing_time,
    base::TimeTicks window_start_time) {
  // RendererScheduler reports the queueing time once per window's duration.
  //          |stepEQT|stepEQT|stepEQT|stepEQT|stepEQT|stepEQT|
  // Report:  |-------window EQT------|
  // Discard:         |-------window EQT------|
  // Discard:                 |-------window EQT------|
  // Report:                          |-------window EQT------|
  if (window_start_time -
          GetMainThreadOnly().uma_last_queueing_time_report_window_start_time <
      kQueueingTimeWindowDuration) {
    return;
  }
  UMA_HISTOGRAM_TIMES("RendererScheduler.ExpectedTaskQueueingDuration",
                      queueing_time);
  TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler"),
                 "estimated_queueing_time_for_window",
                 queueing_time.InMillisecondsF());
  GetMainThreadOnly().uma_last_queueing_time_report_window_start_time =
      window_start_time;
}

AutoAdvancingVirtualTimeDomain* RendererSchedulerImpl::GetVirtualTimeDomain() {
  if (!virtual_time_domain_) {
    virtual_time_domain_.reset(
        new AutoAdvancingVirtualTimeDomain(tick_clock()->NowTicks()));
    RegisterTimeDomain(virtual_time_domain_.get());
  }
  return virtual_time_domain_.get();
}

void RendererSchedulerImpl::EnableVirtualTime() {
  GetMainThreadOnly().use_virtual_time = true;

  // The |unthrottled_task_runners_| are not actively managed by UpdatePolicy().
  AutoAdvancingVirtualTimeDomain* time_domain = GetVirtualTimeDomain();
  for (const scoped_refptr<TaskQueue>& task_queue : unthrottled_task_runners_)
    task_queue->SetTimeDomain(time_domain);

  DCHECK(!virtual_time_control_task_queue_);
  virtual_time_control_task_queue_ =
      helper_.NewTaskQueue(TaskQueue::Spec(TaskQueue::QueueType::CONTROL));
  virtual_time_control_task_queue_->SetQueuePriority(
      TaskQueue::CONTROL_PRIORITY);
  virtual_time_control_task_queue_->SetTimeDomain(time_domain);

  ForceUpdatePolicy();
}

void RendererSchedulerImpl::DisableVirtualTimeForTesting() {
  GetMainThreadOnly().use_virtual_time = false;

  RealTimeDomain* time_domain = real_time_domain();
  // The |unthrottled_task_runners_| are not actively managed by UpdatePolicy().
  for (const scoped_refptr<TaskQueue>& task_queue : unthrottled_task_runners_)
    task_queue->SetTimeDomain(time_domain);
  virtual_time_control_task_queue_->UnregisterTaskQueue();
  virtual_time_control_task_queue_ = nullptr;

  ForceUpdatePolicy();
}

bool RendererSchedulerImpl::ShouldDisableThrottlingBecauseOfAudio(
    base::TimeTicks now) {
  if (!GetMainThreadOnly().last_audio_state_change)
    return false;

  if (GetMainThreadOnly().is_audio_playing)
    return true;

  return GetMainThreadOnly().last_audio_state_change.value() +
             kThrottlingDelayAfterAudioIsPlayed >
         now;
}

void RendererSchedulerImpl::AddQueueToWakeUpBudgetPool(TaskQueue* queue) {
  if (!GetMainThreadOnly().wake_up_budget_pool) {
    GetMainThreadOnly().wake_up_budget_pool =
        task_queue_throttler()->CreateWakeUpBudgetPool("renderer_wake_up_pool");
    GetMainThreadOnly().wake_up_budget_pool->SetWakeUpRate(1);
    GetMainThreadOnly().wake_up_budget_pool->SetWakeUpDuration(
        base::TimeDelta());
  }
  GetMainThreadOnly().wake_up_budget_pool->AddQueue(tick_clock()->NowTicks(),
                                                    queue);
}

TimeDomain* RendererSchedulerImpl::GetActiveTimeDomain() {
  if (GetMainThreadOnly().use_virtual_time) {
    return GetVirtualTimeDomain();
  } else {
    return real_time_domain();
  }
}

void RendererSchedulerImpl::OnTraceLogEnabled() {
  CreateTraceEventObjectSnapshot();
}

void RendererSchedulerImpl::OnTraceLogDisabled() {}

// static
const char* RendererSchedulerImpl::UseCaseToString(UseCase use_case) {
  switch (use_case) {
    case UseCase::NONE:
      return "none";
    case UseCase::COMPOSITOR_GESTURE:
      return "compositor_gesture";
    case UseCase::MAIN_THREAD_CUSTOM_INPUT_HANDLING:
      return "main_thread_custom_input_handling";
    case UseCase::SYNCHRONIZED_GESTURE:
      return "synchronized_gesture";
    case UseCase::TOUCHSTART:
      return "touchstart";
    case UseCase::LOADING:
      return "loading";
    case UseCase::MAIN_THREAD_GESTURE:
      return "main_thread_gesture";
    default:
      NOTREACHED();
      return nullptr;
  }
}

// static
const char* RendererSchedulerImpl::RAILModeToString(v8::RAILMode rail_mode) {
  switch (rail_mode) {
    case v8::PERFORMANCE_RESPONSE:
      return "response";
    case v8::PERFORMANCE_ANIMATION:
      return "animation";
    case v8::PERFORMANCE_IDLE:
      return "idle";
    case v8::PERFORMANCE_LOAD:
      return "load";
    default:
      NOTREACHED();
      return nullptr;
  }
}

// static
const char* RendererSchedulerImpl::TimeDomainTypeToString(
    TimeDomainType domain_type) {
  switch (domain_type) {
    case TimeDomainType::REAL:
      return "real";
    case TimeDomainType::THROTTLED:
      return "throttled";
    case TimeDomainType::VIRTUAL:
      return "virtual";
    default:
      NOTREACHED();
      return nullptr;
  }
}

}  // namespace scheduler
}  // namespace blink
