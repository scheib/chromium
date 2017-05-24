// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/wake_lock/wake_lock_service_impl.h"

#include <utility>

#include "base/memory/ptr_util.h"

namespace device {

namespace {

PowerSaveBlocker::PowerSaveBlockerType ToPowerSaveBlockerType(
    mojom::WakeLockType type) {
  switch (type) {
    case mojom::WakeLockType::PreventAppSuspension:
      return PowerSaveBlocker::PowerSaveBlockerType::
          kPowerSaveBlockPreventAppSuspension;
    case mojom::WakeLockType::PreventDisplaySleep:
      return PowerSaveBlocker::PowerSaveBlockerType::
          kPowerSaveBlockPreventDisplaySleep;
  }

  NOTREACHED();
  return PowerSaveBlocker::PowerSaveBlockerType::
      kPowerSaveBlockPreventAppSuspension;
}

PowerSaveBlocker::Reason ToPowerSaveBlockerReason(
    mojom::WakeLockReason reason) {
  switch (reason) {
    case mojom::WakeLockReason::ReasonAudioPlayback:
      return PowerSaveBlocker::Reason::kReasonAudioPlayback;
    case mojom::WakeLockReason::ReasonVideoPlayback:
      return PowerSaveBlocker::Reason::kReasonVideoPlayback;
    case mojom::WakeLockReason::ReasonOther:
      return PowerSaveBlocker::Reason::kReasonOther;
  }

  NOTREACHED();
  return PowerSaveBlocker::Reason::kReasonOther;
}

}  // namespace

WakeLockServiceImpl::WakeLockServiceImpl(
    mojom::WakeLockServiceRequest request,
    mojom::WakeLockType type,
    mojom::WakeLockReason reason,
    const std::string& description,
    int context_id,
    WakeLockContextCallback native_view_getter,
    scoped_refptr<base::SingleThreadTaskRunner> file_task_runner)
    : type_(type),
      reason_(reason),
      description_(base::MakeUnique<std::string>(description)),
      num_lock_requests_(0),
#if defined(OS_ANDROID)
      context_id_(context_id),
      native_view_getter_(native_view_getter),
#endif
      main_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      file_task_runner_(std::move(file_task_runner)) {
  AddClient(std::move(request));
  binding_set_.set_connection_error_handler(base::Bind(
      &WakeLockServiceImpl::OnConnectionError, base::Unretained(this)));
}

WakeLockServiceImpl::~WakeLockServiceImpl() {}

void WakeLockServiceImpl::AddClient(mojom::WakeLockServiceRequest request) {
  binding_set_.AddBinding(this, std::move(request),
                          base::MakeUnique<bool>(false));
}

void WakeLockServiceImpl::RequestWakeLock() {
  DCHECK(main_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(binding_set_.dispatch_context());

  // Uses the Context to get the outstanding status of current binding.
  // Two consecutive requests from the same client should be coalesced
  // as one request.
  if (*binding_set_.dispatch_context())
    return;

  *binding_set_.dispatch_context() = true;
  num_lock_requests_++;
  UpdateWakeLock();
}

void WakeLockServiceImpl::CancelWakeLock() {
  DCHECK(main_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(binding_set_.dispatch_context());

  if (!(*binding_set_.dispatch_context()))
    return;

  DCHECK(num_lock_requests_ > 0);
  *binding_set_.dispatch_context() = false;
  num_lock_requests_--;
  UpdateWakeLock();
}

void WakeLockServiceImpl::HasWakeLockForTests(
    const HasWakeLockForTestsCallback& callback) {
  callback.Run(!!wake_lock_);
}
void WakeLockServiceImpl::UpdateWakeLock() {
  DCHECK(num_lock_requests_ >= 0);

  if (num_lock_requests_) {
    if (!wake_lock_)
      CreateWakeLock();
  } else {
    if (wake_lock_)
      RemoveWakeLock();
  }
}

void WakeLockServiceImpl::CreateWakeLock() {
  DCHECK(!wake_lock_);

  // TODO(heke): Switch PowerSaveBlocker to use mojom::WakeLockType and
  // mojom::WakeLockReason once all its clients are converted to be the clients
  // of WakeLock.
  wake_lock_ = base::MakeUnique<PowerSaveBlocker>(
      ToPowerSaveBlockerType(type_), ToPowerSaveBlockerReason(reason_),
      *description_, main_task_runner_, file_task_runner_);

  if (type_ != mojom::WakeLockType::PreventDisplaySleep)
    return;

#if defined(OS_ANDROID)
  if (context_id_ == WakeLockServiceContext::WakeLockInvalidContextId) {
    LOG(ERROR) << "Client must pass a valid context_id when requests wake lock "
                  "on Android.";
    return;
  }

  gfx::NativeView native_view = native_view_getter_.Run(context_id_);
  if (native_view)
    wake_lock_.get()->InitDisplaySleepBlocker(native_view);
#endif
}

void WakeLockServiceImpl::RemoveWakeLock() {
  DCHECK(wake_lock_);
  wake_lock_.reset();
}

void WakeLockServiceImpl::OnConnectionError() {
  // If this client has an outstanding wake lock request, decrease the
  // num_lock_requests and call UpdateWakeLock().
  if (*binding_set_.dispatch_context() && num_lock_requests_ > 0) {
    num_lock_requests_--;
    UpdateWakeLock();
  }

  if (binding_set_.empty())
    base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE, this);
}

}  // namespace device
