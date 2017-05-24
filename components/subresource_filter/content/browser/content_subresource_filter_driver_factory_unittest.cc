// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/subresource_filter/content/browser/content_subresource_filter_driver_factory.h"

#include <set>
#include <string>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/field_trial.h"
#include "base/run_loop.h"
#include "base/test/histogram_tester.h"
#include "components/safe_browsing_db/util.h"
#include "components/subresource_filter/content/browser/content_activation_list_utils.h"
#include "components/subresource_filter/content/browser/subresource_filter_client.h"
#include "components/subresource_filter/content/common/subresource_filter_messages.h"
#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "components/subresource_filter/core/browser/subresource_filter_features_test_support.h"
#include "components/subresource_filter/core/common/activation_decision.h"
#include "components/subresource_filter/core/common/activation_list.h"
#include "components/subresource_filter/core/common/test_ruleset_creator.h"
#include "components/subresource_filter/core/common/test_ruleset_utils.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "net/base/net_errors.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace subresource_filter {

namespace {

const char kExampleUrlWithParams[] = "https://example.com/soceng?q=engsoc";
const char kExampleUrl[] = "https://example.com";
const char kExampleLoginUrl[] = "https://example.com/login";
const char kUrlA[] = "https://example_a.com";
const char kUrlB[] = "https://example_b.com";
const char kUrlC[] = "https://example_c.com";
const char kSubframeName[] = "Child";
const char kDisallowedUrl[] = "https://example.com/disallowed.html";

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
  ActivationDecision expected_activation_decision;
  ActivationList activation_list;
  safe_browsing::SBThreatType threat_type;
  safe_browsing::ThreatPatternType threat_type_metadata;
};

const ActivationListTestData kActivationListTestData[] = {
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET, ActivationList::NONE,
     safe_browsing::SB_THREAT_TYPE_URL_PHISHING,
     safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_URL_PHISHING,
     safe_browsing::ThreatPatternType::NONE},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_URL_PHISHING,
     safe_browsing::ThreatPatternType::MALWARE_LANDING},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_URL_PHISHING,
     safe_browsing::ThreatPatternType::MALWARE_DISTRIBUTION},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     ActivationList::PHISHING_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_API_ABUSE,
     safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     ActivationList::PHISHING_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_BLACKLISTED_RESOURCE,
     safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     ActivationList::PHISHING_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_CLIENT_SIDE_MALWARE_URL,
     safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     ActivationList::PHISHING_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_BINARY_MALWARE_URL,
     safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     ActivationList::PHISHING_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_URL_UNWANTED,
     safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     ActivationList::PHISHING_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_URL_MALWARE,
     safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     ActivationList::PHISHING_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_CLIENT_SIDE_PHISHING_URL,
     safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     ActivationList::PHISHING_INTERSTITIAL, safe_browsing::SB_THREAT_TYPE_SAFE,
     safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS},
    {ActivationDecision::ACTIVATED, ActivationList::PHISHING_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_URL_PHISHING,
     safe_browsing::ThreatPatternType::NONE},
    {ActivationDecision::ACTIVATED, ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_URL_PHISHING,
     safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS},
    {ActivationDecision::ACTIVATED, ActivationList::PHISHING_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_URL_PHISHING,
     safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS},
    {ActivationDecision::ACTIVATED, ActivationList::SUBRESOURCE_FILTER,
     safe_browsing::SB_THREAT_TYPE_SUBRESOURCE_FILTER,
     safe_browsing::ThreatPatternType::NONE},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     ActivationList::PHISHING_INTERSTITIAL,
     safe_browsing::SB_THREAT_TYPE_SUBRESOURCE_FILTER,
     safe_browsing::ThreatPatternType::NONE},
};

struct ActivationScopeTestData {
  ActivationDecision expected_activation_decision;
  bool url_matches_activation_list;
  ActivationScope activation_scope;
};

const ActivationScopeTestData kActivationScopeTestData[] = {
    {ActivationDecision::ACTIVATED, false /* url_matches_activation_list */,
     ActivationScope::ALL_SITES},
    {ActivationDecision::ACTIVATED, true /* url_matches_activation_list */,
     ActivationScope::ALL_SITES},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     true /* url_matches_activation_list */, ActivationScope::NO_SITES},
    {ActivationDecision::ACTIVATED, true /* url_matches_activation_list */,
     ActivationScope::ACTIVATION_LIST},
    {ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET,
     false /* url_matches_activation_list */, ActivationScope::ACTIVATION_LIST},
};

struct ActivationLevelTestData {
  ActivationDecision expected_activation_decision;
  ActivationLevel activation_level;
};

const ActivationLevelTestData kActivationLevelTestData[] = {
    {ActivationDecision::ACTIVATED, ActivationLevel::DRYRUN},
    {ActivationDecision::ACTIVATED, ActivationLevel::ENABLED},
    {ActivationDecision::ACTIVATION_DISABLED, ActivationLevel::DISABLED},
};

class MockSubresourceFilterClient : public SubresourceFilterClient {
 public:
  MockSubresourceFilterClient(VerifiedRulesetDealer::Handle* ruleset_dealer)
      : ruleset_dealer_(ruleset_dealer) {}

  ~MockSubresourceFilterClient() override = default;

  bool OnPageActivationComputed(content::NavigationHandle* handle,
                                bool activated) override {
    DCHECK(handle->IsInMainFrame());
    return whitelisted_hosts_.count(handle->GetURL().host());
  }

  void WhitelistInCurrentWebContents(const GURL& url) override {
    ASSERT_TRUE(url.SchemeIsHTTPOrHTTPS());
    whitelisted_hosts_.insert(url.host());
  }

  VerifiedRulesetDealer::Handle* GetRulesetDealer() override {
    return ruleset_dealer_;
  }

  MOCK_METHOD1(ToggleNotificationVisibility, void(bool));

 private:
  std::set<std::string> whitelisted_hosts_;

  // Owned by the test harness.
  VerifiedRulesetDealer::Handle* ruleset_dealer_;

  DISALLOW_COPY_AND_ASSIGN(MockSubresourceFilterClient);
};

}  // namespace

class ContentSubresourceFilterDriverFactoryTest
    : public content::RenderViewHostTestHarness,
      public content::WebContentsObserver {
 public:
  ContentSubresourceFilterDriverFactoryTest() {}
  ~ContentSubresourceFilterDriverFactoryTest() override {}

  // content::RenderViewHostImplTestHarness:
  void SetUp() override {
    RenderViewHostTestHarness::SetUp();

    std::vector<proto::UrlRule> rules;
    rules.push_back(testing::CreateSuffixRule("disallowed.html"));
    ASSERT_NO_FATAL_FAILURE(test_ruleset_creator_.CreateRulesetWithRules(
        rules, &test_ruleset_pair_));
    ruleset_dealer_ = base::MakeUnique<VerifiedRulesetDealer::Handle>(
        base::MessageLoop::current()->task_runner());
    ruleset_dealer_->SetRulesetFile(
        testing::TestRuleset::Open(test_ruleset_pair_.indexed));
    client_ =
        base::MakeUnique<MockSubresourceFilterClient>(ruleset_dealer_.get());
    ContentSubresourceFilterDriverFactory::CreateForWebContents(
        RenderViewHostTestHarness::web_contents(), client());
    ResetConfigurationToEnableFilteringOnPhishingSites();

    // Add a subframe.
    content::RenderFrameHostTester* rfh_tester =
        content::RenderFrameHostTester::For(main_rfh());
    rfh_tester->InitializeRenderFrameIfNeeded();
    rfh_tester->AppendChild(kSubframeName);

    Observe(content::RenderViewHostTestHarness::web_contents());
  }

  void TearDown() override {
    ruleset_dealer_.reset();
    base::RunLoop().RunUntilIdle();
    RenderViewHostTestHarness::TearDown();
  }

  void ResetConfiguration(Configuration config) {
    scoped_configuration_.ResetConfiguration(std::move(config));
  }

  void ResetConfiguration(std::vector<Configuration> configs) {
    scoped_configuration_.ResetConfiguration(std::move(configs));
  }

  void ResetConfigurationToEnableFilteringOnAllSites() {
    ResetConfiguration(
        Configuration(ActivationLevel::ENABLED, ActivationScope::ALL_SITES));
  }

  void ResetConfigurationToEnableFilteringOnPhishingSites() {
    ResetConfiguration(Configuration::MakePresetForLiveRunOnPhishingSites());
  }

  ContentSubresourceFilterDriverFactory* factory() {
    return ContentSubresourceFilterDriverFactory::FromWebContents(
        RenderViewHostTestHarness::web_contents());
  }

  MockSubresourceFilterClient* client() { return client_.get(); }

  content::RenderFrameHost* GetSubframeRFH() {
    for (content::RenderFrameHost* rfh :
         RenderViewHostTestHarness::web_contents()->GetAllFrames()) {
      if (rfh->GetFrameName() == kSubframeName)
        return rfh;
    }
    return nullptr;
  }

  void ExpectActivationSignalForFrame(content::RenderFrameHost* rfh,
                                      bool expect_activation) {
    content::MockRenderProcessHost* render_process_host =
        static_cast<content::MockRenderProcessHost*>(rfh->GetProcess());
    const IPC::Message* message =
        render_process_host->sink().GetFirstMessageMatching(
            SubresourceFilterMsg_ActivateForNextCommittedLoad::ID);
    ASSERT_EQ(expect_activation, !!message);
    if (expect_activation) {
      std::tuple<ActivationState> args;
      SubresourceFilterMsg_ActivateForNextCommittedLoad::Read(message, &args);
      ActivationLevel level = std::get<0>(args).activation_level;
      EXPECT_NE(ActivationLevel::DISABLED, level);
    }
    render_process_host->sink().ClearMessages();
  }

  void BlacklistURLWithRedirectsNavigateAndCommit(
      const std::vector<bool>& blacklisted_urls,
      const std::vector<GURL>& navigation_chain,
      safe_browsing::SBThreatType threat_type,
      safe_browsing::ThreatPatternType threat_type_metadata,
      const content::Referrer& referrer,
      ui::PageTransition transition,
      ActivationDecision expected_activation_decision) {
    const bool expected_activation =
        expected_activation_decision == ActivationDecision::ACTIVATED;
    base::HistogramTester tester;
    EXPECT_CALL(*client(), ToggleNotificationVisibility(false)).Times(1);

    std::unique_ptr<content::NavigationSimulator> navigation_simulator =
        content::NavigationSimulator::CreateRendererInitiated(
            navigation_chain.front(), main_rfh());
    navigation_simulator->SetReferrer(referrer);
    navigation_simulator->SetTransition(transition);
    navigation_simulator->Start();

    ::testing::Mock::VerifyAndClearExpectations(client());
    for (size_t i = 1; i < navigation_chain.size(); ++i)
      navigation_simulator->Redirect(navigation_chain[i]);

    if (blacklisted_urls.size() != navigation_chain.size() ||
        !blacklisted_urls.back()) {
      threat_type = safe_browsing::SBThreatType::SB_THREAT_TYPE_SAFE;
      threat_type_metadata = safe_browsing::ThreatPatternType::NONE;
    }
    factory()->OnSafeBrowsingMatchComputed(
        navigation_simulator->GetNavigationHandle(), threat_type,
        threat_type_metadata);
    std::string suffix;
    ActivationList activation_list =
        GetListForThreatTypeAndMetadata(threat_type, threat_type_metadata);
    suffix = blacklisted_urls.back() ? GetSuffixForList(activation_list)
                                     : std::string();

    navigation_simulator->Commit();
    ExpectActivationSignalForFrame(main_rfh(), expected_activation);
    EXPECT_EQ(expected_activation_decision,
              factory()->GetActivationDecisionForLastCommittedPageLoad());

    // Re-create a subframe now that the frame has navigated.
    content::RenderFrameHostTester* rfh_tester =
        content::RenderFrameHostTester::For(main_rfh());
    rfh_tester->AppendChild(kSubframeName);
  }

  void NavigateSubframeAndExpectCheckResult(const GURL& url,
                                            bool expect_cancelled) {
    std::unique_ptr<content::NavigationSimulator> simulator =
        content::NavigationSimulator::CreateRendererInitiated(url,
                                                              GetSubframeRFH());
    simulator->Start();
    content::NavigationThrottle::ThrottleCheckResult result =
        simulator->GetLastThrottleCheckResult();
    if (expect_cancelled) {
      EXPECT_EQ(content::NavigationThrottle::CANCEL, result);
    } else {
      EXPECT_EQ(content::NavigationThrottle::PROCEED, result);
      simulator->Commit();
    }
  }

  void NavigateAndCommitSubframe(const GURL& url, bool expected_activation) {
    EXPECT_CALL(*client(), ToggleNotificationVisibility(::testing::_)).Times(0);

    content::NavigationSimulator::NavigateAndCommitFromDocument(
        url, GetSubframeRFH());
    ExpectActivationSignalForFrame(GetSubframeRFH(), expected_activation);
    ::testing::Mock::VerifyAndClearExpectations(client());
  }

  void NavigateAndExpectActivation(
      const std::vector<bool>& blacklisted_urls,
      const std::vector<GURL>& navigation_chain,
      safe_browsing::SBThreatType threat_type,
      safe_browsing::ThreatPatternType threat_type_metadata,
      const content::Referrer& referrer,
      ui::PageTransition transition,
      ActivationDecision expected_activation_decision) {
    const bool expected_activation =
        expected_activation_decision == ActivationDecision::ACTIVATED;
    BlacklistURLWithRedirectsNavigateAndCommit(
        blacklisted_urls, navigation_chain, threat_type, threat_type_metadata,
        referrer, transition, expected_activation_decision);

    NavigateAndCommitSubframe(GURL(kExampleLoginUrl), expected_activation);
  }

  void NavigateAndExpectActivation(
      const std::vector<bool>& blacklisted_urls,
      const std::vector<GURL>& navigation_chain,
      ActivationDecision expected_activation_decision) {
    NavigateAndExpectActivation(
        blacklisted_urls, navigation_chain,
        safe_browsing::SB_THREAT_TYPE_URL_PHISHING,
        safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS,
        content::Referrer(), ui::PAGE_TRANSITION_LINK,
        expected_activation_decision);
  }

  void EmulateFailedNavigationAndExpectNoActivation(const GURL& url) {
    EXPECT_CALL(*client(), ToggleNotificationVisibility(false)).Times(1);

    // With browser-side navigation enabled, ReadyToCommitNavigation is invoked
    // even for failed navigations. This is correctly simulated by
    // NavigationSimulator. Make sure no activation message is sent in this
    // case.
    content::NavigationSimulator::NavigateAndFailFromDocument(
        url, net::ERR_TIMED_OUT, main_rfh());
    ExpectActivationSignalForFrame(main_rfh(), false);
    ::testing::Mock::VerifyAndClearExpectations(client());
  }

  void EmulateInPageNavigation(
      const std::vector<bool>& blacklisted_urls,
      ActivationDecision expected_activation_decision) {
    // This test examines the navigation with the following sequence of events:
    //   DidStartProvisional(main, "example.com")
    //   ReadyToCommitNavigation(“example.com”)
    //   DidCommitProvisional(main, "example.com")
    //   DidStartProvisional(sub, "example.com/login")
    //   DidCommitProvisional(sub, "example.com/login")
    //   DidCommitProvisional(main, "example.com#ref")

    NavigateAndExpectActivation(blacklisted_urls, {GURL(kExampleUrl)},
                                expected_activation_decision);
    EXPECT_CALL(*client(), ToggleNotificationVisibility(::testing::_)).Times(0);
    std::unique_ptr<content::NavigationSimulator> navigation_simulator =
        content::NavigationSimulator::CreateRendererInitiated(GURL(kExampleUrl),
                                                              main_rfh());
    navigation_simulator->CommitSameDocument();
    ExpectActivationSignalForFrame(main_rfh(), false);
    ::testing::Mock::VerifyAndClearExpectations(client());
  }

 protected:
  // content::WebContentsObserver
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (navigation_handle->IsSameDocument())
      return;

    std::vector<std::unique_ptr<content::NavigationThrottle>> throttles;
    factory()->throttle_manager()->MaybeAppendNavigationThrottles(
        navigation_handle, &throttles);
    for (auto& it : throttles)
      navigation_handle->RegisterThrottleForTesting(std::move(it));
  }

 private:
  testing::ScopedSubresourceFilterConfigurator scoped_configuration_;
  testing::TestRulesetCreator test_ruleset_creator_;
  testing::TestRulesetPair test_ruleset_pair_;

  std::unique_ptr<MockSubresourceFilterClient> client_;
  std::unique_ptr<VerifiedRulesetDealer::Handle> ruleset_dealer_;

  DISALLOW_COPY_AND_ASSIGN(ContentSubresourceFilterDriverFactoryTest);
};

class ContentSubresourceFilterDriverFactoryThreatTypeTest
    : public ContentSubresourceFilterDriverFactoryTest,
      public ::testing::WithParamInterface<ActivationListTestData> {
 public:
  ContentSubresourceFilterDriverFactoryThreatTypeTest() {}
  ~ContentSubresourceFilterDriverFactoryThreatTypeTest() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ContentSubresourceFilterDriverFactoryThreatTypeTest);
};

class ContentSubresourceFilterDriverFactoryActivationScopeTest
    : public ContentSubresourceFilterDriverFactoryTest,
      public ::testing::WithParamInterface<ActivationScopeTestData> {
 public:
  ContentSubresourceFilterDriverFactoryActivationScopeTest() {}
  ~ContentSubresourceFilterDriverFactoryActivationScopeTest() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(
      ContentSubresourceFilterDriverFactoryActivationScopeTest);
};

class ContentSubresourceFilterDriverFactoryActivationLevelTest
    : public ContentSubresourceFilterDriverFactoryTest,
      public ::testing::WithParamInterface<ActivationLevelTestData> {
 public:
  ContentSubresourceFilterDriverFactoryActivationLevelTest() {}
  ~ContentSubresourceFilterDriverFactoryActivationLevelTest() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(
      ContentSubresourceFilterDriverFactoryActivationLevelTest);
};

TEST_F(ContentSubresourceFilterDriverFactoryTest,
       NoActivationWhenActivationLevelIsDisabled) {
  Configuration config(ActivationLevel::DISABLED,
                       ActivationScope::ACTIVATION_LIST,
                       ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL);
  config.activation_options.activation_level = ActivationLevel::DISABLED;
  ResetConfiguration(std::move(config));

  NavigateAndExpectActivation(
      {false}, {GURL(kExampleLoginUrl)},
      ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET);

  const GURL url(kExampleUrlWithParams);
  NavigateAndExpectActivation({true}, {url},
                              ActivationDecision::ACTIVATION_DISABLED);

  // Whitelisting occurs last, so the decision should still be DISABLED.
  factory()->client()->WhitelistInCurrentWebContents(url);
  NavigateAndExpectActivation({true}, {url},
                              ActivationDecision::ACTIVATION_DISABLED);
}

TEST_F(ContentSubresourceFilterDriverFactoryTest, NoActivationWhenNoMatch) {
  NavigateAndExpectActivation(
      {false}, {GURL(kExampleUrl)},
      ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET);
}

TEST_F(ContentSubresourceFilterDriverFactoryTest,
       SpecialCaseNavigationAllSitesEnabled) {
  // Check that when the experiment is enabled for all site, the activation
  // signal is always sent.
  ResetConfigurationToEnableFilteringOnAllSites();
  EmulateInPageNavigation({false}, ActivationDecision::ACTIVATED);
}

TEST_F(ContentSubresourceFilterDriverFactoryTest,
       SpecialCaseNavigationActivationListEnabled) {
  EmulateInPageNavigation({true}, ActivationDecision::ACTIVATED);
}

TEST_F(ContentSubresourceFilterDriverFactoryTest,
       SpecialCaseNavigationActivationListEnabledWithPerformanceMeasurement) {
  Configuration config(ActivationLevel::ENABLED,
                       ActivationScope::ACTIVATION_LIST,
                       ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL);
  config.activation_options.performance_measurement_rate = 1.0;
  ResetConfiguration(std::move(config));

  EmulateInPageNavigation({true}, ActivationDecision::ACTIVATED);
}

TEST_F(ContentSubresourceFilterDriverFactoryTest, FailedNavigation) {
  const GURL url(kExampleUrl);
  ResetConfigurationToEnableFilteringOnAllSites();
  NavigateAndExpectActivation({false}, {url}, ActivationDecision::ACTIVATED);
  EmulateFailedNavigationAndExpectNoActivation(url);
}

TEST_F(ContentSubresourceFilterDriverFactoryTest, NotificationVisibility) {
  ResetConfigurationToEnableFilteringOnAllSites();
  NavigateAndExpectActivation({false}, {GURL(kExampleUrl)},
                              ActivationDecision::ACTIVATED);
  EXPECT_CALL(*client(), ToggleNotificationVisibility(true)).Times(1);
  NavigateSubframeAndExpectCheckResult(GURL(kDisallowedUrl),
                                       true /* expect_cancelled */);
}

TEST_F(ContentSubresourceFilterDriverFactoryTest,
       SuppressNotificationVisibility) {
  Configuration config(ActivationLevel::ENABLED, ActivationScope::ALL_SITES);
  config.activation_options.should_suppress_notifications = true;
  ResetConfiguration(std::move(config));
  NavigateAndExpectActivation({false}, {GURL(kExampleUrl)},
                              ActivationDecision::ACTIVATED);
  EXPECT_CALL(*client(), ToggleNotificationVisibility(::testing::_)).Times(0);
  NavigateSubframeAndExpectCheckResult(GURL(kDisallowedUrl),
                                       true /* expect_cancelled */);
}

TEST_F(ContentSubresourceFilterDriverFactoryTest,
       InactiveMainFrame_SubframeNotFiltered) {
  GURL url(kExampleUrl);
  Configuration config(ActivationLevel::DISABLED, ActivationScope::ALL_SITES);
  ResetConfiguration(std::move(config));
  NavigateAndExpectActivation({false}, {url},
                              ActivationDecision::ACTIVATION_DISABLED);
  NavigateSubframeAndExpectCheckResult(url, false /* expect_cancelled */);
}

TEST_F(ContentSubresourceFilterDriverFactoryTest, WhitelistSiteOnReload) {
  const struct {
    content::Referrer referrer;
    ui::PageTransition transition;
    ActivationDecision expected_activation_decision;
  } kTestCases[] = {
      {content::Referrer(), ui::PAGE_TRANSITION_LINK,
       ActivationDecision::ACTIVATED},
      {content::Referrer(GURL(kUrlA), blink::kWebReferrerPolicyDefault),
       ui::PAGE_TRANSITION_LINK, ActivationDecision::ACTIVATED},
      {content::Referrer(GURL(kExampleUrl), blink::kWebReferrerPolicyDefault),
       ui::PAGE_TRANSITION_LINK, ActivationDecision::URL_WHITELISTED},
      {content::Referrer(), ui::PAGE_TRANSITION_RELOAD,
       ActivationDecision::URL_WHITELISTED}};

  Configuration config(ActivationLevel::ENABLED, ActivationScope::ALL_SITES);
  config.activation_options.should_whitelist_site_on_reload = true;
  ResetConfiguration(std::move(config));

  for (const auto& test_case : kTestCases) {
    SCOPED_TRACE(::testing::Message("referrer = \"")
                 << test_case.referrer.url << "\""
                 << " transition = \"" << test_case.transition << "\"");

    NavigateAndExpectActivation(
        {false}, {GURL(kExampleUrl)},
        safe_browsing::SB_THREAT_TYPE_URL_PHISHING,
        safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS,
        test_case.referrer, test_case.transition,
        test_case.expected_activation_decision);
    // Verify that if the first URL failed to activate, subsequent same-origin
    // navigations also fail to activate.
    NavigateAndExpectActivation({false}, {GURL(kExampleUrlWithParams)},
                                test_case.expected_activation_decision);
  }
}

TEST_F(ContentSubresourceFilterDriverFactoryTest, NoConfigs) {
  ResetConfiguration(std::vector<Configuration>());
  NavigateAndExpectActivation(
      {true}, {GURL(kExampleUrl)},
      ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET);
}

TEST_F(ContentSubresourceFilterDriverFactoryTest, MultipleSimultaneousConfigs) {
  Configuration config1(ActivationLevel::DRYRUN, ActivationScope::NO_SITES);
  config1.activation_conditions.priority = 2;

  Configuration config2(ActivationLevel::DISABLED,
                        ActivationScope::ACTIVATION_LIST,
                        ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL);
  config2.activation_conditions.priority = 1;

  Configuration config3(ActivationLevel::ENABLED, ActivationScope::ALL_SITES);
  config3.activation_options.should_whitelist_site_on_reload = true;
  config3.activation_conditions.priority = 0;

  ResetConfiguration({config1, config2, config3});

  // Should match |config2| and |config3|, the former with the higher priority.
  NavigateAndExpectActivation({true}, {GURL(kExampleUrl)},
                              ActivationDecision::ACTIVATION_DISABLED);

  // Should match |config3|.
  NavigateAndExpectActivation({false}, {GURL(kExampleUrl)},
                              ActivationDecision::ACTIVATED);

  // Should match |config3|, but a reload, so this should get whitelisted.
  NavigateAndExpectActivation(
      {false}, {GURL(kExampleUrl)}, safe_browsing::SB_THREAT_TYPE_URL_PHISHING,
      safe_browsing::ThreatPatternType::SOCIAL_ENGINEERING_ADS,
      content::Referrer(), ui::PAGE_TRANSITION_RELOAD,
      ActivationDecision::URL_WHITELISTED);
}

TEST_P(ContentSubresourceFilterDriverFactoryActivationLevelTest,
       ActivateForFrameState) {
  const ActivationLevelTestData& test_data = GetParam();
  ResetConfiguration(Configuration(
      test_data.activation_level, ActivationScope::ACTIVATION_LIST,
      ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL));

  const GURL url(kExampleUrlWithParams);
  NavigateAndExpectActivation({true}, {url},
                              test_data.expected_activation_decision);
  factory()->client()->WhitelistInCurrentWebContents(url);

  // Whitelisting is only applied when the page will otherwise activate.
  ActivationDecision decision =
      test_data.activation_level == ActivationLevel::DISABLED
          ? test_data.expected_activation_decision
          : ActivationDecision::URL_WHITELISTED;
  NavigateAndExpectActivation({true}, {url}, decision);
}

TEST_P(ContentSubresourceFilterDriverFactoryThreatTypeTest,
       ActivateForTheListType) {
  // Sets up the experiment in a way that the activation decision depends on the
  // list for which the Safe Browsing hit has happened.
  const ActivationListTestData& test_data = GetParam();
  ResetConfiguration(Configuration(ActivationLevel::ENABLED,
                                   ActivationScope::ACTIVATION_LIST,
                                   test_data.activation_list));

  const GURL test_url("https://example.com/nonsoceng?q=engsocnon");
  std::vector<GURL> navigation_chain;

  NavigateAndExpectActivation(
      {false, false, false, true},
      {GURL(kUrlA), GURL(kUrlB), GURL(kUrlC), test_url}, test_data.threat_type,
      test_data.threat_type_metadata, content::Referrer(),
      ui::PAGE_TRANSITION_LINK, test_data.expected_activation_decision);
};

TEST_P(ContentSubresourceFilterDriverFactoryActivationScopeTest,
       ActivateForScopeType) {
  const ActivationScopeTestData& test_data = GetParam();
  ResetConfiguration(
      Configuration(ActivationLevel::ENABLED, test_data.activation_scope,
                    ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL));

  const GURL test_url(kExampleUrlWithParams);

  NavigateAndExpectActivation({test_data.url_matches_activation_list},
                              {test_url},
                              test_data.expected_activation_decision);
  if (test_data.url_matches_activation_list) {
    factory()->client()->WhitelistInCurrentWebContents(test_url);
    ActivationDecision expected_decision =
        test_data.expected_activation_decision;
    if (expected_decision == ActivationDecision::ACTIVATED)
      expected_decision = ActivationDecision::URL_WHITELISTED;
    NavigateAndExpectActivation({test_data.url_matches_activation_list},
                                {GURL(kExampleUrlWithParams)},
                                expected_decision);
  }
};

// Only main frames with http/https schemes should activate.
TEST_P(ContentSubresourceFilterDriverFactoryActivationScopeTest,
       ActivateForSupportedUrlScheme) {
  const ActivationScopeTestData& test_data = GetParam();
  ResetConfiguration(
      Configuration(ActivationLevel::ENABLED, test_data.activation_scope,
                    ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL));

  // data URLs are also not supported, but not listed here, as it's not possible
  // for a page to redirect to them after https://crbug.com/594215 is fixed.
  const char* unsupported_urls[] = {"ftp://example.com/", "chrome://settings",
                                    "chrome-extension://some-extension",
                                    "file:///var/www/index.html"};
  const char* supported_urls[] = {"http://example.test",
                                  "https://example.test"};
  for (auto* url : unsupported_urls) {
    SCOPED_TRACE(url);
    ActivationDecision expected_decision =
        ActivationDecision::UNSUPPORTED_SCHEME;
    // We only log UNSUPPORTED_SCHEME if the navigation would have otherwise
    // activated. Note that non http/s URLs will never match an activation list.
    if (test_data.expected_activation_decision ==
            ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET ||
        test_data.activation_scope == ActivationScope::ACTIVATION_LIST) {
      expected_decision = ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET;
    }
    NavigateAndExpectActivation({test_data.url_matches_activation_list},
                                {GURL(url)}, expected_decision);
  }
  for (auto* url : supported_urls) {
    SCOPED_TRACE(url);
    NavigateAndExpectActivation({test_data.url_matches_activation_list},
                                {GURL(url)},
                                test_data.expected_activation_decision);
  }
};

INSTANTIATE_TEST_CASE_P(NoSocEngHit,
                        ContentSubresourceFilterDriverFactoryThreatTypeTest,
                        ::testing::ValuesIn(kActivationListTestData));

INSTANTIATE_TEST_CASE_P(
    ActivationScopeTest,
    ContentSubresourceFilterDriverFactoryActivationScopeTest,
    ::testing::ValuesIn(kActivationScopeTestData));

INSTANTIATE_TEST_CASE_P(
    ActivationLevelTest,
    ContentSubresourceFilterDriverFactoryActivationLevelTest,
    ::testing::ValuesIn(kActivationLevelTestData));

}  // namespace subresource_filter
