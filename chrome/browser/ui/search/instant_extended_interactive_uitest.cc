// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <sstream>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram_base.h"
#include "base/metrics/histogram_samples.h"
#include "base/metrics/statistics_recorder.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/instant_service.h"
#include "chrome/browser/search/instant_service_factory.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/search/instant_tab.h"
#include "chrome/browser/ui/search/instant_test_utils.h"
#include "chrome/browser/ui/search/instant_uitest_base.h"
#include "chrome/browser/ui/search/search_tab_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/theme_source.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/search/instant_types.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/bookmarks/browser/bookmark_utils.h"
#include "components/bookmarks/test/bookmark_test_helpers.h"
#include "components/history/core/browser/history_db_task.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/top_sites.h"
#include "components/history/core/common/thumbnail_score.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/omnibox/browser/omnibox_edit_model.h"
#include "components/omnibox/browser/omnibox_field_trial.h"
#include "components/omnibox/browser/search_provider.h"
#include "components/prefs/pref_service.h"
#include "components/search/search.h"
#include "components/search_engines/template_url_service.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "net/base/network_change_notifier.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_fetcher_impl.h"
#include "net/url_request/url_request_status.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/skia/include/core/SkBitmap.h"

using base::ASCIIToUTF16;
using testing::HasSubstr;

namespace {

// Task used to make sure history has finished processing a request. Intended
// for use with BlockUntilHistoryProcessesPendingRequests.
class QuittingHistoryDBTask : public history::HistoryDBTask {
 public:
  QuittingHistoryDBTask() {}

  bool RunOnDBThread(history::HistoryBackend* backend,
                     history::HistoryDatabase* db) override {
    return true;
  }

  void DoneRunOnMainThread() override {
    base::MessageLoop::current()->QuitWhenIdle();
  }

 private:
  ~QuittingHistoryDBTask() override {}

  DISALLOW_COPY_AND_ASSIGN(QuittingHistoryDBTask);
};

class FakeNetworkChangeNotifier : public net::NetworkChangeNotifier {
 public:
  FakeNetworkChangeNotifier() : connection_type_(CONNECTION_NONE) {}

  ConnectionType GetCurrentConnectionType() const override {
    return connection_type_;
  }

  void SetConnectionType(ConnectionType type) {
    connection_type_ = type;
    NotifyObserversOfNetworkChange(type);
    base::RunLoop().RunUntilIdle();
  }

  ~FakeNetworkChangeNotifier() override {}

 private:
  ConnectionType connection_type_;
  DISALLOW_COPY_AND_ASSIGN(FakeNetworkChangeNotifier);
};
}  // namespace

class InstantExtendedTest : public InProcessBrowserTest,
                            public InstantUITestBase {
 public:
  InstantExtendedTest()
      : on_most_visited_change_calls_(0),
        most_visited_items_count_(0),
        first_most_visited_item_id_(0),
        submit_count_(0),
        on_focus_changed_calls_(0),
        is_focused_(false) {}

 protected:
  void SetUpInProcessBrowserTestFixture() override {
    ASSERT_TRUE(https_test_server().Start());
    GURL instant_url =
        https_test_server().GetURL("/instant_extended.html?strk=1&");
    GURL ntp_url =
        https_test_server().GetURL("/instant_extended_ntp.html?strk=1&");
    InstantTestBase::Init(instant_url, ntp_url, false);
  }

  int64_t GetHistogramCount(const char* name) {
    base::HistogramBase* histogram =
        base::StatisticsRecorder::FindHistogram(name);
    if (!histogram) {
      // If no histogram is found, it's possible that no values have been
      // recorded yet. Assume that the value is zero.
      return 0;
    }
    return histogram->SnapshotSamples()->TotalCount();
  }

  bool UpdateSearchState(content::WebContents* contents) WARN_UNUSED_RESULT {
    return instant_test_utils::GetIntFromJS(contents,
                                            "onMostVisitedChangedCalls",
                                            &on_most_visited_change_calls_) &&
           instant_test_utils::GetIntFromJS(contents, "mostVisitedItemsCount",
                                            &most_visited_items_count_) &&
           instant_test_utils::GetIntFromJS(contents, "firstMostVisitedItemId",
                                            &first_most_visited_item_id_) &&
           instant_test_utils::GetIntFromJS(contents, "submitCount",
                                            &submit_count_) &&
           instant_test_utils::GetIntFromJS(contents, "onFocusChangedCalls",
                                            &on_focus_changed_calls_) &&
           instant_test_utils::GetBoolFromJS(contents, "isFocused",
                                             &is_focused_) &&
           instant_test_utils::GetStringFromJS(contents, "prefetchQuery",
                                               &prefetch_query_value_);
  }

  const TemplateURL* GetDefaultSearchProviderTemplateURL() {
    TemplateURLService* template_url_service =
        TemplateURLServiceFactory::GetForProfile(browser()->profile());
    if (template_url_service)
      return template_url_service->GetDefaultSearchProvider();
    return NULL;
  }

  bool AddSearchToHistory(base::string16 term, int visit_count) {
    const TemplateURL* template_url = GetDefaultSearchProviderTemplateURL();
    if (!template_url)
      return false;

    history::HistoryService* history = HistoryServiceFactory::GetForProfile(
        browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
    GURL search(template_url->url_ref().ReplaceSearchTerms(
        TemplateURLRef::SearchTermsArgs(term),
        TemplateURLServiceFactory::GetForProfile(
            browser()->profile())->search_terms_data()));
    history->AddPageWithDetails(
        search, base::string16(), visit_count, visit_count,
        base::Time::Now(), false, history::SOURCE_BROWSED);
    history->SetKeywordSearchTermsForURL(
        search, template_url->id(), term);
    return true;
  }

  void BlockUntilHistoryProcessesPendingRequests() {
    history::HistoryService* history = HistoryServiceFactory::GetForProfile(
        browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
    DCHECK(history);
    DCHECK(base::MessageLoop::current());

    base::CancelableTaskTracker tracker;
    history->ScheduleDBTask(
        std::unique_ptr<history::HistoryDBTask>(new QuittingHistoryDBTask()),
        &tracker);
    base::RunLoop().Run();
  }

  int CountSearchProviderSuggestions() {
    return omnibox()->model()->autocomplete_controller()->search_provider()->
        matches().size();
  }

  int on_most_visited_change_calls_;
  int most_visited_items_count_;
  int first_most_visited_item_id_;
  int submit_count_;
  int on_focus_changed_calls_;
  bool is_focused_;
  std::string prefetch_query_value_;
};

class InstantExtendedPrefetchTest : public InstantExtendedTest {
 public:
  InstantExtendedPrefetchTest()
      : factory_(new net::URLFetcherImplFactory()),
        fake_factory_(new net::FakeURLFetcherFactory(factory_.get())) {
  }

  void SetUpInProcessBrowserTestFixture() override {
    ASSERT_TRUE(https_test_server().Start());
    GURL instant_url =
        https_test_server().GetURL("/instant_extended.html?strk=1&");
    GURL ntp_url =
        https_test_server().GetURL("/instant_extended_ntp.html?strk=1&");
    InstantTestBase::Init(instant_url, ntp_url, true);
  }

  net::FakeURLFetcherFactory* fake_factory() { return fake_factory_.get(); }

 private:
  // Used to instantiate FakeURLFetcherFactory.
  std::unique_ptr<net::URLFetcherImplFactory> factory_;

  // Used to mock default search provider suggest response.
  std::unique_ptr<net::FakeURLFetcherFactory> fake_factory_;

  DISALLOW_COPY_AND_ASSIGN(InstantExtendedPrefetchTest);
};

// Test class used to verify chrome-search: scheme and access policy from the
// Instant overlay.  This is a subclass of |ExtensionBrowserTest| because it
// loads a theme that provides a background image.
class InstantPolicyTest : public ExtensionBrowserTest,
                          public InstantUITestBase {
 public:
  InstantPolicyTest() {}

 protected:
  void SetUpInProcessBrowserTestFixture() override {
    ASSERT_TRUE(https_test_server().Start());
    GURL instant_url =
        https_test_server().GetURL("/instant_extended.html?strk=1&");
    GURL ntp_url =
        https_test_server().GetURL("/instant_extended_ntp.html?strk=1&");
    InstantTestBase::Init(instant_url, ntp_url, false);
  }

  void InstallThemeSource() {
    ThemeSource* theme = new ThemeSource(profile());
    content::URLDataSource::Add(profile(), theme);
  }

  void InstallThemeAndVerify(const std::string& theme_dir,
                             const std::string& theme_name) {
    const extensions::Extension* theme =
        ThemeServiceFactory::GetThemeForProfile(
            ExtensionBrowserTest::browser()->profile());
    // If there is already a theme installed, the current theme should be
    // disabled and the new one installed + enabled.
    int expected_change = theme ? 0 : 1;

    const base::FilePath theme_path = test_data_dir_.AppendASCII(theme_dir);
    ASSERT_TRUE(InstallExtensionWithUIAutoConfirm(
        theme_path, expected_change, ExtensionBrowserTest::browser()));
    const extensions::Extension* new_theme =
        ThemeServiceFactory::GetThemeForProfile(
            ExtensionBrowserTest::browser()->profile());
    ASSERT_NE(static_cast<extensions::Extension*>(NULL), new_theme);
    ASSERT_EQ(new_theme->name(), theme_name);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(InstantPolicyTest);
};

// TODO(https://crbug.com/678975): Flaky on memory bot (all platforms).
IN_PROC_BROWSER_TEST_F(InstantExtendedTest,
                       DISABLED_SearchDoesntReuseInstantTab) {
  ASSERT_NO_FATAL_FAILURE(SetupInstant(browser()));
  FocusOmnibox();

  SetOmniboxText("flowers");
  PressEnterAndWaitForFrameLoad();

  // Just did a regular search.
  content::WebContents* active_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_THAT(active_tab->GetURL().spec(), HasSubstr("q=flowers"));
  ASSERT_TRUE(UpdateSearchState(active_tab));
  ASSERT_EQ(0, submit_count_);

  SetOmniboxText("puppies");
  PressEnterAndWaitForNavigation();

  // Should not have reused the tab.
  active_tab = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_THAT(active_tab->GetURL().spec(), HasSubstr("q=puppies"));
  ASSERT_TRUE(UpdateSearchState(active_tab));
  EXPECT_EQ(0, submit_count_);
}

IN_PROC_BROWSER_TEST_F(InstantExtendedTest,
                       SearchDoesntReuseInstantTabWithoutSupport) {
  ASSERT_NO_FATAL_FAILURE(SetupInstant(browser()));
  FocusOmnibox();

  // Don't wait for the navigation to complete.
  SetOmniboxText("flowers");
  browser()->window()->GetLocationBar()->AcceptInput();

  SetOmniboxText("puppies");
  browser()->window()->GetLocationBar()->AcceptInput();

  // Should not have reused the tab.
  ASSERT_THAT(
      browser()->tab_strip_model()->GetActiveWebContents()->GetURL().spec(),
      HasSubstr("q=puppies"));
}

IN_PROC_BROWSER_TEST_F(InstantExtendedTest,
                       TypedSearchURLDoesntReuseInstantTab) {
  ASSERT_NO_FATAL_FAILURE(SetupInstant(browser()));
  FocusOmnibox();

  SetOmniboxText("flowers");
  PressEnterAndWaitForFrameLoad();

  // Just did a regular search.
  content::WebContents* active_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_THAT(active_tab->GetURL().spec(), HasSubstr("q=flowers"));
  ASSERT_TRUE(UpdateSearchState(active_tab));
  ASSERT_EQ(0, submit_count_);

  // Typed in a search URL "by hand".
  SetOmniboxText(instant_url().Resolve("#q=puppies").spec());
  PressEnterAndWaitForNavigation();

  // Should not have reused the tab.
  active_tab = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_THAT(active_tab->GetURL().spec(), HasSubstr("q=puppies"));
}

// Test to verify that switching tabs should not dispatch onmostvisitedchanged
// events.
IN_PROC_BROWSER_TEST_F(InstantExtendedTest, NoMostVisitedChangedOnTabSwitch) {
  // Initialize Instant.
  ASSERT_NO_FATAL_FAILURE(SetupInstant(browser()));

  // Open new tab.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB |
          ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  EXPECT_EQ(2, browser()->tab_strip_model()->count());

  // Make sure new tab received the onmostvisitedchanged event once.
  content::WebContents* active_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(UpdateSearchState(active_tab));
  EXPECT_EQ(1, on_most_visited_change_calls_);

  // Activate the previous tab.
  browser()->tab_strip_model()->ActivateTabAt(0, false);

  // Switch back to new tab.
  browser()->tab_strip_model()->ActivateTabAt(1, false);

  // Confirm that new tab got no onmostvisitedchanged event.
  active_tab = browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(UpdateSearchState(active_tab));
  EXPECT_EQ(1, on_most_visited_change_calls_);
}

IN_PROC_BROWSER_TEST_F(InstantPolicyTest, ThemeBackgroundAccess) {
  InstallThemeSource();
  ASSERT_NO_FATAL_FAILURE(InstallThemeAndVerify("theme", "camo theme"));
  ASSERT_NO_FATAL_FAILURE(SetupInstant(browser()));

  // The "Instant" New Tab should have access to chrome-search: scheme but not
  // chrome: scheme.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB |
          ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  content::RenderViewHost* rvh =
      browser()->tab_strip_model()->GetActiveWebContents()->GetRenderViewHost();

  const std::string chrome_url("chrome://theme/IDR_THEME_NTP_BACKGROUND");
  const std::string search_url(
      "chrome-search://theme/IDR_THEME_NTP_BACKGROUND");
  bool loaded = false;
  ASSERT_TRUE(LoadImage(rvh, chrome_url, &loaded));
  EXPECT_FALSE(loaded) << chrome_url;
  ASSERT_TRUE(LoadImage(rvh, search_url, &loaded));
  EXPECT_TRUE(loaded) << search_url;
}

// Flaky on all bots. http://crbug.com/335297.
IN_PROC_BROWSER_TEST_F(InstantPolicyTest,
                       DISABLED_NoThemeBackgroundChangeEventOnTabSwitch) {
  InstallThemeSource();
  ASSERT_NO_FATAL_FAILURE(SetupInstant(browser()));

  // Install a theme.
  ASSERT_NO_FATAL_FAILURE(InstallThemeAndVerify("theme", "camo theme"));
  EXPECT_EQ(1, browser()->tab_strip_model()->count());

  // Open new tab.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB |
          ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  EXPECT_EQ(2, browser()->tab_strip_model()->count());

  content::WebContents* active_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_EQ(1, browser()->tab_strip_model()->active_index());
  int on_theme_changed_calls = 0;
  EXPECT_TRUE(instant_test_utils::GetIntFromJS(
      active_tab, "onThemeChangedCalls", &on_theme_changed_calls));
  EXPECT_EQ(1, on_theme_changed_calls);

  // Activate the previous tab.
  browser()->tab_strip_model()->ActivateTabAt(0, false);
  ASSERT_EQ(0, browser()->tab_strip_model()->active_index());

  // Switch back to new tab.
  browser()->tab_strip_model()->ActivateTabAt(1, false);
  ASSERT_EQ(1, browser()->tab_strip_model()->active_index());

  // Confirm that new tab got no onthemechanged event while switching tabs.
  active_tab = browser()->tab_strip_model()->GetActiveWebContents();
  on_theme_changed_calls = 0;
  EXPECT_TRUE(instant_test_utils::GetIntFromJS(
      active_tab, "onThemeChangedCalls", &on_theme_changed_calls));
  EXPECT_EQ(1, on_theme_changed_calls);
}

// Flaky on all bots. http://crbug.com/335297, http://crbug.com/265971.
IN_PROC_BROWSER_TEST_F(InstantPolicyTest,
                       DISABLED_SendThemeBackgroundChangedEvent) {
  InstallThemeSource();
  ASSERT_NO_FATAL_FAILURE(SetupInstant(browser()));

  // Install a theme.
  ASSERT_NO_FATAL_FAILURE(InstallThemeAndVerify("theme", "camo theme"));

  // Open new tab.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB |
          ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  EXPECT_EQ(2, browser()->tab_strip_model()->count());

  // Make sure new tab received an onthemechanged event.
  content::WebContents* active_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_EQ(1, browser()->tab_strip_model()->active_index());
  int on_theme_changed_calls = 0;
  EXPECT_TRUE(instant_test_utils::GetIntFromJS(
      active_tab, "onThemeChangedCalls", &on_theme_changed_calls));
  EXPECT_EQ(1, on_theme_changed_calls);

  // Install a new theme.
  ASSERT_NO_FATAL_FAILURE(InstallThemeAndVerify("theme2", "snowflake theme"));

  // Confirm that new tab is notified about the theme changed event.
  on_theme_changed_calls = 0;
  EXPECT_TRUE(instant_test_utils::GetIntFromJS(
      active_tab, "onThemeChangedCalls", &on_theme_changed_calls));
  EXPECT_EQ(2, on_theme_changed_calls);
}

// Flaky on all bots since re-enabled in r208032, crbug.com/253092
IN_PROC_BROWSER_TEST_F(InstantExtendedTest, DISABLED_NavigateBackToNTP) {
  ASSERT_NO_FATAL_FAILURE(SetupInstant(browser()));
  FocusOmnibox();

  // Open a new tab page.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB |
          ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  EXPECT_EQ(2, browser()->tab_strip_model()->count());

  SetOmniboxText("flowers");
  PressEnterAndWaitForNavigation();

  // Navigate back to NTP.
  content::WebContents* active_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(active_tab->GetController().CanGoBack());
  content::WindowedNotificationObserver load_stop_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<content::NavigationController>(
          &active_tab->GetController()));
  active_tab->GetController().GoBack();
  load_stop_observer.Wait();

  active_tab = browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(search::IsInstantNTP(active_tab));
}

// Flaky: crbug.com/267119
IN_PROC_BROWSER_TEST_F(InstantExtendedTest,
                       DISABLED_DispatchMVChangeEventWhileNavigatingBackToNTP) {
  // Setup Instant.
  ASSERT_NO_FATAL_FAILURE(SetupInstant(browser()));
  FocusOmnibox();

  // Open new tab.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB |
          ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  content::WebContents* active_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(UpdateSearchState(active_tab));
  EXPECT_EQ(1, on_most_visited_change_calls_);

  content::WindowedNotificationObserver observer(
      content::NOTIFICATION_LOAD_STOP,
      content::NotificationService::AllSources());
  // Set the text and press enter to navigate from NTP.
  SetOmniboxText("Pen");
  PressEnterAndWaitForNavigation();
  observer.Wait();

  // Navigate back to NTP.
  content::WindowedNotificationObserver back_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::NotificationService::AllSources());
  active_tab = browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(active_tab->GetController().CanGoBack());
  active_tab->GetController().GoBack();
  back_observer.Wait();

  // Verify that onmostvisitedchange event is dispatched when we navigate from
  // SRP to NTP.
  active_tab = browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(UpdateSearchState(active_tab));
  EXPECT_EQ(1, on_most_visited_change_calls_);
}

// http://crbug.com/518106
IN_PROC_BROWSER_TEST_F(InstantExtendedPrefetchTest, DISABLED_SetPrefetchQuery) {
  // Skip the test if suggest support is disabled, since this is generally due
  // to policy and can't be overridden.
  if (!browser()->profile()->GetPrefs()->GetBoolean(
      prefs::kSearchSuggestEnabled)) {
    return;
  }
  ASSERT_NO_FATAL_FAILURE(SetupInstant(browser()));
  FocusOmnibox();

  content::WindowedNotificationObserver new_tab_observer(
      content::NOTIFICATION_NAV_ENTRY_COMMITTED,
      content::NotificationService::AllSources());
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabURL),
      WindowOpenDisposition::CURRENT_TAB, ui_test_utils::BROWSER_TEST_NONE);
  new_tab_observer.Wait();

  OmniboxFieldTrial::kDefaultMinimumTimeBetweenSuggestQueriesMs = 0;

  // Set the fake response for search query.
  fake_factory()->SetFakeResponse(instant_url().Resolve("#q=flowers"),
                                  "",
                                  net::HTTP_OK,
                                  net::URLRequestStatus::SUCCESS);

  // Navigate to a search results page.
  SetOmniboxText("flowers");
  PressEnterAndWaitForNavigation();

  // Set the fake response for suggest request. Response has prefetch details.
  // Ensure that the page received the suggest response, then add another
  // keystroke to allow the asynchronously-received inline autocomplete
  // suggestion to actually be inlined (which in turn triggers it to prefetch).
  fake_factory()->SetFakeResponse(
      instant_url().Resolve("#q=pup"),
      "[\"pup\",[\"puppy\", \"puppies\"],[],[],"
      "{\"google:clientdata\":{\"phi\": 0},"
          "\"google:suggesttype\":[\"QUERY\", \"QUERY\"],"
          "\"google:suggestrelevance\":[1400, 9]}]",
      net::HTTP_OK,
      net::URLRequestStatus::SUCCESS);

  SetOmniboxText("pup");
  while (!omnibox()->model()->autocomplete_controller()->done()) {
    content::WindowedNotificationObserver ready_observer(
        chrome::NOTIFICATION_AUTOCOMPLETE_CONTROLLER_RESULT_READY,
        content::Source<AutocompleteController>(
            omnibox()->model()->autocomplete_controller()));
    ready_observer.Wait();
  }
  SetOmniboxText("pupp");

  ASSERT_EQ(3, CountSearchProviderSuggestions());
  content::WebContents* active_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(UpdateSearchState(active_tab));
  ASSERT_TRUE(SearchProvider::ShouldPrefetch(*(
      omnibox()->model()->result().default_match())));
  ASSERT_EQ("puppy", prefetch_query_value_);
}

// http://crbug.com/518106
IN_PROC_BROWSER_TEST_F(InstantExtendedPrefetchTest,
                       DISABLED_ClearPrefetchedResults) {
  // Skip the test if suggest support is disabled, since this is generally due
  // to policy and can't be overridden.
  if (!browser()->profile()->GetPrefs()->GetBoolean(
      prefs::kSearchSuggestEnabled)) {
    return;
  }
  ASSERT_NO_FATAL_FAILURE(SetupInstant(browser()));
  FocusOmnibox();

  content::WindowedNotificationObserver new_tab_observer(
      content::NOTIFICATION_NAV_ENTRY_COMMITTED,
      content::NotificationService::AllSources());
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabURL),
      WindowOpenDisposition::CURRENT_TAB, ui_test_utils::BROWSER_TEST_NONE);
  new_tab_observer.Wait();

  OmniboxFieldTrial::kDefaultMinimumTimeBetweenSuggestQueriesMs = 0;

  // Set the fake response for search query.
  fake_factory()->SetFakeResponse(instant_url().Resolve("#q=flowers"),
                                  "",
                                  net::HTTP_OK,
                                  net::URLRequestStatus::SUCCESS);

  // Navigate to a search results page.
  SetOmniboxText("flowers");
  PressEnterAndWaitForNavigation();

  // Set the fake response for suggest request. Response has no prefetch
  // details. Ensure that the page received a blank query to clear the
  // prefetched results.
  fake_factory()->SetFakeResponse(
      instant_url().Resolve("#q=dogs"),
      "[\"dogs\",[\"https://dogs.com\"],[],[],"
          "{\"google:suggesttype\":[\"NAVIGATION\"],"
          "\"google:suggestrelevance\":[2]}]",
      net::HTTP_OK,
      net::URLRequestStatus::SUCCESS);

  SetOmniboxText("dogs");
  while (!omnibox()->model()->autocomplete_controller()->done()) {
    content::WindowedNotificationObserver ready_observer(
        chrome::NOTIFICATION_AUTOCOMPLETE_CONTROLLER_RESULT_READY,
        content::Source<AutocompleteController>(
            omnibox()->model()->autocomplete_controller()));
    ready_observer.Wait();
  }

  ASSERT_EQ(2, CountSearchProviderSuggestions());
  ASSERT_FALSE(SearchProvider::ShouldPrefetch(*(
      omnibox()->model()->result().default_match())));
  content::WebContents* active_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(UpdateSearchState(active_tab));
  ASSERT_EQ("", prefetch_query_value_);
}

// Check that clicking on a result sends the correct referrer.
IN_PROC_BROWSER_TEST_F(InstantExtendedTest, Referrer) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL result_url = embedded_test_server()->GetURL(
      "/referrer_policy/referrer-policy-log.html");
  ASSERT_NO_FATAL_FAILURE(SetupInstant(browser()));
  FocusOmnibox();

  // Type a query and press enter to get results.
  SetOmniboxText("query");
  PressEnterAndWaitForFrameLoad();

  // Simulate going to a result.
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  std::ostringstream stream;
  stream << "var link = document.createElement('a');";
  stream << "link.href = \"" << result_url.spec() << "\";";
  stream << "document.body.appendChild(link);";
  stream << "link.click();";
  EXPECT_TRUE(content::ExecuteScript(contents, stream.str()));

  content::WaitForLoadStop(contents);
  std::string expected_title =
      "Referrer is " + instant_url().GetWithEmptyPath().spec();
  EXPECT_EQ(ASCIIToUTF16(expected_title), contents->GetTitle());
}
