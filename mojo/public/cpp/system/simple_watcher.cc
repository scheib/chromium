// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/system/simple_watcher.h"

#include "base/bind.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/lock.h"
#include "base/trace_event/heap_profiler.h"
#include "mojo/public/c/system/watcher.h"

namespace mojo {

// Thread-safe Context object used to dispatch watch notifications from a
// arbitrary threads.
class SimpleWatcher::Context : public base::RefCountedThreadSafe<Context> {
 public:
  // Creates a |Context| instance for a new watch on |watcher|, to watch
  // |handle| for |signals|.
  static scoped_refptr<Context> Create(
      base::WeakPtr<SimpleWatcher> watcher,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      WatcherHandle watcher_handle,
      Handle handle,
      MojoHandleSignals signals,
      int watch_id,
      MojoResult* watch_result) {
    scoped_refptr<Context> context =
        new Context(watcher, task_runner, watch_id);

    // If MojoWatch succeeds, it assumes ownership of a reference to |context|.
    // In that case, this reference is balanced in CallNotify() when |result| is
    // |MOJO_RESULT_CANCELLED|.
    context->AddRef();

    *watch_result = MojoWatch(watcher_handle.value(), handle.value(), signals,
                              context->value());
    if (*watch_result != MOJO_RESULT_OK) {
      // Balanced by the AddRef() above since watching failed.
      context->Release();
      return nullptr;
    }

    return context;
  }

  static void CallNotify(uintptr_t context_value,
                         MojoResult result,
                         MojoHandleSignalsState signals_state,
                         MojoWatcherNotificationFlags flags) {
    auto* context = reinterpret_cast<Context*>(context_value);
    context->Notify(result, signals_state, flags);

    // That was the last notification for the context. We can release the ref
    // owned by the watch, which may in turn delete the Context.
    if (result == MOJO_RESULT_CANCELLED)
      context->Release();
  }

  uintptr_t value() const { return reinterpret_cast<uintptr_t>(this); }

  void DisableCancellationNotifications() {
    base::AutoLock lock(lock_);
    enable_cancellation_notifications_ = false;
  }

 private:
  friend class base::RefCountedThreadSafe<Context>;

  Context(base::WeakPtr<SimpleWatcher> weak_watcher,
          scoped_refptr<base::SingleThreadTaskRunner> task_runner,
          int watch_id)
      : weak_watcher_(weak_watcher),
        task_runner_(task_runner),
        watch_id_(watch_id) {}
  ~Context() {}

  void Notify(MojoResult result,
              MojoHandleSignalsState signals_state,
              MojoWatcherNotificationFlags flags) {
    if (result == MOJO_RESULT_CANCELLED) {
      // The SimpleWatcher may have explicitly cancelled this watch, so we don't
      // bother dispatching the notification - it would be ignored anyway.
      //
      // TODO(rockot): This shouldn't really be necessary, but there are already
      // instances today where bindings object may be bound and subsequently
      // closed due to pipe error, all before the thread's TaskRunner has been
      // properly initialized.
      base::AutoLock lock(lock_);
      if (!enable_cancellation_notifications_)
        return;
    }

    if ((flags & MOJO_WATCHER_NOTIFICATION_FLAG_FROM_SYSTEM) &&
        task_runner_->RunsTasksOnCurrentThread() && weak_watcher_ &&
        weak_watcher_->is_default_task_runner_) {
      // System notifications will trigger from the task runner passed to
      // mojo::edk::ScopedIPCSupport. In Chrome this happens to always be the
      // default task runner for the IO thread.
      weak_watcher_->OnHandleReady(watch_id_, result);
    } else {
      task_runner_->PostTask(
          FROM_HERE, base::Bind(&SimpleWatcher::OnHandleReady, weak_watcher_,
                                watch_id_, result));
    }
  }

  const base::WeakPtr<SimpleWatcher> weak_watcher_;
  const scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  const int watch_id_;

  base::Lock lock_;
  bool enable_cancellation_notifications_ = true;

  DISALLOW_COPY_AND_ASSIGN(Context);
};

SimpleWatcher::SimpleWatcher(const tracked_objects::Location& from_here,
                             ArmingPolicy arming_policy,
                             scoped_refptr<base::SingleThreadTaskRunner> runner)
    : arming_policy_(arming_policy),
      task_runner_(std::move(runner)),
      is_default_task_runner_(task_runner_ ==
                              base::ThreadTaskRunnerHandle::Get()),
      heap_profiler_tag_(from_here.file_name()),
      weak_factory_(this) {
  MojoResult rv = CreateWatcher(&Context::CallNotify, &watcher_handle_);
  DCHECK_EQ(MOJO_RESULT_OK, rv);
  DCHECK(task_runner_->BelongsToCurrentThread());
}

SimpleWatcher::~SimpleWatcher() {
  if (IsWatching())
    Cancel();
}

bool SimpleWatcher::IsWatching() const {
  DCHECK(thread_checker_.CalledOnValidThread());
  return context_ != nullptr;
}

MojoResult SimpleWatcher::Watch(Handle handle,
                                MojoHandleSignals signals,
                                const ReadyCallback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!IsWatching());
  DCHECK(!callback.is_null());

  callback_ = callback;
  handle_ = handle;
  watch_id_ += 1;

  MojoResult watch_result = MOJO_RESULT_UNKNOWN;
  context_ = Context::Create(weak_factory_.GetWeakPtr(), task_runner_,
                             watcher_handle_.get(), handle_, signals, watch_id_,
                             &watch_result);
  if (!context_) {
    handle_.set_value(kInvalidHandleValue);
    callback_.Reset();
    DCHECK_EQ(MOJO_RESULT_INVALID_ARGUMENT, watch_result);
    return watch_result;
  }

  if (arming_policy_ == ArmingPolicy::AUTOMATIC)
    ArmOrNotify();

  return MOJO_RESULT_OK;
}

void SimpleWatcher::Cancel() {
  DCHECK(thread_checker_.CalledOnValidThread());

  // The watcher may have already been cancelled if the handle was closed.
  if (!context_)
    return;

  // Prevent the cancellation notification from being dispatched to
  // OnHandleReady() when cancellation is explicit. See the note in the
  // implementation of DisableCancellationNotifications() above.
  context_->DisableCancellationNotifications();

  handle_.set_value(kInvalidHandleValue);
  callback_.Reset();

  // Ensure |context_| is unset by the time we call MojoCancelWatch, as may
  // re-enter the notification callback and we want to ensure |context_| is
  // unset by then.
  scoped_refptr<Context> context;
  std::swap(context, context_);
  MojoResult rv =
      MojoCancelWatch(watcher_handle_.get().value(), context->value());

  // It's possible this cancellation could race with a handle closure
  // notification, in which case the watch may have already been implicitly
  // cancelled.
  DCHECK(rv == MOJO_RESULT_OK || rv == MOJO_RESULT_NOT_FOUND);
}

MojoResult SimpleWatcher::Arm(MojoResult* ready_result) {
  DCHECK(thread_checker_.CalledOnValidThread());
  uint32_t num_ready_contexts = 1;
  uintptr_t ready_context;
  MojoResult local_ready_result;
  MojoHandleSignalsState ready_state;
  MojoResult rv =
      MojoArmWatcher(watcher_handle_.get().value(), &num_ready_contexts,
                     &ready_context, &local_ready_result, &ready_state);
  if (rv == MOJO_RESULT_FAILED_PRECONDITION) {
    DCHECK(context_);
    DCHECK_EQ(1u, num_ready_contexts);
    DCHECK_EQ(context_->value(), ready_context);
    if (ready_result)
      *ready_result = local_ready_result;
  }

  return rv;
}

void SimpleWatcher::ArmOrNotify() {
  DCHECK(thread_checker_.CalledOnValidThread());

  // Already cancelled, nothing to do.
  if (!IsWatching())
    return;

  MojoResult ready_result;
  MojoResult rv = Arm(&ready_result);
  if (rv == MOJO_RESULT_OK)
    return;

  DCHECK_EQ(MOJO_RESULT_FAILED_PRECONDITION, rv);
  task_runner_->PostTask(FROM_HERE, base::Bind(&SimpleWatcher::OnHandleReady,
                                               weak_factory_.GetWeakPtr(),
                                               watch_id_, ready_result));
}

void SimpleWatcher::OnHandleReady(int watch_id, MojoResult result) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // This notification may be for a previously watched context, in which case
  // we just ignore it.
  if (watch_id != watch_id_)
    return;

  ReadyCallback callback = callback_;
  if (result == MOJO_RESULT_CANCELLED) {
    // Implicit cancellation due to someone closing the watched handle. We clear
    // the SimppleWatcher's state before dispatching this.
    context_ = nullptr;
    handle_.set_value(kInvalidHandleValue);
    callback_.Reset();
  }

  // NOTE: It's legal for |callback| to delete |this|.
  if (!callback.is_null()) {
    TRACE_HEAP_PROFILER_API_SCOPED_TASK_EXECUTION event(heap_profiler_tag_);

    base::WeakPtr<SimpleWatcher> weak_self = weak_factory_.GetWeakPtr();
    callback.Run(result);
    if (!weak_self)
      return;

    if (unsatisfiable_)
      return;

    // Prevent |MOJO_RESULT_FAILED_PRECONDITION| task spam by only notifying
    // at most once in AUTOMATIC arming mode.
    if (result == MOJO_RESULT_FAILED_PRECONDITION)
      unsatisfiable_ = true;

    if (arming_policy_ == ArmingPolicy::AUTOMATIC && IsWatching())
      ArmOrNotify();
  }
}

}  // namespace mojo
