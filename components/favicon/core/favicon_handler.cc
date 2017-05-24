// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/favicon/core/favicon_handler.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/feature_list.h"
#include "base/memory/ref_counted_memory.h"
#include "base/metrics/histogram_macros.h"
#include "build/build_config.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon/core/features.h"
#include "components/favicon_base/favicon_util.h"
#include "components/favicon_base/select_favicon_frames.h"
#include "skia/ext/image_operations.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_util.h"

namespace favicon {
namespace {

const int kNonTouchLargestIconSize = 192;

// Size (along each axis) of a touch icon. This currently corresponds to
// the apple touch icon for iPad.
const int kTouchIconSize = 144;

// Return true if |bitmap_result| is expired.
bool IsExpired(const favicon_base::FaviconRawBitmapResult& bitmap_result) {
  return bitmap_result.expired;
}

// Return true if |bitmap_result| is valid.
bool IsValid(const favicon_base::FaviconRawBitmapResult& bitmap_result) {
  return bitmap_result.is_valid();
}

void RecordDownloadAttemptsForHandlerType(
    FaviconDriverObserver::NotificationIconType handler_type,
    int attempts) {
  // If not at least one attempts was recorded or more than 15 attempts were
  // registered, something went wrong. Underflows are stored in bucket 0 and
  // overflows in bucket 16.
  attempts = std::max(0, std::min(attempts, 16));
  switch (handler_type) {
    case FaviconDriverObserver::NON_TOUCH_16_DIP:
      UMA_HISTOGRAM_SPARSE_SLOWLY("Favicons.DownloadAttempts.Favicons",
                                  attempts);
      return;
    case FaviconDriverObserver::NON_TOUCH_LARGEST:
      UMA_HISTOGRAM_SPARSE_SLOWLY("Favicons.DownloadAttempts.LargeIcons",
                                  attempts);
      return;
    case FaviconDriverObserver::TOUCH_LARGEST:
      UMA_HISTOGRAM_SPARSE_SLOWLY("Favicons.DownloadAttempts.TouchIcons",
                                  attempts);
      return;
  }
  NOTREACHED();
}

void RecordDownloadOutcome(FaviconHandler::DownloadOutcome outcome) {
  UMA_HISTOGRAM_ENUMERATION(
      "Favicons.DownloadOutcome", outcome,
      FaviconHandler::DownloadOutcome::DOWNLOAD_OUTCOME_COUNT);
}

// Returns true if |bitmap_results| is non-empty and:
// - At least one of the bitmaps in |bitmap_results| is expired
// OR
// - |bitmap_results| is missing favicons for |desired_size_in_dip| and one of
//   the scale factors in favicon_base::GetFaviconScales().
bool HasExpiredOrIncompleteResult(
    int desired_size_in_dip,
    const std::vector<favicon_base::FaviconRawBitmapResult>& bitmap_results) {
  if (bitmap_results.empty())
    return false;

  // Check if at least one of the bitmaps is expired.
  std::vector<favicon_base::FaviconRawBitmapResult>::const_iterator it =
      std::find_if(bitmap_results.begin(), bitmap_results.end(), IsExpired);
  if (it != bitmap_results.end())
    return true;

  // Any favicon size is good if the desired size is 0.
  if (desired_size_in_dip == 0)
    return false;

  // Check if the favicon for at least one of the scale factors is missing.
  // |bitmap_results| should always be complete for data inserted by
  // FaviconHandler as the FaviconHandler stores favicons resized to all
  // of favicon_base::GetFaviconScales() into the history backend.
  // Examples of when |bitmap_results| can be incomplete:
  // - Favicons inserted into the history backend by sync.
  // - Favicons for imported bookmarks.
  std::vector<gfx::Size> favicon_sizes;
  for (const auto& bitmap_result : bitmap_results)
    favicon_sizes.push_back(bitmap_result.pixel_size);

  std::vector<float> favicon_scales = favicon_base::GetFaviconScales();
  for (float favicon_scale : favicon_scales) {
    int edge_size_in_pixel = std::ceil(desired_size_in_dip * favicon_scale);
    auto it = std::find(favicon_sizes.begin(), favicon_sizes.end(),
                        gfx::Size(edge_size_in_pixel, edge_size_in_pixel));
    if (it == favicon_sizes.end())
      return true;
  }
  return false;
}

// Returns true if at least one of |bitmap_results| is valid.
bool HasValidResult(
    const std::vector<favicon_base::FaviconRawBitmapResult>& bitmap_results) {
  return std::find_if(bitmap_results.begin(), bitmap_results.end(), IsValid) !=
      bitmap_results.end();
}

std::vector<int> GetDesiredPixelSizes(
    FaviconDriverObserver::NotificationIconType handler_type) {
  switch (handler_type) {
    case FaviconDriverObserver::NON_TOUCH_16_DIP: {
      std::vector<int> pixel_sizes;
      for (float scale_factor : favicon_base::GetFaviconScales()) {
        pixel_sizes.push_back(
            static_cast<int>(ceil(scale_factor * gfx::kFaviconSize)));
      }
      return pixel_sizes;
    }
    case FaviconDriverObserver::NON_TOUCH_LARGEST:
      return std::vector<int>(1U, kNonTouchLargestIconSize);
    case FaviconDriverObserver::TOUCH_LARGEST:
      return std::vector<int>(1U, kTouchIconSize);
  }
  NOTREACHED();
  return std::vector<int>();
}

bool FaviconURLEquals(const FaviconURL& lhs, const FaviconURL& rhs) {
  return lhs.icon_url == rhs.icon_url && lhs.icon_type == rhs.icon_type &&
         lhs.icon_sizes == rhs.icon_sizes;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////

// static
FaviconHandler::FaviconCandidate
FaviconHandler::FaviconCandidate::FromFaviconURL(
    const favicon::FaviconURL& favicon_url,
    const std::vector<int>& desired_pixel_sizes) {
  FaviconCandidate candidate;
  candidate.icon_url = favicon_url.icon_url;
  candidate.icon_type = favicon_url.icon_type;
  // TODO(crbug.com/705900): For candidates without explicit size information,
  // sizes could be inferred for the most common cases. Namely, .ico files tend
  // to contain the 16x16 bitmap, which would allow to improve the
  // prioritization on desktop.
  SelectFaviconFrameIndices(favicon_url.icon_sizes, desired_pixel_sizes,
                            /*best_indices=*/nullptr, &candidate.score);
  return candidate;
}

////////////////////////////////////////////////////////////////////////////////

FaviconHandler::FaviconHandler(
    FaviconService* service,
    Delegate* delegate,
    FaviconDriverObserver::NotificationIconType handler_type)
    : handler_type_(handler_type),
      got_favicon_from_history_(false),
      initial_history_result_expired_or_incomplete_(false),
      redownload_icons_(false),
      icon_types_(FaviconHandler::GetIconTypesFromHandlerType(handler_type)),
      download_largest_icon_(
          handler_type == FaviconDriverObserver::NON_TOUCH_LARGEST ||
          handler_type == FaviconDriverObserver::TOUCH_LARGEST),
      notification_icon_type_(favicon_base::INVALID_ICON),
      service_(service),
      delegate_(delegate),
      num_image_download_requests_(0),
      current_candidate_index_(0u) {
  DCHECK(delegate_);
}

FaviconHandler::~FaviconHandler() {
}

// static
int FaviconHandler::GetIconTypesFromHandlerType(
    FaviconDriverObserver::NotificationIconType handler_type) {
  switch (handler_type) {
    case FaviconDriverObserver::NON_TOUCH_16_DIP:
    case FaviconDriverObserver::NON_TOUCH_LARGEST:
      return favicon_base::FAVICON;
    case FaviconDriverObserver::TOUCH_LARGEST:
      return favicon_base::TOUCH_ICON | favicon_base::TOUCH_PRECOMPOSED_ICON;
  }
  return 0;
}

void FaviconHandler::FetchFavicon(const GURL& url) {
  cancelable_task_tracker_for_page_url_.TryCancelAll();
  cancelable_task_tracker_for_candidates_.TryCancelAll();

  url_ = url;

  initial_history_result_expired_or_incomplete_ = false;
  redownload_icons_ = false;
  got_favicon_from_history_ = false;
  manifest_download_request_.Cancel();
  image_download_request_.Cancel();
  manifest_url_ = GURL();
  non_manifest_original_candidates_.clear();
  candidates_.clear();
  notification_icon_url_ = GURL();
  notification_icon_type_ = favicon_base::INVALID_ICON;
  num_image_download_requests_ = 0;
  current_candidate_index_ = 0u;
  best_favicon_ = DownloadedFavicon();

  // Request the favicon from the history service. In parallel to this the
  // renderer is going to notify us (well WebContents) when the favicon url is
  // available.
  service_->GetFaviconForPageURL(
      url_, icon_types_, preferred_icon_size(),
      base::Bind(&FaviconHandler::OnFaviconDataForInitialURLFromFaviconService,
                 base::Unretained(this)),
      &cancelable_task_tracker_for_page_url_);
}

bool FaviconHandler::UpdateFaviconCandidate(
    const DownloadedFavicon& downloaded_favicon) {
  if (downloaded_favicon.candidate.score > best_favicon_.candidate.score)
    best_favicon_ = downloaded_favicon;

  if (download_largest_icon_) {
    // The size of the downloaded icon may not match the declared size. It's
    // important to stop downloading if:
    // - current candidate is only candidate.
    // - next candidate has sizes attribute and it is not better than the best
    //   one observed so far, which means any following candidate should also
    //   be worse or equal too.
    // - next candidate doesn't have sizes attributes, which means further
    //   candidates don't have sizes attribute either (because the score lowest
    //   and hence get sorted last during prioritization). We stop immediately
    //   to avoid downloading them all, although we don't have the certainty
    //   that no better favicon is among them.
    return current_candidate_index_ + 1 >= candidates_.size() ||
           candidates_[current_candidate_index_ + 1].score <=
               best_favicon_.candidate.score;
  } else {
    return best_favicon_.candidate.score == 1;
  }
}

void FaviconHandler::SetFavicon(const GURL& icon_url,
                                const gfx::Image& image,
                                favicon_base::IconType icon_type) {
  if (ShouldSaveFavicon())
    service_->SetFavicons(url_, icon_url, icon_type, image);

  NotifyFaviconUpdated(icon_url, icon_type, image);
}

void FaviconHandler::NotifyFaviconUpdated(
    const std::vector<favicon_base::FaviconRawBitmapResult>&
        favicon_bitmap_results) {
  DCHECK(!favicon_bitmap_results.empty());

  gfx::Image resized_image = favicon_base::SelectFaviconFramesFromPNGs(
      favicon_bitmap_results,
      favicon_base::GetFaviconScales(),
      preferred_icon_size());
  // The history service sends back results for a single icon URL and icon
  // type, so it does not matter which result we get |icon_url| and |icon_type|
  // from.
  const GURL icon_url = favicon_bitmap_results[0].icon_url;
  favicon_base::IconType icon_type = favicon_bitmap_results[0].icon_type;
  NotifyFaviconUpdated(icon_url, icon_type, resized_image);
}

void FaviconHandler::NotifyFaviconUpdated(const GURL& icon_url,
                                          favicon_base::IconType icon_type,
                                          const gfx::Image& image) {
  if (image.IsEmpty())
    return;

  gfx::Image image_with_adjusted_colorspace = image;
  favicon_base::SetFaviconColorSpace(&image_with_adjusted_colorspace);

  delegate_->OnFaviconUpdated(url_, handler_type_, icon_url,
                              icon_url != notification_icon_url_,
                              image_with_adjusted_colorspace);

  notification_icon_url_ = icon_url;
  notification_icon_type_ = icon_type;
}

void FaviconHandler::OnUpdateCandidates(
    const GURL& page_url,
    const std::vector<FaviconURL>& candidates,
    const GURL& manifest_url) {
  if (page_url != url_)
    return;

  bool manifests_feature_enabled =
      base::FeatureList::IsEnabled(kFaviconsFromWebManifest);

  // |candidates| or |manifest_url| could have been modified via Javascript. If
  // neither changed, ignore the call.
  if ((!manifests_feature_enabled || manifest_url_ == manifest_url) &&
      non_manifest_original_candidates_.size() == candidates.size() &&
      std::equal(candidates.begin(), candidates.end(),
                 non_manifest_original_candidates_.begin(),
                 &FaviconURLEquals)) {
    return;
  }

  non_manifest_original_candidates_ = candidates;
  cancelable_task_tracker_for_candidates_.TryCancelAll();
  manifest_download_request_.Cancel();
  image_download_request_.Cancel();
  num_image_download_requests_ = 0;
  current_candidate_index_ = 0u;
  best_favicon_ = DownloadedFavicon();

  if (manifests_feature_enabled)
    manifest_url_ = manifest_url;

  // Check if the manifest was previously blacklisted (e.g. returned a 404) and
  // ignore the manifest URL if that's the case.
  if (!manifest_url_.is_empty() &&
      service_->WasUnableToDownloadFavicon(manifest_url_)) {
    DVLOG(1) << "Skip failed Manifest: " << manifest_url;
    manifest_url_ = GURL();
  }

  // If no manifest available, proceed with the regular candidates only.
  if (manifest_url_.is_empty()) {
    OnGotFinalIconURLCandidates(candidates);
    return;
  }

  // See if there is a cached favicon for the manifest. This will update the DB
  // mappings only if the manifest URL is cached.
  GetFaviconAndUpdateMappingsUnlessIncognito(
      /*icon_url=*/manifest_url_, favicon_base::FAVICON,
      base::Bind(&FaviconHandler::OnFaviconDataForManifestFromFaviconService,
                 base::Unretained(this)));
}

void FaviconHandler::OnFaviconDataForManifestFromFaviconService(
    const std::vector<favicon_base::FaviconRawBitmapResult>&
        favicon_bitmap_results) {
  // The database lookup for the page URL is guaranteed to be completed because
  // the HistoryBackend uses a SequencedTaskRunner, and we also know that
  // FetchFavicon() was called before OnUpdateCandidates().
  DCHECK(got_favicon_from_history_);

  bool has_valid_result = HasValidResult(favicon_bitmap_results);
  bool has_expired_or_incomplete_result =
      !has_valid_result || HasExpiredOrIncompleteResult(preferred_icon_size(),
                                                        favicon_bitmap_results);

  if (has_valid_result && (notification_icon_url_ != manifest_url_ ||
                           notification_icon_type_ != favicon_base::FAVICON)) {
    // There is a valid favicon. Notify any observers. It is useful to notify
    // the observers even if the favicon is expired or incomplete (incorrect
    // size) because temporarily showing the user an expired favicon or
    // streched favicon is preferable to showing the user the default favicon.
    NotifyFaviconUpdated(favicon_bitmap_results);
  }

  if (has_expired_or_incomplete_result) {
    manifest_download_request_.Reset(base::Bind(
        &FaviconHandler::OnDidDownloadManifest, base::Unretained(this)));
    delegate_->DownloadManifest(manifest_url_,
                                manifest_download_request_.callback());
  }
}

void FaviconHandler::OnDidDownloadManifest(
    const std::vector<FaviconURL>& candidates) {
  // Mark manifest download as finished.
  manifest_download_request_.Cancel();

  if (!candidates.empty()) {
    OnGotFinalIconURLCandidates(candidates);
    return;
  }

  // If either the downloading of the manifest failed, OR the manifest contains
  // no icons, proceed with the list of icons listed in the HTML.
  DVLOG(1) << "Could not fetch Manifest icons from " << manifest_url_
           << ", falling back to inlined ones, which are "
           << non_manifest_original_candidates_.size();

  service_->UnableToDownloadFavicon(manifest_url_);
  manifest_url_ = GURL();

  OnGotFinalIconURLCandidates(non_manifest_original_candidates_);
}

void FaviconHandler::OnGotFinalIconURLCandidates(
    const std::vector<FaviconURL>& candidates) {
  std::vector<FaviconCandidate> sorted_candidates;
  const std::vector<int> desired_pixel_sizes =
      GetDesiredPixelSizes(handler_type_);
  for (const FaviconURL& candidate : candidates) {
    if (!candidate.icon_url.is_empty() && (candidate.icon_type & icon_types_)) {
      sorted_candidates.push_back(
          FaviconCandidate::FromFaviconURL(candidate, desired_pixel_sizes));
    }
  }

  std::stable_sort(sorted_candidates.begin(), sorted_candidates.end(),
                   &FaviconCandidate::CompareScore);

  candidates_ = std::move(sorted_candidates);

  // TODO(davemoore) Should clear on empty url. Currently we ignore it.
  // This appears to be what FF does as well.
  if (current_candidate() && got_favicon_from_history_)
    OnGotInitialHistoryDataAndIconURLCandidates();
}

// static
int FaviconHandler::GetMaximalIconSize(
    FaviconDriverObserver::NotificationIconType handler_type) {
  int max_size = 0;
  for (int size : GetDesiredPixelSizes(handler_type))
    max_size = std::max(max_size, size);
  return max_size;
}

void FaviconHandler::OnGotInitialHistoryDataAndIconURLCandidates() {
  if (!initial_history_result_expired_or_incomplete_ &&
      current_candidate()->icon_url == notification_icon_url_ &&
      current_candidate()->icon_type == notification_icon_type_) {
    // - The data from history is valid and not expired.
    // - The icon URL of the history data matches one of the page's icon URLs.
    // - The icon URL of the history data matches the icon URL of the last
    //   OnFaviconAvailable() notification.
    // We are done. No additional downloads or history requests are needed.
    // TODO: Store all of the icon URLs associated with a page in history so
    // that we can check whether the page's icon URLs match the page's icon URLs
    // at the time that the favicon data was stored to the history database.
    return;
  }

  DownloadCurrentCandidateOrAskFaviconService();
}

void FaviconHandler::OnDidDownloadFavicon(
    favicon_base::IconType icon_type,
    int id,
    int http_status_code,
    const GURL& image_url,
    const std::vector<SkBitmap>& bitmaps,
    const std::vector<gfx::Size>& original_bitmap_sizes) {
  // Mark download as finished.
  image_download_request_.Cancel();

  if (bitmaps.empty() && http_status_code == 404) {
    DVLOG(1) << "Failed to Download Favicon:" << image_url;
    RecordDownloadOutcome(DownloadOutcome::FAILED);
    service_->UnableToDownloadFavicon(image_url);
  }

  bool request_next_icon = true;
  if (!bitmaps.empty()) {
    RecordDownloadOutcome(DownloadOutcome::SUCCEEDED);
    float score = 0.0f;
    gfx::ImageSkia image_skia;
    if (download_largest_icon_) {
      std::vector<size_t> best_indices;
      SelectFaviconFrameIndices(original_bitmap_sizes,
                                GetDesiredPixelSizes(handler_type_),
                                &best_indices, &score);
      DCHECK_EQ(1U, best_indices.size());
      image_skia =
          gfx::ImageSkia::CreateFrom1xBitmap(bitmaps[best_indices.front()]);
    } else {
      image_skia = CreateFaviconImageSkia(bitmaps,
                                          original_bitmap_sizes,
                                          preferred_icon_size(),
                                          &score);
    }

    if (!image_skia.isNull()) {
      // The downloaded icon is still valid when there is no FaviconURL update
      // during the downloading.
      DownloadedFavicon downloaded_favicon;
      downloaded_favicon.image = gfx::Image(image_skia);
      downloaded_favicon.candidate.icon_url = image_url;
      downloaded_favicon.candidate.icon_type = icon_type;
      downloaded_favicon.candidate.score = score;
      request_next_icon = !UpdateFaviconCandidate(downloaded_favicon);
    }
  }

  if (request_next_icon && current_candidate_index_ + 1 < candidates_.size()) {
    // Process the next candidate.
    ++current_candidate_index_;
    DownloadCurrentCandidateOrAskFaviconService();
  } else {
    // OnDidDownloadFavicon() can only be called after requesting a download, so
    // |num_image_download_requests_| can never be 0.
    RecordDownloadAttemptsForHandlerType(handler_type_,
                                         num_image_download_requests_);
    // We have either found the ideal candidate or run out of candidates.
    if (best_favicon_.candidate.icon_type != favicon_base::INVALID_ICON) {
      // No more icons to request, set the favicon from the candidate. The
      // manifest URL, if available, is used instead of the icon URL.
      SetFavicon(manifest_url_.is_empty() ? best_favicon_.candidate.icon_url
                                          : manifest_url_,
                 best_favicon_.image, best_favicon_.candidate.icon_type);
    }
    // Clear download related state.
    current_candidate_index_ = candidates_.size();
    num_image_download_requests_ = 0;
    best_favicon_ = DownloadedFavicon();
  }
}

const std::vector<GURL> FaviconHandler::GetIconURLs() const {
  std::vector<GURL> icon_urls;
  for (const FaviconCandidate& candidate : candidates_)
    icon_urls.push_back(candidate.icon_url);
  return icon_urls;
}

bool FaviconHandler::HasPendingTasksForTest() {
  return !image_download_request_.IsCancelled() ||
         !manifest_download_request_.IsCancelled() ||
         cancelable_task_tracker_for_page_url_.HasTrackedTasks() ||
         cancelable_task_tracker_for_candidates_.HasTrackedTasks();
}

bool FaviconHandler::ShouldSaveFavicon() {
  if (!delegate_->IsOffTheRecord())
    return true;

  // Always save favicon if the page is bookmarked.
  return delegate_->IsBookmarked(url_);
}

void FaviconHandler::OnFaviconDataForInitialURLFromFaviconService(
    const std::vector<favicon_base::FaviconRawBitmapResult>&
        favicon_bitmap_results) {
  got_favicon_from_history_ = true;
  bool has_valid_result = HasValidResult(favicon_bitmap_results);
  initial_history_result_expired_or_incomplete_ =
      !has_valid_result ||
      HasExpiredOrIncompleteResult(preferred_icon_size(),
                                   favicon_bitmap_results);
  redownload_icons_ = initial_history_result_expired_or_incomplete_ &&
                      !favicon_bitmap_results.empty();

  if (has_valid_result) {
    // The db knows the favicon (although it may be out of date). Set the
    // favicon now, and if the favicon turns out to be expired (or the wrong
    // url) we'll fetch later on. This way the user doesn't see a flash of the
    // default favicon.
    NotifyFaviconUpdated(favicon_bitmap_results);
  }

  if (current_candidate())
    OnGotInitialHistoryDataAndIconURLCandidates();
}

void FaviconHandler::DownloadCurrentCandidateOrAskFaviconService() {
  const GURL icon_url = current_candidate()->icon_url;
  const favicon_base::IconType icon_type = current_candidate()->icon_type;
  // If the icons listed in a manifest are being processed, skip the cache
  // lookup for |icon_url| since the manifest's URL is used for caching, not the
  // icon URL, and this lookup has happened earlier.
  if (redownload_icons_ || !manifest_url_.is_empty()) {
    // We have the mapping, but the favicon is out of date. Download it now.
    ScheduleImageDownload(icon_url, icon_type);
  } else {
    GetFaviconAndUpdateMappingsUnlessIncognito(
        icon_url, icon_type,
        base::Bind(&FaviconHandler::OnFaviconData, base::Unretained(this)));
  }
}

void FaviconHandler::GetFaviconAndUpdateMappingsUnlessIncognito(
    const GURL& icon_url,
    favicon_base::IconType icon_type,
    const favicon_base::FaviconResultsCallback& callback) {
  // We don't know the favicon, but we may have previously downloaded the
  // favicon for another page that shares the same favicon. Ask for the
  // favicon given the favicon URL.
  if (delegate_->IsOffTheRecord()) {
    service_->GetFavicon(icon_url, icon_type, preferred_icon_size(), callback,
                         &cancelable_task_tracker_for_candidates_);
  } else {
    // Ask the history service for the icon. This does two things:
    // 1. Attempts to fetch the favicon data from the database.
    // 2. If the favicon exists in the database, this updates the database to
    //    include the mapping between the page url and the favicon url.
    // This is asynchronous. The history service will call back when done.
    service_->UpdateFaviconMappingsAndFetch(
        url_, icon_url, icon_type, preferred_icon_size(), callback,
        &cancelable_task_tracker_for_candidates_);
  }
}

void FaviconHandler::OnFaviconData(const std::vector<
    favicon_base::FaviconRawBitmapResult>& favicon_bitmap_results) {
  bool has_valid_result = HasValidResult(favicon_bitmap_results);
  bool has_expired_or_incomplete_result =
      !has_valid_result || HasExpiredOrIncompleteResult(preferred_icon_size(),
                                                        favicon_bitmap_results);

  if (has_valid_result) {
    // There is a valid favicon. Notify any observers. It is useful to notify
    // the observers even if the favicon is expired or incomplete (incorrect
    // size) because temporarily showing the user an expired favicon or
    // streched favicon is preferable to showing the user the default favicon.
    NotifyFaviconUpdated(favicon_bitmap_results);
  }

  if (has_expired_or_incomplete_result) {
    ScheduleImageDownload(current_candidate()->icon_url,
                          current_candidate()->icon_type);
  } else if (num_image_download_requests_ > 0) {
    RecordDownloadAttemptsForHandlerType(handler_type_,
                                         num_image_download_requests_);
  }
}

void FaviconHandler::ScheduleImageDownload(const GURL& image_url,
                                           favicon_base::IconType icon_type) {
  DCHECK(image_url.is_valid());
  // Note that CancelableCallback starts cancelled.
  DCHECK(image_download_request_.IsCancelled())
      << "More than one ongoing download";
  if (service_->WasUnableToDownloadFavicon(image_url)) {
    DVLOG(1) << "Skip Failed FavIcon: " << image_url;
    RecordDownloadOutcome(DownloadOutcome::SKIPPED);
    OnDidDownloadFavicon(icon_type, 0, 0, image_url, std::vector<SkBitmap>(),
                         std::vector<gfx::Size>());
    return;
  }
  ++num_image_download_requests_;
  image_download_request_.Reset(
      base::Bind(&FaviconHandler::OnDidDownloadFavicon, base::Unretained(this),
                 icon_type));
  // A max bitmap size is specified to avoid receiving huge bitmaps in
  // OnDidDownloadFavicon(). See FaviconDriver::StartDownload()
  // for more details about the max bitmap size.
  const int download_id =
      delegate_->DownloadImage(image_url, GetMaximalIconSize(handler_type_),
                               image_download_request_.callback());
  DCHECK_NE(download_id, 0);
}

}  // namespace favicon
