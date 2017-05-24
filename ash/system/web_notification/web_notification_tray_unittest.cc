// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/web_notification/web_notification_tray.h"

#include <utility>
#include <vector>

#include "ash/public/cpp/config.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/shelf/shelf_layout_manager.h"
#include "ash/shelf/wm_shelf.h"
#include "ash/shell.h"
#include "ash/system/screen_layout_observer.h"
#include "ash/system/status_area_widget.h"
#include "ash/system/tray/system_tray.h"
#include "ash/system/tray/system_tray_item.h"
#include "ash/system/web_notification/ash_popup_alignment_delegate.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/status_area_widget_test_helper.h"
#include "ash/test/test_system_tray_delegate.h"
#include "ash/wm/window_state.h"
#include "base/memory/ptr_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/display/display.h"
#include "ui/display/manager/display_manager.h"
#include "ui/display/screen.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/message_center/message_center_style.h"
#include "ui/message_center/message_center_tray.h"
#include "ui/message_center/notification_list.h"
#include "ui/message_center/notification_types.h"
#include "ui/message_center/views/message_center_bubble.h"
#include "ui/message_center/views/message_popup_collection.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ash {

namespace {

WebNotificationTray* GetTray() {
  return StatusAreaWidgetTestHelper::GetStatusAreaWidget()
      ->web_notification_tray();
}

WebNotificationTray* GetSecondaryTray() {
  StatusAreaWidget* status_area_widget =
      StatusAreaWidgetTestHelper::GetSecondaryStatusAreaWidget();
  if (status_area_widget)
    return status_area_widget->web_notification_tray();
  return NULL;
}

message_center::MessageCenter* GetMessageCenter() {
  return GetTray()->message_center();
}

SystemTray* GetSystemTray() {
  return StatusAreaWidgetTestHelper::GetStatusAreaWidget()->system_tray();
}

// Trivial item implementation for testing PopupAndSystemTray test case.
class TestItem : public SystemTrayItem {
 public:
  TestItem() : SystemTrayItem(GetSystemTray(), UMA_TEST) {}

  views::View* CreateDefaultView(LoginStatus status) override {
    views::View* default_view = new views::View;
    default_view->SetLayoutManager(new views::FillLayout);
    default_view->AddChildView(new views::Label(base::UTF8ToUTF16("Default")));
    return default_view;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestItem);
};

}  // namespace

class WebNotificationTrayTest : public test::AshTestBase {
 public:
  WebNotificationTrayTest() {}
  ~WebNotificationTrayTest() override {}

  void TearDown() override {
    GetMessageCenter()->RemoveAllNotifications(
        false /* by_user */, message_center::MessageCenter::RemoveType::ALL);
    test::AshTestBase::TearDown();
  }

 protected:
  void AddNotification(const std::string& id) {
    std::unique_ptr<message_center::Notification> notification;
    notification.reset(new message_center::Notification(
        message_center::NOTIFICATION_TYPE_SIMPLE, id,
        base::ASCIIToUTF16("Test Web Notification"),
        base::ASCIIToUTF16("Notification message body."), gfx::Image(),
        base::ASCIIToUTF16("www.test.org"), GURL(),
        message_center::NotifierId(), message_center::RichNotificationData(),
        NULL /* delegate */));
    GetMessageCenter()->AddNotification(std::move(notification));
  }

  void UpdateNotification(const std::string& old_id,
                          const std::string& new_id) {
    std::unique_ptr<message_center::Notification> notification;
    notification.reset(new message_center::Notification(
        message_center::NOTIFICATION_TYPE_SIMPLE, new_id,
        base::ASCIIToUTF16("Updated Web Notification"),
        base::ASCIIToUTF16("Updated message body."), gfx::Image(),
        base::ASCIIToUTF16("www.test.org"), GURL(),
        message_center::NotifierId(), message_center::RichNotificationData(),
        NULL /* delegate */));
    GetMessageCenter()->UpdateNotification(old_id, std::move(notification));
  }

  void RemoveNotification(const std::string& id) {
    GetMessageCenter()->RemoveNotification(id, false);
  }

  views::Widget* GetWidget() { return GetTray()->GetWidget(); }

  int GetPopupWorkAreaBottom() {
    return GetPopupWorkAreaBottomForTray(GetTray());
  }

  int GetPopupWorkAreaBottomForTray(WebNotificationTray* tray) {
    return tray->popup_alignment_delegate_->GetWorkArea().bottom();
  }

  bool IsPopupVisible() { return GetTray()->IsPopupVisible(); }

  static std::unique_ptr<views::Widget> CreateTestWidget() {
    return AshTestBase::CreateTestWidget(
        nullptr, kShellWindowId_DefaultContainer, gfx::Rect(1, 2, 3, 4));
  }

  static void UpdateAutoHideStateNow() {
    GetPrimaryShelf()->shelf_layout_manager()->UpdateAutoHideStateNow();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(WebNotificationTrayTest);
};

TEST_F(WebNotificationTrayTest, WebNotifications) {
  // TODO(mukai): move this test case to ui/message_center.
  ASSERT_TRUE(GetWidget());

  // Add a notification.
  AddNotification("test_id1");
  EXPECT_EQ(1u, GetMessageCenter()->NotificationCount());
  EXPECT_TRUE(GetMessageCenter()->FindVisibleNotificationById("test_id1"));
  AddNotification("test_id2");
  AddNotification("test_id2");
  EXPECT_EQ(2u, GetMessageCenter()->NotificationCount());
  EXPECT_TRUE(GetMessageCenter()->FindVisibleNotificationById("test_id2"));

  // Ensure that updating a notification does not affect the count.
  UpdateNotification("test_id2", "test_id3");
  UpdateNotification("test_id3", "test_id3");
  EXPECT_EQ(2u, GetMessageCenter()->NotificationCount());
  EXPECT_FALSE(GetMessageCenter()->FindVisibleNotificationById("test_id2"));

  // Ensure that Removing the first notification removes it from the tray.
  RemoveNotification("test_id1");
  EXPECT_FALSE(GetMessageCenter()->FindVisibleNotificationById("test_id1"));
  EXPECT_EQ(1u, GetMessageCenter()->NotificationCount());

  // Remove the remianing notification.
  RemoveNotification("test_id3");
  EXPECT_EQ(0u, GetMessageCenter()->NotificationCount());
  EXPECT_FALSE(GetMessageCenter()->FindVisibleNotificationById("test_id3"));
}

TEST_F(WebNotificationTrayTest, WebNotificationPopupBubble) {
  // TODO(mukai): move this test case to ui/message_center.
  ASSERT_TRUE(GetWidget());

  // Adding a notification should show the popup bubble.
  AddNotification("test_id1");
  EXPECT_TRUE(GetTray()->IsPopupVisible());

  // Updating a notification should not hide the popup bubble.
  AddNotification("test_id2");
  UpdateNotification("test_id2", "test_id3");
  EXPECT_TRUE(GetTray()->IsPopupVisible());

  // Removing the first notification should not hide the popup bubble.
  RemoveNotification("test_id1");
  EXPECT_TRUE(GetTray()->IsPopupVisible());

  // Removing the visible notification should hide the popup bubble.
  RemoveNotification("test_id3");
  EXPECT_FALSE(GetTray()->IsPopupVisible());

  // Now test that we can show multiple popups and then show the message center.
  AddNotification("test_id4");
  AddNotification("test_id5");
  EXPECT_TRUE(GetTray()->IsPopupVisible());

  GetTray()->message_center_tray_->ShowMessageCenterBubble();
  GetTray()->message_center_tray_->HideMessageCenterBubble();

  EXPECT_FALSE(GetTray()->IsPopupVisible());
}

using message_center::NotificationList;

// Flakily fails. http://crbug.com/229791
TEST_F(WebNotificationTrayTest, DISABLED_ManyMessageCenterNotifications) {
  // Add the max visible notifications +1, ensure the correct visible number.
  size_t notifications_to_add =
      message_center::kMaxVisibleMessageCenterNotifications + 1;
  for (size_t i = 0; i < notifications_to_add; ++i) {
    std::string id = base::StringPrintf("test_id%d", static_cast<int>(i));
    AddNotification(id);
  }
  bool shown = GetTray()->message_center_tray_->ShowMessageCenterBubble();
  EXPECT_TRUE(shown);
  RunAllPendingInMessageLoop();
  EXPECT_TRUE(GetTray()->message_center_bubble() != NULL);
  EXPECT_EQ(notifications_to_add, GetMessageCenter()->NotificationCount());
  EXPECT_EQ(
      message_center::kMaxVisibleMessageCenterNotifications,
      GetTray()->GetMessageCenterBubbleForTest()->NumMessageViewsForTest());
}

// Flakily times out. http://crbug.com/229792
TEST_F(WebNotificationTrayTest, DISABLED_ManyPopupNotifications) {
  // Add the max visible popup notifications +1, ensure the correct num visible.
  size_t notifications_to_add =
      message_center::kMaxVisiblePopupNotifications + 1;
  for (size_t i = 0; i < notifications_to_add; ++i) {
    std::string id = base::StringPrintf("test_id%d", static_cast<int>(i));
    AddNotification(id);
  }
  GetTray()->ShowPopups();
  EXPECT_TRUE(GetTray()->IsPopupVisible());
  EXPECT_EQ(notifications_to_add, GetMessageCenter()->NotificationCount());
  NotificationList::PopupNotifications popups =
      GetMessageCenter()->GetPopupNotifications();
  EXPECT_EQ(message_center::kMaxVisiblePopupNotifications, popups.size());
}

// Verifies if the notification appears on both displays when extended mode.
TEST_F(WebNotificationTrayTest, PopupShownOnBothDisplays) {
  // TODO: needs ScreenLayoutObserver, http://crbug.com/696752.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  Shell::Get()->screen_layout_observer()->set_show_notifications_for_testing(
      true);
  UpdateDisplay("400x400,200x200");
  // UpdateDisplay() creates the display notifications, so popup is visible.
  EXPECT_TRUE(GetTray()->IsPopupVisible());
  WebNotificationTray* secondary_tray = GetSecondaryTray();
  ASSERT_TRUE(secondary_tray);
  EXPECT_TRUE(secondary_tray->IsPopupVisible());

  // Transition to mirroring and then back to extended display, which recreates
  // root window controller and shelf with having notifications. This code
  // verifies it doesn't cause crash and popups are still visible. See
  // http://crbug.com/263664

  display_manager()->SetMultiDisplayMode(display::DisplayManager::MIRRORING);
  UpdateDisplay("400x400,200x200");
  EXPECT_TRUE(GetTray()->IsPopupVisible());
  EXPECT_FALSE(GetSecondaryTray());

  display_manager()->SetMultiDisplayMode(display::DisplayManager::EXTENDED);
  UpdateDisplay("400x400,200x200");
  EXPECT_TRUE(GetTray()->IsPopupVisible());
  secondary_tray = GetSecondaryTray();
  ASSERT_TRUE(secondary_tray);
  EXPECT_TRUE(secondary_tray->IsPopupVisible());
}

// PopupAndSystemTray may fail in platforms other than ChromeOS because the
// RootWindow's bound can be bigger than display::Display's work area so that
// openingsystem tray doesn't affect at all the work area of popups.
TEST_F(WebNotificationTrayTest, PopupAndSystemTray) {
  TestItem* test_item = new TestItem;
  GetSystemTray()->AddTrayItem(base::WrapUnique(test_item));

  AddNotification("test_id");
  EXPECT_TRUE(GetTray()->IsPopupVisible());
  int bottom = GetPopupWorkAreaBottom();

  // System tray is created, the popup's work area should be narrowed but still
  // visible.
  GetSystemTray()->ShowDefaultView(BUBBLE_CREATE_NEW);
  EXPECT_TRUE(GetTray()->IsPopupVisible());
  int bottom_with_tray = GetPopupWorkAreaBottom();
  EXPECT_GT(bottom, bottom_with_tray);
}

TEST_F(WebNotificationTrayTest, PopupAndAutoHideShelf) {
  AddNotification("test_id");
  EXPECT_TRUE(GetTray()->IsPopupVisible());
  int bottom = GetPopupWorkAreaBottom();

  // Shelf's auto-hide state won't be HIDDEN unless window exists.
  std::unique_ptr<views::Widget> widget(CreateTestWidget());
  WmShelf* shelf = GetPrimaryShelf();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);

  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  int bottom_auto_hidden = GetPopupWorkAreaBottom();
  EXPECT_LT(bottom, bottom_auto_hidden);

  // Close the window, which shows the shelf.
  widget.reset();
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  int bottom_auto_shown = GetPopupWorkAreaBottom();
  EXPECT_EQ(bottom, bottom_auto_shown);

  // Create the system tray during auto-hide.
  widget = CreateTestWidget();
  TestItem* test_item = new TestItem;
  GetSystemTray()->AddTrayItem(base::WrapUnique(test_item));
  GetSystemTray()->ShowDefaultView(BUBBLE_CREATE_NEW);
  UpdateAutoHideStateNow();

  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  EXPECT_TRUE(GetTray()->IsPopupVisible());
  int bottom_with_tray = GetPopupWorkAreaBottom();
  EXPECT_GT(bottom_auto_shown, bottom_with_tray);
}

TEST_F(WebNotificationTrayTest, PopupAndFullscreen) {
  AddNotification("test_id");
  EXPECT_TRUE(IsPopupVisible());
  int bottom = GetPopupWorkAreaBottom();

  // Checks the work area for normal auto-hidden state.
  std::unique_ptr<views::Widget> widget(CreateTestWidget());
  WmShelf* shelf = GetPrimaryShelf();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  int bottom_auto_hidden = GetPopupWorkAreaBottom();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);

  // Put |widget| into fullscreen without forcing the shelf to hide. Currently,
  // this is used by immersive fullscreen and forces the shelf to be auto
  // hidden.
  wm::GetWindowState(widget->GetNativeWindow())
      ->set_hide_shelf_when_fullscreen(false);
  widget->SetFullscreen(true);
  RunAllPendingInMessageLoop();

  // The work area for auto-hidden status of fullscreen is a bit larger
  // since it doesn't even have the 3-pixel width.
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  int bottom_fullscreen_hidden = GetPopupWorkAreaBottom();
  EXPECT_EQ(bottom_auto_hidden, bottom_fullscreen_hidden);

  // Move the mouse cursor at the bottom, which shows the shelf.
  ui::test::EventGenerator& generator = GetEventGenerator();
  gfx::Point bottom_right =
      display::Screen::GetScreen()->GetPrimaryDisplay().bounds().bottom_right();
  bottom_right.Offset(-1, -1);
  generator.MoveMouseTo(bottom_right);
  shelf->UpdateVisibilityState();
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  EXPECT_EQ(bottom, GetPopupWorkAreaBottom());

  generator.MoveMouseTo(
      display::Screen::GetScreen()->GetPrimaryDisplay().bounds().CenterPoint());
  shelf->UpdateVisibilityState();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  EXPECT_EQ(bottom_auto_hidden, GetPopupWorkAreaBottom());
}

TEST_F(WebNotificationTrayTest, PopupAndSystemTrayMultiDisplay) {
  UpdateDisplay("800x600,600x400");

  AddNotification("test_id");
  int bottom = GetPopupWorkAreaBottom();
  int bottom_second = GetPopupWorkAreaBottomForTray(GetSecondaryTray());

  // System tray is created on the primary display. The popups in the secondary
  // tray aren't affected.
  GetSystemTray()->ShowDefaultView(BUBBLE_CREATE_NEW);
  EXPECT_GT(bottom, GetPopupWorkAreaBottom());
  EXPECT_EQ(bottom_second, GetPopupWorkAreaBottomForTray(GetSecondaryTray()));
}

}  // namespace ash
