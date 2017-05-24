// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/profile_info_handler.h"

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/ui/user_manager.h"
#include "chrome/common/pref_names.h"
#include "ui/base/webui/web_ui_util.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/ui/webui/options/chromeos/user_image_source.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/notification_service.h"
#else
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/profiles/profile_statistics.h"
#include "chrome/browser/profiles/profile_statistics_factory.h"
#include "third_party/skia/include/core/SkBitmap.h"
#endif

namespace settings {

// static
const char ProfileInfoHandler::kProfileInfoChangedEventName[] =
    "profile-info-changed";
const char
    ProfileInfoHandler::kProfileManagesSupervisedUsersChangedEventName[] =
        "profile-manages-supervised-users-changed";
const char ProfileInfoHandler::kProfileStatsCountReadyEventName[] =
    "profile-stats-count-ready";

ProfileInfoHandler::ProfileInfoHandler(Profile* profile)
    : profile_(profile),
#if defined(OS_CHROMEOS)
      user_manager_observer_(this),
#endif
      profile_observer_(this),
      callback_weak_ptr_factory_(this) {
#if defined(OS_CHROMEOS)
  // Set up the chrome://userimage/ source.
  content::URLDataSource::Add(profile,
                              new chromeos::options::UserImageSource());
#endif
}

ProfileInfoHandler::~ProfileInfoHandler() {}

void ProfileInfoHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getProfileInfo", base::Bind(&ProfileInfoHandler::HandleGetProfileInfo,
                                   base::Unretained(this)));
#if !defined(OS_CHROMEOS)
  web_ui()->RegisterMessageCallback(
      "getProfileStatsCount",
      base::Bind(&ProfileInfoHandler::HandleGetProfileStats,
                 base::Unretained(this)));
#endif
  web_ui()->RegisterMessageCallback(
      "getProfileManagesSupervisedUsers",
      base::Bind(&ProfileInfoHandler::HandleGetProfileManagesSupervisedUsers,
                 base::Unretained(this)));
}

void ProfileInfoHandler::OnJavascriptAllowed() {
  profile_observer_.Add(
      &g_browser_process->profile_manager()->GetProfileAttributesStorage());

  PrefService* prefs = profile_->GetPrefs();
  profile_pref_registrar_.Init(prefs);
  profile_pref_registrar_.Add(
      prefs::kSupervisedUsers,
      base::Bind(&ProfileInfoHandler::PushProfileManagesSupervisedUsersStatus,
                 base::Unretained(this)));

#if defined(OS_CHROMEOS)
  user_manager_observer_.Add(user_manager::UserManager::Get());
#endif
}

void ProfileInfoHandler::OnJavascriptDisallowed() {
  callback_weak_ptr_factory_.InvalidateWeakPtrs();

  profile_observer_.Remove(
      &g_browser_process->profile_manager()->GetProfileAttributesStorage());

  profile_pref_registrar_.RemoveAll();

#if defined(OS_CHROMEOS)
  user_manager_observer_.Remove(user_manager::UserManager::Get());
#endif
}

#if defined(OS_CHROMEOS)
void ProfileInfoHandler::OnUserImageChanged(const user_manager::User& user) {
  PushProfileInfo();
}
#endif

void ProfileInfoHandler::OnProfileNameChanged(
    const base::FilePath& /* profile_path */,
    const base::string16& /* old_profile_name */) {
  PushProfileInfo();
}

void ProfileInfoHandler::OnProfileAvatarChanged(
    const base::FilePath& /* profile_path */) {
  PushProfileInfo();
}

void ProfileInfoHandler::HandleGetProfileInfo(const base::ListValue* args) {
  AllowJavascript();

  CHECK_EQ(1U, args->GetSize());
  const base::Value* callback_id;
  CHECK(args->Get(0, &callback_id));

  ResolveJavascriptCallback(*callback_id, *GetAccountNameAndIcon());
}

#if !defined(OS_CHROMEOS)
void ProfileInfoHandler::HandleGetProfileStats(const base::ListValue* args) {
  AllowJavascript();

  // Because there is open browser window for the current profile, statistics
  // from the ProfileAttributesStorage may not be up-to-date or may be missing
  // (e.g., |item.success| is false). Therefore, query the actual statistics.
  ProfileStatisticsFactory::GetForProfile(profile_)->GatherStatistics(
      base::Bind(&ProfileInfoHandler::PushProfileStatsCount,
                 callback_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileInfoHandler::PushProfileStatsCount(
    profiles::ProfileCategoryStats stats) {
  int count = 0;
  for (const auto& item : stats) {
    std::unique_ptr<base::DictionaryValue> stat(new base::DictionaryValue);
    if (!item.success) {
      count = 0;
      break;
    }
    count += item.count;
  }
  // PushProfileStatsCount gets invoked multiple times as each stat becomes
  // available. Therefore, webUIListenerCallback mechanism is used instead of
  // the Promise callback approach.
  FireWebUIListener(kProfileStatsCountReadyEventName, base::Value(count));
}
#endif

void ProfileInfoHandler::HandleGetProfileManagesSupervisedUsers(
    const base::ListValue* args) {
  AllowJavascript();

  CHECK_EQ(1U, args->GetSize());
  const base::Value* callback_id;
  CHECK(args->Get(0, &callback_id));

  ResolveJavascriptCallback(*callback_id,
                            base::Value(IsProfileManagingSupervisedUsers()));
}

void ProfileInfoHandler::PushProfileInfo() {
  FireWebUIListener(kProfileInfoChangedEventName, *GetAccountNameAndIcon());
}

void ProfileInfoHandler::PushProfileManagesSupervisedUsersStatus() {
  CallJavascriptFunction(
      "cr.webUIListenerCallback",
      base::Value(kProfileManagesSupervisedUsersChangedEventName),
      base::Value(IsProfileManagingSupervisedUsers()));
}

std::unique_ptr<base::DictionaryValue>
ProfileInfoHandler::GetAccountNameAndIcon() const {
  std::string name;
  std::string icon_url;

#if defined(OS_CHROMEOS)
  const user_manager::User* user =
      chromeos::ProfileHelper::Get()->GetUserByProfile(profile_);
  DCHECK(user);
  name = base::UTF16ToUTF8(user->GetDisplayName());

  // Get image as data URL instead of using chrome://userimage source to avoid
  // issues with caching.
  scoped_refptr<base::RefCountedMemory> image =
      chromeos::options::UserImageSource::GetUserImage(user->GetAccountId());
  icon_url = webui::GetPngDataUrl(image->front(), image->size());
#else   // !defined(OS_CHROMEOS)
  ProfileAttributesEntry* entry;
  if (g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_->GetPath(), &entry)) {
    name = base::UTF16ToUTF8(entry->GetName());
    // TODO(crbug.com/710660): return chrome://theme/IDR_PROFILE_AVATAR_*
    // and update theme_source.cc to get high res avatar icons. This does less
    // work here, sends less over IPC, and is more stable with returned results.
    int kAvatarIconSize = 40.f * web_ui()->GetDeviceScaleFactor();
    gfx::Image icon = profiles::GetSizedAvatarIcon(
        entry->GetAvatarIcon(), true, kAvatarIconSize, kAvatarIconSize);
    icon_url = webui::GetBitmapDataUrl(icon.AsBitmap());
  }
#endif  // defined(OS_CHROMEOS)

  base::DictionaryValue* response = new base::DictionaryValue();
  response->SetString("name", name);
  response->SetString("iconUrl", icon_url);
  return base::WrapUnique(response);
}

bool ProfileInfoHandler::IsProfileManagingSupervisedUsers() const {
  return !profile_->GetPrefs()->GetDictionary(prefs::kSupervisedUsers)->empty();
}

}  // namespace settings
