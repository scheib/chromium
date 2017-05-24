// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/rlz/rlz_ping_handler.h"

#include "base/android/callback_android.h"
#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/profiles/profile_android.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "jni/RlzPingHandler_jni.h"
#include "net/base/url_util.h"
#include "net/http/http_status_code.h"
#include "net/http/http_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "rlz/lib/lib_values.h"
#include "rlz/lib/net_response_check.h"
#include "url/gurl.h"

using base::android::ConvertJavaStringToUTF16;
using base::android::JavaParamRef;

constexpr int kMaxRetries = 10;

namespace chrome {
namespace android {

RlzPingHandler::RlzPingHandler(jobject jprofile) {
  Profile* profile = ProfileAndroid::FromProfileAndroid(jprofile);
  DCHECK(profile);
  request_context_ = profile->GetRequestContext();
}

RlzPingHandler::~RlzPingHandler() = default;

void RlzPingHandler::Ping(
    const base::android::JavaParamRef<jstring>& j_brand,
    const base::android::JavaParamRef<jstring>& j_language,
    const base::android::JavaParamRef<jstring>& j_events,
    const base::android::JavaParamRef<jstring>& j_id,
    const base::android::JavaParamRef<jobject>& j_callback) {
  if (!j_brand || !j_language || !j_events || !j_id || !j_callback) {
    base::android::RunCallbackAndroid(j_callback, false);
    delete this;
    return;
  }

  JNIEnv* env = base::android::AttachCurrentThread();

  j_callback_.Reset(env, j_callback);
  std::string brand = ConvertJavaStringToUTF8(env, j_brand);
  std::string language = ConvertJavaStringToUTF8(env, j_language);
  std::string events = ConvertJavaStringToUTF8(env, j_events);
  std::string id = ConvertJavaStringToUTF8(env, j_id);

  DCHECK_EQ(brand.length(), 4u);
  DCHECK_EQ(language.length(), 2u);
  DCHECK_EQ(id.length(), 50u);

  GURL request_url(base::StringPrintf(
      "https://%s%s?", rlz_lib::kFinancialServer, rlz_lib::kFinancialPingPath));
  request_url = net::AppendQueryParameter(
      request_url, rlz_lib::kProductSignatureCgiVariable, "chrome");
  request_url = net::AppendQueryParameter(
      request_url, rlz_lib::kProductBrandCgiVariable, brand);
  request_url = net::AppendQueryParameter(
      request_url, rlz_lib::kProductLanguageCgiVariable, language);
  request_url = net::AppendQueryParameter(request_url,
                                          rlz_lib::kEventsCgiVariable, events);
  request_url = net::AppendQueryParameter(request_url,
                                          rlz_lib::kMachineIdCgiVariable, id);

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("rlz", R"(
          semantics {
            sender: "RLZ Ping Handler"
            description:
            "Sends rlz pings for revenue related tracking to the designated web"
            "end point."
            trigger:
            "Critical signals like first install, a promotion dialog being"
            "shown, a user selection for a promotion may trigger a ping"
            destination: WEBSITE
          }
          policy {
            cookies_allowed: true
            cookies_store: "user"
            setting: "Not user controlled. But it uses a trusted web end point"
                     "that doesn't use user data"
            policy_exception_justification:
              "Not implemented, considered not useful as no content is being "
              "uploaded."
          })");

  url_fetcher_ = net::URLFetcher::Create(0, request_url, net::URLFetcher::GET,
                                         this, traffic_annotation);
  url_fetcher_->SetMaxRetriesOn5xx(kMaxRetries);
  url_fetcher_->SetAutomaticallyRetryOnNetworkChanges(kMaxRetries);
  url_fetcher_->SetAutomaticallyRetryOn5xx(true);
  url_fetcher_->SetRequestContext(request_context_.get());
  url_fetcher_->Start();
}

void RlzPingHandler::OnURLFetchComplete(const net::URLFetcher* source) {
  DCHECK_EQ(source, url_fetcher_.get());

  bool valid = false;
  if (!source->GetStatus().is_success() ||
      source->GetResponseCode() != net::HTTP_OK) {
    LOG(WARNING) << base::StringPrintf("Rlz endpoint responded with code %d.",
                                       source->GetResponseCode());
  } else {
    std::string response;
    source->GetResponseAsString(&response);
    int response_length = -1;
    valid = rlz_lib::IsPingResponseValid(response.c_str(), &response_length);
  }

  // TODO(yusufo) : Investigate what else can be checked for validity that is
  // specific to the ping
  base::android::RunCallbackAndroid(j_callback_, valid);
  delete this;
}

void StartPing(JNIEnv* env,
               const JavaParamRef<jclass>& clazz,
               const base::android::JavaParamRef<jobject>& j_profile,
               const base::android::JavaParamRef<jstring>& j_brand,
               const base::android::JavaParamRef<jstring>& j_language,
               const base::android::JavaParamRef<jstring>& j_events,
               const base::android::JavaParamRef<jstring>& j_id,
               const base::android::JavaParamRef<jobject>& j_callback) {
  RlzPingHandler* handler = new RlzPingHandler(j_profile);
  handler->Ping(j_brand, j_language, j_events, j_id, j_callback);
}

// Register native methods
bool RegisterRlzPingHandler(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace android
}  // namespace chrome
