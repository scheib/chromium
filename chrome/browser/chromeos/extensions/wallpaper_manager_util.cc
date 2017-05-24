// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/wallpaper_manager_util.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/metrics/field_trial.h"
#include "chrome/browser/chromeos/arc/arc_util.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/app_list/arc/arc_app_list_prefs.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"
#include "chrome/browser/ui/extensions/app_launch_params.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chromeos/chromeos_switches.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/event_constants.h"

namespace wallpaper_manager_util {

namespace {

const char kAndroidWallpapersAppTrialName[] = "AndroidWallpapersAppOnChromeOS";
const char kEnableAndroidWallpapersApp[] =
    "Enable-android-wallpapers-app_Dogfood";

}  // namespace

const char kAndroidWallpapersAppPackage[] = "com.google.android.apps.wallpaper";
const char kAndroidWallpapersAppActivity[] =
    "com.google.android.apps.wallpaper.picker.CategoryPickerActivity";

// Only if the current profile is the primary profile && ARC service is enabled
// && the Android Wallpapers App has been installed && the finch experiment or
// chrome flag is enabled, launch the Android Wallpapers App. Otherwise launch
// the old Chrome OS Wallpaper Picker App.
bool ShouldUseAndroidWallpapersApp(Profile* profile) {
  if (!chromeos::ProfileHelper::IsPrimaryProfile(profile))
    return false;

  // Check if the Google Play Store is enabled.
  if (!arc::IsArcPlayStoreEnabledForProfile(profile))
    return false;

  // Check if Android Wallpapers App has been installed.
  const ArcAppListPrefs* const prefs = ArcAppListPrefs::Get(profile);
  if (!prefs || prefs->GetAppsForPackage(kAndroidWallpapersAppPackage).empty())
    return false;

  // Check if the finch experiment or the chrome flag is enabled.
  if (base::FieldTrialList::FindFullName(kAndroidWallpapersAppTrialName) !=
          kEnableAndroidWallpapersApp &&
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
          chromeos::switches::kEnableAndroidWallpapersApp)) {
    return false;
  }

  return true;
}

void OpenWallpaperManager() {
  Profile* profile = ProfileManager::GetActiveUserProfile();
  DCHECK(profile);

  if (ShouldUseAndroidWallpapersApp(profile)) {
    const std::string app_id = ArcAppListPrefs::GetAppId(
        kAndroidWallpapersAppPackage, kAndroidWallpapersAppActivity);
    arc::LaunchApp(profile, app_id, ui::EF_NONE);
  } else {
    ExtensionService* service =
        extensions::ExtensionSystem::Get(profile)->extension_service();
    if (!service)
      return;

    const extensions::Extension* extension =
        service->GetExtensionById(extension_misc::kWallpaperManagerId, false);
    if (!extension)
      return;

    OpenApplication(AppLaunchParams(
        profile, extension, extensions::LAUNCH_CONTAINER_WINDOW,
        WindowOpenDisposition::NEW_WINDOW, extensions::SOURCE_CHROME_INTERNAL));
  }
}

}  // namespace wallpaper_manager_util
