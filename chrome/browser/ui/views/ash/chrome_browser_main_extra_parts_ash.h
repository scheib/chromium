// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_ASH_CHROME_BROWSER_MAIN_EXTRA_PARTS_ASH_H_
#define CHROME_BROWSER_UI_VIEWS_ASH_CHROME_BROWSER_MAIN_EXTRA_PARTS_ASH_H_

#include <memory>

#include "base/macros.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"

namespace ash {
class ShelfModel;
}

class AshInit;
class CastConfigClientMediaRouter;
class ChromeLauncherController;
class ChromeNewWindowClient;
class ChromeShellContentState;
class LockScreenClient;
class ImmersiveContextMus;
class ImmersiveHandlerFactoryMus;
class MediaClient;
class SessionControllerClient;
class SystemTrayClient;
class VolumeController;
class VpnListForwarder;

// Browser initialization for Ash. Only runs on Chrome OS.
// TODO(jamescook): Fold this into ChromeBrowserMainPartsChromeOS.
class ChromeBrowserMainExtraPartsAsh : public ChromeBrowserMainExtraParts {
 public:
  ChromeBrowserMainExtraPartsAsh();
  ~ChromeBrowserMainExtraPartsAsh() override;

  // Overridden from ChromeBrowserMainExtraParts:
  void ServiceManagerConnectionStarted(
      content::ServiceManagerConnection* connection) override;
  void PreProfileInit() override;
  void PostProfileInit() override;
  void PostMainMessageLoopRun() override;

 private:
  std::unique_ptr<ChromeShellContentState> chrome_shell_content_state_;
  std::unique_ptr<CastConfigClientMediaRouter> cast_config_client_media_router_;
  std::unique_ptr<MediaClient> media_client_;
  std::unique_ptr<ImmersiveHandlerFactoryMus> immersive_handler_factory_;
  std::unique_ptr<ImmersiveContextMus> immersive_context_;
  std::unique_ptr<SessionControllerClient> session_controller_client_;
  std::unique_ptr<SystemTrayClient> system_tray_client_;
  std::unique_ptr<ChromeNewWindowClient> new_window_client_;
  std::unique_ptr<VolumeController> volume_controller_;
  std::unique_ptr<VpnListForwarder> vpn_list_forwarder_;
  std::unique_ptr<AshInit> ash_init_;
  std::unique_ptr<LockScreenClient> lock_screen_client_;

  // These are only used in Mash; corresponding instances are owned by Ash's
  // ShelfController and ChromeShellDelegate in classic Ash.
  std::unique_ptr<ash::ShelfModel> chrome_shelf_model_;
  std::unique_ptr<ChromeLauncherController> chrome_launcher_controller_;

  DISALLOW_COPY_AND_ASSIGN(ChromeBrowserMainExtraPartsAsh);
};

#endif  // CHROME_BROWSER_UI_VIEWS_ASH_CHROME_BROWSER_MAIN_EXTRA_PARTS_ASH_H_
