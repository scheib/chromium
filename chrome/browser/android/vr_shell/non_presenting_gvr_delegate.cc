// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/vr_shell/non_presenting_gvr_delegate.h"

#include <utility>

#include "base/callback_helpers.h"
#include "third_party/gvr-android-sdk/src/libraries/headers/vr/gvr/capi/include/gvr.h"

namespace vr_shell {

NonPresentingGvrDelegate::NonPresentingGvrDelegate(gvr_context* context)
    : task_runner_(base::ThreadTaskRunnerHandle::Get()),
      binding_(this),
      weak_ptr_factory_(this) {
  // Context may be null, see VrShellDelegate#createNonPresentingNativeContext
  // for possible reasons a context could fail to be created. For example,
  // the user might uninstall apps or clear data after VR support was detected.
  if (context)
    gvr_api_ = gvr::GvrApi::WrapNonOwned(context);
}

NonPresentingGvrDelegate::~NonPresentingGvrDelegate() {
  StopVSyncLoop();
}

void NonPresentingGvrDelegate::OnVRVsyncProviderRequest(
    device::mojom::VRVSyncProviderRequest request) {
  binding_.Close();
  binding_.Bind(std::move(request));
  binding_.set_connection_error_handler(
      base::Bind(&NonPresentingGvrDelegate::StopVSyncLoop,
                 weak_ptr_factory_.GetWeakPtr()));
  StartVSyncLoop();
}

void NonPresentingGvrDelegate::Pause() {
  vsync_task_.Cancel();
  vsync_paused_ = true;
  if (gvr_api_)
    gvr_api_->PauseTracking();
}

void NonPresentingGvrDelegate::Resume() {
  if (!vsync_paused_)
    return;
  vsync_paused_ = false;
  StartVSyncLoop();
}

device::mojom::VRVSyncProviderRequest
NonPresentingGvrDelegate::OnSwitchToPresentingDelegate() {
  StopVSyncLoop();
  if (binding_.is_bound())
    return binding_.Unbind();
  return nullptr;
}

void NonPresentingGvrDelegate::StopVSyncLoop() {
  vsync_task_.Cancel();
  binding_.Close();
  if (!callback_.is_null()) {
    base::ResetAndReturn(&callback_)
        .Run(nullptr, base::TimeDelta(), -1,
             device::mojom::VRVSyncProvider::Status::CLOSING);
  }
  if (gvr_api_)
    gvr_api_->PauseTracking();
  // If the loop is stopped, it's not considered to be paused.
  vsync_paused_ = false;
}

void NonPresentingGvrDelegate::StartVSyncLoop() {
  vsync_task_.Reset(
      base::Bind(&NonPresentingGvrDelegate::OnVSync, base::Unretained(this)));
  if (gvr_api_) {
    gvr_api_->RefreshViewerProfile();
    gvr_api_->ResumeTracking();
  }
  OnVSync();
}

void NonPresentingGvrDelegate::OnVSync() {
  base::TimeTicks now = base::TimeTicks::Now();
  base::TimeTicks target;

  // Don't run the VSync loop if we're not bound.
  if (!binding_.is_bound()) {
    return;
  }

  // Don't send VSyncs until we have a timebase/interval.
  if (vsync_interval_.is_zero())
    return;
  target = now + vsync_interval_;
  int64_t intervals = (target - vsync_timebase_) / vsync_interval_;
  target = vsync_timebase_ + intervals * vsync_interval_;
  if (!vsync_task_.IsCancelled()) {
    task_runner_->PostDelayedTask(FROM_HERE, vsync_task_.callback(),
                                  target - now);
  }

  base::TimeDelta time = intervals * vsync_interval_;
  if (!callback_.is_null()) {
    SendVSync(time, base::ResetAndReturn(&callback_));
  } else {
    pending_vsync_ = true;
    pending_time_ = time;
  }
}

void NonPresentingGvrDelegate::GetVSync(const GetVSyncCallback& callback) {
  if (!pending_vsync_) {
    if (!callback_.is_null()) {
      mojo::ReportBadMessage(
          "Requested VSync before waiting for response to previous request.");
      binding_.Close();
      return;
    }
    callback_ = callback;
    return;
  }
  pending_vsync_ = false;
  SendVSync(pending_time_, callback);
}

void NonPresentingGvrDelegate::UpdateVSyncInterval(int64_t timebase_nanos,
                                                   double interval_seconds) {
  vsync_timebase_ = base::TimeTicks();
  vsync_timebase_ += base::TimeDelta::FromMicroseconds(timebase_nanos / 1000);
  vsync_interval_ = base::TimeDelta::FromSecondsD(interval_seconds);
  StartVSyncLoop();
}

void NonPresentingGvrDelegate::SendVSync(base::TimeDelta time,
                                         const GetVSyncCallback& callback) {
  if (!gvr_api_) {
    callback.Run(device::mojom::VRPosePtr(nullptr), time, -1,
                 device::mojom::VRVSyncProvider::Status::SUCCESS);
    return;
  }

  callback.Run(GvrDelegate::GetVRPosePtrWithNeckModel(gvr_api_.get(), nullptr),
               time, -1, device::mojom::VRVSyncProvider::Status::SUCCESS);
}

void NonPresentingGvrDelegate::CreateVRDisplayInfo(
    const base::Callback<void(device::mojom::VRDisplayInfoPtr)>& callback,
    uint32_t device_id) {
  if (!gvr_api_) {
    callback.Run(device::mojom::VRDisplayInfoPtr(nullptr));
    return;
  }

  gfx::Size webvr_size = GvrDelegate::GetRecommendedWebVrSize(gvr_api_.get());
  DVLOG(1) << __FUNCTION__ << ": resize recommended to " << webvr_size.width()
           << "x" << webvr_size.height();
  callback.Run(
      GvrDelegate::CreateVRDisplayInfo(gvr_api_.get(), webvr_size, device_id));
}

void NonPresentingGvrDelegate::UpdateContext(gvr_context* context) {
  gvr_api_ = gvr::GvrApi::WrapNonOwned(context);
}

}  // namespace vr_shell
