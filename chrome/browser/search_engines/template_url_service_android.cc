// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/search_engines/template_url_service_android.h"

#include <stddef.h>

#include "base/android/jni_string.h"
#include "base/bind.h"
#include "base/format_macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/google/core/browser/google_util.h"
#include "components/search_engines/search_terms_data.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_prepopulate_data.h"
#include "components/search_engines/template_url_service.h"
#include "components/search_engines/util.h"
#include "jni/TemplateUrlService_jni.h"
#include "net/base/url_util.h"

using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;

namespace {

Profile* GetOriginalProfile() {
  return ProfileManager::GetActiveUserProfile()->GetOriginalProfile();
}

}  // namespace

TemplateUrlServiceAndroid::TemplateUrlServiceAndroid(JNIEnv* env,
                                                     jobject obj)
    : weak_java_obj_(env, obj),
      template_url_service_(
          TemplateURLServiceFactory::GetForProfile(GetOriginalProfile())) {
  template_url_subscription_ =
      template_url_service_->RegisterOnLoadedCallback(
          base::Bind(&TemplateUrlServiceAndroid::OnTemplateURLServiceLoaded,
                     base::Unretained(this)));
  template_url_service_->AddObserver(this);
  if (template_url_service_->loaded() && template_urls_.empty())
    LoadTemplateURLs();
}

TemplateUrlServiceAndroid::~TemplateUrlServiceAndroid() {
  template_url_service_->RemoveObserver(this);
}

void TemplateUrlServiceAndroid::Load(JNIEnv* env,
                                     const JavaParamRef<jobject>& obj) {
  template_url_service_->Load();
}

void TemplateUrlServiceAndroid::SetUserSelectedDefaultSearchProvider(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& jkeyword) {
  base::string16 keyword(
      base::android::ConvertJavaStringToUTF16(env, jkeyword));
  TemplateURL* template_url =
      template_url_service_->GetTemplateURLForKeyword(keyword);
  template_url_service_->SetUserSelectedDefaultSearchProvider(template_url);
}

jint TemplateUrlServiceAndroid::GetDefaultSearchProviderIndex(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj) const {
  const TemplateURL* default_search_provider =
      template_url_service_->GetDefaultSearchProvider();
  auto it = std::find(template_urls_.begin(), template_urls_.end(),
                      default_search_provider);
  size_t default_search_provider_index_ =
      (it == template_urls_.end()) ? -1 : (it - template_urls_.begin());
  return default_search_provider_index_;
}

jboolean TemplateUrlServiceAndroid::IsLoaded(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) const {
  return template_url_service_->loaded();
}

jint TemplateUrlServiceAndroid::GetTemplateUrlCount(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) const {
  return template_urls_.size();
}

jboolean TemplateUrlServiceAndroid::IsSearchProviderManaged(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  return template_url_service_->is_default_search_managed();
}

jboolean TemplateUrlServiceAndroid::IsSearchByImageAvailable(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  const TemplateURL* default_search_provider =
      template_url_service_->GetDefaultSearchProvider();
  return default_search_provider &&
      !default_search_provider->image_url().empty() &&
      default_search_provider->image_url_ref().IsValid(
          template_url_service_->search_terms_data());
}

jboolean TemplateUrlServiceAndroid::IsDefaultSearchEngineGoogle(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  const TemplateURL* default_search_provider =
      template_url_service_->GetDefaultSearchProvider();
  return default_search_provider &&
      default_search_provider->url_ref().HasGoogleBaseURLs(
          template_url_service_->search_terms_data());
}

base::android::ScopedJavaLocalRef<jobject>
TemplateUrlServiceAndroid::GetTemplateUrlAt(JNIEnv* env,
                                            const JavaParamRef<jobject>& obj,
                                            jint index) const {
  TemplateURL* template_url = template_urls_[index];
  return Java_TemplateUrl_create(
      env, index,
      base::android::ConvertUTF16ToJavaString(env, template_url->short_name()),
      template_url_service_->IsPrepopulatedOrCreatedByPolicy(template_url),
      base::android::ConvertUTF16ToJavaString(env, template_url->keyword()));
}

void TemplateUrlServiceAndroid::OnTemplateURLServiceLoaded() {
  template_url_subscription_.reset();
  JNIEnv* env = base::android::AttachCurrentThread();
  auto java_obj = weak_java_obj_.get(env);
  if (java_obj.is_null())
    return;
  LoadTemplateURLs();

  Java_TemplateUrlService_templateUrlServiceLoaded(env, java_obj);
}

void TemplateUrlServiceAndroid::LoadTemplateURLs() {
  template_urls_ = template_url_service_->GetTemplateURLs();

  // Move prepopulated and policy-created engines to the front of list,
  // and sort by prepopulated_id.
  TemplateURLService* template_url_service = template_url_service_;
  auto it = std::partition(
      template_urls_.begin(), template_urls_.end(),
      [template_url_service](const TemplateURL* t_url) {
        return template_url_service->IsPrepopulatedOrCreatedByPolicy(t_url);
      });
  std::sort(template_urls_.begin(), it,
            [](const TemplateURL* lhs, const TemplateURL* rhs) {
              return lhs->prepopulate_id() < rhs->prepopulate_id();
            });

  // Place any user-selected default engine next.
  const TemplateURL* dsp = template_url_service_->GetDefaultSearchProvider();
  it = std::partition(it, template_urls_.end(),
                      [dsp](const TemplateURL* t_url) { return t_url == dsp; });

  // Sort the remaining engines to place the three most recently-visited first.
  constexpr size_t kMaxRecentUrls = 3;
  const size_t recent_url_num = template_urls_.end() - it;
  auto end = it + std::min(recent_url_num, kMaxRecentUrls);
  std::partial_sort(it, end, template_urls_.end(),
                    [](const TemplateURL* lhs, const TemplateURL* rhs) {
                      return lhs->last_visited() > rhs->last_visited();
                    });

  // Limit to those three engines which must also have been visited in the last
  // two days.
  constexpr base::TimeDelta kMaxVisitAge = base::TimeDelta::FromDays(2);
  const base::Time cutoff = base::Time::Now() - kMaxVisitAge;
  const auto too_old = [cutoff](const TemplateURL* t_url) {
    return t_url->last_visited() < cutoff;
  };
  template_urls_.erase(std::find_if(it, end, too_old), template_urls_.end());
}

void TemplateUrlServiceAndroid::OnTemplateURLServiceChanged() {
  JNIEnv* env = base::android::AttachCurrentThread();
  auto java_obj = weak_java_obj_.get(env);
  if (java_obj.is_null())
    return;
  LoadTemplateURLs();

  Java_TemplateUrlService_onTemplateURLServiceChanged(env, java_obj);
}

base::android::ScopedJavaLocalRef<jstring>
TemplateUrlServiceAndroid::GetUrlForSearchQuery(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& jquery) {
  const TemplateURL* default_provider =
      template_url_service_->GetDefaultSearchProvider();

  base::string16 query(base::android::ConvertJavaStringToUTF16(env, jquery));

  std::string url;
  if (default_provider &&
      default_provider->url_ref().SupportsReplacement(
          template_url_service_->search_terms_data()) &&
      !query.empty()) {
    url = default_provider->url_ref().ReplaceSearchTerms(
        TemplateURLRef::SearchTermsArgs(query),
        template_url_service_->search_terms_data());
  }

  return base::android::ConvertUTF8ToJavaString(env, url);
}

base::android::ScopedJavaLocalRef<jstring>
TemplateUrlServiceAndroid::GetUrlForVoiceSearchQuery(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& jquery) {
  base::string16 query(base::android::ConvertJavaStringToUTF16(env, jquery));
  std::string url;

  if (!query.empty()) {
    GURL gurl(GetDefaultSearchURLForSearchTerms(template_url_service_, query));
    if (google_util::IsGoogleSearchUrl(gurl))
      gurl = net::AppendQueryParameter(gurl, "inm", "vs");
    url = gurl.spec();
  }

  return base::android::ConvertUTF8ToJavaString(env, url);
}

base::android::ScopedJavaLocalRef<jstring>
TemplateUrlServiceAndroid::ReplaceSearchTermsInUrl(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& jquery,
    const JavaParamRef<jstring>& jcurrent_url) {
  const TemplateURL* default_provider =
      template_url_service_->GetDefaultSearchProvider();

  base::string16 query(base::android::ConvertJavaStringToUTF16(env, jquery));
  GURL current_url(base::android::ConvertJavaStringToUTF16(env, jcurrent_url));
  GURL destination_url(current_url);
  if (default_provider && !query.empty()) {
    bool refined_query = default_provider->ReplaceSearchTermsInURL(
        current_url, TemplateURLRef::SearchTermsArgs(query),
        template_url_service_->search_terms_data(), &destination_url);
    if (refined_query)
      return base::android::ConvertUTF8ToJavaString(
          env, destination_url.spec());
  }
  return base::android::ScopedJavaLocalRef<jstring>(env, NULL);
}

base::android::ScopedJavaLocalRef<jstring>
TemplateUrlServiceAndroid::GetUrlForContextualSearchQuery(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& jquery,
    const JavaParamRef<jstring>& jalternate_term,
    jboolean jshould_prefetch,
    const JavaParamRef<jstring>& jprotocol_version) {
  base::string16 query(base::android::ConvertJavaStringToUTF16(env, jquery));
  std::string url;

  if (!query.empty()) {
    GURL gurl(GetDefaultSearchURLForSearchTerms(template_url_service_, query));
    if (google_util::IsGoogleSearchUrl(gurl)) {
      std::string protocol_version(
          base::android::ConvertJavaStringToUTF8(env, jprotocol_version));
      gurl = net::AppendQueryParameter(gurl, "ctxs", protocol_version);
      if (jshould_prefetch) {
        // Indicate that the search page is being prefetched.
        gurl = net::AppendQueryParameter(gurl, "pf", "c");
      }

      if (jalternate_term) {
        std::string alternate_term(
            base::android::ConvertJavaStringToUTF8(env, jalternate_term));
        if (!alternate_term.empty()) {
          gurl = net::AppendQueryParameter(
              gurl, "ctxsl_alternate_term", alternate_term);
        }
      }
    }
    url = gurl.spec();
  }

  return base::android::ConvertUTF8ToJavaString(env, url);
}

base::android::ScopedJavaLocalRef<jstring>
TemplateUrlServiceAndroid::GetSearchEngineUrlFromTemplateUrl(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& jkeyword) {
  base::string16 keyword =
      base::android::ConvertJavaStringToUTF16(env, jkeyword);
  TemplateURL* template_url =
      template_url_service_->GetTemplateURLForKeyword(keyword);
  if (!template_url)
    return base::android::ScopedJavaLocalRef<jstring>(env, nullptr);
  std::string url(template_url->url_ref().ReplaceSearchTerms(
      TemplateURLRef::SearchTermsArgs(base::ASCIIToUTF16("query")),
      template_url_service_->search_terms_data()));
  return base::android::ConvertUTF8ToJavaString(env, url);
}

base::android::ScopedJavaLocalRef<jstring>
TemplateUrlServiceAndroid::AddSearchEngineForTesting(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj,
    const base::android::JavaParamRef<jstring>& jkeyword,
    jint age_in_days) {
  TemplateURLData data;
  base::string16 keyword =
      base::android::ConvertJavaStringToUTF16(env, jkeyword);
  data.SetShortName(keyword);
  data.SetKeyword(keyword);
  data.SetURL("http://testurl");
  data.favicon_url = GURL("http://favicon.url");
  data.safe_for_autoreplace = true;
  data.input_encodings.push_back("UTF-8");
  data.prepopulate_id = 0;
  data.date_created =
      base::Time::Now() - base::TimeDelta::FromDays((int) age_in_days);
  data.last_modified =
      base::Time::Now() - base::TimeDelta::FromDays((int) age_in_days);
  data.last_visited =
      base::Time::Now() - base::TimeDelta::FromDays((int) age_in_days);
  TemplateURL* t_url =
      template_url_service_->Add(base::MakeUnique<TemplateURL>(data));
  return base::android::ConvertUTF16ToJavaString(env, t_url->data().keyword());
}

base::android::ScopedJavaLocalRef<jstring>
TemplateUrlServiceAndroid::UpdateLastVisitedForTesting(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj,
    const base::android::JavaParamRef<jstring>& jkeyword) {
  base::string16 keyword =
      base::android::ConvertJavaStringToUTF16(env, jkeyword);
  TemplateURL* t_url = template_url_service_->GetTemplateURLForKeyword(keyword);
  template_url_service_->UpdateTemplateURLVisitTime(t_url);
  return base::android::ConvertUTF16ToJavaString(env, t_url->data().keyword());
}

static jlong Init(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  TemplateUrlServiceAndroid* template_url_service_android =
      new TemplateUrlServiceAndroid(env, obj);
  return reinterpret_cast<intptr_t>(template_url_service_android);
}

// static
bool TemplateUrlServiceAndroid::Register(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
