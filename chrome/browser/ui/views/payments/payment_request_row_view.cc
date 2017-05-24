// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/payments/payment_request_row_view.h"

#include "chrome/browser/ui/views/payments/payment_request_views_util.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/widget/widget.h"

namespace payments {

PaymentRequestRowView::PaymentRequestRowView(views::ButtonListener* listener,
                                             bool clickable,
                                             const gfx::Insets& insets)
    : views::CustomButton(listener), clickable_(clickable), insets_(insets) {
  SetEnabled(clickable_);
  ShowBottomSeparator();
  SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
}

PaymentRequestRowView::~PaymentRequestRowView() {}

void PaymentRequestRowView::SetActiveBackground() {
  ui::NativeTheme* theme = GetWidget()->GetNativeTheme();
  set_background(views::Background::CreateSolidBackground(theme->GetSystemColor(
      ui::NativeTheme::kColorId_ResultsTableHoveredBackground)));
}

void PaymentRequestRowView::ShowBottomSeparator() {
  SetBorder(payments::CreatePaymentRequestRowBorder(
      GetNativeTheme()->GetSystemColor(
          ui::NativeTheme::kColorId_SeparatorColor),
      insets_));
}

void PaymentRequestRowView::HideBottomSeparator() {
  SetBorder(views::NullBorder());
}

void PaymentRequestRowView::SetIsHighlighted(bool highlighted) {
  if (highlighted) {
    SetActiveBackground();
    HideBottomSeparator();
  } else {
    set_background(nullptr);
    ShowBottomSeparator();
  }
}

// views::CustomButton:
void PaymentRequestRowView::StateChanged(ButtonState old_state) {
  if (!clickable_)
    return;

  SetIsHighlighted(state() == views::Button::STATE_HOVERED ||
                   state() == views::Button::STATE_PRESSED);
}

void PaymentRequestRowView::OnFocus() {
  if (clickable_) {
    SetIsHighlighted(true);
    SchedulePaint();
  }
}

void PaymentRequestRowView::OnBlur() {
  if (clickable_) {
    SetIsHighlighted(false);
    SchedulePaint();
  }
}

}  // namespace payments
