// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cryptauth/remote_device.h"

#include "base/base64.h"

namespace cryptauth {

namespace {

// Returns true if both vectors are BeaconSeeds are equal.
bool AreBeaconSeedsEqual(const std::vector<BeaconSeed> beacon_seeds1,
                         const std::vector<BeaconSeed> beacon_seeds2) {
  if (beacon_seeds1.size() != beacon_seeds2.size()) {
    return false;
  }

  for (size_t i = 0; i < beacon_seeds1.size(); ++i) {
    const BeaconSeed& seed1 = beacon_seeds1[i];
    const BeaconSeed& seed2 = beacon_seeds2[i];
    if (seed1.start_time_millis() != seed2.start_time_millis() ||
        seed1.end_time_millis() != seed2.end_time_millis() ||
        seed1.data() != seed2.data()) {
      return false;
    }
  }

  return true;
}

}  // namespace

RemoteDevice::RemoteDevice() {}

RemoteDevice::RemoteDevice(const std::string& user_id,
                           const std::string& name,
                           const std::string& public_key,
                           const std::string& bluetooth_address,
                           const std::string& persistent_symmetric_key,
                           std::string sign_in_challenge)
    : user_id(user_id),
      name(name),
      public_key(public_key),
      bluetooth_address(bluetooth_address),
      persistent_symmetric_key(persistent_symmetric_key),
      sign_in_challenge(sign_in_challenge),
      are_beacon_seeds_loaded(false) {}

RemoteDevice::RemoteDevice(const RemoteDevice& other) = default;

RemoteDevice::~RemoteDevice() {}

void RemoteDevice::LoadBeaconSeeds(
    const std::vector<BeaconSeed>& beacon_seeds) {
  this->are_beacon_seeds_loaded = true;
  this->beacon_seeds = beacon_seeds;
}

std::string RemoteDevice::GetDeviceId() const {
  std::string to_return;
  base::Base64Encode(public_key, &to_return);
  return to_return;
}

std::string RemoteDevice::GetTruncatedDeviceIdForLogs() const {
  return RemoteDevice::TruncateDeviceIdForLogs(GetDeviceId());
}

bool RemoteDevice::operator==(const RemoteDevice& other) const {
  // Only compare |beacon_seeds| if they are loaded.
  bool are_beacon_seeds_equal = false;
  if (are_beacon_seeds_loaded) {
    are_beacon_seeds_equal =
        other.are_beacon_seeds_loaded &&
        AreBeaconSeedsEqual(beacon_seeds, other.beacon_seeds);
  } else {
    are_beacon_seeds_equal = !other.are_beacon_seeds_loaded;
  }

  return user_id == other.user_id && name == other.name &&
         public_key == other.public_key &&
         bluetooth_address == other.bluetooth_address &&
         persistent_symmetric_key == other.persistent_symmetric_key &&
         sign_in_challenge == other.sign_in_challenge && are_beacon_seeds_equal;
}

bool RemoteDevice::operator<(const RemoteDevice& other) const {
  // |public_key| is the only field guaranteed to be set and is also unique to
  // each RemoteDevice. However, since it can contain null bytes, use
  // GetDeviceId(), which cannot contain null bytes, to compare devices.
  return GetDeviceId().compare(other.GetDeviceId()) < 0;
}

// static
std::string RemoteDevice::TruncateDeviceIdForLogs(const std::string& full_id) {
  if (full_id.length() <= 10) {
    return full_id;
  }

  return full_id.substr(0, 5)
      + "..."
      + full_id.substr(full_id.length() - 5, full_id.length());
}

}  // namespace cryptauth
