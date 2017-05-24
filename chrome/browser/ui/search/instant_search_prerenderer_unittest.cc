// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search/instant_search_prerenderer.h"

#include <stdint.h>

#include <memory>
#include <tuple>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/prerender/prerender_contents.h"
#include "chrome/browser/prerender/prerender_handle.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "chrome/browser/prerender/prerender_origin.h"
#include "chrome/browser/prerender/prerender_tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/instant_service.h"
#include "chrome/browser/search/instant_unittest_base.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_instant_controller.h"
#include "chrome/browser/ui/search/search_ipc_router.h"
#include "chrome/browser/ui/search/search_tab_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/search/instant_types.h"
#include "chrome/common/search/mock_searchbox.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/mock_render_process_host.h"
#include "ipc/ipc_message.h"
#include "ipc/ipc_test_sink.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/gfx/geometry/size.h"

using base::ASCIIToUTF16;
using testing::_;
using testing::AllOf;
using testing::Eq;
using testing::Field;
using testing::Return;

namespace {

class MockSearchBoxClientFactory
    : public SearchIPCRouter::SearchBoxClientFactory {
 public:
  MOCK_METHOD0(GetSearchBox, chrome::mojom::SearchBox*(void));
};

using content::Referrer;
using prerender::Origin;
using prerender::PrerenderContents;
using prerender::PrerenderHandle;
using prerender::PrerenderManager;
using prerender::PrerenderManagerFactory;
using prerender::PrerenderTabHelper;

class DummyPrerenderContents : public PrerenderContents {
 public:
  DummyPrerenderContents(PrerenderManager* prerender_manager,
                         Profile* profile,
                         const GURL& url,
                         const Referrer& referrer,
                         Origin origin,
                         bool call_did_finish_load,
                         chrome::mojom::SearchBox* search_box);

  void StartPrerendering(
      const gfx::Rect& bounds,
      content::SessionStorageNamespace* session_storage_namespace) override;
  bool GetChildId(int* child_id) const override;
  bool GetRouteId(int* route_id) const override;

 private:
  Profile* profile_;
  const GURL url_;
  bool call_did_finish_load_;
  chrome::mojom::SearchBox* search_box_;

  DISALLOW_COPY_AND_ASSIGN(DummyPrerenderContents);
};

class DummyPrerenderContentsFactory : public PrerenderContents::Factory {
 public:
  DummyPrerenderContentsFactory(bool call_did_finish_load,
                                chrome::mojom::SearchBox* search_box)
      : call_did_finish_load_(call_did_finish_load), search_box_(search_box) {}

  PrerenderContents* CreatePrerenderContents(
      PrerenderManager* prerender_manager,
      Profile* profile,
      const GURL& url,
      const Referrer& referrer,
      Origin origin) override;

 private:
  bool call_did_finish_load_;
  chrome::mojom::SearchBox* search_box_;

  DISALLOW_COPY_AND_ASSIGN(DummyPrerenderContentsFactory);
};

DummyPrerenderContents::DummyPrerenderContents(
    PrerenderManager* prerender_manager,
    Profile* profile,
    const GURL& url,
    const Referrer& referrer,
    Origin origin,
    bool call_did_finish_load,
    chrome::mojom::SearchBox* search_box)
    : PrerenderContents(prerender_manager, profile, url, referrer, origin),
      profile_(profile),
      url_(url),
      call_did_finish_load_(call_did_finish_load),
      search_box_(search_box) {}

void DummyPrerenderContents::StartPrerendering(
    const gfx::Rect& bounds,
    content::SessionStorageNamespace* session_storage_namespace) {
  content::SessionStorageNamespaceMap session_storage_namespace_map;
  session_storage_namespace_map[std::string()] = session_storage_namespace;
  prerender_contents_.reset(content::WebContents::CreateWithSessionStorage(
      content::WebContents::CreateParams(profile_),
      session_storage_namespace_map));
  PrerenderTabHelper::CreateForWebContents(prerender_contents_.get());
  content::NavigationController::LoadURLParams params(url_);
  prerender_contents_->GetController().LoadURLWithParams(params);
  SearchTabHelper::CreateForWebContents(prerender_contents_.get());
  auto* search_tab =
      SearchTabHelper::FromWebContents(prerender_contents_.get());
  auto factory = base::MakeUnique<MockSearchBoxClientFactory>();
  ON_CALL(*factory, GetSearchBox()).WillByDefault(Return(search_box_));
  search_tab->ipc_router_for_testing()
      .set_search_box_client_factory_for_testing(std::move(factory));

  prerendering_has_started_ = true;
  DCHECK(session_storage_namespace);
  session_storage_namespace_id_ = session_storage_namespace->id();
  NotifyPrerenderStart();

  if (call_did_finish_load_)
    DidFinishLoad(prerender_contents_->GetMainFrame(), url_);
}

bool DummyPrerenderContents::GetChildId(int* child_id) const {
  *child_id = 1;
  return true;
}

bool DummyPrerenderContents::GetRouteId(int* route_id) const {
  *route_id = 1;
  return true;
}

PrerenderContents* DummyPrerenderContentsFactory::CreatePrerenderContents(
    PrerenderManager* prerender_manager,
    Profile* profile,
    const GURL& url,
    const Referrer& referrer,
    Origin origin) {
  return new DummyPrerenderContents(prerender_manager, profile, url, referrer,
                                    origin, call_did_finish_load_, search_box_);
}

}  // namespace

class InstantSearchPrerendererTest : public InstantUnitTestBase {
 public:
  InstantSearchPrerendererTest() {}

 protected:
  void SetUp() override {
    ASSERT_TRUE(base::FieldTrialList::CreateFieldTrial("EmbeddedSearch",
                                                       "Group1 strk:20"));
    InstantUnitTestBase::SetUp();
  }

  void Init(bool prerender_search_results_base_page,
            bool call_did_finish_load) {
    AddTab(browser(), GURL(url::kAboutBlankURL));

    PrerenderManagerFactory::GetForBrowserContext(browser()->profile())
        ->SetPrerenderContentsFactoryForTest(new DummyPrerenderContentsFactory(
            call_did_finish_load, &mock_search_box_));
    if (prerender_search_results_base_page) {
      content::SessionStorageNamespace* session_storage_namespace =
          GetActiveWebContents()->GetController().
              GetDefaultSessionStorageNamespace();
      InstantSearchPrerenderer* prerenderer = GetInstantSearchPrerenderer();
      prerenderer->Init(session_storage_namespace, gfx::Size(640, 480));
      EXPECT_NE(static_cast<PrerenderHandle*>(NULL), prerender_handle());
    }
  }

  InstantSearchPrerenderer* GetInstantSearchPrerenderer() {
    return instant_service_->GetInstantSearchPrerenderer();
  }

  const GURL& GetPrerenderURL() {
    return GetInstantSearchPrerenderer()->prerender_url();
  }

  void SetLastQuery(const base::string16& query) {
    GetInstantSearchPrerenderer()->last_instant_suggestion_ =
        InstantSuggestion(query, std::string());
  }

  content::WebContents* prerender_contents() {
    return GetInstantSearchPrerenderer()->prerender_contents();
  }

  content::WebContents* GetActiveWebContents() const {
    return browser()->tab_strip_model()->GetWebContentsAt(0);
  }

  PrerenderHandle* prerender_handle() {
    return GetInstantSearchPrerenderer()->prerender_handle_.get();
  }

  void PrerenderSearchQuery(const base::string16& query) {
    Init(true, true);
    InstantSearchPrerenderer* prerenderer = GetInstantSearchPrerenderer();
    prerenderer->Prerender(InstantSuggestion(query, std::string()));
    CommitPendingLoad(&prerender_contents()->GetController());
    EXPECT_TRUE(prerenderer->CanCommitQuery(GetActiveWebContents(), query));
    EXPECT_NE(static_cast<PrerenderHandle*>(NULL), prerender_handle());
  }

  MockSearchBox* mock_search_box() { return &mock_search_box_; }

 private:
  MockSearchBox mock_search_box_;
};

TEST_F(InstantSearchPrerendererTest, GetSearchTermsFromPrerenderedPage) {
  Init(false, false);
  InstantSearchPrerenderer* prerenderer = GetInstantSearchPrerenderer();
  GURL url(GetPrerenderURL());
  EXPECT_EQ(GURL("https://www.google.com/instant?ion=1&foo=foo#foo=foo&strk"),
            url);
  EXPECT_EQ(
      base::UTF16ToASCII(prerenderer->get_last_query()),
      base::UTF16ToASCII(search::ExtractSearchTermsFromURL(profile(), url)));

  // Assume the prerendered page prefetched search results for the query
  // "flowers".
  SetLastQuery(ASCIIToUTF16("flowers"));
  EXPECT_EQ("flowers", base::UTF16ToASCII(prerenderer->get_last_query()));
  EXPECT_EQ(
      base::UTF16ToASCII(prerenderer->get_last_query()),
      base::UTF16ToASCII(search::ExtractSearchTermsFromURL(profile(), url)));
}

TEST_F(InstantSearchPrerendererTest, PrefetchSearchResults) {
  Init(true, true);
  EXPECT_TRUE(prerender_handle()->IsFinishedLoading());
  InstantSearchPrerenderer* prerenderer = GetInstantSearchPrerenderer();
  EXPECT_CALL(*mock_search_box(), SetSuggestionToPrefetch(_));
  prerenderer->Prerender(
      InstantSuggestion(ASCIIToUTF16("flowers"), std::string()));
  EXPECT_EQ("flowers", base::UTF16ToASCII(prerenderer->get_last_query()));
}

TEST_F(InstantSearchPrerendererTest, DoNotPrefetchSearchResults) {
  Init(true, false);
  // Page hasn't finished loading yet.
  EXPECT_FALSE(prerender_handle()->IsFinishedLoading());
  InstantSearchPrerenderer* prerenderer = GetInstantSearchPrerenderer();
  EXPECT_CALL(*mock_search_box(), SetSuggestionToPrefetch(_)).Times(0);
  prerenderer->Prerender(
      InstantSuggestion(ASCIIToUTF16("flowers"), std::string()));
  EXPECT_EQ("", base::UTF16ToASCII(prerenderer->get_last_query()));
}

TEST_F(InstantSearchPrerendererTest, CanCommitQuery) {
  Init(true, true);
  InstantSearchPrerenderer* prerenderer = GetInstantSearchPrerenderer();
  base::string16 query = ASCIIToUTF16("flowers");
  prerenderer->Prerender(InstantSuggestion(query, std::string()));
  EXPECT_TRUE(prerenderer->CanCommitQuery(GetActiveWebContents(), query));

  // Make sure InstantSearchPrerenderer::CanCommitQuery() returns false for
  // invalid search queries.
  EXPECT_TRUE(prerenderer->CanCommitQuery(GetActiveWebContents(),
                                          ASCIIToUTF16("joy")));
  EXPECT_FALSE(prerenderer->CanCommitQuery(GetActiveWebContents(),
                                           base::string16()));
}

TEST_F(InstantSearchPrerendererTest, CommitQuery) {
  base::string16 query = ASCIIToUTF16("flowers");
  PrerenderSearchQuery(query);
  InstantSearchPrerenderer* prerenderer = GetInstantSearchPrerenderer();
  EXPECT_CALL(*mock_search_box(), Submit(_));
  prerenderer->Commit(EmbeddedSearchRequestParams());
}

TEST_F(InstantSearchPrerendererTest, CancelPrerenderRequestOnTabChangeEvent) {
  Init(true, true);
  EXPECT_NE(static_cast<PrerenderHandle*>(NULL), prerender_handle());

  // Add a new tab to deactivate the current tab.
  AddTab(browser(), GURL(url::kAboutBlankURL));
  EXPECT_EQ(2, browser()->tab_strip_model()->count());

  // Make sure the pending prerender request is cancelled.
  EXPECT_EQ(static_cast<PrerenderHandle*>(NULL), prerender_handle());
}

TEST_F(InstantSearchPrerendererTest, CancelPendingPrerenderRequest) {
  Init(true, true);
  EXPECT_NE(static_cast<PrerenderHandle*>(NULL), prerender_handle());

  InstantSearchPrerenderer* prerenderer = GetInstantSearchPrerenderer();
  prerenderer->Cancel();
  EXPECT_EQ(static_cast<PrerenderHandle*>(NULL), prerender_handle());
}

TEST_F(InstantSearchPrerendererTest, PrerenderingAllowed) {
  Init(true, true);
  InstantSearchPrerenderer* prerenderer = GetInstantSearchPrerenderer();
  content::WebContents* active_tab = GetActiveWebContents();
  EXPECT_EQ(GURL(url::kAboutBlankURL), active_tab->GetURL());

  // Allow prerendering only for search type AutocompleteMatch suggestions.
  AutocompleteMatch search_type_match(NULL, 1100, false,
                                      AutocompleteMatchType::SEARCH_SUGGEST);
  search_type_match.keyword = ASCIIToUTF16("{google:baseurl}");
  EXPECT_TRUE(AutocompleteMatch::IsSearchType(search_type_match.type));
  EXPECT_TRUE(prerenderer->IsAllowed(search_type_match, active_tab));

  // Do not allow prerendering for custom search provider requests.
  TemplateURLData data;
  data.SetURL("https://www.dummyurl.com/search?q=%s&img=1");
  data.SetShortName(ASCIIToUTF16("t"));
  data.SetKeyword(ASCIIToUTF16("k"));
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile());
  service->Add(base::MakeUnique<TemplateURL>(data));
  service->Load();
  AutocompleteMatch custom_search_type_match(
      NULL, 1100, false, AutocompleteMatchType::SEARCH_SUGGEST);
  custom_search_type_match.keyword = ASCIIToUTF16("k");
  custom_search_type_match.destination_url =
      GURL("https://www.dummyurl.com/search?q=fan&img=1");
  const TemplateURL* template_url =
      custom_search_type_match.GetTemplateURL(service, false);
  EXPECT_TRUE(template_url);
  EXPECT_TRUE(AutocompleteMatch::IsSearchType(custom_search_type_match.type));
  EXPECT_FALSE(prerenderer->IsAllowed(custom_search_type_match, active_tab));

  AutocompleteMatch url_type_match(NULL, 1100, true,
                                   AutocompleteMatchType::URL_WHAT_YOU_TYPED);
  EXPECT_FALSE(AutocompleteMatch::IsSearchType(url_type_match.type));
  EXPECT_FALSE(prerenderer->IsAllowed(url_type_match, active_tab));
}

TEST_F(InstantSearchPrerendererTest, UsePrerenderPage) {
  PrerenderSearchQuery(ASCIIToUTF16("foo"));

  // Open a search results page. A prerendered page exists for |url|. Make sure
  // the browser swaps the current tab contents with the prerendered contents.
  GURL url("https://www.google.com/alt#quux=foo&strk");
  browser()->OpenURL(content::OpenURLParams(url, Referrer(),
                                            WindowOpenDisposition::CURRENT_TAB,
                                            ui::PAGE_TRANSITION_TYPED, false));
  EXPECT_EQ(GetPrerenderURL(), GetActiveWebContents()->GetURL());
  EXPECT_EQ(static_cast<PrerenderHandle*>(NULL), prerender_handle());
}

TEST_F(InstantSearchPrerendererTest, PrerenderRequestCancelled) {
  PrerenderSearchQuery(ASCIIToUTF16("foo"));

  // Cancel the prerender request.
  InstantSearchPrerenderer* prerenderer = GetInstantSearchPrerenderer();
  prerenderer->Cancel();
  EXPECT_EQ(static_cast<PrerenderHandle*>(NULL), prerender_handle());

  // Open a search results page. Prerendered page does not exists for |url|.
  // Make sure the browser navigates the current tab to this |url|.
  GURL url("https://www.google.com/alt#quux=foo&strk");
  browser()->OpenURL(content::OpenURLParams(url, Referrer(),
                                            WindowOpenDisposition::CURRENT_TAB,
                                            ui::PAGE_TRANSITION_TYPED, false));
  EXPECT_NE(GetPrerenderURL(), GetActiveWebContents()->GetURL());
  EXPECT_EQ(url, GetActiveWebContents()->GetURL());
}

TEST_F(InstantSearchPrerendererTest,
       UsePrerenderedPage_SearchQueryMistmatch) {
  PrerenderSearchQuery(ASCIIToUTF16("foo"));

  // Open a search results page. Committed query("pen") doesn't match with the
  // prerendered search query("foo"). Make sure the browser swaps the current
  // tab contents with the prerendered contents.
  GURL url("https://www.google.com/alt#quux=pen&strk");
  browser()->OpenURL(content::OpenURLParams(url, Referrer(),
                                            WindowOpenDisposition::CURRENT_TAB,
                                            ui::PAGE_TRANSITION_TYPED, false));
  EXPECT_EQ(GetPrerenderURL(), GetActiveWebContents()->GetURL());
  EXPECT_EQ(static_cast<PrerenderHandle*>(NULL), prerender_handle());
}

TEST_F(InstantSearchPrerendererTest,
       CancelPrerenderRequest_EmptySearchQueryCommitted) {
  PrerenderSearchQuery(ASCIIToUTF16("foo"));

  // Open a search results page. Make sure the InstantSearchPrerenderer cancels
  // the active prerender request upon the receipt of empty search query.
  GURL url("https://www.google.com/alt#quux=&strk");
  browser()->OpenURL(content::OpenURLParams(url, Referrer(),
                                            WindowOpenDisposition::CURRENT_TAB,
                                            ui::PAGE_TRANSITION_TYPED, false));
  EXPECT_NE(GetPrerenderURL(), GetActiveWebContents()->GetURL());
  EXPECT_EQ(url, GetActiveWebContents()->GetURL());
  EXPECT_EQ(static_cast<PrerenderHandle*>(NULL), prerender_handle());
}

TEST_F(InstantSearchPrerendererTest,
       CancelPrerenderRequest_UnsupportedDispositions) {
  PrerenderSearchQuery(ASCIIToUTF16("pen"));

  // Open a search results page. Make sure the InstantSearchPrerenderer cancels
  // the active prerender request for unsupported window dispositions.
  GURL url("https://www.google.com/alt#quux=pen&strk");
  browser()->OpenURL(content::OpenURLParams(
      url, Referrer(), WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui::PAGE_TRANSITION_TYPED, false));
  content::WebContents* new_tab =
      browser()->tab_strip_model()->GetWebContentsAt(1);
  EXPECT_NE(GetPrerenderURL(), new_tab->GetURL());
  EXPECT_EQ(url, new_tab->GetURL());
  EXPECT_EQ(static_cast<PrerenderHandle*>(NULL), prerender_handle());
}

class ReuseInstantSearchBasePageTest : public InstantSearchPrerendererTest {
 public:
  ReuseInstantSearchBasePageTest() {}

 protected:
  void SetUp() override {
    ASSERT_TRUE(base::FieldTrialList::CreateFieldTrial("EmbeddedSearch",
                                                       "Group1 strk:20"));
    InstantSearchPrerendererTest::SetUp();
  }
};

TEST_F(ReuseInstantSearchBasePageTest, CanCommitQuery) {
  Init(true, true);
  InstantSearchPrerenderer* prerenderer = GetInstantSearchPrerenderer();
  base::string16 query = ASCIIToUTF16("flowers");
  prerenderer->Prerender(InstantSuggestion(query, std::string()));
  EXPECT_TRUE(prerenderer->CanCommitQuery(GetActiveWebContents(), query));

  // When the Instant search base page has finished loading,
  // InstantSearchPrerenderer can commit any search query to the prerendered
  // page (even if it doesn't match the last known suggestion query).
  EXPECT_TRUE(prerenderer->CanCommitQuery(GetActiveWebContents(),
                                           ASCIIToUTF16("joy")));
  // Invalid search query committed.
  EXPECT_FALSE(prerenderer->CanCommitQuery(GetActiveWebContents(),
                                           base::string16()));
}

TEST_F(ReuseInstantSearchBasePageTest,
       CanCommitQuery_InstantSearchBasePageLoadInProgress) {
  Init(true, false);
  InstantSearchPrerenderer* prerenderer = GetInstantSearchPrerenderer();
  base::string16 query = ASCIIToUTF16("flowers");
  prerenderer->Prerender(InstantSuggestion(query, std::string()));

  // When the Instant search base page hasn't finished loading,
  // InstantSearchPrerenderer cannot commit any search query to the base page.
  EXPECT_FALSE(prerenderer->CanCommitQuery(GetActiveWebContents(), query));
  EXPECT_FALSE(prerenderer->CanCommitQuery(GetActiveWebContents(),
                                           ASCIIToUTF16("joy")));
}

#if !defined(OS_ANDROID)
class TestUsePrerenderPage : public InstantSearchPrerendererTest {
};

TEST_F(TestUsePrerenderPage, ExtractSearchTermsAndUsePrerenderPage) {
  PrerenderSearchQuery(ASCIIToUTF16("foo"));

  // Open a search results page. Query extraction flag is disabled in field
  // trials. Search results page URL does not contain search terms replacement
  // key. Make sure UsePrerenderedPage() extracts the search terms from the URL
  // and uses the prerendered page contents.
  GURL url("https://www.google.com/alt#quux=foo");
  browser()->OpenURL(content::OpenURLParams(url, Referrer(),
                                            WindowOpenDisposition::CURRENT_TAB,
                                            ui::PAGE_TRANSITION_TYPED, false));
  EXPECT_EQ(GetPrerenderURL(), GetActiveWebContents()->GetURL());
  EXPECT_EQ(static_cast<PrerenderHandle*>(NULL), prerender_handle());
}

TEST_F(TestUsePrerenderPage, DoNotUsePrerenderPage) {
  PrerenderSearchQuery(ASCIIToUTF16("foo"));

  // Do not use prerendered page for renderer initiated search request.
  GURL url("https://www.google.com/alt#quux=foo");
  browser()->OpenURL(content::OpenURLParams(
      url, Referrer(), WindowOpenDisposition::CURRENT_TAB,
      ui::PAGE_TRANSITION_LINK, true /* is_renderer_initiated */));
  EXPECT_NE(GetPrerenderURL(), GetActiveWebContents()->GetURL());
  EXPECT_EQ(static_cast<PrerenderHandle*>(NULL), prerender_handle());
}

TEST_F(TestUsePrerenderPage, SetEmbeddedSearchRequestParams) {
  PrerenderSearchQuery(ASCIIToUTF16("foo"));
  EXPECT_TRUE(browser()->instant_controller());
  EXPECT_CALL(
      *mock_search_box(),
      Submit(AllOf(Field(&EmbeddedSearchRequestParams::original_query,
                         Eq(base::ASCIIToUTF16("f"))),
                   Field(&EmbeddedSearchRequestParams::input_encoding,
                         Eq(base::ASCIIToUTF16("utf-8"))),
                   Field(&EmbeddedSearchRequestParams::rlz_parameter_value,
                         Eq(base::ASCIIToUTF16(""))),
                   Field(&EmbeddedSearchRequestParams::assisted_query_stats,
                         Eq(base::ASCIIToUTF16("chrome...0"))))));

  // Open a search results page. Query extraction flag is disabled in field
  // trials. Search results page URL does not contain search terms replacement
  // key.
  GURL url("https://www.google.com/url?bar=foo&aqs=chrome...0&ie=utf-8&oq=f");
  browser()->instant_controller()->OpenInstant(
      WindowOpenDisposition::CURRENT_TAB, url);
}
#endif
