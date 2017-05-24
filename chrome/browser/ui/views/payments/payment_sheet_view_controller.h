// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PAYMENTS_PAYMENT_SHEET_VIEW_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_PAYMENTS_PAYMENT_SHEET_VIEW_CONTROLLER_H_

#include <memory>

#include "base/macros.h"
#include "chrome/browser/ui/views/payments/payment_request_sheet_controller.h"
#include "components/payments/content/payment_request_spec.h"
#include "components/payments/content/payment_request_state.h"
#include "ui/views/controls/styled_label_listener.h"

namespace views {
class StyledLabel;
}

namespace payments {

class PaymentRequestDialogView;

// The PaymentRequestSheetController subtype for the Payment Sheet screen of the
// Payment Request dialog.
class PaymentSheetViewController : public PaymentRequestSheetController,
                                   public PaymentRequestSpec::Observer,
                                   public PaymentRequestState::Observer,
                                   public views::StyledLabelListener {
 public:
  // Does not take ownership of the arguments, which should outlive this object.
  PaymentSheetViewController(PaymentRequestSpec* spec,
                             PaymentRequestState* state,
                             PaymentRequestDialogView* dialog);
  ~PaymentSheetViewController() override;

  // PaymentRequestSpec::Observer:
  void OnSpecUpdated() override;

  // PaymentRequestState::Observer:
  void OnSelectedInformationChanged() override;

 private:
  // PaymentRequestSheetController:
  std::unique_ptr<views::Button> CreatePrimaryButton() override;
  base::string16 GetSecondaryButtonLabel() override;
  bool ShouldShowHeaderBackArrow() override;
  base::string16 GetSheetTitle() override;
  void FillContentView(views::View* content_view) override;
  std::unique_ptr<views::View> CreateExtraFooterView() override;
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

  // views::StyledLabelListener:
  void StyledLabelLinkClicked(views::StyledLabel* label,
                              const gfx::Range& range,
                              int event_flags) override;

  void UpdatePayButtonState(bool enabled);

  std::unique_ptr<views::View> CreateShippingSectionContent();
  std::unique_ptr<views::Button> CreateShippingRow();
  std::unique_ptr<views::Button> CreatePaymentSheetSummaryRow();
  std::unique_ptr<views::Button> CreatePaymentMethodRow();
  std::unique_ptr<views::View> CreateContactInfoSectionContent();
  std::unique_ptr<views::Button> CreateContactInfoRow();
  std::unique_ptr<views::Button> CreateShippingOptionRow();
  std::unique_ptr<views::View> CreateDataSourceRow();

  DISALLOW_COPY_AND_ASSIGN(PaymentSheetViewController);
};

}  // namespace payments

#endif  // CHROME_BROWSER_UI_VIEWS_PAYMENTS_PAYMENT_SHEET_VIEW_CONTROLLER_H_
