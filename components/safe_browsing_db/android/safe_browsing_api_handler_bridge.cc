// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing_db/android/safe_browsing_api_handler_bridge.h"

#include <memory>
#include <string>

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/metrics/histogram_macros.h"
#include "components/safe_browsing_db/safe_browsing_api_handler_util.h"
#include "content/public/browser/browser_thread.h"
#include "jni/SafeBrowsingApiBridge_jni.h"

using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;
using base::android::ToJavaIntArray;
using content::BrowserThread;

namespace safe_browsing {

namespace {
void RunCallbackOnIOThread(
    const SafeBrowsingApiHandler::URLCheckCallbackMeta& callback,
    SBThreatType threat_type,
    const ThreatMetadata& metadata) {
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                          base::Bind(callback, threat_type, metadata));
}

void ReportUmaResult(safe_browsing::UmaRemoteCallResult result) {
  UMA_HISTOGRAM_ENUMERATION("SB2.RemoteCall.Result", result,
                            safe_browsing::UMA_STATUS_MAX_VALUE);
}

// Convert a SBThreatType to a Java threat type.  We only support a few.
int SBThreatTypeToJavaThreatType(const SBThreatType& sb_threat_type) {
  switch (sb_threat_type) {
    case SB_THREAT_TYPE_URL_PHISHING:
      return safe_browsing::JAVA_THREAT_TYPE_SOCIAL_ENGINEERING;
    case SB_THREAT_TYPE_URL_MALWARE:
      return safe_browsing::JAVA_THREAT_TYPE_POTENTIALLY_HARMFUL_APPLICATION;
    case SB_THREAT_TYPE_URL_UNWANTED:
      return safe_browsing::JAVA_THREAT_TYPE_UNWANTED_SOFTWARE;
    default:
      NOTREACHED();
      return 0;
  }
}

// Convert a vector of SBThreatTypes to JavaIntArray of Java threat types.
ScopedJavaLocalRef<jintArray> SBThreatTypesToJavaArray(
    JNIEnv* env,
    const std::vector<SBThreatType>& threat_types) {
  DCHECK(threat_types.size() > 0);
  int int_threat_types[threat_types.size()];
  int* itr = &int_threat_types[0];
  for (auto threat_type : threat_types) {
    *itr++ = SBThreatTypeToJavaThreatType(threat_type);
  }
  return ToJavaIntArray(env, int_threat_types, threat_types.size());
}

}  // namespace

bool RegisterSafeBrowsingApiBridge(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

// Java->Native call, invoked when a check is done.
//   |callback_id| is an int form of pointer to a URLCheckCallbackMeta
//                 that will be called and then deleted here.
//   |result_status| is one of those from SafeBrowsingApiHandler.java
//   |metadata| is a JSON string classifying the threat if there is one.
void OnUrlCheckDone(JNIEnv* env,
                    const JavaParamRef<jclass>& context,
                    jlong callback_id,
                    jint result_status,
                    const JavaParamRef<jstring>& metadata) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(callback_id);

  const std::string metadata_str =
      (metadata ? ConvertJavaStringToUTF8(env, metadata) : "");

  DVLOG(1) << "OnURLCheckDone invoked for check " << callback_id
           << " with status=" << result_status << " and metadata=["
           << metadata_str << "]";

  // Convert java long long int to c++ pointer, take ownership.
  std::unique_ptr<SafeBrowsingApiHandler::URLCheckCallbackMeta> callback(
      reinterpret_cast<SafeBrowsingApiHandlerBridge::URLCheckCallbackMeta*>(
          callback_id));

  if (result_status != RESULT_STATUS_SUCCESS) {
    if (result_status == RESULT_STATUS_TIMEOUT) {
      ReportUmaResult(UMA_STATUS_TIMEOUT);
      VLOG(1) << "Safe browsing API call timed-out";
    } else {
      DCHECK_EQ(result_status, RESULT_STATUS_INTERNAL_ERROR);
      ReportUmaResult(UMA_STATUS_INTERNAL_ERROR);
    }
    RunCallbackOnIOThread(*callback, SB_THREAT_TYPE_SAFE, ThreatMetadata());
    return;
  }

  // Shortcut for safe, so we don't have to parse JSON.
  if (metadata_str == "{}") {
    ReportUmaResult(UMA_STATUS_SAFE);
    RunCallbackOnIOThread(*callback, SB_THREAT_TYPE_SAFE, ThreatMetadata());
  } else {
    // Unsafe, assuming we can parse the JSON.
    SBThreatType worst_threat;
    ThreatMetadata threat_metadata;
    ReportUmaResult(
        ParseJsonFromGMSCore(metadata_str, &worst_threat, &threat_metadata));
    if (worst_threat != SB_THREAT_TYPE_SAFE) {
      DVLOG(1) << "Check " << callback_id << " was a MATCH";
    }

    RunCallbackOnIOThread(*callback, worst_threat, threat_metadata);
  }
}

//
// SafeBrowsingApiHandlerBridge
//
SafeBrowsingApiHandlerBridge::SafeBrowsingApiHandlerBridge()
    : checked_api_support_(false) {}

SafeBrowsingApiHandlerBridge::~SafeBrowsingApiHandlerBridge() {}

bool SafeBrowsingApiHandlerBridge::CheckApiIsSupported() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!checked_api_support_) {
    DVLOG(1) << "Checking API support.";
    j_api_handler_ = Java_SafeBrowsingApiBridge_create(AttachCurrentThread());
    checked_api_support_ = true;
  }
  return j_api_handler_.obj() != nullptr;
}

void SafeBrowsingApiHandlerBridge::StartURLCheck(
    const SafeBrowsingApiHandler::URLCheckCallbackMeta& callback,
    const GURL& url,
    const std::vector<SBThreatType>& threat_types) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (!CheckApiIsSupported()) {
    // Mark all requests as safe. Only users who have an old, broken GMSCore or
    // have sideloaded Chrome w/o PlayStore should land here.
    RunCallbackOnIOThread(callback, SB_THREAT_TYPE_SAFE, ThreatMetadata());
    ReportUmaResult(UMA_STATUS_UNSUPPORTED);
    return;
  }

  // Make copy on the heap so we can pass the pointer through JNI.
  intptr_t callback_id =
      reinterpret_cast<intptr_t>(new URLCheckCallbackMeta(callback));

  DVLOG(1) << "Starting check " << callback_id << " for URL " << url;

  // Default threat types, to support upstream code that doesn't yet set them.
  std::vector<SBThreatType> local_threat_types(threat_types);
  DCHECK(!local_threat_types.empty());

  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jstring> j_url = ConvertUTF8ToJavaString(env, url.spec());
  ScopedJavaLocalRef<jintArray> j_threat_types =
      SBThreatTypesToJavaArray(env, local_threat_types);

  Java_SafeBrowsingApiBridge_startUriLookup(env, j_api_handler_, callback_id,
                                            j_url, j_threat_types);
}

}  // namespace safe_browsing
