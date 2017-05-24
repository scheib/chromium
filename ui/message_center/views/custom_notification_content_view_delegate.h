// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_MESSAGE_CENTER_CUSTOM_NOTIFICATION_CONTENT_VIEW_DELEGATE_H_
#define UI_MESSAGE_CENTER_CUSTOM_NOTIFICATION_CONTENT_VIEW_DELEGATE_H_

#include <memory>

#include "base/macros.h"
#include "ui/message_center/message_center_export.h"

namespace views {
class View;
}  // namespace views

namespace message_center {

// Delegate for a view that is hosted by CustomNotificationView.
class MESSAGE_CENTER_EXPORT CustomNotificationContentViewDelegate {
 public:
  virtual bool IsCloseButtonFocused() const = 0;
  virtual void RequestFocusOnCloseButton() = 0;
  virtual void UpdateControlButtonsVisibility() = 0;
  virtual void OnSlideChanged() = 0;
};

// The struct to hold a view and CustomNotificationContentViewDelegate
// associating with the view.
struct MESSAGE_CENTER_EXPORT CustomContent {
 public:
  CustomContent(
      std::unique_ptr<views::View> view,
      std::unique_ptr<CustomNotificationContentViewDelegate> delegate);
  ~CustomContent();

  std::unique_ptr<views::View> view;
  std::unique_ptr<CustomNotificationContentViewDelegate> delegate;

 private:
  DISALLOW_COPY_AND_ASSIGN(CustomContent);
};

}  // namespace message_center

#endif  // UI_MESSAGE_CENTER_CUSTOM_NOTIFICATION_CONTENT_VIEW_DELEGATE_H_
