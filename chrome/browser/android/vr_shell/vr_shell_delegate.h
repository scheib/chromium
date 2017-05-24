// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_VR_SHELL_DELEGATE_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_VR_SHELL_DELEGATE_H_

#include <jni.h>

#include <memory>

#include "base/android/jni_weak_ref.h"
#include "base/callback.h"
#include "base/macros.h"
#include "device/vr/android/gvr/gvr_delegate_provider.h"
#include "third_party/gvr-android-sdk/src/libraries/headers/vr/gvr/capi/include/gvr_types.h"

namespace device {
class GvrDelegate;
class GvrDeviceProvider;
class PresentingGvrDelegate;
}

namespace vr_shell {

class NonPresentingGvrDelegate;

class VrShellDelegate : public device::GvrDelegateProvider {
 public:
  VrShellDelegate(JNIEnv* env, jobject obj);
  ~VrShellDelegate() override;

  static device::GvrDelegateProvider* CreateVrShellDelegate();

  static VrShellDelegate* GetNativeVrShellDelegate(JNIEnv* env,
                                                   jobject jdelegate);

  void SetPresentingDelegate(device::PresentingGvrDelegate* delegate,
                             gvr_context* context);
  void RemoveDelegate();

  void SetPresentResult(JNIEnv* env,
                        const base::android::JavaParamRef<jobject>& obj,
                        jboolean success);
  void DisplayActivate(JNIEnv* env,
                       const base::android::JavaParamRef<jobject>& obj);
  void UpdateVSyncInterval(JNIEnv* env,
                           const base::android::JavaParamRef<jobject>& obj,
                           jlong timebase_nanos,
                           jdouble interval_seconds);
  void OnPause(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void OnResume(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void UpdateNonPresentingContext(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      jlong context);
  void Destroy(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);

  device::GvrDeviceProvider* device_provider() { return device_provider_; }

  // device::GvrDelegateProvider implementation.
  void ExitWebVRPresent() override;

 private:
  // device::GvrDelegateProvider implementation.
  void SetDeviceProvider(device::GvrDeviceProvider* device_provider) override;
  void ClearDeviceProvider() override;
  void RequestWebVRPresent(device::mojom::VRSubmitFrameClientPtr submit_client,
                           const base::Callback<void(bool)>& callback) override;
  device::GvrDelegate* GetDelegate() override;
  void SetListeningForActivate(bool listening) override;

  void CreateNonPresentingDelegate();

  void OnActivateDisplayHandled(bool present_requested);

  std::unique_ptr<NonPresentingGvrDelegate> non_presenting_delegate_;
  base::android::ScopedJavaGlobalRef<jobject> j_vr_shell_delegate_;
  device::GvrDeviceProvider* device_provider_ = nullptr;
  device::PresentingGvrDelegate* presenting_delegate_ = nullptr;
  base::Callback<void(bool)> present_callback_;
  int64_t timebase_nanos_ = 0;
  double interval_seconds_ = 0;
  device::mojom::VRSubmitFrameClientPtr submit_client_;
  bool pending_successful_present_request_ = false;

  base::WeakPtrFactory<VrShellDelegate> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(VrShellDelegate);
};

bool RegisterVrShellDelegate(JNIEnv* env);

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_VR_SHELL_DELEGATE_H_
