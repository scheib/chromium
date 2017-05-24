// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_LOCK_SCREEN_CLIENT_H_
#define CHROME_BROWSER_UI_ASH_LOCK_SCREEN_CLIENT_H_

#include "ash/public/interfaces/lock_screen.mojom.h"
#include "base/macros.h"
#include "mojo/public/cpp/bindings/binding.h"

// Handles method calls delegated back to chrome from ash. Also notifies ash of
// relevant state changes in chrome.
class LockScreenClient : public ash::mojom::LockScreenClient {
 public:
  LockScreenClient();
  ~LockScreenClient() override;

  static LockScreenClient* Get();

  // ash::mojom::LockScreenClient:
  void AuthenticateUser(const AccountId& account_id,
                        const std::string& hashed_password,
                        bool authenticated_by_pin) override;

  // Wrappers around the mojom::SystemTray interface.
  void ShowErrorMessage(int32_t login_attempts,
                        const std::string& error_text,
                        const std::string& help_link_text,
                        int32_t help_topic_id);
  void ClearErrors();

 private:
  // Lock screen mojo service in ash.
  ash::mojom::LockScreenPtr lock_screen_;

  // Binds this object to the client interface.
  mojo::Binding<ash::mojom::LockScreenClient> binding_;

  DISALLOW_COPY_AND_ASSIGN(LockScreenClient);
};

#endif  // CHROME_BROWSER_UI_ASH_LOCK_SCREEN_CLIENT_H_
