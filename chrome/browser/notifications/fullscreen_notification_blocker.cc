// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/fullscreen_notification_blocker.h"

#include "base/metrics/histogram_macros.h"
#include "base/time/time.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/fullscreen.h"
#include "content/public/browser/notification_service.h"
#include "ui/display/types/display_constants.h"
#include "ui/message_center/notifier_settings.h"

#if defined(USE_ASH)
#include "ash/root_window_controller.h"
#include "ash/shell.h"
#include "ash/system/system_notifier.h"
#include "ash/wm/window_state.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#endif

using message_center::NotifierId;

namespace {

bool DoesFullscreenModeBlockNotifications() {
#if defined(USE_ASH)
  if (ash::Shell::HasInstance()) {
    ash::RootWindowController* controller =
        ash::RootWindowController::ForTargetRootWindow();

    // During shutdown |controller| can be NULL.
    if (!controller)
      return false;

    // Block notifications if the shelf is hidden because of a fullscreen
    // window.
    const aura::Window* fullscreen_window =
        controller->GetWindowForFullscreenMode();
    if (!fullscreen_window)
      return false;
    return ash::wm::GetWindowState(fullscreen_window)
        ->hide_shelf_when_fullscreen();
  }
#endif
  // Fullscreen is global state on platforms other than chromeos.
  return IsFullScreenMode(display::kInvalidDisplayId);
}

}  // namespace

FullscreenNotificationBlocker::FullscreenNotificationBlocker(
    message_center::MessageCenter* message_center)
    : NotificationBlocker(message_center),
      is_fullscreen_mode_(false) {
  registrar_.Add(this, chrome::NOTIFICATION_FULLSCREEN_CHANGED,
                 content::NotificationService::AllSources());
}

FullscreenNotificationBlocker::~FullscreenNotificationBlocker() {
}

void FullscreenNotificationBlocker::CheckState() {
  bool was_fullscreen_mode = is_fullscreen_mode_;
  is_fullscreen_mode_ = DoesFullscreenModeBlockNotifications();
  if (is_fullscreen_mode_ != was_fullscreen_mode)
    NotifyBlockingStateChanged();
}

bool FullscreenNotificationBlocker::ShouldShowNotificationAsPopup(
    const message_center::Notification& notification) const {
  bool enabled = !is_fullscreen_mode_;
  if (is_fullscreen_mode_ && notification.delegate())
    enabled = notification.delegate()->ShouldDisplayOverFullscreen();

#if defined(USE_ASH)
  if (ash::Shell::HasInstance())
    enabled = enabled || ash::system_notifier::ShouldAlwaysShowPopups(
        notification.notifier_id());
#endif

  if (enabled && !is_fullscreen_mode_) {
    UMA_HISTOGRAM_ENUMERATION("Notifications.Display_Windowed",
                              notification.notifier_id().type,
                              NotifierId::SIZE);
  }

  return enabled;
}

void FullscreenNotificationBlocker::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(chrome::NOTIFICATION_FULLSCREEN_CHANGED, type);
  CheckState();
}
