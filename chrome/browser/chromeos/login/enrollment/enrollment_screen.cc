// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/enrollment/enrollment_screen.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/timer/elapsed_timer.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/chromeos/login/enrollment/enrollment_uma.h"
#include "chrome/browser/chromeos/login/screen_manager.h"
#include "chrome/browser/chromeos/login/screens/base_screen_delegate.h"
#include "chrome/browser/chromeos/login/startup_utils.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/policy/enrollment_status_chromeos.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chromeos/dbus/cryptohome_client.h"
#include "chromeos/dbus/dbus_method_call_status.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "components/pairing/controller_pairing_controller.h"
#include "components/policy/core/common/cloud/cloud_policy_constants.h"
#include "google_apis/gaia/gaia_auth_util.h"

using namespace pairing_chromeos;
using policy::EnrollmentConfig;

// Do not change the UMA histogram parameters without renaming the histograms!
#define UMA_ENROLLMENT_TIME(histogram_name, elapsed_timer) \
  do {                                                     \
    UMA_HISTOGRAM_CUSTOM_TIMES(                            \
      (histogram_name),                                    \
      (elapsed_timer)->Elapsed(),                          \
      base::TimeDelta::FromMilliseconds(100) /* min */,    \
      base::TimeDelta::FromMinutes(15) /* max */,          \
      100 /* bucket_count */);                             \
  } while (0)

namespace {

const char * const kMetricEnrollmentTimeCancel =
    "Enterprise.EnrollmentTime.Cancel";
const char * const kMetricEnrollmentTimeFailure =
    "Enterprise.EnrollmentTime.Failure";
const char * const kMetricEnrollmentTimeSuccess =
    "Enterprise.EnrollmentTime.Success";

// Retry policy constants.
constexpr int kInitialDelayMS = 4 * 1000;  // 4 seconds
constexpr double kMultiplyFactor = 1.5;
constexpr double kJitterFactor = 0.1;           // +/- 10% jitter
constexpr int64_t kMaxDelayMS = 8 * 60 * 1000;  // 8 minutes

}  // namespace

namespace chromeos {

// static
EnrollmentScreen* EnrollmentScreen::Get(ScreenManager* manager) {
  return static_cast<EnrollmentScreen*>(
      manager->GetScreen(OobeScreen::SCREEN_OOBE_ENROLLMENT));
}

EnrollmentScreen::EnrollmentScreen(BaseScreenDelegate* base_screen_delegate,
                                   EnrollmentScreenView* view)
    : BaseScreen(base_screen_delegate, OobeScreen::SCREEN_OOBE_ENROLLMENT),
      view_(view),
      weak_ptr_factory_(this) {
  retry_policy_.num_errors_to_ignore = 0;
  retry_policy_.initial_delay_ms = kInitialDelayMS;
  retry_policy_.multiply_factor = kMultiplyFactor;
  retry_policy_.jitter_factor = kJitterFactor;
  retry_policy_.maximum_backoff_ms = kMaxDelayMS;
  retry_policy_.entry_lifetime_ms = -1;
  retry_policy_.always_use_initial_delay = true;
  retry_backoff_.reset(new net::BackoffEntry(&retry_policy_));
}

EnrollmentScreen::~EnrollmentScreen() {
  DCHECK(!enrollment_helper_ || g_browser_process->IsShuttingDown());
}

void EnrollmentScreen::SetParameters(
    const policy::EnrollmentConfig& enrollment_config,
    pairing_chromeos::ControllerPairingController* shark_controller) {
  enrollment_config_ = enrollment_config;
  switch (enrollment_config_.auth_mechanism) {
    case EnrollmentConfig::AUTH_MECHANISM_INTERACTIVE:
      current_auth_ = AUTH_OAUTH;
      last_auth_ = AUTH_OAUTH;
      break;
    case EnrollmentConfig::AUTH_MECHANISM_ATTESTATION:
      current_auth_ = AUTH_ATTESTATION;
      last_auth_ = AUTH_ATTESTATION;
      break;
    case EnrollmentConfig::AUTH_MECHANISM_BEST_AVAILABLE:
      current_auth_ = AUTH_ATTESTATION;
      last_auth_ = enrollment_config_.should_enroll_interactively()
                       ? AUTH_OAUTH
                       : AUTH_ATTESTATION;
      break;
    default:
      NOTREACHED();
      break;
  }
  shark_controller_ = shark_controller;
  SetConfig();
}

void EnrollmentScreen::SetConfig() {
  config_ = enrollment_config_;
  if (current_auth_ == AUTH_ATTESTATION) {
    config_.mode = enrollment_config_.is_attestation_forced()
                       ? policy::EnrollmentConfig::MODE_ATTESTATION_FORCED
                       : policy::EnrollmentConfig::MODE_ATTESTATION;
  }
  view_->SetParameters(this, config_);
  enrollment_helper_ = nullptr;
}

bool EnrollmentScreen::AdvanceToNextAuth() {
  if (current_auth_ != last_auth_ && current_auth_ == AUTH_ATTESTATION) {
    current_auth_ = AUTH_OAUTH;
    SetConfig();
    return true;
  }
  return false;
}

void EnrollmentScreen::CreateEnrollmentHelper() {
  if (!enrollment_helper_) {
    enrollment_helper_ = EnterpriseEnrollmentHelper::Create(
        this, this, config_, enrolling_user_domain_);
  }
}

void EnrollmentScreen::ClearAuth(const base::Closure& callback) {
  if (!enrollment_helper_) {
    callback.Run();
    return;
  }
  enrollment_helper_->ClearAuth(base::Bind(&EnrollmentScreen::OnAuthCleared,
                                           weak_ptr_factory_.GetWeakPtr(),
                                           callback));
}

void EnrollmentScreen::OnAuthCleared(const base::Closure& callback) {
  enrollment_helper_ = nullptr;
  callback.Run();
}

void EnrollmentScreen::Show() {
  UMA(policy::kMetricEnrollmentTriggered);
  switch (current_auth_) {
    case AUTH_OAUTH:
      ShowInteractiveScreen();
      break;
    case AUTH_ATTESTATION:
      AuthenticateUsingAttestation();
      break;
    default:
      NOTREACHED();
      break;
  }
}

void EnrollmentScreen::ShowInteractiveScreen() {
  ClearAuth(base::Bind(&EnrollmentScreen::ShowSigninScreen,
                       weak_ptr_factory_.GetWeakPtr()));
}

void EnrollmentScreen::Hide() {
  view_->Hide();
  weak_ptr_factory_.InvalidateWeakPtrs();
}

void EnrollmentScreen::AuthenticateUsingAttestation() {
  VLOG(1) << "Authenticating using attestation.";
  elapsed_timer_.reset(new base::ElapsedTimer());
  view_->Show();
  CreateEnrollmentHelper();
  enrollment_helper_->EnrollUsingAttestation();
}

void EnrollmentScreen::OnLoginDone(const std::string& user,
                                   const std::string& auth_code) {
  LOG_IF(ERROR, auth_code.empty()) << "Auth code is empty.";
  elapsed_timer_.reset(new base::ElapsedTimer());
  enrolling_user_domain_ = gaia::ExtractDomainName(user);
  UMA(enrollment_failed_once_ ? policy::kMetricEnrollmentRestarted
                              : policy::kMetricEnrollmentStarted);

  view_->ShowEnrollmentSpinnerScreen();
  CreateEnrollmentHelper();
  enrollment_helper_->EnrollUsingAuthCode(
      auth_code, shark_controller_ != nullptr /* fetch_additional_token */);
}

void EnrollmentScreen::OnRetry() {
  retry_task_.Cancel();
  ProcessRetry();
}

void EnrollmentScreen::AutomaticRetry() {
  retry_backoff_->InformOfRequest(false);
  retry_task_.Reset(base::Bind(&EnrollmentScreen::ProcessRetry,
                               weak_ptr_factory_.GetWeakPtr()));

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, retry_task_.callback(), retry_backoff_->GetTimeUntilRelease());
}

void EnrollmentScreen::ProcessRetry() {
  ++num_retries_;
  LOG(WARNING) << "Enrollment retry " << num_retries_;
  Show();
}

void EnrollmentScreen::OnCancel() {
  if (AdvanceToNextAuth()) {
    Show();
    return;
  }

  UMA(policy::kMetricEnrollmentCancelled);
  if (elapsed_timer_)
    UMA_ENROLLMENT_TIME(kMetricEnrollmentTimeCancel, elapsed_timer_);

  const ScreenExitCode exit_code =
      config_.is_forced() ? ScreenExitCode::ENTERPRISE_ENROLLMENT_BACK
                          : ScreenExitCode::ENTERPRISE_ENROLLMENT_COMPLETED;
  ClearAuth(
      base::Bind(&EnrollmentScreen::Finish, base::Unretained(this), exit_code));
}

void EnrollmentScreen::OnConfirmationClosed() {
  ClearAuth(base::Bind(&EnrollmentScreen::Finish, base::Unretained(this),
                       ScreenExitCode::ENTERPRISE_ENROLLMENT_COMPLETED));
  // Restart browser to switch from DeviceCloudPolicyManagerChromeOS to
  // DeviceActiveDirectoryPolicyManager.
  if (g_browser_process->platform_part()
          ->browser_policy_connector_chromeos()
          ->IsActiveDirectoryManaged()) {
    // TODO(tnagel): Refactor BrowserPolicyConnectorChromeOS so that device
    // policy providers are only registered after enrollment has finished and
    // thus the correct one can be picked without restarting the browser.
    chrome::AttemptRestart();
  }
}

void EnrollmentScreen::OnAdJoined(const std::string& realm) {
  std::move(on_joined_callback_).Run(realm);
}

void EnrollmentScreen::OnAuthError(const GoogleServiceAuthError& error) {
  RecordEnrollmentErrorMetrics();
  view_->ShowAuthError(error);
}

void EnrollmentScreen::OnEnrollmentError(policy::EnrollmentStatus status) {
  // TODO(pbond): remove this LOG once http://crbug.com/586961 is fixed.
  LOG(WARNING) << "Enrollment error occured: status=" << status.status()
               << " http status=" << status.http_status()
               << " DM status=" << status.client_status();
  RecordEnrollmentErrorMetrics();
  // If the DM server does not have a device pre-provisioned for attestation-
  // based enrollment and we have a fallback authentication, show it.
  if (status.status() == policy::EnrollmentStatus::REGISTRATION_FAILED &&
      status.client_status() == policy::DM_STATUS_SERVICE_DEVICE_NOT_FOUND &&
      current_auth_ == AUTH_ATTESTATION && AdvanceToNextAuth()) {
    Show();
  } else {
    view_->ShowEnrollmentStatus(status);
    if (WizardController::UsingHandsOffEnrollment())
      AutomaticRetry();
  }
}

void EnrollmentScreen::OnOtherError(
    EnterpriseEnrollmentHelper::OtherError error) {
  RecordEnrollmentErrorMetrics();
  view_->ShowOtherError(error);
  if (WizardController::UsingHandsOffEnrollment())
    AutomaticRetry();
}

void EnrollmentScreen::OnDeviceEnrolled(const std::string& additional_token) {
  // TODO(pbond): remove this LOG once http://crbug.com/586961 is fixed.
  LOG(WARNING) << "Device is successfully enrolled.";
  if (!additional_token.empty())
    SendEnrollmentAuthToken(additional_token);

  enrollment_helper_->GetDeviceAttributeUpdatePermission();
}

void EnrollmentScreen::OnDeviceAttributeProvided(const std::string& asset_id,
                                                 const std::string& location) {
  enrollment_helper_->UpdateDeviceAttributes(asset_id, location);
}

void EnrollmentScreen::OnDeviceAttributeUpdatePermission(bool granted) {
  // If user is permitted to update device attributes
  // Show attribute prompt screen
  if (granted) {
    // TODO(pbond): remove this LOG once http://crbug.com/586961 is fixed.
    LOG(WARNING) << "Show device attribute prompt screen";
    StartupUtils::MarkDeviceRegistered(
        base::Bind(&EnrollmentScreen::ShowAttributePromptScreen,
                   weak_ptr_factory_.GetWeakPtr()));
  } else {
    // TODO(pbond): remove this LOG once http://crbug.com/586961 is fixed.
    LOG(WARNING) << "The device attribute update is not permitted";
    StartupUtils::MarkDeviceRegistered(
        base::Bind(&EnrollmentScreen::ShowEnrollmentStatusOnSuccess,
                   weak_ptr_factory_.GetWeakPtr()));
  }

}

void EnrollmentScreen::OnDeviceAttributeUploadCompleted(bool success) {
  if (success) {
    // If the device attributes have been successfully uploaded, fetch policy.
    policy::BrowserPolicyConnectorChromeOS* connector =
        g_browser_process->platform_part()->browser_policy_connector_chromeos();
    connector->GetDeviceCloudPolicyManager()->core()->RefreshSoon();
    view_->ShowEnrollmentStatus(
        policy::EnrollmentStatus::ForStatus(policy::EnrollmentStatus::SUCCESS));
  } else {
    view_->ShowEnrollmentStatus(policy::EnrollmentStatus::ForStatus(
        policy::EnrollmentStatus::ATTRIBUTE_UPDATE_FAILED));
  }
}

void EnrollmentScreen::ShowAttributePromptScreen() {
  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  policy::DeviceCloudPolicyManagerChromeOS* policy_manager =
      connector->GetDeviceCloudPolicyManager();

  policy::CloudPolicyStore* store = policy_manager->core()->store();

  const enterprise_management::PolicyData* policy = store->policy();

  std::string asset_id = policy ? policy->annotated_asset_id() : std::string();
  std::string location = policy ? policy->annotated_location() : std::string();
  view_->ShowAttributePromptScreen(asset_id, location);
}

void EnrollmentScreen::SendEnrollmentAuthToken(const std::string& token) {
  DCHECK(shark_controller_);
  shark_controller_->OnAuthenticationDone(enrolling_user_domain_, token);
}

void EnrollmentScreen::ShowEnrollmentStatusOnSuccess() {
  retry_backoff_->InformOfRequest(true);
  if (elapsed_timer_)
    UMA_ENROLLMENT_TIME(kMetricEnrollmentTimeSuccess, elapsed_timer_);
  if (WizardController::UsingHandsOffEnrollment()) {
    OnConfirmationClosed();
  } else {
    view_->ShowEnrollmentStatus(
        policy::EnrollmentStatus::ForStatus(policy::EnrollmentStatus::SUCCESS));
  }
}

void EnrollmentScreen::UMA(policy::MetricEnrollment sample) {
  EnrollmentUMA(sample, config_.mode);
}

void EnrollmentScreen::ShowSigninScreen() {
  view_->Show();
  view_->ShowSigninScreen();
}

void EnrollmentScreen::RecordEnrollmentErrorMetrics() {
  enrollment_failed_once_ = true;
  //  TODO(drcrash): Maybe create multiple metrics (http://crbug.com/640313)?
  if (elapsed_timer_)
    UMA_ENROLLMENT_TIME(kMetricEnrollmentTimeFailure, elapsed_timer_);
}

void EnrollmentScreen::JoinDomain(OnDomainJoinedCallback on_joined_callback) {
  on_joined_callback_ = std::move(on_joined_callback);
  view_->ShowAdJoin();
}

}  // namespace chromeos
