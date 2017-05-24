// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"

#include <set>

#include "base/callback.h"
#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "base/sys_info.h"
#include "chrome/browser/chromeos/login/users/chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/chrome_user_manager_util.h"
#include "chrome/browser/chromeos/login/users/fake_supervised_user_manager.h"
#include "chrome/browser/chromeos/login/users/wallpaper/wallpaper_manager.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/grit/theme_resources.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/login/login_state.h"
#include "components/user_manager/known_user.h"
#include "components/user_manager/user_image/user_image.h"
#include "components/user_manager/user_names.h"
#include "components/user_manager/user_type.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image_skia.h"

namespace {

class FakeTaskRunner : public base::TaskRunner {
 public:
  FakeTaskRunner() = default;

 protected:
  ~FakeTaskRunner() override {}

 private:
  // base::TaskRunner overrides.
  bool PostDelayedTask(const tracked_objects::Location& from_here,
                       base::OnceClosure task,
                       base::TimeDelta delay) override {
    std::move(task).Run();
    return true;
  }
  bool RunsTasksInCurrentSequence() const override { return true; }

  DISALLOW_COPY_AND_ASSIGN(FakeTaskRunner);
};

}  // namespace

namespace chromeos {

class FakeSupervisedUserManager;

FakeChromeUserManager::FakeChromeUserManager()
    : ChromeUserManager(new FakeTaskRunner()),
      supervised_user_manager_(new FakeSupervisedUserManager) {
  ProfileHelper::SetProfileToUserForTestingEnabled(true);
}

FakeChromeUserManager::~FakeChromeUserManager() {
  ProfileHelper::SetProfileToUserForTestingEnabled(false);
}

const user_manager::User* FakeChromeUserManager::AddUser(
    const AccountId& account_id) {
  return AddUserWithAffiliation(account_id, false);
}

const user_manager::User* FakeChromeUserManager::AddUserWithAffiliation(
    const AccountId& account_id,
    bool is_affiliated) {
  user_manager::User* user = user_manager::User::CreateRegularUser(account_id);
  user->SetAffiliation(is_affiliated);
  user->set_username_hash(ProfileHelper::GetUserIdHashByUserIdForTesting(
      account_id.GetUserEmail()));
  user->SetStubImage(base::MakeUnique<user_manager::UserImage>(
                         *ResourceBundle::GetSharedInstance().GetImageSkiaNamed(
                             IDR_PROFILE_PICTURE_LOADING)),
                     user_manager::User::USER_IMAGE_PROFILE, false);
  users_.push_back(user);
  chromeos::ProfileHelper::Get()->SetProfileToUserMappingForTesting(user);
  return user;
}

user_manager::User* FakeChromeUserManager::AddKioskAppUser(
    const AccountId& account_id) {
  user_manager::User* user = user_manager::User::CreateKioskAppUser(account_id);
  user->set_username_hash(ProfileHelper::GetUserIdHashByUserIdForTesting(
      account_id.GetUserEmail()));
  users_.push_back(user);
  return user;
}

user_manager::User* FakeChromeUserManager::AddArcKioskAppUser(
    const AccountId& account_id) {
  user_manager::User* user =
      user_manager::User::CreateArcKioskAppUser(account_id);
  user->set_username_hash(ProfileHelper::GetUserIdHashByUserIdForTesting(
      account_id.GetUserEmail()));
  users_.push_back(user);
  return user;
}

user_manager::User* FakeChromeUserManager::AddSupervisedUser(
    const AccountId& account_id) {
  user_manager::User* user =
      user_manager::User::CreateSupervisedUser(account_id);
  user->set_username_hash(ProfileHelper::GetUserIdHashByUserIdForTesting(
      account_id.GetUserEmail()));
  users_.push_back(user);
  return user;
}

const user_manager::User* FakeChromeUserManager::AddPublicAccountUser(
    const AccountId& account_id) {
  user_manager::User* user =
      user_manager::User::CreatePublicAccountUser(account_id);
  user->set_username_hash(ProfileHelper::GetUserIdHashByUserIdForTesting(
      account_id.GetUserEmail()));
  user->SetStubImage(base::MakeUnique<user_manager::UserImage>(
                         *ResourceBundle::GetSharedInstance().GetImageSkiaNamed(
                             IDR_PROFILE_PICTURE_LOADING)),
                     user_manager::User::USER_IMAGE_PROFILE, false);
  users_.push_back(user);
  chromeos::ProfileHelper::Get()->SetProfileToUserMappingForTesting(user);
  return user;
}

bool FakeChromeUserManager::AreEphemeralUsersEnabled() const {
  return fake_ephemeral_users_enabled_;
}

void FakeChromeUserManager::LoginUser(const AccountId& account_id) {
  UserLoggedIn(account_id, ProfileHelper::GetUserIdHashByUserIdForTesting(
                               account_id.GetUserEmail()),
               false /* browser_restart */);

  // NOTE: This does not match production. See function comment.
  for (auto* user : users_) {
    if (user->GetAccountId() == account_id) {
      user->set_profile_is_created();
      break;
    }
  }
}

BootstrapManager* FakeChromeUserManager::GetBootstrapManager() {
  return bootstrap_manager_;
}

MultiProfileUserController*
FakeChromeUserManager::GetMultiProfileUserController() {
  return multi_profile_user_controller_;
}

SupervisedUserManager* FakeChromeUserManager::GetSupervisedUserManager() {
  return supervised_user_manager_.get();
}

UserImageManager* FakeChromeUserManager::GetUserImageManager(
    const AccountId& /* account_id */) {
  return nullptr;
}

void FakeChromeUserManager::SetUserFlow(const AccountId& account_id,
                                        UserFlow* flow) {
  ResetUserFlow(account_id);
  specific_flows_[account_id] = flow;
}

UserFlow* FakeChromeUserManager::GetCurrentUserFlow() const {
  if (!IsUserLoggedIn())
    return GetDefaultUserFlow();
  return GetUserFlow(GetActiveUser()->GetAccountId());
}

UserFlow* FakeChromeUserManager::GetUserFlow(
    const AccountId& account_id) const {
  FlowMap::const_iterator it = specific_flows_.find(account_id);
  if (it != specific_flows_.end())
    return it->second;
  return GetDefaultUserFlow();
}

void FakeChromeUserManager::ResetUserFlow(const AccountId& account_id) {
  FlowMap::iterator it = specific_flows_.find(account_id);
  if (it != specific_flows_.end()) {
    delete it->second;
    specific_flows_.erase(it);
  }
}

void FakeChromeUserManager::SwitchActiveUser(const AccountId& account_id) {
  active_account_id_ = account_id;
  ProfileHelper::Get()->ActiveUserHashChanged(
      ProfileHelper::GetUserIdHashByUserIdForTesting(
          account_id.GetUserEmail()));
  active_user_ = nullptr;
  if (!users_.empty() && active_account_id_.is_valid()) {
    for (user_manager::User* const user : users_) {
      if (user->GetAccountId() == active_account_id_) {
        active_user_ = user;
        user->set_is_active(true);
      } else {
        user->set_is_active(false);
      }
    }
  }
}

const AccountId& FakeChromeUserManager::GetOwnerAccountId() const {
  return owner_account_id_;
}

void FakeChromeUserManager::OnSessionStarted() {}

void FakeChromeUserManager::OnProfileInitialized(user_manager::User* user) {
  user->set_profile_ever_initialized(true);
}

void FakeChromeUserManager::RemoveUser(
    const AccountId& account_id,
    user_manager::RemoveUserDelegate* delegate) {}

void FakeChromeUserManager::RemoveUserFromList(const AccountId& account_id) {
  WallpaperManager::Get()->RemoveUserWallpaperInfo(account_id);
  chromeos::ProfileHelper::Get()->RemoveUserFromListForTesting(account_id);

  const user_manager::UserList::iterator it =
      std::find_if(users_.begin(), users_.end(),
                   [&account_id](const user_manager::User* user) {
                     return user->GetAccountId() == account_id;
                   });
  if (it != users_.end()) {
    if (primary_user_ == *it)
      primary_user_ = nullptr;
    if (active_user_ != *it)
      delete *it;
    users_.erase(it);
  }
}

user_manager::UserList
FakeChromeUserManager::GetUsersAllowedForSupervisedUsersCreation() const {
  CrosSettings* cros_settings = CrosSettings::Get();
  bool allow_new_user = true;
  cros_settings->GetBoolean(kAccountsPrefAllowNewUser, &allow_new_user);
  bool supervised_users_allowed = AreSupervisedUsersAllowed();

  // Restricted either by policy or by owner.
  if (!allow_new_user || !supervised_users_allowed)
    return user_manager::UserList();

  return GetUsersAllowedAsSupervisedUserManagers(GetUsers());
}

user_manager::UserList FakeChromeUserManager::GetUsersAllowedForMultiProfile()
    const {
  // Supervised users are not allowed to use multi-profiles.
  if (GetLoggedInUsers().size() == 1 &&
      GetPrimaryUser()->GetType() != user_manager::USER_TYPE_REGULAR) {
    return user_manager::UserList();
  }

  user_manager::UserList result;
  const user_manager::UserList& users = GetUsers();
  for (user_manager::User* user : users) {
    if (user->GetType() == user_manager::USER_TYPE_REGULAR &&
        !user->is_logged_in()) {
      result.push_back(user);
    }
  }

  return result;
}

UserFlow* FakeChromeUserManager::GetDefaultUserFlow() const {
  if (!default_flow_.get())
    default_flow_.reset(new DefaultUserFlow());
  return default_flow_.get();
}

void FakeChromeUserManager::UpdateLoginState(
    const user_manager::User* active_user,
    const user_manager::User* primary_user,
    bool is_current_user_owner) const {
  chrome_user_manager_util::UpdateLoginState(active_user, primary_user,
                                             is_current_user_owner);
}

bool FakeChromeUserManager::GetPlatformKnownUserId(
    const std::string& user_email,
    const std::string& gaia_id,
    AccountId* out_account_id) const {
  return chrome_user_manager_util::GetPlatformKnownUserId(user_email, gaia_id,
                                                          out_account_id);
}

const AccountId& FakeChromeUserManager::GetGuestAccountId() const {
  return user_manager::GuestAccountId();
}

bool FakeChromeUserManager::IsFirstExecAfterBoot() const {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      chromeos::switches::kFirstExecAfterBoot);
}

void FakeChromeUserManager::AsyncRemoveCryptohome(
    const AccountId& account_id) const {
  NOTIMPLEMENTED();
}

bool FakeChromeUserManager::IsGuestAccountId(
    const AccountId& account_id) const {
  return account_id == user_manager::GuestAccountId();
}

bool FakeChromeUserManager::IsStubAccountId(const AccountId& account_id) const {
  return account_id == user_manager::StubAccountId();
}

bool FakeChromeUserManager::IsSupervisedAccountId(
    const AccountId& account_id) const {
  return gaia::ExtractDomainName(account_id.GetUserEmail()) ==
         user_manager::kSupervisedUserDomain;
}

bool FakeChromeUserManager::HasBrowserRestarted() const {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  return base::SysInfo::IsRunningOnChromeOS() &&
         command_line->HasSwitch(chromeos::switches::kLoginUser);
}

const gfx::ImageSkia& FakeChromeUserManager::GetResourceImagekiaNamed(
    int id) const {
  return *ResourceBundle::GetSharedInstance().GetImageSkiaNamed(id);
}

base::string16 FakeChromeUserManager::GetResourceStringUTF16(
    int string_id) const {
  return base::string16();
}

void FakeChromeUserManager::ScheduleResolveLocale(
    const std::string& locale,
    const base::Closure& on_resolved_callback,
    std::string* out_resolved_locale) const {
  NOTIMPLEMENTED();
  return;
}

bool FakeChromeUserManager::IsValidDefaultUserImageId(int image_index) const {
  NOTIMPLEMENTED();
  return false;
}

// UserManager implementation:
void FakeChromeUserManager::Initialize() {
  return ChromeUserManager::Initialize();
}

void FakeChromeUserManager::Shutdown() {
  return ChromeUserManager::Shutdown();
}

const user_manager::UserList& FakeChromeUserManager::GetUsers() const {
  return users_;
}

const user_manager::UserList& FakeChromeUserManager::GetLoggedInUsers() const {
  return logged_in_users_;
}

const user_manager::UserList& FakeChromeUserManager::GetLRULoggedInUsers()
    const {
  return users_;
}

user_manager::UserList FakeChromeUserManager::GetUnlockUsers() const {
  return users_;
}

void FakeChromeUserManager::UserLoggedIn(const AccountId& account_id,
                                         const std::string& username_hash,
                                         bool browser_restart) {
  for (auto* user : users_) {
    if (user->username_hash() == username_hash) {
      user->set_is_logged_in(true);
      logged_in_users_.push_back(user);

      if (!primary_user_)
        primary_user_ = user;
      break;
    }
  }
  // TODO(jamescook): This should set active_user_ and call NotifyOnLogin().
}

void FakeChromeUserManager::SwitchToLastActiveUser() {
  NOTREACHED();
}

bool FakeChromeUserManager::IsKnownUser(const AccountId& account_id) const {
  return true;
}

const user_manager::User* FakeChromeUserManager::FindUser(
    const AccountId& account_id) const {
  if (active_user_ != nullptr && active_user_->GetAccountId() == account_id)
    return active_user_;

  const user_manager::UserList& users = GetUsers();
  for (const auto* user : users) {
    if (user->GetAccountId() == account_id)
      return user;
  }

  return nullptr;
}

user_manager::User* FakeChromeUserManager::FindUserAndModify(
    const AccountId& account_id) {
  return nullptr;
}

const user_manager::User* FakeChromeUserManager::GetActiveUser() const {
  return GetActiveUserInternal();
}

user_manager::User* FakeChromeUserManager::GetActiveUser() {
  return GetActiveUserInternal();
}

const user_manager::User* FakeChromeUserManager::GetPrimaryUser() const {
  return primary_user_;
}

void FakeChromeUserManager::SaveUserOAuthStatus(
    const AccountId& account_id,
    user_manager::User::OAuthTokenStatus oauth_token_status) {
  NOTREACHED();
}

void FakeChromeUserManager::SaveForceOnlineSignin(const AccountId& account_id,
                                                  bool force_online_signin) {
  NOTREACHED();
}

void FakeChromeUserManager::SaveUserDisplayName(
    const AccountId& account_id,
    const base::string16& display_name) {
  for (auto* user : users_) {
    if (user->GetAccountId() == account_id) {
      user->set_display_name(display_name);
      return;
    }
  }
}

base::string16 FakeChromeUserManager::GetUserDisplayName(
    const AccountId& account_id) const {
  return base::string16();
}

void FakeChromeUserManager::SaveUserDisplayEmail(
    const AccountId& account_id,
    const std::string& display_email) {
  NOTREACHED();
}

std::string FakeChromeUserManager::GetUserDisplayEmail(
    const AccountId& account_id) const {
  return account_id.GetUserEmail();
}

void FakeChromeUserManager::SaveUserType(
    const AccountId& account_id,
    const user_manager::UserType& user_type) {
  NOTREACHED();
}

void FakeChromeUserManager::UpdateUserAccountData(
    const AccountId& account_id,
    const UserAccountData& account_data) {
  NOTREACHED();
}

bool FakeChromeUserManager::IsCurrentUserOwner() const {
  return false;
}

bool FakeChromeUserManager::IsCurrentUserNew() const {
  return current_user_new_;
}

bool FakeChromeUserManager::IsCurrentUserNonCryptohomeDataEphemeral() const {
  return false;
}

bool FakeChromeUserManager::CanCurrentUserLock() const {
  return false;
}

bool FakeChromeUserManager::IsUserLoggedIn() const {
  return logged_in_users_.size() > 0;
}

bool FakeChromeUserManager::IsLoggedInAsUserWithGaiaAccount() const {
  return true;
}

bool FakeChromeUserManager::IsLoggedInAsChildUser() const {
  NOTREACHED();
  return false;
}

bool FakeChromeUserManager::IsLoggedInAsPublicAccount() const {
  return false;
}

bool FakeChromeUserManager::IsLoggedInAsGuest() const {
  return false;
}

bool FakeChromeUserManager::IsLoggedInAsSupervisedUser() const {
  return false;
}

bool FakeChromeUserManager::IsLoggedInAsKioskApp() const {
  const user_manager::User* active_user = GetActiveUser();
  return active_user
             ? active_user->GetType() == user_manager::USER_TYPE_KIOSK_APP
             : false;
}

bool FakeChromeUserManager::IsLoggedInAsArcKioskApp() const {
  const user_manager::User* active_user = GetActiveUser();
  return active_user
             ? active_user->GetType() == user_manager::USER_TYPE_ARC_KIOSK_APP
             : false;
}

bool FakeChromeUserManager::IsLoggedInAsStub() const {
  return false;
}

bool FakeChromeUserManager::IsUserNonCryptohomeDataEphemeral(
    const AccountId& account_id) const {
  return false;
}

void FakeChromeUserManager::ChangeUserChildStatus(user_manager::User* user,
                                                  bool is_child) {
  NOTREACHED();
}

bool FakeChromeUserManager::AreSupervisedUsersAllowed() const {
  return true;
}

void FakeChromeUserManager::CreateLocalState() {
  local_state_ = base::MakeUnique<TestingPrefServiceSimple>();
  user_manager::known_user::RegisterPrefs(local_state_->registry());
}

PrefService* FakeChromeUserManager::GetLocalState() const {
  return local_state_.get();
}

void FakeChromeUserManager::SetIsCurrentUserNew(bool is_new) {
  NOTREACHED();
}

const std::string& FakeChromeUserManager::GetApplicationLocale() const {
  static const std::string default_locale("en-US");
  return default_locale;
}

void FakeChromeUserManager::HandleUserOAuthTokenStatusChange(
    const AccountId& account_id,
    user_manager::User::OAuthTokenStatus status) const {
  NOTREACHED();
}

void FakeChromeUserManager::LoadDeviceLocalAccounts(
    std::set<AccountId>* users_set) {
  NOTREACHED();
}

bool FakeChromeUserManager::IsEnterpriseManaged() const {
  return false;
}

void FakeChromeUserManager::PerformPreUserListLoadingActions() {
  NOTREACHED();
}

void FakeChromeUserManager::PerformPostUserListLoadingActions() {
  NOTREACHED();
}

void FakeChromeUserManager::PerformPostUserLoggedInActions(
    bool browser_restart) {
  NOTREACHED();
}

bool FakeChromeUserManager::IsDemoApp(const AccountId& account_id) const {
  return account_id == user_manager::DemoAccountId();
}

bool FakeChromeUserManager::IsDeviceLocalAccountMarkedForRemoval(
    const AccountId& account_id) const {
  return false;
}

void FakeChromeUserManager::DemoAccountLoggedIn() {
  NOTREACHED();
}

void FakeChromeUserManager::KioskAppLoggedIn(user_manager::User* user) {}

void FakeChromeUserManager::ArcKioskAppLoggedIn(user_manager::User* user) {}

void FakeChromeUserManager::PublicAccountUserLoggedIn(
    user_manager::User* user) {
  NOTREACHED();
}

void FakeChromeUserManager::SupervisedUserLoggedIn(
    const AccountId& account_id) {
  NOTREACHED();
}

void FakeChromeUserManager::OnUserRemoved(const AccountId& account_id) {
  NOTREACHED();
}

void FakeChromeUserManager::SetUserAffiliation(
    const std::string& user_email,
    const AffiliationIDSet& user_affiliation_ids) {}

bool FakeChromeUserManager::ShouldReportUser(const std::string& user_id) const {
  return false;
}

user_manager::User* FakeChromeUserManager::GetActiveUserInternal() const {
  if (active_user_ != nullptr)
    return active_user_;

  if (users_.empty())
    return nullptr;
  if (active_account_id_.is_valid()) {
    for (auto* user : users_) {
      if (user->GetAccountId() == active_account_id_)
        return user;
    }
  }
  return users_[0];
}

}  // namespace chromeos
