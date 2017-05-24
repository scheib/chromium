// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_tiles/most_visited_sites.h"

#include <stddef.h>

#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/sequenced_worker_pool_owner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/history/core/browser/top_sites.h"
#include "components/history/core/browser/top_sites_observer.h"
#include "components/ntp_tiles/constants.h"
#include "components/ntp_tiles/icon_cacher.h"
#include "components/ntp_tiles/json_unsafe_parser.h"
#include "components/ntp_tiles/popular_sites_impl.h"
#include "components/ntp_tiles/pref_names.h"
#include "components/ntp_tiles/switches.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ntp_tiles {

// Defined for googletest. Must be defined in the same namespace.
void PrintTo(const NTPTile& tile, std::ostream* os) {
  *os << "{\"" << tile.title << "\", \"" << tile.url << "\", "
      << static_cast<int>(tile.source) << "}";
}

namespace {

using history::MostVisitedURL;
using history::MostVisitedURLList;
using history::TopSites;
using suggestions::ChromeSuggestion;
using suggestions::SuggestionsProfile;
using suggestions::SuggestionsService;
using testing::AtLeast;
using testing::AllOf;
using testing::AnyNumber;
using testing::ByMove;
using testing::Contains;
using testing::ElementsAre;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::IsEmpty;
using testing::Mock;
using testing::Not;
using testing::Return;
using testing::ReturnRef;
using testing::SizeIs;
using testing::StrictMock;
using testing::_;

const char kHomePageUrl[] = "http://ho.me/";

std::string PrintTile(const std::string& title,
                      const std::string& url,
                      TileSource source) {
  return std::string("has title \"") + title + std::string("\" and url \"") +
         url + std::string("\" and source ") +
         testing::PrintToString(static_cast<int>(source));
}

MATCHER_P3(MatchesTile, title, url, source, PrintTile(title, url, source)) {
  return arg.title == base::ASCIIToUTF16(title) && arg.url == GURL(url) &&
         arg.source == source;
}

MATCHER_P3(FirstTileIs,
           title,
           url,
           source,
           std::string("first tile ") + PrintTile(title, url, source)) {
  return !arg.empty() && arg[0].title == base::ASCIIToUTF16(title) &&
         arg[0].url == GURL(url) && arg[0].source == source;
}

// testing::InvokeArgument<N> does not work with base::Callback, fortunately
// gmock makes it simple to create action templates that do for the various
// possible numbers of arguments.
ACTION_TEMPLATE(InvokeCallbackArgument,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_1_VALUE_PARAMS(p0)) {
  ::std::tr1::get<k>(args).Run(p0);
}

NTPTile MakeTile(const std::string& title,
                 const std::string& url,
                 TileSource source) {
  NTPTile tile;
  tile.title = base::ASCIIToUTF16(title);
  tile.url = GURL(url);
  tile.source = source;
  return tile;
}

ChromeSuggestion MakeSuggestion(const std::string& title,
                                const std::string& url) {
  ChromeSuggestion suggestion;
  suggestion.set_title(title);
  suggestion.set_url(url);
  return suggestion;
}

SuggestionsProfile MakeProfile(
    const std::vector<ChromeSuggestion>& suggestions) {
  SuggestionsProfile profile;
  for (const ChromeSuggestion& suggestion : suggestions) {
    *profile.add_suggestions() = suggestion;
  }
  return profile;
}

MostVisitedURL MakeMostVisitedURL(const std::string& title,
                                  const std::string& url) {
  MostVisitedURL result;
  result.title = base::ASCIIToUTF16(title);
  result.url = GURL(url);
  return result;
}

class MockTopSites : public TopSites {
 public:
  MOCK_METHOD0(ShutdownOnUIThread, void());
  MOCK_METHOD3(SetPageThumbnail,
               bool(const GURL& url,
                    const gfx::Image& thumbnail,
                    const ThumbnailScore& score));
  MOCK_METHOD3(SetPageThumbnailToJPEGBytes,
               bool(const GURL& url,
                    const base::RefCountedMemory* memory,
                    const ThumbnailScore& score));
  MOCK_METHOD2(GetMostVisitedURLs,
               void(const GetMostVisitedURLsCallback& callback,
                    bool include_forced_urls));
  MOCK_METHOD3(GetPageThumbnail,
               bool(const GURL& url,
                    bool prefix_match,
                    scoped_refptr<base::RefCountedMemory>* bytes));
  MOCK_METHOD2(GetPageThumbnailScore,
               bool(const GURL& url, ThumbnailScore* score));
  MOCK_METHOD2(GetTemporaryPageThumbnailScore,
               bool(const GURL& url, ThumbnailScore* score));
  MOCK_METHOD0(SyncWithHistory, void());
  MOCK_CONST_METHOD0(HasBlacklistedItems, bool());
  MOCK_METHOD1(AddBlacklistedURL, void(const GURL& url));
  MOCK_METHOD1(RemoveBlacklistedURL, void(const GURL& url));
  MOCK_METHOD1(IsBlacklisted, bool(const GURL& url));
  MOCK_METHOD0(ClearBlacklistedURLs, void());
  MOCK_METHOD0(StartQueryForMostVisited, base::CancelableTaskTracker::TaskId());
  MOCK_METHOD1(IsKnownURL, bool(const GURL& url));
  MOCK_CONST_METHOD1(GetCanonicalURLString,
                     const std::string&(const GURL& url));
  MOCK_METHOD0(IsNonForcedFull, bool());
  MOCK_METHOD0(IsForcedFull, bool());
  MOCK_CONST_METHOD0(loaded, bool());
  MOCK_METHOD0(GetPrepopulatedPages, history::PrepopulatedPageList());
  MOCK_METHOD2(AddForcedURL, bool(const GURL& url, const base::Time& time));
  MOCK_METHOD1(OnNavigationCommitted, void(const GURL& url));

  // Publicly expose notification to observers, since the implementation cannot
  // be overriden.
  using TopSites::NotifyTopSitesChanged;

 protected:
  ~MockTopSites() override = default;
};

class MockSuggestionsService : public SuggestionsService {
 public:
  MOCK_METHOD0(FetchSuggestionsData, bool());
  MOCK_CONST_METHOD0(GetSuggestionsDataFromCache,
                     base::Optional<SuggestionsProfile>());
  MOCK_METHOD1(AddCallback,
               std::unique_ptr<ResponseCallbackList::Subscription>(
                   const ResponseCallback& callback));
  MOCK_METHOD2(GetPageThumbnail,
               void(const GURL& url, const BitmapCallback& callback));
  MOCK_METHOD3(GetPageThumbnailWithURL,
               void(const GURL& url,
                    const GURL& thumbnail_url,
                    const BitmapCallback& callback));
  MOCK_METHOD1(BlacklistURL, bool(const GURL& candidate_url));
  MOCK_METHOD1(UndoBlacklistURL, bool(const GURL& url));
  MOCK_METHOD0(ClearBlacklist, void());
};

class MockMostVisitedSitesObserver : public MostVisitedSites::Observer {
 public:
  MOCK_METHOD1(OnMostVisitedURLsAvailable, void(const NTPTilesVector& tiles));
  MOCK_METHOD1(OnIconMadeAvailable, void(const GURL& site_url));
};

class FakeHomePageClient : public MostVisitedSites::HomePageClient {
 public:
  FakeHomePageClient() : home_page_enabled_(false), ntp_is_homepage_(false) {}
  ~FakeHomePageClient() override {}

  bool IsHomePageEnabled() const override { return home_page_enabled_; }

  bool IsNewTabPageUsedAsHomePage() const override { return ntp_is_homepage_; }

  GURL GetHomepageUrl() const override { return GURL(kHomePageUrl); }

  void SetHomePageEnabled(bool home_page_enabled) {
    home_page_enabled_ = home_page_enabled;
  }

  void SetNtpIsHomePage(bool ntp_is_homepage) {
    ntp_is_homepage_ = ntp_is_homepage;
  }

 private:
  bool home_page_enabled_;
  bool ntp_is_homepage_;
};

class MockIconCacher : public IconCacher {
 public:
  MOCK_METHOD3(StartFetchPopularSites,
               void(PopularSites::Site site,
                    const base::Closure& icon_available,
                    const base::Closure& preliminary_icon_available));
  MOCK_METHOD2(StartFetchMostLikely,
               void(const GURL& page_url, const base::Closure& icon_available));
};

class PopularSitesFactoryForTest {
 public:
  PopularSitesFactoryForTest(
      bool enabled,
      sync_preferences::TestingPrefServiceSyncable* pref_service)
      : prefs_(pref_service),
        url_fetcher_factory_(/*default_factory=*/nullptr),
        url_request_context_(new net::TestURLRequestContextGetter(
            base::ThreadTaskRunnerHandle::Get())),
        worker_pool_owner_(/*max_threads=*/2, "PopularSitesFactoryForTest.") {
    PopularSitesImpl::RegisterProfilePrefs(pref_service->registry());
    if (enabled) {
      prefs_->SetString(prefs::kPopularSitesOverrideCountry, "IN");
      prefs_->SetString(prefs::kPopularSitesOverrideVersion, "7");

      url_fetcher_factory_.SetFakeResponse(
          GURL("https://www.gstatic.com/chrome/ntp/suggested_sites_IN_7.json"),
          R"([{
                "title": "PopularSite1",
                "url": "http://popularsite1/",
                "favicon_url": "http://popularsite1/favicon.ico"
              },
              {
                "title": "PopularSite2",
                "url": "http://popularsite2/",
                "favicon_url": "http://popularsite2/favicon.ico"
              },
             ])",
          net::HTTP_OK, net::URLRequestStatus::SUCCESS);
    }
  }

  std::unique_ptr<PopularSites> New() {
    return base::MakeUnique<PopularSitesImpl>(
        worker_pool_owner_.pool().get(), prefs_,
        /*template_url_service=*/nullptr,
        /*variations_service=*/nullptr, url_request_context_.get(),
        /*directory=*/base::FilePath(), base::Bind(JsonUnsafeParser::Parse));
  }

 private:
  PrefService* prefs_;
  net::FakeURLFetcherFactory url_fetcher_factory_;
  scoped_refptr<net::TestURLRequestContextGetter> url_request_context_;
  base::SequencedWorkerPoolOwner worker_pool_owner_;
};

// CallbackList-like container without Subscription, mimicking the
// implementation in TopSites (which doesn't use base::CallbackList).
class TopSitesCallbackList {
 public:
  // Second argument declared to match the signature of GetMostVisitedURLs().
  void Add(const TopSites::GetMostVisitedURLsCallback& callback,
           testing::Unused) {
    callbacks_.push_back(callback);
  }

  void ClearAndNotify(const MostVisitedURLList& list) {
    std::vector<TopSites::GetMostVisitedURLsCallback> callbacks;
    callbacks.swap(callbacks_);
    for (const auto& callback : callbacks) {
      callback.Run(list);
    }
  }

  bool empty() const { return callbacks_.empty(); }

 private:
  std::vector<TopSites::GetMostVisitedURLsCallback> callbacks_;
};

// Param specifies whether Popular Sites is enabled via variations.
class MostVisitedSitesTest : public ::testing::TestWithParam<bool> {
 protected:
  MostVisitedSitesTest()
      : popular_sites_factory_(/*enabled=*/GetParam(), &pref_service_),
        mock_top_sites_(new StrictMock<MockTopSites>()) {
    MostVisitedSites::RegisterProfilePrefs(pref_service_.registry());

    if (IsPopularSitesEnabledViaVariations()) {
      base::CommandLine::ForCurrentProcess()->AppendSwitch(
          switches::kEnableNTPPopularSites);
    } else {
      base::CommandLine::ForCurrentProcess()->AppendSwitch(
          switches::kDisableNTPPopularSites);
    }

    // Disable in most tests, this is overriden in a specific test.
    feature_list_.InitAndDisableFeature(
        kNtpMostLikelyFaviconsFromServerFeature);

    // We use StrictMock to make sure the object is not used unless Popular
    // Sites is enabled.
    auto icon_cacher = base::MakeUnique<StrictMock<MockIconCacher>>();
    icon_cacher_ = icon_cacher.get();

    if (IsPopularSitesEnabledViaVariations()) {
      // Populate Popular Sites' internal cache by mimicking a past usage of
      // PopularSitesImpl.
      auto tmp_popular_sites = popular_sites_factory_.New();
      base::RunLoop loop;
      bool save_success = false;
      tmp_popular_sites->MaybeStartFetch(
          /*force_download=*/true,
          base::Bind(
              [](bool* save_success, base::RunLoop* loop, bool success) {
                *save_success = success;
                loop->Quit();
              },
              &save_success, &loop));
      loop.Run();
      EXPECT_TRUE(save_success);

      // With PopularSites enabled, blacklist is exercised.
      EXPECT_CALL(*mock_top_sites_, IsBlacklisted(_))
          .WillRepeatedly(Return(false));
      // Mock icon cacher never replies, and we also don't verify whether the
      // code uses it correctly.
      EXPECT_CALL(*icon_cacher, StartFetchPopularSites(_, _, _))
          .Times(AtLeast(0));
    }

    auto home_page_client = base::MakeUnique<FakeHomePageClient>();
    home_page_client_ = home_page_client.get();

    most_visited_sites_ = base::MakeUnique<MostVisitedSites>(
        &pref_service_, mock_top_sites_, &mock_suggestions_service_,
        popular_sites_factory_.New(), std::move(icon_cacher),
        /*supervisor=*/nullptr, std::move(home_page_client));
  }

  bool IsPopularSitesEnabledViaVariations() const { return GetParam(); }

  bool VerifyAndClearExpectations() {
    base::RunLoop().RunUntilIdle();
    const bool success =
        Mock::VerifyAndClearExpectations(mock_top_sites_.get()) &&
        Mock::VerifyAndClearExpectations(&mock_suggestions_service_) &&
        Mock::VerifyAndClearExpectations(&mock_observer_);
    // For convenience, restore the expectations for IsBlacklisted().
    if (IsPopularSitesEnabledViaVariations()) {
      EXPECT_CALL(*mock_top_sites_, IsBlacklisted(_))
          .WillRepeatedly(Return(false));
    }
    return success;
  }

  void DisableRemoteSuggestions() {
    EXPECT_CALL(mock_suggestions_service_, AddCallback(_))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke(&suggestions_service_callbacks_,
                               &SuggestionsService::ResponseCallbackList::Add));
    EXPECT_CALL(mock_suggestions_service_, GetSuggestionsDataFromCache())
        .Times(AnyNumber())
        .WillRepeatedly(Return(SuggestionsProfile()));  // Empty cache.
    EXPECT_CALL(mock_suggestions_service_, FetchSuggestionsData())
        .Times(AnyNumber())
        .WillRepeatedly(Return(true));
  }

  base::CallbackList<SuggestionsService::ResponseCallback::RunType>
      suggestions_service_callbacks_;
  TopSitesCallbackList top_sites_callbacks_;

  base::MessageLoop message_loop_;
  sync_preferences::TestingPrefServiceSyncable pref_service_;
  PopularSitesFactoryForTest popular_sites_factory_;
  scoped_refptr<StrictMock<MockTopSites>> mock_top_sites_;
  StrictMock<MockSuggestionsService> mock_suggestions_service_;
  StrictMock<MockMostVisitedSitesObserver> mock_observer_;
  FakeHomePageClient* home_page_client_;
  std::unique_ptr<MostVisitedSites> most_visited_sites_;
  base::test::ScopedFeatureList feature_list_;
  MockIconCacher* icon_cacher_;
};

TEST_P(MostVisitedSitesTest, ShouldStartNoCallInConstructor) {
  // No call to mocks expected by the mere fact of instantiating
  // MostVisitedSites.
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesTest, ShouldIncludeTileForHomePage) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(ntp_tiles::kPinHomePageAsTileFeature);
  home_page_client_->SetHomePageEnabled(true);
  DisableRemoteSuggestions();
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillRepeatedly(InvokeCallbackArgument<0>(MostVisitedURLList{}));
  EXPECT_CALL(*mock_top_sites_, SyncWithHistory());
  EXPECT_CALL(*mock_top_sites_, IsBlacklisted(Eq(GURL(kHomePageUrl))))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_observer_, OnMostVisitedURLsAvailable(FirstTileIs(
                                  "", kHomePageUrl, TileSource::HOMEPAGE)));
  most_visited_sites_->SetMostVisitedURLsObserver(&mock_observer_,
                                                  /*num_sites=*/3);
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesTest, ShouldNotIncludeHomePageIfFeatureDisabled) {
  base::test::ScopedFeatureList features;
  features.InitAndDisableFeature(ntp_tiles::kPinHomePageAsTileFeature);
  home_page_client_->SetHomePageEnabled(true);
  DisableRemoteSuggestions();
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillRepeatedly(InvokeCallbackArgument<0>(MostVisitedURLList{}));
  EXPECT_CALL(*mock_top_sites_, SyncWithHistory());
  EXPECT_CALL(*mock_top_sites_, IsBlacklisted(Eq(GURL(kHomePageUrl))))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_observer_,
              OnMostVisitedURLsAvailable(Not(Contains(
                  MatchesTile("", kHomePageUrl, TileSource::HOMEPAGE)))));
  most_visited_sites_->SetMostVisitedURLsObserver(&mock_observer_,
                                                  /*num_sites=*/3);
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesTest, ShouldNotIncludeHomePageIfNoTileRequested) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(ntp_tiles::kPinHomePageAsTileFeature);
  home_page_client_->SetHomePageEnabled(true);
  DisableRemoteSuggestions();
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillRepeatedly(InvokeCallbackArgument<0>(MostVisitedURLList{}));
  EXPECT_CALL(*mock_top_sites_, SyncWithHistory());
  EXPECT_CALL(*mock_top_sites_, IsBlacklisted(Eq(GURL(kHomePageUrl))))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_observer_, OnMostVisitedURLsAvailable(IsEmpty()));
  most_visited_sites_->SetMostVisitedURLsObserver(&mock_observer_,
                                                  /*num_sites=*/0);
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesTest, ShouldReturnOnlyHomePageIfOneTileRequested) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(ntp_tiles::kPinHomePageAsTileFeature);
  home_page_client_->SetHomePageEnabled(true);
  DisableRemoteSuggestions();
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillRepeatedly(InvokeCallbackArgument<0>(
          (MostVisitedURLList{MakeMostVisitedURL("Site 1", "http://site1/")})));
  EXPECT_CALL(*mock_top_sites_, SyncWithHistory());
  EXPECT_CALL(*mock_top_sites_, IsBlacklisted(Eq(GURL(kHomePageUrl))))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_observer_,
              OnMostVisitedURLsAvailable(ElementsAre(
                  MatchesTile("", kHomePageUrl, TileSource::HOMEPAGE))));
  most_visited_sites_->SetMostVisitedURLsObserver(&mock_observer_,
                                                  /*num_sites=*/1);
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesTest, ShouldDeduplicateHomePageWithTopSites) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(ntp_tiles::kPinHomePageAsTileFeature);
  home_page_client_->SetHomePageEnabled(true);
  DisableRemoteSuggestions();
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillRepeatedly(InvokeCallbackArgument<0>(
          (MostVisitedURLList{MakeMostVisitedURL("Site 1", "http://site1/"),
                              MakeMostVisitedURL("", kHomePageUrl)})));
  EXPECT_CALL(*mock_top_sites_, SyncWithHistory());
  EXPECT_CALL(*mock_top_sites_, IsBlacklisted(Eq(GURL(kHomePageUrl))))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_observer_,
              OnMostVisitedURLsAvailable(
                  AllOf(FirstTileIs("", kHomePageUrl, TileSource::HOMEPAGE),
                        Not(Contains(MatchesTile("", kHomePageUrl,
                                                 TileSource::TOP_SITES))))));
  most_visited_sites_->SetMostVisitedURLsObserver(&mock_observer_,
                                                  /*num_sites=*/3);
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesTest, ShouldNotIncludeHomePageIfItIsNewTabPage) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(ntp_tiles::kPinHomePageAsTileFeature);
  home_page_client_->SetHomePageEnabled(true);
  home_page_client_->SetNtpIsHomePage(true);
  DisableRemoteSuggestions();
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillRepeatedly(InvokeCallbackArgument<0>(MostVisitedURLList{}));
  EXPECT_CALL(*mock_top_sites_, SyncWithHistory());
  EXPECT_CALL(*mock_top_sites_, IsBlacklisted(Eq(GURL(kHomePageUrl))))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_observer_,
              OnMostVisitedURLsAvailable(Not(Contains(
                  MatchesTile("", kHomePageUrl, TileSource::HOMEPAGE)))));
  most_visited_sites_->SetMostVisitedURLsObserver(&mock_observer_,
                                                  /*num_sites=*/3);
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesTest, ShouldNotIncludeHomePageIfThereIsNone) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(ntp_tiles::kPinHomePageAsTileFeature);
  home_page_client_->SetHomePageEnabled(false);
  DisableRemoteSuggestions();
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillRepeatedly(InvokeCallbackArgument<0>(MostVisitedURLList{}));
  EXPECT_CALL(*mock_top_sites_, SyncWithHistory());
  EXPECT_CALL(*mock_top_sites_, IsBlacklisted(Eq(GURL(kHomePageUrl))))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_observer_,
              OnMostVisitedURLsAvailable(Not(Contains(
                  MatchesTile("", kHomePageUrl, TileSource::HOMEPAGE)))));
  most_visited_sites_->SetMostVisitedURLsObserver(&mock_observer_,
                                                  /*num_sites=*/3);
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesTest, ShouldNotIncludeHomePageIfBlacklisted) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(ntp_tiles::kPinHomePageAsTileFeature);
  home_page_client_->SetHomePageEnabled(true);
  DisableRemoteSuggestions();
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillRepeatedly(InvokeCallbackArgument<0>(
          (MostVisitedURLList{MakeMostVisitedURL("", kHomePageUrl)})));
  EXPECT_CALL(*mock_top_sites_, SyncWithHistory());
  EXPECT_CALL(*mock_top_sites_, IsBlacklisted(Eq(GURL(kHomePageUrl))))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*mock_top_sites_, IsBlacklisted(Eq(GURL(kHomePageUrl))))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_observer_,
              OnMostVisitedURLsAvailable(Not(Contains(
                  MatchesTile("", kHomePageUrl, TileSource::HOMEPAGE)))));

  most_visited_sites_->SetMostVisitedURLsObserver(&mock_observer_,
                                                  /*num_sites=*/3);
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesTest, ShouldPinHomePageAgainIfBlacklistingUndone) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(ntp_tiles::kPinHomePageAsTileFeature);
  home_page_client_->SetHomePageEnabled(true);

  DisableRemoteSuggestions();
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillOnce(InvokeCallbackArgument<0>(
          (MostVisitedURLList{MakeMostVisitedURL("", kHomePageUrl)})));
  EXPECT_CALL(*mock_top_sites_, SyncWithHistory());
  EXPECT_CALL(*mock_top_sites_, IsBlacklisted(Eq(GURL(kHomePageUrl))))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_observer_,
              OnMostVisitedURLsAvailable(Not(Contains(
                  MatchesTile("", kHomePageUrl, TileSource::HOMEPAGE)))));

  most_visited_sites_->SetMostVisitedURLsObserver(&mock_observer_,
                                                  /*num_sites=*/3);
  base::RunLoop().RunUntilIdle();
  VerifyAndClearExpectations();

  DisableRemoteSuggestions();
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillOnce(InvokeCallbackArgument<0>(MostVisitedURLList{}));
  EXPECT_CALL(*mock_top_sites_, IsBlacklisted(Eq(GURL(kHomePageUrl))))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_observer_, OnMostVisitedURLsAvailable(Contains(MatchesTile(
                                  "", kHomePageUrl, TileSource::HOMEPAGE))));

  most_visited_sites_->OnBlockedSitesChanged();
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesTest, ShouldInformSuggestionSourcesWhenBlacklisting) {
  EXPECT_CALL(*mock_top_sites_, AddBlacklistedURL(Eq(GURL(kHomePageUrl))))
      .Times(1);
  EXPECT_CALL(mock_suggestions_service_, BlacklistURL(Eq(GURL(kHomePageUrl))))
      .Times(AnyNumber());
  most_visited_sites_->AddOrRemoveBlacklistedUrl(GURL(kHomePageUrl),
                                                 /*add_url=*/true);
  EXPECT_CALL(*mock_top_sites_, RemoveBlacklistedURL(Eq(GURL(kHomePageUrl))))
      .Times(1);
  EXPECT_CALL(mock_suggestions_service_,
              UndoBlacklistURL(Eq(GURL(kHomePageUrl))))
      .Times(AnyNumber());
  most_visited_sites_->AddOrRemoveBlacklistedUrl(GURL(kHomePageUrl),
                                                 /*add_url=*/false);
}

TEST_P(MostVisitedSitesTest, ShouldHandleTopSitesCacheHit) {
  // If cached, TopSites returns the tiles synchronously, running the callback
  // even before the function returns.
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillRepeatedly(InvokeCallbackArgument<0>(
          MostVisitedURLList{MakeMostVisitedURL("Site 1", "http://site1/")}));

  InSequence seq;
  EXPECT_CALL(*mock_top_sites_, SyncWithHistory());
  EXPECT_CALL(mock_suggestions_service_, AddCallback(_))
      .WillOnce(Invoke(&suggestions_service_callbacks_,
                       &SuggestionsService::ResponseCallbackList::Add));
  EXPECT_CALL(mock_suggestions_service_, GetSuggestionsDataFromCache())
      .WillOnce(Return(SuggestionsProfile()));  // Empty cache.
  if (IsPopularSitesEnabledViaVariations()) {
    EXPECT_CALL(
        mock_observer_,
        OnMostVisitedURLsAvailable(ElementsAre(
            MatchesTile("Site 1", "http://site1/", TileSource::TOP_SITES),
            MatchesTile("PopularSite1", "http://popularsite1/",
                        TileSource::POPULAR),
            MatchesTile("PopularSite2", "http://popularsite2/",
                        TileSource::POPULAR))));
  } else {
    EXPECT_CALL(mock_observer_,
                OnMostVisitedURLsAvailable(ElementsAre(MatchesTile(
                    "Site 1", "http://site1/", TileSource::TOP_SITES))));
  }
  EXPECT_CALL(mock_suggestions_service_, FetchSuggestionsData())
      .WillOnce(Return(true));

  most_visited_sites_->SetMostVisitedURLsObserver(&mock_observer_,
                                                  /*num_sites=*/3);
  VerifyAndClearExpectations();
  EXPECT_FALSE(suggestions_service_callbacks_.empty());
  CHECK(top_sites_callbacks_.empty());

  // Update by TopSites is propagated.
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillOnce(InvokeCallbackArgument<0>(
          MostVisitedURLList{MakeMostVisitedURL("Site 2", "http://site2/")}));
  if (IsPopularSitesEnabledViaVariations()) {
    EXPECT_CALL(*mock_top_sites_, IsBlacklisted(_))
        .WillRepeatedly(Return(false));
  }
  EXPECT_CALL(mock_observer_, OnMostVisitedURLsAvailable(_));
  mock_top_sites_->NotifyTopSitesChanged(
      history::TopSitesObserver::ChangeReason::MOST_VISITED);
  base::RunLoop().RunUntilIdle();
}

INSTANTIATE_TEST_CASE_P(MostVisitedSitesTest,
                        MostVisitedSitesTest,
                        ::testing::Bool());

class MostVisitedSitesWithCacheHitTest : public MostVisitedSitesTest {
 public:
  // Constructor sets the common expectations for the case where suggestions
  // service has cached results when the observer is registered.
  MostVisitedSitesWithCacheHitTest() {
    InSequence seq;
    EXPECT_CALL(*mock_top_sites_, SyncWithHistory());
    EXPECT_CALL(mock_suggestions_service_, AddCallback(_))
        .WillOnce(Invoke(&suggestions_service_callbacks_,
                         &SuggestionsService::ResponseCallbackList::Add));
    EXPECT_CALL(mock_suggestions_service_, GetSuggestionsDataFromCache())
        .WillOnce(Return(MakeProfile({
            MakeSuggestion("Site 1", "http://site1/"),
            MakeSuggestion("Site 2", "http://site2/"),
            MakeSuggestion("Site 3", "http://site3/"),
        })));
    if (IsPopularSitesEnabledViaVariations()) {
      EXPECT_CALL(mock_observer_,
                  OnMostVisitedURLsAvailable(ElementsAre(
                      MatchesTile("Site 1", "http://site1/",
                                  TileSource::SUGGESTIONS_SERVICE),
                      MatchesTile("Site 2", "http://site2/",
                                  TileSource::SUGGESTIONS_SERVICE),
                      MatchesTile("Site 3", "http://site3/",
                                  TileSource::SUGGESTIONS_SERVICE),
                      MatchesTile("PopularSite1", "http://popularsite1/",
                                  TileSource::POPULAR))));
    } else {
      EXPECT_CALL(mock_observer_,
                  OnMostVisitedURLsAvailable(ElementsAre(
                      MatchesTile("Site 1", "http://site1/",
                                  TileSource::SUGGESTIONS_SERVICE),
                      MatchesTile("Site 2", "http://site2/",
                                  TileSource::SUGGESTIONS_SERVICE),
                      MatchesTile("Site 3", "http://site3/",
                                  TileSource::SUGGESTIONS_SERVICE))));
    }
    EXPECT_CALL(mock_suggestions_service_, FetchSuggestionsData())
        .WillOnce(Return(true));

    most_visited_sites_->SetMostVisitedURLsObserver(&mock_observer_,
                                                    /*num_sites=*/4);
    VerifyAndClearExpectations();

    EXPECT_FALSE(suggestions_service_callbacks_.empty());
    EXPECT_TRUE(top_sites_callbacks_.empty());
  }
};

TEST_P(MostVisitedSitesWithCacheHitTest, ShouldFavorSuggestionsServiceCache) {
  // Constructor sets basic expectations for a suggestions service cache hit.
}

TEST_P(MostVisitedSitesWithCacheHitTest,
       ShouldPropagateUpdateBySuggestionsService) {
  EXPECT_CALL(mock_observer_,
              OnMostVisitedURLsAvailable(
                  ElementsAre(MatchesTile("Site 4", "http://site4/",
                                          TileSource::SUGGESTIONS_SERVICE),
                              MatchesTile("Site 5", "http://site5/",
                                          TileSource::SUGGESTIONS_SERVICE),
                              MatchesTile("Site 6", "http://site6/",
                                          TileSource::SUGGESTIONS_SERVICE),
                              MatchesTile("Site 7", "http://site7/",
                                          TileSource::SUGGESTIONS_SERVICE))));
  suggestions_service_callbacks_.Notify(
      MakeProfile({MakeSuggestion("Site 4", "http://site4/"),
                   MakeSuggestion("Site 5", "http://site5/"),
                   MakeSuggestion("Site 6", "http://site6/"),
                   MakeSuggestion("Site 7", "http://site7/")}));
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesWithCacheHitTest, ShouldTruncateList) {
  EXPECT_CALL(mock_observer_, OnMostVisitedURLsAvailable(SizeIs(4)));
  suggestions_service_callbacks_.Notify(
      MakeProfile({MakeSuggestion("Site 4", "http://site4/"),
                   MakeSuggestion("Site 5", "http://site5/"),
                   MakeSuggestion("Site 6", "http://site6/"),
                   MakeSuggestion("Site 7", "http://site7/"),
                   MakeSuggestion("Site 8", "http://site8/")}));
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesWithCacheHitTest,
       ShouldCompleteWithPopularSitesIffEnabled) {
  if (IsPopularSitesEnabledViaVariations()) {
    EXPECT_CALL(mock_observer_,
                OnMostVisitedURLsAvailable(ElementsAre(
                    MatchesTile("Site 4", "http://site4/",
                                TileSource::SUGGESTIONS_SERVICE),
                    MatchesTile("PopularSite1", "http://popularsite1/",
                                TileSource::POPULAR),
                    MatchesTile("PopularSite2", "http://popularsite2/",
                                TileSource::POPULAR))));
  } else {
    EXPECT_CALL(
        mock_observer_,
        OnMostVisitedURLsAvailable(ElementsAre(MatchesTile(
            "Site 4", "http://site4/", TileSource::SUGGESTIONS_SERVICE))));
  }
  suggestions_service_callbacks_.Notify(
      MakeProfile({MakeSuggestion("Site 4", "http://site4/")}));
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesWithCacheHitTest,
       ShouldSwitchToTopSitesIfEmptyUpdateBySuggestionsService) {
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillOnce(Invoke(&top_sites_callbacks_, &TopSitesCallbackList::Add));
  suggestions_service_callbacks_.Notify(SuggestionsProfile());
  VerifyAndClearExpectations();

  EXPECT_CALL(
      mock_observer_,
      OnMostVisitedURLsAvailable(ElementsAre(
          MatchesTile("Site 4", "http://site4/", TileSource::TOP_SITES),
          MatchesTile("Site 5", "http://site5/", TileSource::TOP_SITES),
          MatchesTile("Site 6", "http://site6/", TileSource::TOP_SITES),
          MatchesTile("Site 7", "http://site7/", TileSource::TOP_SITES))));
  top_sites_callbacks_.ClearAndNotify(
      {MakeMostVisitedURL("Site 4", "http://site4/"),
       MakeMostVisitedURL("Site 5", "http://site5/"),
       MakeMostVisitedURL("Site 6", "http://site6/"),
       MakeMostVisitedURL("Site 7", "http://site7/")});
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesWithCacheHitTest, ShouldFetchFaviconsIfEnabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(kNtpMostLikelyFaviconsFromServerFeature);

  EXPECT_CALL(mock_observer_, OnMostVisitedURLsAvailable(_));
  EXPECT_CALL(*icon_cacher_, StartFetchMostLikely(GURL("http://site4/"), _));

  suggestions_service_callbacks_.Notify(
      MakeProfile({MakeSuggestion("Site 4", "http://site4/")}));
  base::RunLoop().RunUntilIdle();
}

INSTANTIATE_TEST_CASE_P(MostVisitedSitesWithCacheHitTest,
                        MostVisitedSitesWithCacheHitTest,
                        ::testing::Bool());

class MostVisitedSitesWithEmptyCacheTest : public MostVisitedSitesTest {
 public:
  // Constructor sets the common expectations for the case where suggestions
  // service doesn't have cached results when the observer is registered.
  MostVisitedSitesWithEmptyCacheTest() {
    InSequence seq;
    EXPECT_CALL(*mock_top_sites_, SyncWithHistory());
    EXPECT_CALL(mock_suggestions_service_, AddCallback(_))
        .WillOnce(Invoke(&suggestions_service_callbacks_,
                         &SuggestionsService::ResponseCallbackList::Add));
    EXPECT_CALL(mock_suggestions_service_, GetSuggestionsDataFromCache())
        .WillOnce(Return(SuggestionsProfile()));  // Empty cache.
    EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
        .WillOnce(Invoke(&top_sites_callbacks_, &TopSitesCallbackList::Add));
    EXPECT_CALL(mock_suggestions_service_, FetchSuggestionsData())
        .WillOnce(Return(true));

    most_visited_sites_->SetMostVisitedURLsObserver(&mock_observer_,
                                                    /*num_sites=*/3);
    VerifyAndClearExpectations();

    EXPECT_FALSE(suggestions_service_callbacks_.empty());
    EXPECT_FALSE(top_sites_callbacks_.empty());
  }
};

TEST_P(MostVisitedSitesWithEmptyCacheTest,
       ShouldQueryTopSitesAndSuggestionsService) {
  // Constructor sets basic expectations for a suggestions service cache miss.
}

TEST_P(MostVisitedSitesWithEmptyCacheTest,
       ShouldCompleteWithPopularSitesIffEnabled) {
  if (IsPopularSitesEnabledViaVariations()) {
    EXPECT_CALL(mock_observer_,
                OnMostVisitedURLsAvailable(ElementsAre(
                    MatchesTile("Site 4", "http://site4/",
                                TileSource::SUGGESTIONS_SERVICE),
                    MatchesTile("PopularSite1", "http://popularsite1/",
                                TileSource::POPULAR),
                    MatchesTile("PopularSite2", "http://popularsite2/",
                                TileSource::POPULAR))));
  } else {
    EXPECT_CALL(
        mock_observer_,
        OnMostVisitedURLsAvailable(ElementsAre(MatchesTile(
            "Site 4", "http://site4/", TileSource::SUGGESTIONS_SERVICE))));
  }
  suggestions_service_callbacks_.Notify(
      MakeProfile({MakeSuggestion("Site 4", "http://site4/")}));
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesWithEmptyCacheTest,
       ShouldIgnoreTopSitesIfSuggestionsServiceFaster) {
  // Reply from suggestions service triggers and update to our observer.
  EXPECT_CALL(mock_observer_,
              OnMostVisitedURLsAvailable(
                  ElementsAre(MatchesTile("Site 1", "http://site1/",
                                          TileSource::SUGGESTIONS_SERVICE),
                              MatchesTile("Site 2", "http://site2/",
                                          TileSource::SUGGESTIONS_SERVICE),
                              MatchesTile("Site 3", "http://site3/",
                                          TileSource::SUGGESTIONS_SERVICE))));
  suggestions_service_callbacks_.Notify(
      MakeProfile({MakeSuggestion("Site 1", "http://site1/"),
                   MakeSuggestion("Site 2", "http://site2/"),
                   MakeSuggestion("Site 3", "http://site3/")}));
  VerifyAndClearExpectations();

  // Reply from top sites is ignored (i.e. not reported to observer).
  top_sites_callbacks_.ClearAndNotify(
      {MakeMostVisitedURL("Site 4", "http://site4/")});
  VerifyAndClearExpectations();

  // Update by TopSites is also ignored.
  mock_top_sites_->NotifyTopSitesChanged(
      history::TopSitesObserver::ChangeReason::MOST_VISITED);
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesWithEmptyCacheTest,
       ShouldExposeTopSitesIfSuggestionsServiceFasterButEmpty) {
  // Empty reply from suggestions service causes no update to our observer.
  suggestions_service_callbacks_.Notify(SuggestionsProfile());
  VerifyAndClearExpectations();

  // Reply from top sites is propagated to observer.
  EXPECT_CALL(
      mock_observer_,
      OnMostVisitedURLsAvailable(ElementsAre(
          MatchesTile("Site 1", "http://site1/", TileSource::TOP_SITES),
          MatchesTile("Site 2", "http://site2/", TileSource::TOP_SITES),
          MatchesTile("Site 3", "http://site3/", TileSource::TOP_SITES))));
  top_sites_callbacks_.ClearAndNotify(
      {MakeMostVisitedURL("Site 1", "http://site1/"),
       MakeMostVisitedURL("Site 2", "http://site2/"),
       MakeMostVisitedURL("Site 3", "http://site3/")});
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesWithEmptyCacheTest,
       ShouldFavorSuggestionsServiceAlthoughSlower) {
  // Reply from top sites is propagated to observer.
  EXPECT_CALL(
      mock_observer_,
      OnMostVisitedURLsAvailable(ElementsAre(
          MatchesTile("Site 1", "http://site1/", TileSource::TOP_SITES),
          MatchesTile("Site 2", "http://site2/", TileSource::TOP_SITES),
          MatchesTile("Site 3", "http://site3/", TileSource::TOP_SITES))));
  top_sites_callbacks_.ClearAndNotify(
      {MakeMostVisitedURL("Site 1", "http://site1/"),
       MakeMostVisitedURL("Site 2", "http://site2/"),
       MakeMostVisitedURL("Site 3", "http://site3/")});
  VerifyAndClearExpectations();

  // Reply from suggestions service overrides top sites.
  InSequence seq;
  EXPECT_CALL(mock_observer_,
              OnMostVisitedURLsAvailable(
                  ElementsAre(MatchesTile("Site 4", "http://site4/",
                                          TileSource::SUGGESTIONS_SERVICE),
                              MatchesTile("Site 5", "http://site5/",
                                          TileSource::SUGGESTIONS_SERVICE),
                              MatchesTile("Site 6", "http://site6/",
                                          TileSource::SUGGESTIONS_SERVICE))));
  suggestions_service_callbacks_.Notify(
      MakeProfile({MakeSuggestion("Site 4", "http://site4/"),
                   MakeSuggestion("Site 5", "http://site5/"),
                   MakeSuggestion("Site 6", "http://site6/")}));
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesWithEmptyCacheTest,
       ShouldIgnoreSuggestionsServiceIfSlowerAndEmpty) {
  // Reply from top sites is propagated to observer.
  EXPECT_CALL(
      mock_observer_,
      OnMostVisitedURLsAvailable(ElementsAre(
          MatchesTile("Site 1", "http://site1/", TileSource::TOP_SITES),
          MatchesTile("Site 2", "http://site2/", TileSource::TOP_SITES),
          MatchesTile("Site 3", "http://site3/", TileSource::TOP_SITES))));
  top_sites_callbacks_.ClearAndNotify(
      {MakeMostVisitedURL("Site 1", "http://site1/"),
       MakeMostVisitedURL("Site 2", "http://site2/"),
       MakeMostVisitedURL("Site 3", "http://site3/")});
  VerifyAndClearExpectations();

  // Reply from suggestions service is empty and thus ignored.
  suggestions_service_callbacks_.Notify(SuggestionsProfile());
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesWithEmptyCacheTest, ShouldPropagateUpdateByTopSites) {
  // Reply from top sites is propagated to observer.
  EXPECT_CALL(
      mock_observer_,
      OnMostVisitedURLsAvailable(ElementsAre(
          MatchesTile("Site 1", "http://site1/", TileSource::TOP_SITES),
          MatchesTile("Site 2", "http://site2/", TileSource::TOP_SITES),
          MatchesTile("Site 3", "http://site3/", TileSource::TOP_SITES))));
  top_sites_callbacks_.ClearAndNotify(
      {MakeMostVisitedURL("Site 1", "http://site1/"),
       MakeMostVisitedURL("Site 2", "http://site2/"),
       MakeMostVisitedURL("Site 3", "http://site3/")});
  VerifyAndClearExpectations();

  // Reply from suggestions service is empty and thus ignored.
  suggestions_service_callbacks_.Notify(SuggestionsProfile());
  VerifyAndClearExpectations();
  EXPECT_TRUE(top_sites_callbacks_.empty());

  // Update from top sites is propagated to observer.
  EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
      .WillOnce(InvokeCallbackArgument<0>(
          MostVisitedURLList{MakeMostVisitedURL("Site 4", "http://site4/"),
                             MakeMostVisitedURL("Site 5", "http://site5/"),
                             MakeMostVisitedURL("Site 6", "http://site6/")}));
  EXPECT_CALL(
      mock_observer_,
      OnMostVisitedURLsAvailable(ElementsAre(
          MatchesTile("Site 4", "http://site4/", TileSource::TOP_SITES),
          MatchesTile("Site 5", "http://site5/", TileSource::TOP_SITES),
          MatchesTile("Site 6", "http://site6/", TileSource::TOP_SITES))));
  mock_top_sites_->NotifyTopSitesChanged(
      history::TopSitesObserver::ChangeReason::MOST_VISITED);
  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesWithEmptyCacheTest,
       ShouldSendEmptyListIfBothTopSitesAndSuggestionsServiceEmpty) {
  if (IsPopularSitesEnabledViaVariations()) {
    EXPECT_CALL(mock_observer_,
                OnMostVisitedURLsAvailable(ElementsAre(
                    MatchesTile("PopularSite1", "http://popularsite1/",
                                TileSource::POPULAR),
                    MatchesTile("PopularSite2", "http://popularsite2/",
                                TileSource::POPULAR))));
  } else {
    // The Android NTP doesn't finish initialization until it gets tiles, so a
    // 0-tile notification is always needed.
    EXPECT_CALL(mock_observer_, OnMostVisitedURLsAvailable(IsEmpty()));
  }
  suggestions_service_callbacks_.Notify(SuggestionsProfile());
  top_sites_callbacks_.ClearAndNotify(MostVisitedURLList{});

  base::RunLoop().RunUntilIdle();
}

TEST_P(MostVisitedSitesWithEmptyCacheTest,
       ShouldNotifyOnceIfTopSitesUnchanged) {
  EXPECT_CALL(
      mock_observer_,
      OnMostVisitedURLsAvailable(ElementsAre(
          MatchesTile("Site 1", "http://site1/", TileSource::TOP_SITES),
          MatchesTile("Site 2", "http://site2/", TileSource::TOP_SITES),
          MatchesTile("Site 3", "http://site3/", TileSource::TOP_SITES))));

  suggestions_service_callbacks_.Notify(SuggestionsProfile());

  top_sites_callbacks_.ClearAndNotify(
      {MakeMostVisitedURL("Site 1", "http://site1/"),
       MakeMostVisitedURL("Site 2", "http://site2/"),
       MakeMostVisitedURL("Site 3", "http://site3/")});
  base::RunLoop().RunUntilIdle();

  for (int i = 0; i < 4; ++i) {
    EXPECT_CALL(*mock_top_sites_, GetMostVisitedURLs(_, false))
        .WillOnce(Invoke(&top_sites_callbacks_, &TopSitesCallbackList::Add));
    mock_top_sites_->NotifyTopSitesChanged(
        history::TopSitesObserver::ChangeReason::MOST_VISITED);
    EXPECT_FALSE(top_sites_callbacks_.empty());
    top_sites_callbacks_.ClearAndNotify(
        {MakeMostVisitedURL("Site 1", "http://site1/"),
         MakeMostVisitedURL("Site 2", "http://site2/"),
         MakeMostVisitedURL("Site 3", "http://site3/")});
    base::RunLoop().RunUntilIdle();
  }
}

TEST_P(MostVisitedSitesWithEmptyCacheTest,
       ShouldNotifyOnceIfSuggestionsUnchanged) {
  EXPECT_CALL(mock_observer_,
              OnMostVisitedURLsAvailable(
                  ElementsAre(MatchesTile("Site 1", "http://site1/",
                                          TileSource::SUGGESTIONS_SERVICE),
                              MatchesTile("Site 2", "http://site2/",
                                          TileSource::SUGGESTIONS_SERVICE),
                              MatchesTile("Site 3", "http://site3/",
                                          TileSource::SUGGESTIONS_SERVICE))));

  for (int i = 0; i < 5; ++i) {
    suggestions_service_callbacks_.Notify(
        MakeProfile({MakeSuggestion("Site 1", "http://site1/"),
                     MakeSuggestion("Site 2", "http://site2/"),
                     MakeSuggestion("Site 3", "http://site3/")}));
  }
}

INSTANTIATE_TEST_CASE_P(MostVisitedSitesWithEmptyCacheTest,
                        MostVisitedSitesWithEmptyCacheTest,
                        ::testing::Bool());

// This a test for MostVisitedSites::MergeTiles(...) method, and thus has the
// same scope as the method itself. This tests merging popular sites with
// personal tiles.
// More important things out of the scope of testing presently:
// - Removing blacklisted tiles.
// - Correct host extraction from the URL.
// - Ensuring personal tiles are not duplicated in popular tiles.
TEST(MostVisitedSitesMergeTest, ShouldMergeTilesWithPersonalOnly) {
  std::vector<NTPTile> personal_tiles{
      MakeTile("Site 1", "https://www.site1.com/", TileSource::TOP_SITES),
      MakeTile("Site 2", "https://www.site2.com/", TileSource::TOP_SITES),
      MakeTile("Site 3", "https://www.site3.com/", TileSource::TOP_SITES),
      MakeTile("Site 4", "https://www.site4.com/", TileSource::TOP_SITES),
  };
  // Without any popular tiles, the result after merge should be the personal
  // tiles.
  EXPECT_THAT(MostVisitedSites::MergeTiles(std::move(personal_tiles),
                                           /*whitelist_tiles=*/NTPTilesVector(),
                                           /*popular_tiles=*/NTPTilesVector()),
              ElementsAre(MatchesTile("Site 1", "https://www.site1.com/",
                                      TileSource::TOP_SITES),
                          MatchesTile("Site 2", "https://www.site2.com/",
                                      TileSource::TOP_SITES),
                          MatchesTile("Site 3", "https://www.site3.com/",
                                      TileSource::TOP_SITES),
                          MatchesTile("Site 4", "https://www.site4.com/",
                                      TileSource::TOP_SITES)));
}

TEST(MostVisitedSitesMergeTest, ShouldMergeTilesWithPopularOnly) {
  std::vector<NTPTile> popular_tiles{
      MakeTile("Site 1", "https://www.site1.com/", TileSource::POPULAR),
      MakeTile("Site 2", "https://www.site2.com/", TileSource::POPULAR),
      MakeTile("Site 3", "https://www.site3.com/", TileSource::POPULAR),
      MakeTile("Site 4", "https://www.site4.com/", TileSource::POPULAR),
  };
  // Without any personal tiles, the result after merge should be the popular
  // tiles.
  EXPECT_THAT(
      MostVisitedSites::MergeTiles(/*personal_tiles=*/NTPTilesVector(),
                                   /*whitelist_tiles=*/NTPTilesVector(),
                                   /*popular_tiles=*/std::move(popular_tiles)),
      ElementsAre(
          MatchesTile("Site 1", "https://www.site1.com/", TileSource::POPULAR),
          MatchesTile("Site 2", "https://www.site2.com/", TileSource::POPULAR),
          MatchesTile("Site 3", "https://www.site3.com/", TileSource::POPULAR),
          MatchesTile("Site 4", "https://www.site4.com/",
                      TileSource::POPULAR)));
}

TEST(MostVisitedSitesMergeTest, ShouldMergeTilesFavoringPersonalOverPopular) {
  std::vector<NTPTile> popular_tiles{
      MakeTile("Site 1", "https://www.site1.com/", TileSource::POPULAR),
      MakeTile("Site 2", "https://www.site2.com/", TileSource::POPULAR),
  };
  std::vector<NTPTile> personal_tiles{
      MakeTile("Site 3", "https://www.site3.com/", TileSource::TOP_SITES),
      MakeTile("Site 4", "https://www.site4.com/", TileSource::TOP_SITES),
  };
  EXPECT_THAT(
      MostVisitedSites::MergeTiles(std::move(personal_tiles),
                                   /*whitelist_tiles=*/NTPTilesVector(),
                                   /*popular_tiles=*/std::move(popular_tiles)),
      ElementsAre(
          MatchesTile("Site 3", "https://www.site3.com/",
                      TileSource::TOP_SITES),
          MatchesTile("Site 4", "https://www.site4.com/",
                      TileSource::TOP_SITES),
          MatchesTile("Site 1", "https://www.site1.com/", TileSource::POPULAR),
          MatchesTile("Site 2", "https://www.site2.com/",
                      TileSource::POPULAR)));
}

}  // namespace
}  // namespace ntp_tiles
