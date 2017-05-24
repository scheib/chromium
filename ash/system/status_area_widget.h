// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_STATUS_AREA_WIDGET_H_
#define ASH_SYSTEM_STATUS_AREA_WIDGET_H_

#include "ash/ash_export.h"
#include "ash/login_status.h"
#include "ash/public/cpp/shelf_types.h"
#include "ash/shelf/shelf_background_animator_observer.h"
#include "base/macros.h"
#include "ui/views/widget/widget.h"

namespace aura {
class Window;
}

namespace ash {
class ImeMenuTray;
class LockScreenActionTray;
class LogoutButtonTray;
class OverviewButtonTray;
class PaletteTray;
class StatusAreaWidgetDelegate;
class SystemTray;
class VirtualKeyboardTray;
class WebNotificationTray;
class WmShelf;

class ASH_EXPORT StatusAreaWidget : public views::Widget,
                                    public ShelfBackgroundAnimatorObserver {
 public:
  StatusAreaWidget(aura::Window* status_container, WmShelf* wm_shelf);
  ~StatusAreaWidget() override;

  // Creates the SystemTray, WebNotificationTray and LogoutButtonTray.
  void CreateTrayViews();

  // Destroys the system tray and web notification tray. Called before
  // tearing down the windows to avoid shutdown ordering issues.
  void Shutdown();

  // Update the alignment of the widget and tray views.
  void UpdateAfterShelfAlignmentChange();

  // Called by the client when the login status changes. Caches login_status
  // and calls UpdateAfterLoginStatusChange for the system tray and the web
  // notification tray.
  void UpdateAfterLoginStatusChange(LoginStatus login_status);

  StatusAreaWidgetDelegate* status_area_widget_delegate() {
    return status_area_widget_delegate_;
  }
  SystemTray* system_tray() { return system_tray_; }
  WebNotificationTray* web_notification_tray() {
    return web_notification_tray_;
  }
  OverviewButtonTray* overview_button_tray() { return overview_button_tray_; }

  PaletteTray* palette_tray() { return palette_tray_; }

  ImeMenuTray* ime_menu_tray() { return ime_menu_tray_; }

  WmShelf* wm_shelf() { return wm_shelf_; }

  LoginStatus login_status() const { return login_status_; }

  // Returns true if the shelf should be visible. This is used when the
  // shelf is configured to auto-hide and test if the shelf should force
  // the shelf to remain visible.
  bool ShouldShowShelf() const;

  // True if any message bubble is shown.
  bool IsMessageBubbleShown() const;

  // Notifies child trays, and the |status_area_widget_delegate_| to schedule a
  // paint.
  void SchedulePaint();

  // Overridden from views::Widget:
  const ui::NativeTheme* GetNativeTheme() const override;
  void OnNativeWidgetActivationChanged(bool active) override;

  // ShelfBackgroundAnimatorObserver:
  void UpdateShelfItemBackground(SkColor color) override;

  LockScreenActionTray* lock_screen_action_tray_for_testing() {
    return lock_screen_action_tray_;
  }

  LogoutButtonTray* logout_button_tray_for_testing() {
    return logout_button_tray_;
  }

  VirtualKeyboardTray* virtual_keyboard_tray_for_testing() {
    return virtual_keyboard_tray_;
  }

 private:
  void AddSystemTray();
  void AddWebNotificationTray();
  void AddLockScreenActionTray();
  void AddLogoutButtonTray();
  void AddPaletteTray();
  void AddVirtualKeyboardTray();
  void AddImeMenuTray();
  void AddOverviewButtonTray();

  // Weak pointers to View classes that are parented to StatusAreaWidget:
  StatusAreaWidgetDelegate* status_area_widget_delegate_;
  OverviewButtonTray* overview_button_tray_;
  SystemTray* system_tray_;
  WebNotificationTray* web_notification_tray_;
  LockScreenActionTray* lock_screen_action_tray_;
  LogoutButtonTray* logout_button_tray_;
  PaletteTray* palette_tray_;
  VirtualKeyboardTray* virtual_keyboard_tray_;
  ImeMenuTray* ime_menu_tray_;
  LoginStatus login_status_;

  WmShelf* wm_shelf_;

  DISALLOW_COPY_AND_ASSIGN(StatusAreaWidget);
};

}  // namespace ash

#endif  // ASH_SYSTEM_STATUS_AREA_WIDGET_H_
