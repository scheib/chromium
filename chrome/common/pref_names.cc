// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/pref_names.h"

#include "base/macros.h"
#include "build/build_config.h"
#include "chrome/common/features.h"
#include "chrome/common/pref_font_webkit_names.h"
#include "extensions/features/features.h"
#include "media/media_features.h"
#include "ppapi/features/features.h"

namespace prefs {

// *************** PROFILE PREFS ***************
// These are attached to the user profile

#if defined(OS_CHROMEOS) && BUILDFLAG(ENABLE_APP_LIST)
// Stores the user id received from DM Server when enrolling a Play user on an
// Active Directory managed device. Used to report to DM Server that the account
// is still used.
const char kArcActiveDirectoryPlayUserId[] =
    "arc.active_directory_play_user_id";
// A preference to keep list of Android apps and their state.
const char kArcApps[] = "arc.apps";
// A preference to store backup and restore state for Android apps.
const char kArcBackupRestoreEnabled[] = "arc.backup_restore.enabled";
// A preference to indicate that Android's data directory should be removed.
const char kArcDataRemoveRequested[] = "arc.data.remove_requested";
// A preference representing whether a user has opted in to use Google Play
// Store on ARC.
// TODO(hidehiko): For historical reason, now the preference name does not
// directly reflect "Google Play Store". We should get and set the values via
// utility methods (IsArcPlayStoreEnabledForProfile() and
// SetArcPlayStoreEnabledForProfile()) in chrome/browser/chromeos/arc/arc_util.
const char kArcEnabled[] = "arc.enabled";
// A preference that indicated whether Android reported it's compliance status
// with provided policies. This is used only as a signal to start Android kiosk.
const char kArcPolicyComplianceReported[] = "arc.policy_compliance_reported";
// A preference that indicates that user accepted PlayStore terms.
const char kArcTermsAccepted[] = "arc.terms.accepted";
// A preference to keep user's consent to use location service.
const char kArcLocationServiceEnabled[] = "arc.location_service.enabled";
// A preference to keep list of Android packages and their infomation.
const char kArcPackages[] = "arc.packages";
// A preference to keep deferred requests of setting notifications enabled flag.
const char kArcSetNotificationsEnabledDeferred[] =
    "arc.set_notifications_enabled_deferred";
// A preference that indicates status of Android sign-in.
const char kArcSignedIn[] = "arc.signedin";
// A preference that indicates an ARC comaptible filesystem was chosen for
// the user directory (i.e., the user finished required migration.)
extern const char kArcCompatibleFilesystemChosen[] =
    "arc.compatible_filesystem.chosen";
#endif

// A bool pref that keeps whether the child status for this profile was already
// successfully checked via ChildAccountService.
const char kChildAccountStatusKnown[] = "child_account_status_known";

// A string property indicating whether default apps should be installed
// in this profile.  Use the value "install" to enable defaults apps, or
// "noinstall" to disable them.  This property is usually set in the
// master_preferences and copied into the profile preferences on first run.
// Defaults apps are installed only when creating a new profile.
const char kDefaultApps[] = "default_apps";

// Disables screenshot accelerators and extension APIs.
// This setting resides both in profile prefs and local state. Accelerator
// handling code reads local state, while extension APIs use profile pref.
const char kDisableScreenshots[] = "disable_screenshots";

// If set to true profiles are created in ephemeral mode and do not store their
// data in the profile folder on disk but only in memory.
const char kForceEphemeralProfiles[] = "profile.ephemeral_mode";

// A boolean specifying whether the New Tab page is the home page or not.
const char kHomePageIsNewTabPage[] = "homepage_is_newtabpage";

// This is the URL of the page to load when opening new tabs.
const char kHomePage[] = "homepage";

// Stores information about the important sites dialog, including the time and
// frequency it has been ignored.
const char kImportantSitesDialogHistory[] = "important_sites_dialog";

#if defined(OS_WIN)
// This is a timestamp of the last time this profile was reset by a third party
// tool. On Windows, a third party tool may set a registry value that will be
// compared to this value and if different will result in a profile reset
// prompt. See triggered_profile_resetter.h for more information.
const char kLastProfileResetTimestamp[] = "profile.last_reset_timestamp";
#endif

// The URL to open the new tab page to. Only set by Group Policy.
const char kNewTabPageLocationOverride[] = "newtab_page_location_override";

// An integer that keeps track of the profile icon version. This allows us to
// determine the state of the profile icon for icon format changes.
const char kProfileIconVersion[] = "profile.icon_version";

// Used to determine if the last session exited cleanly. Set to false when
// first opened, and to true when closing. On startup if the value is false,
// it means the profile didn't exit cleanly.
// DEPRECATED: this is replaced by kSessionExitType and exists for backwards
// compatibility.
const char kSessionExitedCleanly[] = "profile.exited_cleanly";

// A string pref whose values is one of the values defined by
// |ProfileImpl::kPrefExitTypeXXX|. Set to |kPrefExitTypeCrashed| on startup and
// one of |kPrefExitTypeNormal| or |kPrefExitTypeSessionEnded| during
// shutdown. Used to determine the exit type the last time the profile was open.
const char kSessionExitType[] = "profile.exit_type";

// The last time that the site engagement service recorded an engagement event
// for this profile for any URL. Recorded only during shutdown. Used to prevent
// the service from decaying engagement when a user does not use Chrome at all
// for an extended period of time.
const char kSiteEngagementLastUpdateTime[] = "profile.last_engagement_time";

// An integer pref. Holds one of several values:
// 0: unused, previously indicated to open the homepage on startup
// 1: restore the last session.
// 2: this was used to indicate a specific session should be restored. It is
//    no longer used, but saved to avoid conflict with old preferences.
// 3: unused, previously indicated the user wants to restore a saved session.
// 4: restore the URLs defined in kURLsToRestoreOnStartup.
// 5: open the New Tab Page on startup.
const char kRestoreOnStartup[] = "session.restore_on_startup";

// The URLs to restore on startup or when the home button is pressed. The URLs
// are only restored on startup if kRestoreOnStartup is 4.
const char kURLsToRestoreOnStartup[] = "session.startup_urls";

// Stores the email address associated with the google account of the custodian
// of the supervised user, set when the supervised user is created.
const char kSupervisedUserCustodianEmail[] = "profile.managed.custodian_email";

// Stores the display name associated with the google account of the custodian
// of the supervised user, updated (if possible) each time the supervised user
// starts a session.
const char kSupervisedUserCustodianName[] = "profile.managed.custodian_name";

// Stores the URL of the profile image associated with the google account of the
// custodian of the supervised user.
const char kSupervisedUserCustodianProfileImageURL[] =
    "profile.managed.custodian_profile_image_url";

// Stores the URL of the profile associated with the google account of the
// custodian of the supervised user.
const char kSupervisedUserCustodianProfileURL[] =
    "profile.managed.custodian_profile_url";

// Maps host names to whether the host is manually allowed or blocked.
const char kSupervisedUserManualHosts[] = "profile.managed.manual_hosts";

// Maps URLs to whether the URL is manually allowed or blocked.
const char kSupervisedUserManualURLs[] = "profile.managed.manual_urls";

// Maps extension ids to the approved version of this extension for a
// supervised user. Missing extensions are not approved.
const char kSupervisedUserApprovedExtensions[] =
    "profile.managed.approved_extensions";

// Stores whether the SafeSites filter is enabled.
const char kSupervisedUserSafeSites[] = "profile.managed.safe_sites";

// Stores the email address associated with the google account of the secondary
// custodian of the supervised user, set when the supervised user is created.
const char kSupervisedUserSecondCustodianEmail[] =
    "profile.managed.second_custodian_email";

// Stores the display name associated with the google account of the secondary
// custodian of the supervised user, updated (if possible) each time the
// supervised user starts a session.
const char kSupervisedUserSecondCustodianName[] =
    "profile.managed.second_custodian_name";

// Stores the URL of the profile image associated with the google account of the
// secondary custodian of the supervised user.
const char kSupervisedUserSecondCustodianProfileImageURL[] =
    "profile.managed.second_custodian_profile_image_url";

// Stores the URL of the profile associated with the google account of the
// secondary custodian of the supervised user.
const char kSupervisedUserSecondCustodianProfileURL[] =
    "profile.managed.second_custodian_profile_url";

// Stores settings that can be modified both by a supervised user and their
// manager. See SupervisedUserSharedSettingsService for a description of
// the format.
const char kSupervisedUserSharedSettings[] = "profile.managed.shared_settings";

// A dictionary storing whitelists for a supervised user. The key is the CRX ID
// of the whitelist, the value a dictionary containing whitelist properties
// (currently the name).
const char kSupervisedUserWhitelists[] = "profile.managed.whitelists";

#if BUILDFLAG(ENABLE_RLZ)
// Integer. RLZ ping delay in seconds.
const char kRlzPingDelaySeconds[] = "rlz_ping_delay";
#endif  // BUILDFLAG(ENABLE_RLZ)

// The application locale.
// For OS_CHROMEOS we maintain the kApplicationLocale property in both local
// state and the user's profile.  The global property determines the locale of
// the login screen, while the user's profile determines their personal locale
// preference.
const char kApplicationLocale[] = "intl.app_locale";
#if defined(OS_CHROMEOS)
// Locale preference of device' owner.  ChromeOS device appears in this locale
// after startup/wakeup/signout.
const char kOwnerLocale[] = "intl.owner_locale";
// Locale accepted by user.  Non-syncable.
// Used to determine whether we need to show Locale Change notification.
const char kApplicationLocaleAccepted[] = "intl.app_locale_accepted";
// Non-syncable item.
// It is used in two distinct ways.
// (1) Used for two-step initialization of locale in ChromeOS
//     because synchronization of kApplicationLocale is not instant.
// (2) Used to detect locale change.  Locale change is detected by
//     LocaleChangeGuard in case values of kApplicationLocaleBackup and
//     kApplicationLocale are both non-empty and differ.
// Following is a table showing how state of those prefs may change upon
// common real-life use cases:
//                                  AppLocale Backup Accepted
// Initial login                       -        A       -
// Sync                                B        A       -
// Accept (B)                          B        B       B
// -----------------------------------------------------------
// Initial login                       -        A       -
// No sync and second login            A        A       -
// Change options                      B        B       -
// -----------------------------------------------------------
// Initial login                       -        A       -
// Sync                                A        A       -
// Locale changed on login screen      A        C       -
// Accept (A)                          A        A       A
// -----------------------------------------------------------
// Initial login                       -        A       -
// Sync                                B        A       -
// Revert                              A        A       -
const char kApplicationLocaleBackup[] = "intl.app_locale_backup";
#endif

// The default character encoding to assume for a web page in the
// absence of MIME charset specification
const char kDefaultCharset[] = "intl.charset_default";

// The value to use for Accept-Languages HTTP header when making an HTTP
// request.
const char kAcceptLanguages[] = "intl.accept_languages";

// If these change, the corresponding enums in the extension API
// experimental.fontSettings.json must also change.
const char* const kWebKitScriptsForFontFamilyMaps[] = {
#define EXPAND_SCRIPT_FONT(x, script_name) script_name ,
#include "chrome/common/pref_font_script_names-inl.h"
ALL_FONT_SCRIPTS("unused param")
#undef EXPAND_SCRIPT_FONT
};

const size_t kWebKitScriptsForFontFamilyMapsLength =
    arraysize(kWebKitScriptsForFontFamilyMaps);

// Strings for WebKit font family preferences. If these change, the pref prefix
// in pref_names_util.cc and the pref format in font_settings_api.cc must also
// change.
const char kWebKitStandardFontFamilyMap[] =
    WEBKIT_WEBPREFS_FONTS_STANDARD;
const char kWebKitFixedFontFamilyMap[] =
    WEBKIT_WEBPREFS_FONTS_FIXED;
const char kWebKitSerifFontFamilyMap[] =
    WEBKIT_WEBPREFS_FONTS_SERIF;
const char kWebKitSansSerifFontFamilyMap[] =
    WEBKIT_WEBPREFS_FONTS_SANSERIF;
const char kWebKitCursiveFontFamilyMap[] =
    WEBKIT_WEBPREFS_FONTS_CURSIVE;
const char kWebKitFantasyFontFamilyMap[] =
    WEBKIT_WEBPREFS_FONTS_FANTASY;
const char kWebKitPictographFontFamilyMap[] =
    WEBKIT_WEBPREFS_FONTS_PICTOGRAPH;
const char kWebKitStandardFontFamilyArabic[] =
    "webkit.webprefs.fonts.standard.Arab";
#if defined(OS_WIN)
const char kWebKitFixedFontFamilyArabic[] =
    "webkit.webprefs.fonts.fixed.Arab";
#endif
const char kWebKitSerifFontFamilyArabic[] =
    "webkit.webprefs.fonts.serif.Arab";
const char kWebKitSansSerifFontFamilyArabic[] =
    "webkit.webprefs.fonts.sansserif.Arab";
#if defined(OS_WIN)
const char kWebKitStandardFontFamilyCyrillic[] =
    "webkit.webprefs.fonts.standard.Cyrl";
const char kWebKitFixedFontFamilyCyrillic[] =
    "webkit.webprefs.fonts.fixed.Cyrl";
const char kWebKitSerifFontFamilyCyrillic[] =
    "webkit.webprefs.fonts.serif.Cyrl";
const char kWebKitSansSerifFontFamilyCyrillic[] =
    "webkit.webprefs.fonts.sansserif.Cyrl";
const char kWebKitStandardFontFamilyGreek[] =
    "webkit.webprefs.fonts.standard.Grek";
const char kWebKitFixedFontFamilyGreek[] =
    "webkit.webprefs.fonts.fixed.Grek";
const char kWebKitSerifFontFamilyGreek[] =
    "webkit.webprefs.fonts.serif.Grek";
const char kWebKitSansSerifFontFamilyGreek[] =
    "webkit.webprefs.fonts.sansserif.Grek";
#endif
const char kWebKitStandardFontFamilyJapanese[] =
    "webkit.webprefs.fonts.standard.Jpan";
const char kWebKitFixedFontFamilyJapanese[] =
    "webkit.webprefs.fonts.fixed.Jpan";
const char kWebKitSerifFontFamilyJapanese[] =
    "webkit.webprefs.fonts.serif.Jpan";
const char kWebKitSansSerifFontFamilyJapanese[] =
    "webkit.webprefs.fonts.sansserif.Jpan";
const char kWebKitStandardFontFamilyKorean[] =
    "webkit.webprefs.fonts.standard.Hang";
const char kWebKitFixedFontFamilyKorean[] =
    "webkit.webprefs.fonts.fixed.Hang";
const char kWebKitSerifFontFamilyKorean[] =
    "webkit.webprefs.fonts.serif.Hang";
const char kWebKitSansSerifFontFamilyKorean[] =
    "webkit.webprefs.fonts.sansserif.Hang";
#if defined(OS_WIN)
const char kWebKitCursiveFontFamilyKorean[] =
    "webkit.webprefs.fonts.cursive.Hang";
#endif
const char kWebKitStandardFontFamilySimplifiedHan[] =
    "webkit.webprefs.fonts.standard.Hans";
const char kWebKitFixedFontFamilySimplifiedHan[] =
    "webkit.webprefs.fonts.fixed.Hans";
const char kWebKitSerifFontFamilySimplifiedHan[] =
    "webkit.webprefs.fonts.serif.Hans";
const char kWebKitSansSerifFontFamilySimplifiedHan[] =
    "webkit.webprefs.fonts.sansserif.Hans";
const char kWebKitStandardFontFamilyTraditionalHan[] =
    "webkit.webprefs.fonts.standard.Hant";
const char kWebKitFixedFontFamilyTraditionalHan[] =
    "webkit.webprefs.fonts.fixed.Hant";
const char kWebKitSerifFontFamilyTraditionalHan[] =
    "webkit.webprefs.fonts.serif.Hant";
const char kWebKitSansSerifFontFamilyTraditionalHan[] =
    "webkit.webprefs.fonts.sansserif.Hant";
#if defined(OS_WIN) || defined(OS_MACOSX)
const char kWebKitCursiveFontFamilySimplifiedHan[] =
    "webkit.webprefs.fonts.cursive.Hans";
const char kWebKitCursiveFontFamilyTraditionalHan[] =
    "webkit.webprefs.fonts.cursive.Hant";
#endif

// WebKit preferences.
const char kWebKitWebSecurityEnabled[] = "webkit.webprefs.web_security_enabled";
const char kWebKitDomPasteEnabled[] = "webkit.webprefs.dom_paste_enabled";
const char kWebKitTextAreasAreResizable[] =
    "webkit.webprefs.text_areas_are_resizable";
const char kWebkitTabsToLinks[] = "webkit.webprefs.tabs_to_links";
const char kWebKitAllowRunningInsecureContent[] =
    "webkit.webprefs.allow_running_insecure_content";
#if defined(OS_ANDROID)
const char kWebKitFontScaleFactor[] = "webkit.webprefs.font_scale_factor";
const char kWebKitForceEnableZoom[] = "webkit.webprefs.force_enable_zoom";
const char kWebKitPasswordEchoEnabled[] =
    "webkit.webprefs.password_echo_enabled";
#endif

const char kWebKitCommonScript[] = "Zyyy";
const char kWebKitStandardFontFamily[] = "webkit.webprefs.fonts.standard.Zyyy";
const char kWebKitFixedFontFamily[] = "webkit.webprefs.fonts.fixed.Zyyy";
const char kWebKitSerifFontFamily[] = "webkit.webprefs.fonts.serif.Zyyy";
const char kWebKitSansSerifFontFamily[] =
    "webkit.webprefs.fonts.sansserif.Zyyy";
const char kWebKitCursiveFontFamily[] = "webkit.webprefs.fonts.cursive.Zyyy";
const char kWebKitFantasyFontFamily[] = "webkit.webprefs.fonts.fantasy.Zyyy";
const char kWebKitPictographFontFamily[] =
    "webkit.webprefs.fonts.pictograph.Zyyy";
const char kWebKitDefaultFontSize[] = "webkit.webprefs.default_font_size";
const char kWebKitDefaultFixedFontSize[] =
    "webkit.webprefs.default_fixed_font_size";
const char kWebKitMinimumFontSize[] = "webkit.webprefs.minimum_font_size";
const char kWebKitMinimumLogicalFontSize[] =
    "webkit.webprefs.minimum_logical_font_size";
const char kWebKitJavascriptEnabled[] = "webkit.webprefs.javascript_enabled";
const char kWebKitJavascriptCanOpenWindowsAutomatically[] =
    "webkit.webprefs.javascript_can_open_windows_automatically";
const char kWebKitLoadsImagesAutomatically[] =
    "webkit.webprefs.loads_images_automatically";
const char kWebKitPluginsEnabled[] = "webkit.webprefs.plugins_enabled";
const char kWebKitEncryptedMediaEnabled[] =
    "webkit.webprefs.encrypted_media_enabled";

// Boolean that is true when Data Saver is enabled.
// TODO(bengr): Migrate the preference string to "data_saver.enabled"
// (crbug.com/564207).
const char kDataSaverEnabled[] = "spdy_proxy.enabled";

// Boolean that is true when SafeBrowsing is enabled.
const char kSafeBrowsingEnabled[] = "safebrowsing.enabled";

// Boolean that is true when the SafeBrowsing interstitial should not allow
// users to proceed anyway.
const char kSafeBrowsingProceedAnywayDisabled[] =
    "safebrowsing.proceed_anyway_disabled";

// A dictionary mapping incident types to a dict of incident key:digest pairs.
const char kSafeBrowsingIncidentsSent[] = "safebrowsing.incidents_sent";

// Boolean that tells us whether users are given the option to opt in to Safe
// Browsing extended reporting.
const char kSafeBrowsingExtendedReportingOptInAllowed[] =
    "safebrowsing.extended_reporting_opt_in_allowed";

// Boolean that is true when the SSL interstitial should allow users to
// proceed anyway. Otherwise, proceeding is not possible.
const char kSSLErrorOverrideAllowed[] = "ssl.error_override_allowed";

// Enum that specifies whether Incognito mode is:
// 0 - Enabled. Default behaviour. Default mode is available on demand.
// 1 - Disabled. Used cannot browse pages in Incognito mode.
// 2 - Forced. All pages/sessions are forced into Incognito.
const char kIncognitoModeAvailability[] = "incognito.mode_availability";

// Boolean that is true when Suggest support is enabled.
const char kSearchSuggestEnabled[] = "search.suggest_enabled";

#if defined(OS_ANDROID)
// String indicating the Contextual Search enabled state.
// "false" - opt-out (disabled)
// "" (empty string) - undecided
// "true" - opt-in (enabled)
const char kContextualSearchEnabled[] = "search.contextual_search_enabled";
#endif  // defined(OS_ANDROID)

#if defined(OS_MACOSX)
// Boolean that indicates whether the browser should put up a confirmation
// window when the user is attempting to quit. Mac only.
const char kConfirmToQuitEnabled[] = "browser.confirm_to_quit";

// Boolean that indicates whether the browser should show the toolbar when it's
// in fullscreen. Mac only.
const char kShowFullscreenToolbar[] = "browser.show_fullscreen_toolbar";

// TODO(spqchan): Remove this, see crbug.com/590827.
// This is being migrated to kShowFullscreenToolbar.
const char kHideFullscreenToolbar[] = "browser.hide_fullscreen_toolbar";
#endif

// Boolean which specifies whether we should ask the user if we should download
// a file (true) or just download it automatically.
const char kPromptForDownload[] = "download.prompt_for_download";

// A boolean pref set to true if we're using Link Doctor error pages.
const char kAlternateErrorPagesEnabled[] = "alternate_error_pages.enabled";

// An adaptively identified list of domain names to be pre-fetched during the
// next startup, based on what was actually needed during this startup.
const char kDnsPrefetchingStartupList[] = "dns_prefetching.startup_list";

// A list of host names used to fetch web pages, and their commonly used
// sub-resource hostnames (and expected latency benefits from pre-resolving, or
// preconnecting to, such sub-resource hostnames).
// This list is adaptively grown and pruned.
const char kDnsPrefetchingHostReferralList[] =
    "dns_prefetching.host_referral_list";

// Controls if the QUIC protocol is allowed.
const char kQuicAllowed[] = "net.quic_allowed";

// Prefs for persisting HttpServerProperties.
const char kHttpServerProperties[] = "net.http_server_properties";

// Prefs for persisting network qualities.
const char kNetworkQualities[] = "net.network_qualities";

#if defined(OS_ANDROID)
// Last time that a check for cloud policy management was done. This time is
// recorded on Android so that retries aren't attempted on every startup.
// Instead the cloud policy registration is retried at least 1 or 3 days later.
const char kLastPolicyCheckTime[] = "policy.last_policy_check_time";
#endif

// Prefix URL for the experimental Instant ZeroSuggest provider.
const char kInstantUIZeroSuggestUrlPrefix[] =
    "instant_ui.zero_suggest_url_prefix";

// A preference of enum chrome_browser_net::NetworkPredictionOptions shows
// if prediction of network actions is allowed, depending on network type.
// Actions include DNS prefetching, TCP and SSL preconnection, prerendering
// of web pages, and resource prefetching.
// TODO(bnc): Implement this preference as per crbug.com/334602.
const char kNetworkPredictionOptions[] = "net.network_prediction_options";

// An integer representing the state of the default apps installation process.
// This value is persisted in the profile's user preferences because the process
// is async, and the user may have stopped chrome in the middle.  The next time
// the profile is opened, the process will continue from where it left off.
//
// See possible values in external_provider_impl.cc.
const char kDefaultAppsInstallState[] = "default_apps_install_state";

// A boolean pref set to true if the Chrome Web Store icons should be hidden
// from the New Tab Page and app launcher.
const char kHideWebStoreIcon[] = "hide_web_store_icon";

#if defined(OS_CHROMEOS)
// A boolean pref set to true if touchpad tap-to-click is enabled.
const char kTapToClickEnabled[] = "settings.touchpad.enable_tap_to_click";

// A boolean pref set to true if touchpad tap-dragging is enabled.
const char kTapDraggingEnabled[] = "settings.touchpad.enable_tap_dragging";

// A boolean pref set to true if touchpad three-finger-click is enabled.
const char kEnableTouchpadThreeFingerClick[] =
    "settings.touchpad.enable_three_finger_click";

// A boolean pref set to true if touchpad natural scrolling is enabled.
const char kNaturalScroll[] = "settings.touchpad.natural_scroll";

// A boolean pref set to true if primary mouse button is the left button.
const char kPrimaryMouseButtonRight[] = "settings.mouse.primary_right";

// A integer pref for the touchpad sensitivity.
const char kMouseSensitivity[] = "settings.mouse.sensitivity2";

// A integer pref for the touchpad sensitivity.
const char kTouchpadSensitivity[] = "settings.touchpad.sensitivity2";

// A boolean pref set to true if time should be displayed in 24-hour clock.
const char kUse24HourClock[] = "settings.clock.use_24hour_clock";

// This setting disables manual timezone selection and starts periodic timezone
// refresh.
const char kResolveTimezoneByGeolocation[] =
    "settings.resolve_timezone_by_geolocation";

// A string pref set to the current input method.
const char kLanguageCurrentInputMethod[] =
    "settings.language.current_input_method";

// A string pref set to the previous input method.
const char kLanguagePreviousInputMethod[] =
    "settings.language.previous_input_method";

// A string pref (comma-separated list) set to the preferred language IDs
// (ex. "en-US,fr,ko").
const char kLanguagePreferredLanguages[] =
    "settings.language.preferred_languages";
const char kLanguagePreferredLanguagesSyncable[] =
    "settings.language.preferred_languages_syncable";

// A string pref (comma-separated list) set to the preloaded (active) input
// method IDs (ex. "pinyin,mozc").
const char kLanguagePreloadEngines[] = "settings.language.preload_engines";
const char kLanguagePreloadEnginesSyncable[] =
    "settings.language.preload_engines_syncable";

// A string pref (comma-separated list) set to the extension IMEs to be enabled.
const char kLanguageEnabledExtensionImes[] =
    "settings.language.enabled_extension_imes";
const char kLanguageEnabledExtensionImesSyncable[] =
    "settings.language.enabled_extension_imes_syncable";

// A boolean pref set to true if the IME menu is activated.
const char kLanguageImeMenuActivated[] = "settings.language.ime_menu_activated";

// A boolean pref to indicate whether we still need to add the globally synced
// input methods. False after the initial post-OOBE sync.
const char kLanguageShouldMergeInputMethods[] =
    "settings.language.merge_input_methods";

// A boolean pref that causes top-row keys to be interpreted as function keys
// instead of as media keys.
const char kLanguageSendFunctionKeys[] =
    "settings.language.send_function_keys";

// A boolean pref which determines whether key repeat is enabled.
const char kLanguageXkbAutoRepeatEnabled[] =
    "settings.language.xkb_auto_repeat_enabled_r2";
// A integer pref which determines key repeat delay (in ms).
const char kLanguageXkbAutoRepeatDelay[] =
    "settings.language.xkb_auto_repeat_delay_r2";
// A integer pref which determines key repeat interval (in ms).
const char kLanguageXkbAutoRepeatInterval[] =
    "settings.language.xkb_auto_repeat_interval_r2";
// "_r2" suffixes were added to the three prefs above when we changed the
// preferences to not be user-configurable or sync with the cloud. The prefs are
// now user-configurable and syncable again, but we don't want to overwrite the
// current values with the old synced values, so we continue to use this suffix.

// A boolean pref which determines whether the large cursor feature is enabled.
const char kAccessibilityLargeCursorEnabled[] =
    "settings.a11y.large_cursor_enabled";

// A integer pref that specifies the size of large cursor for accessibility.
const char kAccessibilityLargeCursorDipSize[] =
    "settings.a11y.large_cursor_dip_size";

// A boolean pref which determines whether the sticky keys feature is enabled.
const char kAccessibilityStickyKeysEnabled[] =
    "settings.a11y.sticky_keys_enabled";
// A boolean pref which determines whether spoken feedback is enabled.
const char kAccessibilitySpokenFeedbackEnabled[] = "settings.accessibility";
// A boolean pref which determines whether high conrast is enabled.
const char kAccessibilityHighContrastEnabled[] =
    "settings.a11y.high_contrast_enabled";
// A boolean pref which determines whether screen magnifier is enabled.
const char kAccessibilityScreenMagnifierEnabled[] =
    "settings.a11y.screen_magnifier";
// A boolean pref which determines whether screen magnifier should center
// the text input focus.
const char kAccessibilityScreenMagnifierCenterFocus[] =
    "settings.a11y.screen_magnifier_center_focus";
// A integer pref which determines what type of screen magnifier is enabled.
// Note that: 'screen_magnifier_type' had been used as string pref. Hence,
// we are using another name pref here.
const char kAccessibilityScreenMagnifierType[] =
    "settings.a11y.screen_magnifier_type2";
// A double pref which determines a zooming scale of the screen magnifier.
const char kAccessibilityScreenMagnifierScale[] =
    "settings.a11y.screen_magnifier_scale";
// A boolean pref which determines whether the virtual keyboard is enabled for
// accessibility.  This feature is separate from displaying an onscreen keyboard
// due to lack of a physical keyboard.
const char kAccessibilityVirtualKeyboardEnabled[] =
    "settings.a11y.virtual_keyboard";
// A boolean pref which determines whether the mono audio output is enabled for
// accessibility.
const char kAccessibilityMonoAudioEnabled[] =
    "settings.a11y.mono_audio";
// A boolean pref which determines whether autoclick is enabled.
const char kAccessibilityAutoclickEnabled[] = "settings.a11y.autoclick";
// An integer pref which determines time in ms between when the mouse cursor
// stops and when an autoclick is triggered.
const char kAccessibilityAutoclickDelayMs[] =
    "settings.a11y.autoclick_delay_ms";
// A boolean pref which determines whether caret highlighting is enabled.
const char kAccessibilityCaretHighlightEnabled[] =
    "settings.a11y.caret_highlight";
// A boolean pref which determines whether cursor highlighting is enabled.
const char kAccessibilityCursorHighlightEnabled[] =
    "settings.a11y.cursor_highlight";
// A boolean pref which determines whether focus highlighting is enabled.
const char kAccessibilityFocusHighlightEnabled[] =
    "settings.a11y.focus_highlight";
// A boolean pref which determines whether select-to-speak is enabled.
const char kAccessibilitySelectToSpeakEnabled[] =
    "settings.a11y.select_to_speak";
// A boolean pref which determines whether switch access is enabled.
const char kAccessibilitySwitchAccessEnabled[] =
    "settings.a11y.switch_access";
// A boolean pref which determines whether the accessibility menu shows
// regardless of the state of a11y features.
const char kShouldAlwaysShowAccessibilityMenu[] = "settings.a11y.enable_menu";

// A boolean pref which turns on Advanced Filesystem
// (USB support, SD card, etc).
const char kLabsAdvancedFilesystemEnabled[] =
    "settings.labs.advanced_filesystem";

// A boolean pref which turns on the mediaplayer.
const char kLabsMediaplayerEnabled[] = "settings.labs.mediaplayer";

// A boolean pref that turns on automatic screen locking.
const char kEnableAutoScreenLock[] = "settings.enable_screen_lock";

// A boolean pref of whether to show 3G promo notification.
const char kShow3gPromoNotification[] =
    "settings.internet.mobile.show_3g_promo_notification";

// An integer pref counting times Data Saver prompt has been shown.
const char kDataSaverPromptsShown[] =
    "settings.internet.mobile.datasaver_prompts_shown";

// A string pref that contains version where "What's new" promo was shown.
const char kChromeOSReleaseNotesVersion[] = "settings.release_notes.version";

// Power state of the current displays from the last run.
const char kDisplayPowerState[] = "settings.display.power_state";
// A dictionary pref that stores per display preferences.
const char kDisplayProperties[] = "settings.display.properties";

// A dictionary pref that specifies per-display layout/offset information.
// Its key is the ID of the display and its value is a dictionary for the
// layout/offset information.
const char kSecondaryDisplays[] = "settings.display.secondary_displays";

// A dictionary pref that specifies the state of the rotation lock, and the
// display orientation, for the internal display.
const char kDisplayRotationLock[] = "settings.display.rotation_lock";

// A boolean pref that specifies if the stylus tools should be enabled/disabled.
const char kEnableStylusTools[] = "settings.enable_stylus_tools";

// A boolean pref that specifies if the ash palette should be launched after an
// eject input event has been received.
const char kLaunchPaletteOnEjectEvent[] =
    "settings.launch_palette_on_eject_event";

// A string pref that contains either a Chrome app ID (see
// extensions::ExtensionId) or an Android package name (using Java package
// naming conventions) of the preferred note-taking app. An empty value
// indicates that the user hasn't selected an app yet.
const char kNoteTakingAppId[] = "settings.note_taking_app_id";

// A boolean pref indicating whether user activity has been observed in the
// current session already. The pref is used to restore information about user
// activity after browser crashes.
const char kSessionUserActivitySeen[] = "session.user_activity_seen";

// A preference to keep track of the session start time. If the session length
// limit is configured to start running after initial user activity has been
// observed, the pref is set after the first user activity in a session.
// Otherwise, it is set immediately after session start. The pref is used to
// restore the session start time after browser crashes. The time is expressed
// as the serialization obtained from base::TimeTicks::ToInternalValue().
const char kSessionStartTime[] = "session.start_time";

// Holds the maximum session time in milliseconds. If this pref is set, the
// user is logged out when the maximum session time is reached. The user is
// informed about the remaining time by a countdown timer shown in the ash
// system tray.
const char kSessionLengthLimit[] = "session.length_limit";

// Whether the session length limit should start running only after the first
// user activity has been observed in a session.
const char kSessionWaitForInitialUserActivity[] =
    "session.wait_for_initial_user_activity";

// A preference of the last user session type. It is used with the
// kLastSessionLength pref below to store the last user session info
// on shutdown so that it could be reported on the next run.
const char kLastSessionType[] = "session.last_session_type";

// A preference of the last user session length.
const char kLastSessionLength[] = "session.last_session_length";

// Inactivity time in milliseconds while the system is on AC power before
// the screen should be dimmed, turned off, or locked, before an
// IdleActionImminent D-Bus signal should be sent, or before
// kPowerAcIdleAction should be performed.  0 disables the delay (N/A for
// kPowerAcIdleDelayMs).
const char kPowerAcScreenDimDelayMs[] = "power.ac_screen_dim_delay_ms";
const char kPowerAcScreenOffDelayMs[] = "power.ac_screen_off_delay_ms";
const char kPowerAcScreenLockDelayMs[] = "power.ac_screen_lock_delay_ms";
const char kPowerAcIdleWarningDelayMs[] = "power.ac_idle_warning_delay_ms";
const char kPowerAcIdleDelayMs[] = "power.ac_idle_delay_ms";

// Similar delays while the system is on battery power.
const char kPowerBatteryScreenDimDelayMs[] =
    "power.battery_screen_dim_delay_ms";
const char kPowerBatteryScreenOffDelayMs[] =
    "power.battery_screen_off_delay_ms";
const char kPowerBatteryScreenLockDelayMs[] =
    "power.battery_screen_lock_delay_ms";
const char kPowerBatteryIdleWarningDelayMs[] =
    "power.battery_idle_warning_delay_ms";
const char kPowerBatteryIdleDelayMs[] =
    "power.battery_idle_delay_ms";

// Inactivity delays used to dim the screen or turn it off while the screen is
// locked.
const char kPowerLockScreenDimDelayMs[] = "power.lock_screen_dim_delay_ms";
const char kPowerLockScreenOffDelayMs[] = "power.lock_screen_off_delay_ms";

// Action that should be performed when the idle delay is reached while the
// system is on AC power or battery power.
// Values are from the chromeos::PowerPolicyController::Action enum.
const char kPowerAcIdleAction[] = "power.ac_idle_action";
const char kPowerBatteryIdleAction[] = "power.battery_idle_action";

// Action that should be performed when the lid is closed.
// Values are from the chromeos::PowerPolicyController::Action enum.
const char kPowerLidClosedAction[] = "power.lid_closed_action";

// Should audio and video activity be used to disable the above delays?
const char kPowerUseAudioActivity[] = "power.use_audio_activity";
const char kPowerUseVideoActivity[] = "power.use_video_activity";

// Should extensions be able to use the chrome.power API to override
// screen-related power management (including locking)?
const char kPowerAllowScreenWakeLocks[] = "power.allow_screen_wake_locks";

// Amount by which the screen-dim delay should be scaled while the system
// is in presentation mode. Values are limited to a minimum of 1.0.
const char kPowerPresentationScreenDimDelayFactor[] =
    "power.presentation_screen_dim_delay_factor";

// Amount by which the screen-dim delay should be scaled when user activity is
// observed while the screen is dimmed or soon after the screen has been turned
// off.  Values are limited to a minimum of 1.0.
const char kPowerUserActivityScreenDimDelayFactor[] =
    "power.user_activity_screen_dim_delay_factor";

// Whether the power management delays should start running only after the first
// user activity has been observed in a session.
const char kPowerWaitForInitialUserActivity[] =
    "power.wait_for_initial_user_activity";

// Boolean controlling whether the panel backlight should be forced to a
// nonzero level when user activity is observed.
const char kPowerForceNonzeroBrightnessForUserActivity[] =
    "power.force_nonzero_brightness_for_user_activity";

// The URL from which the Terms of Service can be downloaded. The value is only
// honored for public accounts.
const char kTermsOfServiceURL[] = "terms_of_service.url";

// Indicates that the Profile has made navigations that used a certificate
// installed by the system administrator. If that is true then the local cache
// of remote data is tainted (e.g. shared scripts), and future navigations
// show a warning indicating that the organization may track the browsing
// session.
const char kUsedPolicyCertificatesOnce[] = "used_policy_certificates_once";

// Indicates whether the remote attestation is enabled for the user.
const char kAttestationEnabled[] = "attestation.enabled";
// The list of extensions allowed to use the platformKeysPrivate API for
// remote attestation.
const char kAttestationExtensionWhitelist[] = "attestation.extension_whitelist";

// A boolean pref indicating whether the projection touch HUD is enabled or not.
const char kTouchHudProjectionEnabled[] = "touch_hud.projection_enabled";

// A boolean pref recording whether user has dismissed the multiprofile
// itroduction dialog show.
const char kMultiProfileNeverShowIntro[] =
    "settings.multi_profile_never_show_intro";

// A boolean pref recording whether user has dismissed the multiprofile
// teleport warning dialog show.
const char kMultiProfileWarningShowDismissed[] =
    "settings.multi_profile_warning_show_dismissed";

// A string pref that holds string enum values of how the user should behave
// in a multiprofile session. See ChromeOsMultiProfileUserBehavior policy
// for more details of the valid values.
const char kMultiProfileUserBehavior[] = "settings.multiprofile_user_behavior";

// A boolean preference indicating whether user has seen first-run tutorial
// already.
const char kFirstRunTutorialShown[] = "settings.first_run_tutorial_shown";

// Indicates the amount of time for which a user authenticated via SAML can use
// offline authentication against a cached password before being forced to go
// through online authentication against GAIA again. The time is expressed in
// seconds. A value of -1 indicates no limit, allowing the user to use offline
// authentication indefinitely. The limit is in effect only if GAIA redirected
// the user to a SAML IdP during the last online authentication.
const char kSAMLOfflineSigninTimeLimit[] = "saml.offline_signin_time_limit";

// A preference to keep track of the last time the user authenticated against
// GAIA using SAML. The preference is updated whenever the user authenticates
// against GAIA: If GAIA redirects to a SAML IdP, the preference is set to the
// current time. If GAIA performs the authentication itself, the preference is
// cleared. The time is expressed as the serialization obtained from
// base::Time::ToInternalValue().
const char kSAMLLastGAIASignInTime[] = "saml.last_gaia_sign_in_time";

// The total number of seconds that the machine has spent sitting on the
// OOBE screen.
const char kTimeOnOobe[] = "settings.time_on_oobe";

// The app/extension name who sets the current wallpaper. If current wallpaper
// is set by the component wallpaper picker, it is set to an empty string.
const char kCurrentWallpaperAppName[] = "wallpaper.app.name";

// List of mounted file systems via the File System Provider API. Used to
// restore them after a reboot.
const char kFileSystemProviderMounted[] = "file_system_provider.mounted";

// A boolean pref set to true if the virtual keyboard should be enabled.
const char kTouchVirtualKeyboardEnabled[] = "ui.touch_virtual_keyboard_enabled";

// Boolean prefs for the status of the touchscreen and the touchpad.
const char kTouchscreenEnabled[] = "events.touch_screen.enabled";
const char kTouchpadEnabled[] = "events.touch_pad.enabled";

// A boolean pref that controls whether the dark connect feature is enabled.
// The dark connect feature allows a Chrome OS device to periodically wake
// from suspend in a low-power state to maintain WiFi connectivity.
const char kWakeOnWifiDarkConnect[] =
    "settings.internet.wake_on_wifi_darkconnect";

// This is the policy CaptivePortalAuthenticationIgnoresProxy that allows to
// open captive portal authentication pages in a separate window under
// a temporary incognito profile ("signin profile" is used for this purpose),
// which allows to bypass the user's proxy for captive portal authentication.
const char kCaptivePortalAuthenticationIgnoresProxy[] =
    "proxy.captive_portal_ignores_proxy";

// This boolean controls whether the first window shown on first run should be
// unconditionally maximized, overriding the heuristic that normally chooses the
// window size.
const char kForceMaximizeOnFirstRun[] = "ui.force_maximize_on_first_run";

// A dictionary pref mapping public keys that identify platform keys to its
// properties like whether it's meant for corporate usage.
const char kPlatformKeys[] = "platform_keys";

// A boolean pref. If set to true, the Unified Desktop feature is made
// available and turned on by default, which allows applications to span
// multiple screens. Users may turn the feature off and on in the settings
// while this is set to true.
const char kUnifiedDesktopEnabledByDefault[] =
    "settings.display.unified_desktop_enabled_by_default";

// Whether the Chrome OS lock screen is allowed.
const char kAllowScreenLock[] = "allow_screen_lock";

// An int64 pref. This is a timestamp of the most recent time the profile took
// or dismissed HaTS (happiness-tracking) survey.
const char kHatsLastInteractionTimestamp[] = "hats_last_interaction_timestamp";

// An int64 pref. This is the timestamp that indicates the end of the most
// recent survey cycle.
const char kHatsSurveyCycleEndTimestamp[] = "hats_survey_cycle_end_timestamp";

// A boolean pref. Indicates if the device is selected for HaTS in the current
// survey cycle.
const char kHatsDeviceIsSelected[] = "hats_device_is_selected";

// A boolean pref. Indicates if we've already shown a notification to inform the
// current user about the quick unlock feature.
const char kPinUnlockFeatureNotificationShown[] =
    "pin_unlock_feature_notification_shown";
// A boolean pref. Indicates if we've already shown a notification to inform the
// current user about the fingerprint unlock feature.
const char kFingerprintUnlockFeatureNotificationShown[] =
    "fingerprint_unlock_feature_notification_shown";

// The salt and hash for the pin quick unlock mechanism.
const char kQuickUnlockPinSalt[] = "quick_unlock.pin.salt";
const char kQuickUnlockPinSecret[] = "quick_unlock.pin.secret";

// An integer pref. Indicates the number of fingerprint records registered.
const char kQuickUnlockFingerprintRecord[] = "quick_unlock.fingerprint.record";

// An integer pref. Holds one of several values:
// 0: Supported. Device is in supported state.
// 1: Security Only. Device is in Security-Only update (after initial 5 years).
// 2: EOL. Device is End of Life(No more updates expected).
// This value needs to be consistent with EndOfLifeStatus enum.
const char kEolStatus[] = "eol_status";

// Boolean pref indicating the End Of Life notification was dismissed by the
// user.
const char kEolNotificationDismissed[] = "eol_notification_dismissed";

// A list of allowed quick unlock modes. A quick unlock mode can only be used if
// its type is on this list, or if type all (all quick unlock modes enabled) is
// on this list.
const char kQuickUnlockModeWhitelist[] = "quick_unlock_mode_whitelist";
// Enum that specifies how often a user has to enter their password to continue
// using quick unlock. These values are the same as the ones in
// chromeos::QuickUnlockPasswordConfirmationFrequency.
// 0 - six hours. Users will have to enter their password every six hours.
// 1 - twelve hours. Users will have to enter their password every twelve hours.
// 2 - day. Users will have to enter their password every day.
// 3 - week. Users will have to enter their password every week.
const char kQuickUnlockTimeout[] = "quick_unlock_timeout";
// Integer prefs indicating the minimum and maximum lengths of the lock screen
// pin.
const char kPinUnlockMinimumLength[] = "pin_unlock_minimum_length";
const char kPinUnlockMaximumLength[] = "pin_unlock_maximum_length";
// Boolean pref indicating whether users are allowed to set easy pins.
const char kPinUnlockWeakPinsAllowed[] = "pin_unlock_weak_pins_allowed";

// Boolean pref indicating whether fingerprint unlock is enabled.
const char kEnableQuickUnlockFingerprint[] =
    "settings.enable_quick_unlock_fingerprint";

// Boolean pref indicating whether a user is allowed to use Tether.
const char kInstantTetheringAllowed[] = "tether.allowed";

// Boolean pref indicating whether a user has enabled Tether.
const char kInstantTetheringEnabled[] = "tether.enabled";
#endif  // defined(OS_CHROMEOS)

// A boolean pref set to true if a Home button to open the Home pages should be
// visible on the toolbar.
const char kShowHomeButton[] = "browser.show_home_button";

// Boolean pref to define the default setting for "block offensive words".
// The old key value is kept to avoid unnecessary migration code.
const char kSpeechRecognitionFilterProfanities[] =
    "browser.speechinput_censor_results";

// Boolean controlling whether history saving is disabled.
const char kSavingBrowserHistoryDisabled[] = "history.saving_disabled";

// Boolean controlling whether deleting browsing and download history is
// permitted.
const char kAllowDeletingBrowserHistory[] = "history.deleting_enabled";

#if !defined(OS_ANDROID) && !defined(OS_IOS)
// Whether the "Click here to clear your browsing data" tooltip promo has been
// shown on the Material Design History page.
const char kMdHistoryMenuPromoShown[] = "history.menu_promo_shown";
#endif

// Boolean controlling whether SafeSearch is mandatory for Google Web Searches.
const char kForceGoogleSafeSearch[] = "settings.force_google_safesearch";

// Integer controlling whether Restrict Mode (moderate/strict) is mandatory on
// YouTube. See |safe_search_util::YouTubeRestrictMode| for possible values.
const char kForceYouTubeRestrict[] = "settings.force_youtube_restrict";

// Boolean controlling whether history is recorded via Session Sync
// (for supervised users).
const char kForceSessionSync[] = "settings.history_recorded";

// Comma separated list of domain names (e.g. "google.com,school.edu").
// When this pref is set, the user will be able to access Google Apps
// only using an account that belongs to one of the domains from this pref.
const char kAllowedDomainsForApps[] = "settings.allowed_domains_for_apps";

#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
// Linux specific preference on whether we should match the system theme.
const char kUsesSystemTheme[] = "extensions.theme.use_system";
#endif
const char kCurrentThemePackFilename[] = "extensions.theme.pack";
const char kCurrentThemeID[] = "extensions.theme.id";
const char kCurrentThemeImages[] = "extensions.theme.images";
const char kCurrentThemeColors[] = "extensions.theme.colors";
const char kCurrentThemeTints[] = "extensions.theme.tints";
const char kCurrentThemeDisplayProperties[] = "extensions.theme.properties";

// Boolean pref which persists whether the extensions_ui is in developer mode
// (showing developer packing tools and extensions details)
const char kExtensionsUIDeveloperMode[] = "extensions.ui.developer_mode";

// Boolean pref which indicates whether the Chrome Apps & Extensions Developer
// Tool promotion has been dismissed by the user.
const char kExtensionsUIDismissedADTPromo[] =
    "extensions.ui.dismissed_adt_promo";

// Dictionary pref that tracks which command belongs to which
// extension + named command pair.
const char kExtensionCommands[] = "extensions.commands";

// Pref containing the directory for internal plugins as written to the plugins
// list (below).
const char kPluginsLastInternalDirectory[] = "plugins.last_internal_directory";

// List pref containing information (dictionaries) on plugins.
const char kPluginsPluginsList[] = "plugins.plugins_list";

// List pref containing names of plugins that are disabled by policy.
const char kPluginsDisabledPlugins[] = "plugins.plugins_disabled";

// List pref containing exceptions to the list of plugins disabled by policy.
const char kPluginsDisabledPluginsExceptions[] =
    "plugins.plugins_disabled_exceptions";

// List pref containing names of plugins that are enabled by policy.
const char kPluginsEnabledPlugins[] = "plugins.plugins_enabled";

// Whether Chrome should use its internal PDF viewer or not.
const char kPluginsAlwaysOpenPdfExternally[] =
    "plugins.always_open_pdf_externally";

#if BUILDFLAG(ENABLE_PLUGINS)
// Whether about:plugins is shown in the details mode or not.
const char kPluginsShowDetails[] = "plugins.show_details";
#endif

// Boolean that indicates whether outdated plugins are allowed or not.
const char kPluginsAllowOutdated[] = "plugins.allow_outdated";

// Boolean that indicates whether plugins that require authorization should
// be always allowed or not.
const char kPluginsAlwaysAuthorize[] = "plugins.always_authorize";

#if BUILDFLAG(ENABLE_PLUGINS)
// Dictionary holding plugins metadata.
const char kPluginsMetadata[] = "plugins.metadata";

// Last update time of plugins resource cache.
const char kPluginsResourceCacheUpdate[] = "plugins.resource_cache_update";
#endif

// Int64 containing the internal value of the time at which the default browser
// infobar was last dismissed by the user.
const char kDefaultBrowserLastDeclined[] =
    "browser.default_browser_infobar_last_declined";
// Boolean that indicates whether the kDefaultBrowserLastDeclined preference
// should be reset on start-up.
const char kResetCheckDefaultBrowser[] =
    "browser.should_reset_check_default_browser";

// Policy setting whether default browser check should be disabled and default
// browser registration should take place.
const char kDefaultBrowserSettingEnabled[] =
    "browser.default_browser_setting_enabled";

#if defined(OS_MACOSX)
// Boolean that indicates whether the application should show the info bar
// asking the user to set up automatic updates when Keystone promotion is
// required.
const char kShowUpdatePromotionInfoBar[] =
    "browser.show_update_promotion_info_bar";
#endif

#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
// Boolean that is false if we should show window manager decorations.  If
// true, we draw a custom chrome frame (thicker title bar and blue border).
const char kUseCustomChromeFrame[] = "browser.custom_chrome_frame";
#endif

const char kBackShortcutBubbleShownCount[] =
    "browser.back_shortcut_bubble_shown_count";

#if BUILDFLAG(ENABLE_PLUGINS)
// Which plugins have been whitelisted manually by the user.
const char kContentSettingsPluginWhitelist[] =
    "profile.content_settings.plugin_whitelist";
#endif

// Double that indicates the default zoom level.
const char kPartitionDefaultZoomLevel[] = "partition.default_zoom_level";

// Dictionary that maps hostnames to zoom levels.  Hosts not in this pref will
// be displayed at the default zoom level.
const char kPartitionPerHostZoomLevels[] = "partition.per_host_zoom_levels";

#if !defined(OS_ANDROID)
const char kPinnedTabs[] = "pinned_tabs";
#endif

// Preference to disable 3D APIs (WebGL, Pepper 3D).
const char kDisable3DAPIs[] = "disable_3d_apis";

const char kEnableDeprecatedWebPlatformFeatures[] =
    "enable_deprecated_web_platform_features";

// Whether to enable hyperlink auditing ("<a ping>").
const char kEnableHyperlinkAuditing[] = "enable_a_ping";

// Whether to enable sending referrers.
const char kEnableReferrers[] = "enable_referrers";

// Whether to send the DNT header.
const char kEnableDoNotTrack[] = "enable_do_not_track";

// GL_VENDOR string.
const char kGLVendorString[] = "gl_vendor_string";

// GL_RENDERER string.
const char kGLRendererString[] = "gl_renderer_string";

// GL_VERSION string.
const char kGLVersionString[] = "gl_version_string";

// Boolean that specifies whether to import the form data for autofill from the
// default browser on first run.
const char kImportAutofillFormData[] = "import_autofill_form_data";

// Boolean that specifies whether to import bookmarks from the default browser
// on first run.
const char kImportBookmarks[] = "import_bookmarks";

// Boolean that specifies whether to import the browsing history from the
// default browser on first run.
const char kImportHistory[] = "import_history";

// Boolean that specifies whether to import the homepage from the default
// browser on first run.
const char kImportHomepage[] = "import_home_page";

// Boolean that specifies whether to import the saved passwords from the default
// browser on first run.
const char kImportSavedPasswords[] = "import_saved_passwords";

// Boolean that specifies whether to import the search engine from the default
// browser on first run.
const char kImportSearchEngine[] = "import_search_engine";

// Prefs used to remember selections in the "Import data" dialog on the settings
// page (chrome://settings/importData).
const char kImportDialogAutofillFormData[] = "import_dialog_autofill_form_data";
const char kImportDialogBookmarks[] = "import_dialog_bookmarks";
const char kImportDialogHistory[] = "import_dialog_history";
const char kImportDialogSavedPasswords[] = "import_dialog_saved_passwords";
const char kImportDialogSearchEngine[] = "import_dialog_search_engine";

// Profile avatar and name
const char kProfileAvatarIndex[] = "profile.avatar_index";
const char kProfileName[] = "profile.name";
// Whether a profile is using a default avatar name (eg. Pickles or Person 1)
// because it was randomly assigned at profile creation time.
const char kProfileUsingDefaultName[] = "profile.using_default_name";
// Whether a profile is using an avatar without having explicitely chosen it
// (i.e. was assigned by default by legacy profile creation).
const char kProfileUsingDefaultAvatar[] = "profile.using_default_avatar";
const char kProfileUsingGAIAAvatar[] = "profile.using_gaia_avatar";

// The supervised user ID.
const char kSupervisedUserId[] = "profile.managed_user_id";

// 64-bit integer serialization of the base::Time when the user's GAIA info
// was last updated.
const char kProfileGAIAInfoUpdateTime[] = "profile.gaia_info_update_time";

// The URL from which the GAIA profile picture was downloaded. This is cached to
// prevent the same picture from being downloaded multiple times.
const char kProfileGAIAInfoPictureURL[] = "profile.gaia_info_picture_url";

// Integer that specifies the number of times that we have shown the upgrade
// tutorial card in the avatar menu bubble.
const char kProfileAvatarTutorialShown[] =
    "profile.avatar_bubble_tutorial_shown";

// Indicates if we've already shown a notification that high contrast
// mode is on, recommending high-contrast extensions and themes.
const char kInvertNotificationShown[] = "invert_notification_version_2_shown";

// Boolean controlling whether printing is enabled.
const char kPrintingEnabled[] = "printing.enabled";

// Boolean controlling whether print preview is disabled.
const char kPrintPreviewDisabled[] = "printing.print_preview_disabled";

// A pref holding the value of the policy used to control default destination
// selection in the Print Preview. See DefaultPrinterSelection policy.
const char kPrintPreviewDefaultDestinationSelectionRules[] =
    "printing.default_destination_selection_rules";

#if defined(OS_CHROMEOS)
// List of all printers that the user has configured.
const char kPrintingDevices[] = "printing.devices";

// List of printers configured by policy.
const char kRecommendedNativePrinters[] =
    "native_printing.recommended_printers";
#endif  // OS_CHROMEOS

// An integer pref specifying the fallback behavior for sites outside of content
// packs. One of:
// 0: Allow (does nothing)
// 1: Warn.
// 2: Block.
const char kDefaultSupervisedUserFilteringBehavior[] =
    "profile.managed.default_filtering_behavior";

// Whether this user is permitted to create supervised users.
const char kSupervisedUserCreationAllowed[] =
    "profile.managed_user_creation_allowed";

// List pref containing the users supervised by this user.
const char kSupervisedUsers[] = "profile.managed_users";

// List pref containing the extension ids which are not allowed to send
// notifications to the message center.
const char kMessageCenterDisabledExtensionIds[] =
    "message_center.disabled_extension_ids";

// List pref containing the system component ids which are not allowed to send
// notifications to the message center.
const char kMessageCenterDisabledSystemComponentIds[] =
    "message_center.disabled_system_component_ids";

// Boolean pref indicating the Chrome Now welcome notification was dismissed
// by the user. Syncable.
// Note: This is now read-only. The welcome notification writes the _local
// version, below.
const char kWelcomeNotificationDismissed[] =
    "message_center.welcome_notification_dismissed";

// Boolean pref indicating the Chrome Now welcome notification was dismissed
// by the user on this machine.
const char kWelcomeNotificationDismissedLocal[] =
    "message_center.welcome_notification_dismissed_local";

// Boolean pref indicating the welcome notification was previously popped up.
const char kWelcomeNotificationPreviouslyPoppedUp[] =
    "message_center.welcome_notification_previously_popped_up";

// Integer pref containing the expiration timestamp of the welcome notification.
const char kWelcomeNotificationExpirationTimestamp[] =
    "message_center.welcome_notification_expiration_timestamp";

// Boolean pref that determines whether the user can enter fullscreen mode.
// Disabling fullscreen mode also makes kiosk mode unavailable on desktop
// platforms.
const char kFullscreenAllowed[] = "fullscreen.allowed";

// Enable notifications for new devices on the local network that can be
// registered to the user's account, e.g. Google Cloud Print printers.
const char kLocalDiscoveryNotificationsEnabled[] =
    "local_discovery.notifications_enabled";

#if defined(OS_ANDROID)
// Enable vibration for web notifications.
const char kNotificationsVibrateEnabled[] = "notifications.vibrate_enabled";

// Cached information about GPU driver.
const char kGLExtensionsString[] = "gl_extensions_string";
const char kGpuDriverInfoMaxSamples[] = "gpu_driver_info_max_samples";
const char kGpuDriverInfoResetNotificationStrategy[] =
    "gpu_driver_info_reset_notification_strategy";
const char kGpuDriverInfoShaderVersion[] = "gpu_driver_info_shader_version";
const char kGpuDriverInfoBuildFingerPrint[] =
    "gpu_driver_info_build_finder_print";
#endif

// Maps from app ids to origin + Service Worker registration ID.
const char kPushMessagingAppIdentifierMap[] =
    "gcm.push_messaging_application_id_map";

// A string like "com.chrome.macosx" that should be used as the GCM category
// when an app_id is sent as a subtype instead of as a category.
const char kGCMProductCategoryForSubtypes[] =
    "gcm.product_category_for_subtypes";

// Whether a user is allowed to use Easy Unlock.
const char kEasyUnlockAllowed[] = "easy_unlock.allowed";

// Whether Easy Unlock is enabled.
const char kEasyUnlockEnabled[] = "easy_unlock.enabled";

// Preference storing Easy Unlock pairing data.
const char kEasyUnlockPairing[] = "easy_unlock.pairing";

// Whether close proximity between the remote and the local device is required
// in order to use Easy Unlock.
const char kEasyUnlockProximityRequired[] = "easy_unlock.proximity_required";

#if BUILDFLAG(ENABLE_EXTENSIONS)
// Used to indicate whether or not the toolbar redesign bubble has been shown
// and acknowledged, and the last time the bubble was shown.
const char kToolbarIconSurfacingBubbleAcknowledged[] =
    "toolbar_icon_surfacing_bubble_acknowledged";
const char kToolbarIconSurfacingBubbleLastShowTime[] =
    "toolbar_icon_surfacing_bubble_show_time";
#endif

#if BUILDFLAG(ENABLE_WEBRTC)
// Whether WebRTC should bind to individual NICs to explore all possible routing
// options. Default is true. This has become obsoleted and replaced by
// kWebRTCIPHandlingPolicy. TODO(guoweis): Remove this at M50.
const char kWebRTCMultipleRoutesEnabled[] = "webrtc.multiple_routes_enabled";
// Whether WebRTC should use non-proxied UDP. If false, WebRTC will not send UDP
// unless it goes through a proxy (i.e RETURN when it's available).  If no UDP
// proxy is configured, it will not send UDP.  If true, WebRTC will send UDP
// regardless of whether or not a proxy is configured. TODO(guoweis): Remove
// this at M50.
const char kWebRTCNonProxiedUdpEnabled[] =
    "webrtc.nonproxied_udp_enabled";
// Define the IP handling policy override that WebRTC should follow. When not
// set, it defaults to "default".
const char kWebRTCIPHandlingPolicy[] = "webrtc.ip_handling_policy";
// Define range of UDP ports allowed to be used by WebRTC PeerConnections.
const char kWebRTCUDPPortRange[] = "webrtc.udp_port_range";
#endif

#if !defined(OS_ANDROID)
// Whether or not this profile has been shown the Welcome page.
const char kHasSeenWelcomePage[] = "browser.has_seen_welcome_page";
#endif

#if defined(OS_WIN)
// Whether or not this profile has been shown the Win10 promo page.
const char kHasSeenWin10PromoPage[] = "browser.has_seen_win10_promo_page";
#endif

// *************** LOCAL STATE ***************
// These are attached to the machine/installation

// Directory of the last profile used.
const char kProfileLastUsed[] = "profile.last_used";

// List of directories of the profiles last active.
const char kProfilesLastActive[] = "profile.last_active_profiles";

// Total number of profiles created for this Chrome build. Used to tag profile
// directories.
const char kProfilesNumCreated[] = "profile.profiles_created";

// String containing the version of Chrome that the profile was created by.
// If profile was created before this feature was added, this pref will default
// to "1.0.0.0".
const char kProfileCreatedByVersion[] = "profile.created_by_version";

// A map of profile data directory to cached information. This cache can be
// used to display information about profiles without actually having to load
// them.
const char kProfileInfoCache[] = "profile.info_cache";

// A list of profile paths that should be deleted on shutdown. The deletion does
// not happen if the browser crashes, so we remove the profile on next start.
const char kProfilesDeleted[] = "profiles.profiles_deleted";

// Deprecated preference for metric / crash reporting on Android. Use
// kMetricsReportingEnabled instead.
#if defined(OS_ANDROID)
const char kCrashReportingEnabled[] =
    "user_experience_metrics_crash.reporting_enabled";
#endif  // defined(OS_ANDROID)

// This is the location of a list of dictionaries of plugin stability stats.
const char kStabilityPluginStats[] =
    "user_experience_metrics.stability.plugin_stats2";

// On Chrome OS, total number of non-Chrome user process crashes
// since the last report.
const char kStabilityOtherUserCrashCount[] =
    "user_experience_metrics.stability.other_user_crash_count";

// On Chrome OS, total number of kernel crashes since the last report.
const char kStabilityKernelCrashCount[] =
    "user_experience_metrics.stability.kernel_crash_count";

// On Chrome OS, total number of unclean system shutdowns since the
// last report.
const char kStabilitySystemUncleanShutdownCount[] =
    "user_experience_metrics.stability.system_unclean_shutdowns";

#if defined(OS_ANDROID)
// Activity type that is currently in the foreground for the UMA session.
// Uses the ActivityTypeIds::Type enum.
const char kStabilityForegroundActivityType[] =
    "user_experience_metrics.stability.current_foreground_activity_type";

// Tracks which Activities were launched during the last session.
// See |metrics_service_android.cc| for its usage.
const char kStabilityLaunchedActivityFlags[] =
    "user_experience_metrics.stability.launched_activity_flags";

// List pref: Counts how many times each Activity was launched.
// Indexed into by ActivityTypeIds::Type.
const char kStabilityLaunchedActivityCounts[] =
    "user_experience_metrics.stability.launched_activity_counts";

// List pref: Counts how many times each Activity type was in the foreground
// when a UMA session failed to be shut down properly.
// Indexed into by ActivityTypeIds::Type.
const char kStabilityCrashedActivityCounts[] =
    "user_experience_metrics.stability.crashed_activity_counts";
#endif  // defined(OS_ANDROID)

// The keys below are used for the dictionaries in the
// kStabilityPluginStats list.
const char kStabilityPluginName[] = "name";
const char kStabilityPluginLaunches[] = "launches";
const char kStabilityPluginInstances[] = "instances";
const char kStabilityPluginCrashes[] = "crashes";
const char kStabilityPluginLoadingErrors[] = "loading_errors";

// String containing the version of Chrome for which Chrome will not prompt the
// user about setting Chrome as the default browser.
const char kBrowserSuppressDefaultBrowserPrompt[] =
    "browser.suppress_default_browser_prompt_for_version";

// A collection of position, size, and other data relating to the browser
// window to restore on startup.
const char kBrowserWindowPlacement[] = "browser.window_placement";

// Browser window placement for popup windows.
const char kBrowserWindowPlacementPopup[] = "browser.window_placement_popup";

// A collection of position, size, and other data relating to the task
// manager window to restore on startup.
const char kTaskManagerWindowPlacement[] = "task_manager.window_placement";

// The most recent stored column visibility of the task manager table to be
// restored on startup.
const char kTaskManagerColumnVisibility[] = "task_manager.column_visibility";

// A boolean indicating if ending processes are enabled or disabled by policy.
const char kTaskManagerEndProcessEnabled[] = "task_manager.end_process_enabled";

// A collection of position, size, and other data relating to app windows to
// restore on startup.
const char kAppWindowPlacement[] = "browser.app_window_placement";

// String which specifies where to download files to by default.
const char kDownloadDefaultDirectory[] = "download.default_directory";

// Boolean that records if the download directory was changed by an
// upgrade a unsafe location to a safe location.
const char kDownloadDirUpgraded[] = "download.directory_upgrade";

#if defined(OS_WIN) || defined(OS_LINUX) || defined(OS_MACOSX)
const char kOpenPdfDownloadInSystemReader[] =
    "download.open_pdf_in_system_reader";
#endif

// String which specifies where to save html files to by default.
const char kSaveFileDefaultDirectory[] = "savefile.default_directory";

// The type used to save the page. See the enum SavePackage::SavePackageType in
// the chrome/browser/download/save_package.h for the possible values.
const char kSaveFileType[] = "savefile.type";

// String which specifies the last directory that was chosen for uploading
// or opening a file.
const char kSelectFileLastDirectory[] = "selectfile.last_directory";

// Boolean that specifies if file selection dialogs are shown.
const char kAllowFileSelectionDialogs[] = "select_file_dialogs.allowed";

// Map of default tasks, associated by MIME type.
const char kDefaultTasksByMimeType[] =
    "filebrowser.tasks.default_by_mime_type";

// Map of default tasks, associated by file suffix.
const char kDefaultTasksBySuffix[] =
    "filebrowser.tasks.default_by_suffix";

// Extensions which should be opened upon completion.
const char kDownloadExtensionsToOpen[] = "download.extensions_to_open";

// Dictionary of schemes used by the external protocol handler.
// The value is true if the scheme must be ignored.
const char kExcludedSchemes[] = "protocol_handler.excluded_schemes";

// Integer that specifies the index of the tab the user was on when they
// last visited the options window.
const char kOptionsWindowLastTabIndex[] = "options_window.last_tab_index";

// Integer that specifies if the first run bubble should be shown.
// This preference is only registered by the first-run procedure.
const char kShowFirstRunBubbleOption[] = "show-first-run-bubble-option";

// String containing the last known intranet redirect URL, if any.  See
// intranet_redirect_detector.h for more information.
const char kLastKnownIntranetRedirectOrigin[] = "browser.last_redirect_origin";

// An enum value of how the browser was shut down (see browser_shutdown.h).
const char kShutdownType[] = "shutdown.type";
// Number of processes that were open when the user shut down.
const char kShutdownNumProcesses[] = "shutdown.num_processes";
// Number of processes that were shut down using the slow path.
const char kShutdownNumProcessesSlow[] = "shutdown.num_processes_slow";

// Whether to restart the current Chrome session automatically as the last thing
// before shutting everything down.
const char kRestartLastSessionOnShutdown[] = "restart.last.session.on.shutdown";

#if !defined(OS_ANDROID)
// Boolean that specifies whether or not showing the unsupported OS warning is
// suppressed. False by default. Controlled by the SuppressUnsupportedOSWarning
// policy setting.
const char kSuppressUnsupportedOSWarning[] =
    "browser.suppress_unsupported_os_warning";

// Set before autorestarting Chrome, cleared on clean exit.
const char kWasRestarted[] = "was.restarted";
#endif

// Whether Extensions are enabled.
const char kDisableExtensions[] = "extensions.disabled";

// Whether the plugin finder that lets you install missing plugins is enabled.
const char kDisablePluginFinder[] = "plugins.disable_plugin_finder";

// Customized app page names that appear on the New Tab Page.
const char kNtpAppPageNames[] = "ntp.app_page_names";

// Keeps track of which sessions are collapsed in the Other Devices menu.
const char kNtpCollapsedForeignSessions[] = "ntp.collapsed_foreign_sessions";

#if defined(OS_ANDROID)
// Keeps track of recently closed tabs collapsed state in the Other Devices
// menu.
const char kNtpCollapsedRecentlyClosedTabs[] =
    "ntp.collapsed_recently_closed_tabs";

// Keeps track of snapshot documents collapsed state in the Other Devices menu.
const char kNtpCollapsedSnapshotDocument[] = "ntp.collapsed_snapshot_document";

// Keeps track of sync promo collapsed state in the Other Devices menu.
const char kNtpCollapsedSyncPromo[] = "ntp.collapsed_sync_promo";

// Tracks whether we should show notifications related to content suggestions.
const char kContentSuggestionsNotificationsEnabled[] =
    "ntp.content_suggestions.notifications.enabled";

// Tracks how many notifications the user has ignored, so we can tell when we
// should stop showing them.
const char kContentSuggestionsConsecutiveIgnoredPrefName[] =
    "ntp.content_suggestions.notifications.consecutive_ignored";

// Tracks how many notifications have been sent today, and what day "today" is,
// as an integer YYYYMMDD, in wall time in the local timezone.
// If sent_day changes, sent_count is reset to 0. Allows limiting per-day
// notification count.
extern const char kContentSuggestionsNotificationsSentDay[] =
    "ntp.content_suggestions.notifications.sent_day";
extern const char kContentSuggestionsNotificationsSentCount[] =
    "ntp.content_suggestions.notifications.sent_count";
#endif  // defined(OS_ANDROID)

// Which page should be visible on the new tab page v4
const char kNtpShownPage[] = "ntp.shown_page";

// A private RSA key for ADB handshake.
const char kDevToolsAdbKey[] = "devtools.adb_key";

const char kDevToolsDisabled[] = "devtools.disabled";

// Determines whether devtools should be discovering usb devices for
// remote debugging at chrome://inspect.
const char kDevToolsDiscoverUsbDevicesEnabled[] =
    "devtools.discover_usb_devices";

// Maps of files edited locally using DevTools.
const char kDevToolsEditedFiles[] = "devtools.edited_files";

// List of file system paths added in DevTools.
const char kDevToolsFileSystemPaths[] = "devtools.file_system_paths";

// A boolean specifying whether port forwarding should be enabled.
const char kDevToolsPortForwardingEnabled[] =
    "devtools.port_forwarding_enabled";

// A boolean specifying whether default port forwarding configuration has been
// set.
const char kDevToolsPortForwardingDefaultSet[] =
    "devtools.port_forwarding_default_set";

// A dictionary of port->location pairs for port forwarding.
const char kDevToolsPortForwardingConfig[] = "devtools.port_forwarding_config";

// A boolean specifying whether or not Chrome will scan for available remote
// debugging targets.
const char kDevToolsDiscoverTCPTargetsEnabled[] =
    "devtools.discover_tcp_targets";

// A list of strings representing devtools target discovery servers.
const char kDevToolsTCPDiscoveryConfig[] = "devtools.tcp_discovery_config";

// A dictionary with generic DevTools settings.
const char kDevToolsPreferences[] = "devtools.preferences";

#if defined(OS_ANDROID)
// A boolean specifying whether remote dev tools debugging is enabled.
const char kDevToolsRemoteEnabled[] = "devtools.remote_enabled";
#endif

// Local hash of authentication password, used for off-line authentication
// when on-line authentication is not available.
const char kGoogleServicesPasswordHash[] = "google.services.password_hash";

#if !defined(OS_ANDROID)
// Tracks the number of times that we have shown the sign in promo at startup.
const char kSignInPromoStartupCount[] = "sync_promo.startup_count";

// Boolean tracking whether the user chose to skip the sign in promo.
const char kSignInPromoUserSkipped[] = "sync_promo.user_skipped";

// Boolean that specifies if the sign in promo is allowed to show on first run.
// This preference is specified in the master preference file to suppress the
// sign in promo for some installations.
const char kSignInPromoShowOnFirstRunAllowed[] =
    "sync_promo.show_on_first_run_allowed";

// Boolean that specifies if we should show a bubble in the new tab page.
// The bubble is used to confirm that the user is signed into sync.
const char kSignInPromoShowNTPBubble[] = "sync_promo.show_ntp_bubble";
#endif

#if !defined(OS_CHROMEOS) && !defined(OS_ANDROID)
// Boolean tracking whether the user chose to opt out of the x-device promo.
const char kCrossDevicePromoOptedOut[] = "x_device_promo.opted_out";

// Boolean tracking whether the x-device promo should be shown.
const char kCrossDevicePromoShouldBeShown[] = "x_device_promo.should_be_shown";

// Int64, representing the time when we first observed a single GAIA account in
// the cookie. If the most recent observation does not contain exactly one
// account, this pref does not exist.
const char kCrossDevicePromoObservedSingleAccountCookie[] =
    "x_device_promo.single_account_observed";

// Int64, representing the time to next call the ListDevices endpoint.
const char kCrossDevicePromoNextFetchListDevicesTime[] =
    "x_device_promo.next_list_devices_fetch";

// Int containing the number of other devices where the profile's account syncs.
const char kCrossDevicePromoNumDevices[] = "x_device_promo.num_devices";

// Int64, representing the time when we last saw activity on another device.
const char kCrossDevicePromoLastDeviceActiveTime[] =
    "x_device_promo.last_device_active_time";
#endif

// Create web application shortcut dialog preferences.
const char kWebAppCreateOnDesktop[] = "browser.web_app.create_on_desktop";
const char kWebAppCreateInAppsMenu[] = "browser.web_app.create_in_apps_menu";
const char kWebAppCreateInQuickLaunchBar[] =
    "browser.web_app.create_in_quick_launch_bar";

// Dictionary that maps Geolocation network provider server URLs to
// corresponding access token.
const char kGeolocationAccessToken[] = "geolocation.access_token";

// Boolean that specifies whether to enable the Google Now Launcher extension.
// Note: This is not the notifications component gated by ENABLE_GOOGLE_NOW.
const char kGoogleNowLauncherEnabled[] = "google_now_launcher.enabled";

// The default audio capture device used by the Media content setting.
const char kDefaultAudioCaptureDevice[] = "media.default_audio_capture_device";

// The default video capture device used by the Media content setting.
const char kDefaultVideoCaptureDevice[] = "media.default_video_capture_Device";

// The salt used for creating random MediaSource IDs.
const char kMediaDeviceIdSalt[] = "media.device_id_salt";

// The last used printer and its settings.
const char kPrintPreviewStickySettings[] =
    "printing.print_preview_sticky_settings";

// The list of BackgroundContents that should be loaded when the browser
// launches.
const char kRegisteredBackgroundContents[] = "background_contents.registered";

#if defined(OS_WIN)
// The "major.minor" OS version for which the welcome page was last shown.
const char kLastWelcomedOSVersion[] = "browser.last_welcomed_os_version";

// Boolean that specifies whether or not showing the welcome page following an
// OS upgrade is enabled. True by default. May be set by master_preferences or
// overridden by the WelcomePageOnOSUpgradeEnabled policy setting.
const char kWelcomePageOnOSUpgradeEnabled[] =
    "browser.welcome_page_on_os_upgrade_enabled";
#endif

// String that lists supported HTTP authentication schemes.
const char kAuthSchemes[] = "auth.schemes";

// Boolean that specifies whether to disable CNAME lookups when generating
// Kerberos SPN.
const char kDisableAuthNegotiateCnameLookup[] =
    "auth.disable_negotiate_cname_lookup";

// Boolean that specifies whether to include the port in a generated Kerberos
// SPN.
const char kEnableAuthNegotiatePort[] = "auth.enable_negotiate_port";

// Whitelist containing servers for which Integrated Authentication is enabled.
const char kAuthServerWhitelist[] = "auth.server_whitelist";

// Whitelist containing servers Chrome is allowed to do Kerberos delegation
// with.
const char kAuthNegotiateDelegateWhitelist[] =
    "auth.negotiate_delegate_whitelist";

// String that specifies the name of a custom GSSAPI library to load.
const char kGSSAPILibraryName[] = "auth.gssapi_library_name";

// String that specifies the Android account type to use for Negotiate
// authentication.
const char kAuthAndroidNegotiateAccountType[] =
    "auth.android_negotiate_account_type";

// Boolean that specifies whether to allow basic auth prompting on cross-
// domain sub-content requests.
const char kAllowCrossOriginAuthPrompt[] = "auth.allow_cross_origin_prompt";

// Boolean that specifies whether the built-in asynchronous DNS client is used.
const char kBuiltInDnsClientEnabled[] = "async_dns.enabled";

// A pref holding the value of the policy used to explicitly allow or deny
// access to audio capture devices.  When enabled or not set, the user is
// prompted for device access.  When disabled, access to audio capture devices
// is not allowed and no prompt will be shown.
// See also kAudioCaptureAllowedUrls.
const char kAudioCaptureAllowed[] = "hardware.audio_capture_enabled";
// Holds URL patterns that specify URLs that will be granted access to audio
// capture devices without prompt.
const char kAudioCaptureAllowedUrls[] = "hardware.audio_capture_allowed_urls";

// A pref holding the value of the policy used to explicitly allow or deny
// access to video capture devices.  When enabled or not set, the user is
// prompted for device access.  When disabled, access to video capture devices
// is not allowed and no prompt will be shown.
const char kVideoCaptureAllowed[] = "hardware.video_capture_enabled";
// Holds URL patterns that specify URLs that will be granted access to video
// capture devices without prompt.
const char kVideoCaptureAllowedUrls[] = "hardware.video_capture_allowed_urls";

// A boolean pref that controls the enabled-state of hotword search voice
// trigger.
const char kHotwordSearchEnabled[] = "hotword.search_enabled_2";

// A boolean pref that controls the enabled-state of hotword search voice
// trigger from any screen.
const char kHotwordAlwaysOnSearchEnabled[] = "hotword.always_on_search_enabled";

// A boolean pref that indicates whether the hotword always-on notification
// has been seen already.
const char kHotwordAlwaysOnNotificationSeen[] =
    "hotword.always_on_notification_seen";

// A boolean pref that controls whether the sound of "Ok, Google" plus a few
// seconds of audio data before and the spoken query are sent back to be stored
// in a user's Voice & Audio Activity. Updated whenever the user opens
// chrome://settings and also polled for every 24 hours.
const char kHotwordAudioLoggingEnabled[] = "hotword.audio_logging_enabled";

// A string holding the locale information under which Hotword was installed.
// It is used for comparison since the hotword voice search trigger must be
// reinstalled to handle a new language.
const char kHotwordPreviousLanguage[] = "hotword.previous_language";

#if defined(OS_CHROMEOS)
// Dictionary for transient storage of settings that should go into device
// settings storage before owner has been assigned.
const char kDeviceSettingsCache[] = "signed_settings_cache";

// The hardware keyboard layout of the device. This should look like
// "xkb:us::eng".
const char kHardwareKeyboardLayout[] = "intl.hardware_keyboard";

// An integer pref which shows number of times carrier deal promo
// notification has been shown to user.
const char kCarrierDealPromoShown[] =
    "settings.internet.mobile.carrier_deal_promo_shown";

// A boolean pref of the auto-enrollment decision. Its value is only valid if
// it's not the default value; otherwise, no auto-enrollment decision has been
// made yet.
const char kShouldAutoEnroll[] = "ShouldAutoEnroll";

// An integer pref with the maximum number of bits used by the client in a
// previous auto-enrollment request. If the client goes through an auto update
// during OOBE and reboots into a version of the OS with a larger maximum
// modulus, then it will retry auto-enrollment using the updated value.
const char kAutoEnrollmentPowerLimit[] = "AutoEnrollmentPowerLimit";

// The local state pref that stores device activity times before reporting
// them to the policy server.
const char kDeviceActivityTimes[] = "device_status.activity_times";

// A pref holding the value of the policy used to disable mounting of external
// storage for the user.
const char kExternalStorageDisabled[] = "hardware.external_storage_disabled";

// A pref holding the value of the policy used to limit mounting of external
// storage to read-only mode for the user.
const char kExternalStorageReadOnly[] = "hardware.external_storage_read_only";

// Copy of owner swap mouse buttons option to use on login screen.
const char kOwnerPrimaryMouseButtonRight[] = "owner.mouse.primary_right";

// Copy of owner tap-to-click option to use on login screen.
const char kOwnerTapToClickEnabled[] = "owner.touchpad.enable_tap_to_click";

// The length of device uptime after which an automatic reboot is scheduled,
// expressed in seconds.
const char kUptimeLimit[] = "automatic_reboot.uptime_limit";

// Whether an automatic reboot should be scheduled when an update has been
// applied and a reboot is required to complete the update process.
const char kRebootAfterUpdate[] = "automatic_reboot.reboot_after_update";

// An any-api scoped refresh token for enterprise-enrolled devices.  Allows
// for connection to Google APIs when the user isn't logged in.  Currently used
// for for getting a cloudprint scoped token to allow printing in Guest mode,
// Public Accounts and kiosks.
const char kDeviceRobotAnyApiRefreshToken[] =
    "device_robot_refresh_token.any-api";

// Device requisition for enterprise enrollment.
const char kDeviceEnrollmentRequisition[] = "enrollment.device_requisition";

// Whether to automatically start the enterprise enrollment step during OOBE.
const char kDeviceEnrollmentAutoStart[] = "enrollment.auto_start";

// Whether the user may exit enrollment.
const char kDeviceEnrollmentCanExit[] = "enrollment.can_exit";

// DM token fetched from the DM server during enrollment. Stored for Active
// Directory devices only.
const char kDeviceDMToken[] = "device_dm_token";

// How many times HID detection OOBE dialog was shown.
const char kTimesHIDDialogShown[] = "HIDDialog.shown_how_many_times";

// Dictionary of per-user last input method (used at login screen). Note that
// the pref name is UsersLRUInputMethods for compatibility with previous
// versions.
const char kUsersLastInputMethod[] = "UsersLRUInputMethod";

// A dictionary pref of the echo offer check flag. It sets offer info when
// an offer is checked.
const char kEchoCheckedOffers[] = "EchoCheckedOffers";

// Key name of a dictionary in local state to store cached multiprofle user
// behavior policy value.
const char kCachedMultiProfileUserBehavior[] = "CachedMultiProfileUserBehavior";

// A string pref with initial locale set in VPD or manifest.
const char kInitialLocale[] = "intl.initial_locale";

// A boolean pref of the OOBE complete flag (first OOBE part before login).
const char kOobeComplete[] = "OobeComplete";

// The name of the screen that has to be shown if OOBE has been interrupted.
const char kOobeScreenPending[] = "OobeScreenPending";

// If material design OOBE should be selected if OOBE has been interrupted.
const char kOobeMdMode[] = "OobeMdModeEnabled";

// A boolean pref to indicate if an eligible controller (either a Chrome OS
// device, or an Android device) is detected during bootstrapping or
// shark/remora setup process. A controller can help the device go through OOBE
// and get enrolled into a domain automatically.
const char kOobeControllerDetected[] = "OobeControllerDetected";

// A 64-bit signed integer pref (encoded as a string) which stores the time
// of the last OOBE-initiated update check not resulting in an update.
const char kOobeTimeOfLastUpdateCheckWithoutUpdate[] =
    "OobeTimeOfLastUpdateCheckWithoutUpdate";

// A boolean pref for whether the Goodies promotion webpage has been displayed,
// or otherwise disqualified for auto-display, on this device.
const char kCanShowOobeGoodiesPage[] = "CanShowOobeGoodiesPage";

// A boolean pref of the device registered flag (second part after first login).
const char kDeviceRegistered[] = "DeviceRegistered";

// Boolean pref to signal corrupted enrollment to force the device through
// enrollment recovery flow upon next boot.
const char kEnrollmentRecoveryRequired[] = "EnrollmentRecoveryRequired";

// List of usernames that used certificates pushed by policy before.
// This is used to prevent these users from joining multiprofile sessions.
const char kUsedPolicyCertificates[] = "policy.used_policy_certificates";

// A dictionary containing server-provided device state pulled form the cloud
// after recovery.
const char kServerBackedDeviceState[] = "server_backed_device_state";

// Customized wallpaper URL, which is already downloaded and scaled.
// The URL from this preference must never be fetched. It is compared to the
// URL from customization document to check if wallpaper URL has changed
// since wallpaper was cached.
const char kCustomizationDefaultWallpaperURL[] =
    "customization.default_wallpaper_url";

// System uptime, when last logout started.
// This is saved to file and cleared after chrome process starts.
const char kLogoutStartedLast[] = "chromeos.logout-started";

// The role of the device in the OOBE bootstrapping process. If it's a "slave"
// device, then it's eligible to be enrolled by a "master" device (which could
// be an Android app).
const char kIsBootstrappingSlave[] = "is_oobe_bootstrapping_slave";

// A preference that controlles Android status reporting.
const char kReportArcStatusEnabled[] = "arc.status_reporting_enabled";

// Dictionary indicating current network bandwidth throttling settings.
// Contains a boolean (is throttling enabled) and two integers (upload rate
// and download rate in kbits/s to throttle to)
const char kNetworkThrottlingEnabled[] = "net.throttling_enabled";

// Boolean prefs for the local status of the touchscreen.
const char kTouchscreenEnabledLocal[] = "events.touch_screen.enabled_local";

#endif  // defined(OS_CHROMEOS)

// Whether there is a Flash version installed that supports clearing LSO data.
const char kClearPluginLSODataEnabled[] = "browser.clear_lso_data_enabled";

// Whether we should show Pepper Flash-specific settings.
const char kPepperFlashSettingsEnabled[] =
    "browser.pepper_flash_settings_enabled";

// String which specifies where to store the disk cache.
const char kDiskCacheDir[] = "browser.disk_cache_dir";
// Pref name for the policy specifying the maximal cache size.
const char kDiskCacheSize[] = "browser.disk_cache_size";
// Pref name for the policy specifying the maximal media cache size.
const char kMediaCacheSize[] = "browser.media_cache_size";

// Specifies the release channel that the device should be locked to.
// Possible values: "stable-channel", "beta-channel", "dev-channel", or an
// empty string, in which case the value will be ignored.
// TODO(dubroy): This preference may not be necessary once
// http://crosbug.com/17015 is implemented and the update engine can just
// fetch the correct value from the policy.
const char kChromeOsReleaseChannel[] = "cros.system.releaseChannel";

const char kPerformanceTracingEnabled[] =
    "feedback.performance_tracing_enabled";

// Boolean indicating whether tabstrip uses stacked layout (on touch devices).
// Defaults to false.
const char kTabStripStackedLayout[] = "tab-strip-stacked-layout";

// Indicates that factory reset was requested from options page or reset screen.
const char kFactoryResetRequested[] = "FactoryResetRequested";

// Indicates that debugging features were requested from oobe screen.
const char kDebuggingFeaturesRequested[] = "DebuggingFeaturesRequested";

#if defined(OS_CHROMEOS)
// This setting starts periodic timezone refresh when not in user session.
// (user session is controlled by user profile preference
// kResolveTimezoneByGeolocation
const char kResolveDeviceTimezoneByGeolocation[] =
    "settings.resolve_device_timezone_by_geolocation";

// This is policy-controlled preference.
// It has values defined in policy enum
// SystemTimezoneAutomaticDetectionProto_AutomaticTimezoneDetectionType;
const char kSystemTimezoneAutomaticDetectionPolicy[] =
    "settings.resolve_device_timezone_by_geolocation_policy";
#endif  // defined(OS_CHROMEOS)

// Pref name for the policy controlling whether to enable Media Router.
const char kEnableMediaRouter[] = "media_router.enable_media_router";
#if !defined(OS_ANDROID)
// Pref name for the policy controlling whether to force the Cast icon to be
// shown in the toolbar/overflow menu.
const char kShowCastIconInToolbar[] = "media_router.show_cast_icon_in_toolbar";
#endif  // !defined(OS_ANDROID)

// *************** SERVICE PREFS ***************
// These are attached to the service process.

const char kCloudPrintRoot[] = "cloud_print";
const char kCloudPrintProxyEnabled[] = "cloud_print.enabled";
// The unique id for this instance of the cloud print proxy.
const char kCloudPrintProxyId[] = "cloud_print.proxy_id";
// The GAIA auth token for Cloud Print
const char kCloudPrintAuthToken[] = "cloud_print.auth_token";
// The email address of the account used to authenticate with the Cloud Print
// server.
const char kCloudPrintEmail[] = "cloud_print.email";
// Settings specific to underlying print system.
const char kCloudPrintPrintSystemSettings[] =
    "cloud_print.print_system_settings";
// A boolean indicating whether we should poll for print jobs when don't have
// an XMPP connection (false by default).
const char kCloudPrintEnableJobPoll[] = "cloud_print.enable_job_poll";
const char kCloudPrintRobotRefreshToken[] = "cloud_print.robot_refresh_token";
const char kCloudPrintRobotEmail[] = "cloud_print.robot_email";
// A boolean indicating whether we should connect to cloud print new printers.
const char kCloudPrintConnectNewPrinters[] =
    "cloud_print.user_settings.connectNewPrinters";
// A boolean indicating whether we should ping XMPP connection.
const char kCloudPrintXmppPingEnabled[] = "cloud_print.xmpp_ping_enabled";
// An int value indicating the average timeout between xmpp pings.
const char kCloudPrintXmppPingTimeout[] = "cloud_print.xmpp_ping_timeout_sec";
// Dictionary with settings stored by connector setup page.
const char kCloudPrintUserSettings[] = "cloud_print.user_settings";
// List of printers settings.
const char kCloudPrintPrinters[] = "cloud_print.user_settings.printers";
// A boolean indicating whether submitting jobs to Google Cloud Print is
// blocked by policy.
const char kCloudPrintSubmitEnabled[] = "cloud_print.submit_enabled";

// Preference to store proxy settings.
const char kMaxConnectionsPerProxy[] = "net.max_connections_per_proxy";

#if defined(OS_MACOSX)
// Set to true if the user removed our login item so we should not create a new
// one when uninstalling background apps.
const char kUserRemovedLoginItem[] = "background_mode.user_removed_login_item";

// Set to true if Chrome already created a login item, so there's no need to
// create another one.
const char kChromeCreatedLoginItem[] =
  "background_mode.chrome_created_login_item";

// Set to true once we've initialized kChromeCreatedLoginItem for the first
// time.
const char kMigratedLoginItemPref[] =
  "background_mode.migrated_login_item_pref";

// A boolean that tracks whether to show a notification when trying to quit
// while there are apps running.
const char kNotifyWhenAppsKeepChromeAlive[] =
    "apps.notify-when-apps-keep-chrome-alive";
#endif

// Set to true if background mode is enabled on this browser.
const char kBackgroundModeEnabled[] = "background_mode.enabled";

// Set to true if hardware acceleration mode is enabled on this browser.
const char kHardwareAccelerationModeEnabled[] =
  "hardware_acceleration_mode.enabled";

// Hardware acceleration mode from previous browser launch.
const char kHardwareAccelerationModePrevious[] =
  "hardware_acceleration_mode_previous";

// List of protocol handlers.
const char kRegisteredProtocolHandlers[] =
  "custom_handlers.registered_protocol_handlers";

// List of protocol handlers the user has requested not to be asked about again.
const char kIgnoredProtocolHandlers[] =
  "custom_handlers.ignored_protocol_handlers";

// List of protocol handlers registered by policy.
const char kPolicyRegisteredProtocolHandlers[] =
    "custom_handlers.policy.registered_protocol_handlers";

// List of protocol handlers the policy has requested to be ignored.
const char kPolicyIgnoredProtocolHandlers[] =
    "custom_handlers.policy.ignored_protocol_handlers";

// Whether user-specified handlers for protocols and content types can be
// specified.
const char kCustomHandlersEnabled[] = "custom_handlers.enabled";

// Integer that specifies the policy refresh rate for device-policy in
// milliseconds. Not all values are meaningful, so it is clamped to a sane range
// by the cloud policy subsystem.
const char kDevicePolicyRefreshRate[] = "policy.device_refresh_rate";

#if !defined(OS_ANDROID)
// A boolean where true means that the browser has previously attempted to
// enable autoupdate and failed, so the next out-of-date browser start should
// not prompt the user to enable autoupdate, it should offer to reinstall Chrome
// instead.
const char kAttemptedToEnableAutoupdate[] =
    "browser.attempted_to_enable_autoupdate";

// The next media gallery ID to assign.
const char kMediaGalleriesUniqueId[] = "media_galleries.gallery_id";

// A list of dictionaries, where each dictionary represents a known media
// gallery.
const char kMediaGalleriesRememberedGalleries[] =
    "media_galleries.remembered_galleries";
#endif  // !defined(OS_ANDROID)

#if defined(USE_AURA)
// |kShelfAlignment| and |kShelfAutoHideBehavior| have a local variant. The
// local variant is not synced and is used if set. If the local variant is not
// set its value is set from the synced value (once prefs have been
// synced). This gives a per-machine setting that is initialized from the last
// set value.
// These values are default on the machine but can be overridden by per-display
// values in kShelfPreferences (unless overridden by managed policy).
// String value corresponding to ash::Shell::ShelfAlignment.
const char kShelfAlignment[] = "shelf_alignment";
const char kShelfAlignmentLocal[] = "shelf_alignment_local";
// String value corresponding to ash::Shell::ShelfAutoHideBehavior.
const char kShelfAutoHideBehavior[] = "auto_hide_behavior";
const char kShelfAutoHideBehaviorLocal[] = "auto_hide_behavior_local";
// This value stores chrome icon's index in the launcher. This should be handled
// separately with app shortcut's index because of ShelfModel's backward
// compatibility. If we add chrome icon index to |kPinnedLauncherApps|, its
// index is also stored in the |kPinnedLauncherApp| pref. It may causes
// creating two chrome icons.
const char kShelfChromeIconIndex[] = "shelf_chrome_icon_index";
// Dictionary value that holds per-display preference of shelf alignment and
// auto-hide behavior. Key of the dictionary is the id of the display, and
// its value is a dictionary whose keys are kShelfAlignment and
// kShelfAutoHideBehavior.
const char kShelfPreferences[] = "shelf_preferences";

// Integer value in milliseconds indicating the length of time for which a
// confirmation dialog should be shown when the user presses the logout button.
// A value of 0 indicates that logout should happen immediately, without showing
// a confirmation dialog.
const char kLogoutDialogDurationMs[] = "logout_dialog_duration_ms";
const char kPinnedLauncherApps[] = "pinned_launcher_apps";
const char kPolicyPinnedLauncherApps[] = "policy_pinned_launcher_apps";
// Boolean value indicating whether to show a logout button in the ash tray.
const char kShowLogoutButtonInTray[] = "show_logout_button_in_tray";
#endif

#if defined(OS_WIN)
// Counts how many more times the 'profile on a network share' warning should be
// shown to the user before the next silence period.
const char kNetworkProfileWarningsLeft[] = "network_profile.warnings_left";
// Tracks the time of the last shown warning. Used to reset
// |network_profile.warnings_left| after a silence period.
const char kNetworkProfileLastWarningTime[] =
    "network_profile.last_warning_time";
#endif

#if defined(OS_CHROMEOS)
// The RLZ brand code, if enabled.
const char kRLZBrand[] = "rlz.brand";
// Whether RLZ pings are disabled.
const char kRLZDisabled[] = "rlz.disabled";
#endif

#if BUILDFLAG(ENABLE_APP_LIST)
// The directory in user data dir that contains the profile to be used with the
// app launcher.
const char kAppListProfile[] = "app_list.profile";

// The number of times the app launcher was launched since last ping and
// the time of the last ping.
const char kAppListLaunchCount[] = "app_list.launch_count";
const char kLastAppListLaunchPing[] = "app_list.last_launch_ping";

// The number of times the an app was launched from the app launcher since last
// ping and the time of the last ping.
const char kAppListAppLaunchCount[] = "app_list.app_launch_count";
const char kLastAppListAppLaunchPing[] = "app_list.last_app_launch_ping";

// A boolean that tracks whether the user has ever enabled the app launcher.
const char kAppLauncherHasBeenEnabled[] =
    "apps.app_launcher.has_been_enabled";

// An enum indicating how the app launcher was enabled. E.g., via webstore, app
// install, command line, etc. For UMA.
const char kAppListEnableMethod[] = "app_list.how_enabled";

// The time that the app launcher was enabled. Cleared when UMA is recorded.
const char kAppListEnableTime[] = "app_list.when_enabled";

// Keeps local state of app list while sync service is not available.
const char kAppListLocalState[] = "app_list.local_state";

// A boolean identifying if we should show the app launcher promo or not.
const char kShowAppLauncherPromo[] = "app_launcher.show_promo";

// A dictionary that tracks the Drive app to Chrome app mapping. The key is
// a Drive app id and the value is the corresponding Chrome app id. The pref
// is unsynable and used to track local mappings only.
const char kAppLauncherDriveAppMapping[] =
    "apps.app_launcher.drive_app_mapping";

// A list of Drive app ids that tracks the uninstallable Drive apps.
const char kAppLauncherUninstalledDriveApps[] =
    "apps.app_launcher.uninstalled_drive_apps";
#endif  // BUILDFLAG(ENABLE_APP_LIST)

#if defined(OS_WIN)
// If set, the user requested to launch the app with this extension id while
// in Metro mode, and then relaunched to Desktop mode to start it.
const char kAppLaunchForMetroRestart[] = "apps.app_launch_for_metro_restart";

// Set with |kAppLaunchForMetroRestart|, the profile whose loading triggers
// launch of the specified app when restarting Chrome in desktop mode.
const char kAppLaunchForMetroRestartProfile[] =
    "apps.app_launch_for_metro_restart_profile";
#endif

// An integer that is incremented whenever changes are made to app shortcuts.
// Increasing this causes all app shortcuts to be recreated.
const char kAppShortcutsVersion[] = "apps.shortcuts_version";

// How often the bubble has been shown.
const char kModuleConflictBubbleShown[] = "module_conflict.bubble_shown";

// A string pref for storing the salt used to compute the pepper device ID.
const char kDRMSalt[] = "settings.privacy.drm_salt";
// A boolean pref that enables the (private) pepper GetDeviceID() call and
// enables the use of remote attestation for content protection.
const char kEnableDRM[] = "settings.privacy.drm_enabled";

// An integer per-profile pref that signals if the watchdog extension is
// installed and active. We need to know if the watchdog extension active for
// ActivityLog initialization before the extension system is initialized.
const char kWatchdogExtensionActive[] =
    "profile.extensions.activity_log.num_consumers_active";

#if defined(OS_ANDROID)
// A list of partner bookmark rename/remove mappings.
// Each list item is a dictionary containing a "url", a "provider_title" and
// "mapped_title" entries, detailing the bookmark target URL (if any), the title
// given by the PartnerBookmarksProvider and either the user-visible renamed
// title or an empty string if the bookmark node was removed.
const char kPartnerBookmarkMappings[] = "partnerbookmarks.mappings";
#endif  // defined(OS_ANDROID)

// Whether DNS Quick Check is disabled in proxy resolution.
//
// This is a performance optimization for WPAD (Web Proxy
// Auto-Discovery) which places a 1 second timeout on resolving the
// DNS for PAC script URLs.
//
// It is on by default, but can be disabled via the Policy option
// "WPADQuickCheckEnbled". There is no other UI for changing this
// preference.
//
// For instance, if the DNS resolution for 'wpad' takes longer than 1
// second, auto-detection will give up and fallback to the next proxy
// configuration (which could be manually configured proxy server
// rules, or an implicit fallback to DIRECT connections).
const char kQuickCheckEnabled[] = "proxy.quick_check_enabled";

// Whether PAC scripts are given a stripped https:// URL (enabled), or
// the full URL for https:// (disabled).
//
// This is a security feature which is on by default, and prevents PAC
// scripts (which may have been sourced in an untrusted manner) from
// having access to data that is ordinarily protected by a TLS channel
// (i.e. the path and query components of an https:// URL).
//
// This preference is not exposed in the UI, but is overridable using
// a Policy (PacHttpsUrlStrippingEnabled), or using a commandline
// flag --unsafe-pac-url.
//
// The ability to turn off this security feature is not intended to be
// a long-lived feature, but rather an escape-hatch for enterprises
// while rolling out the change to PAC.
const char kPacHttpsUrlStrippingEnabled[] =
    "proxy.pac_https_url_stripping_enabled";

// Whether Guest Mode is enabled within the browser.
const char kBrowserGuestModeEnabled[] = "profile.browser_guest_enabled";

// Whether Adding a new Person is enabled within the user manager.
const char kBrowserAddPersonEnabled[] = "profile.add_person_enabled";

// Whether profile can be used before sign in.
const char kForceBrowserSignin[] = "profile.force_browser_signin";

// Device identifier used by Easy Unlock stored in local state. This id will be
// combined with a user id, before being registered with the CryptAuth server,
// so it can't correlate users on the same device.
const char kEasyUnlockDeviceId[] = "easy_unlock.device_id";

// A dictionary that maps user id to hardlock state.
const char kEasyUnlockHardlockState[] = "easy_unlock.hardlock_state";

// A dictionary that maps user id to public part of RSA key pair used by
// Easy Sign-in for the user.
const char kEasyUnlockLocalStateTpmKeys[] = "easy_unlock.public_tpm_keys";

// A dictionary in local state containing each user's Easy Unlock profile
// preferences, so they can be accessed outside of the user's profile. The value
// is a dictionary containing an entry for each user. Each user's entry mirrors
// their profile's Easy Unlock preferences.
const char kEasyUnlockLocalStateUserPrefs[] = "easy_unlock.user_prefs";

// Boolean that indicates whether elevation is needed to recover Chrome upgrade.
const char kRecoveryComponentNeedsElevation[] =
    "recovery_component.needs_elevation";

// A dictionary that maps from supervised user whitelist IDs to their properties
// (name and a list of clients that registered the whitelist).
const char kRegisteredSupervisedUserWhitelists[] =
    "supervised_users.whitelists";

#if BUILDFLAG(ENABLE_EXTENSIONS)
// Policy that indicates how to handle animated images.
const char kAnimationPolicy[] = "settings.a11y.animation_policy";
#endif

const char kBackgroundTracingLastUpload[] = "background_tracing.last_upload";

const char kAllowDinosaurEasterEgg[] =
    "allow_dinosaur_easter_egg";

#if defined(OS_ANDROID)
// Whether the update menu item was clicked. Used to facilitate logging whether
// Chrome was updated after the menu item is clicked.
const char kClickedUpdateMenuItem[] = "omaha.clicked_update_menu_item";
// The latest version of Chrome available when the user clicked on the update
// menu item.
const char kLatestVersionWhenClickedUpdateMenuItem[] =
    "omaha.latest_version_when_clicked_upate_menu_item";
#endif

// Whether or not the user has explicitly set the cloud services preference
// through the first run flow.
const char kMediaRouterCloudServicesPrefSet[] =
    "media_router.cloudservices.prefset";
// Whether or not the user has enabled cloud services with Media Router.
const char kMediaRouterEnableCloudServices[] =
    "media_router.cloudservices.enabled";
// Whether or not the Media Router first run flow has been acknowledged by the
// user.
const char kMediaRouterFirstRunFlowAcknowledged[] =
    "media_router.firstrunflow.acknowledged";
// A list of website origins on which the user has chosen to use tab mirroring.
const char kMediaRouterTabMirroringSources[] =
    "media_router.tab_mirroring_sources";

// The base64-encoded representation of the public key to use to validate origin
// trial token signatures.
const char kOriginTrialPublicKey[] = "origin_trials.public_key";

// A list of origin trial features to disable by policy.
const char kOriginTrialDisabledFeatures[] = "origin_trials.disabled_features";

// A list of origin trial tokens to disable by policy.
const char kOriginTrialDisabledTokens[] = "origin_trials.disabled_tokens";

// Policy that indicates the state of updates for the binary components.
const char kComponentUpdatesEnabled[] =
    "component_updates.component_updates_enabled";

#if defined(OS_ANDROID)
// The current level of backoff for showing the location settings dialog for the
// default search engine.
const char kLocationSettingsBackoffLevelDSE[] =
    "location_settings_backoff_level_dse";

// The current level of backoff for showing the location settings dialog for
// sites other than the default search engine.
const char kLocationSettingsBackoffLevelDefault[] =
    "location_settings_backoff_level_default";

// The next time the location settings dialog can be shown for the default
// search engine.
const char kLocationSettingsNextShowDSE[] = "location_settings_next_show_dse";

// The next time the location settings dialog can be shown for sites other than
// the default search engine.
const char kLocationSettingsNextShowDefault[] =
    "location_settings_next_show_default";

// Whether the search geolocation disclosure has been dismissed by the user.
const char kSearchGeolocationDisclosureDismissed[] =
    "search_geolocation_disclosure.dismissed";

// How many times the search geolocation disclosure has been shown.
const char kSearchGeolocationDisclosureShownCount[] =
    "search_geolocation_disclosure.shown_count";

// When the disclosure was shown last.
const char kSearchGeolocationDisclosureLastShowDate[] =
    "search_geolocation_disclosure.last_show_date";

// Whether the metrics for the state of geolocation pre-disclosure being shown
// have been recorded.
const char kSearchGeolocationPreDisclosureMetricsRecorded[] =
    "search_geolocation_pre_disclosure_metrics_recorded";

// Whether the metrics for the state of geolocation post-disclosure being shown
// have been recorded.
const char kSearchGeolocationPostDisclosureMetricsRecorded[] =
    "search_geolocation_post_disclosure_metrics_recorded";
#endif

// A dictionary which stores whether location access is enabled for the current
// default search engine, if it is the Google search engine.
const char kGoogleDSEGeolocationSetting[] = "google_dse_geolocation_setting";

// A dictionary of manifest URLs of Web Share Targets to a dictionary containing
// attributes of its share_target field found in its manifest. Each key in the
// dictionary is the name of the attribute, and the value is the corresponding
// value.
const char kWebShareVisitedTargets[] = "profile.web_share.visited_targets";

#if defined(OS_WIN)
// True if the user is eligible to recieve "desktop to iOS" promotion. This
// vlaue is set by a job that access the growth table to check users with iOS
// devices and phone recovery number and update the eligibility on chrome sync.
const char kIOSPromotionEligible[] = "ios.desktoptomobileeligible";

// True if the "desktop to iOS" promotion was successful, i.e. user installed
// the application and signed in on the iOS client after seeing the promotion
// and receiving the SMS.
const char kIOSPromotionDone[] = "ios.desktop_ios_promo_done";

// Index of the entry point that last initiated sending the SMS to the user for
// the "desktop to iOS" promotion (see DesktopIOSPromotion.IOSSigninReason
// histogram for details).
const char kIOSPromotionSMSEntryPoint[] =
    "ios.desktop_ios_promo_sms_entrypoint";

// Bit mask that represents the Indices of all the entry points shown to the
// user for "desktop to iOS" promotion. Each entry point is represented by
// 1<<entrypoint_value using the values from the Enum
// desktop_ios_promotion::PromotionEntryPoint.
const char kIOSPromotionShownEntryPoints[] =
    "ios.desktop_ios_promo_shown_entrypoints";

// Timestamp of the last "desktop to iOS" promotion last impression. If the
// user sends SMS on that impression then we deal with this timestamp as the
// SMS sending time because after sending the sms the user shouldn't see the
// promotion again (Accuracy to the minutes and seconds is not important).
const char kIOSPromotionLastImpression[] =
    "ios.desktop_ios_promo_last_impression";

// Integer that represents which variation of title and text of the
// "desktop to iOS" promotion was presented to the user.
const char kIOSPromotionVariationId[] = "ios.desktop_ios_promo_variation_id";

// Number of times user has seen the "desktop to iOS" save passwords bubble
// promotion.
const char kNumberSavePasswordsBubbleIOSPromoShown[] =
    "savepasswords_bubble_ios_promo_shown_count";

// True if the user has dismissed the "desktop to iOS" save passwords bubble
// promotion.
const char kSavePasswordsBubbleIOSPromoDismissed[] =
    "savepasswords_bubble_ios_promo_dismissed";

// Number of times the user has seen the "desktop to iOS" bookmarks bubble
// promotion.
const char kNumberBookmarksBubbleIOSPromoShown[] =
    "bookmarks_bubble_ios_promo_shown_count";

// True if the user has dismissed the "desktop to iOS" bookmarks bubble
// promotion.
const char kBookmarksBubbleIOSPromoDismissed[] =
    "bookmarks_bubble_ios_promo_dismissed";

// Number of times user has seen the "desktop to iOS" bookmarks foot note
// promotion.
const char kNumberBookmarksFootNoteIOSPromoShown[] =
    "bookmarks_footnote_ios_promo_shown_count";

// True if the user has dismissed the "desktop to iOS" bookmarks foot note
// promotion.
const char kBookmarksFootNoteIOSPromoDismissed[] =
    "bookmarks_footnote_ios_promo_dismissed";

// Number of times user has seen the "desktop to iOS" history page promotion.
const char kNumberHistoryPageIOSPromoShown[] =
    "history_page_ios_promo_shown_count";

// True if the user has dismissed the "desktop to iOS" history page promotion.
const char kHistoryPageIOSPromoDismissed[] = "history_page_ios_promo_dismissed";
#endif

// An integer that keeps track of prompt waves for the settings reset
// prompt. Users will be prompted to reset settings at most once per prompt wave
// for each setting that the prompt targets (default search, startup URLs and
// homepage). The value is obtained via a feature parameter. When the stored
// value is different from the feature parameter, a new prompt wave begins.
const char kSettingsResetPromptPromptWave[] =
    "settings_reset_prompt.prompt_wave";

// Timestamp of the last time the settings reset prompt was shown during the
// current prompt wave asking the user if they want to restore their search
// engine.
const char kSettingsResetPromptLastTriggeredForDefaultSearch[] =
    "settings_reset_prompt.last_triggered_for_default_search";

// Timestamp of the last time the settings reset prompt was shown during the
// current prompt wave asking the user if they want to restore their startup
// settings.
const char kSettingsResetPromptLastTriggeredForStartupUrls[] =
    "settings_reset_prompt.last_triggered_for_startup_urls";

// Timestamp of the last time the settings reset prompt was shown during the
// current prompt wave asking the user if they want to restore their homepage.
const char kSettingsResetPromptLastTriggeredForHomepage[] =
    "settings_reset_prompt.last_triggered_for_homepage";

#if defined(OS_ANDROID)
// Timestamp of the clipboard's last modified time, stored in base::Time's
// internal format (int64) in local store.  (I.e., this is not a per-profile
// pref.)
const char kClipboardLastModifiedTime[] = "ui.clipboard.last_modified_time";
#endif

}  // namespace prefs
