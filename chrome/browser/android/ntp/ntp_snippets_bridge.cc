// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/ntp/ntp_snippets_bridge.h"

#include <jni.h>
#include <utility>
#include <vector>

#include "base/android/callback_android.h"
#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/callback.h"
#include "base/feature_list.h"
#include "base/time/time.h"
#include "chrome/browser/android/chrome_feature_list.h"
#include "chrome/browser/android/ntp/content_suggestions_notifier_service.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/ntp_snippets/content_suggestions_notifier_service_factory.h"
#include "chrome/browser/ntp_snippets/content_suggestions_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_android.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/history/core/browser/history_service.h"
#include "components/ntp_snippets/content_suggestion.h"
#include "components/ntp_snippets/content_suggestions_metrics.h"
#include "components/ntp_snippets/remote/remote_suggestions_provider.h"
#include "components/ntp_snippets/remote/remote_suggestions_scheduler.h"
#include "jni/SnippetsBridge_jni.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/android/java_bitmap.h"
#include "ui/gfx/image/image.h"

using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::ConvertUTF16ToJavaString;
using base::android::JavaIntArrayToIntVector;
using base::android::AppendJavaStringArrayToStringVector;
using base::android::JavaParamRef;
using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;
using base::android::ToJavaIntArray;
using ntp_snippets::Category;
using ntp_snippets::CategoryInfo;
using ntp_snippets::CategoryStatus;
using ntp_snippets::KnownCategories;
using ntp_snippets::ContentSuggestion;

namespace {

// Converts a vector of ContentSuggestions to its Java equivalent.
ScopedJavaLocalRef<jobject> ToJavaSuggestionList(
    JNIEnv* env,
    const Category& category,
    const std::vector<ContentSuggestion>& suggestions) {
  ScopedJavaLocalRef<jobject> result =
      Java_SnippetsBridge_createSuggestionList(env);
  for (const ContentSuggestion& suggestion : suggestions) {
    ScopedJavaLocalRef<jobject> java_suggestion =
        Java_SnippetsBridge_addSuggestion(
            env, result, category.id(),
            ConvertUTF8ToJavaString(env, suggestion.id().id_within_category()),
            ConvertUTF16ToJavaString(env, suggestion.title()),
            ConvertUTF16ToJavaString(env, suggestion.publisher_name()),
            ConvertUTF16ToJavaString(env, suggestion.snippet_text()),
            ConvertUTF8ToJavaString(env, suggestion.url().spec()),
            suggestion.publish_date().ToJavaTime(), suggestion.score(),
            suggestion.fetch_date().ToJavaTime());
    if (suggestion.id().category().IsKnownCategory(
            KnownCategories::DOWNLOADS) &&
        suggestion.download_suggestion_extra() != nullptr) {
      if (suggestion.download_suggestion_extra()->is_download_asset) {
        Java_SnippetsBridge_setAssetDownloadDataForSuggestion(
            env, java_suggestion,
            ConvertUTF8ToJavaString(
                env, suggestion.download_suggestion_extra()->download_guid),
            ConvertUTF8ToJavaString(env, suggestion.download_suggestion_extra()
                                             ->target_file_path.value()),
            ConvertUTF8ToJavaString(
                env, suggestion.download_suggestion_extra()->mime_type));
      } else {
        Java_SnippetsBridge_setOfflinePageDownloadDataForSuggestion(
            env, java_suggestion,
            suggestion.download_suggestion_extra()->offline_page_id);
      }
    }
    if (suggestion.id().category().IsKnownCategory(
            KnownCategories::RECENT_TABS) &&
        suggestion.recent_tab_suggestion_extra() != nullptr) {
      Java_SnippetsBridge_setRecentTabDataForSuggestion(
          env, java_suggestion,
          suggestion.recent_tab_suggestion_extra()->tab_id,
          suggestion.recent_tab_suggestion_extra()->offline_page_id);
    }
  }

  return result;
}

ntp_snippets::RemoteSuggestionsScheduler* GetRemoteSuggestionsScheduler() {
  ntp_snippets::ContentSuggestionsService* content_suggestions_service =
      ContentSuggestionsServiceFactory::GetForProfile(
          ProfileManager::GetLastUsedProfile());
  // Can maybe be null in some cases? (Incognito profile?) crbug.com/647920
  if (!content_suggestions_service) {
    return nullptr;
  }
  return content_suggestions_service->remote_suggestions_scheduler();
}

}  // namespace

static jlong Init(JNIEnv* env,
                  const JavaParamRef<jobject>& j_bridge,
                  const JavaParamRef<jobject>& j_profile) {
  NTPSnippetsBridge* snippets_bridge =
      new NTPSnippetsBridge(env, j_bridge, j_profile);
  return reinterpret_cast<intptr_t>(snippets_bridge);
}

// Initiates a background fetch for remote suggestions.
static void RemoteSuggestionsSchedulerOnFetchDue(
    JNIEnv* env,
    const JavaParamRef<jclass>& caller) {
  ntp_snippets::RemoteSuggestionsScheduler* scheduler =
      GetRemoteSuggestionsScheduler();
  if (!scheduler) {
    return;
  }

  scheduler->OnPersistentSchedulerWakeUp();
}

// Reschedules the fetching of snippets. If tasks are already scheduled, they
// will be rescheduled anyway, so all running intervals will be reset.
static void RemoteSuggestionsSchedulerRescheduleFetching(
    JNIEnv* env,
    const JavaParamRef<jclass>& caller) {
  ntp_snippets::RemoteSuggestionsScheduler* scheduler =
      GetRemoteSuggestionsScheduler();
  // Can be null if the feature has been disabled but the scheduler has not been
  // unregistered yet. The next start should unregister it.
  if (!scheduler) {
    return;
  }

  scheduler->RescheduleFetching();
}

static void SetRemoteSuggestionsEnabled(JNIEnv* env,
                                        const JavaParamRef<jclass>& caller,
                                        jboolean enabled) {
  ntp_snippets::ContentSuggestionsService* content_suggestions_service =
      ContentSuggestionsServiceFactory::GetForProfile(
          ProfileManager::GetLastUsedProfile());
  if (!content_suggestions_service)
    return;

  content_suggestions_service->SetRemoteSuggestionsEnabled(enabled);
}

static jboolean AreRemoteSuggestionsEnabled(
    JNIEnv* env,
    const JavaParamRef<jclass>& caller) {
  ntp_snippets::ContentSuggestionsService* content_suggestions_service =
      ContentSuggestionsServiceFactory::GetForProfile(
          ProfileManager::GetLastUsedProfile());
  if (!content_suggestions_service)
    return false;

  return content_suggestions_service->AreRemoteSuggestionsEnabled();
}

// Returns true if the remote provider is managed by an adminstrator's policy.
static jboolean AreRemoteSuggestionsManaged(
    JNIEnv* env,
    const JavaParamRef<jclass>& caller) {
  ntp_snippets::ContentSuggestionsService* content_suggestions_service =
      ContentSuggestionsServiceFactory::GetForProfile(
          ProfileManager::GetLastUsedProfile());
  if (!content_suggestions_service)
    return false;

  return content_suggestions_service->AreRemoteSuggestionsManaged();
}

// Returns true if the remote provider is managed by a supervisor
static jboolean AreRemoteSuggestionsManagedByCustodian(
    JNIEnv* env,
    const JavaParamRef<jclass>& caller) {
  ntp_snippets::ContentSuggestionsService* content_suggestions_service =
      ContentSuggestionsServiceFactory::GetForProfile(
          ProfileManager::GetLastUsedProfile());
  if (!content_suggestions_service)
    return false;

  return content_suggestions_service->AreRemoteSuggestionsManagedByCustodian();
}

static void SetContentSuggestionsNotificationsEnabled(
    JNIEnv* env,
    const JavaParamRef<jclass>& caller,
    jboolean enabled) {
  ContentSuggestionsNotifierService* notifier_service =
      ContentSuggestionsNotifierServiceFactory::GetForProfile(
          ProfileManager::GetLastUsedProfile());
  if (!notifier_service)
    return;

  notifier_service->SetEnabled(enabled);
}

static jboolean AreContentSuggestionsNotificationsEnabled(
    JNIEnv* env,
    const JavaParamRef<jclass>& caller) {
  ContentSuggestionsNotifierService* notifier_service =
      ContentSuggestionsNotifierServiceFactory::GetForProfile(
          ProfileManager::GetLastUsedProfile());
  if (!notifier_service)
    return false;

  return notifier_service->IsEnabled();
}

NTPSnippetsBridge::NTPSnippetsBridge(JNIEnv* env,
                                     const JavaParamRef<jobject>& j_bridge,
                                     const JavaParamRef<jobject>& j_profile)
    : content_suggestions_service_observer_(this),
      bridge_(env, j_bridge),
      weak_ptr_factory_(this) {
  Profile* profile = ProfileAndroid::FromProfileAndroid(j_profile);
  content_suggestions_service_ =
      ContentSuggestionsServiceFactory::GetForProfile(profile);
  history_service_ = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  content_suggestions_service_observer_.Add(content_suggestions_service_);
}

void NTPSnippetsBridge::Destroy(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  delete this;
}

ScopedJavaLocalRef<jintArray> NTPSnippetsBridge::GetCategories(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  std::vector<int> category_ids;
  for (Category category : content_suggestions_service_->GetCategories()) {
    category_ids.push_back(category.id());
  }
  return ToJavaIntArray(env, category_ids);
}

int NTPSnippetsBridge::GetCategoryStatus(JNIEnv* env,
                                         const JavaParamRef<jobject>& obj,
                                         jint j_category_id) {
  return static_cast<int>(content_suggestions_service_->GetCategoryStatus(
      Category::FromIDValue(j_category_id)));
}

ScopedJavaLocalRef<jobject> NTPSnippetsBridge::GetCategoryInfo(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jint j_category_id) {
  base::Optional<CategoryInfo> info =
      content_suggestions_service_->GetCategoryInfo(
          Category::FromIDValue(j_category_id));
  if (!info) {
    return ScopedJavaLocalRef<jobject>(env, nullptr);
  }
  return Java_SnippetsBridge_createSuggestionsCategoryInfo(
      env, j_category_id, ConvertUTF16ToJavaString(env, info->title()),
      static_cast<int>(info->card_layout()),
      static_cast<int>(info->additional_action()), info->show_if_empty(),
      ConvertUTF16ToJavaString(env, info->no_suggestions_message()));
}

ScopedJavaLocalRef<jobject> NTPSnippetsBridge::GetSuggestionsForCategory(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jint j_category_id) {
  Category category = Category::FromIDValue(j_category_id);
  return ToJavaSuggestionList(
      env, category,
      content_suggestions_service_->GetSuggestionsForCategory(category));
}

void NTPSnippetsBridge::FetchSuggestionImage(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jint j_category_id,
    const JavaParamRef<jstring>& id_within_category,
    const JavaParamRef<jobject>& j_callback) {
  ScopedJavaGlobalRef<jobject> callback(j_callback);
  content_suggestions_service_->FetchSuggestionImage(
      ContentSuggestion::ID(Category::FromIDValue(j_category_id),
                            ConvertJavaStringToUTF8(env, id_within_category)),
      base::Bind(&NTPSnippetsBridge::OnImageFetched,
                 weak_ptr_factory_.GetWeakPtr(), callback));
}

void NTPSnippetsBridge::FetchSuggestionFavicon(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jint j_category_id,
    const JavaParamRef<jstring>& id_within_category,
    jint j_minimum_size_px,
    jint j_desired_size_px,
    const JavaParamRef<jobject>& j_callback) {
  ScopedJavaGlobalRef<jobject> callback(j_callback);
  content_suggestions_service_->FetchSuggestionFavicon(
      ContentSuggestion::ID(Category::FromIDValue(j_category_id),
                            ConvertJavaStringToUTF8(env, id_within_category)),
      j_minimum_size_px, j_desired_size_px,
      base::Bind(&NTPSnippetsBridge::OnImageFetched,
                 weak_ptr_factory_.GetWeakPtr(), callback));
}

void NTPSnippetsBridge::Fetch(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jint j_category_id,
    const JavaParamRef<jobjectArray>& j_displayed_suggestions) {
  std::vector<std::string> known_suggestion_ids;
  AppendJavaStringArrayToStringVector(env, j_displayed_suggestions,
                                      &known_suggestion_ids);

  Category category = Category::FromIDValue(j_category_id);
  content_suggestions_service_->Fetch(
      category,
      std::set<std::string>(known_suggestion_ids.begin(),
                            known_suggestion_ids.end()),
      base::Bind(&NTPSnippetsBridge::OnSuggestionsFetched,
                 weak_ptr_factory_.GetWeakPtr(), category));
}

void NTPSnippetsBridge::FetchContextualSuggestions(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& j_url,
    const JavaParamRef<jobject>& j_callback) {
  DCHECK(base::FeatureList::IsEnabled(
      chrome::android::kContextualSuggestionsCarousel));

  // We don't currently have a contextual suggestions service or provider, so
  // we use articles as placeholders.
  Category category = Category::FromKnownCategory(KnownCategories::ARTICLES);
  auto suggestions = ToJavaSuggestionList(
      env, category,
      content_suggestions_service_->GetSuggestionsForCategory(category));

  // We would eventually have to hit the network or a database, so let's
  // pretend here the call is asynchronous.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](const base::android::JavaRef<jobject>& j_callback,
                        const base::android::JavaRef<jobject>& j_suggestions) {
                       RunCallbackAndroid(j_callback, j_suggestions);
                     },
                     ScopedJavaGlobalRef<jobject>(j_callback),
                     ScopedJavaGlobalRef<jobject>(suggestions)));
}

void NTPSnippetsBridge::ReloadSuggestions(JNIEnv* env,
                                          const JavaParamRef<jobject>& obj) {
  content_suggestions_service_->ReloadSuggestions();
}

void NTPSnippetsBridge::DismissSuggestion(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& jurl,
    jint global_position,
    jint j_category_id,
    jint position_in_category,
    const JavaParamRef<jstring>& id_within_category) {
  Category category = Category::FromIDValue(j_category_id);

  content_suggestions_service_->DismissSuggestion(ContentSuggestion::ID(
      category, ConvertJavaStringToUTF8(env, id_within_category)));

  history_service_->QueryURL(
      GURL(ConvertJavaStringToUTF8(env, jurl)), /*want_visits=*/false,
      base::Bind(
          [](int global_position, Category category, int position_in_category,
             bool success, const history::URLRow& row,
             const history::VisitVector& visit_vector) {
            bool visited = success && row.visit_count() != 0;
            ntp_snippets::metrics::OnSuggestionDismissed(
                global_position, category, position_in_category, visited);
          },
          global_position, category, position_in_category),
      &tracker_);
}

void NTPSnippetsBridge::DismissCategory(JNIEnv* env,
                                        const JavaParamRef<jobject>& obj,
                                        jint j_category_id) {
  Category category = Category::FromIDValue(j_category_id);

  content_suggestions_service_->DismissCategory(category);

  ntp_snippets::metrics::OnCategoryDismissed(category);
}

void NTPSnippetsBridge::RestoreDismissedCategories(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  content_suggestions_service_->RestoreDismissedCategories();
}

NTPSnippetsBridge::~NTPSnippetsBridge() {}

void NTPSnippetsBridge::OnNewSuggestions(Category category) {
  JNIEnv* env = AttachCurrentThread();
  Java_SnippetsBridge_onNewSuggestions(env, bridge_,
                                       static_cast<int>(category.id()));
}

void NTPSnippetsBridge::OnCategoryStatusChanged(Category category,
                                                CategoryStatus new_status) {
  JNIEnv* env = AttachCurrentThread();
  Java_SnippetsBridge_onCategoryStatusChanged(env, bridge_,
                                              static_cast<int>(category.id()),
                                              static_cast<int>(new_status));
}

void NTPSnippetsBridge::OnSuggestionInvalidated(
    const ContentSuggestion::ID& suggestion_id) {
  JNIEnv* env = AttachCurrentThread();
  Java_SnippetsBridge_onSuggestionInvalidated(
      env, bridge_.obj(), static_cast<int>(suggestion_id.category().id()),
      ConvertUTF8ToJavaString(env, suggestion_id.id_within_category()).obj());
}

void NTPSnippetsBridge::OnFullRefreshRequired() {
  JNIEnv* env = AttachCurrentThread();
  Java_SnippetsBridge_onFullRefreshRequired(env, bridge_.obj());
}

void NTPSnippetsBridge::ContentSuggestionsServiceShutdown() {
  bridge_.Reset();
  content_suggestions_service_observer_.Remove(content_suggestions_service_);
}

void NTPSnippetsBridge::OnImageFetched(ScopedJavaGlobalRef<jobject> callback,
                                       const gfx::Image& image) {
  ScopedJavaLocalRef<jobject> j_bitmap;
  if (!image.IsEmpty()) {
    j_bitmap = gfx::ConvertToJavaBitmap(image.ToSkBitmap());
  }
  RunCallbackAndroid(callback, j_bitmap);
}

void NTPSnippetsBridge::OnSuggestionsFetched(
    Category category,
    ntp_snippets::Status status,
    std::vector<ContentSuggestion> suggestions) {
  // TODO(fhorschig, dgn): Allow refetch or show notification acc. to status.
  JNIEnv* env = AttachCurrentThread();
  Java_SnippetsBridge_onMoreSuggestions(
      env, bridge_, category.id(),
      ToJavaSuggestionList(env, category, suggestions));
}

// static
bool NTPSnippetsBridge::Register(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
