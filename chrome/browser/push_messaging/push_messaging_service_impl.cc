// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/push_messaging/push_messaging_service_impl.h"

#include <vector>

#include "base/barrier_closure.h"
#include "base/base64url.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/gcm/gcm_profile_service_factory.h"
#include "chrome/browser/gcm/instance_id/instance_id_profile_service.h"
#include "chrome/browser/gcm/instance_id/instance_id_profile_service_factory.h"
#include "chrome/browser/permissions/permission_manager.h"
#include "chrome/browser/permissions/permission_result.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/push_messaging/push_messaging_app_identifier.h"
#include "chrome/browser/push_messaging/push_messaging_constants.h"
#include "chrome/browser/push_messaging/push_messaging_service_factory.h"
#include "chrome/browser/push_messaging/push_messaging_service_observer.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/features.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/gcm_driver/gcm_driver.h"
#include "components/gcm_driver/gcm_profile_service.h"
#include "components/gcm_driver/instance_id/instance_id.h"
#include "components/gcm_driver/instance_id/instance_id_driver.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/rappor/public/rappor_utils.h"
#include "components/rappor/rappor_service_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/service_worker_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/child_process_host.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/push_messaging_status.h"
#include "content/public/common/push_subscription_options.h"
#include "ui/base/l10n/l10n_util.h"

#if BUILDFLAG(ENABLE_BACKGROUND)
#include "chrome/browser/background/background_mode_manager.h"
#include "chrome/browser/lifetime/keep_alive_types.h"
#include "chrome/browser/lifetime/scoped_keep_alive.h"
#endif

using instance_id::InstanceID;

namespace {

// Scope passed to getToken to obtain GCM registration tokens.
// Must match Java GoogleCloudMessaging.INSTANCE_ID_SCOPE.
const char kGCMScope[] = "GCM";

const int kMaxRegistrations = 1000000;

// Chrome does not yet support silent push messages, and requires websites to
// indicate that they will only send user-visible messages.
const char kSilentPushUnsupportedMessage[] =
    "Chrome currently only supports the Push API for subscriptions that will "
    "result in user-visible messages. You can indicate this by calling "
    "pushManager.subscribe({userVisibleOnly: true}) instead. See "
    "https://goo.gl/yqv4Q4 for more details.";

void RecordDeliveryStatus(content::PushDeliveryStatus status) {
  UMA_HISTOGRAM_ENUMERATION("PushMessaging.DeliveryStatus", status,
                            content::PUSH_DELIVERY_STATUS_LAST + 1);
}

void RecordUnsubscribeReason(content::PushUnregistrationReason reason) {
  UMA_HISTOGRAM_ENUMERATION("PushMessaging.UnregistrationReason", reason,
                            content::PUSH_UNREGISTRATION_REASON_LAST + 1);
}

void RecordUnsubscribeGCMResult(gcm::GCMClient::Result result) {
  UMA_HISTOGRAM_ENUMERATION("PushMessaging.UnregistrationGCMResult", result,
                            gcm::GCMClient::LAST_RESULT + 1);
}

void RecordUnsubscribeIIDResult(InstanceID::Result result) {
  UMA_HISTOGRAM_ENUMERATION("PushMessaging.UnregistrationIIDResult", result,
                            InstanceID::LAST_RESULT + 1);
}

blink::WebPushPermissionStatus ToPushPermission(
    ContentSetting content_setting) {
  switch (content_setting) {
    case CONTENT_SETTING_ALLOW:
      return blink::kWebPushPermissionStatusGranted;
    case CONTENT_SETTING_BLOCK:
      return blink::kWebPushPermissionStatusDenied;
    case CONTENT_SETTING_ASK:
      return blink::kWebPushPermissionStatusPrompt;
    default:
      break;
  }
  NOTREACHED();
  return blink::kWebPushPermissionStatusDenied;
}

void UnregisterCallbackToClosure(const base::Closure& closure,
                                 content::PushUnregistrationStatus status) {
  DCHECK(!closure.is_null());
  closure.Run();
}

#if BUILDFLAG(ENABLE_BACKGROUND)
bool UseBackgroundMode() {
  // Note: if push is ever enabled in incognito, the background mode integration
  // should not be enabled for it.
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kDisablePushApiBackgroundMode))
    return false;
  if (command_line->HasSwitch(switches::kEnablePushApiBackgroundMode))
    return true;
  return base::FeatureList::IsEnabled(features::kPushMessagingBackgroundMode);
}
#endif  // BUILDFLAG(ENABLE_BACKGROUND)

}  // namespace

// static
void PushMessagingServiceImpl::InitializeForProfile(Profile* profile) {
  // TODO(johnme): Consider whether push should be enabled in incognito.
  if (!profile || profile->IsOffTheRecord())
    return;

  int count = PushMessagingAppIdentifier::GetCount(profile);
  if (count <= 0)
    return;

  PushMessagingServiceImpl* push_service =
      PushMessagingServiceFactory::GetForProfile(profile);
  push_service->IncreasePushSubscriptionCount(count, false /* is_pending */);
}

PushMessagingServiceImpl::PushMessagingServiceImpl(Profile* profile)
    : profile_(profile),
      push_subscription_count_(0),
      pending_push_subscription_count_(0),
      notification_manager_(profile),
      push_messaging_service_observer_(PushMessagingServiceObserver::Create()),
      weak_factory_(this) {
  DCHECK(profile);
  HostContentSettingsMapFactory::GetForProfile(profile_)->AddObserver(this);

  registrar_.Add(this, chrome::NOTIFICATION_APP_TERMINATING,
                 content::NotificationService::AllSources());
}

PushMessagingServiceImpl::~PushMessagingServiceImpl() = default;

void PushMessagingServiceImpl::IncreasePushSubscriptionCount(int add,
                                                             bool is_pending) {
  DCHECK(add > 0);
  if (push_subscription_count_ + pending_push_subscription_count_ == 0) {
    GetGCMDriver()->AddAppHandler(kPushMessagingAppIdentifierPrefix, this);
  }
  if (is_pending) {
    pending_push_subscription_count_ += add;
  } else {
#if BUILDFLAG(ENABLE_BACKGROUND)
    if (UseBackgroundMode() && g_browser_process->background_mode_manager() &&
        !push_subscription_count_) {
      g_browser_process->background_mode_manager()->RegisterTrigger(
          profile_, this, false /* should_notify_user */);
    }
#endif  // BUILDFLAG(ENABLE_BACKGROUND)
    push_subscription_count_ += add;
  }
}

void PushMessagingServiceImpl::DecreasePushSubscriptionCount(int subtract,
                                                             bool was_pending) {
  DCHECK(subtract > 0);
  if (was_pending) {
    pending_push_subscription_count_ -= subtract;
    DCHECK(pending_push_subscription_count_ >= 0);
  } else {
    push_subscription_count_ -= subtract;
    DCHECK(push_subscription_count_ >= 0);
  }
  if (push_subscription_count_ + pending_push_subscription_count_ == 0) {
    GetGCMDriver()->RemoveAppHandler(kPushMessagingAppIdentifierPrefix);

#if BUILDFLAG(ENABLE_BACKGROUND)
    if (UseBackgroundMode() && g_browser_process->background_mode_manager()) {
      g_browser_process->background_mode_manager()->UnregisterTrigger(profile_,
                                                                      this);
    }
#endif  // BUILDFLAG(ENABLE_BACKGROUND)
  }
}

bool PushMessagingServiceImpl::CanHandle(const std::string& app_id) const {
  return base::StartsWith(app_id, kPushMessagingAppIdentifierPrefix,
                          base::CompareCase::INSENSITIVE_ASCII);
}

void PushMessagingServiceImpl::ShutdownHandler() {
  // Shutdown() should come before and it removes us from the list of app
  // handlers of gcm::GCMDriver so this shouldn't ever been called.
  NOTREACHED();
}

void PushMessagingServiceImpl::OnStoreReset() {
  // Delete all cached subscriptions, since they are now invalid.
  for (const auto& identifier : PushMessagingAppIdentifier::GetAll(profile_)) {
    RecordUnsubscribeReason(
        content::PUSH_UNREGISTRATION_REASON_GCM_STORE_RESET);
    // Clear all the subscriptions in parallel, to reduce risk that shutdown
    // occurs before we finish clearing them.
    ClearPushSubscriptionId(profile_, identifier.origin(),
                            identifier.service_worker_registration_id(),
                            base::Bind(&base::DoNothing));
    // TODO(johnme): Fire pushsubscriptionchange/pushsubscriptionlost SW event.
  }
  PushMessagingAppIdentifier::DeleteAllFromPrefs(profile_);
}

// OnMessage methods -----------------------------------------------------------

void PushMessagingServiceImpl::OnMessage(const std::string& app_id,
                                         const gcm::IncomingMessage& message) {
  // We won't have time to process and act on the message.
  // TODO(peter) This should be checked at the level of the GCMDriver, so that
  // the message is not consumed. See https://crbug.com/612815
  if (g_browser_process->IsShuttingDown() || shutdown_started_)
    return;

  in_flight_message_deliveries_.insert(app_id);

#if BUILDFLAG(ENABLE_BACKGROUND)
  if (g_browser_process->background_mode_manager()) {
    UMA_HISTOGRAM_BOOLEAN("PushMessaging.ReceivedMessageInBackground",
                          g_browser_process->background_mode_manager()
                              ->IsBackgroundWithoutWindows());
  }

  if (!in_flight_keep_alive_) {
    in_flight_keep_alive_.reset(
        new ScopedKeepAlive(KeepAliveOrigin::IN_FLIGHT_PUSH_MESSAGE,
                            KeepAliveRestartOption::DISABLED));
  }
#endif

  base::Closure message_handled_closure =
      message_callback_for_testing_.is_null() ? base::Bind(&base::DoNothing)
                                              : message_callback_for_testing_;
  PushMessagingAppIdentifier app_identifier =
      PushMessagingAppIdentifier::FindByAppId(profile_, app_id);
  // Drop message and unregister if app_id was unknown (maybe recently deleted).
  if (app_identifier.is_null()) {
    DeliverMessageCallback(app_id, GURL::EmptyGURL(),
                           -1 /* kInvalidServiceWorkerRegistrationId */,
                           message, message_handled_closure,
                           content::PUSH_DELIVERY_STATUS_UNKNOWN_APP_ID);
    return;
  }
  // Drop message and unregister if |origin| has lost push permission.
  if (!IsPermissionSet(app_identifier.origin())) {
    DeliverMessageCallback(app_id, app_identifier.origin(),
                           app_identifier.service_worker_registration_id(),
                           message, message_handled_closure,
                           content::PUSH_DELIVERY_STATUS_PERMISSION_DENIED);
    return;
  }

  rappor::SampleDomainAndRegistryFromGURL(
      g_browser_process->rappor_service(),
      "PushMessaging.MessageReceived.Origin", app_identifier.origin());

  // The payload of a push message can be valid with content, valid with empty
  // content, or null. Only set the payload data if it is non-null.
  content::PushEventPayload payload;
  if (message.decrypted)
    payload.setData(message.raw_data);

  // Dispatch the message to the appropriate Service Worker.
  content::BrowserContext::DeliverPushMessage(
      profile_, app_identifier.origin(),
      app_identifier.service_worker_registration_id(), payload,
      base::Bind(&PushMessagingServiceImpl::DeliverMessageCallback,
                 weak_factory_.GetWeakPtr(), app_identifier.app_id(),
                 app_identifier.origin(),
                 app_identifier.service_worker_registration_id(), message,
                 message_handled_closure));

  // Inform tests observing message dispatching about the event.
  if (!message_dispatched_callback_for_testing_.is_null()) {
    message_dispatched_callback_for_testing_.Run(
        app_id, app_identifier.origin(),
        app_identifier.service_worker_registration_id(), payload);
  }
}

void PushMessagingServiceImpl::DeliverMessageCallback(
    const std::string& app_id,
    const GURL& requesting_origin,
    int64_t service_worker_registration_id,
    const gcm::IncomingMessage& message,
    const base::Closure& message_handled_closure,
    content::PushDeliveryStatus status) {
  DCHECK_GE(in_flight_message_deliveries_.count(app_id), 1u);

  RecordDeliveryStatus(status);

  base::Closure completion_closure =
      base::Bind(&PushMessagingServiceImpl::DidHandleMessage,
                 weak_factory_.GetWeakPtr(), app_id, message_handled_closure);
  // The completion_closure should run by default at the end of this function,
  // unless it is explicitly passed to another function.
  base::ScopedClosureRunner completion_closure_runner(completion_closure);

  // A reason to automatically unsubscribe. UNKNOWN means do not unsubscribe.
  content::PushUnregistrationReason unsubscribe_reason =
      content::PUSH_UNREGISTRATION_REASON_UNKNOWN;

  // TODO(mvanouwerkerk): Show a warning in the developer console of the
  // Service Worker corresponding to app_id (and/or on an internals page).
  // See https://crbug.com/508516 for options.
  switch (status) {
    // Call EnforceUserVisibleOnlyRequirements if the message was delivered to
    // the Service Worker JavaScript, even if the website's event handler failed
    // (to prevent sites deliberately failing in order to avoid having to show
    // notifications).
    case content::PUSH_DELIVERY_STATUS_SUCCESS:
    case content::PUSH_DELIVERY_STATUS_EVENT_WAITUNTIL_REJECTED:
    case content::PUSH_DELIVERY_STATUS_TIMEOUT:
      // Only enforce the user visible requirements if this is currently running
      // as the delivery callback for the last in-flight message, and silent
      // push has not been enabled through a command line flag.
      if (in_flight_message_deliveries_.count(app_id) == 1 &&
          !base::CommandLine::ForCurrentProcess()->HasSwitch(
              switches::kAllowSilentPush)) {
        notification_manager_.EnforceUserVisibleOnlyRequirements(
            requesting_origin, service_worker_registration_id,
            completion_closure_runner.Release());
      }
      break;
    case content::PUSH_DELIVERY_STATUS_SERVICE_WORKER_ERROR:
      // Do nothing, and hope the error is transient.
      break;
    case content::PUSH_DELIVERY_STATUS_UNKNOWN_APP_ID:
      unsubscribe_reason =
          content::PUSH_UNREGISTRATION_REASON_DELIVERY_UNKNOWN_APP_ID;
      break;
    case content::PUSH_DELIVERY_STATUS_PERMISSION_DENIED:
      unsubscribe_reason =
          content::PUSH_UNREGISTRATION_REASON_DELIVERY_PERMISSION_DENIED;
      break;
    case content::PUSH_DELIVERY_STATUS_NO_SERVICE_WORKER:
      unsubscribe_reason =
          content::PUSH_UNREGISTRATION_REASON_DELIVERY_NO_SERVICE_WORKER;
      break;
  }

  if (unsubscribe_reason != content::PUSH_UNREGISTRATION_REASON_UNKNOWN) {
    PushMessagingAppIdentifier app_identifier =
        PushMessagingAppIdentifier::FindByAppId(profile_, app_id);
    UnsubscribeInternal(
        unsubscribe_reason,
        app_identifier.is_null() ? GURL::EmptyGURL() : app_identifier.origin(),
        app_identifier.is_null()
            ? -1 /* kInvalidServiceWorkerRegistrationId */
            : app_identifier.service_worker_registration_id(),
        app_id, message.sender_id,
        base::Bind(&UnregisterCallbackToClosure,
                   completion_closure_runner.Release()));
  }
}

void PushMessagingServiceImpl::DidHandleMessage(
    const std::string& app_id,
    const base::Closure& message_handled_closure) {
  auto in_flight_iterator = in_flight_message_deliveries_.find(app_id);
  DCHECK(in_flight_iterator != in_flight_message_deliveries_.end());

  // Remove a single in-flight delivery for |app_id|. This has to be done using
  // an iterator rather than by value, as the latter removes all entries.
  in_flight_message_deliveries_.erase(in_flight_iterator);

#if BUILDFLAG(ENABLE_BACKGROUND)
  // Reset before running callbacks below, so tests can verify keep-alive reset.
  if (in_flight_message_deliveries_.empty())
    in_flight_keep_alive_.reset();
#endif

  message_handled_closure.Run();

  if (push_messaging_service_observer_)
    push_messaging_service_observer_->OnMessageHandled();
}

void PushMessagingServiceImpl::SetMessageCallbackForTesting(
    const base::Closure& callback) {
  message_callback_for_testing_ = callback;
}

// Other gcm::GCMAppHandler methods --------------------------------------------

void PushMessagingServiceImpl::OnMessagesDeleted(const std::string& app_id) {
  // TODO(mvanouwerkerk): Consider firing an event on the Service Worker
  // corresponding to |app_id| to inform the app about deleted messages.
}

void PushMessagingServiceImpl::OnSendError(
    const std::string& app_id,
    const gcm::GCMClient::SendErrorDetails& send_error_details) {
  NOTREACHED() << "The Push API shouldn't have sent messages upstream";
}

void PushMessagingServiceImpl::OnSendAcknowledged(
    const std::string& app_id,
    const std::string& message_id) {
  NOTREACHED() << "The Push API shouldn't have sent messages upstream";
}

// GetEndpoint method ----------------------------------------------------------

GURL PushMessagingServiceImpl::GetEndpoint(bool standard_protocol) const {
  return GURL(standard_protocol ? kPushMessagingPushProtocolEndpoint
                                : kPushMessagingGcmEndpoint);
}

// Subscribe and GetPermissionStatus methods -----------------------------------

void PushMessagingServiceImpl::SubscribeFromDocument(
    const GURL& requesting_origin,
    int64_t service_worker_registration_id,
    int renderer_id,
    int render_frame_id,
    const content::PushSubscriptionOptions& options,
    const RegisterCallback& callback) {
  PushMessagingAppIdentifier app_identifier =
      PushMessagingAppIdentifier::Generate(requesting_origin,
                                           service_worker_registration_id);

  if (push_subscription_count_ + pending_push_subscription_count_ >=
      kMaxRegistrations) {
    SubscribeEndWithError(callback,
                          content::PUSH_REGISTRATION_STATUS_LIMIT_REACHED);
    return;
  }

  content::RenderFrameHost* render_frame_host =
      content::RenderFrameHost::FromID(renderer_id, render_frame_id);
  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  if (!web_contents)
    return;

  if (!options.user_visible_only) {
    web_contents->GetMainFrame()->AddMessageToConsole(
        content::CONSOLE_MESSAGE_LEVEL_ERROR, kSilentPushUnsupportedMessage);

    SubscribeEndWithError(callback,
                          content::PUSH_REGISTRATION_STATUS_PERMISSION_DENIED);
    return;
  }

  // Push does not allow permission requests from iframes.
  PermissionManager::Get(profile_)->RequestPermission(
      CONTENT_SETTINGS_TYPE_PUSH_MESSAGING, web_contents->GetMainFrame(),
      requesting_origin, true /* user_gesture */,
      base::Bind(&PushMessagingServiceImpl::DoSubscribe,
                 weak_factory_.GetWeakPtr(), app_identifier, options,
                 callback));
}

void PushMessagingServiceImpl::SubscribeFromWorker(
    const GURL& requesting_origin,
    int64_t service_worker_registration_id,
    const content::PushSubscriptionOptions& options,
    const RegisterCallback& register_callback) {
  PushMessagingAppIdentifier app_identifier =
      PushMessagingAppIdentifier::Generate(requesting_origin,
                                           service_worker_registration_id);

  if (push_subscription_count_ + pending_push_subscription_count_ >=
      kMaxRegistrations) {
    SubscribeEndWithError(register_callback,
                          content::PUSH_REGISTRATION_STATUS_LIMIT_REACHED);
    return;
  }

  blink::WebPushPermissionStatus permission_status =
      GetPermissionStatus(requesting_origin, options.user_visible_only);

  if (permission_status != blink::kWebPushPermissionStatusGranted) {
    SubscribeEndWithError(register_callback,
                          content::PUSH_REGISTRATION_STATUS_PERMISSION_DENIED);
    return;
  }

  DoSubscribe(app_identifier, options, register_callback,
              CONTENT_SETTING_ALLOW);
}

blink::WebPushPermissionStatus PushMessagingServiceImpl::GetPermissionStatus(
    const GURL& origin,
    bool user_visible) {
  if (!user_visible)
    return blink::kWebPushPermissionStatusDenied;

  // Because the Push API is tied to Service Workers, many usages of the API
  // won't have an embedding origin at all. Only consider the requesting
  // |origin| when checking whether permission to use the API has been granted.
  return ToPushPermission(
      PermissionManager::Get(profile_)
          ->GetPermissionStatus(CONTENT_SETTINGS_TYPE_PUSH_MESSAGING, origin,
                                origin)
          .content_setting);
}

bool PushMessagingServiceImpl::SupportNonVisibleMessages() {
  return false;
}

void PushMessagingServiceImpl::DoSubscribe(
    const PushMessagingAppIdentifier& app_identifier,
    const content::PushSubscriptionOptions& options,
    const RegisterCallback& register_callback,
    ContentSetting content_setting) {
  if (content_setting != CONTENT_SETTING_ALLOW) {
    SubscribeEndWithError(register_callback,
                          content::PUSH_REGISTRATION_STATUS_PERMISSION_DENIED);
    return;
  }

  IncreasePushSubscriptionCount(1, true /* is_pending */);

  GetInstanceIDDriver()
      ->GetInstanceID(app_identifier.app_id())
      ->GetToken(NormalizeSenderInfo(options.sender_info), kGCMScope,
                 std::map<std::string, std::string>() /* options */,
                 base::Bind(&PushMessagingServiceImpl::DidSubscribe,
                            weak_factory_.GetWeakPtr(), app_identifier,
                            options.sender_info, register_callback));
}

void PushMessagingServiceImpl::SubscribeEnd(
    const RegisterCallback& callback,
    const std::string& subscription_id,
    const std::vector<uint8_t>& p256dh,
    const std::vector<uint8_t>& auth,
    content::PushRegistrationStatus status) {
  callback.Run(subscription_id, p256dh, auth, status);
}

void PushMessagingServiceImpl::SubscribeEndWithError(
    const RegisterCallback& callback,
    content::PushRegistrationStatus status) {
  SubscribeEnd(callback, std::string() /* subscription_id */,
               std::vector<uint8_t>() /* p256dh */,
               std::vector<uint8_t>() /* auth */, status);
}

void PushMessagingServiceImpl::DidSubscribe(
    const PushMessagingAppIdentifier& app_identifier,
    const std::string& sender_id,
    const RegisterCallback& callback,
    const std::string& subscription_id,
    InstanceID::Result result) {
  DecreasePushSubscriptionCount(1, true /* was_pending */);

  content::PushRegistrationStatus status =
      content::PUSH_REGISTRATION_STATUS_SERVICE_ERROR;

  switch (result) {
    case InstanceID::SUCCESS:
      // Make sure that this subscription has associated encryption keys prior
      // to returning it to the developer - they'll need this information in
      // order to send payloads to the user.
      GetEncryptionInfoForAppId(
          app_identifier.app_id(), sender_id,
          base::Bind(&PushMessagingServiceImpl::DidSubscribeWithEncryptionInfo,
                     weak_factory_.GetWeakPtr(), app_identifier, callback,
                     subscription_id));
      return;
    case InstanceID::INVALID_PARAMETER:
    case InstanceID::DISABLED:
    case InstanceID::ASYNC_OPERATION_PENDING:
    case InstanceID::SERVER_ERROR:
    case InstanceID::UNKNOWN_ERROR:
      DLOG(ERROR) << "Push messaging subscription failed; InstanceID::Result = "
                  << result;
      status = content::PUSH_REGISTRATION_STATUS_SERVICE_ERROR;
      break;
    case InstanceID::NETWORK_ERROR:
      status = content::PUSH_REGISTRATION_STATUS_NETWORK_ERROR;
      break;
  }

  SubscribeEndWithError(callback, status);
}

void PushMessagingServiceImpl::DidSubscribeWithEncryptionInfo(
    const PushMessagingAppIdentifier& app_identifier,
    const RegisterCallback& callback,
    const std::string& subscription_id,
    const std::string& p256dh,
    const std::string& auth_secret) {
  if (p256dh.empty()) {
    SubscribeEndWithError(
        callback, content::PUSH_REGISTRATION_STATUS_PUBLIC_KEY_UNAVAILABLE);
    return;
  }

  app_identifier.PersistToPrefs(profile_);

  IncreasePushSubscriptionCount(1, false /* is_pending */);

  SubscribeEnd(callback, subscription_id,
               std::vector<uint8_t>(p256dh.begin(), p256dh.end()),
               std::vector<uint8_t>(auth_secret.begin(), auth_secret.end()),
               content::PUSH_REGISTRATION_STATUS_SUCCESS_FROM_PUSH_SERVICE);
}

// GetSubscriptionInfo methods -------------------------------------------------

void PushMessagingServiceImpl::GetSubscriptionInfo(
    const GURL& origin,
    int64_t service_worker_registration_id,
    const std::string& sender_id,
    const std::string& subscription_id,
    const SubscriptionInfoCallback& callback) {
  PushMessagingAppIdentifier app_identifier =
      PushMessagingAppIdentifier::FindByServiceWorker(
          profile_, origin, service_worker_registration_id);

  if (app_identifier.is_null()) {
    callback.Run(false /* is_valid */, std::vector<uint8_t>() /* p256dh */,
                 std::vector<uint8_t>() /* auth */);
    return;
  }

  const std::string& app_id = app_identifier.app_id();
  base::Callback<void(bool)> validate_cb =
      base::Bind(&PushMessagingServiceImpl::DidValidateSubscription,
                 weak_factory_.GetWeakPtr(), app_id, sender_id, callback);

  if (PushMessagingAppIdentifier::UseInstanceID(app_id)) {
    GetInstanceIDDriver()->GetInstanceID(app_id)->ValidateToken(
        NormalizeSenderInfo(sender_id), kGCMScope, subscription_id,
        validate_cb);
  } else {
    GetGCMDriver()->ValidateRegistration(
        app_id, {NormalizeSenderInfo(sender_id)}, subscription_id, validate_cb);
  }
}

void PushMessagingServiceImpl::DidValidateSubscription(
    const std::string& app_id,
    const std::string& sender_id,
    const SubscriptionInfoCallback& callback,
    bool is_valid) {
  if (!is_valid) {
    callback.Run(false /* is_valid */, std::vector<uint8_t>() /* p256dh */,
                 std::vector<uint8_t>() /* auth */);
    return;
  }

  GetEncryptionInfoForAppId(
      app_id, sender_id,
      base::Bind(&PushMessagingServiceImpl::DidGetEncryptionInfo,
                 weak_factory_.GetWeakPtr(), callback));
}

void PushMessagingServiceImpl::DidGetEncryptionInfo(
    const SubscriptionInfoCallback& callback,
    const std::string& p256dh,
    const std::string& auth_secret) const {
  // I/O errors might prevent the GCM Driver from retrieving a key-pair.
  bool is_valid = !p256dh.empty();
  callback.Run(is_valid, std::vector<uint8_t>(p256dh.begin(), p256dh.end()),
               std::vector<uint8_t>(auth_secret.begin(), auth_secret.end()));
}

// Unsubscribe methods ---------------------------------------------------------

void PushMessagingServiceImpl::Unsubscribe(
    content::PushUnregistrationReason reason,
    const GURL& requesting_origin,
    int64_t service_worker_registration_id,
    const std::string& sender_id,
    const UnregisterCallback& callback) {
  PushMessagingAppIdentifier app_identifier =
      PushMessagingAppIdentifier::FindByServiceWorker(
          profile_, requesting_origin, service_worker_registration_id);

  UnsubscribeInternal(
      reason, requesting_origin, service_worker_registration_id,
      app_identifier.is_null() ? std::string() : app_identifier.app_id(),
      sender_id, callback);
}

void PushMessagingServiceImpl::UnsubscribeInternal(
    content::PushUnregistrationReason reason,
    const GURL& origin,
    int64_t service_worker_registration_id,
    const std::string& app_id,
    const std::string& sender_id,
    const UnregisterCallback& callback) {
  DCHECK(!app_id.empty() || (!origin.is_empty() &&
                             service_worker_registration_id !=
                                 -1 /* kInvalidServiceWorkerRegistrationId */))
      << "Need an app_id and/or origin+service_worker_registration_id";

  RecordUnsubscribeReason(reason);

  if (origin.is_empty() ||
      service_worker_registration_id ==
          -1 /* kInvalidServiceWorkerRegistrationId */) {
    // Can't clear Service Worker database.
    DidClearPushSubscriptionId(reason, app_id, sender_id, callback);
    return;
  }
  ClearPushSubscriptionId(
      profile_, origin, service_worker_registration_id,
      base::Bind(&PushMessagingServiceImpl::DidClearPushSubscriptionId,
                 weak_factory_.GetWeakPtr(), reason, app_id, sender_id,
                 callback));
}

void PushMessagingServiceImpl::DidClearPushSubscriptionId(
    content::PushUnregistrationReason reason,
    const std::string& app_id,
    const std::string& sender_id,
    const UnregisterCallback& callback) {
  if (app_id.empty()) {
    // Without an |app_id|, we can neither delete the subscription from the
    // PushMessagingAppIdentifier map, nor unsubscribe with the GCM Driver.
    callback.Run(
        content::PUSH_UNREGISTRATION_STATUS_SUCCESS_WAS_NOT_REGISTERED);
    return;
  }

  // Delete the mapping for this app_id, to guarantee that no messages get
  // delivered in future (even if unregistration fails).
  // TODO(johnme): Instead of deleting these app ids, store them elsewhere, and
  // retry unregistration if it fails due to network errors (crbug.com/465399).
  PushMessagingAppIdentifier app_identifier =
      PushMessagingAppIdentifier::FindByAppId(profile_, app_id);
  bool was_subscribed = !app_identifier.is_null();
  if (was_subscribed)
    app_identifier.DeleteFromPrefs(profile_);

  // Run the unsubscribe callback *before* asking the InstanceIDDriver/GCMDriver
  // to unsubscribe, since that's a slow process involving network retries, and
  // by this point enough local state has been deleted that the subscription is
  // inactive. Note that DeliverMessageCallback automatically unsubscribes if
  // messages are later received for a subscription that was locally deleted,
  // so as long as messages keep getting sent to it, the unsubscription should
  // eventually reach GCM servers even if this particular attempt fails.
  callback.Run(
      was_subscribed
          ? content::PUSH_UNREGISTRATION_STATUS_SUCCESS_UNREGISTERED
          : content::PUSH_UNREGISTRATION_STATUS_SUCCESS_WAS_NOT_REGISTERED);

  if (PushMessagingAppIdentifier::UseInstanceID(app_id)) {
    GetInstanceIDDriver()->GetInstanceID(app_id)->DeleteID(
        base::Bind(&PushMessagingServiceImpl::DidDeleteID,
                   weak_factory_.GetWeakPtr(), app_id, was_subscribed));

  } else {
    auto unregister_callback =
        base::Bind(&PushMessagingServiceImpl::DidUnregister,
                   weak_factory_.GetWeakPtr(), was_subscribed);
#if defined(OS_ANDROID)
    // On Android the backend is different, and requires the original sender_id.
    // UnsubscribeBecausePermissionRevoked and
    // DidDeleteServiceWorkerRegistration sometimes call us with an empty one.
    if (sender_id.empty()) {
      unregister_callback.Run(gcm::GCMClient::INVALID_PARAMETER);
    } else {
      GetGCMDriver()->UnregisterWithSenderId(
          app_id, NormalizeSenderInfo(sender_id), unregister_callback);
    }
#else
    GetGCMDriver()->Unregister(app_id, unregister_callback);
#endif
  }
}

void PushMessagingServiceImpl::DidUnregister(bool was_subscribed,
                                             gcm::GCMClient::Result result) {
  RecordUnsubscribeGCMResult(result);
  DidUnsubscribe(std::string() /* app_id_when_instance_id */, was_subscribed);
}

void PushMessagingServiceImpl::DidDeleteID(const std::string& app_id,
                                           bool was_subscribed,
                                           InstanceID::Result result) {
  RecordUnsubscribeIIDResult(result);
  // DidUnsubscribe must be run asynchronously when passing a non-empty
  // |app_id_when_instance_id|, since it calls
  // InstanceIDDriver::RemoveInstanceID which deletes the InstanceID itself.
  // Calling that immediately would cause a use-after-free in our caller.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&PushMessagingServiceImpl::DidUnsubscribe,
                     weak_factory_.GetWeakPtr(), app_id, was_subscribed));
}

void PushMessagingServiceImpl::DidUnsubscribe(
    const std::string& app_id_when_instance_id,
    bool was_subscribed) {
  if (!app_id_when_instance_id.empty())
    GetInstanceIDDriver()->RemoveInstanceID(app_id_when_instance_id);

  if (was_subscribed)
    DecreasePushSubscriptionCount(1, false /* was_pending */);

  if (!unsubscribe_callback_for_testing_.is_null())
    unsubscribe_callback_for_testing_.Run();
}

void PushMessagingServiceImpl::SetUnsubscribeCallbackForTesting(
    const base::Closure& callback) {
  unsubscribe_callback_for_testing_ = callback;
}

// DidDeleteServiceWorkerRegistration methods ----------------------------------

void PushMessagingServiceImpl::DidDeleteServiceWorkerRegistration(
    const GURL& origin,
    int64_t service_worker_registration_id) {
  const PushMessagingAppIdentifier& app_identifier =
      PushMessagingAppIdentifier::FindByServiceWorker(
          profile_, origin, service_worker_registration_id);
  if (app_identifier.is_null()) {
    if (!service_worker_unregistered_callback_for_testing_.is_null())
      service_worker_unregistered_callback_for_testing_.Run();
    return;
  }
  // Note this will not fully unsubscribe pre-InstanceID subscriptions on
  // Android from GCM, as that requires a sender_id. (Ideally we'd fetch it
  // from the SWDB in some "before_unregistered" SWObserver event.)
  UnsubscribeInternal(
      content::PUSH_UNREGISTRATION_REASON_SERVICE_WORKER_UNREGISTERED, origin,
      service_worker_registration_id, app_identifier.app_id(),
      std::string() /* sender_id */,
      base::Bind(&UnregisterCallbackToClosure,
                 service_worker_unregistered_callback_for_testing_.is_null()
                     ? base::Bind(&base::DoNothing)
                     : service_worker_unregistered_callback_for_testing_));
}

void PushMessagingServiceImpl::SetServiceWorkerUnregisteredCallbackForTesting(
    const base::Closure& callback) {
  service_worker_unregistered_callback_for_testing_ = callback;
}

// OnContentSettingChanged methods ---------------------------------------------

void PushMessagingServiceImpl::OnContentSettingChanged(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsType content_type,
    std::string resource_identifier) {
  if (content_type != CONTENT_SETTINGS_TYPE_NOTIFICATIONS)
    return;

  std::vector<PushMessagingAppIdentifier> all_app_identifiers =
      PushMessagingAppIdentifier::GetAll(profile_);

  base::Closure barrier_closure = base::BarrierClosure(
      all_app_identifiers.size(),
      content_setting_changed_callback_for_testing_.is_null()
          ? base::Bind(&base::DoNothing)
          : content_setting_changed_callback_for_testing_);

  for (const PushMessagingAppIdentifier& app_identifier : all_app_identifiers) {
    // If |primary_pattern| is not valid, we should always check for a
    // permission change because it can happen for example when the entire
    // Push or Notifications permissions are cleared.
    // Otherwise, the permission should be checked if the pattern matches the
    // origin.
    if (primary_pattern.IsValid() &&
        !primary_pattern.Matches(app_identifier.origin())) {
      barrier_closure.Run();
      continue;
    }

    if (IsPermissionSet(app_identifier.origin())) {
      barrier_closure.Run();
      continue;
    }

    bool need_sender_id = false;
#if defined(OS_ANDROID)
    need_sender_id =
        !PushMessagingAppIdentifier::UseInstanceID(app_identifier.app_id());
#endif
    if (need_sender_id) {
      GetSenderId(
          profile_, app_identifier.origin(),
          app_identifier.service_worker_registration_id(),
          base::Bind(
              &PushMessagingServiceImpl::UnsubscribeBecausePermissionRevoked,
              weak_factory_.GetWeakPtr(), app_identifier,
              base::Bind(&UnregisterCallbackToClosure, barrier_closure)));
    } else {
      UnsubscribeInternal(
          content::PUSH_UNREGISTRATION_REASON_PERMISSION_REVOKED,
          app_identifier.origin(),
          app_identifier.service_worker_registration_id(),
          app_identifier.app_id(), std::string() /* sender_id */,
          base::Bind(&UnregisterCallbackToClosure, barrier_closure));
    }
  }
}

void PushMessagingServiceImpl::UnsubscribeBecausePermissionRevoked(
    const PushMessagingAppIdentifier& app_identifier,
    const UnregisterCallback& callback,
    const std::string& sender_id,
    bool success,
    bool not_found) {
  // Unsubscribe the PushMessagingAppIdentifier with the push service.
  // It's possible for GetSenderId to have failed and sender_id to be empty, if
  // cookies (and the SW database) for an origin got cleared before permissions
  // are cleared for the origin. In that case for legacy GCM registrations on
  // Android, Unsubscribe will just delete the app identifier to block future
  // messages.
  // TODO(johnme): Auto-unregister before SW DB is cleared (crbug.com/402458).
  UnsubscribeInternal(content::PUSH_UNREGISTRATION_REASON_PERMISSION_REVOKED,
                      app_identifier.origin(),
                      app_identifier.service_worker_registration_id(),
                      app_identifier.app_id(), sender_id, callback);
}

void PushMessagingServiceImpl::SetContentSettingChangedCallbackForTesting(
    const base::Closure& callback) {
  content_setting_changed_callback_for_testing_ = callback;
}

// KeyedService methods -------------------------------------------------------

void PushMessagingServiceImpl::Shutdown() {
  GetGCMDriver()->RemoveAppHandler(kPushMessagingAppIdentifierPrefix);
  HostContentSettingsMapFactory::GetForProfile(profile_)->RemoveObserver(this);
}

// BackgroundTrigger methods ---------------------------------------------------
base::string16 PushMessagingServiceImpl::GetName() {
  return l10n_util::GetStringUTF16(IDS_NOTIFICATIONS_BACKGROUND_SERVICE_NAME);
}

gfx::ImageSkia* PushMessagingServiceImpl::GetIcon() {
  return nullptr;
}

void PushMessagingServiceImpl::OnMenuClick() {
#if BUILDFLAG(ENABLE_BACKGROUND)
  chrome::ShowContentSettings(
      BackgroundModeManager::GetBrowserWindowForProfile(profile_),
      CONTENT_SETTINGS_TYPE_NOTIFICATIONS);
#endif  // BUILDFLAG(ENABLE_BACKGROUND)
}

// content::NotificationObserver methods ---------------------------------------

void PushMessagingServiceImpl::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(chrome::NOTIFICATION_APP_TERMINATING, type);
  shutdown_started_ = true;
#if BUILDFLAG(ENABLE_BACKGROUND)
  in_flight_keep_alive_.reset();
#endif  // BUILDFLAG(ENABLE_BACKGROUND)
}

// Helper methods --------------------------------------------------------------

std::string PushMessagingServiceImpl::NormalizeSenderInfo(
    const std::string& sender_info) const {
  // Only encode the |sender_info| when it is a NIST P-256 public key in
  // uncompressed format, verified through its length and the 0x04 prefix byte.
  if (sender_info.size() != 65 || sender_info[0] != 0x04)
    return sender_info;

  std::string encoded_sender_info;
  base::Base64UrlEncode(sender_info, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &encoded_sender_info);

  return encoded_sender_info;
}

// Assumes user_visible always since this is just meant to check
// if the permission was previously granted and not revoked.
bool PushMessagingServiceImpl::IsPermissionSet(const GURL& origin) {
  return GetPermissionStatus(origin, true /* user_visible */) ==
         blink::kWebPushPermissionStatusGranted;
}

void PushMessagingServiceImpl::GetEncryptionInfoForAppId(
    const std::string& app_id,
    const std::string& sender_id,
    gcm::GCMEncryptionProvider::EncryptionInfoCallback callback) {
  if (PushMessagingAppIdentifier::UseInstanceID(app_id)) {
    GetInstanceIDDriver()->GetInstanceID(app_id)->GetEncryptionInfo(
        NormalizeSenderInfo(sender_id), callback);
  } else {
    GetGCMDriver()->GetEncryptionInfo(app_id, callback);
  }
}

gcm::GCMDriver* PushMessagingServiceImpl::GetGCMDriver() const {
  gcm::GCMProfileService* gcm_profile_service =
      gcm::GCMProfileServiceFactory::GetForProfile(profile_);
  CHECK(gcm_profile_service);
  CHECK(gcm_profile_service->driver());
  return gcm_profile_service->driver();
}

instance_id::InstanceIDDriver* PushMessagingServiceImpl::GetInstanceIDDriver()
    const {
  instance_id::InstanceIDProfileService* instance_id_profile_service =
      instance_id::InstanceIDProfileServiceFactory::GetForProfile(profile_);
  CHECK(instance_id_profile_service);
  CHECK(instance_id_profile_service->driver());
  return instance_id_profile_service->driver();
}
