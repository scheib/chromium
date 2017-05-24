// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <string>

#include "base/barrier_closure.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/histogram_tester.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_delegate.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/engagement/site_engagement_score.h"
#include "chrome/browser/engagement/site_engagement_service.h"
#include "chrome/browser/gcm/fake_gcm_profile_service.h"
#include "chrome/browser/gcm/gcm_profile_service_factory.h"
#include "chrome/browser/gcm/instance_id/instance_id_profile_service.h"
#include "chrome/browser/gcm/instance_id/instance_id_profile_service_factory.h"
#include "chrome/browser/lifetime/keep_alive_registry.h"
#include "chrome/browser/lifetime/keep_alive_types.h"
#include "chrome/browser/notifications/message_center_display_service.h"
#include "chrome/browser/notifications/notification_test_util.h"
#include "chrome/browser/notifications/platform_notification_service_impl.h"
#include "chrome/browser/permissions/permission_request_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/push_messaging/push_messaging_app_identifier.h"
#include "chrome/browser/push_messaging/push_messaging_constants.h"
#include "chrome/browser/push_messaging/push_messaging_service_factory.h"
#include "chrome/browser/push_messaging/push_messaging_service_impl.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/features.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/gcm_driver/common/gcm_messages.h"
#include "components/gcm_driver/gcm_client.h"
#include "components/gcm_driver/instance_id/fake_gcm_driver_for_instance_id.h"
#include "components/gcm_driver/instance_id/instance_id_driver.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/push_subscription_options.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/browsing_data_remover_test_util.h"
#include "content/public/test/test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/base/window_open_disposition.h"

#if BUILDFLAG(ENABLE_BACKGROUND)
#include "chrome/browser/background/background_mode_manager.h"
#endif

namespace {

const char kManifestSenderId[] = "1234567890";

// NIST P-256 public key made available to tests. Must be an uncompressed
// point in accordance with SEC1 2.3.3.
const uint8_t kApplicationServerKey[65] = {
    0x04, 0x55, 0x52, 0x6A, 0xA5, 0x6E, 0x8E, 0xAA, 0x47, 0x97, 0x36,
    0x10, 0xC1, 0x66, 0x3C, 0x1E, 0x65, 0xBF, 0xA1, 0x7B, 0xEE, 0x48,
    0xC9, 0xC6, 0xBB, 0xBF, 0x02, 0x18, 0x53, 0x72, 0x1D, 0x0C, 0x7B,
    0xA9, 0xE3, 0x11, 0xB7, 0x03, 0x52, 0x21, 0xD3, 0x71, 0x90, 0x13,
    0xA8, 0xC1, 0xCF, 0xED, 0x20, 0xF7, 0x1F, 0xD1, 0x7F, 0xF2, 0x76,
    0xB6, 0x01, 0x20, 0xD8, 0x35, 0xA5, 0xD9, 0x3C, 0x43, 0xFD};

// URL-safe base64 encoded version of the |kApplicationServerKey|.
const char kEncodedApplicationServerKey[] =
    "BFVSaqVujqpHlzYQwWY8HmW_oXvuSMnGu78CGFNyHQx7qeMRtwNSIdNxkBOowc_tIPcf0X_ydr"
    "YBINg1pdk8Q_0";

std::string GetTestApplicationServerKey() {
  return std::string(kApplicationServerKey,
                     kApplicationServerKey + arraysize(kApplicationServerKey));
}

void LegacyRegisterCallback(const base::Closure& done_callback,
                            std::string* out_registration_id,
                            gcm::GCMClient::Result* out_result,
                            const std::string& registration_id,
                            gcm::GCMClient::Result result) {
  if (out_registration_id)
    *out_registration_id = registration_id;
  if (out_result)
    *out_result = result;
  done_callback.Run();
}

void DidRegister(base::Closure done_callback,
                 const std::string& registration_id,
                 const std::vector<uint8_t>& p256dh,
                 const std::vector<uint8_t>& auth,
                 content::PushRegistrationStatus status) {
  EXPECT_EQ(content::PUSH_REGISTRATION_STATUS_SUCCESS_FROM_PUSH_SERVICE,
            status);
  done_callback.Run();
}

void InstanceIDResultCallback(base::Closure done_callback,
                              instance_id::InstanceID::Result* out_result,
                              instance_id::InstanceID::Result result) {
  DCHECK(out_result);
  *out_result = result;
  done_callback.Run();
}

}  // namespace

class PushMessagingBrowserTest : public InProcessBrowserTest {
 public:
  PushMessagingBrowserTest() : gcm_service_(nullptr), gcm_driver_(nullptr) {}
  ~PushMessagingBrowserTest() override {}

  // InProcessBrowserTest:
  void SetUp() override {
    https_server_.reset(
        new net::EmbeddedTestServer(net::EmbeddedTestServer::TYPE_HTTPS));
    https_server_->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(https_server_->Start());

    notification_manager_.reset(new StubNotificationUIManager);

    SiteEngagementScore::SetParamValuesForTesting();
    InProcessBrowserTest::SetUp();
  }
  void SetUpCommandLine(base::CommandLine* command_line) override {
    // Enable experimental features for subscription restrictions.
    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);
    InProcessBrowserTest::SetUpCommandLine(command_line);
  }

  // InProcessBrowserTest:
  void SetUpOnMainThread() override {
    gcm_service_ = static_cast<gcm::FakeGCMProfileService*>(
        gcm::GCMProfileServiceFactory::GetInstance()->SetTestingFactoryAndUse(
            GetBrowser()->profile(), &gcm::FakeGCMProfileService::Build));
    gcm_driver_ = static_cast<instance_id::FakeGCMDriverForInstanceID*>(
        gcm_service_->driver());

    push_service_ =
        PushMessagingServiceFactory::GetForProfile(GetBrowser()->profile());
    display_service_.reset(new MessageCenterDisplayService(
        GetBrowser()->profile(), notification_manager_.get()));
    notification_service()->SetNotificationDisplayServiceForTesting(
        display_service_.get());

    LoadTestPage();
    InProcessBrowserTest::SetUpOnMainThread();
  }

  // Calls should be wrapped in the ASSERT_NO_FATAL_FAILURE() macro.
  void RestartPushService() {
    Profile* profile = GetBrowser()->profile();
    PushMessagingServiceFactory::GetInstance()->SetTestingFactory(profile,
                                                                  nullptr);
    ASSERT_EQ(nullptr, PushMessagingServiceFactory::GetForProfile(profile));
    PushMessagingServiceFactory::GetInstance()->RestoreFactoryForTests(profile);
    PushMessagingServiceImpl::InitializeForProfile(profile);
    push_service_ = PushMessagingServiceFactory::GetForProfile(profile);
  }

  // Helper function to test if a Keep Alive is registered while avoiding the
  // platform checks. Returns a boolean so that assertion failures are reported
  // at the right line.
  // Returns true when KeepAlives are not supported by the platform, or when
  // the registration state is equal to the expectation.
  bool IsRegisteredKeepAliveEqualTo(bool expectation) {
#if BUILDFLAG(ENABLE_BACKGROUND)
    return expectation ==
           KeepAliveRegistry::GetInstance()->IsOriginRegistered(
               KeepAliveOrigin::IN_FLIGHT_PUSH_MESSAGE);
#else
    return true;
#endif
  }

  // InProcessBrowserTest:
  void TearDown() override {
    notification_service()->SetNotificationDisplayServiceForTesting(nullptr);
    InProcessBrowserTest::TearDown();
  }

  void LoadTestPage(const std::string& path) {
    ui_test_utils::NavigateToURL(GetBrowser(), https_server_->GetURL(path));
  }

  void LoadTestPage() { LoadTestPage(GetTestURL()); }

  void LoadTestPageWithoutManifest() { LoadTestPage(GetNoManifestTestURL()); }

  bool RunScript(const std::string& script, std::string* result) {
    return RunScript(script, result, nullptr);
  }

  bool RunScript(const std::string& script, std::string* result,
                 content::WebContents* web_contents) {
    if (!web_contents)
      web_contents = GetBrowser()->tab_strip_model()->GetActiveWebContents();
    return content::ExecuteScriptAndExtractString(web_contents->GetMainFrame(),
                                                  script, result);
  }

  gcm::GCMAppHandler* GetAppHandler() {
    return gcm_driver_->GetAppHandler(kPushMessagingAppIdentifierPrefix);
  }

  PermissionRequestManager* GetPermissionRequestManager() {
    return PermissionRequestManager::FromWebContents(
        GetBrowser()->tab_strip_model()->GetActiveWebContents());
  }

  // Calls should be wrapped in the ASSERT_NO_FATAL_FAILURE() macro.
  void RequestAndAcceptPermission();
  // Calls should be wrapped in the ASSERT_NO_FATAL_FAILURE() macro.
  void RequestAndDenyPermission();

  // Sets out_token to the subscription token (not including server URL).
  // Calls should be wrapped in the ASSERT_NO_FATAL_FAILURE() macro.
  void SubscribeSuccessfully(bool use_key = true,
                             std::string* out_token = nullptr);

  // Sets up the state corresponding to a dangling push subscription whose
  // service worker registration no longer exists. Some users may be left with
  // such orphaned subscriptions due to service worker unregistrations not
  // clearing push subscriptions in the past. This allows us to emulate that.
  // Calls should be wrapped in the ASSERT_NO_FATAL_FAILURE() macro.
  void SetupOrphanedPushSubscription(std::string* out_app_id);

  // Legacy subscribe path using GCMDriver rather than Instance IDs. Only
  // for testing that we maintain support for existing stored registrations.
  // Calls should be wrapped in the ASSERT_NO_FATAL_FAILURE() macro.
  void LegacySubscribeSuccessfully(std::string* out_subscription_id = nullptr);

  // Strips server URL from a registration endpoint to get subscription token.
  // Calls should be wrapped in the ASSERT_NO_FATAL_FAILURE() macro.
  void EndpointToToken(const std::string& endpoint,
                       bool standard_protocol = true,
                       std::string* out_token = nullptr);

  // Deletes an Instance ID from the GCM Store but keeps the push subscription
  // stored in the PushMessagingAppIdentifier map and Service Worker DB.
  // Calls should be wrapped in the ASSERT_NO_FATAL_FAILURE() macro.
  void DeleteInstanceIDAsIfGCMStoreReset(const std::string& app_id);

  PushMessagingAppIdentifier GetAppIdentifierForServiceWorkerRegistration(
      int64_t service_worker_registration_id);

  void SendMessageAndWaitUntilHandled(
      const PushMessagingAppIdentifier& app_identifier,
      const gcm::IncomingMessage& message);

  net::EmbeddedTestServer* https_server() const { return https_server_.get(); }

  // To be called when delivery of a push message has finished. The |run_loop|
  // will be told to quit after |messages_required| messages were received.
  void OnDeliveryFinished(std::vector<size_t>* number_of_notifications_shown,
                          const base::Closure& done_closure) {
    DCHECK(number_of_notifications_shown);

    number_of_notifications_shown->push_back(
        notification_manager_->GetNotificationCount());

    done_closure.Run();
  }

  StubNotificationUIManager* notification_manager() const {
    return notification_manager_.get();
  }

  PlatformNotificationServiceImpl* notification_service() const {
    return PlatformNotificationServiceImpl::GetInstance();
  }

  PushMessagingServiceImpl* push_service() const { return push_service_; }

  void SetSiteEngagementScore(const GURL& url,
                              double score,
                              double expected_score) {
    // There will be a bonus of 5.0 points for having notification permission
    // granted, so we assert that the final score is as expected.
    SiteEngagementService* service =
        SiteEngagementService::Get(GetBrowser()->profile());
    service->ResetBaseScoreForURL(url, score);
    EXPECT_EQ(expected_score, service->GetScore(url));
  }

 protected:
  virtual std::string GetTestURL() { return "/push_messaging/test.html"; }

  virtual std::string GetNoManifestTestURL() {
    return "/push_messaging/test_no_manifest.html";
  }

  virtual Browser* GetBrowser() const { return browser(); }

  gcm::FakeGCMProfileService* gcm_service_;
  instance_id::FakeGCMDriverForInstanceID* gcm_driver_;
  base::HistogramTester histogram_tester_;

 private:
  std::unique_ptr<net::EmbeddedTestServer> https_server_;
  PushMessagingServiceImpl* push_service_;

  std::unique_ptr<StubNotificationUIManager> notification_manager_;
  std::unique_ptr<MessageCenterDisplayService> display_service_;

  DISALLOW_COPY_AND_ASSIGN(PushMessagingBrowserTest);
};

void PushMessagingBrowserTest::RequestAndAcceptPermission() {
  std::string script_result;
  GetPermissionRequestManager()->set_auto_response_for_test(
      PermissionRequestManager::ACCEPT_ALL);
  ASSERT_TRUE(RunScript("requestNotificationPermission();", &script_result));
  ASSERT_EQ("permission status - granted", script_result);
}

void PushMessagingBrowserTest::RequestAndDenyPermission() {
  std::string script_result;
  GetPermissionRequestManager()->set_auto_response_for_test(
      PermissionRequestManager::DENY_ALL);
  ASSERT_TRUE(RunScript("requestNotificationPermission();", &script_result));
  ASSERT_EQ("permission status - denied", script_result);
}

void PushMessagingBrowserTest::SubscribeSuccessfully(bool use_key,
                                                     std::string* out_token) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  if (use_key) {
    ASSERT_TRUE(RunScript("removeManifest()", &script_result));
    ASSERT_EQ("manifest removed", script_result);

    ASSERT_TRUE(RunScript("documentSubscribePush()", &script_result));
  } else {
    // Test backwards compatibility with old ID based subscriptions.
    ASSERT_TRUE(RunScript("documentSubscribePushWithoutKey()", &script_result));
  }

  ASSERT_NO_FATAL_FAILURE(EndpointToToken(script_result, use_key, out_token));
}

void PushMessagingBrowserTest::SetupOrphanedPushSubscription(
    std::string* out_app_id) {
  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());
  GURL requesting_origin = https_server()->GetURL("/").GetOrigin();
  // Use 1234LL as it's unlikely to collide with an active service worker
  // registration id (they increment from 0).
  const int64_t service_worker_registration_id = 1234LL;

  content::PushSubscriptionOptions options;
  options.user_visible_only = true;
  options.sender_info = GetTestApplicationServerKey();
  base::RunLoop run_loop;
  push_service()->SubscribeFromWorker(
      requesting_origin, service_worker_registration_id, options,
      base::Bind(&DidRegister, run_loop.QuitClosure()));
  run_loop.Run();

  PushMessagingAppIdentifier app_identifier =
      PushMessagingAppIdentifier::FindByServiceWorker(
          GetBrowser()->profile(), requesting_origin,
          service_worker_registration_id);
  ASSERT_FALSE(app_identifier.is_null());
  *out_app_id = app_identifier.app_id();
}

void PushMessagingBrowserTest::LegacySubscribeSuccessfully(
    std::string* out_subscription_id) {
  // Create a non-InstanceID GCM registration. Have to directly access
  // GCMDriver, since this codepath has been deleted from Push.

  std::string script_result;
  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  GURL requesting_origin = https_server()->GetURL("/").GetOrigin();
  int64_t service_worker_registration_id = 0LL;
  PushMessagingAppIdentifier app_identifier =
      PushMessagingAppIdentifier::LegacyGenerateForTesting(
          requesting_origin, service_worker_registration_id);
  push_service_->IncreasePushSubscriptionCount(1, true /* is_pending */);

  std::string subscription_id;
  {
    base::RunLoop run_loop;
    gcm::GCMClient::Result register_result = gcm::GCMClient::UNKNOWN_ERROR;
    gcm_driver_->Register(
        app_identifier.app_id(), {kManifestSenderId},
        base::Bind(&LegacyRegisterCallback, run_loop.QuitClosure(),
                   &subscription_id, &register_result));
    run_loop.Run();
    ASSERT_EQ(gcm::GCMClient::SUCCESS, register_result);
  }

  app_identifier.PersistToPrefs(GetBrowser()->profile());
  push_service_->IncreasePushSubscriptionCount(1, false /* is_pending */);
  push_service_->DecreasePushSubscriptionCount(1, true /* was_pending */);

  {
    base::RunLoop run_loop;
    push_service_->StorePushSubscriptionForTesting(
        GetBrowser()->profile(), requesting_origin,
        service_worker_registration_id, subscription_id, kManifestSenderId,
        run_loop.QuitClosure());
    run_loop.Run();
  }

  if (out_subscription_id)
    *out_subscription_id = subscription_id;
}

void PushMessagingBrowserTest::EndpointToToken(const std::string& endpoint,
                                               bool standard_protocol,
                                               std::string* out_token) {
  size_t last_slash = endpoint.rfind('/');

  ASSERT_EQ(push_service()->GetEndpoint(standard_protocol).spec(),
            endpoint.substr(0, last_slash + 1));

  ASSERT_LT(last_slash + 1, endpoint.length());  // Token must not be empty.

  if (out_token)
    *out_token = endpoint.substr(last_slash + 1);
}

PushMessagingAppIdentifier
PushMessagingBrowserTest::GetAppIdentifierForServiceWorkerRegistration(
    int64_t service_worker_registration_id) {
  GURL origin = https_server()->GetURL("/").GetOrigin();
  PushMessagingAppIdentifier app_identifier =
      PushMessagingAppIdentifier::FindByServiceWorker(
          GetBrowser()->profile(), origin, service_worker_registration_id);
  EXPECT_FALSE(app_identifier.is_null());
  return app_identifier;
}

void PushMessagingBrowserTest::DeleteInstanceIDAsIfGCMStoreReset(
    const std::string& app_id) {
  // Delete the Instance ID directly, keeping the push subscription stored in
  // the PushMessagingAppIdentifier map and the Service Worker database. This
  // simulates the GCM Store getting reset but failing to clear push
  // subscriptions, either because the store got reset before
  // 93ec793ac69a542b2213297737178a55d069fd0d (Chrome 56), or because a race
  // condition (e.g. shutdown) prevents PushMessagingServiceImpl::OnStoreReset
  // from clearing all subscriptions.
  instance_id::InstanceIDProfileService* instance_id_profile_service =
      instance_id::InstanceIDProfileServiceFactory::GetForProfile(
          GetBrowser()->profile());
  DCHECK(instance_id_profile_service);
  instance_id::InstanceIDDriver* instance_id_driver =
      instance_id_profile_service->driver();
  DCHECK(instance_id_driver);
  instance_id::InstanceID::Result delete_result =
      instance_id::InstanceID::UNKNOWN_ERROR;
  base::RunLoop run_loop;
  instance_id_driver->GetInstanceID(app_id)->DeleteID(base::Bind(
      &InstanceIDResultCallback, run_loop.QuitClosure(), &delete_result));
  run_loop.Run();
  ASSERT_EQ(instance_id::InstanceID::SUCCESS, delete_result);
}

void PushMessagingBrowserTest::SendMessageAndWaitUntilHandled(
    const PushMessagingAppIdentifier& app_identifier,
    const gcm::IncomingMessage& message) {
  base::RunLoop run_loop;
  push_service()->SetMessageCallbackForTesting(run_loop.QuitClosure());
  push_service()->OnMessage(app_identifier.app_id(), message);
  run_loop.Run();
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       SubscribeWithoutKeySuccessNotificationsGranted) {
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(false /* use_key */));
  EXPECT_EQ(kManifestSenderId, gcm_driver_->last_gettoken_authorized_entity());
  EXPECT_EQ(GetAppIdentifierForServiceWorkerRegistration(0LL).app_id(),
            gcm_driver_->last_gettoken_app_id());
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       SubscribeSuccessNotificationsGranted) {
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  EXPECT_EQ(kEncodedApplicationServerKey,
            gcm_driver_->last_gettoken_authorized_entity());
  EXPECT_EQ(GetAppIdentifierForServiceWorkerRegistration(0LL).app_id(),
            gcm_driver_->last_gettoken_app_id());
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       SubscribeSuccessNotificationsPrompt) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  GetPermissionRequestManager()->set_auto_response_for_test(
      PermissionRequestManager::ACCEPT_ALL);
  ASSERT_TRUE(RunScript("documentSubscribePush()", &script_result));
  // Both of these methods EXPECT that they succeed.
  ASSERT_NO_FATAL_FAILURE(EndpointToToken(script_result));
  GetAppIdentifierForServiceWorkerRegistration(0LL);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       SubscribeFailureNotificationsBlocked) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndDenyPermission());

  ASSERT_TRUE(RunScript("documentSubscribePush()", &script_result));
  EXPECT_EQ("NotAllowedError - Registration failed - permission denied",
            script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, SubscribeFailureNoManifest) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  ASSERT_TRUE(RunScript("removeManifest()", &script_result));
  ASSERT_EQ("manifest removed", script_result);

  ASSERT_TRUE(RunScript("documentSubscribePushWithoutKey()", &script_result));
  EXPECT_EQ(
      "AbortError - Registration failed - missing applicationServerKey, and "
      "manifest empty or missing",
      script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, SubscribeFailureNoSenderId) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  ASSERT_TRUE(RunScript("swapManifestNoSenderId()", &script_result));
  ASSERT_EQ("sender id removed from manifest", script_result);

  ASSERT_TRUE(RunScript("documentSubscribePushWithoutKey()", &script_result));
  EXPECT_EQ(
      "AbortError - Registration failed - missing applicationServerKey, and "
      "gcm_sender_id not found in manifest",
      script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       RegisterFailureEmptyPushSubscriptionOptions) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  ASSERT_TRUE(
      RunScript("documentSubscribePushWithEmptyOptions()", &script_result));
  EXPECT_EQ("NotAllowedError - Registration failed - permission denied",
            script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, SubscribeWorker) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  LoadTestPage();  // Reload to become controlled.

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  // Try to subscribe from a worker without a key. This should fail.
  ASSERT_TRUE(RunScript("workerSubscribePushNoKey()", &script_result));
  EXPECT_EQ(
      "AbortError - Registration failed - missing applicationServerKey, and "
      "gcm_sender_id not found in manifest",
      script_result);

  // Now run the subscribe with a key. This should succeed.
  ASSERT_TRUE(RunScript("workerSubscribePush()", &script_result));
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, true /* standard_protocol */));

  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       ResubscribeWithoutKeyAfterSubscribingWithKeyInManifest) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  LoadTestPage();  // Reload to become controlled.

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  // Run the subscription from the document without a key, this will trigger
  // the code to read sender id from the manifest and will write it to the
  // datastore.
  ASSERT_TRUE(RunScript("documentSubscribePushWithoutKey()", &script_result));
  std::string token1;
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, false /* standard_protocol */, &token1));

  ASSERT_TRUE(RunScript("removeManifest()", &script_result));
  ASSERT_EQ("manifest removed", script_result);

  // Try to resubscribe from the document without a key or manifest.
  // This should fail.
  ASSERT_TRUE(RunScript("documentSubscribePushWithoutKey()", &script_result));
  EXPECT_EQ(
      "AbortError - Registration failed - missing applicationServerKey, "
      "and manifest empty or missing",
      script_result);

  // Now run the subscribe from the service worker without a key.
  // In this case, the sender id should be read from the datastore.
  ASSERT_TRUE(RunScript("workerSubscribePushNoKey()", &script_result));
  std::string token2;
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, false /* standard_protocol */, &token2));
  EXPECT_EQ(token1, token2);

  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);

  // After unsubscribing, subscribe again from the worker with no key.
  // The sender id should again be read from the datastore, so the
  // subcribe should succeed, and we should get a new subscription token.
  ASSERT_TRUE(RunScript("workerSubscribePushNoKey()", &script_result));
  std::string token3;
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, false /* standard_protocol */, &token3));
  EXPECT_NE(token1, token3);

  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
}

IN_PROC_BROWSER_TEST_F(
    PushMessagingBrowserTest,
    ResubscribeWithoutKeyAfterSubscribingFromDocumentWithP256Key) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  LoadTestPageWithoutManifest();  // Reload to become controlled.

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  // Run the subscription from the document with a key.
  ASSERT_TRUE(RunScript("documentSubscribePush()", &script_result));
  ASSERT_NO_FATAL_FAILURE(EndpointToToken(script_result));

  // Try to resubscribe from the document without a key - should fail.
  ASSERT_TRUE(RunScript("documentSubscribePushWithoutKey()", &script_result));
  EXPECT_EQ(
      "AbortError - Registration failed - missing applicationServerKey, "
      "and manifest empty or missing",
      script_result);

  // Now try to resubscribe from the service worker without a key.
  // This should also fail as the original key was not numeric.
  ASSERT_TRUE(RunScript("workerSubscribePushNoKey()", &script_result));
  EXPECT_EQ(
      "AbortError - Registration failed - missing applicationServerKey, "
      "and gcm_sender_id not found in manifest",
      script_result);

  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);

  // After unsubscribing, try to resubscribe again without a key.
  // This should again fail.
  ASSERT_TRUE(RunScript("workerSubscribePushNoKey()", &script_result));
  EXPECT_EQ(
      "AbortError - Registration failed - missing applicationServerKey, "
      "and gcm_sender_id not found in manifest",
      script_result);
}

IN_PROC_BROWSER_TEST_F(
    PushMessagingBrowserTest,
    ResubscribeWithoutKeyAfterSubscribingFromWorkerWithP256Key) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  LoadTestPageWithoutManifest();  // Reload to become controlled.

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  // Run the subscribe from the service worker with a key.
  // This should succeed.
  ASSERT_TRUE(RunScript("workerSubscribePush()", &script_result));
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, true /* standard_protocol */));

  // Try to resubscribe from the document without a key - should fail.
  ASSERT_TRUE(RunScript("documentSubscribePushWithoutKey()", &script_result));
  EXPECT_EQ(
      "AbortError - Registration failed - missing applicationServerKey, "
      "and manifest empty or missing",
      script_result);

  // Now try to resubscribe from the service worker without a key.
  // This should also fail as the original key was not numeric.
  ASSERT_TRUE(RunScript("workerSubscribePushNoKey()", &script_result));
  EXPECT_EQ(
      "AbortError - Registration failed - missing applicationServerKey, and "
      "gcm_sender_id not found in manifest",
      script_result);

  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);

  // After unsubscribing, try to resubscribe again without a key.
  // This should again fail.
  ASSERT_TRUE(RunScript("workerSubscribePushNoKey()", &script_result));
  EXPECT_EQ(
      "AbortError - Registration failed - missing applicationServerKey, "
      "and gcm_sender_id not found in manifest",
      script_result);
}

IN_PROC_BROWSER_TEST_F(
    PushMessagingBrowserTest,
    ResubscribeWithoutKeyAfterSubscribingFromDocumentWithNumber) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  LoadTestPageWithoutManifest();  // Reload to become controlled.

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  // Run the subscribe from the document with a numeric key.
  // This should succeed.
  ASSERT_TRUE(
      RunScript("documentSubscribePushWithNumericKey()", &script_result));
  std::string token1;
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, false /* standard_protocol */, &token1));

  // Try to resubscribe from the document without a key - should fail.
  ASSERT_TRUE(RunScript("documentSubscribePushWithoutKey()", &script_result));
  EXPECT_EQ(
      "AbortError - Registration failed - missing applicationServerKey, "
      "and manifest empty or missing",
      script_result);

  // Now run the subscribe from the service worker without a key.
  // In this case, the sender id should be read from the datastore.
  // Note, we would rather this failed as we only really want to support
  // no-key subscribes after subscribing with a numeric gcm sender id in the
  // manifest, not a numeric applicationServerKey, but for code simplicity
  // this case is allowed.
  ASSERT_TRUE(RunScript("workerSubscribePushNoKey()", &script_result));
  std::string token2;
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, false /* standard_protocol */, &token2));
  EXPECT_EQ(token1, token2);

  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);

  // After unsubscribing, subscribe again from the worker with no key.
  // The sender id should again be read from the datastore, so the
  // subcribe should succeed, and we should get a new subscription token.
  ASSERT_TRUE(RunScript("workerSubscribePushNoKey()", &script_result));
  std::string token3;
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, false /* standard_protocol */, &token3));
  EXPECT_NE(token1, token3);

  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
}

IN_PROC_BROWSER_TEST_F(
    PushMessagingBrowserTest,
    ResubscribeWithoutKeyAfterSubscribingFromWorkerWithNumber) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  LoadTestPageWithoutManifest();  // Reload to become controlled.

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  // Run the subscribe from the service worker with a numeric key.
  // This should succeed.
  ASSERT_TRUE(RunScript("workerSubscribePushWithNumericKey()", &script_result));
  std::string token1;
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, false /* standard_protocol */, &token1));

  // Try to resubscribe from the document without a key - should fail.
  ASSERT_TRUE(RunScript("documentSubscribePushWithoutKey()", &script_result));
  EXPECT_EQ(
      "AbortError - Registration failed - missing applicationServerKey, "
      "and manifest empty or missing",
      script_result);

  // Now run the subscribe from the service worker without a key.
  // In this case, the sender id should be read from the datastore.
  // Note, we would rather this failed as we only really want to support
  // no-key subscribes after subscribing with a numeric gcm sender id in the
  // manifest, not a numeric applicationServerKey, but for code simplicity
  // this case is allowed.
  ASSERT_TRUE(RunScript("workerSubscribePushNoKey()", &script_result));
  std::string token2;
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, false /* standard_protocol */, &token2));
  EXPECT_EQ(token1, token2);

  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);

  // After unsubscribing, subscribe again from the worker with no key.
  // The sender id should again be read from the datastore, so the
  // subcribe should succeed, and we should get a new subscription token.
  ASSERT_TRUE(RunScript("workerSubscribePushNoKey()", &script_result));
  std::string token3;
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, false /* standard_protocol */, &token3));
  EXPECT_NE(token1, token3);

  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, ResubscribeWithMismatchedKey) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  LoadTestPage();  // Reload to become controlled.

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  // Run the subscribe from the service worker with a key.
  // This should succeed.
  ASSERT_TRUE(
      RunScript("workerSubscribePushWithNumericKey('11111')", &script_result));
  std::string token1;
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, false /* standard_protocol */, &token1));

  // Try to resubscribe with a different key - should fail.
  ASSERT_TRUE(
      RunScript("workerSubscribePushWithNumericKey('22222')", &script_result));
  EXPECT_EQ(
      "InvalidStateError - Registration failed - A subscription with a "
      "different applicationServerKey (or gcm_sender_id) already exists; to "
      "change the applicationServerKey, unsubscribe then resubscribe.",
      script_result);

  // Try to resubscribe with the original key - should succeed.
  ASSERT_TRUE(
      RunScript("workerSubscribePushWithNumericKey('11111')", &script_result));
  std::string token2;
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, false /* standard_protocol */, &token2));
  EXPECT_EQ(token1, token2);

  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);

  // Resubscribe with a different key after unsubscribing.
  // Should succeed, and we should get a new subscription token.
  ASSERT_TRUE(
      RunScript("workerSubscribePushWithNumericKey('22222')", &script_result));
  std::string token3;
  ASSERT_NO_FATAL_FAILURE(
      EndpointToToken(script_result, false /* standard_protocol */, &token3));
  EXPECT_NE(token1, token3);

  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, SubscribePersisted) {
  std::string script_result;

  // First, test that Service Worker registration IDs are assigned in order of
  // registering the Service Workers, and the (fake) push subscription ids are
  // assigned in order of push subscription (even when these orders are
  // different).

  std::string token1;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(true /* use_key */, &token1));
  PushMessagingAppIdentifier sw0_identifier =
      GetAppIdentifierForServiceWorkerRegistration(0LL);
  EXPECT_EQ(sw0_identifier.app_id(), gcm_driver_->last_gettoken_app_id());

  LoadTestPage("/push_messaging/subscope1/test.html");
  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  LoadTestPage("/push_messaging/subscope2/test.html");
  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  // Note that we need to reload the page after registering, otherwise
  // navigator.serviceWorker.ready is going to be resolved with the parent
  // Service Worker which still controls the page.
  LoadTestPage("/push_messaging/subscope2/test.html");
  std::string token2;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(true /* use_key */, &token2));
  EXPECT_NE(token1, token2);
  PushMessagingAppIdentifier sw2_identifier =
      GetAppIdentifierForServiceWorkerRegistration(2LL);
  EXPECT_EQ(sw2_identifier.app_id(), gcm_driver_->last_gettoken_app_id());

  LoadTestPage("/push_messaging/subscope1/test.html");
  std::string token3;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(true /* use_key */, &token3));
  EXPECT_NE(token1, token3);
  EXPECT_NE(token2, token3);
  PushMessagingAppIdentifier sw1_identifier =
      GetAppIdentifierForServiceWorkerRegistration(1LL);
  EXPECT_EQ(sw1_identifier.app_id(), gcm_driver_->last_gettoken_app_id());

  // Now test that the Service Worker registration IDs and push subscription IDs
  // generated above were persisted to SW storage, by checking that they are
  // unchanged despite requesting them in a different order.
  // TODO(johnme): Ideally we would restart the browser at this point to check
  // they were persisted to disk, but that's not currently possible since the
  // test server uses random port numbers for each test (even PRE_Foo and Foo),
  // so we wouldn't be able to load the test pages with the same origin.

  LoadTestPage("/push_messaging/subscope1/test.html");
  std::string token4;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(true /* use_key */, &token4));
  EXPECT_EQ(token3, token4);
  EXPECT_EQ(sw1_identifier.app_id(), gcm_driver_->last_gettoken_app_id());

  LoadTestPage("/push_messaging/subscope2/test.html");
  std::string token5;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(true /* use_key */, &token5));
  EXPECT_EQ(token2, token5);
  EXPECT_EQ(sw1_identifier.app_id(), gcm_driver_->last_gettoken_app_id());

  LoadTestPage();
  std::string token6;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(true /* use_key */, &token6));
  EXPECT_EQ(token1, token6);
  EXPECT_EQ(sw1_identifier.app_id(), gcm_driver_->last_gettoken_app_id());
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, AppHandlerOnlyIfSubscribed) {
  // This test restarts the push service to simulate restarting the browser.

  EXPECT_NE(push_service(), GetAppHandler());
  ASSERT_NO_FATAL_FAILURE(RestartPushService());
  EXPECT_NE(push_service(), GetAppHandler());

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());

  EXPECT_EQ(push_service(), GetAppHandler());
  ASSERT_NO_FATAL_FAILURE(RestartPushService());
  EXPECT_EQ(push_service(), GetAppHandler());

  std::string script_result;

  // Unsubscribe.
  base::RunLoop run_loop;
  push_service()->SetUnsubscribeCallbackForTesting(run_loop.QuitClosure());
  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
  // The app handler is only guaranteed to be unregistered once the unsubscribe
  // callback for testing has been run (PushSubscription.unsubscribe() usually
  // resolves before that, in order to avoid blocking on network retries etc).
  run_loop.Run();

  EXPECT_NE(push_service(), GetAppHandler());
  ASSERT_NO_FATAL_FAILURE(RestartPushService());
  EXPECT_NE(push_service(), GetAppHandler());
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, PushEventSuccess) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  PushMessagingAppIdentifier app_identifier =
      GetAppIdentifierForServiceWorkerRegistration(0LL);

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("false - is not controlled", script_result);
  LoadTestPage();  // Reload to become controlled.
  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  EXPECT_TRUE(IsRegisteredKeepAliveEqualTo(false));
  gcm::IncomingMessage message;
  message.sender_id = GetTestApplicationServerKey();
  message.raw_data = "testdata";
  message.decrypted = true;
  push_service()->OnMessage(app_identifier.app_id(), message);
  EXPECT_TRUE(IsRegisteredKeepAliveEqualTo(true));
  ASSERT_TRUE(RunScript("resultQueue.pop()", &script_result));
  EXPECT_EQ("testdata", script_result);

  // Check that we record this case in UMA.
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.DeliveryStatus.FindServiceWorker",
      0 /* SERVICE_WORKER_OK */, 1);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.DeliveryStatus.ServiceWorkerEvent",
      0 /* SERVICE_WORKER_OK */, 1);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.DeliveryStatus",
       content::PUSH_DELIVERY_STATUS_SUCCESS, 1);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, PushEventOnShutdown) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  PushMessagingAppIdentifier app_identifier =
      GetAppIdentifierForServiceWorkerRegistration(0LL);

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("false - is not controlled", script_result);
  LoadTestPage();  // Reload to become controlled.
  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  EXPECT_TRUE(IsRegisteredKeepAliveEqualTo(false));
  gcm::IncomingMessage message;
  message.sender_id = GetTestApplicationServerKey();
  message.raw_data = "testdata";
  message.decrypted = true;
  push_service()->Observe(chrome::NOTIFICATION_APP_TERMINATING,
                          content::NotificationService::AllSources(),
                          content::NotificationService::NoDetails());
  push_service()->OnMessage(app_identifier.app_id(), message);
  EXPECT_TRUE(IsRegisteredKeepAliveEqualTo(false));
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, PushEventWithoutPayload) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  PushMessagingAppIdentifier app_identifier =
      GetAppIdentifierForServiceWorkerRegistration(0LL);

  LoadTestPage();  // Reload to become controlled.
  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  gcm::IncomingMessage message;
  message.sender_id = GetTestApplicationServerKey();
  message.decrypted = false;

  push_service()->OnMessage(app_identifier.app_id(), message);
  ASSERT_TRUE(RunScript("resultQueue.pop()", &script_result));
  EXPECT_EQ("[NULL]", script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, LegacyPushEvent) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(LegacySubscribeSuccessfully());
  PushMessagingAppIdentifier app_identifier =
      GetAppIdentifierForServiceWorkerRegistration(0LL);

  LoadTestPage();  // Reload to become controlled.
  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  gcm::IncomingMessage message;
  message.sender_id = kManifestSenderId;
  message.decrypted = false;

  push_service()->OnMessage(app_identifier.app_id(), message);
  ASSERT_TRUE(RunScript("resultQueue.pop()", &script_result));
  EXPECT_EQ("[NULL]", script_result);
}

// Some users may have gotten into a state in the past where they still have
// a subscription even though the service worker was unregistered.
// Emulate this and test a push message triggers unsubscription.
IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, PushEventNoServiceWorker) {
  std::string app_id;
  ASSERT_NO_FATAL_FAILURE(SetupOrphanedPushSubscription(&app_id));

  // Try to send a push message.
  gcm::IncomingMessage message;
  message.sender_id = GetTestApplicationServerKey();
  message.raw_data = "testdata";
  message.decrypted = true;

  base::RunLoop run_loop;
  push_service()->SetMessageCallbackForTesting(run_loop.QuitClosure());
  EXPECT_TRUE(IsRegisteredKeepAliveEqualTo(false));
  push_service()->OnMessage(app_id, message);
  EXPECT_TRUE(IsRegisteredKeepAliveEqualTo(true));
  run_loop.Run();
  EXPECT_TRUE(IsRegisteredKeepAliveEqualTo(false));

  // No push data should have been received.
  std::string script_result;
  ASSERT_TRUE(RunScript("resultQueue.popImmediately()", &script_result));
  EXPECT_EQ("null", script_result);

  // Check that we record this case in UMA.
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.DeliveryStatus.FindServiceWorker",
      5 /* SERVICE_WORKER_ERROR_NOT_FOUND */, 1);
  histogram_tester_.ExpectTotalCount(
      "PushMessaging.DeliveryStatus.ServiceWorkerEvent", 0);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.DeliveryStatus",
      content::PUSH_DELIVERY_STATUS_NO_SERVICE_WORKER, 1);

  // Missing Service Workers should trigger an automatic unsubscription attempt.
  EXPECT_EQ(app_id, gcm_driver_->last_deletetoken_app_id());
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_DELIVERY_NO_SERVICE_WORKER, 1);

  // |app_identifier| should no longer be stored in prefs.
  PushMessagingAppIdentifier stored_app_identifier =
      PushMessagingAppIdentifier::FindByAppId(GetBrowser()->profile(), app_id);
  EXPECT_TRUE(stored_app_identifier.is_null());
}

// Tests receiving messages for a subscription that no longer exists.
IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, NoSubscription) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  PushMessagingAppIdentifier app_identifier =
      GetAppIdentifierForServiceWorkerRegistration(0LL);

  LoadTestPage();  // Reload to become controlled.
  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_JAVASCRIPT_API, 1);

  gcm::IncomingMessage message;
  message.sender_id = GetTestApplicationServerKey();
  message.raw_data = "testdata";
  message.decrypted = true;
  SendMessageAndWaitUntilHandled(app_identifier, message);

  // No push data should have been received.
  ASSERT_TRUE(RunScript("resultQueue.popImmediately()", &script_result));
  EXPECT_EQ("null", script_result);

  // Check that we record this case in UMA.
  histogram_tester_.ExpectTotalCount(
      "PushMessaging.DeliveryStatus.FindServiceWorker", 0);
  histogram_tester_.ExpectTotalCount(
      "PushMessaging.DeliveryStatus.ServiceWorkerEvent", 0);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.DeliveryStatus",
      content::PUSH_DELIVERY_STATUS_UNKNOWN_APP_ID, 1);

  // Missing subscriptions should trigger an automatic unsubscription attempt.
  EXPECT_EQ(app_identifier.app_id(), gcm_driver_->last_deletetoken_app_id());
  histogram_tester_.ExpectBucketCount(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_DELIVERY_UNKNOWN_APP_ID, 1);
}

// Tests receiving messages for an origin that does not have permission, but
// somehow still has a subscription (as happened in https://crbug.com/633310).
IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, PushEventWithoutPermission) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  PushMessagingAppIdentifier app_identifier =
      GetAppIdentifierForServiceWorkerRegistration(0LL);

  LoadTestPage();  // Reload to become controlled.
  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  // Revoke notifications permission, but first disable the
  // PushMessagingServiceImpl's OnContentSettingChanged handler so that it
  // doesn't automatically unsubscribe, since we want to test the case where
  // there is still a subscription.
  HostContentSettingsMapFactory::GetForProfile(GetBrowser()->profile())
      ->RemoveObserver(push_service());
  HostContentSettingsMapFactory::GetForProfile(GetBrowser()->profile())
      ->ClearSettingsForOneType(CONTENT_SETTINGS_TYPE_NOTIFICATIONS);
  base::RunLoop().RunUntilIdle();

  gcm::IncomingMessage message;
  message.sender_id = GetTestApplicationServerKey();
  message.raw_data = "testdata";
  message.decrypted = true;
  SendMessageAndWaitUntilHandled(app_identifier, message);

  // No push data should have been received.
  ASSERT_TRUE(RunScript("resultQueue.popImmediately()", &script_result));
  EXPECT_EQ("null", script_result);

  // Check that we record this case in UMA.
  histogram_tester_.ExpectTotalCount(
      "PushMessaging.DeliveryStatus.FindServiceWorker", 0);
  histogram_tester_.ExpectTotalCount(
      "PushMessaging.DeliveryStatus.ServiceWorkerEvent", 0);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.DeliveryStatus",
      content::PUSH_DELIVERY_STATUS_PERMISSION_DENIED, 1);

  // Missing permission should trigger an automatic unsubscription attempt.
  EXPECT_EQ(app_identifier.app_id(), gcm_driver_->last_deletetoken_app_id());
  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("false - not subscribed", script_result);
  GURL origin = https_server()->GetURL("/").GetOrigin();
  PushMessagingAppIdentifier app_identifier_afterwards =
      PushMessagingAppIdentifier::FindByServiceWorker(GetBrowser()->profile(),
                                                      origin, 0LL);
  EXPECT_TRUE(app_identifier_afterwards.is_null());
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_DELIVERY_PERMISSION_DENIED, 1);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       PushEventEnforcesUserVisibleNotification) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  PushMessagingAppIdentifier app_identifier =
      GetAppIdentifierForServiceWorkerRegistration(0LL);

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("false - is not controlled", script_result);

  LoadTestPage();  // Reload to become controlled.

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  notification_manager()->CancelAll();
  ASSERT_EQ(0u, notification_manager()->GetNotificationCount());

  // We'll need to specify the web_contents in which to eval script, since we're
  // going to run script in a background tab.
  content::WebContents* web_contents =
      GetBrowser()->tab_strip_model()->GetActiveWebContents();

  // Set the site engagement score for the site. Setting it to 10 means it
  // should have a budget of 4, enough for two non-shown notification, which
  // cost 2 each.
  SetSiteEngagementScore(web_contents->GetURL(), 5.0, 10.0);

  // If the site is visible in an active tab, we should not force a notification
  // to be shown. Try it twice, since we allow one mistake per 10 push events.
  gcm::IncomingMessage message;
  message.sender_id = GetTestApplicationServerKey();
  message.decrypted = true;
  for (int n = 0; n < 2; n++) {
    message.raw_data = "testdata";
    SendMessageAndWaitUntilHandled(app_identifier, message);
    ASSERT_TRUE(RunScript("resultQueue.pop()", &script_result));
    EXPECT_EQ("testdata", script_result);
    EXPECT_EQ(0u, notification_manager()->GetNotificationCount());
  }

  // Open a blank foreground tab so site is no longer visible.
  ui_test_utils::NavigateToURLWithDisposition(
      GetBrowser(), GURL("about:blank"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB);

  // If the Service Worker push event handler shows a notification, we
  // should not show a forced one.
  message.raw_data = "shownotification";
  SendMessageAndWaitUntilHandled(app_identifier, message);
  ASSERT_TRUE(RunScript("resultQueue.pop()", &script_result, web_contents));
  EXPECT_EQ("shownotification", script_result);
  EXPECT_EQ(1u, notification_manager()->GetNotificationCount());
  EXPECT_EQ("push_test_tag",
            notification_manager()->GetNotificationAt(0).tag());
  notification_manager()->CancelAll();

  // If the Service Worker push event handler does not show a notification, we
  // should show a forced one, but only once the origin is out of budget.
  message.raw_data = "testdata";
  for (int n = 0; n < 2; n++) {
    // First two missed notifications shouldn't force a default one.
    SendMessageAndWaitUntilHandled(app_identifier, message);
    ASSERT_TRUE(RunScript("resultQueue.pop()", &script_result, web_contents));
    EXPECT_EQ("testdata", script_result);
    EXPECT_EQ(0u, notification_manager()->GetNotificationCount());
  }

  // Third missed notification should trigger a default notification, since the
  // origin will be out of budget.
  message.raw_data = "testdata";
  SendMessageAndWaitUntilHandled(app_identifier, message);
  ASSERT_TRUE(RunScript("resultQueue.pop()", &script_result, web_contents));
  EXPECT_EQ("testdata", script_result);

  ASSERT_EQ(1u, notification_manager()->GetNotificationCount());
  {
    const Notification& forced_notification =
        notification_manager()->GetNotificationAt(0);

    EXPECT_EQ(kPushMessagingForcedNotificationTag, forced_notification.tag());
    EXPECT_TRUE(forced_notification.silent());
  }

  // The notification will be automatically dismissed when the developer shows
  // a new notification themselves at a later point in time.
  message.raw_data = "shownotification";
  SendMessageAndWaitUntilHandled(app_identifier, message);
  ASSERT_TRUE(RunScript("resultQueue.pop()", &script_result, web_contents));
  EXPECT_EQ("shownotification", script_result);

  ASSERT_EQ(1u, notification_manager()->GetNotificationCount());
  {
    const Notification& first_notification =
        notification_manager()->GetNotificationAt(0);

    EXPECT_NE(kPushMessagingForcedNotificationTag, first_notification.tag());
  }
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       PushEventAllowSilentPushCommandLineFlag) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  PushMessagingAppIdentifier app_identifier =
      GetAppIdentifierForServiceWorkerRegistration(0LL);
  EXPECT_EQ(app_identifier.app_id(), gcm_driver_->last_gettoken_app_id());
  EXPECT_EQ(kEncodedApplicationServerKey,
            gcm_driver_->last_gettoken_authorized_entity());

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("false - is not controlled", script_result);

  LoadTestPage();  // Reload to become controlled.

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  notification_manager()->CancelAll();
  ASSERT_EQ(0u, notification_manager()->GetNotificationCount());

  // We'll need to specify the web_contents in which to eval script, since we're
  // going to run script in a background tab.
  content::WebContents* web_contents =
      GetBrowser()->tab_strip_model()->GetActiveWebContents();

  SetSiteEngagementScore(web_contents->GetURL(), 0.0, 5.0);

  ui_test_utils::NavigateToURLWithDisposition(
      GetBrowser(), GURL("about:blank"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB);

  // Send a missed notification to use up the budget.
  gcm::IncomingMessage message;
  message.sender_id = GetTestApplicationServerKey();
  message.raw_data = "testdata";
  message.decrypted = true;

  SendMessageAndWaitUntilHandled(app_identifier, message);
  ASSERT_TRUE(RunScript("resultQueue.pop()", &script_result, web_contents));
  EXPECT_EQ("testdata", script_result);
  EXPECT_EQ(0u, notification_manager()->GetNotificationCount());

  // If the Service Worker push event handler does not show a notification, we
  // should show a forced one providing there is no foreground tab and the
  // origin ran out of budget.
  SendMessageAndWaitUntilHandled(app_identifier, message);
  ASSERT_TRUE(RunScript("resultQueue.pop()", &script_result, web_contents));
  EXPECT_EQ("testdata", script_result);

  // Because the --allow-silent-push command line flag has not been passed,
  // this should have shown a default notification.
  ASSERT_EQ(1u, notification_manager()->GetNotificationCount());
  {
    const Notification& forced_notification =
        notification_manager()->GetNotificationAt(0);

    EXPECT_EQ(kPushMessagingForcedNotificationTag, forced_notification.tag());
    EXPECT_TRUE(forced_notification.silent());
  }

  notification_manager()->CancelAll();

  // Send the message again, but this time with the -allow-silent-push command
  // line flag set. The default notification should *not* be shown.
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kAllowSilentPush);

  SendMessageAndWaitUntilHandled(app_identifier, message);
  ASSERT_TRUE(RunScript("resultQueue.pop()", &script_result, web_contents));
  EXPECT_EQ("testdata", script_result);

  ASSERT_EQ(0u, notification_manager()->GetNotificationCount());
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       PushEventEnforcesUserVisibleNotificationAfterQueue) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  PushMessagingAppIdentifier app_identifier =
      GetAppIdentifierForServiceWorkerRegistration(0LL);

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("false - is not controlled", script_result);

  LoadTestPage();  // Reload to become controlled.

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  // Fire off two push messages in sequence, only the second one of which will
  // display a notification. The additional round-trip and I/O required by the
  // second message, which shows a notification, should give us a reasonable
  // confidence that the ordering will be maintained.

  std::vector<size_t> number_of_notifications_shown;

  gcm::IncomingMessage message;
  message.sender_id = GetTestApplicationServerKey();
  message.decrypted = true;

  {
    base::RunLoop run_loop;
    push_service()->SetMessageCallbackForTesting(base::Bind(
        &PushMessagingBrowserTest::OnDeliveryFinished, base::Unretained(this),
        &number_of_notifications_shown,
        base::BarrierClosure(2 /* num_closures */, run_loop.QuitClosure())));

    message.raw_data = "testdata";
    push_service()->OnMessage(app_identifier.app_id(), message);

    message.raw_data = "shownotification";
    push_service()->OnMessage(app_identifier.app_id(), message);

    run_loop.Run();
  }

  ASSERT_EQ(2u, number_of_notifications_shown.size());
  EXPECT_EQ(0u, number_of_notifications_shown[0]);
  EXPECT_EQ(1u, number_of_notifications_shown[1]);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       PushEventNotificationWithoutEventWaitUntil) {
  std::string script_result;
  content::WebContents* web_contents =
      GetBrowser()->tab_strip_model()->GetActiveWebContents();

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  PushMessagingAppIdentifier app_identifier =
      GetAppIdentifierForServiceWorkerRegistration(0LL);

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("false - is not controlled", script_result);

  LoadTestPage();  // Reload to become controlled.

  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  base::RunLoop run_loop;
  base::Closure quit_barrier =
      base::BarrierClosure(2 /* num_closures */, run_loop.QuitClosure());
  push_service()->SetMessageCallbackForTesting(quit_barrier);
  notification_manager()->SetNotificationAddedCallback(quit_barrier);

  gcm::IncomingMessage message;
  message.sender_id = GetTestApplicationServerKey();
  message.raw_data = "shownotification-without-waituntil";
  message.decrypted = true;
  EXPECT_TRUE(IsRegisteredKeepAliveEqualTo(false));
  push_service()->OnMessage(app_identifier.app_id(), message);
  EXPECT_TRUE(IsRegisteredKeepAliveEqualTo(true));
  ASSERT_TRUE(RunScript("resultQueue.pop()", &script_result, web_contents));
  EXPECT_EQ("immediate:shownotification-without-waituntil", script_result);

  run_loop.Run();

  EXPECT_TRUE(IsRegisteredKeepAliveEqualTo(false));
  ASSERT_EQ(1u, notification_manager()->GetNotificationCount());
  EXPECT_EQ("push_test_tag",
            notification_manager()->GetNotificationAt(0).tag());

  // Verify that the renderer process hasn't crashed.
  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - granted", script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, PermissionStateSaysPrompt) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  ASSERT_EQ("permission status - prompt", script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, PermissionStateSaysGranted) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndAcceptPermission());

  ASSERT_TRUE(RunScript("documentSubscribePush()", &script_result));
  ASSERT_NO_FATAL_FAILURE(EndpointToToken(script_result));

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - granted", script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, PermissionStateSaysDenied) {
  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  ASSERT_NO_FATAL_FAILURE(RequestAndDenyPermission());

  ASSERT_TRUE(RunScript("documentSubscribePush()", &script_result));
  EXPECT_EQ("NotAllowedError - Registration failed - permission denied",
            script_result);

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - denied", script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, UnsubscribeSuccess) {
  std::string script_result;

  std::string token1;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(false /* use_key */, &token1));
  ASSERT_TRUE(RunScript("storePushSubscription()", &script_result));
  EXPECT_EQ("ok - stored", script_result);

  // Resolves true if there was a subscription.
  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_JAVASCRIPT_API, 1);

  // Resolves false if there was no longer a subscription.
  ASSERT_TRUE(RunScript("unsubscribeStoredPushSubscription()", &script_result));
  EXPECT_EQ("unsubscribe result: false", script_result);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_JAVASCRIPT_API, 2);

  // TODO(johnme): Test that doesn't reject if there was a network error (should
  // deactivate subscription locally anyway).
  // TODO(johnme): Test that doesn't reject if there were other push service
  // errors (should deactivate subscription locally anyway).

  // Unsubscribing (with an existing reference to a PushSubscription), after
  // replacing the Service Worker, actually still works, as the Service Worker
  // registration is unchanged.
  std::string token2;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(false /* use_key */, &token2));
  EXPECT_NE(token1, token2);
  ASSERT_TRUE(RunScript("storePushSubscription()", &script_result));
  EXPECT_EQ("ok - stored", script_result);
  ASSERT_TRUE(RunScript("replaceServiceWorker()", &script_result));
  EXPECT_EQ("ok - service worker replaced", script_result);
  ASSERT_TRUE(RunScript("unsubscribeStoredPushSubscription()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_JAVASCRIPT_API, 3);

  // Unsubscribing (with an existing reference to a PushSubscription), after
  // unregistering the Service Worker, should fail.
  std::string token3;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(false /* use_key */, &token3));
  EXPECT_NE(token1, token3);
  EXPECT_NE(token2, token3);
  ASSERT_TRUE(RunScript("storePushSubscription()", &script_result));
  EXPECT_EQ("ok - stored", script_result);

  // Unregister service worker and wait for callback.
  base::RunLoop run_loop;
  push_service()->SetServiceWorkerUnregisteredCallbackForTesting(
      run_loop.QuitClosure());
  ASSERT_TRUE(RunScript("unregisterServiceWorker()", &script_result));
  EXPECT_EQ("service worker unregistration status: true", script_result);
  run_loop.Run();

  // Unregistering should have triggered an automatic unsubscribe.
  histogram_tester_.ExpectBucketCount(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_SERVICE_WORKER_UNREGISTERED, 1);
  histogram_tester_.ExpectTotalCount("PushMessaging.UnregistrationReason", 4);

  // Now manual unsubscribe should return false.
  ASSERT_TRUE(RunScript("unsubscribeStoredPushSubscription()", &script_result));
  EXPECT_EQ("unsubscribe result: false", script_result);
}

// Push subscriptions used to be non-InstanceID GCM registrations. Still need
// to be able to unsubscribe these, even though new ones are no longer created.
IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, LegacyUnsubscribeSuccess) {
  std::string script_result;

  std::string subscription_id1;
  ASSERT_NO_FATAL_FAILURE(LegacySubscribeSuccessfully(&subscription_id1));
  ASSERT_TRUE(RunScript("storePushSubscription()", &script_result));
  EXPECT_EQ("ok - stored", script_result);

  // Resolves true if there was a subscription.
  gcm_service_->AddExpectedUnregisterResponse(gcm::GCMClient::SUCCESS);
  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_JAVASCRIPT_API, 1);

  // Resolves false if there was no longer a subscription.
  ASSERT_TRUE(RunScript("unsubscribeStoredPushSubscription()", &script_result));
  EXPECT_EQ("unsubscribe result: false", script_result);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_JAVASCRIPT_API, 2);

  // Doesn't reject if there was a network error (deactivates subscription
  // locally anyway).
  std::string subscription_id2;
  ASSERT_NO_FATAL_FAILURE(LegacySubscribeSuccessfully(&subscription_id2));
  EXPECT_NE(subscription_id1, subscription_id2);
  gcm_service_->AddExpectedUnregisterResponse(gcm::GCMClient::NETWORK_ERROR);
  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_JAVASCRIPT_API, 3);
  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("false - not subscribed", script_result);

  // Doesn't reject if there were other push service errors (deactivates
  // subscription locally anyway).
  std::string subscription_id3;
  ASSERT_NO_FATAL_FAILURE(LegacySubscribeSuccessfully(&subscription_id3));
  EXPECT_NE(subscription_id1, subscription_id3);
  EXPECT_NE(subscription_id2, subscription_id3);
  gcm_service_->AddExpectedUnregisterResponse(
      gcm::GCMClient::INVALID_PARAMETER);
  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_JAVASCRIPT_API, 4);

  // Unsubscribing (with an existing reference to a PushSubscription), after
  // replacing the Service Worker, actually still works, as the Service Worker
  // registration is unchanged.
  std::string subscription_id4;
  ASSERT_NO_FATAL_FAILURE(LegacySubscribeSuccessfully(&subscription_id4));
  EXPECT_NE(subscription_id1, subscription_id4);
  EXPECT_NE(subscription_id2, subscription_id4);
  EXPECT_NE(subscription_id3, subscription_id4);
  ASSERT_TRUE(RunScript("storePushSubscription()", &script_result));
  EXPECT_EQ("ok - stored", script_result);
  ASSERT_TRUE(RunScript("replaceServiceWorker()", &script_result));
  EXPECT_EQ("ok - service worker replaced", script_result);
  ASSERT_TRUE(RunScript("unsubscribeStoredPushSubscription()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_JAVASCRIPT_API, 5);

  // Unsubscribing (with an existing reference to a PushSubscription), after
  // unregistering the Service Worker, should fail.
  std::string subscription_id5;
  ASSERT_NO_FATAL_FAILURE(LegacySubscribeSuccessfully(&subscription_id5));
  EXPECT_NE(subscription_id1, subscription_id5);
  EXPECT_NE(subscription_id2, subscription_id5);
  EXPECT_NE(subscription_id3, subscription_id5);
  EXPECT_NE(subscription_id4, subscription_id5);
  ASSERT_TRUE(RunScript("storePushSubscription()", &script_result));
  EXPECT_EQ("ok - stored", script_result);

  // Unregister service worker and wait for callback.
  base::RunLoop run_loop;
  push_service()->SetServiceWorkerUnregisteredCallbackForTesting(
      run_loop.QuitClosure());
  ASSERT_TRUE(RunScript("unregisterServiceWorker()", &script_result));
  EXPECT_EQ("service worker unregistration status: true", script_result);
  run_loop.Run();

  // Unregistering should have triggered an automatic unsubscribe.
  histogram_tester_.ExpectBucketCount(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_SERVICE_WORKER_UNREGISTERED, 1);
  histogram_tester_.ExpectTotalCount("PushMessaging.UnregistrationReason", 6);

  // Now manual unsubscribe should return false.
  ASSERT_TRUE(RunScript("unsubscribeStoredPushSubscription()", &script_result));
  EXPECT_EQ("unsubscribe result: false", script_result);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, UnsubscribeOffline) {
  std::string script_result;

  EXPECT_NE(push_service(), GetAppHandler());

  std::string token;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(true /* use_key */, &token));

  gcm_service_->set_offline(true);

  // Should quickly resolve true after deleting local state (rather than waiting
  // until unsubscribing over the network exceeds the maximum backoff duration).
  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_JAVASCRIPT_API, 1);

  // Since the service is offline, the network request to GCM is still being
  // retried, so the app handler shouldn't have been unregistered yet.
  EXPECT_EQ(push_service(), GetAppHandler());
  // But restarting the push service will unregister the app handler, since the
  // subscription is no longer stored in the PushMessagingAppIdentifier map.
  ASSERT_NO_FATAL_FAILURE(RestartPushService());
  EXPECT_NE(push_service(), GetAppHandler());
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       UnregisteringServiceWorkerUnsubscribes) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());

  LoadTestPage();  // Reload to become controlled.
  ASSERT_TRUE(RunScript("isControlled()", &script_result));
  ASSERT_EQ("true - is controlled", script_result);

  // Unregister the worker, and wait for callback to complete.
  base::RunLoop run_loop;
  push_service()->SetServiceWorkerUnregisteredCallbackForTesting(
      run_loop.QuitClosure());
  ASSERT_TRUE(RunScript("unregisterServiceWorker()", &script_result));
  ASSERT_EQ("service worker unregistration status: true", script_result);
  run_loop.Run();

  // This should have unregistered the push subscription.
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_SERVICE_WORKER_UNREGISTERED, 1);

  // We should not be able to look up the app id.
  GURL origin = https_server()->GetURL("/").GetOrigin();
  PushMessagingAppIdentifier app_identifier =
      PushMessagingAppIdentifier::FindByServiceWorker(
          GetBrowser()->profile(), origin,
          0LL /* service_worker_registration_id */);
  EXPECT_TRUE(app_identifier.is_null());
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       InvalidGetSubscriptionUnsubscribes) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());

  GURL origin = https_server()->GetURL("/").GetOrigin();
  PushMessagingAppIdentifier app_identifier1 =
      PushMessagingAppIdentifier::FindByServiceWorker(
          GetBrowser()->profile(), origin,
          0LL /* service_worker_registration_id */);
  ASSERT_FALSE(app_identifier1.is_null());

  ASSERT_NO_FATAL_FAILURE(
      DeleteInstanceIDAsIfGCMStoreReset(app_identifier1.app_id()));

  // Push messaging should not yet be aware of the InstanceID being deleted.
  histogram_tester_.ExpectTotalCount("PushMessaging.UnregistrationReason", 0);
  // We should still be able to look up the app id.
  PushMessagingAppIdentifier app_identifier2 =
      PushMessagingAppIdentifier::FindByServiceWorker(
          GetBrowser()->profile(), origin,
          0LL /* service_worker_registration_id */);
  EXPECT_FALSE(app_identifier2.is_null());
  EXPECT_EQ(app_identifier1.app_id(), app_identifier2.app_id());

  // Now call PushManager.getSubscription(). It should return null.
  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("false - not subscribed", script_result);

  // This should have unsubscribed the push subscription.
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_GET_SUBSCRIPTION_STORAGE_CORRUPT, 1);
  // We should no longer be able to look up the app id.
  PushMessagingAppIdentifier app_identifier3 =
      PushMessagingAppIdentifier::FindByServiceWorker(
          GetBrowser()->profile(), origin,
          0LL /* service_worker_registration_id */);
  EXPECT_TRUE(app_identifier3.is_null());
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, InvalidSubscribeUnsubscribes) {
  std::string script_result;

  std::string token1;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(true /* use_key */, &token1));

  GURL origin = https_server()->GetURL("/").GetOrigin();
  PushMessagingAppIdentifier app_identifier1 =
      PushMessagingAppIdentifier::FindByServiceWorker(
          GetBrowser()->profile(), origin,
          0LL /* service_worker_registration_id */);
  ASSERT_FALSE(app_identifier1.is_null());

  ASSERT_NO_FATAL_FAILURE(
      DeleteInstanceIDAsIfGCMStoreReset(app_identifier1.app_id()));

  // Push messaging should not yet be aware of the InstanceID being deleted.
  histogram_tester_.ExpectTotalCount("PushMessaging.UnregistrationReason", 0);
  // We should still be able to look up the app id.
  PushMessagingAppIdentifier app_identifier2 =
      PushMessagingAppIdentifier::FindByServiceWorker(
          GetBrowser()->profile(), origin,
          0LL /* service_worker_registration_id */);
  EXPECT_FALSE(app_identifier2.is_null());
  EXPECT_EQ(app_identifier1.app_id(), app_identifier2.app_id());

  // Now call PushManager.subscribe() again. It should succeed, but with a
  // *different* token, indicating that it unsubscribed and re-subscribed.
  std::string token2;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(true /* use_key */, &token2));
  EXPECT_NE(token1, token2);

  // This should have unsubscribed the original push subscription.
  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_SUBSCRIBE_STORAGE_CORRUPT, 1);
  // Looking up the app id should return a different id.
  PushMessagingAppIdentifier app_identifier3 =
      PushMessagingAppIdentifier::FindByServiceWorker(
          GetBrowser()->profile(), origin,
          0LL /* service_worker_registration_id */);
  EXPECT_FALSE(app_identifier3.is_null());
  EXPECT_NE(app_identifier2.app_id(), app_identifier3.app_id());
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       GlobalResetPushPermissionUnsubscribes) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("true - subscribed", script_result);

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - granted", script_result);

  scoped_refptr<content::MessageLoopRunner> message_loop_runner =
      new content::MessageLoopRunner;
  push_service()->SetContentSettingChangedCallbackForTesting(
      message_loop_runner->QuitClosure());

  HostContentSettingsMapFactory::GetForProfile(GetBrowser()->profile())
      ->ClearSettingsForOneType(CONTENT_SETTINGS_TYPE_NOTIFICATIONS);

  message_loop_runner->Run();

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - prompt", script_result);

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("false - not subscribed", script_result);

  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_PERMISSION_REVOKED, 1);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       LocalResetPushPermissionUnsubscribes) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("true - subscribed", script_result);

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - granted", script_result);

  scoped_refptr<content::MessageLoopRunner> message_loop_runner =
      new content::MessageLoopRunner;
  push_service()->SetContentSettingChangedCallbackForTesting(
      message_loop_runner->QuitClosure());

  GURL origin = https_server()->GetURL("/").GetOrigin();
  HostContentSettingsMapFactory::GetForProfile(GetBrowser()->profile())
      ->SetContentSettingDefaultScope(origin, origin,
                                      CONTENT_SETTINGS_TYPE_NOTIFICATIONS,
                                      std::string(), CONTENT_SETTING_DEFAULT);

  message_loop_runner->Run();

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - prompt", script_result);

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("false - not subscribed", script_result);

  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_PERMISSION_REVOKED, 1);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       DenyPushPermissionUnsubscribes) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("true - subscribed", script_result);

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - granted", script_result);

  scoped_refptr<content::MessageLoopRunner> message_loop_runner =
      new content::MessageLoopRunner;
  push_service()->SetContentSettingChangedCallbackForTesting(
      message_loop_runner->QuitClosure());

  GURL origin = https_server()->GetURL("/").GetOrigin();
  HostContentSettingsMapFactory::GetForProfile(GetBrowser()->profile())
      ->SetContentSettingDefaultScope(origin, origin,
                                      CONTENT_SETTINGS_TYPE_NOTIFICATIONS,
                                      std::string(), CONTENT_SETTING_BLOCK);

  message_loop_runner->Run();

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - denied", script_result);

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("false - not subscribed", script_result);

  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_PERMISSION_REVOKED, 1);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       GlobalResetNotificationsPermissionUnsubscribes) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("true - subscribed", script_result);

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - granted", script_result);

  scoped_refptr<content::MessageLoopRunner> message_loop_runner =
      new content::MessageLoopRunner;
  push_service()->SetContentSettingChangedCallbackForTesting(
      message_loop_runner->QuitClosure());

  HostContentSettingsMapFactory::GetForProfile(GetBrowser()->profile())
      ->ClearSettingsForOneType(CONTENT_SETTINGS_TYPE_NOTIFICATIONS);

  message_loop_runner->Run();

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - prompt", script_result);

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("false - not subscribed", script_result);

  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_PERMISSION_REVOKED, 1);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       LocalResetNotificationsPermissionUnsubscribes) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("true - subscribed", script_result);

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - granted", script_result);

  scoped_refptr<content::MessageLoopRunner> message_loop_runner =
      new content::MessageLoopRunner;
  push_service()->SetContentSettingChangedCallbackForTesting(
      message_loop_runner->QuitClosure());

  GURL origin = https_server()->GetURL("/").GetOrigin();
  HostContentSettingsMapFactory::GetForProfile(GetBrowser()->profile())
      ->SetContentSettingDefaultScope(origin, GURL(),
                                      CONTENT_SETTINGS_TYPE_NOTIFICATIONS,
                                      std::string(), CONTENT_SETTING_DEFAULT);

  message_loop_runner->Run();

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - prompt", script_result);

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("false - not subscribed", script_result);

  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_PERMISSION_REVOKED, 1);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       DenyNotificationsPermissionUnsubscribes) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("true - subscribed", script_result);

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - granted", script_result);

  scoped_refptr<content::MessageLoopRunner> message_loop_runner =
      new content::MessageLoopRunner;
  push_service()->SetContentSettingChangedCallbackForTesting(
      message_loop_runner->QuitClosure());

  GURL origin = https_server()->GetURL("/").GetOrigin();
  HostContentSettingsMapFactory::GetForProfile(GetBrowser()->profile())
      ->SetContentSettingDefaultScope(origin, GURL(),
                                      CONTENT_SETTINGS_TYPE_NOTIFICATIONS,
                                      std::string(), CONTENT_SETTING_BLOCK);

  message_loop_runner->Run();

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - denied", script_result);

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("false - not subscribed", script_result);

  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_PERMISSION_REVOKED, 1);
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       GrantAlreadyGrantedPermissionDoesNotUnsubscribe) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("true - subscribed", script_result);

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - granted", script_result);

  scoped_refptr<content::MessageLoopRunner> message_loop_runner =
      new content::MessageLoopRunner;
  push_service()->SetContentSettingChangedCallbackForTesting(
      base::BarrierClosure(1, message_loop_runner->QuitClosure()));

  GURL origin = https_server()->GetURL("/").GetOrigin();
  HostContentSettingsMapFactory::GetForProfile(GetBrowser()->profile())
      ->SetContentSettingDefaultScope(origin, GURL(),
                                      CONTENT_SETTINGS_TYPE_NOTIFICATIONS,
                                      std::string(), CONTENT_SETTING_ALLOW);

  message_loop_runner->Run();

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - granted", script_result);

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("true - subscribed", script_result);

  histogram_tester_.ExpectTotalCount("PushMessaging.UnregistrationReason", 0);
}

// This test is testing some non-trivial content settings rules and make sure
// that they are respected with regards to automatic unsubscription. In other
// words, it checks that the push service does not end up unsubscribing origins
// that have push permission with some non-common rules.
IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       AutomaticUnsubscriptionFollowsContentSettingRules) {
  std::string script_result;

  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("true - subscribed", script_result);

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - granted", script_result);

  scoped_refptr<content::MessageLoopRunner> message_loop_runner =
      new content::MessageLoopRunner;
  push_service()->SetContentSettingChangedCallbackForTesting(
      base::BarrierClosure(2, message_loop_runner->QuitClosure()));

  GURL origin = https_server()->GetURL("/").GetOrigin();
  HostContentSettingsMapFactory::GetForProfile(GetBrowser()->profile())
      ->SetDefaultContentSetting(CONTENT_SETTINGS_TYPE_NOTIFICATIONS,
                                 CONTENT_SETTING_ALLOW);
  HostContentSettingsMapFactory::GetForProfile(GetBrowser()->profile())
      ->SetContentSettingDefaultScope(origin, GURL(),
                                      CONTENT_SETTINGS_TYPE_NOTIFICATIONS,
                                      std::string(), CONTENT_SETTING_DEFAULT);

  message_loop_runner->Run();

  // The two first rules should give |origin| the permission to use Push even
  // if the rules it used to have have been reset.
  // The Push service should not unsubscribe |origin| because at no point it was
  // left without permission to use Push.

  ASSERT_TRUE(RunScript("permissionState()", &script_result));
  EXPECT_EQ("permission status - granted", script_result);

  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  EXPECT_EQ("true - subscribed", script_result);

  histogram_tester_.ExpectTotalCount("PushMessaging.UnregistrationReason", 0);
}

// Checks automatically unsubscribing due to a revoked permission after
// previously clearing site data, under legacy conditions (ie. when
// unregistering a worker did not unsubscribe from push.)
IN_PROC_BROWSER_TEST_F(
    PushMessagingBrowserTest,
    ResetPushPermissionAfterClearingSiteDataUnderLegacyConditions) {
  std::string app_id;
  ASSERT_NO_FATAL_FAILURE(SetupOrphanedPushSubscription(&app_id));

  // Simulate a user clearing site data (including Service Workers, crucially).
  content::BrowsingDataRemover* remover =
      content::BrowserContext::GetBrowsingDataRemover(GetBrowser()->profile());
  content::BrowsingDataRemoverCompletionObserver observer(remover);
  remover->RemoveAndReply(
      base::Time(), base::Time::Max(),
      ChromeBrowsingDataRemoverDelegate::DATA_TYPE_SITE_DATA,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB, &observer);
  observer.BlockUntilCompletion();

  base::RunLoop run_loop;
  push_service()->SetContentSettingChangedCallbackForTesting(
      run_loop.QuitClosure());
  // This shouldn't (asynchronously) cause a DCHECK.
  // TODO(johnme): Get this test running on Android with legacy GCM
  // registrations, which have a different codepath due to sender_id being
  // required for unsubscribing there.
  HostContentSettingsMapFactory::GetForProfile(GetBrowser()->profile())
      ->ClearSettingsForOneType(CONTENT_SETTINGS_TYPE_NOTIFICATIONS);

  run_loop.Run();

  // |app_identifier| should no longer be stored in prefs.
  PushMessagingAppIdentifier stored_app_identifier =
      PushMessagingAppIdentifier::FindByAppId(GetBrowser()->profile(), app_id);
  EXPECT_TRUE(stored_app_identifier.is_null());

  histogram_tester_.ExpectUniqueSample(
      "PushMessaging.UnregistrationReason",
      content::PUSH_UNREGISTRATION_REASON_PERMISSION_REVOKED, 1);

  base::RunLoop().RunUntilIdle();

  // Revoked permission should trigger an automatic unsubscription attempt.
  EXPECT_EQ(app_id, gcm_driver_->last_deletetoken_app_id());
}

IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest, EncryptionKeyUniqueness) {
  std::string token1;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(false /* use_key */, &token1));

  std::string first_public_key;
  ASSERT_TRUE(RunScript("GetP256dh()", &first_public_key));
  EXPECT_GE(first_public_key.size(), 32u);

  std::string script_result;
  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);

  std::string token2;
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully(true /* use_key */, &token2));
  EXPECT_NE(token1, token2);

  std::string second_public_key;
  ASSERT_TRUE(RunScript("GetP256dh()", &second_public_key));
  EXPECT_GE(second_public_key.size(), 32u);

  EXPECT_NE(first_public_key, second_public_key);
}

class PushMessagingIncognitoBrowserTest : public PushMessagingBrowserTest {
 public:
  ~PushMessagingIncognitoBrowserTest() override {}

  // PushMessagingBrowserTest:
  void SetUpOnMainThread() override {
    incognito_browser_ = CreateIncognitoBrowser();
    PushMessagingBrowserTest::SetUpOnMainThread();
  }

  Browser* GetBrowser() const override { return incognito_browser_; }

 private:
  Browser* incognito_browser_ = nullptr;
};

// Regression test for https://crbug.com/476474
IN_PROC_BROWSER_TEST_F(PushMessagingIncognitoBrowserTest,
                       IncognitoGetSubscriptionDoesNotHang) {
  ASSERT_TRUE(GetBrowser()->profile()->IsOffTheRecord());

  std::string script_result;

  ASSERT_TRUE(RunScript("registerServiceWorker()", &script_result));
  ASSERT_EQ("ok - service worker registered", script_result);

  // In Incognito mode the promise returned by getSubscription should not hang,
  // it should just fulfill with null.
  ASSERT_TRUE(RunScript("hasSubscription()", &script_result));
  ASSERT_EQ("false - not subscribed", script_result);
}

// None of the following should matter on ChromeOS: crbug.com/527045
#if BUILDFLAG(ENABLE_BACKGROUND) && !defined(OS_CHROMEOS)
// Push background mode is disabled by default.
IN_PROC_BROWSER_TEST_F(PushMessagingBrowserTest,
                       BackgroundModeDisabledByDefault) {
  // Initially background mode is inactive.
  BackgroundModeManager* background_mode_manager =
      g_browser_process->background_mode_manager();
  ASSERT_FALSE(background_mode_manager->IsBackgroundModeActive());

  // Once there is a push subscription background mode is still inactive.
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  ASSERT_FALSE(background_mode_manager->IsBackgroundModeActive());

  // After dropping the last subscription it is still inactive.
  std::string script_result;
  base::RunLoop run_loop;
  push_service()->SetUnsubscribeCallbackForTesting(run_loop.QuitClosure());
  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
  // Background mode is only guaranteed to have updated once the unsubscribe
  // callback for testing has been run (PushSubscription.unsubscribe() usually
  // resolves before that, in order to avoid blocking on network retries etc).
  run_loop.Run();
  ASSERT_FALSE(background_mode_manager->IsBackgroundModeActive());
}

class PushMessagingBackgroundModeEnabledBrowserTest
    : public PushMessagingBrowserTest {
 public:
  ~PushMessagingBackgroundModeEnabledBrowserTest() override {}

  // PushMessagingBrowserTest:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kEnablePushApiBackgroundMode);
    PushMessagingBrowserTest::SetUpCommandLine(command_line);
  }
};

// In this test the command line enables push background mode.
IN_PROC_BROWSER_TEST_F(PushMessagingBackgroundModeEnabledBrowserTest,
                       BackgroundModeEnabledWithCommandLine) {
  // Initially background mode is inactive.
  BackgroundModeManager* background_mode_manager =
      g_browser_process->background_mode_manager();
  ASSERT_FALSE(background_mode_manager->IsBackgroundModeActive());

  // Once there is a push subscription background mode is active.
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  ASSERT_TRUE(background_mode_manager->IsBackgroundModeActive());

  // Dropping the last subscription deactivates background mode.
  std::string script_result;
  base::RunLoop run_loop;
  push_service()->SetUnsubscribeCallbackForTesting(run_loop.QuitClosure());
  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
  // Background mode is only guaranteed to have updated once the unsubscribe
  // callback for testing has been run (PushSubscription.unsubscribe() usually
  // resolves before that, in order to avoid blocking on network retries etc).
  run_loop.Run();
  ASSERT_FALSE(background_mode_manager->IsBackgroundModeActive());
}

class PushMessagingBackgroundModeDisabledBrowserTest
    : public PushMessagingBrowserTest {
 public:
  ~PushMessagingBackgroundModeDisabledBrowserTest() override {}

  // PushMessagingBrowserTest:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kDisablePushApiBackgroundMode);
    PushMessagingBrowserTest::SetUpCommandLine(command_line);
  }
};

// In this test the command line disables push background mode.
IN_PROC_BROWSER_TEST_F(PushMessagingBackgroundModeDisabledBrowserTest,
                       BackgroundModeDisabledWithCommandLine) {
  // Initially background mode is inactive.
  BackgroundModeManager* background_mode_manager =
      g_browser_process->background_mode_manager();
  ASSERT_FALSE(background_mode_manager->IsBackgroundModeActive());

  // Once there is a push subscription background mode is still inactive.
  ASSERT_NO_FATAL_FAILURE(SubscribeSuccessfully());
  ASSERT_FALSE(background_mode_manager->IsBackgroundModeActive());

  // After dropping the last subscription background mode is still inactive.
  std::string script_result;
  base::RunLoop run_loop;
  push_service()->SetUnsubscribeCallbackForTesting(run_loop.QuitClosure());
  ASSERT_TRUE(RunScript("unsubscribePush()", &script_result));
  EXPECT_EQ("unsubscribe result: true", script_result);
  // Background mode is only guaranteed to have updated once the unsubscribe
  // callback for testing has been run (PushSubscription.unsubscribe() usually
  // resolves before that, in order to avoid blocking on network retries etc).
  run_loop.Run();
  ASSERT_FALSE(background_mode_manager->IsBackgroundModeActive());
}
#endif  // BUILDFLAG(ENABLE_BACKGROUND) && !defined(OS_CHROMEOS)
