// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_SEARCHBOX_SEARCHBOX_H_
#define CHROME_RENDERER_SEARCHBOX_SEARCHBOX_H_

#include <string>
#include <vector>

#include "base/macros.h"
#include "base/strings/string16.h"
#include "chrome/common/instant.mojom.h"
#include "chrome/common/search/instant_types.h"
#include "chrome/common/search/ntp_logging_events.h"
#include "chrome/renderer/instant_restricted_id_cache.h"
#include "components/ntp_tiles/tile_source.h"
#include "components/ntp_tiles/tile_visual_type.h"
#include "components/omnibox/common/omnibox_focus_state.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_observer_tracker.h"
#include "mojo/public/cpp/bindings/associated_binding.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

class SearchBox : public content::RenderFrameObserver,
                  public content::RenderFrameObserverTracker<SearchBox>,
                  public chrome::mojom::SearchBox {
 public:
  enum ImageSourceType {
    NONE = -1,
    FAVICON,
    THUMB
  };

  // Helper class for GenerateImageURLFromTransientURL() to adapt SearchBox's
  // instance, thereby allow mocking for unit tests.
  class IconURLHelper {
   public:
    IconURLHelper();
    virtual ~IconURLHelper();
    // Retruns view id for validating icon URL.
    virtual int GetViewID() const = 0;
    // Returns the page URL string for |rid|, or empty string for invalid |rid|.
    virtual std::string GetURLStringFromRestrictedID(InstantRestrictedID rid)
        const = 0;
  };

  explicit SearchBox(content::RenderFrame* render_frame);
  ~SearchBox() override;

  // Sends LogEvent to the browser.
  void LogEvent(NTPLoggingEventType event);

  // Sends LogMostVisitedImpression to the browser.
  void LogMostVisitedImpression(int position,
                                ntp_tiles::TileSource tile_source,
                                ntp_tiles::TileVisualType tile_type);

  // Sends LogMostVisitedNavigation to the browser.
  void LogMostVisitedNavigation(int position,
                                ntp_tiles::TileSource tile_source,
                                ntp_tiles::TileVisualType tile_type);

  // Sends ChromeIdentityCheck to the browser.
  void CheckIsUserSignedInToChromeAs(const base::string16& identity);

  // Sends HistorySyncCheck to the browser.
  void CheckIsUserSyncingHistory();

  // Sends DeleteMostVisitedItem to the browser.
  void DeleteMostVisitedItem(InstantRestrictedID most_visited_item_id);

  // Generates the image URL of |type| for the most visited item specified in
  // |transient_url|. If |transient_url| is valid, |url| with a translated URL
  // and returns true.  Otherwise it depends on |type|:
  // - FAVICON: Returns true and renders an URL to display the default favicon.
  //
  // For |type| == FAVICON, valid forms of |transient_url|:
  //    chrome-search://favicon/<view_id>/<restricted_id>
  //    chrome-search://favicon/<favicon_parameters>/<view_id>/<restricted_id>
  //
  // For |type| == THUMB, valid form of |transient_url|:
  //    chrome-search://thumb/<render_view_id>/<most_visited_item_id>
  //
  // We do this to prevent search providers from abusing image URLs and deduce
  // whether the user has visited a particular page. For example, if
  // "chrome-search://favicon/http://www.secretsite.com" is accessible, then
  // the search provider can use its return code to determine whether the user
  // has visited "http://www.secretsite.com". Therefore we require search
  // providers to specify URL by "<view_id>/<restricted_id>". We then translate
  // this to the original |url|, and pass the request to the proper endpoint.
  bool GenerateImageURLFromTransientURL(const GURL& transient_url,
                                        ImageSourceType type,
                                        GURL* url) const;

  // Returns the latest most visited items sent by the browser.
  void GetMostVisitedItems(
      std::vector<InstantMostVisitedItemIDPair>* items) const;

  // If the |most_visited_item_id| is found in the cache, sets |item| to it
  // and returns true.
  bool GetMostVisitedItemWithID(InstantRestrictedID most_visited_item_id,
                                InstantMostVisitedItem* item) const;

  // Sends SearchBoxPaste to the browser.
  void Paste(const base::string16& text);

  const ThemeBackgroundInfo& GetThemeBackgroundInfo();
  const EmbeddedSearchRequestParams& GetEmbeddedSearchRequestParams();

  // Sends ChromeViewHostMsg_StartCapturingKeyStrokes to the browser.
  void StartCapturingKeyStrokes();

  // Sends ChromeViewHostMsg_StopCapturingKeyStrokes to the browser.
  void StopCapturingKeyStrokes();

  // Sends UndoAllMostVisitedDeletions to the browser.
  void UndoAllMostVisitedDeletions();

  // Sends UndoMostVisitedDeletion to the browser.
  void UndoMostVisitedDeletion(InstantRestrictedID most_visited_item_id);

  bool is_focused() const { return is_focused_; }
  bool is_input_in_progress() const { return is_input_in_progress_; }
  bool is_key_capture_enabled() const { return is_key_capture_enabled_; }
  const InstantSuggestion& suggestion() const { return suggestion_; }

 private:
  // Overridden from content::RenderFrameObserver:
  void OnDestruct() override;

  // Overridden from chrome::mojom::SearchBox:
  void SetPageSequenceNumber(int page_seq_no) override;
  void ChromeIdentityCheckResult(const base::string16& identity,
                                 bool identity_match) override;
  void FocusChanged(OmniboxFocusState new_focus_state,
                    OmniboxFocusChangeReason reason) override;
  void HistorySyncCheckResult(bool sync_history) override;
  void MostVisitedChanged(
      const std::vector<InstantMostVisitedItem>& items) override;
  void SetInputInProgress(bool input_in_progress) override;
  void SetSuggestionToPrefetch(const InstantSuggestion& suggestion) override;
  void Submit(const EmbeddedSearchRequestParams& params) override;
  void ThemeChanged(const ThemeBackgroundInfo& theme_info) override;

  // Returns the current zoom factor of the render view or 1 on failure.
  double GetZoom() const;

  // Sets the searchbox values to their initial value.
  void Reset();

  // Returns the URL of the Most Visited item specified by the |item_id|.
  GURL GetURLForMostVisitedItem(InstantRestrictedID item_id) const;

  void Bind(chrome::mojom::SearchBoxAssociatedRequest request);

  int page_seq_no_;
  bool is_focused_;
  bool is_input_in_progress_;
  bool is_key_capture_enabled_;
  InstantRestrictedIDCache<InstantMostVisitedItem> most_visited_items_cache_;
  ThemeBackgroundInfo theme_info_;
  EmbeddedSearchRequestParams embedded_search_request_params_;
  InstantSuggestion suggestion_;
  chrome::mojom::InstantAssociatedPtr instant_service_;
  mojo::AssociatedBinding<chrome::mojom::SearchBox> binding_;

  DISALLOW_COPY_AND_ASSIGN(SearchBox);
};

#endif  // CHROME_RENDERER_SEARCHBOX_SEARCHBOX_H_
