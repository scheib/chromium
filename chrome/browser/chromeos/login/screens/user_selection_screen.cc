// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/screens/user_selection_screen.h"

#include <stddef.h>

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/chromeos/login/lock/screen_locker.h"
#include "chrome/browser/chromeos/login/quick_unlock/quick_unlock_factory.h"
#include "chrome/browser/chromeos/login/quick_unlock/quick_unlock_storage.h"
#include "chrome/browser/chromeos/login/reauth_stats.h"
#include "chrome/browser/chromeos/login/ui/login_display_host.h"
#include "chrome/browser/chromeos/login/ui/views/user_board_view.h"
#include "chrome/browser/chromeos/login/users/chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/multi_profile_user_controller.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/signin/easy_unlock_service.h"
#include "chrome/browser/ui/webui/chromeos/login/l10n_util.h"
#include "chrome/browser/ui/webui/chromeos/login/signin_screen_handler.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/cryptohome/cryptohome_parameters.h"
#include "chromeos/dbus/cryptohome_client.h"
#include "chromeos/dbus/dbus_method_call_status.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "components/arc/arc_util.h"
#include "components/prefs/pref_service.h"
#include "components/proximity_auth/screenlock_bridge.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/user_manager/known_user.h"
#include "components/user_manager/user_manager.h"
#include "components/user_manager/user_type.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/user_activity/user_activity_detector.h"

namespace chromeos {

namespace {

// User dictionary keys.
const char kKeyUsername[] = "username";
const char kKeyGaiaID[] = "gaiaId";
const char kKeyDisplayName[] = "displayName";
const char kKeyEmailAddress[] = "emailAddress";
const char kKeyEnterpriseDomain[] = "enterpriseDomain";
const char kKeyPublicAccount[] = "publicAccount";
const char kKeyLegacySupervisedUser[] = "legacySupervisedUser";
const char kKeyChildUser[] = "childUser";
const char kKeyDesktopUser[] = "isDesktopUser";
const char kKeySignedIn[] = "signedIn";
const char kKeyCanRemove[] = "canRemove";
const char kKeyIsOwner[] = "isOwner";
const char kKeyIsActiveDirectory[] = "isActiveDirectory";
const char kKeyInitialAuthType[] = "initialAuthType";
const char kKeyMultiProfilesAllowed[] = "isMultiProfilesAllowed";
const char kKeyMultiProfilesPolicy[] = "multiProfilesPolicy";
const char kKeyInitialLocales[] = "initialLocales";
const char kKeyInitialLocale[] = "initialLocale";
const char kKeyInitialMultipleRecommendedLocales[] =
    "initialMultipleRecommendedLocales";
const char kKeyInitialKeyboardLayout[] = "initialKeyboardLayout";
const char kKeyAllowFingerprint[] = "allowFingerprint";

// Max number of users to show.
// Please keep synced with one in signin_userlist_unittest.cc.
const size_t kMaxUsers = 18;

const int kPasswordClearTimeoutSec = 60;

void AddPublicSessionDetailsToUserDictionaryEntry(
    base::DictionaryValue* user_dict,
    const std::vector<std::string>* public_session_recommended_locales) {
  policy::BrowserPolicyConnectorChromeOS* policy_connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();

  if (policy_connector->IsCloudManaged()) {
    user_dict->SetString(kKeyEnterpriseDomain,
                         policy_connector->GetEnterpriseDomain());
  }

  std::vector<std::string> kEmptyRecommendedLocales;
  const std::vector<std::string>& recommended_locales =
      public_session_recommended_locales ?
          *public_session_recommended_locales : kEmptyRecommendedLocales;

  // Construct the list of available locales. This list consists of the
  // recommended locales, followed by all others.
  std::unique_ptr<base::ListValue> available_locales =
      GetUILanguageList(&recommended_locales, std::string());

  // Select the the first recommended locale that is actually available or the
  // current UI locale if none of them are available.
  const std::string selected_locale = FindMostRelevantLocale(
      recommended_locales,
      *available_locales.get(),
      g_browser_process->GetApplicationLocale());

  // Set |kKeyInitialLocales| to the list of available locales.
  user_dict->Set(kKeyInitialLocales, std::move(available_locales));

  // Set |kKeyInitialLocale| to the initially selected locale.
  user_dict->SetString(kKeyInitialLocale, selected_locale);

  // Set |kKeyInitialMultipleRecommendedLocales| to indicate whether the list
  // of recommended locales contains at least two entries. This is used to
  // decide whether the public session pod expands to its basic form (for zero
  // or one recommended locales) or the advanced form (two or more recommended
  // locales).
  user_dict->SetBoolean(kKeyInitialMultipleRecommendedLocales,
                        recommended_locales.size() >= 2);

  // Set |kKeyInitialKeyboardLayout| to the current keyboard layout. This
  // value will be used temporarily only because the UI immediately requests a
  // list of keyboard layouts suitable for the currently selected locale.
  user_dict->Set(kKeyInitialKeyboardLayout, GetCurrentKeyboardLayout());
}

// Returns true if the fingerprint icon should be displayed for the given
// |user|.
bool AllowFingerprintForUser(user_manager::User* user) {
  if (!user->is_logged_in())
    return false;

  quick_unlock::QuickUnlockStorage* quick_unlock_storage =
      quick_unlock::QuickUnlockFactory::GetForUser(user);
  if (!quick_unlock_storage)
    return false;

  return quick_unlock_storage->IsFingerprintAuthenticationAvailable();
}

// Returns true if dircrypto migration check should be performed.
bool ShouldCheckNeedDircryptoMigration() {
  return !base::CommandLine::ForCurrentProcess()->HasSwitch(
             switches::kDisableEncryptionMigration) &&
         arc::IsArcAvailable();
}

// Returns true if the user can run ARC based on the user type.
bool IsUserAllowedForARC(const AccountId& account_id) {
  if (!user_manager::UserManager::IsInitialized())
    return false;

  const user_manager::User* user =
      user_manager::UserManager::Get()->FindUser(account_id);
  if (!user)
    return false;

  return user->HasGaiaAccount() || user->IsActiveDirectoryUser();
}

}  // namespace

// Helper class to call cryptohome to check whether a user needs dircrypto
// migration. The check results are cached to limit calls to cryptohome.
class UserSelectionScreen::DircryptoMigrationChecker {
 public:
  explicit DircryptoMigrationChecker(UserSelectionScreen* owner)
      : owner_(owner), weak_ptr_factory_(this) {}
  ~DircryptoMigrationChecker() = default;

  // Start to check whether the given user needs dircrypto migration.
  void Check(const AccountId& account_id) {
    focused_user_ = account_id;

    auto it = needs_dircrypto_migration_cache_.find(account_id);
    if (it != needs_dircrypto_migration_cache_.end()) {
      UpdateUI(account_id, it->second);
      return;
    }

    // No banner if the user is not allowed for ARC.
    if (!IsUserAllowedForARC(account_id)) {
      UpdateUI(account_id, false);
      return;
    }

    DBusThreadManager::Get()
        ->GetCryptohomeClient()
        ->WaitForServiceToBeAvailable(
            base::Bind(&DircryptoMigrationChecker::RunCryptohomeCheck,
                       weak_ptr_factory_.GetWeakPtr(), account_id));
  }

 private:
  // WaitForServiceToBeAvailable callback to invoke NeedsDircryptoMigration when
  // cryptohome service is available.
  void RunCryptohomeCheck(const AccountId& account_id, bool service_is_ready) {
    if (!service_is_ready) {
      LOG(ERROR) << "Cryptohome is not available.";
      return;
    }

    const cryptohome::Identification cryptohome_id(account_id);
    DBusThreadManager::Get()->GetCryptohomeClient()->NeedsDircryptoMigration(
        cryptohome_id,
        base::Bind(&DircryptoMigrationChecker::
                       OnCryptohomeNeedsDircryptoMigrationCallback,
                   weak_ptr_factory_.GetWeakPtr(), account_id));
  }

  // Callback invoked when NeedsDircryptoMigration call is finished.
  void OnCryptohomeNeedsDircryptoMigrationCallback(
      const AccountId& account_id,
      DBusMethodCallStatus call_status,
      bool needs_migration) {
    if (call_status != DBUS_METHOD_CALL_SUCCESS) {
      LOG(ERROR) << "Failed to call cryptohome NeedsDircryptoMigration.";
      // Hide the banner to avoid confusion in http://crbug.com/721948.
      // Cache is not updated so that cryptohome call will still be attempted.
      UpdateUI(account_id, false);
      return;
    }

    needs_dircrypto_migration_cache_[account_id] = needs_migration;
    UpdateUI(account_id, needs_migration);
  }

  // Update UI for the given user when the check result is available.
  void UpdateUI(const AccountId& account_id, bool needs_migration) {
    // Bail if the user is not the currently focused.
    if (account_id != focused_user_)
      return;

    owner_->ShowBannerMessage(
        needs_migration ? l10n_util::GetStringUTF16(
                              IDS_LOGIN_NEEDS_DIRCRYPTO_MIGRATION_BANNER)
                        : base::string16());
  }

  UserSelectionScreen* const owner_;
  AccountId focused_user_ = EmptyAccountId();

  // Cached result of NeedsDircryptoMigration cryptohome check. Key is the
  // account id of users. True value means the user needs dircrypto migration
  // and false means dircrypto migration is done.
  std::map<AccountId, bool> needs_dircrypto_migration_cache_;

  base::WeakPtrFactory<DircryptoMigrationChecker> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DircryptoMigrationChecker);
};

UserSelectionScreen::UserSelectionScreen(const std::string& display_type)
    : BaseScreen(nullptr, OobeScreen::SCREEN_USER_SELECTION),
      display_type_(display_type),
      weak_factory_(this) {}

UserSelectionScreen::~UserSelectionScreen() {
  proximity_auth::ScreenlockBridge::Get()->SetLockHandler(nullptr);
  ui::UserActivityDetector* activity_detector = ui::UserActivityDetector::Get();
  if (activity_detector && activity_detector->HasObserver(this))
    activity_detector->RemoveObserver(this);
}

void UserSelectionScreen::InitEasyUnlock() {
  proximity_auth::ScreenlockBridge::Get()->SetLockHandler(this);
}

void UserSelectionScreen::SetLoginDisplayDelegate(
    LoginDisplay::Delegate* login_display_delegate) {
  login_display_delegate_ = login_display_delegate;
}

// static
void UserSelectionScreen::FillUserDictionary(
    user_manager::User* user,
    bool is_owner,
    bool is_signin_to_add,
    AuthType auth_type,
    const std::vector<std::string>* public_session_recommended_locales,
    base::DictionaryValue* user_dict) {
  const bool is_public_session =
      user->GetType() == user_manager::USER_TYPE_PUBLIC_ACCOUNT;
  const bool is_legacy_supervised_user =
      user->GetType() == user_manager::USER_TYPE_SUPERVISED;
  const bool is_child_user = user->GetType() == user_manager::USER_TYPE_CHILD;

  user_dict->SetString(kKeyUsername, user->GetAccountId().Serialize());
  user_dict->SetString(kKeyEmailAddress, user->display_email());
  user_dict->SetString(kKeyDisplayName, user->GetDisplayName());
  user_dict->SetBoolean(kKeyPublicAccount, is_public_session);
  user_dict->SetBoolean(kKeyLegacySupervisedUser, is_legacy_supervised_user);
  user_dict->SetBoolean(kKeyChildUser, is_child_user);
  user_dict->SetBoolean(kKeyDesktopUser, false);
  user_dict->SetInteger(kKeyInitialAuthType, auth_type);
  user_dict->SetBoolean(kKeySignedIn, user->is_logged_in());
  user_dict->SetBoolean(kKeyIsOwner, is_owner);
  user_dict->SetBoolean(kKeyIsActiveDirectory, user->IsActiveDirectoryUser());
  user_dict->SetBoolean(kKeyAllowFingerprint, AllowFingerprintForUser(user));

  FillMultiProfileUserPrefs(user, user_dict, is_signin_to_add);
  FillKnownUserPrefs(user, user_dict);

  if (is_public_session) {
    AddPublicSessionDetailsToUserDictionaryEntry(
        user_dict, public_session_recommended_locales);
  }
}

// static
void UserSelectionScreen::FillKnownUserPrefs(user_manager::User* user,
                                             base::DictionaryValue* user_dict) {
  std::string gaia_id;
  if (user_manager::known_user::FindGaiaID(user->GetAccountId(), &gaia_id)) {
    user_dict->SetString(kKeyGaiaID, gaia_id);
  }
}

// static
void UserSelectionScreen::FillMultiProfileUserPrefs(
    user_manager::User* user,
    base::DictionaryValue* user_dict,
    bool is_signin_to_add) {
  const std::string& user_id = user->GetAccountId().GetUserEmail();

  if (is_signin_to_add) {
    MultiProfileUserController* multi_profile_user_controller =
        ChromeUserManager::Get()->GetMultiProfileUserController();
    MultiProfileUserController::UserAllowedInSessionReason isUserAllowedReason;
    bool isUserAllowed = multi_profile_user_controller->IsUserAllowedInSession(
        user_id, &isUserAllowedReason);
    user_dict->SetBoolean(kKeyMultiProfilesAllowed, isUserAllowed);

    std::string behavior;
    switch (isUserAllowedReason) {
      case MultiProfileUserController::NOT_ALLOWED_OWNER_AS_SECONDARY:
        behavior = MultiProfileUserController::kBehaviorOwnerPrimaryOnly;
        break;
      default:
        behavior = multi_profile_user_controller->GetCachedValue(user_id);
    }
    user_dict->SetString(kKeyMultiProfilesPolicy, behavior);
  } else {
    user_dict->SetBoolean(kKeyMultiProfilesAllowed, true);
  }
}

// static
bool UserSelectionScreen::ShouldForceOnlineSignIn(
    const user_manager::User* user) {
  // Public sessions are always allowed to log in offline.
  // Supervised users are always allowed to log in offline.
  // For all other users, force online sign in if:
  // * The flag to force online sign-in is set for the user.
  // * The user's OAuth token is invalid or unknown.
  if (user->is_logged_in())
    return false;

  const user_manager::User::OAuthTokenStatus token_status =
      user->oauth_token_status();
  const bool is_supervised_user =
      user->GetType() == user_manager::USER_TYPE_SUPERVISED;
  const bool is_public_session =
      user->GetType() == user_manager::USER_TYPE_PUBLIC_ACCOUNT;
  const bool has_gaia_account = user->HasGaiaAccount();

  if (is_supervised_user)
    return false;

  if (is_public_session)
    return false;

  // At this point the reason for invalid token should be already set. If not,
  // this might be a leftover from an old version.
  if (has_gaia_account &&
      token_status == user_manager::User::OAUTH2_TOKEN_STATUS_INVALID)
    RecordReauthReason(user->GetAccountId(), ReauthReason::OTHER);

  // We need to force an online signin if the user is marked as requiring it,
  // or if the user's session never completed initialization (still need to
  // check for policy/management state) or if there's an invalid OAUTH token
  // that needs to be refreshed.
  return user->force_online_signin() || !user->profile_ever_initialized() ||
         (has_gaia_account &&
          (token_status == user_manager::User::OAUTH2_TOKEN_STATUS_INVALID ||
           token_status == user_manager::User::OAUTH_TOKEN_STATUS_UNKNOWN));
}

void UserSelectionScreen::SetHandler(LoginDisplayWebUIHandler* handler) {
  handler_ = handler;

  if (handler_) {
    // Forcibly refresh all of the user images, as the |handler_| instance may
    // have been reused.
    for (user_manager::User* user : users_)
      handler_->OnUserImageChanged(*user);
  }
}

void UserSelectionScreen::SetView(UserBoardView* view) {
  view_ = view;
}

void UserSelectionScreen::Init(const user_manager::UserList& users,
                               bool show_guest) {
  users_ = users;
  show_guest_ = show_guest;

  ui::UserActivityDetector* activity_detector = ui::UserActivityDetector::Get();
  if (activity_detector && !activity_detector->HasObserver(this))
    activity_detector->AddObserver(this);
}

void UserSelectionScreen::OnBeforeUserRemoved(const AccountId& account_id) {
  for (user_manager::UserList::iterator it = users_.begin(); it != users_.end();
       ++it) {
    if ((*it)->GetAccountId() == account_id) {
      users_.erase(it);
      break;
    }
  }
}

void UserSelectionScreen::OnUserRemoved(const AccountId& account_id) {
  if (!handler_)
    return;
  handler_->OnUserRemoved(account_id, users_.empty());
}

void UserSelectionScreen::OnUserImageChanged(const user_manager::User& user) {
  if (!handler_)
    return;
  handler_->OnUserImageChanged(user);
  // TODO(antrim) : updateUserImage(user.email())
}

void UserSelectionScreen::OnPasswordClearTimerExpired() {
  if (handler_)
    handler_->ClearUserPodPassword();
}

void UserSelectionScreen::OnUserActivity(const ui::Event* event) {
  if (!password_clear_timer_.IsRunning()) {
    password_clear_timer_.Start(
        FROM_HERE,
        base::TimeDelta::FromSeconds(kPasswordClearTimeoutSec),
        this,
        &UserSelectionScreen::OnPasswordClearTimerExpired);
  }
  password_clear_timer_.Reset();
}

// static
const user_manager::UserList UserSelectionScreen::PrepareUserListForSending(
    const user_manager::UserList& users,
    const AccountId& owner,
    bool is_signin_to_add) {
  user_manager::UserList users_to_send;
  bool has_owner = owner.is_valid();
  size_t max_non_owner_users = has_owner ? kMaxUsers - 1 : kMaxUsers;
  size_t non_owner_count = 0;

  for (user_manager::UserList::const_iterator it = users.begin();
       it != users.end();
       ++it) {
    bool is_owner = ((*it)->GetAccountId() == owner);
    bool is_public_account =
        ((*it)->GetType() == user_manager::USER_TYPE_PUBLIC_ACCOUNT);

    if ((is_public_account && !is_signin_to_add) || is_owner ||
        (!is_public_account && non_owner_count < max_non_owner_users)) {

      if (!is_owner)
        ++non_owner_count;
      if (is_owner && users_to_send.size() > kMaxUsers) {
        // Owner is always in the list.
        users_to_send.insert(users_to_send.begin() + (kMaxUsers - 1), *it);
        while (users_to_send.size() > kMaxUsers)
          users_to_send.erase(users_to_send.begin() + kMaxUsers);
      } else if (users_to_send.size() < kMaxUsers) {
        users_to_send.push_back(*it);
      }
    }
  }
  return users_to_send;
}

void UserSelectionScreen::SendUserList() {
  base::ListValue users_list;

  // TODO(nkostylev): Move to a separate method in UserManager.
  // http://crbug.com/230852
  bool single_user = users_.size() == 1;
  bool is_signin_to_add = LoginDisplayHost::default_host() &&
                          user_manager::UserManager::Get()->IsUserLoggedIn();
  std::string owner_email;
  chromeos::CrosSettings::Get()->GetString(chromeos::kDeviceOwner,
                                           &owner_email);
  const AccountId owner = user_manager::known_user::GetAccountId(
      owner_email, std::string() /* id */, AccountType::UNKNOWN);

  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  bool is_enterprise_managed = connector->IsEnterpriseManaged();

  const user_manager::UserList users_to_send =
      PrepareUserListForSending(users_, owner, is_signin_to_add);

  user_auth_type_map_.clear();

  const std::vector<std::string> kEmptyRecommendedLocales;
  for (user_manager::UserList::const_iterator it = users_to_send.begin();
       it != users_to_send.end();
       ++it) {
    const AccountId& account_id = (*it)->GetAccountId();
    bool is_owner = (account_id == owner);
    const bool is_public_account =
        ((*it)->GetType() == user_manager::USER_TYPE_PUBLIC_ACCOUNT);
    const AuthType initial_auth_type =
        is_public_account ? EXPAND_THEN_USER_CLICK
                          : (ShouldForceOnlineSignIn(*it) ? ONLINE_SIGN_IN
                                                          : OFFLINE_PASSWORD);
    user_auth_type_map_[account_id] = initial_auth_type;

    auto user_dict = base::MakeUnique<base::DictionaryValue>();
    const std::vector<std::string>* public_session_recommended_locales =
        public_session_recommended_locales_.find(account_id) ==
                public_session_recommended_locales_.end()
            ? &kEmptyRecommendedLocales
            : &public_session_recommended_locales_[account_id];
    FillUserDictionary(*it, is_owner, is_signin_to_add, initial_auth_type,
                       public_session_recommended_locales, user_dict.get());
    bool signed_in = (*it)->is_logged_in();

    // Single user check here is necessary because owner info might not be
    // available when running into login screen on first boot.
    // See http://crosbug.com/12723
    bool can_remove_user =
        ((!single_user || is_enterprise_managed) && account_id.is_valid() &&
         !is_owner && !is_public_account && !signed_in && !is_signin_to_add);
    user_dict->SetBoolean(kKeyCanRemove, can_remove_user);
    users_list.Append(std::move(user_dict));
  }

  handler_->LoadUsers(users_list, show_guest_);
}

void UserSelectionScreen::HandleGetUsers() {
  SendUserList();
}

void UserSelectionScreen::CheckUserStatus(const AccountId& account_id) {
  // No checks on lock screen.
  if (ScreenLocker::default_screen_locker())
    return;

  if (!token_handle_util_.get()) {
    token_handle_util_.reset(new TokenHandleUtil());
  }

  if (token_handle_util_->HasToken(account_id)) {
    token_handle_util_->CheckToken(
        account_id, base::Bind(&UserSelectionScreen::OnUserStatusChecked,
                               weak_factory_.GetWeakPtr()));
  }

  // Run dircrypto migration check only on the login screen when necessary.
  if (display_type_ == OobeUI::kLoginDisplay &&
      ShouldCheckNeedDircryptoMigration()) {
    if (!dircrypto_migration_checker_) {
      dircrypto_migration_checker_ =
          base::MakeUnique<DircryptoMigrationChecker>(this);
    }
    dircrypto_migration_checker_->Check(account_id);
  }
}

void UserSelectionScreen::OnUserStatusChecked(
    const AccountId& account_id,
    TokenHandleUtil::TokenHandleStatus status) {
  if (status == TokenHandleUtil::INVALID) {
    RecordReauthReason(account_id, ReauthReason::INVALID_TOKEN_HANDLE);
    token_handle_util_->MarkHandleInvalid(account_id);
    SetAuthType(account_id, ONLINE_SIGN_IN, base::string16());
  }
}

// EasyUnlock stuff

void UserSelectionScreen::SetAuthType(const AccountId& account_id,
                                      AuthType auth_type,
                                      const base::string16& initial_value) {
  if (GetAuthType(account_id) == FORCE_OFFLINE_PASSWORD)
    return;
  DCHECK(GetAuthType(account_id) != FORCE_OFFLINE_PASSWORD ||
         auth_type == FORCE_OFFLINE_PASSWORD);
  user_auth_type_map_[account_id] = auth_type;
  view_->SetAuthType(account_id, auth_type, initial_value);
}

proximity_auth::ScreenlockBridge::LockHandler::AuthType
UserSelectionScreen::GetAuthType(const AccountId& account_id) const {
  if (user_auth_type_map_.find(account_id) == user_auth_type_map_.end())
    return OFFLINE_PASSWORD;
  return user_auth_type_map_.find(account_id)->second;
}

proximity_auth::ScreenlockBridge::LockHandler::ScreenType
UserSelectionScreen::GetScreenType() const {
  if (display_type_ == OobeUI::kLockDisplay)
    return LOCK_SCREEN;

  if (display_type_ == OobeUI::kLoginDisplay)
    return SIGNIN_SCREEN;

  return OTHER_SCREEN;
}

void UserSelectionScreen::ShowBannerMessage(const base::string16& message) {
  view_->ShowBannerMessage(message);
}

void UserSelectionScreen::ShowUserPodCustomIcon(
    const AccountId& account_id,
    const proximity_auth::ScreenlockBridge::UserPodCustomIconOptions&
        icon_options) {
  std::unique_ptr<base::DictionaryValue> icon =
      icon_options.ToDictionaryValue();
  if (!icon || icon->empty())
    return;
  view_->ShowUserPodCustomIcon(account_id, *icon);
}

void UserSelectionScreen::HideUserPodCustomIcon(const AccountId& account_id) {
  view_->HideUserPodCustomIcon(account_id);
}

void UserSelectionScreen::EnableInput() {
  // If Easy Unlock fails to unlock the screen, re-enable the password input.
  // This is only necessary on the lock screen, because the error handling for
  // the sign-in screen uses a different code path.
  if (ScreenLocker::default_screen_locker())
    ScreenLocker::default_screen_locker()->EnableInput();
}

void UserSelectionScreen::Unlock(const AccountId& account_id) {
  DCHECK_EQ(GetScreenType(), LOCK_SCREEN);
  ScreenLocker::Hide();
}

void UserSelectionScreen::AttemptEasySignin(const AccountId& account_id,
                                            const std::string& secret,
                                            const std::string& key_label) {
  DCHECK_EQ(GetScreenType(), SIGNIN_SCREEN);

  UserContext user_context(account_id);
  user_context.SetAuthFlow(UserContext::AUTH_FLOW_EASY_UNLOCK);
  user_context.SetKey(Key(secret));
  user_context.GetKey()->SetLabel(key_label);

  login_display_delegate_->Login(user_context, SigninSpecifics());
}

void UserSelectionScreen::Show() {}

void UserSelectionScreen::Hide() {}

void UserSelectionScreen::HardLockPod(const AccountId& account_id) {
  view_->SetAuthType(account_id, OFFLINE_PASSWORD, base::string16());
  EasyUnlockService* service = GetEasyUnlockServiceForUser(account_id);
  if (!service)
    return;
  service->SetHardlockState(EasyUnlockScreenlockStateHandler::USER_HARDLOCK);
}

void UserSelectionScreen::AttemptEasyUnlock(const AccountId& account_id) {
  EasyUnlockService* service = GetEasyUnlockServiceForUser(account_id);
  if (!service)
    return;
  service->AttemptAuth(account_id);
}

void UserSelectionScreen::RecordClickOnLockIcon(const AccountId& account_id) {
  EasyUnlockService* service = GetEasyUnlockServiceForUser(account_id);
  if (!service)
    return;
  service->RecordClickOnLockIcon();
}

EasyUnlockService* UserSelectionScreen::GetEasyUnlockServiceForUser(
    const AccountId& account_id) const {
  if (GetScreenType() == OTHER_SCREEN)
    return nullptr;

  const user_manager::User* unlock_user = nullptr;
  for (const user_manager::User* user : users_) {
    if (user->GetAccountId() == account_id) {
      unlock_user = user;
      break;
    }
  }
  if (!unlock_user)
    return nullptr;

  ProfileHelper* profile_helper = ProfileHelper::Get();
  Profile* profile = profile_helper->GetProfileByUser(unlock_user);

  // The user profile should exist if and only if this is the lock screen.
  DCHECK_EQ(!!profile, GetScreenType() == LOCK_SCREEN);

  if (!profile)
    profile = profile_helper->GetSigninProfile();

  return EasyUnlockService::Get(profile);
}

}  // namespace chromeos
