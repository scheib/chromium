// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/android_webview_jni_registrar.h"

#include "android_webview/browser/android_protocol_handler.h"
#include "android_webview/browser/aw_autofill_client.h"
#include "android_webview/browser/aw_contents.h"
#include "android_webview/browser/aw_contents_client_bridge.h"
#include "android_webview/browser/aw_contents_statics.h"
#include "android_webview/browser/aw_debug.h"
#include "android_webview/browser/aw_devtools_server.h"
#include "android_webview/browser/aw_form_database.h"
#include "android_webview/browser/aw_gl_functor.h"
#include "android_webview/browser/aw_http_auth_handler.h"
#include "android_webview/browser/aw_metrics_service_client.h"
#include "android_webview/browser/aw_pdf_exporter.h"
#include "android_webview/browser/aw_picture.h"
#include "android_webview/browser/aw_quota_manager_bridge.h"
#include "android_webview/browser/aw_settings.h"
#include "android_webview/browser/aw_web_contents_delegate.h"
#include "android_webview/browser/cookie_manager.h"
#include "android_webview/browser/permission/aw_permission_request.h"
#include "base/android/jni_android.h"
#include "base/android/jni_registrar.h"
#include "base/trace_event/trace_event.h"
#include "components/spellcheck/spellcheck_build_features.h"

#if BUILDFLAG(ENABLE_SPELLCHECK)
#include "components/spellcheck/browser/android/component_jni_registrar.h"
#endif

namespace android_webview {

static base::android::RegistrationMethod kWebViewRegisteredMethods[] = {
    // Register JNI for android_webview classes.
    {"AndroidProtocolHandler", RegisterAndroidProtocolHandler},
    {"AwAutofillClient", RegisterAwAutofillClient},
    {"AwContents", RegisterAwContents},
    {"AwContentsClientBridge", RegisterAwContentsClientBridge},
    {"AwContentsStatics", RegisterAwContentsStatics},
    {"AwDebug", RegisterAwDebug},
    {"AwDevToolsServer", RegisterAwDevToolsServer},
    {"AwFormDatabase", RegisterAwFormDatabase},
    {"AwPicture", RegisterAwPicture},
    {"AwSettings", RegisterAwSettings},
    {"AwHttpAuthHandler", RegisterAwHttpAuthHandler},
    {"AwMetricsServiceClient", RegisterAwMetricsServiceClient},
    {"AwPdfExporter", RegisterAwPdfExporter},
    {"AwPermissionRequest", RegisterAwPermissionRequest},
    {"AwQuotaManagerBridge", RegisterAwQuotaManagerBridge},
    {"AwWebContentsDelegate", RegisterAwWebContentsDelegate},
    {"CookieManager", RegisterCookieManager},
    {"AwGLFunctor", RegisterAwGLFunctor},
#if BUILDFLAG(ENABLE_SPELLCHECK)
    {"SpellCheckerSessionBridge", spellcheck::android::RegisterSpellcheckJni},
#endif
};

bool RegisterJni(JNIEnv* env) {
  TRACE_EVENT0("startup", "android_webview::RegisterJni");
  return RegisterNativeMethods(env, kWebViewRegisteredMethods,
                               arraysize(kWebViewRegisteredMethods));
}

}  // namespace android_webview
