// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <string>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/path_service.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/histogram_tester.h"
#include "base/test/simple_test_clock.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/content_settings/tab_specific_content_settings.h"
#include "chrome/browser/metrics/subprocess_metrics_provider.h"
#include "chrome/browser/page_load_metrics/observers/subresource_filter_metrics_observer.h"
#include "chrome/browser/safe_browsing/test_safe_browsing_service.h"
#include "chrome/browser/safe_browsing/v4_test_utils.h"
#include "chrome/browser/subresource_filter/chrome_subresource_filter_client.h"
#include "chrome/browser/subresource_filter/subresource_filter_content_settings_manager.h"
#include "chrome/browser/subresource_filter/subresource_filter_profile_context.h"
#include "chrome/browser/subresource_filter/subresource_filter_profile_context_factory.h"
#include "chrome/browser/subresource_filter/test_ruleset_publisher.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/safe_browsing_db/test_database_manager.h"
#include "components/safe_browsing_db/util.h"
#include "components/safe_browsing_db/v4_database.h"
#include "components/security_interstitials/content/unsafe_resource.h"
#include "components/subresource_filter/content/browser/async_document_subresource_filter.h"
#include "components/subresource_filter/content/browser/async_document_subresource_filter_test_utils.h"
#include "components/subresource_filter/content/browser/content_ruleset_service.h"
#include "components/subresource_filter/content/browser/content_subresource_filter_driver_factory.h"
#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "components/subresource_filter/core/browser/subresource_filter_features_test_support.h"
#include "components/subresource_filter/core/common/activation_decision.h"
#include "components/subresource_filter/core/common/activation_level.h"
#include "components/subresource_filter/core/common/activation_state.h"
#include "components/subresource_filter/core/common/scoped_timers.h"
#include "components/subresource_filter/core/common/test_ruleset_creator.h"
#include "components/subresource_filter/core/common/test_ruleset_utils.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/spawned_test_server/spawned_test_server.h"
#include "net/test/test_data_directory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

// The path to a multi-frame document used for tests.
static constexpr const char kTestFrameSetPath[] =
    "/subresource_filter/frame_set.html";

// Names of DocumentLoad histograms.
constexpr const char kDocumentLoadActivationLevel[] =
    "SubresourceFilter.DocumentLoad.ActivationState";
constexpr const char kSubresourceLoadsTotal[] =
    "SubresourceFilter.DocumentLoad.NumSubresourceLoads.Total";
constexpr const char kSubresourceLoadsEvaluated[] =
    "SubresourceFilter.DocumentLoad.NumSubresourceLoads.Evaluated";
constexpr const char kSubresourceLoadsMatchedRules[] =
    "SubresourceFilter.DocumentLoad.NumSubresourceLoads.MatchedRules";
constexpr const char kSubresourceLoadsDisallowed[] =
    "SubresourceFilter.DocumentLoad.NumSubresourceLoads.Disallowed";

constexpr const char kSubresourceLoadsTotalForPage[] =
    "SubresourceFilter.PageLoad.NumSubresourceLoads.Total";
constexpr const char kSubresourceLoadsEvaluatedForPage[] =
    "SubresourceFilter.PageLoad.NumSubresourceLoads.Evaluated";
constexpr const char kSubresourceLoadsMatchedRulesForPage[] =
    "SubresourceFilter.PageLoad.NumSubresourceLoads.MatchedRules";
constexpr const char kSubresourceLoadsDisallowedForPage[] =
    "SubresourceFilter.PageLoad.NumSubresourceLoads.Disallowed";

// Names of the performance measurement histograms.
constexpr const char kActivationWallDuration[] =
    "SubresourceFilter.DocumentLoad.Activation.WallDuration";
constexpr const char kActivationCPUDuration[] =
    "SubresourceFilter.DocumentLoad.Activation.CPUDuration";
constexpr const char kEvaluationTotalWallDurationForPage[] =
    "SubresourceFilter.PageLoad.SubresourceEvaluation.TotalWallDuration";
constexpr const char kEvaluationTotalCPUDurationForPage[] =
    "SubresourceFilter.PageLoad.SubresourceEvaluation.TotalCPUDuration";
constexpr const char kEvaluationTotalWallDurationForDocument[] =
    "SubresourceFilter.DocumentLoad.SubresourceEvaluation.TotalWallDuration";
constexpr const char kEvaluationTotalCPUDurationForDocument[] =
    "SubresourceFilter.DocumentLoad.SubresourceEvaluation.TotalCPUDuration";
constexpr const char kEvaluationWallDuration[] =
    "SubresourceFilter.SubresourceLoad.Evaluation.WallDuration";
constexpr const char kEvaluationCPUDuration[] =
    "SubresourceFilter.SubresourceLoad.Evaluation.CPUDuration";

#if defined(GOOGLE_CHROME_BUILD)
// Names of navigation chain patterns histogram.
const char kMatchesPatternHistogramName[] =
    "SubresourceFilter.PageLoad.FinalURLMatch";
const char kNavigationChainSize[] =
    "SubresourceFilter.PageLoad.RedirectChainLength";
const char kSubresourceFilterOnlySuffix[] = ".SubresourceFilterOnly";
const char kSocialEngineeringAdsInterstitialSuffix[] =
    ".SocialEngineeringAdsInterstitial";
const char kPhishingInterstitialSuffix[] = ".PhishingInterstitial";
#endif

// Other histograms.
const char kSubresourceFilterActionsHistogram[] = "SubresourceFilter.Actions";

// UI manager that never actually shows any interstitials, but emulates as if
// the user chose to proceed through them.
class FakeSafeBrowsingUIManager
    : public safe_browsing::TestSafeBrowsingUIManager {
 public:
  FakeSafeBrowsingUIManager() {}

 protected:
  ~FakeSafeBrowsingUIManager() override {}

  void DisplayBlockingPage(const UnsafeResource& resource) override {
    resource.callback_thread->PostTask(
        FROM_HERE, base::Bind(resource.callback, true /* proceed */));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeSafeBrowsingUIManager);
};

GURL GetURLWithFragment(const GURL& url, base::StringPiece fragment) {
  GURL::Replacements replacements;
  replacements.SetRefStr(fragment);
  return url.ReplaceComponents(replacements);
}

}  // namespace

namespace subresource_filter {

using subresource_filter::testing::ScopedSubresourceFilterConfigurator;
using subresource_filter::testing::TestRulesetPublisher;
using subresource_filter::testing::TestRulesetCreator;
using subresource_filter::testing::TestRulesetPair;

// SubresourceFilterDisabledBrowserTest ---------------------------------------

class SubresourceFilterDisabledByDefaultBrowserTest
    : public InProcessBrowserTest {
 public:
  SubresourceFilterDisabledByDefaultBrowserTest() {}

 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(
        "suppress-enabling-subresource-filter-from-fieldtrial-testing-config");
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(SubresourceFilterDisabledByDefaultBrowserTest);
};

// The RulesetService should not even be instantiated when the feature is
// disabled, which should be the default state unless there is an override
// specified in the field trial configuration.
IN_PROC_BROWSER_TEST_F(SubresourceFilterDisabledByDefaultBrowserTest,
                       RulesetServiceNotCreated) {
  EXPECT_FALSE(g_browser_process->subresource_filter_ruleset_service());
}

// SubresourceFilterBrowserTest -----------------------------------------------

class SubresourceFilterBrowserTest : public InProcessBrowserTest {
 public:
  SubresourceFilterBrowserTest() {}
  ~SubresourceFilterBrowserTest() override {}

 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitchASCII(
        switches::kEnableFeatures,
        base::JoinString(
            {kSafeBrowsingSubresourceFilter.name, "SafeBrowsingV4OnlyEnabled",
             kSafeBrowsingSubresourceFilterExperimentalUI.name},
            ","));
  }

  void SetUp() override {
    sb_factory_ =
        base::MakeUnique<safe_browsing::TestSafeBrowsingServiceFactory>(
            safe_browsing::V4FeatureList::V4UsageStatus::V4_ONLY);
    sb_factory_->SetTestUIManager(new FakeSafeBrowsingUIManager());
    safe_browsing::SafeBrowsingService::RegisterFactory(sb_factory_.get());

    safe_browsing::V4Database::RegisterStoreFactoryForTest(
        base::WrapUnique(new safe_browsing::TestV4StoreFactory()));

    v4_db_factory_ = new safe_browsing::TestV4DatabaseFactory();
    safe_browsing::V4Database::RegisterDatabaseFactoryForTest(
        base::WrapUnique(v4_db_factory_));

    v4_get_hash_factory_ =
        new safe_browsing::TestV4GetHashProtocolManagerFactory();
    safe_browsing::V4GetHashProtocolManager::RegisterFactory(
        base::WrapUnique(v4_get_hash_factory_));
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    // Unregister test factories after InProcessBrowserTest::TearDown
    // (which destructs SafeBrowsingService).
    safe_browsing::V4GetHashProtocolManager::RegisterFactory(nullptr);
    safe_browsing::V4Database::RegisterDatabaseFactoryForTest(nullptr);
    safe_browsing::V4Database::RegisterStoreFactoryForTest(nullptr);
    safe_browsing::SafeBrowsingService::RegisterFactory(nullptr);
  }

  void SetUpOnMainThread() override {
    base::FilePath test_data_dir;
    PathService::Get(chrome::DIR_TEST_DATA, &test_data_dir);
    embedded_test_server()->ServeFilesFromDirectory(test_data_dir);
    host_resolver()->AddSimulatedFailure("host-with-dns-lookup-failure");
    host_resolver()->AddRule("*", "127.0.0.1");
    content::SetupCrossSiteRedirector(embedded_test_server());
    ASSERT_TRUE(embedded_test_server()->Start());
    ResetConfigurationToEnableOnPhishingSites();

    settings_manager_ = SubresourceFilterProfileContextFactory::GetForProfile(
                            browser()->profile())
                            ->settings_manager();
#if defined(OS_ANDROID)
    EXPECT_TRUE(settings_manager->should_use_smart_ui());
#endif
  }

  GURL GetTestUrl(const std::string& relative_url) {
    return embedded_test_server()->base_url().Resolve(relative_url);
  }

  void MarkUrlAsMatchingListWithId(
      const GURL& bad_url,
      const safe_browsing::ListIdentifier& list_id,
      safe_browsing::ThreatPatternType threat_pattern_type) {
    safe_browsing::FullHashInfo full_hash_info =
        GetFullHashInfoWithMetadata(bad_url, list_id, threat_pattern_type);
    v4_db_factory_->MarkPrefixAsBad(list_id, full_hash_info.full_hash);
    v4_get_hash_factory_->AddToFullHashCache(full_hash_info);
  }

  void ConfigureAsPhishingURL(const GURL& url) {
    MarkUrlAsMatchingListWithId(url, safe_browsing::GetUrlSocEngId(),
                                safe_browsing::ThreatPatternType::NONE);
  }

  void ConfigureAsSubresourceFilterOnlyURL(const GURL& url) {
    MarkUrlAsMatchingListWithId(url, safe_browsing::GetUrlSubresourceFilterId(),
                                safe_browsing::ThreatPatternType::NONE);
  }

  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  SubresourceFilterContentSettingsManager* settings_manager() {
    return settings_manager_;
  }

  content::RenderFrameHost* FindFrameByName(const std::string& name) {
    return content::FrameMatchingPredicate(
        web_contents(), base::Bind(&content::FrameMatchesName, name));
  }

  bool WasParsedScriptElementLoaded(content::RenderFrameHost* rfh) {
    DCHECK(rfh);
    bool script_resource_was_loaded = false;
    EXPECT_TRUE(content::ExecuteScriptAndExtractBool(
        rfh, "domAutomationController.send(!!document.scriptExecuted)",
        &script_resource_was_loaded));
    return script_resource_was_loaded;
  }

  void ExpectParsedScriptElementLoadedStatusInFrames(
      const std::vector<const char*>& frame_names,
      const std::vector<bool>& expect_loaded) {
    ASSERT_EQ(expect_loaded.size(), frame_names.size());
    for (size_t i = 0; i < frame_names.size(); ++i) {
      SCOPED_TRACE(frame_names[i]);
      content::RenderFrameHost* frame = FindFrameByName(frame_names[i]);
      ASSERT_TRUE(frame);
      ASSERT_EQ(expect_loaded[i], WasParsedScriptElementLoaded(frame));
    }
  }

  bool IsDynamicScriptElementLoaded(content::RenderFrameHost* rfh) {
    DCHECK(rfh);
    bool script_resource_was_loaded = false;
    EXPECT_TRUE(content::ExecuteScriptAndExtractBool(
        rfh, "insertScriptElementAndReportSuccess()",
        &script_resource_was_loaded));
    return script_resource_was_loaded;
  }

  void InsertDynamicFrameWithScript() {
    bool frame_was_loaded = false;
    ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
        web_contents()->GetMainFrame(), "insertFrameWithScriptAndNotify()",
        &frame_was_loaded));
    ASSERT_TRUE(frame_was_loaded);
  }

  void NavigateFromRendererSide(const GURL& url) {
    ASSERT_TRUE(content::ExecuteScript(
        web_contents()->GetMainFrame(),
        base::StringPrintf("window.location = \"%s\";", url.spec().c_str())));
  }

  void SetRulesetToDisallowURLsWithPathSuffix(const std::string& suffix) {
    TestRulesetPair test_ruleset_pair;
    ruleset_creator_.CreateRulesetToDisallowURLsWithPathSuffix(
        suffix, &test_ruleset_pair);
    ASSERT_NO_FATAL_FAILURE(
        test_ruleset_publisher_.SetRuleset(test_ruleset_pair.unindexed));
  }

  void SetRulesetWithRules(const std::vector<proto::UrlRule>& rules) {
    TestRulesetPair test_ruleset_pair;
    ruleset_creator_.CreateRulesetWithRules(rules, &test_ruleset_pair);
    ASSERT_NO_FATAL_FAILURE(
        test_ruleset_publisher_.SetRuleset(test_ruleset_pair.unindexed));
  }

  void ResetConfiguration(Configuration config) {
    scoped_configuration_.ResetConfiguration(std::move(config));
  }

  void ResetConfigurationToEnableOnPhishingSites(
      bool measure_performance = false,
      bool whitelist_site_on_reload = false) {
    Configuration config = Configuration::MakePresetForLiveRunOnPhishingSites();
    config.activation_options.performance_measurement_rate =
        measure_performance ? 1.0 : 0.0;
    config.activation_options.should_whitelist_site_on_reload =
        whitelist_site_on_reload;
    ResetConfiguration(std::move(config));
  }

 private:
  TestRulesetCreator ruleset_creator_;
  ScopedSubresourceFilterConfigurator scoped_configuration_;
  TestRulesetPublisher test_ruleset_publisher_;

  std::unique_ptr<safe_browsing::TestSafeBrowsingServiceFactory> sb_factory_;
  // Owned by the V4Database.
  safe_browsing::TestV4DatabaseFactory* v4_db_factory_;
  // Owned by the V4GetHashProtocolManager.
  safe_browsing::TestV4GetHashProtocolManagerFactory* v4_get_hash_factory_;

  // Owned by the profile.
  SubresourceFilterContentSettingsManager* settings_manager_;

  DISALLOW_COPY_AND_ASSIGN(SubresourceFilterBrowserTest);
};

enum WebSocketCreationPolicy {
  IN_MAIN_FRAME,
  IN_WORKER,
};
class SubresourceFilterWebSocketBrowserTest
    : public SubresourceFilterBrowserTest,
      public ::testing::WithParamInterface<WebSocketCreationPolicy> {
 public:
  SubresourceFilterWebSocketBrowserTest() {}

  void SetUpOnMainThread() override {
    SubresourceFilterBrowserTest::SetUpOnMainThread();
    websocket_test_server_ = base::MakeUnique<net::SpawnedTestServer>(
        net::SpawnedTestServer::TYPE_WS, net::SpawnedTestServer::kLocalhost,
        net::GetWebSocketTestDataDirectory());
    ASSERT_TRUE(websocket_test_server_->Start());
  }

  net::SpawnedTestServer* websocket_test_server() {
    return websocket_test_server_.get();
  }

  GURL GetWebSocketUrl(const std::string& path) {
    GURL::Replacements replacements;
    replacements.SetSchemeStr("ws");
    return websocket_test_server_->GetURL(path).ReplaceComponents(replacements);
  }

  void CreateWebSocketAndExpectResult(const GURL& url,
                                      bool expect_connection_success) {
    bool websocket_connection_succeeded = false;
    EXPECT_TRUE(content::ExecuteScriptAndExtractBool(
        browser()->tab_strip_model()->GetActiveWebContents(),
        base::StringPrintf("connectWebSocket('%s');", url.spec().c_str()),
        &websocket_connection_succeeded));
    EXPECT_EQ(expect_connection_success, websocket_connection_succeeded);
  }

 private:
  std::unique_ptr<net::SpawnedTestServer> websocket_test_server_;
};

// Tests -----------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest, MainFrameActivation) {
  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));
  ConfigureAsPhishingURL(url);
  ASSERT_NO_FATAL_FAILURE(SetRulesetToDisallowURLsWithPathSuffix(
      "suffix-that-does-not-match-anything"));
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  // The main frame document should never be filtered.
  SetRulesetToDisallowURLsWithPathSuffix("frame_with_included_script.html");
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));
}

#if defined(GOOGLE_CHROME_BUILD)
IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       MainFrameActivation_SubresourceFilterList) {
  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));
  ConfigureAsSubresourceFilterOnlyURL(url);
  ASSERT_NO_FATAL_FAILURE(SetRulesetToDisallowURLsWithPathSuffix(
      "suffix-that-does-not-match-anything"));

  Configuration config(subresource_filter::ActivationLevel::ENABLED,
                       subresource_filter::ActivationScope::ACTIVATION_LIST,
                       subresource_filter::ActivationList::SUBRESOURCE_FILTER);
  ResetConfiguration(std::move(config));

  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  // The main frame document should never be filtered.
  SetRulesetToDisallowURLsWithPathSuffix("frame_with_included_script.html");
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));
}
#endif

// There should be no document-level de-/reactivation happening on the renderer
// side as a result of a same document navigation.
IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       DocumentActivationOutlivesSameDocumentNavigation) {
  GURL url(GetTestUrl("subresource_filter/frame_with_delayed_script.html"));
  ConfigureAsPhishingURL(url);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ui_test_utils::NavigateToURL(browser(), url);

  // Deactivation would already detected by the IsDynamicScriptElementLoaded
  // line alone. To ensure no reactivation, which would muddy up recorded
  // histograms, also set a ruleset that allows everything. If there was
  // reactivation, then this new ruleset would be picked up, once again causing
  // the IsDynamicScriptElementLoaded check to fail.
  ASSERT_NO_FATAL_FAILURE(SetRulesetToDisallowURLsWithPathSuffix(
      "suffix-that-does-not-match-anything"));
  NavigateFromRendererSide(GetURLWithFragment(url, "ref"));
  EXPECT_FALSE(IsDynamicScriptElementLoaded(web_contents()->GetMainFrame()));
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest, SubFrameActivation) {
  GURL url(GetTestUrl(kTestFrameSetPath));
  ConfigureAsPhishingURL(url);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  base::HistogramTester tester;
  ui_test_utils::NavigateToURL(browser(), url);

  const std::vector<const char*> kSubframeNames{"one", "two", "three"};
  const std::vector<bool> kExpectScriptInFrameToLoad{false, true, false};
  ASSERT_NO_FATAL_FAILURE(ExpectParsedScriptElementLoadedStatusInFrames(
      kSubframeNames, kExpectScriptInFrameToLoad));

  tester.ExpectBucketCount(kSubresourceFilterActionsHistogram, kActionUIShown,
                           1);
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       SubframeDocumentLoadFiltering) {
  GURL url(GetTestUrl(kTestFrameSetPath));
  ConfigureAsPhishingURL(url);

  // Disallow all the subframes that end up loading included_script.js.
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.html"));
  base::HistogramTester tester;
  ui_test_utils::NavigateToURL(browser(), url);

  const std::vector<const char*> kSubframeNames{"one", "two", "three"};
  const std::vector<bool> kExpectScriptInFrameToLoad{false, true, false};
  ASSERT_NO_FATAL_FAILURE(ExpectParsedScriptElementLoadedStatusInFrames(
      kSubframeNames, kExpectScriptInFrameToLoad));

  // The subframe navigations never commit.
  for (size_t i = 0; i < kSubframeNames.size(); ++i) {
    const char* subframe_name = kSubframeNames[i];
    bool expect_loaded = kExpectScriptInFrameToLoad[i];
    content::RenderFrameHost* frame = FindFrameByName(subframe_name);
    if (!expect_loaded)
      EXPECT_EQ(GURL(), frame->GetLastCommittedURL());
  }

  tester.ExpectBucketCount(kSubresourceFilterActionsHistogram, kActionUIShown,
                           1);
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       HistoryNavigationActivation) {
  GURL url_with_activation(GetTestUrl(kTestFrameSetPath));
  GURL url_without_activation(
      embedded_test_server()->GetURL("a.com", kTestFrameSetPath));
  ConfigureAsPhishingURL(url_with_activation);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));

  const std::vector<const char*> kSubframeNames{"one", "two", "three"};
  const std::vector<bool> kExpectScriptInFrameToLoadWithoutActivation{
      true, true, true};
  const std::vector<bool> kExpectScriptInFrameToLoadWithActivation{false, true,
                                                                   false};

  ui_test_utils::NavigateToURL(browser(), url_without_activation);
  ASSERT_NO_FATAL_FAILURE(ExpectParsedScriptElementLoadedStatusInFrames(
      kSubframeNames, kExpectScriptInFrameToLoadWithoutActivation));

  ui_test_utils::NavigateToURL(browser(), url_with_activation);
  ASSERT_NO_FATAL_FAILURE(ExpectParsedScriptElementLoadedStatusInFrames(
      kSubframeNames, kExpectScriptInFrameToLoadWithActivation));

  ASSERT_TRUE(web_contents()->GetController().CanGoBack());
  content::WindowedNotificationObserver back_navigation_stop_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::NotificationService::AllSources());
  web_contents()->GetController().GoBack();
  back_navigation_stop_observer.Wait();
  ASSERT_NO_FATAL_FAILURE(ExpectParsedScriptElementLoadedStatusInFrames(
      kSubframeNames, kExpectScriptInFrameToLoadWithoutActivation));

  ASSERT_TRUE(web_contents()->GetController().CanGoForward());
  content::WindowedNotificationObserver forward_navigation_stop_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::NotificationService::AllSources());
  web_contents()->GetController().GoForward();
  forward_navigation_stop_observer.Wait();
  ASSERT_NO_FATAL_FAILURE(ExpectParsedScriptElementLoadedStatusInFrames(
      kSubframeNames, kExpectScriptInFrameToLoadWithActivation));
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       FailedProvisionalLoadInMainframe) {
  GURL url_with_activation_but_dns_error(
      "http://host-with-dns-lookup-failure/");
  GURL url_with_activation_but_not_existent(GetTestUrl("non-existent.html"));
  GURL url_without_activation(GetTestUrl(kTestFrameSetPath));

  ConfigureAsPhishingURL(url_with_activation_but_dns_error);
  ConfigureAsPhishingURL(url_with_activation_but_not_existent);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));

  const std::vector<const char*> kSubframeNames{"one", "two", "three"};
  const std::vector<bool> kExpectScriptInFrameToLoad{true, true, true};

  for (const auto& url_with_activation :
       {url_with_activation_but_dns_error,
        url_with_activation_but_not_existent}) {
    SCOPED_TRACE(url_with_activation);

    // In either test case, there is no server-supplied error page, so Chrome's
    // own navigation error page is shown. This also triggers a background
    // request to load navigation corrections (aka. Link Doctor), and once the
    // results are back, there is a navigation to a second error page with the
    // suggestions. Hence the wait for two navigations in a row.
    ui_test_utils::NavigateToURLBlockUntilNavigationsComplete(
        browser(), url_with_activation, 2);
    ui_test_utils::NavigateToURL(browser(), url_without_activation);
    ASSERT_NO_FATAL_FAILURE(ExpectParsedScriptElementLoadedStatusInFrames(
        kSubframeNames, kExpectScriptInFrameToLoad));
  }
}

// The page-level activation state on the browser-side should not be reset when
// a same document navigation starts in the main frame. Verify this by
// dynamically inserting a subframe afterwards, and still expecting activation.
IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       PageLevelActivationOutlivesSameDocumentNavigation) {
  GURL url(GetTestUrl(kTestFrameSetPath));
  ConfigureAsPhishingURL(url);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ui_test_utils::NavigateToURL(browser(), url);

  content::RenderFrameHost* frame = FindFrameByName("one");
  ASSERT_TRUE(frame);
  EXPECT_FALSE(WasParsedScriptElementLoaded(frame));

  NavigateFromRendererSide(GetURLWithFragment(url, "ref"));

  ASSERT_NO_FATAL_FAILURE(InsertDynamicFrameWithScript());
  content::RenderFrameHost* dynamic_frame = FindFrameByName("dynamic");
  ASSERT_TRUE(dynamic_frame);
  EXPECT_FALSE(WasParsedScriptElementLoaded(dynamic_frame));
}

// If a navigation starts but aborts before commit, page level activation should
// remain unchanged.
IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       PageLevelActivationOutlivesAbortedNavigation) {
  GURL url(GetTestUrl(kTestFrameSetPath));
  ConfigureAsPhishingURL(url);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ui_test_utils::NavigateToURL(browser(), url);

  content::RenderFrameHost* frame = FindFrameByName("one");
  EXPECT_FALSE(WasParsedScriptElementLoaded(frame));

  // Start a new navigation, but abort it right away.
  GURL aborted_url = GURL("https://abort-me.com");
  content::TestNavigationManager manager(
      browser()->tab_strip_model()->GetActiveWebContents(), aborted_url);

  chrome::NavigateParams params(browser(), aborted_url,
                                ui::PAGE_TRANSITION_LINK);
  chrome::Navigate(&params);
  ASSERT_TRUE(manager.WaitForRequestStart());
  browser()->tab_strip_model()->GetActiveWebContents()->Stop();

  // Will return false if the navigation was successfully aborted.
  ASSERT_FALSE(manager.WaitForResponse());
  manager.WaitForNavigationFinished();

  // Now, dynamically insert a frame and expect that it is still activated.
  ASSERT_NO_FATAL_FAILURE(InsertDynamicFrameWithScript());
  content::RenderFrameHost* dynamic_frame = FindFrameByName("dynamic");
  ASSERT_TRUE(dynamic_frame);
  EXPECT_FALSE(WasParsedScriptElementLoaded(dynamic_frame));
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest, DynamicFrame) {
  GURL url(GetTestUrl("subresource_filter/frame_set.html"));
  ConfigureAsPhishingURL(url);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ui_test_utils::NavigateToURL(browser(), url);

  ASSERT_NO_FATAL_FAILURE(InsertDynamicFrameWithScript());
  content::RenderFrameHost* dynamic_frame = FindFrameByName("dynamic");
  ASSERT_TRUE(dynamic_frame);
  EXPECT_FALSE(WasParsedScriptElementLoaded(dynamic_frame));
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       RulesetVerified_Activation) {
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ContentRulesetService* service =
      g_browser_process->subresource_filter_ruleset_service();
  ASSERT_TRUE(service->ruleset_dealer());
  auto ruleset_handle =
      base::MakeUnique<VerifiedRuleset::Handle>(service->ruleset_dealer());
  AsyncDocumentSubresourceFilter::InitializationParams params(
      GURL("https://example.com/"), ActivationLevel::ENABLED, false);

  testing::TestActivationStateCallbackReceiver receiver;
  AsyncDocumentSubresourceFilter filter(ruleset_handle.get(), std::move(params),
                                        receiver.GetCallback());
  receiver.WaitForActivationDecision();
  receiver.ExpectReceivedOnce(ActivationState(ActivationLevel::ENABLED));
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest, NoRuleset_NoActivation) {
  // Do not set the ruleset, which results in an invalid ruleset.
  ContentRulesetService* service =
      g_browser_process->subresource_filter_ruleset_service();
  ASSERT_TRUE(service->ruleset_dealer());
  auto ruleset_handle =
      base::MakeUnique<VerifiedRuleset::Handle>(service->ruleset_dealer());
  AsyncDocumentSubresourceFilter::InitializationParams params(
      GURL("https://example.com/"), ActivationLevel::ENABLED, false);

  testing::TestActivationStateCallbackReceiver receiver;
  AsyncDocumentSubresourceFilter filter(ruleset_handle.get(), std::move(params),
                                        receiver.GetCallback());
  receiver.WaitForActivationDecision();
  receiver.ExpectReceivedOnce(ActivationState(ActivationLevel::DISABLED));
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       InvalidRuleset_NoActivation) {
  const char kTestRulesetSuffix[] = "foo";
  const int kNumberOfRules = 500;
  TestRulesetCreator ruleset_creator;
  TestRulesetPair test_ruleset_pair;
  ASSERT_NO_FATAL_FAILURE(
      ruleset_creator.CreateRulesetToDisallowURLsWithManySuffixes(
          kTestRulesetSuffix, kNumberOfRules, &test_ruleset_pair));
  testing::TestRuleset::CorruptByTruncating(test_ruleset_pair.indexed, 123);

  // Just publish the corrupt indexed file directly, to simulate it being
  // corrupt on startup.
  ContentRulesetService* service =
      g_browser_process->subresource_filter_ruleset_service();
  ASSERT_TRUE(service->ruleset_dealer());
  service->PublishNewRulesetVersion(
      testing::TestRuleset::Open(test_ruleset_pair.indexed));

  auto ruleset_handle =
      base::MakeUnique<VerifiedRuleset::Handle>(service->ruleset_dealer());
  AsyncDocumentSubresourceFilter::InitializationParams params(
      GURL("https://example.com/"), ActivationLevel::ENABLED, false);

  testing::TestActivationStateCallbackReceiver receiver;
  AsyncDocumentSubresourceFilter filter(ruleset_handle.get(), std::move(params),
                                        receiver.GetCallback());
  receiver.WaitForActivationDecision();
  receiver.ExpectReceivedOnce(ActivationState(ActivationLevel::DISABLED));
}

// Schemes 'about:' and 'chrome-native:' are loaded synchronously as empty
// documents in Blink, so there is no chance (or need) to send an activation
// IPC message. Make sure that histograms are not polluted with these loads.
IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest, NoActivationOnAboutBlank) {
  GURL url(GetTestUrl("subresource_filter/frame_set_sync_loads.html"));
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ConfigureAsPhishingURL(url);

  base::HistogramTester histogram_tester;
  ui_test_utils::NavigateToURL(browser(), url);

  content::RenderFrameHost* frame = FindFrameByName("initially_blank");
  ASSERT_TRUE(frame);
  EXPECT_FALSE(WasParsedScriptElementLoaded(frame));

  // Support both pre-/post-PersistentHistograms worlds. The latter is enabled
  // through field trials, so the former is still used on offical builders.
  content::FetchHistogramsFromChildProcesses();
  SubprocessMetricsProvider::MergeHistogramDeltasForTesting();

  // The only frames where filtering was (even considered to be) activated
  // should be the main frame, and the child that was navigated to an HTTP URL.
  histogram_tester.ExpectUniqueSample(
      kDocumentLoadActivationLevel,
      static_cast<base::Histogram::Sample>(ActivationLevel::ENABLED), 2);
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest, PageLoadMetrics) {
  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));
  ConfigureAsPhishingURL(url);

  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));

  base::HistogramTester histogram_tester;
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  // Force a navigation to another page, which flushes page load metrics for the
  // previous page load.
  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));

  histogram_tester.ExpectTotalCount(internal::kHistogramSubresourceFilterCount,
                                    1);
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       PRE_MainFrameActivationOnStartup) {
  SetRulesetToDisallowURLsWithPathSuffix("included_script.js");
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       MainFrameActivationOnStartup) {
  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));
  ConfigureAsPhishingURL(url);
  // Verify that the ruleset persisted in the previous session is used for this
  // page load right after start-up.
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       PromptShownAgainOnNextNavigation) {
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  GURL url(GetTestUrl(kTestFrameSetPath));
  GURL a_url(embedded_test_server()->GetURL(
      "a.com", "/subresource_filter/frame_with_included_script.html"));
  ConfigureAsPhishingURL(url);
  base::HistogramTester tester;
  ui_test_utils::NavigateToURL(browser(), url);
  tester.ExpectBucketCount(kSubresourceFilterActionsHistogram, kActionUIShown,
                           1);
  // Check that the bubble is not shown again for this navigation.
  EXPECT_FALSE(IsDynamicScriptElementLoaded(FindFrameByName("five")));
  tester.ExpectBucketCount(kSubresourceFilterActionsHistogram, kActionUIShown,
                           1);
  // Check that bubble is shown for new navigation. Must be cross site to avoid
  // triggering smart UI on Android.
  ConfigureAsPhishingURL(a_url);
  ui_test_utils::NavigateToURL(browser(), a_url);
  tester.ExpectBucketCount(kSubresourceFilterActionsHistogram, kActionUIShown,
                           2);
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       CrossSiteSubFrameActivationWithoutWhitelist) {
  GURL a_url(embedded_test_server()->GetURL(
      "a.com", "/subresource_filter/frame_cross_site_set.html"));
  ConfigureAsPhishingURL(a_url);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ui_test_utils::NavigateToURL(browser(), a_url);
  ExpectParsedScriptElementLoadedStatusInFrames(
      std::vector<const char*>{"b", "c", "d"}, {false, false, false});
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       CrossSiteSubFrameActivationWithWhitelist) {
  GURL a_url(embedded_test_server()->GetURL(
      "a.com", "/subresource_filter/frame_cross_site_set.html"));
  ConfigureAsPhishingURL(a_url);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetWithRules({testing::CreateSuffixRule("included_script.js"),
                           testing::CreateWhitelistRuleForDocument("c.com")}));
  ui_test_utils::NavigateToURL(browser(), a_url);
  ExpectParsedScriptElementLoadedStatusInFrames(
      std::vector<const char*>{"b", "d"}, {false, true});
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest, BlockCreatingNewWindows) {
  base::HistogramTester tester;
  const char kWindowOpenPath[] = "/subresource_filter/window_open.html";
  GURL a_url(embedded_test_server()->GetURL("a.com", kWindowOpenPath));
  GURL b_url(embedded_test_server()->GetURL("b.com", kWindowOpenPath));
  // Only configure |a_url| as a phishing URL.
  ConfigureAsPhishingURL(a_url);

  // Only necessary so we have a valid ruleset.
  ASSERT_NO_FATAL_FAILURE(SetRulesetWithRules(std::vector<proto::UrlRule>()));

  // Navigate to a_url, should trigger the popup blocker.
  ui_test_utils::NavigateToURL(browser(), a_url);
  bool opened_window = false;
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(content::ExecuteScriptAndExtractBool(web_contents, "openWindow()",
                                                   &opened_window));
  EXPECT_FALSE(opened_window);
  // Do not force the UI if the popup was the only thing disallowed. The popup
  // UI is good enough.
  tester.ExpectBucketCount(kSubresourceFilterActionsHistogram, kActionUIShown,
                           0);
  // Make sure the popup UI was shown.
  EXPECT_TRUE(TabSpecificContentSettings::FromWebContents(web_contents)
                  ->IsContentBlocked(CONTENT_SETTINGS_TYPE_POPUPS));

  // Navigate to |b_url|, which should successfully open the popup.
  ui_test_utils::NavigateToURL(browser(), b_url);
  opened_window = false;
  EXPECT_TRUE(content::ExecuteScriptAndExtractBool(web_contents, "openWindow()",
                                                   &opened_window));
  EXPECT_TRUE(opened_window);
  // Popup UI should not be shown.
  EXPECT_FALSE(TabSpecificContentSettings::FromWebContents(web_contents)
                   ->IsContentBlocked(CONTENT_SETTINGS_TYPE_POPUPS));
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       ContentSettingsWhitelist_DoNotActivate) {
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));
  ConfigureAsPhishingURL(url);

  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  // Simulate an explicity whitelisting via content settings.
  HostContentSettingsMap* settings_map =
      HostContentSettingsMapFactory::GetForProfile(browser()->profile());
  settings_map->SetContentSettingDefaultScope(
      url, url, ContentSettingsType::CONTENT_SETTINGS_TYPE_SUBRESOURCE_FILTER,
      std::string(), CONTENT_SETTING_ALLOW);

  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       ContentSettingsAllowWithNoPageActivation_DoNotActivate) {
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));

  // Do not configure as phishing URL.

  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  // Simulate allowing the subresource filter via content settings.
  HostContentSettingsMap* settings_map =
      HostContentSettingsMapFactory::GetForProfile(browser()->profile());
  settings_map->SetContentSettingDefaultScope(
      url, url, ContentSettingsType::CONTENT_SETTINGS_TYPE_SUBRESOURCE_FILTER,
      std::string(), CONTENT_SETTING_BLOCK);

  // Setting the site to "allow" should not activate filtering.
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       ContentSettingsWhitelistViaReload_DoNotActivate) {
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));
  ConfigureAsPhishingURL(url);

  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  // Whitelist via a reload.
  content::TestNavigationObserver navigation_observer(web_contents(), 1);
  ChromeSubresourceFilterClient::FromWebContents(web_contents())
      ->OnReloadRequested();
  navigation_observer.Wait();

  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       ContentSettingsWhitelistViaReload_WhitelistIsByDomain) {
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));
  ConfigureAsPhishingURL(url);

  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  // Whitelist via a reload.
  content::TestNavigationObserver navigation_observer(web_contents(), 1);
  ChromeSubresourceFilterClient::FromWebContents(web_contents())
      ->OnReloadRequested();
  navigation_observer.Wait();

  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  // Another navigation to the same domain should be whitelisted too.
  ui_test_utils::NavigateToURL(
      browser(),
      GetTestUrl("subresource_filter/frame_with_included_script.html?query"));
  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  // A cross site blacklisted navigation should stay activated, however.
  GURL a_url(embedded_test_server()->GetURL(
      "a.com", "/subresource_filter/frame_with_included_script.html"));
  ConfigureAsPhishingURL(a_url);
  ui_test_utils::NavigateToURL(browser(), a_url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));
}

// Test the "smart" UI, aka the logic to hide the UI on subsequent same-domain
// navigations, until a certain time threshold has been reached. This is an
// android-only feature.
IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       DoNotShowUIUntilThresholdReached) {
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  GURL a_url(embedded_test_server()->GetURL(
      "a.com", "/subresource_filter/frame_with_included_script.html"));
  GURL b_url(embedded_test_server()->GetURL(
      "b.com", "/subresource_filter/frame_with_included_script.html"));
  // Test utils only support one blacklisted site at a time.
  // TODO(csharrison): Add support for more than one URL.
  ConfigureAsPhishingURL(a_url);

  ChromeSubresourceFilterClient* client =
      ChromeSubresourceFilterClient::FromWebContents(web_contents());
  auto test_clock = base::MakeUnique<base::SimpleTestClock>();
  base::SimpleTestClock* raw_clock = test_clock.get();
  settings_manager()->set_clock_for_testing(std::move(test_clock));

  base::HistogramTester histogram_tester;

  // First load should trigger the UI.
  ui_test_utils::NavigateToURL(browser(), a_url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));
  EXPECT_TRUE(client->did_show_ui_for_navigation());

  histogram_tester.ExpectBucketCount(kSubresourceFilterActionsHistogram,
                                     kActionUISuppressed, 0);

  // Second load should not trigger the UI, but should still filter content.
  ui_test_utils::NavigateToURL(browser(), a_url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  bool use_smart_ui = settings_manager()->should_use_smart_ui();
  EXPECT_EQ(client->did_show_ui_for_navigation(), !use_smart_ui);

  histogram_tester.ExpectBucketCount(kSubresourceFilterActionsHistogram,
                                     kActionUISuppressed, use_smart_ui ? 1 : 0);

  ConfigureAsPhishingURL(b_url);

  // Load to another domain should trigger the UI.
  ui_test_utils::NavigateToURL(browser(), b_url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));
  EXPECT_TRUE(client->did_show_ui_for_navigation());

  ConfigureAsPhishingURL(a_url);

  // Fast forward the clock, and a_url should trigger the UI again.
  raw_clock->Advance(
      SubresourceFilterContentSettingsManager::kDelayBeforeShowingInfobarAgain);
  ui_test_utils::NavigateToURL(browser(), a_url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));
  EXPECT_TRUE(client->did_show_ui_for_navigation());

  histogram_tester.ExpectBucketCount(kSubresourceFilterActionsHistogram,
                                     kActionUISuppressed, use_smart_ui ? 1 : 0);
}

IN_PROC_BROWSER_TEST_P(SubresourceFilterWebSocketBrowserTest, BlockWebSocket) {
  GURL url(GetTestUrl(
      base::StringPrintf("subresource_filter/page_with_websocket.html?%s",
                         GetParam() == IN_WORKER ? "inWorker" : "")));
  GURL websocket_url(GetWebSocketUrl("echo-with-no-extension"));
  ConfigureAsPhishingURL(url);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("echo-with-no-extension"));
  ui_test_utils::NavigateToURL(browser(), url);
  CreateWebSocketAndExpectResult(websocket_url,
                                 false /* expect_connection_success */);
}

IN_PROC_BROWSER_TEST_P(SubresourceFilterWebSocketBrowserTest,
                       DoNotBlockWebSocketNoActivatedFrame) {
  GURL url(GetTestUrl(
      base::StringPrintf("subresource_filter/page_with_websocket.html?%s",
                         GetParam() == IN_WORKER ? "inWorker" : "")));
  GURL websocket_url(GetWebSocketUrl("echo-with-no-extension"));
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("echo-with-no-extension"));
  ui_test_utils::NavigateToURL(browser(), url);

  CreateWebSocketAndExpectResult(websocket_url,
                                 true /* expect_connection_success */);
}

IN_PROC_BROWSER_TEST_P(SubresourceFilterWebSocketBrowserTest,
                       DoNotBlockWebSocketInActivatedFrameWithNoRule) {
  GURL url(GetTestUrl(
      base::StringPrintf("subresource_filter/page_with_websocket.html?%s",
                         GetParam() == IN_WORKER ? "inWorker" : "")));
  GURL websocket_url(GetWebSocketUrl("echo-with-no-extension"));
  ConfigureAsPhishingURL(url);
  ui_test_utils::NavigateToURL(browser(), url);

  CreateWebSocketAndExpectResult(websocket_url,
                                 true /* expect_connection_success */);
}

INSTANTIATE_TEST_CASE_P(
    /* no prefix */,
    SubresourceFilterWebSocketBrowserTest,
    ::testing::Values(WebSocketCreationPolicy::IN_WORKER,
                      WebSocketCreationPolicy::IN_MAIN_FRAME));

// Tests checking how histograms are recorded. ---------------------------------

namespace {

void ExpectHistogramsAreRecordedForTestFrameSet(
    const base::HistogramTester& tester,
    bool expect_performance_measurements) {
  const bool time_recorded =
      expect_performance_measurements && ScopedThreadTimers::IsSupported();

  // The following histograms are generated on the browser side.
  tester.ExpectUniqueSample(kSubresourceLoadsTotalForPage, 6, 1);
  tester.ExpectUniqueSample(kSubresourceLoadsEvaluatedForPage, 6, 1);
  tester.ExpectUniqueSample(kSubresourceLoadsMatchedRulesForPage, 4, 1);
  tester.ExpectUniqueSample(kSubresourceLoadsDisallowedForPage, 4, 1);
  tester.ExpectTotalCount(kEvaluationTotalWallDurationForPage, time_recorded);
  tester.ExpectTotalCount(kEvaluationTotalCPUDurationForPage, time_recorded);

  // The rest is produced by renderers, therefore needs to be merged here.
  content::FetchHistogramsFromChildProcesses();
  SubprocessMetricsProvider::MergeHistogramDeltasForTesting();

  tester.ExpectTotalCount(kEvaluationTotalWallDurationForDocument,
                          time_recorded ? 6 : 0);
  tester.ExpectTotalCount(kEvaluationTotalCPUDurationForDocument,
                          time_recorded ? 6 : 0);

  // 5 subframes, each with an include.js, plus a top level include.js.
  int num_subresource_checks = 5 + 5 + 1;
  tester.ExpectTotalCount(kEvaluationWallDuration,
                          time_recorded ? num_subresource_checks : 0);
  tester.ExpectTotalCount(kEvaluationCPUDuration,
                          time_recorded ? num_subresource_checks : 0);

  // Activation timing histograms are always recorded.
  tester.ExpectTotalCount(kActivationWallDuration, 6);
  tester.ExpectTotalCount(kActivationCPUDuration, 6);

  tester.ExpectUniqueSample(
      kDocumentLoadActivationLevel,
      static_cast<base::Histogram::Sample>(ActivationLevel::ENABLED), 6);

  EXPECT_THAT(tester.GetAllSamples(kSubresourceLoadsTotal),
              ::testing::ElementsAre(base::Bucket(0, 3), base::Bucket(2, 3)));
  EXPECT_THAT(tester.GetAllSamples(kSubresourceLoadsEvaluated),
              ::testing::ElementsAre(base::Bucket(0, 3), base::Bucket(2, 3)));

  EXPECT_THAT(tester.GetAllSamples(kSubresourceLoadsMatchedRules),
              ::testing::ElementsAre(base::Bucket(0, 4), base::Bucket(2, 2)));
  EXPECT_THAT(tester.GetAllSamples(kSubresourceLoadsDisallowed),
              ::testing::ElementsAre(base::Bucket(0, 4), base::Bucket(2, 2)));
}

}  // namespace

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       ExpectHistogramsAreRecordedForFilteredChildFrames) {
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));

  // Navigate to a URL which doesn't include any filtered resources in the
  // top-level document, but which includes filtered resources in child frames.
  const GURL url = GetTestUrl(kTestFrameSetPath);
  ConfigureAsPhishingURL(url);

  base::HistogramTester tester;
  ui_test_utils::NavigateToURL(browser(), url);

  ExpectHistogramsAreRecordedForTestFrameSet(
      tester, false /* expect_performance_measurements */);

  // Force a navigation to another page, which flushes page load metrics for the
  // previous page load.
  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));

  // Make sure that pages that have subresource filtered in child frames are
  // also counted.
  tester.ExpectTotalCount(internal::kHistogramSubresourceFilterCount, 1);
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       ExpectPerformanceHistogramsAreRecorded) {
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ResetConfigurationToEnableOnPhishingSites(
      true /* measure_performance */, false /* whitelist_site_on_reload */);
  const GURL url = GetTestUrl(kTestFrameSetPath);
  ConfigureAsPhishingURL(url);

  base::HistogramTester tester;
  ui_test_utils::NavigateToURL(browser(), url);

  ExpectHistogramsAreRecordedForTestFrameSet(
      tester, true /* expect_performance_measurements */);
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       ExpectHistogramsNotRecordedWhenFilteringNotActivated) {
  ASSERT_NO_FATAL_FAILURE(SetRulesetToDisallowURLsWithPathSuffix(
      "suffix-that-does-not-match-anything"));
  const GURL url = GetTestUrl(kTestFrameSetPath);
  // Note: The |url| is not configured to be fishing.

  base::HistogramTester tester;
  ui_test_utils::NavigateToURL(browser(), url);

  // The following histograms are generated only when filtering is activated.
  tester.ExpectTotalCount(kSubresourceLoadsTotalForPage, 0);
  tester.ExpectTotalCount(kSubresourceLoadsEvaluatedForPage, 0);
  tester.ExpectTotalCount(kSubresourceLoadsMatchedRulesForPage, 0);
  tester.ExpectTotalCount(kSubresourceLoadsDisallowedForPage, 0);
  tester.ExpectTotalCount(kEvaluationTotalWallDurationForPage, 0);
  tester.ExpectTotalCount(kEvaluationTotalCPUDurationForPage, 0);

  // The rest is produced by renderers, therefore needs to be merged here.
  content::FetchHistogramsFromChildProcesses();
  SubprocessMetricsProvider::MergeHistogramDeltasForTesting();

  // But they still should not be recorded as the filtering is not activated.
  tester.ExpectTotalCount(kEvaluationTotalWallDurationForDocument, 0);
  tester.ExpectTotalCount(kEvaluationTotalCPUDurationForDocument, 0);
  tester.ExpectTotalCount(kEvaluationWallDuration, 0);
  tester.ExpectTotalCount(kEvaluationCPUDuration, 0);

  tester.ExpectTotalCount(kActivationWallDuration, 0);
  tester.ExpectTotalCount(kActivationCPUDuration, 0);

  tester.ExpectTotalCount(kSubresourceLoadsTotal, 0);
  tester.ExpectTotalCount(kSubresourceLoadsEvaluated, 0);
  tester.ExpectTotalCount(kSubresourceLoadsMatchedRules, 0);
  tester.ExpectTotalCount(kSubresourceLoadsDisallowed, 0);

  // Although SubresourceFilterAgents still record the activation decision.
  tester.ExpectUniqueSample(
      kDocumentLoadActivationLevel,
      static_cast<base::Histogram::Sample>(ActivationLevel::DISABLED), 6);
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       ActivationEnabledOnReloadByDefault) {
  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));
  ConfigureAsPhishingURL(url);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));

  base::HistogramTester tester;
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  content::TestNavigationObserver observer(
      browser()->tab_strip_model()->GetActiveWebContents(),
      content::MessageLoopRunner::QuitMode::DEFERRED);
  chrome::Reload(browser(), WindowOpenDisposition::CURRENT_TAB);
  observer.Wait();
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  tester.ExpectTotalCount(
      internal::kHistogramSubresourceFilterActivationDecision, 2);
  tester.ExpectBucketCount(
      internal::kHistogramSubresourceFilterActivationDecision,
      static_cast<int>(ActivationDecision::ACTIVATED), 2);

  tester.ExpectTotalCount(
      internal::kHistogramSubresourceFilterActivationDecisionReload, 1);
  tester.ExpectBucketCount(
      internal::kHistogramSubresourceFilterActivationDecisionReload,
      static_cast<int>(ActivationDecision::ACTIVATED), 1);
}

IN_PROC_BROWSER_TEST_F(SubresourceFilterBrowserTest,
                       WhiteliseSiteOnReload_ActivationDisabledOnReload) {
  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));
  ConfigureAsPhishingURL(url);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ResetConfigurationToEnableOnPhishingSites(
      false /* measure_performance */, true /* whitelist_site_on_reload */);
  base::HistogramTester tester;
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  content::TestNavigationObserver observer(
      browser()->tab_strip_model()->GetActiveWebContents(),
      content::MessageLoopRunner::QuitMode::DEFERRED);
  chrome::Reload(browser(), WindowOpenDisposition::CURRENT_TAB);
  observer.Wait();
  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  tester.ExpectTotalCount(
      internal::kHistogramSubresourceFilterActivationDecision, 2);
  tester.ExpectBucketCount(
      internal::kHistogramSubresourceFilterActivationDecision,
      static_cast<int>(ActivationDecision::ACTIVATED), 1);
  tester.ExpectBucketCount(
      internal::kHistogramSubresourceFilterActivationDecision,
      static_cast<int>(ActivationDecision::URL_WHITELISTED), 1);

  tester.ExpectTotalCount(
      internal::kHistogramSubresourceFilterActivationDecisionReload, 1);
  tester.ExpectBucketCount(
      internal::kHistogramSubresourceFilterActivationDecisionReload,
      static_cast<int>(ActivationDecision::URL_WHITELISTED), 1);
}

IN_PROC_BROWSER_TEST_F(
    SubresourceFilterBrowserTest,
    WhiteliseSiteOnReload_ActivationDisabledOnReloadFromScript) {
  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));
  ConfigureAsPhishingURL(url);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ResetConfigurationToEnableOnPhishingSites(
      false /* measure_performance */, true /* whitelist_site_on_reload */);
  base::HistogramTester tester;
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  content::TestNavigationObserver observer(
      browser()->tab_strip_model()->GetActiveWebContents(),
      content::MessageLoopRunner::QuitMode::DEFERRED);
  ASSERT_TRUE(content::ExecuteScript(
      browser()->tab_strip_model()->GetActiveWebContents(),
      "location.reload();"));
  observer.Wait();
  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  tester.ExpectTotalCount(
      internal::kHistogramSubresourceFilterActivationDecision, 2);
  tester.ExpectBucketCount(
      internal::kHistogramSubresourceFilterActivationDecision,
      static_cast<int>(ActivationDecision::ACTIVATED), 1);
  tester.ExpectBucketCount(
      internal::kHistogramSubresourceFilterActivationDecision,
      static_cast<int>(ActivationDecision::URL_WHITELISTED), 1);

  tester.ExpectTotalCount(
      internal::kHistogramSubresourceFilterActivationDecisionReload, 1);
  tester.ExpectBucketCount(
      internal::kHistogramSubresourceFilterActivationDecisionReload,
      static_cast<int>(ActivationDecision::URL_WHITELISTED), 1);
}

IN_PROC_BROWSER_TEST_F(
    SubresourceFilterBrowserTest,
    WhiteliseSiteOnReload_ActivationDisabledOnNavigationToSameURL) {
  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));
  ConfigureAsPhishingURL(url);
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  ResetConfigurationToEnableOnPhishingSites(
      false /* measure_performance */, true /* whitelist_site_on_reload */);
  base::HistogramTester tester;
  ui_test_utils::NavigateToURL(browser(), url);
  EXPECT_FALSE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  content::TestNavigationObserver observer(
      browser()->tab_strip_model()->GetActiveWebContents(),
      content::MessageLoopRunner::QuitMode::DEFERRED);
  std::string nav_frame_script = "location.href = '" + url.spec() + "';";
  ASSERT_TRUE(content::ExecuteScript(
      browser()->tab_strip_model()->GetActiveWebContents(), nav_frame_script));
  observer.Wait();
  EXPECT_TRUE(WasParsedScriptElementLoaded(web_contents()->GetMainFrame()));

  tester.ExpectTotalCount(
      internal::kHistogramSubresourceFilterActivationDecision, 2);
  tester.ExpectBucketCount(
      internal::kHistogramSubresourceFilterActivationDecision,
      static_cast<int>(ActivationDecision::ACTIVATED), 1);
  tester.ExpectBucketCount(
      internal::kHistogramSubresourceFilterActivationDecision,
      static_cast<int>(ActivationDecision::URL_WHITELISTED), 1);

  tester.ExpectTotalCount(
      internal::kHistogramSubresourceFilterActivationDecisionReload, 1);
  tester.ExpectBucketCount(
      internal::kHistogramSubresourceFilterActivationDecisionReload,
      static_cast<int>(ActivationDecision::URL_WHITELISTED), 1);
}

#if defined(GOOGLE_CHROME_BUILD)
// These tests are only enabled when GOOGLE_CHROME_BUILD is true because the
// store that this test uses is only populated on GOOGLE_CHROME_BUILD builds.
IN_PROC_BROWSER_TEST_F(
    SubresourceFilterBrowserTest,
    ExpectRedirectPatternHistogramsAreRecordedForSubresourceFilterOnlyMatch) {
  ASSERT_NO_FATAL_FAILURE(SetRulesetToDisallowURLsWithPathSuffix(
      "suffix-that-does-not-match-anything"));

  GURL url(GetTestUrl("subresource_filter/frame_with_included_script.html"));
  ConfigureAsSubresourceFilterOnlyURL(url);

  base::HistogramTester tester;
  ui_test_utils::NavigateToURL(browser(), url);

  tester.ExpectUniqueSample(
      std::string(kMatchesPatternHistogramName) +
          std::string(kSocialEngineeringAdsInterstitialSuffix),
      false, 1);
  tester.ExpectUniqueSample(std::string(kMatchesPatternHistogramName) +
                                std::string(kSubresourceFilterOnlySuffix),
                            true, 1);

  tester.ExpectUniqueSample(std::string(kMatchesPatternHistogramName) +
                                std::string(kPhishingInterstitialSuffix),
                            false, 1);
  EXPECT_THAT(tester.GetAllSamples(std::string(kNavigationChainSize) +
                                   std::string(kSubresourceFilterOnlySuffix)),
              ::testing::ElementsAre(base::Bucket(1, 1)));
}

IN_PROC_BROWSER_TEST_F(
    SubresourceFilterBrowserTest,
    ExpectRedirectPatternHistogramsAreRecordedForSubresourceFilterOnlyRedirectMatch) {
  ASSERT_NO_FATAL_FAILURE(
      SetRulesetToDisallowURLsWithPathSuffix("included_script.js"));
  const std::string initial_host("a.com");
  const std::string redirected_host("b.com");

  GURL redirect_url(embedded_test_server()->GetURL(
      redirected_host, "/subresource_filter/frame_with_included_script.html"));
  GURL url(embedded_test_server()->GetURL(
      initial_host, "/server-redirect?" + redirect_url.spec()));

  ConfigureAsSubresourceFilterOnlyURL(url.GetOrigin());
  base::HistogramTester tester;
  ui_test_utils::NavigateToURL(browser(), url);
  tester.ExpectUniqueSample(
      std::string(kMatchesPatternHistogramName) +
          std::string(kSocialEngineeringAdsInterstitialSuffix),
      false, 1);
  tester.ExpectUniqueSample(std::string(kMatchesPatternHistogramName) +
                                std::string(kSubresourceFilterOnlySuffix),
                            false, 1);

  tester.ExpectUniqueSample(std::string(kMatchesPatternHistogramName) +
                                std::string(kPhishingInterstitialSuffix),
                            false, 1);
}
#endif

}  // namespace subresource_filter
