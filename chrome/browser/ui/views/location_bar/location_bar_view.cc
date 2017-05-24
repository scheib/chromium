// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/location_bar_view.h"

#include <algorithm>
#include <map>

#include "base/i18n/rtl.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/command_updater.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/extensions/api/omnibox/omnibox_api.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/translate/translate_service.h"
#include "chrome/browser/ui/autofill/save_card_bubble_controller_impl.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/content_settings/content_setting_bubble_model.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/omnibox/chrome_omnibox_client.h"
#include "chrome/browser/ui/passwords/manage_passwords_ui_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/autofill/save_card_icon_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/background_with_1_px_border.h"
#include "chrome/browser/ui/views/location_bar/content_setting_image_view.h"
#include "chrome/browser/ui/views/location_bar/keyword_hint_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_layout.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/location_bar/selected_keyword_view.h"
#include "chrome/browser/ui/views/location_bar/star_view.h"
#include "chrome/browser/ui/views/location_bar/zoom_bubble_view.h"
#include "chrome/browser/ui/views/location_bar/zoom_view.h"
#include "chrome/browser/ui/views/passwords/manage_passwords_bubble_view.h"
#include "chrome/browser/ui/views/passwords/manage_passwords_icon_views.h"
#include "chrome/browser/ui/views/translate/translate_bubble_view.h"
#include "chrome/browser/ui/views/translate/translate_icon_view.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/theme_resources.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/grit/components_scaled_resources.h"
#include "components/omnibox/browser/omnibox_popup_model.h"
#include "components/omnibox/browser/omnibox_popup_view.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/toolbar/toolbar_model.h"
#include "components/translate/core/browser/language_state.h"
#include "components/variations/variations_associated_data.h"
#include "components/zoom/zoom_controller.h"
#include "components/zoom/zoom_event_manager.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/feature_switch.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/material_design/material_design_controller.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/theme_provider.h"
#include "ui/compositor/paint_recorder.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/skia_util.h"
#include "ui/gfx/text_utils.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/button_drag_utils.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"

#if !defined(OS_CHROMEOS)
#include "chrome/browser/ui/views/first_run_bubble.h"
#endif

using content::WebContents;
using views::View;


// LocationBarView -----------------------------------------------------------

// static
const char LocationBarView::kViewClassName[] = "LocationBarView";

LocationBarView::LocationBarView(Browser* browser,
                                 Profile* profile,
                                 CommandUpdater* command_updater,
                                 Delegate* delegate,
                                 bool is_popup_mode)
    : LocationBar(profile),
      ChromeOmniboxEditController(command_updater),
      browser_(browser),
      omnibox_view_(nullptr),
      delegate_(delegate),
      location_icon_view_(nullptr),
      ime_inline_autocomplete_view_(nullptr),
      selected_keyword_view_(nullptr),
      keyword_hint_view_(nullptr),
      zoom_view_(nullptr),
      manage_passwords_icon_view_(nullptr),
      save_credit_card_icon_view_(nullptr),
      translate_icon_view_(nullptr),
      star_view_(nullptr),
      size_animation_(this),
      is_popup_mode_(is_popup_mode),
      show_focus_rect_(false),
      template_url_service_(NULL) {
  edit_bookmarks_enabled_.Init(
      bookmarks::prefs::kEditBookmarksEnabled, profile->GetPrefs(),
      base::Bind(&LocationBarView::UpdateWithoutTabRestore,
                 base::Unretained(this)));

  zoom::ZoomEventManager::GetForBrowserContext(profile)
      ->AddZoomEventManagerObserver(this);
}

LocationBarView::~LocationBarView() {
  if (template_url_service_)
    template_url_service_->RemoveObserver(this);

  zoom::ZoomEventManager::GetForBrowserContext(profile())
      ->RemoveZoomEventManagerObserver(this);
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, public:

void LocationBarView::Init() {
  // We need to be in a Widget, otherwise GetNativeTheme() may change and we're
  // not prepared for that.
  DCHECK(GetWidget());

  // Make sure children with layers are clipped. See http://crbug.com/589497
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetMasksToBounds(true);

  // Determine the main font.
  gfx::FontList font_list = ResourceBundle::GetSharedInstance().GetFontList(
      ResourceBundle::BaseFont);
  const int current_font_size = font_list.GetFontSize();
  constexpr int kDesiredFontSize = 14;
  if (current_font_size != kDesiredFontSize) {
    font_list =
        font_list.DeriveWithSizeDelta(kDesiredFontSize - current_font_size);
  }
  // Shrink large fonts to make them fit.
  // TODO(pkasting): Stretch the location bar instead in this case.
  const int vertical_padding = GetTotalVerticalPadding();
  const int location_height =
      std::max(GetPreferredSize().height() - (vertical_padding * 2), 0);
  font_list = font_list.DeriveWithHeightUpperBound(location_height);

  // Determine the font for use inside the bubbles.
  const int bubble_padding =
      kBubbleVerticalPadding +
      GetLayoutConstant(LOCATION_BAR_BUBBLE_FONT_VERTICAL_PADDING);
  const int bubble_height = location_height - (bubble_padding * 2);

  const SkColor background_color = GetColor(BACKGROUND);
  location_icon_view_ = new LocationIconView(font_list, this);
  location_icon_view_->set_drag_controller(this);
  AddChildView(location_icon_view_);

  // Initialize the Omnibox view.
  omnibox_view_ = new OmniboxViewViews(
      this, base::MakeUnique<ChromeOmniboxClient>(this, profile()),
      command_updater(), is_popup_mode_, this, font_list);
  omnibox_view_->Init();
  AddChildView(omnibox_view_);

  // Initialize the inline autocomplete view which is visible only when IME is
  // turned on.  Use the same font with the omnibox and highlighted background.
  ime_inline_autocomplete_view_ =
      new views::Label(base::string16(), {font_list});
  ime_inline_autocomplete_view_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  ime_inline_autocomplete_view_->SetAutoColorReadabilityEnabled(false);
  ime_inline_autocomplete_view_->set_background(
      views::Background::CreateSolidBackground(GetNativeTheme()->GetSystemColor(
          ui::NativeTheme::kColorId_TextfieldSelectionBackgroundFocused)));
  ime_inline_autocomplete_view_->SetEnabledColor(
      GetNativeTheme()->GetSystemColor(
          ui::NativeTheme::kColorId_TextfieldSelectionColor));
  ime_inline_autocomplete_view_->SetVisible(false);
  AddChildView(ime_inline_autocomplete_view_);

  selected_keyword_view_ = new SelectedKeywordView(font_list, profile());
  AddChildView(selected_keyword_view_);

  gfx::FontList bubble_font_list =
      font_list.DeriveWithHeightUpperBound(bubble_height);
  keyword_hint_view_ = new KeywordHintView(
      this, profile(), font_list, bubble_font_list,
      GetColor(LocationBarView::DEEMPHASIZED_TEXT), background_color);
  AddChildView(keyword_hint_view_);

  std::vector<std::unique_ptr<ContentSettingImageModel>> models =
      ContentSettingImageModel::GenerateContentSettingImageModels();
  for (auto& model : models) {
    ContentSettingImageView* image_view =
        new ContentSettingImageView(std::move(model), this, font_list);
    content_setting_views_.push_back(image_view);
    image_view->SetVisible(false);
    AddChildView(image_view);
  }

  zoom_view_ = new ZoomView(delegate_);
  zoom_view_->Init();
  AddChildView(zoom_view_);

  manage_passwords_icon_view_ = new ManagePasswordsIconViews(command_updater());
  manage_passwords_icon_view_->Init();
  AddChildView(manage_passwords_icon_view_);

  save_credit_card_icon_view_ =
      new autofill::SaveCardIconView(command_updater(), browser_);
  save_credit_card_icon_view_->Init();
  save_credit_card_icon_view_->SetVisible(false);
  AddChildView(save_credit_card_icon_view_);

  translate_icon_view_ = new TranslateIconView(command_updater());
  translate_icon_view_->Init();
  translate_icon_view_->SetVisible(false);
  AddChildView(translate_icon_view_);

  star_view_ = new StarView(command_updater(), browser_);
  star_view_->Init();
  star_view_->SetVisible(false);
  AddChildView(star_view_);

  // Initialize the location entry. We do this to avoid a black flash which is
  // visible when the location entry has just been initialized.
  Update(nullptr);

  size_animation_.Reset(1);
}

bool LocationBarView::IsInitialized() const {
  return omnibox_view_ != nullptr;
}

SkColor LocationBarView::GetColor(
    ColorKind kind) const {
  const ui::NativeTheme* native_theme = GetNativeTheme();
  switch (kind) {
    case BACKGROUND:
      return native_theme->GetSystemColor(
          ui::NativeTheme::kColorId_TextfieldDefaultBackground);

    case TEXT:
      return native_theme->GetSystemColor(
          ui::NativeTheme::kColorId_TextfieldDefaultColor);

    case SELECTED_TEXT:
      return native_theme->GetSystemColor(
          ui::NativeTheme::kColorId_TextfieldSelectionColor);

    case DEEMPHASIZED_TEXT:
      return color_utils::AlphaBlend(GetColor(TEXT), GetColor(BACKGROUND), 128);

    case SECURITY_CHIP_TEXT:
      return GetSecureTextColor(GetToolbarModel()->GetSecurityLevel(false));
  }
  NOTREACHED();
  return gfx::kPlaceholderColor;
}

SkColor LocationBarView::GetOpaqueBorderColor(bool incognito) const {
  return color_utils::GetResultingPaintColor(
      GetBorderColor(), ThemeProperties::GetDefaultColor(
                            ThemeProperties::COLOR_TOOLBAR, incognito));
}

SkColor LocationBarView::GetSecureTextColor(
    security_state::SecurityLevel security_level) const {
  if (security_level == security_state::SECURE_WITH_POLICY_INSTALLED_CERT) {
    return GetColor(DEEMPHASIZED_TEXT);
  }

  SkColor text_color = GetColor(TEXT);
  if (!color_utils::IsDark(GetColor(BACKGROUND))) {
    if ((security_level == security_state::EV_SECURE) ||
        (security_level == security_state::SECURE)) {
      text_color = gfx::kGoogleGreen700;
    } else if (security_level == security_state::DANGEROUS) {
      text_color = gfx::kGoogleRed700;
    }
  }
  return color_utils::GetReadableColor(text_color, GetColor(BACKGROUND));
}

void LocationBarView::ZoomChangedForActiveTab(bool can_show_bubble) {
  DCHECK(zoom_view_);
  if (RefreshZoomView()) {
    Layout();
    SchedulePaint();
  }

  WebContents* web_contents = GetWebContents();
  if (can_show_bubble && web_contents) {
    ZoomBubbleView::ShowBubble(web_contents, gfx::Point(),
                               ZoomBubbleView::AUTOMATIC);
  }
}

void LocationBarView::SetStarToggled(bool on) {
  if (star_view_)
    star_view_->SetToggled(on);
}

gfx::Point LocationBarView::GetOmniboxViewOrigin() const {
  gfx::Point origin(omnibox_view_->origin());
  origin.set_x(GetMirroredXInView(origin.x()));
  views::View::ConvertPointToScreen(this, &origin);
  return origin;
}

void LocationBarView::SetImeInlineAutocompletion(const base::string16& text) {
  ime_inline_autocomplete_view_->SetText(text);
  ime_inline_autocomplete_view_->SetVisible(!text.empty());
}

void LocationBarView::SetShowFocusRect(bool show) {
  show_focus_rect_ = show;
  SchedulePaint();
}

void LocationBarView::SelectAll() {
  omnibox_view_->SelectAll(true);
}

gfx::Point LocationBarView::GetInfoBarAnchorPoint() const {
  const views::ImageView* image = location_icon_view_->GetImageView();
  const gfx::Rect image_bounds(image->GetImageBounds());
  gfx::Point point(image_bounds.CenterPoint().x(), image_bounds.bottom());
  ConvertPointToTarget(image, this, &point);
  return point;
}

views::View* LocationBarView::GetSecurityBubbleAnchorView() {
  if (ui::MaterialDesignController::IsSecondaryUiMaterial())
    return this;
  return location_icon_view()->GetImageView();
}

void LocationBarView::GetOmniboxPopupPositioningInfo(
    gfx::Point* top_left_screen_coord,
    int* popup_width,
    int* left_margin,
    int* right_margin,
    int top_edge_overlap) {
  *top_left_screen_coord = gfx::Point(0, parent()->height() - top_edge_overlap);
  views::View::ConvertPointToScreen(parent(), top_left_screen_coord);

  *popup_width = parent()->width();
  *left_margin = x();
  *right_margin = *popup_width - bounds().right();
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, public LocationBar implementation:

void LocationBarView::FocusLocation(bool select_all) {
  omnibox_view_->SetFocus();
  if (select_all)
    omnibox_view_->SelectAll(true);
}

void LocationBarView::Revert() {
  omnibox_view_->RevertAll();
}

OmniboxView* LocationBarView::GetOmniboxView() {
  return omnibox_view_;
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, public views::View implementation:

bool LocationBarView::HasFocus() const {
  return omnibox_view_->model()->has_focus();
}

void LocationBarView::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  node_data->role = ui::AX_ROLE_GROUP;
}

gfx::Size LocationBarView::GetPreferredSize() const {
  // Compute minimum height.
  gfx::Size min_size(0, GetLayoutConstant(LOCATION_BAR_HEIGHT));

  if (!IsInitialized())
    return min_size;

  min_size.set_height(min_size.height() * size_animation_.GetCurrentValue());

  // Compute width of omnibox-leading content.
  const int edge_thickness = GetHorizontalEdgeThickness();
  int leading_width = edge_thickness;
  if (ShouldShowKeywordBubble()) {
    // The selected keyword view can collapse completely.
  } else if (ShouldShowLocationIconText()) {
    leading_width +=
        location_icon_view_->GetMinimumSizeForLabelText(GetLocationIconText())
            .width();
  } else {
    leading_width += GetLayoutConstant(LOCATION_BAR_ELEMENT_PADDING) +
                     location_icon_view_->GetMinimumSize().width();
  }

  // Compute width of omnibox-trailing content.
  int trailing_width = edge_thickness;
  trailing_width += IncrementalMinimumWidth(star_view_) +
                    IncrementalMinimumWidth(translate_icon_view_) +
                    IncrementalMinimumWidth(save_credit_card_icon_view_) +
                    IncrementalMinimumWidth(manage_passwords_icon_view_) +
                    IncrementalMinimumWidth(zoom_view_);
  for (auto i = content_setting_views_.begin();
       i != content_setting_views_.end(); ++i) {
    trailing_width += IncrementalMinimumWidth((*i));
  }

  min_size.set_width(leading_width + omnibox_view_->GetMinimumSize().width() +
                     2 * GetLayoutConstant(LOCATION_BAR_ELEMENT_PADDING) -
                     omnibox_view_->GetInsets().width() + trailing_width);
  return min_size;
}

void LocationBarView::Layout() {
  if (!IsInitialized())
    return;

  selected_keyword_view_->SetVisible(false);
  location_icon_view_->SetVisible(false);
  keyword_hint_view_->SetVisible(false);

  const int item_padding = GetLayoutConstant(LOCATION_BAR_ELEMENT_PADDING);

  LocationBarLayout leading_decorations(
      LocationBarLayout::LEFT_EDGE, item_padding,
      item_padding - omnibox_view_->GetInsets().left());
  LocationBarLayout trailing_decorations(LocationBarLayout::RIGHT_EDGE,
                                         item_padding, item_padding);

  const base::string16 keyword(omnibox_view_->model()->keyword());
  // In some cases (e.g. fullscreen mode) we may have 0 height.  We still want
  // to position our child views in this case, because other things may be
  // positioned relative to them (e.g. the "bookmark added" bubble if the user
  // hits ctrl-d).
  const int vertical_padding = GetTotalVerticalPadding();
  const int location_height = std::max(height() - (vertical_padding * 2), 0);
  // The largest fraction of the omnibox that can be taken by the EV or search
  // label/chip.
  const double kLeadingDecorationMaxFraction = 0.5;

  location_icon_view_->SetLabel(base::string16());
  if (ShouldShowKeywordBubble()) {
    leading_decorations.AddDecoration(
        vertical_padding, location_height, false, kLeadingDecorationMaxFraction,
        item_padding, item_padding, selected_keyword_view_);
    if (selected_keyword_view_->keyword() != keyword) {
      selected_keyword_view_->SetKeyword(keyword);
      const TemplateURL* template_url =
          TemplateURLServiceFactory::GetForProfile(profile())->
          GetTemplateURLForKeyword(keyword);
      if (template_url &&
          (template_url->type() == TemplateURL::OMNIBOX_API_EXTENSION)) {
        gfx::Image image = extensions::OmniboxAPI::Get(profile())->
            GetOmniboxIcon(template_url->GetExtensionId());
        selected_keyword_view_->SetImage(image.AsImageSkia());
      } else {
        selected_keyword_view_->ResetImage();
      }
    }
  } else if (ShouldShowLocationIconText()) {
    location_icon_view_->SetLabel(GetLocationIconText());
    leading_decorations.AddDecoration(
        vertical_padding, location_height, false, kLeadingDecorationMaxFraction,
        item_padding, item_padding, location_icon_view_);
  } else {
    leading_decorations.AddDecoration(vertical_padding, location_height,
                                      location_icon_view_);
  }

  if (star_view_->visible()) {
    trailing_decorations.AddDecoration(vertical_padding, location_height,
                                       star_view_);
  }
  if (translate_icon_view_->visible()) {
    trailing_decorations.AddDecoration(vertical_padding, location_height,
                                       translate_icon_view_);
  }
  if (save_credit_card_icon_view_->visible()) {
    trailing_decorations.AddDecoration(vertical_padding, location_height,
                                       save_credit_card_icon_view_);
  }
  if (manage_passwords_icon_view_->visible()) {
    trailing_decorations.AddDecoration(vertical_padding, location_height,
                                       manage_passwords_icon_view_);
  }
  if (zoom_view_->visible()) {
    trailing_decorations.AddDecoration(vertical_padding, location_height,
                                       zoom_view_);
  }
  for (ContentSettingViews::const_reverse_iterator i(
           content_setting_views_.rbegin()); i != content_setting_views_.rend();
       ++i) {
    if ((*i)->visible()) {
      trailing_decorations.AddDecoration(vertical_padding, location_height,
                                         false, 0, item_padding, item_padding,
                                         *i);
    }
  }
  // Because IMEs may eat the tab key, we don't show "press tab to search" while
  // IME composition is in progress.
  if (HasFocus() && !keyword.empty() &&
      omnibox_view_->model()->is_keyword_hint() &&
      !omnibox_view_->IsImeComposing()) {
    trailing_decorations.AddDecoration(vertical_padding, location_height, true,
                                       0, item_padding, item_padding,
                                       keyword_hint_view_);
    if (keyword_hint_view_->keyword() != keyword)
      keyword_hint_view_->SetKeyword(keyword);
  }

  const int edge_thickness = GetHorizontalEdgeThickness();

  // Perform layout.
  int full_width = width() - (2 * edge_thickness);

  int entry_width = full_width;
  leading_decorations.LayoutPass1(&entry_width);
  trailing_decorations.LayoutPass1(&entry_width);
  leading_decorations.LayoutPass2(&entry_width);
  trailing_decorations.LayoutPass2(&entry_width);

  int location_needed_width = omnibox_view_->GetTextWidth();
  int available_width = entry_width - location_needed_width;
  // The bounds must be wide enough for all the decorations to fit.
  gfx::Rect location_bounds(edge_thickness, vertical_padding,
                            std::max(full_width, full_width - entry_width),
                            location_height);
  leading_decorations.LayoutPass3(&location_bounds, &available_width);
  trailing_decorations.LayoutPass3(&location_bounds, &available_width);

  // Layout |ime_inline_autocomplete_view_| next to the user input.
  if (ime_inline_autocomplete_view_->visible()) {
    int width =
        gfx::GetStringWidth(ime_inline_autocomplete_view_->text(),
                            ime_inline_autocomplete_view_->font_list()) +
        ime_inline_autocomplete_view_->GetInsets().width();
    // All the target languages (IMEs) are LTR, and we do not need to support
    // RTL so far.  In other words, no testable RTL environment so far.
    int x = location_needed_width;
    if (width > entry_width)
      x = 0;
    else if (location_needed_width + width > entry_width)
      x = entry_width - width;
    location_bounds.set_width(x);
    ime_inline_autocomplete_view_->SetBounds(
        location_bounds.right(), location_bounds.y(),
        std::min(width, entry_width), location_bounds.height());
  }
  omnibox_view_->SetBoundsRect(location_bounds);
}

void LocationBarView::OnNativeThemeChanged(const ui::NativeTheme* theme) {
  RefreshLocationIcon();
  if (is_popup_mode_) {
    set_background(
        views::Background::CreateSolidBackground(GetColor(BACKGROUND)));
  } else {
    // This border color will be blended on top of the toolbar (which may use an
    // image in the case of themes).
    set_background(
        new BackgroundWith1PxBorder(GetColor(BACKGROUND), GetBorderColor()));
  }
  SchedulePaint();
}

void LocationBarView::Update(const WebContents* contents) {
  RefreshContentSettingViews();
  RefreshZoomView();
  RefreshTranslateIcon();
  RefreshSaveCreditCardIconView();
  RefreshManagePasswordsIconView();

  if (star_view_)
    UpdateBookmarkStarVisibility();

  if (contents)
    omnibox_view_->OnTabChanged(contents);
  else
    omnibox_view_->Update();

  location_icon_view_->SetTextVisibility(
      ShouldShowLocationIconText(),
      !contents && ShouldAnimateLocationIconTextVisibilityChange());
  OnChanged();  // NOTE: Calls Layout().
}

void LocationBarView::ResetTabState(WebContents* contents) {
  omnibox_view_->ResetTabState(contents);
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, public OmniboxEditController implementation:

void LocationBarView::UpdateWithoutTabRestore() {
  Update(nullptr);
}

ToolbarModel* LocationBarView::GetToolbarModel() {
  return delegate_->GetToolbarModel();
}

WebContents* LocationBarView::GetWebContents() {
  return delegate_->GetWebContents();
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, private:

int LocationBarView::IncrementalMinimumWidth(views::View* view) const {
  return view->visible() ? (GetLayoutConstant(LOCATION_BAR_ELEMENT_PADDING) +
                            view->GetMinimumSize().width())
                         : 0;
}

SkColor LocationBarView::GetBorderColor() const {
  return GetThemeProvider()->GetColor(
      ThemeProperties::COLOR_LOCATION_BAR_BORDER);
}

int LocationBarView::GetHorizontalEdgeThickness() const {
  return is_popup_mode_
             ? 0
             : BackgroundWith1PxBorder::kLocationBarBorderThicknessDip;
}

int LocationBarView::GetTotalVerticalPadding() const {
  return BackgroundWith1PxBorder::kLocationBarBorderThicknessDip +
         GetLayoutConstant(LOCATION_BAR_ELEMENT_PADDING);
}

void LocationBarView::RefreshLocationIcon() {
  // |omnibox_view_| may not be ready yet if Init() has not been called. The
  // icon will be set soon by OnChanged().
  if (!omnibox_view_)
    return;

  security_state::SecurityLevel security_level =
      GetToolbarModel()->GetSecurityLevel(false);
  SkColor icon_color = (security_level == security_state::NONE ||
                        security_level == security_state::HTTP_SHOW_WARNING)
                           ? color_utils::DeriveDefaultIconColor(GetColor(TEXT))
                           : GetSecureTextColor(security_level);
  location_icon_view_->SetImage(gfx::CreateVectorIcon(
      omnibox_view_->GetVectorIcon(), kIconWidth, icon_color));
  location_icon_view_->SetEnabled(!omnibox_view_->IsEditingOrEmpty());
}

bool LocationBarView::RefreshContentSettingViews() {
  bool visibility_changed = false;
  for (ContentSettingViews::const_iterator i(content_setting_views_.begin());
       i != content_setting_views_.end(); ++i) {
    const bool was_visible = (*i)->visible();
    (*i)->Update(GetToolbarModel()->input_in_progress() ? nullptr
                                                        : GetWebContents());
    if (was_visible != (*i)->visible())
      visibility_changed = true;
  }
  return visibility_changed;
}

bool LocationBarView::RefreshZoomView() {
  DCHECK(zoom_view_);
  WebContents* web_contents = GetWebContents();
  if (!web_contents)
    return false;
  const bool was_visible = zoom_view_->visible();
  zoom_view_->Update(zoom::ZoomController::FromWebContents(web_contents));
  return was_visible != zoom_view_->visible();
}

void LocationBarView::OnDefaultZoomLevelChanged() {
  RefreshZoomView();
}

void LocationBarView::ButtonPressed(views::Button* sender,
                                    const ui::Event& event) {
  DCHECK_EQ(keyword_hint_view_, sender);
  DCHECK(event.IsMouseEvent() || event.IsGestureEvent());
  omnibox_view_->model()->AcceptKeyword(
      event.IsMouseEvent() ? KeywordModeEntryMethod::CLICK_ON_VIEW
                           : KeywordModeEntryMethod::TAP_ON_VIEW);
}

bool LocationBarView::RefreshSaveCreditCardIconView() {
  WebContents* web_contents = GetWebContents();
  if (!web_contents)
    return false;

  const bool was_visible = save_credit_card_icon_view_->visible();
  // |controller| may be nullptr due to lazy initialization.
  autofill::SaveCardBubbleControllerImpl* controller =
      autofill::SaveCardBubbleControllerImpl::FromWebContents(web_contents);
  bool enabled = controller && controller->IsIconVisible();
  command_updater()->UpdateCommandEnabled(IDC_SAVE_CREDIT_CARD_FOR_PAGE,
                                          enabled);
  save_credit_card_icon_view_->SetVisible(enabled);

  return was_visible != save_credit_card_icon_view_->visible();
}

void LocationBarView::RefreshTranslateIcon() {
  WebContents* web_contents = GetWebContents();
  if (!web_contents)
    return;
  translate::LanguageState& language_state =
      ChromeTranslateClient::FromWebContents(web_contents)->GetLanguageState();
  bool enabled = language_state.translate_enabled();
  command_updater()->UpdateCommandEnabled(IDC_TRANSLATE_PAGE, enabled);
  translate_icon_view_->SetVisible(enabled);
  if (!enabled)
    TranslateBubbleView::CloseCurrentBubble();
}

bool LocationBarView::RefreshManagePasswordsIconView() {
  DCHECK(manage_passwords_icon_view_);
  WebContents* web_contents = GetWebContents();
  if (!web_contents)
    return false;
  const bool was_visible = manage_passwords_icon_view_->visible();
  ManagePasswordsUIController::FromWebContents(
      web_contents)->UpdateIconAndBubbleState(manage_passwords_icon_view_);
  return was_visible != manage_passwords_icon_view_->visible();
}

void LocationBarView::ShowFirstRunBubbleInternal() {
  // First run bubble doesn't make sense for Chrome OS.
#if !defined(OS_CHROMEOS)
  WebContents* web_contents = delegate_->GetWebContents();
  if (!web_contents)
    return;
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents);
  if (browser)
    FirstRunBubble::ShowBubble(browser, GetSecurityBubbleAnchorView());
#endif
}

base::string16 LocationBarView::GetLocationIconText() const {
  if (GetToolbarModel()->GetURL().SchemeIs(content::kChromeUIScheme))
    return l10n_util::GetStringUTF16(IDS_SHORT_PRODUCT_NAME);

  const base::string16 extension_name = GetExtensionName(
      GetToolbarModel()->GetURL(), delegate_->GetWebContents());
  if (!extension_name.empty())
    return extension_name;

  bool has_ev_cert =
      (GetToolbarModel()->GetSecurityLevel(false) == security_state::EV_SECURE);
  return has_ev_cert ? GetToolbarModel()->GetEVCertName()
                     : GetToolbarModel()->GetSecureVerboseText();
}

bool LocationBarView::ShouldShowKeywordBubble() const {
  return !omnibox_view_->model()->keyword().empty() &&
         !omnibox_view_->model()->is_keyword_hint();
}

bool LocationBarView::ShouldShowLocationIconText() const {
  if (!GetToolbarModel()->input_in_progress() &&
      (GetToolbarModel()->GetURL().SchemeIs(content::kChromeUIScheme) ||
       GetToolbarModel()->GetURL().SchemeIs(extensions::kExtensionScheme)))
    return true;

  using SecurityLevel = security_state::SecurityLevel;
  const SecurityLevel level = GetToolbarModel()->GetSecurityLevel(false);
  return level == SecurityLevel::EV_SECURE || level == SecurityLevel::SECURE ||
         level == SecurityLevel::DANGEROUS ||
         level == SecurityLevel::HTTP_SHOW_WARNING;
}

bool LocationBarView::ShouldAnimateLocationIconTextVisibilityChange() const {
  // Text for extension URLs should not be animated (their security level is
  // SecurityLevel::NONE).
  using SecurityLevel = security_state::SecurityLevel;
  SecurityLevel level = GetToolbarModel()->GetSecurityLevel(false);
  return level == SecurityLevel::DANGEROUS ||
         level == SecurityLevel::HTTP_SHOW_WARNING;
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, private LocationBar implementation:

void LocationBarView::ShowFirstRunBubble() {
  // Wait until search engines have loaded to show the first run bubble.
  TemplateURLService* url_service =
      TemplateURLServiceFactory::GetForProfile(profile());
  if (!url_service->loaded()) {
    template_url_service_ = url_service;
    template_url_service_->AddObserver(this);
    template_url_service_->Load();
    return;
  }
  ShowFirstRunBubbleInternal();
}

GURL LocationBarView::GetDestinationURL() const {
  return destination_url();
}

WindowOpenDisposition LocationBarView::GetWindowOpenDisposition() const {
  return disposition();
}

ui::PageTransition LocationBarView::GetPageTransition() const {
  return transition();
}

void LocationBarView::AcceptInput() {
  omnibox_view_->model()->AcceptInput(WindowOpenDisposition::CURRENT_TAB,
                                      false);
}

void LocationBarView::FocusSearch() {
  omnibox_view_->SetFocus();
  omnibox_view_->EnterKeywordModeForDefaultSearchProvider();
}

void LocationBarView::UpdateContentSettingsIcons() {
  if (RefreshContentSettingViews()) {
    Layout();
    SchedulePaint();
  }
}

void LocationBarView::UpdateManagePasswordsIconAndBubble() {
  if (RefreshManagePasswordsIconView()) {
    Layout();
    SchedulePaint();
  }
}

void LocationBarView::UpdateSaveCreditCardIcon() {
  if (RefreshSaveCreditCardIconView()) {
    Layout();
    SchedulePaint();
  }
}

void LocationBarView::UpdateBookmarkStarVisibility() {
  if (star_view_) {
    star_view_->SetVisible(
        browser_defaults::bookmarks_enabled && !is_popup_mode_ &&
        !GetToolbarModel()->input_in_progress() &&
        edit_bookmarks_enabled_.GetValue() &&
        !IsBookmarkStarHiddenByExtension());
  }
}

void LocationBarView::UpdateZoomViewVisibility() {
  RefreshZoomView();
  OnChanged();
}

void LocationBarView::UpdateLocationBarVisibility(bool visible, bool animate) {
  if (!animate) {
    size_animation_.Reset(visible ? 1 : 0);
    return;
  }

  if (visible)
    size_animation_.Show();
  else
    size_animation_.Hide();
}

void LocationBarView::SaveStateToContents(WebContents* contents) {
  omnibox_view_->SaveStateToTab(contents);
}

const OmniboxView* LocationBarView::GetOmniboxView() const {
  return omnibox_view_;
}

LocationBarTesting* LocationBarView::GetLocationBarForTesting() {
  return this;
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, private LocationBarTesting implementation:

bool LocationBarView::GetBookmarkStarVisibility() {
  DCHECK(star_view_);
  return star_view_->visible();
}

bool LocationBarView::TestContentSettingImagePressed(size_t index) {
  if (index >= content_setting_views_.size())
    return false;

  views::View* image_view = content_setting_views_[index];
  image_view->SetSize(gfx::Size(24, 24));
  image_view->OnKeyPressed(
      ui::KeyEvent(ui::ET_KEY_PRESSED, ui::VKEY_SPACE, ui::EF_NONE));
  image_view->OnKeyReleased(
      ui::KeyEvent(ui::ET_KEY_RELEASED, ui::VKEY_SPACE, ui::EF_NONE));
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, private views::View implementation:

const char* LocationBarView::GetClassName() const {
  return kViewClassName;
}

void LocationBarView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  OmniboxPopupView* popup = omnibox_view_->model()->popup_model()->view();
  if (popup->IsOpen())
    popup->UpdatePopupAppearance();
}

void LocationBarView::OnFocus() {
  omnibox_view_->SetFocus();
}

void LocationBarView::OnPaint(gfx::Canvas* canvas) {
  View::OnPaint(canvas);

  if (show_focus_rect_ && omnibox_view_->HasFocus()) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(GetNativeTheme()->GetSystemColor(
        ui::NativeTheme::NativeTheme::kColorId_FocusedBorderColor));
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    gfx::RectF focus_rect(GetLocalBounds());
    focus_rect.Inset(gfx::InsetsF(0.5f));
    canvas->DrawRoundRect(focus_rect,
                          BackgroundWith1PxBorder::kCornerRadius + 0.5f, flags);
  }
}

void LocationBarView::OnPaintBorder(gfx::Canvas* canvas) {
  if (!is_popup_mode_)
    return;  // The border is painted by our Background.

  gfx::Rect bounds(GetContentsBounds());
  const SkColor border_color =
      GetOpaqueBorderColor(profile()->IsOffTheRecord());
  BrowserView::Paint1pxHorizontalLine(canvas, border_color, bounds, false);
  BrowserView::Paint1pxHorizontalLine(canvas, border_color, bounds, true);
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, private views::DragController implementation:

void LocationBarView::WriteDragDataForView(views::View* sender,
                                           const gfx::Point& press_pt,
                                           OSExchangeData* data) {
  DCHECK_NE(GetDragOperationsForView(sender, press_pt),
            ui::DragDropTypes::DRAG_NONE);

  WebContents* web_contents = GetWebContents();
  favicon::FaviconDriver* favicon_driver =
      favicon::ContentFaviconDriver::FromWebContents(web_contents);
  gfx::ImageSkia favicon = favicon_driver->GetFavicon().AsImageSkia();
  button_drag_utils::SetURLAndDragImage(web_contents->GetURL(),
                                        web_contents->GetTitle(), favicon,
                                        nullptr, *sender->GetWidget(), data);
}

int LocationBarView::GetDragOperationsForView(views::View* sender,
                                              const gfx::Point& p) {
  DCHECK_EQ(location_icon_view_, sender);
  WebContents* web_contents = delegate_->GetWebContents();
  return (web_contents && web_contents->GetURL().is_valid() &&
          (!GetOmniboxView()->IsEditingOrEmpty())) ?
      (ui::DragDropTypes::DRAG_COPY | ui::DragDropTypes::DRAG_LINK) :
      ui::DragDropTypes::DRAG_NONE;
}

bool LocationBarView::CanStartDragForView(View* sender,
                                          const gfx::Point& press_pt,
                                          const gfx::Point& p) {
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, private gfx::AnimationDelegate implementation:
void LocationBarView::AnimationProgressed(const gfx::Animation* animation) {
  GetWidget()->non_client_view()->Layout();
}

void LocationBarView::AnimationEnded(const gfx::Animation* animation) {
  AnimationProgressed(animation);
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, private OmniboxEditController implementation:

void LocationBarView::OnChanged() {
  RefreshLocationIcon();
  location_icon_view_->set_show_tooltip(!GetOmniboxView()->IsEditingOrEmpty());
  Layout();
  SchedulePaint();
}

const ToolbarModel* LocationBarView::GetToolbarModel() const {
  return delegate_->GetToolbarModel();
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, private DropdownBarHostDelegate implementation:

void LocationBarView::SetFocusAndSelection(bool select_all) {
  FocusLocation(select_all);
}

////////////////////////////////////////////////////////////////////////////////
// LocationBarView, private TemplateURLServiceObserver implementation:

void LocationBarView::OnTemplateURLServiceChanged() {
  template_url_service_->RemoveObserver(this);
  template_url_service_ = nullptr;
  // If the browser is no longer active, let's not show the info bubble, as this
  // would make the browser the active window again.
  if (omnibox_view_ && omnibox_view_->GetWidget()->IsActive())
    ShowFirstRunBubble();
}
