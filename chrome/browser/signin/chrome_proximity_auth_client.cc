// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/chrome_proximity_auth_client.h"

#include <stdint.h>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/sys_info.h"
#include "base/version.h"
#include "build/build_config.h"
#include "chrome/browser/cryptauth/chrome_cryptauth_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/signin/easy_unlock_service.h"
#include "chrome/browser/signin/easy_unlock_service_regular.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "components/cryptauth/cryptauth_client_impl.h"
#include "components/cryptauth/cryptauth_device_manager.h"
#include "components/cryptauth/cryptauth_enrollment_manager.h"
#include "components/cryptauth/secure_message_delegate.h"
#include "components/prefs/pref_service.h"
#include "components/proximity_auth/logging/logging.h"
#include "components/signin/core/browser/profile_oauth2_token_service.h"
#include "components/signin/core/browser/signin_manager_base.h"
#include "components/version_info/version_info.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/easy_unlock/secure_message_delegate_chromeos.h"
#include "chrome/browser/signin/easy_unlock_service_signin_chromeos.h"
#endif

using proximity_auth::ScreenlockState;

ChromeProximityAuthClient::ChromeProximityAuthClient(Profile* profile)
    : profile_(profile) {
}

ChromeProximityAuthClient::~ChromeProximityAuthClient() {
}

std::string ChromeProximityAuthClient::GetAuthenticatedUsername() const {
  const SigninManagerBase* signin_manager =
      SigninManagerFactory::GetForProfileIfExists(profile_);
  // |profile_| has to be a signed-in profile with SigninManager already
  // created. Otherwise, just crash to collect stack.
  DCHECK(signin_manager);
  return signin_manager->GetAuthenticatedAccountInfo().email;
}

void ChromeProximityAuthClient::UpdateScreenlockState(ScreenlockState state) {
  EasyUnlockService* service = EasyUnlockService::Get(profile_);
  if (service)
    service->UpdateScreenlockState(state);
}

void ChromeProximityAuthClient::FinalizeUnlock(bool success) {
  EasyUnlockService* service = EasyUnlockService::Get(profile_);
  if (service)
    service->FinalizeUnlock(success);
}

void ChromeProximityAuthClient::FinalizeSignin(const std::string& secret) {
  EasyUnlockService* service = EasyUnlockService::Get(profile_);
  if (service)
    service->FinalizeSignin(secret);
}

void ChromeProximityAuthClient::GetChallengeForUserAndDevice(
    const std::string& user_id,
    const std::string& remote_public_key,
    const std::string& channel_binding_data,
    base::Callback<void(const std::string& challenge)> callback) {
#if defined(OS_CHROMEOS)
  EasyUnlockService* easy_unlock_service = EasyUnlockService::Get(profile_);
  if (easy_unlock_service->GetType() == EasyUnlockService::TYPE_REGULAR) {
    PA_LOG(ERROR) << "Unable to get challenge when user is logged in.";
    callback.Run(std::string());
    return;
  }

  static_cast<EasyUnlockServiceSignin*>(easy_unlock_service)
      ->WrapChallengeForUserAndDevice(AccountId::FromUserEmail(user_id),
                                      remote_public_key, channel_binding_data,
                                      callback);
#else
  callback.Run(std::string());
#endif
}

PrefService* ChromeProximityAuthClient::GetPrefService() {
  return profile_->GetPrefs();
}

std::unique_ptr<cryptauth::SecureMessageDelegate>
ChromeProximityAuthClient::CreateSecureMessageDelegate() {
// Note: Although CryptAuthService::CreateSecureMessageDelegate() exists, we
// don't use it here (as opposed to other methods in this class) because the
// CryptAuthService is not available on the ChromeOS login screen.
#if defined(OS_CHROMEOS)
  return base::MakeUnique<chromeos::SecureMessageDelegateChromeOS>();
#else
  return nullptr;
#endif
}

std::unique_ptr<cryptauth::CryptAuthClientFactory>
ChromeProximityAuthClient::CreateCryptAuthClientFactory() {
  return GetCryptAuthService()->CreateCryptAuthClientFactory();
}

cryptauth::DeviceClassifier ChromeProximityAuthClient::GetDeviceClassifier() {
  return GetCryptAuthService()->GetDeviceClassifier();
}

std::string ChromeProximityAuthClient::GetAccountId() {
  return GetCryptAuthService()->GetAccountId();
}

cryptauth::CryptAuthEnrollmentManager*
ChromeProximityAuthClient::GetCryptAuthEnrollmentManager() {
  return GetCryptAuthService()->GetCryptAuthEnrollmentManager();
}

cryptauth::CryptAuthDeviceManager*
ChromeProximityAuthClient::GetCryptAuthDeviceManager() {
  return GetCryptAuthService()->GetCryptAuthDeviceManager();
}

cryptauth::CryptAuthService* ChromeProximityAuthClient::GetCryptAuthService() {
  return ChromeCryptAuthServiceFactory::GetInstance()->GetForBrowserContext(
      profile_);
}
