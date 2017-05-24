// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/chrome_browser_main_chromeos.h"

#include <stddef.h>

#include <string>
#include <utility>
#include <vector>

#include "ash/shell.h"
#include "ash/sticky_keys/sticky_keys_controller.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/lazy_instance.h"
#include "base/linux_util.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/sys_info.h"
#include "base/task_scheduler/post_task.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part_chromeos.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/accessibility/accessibility_manager.h"
#include "chrome/browser/chromeos/accessibility/magnification_manager.h"
#include "chrome/browser/chromeos/accessibility/spoken_feedback_event_rewriter.h"
#include "chrome/browser/chromeos/app_mode/arc/arc_kiosk_app_manager.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_launch_error.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_manager.h"
#include "chrome/browser/chromeos/app_mode/kiosk_mode_idle_app_name_notification.h"
#include "chrome/browser/chromeos/arc/arc_service_launcher.h"
#include "chrome/browser/chromeos/ash_config.h"
#include "chrome/browser/chromeos/boot_times_recorder.h"
#include "chrome/browser/chromeos/dbus/chrome_console_service_provider_delegate.h"
#include "chrome/browser/chromeos/dbus/chrome_display_power_service_provider_delegate.h"
#include "chrome/browser/chromeos/dbus/chrome_proxy_resolution_service_provider_delegate.h"
#include "chrome/browser/chromeos/dbus/kiosk_info_service_provider.h"
#include "chrome/browser/chromeos/dbus/mus_console_service_provider_delegate.h"
#include "chrome/browser/chromeos/dbus/screen_lock_service_provider.h"
#include "chrome/browser/chromeos/display/quirks_manager_delegate_impl.h"
#include "chrome/browser/chromeos/events/event_rewriter_controller.h"
#include "chrome/browser/chromeos/events/event_rewriter_delegate_impl.h"
#include "chrome/browser/chromeos/events/keyboard_driven_event_rewriter.h"
#include "chrome/browser/chromeos/extensions/default_app_order.h"
#include "chrome/browser/chromeos/extensions/extension_volume_observer.h"
#include "chrome/browser/chromeos/external_metrics.h"
#include "chrome/browser/chromeos/input_method/input_method_configuration.h"
#include "chrome/browser/chromeos/input_method/input_method_util.h"
#include "chrome/browser/chromeos/language_preferences.h"
#include "chrome/browser/chromeos/lock_screen_apps/state_controller.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/login/lock/screen_locker.h"
#include "chrome/browser/chromeos/login/login_wizard.h"
#include "chrome/browser/chromeos/login/session/chrome_session_manager.h"
#include "chrome/browser/chromeos/login/session/user_session_manager.h"
#include "chrome/browser/chromeos/login/startup_utils.h"
#include "chrome/browser/chromeos/login/users/chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/wallpaper/wallpaper_manager.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/chromeos/net/network_connect_delegate_chromeos.h"
#include "chrome/browser/chromeos/net/network_portal_detector_impl.h"
#include "chrome/browser/chromeos/net/network_pref_state_observer.h"
#include "chrome/browser/chromeos/net/network_throttling_observer.h"
#include "chrome/browser/chromeos/net/wake_on_wifi_manager.h"
#include "chrome/browser/chromeos/note_taking_helper.h"
#include "chrome/browser/chromeos/options/cert_library.h"
#include "chrome/browser/chromeos/ownership/owner_settings_service_chromeos_factory.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/policy/device_local_account.h"
#include "chrome/browser/chromeos/power/freezer_cgroup_process_manager.h"
#include "chrome/browser/chromeos/power/idle_action_warning_observer.h"
#include "chrome/browser/chromeos/power/peripheral_battery_observer.h"
#include "chrome/browser/chromeos/power/power_data_collector.h"
#include "chrome/browser/chromeos/power/power_prefs.h"
#include "chrome/browser/chromeos/power/renderer_freezer.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/chromeos/resource_reporter/resource_reporter.h"
#include "chrome/browser/chromeos/settings/device_oauth2_token_service_factory.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "chrome/browser/chromeos/settings/shutdown_policy_forwarder.h"
#include "chrome/browser/chromeos/status/data_promo_notification.h"
#include "chrome/browser/chromeos/system/input_device_settings.h"
#include "chrome/browser/chromeos/ui/low_disk_notification.h"
#include "chrome/browser/chromeos/upgrade_detector_chromeos.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/net/chrome_network_delegate.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/task_manager/task_manager_interface.h"
#include "chrome/browser/ui/ash/ash_util.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/logging_chrome.h"
#include "chrome/common/pref_names.h"
#include "chromeos/audio/audio_devices_pref_handler_impl.h"
#include "chromeos/audio/cras_audio_handler.h"
#include "chromeos/cert_loader.h"
#include "chromeos/chromeos_paths.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/cryptohome/async_method_caller.h"
#include "chromeos/cryptohome/cryptohome_parameters.h"
#include "chromeos/cryptohome/homedir_methods.h"
#include "chromeos/cryptohome/system_salt_getter.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/power_policy_controller.h"
#include "chromeos/dbus/services/console_service_provider.h"
#include "chromeos/dbus/services/cros_dbus_service.h"
#include "chromeos/dbus/services/display_power_service_provider.h"
#include "chromeos/dbus/services/liveness_service_provider.h"
#include "chromeos/dbus/services/proxy_resolution_service_provider.h"
#include "chromeos/dbus/session_manager_client.h"
#include "chromeos/disks/disk_mount_manager.h"
#include "chromeos/login/login_state.h"
#include "chromeos/login_event_recorder.h"
#include "chromeos/network/network_change_notifier_chromeos.h"
#include "chromeos/network/network_change_notifier_factory_chromeos.h"
#include "chromeos/network/network_handler.h"
#include "chromeos/network/portal_detector/network_portal_detector_stub.h"
#include "chromeos/system/statistics_provider.h"
#include "chromeos/tpm/tpm_token_loader.h"
#include "components/browser_sync/browser_sync_switches.h"
#include "components/device_event_log/device_event_log.h"
#include "components/metrics/metrics_service.h"
#include "components/ownership/owner_key_util.h"
#include "components/prefs/pref_service.h"
#include "components/session_manager/core/session_manager.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#include "components/user_manager/user_names.h"
#include "components/wallpaper/wallpaper_manager_base.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/media_capture_devices.h"
#include "content/public/browser/notification_service.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/main_function_params.h"
#include "crypto/nss_util_internal.h"
#include "crypto/scoped_nss_types.h"
#include "dbus/object_path.h"
#include "device/bluetooth/bluetooth_adapter_factory.h"
#include "device/bluetooth/dbus/bluez_dbus_manager.h"
#include "media/audio/sounds/sounds_manager.h"
#include "net/base/network_change_notifier.h"
#include "net/cert/nss_cert_database.h"
#include "net/cert/nss_cert_database_chromeos.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context_getter.h"
#include "printing/backend/print_backend.h"
#include "rlz/features/features.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "ui/base/ime/chromeos/ime_keyboard.h"
#include "ui/base/ime/chromeos/input_method_manager.h"
#include "ui/base/touch/touch_device.h"
#include "ui/chromeos/events/event_rewriter_chromeos.h"
#include "ui/chromeos/events/pref_names.h"
#include "ui/events/event_utils.h"

#if BUILDFLAG(ENABLE_RLZ)
#include "components/rlz/rlz_tracker.h"
#endif

// Exclude X11 dependents for ozone
#if defined(USE_X11)
#include "chrome/browser/chromeos/device_uma.h"
#include "chrome/browser/chromeos/events/system_key_event_listener.h"
#include "chrome/browser/chromeos/events/xinput_hierarchy_changed_event_listener.h"
#endif

namespace chromeos {

namespace {

void ChromeOSVersionCallback(const std::string& version) {
  base::SetLinuxDistro(std::string("CrOS ") + version);
}

bool ShouldAutoLaunchKioskApp(const base::CommandLine& command_line) {
  KioskAppManager* app_manager = KioskAppManager::Get();
  return command_line.HasSwitch(switches::kLoginManager) &&
      !command_line.HasSwitch(switches::kForceLoginManagerInTests) &&
      app_manager->IsAutoLaunchEnabled() &&
      KioskAppLaunchError::Get() == KioskAppLaunchError::NONE;
}

// Creates an instance of the NetworkPortalDetector implementation or a stub.
void InitializeNetworkPortalDetector() {
  if (network_portal_detector::SetForTesting())
    return;
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          ::switches::kTestType)) {
    network_portal_detector::SetNetworkPortalDetector(
        new NetworkPortalDetectorStub());
  } else {
    network_portal_detector::SetNetworkPortalDetector(
        new NetworkPortalDetectorImpl(
            g_browser_process->system_request_context(), true));
  }
}

// Called on UI Thread when the system slot has been retrieved.
void GotSystemSlotOnUIThread(
    base::Callback<void(crypto::ScopedPK11Slot)> callback_ui_thread,
    crypto::ScopedPK11Slot system_slot) {
  callback_ui_thread.Run(std::move(system_slot));
}

// Called on IO Thread when the system slot has been retrieved.
void GotSystemSlotOnIOThread(
    base::Callback<void(crypto::ScopedPK11Slot)> callback_ui_thread,
    crypto::ScopedPK11Slot system_slot) {
  content::BrowserThread::PostTask(
      content::BrowserThread::UI, FROM_HERE,
      base::BindOnce(&GotSystemSlotOnUIThread, callback_ui_thread,
                     std::move(system_slot)));
}

// Called on IO Thread, initiates retrieval of system slot. |callback_ui_thread|
// will be executed on the UI thread when the system slot has been retrieved.
void GetSystemSlotOnIOThread(
    base::Callback<void(crypto::ScopedPK11Slot)> callback_ui_thread) {
  auto callback =
      base::BindRepeating(&GotSystemSlotOnIOThread, callback_ui_thread);
  crypto::ScopedPK11Slot system_nss_slot =
      crypto::GetSystemNSSKeySlot(callback);
  if (system_nss_slot) {
    callback.Run(std::move(system_nss_slot));
  }
}

}  // namespace

namespace internal {

// Wrapper class for initializing dbus related services and shutting them
// down. This gets instantiated in a scoped_ptr so that shutdown methods in the
// destructor will get called if and only if this has been instantiated.
class DBusServices {
 public:
  explicit DBusServices(const content::MainFunctionParams& parameters) {
    // Under mash, some D-Bus clients are owned by other processes.
    DBusThreadManager::ProcessMask process_mask;
    switch (GetAshConfig()) {
      case ash::Config::CLASSIC:
        process_mask = DBusThreadManager::PROCESS_ALL;
        break;
      case ash::Config::MUS:
        // TODO(jamescook|derat): We need another category for mushrome.
        process_mask = DBusThreadManager::PROCESS_ALL;
        break;
      case ash::Config::MASH:
        process_mask = DBusThreadManager::PROCESS_BROWSER;
        break;
    }

    // Initialize DBusThreadManager for the browser. This must be done after
    // the main message loop is started, as it uses the message loop.
    DBusThreadManager::Initialize(process_mask);

    bluez::BluezDBusManager::Initialize(
        DBusThreadManager::Get()->GetSystemBus(),
        chromeos::DBusThreadManager::Get()->IsUsingFakes());

    PowerPolicyController::Initialize(
        DBusThreadManager::Get()->GetPowerManagerClient());

    CrosDBusService::ServiceProviderList service_providers;
    // TODO(derat): Remove this provider once all callers are using
    // |proxy_resolution_service_| instead: http://crbug.com/703217
    service_providers.push_back(
        base::MakeUnique<ProxyResolutionServiceProvider>(
            kLibCrosServiceInterface, kResolveNetworkProxy,
            base::MakeUnique<ChromeProxyResolutionServiceProviderDelegate>()));
    if (GetAshConfig() == ash::Config::CLASSIC) {
      // TODO(crbug.com/629707): revisit this with mustash dbus work.
      service_providers.push_back(base::MakeUnique<DisplayPowerServiceProvider>(
          base::MakeUnique<ChromeDisplayPowerServiceProviderDelegate>()));
    }
    service_providers.push_back(base::MakeUnique<LivenessServiceProvider>());
    service_providers.push_back(base::MakeUnique<ScreenLockServiceProvider>());
    // TODO(sky): once mash supports simplified display mode we should always
    // use ChromeConsoleServiceProviderDelegate.
    if (GetAshConfig() != ash::Config::MASH) {
      service_providers.push_back(base::MakeUnique<ConsoleServiceProvider>(
          base::MakeUnique<ChromeConsoleServiceProviderDelegate>()));
    } else {
      service_providers.push_back(base::MakeUnique<ConsoleServiceProvider>(
          base::MakeUnique<MusConsoleServiceProviderDelegate>()));
    }
    // TODO(teravest): Remove this provider once all callers are using
    // |kiosk_info_service_| instead: http://crbug.com/703229
    service_providers.push_back(base::MakeUnique<KioskInfoService>(
        kLibCrosServiceInterface, kGetKioskAppRequiredPlatforVersion));
    cros_dbus_service_ = CrosDBusService::Create(
        kLibCrosServiceName, dbus::ObjectPath(kLibCrosServicePath),
        std::move(service_providers));

    proxy_resolution_service_ = CrosDBusService::Create(
        kNetworkProxyServiceName, dbus::ObjectPath(kNetworkProxyServicePath),
        CrosDBusService::CreateServiceProviderList(
            base::MakeUnique<ProxyResolutionServiceProvider>(
                kNetworkProxyServiceInterface,
                kNetworkProxyServiceResolveProxyMethod,
                base::MakeUnique<
                    ChromeProxyResolutionServiceProviderDelegate>())));

    kiosk_info_service_ = CrosDBusService::Create(
        kKioskAppServiceName, dbus::ObjectPath(kKioskAppServicePath),
        CrosDBusService::CreateServiceProviderList(
            base::MakeUnique<KioskInfoService>(
                kKioskAppServiceInterface,
                kKioskAppServiceGetRequiredPlatformVersionMethod)));

    // Initialize PowerDataCollector after DBusThreadManager is initialized.
    PowerDataCollector::Initialize();

    LoginState::Initialize();
    SystemSaltGetter::Initialize();
    TPMTokenLoader::Initialize();
    CertLoader::Initialize();

    disks::DiskMountManager::Initialize();
    cryptohome::AsyncMethodCaller::Initialize();
    cryptohome::HomedirMethods::Initialize();

    NetworkHandler::Initialize();
    CertLibrary::Initialize();

    // Initialize the network change notifier for Chrome OS. The network
    // change notifier starts to monitor changes from the power manager and
    // the network manager.
    NetworkChangeNotifierFactoryChromeos::GetInstance()->Initialize();

    // Initialize the NetworkConnect handler.
    network_connect_delegate_.reset(new NetworkConnectDelegateChromeOS);
    NetworkConnect::Initialize(network_connect_delegate_.get());

    // Likewise, initialize the upgrade detector for Chrome OS. The upgrade
    // detector starts to monitor changes from the update engine.
    UpgradeDetectorChromeos::GetInstance()->Init();

    // Initialize the device settings service so that we'll take actions per
    // signals sent from the session manager. This needs to happen before
    // g_browser_process initializes BrowserPolicyConnector.
    DeviceSettingsService::Initialize();
    DeviceSettingsService::Get()->SetSessionManager(
        DBusThreadManager::Get()->GetSessionManagerClient(),
        OwnerSettingsServiceChromeOSFactory::GetInstance()->GetOwnerKeyUtil());
  }

  ~DBusServices() {
    NetworkConnect::Shutdown();
    network_connect_delegate_.reset();
    CertLibrary::Shutdown();
    NetworkHandler::Shutdown();
    cryptohome::AsyncMethodCaller::Shutdown();
    disks::DiskMountManager::Shutdown();
    SystemSaltGetter::Shutdown();
    LoginState::Shutdown();
    CertLoader::Shutdown();
    TPMTokenLoader::Shutdown();
    cros_dbus_service_.reset();
    proxy_resolution_service_.reset();
    kiosk_info_service_.reset();
    PowerDataCollector::Shutdown();
    PowerPolicyController::Shutdown();
    device::BluetoothAdapterFactory::Shutdown();
    bluez::BluezDBusManager::Shutdown();

    // NOTE: This must only be called if Initialize() was called.
    DBusThreadManager::Shutdown();
  }

 private:
  // Hosts providers for the "org.chromium.LibCrosService" D-Bus service owned
  // by Chrome. The name of this service was chosen for historical reasons that
  // are irrelevant now.
  // TODO(derat): Move these providers into more-specific services that are
  // split between different processes: http://crbug.com/692246
  std::unique_ptr<CrosDBusService> cros_dbus_service_;

  std::unique_ptr<CrosDBusService> proxy_resolution_service_;
  std::unique_ptr<CrosDBusService> kiosk_info_service_;

  std::unique_ptr<NetworkConnectDelegateChromeOS> network_connect_delegate_;

  DISALLOW_COPY_AND_ASSIGN(DBusServices);
};

// Initializes a global NSSCertDatabase for the system token and starts
// CertLoader with that database. Note that this is triggered from
// PreMainMessageLoopRun, which is executed after PostMainMessageLoopStart,
// where CertLoader is initialized. We can thus assume that CertLoader is
// initialized.
class SystemTokenCertDBInitializer {
 public:
  SystemTokenCertDBInitializer() : weak_ptr_factory_(this) {}
  ~SystemTokenCertDBInitializer() {}

  // Entry point, called on UI thread.
  void Initialize() {
    base::Callback<void(crypto::ScopedPK11Slot)> callback =
        base::BindRepeating(&SystemTokenCertDBInitializer::InitializeDatabase,
                            weak_ptr_factory_.GetWeakPtr());
    content::BrowserThread::PostTask(
        content::BrowserThread::IO, FROM_HERE,
        base::BindOnce(&GetSystemSlotOnIOThread, callback));
  }

 private:
  // Initializes the global system token NSSCertDatabase with |system_slot|.
  // Also starts CertLoader with the system token database.
  void InitializeDatabase(crypto::ScopedPK11Slot system_slot) {
    // Currently, NSSCertDatabase requires a public slot to be set, so we use
    // the system slot there. We also want GetSystemSlot() to return the system
    // slot. As ScopedPK11Slot is actually a unique_ptr which will be moved into
    // the NSSCertDatabase, we need to create a copy, referencing the same slot
    // (using PK11_ReferenceSlot).
    crypto::ScopedPK11Slot system_slot_copy =
        crypto::ScopedPK11Slot(PK11_ReferenceSlot(system_slot.get()));
    auto database = base::MakeUnique<net::NSSCertDatabaseChromeOS>(
        std::move(system_slot) /* public_slot */,
        crypto::ScopedPK11Slot() /* private_slot */);
    database->SetSystemSlot(std::move(system_slot_copy));
    system_token_cert_database_ = std::move(database);

    CertLoader::Get()->SetSystemNSSDB(system_token_cert_database_.get());
  }

  // Global NSSCertDatabase which sees the system token.
  std::unique_ptr<net::NSSCertDatabase> system_token_cert_database_;

  base::WeakPtrFactory<SystemTokenCertDBInitializer> weak_ptr_factory_;
};

}  //  namespace internal

// ChromeBrowserMainPartsChromeos ----------------------------------------------

ChromeBrowserMainPartsChromeos::ChromeBrowserMainPartsChromeos(
    const content::MainFunctionParams& parameters)
    : ChromeBrowserMainPartsLinux(parameters) {
}

ChromeBrowserMainPartsChromeos::~ChromeBrowserMainPartsChromeos() {
  // To be precise, logout (browser shutdown) is not yet done, but the
  // remaining work is negligible, hence we say LogoutDone here.
  BootTimesRecorder::Get()->AddLogoutTimeMarker("LogoutDone", false);
  BootTimesRecorder::Get()->WriteLogoutTimes();
}

// content::BrowserMainParts and ChromeBrowserMainExtraParts overrides ---------

void ChromeBrowserMainPartsChromeos::PreEarlyInitialization() {
  base::CommandLine* singleton_command_line =
      base::CommandLine::ForCurrentProcess();

  if (parsed_command_line().HasSwitch(switches::kGuestSession)) {
    // Disable sync and extensions if we're in "browse without sign-in" mode.
    singleton_command_line->AppendSwitch(::switches::kDisableSync);
    singleton_command_line->AppendSwitch(::switches::kDisableExtensions);
    browser_defaults::bookmarks_enabled = false;
  }

  // If we're not running on real Chrome OS hardware (or under VM), and are not
  // showing the login manager or attempting a command line login, login with a
  // stub user.
  if (!base::SysInfo::IsRunningOnChromeOS() &&
      !parsed_command_line().HasSwitch(switches::kLoginManager) &&
      !parsed_command_line().HasSwitch(switches::kLoginUser) &&
      !parsed_command_line().HasSwitch(switches::kGuestSession)) {
    singleton_command_line->AppendSwitchASCII(
        switches::kLoginUser,
        cryptohome::Identification(user_manager::StubAccountId()).id());
    if (!parsed_command_line().HasSwitch(switches::kLoginProfile)) {
      singleton_command_line->AppendSwitchASCII(switches::kLoginProfile,
                                                chrome::kTestUserProfileDir);
    }
    LOG(WARNING) << "Running as stub user with profile dir: "
                 << singleton_command_line->GetSwitchValuePath(
                     switches::kLoginProfile).value();
  }

#if defined(GOOGLE_CHROME_BUILD)
  const char kChromeOSReleaseTrack[] = "CHROMEOS_RELEASE_TRACK";
  std::string channel;
  if (base::SysInfo::GetLsbReleaseValue(kChromeOSReleaseTrack, &channel))
    chrome::SetChannel(channel);
#endif

  // Start monitoring OOM kills.
  memory_kills_monitor_ = base::MakeUnique<memory::MemoryKillsMonitor::Handle>(
      memory::MemoryKillsMonitor::StartMonitoring());

  ChromeBrowserMainPartsLinux::PreEarlyInitialization();
}

void ChromeBrowserMainPartsChromeos::PreMainMessageLoopStart() {
  // Replace the default NetworkChangeNotifierFactory with ChromeOS specific
  // implementation. This must be done before BrowserMainLoop calls
  // net::NetworkChangeNotifier::Create() in MainMessageLoopStart().
  net::NetworkChangeNotifier::SetFactory(
      new NetworkChangeNotifierFactoryChromeos());
  ChromeBrowserMainPartsLinux::PreMainMessageLoopStart();
}

void ChromeBrowserMainPartsChromeos::PostMainMessageLoopStart() {
  // device_event_log must be initialized after the message loop.
  device_event_log::Initialize(0 /* default max entries */);

  base::FilePath user_data_dir;
  if (!base::SysInfo::IsRunningOnChromeOS() &&
      PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    // Override some paths with stub locations so that cloud policy and
    // enterprise enrollment work on desktop builds, for ease of
    // development.
    chromeos::RegisterStubPathOverrides(user_data_dir);
  }

  dbus_services_.reset(new internal::DBusServices(parameters()));

  ChromeBrowserMainPartsLinux::PostMainMessageLoopStart();
}

// Threads are initialized between MainMessageLoopStart and MainMessageLoopRun.
// about_flags settings are applied in ChromeBrowserMainParts::PreCreateThreads.
void ChromeBrowserMainPartsChromeos::PreMainMessageLoopRun() {
  // Set the crypto thread after the IO thread has been created/started.
  TPMTokenLoader::Get()->SetCryptoTaskRunner(
      content::BrowserThread::GetTaskRunnerForThread(
          content::BrowserThread::IO));

  // Initialize NSS database for system token.
  TPMTokenLoader::Get()->EnsureStarted();
  system_token_certdb_initializer_ =
      base::MakeUnique<internal::SystemTokenCertDBInitializer>();
  system_token_certdb_initializer_->Initialize();

  CrasAudioHandler::Initialize(
      new AudioDevicesPrefHandlerImpl(g_browser_process->local_state()));

  content::MediaCaptureDevices::GetInstance()->AddVideoCaptureObserver(
      CrasAudioHandler::Get());

  quirks::QuirksManager::Initialize(
      std::unique_ptr<quirks::QuirksManager::Delegate>(
          new quirks::QuirksManagerDelegateImpl()),
      content::BrowserThread::GetBlockingPool(),
      g_browser_process->local_state(),
      g_browser_process->system_request_context());

  // Start loading machine statistics here. StatisticsProvider::Shutdown()
  // will ensure that loading is aborted on early exit.
  bool load_oem_statistics = !StartupUtils::IsOobeCompleted();
  system::StatisticsProvider::GetInstance()->StartLoadingMachineStatistics(
      content::BrowserThread::GetTaskRunnerForThread(
          content::BrowserThread::FILE),
      load_oem_statistics);

  base::FilePath downloads_directory;
  CHECK(PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS, &downloads_directory));

  DeviceOAuth2TokenServiceFactory::Initialize();

  wake_on_wifi_manager_.reset(new WakeOnWifiManager());
  network_throttling_observer_.reset(
      new NetworkThrottlingObserver(g_browser_process->local_state()));

  arc_service_launcher_.reset(new arc::ArcServiceLauncher());
  arc_service_launcher_->Initialize();

  chromeos::ResourceReporter::GetInstance()->StartMonitoring(
      task_manager::TaskManagerInterface::GetTaskManager());

  ChromeBrowserMainPartsLinux::PreMainMessageLoopRun();
}

void ChromeBrowserMainPartsChromeos::PreProfileInit() {
  // -- This used to be in ChromeBrowserMainParts::PreMainMessageLoopRun()
  // -- immediately before Profile creation().

  // Now that the file thread exists we can record our stats.
  BootTimesRecorder::Get()->RecordChromeMainStats();
  LoginEventRecorder::Get()->SetDelegate(BootTimesRecorder::Get());

  // Trigger prefetching of ownership status.
  DeviceSettingsService::Get()->Load();

  // -- This used to be in ChromeBrowserMainParts::PreMainMessageLoopRun()
  // -- just before CreateProfile().

  g_browser_process->platform_part()->InitializeChromeUserManager();
  g_browser_process->platform_part()->InitializeSessionManager();

  ScreenLocker::InitClass();

  // This forces the ProfileManager to be created and register for the
  // notification it needs to track the logged in user.
  g_browser_process->profile_manager();

  // AccessibilityManager and SystemKeyEventListener use InputMethodManager.
  input_method::Initialize();

  // ProfileHelper has to be initialized after UserManager instance is created.
  ProfileHelper::Get()->Initialize();

  // If kLoginUser is passed this indicates that user has already
  // logged in and we should behave accordingly.
  bool immediate_login =
      parsed_command_line().HasSwitch(switches::kLoginUser);
  if (immediate_login) {
    // Redirects Chrome logging to the user data dir.
    logging::RedirectChromeLogging(parsed_command_line());

    // Load the default app order synchronously for restarting case.
    app_order_loader_.reset(
        new default_app_order::ExternalLoader(false /* async */));
  }

  if (!app_order_loader_) {
    app_order_loader_.reset(
        new default_app_order::ExternalLoader(true /* async */));
  }

  media::SoundsManager::Create();

  // |arc_service_launcher_| must be initialized before NoteTakingHelper.
  NoteTakingHelper::Initialize();

  AccessibilityManager::Initialize();

  if (!ash_util::IsRunningInMash()) {
    // Initialize magnification manager before ash tray is created. And this
    // must be placed after UserManager::SessionStarted();
    // TODO(sad): These components expects the ash::Shell instance to be
    // created. However, when running as a mus-client, an ash::Shell instance is
    // not created. These accessibility services should instead be exposed as
    // separate services. crbug.com/557401
    MagnificationManager::Initialize();
  }

  wallpaper::WallpaperManagerBase::SetPathIds(
      chrome::DIR_USER_DATA,
      chrome::DIR_CHROMEOS_WALLPAPERS,
      chrome::DIR_CHROMEOS_CUSTOM_WALLPAPERS);

  // Add observers for WallpaperManager. This depends on PowerManagerClient,
  // TimezoneSettings and CrosSettings.
  WallpaperManager::Initialize();
  WallpaperManager::Get()->AddObservers();

  base::PostTaskWithTraitsAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BACKGROUND},
      base::Bind(&version_loader::GetVersion, version_loader::VERSION_FULL),
      base::Bind(&ChromeOSVersionCallback));

  // Make sure that wallpaper boot transition and other delays in OOBE
  // are disabled for tests and kiosk app launch by default.
  // Individual tests may enable them if they want.
  if (parsed_command_line().HasSwitch(::switches::kTestType) ||
      ShouldAutoLaunchKioskApp(parsed_command_line())) {
    WizardController::SetZeroDelays();
  }

  // Enable/disable native CUPS integration
  printing::PrintBackend::SetNativeCupsEnabled(
      !parsed_command_line().HasSwitch(::switches::kDisableNativeCups));

  power_prefs_.reset(new PowerPrefs(PowerPolicyController::Get()));

  arc_kiosk_app_manager_.reset(new ArcKioskAppManager());

  // In Aura builds this will initialize ash::Shell.
  ChromeBrowserMainPartsLinux::PreProfileInit();

  if (lock_screen_apps::StateController::IsEnabled()) {
    lock_screen_apps_state_controller_ =
        base::MakeUnique<lock_screen_apps::StateController>();
    lock_screen_apps_state_controller_->Initialize();
  }

  if (immediate_login) {
    const std::string cryptohome_id =
        parsed_command_line().GetSwitchValueASCII(switches::kLoginUser);
    const AccountId account_id(
        cryptohome::Identification::FromString(cryptohome_id).GetAccountId());

    user_manager::UserManager* user_manager = user_manager::UserManager::Get();

    if (policy::IsDeviceLocalAccountUser(account_id.GetUserEmail(), nullptr) &&
        !user_manager->IsKnownUser(account_id)) {
      // When a device-local account is removed, its policy is deleted from disk
      // immediately. If a session using this account happens to be in progress,
      // the session is allowed to continue with policy served from an in-memory
      // cache. If Chrome crashes later in the session, the policy becomes
      // completely unavailable. Exit the session in that case, rather than
      // allowing it to continue without policy.
      chrome::AttemptUserExit();
      return;
    }

    // In case of multi-profiles --login-profile will contain user_id_hash.
    std::string user_id_hash =
        parsed_command_line().GetSwitchValueASCII(switches::kLoginProfile);
    session_manager::SessionManager::Get()->CreateSessionForRestart(
        account_id, user_id_hash);
    VLOG(1) << "Relaunching browser for user: " << account_id.Serialize()
            << " with hash: " << user_id_hash;
  }
}

class GuestLanguageSetCallbackData {
 public:
  explicit GuestLanguageSetCallbackData(Profile* profile) : profile(profile) {
  }

  // Must match SwitchLanguageCallback type.
  static void Callback(
      const std::unique_ptr<GuestLanguageSetCallbackData>& self,
      const locale_util::LanguageSwitchResult& result);

  Profile* profile;
};

// static
void GuestLanguageSetCallbackData::Callback(
    const std::unique_ptr<GuestLanguageSetCallbackData>& self,
    const locale_util::LanguageSwitchResult& result) {
  input_method::InputMethodManager* manager =
      input_method::InputMethodManager::Get();
  scoped_refptr<input_method::InputMethodManager::State> ime_state =
      manager->GetActiveIMEState();
  // For guest mode, we should always use the first login input methods.
  // This is to keep consistency with UserSessionManager::SetFirstLoginPrefs().
  // See crbug.com/530808.
  std::vector<std::string> input_methods;
  manager->GetInputMethodUtil()->GetFirstLoginInputMethodIds(
      result.loaded_locale, ime_state->GetCurrentInputMethod(), &input_methods);
  ime_state->ReplaceEnabledInputMethods(input_methods);

  // Active layout must be hardware "login layout".
  // The previous one must be "locale default layout".
  // First, enable all hardware input methods.
  input_methods = manager->GetInputMethodUtil()->GetHardwareInputMethodIds();
  for (size_t i = 0; i < input_methods.size(); ++i)
    ime_state->EnableInputMethod(input_methods[i]);

  // Second, enable locale based input methods.
  const std::string locale_default_input_method =
      manager->GetInputMethodUtil()->GetLanguageDefaultInputMethodId(
          result.loaded_locale);
  if (!locale_default_input_method.empty()) {
    PrefService* user_prefs = self->profile->GetPrefs();
    user_prefs->SetString(prefs::kLanguagePreviousInputMethod,
                          locale_default_input_method);
    ime_state->EnableInputMethod(locale_default_input_method);
  }

  // Finally, activate the first login input method.
  const std::vector<std::string>& login_input_methods =
      manager->GetInputMethodUtil()->GetHardwareLoginInputMethodIds();
  ime_state->ChangeInputMethod(login_input_methods[0],
                               false /* show_message */);
}

void SetGuestLocale(Profile* const profile) {
  std::unique_ptr<GuestLanguageSetCallbackData> data(
      new GuestLanguageSetCallbackData(profile));
  locale_util::SwitchLanguageCallback callback(base::Bind(
      &GuestLanguageSetCallbackData::Callback, base::Passed(std::move(data))));
  const user_manager::User* const user =
      ProfileHelper::Get()->GetUserByProfile(profile);
  UserSessionManager::GetInstance()->RespectLocalePreference(
      profile, user, callback);
}

void ChromeBrowserMainPartsChromeos::PostProfileInit() {
  // -- This used to be in ChromeBrowserMainParts::PreMainMessageLoopRun()
  // -- just after CreateProfile().

  if (chromeos::ProfileHelper::IsSigninProfile(profile())) {
    // Flush signin profile if it is just created (new device or after recovery)
    // to ensure it is correctly persisted.
    if (profile()->IsNewProfile())
      ProfileHelper::Get()->FlushProfile(profile());
  } else {
    // Force loading of signin profile if it was not loaded before. It is
    // possible when we are restoring session or skipping login screen for some
    // other reason.
    chromeos::ProfileHelper::GetSigninProfile();
  }

  BootTimesRecorder::Get()->OnChromeProcessStart();

  // Initialize the network portal detector for Chrome OS. The network
  // portal detector starts to listen for notifications from
  // NetworkStateHandler and initiates captive portal detection for
  // active networks. Should be called before call to initialize
  // ChromeSessionManager because it depends on NetworkPortalDetector.
  InitializeNetworkPortalDetector();
  {
#if defined(GOOGLE_CHROME_BUILD)
    bool is_official_build = true;
#else
    bool is_official_build = false;
#endif
    // Enable portal detector if EULA was previously accepted or if
    // this is an unofficial build.
    if (!is_official_build || StartupUtils::IsEulaAccepted())
      network_portal_detector::GetInstance()->Enable(true);
  }

  // Initialize an observer to update NetworkHandler's pref based services.
  network_pref_state_observer_ = base::MakeUnique<NetworkPrefStateObserver>();

  // Initialize input methods.
  input_method::InputMethodManager* manager =
      input_method::InputMethodManager::Get();
  UserSessionManager* session_manager = UserSessionManager::GetInstance();
  DCHECK(manager);
  DCHECK(session_manager);

  manager->SetState(session_manager->GetDefaultIMEState(profile()));

  bool is_running_test = parameters().ui_task != nullptr;
  g_browser_process->platform_part()->session_manager()->Initialize(
      parsed_command_line(), profile(), is_running_test);

  // Guest user profile is never initialized with locale settings,
  // so we need special handling for Guest session.
  if (user_manager::UserManager::Get()->IsLoggedInAsGuest())
    SetGuestLocale(profile());

  // This observer uses the intialized profile to dispatch extension events.
  extension_volume_observer_ = base::MakeUnique<ExtensionVolumeObserver>();

  peripheral_battery_observer_ = base::MakeUnique<PeripheralBatteryObserver>();

  renderer_freezer_ = base::MakeUnique<RendererFreezer>(
      base::MakeUnique<FreezerCgroupProcessManager>());

  g_browser_process->platform_part()->InitializeAutomaticRebootManager();
  g_browser_process->platform_part()->InitializeDeviceDisablingManager();

  // This observer cannot be created earlier because it requires the shell to be
  // available.
  idle_action_warning_observer_ = base::MakeUnique<IdleActionWarningObserver>();

  // Start watching for low disk space events to notify the user if it is not a
  // guest profile.
  if (!user_manager::UserManager::Get()->IsLoggedInAsGuest())
    low_disk_notification_ = base::MakeUnique<LowDiskNotification>();

  ChromeBrowserMainPartsLinux::PostProfileInit();
}

void ChromeBrowserMainPartsChromeos::PreBrowserStart() {
  // -- This used to be in ChromeBrowserMainParts::PreMainMessageLoopRun()
  // -- just before MetricsService::LogNeedForCleanShutdown().

  // Start the external metrics service, which collects metrics from Chrome OS
  // and passes them to the browser process.
  external_metrics_ = new chromeos::ExternalMetrics;
  external_metrics_->Start();

#if defined(USE_X11)
  // Listen for system key events so that the user will be able to adjust the
  // volume on the login screen, if Chrome is running on Chrome OS
  // (i.e. not Linux desktop), and in non-test mode.
  // Note: SystemKeyEventListener depends on the DBus thread.
  if (base::SysInfo::IsRunningOnChromeOS() &&
      !parameters().ui_task) {  // ui_task is non-NULL when running tests.
    SystemKeyEventListener::Initialize();
  }

  if (!ash_util::IsRunningInMash()) {
    // Listen for XI_HierarchyChanged events. Note: if this is moved to
    // PreMainMessageLoopRun() then desktopui_PageCyclerTests fail for unknown
    // reasons, see http://crosbug.com/24833.
    XInputHierarchyChangedEventListener::GetInstance();

    // Start the CrOS input device UMA watcher
    DeviceUMA::GetInstance();
  }
#endif

  // -- This used to be in ChromeBrowserMainParts::PreMainMessageLoopRun()
  // -- immediately after ChildProcess::WaitForDebugger().

  if (ui::ShouldDefaultToNaturalScroll()) {
    base::CommandLine::ForCurrentProcess()->AppendSwitch(
        chromeos::switches::kNaturalScrollDefault);
    system::InputDeviceSettings::Get()->SetTapToClick(true);
  }

  ChromeBrowserMainPartsLinux::PreBrowserStart();
}

void ChromeBrowserMainPartsChromeos::PostBrowserStart() {
  if (!ash_util::IsRunningInMash()) {
    system::InputDeviceSettings::Get()->UpdateTouchDevicesStatusFromPrefs();

    // These are dependent on the ash::Shell singleton already having been
    // initialized. Consequently, these cannot be used when running as a mus
    // client.
    data_promo_notification_.reset(new DataPromoNotification());

    // TODO(mash): Support EventRewriterController; see crbug.com/647781
    keyboard_event_rewriters_.reset(new EventRewriterController());
    keyboard_event_rewriters_->AddEventRewriter(
        std::unique_ptr<ui::EventRewriter>(new KeyboardDrivenEventRewriter()));
    keyboard_event_rewriters_->AddEventRewriter(
        std::unique_ptr<ui::EventRewriter>(new SpokenFeedbackEventRewriter()));
    event_rewriter_delegate_ = base::MakeUnique<EventRewriterDelegateImpl>();
    keyboard_event_rewriters_->AddEventRewriter(
        base::MakeUnique<ui::EventRewriterChromeOS>(
            event_rewriter_delegate_.get(),
            ash::Shell::Get()->sticky_keys_controller()));
    keyboard_event_rewriters_->Init();
  }

  // In classic ash must occur after ash::ShellPort is initialized. Triggers a
  // fetch of the initial CrosSettings DeviceRebootOnShutdown policy.
  shutdown_policy_forwarder_ = base::MakeUnique<ShutdownPolicyForwarder>();

  ChromeBrowserMainPartsLinux::PostBrowserStart();
}

// Shut down services before the browser process, etc are destroyed.
void ChromeBrowserMainPartsChromeos::PostMainMessageLoopRun() {
  chromeos::ResourceReporter::GetInstance()->StopMonitoring();

  BootTimesRecorder::Get()->AddLogoutTimeMarker("UIMessageLoopEnded", true);

  lock_screen_apps_state_controller_.reset();

  // This must be shut down before |arc_service_launcher_|.
  NoteTakingHelper::Shutdown();

  arc_service_launcher_->Shutdown();

  // Unregister CrosSettings observers before CrosSettings is destroyed.
  shutdown_policy_forwarder_.reset();

  // Destroy the application name notifier for Kiosk mode.
  KioskModeIdleAppNameNotification::Shutdown();

  // Shutdown the upgrade detector for Chrome OS. The upgrade detector
  // stops monitoring changes from the update engine.
  if (UpgradeDetectorChromeos::GetInstance())
    UpgradeDetectorChromeos::GetInstance()->Shutdown();

  // Shutdown the network change notifier for Chrome OS. The network
  // change notifier stops monitoring changes from the power manager and
  // the network manager.
  if (NetworkChangeNotifierFactoryChromeos::GetInstance())
    NetworkChangeNotifierFactoryChromeos::GetInstance()->Shutdown();

  // Destroy UI related classes before destroying services that they may
  // depend on.
  data_promo_notification_.reset();

  // Tell DeviceSettingsService to stop talking to session_manager. Do not
  // shutdown DeviceSettingsService yet, it might still be accessed by
  // BrowserPolicyConnector (owned by g_browser_process).
  DeviceSettingsService::Get()->UnsetSessionManager();

  // We should remove observers attached to D-Bus clients before
  // DBusThreadManager is shut down.
  network_pref_state_observer_.reset();
  extension_volume_observer_.reset();
  peripheral_battery_observer_.reset();
  power_prefs_.reset();
  renderer_freezer_.reset();
  wake_on_wifi_manager_.reset();
  network_throttling_observer_.reset();
  ScreenLocker::ShutDownClass();
  keyboard_event_rewriters_.reset();
  low_disk_notification_.reset();
#if defined(USE_X11)
  if (!ash_util::IsRunningInMash()) {
    // The XInput2 event listener needs to be shut down earlier than when
    // Singletons are finally destroyed in AtExitManager.
    XInputHierarchyChangedEventListener::GetInstance()->Stop();

    DeviceUMA::GetInstance()->Stop();
  }

  // SystemKeyEventListener::Shutdown() is always safe to call,
  // even if Initialize() wasn't called.
  SystemKeyEventListener::Shutdown();
#endif

  // Detach D-Bus clients before DBusThreadManager is shut down.
  idle_action_warning_observer_.reset();

  if (!ash_util::IsRunningInMash())
    MagnificationManager::Shutdown();

  media::SoundsManager::Shutdown();

  system::StatisticsProvider::GetInstance()->Shutdown();

  // Let the UserManager and WallpaperManager unregister itself as an observer
  // of the CrosSettings singleton before it is destroyed. This also ensures
  // that the UserManager has no URLRequest pending (see
  // http://crbug.com/276659).
  g_browser_process->platform_part()->user_manager()->Shutdown();
  WallpaperManager::Shutdown();

  // Let the DeviceDisablingManager unregister itself as an observer of the
  // CrosSettings singleton before it is destroyed.
  g_browser_process->platform_part()->ShutdownDeviceDisablingManager();

  // Let the AutomaticRebootManager unregister itself as an observer of several
  // subsystems.
  g_browser_process->platform_part()->ShutdownAutomaticRebootManager();

  // Clean up dependency on CrosSettings and stop pending data fetches.
  KioskAppManager::Shutdown();

  // Make sure that there is no pending URLRequests.
  UserSessionManager::GetInstance()->Shutdown();

  // Give BrowserPolicyConnectorChromeOS a chance to unregister any observers
  // on services that are going to be deleted later but before its Shutdown()
  // is called.
  g_browser_process->platform_part()->browser_policy_connector_chromeos()->
      PreShutdown();

  // We first call PostMainMessageLoopRun and then destroy UserManager, because
  // Ash needs to be closed before UserManager is destroyed.
  ChromeBrowserMainPartsLinux::PostMainMessageLoopRun();

  // Destroy ArcKioskAppManager after its observers are removed when Ash is
  // closed above.
  arc_kiosk_app_manager_.reset();

  if (!ash_util::IsRunningInMash())
    AccessibilityManager::Shutdown();

  input_method::Shutdown();

  // Stops all in-flight OAuth2 token fetchers before the IO thread stops.
  DeviceOAuth2TokenServiceFactory::Shutdown();

  content::MediaCaptureDevices::GetInstance()->RemoveAllVideoCaptureObservers();

  // Shutdown after PostMainMessageLoopRun() which should destroy all observers.
  CrasAudioHandler::Shutdown();

  quirks::QuirksManager::Shutdown();

  // Called after
  // ChromeBrowserMainPartsLinux::PostMainMessageLoopRun() to be
  // executed after execution of chrome::CloseAsh(), because some
  // parts of WebUI depends on NetworkPortalDetector.
  network_portal_detector::Shutdown();

  g_browser_process->platform_part()->ShutdownSessionManager();
  g_browser_process->platform_part()->DestroyChromeUserManager();
}

void ChromeBrowserMainPartsChromeos::PostDestroyThreads() {
  // Destroy DBus services immediately after threads are stopped.
  dbus_services_.reset();

  // Reset SystemTokenCertDBInitializer after DBus services because it should
  // outlive CertLoader.
  system_token_certdb_initializer_.reset();

  ChromeBrowserMainPartsLinux::PostDestroyThreads();

  // Destroy DeviceSettingsService after g_browser_process.
  DeviceSettingsService::Shutdown();
}

}  //  namespace chromeos
