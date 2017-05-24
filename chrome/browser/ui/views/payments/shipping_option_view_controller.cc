// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/payments/shipping_option_view_controller.h"

#include "chrome/browser/ui/views/payments/payment_request_dialog_view.h"
#include "chrome/browser/ui/views/payments/payment_request_views_util.h"
#include "components/payments/content/payment_request_spec.h"
#include "components/payments/content/payment_request_state.h"
#include "components/payments/core/strings_util.h"
#include "ui/views/layout/fill_layout.h"

namespace payments {

namespace {

class ShippingOptionItem : public PaymentRequestItemList::Item {
 public:
  ShippingOptionItem(mojom::PaymentShippingOption* shipping_option,
                     PaymentRequestSpec* spec,
                     PaymentRequestState* state,
                     PaymentRequestItemList* parent_list,
                     PaymentRequestDialogView* dialog,
                     bool selected)
      : PaymentRequestItemList::Item(spec,
                                     state,
                                     parent_list,
                                     selected,
                                     /*show_edit_button=*/false),
        shipping_option_(shipping_option) {}
  ~ShippingOptionItem() override {}

 private:
  // payments::PaymentRequestItemList::Item:
  std::unique_ptr<views::View> CreateContentView() override {
    return CreateShippingOptionLabel(
        shipping_option_,
        spec()->GetFormattedCurrencyAmount(shipping_option_->amount),
        /*emphasize_label=*/true);
  }

  void SelectedStateChanged() override {
    if (selected()) {
      state()->SetSelectedShippingOption(shipping_option_->id);
    }
  }

  bool IsEnabled() override {
    // Shipping options are vetted by the website; none are disabled.
    return true;
  }

  bool CanBeSelected() override {
    // Shipping options are vetted by the website; they're all OK to select.
    return true;
  }

  void PerformSelectionFallback() override {
    // Since CanBeSelected() is always true, this should never be called.
    NOTREACHED();
  }

  void EditButtonPressed() override {
    // This subclass doesn't display the edit button.
    NOTREACHED();
  }

  mojom::PaymentShippingOption* shipping_option_;

  DISALLOW_COPY_AND_ASSIGN(ShippingOptionItem);
};

}  // namespace

ShippingOptionViewController::ShippingOptionViewController(
    PaymentRequestSpec* spec,
    PaymentRequestState* state,
    PaymentRequestDialogView* dialog)
    : PaymentRequestSheetController(spec, state, dialog) {
  spec->AddObserver(this);
  for (const auto& option : spec->details().shipping_options) {
    shipping_option_list_.AddItem(base::MakeUnique<ShippingOptionItem>(
        option.get(), spec, state, &shipping_option_list_, dialog,
        option.get() == spec->selected_shipping_option()));
  }
}

ShippingOptionViewController::~ShippingOptionViewController() {
  spec()->RemoveObserver(this);
}

void ShippingOptionViewController::OnSpecUpdated() {
  if (spec()->current_update_reason() ==
      PaymentRequestSpec::UpdateReason::SHIPPING_OPTION) {
    dialog()->GoBack();
  } else {
    UpdateContentView();
  }
}

base::string16 ShippingOptionViewController::GetSheetTitle() {
  return GetShippingOptionSectionString(spec()->shipping_type());
}

void ShippingOptionViewController::FillContentView(views::View* content_view) {
  content_view->SetLayoutManager(new views::FillLayout);
  content_view->AddChildView(shipping_option_list_.CreateListView().release());
}

std::unique_ptr<views::View>
ShippingOptionViewController::CreateExtraFooterView() {
  return nullptr;
}

}  // namespace payments
