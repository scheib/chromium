// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/login/lock_screen_controller.h"

#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "base/run_loop.h"
#include "chromeos/cryptohome/system_salt_getter.h"

namespace ash {

namespace {

class TestLockScreenClient : public mojom::LockScreenClient {
 public:
  TestLockScreenClient() : binding_(this) {}
  ~TestLockScreenClient() override = default;

  mojom::LockScreenClientPtr CreateInterfacePtrAndBind() {
    return binding_.CreateInterfacePtrAndBind();
  }

  // mojom::LockScreenClient:
  void AuthenticateUser(const AccountId& account_id,
                        const std::string& password,
                        bool authenticated_by_pin) override {
    ++autentication_requests_count_;
  }

  int authentication_requests_count() const {
    return autentication_requests_count_;
  }

 private:
  mojo::Binding<ash::mojom::LockScreenClient> binding_;
  int autentication_requests_count_ = 0;

  DISALLOW_COPY_AND_ASSIGN(TestLockScreenClient);
};

using LockScreenControllerTest = test::AshTestBase;

}  // namespace

TEST_F(LockScreenControllerTest, RequestAuthentication) {
  LockScreenController* lock_screen_controller =
      Shell::Get()->lock_screen_controller();
  TestLockScreenClient lock_screen_client;
  lock_screen_controller->SetClient(
      lock_screen_client.CreateInterfacePtrAndBind());
  EXPECT_EQ(0, lock_screen_client.authentication_requests_count());

  AccountId id = AccountId::FromUserEmail("user1@test.com");
  lock_screen_controller->AuthenticateUser(id, std::string(), false);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, lock_screen_client.authentication_requests_count());
}

}  // namespace ash
