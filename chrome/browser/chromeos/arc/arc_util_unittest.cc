// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/arc_util.h"

#include <memory>

#include "base/command_line.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/sys_info.h"
#include "base/test/scoped_command_line.h"
#include "base/values.h"
#include "chrome/browser/chromeos/arc/arc_session_manager.h"
#include "chrome/browser/chromeos/login/supervised/supervised_user_creation_flow.h"
#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/scoped_user_manager_enabler.h"
#include "chrome/browser/chromeos/login/users/wallpaper/wallpaper_manager.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/testing_pref_service.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "components/user_manager/known_user.h"
#include "components/user_manager/user_manager.h"
#include "components/user_manager/user_names.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arc {
namespace util {

namespace {

constexpr char kTestProfileName[] = "user@gmail.com";
constexpr char kTestGaiaId[] = "1234567890";

class ScopedLogIn {
 public:
  ScopedLogIn(
      chromeos::FakeChromeUserManager* fake_user_manager,
      const AccountId& account_id,
      user_manager::UserType user_type = user_manager::USER_TYPE_REGULAR)
      : fake_user_manager_(fake_user_manager), account_id_(account_id) {
    switch (user_type) {
      case user_manager::USER_TYPE_REGULAR:  // fallthrough
      case user_manager::USER_TYPE_ACTIVE_DIRECTORY:
        LogIn();
        break;
      case user_manager::USER_TYPE_PUBLIC_ACCOUNT:
        LogInAsPublicAccount();
        break;
      case user_manager::USER_TYPE_ARC_KIOSK_APP:
        LogInArcKioskApp();
        break;
      default:
        NOTREACHED();
    }
  }

  ~ScopedLogIn() { LogOut(); }

 private:
  void LogIn() {
    fake_user_manager_->AddUser(account_id_);
    fake_user_manager_->LoginUser(account_id_);
  }

  void LogInAsPublicAccount() {
    fake_user_manager_->AddPublicAccountUser(account_id_);
    fake_user_manager_->LoginUser(account_id_);
  }

  void LogInArcKioskApp() {
    fake_user_manager_->AddArcKioskAppUser(account_id_);
    fake_user_manager_->LoginUser(account_id_);
  }

  void LogOut() { fake_user_manager_->RemoveUserFromList(account_id_); }

  chromeos::FakeChromeUserManager* fake_user_manager_;
  const AccountId account_id_;

  DISALLOW_COPY_AND_ASSIGN(ScopedLogIn);
};

class FakeUserManagerWithLocalState : public chromeos::FakeChromeUserManager {
 public:
  FakeUserManagerWithLocalState()
      : test_local_state_(base::MakeUnique<TestingPrefServiceSimple>()) {
    RegisterPrefs(test_local_state_->registry());
  }

  PrefService* GetLocalState() const override {
    return test_local_state_.get();
  }

 private:
  std::unique_ptr<TestingPrefServiceSimple> test_local_state_;

  DISALLOW_COPY_AND_ASSIGN(FakeUserManagerWithLocalState);
};

}  // namespace

class ChromeArcUtilTest : public testing::Test {
 public:
  ChromeArcUtilTest() = default;
  ~ChromeArcUtilTest() override = default;

  void SetUp() override {
    command_line_ = base::MakeUnique<base::test::ScopedCommandLine>();

    user_manager_enabler_ =
        base::MakeUnique<chromeos::ScopedUserManagerEnabler>(
            new FakeUserManagerWithLocalState());
    chromeos::WallpaperManager::Initialize();
    profile_ = base::MakeUnique<TestingProfile>();
    profile_->set_profile_name(kTestProfileName);
  }

  void TearDown() override {
    profile_.reset();
    chromeos::WallpaperManager::Shutdown();
    user_manager_enabler_.reset();
    command_line_.reset();
  }

  TestingProfile* profile() { return profile_.get(); }

  chromeos::FakeChromeUserManager* GetFakeUserManager() const {
    return static_cast<chromeos::FakeChromeUserManager*>(
        user_manager::UserManager::Get());
  }

  void LogIn() {
    const auto account_id = AccountId::FromUserEmailGaiaId(
        profile()->GetProfileUserName(), kTestGaiaId);
    GetFakeUserManager()->AddUser(account_id);
    GetFakeUserManager()->LoginUser(account_id);
  }

 private:
  std::unique_ptr<base::test::ScopedCommandLine> command_line_;
  content::TestBrowserThreadBundle thread_bundle_;
  std::unique_ptr<chromeos::ScopedUserManagerEnabler> user_manager_enabler_;
  std::unique_ptr<TestingProfile> profile_;

  DISALLOW_COPY_AND_ASSIGN(ChromeArcUtilTest);
};

TEST_F(ChromeArcUtilTest, IsArcAllowedForProfile) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  ScopedLogIn login(GetFakeUserManager(),
                    AccountId::FromUserEmailGaiaId(
                        profile()->GetProfileUserName(), kTestGaiaId));
  EXPECT_TRUE(IsArcAllowedForProfile(profile()));
  EXPECT_TRUE(IsArcAllowedInAppListForProfile(profile()));

  // false for nullptr.
  EXPECT_FALSE(IsArcAllowedForProfile(nullptr));
  EXPECT_FALSE(IsArcAllowedInAppListForProfile(nullptr));

  // false for incognito mode profile.
  EXPECT_FALSE(IsArcAllowedForProfile(profile()->GetOffTheRecordProfile()));
  EXPECT_FALSE(
      IsArcAllowedInAppListForProfile(profile()->GetOffTheRecordProfile()));

  // false for Legacy supervised user.
  profile()->SetSupervisedUserId("foo");
  EXPECT_FALSE(IsArcAllowedForProfile(profile()));
  EXPECT_FALSE(IsArcAllowedInAppListForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, IsArcAllowedForProfileLegacy) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv({"", "--enable-arc"});
  ScopedLogIn login(GetFakeUserManager(),
                    AccountId::FromUserEmailGaiaId(
                        profile()->GetProfileUserName(), kTestGaiaId));
  EXPECT_TRUE(IsArcAllowedForProfile(profile()));
  EXPECT_TRUE(IsArcAllowedInAppListForProfile(profile()));

  // false for nullptr.
  EXPECT_FALSE(IsArcAllowedForProfile(nullptr));
  EXPECT_FALSE(IsArcAllowedInAppListForProfile(nullptr));

  // false for incognito mode profile.
  EXPECT_FALSE(IsArcAllowedForProfile(profile()->GetOffTheRecordProfile()));
  EXPECT_FALSE(
      IsArcAllowedInAppListForProfile(profile()->GetOffTheRecordProfile()));

  // false for Legacy supervised user.
  profile()->SetSupervisedUserId("foo");
  EXPECT_FALSE(IsArcAllowedForProfile(profile()));
  EXPECT_FALSE(IsArcAllowedInAppListForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, IsArcAllowedForProfile_DisableArc) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv({""});
  ScopedLogIn login(GetFakeUserManager(),
                    AccountId::FromUserEmailGaiaId(
                        profile()->GetProfileUserName(), kTestGaiaId));
  EXPECT_FALSE(IsArcAllowedForProfile(profile()));
  EXPECT_FALSE(IsArcAllowedInAppListForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, IsArcAllowedForProfile_NonPrimaryProfile) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  ScopedLogIn login2(
      GetFakeUserManager(),
      AccountId::FromUserEmailGaiaId("user2@gmail.com", "0123456789"));
  ScopedLogIn login(GetFakeUserManager(),
                    AccountId::FromUserEmailGaiaId(
                        profile()->GetProfileUserName(), kTestGaiaId));
  EXPECT_FALSE(IsArcAllowedForProfile(profile()));
  EXPECT_FALSE(IsArcAllowedInAppListForProfile(profile()));
}

// User without GAIA account.
TEST_F(ChromeArcUtilTest, IsArcAllowedForProfile_PublicAccount) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  ScopedLogIn login(GetFakeUserManager(),
                    AccountId::FromUserEmail("public_user@gmail.com"),
                    user_manager::USER_TYPE_PUBLIC_ACCOUNT);
  EXPECT_FALSE(IsArcAllowedForProfile(profile()));
  EXPECT_FALSE(IsArcAllowedInAppListForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, IsArcAllowedForProfile_ActiveDirectoryEnabled) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  ScopedLogIn login(
      GetFakeUserManager(),
      AccountId::AdFromObjGuid("f04557de-5da2-40ce-ae9d-b8874d8da96e"),
      user_manager::USER_TYPE_ACTIVE_DIRECTORY);
  EXPECT_FALSE(chromeos::ProfileHelper::Get()
                   ->GetUserByProfile(profile())
                   ->HasGaiaAccount());
  EXPECT_TRUE(IsArcAllowedForProfile(profile()));
  EXPECT_TRUE(IsArcAllowedInAppListForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, IsArcAllowedForProfile_ActiveDirectoryDisabled) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv({""});
  ScopedLogIn login(
      GetFakeUserManager(),
      AccountId::AdFromObjGuid("f04557de-5da2-40ce-ae9d-b8874d8da96e"),
      user_manager::USER_TYPE_ACTIVE_DIRECTORY);
  EXPECT_FALSE(chromeos::ProfileHelper::Get()
                   ->GetUserByProfile(profile())
                   ->HasGaiaAccount());
  EXPECT_FALSE(IsArcAllowedForProfile(profile()));
  EXPECT_FALSE(IsArcAllowedInAppListForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, IsArcAllowedForProfile_KioskArcNotAvailable) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv({""});
  ScopedLogIn login(GetFakeUserManager(),
                    AccountId::FromUserEmail(profile()->GetProfileUserName()),
                    user_manager::USER_TYPE_ARC_KIOSK_APP);
  EXPECT_FALSE(chromeos::ProfileHelper::Get()
                   ->GetUserByProfile(profile())
                   ->HasGaiaAccount());
  EXPECT_FALSE(IsArcAllowedForProfile(profile()));
  EXPECT_FALSE(IsArcAllowedInAppListForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, IsArcAllowedForProfile_KioskArcInstalled) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=installed"});
  ScopedLogIn login(GetFakeUserManager(),
                    AccountId::FromUserEmail(profile()->GetProfileUserName()),
                    user_manager::USER_TYPE_ARC_KIOSK_APP);
  EXPECT_FALSE(chromeos::ProfileHelper::Get()
                   ->GetUserByProfile(profile())
                   ->HasGaiaAccount());
  EXPECT_TRUE(IsArcAllowedForProfile(profile()));
  EXPECT_TRUE(IsArcAllowedInAppListForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, IsArcAllowedForProfile_KioskArcSupported) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  ScopedLogIn login(GetFakeUserManager(),
                    AccountId::FromUserEmail(profile()->GetProfileUserName()),
                    user_manager::USER_TYPE_ARC_KIOSK_APP);
  EXPECT_FALSE(chromeos::ProfileHelper::Get()
                   ->GetUserByProfile(profile())
                   ->HasGaiaAccount());
  EXPECT_TRUE(IsArcAllowedForProfile(profile()));
  EXPECT_TRUE(IsArcAllowedInAppListForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, IsArcAllowedForProfile_SupervisedUserFlow) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  auto manager_id = AccountId::FromUserEmailGaiaId(
      profile()->GetProfileUserName(), kTestGaiaId);
  ScopedLogIn login(GetFakeUserManager(), manager_id);
  GetFakeUserManager()->SetUserFlow(
      manager_id, new chromeos::SupervisedUserCreationFlow(manager_id));
  EXPECT_FALSE(IsArcAllowedForProfile(profile()));
  GetFakeUserManager()->ResetUserFlow(manager_id);
}

// Guest account is interpreted as EphemeralDataUser.
TEST_F(ChromeArcUtilTest, IsArcAllowedForProfile_GuestAccount) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  ScopedLogIn login(GetFakeUserManager(),
                    GetFakeUserManager()->GetGuestAccountId());
  EXPECT_FALSE(IsArcAllowedForProfile(profile()));
  EXPECT_FALSE(IsArcAllowedInAppListForProfile(profile()));
}

// Demo account is interpreted as EphemeralDataUser.
TEST_F(ChromeArcUtilTest, IsArcAllowedForProfile_DemoAccount) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  ScopedLogIn login(GetFakeUserManager(), user_manager::DemoAccountId());
  EXPECT_FALSE(IsArcAllowedForProfile(profile()));
  EXPECT_FALSE(IsArcAllowedInAppListForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, IsArcCompatibleFileSystemUsedForProfile) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  // TODO(kinaba): Come up with some way to test the conditions below
  // causes differences in the return values of IsArcAllowedForProfile()
  // and IsArcAllowedInAppListForProfile().
  const AccountId id(AccountId::FromUserEmailGaiaId(
      profile()->GetProfileUserName(), kTestGaiaId));
  ScopedLogIn login(GetFakeUserManager(), id);

  // Unconfirmed + Old ARC
  base::SysInfo::SetChromeOSVersionInfoForTest(
      "CHROMEOS_ARC_ANDROID_SDK_VERSION=23", base::Time::Now());
  EXPECT_TRUE(IsArcCompatibleFileSystemUsedForProfile(profile()));

  // Unconfirmed + New ARC
  base::SysInfo::SetChromeOSVersionInfoForTest(
      "CHROMEOS_ARC_ANDROID_SDK_VERSION=25", base::Time::Now());
  EXPECT_FALSE(IsArcCompatibleFileSystemUsedForProfile(profile()));

  // Old FS + Old ARC
  user_manager::known_user::SetIntegerPref(
      id, prefs::kArcCompatibleFilesystemChosen, kFileSystemIncompatible);
  base::SysInfo::SetChromeOSVersionInfoForTest(
      "CHROMEOS_ARC_ANDROID_SDK_VERSION=23", base::Time::Now());
  EXPECT_TRUE(IsArcCompatibleFileSystemUsedForProfile(profile()));

  // Old FS + New ARC
  base::SysInfo::SetChromeOSVersionInfoForTest(
      "CHROMEOS_ARC_ANDROID_SDK_VERSION=25", base::Time::Now());
  EXPECT_FALSE(IsArcCompatibleFileSystemUsedForProfile(profile()));

  // New FS + Old ARC
  user_manager::known_user::SetIntegerPref(
      id, prefs::kArcCompatibleFilesystemChosen, kFileSystemCompatible);
  base::SysInfo::SetChromeOSVersionInfoForTest(
      "CHROMEOS_ARC_ANDROID_SDK_VERSION=23", base::Time::Now());
  EXPECT_TRUE(IsArcCompatibleFileSystemUsedForProfile(profile()));

  // New FS + New ARC
  base::SysInfo::SetChromeOSVersionInfoForTest(
      "CHROMEOS_ARC_ANDROID_SDK_VERSION=25", base::Time::Now());
  EXPECT_TRUE(IsArcCompatibleFileSystemUsedForProfile(profile()));

  // New FS (User notified) + Old ARC
  user_manager::known_user::SetIntegerPref(
      id, prefs::kArcCompatibleFilesystemChosen,
      kFileSystemCompatibleAndNotified);
  base::SysInfo::SetChromeOSVersionInfoForTest(
      "CHROMEOS_ARC_ANDROID_SDK_VERSION=23", base::Time::Now());
  EXPECT_TRUE(IsArcCompatibleFileSystemUsedForProfile(profile()));

  // New FS (User notified) + New ARC
  base::SysInfo::SetChromeOSVersionInfoForTest(
      "CHROMEOS_ARC_ANDROID_SDK_VERSION=25", base::Time::Now());
  EXPECT_TRUE(IsArcCompatibleFileSystemUsedForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, ArcPlayStoreEnabledForProfile) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  // Ensure IsAllowedForProfile() true.
  ScopedLogIn login(GetFakeUserManager(),
                    AccountId::FromUserEmailGaiaId(
                        profile()->GetProfileUserName(), kTestGaiaId));
  ASSERT_TRUE(IsArcAllowedForProfile(profile()));

  // By default, Google Play Store is disabled.
  EXPECT_FALSE(IsArcPlayStoreEnabledForProfile(profile()));

  // Enable Google Play Store.
  SetArcPlayStoreEnabledForProfile(profile(), true);
  EXPECT_TRUE(IsArcPlayStoreEnabledForProfile(profile()));

  // Disable Google Play Store.
  SetArcPlayStoreEnabledForProfile(profile(), false);
  EXPECT_FALSE(IsArcPlayStoreEnabledForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, ArcPlayStoreEnabledForProfile_NotAllowed) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  ASSERT_FALSE(IsArcAllowedForProfile(profile()));

  // If ARC is not allowed for the profile, always return false.
  EXPECT_FALSE(IsArcPlayStoreEnabledForProfile(profile()));

  // Directly set the preference value, to avoid DCHECK in
  // SetArcPlayStoreEnabledForProfile().
  profile()->GetPrefs()->SetBoolean(prefs::kArcEnabled, true);
  EXPECT_FALSE(IsArcPlayStoreEnabledForProfile(profile()));
}

TEST_F(ChromeArcUtilTest, ArcPlayStoreEnabledForProfile_Managed) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  // Ensure IsAllowedForProfile() true.
  ScopedLogIn login(GetFakeUserManager(),
                    AccountId::FromUserEmailGaiaId(
                        profile()->GetProfileUserName(), kTestGaiaId));
  ASSERT_TRUE(IsArcAllowedForProfile(profile()));

  // By default it is not managed.
  EXPECT_FALSE(IsArcPlayStoreEnabledPreferenceManagedForProfile(profile()));
  EXPECT_FALSE(IsArcPlayStoreEnabledForProfile(profile()));

  // 1) Set managed preference to true, then try to set the value to false
  // via SetArcPlayStoreEnabledForProfile().
  profile()->GetTestingPrefService()->SetManagedPref(
      prefs::kArcEnabled, base::MakeUnique<base::Value>(true));
  EXPECT_TRUE(IsArcPlayStoreEnabledPreferenceManagedForProfile(profile()));
  EXPECT_TRUE(IsArcPlayStoreEnabledForProfile(profile()));
  SetArcPlayStoreEnabledForProfile(profile(), false);
  EXPECT_TRUE(IsArcPlayStoreEnabledPreferenceManagedForProfile(profile()));
  EXPECT_TRUE(IsArcPlayStoreEnabledForProfile(profile()));

  // Remove managed state.
  profile()->GetTestingPrefService()->RemoveManagedPref(prefs::kArcEnabled);
  EXPECT_FALSE(IsArcPlayStoreEnabledPreferenceManagedForProfile(profile()));

  // 2) Set managed preference to false, then try to set the value to true
  // via SetArcPlayStoreEnabledForProfile().
  profile()->GetTestingPrefService()->SetManagedPref(
      prefs::kArcEnabled, base::MakeUnique<base::Value>(false));
  EXPECT_TRUE(IsArcPlayStoreEnabledPreferenceManagedForProfile(profile()));
  EXPECT_FALSE(IsArcPlayStoreEnabledForProfile(profile()));
  SetArcPlayStoreEnabledForProfile(profile(), true);
  EXPECT_TRUE(IsArcPlayStoreEnabledPreferenceManagedForProfile(profile()));
  EXPECT_FALSE(IsArcPlayStoreEnabledForProfile(profile()));

  // Remove managed state.
  profile()->GetTestingPrefService()->RemoveManagedPref(prefs::kArcEnabled);
  EXPECT_FALSE(IsArcPlayStoreEnabledPreferenceManagedForProfile(profile()));
}

// Test the AreArcAllOptInPreferencesManagedForProfile() function.
TEST_F(ChromeArcUtilTest, AreArcAllOptInPreferencesManagedForProfile) {
  base::CommandLine::ForCurrentProcess()->InitFromArgv(
      {"", "--arc-availability=officially-supported"});
  // OptIn prefs are unset, the function returns false.
  EXPECT_FALSE(AreArcAllOptInPreferencesManagedForProfile(profile()));

  // OptIn prefs are set to unmanaged values, and the function returns false.
  profile()->GetPrefs()->SetBoolean(prefs::kArcBackupRestoreEnabled, false);
  profile()->GetPrefs()->SetBoolean(prefs::kArcLocationServiceEnabled, false);
  EXPECT_FALSE(AreArcAllOptInPreferencesManagedForProfile(profile()));

  // Backup-restore pref is managed, while location-service is not, and the
  // function returns false.
  profile()->GetTestingPrefService()->SetManagedPref(
      prefs::kArcBackupRestoreEnabled, base::MakeUnique<base::Value>(false));
  EXPECT_FALSE(AreArcAllOptInPreferencesManagedForProfile(profile()));

  // Location-service pref is managed, while backup-restore is not, and the
  // function returns false.
  profile()->GetTestingPrefService()->RemoveManagedPref(
      prefs::kArcBackupRestoreEnabled);
  profile()->GetTestingPrefService()->SetManagedPref(
      prefs::kArcLocationServiceEnabled, base::MakeUnique<base::Value>(false));
  EXPECT_FALSE(AreArcAllOptInPreferencesManagedForProfile(profile()));

  // Both OptIn prefs are set to managed values, and the function returns true.
  profile()->GetTestingPrefService()->SetManagedPref(
      prefs::kArcBackupRestoreEnabled, base::MakeUnique<base::Value>(false));
  EXPECT_TRUE(AreArcAllOptInPreferencesManagedForProfile(profile()));
}

}  // namespace util
}  // namespace arc
