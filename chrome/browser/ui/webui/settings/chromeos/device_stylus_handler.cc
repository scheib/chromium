// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/device_stylus_handler.h"

#include "ash/system/palette/palette_utils.h"
#include "base/bind.h"
#include "chrome/browser/chromeos/arc/arc_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"
#include "ui/events/devices/input_device_manager.h"

namespace chromeos {
namespace settings {

namespace {

// Keys in objects passed to onNoteTakingAppsUpdated.
constexpr char kAppNameKey[] = "name";
constexpr char kAppIdKey[] = "value";
constexpr char kAppPreferredKey[] = "preferred";

}  // namespace

StylusHandler::StylusHandler() {
  NoteTakingHelper::Get()->AddObserver(this);
  ui::InputDeviceManager::GetInstance()->AddObserver(this);
}

StylusHandler::~StylusHandler() {
  ui::InputDeviceManager::GetInstance()->RemoveObserver(this);
  NoteTakingHelper::Get()->RemoveObserver(this);
}

void StylusHandler::RegisterMessages() {
  DCHECK(web_ui());

  web_ui()->RegisterMessageCallback(
      "initializeStylusSettings",
      base::Bind(&StylusHandler::HandleInitialize, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "requestNoteTakingApps",
      base::Bind(&StylusHandler::RequestApps, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setPreferredNoteTakingApp",
      base::Bind(&StylusHandler::SetPreferredNoteTakingApp,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "showPlayStoreApps",
      base::Bind(&StylusHandler::ShowPlayStoreApps, base::Unretained(this)));
}

void StylusHandler::OnAvailableNoteTakingAppsUpdated() {
  UpdateNoteTakingApps();
}

void StylusHandler::OnDeviceListsComplete() {
  SendHasStylus();
}

void StylusHandler::UpdateNoteTakingApps() {
  bool waiting_for_android = false;
  note_taking_app_ids_.clear();
  base::ListValue apps_list;

  NoteTakingHelper* helper = NoteTakingHelper::Get();
  if (helper->play_store_enabled() && !helper->android_apps_received()) {
    // If Play Store is enabled but not ready yet, let the JS know so it can
    // disable the menu and display an explanatory message.
    waiting_for_android = true;
  } else {
    std::vector<NoteTakingAppInfo> available_apps =
        helper->GetAvailableApps(Profile::FromWebUI(web_ui()));
    for (const NoteTakingAppInfo& info : available_apps) {
      auto dict = base::MakeUnique<base::DictionaryValue>();
      dict->SetString(kAppNameKey, info.name);
      dict->SetString(kAppIdKey, info.app_id);
      dict->SetBoolean(kAppPreferredKey, info.preferred);
      apps_list.Append(std::move(dict));

      note_taking_app_ids_.insert(info.app_id);
    }
  }

  AllowJavascript();
  FireWebUIListener("onNoteTakingAppsUpdated", apps_list,
                    base::Value(waiting_for_android));
}

void StylusHandler::RequestApps(const base::ListValue* unused_args) {
  UpdateNoteTakingApps();
}

void StylusHandler::SetPreferredNoteTakingApp(const base::ListValue* args) {
  std::string app_id;
  CHECK(args->GetString(0, &app_id));

  // Sanity check: make sure that the ID we got back from WebUI is in the
  // currently-available set.
  if (!note_taking_app_ids_.count(app_id)) {
    LOG(ERROR) << "Got unknown note-taking-app ID \"" << app_id << "\"";
    return;
  }

  NoteTakingHelper::Get()->SetPreferredApp(Profile::FromWebUI(web_ui()),
                                           app_id);
}

void StylusHandler::HandleInitialize(const base::ListValue* args) {
  if (ui::InputDeviceManager::GetInstance()->AreDeviceListsComplete())
    SendHasStylus();
}

void StylusHandler::SendHasStylus() {
  DCHECK(ui::InputDeviceManager::GetInstance()->AreDeviceListsComplete());
  AllowJavascript();
  FireWebUIListener("has-stylus-changed",
                    base::Value(ash::palette_utils::HasStylusInput()));
}

void StylusHandler::ShowPlayStoreApps(const base::ListValue* args) {
  std::string apps_url;
  args->GetString(0, &apps_url);
  Profile* profile = Profile::FromWebUI(web_ui());
  if (!arc::IsArcAllowedForProfile(profile)) {
    VLOG(1) << "ARC is not enabled for this profile";
    return;
  }

  arc::LaunchPlayStoreWithUrl(apps_url);
}

}  // namespace settings
}  // namespace chromeos
