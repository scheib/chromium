// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SEARCH_SEARCH_TAB_HELPER_H_
#define CHROME_BROWSER_UI_SEARCH_SEARCH_TAB_HELPER_H_

#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/strings/string16.h"
#include "base/time/time.h"
#include "chrome/browser/search/instant_service_observer.h"
#include "chrome/browser/ui/search/search_ipc_router.h"
#include "chrome/browser/ui/search/search_model.h"
#include "chrome/common/search/instant_types.h"
#include "chrome/common/search/ntp_logging_events.h"
#include "components/ntp_tiles/tile_source.h"
#include "components/ntp_tiles/tile_visual_type.h"
#include "components/omnibox/common/omnibox_focus_state.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class WebContents;
struct LoadCommittedDetails;
}

class GURL;
class InstantService;
class InstantTabTest;
class OmniboxView;
class Profile;
class SearchIPCRouterTest;

// Per-tab search "helper".  Acts as the owner and controller of the tab's
// search UI model.
//
// When a navigation is committed and when the page is finished loading,
// SearchTabHelper determines the instant support for the page, i.e. whether
// the page is rendered in the instant process.
class SearchTabHelper : public content::WebContentsObserver,
                        public content::WebContentsUserData<SearchTabHelper>,
                        public InstantServiceObserver,
                        public SearchIPCRouter::Delegate {
 public:
  ~SearchTabHelper() override;

  SearchModel* model() {
    return &model_;
  }

  // Invoked when the omnibox input state is changed in some way that might
  // affect the search mode.
  void OmniboxInputStateChanged();

  // Called to indicate that the omnibox focus state changed with the given
  // |reason|.
  void OmniboxFocusChanged(OmniboxFocusState state,
                           OmniboxFocusChangeReason reason);

  // Invoked when the active navigation entry is updated in some way that might
  // affect the search mode. This is used by Instant when it "fixes up" the
  // virtual URL of the active entry. Regular navigations are captured through
  // the notification system and shouldn't call this method.
  void NavigationEntryUpdated();

  // Sends the current SearchProvider suggestion to the Instant page if any.
  void SetSuggestionToPrefetch(const InstantSuggestion& suggestion);

  // Tells the page that the user pressed Enter in the omnibox.
  void Submit(const EmbeddedSearchRequestParams& params);

  // Called when the tab corresponding to |this| instance is activated.
  void OnTabActivated();

  // Called when the tab corresponding to |this| instance is deactivated.
  void OnTabDeactivated();

  SearchIPCRouter& ipc_router_for_testing() { return ipc_router_; }

 private:
  friend class content::WebContentsUserData<SearchTabHelper>;
  friend class SearchIPCRouterPolicyTest;
  friend class SearchIPCRouterTest;

  FRIEND_TEST_ALL_PREFIXES(SearchTabHelperTest,
                           DetermineIfPageSupportsInstant_Local);
  FRIEND_TEST_ALL_PREFIXES(SearchTabHelperTest,
                           DetermineIfPageSupportsInstant_NonLocal);
  FRIEND_TEST_ALL_PREFIXES(SearchTabHelperTest,
                           PageURLDoesntBelongToInstantRenderer);
  FRIEND_TEST_ALL_PREFIXES(SearchTabHelperTest,
                           OnChromeIdentityCheckMatch);
  FRIEND_TEST_ALL_PREFIXES(SearchTabHelperTest,
                           OnChromeIdentityCheckMatchSlightlyDifferentGmail);
  FRIEND_TEST_ALL_PREFIXES(SearchTabHelperTest,
                           OnChromeIdentityCheckMatchSlightlyDifferentGmail2);
  FRIEND_TEST_ALL_PREFIXES(SearchTabHelperTest, OnChromeIdentityCheckMismatch);
  FRIEND_TEST_ALL_PREFIXES(SearchTabHelperTest,
                           OnChromeIdentityCheckSignedOutMismatch);
  FRIEND_TEST_ALL_PREFIXES(SearchTabHelperTest,
                           OnHistorySyncCheckSyncing);
  FRIEND_TEST_ALL_PREFIXES(SearchTabHelperTest,
                           OnHistorySyncCheckNotSyncing);
  FRIEND_TEST_ALL_PREFIXES(SearchIPCRouterTest,
                           IgnoreMessageIfThePageIsNotActive);
  FRIEND_TEST_ALL_PREFIXES(SearchIPCRouterTest, HandleTabChangedEvents);
  FRIEND_TEST_ALL_PREFIXES(InstantTabTest,
                           DetermineIfPageSupportsInstant_Local);
  FRIEND_TEST_ALL_PREFIXES(InstantTabTest,
                           DetermineIfPageSupportsInstant_NonLocal);
  FRIEND_TEST_ALL_PREFIXES(InstantTabTest,
                           PageURLDoesntBelongToInstantRenderer);
  FRIEND_TEST_ALL_PREFIXES(InstantTabTest, PageSupportsInstant);

  explicit SearchTabHelper(content::WebContents* web_contents);

  // Overridden from contents::WebContentsObserver:
  void DidStartNavigationToPendingEntry(
      const GURL& url,
      content::ReloadType reload_type) override;
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishLoad(content::RenderFrameHost* render_frame_host,
                     const GURL& validated_url) override;
  void NavigationEntryCommitted(
      const content::LoadCommittedDetails& load_details) override;

  // Overridden from SearchIPCRouter::Delegate:
  void FocusOmnibox(OmniboxFocusState state) override;
  void OnDeleteMostVisitedItem(const GURL& url) override;
  void OnUndoMostVisitedDeletion(const GURL& url) override;
  void OnUndoAllMostVisitedDeletions() override;
  void OnLogEvent(NTPLoggingEventType event, base::TimeDelta time) override;
  void OnLogMostVisitedImpression(int position,
                                  ntp_tiles::TileSource tile_source,
                                  ntp_tiles::TileVisualType tile_type) override;
  void OnLogMostVisitedNavigation(int position,
                                  ntp_tiles::TileSource tile_source,
                                  ntp_tiles::TileVisualType tile_type) override;
  void PasteIntoOmnibox(const base::string16& text) override;
  void OnChromeIdentityCheck(const base::string16& identity) override;
  void OnHistorySyncCheck() override;

  // Overridden from InstantServiceObserver:
  void ThemeInfoChanged(const ThemeBackgroundInfo& theme_info) override;
  void MostVisitedItemsChanged(
      const std::vector<InstantMostVisitedItem>& items) override;

  // Invoked to update the instant support state.
  void InstantSupportChanged(bool supports_instant);

  // Sets the mode of the model based on the current URL of web_contents().
  // Only updates the origin part of the mode if |update_origin| is true,
  // otherwise keeps the current origin.
  void UpdateMode(bool update_origin);

  OmniboxView* GetOmniboxView();
  const OmniboxView* GetOmniboxView() const;

  Profile* profile() const;

  // Returns whether input is in progress, i.e. if the omnibox has focus and the
  // active tab is in mode SEARCH_SUGGESTIONS.
  bool IsInputInProgress() const;

  const bool is_search_enabled_;

  // Model object for UI that cares about search state.
  SearchModel model_;

  content::WebContents* web_contents_;

  SearchIPCRouter ipc_router_;

  InstantService* instant_service_;

  DISALLOW_COPY_AND_ASSIGN(SearchTabHelper);
};

#endif  // CHROME_BROWSER_UI_SEARCH_SEARCH_TAB_HELPER_H_
