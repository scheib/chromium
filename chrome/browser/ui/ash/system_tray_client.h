// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_SYSTEM_TRAY_CLIENT_H_
#define CHROME_BROWSER_UI_ASH_SYSTEM_TRAY_CLIENT_H_

#include "ash/public/interfaces/system_tray.mojom.h"
#include "base/macros.h"
#include "chrome/browser/chromeos/system/system_clock_observer.h"
#include "chrome/browser/upgrade_observer.h"
#include "components/policy/core/common/cloud/cloud_policy_store.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace ash {
enum class LoginStatus;
}

namespace views {
class Widget;
class WidgetDelegate;
}

// Handles method calls delegated back to chrome from ash. Also notifies ash of
// relevant state changes in chrome.
// TODO: Consider renaming this to SystemTrayClientChromeOS.
class SystemTrayClient : public ash::mojom::SystemTrayClient,
                         public chromeos::system::SystemClockObserver,
                         public policy::CloudPolicyStore::Observer,
                         public content::NotificationObserver,
                         public UpgradeObserver {
 public:
  SystemTrayClient();
  ~SystemTrayClient() override;

  static SystemTrayClient* Get();

  // Returns the login state based on the user type, lock screen status, etc.
  static ash::LoginStatus GetUserLoginStatus();

  // Returns the container id for the parent window for new dialogs. The parent
  // varies based on the current login and lock screen state.
  static int GetDialogParentContainerId();

  // Creates a modal dialog in the parent window for new dialogs on the primary
  // display. See GetDialogParentContainerId() and views::CreateDialogWidget().
  // The returned widget is owned by its native widget.
  static views::Widget* CreateUnownedDialogWidget(
      views::WidgetDelegate* widget_delegate);

  // Shows an update icon for an Adobe Flash update and forces a device reboot
  // when the update is applied.
  void SetFlashUpdateAvailable();

  // Wrappers around ash::mojom::SystemTray interface:
  void SetPrimaryTrayEnabled(bool enabled);
  void SetPrimaryTrayVisible(bool visible);

  // ash::mojom::SystemTrayClient:
  void ShowSettings() override;
  void ShowBluetoothSettings() override;
  void ShowBluetoothPairingDialog(const std::string& address,
                                  const base::string16& name_for_display,
                                  bool paired,
                                  bool connected) override;
  void ShowDateSettings() override;
  void ShowSetTimeDialog() override;
  void ShowDisplaySettings() override;
  void ShowPowerSettings() override;
  void ShowChromeSlow() override;
  void ShowIMESettings() override;
  void ShowAboutChromeOS() override;
  void ShowHelp() override;
  void ShowAccessibilityHelp() override;
  void ShowAccessibilitySettings() override;
  void ShowPaletteHelp() override;
  void ShowPaletteSettings() override;
  void ShowPublicAccountInfo() override;
  void ShowEnterpriseInfo() override;
  void ShowNetworkConfigure(const std::string& network_id) override;
  void ShowNetworkCreate(const std::string& type) override;
  void ShowThirdPartyVpnCreate(const std::string& extension_id) override;
  void ShowNetworkSettings(const std::string& network_id) override;
  void ShowProxySettings() override;
  void SignOut() override;
  void RequestRestartForUpdate() override;

 private:
  // Helper function shared by ShowNetworkSettings() and ShowNetworkConfigure().
  void ShowNetworkSettingsHelper(const std::string& network_id,
                                 bool show_configure);

  // Requests that ash show the update available icon.
  void HandleUpdateAvailable();

  // Requests that ash show the update over cellular available icon.
  void HandleUpdateOverCellularAvailable();

  // chromeos::system::SystemClockObserver:
  void OnSystemClockChanged(chromeos::system::SystemClock* clock) override;

  // UpgradeObserver:
  void OnUpdateOverCellularAvailable() override;

  // policy::CloudPolicyStore::Observer
  void OnStoreLoaded(policy::CloudPolicyStore* store) override;
  void OnStoreError(policy::CloudPolicyStore* store) override;

  void UpdateEnterpriseDomain();

  // content::NotificationObserver:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  // System tray mojo service in ash.
  ash::mojom::SystemTrayPtr system_tray_;

  // Binds this object to the client interface.
  mojo::Binding<ash::mojom::SystemTrayClient> binding_;

  // Whether an Adobe Flash component update is available.
  bool flash_update_available_ = false;

  // Avoid sending ash an empty enterprise domain at startup and suppress
  // duplicate IPCs during the session.
  std::string last_enterprise_domain_;
  bool last_active_directory_managed_ = false;

  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(SystemTrayClient);
};

#endif  // CHROME_BROWSER_UI_ASH_SYSTEM_TRAY_CLIENT_H_
