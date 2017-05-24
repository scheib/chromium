// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_VR_SHELL_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_VR_SHELL_H_

#include <jni.h>

#include <memory>

#include "base/android/jni_weak_ref.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/single_thread_task_runner.h"
#include "chrome/browser/android/vr_shell/ui_interface.h"
#include "chrome/browser/android/vr_shell/vr_controller_model.h"
#include "content/public/browser/web_contents_observer.h"
#include "device/vr/android/gvr/gvr_delegate.h"
#include "device/vr/android/gvr/gvr_gamepad_data_provider.h"
#include "device/vr/vr_service.mojom.h"
#include "third_party/gvr-android-sdk/src/libraries/headers/vr/gvr/capi/include/gvr.h"
#include "third_party/gvr-android-sdk/src/libraries/headers/vr/gvr/capi/include/gvr_types.h"

namespace blink {
class WebInputEvent;
}

namespace content {
class WebContents;
}

namespace gpu {
struct MailboxHolder;
}

namespace ui {
class WindowAndroid;
}

namespace vr_shell {

class AndroidUiGestureTarget;
class UiInterface;
class VrCompositor;
class VrGLThread;
class VrInputManager;
class VrMetricsHelper;
class VrShellDelegate;
class VrWebContentsObserver;

enum UiAction {
  HISTORY_BACK = 0,
  HISTORY_FORWARD,
  RELOAD,
  SHOW_TAB,
  OPEN_NEW_TAB,
  EXIT_PRESENT,
};

class VrMetricsHelper;

// The native instance of the Java VrShell. This class is not threadsafe and
// must only be used on the UI thread.
class VrShell : public device::PresentingGvrDelegate,
                device::GvrGamepadDataProvider {
 public:
  VrShell(JNIEnv* env,
          jobject obj,
          ui::WindowAndroid* window,
          bool for_web_vr,
          bool in_cct,
          VrShellDelegate* delegate,
          gvr_context* gvr_api,
          bool reprojected_rendering);
  void SwapContents(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      const base::android::JavaParamRef<jobject>& web_contents,
      const base::android::JavaParamRef<jobject>& touch_event_synthesizer);
  void LoadUIContent(JNIEnv* env,
                     const base::android::JavaParamRef<jobject>& obj);
  void Destroy(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void OnTriggerEvent(JNIEnv* env,
                      const base::android::JavaParamRef<jobject>& obj);
  void OnPause(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void OnResume(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void SetSurface(JNIEnv* env,
                  const base::android::JavaParamRef<jobject>& obj,
                  const base::android::JavaParamRef<jobject>& surface);
  void SetWebVrMode(JNIEnv* env,
                    const base::android::JavaParamRef<jobject>& obj,
                    bool enabled);
  bool GetWebVrMode(JNIEnv* env,
                    const base::android::JavaParamRef<jobject>& obj);
  void OnFullscreenChanged(bool enabled);
  void OnLoadProgressChanged(JNIEnv* env,
                             const base::android::JavaParamRef<jobject>& obj,
                             double progress);
  void OnTabListCreated(JNIEnv* env,
                        const base::android::JavaParamRef<jobject>& obj,
                        jobjectArray tabs,
                        jobjectArray incognito_tabs);
  void OnTabUpdated(JNIEnv* env,
                    const base::android::JavaParamRef<jobject>& obj,
                    jboolean incognito,
                    jint id,
                    jstring jtitle);
  void OnTabRemoved(JNIEnv* env,
                    const base::android::JavaParamRef<jobject>& obj,
                    jboolean incognito,
                    jint id);
  void OnContentPaused(bool paused);
  void NavigateBack();
  void ExitCct();
  base::android::ScopedJavaGlobalRef<jobject> TakeContentSurface(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj);
  void RestoreContentSurface(JNIEnv* env,
                             const base::android::JavaParamRef<jobject>& obj);
  void SetHistoryButtonsEnabled(JNIEnv* env,
                                const base::android::JavaParamRef<jobject>& obj,
                                jboolean can_go_back,
                                jboolean can_go_forward);

  void ContentWebContentsDestroyed();
  // Called when our WebContents have been hidden. Usually a sign that something
  // like another tab placed in front of it.
  void ContentWasHidden();
  void ContentWasShown();

  void ContentSurfaceChanged(jobject surface);
  void GvrDelegateReady();

  void OnPhysicalBackingSizeChanged(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      const base::android::JavaParamRef<jobject>& jweb_contents,
      jint width,
      jint height);
  void ContentPhysicalBoundsChanged(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& object,
      jint width,
      jint height,
      jfloat dpr);

  // Perform a UI action triggered by the javascript API.
  void DoUiAction(const UiAction action,
                  const base::DictionaryValue* arguments);

  void SetContentCssSize(float width, float height, float dpr);

  void ContentFrameWasResized(bool width_changed);

  void ForceExitVr();
  void ExitPresent();
  void ExitFullscreen();
  void ExitVrDueToUnsupportedMode();

  void ProcessContentGesture(std::unique_ptr<blink::WebInputEvent> event);
  void SubmitControllerModel(std::unique_ptr<VrControllerModel> model);

  // device::GvrGamepadDataProvider implementation.
  void UpdateGamepadData(device::GvrGamepadData) override;
  void RegisterGamepadDataFetcher(device::GvrGamepadDataFetcher*) override;

 private:
  ~VrShell() override;
  void WaitForGlThread();
  void PostToGlThread(const tracked_objects::Location& from_here,
                      const base::Closure& task);
  void SetUiState();

  // device::GvrDelegate implementation.
  void SetWebVRSecureOrigin(bool secure_origin) override;
  void SubmitWebVRFrame(int16_t frame_index,
                        const gpu::MailboxHolder& mailbox) override;
  void UpdateWebVRTextureBounds(int16_t frame_index,
                                const gfx::RectF& left_bounds,
                                const gfx::RectF& right_bounds,
                                const gfx::Size& source_size) override;
  void OnVRVsyncProviderRequest(
      device::mojom::VRVSyncProviderRequest request) override;
  void UpdateVSyncInterval(int64_t timebase_nanos,
                           double interval_seconds) override;
  void CreateVRDisplayInfo(
      const base::Callback<void(device::mojom::VRDisplayInfoPtr)>& callback,
      uint32_t device_id) override;

  // device::PresentingGvrDelegate implementation.
  void SetSubmitClient(
      device::mojom::VRSubmitFrameClientPtr submit_client) override;

  void ProcessTabArray(JNIEnv* env, jobjectArray tabs, bool incognito);

  void PollMediaAccessFlag();

  bool HasDaydreamSupport(JNIEnv* env);

  bool vr_shell_enabled_;

  bool content_paused_ = false;
  bool webvr_mode_ = false;

  content::WebContents* web_contents_ = nullptr;
  base::android::ScopedJavaGlobalRef<jobject> j_motion_event_synthesizer_;
  ui::WindowAndroid* window_;
  std::unique_ptr<VrCompositor> compositor_;

  std::unique_ptr<VrWebContentsObserver> vr_web_contents_observer_;

  VrShellDelegate* delegate_provider_ = nullptr;
  base::android::ScopedJavaGlobalRef<jobject> j_vr_shell_;

  std::unique_ptr<VrInputManager> input_manager_;
  std::unique_ptr<AndroidUiGestureTarget> android_ui_gesture_target_;
  std::unique_ptr<VrMetricsHelper> metrics_helper_;

  scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner_;
  std::unique_ptr<VrGLThread> gl_thread_;
  bool thread_started_ = false;
  UiInterface* ui_;
  bool reprojected_rendering_;

  jobject content_surface_ = nullptr;
  bool taken_surface_ = false;
  base::CancelableClosure poll_capturing_media_task_;
  bool is_capturing_audio_ = false;
  bool is_capturing_video_ = false;
  bool is_capturing_screen_ = false;

  // TODO(mthiesse): Remove the need for this to be stored here.
  // crbug.com/674594
  gvr_context* gvr_api_;

  // Are we currently providing a gamepad factory to the gamepad manager?
  bool gamepad_source_active_ = false;
  // Registered fetcher, must remain alive for UpdateGamepadData calls.
  // That's ok since the fetcher is only destroyed from VrShell's destructor.
  device::GvrGamepadDataFetcher* gamepad_data_fetcher_ = nullptr;

  base::WeakPtrFactory<VrShell> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(VrShell);
};

bool RegisterVrShell(JNIEnv* env);

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_VR_SHELL_H_
