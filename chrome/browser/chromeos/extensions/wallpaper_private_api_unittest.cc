// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/wallpaper_private_api.h"

#include <memory>

#include "ash/test/ash_test_base.h"
#include "ash/wm/window_state.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/scoped_user_manager_enabler.h"
#include "chrome/browser/chromeos/login/users/wallpaper/wallpaper_manager.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager_chromeos.h"
#include "components/signin/core/account_id/account_id.h"
#include "extensions/browser/api_test_utils.h"
#include "ui/aura/test/test_windows.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"

namespace chromeos {
namespace {

const char kTestAccount1[] = "user1@test.com";
const char kTestAccount2[] = "user2@test.com";

class WallpaperPrivateApiUnittest : public ash::test::AshTestBase {
 public:
  WallpaperPrivateApiUnittest()
      : fake_user_manager_(new FakeChromeUserManager()),
        scoped_user_manager_(fake_user_manager_) {}

 protected:
  FakeChromeUserManager* fake_user_manager() { return fake_user_manager_; }

  const AccountId test_account_id1_ = AccountId::FromUserEmail(kTestAccount1);
  const AccountId test_account_id2_ = AccountId::FromUserEmail(kTestAccount2);

 private:
  FakeChromeUserManager* fake_user_manager_;
  ScopedUserManagerEnabler scoped_user_manager_;

  DISALLOW_COPY_AND_ASSIGN(WallpaperPrivateApiUnittest);
};

bool RunMinimizeFunction() {
  scoped_refptr<WallpaperPrivateMinimizeInactiveWindowsFunction> function(
      new WallpaperPrivateMinimizeInactiveWindowsFunction());
  return extensions::api_test_utils::RunFunction(function.get(), "[]", nullptr);
}

bool RunRestoreFunction() {
  scoped_refptr<WallpaperPrivateRestoreMinimizedWindowsFunction> function(
      new WallpaperPrivateRestoreMinimizedWindowsFunction());
  return extensions::api_test_utils::RunFunction(function.get(), "[]", nullptr);
}

}  // namespace

TEST_F(WallpaperPrivateApiUnittest, HideAndRestoreWindows) {
  fake_user_manager()->AddUser(test_account_id1_);
  std::unique_ptr<aura::Window> window4(CreateTestWindowInShellWithId(4));
  std::unique_ptr<aura::Window> window3(CreateTestWindowInShellWithId(3));
  std::unique_ptr<aura::Window> window2(CreateTestWindowInShellWithId(2));
  std::unique_ptr<aura::Window> window1(CreateTestWindowInShellWithId(1));
  std::unique_ptr<aura::Window> window0(CreateTestWindowInShellWithId(0));

  ash::wm::WindowState* window0_state = ash::wm::GetWindowState(window0.get());
  ash::wm::WindowState* window1_state = ash::wm::GetWindowState(window1.get());
  ash::wm::WindowState* window2_state = ash::wm::GetWindowState(window2.get());
  ash::wm::WindowState* window3_state = ash::wm::GetWindowState(window3.get());
  ash::wm::WindowState* window4_state = ash::wm::GetWindowState(window4.get());

  window3_state->Minimize();
  window1_state->Maximize();

  // Window 3 starts minimized, window 1 starts maximized.
  EXPECT_FALSE(window0_state->IsMinimized());
  EXPECT_FALSE(window1_state->IsMinimized());
  EXPECT_FALSE(window2_state->IsMinimized());
  EXPECT_TRUE(window3_state->IsMinimized());
  EXPECT_FALSE(window4_state->IsMinimized());

  // We then activate window 0 (i.e. wallpaper picker) and call the minimize
  // function.
  window0_state->Activate();
  EXPECT_TRUE(window0_state->IsActive());
  EXPECT_TRUE(RunMinimizeFunction());

  // All windows except window 0 should be minimized.
  EXPECT_FALSE(window0_state->IsMinimized());
  EXPECT_TRUE(window1_state->IsMinimized());
  EXPECT_TRUE(window2_state->IsMinimized());
  EXPECT_TRUE(window3_state->IsMinimized());
  EXPECT_TRUE(window4_state->IsMinimized());

  // Activates window 4 and then minimizes it.
  window4_state->Activate();
  window4_state->Minimize();

  // Then we destroy window 0 and call the restore function.
  window0.reset();
  EXPECT_TRUE(RunRestoreFunction());

  // Windows 1 and 2 should no longer be minimized. Window 1 should again
  // be maximized. Window 3 should still be minimized. Window 4 should remain
  // minimized since user interacted with it while wallpaper picker was open.
  EXPECT_TRUE(window1_state->IsMaximized());
  EXPECT_FALSE(window2_state->IsMinimized());
  EXPECT_TRUE(window3_state->IsMinimized());
  EXPECT_TRUE(window4_state->IsMinimized());
}

// Test for multiple calls to |MinimizeInactiveWindows| before call
// |RestoreWindows|:
// 1. If all window hasn't change their states, the following calls are noops.
// 2. If some windows are manually unminimized, the following call will minimize
// all the unminimized windows.
TEST_F(WallpaperPrivateApiUnittest, HideAndManualUnminimizeWindows) {
  fake_user_manager()->AddUser(test_account_id1_);
  std::unique_ptr<aura::Window> window1(CreateTestWindowInShellWithId(1));
  std::unique_ptr<aura::Window> window0(CreateTestWindowInShellWithId(0));

  ash::wm::WindowState* window0_state = ash::wm::GetWindowState(window0.get());
  ash::wm::WindowState* window1_state = ash::wm::GetWindowState(window1.get());

  // We then activate window 0 (i.e. wallpaper picker) and call the minimize
  // function.
  window0_state->Activate();
  EXPECT_TRUE(window0_state->IsActive());
  EXPECT_TRUE(RunMinimizeFunction());

  // All windows except window 0 should be minimized.
  EXPECT_FALSE(window0_state->IsMinimized());
  EXPECT_TRUE(window1_state->IsMinimized());

  // Calls minimize function again should be an noop if window state didn't
  // change.
  EXPECT_TRUE(RunMinimizeFunction());

  // All windows except window 0 should be minimized.
  EXPECT_FALSE(window0_state->IsMinimized());
  EXPECT_TRUE(window1_state->IsMinimized());

  // Manually unminimize window 1.
  window1_state->Unminimize();
  EXPECT_FALSE(window1_state->IsMinimized());
  window0_state->Activate();

  EXPECT_TRUE(RunMinimizeFunction());

  // Window 1 should be minimized again.
  EXPECT_FALSE(window0_state->IsMinimized());
  EXPECT_TRUE(window1_state->IsMinimized());

  // Then we destroy window 0 and call the restore function.
  window0.reset();
  EXPECT_TRUE(RunRestoreFunction());

  // Windows 1 should no longer be minimized.
  EXPECT_FALSE(window1_state->IsMinimized());
}

class WallpaperPrivateApiMultiUserUnittest
    : public WallpaperPrivateApiUnittest {
 public:
  WallpaperPrivateApiMultiUserUnittest(): multi_user_window_manager_(nullptr) {}

  void SetUp() override;
  void TearDown() override;

 protected:
  void SetUpMultiUserWindowManager(
      const AccountId& active_account_id,
      chrome::MultiUserWindowManager::MultiProfileMode mode);

  void SwitchActiveUser(const AccountId& active_account_id);

  chrome::MultiUserWindowManagerChromeOS* multi_user_window_manager() {
    return multi_user_window_manager_;
  }

 private:
  chrome::MultiUserWindowManagerChromeOS* multi_user_window_manager_;

  DISALLOW_COPY_AND_ASSIGN(WallpaperPrivateApiMultiUserUnittest);
};

void WallpaperPrivateApiMultiUserUnittest::SetUp() {
  AshTestBase::SetUp();
  WallpaperManager::Initialize();
  fake_user_manager()->AddUser(test_account_id1_);
  fake_user_manager()->AddUser(test_account_id2_);
}

void WallpaperPrivateApiMultiUserUnittest::TearDown() {
  chrome::MultiUserWindowManager::DeleteInstance();
  AshTestBase::TearDown();
  WallpaperManager::Shutdown();
}

void WallpaperPrivateApiMultiUserUnittest::SetUpMultiUserWindowManager(
    const AccountId& active_account_id,
    chrome::MultiUserWindowManager::MultiProfileMode mode) {
  multi_user_window_manager_ =
      new chrome::MultiUserWindowManagerChromeOS(active_account_id);
  multi_user_window_manager_->Init();
  chrome::MultiUserWindowManager::SetInstanceForTest(
      multi_user_window_manager_, mode);
  // We do not want animations while the test is going on.
  multi_user_window_manager_->SetAnimationSpeedForTest(
      chrome::MultiUserWindowManagerChromeOS::ANIMATION_SPEED_DISABLED);
  EXPECT_TRUE(multi_user_window_manager_);
}

void WallpaperPrivateApiMultiUserUnittest::SwitchActiveUser(
    const AccountId& active_account_id) {
  fake_user_manager()->SwitchActiveUser(active_account_id);
  multi_user_window_manager_->ActiveUserChanged(
      fake_user_manager()->FindUser(active_account_id));
}

// In multi profile mode, user may open wallpaper picker in one profile and
// then switch to a different profile and open another wallpaper picker
// without closing the first one.
TEST_F(WallpaperPrivateApiMultiUserUnittest, HideAndRestoreWindowsTwoUsers) {
  SetUpMultiUserWindowManager(
      test_account_id1_,
      chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_SEPARATED);

  std::unique_ptr<aura::Window> window4(CreateTestWindowInShellWithId(4));
  std::unique_ptr<aura::Window> window3(CreateTestWindowInShellWithId(3));
  std::unique_ptr<aura::Window> window2(CreateTestWindowInShellWithId(2));
  std::unique_ptr<aura::Window> window1(CreateTestWindowInShellWithId(1));
  std::unique_ptr<aura::Window> window0(CreateTestWindowInShellWithId(0));

  ash::wm::WindowState* window0_state = ash::wm::GetWindowState(window0.get());
  ash::wm::WindowState* window1_state = ash::wm::GetWindowState(window1.get());
  ash::wm::WindowState* window2_state = ash::wm::GetWindowState(window2.get());
  ash::wm::WindowState* window3_state = ash::wm::GetWindowState(window3.get());
  ash::wm::WindowState* window4_state = ash::wm::GetWindowState(window4.get());

  multi_user_window_manager()->SetWindowOwner(window0.get(), test_account_id1_);
  multi_user_window_manager()->SetWindowOwner(window1.get(), test_account_id1_);

  // Set some windows to an inactive owner.
  multi_user_window_manager()->SetWindowOwner(window2.get(), test_account_id2_);
  multi_user_window_manager()->SetWindowOwner(window3.get(), test_account_id2_);
  multi_user_window_manager()->SetWindowOwner(window4.get(), test_account_id2_);

  EXPECT_FALSE(window0_state->IsMinimized());
  EXPECT_FALSE(window1_state->IsMinimized());
  EXPECT_FALSE(window2_state->IsMinimized());
  EXPECT_FALSE(window3_state->IsMinimized());
  EXPECT_FALSE(window4_state->IsMinimized());

  // We then activate window 0 (i.e. wallpaper picker) and call the minimize
  // function.
  window0_state->Activate();
  EXPECT_TRUE(window0_state->IsActive());
  EXPECT_TRUE(RunMinimizeFunction());

  // All windows except window 0 should be minimized.
  EXPECT_FALSE(window0_state->IsMinimized());
  EXPECT_TRUE(window1_state->IsMinimized());

  // All windows that belong to inactive user should not be affected.
  EXPECT_FALSE(window2_state->IsMinimized());
  EXPECT_FALSE(window3_state->IsMinimized());
  EXPECT_FALSE(window4_state->IsMinimized());

  // Activate kTestAccount2. kTestAccount1 becomes inactive user.
  SwitchActiveUser(test_account_id2_);

  window2_state->Activate();
  EXPECT_TRUE(window2_state->IsActive());
  EXPECT_TRUE(RunMinimizeFunction());

  // All windows except window 2 should be minimized.
  EXPECT_FALSE(window2_state->IsMinimized());
  EXPECT_TRUE(window3_state->IsMinimized());
  EXPECT_TRUE(window4_state->IsMinimized());

  // All windows that belong to inactive user should not be affected.
  EXPECT_FALSE(window0_state->IsMinimized());
  EXPECT_TRUE(window1_state->IsMinimized());

  // Destroy window 4. Nothing should happen to other windows.
  window4_state->Unminimize();
  window4.reset();

  EXPECT_FALSE(window2_state->IsMinimized());
  EXPECT_TRUE(window3_state->IsMinimized());
  EXPECT_FALSE(window0_state->IsMinimized());
  EXPECT_TRUE(window1_state->IsMinimized());

  // Then we destroy window 2 and call the restore function.
  window2.reset();
  EXPECT_TRUE(RunRestoreFunction());

  EXPECT_FALSE(window3_state->IsMinimized());

  // All windows that belong to inactive user should not be affected.
  EXPECT_FALSE(window0_state->IsMinimized());
  EXPECT_TRUE(window1_state->IsMinimized());

  SwitchActiveUser(test_account_id1_);

  // Then we destroy window 0 and call the restore function.
  window0.reset();
  EXPECT_TRUE(RunRestoreFunction());

  EXPECT_FALSE(window1_state->IsMinimized());
  EXPECT_FALSE(window3_state->IsMinimized());
}

// In multi profile mode, user may teleport windows. Teleported window should
// also be minimized when open wallpaper picker.
TEST_F(WallpaperPrivateApiMultiUserUnittest, HideTeleportedWindow) {
  SetUpMultiUserWindowManager(
      AccountId::FromUserEmail(kTestAccount1),
      chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_MIXED);

  std::unique_ptr<aura::Window> window3(CreateTestWindowInShellWithId(3));
  std::unique_ptr<aura::Window> window2(CreateTestWindowInShellWithId(2));
  std::unique_ptr<aura::Window> window1(CreateTestWindowInShellWithId(1));
  std::unique_ptr<aura::Window> window0(CreateTestWindowInShellWithId(0));

  ash::wm::WindowState* window0_state = ash::wm::GetWindowState(window0.get());
  ash::wm::WindowState* window1_state = ash::wm::GetWindowState(window1.get());
  ash::wm::WindowState* window2_state = ash::wm::GetWindowState(window2.get());
  ash::wm::WindowState* window3_state = ash::wm::GetWindowState(window3.get());

  multi_user_window_manager()->SetWindowOwner(window0.get(), test_account_id1_);
  multi_user_window_manager()->SetWindowOwner(window1.get(), test_account_id1_);

  // Set some windows to an inactive owner.
  multi_user_window_manager()->SetWindowOwner(window2.get(), test_account_id2_);
  multi_user_window_manager()->SetWindowOwner(window3.get(), test_account_id2_);

  // Teleport window2 to kTestAccount1.
  multi_user_window_manager()->ShowWindowForUser(window2.get(),
                                                 test_account_id1_);

  // Initial window state. All windows shouldn't be minimized.
  EXPECT_FALSE(window0_state->IsMinimized());
  EXPECT_FALSE(window1_state->IsMinimized());
  EXPECT_FALSE(window2_state->IsMinimized());
  EXPECT_FALSE(window3_state->IsMinimized());

  // We then activate window 0 (i.e. wallpaper picker) and call the minimize
  // function.
  window0_state->Activate();
  EXPECT_TRUE(window0_state->IsActive());
  EXPECT_TRUE(RunMinimizeFunction());

  // All windows except window 0 should be minimized.
  EXPECT_FALSE(window0_state->IsMinimized());
  EXPECT_TRUE(window1_state->IsMinimized());

  // Teleported window should also be minimized.
  EXPECT_TRUE(window2_state->IsMinimized());
  // Other window should remain the same.
  EXPECT_FALSE(window3_state->IsMinimized());

  // Then we destroy window 0 and call the restore function.
  window0.reset();
  EXPECT_TRUE(RunRestoreFunction());

  EXPECT_FALSE(window1_state->IsMinimized());
  EXPECT_FALSE(window2_state->IsMinimized());
  EXPECT_FALSE(window3_state->IsMinimized());
}

}  // namespace chromeos
