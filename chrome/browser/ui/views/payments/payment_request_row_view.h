// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PAYMENTS_PAYMENT_REQUEST_ROW_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_PAYMENTS_PAYMENT_REQUEST_ROW_VIEW_H_

#include "base/macros.h"
#include "ui/views/controls/button/custom_button.h"

namespace payments {

// This class implements a clickable row of the Payment Request dialog that
// darkens on hover and displays a horizontal ruler on its lower bound.
class PaymentRequestRowView : public views::CustomButton {
 public:
  // Creates a row view. If |clickable| is true, the row will be shaded on hover
  // and handle click events. |insets| are used as padding around the content.
  PaymentRequestRowView(views::ButtonListener* listener,
                        bool clickable,
                        const gfx::Insets& insets);
  ~PaymentRequestRowView() override;

 private:
  // Sets this row's background to the theme's hovered color to indicate that
  // it's begin hovered or it's focused.
  void SetActiveBackground();

  // Show/hide the separator at the bottom of the row. This is used to hide the
  // separator when the row is hovered.
  void ShowBottomSeparator();
  void HideBottomSeparator();

  // Sets the row as |highlighted| or not. A row is highlighted if it's hovered
  // on or focused, in which case it hides its bottom separator and gets a light
  // colored background color.
  void SetIsHighlighted(bool highlighted);

  // views::CustomButton:
  void StateChanged(ButtonState old_state) override;

  // views::View:
  void OnFocus() override;
  void OnBlur() override;

  bool clickable_;
  gfx::Insets insets_;

  DISALLOW_COPY_AND_ASSIGN(PaymentRequestRowView);
};

}  // namespace payments

#endif  // CHROME_BROWSER_UI_VIEWS_PAYMENTS_PAYMENT_REQUEST_ROW_VIEW_H_
