// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PAGE_INFO_PAGE_INFO_BUBBLE_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_PAGE_INFO_PAGE_INFO_BUBBLE_VIEW_H_

#include <memory>
#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/page_info/page_info_ui.h"
#include "chrome/browser/ui/views/page_info/chosen_object_row_observer.h"
#include "chrome/browser/ui/views/page_info/permission_selector_row.h"
#include "chrome/browser/ui/views/page_info/permission_selector_row_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/bubble/bubble_dialog_delegate.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/link_listener.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/styled_label_listener.h"

class GURL;
class BubbleHeaderView;
class Profile;

namespace content {
class WebContents;
}

namespace net {
class X509Certificate;
}

namespace security_state {
struct SecurityInfo;
}  // namespace security_state

namespace test {
class PageInfoBubbleViewTestApi;
}

namespace views {
class Link;
class Widget;
class WidgetObserver;
}

enum : int {
  // Left icon margin.
  kPermissionIconMarginLeft = 6,
  // The width of the column that contains the permissions icons.
  kPermissionIconColumnWidth = 16,
};

// The views implementation of the page info UI.
class PageInfoBubbleView : public content::WebContentsObserver,
                           public PermissionSelectorRowObserver,
                           public ChosenObjectRowObserver,
                           public views::BubbleDialogDelegateView,
                           public views::ButtonListener,
                           public views::LinkListener,
                           public views::StyledLabelListener,
                           public PageInfoUI {
 public:
  ~PageInfoBubbleView() override;

  // Type of the bubble being displayed.
  enum BubbleType {
    BUBBLE_NONE,
    // Usual page info bubble for websites.
    BUBBLE_PAGE_INFO,
    // Custom bubble for internal pages like chrome:// and chrome-extensions://.
    BUBBLE_INTERNAL_PAGE
  };

  // If |anchor_view| is null, |anchor_rect| is used to anchor the bubble.
  static void ShowBubble(views::View* anchor_view,
                         views::WidgetObserver* widget_observer,
                         const gfx::Rect& anchor_rect,
                         Profile* profile,
                         content::WebContents* web_contents,
                         const GURL& url,
                         const security_state::SecurityInfo& security_info);

  // Returns the type of the bubble being shown.
  static BubbleType GetShownBubbleType();

 private:
  friend class test::PageInfoBubbleViewTestApi;

  PageInfoBubbleView(views::View* anchor_view,
                     gfx::NativeView parent_window,
                     Profile* profile,
                     content::WebContents* web_contents,
                     const GURL& url,
                     const security_state::SecurityInfo& security_info);

  // WebContentsObserver implementation.
  void RenderFrameDeleted(content::RenderFrameHost* render_frame_host) override;
  void WebContentsDestroyed() override;

  // PermissionSelectorRowObserver implementation.
  void OnPermissionChanged(
      const PageInfoUI::PermissionInfo& permission) override;

  // ChosenObjectRowObserver implementation.
  void OnChosenObjectDeleted(const PageInfoUI::ChosenObjectInfo& info) override;

  // views::BubbleDialogDelegateView implementation.
  base::string16 GetWindowTitle() const override;
  bool ShouldShowCloseButton() const override;
  void OnWidgetDestroying(views::Widget* widget) override;
  int GetDialogButtons() const override;
  const gfx::FontList& GetTitleFontList() const override;

  // views::ButtonListener implementation.
  void ButtonPressed(views::Button* button, const ui::Event& event) override;

  // views::LinkListener implementation.
  void LinkClicked(views::Link* source, int event_flags) override;

  // views::StyledLabelListener implementation.
  void StyledLabelLinkClicked(views::StyledLabel* label,
                              const gfx::Range& range,
                              int event_flags) override;

  // views::View implementation.
  gfx::Size GetPreferredSize() const override;

  // PageInfoUI implementations.
  void SetCookieInfo(const CookieInfoList& cookie_info_list) override;
  void SetPermissionInfo(const PermissionInfoList& permission_info_list,
                         ChosenObjectInfoList chosen_object_info_list) override;
  void SetIdentityInfo(const IdentityInfo& identity_info) override;

  // Creates the contents of the |site_settings_view_|. The ownership of the
  // returned view is transferred to the caller.
  views::View* CreateSiteSettingsView(int side_margin) WARN_UNUSED_RESULT;

  // Used to asynchronously handle clicks since these calls may cause the
  // destruction of the settings view and the base class window still needs to
  // be alive to finish handling the mouse or keyboard click.
  void HandleLinkClickedAsync(views::Link* source);

  // The presenter that controls the Page Info UI.
  std::unique_ptr<PageInfo> presenter_;

  Profile* profile_;

  // The header section (containing security-related information).
  BubbleHeaderView* header_;

  // The security summary for the current page.
  base::string16 summary_text_;

  // The separator between the header and the site settings view.
  views::Separator* separator_;

  // The view that contains the certificate, cookie, and permissions sections.
  views::View* site_settings_view_;

  // The link that opens the "Cookies" dialog.
  views::Link* cookie_dialog_link_;

  // The view that contains the "Permissions" table of the site settings view.
  views::View* permissions_view_;

  // The certificate provided by the site, if one exists.
  scoped_refptr<net::X509Certificate> certificate_;

  // These rows bundle together all the |View|s involved in a single row of the
  // permissions section, and keep those views updated when the underlying
  // |Permission| changes.
  std::vector<std::unique_ptr<PermissionSelectorRow>> selector_rows_;

  base::WeakPtrFactory<PageInfoBubbleView> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(PageInfoBubbleView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_PAGE_INFO_PAGE_INFO_BUBBLE_VIEW_H_
