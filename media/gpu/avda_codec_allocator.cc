// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/avda_codec_allocator.h"

#include <stddef.h>

#include <memory>

#include "base/logging.h"
#include "base/single_thread_task_runner.h"
#include "base/sys_info.h"
#include "base/task_runner_util.h"
#include "base/threading/thread.h"
#include "base/threading/thread_checker.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/default_tick_clock.h"
#include "base/trace_event/trace_event.h"
#include "media/base/android/media_codec_bridge_impl.h"
#include "media/base/limits.h"
#include "media/base/media.h"
#include "media/base/timestamp_constants.h"
#include "media/gpu/android_video_decode_accelerator.h"

namespace media {

namespace {

// Give tasks 800ms before considering them hung. MediaCodec.configure() calls
// typically take 100-200ms on a N5, so 800ms is expected to very rarely result
// in false positives. Also, false positives have low impact because we resume
// using the thread when the task completes.
constexpr base::TimeDelta kHungTaskDetectionTimeout =
    base::TimeDelta::FromMilliseconds(800);

// This must be safe to call on any thread. Returns nullptr on failure.
std::unique_ptr<MediaCodecBridge> CreateMediaCodecInternal(
    scoped_refptr<CodecConfig> codec_config,
    bool require_software_codec) {
  TRACE_EVENT0("media", "CreateMediaCodecInternal");

  jobject media_crypto =
      codec_config->media_crypto ? codec_config->media_crypto->obj() : nullptr;

  // |needs_protected_surface| implies that it's an encrypted stream.
  DCHECK(!codec_config->needs_protected_surface || media_crypto);

  std::unique_ptr<MediaCodecBridge> codec(
      MediaCodecBridgeImpl::CreateVideoDecoder(
          codec_config->codec, codec_config->needs_protected_surface,
          codec_config->initial_expected_coded_size,
          codec_config->surface_bundle->GetJavaSurface().obj(), media_crypto,
          codec_config->csd0, codec_config->csd1, true,
          require_software_codec));

  return codec;
}

// Delete |codec| and signal |done_event| if it's not null.
void DeleteMediaCodecAndSignal(std::unique_ptr<MediaCodecBridge> codec,
                               base::WaitableEvent* done_event) {
  codec.reset();
  if (done_event)
    done_event->Signal();
}

void DropReferenceToSurfaceBundle(
    scoped_refptr<AVDASurfaceBundle> surface_bundle) {
  // Do nothing.  Let |surface_bundle| go out of scope.
}

}  // namespace

CodecConfig::CodecConfig() {}
CodecConfig::~CodecConfig() {}

AVDACodecAllocator::HangDetector::HangDetector(base::TickClock* tick_clock)
    : tick_clock_(tick_clock) {}

void AVDACodecAllocator::HangDetector::WillProcessTask(
    const base::PendingTask& pending_task) {
  base::AutoLock l(lock_);
  task_start_time_ = tick_clock_->NowTicks();
}

void AVDACodecAllocator::HangDetector::DidProcessTask(
    const base::PendingTask& pending_task) {
  base::AutoLock l(lock_);
  task_start_time_ = base::TimeTicks();
}

bool AVDACodecAllocator::HangDetector::IsThreadLikelyHung() {
  base::AutoLock l(lock_);
  if (task_start_time_.is_null())
    return false;

  return (tick_clock_->NowTicks() - task_start_time_) >
         kHungTaskDetectionTimeout;
}

// static
AVDACodecAllocator* AVDACodecAllocator::GetInstance() {
  static AVDACodecAllocator* allocator = new AVDACodecAllocator();
  return allocator;
}

bool AVDACodecAllocator::StartThread(AVDACodecAllocatorClient* client) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // Cancel any pending StopThreadTask()s because we need the threads now.
  weak_this_factory_.InvalidateWeakPtrs();

  // Try to start the threads if they haven't been started.
  for (auto* thread : threads_) {
    if (thread->thread.IsRunning())
      continue;

    if (!thread->thread.Start())
      return false;

    // Register the hang detector to observe the thread's MessageLoop.
    thread->thread.task_runner()->PostTask(
        FROM_HERE, base::Bind(&base::MessageLoop::AddTaskObserver,
                              base::Unretained(thread->thread.message_loop()),
                              &thread->hang_detector));
  }

  clients_.insert(client);
  return true;
}

void AVDACodecAllocator::StopThread(AVDACodecAllocatorClient* client) {
  DCHECK(thread_checker_.CalledOnValidThread());

  clients_.erase(client);
  if (!clients_.empty()) {
    // If we aren't stopping, then signal immediately.
    if (stop_event_for_testing_)
      stop_event_for_testing_->Signal();
    return;
  }

  // Post a task to stop each thread through its task runner and back to this
  // thread. This ensures that all pending tasks are run first. If a new AVDA
  // calls StartThread() before StopThreadTask() runs, it's canceled by
  // invalidating its weak pointer. As a result we're guaranteed to only call
  // Thread::Stop() while there are no tasks on its queue. We don't try to stop
  // hung threads. But if it recovers it will be stopped the next time a client
  // calls this.
  for (size_t i = 0; i < threads_.size(); i++) {
    if (threads_[i]->thread.IsRunning() &&
        !threads_[i]->hang_detector.IsThreadLikelyHung()) {
      threads_[i]->thread.task_runner()->PostTaskAndReply(
          FROM_HERE, base::Bind(&base::DoNothing),
          base::Bind(&AVDACodecAllocator::StopThreadTask,
                     weak_this_factory_.GetWeakPtr(), i));
    }
  }
}

// Return the task runner for tasks of type |type|.
scoped_refptr<base::SingleThreadTaskRunner> AVDACodecAllocator::TaskRunnerFor(
    TaskType task_type) {
  DCHECK(thread_checker_.CalledOnValidThread());
  return threads_[task_type]->thread.task_runner();
}

std::unique_ptr<MediaCodecBridge> AVDACodecAllocator::CreateMediaCodecSync(
    scoped_refptr<CodecConfig> codec_config) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto task_type =
      TaskTypeForAllocation(codec_config->software_codec_forbidden);
  if (!task_type)
    return nullptr;

  auto codec = CreateMediaCodecInternal(codec_config, task_type == SW_CODEC);
  if (codec)
    codec_task_types_[codec.get()] = *task_type;
  return codec;
}

void AVDACodecAllocator::CreateMediaCodecAsync(
    base::WeakPtr<AVDACodecAllocatorClient> client,
    scoped_refptr<CodecConfig> codec_config) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto task_type =
      TaskTypeForAllocation(codec_config->software_codec_forbidden);
  if (!task_type) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&AVDACodecAllocatorClient::OnCodecConfigured,
                              client, nullptr));
    return;
  }

  // Allocate the codec on the appropriate thread, and reply to this one with
  // the result.  If |client| is gone by then, we handle cleanup.
  base::PostTaskAndReplyWithResult(
      TaskRunnerFor(*task_type).get(), FROM_HERE,
      base::Bind(&CreateMediaCodecInternal, codec_config,
                 task_type == SW_CODEC),
      base::Bind(&AVDACodecAllocator::ForwardOrDropCodec,
                 base::Unretained(this), client, *task_type,
                 codec_config->surface_bundle));
}

void AVDACodecAllocator::ForwardOrDropCodec(
    base::WeakPtr<AVDACodecAllocatorClient> client,
    TaskType task_type,
    scoped_refptr<AVDASurfaceBundle> surface_bundle,
    std::unique_ptr<MediaCodecBridge> media_codec) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (media_codec)
    codec_task_types_[media_codec.get()] = task_type;

  if (!client) {
    // |client| has been destroyed.  Free |media_codec| on the right thread.
    // Note that this also preserves |surface_bundle| until |media_codec| has
    // been released, in case our ref to it is the last one.
    if (media_codec)
      ReleaseMediaCodec(std::move(media_codec), surface_bundle);
    return;
  }

  client->OnCodecConfigured(std::move(media_codec));
}

void AVDACodecAllocator::ReleaseMediaCodec(
    std::unique_ptr<MediaCodecBridge> media_codec,
    scoped_refptr<AVDASurfaceBundle> surface_bundle) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(media_codec);

  auto task_type = codec_task_types_[media_codec.get()];
  int erased = codec_task_types_.erase(media_codec.get());
  DCHECK(erased);

  // No need to track the release if it's a SurfaceTexture.  We still forward
  // the reference to |surface_bundle|, though, so that the SurfaceTexture
  // lasts at least as long as the codec.
  if (!surface_bundle->overlay) {
    TaskRunnerFor(task_type)->PostTaskAndReply(
        FROM_HERE,
        base::Bind(&DeleteMediaCodecAndSignal,
                   base::Passed(std::move(media_codec)), nullptr),
        base::Bind(&DropReferenceToSurfaceBundle, surface_bundle));
    return;
  }

  DCHECK(!surface_bundle->surface_texture);
  pending_codec_releases_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(surface_bundle->overlay.get()),
      std::forward_as_tuple(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED));
  base::WaitableEvent* released =
      &pending_codec_releases_.find(surface_bundle->overlay.get())->second;

  // Note that we forward |surface_bundle|, too, so that the surface outlasts
  // the codec.  This doesn't matter so much for CVV surfaces, since they don't
  // auto-release when they're dropped.  However, for surface owners, this will
  // become important, so we still handle it.  Plus, it makes sense.
  TaskRunnerFor(task_type)->PostTaskAndReply(
      FROM_HERE,
      base::Bind(&DeleteMediaCodecAndSignal,
                 base::Passed(std::move(media_codec)), released),
      base::Bind(&AVDACodecAllocator::OnMediaCodecReleased,
                 base::Unretained(this), surface_bundle));
}

void AVDACodecAllocator::OnMediaCodecReleased(
    scoped_refptr<AVDASurfaceBundle> surface_bundle) {
  DCHECK(thread_checker_.CalledOnValidThread());

  pending_codec_releases_.erase(surface_bundle->overlay.get());

  // Also note that |surface_bundle| lasted at least as long as the codec.
}

bool AVDACodecAllocator::IsAnyRegisteredAVDA() {
  return !clients_.empty();
}

base::Optional<TaskType> AVDACodecAllocator::TaskTypeForAllocation(
    bool software_codec_forbidden) {
  if (!threads_[AUTO_CODEC]->hang_detector.IsThreadLikelyHung())
    return AUTO_CODEC;

  if (!threads_[SW_CODEC]->hang_detector.IsThreadLikelyHung() &&
      !software_codec_forbidden) {
    return SW_CODEC;
  }

  return base::nullopt;
}

base::Thread& AVDACodecAllocator::GetThreadForTesting(TaskType task_type) {
  return threads_[task_type]->thread;
}

bool AVDACodecAllocator::WaitForPendingRelease(AndroidOverlay* overlay) {
  if (!pending_codec_releases_.count(overlay))
    return true;

  // The codec is being released so we have to wait for it here. It's a
  // TimedWait() because the MediaCodec release may hang due to framework bugs.
  // And in that case we don't want to hang the browser UI thread. Android ANRs
  // occur when the UI thread is blocked for 5 seconds, so waiting for 2 seconds
  // gives us leeway to avoid an ANR. Verified no ANR on a Nexus 7.
  base::WaitableEvent& released = pending_codec_releases_.find(overlay)->second;
  released.TimedWait(base::TimeDelta::FromSeconds(2));
  if (released.IsSignaled())
    return true;

  DLOG(WARNING) << __func__ << ": timed out waiting for MediaCodec#release()";
  return false;
}

AVDACodecAllocator::AVDACodecAllocator(base::TickClock* tick_clock,
                                       base::WaitableEvent* stop_event)
    : stop_event_for_testing_(stop_event), weak_this_factory_(this) {
  // We leak the clock we create, but that's okay because we're a singleton.
  auto* clock = tick_clock ? tick_clock : new base::DefaultTickClock();

  // Create threads with names and indices that match up with TaskType.
  threads_.push_back(new ThreadAndHangDetector("AVDAAutoThread", clock));
  threads_.push_back(new ThreadAndHangDetector("AVDASWThread", clock));
  static_assert(AUTO_CODEC == 0 && SW_CODEC == 1,
                "TaskType values are not ordered correctly.");
}

AVDACodecAllocator::~AVDACodecAllocator() {
  // Only tests should reach here.  Shut down threads so that we guarantee that
  // nothing will use the threads.
  for (auto* thread : threads_)
    thread->thread.Stop();
}

void AVDACodecAllocator::StopThreadTask(size_t index) {
  threads_[index]->thread.Stop();
  // Signal the stop event after both threads are stopped.
  if (stop_event_for_testing_ && !threads_[AUTO_CODEC]->thread.IsRunning() &&
      !threads_[SW_CODEC]->thread.IsRunning()) {
    stop_event_for_testing_->Signal();
  }
}

}  // namespace media
