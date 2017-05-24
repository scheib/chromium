// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/login/lock_screen_controller.h"

#include "chromeos/cryptohome/system_salt_getter.h"
#include "chromeos/login/auth/user_context.h"

namespace ash {

LockScreenController::LockScreenController() = default;

LockScreenController::~LockScreenController() = default;

void LockScreenController::BindRequest(mojom::LockScreenRequest request) {
  bindings_.AddBinding(this, std::move(request));
}

void LockScreenController::AuthenticateUser(const AccountId& account_id,
                                            const std::string& password,
                                            bool authenticated_by_pin) {
  if (!lock_screen_client_)
    return;

  chromeos::SystemSaltGetter::Get()->GetSystemSalt(base::Bind(
      &LockScreenController::DoAuthenticateUser, base::Unretained(this),
      account_id, password, authenticated_by_pin));
}

void LockScreenController::SetClient(mojom::LockScreenClientPtr client) {
  lock_screen_client_ = std::move(client);
}

void LockScreenController::ShowErrorMessage(int32_t login_attempts,
                                            const std::string& error_text,
                                            const std::string& help_link_text,
                                            int32_t help_topic_id) {
  NOTIMPLEMENTED();
}

void LockScreenController::ClearErrors() {
  NOTIMPLEMENTED();
}

void LockScreenController::DoAuthenticateUser(const AccountId& account_id,
                                              const std::string& password,
                                              bool authenticated_by_pin,
                                              const std::string& system_salt) {
  // Hash password before sending through mojo.
  // TODO(xiaoyinh): Pin is hashed differently by using a different salt and
  // a different hash algorithm. Update this part in PinStorage.
  chromeos::Key key(password);
  key.Transform(chromeos::Key::KEY_TYPE_SALTED_SHA256_TOP_HALF, system_salt);
  lock_screen_client_->AuthenticateUser(account_id, key.GetSecret(),
                                        authenticated_by_pin);
}

}  // namespace ash
