// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/users_private/users_private_api.h"

#include <stddef.h>

#include <utility>

#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/extensions/users_private/users_private_delegate.h"
#include "chrome/browser/chromeos/extensions/users_private/users_private_delegate_factory.h"
#include "chrome/browser/chromeos/ownership/owner_settings_service_chromeos.h"
#include "chrome/browser/chromeos/ownership/owner_settings_service_chromeos_factory.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/extensions/chrome_extension_function.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/users_private.h"
#include "chromeos/settings/cros_settings_names.h"
#include "components/user_manager/user_manager.h"
#include "components/user_manager/user_names.h"
#include "extensions/browser/extension_function_registry.h"
#include "google_apis/gaia/gaia_auth_util.h"

namespace extensions {

////////////////////////////////////////////////////////////////////////////////
// UsersPrivateGetWhitelistedUsersFunction

UsersPrivateGetWhitelistedUsersFunction::
    UsersPrivateGetWhitelistedUsersFunction()
    : chrome_details_(this) {
}

UsersPrivateGetWhitelistedUsersFunction::
    ~UsersPrivateGetWhitelistedUsersFunction() {
}

ExtensionFunction::ResponseAction
UsersPrivateGetWhitelistedUsersFunction::Run() {
  Profile* profile = chrome_details_.GetProfile();
  std::unique_ptr<base::ListValue> user_list(new base::ListValue);

  // Non-owners should not be able to see the list of users.
  if (!chromeos::ProfileHelper::IsOwnerProfile(profile))
    return RespondNow(OneArgument(std::move(user_list)));

  // Create one list to set. This is needed because user white list update is
  // asynchronous and sequential. Before previous write comes back, cached list
  // is stale and should not be used for appending. See http://crbug.com/127215
  std::unique_ptr<base::ListValue> email_list;

  UsersPrivateDelegate* delegate =
      UsersPrivateDelegateFactory::GetForBrowserContext(browser_context());
  PrefsUtil* prefs_util = delegate->GetPrefsUtil();

  std::unique_ptr<api::settings_private::PrefObject> users_pref_object =
      prefs_util->GetPref(chromeos::kAccountsPrefUsers);
  if (users_pref_object->value) {
    const base::ListValue* existing = nullptr;
    users_pref_object->value->GetAsList(&existing);
    email_list.reset(existing->DeepCopy());
  } else {
    email_list.reset(new base::ListValue());
  }

  // Remove all supervised users. On the next step only supervised users present
  // on the device will be added back. Thus not present SU are removed.
  // No need to remove usual users as they can simply login back.
  for (size_t i = 0; i < email_list->GetSize(); ++i) {
    std::string whitelisted_user;
    email_list->GetString(i, &whitelisted_user);
    if (gaia::ExtractDomainName(whitelisted_user) ==
        user_manager::kSupervisedUserDomain) {
      email_list->Remove(i, NULL);
      --i;
    }
  }

  user_manager::UserManager* user_manager = user_manager::UserManager::Get();
  const user_manager::UserList& users = user_manager->GetUsers();
  for (const auto* user : users) {
    email_list->AppendIfNotPresent(
        base::MakeUnique<base::Value>(user->GetAccountId().GetUserEmail()));
  }

  if (chromeos::OwnerSettingsServiceChromeOS* service =
          chromeos::OwnerSettingsServiceChromeOSFactory::GetForBrowserContext(
              profile)) {
    service->Set(chromeos::kAccountsPrefUsers, *email_list.get());
  }

  // Now populate the list of User objects for returning to the JS.
  for (size_t i = 0; i < email_list->GetSize(); ++i) {
    std::string email;
    email_list->GetString(i, &email);
    AccountId account_id = AccountId::FromUserEmail(email);
    const user_manager::User* user = user_manager->FindUser(account_id);
    api::users_private::User api_user;
    if (user) {
      api_user.email = email;
      api_user.display_email = user->GetDisplayEmail();
      api_user.name = base::UTF16ToUTF8(user->GetDisplayName());
      api_user.is_owner =
          user->GetAccountId() == user_manager->GetOwnerAccountId();
      api_user.is_supervised = user->IsSupervised();
    } else {
      // User is unknown (i.e. not on device).
      api_user.email = email;
      api_user.display_email = email;
      api_user.name = email;
      api_user.is_owner = false;
      api_user.is_supervised = false;
    }
    user_list->Append(api_user.ToValue());
  }

  return RespondNow(OneArgument(std::move(user_list)));
}

////////////////////////////////////////////////////////////////////////////////
// UsersPrivateAddWhitelistedUserFunction

UsersPrivateAddWhitelistedUserFunction::UsersPrivateAddWhitelistedUserFunction()
    : chrome_details_(this) {
}

UsersPrivateAddWhitelistedUserFunction::
    ~UsersPrivateAddWhitelistedUserFunction() {
}

ExtensionFunction::ResponseAction
UsersPrivateAddWhitelistedUserFunction::Run() {
  std::unique_ptr<api::users_private::AddWhitelistedUser::Params> parameters =
      api::users_private::AddWhitelistedUser::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(parameters.get());

  // Non-owners should not be able to add users.
  if (!chromeos::ProfileHelper::IsOwnerProfile(chrome_details_.GetProfile())) {
    return RespondNow(OneArgument(base::MakeUnique<base::Value>(false)));
  }

  std::string username = gaia::CanonicalizeEmail(parameters->email);
  if (chromeos::CrosSettings::Get()->FindEmailInList(
          chromeos::kAccountsPrefUsers, username, NULL)) {
    return RespondNow(OneArgument(base::MakeUnique<base::Value>(false)));
  }

  base::Value username_value(username);

  UsersPrivateDelegate* delegate =
      UsersPrivateDelegateFactory::GetForBrowserContext(browser_context());
  PrefsUtil* prefs_util = delegate->GetPrefsUtil();
  bool added = prefs_util->AppendToListCrosSetting(chromeos::kAccountsPrefUsers,
                                                   username_value);
  return RespondNow(OneArgument(base::MakeUnique<base::Value>(added)));
}

////////////////////////////////////////////////////////////////////////////////
// UsersPrivateRemoveWhitelistedUserFunction

UsersPrivateRemoveWhitelistedUserFunction::
    UsersPrivateRemoveWhitelistedUserFunction()
    : chrome_details_(this) {
}

UsersPrivateRemoveWhitelistedUserFunction::
    ~UsersPrivateRemoveWhitelistedUserFunction() {
}

ExtensionFunction::ResponseAction
UsersPrivateRemoveWhitelistedUserFunction::Run() {
  std::unique_ptr<api::users_private::RemoveWhitelistedUser::Params>
      parameters =
          api::users_private::RemoveWhitelistedUser::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(parameters.get());

  // Non-owners should not be able to remove users.
  if (!chromeos::ProfileHelper::IsOwnerProfile(chrome_details_.GetProfile())) {
    return RespondNow(OneArgument(base::MakeUnique<base::Value>(false)));
  }

  base::Value canonical_email(gaia::CanonicalizeEmail(parameters->email));

  UsersPrivateDelegate* delegate =
      UsersPrivateDelegateFactory::GetForBrowserContext(browser_context());
  PrefsUtil* prefs_util = delegate->GetPrefsUtil();
  bool removed = prefs_util->RemoveFromListCrosSetting(
      chromeos::kAccountsPrefUsers, canonical_email);
  user_manager::UserManager::Get()->RemoveUser(
      AccountId::FromUserEmail(parameters->email), NULL);
  return RespondNow(OneArgument(base::MakeUnique<base::Value>(removed)));
}

////////////////////////////////////////////////////////////////////////////////
// UsersPrivateIsCurrentUserOwnerFunction

UsersPrivateIsCurrentUserOwnerFunction::UsersPrivateIsCurrentUserOwnerFunction()
    : chrome_details_(this) {
}

UsersPrivateIsCurrentUserOwnerFunction::
    ~UsersPrivateIsCurrentUserOwnerFunction() {
}

ExtensionFunction::ResponseAction
UsersPrivateIsCurrentUserOwnerFunction::Run() {
  bool is_owner =
      chromeos::ProfileHelper::IsOwnerProfile(chrome_details_.GetProfile());
  return RespondNow(OneArgument(base::MakeUnique<base::Value>(is_owner)));
}

////////////////////////////////////////////////////////////////////////////////
// UsersPrivateIsWhitelistManagedFunction

UsersPrivateIsWhitelistManagedFunction::
    UsersPrivateIsWhitelistManagedFunction() {
}

UsersPrivateIsWhitelistManagedFunction::
    ~UsersPrivateIsWhitelistManagedFunction() {
}

ExtensionFunction::ResponseAction
UsersPrivateIsWhitelistManagedFunction::Run() {
  bool is_managed = g_browser_process->platform_part()
                        ->browser_policy_connector_chromeos()
                        ->IsEnterpriseManaged();
  return RespondNow(OneArgument(base::MakeUnique<base::Value>(is_managed)));
}

}  // namespace extensions
