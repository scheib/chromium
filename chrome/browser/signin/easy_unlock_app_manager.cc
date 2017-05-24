// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/easy_unlock_app_manager.h"

#include <utility>

#include "base/command_line.h"
#include "base/location.h"
#include "base/macros.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/api/screenlock_private/screenlock_private_api.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/common/extensions/api/easy_unlock_private.h"
#include "chrome/common/extensions/extension_constants.h"
#include "components/proximity_auth/logging/logging.h"
#include "components/proximity_auth/switches.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/one_shot_event.h"

#if defined(OS_CHROMEOS)
#include "base/sys_info.h"
#endif

namespace {

class EasyUnlockAppManagerImpl : public EasyUnlockAppManager {
 public:
  EasyUnlockAppManagerImpl(extensions::ExtensionSystem* extension_system,
                           int manifest_id,
                           const base::FilePath& app_path);
  ~EasyUnlockAppManagerImpl() override;

  // EasyUnlockAppManager overrides.
  void EnsureReady(const base::Closure& ready_callback) override;
  void LaunchSetup() override;
  void LoadApp() override;
  void DisableAppIfLoaded() override;
  void ReloadApp() override;
  bool SendUserUpdatedEvent(const std::string& user_id,
                            bool is_logged_in,
                            bool data_ready) override;
  bool SendAuthAttemptEvent() override;

 private:
  extensions::ExtensionSystem* extension_system_;

  // The Easy Unlock app id.
  std::string app_id_;
  int manifest_id_;

  // The path from which Easy Unlock app should be loaded.
  base::FilePath app_path_;

  DISALLOW_COPY_AND_ASSIGN(EasyUnlockAppManagerImpl);
};

EasyUnlockAppManagerImpl::EasyUnlockAppManagerImpl(
    extensions::ExtensionSystem* extension_system,
    int manifest_id,
    const base::FilePath& app_path)
    : extension_system_(extension_system),
      app_id_(extension_misc::kEasyUnlockAppId),
      manifest_id_(manifest_id),
      app_path_(app_path) {
}

EasyUnlockAppManagerImpl::~EasyUnlockAppManagerImpl() {
}

void EasyUnlockAppManagerImpl::EnsureReady(
    const base::Closure& ready_callback) {
  extension_system_->ready().Post(FROM_HERE, ready_callback);
}

void EasyUnlockAppManagerImpl::LaunchSetup() {
  PA_LOG(WARNING) << "LaunchSetup()";
  ExtensionService* extension_service = extension_system_->extension_service();
  if (!extension_service)
    return;
  PA_LOG(WARNING) << "got extension service";

  const extensions::Extension* extension =
      extension_service->GetExtensionById(app_id_, false);
  if (!extension) {
    PA_LOG(WARNING) << "No extension";
    return;
  }

  PA_LOG(WARNING) << "launching app...";
  OpenApplication(AppLaunchParams(extension_service->profile(), extension,
                                  extensions::LAUNCH_CONTAINER_WINDOW,
                                  WindowOpenDisposition::NEW_WINDOW,
                                  extensions::SOURCE_CHROME_INTERNAL));
}

void EasyUnlockAppManagerImpl::LoadApp() {
  ExtensionService* extension_service = extension_system_->extension_service();
  if (!extension_service)
    return;

#if defined(OS_CHROMEOS)
  // TODO(xiyuan): Remove this when the app is bundled with chrome.
  if (!base::SysInfo::IsRunningOnChromeOS() &&
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
          proximity_auth::switches::kForceLoadEasyUnlockAppInTests)) {
    return;
  }
#endif

  if (app_path_.empty())
    return;

  extensions::ComponentLoader* loader = extension_service->component_loader();
  if (!loader->Exists(app_id_))
    app_id_ = loader->Add(manifest_id_, app_path_);

  extension_service->EnableExtension(app_id_);
}

void EasyUnlockAppManagerImpl::DisableAppIfLoaded() {
  ExtensionService* extension_service = extension_system_->extension_service();
  if (!extension_service)
    return;

  if (!extension_service->component_loader()->Exists(app_id_))
    return;

  extension_service->DisableExtension(app_id_,
                                      extensions::Extension::DISABLE_RELOAD);
}

void EasyUnlockAppManagerImpl::ReloadApp() {
  ExtensionService* extension_service = extension_system_->extension_service();
  if (!extension_service)
    return;

  if (!extension_service->component_loader()->Exists(app_id_))
    return;

  extension_service->ReloadExtension(app_id_);
}

bool EasyUnlockAppManagerImpl::SendUserUpdatedEvent(const std::string& user_id,
                                                    bool is_logged_in,
                                                    bool data_ready) {
  ExtensionService* extension_service = extension_system_->extension_service();
  if (!extension_service)
    return false;

  extensions::EventRouter* event_router =
      extensions::EventRouter::Get(extension_service->GetBrowserContext());
  if (!event_router)
    return false;

  extensions::events::HistogramValue histogram_value =
      extensions::events::EASY_UNLOCK_PRIVATE_ON_USER_INFO_UPDATED;
  std::string event_name =
      extensions::api::easy_unlock_private::OnUserInfoUpdated::kEventName;

  if (!event_router->ExtensionHasEventListener(app_id_, event_name))
    return false;

  extensions::api::easy_unlock_private::UserInfo info;
  info.user_id = user_id;
  info.logged_in = is_logged_in;
  info.data_ready = data_ready;

  std::unique_ptr<base::ListValue> args(new base::ListValue());
  args->Append(info.ToValue());

  std::unique_ptr<extensions::Event> event(
      new extensions::Event(histogram_value, event_name, std::move(args)));

  event_router->DispatchEventToExtension(app_id_, std::move(event));
  return true;
}

bool EasyUnlockAppManagerImpl::SendAuthAttemptEvent() {
  ExtensionService* extension_service = extension_system_->extension_service();
  if (!extension_service)
    return false;

  // TODO(tbarzic): Restrict this to EasyUnlock app.
  extensions::ScreenlockPrivateEventRouter* screenlock_router =
      extensions::ScreenlockPrivateEventRouter::GetFactoryInstance()->Get(
          extension_service->profile());
  return screenlock_router->OnAuthAttempted(
      proximity_auth::ScreenlockBridge::LockHandler::USER_CLICK, std::string());
}

}  // namespace

EasyUnlockAppManager::~EasyUnlockAppManager() {
}

// static
std::unique_ptr<EasyUnlockAppManager> EasyUnlockAppManager::Create(
    extensions::ExtensionSystem* extension_system,
    int manifest_id,
    const base::FilePath& app_path) {
  return std::unique_ptr<EasyUnlockAppManager>(
      new EasyUnlockAppManagerImpl(extension_system, manifest_id, app_path));
}
