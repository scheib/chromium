// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cryptauth/remote_device_loader.h"

#include <stddef.h>

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "components/cryptauth/fake_secure_message_delegate.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cryptauth {
namespace {

// Prefixes for RemoteDevice fields.
const char kDeviceNamePrefix[] = "device";
const char kPublicKeyPrefix[] = "pk";
const char kBluetoothAddressPrefix[] = "11:22:33:44:55:0";

// The id of the user who the remote devices belong to.
const char kUserId[] = "example@gmail.com";

// The public key of the user's local device.
const char kUserPublicKey[] = "User public key";

// BeaconSeed values.
const int64_t kBeaconSeedStartTimeMs = 1000;
const int64_t kBeaconSeedEndTimeMs = 2000;
const char kBeaconSeedData[] = "Beacon Seed Data";

// Creates and returns an ExternalDeviceInfo proto with the fields appended with
// |suffix|.
cryptauth::ExternalDeviceInfo CreateDeviceInfo(const std::string& suffix) {
  cryptauth::ExternalDeviceInfo device_info;
  device_info.set_friendly_device_name(std::string(kDeviceNamePrefix) + suffix);
  device_info.set_public_key(std::string(kPublicKeyPrefix) + suffix);
  device_info.set_bluetooth_address(std::string(kBluetoothAddressPrefix) +
                                   suffix);
  device_info.add_beacon_seeds();
  BeaconSeed* beacon_seed = device_info.mutable_beacon_seeds(0);
  beacon_seed->set_start_time_millis(kBeaconSeedStartTimeMs);
  beacon_seed->set_end_time_millis(kBeaconSeedEndTimeMs);
  beacon_seed->set_data(kBeaconSeedData);
  return device_info;
}

}  // namespace

class CryptAuthRemoteDeviceLoaderTest : public testing::Test {
 public:
  CryptAuthRemoteDeviceLoaderTest()
      : secure_message_delegate_(new cryptauth::FakeSecureMessageDelegate()),
        user_private_key_(secure_message_delegate_->GetPrivateKeyForPublicKey(
            kUserPublicKey)) {}

  ~CryptAuthRemoteDeviceLoaderTest() {}

  void OnRemoteDevicesLoaded(
      const std::vector<cryptauth::RemoteDevice>& remote_devices) {
    remote_devices_ = remote_devices;
    LoadCompleted();
  }

  MOCK_METHOD0(LoadCompleted, void());

 protected:
  // Handles deriving the PSK. Ownership will be passed to the
  // RemoteDeviceLoader under test.
  std::unique_ptr<cryptauth::FakeSecureMessageDelegate>
      secure_message_delegate_;

  // The private key of the user local device.
  std::string user_private_key_;

  // Stores the result of the RemoteDeviceLoader.
  std::vector<cryptauth::RemoteDevice> remote_devices_;

  DISALLOW_COPY_AND_ASSIGN(CryptAuthRemoteDeviceLoaderTest);
};

TEST_F(CryptAuthRemoteDeviceLoaderTest, LoadZeroDevices) {
  std::vector<cryptauth::ExternalDeviceInfo> device_infos;
  RemoteDeviceLoader loader(device_infos, user_private_key_, kUserId,
                            std::move(secure_message_delegate_));

  std::vector<cryptauth::RemoteDevice> result;
  EXPECT_CALL(*this, LoadCompleted());
  loader.Load(
      false, base::Bind(&CryptAuthRemoteDeviceLoaderTest::OnRemoteDevicesLoaded,
                        base::Unretained(this)));

  EXPECT_EQ(0u, remote_devices_.size());
}

TEST_F(CryptAuthRemoteDeviceLoaderTest, LoadOneDeviceWithBeaconSeeds) {
  std::vector<cryptauth::ExternalDeviceInfo> device_infos(
      1, CreateDeviceInfo("0"));
  RemoteDeviceLoader loader(device_infos, user_private_key_, kUserId,
                            std::move(secure_message_delegate_));

  std::vector<cryptauth::RemoteDevice> result;
  EXPECT_CALL(*this, LoadCompleted());
  loader.Load(
      true, base::Bind(&CryptAuthRemoteDeviceLoaderTest::OnRemoteDevicesLoaded,
                       base::Unretained(this)));

  EXPECT_EQ(1u, remote_devices_.size());
  EXPECT_FALSE(remote_devices_[0].persistent_symmetric_key.empty());
  EXPECT_EQ(device_infos[0].friendly_device_name(), remote_devices_[0].name);
  EXPECT_EQ(device_infos[0].public_key(), remote_devices_[0].public_key);
  EXPECT_TRUE(remote_devices_[0].are_beacon_seeds_loaded);
  ASSERT_EQ(1u, remote_devices_[0].beacon_seeds.size());

  const BeaconSeed& beacon_seed = remote_devices_[0].beacon_seeds[0];
  EXPECT_EQ(kBeaconSeedData, beacon_seed.data());
  EXPECT_EQ(kBeaconSeedStartTimeMs, beacon_seed.start_time_millis());
  EXPECT_EQ(kBeaconSeedEndTimeMs, beacon_seed.end_time_millis());
}

TEST_F(CryptAuthRemoteDeviceLoaderTest, LoadDevicesWithAndWithoutBeaconSeeds) {
  std::vector<cryptauth::ExternalDeviceInfo> device_infos(
      1, CreateDeviceInfo("0"));

  RemoteDeviceLoader loader1(device_infos, user_private_key_, kUserId,
                             base::MakeUnique<FakeSecureMessageDelegate>());
  EXPECT_CALL(*this, LoadCompleted());
  loader1.Load(
      false /* should_load_beacon_seeds */,
      base::Bind(&CryptAuthRemoteDeviceLoaderTest::OnRemoteDevicesLoaded,
                 base::Unretained(this)));
  RemoteDevice remote_device_without_beacon_seed = remote_devices_[0];

  RemoteDeviceLoader loader2(device_infos, user_private_key_, kUserId,
                             base::MakeUnique<FakeSecureMessageDelegate>());
  EXPECT_CALL(*this, LoadCompleted());
  loader2.Load(
      true /* should_load_beacon_seeds */,
      base::Bind(&CryptAuthRemoteDeviceLoaderTest::OnRemoteDevicesLoaded,
                 base::Unretained(this)));
  RemoteDevice remote_device_with_beacon_seed = remote_devices_[0];

  EXPECT_EQ(remote_device_without_beacon_seed,
            remote_device_without_beacon_seed);
  EXPECT_EQ(remote_device_with_beacon_seed, remote_device_with_beacon_seed);
  EXPECT_FALSE(remote_device_with_beacon_seed ==
               remote_device_without_beacon_seed);
}

TEST_F(CryptAuthRemoteDeviceLoaderTest, LoadOneDeviceWithAddress) {
  std::vector<cryptauth::ExternalDeviceInfo> device_infos(1,
                                                         CreateDeviceInfo("0"));
  RemoteDeviceLoader loader(device_infos, user_private_key_, kUserId,
                            std::move(secure_message_delegate_));

  std::vector<cryptauth::RemoteDevice> result;
  EXPECT_CALL(*this, LoadCompleted());
  loader.Load(
      false, base::Bind(&CryptAuthRemoteDeviceLoaderTest::OnRemoteDevicesLoaded,
                        base::Unretained(this)));

  EXPECT_EQ(1u, remote_devices_.size());
  EXPECT_FALSE(remote_devices_[0].persistent_symmetric_key.empty());
  EXPECT_EQ(device_infos[0].friendly_device_name(), remote_devices_[0].name);
  EXPECT_EQ(device_infos[0].public_key(), remote_devices_[0].public_key);
  EXPECT_EQ(device_infos[0].bluetooth_address(),
            remote_devices_[0].bluetooth_address);
  EXPECT_EQ(0u, remote_devices_[0].beacon_seeds.size());
}

TEST_F(CryptAuthRemoteDeviceLoaderTest, LoadOneDeviceWithoutAddress) {
  std::vector<cryptauth::ExternalDeviceInfo> device_infos(1,
                                                         CreateDeviceInfo("0"));
  device_infos[0].set_bluetooth_address(std::string());
  RemoteDeviceLoader loader(device_infos, user_private_key_, kUserId,
                            std::move(secure_message_delegate_));

  std::vector<cryptauth::RemoteDevice> result;
  EXPECT_CALL(*this, LoadCompleted());
  loader.Load(
      false, base::Bind(&CryptAuthRemoteDeviceLoaderTest::OnRemoteDevicesLoaded,
                        base::Unretained(this)));

  EXPECT_EQ(1u, remote_devices_.size());
  EXPECT_FALSE(remote_devices_[0].persistent_symmetric_key.empty());
  EXPECT_EQ(device_infos[0].friendly_device_name(), remote_devices_[0].name);
  EXPECT_EQ(device_infos[0].public_key(), remote_devices_[0].public_key);
  EXPECT_EQ("", remote_devices_[0].bluetooth_address);
}

TEST_F(CryptAuthRemoteDeviceLoaderTest, LoadThreeRemoteDevices) {
  std::vector<cryptauth::ExternalDeviceInfo> device_infos;
  device_infos.push_back(CreateDeviceInfo("0"));
  device_infos.push_back(CreateDeviceInfo("1"));
  device_infos.push_back(CreateDeviceInfo("2"));

  // Devices 0 and 1 do not have a Bluetooth address, but device 2 does.
  device_infos[0].set_bluetooth_address(std::string());
  device_infos[1].set_bluetooth_address(std::string());

  RemoteDeviceLoader loader(device_infos, user_private_key_, kUserId,
                            std::move(secure_message_delegate_));

  EXPECT_CALL(*this, LoadCompleted());
  loader.Load(
      false, base::Bind(&CryptAuthRemoteDeviceLoaderTest::OnRemoteDevicesLoaded,
                        base::Unretained(this)));

  EXPECT_EQ(3u, remote_devices_.size());
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_FALSE(remote_devices_[i].persistent_symmetric_key.empty());
    EXPECT_EQ(device_infos[i].friendly_device_name(), remote_devices_[i].name);
    EXPECT_EQ(device_infos[i].public_key(), remote_devices_[i].public_key);
    EXPECT_EQ(device_infos[i].bluetooth_address(),
              remote_devices_[i].bluetooth_address);
  }
}

}  // namespace cryptauth
