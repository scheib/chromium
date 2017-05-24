// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/subresource_filter/content/browser/subresource_filter_safe_browsing_activation_throttle.h"

#include <memory>
#include <tuple>
#include <utility>

#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial.h"
#include "base/run_loop.h"
#include "base/test/histogram_tester.h"
#include "base/test/test_mock_time_task_runner.h"
#include "components/safe_browsing_db/test_database_manager.h"
#include "components/subresource_filter/content/browser/content_subresource_filter_driver_factory.h"
#include "components/subresource_filter/content/browser/fake_safe_browsing_database_manager.h"
#include "components/subresource_filter/content/browser/subresource_filter_client.h"
#include "components/subresource_filter/content/browser/subresource_filter_safe_browsing_client.h"
#include "components/subresource_filter/content/browser/subresource_filter_safe_browsing_client_request.h"
#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "components/subresource_filter/core/browser/subresource_filter_features_test_support.h"
#include "components/subresource_filter/core/common/activation_decision.h"
#include "components/subresource_filter/core/common/activation_level.h"
#include "components/subresource_filter/core/common/activation_list.h"
#include "components/subresource_filter/core/common/activation_state.h"
#include "components/subresource_filter/core/common/test_ruleset_creator.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/cancelling_navigation_throttle.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace subresource_filter {

namespace {

const char kUrlA[] = "https://example_a.com";
const char kUrlB[] = "https://example_b.com";
const char kUrlC[] = "https://example_c.com";
const char kUrlD[] = "https://example_d.com";

char kURL[] = "http://example.test/";
char kRedirectURL[] = "http://redirect.test/";

// Names of navigation chain patterns histogram.
const char kMatchesPatternHistogramNameSubresourceFilterSuffix[] =
    "SubresourceFilter.PageLoad.RedirectChainMatchPattern."
    "SubresourceFilterOnly";
const char kNavigationChainSizeSubresourceFilterSuffix[] =
    "SubresourceFilter.PageLoad.RedirectChainLength.SubresourceFilterOnly";
const char kSafeBrowsingNavigationDelay[] =
    "SubresourceFilter.PageLoad.SafeBrowsingDelay";
const char kSafeBrowsingNavigationDelayNoSpeculation[] =
    "SubresourceFilter.PageLoad.SafeBrowsingDelay.NoRedirectSpeculation";
const char kSafeBrowsingCheckTime[] =
    "SubresourceFilter.SafeBrowsing.CheckTime";
const char kMatchesPatternHistogramName[] =
    "SubresourceFilter.PageLoad.FinalURLMatch.";
const char kNavigationChainSize[] =
    "SubresourceFilter.PageLoad.RedirectChainLength.";

class MockSubresourceFilterClient
    : public subresource_filter::SubresourceFilterClient {
 public:
  MockSubresourceFilterClient() {}

  ~MockSubresourceFilterClient() override = default;

  MOCK_METHOD1(ToggleNotificationVisibility, void(bool));
  MOCK_METHOD2(OnPageActivationComputed,
               bool(content::NavigationHandle*, bool));
  MOCK_METHOD1(WhitelistInCurrentWebContents, void(const GURL&));
  MOCK_METHOD0(GetRulesetDealer, VerifiedRulesetDealer::Handle*());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockSubresourceFilterClient);
};

std::string GetSuffixForList(const ActivationList& type) {
  switch (type) {
    case ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL:
      return "SocialEngineeringAdsInterstitial";
    case ActivationList::PHISHING_INTERSTITIAL:
      return "PhishingInterstitial";
    case ActivationList::SUBRESOURCE_FILTER:
      return "SubresourceFilterOnly";
    case ActivationList::NONE:
      return std::string();
  }
  return std::string();
}

struct ActivationListTestData {
  const char* const activation_list;
  ActivationList activation_list_type;
  safe_browsing::SBThreatType threat_type;
  safe_browsing::ThreatPatternType threat_type_metadata;
};

const ActivationListTestData kActivationListTestData[] = {
    {subresource_filter::kActivationListSocialEngineeringAdsInterstitial,
     ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_URL_PHISHING,
     safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS},
    {subresource_filter::kActivationListPhishingInterstitial,
     ActivationList::PHISHING_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_URL_PHISHING,
     safe_browsing::ThreatPatternType::NONE},
    {subresource_filter::kActivationListSubresourceFilter,
     ActivationList::SUBRESOURCE_FILTER,
     safe_browsing::SB_THREAT_TYPE_SUBRESOURCE_FILTER,
     safe_browsing::ThreatPatternType::NONE},
};

void ExpectSampleForSuffix(const std::string& suffix,
                           const std::string& match_suffix,
                           const base::HistogramTester& tester) {
  tester.ExpectUniqueSample(kMatchesPatternHistogramName + suffix,
                            (suffix == match_suffix), 1);
}

}  //  namespace

class SubresourceFilterSafeBrowsingActivationThrottleTest
    : public content::RenderViewHostTestHarness,
      public content::WebContentsObserver {
 public:
  SubresourceFilterSafeBrowsingActivationThrottleTest()
      : field_trial_list_(nullptr) {}
  ~SubresourceFilterSafeBrowsingActivationThrottleTest() override {}

  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    Configure();
    test_io_task_runner_ = new base::TestMockTimeTaskRunner();
    // Note: Using NiceMock to allow uninteresting calls and suppress warnings.
    client_ =
        base::MakeUnique<::testing::NiceMock<MockSubresourceFilterClient>>();
    ContentSubresourceFilterDriverFactory::CreateForWebContents(
        RenderViewHostTestHarness::web_contents(), client_.get());
    fake_safe_browsing_database_ = new FakeSafeBrowsingDatabaseManager();
    NavigateAndCommit(GURL("https://test.com"));
    Observe(RenderViewHostTestHarness::web_contents());
  }

  virtual void Configure() {
    scoped_configuration_.ResetConfiguration(Configuration(
        ActivationLevel::ENABLED, ActivationScope::ACTIVATION_LIST,
        ActivationList::SUBRESOURCE_FILTER));
  }

  void TearDown() override {
    RunUntilIdle();
    content::RenderViewHostTestHarness::TearDown();
  }

  ContentSubresourceFilterDriverFactory* factory() {
    return ContentSubresourceFilterDriverFactory::FromWebContents(
        RenderViewHostTestHarness::web_contents());
  }

  // content::WebContentsObserver:
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override {
    ASSERT_TRUE(navigation_handle->IsInMainFrame());
    navigation_handle->RegisterThrottleForTesting(
        base::MakeUnique<SubresourceFilterSafeBrowsingActivationThrottle>(
            navigation_handle, test_io_task_runner_,
            fake_safe_browsing_database_));
  }

  content::NavigationThrottle::ThrottleCheckResult SimulateStart() {
    navigation_simulator_->Start();
    auto result = navigation_simulator_->GetLastThrottleCheckResult();
    if (result == content::NavigationThrottle::CANCEL)
      navigation_simulator_.reset();
    return result;
  }

  content::NavigationThrottle::ThrottleCheckResult SimulateRedirect(
      const GURL& new_url) {
    navigation_simulator_->Redirect(new_url);
    auto result = navigation_simulator_->GetLastThrottleCheckResult();
    if (result == content::NavigationThrottle::CANCEL)
      navigation_simulator_.reset();
    return result;
  }

  content::NavigationThrottle::ThrottleCheckResult SimulateCommit() {
    // Need to post a task to flush the IO thread because calling Commit()
    // blocks until the throttle checks are complete.
    // TODO(csharrison): Consider adding finer grained control to the
    // NavigationSimulator by giving it an option to be driven by a
    // TestMockTimeTaskRunner. Also see https://crbug.com/703346.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&base::TestMockTimeTaskRunner::RunUntilIdle,
                              base::Unretained(test_io_task_runner_.get())));
    navigation_simulator_->Commit();
    auto result = navigation_simulator_->GetLastThrottleCheckResult();
    if (result == content::NavigationThrottle::CANCEL)
      navigation_simulator_.reset();
    return result;
  }

  void SimulateStartAndExpectProceed() {
    EXPECT_EQ(content::NavigationThrottle::PROCEED, SimulateStart());
  }

  void SimulateRedirectAndExpectProceed(const GURL& new_url) {
    EXPECT_EQ(content::NavigationThrottle::PROCEED, SimulateRedirect(new_url));
  }

  void SimulateCommitAndExpectProceed() {
    EXPECT_EQ(content::NavigationThrottle::PROCEED, SimulateCommit());
  }

  void CreateTestNavigationForMainFrame(const GURL& first_url) {
    navigation_simulator_ =
        content::NavigationSimulator::CreateRendererInitiated(first_url,
                                                              main_rfh());
  }

  void ConfigureForMatch(const GURL& url,
                         safe_browsing::SBThreatType pattern_type =
                             safe_browsing::SB_THREAT_TYPE_SUBRESOURCE_FILTER,
                         safe_browsing::ThreatPatternType metadata =
                             safe_browsing::ThreatPatternType::NONE) {
    fake_safe_browsing_database_->AddBlacklistedUrl(url, pattern_type,
                                                    metadata);
  }

  void SimulateTimeout() { fake_safe_browsing_database_->SimulateTimeout(); }

  void ClearAllBlacklistedUrls() {
    fake_safe_browsing_database_->RemoveAllBlacklistedUrls();
  }

  // With a null database the throttle becomes pass-through.
  void UsePassThroughThrottle() { fake_safe_browsing_database_ = nullptr; }

  void RunUntilIdle() {
    test_io_task_runner_->RunUntilIdle();
    base::RunLoop().RunUntilIdle();
  }

  const base::HistogramTester& tester() const { return tester_; }

  base::TestMockTimeTaskRunner* test_io_task_runner() const {
    return test_io_task_runner_.get();
  }

  testing::ScopedSubresourceFilterConfigurator* scoped_configuration() {
    return &scoped_configuration_;
  }

 private:
  base::FieldTrialList field_trial_list_;
  testing::ScopedSubresourceFilterConfigurator scoped_configuration_;
  scoped_refptr<base::TestMockTimeTaskRunner> test_io_task_runner_;
  std::unique_ptr<content::NavigationSimulator> navigation_simulator_;
  std::unique_ptr<SubresourceFilterClient> client_;
  scoped_refptr<FakeSafeBrowsingDatabaseManager> fake_safe_browsing_database_;
  base::HistogramTester tester_;

  DISALLOW_COPY_AND_ASSIGN(SubresourceFilterSafeBrowsingActivationThrottleTest);
};

class SubresourceFilterSafeBrowsingActivationThrottleParamTest
    : public SubresourceFilterSafeBrowsingActivationThrottleTest,
      public ::testing::WithParamInterface<ActivationListTestData> {
 public:
  SubresourceFilterSafeBrowsingActivationThrottleParamTest() {}
  ~SubresourceFilterSafeBrowsingActivationThrottleParamTest() override {}

  void Configure() override {
    const ActivationListTestData& test_data = GetParam();
    scoped_configuration()->ResetConfiguration(Configuration(
        ActivationLevel::ENABLED, ActivationScope::ACTIVATION_LIST,
        test_data.activation_list_type));
  }

  void ConfigureForMatchParam(const GURL& url) {
    const ActivationListTestData& test_data = GetParam();
    ConfigureForMatch(url, test_data.threat_type,
                      test_data.threat_type_metadata);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(
      SubresourceFilterSafeBrowsingActivationThrottleParamTest);
};

class SubresourceFilterSafeBrowsingActivationThrottleTestWithCancelling
    : public SubresourceFilterSafeBrowsingActivationThrottleTest,
      public ::testing::WithParamInterface<
          std::tuple<content::CancellingNavigationThrottle::CancelTime,
                     content::CancellingNavigationThrottle::ResultSynchrony>> {
 public:
  SubresourceFilterSafeBrowsingActivationThrottleTestWithCancelling() {
    std::tie(cancel_time_, result_sync_) = GetParam();
  }
  ~SubresourceFilterSafeBrowsingActivationThrottleTestWithCancelling()
      override {}

  void DidStartNavigation(content::NavigationHandle* handle) override {
    handle->RegisterThrottleForTesting(
        base::MakeUnique<content::CancellingNavigationThrottle>(
            handle, cancel_time_, result_sync_));
    SubresourceFilterSafeBrowsingActivationThrottleTest::DidStartNavigation(
        handle);
  }

  content::CancellingNavigationThrottle::CancelTime cancel_time() {
    return cancel_time_;
  }

  content::CancellingNavigationThrottle::ResultSynchrony result_sync() {
    return result_sync_;
  }

 private:
  content::CancellingNavigationThrottle::CancelTime cancel_time_;
  content::CancellingNavigationThrottle::ResultSynchrony result_sync_;

  DISALLOW_COPY_AND_ASSIGN(
      SubresourceFilterSafeBrowsingActivationThrottleTestWithCancelling);
};

TEST_F(SubresourceFilterSafeBrowsingActivationThrottleTest,
       PassThroughThrottle) {
  UsePassThroughThrottle();
  CreateTestNavigationForMainFrame(GURL(kURL));
  SimulateStartAndExpectProceed();
  SimulateRedirectAndExpectProceed(GURL(kRedirectURL));
  SimulateCommitAndExpectProceed();
  EXPECT_EQ(ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
            factory()->GetActivationDecisionForLastCommittedPageLoad());

  scoped_configuration()->ResetConfiguration(
      Configuration(ActivationLevel::ENABLED, ActivationScope::ALL_SITES));
  CreateTestNavigationForMainFrame(GURL(kURL));
  SimulateStartAndExpectProceed();
  SimulateRedirectAndExpectProceed(GURL(kRedirectURL));
  SimulateCommitAndExpectProceed();
  EXPECT_EQ(ActivationDecision::ACTIVATED,
            factory()->GetActivationDecisionForLastCommittedPageLoad());

  scoped_configuration()->ResetConfiguration(
      Configuration(ActivationLevel::ENABLED, ActivationScope::NO_SITES));
  CreateTestNavigationForMainFrame(GURL(kURL));
  SimulateStartAndExpectProceed();
  SimulateRedirectAndExpectProceed(GURL(kRedirectURL));
  SimulateCommitAndExpectProceed();
  EXPECT_EQ(ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
            factory()->GetActivationDecisionForLastCommittedPageLoad());
}

TEST_P(SubresourceFilterSafeBrowsingActivationThrottleParamTest,
       ListNotMatched_NoActivation) {
  const ActivationListTestData& test_data = GetParam();
  const GURL url(kURL);
  const std::string suffix(GetSuffixForList(test_data.activation_list_type));
  CreateTestNavigationForMainFrame(url);
  SimulateStartAndExpectProceed();
  SimulateCommitAndExpectProceed();
  EXPECT_EQ(ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
            factory()->GetActivationDecisionForLastCommittedPageLoad());
  ExpectSampleForSuffix("SocialEngineeringAdsInterstitial", std::string(),
                        tester());
  ExpectSampleForSuffix("PhishingInterstitial", std::string(), tester());
  ExpectSampleForSuffix("SubresourceFilterOnly", std::string(), tester());

  tester().ExpectTotalCount(kSafeBrowsingNavigationDelay, 1);
  tester().ExpectTotalCount(kSafeBrowsingNavigationDelayNoSpeculation, 1);
  tester().ExpectTotalCount(kSafeBrowsingCheckTime, 1);
  tester().ExpectTotalCount(kNavigationChainSize + suffix, 0);
}

TEST_P(SubresourceFilterSafeBrowsingActivationThrottleParamTest,
       ListMatched_Activation) {
  const ActivationListTestData& test_data = GetParam();
  const GURL url(kURL);
  ConfigureForMatchParam(url);
  CreateTestNavigationForMainFrame(url);
  SimulateStartAndExpectProceed();
  SimulateCommitAndExpectProceed();
  EXPECT_EQ(ActivationDecision::ACTIVATED,
            factory()->GetActivationDecisionForLastCommittedPageLoad());
  const std::string suffix(GetSuffixForList(test_data.activation_list_type));
  ExpectSampleForSuffix("SocialEngineeringAdsInterstitial", suffix, tester());
  ExpectSampleForSuffix("PhishingInterstitial", suffix, tester());
  ExpectSampleForSuffix("SubresourceFilterOnly", suffix, tester());
  tester().ExpectUniqueSample(kNavigationChainSize + suffix, 1, 1);
}

TEST_P(SubresourceFilterSafeBrowsingActivationThrottleParamTest,
       ListNotMatchedAfterRedirect_NoActivation) {
  const ActivationListTestData& test_data = GetParam();
  const GURL url(kURL);
  const std::string suffix(GetSuffixForList(test_data.activation_list_type));
  CreateTestNavigationForMainFrame(url);
  SimulateStartAndExpectProceed();
  SimulateRedirectAndExpectProceed(GURL(kRedirectURL));
  SimulateCommitAndExpectProceed();
  EXPECT_EQ(ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
            factory()->GetActivationDecisionForLastCommittedPageLoad());
  ExpectSampleForSuffix("SocialEngineeringAdsInterstitial", std::string(),
                        tester());
  ExpectSampleForSuffix("PhishingInterstitial", std::string(), tester());
  ExpectSampleForSuffix("SubresourceFilterOnly", std::string(), tester());
  tester().ExpectTotalCount(kNavigationChainSize + suffix, 0);
}

TEST_P(SubresourceFilterSafeBrowsingActivationThrottleParamTest,
       ListMatchedAfterRedirect_Activation) {
  const ActivationListTestData& test_data = GetParam();
  const GURL url(kURL);
  const std::string suffix(GetSuffixForList(test_data.activation_list_type));
  ConfigureForMatchParam(GURL(kRedirectURL));
  CreateTestNavigationForMainFrame(url);
  SimulateStartAndExpectProceed();
  SimulateRedirectAndExpectProceed(GURL(kRedirectURL));
  SimulateCommitAndExpectProceed();
  EXPECT_EQ(ActivationDecision::ACTIVATED,
            factory()->GetActivationDecisionForLastCommittedPageLoad());
  tester().ExpectUniqueSample(kNavigationChainSize + suffix, 2, 1);
  ExpectSampleForSuffix("SocialEngineeringAdsInterstitial", suffix, tester());
  ExpectSampleForSuffix("PhishingInterstitial", suffix, tester());
  ExpectSampleForSuffix("SubresourceFilterOnly", suffix, tester());
}

TEST_P(SubresourceFilterSafeBrowsingActivationThrottleParamTest,
       ListNotMatchedAndTimeout_NoActivation) {
  const ActivationListTestData& test_data = GetParam();
  const GURL url(kURL);
  const std::string suffix(GetSuffixForList(test_data.activation_list_type));
  SimulateTimeout();
  CreateTestNavigationForMainFrame(url);
  SimulateStartAndExpectProceed();

  // Flush the pending tasks on the IO thread, so the delayed task surely gets
  // posted.
  test_io_task_runner()->RunUntilIdle();

  // Expect one delayed task, and fast forward time.
  base::TimeDelta expected_delay =
      SubresourceFilterSafeBrowsingClientRequest::kCheckURLTimeout;
  EXPECT_EQ(expected_delay, test_io_task_runner()->NextPendingTaskDelay());
  test_io_task_runner()->FastForwardBy(expected_delay);
  SimulateCommitAndExpectProceed();
  EXPECT_EQ(ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
            factory()->GetActivationDecisionForLastCommittedPageLoad());
  tester().ExpectTotalCount(kMatchesPatternHistogramNameSubresourceFilterSuffix,
                            0);
  tester().ExpectTotalCount(kNavigationChainSizeSubresourceFilterSuffix, 0);
  tester().ExpectTotalCount(kSafeBrowsingNavigationDelay, 1);
  tester().ExpectTotalCount(kSafeBrowsingNavigationDelayNoSpeculation, 1);
  tester().ExpectTotalCount(kSafeBrowsingCheckTime, 1);
}

TEST_P(SubresourceFilterSafeBrowsingActivationThrottleParamTest,
       ListMatchedOnStart_NoDelay) {
  const ActivationListTestData& test_data = GetParam();
  const GURL url(kURL);
  ConfigureForMatchParam(url);
  CreateTestNavigationForMainFrame(url);
  SimulateStartAndExpectProceed();

  // Get the database result back before commit.
  RunUntilIdle();

  SimulateCommitAndExpectProceed();
  EXPECT_EQ(ActivationDecision::ACTIVATED,
            factory()->GetActivationDecisionForLastCommittedPageLoad());
  const std::string suffix(GetSuffixForList(test_data.activation_list_type));
  ExpectSampleForSuffix("SocialEngineeringAdsInterstitial", suffix, tester());
  ExpectSampleForSuffix("PhishingInterstitial", suffix, tester());
  ExpectSampleForSuffix("SubresourceFilterOnly", suffix, tester());
  tester().ExpectUniqueSample(kNavigationChainSize + suffix, 1, 1);

  tester().ExpectTimeBucketCount(kSafeBrowsingNavigationDelay,
                                 base::TimeDelta::FromMilliseconds(0), 1);
  tester().ExpectTotalCount(kSafeBrowsingNavigationDelayNoSpeculation, 1);
}

TEST_P(SubresourceFilterSafeBrowsingActivationThrottleParamTest,
       ListMatchedOnRedirect_NoDelay) {
  const ActivationListTestData& test_data = GetParam();
  const GURL url(kURL);
  const GURL redirect_url(kRedirectURL);
  ConfigureForMatchParam(redirect_url);
  CreateTestNavigationForMainFrame(url);

  SimulateStartAndExpectProceed();
  SimulateRedirectAndExpectProceed(redirect_url);

  // Get the database result back before commit.
  RunUntilIdle();

  SimulateCommitAndExpectProceed();
  EXPECT_EQ(ActivationDecision::ACTIVATED,
            factory()->GetActivationDecisionForLastCommittedPageLoad());
  const std::string suffix(GetSuffixForList(test_data.activation_list_type));
  ExpectSampleForSuffix("SocialEngineeringAdsInterstitial", suffix, tester());
  ExpectSampleForSuffix("PhishingInterstitial", suffix, tester());
  ExpectSampleForSuffix("SubresourceFilterOnly", suffix, tester());
  tester().ExpectUniqueSample(kNavigationChainSize + suffix, 2, 1);

  tester().ExpectTimeBucketCount(kSafeBrowsingNavigationDelay,
                                 base::TimeDelta::FromMilliseconds(0), 1);
  tester().ExpectTotalCount(kSafeBrowsingNavigationDelayNoSpeculation, 1);
  tester().ExpectTotalCount(kSafeBrowsingCheckTime, 2);
}

TEST_P(SubresourceFilterSafeBrowsingActivationThrottleParamTest,
       ListMatchedOnStartWithRedirect_NoActivation) {
  const GURL url(kURL);
  const GURL redirect_url(kRedirectURL);
  ConfigureForMatchParam(url);
  CreateTestNavigationForMainFrame(url);

  // These two lines also test how the database client reacts to two requests
  // happening one after another.
  SimulateStartAndExpectProceed();
  SimulateRedirectAndExpectProceed(redirect_url);

  // Get the database result back before commit.
  RunUntilIdle();

  SimulateCommitAndExpectProceed();
  EXPECT_EQ(ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
            factory()->GetActivationDecisionForLastCommittedPageLoad());
  tester().ExpectTotalCount(kMatchesPatternHistogramNameSubresourceFilterSuffix,
                            0);
  tester().ExpectTotalCount(kNavigationChainSizeSubresourceFilterSuffix, 0);
  tester().ExpectTimeBucketCount(kSafeBrowsingNavigationDelay,
                                 base::TimeDelta::FromMilliseconds(0), 1);
  tester().ExpectTotalCount(kSafeBrowsingNavigationDelayNoSpeculation, 1);
}

TEST_P(SubresourceFilterSafeBrowsingActivationThrottleParamTest,
       RedirectPatternTest) {
  struct RedirectRedirectChainMatchPatternTestData {
    std::vector<bool> blacklisted_urls;
    std::vector<GURL> navigation_chain;
  } kRedirectRecordedHistogramsTestData[] = {
      {{false}, {GURL(kUrlA)}},
      {{true}, {GURL(kUrlA)}},
      {{false, false}, {GURL(kUrlA), GURL(kUrlB)}},
      {{false, true}, {GURL(kUrlA), GURL(kUrlB)}},
      {{true, false}, {GURL(kUrlA), GURL(kUrlB)}},
      {{true, true}, {GURL(kUrlA), GURL(kUrlB)}},
      {{false, false, false}, {GURL(kUrlA), GURL(kUrlB), GURL(kUrlC)}},
      {{false, false, true}, {GURL(kUrlA), GURL(kUrlB), GURL(kUrlC)}},
      {{false, true, false}, {GURL(kUrlA), GURL(kUrlB), GURL(kUrlC)}},
      {{false, true, true}, {GURL(kUrlA), GURL(kUrlB), GURL(kUrlC)}},
      {{true, false, false}, {GURL(kUrlA), GURL(kUrlB), GURL(kUrlC)}},
      {{true, false, true}, {GURL(kUrlA), GURL(kUrlB), GURL(kUrlC)}},
      {{true, true, false}, {GURL(kUrlA), GURL(kUrlB), GURL(kUrlC)}},
      {{true, true, true}, {GURL(kUrlA), GURL(kUrlB), GURL(kUrlC)}},
      {{false, true, false, false},
       {GURL(kUrlA), GURL(kUrlB), GURL(kUrlC), GURL(kUrlD)}},
  };

  for (const auto& test_data : kRedirectRecordedHistogramsTestData) {
    base::HistogramTester histogram_tester;
    ClearAllBlacklistedUrls();
    auto it = test_data.navigation_chain.begin();
    for (size_t i = 0u; i < test_data.blacklisted_urls.size(); ++i) {
      if (test_data.blacklisted_urls[i])
        ConfigureForMatchParam(test_data.navigation_chain[i]);
    }
    CreateTestNavigationForMainFrame(*it);
    SimulateStartAndExpectProceed();
    for (++it; it != test_data.navigation_chain.end(); ++it)
      SimulateRedirectAndExpectProceed(*it);
    SimulateCommitAndExpectProceed();

    // Verify histograms
    const std::string suffix_param(
        GetSuffixForList(GetParam().activation_list_type));
    auto check_histogram = [&](std::string suffix) {
      bool matches =
          suffix == suffix_param && test_data.blacklisted_urls.back();
      histogram_tester.ExpectBucketCount(kMatchesPatternHistogramName + suffix,
                                         matches, 1);

      if (matches) {
        histogram_tester.ExpectBucketCount(kNavigationChainSize + suffix,
                                           test_data.navigation_chain.size(),
                                           1);
      } else {
        histogram_tester.ExpectTotalCount(kNavigationChainSize + suffix, 0);
      }
    };

    check_histogram("SocialEngineeringAdsInterstitial");
    check_histogram("PhishingInterstitial");
    check_histogram("SubresourceFilterOnly");
  }
}

TEST_P(SubresourceFilterSafeBrowsingActivationThrottleTestWithCancelling,
       Cancel) {
  const GURL url(kURL);
  SCOPED_TRACE(::testing::Message() << "CancelTime: " << cancel_time()
                                    << " ResultSynchrony: " << result_sync());
  ConfigureForMatch(url);
  CreateTestNavigationForMainFrame(url);

  content::NavigationThrottle::ThrottleCheckResult result = SimulateStart();
  if (cancel_time() ==
      content::CancellingNavigationThrottle::WILL_START_REQUEST) {
    EXPECT_EQ(content::NavigationThrottle::CANCEL, result);
    tester().ExpectTotalCount(kSafeBrowsingNavigationDelay, 0);
    return;
  }
  EXPECT_EQ(content::NavigationThrottle::PROCEED, result);

  result = SimulateRedirect(GURL(kRedirectURL));
  if (cancel_time() ==
      content::CancellingNavigationThrottle::WILL_REDIRECT_REQUEST) {
    EXPECT_EQ(content::NavigationThrottle::CANCEL, result);
    tester().ExpectTotalCount(kSafeBrowsingNavigationDelay, 0);
    return;
  }
  EXPECT_EQ(content::NavigationThrottle::PROCEED, result);

  base::RunLoop().RunUntilIdle();

  result = SimulateCommit();
  EXPECT_EQ(content::NavigationThrottle::CANCEL, result);
  tester().ExpectTotalCount(kSafeBrowsingNavigationDelay, 0);
}

INSTANTIATE_TEST_CASE_P(
    CancelMethod,
    SubresourceFilterSafeBrowsingActivationThrottleTestWithCancelling,
    ::testing::Combine(
        ::testing::Values(
            content::CancellingNavigationThrottle::WILL_START_REQUEST,
            content::CancellingNavigationThrottle::WILL_REDIRECT_REQUEST,
            content::CancellingNavigationThrottle::WILL_PROCESS_RESPONSE),
        ::testing::Values(
            content::CancellingNavigationThrottle::SYNCHRONOUS,
            content::CancellingNavigationThrottle::ASYNCHRONOUS)));

INSTANTIATE_TEST_CASE_P(
    ActivationLevelTest,
    SubresourceFilterSafeBrowsingActivationThrottleParamTest,
    ::testing::ValuesIn(kActivationListTestData));

}  // namespace subresource_filter
