// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SEARCH_SEARCH_IPC_ROUTER_H_
#define CHROME_BROWSER_UI_SEARCH_SEARCH_IPC_ROUTER_H_

#include <memory>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/time/time.h"
#include "chrome/common/instant.mojom.h"
#include "chrome/common/search/instant_types.h"
#include "chrome/common/search/ntp_logging_events.h"
#include "components/ntp_tiles/tile_source.h"
#include "components/ntp_tiles/tile_visual_type.h"
#include "components/omnibox/common/omnibox_focus_state.h"
#include "content/public/browser/web_contents_binding_set.h"
#include "content/public/browser/web_contents_observer.h"

class GURL;

namespace content {
class WebContents;
}

class SearchIPCRouterTest;

// SearchIPCRouter is responsible for receiving and sending IPC messages between
// the browser and the Instant page.
class SearchIPCRouter : public content::WebContentsObserver,
                        public chrome::mojom::Instant {
 public:
  // SearchIPCRouter calls its delegate in response to messages received from
  // the page.
  class Delegate {
   public:
    // Called when the page wants the omnibox to be focused. |state| specifies
    // the omnibox focus state.
    virtual void FocusOmnibox(OmniboxFocusState state) = 0;

    // Called when the SearchBox wants to delete a Most Visited item.
    virtual void OnDeleteMostVisitedItem(const GURL& url) = 0;

    // Called when the SearchBox wants to undo a Most Visited deletion.
    virtual void OnUndoMostVisitedDeletion(const GURL& url) = 0;

    // Called when the SearchBox wants to undo all Most Visited deletions.
    virtual void OnUndoAllMostVisitedDeletions() = 0;

    // Called to signal that an event has occurred on the New Tab Page at a
    // particular time since navigation start.
    virtual void OnLogEvent(NTPLoggingEventType event,
                            base::TimeDelta time) = 0;

    // Called to log an impression from a given provider on the New Tab Page.
    virtual void OnLogMostVisitedImpression(
        int position,
        ntp_tiles::TileSource tile_source,
        ntp_tiles::TileVisualType tile_type) = 0;

    // Called to log a navigation from a given provider on the New Tab Page.
    virtual void OnLogMostVisitedNavigation(
        int position,
        ntp_tiles::TileSource tile_source,
        ntp_tiles::TileVisualType tile_type) = 0;

    // Called when the page wants to paste the |text| (or the clipboard contents
    // if the |text| is empty) into the omnibox.
    virtual void PasteIntoOmnibox(const base::string16& text) = 0;

    // Called when the SearchBox wants to verify the signed-in Chrome identity
    // against the provided |identity|. Will make a round-trip to the browser
    // and eventually return the result through SendChromeIdentityCheckResult.
    // Calls SendChromeIdentityCheckResult with true if the identity matches.
    virtual void OnChromeIdentityCheck(const base::string16& identity) = 0;

    // Called when the SearchBox wants to verify the signed-in Chrome identity
    // against the provided |identity|. Will make a round-trip to the browser
    // and eventually return the result through SendHistorySyncCheckResult.
    // Calls SendHistorySyncCheckResult with true if the user syncs their
    // history.
    virtual void OnHistorySyncCheck() = 0;
  };

  // An interface to be implemented by consumers of SearchIPCRouter objects to
  // decide whether to process the message received from the page, and vice
  // versa (decide whether to send messages to the page).
  class Policy {
   public:
    virtual ~Policy() {}

    // SearchIPCRouter calls these functions before sending/receiving messages
    // to/from the page.
    virtual bool ShouldProcessFocusOmnibox(bool is_active_tab) = 0;
    virtual bool ShouldProcessDeleteMostVisitedItem() = 0;
    virtual bool ShouldProcessUndoMostVisitedDeletion() = 0;
    virtual bool ShouldProcessUndoAllMostVisitedDeletions() = 0;
    virtual bool ShouldProcessLogEvent() = 0;
    virtual bool ShouldProcessPasteIntoOmnibox(bool is_active_tab) = 0;
    virtual bool ShouldProcessChromeIdentityCheck() = 0;
    virtual bool ShouldProcessHistorySyncCheck() = 0;
    virtual bool ShouldSendSetSuggestionToPrefetch() = 0;
    virtual bool ShouldSendSetInputInProgress(bool is_active_tab) = 0;
    virtual bool ShouldSendOmniboxFocusChanged() = 0;
    virtual bool ShouldSendMostVisitedItems() = 0;
    virtual bool ShouldSendThemeBackgroundInfo() = 0;
    virtual bool ShouldSubmitQuery() = 0;
  };

  // Creates chrome::mojom::SearchBox connections on request.
  class SearchBoxClientFactory {
   public:
    SearchBoxClientFactory() = default;
    virtual ~SearchBoxClientFactory() = default;

    // The returned pointer is owned by the factory.
    virtual chrome::mojom::SearchBox* GetSearchBox() = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(SearchBoxClientFactory);
  };

  SearchIPCRouter(content::WebContents* web_contents,
                  Delegate* delegate,
                  std::unique_ptr<Policy> policy);
  ~SearchIPCRouter() override;

  // Tells the SearchIPCRouter that a new page in an Instant process committed.
  void OnNavigationEntryCommitted();

  // Tells the renderer about the result of the Chrome identity check.
  void SendChromeIdentityCheckResult(const base::string16& identity,
                                     bool identity_match);

  // Tells the renderer whether the user syncs history.
  void SendHistorySyncCheckResult(bool sync_history);

  // Tells the page the suggestion to be prefetched if any.
  void SetSuggestionToPrefetch(const InstantSuggestion& suggestion);

  // Tells the page that user input started or stopped.
  void SetInputInProgress(bool input_in_progress);

  // Tells the page that the omnibox focus has changed.
  void OmniboxFocusChanged(OmniboxFocusState state,
                           OmniboxFocusChangeReason reason);

  // Tells the renderer about the most visited items.
  void SendMostVisitedItems(const std::vector<InstantMostVisitedItem>& items);

  // Tells the renderer about the current theme background.
  void SendThemeBackgroundInfo(const ThemeBackgroundInfo& theme_info);

  // Tells the page that the user pressed Enter in the omnibox.
  void Submit(const EmbeddedSearchRequestParams& params);

  // Called when the tab corresponding to |this| instance is activated.
  void OnTabActivated();

  // Called when the tab corresponding to |this| instance is deactivated.
  void OnTabDeactivated();

  // chrome::mojom::Instant:
  void FocusOmnibox(int page_id, OmniboxFocusState state) override;
  void DeleteMostVisitedItem(int page_seq_no, const GURL& url) override;
  void UndoMostVisitedDeletion(int page_seq_no, const GURL& url) override;
  void UndoAllMostVisitedDeletions(int page_seq_no) override;
  void LogEvent(int page_seq_no,
                NTPLoggingEventType event,
                base::TimeDelta time) override;
  void LogMostVisitedImpression(int page_seq_no,
                                int position,
                                ntp_tiles::TileSource tile_source,
                                ntp_tiles::TileVisualType tile_type) override;
  void LogMostVisitedNavigation(int page_seq_no,
                                int position,
                                ntp_tiles::TileSource tile_source,
                                ntp_tiles::TileVisualType tile_type) override;
  void PasteAndOpenDropdown(int page_seq_no,
                            const base::string16& text) override;
  void ChromeIdentityCheck(int page_seq_no,
                           const base::string16& identity) override;
  void HistorySyncCheck(int page_seq_no) override;

  void set_search_box_client_factory_for_testing(
      std::unique_ptr<SearchBoxClientFactory> factory) {
    search_box_client_factory_ = std::move(factory);
  }

 private:
  friend class SearchIPCRouterPolicyTest;
  friend class SearchIPCRouterTest;
  FRIEND_TEST_ALL_PREFIXES(SearchTabHelperTest,
                           PageURLDoesntBelongToInstantRenderer);
  FRIEND_TEST_ALL_PREFIXES(SearchIPCRouterTest,
                           IgnoreMessageIfThePageIsNotActive);
  FRIEND_TEST_ALL_PREFIXES(SearchIPCRouterTest, HandleTabChangedEvents);

  // Used by unit tests to set a fake delegate.
  void set_delegate_for_testing(Delegate* delegate);

  // Used by unit tests.
  void set_policy_for_testing(std::unique_ptr<Policy> policy);

  // Used by unit tests.
  Policy* policy_for_testing() const { return policy_.get(); }

  // Used by unit tests.
  int page_seq_no_for_testing() const { return commit_counter_; }

  chrome::mojom::SearchBox* search_box() {
    return search_box_client_factory_->GetSearchBox();
  }

  Delegate* delegate_;
  std::unique_ptr<Policy> policy_;

  // Holds the number of main frame commits executed in this tab. Used by the
  // SearchIPCRouter to ensure that delayed IPC replies are ignored.
  int commit_counter_;

  // Set to true, when the tab corresponding to |this| instance is active.
  bool is_active_tab_;

  // Binding for the connected main frame. We only allow one frame to connect at
  // the moment, but this could be extended to a map of connected frames, if
  // desired.
  mojo::AssociatedBinding<chrome::mojom::Instant> binding_;

  std::unique_ptr<SearchBoxClientFactory> search_box_client_factory_;

  DISALLOW_COPY_AND_ASSIGN(SearchIPCRouter);
};

#endif  // CHROME_BROWSER_UI_SEARCH_SEARCH_IPC_ROUTER_H_
