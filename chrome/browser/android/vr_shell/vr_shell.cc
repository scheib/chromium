// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/vr_shell/vr_shell.h"

#include <android/native_window_jni.h>

#include <string>
#include <utility>

#include "base/android/jni_string.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram_macros.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/android/vr_shell/android_ui_gesture_target.h"
#include "chrome/browser/android/vr_shell/ui_interface.h"
#include "chrome/browser/android/vr_shell/ui_scene_manager.h"
#include "chrome/browser/android/vr_shell/vr_compositor.h"
#include "chrome/browser/android/vr_shell/vr_controller_model.h"
#include "chrome/browser/android/vr_shell/vr_gl_thread.h"
#include "chrome/browser/android/vr_shell/vr_input_manager.h"
#include "chrome/browser/android/vr_shell/vr_shell_delegate.h"
#include "chrome/browser/android/vr_shell/vr_shell_gl.h"
#include "chrome/browser/android/vr_shell/vr_tab_helper.h"
#include "chrome/browser/android/vr_shell/vr_usage_monitor.h"
#include "chrome/browser/android/vr_shell/vr_web_contents_observer.h"
#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/media/webrtc/media_stream_capture_indicator.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "content/public/common/referrer.h"
#include "device/vr/android/gvr/gvr_device.h"
#include "device/vr/android/gvr/gvr_device_provider.h"
#include "device/vr/android/gvr/gvr_gamepad_data_fetcher.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "jni/VrShellImpl_jni.h"
#include "third_party/WebKit/public/platform/WebInputEvent.h"
#include "ui/android/view_android.h"
#include "ui/android/window_android.h"
#include "ui/base/page_transition_types.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_widget_types.h"
#include "url/gurl.h"

using base::android::JavaParamRef;
using base::android::JavaRef;

namespace vr_shell {

namespace {
vr_shell::VrShell* g_instance;

constexpr base::TimeDelta poll_media_access_interval_ =
    base::TimeDelta::FromSecondsD(0.01);

constexpr base::TimeDelta kExitVrDueToUnsupportedModeDelay =
    base::TimeDelta::FromSeconds(5);

void SetIsInVR(content::WebContents* contents, bool is_in_vr) {
  if (contents && contents->GetRenderWidgetHostView()) {
    // TODO(asimjour) Contents should not be aware of VR mode. Instead, we
    // should add a flag for disabling specific UI such as the keyboard (see
    // VrTabHelper for details).
    contents->GetRenderWidgetHostView()->SetIsInVR(is_in_vr);

    VrTabHelper* vr_tab_helper = VrTabHelper::FromWebContents(contents);
    DCHECK(vr_tab_helper);
    vr_tab_helper->SetIsInVr(is_in_vr);
  }
}

}  // namespace

VrShell::VrShell(JNIEnv* env,
                 jobject obj,
                 ui::WindowAndroid* window,
                 bool for_web_vr,
                 bool in_cct,
                 VrShellDelegate* delegate,
                 gvr_context* gvr_api,
                 bool reprojected_rendering)
    : vr_shell_enabled_(base::FeatureList::IsEnabled(features::kVrShell)),
      window_(window),
      compositor_(base::MakeUnique<VrCompositor>(window_)),
      delegate_provider_(delegate),
      main_thread_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      reprojected_rendering_(reprojected_rendering),
      gvr_api_(gvr_api),
      weak_ptr_factory_(this) {
  DVLOG(1) << __FUNCTION__ << "=" << this;
  DCHECK(g_instance == nullptr);
  g_instance = this;
  j_vr_shell_.Reset(env, obj);

  gl_thread_ = base::MakeUnique<VrGLThread>(
      weak_ptr_factory_.GetWeakPtr(), main_thread_task_runner_, gvr_api,
      for_web_vr, in_cct, reprojected_rendering_, HasDaydreamSupport(env));
  ui_ = gl_thread_.get();

  base::Thread::Options options(base::MessageLoop::TYPE_DEFAULT, 0);
  options.priority = base::ThreadPriority::DISPLAY;
  gl_thread_->StartWithOptions(options);
}

void VrShell::Destroy(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  delete this;
}

void VrShell::SwapContents(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobject>& web_contents,
    const JavaParamRef<jobject>& touch_event_synthesizer) {
  content::WebContents* contents =
      content::WebContents::FromJavaWebContents(web_contents);
  if (contents == web_contents_ &&
      touch_event_synthesizer.obj() == j_motion_event_synthesizer_.obj())
    return;

  SetIsInVR(web_contents_, false);
  j_motion_event_synthesizer_.Reset(env, touch_event_synthesizer);
  web_contents_ = contents;
  compositor_->SetLayer(web_contents_);
  SetIsInVR(web_contents_, true);
  ContentFrameWasResized(false /* unused */);
  SetUiState();

  if (!web_contents_) {
    android_ui_gesture_target_ = base::MakeUnique<AndroidUiGestureTarget>(
        j_motion_event_synthesizer_.obj(),
        Java_VrShellImpl_getNativePageScrollRatio(env, j_vr_shell_.obj()));
    input_manager_ = nullptr;
    vr_web_contents_observer_ = nullptr;
    metrics_helper_ = nullptr;
    return;
  }
  input_manager_ = base::MakeUnique<VrInputManager>(web_contents_);
  vr_web_contents_observer_ =
      base::MakeUnique<VrWebContentsObserver>(web_contents_, ui_, this);
  // TODO(billorr): Make VrMetricsHelper tab-aware and able to track multiple
  // tabs. crbug.com/684661
  metrics_helper_ = base::MakeUnique<VrMetricsHelper>(web_contents_);
  metrics_helper_->SetVRActive(true);
  metrics_helper_->SetWebVREnabled(webvr_mode_);
}

void VrShell::SetUiState() {
  if (!web_contents_) {
    // TODO(mthiesse): Properly handle native page URLs.
    ui_->SetURL(GURL());
    ui_->SetLoading(false);
    ui_->SetFullscreen(false);
    ui_->SetURL(GURL());
  } else {
    ui_->SetURL(web_contents_->GetVisibleURL());
    ui_->SetLoading(web_contents_->IsLoading());
    ui_->SetFullscreen(web_contents_->IsFullscreen());
  }
}

bool RegisterVrShell(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

VrShell::~VrShell() {
  DVLOG(1) << __FUNCTION__ << "=" << this;
  poll_capturing_media_task_.Cancel();
  if (gamepad_source_active_) {
    device::GamepadDataFetcherManager::GetInstance()->RemoveSourceFactory(
        device::GAMEPAD_SOURCE_GVR);
  }

  delegate_provider_->RemoveDelegate();
  {
    // The GvrLayout is, and must always be, used only on the UI thread, and the
    // GvrApi used for rendering should only be used from the GL thread as it's
    // not thread safe. However, the GvrLayout owns the GvrApi instance, and
    // when it gets shut down it deletes the GvrApi instance with it. Therefore,
    // we need to block shutting down the GvrLayout on stopping our GL thread
    // from using the GvrApi instance.
    // base::Thread::Stop, which is called when destroying the thread, asserts
    // that IO is allowed to prevent jank, but there shouldn't be any concerns
    // regarding jank in this case, because we're switching from 3D to 2D,
    // adding/removing a bunch of Java views, and probably changing device
    // orientation here.
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    gl_thread_.reset();
  }
  g_instance = nullptr;
}

void VrShell::WaitForGlThread() {
  if (thread_started_)
    return;
  // TODO(mthiesse): Remove this blocking wait. Queue up events on the thread
  // object, rather than on the weak ptr initialized after the thread is
  // started.
  gl_thread_->WaitUntilThreadStarted();
  thread_started_ = true;
}

void VrShell::PostToGlThread(const tracked_objects::Location& from_here,
                             const base::Closure& task) {
  DCHECK(thread_started_);
  gl_thread_->task_runner()->PostTask(from_here, task);
}

void VrShell::OnContentPaused(bool paused) {
  if (!vr_shell_enabled_)
    return;

  if (!delegate_provider_->device_provider())
    return;

  // TODO(mthiesse): The page is no longer visible when in menu mode. We
  // should unfocus or otherwise let it know it's hidden.
  if (paused)
    delegate_provider_->device_provider()->Device()->OnBlur();
  else
    delegate_provider_->device_provider()->Device()->OnFocus();
}

void VrShell::NavigateBack() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_VrShellImpl_navigateBack(env, j_vr_shell_.obj());
}

void VrShell::ExitCct() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_VrShellImpl_exitCct(env, j_vr_shell_.obj());
}

void VrShell::OnTriggerEvent(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  WaitForGlThread();
  PostToGlThread(FROM_HERE, base::Bind(&VrShellGl::OnTriggerEvent,
                                       gl_thread_->GetVrShellGl()));
}

void VrShell::OnPause(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  WaitForGlThread();
  PostToGlThread(FROM_HERE,
                 base::Bind(&VrShellGl::OnPause, gl_thread_->GetVrShellGl()));

  // exit vr session
  if (metrics_helper_)
    metrics_helper_->SetVRActive(false);
  SetIsInVR(web_contents_, false);
}

void VrShell::OnResume(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  WaitForGlThread();
  PostToGlThread(FROM_HERE,
                 base::Bind(&VrShellGl::OnResume, gl_thread_->GetVrShellGl()));

  if (metrics_helper_)
    metrics_helper_->SetVRActive(true);
  SetIsInVR(web_contents_, true);
}

void VrShell::SetSurface(JNIEnv* env,
                         const JavaParamRef<jobject>& obj,
                         const JavaParamRef<jobject>& surface) {
  CHECK(!reprojected_rendering_);
  gfx::AcceleratedWidget window =
      ANativeWindow_fromSurface(base::android::AttachCurrentThread(), surface);
  WaitForGlThread();
  PostToGlThread(FROM_HERE, base::Bind(&VrShellGl::InitializeGl,
                                       gl_thread_->GetVrShellGl(),
                                       base::Unretained(window)));
}

void VrShell::SetWebVrMode(JNIEnv* env,
                           const JavaParamRef<jobject>& obj,
                           bool enabled) {
  webvr_mode_ = enabled;
  if (metrics_helper_)
    metrics_helper_->SetWebVREnabled(enabled);
  WaitForGlThread();
  PostToGlThread(FROM_HERE, base::Bind(&VrShellGl::SetWebVrMode,
                                       gl_thread_->GetVrShellGl(), enabled));
  ui_->SetWebVrMode(enabled);
}

void VrShell::OnFullscreenChanged(bool enabled) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_VrShellImpl_onFullscreenChanged(env, j_vr_shell_.obj(), enabled);
  ui_->SetFullscreen(enabled);
}

bool VrShell::GetWebVrMode(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  return webvr_mode_;
}

void VrShell::OnLoadProgressChanged(JNIEnv* env,
                                    const JavaParamRef<jobject>& obj,
                                    double progress) {
  ui_->SetLoadProgress(progress);
}

void VrShell::OnTabListCreated(JNIEnv* env,
                               const JavaParamRef<jobject>& obj,
                               jobjectArray tabs,
                               jobjectArray incognito_tabs) {
  ProcessTabArray(env, tabs, false);
  ProcessTabArray(env, incognito_tabs, true);
  ui_->FlushTabList();
}

void VrShell::ProcessTabArray(JNIEnv* env, jobjectArray tabs, bool incognito) {
  size_t len = env->GetArrayLength(tabs);
  for (size_t i = 0; i < len; ++i) {
    jobject jtab = env->GetObjectArrayElement(tabs, i);
    TabAndroid* tab =
        TabAndroid::GetNativeTab(env, JavaParamRef<jobject>(env, jtab));
    ui_->AppendToTabList(incognito, tab->GetAndroidId(), tab->GetTitle());
  }
}

void VrShell::OnTabUpdated(JNIEnv* env,
                           const JavaParamRef<jobject>& obj,
                           jboolean incognito,
                           jint id,
                           jstring jtitle) {
  std::string title;
  base::android::ConvertJavaStringToUTF8(env, jtitle, &title);
  ui_->UpdateTab(incognito, id, title);
}

void VrShell::OnTabRemoved(JNIEnv* env,
                           const JavaParamRef<jobject>& obj,
                           jboolean incognito,
                           jint id) {
  ui_->RemoveTab(incognito, id);
}

void VrShell::SetWebVRSecureOrigin(bool secure_origin) {
  ui_->SetWebVrSecureOrigin(secure_origin);
}

void VrShell::SubmitWebVRFrame(int16_t frame_index,
                               const gpu::MailboxHolder& mailbox) {
  TRACE_EVENT1("gpu", "SubmitWebVRFrame", "frame", frame_index);
  WaitForGlThread();
  PostToGlThread(FROM_HERE,
                 base::Bind(&VrShellGl::SubmitWebVRFrame,
                            gl_thread_->GetVrShellGl(), frame_index, mailbox));
}

void VrShell::UpdateWebVRTextureBounds(int16_t frame_index,
                                       const gfx::RectF& left_bounds,
                                       const gfx::RectF& right_bounds,
                                       const gfx::Size& source_size) {
  WaitForGlThread();
  PostToGlThread(FROM_HERE, base::Bind(&VrShellGl::UpdateWebVRTextureBounds,
                                       gl_thread_->GetVrShellGl(), frame_index,
                                       left_bounds, right_bounds, source_size));
}

void VrShell::CreateVRDisplayInfo(
    const base::Callback<void(device::mojom::VRDisplayInfoPtr)>& callback,
    uint32_t device_id) {
  WaitForGlThread();
  PostToGlThread(FROM_HERE,
                 base::Bind(&VrShellGl::CreateVRDisplayInfo,
                            gl_thread_->GetVrShellGl(), callback, device_id));
}

void VrShell::SetSubmitClient(
    device::mojom::VRSubmitFrameClientPtr submit_client) {
  WaitForGlThread();
  PostToGlThread(
      FROM_HERE,
      base::Bind(&VrShellGl::SetSubmitClient, gl_thread_->GetVrShellGl(),
                 base::Passed(submit_client.PassInterface())));
}

base::android::ScopedJavaGlobalRef<jobject> VrShell::TakeContentSurface(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  if (!content_surface_) {
    return base::android::ScopedJavaGlobalRef<jobject>(env, nullptr);
  }
  taken_surface_ = true;
  compositor_->SurfaceChanged(nullptr);
  base::android::ScopedJavaGlobalRef<jobject> surface(env, content_surface_);
  content_surface_ = nullptr;
  return surface;
}

void VrShell::RestoreContentSurface(JNIEnv* env,
                                    const JavaParamRef<jobject>& obj) {
  // Don't try to restore the surface if we haven't successfully taken it yet.
  if (!taken_surface_)
    return;
  taken_surface_ = false;
  WaitForGlThread();
  PostToGlThread(FROM_HERE, base::Bind(&VrShellGl::CreateContentSurface,
                                       gl_thread_->GetVrShellGl()));
}

void VrShell::SetHistoryButtonsEnabled(JNIEnv* env,
                                       const JavaParamRef<jobject>& obj,
                                       jboolean can_go_back,
                                       jboolean can_go_forward) {
  ui_->SetHistoryButtonsEnabled(can_go_back, can_go_forward);
}

void VrShell::ContentSurfaceChanged(jobject surface) {
  content_surface_ = surface;
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_VrShellImpl_contentSurfaceChanged(env, j_vr_shell_.obj());
  compositor_->SurfaceChanged(content_surface_);
}

void VrShell::GvrDelegateReady() {
  delegate_provider_->SetPresentingDelegate(this, gvr_api_);
}

void VrShell::OnPhysicalBackingSizeChanged(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobject>& jweb_contents,
    jint width,
    jint height) {
  content::WebContents* web_contents =
      content::WebContents::FromJavaWebContents(jweb_contents);
  gfx::Size size(width, height);
  web_contents->GetNativeView()->OnPhysicalBackingSizeChanged(size);
}

void VrShell::ContentPhysicalBoundsChanged(JNIEnv* env,
                                           const JavaParamRef<jobject>& object,
                                           jint width,
                                           jint height,
                                           jfloat dpr) {
  TRACE_EVENT0("gpu", "VrShell::ContentPhysicalBoundsChanged");
  WaitForGlThread();
  // TODO(acondor): Set the device scale factor for font rendering on the
  // VR Shell textures.
  PostToGlThread(FROM_HERE,
                 base::Bind(&VrShellGl::ContentPhysicalBoundsChanged,
                            gl_thread_->GetVrShellGl(), width, height));
  compositor_->SetWindowBounds(gfx::Size(width, height));
}

// Note that the following code is obsolete and is here as reference for the
// actions that need to be implemented natively.
void VrShell::DoUiAction(const UiAction action,
                         const base::DictionaryValue* arguments) {
  // Actions that can be handled natively.
  switch (action) {
    default:
      break;
  }
  // Actions that are handled in java.
  JNIEnv* env = base::android::AttachCurrentThread();
  switch (action) {
    case SHOW_TAB: {
      int id;
      CHECK(arguments->GetInteger("id", &id));
      Java_VrShellImpl_showTab(env, j_vr_shell_.obj(), id);
      return;
    }
    case OPEN_NEW_TAB: {
      bool incognito;
      CHECK(arguments->GetBoolean("incognito", &incognito));
      Java_VrShellImpl_openNewTab(env, j_vr_shell_.obj(), incognito);
      return;
    }
    case HISTORY_FORWARD:
      Java_VrShellImpl_navigateForward(env, j_vr_shell_.obj());
      break;
    case RELOAD:
      Java_VrShellImpl_reload(env, j_vr_shell_.obj());
      break;
    default:
      NOTREACHED();
  }
}

void VrShell::ContentFrameWasResized(bool width_changed) {
  display::Display display =
      display::Screen::GetScreen()->GetDisplayNearestWindow(window_);
  WaitForGlThread();
  PostToGlThread(
      FROM_HERE,
      base::Bind(&VrShellGl::ContentBoundsChanged, gl_thread_->GetVrShellGl(),
                 display.size().width(), display.size().height()));
}

void VrShell::ContentWebContentsDestroyed() {
  input_manager_.reset();
  web_contents_ = nullptr;
  // TODO(mthiesse): Handle web contents being destroyed.
  ForceExitVr();
}

void VrShell::ContentWasHidden() {
  // Ensure we don't continue sending input to it.
  input_manager_ = nullptr;
}

void VrShell::ContentWasShown() {
  if (web_contents_)
    input_manager_ = base::MakeUnique<VrInputManager>(web_contents_);
}

void VrShell::ForceExitVr() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_VrShellImpl_forceExitVr(env, j_vr_shell_.obj());
}

void VrShell::ExitPresent() {
  delegate_provider_->ExitWebVRPresent();
}

void VrShell::ExitFullscreen() {
  if (web_contents_ && web_contents_->IsFullscreen()) {
    web_contents_->ExitFullscreen(false);
  }
}

void VrShell::ExitVrDueToUnsupportedMode() {
  ui_->SetIsExiting();
  main_thread_task_runner_->PostDelayedTask(
      FROM_HERE,
      base::Bind(&VrShell::ForceExitVr, weak_ptr_factory_.GetWeakPtr()),
      kExitVrDueToUnsupportedModeDelay);
}

void VrShell::OnVRVsyncProviderRequest(
    device::mojom::VRVSyncProviderRequest request) {
  WaitForGlThread();
  PostToGlThread(FROM_HERE,
                 base::Bind(&VrShellGl::OnRequest, gl_thread_->GetVrShellGl(),
                            base::Passed(&request)));
}

void VrShell::UpdateVSyncInterval(int64_t timebase_nanos,
                                  double interval_seconds) {
  PollMediaAccessFlag();
  WaitForGlThread();
  PostToGlThread(FROM_HERE, base::Bind(&VrShellGl::UpdateVSyncInterval,
                                       gl_thread_->GetVrShellGl(),
                                       timebase_nanos, interval_seconds));
}

void VrShell::PollMediaAccessFlag() {
  poll_capturing_media_task_.Cancel();

  poll_capturing_media_task_.Reset(
      base::Bind(&VrShell::PollMediaAccessFlag, base::Unretained(this)));
  main_thread_task_runner_->PostDelayedTask(
      FROM_HERE, poll_capturing_media_task_.callback(),
      poll_media_access_interval_);

  scoped_refptr<MediaStreamCaptureIndicator> indicator =
      MediaCaptureDevicesDispatcher::GetInstance()
          ->GetMediaStreamCaptureIndicator();
  bool is_capturing_audio = indicator->IsCapturingAudio(web_contents_);
  if (is_capturing_audio != is_capturing_audio_)
    ui_->SetAudioCapturingIndicator(is_capturing_audio);
  is_capturing_audio_ = is_capturing_audio;

  bool is_capturing_video = indicator->IsCapturingVideo(web_contents_);
  if (is_capturing_video != is_capturing_video_)
    ui_->SetVideoCapturingIndicator(is_capturing_video);
  is_capturing_video_ = is_capturing_video;

  bool is_capturing_screen = indicator->IsBeingMirrored(web_contents_);
  if (is_capturing_screen != is_capturing_screen_)
    ui_->SetScreenCapturingIndicator(is_capturing_screen);
  is_capturing_screen_ = is_capturing_screen;
}

void VrShell::SetContentCssSize(float width, float height, float dpr) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_VrShellImpl_setContentCssSize(env, j_vr_shell_.obj(), width, height,
                                     dpr);
}

void VrShell::ProcessContentGesture(
    std::unique_ptr<blink::WebInputEvent> event) {
  if (input_manager_) {
    input_manager_->ProcessUpdatedGesture(std::move(event));
  } else if (android_ui_gesture_target_) {
    android_ui_gesture_target_->DispatchWebInputEvent(std::move(event));
  }
}

void VrShell::UpdateGamepadData(device::GvrGamepadData pad) {
  if (!gamepad_source_active_) {
    if (!delegate_provider_->device_provider())
      return;

    unsigned int device_id =
        delegate_provider_->device_provider()->Device()->id();
    device::GamepadDataFetcherManager::GetInstance()->AddFactory(
        new device::GvrGamepadDataFetcher::Factory(this, device_id));
    gamepad_source_active_ = true;
  }
  if (gamepad_data_fetcher_) {
    gamepad_data_fetcher_->SetGamepadData(pad);
  }
}

void VrShell::RegisterGamepadDataFetcher(
    device::GvrGamepadDataFetcher* fetcher) {
  DVLOG(1) << __FUNCTION__ << "(" << fetcher << ")";
  gamepad_data_fetcher_ = fetcher;
}

bool VrShell::HasDaydreamSupport(JNIEnv* env) {
  return Java_VrShellImpl_hasDaydreamSupport(env, j_vr_shell_.obj());
}

// ----------------------------------------------------------------------------
// Native JNI methods
// ----------------------------------------------------------------------------

jlong Init(JNIEnv* env,
           const JavaParamRef<jobject>& obj,
           const JavaParamRef<jobject>& delegate,
           jlong window_android,
           jboolean for_web_vr,
           jboolean in_cct,
           jlong gvr_api,
           jboolean reprojected_rendering) {
  return reinterpret_cast<intptr_t>(new VrShell(
      env, obj, reinterpret_cast<ui::WindowAndroid*>(window_android),
      for_web_vr, in_cct,
      VrShellDelegate::GetNativeVrShellDelegate(env, delegate),
      reinterpret_cast<gvr_context*>(gvr_api), reprojected_rendering));
}

}  // namespace vr_shell
