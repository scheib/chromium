// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "chrome/browser/apps/app_browsertest_util.h"
#include "chrome/browser/extensions/api/notifications/extension_notification_display_helper.h"
#include "chrome/browser/extensions/api/notifications/extension_notification_display_helper_factory.h"
#include "chrome/browser/extensions/api/notifications/notifications_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/notifications/notification_common.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/notifications/notifier_state_tracker.h"
#include "chrome/browser/notifications/notifier_state_tracker_factory.h"
#include "chrome/browser/notifications/stub_notification_display_service.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/extensions/app_launch_params.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/common/chrome_features.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/api/test/test_api.h"
#include "extensions/browser/app_window/app_window.h"
#include "extensions/browser/app_window/app_window_registry.h"
#include "extensions/browser/app_window/native_app_window.h"
#include "extensions/browser/notification_types.h"
#include "extensions/common/features/feature.h"
#include "extensions/common/test_util.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/result_catcher.h"
#include "ui/message_center/notifier_settings.h"

#if defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#endif

using extensions::AppWindow;
using extensions::AppWindowRegistry;
using extensions::Extension;
using extensions::ExtensionNotificationDisplayHelper;
using extensions::ExtensionNotificationDisplayHelperFactory;
using extensions::ResultCatcher;

namespace utils = extension_function_test_utils;

namespace {

// A class that waits for a |chrome.test.sendMessage| call, ignores the message,
// and writes down the user gesture status of the message.
class UserGestureCatcher : public content::NotificationObserver {
 public:
  UserGestureCatcher() : waiting_(false) {
    registrar_.Add(this,
                   extensions::NOTIFICATION_EXTENSION_TEST_MESSAGE,
                   content::NotificationService::AllSources());
  }

  ~UserGestureCatcher() override {}

  bool GetNextResult() {
    if (results_.empty()) {
      waiting_ = true;
      content::RunMessageLoop();
      waiting_ = false;
    }

    if (!results_.empty()) {
      bool ret = results_.front();
      results_.pop_front();
      return ret;
    }
    NOTREACHED();
    return false;
  }

 private:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override {
    results_.push_back(
        static_cast<content::Source<extensions::TestSendMessageFunction> >(
            source)
            .ptr()
            ->user_gesture());
    if (waiting_)
      base::MessageLoopForUI::current()->QuitWhenIdle();
  }

  content::NotificationRegistrar registrar_;

  // A sequential list of user gesture notifications from the test extension(s).
  std::deque<bool> results_;

  // True if we're in a nested run loop waiting for results from
  // the extension.
  bool waiting_;
};

enum class WindowState {
  FULLSCREEN,
  NORMAL
};

class NotificationsApiTest : public ExtensionApiTest {
 public:
  const Extension* LoadExtensionAndWait(
      const std::string& test_name) {
    base::FilePath extdir = test_data_dir_.AppendASCII(test_name);
    content::WindowedNotificationObserver page_created(
        extensions::NOTIFICATION_EXTENSION_BACKGROUND_PAGE_READY,
        content::NotificationService::AllSources());
    const extensions::Extension* extension = LoadExtension(extdir);
    if (extension) {
      page_created.Wait();
    }
    return extension;
  }

  const Extension* LoadAppWithWindowState(
      const std::string& test_name, WindowState window_state) {
    const char* window_state_string = NULL;
    switch (window_state) {
      case WindowState::FULLSCREEN:
        window_state_string = "fullscreen";
        break;
      case WindowState::NORMAL:
        window_state_string = "normal";
        break;
    }
    const std::string& create_window_options = base::StringPrintf(
        "{\"state\":\"%s\"}", window_state_string);
    base::FilePath extdir = test_data_dir_.AppendASCII(test_name);
    const extensions::Extension* extension = LoadExtension(extdir);
    EXPECT_TRUE(extension);

    ExtensionTestMessageListener launched_listener("launched", true);
    LaunchPlatformApp(extension);
    EXPECT_TRUE(launched_listener.WaitUntilSatisfied());
    launched_listener.Reply(create_window_options);

    return extension;
  }

  AppWindow* GetFirstAppWindow(const std::string& app_id) {
    AppWindowRegistry::AppWindowList app_windows = AppWindowRegistry::Get(
        browser()->profile())->GetAppWindowsForApp(app_id);

    AppWindowRegistry::const_iterator iter = app_windows.begin();
    if (iter != app_windows.end())
      return *iter;

    return NULL;
  }

  ExtensionNotificationDisplayHelper* GetDisplayHelper() {
    return ExtensionNotificationDisplayHelperFactory::GetForProfile(profile());
  }

  StubNotificationDisplayService* GetDisplayService() {
    return reinterpret_cast<StubNotificationDisplayService*>(
        NotificationDisplayServiceFactory::GetForProfile(profile()));
  }

  NotifierStateTracker* GetNotifierStateTracker() {
    return NotifierStateTrackerFactory::GetForProfile(profile());
  }

 protected:
  void SetUpOnMainThread() override {
    ExtensionApiTest::SetUpOnMainThread();

    DCHECK(profile());
    NotificationDisplayServiceFactory::GetInstance()->SetTestingFactory(
        profile(), &StubNotificationDisplayService::FactoryForTests);
  }

  // Returns the notification that's being displayed for |extension|, or nullptr
  // when the notification count is not equal to one. It's not safe to rely on
  // the Notification pointer after closing the notification, but a copy can be
  // made to continue to be able to access the underlying information.
  Notification* GetNotificationForExtension(
      const extensions::Extension* extension) {
    DCHECK(extension);

    std::set<std::string> notifications =
        GetDisplayHelper()->GetNotificationIdsForExtension(extension->url());
    if (notifications.size() != 1)
      return nullptr;

    return GetDisplayHelper()->GetByNotificationId(*notifications.begin());
  }

  std::string GetNotificationIdFromDelegateId(const std::string& delegate_id) {
    return GetDisplayHelper()->GetByNotificationId(delegate_id)->id();
  }

  void LaunchPlatformApp(const Extension* extension) {
    OpenApplication(AppLaunchParams(
        browser()->profile(), extension, extensions::LAUNCH_CONTAINER_NONE,
        WindowOpenDisposition::NEW_WINDOW, extensions::SOURCE_TEST));
  }

  void EnableFullscreenNotifications() {
    feature_list_.InitWithFeatures({
      features::kPreferHtmlOverPlugins,
      extensions::kAllowFullscreenAppNotificationsFeature}, {});
  }

  void DisableFullscreenNotifications() {
    feature_list_.InitWithFeatures(
        {features::kPreferHtmlOverPlugins},
        {extensions::kAllowFullscreenAppNotificationsFeature});
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

}  // namespace

// http://crbug.com/691913
#if (defined(OS_LINUX) || defined(OS_WIN)) && !defined(NDEBUG)
#define MAYBE_TestBasicUsage DISABLED_TestBasicUsage
#else
#define MAYBE_TestBasicUsage TestBasicUsage
#endif
IN_PROC_BROWSER_TEST_F(NotificationsApiTest, MAYBE_TestBasicUsage) {
  ASSERT_TRUE(RunExtensionTest("notifications/api/basic_usage")) << message_;
}

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestEvents) {
  ASSERT_TRUE(RunExtensionTest("notifications/api/events")) << message_;
}

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestCSP) {
  ASSERT_TRUE(RunExtensionTest("notifications/api/csp")) << message_;
}

// Native notifications don't support (nor use) observers.
#if !defined(OS_MACOSX)
IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestByUser) {
  const extensions::Extension* extension =
      LoadExtensionAndWait("notifications/api/by_user");
  ASSERT_TRUE(extension) << message_;

  {
    ResultCatcher catcher;
    const std::string notification_id =
        GetNotificationIdFromDelegateId(extension->id() + "-FOO");
    GetDisplayService()->RemoveNotification(
        NotificationCommon::EXTENSION, notification_id, false /* by_user */);
    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }

  {
    ResultCatcher catcher;
    const std::string notification_id =
        GetNotificationIdFromDelegateId(extension->id() + "-BAR");
    GetDisplayService()->RemoveNotification(
        NotificationCommon::EXTENSION, notification_id, true /* by_user */);
    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }

  {
    ResultCatcher catcher;
    GetDisplayService()->RemoveAllNotifications(NotificationCommon::EXTENSION,
                                                false /* by_user */);
    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }
  {
    ResultCatcher catcher;
    GetDisplayService()->RemoveAllNotifications(NotificationCommon::EXTENSION,
                                                true /* by_user */);
    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }
}
#endif  // !defined(OS_MACOSX)

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestPartialUpdate) {
  ASSERT_TRUE(RunExtensionTest("notifications/api/partial_update")) << message_;
  const extensions::Extension* extension = GetSingleLoadedExtension();
  ASSERT_TRUE(extension) << message_;

  const char kNewTitle[] = "Changed!";
  const char kNewMessage[] = "Too late! The show ended yesterday";
  int kNewPriority = 2;
  const char kButtonTitle[] = "NewButton";

  Notification* notification = GetNotificationForExtension(extension);
  ASSERT_TRUE(notification);

  LOG(INFO) << "Notification ID: " << notification->id();

  EXPECT_EQ(base::ASCIIToUTF16(kNewTitle), notification->title());
  EXPECT_EQ(base::ASCIIToUTF16(kNewMessage), notification->message());
  EXPECT_EQ(kNewPriority, notification->priority());
  EXPECT_EQ(1u, notification->buttons().size());
  EXPECT_EQ(base::ASCIIToUTF16(kButtonTitle), notification->buttons()[0].title);
}

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestGetPermissionLevel) {
  scoped_refptr<Extension> empty_extension(
      extensions::test_util::CreateEmptyExtension());

  // Get permission level for the extension whose notifications are enabled.
  {
    scoped_refptr<extensions::NotificationsGetPermissionLevelFunction>
        notification_function(
            new extensions::NotificationsGetPermissionLevelFunction());

    notification_function->set_extension(empty_extension.get());
    notification_function->set_has_callback(true);

    std::unique_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_function.get(), "[]", browser(), utils::NONE));

    EXPECT_EQ(base::Value::Type::STRING, result->GetType());
    std::string permission_level;
    EXPECT_TRUE(result->GetAsString(&permission_level));
    EXPECT_EQ("granted", permission_level);
  }

  // Get permission level for the extension whose notifications are disabled.
  {
    scoped_refptr<extensions::NotificationsGetPermissionLevelFunction>
        notification_function(
            new extensions::NotificationsGetPermissionLevelFunction());

    notification_function->set_extension(empty_extension.get());
    notification_function->set_has_callback(true);

    message_center::NotifierId notifier_id(
        message_center::NotifierId::APPLICATION,
        empty_extension->id());
    GetNotifierStateTracker()->SetNotifierEnabled(notifier_id, false);

    std::unique_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_function.get(), "[]", browser(), utils::NONE));

    EXPECT_EQ(base::Value::Type::STRING, result->GetType());
    std::string permission_level;
    EXPECT_TRUE(result->GetAsString(&permission_level));
    EXPECT_EQ("denied", permission_level);
  }
}

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestOnPermissionLevelChanged) {
  const extensions::Extension* extension =
      LoadExtensionAndWait("notifications/api/permission");
  ASSERT_TRUE(extension) << message_;

  // Test permission level changing from granted to denied.
  {
    ResultCatcher catcher;

    message_center::NotifierId notifier_id(
        message_center::NotifierId::APPLICATION,
        extension->id());
    GetNotifierStateTracker()->SetNotifierEnabled(notifier_id, false);

    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }

  // Test permission level changing from denied to granted.
  {
    ResultCatcher catcher;

    message_center::NotifierId notifier_id(
        message_center::NotifierId::APPLICATION,
        extension->id());
    GetNotifierStateTracker()->SetNotifierEnabled(notifier_id, true);

    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }
}

// Native notifications don't support (nor use) observers.
#if !defined(OS_MACOSX)
IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestUserGesture) {
  const extensions::Extension* extension =
      LoadExtensionAndWait("notifications/api/user_gesture");
  ASSERT_TRUE(extension) << message_;

  Notification* notification = GetNotificationForExtension(extension);
  ASSERT_TRUE(notification);

  {
    UserGestureCatcher catcher;
    notification->ButtonClick(0);
    EXPECT_TRUE(catcher.GetNextResult());
    notification->Click();
    EXPECT_TRUE(catcher.GetNextResult());
    notification->Close(true /* by_user */);
    EXPECT_TRUE(catcher.GetNextResult());

    // Note that |notification| no longer points to valid memory.
  }

  ASSERT_FALSE(GetNotificationForExtension(extension));
}
#endif  // !defined(OS_MACOSX)

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestRequireInteraction) {
  const extensions::Extension* extension =
      LoadExtensionAndWait("notifications/api/require_interaction");
  ASSERT_TRUE(extension) << message_;

  Notification* notification = GetNotificationForExtension(extension);
  ASSERT_TRUE(notification);

  EXPECT_TRUE(notification->never_timeout());
}

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestShouldDisplayNormal) {
  EnableFullscreenNotifications();
  ExtensionTestMessageListener notification_created_listener("created", false);
  const Extension* extension = LoadAppWithWindowState(
      "notifications/api/basic_app", WindowState::NORMAL);
  ASSERT_TRUE(extension) << message_;
  ASSERT_TRUE(notification_created_listener.WaitUntilSatisfied());

  // We start by making sure the window is actually focused.
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      GetFirstAppWindow(extension->id())->GetNativeWindow()));

  Notification* notification = GetNotificationForExtension(extension);
  ASSERT_TRUE(notification);

  // If the app hasn't created a fullscreen window, then its notifications
  // shouldn't be displayed when a window is fullscreen.
  ASSERT_FALSE(notification->delegate()->ShouldDisplayOverFullscreen());
}

// Full screen related tests don't run on Mac as native notifications full
// screen decisions are done by the OS directly.
#if !defined(OS_MACOSX)
IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestShouldDisplayFullscreen) {
  EnableFullscreenNotifications();
  ExtensionTestMessageListener notification_created_listener("created", false);
  const Extension* extension = LoadAppWithWindowState(
      "notifications/api/basic_app", WindowState::FULLSCREEN);
  ASSERT_TRUE(extension) << message_;
  ASSERT_TRUE(notification_created_listener.WaitUntilSatisfied());

  // We start by making sure the window is actually focused.
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      GetFirstAppWindow(extension->id())->GetNativeWindow()));

  ASSERT_TRUE(GetFirstAppWindow(extension->id())->IsFullscreen())
      << "Not Fullscreen";
  ASSERT_TRUE(GetFirstAppWindow(extension->id())->GetBaseWindow()->IsActive())
      << "Not Active";

  Notification* notification = GetNotificationForExtension(extension);
  ASSERT_TRUE(notification);

  // If the app has created a fullscreen window, then its notifications should
  // be displayed when a window is fullscreen.
  ASSERT_TRUE(notification->delegate()->ShouldDisplayOverFullscreen());
}

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestShouldDisplayFullscreenOff) {
  DisableFullscreenNotifications();
  ExtensionTestMessageListener notification_created_listener("created", false);
  const Extension* extension = LoadAppWithWindowState(
      "notifications/api/basic_app", WindowState::FULLSCREEN);
  ASSERT_TRUE(extension) << message_;
  ASSERT_TRUE(notification_created_listener.WaitUntilSatisfied());

  // We start by making sure the window is actually focused.
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      GetFirstAppWindow(extension->id())->GetNativeWindow()));

  ASSERT_TRUE(GetFirstAppWindow(extension->id())->IsFullscreen())
      << "Not Fullscreen";
  ASSERT_TRUE(GetFirstAppWindow(extension->id())->GetBaseWindow()->IsActive())
      << "Not Active";

  Notification* notification = GetNotificationForExtension(extension);
  ASSERT_TRUE(notification);

  // When the experiment flag is off, then ShouldDisplayOverFullscreen should
  // return false.
  ASSERT_FALSE(notification->delegate()->ShouldDisplayOverFullscreen());
}

// The Fake OSX fullscreen window doesn't like drawing a second fullscreen
// window when another is visible.
IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestShouldDisplayMultiFullscreen) {
  // Start a fullscreen app, and then start another fullscreen app on top of the
  // first. Notifications from the first should not be displayed because it is
  // not the app actually displaying on the screen.
  EnableFullscreenNotifications();
  ExtensionTestMessageListener notification_created_listener("created", false);
  const Extension* extension1 = LoadAppWithWindowState(
      "notifications/api/basic_app", WindowState::FULLSCREEN);
  ASSERT_TRUE(extension1) << message_;

  ExtensionTestMessageListener window_visible_listener("visible", false);
  const Extension* extension2 = LoadAppWithWindowState(
      "notifications/api/other_app", WindowState::FULLSCREEN);
  ASSERT_TRUE(extension2) << message_;

  ASSERT_TRUE(notification_created_listener.WaitUntilSatisfied());
  ASSERT_TRUE(window_visible_listener.WaitUntilSatisfied());

  // We start by making sure the window is actually focused.
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      GetFirstAppWindow(extension2->id())->GetNativeWindow()));

  Notification* notification = GetNotificationForExtension(extension1);
  ASSERT_TRUE(notification);

  // The first app window is superseded by the second window, so its
  // notification shouldn't be displayed.
  ASSERT_FALSE(notification->delegate()->ShouldDisplayOverFullscreen());
}

// Verify that a notification is actually displayed when the app window that
// creates it is fullscreen with the fullscreen notification flag turned on.
IN_PROC_BROWSER_TEST_F(NotificationsApiTest,
                       TestShouldDisplayPopupNotification) {
  EnableFullscreenNotifications();
  ExtensionTestMessageListener notification_created_listener("created", false);
  const Extension* extension = LoadAppWithWindowState(
      "notifications/api/basic_app", WindowState::FULLSCREEN);
  ASSERT_TRUE(extension) << message_;
  ASSERT_TRUE(notification_created_listener.WaitUntilSatisfied());

  // We start by making sure the window is actually focused.
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      GetFirstAppWindow(extension->id())->GetNativeWindow()));

  ASSERT_TRUE(GetFirstAppWindow(extension->id())->IsFullscreen())
      << "Not Fullscreen";
  ASSERT_TRUE(GetFirstAppWindow(extension->id())->GetBaseWindow()->IsActive())
      << "Not Active";

  Notification* notification = GetNotificationForExtension(extension);
  ASSERT_TRUE(notification);

  // The extension's window is being shown and focused, so its expected that
  // the notification displays on top of it.
  ASSERT_TRUE(notification->delegate()->ShouldDisplayOverFullscreen());
}
#endif  // !defined(OS_MACOSX)
