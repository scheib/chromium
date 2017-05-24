// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/screens/update_screen.h"
#include "base/command_line.h"
#include "base/message_loop/message_loop.h"
#include "base/test/scoped_mock_time_message_loop_task_runner.h"
#include "chrome/browser/chromeos/login/screens/mock_base_screen_delegate.h"
#include "chrome/browser/chromeos/login/screens/mock_error_screen.h"
#include "chrome/browser/chromeos/login/screens/mock_update_screen.h"
#include "chrome/browser/chromeos/login/startup_utils.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "chrome/test/base/scoped_testing_local_state.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/fake_update_engine_client.h"
#include "chromeos/dbus/update_engine_client.h"
#include "chromeos/network/network_handler.h"
#include "chromeos/network/portal_detector/mock_network_portal_detector.h"
#include "chromeos/network/portal_detector/network_portal_detector.h"
#include "components/pairing/fake_host_pairing_controller.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Return;

namespace chromeos {

class UpdateScreenUnitTest : public testing::Test {
 public:
  UpdateScreenUnitTest()
      : fake_controller_(""),
        local_state_(TestingBrowserProcess::GetGlobal()) {}

  // Fast-forwards time by the specified amount.
  void FastForwardTime(base::TimeDelta time) {
    base::Time last = StartupUtils::GetTimeOfLastUpdateCheckWithoutUpdate();
    ASSERT_FALSE(last.is_null());
    base::Time modified_last = last - time;
    StartupUtils::SaveTimeOfLastUpdateCheckWithoutUpdate(modified_last);
  }

  // Simulates an update being available (or not).
  // The parameter "update_screen" points to the currently active UpdateScreen.
  // The parameter "available" indicates whether an update is available.
  // The parameter "critical" indicates whether that update is critical.
  void SimulateUpdateAvailable(
      const std::unique_ptr<UpdateScreen>& update_screen,
      bool available,
      bool critical) {
    update_engine_status_.status =
        UpdateEngineClient::UPDATE_STATUS_CHECKING_FOR_UPDATE;
    fake_update_engine_client_->NotifyObserversThatStatusChanged(
        update_engine_status_);
    if (critical) {
      ASSERT_TRUE(available) << "Does not make sense for an update to be "
                                "critical if one is not even available.";
      update_screen->is_ignore_update_deadlines_ = true;
    }
    update_engine_status_.status =
        available ? UpdateEngineClient::UPDATE_STATUS_UPDATE_AVAILABLE
                  : UpdateEngineClient::UPDATE_STATUS_IDLE;
    fake_update_engine_client_->NotifyObserversThatStatusChanged(
        update_engine_status_);
  }

  // testing::Test:
  void SetUp() override {
    // Configure the browser to use Hands-Off Enrollment.
    base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
        switches::kEnterpriseEnableZeroTouchEnrollment, "hands-off");

    // Initialize objects needed by UpdateScreen
    fake_update_engine_client_ = new FakeUpdateEngineClient();
    DBusThreadManager::GetSetterForTesting()->SetUpdateEngineClient(
        std::unique_ptr<UpdateEngineClient>(fake_update_engine_client_));
    NetworkHandler::Initialize();
    mock_network_portal_detector_ = new MockNetworkPortalDetector();
    network_portal_detector::SetNetworkPortalDetector(
        mock_network_portal_detector_);
    mock_error_screen_.reset(
        new MockErrorScreen(&mock_base_screen_delegate_, &mock_error_view_));

    // Ensure proper behavior of UpdateScreen's supporting objects.
    EXPECT_CALL(*mock_network_portal_detector_, IsEnabled())
        .Times(AnyNumber())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(mock_base_screen_delegate_, GetErrorScreen())
        .Times(AnyNumber())
        .WillRepeatedly(Return(mock_error_screen_.get()));

    // Later verifies that UpdateScreen successfully exits both times.
    EXPECT_CALL(mock_base_screen_delegate_,
                OnExit(_, ScreenExitCode::UPDATE_NOUPDATE, _))
        .Times(2);
  }

  void TearDown() override {
    TestingBrowserProcess::GetGlobal()->SetShuttingDown(true);
    first_update_screen_.reset();
    second_update_screen_.reset();
    mock_error_screen_.reset();
    network_portal_detector::Shutdown();
    NetworkHandler::Shutdown();
    DBusThreadManager::Shutdown();
  }

 protected:
  // A pointer to the UpdateScreen that shows up during the first OOBE.
  std::unique_ptr<UpdateScreen> first_update_screen_;

  // A pointer to the UpdateScreen which shows up during the second OOBE.
  // This test verifies proper behavior if the device is restarted before
  // OOBE is complete, which is why there is a second OOBE.
  std::unique_ptr<UpdateScreen> second_update_screen_;

  // Accessory objects needed by UpdateScreen.
  MockBaseScreenDelegate mock_base_screen_delegate_;
  MockUpdateView mock_view_;
  MockNetworkErrorView mock_error_view_;
  UpdateEngineClient::Status update_engine_status_;
  pairing_chromeos::FakeHostPairingController fake_controller_;
  std::unique_ptr<MockErrorScreen> mock_error_screen_;
  MockNetworkPortalDetector* mock_network_portal_detector_;
  FakeUpdateEngineClient* fake_update_engine_client_;

 private:
  // Test versions of core browser infrastructure.
  content::TestBrowserThreadBundle threads_;
  ScopedTestingLocalState local_state_;

  DISALLOW_COPY_AND_ASSIGN(UpdateScreenUnitTest);
};

// Test Scenario Description:
// In this description, "will" refers to an external event, and "should" refers
// to the expected behavior of the DUT in response to external events.
//
// The DUT boots up and starts OOBE. Since it is a Hands-Off device, it
// proceeds through OOBE automatically. When it hits the UpdateScreen,
// it checks for updates. It will find that there is indeed an update
// available, will then install it. After installing the update, it should
// continue with Hands-Off OOBE. Then, before OOBE is complete, something
// (could be user, environment, anything) will cause the DUT to reboot.
// Since OOBE is not complete, the DUT goes through OOBE again.
// When the DUT hits the UpdateScreen during this second OOBE run-through,
// it should check for updates again.
TEST_F(UpdateScreenUnitTest, ChecksForUpdateBothTimesIfFirstIsInstalled) {
  // DUT reaches UpdateScreen for the first time.
  first_update_screen_.reset(new UpdateScreen(&mock_base_screen_delegate_,
                                              &mock_view_, &fake_controller_));
  first_update_screen_->StartNetworkCheck();

  // Verify that the DUT checks for an update.
  EXPECT_EQ(fake_update_engine_client_->request_update_check_call_count(), 1);

  // An update is available.
  SimulateUpdateAvailable(first_update_screen_, true /* available */,
                          false /* critical */);

  // DUT reboots...
  // After rebooting, the DUT reaches UpdateScreen for the second time.
  second_update_screen_.reset(new UpdateScreen(&mock_base_screen_delegate_,
                                               &mock_view_, &fake_controller_));
  second_update_screen_->StartNetworkCheck();

  // Verify that the DUT checks for updates again.
  EXPECT_EQ(fake_update_engine_client_->request_update_check_call_count(), 2);

  // No updates available this time.
  SimulateUpdateAvailable(second_update_screen_, false /* available */,
                          false /* critical */);
}

// Test Scenario Description:
// In this description, "will" refers to an external event, and "should" refers
// to the expected behavior of the DUT in response to external events.
//
// The DUT boots up and starts OOBE. Since it is a Hands-Off device, it
// proceeds through OOBE automatically. When it hits the UpdateScreen,
// it checks for updates. It will find that there are no updates
// available, and it should leave the UpdateScreen without installing any
// updates. It continues with OOBE. Then, before OOBE is complete, something
// (could be user, environment, anything) will cause the DUT to reboot.
// Since OOBE is not complete, the DUT goes through OOBE again.
// When the DUT hits the UpdateScreen during this second OOBE run-through,
// more than one hour will have passed since the previous update check.
// Therefore, the DUT should check for updates again.
TEST_F(UpdateScreenUnitTest, ChecksForUpdateBothTimesIfEnoughTimePasses) {
  // DUT reaches UpdateScreen for the first time.
  first_update_screen_.reset(new UpdateScreen(&mock_base_screen_delegate_,
                                              &mock_view_, &fake_controller_));
  first_update_screen_->StartNetworkCheck();

  // Verify that the DUT checks for updates.
  EXPECT_EQ(fake_update_engine_client_->request_update_check_call_count(), 1);

  // No updates are available.
  SimulateUpdateAvailable(first_update_screen_, false /* available */,
                          false /* critical */);

  // Fast-forward time by one hour.
  FastForwardTime(base::TimeDelta::FromHours(1));

  // DUT reboots...
  // After rebooting, the DUT reaches UpdateScreen for the second time.
  second_update_screen_.reset(new UpdateScreen(&mock_base_screen_delegate_,
                                               &mock_view_, &fake_controller_));
  second_update_screen_->StartNetworkCheck();

  // Verify that the DUT checks for updates again.
  EXPECT_EQ(fake_update_engine_client_->request_update_check_call_count(), 2);

  // No updates available this time either.
  SimulateUpdateAvailable(second_update_screen_, false /* available */,
                          false /* critical */);
}

// Test Scenario Description:
// In this description, "will" refers to an external event, and "should" refers
// to the expected behavior of the DUT in response to external events.
//
// The DUT boots up and starts OOBE. Since it is a Hands-Off device, it
// proceeds through OOBE automatically. When it hits the UpdateScreen,
// it checks for updates. It will find that there are no updates
// available, and it should leave the UpdateScreen without installing any
// updates. It continues with OOBE. Then, before OOBE is complete, something
// (could be user, environment, anything) will cause the DUT to reboot.
// Since OOBE is not complete, the DUT goes through OOBE again.
// When the DUT hits the UpdateScreen during this second OOBE run-through,
// less than one hour will have passed since the previous update check.
// Therefore, the DUT should not check for updates again.
TEST_F(UpdateScreenUnitTest, ChecksForUpdateOnceButNotAgainIfTooSoon) {
  // DUT reaches UpdateScreen for the first time.
  first_update_screen_.reset(new UpdateScreen(&mock_base_screen_delegate_,
                                              &mock_view_, &fake_controller_));
  first_update_screen_->StartNetworkCheck();

  // Verify that the DUT checks for updates.
  EXPECT_EQ(fake_update_engine_client_->request_update_check_call_count(), 1);

  // No update available.
  SimulateUpdateAvailable(first_update_screen_, false /* available */,
                          false /* critical */);

  // DUT reboots...
  // After rebooting, the DUT reaches UpdateScreen for the second time.
  second_update_screen_.reset(new UpdateScreen(&mock_base_screen_delegate_,
                                               &mock_view_, &fake_controller_));
  second_update_screen_->StartNetworkCheck();

  // Verify that the DUT did not check for updates again.
  EXPECT_EQ(fake_update_engine_client_->request_update_check_call_count(), 1);

  // No update available this time either.
  SimulateUpdateAvailable(second_update_screen_, false /* available */,
                          false /* critical */);
}

// Test Scenario Description:
// In this description, "will" refers to an external event, and "should" refers
// to the expected behavior of the DUT in response to external events.
//
// The DUT boots up and starts OOBE. Since it is a Hands-Off device, it
// proceeds through OOBE automatically. When it hits the UpdateScreen,
// it checks for updates. It will find that a critical update is available.
// The DUT installs the update, and because the update is critical, it reboots.
// Since OOBE is not complete, the DUT goes through OOBE again after reboot.
// When the DUT hits the UpdateScreen during this second OOBE run-through,
// it should check for updates again.
TEST_F(UpdateScreenUnitTest, ChecksForUpdateBothTimesIfCriticalUpdate) {
  // DUT reaches UpdateScreen for the first time.
  first_update_screen_.reset(new UpdateScreen(&mock_base_screen_delegate_,
                                              &mock_view_, &fake_controller_));
  first_update_screen_->StartNetworkCheck();

  // Verify that the DUT checks for updates.
  EXPECT_EQ(fake_update_engine_client_->request_update_check_call_count(), 1);

  // An update is available, and it's critical!
  SimulateUpdateAvailable(first_update_screen_, true /* available */,
                          true /* critical */);

  // DUT reboots...
  // After rebooting, the DUT reaches UpdateScreen for the second time.
  second_update_screen_.reset(new UpdateScreen(&mock_base_screen_delegate_,
                                               &mock_view_, &fake_controller_));
  second_update_screen_->StartNetworkCheck();

  // Verify that the DUT checks for updates again.
  EXPECT_EQ(fake_update_engine_client_->request_update_check_call_count(), 2);

  // No update available this time.
  SimulateUpdateAvailable(second_update_screen_, false /* available */,
                          false /* critical */);
}

}  // namespace chromeos
