// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/location_icon_view.h"

#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/theme_resources.h"
#include "components/grit/components_scaled_resources.h"
#include "components/omnibox/browser/omnibox_edit_model.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/label.h"

using content::WebContents;

LocationIconView::LocationIconView(const gfx::FontList& font_list,
                                   LocationBarView* location_bar)
    : IconLabelBubbleView(font_list, true),
      location_bar_(location_bar),
      animation_(this) {
  set_id(VIEW_ID_LOCATION_ICON);
  SetInkDropMode(InkDropMode::ON);

#if defined(OS_MACOSX)
  SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
#else
  SetFocusBehavior(FocusBehavior::ALWAYS);
#endif

  animation_.SetSlideDuration(kOpenTimeMS);
}

LocationIconView::~LocationIconView() {
}

gfx::Size LocationIconView::GetMinimumSize() const {
  return GetMinimumSizeForPreferredSize(GetPreferredSize());
}

bool LocationIconView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyMiddleMouseButton() &&
      ui::Clipboard::IsSupportedClipboardType(ui::CLIPBOARD_TYPE_SELECTION)) {
    base::string16 text;
    ui::Clipboard::GetForCurrentThread()->ReadText(
        ui::CLIPBOARD_TYPE_SELECTION, &text);
    text = OmniboxView::SanitizeTextForPaste(text);
    OmniboxEditModel* model = location_bar_->GetOmniboxView()->model();
    if (model->CanPasteAndGo(text))
      model->PasteAndGo(text);
  }

  IconLabelBubbleView::OnMousePressed(event);
  return true;
}

bool LocationIconView::OnMouseDragged(const ui::MouseEvent& event) {
  location_bar_->GetOmniboxView()->CloseOmniboxPopup();
  return IconLabelBubbleView::OnMouseDragged(event);
}

bool LocationIconView::GetTooltipText(const gfx::Point& p,
                                      base::string16* tooltip) const {
  if (show_tooltip_)
    *tooltip = l10n_util::GetStringUTF16(IDS_TOOLTIP_LOCATION_ICON);
  return show_tooltip_;
}

SkColor LocationIconView::GetTextColor() const {
  return location_bar_->GetColor(LocationBarView::SECURITY_CHIP_TEXT);
}

bool LocationIconView::ShowBubble(const ui::Event& event) {
  WebContents* contents = location_bar_->GetWebContents();
  if (!contents)
    return false;
  location_bar_->delegate()->ShowPageInfo(contents);
  return true;
}

void LocationIconView::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  IconLabelBubbleView::GetAccessibleNodeData(node_data);
  node_data->role = ui::AX_ROLE_POP_UP_BUTTON;
}

bool LocationIconView::IsBubbleShowing() const {
  return PageInfoBubbleView::GetShownBubbleType() !=
         PageInfoBubbleView::BUBBLE_NONE;
}

gfx::Size LocationIconView::GetMinimumSizeForLabelText(
    const base::string16& text) const {
  views::Label label(text, {font_list()});
  return GetMinimumSizeForPreferredSize(
      GetSizeForLabelWidth(label.GetPreferredSize().width()));
}

void LocationIconView::SetTextVisibility(bool should_show,
                                         bool should_animate) {
  if (!should_animate) {
    animation_.Reset(should_show);
  } else if (should_show) {
    animation_.Show();
  } else {
    animation_.Hide();
  }
  // The label text color may have changed.
  OnNativeThemeChanged(GetNativeTheme());
}

bool LocationIconView::IsTriggerableEvent(const ui::Event& event) {
  if (location_bar_->GetOmniboxView()->IsEditingOrEmpty())
    return false;

  if (event.IsMouseEvent()) {
    if (event.AsMouseEvent()->IsOnlyMiddleMouseButton())
      return false;
  } else if (event.IsGestureEvent() && event.type() != ui::ET_GESTURE_TAP) {
    return false;
  }

  return IconLabelBubbleView::IsTriggerableEvent(event);
}

double LocationIconView::WidthMultiplier() const {
  return animation_.GetCurrentValue();
}

void LocationIconView::AnimationProgressed(const gfx::Animation*) {
  location_bar_->Layout();
  location_bar_->SchedulePaint();
}

gfx::Size LocationIconView::GetMinimumSizeForPreferredSize(
    gfx::Size size) const {
  const int kMinCharacters = 10;
  size.SetToMin(
      GetSizeForLabelWidth(font_list().GetExpectedTextWidth(kMinCharacters)));
  return size;
}
