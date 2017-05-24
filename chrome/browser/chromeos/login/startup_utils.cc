// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/startup_utils.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/sys_info.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "chromeos/chromeos_switches.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/web_resource/web_resource_pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "ui/base/l10n/l10n_util.h"

using content::BrowserThread;

namespace {

// Saves boolean "Local State" preference and forces its persistence to disk.
void SaveBoolPreferenceForced(const char* pref_name, bool value) {
  PrefService* prefs = g_browser_process->local_state();
  prefs->SetBoolean(pref_name, value);
  prefs->CommitPendingWrite();
}

// Saves integer "Local State" preference and forces its persistence to disk.
void SaveIntegerPreferenceForced(const char* pref_name, int value) {
  PrefService* prefs = g_browser_process->local_state();
  prefs->SetInteger(pref_name, value);
  prefs->CommitPendingWrite();
}

// Saves 64 bit signed integer "Local State" preference and forces its
// persistence to disk.
void SaveInt64PreferenceForced(const char* pref_name, int64_t value) {
  PrefService* prefs = g_browser_process->local_state();
  prefs->SetInt64(pref_name, value);
  prefs->CommitPendingWrite();
}

// Saves string "Local State" preference and forces its persistence to disk.
void SaveStringPreferenceForced(const char* pref_name,
                                const std::string& value) {
  PrefService* prefs = g_browser_process->local_state();
  prefs->SetString(pref_name, value);
  prefs->CommitPendingWrite();
}

// Returns the path to flag file indicating that both parts of OOBE were
// completed.
// On chrome device, returns /home/chronos/.oobe_completed.
// On Linux desktop, returns {DIR_USER_DATA}/.oobe_completed.
base::FilePath GetOobeCompleteFlagPath() {
  // The constant is defined here so it won't be referenced directly.
  const char kOobeCompleteFlagFilePath[] = "/home/chronos/.oobe_completed";

  if (base::SysInfo::IsRunningOnChromeOS()) {
    return base::FilePath(kOobeCompleteFlagFilePath);
  } else {
    base::FilePath user_data_dir;
    PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
    return user_data_dir.AppendASCII(".oobe_completed");
  }
}

void CreateOobeCompleteFlagFile() {
  // Create flag file for boot-time init scripts.
  const base::FilePath oobe_complete_flag_path = GetOobeCompleteFlagPath();
  if (!base::PathExists(oobe_complete_flag_path)) {
    FILE* oobe_flag_file = base::OpenFile(oobe_complete_flag_path, "w+b");
    if (oobe_flag_file == NULL)
      DLOG(WARNING) << oobe_complete_flag_path.value() << " doesn't exist.";
    else
      base::CloseFile(oobe_flag_file);
  }
}

}  // namespace

namespace chromeos {

// static
void StartupUtils::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(prefs::kOobeComplete, false);
  registry->RegisterStringPref(prefs::kOobeScreenPending, "");
  registry->RegisterBooleanPref(prefs::kOobeMdMode, false);
  registry->RegisterIntegerPref(prefs::kDeviceRegistered, -1);
  registry->RegisterBooleanPref(prefs::kEnrollmentRecoveryRequired, false);
  registry->RegisterStringPref(prefs::kInitialLocale, "en-US");
  registry->RegisterBooleanPref(prefs::kIsBootstrappingSlave, false);
  registry->RegisterBooleanPref(prefs::kOobeControllerDetected, false);
  registry->RegisterInt64Pref(prefs::kOobeTimeOfLastUpdateCheckWithoutUpdate,
                              0);
}

// static
bool StartupUtils::IsEulaAccepted() {
  return g_browser_process->local_state()->GetBoolean(prefs::kEulaAccepted);
}

// static
bool StartupUtils::IsOobeCompleted() {
  return g_browser_process->local_state()->GetBoolean(prefs::kOobeComplete);
}

// static
void StartupUtils::MarkEulaAccepted() {
  SaveBoolPreferenceForced(prefs::kEulaAccepted, true);
}

// static
void StartupUtils::MarkOobeCompleted() {
  // Forcing the second pref will force this one as well. Even if this one
  // doesn't end up synced it is only going to eat up a couple of bytes with no
  // side-effects.
  g_browser_process->local_state()->ClearPref(prefs::kOobeScreenPending);
  SaveBoolPreferenceForced(prefs::kOobeComplete, true);

  g_browser_process->local_state()->ClearPref(prefs::kIsBootstrappingSlave);

  // Successful enrollment implies that recovery is not required.
  SaveBoolPreferenceForced(prefs::kEnrollmentRecoveryRequired, false);
}

// static
void StartupUtils::SaveOobePendingScreen(const std::string& screen) {
  SaveStringPreferenceForced(prefs::kOobeScreenPending, screen);
}

// static
base::TimeDelta StartupUtils::GetTimeSinceOobeFlagFileCreation() {
  const base::FilePath oobe_complete_flag_path = GetOobeCompleteFlagPath();
  base::File::Info file_info;
  if (base::GetFileInfo(oobe_complete_flag_path, &file_info))
    return base::Time::Now() - file_info.creation_time;
  return base::TimeDelta();
}

// static
bool StartupUtils::IsDeviceRegistered() {
  int value =
      g_browser_process->local_state()->GetInteger(prefs::kDeviceRegistered);
  if (value > 0) {
    // Recreate flag file in case it was lost.
    BrowserThread::PostTask(
        BrowserThread::FILE,
        FROM_HERE,
        base::Bind(&CreateOobeCompleteFlagFile));
    return true;
  } else if (value == 0) {
    return false;
  } else {
    // Pref is not set. For compatibility check flag file. It causes blocking
    // IO on UI thread. But it's required for update from old versions.
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    const base::FilePath oobe_complete_flag_path = GetOobeCompleteFlagPath();
    bool file_exists = base::PathExists(oobe_complete_flag_path);
    SaveIntegerPreferenceForced(prefs::kDeviceRegistered, file_exists ? 1 : 0);
    return file_exists;
  }
}

// static
void StartupUtils::MarkDeviceRegistered(const base::Closure& done_callback) {
  SaveIntegerPreferenceForced(prefs::kDeviceRegistered, 1);
  if (done_callback.is_null()) {
    BrowserThread::PostTask(
        BrowserThread::FILE,
        FROM_HERE,
        base::Bind(&CreateOobeCompleteFlagFile));
  } else {
    BrowserThread::PostTaskAndReply(
        BrowserThread::FILE,
        FROM_HERE,
        base::Bind(&CreateOobeCompleteFlagFile),
        done_callback);
  }
}

// static
void StartupUtils::MarkEnrollmentRecoveryRequired() {
  SaveBoolPreferenceForced(prefs::kEnrollmentRecoveryRequired, true);
}

// static
std::string StartupUtils::GetInitialLocale() {
  std::string locale =
      g_browser_process->local_state()->GetString(prefs::kInitialLocale);
  if (!l10n_util::IsValidLocaleSyntax(locale))
    locale = "en-US";
  return locale;
}

// static
void StartupUtils::SetInitialLocale(const std::string& locale) {
  if (l10n_util::IsValidLocaleSyntax(locale))
    SaveStringPreferenceForced(prefs::kInitialLocale, locale);
  else
    NOTREACHED();
}

// static
void StartupUtils::SaveTimeOfLastUpdateCheckWithoutUpdate(base::Time time) {
  SaveInt64PreferenceForced(prefs::kOobeTimeOfLastUpdateCheckWithoutUpdate,
                            time.ToInternalValue());
}

// static
void StartupUtils::ClearTimeOfLastUpdateCheckWithoutUpdate() {
  PrefService* prefs = g_browser_process->local_state();
  prefs->ClearPref(prefs::kOobeTimeOfLastUpdateCheckWithoutUpdate);
  prefs->CommitPendingWrite();
}

// static
base::Time StartupUtils::GetTimeOfLastUpdateCheckWithoutUpdate() {
  return base::Time::FromInternalValue(
      g_browser_process->local_state()->GetInt64(
          prefs::kOobeTimeOfLastUpdateCheckWithoutUpdate));
}

}  // namespace chromeos
