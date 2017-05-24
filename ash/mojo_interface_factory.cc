// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/mojo_interface_factory.h"

#include <utility>

#include "ash/accelerators/accelerator_controller.h"
#include "ash/cast_config_controller.h"
#include "ash/login/lock_screen_controller.h"
#include "ash/media_controller.h"
#include "ash/new_window_controller.h"
#include "ash/session/session_controller.h"
#include "ash/shelf/shelf_controller.h"
#include "ash/shell.h"
#include "ash/shell_delegate.h"
#include "ash/shutdown_controller.h"
#include "ash/system/locale/locale_notification_controller.h"
#include "ash/system/network/vpn_list.h"
#include "ash/system/tray/system_tray_controller.h"
#include "ash/tray_action/tray_action.h"
#include "ash/wallpaper/wallpaper_controller.h"
#include "ash/wm/maximize_mode/maximize_mode_controller.h"
#include "base/bind.h"
#include "base/single_thread_task_runner.h"
#include "services/service_manager/public/cpp/bind_source_info.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "ui/app_list/presenter/app_list.h"

namespace ash {

namespace {

void BindAcceleratorControllerRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::AcceleratorControllerRequest request) {
  Shell::Get()->accelerator_controller()->BindRequest(std::move(request));
}

void BindAppListRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    app_list::mojom::AppListRequest request) {
  Shell::Get()->app_list()->BindRequest(std::move(request));
}

void BindCastConfigOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::CastConfigRequest request) {
  Shell::Get()->cast_config()->BindRequest(std::move(request));
}

void BindLocaleNotificationControllerOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::LocaleNotificationControllerRequest request) {
  Shell::Get()->locale_notification_controller()->BindRequest(
      std::move(request));
}

void BindLockScreenRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::LockScreenRequest request) {
  Shell::Get()->lock_screen_controller()->BindRequest(std::move(request));
}

void BindMediaControllerRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::MediaControllerRequest request) {
  Shell::Get()->media_controller()->BindRequest(std::move(request));
}

void BindNewWindowControllerRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::NewWindowControllerRequest request) {
  Shell::Get()->new_window_controller()->BindRequest(std::move(request));
}

void BindSessionControllerRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::SessionControllerRequest request) {
  Shell::Get()->session_controller()->BindRequest(std::move(request));
}

void BindShelfRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::ShelfControllerRequest request) {
  Shell::Get()->shelf_controller()->BindRequest(std::move(request));
}

void BindShutdownControllerRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::ShutdownControllerRequest request) {
  Shell::Get()->shutdown_controller()->BindRequest(std::move(request));
}

void BindSystemTrayRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::SystemTrayRequest request) {
  Shell::Get()->system_tray_controller()->BindRequest(std::move(request));
}

void BindTouchViewRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::TouchViewManagerRequest request) {
  Shell::Get()->maximize_mode_controller()->BindRequest(std::move(request));
}

void BindTrayActionRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::TrayActionRequest request) {
  Shell::Get()->tray_action()->BindRequest(std::move(request));
}

void BindVpnListRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::VpnListRequest request) {
  Shell::Get()->vpn_list()->BindRequest(std::move(request));
}

void BindWallpaperRequestOnMainThread(
    const service_manager::BindSourceInfo& source_info,
    mojom::WallpaperControllerRequest request) {
  Shell::Get()->wallpaper_controller()->BindRequest(std::move(request));
}

}  // namespace

namespace mojo_interface_factory {

void RegisterInterfaces(
    service_manager::BinderRegistry* registry,
    scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner) {
  registry->AddInterface(
      base::Bind(&BindAcceleratorControllerRequestOnMainThread),
      main_thread_task_runner);
  registry->AddInterface(base::Bind(&BindAppListRequestOnMainThread),
                         main_thread_task_runner);
  registry->AddInterface(base::Bind(&BindCastConfigOnMainThread),
                         main_thread_task_runner);
  registry->AddInterface(
      base::Bind(&BindLocaleNotificationControllerOnMainThread),
      main_thread_task_runner);
  registry->AddInterface(base::Bind(&BindLockScreenRequestOnMainThread),
                         main_thread_task_runner);
  registry->AddInterface(base::Bind(&BindMediaControllerRequestOnMainThread),
                         main_thread_task_runner);
  registry->AddInterface(
      base::Bind(&BindNewWindowControllerRequestOnMainThread),
      main_thread_task_runner);
  registry->AddInterface(base::Bind(&BindSessionControllerRequestOnMainThread),
                         main_thread_task_runner);
  registry->AddInterface(base::Bind(&BindShelfRequestOnMainThread),
                         main_thread_task_runner);
  registry->AddInterface(base::Bind(&BindShutdownControllerRequestOnMainThread),
                         main_thread_task_runner);
  registry->AddInterface(base::Bind(&BindSystemTrayRequestOnMainThread),
                         main_thread_task_runner);
  registry->AddInterface(base::Bind(&BindTouchViewRequestOnMainThread),
                         main_thread_task_runner);
  registry->AddInterface(base::Bind(&BindTrayActionRequestOnMainThread),
                         main_thread_task_runner);
  registry->AddInterface(base::Bind(&BindVpnListRequestOnMainThread),
                         main_thread_task_runner);
  registry->AddInterface(base::Bind(&BindWallpaperRequestOnMainThread),
                         main_thread_task_runner);
}

}  // namespace mojo_interface_factory

}  // namespace ash
