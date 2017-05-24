// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/lock/screen_locker.h"

#include <string>
#include <vector>

#include "ash/login/ui/lock_screen.h"
#include "ash/public/interfaces/constants.mojom.h"
#include "ash/public/interfaces/session_controller.mojom.h"
#include "ash/shell.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm/wm_event.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/accessibility/accessibility_manager.h"
#include "chrome/browser/chromeos/login/lock/webui_screen_locker.h"
#include "chrome/browser/chromeos/login/quick_unlock/quick_unlock_factory.h"
#include "chrome/browser/chromeos/login/quick_unlock/quick_unlock_storage.h"
#include "chrome/browser/chromeos/login/session/user_session_manager.h"
#include "chrome/browser/chromeos/login/supervised/supervised_user_authentication.h"
#include "chrome/browser/chromeos/login/ui/user_adding_screen.h"
#include "chrome/browser/chromeos/login/users/chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/supervised_user_manager.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/signin/easy_unlock_service.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/ui/ash/ash_util.h"
#include "chrome/browser/ui/ash/lock_screen_client.h"
#include "chrome/browser/ui/ash/session_controller_client.h"
#include "chrome/browser/ui/webui/chromeos/login/screenlock_icon_provider.h"
#include "chrome/browser/ui/webui/chromeos/login/screenlock_icon_source.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/audio/chromeos_sounds.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/biod/constants.pb.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/session_manager_client.h"
#include "chromeos/login/auth/authenticator.h"
#include "chromeos/login/auth/extended_authenticator.h"
#include "chromeos/network/portal_detector/network_portal_detector.h"
#include "components/session_manager/core/session_manager.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/user_manager/user_manager.h"
#include "components/user_manager/user_type.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/service_manager_connection.h"
#include "media/audio/sounds/sounds_manager.h"
#include "services/device/public/interfaces/constants.mojom.h"
#include "services/service_manager/public/cpp/connector.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

using base::UserMetricsAction;
using content::BrowserThread;

namespace chromeos {

namespace {

// Timeout for unlock animation guard - some animations may be required to run
// on successful authentication before unlocking, but we want to be sure that
// unlock happens even if animations are broken.
const int kUnlockGuardTimeoutMs = 400;

// Returns true if we are using md-based login/lock.
bool IsUsingMdLogin() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      chromeos::switches::kShowMdLogin);
}

// Returns true if fingerprint authentication is available for one of the
// |users|.
bool IsFingerprintAuthenticationAvailableForUsers(
    const user_manager::UserList& users) {
  for (auto* user : users) {
    quick_unlock::QuickUnlockStorage* quick_unlock_storage =
        quick_unlock::QuickUnlockFactory::GetForUser(user);
    if (quick_unlock_storage &&
        quick_unlock_storage->IsFingerprintAuthenticationAvailable()) {
      return true;
    }
  }
  return false;
}

// Observer to start ScreenLocker when locking the screen is requested.
class ScreenLockObserver : public SessionManagerClient::StubDelegate,
                           public content::NotificationObserver,
                           public UserAddingScreen::Observer {
 public:
  ScreenLockObserver() : session_started_(false) {
    registrar_.Add(this,
                   chrome::NOTIFICATION_SESSION_STARTED,
                   content::NotificationService::AllSources());
    DBusThreadManager::Get()->GetSessionManagerClient()->SetStubDelegate(this);
  }

  ~ScreenLockObserver() override {
    if (DBusThreadManager::IsInitialized()) {
      DBusThreadManager::Get()->GetSessionManagerClient()->SetStubDelegate(
          nullptr);
    }
  }

  bool session_started() const { return session_started_; }

  // SessionManagerClient::StubDelegate overrides:
  void LockScreenForStub() override { ScreenLocker::HandleLockScreenRequest(); }

  // NotificationObserver overrides:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override {
    if (type == chrome::NOTIFICATION_SESSION_STARTED) {
      session_started_ = true;

      // The user session has just started, so the user has logged in. Mark a
      // strong authentication to allow them to use PIN to unlock the device.
      user_manager::User* user =
          content::Details<user_manager::User>(details).ptr();
      quick_unlock::QuickUnlockStorage* quick_unlock_storage =
          quick_unlock::QuickUnlockFactory::GetForUser(user);
      if (quick_unlock_storage)
        quick_unlock_storage->MarkStrongAuth();
    } else {
      NOTREACHED() << "Unexpected notification " << type;
    }
  }

  // UserAddingScreen::Observer overrides:
  void OnUserAddingFinished() override {
    UserAddingScreen::Get()->RemoveObserver(this);
    ScreenLocker::HandleLockScreenRequest();
  }

 private:
  bool session_started_;
  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(ScreenLockObserver);
};

// Mojo delegate that calls into the views-based lock screen via mojo.
class MojoDelegate : public ScreenLocker::Delegate {
 public:
  MojoDelegate() = default;
  ~MojoDelegate() override = default;

  // ScreenLocker::Delegate:
  void SetPasswordInputEnabled(bool enabled) override { NOTIMPLEMENTED(); }
  void ShowErrorMessage(int error_msg_id,
                        HelpAppLauncher::HelpTopic help_topic_id) override {
    // TODO(xiaoyinh): Complete the implementation here.
    LockScreenClient::Get()->ShowErrorMessage(0 /* login_attempts */,
                                              std::string(), std::string(),
                                              static_cast<int>(help_topic_id));
  }
  void ClearErrors() override { LockScreenClient::Get()->ClearErrors(); }
  void AnimateAuthenticationSuccess() override { NOTIMPLEMENTED(); }
  void OnLockWebUIReady() override { NOTIMPLEMENTED(); }
  void OnLockBackgroundDisplayed() override { NOTIMPLEMENTED(); }
  void OnHeaderBarVisible() override { NOTIMPLEMENTED(); }
  void OnAshLockAnimationFinished() override { NOTIMPLEMENTED(); }
  void SetFingerprintState(const AccountId& account_id,
                           ScreenLocker::FingerprintState state) override {
    NOTIMPLEMENTED();
  }
  content::WebContents* GetWebContents() override { return nullptr; }

 private:
  DISALLOW_COPY_AND_ASSIGN(MojoDelegate);
};

ScreenLockObserver* g_screen_lock_observer = nullptr;

}  // namespace

// static
ScreenLocker* ScreenLocker::screen_locker_ = nullptr;

//////////////////////////////////////////////////////////////////////////////
// ScreenLocker::Delegate, public:

ScreenLocker::Delegate::Delegate() = default;

ScreenLocker::Delegate::~Delegate() = default;

//////////////////////////////////////////////////////////////////////////////
// ScreenLocker, public:

ScreenLocker::ScreenLocker(const user_manager::UserList& users)
    : users_(users), binding_(this), weak_factory_(this) {
  DCHECK(!screen_locker_);
  screen_locker_ = this;

  ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
  media::SoundsManager* manager = media::SoundsManager::Get();
  manager->Initialize(SOUND_LOCK,
                      bundle.GetRawDataResource(IDR_SOUND_LOCK_WAV));
  manager->Initialize(SOUND_UNLOCK,
                      bundle.GetRawDataResource(IDR_SOUND_UNLOCK_WAV));
  service_manager::Connector* connector =
      content::ServiceManagerConnection::GetForProcess()->GetConnector();
  connector->BindInterface(device::mojom::kServiceName, &fp_service_);
  fp_service_->AddFingerprintObserver(binding_.CreateInterfacePtrAndBind());
}

void ScreenLocker::Init() {
  input_method::InputMethodManager* imm =
      input_method::InputMethodManager::Get();
  saved_ime_state_ = imm->GetActiveIMEState();
  imm->SetState(saved_ime_state_->Clone());

  authenticator_ = UserSessionManager::GetInstance()->CreateAuthenticator(this);
  extended_authenticator_ = ExtendedAuthenticator::Create(this);
  if (IsUsingMdLogin()) {
    // Create delegate that calls into the views-based lock screen via mojo.
    delegate_ = new MojoDelegate();
    owns_delegate_ = true;

    // Create and display lock screen.
    // TODO(jdufault): Calling ash::ShowLockScreenInWidget should be a mojo
    // call. We should only set the session state to locked after the mojo call
    // has completed.
    if (ash::ShowLockScreen()) {
      session_manager::SessionManager::Get()->SetSessionState(
          session_manager::SessionState::LOCKED);
    }
  } else {
    web_ui_.reset(new WebUIScreenLocker(this));
    delegate_ = web_ui_.get();
    owns_delegate_ = false;
    web_ui_->LockScreen();

    // Ownership of |icon_image_source| is passed.
    screenlock_icon_provider_ = base::MakeUnique<ScreenlockIconProvider>();
    ScreenlockIconSource* screenlock_icon_source =
        new ScreenlockIconSource(screenlock_icon_provider_->AsWeakPtr());
    content::URLDataSource::Add(web_ui_->web_contents()->GetBrowserContext(),
                                screenlock_icon_source);
  }

  // Start locking on ash side.
  SessionControllerClient::Get()->StartLock(base::Bind(
      &ScreenLocker::OnStartLockCallback, weak_factory_.GetWeakPtr()));
}

void ScreenLocker::OnAuthFailure(const AuthFailure& error) {
  base::RecordAction(UserMetricsAction("ScreenLocker_OnLoginFailure"));
  if (authentication_start_time_.is_null()) {
    LOG(ERROR) << "Start time is not set at authentication failure";
  } else {
    base::TimeDelta delta = base::Time::Now() - authentication_start_time_;
    VLOG(1) << "Authentication failure: " << delta.InSecondsF() << " second(s)";
    UMA_HISTOGRAM_TIMES("ScreenLocker.AuthenticationFailureTime", delta);
  }

  UMA_HISTOGRAM_ENUMERATION("ScreenLocker.AuthenticationFailure",
                            unlock_attempt_type_, UnlockType::AUTH_COUNT);

  EnableInput();
  // Don't enable signout button here as we're showing
  // MessageBubble.

  delegate_->ShowErrorMessage(incorrect_passwords_count_++
                                  ? IDS_LOGIN_ERROR_AUTHENTICATING_2ND_TIME
                                  : IDS_LOGIN_ERROR_AUTHENTICATING,
                              HelpAppLauncher::HELP_CANT_ACCESS_ACCOUNT);

  if (auth_status_consumer_)
    auth_status_consumer_->OnAuthFailure(error);
}

void ScreenLocker::OnAuthSuccess(const UserContext& user_context) {
  incorrect_passwords_count_ = 0;
  if (authentication_start_time_.is_null()) {
    if (user_context.GetAccountId().is_valid())
      LOG(ERROR) << "Start time is not set at authentication success";
  } else {
    base::TimeDelta delta = base::Time::Now() - authentication_start_time_;
    VLOG(1) << "Authentication success: " << delta.InSecondsF() << " second(s)";
    UMA_HISTOGRAM_TIMES("ScreenLocker.AuthenticationSuccessTime", delta);
  }

  UMA_HISTOGRAM_ENUMERATION("ScreenLocker.AuthenticationSuccess",
                            unlock_attempt_type_, UnlockType::AUTH_COUNT);

  const user_manager::User* user =
      user_manager::UserManager::Get()->FindUser(user_context.GetAccountId());
  if (user) {
    if (!user->is_active()) {
      saved_ime_state_ = nullptr;
      user_manager::UserManager::Get()->SwitchActiveUser(
          user_context.GetAccountId());
    }

    // Reset the number of PIN attempts available to the user. We always do this
    // because:
    // 1. If the user signed in with a PIN, that means they should be able to
    //    continue signing in with a PIN.
    // 2. If the user signed in with cryptohome keys, then the PIN timeout is
    //    going to be reset as well, so it is safe to reset the unlock attempt
    //    count.
    quick_unlock::QuickUnlockStorage* quick_unlock_storage =
        quick_unlock::QuickUnlockFactory::GetForUser(user);
    if (quick_unlock_storage) {
      quick_unlock_storage->pin_storage()->ResetUnlockAttemptCount();
      quick_unlock_storage->fingerprint_storage()->ResetUnlockAttemptCount();
    }

    UserSessionManager::GetInstance()->UpdateEasyUnlockKeys(user_context);
  } else {
    NOTREACHED() << "Logged in user not found.";
  }

  authentication_capture_.reset(new AuthenticationParametersCapture());
  authentication_capture_->user_context = user_context;

  // Add guard for case when something get broken in call chain to unlock
  // for sure.
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::Bind(&ScreenLocker::UnlockOnLoginSuccess,
                            weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(kUnlockGuardTimeoutMs));
  delegate_->AnimateAuthenticationSuccess();
}

void ScreenLocker::OnPasswordAuthSuccess(const UserContext& user_context) {
  // The user has signed in using their password, so reset the PIN timeout.
  quick_unlock::QuickUnlockStorage* quick_unlock_storage =
      quick_unlock::QuickUnlockFactory::GetForAccountId(
          user_context.GetAccountId());
  if (quick_unlock_storage)
    quick_unlock_storage->MarkStrongAuth();
}

void ScreenLocker::UnlockOnLoginSuccess() {
  DCHECK(base::MessageLoopForUI::IsCurrent());
  if (!authentication_capture_.get()) {
    LOG(WARNING) << "Call to UnlockOnLoginSuccess without previous " <<
      "authentication success.";
    return;
  }

  if (auth_status_consumer_) {
    auth_status_consumer_->OnAuthSuccess(authentication_capture_->user_context);
  }
  authentication_capture_.reset();
  weak_factory_.InvalidateWeakPtrs();

  VLOG(1) << "Hiding the lock screen.";
  chromeos::ScreenLocker::Hide();
}

void ScreenLocker::Authenticate(const UserContext& user_context) {
  LOG_ASSERT(IsUserLoggedIn(user_context.GetAccountId()))
      << "Invalid user trying to unlock.";

  authentication_start_time_ = base::Time::Now();
  delegate_->SetPasswordInputEnabled(false);
  if (user_context.IsUsingPin())
    unlock_attempt_type_ = AUTH_PIN;

  const user_manager::User* user = FindUnlockUser(user_context.GetAccountId());
  if (user) {
    // Check to see if the user submitted a PIN and it is valid.
    const std::string pin = user_context.GetKey()->GetSecret();

    // We only want to try authenticating the pin if it is a number,
    // otherwise we will timeout PIN if the user enters their account password
    // incorrectly more than a few times.
    int dummy_value;
    if (unlock_attempt_type_ == AUTH_PIN &&
        base::StringToInt(pin, &dummy_value)) {
      quick_unlock::QuickUnlockStorage* quick_unlock_storage =
          quick_unlock::QuickUnlockFactory::GetForUser(user);
      if (quick_unlock_storage &&
          quick_unlock_storage->TryAuthenticatePin(pin)) {
        OnAuthSuccess(user_context);
        return;
      }
    }

    // Special case: supervised users. Use special authenticator.
    if (user->GetType() == user_manager::USER_TYPE_SUPERVISED) {
      UserContext updated_context = ChromeUserManager::Get()
                                        ->GetSupervisedUserManager()
                                        ->GetAuthentication()
                                        ->TransformKey(user_context);
      BrowserThread::PostTask(
          BrowserThread::UI, FROM_HERE,
          base::Bind(&ExtendedAuthenticator::AuthenticateToCheck,
                     extended_authenticator_.get(), updated_context,
                     base::Bind(&ScreenLocker::OnPasswordAuthSuccess,
                                weak_factory_.GetWeakPtr(), updated_context)));
      return;
    }
  }

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ExtendedAuthenticator::AuthenticateToCheck,
                 extended_authenticator_.get(), user_context,
                 base::Bind(&ScreenLocker::OnPasswordAuthSuccess,
                            weak_factory_.GetWeakPtr(), user_context)));
}

const user_manager::User* ScreenLocker::FindUnlockUser(
    const AccountId& account_id) {
  for (const user_manager::User* user : users_) {
    if (user->GetAccountId() == account_id)
      return user;
  }
  return nullptr;
}

void ScreenLocker::OnStartLockCallback(bool locked) {
  // Happens in tests that exit with a pending lock. In real lock failure,
  // ash::LockStateController would cause the current user session to be
  // terminated.
  if (!locked)
    return;

  delegate_->OnAshLockAnimationFinished();

  AccessibilityManager::Get()->PlayEarcon(
      chromeos::SOUND_LOCK, PlaySoundOption::SPOKEN_FEEDBACK_ENABLED);
}

void ScreenLocker::ClearErrors() {
  delegate_->ClearErrors();
}

void ScreenLocker::Signout() {
  delegate_->ClearErrors();
  base::RecordAction(UserMetricsAction("ScreenLocker_Signout"));
  // We expect that this call will not wait for any user input.
  // If it changes at some point, we will need to force exit.
  chrome::AttemptUserExit();

  // Don't hide yet the locker because the chrome screen may become visible
  // briefly.
}

void ScreenLocker::EnableInput() {
  delegate_->SetPasswordInputEnabled(true);
}

void ScreenLocker::ShowErrorMessage(int error_msg_id,
                                    HelpAppLauncher::HelpTopic help_topic_id,
                                    bool sign_out_only) {
  delegate_->SetPasswordInputEnabled(!sign_out_only);
  delegate_->ShowErrorMessage(error_msg_id, help_topic_id);
}

void ScreenLocker::SetLoginStatusConsumer(
    chromeos::AuthStatusConsumer* consumer) {
  auth_status_consumer_ = consumer;
}

// static
void ScreenLocker::InitClass() {
  DCHECK(!g_screen_lock_observer);
  g_screen_lock_observer = new ScreenLockObserver;
}

// static
void ScreenLocker::ShutDownClass() {
  DCHECK(g_screen_lock_observer);
  delete g_screen_lock_observer;
  g_screen_lock_observer = nullptr;
}

// static
void ScreenLocker::HandleLockScreenRequest() {
  VLOG(1) << "Received LockScreen request from session manager";
  DCHECK(g_screen_lock_observer);
  if (UserAddingScreen::Get()->IsRunning()) {
    VLOG(1) << "Waiting for user adding screen to stop";
    UserAddingScreen::Get()->AddObserver(g_screen_lock_observer);
    UserAddingScreen::Get()->Cancel();
    return;
  }
  if (g_screen_lock_observer->session_started() &&
      user_manager::UserManager::Get()->CanCurrentUserLock()) {
    ScreenLocker::Show();
  } else {
    // If the current user's session cannot be locked or the user has not
    // completed all sign-in steps yet, log out instead. The latter is done to
    // avoid complications with displaying the lock screen over the login
    // screen while remaining secure in the case the user walks away during
    // the sign-in steps. See crbug.com/112225 and crbug.com/110933.
    VLOG(1) << "Calling session manager's StopSession D-Bus method";
    DBusThreadManager::Get()->GetSessionManagerClient()->StopSession();
  }
  // Close captive portal window and clear signin profile.
  network_portal_detector::GetInstance()->OnLockScreenRequest();
}

// static
void ScreenLocker::Show() {
  base::RecordAction(UserMetricsAction("ScreenLocker_Show"));
  DCHECK(base::MessageLoopForUI::IsCurrent());

  // Check whether the currently logged in user is a guest account and if so,
  // refuse to lock the screen (crosbug.com/23764).
  if (user_manager::UserManager::Get()->IsLoggedInAsGuest()) {
    VLOG(1) << "Refusing to lock screen for guest account";
    return;
  }

  // Manipulating active window state directly does not work in mash so skip it
  // for mash. http://crbug.com/714677 tracks work to add the support for mash.
  if (!ash_util::IsRunningInMash()) {
    // If the active window is fullscreen, exit fullscreen to avoid the web page
    // or app mimicking the lock screen. Do not exit fullscreen if the shelf is
    // visible while in fullscreen because the shelf makes it harder for a web
    // page or app to mimick the lock screen.
    ash::wm::WindowState* active_window_state = ash::wm::GetActiveWindowState();
    if (active_window_state && active_window_state->IsFullscreen() &&
        active_window_state->hide_shelf_when_fullscreen()) {
      const ash::wm::WMEvent event(ash::wm::WM_EVENT_TOGGLE_FULLSCREEN);
      active_window_state->OnWMEvent(&event);
    }
  }

  if (!screen_locker_) {
    ScreenLocker* locker =
        new ScreenLocker(user_manager::UserManager::Get()->GetUnlockUsers());
    VLOG(1) << "Created ScreenLocker " << locker;
    locker->Init();
  } else {
    VLOG(1) << "ScreenLocker " << screen_locker_ << " already exists; "
            << " calling session manager's HandleLockScreenShown D-Bus method";
    DBusThreadManager::Get()->GetSessionManagerClient()->
        NotifyLockScreenShown();
  }
}

// static
void ScreenLocker::Hide() {
  DCHECK(base::MessageLoopForUI::IsCurrent());
  // For a guest user, screen_locker_ would have never been initialized.
  if (user_manager::UserManager::Get()->IsLoggedInAsGuest()) {
    VLOG(1) << "Refusing to hide lock screen for guest account";
    return;
  }

  DCHECK(screen_locker_);
  SessionControllerClient::Get()->RunUnlockAnimation(
      base::Bind(&ScreenLocker::ScheduleDeletion));
}

// static
void ScreenLocker::ScheduleDeletion() {
  // Avoid possible multiple calls.
  if (screen_locker_ == nullptr)
    return;
  VLOG(1) << "Deleting ScreenLocker " << screen_locker_;

  AccessibilityManager::Get()->PlayEarcon(
      SOUND_UNLOCK, PlaySoundOption::SPOKEN_FEEDBACK_ENABLED);

  delete screen_locker_;
  screen_locker_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// ScreenLocker, private:

ScreenLocker::~ScreenLocker() {
  VLOG(1) << "Destroying ScreenLocker " << this;
  DCHECK(base::MessageLoopForUI::IsCurrent());

  if (authenticator_.get())
    authenticator_->SetConsumer(nullptr);
  ClearErrors();

  screen_locker_ = nullptr;
  bool state = false;
  VLOG(1) << "Emitting SCREEN_LOCK_STATE_CHANGED with state=" << state;
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_SCREEN_LOCK_STATE_CHANGED,
      content::Source<ScreenLocker>(this),
      content::Details<bool>(&state));

  VLOG(1) << "Calling session manager's HandleLockScreenDismissed D-Bus method";
  DBusThreadManager::Get()->GetSessionManagerClient()->
      NotifyLockScreenDismissed();

  session_manager::SessionManager::Get()->SetSessionState(
      session_manager::SessionState::ACTIVE);

  if (saved_ime_state_.get()) {
    input_method::InputMethodManager::Get()->SetState(saved_ime_state_);
  }

  if (owns_delegate_) {
    owns_delegate_ = false;
    delete delegate_;
    delegate_ = nullptr;
  }
}

void ScreenLocker::SetAuthenticator(Authenticator* authenticator) {
  authenticator_ = authenticator;
}

void ScreenLocker::ScreenLockReady() {
  locked_ = true;
  base::TimeDelta delta = base::Time::Now() - start_time_;
  VLOG(1) << "ScreenLocker " << this << " is ready after "
          << delta.InSecondsF() << " second(s)";
  UMA_HISTOGRAM_TIMES("ScreenLocker.ScreenLockTime", delta);

  bool state = true;
  VLOG(1) << "Emitting SCREEN_LOCK_STATE_CHANGED with state=" << state;
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_SCREEN_LOCK_STATE_CHANGED,
      content::Source<ScreenLocker>(this),
      content::Details<bool>(&state));
  VLOG(1) << "Calling session manager's HandleLockScreenShown D-Bus method";
  DBusThreadManager::Get()->GetSessionManagerClient()->NotifyLockScreenShown();

  session_manager::SessionManager::Get()->SetSessionState(
      session_manager::SessionState::LOCKED);

  input_method::InputMethodManager::Get()
      ->GetActiveIMEState()
      ->EnableLockScreenLayouts();

  if (IsFingerprintAuthenticationAvailableForUsers(users_))
    fp_service_->StartAuthSession();
}

bool ScreenLocker::IsUserLoggedIn(const AccountId& account_id) const {
  for (user_manager::User* user : users_) {
    if (user->GetAccountId() == account_id)
      return true;
  }
  return false;
}

void ScreenLocker::OnAuthScanDone(
    uint32_t scan_result,
    const std::unordered_map<std::string, std::vector<std::string>>& matches) {
  unlock_attempt_type_ = AUTH_FINGERPRINT;
  user_manager::User* active_user =
      user_manager::UserManager::Get()->GetActiveUser();
  quick_unlock::QuickUnlockStorage* quick_unlock_storage =
      quick_unlock::QuickUnlockFactory::GetForUser(active_user);
  if (!quick_unlock_storage ||
      !quick_unlock_storage->IsFingerprintAuthenticationAvailable()) {
    return;
  }

  if (scan_result != biod::ScanResult::SCAN_RESULT_SUCCESS) {
    OnFingerprintAuthFailure(*active_user);
    return;
  }

  UserContext user_context(active_user->GetAccountId());
  if (!base::ContainsKey(matches, active_user->username_hash())) {
    OnFingerprintAuthFailure(*active_user);
    return;
  }
  delegate_->SetFingerprintState(active_user->GetAccountId(),
                                 FingerprintState::kSignin);
  OnAuthSuccess(user_context);
}

void ScreenLocker::OnSessionFailed() {
  LOG(ERROR) << "Fingerprint session failed.";
}

void ScreenLocker::OnFingerprintAuthFailure(const user_manager::User& user) {
  UMA_HISTOGRAM_ENUMERATION("ScreenLocker.AuthenticationFailure",
                            unlock_attempt_type_, UnlockType::AUTH_COUNT);

  delegate_->SetFingerprintState(user.GetAccountId(),
                                 FingerprintState::kFailed);

  quick_unlock::QuickUnlockStorage* quick_unlock_storage =
      quick_unlock::QuickUnlockFactory::GetForUser(&user);
  if (quick_unlock_storage &&
      quick_unlock_storage->IsFingerprintAuthenticationAvailable()) {
    quick_unlock_storage->fingerprint_storage()->AddUnlockAttempt();
    if (quick_unlock_storage->fingerprint_storage()->ExceededUnlockAttempts()) {
      delegate_->SetFingerprintState(user.GetAccountId(),
                                     FingerprintState::kRemoved);
      delegate_->ShowErrorMessage(IDS_LOGIN_ERROR_FINGERPRINT_MAX_ATTEMPT,
                                  HelpAppLauncher::HELP_CANT_ACCESS_ACCOUNT);
    }
  }

  if (auth_status_consumer_) {
    AuthFailure failure(AuthFailure::UNLOCK_FAILED);
    auth_status_consumer_->OnAuthFailure(failure);
  }
}

}  // namespace chromeos
