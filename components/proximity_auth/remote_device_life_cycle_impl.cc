// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/proximity_auth/remote_device_life_cycle_impl.h"

#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/default_tick_clock.h"
#include "components/cryptauth/bluetooth_throttler_impl.h"
#include "components/cryptauth/connection_finder.h"
#include "components/cryptauth/device_to_device_authenticator.h"
#include "components/cryptauth/secure_context.h"
#include "components/cryptauth/secure_message_delegate.h"
#include "components/proximity_auth/bluetooth_connection.h"
#include "components/proximity_auth/bluetooth_connection_finder.h"
#include "components/proximity_auth/bluetooth_low_energy_connection_finder.h"
#include "components/proximity_auth/logging/logging.h"
#include "components/proximity_auth/messenger_impl.h"
#include "components/proximity_auth/proximity_auth_client.h"
#include "components/proximity_auth/switches.h"

namespace proximity_auth {

namespace {

// The UUID of the Smart Lock classic Bluetooth service.
const char kClassicBluetoothServiceUUID[] =
    "704EE561-3782-405A-A14B-2D47A2DDCDDF";

// The time to wait, in seconds, after authentication fails, before retrying
// another connection.
const int kAuthenticationRecoveryTimeSeconds = 10;

}  // namespace

RemoteDeviceLifeCycleImpl::RemoteDeviceLifeCycleImpl(
    const cryptauth::RemoteDevice& remote_device,
    ProximityAuthClient* proximity_auth_client)
    : remote_device_(remote_device),
      proximity_auth_client_(proximity_auth_client),
      state_(RemoteDeviceLifeCycle::State::STOPPED),
      observers_(base::ObserverList<Observer>::NOTIFY_EXISTING_ONLY),
      bluetooth_throttler_(cryptauth::BluetoothThrottlerImpl::GetInstance()),
      weak_ptr_factory_(this) {}

RemoteDeviceLifeCycleImpl::~RemoteDeviceLifeCycleImpl() {}

void RemoteDeviceLifeCycleImpl::Start() {
  PA_LOG(INFO) << "Life cycle for " << remote_device_.bluetooth_address
               << " started.";
  DCHECK(state_ == RemoteDeviceLifeCycle::State::STOPPED);
  FindConnection();
}

cryptauth::RemoteDevice RemoteDeviceLifeCycleImpl::GetRemoteDevice() const {
  return remote_device_;
}

cryptauth::Connection* RemoteDeviceLifeCycleImpl::GetConnection() const {
  if (connection_)
    return connection_.get();
  if (messenger_)
    return messenger_->GetConnection();
  return nullptr;
}

RemoteDeviceLifeCycle::State RemoteDeviceLifeCycleImpl::GetState() const {
  return state_;
}

Messenger* RemoteDeviceLifeCycleImpl::GetMessenger() {
  return messenger_.get();
}

void RemoteDeviceLifeCycleImpl::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void RemoteDeviceLifeCycleImpl::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

std::unique_ptr<cryptauth::ConnectionFinder>
RemoteDeviceLifeCycleImpl::CreateConnectionFinder() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          proximity_auth::switches::kEnableBluetoothLowEnergyDiscovery)) {
    return base::MakeUnique<BluetoothLowEnergyConnectionFinder>(
        remote_device_, bluetooth_throttler_);
  } else {
    return base::MakeUnique<BluetoothConnectionFinder>(
        remote_device_, device::BluetoothUUID(kClassicBluetoothServiceUUID),
        base::TimeDelta::FromSeconds(3));
  }
}

std::unique_ptr<cryptauth::Authenticator>
RemoteDeviceLifeCycleImpl::CreateAuthenticator() {
  return base::MakeUnique<cryptauth::DeviceToDeviceAuthenticator>(
      connection_.get(), remote_device_.user_id,
      proximity_auth_client_->CreateSecureMessageDelegate());
}

void RemoteDeviceLifeCycleImpl::TransitionToState(
    RemoteDeviceLifeCycle::State new_state) {
  PA_LOG(INFO) << "Life cycle transition: " << static_cast<int>(state_)
               << " => " << static_cast<int>(new_state);
  RemoteDeviceLifeCycle::State old_state = state_;
  state_ = new_state;
  for (auto& observer : observers_)
    observer.OnLifeCycleStateChanged(old_state, new_state);
}

void RemoteDeviceLifeCycleImpl::FindConnection() {
  connection_finder_ = CreateConnectionFinder();
  if (!connection_finder_) {
    // TODO(tengs): We need to introduce a failed state if the RemoteDevice data
    // is invalid.
    TransitionToState(RemoteDeviceLifeCycleImpl::State::FINDING_CONNECTION);
    return;
  }

  connection_finder_->Find(
      base::Bind(&RemoteDeviceLifeCycleImpl::OnConnectionFound,
                 weak_ptr_factory_.GetWeakPtr()));
  TransitionToState(RemoteDeviceLifeCycle::State::FINDING_CONNECTION);
}

void RemoteDeviceLifeCycleImpl::OnConnectionFound(
    std::unique_ptr<cryptauth::Connection> connection) {
  DCHECK(state_ == RemoteDeviceLifeCycle::State::FINDING_CONNECTION);
  connection_ = std::move(connection);
  authenticator_ = CreateAuthenticator();
  authenticator_->Authenticate(
      base::Bind(&RemoteDeviceLifeCycleImpl::OnAuthenticationResult,
                 weak_ptr_factory_.GetWeakPtr()));
  TransitionToState(RemoteDeviceLifeCycle::State::AUTHENTICATING);
}

void RemoteDeviceLifeCycleImpl::OnAuthenticationResult(
    cryptauth::Authenticator::Result result,
    std::unique_ptr<cryptauth::SecureContext> secure_context) {
  DCHECK(state_ == RemoteDeviceLifeCycle::State::AUTHENTICATING);
  authenticator_.reset();
  if (result != cryptauth::Authenticator::Result::SUCCESS) {
    PA_LOG(WARNING) << "Waiting " << kAuthenticationRecoveryTimeSeconds
                    << " seconds to retry after authentication failure.";
    connection_->Disconnect();
    authentication_recovery_timer_.Start(
        FROM_HERE,
        base::TimeDelta::FromSeconds(kAuthenticationRecoveryTimeSeconds), this,
        &RemoteDeviceLifeCycleImpl::FindConnection);
    TransitionToState(RemoteDeviceLifeCycle::State::AUTHENTICATION_FAILED);
    return;
  }

  // Create the MessengerImpl asynchronously. |messenger_| registers itself as
  // an observer of |connection_|, so creating it synchronously would trigger
  // |OnSendCompleted()| as an observer call for |messenger_|.
  secure_context_ = std::move(secure_context);
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&RemoteDeviceLifeCycleImpl::CreateMessenger,
                            weak_ptr_factory_.GetWeakPtr()));
}

void RemoteDeviceLifeCycleImpl::CreateMessenger() {
  DCHECK(state_ == RemoteDeviceLifeCycle::State::AUTHENTICATING);
  DCHECK(secure_context_);
  messenger_.reset(
      new MessengerImpl(std::move(connection_), std::move(secure_context_)));
  messenger_->AddObserver(this);

  TransitionToState(RemoteDeviceLifeCycle::State::SECURE_CHANNEL_ESTABLISHED);
}

void RemoteDeviceLifeCycleImpl::OnDisconnected() {
  DCHECK(state_ == RemoteDeviceLifeCycle::State::SECURE_CHANNEL_ESTABLISHED);
  messenger_.reset();
  FindConnection();
}

}  // namespace proximity_auth
