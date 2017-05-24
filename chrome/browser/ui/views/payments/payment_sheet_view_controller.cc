// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/payments/payment_sheet_view_controller.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/i18n/message_formatter.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/views/payments/payment_request_dialog_view.h"
#include "chrome/browser/ui/views/payments/payment_request_dialog_view_ids.h"
#include "chrome/browser/ui/views/payments/payment_request_row_view.h"
#include "chrome/browser/ui/views/payments/payment_request_views_util.h"
#include "chrome/grit/generated_resources.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/payments/content/payment_request_spec.h"
#include "components/payments/content/payment_request_state.h"
#include "components/payments/core/currency_formatter.h"
#include "components/payments/core/payment_instrument.h"
#include "components/payments/core/payment_prefs.h"
#include "components/payments/core/strings_util.h"
#include "components/prefs/pref_service.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/range/range.h"
#include "ui/gfx/text_elider.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view.h"

namespace payments {
namespace {

constexpr int kFirstTagValue = static_cast<int>(
    payments::PaymentRequestCommonTags::PAYMENT_REQUEST_COMMON_TAG_MAX);

// Tags for the buttons in the payment sheet
enum class PaymentSheetViewControllerTags {
  SHOW_ORDER_SUMMARY_BUTTON = kFirstTagValue,  // Navigate to order summary
  SHOW_SHIPPING_BUTTON,         // Navigate to the shipping address screen
  ADD_SHIPPING_BUTTON,          // Navigate to the shipping address editor
  SHOW_PAYMENT_METHOD_BUTTON,   // Navigate to the payment method screen
  ADD_PAYMENT_METHOD_BUTTON,    // Navigate to the payment method editor
  SHOW_CONTACT_INFO_BUTTON,     // Navigate to the contact info screen
  ADD_CONTACT_INFO_BUTTON,      // Navigate to the contact info editor
  SHOW_SHIPPING_OPTION_BUTTON,  // Navigate to the shipping options screen
  PAY_BUTTON,
  MAX_TAG,  // Always keep last.
};

// A class that ensures proper elision of labels in the form
// "[preview] and N more" where preview might be elided to allow "and N more" to
// be always visible.
class PreviewEliderLabel : public views::Label {
 public:
  // Creates a PreviewEliderLabel where |preview_text| might be elided,
  // |format_string| is the string with format argument numbers in ICU syntax
  // and |n| is the "N more" item count.
  PreviewEliderLabel(const base::string16& preview_text,
                     const base::string16& format_string,
                     int n)
      : views::Label(base::ASCIIToUTF16("")),
        preview_text_(preview_text),
        format_string_(format_string),
        n_(n) {}

 private:
  // Formats |preview_text_|, |format_string_|, and |n_| into a string that fits
  // inside of |pixel_width|, eliding |preview_text_| as required.
  base::string16 CreateElidedString(int pixel_width) {
    for (int preview_length = preview_text_.size(); preview_length > 0;
         --preview_length) {
      base::string16 elided_preview;
      gfx::ElideRectangleString(preview_text_, 1, preview_length,
                                /*strict=*/false, &elided_preview);
      base::string16 elided_string =
          base::i18n::MessageFormatter::FormatWithNumberedArgs(
              format_string_, "", elided_preview, n_);
      if (gfx::GetStringWidth(elided_string, font_list()) <= pixel_width)
        return elided_string;
    }

    // TODO(crbug.com/714776): Display something meaningful if the preview can't
    // be elided enough for the string to fit.
    return base::ASCIIToUTF16("");
  }

  // views::View:
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override {
    SetText(CreateElidedString(width()));
    views::Label::OnBoundsChanged(previous_bounds);
  }

  base::string16 preview_text_;
  base::string16 format_string_;
  int n_;

  DISALLOW_COPY_AND_ASSIGN(PreviewEliderLabel);
};

std::unique_ptr<views::Button> CreatePaymentSheetRow(
    views::ButtonListener* listener,
    const base::string16& section_name,
    std::unique_ptr<views::View> content_view,
    std::unique_ptr<views::View> extra_content_view,
    std::unique_ptr<views::View> trailing_button,
    bool clickable,
    bool extra_trailing_inset) {
  const int trailing_inset = extra_trailing_inset
                                 ? kPaymentRequestRowHorizontalInsets +
                                       kPaymentRequestRowExtraRightInset
                                 : kPaymentRequestRowHorizontalInsets;
  const gfx::Insets row_insets(
      kPaymentRequestRowVerticalInsets, kPaymentRequestRowHorizontalInsets,
      kPaymentRequestRowVerticalInsets, trailing_inset);
  std::unique_ptr<PaymentRequestRowView> row =
      base::MakeUnique<PaymentRequestRowView>(listener, clickable, row_insets);
  views::GridLayout* layout = new views::GridLayout(row.get());
  row->SetLayoutManager(layout);

  views::ColumnSet* columns = layout->AddColumnSet(0);
  // A column for the section name.
  constexpr int kNameColumnWidth = 112;
  columns->AddColumn(views::GridLayout::LEADING, views::GridLayout::LEADING, 0,
                     views::GridLayout::FIXED, kNameColumnWidth, 0);

  constexpr int kPaddingAfterName = 32;
  columns->AddPaddingColumn(0, kPaddingAfterName);

  // A column for the content.
  columns->AddColumn(views::GridLayout::FILL, views::GridLayout::LEADING,
                     1, views::GridLayout::USE_PREF, 0, 0);
  // A column for the extra content.
  columns->AddColumn(views::GridLayout::TRAILING, views::GridLayout::CENTER,
                     0, views::GridLayout::USE_PREF, 0, 0);

  constexpr int kPaddingColumnsWidth = 25;
  columns->AddPaddingColumn(0, kPaddingColumnsWidth);
  // A column for the trailing_button.
  columns->AddColumn(views::GridLayout::TRAILING, views::GridLayout::CENTER,
                     0, views::GridLayout::USE_PREF, 0, 0);

  layout->StartRow(0, 0);
  views::Label* name_label = new views::Label(section_name);
  name_label->SetMultiLine(true);
  name_label->SetFontList(
      name_label->font_list().DeriveWithWeight(gfx::Font::Weight::MEDIUM));
  layout->AddView(name_label);

  if (content_view) {
    content_view->set_can_process_events_within_subtree(false);
    layout->AddView(content_view.release());
  } else {
    layout->SkipColumns(1);
  }

  if (extra_content_view) {
    extra_content_view->set_can_process_events_within_subtree(false);
    layout->AddView(extra_content_view.release());
  } else {
    layout->SkipColumns(1);
  }

  layout->AddView(trailing_button.release());

  return std::move(row);
}

std::unique_ptr<views::View> CreateInlineCurrencyAmountItem(
    const base::string16& currency,
    const base::string16& amount,
    bool disabled_color,
    bool bold) {
  std::unique_ptr<views::View> item_amount_line =
      base::MakeUnique<views::View>();
  std::unique_ptr<views::GridLayout> item_amount_layout =
      base::MakeUnique<views::GridLayout>(item_amount_line.get());
  views::ColumnSet* item_amount_columns = item_amount_layout->AddColumnSet(0);
  item_amount_columns->AddColumn(views::GridLayout::LEADING,
                                 views::GridLayout::LEADING, 0,
                                 views::GridLayout::USE_PREF, 0, 0);
  item_amount_columns->AddColumn(views::GridLayout::TRAILING,
                                 views::GridLayout::LEADING, 1,
                                 views::GridLayout::USE_PREF, 0, 0);

  std::unique_ptr<views::Label> currency_label =
      bold ? CreateBoldLabel(currency)
           : base::MakeUnique<views::Label>(currency);
  if (disabled_color) {
    currency_label->SetDisabledColor(
        currency_label->GetNativeTheme()->GetSystemColor(
            ui::NativeTheme::kColorId_LabelDisabledColor));
    currency_label->SetEnabled(false);
  }
  std::unique_ptr<views::Label> amount_label =
      bold ? CreateBoldLabel(amount) : base::MakeUnique<views::Label>(amount);
  amount_label->SetMultiLine(true);
  amount_label->SetAllowCharacterBreak(true);

  item_amount_layout->StartRow(0, 0);
  item_amount_layout->AddView(currency_label.release());
  item_amount_layout->AddView(amount_label.release());

  item_amount_line->SetLayoutManager(item_amount_layout.release());
  return item_amount_line;
}

// A class used to build Payment Sheet Rows. Construct an instance of it, chain
// calls to argument-setting functions, then call one of the CreateWith*
// functions to create the row view.
class PaymentSheetRowBuilder {
 public:
  PaymentSheetRowBuilder(views::ButtonListener* listener,
                         const base::string16& section_name)
      : listener_(listener), section_name_(section_name) {}

  PaymentSheetRowBuilder& Tag(PaymentSheetViewControllerTags tag) {
    tag_ = static_cast<int>(tag);
    return *this;
  }

  PaymentSheetRowBuilder& Id(DialogViewID id) {
    id_ = static_cast<int>(id);
    return *this;
  }

  // Creates a clickable row to be displayed in the Payment Sheet. It contains
  // a section name and some content, followed by a chevron as a clickability
  // affordance. Both, either, or none of |content_view| and
  // |extra_content_view| may be present, the difference between the two being
  // that content is pinned to the left and extra_content is pinned to the
  // right. The row also displays a light gray horizontal ruler on its lower
  // boundary. The name column has a fixed width equal to |name_column_width|.
  // +----------------------------+
  // | Name | Content | Extra | > |
  // +~~~~~~~~~~~~~~~~~~~~~~~~~~~~+ <-- ruler
  std::unique_ptr<views::Button> CreateWithChevron(
      std::unique_ptr<views::View> content_view,
      std::unique_ptr<views::View> extra_content_view) {
    std::unique_ptr<views::ImageView> chevron =
        base::MakeUnique<views::ImageView>();
    chevron->set_can_process_events_within_subtree(false);
    std::unique_ptr<views::Label> label =
        base::MakeUnique<views::Label>(section_name_);
    chevron->SetImage(gfx::CreateVectorIcon(
        views::kSubmenuArrowIcon,
        color_utils::DeriveDefaultIconColor(label->enabled_color())));
    std::unique_ptr<views::Button> section = CreatePaymentSheetRow(
        listener_, section_name_, std::move(content_view),
        std::move(extra_content_view), std::move(chevron),
        /*clickable=*/true, /*extra_trailing_inset=*/true);
    section->set_tag(tag_);
    section->set_id(id_);
    return section;
  }

  // Creates a row with a button in place of the chevron and |truncated_content|
  // between |section_name| and the button.
  // +------------------------------------------+
  // | Name | truncated_content | button_string |
  // +~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~+
  std::unique_ptr<views::Button> CreateWithButton(
      const base::string16& truncated_content,
      const base::string16& button_string,
      bool button_enabled) {
    std::unique_ptr<views::Label> content_view =
        base::MakeUnique<views::Label>(truncated_content);
    content_view->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    content_view->SetDisabledColor(
        content_view->GetNativeTheme()->GetSystemColor(
            ui::NativeTheme::kColorId_LabelDisabledColor));
    content_view->SetEnabled(false);
    return CreateWithButton(std::move(content_view), button_string,
                            button_enabled);
  }

  // Creates a row with a button in place of the chevron with the string between
  // |section_name| and the button built as "|preview|... and |n| more".
  // |format_string| is used to assemble the truncated preview and the rest of
  // the content string.
  // +----------------------------------------------+
  // | Name | preview... and N more | button_string |
  // +~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~+
  std::unique_ptr<views::Button> CreateWithButton(
      const base::string16& preview_text,
      const base::string16& format_string,
      int n,
      const base::string16& button_string,
      bool button_enabled) {
    std::unique_ptr<PreviewEliderLabel> content_view =
        base::MakeUnique<PreviewEliderLabel>(preview_text, format_string, n);
    content_view->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    content_view->SetDisabledColor(
        content_view->GetNativeTheme()->GetSystemColor(
            ui::NativeTheme::kColorId_LabelDisabledColor));
    content_view->SetEnabled(false);
    return CreateWithButton(std::move(content_view), button_string,
                            button_enabled);
  }

 private:
  // Creates a row with a button in place of the chevron.
  // +------------------------------------------+
  // | Name | content_view      | button_string |
  // +~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~+
  std::unique_ptr<views::Button> CreateWithButton(
      std::unique_ptr<views::View> content_view,
      const base::string16& button_string,
      bool button_enabled) {
    std::unique_ptr<views::Button> button(
        views::MdTextButton::CreateSecondaryUiBlueButton(listener_,
                                                         button_string));
    button->set_tag(tag_);
    button->set_id(id_);
    button->SetEnabled(button_enabled);
    return CreatePaymentSheetRow(listener_, section_name_,
                                 std::move(content_view), nullptr,
                                 std::move(button), /*clickable=*/false,
                                 /*extra_trailing_inset=*/false);
  }

  views::ButtonListener* listener_;
  base::string16 section_name_;
  int tag_;
  int id_;
  DISALLOW_COPY_AND_ASSIGN(PaymentSheetRowBuilder);
};

}  // namespace

PaymentSheetViewController::PaymentSheetViewController(
    PaymentRequestSpec* spec,
    PaymentRequestState* state,
    PaymentRequestDialogView* dialog)
    : PaymentRequestSheetController(spec, state, dialog) {
  spec->AddObserver(this);
  state->AddObserver(this);
}

PaymentSheetViewController::~PaymentSheetViewController() {
  spec()->RemoveObserver(this);
  state()->RemoveObserver(this);
}

void PaymentSheetViewController::OnSpecUpdated() {
  UpdateContentView();
}

void PaymentSheetViewController::OnSelectedInformationChanged() {
  UpdatePayButtonState(state()->is_ready_to_pay());
  UpdateContentView();
}

std::unique_ptr<views::Button>
PaymentSheetViewController::CreatePrimaryButton() {
  std::unique_ptr<views::Button> button(
      views::MdTextButton::CreateSecondaryUiBlueButton(
          this, l10n_util::GetStringUTF16(IDS_PAYMENTS_PAY_BUTTON)));
  button->set_tag(static_cast<int>(PaymentRequestCommonTags::PAY_BUTTON_TAG));
  button->set_id(static_cast<int>(DialogViewID::PAY_BUTTON));
  button->SetEnabled(state()->is_ready_to_pay());
  return button;
}

base::string16 PaymentSheetViewController::GetSecondaryButtonLabel() {
  return l10n_util::GetStringUTF16(IDS_CANCEL);
}

bool PaymentSheetViewController::ShouldShowHeaderBackArrow() {
  return false;
}

base::string16 PaymentSheetViewController::GetSheetTitle() {
  return l10n_util::GetStringUTF16(IDS_PAYMENTS_TITLE);
}

void PaymentSheetViewController::FillContentView(views::View* content_view) {
  views::GridLayout* layout = new views::GridLayout(content_view);
  content_view->SetLayoutManager(layout);
  views::ColumnSet* columns = layout->AddColumnSet(0);
  columns->AddColumn(views::GridLayout::FILL, views::GridLayout::CENTER, 1,
                     views::GridLayout::USE_PREF, 0, 0);

  // The shipping address and contact info rows are optional.
  layout->StartRow(0, 0);
  layout->AddView(CreatePaymentSheetSummaryRow().release());

  if (spec()->request_shipping()) {
    layout->StartRow(0, 0);
    layout->AddView(CreateShippingRow().release());
    // It's possible for requestShipping to be true and for there to be no
    // shipping options yet (they will come in updateWith).
    // TODO(crbug.com/707353): Put a better placeholder row, instead of no row.
    std::unique_ptr<views::Button> shipping_row = CreateShippingOptionRow();
    if (shipping_row) {
      layout->StartRow(0, 0);
      layout->AddView(shipping_row.release());
    }
  }
  layout->StartRow(0, 0);
  layout->AddView(CreatePaymentMethodRow().release());
  if (spec()->request_payer_name() || spec()->request_payer_email() ||
      spec()->request_payer_phone()) {
    layout->StartRow(0, 0);
    layout->AddView(CreateContactInfoRow().release());
  }
  layout->StartRow(0, 0);
  layout->AddView(CreateDataSourceRow().release());
}

// Adds the product logo to the footer.
// +---------------------------------------------------------+
// | (•) chrome                               | PAY | CANCEL |
// +---------------------------------------------------------+
std::unique_ptr<views::View>
PaymentSheetViewController::CreateExtraFooterView() {
  return CreateProductLogoFooterView();
}

void PaymentSheetViewController::ButtonPressed(
    views::Button* sender, const ui::Event& event) {
  switch (sender->tag()) {
    case static_cast<int>(
        PaymentSheetViewControllerTags::SHOW_ORDER_SUMMARY_BUTTON):
      dialog()->ShowOrderSummary();
      break;

    case static_cast<int>(PaymentSheetViewControllerTags::SHOW_SHIPPING_BUTTON):
      dialog()->ShowShippingProfileSheet();
      break;

    case static_cast<int>(PaymentSheetViewControllerTags::ADD_SHIPPING_BUTTON):
      dialog()->ShowShippingAddressEditor(
          BackNavigationType::kPaymentSheet,
          /*on_edited=*/base::OnceClosure(),  // This is always an add.
          /*on_added=*/
          base::BindOnce(&PaymentRequestState::AddAutofillShippingProfile,
                         base::Unretained(state()), /*selected=*/true),
          nullptr);
      break;

    case static_cast<int>(
        PaymentSheetViewControllerTags::SHOW_PAYMENT_METHOD_BUTTON):
      dialog()->ShowPaymentMethodSheet();
      break;

    case static_cast<int>(
        PaymentSheetViewControllerTags::ADD_PAYMENT_METHOD_BUTTON):
      dialog()->ShowCreditCardEditor(
          BackNavigationType::kPaymentSheet,
          static_cast<int>(PaymentSheetViewControllerTags::MAX_TAG),
          /*on_edited=*/base::OnceClosure(),  // This is always an add.
          /*on_added=*/
          base::BindOnce(&PaymentRequestState::AddAutofillPaymentInstrument,
                         base::Unretained(state()), /*selected=*/true),
          /*credit_card=*/nullptr);

      break;

    case static_cast<int>(
        PaymentSheetViewControllerTags::SHOW_CONTACT_INFO_BUTTON):
      dialog()->ShowContactProfileSheet();
      break;

    case static_cast<int>(
        PaymentSheetViewControllerTags::ADD_CONTACT_INFO_BUTTON):
      dialog()->ShowContactInfoEditor(
          BackNavigationType::kPaymentSheet,
          /*on_edited=*/base::OnceClosure(),  // This is always an add.
          /*on_added=*/
          base::BindOnce(&PaymentRequestState::AddAutofillContactProfile,
                         base::Unretained(state()), /*selected=*/true));
      break;

    case static_cast<int>(
        PaymentSheetViewControllerTags::SHOW_SHIPPING_OPTION_BUTTON):
      dialog()->ShowShippingOptionSheet();
      break;

    default:
      PaymentRequestSheetController::ButtonPressed(sender, event);
      break;
  }
}

void PaymentSheetViewController::StyledLabelLinkClicked(
    views::StyledLabel* label,
    const gfx::Range& range,
    int event_flags) {
  // The only thing that can trigger this is the user clicking on the "settings"
  // link in the data attribution text.
  chrome::ShowSettingsSubPageForProfile(dialog()->GetProfile(), "");
}

void PaymentSheetViewController::UpdatePayButtonState(bool enabled) {
  primary_button()->SetEnabled(enabled);
}

// Creates the Order Summary row, which contains an "Order Summary" label,
// an inline list of display items, a Total Amount label, and a Chevron.
// +----------------------------------------------+
// | Order Summary   Item 1            $ 1.34     |
// |                 Item 2            $ 2.00   > |
// |                 2 more items...              |
// |                 Total         USD $12.34     |
// +----------------------------------------------+
std::unique_ptr<views::Button>
PaymentSheetViewController::CreatePaymentSheetSummaryRow() {
  std::unique_ptr<views::View> inline_summary = base::MakeUnique<views::View>();
  std::unique_ptr<views::GridLayout> layout =
      base::MakeUnique<views::GridLayout>(inline_summary.get());
  views::ColumnSet* columns = layout->AddColumnSet(0);
  columns->AddColumn(views::GridLayout::LEADING, views::GridLayout::LEADING, 1,
                     views::GridLayout::USE_PREF, 0, 0);
  constexpr int kItemSummaryPriceFixedWidth = 96;
  columns->AddColumn(views::GridLayout::FILL, views::GridLayout::LEADING, 0,
                     views::GridLayout::FIXED, kItemSummaryPriceFixedWidth,
                     kItemSummaryPriceFixedWidth);

  const std::vector<mojom::PaymentItemPtr>& items =
      spec()->details().display_items;

  bool is_mixed_currency = spec()->IsMixedCurrency();
  // The inline items section contains the first 2 display items of the
  // request's details, followed by a label indicating "N more items..." if
  // there are more than 2 items in the details. The total label and amount
  // always follow.
  constexpr int kMaxNumberOfItemsShown = 2;
  int hidden_item_count = items.size() - kMaxNumberOfItemsShown;
  if (hidden_item_count > 0) {
    layout->StartRow(0, 0);
    std::unique_ptr<views::Label> label =
        base::MakeUnique<views::Label>(l10n_util::GetPluralStringFUTF16(
            IDS_PAYMENT_REQUEST_ORDER_SUMMARY_MORE_ITEMS, hidden_item_count));
    label->SetDisabledColor(label->GetNativeTheme()->GetSystemColor(
        ui::NativeTheme::kColorId_LabelDisabledColor));
    label->SetEnabled(false);
    layout->AddView(label.release());
    if (is_mixed_currency) {
      std::unique_ptr<views::Label> multiple_currency_label =
          base::MakeUnique<views::Label>(l10n_util::GetStringUTF16(
              IDS_PAYMENT_REQUEST_ORDER_SUMMARY_MULTIPLE_CURRENCY_INDICATOR));
      multiple_currency_label->SetDisabledColor(
          multiple_currency_label->GetNativeTheme()->GetSystemColor(
              ui::NativeTheme::kColorId_LabelDisabledColor));
      multiple_currency_label->SetEnabled(false);
      layout->AddView(multiple_currency_label.release());
    }
  }

  for (size_t i = 0; i < items.size() && i < kMaxNumberOfItemsShown; ++i) {
    layout->StartRow(0, 0);
    std::unique_ptr<views::Label> summary =
        base::MakeUnique<views::Label>(base::UTF8ToUTF16(items[i]->label));
    summary->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    layout->AddView(summary.release());

    layout->AddView(
        CreateInlineCurrencyAmountItem(
            is_mixed_currency
                ? base::UTF8ToUTF16(
                      spec()->GetFormattedCurrencyCode(items[i]->amount))
                : base::string16(),
            spec()->GetFormattedCurrencyAmount(items[i]->amount), true, false)
            .release());
  }

  layout->StartRow(0, 0);
  layout->AddView(
      CreateBoldLabel(base::UTF8ToUTF16(spec()->details().total->label))
          .release());

  layout->AddView(
      CreateInlineCurrencyAmountItem(
          base::UTF8ToUTF16(spec()->GetFormattedCurrencyCode(
              spec()->details().total->amount)),
          spec()->GetFormattedCurrencyAmount(spec()->details().total->amount),
          false, true)
          .release());

  inline_summary->SetLayoutManager(layout.release());

  PaymentSheetRowBuilder builder(
      this, l10n_util::GetStringUTF16(
                IDS_PAYMENT_REQUEST_ORDER_SUMMARY_SECTION_NAME));
  builder.Tag(PaymentSheetViewControllerTags::SHOW_ORDER_SUMMARY_BUTTON)
      .Id(DialogViewID::PAYMENT_SHEET_SUMMARY_SECTION);

  return builder.CreateWithChevron(std::move(inline_summary), nullptr);
}

std::unique_ptr<views::View>
PaymentSheetViewController::CreateShippingSectionContent() {
  autofill::AutofillProfile* profile = state()->selected_shipping_profile();
  if (!profile)
    return base::MakeUnique<views::Label>(base::string16());

  // If there is a shipping option error related to the selected address,
  // display it (without disabling the row because the user should feel like
  // they can click on it to correct the problem). Otherwise, display the
  // address possibly with the missing information to make it complete.
  if (!spec()->selected_shipping_option_error().empty()) {
    return GetShippingAddressLabelWithError(
        AddressStyleType::SUMMARY, state()->GetApplicationLocale(), *profile,
        spec()->selected_shipping_option_error(), /*disabled_state=*/false);
  } else {
    return GetShippingAddressLabelWithMissingInfo(
        AddressStyleType::SUMMARY, state()->GetApplicationLocale(), *profile,
        *(state()->profile_comparator()));
  }
}

// Creates the Shipping row, which contains a "Shipping address" label, the
// user's selected shipping address, and a chevron.
// +----------------------------------------------+
// | Shipping Address   Barack Obama              |
// |                    1600 Pennsylvania Ave.  > |
// |                    1800MYPOTUS               |
// +----------------------------------------------+
std::unique_ptr<views::Button> PaymentSheetViewController::CreateShippingRow() {
  std::unique_ptr<views::Button> section;
  PaymentSheetRowBuilder builder(
      this, GetShippingAddressSectionString(spec()->shipping_type()));
  builder.Tag(PaymentSheetViewControllerTags::SHOW_SHIPPING_BUTTON);
  if (state()->selected_shipping_profile()) {
    builder.Id(DialogViewID::PAYMENT_SHEET_SHIPPING_ADDRESS_SECTION);
    return builder.CreateWithChevron(CreateShippingSectionContent(), nullptr);
  } else {
    builder.Id(DialogViewID::PAYMENT_SHEET_SHIPPING_ADDRESS_SECTION_BUTTON);
    if (state()->shipping_profiles().empty()) {
      // If the button is "Add", clicking it should navigate to the editor
      // instead of the list.
      builder.Tag(PaymentSheetViewControllerTags::ADD_SHIPPING_BUTTON);
      return builder.CreateWithButton(base::ASCIIToUTF16(""),
                                      l10n_util::GetStringUTF16(IDS_ADD),
                                      /*button_enabled=*/true);
    } else if (state()->shipping_profiles().size() == 1) {
      base::string16 truncated_content =
          state()->shipping_profiles()[0]->ConstructInferredLabel(
              {
                  autofill::ADDRESS_HOME_LINE1, autofill::ADDRESS_HOME_LINE2,
                  autofill::ADDRESS_HOME_CITY, autofill::ADDRESS_HOME_STATE,
                  autofill::ADDRESS_HOME_COUNTRY,
              },
              6, state()->GetApplicationLocale());
      return builder.CreateWithButton(truncated_content,
                                      l10n_util::GetStringUTF16(IDS_CHOOSE),
                                      /*button_enabled=*/true);
    } else {
      base::string16 format = l10n_util::GetPluralStringFUTF16(
          IDS_PAYMENT_REQUEST_SHIPPING_ADDRESSES_PREVIEW,
          state()->shipping_profiles().size() - 1);
      base::string16 label =
          state()->shipping_profiles()[0]->ConstructInferredLabel(
              {
                  autofill::ADDRESS_HOME_LINE1, autofill::ADDRESS_HOME_LINE2,
                  autofill::ADDRESS_HOME_CITY, autofill::ADDRESS_HOME_STATE,
                  autofill::ADDRESS_HOME_COUNTRY,
              },
              6, state()->GetApplicationLocale());
      return builder.CreateWithButton(label, format,
                                      state()->shipping_profiles().size() - 1,
                                      l10n_util::GetStringUTF16(IDS_CHOOSE),
                                      /*button_enabled=*/true);
    }
  }
}

// Creates the Payment Method row, which contains a "Payment" label, the user's
// masked Credit Card details, the icon for the selected card, and a chevron.
// If no option is selected or none is available, the Chevron and icon are
// replaced with a button
// +----------------------------------------------+
// | Payment         Visa ****0000                |
// |                 John Smith        | VISA | > |
// |                                              |
// +----------------------------------------------+
std::unique_ptr<views::Button>
PaymentSheetViewController::CreatePaymentMethodRow() {
  PaymentInstrument* selected_instrument = state()->selected_instrument();

  PaymentSheetRowBuilder builder(
      this, l10n_util::GetStringUTF16(
                IDS_PAYMENT_REQUEST_PAYMENT_METHOD_SECTION_NAME));
  builder.Tag(PaymentSheetViewControllerTags::SHOW_PAYMENT_METHOD_BUTTON);

  if (selected_instrument) {
    std::unique_ptr<views::View> content_view = base::MakeUnique<views::View>();

    views::GridLayout* layout = new views::GridLayout(content_view.get());
    content_view->SetLayoutManager(layout);
    views::ColumnSet* columns = layout->AddColumnSet(0);
    columns->AddColumn(views::GridLayout::LEADING, views::GridLayout::CENTER,
                       1, views::GridLayout::USE_PREF, 0, 0);

    layout->StartRow(0, 0);
    std::unique_ptr<views::Label> selected_instrument_label =
        base::MakeUnique<views::Label>(selected_instrument->label());
    selected_instrument_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    layout->AddView(selected_instrument_label.release());

    layout->StartRow(0, 0);
    std::unique_ptr<views::Label> selected_instrument_sublabel =
        base::MakeUnique<views::Label>(selected_instrument->sublabel());
    selected_instrument_sublabel->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    layout->AddView(selected_instrument_sublabel.release());

    std::unique_ptr<views::ImageView> card_icon_view = CreateInstrumentIconView(
        selected_instrument->icon_resource_id(), selected_instrument->label());
    card_icon_view->SetImageSize(gfx::Size(32, 20));

    return builder.Id(DialogViewID::PAYMENT_SHEET_PAYMENT_METHOD_SECTION)
        .CreateWithChevron(std::move(content_view), std::move(card_icon_view));
  } else {
    builder.Id(DialogViewID::PAYMENT_SHEET_PAYMENT_METHOD_SECTION_BUTTON);
    if (state()->available_instruments().empty()) {
      // If the button is "Add", navigate to the editor directly.
      builder.Tag(PaymentSheetViewControllerTags::ADD_PAYMENT_METHOD_BUTTON);
      return builder.CreateWithButton(base::ASCIIToUTF16(""),
                                      l10n_util::GetStringUTF16(IDS_ADD),
                                      /*button_enabled=*/true);
    } else if (state()->available_instruments().size() == 1) {
      return builder.CreateWithButton(
          state()->available_instruments()[0]->label(),
          l10n_util::GetStringUTF16(IDS_CHOOSE),
          /*button_enabled=*/true);
    } else {
      base::string16 format = l10n_util::GetPluralStringFUTF16(
          IDS_PAYMENT_REQUEST_PAYMENT_METHODS_PREVIEW,
          state()->available_instruments().size() - 1);
      return builder.CreateWithButton(
          state()->available_instruments()[0]->label(), format,
          state()->available_instruments().size() - 1,
          l10n_util::GetStringUTF16(IDS_CHOOSE),
          /*button_enabled=*/true);
    }
  }
}

std::unique_ptr<views::View>
PaymentSheetViewController::CreateContactInfoSectionContent() {
  autofill::AutofillProfile* profile = state()->selected_contact_profile();
  return profile
             ? payments::GetContactInfoLabel(
                   AddressStyleType::SUMMARY, state()->GetApplicationLocale(),
                   *profile, *spec(), *(state()->profile_comparator()))
             : base::MakeUnique<views::Label>(base::string16());
}

// Creates the Contact Info row, which contains a "Contact info" label; the
// name, email address, and/or phone number; and a chevron.
// +----------------------------------------------+
// | Contact info       Barack Obama              |
// |                    1800MYPOTUS             > |
// |                    potus@whitehouse.gov      |
// +----------------------------------------------+
std::unique_ptr<views::Button>
PaymentSheetViewController::CreateContactInfoRow() {
  PaymentSheetRowBuilder builder(
      this,
      l10n_util::GetStringUTF16(IDS_PAYMENT_REQUEST_CONTACT_INFO_SECTION_NAME));
  builder.Tag(PaymentSheetViewControllerTags::SHOW_CONTACT_INFO_BUTTON);

  if (state()->selected_contact_profile()) {
    return builder.Id(DialogViewID::PAYMENT_SHEET_CONTACT_INFO_SECTION)
        .CreateWithChevron(CreateContactInfoSectionContent(), nullptr);
  } else {
    builder.Id(DialogViewID::PAYMENT_SHEET_CONTACT_INFO_SECTION_BUTTON);
    if (state()->contact_profiles().empty()) {
      // If the button is "Add", navigate directly to the editor.
      builder.Tag(PaymentSheetViewControllerTags::ADD_CONTACT_INFO_BUTTON);
      return builder.CreateWithButton(base::ASCIIToUTF16(""),
                                      l10n_util::GetStringUTF16(IDS_ADD),
                                      /*button_enabled=*/true);
    } else if (state()->contact_profiles().size() == 1) {
      base::string16 truncated_content =
          state()->contact_profiles()[0]->ConstructInferredLabel(
              {
                  autofill::ADDRESS_HOME_LINE1, autofill::ADDRESS_HOME_LINE2,
                  autofill::ADDRESS_HOME_CITY, autofill::ADDRESS_HOME_STATE,
                  autofill::ADDRESS_HOME_COUNTRY,
              },
              6, state()->GetApplicationLocale());
      return builder.CreateWithButton(truncated_content,
                                      l10n_util::GetStringUTF16(IDS_CHOOSE),
                                      /*button_enabled=*/true);
    } else {
      base::string16 preview =
          state()->contact_profiles()[0]->ConstructInferredLabel(
              {
                  autofill::ADDRESS_HOME_LINE1, autofill::ADDRESS_HOME_LINE2,
                  autofill::ADDRESS_HOME_CITY, autofill::ADDRESS_HOME_STATE,
                  autofill::ADDRESS_HOME_COUNTRY,
              },
              6, state()->GetApplicationLocale());
      base::string16 format = l10n_util::GetPluralStringFUTF16(
          IDS_PAYMENT_REQUEST_CONTACTS_PREVIEW,
          state()->contact_profiles().size() - 1);
      return builder.CreateWithButton(
          preview, format, state()->contact_profiles().size() - 1,
          l10n_util::GetStringUTF16(IDS_CHOOSE), /*button_enabled=*/true);
    }
  }
}

std::unique_ptr<views::Button>
PaymentSheetViewController::CreateShippingOptionRow() {
  // The Shipping Options row has many different ways of being displayed
  // depending on the state of the dialog and Payment Request.
  // 1. There is a selected shipping address. The website updated the shipping
  //    options.
  //    1.1 There are no available shipping options: don't display the row.
  //    1.2 There are options and one is selected: display the row with the
  //        selection's label and a chevron.
  //    1.3 There are options and none is selected: display a row with a
  //        choose button and the string "|preview of first option| and N more"
  // 2. There is no selected shipping address: do not display the row.
  mojom::PaymentShippingOption* selected_option =
      spec()->selected_shipping_option();
  // The shipping option section displays the currently selected option if there
  // is one. It's not possible to select an option without selecting an address
  // first.
  PaymentSheetRowBuilder builder(
      this, GetShippingOptionSectionString(spec()->shipping_type()));
  builder.Tag(PaymentSheetViewControllerTags::SHOW_SHIPPING_OPTION_BUTTON);

  if (state()->selected_shipping_profile()) {
    if (spec()->details().shipping_options.empty()) {
      // 1.1 No shipping options, do not display the row.
      return nullptr;
    }

    if (selected_option) {
      // 1.2 Show the selected shipping option.
      std::unique_ptr<views::View> option_row_content =
          CreateShippingOptionLabel(
              selected_option,
              spec()->GetFormattedCurrencyAmount(selected_option->amount),
              /*emphasize_label=*/false);
      return builder.Id(DialogViewID::PAYMENT_SHEET_SHIPPING_OPTION_SECTION)
          .CreateWithChevron(std::move(option_row_content), nullptr);
    } else {
      // 1.3 There are options, none are selected: show the enabled Choose
      // button.
      return builder
          .Id(DialogViewID::PAYMENT_SHEET_SHIPPING_OPTION_SECTION_BUTTON)
          .CreateWithButton(
              base::UTF8ToUTF16(spec()->details().shipping_options[0]->label),
              l10n_util::GetPluralStringFUTF16(
                  IDS_PAYMENT_REQUEST_SHIPPING_OPTIONS_PREVIEW,
                  spec()->details().shipping_options.size() - 1),
              spec()->details().shipping_options.size() - 1,
              l10n_util::GetStringUTF16(IDS_CHOOSE),
              /*button_enabled=*/true);
    }
  } else {
    // 2. There is no selected address: do not show the shipping option section.
    return nullptr;
  }
}

std::unique_ptr<views::View> PaymentSheetViewController::CreateDataSourceRow() {
  std::unique_ptr<views::View> content_view = base::MakeUnique<views::View>();
  views::BoxLayout* layout = new views::BoxLayout(
      views::BoxLayout::kVertical, kPaymentRequestRowHorizontalInsets, 0, 0);
  layout->set_main_axis_alignment(views::BoxLayout::MAIN_AXIS_ALIGNMENT_START);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CROSS_AXIS_ALIGNMENT_START);
  content_view->SetLayoutManager(layout);

  base::string16 data_source;
  // If no transaction has been completed so far, choose which string to display
  // as a function of the profile's signed in state. Otherwise, always show the
  // same string.
  bool first_transaction_completed =
      dialog()->GetProfile()->GetPrefs()->GetBoolean(
          payments::kPaymentsFirstTransactionCompleted);
  if (first_transaction_completed) {
    data_source =
        l10n_util::GetStringUTF16(IDS_PAYMENTS_CARD_AND_ADDRESS_SETTINGS);
  } else {
    std::string user_email = state()->GetAuthenticatedEmail();
    if (!user_email.empty()) {
      // Insert the user's email into the format string.
      data_source = l10n_util::GetStringFUTF16(
          IDS_PAYMENTS_CARD_AND_ADDRESS_SETTINGS_SIGNED_IN,
          base::UTF8ToUTF16(user_email));
    } else {
      data_source = l10n_util::GetStringUTF16(
          IDS_PAYMENTS_CARD_AND_ADDRESS_SETTINGS_SIGNED_OUT);
    }
  }

  // The translated string will surround the actual "Settings" substring with
  // BEGIN_LINK and END_LINK. Find the beginning of the link range and the
  // length of the "settings" part, then remove the BEGIN_LINK and END_LINK
  // parts and linkify "settings".
  base::string16 begin_tag = base::UTF8ToUTF16("BEGIN_LINK");
  base::string16 end_tag = base::UTF8ToUTF16("END_LINK");
  size_t link_begin = data_source.find(begin_tag);
  DCHECK(link_begin != base::string16::npos);

  size_t link_end = data_source.find(end_tag);
  DCHECK(link_end != base::string16::npos);

  size_t link_length = link_end - link_begin - begin_tag.size();
  data_source.erase(link_end, end_tag.size());
  data_source.erase(link_begin, begin_tag.size());

  std::unique_ptr<views::StyledLabel> data_source_label =
      base::MakeUnique<views::StyledLabel>(data_source, this);
  data_source_label->SetBorder(views::CreateEmptyBorder(22, 0, 0, 0));

  views::StyledLabel::RangeStyleInfo default_style;
  default_style.color = data_source_label->GetNativeTheme()->GetSystemColor(
      ui::NativeTheme::kColorId_LabelDisabledColor);
  data_source_label->SetDefaultStyle(default_style);
  data_source_label->AddStyleRange(
      gfx::Range(link_begin, link_begin + link_length),
      views::StyledLabel::RangeStyleInfo::CreateForLink());
  data_source_label->SizeToFit(0);
  content_view->AddChildView(data_source_label.release());
  return content_view;
}

}  // namespace payments
