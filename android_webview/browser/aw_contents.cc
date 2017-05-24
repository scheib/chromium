// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/aw_contents.h"

#include <limits>
#include <utility>

#include "android_webview/browser/aw_autofill_client.h"
#include "android_webview/browser/aw_browser_context.h"
#include "android_webview/browser/aw_browser_main_parts.h"
#include "android_webview/browser/aw_contents_client_bridge.h"
#include "android_webview/browser/aw_contents_io_thread_client_impl.h"
#include "android_webview/browser/aw_contents_lifecycle_notifier.h"
#include "android_webview/browser/aw_gl_functor.h"
#include "android_webview/browser/aw_pdf_exporter.h"
#include "android_webview/browser/aw_picture.h"
#include "android_webview/browser/aw_renderer_priority_manager.h"
#include "android_webview/browser/aw_resource_context.h"
#include "android_webview/browser/aw_web_contents_delegate.h"
#include "android_webview/browser/browser_view_renderer.h"
#include "android_webview/browser/child_frame.h"
#include "android_webview/browser/deferred_gpu_command_service.h"
#include "android_webview/browser/java_browser_view_renderer_helper.h"
#include "android_webview/browser/permission/aw_permission_request.h"
#include "android_webview/browser/permission/permission_request_handler.h"
#include "android_webview/browser/permission/simple_permission_request.h"
#include "android_webview/browser/render_thread_manager.h"
#include "android_webview/browser/renderer_host/aw_resource_dispatcher_host_delegate.h"
#include "android_webview/browser/scoped_app_gl_state_restore.h"
#include "android_webview/browser/state_serializer.h"
#include "android_webview/common/aw_hit_test_data.h"
#include "android_webview/common/aw_switches.h"
#include "android_webview/common/devtools_instrumentation.h"
#include "android_webview/public/browser/draw_gl.h"
#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/android/locale_utils.h"
#include "base/android/scoped_java_ref.h"
#include "base/atomicops.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/memory/memory_pressure_listener.h"
#include "base/memory/ptr_util.h"
#include "base/pickle.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string16.h"
#include "base/supports_user_data.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/autofill/content/browser/content_autofill_driver_factory.h"
#include "components/autofill/core/browser/autofill_manager.h"
#include "components/autofill/core/browser/webdata/autofill_webdata_service.h"
#include "components/navigation_interception/intercept_navigation_delegate.h"
#include "content/public/browser/android/content_view_core.h"
#include "content/public/browser/android/synchronous_compositor.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/interstitial_page.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_iterator.h"
#include "content/public/browser/ssl_status.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/mhtml_generation_params.h"
#include "content/public/common/renderer_preferences.h"
#include "jni/AwContents_jni.h"
#include "net/base/auth.h"
#include "net/cert/x509_certificate.h"
#include "third_party/skia/include/core/SkPicture.h"
#include "ui/gfx/android/java_bitmap.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
struct AwDrawSWFunctionTable;

using autofill::ContentAutofillDriverFactory;
using autofill::AutofillManager;
using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF16;
using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF16ToJavaString;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::JavaRef;
using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;
using navigation_interception::InterceptNavigationDelegate;
using content::BrowserThread;
using content::ContentViewCore;
using content::RenderFrameHost;
using content::WebContents;

namespace android_webview {

namespace {

bool g_should_download_favicons = false;

bool g_force_auxiliary_bitmap_rendering = false;

std::string g_locale;

std::string g_locale_list;

const void* kAwContentsUserDataKey = &kAwContentsUserDataKey;
const void* kComputedRendererPriorityUserDataKey =
    &kComputedRendererPriorityUserDataKey;

class AwContentsUserData : public base::SupportsUserData::Data {
 public:
  explicit AwContentsUserData(AwContents* ptr) : contents_(ptr) {}

  static AwContents* GetContents(WebContents* web_contents) {
    if (!web_contents)
      return NULL;
    AwContentsUserData* data = static_cast<AwContentsUserData*>(
        web_contents->GetUserData(kAwContentsUserDataKey));
    return data ? data->contents_ : NULL;
  }

 private:
  AwContents* contents_;
};

base::subtle::Atomic32 g_instance_count = 0;

}  // namespace

// static
AwContents* AwContents::FromWebContents(WebContents* web_contents) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return AwContentsUserData::GetContents(web_contents);
}

// static
AwContents* AwContents::FromID(int render_process_id, int render_view_id) {
  content::RenderViewHost* rvh =
      content::RenderViewHost::FromID(render_process_id, render_view_id);
  if (!rvh)
    return NULL;
  content::WebContents* web_contents =
      content::WebContents::FromRenderViewHost(rvh);
  if (!web_contents)
    return NULL;
  return FromWebContents(web_contents);
}

// static
void UpdateDefaultLocale(JNIEnv* env,
                         const JavaParamRef<jclass>&,
                         const JavaParamRef<jstring>& locale,
                         const JavaParamRef<jstring>& locale_list) {
  g_locale = ConvertJavaStringToUTF8(env, locale);
  g_locale_list = ConvertJavaStringToUTF8(env, locale_list);
}

// static
std::string AwContents::GetLocale() {
  return g_locale;
}

// static
std::string AwContents::GetLocaleList() {
  return g_locale_list;
}

// static
AwBrowserPermissionRequestDelegate* AwBrowserPermissionRequestDelegate::FromID(
    int render_process_id,
    int render_frame_id) {
  AwContents* aw_contents =
      AwContents::FromWebContents(content::WebContents::FromRenderFrameHost(
          content::RenderFrameHost::FromID(render_process_id,
                                           render_frame_id)));
  return aw_contents;
}

// static
AwSafeBrowsingUIManager::UIManagerClient*
AwSafeBrowsingUIManager::UIManagerClient::FromWebContents(
    WebContents* web_contents) {
  return AwContents::FromWebContents(web_contents);
}

// static
AwRenderProcessGoneDelegate* AwRenderProcessGoneDelegate::FromWebContents(
    content::WebContents* web_contents) {
  return AwContents::FromWebContents(web_contents);
}

AwContents::AwContents(std::unique_ptr<WebContents> web_contents)
    : content::WebContentsObserver(web_contents.get()),
      functor_(nullptr),
      browser_view_renderer_(
          this,
          BrowserThread::GetTaskRunnerForThread(BrowserThread::UI)),
      web_contents_(std::move(web_contents)),
      renderer_manager_key_(GLViewRendererManager::GetInstance()->NullKey()),
      renderer_requested_priority_(
          AwRendererPriorityManager::RENDERER_PRIORITY_HIGH),
      renderer_priority_waived_when_not_visible_(false) {
  base::subtle::NoBarrier_AtomicIncrement(&g_instance_count, 1);
  icon_helper_.reset(new IconHelper(web_contents_.get()));
  icon_helper_->SetListener(this);
  web_contents_->SetUserData(android_webview::kAwContentsUserDataKey,
                             base::MakeUnique<AwContentsUserData>(this));
  browser_view_renderer_.RegisterWithWebContents(web_contents_.get());

  CompositorID compositor_id;
  if (web_contents_->GetRenderProcessHost() &&
      web_contents_->GetRenderViewHost()) {
    compositor_id.process_id = web_contents_->GetRenderProcessHost()->GetID();
    compositor_id.routing_id =
        web_contents_->GetRenderViewHost()->GetRoutingID();
  }

  browser_view_renderer_.SetActiveCompositorID(compositor_id);
  render_view_host_ext_.reset(
      new AwRenderViewHostExt(this, web_contents_.get()));

  permission_request_handler_.reset(
      new PermissionRequestHandler(this, web_contents_.get()));

  AwAutofillClient* autofill_manager_delegate =
      AwAutofillClient::FromWebContents(web_contents_.get());
  if (autofill_manager_delegate)
    InitAutofillIfNecessary(autofill_manager_delegate->GetSaveFormData());
  content::SynchronousCompositor::SetClientForWebContents(
      web_contents_.get(), &browser_view_renderer_);
  UpdateRendererPriority();
  web_contents_->GetRenderProcessHost()->AddObserver(this);
  AwContentsLifecycleNotifier::OnWebViewCreated();
}

void AwContents::SetJavaPeers(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobject>& aw_contents,
    const JavaParamRef<jobject>& web_contents_delegate,
    const JavaParamRef<jobject>& contents_client_bridge,
    const JavaParamRef<jobject>& io_thread_client,
    const JavaParamRef<jobject>& intercept_navigation_delegate) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // The |aw_content| param is technically spurious as it duplicates |obj| but
  // is passed over anyway to make the binding more explicit.
  java_ref_ = JavaObjectWeakGlobalRef(env, aw_contents);

  web_contents_delegate_.reset(
      new AwWebContentsDelegate(env, web_contents_delegate));
  web_contents_->SetDelegate(web_contents_delegate_.get());

  contents_client_bridge_.reset(
      new AwContentsClientBridge(env, contents_client_bridge));
  AwContentsClientBridge::Associate(web_contents_.get(),
                                    contents_client_bridge_.get());

  AwContentsIoThreadClientImpl::Associate(web_contents_.get(),
                                          io_thread_client);

  InterceptNavigationDelegate::Associate(
      web_contents_.get(), base::MakeUnique<InterceptNavigationDelegate>(
                               env, intercept_navigation_delegate));

  // Finally, having setup the associations, release any deferred requests
  for (content::RenderFrameHost* rfh : web_contents_->GetAllFrames()) {
    int render_process_id = rfh->GetProcess()->GetID();
    int render_frame_id = rfh->GetRoutingID();
    AwResourceDispatcherHostDelegate::OnIoThreadClientReady(render_process_id,
                                                            render_frame_id);
  }
}

void AwContents::SetSaveFormData(bool enabled) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  InitAutofillIfNecessary(enabled);
  // We need to check for the existence, since autofill_manager_delegate
  // may not be created when the setting is false.
  if (AwAutofillClient::FromWebContents(web_contents_.get())) {
    AwAutofillClient::FromWebContents(web_contents_.get())
        ->SetSaveFormData(enabled);
  }
}

void AwContents::InitAutofillIfNecessary(bool enabled) {
  // Do not initialize if the feature is not enabled.
  if (!enabled)
    return;
  // Check if the autofill driver factory already exists.
  content::WebContents* web_contents = web_contents_.get();
  if (ContentAutofillDriverFactory::FromWebContents(web_contents))
    return;

  AwAutofillClient::CreateForWebContents(web_contents);
  ContentAutofillDriverFactory::CreateForWebContentsAndDelegate(
      web_contents, AwAutofillClient::FromWebContents(web_contents),
      base::android::GetDefaultLocaleString(),
      AutofillManager::DISABLE_AUTOFILL_DOWNLOAD_MANAGER);
}

void AwContents::SetAwAutofillClient(const JavaRef<jobject>& client) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;
  Java_AwContents_setAwAutofillClient(env, obj, client);
}

AwContents::~AwContents() {
  DCHECK_EQ(this, AwContents::FromWebContents(web_contents_.get()));
  web_contents_->GetRenderProcessHost()->RemoveObserver(this);
  UpdateRendererPriority(AwRendererPriorityManager::RENDERER_PRIORITY_WAIVED);
  web_contents_->RemoveUserData(kAwContentsUserDataKey);
  if (find_helper_.get())
    find_helper_->SetListener(NULL);
  if (icon_helper_.get())
    icon_helper_->SetListener(NULL);
  base::subtle::Atomic32 instance_count =
      base::subtle::NoBarrier_AtomicIncrement(&g_instance_count, -1);
  // When the last WebView is destroyed free all discardable memory allocated by
  // Chromium, because the app process may continue to run for a long time
  // without ever using another WebView.
  if (instance_count == 0) {
    // TODO(timvolodine): consider moving NotifyMemoryPressure to
    // AwContentsLifecycleNotifier (crbug.com/522988).
    base::MemoryPressureListener::NotifyMemoryPressure(
        base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);
  }
  SetAwGLFunctor(nullptr);
  AwContentsLifecycleNotifier::OnWebViewDestroyed();
}

base::android::ScopedJavaLocalRef<jobject> AwContents::GetWebContents(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(web_contents_);
  if (!web_contents_)
    return base::android::ScopedJavaLocalRef<jobject>();

  return web_contents_->GetJavaWebContents();
}

void AwContents::SetAwGLFunctor(AwGLFunctor* functor) {
  if (functor == functor_) {
    return;
  }
  functor_ = functor;
  if (functor_) {
    browser_view_renderer_.SetCurrentCompositorFrameConsumer(
        functor_->GetCompositorFrameConsumer());
  } else {
    browser_view_renderer_.SetCurrentCompositorFrameConsumer(nullptr);
  }
}

void AwContents::SetAwGLFunctor(JNIEnv* env,
                                const base::android::JavaParamRef<jobject>& obj,
                                jlong gl_functor) {
  SetAwGLFunctor(reinterpret_cast<AwGLFunctor*>(gl_functor));
}

void AwContents::Destroy(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  java_ref_.reset();
  delete this;
}

static jlong Init(JNIEnv* env,
                  const JavaParamRef<jclass>&,
                  const JavaParamRef<jobject>& browser_context) {
  // TODO(joth): Use |browser_context| to get the native BrowserContext, rather
  // than hard-code the default instance lookup here.
  std::unique_ptr<WebContents> web_contents(content::WebContents::Create(
      content::WebContents::CreateParams(AwBrowserContext::GetDefault())));
  // Return an 'uninitialized' instance; most work is deferred until the
  // subsequent SetJavaPeers() call.
  return reinterpret_cast<intptr_t>(new AwContents(std::move(web_contents)));
}

static void SetForceAuxiliaryBitmapRendering(
    JNIEnv* env,
    const JavaParamRef<jclass>&,
    jboolean force_auxiliary_bitmap_rendering) {
  g_force_auxiliary_bitmap_rendering = force_auxiliary_bitmap_rendering;
}

static void SetAwDrawSWFunctionTable(JNIEnv* env,
                                     const JavaParamRef<jclass>&,
                                     jlong function_table) {
  RasterHelperSetAwDrawSWFunctionTable(
      reinterpret_cast<AwDrawSWFunctionTable*>(function_table));
}

static void SetAwDrawGLFunctionTable(JNIEnv* env,
                                     const JavaParamRef<jclass>&,
                                     jlong function_table) {}

// static
jint GetNativeInstanceCount(JNIEnv* env, const JavaParamRef<jclass>&) {
  return base::subtle::NoBarrier_Load(&g_instance_count);
}

namespace {
void DocumentHasImagesCallback(const ScopedJavaGlobalRef<jobject>& message,
                               bool has_images) {
  Java_AwContents_onDocumentHasImagesResponse(AttachCurrentThread(), has_images,
                                              message);
}
}  // namespace

void AwContents::DocumentHasImages(JNIEnv* env,
                                   const JavaParamRef<jobject>& obj,
                                   const JavaParamRef<jobject>& message) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  ScopedJavaGlobalRef<jobject> j_message;
  j_message.Reset(env, message);
  render_view_host_ext_->DocumentHasImages(
      base::Bind(&DocumentHasImagesCallback, j_message));
}

namespace {
void GenerateMHTMLCallback(const JavaRef<jobject>& callback,
                           const base::FilePath& path,
                           int64_t size) {
  JNIEnv* env = AttachCurrentThread();
  // Android files are UTF8, so the path conversion below is safe.
  Java_AwContents_generateMHTMLCallback(
      env, ConvertUTF8ToJavaString(env, path.AsUTF8Unsafe()), size, callback);
}
}  // namespace

void AwContents::GenerateMHTML(JNIEnv* env,
                               const JavaParamRef<jobject>& obj,
                               const JavaParamRef<jstring>& jpath,
                               const JavaParamRef<jobject>& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  base::FilePath target_path(ConvertJavaStringToUTF8(env, jpath));
  web_contents_->GenerateMHTML(
      content::MHTMLGenerationParams(target_path),
      base::Bind(&GenerateMHTMLCallback,
                 ScopedJavaGlobalRef<jobject>(env, callback), target_path));
}

void AwContents::CreatePdfExporter(JNIEnv* env,
                                   const JavaParamRef<jobject>& obj,
                                   const JavaParamRef<jobject>& pdfExporter) {
  pdf_exporter_.reset(new AwPdfExporter(env, pdfExporter, web_contents_.get()));
}

bool AwContents::OnReceivedHttpAuthRequest(const JavaRef<jobject>& handler,
                                           const std::string& host,
                                           const std::string& realm) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return false;

  ScopedJavaLocalRef<jstring> jhost = ConvertUTF8ToJavaString(env, host);
  ScopedJavaLocalRef<jstring> jrealm = ConvertUTF8ToJavaString(env, realm);
  devtools_instrumentation::ScopedEmbedderCallbackTask embedder_callback(
      "onReceivedHttpAuthRequest");
  Java_AwContents_onReceivedHttpAuthRequest(env, obj, handler, jhost, jrealm);
  return true;
}

void AwContents::SetOffscreenPreRaster(bool enabled) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  browser_view_renderer_.SetOffscreenPreRaster(enabled);
}

void AwContents::AddVisitedLinks(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobjectArray>& jvisited_links) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  std::vector<base::string16> visited_link_strings;
  base::android::AppendJavaStringArrayToStringVector(env, jvisited_links,
                                                     &visited_link_strings);

  std::vector<GURL> visited_link_gurls;
  std::vector<base::string16>::const_iterator itr;
  for (itr = visited_link_strings.begin(); itr != visited_link_strings.end();
       ++itr) {
    visited_link_gurls.push_back(GURL(*itr));
  }

  AwBrowserContext::FromWebContents(web_contents_.get())
      ->AddVisitedURLs(visited_link_gurls);
}

bool RegisterAwContents(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

namespace {

void ShowGeolocationPromptHelperTask(const JavaObjectWeakGlobalRef& java_ref,
                                     const GURL& origin) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_ref = java_ref.get(env);
  if (j_ref.obj()) {
    ScopedJavaLocalRef<jstring> j_origin(
        ConvertUTF8ToJavaString(env, origin.spec()));
    devtools_instrumentation::ScopedEmbedderCallbackTask embedder_callback(
        "onGeolocationPermissionsShowPrompt");
    Java_AwContents_onGeolocationPermissionsShowPrompt(env, j_ref, j_origin);
  }
}

void ShowGeolocationPromptHelper(const JavaObjectWeakGlobalRef& java_ref,
                                 const GURL& origin) {
  JNIEnv* env = AttachCurrentThread();
  if (java_ref.get(env).obj()) {
    content::BrowserThread::PostTask(
        content::BrowserThread::UI, FROM_HERE,
        base::Bind(&ShowGeolocationPromptHelperTask, java_ref, origin));
  }
}

}  // anonymous namespace

void AwContents::ShowGeolocationPrompt(const GURL& requesting_frame,
                                       base::Callback<void(bool)> callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  GURL origin = requesting_frame.GetOrigin();
  bool show_prompt = pending_geolocation_prompts_.empty();
  pending_geolocation_prompts_.push_back(OriginCallback(origin, callback));
  if (show_prompt) {
    ShowGeolocationPromptHelper(java_ref_, origin);
  }
}

// Invoked from Java
void AwContents::InvokeGeolocationCallback(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jboolean value,
    const JavaParamRef<jstring>& origin) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (pending_geolocation_prompts_.empty())
    return;

  GURL callback_origin(base::android::ConvertJavaStringToUTF16(env, origin));
  if (callback_origin.GetOrigin() ==
      pending_geolocation_prompts_.front().first) {
    pending_geolocation_prompts_.front().second.Run(value);
    pending_geolocation_prompts_.pop_front();
    if (!pending_geolocation_prompts_.empty()) {
      ShowGeolocationPromptHelper(java_ref_,
                                  pending_geolocation_prompts_.front().first);
    }
  }
}

void AwContents::HideGeolocationPrompt(const GURL& origin) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  bool removed_current_outstanding_callback = false;
  std::list<OriginCallback>::iterator it = pending_geolocation_prompts_.begin();
  while (it != pending_geolocation_prompts_.end()) {
    if ((*it).first == origin.GetOrigin()) {
      if (it == pending_geolocation_prompts_.begin()) {
        removed_current_outstanding_callback = true;
      }
      it = pending_geolocation_prompts_.erase(it);
    } else {
      ++it;
    }
  }

  if (removed_current_outstanding_callback) {
    JNIEnv* env = AttachCurrentThread();
    ScopedJavaLocalRef<jobject> j_ref = java_ref_.get(env);
    if (j_ref.obj()) {
      devtools_instrumentation::ScopedEmbedderCallbackTask embedder_callback(
          "onGeolocationPermissionsHidePrompt");
      Java_AwContents_onGeolocationPermissionsHidePrompt(env, j_ref);
    }
    if (!pending_geolocation_prompts_.empty()) {
      ShowGeolocationPromptHelper(java_ref_,
                                  pending_geolocation_prompts_.front().first);
    }
  }
}

void AwContents::OnPermissionRequest(
    base::android::ScopedJavaLocalRef<jobject> j_request,
    AwPermissionRequest* request) {
  DCHECK(!j_request.is_null());
  DCHECK(request);

  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_ref = java_ref_.get(env);
  if (j_ref.is_null()) {
    permission_request_handler_->CancelRequest(request->GetOrigin(),
                                               request->GetResources());
    return;
  }

  Java_AwContents_onPermissionRequest(env, j_ref, j_request);
}

void AwContents::OnPermissionRequestCanceled(AwPermissionRequest* request) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_request = request->GetJavaObject();
  ScopedJavaLocalRef<jobject> j_ref = java_ref_.get(env);
  if (j_request.is_null() || j_ref.is_null())
    return;

  Java_AwContents_onPermissionRequestCanceled(env, j_ref, j_request);
}

void AwContents::PreauthorizePermission(JNIEnv* env,
                                        const JavaParamRef<jobject>& obj,
                                        const JavaParamRef<jstring>& origin,
                                        jlong resources) {
  permission_request_handler_->PreauthorizePermission(
      GURL(base::android::ConvertJavaStringToUTF8(env, origin)), resources);
}

void AwContents::RequestProtectedMediaIdentifierPermission(
    const GURL& origin,
    const base::Callback<void(bool)>& callback) {
  permission_request_handler_->SendRequest(
      std::unique_ptr<AwPermissionRequestDelegate>(new SimplePermissionRequest(
          origin, AwPermissionRequest::ProtectedMediaId, callback)));
}

void AwContents::CancelProtectedMediaIdentifierPermissionRequests(
    const GURL& origin) {
  permission_request_handler_->CancelRequest(
      origin, AwPermissionRequest::ProtectedMediaId);
}

void AwContents::RequestGeolocationPermission(
    const GURL& origin,
    const base::Callback<void(bool)>& callback) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;

  if (Java_AwContents_useLegacyGeolocationPermissionAPI(env, obj)) {
    ShowGeolocationPrompt(origin, callback);
    return;
  }
  permission_request_handler_->SendRequest(
      std::unique_ptr<AwPermissionRequestDelegate>(new SimplePermissionRequest(
          origin, AwPermissionRequest::Geolocation, callback)));
}

void AwContents::CancelGeolocationPermissionRequests(const GURL& origin) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;

  if (Java_AwContents_useLegacyGeolocationPermissionAPI(env, obj)) {
    HideGeolocationPrompt(origin);
    return;
  }
  permission_request_handler_->CancelRequest(origin,
                                             AwPermissionRequest::Geolocation);
}

void AwContents::RequestMIDISysexPermission(
    const GURL& origin,
    const base::Callback<void(bool)>& callback) {
  permission_request_handler_->SendRequest(
      std::unique_ptr<AwPermissionRequestDelegate>(new SimplePermissionRequest(
          origin, AwPermissionRequest::MIDISysex, callback)));
}

void AwContents::CancelMIDISysexPermissionRequests(const GURL& origin) {
  permission_request_handler_->CancelRequest(
      origin, AwPermissionRequest::AwPermissionRequest::MIDISysex);
}

void AwContents::FindAllAsync(JNIEnv* env,
                              const JavaParamRef<jobject>& obj,
                              const JavaParamRef<jstring>& search_string) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  GetFindHelper()->FindAllAsync(ConvertJavaStringToUTF16(env, search_string));
}

void AwContents::FindNext(JNIEnv* env,
                          const JavaParamRef<jobject>& obj,
                          jboolean forward) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  GetFindHelper()->FindNext(forward);
}

void AwContents::ClearMatches(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  GetFindHelper()->ClearMatches();
}

void AwContents::ClearCache(JNIEnv* env,
                            const JavaParamRef<jobject>& obj,
                            jboolean include_disk_files) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  render_view_host_ext_->ClearCache();

  if (include_disk_files) {
    content::BrowsingDataRemover* remover =
        content::BrowserContext::GetBrowsingDataRemover(
            web_contents_->GetBrowserContext());
    remover->Remove(
        base::Time(), base::Time::Max(),
        content::BrowsingDataRemover::DATA_TYPE_CACHE,
        content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB |
            content::BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB);
  }
}

void AwContents::KillRenderProcess(JNIEnv* env,
                                   const JavaParamRef<jobject>& obj) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  render_view_host_ext_->KillRenderProcess();
}

FindHelper* AwContents::GetFindHelper() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!find_helper_.get()) {
    find_helper_.reset(new FindHelper(web_contents_.get()));
    find_helper_->SetListener(this);
  }
  return find_helper_.get();
}

void AwContents::OnFindResultReceived(int active_ordinal,
                                      int match_count,
                                      bool finished) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;

  Java_AwContents_onFindResultReceived(env, obj, active_ordinal, match_count,
                                       finished);
}

bool AwContents::ShouldDownloadFavicon(const GURL& icon_url) {
  return g_should_download_favicons;
}

void AwContents::OnReceivedIcon(const GURL& icon_url, const SkBitmap& bitmap) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;

  content::NavigationEntry* entry =
      web_contents_->GetController().GetLastCommittedEntry();

  if (entry) {
    entry->GetFavicon().valid = true;
    entry->GetFavicon().url = icon_url;
    entry->GetFavicon().image = gfx::Image::CreateFrom1xBitmap(bitmap);
  }

  Java_AwContents_onReceivedIcon(env, obj, gfx::ConvertToJavaBitmap(&bitmap));
}

void AwContents::OnReceivedTouchIconUrl(const std::string& url,
                                        bool precomposed) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;

  Java_AwContents_onReceivedTouchIconUrl(
      env, obj, ConvertUTF8ToJavaString(env, url), precomposed);
}

void AwContents::PostInvalidate() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (!obj.is_null())
    Java_AwContents_postInvalidateOnAnimation(env, obj);
}

void AwContents::OnNewPicture() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (!obj.is_null()) {
    devtools_instrumentation::ScopedEmbedderCallbackTask embedder_callback(
        "onNewPicture");
    Java_AwContents_onNewPicture(env, obj);
  }
}

base::android::ScopedJavaLocalRef<jbyteArray> AwContents::GetCertificate(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  content::NavigationEntry* entry =
      web_contents_->GetController().GetLastCommittedEntry();
  if (!entry || !entry->GetSSL().certificate)
    return ScopedJavaLocalRef<jbyteArray>();

  // Convert the certificate and return it
  std::string der_string;
  net::X509Certificate::GetDEREncoded(
      entry->GetSSL().certificate->os_cert_handle(), &der_string);
  return base::android::ToJavaByteArray(
      env, reinterpret_cast<const uint8_t*>(der_string.data()),
      der_string.length());
}

void AwContents::RequestNewHitTestDataAt(JNIEnv* env,
                                         const JavaParamRef<jobject>& obj,
                                         jfloat x,
                                         jfloat y,
                                         jfloat touch_major) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  gfx::PointF touch_center(x, y);
  gfx::SizeF touch_area(touch_major, touch_major);
  render_view_host_ext_->RequestNewHitTestDataAt(touch_center, touch_area);
}

void AwContents::UpdateLastHitTestData(JNIEnv* env,
                                       const JavaParamRef<jobject>& obj) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!render_view_host_ext_->HasNewHitTestData())
    return;

  const AwHitTestData& data = render_view_host_ext_->GetLastHitTestData();
  render_view_host_ext_->MarkHitTestDataRead();

  // Make sure to null the Java object if data is empty/invalid.
  ScopedJavaLocalRef<jstring> extra_data_for_type;
  if (data.extra_data_for_type.length())
    extra_data_for_type =
        ConvertUTF8ToJavaString(env, data.extra_data_for_type);

  ScopedJavaLocalRef<jstring> href;
  if (data.href.length())
    href = ConvertUTF16ToJavaString(env, data.href);

  ScopedJavaLocalRef<jstring> anchor_text;
  if (data.anchor_text.length())
    anchor_text = ConvertUTF16ToJavaString(env, data.anchor_text);

  ScopedJavaLocalRef<jstring> img_src;
  if (data.img_src.is_valid())
    img_src = ConvertUTF8ToJavaString(env, data.img_src.spec());

  Java_AwContents_updateHitTestData(env, obj, data.type, extra_data_for_type,
                                    href, anchor_text, img_src);
}

void AwContents::OnSizeChanged(JNIEnv* env,
                               const JavaParamRef<jobject>& obj,
                               int w,
                               int h,
                               int ow,
                               int oh) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  gfx::Size size(w, h);
  web_contents_->GetNativeView()->OnPhysicalBackingSizeChanged(size);
  browser_view_renderer_.OnSizeChanged(w, h);
}

void AwContents::SetViewVisibility(JNIEnv* env,
                                   const JavaParamRef<jobject>& obj,
                                   bool visible) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  browser_view_renderer_.SetViewVisibility(visible);
  UpdateRendererPriority();
}

void AwContents::SetWindowVisibility(JNIEnv* env,
                                     const JavaParamRef<jobject>& obj,
                                     bool visible) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  browser_view_renderer_.SetWindowVisibility(visible);
  UpdateRendererPriority();
}

void AwContents::SetIsPaused(JNIEnv* env,
                             const JavaParamRef<jobject>& obj,
                             bool paused) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  browser_view_renderer_.SetIsPaused(paused);
  UpdateRendererPriority();
}

void AwContents::OnAttachedToWindow(JNIEnv* env,
                                    const JavaParamRef<jobject>& obj,
                                    int w,
                                    int h) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  browser_view_renderer_.OnAttachedToWindow(w, h);
  UpdateRendererPriority();
}

void AwContents::OnDetachedFromWindow(JNIEnv* env,
                                      const JavaParamRef<jobject>& obj) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  browser_view_renderer_.OnDetachedFromWindow();
  UpdateRendererPriority();
}

bool AwContents::IsVisible(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  return browser_view_renderer_.IsClientVisible();
}

base::android::ScopedJavaLocalRef<jbyteArray> AwContents::GetOpaqueState(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Required optimization in WebViewClassic to not save any state if
  // there has been no navigations.
  if (!web_contents_->GetController().GetEntryCount())
    return ScopedJavaLocalRef<jbyteArray>();

  base::Pickle pickle;
  if (!WriteToPickle(*web_contents_, &pickle)) {
    return ScopedJavaLocalRef<jbyteArray>();
  } else {
    return base::android::ToJavaByteArray(
        env, reinterpret_cast<const uint8_t*>(pickle.data()), pickle.size());
  }
}

jboolean AwContents::RestoreFromOpaqueState(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jbyteArray>& state) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // TODO(boliu): This copy can be optimized out if this is a performance
  // problem.
  std::vector<uint8_t> state_vector;
  base::android::JavaByteArrayToByteVector(env, state, &state_vector);

  base::Pickle pickle(reinterpret_cast<const char*>(state_vector.data()),
                      state_vector.size());
  base::PickleIterator iterator(pickle);

  return RestoreFromPickle(&iterator, web_contents_.get());
}

bool AwContents::OnDraw(JNIEnv* env,
                        const JavaParamRef<jobject>& obj,
                        const JavaParamRef<jobject>& canvas,
                        jboolean is_hardware_accelerated,
                        jint scroll_x,
                        jint scroll_y,
                        jint visible_left,
                        jint visible_top,
                        jint visible_right,
                        jint visible_bottom) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  gfx::Vector2d scroll(scroll_x, scroll_y);
  browser_view_renderer_.PrepareToDraw(
      scroll, gfx::Rect(visible_left, visible_top, visible_right - visible_left,
                        visible_bottom - visible_top));
  if (is_hardware_accelerated && browser_view_renderer_.attached_to_window() &&
      !g_force_auxiliary_bitmap_rendering) {
    return browser_view_renderer_.OnDrawHardware();
  }

  gfx::Size view_size = browser_view_renderer_.size();
  if (view_size.IsEmpty()) {
    TRACE_EVENT_INSTANT0("android_webview", "EarlyOut_EmptySize",
                         TRACE_EVENT_SCOPE_THREAD);
    return false;
  }

  // TODO(hush): Right now webview size is passed in as the auxiliary bitmap
  // size, which might hurt performace (only for software draws with auxiliary
  // bitmap). For better performance, get global visible rect, transform it
  // from screen space to view space, then intersect with the webview in
  // viewspace.  Use the resulting rect as the auxiliary bitmap.
  std::unique_ptr<SoftwareCanvasHolder> canvas_holder =
      SoftwareCanvasHolder::Create(canvas, scroll, view_size,
                                   g_force_auxiliary_bitmap_rendering);
  if (!canvas_holder || !canvas_holder->GetCanvas()) {
    TRACE_EVENT_INSTANT0("android_webview", "EarlyOut_NoSoftwareCanvas",
                         TRACE_EVENT_SCOPE_THREAD);
    return false;
  }
  return browser_view_renderer_.OnDrawSoftware(canvas_holder->GetCanvas());
}

void AwContents::SetPendingWebContentsForPopup(
    std::unique_ptr<content::WebContents> pending) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (pending_contents_.get()) {
    // TODO(benm): Support holding multiple pop up window requests.
    LOG(WARNING) << "Blocking popup window creation as an outstanding "
                 << "popup window is still pending.";
    base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE,
                                                    pending.release());
    return;
  }
  pending_contents_.reset(new AwContents(std::move(pending)));
  // Set dip_scale for pending contents, which is necessary for the later
  // SynchronousCompositor and InputHandler setup.
  pending_contents_->SetDipScaleInternal(browser_view_renderer_.dip_scale());
}

void AwContents::FocusFirstNode(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  web_contents_->FocusThroughTabTraversal(false);
}

void AwContents::SetBackgroundColor(JNIEnv* env,
                                    const JavaParamRef<jobject>& obj,
                                    jint color) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  render_view_host_ext_->SetBackgroundColor(color);
}

void AwContents::ZoomBy(JNIEnv* env,
                        const base::android::JavaParamRef<jobject>& obj,
                        jfloat delta) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  browser_view_renderer_.ZoomBy(delta);
}

void AwContents::OnComputeScroll(JNIEnv* env,
                                 const JavaParamRef<jobject>& obj,
                                 jlong animation_time_millis) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  browser_view_renderer_.OnComputeScroll(
      base::TimeTicks() +
      base::TimeDelta::FromMilliseconds(animation_time_millis));
}

jlong AwContents::ReleasePopupAwContents(JNIEnv* env,
                                         const JavaParamRef<jobject>& obj) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return reinterpret_cast<intptr_t>(pending_contents_.release());
}

gfx::Point AwContents::GetLocationOnScreen() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return gfx::Point();
  std::vector<int> location;
  base::android::JavaIntArrayToIntVector(
      env, Java_AwContents_getLocationOnScreen(env, obj).obj(), &location);
  return gfx::Point(location[0], location[1]);
}

void AwContents::ScrollContainerViewTo(const gfx::Vector2d& new_value) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;
  Java_AwContents_scrollContainerViewTo(env, obj, new_value.x(), new_value.y());
}

void AwContents::UpdateScrollState(const gfx::Vector2d& max_scroll_offset,
                                   const gfx::SizeF& contents_size_dip,
                                   float page_scale_factor,
                                   float min_page_scale_factor,
                                   float max_page_scale_factor) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;
  Java_AwContents_updateScrollState(
      env, obj, max_scroll_offset.x(), max_scroll_offset.y(),
      contents_size_dip.width(), contents_size_dip.height(), page_scale_factor,
      min_page_scale_factor, max_page_scale_factor);
}

void AwContents::DidOverscroll(const gfx::Vector2d& overscroll_delta,
                               const gfx::Vector2dF& overscroll_velocity) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;
  Java_AwContents_didOverscroll(env, obj, overscroll_delta.x(),
                                overscroll_delta.y(), overscroll_velocity.x(),
                                overscroll_velocity.y());
}

ui::TouchHandleDrawable* AwContents::CreateDrawable() {
  JNIEnv* env = AttachCurrentThread();
  const ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return nullptr;
  return reinterpret_cast<ui::TouchHandleDrawable*>(
      Java_AwContents_onCreateTouchHandle(env, obj));
}

void AwContents::SetDipScale(JNIEnv* env,
                             const JavaParamRef<jobject>& obj,
                             jfloat dip_scale) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  SetDipScaleInternal(dip_scale);
}

void AwContents::SetDipScaleInternal(float dip_scale) {
  browser_view_renderer_.SetDipScale(dip_scale);
}

void AwContents::ScrollTo(JNIEnv* env,
                          const JavaParamRef<jobject>& obj,
                          jint x,
                          jint y) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  browser_view_renderer_.ScrollTo(gfx::Vector2d(x, y));
}

void AwContents::SmoothScroll(JNIEnv* env,
                              const JavaParamRef<jobject>& obj,
                              jint target_x,
                              jint target_y,
                              jlong duration_ms) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  float scale = browser_view_renderer_.dip_scale() *
                browser_view_renderer_.page_scale_factor();
  render_view_host_ext_->SmoothScroll(target_x / scale, target_y / scale,
                                      duration_ms);
}

void AwContents::OnWebLayoutPageScaleFactorChanged(float page_scale_factor) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;
  Java_AwContents_onWebLayoutPageScaleFactorChanged(env, obj,
                                                    page_scale_factor);
}

void AwContents::OnWebLayoutContentsSizeChanged(
    const gfx::Size& contents_size) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;
  Java_AwContents_onWebLayoutContentsSizeChanged(
      env, obj, contents_size.width(), contents_size.height());
}

jlong AwContents::CapturePicture(JNIEnv* env,
                                 const JavaParamRef<jobject>& obj,
                                 int width,
                                 int height) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return reinterpret_cast<intptr_t>(
      new AwPicture(browser_view_renderer_.CapturePicture(width, height)));
}

void AwContents::EnableOnNewPicture(JNIEnv* env,
                                    const JavaParamRef<jobject>& obj,
                                    jboolean enabled) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  browser_view_renderer_.EnableOnNewPicture(enabled);
}

namespace {
void InvokeVisualStateCallback(const JavaObjectWeakGlobalRef& java_ref,
                               jlong request_id,
                               const JavaRef<jobject>& callback,
                               bool result) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref.get(env);
  if (obj.is_null())
    return;
  Java_AwContents_invokeVisualStateCallback(env, obj, callback, request_id);
}
}  // namespace

void AwContents::InsertVisualStateCallback(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jlong request_id,
    const JavaParamRef<jobject>& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  web_contents_->GetMainFrame()->InsertVisualStateCallback(
      base::Bind(&InvokeVisualStateCallback, java_ref_, request_id,
                 ScopedJavaGlobalRef<jobject>(env, callback)));
}

void AwContents::UpdateRendererPriority(
    AwRendererPriorityManager::RendererPriority base_priority) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  content::RenderProcessHost* rph = web_contents_->GetRenderProcessHost();
  AwRendererPriorityManager::RendererPriority computed_priority = base_priority;

  std::unique_ptr<content::RenderWidgetHostIterator> widgets(
      content::RenderWidgetHost::GetRenderWidgetHosts());
  content::RenderWidgetHost* widget;
  while ((widget = widgets->GetNextHost()) != nullptr &&
         computed_priority <
             AwRendererPriorityManager::RENDERER_PRIORITY_HIGH) {
    content::RenderViewHost* view = content::RenderViewHost::From(widget);
    if (view && rph == view->GetProcess()) {
      content::WebContents* wc = content::WebContents::FromRenderViewHost(view);
      if (wc && wc != web_contents_.get()) {
        AwContents* aw_contents = FromWebContents(wc);
        if (aw_contents) {
          computed_priority = std::max(
              aw_contents->GetComputedRendererPriority(), computed_priority);
        }
      }
    }
  }
  GetAwRendererPriorityManager()->SetRendererPriority(computed_priority);
}

AwRendererPriorityManager::RendererPriority
AwContents::GetComputedRendererPriority() {
  if (renderer_priority_waived_when_not_visible_ &&
      !browser_view_renderer_.IsClientVisible()) {
    return AwRendererPriorityManager::RENDERER_PRIORITY_WAIVED;
  }
  return renderer_requested_priority_;
}

void AwContents::UpdateRendererPriority() {
  UpdateRendererPriority(GetComputedRendererPriority());
}

AwRendererPriorityManager* AwContents::GetAwRendererPriorityManager() {
  content::RenderProcessHost* rph = web_contents_->GetRenderProcessHost();
  AwRendererPriorityManager* manager = static_cast<AwRendererPriorityManager*>(
      rph->GetUserData(kComputedRendererPriorityUserDataKey));
  if (manager == nullptr) {
    manager = new AwRendererPriorityManager(rph);
    rph->SetUserData(kComputedRendererPriorityUserDataKey,
                     base::WrapUnique(manager));
  }
  return manager;
}

AwRendererPriorityManager::RendererPriority
AwContents::GetCurrentRendererPriority() {
  return GetAwRendererPriorityManager()->GetRendererPriority();
}

jint AwContents::GetRendererCurrentPriority(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj) {
  return GetCurrentRendererPriority();
}

jint AwContents::GetRendererRequestedPriority(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj) {
  return renderer_requested_priority_;
}

jboolean AwContents::GetRendererPriorityWaivedWhenNotVisible(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj) {
  return renderer_priority_waived_when_not_visible_;
}

void AwContents::SetRendererPriorityPolicy(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jint renderer_requested_priority,
    jboolean renderer_priority_waived_when_not_visible) {
  renderer_requested_priority_ =
      static_cast<AwRendererPriorityManager::RendererPriority>(
          renderer_requested_priority);
  renderer_priority_waived_when_not_visible_ =
      renderer_priority_waived_when_not_visible;
  UpdateRendererPriority(renderer_requested_priority_);
}

void AwContents::ClearView(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  browser_view_renderer_.ClearView();
}

void AwContents::SetExtraHeadersForUrl(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& url,
    const JavaParamRef<jstring>& jextra_headers) {
  std::string extra_headers;
  if (jextra_headers)
    extra_headers = ConvertJavaStringToUTF8(env, jextra_headers);
  AwResourceContext* resource_context = static_cast<AwResourceContext*>(
      AwBrowserContext::FromWebContents(web_contents_.get())
          ->GetResourceContext());
  resource_context->SetExtraHeaders(GURL(ConvertJavaStringToUTF8(env, url)),
                                    extra_headers);
}

void AwContents::SetJsOnlineProperty(JNIEnv* env,
                                     const JavaParamRef<jobject>& obj,
                                     jboolean network_up) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  render_view_host_ext_->SetJsOnlineProperty(network_up);
}

void AwContents::TrimMemory(JNIEnv* env,
                            const JavaParamRef<jobject>& obj,
                            jint level,
                            jboolean visible) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Constants from Android ComponentCallbacks2.
  enum {
    TRIM_MEMORY_RUNNING_LOW = 10,
    TRIM_MEMORY_UI_HIDDEN = 20,
    TRIM_MEMORY_BACKGROUND = 40,
    TRIM_MEMORY_MODERATE = 60,
  };

  // Not urgent enough. TRIM_MEMORY_UI_HIDDEN is treated specially because
  // it does not indicate memory pressure, but merely that the app is
  // backgrounded.
  if (level < TRIM_MEMORY_RUNNING_LOW || level == TRIM_MEMORY_UI_HIDDEN)
    return;

  // Do not release resources on view we expect to get DrawGL soon.
  if (level < TRIM_MEMORY_BACKGROUND && visible)
    return;

  browser_view_renderer_.TrimMemory();
}

void AwContents::GrantFileSchemeAccesstoChildProcess(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  content::ChildProcessSecurityPolicy::GetInstance()->GrantScheme(
      web_contents_->GetRenderProcessHost()->GetID(), url::kFileScheme);
}

void AwContents::ResumeLoadingCreatedPopupWebContents(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  web_contents_->ResumeLoadingCreatedWebContents();
}

void SetShouldDownloadFavicons(JNIEnv* env,
                               const JavaParamRef<jclass>& jclazz) {
  g_should_download_favicons = true;
}

void AwContents::RenderViewHostChanged(content::RenderViewHost* old_host,
                                       content::RenderViewHost* new_host) {
  DCHECK(new_host);

  int process_id = new_host->GetProcess()->GetID();
  int routing_id = new_host->GetRoutingID();

  // At this point, the current RVH may or may not contain a compositor. So
  // compositor_ may be nullptr, in which case
  // BrowserViewRenderer::DidInitializeCompositor() callback is time when the
  // new compositor is constructed.
  browser_view_renderer_.SetActiveCompositorID(
      CompositorID(process_id, routing_id));
}

void AwContents::DidAttachInterstitialPage() {
  CompositorID compositor_id;
  RenderFrameHost* rfh = web_contents_->GetInterstitialPage()->GetMainFrame();
  compositor_id.process_id = rfh->GetProcess()->GetID();
  compositor_id.routing_id = rfh->GetRenderViewHost()->GetRoutingID();
  browser_view_renderer_.SetActiveCompositorID(compositor_id);
}

void AwContents::DidDetachInterstitialPage() {
  CompositorID compositor_id;
  if (!web_contents_)
    return;
  if (web_contents_->GetRenderProcessHost() &&
      web_contents_->GetRenderViewHost()) {
    compositor_id.process_id = web_contents_->GetRenderProcessHost()->GetID();
    compositor_id.routing_id =
        web_contents_->GetRenderViewHost()->GetRoutingID();
  } else {
    LOG(WARNING) << "failed setting the compositor on detaching interstitital";
  }
  browser_view_renderer_.SetActiveCompositorID(compositor_id);
}

bool AwContents::CanShowBigInterstitial() {
  JNIEnv* env = AttachCurrentThread();
  const ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return false;
  return Java_AwContents_canShowBigInterstitial(env, obj);
}

void AwContents::CallProceedOnInterstitialForTesting(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj) {
  DCHECK(web_contents_->GetInterstitialPage());
  web_contents_->GetInterstitialPage()->Proceed();
}

void AwContents::CallDontProceedOnInterstitialForTesting(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj) {
  DCHECK(web_contents_->GetInterstitialPage());
  web_contents_->GetInterstitialPage()->DontProceed();
}

void AwContents::OnRenderProcessGone(int child_process_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;

  Java_AwContents_onRenderProcessGone(env, obj, child_process_id);
}

bool AwContents::OnRenderProcessGoneDetail(int child_process_id, bool crashed) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return false;

  return Java_AwContents_onRenderProcessGoneDetail(env, obj, child_process_id,
                                                   crashed);
}

void AwContents::RenderProcessReady(content::RenderProcessHost* host) {
  UpdateRendererPriority();
}

}  // namespace android_webview
