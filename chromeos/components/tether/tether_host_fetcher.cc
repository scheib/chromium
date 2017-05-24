// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/tether/tether_host_fetcher.h"

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "components/cryptauth/cryptauth_device_manager.h"
#include "components/cryptauth/cryptauth_enrollment_manager.h"
#include "components/cryptauth/cryptauth_service.h"
#include "components/cryptauth/remote_device_loader.h"
#include "components/cryptauth/secure_message_delegate.h"

namespace chromeos {

namespace tether {

TetherHostFetcher::TetherHostFetchRequest::TetherHostFetchRequest(
    const TetherHostListCallback& list_callback)
    : device_id(""), list_callback(list_callback) {}

TetherHostFetcher::TetherHostFetchRequest::TetherHostFetchRequest(
    const std::string& device_id,
    const TetherHostCallback& single_callback)
    : device_id(device_id), single_callback(single_callback) {}

TetherHostFetcher::TetherHostFetchRequest::TetherHostFetchRequest(
    const TetherHostFetchRequest& other)
    : device_id(other.device_id),
      list_callback(other.list_callback),
      single_callback(other.single_callback) {}

TetherHostFetcher::TetherHostFetchRequest::~TetherHostFetchRequest() {}

TetherHostFetcher::TetherHostFetcher(
    cryptauth::CryptAuthService* cryptauth_service)
    : cryptauth_service_(cryptauth_service), weak_ptr_factory_(this) {}

TetherHostFetcher::~TetherHostFetcher() {}

void TetherHostFetcher::FetchAllTetherHosts(
    const TetherHostListCallback& callback) {
  requests_.push_back(TetherHostFetchRequest(callback));
  StartLoadingDevicesIfNeeded();
}

void TetherHostFetcher::FetchTetherHost(const std::string& device_id,
                                        const TetherHostCallback& callback) {
  requests_.push_back(TetherHostFetchRequest(device_id, callback));
  StartLoadingDevicesIfNeeded();
}

void TetherHostFetcher::StartLoadingDevicesIfNeeded() {
  if (remote_device_loader_) {
    // If the loader is already active, there is nothing to do.
    return;
  }

  remote_device_loader_ = cryptauth::RemoteDeviceLoader::Factory::NewInstance(
      cryptauth_service_->GetCryptAuthDeviceManager()->GetTetherHosts(),
      cryptauth_service_->GetAccountId(),
      cryptauth_service_->GetCryptAuthEnrollmentManager()->GetUserPrivateKey(),
      cryptauth_service_->CreateSecureMessageDelegate());
  remote_device_loader_->Load(
      false /* should_load_beacon_seeds */,
      base::Bind(&TetherHostFetcher::OnRemoteDevicesLoaded,
                 weak_ptr_factory_.GetWeakPtr()));
}

void TetherHostFetcher::OnRemoteDevicesLoaded(
    const cryptauth::RemoteDeviceList& remote_devices) {
  // Make a copy of the list before deleting |remote_device_loader_|.
  cryptauth::RemoteDeviceList remote_devices_copy = remote_devices;
  remote_device_loader_.reset();

  // Make a copy of the requests to ensure that if one of the callbacks makes a
  // new fetch request, it does not affect the iteration of requests.
  std::vector<TetherHostFetchRequest> requests = requests_;
  requests_.clear();

  for (auto& request : requests) {
    if (request.device_id.empty()) {
      DCHECK(!request.list_callback.is_null());

      request.list_callback.Run(remote_devices_copy);
      continue;
    }

    DCHECK(!request.single_callback.is_null());
    bool has_run_callback = false;
    for (const auto& remote_device : remote_devices_copy) {
      if (request.device_id == remote_device.GetDeviceId()) {
        has_run_callback = true;
        request.single_callback.Run(
            base::MakeUnique<cryptauth::RemoteDevice>(remote_device));
        break;
      }
    }

    if (!has_run_callback) {
      request.single_callback.Run(nullptr);
    }
  }
}

}  // namespace tether

}  // namespace chromeos
