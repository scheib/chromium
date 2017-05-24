// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "chrome/browser/ui/bookmarks/bookmark_bubble_sign_in_delegate.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/cocoa/bubble_anchor_helper_views.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bubble_view.h"
#include "chrome/browser/ui/views/content_setting_bubble_contents.h"
#include "chrome/browser/ui/views/location_bar/zoom_bubble_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/browser/ui/views/task_manager_view.h"
#include "chrome/browser/ui/views/update_recommended_message_box.h"

// This file provides definitions of desktop browser dialog-creation methods for
// Mac where a Cocoa browser is using Views dialogs. I.e. it is included in the
// Cocoa build and definitions under chrome/browser/ui/cocoa may select at
// runtime whether to show a Cocoa dialog, or the toolkit-views dialog defined
// here (declared in browser_dialogs.h).

namespace chrome {

void ShowPageInfoBubbleViewsAtPoint(
    const gfx::Point& anchor_point,
    Profile* profile,
    content::WebContents* web_contents,
    const GURL& virtual_url,
    const security_state::SecurityInfo& security_info) {
  // Don't show the bubble again if it's already showing. A second click on the
  // location icon in the omnibox will dismiss an open bubble. This behaviour is
  // consistent with the non-Mac views implementation.
  // Note that when the browser is toolkit-views, IsBubbleShowing() is checked
  // earlier because the bubble is shown on mouse release (but dismissed on
  // mouse pressed). A Cocoa browser does both on mouse pressed, so a check
  // when showing is sufficient.
  if (PageInfoBubbleView::GetShownBubbleType() !=
      PageInfoBubbleView::BUBBLE_NONE) {
    return;
  }

  PageInfoBubbleView::ShowBubble(nullptr, nullptr,
                                 gfx::Rect(anchor_point, gfx::Size()), profile,
                                 web_contents, virtual_url, security_info);
}

void ShowBookmarkBubbleViewsAtPoint(const gfx::Point& anchor_point,
                                    gfx::NativeView parent,
                                    bookmarks::BookmarkBubbleObserver* observer,
                                    Browser* browser,
                                    const GURL& virtual_url,
                                    bool already_bookmarked) {
  // The Views dialog may prompt for sign in.
  std::unique_ptr<BubbleSyncPromoDelegate> delegate(
      new BookmarkBubbleSignInDelegate(browser));

  BookmarkBubbleView::ShowBubble(
      nullptr, gfx::Rect(anchor_point, gfx::Size()), parent, observer,
      std::move(delegate), browser->profile(), virtual_url, already_bookmarked);
}

void ShowZoomBubbleViewsAtPoint(content::WebContents* web_contents,
                                const gfx::Point& anchor_point,
                                bool user_action) {
  ZoomBubbleView::ShowBubble(web_contents, anchor_point,
                             user_action
                                 ? LocationBarBubbleDelegateView::USER_GESTURE
                                 : LocationBarBubbleDelegateView::AUTOMATIC);
  if (ZoomBubbleView::GetZoomBubble())
    KeepBubbleAnchored(ZoomBubbleView::GetZoomBubble());
}

void CloseZoomBubbleViews() {
  ZoomBubbleView::CloseCurrentBubble();
}

void RefreshZoomBubbleViews() {
  if (ZoomBubbleView::GetZoomBubble())
    ZoomBubbleView::GetZoomBubble()->Refresh();
}

bool IsZoomBubbleViewsShown() {
  return ZoomBubbleView::GetZoomBubble() != nullptr;
}

task_manager::TaskManagerTableModel* ShowTaskManagerViews(Browser* browser) {
  return task_manager::TaskManagerView::Show(browser);
}

void HideTaskManagerViews() {
  task_manager::TaskManagerView::Hide();
}

void ContentSettingBubbleViewsBridge::Show(gfx::NativeView parent_view,
                                           ContentSettingBubbleModel* model,
                                           content::WebContents* web_contents,
                                           const gfx::Point& anchor) {
  ContentSettingBubbleContents* contents =
      new ContentSettingBubbleContents(model, web_contents, nullptr,
          views::BubbleBorder::Arrow::TOP_RIGHT);
  contents->set_parent_window(parent_view);
  contents->SetAnchorRect(gfx::Rect(anchor, gfx::Size()));
  views::BubbleDialogDelegateView::CreateBubble(contents)->Show();
}

void ShowUpdateChromeDialogViews(gfx::NativeWindow parent) {
  UpdateRecommendedMessageBox::Show(parent);
}

}  // namespace chrome
