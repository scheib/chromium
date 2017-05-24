// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/lock_screen_client.h"

#include "ash/public/interfaces/constants.mojom.h"
#include "chrome/browser/chromeos/login/lock/screen_locker.h"
#include "content/public/common/service_manager_connection.h"
#include "services/service_manager/public/cpp/connector.h"

namespace {
LockScreenClient* g_instance = nullptr;
}  // namespace

LockScreenClient::LockScreenClient() : binding_(this) {
  content::ServiceManagerConnection::GetForProcess()
      ->GetConnector()
      ->BindInterface(ash::mojom::kServiceName, &lock_screen_);
  // Register this object as the client interface implementation.
  lock_screen_->SetClient(binding_.CreateInterfacePtrAndBind());

  DCHECK(!g_instance);
  g_instance = this;
}

LockScreenClient::~LockScreenClient() {
  DCHECK_EQ(this, g_instance);
  g_instance = nullptr;
}

// static
LockScreenClient* LockScreenClient::Get() {
  return g_instance;
}

void LockScreenClient::AuthenticateUser(const AccountId& account_id,
                                        const std::string& hashed_password,
                                        bool authenticated_by_pin) {
  // TODO(xiaoyinh): Complete the implementation below.
  // It should be similar as SigninScreenHandler::HandleAuthenticateUser.
  chromeos::UserContext user_context(account_id);
  chromeos::Key key(chromeos::Key::KEY_TYPE_SALTED_SHA256_TOP_HALF,
                    std::string(), hashed_password);
  user_context.SetKey(key);
  user_context.SetIsUsingPin(authenticated_by_pin);
  chromeos::ScreenLocker::default_screen_locker()->Authenticate(user_context);
}

void LockScreenClient::ShowErrorMessage(int32_t login_attempts,
                                        const std::string& error_text,
                                        const std::string& help_link_text,
                                        int32_t help_topic_id) {
  lock_screen_->ShowErrorMessage(login_attempts, error_text, help_link_text,
                                 help_topic_id);
}

void LockScreenClient::ClearErrors() {
  lock_screen_->ClearErrors();
}
