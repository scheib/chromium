// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/people_handler.h"

#include <string>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/i18n/time_formatting.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/metrics/histogram_macros.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/signin/chrome_signin_helper.h"
#include "chrome/browser/signin/signin_error_controller_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/signin/signin_promo.h"
#include "chrome/browser/signin/signin_ui_util.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/sync/sync_ui_util.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "chrome/browser/ui/user_manager.h"
#include "chrome/browser/ui/webui/profile_helper.h"
#include "chrome/browser/ui/webui/signin/login_ui_service.h"
#include "chrome/browser/ui/webui/signin/login_ui_service_factory.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/grit/generated_resources.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/autofill/core/common/autofill_pref_names.h"
#include "components/browser_sync/profile_sync_service.h"
#include "components/prefs/pref_service.h"
#include "components/signin/core/browser/signin_error_controller.h"
#include "components/signin/core/browser/signin_header_helper.h"
#include "components/signin/core/browser/signin_metrics.h"
#include "components/signin/core/common/profile_management_switches.h"
#include "components/signin/core/common/signin_pref_names.h"
#include "components/strings/grit/components_strings.h"
#include "components/sync/base/passphrase_type.h"
#include "components/sync/base/sync_prefs.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "google_apis/gaia/gaia_constants.h"
#include "net/base/url_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/webui/web_ui_util.h"

#if defined(OS_CHROMEOS)
#include "components/signin/core/browser/signin_manager_base.h"
#else
#include "components/signin/core/browser/signin_manager.h"
#endif

using browser_sync::ProfileSyncService;
using content::WebContents;
using l10n_util::GetStringFUTF16;
using l10n_util::GetStringUTF16;

namespace {

// A structure which contains all the configuration information for sync.
struct SyncConfigInfo {
  SyncConfigInfo();
  ~SyncConfigInfo();

  bool encrypt_all;
  bool sync_everything;
  syncer::ModelTypeSet data_types;
  bool payments_integration_enabled;
  std::string passphrase;
  bool set_new_passphrase;
};

SyncConfigInfo::SyncConfigInfo()
    : encrypt_all(false),
      sync_everything(false),
      payments_integration_enabled(false),
      set_new_passphrase(false) {}

SyncConfigInfo::~SyncConfigInfo() {}

bool GetConfiguration(const std::string& json, SyncConfigInfo* config) {
  std::unique_ptr<base::Value> parsed_value = base::JSONReader::Read(json);
  base::DictionaryValue* result;
  if (!parsed_value || !parsed_value->GetAsDictionary(&result)) {
    DLOG(ERROR) << "GetConfiguration() not passed a Dictionary";
    return false;
  }

  if (!result->GetBoolean("syncAllDataTypes", &config->sync_everything)) {
    DLOG(ERROR) << "GetConfiguration() not passed a syncAllDataTypes value";
    return false;
  }

  if (!result->GetBoolean("paymentsIntegrationEnabled",
                          &config->payments_integration_enabled)) {
    DLOG(ERROR) << "GetConfiguration() not passed a paymentsIntegrationEnabled "
                << "value";
    return false;
  }

  syncer::ModelTypeNameMap type_names = syncer::GetUserSelectableTypeNameMap();

  for (syncer::ModelTypeNameMap::const_iterator it = type_names.begin();
       it != type_names.end(); ++it) {
    std::string key_name = it->second + std::string("Synced");
    bool sync_value;
    if (!result->GetBoolean(key_name, &sync_value)) {
      DLOG(ERROR) << "GetConfiguration() not passed a value for " << key_name;
      return false;
    }
    if (sync_value)
      config->data_types.Put(it->first);
  }

  // Encryption settings.
  if (!result->GetBoolean("encryptAllData", &config->encrypt_all)) {
    DLOG(ERROR) << "GetConfiguration() not passed a value for encryptAllData";
    return false;
  }

  // Passphrase settings.
  if (result->GetString("passphrase", &config->passphrase) &&
      !config->passphrase.empty() &&
      !result->GetBoolean("setNewPassphrase", &config->set_new_passphrase)) {
    DLOG(ERROR) << "GetConfiguration() not passed a set_new_passphrase value";
    return false;
  }
  return true;
}

// Guaranteed to return a valid result (or crash).
void ParseConfigurationArguments(const base::ListValue* args,
                                 SyncConfigInfo* config,
                                 const base::Value** callback_id) {
  std::string json;
  if (args->Get(0, callback_id) && args->GetString(1, &json) && !json.empty())
    CHECK(GetConfiguration(json, config));
  else
    NOTREACHED();
}

std::string GetSyncErrorAction(sync_ui_util::ActionType action_type) {
  switch (action_type) {
    case sync_ui_util::REAUTHENTICATE:
      return "reauthenticate";
    case sync_ui_util::SIGNOUT_AND_SIGNIN:
      return "signOutAndSignIn";
    case sync_ui_util::UPGRADE_CLIENT:
      return "upgradeClient";
    case sync_ui_util::ENTER_PASSPHRASE:
      return "enterPassphrase";
    default:
      return "noAction";
  }
}

}  // namespace

namespace settings {

// static
const char PeopleHandler::kSpinnerPageStatus[] = "spinner";
const char PeopleHandler::kConfigurePageStatus[] = "configure";
const char PeopleHandler::kTimeoutPageStatus[] = "timeout";
const char PeopleHandler::kDonePageStatus[] = "done";
const char PeopleHandler::kPassphraseFailedPageStatus[] = "passphraseFailed";

PeopleHandler::PeopleHandler(Profile* profile)
    : profile_(profile),
      configuring_sync_(false),
      signin_observer_(this),
      sync_service_observer_(this) {}

PeopleHandler::~PeopleHandler() {
  // Early exit if running unit tests (no actual WebUI is attached).
  if (!web_ui())
    return;

  // This case is hit when the user performs a back navigation.
  CloseSyncSetup();
}

void PeopleHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "SyncSetupDidClosePage",
      base::Bind(&PeopleHandler::OnDidClosePage, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "SyncSetupSetDatatypes",
      base::Bind(&PeopleHandler::HandleSetDatatypes, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "SyncSetupSetEncryption",
      base::Bind(&PeopleHandler::HandleSetEncryption, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "SyncSetupShowSetupUI",
      base::Bind(&PeopleHandler::HandleShowSetupUI, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "SyncSetupGetSyncStatus",
      base::Bind(&PeopleHandler::HandleGetSyncStatus, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "SyncSetupManageOtherPeople",
      base::Bind(&PeopleHandler::HandleManageOtherPeople,
                 base::Unretained(this)));
#if defined(OS_CHROMEOS)
  web_ui()->RegisterMessageCallback(
      "AttemptUserExit",
      base::Bind(&PeopleHandler::HandleAttemptUserExit,
                 base::Unretained(this)));
#else
  web_ui()->RegisterMessageCallback(
      "SyncSetupStopSyncing",
      base::Bind(&PeopleHandler::HandleStopSyncing, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "SyncSetupStartSignIn",
      base::Bind(&PeopleHandler::HandleStartSignin, base::Unretained(this)));
#endif
}

void PeopleHandler::OnJavascriptAllowed() {
  PrefService* prefs = profile_->GetPrefs();
  profile_pref_registrar_.Init(prefs);
  profile_pref_registrar_.Add(
      prefs::kSigninAllowed,
      base::Bind(&PeopleHandler::UpdateSyncStatus, base::Unretained(this)));

  SigninManagerBase* signin_manager(
      SigninManagerFactory::GetInstance()->GetForProfile(profile_));
  if (signin_manager)
    signin_observer_.Add(signin_manager);

  ProfileSyncService* sync_service(
      ProfileSyncServiceFactory::GetInstance()->GetForProfile(profile_));
  if (sync_service)
    sync_service_observer_.Add(sync_service);
}

void PeopleHandler::OnJavascriptDisallowed() {
  profile_pref_registrar_.RemoveAll();
  signin_observer_.RemoveAll();
  sync_service_observer_.RemoveAll();
}

#if !defined(OS_CHROMEOS)
void PeopleHandler::DisplayGaiaLogin(signin_metrics::AccessPoint access_point) {
  DCHECK(!sync_startup_tracker_);
  // Advanced options are no longer being configured if the login screen is
  // visible. If the user exits the signin wizard after this without
  // configuring sync, CloseSyncSetup() will ensure they are logged out.
  configuring_sync_ = false;
  DisplayGaiaLoginInNewTabOrWindow(access_point);
}

void PeopleHandler::DisplayGaiaLoginInNewTabOrWindow(
    signin_metrics::AccessPoint access_point) {
  Browser* browser =
      chrome::FindBrowserWithWebContents(web_ui()->GetWebContents());
  bool force_new_tab = false;
  if (!browser) {
    // Settings is not displayed in a browser window. Open a new window.
    browser = new Browser(
        Browser::CreateParams(Browser::TYPE_TABBED, profile_, true));
    force_new_tab = true;
  }

  // If the signin manager already has an authenticated username, this is a
  // re-auth scenario, and we need to ensure that the user signs in with the
  // same email address.
  GURL url;
  if (SigninManagerFactory::GetForProfile(browser->profile())
          ->IsAuthenticated()) {
    UMA_HISTOGRAM_ENUMERATION("Signin.Reauth",
                              signin_metrics::HISTOGRAM_REAUTH_SHOWN,
                              signin_metrics::HISTOGRAM_REAUTH_MAX);

    SigninErrorController* error_controller =
        SigninErrorControllerFactory::GetForProfile(browser->profile());
    DCHECK(error_controller->HasError());
    if (!force_new_tab) {
      browser->window()->ShowAvatarBubbleFromAvatarButton(
          BrowserWindow::AVATAR_BUBBLE_MODE_REAUTH,
          signin::ManageAccountsParams(), access_point, false);
    } else {
      url = signin::GetReauthURL(
          access_point, signin_metrics::Reason::REASON_REAUTHENTICATION,
          browser->profile(), error_controller->error_account_id());
    }
  } else {
    if (!force_new_tab) {
      browser->window()->ShowAvatarBubbleFromAvatarButton(
          BrowserWindow::AVATAR_BUBBLE_MODE_SIGNIN,
          signin::ManageAccountsParams(), access_point, false);
    } else {
      url = signin::GetPromoURL(
          access_point, signin_metrics::Reason::REASON_SIGNIN_PRIMARY_ACCOUNT,
          true);
    }
  }

  if (url.is_valid())
    chrome::ShowSingletonTab(browser, url);
}
#endif

void PeopleHandler::DisplaySpinner() {
  configuring_sync_ = true;

  const int kTimeoutSec = 30;
  DCHECK(!engine_start_timer_);
  engine_start_timer_.reset(new base::OneShotTimer());
  engine_start_timer_->Start(FROM_HERE,
                             base::TimeDelta::FromSeconds(kTimeoutSec), this,
                             &PeopleHandler::DisplayTimeout);

  FireWebUIListener("page-status-changed", base::Value(kSpinnerPageStatus));
}

void PeopleHandler::DisplayTimeout() {
  // Stop a timer to handle timeout in waiting for checking network connection.
  engine_start_timer_.reset();

  // Do not listen to sync startup events.
  sync_startup_tracker_.reset();

  FireWebUIListener("page-status-changed", base::Value(kTimeoutPageStatus));
}

void PeopleHandler::OnDidClosePage(const base::ListValue* args) {
  MarkFirstSetupComplete();
  CloseSyncSetup();
}

void PeopleHandler::SyncStartupFailed() {
  // Stop a timer to handle timeout in waiting for checking network connection.
  engine_start_timer_.reset();

  // Just close the sync overlay (the idea is that the base settings page will
  // display the current error.)
  CloseUI();
}

void PeopleHandler::SyncStartupCompleted() {
  ProfileSyncService* service = GetSyncService();
  DCHECK(service->IsEngineInitialized());

  // Stop a timer to handle timeout in waiting for checking network connection.
  engine_start_timer_.reset();

  sync_startup_tracker_.reset();

  PushSyncPrefs();
}

ProfileSyncService* PeopleHandler::GetSyncService() const {
  return profile_->IsSyncAllowed()
             ? ProfileSyncServiceFactory::GetForProfile(profile_)
             : nullptr;
}

void PeopleHandler::HandleSetDatatypes(const base::ListValue* args) {
  DCHECK(!sync_startup_tracker_);

  SyncConfigInfo configuration;
  const base::Value* callback_id = nullptr;
  ParseConfigurationArguments(args, &configuration, &callback_id);

  PrefService* pref_service = profile_->GetPrefs();
  pref_service->SetBoolean(autofill::prefs::kAutofillWalletImportEnabled,
                           configuration.payments_integration_enabled);

  // Start configuring the ProfileSyncService using the configuration passed
  // to us from the JS layer.
  ProfileSyncService* service = GetSyncService();

  // If the sync engine has shutdown for some reason, just close the sync
  // dialog.
  if (!service || !service->IsEngineInitialized()) {
    CloseSyncSetup();
    ResolveJavascriptCallback(*callback_id, base::Value(kDonePageStatus));
    return;
  }

  service->OnUserChoseDatatypes(configuration.sync_everything,
                                configuration.data_types);

  // Choosing data types to sync never fails.
  ResolveJavascriptCallback(*callback_id, base::Value(kConfigurePageStatus));

  ProfileMetrics::LogProfileSyncInfo(ProfileMetrics::SYNC_CUSTOMIZE);
  if (!configuration.sync_everything)
    ProfileMetrics::LogProfileSyncInfo(ProfileMetrics::SYNC_CHOOSE);
}

void PeopleHandler::HandleSetEncryption(const base::ListValue* args) {
  DCHECK(!sync_startup_tracker_);

  SyncConfigInfo configuration;
  const base::Value* callback_id = nullptr;
  ParseConfigurationArguments(args, &configuration, &callback_id);

  // Start configuring the ProfileSyncService using the configuration passed
  // to us from the JS layer.
  ProfileSyncService* service = GetSyncService();

  // If the sync engine has shutdown for some reason, just close the sync
  // dialog.
  if (!service || !service->IsEngineInitialized()) {
    CloseSyncSetup();
    ResolveJavascriptCallback(*callback_id, base::Value(kDonePageStatus));
    return;
  }

  // Don't allow "encrypt all" if the ProfileSyncService doesn't allow it.
  // The UI is hidden, but the user may have enabled it e.g. by fiddling with
  // the web inspector.
  if (!service->IsEncryptEverythingAllowed())
    configuration.encrypt_all = false;

  // Note: Data encryption will not occur until configuration is complete
  // (when the PSS receives its CONFIGURE_DONE notification from the sync
  // engine), so the user still has a chance to cancel out of the operation
  // if (for example) some kind of passphrase error is encountered.
  if (configuration.encrypt_all)
    service->EnableEncryptEverything();

  bool passphrase_failed = false;
  if (!configuration.passphrase.empty()) {
    // We call IsPassphraseRequired() here (instead of
    // IsPassphraseRequiredForDecryption()) because the user may try to enter
    // a passphrase even though no encrypted data types are enabled.
    if (service->IsPassphraseRequired()) {
      // If we have pending keys, try to decrypt them with the provided
      // passphrase. We track if this succeeds or fails because a failed
      // decryption should result in an error even if there aren't any encrypted
      // data types.
      passphrase_failed =
          !service->SetDecryptionPassphrase(configuration.passphrase);
    } else {
      // OK, the user sent us a passphrase, but we don't have pending keys. So
      // it either means that the pending keys were resolved somehow since the
      // time the UI was displayed (re-encryption, pending passphrase change,
      // etc) or the user wants to re-encrypt.
      if (configuration.set_new_passphrase &&
          !service->IsUsingSecondaryPassphrase()) {
        service->SetEncryptionPassphrase(configuration.passphrase,
                                         ProfileSyncService::EXPLICIT);
      }
    }
  }

  if (passphrase_failed || service->IsPassphraseRequiredForDecryption()) {
    // If the user doesn't enter any passphrase, we won't call
    // SetDecryptionPassphrase() (passphrase_failed == false), but we still
    // want to display an error message to let the user know that their blank
    // passphrase entry is not acceptable.

    // TODO(tommycli): Switch this to RejectJavascriptCallback once the
    // Sync page JavaScript has been further refactored.
    ResolveJavascriptCallback(*callback_id,
                              base::Value(kPassphraseFailedPageStatus));
  } else {
    ResolveJavascriptCallback(*callback_id, base::Value(kConfigurePageStatus));
  }

  if (configuration.encrypt_all)
    ProfileMetrics::LogProfileSyncInfo(ProfileMetrics::SYNC_ENCRYPT);
  if (!configuration.set_new_passphrase && !configuration.passphrase.empty())
    ProfileMetrics::LogProfileSyncInfo(ProfileMetrics::SYNC_PASSPHRASE);
}

void PeopleHandler::HandleShowSetupUI(const base::ListValue* args) {
  AllowJavascript();

  if (!GetSyncService()) {
    CloseUI();
    return;
  }

  SigninManagerBase* signin = SigninManagerFactory::GetForProfile(profile_);
  if (!signin->IsAuthenticated()) {
    // For web-based signin, the signin page is not displayed in an overlay
    // on the settings page. So if we get here, it must be due to the user
    // cancelling signin (by reloading the sync settings page during initial
    // signin) or by directly navigating to settings/syncSetup
    // (http://crbug.com/229836). So just exit and go back to the settings page.
    DLOG(WARNING) << "Cannot display sync setup UI when not signed in";
    CloseUI();
    return;
  }

  OpenSyncSetup();
}

#if defined(OS_CHROMEOS)
// On ChromeOS, we need to sign out the user session to fix an auth error, so
// the user goes through the real signin flow to generate a new auth token.
void PeopleHandler::HandleAttemptUserExit(const base::ListValue* args) {
  DVLOG(1) << "Signing out the user to fix a sync error.";
  chrome::AttemptUserExit();
}
#endif

#if !defined(OS_CHROMEOS)
void PeopleHandler::HandleStartSignin(const base::ListValue* args) {
  AllowJavascript();

  // Should only be called if the user is not already signed in or has an auth
  // error.
  DCHECK(!SigninManagerFactory::GetForProfile(profile_)->IsAuthenticated() ||
         SigninErrorControllerFactory::GetForProfile(profile_)->HasError());

  OpenSyncSetup();
}

void PeopleHandler::HandleStopSyncing(const base::ListValue* args) {
  bool delete_profile = false;
  args->GetBoolean(0, &delete_profile);

  if (!SigninManagerFactory::GetForProfile(profile_)->IsSignoutProhibited()) {
    if (GetSyncService())
      ProfileSyncService::SyncEvent(ProfileSyncService::STOP_FROM_OPTIONS);

    signin_metrics::SignoutDelete delete_metric =
        delete_profile ? signin_metrics::SignoutDelete::DELETED
                       : signin_metrics::SignoutDelete::KEEPING;
    SigninManagerFactory::GetForProfile(profile_)
        ->SignOut(signin_metrics::USER_CLICKED_SIGNOUT_SETTINGS, delete_metric);
  }

  if (delete_profile) {
    webui::DeleteProfileAtPath(profile_->GetPath(),
                               web_ui(),
                               ProfileMetrics::DELETE_PROFILE_SETTINGS);
  }
}
#endif

void PeopleHandler::HandleGetSyncStatus(const base::ListValue* args) {
  AllowJavascript();

  CHECK_EQ(1U, args->GetSize());
  const base::Value* callback_id;
  CHECK(args->Get(0, &callback_id));

  ResolveJavascriptCallback(*callback_id, *GetSyncStatusDictionary());
}

void PeopleHandler::HandleManageOtherPeople(const base::ListValue* /* args */) {
  UserManager::Show(base::FilePath(), profiles::USER_MANAGER_NO_TUTORIAL,
                    profiles::USER_MANAGER_SELECT_PROFILE_NO_ACTION);
}

void PeopleHandler::CloseSyncSetup() {
  // Stop a timer to handle timeout in waiting for checking network connection.
  engine_start_timer_.reset();

  // Clear the sync startup tracker, since the setup wizard is being closed.
  sync_startup_tracker_.reset();

  ProfileSyncService* sync_service = GetSyncService();

  // LoginUIService can be nullptr if page is brought up in incognito mode
  // (i.e. if the user is running in guest mode in cros and brings up settings).
  LoginUIService* service = GetLoginUIService();
  if (service) {
    // Don't log a cancel event if the sync setup dialog is being
    // automatically closed due to an auth error.
    if ((service->current_login_ui() == this) &&
        (!sync_service || (!sync_service->IsFirstSetupComplete() &&
                           sync_service->GetAuthError().state() ==
                               GoogleServiceAuthError::NONE))) {
      if (configuring_sync_) {
        ProfileSyncService::SyncEvent(
            ProfileSyncService::CANCEL_DURING_CONFIGURE);

        // If the user clicked "Cancel" while setting up sync, disable sync
        // because we don't want the sync engine to remain in the
        // first-setup-incomplete state.
        // Note: In order to disable sync across restarts on Chrome OS,
        // we must call RequestStop(CLEAR_DATA), which suppresses sync startup
        // in addition to disabling it.
        if (sync_service) {
          DVLOG(1) << "Sync setup aborted by user action";
          sync_service->RequestStop(ProfileSyncService::CLEAR_DATA);
#if !defined(OS_CHROMEOS)
          // Sign out the user on desktop Chrome if they click cancel during
          // initial setup.
          // TODO(rsimha): Revisit this for M30. See http://crbug.com/252049.
          if (sync_service->IsFirstSetupInProgress()) {
            SigninManagerFactory::GetForProfile(profile_)
                ->SignOut(signin_metrics::ABORT_SIGNIN,
                          signin_metrics::SignoutDelete::IGNORE_METRIC);
          }
#endif
        }
      }
    }

    service->LoginUIClosed(this);
  }

  // Alert the sync service anytime the sync setup dialog is closed. This can
  // happen due to the user clicking the OK or Cancel button, or due to the
  // dialog being closed by virtue of sync being disabled in the background.
  sync_blocker_.reset();

  configuring_sync_ = false;
}

void PeopleHandler::OpenSyncSetup() {
  // Notify services that login UI is now active.
  GetLoginUIService()->SetLoginUI(this);

  ProfileSyncService* service = GetSyncService();
  if (service)
    sync_blocker_ = service->GetSetupInProgressHandle();

  // There are several different UI flows that can bring the user here:
  // 1) Signin promo.
  // 2) Normal signin through settings page (IsAuthenticated() is false).
  // 3) Previously working credentials have expired.
  // 4) User is signed in, but has stopped sync via the google dashboard, and
  //    signout is prohibited by policy so we need to force a re-auth.
  // 5) User clicks [Advanced Settings] button on options page while already
  //    logged in.
  // 6) One-click signin (credentials are already available, so should display
  //    sync configure UI, not login UI).
  // 7) User re-enables sync after disabling it via advanced settings.
#if !defined(OS_CHROMEOS)
  if (!SigninManagerFactory::GetForProfile(profile_)->IsAuthenticated() ||
      SigninErrorControllerFactory::GetForProfile(profile_)->HasError()) {
    // User is not logged in (cases 1-2), or login has been specially requested
    // because previously working credentials have expired (case 3). Close sync
    // setup including any visible overlays, and display the gaia auth page.
    // Control will be returned to the sync settings page once auth is complete.
    CloseUI();
    DisplayGaiaLogin(signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS);
    return;
  }
#endif
  if (!service) {
    // This can happen if the user directly navigates to /settings/syncSetup.
    DLOG(WARNING) << "Cannot display sync UI when sync is disabled";
    CloseUI();
    return;
  }

  // Early exit if there is already a preferences push pending sync startup.
  if (sync_startup_tracker_)
    return;

  if (!service->IsEngineInitialized()) {
    // Requesting the sync service to start may trigger call to PushSyncPrefs.
    // Setting up the startup tracker beforehand correctly signals the
    // re-entrant call to early exit.
    sync_startup_tracker_.reset(new SyncStartupTracker(profile_, this));
    service->RequestStart();

    // See if it's even possible to bring up the sync engine - if not
    // (unrecoverable error?), don't bother displaying a spinner that will be
    // immediately closed because this leads to some ugly infinite UI loop (see
    // http://crbug.com/244769).
    if (SyncStartupTracker::GetSyncServiceState(profile_) !=
        SyncStartupTracker::SYNC_STARTUP_ERROR) {
      DisplaySpinner();
    }
    return;
  }

  // User is already logged in. They must have brought up the config wizard
  // via the "Advanced..." button or through One-Click signin (cases 4-6), or
  // they are re-enabling sync after having disabled it (case 7).
  PushSyncPrefs();
}

void PeopleHandler::FocusUI() {
  WebContents* web_contents = web_ui()->GetWebContents();
  web_contents->GetDelegate()->ActivateContents(web_contents);
}

void PeopleHandler::CloseUI() {
  CloseSyncSetup();
  FireWebUIListener("page-status-changed", base::Value(kDonePageStatus));
}

void PeopleHandler::GoogleSigninSucceeded(const std::string& /* account_id */,
                                          const std::string& /* username */,
                                          const std::string& /* password */) {
  UpdateSyncStatus();
}

void PeopleHandler::GoogleSignedOut(const std::string& /* account_id */,
                                    const std::string& /* username */) {
  UpdateSyncStatus();
}

void PeopleHandler::OnStateChanged(syncer::SyncService* sync) {
  UpdateSyncStatus();

  // When the SyncService changes its state, we should also push the updated
  // sync preferences.
  PushSyncPrefs();
}

std::unique_ptr<base::DictionaryValue>
PeopleHandler::GetSyncStatusDictionary() {
  // The items which are to be written into |sync_status| are also described in
  // chrome/browser/resources/options/browser_options.js in @typedef
  // for SyncStatus. Please update it whenever you add or remove any keys here.
  std::unique_ptr<base::DictionaryValue> sync_status(new base::DictionaryValue);
  if (profile_->IsGuestSession()) {
    // Cannot display signin status when running in guest mode on chromeos
    // because there is no SigninManager.
    sync_status->SetBoolean("signinAllowed", false);
    return sync_status;
  }

  sync_status->SetBoolean("supervisedUser", profile_->IsSupervised());
  sync_status->SetBoolean("childUser", profile_->IsChild());

  SigninManagerBase* signin = SigninManagerFactory::GetForProfile(profile_);
  DCHECK(signin);
#if !defined(OS_CHROMEOS)
  // Signout is not allowed if the user has policy (crbug.com/172204).
  if (SigninManagerFactory::GetForProfile(profile_)->IsSignoutProhibited()) {
    std::string username = signin->GetAuthenticatedAccountInfo().email;

    // If there is no one logged in or if the profile name is empty then the
    // domain name is empty. This happens in browser tests.
    if (!username.empty())
      sync_status->SetString("domain", gaia::ExtractDomainName(username));
  }
#endif

  ProfileSyncService* service =
      ProfileSyncServiceFactory::GetInstance()->GetForProfile(profile_);
  sync_status->SetBoolean("signinAllowed", signin->IsSigninAllowed());
  sync_status->SetBoolean("syncSystemEnabled", (service != nullptr));
  sync_status->SetBoolean("setupCompleted",
                          service && service->IsFirstSetupComplete());
  sync_status->SetBoolean(
      "setupInProgress",
      service && !service->IsManaged() && service->IsFirstSetupInProgress());

  base::string16 status_label;
  base::string16 link_label;
  sync_ui_util::ActionType action_type = sync_ui_util::NO_ACTION;
  bool status_has_error =
      sync_ui_util::GetStatusLabels(profile_, service, *signin,
                                    sync_ui_util::PLAIN_TEXT, &status_label,
                                    &link_label, &action_type) ==
      sync_ui_util::SYNC_ERROR;
  sync_status->SetString("statusText", status_label);
  sync_status->SetBoolean("hasError", status_has_error);
  sync_status->SetString("statusAction", GetSyncErrorAction(action_type));

  sync_status->SetBoolean("managed", service && service->IsManaged());
  sync_status->SetBoolean("signedIn", signin->IsAuthenticated());
  sync_status->SetString("signedInUsername",
                         signin_ui_util::GetAuthenticatedUsername(signin));
  sync_status->SetBoolean("hasUnrecoverableError",
                          service && service->HasUnrecoverableError());

  return sync_status;
}

void PeopleHandler::PushSyncPrefs() {
#if !defined(OS_CHROMEOS)
  // Early exit if the user has not signed in yet.
  if (!SigninManagerFactory::GetForProfile(profile_)->IsAuthenticated() ||
      SigninErrorControllerFactory::GetForProfile(profile_)->HasError()) {
    return;
  }
#endif

  ProfileSyncService* service = GetSyncService();
  // The sync service may be nullptr if it has been just disabled by policy.
  if (!service || !service->IsEngineInitialized()) {
    return;
  }

  configuring_sync_ = true;
  DCHECK(service->IsEngineInitialized())
      << "Cannot configure sync until the sync engine is initialized";

  // Setup args for the sync configure screen:
  //   syncAllDataTypes: true if the user wants to sync everything
  //   <data_type>Registered: true if the associated data type is supported
  //   <data_type>Synced: true if the user wants to sync that specific data type
  //   paymentsIntegrationEnabled: true if the user wants Payments integration
  //   encryptionEnabled: true if sync supports encryption
  //   encryptAllData: true if user wants to encrypt all data (not just
  //       passwords)
  //   passphraseRequired: true if a passphrase is needed to start sync
  //   passphraseTypeIsCustom: true if the passphrase type is custom
  //
  base::DictionaryValue args;

  // Tell the UI layer which data types are registered/enabled by the user.
  const syncer::ModelTypeSet registered_types =
      service->GetRegisteredDataTypes();
  const syncer::ModelTypeSet preferred_types = service->GetPreferredDataTypes();
  const syncer::ModelTypeSet enforced_types = service->GetForcedDataTypes();
  syncer::ModelTypeNameMap type_names = syncer::GetUserSelectableTypeNameMap();
  for (syncer::ModelTypeNameMap::const_iterator it = type_names.begin();
       it != type_names.end(); ++it) {
    syncer::ModelType sync_type = it->first;
    const std::string key_name = it->second;
    args.SetBoolean(key_name + "Registered", registered_types.Has(sync_type));
    args.SetBoolean(key_name + "Synced", preferred_types.Has(sync_type));
    args.SetBoolean(key_name + "Enforced", enforced_types.Has(sync_type));
    // TODO(treib): How do we want to handle pref groups, i.e. when only some of
    // the sync types behind a checkbox are force-enabled? crbug.com/403326
  }
  PrefService* pref_service = profile_->GetPrefs();
  syncer::SyncPrefs sync_prefs(pref_service);
  args.SetBoolean("syncAllDataTypes", sync_prefs.HasKeepEverythingSynced());
  args.SetBoolean(
      "paymentsIntegrationEnabled",
      pref_service->GetBoolean(autofill::prefs::kAutofillWalletImportEnabled));
  args.SetBoolean("encryptAllData", service->IsEncryptEverythingEnabled());
  args.SetBoolean("encryptAllDataAllowed",
                  service->IsEncryptEverythingAllowed());

  // We call IsPassphraseRequired() here, instead of calling
  // IsPassphraseRequiredForDecryption(), because we want to show the passphrase
  // UI even if no encrypted data types are enabled.
  args.SetBoolean("passphraseRequired", service->IsPassphraseRequired());

  // To distinguish between PassphraseType::FROZEN_IMPLICIT_PASSPHRASE and
  // PassphraseType::CUSTOM_PASSPHRASE
  // we only set passphraseTypeIsCustom for PassphraseType::CUSTOM_PASSPHRASE.
  args.SetBoolean("passphraseTypeIsCustom",
                  service->GetPassphraseType() ==
                      syncer::PassphraseType::CUSTOM_PASSPHRASE);
  base::Time passphrase_time = service->GetExplicitPassphraseTime();
  syncer::PassphraseType passphrase_type = service->GetPassphraseType();
  if (!passphrase_time.is_null()) {
    base::string16 passphrase_time_str =
        base::TimeFormatShortDate(passphrase_time);
    args.SetString("enterPassphraseBody",
                   GetStringFUTF16(IDS_SYNC_ENTER_PASSPHRASE_BODY_WITH_DATE,
                                   passphrase_time_str));
    args.SetString(
        "enterGooglePassphraseBody",
        GetStringFUTF16(IDS_SYNC_ENTER_GOOGLE_PASSPHRASE_BODY_WITH_DATE,
                        passphrase_time_str));
    switch (passphrase_type) {
      case syncer::PassphraseType::FROZEN_IMPLICIT_PASSPHRASE:
        args.SetString(
            "fullEncryptionBody",
            GetStringFUTF16(IDS_SYNC_FULL_ENCRYPTION_BODY_GOOGLE_WITH_DATE,
                            passphrase_time_str));
        break;
      case syncer::PassphraseType::CUSTOM_PASSPHRASE:
        args.SetString(
            "fullEncryptionBody",
            GetStringFUTF16(IDS_SYNC_FULL_ENCRYPTION_BODY_CUSTOM_WITH_DATE,
                            passphrase_time_str));
        break;
      default:
        args.SetString("fullEncryptionBody",
                       GetStringUTF16(IDS_SYNC_FULL_ENCRYPTION_BODY_CUSTOM));
        break;
    }
  } else if (passphrase_type == syncer::PassphraseType::CUSTOM_PASSPHRASE) {
    args.SetString("fullEncryptionBody",
                   GetStringUTF16(IDS_SYNC_FULL_ENCRYPTION_BODY_CUSTOM));
  }

  FireWebUIListener("sync-prefs-changed", args);
}

LoginUIService* PeopleHandler::GetLoginUIService() const {
  return LoginUIServiceFactory::GetForProfile(profile_);
}

void PeopleHandler::UpdateSyncStatus() {
  FireWebUIListener("sync-status-changed", *GetSyncStatusDictionary());
}

void PeopleHandler::MarkFirstSetupComplete() {
  // Suppress the sign in promo once the user starts sync. This way the user
  // doesn't see the sign in promo even if they sign out later on.
  signin::SetUserSkippedPromo(profile_);

  ProfileSyncService* service = GetSyncService();
  // The sync service may be nullptr if it has been just disabled by policy.
  if (!service || service->IsFirstSetupComplete())
    return;

  // This is the first time configuring sync, so log it.
  base::FilePath profile_file_path = profile_->GetPath();
  ProfileMetrics::LogProfileSyncSignIn(profile_file_path);

  // We're done configuring, so notify ProfileSyncService that it is OK to
  // start syncing.
  sync_blocker_.reset();
  service->SetFirstSetupComplete();
}

}  // namespace settings
