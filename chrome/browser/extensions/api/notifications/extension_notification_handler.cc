// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/notifications/extension_notification_handler.h"

#include "base/logging.h"
#include "base/strings/nullable_string16.h"
#include "base/strings/string_piece.h"
#include "chrome/browser/extensions/api/notifications/extension_notification_display_helper.h"
#include "chrome/browser/extensions/api/notifications/extension_notification_display_helper_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/notifications.h"
#include "extensions/common/constants.h"
#include "url/gurl.h"

namespace extensions {

namespace {

std::string GetExtensionId(const std::string& extension_url) {
  GURL url(extension_url);
  if (!url.is_valid() || !url.SchemeIs(extensions::kExtensionScheme))
    return "";
  return url.GetOrigin().host_piece().as_string();
}

std::unique_ptr<base::ListValue> CreateBaseEventArgs(
    const std::string& extension_id,
    const std::string& scoped_notification_id) {
  // Unscope the notification id before returning it.
  size_t index_of_separator = extension_id.length() + 1;
  DCHECK_LT(index_of_separator, scoped_notification_id.length());
  std::string unscoped_notification_id =
      scoped_notification_id.substr(index_of_separator);

  std::unique_ptr<base::ListValue> args(new base::ListValue());
  args->AppendString(unscoped_notification_id);
  return args;
}

}  // namespace

ExtensionNotificationHandler::ExtensionNotificationHandler() = default;

ExtensionNotificationHandler::~ExtensionNotificationHandler() = default;

void ExtensionNotificationHandler::OnClose(Profile* profile,
                                           const std::string& origin,
                                           const std::string& notification_id,
                                           bool by_user) {
  EventRouter::UserGestureState gesture =
      by_user ? EventRouter::USER_GESTURE_ENABLED
              : EventRouter::USER_GESTURE_NOT_ENABLED;
  std::string extension_id(GetExtensionId(origin));
  DCHECK(!extension_id.empty());

  std::unique_ptr<base::ListValue> args(
      CreateBaseEventArgs(extension_id, notification_id));
  args->AppendBoolean(by_user);
  SendEvent(profile, extension_id, events::NOTIFICATIONS_ON_CLOSED,
            api::notifications::OnClosed::kEventName, gesture, std::move(args));

  ExtensionNotificationDisplayHelper* display_helper =
      ExtensionNotificationDisplayHelperFactory::GetForProfile(profile);
  if (display_helper)
    display_helper->EraseDataForNotificationId(notification_id);
}

void ExtensionNotificationHandler::OnClick(
    Profile* profile,
    const std::string& origin,
    const std::string& notification_id,
    int action_index,
    const base::NullableString16& reply) {
  DCHECK(reply.is_null());

  std::string extension_id(GetExtensionId(origin));
  std::unique_ptr<base::ListValue> args(
      CreateBaseEventArgs(extension_id, notification_id));
  if (action_index > -1)
    args->AppendInteger(action_index);
  events::HistogramValue histogram_value =
      action_index > -1 ? events::NOTIFICATIONS_ON_BUTTON_CLICKED
                        : events::NOTIFICATIONS_ON_CLICKED;
  const std::string& event_name =
      action_index > -1 ? api::notifications::OnButtonClicked::kEventName
                        : api::notifications::OnClicked::kEventName;

  SendEvent(profile, extension_id, histogram_value, event_name,
            EventRouter::USER_GESTURE_ENABLED, std::move(args));
}

void ExtensionNotificationHandler::OpenSettings(Profile* profile) {
  // Extension notifications don't display a settings button.
  NOTREACHED();
}

void ExtensionNotificationHandler::RegisterNotification(
    const std::string& notification_id,
    NotificationDelegate* delegate) {}

void ExtensionNotificationHandler::SendEvent(
    Profile* profile,
    const std::string& extension_id,
    events::HistogramValue histogram_value,
    const std::string& event_name,
    EventRouter::UserGestureState user_gesture,
    std::unique_ptr<base::ListValue> args) {
  if (extension_id.empty())
    return;

  EventRouter* event_router = EventRouter::Get(profile);
  if (!event_router)
    return;

  std::unique_ptr<Event> event(
      new Event(histogram_value, event_name, std::move(args)));
  event->user_gesture = user_gesture;
  event_router->DispatchEventToExtension(extension_id, std::move(event));
}

}  // namespace extensions
