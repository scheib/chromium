// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_tiles/most_visited_sites.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/feature_list.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/utf_string_conversions.h"
#include "components/history/core/browser/top_sites.h"
#include "components/ntp_tiles/constants.h"
#include "components/ntp_tiles/field_trial.h"
#include "components/ntp_tiles/icon_cacher.h"
#include "components/ntp_tiles/pref_names.h"
#include "components/ntp_tiles/switches.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"

using history::TopSites;
using suggestions::ChromeSuggestion;
using suggestions::SuggestionsProfile;
using suggestions::SuggestionsService;

namespace ntp_tiles {

namespace {

const base::Feature kDisplaySuggestionsServiceTiles{
    "DisplaySuggestionsServiceTiles", base::FEATURE_ENABLED_BY_DEFAULT};

// Determine whether we need any tiles from PopularSites to fill up a grid of
// |num_tiles| tiles.
bool NeedPopularSites(const PrefService* prefs, int num_tiles) {
  return prefs->GetInteger(prefs::kNumPersonalTiles) < num_tiles;
}

bool AreURLsEquivalent(const GURL& url1, const GURL& url2) {
  return url1.host_piece() == url2.host_piece() &&
         url1.path_piece() == url2.path_piece();
}

}  // namespace

MostVisitedSites::MostVisitedSites(
    PrefService* prefs,
    scoped_refptr<history::TopSites> top_sites,
    SuggestionsService* suggestions,
    std::unique_ptr<PopularSites> popular_sites,
    std::unique_ptr<IconCacher> icon_cacher,
    std::unique_ptr<MostVisitedSitesSupervisor> supervisor,
    std::unique_ptr<HomePageClient> home_page_client)
    : prefs_(prefs),
      top_sites_(top_sites),
      suggestions_service_(suggestions),
      popular_sites_(std::move(popular_sites)),
      icon_cacher_(std::move(icon_cacher)),
      supervisor_(std::move(supervisor)),
      home_page_client_(std::move(home_page_client)),
      observer_(nullptr),
      num_sites_(0u),
      top_sites_observer_(this),
      mv_source_(TileSource::TOP_SITES),
      top_sites_weak_ptr_factory_(this) {
  DCHECK(prefs_);
  // top_sites_ can be null in tests.
  // TODO(sfiera): have iOS use a dummy TopSites in its tests.
  DCHECK(suggestions_service_);
  if (supervisor_)
    supervisor_->SetObserver(this);
}

MostVisitedSites::MostVisitedSites(
    PrefService* prefs,
    scoped_refptr<history::TopSites> top_sites,
    SuggestionsService* suggestions,
    std::unique_ptr<PopularSites> popular_sites,
    std::unique_ptr<IconCacher> icon_cacher,
    std::unique_ptr<MostVisitedSitesSupervisor> supervisor)
    : MostVisitedSites(prefs,
                       top_sites,
                       suggestions,
                       std::move(popular_sites),
                       std::move(icon_cacher),
                       std::move(supervisor),
                       nullptr) {}

MostVisitedSites::~MostVisitedSites() {
  if (supervisor_)
    supervisor_->SetObserver(nullptr);
}

bool MostVisitedSites::DoesSourceExist(TileSource source) const {
  switch (source) {
    case TileSource::TOP_SITES:
      return top_sites_ != nullptr;
    case TileSource::SUGGESTIONS_SERVICE:
      return suggestions_service_ != nullptr;
    case TileSource::POPULAR:
      return popular_sites_ != nullptr;
    case TileSource::WHITELIST:
      return supervisor_ != nullptr;
    case TileSource::HOMEPAGE:
      return home_page_client_ != nullptr;
  }
  NOTREACHED();
  return false;
}

void MostVisitedSites::SetMostVisitedURLsObserver(Observer* observer,
                                                  size_t num_sites) {
  DCHECK(observer);
  observer_ = observer;
  num_sites_ = num_sites;

  // The order for this condition is important, ShouldShowPopularSites() should
  // always be called last to keep metrics as relevant as possible.
  if (popular_sites_ && NeedPopularSites(prefs_, num_sites_) &&
      ShouldShowPopularSites()) {
    popular_sites_->MaybeStartFetch(
        false, base::Bind(&MostVisitedSites::OnPopularSitesDownloaded,
                          base::Unretained(this)));
  }

  if (top_sites_) {
    // TopSites updates itself after a delay. To ensure up-to-date results,
    // force an update now.
    top_sites_->SyncWithHistory();

    // Register as TopSitesObserver so that we can update ourselves when the
    // TopSites changes.
    top_sites_observer_.Add(top_sites_.get());
  }

  suggestions_subscription_ = suggestions_service_->AddCallback(base::Bind(
      &MostVisitedSites::OnSuggestionsProfileChanged, base::Unretained(this)));

  // Immediately build the current set of tiles, getting suggestions from the
  // SuggestionsService's cache or, if that is empty, sites from TopSites.
  BuildCurrentTiles();
  // Also start a request for fresh suggestions.
  Refresh();
}

void MostVisitedSites::Refresh() {
  suggestions_service_->FetchSuggestionsData();
}

void MostVisitedSites::AddOrRemoveBlacklistedUrl(const GURL& url,
                                                 bool add_url) {
  if (add_url) {
    base::RecordAction(base::UserMetricsAction("Suggestions.Site.Removed"));
  } else {
    base::RecordAction(
        base::UserMetricsAction("Suggestions.Site.RemovalUndone"));
  }

  if (top_sites_) {
    // Always blacklist in the local TopSites.
    if (add_url)
      top_sites_->AddBlacklistedURL(url);
    else
      top_sites_->RemoveBlacklistedURL(url);
  }

  // Only blacklist in the server-side suggestions service if it's active.
  if (mv_source_ == TileSource::SUGGESTIONS_SERVICE) {
    if (add_url)
      suggestions_service_->BlacklistURL(url);
    else
      suggestions_service_->UndoBlacklistURL(url);
  }
}

void MostVisitedSites::ClearBlacklistedUrls() {
  if (top_sites_) {
    // Always update the blacklist in the local TopSites.
    top_sites_->ClearBlacklistedURLs();
  }

  // Only update the server-side blacklist if it's active.
  if (mv_source_ == TileSource::SUGGESTIONS_SERVICE) {
    suggestions_service_->ClearBlacklist();
  }
}

void MostVisitedSites::OnBlockedSitesChanged() {
  BuildCurrentTiles();
}

// static
void MostVisitedSites::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(prefs::kNumPersonalTiles, 0);
}

void MostVisitedSites::InitiateTopSitesQuery() {
  if (!top_sites_)
    return;
  if (top_sites_weak_ptr_factory_.HasWeakPtrs())
    return;  // Ongoing query.
  top_sites_->GetMostVisitedURLs(
      base::Bind(&MostVisitedSites::OnMostVisitedURLsAvailable,
                 top_sites_weak_ptr_factory_.GetWeakPtr()),
      false);
}

base::FilePath MostVisitedSites::GetWhitelistLargeIconPath(const GURL& url) {
  if (supervisor_) {
    for (const auto& whitelist : supervisor_->whitelists()) {
      if (AreURLsEquivalent(whitelist.entry_point, url))
        return whitelist.large_icon_path;
    }
  }
  return base::FilePath();
}

void MostVisitedSites::OnMostVisitedURLsAvailable(
    const history::MostVisitedURLList& visited_list) {
  // Ignore the event if tiles provided by the Suggestions Service, which take
  // precedence.
  if (mv_source_ == TileSource::SUGGESTIONS_SERVICE) {
    return;
  }

  NTPTilesVector tiles;
  size_t num_tiles = std::min(visited_list.size(), num_sites_);
  for (size_t i = 0; i < num_tiles; ++i) {
    const history::MostVisitedURL& visited = visited_list[i];
    if (visited.url.is_empty())
      break;  // This is the signal that there are no more real visited sites.
    if (supervisor_ && supervisor_->IsBlocked(visited.url))
      continue;

    NTPTile tile;
    tile.title = visited.title;
    tile.url = visited.url;
    tile.source = TileSource::TOP_SITES;
    tile.whitelist_icon_path = GetWhitelistLargeIconPath(visited.url);

    tiles.push_back(std::move(tile));
  }

  mv_source_ = TileSource::TOP_SITES;
  SaveNewTilesAndNotify(std::move(tiles));
}

void MostVisitedSites::OnSuggestionsProfileChanged(
    const SuggestionsProfile& suggestions_profile) {
  if (suggestions_profile.suggestions_size() == 0 &&
      mv_source_ != TileSource::SUGGESTIONS_SERVICE) {
    return;
  }

  BuildCurrentTilesGivenSuggestionsProfile(suggestions_profile);
}

void MostVisitedSites::BuildCurrentTiles() {
  BuildCurrentTilesGivenSuggestionsProfile(
      suggestions_service_->GetSuggestionsDataFromCache().value_or(
          SuggestionsProfile()));
}

void MostVisitedSites::BuildCurrentTilesGivenSuggestionsProfile(
    const suggestions::SuggestionsProfile& suggestions_profile) {
  size_t num_tiles = suggestions_profile.suggestions_size();
  // With no server suggestions, fall back to local TopSites.
  if (num_tiles == 0 ||
      !base::FeatureList::IsEnabled(kDisplaySuggestionsServiceTiles)) {
    mv_source_ = TileSource::TOP_SITES;
    InitiateTopSitesQuery();
    return;
  }
  if (num_sites_ < num_tiles)
    num_tiles = num_sites_;

  NTPTilesVector tiles;
  for (size_t i = 0; i < num_tiles; ++i) {
    const ChromeSuggestion& suggestion_pb = suggestions_profile.suggestions(i);
    GURL url(suggestion_pb.url());
    if (supervisor_ && supervisor_->IsBlocked(url))
      continue;

    NTPTile tile;
    tile.title = base::UTF8ToUTF16(suggestion_pb.title());
    tile.url = url;
    tile.source = TileSource::SUGGESTIONS_SERVICE;
    tile.whitelist_icon_path = GetWhitelistLargeIconPath(url);
    tile.thumbnail_url = GURL(suggestion_pb.thumbnail());
    tile.favicon_url = GURL(suggestion_pb.favicon_url());
    if (AreNtpMostLikelyFaviconsFromServerEnabled()) {
      icon_cacher_->StartFetchMostLikely(
          url, base::Bind(&MostVisitedSites::OnIconMadeAvailable,
                          base::Unretained(this), url));
    }

    tiles.push_back(std::move(tile));
  }

  mv_source_ = TileSource::SUGGESTIONS_SERVICE;
  SaveNewTilesAndNotify(std::move(tiles));
}

NTPTilesVector MostVisitedSites::CreateWhitelistEntryPointTiles(
    const std::set<std::string>& used_hosts,
    size_t num_actual_tiles) {
  if (!supervisor_) {
    return NTPTilesVector();
  }

  NTPTilesVector whitelist_tiles;
  for (const auto& whitelist : supervisor_->whitelists()) {
    if (whitelist_tiles.size() + num_actual_tiles >= num_sites_)
      break;

    // Skip blacklisted sites.
    if (top_sites_ && top_sites_->IsBlacklisted(whitelist.entry_point))
      continue;

    // Skip tiles already present.
    if (used_hosts.find(whitelist.entry_point.host()) != used_hosts.end())
      continue;

    // Skip whitelist entry points that are manually blocked.
    if (supervisor_->IsBlocked(whitelist.entry_point))
      continue;

    NTPTile tile;
    tile.title = whitelist.title;
    tile.url = whitelist.entry_point;
    tile.source = TileSource::WHITELIST;
    tile.whitelist_icon_path = whitelist.large_icon_path;
    whitelist_tiles.push_back(std::move(tile));
  }

  return whitelist_tiles;
}

NTPTilesVector MostVisitedSites::CreatePopularSitesTiles(
    const std::set<std::string>& used_hosts,
    size_t num_actual_tiles) {
  // For child accounts popular sites tiles will not be added.
  if (supervisor_ && supervisor_->IsChildProfile()) {
    return NTPTilesVector();
  }

  if (!popular_sites_ || !ShouldShowPopularSites()) {
    return NTPTilesVector();
  }

  // Collect non-blacklisted popular suggestions, skipping those already present
  // in the personal suggestions.
  NTPTilesVector popular_sites_tiles;
  for (const PopularSites::Site& popular_site : popular_sites_->sites()) {
    if (popular_sites_tiles.size() + num_actual_tiles >= num_sites_)
      break;

    // Skip blacklisted sites.
    if (top_sites_ && top_sites_->IsBlacklisted(popular_site.url))
      continue;

    const std::string& host = popular_site.url.host();
    // Skip tiles already present in personal or whitelists.
    if (used_hosts.find(host) != used_hosts.end())
      continue;

    NTPTile tile;
    tile.title = popular_site.title;
    tile.url = GURL(popular_site.url);
    tile.source = TileSource::POPULAR;
    popular_sites_tiles.push_back(std::move(tile));
    base::Closure icon_available =
        base::Bind(&MostVisitedSites::OnIconMadeAvailable,
                   base::Unretained(this), popular_site.url);
    icon_cacher_->StartFetchPopularSites(popular_site, icon_available,
                                         icon_available);
  }
  return popular_sites_tiles;
}

NTPTilesVector MostVisitedSites::CreatePersonalTilesWithHomeTile(
    NTPTilesVector tiles) const {
  DCHECK(home_page_client_);
  DCHECK_GT(num_sites_, 0u);

  const GURL& home_page_url = home_page_client_->GetHomepageUrl();
  NTPTilesVector new_tiles;
  // Add the home tile as first tile.
  NTPTile home_tile;
  home_tile.url = home_page_url;
  home_tile.source = TileSource::HOMEPAGE;
  new_tiles.push_back(std::move(home_tile));

  for (auto& tile : tiles) {
    if (new_tiles.size() >= num_sites_) {
      break;
    }

    if (tile.url.host() == home_page_url.host()) {
      continue;
    }

    new_tiles.push_back(std::move(tile));
  }
  return new_tiles;
}

void MostVisitedSites::SaveNewTilesAndNotify(NTPTilesVector personal_tiles) {
  std::set<std::string> used_hosts;
  size_t num_actual_tiles = 0u;

  if (ShouldAddHomeTile()) {
    personal_tiles = CreatePersonalTilesWithHomeTile(std::move(personal_tiles));
  }
  AddToHostsAndTotalCount(personal_tiles, &used_hosts, &num_actual_tiles);

  NTPTilesVector whitelist_tiles =
      CreateWhitelistEntryPointTiles(used_hosts, num_actual_tiles);
  AddToHostsAndTotalCount(whitelist_tiles, &used_hosts, &num_actual_tiles);

  NTPTilesVector popular_sites_tiles =
      CreatePopularSitesTiles(used_hosts, num_actual_tiles);
  AddToHostsAndTotalCount(popular_sites_tiles, &used_hosts, &num_actual_tiles);

  NTPTilesVector new_tiles =
      MergeTiles(std::move(personal_tiles), std::move(whitelist_tiles),
                 std::move(popular_sites_tiles));
  if (current_tiles_.has_value() && (*current_tiles_ == new_tiles)) {
    return;
  }

  current_tiles_.emplace(std::move(new_tiles));
  DCHECK_EQ(num_actual_tiles, current_tiles_->size());

  int num_personal_tiles = 0;
  for (const auto& tile : *current_tiles_) {
    if (tile.source != TileSource::POPULAR)
      num_personal_tiles++;
  }
  prefs_->SetInteger(prefs::kNumPersonalTiles, num_personal_tiles);

  if (!observer_)
    return;
  observer_->OnMostVisitedURLsAvailable(*current_tiles_);
}

// static
NTPTilesVector MostVisitedSites::MergeTiles(NTPTilesVector personal_tiles,
                                            NTPTilesVector whitelist_tiles,
                                            NTPTilesVector popular_tiles) {
  NTPTilesVector merged_tiles;
  std::move(personal_tiles.begin(), personal_tiles.end(),
            std::back_inserter(merged_tiles));
  std::move(whitelist_tiles.begin(), whitelist_tiles.end(),
            std::back_inserter(merged_tiles));
  std::move(popular_tiles.begin(), popular_tiles.end(),
            std::back_inserter(merged_tiles));
  return merged_tiles;
}

void MostVisitedSites::OnPopularSitesDownloaded(bool success) {
  if (!success) {
    LOG(WARNING) << "Download of popular sites failed";
    return;
  }

  for (const PopularSites::Site& popular_site : popular_sites_->sites()) {
    // Ignore callback; these icons will be seen on the *next* NTP.
    icon_cacher_->StartFetchPopularSites(popular_site, base::Closure(),
                                         base::Closure());
  }
}

void MostVisitedSites::OnIconMadeAvailable(const GURL& site_url) {
  observer_->OnIconMadeAvailable(site_url);
}

void MostVisitedSites::TopSitesLoaded(TopSites* top_sites) {}

void MostVisitedSites::TopSitesChanged(TopSites* top_sites,
                                       ChangeReason change_reason) {
  if (mv_source_ == TileSource::TOP_SITES) {
    // The displayed tiles are invalidated.
    InitiateTopSitesQuery();
  }
}

bool MostVisitedSites::ShouldAddHomeTile() const {
  return base::FeatureList::IsEnabled(kPinHomePageAsTileFeature) &&
         num_sites_ > 0u && home_page_client_ &&
         home_page_client_->IsHomePageEnabled() &&
         !home_page_client_->IsNewTabPageUsedAsHomePage() &&
         !(top_sites_ &&
           top_sites_->IsBlacklisted(home_page_client_->GetHomepageUrl()));
}

void MostVisitedSites::AddToHostsAndTotalCount(const NTPTilesVector& new_tiles,
                                               std::set<std::string>* hosts,
                                               size_t* total_tile_count) const {
  for (const auto& tile : new_tiles) {
    hosts->insert(tile.url.host());
  }
  *total_tile_count += new_tiles.size();
  DCHECK_LE(*total_tile_count, num_sites_);
}

}  // namespace ntp_tiles
