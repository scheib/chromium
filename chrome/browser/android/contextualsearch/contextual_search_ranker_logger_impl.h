// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_CONTEXTUALSEARCH_CONTEXTUAL_SEARCH_RANKER_LOGGER_IMPL_H_
#define CHROME_BROWSER_ANDROID_CONTEXTUALSEARCH_CONTEXTUAL_SEARCH_RANKER_LOGGER_IMPL_H_

#include "base/android/jni_android.h"
#include "components/ukm/ukm_service.h"
#include "url/gurl.h"

class GURL;

namespace ukm {
class UkmService;
}  // namespace ukm

class ContextualSearchRankerLoggerImpl {
 public:
  ContextualSearchRankerLoggerImpl(JNIEnv* env, jobject obj);
  ~ContextualSearchRankerLoggerImpl();

  // Calls the destructor.  Should be called when this native object is no
  // longer needed.
  void Destroy(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);

  // Sets up the logging and Ranker for Contextual Search features using the
  // given details.
  // |j_base_page_url| is the URL of the Contextual Search base-page (where the
  // user might tap.
  void SetupLoggingAndRanker(
      JNIEnv* env,
      jobject obj,
      const base::android::JavaParamRef<jstring>& j_base_page_url);

  // Logs a long value with the given feature name.
  void LogLong(JNIEnv* env,
               jobject obj,
               const base::android::JavaParamRef<jstring>& j_feature,
               jlong j_long);

  // Writes the currently logged data and resets the current builder to be
  // ready to start logging the next set of data.
  void WriteLogAndReset(JNIEnv* env, jobject obj);

 private:
  // Set the UKM service and base-page URL.
  // TODO(donnd): write a test, using this to inject a test-ukm-service.
  void SetUkmService(ukm::UkmService* ukm_service, const GURL& page_url);

  // Used to log URL-keyed metrics. This pointer will outlive |this|.
  ukm::UkmService* ukm_service_;

  // The UKM source ID being used for this session.
  int32_t source_id_;

  // The entry builder for the current record.
  std::unique_ptr<ukm::UkmEntryBuilder> builder_;

  // The linked Java object.
  base::android::ScopedJavaGlobalRef<jobject> java_object_;

  DISALLOW_COPY_AND_ASSIGN(ContextualSearchRankerLoggerImpl);
};

bool RegisterContextualSearchRankerLoggerImpl(JNIEnv* env);

#endif  // CHROME_BROWSER_ANDROID_CONTEXTUALSEARCH_CONTEXTUAL_SEARCH_RANKER_LOGGER_IMPL_H_
