// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/tether/tether_host_fetcher.h"

#include <memory>
#include <vector>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "components/cryptauth/cryptauth_device_manager.h"
#include "components/cryptauth/cryptauth_enroller.h"
#include "components/cryptauth/cryptauth_enrollment_manager.h"
#include "components/cryptauth/fake_cryptauth_gcm_manager.h"
#include "components/cryptauth/fake_cryptauth_service.h"
#include "components/cryptauth/fake_secure_message_delegate.h"
#include "components/cryptauth/proto/cryptauth_api.pb.h"
#include "components/cryptauth/remote_device_loader.h"
#include "components/cryptauth/remote_device_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace chromeos {

namespace tether {

namespace {

const char kTestUserId[] = "testUserId";
const char kTestUserPrivateKey[] = "kTestUserPrivateKey";

class MockCryptAuthDeviceManager : public cryptauth::CryptAuthDeviceManager {
 public:
  ~MockCryptAuthDeviceManager() override {}

  MOCK_CONST_METHOD0(GetTetherHosts,
                     std::vector<cryptauth::ExternalDeviceInfo>());
};

class MockCryptAuthEnrollmentManager
    : public cryptauth::CryptAuthEnrollmentManager {
 public:
  explicit MockCryptAuthEnrollmentManager(
      cryptauth::FakeCryptAuthGCMManager* fake_cryptauth_gcm_manager)
      : cryptauth::CryptAuthEnrollmentManager(
            nullptr /* clock */,
            nullptr /* enroller_factory */,
            nullptr /* secure_message_delegate */,
            cryptauth::GcmDeviceInfo(),
            fake_cryptauth_gcm_manager,
            nullptr /* pref_service */) {}
  ~MockCryptAuthEnrollmentManager() override {}

  MOCK_CONST_METHOD0(GetUserPrivateKey, std::string());
};

class FakeCryptAuthServiceWithTracking
    : public cryptauth::FakeCryptAuthService {
 public:
  // FakeCryptAuthService:
  std::unique_ptr<cryptauth::SecureMessageDelegate>
  CreateSecureMessageDelegate() override {
    cryptauth::FakeSecureMessageDelegate* delegate =
        new cryptauth::FakeSecureMessageDelegate();
    created_delegates_.push_back(delegate);
    return base::WrapUnique(delegate);
  }

  void VerifySecureMessageDelegateCreatedByFactory(
      cryptauth::FakeSecureMessageDelegate* delegate) {
    EXPECT_FALSE(std::find(created_delegates_.begin(), created_delegates_.end(),
                           delegate) == created_delegates_.end());
  }

 private:
  std::vector<cryptauth::FakeSecureMessageDelegate*> created_delegates_;
};

class MockDeviceLoader : public cryptauth::RemoteDeviceLoader {
 public:
  MockDeviceLoader()
      : cryptauth::RemoteDeviceLoader(
            std::vector<cryptauth::ExternalDeviceInfo>(),
            "",
            "",
            nullptr) {}
  ~MockDeviceLoader() override {}

  MOCK_METHOD2(
      Load,
      void(bool, const cryptauth::RemoteDeviceLoader::RemoteDeviceCallback&));
};

std::vector<cryptauth::ExternalDeviceInfo>
CreateTetherExternalDeviceInfosForRemoteDevices(
    const std::vector<cryptauth::RemoteDevice> remote_devices) {
  std::vector<cryptauth::ExternalDeviceInfo> device_infos;
  for (const auto& remote_device : remote_devices) {
    // Add an ExternalDeviceInfo with the same public key as the RemoteDevice.
    cryptauth::ExternalDeviceInfo info;
    info.set_public_key(remote_device.public_key);
    device_infos.push_back(info);
  }
  return device_infos;
}

}  // namespace

class TetherHostFetcherTest : public testing::Test {
 public:
  class TestRemoteDeviceLoaderFactory
      : public cryptauth::RemoteDeviceLoader::Factory {
   public:
    explicit TestRemoteDeviceLoaderFactory(TetherHostFetcherTest* test)
        : test_(test) {}

    std::unique_ptr<cryptauth::RemoteDeviceLoader> BuildInstance(
        const std::vector<cryptauth::ExternalDeviceInfo>& device_info_list,
        const std::string& user_id,
        const std::string& user_private_key,
        std::unique_ptr<cryptauth::SecureMessageDelegate>
            secure_message_delegate) override {
      EXPECT_EQ(test_->test_device_infos_.size(), device_info_list.size());
      EXPECT_EQ(std::string(kTestUserId), user_id);
      EXPECT_EQ(std::string(kTestUserPrivateKey), user_private_key);
      test_->fake_cryptauth_service_
          ->VerifySecureMessageDelegateCreatedByFactory(
              static_cast<cryptauth::FakeSecureMessageDelegate*>(
                  secure_message_delegate.get()));

      std::unique_ptr<MockDeviceLoader> device_loader =
          base::WrapUnique(new NiceMock<MockDeviceLoader>());
      ON_CALL(*device_loader, Load(false, _))
          .WillByDefault(
              Invoke(this, &TestRemoteDeviceLoaderFactory::MockLoadImpl));
      return std::move(device_loader);
    }

    void MockLoadImpl(
        bool should_load_beacon_seeds,
        const cryptauth::RemoteDeviceLoader::RemoteDeviceCallback& callback) {
      callback_ = callback;
    }

    void InvokeLastCallback() {
      DCHECK(!callback_.is_null());
      callback_.Run(test_->test_devices_);
    }

   private:
    cryptauth::RemoteDeviceLoader::RemoteDeviceCallback callback_;
    TetherHostFetcherTest* test_;
  };

  TetherHostFetcherTest()
      : test_devices_(cryptauth::GenerateTestRemoteDevices(5)),
        test_device_infos_(
            CreateTetherExternalDeviceInfosForRemoteDevices(test_devices_)) {}

  void SetUp() override {
    device_list_list_.clear();
    single_device_list_.clear();

    mock_device_manager_ =
        base::WrapUnique(new NiceMock<MockCryptAuthDeviceManager>());
    ON_CALL(*mock_device_manager_, GetTetherHosts())
        .WillByDefault(Return(test_device_infos_));

    fake_cryptauth_gcm_manager_ =
        base::MakeUnique<cryptauth::FakeCryptAuthGCMManager>("registrationId");
    mock_enrollment_manager_ =
        base::WrapUnique(new NiceMock<MockCryptAuthEnrollmentManager>(
            fake_cryptauth_gcm_manager_.get()));
    ON_CALL(*mock_enrollment_manager_, GetUserPrivateKey())
        .WillByDefault(Return(kTestUserPrivateKey));

    fake_cryptauth_service_ =
        base::WrapUnique(new FakeCryptAuthServiceWithTracking());
    fake_cryptauth_service_->set_account_id(kTestUserId);
    fake_cryptauth_service_->set_cryptauth_device_manager(
        mock_device_manager_.get());
    fake_cryptauth_service_->set_cryptauth_enrollment_manager(
        mock_enrollment_manager_.get());

    test_device_loader_factory_ =
        base::WrapUnique(new TestRemoteDeviceLoaderFactory(this));
    cryptauth::RemoteDeviceLoader::Factory::SetInstanceForTesting(
        test_device_loader_factory_.get());

    tether_host_fetcher_ =
        base::MakeUnique<TetherHostFetcher>(fake_cryptauth_service_.get());
  }

  void OnTetherHostListFetched(const cryptauth::RemoteDeviceList& device_list) {
    device_list_list_.push_back(device_list);
  }

  void OnSingleTetherHostFetched(
      std::unique_ptr<cryptauth::RemoteDevice> device) {
    single_device_list_.push_back(std::move(device));
  }

  const std::vector<cryptauth::RemoteDevice> test_devices_;
  const std::vector<cryptauth::ExternalDeviceInfo> test_device_infos_;

  std::vector<cryptauth::RemoteDeviceList> device_list_list_;
  std::vector<std::shared_ptr<cryptauth::RemoteDevice>> single_device_list_;

  std::unique_ptr<FakeCryptAuthServiceWithTracking> fake_cryptauth_service_;
  std::unique_ptr<cryptauth::FakeCryptAuthGCMManager>
      fake_cryptauth_gcm_manager_;
  std::unique_ptr<NiceMock<MockCryptAuthDeviceManager>> mock_device_manager_;
  std::unique_ptr<NiceMock<MockCryptAuthEnrollmentManager>>
      mock_enrollment_manager_;
  std::unique_ptr<TestRemoteDeviceLoaderFactory> test_device_loader_factory_;

  std::unique_ptr<TetherHostFetcher> tether_host_fetcher_;

 private:
  DISALLOW_COPY_AND_ASSIGN(TetherHostFetcherTest);
};

TEST_F(TetherHostFetcherTest, TestFetchAllTetherHosts) {
  tether_host_fetcher_->FetchAllTetherHosts(base::Bind(
      &TetherHostFetcherTest::OnTetherHostListFetched, base::Unretained(this)));

  // Nothing should load until the callback is invoked.
  EXPECT_EQ(0u, device_list_list_.size());
  EXPECT_EQ(0u, single_device_list_.size());

  test_device_loader_factory_->InvokeLastCallback();

  EXPECT_EQ(1u, device_list_list_.size());
  EXPECT_EQ(test_devices_, device_list_list_[0]);
  EXPECT_EQ(0u, single_device_list_.size());
}

TEST_F(TetherHostFetcherTest, TestSingleTetherHost) {
  tether_host_fetcher_->FetchTetherHost(
      test_devices_[0].GetDeviceId(),
      base::Bind(&TetherHostFetcherTest::OnSingleTetherHostFetched,
                 base::Unretained(this)));

  // Nothing should load until the callback is invoked.
  EXPECT_EQ(0u, device_list_list_.size());
  EXPECT_EQ(0u, single_device_list_.size());

  test_device_loader_factory_->InvokeLastCallback();

  EXPECT_EQ(0u, device_list_list_.size());
  EXPECT_EQ(1u, single_device_list_.size());
  EXPECT_EQ(test_devices_[0], *single_device_list_[0]);
}

TEST_F(TetherHostFetcherTest,
       TestSingleTetherHost_IdDoesNotCorrespondToDevice) {
  tether_host_fetcher_->FetchTetherHost(
      "idWhichDoesNotCorrespondToADevice",
      base::Bind(&TetherHostFetcherTest::OnSingleTetherHostFetched,
                 base::Unretained(this)));

  // Nothing should load until the callback is invoked.
  EXPECT_EQ(0u, device_list_list_.size());
  EXPECT_EQ(0u, single_device_list_.size());

  test_device_loader_factory_->InvokeLastCallback();

  EXPECT_EQ(0u, device_list_list_.size());
  EXPECT_EQ(1u, single_device_list_.size());
  EXPECT_EQ(nullptr, single_device_list_[0]);
}

TEST_F(TetherHostFetcherTest, TestMultipleSimultaneousRequests) {
  // Make the requests in the three previous tests at the same time.
  tether_host_fetcher_->FetchAllTetherHosts(base::Bind(
      &TetherHostFetcherTest::OnTetherHostListFetched, base::Unretained(this)));
  tether_host_fetcher_->FetchTetherHost(
      test_devices_[0].GetDeviceId(),
      base::Bind(&TetherHostFetcherTest::OnSingleTetherHostFetched,
                 base::Unretained(this)));
  tether_host_fetcher_->FetchTetherHost(
      "idWhichDoesNotCorrespondToADevice",
      base::Bind(&TetherHostFetcherTest::OnSingleTetherHostFetched,
                 base::Unretained(this)));

  // Nothing should load until the callback is invoked.
  EXPECT_EQ(0u, device_list_list_.size());
  EXPECT_EQ(0u, single_device_list_.size());

  test_device_loader_factory_->InvokeLastCallback();

  EXPECT_EQ(1u, device_list_list_.size());
  EXPECT_EQ(test_devices_, device_list_list_[0]);
  EXPECT_EQ(2u, single_device_list_.size());
  EXPECT_EQ(test_devices_[0], *single_device_list_[0]);
  EXPECT_EQ(nullptr, single_device_list_[1]);
}

}  // namespace tether

}  // namespace chromeos
