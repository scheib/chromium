// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/vr_shell/vr_shell_delegate.h"

#include <utility>

#include "base/android/jni_android.h"
#include "base/callback_helpers.h"
#include "chrome/browser/android/vr_shell/non_presenting_gvr_delegate.h"
#include "device/vr/android/gvr/gvr_delegate.h"
#include "device/vr/android/gvr/gvr_device.h"
#include "device/vr/android/gvr/gvr_device_provider.h"
#include "jni/VrShellDelegate_jni.h"

using base::android::JavaParamRef;
using base::android::AttachCurrentThread;

namespace vr_shell {

VrShellDelegate::VrShellDelegate(JNIEnv* env, jobject obj)
    : weak_ptr_factory_(this) {
  DVLOG(1) << __FUNCTION__ << "=" << this;
  j_vr_shell_delegate_.Reset(env, obj);
}

VrShellDelegate::~VrShellDelegate() {
  DVLOG(1) << __FUNCTION__ << "=" << this;
  if (device_provider_) {
    device_provider_->Device()->OnExitPresent();
    device_provider_->Device()->OnDelegateChanged();
  }
  if (!present_callback_.is_null()) {
    base::ResetAndReturn(&present_callback_).Run(false);
  }
}

device::GvrDelegateProvider* VrShellDelegate::CreateVrShellDelegate() {
  JNIEnv* env = AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> jdelegate =
      Java_VrShellDelegate_getInstance(env);
  if (!jdelegate.is_null())
    return GetNativeVrShellDelegate(env, jdelegate.obj());
  return nullptr;
}

VrShellDelegate* VrShellDelegate::GetNativeVrShellDelegate(JNIEnv* env,
                                                           jobject jdelegate) {
  return reinterpret_cast<VrShellDelegate*>(
      Java_VrShellDelegate_getNativePointer(env, jdelegate));
}

void VrShellDelegate::SetPresentingDelegate(
    device::PresentingGvrDelegate* delegate,
    gvr_context* context) {
  presenting_delegate_ = delegate;
  // Clean up the non-presenting delegate.
  if (presenting_delegate_ && non_presenting_delegate_) {
    non_presenting_delegate_ = nullptr;
    JNIEnv* env = AttachCurrentThread();
    Java_VrShellDelegate_shutdownNonPresentingNativeContext(
        env, j_vr_shell_delegate_.obj());
  }
  if (device_provider_) {
    device::GvrDevice* device = device_provider_->Device();
    device->OnDelegateChanged();
  }

  presenting_delegate_->UpdateVSyncInterval(timebase_nanos_, interval_seconds_);

  if (pending_successful_present_request_) {
    presenting_delegate_->SetSubmitClient(std::move(submit_client_));
    base::ResetAndReturn(&present_callback_).Run(true);
    pending_successful_present_request_ = false;
  }
}

void VrShellDelegate::RemoveDelegate() {
  presenting_delegate_ = nullptr;
  if (device_provider_) {
    CreateNonPresentingDelegate();
    device_provider_->Device()->OnExitPresent();
    device_provider_->Device()->OnDelegateChanged();
  }
}

void VrShellDelegate::SetPresentResult(JNIEnv* env,
                                       const JavaParamRef<jobject>& obj,
                                       jboolean success) {
  CHECK(!present_callback_.is_null());
  if (success && !presenting_delegate_) {
    // We have to wait until the GL thread is ready since we have to pass it
    // the VRSubmitFrameClient.
    pending_successful_present_request_ = true;
    return;
  }

  if (success) {
    presenting_delegate_->SetSubmitClient(std::move(submit_client_));
  }

  base::ResetAndReturn(&present_callback_).Run(success);
  pending_successful_present_request_ = false;
}

void VrShellDelegate::DisplayActivate(JNIEnv* env,
                                      const JavaParamRef<jobject>& obj) {
  if (device_provider_) {
    device_provider_->Device()->OnActivate(
        device::mojom::VRDisplayEventReason::MOUNTED,
        base::Bind(&VrShellDelegate::OnActivateDisplayHandled,
                   weak_ptr_factory_.GetWeakPtr()));
  }
}

void VrShellDelegate::UpdateVSyncInterval(JNIEnv* env,
                                          const JavaParamRef<jobject>& obj,
                                          jlong timebase_nanos,
                                          jdouble interval_seconds) {
  timebase_nanos_ = timebase_nanos;
  interval_seconds_ = interval_seconds;
  if (presenting_delegate_) {
    presenting_delegate_->UpdateVSyncInterval(timebase_nanos_,
                                              interval_seconds_);
  }
  if (non_presenting_delegate_) {
    non_presenting_delegate_->UpdateVSyncInterval(timebase_nanos_,
                                                  interval_seconds_);
  }
}

void VrShellDelegate::OnPause(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  if (non_presenting_delegate_) {
    non_presenting_delegate_->Pause();
  }
}

void VrShellDelegate::OnResume(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  if (non_presenting_delegate_) {
    non_presenting_delegate_->Resume();
  }
}

void VrShellDelegate::UpdateNonPresentingContext(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jlong context) {
  gvr_context* ctx = reinterpret_cast<gvr_context*>(context);
  non_presenting_delegate_->UpdateContext(ctx);
}

void VrShellDelegate::Destroy(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  delete this;
}

void VrShellDelegate::SetDeviceProvider(
    device::GvrDeviceProvider* device_provider) {
  if (device_provider_ == device_provider)
    return;
  if (device_provider_)
    ClearDeviceProvider();
  CHECK(!device_provider_);
  device_provider_ = device_provider;
  if (!presenting_delegate_) {
    CreateNonPresentingDelegate();
  }
  device_provider_->Device()->OnDelegateChanged();
}

void VrShellDelegate::ClearDeviceProvider() {
  non_presenting_delegate_ = nullptr;
  JNIEnv* env = AttachCurrentThread();
  Java_VrShellDelegate_shutdownNonPresentingNativeContext(
      env, j_vr_shell_delegate_.obj());
  device_provider_ = nullptr;
}

void VrShellDelegate::RequestWebVRPresent(
    device::mojom::VRSubmitFrameClientPtr submit_client,
    const base::Callback<void(bool)>& callback) {
  if (!present_callback_.is_null()) {
    // Can only handle one request at a time. This is also extremely unlikely to
    // happen in practice.
    callback.Run(false);
    return;
  }

  present_callback_ = std::move(callback);
  submit_client_ = std::move(submit_client);

  // If/When VRShell is ready for use it will call SetPresentResult.
  JNIEnv* env = AttachCurrentThread();
  Java_VrShellDelegate_presentRequested(env, j_vr_shell_delegate_.obj());
}

void VrShellDelegate::ExitWebVRPresent() {
  // VRShell is no longer needed by WebVR, allow it to shut down if it's not
  // being used elsewhere.
  JNIEnv* env = AttachCurrentThread();
  if (Java_VrShellDelegate_exitWebVRPresent(env, j_vr_shell_delegate_.obj())) {
    if (device_provider_) {
      device_provider_->Device()->OnExitPresent();
    }
  }
}

void VrShellDelegate::CreateNonPresentingDelegate() {
  JNIEnv* env = AttachCurrentThread();
  gvr_context* context = reinterpret_cast<gvr_context*>(
      Java_VrShellDelegate_createNonPresentingNativeContext(
          env, j_vr_shell_delegate_.obj()));
  non_presenting_delegate_ =
      base::MakeUnique<NonPresentingGvrDelegate>(context);
  non_presenting_delegate_->UpdateVSyncInterval(timebase_nanos_,
                                                interval_seconds_);
}

void VrShellDelegate::OnActivateDisplayHandled(bool present_requested) {
  if (!present_requested) {
    // WebVR page didn't request presentation in the vrdisplayactivate handler.
    // Tell VrShell that we are in VR Browsing Mode.
    ExitWebVRPresent();
  }
}

device::GvrDelegate* VrShellDelegate::GetDelegate() {
  if (presenting_delegate_)
    return presenting_delegate_;
  return non_presenting_delegate_.get();
}

void VrShellDelegate::SetListeningForActivate(bool listening) {
  JNIEnv* env = AttachCurrentThread();
  Java_VrShellDelegate_setListeningForWebVrActivate(
      env, j_vr_shell_delegate_.obj(), listening);
}

// ----------------------------------------------------------------------------
// Native JNI methods
// ----------------------------------------------------------------------------

bool RegisterVrShellDelegate(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

jlong Init(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  return reinterpret_cast<intptr_t>(new VrShellDelegate(env, obj));
}

static void OnLibraryAvailable(JNIEnv* env, const JavaParamRef<jclass>& clazz) {
  device::GvrDelegateProvider::SetInstance(
      base::Bind(&VrShellDelegate::CreateVrShellDelegate));
}

}  // namespace vr_shell
