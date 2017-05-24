// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_TRAY_SYSTEM_TRAY_CONTROLLER_H_
#define ASH_SYSTEM_TRAY_SYSTEM_TRAY_CONTROLLER_H_

#include "ash/ash_export.h"
#include "ash/public/interfaces/system_tray.mojom.h"
#include "base/compiler_specific.h"
#include "base/i18n/time_formatting.h"
#include "base/macros.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace ash {

// Both implements mojom::SystemTray and wraps the mojom::SystemTrayClient
// interface. Implements both because it caches state pushed down from the
// browser process via SystemTray so it can be synchronously queried inside ash.
//
// Conceptually similar to historical ash-to-chrome interfaces like
// SystemTrayDelegate. Lives on the main thread.
//
// TODO: Consider renaming this to SystemTrayClient or renaming the current
// SystemTray to SystemTrayView and making this class SystemTray.
class ASH_EXPORT SystemTrayController
    : NON_EXPORTED_BASE(public mojom::SystemTray) {
 public:
  SystemTrayController();
  ~SystemTrayController() override;

  base::HourClockType hour_clock_type() const { return hour_clock_type_; }
  const std::string& enterprise_domain() const { return enterprise_domain_; }
  bool active_directory_managed() const { return active_directory_managed_; }

  // Wrappers around the mojom::SystemTrayClient interface.
  void ShowSettings();
  void ShowBluetoothSettings();
  void ShowBluetoothPairingDialog(const std::string& address,
                                  const base::string16& name_for_display,
                                  bool paired,
                                  bool connected);
  void ShowDateSettings();
  void ShowSetTimeDialog();
  void ShowDisplaySettings();
  void ShowPowerSettings();
  void ShowChromeSlow();
  void ShowIMESettings();
  void ShowAboutChromeOS();
  void ShowHelp();
  void ShowAccessibilityHelp();
  void ShowAccessibilitySettings();
  void ShowPaletteHelp();
  void ShowPaletteSettings();
  void ShowPublicAccountInfo();
  void ShowEnterpriseInfo();
  void ShowNetworkConfigure(const std::string& network_id);
  void ShowNetworkCreate(const std::string& type);
  void ShowThirdPartyVpnCreate(const std::string& extension_id);
  void ShowNetworkSettings(const std::string& network_id);
  void ShowProxySettings();
  void SignOut();
  void RequestRestartForUpdate();

  // Binds the mojom::SystemTray interface to this object.
  void BindRequest(mojom::SystemTrayRequest request);

  // mojom::SystemTray overrides. Public for testing.
  void SetClient(mojom::SystemTrayClientPtr client) override;
  void SetPrimaryTrayEnabled(bool enabled) override;
  void SetPrimaryTrayVisible(bool visible) override;
  void SetUse24HourClock(bool use_24_hour) override;
  void SetEnterpriseDomain(const std::string& enterprise_domain,
                           bool active_directory_managed) override;
  void ShowUpdateIcon(mojom::UpdateSeverity severity,
                      bool factory_reset_required,
                      mojom::UpdateType update_type) override;
  void ShowUpdateOverCellularAvailableIcon() override;

 private:
  // Client interface in chrome browser. May be null in tests.
  mojom::SystemTrayClientPtr system_tray_client_;

  // Bindings for the SystemTray interface.
  mojo::BindingSet<mojom::SystemTray> bindings_;

  // The type of clock hour display: 12 or 24 hour.
  base::HourClockType hour_clock_type_;

  // The domain name of the organization that manages the device. Empty if the
  // device is not enterprise enrolled or if it uses Active Directory.
  std::string enterprise_domain_;

  // Whether this is an Active Directory managed enterprise device.
  bool active_directory_managed_ = false;

  DISALLOW_COPY_AND_ASSIGN(SystemTrayController);
};

}  // namspace ash

#endif  // ASH_SYSTEM_TRAY_SYSTEM_TRAY_CONTROLLER_H_
