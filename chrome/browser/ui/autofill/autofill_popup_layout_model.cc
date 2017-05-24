// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/autofill/autofill_popup_layout_model.h"

#include <algorithm>

#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/autofill/autofill_popup_view.h"
#include "chrome/browser/ui/autofill/popup_constants.h"
#include "components/autofill/core/browser/autofill_experiments.h"
#include "components/autofill/core/browser/popup_item_ids.h"
#include "components/autofill/core/browser/suggestion.h"
#include "components/autofill/core/common/autofill_util.h"
#include "components/grit/components_scaled_resources.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_vector_icon.h"

#if !defined(OS_ANDROID)
#include "components/toolbar/vector_icons.h"  // nogncheck
#endif

namespace autofill {

namespace {

// The vertical height of each row in pixels.
const size_t kRowHeight = 24;

// The vertical height of a separator in pixels.
const size_t kSeparatorHeight = 1;

#if !defined(OS_ANDROID)
// Size difference between the normal font and the smaller font, in pixels.
const int kSmallerFontSizeDelta = -1;

const int kHttpWarningIconWidth = 16;
#endif

const struct {
  const char* name;
  int id;
} kDataResources[] = {
    {"americanExpressCC", IDR_AUTOFILL_CC_AMEX},
    {"dinersCC", IDR_AUTOFILL_CC_GENERIC},
    {"discoverCC", IDR_AUTOFILL_CC_DISCOVER},
    {"genericCC", IDR_AUTOFILL_CC_GENERIC},
    {"jcbCC", IDR_AUTOFILL_CC_GENERIC},
    {"masterCardCC", IDR_AUTOFILL_CC_MASTERCARD},
    {"mirCC", IDR_AUTOFILL_CC_MIR},
    {"unionPayCC", IDR_AUTOFILL_CC_UNIONPAY},
    {"visaCC", IDR_AUTOFILL_CC_VISA},
#if defined(OS_ANDROID)
    {"httpWarning", IDR_AUTOFILL_HTTP_WARNING},
    {"httpsInvalid", IDR_AUTOFILL_HTTPS_INVALID_WARNING},
    {"scanCreditCardIcon", IDR_AUTOFILL_CC_SCAN_NEW},
    {"settings", IDR_AUTOFILL_SETTINGS},
    {"create", IDR_AUTOFILL_CREATE},
#endif
};

int GetRowHeightFromId(int identifier) {
  if (identifier == POPUP_ITEM_ID_SEPARATOR)
    return kSeparatorHeight;

  return kRowHeight;
}

}  // namespace

AutofillPopupLayoutModel::AutofillPopupLayoutModel(
    AutofillPopupViewDelegate* delegate, bool is_credit_card_popup)
    : delegate_(delegate), is_credit_card_popup_(is_credit_card_popup) {
#if !defined(OS_ANDROID)
  smaller_font_list_ =
      normal_font_list_.DeriveWithSizeDelta(kSmallerFontSizeDelta);
  bold_font_list_ = normal_font_list_.DeriveWithWeight(gfx::Font::Weight::BOLD);
#endif
}

AutofillPopupLayoutModel::~AutofillPopupLayoutModel() {}

#if !defined(OS_ANDROID)
int AutofillPopupLayoutModel::GetDesiredPopupHeight() const {
  std::vector<autofill::Suggestion> suggestions = delegate_->GetSuggestions();
  int popup_height = 2 * kPopupBorderThickness;

  for (size_t i = 0; i < suggestions.size(); ++i) {
    popup_height += GetRowHeightFromId(suggestions[i].frontend_id);
  }

  return popup_height;
}

int AutofillPopupLayoutModel::GetDesiredPopupWidth() const {
  std::vector<autofill::Suggestion> suggestions = delegate_->GetSuggestions();

  int popup_width = RoundedElementBounds().width();

  for (size_t i = 0; i < suggestions.size(); ++i) {
    int label_size = delegate_->GetElidedLabelWidthForRow(i);
    int row_size = delegate_->GetElidedValueWidthForRow(i) + label_size +
                   RowWidthWithoutText(i, /* with_label= */ label_size > 0);

    popup_width = std::max(popup_width, row_size);
  }

  return popup_width;
}

int AutofillPopupLayoutModel::RowWidthWithoutText(int row,
                                                  bool with_label) const {
  std::vector<autofill::Suggestion> suggestions = delegate_->GetSuggestions();
  bool is_warning_message = (suggestions[row].frontend_id ==
                             POPUP_ITEM_ID_HTTP_NOT_SECURE_WARNING_MESSAGE);

  int row_size = kEndPadding;

  if (with_label)
    row_size += is_warning_message ? kHttpWarningNamePadding : kNamePadding;

  // Add the Autofill icon size, if required.
  const base::string16& icon = suggestions[row].icon;
  if (!icon.empty()) {
    row_size += GetIconImage(row).width() +
                (is_warning_message ? kHttpWarningIconPadding : kIconPadding);
  }

  // Add the padding at the end.
  row_size += kEndPadding;

  // Add room for the popup border.
  row_size += 2 * kPopupBorderThickness;

  return row_size;
}

int AutofillPopupLayoutModel::GetAvailableWidthForRow(int row,
                                                      bool with_label) const {
  return popup_bounds_.width() - RowWidthWithoutText(row, with_label);
}

void AutofillPopupLayoutModel::UpdatePopupBounds() {
  int popup_width = GetDesiredPopupWidth();
  int popup_height = GetDesiredPopupHeight();

  popup_bounds_ = view_common_.CalculatePopupBounds(
      popup_width, popup_height, RoundedElementBounds(),
      delegate_->container_view(), delegate_->IsRTL());
}

const gfx::FontList& AutofillPopupLayoutModel::GetValueFontListForRow(
    size_t index) const {
  std::vector<autofill::Suggestion> suggestions = delegate_->GetSuggestions();

  // Autofill values have positive |frontend_id|.
  if (suggestions[index].frontend_id > 0)
    return bold_font_list_;

  // All other message types are defined here.
  PopupItemId id = static_cast<PopupItemId>(suggestions[index].frontend_id);
  switch (id) {
    case POPUP_ITEM_ID_INSECURE_CONTEXT_PAYMENT_DISABLED_MESSAGE:
    case POPUP_ITEM_ID_CLEAR_FORM:
    case POPUP_ITEM_ID_CREDIT_CARD_SIGNIN_PROMO:
    case POPUP_ITEM_ID_AUTOFILL_OPTIONS:
    case POPUP_ITEM_ID_CREATE_HINT:
    case POPUP_ITEM_ID_SCAN_CREDIT_CARD:
    case POPUP_ITEM_ID_SEPARATOR:
    case POPUP_ITEM_ID_HTTP_NOT_SECURE_WARNING_MESSAGE:
    case POPUP_ITEM_ID_TITLE:
    case POPUP_ITEM_ID_PASSWORD_ENTRY:
      return normal_font_list_;
    case POPUP_ITEM_ID_AUTOCOMPLETE_ENTRY:
    case POPUP_ITEM_ID_DATALIST_ENTRY:
    case POPUP_ITEM_ID_USERNAME_ENTRY:
      return bold_font_list_;
  }
  NOTREACHED();
  return normal_font_list_;
}

const gfx::FontList& AutofillPopupLayoutModel::GetLabelFontListForRow(
    size_t index) const {
  std::vector<autofill::Suggestion> suggestions = delegate_->GetSuggestions();
  if (suggestions[index].frontend_id ==
      POPUP_ITEM_ID_HTTP_NOT_SECURE_WARNING_MESSAGE)
    return normal_font_list_;

  return smaller_font_list_;
}

ui::NativeTheme::ColorId AutofillPopupLayoutModel::GetValueFontColorIDForRow(
    size_t index) const {
  std::vector<autofill::Suggestion> suggestions = delegate_->GetSuggestions();
  switch (suggestions[index].frontend_id) {
    case POPUP_ITEM_ID_HTTP_NOT_SECURE_WARNING_MESSAGE:
      return ui::NativeTheme::kColorId_AlertSeverityHigh;
    case POPUP_ITEM_ID_INSECURE_CONTEXT_PAYMENT_DISABLED_MESSAGE:
      return ui::NativeTheme::kColorId_ResultsTableNormalDimmedText;
    default:
      return ui::NativeTheme::kColorId_ResultsTableNormalText;
  }
}

gfx::ImageSkia AutofillPopupLayoutModel::GetIconImage(size_t index) const {
  std::vector<autofill::Suggestion> suggestions = delegate_->GetSuggestions();
  const base::string16& icon_str = suggestions[index].icon;

  // For http warning message, get icon images from VectorIcon, which is the
  // same as security indicator icons in location bar.
  if (suggestions[index].frontend_id ==
      POPUP_ITEM_ID_HTTP_NOT_SECURE_WARNING_MESSAGE) {
    if (icon_str == base::ASCIIToUTF16("httpWarning")) {
      return gfx::CreateVectorIcon(toolbar::kHttpIcon, kHttpWarningIconWidth,
                                   gfx::kChromeIconGrey);
    }
    DCHECK_EQ(icon_str, base::ASCIIToUTF16("httpsInvalid"));
    return gfx::CreateVectorIcon(toolbar::kHttpsInvalidIcon,
                                 kHttpWarningIconWidth, gfx::kGoogleRed700);
  }

  // For other suggestion entries, get icon from PNG files.
  int icon_id = GetIconResourceID(icon_str);
  DCHECK_NE(-1, icon_id);
  return *ui::ResourceBundle::GetSharedInstance().GetImageSkiaNamed(icon_id);
}
#endif

int AutofillPopupLayoutModel::LineFromY(int y) const {
  std::vector<autofill::Suggestion> suggestions = delegate_->GetSuggestions();
  int current_height = kPopupBorderThickness;

  for (size_t i = 0; i < suggestions.size(); ++i) {
    current_height += GetRowHeightFromId(suggestions[i].frontend_id);

    if (y <= current_height)
      return i;
  }

  // The y value goes beyond the popup so stop the selection at the last line.
  return suggestions.size() - 1;
}

gfx::Rect AutofillPopupLayoutModel::GetRowBounds(size_t index) const {
  std::vector<autofill::Suggestion> suggestions = delegate_->GetSuggestions();

  int top = kPopupBorderThickness;
  for (size_t i = 0; i < index; ++i) {
    top += GetRowHeightFromId(suggestions[i].frontend_id);
  }

  return gfx::Rect(kPopupBorderThickness, top,
                   popup_bounds_.width() - 2 * kPopupBorderThickness,
                   GetRowHeightFromId(suggestions[index].frontend_id));
}

int AutofillPopupLayoutModel::GetIconResourceID(
    const base::string16& resource_name) const {
  int result = -1;
  for (size_t i = 0; i < arraysize(kDataResources); ++i) {
    if (resource_name == base::ASCIIToUTF16(kDataResources[i].name)) {
      result = kDataResources[i].id;
      break;
    }
  }

  return result;
}

const gfx::Rect AutofillPopupLayoutModel::RoundedElementBounds() const {
  return gfx::ToEnclosingRect(delegate_->element_bounds());
}

bool AutofillPopupLayoutModel::IsPopupLayoutExperimentEnabled() const {
  return is_credit_card_popup_ &&
      IsAutofillCreditCardPopupLayoutExperimentEnabled();
}

SkColor AutofillPopupLayoutModel::GetBackgroundColor() const {
  return is_credit_card_popup_ ?
      GetCreditCardPopupBackgroundColor() : SK_ColorTRANSPARENT;
}

SkColor AutofillPopupLayoutModel::GetDividerColor() const {
  return is_credit_card_popup_ ?
      GetCreditCardPopupDividerColor() : SK_ColorTRANSPARENT;
}

unsigned int AutofillPopupLayoutModel::GetDropdownItemHeight() const {
  return GetPopupDropdownItemHeight();
}

bool AutofillPopupLayoutModel::IsIconAtStart(int frontend_id) const {
  return frontend_id == POPUP_ITEM_ID_HTTP_NOT_SECURE_WARNING_MESSAGE ||
      (is_credit_card_popup_ && IsIconInCreditCardPopupAtStart());
}

unsigned int AutofillPopupLayoutModel::GetMargin() const {
  return GetPopupMargin();
}

}  // namespace autofill
