// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrome/browser/android/contextualsearch/contextual_search_ranker_logger_impl.h>

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/metrics/metrics_hashes.h"
#include "chrome/browser/browser_process.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/ukm/ukm_entry_builder.h"
#include "components/ukm/ukm_service.h"
#include "jni/ContextualSearchRankerLoggerImpl_jni.h"

ContextualSearchRankerLoggerImpl::ContextualSearchRankerLoggerImpl(
    JNIEnv* env,
    jobject obj) {
  java_object_.Reset(env, obj);
}

ContextualSearchRankerLoggerImpl::~ContextualSearchRankerLoggerImpl() {
  java_object_ = nullptr;
}

void ContextualSearchRankerLoggerImpl::SetupLoggingAndRanker(
    JNIEnv* env,
    jobject obj,
    const base::android::JavaParamRef<jstring>& j_base_page_url) {
  GURL page_url =
      GURL(base::android::ConvertJavaStringToUTF8(env, j_base_page_url));
  ukm::UkmService* ukm_service = g_browser_process->ukm_service();
  SetUkmService(ukm_service, page_url);
  // TODO(donnd): set up the model once inference is available.
}

void ContextualSearchRankerLoggerImpl::SetUkmService(
    ukm::UkmService* ukm_service,
    const GURL& page_url) {
  ukm_service_ = ukm_service;
  source_id_ = ukm_service_->GetNewSourceID();
  ukm_service_->UpdateSourceURL(source_id_, page_url);
  builder_ = ukm_service_->GetEntryBuilder(source_id_, "ContextualSearch");
}

void ContextualSearchRankerLoggerImpl::LogLong(
    JNIEnv* env,
    jobject obj,
    const base::android::JavaParamRef<jstring>& j_feature,
    jlong j_long) {
  std::string feature = base::android::ConvertJavaStringToUTF8(env, j_feature);
  builder_->AddMetric(feature.c_str(), j_long);
}

void ContextualSearchRankerLoggerImpl::WriteLogAndReset(JNIEnv* env,
                                                        jobject obj) {
  // Set up another builder for the next record (in case it's needed).
  builder_ = ukm_service_->GetEntryBuilder(source_id_, "ContextualSearch");
}

// Java wrapper boilerplate

void ContextualSearchRankerLoggerImpl::Destroy(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj) {
  delete this;
}

bool RegisterContextualSearchRankerLoggerImpl(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

jlong Init(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj) {
  ContextualSearchRankerLoggerImpl* ranker_logger_impl =
      new ContextualSearchRankerLoggerImpl(env, obj);
  return reinterpret_cast<intptr_t>(ranker_logger_impl);
}
