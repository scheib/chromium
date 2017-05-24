// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"

#include <stddef.h>

#include <algorithm>
#include <utility>

#include "base/i18n/rtl.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string16.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/certificate_viewer.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/page_info/page_info.h"
#include "chrome/browser/ui/views/collected_cookies_views.h"
#include "chrome/browser/ui/views/harmony/chrome_layout_provider.h"
#include "chrome/browser/ui/views/harmony/chrome_typography.h"
#include "chrome/browser/ui/views/page_info/chosen_object_row.h"
#include "chrome/browser/ui/views/page_info/non_accessible_image_view.h"
#include "chrome/browser/ui/views/page_info/permission_selector_row.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/theme_resources.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/strings/grit/components_chromium_strings.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/common/constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/material_design/material_design_controller.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/image.h"
#include "ui/resources/grit/ui_resources.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"

namespace {

// NOTE(jdonnelly): This use of this process-wide variable assumes that there's
// never more than one page info bubble shown and that it's associated
// with the current window. If this assumption fails in the future, we'll need
// to return a weak pointer from ShowBubble so callers can associate it with the
// current window (or other context) and check if the bubble they care about is
// showing.
PageInfoBubbleView::BubbleType g_shown_bubble_type =
    PageInfoBubbleView::BUBBLE_NONE;

// General constants -----------------------------------------------------------

// Bubble width constraints.
const int kMinBubbleWidth = 320;
const int kMaxBubbleWidth = 1000;

// Security Section (BubbleHeaderView)
// ------------------------------------------

// Margin and padding values for the |BubbleHeaderView|.
const int kHeaderMarginBottom = 10;
const int kHeaderPaddingBottom = 13;

// Spacing between labels in the header.
const int kHeaderLabelSpacing = 4;

// Site Settings Section -------------------------------------------------------

// Spacing above and below the cookies and certificate views.
const int kSubViewsVerticalPadding = 6;

// Spacing between a permission image and the text.
const int kPermissionImageSpacing = 6;

// Spacing between rows in the site settings section
const int kPermissionsVerticalSpacing = 12;

// Spacing between the label and the menu.
const int kPermissionMenuSpacing = 16;

// Button/styled label/link IDs ------------------------------------------------
const int BUTTON_CLOSE = 1337;
const int STYLED_LABEL_SECURITY_DETAILS = 1338;
const int STYLED_LABEL_RESET_CERTIFICATE_DECISIONS = 1339;
const int LINK_COOKIE_DIALOG = 1340;
const int LINK_SITE_SETTINGS = 1341;
const int LINK_CERTIFICATE_VIEWER = 1342;

// The default, ui::kTitleFontSizeDelta, is too large for the page info
// bubble (e.g. +3). Use +1 to obtain a smaller font.
constexpr int kSummaryFontSizeDelta = 1;

// Adds a ColumnSet on |layout| with a single View column and padding columns
// on either side of it with |margin| width.
void AddColumnWithSideMargin(views::GridLayout* layout, int margin, int id) {
  views::ColumnSet* column_set = layout->AddColumnSet(id);
  column_set->AddPaddingColumn(0, margin);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL, 1,
                        views::GridLayout::USE_PREF, 0, 0);
  column_set->AddPaddingColumn(0, margin);
}

// Creates a section containing a title, icon, and link. Used to display
// Cookies and Certificate information.
views::View* CreateInspectLinkSection(const gfx::ImageSkia& image_icon,
                                      const int title_id,
                                      views::Link* link) {
  views::View* new_view = new views::View();

  views::GridLayout* layout = new views::GridLayout(new_view);
  new_view->SetLayoutManager(layout);

  const int column = 0;
  views::ColumnSet* column_set = layout->AddColumnSet(column);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL, 0,
                        views::GridLayout::FIXED, kPermissionIconColumnWidth,
                        0);
  column_set->AddPaddingColumn(0, kPermissionImageSpacing);
  column_set->AddColumn(views::GridLayout::LEADING, views::GridLayout::FILL, 0,
                        views::GridLayout::USE_PREF, 0, 0);

  layout->AddPaddingRow(0, kSubViewsVerticalPadding);

  layout->StartRow(1, column);

  views::ImageView* icon = new NonAccessibleImageView();
  icon->SetImage(image_icon);
  layout->AddView(
      icon, 1, 2, views::GridLayout::FILL,
      // TODO(lgarron): The vertical alignment may change to CENTER once
      // Harmony is implemented. See https://crbug.com/512442#c48
      views::GridLayout::LEADING);

  views::Label* title_label = new views::Label(
      l10n_util::GetStringUTF16(title_id), CONTEXT_BODY_TEXT_LARGE);
  layout->AddView(title_label);
  layout->StartRow(1, column);
  layout->SkipColumns(1);

  layout->AddView(link);
  return new_view;
}

}  // namespace

// |BubbleHeaderView| is the UI element (view) that represents the header of the
// |PageInfoBubbleView|. The header shows the status of the site's
// identity check and the name of the site's identity.
class BubbleHeaderView : public views::View {
 public:
  BubbleHeaderView(views::ButtonListener* button_listener,
                   views::StyledLabelListener* styled_label_listener,
                   int side_margin);
  ~BubbleHeaderView() override;

  // Sets the security summary for the current page.
  void SetSummary(const base::string16& summary_text);

  // Sets the security details for the current page.
  void SetDetails(const base::string16& details_text);

  void AddResetDecisionsLabel();

 private:
  // The listener for the styled labels in this view.
  views::StyledLabelListener* styled_label_listener_;

  // The label that displays the status of the identity check for this site.
  // Includes a link to open the Chrome Help Center article about connection
  // security.
  views::StyledLabel* details_label_;

  // A container for the styled label with a link for resetting cert decisions.
  // This is only shown sometimes, so we use a container to keep track of
  // where to place it (if needed).
  views::View* reset_decisions_label_container_;
  views::StyledLabel* reset_decisions_label_;

  DISALLOW_COPY_AND_ASSIGN(BubbleHeaderView);
};

// The regular PageInfoBubbleView is not supported for internal Chrome pages and
// extension pages. Instead of the |PageInfoBubbleView|, the
// |InternalPageInfoBubbleView| is displayed.
class InternalPageInfoBubbleView : public views::BubbleDialogDelegateView {
 public:
  // If |anchor_view| is nullptr, or has no Widget, |parent_window| may be
  // provided to ensure this bubble is closed when the parent closes.
  InternalPageInfoBubbleView(views::View* anchor_view,
                             gfx::NativeView parent_window,
                             const GURL& url);
  ~InternalPageInfoBubbleView() override;

  // views::BubbleDialogDelegateView:
  void OnWidgetDestroying(views::Widget* widget) override;
  int GetDialogButtons() const override;

 private:
  friend class PageInfoBubbleView;

  // Used around icon and inside bubble border.
  static constexpr int kSpacing = 12;

  DISALLOW_COPY_AND_ASSIGN(InternalPageInfoBubbleView);
};

////////////////////////////////////////////////////////////////////////////////
// Bubble Header
////////////////////////////////////////////////////////////////////////////////

BubbleHeaderView::BubbleHeaderView(
    views::ButtonListener* button_listener,
    views::StyledLabelListener* styled_label_listener,
    int side_margin)
    : styled_label_listener_(styled_label_listener),
      details_label_(nullptr),
      reset_decisions_label_container_(nullptr),
      reset_decisions_label_(nullptr) {
  views::GridLayout* layout = new views::GridLayout(this);
  SetLayoutManager(layout);

  const int label_column_status = 1;
  AddColumnWithSideMargin(layout, side_margin, label_column_status);
  layout->AddPaddingRow(0, kHeaderLabelSpacing);

  layout->StartRow(0, label_column_status);
  details_label_ =
      new views::StyledLabel(base::string16(), styled_label_listener);
  details_label_->set_id(STYLED_LABEL_SECURITY_DETAILS);
  layout->AddView(details_label_, 1, 1, views::GridLayout::FILL,
                  views::GridLayout::LEADING);

  layout->StartRow(0, label_column_status);
  reset_decisions_label_container_ = new views::View();
  reset_decisions_label_container_->SetLayoutManager(
      new views::BoxLayout(views::BoxLayout::kHorizontal, 0, 0, 0));
  layout->AddView(reset_decisions_label_container_, 1, 1,
                  views::GridLayout::FILL, views::GridLayout::LEADING);

  layout->AddPaddingRow(1, kHeaderPaddingBottom);
}

BubbleHeaderView::~BubbleHeaderView() {}

void BubbleHeaderView::SetDetails(const base::string16& details_text) {
  std::vector<base::string16> subst;
  subst.push_back(details_text);
  subst.push_back(l10n_util::GetStringUTF16(IDS_LEARN_MORE));

  std::vector<size_t> offsets;

  base::string16 text = base::ReplaceStringPlaceholders(
      base::ASCIIToUTF16("$1 $2"), subst, &offsets);
  details_label_->SetText(text);
  gfx::Range details_range(offsets[1], text.length());

  views::StyledLabel::RangeStyleInfo link_style =
      views::StyledLabel::RangeStyleInfo::CreateForLink();
  if (!ui::MaterialDesignController::IsSecondaryUiMaterial())
    link_style.font_style |= gfx::Font::FontStyle::UNDERLINE;
  link_style.disable_line_wrapping = false;

  details_label_->AddStyleRange(details_range, link_style);
}

void BubbleHeaderView::AddResetDecisionsLabel() {
  std::vector<base::string16> subst;
  subst.push_back(
      l10n_util::GetStringUTF16(IDS_PAGE_INFO_INVALID_CERTIFICATE_DESCRIPTION));
  subst.push_back(l10n_util::GetStringUTF16(
      IDS_PAGE_INFO_RESET_INVALID_CERTIFICATE_DECISIONS_BUTTON));

  std::vector<size_t> offsets;

  base::string16 text = base::ReplaceStringPlaceholders(
      base::ASCIIToUTF16("$1 $2"), subst, &offsets);
  reset_decisions_label_ = new views::StyledLabel(text, styled_label_listener_);
  reset_decisions_label_->set_id(STYLED_LABEL_RESET_CERTIFICATE_DECISIONS);
  gfx::Range link_range(offsets[1], text.length());

  views::StyledLabel::RangeStyleInfo link_style =
      views::StyledLabel::RangeStyleInfo::CreateForLink();
  if (!ui::MaterialDesignController::IsSecondaryUiMaterial())
    link_style.font_style |= gfx::Font::FontStyle::UNDERLINE;
  link_style.disable_line_wrapping = false;

  reset_decisions_label_->AddStyleRange(link_range, link_style);
  // Fit the styled label to occupy available width.
  reset_decisions_label_->SizeToFit(0);
  reset_decisions_label_container_->AddChildView(reset_decisions_label_);

  // Now that it contains a label, the container needs padding at the top.
  reset_decisions_label_container_->SetBorder(
      views::CreateEmptyBorder(8, 0, 0, 0));

  InvalidateLayout();
}

////////////////////////////////////////////////////////////////////////////////
// InternalPageInfoBubbleView
////////////////////////////////////////////////////////////////////////////////

InternalPageInfoBubbleView::InternalPageInfoBubbleView(
    views::View* anchor_view,
    gfx::NativeView parent_window,
    const GURL& url)
    : BubbleDialogDelegateView(anchor_view, views::BubbleBorder::TOP_LEFT) {
  g_shown_bubble_type = PageInfoBubbleView::BUBBLE_INTERNAL_PAGE;
  set_parent_window(parent_window);

  int text = IDS_PAGE_INFO_INTERNAL_PAGE;
  int icon = IDR_PRODUCT_LOGO_16;
  if (url.SchemeIs(extensions::kExtensionScheme)) {
    text = IDS_PAGE_INFO_EXTENSION_PAGE;
    icon = IDR_PLUGINS_FAVICON;
  } else if (url.SchemeIs(content::kViewSourceScheme)) {
    text = IDS_PAGE_INFO_VIEW_SOURCE_PAGE;
    // view-source scheme uses the same icon as chrome:// pages.
    icon = IDR_PRODUCT_LOGO_16;
  } else if (!url.SchemeIs(content::kChromeUIScheme) &&
             !url.SchemeIs(content::kChromeDevToolsScheme)) {
    NOTREACHED();
  }

  // Compensate for built-in vertical padding in the anchor view's image.
  set_anchor_view_insets(gfx::Insets(
      GetLayoutConstant(LOCATION_BAR_BUBBLE_ANCHOR_VERTICAL_INSET), 0));

  SetLayoutManager(new views::BoxLayout(views::BoxLayout::kHorizontal, kSpacing,
                                        kSpacing, kSpacing));
  set_margins(gfx::Insets());
  if (ChromeLayoutProvider::Get()->ShouldShowWindowIcon()) {
    views::ImageView* icon_view = new NonAccessibleImageView();
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    icon_view->SetImage(rb.GetImageSkiaNamed(icon));
    AddChildView(icon_view);
  }

  views::Label* label = new views::Label(l10n_util::GetStringUTF16(text));
  label->SetMultiLine(true);
  label->SetAllowCharacterBreak(true);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  AddChildView(label);

  views::BubbleDialogDelegateView::CreateBubble(this);
}

InternalPageInfoBubbleView::~InternalPageInfoBubbleView() {}

void InternalPageInfoBubbleView::OnWidgetDestroying(views::Widget* widget) {
  g_shown_bubble_type = PageInfoBubbleView::BUBBLE_NONE;
}

int InternalPageInfoBubbleView::GetDialogButtons() const {
  return ui::DIALOG_BUTTON_NONE;
}

////////////////////////////////////////////////////////////////////////////////
// PageInfoBubbleView
////////////////////////////////////////////////////////////////////////////////

PageInfoBubbleView::~PageInfoBubbleView() {}

// static
void PageInfoBubbleView::ShowBubble(
    views::View* anchor_view,
    views::WidgetObserver* widget_observer,
    const gfx::Rect& anchor_rect,
    Profile* profile,
    content::WebContents* web_contents,
    const GURL& url,
    const security_state::SecurityInfo& security_info) {
  gfx::NativeView parent_window =
      anchor_view ? nullptr : web_contents->GetNativeView();
  if (url.SchemeIs(content::kChromeUIScheme) ||
      url.SchemeIs(content::kChromeDevToolsScheme) ||
      url.SchemeIs(extensions::kExtensionScheme) ||
      url.SchemeIs(content::kViewSourceScheme)) {
    // Use the concrete type so that |SetAnchorRect| can be called as a friend.
    InternalPageInfoBubbleView* bubble =
        new InternalPageInfoBubbleView(anchor_view, parent_window, url);
    if (!anchor_view)
      bubble->SetAnchorRect(anchor_rect);
    if (widget_observer)
      bubble->GetWidget()->AddObserver(widget_observer);
    bubble->GetWidget()->Show();
    return;
  }
  PageInfoBubbleView* bubble = new PageInfoBubbleView(
      anchor_view, parent_window, profile, web_contents, url, security_info);
  if (!anchor_view)
    bubble->SetAnchorRect(anchor_rect);
  if (widget_observer)
    bubble->GetWidget()->AddObserver(widget_observer);
  bubble->GetWidget()->Show();
}

// static
PageInfoBubbleView::BubbleType PageInfoBubbleView::GetShownBubbleType() {
  return g_shown_bubble_type;
}

PageInfoBubbleView::PageInfoBubbleView(
    views::View* anchor_view,
    gfx::NativeView parent_window,
    Profile* profile,
    content::WebContents* web_contents,
    const GURL& url,
    const security_state::SecurityInfo& security_info)
    : content::WebContentsObserver(web_contents),
      BubbleDialogDelegateView(anchor_view, views::BubbleBorder::TOP_LEFT),
      profile_(profile),
      header_(nullptr),
      separator_(nullptr),
      site_settings_view_(nullptr),
      cookie_dialog_link_(nullptr),
      permissions_view_(nullptr),
      weak_factory_(this) {
  g_shown_bubble_type = BUBBLE_PAGE_INFO;
  set_parent_window(parent_window);

  // Compensate for built-in vertical padding in the anchor view's image.
  set_anchor_view_insets(gfx::Insets(
      GetLayoutConstant(LOCATION_BAR_BUBBLE_ANCHOR_VERTICAL_INSET), 0));

  // Capture the default bubble margin, and move it to the Layout classes. This
  // is necessary so that the views::Separator can extend the full width of the
  // bubble.
  const int side_margin = margins().left();
  DCHECK_EQ(margins().left(), margins().right());

  // Also remove the top margin from the client area so there is less space
  // below the dialog title.
  set_margins(gfx::Insets(0, 0, margins().bottom(), 0));

  views::GridLayout* layout = new views::GridLayout(this);
  SetLayoutManager(layout);

  // Use a single ColumnSet here. Otherwise the preferred width doesn't properly
  // propagate up to the dialog width.
  const int content_column = 0;
  views::ColumnSet* column_set = layout->AddColumnSet(content_column);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL, 1,
                        views::GridLayout::USE_PREF, 0, 0);

  header_ = new BubbleHeaderView(this, this, side_margin);
  layout->StartRow(1, content_column);
  layout->AddView(header_);

  layout->StartRow(0, content_column);
  separator_ = new views::Separator();
  layout->AddView(separator_);

  layout->AddPaddingRow(1, kHeaderMarginBottom);
  layout->StartRow(1, content_column);

  site_settings_view_ = CreateSiteSettingsView(side_margin);
  layout->AddView(site_settings_view_);

  if (!ui::MaterialDesignController::IsSecondaryUiMaterial()) {
    // In non-material, titles are inset from the dialog margin. Ensure the
    // horizontal insets match.
    set_title_margins(
        gfx::Insets(ChromeLayoutProvider::Get()->GetDistanceMetric(
                        DISTANCE_PANEL_CONTENT_MARGIN),
                    side_margin, 0, side_margin));
  }
  views::BubbleDialogDelegateView::CreateBubble(this);

  presenter_.reset(new PageInfo(
      this, profile, TabSpecificContentSettings::FromWebContents(web_contents),
      web_contents, url, security_info));
}

void PageInfoBubbleView::RenderFrameDeleted(
    content::RenderFrameHost* render_frame_host) {
  if (render_frame_host == web_contents()->GetMainFrame())
    GetWidget()->Close();
}

void PageInfoBubbleView::WebContentsDestroyed() {
  weak_factory_.InvalidateWeakPtrs();
}

void PageInfoBubbleView::OnPermissionChanged(
    const PageInfoUI::PermissionInfo& permission) {
  presenter_->OnSitePermissionChanged(permission.type, permission.setting);
  // The menu buttons for the permissions might have longer strings now, so we
  // need to layout and size the whole bubble.
  Layout();
  SizeToContents();
}

void PageInfoBubbleView::OnChosenObjectDeleted(
    const PageInfoUI::ChosenObjectInfo& info) {
  presenter_->OnSiteChosenObjectDeleted(info.ui_info, *info.object);
}

base::string16 PageInfoBubbleView::GetWindowTitle() const {
  return summary_text_;
}

bool PageInfoBubbleView::ShouldShowCloseButton() const {
  return true;
}

void PageInfoBubbleView::OnWidgetDestroying(views::Widget* widget) {
  g_shown_bubble_type = BUBBLE_NONE;
  presenter_->OnUIClosing();
}

int PageInfoBubbleView::GetDialogButtons() const {
  return ui::DIALOG_BUTTON_NONE;
}

const gfx::FontList& PageInfoBubbleView::GetTitleFontList() const {
  return ui::ResourceBundle::GetSharedInstance().GetFontListWithDelta(
      kSummaryFontSizeDelta);
}

void PageInfoBubbleView::ButtonPressed(views::Button* button,
                                       const ui::Event& event) {
  DCHECK_EQ(BUTTON_CLOSE, button->id());
  GetWidget()->Close();
}

void PageInfoBubbleView::LinkClicked(views::Link* source, int event_flags) {
  // The bubble closes automatically when the collected cookies dialog or the
  // certificate viewer opens. So delay handling of the link clicked to avoid
  // a crash in the base class which needs to complete the mouse event handling.
  content::BrowserThread::PostTask(
      content::BrowserThread::UI, FROM_HERE,
      base::BindOnce(&PageInfoBubbleView::HandleLinkClickedAsync,
                     weak_factory_.GetWeakPtr(), source));
}

gfx::Size PageInfoBubbleView::GetPreferredSize() const {
  if (header_ == nullptr && site_settings_view_ == nullptr)
    return views::View::GetPreferredSize();

  int height = 0;
  if (header_)
    height += header_->GetPreferredSize().height() + kHeaderMarginBottom;
  if (separator_)
    height += separator_->GetPreferredSize().height();

  if (site_settings_view_)
    height += site_settings_view_->GetPreferredSize().height();

  int width = kMinBubbleWidth;
  if (site_settings_view_)
    width = std::max(width, site_settings_view_->GetPreferredSize().width());
  width = std::min(width, kMaxBubbleWidth);
  return gfx::Size(width, height);
}

void PageInfoBubbleView::SetCookieInfo(const CookieInfoList& cookie_info_list) {
  // |cookie_info_list| should only ever have 2 items: first- and third-party
  // cookies.
  DCHECK_EQ(cookie_info_list.size(), 2u);
  int total_allowed = 0;
  for (const auto& i : cookie_info_list)
    total_allowed += i.allowed;
  base::string16 label_text = l10n_util::GetPluralStringFUTF16(
      IDS_PAGE_INFO_NUM_COOKIES, total_allowed);

  cookie_dialog_link_->SetText(label_text);
  Layout();
  SizeToContents();
}

void PageInfoBubbleView::SetPermissionInfo(
    const PermissionInfoList& permission_info_list,
    ChosenObjectInfoList chosen_object_info_list) {
  // When a permission is changed, PageInfo::OnSitePermissionChanged()
  // calls this method with updated permissions. However, PermissionSelectorRow
  // will have already updated its state, so it's already reflected in the UI.
  // In addition, if a permission is set to the default setting, PageInfo
  // removes it from |permission_info_list|, but the button should remain.
  if (permissions_view_)
    return;

  permissions_view_ = new views::View();
  views::GridLayout* layout = new views::GridLayout(permissions_view_);
  permissions_view_->SetLayoutManager(layout);

  site_settings_view_->AddChildView(permissions_view_);

  const int content_column = 0;
  views::ColumnSet* column_set = layout->AddColumnSet(content_column);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL, 1,
                        views::GridLayout::USE_PREF, 0, 0);
  const int permissions_column = 1;
  views::ColumnSet* permissions_set = layout->AddColumnSet(permissions_column);
  permissions_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL,
                             0, views::GridLayout::FIXED,
                             kPermissionIconColumnWidth, 0);
  permissions_set->AddPaddingColumn(0, kPermissionIconMarginLeft);
  permissions_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL,
                             0, views::GridLayout::USE_PREF, 0, 0);
  permissions_set->AddPaddingColumn(1, kPermissionMenuSpacing);
  permissions_set->AddColumn(views::GridLayout::TRAILING,
                             views::GridLayout::FILL, 0,
                             views::GridLayout::USE_PREF, 0, 0);
  for (const auto& permission : permission_info_list) {
    layout->StartRow(1, permissions_column);
    std::unique_ptr<PermissionSelectorRow> selector =
        base::MakeUnique<PermissionSelectorRow>(
            profile_,
            web_contents() ? web_contents()->GetVisibleURL()
                           : GURL::EmptyGURL(),
            permission, layout);
    selector->AddObserver(this);
    layout->AddPaddingRow(1, kPermissionsVerticalSpacing);
    selector_rows_.push_back(std::move(selector));
  }

  for (auto& object : chosen_object_info_list) {
    layout->StartRow(1, content_column);
    // The view takes ownership of the object info.
    auto* object_view = new ChosenObjectRow(std::move(object));
    object_view->AddObserver(this);
    layout->AddView(object_view, 1, 1, views::GridLayout::LEADING,
                    views::GridLayout::CENTER);
    layout->AddPaddingRow(1, kPermissionsVerticalSpacing);
  }

  layout->Layout(permissions_view_);

  // Add site settings link.
  views::Link* site_settings_link = new views::Link(
      l10n_util::GetStringUTF16(IDS_PAGE_INFO_SITE_SETTINGS_LINK));
  site_settings_link->set_id(LINK_SITE_SETTINGS);
  site_settings_link->set_listener(this);
  views::View* link_section = new views::View();
  const int kLinkMarginTop = 4;
  link_section->SetLayoutManager(new views::BoxLayout(
      views::BoxLayout::kHorizontal, 0, kLinkMarginTop, 0));
  link_section->AddChildView(site_settings_link);
  site_settings_view_->AddChildView(link_section);

  SizeToContents();
}

void PageInfoBubbleView::SetIdentityInfo(const IdentityInfo& identity_info) {
  std::unique_ptr<PageInfoUI::SecurityDescription> security_description =
      identity_info.GetSecurityDescription();

  summary_text_ = security_description->summary;
  GetWidget()->UpdateWindowTitle();

  if (identity_info.certificate) {
    certificate_ = identity_info.certificate;

    if (identity_info.show_ssl_decision_revoke_button)
      header_->AddResetDecisionsLabel();

    if (PageInfoUI::ShouldShowCertificateLink()) {
      // The text of link to the Certificate Viewer varies depending on the
      // validity of the Certificate.
      const bool valid_identity = (identity_info.identity_status !=
                                   PageInfo::SITE_IDENTITY_STATUS_ERROR);
      const base::string16 link_title = l10n_util::GetStringUTF16(
          valid_identity ? IDS_PAGE_INFO_CERTIFICATE_VALID_LINK
                         : IDS_PAGE_INFO_CERTIFICATE_INVALID_LINK);

      // Create the link to add to the Certificate Section.
      views::Link* inspect_link = new views::Link(link_title);
      inspect_link->set_id(LINK_CERTIFICATE_VIEWER);
      inspect_link->set_listener(this);
      if (valid_identity) {
        inspect_link->SetTooltipText(l10n_util::GetStringFUTF16(
            IDS_PAGE_INFO_CERTIFICATE_VALID_LINK_TOOLTIP,
            base::UTF8ToUTF16(certificate_->issuer().GetDisplayName())));
      }

      // Add the Certificate Section.
      site_settings_view_->AddChildViewAt(
          CreateInspectLinkSection(PageInfoUI::GetCertificateIcon(),
                                   IDS_PAGE_INFO_CERTIFICATE, inspect_link),
          0);
    }
  }

  header_->SetDetails(security_description->details);

  Layout();
  SizeToContents();
}

views::View* PageInfoBubbleView::CreateSiteSettingsView(int side_margin) {
  views::View* site_settings_view = new views::View();
  views::BoxLayout* box_layout =
      new views::BoxLayout(views::BoxLayout::kVertical, side_margin, 0, 0);
  site_settings_view->SetLayoutManager(box_layout);
  box_layout->set_cross_axis_alignment(
      views::BoxLayout::CROSS_AXIS_ALIGNMENT_STRETCH);

  // Create the link and icon for the Certificate section.
  cookie_dialog_link_ = new views::Link(
      l10n_util::GetPluralStringFUTF16(IDS_PAGE_INFO_NUM_COOKIES, 0));
  cookie_dialog_link_->set_id(LINK_COOKIE_DIALOG);
  cookie_dialog_link_->set_listener(this);

  PageInfoUI::PermissionInfo info;
  info.type = CONTENT_SETTINGS_TYPE_COOKIES;
  info.setting = CONTENT_SETTING_ALLOW;
  info.is_incognito =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext())
          ->IsOffTheRecord();

  const gfx::ImageSkia icon = PageInfoUI::GetPermissionIcon(info).AsImageSkia();
  // Add the Cookies section.
  site_settings_view->AddChildView(CreateInspectLinkSection(
      icon, IDS_PAGE_INFO_COOKIES, cookie_dialog_link_));

  return site_settings_view;
}

void PageInfoBubbleView::HandleLinkClickedAsync(views::Link* source) {
  // All switch cases require accessing web_contents(), so we check it here.
  if (web_contents() == nullptr || web_contents()->IsBeingDestroyed())
    return;
  switch (source->id()) {
    case LINK_SITE_SETTINGS:
      // TODO(crbug.com/655876): This opens the general Content Settings pane,
      // which is OK for now. But on Android, it opens a page specific to a
      // given origin that shows all of the settings for that origin. If/when
      // that's available on desktop we should link to that here, too.
      web_contents()->OpenURL(content::OpenURLParams(
          GURL(chrome::kChromeUIContentSettingsURL), content::Referrer(),
          WindowOpenDisposition::NEW_FOREGROUND_TAB, ui::PAGE_TRANSITION_LINK,
          false));
      presenter_->RecordPageInfoAction(
          PageInfo::PAGE_INFO_SITE_SETTINGS_OPENED);
      break;
    case LINK_COOKIE_DIALOG:
      // Count how often the Collected Cookies dialog is opened.
      presenter_->RecordPageInfoAction(
          PageInfo::PAGE_INFO_COOKIES_DIALOG_OPENED);
      new CollectedCookiesViews(web_contents());
      break;
    case LINK_CERTIFICATE_VIEWER: {
      gfx::NativeWindow top_window = web_contents()->GetTopLevelNativeWindow();
      if (certificate_ && top_window) {
        presenter_->RecordPageInfoAction(
            PageInfo::PAGE_INFO_CERTIFICATE_DIALOG_OPENED);
        ShowCertificateViewer(web_contents(), top_window, certificate_.get());
      }
      break;
    }
    default:
      NOTREACHED();
  }
}

void PageInfoBubbleView::StyledLabelLinkClicked(views::StyledLabel* label,
                                                const gfx::Range& range,
                                                int event_flags) {
  switch (label->id()) {
    case STYLED_LABEL_SECURITY_DETAILS:
      web_contents()->OpenURL(content::OpenURLParams(
          GURL(chrome::kPageInfoHelpCenterURL), content::Referrer(),
          WindowOpenDisposition::NEW_FOREGROUND_TAB, ui::PAGE_TRANSITION_LINK,
          false));
      presenter_->RecordPageInfoAction(
          PageInfo::PAGE_INFO_CONNECTION_HELP_OPENED);
      break;
    case STYLED_LABEL_RESET_CERTIFICATE_DECISIONS:
      presenter_->OnRevokeSSLErrorBypassButtonPressed();
      GetWidget()->Close();
      break;
    default:
      NOTREACHED();
  }
}
