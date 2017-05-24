// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search/search_ipc_router.h"

#include <stdint.h>

#include <memory>
#include <tuple>
#include <vector>

#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/search/search_ipc_router_policy_impl.h"
#include "chrome/browser/ui/search/search_tab_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/search/instant_types.h"
#include "chrome/common/search/mock_searchbox.h"
#include "chrome/common/search/ntp_logging_events.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/search_test_utils.h"
#include "components/omnibox/common/omnibox_focus_state.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/mock_render_process_host.h"
#include "ipc/ipc_message.h"
#include "ipc/ipc_message_start.h"
#include "ipc/ipc_test_sink.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

using testing::_;
using testing::Return;

namespace {

class MockSearchIPCRouterDelegate : public SearchIPCRouter::Delegate {
 public:
  virtual ~MockSearchIPCRouterDelegate() {}

  MOCK_METHOD1(FocusOmnibox, void(OmniboxFocusState state));
  MOCK_METHOD1(OnDeleteMostVisitedItem, void(const GURL& url));
  MOCK_METHOD1(OnUndoMostVisitedDeletion, void(const GURL& url));
  MOCK_METHOD0(OnUndoAllMostVisitedDeletions, void());
  MOCK_METHOD2(OnLogEvent, void(NTPLoggingEventType event,
                                base::TimeDelta time));
  MOCK_METHOD3(OnLogMostVisitedImpression,
               void(int position,
                    ntp_tiles::TileSource tile_source,
                    ntp_tiles::TileVisualType tile_type));
  MOCK_METHOD3(OnLogMostVisitedNavigation,
               void(int position,
                    ntp_tiles::TileSource tile_source,
                    ntp_tiles::TileVisualType tile_type));
  MOCK_METHOD1(PasteIntoOmnibox, void(const base::string16&));
  MOCK_METHOD1(OnChromeIdentityCheck, void(const base::string16& identity));
  MOCK_METHOD0(OnHistorySyncCheck, void());
};

class MockSearchIPCRouterPolicy : public SearchIPCRouter::Policy {
 public:
  virtual ~MockSearchIPCRouterPolicy() {}

  MOCK_METHOD1(ShouldProcessFocusOmnibox, bool(bool));
  MOCK_METHOD0(ShouldProcessDeleteMostVisitedItem, bool());
  MOCK_METHOD0(ShouldProcessUndoMostVisitedDeletion, bool());
  MOCK_METHOD0(ShouldProcessUndoAllMostVisitedDeletions, bool());
  MOCK_METHOD0(ShouldProcessLogEvent, bool());
  MOCK_METHOD1(ShouldProcessPasteIntoOmnibox, bool(bool));
  MOCK_METHOD0(ShouldProcessChromeIdentityCheck, bool());
  MOCK_METHOD0(ShouldProcessHistorySyncCheck, bool());
  MOCK_METHOD0(ShouldSendSetSuggestionToPrefetch, bool());
  MOCK_METHOD1(ShouldSendSetInputInProgress, bool(bool));
  MOCK_METHOD0(ShouldSendOmniboxFocusChanged, bool());
  MOCK_METHOD0(ShouldSendMostVisitedItems, bool());
  MOCK_METHOD0(ShouldSendThemeBackgroundInfo, bool());
  MOCK_METHOD0(ShouldSubmitQuery, bool());
};

class MockSearchBoxClientFactory
    : public SearchIPCRouter::SearchBoxClientFactory {
 public:
  MOCK_METHOD0(GetSearchBox, chrome::mojom::SearchBox*(void));
};

}  // namespace

class SearchIPCRouterTest : public BrowserWithTestWindowTest {
 public:
  SearchIPCRouterTest() : field_trial_list_(NULL) {}

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    AddTab(browser(), GURL("chrome://blank"));
    SearchTabHelper::CreateForWebContents(web_contents());

    TemplateURLServiceFactory::GetInstance()->SetTestingFactoryAndUse(
        profile(),
        &TemplateURLServiceFactory::BuildInstanceFor);
    TemplateURLService* template_url_service =
        TemplateURLServiceFactory::GetForProfile(profile());
    search_test_utils::WaitForTemplateURLServiceToLoad(template_url_service);

    TemplateURLData data;
    data.SetShortName(base::ASCIIToUTF16("foo.com"));
    data.SetURL("http://foo.com/url?bar={searchTerms}");
    data.instant_url = "http://foo.com/instant?foo=foo#foo=foo&espv";
    data.new_tab_url = "https://foo.com/newtab?espv";
    data.alternate_urls.push_back("http://foo.com/alt#quux={searchTerms}");
    data.search_terms_replacement_key = "espv";

    TemplateURL* template_url =
        template_url_service->Add(base::MakeUnique<TemplateURL>(data));
    template_url_service->SetUserSelectedDefaultSearchProvider(template_url);
  }

  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  SearchTabHelper* GetSearchTabHelper(
      content::WebContents* web_contents) {
    EXPECT_NE(static_cast<content::WebContents*>(NULL), web_contents);
    return SearchTabHelper::FromWebContents(web_contents);
  }

  void SetupMockDelegateAndPolicy() {
    content::WebContents* contents = web_contents();
    ASSERT_NE(static_cast<content::WebContents*>(NULL), contents);
    SearchTabHelper* search_tab_helper = GetSearchTabHelper(contents);
    ASSERT_NE(static_cast<SearchTabHelper*>(NULL), search_tab_helper);
    search_tab_helper->ipc_router_for_testing().set_delegate_for_testing(
        mock_delegate());
    search_tab_helper->ipc_router_for_testing().set_policy_for_testing(
        base::WrapUnique(new MockSearchIPCRouterPolicy));
    auto factory = base::MakeUnique<MockSearchBoxClientFactory>();
    ON_CALL(*factory, GetSearchBox()).WillByDefault(Return(&mock_search_box_));
    GetSearchIPCRouter().set_search_box_client_factory_for_testing(
        std::move(factory));
  }

  MockSearchIPCRouterDelegate* mock_delegate() { return &delegate_; }

  MockSearchIPCRouterPolicy* GetSearchIPCRouterPolicy() {
    content::WebContents* contents = web_contents();
    EXPECT_NE(static_cast<content::WebContents*>(NULL), contents);
    SearchTabHelper* search_tab_helper = GetSearchTabHelper(contents);
    EXPECT_NE(static_cast<SearchTabHelper*>(NULL), search_tab_helper);
    return static_cast<MockSearchIPCRouterPolicy*>(
        search_tab_helper->ipc_router_for_testing().policy_for_testing());
  }

  SearchIPCRouter& GetSearchIPCRouter() {
    return GetSearchTabHelper(web_contents())->ipc_router_for_testing();
  }

  int GetSearchIPCRouterSeqNo() {
    return GetSearchIPCRouter().page_seq_no_for_testing();
  }

  bool IsActiveTab(content::WebContents* contents) {
    return GetSearchTabHelper(contents)
        ->ipc_router_for_testing()
        .is_active_tab_;
  }

  MockSearchBox* mock_search_box() { return &mock_search_box_; }

 private:
  MockSearchIPCRouterDelegate delegate_;
  base::FieldTrialList field_trial_list_;
  MockSearchBox mock_search_box_;
};

TEST_F(SearchIPCRouterTest, ProcessFocusOmniboxMsg) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*mock_delegate(), FocusOmnibox(OMNIBOX_FOCUS_VISIBLE)).Times(1);

  content::WebContents* contents = web_contents();
  bool is_active_tab = IsActiveTab(contents);
  EXPECT_TRUE(is_active_tab);
  EXPECT_CALL(*policy, ShouldProcessFocusOmnibox(is_active_tab))
      .Times(1)
      .WillOnce(Return(true));

  GetSearchIPCRouter().FocusOmnibox(GetSearchIPCRouterSeqNo(),
                                    OMNIBOX_FOCUS_VISIBLE);
}

TEST_F(SearchIPCRouterTest, IgnoreFocusOmniboxMsg) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*mock_delegate(), FocusOmnibox(OMNIBOX_FOCUS_VISIBLE)).Times(0);

  content::WebContents* contents = web_contents();
  bool is_active_tab = IsActiveTab(contents);
  EXPECT_TRUE(is_active_tab);
  EXPECT_CALL(*policy, ShouldProcessFocusOmnibox(is_active_tab))
      .Times(1)
      .WillOnce(Return(false));

  GetSearchIPCRouter().FocusOmnibox(GetSearchIPCRouterSeqNo(),
                                    OMNIBOX_FOCUS_VISIBLE);
}

TEST_F(SearchIPCRouterTest, HandleTabChangedEvents) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  content::WebContents* contents = web_contents();
  EXPECT_EQ(0, browser()->tab_strip_model()->GetIndexOfWebContents(contents));
  EXPECT_TRUE(IsActiveTab(contents));

  // Add a new tab to deactivate the current tab.
  AddTab(browser(), GURL(url::kAboutBlankURL));
  EXPECT_EQ(2, browser()->tab_strip_model()->count());
  EXPECT_EQ(1, browser()->tab_strip_model()->GetIndexOfWebContents(contents));
  EXPECT_EQ(0, browser()->tab_strip_model()->active_index());
  EXPECT_FALSE(IsActiveTab(contents));

  // Activate the first tab.
  browser()->tab_strip_model()->ActivateTabAt(1, false);
  EXPECT_EQ(browser()->tab_strip_model()->active_index(),
            browser()->tab_strip_model()->GetIndexOfWebContents(contents));
  EXPECT_TRUE(IsActiveTab(contents));
}

TEST_F(SearchIPCRouterTest, ProcessLogEventMsg) {
  base::TimeDelta delta = base::TimeDelta::FromMilliseconds(123);
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*mock_delegate(), OnLogEvent(NTP_ALL_TILES_LOADED, delta))
      .Times(1);
  EXPECT_CALL(*policy, ShouldProcessLogEvent()).Times(1).WillOnce(Return(true));

  GetSearchIPCRouter().LogEvent(GetSearchIPCRouterSeqNo(), NTP_ALL_TILES_LOADED,
                                delta);
}

TEST_F(SearchIPCRouterTest, IgnoreLogEventMsg) {
  base::TimeDelta delta = base::TimeDelta::FromMilliseconds(123);
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*mock_delegate(), OnLogEvent(NTP_ALL_TILES_LOADED, delta))
      .Times(0);
  EXPECT_CALL(*policy, ShouldProcessLogEvent())
      .Times(1)
      .WillOnce(Return(false));

  GetSearchIPCRouter().LogEvent(GetSearchIPCRouterSeqNo(), NTP_ALL_TILES_LOADED,
                                delta);
}

TEST_F(SearchIPCRouterTest, ProcessLogMostVisitedImpressionMsg) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(
      *mock_delegate(),
      OnLogMostVisitedImpression(3, ntp_tiles::TileSource::SUGGESTIONS_SERVICE,
                                 ntp_tiles::TileVisualType::THUMBNAIL))
      .Times(1);
  EXPECT_CALL(*policy, ShouldProcessLogEvent()).Times(1).WillOnce(Return(true));

  GetSearchIPCRouter().LogMostVisitedImpression(
      GetSearchIPCRouterSeqNo(), 3, ntp_tiles::TileSource::SUGGESTIONS_SERVICE,
      ntp_tiles::TileVisualType::THUMBNAIL);
}

TEST_F(SearchIPCRouterTest, ProcessLogMostVisitedNavigationMsg) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(
      *mock_delegate(),
      OnLogMostVisitedNavigation(3, ntp_tiles::TileSource::SUGGESTIONS_SERVICE,
                                 ntp_tiles::TileVisualType::THUMBNAIL))
      .Times(1);
  EXPECT_CALL(*policy, ShouldProcessLogEvent()).Times(1).WillOnce(Return(true));

  GetSearchIPCRouter().LogMostVisitedNavigation(
      GetSearchIPCRouterSeqNo(), 3, ntp_tiles::TileSource::SUGGESTIONS_SERVICE,
      ntp_tiles::TileVisualType::THUMBNAIL);
}

TEST_F(SearchIPCRouterTest, ProcessChromeIdentityCheckMsg) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  const base::string16 test_identity = base::ASCIIToUTF16("foo@bar.com");
  EXPECT_CALL(*mock_delegate(), OnChromeIdentityCheck(test_identity)).Times(1);
  EXPECT_CALL(*policy, ShouldProcessChromeIdentityCheck())
      .Times(1)
      .WillOnce(Return(true));

  GetSearchIPCRouter().ChromeIdentityCheck(GetSearchIPCRouterSeqNo(),
                                           test_identity);
}

TEST_F(SearchIPCRouterTest, IgnoreChromeIdentityCheckMsg) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();

  const base::string16 test_identity = base::ASCIIToUTF16("foo@bar.com");
  EXPECT_CALL(*mock_delegate(), OnChromeIdentityCheck(test_identity)).Times(0);
  EXPECT_CALL(*policy, ShouldProcessChromeIdentityCheck())
      .Times(1)
      .WillOnce(Return(false));

  GetSearchIPCRouter().ChromeIdentityCheck(GetSearchIPCRouterSeqNo(),
                                           test_identity);
}

TEST_F(SearchIPCRouterTest, ProcessHistorySyncCheckMsg) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*mock_delegate(), OnHistorySyncCheck()).Times(1);
  EXPECT_CALL(*policy, ShouldProcessHistorySyncCheck())
      .Times(1)
      .WillOnce(Return(true));

  GetSearchIPCRouter().HistorySyncCheck(GetSearchIPCRouterSeqNo());
}

TEST_F(SearchIPCRouterTest, IgnoreHistorySyncCheckMsg) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();

  EXPECT_CALL(*mock_delegate(), OnHistorySyncCheck()).Times(0);
  EXPECT_CALL(*policy, ShouldProcessHistorySyncCheck())
      .Times(1)
      .WillOnce(Return(false));

  GetSearchIPCRouter().HistorySyncCheck(GetSearchIPCRouterSeqNo());
}

TEST_F(SearchIPCRouterTest, ProcessDeleteMostVisitedItemMsg) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  GURL item_url("www.foo.com");
  EXPECT_CALL(*mock_delegate(), OnDeleteMostVisitedItem(item_url)).Times(1);
  EXPECT_CALL(*policy, ShouldProcessDeleteMostVisitedItem())
      .Times(1)
      .WillOnce(Return(true));

  GetSearchIPCRouter().DeleteMostVisitedItem(GetSearchIPCRouterSeqNo(),
                                             item_url);
}

TEST_F(SearchIPCRouterTest, IgnoreDeleteMostVisitedItemMsg) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  GURL item_url("www.foo.com");
  EXPECT_CALL(*mock_delegate(), OnDeleteMostVisitedItem(item_url)).Times(0);
  EXPECT_CALL(*policy, ShouldProcessDeleteMostVisitedItem())
      .Times(1)
      .WillOnce(Return(false));

  GetSearchIPCRouter().DeleteMostVisitedItem(GetSearchIPCRouterSeqNo(),
                                             item_url);
}

TEST_F(SearchIPCRouterTest, ProcessUndoMostVisitedDeletionMsg) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  GURL item_url("www.foo.com");
  EXPECT_CALL(*mock_delegate(), OnUndoMostVisitedDeletion(item_url)).Times(1);
  EXPECT_CALL(*policy, ShouldProcessUndoMostVisitedDeletion())
      .Times(1)
      .WillOnce(Return(true));

  GetSearchIPCRouter().UndoMostVisitedDeletion(GetSearchIPCRouterSeqNo(),
                                               item_url);
}

TEST_F(SearchIPCRouterTest, IgnoreUndoMostVisitedDeletionMsg) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  GURL item_url("www.foo.com");
  EXPECT_CALL(*mock_delegate(), OnUndoMostVisitedDeletion(item_url)).Times(0);
  EXPECT_CALL(*policy, ShouldProcessUndoMostVisitedDeletion())
      .Times(1)
      .WillOnce(Return(false));

  GetSearchIPCRouter().UndoMostVisitedDeletion(GetSearchIPCRouterSeqNo(),
                                               item_url);
}

TEST_F(SearchIPCRouterTest, ProcessUndoAllMostVisitedDeletionsMsg) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*mock_delegate(), OnUndoAllMostVisitedDeletions()).Times(1);
  EXPECT_CALL(*policy, ShouldProcessUndoAllMostVisitedDeletions())
      .Times(1)
      .WillOnce(Return(true));

  GetSearchIPCRouter().UndoAllMostVisitedDeletions(GetSearchIPCRouterSeqNo());
}

TEST_F(SearchIPCRouterTest, IgnoreUndoAllMostVisitedDeletionsMsg) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*mock_delegate(), OnUndoAllMostVisitedDeletions()).Times(0);
  EXPECT_CALL(*policy, ShouldProcessUndoAllMostVisitedDeletions())
      .Times(1)
      .WillOnce(Return(false));

  GetSearchIPCRouter().UndoAllMostVisitedDeletions(GetSearchIPCRouterSeqNo());
}

TEST_F(SearchIPCRouterTest, ProcessPasteAndOpenDropdownMsg) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();

  content::WebContents* contents = web_contents();
  bool is_active_tab = IsActiveTab(contents);
  EXPECT_TRUE(is_active_tab);

  base::string16 text;
  EXPECT_CALL(*mock_delegate(), PasteIntoOmnibox(text)).Times(1);
  EXPECT_CALL(*policy, ShouldProcessPasteIntoOmnibox(is_active_tab))
      .Times(1)
      .WillOnce(Return(true));
  GetSearchIPCRouter().PasteAndOpenDropdown(GetSearchIPCRouterSeqNo(), text);
}

TEST_F(SearchIPCRouterTest, IgnorePasteAndOpenDropdownMsg) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  base::string16 text;
  EXPECT_CALL(*mock_delegate(), PasteIntoOmnibox(text)).Times(0);

  content::WebContents* contents = web_contents();
  bool is_active_tab = IsActiveTab(contents);
  EXPECT_TRUE(is_active_tab);

  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldProcessPasteIntoOmnibox(is_active_tab))
      .Times(1)
      .WillOnce(Return(false));

  GetSearchIPCRouter().PasteAndOpenDropdown(GetSearchIPCRouterSeqNo(), text);
}

TEST_F(SearchIPCRouterTest, SendSetSuggestionToPrefetch) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldSendSetSuggestionToPrefetch())
      .Times(1)
      .WillOnce(Return(true));

  content::WebContents* contents = web_contents();
  EXPECT_CALL(*mock_search_box(), SetSuggestionToPrefetch(_));
  GetSearchTabHelper(contents)->SetSuggestionToPrefetch(InstantSuggestion());
}

TEST_F(SearchIPCRouterTest, DoNotSendSetSuggestionToPrefetch) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldSendSetSuggestionToPrefetch())
      .Times(1)
      .WillOnce(Return(false));

  content::WebContents* contents = web_contents();
  EXPECT_CALL(*mock_search_box(), SetSuggestionToPrefetch(_)).Times(0);
  GetSearchTabHelper(contents)->SetSuggestionToPrefetch(InstantSuggestion());
}

TEST_F(SearchIPCRouterTest, SendOmniboxFocusChange) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldSendOmniboxFocusChanged())
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_search_box(), FocusChanged(_, _));
  GetSearchIPCRouter().OmniboxFocusChanged(OMNIBOX_FOCUS_NONE,
                                           OMNIBOX_FOCUS_CHANGE_EXPLICIT);
}

TEST_F(SearchIPCRouterTest, DoNotSendOmniboxFocusChange) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldSendOmniboxFocusChanged())
      .Times(1)
      .WillOnce(Return(false));

  EXPECT_CALL(*mock_search_box(), FocusChanged(_, _)).Times(0);
  GetSearchIPCRouter().OmniboxFocusChanged(OMNIBOX_FOCUS_NONE,
                                           OMNIBOX_FOCUS_CHANGE_EXPLICIT);
}

TEST_F(SearchIPCRouterTest, SendSetInputInProgress) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldSendSetInputInProgress(true))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_search_box(), SetInputInProgress(_));
  GetSearchIPCRouter().SetInputInProgress(true);
}

TEST_F(SearchIPCRouterTest, DoNotSendSetInputInProgress) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldSendSetInputInProgress(true))
      .Times(1)
      .WillOnce(Return(false));

  EXPECT_CALL(*mock_search_box(), SetInputInProgress(_)).Times(0);
  GetSearchIPCRouter().SetInputInProgress(true);
}

TEST_F(SearchIPCRouterTest, SendMostVisitedItemsMsg) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldSendMostVisitedItems())
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_search_box(), MostVisitedChanged(_));
  GetSearchIPCRouter().SendMostVisitedItems(
      std::vector<InstantMostVisitedItem>());
}

TEST_F(SearchIPCRouterTest, DoNotSendMostVisitedItemsMsg) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldSendMostVisitedItems())
      .Times(1)
      .WillOnce(Return(false));

  EXPECT_CALL(*mock_search_box(), MostVisitedChanged(_)).Times(0);
  GetSearchIPCRouter().SendMostVisitedItems(
      std::vector<InstantMostVisitedItem>());
}

TEST_F(SearchIPCRouterTest, SendThemeBackgroundInfoMsg) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldSendThemeBackgroundInfo())
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_search_box(), ThemeChanged(_));
  GetSearchIPCRouter().SendThemeBackgroundInfo(ThemeBackgroundInfo());
}

TEST_F(SearchIPCRouterTest, DoNotSendThemeBackgroundInfoMsg) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldSendThemeBackgroundInfo())
      .Times(1)
      .WillOnce(Return(false));

  EXPECT_CALL(*mock_search_box(), ThemeChanged(_)).Times(0);
  GetSearchIPCRouter().SendThemeBackgroundInfo(ThemeBackgroundInfo());
}

TEST_F(SearchIPCRouterTest, SendSubmitMsg) {
  NavigateAndCommitActiveTab(GURL("chrome-search://foo/bar"));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldSubmitQuery()).Times(1).WillOnce(Return(true));

  EXPECT_CALL(*mock_search_box(), Submit(_));
  GetSearchIPCRouter().Submit(EmbeddedSearchRequestParams());
}

TEST_F(SearchIPCRouterTest, DoNotSendSubmitMsg) {
  NavigateAndCommitActiveTab(GURL(chrome::kChromeSearchLocalNtpUrl));
  SetupMockDelegateAndPolicy();
  MockSearchIPCRouterPolicy* policy = GetSearchIPCRouterPolicy();
  EXPECT_CALL(*policy, ShouldSubmitQuery()).Times(1).WillOnce(Return(false));

  EXPECT_CALL(*mock_search_box(), Submit(_)).Times(0);
  GetSearchIPCRouter().Submit(EmbeddedSearchRequestParams());
}
