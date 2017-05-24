// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "ash/content/shell_content_state.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/shelf/shelf_widget.h"
#include "ash/shelf/wm_shelf.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/ash_test_environment_content.h"
#include "ash/test/ash_test_helper.h"
#include "ash/test/test_shell_delegate.h"
#include "ash/wm/maximize_mode/maximize_mode_controller.h"
#include "ash/wm/maximize_mode/maximize_mode_window_manager.h"
#include "ash/wm/mru_window_tracker.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm/wm_event.h"
#include "ash/wm_window.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/scoped_user_manager_enabler.h"
#include "chrome/browser/chromeos/login/users/wallpaper/wallpaper_manager.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/ui/ash/chrome_new_window_client.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager_chromeos.h"
#include "chrome/browser/ui/ash/multi_user/user_switch_animator_chromeos.h"
#include "chrome/browser/ui/ash/session_controller_client.h"
#include "chrome/browser/ui/ash/session_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/test/base/test_browser_window_aura.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/user_manager/user_info.h"
#include "components/user_manager/user_manager.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/base/ui_base_types.h"
#include "ui/wm/core/window_modality_controller.h"
#include "ui/wm/core/window_util.h"
#include "ui/wm/public/activation_client.h"

namespace {

const char kAAccountIdString[] =
    "{\"account_type\":\"unknown\",\"email\":\"A\"}";
const char kBAccountIdString[] =
    "{\"account_type\":\"unknown\",\"email\":\"B\"}";
const char kArrowBAccountIdString[] =
    "->{\"account_type\":\"unknown\",\"email\":\"B\"}";

// TOOD(beng): This implementation seems only superficially different to the
//             production impl. Evaluate whether or not we can just use that
//             one.
class TestShellContentState : public ash::ShellContentState {
 public:
  TestShellContentState() {}
  ~TestShellContentState() override {}

 private:
  content::BrowserContext* GetActiveBrowserContext() override {
    const user_manager::UserManager* user_manager =
        user_manager::UserManager::Get();
    const user_manager::User* active_user = user_manager->GetActiveUser();
    return active_user ? multi_user_util::GetProfileFromAccountId(
                             active_user->GetAccountId())
                       : NULL;
  }

  content::BrowserContext* GetBrowserContextByIndex(
      ash::UserIndex index) override {
    return nullptr;
  }

  content::BrowserContext* GetBrowserContextForWindow(
      aura::Window* window) override {
    const AccountId& account_id =
        chrome::MultiUserWindowManager::GetInstance()->GetWindowOwner(window);
    return account_id.is_valid()
               ? multi_user_util::GetProfileFromAccountId(account_id)
               : nullptr;
  }

  content::BrowserContext* GetUserPresentingBrowserContextForWindow(
      aura::Window* window) override {
    const AccountId& account_id =
        chrome::MultiUserWindowManager::GetInstance()->GetUserPresentingWindow(
            window);
    return account_id.is_valid()
               ? multi_user_util::GetProfileFromAccountId(account_id)
               : nullptr;
  }

  DISALLOW_COPY_AND_ASSIGN(TestShellContentState);
};

class TestShellDelegateChromeOS : public ash::test::TestShellDelegate {
 public:
  TestShellDelegateChromeOS() {}
  ash::test::TestSessionStateDelegate* CreateSessionStateDelegate() override {
    return new ash::test::TestSessionStateDelegate;
  }

  bool CanShowWindowForUser(ash::WmWindow* window) const override {
    return ::CanShowWindowForUser(
        ash::WmWindow::GetAuraWindow(window),
        base::Bind(&ash::ShellContentState::GetActiveBrowserContext,
                   base::Unretained(ash::ShellContentState::GetInstance())));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestShellDelegateChromeOS);
};

std::unique_ptr<Browser> CreateTestBrowser(aura::Window* window,
                                           const gfx::Rect& bounds,
                                           Browser::CreateParams* params) {
  if (!bounds.IsEmpty())
    window->SetBounds(bounds);
  std::unique_ptr<Browser> browser =
      chrome::CreateBrowserWithAuraTestWindowForParams(base::WrapUnique(window),
                                                       params);
  return browser;
}

}  // namespace

namespace ash {
namespace test {

// A test class for preparing the chrome::MultiUserWindowManager. It creates
// various windows and instantiates the chrome::MultiUserWindowManager.
class MultiUserWindowManagerChromeOSTest : public AshTestBase {
 public:
  MultiUserWindowManagerChromeOSTest()
      : multi_user_window_manager_(NULL),
        fake_user_manager_(new chromeos::FakeChromeUserManager),
        user_manager_enabler_(fake_user_manager_) {}

  void SetUp() override;
  void TearDown() override;

 protected:
  // Set up the test environment for this many windows.
  void SetUpForThisManyWindows(int windows);

  // Switch the user and wait until the animation is finished.
  void SwitchUserAndWaitForAnimation(const AccountId& account_id) {
    multi_user_window_manager_->ActiveUserChanged(EnsureTestUser(account_id));
    base::TimeTicks now = base::TimeTicks::Now();
    while (multi_user_window_manager_->IsAnimationRunningForTest()) {
      // This should never take longer then a second.
      ASSERT_GE(1000, (base::TimeTicks::Now() - now).InMilliseconds());
      base::RunLoop().RunUntilIdle();
    }
  }

  // Return the window with the given index.
  aura::Window* window(size_t index) {
    DCHECK(index < window_.size());
    return window_[index];
  }

  // Delete the window at the given index, and set the referefence to NULL.
  void delete_window_at(size_t index) {
    delete window_[index];
    window_[index] = NULL;
  }

  // The accessor to the MultiWindowManager.
  chrome::MultiUserWindowManagerChromeOS* multi_user_window_manager() {
    return multi_user_window_manager_;
  }

  chromeos::FakeChromeUserManager* user_manager() { return fake_user_manager_; }

  TestingProfileManager* profile_manager() { return profile_manager_.get(); }

  // Ensures that a user with the given |account_id| exists.
  const user_manager::User* EnsureTestUser(const AccountId& account_id) {
    const user_manager::User* user = fake_user_manager_->FindUser(account_id);
    if (!user)
      user = fake_user_manager_->AddUser(account_id);
    return user;
  }

  const user_manager::User* AddTestUser(const AccountId& account_id) {
    const user_manager::User* user = EnsureTestUser(account_id);
    fake_user_manager_->LoginUser(account_id);
    TestingProfile* profile =
        profile_manager()->CreateTestingProfile(account_id.GetUserEmail());
    chromeos::ProfileHelper::Get()->SetUserToProfileMappingForTesting(user,
                                                                      profile);
    return user;
  }

  // Returns a list of all open windows in the following form:
  // "<H(idden)/S(hown)/D(eleted)>[<Owner>[,<shownForUser>]], .."
  // Like: "S[B], .." would mean that window#0 is shown and belongs to user B.
  // or "S[B,A], .." would mean that window#0 is shown, belongs to B but is
  // shown by A, and "D,..." would mean that window#0 is deleted.
  std::string GetStatus();

  // Returns a test-friendly string format of GetOwnersOfVisibleWindows().
  std::string GetOwnersOfVisibleWindowsAsString();

  // Make a window system modal.
  void MakeWindowSystemModal(aura::Window* window) {
    aura::Window* system_modal_container =
        window->GetRootWindow()->GetChildById(
            kShellWindowId_SystemModalContainer);
    system_modal_container->AddChild(window);
  }

  void ShowWindowForUserNoUserTransition(aura::Window* window,
                                         const AccountId& account_id) {
    multi_user_window_manager_->ShowWindowForUserIntern(window, account_id);
  }

  // The FakeChromeUserManager does not automatically call the window
  // manager. This function gets the current user from it and also sets it to
  // the multi user window manager.
  AccountId GetAndValidateCurrentUserFromSessionStateObserver() {
    const AccountId account_id =
        user_manager()->GetActiveUser()->GetAccountId();
    if (account_id != multi_user_window_manager_->GetCurrentUserForTest()) {
      multi_user_window_manager()->ActiveUserChanged(
          fake_user_manager_->FindUser(account_id));
    }

    return account_id;
  }

  // Initiate a user transition.
  void StartUserTransitionAnimation(const AccountId& account_id) {
    // Ensures a user exists for the ActiveUserChanged call but do not make the
    // user as logged in. The tests that call StartUserTransitionAnimation do
    // not need a logged in user. Otherwise, profile switch also needs to
    // be simulated so that CanShowWindowForUser works correctly.
    multi_user_window_manager_->ActiveUserChanged(EnsureTestUser(account_id));
  }

  // Call next animation step.
  void AdvanceUserTransitionAnimation() {
    multi_user_window_manager_->animation_->AdvanceUserTransitionAnimation();
  }

  // Return the user id of the wallpaper which is currently set.
  const std::string& GetWallpaperUserIdForTest() {
    return multi_user_window_manager_->animation_->wallpaper_user_id_for_test();
  }

  // Returns true if the given window covers the screen.
  bool CoversScreen(aura::Window* window) {
    return chrome::UserSwitchAnimatorChromeOS::CoversScreen(window);
  }

  // Create a maximize mode window manager.
  MaximizeModeWindowManager* CreateMaximizeModeWindowManager() {
    EXPECT_FALSE(maximize_mode_window_manager());
    Shell::Get()->maximize_mode_controller()->EnableMaximizeModeWindowManager(
        true);
    return maximize_mode_window_manager();
  }

  MaximizeModeWindowManager* maximize_mode_window_manager() {
    return Shell::Get()
        ->maximize_mode_controller()
        ->maximize_mode_window_manager_.get();
  }

 private:
  // These get created for each session.
  std::vector<aura::Window*> window_;

  // The instance of the MultiUserWindowManager.
  chrome::MultiUserWindowManagerChromeOS* multi_user_window_manager_;

  // Owned by |user_manager_enabler_|.
  chromeos::FakeChromeUserManager* fake_user_manager_ = nullptr;

  std::unique_ptr<TestingProfileManager> profile_manager_;

  chromeos::ScopedUserManagerEnabler user_manager_enabler_;

  // The maximized window manager (if enabled).
  std::unique_ptr<MaximizeModeWindowManager> maximize_mode_window_manager_;

  DISALLOW_COPY_AND_ASSIGN(MultiUserWindowManagerChromeOSTest);
};

void MultiUserWindowManagerChromeOSTest::SetUp() {
  ash_test_helper()->set_test_shell_delegate(new TestShellDelegateChromeOS);
  ash::test::AshTestEnvironmentContent* test_environment =
      static_cast<ash::test::AshTestEnvironmentContent*>(
          ash_test_helper()->ash_test_environment());
  test_environment->set_content_state(new ::TestShellContentState);
  AshTestBase::SetUp();
  profile_manager_.reset(
      new TestingProfileManager(TestingBrowserProcess::GetGlobal()));
  ASSERT_TRUE(profile_manager_.get()->SetUp());
  EnsureTestUser(AccountId::FromUserEmail("a"));
  EnsureTestUser(AccountId::FromUserEmail("b"));
  EnsureTestUser(AccountId::FromUserEmail("c"));
}

void MultiUserWindowManagerChromeOSTest::SetUpForThisManyWindows(int windows) {
  DCHECK(window_.empty());
  for (int i = 0; i < windows; i++) {
    window_.push_back(CreateTestWindowInShellWithId(i));
    window_[i]->Show();
  }
  multi_user_window_manager_ =
      new chrome::MultiUserWindowManagerChromeOS(AccountId::FromUserEmail("A"));
  multi_user_window_manager_->Init();
  multi_user_window_manager_->SetAnimationSpeedForTest(
      chrome::MultiUserWindowManagerChromeOS::ANIMATION_SPEED_DISABLED);
  chrome::MultiUserWindowManager::SetInstanceForTest(multi_user_window_manager_,
        chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_SEPARATED);
  EXPECT_TRUE(multi_user_window_manager_);
  chromeos::WallpaperManager::Initialize();
}

void MultiUserWindowManagerChromeOSTest::TearDown() {
  // Since the AuraTestBase is needed to create our assets, we have to
  // also delete them before we tear it down.
  while (!window_.empty()) {
    delete *(window_.begin());
    window_.erase(window_.begin());
  }

  chrome::MultiUserWindowManager::DeleteInstance();
  AshTestBase::TearDown();
  chromeos::WallpaperManager::Shutdown();
  profile_manager_.reset();
}

std::string MultiUserWindowManagerChromeOSTest::GetStatus() {
  std::string s;
  for (size_t i = 0; i < window_.size(); i++) {
    if (i)
      s += ", ";
    if (!window(i)) {
      s += "D";
      continue;
    }
    s += window(i)->IsVisible() ? "S[" : "H[";
    const AccountId& owner =
        multi_user_window_manager_->GetWindowOwner(window(i));
    s += owner.GetUserEmail();
    const AccountId& presenter =
        multi_user_window_manager_->GetUserPresentingWindow(window(i));
    if (!owner.empty() && owner != presenter) {
      s += ",";
      s += presenter.GetUserEmail();
    }
    s += "]";
  }
  return s;
}

std::string
MultiUserWindowManagerChromeOSTest::GetOwnersOfVisibleWindowsAsString() {
  std::set<AccountId> owners;
  multi_user_window_manager_->GetOwnersOfVisibleWindows(&owners);

  std::vector<base::StringPiece> owner_list;
  for (auto& owner : owners)
    owner_list.push_back(owner.GetUserEmail());
  return base::JoinString(owner_list, " ");
}

// Testing basic assumptions like default state and existence of manager.
TEST_F(MultiUserWindowManagerChromeOSTest, BasicTests) {
  SetUpForThisManyWindows(3);
  // Check the basic assumptions: All windows are visible and there is no owner.
  EXPECT_EQ("S[], S[], S[]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager());
  EXPECT_EQ(multi_user_window_manager(),
            chrome::MultiUserWindowManager::GetInstance());
  EXPECT_FALSE(multi_user_window_manager()->AreWindowsSharedAmongUsers());

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));

  // The owner of an unowned window should be empty and it should be shown on
  // all windows.
  EXPECT_FALSE(
      multi_user_window_manager()->GetWindowOwner(window(0)).is_valid());
  EXPECT_FALSE(multi_user_window_manager()
                   ->GetUserPresentingWindow(window(0))
                   .is_valid());
  EXPECT_TRUE(multi_user_window_manager()->IsWindowOnDesktopOfUser(
      window(0), account_id_A));
  EXPECT_TRUE(multi_user_window_manager()->IsWindowOnDesktopOfUser(
      window(0), account_id_B));

  // Set the owner of one window should remember it as such. It should only be
  // drawn on the owners desktop - not on any other.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  EXPECT_EQ(account_id_A,
            multi_user_window_manager()->GetWindowOwner(window(0)));
  EXPECT_EQ(account_id_A,
            multi_user_window_manager()->GetUserPresentingWindow(window(0)));
  EXPECT_TRUE(multi_user_window_manager()->IsWindowOnDesktopOfUser(
      window(0), account_id_A));
  EXPECT_FALSE(multi_user_window_manager()->IsWindowOnDesktopOfUser(
      window(0), account_id_B));

  // Overriding it with another state should show it on the other user's
  // desktop.
  ShowWindowForUserNoUserTransition(window(0), account_id_B);
  EXPECT_EQ(account_id_A,
            multi_user_window_manager()->GetWindowOwner(window(0)));
  EXPECT_EQ(account_id_B,
            multi_user_window_manager()->GetUserPresentingWindow(window(0)));
  EXPECT_FALSE(multi_user_window_manager()->IsWindowOnDesktopOfUser(
      window(0), account_id_A));
  EXPECT_TRUE(multi_user_window_manager()->IsWindowOnDesktopOfUser(
      window(0), account_id_B));
}

// Testing simple owner changes.
TEST_F(MultiUserWindowManagerChromeOSTest, OwnerTests) {
  SetUpForThisManyWindows(5);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));
  const AccountId account_id_C(AccountId::FromUserEmail("C"));

  // Set some windows to the active owner.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  EXPECT_EQ("S[A], S[], S[], S[], S[]", GetStatus());
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_A);
  EXPECT_EQ("S[A], S[], S[A], S[], S[]", GetStatus());

  // Set some windows to an inactive owner. Note that the windows should hide.
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_B);
  EXPECT_EQ("S[A], H[B], S[A], S[], S[]", GetStatus());
  multi_user_window_manager()->SetWindowOwner(window(3), account_id_B);
  EXPECT_EQ("S[A], H[B], S[A], H[B], S[]", GetStatus());

  // Assume that the user has now changed to C - which should show / hide
  // accordingly.
  StartUserTransitionAnimation(account_id_C);
  EXPECT_EQ("H[A], H[B], H[A], H[B], S[]", GetStatus());

  // If someone tries to show an inactive window it should only work if it can
  // be shown / hidden.
  StartUserTransitionAnimation(account_id_A);
  EXPECT_EQ("S[A], H[B], S[A], H[B], S[]", GetStatus());
  window(3)->Show();
  EXPECT_EQ("S[A], H[B], S[A], H[B], S[]", GetStatus());
  window(2)->Hide();
  EXPECT_EQ("S[A], H[B], H[A], H[B], S[]", GetStatus());
  window(2)->Show();
  EXPECT_EQ("S[A], H[B], S[A], H[B], S[]", GetStatus());
}

TEST_F(MultiUserWindowManagerChromeOSTest, CloseWindowTests) {
  SetUpForThisManyWindows(1);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));

  multi_user_window_manager()->SetWindowOwner(window(0), account_id_B);
  EXPECT_EQ("H[B]", GetStatus());
  ShowWindowForUserNoUserTransition(window(0), account_id_A);
  EXPECT_EQ("S[B,A]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager()->AreWindowsSharedAmongUsers());
  EXPECT_EQ("B", GetOwnersOfVisibleWindowsAsString());

  aura::Window* to_be_deleted = window(0);

  EXPECT_EQ(account_id_A, multi_user_window_manager()->GetUserPresentingWindow(
                              to_be_deleted));
  EXPECT_EQ(account_id_B,
            multi_user_window_manager()->GetWindowOwner(to_be_deleted));

  // Close the window.
  delete_window_at(0);

  EXPECT_EQ("D", GetStatus());
  EXPECT_EQ("", GetOwnersOfVisibleWindowsAsString());
  // There should be no owner anymore for that window and the shared windows
  // should be gone as well.
  EXPECT_FALSE(multi_user_window_manager()
                   ->GetUserPresentingWindow(to_be_deleted)
                   .is_valid());
  EXPECT_FALSE(
      multi_user_window_manager()->GetWindowOwner(to_be_deleted).is_valid());
}

TEST_F(MultiUserWindowManagerChromeOSTest, SharedWindowTests) {
  SetUpForThisManyWindows(5);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));
  const AccountId account_id_C(AccountId::FromUserEmail("C"));

  // Set some owners and make sure we got what we asked for.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_B);
  multi_user_window_manager()->SetWindowOwner(window(3), account_id_B);
  multi_user_window_manager()->SetWindowOwner(window(4), account_id_C);
  EXPECT_EQ("S[A], S[A], H[B], H[B], H[C]", GetStatus());
  EXPECT_FALSE(multi_user_window_manager()->AreWindowsSharedAmongUsers());
  EXPECT_EQ("A", GetOwnersOfVisibleWindowsAsString());

  // For all following tests we override window 2 to be shown by user B.
  ShowWindowForUserNoUserTransition(window(1), account_id_B);

  // Change window 3 between two users and see that it changes
  // accordingly (or not).
  ShowWindowForUserNoUserTransition(window(2), account_id_A);
  EXPECT_EQ("S[A], H[A,B], S[B,A], H[B], H[C]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager()->AreWindowsSharedAmongUsers());
  EXPECT_EQ("A B", GetOwnersOfVisibleWindowsAsString());
  ShowWindowForUserNoUserTransition(window(2), account_id_C);
  EXPECT_EQ("S[A], H[A,B], H[B,C], H[B], H[C]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager()->AreWindowsSharedAmongUsers());
  EXPECT_EQ("A", GetOwnersOfVisibleWindowsAsString());

  // Switch the users and see that the results are correct.
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], S[A,B], H[B,C], S[B], H[C]", GetStatus());
  EXPECT_EQ("A B", GetOwnersOfVisibleWindowsAsString());
  StartUserTransitionAnimation(account_id_C);
  EXPECT_EQ("H[A], H[A,B], S[B,C], H[B], S[C]", GetStatus());
  EXPECT_EQ("B C", GetOwnersOfVisibleWindowsAsString());

  // Showing on the desktop of the already owning user should have no impact.
  ShowWindowForUserNoUserTransition(window(4), account_id_C);
  EXPECT_EQ("H[A], H[A,B], S[B,C], H[B], S[C]", GetStatus());
  EXPECT_EQ("B C", GetOwnersOfVisibleWindowsAsString());

  // Changing however a shown window back to the original owner should hide it.
  ShowWindowForUserNoUserTransition(window(2), account_id_B);
  EXPECT_EQ("H[A], H[A,B], H[B], H[B], S[C]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager()->AreWindowsSharedAmongUsers());
  EXPECT_EQ("C", GetOwnersOfVisibleWindowsAsString());

  // And the change should be "permanent" - switching somewhere else and coming
  // back.
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], S[A,B], S[B], S[B], H[C]", GetStatus());
  EXPECT_EQ("A B", GetOwnersOfVisibleWindowsAsString());
  StartUserTransitionAnimation(account_id_C);
  EXPECT_EQ("H[A], H[A,B], H[B], H[B], S[C]", GetStatus());
  EXPECT_EQ("C", GetOwnersOfVisibleWindowsAsString());

  // After switching window 2 back to its original desktop, all desktops should
  // be "clean" again.
  ShowWindowForUserNoUserTransition(window(1), account_id_A);
  EXPECT_FALSE(multi_user_window_manager()->AreWindowsSharedAmongUsers());
}

// Make sure that adding a window to another desktop does not cause harm.
TEST_F(MultiUserWindowManagerChromeOSTest, DoubleSharedWindowTests) {
  SetUpForThisManyWindows(1);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));

  multi_user_window_manager()->SetWindowOwner(window(0), account_id_B);

  // Add two references to the same window.
  ShowWindowForUserNoUserTransition(window(0), account_id_A);
  ShowWindowForUserNoUserTransition(window(0), account_id_A);
  EXPECT_TRUE(multi_user_window_manager()->AreWindowsSharedAmongUsers());

  // Close the window.
  delete_window_at(0);

  EXPECT_EQ("D", GetStatus());
  // There should be no shares anymore open.
  EXPECT_FALSE(multi_user_window_manager()->AreWindowsSharedAmongUsers());
}

// Tests that the user's desktop visibility changes get respected. These tests
// are required to make sure that our usage of the same feature for showing and
// hiding does not interfere with the "normal operation".
TEST_F(MultiUserWindowManagerChromeOSTest, PreserveWindowVisibilityTests) {
  SetUpForThisManyWindows(5);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));
  const AccountId account_id_C(AccountId::FromUserEmail("C"));

  // Set some owners and make sure we got what we asked for.
  // Note that we try to cover all combinations in one go.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_B);
  multi_user_window_manager()->SetWindowOwner(window(3), account_id_B);
  ShowWindowForUserNoUserTransition(window(2), account_id_A);
  ShowWindowForUserNoUserTransition(window(3), account_id_A);
  EXPECT_EQ("S[A], S[A], S[B,A], S[B,A], S[]", GetStatus());

  // Hiding a window should be respected - no matter if it is owned by that user
  // owned by someone else but shown on that desktop - or not owned.
  window(0)->Hide();
  window(2)->Hide();
  window(4)->Hide();
  EXPECT_EQ("H[A], S[A], H[B,A], S[B,A], H[]", GetStatus());

  // Flipping to another user and back should preserve all show / hide states.
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], H[A], H[B,A], H[B,A], H[]", GetStatus());

  StartUserTransitionAnimation(account_id_A);
  EXPECT_EQ("H[A], S[A], H[B,A], S[B,A], H[]", GetStatus());

  // After making them visible and switching fore and back everything should be
  // visible.
  window(0)->Show();
  window(2)->Show();
  window(4)->Show();
  EXPECT_EQ("S[A], S[A], S[B,A], S[B,A], S[]", GetStatus());

  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], H[A], H[B,A], H[B,A], S[]", GetStatus());

  StartUserTransitionAnimation(account_id_A);
  EXPECT_EQ("S[A], S[A], S[B,A], S[B,A], S[]", GetStatus());

  // Now test that making windows visible through "normal operation" while the
  // user's desktop is hidden leads to the correct result.
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], H[A], H[B,A], H[B,A], S[]", GetStatus());
  window(0)->Show();
  window(2)->Show();
  window(4)->Show();
  EXPECT_EQ("H[A], H[A], H[B,A], H[B,A], S[]", GetStatus());
  StartUserTransitionAnimation(account_id_A);
  EXPECT_EQ("S[A], S[A], S[B,A], S[B,A], S[]", GetStatus());
}

// Check that minimizing a window which is owned by another user will move it
// back and gets restored upon switching back to the original user.
TEST_F(MultiUserWindowManagerChromeOSTest, MinimizeChangesOwnershipBack) {
  SetUpForThisManyWindows(4);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));

  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_B);
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_B);
  ShowWindowForUserNoUserTransition(window(1), account_id_A);
  EXPECT_EQ("S[A], S[B,A], H[B], S[]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager()->IsWindowOnDesktopOfUser(
      window(1), account_id_A));
  wm::GetWindowState(window(1))->Minimize();
  // At this time the window is still on the desktop of that user, but the user
  // does not have a way to get to it.
  EXPECT_EQ("S[A], H[B,A], H[B], S[]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager()->IsWindowOnDesktopOfUser(
      window(1), account_id_A));
  EXPECT_TRUE(wm::GetWindowState(window(1))->IsMinimized());
  // Change to user B and make sure that minimizing does not change anything.
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], S[B], S[B], S[]", GetStatus());
  EXPECT_FALSE(wm::GetWindowState(window(1))->IsMinimized());
}

// Check that we cannot transfer the ownership of a minimized window.
TEST_F(MultiUserWindowManagerChromeOSTest, MinimizeSuppressesViewTransfer) {
  SetUpForThisManyWindows(1);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));

  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  wm::GetWindowState(window(0))->Minimize();
  EXPECT_EQ("H[A]", GetStatus());

  // Try to transfer the window to user B - which should get ignored.
  ShowWindowForUserNoUserTransition(window(0), account_id_B);
  EXPECT_EQ("H[A]", GetStatus());
}

// Testing that the activation state changes to the active window.
TEST_F(MultiUserWindowManagerChromeOSTest, ActiveWindowTests) {
  SetUpForThisManyWindows(4);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));
  const AccountId account_id_C(AccountId::FromUserEmail("C"));

  aura::client::ActivationClient* activation_client =
      aura::client::GetActivationClient(window(0)->GetRootWindow());

  // Set some windows to the active owner.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_B);
  multi_user_window_manager()->SetWindowOwner(window(3), account_id_B);
  EXPECT_EQ("S[A], S[A], H[B], H[B]", GetStatus());

  // Set the active window for user A to be #1
  activation_client->ActivateWindow(window(1));

  // Change to user B and make sure that one of its windows is active.
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], H[A], S[B], S[B]", GetStatus());
  EXPECT_TRUE(window(3) == activation_client->GetActiveWindow() ||
              window(2) == activation_client->GetActiveWindow());
  // Set the active window for user B now to be #2
  activation_client->ActivateWindow(window(2));

  StartUserTransitionAnimation(account_id_A);
  EXPECT_EQ(window(1), activation_client->GetActiveWindow());

  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ(window(2), activation_client->GetActiveWindow());

  StartUserTransitionAnimation(account_id_C);
  EXPECT_EQ(NULL, activation_client->GetActiveWindow());

  // Now test that a minimized window stays minimized upon switch and back.
  StartUserTransitionAnimation(account_id_A);
  wm::GetWindowState(window(0))->Minimize();

  StartUserTransitionAnimation(account_id_B);
  StartUserTransitionAnimation(account_id_A);
  EXPECT_TRUE(wm::GetWindowState(window(0))->IsMinimized());
  EXPECT_EQ(window(1), activation_client->GetActiveWindow());
}

// Test that Transient windows are handled properly.
TEST_F(MultiUserWindowManagerChromeOSTest, TransientWindows) {
  SetUpForThisManyWindows(10);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));

  // We create a hierarchy like this:
  //    0 (A)  4 (B)   7 (-)   - The top level owned/not owned windows
  //    |      |       |
  //    1      5 - 6   8       - Transient child of the owned windows.
  //    |              |
  //    2              9       - A transtient child of a transient child.
  //    |
  //    3                      - ..
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(4), account_id_B);
  ::wm::AddTransientChild(window(0), window(1));
  // We first attach 2->3 and then 1->2 to see that the ownership gets
  // properly propagated through the sub tree upon assigning.
  ::wm::AddTransientChild(window(2), window(3));
  ::wm::AddTransientChild(window(1), window(2));
  ::wm::AddTransientChild(window(4), window(5));
  ::wm::AddTransientChild(window(4), window(6));
  ::wm::AddTransientChild(window(7), window(8));
  ::wm::AddTransientChild(window(7), window(9));

  // By now the hierarchy should have updated itself to show all windows of A
  // and hide all windows of B. Unowned windows should remain in what ever state
  // they are in.
  EXPECT_EQ("S[A], S[], S[], S[], H[B], H[], H[], S[], S[], S[]", GetStatus());

  // Trying to show a hidden transient window shouldn't change anything for now.
  window(5)->Show();
  window(6)->Show();
  EXPECT_EQ("S[A], S[], S[], S[], H[B], H[], H[], S[], S[], S[]", GetStatus());

  // Hiding on the other hand a shown window should work and hide also its
  // children. Note that hide will have an immediate impact on itself and all
  // transient children. It furthermore should remember this state when the
  // transient children are removed from its owner later on.
  window(2)->Hide();
  window(9)->Hide();
  EXPECT_EQ("S[A], S[], H[], H[], H[B], H[], H[], S[], S[], H[]", GetStatus());

  // Switching users and switch back should return to the previous state.
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], H[], H[], H[], S[B], S[], S[], S[], S[], H[]", GetStatus());
  StartUserTransitionAnimation(account_id_A);
  EXPECT_EQ("S[A], S[], H[], H[], H[B], H[], H[], S[], S[], H[]", GetStatus());

  // Removing a window from its transient parent should return to the previously
  // set visibility state.
  // Note: Window2 was explicitly hidden above and that state should remain.
  // Note furthermore that Window3 should also be hidden since it was hidden
  // implicitly by hiding Window2.
  // set hidden above).
  //    0 (A)  4 (B)   7 (-)   2(-)   3 (-)    6(-)
  //    |      |       |
  //    1      5       8
  //                   |
  //                   9
  ::wm::RemoveTransientChild(window(2), window(3));
  ::wm::RemoveTransientChild(window(4), window(6));
  EXPECT_EQ("S[A], S[], H[], H[], H[B], H[], S[], S[], S[], H[]", GetStatus());
  // Before we leave we need to reverse all transient window ownerships.
  ::wm::RemoveTransientChild(window(0), window(1));
  ::wm::RemoveTransientChild(window(1), window(2));
  ::wm::RemoveTransientChild(window(4), window(5));
  ::wm::RemoveTransientChild(window(7), window(8));
  ::wm::RemoveTransientChild(window(7), window(9));
}

// Test that the initial visibility state gets remembered.
TEST_F(MultiUserWindowManagerChromeOSTest, PreserveInitialVisibility) {
  SetUpForThisManyWindows(4);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));

  // Set our initial show state before we assign an owner.
  window(0)->Show();
  window(1)->Hide();
  window(2)->Show();
  window(3)->Hide();
  EXPECT_EQ("S[], H[], S[], H[]", GetStatus());

  // First test: The show state gets preserved upon user switch.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_B);
  multi_user_window_manager()->SetWindowOwner(window(3), account_id_B);
  EXPECT_EQ("S[A], H[A], H[B], H[B]", GetStatus());
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], H[A], S[B], H[B]", GetStatus());
  StartUserTransitionAnimation(account_id_A);
  EXPECT_EQ("S[A], H[A], H[B], H[B]", GetStatus());

  // Second test: Transferring the window to another desktop preserves the
  // show state.
  ShowWindowForUserNoUserTransition(window(0), account_id_B);
  ShowWindowForUserNoUserTransition(window(1), account_id_B);
  ShowWindowForUserNoUserTransition(window(2), account_id_A);
  ShowWindowForUserNoUserTransition(window(3), account_id_A);
  EXPECT_EQ("H[A,B], H[A,B], S[B,A], H[B,A]", GetStatus());
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("S[A,B], H[A,B], H[B,A], H[B,A]", GetStatus());
  StartUserTransitionAnimation(account_id_A);
  EXPECT_EQ("H[A,B], H[A,B], S[B,A], H[B,A]", GetStatus());
}

// Test that in case of an activated maximize mode, windows from other users get
// maximized after a user switch.
TEST_F(MultiUserWindowManagerChromeOSTest, MaximizeModeInteraction) {
  SetUpForThisManyWindows(2);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));

  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_B);

  EXPECT_FALSE(wm::GetWindowState(window(0))->IsMaximized());
  EXPECT_FALSE(wm::GetWindowState(window(1))->IsMaximized());

  MaximizeModeWindowManager* manager = CreateMaximizeModeWindowManager();
  ASSERT_TRUE(manager);

  EXPECT_TRUE(wm::GetWindowState(window(0))->IsMaximized());
  EXPECT_FALSE(wm::GetWindowState(window(1))->IsMaximized());

  // After we start switching to B, the windows of user B should maximize.
  StartUserTransitionAnimation(account_id_B);

  EXPECT_TRUE(wm::GetWindowState(window(0))->IsMaximized());
  EXPECT_TRUE(wm::GetWindowState(window(1))->IsMaximized());
}

// Test that a system modal dialog will switch to the desktop of the owning
// user.
TEST_F(MultiUserWindowManagerChromeOSTest, SwitchUsersUponModalityChange) {
  SetUpForThisManyWindows(1);

  const AccountId account_id_a(AccountId::FromUserEmail("a"));
  const AccountId account_id_b(AccountId::FromUserEmail("b"));

  SessionControllerClient::DoSwitchActiveUser(account_id_a);

  // Making the window system modal should not change anything.
  MakeWindowSystemModal(window(0));
  EXPECT_EQ(account_id_a, user_manager()->GetActiveUser()->GetAccountId());

  // Making the window owned by user B should switch users.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_b);
  EXPECT_EQ(account_id_b, user_manager()->GetActiveUser()->GetAccountId());
}

// Test that a system modal dialog will not switch desktop if active user has
// shows window.
TEST_F(MultiUserWindowManagerChromeOSTest, DontSwitchUsersUponModalityChange) {
  SetUpForThisManyWindows(1);

  const AccountId account_id_a(AccountId::FromUserEmail("a"));
  const AccountId account_id_b(AccountId::FromUserEmail("b"));

  SessionControllerClient::DoSwitchActiveUser(account_id_a);

  // Making the window system modal should not change anything.
  MakeWindowSystemModal(window(0));
  EXPECT_EQ(account_id_a, user_manager()->GetActiveUser()->GetAccountId());

  // Making the window owned by user a should not switch users.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_a);
  EXPECT_EQ(account_id_a, user_manager()->GetActiveUser()->GetAccountId());
}

// Test that a system modal dialog will not switch if shown on correct desktop
// but owned by another user.
TEST_F(MultiUserWindowManagerChromeOSTest,
       DontSwitchUsersUponModalityChangeWhenShownButNotOwned) {
  SetUpForThisManyWindows(1);

  const AccountId account_id_a(AccountId::FromUserEmail("a"));
  const AccountId account_id_b(AccountId::FromUserEmail("b"));

  SessionControllerClient::DoSwitchActiveUser(account_id_a);

  window(0)->Hide();
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_b);
  ShowWindowForUserNoUserTransition(window(0), account_id_a);
  MakeWindowSystemModal(window(0));
  // Showing the window should trigger no user switch.
  window(0)->Show();
  EXPECT_EQ(account_id_a, user_manager()->GetActiveUser()->GetAccountId());
}

// Test that a system modal dialog will switch if shown on incorrect desktop but
// even if owned by current user.
TEST_F(MultiUserWindowManagerChromeOSTest,
       SwitchUsersUponModalityChangeWhenShownButNotOwned) {
  SetUpForThisManyWindows(1);

  const AccountId account_id_a(AccountId::FromUserEmail("a"));
  const AccountId account_id_b(AccountId::FromUserEmail("b"));

  SessionControllerClient::DoSwitchActiveUser(account_id_a);

  window(0)->Hide();
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_a);
  ShowWindowForUserNoUserTransition(window(0), account_id_b);
  MakeWindowSystemModal(window(0));
  // Showing the window should trigger a user switch.
  window(0)->Show();
  EXPECT_EQ(account_id_b, user_manager()->GetActiveUser()->GetAccountId());
}

// Test that using the full user switch animations are working as expected.
TEST_F(MultiUserWindowManagerChromeOSTest, FullUserSwitchAnimationTests) {
  SetUpForThisManyWindows(3);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));
  const AccountId account_id_C(AccountId::FromUserEmail("C"));

  // Turn the use of delays and animation on.
  multi_user_window_manager()->SetAnimationSpeedForTest(
      chrome::MultiUserWindowManagerChromeOS::ANIMATION_SPEED_FAST);
  // Set some owners and make sure we got what we asked for.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_B);
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_C);
  EXPECT_EQ("S[A], H[B], H[C]", GetStatus());
  EXPECT_EQ("A", GetOwnersOfVisibleWindowsAsString());

  // Switch the user fore and back and see that the results are correct.
  SwitchUserAndWaitForAnimation(account_id_B);

  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  EXPECT_EQ("B", GetOwnersOfVisibleWindowsAsString());

  SwitchUserAndWaitForAnimation(account_id_A);

  EXPECT_EQ("S[A], H[B], H[C]", GetStatus());

  // Switch the user quickly to another user and before the animation is done
  // switch back and see that this works.
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  // Check that after switching to C, C is fully visible.
  SwitchUserAndWaitForAnimation(account_id_C);
  EXPECT_EQ("H[A], H[B], S[C]", GetStatus());
  EXPECT_EQ("C", GetOwnersOfVisibleWindowsAsString());
}

// Make sure that we do not crash upon shutdown when an animation is pending and
// a shutdown happens.
TEST_F(MultiUserWindowManagerChromeOSTest, SystemShutdownWithActiveAnimation) {
  SetUpForThisManyWindows(2);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));

  // Turn the use of delays and animation on.
  multi_user_window_manager()->SetAnimationSpeedForTest(
      chrome::MultiUserWindowManagerChromeOS::ANIMATION_SPEED_FAST);
  // Set some owners and make sure we got what we asked for.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_B);
  StartUserTransitionAnimation(account_id_B);
  // We don't do anything more here - the animations are pending and with the
  // shutdown of the framework the animations should get cancelled. If not a
  // crash would happen.
}

// Test that using the full user switch, the animations are transitioning as
// we expect them to in all animation steps.
TEST_F(MultiUserWindowManagerChromeOSTest, AnimationSteps) {
  SetUpForThisManyWindows(3);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));
  const AccountId account_id_C(AccountId::FromUserEmail("C"));

  // Turn the use of delays and animation on.
  multi_user_window_manager()->SetAnimationSpeedForTest(
      chrome::MultiUserWindowManagerChromeOS::ANIMATION_SPEED_FAST);
  // Set some owners and make sure we got what we asked for.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_B);
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_C);
  EXPECT_FALSE(CoversScreen(window(0)));
  EXPECT_FALSE(CoversScreen(window(1)));
  EXPECT_EQ("S[A], H[B], H[C]", GetStatus());
  EXPECT_EQ("A", GetOwnersOfVisibleWindowsAsString());
  WmShelf* shelf = GetPrimaryShelf();
  EXPECT_NE(SHELF_AUTO_HIDE_ALWAYS_HIDDEN, shelf->auto_hide_behavior());
  EXPECT_EQ(1.0f, window(0)->layer()->GetTargetOpacity());
  ShelfWidget* shelf_widget = shelf->shelf_widget();
  EXPECT_FALSE(shelf_widget->IsShelfHiddenBehindBlackBar());

  // Start the animation and see that the old window is becoming invisible, the
  // new one is becoming visible, the background starts transitionining and the
  // shelf hides.
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ(kArrowBAccountIdString,
            GetWallpaperUserIdForTest());
  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  EXPECT_EQ(0.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window(1)->layer()->GetTargetOpacity());
  EXPECT_EQ(SHELF_AUTO_HIDE_ALWAYS_HIDDEN, shelf->auto_hide_behavior());
  EXPECT_FALSE(shelf_widget->IsShelfHiddenBehindBlackBar());

  // Staring the next step should show the shelf again, but there are many
  // subsystems missing (preferences system, ChromeLauncherController, ...)
  // which should set the shelf to its users state. Since that isn't there we
  // can only make sure that it stays where it is.
  AdvanceUserTransitionAnimation();
  EXPECT_EQ(kArrowBAccountIdString,
            GetWallpaperUserIdForTest());
  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  EXPECT_EQ(0.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window(1)->layer()->GetTargetOpacity());
  EXPECT_EQ(SHELF_AUTO_HIDE_ALWAYS_HIDDEN, shelf->auto_hide_behavior());
  EXPECT_FALSE(shelf_widget->IsShelfHiddenBehindBlackBar());

  // After the finalize the animation of the wallpaper should be finished.
  AdvanceUserTransitionAnimation();
  EXPECT_FALSE(shelf_widget->IsShelfHiddenBehindBlackBar());
  EXPECT_EQ(kBAccountIdString, GetWallpaperUserIdForTest());
}

// Test that the screen coverage is properly determined.
TEST_F(MultiUserWindowManagerChromeOSTest, AnimationStepsScreenCoverage) {
  SetUpForThisManyWindows(3);
  // Maximizing, fully covering the screen by bounds or fullscreen mode should
  // make CoversScreen return true.
  wm::GetWindowState(window(0))->Maximize();
  window(1)->SetBounds(gfx::Rect(0, 0, 3000, 3000));

  EXPECT_TRUE(CoversScreen(window(0)));
  EXPECT_TRUE(CoversScreen(window(1)));
  EXPECT_FALSE(CoversScreen(window(2)));

  wm::WMEvent event(wm::WM_EVENT_FULLSCREEN);
  wm::GetWindowState(window(2))->OnWMEvent(&event);
  EXPECT_TRUE(CoversScreen(window(2)));
}

// Test that switching from a desktop which has a maximized window to a desktop
// which has no maximized window will produce the proper animation.
TEST_F(MultiUserWindowManagerChromeOSTest, AnimationStepsMaximizeToNormal) {
  SetUpForThisManyWindows(3);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));
  const AccountId account_id_C(AccountId::FromUserEmail("C"));

  // Turn the use of delays and animation on.
  multi_user_window_manager()->SetAnimationSpeedForTest(
      chrome::MultiUserWindowManagerChromeOS::ANIMATION_SPEED_FAST);
  // Set some owners and make sure we got what we asked for.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  wm::GetWindowState(window(0))->Maximize();
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_B);
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_C);
  EXPECT_TRUE(CoversScreen(window(0)));
  EXPECT_FALSE(CoversScreen(window(1)));
  EXPECT_EQ("S[A], H[B], H[C]", GetStatus());
  EXPECT_EQ("A", GetOwnersOfVisibleWindowsAsString());
  EXPECT_EQ(1.0f, window(0)->layer()->GetTargetOpacity());

  // Start the animation and see that the new background is immediately set.
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  EXPECT_EQ(kBAccountIdString, GetWallpaperUserIdForTest());
  EXPECT_EQ(0.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window(1)->layer()->GetTargetOpacity());

  // The next step will not change anything.
  AdvanceUserTransitionAnimation();
  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  EXPECT_EQ(kBAccountIdString, GetWallpaperUserIdForTest());
  EXPECT_EQ(0.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window(1)->layer()->GetTargetOpacity());

  // The final step will also not have any visible impact.
  AdvanceUserTransitionAnimation();
  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  EXPECT_EQ(kBAccountIdString, GetWallpaperUserIdForTest());
  EXPECT_EQ(0.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window(1)->layer()->GetTargetOpacity());
}

// Test that switching from a desktop which has a normal window to a desktop
// which has a maximized window will produce the proper animation.
TEST_F(MultiUserWindowManagerChromeOSTest, AnimationStepsNormalToMaximized) {
  SetUpForThisManyWindows(3);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));
  const AccountId account_id_C(AccountId::FromUserEmail("C"));

  // Turn the use of delays and animation on.
  multi_user_window_manager()->SetAnimationSpeedForTest(
      chrome::MultiUserWindowManagerChromeOS::ANIMATION_SPEED_FAST);
  // Set some owners and make sure we got what we asked for.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_B);
  wm::GetWindowState(window(1))->Maximize();
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_C);
  EXPECT_FALSE(CoversScreen(window(0)));
  EXPECT_TRUE(CoversScreen(window(1)));
  EXPECT_EQ("S[A], H[B], H[C]", GetStatus());
  EXPECT_EQ("A", GetOwnersOfVisibleWindowsAsString());
  EXPECT_EQ(1.0f, window(0)->layer()->GetTargetOpacity());

  // Start the animation and see that the old window is becoming invisible, the
  // new one visible and the background remains as is.
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  EXPECT_EQ("", GetWallpaperUserIdForTest());
  EXPECT_EQ(0.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window(1)->layer()->GetTargetOpacity());

  // The next step will not change anything.
  AdvanceUserTransitionAnimation();
  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  EXPECT_EQ("", GetWallpaperUserIdForTest());
  EXPECT_EQ(0.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window(1)->layer()->GetTargetOpacity());

  // The final step however will switch the background.
  AdvanceUserTransitionAnimation();
  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  EXPECT_EQ(kBAccountIdString, GetWallpaperUserIdForTest());
  EXPECT_EQ(0.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window(1)->layer()->GetTargetOpacity());
}

// Test that switching from a desktop which has a maximized window to a desktop
// which has a maximized window will produce the proper animation.
TEST_F(MultiUserWindowManagerChromeOSTest, AnimationStepsMaximizedToMaximized) {
  SetUpForThisManyWindows(3);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));
  const AccountId account_id_C(AccountId::FromUserEmail("C"));

  // Turn the use of delays and animation on.
  multi_user_window_manager()->SetAnimationSpeedForTest(
      chrome::MultiUserWindowManagerChromeOS::ANIMATION_SPEED_FAST);
  // Set some owners and make sure we got what we asked for.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  wm::GetWindowState(window(0))->Maximize();
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_B);
  wm::GetWindowState(window(1))->Maximize();
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_C);
  EXPECT_TRUE(CoversScreen(window(0)));
  EXPECT_TRUE(CoversScreen(window(1)));
  EXPECT_EQ("S[A], H[B], H[C]", GetStatus());
  EXPECT_EQ("A", GetOwnersOfVisibleWindowsAsString());
  EXPECT_EQ(1.0f, window(0)->layer()->GetTargetOpacity());

  // Start the animation and see that the all windows are hidden (except that of
  // the new user).
  StartUserTransitionAnimation(account_id_B);
  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  EXPECT_EQ(kBAccountIdString, GetWallpaperUserIdForTest());
  EXPECT_EQ(0.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window(1)->layer()->GetTargetOpacity());

  // The next step will not change anything.
  AdvanceUserTransitionAnimation();
  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  EXPECT_EQ(kBAccountIdString, GetWallpaperUserIdForTest());
  EXPECT_EQ(0.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window(1)->layer()->GetTargetOpacity());

  // The final step however will hide the old window.
  AdvanceUserTransitionAnimation();
  EXPECT_EQ("H[A], S[B], H[C]", GetStatus());
  EXPECT_EQ(kBAccountIdString, GetWallpaperUserIdForTest());
  EXPECT_EQ(0.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window(1)->layer()->GetTargetOpacity());

  // Switching back will do the exact same thing.
  StartUserTransitionAnimation(account_id_A);
  EXPECT_EQ("S[A], H[B], H[C]", GetStatus());
  EXPECT_EQ(kAAccountIdString, GetWallpaperUserIdForTest());
  EXPECT_EQ(1.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(0.0f, window(1)->layer()->GetTargetOpacity());

  // The next step will not change anything.
  AdvanceUserTransitionAnimation();
  EXPECT_EQ("S[A], H[B], H[C]", GetStatus());
  EXPECT_EQ(kAAccountIdString, GetWallpaperUserIdForTest());
  EXPECT_EQ(1.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(0.0f, window(1)->layer()->GetTargetOpacity());

  // The final step is also not changing anything to the status.
  AdvanceUserTransitionAnimation();
  EXPECT_EQ("S[A], H[B], H[C]", GetStatus());
  EXPECT_EQ(kAAccountIdString, GetWallpaperUserIdForTest());
  EXPECT_EQ(1.0f, window(0)->layer()->GetTargetOpacity());
  EXPECT_EQ(0.0f, window(1)->layer()->GetTargetOpacity());
}

// Test that showing a window for another user also switches the desktop.
TEST_F(MultiUserWindowManagerChromeOSTest, ShowForUserSwitchesDesktop) {
  SetUpForThisManyWindows(3);

  const AccountId account_id_a(AccountId::FromUserEmail("a"));
  const AccountId account_id_b(AccountId::FromUserEmail("b"));
  const AccountId account_id_c(AccountId::FromUserEmail("c"));

  StartUserTransitionAnimation(account_id_a);
  SessionControllerClient::DoSwitchActiveUser(account_id_a);

  // Set some owners and make sure we got what we asked for.
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_a);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_b);
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_c);
  EXPECT_EQ("S[a], H[b], H[c]", GetStatus());

  // SetWindowOwner should not have changed the active user.
  EXPECT_EQ(account_id_a, GetAndValidateCurrentUserFromSessionStateObserver());

  // Check that teleporting the window of the currently active user will
  // teleport to the new desktop.
  multi_user_window_manager()->ShowWindowForUser(window(0), account_id_b);
  EXPECT_EQ(account_id_b, GetAndValidateCurrentUserFromSessionStateObserver());
  EXPECT_EQ("S[a,b], S[b], H[c]", GetStatus());

  // Check that teleporting a window from a currently inactive user will not
  // trigger a switch.
  multi_user_window_manager()->ShowWindowForUser(window(2), account_id_a);
  EXPECT_EQ(account_id_b, GetAndValidateCurrentUserFromSessionStateObserver());
  EXPECT_EQ("S[a,b], S[b], H[c,a]", GetStatus());
  multi_user_window_manager()->ShowWindowForUser(window(2), account_id_b);
  EXPECT_EQ(account_id_b, GetAndValidateCurrentUserFromSessionStateObserver());
  EXPECT_EQ("S[a,b], S[b], S[c,b]", GetStatus());

  // Check that teleporting back will also change the desktop.
  multi_user_window_manager()->ShowWindowForUser(window(2), account_id_c);
  EXPECT_EQ(account_id_c, GetAndValidateCurrentUserFromSessionStateObserver());
  EXPECT_EQ("H[a,b], H[b], S[c]", GetStatus());
}

class TestWindowObserver : public aura::WindowObserver {
 public:
  TestWindowObserver(): resize_calls_(0) {}
  ~TestWindowObserver() override{};

  void OnWindowBoundsChanged(aura::Window* window,
                             const gfx::Rect& old_bounds,
                             const gfx::Rect& new_bounds) override {
    resize_calls_++;
  }

  int resize_calls() { return resize_calls_; }

 private:
  int resize_calls_;

  DISALLOW_COPY_AND_ASSIGN(TestWindowObserver);
};

// Test that switching between users with the shelf in the same place, the shelf
// will get covered with a black bar instead being hidden and re-shown.
TEST_F(MultiUserWindowManagerChromeOSTest, TestBlackBarCover) {
  SetUpForThisManyWindows(2);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));

  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_B);
  WmShelf* shelf = GetPrimaryShelf();

  // Turn the use of delays and animation on.
  multi_user_window_manager()->SetAnimationSpeedForTest(
      chrome::MultiUserWindowManagerChromeOS::ANIMATION_SPEED_FAST);
  EXPECT_NE(SHELF_AUTO_HIDE_ALWAYS_HIDDEN, shelf->auto_hide_behavior());
  ShelfWidget* shelf_widget = shelf->shelf_widget();
  EXPECT_FALSE(shelf_widget->IsShelfHiddenBehindBlackBar());

  // First test that with no maximized window we show/hide the shelf.
  StartUserTransitionAnimation(account_id_B);
  EXPECT_FALSE(shelf_widget->IsShelfHiddenBehindBlackBar());
  EXPECT_EQ(SHELF_AUTO_HIDE_ALWAYS_HIDDEN, shelf->auto_hide_behavior());

  // Staring the next step should show the shelf again.
  AdvanceUserTransitionAnimation();
  EXPECT_FALSE(shelf_widget->IsShelfHiddenBehindBlackBar());

  AdvanceUserTransitionAnimation();
  EXPECT_FALSE(shelf_widget->IsShelfHiddenBehindBlackBar());
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);

  // Now we maximize the windows which will cause the black overlay to show up.
  wm::GetWindowState(window(0))->Maximize();
  wm::GetWindowState(window(1))->Maximize();
  // We set a window observer on both windows to see that no resize is performed
  // on our test windows.
  TestWindowObserver window_observer;
  window(0)->AddObserver(&window_observer);
  window(1)->AddObserver(&window_observer);

  // Start the animation and see that the shelf gets hidden by the black bar,
  // and the AutoHide behavior remains as it was.
  StartUserTransitionAnimation(account_id_A);
  EXPECT_TRUE(shelf_widget->IsShelfHiddenBehindBlackBar());
  EXPECT_NE(SHELF_AUTO_HIDE_ALWAYS_HIDDEN, shelf->auto_hide_behavior());

  // Starting the next step should show the shelf again.
  AdvanceUserTransitionAnimation();
  EXPECT_FALSE(shelf_widget->IsShelfHiddenBehindBlackBar());
  EXPECT_NE(SHELF_AUTO_HIDE_ALWAYS_HIDDEN, shelf->auto_hide_behavior());

  AdvanceUserTransitionAnimation();
  EXPECT_FALSE(shelf_widget->IsShelfHiddenBehindBlackBar());
  window(0)->RemoveObserver(&window_observer);
  window(1)->RemoveObserver(&window_observer);
  // No resize should have been done to the window.
  EXPECT_EQ(0, window_observer.resize_calls());
}

// Test that switching between different user won't change the activated windows
// and the property of transient windows.
TEST_F(MultiUserWindowManagerChromeOSTest, TransientWindowActivationTest) {
  SetUpForThisManyWindows(3);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));

  // Create a window hierarchy like this:
  // 0 (A)          - The normal windows
  // |
  // 1              - Transient child of the normal windows.
  // |
  // 2              - A transient child of a transient child.

  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);

  ::wm::AddTransientChild(window(0), window(1));
  window(1)->SetProperty(aura::client::kModalKey, ui::MODAL_TYPE_WINDOW);

  ::wm::AddTransientChild(window(1), window(2));
  window(2)->SetProperty(aura::client::kModalKey, ui::MODAL_TYPE_WINDOW);

  aura::client::ActivationClient* activation_client =
      aura::client::GetActivationClient(window(0)->GetRootWindow());

  // Activate window #0 will activate its deepest transient child window #2.
  activation_client->ActivateWindow(window(0));
  EXPECT_EQ(window(2), activation_client->GetActiveWindow());
  EXPECT_FALSE(::wm::CanActivateWindow(window(0)));
  EXPECT_FALSE(::wm::CanActivateWindow(window(1)));

  // Change active user to User B.
  StartUserTransitionAnimation(account_id_B);

  // Change active user back to User A.
  StartUserTransitionAnimation(account_id_A);
  EXPECT_EQ(window(2), activation_client->GetActiveWindow());
  EXPECT_FALSE(::wm::CanActivateWindow(window(0)));
  EXPECT_FALSE(::wm::CanActivateWindow(window(1)));

  // Test that switching user doesn't change the property of the windows.
  EXPECT_EQ(ui::MODAL_TYPE_NONE,
            window(0)->GetProperty(aura::client::kModalKey));
  EXPECT_EQ(ui::MODAL_TYPE_WINDOW,
            window(1)->GetProperty(aura::client::kModalKey));
  EXPECT_EQ(ui::MODAL_TYPE_WINDOW,
            window(2)->GetProperty(aura::client::kModalKey));

  ::wm::RemoveTransientChild(window(0), window(1));
  ::wm::RemoveTransientChild(window(1), window(2));
}

// Test that minimized window on one desktop can't be activated on another
// desktop.
TEST_F(MultiUserWindowManagerChromeOSTest, MinimizedWindowActivatableTests) {
  SetUpForThisManyWindows(4);

  const AccountId user1(AccountId::FromUserEmail("a@test.com"));
  const AccountId user2(AccountId::FromUserEmail("b@test.com"));
  AddTestUser(user1);
  AddTestUser(user2);

  multi_user_window_manager()->SetWindowOwner(window(0), user1);
  multi_user_window_manager()->SetWindowOwner(window(1), user1);
  multi_user_window_manager()->SetWindowOwner(window(2), user2);
  multi_user_window_manager()->SetWindowOwner(window(3), user2);

  // Minimizes window #0 and window #2.
  wm::GetWindowState(window(0))->Minimize();
  wm::GetWindowState(window(2))->Minimize();

  // Windows belonging to user2 (window #2 and #3) can't be activated by user1.
  user_manager()->SwitchActiveUser(user1);
  multi_user_window_manager()->ActiveUserChanged(
      user_manager()->FindUser(user1));
  EXPECT_TRUE(::wm::CanActivateWindow(window(0)));
  EXPECT_TRUE(::wm::CanActivateWindow(window(1)));
  EXPECT_FALSE(::wm::CanActivateWindow(window(2)));
  EXPECT_FALSE(::wm::CanActivateWindow(window(3)));

  // Windows belonging to user1 (window #0 and #1) can't be activated by user2.
  user_manager()->SwitchActiveUser(user2);
  multi_user_window_manager()->ActiveUserChanged(
      user_manager()->FindUser(user2));
  EXPECT_FALSE(::wm::CanActivateWindow(window(0)));
  EXPECT_FALSE(::wm::CanActivateWindow(window(1)));
  EXPECT_TRUE(::wm::CanActivateWindow(window(2)));
  EXPECT_TRUE(::wm::CanActivateWindow(window(3)));
}

// Test that teleported window can be activated by the presenting user.
TEST_F(MultiUserWindowManagerChromeOSTest, TeleportedWindowActivatableTests) {
  SetUpForThisManyWindows(2);

  const AccountId user1(AccountId::FromUserEmail("a@test.com"));
  const AccountId user2(AccountId::FromUserEmail("b@test.com"));
  AddTestUser(user1);
  AddTestUser(user2);

  multi_user_window_manager()->SetWindowOwner(window(0), user1);
  multi_user_window_manager()->SetWindowOwner(window(1), user2);

  user_manager()->SwitchActiveUser(user1);
  multi_user_window_manager()->ActiveUserChanged(
      user_manager()->FindUser(user1));
  EXPECT_TRUE(::wm::CanActivateWindow(window(0)));
  EXPECT_FALSE(::wm::CanActivateWindow(window(1)));

  // Teleports window #0 to user2 desktop. Then window #0 can't be activated by
  // user 1.
  multi_user_window_manager()->ShowWindowForUser(window(0), user2);
  EXPECT_FALSE(::wm::CanActivateWindow(window(0)));

  // Test that window #0 can be activated by user2.
  user_manager()->SwitchActiveUser(user2);
  multi_user_window_manager()->ActiveUserChanged(
      user_manager()->FindUser(user2));
  EXPECT_TRUE(::wm::CanActivateWindow(window(0)));
  EXPECT_TRUE(::wm::CanActivateWindow(window(1)));
}

// Tests that the window order is preserved when switching between users. Also
// tests that the window's activation is restored correctly if one user's MRU
// window list is empty.
TEST_F(MultiUserWindowManagerChromeOSTest, WindowsOrderPreservedTests) {
  SetUpForThisManyWindows(3);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));
  AddTestUser(account_id_A);
  AddTestUser(account_id_B);
  user_manager()->SwitchActiveUser(account_id_A);
  multi_user_window_manager()->ActiveUserChanged(
      user_manager()->FindUser(account_id_A));

  // Set the windows owner.
  aura::client::ActivationClient* activation_client =
      aura::client::GetActivationClient(window(0)->GetRootWindow());
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(1), account_id_A);
  multi_user_window_manager()->SetWindowOwner(window(2), account_id_A);
  EXPECT_EQ("S[A], S[A], S[A]", GetStatus());

  // Activate the windows one by one.
  activation_client->ActivateWindow(window(2));
  activation_client->ActivateWindow(window(1));
  activation_client->ActivateWindow(window(0));
  EXPECT_EQ(wm::GetActiveWindow(), window(0));

  aura::Window::Windows mru_list = WmWindow::ToAuraWindows(
      Shell::Get()->mru_window_tracker()->BuildMruWindowList());
  EXPECT_EQ(mru_list[0], window(0));
  EXPECT_EQ(mru_list[1], window(1));
  EXPECT_EQ(mru_list[2], window(2));

  user_manager()->SwitchActiveUser(account_id_B);
  multi_user_window_manager()->ActiveUserChanged(
      user_manager()->FindUser(account_id_B));
  EXPECT_EQ("H[A], H[A], H[A]", GetStatus());
  EXPECT_EQ(wm::GetActiveWindow(), nullptr);

  user_manager()->SwitchActiveUser(account_id_A);
  multi_user_window_manager()->ActiveUserChanged(
      user_manager()->FindUser(account_id_A));
  EXPECT_EQ("S[A], S[A], S[A]", GetStatus());
  EXPECT_EQ(wm::GetActiveWindow(), window(0));

  mru_list = WmWindow::ToAuraWindows(
      Shell::Get()->mru_window_tracker()->BuildMruWindowList());
  EXPECT_EQ(mru_list[0], window(0));
  EXPECT_EQ(mru_list[1], window(1));
  EXPECT_EQ(mru_list[2], window(2));
}

// Tests that ChromeNewWindowClient::GetActiveBrowser works properly in
// multi-user scenario, that is it should return the browser with active window
// associated with it (crbug.com/675265).
TEST_F(MultiUserWindowManagerChromeOSTest, GetActiveBrowserTest) {
  SetUpForThisManyWindows(1);

  const AccountId account_id_A(AccountId::FromUserEmail("A"));
  const AccountId account_id_B(AccountId::FromUserEmail("B"));
  AddTestUser(account_id_A);
  AddTestUser(account_id_B);
  user_manager()->SwitchActiveUser(account_id_A);
  multi_user_window_manager()->ActiveUserChanged(
      user_manager()->FindUser(account_id_A));

  aura::client::ActivationClient* activation_client =
      aura::client::GetActivationClient(window(0)->GetRootWindow());
  multi_user_window_manager()->SetWindowOwner(window(0), account_id_A);
  Profile* profile = multi_user_util::GetProfileFromAccountId(account_id_A);
  Browser::CreateParams params(profile, true);
  std::unique_ptr<Browser> browser(CreateTestBrowser(
      CreateTestWindowInShellWithId(0), gfx::Rect(16, 32, 640, 320), &params));
  aura::Window* browser_native_window = browser->window()->GetNativeWindow();
  activation_client->ActivateWindow(browser_native_window);
  // Manually set last active browser in BrowserList for testing.
  BrowserList::GetInstance()->SetLastActive(browser.get());
  EXPECT_EQ(browser.get(), BrowserList::GetInstance()->GetLastActive());
  EXPECT_EQ(browser_native_window, wm::GetActiveWindow());
  EXPECT_EQ(browser.get(), ChromeNewWindowClient::GetActiveBrowser());

  // Switch to another user's desktop with no active window.
  user_manager()->SwitchActiveUser(account_id_B);
  multi_user_window_manager()->ActiveUserChanged(
      user_manager()->FindUser(account_id_B));
  EXPECT_EQ(browser.get(), BrowserList::GetInstance()->GetLastActive());
  EXPECT_EQ(nullptr, wm::GetActiveWindow());
  EXPECT_EQ(nullptr, ChromeNewWindowClient::GetActiveBrowser());
}

}  // namespace test
}  // namespace ash
