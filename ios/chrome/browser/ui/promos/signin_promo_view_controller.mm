// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/promos/signin_promo_view_controller.h"

#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/sys_string_conversions.h"
#include "base/version.h"
#include "components/signin/core/browser/signin_metrics.h"
#include "components/version_info/version_info.h"
#include "ios/chrome/app/tests_hook.h"
#import "ios/chrome/browser/browser_state/chrome_browser_state.h"
#include "ios/chrome/browser/signin/authentication_service.h"
#include "ios/chrome/browser/signin/authentication_service_factory.h"
#import "ios/chrome/browser/ui/commands/UIKit+ChromeExecuteCommand.h"
#include "ios/public/provider/chrome/browser/chrome_browser_provider.h"
#import "ios/public/provider/chrome/browser/signin/chrome_identity_service.h"
#include "net/base/network_change_notifier.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

// Key in the UserDefaults to record the version of the application when the
// SSO Recall promo has been displayed.
NSString* kDisplayedSSORecallForMajorVersionKey =
    @"DisplayedSSORecallForMajorVersionKey";

namespace {

// Key in the UserDefaults to track how many times the SSO Recall promo has been
// displayed.
NSString* kDisplayedSSORecallPromoCountKey = @"DisplayedSSORecallPromoCount";

// Name of the UMA SSO Recall histogram.
const char* const kUMASSORecallPromoAction = "SSORecallPromo.PromoAction";

// Name of the histogram recording how many accounts were avilable on the device
// when the promo was shown.
const char* const kUMASSORecallAccountsAvailable =
    "SSORecallPromo.AccountsAvailable";

// Name of the histogram recording how many times the promo has been shown.
const char* const kUMASSORecallPromoSeenCount = "SSORecallPromo.PromoSeenCount";

// Values of the UMA SSORecallPromo.PromoAction histogram.
enum PromoAction {
  ACTION_DISMISSED,
  ACTION_ENABLED_SSO_ACCOUNT,
  ACTION_ADDED_ANOTHER_ACCOUNT,
  PROMO_ACTION_COUNT
};

}  // namespace

@interface SigninPromoViewController ()<ChromeSigninViewControllerDelegate>
@end

@implementation SigninPromoViewController {
  BOOL _addAccountOperation;
}

- (instancetype)initWithBrowserState:(ios::ChromeBrowserState*)browserState {
  self = [super initWithBrowserState:browserState
               isPresentedOnSettings:NO
                         accessPoint:signin_metrics::AccessPoint::
                                         ACCESS_POINT_SIGNIN_PROMO
                         promoAction:signin_metrics::PromoAction::
                                         PROMO_ACTION_NO_SIGNIN_PROMO
                      signInIdentity:nil];
  if (self) {
    super.delegate = self;
  }
  return self;
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];

  if ([self isBeingPresented] || [self isMovingToParentViewController]) {
    base::RecordAction(
        base::UserMetricsAction("Signin_Signin_FromSigninPromo"));
    signin_metrics::LogSigninAccessPointStarted(
        signin_metrics::AccessPoint::ACCESS_POINT_SIGNIN_PROMO);
  }

  [self recordPromoDisplayed];
}

- (void)dismissWithSignedIn:(BOOL)signedIn
             executeCommand:(GenericChromeCommand*)command {
  DCHECK(self.presentingViewController);
  UIViewController* presentingViewController = self.presentingViewController;
  [presentingViewController
      dismissViewControllerAnimated:YES
                         completion:^{
                           if (command) {
                             [presentingViewController
                                 chromeExecuteCommand:command];
                           }
                         }];
}

// Records in user defaults that the promo has been shown as well as the number
// of times it's been displayed.
- (void)recordPromoDisplayed {
  [[self class] recordVersionSeen];
  NSUserDefaults* standardDefaults = [NSUserDefaults standardUserDefaults];
  int promoSeenCount =
      [standardDefaults integerForKey:kDisplayedSSORecallPromoCountKey];
  promoSeenCount++;
  [standardDefaults setInteger:promoSeenCount
                        forKey:kDisplayedSSORecallPromoCountKey];

  NSArray* identities = ios::GetChromeBrowserProvider()
                            ->GetChromeIdentityService()
                            ->GetAllIdentitiesSortedForDisplay();
  UMA_HISTOGRAM_COUNTS_100(kUMASSORecallAccountsAvailable, [identities count]);
  UMA_HISTOGRAM_COUNTS_100(kUMASSORecallPromoSeenCount, promoSeenCount);
}

// Called to obtain the current version of the application to compare against
// the version the last time the promo was seen.
// Separated out into a discrete function to allow overriding when testing.
+ (void)recordVersionSeen {
  base::Version currentVersion = [self currentVersion];
  NSUserDefaults* standardDefaults = [NSUserDefaults standardUserDefaults];
  [standardDefaults
      setObject:base::SysUTF8ToNSString(currentVersion.GetString())
         forKey:kDisplayedSSORecallForMajorVersionKey];
}

+ (base::Version)currentVersion {
  base::Version currentVersion(version_info::GetVersionNumber());
  DCHECK(currentVersion.IsValid());
  return currentVersion;
}

#pragma mark - PromoViewController

+ (BOOL)shouldBePresentedForBrowserState:
    (ios::ChromeBrowserState*)browserState {
  if (tests_hook::DisableSigninRecallPromo())
    return NO;

  if (browserState->IsOffTheRecord())
    return NO;

  // There will be an error shown if the user chooses to sign in or select
  // another account while offline.
  if (net::NetworkChangeNotifier::IsOffline())
    return NO;

  AuthenticationService* authService =
      AuthenticationServiceFactory::GetForBrowserState(browserState);
  // Do not show the SSO promo if the user is already logged in.
  if (authService->GetAuthenticatedUserEmail())
    return NO;

  // Show the promo at most every two major versions.
  NSUserDefaults* standardDefaults = [NSUserDefaults standardUserDefaults];
  NSString* versionShown =
      [standardDefaults stringForKey:kDisplayedSSORecallForMajorVersionKey];
  if (versionShown) {
    base::Version seenVersion(base::SysNSStringToUTF8(versionShown));
    base::Version currentVersion = [[self class] currentVersion];
    if (currentVersion.components()[0] - seenVersion.components()[0] < 2)
      return NO;
  }

  NSArray* identities = ios::GetChromeBrowserProvider()
                            ->GetChromeIdentityService()
                            ->GetAllIdentitiesSortedForDisplay();
  return [identities count] > 0;
}

+ (UIViewController*)controllerToPresentForBrowserState:
    (ios::ChromeBrowserState*)browserState {
  UIViewController* controller =
      [[SigninPromoViewController alloc] initWithBrowserState:browserState];
  return controller;
}

#pragma mark - ChromeSigninViewControllerDelegate

- (void)willStartSignIn:(ChromeSigninViewController*)controller {
  DCHECK_EQ(self, controller);
  _addAccountOperation = NO;
}

- (void)willStartAddAccount:(ChromeSigninViewController*)controller {
  DCHECK_EQ(self, controller);
  _addAccountOperation = YES;
}

- (void)didSkipSignIn:(ChromeSigninViewController*)controller {
  DCHECK_EQ(self, controller);
  UMA_HISTOGRAM_ENUMERATION(kUMASSORecallPromoAction, ACTION_DISMISSED,
                            PROMO_ACTION_COUNT);
  [self dismissWithSignedIn:NO executeCommand:nil];
}

- (void)didFailSignIn:(ChromeSigninViewController*)controller {
  DCHECK_EQ(self, controller);
  [self dismissWithSignedIn:NO executeCommand:nil];
}

- (void)didSignIn:(ChromeSigninViewController*)controller {
  DCHECK_EQ(self, controller);
}

- (void)didUndoSignIn:(ChromeSigninViewController*)controller
             identity:(ChromeIdentity*)identity {
  DCHECK_EQ(self, controller);
  // No accounts case is impossible in SigninPromoViewController, nothing to do.
}

- (void)didAcceptSignIn:(ChromeSigninViewController*)controller
         executeCommand:(GenericChromeCommand*)command {
  DCHECK_EQ(self, controller);
  PromoAction promoAction = _addAccountOperation ? ACTION_ADDED_ANOTHER_ACCOUNT
                                                 : ACTION_ENABLED_SSO_ACCOUNT;
  UMA_HISTOGRAM_ENUMERATION(kUMASSORecallPromoAction, promoAction,
                            PROMO_ACTION_COUNT);

  [self dismissWithSignedIn:YES executeCommand:command];
}

@end
