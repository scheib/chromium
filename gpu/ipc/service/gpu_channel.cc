// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/service/gpu_channel.h"

#include <utility>

#if defined(OS_WIN)
#include <windows.h>
#endif

#include <algorithm>
#include <deque>
#include <set>
#include <vector>

#include "base/atomicops.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/numerics/safe_conversions.h"
#include "base/single_thread_task_runner.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/timer/timer.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/process_memory_dump.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/command_buffer/service/image_factory.h"
#include "gpu/command_buffer/service/mailbox_manager.h"
#include "gpu/command_buffer/service/preemption_flag.h"
#include "gpu/command_buffer/service/scheduler.h"
#include "gpu/ipc/common/gpu_messages.h"
#include "gpu/ipc/service/gpu_channel_manager.h"
#include "gpu/ipc/service/gpu_channel_manager_delegate.h"
#include "gpu/ipc/service/gpu_memory_buffer_factory.h"
#include "ipc/ipc_channel.h"
#include "ipc/message_filter.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_image_shared_memory.h"
#include "ui/gl/gl_surface.h"

namespace gpu {
namespace {

// Number of milliseconds between successive vsync. Many GL commands block
// on vsync, so thresholds for preemption should be multiples of this.
const int64_t kVsyncIntervalMs = 17;

// Amount of time that we will wait for an IPC to be processed before
// preempting. After a preemption, we must wait this long before triggering
// another preemption.
const int64_t kPreemptWaitTimeMs = 2 * kVsyncIntervalMs;

// Once we trigger a preemption, the maximum duration that we will wait
// before clearing the preemption.
const int64_t kMaxPreemptTimeMs = kVsyncIntervalMs;

// Stop the preemption once the time for the longest pending IPC drops
// below this threshold.
const int64_t kStopPreemptThresholdMs = kVsyncIntervalMs;

CommandBufferId GenerateCommandBufferId(int channel_id, int32_t route_id) {
  return CommandBufferId::FromUnsafeValue(
      (static_cast<uint64_t>(channel_id) << 32) | route_id);
}

}  // anonymous namespace

struct GpuChannelMessage {
  IPC::Message message;
  uint32_t order_number;
  base::TimeTicks time_received;

  GpuChannelMessage(const IPC::Message& msg,
                    uint32_t order_num,
                    base::TimeTicks ts)
      : message(msg), order_number(order_num), time_received(ts) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(GpuChannelMessage);
};

// This message queue counts and timestamps each message forwarded to the
// channel so that we can preempt other channels if a message takes too long to
// process. To guarantee fairness, we must wait a minimum amount of time before
// preempting and we limit the amount of time that we can preempt in one shot
// (see constants above).
class GpuChannelMessageQueue
    : public base::RefCountedThreadSafe<GpuChannelMessageQueue> {
 public:
  GpuChannelMessageQueue(
      GpuChannel* channel,
      scoped_refptr<SyncPointOrderData> sync_point_order_data,
      scoped_refptr<base::SingleThreadTaskRunner> main_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
      scoped_refptr<PreemptionFlag> preempting_flag,
      scoped_refptr<PreemptionFlag> preempted_flag);

  void Destroy();

  SequenceId sequence_id() const {
    return sync_point_order_data_->sequence_id();
  }

  bool IsScheduled() const;
  void SetScheduled(bool scheduled);

  // Should be called before a message begins to be processed. Returns false if
  // there are no messages to process.
  const GpuChannelMessage* BeginMessageProcessing();
  // Should be called if a message began processing but did not finish.
  void PauseMessageProcessing();
  // Should be called if a message is completely processed. Returns true if
  // there are more messages to process.
  void FinishMessageProcessing();

  void PushBackMessage(const IPC::Message& message);

 private:
  enum PreemptionState {
    // Either there's no other channel to preempt, there are no messages
    // pending processing, or we just finished preempting and have to wait
    // before preempting again.
    IDLE,
    // We are waiting kPreemptWaitTimeMs before checking if we should preempt.
    WAITING,
    // We can preempt whenever any IPC processing takes more than
    // kPreemptWaitTimeMs.
    CHECKING,
    // We are currently preempting (i.e. no stub is descheduled).
    PREEMPTING,
    // We would like to preempt, but some stub is descheduled.
    WOULD_PREEMPT_DESCHEDULED,
  };

  friend class base::RefCountedThreadSafe<GpuChannelMessageQueue>;

  ~GpuChannelMessageQueue();

  void PostHandleMessageOnQueue();

  void UpdatePreemptionState();
  void UpdatePreemptionStateHelper();

  void UpdateStateIdle();
  void UpdateStateWaiting();
  void UpdateStateChecking();
  void UpdateStatePreempting();
  void UpdateStateWouldPreemptDescheduled();

  void TransitionToIdle();
  void TransitionToWaiting();
  void TransitionToChecking();
  void TransitionToPreempting();
  void TransitionToWouldPreemptDescheduled();

  bool ShouldTransitionToIdle() const;

  // These can be accessed from both IO and main threads and are protected by
  // |channel_lock_|.
  bool scheduled_ = true;
  GpuChannel* channel_ = nullptr;  // set to nullptr on Destroy
  std::deque<std::unique_ptr<GpuChannelMessage>> channel_messages_;
  bool handle_message_post_task_pending_ = false;
  mutable base::Lock channel_lock_;

  // The following are accessed on the IO thread only.
  // No lock is necessary for preemption state because it's only accessed on the
  // IO thread.
  PreemptionState preemption_state_ = IDLE;
  // Maximum amount of time that we can spend in PREEMPTING.
  // It is reset when we transition to IDLE.
  base::TimeDelta max_preemption_time_;
  // This timer is used and runs tasks on the IO thread.
  std::unique_ptr<base::OneShotTimer> timer_;
  base::ThreadChecker io_thread_checker_;

  // Keeps track of sync point related state such as message order numbers.
  scoped_refptr<SyncPointOrderData> sync_point_order_data_;

  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;
  scoped_refptr<PreemptionFlag> preempting_flag_;
  scoped_refptr<PreemptionFlag> preempted_flag_;

  DISALLOW_COPY_AND_ASSIGN(GpuChannelMessageQueue);
};

// This filter does the following:
// - handles the Nop message used for verifying sync tokens on the IO thread
// - forwards messages to child message filters
// - posts control and out of order messages to the main thread
// - forwards other messages to the message queue or the scheduler
class GPU_EXPORT GpuChannelMessageFilter : public IPC::MessageFilter {
 public:
  GpuChannelMessageFilter(
      GpuChannel* gpu_channel,
      Scheduler* scheduler,
      scoped_refptr<GpuChannelMessageQueue> message_queue,
      scoped_refptr<base::SingleThreadTaskRunner> main_task_runner);

  // Methods called on main thread.
  void Destroy();

  // Called when scheduler is enabled.
  void AddRoute(int32_t route_id, SequenceId sequence_id);
  void RemoveRoute(int32_t route_id);

  // Methods called on IO thread.
  // IPC::MessageFilter implementation.
  void OnFilterAdded(IPC::Channel* channel) override;
  void OnFilterRemoved() override;
  void OnChannelConnected(int32_t peer_pid) override;
  void OnChannelError() override;
  void OnChannelClosing() override;
  bool OnMessageReceived(const IPC::Message& message) override;

  void AddChannelFilter(scoped_refptr<IPC::MessageFilter> filter);
  void RemoveChannelFilter(scoped_refptr<IPC::MessageFilter> filter);

 private:
  ~GpuChannelMessageFilter() override;

  bool MessageErrorHandler(const IPC::Message& message, const char* error_msg);

  IPC::Channel* ipc_channel_ = nullptr;
  base::ProcessId peer_pid_ = base::kNullProcessId;
  std::vector<scoped_refptr<IPC::MessageFilter>> channel_filters_;

  GpuChannel* gpu_channel_ = nullptr;
  // Map of route id to scheduler sequence id.
  base::flat_map<int32_t, SequenceId> route_sequences_;
  mutable base::Lock gpu_channel_lock_;

  Scheduler* scheduler_;
  scoped_refptr<GpuChannelMessageQueue> message_queue_;
  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;

  DISALLOW_COPY_AND_ASSIGN(GpuChannelMessageFilter);
};

GpuChannelMessageQueue::GpuChannelMessageQueue(
    GpuChannel* channel,
    scoped_refptr<SyncPointOrderData> sync_point_order_data,
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
    scoped_refptr<PreemptionFlag> preempting_flag,
    scoped_refptr<PreemptionFlag> preempted_flag)
    : channel_(channel),
      max_preemption_time_(
          base::TimeDelta::FromMilliseconds(kMaxPreemptTimeMs)),
      timer_(new base::OneShotTimer),
      sync_point_order_data_(std::move(sync_point_order_data)),
      main_task_runner_(std::move(main_task_runner)),
      io_task_runner_(std::move(io_task_runner)),
      preempting_flag_(std::move(preempting_flag)),
      preempted_flag_(std::move(preempted_flag)) {
  timer_->SetTaskRunner(io_task_runner_);
  io_thread_checker_.DetachFromThread();
}

GpuChannelMessageQueue::~GpuChannelMessageQueue() = default;

void GpuChannelMessageQueue::Destroy() {
  // There's no need to reply to sync messages here because the channel is being
  // destroyed and the client Sends will fail.
  sync_point_order_data_->Destroy();

  if (preempting_flag_)
    preempting_flag_->Reset();

  // Destroy timer on io thread.
  io_task_runner_->PostTask(
      FROM_HERE, base::Bind([](std::unique_ptr<base::OneShotTimer>) {},
                            base::Passed(&timer_)));

  channel_ = nullptr;
}

bool GpuChannelMessageQueue::IsScheduled() const {
  base::AutoLock lock(channel_lock_);
  return scheduled_;
}

void GpuChannelMessageQueue::SetScheduled(bool scheduled) {
  base::AutoLock lock(channel_lock_);
  if (scheduled_ == scheduled)
    return;
  scheduled_ = scheduled;
  if (scheduled)
    PostHandleMessageOnQueue();
  if (preempting_flag_) {
    io_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&GpuChannelMessageQueue::UpdatePreemptionState, this));
  }
}

void GpuChannelMessageQueue::PushBackMessage(const IPC::Message& message) {
  base::AutoLock auto_lock(channel_lock_);
  DCHECK(channel_);
  uint32_t order_num = sync_point_order_data_->GenerateUnprocessedOrderNumber();
  std::unique_ptr<GpuChannelMessage> msg(
      new GpuChannelMessage(message, order_num, base::TimeTicks::Now()));

  channel_messages_.push_back(std::move(msg));

  bool first_message = channel_messages_.size() == 1;
  if (first_message)
    PostHandleMessageOnQueue();

  if (preempting_flag_)
    UpdatePreemptionStateHelper();
}

void GpuChannelMessageQueue::PostHandleMessageOnQueue() {
  channel_lock_.AssertAcquired();
  DCHECK(channel_);
  DCHECK(scheduled_);
  DCHECK(!channel_messages_.empty());
  DCHECK(!handle_message_post_task_pending_);
  handle_message_post_task_pending_ = true;
  main_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&GpuChannel::HandleMessageOnQueue, channel_->AsWeakPtr()));
}

const GpuChannelMessage* GpuChannelMessageQueue::BeginMessageProcessing() {
  base::AutoLock auto_lock(channel_lock_);
  DCHECK(channel_);
  DCHECK(scheduled_);
  DCHECK(!channel_messages_.empty());
  handle_message_post_task_pending_ = false;
  // If we have been preempted by another channel, just post a task to wake up.
  if (preempted_flag_ && preempted_flag_->IsSet()) {
    PostHandleMessageOnQueue();
    return nullptr;
  }
  sync_point_order_data_->BeginProcessingOrderNumber(
      channel_messages_.front()->order_number);
  return channel_messages_.front().get();
}

void GpuChannelMessageQueue::PauseMessageProcessing() {
  base::AutoLock auto_lock(channel_lock_);
  DCHECK(!channel_messages_.empty());

  // If we have been preempted by another channel, just post a task to wake up.
  if (scheduled_)
    PostHandleMessageOnQueue();

  sync_point_order_data_->PauseProcessingOrderNumber(
      channel_messages_.front()->order_number);
}

void GpuChannelMessageQueue::FinishMessageProcessing() {
  base::AutoLock auto_lock(channel_lock_);
  DCHECK(!channel_messages_.empty());
  DCHECK(scheduled_);

  sync_point_order_data_->FinishProcessingOrderNumber(
      channel_messages_.front()->order_number);
  channel_messages_.pop_front();

  if (!channel_messages_.empty())
    PostHandleMessageOnQueue();

  if (preempting_flag_) {
    io_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&GpuChannelMessageQueue::UpdatePreemptionState, this));
  }
}

void GpuChannelMessageQueue::UpdatePreemptionState() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  base::AutoLock lock(channel_lock_);
  if (channel_)
    UpdatePreemptionStateHelper();
}

void GpuChannelMessageQueue::UpdatePreemptionStateHelper() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  switch (preemption_state_) {
    case IDLE:
      UpdateStateIdle();
      break;
    case WAITING:
      UpdateStateWaiting();
      break;
    case CHECKING:
      UpdateStateChecking();
      break;
    case PREEMPTING:
      UpdateStatePreempting();
      break;
    case WOULD_PREEMPT_DESCHEDULED:
      UpdateStateWouldPreemptDescheduled();
      break;
    default:
      NOTREACHED();
  }
}

void GpuChannelMessageQueue::UpdateStateIdle() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK(!timer_->IsRunning());
  if (!channel_messages_.empty())
    TransitionToWaiting();
}

void GpuChannelMessageQueue::UpdateStateWaiting() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  // Transition to CHECKING if timer fired.
  if (!timer_->IsRunning())
    TransitionToChecking();
}

void GpuChannelMessageQueue::UpdateStateChecking() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  if (!channel_messages_.empty()) {
    base::TimeTicks time_recv = channel_messages_.front()->time_received;
    base::TimeDelta time_elapsed = base::TimeTicks::Now() - time_recv;
    if (time_elapsed.InMilliseconds() < kPreemptWaitTimeMs) {
      // Schedule another check for when the IPC may go long.
      timer_->Start(
          FROM_HERE,
          base::TimeDelta::FromMilliseconds(kPreemptWaitTimeMs) - time_elapsed,
          this, &GpuChannelMessageQueue::UpdatePreemptionState);
    } else {
      timer_->Stop();
      if (!scheduled_)
        TransitionToWouldPreemptDescheduled();
      else
        TransitionToPreempting();
    }
  }
}

void GpuChannelMessageQueue::UpdateStatePreempting() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  // We should stop preempting if the timer fired or for other conditions.
  if (!timer_->IsRunning() || ShouldTransitionToIdle()) {
    TransitionToIdle();
  } else if (!scheduled_) {
    // Save the remaining preemption time before stopping the timer.
    max_preemption_time_ = timer_->desired_run_time() - base::TimeTicks::Now();
    timer_->Stop();
    TransitionToWouldPreemptDescheduled();
  }
}

void GpuChannelMessageQueue::UpdateStateWouldPreemptDescheduled() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK(!timer_->IsRunning());
  if (ShouldTransitionToIdle()) {
    TransitionToIdle();
  } else if (scheduled_) {
    TransitionToPreempting();
  }
}

bool GpuChannelMessageQueue::ShouldTransitionToIdle() const {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK(preemption_state_ == PREEMPTING ||
         preemption_state_ == WOULD_PREEMPT_DESCHEDULED);
  if (channel_messages_.empty()) {
    return true;
  } else {
    base::TimeTicks next_tick = channel_messages_.front()->time_received;
    base::TimeDelta time_elapsed = base::TimeTicks::Now() - next_tick;
    if (time_elapsed.InMilliseconds() < kStopPreemptThresholdMs)
      return true;
  }
  return false;
}

void GpuChannelMessageQueue::TransitionToIdle() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK(preemption_state_ == PREEMPTING ||
         preemption_state_ == WOULD_PREEMPT_DESCHEDULED);

  preemption_state_ = IDLE;
  preempting_flag_->Reset();

  max_preemption_time_ = base::TimeDelta::FromMilliseconds(kMaxPreemptTimeMs);
  timer_->Stop();

  TRACE_COUNTER_ID1("gpu", "GpuChannel::Preempting", this, 0);

  UpdateStateIdle();
}

void GpuChannelMessageQueue::TransitionToWaiting() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK_EQ(preemption_state_, IDLE);
  DCHECK(!timer_->IsRunning());

  preemption_state_ = WAITING;

  timer_->Start(FROM_HERE,
                base::TimeDelta::FromMilliseconds(kPreemptWaitTimeMs), this,
                &GpuChannelMessageQueue::UpdatePreemptionState);
}

void GpuChannelMessageQueue::TransitionToChecking() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK_EQ(preemption_state_, WAITING);
  DCHECK(!timer_->IsRunning());

  preemption_state_ = CHECKING;

  UpdateStateChecking();
}

void GpuChannelMessageQueue::TransitionToPreempting() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK(preemption_state_ == CHECKING ||
         preemption_state_ == WOULD_PREEMPT_DESCHEDULED);
  DCHECK(scheduled_);

  preemption_state_ = PREEMPTING;
  preempting_flag_->Set();
  TRACE_COUNTER_ID1("gpu", "GpuChannel::Preempting", this, 1);

  DCHECK_LE(max_preemption_time_,
            base::TimeDelta::FromMilliseconds(kMaxPreemptTimeMs));
  timer_->Start(FROM_HERE, max_preemption_time_, this,
                &GpuChannelMessageQueue::UpdatePreemptionState);
}

void GpuChannelMessageQueue::TransitionToWouldPreemptDescheduled() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK(preemption_state_ == CHECKING || preemption_state_ == PREEMPTING);
  DCHECK(!scheduled_);

  preemption_state_ = WOULD_PREEMPT_DESCHEDULED;
  preempting_flag_->Reset();
  TRACE_COUNTER_ID1("gpu", "GpuChannel::Preempting", this, 0);
}

GpuChannelMessageFilter::GpuChannelMessageFilter(
    GpuChannel* gpu_channel,
    Scheduler* scheduler,
    scoped_refptr<GpuChannelMessageQueue> message_queue,
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner)
    : gpu_channel_(gpu_channel),
      scheduler_(scheduler),
      message_queue_(std::move(message_queue)),
      main_task_runner_(std::move(main_task_runner)) {}

GpuChannelMessageFilter::~GpuChannelMessageFilter() {
  DCHECK(!gpu_channel_);
}

void GpuChannelMessageFilter::Destroy() {
  base::AutoLock auto_lock(gpu_channel_lock_);
  gpu_channel_ = nullptr;
}

void GpuChannelMessageFilter::AddRoute(int32_t route_id,
                                       SequenceId sequence_id) {
  base::AutoLock auto_lock(gpu_channel_lock_);
  DCHECK(gpu_channel_);
  DCHECK(scheduler_);
  route_sequences_[route_id] = sequence_id;
}

void GpuChannelMessageFilter::RemoveRoute(int32_t route_id) {
  base::AutoLock auto_lock(gpu_channel_lock_);
  DCHECK(gpu_channel_);
  DCHECK(scheduler_);
  route_sequences_.erase(route_id);
}

void GpuChannelMessageFilter::OnFilterAdded(IPC::Channel* channel) {
  DCHECK(!ipc_channel_);
  ipc_channel_ = channel;
  for (scoped_refptr<IPC::MessageFilter>& filter : channel_filters_)
    filter->OnFilterAdded(ipc_channel_);
}

void GpuChannelMessageFilter::OnFilterRemoved() {
  for (scoped_refptr<IPC::MessageFilter>& filter : channel_filters_)
    filter->OnFilterRemoved();
  ipc_channel_ = nullptr;
  peer_pid_ = base::kNullProcessId;
}

void GpuChannelMessageFilter::OnChannelConnected(int32_t peer_pid) {
  DCHECK(peer_pid_ == base::kNullProcessId);
  peer_pid_ = peer_pid;
  for (scoped_refptr<IPC::MessageFilter>& filter : channel_filters_)
    filter->OnChannelConnected(peer_pid);
}

void GpuChannelMessageFilter::OnChannelError() {
  for (scoped_refptr<IPC::MessageFilter>& filter : channel_filters_)
    filter->OnChannelError();
}

void GpuChannelMessageFilter::OnChannelClosing() {
  for (scoped_refptr<IPC::MessageFilter>& filter : channel_filters_)
    filter->OnChannelClosing();
}

void GpuChannelMessageFilter::AddChannelFilter(
    scoped_refptr<IPC::MessageFilter> filter) {
  channel_filters_.push_back(filter);
  if (ipc_channel_)
    filter->OnFilterAdded(ipc_channel_);
  if (peer_pid_ != base::kNullProcessId)
    filter->OnChannelConnected(peer_pid_);
}

void GpuChannelMessageFilter::RemoveChannelFilter(
    scoped_refptr<IPC::MessageFilter> filter) {
  if (ipc_channel_)
    filter->OnFilterRemoved();
  base::Erase(channel_filters_, filter);
}

bool GpuChannelMessageFilter::OnMessageReceived(const IPC::Message& message) {
  DCHECK(ipc_channel_);

  if (!gpu_channel_)
    return MessageErrorHandler(message, "Channel destroyed");

  if (message.should_unblock() || message.is_reply())
    return MessageErrorHandler(message, "Unexpected message type");

  if (message.type() == GpuChannelMsg_Nop::ID) {
    IPC::Message* reply = IPC::SyncMessage::GenerateReply(&message);
    ipc_channel_->Send(reply);
    return true;
  }

  for (scoped_refptr<IPC::MessageFilter>& filter : channel_filters_) {
    if (filter->OnMessageReceived(message))
      return true;
  }

  base::AutoLock auto_lock(gpu_channel_lock_);
  if (!gpu_channel_)
    return MessageErrorHandler(message, "Channel destroyed");

  if (message.routing_id() == MSG_ROUTING_CONTROL ||
      message.type() == GpuCommandBufferMsg_WaitForTokenInRange::ID ||
      message.type() == GpuCommandBufferMsg_WaitForGetOffsetInRange::ID) {
    // It's OK to post task that may never run even for sync messages, because
    // if the channel is destroyed, the client Send will fail.
    main_task_runner_->PostTask(FROM_HERE,
                                base::Bind(&GpuChannel::HandleOutOfOrderMessage,
                                           gpu_channel_->AsWeakPtr(), message));
  } else if (scheduler_) {
    SequenceId sequence_id = route_sequences_[message.routing_id()];
    if (sequence_id.is_null())
      return MessageErrorHandler(message, "Invalid route");

    std::vector<SyncToken> sync_token_fences;
    if (message.type() == GpuCommandBufferMsg_AsyncFlush::ID) {
      GpuCommandBufferMsg_AsyncFlush::Param params;
      if (!GpuCommandBufferMsg_AsyncFlush::Read(&message, &params))
        return MessageErrorHandler(message, "Invalid flush message");
      sync_token_fences = std::get<3>(params);
    }

    scheduler_->ScheduleTask(sequence_id,
                             base::BindOnce(&GpuChannel::HandleMessage,
                                            gpu_channel_->AsWeakPtr(), message),
                             sync_token_fences);
  } else {
    // Message queue takes care of PostTask.
    message_queue_->PushBackMessage(message);
  }

  return true;
}

bool GpuChannelMessageFilter::MessageErrorHandler(const IPC::Message& message,
                                                  const char* error_msg) {
  DLOG(ERROR) << error_msg;
  if (message.is_sync()) {
    IPC::Message* reply = IPC::SyncMessage::GenerateReply(&message);
    reply->set_reply_error();
    ipc_channel_->Send(reply);
  }
  return true;
}

// Definitions for constructor and destructor of this interface are needed to
// avoid MSVC LNK2019.
FilteredSender::FilteredSender() = default;

FilteredSender::~FilteredSender() = default;

SyncChannelFilteredSender::SyncChannelFilteredSender(
    IPC::ChannelHandle channel_handle,
    IPC::Listener* listener,
    scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
    base::WaitableEvent* shutdown_event)
    : channel_(IPC::SyncChannel::Create(channel_handle,
                                        IPC::Channel::MODE_SERVER,
                                        listener,
                                        ipc_task_runner,
                                        false,
                                        shutdown_event)) {}

SyncChannelFilteredSender::~SyncChannelFilteredSender() = default;

bool SyncChannelFilteredSender::Send(IPC::Message* msg) {
  return channel_->Send(msg);
}

void SyncChannelFilteredSender::AddFilter(IPC::MessageFilter* filter) {
  channel_->AddFilter(filter);
}

void SyncChannelFilteredSender::RemoveFilter(IPC::MessageFilter* filter) {
  channel_->RemoveFilter(filter);
}

GpuChannel::GpuChannel(
    GpuChannelManager* gpu_channel_manager,
    Scheduler* scheduler,
    SyncPointManager* sync_point_manager,
    GpuWatchdogThread* watchdog,
    scoped_refptr<gl::GLShareGroup> share_group,
    scoped_refptr<gles2::MailboxManager> mailbox_manager,
    ServiceDiscardableManager* discardable_manager,
    scoped_refptr<PreemptionFlag> preempting_flag,
    scoped_refptr<PreemptionFlag> preempted_flag,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
    int32_t client_id,
    uint64_t client_tracing_id,
    bool is_gpu_host)
    : gpu_channel_manager_(gpu_channel_manager),
      scheduler_(scheduler),
      sync_point_manager_(sync_point_manager),
      preempting_flag_(preempting_flag),
      preempted_flag_(preempted_flag),
      client_id_(client_id),
      client_tracing_id_(client_tracing_id),
      task_runner_(task_runner),
      io_task_runner_(io_task_runner),
      share_group_(share_group),
      mailbox_manager_(mailbox_manager),
      watchdog_(watchdog),
      discardable_manager_(std::move(discardable_manager)),
      is_gpu_host_(is_gpu_host),
      weak_factory_(this) {
  DCHECK(gpu_channel_manager_);
  DCHECK(client_id_);
  DCHECK(discardable_manager_);

  if (!scheduler_) {
    message_queue_ = new GpuChannelMessageQueue(
        this, sync_point_manager->CreateSyncPointOrderData(), task_runner,
        io_task_runner, preempting_flag, preempted_flag);
  }

  filter_ =
      new GpuChannelMessageFilter(this, scheduler, message_queue_, task_runner);
}

GpuChannel::~GpuChannel() {
  // Clear stubs first because of dependencies.
  stubs_.clear();

  // Destroy filter first so that no message queue gets no more messages.
  filter_->Destroy();

  if (scheduler_) {
    for (const auto& kv : stream_sequences_)
      scheduler_->DestroySequence(kv.second);
  } else {
    message_queue_->Destroy();
  }

  DCHECK(!preempting_flag_ || !preempting_flag_->IsSet());
}

void GpuChannel::Init(std::unique_ptr<FilteredSender> channel) {
  channel_ = std::move(channel);
  channel_->AddFilter(filter_.get());
}

void GpuChannel::SetUnhandledMessageListener(IPC::Listener* listener) {
  unhandled_message_listener_ = listener;
}

base::WeakPtr<GpuChannel> GpuChannel::AsWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

base::ProcessId GpuChannel::GetClientPID() const {
  DCHECK_NE(peer_pid_, base::kNullProcessId);
  return peer_pid_;
}

bool GpuChannel::OnMessageReceived(const IPC::Message& msg) {
  // All messages should be pushed to channel_messages_ and handled separately.
  NOTREACHED();
  return false;
}

void GpuChannel::OnChannelConnected(int32_t peer_pid) {
  peer_pid_ = peer_pid;
}

void GpuChannel::OnChannelError() {
  gpu_channel_manager_->RemoveChannel(client_id_);
}

bool GpuChannel::Send(IPC::Message* message) {
  // The GPU process must never send a synchronous IPC message to the renderer
  // process. This could result in deadlock.
  DCHECK(!message->is_sync());

  DVLOG(1) << "sending message @" << message << " on channel @" << this
           << " with type " << message->type();

  if (!channel_) {
    delete message;
    return false;
  }

  return channel_->Send(message);
}

void GpuChannel::OnCommandBufferScheduled(GpuCommandBufferStub* stub) {
  if (scheduler_) {
    scheduler_->EnableSequence(stub->sequence_id());
  } else {
    message_queue_->SetScheduled(true);
  }
}

void GpuChannel::OnCommandBufferDescheduled(GpuCommandBufferStub* stub) {
  if (scheduler_) {
    scheduler_->DisableSequence(stub->sequence_id());
  } else {
    message_queue_->SetScheduled(false);
  }
}

GpuCommandBufferStub* GpuChannel::LookupCommandBuffer(int32_t route_id) {
  auto it = stubs_.find(route_id);
  if (it == stubs_.end())
    return nullptr;

  return it->second.get();
}

void GpuChannel::LoseAllContexts() {
  gpu_channel_manager_->LoseAllContexts();
}

void GpuChannel::MarkAllContextsLost() {
  for (auto& kv : stubs_)
    kv.second->MarkContextLost();
}

bool GpuChannel::AddRoute(int32_t route_id,
                          SequenceId sequence_id,
                          IPC::Listener* listener) {
  if (scheduler_)
    filter_->AddRoute(route_id, sequence_id);
  return router_.AddRoute(route_id, listener);
}

void GpuChannel::RemoveRoute(int32_t route_id) {
  if (scheduler_)
    filter_->RemoveRoute(route_id);
  router_.RemoveRoute(route_id);
}

bool GpuChannel::OnControlMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(GpuChannel, msg)
    IPC_MESSAGE_HANDLER(GpuChannelMsg_CreateCommandBuffer,
                        OnCreateCommandBuffer)
    IPC_MESSAGE_HANDLER(GpuChannelMsg_DestroyCommandBuffer,
                        OnDestroyCommandBuffer)
    IPC_MESSAGE_HANDLER(GpuChannelMsg_GetDriverBugWorkArounds,
                        OnGetDriverBugWorkArounds)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void GpuChannel::HandleMessage(const IPC::Message& msg) {
  int32_t routing_id = msg.routing_id();
  GpuCommandBufferStub* stub = LookupCommandBuffer(routing_id);

  DCHECK(!stub || stub->IsScheduled());

  DVLOG(1) << "received message @" << &msg << " on channel @" << this
           << " with type " << msg.type();

  HandleMessageHelper(msg);

  // If we get descheduled or yield while processing a message.
  if (stub && (stub->HasUnprocessedCommands() || !stub->IsScheduled())) {
    DCHECK((uint32_t)GpuCommandBufferMsg_AsyncFlush::ID == msg.type() ||
           (uint32_t)GpuCommandBufferMsg_WaitSyncToken::ID == msg.type());
    scheduler_->ContinueTask(
        stub->sequence_id(),
        base::BindOnce(&GpuChannel::HandleMessage, AsWeakPtr(), msg));
  }
}

void GpuChannel::HandleMessageOnQueue() {
  const GpuChannelMessage* channel_msg =
      message_queue_->BeginMessageProcessing();
  if (!channel_msg)
    return;

  const IPC::Message& msg = channel_msg->message;
  int32_t routing_id = msg.routing_id();
  GpuCommandBufferStub* stub = LookupCommandBuffer(routing_id);

  DCHECK(!stub || stub->IsScheduled());

  DVLOG(1) << "received message @" << &msg << " on channel @" << this
           << " with type " << msg.type();

  HandleMessageHelper(msg);

  // If we get descheduled or yield while processing a message.
  if (stub && (stub->HasUnprocessedCommands() || !stub->IsScheduled())) {
    DCHECK((uint32_t)GpuCommandBufferMsg_AsyncFlush::ID == msg.type() ||
           (uint32_t)GpuCommandBufferMsg_WaitSyncToken::ID == msg.type());
    DCHECK_EQ(stub->IsScheduled(), message_queue_->IsScheduled());
    message_queue_->PauseMessageProcessing();
  } else {
    message_queue_->FinishMessageProcessing();
  }
}

void GpuChannel::HandleMessageForTesting(const IPC::Message& msg) {
  // Message filter gets message first on IO thread.
  filter_->OnMessageReceived(msg);
}

void GpuChannel::HandleMessageHelper(const IPC::Message& msg) {
  int32_t routing_id = msg.routing_id();

  bool handled = false;
  if (routing_id == MSG_ROUTING_CONTROL) {
    handled = OnControlMessageReceived(msg);
  } else {
    handled = router_.RouteMessage(msg);
  }

  if (!handled && unhandled_message_listener_)
    handled = unhandled_message_listener_->OnMessageReceived(msg);

  // Respond to sync messages even if router failed to route.
  if (!handled && msg.is_sync()) {
    IPC::Message* reply = IPC::SyncMessage::GenerateReply(&msg);
    reply->set_reply_error();
    Send(reply);
  }
}

void GpuChannel::HandleOutOfOrderMessage(const IPC::Message& msg) {
  HandleMessageHelper(msg);
}

#if defined(OS_ANDROID)
const GpuCommandBufferStub* GpuChannel::GetOneStub() const {
  for (const auto& kv : stubs_) {
    const GpuCommandBufferStub* stub = kv.second.get();
    if (stub->decoder() && !stub->decoder()->WasContextLost())
      return stub;
  }
  return nullptr;
}
#endif

void GpuChannel::OnCreateCommandBuffer(
    const GPUCreateCommandBufferConfig& init_params,
    int32_t route_id,
    base::SharedMemoryHandle shared_state_handle,
    bool* result,
    gpu::Capabilities* capabilities) {
  TRACE_EVENT2("gpu", "GpuChannel::OnCreateCommandBuffer", "route_id", route_id,
               "offscreen", (init_params.surface_handle == kNullSurfaceHandle));
  std::unique_ptr<base::SharedMemory> shared_state_shm(
      new base::SharedMemory(shared_state_handle, false));
  std::unique_ptr<GpuCommandBufferStub> stub =
      CreateCommandBuffer(init_params, route_id, std::move(shared_state_shm));
  if (stub) {
    *result = true;
    *capabilities = stub->decoder()->GetCapabilities();
    stubs_[route_id] = std::move(stub);
  } else {
    *result = false;
    *capabilities = gpu::Capabilities();
  }
}

std::unique_ptr<GpuCommandBufferStub> GpuChannel::CreateCommandBuffer(
    const GPUCreateCommandBufferConfig& init_params,
    int32_t route_id,
    std::unique_ptr<base::SharedMemory> shared_state_shm) {
  if (init_params.surface_handle != kNullSurfaceHandle && !is_gpu_host_) {
    DLOG(ERROR) << "GpuChannel::CreateCommandBuffer(): attempt to create a "
                   "view context on a non-privileged channel";
    return nullptr;
  }

  int32_t share_group_id = init_params.share_group_id;
  GpuCommandBufferStub* share_group = LookupCommandBuffer(share_group_id);

  if (!share_group && share_group_id != MSG_ROUTING_NONE) {
    DLOG(ERROR) << "GpuChannel::CreateCommandBuffer(): invalid share group id";
    return nullptr;
  }

  int32_t stream_id = init_params.stream_id;
  if (share_group && stream_id != share_group->stream_id()) {
    DLOG(ERROR) << "GpuChannel::CreateCommandBuffer(): stream id does not "
                   "match share group stream id";
    return nullptr;
  }

  SchedulingPriority stream_priority = init_params.stream_priority;
  if (stream_priority <= SchedulingPriority::kHigh && !is_gpu_host_) {
    DLOG(ERROR) << "GpuChannel::CreateCommandBuffer(): high priority stream "
                   "not allowed on a non-privileged channel";
    return nullptr;
  }

  if (share_group && !share_group->decoder()) {
    // This should catch test errors where we did not Initialize the
    // share_group's CommandBuffer.
    DLOG(ERROR) << "GpuChannel::CreateCommandBuffer(): shared context was "
                   "not initialized";
    return nullptr;
  }

  if (share_group && share_group->decoder()->WasContextLost()) {
    DLOG(ERROR) << "GpuChannel::CreateCommandBuffer(): shared context was "
                   "already lost";
    return nullptr;
  }

  CommandBufferId command_buffer_id =
      GenerateCommandBufferId(client_id_, route_id);

  SequenceId sequence_id;
  if (scheduler_) {
    sequence_id = stream_sequences_[stream_id];
    if (sequence_id.is_null()) {
      sequence_id = scheduler_->CreateSequence(stream_priority);
      stream_sequences_[stream_id] = sequence_id;
    }
  } else {
    sequence_id = message_queue_->sequence_id();
  }

  std::unique_ptr<GpuCommandBufferStub> stub(GpuCommandBufferStub::Create(
      this, share_group, init_params, command_buffer_id, sequence_id, stream_id,
      route_id, std::move(shared_state_shm)));

  if (!AddRoute(route_id, sequence_id, stub.get())) {
    DLOG(ERROR) << "GpuChannel::CreateCommandBuffer(): failed to add route";
    return nullptr;
  }

  return stub;
}

void GpuChannel::OnDestroyCommandBuffer(int32_t route_id) {
  TRACE_EVENT1("gpu", "GpuChannel::OnDestroyCommandBuffer", "route_id",
               route_id);

  std::unique_ptr<GpuCommandBufferStub> stub;
  auto it = stubs_.find(route_id);
  if (it != stubs_.end()) {
    stub = std::move(it->second);
    stubs_.erase(it);
  }
  // In case the renderer is currently blocked waiting for a sync reply from the
  // stub, we need to make sure to reschedule the correct stream here.
  if (stub && !stub->IsScheduled()) {
    // This stub won't get a chance to be scheduled so do that now.
    OnCommandBufferScheduled(stub.get());
  }

  RemoveRoute(route_id);
}

void GpuChannel::OnGetDriverBugWorkArounds(
    std::vector<std::string>* gpu_driver_bug_workarounds) {
  gpu_driver_bug_workarounds->clear();
#define GPU_OP(type, name)                                     \
  if (gpu_channel_manager_->gpu_driver_bug_workarounds().name) \
    gpu_driver_bug_workarounds->push_back(#name);
  GPU_DRIVER_BUG_WORKAROUNDS(GPU_OP)
#undef GPU_OP
}

void GpuChannel::CacheShader(const std::string& key,
                             const std::string& shader) {
  gpu_channel_manager_->delegate()->StoreShaderToDisk(client_id_, key, shader);
}

void GpuChannel::AddFilter(IPC::MessageFilter* filter) {
  io_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&GpuChannelMessageFilter::AddChannelFilter, filter_, filter));
}

void GpuChannel::RemoveFilter(IPC::MessageFilter* filter) {
  io_task_runner_->PostTask(
      FROM_HERE, base::Bind(&GpuChannelMessageFilter::RemoveChannelFilter,
                            filter_, filter));
}

uint64_t GpuChannel::GetMemoryUsage() {
  // Collect the unique memory trackers in use by the |stubs_|.
  std::set<gles2::MemoryTracker*> unique_memory_trackers;
  for (auto& kv : stubs_)
    unique_memory_trackers.insert(kv.second->GetMemoryTracker());

  // Sum the memory usage for all unique memory trackers.
  uint64_t size = 0;
  for (auto* tracker : unique_memory_trackers) {
    size += gpu_channel_manager()->gpu_memory_manager()->GetTrackerMemoryUsage(
        tracker);
  }

  return size;
}

scoped_refptr<gl::GLImage> GpuChannel::CreateImageForGpuMemoryBuffer(
    const gfx::GpuMemoryBufferHandle& handle,
    const gfx::Size& size,
    gfx::BufferFormat format,
    uint32_t internalformat,
    SurfaceHandle surface_handle) {
  switch (handle.type) {
    case gfx::SHARED_MEMORY_BUFFER: {
      if (!base::IsValueInRangeForNumericType<size_t>(handle.stride))
        return nullptr;
      scoped_refptr<gl::GLImageSharedMemory> image(
          new gl::GLImageSharedMemory(size, internalformat));
      if (!image->Initialize(handle.handle, handle.id, format, handle.offset,
                             handle.stride)) {
        return nullptr;
      }

      return image;
    }
    default: {
      GpuChannelManager* manager = gpu_channel_manager();
      if (!manager->gpu_memory_buffer_factory())
        return nullptr;

      return manager->gpu_memory_buffer_factory()
          ->AsImageFactory()
          ->CreateImageForGpuMemoryBuffer(handle, size, format, internalformat,
                                          client_id_, surface_handle);
    }
  }
}

}  // namespace gpu
