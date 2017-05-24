// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/tray/system_tray_controller.h"

#include "ash/root_window_controller.h"
#include "ash/shell.h"
#include "ash/shell_port.h"
#include "ash/system/tray/system_tray.h"
#include "ash/system/tray/system_tray_notifier.h"
#include "ash/system/update/tray_update.h"
#include "ash/wm_window.h"

namespace ash {

SystemTrayController::SystemTrayController()
    : hour_clock_type_(base::GetHourClockType()) {}

SystemTrayController::~SystemTrayController() {}

void SystemTrayController::ShowSettings() {
  if (system_tray_client_)
    system_tray_client_->ShowSettings();
}

void SystemTrayController::ShowBluetoothSettings() {
  if (system_tray_client_)
    system_tray_client_->ShowBluetoothSettings();
}

void SystemTrayController::ShowBluetoothPairingDialog(
    const std::string& address,
    const base::string16& name_for_display,
    bool paired,
    bool connected) {
  if (system_tray_client_) {
    system_tray_client_->ShowBluetoothPairingDialog(address, name_for_display,
                                                    paired, connected);
  }
}

void SystemTrayController::ShowDateSettings() {
  if (system_tray_client_)
    system_tray_client_->ShowDateSettings();
}

void SystemTrayController::ShowSetTimeDialog() {
  if (system_tray_client_)
    system_tray_client_->ShowSetTimeDialog();
}

void SystemTrayController::ShowDisplaySettings() {
  if (system_tray_client_)
    system_tray_client_->ShowDisplaySettings();
}

void SystemTrayController::ShowPowerSettings() {
  if (system_tray_client_)
    system_tray_client_->ShowPowerSettings();
}

void SystemTrayController::ShowChromeSlow() {
  if (system_tray_client_)
    system_tray_client_->ShowChromeSlow();
}

void SystemTrayController::ShowIMESettings() {
  if (system_tray_client_)
    system_tray_client_->ShowIMESettings();
}

void SystemTrayController::ShowAboutChromeOS() {
  if (system_tray_client_)
    system_tray_client_->ShowAboutChromeOS();
}

void SystemTrayController::ShowHelp() {
  if (system_tray_client_)
    system_tray_client_->ShowHelp();
}

void SystemTrayController::ShowAccessibilityHelp() {
  if (system_tray_client_)
    system_tray_client_->ShowAccessibilityHelp();
}

void SystemTrayController::ShowAccessibilitySettings() {
  if (system_tray_client_)
    system_tray_client_->ShowAccessibilitySettings();
}

void SystemTrayController::ShowPaletteHelp() {
  if (system_tray_client_)
    system_tray_client_->ShowPaletteHelp();
}

void SystemTrayController::ShowPaletteSettings() {
  if (system_tray_client_)
    system_tray_client_->ShowPaletteSettings();
}

void SystemTrayController::ShowPublicAccountInfo() {
  if (system_tray_client_)
    system_tray_client_->ShowPublicAccountInfo();
}

void SystemTrayController::ShowEnterpriseInfo() {
  if (system_tray_client_)
    system_tray_client_->ShowEnterpriseInfo();
}

void SystemTrayController::ShowNetworkConfigure(const std::string& network_id) {
  if (system_tray_client_)
    system_tray_client_->ShowNetworkConfigure(network_id);
}

void SystemTrayController::ShowNetworkCreate(const std::string& type) {
  if (system_tray_client_)
    system_tray_client_->ShowNetworkCreate(type);
}

void SystemTrayController::ShowThirdPartyVpnCreate(
    const std::string& extension_id) {
  if (system_tray_client_)
    system_tray_client_->ShowThirdPartyVpnCreate(extension_id);
}

void SystemTrayController::ShowNetworkSettings(const std::string& network_id) {
  if (system_tray_client_)
    system_tray_client_->ShowNetworkSettings(network_id);
}

void SystemTrayController::ShowProxySettings() {
  if (system_tray_client_)
    system_tray_client_->ShowProxySettings();
}

void SystemTrayController::SignOut() {
  if (system_tray_client_)
    system_tray_client_->SignOut();
}

void SystemTrayController::RequestRestartForUpdate() {
  if (system_tray_client_)
    system_tray_client_->RequestRestartForUpdate();
}

void SystemTrayController::BindRequest(mojom::SystemTrayRequest request) {
  bindings_.AddBinding(this, std::move(request));
}

void SystemTrayController::SetClient(mojom::SystemTrayClientPtr client) {
  system_tray_client_ = std::move(client);
}

void SystemTrayController::SetPrimaryTrayEnabled(bool enabled) {
  ash::SystemTray* tray =
      Shell::GetPrimaryRootWindowController()->GetSystemTray();
  if (!tray)
    return;

  // We disable the UI to prevent user from interacting with UI elements,
  // particularly with the system tray menu. However, in case if the system tray
  // bubble is opened at this point, it remains opened and interactive even
  // after SystemTray::SetEnabled(false) call, which can be dangerous
  // (http://crbug.com/497080). Close the menu to fix it. Calling
  // SystemTray::SetEnabled(false) guarantees, that the menu will not be opened
  // until the UI is enabled again.
  if (!enabled && tray->HasSystemBubble())
    tray->CloseSystemBubble();

  tray->SetEnabled(enabled);
}

void SystemTrayController::SetPrimaryTrayVisible(bool visible) {
  ash::SystemTray* tray =
      Shell::GetPrimaryRootWindowController()->GetSystemTray();
  if (!tray)
    return;

  tray->SetVisible(visible);
  tray->GetWidget()->SetOpacity(visible ? 1.f : 0.f);
  if (visible) {
    tray->GetWidget()->Show();
  } else {
    tray->GetWidget()->Hide();
  }
}

void SystemTrayController::SetUse24HourClock(bool use_24_hour) {
  hour_clock_type_ = use_24_hour ? base::k24HourClock : base::k12HourClock;
  Shell::Get()->system_tray_notifier()->NotifyDateFormatChanged();
}

void SystemTrayController::SetEnterpriseDomain(
    const std::string& enterprise_domain,
    bool active_directory_managed) {
  enterprise_domain_ = enterprise_domain;
  active_directory_managed_ = active_directory_managed;
  Shell::Get()->system_tray_notifier()->NotifyEnterpriseDomainChanged();
}

void SystemTrayController::ShowUpdateIcon(mojom::UpdateSeverity severity,
                                          bool factory_reset_required,
                                          mojom::UpdateType update_type) {
  // Show the icon on all displays.
  for (RootWindowController* root : Shell::GetAllRootWindowControllers()) {
    ash::SystemTray* tray = root->GetSystemTray();
    // External monitors might not have a tray yet.
    if (!tray)
      continue;
    tray->tray_update()->ShowUpdateIcon(severity, factory_reset_required,
                                        update_type);
  }
}

void SystemTrayController::ShowUpdateOverCellularAvailableIcon() {
  // Show the icon on all displays.
  for (WmWindow* root : ShellPort::Get()->GetAllRootWindows()) {
    ash::SystemTray* tray = root->GetRootWindowController()->GetSystemTray();
    // External monitors might not have a tray yet.
    if (!tray)
      continue;
    tray->tray_update()->ShowUpdateOverCellularAvailableIcon();
  }
}

}  // namespace ash
