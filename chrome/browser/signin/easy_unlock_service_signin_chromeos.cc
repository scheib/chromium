// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/easy_unlock_service_signin_chromeos.h"

#include <stdint.h>

#include "base/base64url.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/json/json_string_value_serializer.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/sys_info.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/login/easy_unlock/easy_unlock_challenge_wrapper.h"
#include "chrome/browser/chromeos/login/easy_unlock/easy_unlock_key_manager.h"
#include "chrome/browser/chromeos/login/easy_unlock/easy_unlock_tpm_key_manager.h"
#include "chrome/browser/chromeos/login/easy_unlock/easy_unlock_tpm_key_manager_factory.h"
#include "chrome/browser/chromeos/login/session/user_session_manager.h"
#include "chrome/browser/signin/easy_unlock_app_manager.h"
#include "chrome/browser/signin/easy_unlock_metrics.h"
#include "chromeos/login/auth/user_context.h"
#include "chromeos/tpm/tpm_token_loader.h"
#include "components/cryptauth/remote_device.h"
#include "components/proximity_auth/logging/logging.h"
#include "components/proximity_auth/switches.h"

namespace {

// The maximum allowed backoff interval when waiting for cryptohome to start.
uint32_t kMaxCryptohomeBackoffIntervalMs = 10000u;

// If the data load fails, the initial interval after which the load will be
// retried. Further intervals will exponentially increas by factor 2.
uint32_t kInitialCryptohomeBackoffIntervalMs = 200u;

// Calculates the backoff interval that should be used next.
// |backoff| The last backoff interval used.
uint32_t GetNextBackoffInterval(uint32_t backoff) {
  if (backoff == 0u)
    return kInitialCryptohomeBackoffIntervalMs;
  return backoff * 2;
}

void LoadDataForUser(
    const AccountId& account_id,
    uint32_t backoff_ms,
    const chromeos::EasyUnlockKeyManager::GetDeviceDataListCallback& callback);

// Callback passed to |LoadDataForUser()|.
// If |LoadDataForUser| function succeeded, it invokes |callback| with the
// results.
// If |LoadDataForUser| failed and further retries are allowed, schedules new
// |LoadDataForUser| call with some backoff. If no further retires are allowed,
// it invokes |callback| with the |LoadDataForUser| results.
void RetryDataLoadOnError(
    const AccountId& account_id,
    uint32_t backoff_ms,
    const chromeos::EasyUnlockKeyManager::GetDeviceDataListCallback& callback,
    bool success,
    const chromeos::EasyUnlockDeviceKeyDataList& data_list) {
  if (success) {
    callback.Run(success, data_list);
    return;
  }

  uint32_t next_backoff_ms = GetNextBackoffInterval(backoff_ms);
  if (next_backoff_ms > kMaxCryptohomeBackoffIntervalMs) {
    callback.Run(false, data_list);
    return;
  }

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&LoadDataForUser, account_id, next_backoff_ms, callback),
      base::TimeDelta::FromMilliseconds(next_backoff_ms));
}

// Loads device data list associated with the user's Easy unlock keys.
void LoadDataForUser(
    const AccountId& account_id,
    uint32_t backoff_ms,
    const chromeos::EasyUnlockKeyManager::GetDeviceDataListCallback& callback) {
  chromeos::EasyUnlockKeyManager* key_manager =
      chromeos::UserSessionManager::GetInstance()->GetEasyUnlockKeyManager();
  DCHECK(key_manager);

  key_manager->GetDeviceDataList(
      chromeos::UserContext(account_id),
      base::Bind(&RetryDataLoadOnError, account_id, backoff_ms, callback));
}

// Deserializes a vector of BeaconSeeds. If an error occurs, an empty vector
// will be returned. Note: The logic to serialize BeaconSeeds lives in
// EasyUnlockServiceRegular.
// Note: The serialization of device data inside a user session is different
// than outside the user session (sign-in). RemoteDevices are serialized as
// protocol buffers inside the user session, but we have a custom serialization
// scheme for sign-in due to slightly different data requirements.
std::vector<cryptauth::BeaconSeed> DeserializeBeaconSeeds(
    const std::string& serialized_beacon_seeds) {
  std::vector<cryptauth::BeaconSeed> beacon_seeds;

  JSONStringValueDeserializer deserializer(serialized_beacon_seeds);
  std::string error;
  std::unique_ptr<base::Value> deserialized_value =
      deserializer.Deserialize(nullptr, &error);
  if (!deserialized_value) {
    PA_LOG(ERROR) << "Unable to deserialize BeaconSeeds: " << error;
    return beacon_seeds;
  }

  base::ListValue* beacon_seed_list;
  if (!deserialized_value->GetAsList(&beacon_seed_list)) {
    PA_LOG(ERROR) << "Deserialized BeaconSeeds value is not list.";
    return beacon_seeds;
  }

  for (size_t i = 0; i < beacon_seed_list->GetSize(); ++i) {
    std::string b64_beacon_seed;
    if (!beacon_seed_list->GetString(i, &b64_beacon_seed)) {
      PA_LOG(ERROR) << "Expected Base64 BeaconSeed.";
      continue;
    }

    std::string proto_serialized_beacon_seed;
    if (!base::Base64UrlDecode(b64_beacon_seed,
                               base::Base64UrlDecodePolicy::REQUIRE_PADDING,
                               &proto_serialized_beacon_seed)) {
      PA_LOG(ERROR) << "Unable to decode BeaconSeed.";
      continue;
    }

    cryptauth::BeaconSeed beacon_seed;
    if (!beacon_seed.ParseFromString(proto_serialized_beacon_seed)) {
      PA_LOG(ERROR) << "Unable to parse BeaconSeed proto.";
      continue;
    }

    beacon_seeds.push_back(beacon_seed);
  }

  PA_LOG(INFO) << "Deserialized " << beacon_seeds.size() << " BeaconSeeds.";
  return beacon_seeds;
}

}  // namespace

EasyUnlockServiceSignin::UserData::UserData()
    : state(EasyUnlockServiceSignin::USER_DATA_STATE_INITIAL) {
}

EasyUnlockServiceSignin::UserData::~UserData() {}

EasyUnlockServiceSignin::EasyUnlockServiceSignin(Profile* profile)
    : EasyUnlockService(profile),
      account_id_(EmptyAccountId()),
      user_pod_last_focused_timestamp_(base::TimeTicks::Now()),
      weak_ptr_factory_(this) {}

EasyUnlockServiceSignin::~EasyUnlockServiceSignin() {
}

void EasyUnlockServiceSignin::SetCurrentUser(const AccountId& account_id) {
  OnFocusedUserChanged(account_id);
}

void EasyUnlockServiceSignin::WrapChallengeForUserAndDevice(
    const AccountId& account_id,
    const std::string& device_public_key,
    const std::string& channel_binding_data,
    base::Callback<void(const std::string& wraped_challenge)> callback) {
  auto it = user_data_.find(account_id);
  if (it == user_data_.end() || it->second->state != USER_DATA_STATE_LOADED) {
    PA_LOG(ERROR) << "TPM data not loaded for " << account_id.Serialize();
    callback.Run(std::string());
    return;
  }

  std::string device_public_key_base64;
  base::Base64UrlEncode(device_public_key,
                        base::Base64UrlEncodePolicy::INCLUDE_PADDING,
                        &device_public_key_base64);
  for (const auto& device_data : it->second->devices) {
    if (device_data.public_key == device_public_key_base64) {
      PA_LOG(INFO) << "Wrapping challenge for " << account_id.Serialize()
                   << "...";
      challenge_wrapper_.reset(new chromeos::EasyUnlockChallengeWrapper(
          device_data.challenge, channel_binding_data, account_id,
          EasyUnlockTpmKeyManagerFactory::GetInstance()->Get(profile())));
      challenge_wrapper_->WrapChallenge(callback);
      return;
    }
  }

  PA_LOG(ERROR) << "Unable to find device record for "
                << account_id.Serialize();
  callback.Run(std::string());
}

EasyUnlockService::Type EasyUnlockServiceSignin::GetType() const {
  return EasyUnlockService::TYPE_SIGNIN;
}

AccountId EasyUnlockServiceSignin::GetAccountId() const {
  return account_id_;
}

void EasyUnlockServiceSignin::LaunchSetup() {
  NOTREACHED();
}

const base::DictionaryValue* EasyUnlockServiceSignin::GetPermitAccess() const {
  return nullptr;
}

void EasyUnlockServiceSignin::SetPermitAccess(
    const base::DictionaryValue& permit) {
  NOTREACHED();
}

void EasyUnlockServiceSignin::ClearPermitAccess() {
  NOTREACHED();
}

const base::ListValue* EasyUnlockServiceSignin::GetRemoteDevices() const {
  const UserData* data = FindLoadedDataForCurrentUser();
  if (!data)
    return nullptr;
  return &data->remote_devices_value;
}

void EasyUnlockServiceSignin::SetRemoteDevices(
    const base::ListValue& devices) {
  NOTREACHED();
}

void EasyUnlockServiceSignin::SetRemoteBleDevices(
    const base::ListValue& devices) {
  NOTREACHED();
}

void EasyUnlockServiceSignin::RunTurnOffFlow() {
  NOTREACHED();
}

void EasyUnlockServiceSignin::ResetTurnOffFlow() {
  NOTREACHED();
}

EasyUnlockService::TurnOffFlowStatus
    EasyUnlockServiceSignin::GetTurnOffFlowStatus() const {
  return EasyUnlockService::IDLE;
}

std::string EasyUnlockServiceSignin::GetChallenge() const {
  const UserData* data = FindLoadedDataForCurrentUser();
  // TODO(xiyuan): Use correct remote device instead of hard coded first one.
  uint32_t device_index = 0;
  if (!data || data->devices.size() <= device_index)
    return std::string();
  return data->devices[device_index].challenge;
}

std::string EasyUnlockServiceSignin::GetWrappedSecret() const {
  const UserData* data = FindLoadedDataForCurrentUser();
  // TODO(xiyuan): Use correct remote device instead of hard coded first one.
  uint32_t device_index = 0;
  if (!data || data->devices.size() <= device_index)
    return std::string();
  return data->devices[device_index].wrapped_secret;
}

void EasyUnlockServiceSignin::RecordEasySignInOutcome(
    const AccountId& account_id,
    bool success) const {
  DCHECK(GetAccountId() == account_id)
      << "GetAccountId()=" << GetAccountId().Serialize()
      << " != account_id=" << account_id.Serialize();

  RecordEasyUnlockSigninEvent(
      success ? EASY_UNLOCK_SUCCESS : EASY_UNLOCK_FAILURE);
  if (success) {
    RecordEasyUnlockSigninDuration(
        base::TimeTicks::Now() - user_pod_last_focused_timestamp_);
  }
  DVLOG(1) << "Easy sign-in " << (success ? "success" : "failure");
}

void EasyUnlockServiceSignin::RecordPasswordLoginEvent(
    const AccountId& account_id) const {
  // This happens during tests, where a user could log in without the user pod
  // being focused.
  if (GetAccountId() != account_id)
    return;

  if (!IsEnabled())
    return;

  EasyUnlockAuthEvent event = GetPasswordAuthEvent();
  RecordEasyUnlockSigninEvent(event);
  DVLOG(1) << "Easy Sign-in password login event, event=" << event;
}

void EasyUnlockServiceSignin::StartAutoPairing(
    const AutoPairingResultCallback& callback) {
  NOTREACHED();
}

void EasyUnlockServiceSignin::SetAutoPairingResult(
    bool success,
    const std::string& error) {
  NOTREACHED();
}

void EasyUnlockServiceSignin::InitializeInternal() {
  if (chromeos::LoginState::Get()->IsUserLoggedIn())
    return;

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          proximity_auth::switches::kEnableBluetoothLowEnergyDiscovery) &&
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
          proximity_auth::switches::kEnableChromeOSLogin)) {
    return;
  }

  service_active_ = true;

  chromeos::LoginState::Get()->AddObserver(this);
  proximity_auth::ScreenlockBridge* screenlock_bridge =
      proximity_auth::ScreenlockBridge::Get();
  screenlock_bridge->AddObserver(this);
  if (screenlock_bridge->focused_account_id().is_valid())
    OnFocusedUserChanged(screenlock_bridge->focused_account_id());
}

void EasyUnlockServiceSignin::ShutdownInternal() {
  if (!service_active_)
    return;
  service_active_ = false;

  weak_ptr_factory_.InvalidateWeakPtrs();
  proximity_auth::ScreenlockBridge::Get()->RemoveObserver(this);
  chromeos::LoginState::Get()->RemoveObserver(this);
  user_data_.clear();
}

bool EasyUnlockServiceSignin::IsAllowedInternal() const {
  return service_active_ && account_id_.is_valid() &&
         !chromeos::LoginState::Get()->IsUserLoggedIn();
}

void EasyUnlockServiceSignin::OnWillFinalizeUnlock(bool success) {
  // This code path should only be exercised for the lock screen, not for the
  // sign-in screen.
  NOTREACHED();
}

void EasyUnlockServiceSignin::OnSuspendDoneInternal() {
  // Ignored.
}

void EasyUnlockServiceSignin::OnScreenDidLock(
    proximity_auth::ScreenlockBridge::LockHandler::ScreenType screen_type) {
  // In production code, the screen type should always be the signin screen; but
  // in tests, the screen type might be different.
  if (screen_type !=
          proximity_auth::ScreenlockBridge::LockHandler::SIGNIN_SCREEN)
    return;

  // Update initial UI is when the account picker on login screen is ready.
  ShowInitialUserState();
  user_pod_last_focused_timestamp_ = base::TimeTicks::Now();
}

void EasyUnlockServiceSignin::OnScreenDidUnlock(
    proximity_auth::ScreenlockBridge::LockHandler::ScreenType screen_type) {
  // In production code, the screen type should always be the signin screen; but
  // in tests, the screen type might be different.
  if (screen_type !=
          proximity_auth::ScreenlockBridge::LockHandler::SIGNIN_SCREEN)
    return;

  DisableAppWithoutResettingScreenlockState();

  Shutdown();
}

void EasyUnlockServiceSignin::OnFocusedUserChanged(
    const AccountId& account_id) {
  if (account_id_ == account_id)
    return;

  // Setting or clearing the account_id may changed |IsAllowed| value, so in
  // these
  // cases update the app state. Otherwise, it's enough to notify the app the
  // user data has been updated.
  const bool should_update_app_state = (account_id_ != account_id);
  account_id_ = account_id;
  user_pod_last_focused_timestamp_ = base::TimeTicks::Now();

  ResetScreenlockState();
  ShowInitialUserState();

  if (should_update_app_state) {
    UpdateAppState();
  } else {
    NotifyUserUpdated();
  }

  LoadCurrentUserDataIfNeeded();

  // Start loading TPM system token.
  // The system token will be needed to sign a nonce using TPM private key
  // during the sign-in protocol.
  EasyUnlockScreenlockStateHandler::HardlockState hardlock_state;
  if (GetPersistedHardlockState(&hardlock_state) &&
      hardlock_state != EasyUnlockScreenlockStateHandler::NO_PAIRING) {
    chromeos::TPMTokenLoader::Get()->EnsureStarted();
  }
}

void EasyUnlockServiceSignin::LoggedInStateChanged() {
  if (!chromeos::LoginState::Get()->IsUserLoggedIn())
    return;
  DisableAppWithoutResettingScreenlockState();
}

void EasyUnlockServiceSignin::LoadCurrentUserDataIfNeeded() {
  // TODO(xiyuan): Revisit this when adding tests.
  if (!base::SysInfo::IsRunningOnChromeOS())
    return;

  if (!account_id_.is_valid() || !service_active_)
    return;

  const auto it = user_data_.find(account_id_);
  if (it == user_data_.end())
    user_data_.insert(
        std::make_pair(account_id_, base::MakeUnique<UserData>()));

  UserData* data = user_data_[account_id_].get();

  if (data->state != USER_DATA_STATE_INITIAL)
    return;
  data->state = USER_DATA_STATE_LOADING;

  LoadDataForUser(
      account_id_,
      allow_cryptohome_backoff_ ? 0u : kMaxCryptohomeBackoffIntervalMs,
      base::Bind(&EasyUnlockServiceSignin::OnUserDataLoaded,
                 weak_ptr_factory_.GetWeakPtr(), account_id_));
}

void EasyUnlockServiceSignin::OnUserDataLoaded(
    const AccountId& account_id,
    bool success,
    const chromeos::EasyUnlockDeviceKeyDataList& devices) {
  allow_cryptohome_backoff_ = false;

  UserData* data = user_data_[account_id].get();
  data->state = USER_DATA_STATE_LOADED;
  if (success) {
    data->devices = devices;
    chromeos::EasyUnlockKeyManager::DeviceDataListToRemoteDeviceList(
        account_id, devices, &data->remote_devices_value);

    // User could have a NO_HARDLOCK state but has no remote devices if
    // previous user session shuts down before
    // CheckCryptohomeKeysAndMaybeHardlock finishes. Set NO_PAIRING state
    // and update UI to remove the confusing spinner in this case.
    EasyUnlockScreenlockStateHandler::HardlockState hardlock_state;
    if (devices.empty() &&
        GetPersistedHardlockState(&hardlock_state) &&
        hardlock_state == EasyUnlockScreenlockStateHandler::NO_HARDLOCK) {
      SetHardlockStateForUser(account_id,
                              EasyUnlockScreenlockStateHandler::NO_PAIRING);
    }
  }

  // If the fetched data belongs to the currently focused user, notify the app
  // that it has to refresh it's user data.
  if (account_id == account_id_)
    NotifyUserUpdated();

  if (devices.empty())
    return;

  cryptauth::RemoteDeviceList remote_devices;
  for (const auto& device : devices) {
    std::string decoded_public_key, decoded_psk, decoded_challenge;
    if (!base::Base64UrlDecode(device.public_key,
                               base::Base64UrlDecodePolicy::REQUIRE_PADDING,
                               &decoded_public_key) ||
        !base::Base64UrlDecode(device.psk,
                               base::Base64UrlDecodePolicy::REQUIRE_PADDING,
                               &decoded_psk)) {
      PA_LOG(ERROR) << "Unable to decode stored remote device:\n"
                    << "  public_key: " << device.public_key << "\n"
                    << "  psk: " << device.psk;
      continue;
    }
    cryptauth::RemoteDevice remote_device(
        account_id.GetUserEmail(), std::string(), decoded_public_key,
        device.bluetooth_address, decoded_psk, decoded_challenge);

    if (!device.serialized_beacon_seeds.empty()) {
      PA_LOG(INFO) << "Deserializing BeaconSeeds: "
                   << device.serialized_beacon_seeds;
      // TODO(tengs): Assign deserialized BeaconSeeds to the RemoteDevice.
      DeserializeBeaconSeeds(device.serialized_beacon_seeds);
    } else {
      PA_LOG(WARNING) << "No BeaconSeeds were loaded.";
    }

    remote_devices.push_back(remote_device);
    PA_LOG(INFO) << "Loaded Remote Device:\n"
                 << "  user id: " << remote_device.user_id << "\n"
                 << "  name: " << remote_device.name << "\n"
                 << "  public key" << device.public_key << "\n"
                 << "  bt_addr:" << remote_device.bluetooth_address;
  }

  SetProximityAuthDevices(account_id, remote_devices);
}

const EasyUnlockServiceSignin::UserData*
    EasyUnlockServiceSignin::FindLoadedDataForCurrentUser() const {
  if (!account_id_.is_valid())
    return nullptr;

  const auto it = user_data_.find(account_id_);
  if (it == user_data_.end())
    return nullptr;
  if (it->second->state != USER_DATA_STATE_LOADED)
    return nullptr;
  return it->second.get();
}
