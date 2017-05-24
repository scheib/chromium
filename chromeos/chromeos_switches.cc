// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/chromeos_switches.h"

#include <string>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial.h"

namespace chromeos {
namespace switches {

namespace {

// The memory pressure thresholds selection which is used to decide whether and
// when a memory pressure event needs to get fired.
const char kMemoryPressureExperimentName[] = "ChromeOSMemoryPressureHandling";
const char kMemoryPressureHandlingOff[] = "memory-pressure-off";

// Controls CrOS GaiaId migration for tests ("" is default).
const char kTestCrosGaiaIdMigration[] = "test-cros-gaia-id-migration";

// Value for kTestCrosGaiaIdMigration indicating that migration is started (i.e.
// all stored user keys will be converted to GaiaId)
const char kTestCrosGaiaIdMigrationStarted[] = "started";

// Controls whether enable voice interaction feature.
const base::Feature kVoiceInteractionFeature{"ChromeOSVoiceInteraction",
                                             base::FEATURE_DISABLED_BY_DEFAULT};

}  // namespace

// Please keep the order of these switches synchronized with the header file
// (i.e. in alphabetical order).

const char kAggressiveCacheDiscardThreshold[] = "aggressive-cache-discard";

const char kAggressiveTabDiscardThreshold[] = "aggressive-tab-discard";

const char kAggressiveThreshold[] = "aggressive";

// If this flag is passed, failed policy fetches will not cause profile
// initialization to fail. This is useful for tests because it means that
// tests don't have to mock out the policy infrastructure.
const char kAllowFailedPolicyFetchForTest[] =
    "allow-failed-policy-fetch-for-test";

// Allows remote attestation (RA) in dev mode for testing purpose. Usually RA
// is disabled in dev mode because it will always fail. However, there are cases
// in testing where we do want to go through the permission flow even in dev
// mode. This can be enabled by this flag.
const char kAllowRAInDevMode[] = "allow-ra-in-dev-mode";

// Specifies whether an app launched in kiosk mode was auto launched with zero
// delay. Used in order to properly restore auto-launched state during session
// restore flow.
const char kAppAutoLaunched[] = "app-auto-launched";

// Path for app's OEM manifest file.
const char kAppOemManifestFile[] = "app-mode-oem-manifest";

// Always starts ARC after login screen without Play Store in almost all cases.
// Secondary profile is an exception where ARC will not start.
const char kArcAlwaysStart[] = "arc-always-start";

// Signals ARC support status on this device. This can take one of the
// following three values.
// - none: ARC is not installed on this device. (default)
// - installed: ARC is installed on this device, but not officially supported.
//   Users can enable ARC only when Finch experiment is turned on.
// - officially-supported: ARC is installed and supported on this device. So
//   users can enable ARC via settings etc.
const char kArcAvailability[] = "arc-availability";

// DEPRECATED: Please use --arc-availability=installed.
// Signals the availability of the ARC instance on this device.
const char kArcAvailable[] = "arc-available";

// Screenshot testing: specifies the directoru where artifacts will be stored.
const char kArtifactsDir[] = "artifacts-dir";

// When wallpaper boot animation is not disabled this switch
// is used to override OOBE/sign in WebUI init type.
// Possible values: parallel|postpone. Default: parallel.
const char kAshWebUIInit[] = "ash-webui-init";

// If this flag is set, it indicates that this device is a "Cellular First"
// device. Cellular First devices use cellular telephone data networks as
// their primary means of connecting to the internet.
// Setting this flag has two consequences:
// 1. Cellular data roaming will be enabled by default.
// 2. UpdateEngine will be instructed to allow auto-updating over cellular
//    data connections.
const char kCellularFirst[] = "cellular-first";

// Default large wallpaper to use for kids accounts (as path to trusted,
// non-user-writable JPEG file).
const char kChildWallpaperLarge[] = "child-wallpaper-large";

// Default small wallpaper to use for kids accounts (as path to trusted,
// non-user-writable JPEG file).
const char kChildWallpaperSmall[] = "child-wallpaper-small";

const char kConservativeThreshold[] = "conservative";

// Forces CrOS region value.
const char kCrosRegion[] = "cros-region";

// Control regions data load ("" is default).
const char kCrosRegionsMode[] = "cros-regions-mode";

// "Override" value for kCrosRegionsMode (region's data is read first).
const char kCrosRegionsModeOverride[] = "override";

// "Hide" value for kCrosRegionsMode (VPD values are hidden).
const char kCrosRegionsModeHide[] = "hide";

// Optional value for Data Saver prompt on cellular networks.
const char kDataSaverPromptDemoMode[] = "demo";

// Forces the stub implementation of dbus clients.
const char kDbusStub[] = "dbus-stub";

// Indicates that the wallpaper images specified by
// kAshDefaultWallpaper{Large,Small} are OEM-specific (i.e. they are not
// downloadable from Google).
const char kDefaultWallpaperIsOem[] = "default-wallpaper-is-oem";

// Default large wallpaper to use (as path to trusted, non-user-writable JPEG
// file).
const char kDefaultWallpaperLarge[] = "default-wallpaper-large";

// Default small wallpaper to use (as path to trusted, non-user-writable JPEG
// file).
const char kDefaultWallpaperSmall[] = "default-wallpaper-small";

// Time in seconds before a machine at OOBE is considered derelict.
const char kDerelictDetectionTimeout[] = "derelict-detection-timeout";

// Time in seconds before a derelict machines starts demo mode.
const char kDerelictIdleTimeout[] = "derelict-idle-timeout";

// Disables android user data wipe on opt out.
const char kDisableArcDataWipe[] = "disable-arc-data-wipe";

// Disables ARC Opt-in verification process and ARC is enabled by default.
const char kDisableArcOptInVerification[] = "disable-arc-opt-in-verification";

// Disables wallpaper boot animation (except of OOBE case).
const char kDisableBootAnimation[] = "disable-boot-animation";

// Disables bypass proxy for captive portal authorization.
const char kDisableCaptivePortalBypassProxy[] =
    "disable-captive-portal-bypass-proxy";

// Disables cloud backup feature.
const char kDisableCloudImport[] = "disable-cloud-import";

// Disables Data Saver prompt on cellular networks.
const char kDisableDataSaverPrompt[] = "disable-datasaver-prompt";

// Disables the Chrome OS demo.
const char kDisableDemoMode[] = "disable-demo-mode";

// If this switch is set, the device cannot be remotely disabled by its owner.
const char kDisableDeviceDisabling[] = "disable-device-disabling";

// Disable encryption migration for user's cryptohome to run latest Arc.
const char kDisableEncryptionMigration[] = "disable-encryption-migration";

// Disables notification when device is in end of life status.
const char kDisableEolNotification[] = "disable-eol-notification";

// Disables GAIA services such as enrollment and OAuth session restore. Used by
// 'fake' telemetry login.
const char kDisableGaiaServices[] = "disable-gaia-services";

// Disables HID-detection OOBE screen.
const char kDisableHIDDetectionOnOOBE[] = "disable-hid-detection-on-oobe";

// Avoid doing expensive animations upon login.
const char kDisableLoginAnimations[] = "disable-login-animations";

// Disables mtp write support.
const char kDisableMtpWriteSupport[] = "disable-mtp-write-support";

// Disables the multiple display layout UI.
const char kDisableMultiDisplayLayout[] = "disable-multi-display-layout";

// Disables notifications about captive portals in session.
const char kDisableNetworkPortalNotification[] =
    "disable-network-portal-notification";

// Disables new channel switcher UI.
const char kDisableNewChannelSwitcherUI[] = "disable-new-channel-switcher-ui";

// Disables new Kiosk UI when kiosk apps are represented as user pods.
const char kDisableNewKioskUI[] = "disable-new-kiosk-ui";

// Disables the new Korean IME in chrome://settings/languages.
const char kDisableNewKoreanIme[] = "disable-new-korean-ime";

// Disables the new File System Provider API based ZIP unpacker.
const char kDisableNewZIPUnpacker[] = "disable-new-zip-unpacker";

// Disables Office Editing for Docs, Sheets & Slides component app so handlers
// won't be registered, making it possible to install another version for
// testing.
const char kDisableOfficeEditingComponentApp[] =
    "disable-office-editing-component-extension";

// Disables suggestions while typing on a physical keyboard.
const char kDisablePhysicalKeyboardAutocorrect[] =
    "disable-physical-keyboard-autocorrect";

// Disables rollback option on reset screen.
const char kDisableRollbackOption[] = "disable-rollback-option";

// Disables SystemTimezoneAutomaticDetection policy.
const char kDisableSystemTimezoneAutomaticDetectionPolicy[] =
    "disable-system-timezone-automatic-detection";

// Disables volume adjust sound.
const char kDisableVolumeAdjustSound[] = "disable-volume-adjust-sound";

// Disables wake on wifi features.
const char kDisableWakeOnWifi[] = "disable-wake-on-wifi";

// EAFE path to use for Easy bootstrapping.
const char kEafePath[] = "eafe-path";

// EAFE URL to use for Easy bootstrapping.
const char kEafeUrl[] = "eafe-url";

// Enables the Android Wallpapers App as the default app on Chrome OS.
const char kEnableAndroidWallpapersApp[] = "enable-android-wallpapers-app";

// DEPRECATED. Please use --arc-availability=officially-supported.
// Enables starting the ARC instance upon session start.
const char kEnableArc[] = "enable-arc";

// Enables ARC OptIn flow in OOBE.
const char kEnableArcOOBEOptIn[] = "enable-arc-oobe-optin";

// Enables native ChromeVox support for Arc.
const char kEnableChromeVoxArcSupport[] = "enable-chromevox-arc-support";

// Enables consumer kiosk mode for Chrome OS.
const char kEnableConsumerKiosk[] = "enable-consumer-kiosk";

// Enables Data Saver prompt on cellular networks.
const char kEnableDataSaverPrompt[] = "enable-datasaver-prompt";

// Enables encryption migration for user's cryptohome to run latest Arc.
const char kEnableEncryptionMigration[] = "enable-encryption-migration";

// Shows additional checkboxes in Settings to enable Chrome OS accessibility
// features that haven't launched yet.
const char kEnableExperimentalAccessibilityFeatures[] =
    "enable-experimental-accessibility-features";

// Enables sharing assets for installed default apps.
const char kEnableExtensionAssetsSharing[] = "enable-extension-assets-sharing";

// Enables animated transitions during first-run tutorial.
const char kEnableFirstRunUITransitions[] = "enable-first-run-ui-transitions";

// Enables action handler apps (e.g. creating new notes) on lock screen.
const char kEnableLockScreenApps[] = "enable-lock-screen-apps";

// Enables Kiosk mode for Chrome OS. Note this switch refers to retail mode
// rather than the kiosk app mode.
const char kEnableKioskMode[] = "enable-kiosk-mode";

// Enables tethering to nearby LTE devices.
const char kEnableTether[] = "enable-tether";

// Disables material design OOBE UI.
const char kDisableMdOobe[] = "disable-md-oobe";

// Enables notifications about captive portals in session.
const char kEnableNetworkPortalNotification[] =
    "enable-network-portal-notification";

// Enables suggestions while typing on a physical keyboard.
const char kEnablePhysicalKeyboardAutocorrect[] =
    "enable-physical-keyboard-autocorrect";

// Enables request of tablet site (via user agent override).
const char kEnableRequestTabletSite[] = "enable-request-tablet-site";

// Enables using screenshots in tests and seets mode.
const char kEnableScreenshotTestingWithMode[] =
    "enable-screenshot-testing-with-mode";

// Enables the touch calibration option in MD settings UI for valid touch
// displays.
const char kEnableTouchCalibrationSetting[] =
    "enable-touch-calibration-setting";

// Enables touchpad three-finger-click as middle button.
const char kEnableTouchpadThreeFingerClick[] =
    "enable-touchpad-three-finger-click";

// Enables touch support for screen magnifier.
const char kEnableTouchSupportForScreenMagnifier[] =
    "enable-touch-support-for-screen-magnifier";

// Enables the chromecast support for video player app.
const char kEnableVideoPlayerChromecastSupport[] =
    "enable-video-player-chromecast-support";

// Enables the VoiceInteraction support.
const char kEnableVoiceInteraction[] = "enable-voice-interaction";

// Enables zip archiver.
const char kEnableZipArchiverOnFileManager[] =
    "enable-zip-archiver-on-file-manager";

// Disables ARC for managed accounts.
const char kEnterpriseDisableArc[] = "enterprise-disable-arc";

// Whether to enable forced enterprise re-enrollment.
const char kEnterpriseEnableForcedReEnrollment[] =
    "enterprise-enable-forced-re-enrollment";

// Enables the zero-touch enterprise enrollment flow.
const char kEnterpriseEnableZeroTouchEnrollment[] =
    "enterprise-enable-zero-touch-enrollment";

// Power of the power-of-2 initial modulus that will be used by the
// auto-enrollment client. E.g. "4" means the modulus will be 2^4 = 16.
const char kEnterpriseEnrollmentInitialModulus[] =
    "enterprise-enrollment-initial-modulus";

// Power of the power-of-2 maximum modulus that will be used by the
// auto-enrollment client.
const char kEnterpriseEnrollmentModulusLimit[] =
    "enterprise-enrollment-modulus-limit";

// Passed to Chrome the first time that it's run after the system boots.
// Not passed on restart after sign out.
const char kFirstExecAfterBoot[] = "first-exec-after-boot";

// Forces first-run UI to be shown for every login.
const char kForceFirstRunUI[] = "force-first-run-ui";

// Usually in browser tests the usual login manager bringup is skipped so that
// tests can change how it's brought up. This flag disables that.
const char kForceLoginManagerInTests[] = "force-login-manager-in-tests";

// Screenshot testing: specifies the directory where the golden screenshots are
// stored.
const char kGoldenScreenshotsDir[] = "golden-screenshots-dir";

// Indicates that the browser is in "browse without sign-in" (Guest session)
// mode. Should completely disable extensions, sync and bookmarks.
const char kGuestSession[] = "bwsi";

// Large wallpaper to use in guest mode (as path to trusted, non-user-writable
// JPEG file).
const char kGuestWallpaperLarge[] = "guest-wallpaper-large";

// Small wallpaper to use in guest mode (as path to trusted, non-user-writable
// JPEG file).
const char kGuestWallpaperSmall[] = "guest-wallpaper-small";

// Force enables the Happiness Tracking System for the device. This ignores
// user profile check and time limits and shows the notification every time
// for any type of user. Should be used only for testing.
const char kForceHappinessTrackingSystem[] = "force-happiness-tracking-system";

// If true, the Chromebook has a keyboard with a diamond key.
const char kHasChromeOSDiamondKey[] = "has-chromeos-diamond-key";

// Defines user homedir. This defaults to primary user homedir.
const char kHomedir[] = "homedir";

// With this switch, start remora OOBE with the pairing screen.
const char kHostPairingOobe[] = "host-pairing-oobe";

// If true, profile selection in UserManager will always return active user's
// profile.
// TODO(nkostlyev): http://crbug.com/364604 - Get rid of this switch after we
// turn on multi-profile feature on ChromeOS.
const char kIgnoreUserProfileMappingForTests[] =
    "ignore-user-profile-mapping-for-tests";

// Enables Chrome-as-a-login-manager behavior.
const char kLoginManager[] = "login-manager";

// Specifies the profile to use once a chromeos user is logged in.
// This parameter is ignored if user goes through login screen since user_id
// hash defines which profile directory to use.
// In case of browser restart within active session this parameter is used
// to pass user_id hash for primary user.
const char kLoginProfile[] = "login-profile";

// Specifies the user which is already logged in.
const char kLoginUser[] = "login-user";

// The memory pressure threshold selection which is used to decide whether and
// when a memory pressure event needs to get fired.
const char kMemoryPressureThresholds[] = "memory-pressure-thresholds";

// Enables natural scroll by default.
const char kNaturalScrollDefault[] = "enable-natural-scroll-default";

// An optional comma-separated list of IDs of apps that can be used to take
// notes. If unset, a hardcoded list is used instead.
const char kNoteTakingAppIds[] = "note-taking-app-ids";

// Indicates that if we should start bootstrapping Master OOBE.
const char kOobeBootstrappingMaster[] = "oobe-bootstrapping-master";

// Indicates that a guest session has been started before OOBE completion.
const char kOobeGuestSession[] = "oobe-guest-session";

// Skips all other OOBE pages after user login.
const char kOobeSkipPostLogin[] = "oobe-skip-postlogin";

// Interval at which we check for total time on OOBE.
const char kOobeTimerInterval[] = "oobe-timer-interval";

// If true, the views-based md login and lock screens will be shown.
const char kShowMdLogin[] = "show-md-login";

// If true, the non-views-based md login and lock screens will be shown.
const char kShowNonViewMdLogin[] = "show-non-view-md-login";

// Specifies power stub behavior:
//  'cycle=2' - Cycles power states every 2 seconds.
// See FakeDBusThreadManager::ParsePowerCommandLineSwitch for full details.
const char kPowerStub[] = "power-stub";

// Overrides network stub behavior. By default, ethernet, wifi and vpn are
// enabled, and transitions occur instantaneously. Multiple options can be
// comma separated (no spaces). Note: all options are in the format 'foo=x'.
// Values are case sensitive and based on Shill names in service_constants.h.
// See FakeShillManagerClient::SetInitialNetworkState for implementation.
// Examples:
//  'clear=1' - Clears all default configurations
//  'wifi=on' - A wifi network is initially connected ('1' also works)
//  'wifi=off' - Wifi networks are all initially disconnected ('0' also works)
//  'wifi=disabled' - Wifi is initially disabled
//  'wifi=none' - Wifi is unavailable
//  'wifi=portal' - Wifi connection will be in Portal state
//  'cellular=1' - Cellular is initially connected
//  'cellular=LTE' - Cellular is initially connected, technology is LTE
//  'interactive=3' - Interactive mode, connect/scan/etc requests take 3 secs
const char kShillStub[] = "shill-stub";

// Sends test messages on first call to RequestUpdate (stub only).
const char kSmsTestMessages[] = "sms-test-messages";

// Indicates that a stub implementation of CrosSettings that stores settings in
// memory without signing should be used, treating current user as the owner.
// This also modifies OwnerSettingsServiceChromeOS::HandlesSetting such that no
// settings are handled by OwnerSettingsServiceChromeOS.
// This option is for testing the chromeos build of chrome on the desktop only.
const char kStubCrosSettings[] = "stub-cros-settings";

// Indicates that the system is running in dev mode. The dev mode probing is
// done by session manager.
const char kSystemDevMode[] = "system-developer-mode";

// Enables testing for auto update UI.
const char kTestAutoUpdateUI[] = "test-auto-update-ui";

// Determines which Google Privacy CA to use for attestation.
const char kAttestationServer[] = "attestation-server";

// Enables wake on wifi packet feature, which wakes the device on the receipt
// of network packets from whitelisted sources.
const char kWakeOnWifiPacket[] = "wake-on-wifi-packet";

// Force system compositor mode when set.
const char kForceSystemCompositorMode[] = "force-system-compositor-mode";

// Enables testing for encryption migration UI.
const char kTestEncryptionMigrationUI[] = "test-encryption-migration-ui";

bool WakeOnWifiEnabled() {
  return !base::CommandLine::ForCurrentProcess()->HasSwitch(kDisableWakeOnWifi);
}

bool MemoryPressureHandlingEnabled() {
  if (base::FieldTrialList::FindFullName(kMemoryPressureExperimentName) ==
      kMemoryPressureHandlingOff) {
    return false;
  }
  return true;
}

base::chromeos::MemoryPressureMonitor::MemoryPressureThresholds
GetMemoryPressureThresholds() {
  using MemoryPressureMonitor = base::chromeos::MemoryPressureMonitor;

  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
          kMemoryPressureThresholds)) {
    const std::string group_name =
        base::FieldTrialList::FindFullName(kMemoryPressureExperimentName);
    if (group_name == kConservativeThreshold)
      return MemoryPressureMonitor::THRESHOLD_CONSERVATIVE;
    if (group_name == kAggressiveCacheDiscardThreshold)
      return MemoryPressureMonitor::THRESHOLD_AGGRESSIVE_CACHE_DISCARD;
    if (group_name == kAggressiveTabDiscardThreshold)
      return MemoryPressureMonitor::THRESHOLD_AGGRESSIVE_TAB_DISCARD;
    if (group_name == kAggressiveThreshold)
      return MemoryPressureMonitor::THRESHOLD_AGGRESSIVE;
    return MemoryPressureMonitor::THRESHOLD_DEFAULT;
  }

  const std::string option =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          kMemoryPressureThresholds);
  if (option == kConservativeThreshold)
    return MemoryPressureMonitor::THRESHOLD_CONSERVATIVE;
  if (option == kAggressiveCacheDiscardThreshold)
    return MemoryPressureMonitor::THRESHOLD_AGGRESSIVE_CACHE_DISCARD;
  if (option == kAggressiveTabDiscardThreshold)
    return MemoryPressureMonitor::THRESHOLD_AGGRESSIVE_TAB_DISCARD;
  if (option == kAggressiveThreshold)
    return MemoryPressureMonitor::THRESHOLD_AGGRESSIVE;

  return MemoryPressureMonitor::THRESHOLD_DEFAULT;
}

bool IsGaiaIdMigrationStarted() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(kTestCrosGaiaIdMigration))
    return false;

  return command_line->GetSwitchValueASCII(kTestCrosGaiaIdMigration) ==
         kTestCrosGaiaIdMigrationStarted;
}

bool IsCellularFirstDevice() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(kCellularFirst);
}

bool IsVoiceInteractionEnabled() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
             kEnableVoiceInteraction) ||
         base::FeatureList::IsEnabled(kVoiceInteractionFeature);
}

}  // namespace switches
}  // namespace chromeos
