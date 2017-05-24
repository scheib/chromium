// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/mojo/MojoWatcher.h"

#include "bindings/core/v8/MojoWatchCallback.h"
#include "core/dom/ExecutionContext.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/mojo/MojoHandleSignals.h"
#include "platform/CrossThreadFunctional.h"
#include "platform/WebTaskRunner.h"
#include "platform/bindings/ScriptState.h"

namespace blink {

static void RunWatchCallback(MojoWatchCallback* callback,
                             ScriptWrappable* wrappable,
                             MojoResult result) {
  callback->call(wrappable, result);
}

// static
MojoWatcher* MojoWatcher::Create(mojo::Handle handle,
                                 const MojoHandleSignals& signals_dict,
                                 MojoWatchCallback* callback,
                                 ExecutionContext* context) {
  MojoWatcher* watcher = new MojoWatcher(context, callback);
  MojoResult result = watcher->Watch(handle, signals_dict);
  // TODO(alokp): Consider raising an exception.
  // Current clients expect to recieve the initial error returned by MojoWatch
  // via watch callback.
  //
  // Note that the usage of wrapPersistent is intentional so that the intial
  // error is guaranteed to be reported to the client in case where the given
  // handle is invalid and garbage collection happens before the callback
  // is scheduled.
  if (result != MOJO_RESULT_OK) {
    watcher->task_runner_->PostTask(
        BLINK_FROM_HERE, WTF::Bind(&RunWatchCallback, WrapPersistent(callback),
                                   WrapPersistent(watcher), result));
  }
  return watcher;
}

MojoWatcher::~MojoWatcher() {
  DCHECK(!handle_.is_valid());
}

MojoResult MojoWatcher::cancel() {
  if (!watcher_handle_.is_valid())
    return MOJO_RESULT_INVALID_ARGUMENT;

  watcher_handle_.reset();
  return MOJO_RESULT_OK;
}

DEFINE_TRACE(MojoWatcher) {
  visitor->Trace(callback_);
  ContextLifecycleObserver::Trace(visitor);
}

DEFINE_TRACE_WRAPPERS(MojoWatcher) {
  visitor->TraceWrappers(callback_);
}

bool MojoWatcher::HasPendingActivity() const {
  return handle_.is_valid();
}

void MojoWatcher::ContextDestroyed(ExecutionContext*) {
  cancel();
}

MojoWatcher::MojoWatcher(ExecutionContext* context, MojoWatchCallback* callback)
    : ContextLifecycleObserver(context),
      task_runner_(TaskRunnerHelper::Get(TaskType::kUnspecedTimer, context)),
      callback_(this, callback) {}

MojoResult MojoWatcher::Watch(mojo::Handle handle,
                              const MojoHandleSignals& signals_dict) {
  ::MojoHandleSignals signals = MOJO_HANDLE_SIGNAL_NONE;
  if (signals_dict.readable())
    signals |= MOJO_HANDLE_SIGNAL_READABLE;
  if (signals_dict.writable())
    signals |= MOJO_HANDLE_SIGNAL_WRITABLE;
  if (signals_dict.peerClosed())
    signals |= MOJO_HANDLE_SIGNAL_PEER_CLOSED;

  MojoResult result =
      mojo::CreateWatcher(&MojoWatcher::OnHandleReady, &watcher_handle_);
  DCHECK_EQ(MOJO_RESULT_OK, result);

  result = MojoWatch(watcher_handle_.get().value(), handle.value(), signals,
                     reinterpret_cast<uintptr_t>(this));
  if (result != MOJO_RESULT_OK)
    return result;

  handle_ = handle;

  MojoResult ready_result;
  result = Arm(&ready_result);
  if (result == MOJO_RESULT_OK)
    return result;

  // We couldn't arm the watcher because the handle is already ready to
  // trigger a success notification. Post a notification manually.
  DCHECK_EQ(MOJO_RESULT_FAILED_PRECONDITION, result);
  task_runner_->PostTask(BLINK_FROM_HERE,
                         WTF::Bind(&MojoWatcher::RunReadyCallback,
                                   WrapPersistent(this), ready_result));
  return MOJO_RESULT_OK;
}

MojoResult MojoWatcher::Arm(MojoResult* ready_result) {
  // Nothing to do if the watcher is inactive.
  if (!handle_.is_valid())
    return MOJO_RESULT_OK;

  uint32_t num_ready_contexts = 1;
  uintptr_t ready_context;
  MojoResult local_ready_result;
  MojoHandleSignalsState ready_signals;
  MojoResult result =
      MojoArmWatcher(watcher_handle_.get().value(), &num_ready_contexts,
                     &ready_context, &local_ready_result, &ready_signals);
  if (result == MOJO_RESULT_OK)
    return MOJO_RESULT_OK;

  DCHECK_EQ(MOJO_RESULT_FAILED_PRECONDITION, result);
  DCHECK_EQ(1u, num_ready_contexts);
  DCHECK_EQ(reinterpret_cast<uintptr_t>(this), ready_context);
  *ready_result = local_ready_result;
  return result;
}

void MojoWatcher::OnHandleReady(uintptr_t context,
                                MojoResult result,
                                MojoHandleSignalsState,
                                MojoWatcherNotificationFlags) {
  // It is safe to assume the MojoWatcher still exists. It stays alive at least
  // as long as |m_handle| is valid, and |m_handle| is only reset after we
  // dispatch a |MOJO_RESULT_CANCELLED| notification. That is always the last
  // notification received by this callback.
  MojoWatcher* watcher = reinterpret_cast<MojoWatcher*>(context);
  watcher->task_runner_->PostTask(
      BLINK_FROM_HERE,
      CrossThreadBind(&MojoWatcher::RunReadyCallback,
                      WrapCrossThreadWeakPersistent(watcher), result));
}

void MojoWatcher::RunReadyCallback(MojoResult result) {
  if (result == MOJO_RESULT_CANCELLED) {
    // Last notification.
    handle_ = mojo::Handle();

    // Only dispatch to the callback if this cancellation was implicit due to
    // |m_handle| closure. If it was explicit, |m_watcherHandle| has already
    // been reset.
    if (watcher_handle_.is_valid()) {
      watcher_handle_.reset();
      RunWatchCallback(callback_, this, result);
    }
    return;
  }

  // Ignore callbacks if not watching.
  if (!watcher_handle_.is_valid())
    return;

  RunWatchCallback(callback_, this, result);

  // The user callback may have canceled watching.
  if (!watcher_handle_.is_valid())
    return;

  // Rearm the watcher so another notification can fire.
  //
  // TODO(rockot): MojoWatcher should expose some better approximation of the
  // new watcher API, including explicit add and removal of handles from the
  // watcher, as well as explicit arming.
  MojoResult ready_result;
  MojoResult arm_result = Arm(&ready_result);
  if (arm_result == MOJO_RESULT_OK)
    return;

  DCHECK_EQ(MOJO_RESULT_FAILED_PRECONDITION, arm_result);

  task_runner_->PostTask(BLINK_FROM_HERE,
                         WTF::Bind(&MojoWatcher::RunReadyCallback,
                                   WrapWeakPersistent(this), ready_result));
}

}  // namespace blink
