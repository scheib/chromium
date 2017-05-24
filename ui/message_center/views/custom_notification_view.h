// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_MESSAGE_CENTER_VIEWS_CUSTOM_NOTIFICATION_VIEW_H_
#define UI_MESSAGE_CENTER_VIEWS_CUSTOM_NOTIFICATION_VIEW_H_

#include "base/macros.h"
#include "ui/message_center/message_center_export.h"
#include "ui/message_center/views/message_view.h"

namespace ui {
struct AXActionData;
}

namespace views {
class Painter;
}

namespace message_center {

// View for notification with NOTIFICATION_TYPE_CUSTOM that hosts the custom
// content of the notification.
class MESSAGE_CENTER_EXPORT CustomNotificationView : public MessageView {
 public:
  static const char kViewClassName[];

  CustomNotificationView(MessageCenterController* controller,
                         const Notification& notification);
  ~CustomNotificationView() override;

  // These method are called by the content view when focus handling is defered
  // to the content.
  void OnContentFocused();
  void OnContentBlured();

  // Overridden from MessageView:
  void SetDrawBackgroundAsActive(bool active) override;
  bool IsCloseButtonFocused() const override;
  void RequestFocusOnCloseButton() override;
  void UpdateControlButtonsVisibility() override;

  // views::SlideOutController::Delegate:
  void OnSlideChanged() override;

  // Overridden from views::View:
  const char* GetClassName() const override;
  gfx::Size GetPreferredSize() const override;
  void Layout() override;
  bool HasFocus() const override;
  void RequestFocus() override;
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  void ChildPreferredSizeChanged(View* child) override;
  bool HandleAccessibleAction(const ui::AXActionData& action) override;

 private:
  friend class CustomNotificationViewTest;

  // The view for the custom content. Owned by view hierarchy.
  views::View* contents_view_ = nullptr;
  std::unique_ptr<CustomNotificationContentViewDelegate>
      contents_view_delegate_;

  std::unique_ptr<views::Painter> focus_painter_;

  DISALLOW_COPY_AND_ASSIGN(CustomNotificationView);
};

}  // namespace message_center

#endif  // UI_MESSAGE_CENTER_VIEWS_CUSTOM_NOTIFICATION_VIEW_H_
