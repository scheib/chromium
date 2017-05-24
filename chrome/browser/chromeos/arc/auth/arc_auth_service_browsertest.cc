// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "chrome/browser/chromeos/arc/arc_auth_context.h"
#include "chrome/browser/chromeos/arc/arc_auth_notification.h"
#include "chrome/browser/chromeos/arc/arc_auth_service.h"
#include "chrome/browser/chromeos/arc/arc_service_launcher.h"
#include "chrome/browser/chromeos/arc/arc_session_manager.h"
#include "chrome/browser/chromeos/arc/arc_util.h"
#include "chrome/browser/chromeos/arc/auth/arc_background_auth_code_fetcher.h"
#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/scoped_user_manager_enabler.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/fake_profile_oauth2_token_service_builder.h"
#include "chrome/browser/signin/fake_signin_manager_builder.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_profile.h"
#include "components/arc/arc_features.h"
#include "components/arc/arc_service_manager.h"
#include "components/arc/arc_session_runner.h"
#include "components/arc/arc_util.h"
#include "components/arc/test/fake_arc_session.h"
#include "components/prefs/pref_member.h"
#include "components/prefs/pref_service.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/signin/core/browser/fake_profile_oauth2_token_service.h"
#include "components/user_manager/known_user.h"
#include "components/user_manager/user_manager.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

constexpr char kRefreshToken[] = "fake-refresh-token";
constexpr char kFakeUserName[] = "test@example.com";
constexpr char kFakeGaiaId[] = "1234567890";
constexpr char kFakeAuthCode[] = "fake-auth-code";

}  // namespace

namespace arc {

class FakeAuthInstance : public mojom::AuthInstance {
 public:
  // mojom::AuthInstance:
  void Init(mojom::AuthHostPtr host) override { host_ = std::move(host); }

  void OnAccountInfoReady(mojom::AccountInfoPtr account_info) override {
    account_info_ = std::move(account_info);
    base::ResetAndReturn(&done_closure_).Run();
  }

  void RequestAccountInfo(base::Closure done_closure) {
    done_closure_ = done_closure;
    host_->RequestAccountInfo();
  }

  mojom::AccountInfo* account_info() { return account_info_.get(); }

 private:
  mojom::AuthHostPtr host_;
  mojom::AccountInfoPtr account_info_;
  base::Closure done_closure_;
};

class ArcAuthServiceTest : public InProcessBrowserTest {
 protected:
  ArcAuthServiceTest() = default;

  // InProcessBrowserTest:
  ~ArcAuthServiceTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    arc::SetArcAvailableCommandLineForTesting(command_line);
  }

  void SetUpOnMainThread() override {
    user_manager_enabler_ =
        base::MakeUnique<chromeos::ScopedUserManagerEnabler>(
            new chromeos::FakeChromeUserManager());
    // Init ArcSessionManager for testing.
    ArcSessionManager::DisableUIForTesting();
    ArcAuthNotification::DisableForTesting();
    ArcSessionManager::EnableCheckAndroidManagementForTesting();
    ArcSessionManager::Get()->SetArcSessionRunnerForTesting(
        base::MakeUnique<ArcSessionRunner>(base::Bind(FakeArcSession::Create)));

    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    // Create test profile.
    TestingProfile::Builder profile_builder;
    profile_builder.SetPath(temp_dir_.GetPath().AppendASCII("TestArcProfile"));
    profile_builder.SetProfileName(kFakeUserName);

    profile_builder.AddTestingFactory(
        ProfileOAuth2TokenServiceFactory::GetInstance(),
        BuildFakeProfileOAuth2TokenService);
    profile_builder.AddTestingFactory(SigninManagerFactory::GetInstance(),
                                      BuildFakeSigninManagerBase);

    profile_ = profile_builder.Build();

    FakeProfileOAuth2TokenService* token_service =
        static_cast<FakeProfileOAuth2TokenService*>(
            ProfileOAuth2TokenServiceFactory::GetForProfile(profile()));
    token_service->UpdateCredentials(kFakeUserName, kRefreshToken);
    token_service->set_auto_post_fetch_response_on_message_loop(true);

    FakeSigninManagerBase* signin_manager = static_cast<FakeSigninManagerBase*>(
        SigninManagerFactory::GetForProfile(profile()));
    signin_manager->SetAuthenticatedAccountInfo(kFakeGaiaId, kFakeUserName);

    profile()->GetPrefs()->SetBoolean(prefs::kArcSignedIn, true);
    profile()->GetPrefs()->SetBoolean(prefs::kArcTermsAccepted, true);

    const AccountId account_id(
        AccountId::FromUserEmailGaiaId(kFakeUserName, kFakeGaiaId));
    GetFakeUserManager()->AddUser(account_id);
    GetFakeUserManager()->LoginUser(account_id);
    GetFakeUserManager()->CreateLocalState();

    // Set up ARC for test profile.
    // Currently, ArcSessionManager is singleton and set up with the original
    // Profile instance. This re-initializes the ArcServiceLauncher by
    // overwriting Profile with profile().
    // TODO(hidehiko): This way several ArcService instances created with
    // the original Profile instance on Browser creatuion are kept in the
    // ArcServiceManager. For proper overwriting, those should be removed.
    ArcServiceLauncher::Get()->OnPrimaryUserProfilePrepared(profile());

    // It is non-trivial to navigate through the merge session in a testing
    // context; currently we just skip it.
    // TODO(blundell): Figure out how to enable this flow.
    ArcSessionManager::Get()->auth_context()->SkipMergeSessionForTesting();

    user_manager::known_user::SetDeviceId(
        multi_user_util::GetAccountIdFromProfile(profile()), "dummy");
  }

  void TearDownOnMainThread() override {
    // Explicitly removing the user is required; otherwise ProfileHelper keeps
    // a dangling pointer to the User.
    // TODO(nya): Consider removing all users from ProfileHelper in the
    // destructor of FakeChromeUserManager.
    const AccountId account_id(
        AccountId::FromUserEmailGaiaId(kFakeUserName, kFakeGaiaId));
    GetFakeUserManager()->RemoveUserFromList(account_id);
    // Since ArcServiceLauncher is (re-)set up with profile() in
    // SetUpOnMainThread() it is necessary to Shutdown() before the profile()
    // is destroyed. ArcServiceLauncher::Shutdown() will be called again on
    // fixture destruction (because it is initialized with the original Profile
    // instance in fixture, once), but it should be no op.
    // TODO(hidehiko): Think about a way to test the code cleanly.
    ArcServiceLauncher::Get()->Shutdown();
    profile_.reset();
    user_manager_enabler_.reset();
  }

  chromeos::FakeChromeUserManager* GetFakeUserManager() const {
    return static_cast<chromeos::FakeChromeUserManager*>(
        user_manager::UserManager::Get());
  }

  void set_profile_name(const std::string& username) {
    profile_->set_profile_name(username);
  }

  Profile* profile() { return profile_.get(); }

 private:
  std::unique_ptr<chromeos::ScopedUserManagerEnabler> user_manager_enabler_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<TestingProfile> profile_;

  DISALLOW_COPY_AND_ASSIGN(ArcAuthServiceTest);
};

// Tests that when ARC requests account info for a non-managed account and
// the system is in silent authentication mode, Chrome supplies the info
// configured in SetUpOnMainThread() above.
IN_PROC_BROWSER_TEST_F(ArcAuthServiceTest, SuccessfulBackgroundFetch) {
  ASSERT_TRUE(base::FeatureList::IsEnabled(arc::kArcUseAuthEndpointFeature));

  net::FakeURLFetcherFactory factory(nullptr);
  factory.SetFakeResponse(
      GURL(arc::kAuthTokenExchangeEndPoint),
      "{ \"token\" : \"" + std::string(kFakeAuthCode) + "\" }", net::HTTP_OK,
      net::URLRequestStatus::SUCCESS);

  FakeAuthInstance auth_instance;
  ArcAuthService* auth_service =
      ArcServiceManager::GetGlobalService<ArcAuthService>();
  ASSERT_TRUE(auth_service);

  ArcBridgeService* arc_bridge_service =
      ArcServiceManager::Get()->arc_bridge_service();
  ASSERT_TRUE(arc_bridge_service);
  arc_bridge_service->auth()->SetInstance(&auth_instance);

  base::RunLoop run_loop;
  auth_instance.RequestAccountInfo(run_loop.QuitClosure());
  run_loop.Run();

  EXPECT_TRUE(auth_instance.account_info());
  EXPECT_EQ(kFakeUserName, auth_instance.account_info()->account_name.value());
  EXPECT_EQ(kFakeAuthCode, auth_instance.account_info()->auth_code.value());
  EXPECT_EQ(mojom::ChromeAccountType::USER_ACCOUNT,
            auth_instance.account_info()->account_type);
  EXPECT_FALSE(auth_instance.account_info()->enrollment_token);
  EXPECT_FALSE(auth_instance.account_info()->is_managed);
}

}  // namespace arc
