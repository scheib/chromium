// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_snippets/remote/remote_suggestions_provider_impl.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/default_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/data_use_measurement/core/data_use_user_data.h"
#include "components/image_fetcher/core/image_decoder.h"
#include "components/image_fetcher/core/image_fetcher.h"
#include "components/ntp_snippets/category_rankers/category_ranker.h"
#include "components/ntp_snippets/features.h"
#include "components/ntp_snippets/pref_names.h"
#include "components/ntp_snippets/remote/remote_suggestions_database.h"
#include "components/ntp_snippets/remote/remote_suggestions_scheduler.h"
#include "components/ntp_snippets/switches.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/strings/grit/components_strings.h"
#include "components/variations/variations_associated_data.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"

namespace ntp_snippets {

namespace {

// Number of suggestions requested to the server. Consider replacing sparse UMA
// histograms with COUNTS() if this number increases beyond 50.
const int kMaxSuggestionCount = 10;

// Number of archived suggestions we keep around in memory.
const int kMaxArchivedSuggestionCount = 200;

// Keys for storing CategoryContent info in prefs.
const char kCategoryContentId[] = "id";
const char kCategoryContentTitle[] = "title";
const char kCategoryContentProvidedByServer[] = "provided_by_server";
const char kCategoryContentAllowFetchingMore[] = "allow_fetching_more";

// Variation parameter for ordering new remote categories based on their
// position in the response relative to "Article for you" category.
const char kOrderNewRemoteCategoriesBasedOnArticlesCategory[] =
    "order_new_remote_categories_based_on_articles_category";

bool IsOrderingNewRemoteCategoriesBasedOnArticlesCategoryEnabled() {
  return variations::GetVariationParamByFeatureAsBool(
      ntp_snippets::kArticleSuggestionsFeature,
      kOrderNewRemoteCategoriesBasedOnArticlesCategory,
      /*default_value=*/false);
}

void AddFetchedCategoriesToRankerBasedOnArticlesCategory(
    CategoryRanker* ranker,
    const RemoteSuggestionsFetcher::FetchedCategoriesVector& fetched_categories,
    Category articles_category) {
  DCHECK(IsOrderingNewRemoteCategoriesBasedOnArticlesCategoryEnabled());
  // Insert categories which precede "Articles" in the response.
  for (const RemoteSuggestionsFetcher::FetchedCategory& fetched_category :
       fetched_categories) {
    if (fetched_category.category == articles_category) {
      break;
    }
    ranker->InsertCategoryBeforeIfNecessary(fetched_category.category,
                                            articles_category);
  }
  // Insert categories which follow "Articles" in the response. Note that we
  // insert them in reversed order, because they are inserted right after
  // "Articles", which reverses the order.
  for (auto fetched_category_it = fetched_categories.rbegin();
       fetched_category_it != fetched_categories.rend();
       ++fetched_category_it) {
    if (fetched_category_it->category == articles_category) {
      return;
    }
    ranker->InsertCategoryAfterIfNecessary(fetched_category_it->category,
                                           articles_category);
  }
  NOTREACHED() << "Articles category was not found.";
}

template <typename SuggestionPtrContainer>
std::unique_ptr<std::vector<std::string>> GetSuggestionIDVector(
    const SuggestionPtrContainer& suggestions) {
  auto result = base::MakeUnique<std::vector<std::string>>();
  for (const auto& suggestion : suggestions) {
    result->push_back(suggestion->id());
  }
  return result;
}

bool HasIntersection(const std::vector<std::string>& a,
                     const std::set<std::string>& b) {
  for (const std::string& item : a) {
    if (base::ContainsValue(b, item)) {
      return true;
    }
  }
  return false;
}

void EraseByPrimaryID(RemoteSuggestion::PtrVector* suggestions,
                      const std::vector<std::string>& ids) {
  std::set<std::string> ids_lookup(ids.begin(), ids.end());
  base::EraseIf(
      *suggestions,
      [&ids_lookup](const std::unique_ptr<RemoteSuggestion>& suggestion) {
        return base::ContainsValue(ids_lookup, suggestion->id());
      });
}

void EraseMatchingSuggestions(
    RemoteSuggestion::PtrVector* suggestions,
    const RemoteSuggestion::PtrVector& compare_against) {
  std::set<std::string> compare_against_ids;
  for (const std::unique_ptr<RemoteSuggestion>& suggestion : compare_against) {
    const std::vector<std::string>& suggestion_ids = suggestion->GetAllIDs();
    compare_against_ids.insert(suggestion_ids.begin(), suggestion_ids.end());
  }
  base::EraseIf(
      *suggestions, [&compare_against_ids](
                        const std::unique_ptr<RemoteSuggestion>& suggestion) {
        return HasIntersection(suggestion->GetAllIDs(), compare_against_ids);
      });
}

void RemoveNullPointers(RemoteSuggestion::PtrVector* suggestions) {
  base::EraseIf(*suggestions,
                [](const std::unique_ptr<RemoteSuggestion>& suggestion) {
                  return !suggestion;
                });
}

void RemoveIncompleteSuggestions(RemoteSuggestion::PtrVector* suggestions) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kAddIncompleteSnippets)) {
    return;
  }
  int num_suggestions = suggestions->size();
  // Remove suggestions that do not have all the info we need to display it to
  // the user.
  base::EraseIf(*suggestions,
                [](const std::unique_ptr<RemoteSuggestion>& suggestion) {
                  return !suggestion->is_complete();
                });
  int num_suggestions_removed = num_suggestions - suggestions->size();
  UMA_HISTOGRAM_BOOLEAN("NewTabPage.Snippets.IncompleteSnippetsAfterFetch",
                        num_suggestions_removed > 0);
  if (num_suggestions_removed > 0) {
    UMA_HISTOGRAM_SPARSE_SLOWLY("NewTabPage.Snippets.NumIncompleteSnippets",
                                num_suggestions_removed);
  }
}

std::vector<ContentSuggestion> ConvertToContentSuggestions(
    Category category,
    const RemoteSuggestion::PtrVector& suggestions) {
  std::vector<ContentSuggestion> result;
  for (const std::unique_ptr<RemoteSuggestion>& suggestion : suggestions) {
    // TODO(sfiera): if a suggestion is not going to be displayed, move it
    // directly to content.dismissed on fetch. Otherwise, we might prune
    // other suggestions to get down to kMaxSuggestionCount, only to hide one of
    // the incomplete ones we kept.
    if (!suggestion->is_complete()) {
      continue;
    }
    result.emplace_back(suggestion->ToContentSuggestion(category));
  }
  return result;
}

void CallWithEmptyResults(const FetchDoneCallback& callback,
                          const Status& status) {
  if (callback.is_null()) {
    return;
  }
  callback.Run(status, std::vector<ContentSuggestion>());
}

}  // namespace

CachedImageFetcher::CachedImageFetcher(
    std::unique_ptr<image_fetcher::ImageFetcher> image_fetcher,
    PrefService* pref_service,
    RemoteSuggestionsDatabase* database)
    : image_fetcher_(std::move(image_fetcher)),
      database_(database),
      thumbnail_requests_throttler_(
          pref_service,
          RequestThrottler::RequestType::CONTENT_SUGGESTION_THUMBNAIL) {
  // |image_fetcher_| can be null in tests.
  if (image_fetcher_) {
    image_fetcher_->SetImageFetcherDelegate(this);
    image_fetcher_->SetDataUseServiceName(
        data_use_measurement::DataUseUserData::NTP_SNIPPETS_THUMBNAILS);
  }
}

CachedImageFetcher::~CachedImageFetcher() {}

void CachedImageFetcher::FetchSuggestionImage(
    const ContentSuggestion::ID& suggestion_id,
    const GURL& url,
    const ImageFetchedCallback& callback) {
  database_->LoadImage(
      suggestion_id.id_within_category(),
      base::Bind(&CachedImageFetcher::OnImageFetchedFromDatabase,
                 base::Unretained(this), callback, suggestion_id, url));
}

// This function gets only called for caching the image data received from the
// network. The actual decoding is done in OnImageDecodedFromDatabase().
void CachedImageFetcher::OnImageDataFetched(
    const std::string& id_within_category,
    const std::string& image_data) {
  if (image_data.empty()) {
    return;
  }
  database_->SaveImage(id_within_category, image_data);
}

void CachedImageFetcher::OnImageDecodingDone(
    const ImageFetchedCallback& callback,
    const std::string& id_within_category,
    const gfx::Image& image,
    const image_fetcher::RequestMetadata& metadata) {
  callback.Run(image);
}

void CachedImageFetcher::OnImageFetchedFromDatabase(
    const ImageFetchedCallback& callback,
    const ContentSuggestion::ID& suggestion_id,
    const GURL& url,
    std::string data) {  // SnippetImageCallback requires by-value.
  // The image decoder is null in tests.
  if (image_fetcher_->GetImageDecoder() && !data.empty()) {
    image_fetcher_->GetImageDecoder()->DecodeImage(
        data,
        // We're not dealing with multi-frame images.
        /*desired_image_frame_size=*/gfx::Size(),
        base::Bind(&CachedImageFetcher::OnImageDecodedFromDatabase,
                   base::Unretained(this), callback, suggestion_id, url));
    return;
  }
  // Fetching from the DB failed; start a network fetch.
  FetchImageFromNetwork(suggestion_id, url, callback);
}

void CachedImageFetcher::OnImageDecodedFromDatabase(
    const ImageFetchedCallback& callback,
    const ContentSuggestion::ID& suggestion_id,
    const GURL& url,
    const gfx::Image& image) {
  if (!image.IsEmpty()) {
    callback.Run(image);
    return;
  }
  // If decoding the image failed, delete the DB entry.
  database_->DeleteImage(suggestion_id.id_within_category());
  FetchImageFromNetwork(suggestion_id, url, callback);
}

void CachedImageFetcher::FetchImageFromNetwork(
    const ContentSuggestion::ID& suggestion_id,
    const GURL& url,
    const ImageFetchedCallback& callback) {
  if (url.is_empty() || !thumbnail_requests_throttler_.DemandQuotaForRequest(
                            /*interactive_request=*/true)) {
    // Return an empty image. Directly, this is never synchronous with the
    // original FetchSuggestionImage() call - an asynchronous database query has
    // happened in the meantime.
    callback.Run(gfx::Image());
    return;
  }

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("remote_suggestions_provider", R"(
        semantics {
          sender: "Content Suggestion Thumbnail Fetch"
          description:
            "Retrieves thumbnails for content suggestions, for display on the "
            "New Tab page or Chrome Home."
          trigger:
            "Triggered when the user looks at a content suggestion (and its "
            "thumbnail isn't cached yet)."
          data: "None."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: false
          setting: "Currently not available, but in progress: crbug.com/703684"
        chrome_policy {
          NTPContentSuggestionsEnabled {
            policy_options {mode: MANDATORY}
            NTPContentSuggestionsEnabled: false
          }
        }
      })");
  image_fetcher_->StartOrQueueNetworkRequest(
      suggestion_id.id_within_category(), url,
      base::Bind(&CachedImageFetcher::OnImageDecodingDone,
                 base::Unretained(this), callback),
      traffic_annotation);
}

RemoteSuggestionsProviderImpl::RemoteSuggestionsProviderImpl(
    Observer* observer,
    PrefService* pref_service,
    const std::string& application_language_code,
    CategoryRanker* category_ranker,
    RemoteSuggestionsScheduler* scheduler,
    std::unique_ptr<RemoteSuggestionsFetcher> suggestions_fetcher,
    std::unique_ptr<image_fetcher::ImageFetcher> image_fetcher,
    std::unique_ptr<RemoteSuggestionsDatabase> database,
    std::unique_ptr<RemoteSuggestionsStatusService> status_service)
    : RemoteSuggestionsProvider(observer),
      state_(State::NOT_INITED),
      pref_service_(pref_service),
      articles_category_(
          Category::FromKnownCategory(KnownCategories::ARTICLES)),
      application_language_code_(application_language_code),
      category_ranker_(category_ranker),
      remote_suggestions_scheduler_(scheduler),
      suggestions_fetcher_(std::move(suggestions_fetcher)),
      database_(std::move(database)),
      image_fetcher_(std::move(image_fetcher), pref_service, database_.get()),
      status_service_(std::move(status_service)),
      fetch_when_ready_(false),
      fetch_when_ready_interactive_(false),
      clear_history_dependent_state_when_initialized_(false),
      clock_(base::MakeUnique<base::DefaultClock>()) {
  RestoreCategoriesFromPrefs();
  // The articles category always exists. Add it if we didn't get it from prefs.
  // TODO(treib): Rethink this.
  category_contents_.insert(
      std::make_pair(articles_category_,
                     CategoryContent(BuildArticleCategoryInfo(base::nullopt))));
  // Tell the observer about all the categories.
  for (const auto& entry : category_contents_) {
    observer->OnCategoryStatusChanged(this, entry.first, entry.second.status);
  }

  if (database_->IsErrorState()) {
    EnterState(State::ERROR_OCCURRED);
    UpdateAllCategoryStatus(CategoryStatus::LOADING_ERROR);
    return;
  }

  database_->SetErrorCallback(base::Bind(
      &RemoteSuggestionsProviderImpl::OnDatabaseError, base::Unretained(this)));

  // We transition to other states while finalizing the initialization, when the
  // database is done loading.
  database_load_start_ = base::TimeTicks::Now();
  database_->LoadSnippets(
      base::Bind(&RemoteSuggestionsProviderImpl::OnDatabaseLoaded,
                 base::Unretained(this)));
}

RemoteSuggestionsProviderImpl::~RemoteSuggestionsProviderImpl() = default;

// static
void RemoteSuggestionsProviderImpl::RegisterProfilePrefs(
    PrefRegistrySimple* registry) {
  registry->RegisterListPref(prefs::kRemoteSuggestionCategories);
  registry->RegisterInt64Pref(prefs::kLastSuccessfulBackgroundFetchTime, 0);

  RemoteSuggestionsStatusService::RegisterProfilePrefs(registry);
}

void RemoteSuggestionsProviderImpl::ReloadSuggestions() {
  if (!remote_suggestions_scheduler_->AcquireQuotaForInteractiveFetch()) {
    return;
  }
  FetchSuggestions(
      /*interactive_request=*/true,
      base::Bind(
          [](RemoteSuggestionsScheduler* scheduler, Status status_code) {
            scheduler->OnInteractiveFetchFinished(status_code);
          },
          base::Unretained(remote_suggestions_scheduler_)));
}

void RemoteSuggestionsProviderImpl::RefetchInTheBackground(
    const FetchStatusCallback& callback) {
  FetchSuggestions(/*interactive_request=*/false, callback);
}

const RemoteSuggestionsFetcher*
RemoteSuggestionsProviderImpl::suggestions_fetcher_for_debugging() const {
  return suggestions_fetcher_.get();
}

GURL RemoteSuggestionsProviderImpl::GetUrlWithFavicon(
    const ContentSuggestion::ID& suggestion_id) const {
  DCHECK(base::ContainsKey(category_contents_, suggestion_id.category()));

  const CategoryContent& content =
      category_contents_.at(suggestion_id.category());
  const RemoteSuggestion* suggestion =
      content.FindSuggestion(suggestion_id.id_within_category());
  if (!suggestion) {
    return GURL();
  }
  return ContentSuggestion::GetFaviconDomain(suggestion->url());
}

void RemoteSuggestionsProviderImpl::FetchSuggestions(
    bool interactive_request,
    const FetchStatusCallback& callback) {
  if (!ready()) {
    fetch_when_ready_ = true;
    fetch_when_ready_interactive_ = interactive_request;
    fetch_when_ready_callback_ = callback;
    return;
  }

  MarkEmptyCategoriesAsLoading();

  RequestParams params = BuildFetchParams();
  params.interactive_request = interactive_request;
  suggestions_fetcher_->FetchSnippets(
      params,
      base::BindOnce(&RemoteSuggestionsProviderImpl::OnFetchFinished,
                     base::Unretained(this), callback, interactive_request));
}

void RemoteSuggestionsProviderImpl::Fetch(
    const Category& category,
    const std::set<std::string>& known_suggestion_ids,
    const FetchDoneCallback& callback) {
  if (!ready()) {
    CallWithEmptyResults(callback,
                         Status(StatusCode::TEMPORARY_ERROR,
                                "RemoteSuggestionsProvider is not ready!"));
    return;
  }
  if (!remote_suggestions_scheduler_->AcquireQuotaForInteractiveFetch()) {
    CallWithEmptyResults(callback, Status(StatusCode::TEMPORARY_ERROR,
                                          "Interactive quota exceeded!"));
    return;
  }
  // Make sure after the fetch, the scheduler is informed about the status.
  FetchDoneCallback callback_wrapper = base::Bind(
      [](RemoteSuggestionsScheduler* scheduler,
         const FetchDoneCallback& callback, Status status_code,
         std::vector<ContentSuggestion> suggestions) {
        scheduler->OnInteractiveFetchFinished(status_code);
        callback.Run(status_code, std::move(suggestions));
      },
      base::Unretained(remote_suggestions_scheduler_), callback);

  RequestParams params = BuildFetchParams();
  params.excluded_ids.insert(known_suggestion_ids.begin(),
                             known_suggestion_ids.end());
  params.interactive_request = true;
  params.exclusive_category = category;

  suggestions_fetcher_->FetchSnippets(
      params,
      base::BindOnce(&RemoteSuggestionsProviderImpl::OnFetchMoreFinished,
                     base::Unretained(this), callback_wrapper));
}

// Builds default fetcher params.
RequestParams RemoteSuggestionsProviderImpl::BuildFetchParams() const {
  RequestParams result;
  result.language_code = application_language_code_;
  result.count_to_fetch = kMaxSuggestionCount;
  for (const auto& map_entry : category_contents_) {
    const CategoryContent& content = map_entry.second;
    for (const auto& dismissed_suggestion : content.dismissed) {
      result.excluded_ids.insert(dismissed_suggestion->id());
    }
  }
  return result;
}

void RemoteSuggestionsProviderImpl::MarkEmptyCategoriesAsLoading() {
  for (const auto& item : category_contents_) {
    Category category = item.first;
    const CategoryContent& content = item.second;
    if (content.suggestions.empty()) {
      UpdateCategoryStatus(category, CategoryStatus::AVAILABLE_LOADING);
    }
  }
}

CategoryStatus RemoteSuggestionsProviderImpl::GetCategoryStatus(
    Category category) {
  auto content_it = category_contents_.find(category);
  DCHECK(content_it != category_contents_.end());
  return content_it->second.status;
}

CategoryInfo RemoteSuggestionsProviderImpl::GetCategoryInfo(Category category) {
  auto content_it = category_contents_.find(category);
  DCHECK(content_it != category_contents_.end());
  return content_it->second.info;
}

void RemoteSuggestionsProviderImpl::DismissSuggestion(
    const ContentSuggestion::ID& suggestion_id) {
  if (!ready()) {
    return;
  }

  auto content_it = category_contents_.find(suggestion_id.category());
  DCHECK(content_it != category_contents_.end());
  CategoryContent* content = &content_it->second;
  DismissSuggestionFromCategoryContent(content,
                                       suggestion_id.id_within_category());
}

void RemoteSuggestionsProviderImpl::ClearHistory(
    base::Time begin,
    base::Time end,
    const base::Callback<bool(const GURL& url)>& filter) {
  // Both time range and the filter are ignored and all suggestions are removed,
  // because it is not known which history entries were used for the suggestions
  // personalization.
  ClearHistoryDependentState();
}

void RemoteSuggestionsProviderImpl::ClearCachedSuggestions(Category category) {
  if (!initialized()) {
    return;
  }

  auto content_it = category_contents_.find(category);
  if (content_it == category_contents_.end()) {
    return;
  }
  CategoryContent* content = &content_it->second;
  // TODO(tschumann): We do the unnecessary checks for .empty() in many places
  // before calling database methods. Change the RemoteSuggestionsDatabase to
  // return early for those and remove the many if statements in this file.
  if (!content->suggestions.empty()) {
    database_->DeleteSnippets(GetSuggestionIDVector(content->suggestions));
    database_->DeleteImages(GetSuggestionIDVector(content->suggestions));
    content->suggestions.clear();
  }
  if (!content->archived.empty()) {
    database_->DeleteSnippets(GetSuggestionIDVector(content->archived));
    database_->DeleteImages(GetSuggestionIDVector(content->archived));
    content->archived.clear();
  }
}

void RemoteSuggestionsProviderImpl::OnSignInStateChanged() {
  // Make sure the status service is registered and we already initialised its
  // start state.
  if (!initialized()) {
    return;
  }

  status_service_->OnSignInStateChanged();
}

void RemoteSuggestionsProviderImpl::GetDismissedSuggestionsForDebugging(
    Category category,
    const DismissedSuggestionsCallback& callback) {
  auto content_it = category_contents_.find(category);
  DCHECK(content_it != category_contents_.end());
  callback.Run(
      ConvertToContentSuggestions(category, content_it->second.dismissed));
}

void RemoteSuggestionsProviderImpl::ClearDismissedSuggestionsForDebugging(
    Category category) {
  auto content_it = category_contents_.find(category);
  DCHECK(content_it != category_contents_.end());
  CategoryContent* content = &content_it->second;

  if (!initialized()) {
    return;
  }

  if (content->dismissed.empty()) {
    return;
  }

  database_->DeleteSnippets(GetSuggestionIDVector(content->dismissed));
  // The image got already deleted when the suggestion was dismissed.

  content->dismissed.clear();
}

// static
int RemoteSuggestionsProviderImpl::GetMaxSuggestionCountForTesting() {
  return kMaxSuggestionCount;
}

////////////////////////////////////////////////////////////////////////////////
// Private methods

GURL RemoteSuggestionsProviderImpl::FindSuggestionImageUrl(
    const ContentSuggestion::ID& suggestion_id) const {
  DCHECK(base::ContainsKey(category_contents_, suggestion_id.category()));

  const CategoryContent& content =
      category_contents_.at(suggestion_id.category());
  const RemoteSuggestion* suggestion =
      content.FindSuggestion(suggestion_id.id_within_category());
  if (!suggestion) {
    return GURL();
  }
  return suggestion->salient_image_url();
}

void RemoteSuggestionsProviderImpl::OnDatabaseLoaded(
    RemoteSuggestion::PtrVector suggestions) {
  if (state_ == State::ERROR_OCCURRED) {
    return;
  }
  DCHECK(state_ == State::NOT_INITED);
  DCHECK(base::ContainsKey(category_contents_, articles_category_));

  base::TimeDelta database_load_time =
      base::TimeTicks::Now() - database_load_start_;
  UMA_HISTOGRAM_MEDIUM_TIMES("NewTabPage.Snippets.DatabaseLoadTime",
                             database_load_time);

  RemoteSuggestion::PtrVector to_delete;
  for (std::unique_ptr<RemoteSuggestion>& suggestion : suggestions) {
    Category suggestion_category =
        Category::FromRemoteCategory(suggestion->remote_category_id());
    auto content_it = category_contents_.find(suggestion_category);
    // We should already know about the category.
    if (content_it == category_contents_.end()) {
      DLOG(WARNING) << "Loaded a suggestion for unknown category "
                    << suggestion_category << " from the DB; deleting";
      to_delete.emplace_back(std::move(suggestion));
      continue;
    }
    CategoryContent* content = &content_it->second;
    if (suggestion->is_dismissed()) {
      content->dismissed.emplace_back(std::move(suggestion));
    } else {
      content->suggestions.emplace_back(std::move(suggestion));
    }
  }
  if (!to_delete.empty()) {
    database_->DeleteSnippets(GetSuggestionIDVector(to_delete));
    database_->DeleteImages(GetSuggestionIDVector(to_delete));
  }

  // Sort the suggestions in each category.
  // TODO(treib): Persist the actual order in the DB somehow? crbug.com/654409
  for (auto& entry : category_contents_) {
    CategoryContent* content = &entry.second;
    std::sort(content->suggestions.begin(), content->suggestions.end(),
              [](const std::unique_ptr<RemoteSuggestion>& lhs,
                 const std::unique_ptr<RemoteSuggestion>& rhs) {
                return lhs->score() > rhs->score();
              });
  }

  // TODO(tschumann): If I move ClearExpiredDismissedSuggestions() to the
  // beginning of the function, it essentially does nothing but tests are still
  // green. Fix this!
  ClearExpiredDismissedSuggestions();
  ClearOrphanedImages();
  FinishInitialization();
}

void RemoteSuggestionsProviderImpl::OnDatabaseError() {
  EnterState(State::ERROR_OCCURRED);
  UpdateAllCategoryStatus(CategoryStatus::LOADING_ERROR);
}

void RemoteSuggestionsProviderImpl::OnFetchMoreFinished(
    const FetchDoneCallback& fetching_callback,
    Status status,
    RemoteSuggestionsFetcher::OptionalFetchedCategories fetched_categories) {
  if (!fetched_categories) {
    DCHECK(!status.IsSuccess());
    CallWithEmptyResults(fetching_callback, status);
    return;
  }
  if (fetched_categories->size() != 1u) {
    LOG(DFATAL) << "Requested one exclusive category but received "
                << fetched_categories->size() << " categories.";
    CallWithEmptyResults(fetching_callback,
                         Status(StatusCode::PERMANENT_ERROR,
                                "RemoteSuggestionsProvider received more "
                                "categories than requested."));
    return;
  }
  auto& fetched_category = (*fetched_categories)[0];
  Category category = fetched_category.category;
  CategoryContent* existing_content =
      UpdateCategoryInfo(category, fetched_category.info);
  SanitizeReceivedSuggestions(existing_content->dismissed,
                              &fetched_category.suggestions);
  std::vector<ContentSuggestion> result =
      ConvertToContentSuggestions(category, fetched_category.suggestions);
  // Store the additional suggestions into the archive to be able to fetch
  // images and favicons for them. Note that ArchiveSuggestions clears
  // |fetched_category.suggestions|.
  ArchiveSuggestions(existing_content, &fetched_category.suggestions);

  // TODO(tschumann): We should properly honor the existing category state,
  // e.g. to make sure we don't serve results after the sign-out. Revisit this:
  // Should Nuke also cancel outstanding requests, or do we want to check the
  // status?
  UpdateCategoryStatus(category, CategoryStatus::AVAILABLE);
  fetching_callback.Run(Status::Success(), std::move(result));
}

void RemoteSuggestionsProviderImpl::OnFetchFinished(
    const FetchStatusCallback& callback,
    bool interactive_request,
    Status status,
    RemoteSuggestionsFetcher::OptionalFetchedCategories fetched_categories) {
  if (!ready()) {
    // TODO(tschumann): What happens if this was a user-triggered, interactive
    // request? Is the UI waiting indefinitely now?
    return;
  }

  // Record the fetch time of a successfull background fetch.
  if (!interactive_request && status.IsSuccess()) {
    pref_service_->SetInt64(prefs::kLastSuccessfulBackgroundFetchTime,
                            clock_->Now().ToInternalValue());
  }

  // Mark all categories as not provided by the server in the latest fetch. The
  // ones we got will be marked again below.
  for (auto& item : category_contents_) {
    CategoryContent* content = &item.second;
    content->included_in_last_server_response = false;
  }

  // Clear up expired dismissed suggestions before we use them to filter new
  // ones.
  ClearExpiredDismissedSuggestions();

  // If suggestions were fetched successfully, update our |category_contents_|
  // from
  // each category provided by the server.
  if (fetched_categories) {
    // TODO(treib): Reorder |category_contents_| to match the order we received
    // from the server. crbug.com/653816
    bool response_includes_article_category = false;
    for (RemoteSuggestionsFetcher::FetchedCategory& fetched_category :
         *fetched_categories) {
      // TODO(tschumann): Remove this histogram once we only talk to the content
      // suggestions cloud backend.
      if (fetched_category.category == articles_category_) {
        UMA_HISTOGRAM_SPARSE_SLOWLY(
            "NewTabPage.Snippets.NumArticlesFetched",
            std::min(fetched_category.suggestions.size(),
                     static_cast<size_t>(kMaxSuggestionCount + 1)));
        response_includes_article_category = true;
      }

      CategoryContent* content =
          UpdateCategoryInfo(fetched_category.category, fetched_category.info);
      content->included_in_last_server_response = true;
      SanitizeReceivedSuggestions(content->dismissed,
                                  &fetched_category.suggestions);
      IntegrateSuggestions(content, std::move(fetched_category.suggestions));
    }

    // Add new remote categories to the ranker.
    if (IsOrderingNewRemoteCategoriesBasedOnArticlesCategoryEnabled() &&
        response_includes_article_category) {
      AddFetchedCategoriesToRankerBasedOnArticlesCategory(
          category_ranker_, *fetched_categories, articles_category_);
    } else {
      for (const RemoteSuggestionsFetcher::FetchedCategory& fetched_category :
           *fetched_categories) {
        category_ranker_->AppendCategoryIfNecessary(fetched_category.category);
      }
    }
  }

  // TODO(tschumann): The suggestions fetcher needs to signal errors so that we
  // know why we received no data. If an error occured, none of the following
  // should take place.

  // We might have gotten new categories (or updated the titles of existing
  // ones), so update the pref.
  StoreCategoriesToPrefs();

  for (const auto& item : category_contents_) {
    Category category = item.first;
    UpdateCategoryStatus(category, CategoryStatus::AVAILABLE);
    // TODO(sfiera): notify only when a category changed above.
    NotifyNewSuggestions(category, item.second);
  }

  // TODO(sfiera): equivalent metrics for non-articles.
  auto content_it = category_contents_.find(articles_category_);
  DCHECK(content_it != category_contents_.end());
  const CategoryContent& content = content_it->second;
  UMA_HISTOGRAM_SPARSE_SLOWLY("NewTabPage.Snippets.NumArticles",
                              content.suggestions.size());
  if (content.suggestions.empty() && !content.dismissed.empty()) {
    UMA_HISTOGRAM_COUNTS("NewTabPage.Snippets.NumArticlesZeroDueToDiscarded",
                         content.dismissed.size());
  }

  if (callback) {
    callback.Run(status);
  }
}

void RemoteSuggestionsProviderImpl::ArchiveSuggestions(
    CategoryContent* content,
    RemoteSuggestion::PtrVector* to_archive) {
  // Archive previous suggestions - move them at the beginning of the list.
  content->archived.insert(content->archived.begin(),
                           std::make_move_iterator(to_archive->begin()),
                           std::make_move_iterator(to_archive->end()));
  to_archive->clear();

  // If there are more archived suggestions than we want to keep, delete the
  // oldest ones by their fetch time (which are always in the back).
  if (content->archived.size() > kMaxArchivedSuggestionCount) {
    RemoteSuggestion::PtrVector to_delete(
        std::make_move_iterator(content->archived.begin() +
                                kMaxArchivedSuggestionCount),
        std::make_move_iterator(content->archived.end()));
    content->archived.resize(kMaxArchivedSuggestionCount);
    database_->DeleteImages(GetSuggestionIDVector(to_delete));
  }
}

void RemoteSuggestionsProviderImpl::SanitizeReceivedSuggestions(
    const RemoteSuggestion::PtrVector& dismissed,
    RemoteSuggestion::PtrVector* suggestions) {
  DCHECK(ready());
  EraseMatchingSuggestions(suggestions, dismissed);
  RemoveIncompleteSuggestions(suggestions);
}

void RemoteSuggestionsProviderImpl::IntegrateSuggestions(
    CategoryContent* content,
    RemoteSuggestion::PtrVector new_suggestions) {
  DCHECK(ready());

  // Do not touch the current set of suggestions if the newly fetched one is
  // empty.
  // TODO(tschumann): This should go. If we get empty results we should update
  // accordingly and remove the old one (only of course if this was not received
  // through a fetch-more).
  if (new_suggestions.empty()) {
    return;
  }

  // It's entirely possible that the newly fetched suggestions contain articles
  // that have been present before.
  // We need to make sure to only delete and archive suggestions that don't
  // appear with the same ID in the new suggestions (it's fine for additional
  // IDs though).
  EraseByPrimaryID(&content->suggestions,
                   *GetSuggestionIDVector(new_suggestions));
  // Do not delete the thumbnail images as they are still handy on open NTPs.
  database_->DeleteSnippets(GetSuggestionIDVector(content->suggestions));
  // Note, that ArchiveSuggestions will clear |content->suggestions|.
  ArchiveSuggestions(content, &content->suggestions);

  database_->SaveSnippets(new_suggestions);

  content->suggestions = std::move(new_suggestions);
}

void RemoteSuggestionsProviderImpl::DismissSuggestionFromCategoryContent(
    CategoryContent* content,
    const std::string& id_within_category) {
  auto it =
      std::find_if(content->suggestions.begin(), content->suggestions.end(),
                   [&id_within_category](
                       const std::unique_ptr<RemoteSuggestion>& suggestion) {
                     return suggestion->id() == id_within_category;
                   });
  if (it == content->suggestions.end()) {
    return;
  }

  (*it)->set_dismissed(true);

  database_->SaveSnippet(**it);

  content->dismissed.push_back(std::move(*it));
  content->suggestions.erase(it);
}

void RemoteSuggestionsProviderImpl::ClearExpiredDismissedSuggestions() {
  std::vector<Category> categories_to_erase;

  const base::Time now = base::Time::Now();

  for (auto& item : category_contents_) {
    Category category = item.first;
    CategoryContent* content = &item.second;

    RemoteSuggestion::PtrVector to_delete;
    // Move expired dismissed suggestions over into |to_delete|.
    for (std::unique_ptr<RemoteSuggestion>& suggestion : content->dismissed) {
      if (suggestion->expiry_date() <= now) {
        to_delete.emplace_back(std::move(suggestion));
      }
    }
    RemoveNullPointers(&content->dismissed);

    // Delete the images.
    database_->DeleteImages(GetSuggestionIDVector(to_delete));
    // Delete the removed article suggestions from the DB.
    database_->DeleteSnippets(GetSuggestionIDVector(to_delete));

    if (content->suggestions.empty() && content->dismissed.empty() &&
        category != articles_category_ &&
        !content->included_in_last_server_response) {
      categories_to_erase.push_back(category);
    }
  }

  for (Category category : categories_to_erase) {
    UpdateCategoryStatus(category, CategoryStatus::NOT_PROVIDED);
    category_contents_.erase(category);
  }

  StoreCategoriesToPrefs();
}

void RemoteSuggestionsProviderImpl::ClearOrphanedImages() {
  auto alive_suggestions = base::MakeUnique<std::set<std::string>>();
  for (const auto& entry : category_contents_) {
    const CategoryContent& content = entry.second;
    for (const auto& suggestion_ptr : content.suggestions) {
      alive_suggestions->insert(suggestion_ptr->id());
    }
    for (const auto& suggestion_ptr : content.dismissed) {
      alive_suggestions->insert(suggestion_ptr->id());
    }
  }
  database_->GarbageCollectImages(std::move(alive_suggestions));
}

void RemoteSuggestionsProviderImpl::ClearHistoryDependentState() {
  if (!initialized()) {
    clear_history_dependent_state_when_initialized_ = true;
    return;
  }

  NukeAllSuggestions();
  remote_suggestions_scheduler_->OnHistoryCleared();
}

void RemoteSuggestionsProviderImpl::ClearSuggestions() {
  DCHECK(initialized());

  NukeAllSuggestions();
  remote_suggestions_scheduler_->OnSuggestionsCleared();
}

void RemoteSuggestionsProviderImpl::NukeAllSuggestions() {
  for (const auto& item : category_contents_) {
    Category category = item.first;
    const CategoryContent& content = item.second;

    ClearCachedSuggestions(category);
    // Update listeners about the new (empty) state.
    if (IsCategoryStatusAvailable(content.status)) {
      NotifyNewSuggestions(category, content);
    }
    // TODO(tschumann): We should not call debug code from production code.
    ClearDismissedSuggestionsForDebugging(category);
  }

  StoreCategoriesToPrefs();
}

void RemoteSuggestionsProviderImpl::FetchSuggestionImage(
    const ContentSuggestion::ID& suggestion_id,
    const ImageFetchedCallback& callback) {
  if (!base::ContainsKey(category_contents_, suggestion_id.category())) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(callback, gfx::Image()));
    return;
  }
  GURL image_url = FindSuggestionImageUrl(suggestion_id);
  if (image_url.is_empty()) {
    // As we don't know the corresponding suggestion anymore, we don't expect to
    // find it in the database (and also can't fetch it remotely). Cut the
    // lookup short and return directly.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(callback, gfx::Image()));
    return;
  }
  image_fetcher_.FetchSuggestionImage(suggestion_id, image_url, callback);
}

void RemoteSuggestionsProviderImpl::EnterStateReady() {
  if (clear_history_dependent_state_when_initialized_) {
    clear_history_dependent_state_when_initialized_ = false;
    ClearHistoryDependentState();
  }

  auto article_category_it = category_contents_.find(articles_category_);
  DCHECK(article_category_it != category_contents_.end());
  if (fetch_when_ready_) {
    FetchSuggestions(fetch_when_ready_interactive_, fetch_when_ready_callback_);
    fetch_when_ready_ = false;
  }

  for (const auto& item : category_contents_) {
    Category category = item.first;
    const CategoryContent& content = item.second;
    // FetchSuggestions has set the status to |AVAILABLE_LOADING| if relevant,
    // otherwise we transition to |AVAILABLE| here.
    if (content.status != CategoryStatus::AVAILABLE_LOADING) {
      UpdateCategoryStatus(category, CategoryStatus::AVAILABLE);
    }
  }
}

void RemoteSuggestionsProviderImpl::EnterStateDisabled() {
  ClearSuggestions();
}

void RemoteSuggestionsProviderImpl::EnterStateError() {
  status_service_.reset();
}

void RemoteSuggestionsProviderImpl::FinishInitialization() {
  if (clear_history_dependent_state_when_initialized_) {
    // We clear here in addition to EnterStateReady, so that it happens even if
    // we enter the DISABLED state below.
    clear_history_dependent_state_when_initialized_ = false;
    ClearHistoryDependentState();
  }

  // Note: Initializing the status service will run the callback right away with
  // the current state.
  status_service_->Init(base::Bind(
      &RemoteSuggestionsProviderImpl::OnStatusChanged, base::Unretained(this)));

  // Always notify here even if we got nothing from the database, because we
  // don't know how long the fetch will take or if it will even complete.
  for (const auto& item : category_contents_) {
    Category category = item.first;
    const CategoryContent& content = item.second;
    // Note: We might be in a non-available status here, e.g. DISABLED due to
    // enterprise policy.
    if (IsCategoryStatusAvailable(content.status)) {
      NotifyNewSuggestions(category, content);
    }
  }
}

void RemoteSuggestionsProviderImpl::OnStatusChanged(
    RemoteSuggestionsStatus old_status,
    RemoteSuggestionsStatus new_status) {
  switch (new_status) {
    case RemoteSuggestionsStatus::ENABLED_AND_SIGNED_IN:
      if (old_status == RemoteSuggestionsStatus::ENABLED_AND_SIGNED_OUT) {
        DCHECK(state_ == State::READY);
        // Clear nonpersonalized suggestions (and notify the scheduler there are
        // no suggestions).
        ClearSuggestions();
      } else {
        // Do not change the status. That will be done in EnterStateReady().
        EnterState(State::READY);
      }
      break;

    case RemoteSuggestionsStatus::ENABLED_AND_SIGNED_OUT:
      if (old_status == RemoteSuggestionsStatus::ENABLED_AND_SIGNED_IN) {
        DCHECK(state_ == State::READY);
        // Clear personalized suggestions (and notify the scheduler there are
        // no suggestions).
        ClearSuggestions();
      } else {
        // Do not change the status. That will be done in EnterStateReady().
        EnterState(State::READY);
      }
      break;

    case RemoteSuggestionsStatus::EXPLICITLY_DISABLED:
      EnterState(State::DISABLED);
      UpdateAllCategoryStatus(CategoryStatus::CATEGORY_EXPLICITLY_DISABLED);
      break;
  }
}

void RemoteSuggestionsProviderImpl::EnterState(State state) {
  if (state == state_) {
    return;
  }

  UMA_HISTOGRAM_ENUMERATION("NewTabPage.Snippets.EnteredState",
                            static_cast<int>(state),
                            static_cast<int>(State::COUNT));

  switch (state) {
    case State::NOT_INITED:
      // Initial state, it should not be possible to get back there.
      NOTREACHED();
      break;

    case State::READY:
      DCHECK(state_ == State::NOT_INITED || state_ == State::DISABLED);

      DVLOG(1) << "Entering state: READY";
      state_ = State::READY;
      NotifyStateChanged();
      EnterStateReady();
      break;

    case State::DISABLED:
      DCHECK(state_ == State::NOT_INITED || state_ == State::READY);

      DVLOG(1) << "Entering state: DISABLED";
      // TODO(jkrcal): Fix the fragility of the following code. Currently, it is
      // important that we first change the state and notify the scheduler (as
      // it will update its state) and only at last we EnterStateDisabled()
      // which clears suggestions. Clearing suggestions namely notifies the
      // scheduler to fetch them again, which is ignored because the scheduler
      // is disabled. crbug/695447
      state_ = State::DISABLED;
      NotifyStateChanged();
      EnterStateDisabled();
      break;

    case State::ERROR_OCCURRED:
      DVLOG(1) << "Entering state: ERROR_OCCURRED";
      state_ = State::ERROR_OCCURRED;
      NotifyStateChanged();
      EnterStateError();
      break;

    case State::COUNT:
      NOTREACHED();
      break;
  }
}

void RemoteSuggestionsProviderImpl::NotifyStateChanged() {
  switch (state_) {
    case State::NOT_INITED:
      // Initial state, not sure yet whether active or not.
      break;
    case State::READY:
      remote_suggestions_scheduler_->OnProviderActivated();
      break;
    case State::DISABLED:
      remote_suggestions_scheduler_->OnProviderDeactivated();
      break;
    case State::ERROR_OCCURRED:
      remote_suggestions_scheduler_->OnProviderDeactivated();
      break;
    case State::COUNT:
      NOTREACHED();
      break;
  }
}

void RemoteSuggestionsProviderImpl::NotifyNewSuggestions(
    Category category,
    const CategoryContent& content) {
  DCHECK(IsCategoryStatusAvailable(content.status));

  std::vector<ContentSuggestion> result =
      ConvertToContentSuggestions(category, content.suggestions);

  DVLOG(1) << "NotifyNewSuggestions(): " << result.size()
           << " items in category " << category;
  observer()->OnNewSuggestions(this, category, std::move(result));
}

void RemoteSuggestionsProviderImpl::UpdateCategoryStatus(
    Category category,
    CategoryStatus status) {
  auto content_it = category_contents_.find(category);
  DCHECK(content_it != category_contents_.end());
  CategoryContent& content = content_it->second;

  if (status == content.status) {
    return;
  }

  DVLOG(1) << "UpdateCategoryStatus(): " << category.id() << ": "
           << static_cast<int>(content.status) << " -> "
           << static_cast<int>(status);
  content.status = status;
  observer()->OnCategoryStatusChanged(this, category, content.status);
}

void RemoteSuggestionsProviderImpl::UpdateAllCategoryStatus(
    CategoryStatus status) {
  for (const auto& category : category_contents_) {
    UpdateCategoryStatus(category.first, status);
  }
}

namespace {

template <typename T>
typename T::const_iterator FindSuggestionInContainer(
    const T& container,
    const std::string& id_within_category) {
  return std::find_if(container.begin(), container.end(),
                      [&id_within_category](
                          const std::unique_ptr<RemoteSuggestion>& suggestion) {
                        return suggestion->id() == id_within_category;
                      });
}

}  // namespace

const RemoteSuggestion*
RemoteSuggestionsProviderImpl::CategoryContent::FindSuggestion(
    const std::string& id_within_category) const {
  // Search for the suggestion in current and archived suggestions.
  auto it = FindSuggestionInContainer(suggestions, id_within_category);
  if (it != suggestions.end()) {
    return it->get();
  }
  auto archived_it = FindSuggestionInContainer(archived, id_within_category);
  if (archived_it != archived.end()) {
    return archived_it->get();
  }
  auto dismissed_it = FindSuggestionInContainer(dismissed, id_within_category);
  if (dismissed_it != dismissed.end()) {
    return dismissed_it->get();
  }
  return nullptr;
}

RemoteSuggestionsProviderImpl::CategoryContent*
RemoteSuggestionsProviderImpl::UpdateCategoryInfo(Category category,
                                                  const CategoryInfo& info) {
  auto content_it = category_contents_.find(category);
  if (content_it == category_contents_.end()) {
    content_it = category_contents_
                     .insert(std::make_pair(category, CategoryContent(info)))
                     .first;
  } else {
    content_it->second.info = info;
  }
  return &content_it->second;
}

void RemoteSuggestionsProviderImpl::RestoreCategoriesFromPrefs() {
  // This must only be called at startup, before there are any categories.
  DCHECK(category_contents_.empty());

  const base::ListValue* list =
      pref_service_->GetList(prefs::kRemoteSuggestionCategories);
  for (const base::Value& entry : *list) {
    const base::DictionaryValue* dict = nullptr;
    if (!entry.GetAsDictionary(&dict)) {
      DLOG(WARNING) << "Invalid category pref value: " << entry;
      continue;
    }
    int id = 0;
    if (!dict->GetInteger(kCategoryContentId, &id)) {
      DLOG(WARNING) << "Invalid category pref value, missing '"
                    << kCategoryContentId << "': " << entry;
      continue;
    }
    base::string16 title;
    if (!dict->GetString(kCategoryContentTitle, &title)) {
      DLOG(WARNING) << "Invalid category pref value, missing '"
                    << kCategoryContentTitle << "': " << entry;
      continue;
    }
    bool included_in_last_server_response = false;
    if (!dict->GetBoolean(kCategoryContentProvidedByServer,
                          &included_in_last_server_response)) {
      DLOG(WARNING) << "Invalid category pref value, missing '"
                    << kCategoryContentProvidedByServer << "': " << entry;
      continue;
    }
    bool allow_fetching_more_results = false;
    // This wasn't always around, so it's okay if it's missing.
    dict->GetBoolean(kCategoryContentAllowFetchingMore,
                     &allow_fetching_more_results);

    Category category = Category::FromIDValue(id);
    // The ranker may not persist the order of remote categories.
    category_ranker_->AppendCategoryIfNecessary(category);
    // TODO(tschumann): The following has a bad smell that category
    // serialization / deserialization should not be done inside this
    // class. We should move that into a central place that also knows how to
    // parse data we received from remote backends.
    CategoryInfo info =
        category == articles_category_
            ? BuildArticleCategoryInfo(title)
            : BuildRemoteCategoryInfo(title, allow_fetching_more_results);
    CategoryContent* content = UpdateCategoryInfo(category, info);
    content->included_in_last_server_response =
        included_in_last_server_response;
  }
}

void RemoteSuggestionsProviderImpl::StoreCategoriesToPrefs() {
  // Collect all the CategoryContents.
  std::vector<std::pair<Category, const CategoryContent*>> to_store;
  for (const auto& entry : category_contents_) {
    to_store.emplace_back(entry.first, &entry.second);
  }
  // The ranker may not persist the order, thus, it is stored by the provider.
  std::sort(to_store.begin(), to_store.end(),
            [this](const std::pair<Category, const CategoryContent*>& left,
                   const std::pair<Category, const CategoryContent*>& right) {
              return category_ranker_->Compare(left.first, right.first);
            });
  // Convert the relevant info into a base::ListValue for storage.
  base::ListValue list;
  for (const auto& entry : to_store) {
    const Category& category = entry.first;
    const CategoryContent& content = *entry.second;
    auto dict = base::MakeUnique<base::DictionaryValue>();
    dict->SetInteger(kCategoryContentId, category.id());
    // TODO(tschumann): Persist other properties of the CategoryInfo.
    dict->SetString(kCategoryContentTitle, content.info.title());
    dict->SetBoolean(kCategoryContentProvidedByServer,
                     content.included_in_last_server_response);
    bool has_fetch_action = content.info.additional_action() ==
                            ContentSuggestionsAdditionalAction::FETCH;
    dict->SetBoolean(kCategoryContentAllowFetchingMore, has_fetch_action);
    list.Append(std::move(dict));
  }
  // Finally, store the result in the pref service.
  pref_service_->Set(prefs::kRemoteSuggestionCategories, list);
}

RemoteSuggestionsProviderImpl::CategoryContent::CategoryContent(
    const CategoryInfo& info)
    : info(info) {}

RemoteSuggestionsProviderImpl::CategoryContent::CategoryContent(
    CategoryContent&&) = default;

RemoteSuggestionsProviderImpl::CategoryContent::~CategoryContent() = default;

RemoteSuggestionsProviderImpl::CategoryContent&
RemoteSuggestionsProviderImpl::CategoryContent::operator=(CategoryContent&&) =
    default;

}  // namespace ntp_snippets
