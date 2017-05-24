// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_MESSAGE_CENTER_VIEWS_MESSAGE_VIEW_H_
#define UI_MESSAGE_CENTER_VIEWS_MESSAGE_VIEW_H_

#include <memory>

#include "base/macros.h"
#include "base/strings/string16.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/message_center/message_center_export.h"
#include "ui/message_center/notification.h"
#include "ui/message_center/views/slide_out_controller.h"
#include "ui/views/view.h"

namespace views {
class Painter;
class ScrollView;
}

namespace message_center {

class MessageCenterController;

// Individual notifications constants.
const int kPaddingBetweenItems = 10;
const int kPaddingHorizontal = 18;
const int kWebNotificationButtonWidth = 32;
const int kWebNotificationIconSize = 40;

// An base class for a notification entry. Contains background and other
// elements shared by derived notification views.
class MESSAGE_CENTER_EXPORT MessageView
    : public views::View,
      public views::SlideOutController::Delegate {
 public:
  MessageView(MessageCenterController* controller,
              const Notification& notification);
  ~MessageView() override;

  // Updates this view with the new data contained in the notification.
  virtual void UpdateWithNotification(const Notification& notification);

  // Returns the insets for the shadow it will have for rich notification.
  static gfx::Insets GetShadowInsets();

  // Creates a shadow around the notification and changes slide-out behavior.
  void SetIsNested();

  virtual bool IsCloseButtonFocused() const = 0;
  virtual void RequestFocusOnCloseButton() = 0;
  virtual void UpdateControlButtonsVisibility() = 0;

  void OnCloseButtonPressed();

  // views::View
  void GetAccessibleNodeData(ui::AXNodeData* node_data) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool OnKeyReleased(const ui::KeyEvent& event) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnFocus() override;
  void OnBlur() override;
  void Layout() override;
  void OnGestureEvent(ui::GestureEvent* event) override;

  // views::SlideOutController::Delegate
  ui::Layer* GetSlideOutLayer() override;
  void OnSlideChanged() override;
  void OnSlideOut() override;

  void set_scroller(views::ScrollView* scroller) { scroller_ = scroller; }
  std::string notification_id() { return notification_id_; }
  NotifierId notifier_id() { return notifier_id_; }
  const base::string16& display_source() const { return display_source_; }
  bool pinned() const { return pinned_; }

  void set_controller(MessageCenterController* controller) {
    controller_ = controller;
  }

 protected:
  // Creates and add close button to view hierarchy when necessary. Derived
  // classes should call this after its view hierarchy is populated to ensure
  // it is on top of other views.
  void CreateOrUpdateCloseButtonView(const Notification& notification);

  // Changes the background color being used by |background_view_| and schedules
  // a paint.
  virtual void SetDrawBackgroundAsActive(bool active);

  views::View* background_view() { return background_view_; }
  views::ScrollView* scroller() { return scroller_; }
  MessageCenterController* controller() { return controller_; }

 private:
  MessageCenterController* controller_;  // Weak, lives longer then views.
  std::string notification_id_;
  NotifierId notifier_id_;
  views::View* background_view_ = nullptr;  // Owned by views hierarchy.
  views::ScrollView* scroller_ = nullptr;

  base::string16 accessible_name_;

  base::string16 display_source_;
  bool pinned_ = false;

  std::unique_ptr<views::Painter> focus_painter_;

  views::SlideOutController slide_out_controller_;

  // True if |this| is embedded in another view. Equivalent to |!top_level| in
  // MessageViewFactory parlance.
  bool is_nested_ = false;

  DISALLOW_COPY_AND_ASSIGN(MessageView);
};

}  // namespace message_center

#endif // UI_MESSAGE_CENTER_VIEWS_MESSAGE_VIEW_H_
