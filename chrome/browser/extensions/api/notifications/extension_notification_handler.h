// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_NOTIFICATIONS_EXTENSION_NOTIFICATION_HANDLER_H_
#define CHROME_BROWSER_EXTENSIONS_API_NOTIFICATIONS_EXTENSION_NOTIFICATION_HANDLER_H_

#include "base/macros.h"
#include "chrome/browser/notifications/notification_handler.h"
#include "extensions/browser/event_router.h"

class Profile;

namespace extensions {

// Handler for notifications shown by extensions. Will be created and owned by
// the NativeNotificationDisplayService.
class ExtensionNotificationHandler : public NotificationHandler {
 public:
  ExtensionNotificationHandler();
  ~ExtensionNotificationHandler() override;

  // NotificationHandler implementation.
  void OnClose(Profile* profile,
               const std::string& origin,
               const std::string& notification_id,
               bool by_user) override;
  void OnClick(Profile* profile,
               const std::string& origin,
               const std::string& notification_id,
               int action_index,
               const base::NullableString16& reply) override;
  void OpenSettings(Profile* profile) override;
  void RegisterNotification(const std::string& notification_id,
                            NotificationDelegate* delegate) override;

 protected:
  // Overriden in unit tests.
  virtual void SendEvent(Profile* profile,
                         const std::string& extension_id,
                         events::HistogramValue histogram_value,
                         const std::string& name,
                         EventRouter::UserGestureState user_gesture,
                         std::unique_ptr<base::ListValue> args);

  DISALLOW_COPY_AND_ASSIGN(ExtensionNotificationHandler);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_NOTIFICATIONS_EXTENSION_NOTIFICATION_HANDLER_H_
