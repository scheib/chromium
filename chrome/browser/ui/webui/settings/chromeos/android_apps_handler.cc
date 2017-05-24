// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/android_apps_handler.h"

#include "base/values.h"
#include "chrome/browser/chromeos/arc/arc_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"  // kSettingsAppId
#include "ui/events/event_constants.h"

namespace chromeos {
namespace settings {

AndroidAppsHandler::AndroidAppsHandler(Profile* profile)
    : arc_prefs_observer_(this),
      arc_session_manager_observer_(this),
      profile_(profile),
      weak_ptr_factory_(this) {}

AndroidAppsHandler::~AndroidAppsHandler() {}

void AndroidAppsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "requestAndroidAppsInfo",
      base::Bind(&AndroidAppsHandler::HandleRequestAndroidAppsInfo,
                 weak_ptr_factory_.GetWeakPtr()));
  web_ui()->RegisterMessageCallback(
      "showAndroidAppsSettings",
      base::Bind(&AndroidAppsHandler::ShowAndroidAppsSettings,
                 weak_ptr_factory_.GetWeakPtr()));
}

void AndroidAppsHandler::OnJavascriptAllowed() {
  ArcAppListPrefs* arc_prefs = ArcAppListPrefs::Get(profile_);
  if (arc_prefs) {
    arc_prefs_observer_.Add(arc_prefs);
    // arc::ArcSessionManager is assosiated with primary profile.
    arc_session_manager_observer_.Add(arc::ArcSessionManager::Get());
  }
}

void AndroidAppsHandler::OnJavascriptDisallowed() {
  arc_prefs_observer_.RemoveAll();
  arc_session_manager_observer_.RemoveAll();
}

void AndroidAppsHandler::OnAppRegistered(
    const std::string& app_id,
    const ArcAppListPrefs::AppInfo& app_info) {
  OnAppChanged(app_id);
}

void AndroidAppsHandler::OnAppRemoved(const std::string& app_id) {
  OnAppChanged(app_id);
}

void AndroidAppsHandler::OnAppChanged(const std::string& app_id) {
  if (app_id != arc::kSettingsAppId)
    return;
  SendAndroidAppsInfo();
}

void AndroidAppsHandler::OnArcPlayStoreEnabledChanged(bool enabled) {
  SendAndroidAppsInfo();
}

std::unique_ptr<base::DictionaryValue>
AndroidAppsHandler::BuildAndroidAppsInfo() {
  std::unique_ptr<base::DictionaryValue> info(new base::DictionaryValue);
  info->SetBoolean("playStoreEnabled",
                   arc::IsArcPlayStoreEnabledForProfile(profile_));
  info->SetBoolean(
      "settingsAppAvailable",
      ArcAppListPrefs::Get(profile_)->IsRegistered(arc::kSettingsAppId));
  return info;
}

void AndroidAppsHandler::HandleRequestAndroidAppsInfo(
    const base::ListValue* args) {
  SendAndroidAppsInfo();
}

void AndroidAppsHandler::SendAndroidAppsInfo() {
  AllowJavascript();
  std::unique_ptr<base::DictionaryValue> info = BuildAndroidAppsInfo();
  FireWebUIListener("android-apps-info-update", *info);
}

void AndroidAppsHandler::ShowAndroidAppsSettings(const base::ListValue* args) {
  CHECK_EQ(1U, args->GetSize());
  bool activated_from_keyboard = false;
  args->GetBoolean(0, &activated_from_keyboard);
  int flags = activated_from_keyboard ? ui::EF_NONE : ui::EF_LEFT_MOUSE_BUTTON;

  // Settings in secondary profile cannot access ARC.
  CHECK(arc::IsArcAllowedForProfile(profile_));
  arc::LaunchAndroidSettingsApp(profile_, flags);
}

}  // namespace settings
}  // namespace chromeos
