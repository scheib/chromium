// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/message_center/views/custom_notification_view.h"

#include <algorithm>

#include "ui/base/ime/input_method.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/gfx/geometry/size.h"
#include "ui/message_center/message_center_style.h"
#include "ui/message_center/views/message_center_controller.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/painter.h"

namespace message_center {

// static
const char CustomNotificationView::kViewClassName[] = "CustomNotificationView";

CustomNotificationView::CustomNotificationView(
    MessageCenterController* controller,
    const Notification& notification)
    : MessageView(controller, notification) {
  DCHECK_EQ(NOTIFICATION_TYPE_CUSTOM, notification.type());

  auto custom_content = notification.delegate()->CreateCustomContent();

  contents_view_ = custom_content->view.release();
  DCHECK(contents_view_);
  AddChildView(contents_view_);

  contents_view_delegate_ = std::move(custom_content->delegate);
  DCHECK(contents_view_delegate_);

  if (contents_view_->background()) {
    background_view()->background()->SetNativeControlColor(
        contents_view_->background()->get_color());
  }

  focus_painter_ = views::Painter::CreateSolidFocusPainter(
      kFocusBorderColor, gfx::Insets(0, 1, 3, 2));
}

CustomNotificationView::~CustomNotificationView() {}

void CustomNotificationView::OnContentFocused() {
  SchedulePaint();
}

void CustomNotificationView::OnContentBlured() {
  SchedulePaint();
}

void CustomNotificationView::SetDrawBackgroundAsActive(bool active) {
  // Do nothing if |contents_view_| has a background.
  if (contents_view_->background())
    return;

  MessageView::SetDrawBackgroundAsActive(active);
}

bool CustomNotificationView::IsCloseButtonFocused() const {
  if (!contents_view_delegate_)
    return false;
  return contents_view_delegate_->IsCloseButtonFocused();
}

void CustomNotificationView::RequestFocusOnCloseButton() {
  if (contents_view_delegate_)
    contents_view_delegate_->RequestFocusOnCloseButton();
}

const char* CustomNotificationView::GetClassName() const {
  return kViewClassName;
}

void CustomNotificationView::UpdateControlButtonsVisibility() {
  if (contents_view_delegate_)
    contents_view_delegate_->UpdateControlButtonsVisibility();
}

void CustomNotificationView::OnSlideChanged() {
  if (contents_view_delegate_)
    contents_view_delegate_->OnSlideChanged();
}

gfx::Size CustomNotificationView::GetPreferredSize() const {
  const gfx::Insets insets = GetInsets();
  const int contents_width = kNotificationWidth - insets.width();
  const int contents_height = contents_view_->GetHeightForWidth(contents_width);
  return gfx::Size(kNotificationWidth, contents_height + insets.height());
}

void CustomNotificationView::Layout() {
  MessageView::Layout();

  contents_view_->SetBoundsRect(GetContentsBounds());

  // If the content view claims focus, defer focus handling to the content view.
  if (contents_view_->IsFocusable())
    SetFocusBehavior(FocusBehavior::NEVER);
}

bool CustomNotificationView::HasFocus() const {
  // In case that focus handling is defered to the content view, asking the
  // content view about focus.
  if (contents_view_ && contents_view_->IsFocusable())
    return contents_view_->HasFocus();
  else
    return MessageView::HasFocus();
}

void CustomNotificationView::RequestFocus() {
  if (contents_view_ && contents_view_->IsFocusable())
    contents_view_->RequestFocus();
  else
    MessageView::RequestFocus();
}

void CustomNotificationView::OnPaint(gfx::Canvas* canvas) {
  MessageView::OnPaint(canvas);
  if (contents_view_ && contents_view_->IsFocusable())
    views::Painter::PaintFocusPainter(contents_view_, canvas,
                                      focus_painter_.get());
}

bool CustomNotificationView::OnKeyPressed(const ui::KeyEvent& event) {
  if (contents_view_) {
    ui::InputMethod* input_method = contents_view_->GetInputMethod();
    if (input_method) {
      ui::TextInputClient* text_input_client =
          input_method->GetTextInputClient();
      if (text_input_client &&
          text_input_client->GetTextInputType() != ui::TEXT_INPUT_TYPE_NONE) {
        // If the focus is in an edit box, we skip the special key handling for
        // back space and return keys. So that these key events are sent to the
        // arc container correctly without being handled by the message center.
        return false;
      }
    }
  }

  return MessageView::OnKeyPressed(event);
}

void CustomNotificationView::ChildPreferredSizeChanged(View* child) {
  // Notify MessageCenterController when the custom content changes size,
  // as it may need to relayout.
  if (controller())
    controller()->UpdateNotificationSize(notification_id());
}

bool CustomNotificationView::HandleAccessibleAction(
    const ui::AXActionData& action) {
  if (contents_view_)
    return contents_view_->HandleAccessibleAction(action);
  return false;
}

}  // namespace message_center
