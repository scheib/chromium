// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/download/download_controller.h"

#include <memory>
#include <utility>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/android/download/dangerous_download_infobar_delegate.h"
#include "chrome/browser/android/download/download_manager_service.h"
#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/permissions/permission_update_infobar_delegate_android.h"
#include "chrome/browser/ui/android/view_android_helper.h"
#include "chrome/grit/chromium_strings.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/download_url_parameters.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/common/referrer.h"
#include "jni/DownloadController_jni.h"
#include "net/base/filename_util.h"
#include "ui/android/view_android.h"
#include "ui/android/window_android.h"
#include "ui/base/page_transition_types.h"

using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;
using content::BrowserContext;
using content::BrowserThread;
using content::ContextMenuParams;
using content::DownloadItem;
using content::DownloadManager;
using content::WebContents;

namespace {
// Guards download_controller_
base::LazyInstance<base::Lock>::DestructorAtExit g_download_controller_lock_;

WebContents* GetWebContents(int render_process_id, int render_view_id) {
  content::RenderViewHost* render_view_host =
      content::RenderViewHost::FromID(render_process_id, render_view_id);

  if (!render_view_host)
    return nullptr;

  return WebContents::FromRenderViewHost(render_view_host);
}

void CreateContextMenuDownload(
    const content::ResourceRequestInfo::WebContentsGetter& wc_getter,
    const content::ContextMenuParams& params,
    bool is_link,
    const std::string& extra_headers,
    bool granted) {
  content::WebContents* web_contents = wc_getter.Run();

  if (!web_contents)
    return;

  const GURL& url = is_link ? params.link_url : params.src_url;
  const GURL& referring_url =
      params.frame_url.is_empty() ? params.page_url : params.frame_url;
  content::DownloadManager* dlm =
      content::BrowserContext::GetDownloadManager(
          web_contents->GetBrowserContext());
  std::unique_ptr<content::DownloadUrlParameters> dl_params(
      content::DownloadUrlParameters::CreateForWebContentsMainFrame(
          web_contents, url));
  content::Referrer referrer = content::Referrer::SanitizeForRequest(
      url,
      content::Referrer(referring_url.GetAsReferrer(), params.referrer_policy));
  dl_params->set_referrer(referrer);
  if (is_link)
    dl_params->set_referrer_encoding(params.frame_charset);
  net::HttpRequestHeaders headers;
  headers.AddHeadersFromString(extra_headers);
  for (net::HttpRequestHeaders::Iterator it(headers); it.GetNext();)
    dl_params->add_request_header(it.name(), it.value());
  if (!is_link && extra_headers.empty())
    dl_params->set_prefer_cache(true);
  dl_params->set_prompt(false);
  dlm->DownloadUrl(std::move(dl_params));
}

// Check if an interrupted download item can be auto resumed.
bool IsInterruptedDownloadAutoResumable(content::DownloadItem* download_item) {
  int interrupt_reason = download_item->GetLastReason();
  DCHECK_NE(interrupt_reason, content::DOWNLOAD_INTERRUPT_REASON_NONE);
  return
      interrupt_reason == content::DOWNLOAD_INTERRUPT_REASON_NETWORK_TIMEOUT ||
      interrupt_reason == content::DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED ||
      interrupt_reason ==
          content::DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED;
}

}  // namespace

static void OnAcquirePermissionResult(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz,
    jlong callback_id,
    jboolean granted,
    const JavaParamRef<jstring>& jpermission_to_update) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(callback_id);

  std::string permission_to_update =
      base::android::ConvertJavaStringToUTF8(env, jpermission_to_update);
  // Convert java long long int to c++ pointer, take ownership.
  std::unique_ptr<DownloadController::AcquirePermissionCallback> cb(
      reinterpret_cast<DownloadController::AcquirePermissionCallback*>(
          callback_id));
  cb->Run(granted, permission_to_update);
}

static void OnRequestFileAccessResult(
    const content::ResourceRequestInfo::WebContentsGetter& web_contents_getter,
    const DownloadControllerBase::AcquireFileAccessPermissionCallback& cb,
    bool granted,
    const std::string& permission_to_update) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!granted && !permission_to_update.empty() && web_contents_getter.Run()) {
    WebContents* web_contents = web_contents_getter.Run();
    std::vector<std::string> permissions;
    permissions.push_back(permission_to_update);

    PermissionUpdateInfoBarDelegate::Create(
        web_contents, permissions,
        IDS_MISSING_STORAGE_PERMISSION_DOWNLOAD_EDUCATION_TEXT, cb);
    return;
  }

  if (!granted) {
    DownloadController::RecordDownloadCancelReason(
        DownloadController::CANCEL_REASON_NO_STORAGE_PERMISSION);
  }
  cb.Run(granted);
}

// static
bool DownloadController::RegisterDownloadController(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

// static
DownloadControllerBase* DownloadControllerBase::Get() {
  base::AutoLock lock(g_download_controller_lock_.Get());
  if (!DownloadControllerBase::download_controller_)
    download_controller_ = DownloadController::GetInstance();
  return DownloadControllerBase::download_controller_;
}

// static
void DownloadControllerBase::SetDownloadControllerBase(
    DownloadControllerBase* download_controller) {
  base::AutoLock lock(g_download_controller_lock_.Get());
  DownloadControllerBase::download_controller_ = download_controller;
}

// static
void DownloadController::RecordDownloadCancelReason(
    DownloadCancelReason reason) {
  UMA_HISTOGRAM_ENUMERATION(
      "MobileDownload.CancelReason", reason, CANCEL_REASON_MAX);
}

// static
DownloadController* DownloadController::GetInstance() {
  return base::Singleton<DownloadController>::get();
}

DownloadController::DownloadController() = default;

DownloadController::~DownloadController() = default;

void DownloadController::AcquireFileAccessPermission(
    const content::ResourceRequestInfo::WebContentsGetter& web_contents_getter,
    const DownloadControllerBase::AcquireFileAccessPermissionCallback& cb) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (HasFileAccessPermission()) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE, base::Bind(cb, true));
    return;
  }

  AcquirePermissionCallback callback(
      base::Bind(&OnRequestFileAccessResult, web_contents_getter, cb));
  // Make copy on the heap so we can pass the pointer through JNI.
  intptr_t callback_id =
      reinterpret_cast<intptr_t>(new AcquirePermissionCallback(callback));
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_DownloadController_requestFileAccess(env, callback_id);
}

void DownloadController::CreateAndroidDownload(
    const content::ResourceRequestInfo::WebContentsGetter& wc_getter,
    const DownloadInfo& info) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&DownloadController::StartAndroidDownload,
                 base::Unretained(this),
                 wc_getter, info));
}

void DownloadController::StartAndroidDownload(
    const content::ResourceRequestInfo::WebContentsGetter& wc_getter,
    const DownloadInfo& info) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  AcquireFileAccessPermission(
      wc_getter, base::Bind(&DownloadController::StartAndroidDownloadInternal,
                            base::Unretained(this), wc_getter, info));
}

void DownloadController::StartAndroidDownloadInternal(
    const content::ResourceRequestInfo::WebContentsGetter& wc_getter,
    const DownloadInfo& info, bool allowed) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!allowed)
    return;

  JNIEnv* env = base::android::AttachCurrentThread();
  base::string16 file_name =
      net::GetSuggestedFilename(info.url, info.content_disposition,
                                std::string(),  // referrer_charset
                                std::string(),  // suggested_name
                                info.original_mime_type, default_file_name_);
  ScopedJavaLocalRef<jstring> jurl =
      ConvertUTF8ToJavaString(env, info.url.spec());
  ScopedJavaLocalRef<jstring> juser_agent =
      ConvertUTF8ToJavaString(env, info.user_agent);
  ScopedJavaLocalRef<jstring> jmime_type =
      ConvertUTF8ToJavaString(env, info.original_mime_type);
  ScopedJavaLocalRef<jstring> jcookie =
      ConvertUTF8ToJavaString(env, info.cookie);
  ScopedJavaLocalRef<jstring> jreferer =
      ConvertUTF8ToJavaString(env, info.referer);
  ScopedJavaLocalRef<jstring> jfile_name =
      base::android::ConvertUTF16ToJavaString(env, file_name);
  Java_DownloadController_enqueueAndroidDownloadManagerRequest(
      env, jurl, juser_agent, jfile_name, jmime_type, jcookie, jreferer);

  WebContents* web_contents = wc_getter.Run();
  if (web_contents) {
    TabAndroid* tab = TabAndroid::FromWebContents(web_contents);
    if (tab && !tab->GetJavaObject().is_null())
      Java_DownloadController_closeTabIfBlank(env, tab->GetJavaObject());
  }
}

bool DownloadController::HasFileAccessPermission() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_DownloadController_hasFileAccess(env);
}

void DownloadController::OnDownloadStarted(
    DownloadItem* download_item) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // For dangerous item, we need to show the dangerous infobar before the
  // download can start.
  JNIEnv* env = base::android::AttachCurrentThread();
  if (!download_item->IsDangerous())
    Java_DownloadController_onDownloadStarted(env);

  WebContents* web_contents = download_item->GetWebContents();
  if (web_contents) {
    TabAndroid* tab = TabAndroid::FromWebContents(web_contents);
    if (tab && !tab->GetJavaObject().is_null()) {
      Java_DownloadController_closeTabIfBlank(env, tab->GetJavaObject());
    }
  }

  // Register for updates to the DownloadItem.
  download_item->AddObserver(this);

  OnDownloadUpdated(download_item);
}

void DownloadController::OnDownloadUpdated(DownloadItem* item) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (item->IsDangerous() && (item->GetState() != DownloadItem::CANCELLED)) {
    // Dont't show notification for a dangerous download, as user can resume
    // the download after browser crash through notification.
    OnDangerousDownload(item);
    return;
  }

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_item =
      DownloadManagerService::CreateJavaDownloadInfo(env, item);
  switch (item->GetState()) {
    case DownloadItem::IN_PROGRESS: {
      Java_DownloadController_onDownloadUpdated(env, j_item);
      break;
    }
    case DownloadItem::COMPLETE:
      // Multiple OnDownloadUpdated() notifications may be issued while the
      // download is in the COMPLETE state. Only handle one.
      item->RemoveObserver(this);

      // Call onDownloadCompleted
      Java_DownloadController_onDownloadCompleted(env, j_item);
      DownloadController::RecordDownloadCancelReason(
             DownloadController::CANCEL_REASON_NOT_CANCELED);
      break;
    case DownloadItem::CANCELLED:
      Java_DownloadController_onDownloadCancelled(env, j_item);
      DownloadController::RecordDownloadCancelReason(
          DownloadController::CANCEL_REASON_OTHER_NATIVE_RESONS);
      break;
    case DownloadItem::INTERRUPTED:
      // When device loses/changes network, we get a NETWORK_TIMEOUT,
      // NETWORK_FAILED or NETWORK_DISCONNECTED error. Download should auto
      // resume in this case.
      Java_DownloadController_onDownloadInterrupted(env, j_item,
          IsInterruptedDownloadAutoResumable(item));
      item->RemoveObserver(this);
      break;
    case DownloadItem::MAX_DOWNLOAD_STATE:
      NOTREACHED();
  }
}

void DownloadController::OnDangerousDownload(DownloadItem* item) {
  WebContents* web_contents = item->GetWebContents();
  if (!web_contents) {
    item->Remove();
    return;
  }

  DangerousDownloadInfoBarDelegate::Create(
        InfoBarService::FromWebContents(web_contents), item);
}

void DownloadController::StartContextMenuDownload(
    const ContextMenuParams& params, WebContents* web_contents, bool is_link,
    const std::string& extra_headers) {
  int process_id = web_contents->GetRenderProcessHost()->GetID();
  int routing_id = web_contents->GetRenderViewHost()->GetRoutingID();

  const content::ResourceRequestInfo::WebContentsGetter& wc_getter(
      base::Bind(&GetWebContents, process_id, routing_id));

  AcquireFileAccessPermission(
      wc_getter, base::Bind(&CreateContextMenuDownload, wc_getter, params,
                            is_link, extra_headers));
}

