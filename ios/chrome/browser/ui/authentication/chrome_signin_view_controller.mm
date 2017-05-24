// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/ui/authentication/chrome_signin_view_controller.h"

#include <stdint.h>
#include <cmath>
#include <memory>

#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/QuartzCore.h>

#import "base/ios/block_types.h"
#import "base/ios/ios_util.h"
#import "base/ios/weak_nsobject.h"
#import "base/mac/bind_objc_block.h"
#import "base/mac/scoped_nsobject.h"
#include "base/metrics/user_metrics.h"
#import "base/strings/sys_string_conversions.h"
#include "base/timer/elapsed_timer.h"
#include "base/timer/timer.h"
#include "components/signin/core/browser/signin_metrics.h"
#include "components/strings/grit/components_strings.h"
#import "ios/chrome/browser/browser_state/chrome_browser_state.h"
#include "ios/chrome/browser/chrome_url_constants.h"
#import "ios/chrome/browser/signin/authentication_service.h"
#import "ios/chrome/browser/signin/authentication_service_factory.h"
#import "ios/chrome/browser/signin/chrome_identity_service_observer_bridge.h"
#include "ios/chrome/browser/signin/signin_util.h"
#import "ios/chrome/browser/sync/sync_setup_service.h"
#import "ios/chrome/browser/sync/sync_setup_service_factory.h"
#import "ios/chrome/browser/ui/alert_coordinator/alert_coordinator.h"
#import "ios/chrome/browser/ui/authentication/authentication_flow.h"
#import "ios/chrome/browser/ui/authentication/authentication_ui_util.h"
#include "ios/chrome/browser/ui/authentication/signin_account_selector_view_controller.h"
#include "ios/chrome/browser/ui/authentication/signin_confirmation_view_controller.h"
#import "ios/chrome/browser/ui/colors/MDCPalette+CrAdditions.h"
#import "ios/chrome/browser/ui/commands/UIKit+ChromeExecuteCommand.h"
#import "ios/chrome/browser/ui/commands/generic_chrome_command.h"
#include "ios/chrome/browser/ui/commands/ios_command_ids.h"
#import "ios/chrome/browser/ui/rtl_geometry.h"
#import "ios/chrome/browser/ui/ui_util.h"
#import "ios/chrome/browser/ui/uikit_ui_util.h"
#import "ios/chrome/browser/ui/util/label_link_controller.h"
#include "ios/chrome/common/string_util.h"
#include "ios/chrome/grit/ios_chromium_strings.h"
#include "ios/chrome/grit/ios_strings.h"
#import "ios/public/provider/chrome/browser/chrome_browser_provider.h"
#import "ios/public/provider/chrome/browser/signin/chrome_identity.h"
#import "ios/public/provider/chrome/browser/signin/chrome_identity_interaction_manager.h"
#import "ios/public/provider/chrome/browser/signin/chrome_identity_service.h"
#import "ios/third_party/material_components_ios/src/components/ActivityIndicator/src/MaterialActivityIndicator.h"
#import "ios/third_party/material_components_ios/src/components/Buttons/src/MaterialButtons.h"
#import "ios/third_party/material_components_ios/src/components/Palettes/src/MaterialPalettes.h"
#import "ios/third_party/material_roboto_font_loader_ios/src/src/MaterialRobotoFontLoader.h"
#import "ui/base/l10n/l10n_util.h"

namespace {

// Default animation duration.
const CGFloat kAnimationDuration = 0.5f;

enum LayoutType {
  LAYOUT_REGULAR,
  LAYOUT_COMPACT,
};

// Alpha threshold upon which a view is considered hidden.
const CGFloat kHiddenAlphaThreshold = 0.1;

// Minimum duration of the pending state in milliseconds.
const int64_t kMinimunPendingStateDurationMs = 300;

// Internal padding between the title and image in the "More" button.
const CGFloat kMoreButtonPadding = 5.0f;

struct AuthenticationViewConstants {
  CGFloat PrimaryFontSize;
  CGFloat SecondaryFontSize;
  CGFloat GradientHeight;
  CGFloat ButtonHeight;
  CGFloat ButtonHorizontalPadding;
  CGFloat ButtonVerticalPadding;
};

const AuthenticationViewConstants kCompactConstants = {
    24,  // PrimaryFontSize
    14,  // SecondaryFontSize
    40,  // GradientHeight
    36,  // ButtonHeight
    32,  // ButtonHorizontalPadding
    32,  // ButtonVerticalPadding
};

const AuthenticationViewConstants kRegularConstants = {
    1.5 * kCompactConstants.PrimaryFontSize,
    1.5 * kCompactConstants.SecondaryFontSize,
    kCompactConstants.GradientHeight,
    1.5 * kCompactConstants.ButtonHeight,
    kCompactConstants.ButtonHorizontalPadding,
    kCompactConstants.ButtonVerticalPadding,
};

enum AuthenticationState {
  NULL_STATE,
  IDENTITY_PICKER_STATE,
  SIGNIN_PENDING_STATE,
  IDENTITY_SELECTED_STATE,
  DONE_STATE,
};

// Fades in |button| on screen if not already visible.
void ShowButton(UIButton* button) {
  if (button.alpha > kHiddenAlphaThreshold)
    return;
  button.alpha = 1.0;
}

// Fades out |button| on screen if not already hidden.
void HideButton(UIButton* button) {
  if (button.alpha < kHiddenAlphaThreshold)
    return;
  button.alpha = 0.0;
}

}  // namespace

@interface ChromeSigninViewController ()<
    ChromeIdentityInteractionManagerDelegate,
    ChromeIdentityServiceObserver,
    SigninAccountSelectorViewControllerDelegate,
    SigninConfirmationViewControllerDelegate,
    MDCActivityIndicatorDelegate>
@property(nonatomic, retain) ChromeIdentity* selectedIdentity;

@end

@implementation ChromeSigninViewController {
  ios::ChromeBrowserState* _browserState;  // weak
  base::WeakNSProtocol<id<ChromeSigninViewControllerDelegate>> _delegate;
  std::unique_ptr<ChromeIdentityServiceObserverBridge> _identityServiceObserver;
  base::scoped_nsobject<ChromeIdentity> _selectedIdentity;

  // Authentication
  base::scoped_nsobject<AlertCoordinator> _alertCoordinator;
  base::scoped_nsobject<AuthenticationFlow> _authenticationFlow;
  BOOL _addedAccount;
  BOOL _autoSignIn;
  BOOL _didSignIn;
  BOOL _didAcceptSignIn;
  BOOL _didFinishSignIn;
  BOOL _isPresentedOnSettings;
  signin_metrics::AccessPoint _accessPoint;
  signin_metrics::PromoAction _promoAction;
  base::scoped_nsobject<ChromeIdentityInteractionManager> _interactionManager;

  // Basic state.
  AuthenticationState _currentState;
  BOOL _ongoingStateChange;
  base::scoped_nsobject<MDCActivityIndicator> _activityIndicator;
  base::scoped_nsobject<MDCButton> _primaryButton;
  base::scoped_nsobject<MDCButton> _secondaryButton;
  base::scoped_nsobject<UIView> _gradientView;
  base::scoped_nsobject<CAGradientLayer> _gradientLayer;

  // Identity picker state.
  base::scoped_nsobject<SigninAccountSelectorViewController> _accountSelectorVC;

  // Signin pending state.
  AuthenticationState _activityIndicatorNextState;
  std::unique_ptr<base::ElapsedTimer> _pendingStateTimer;
  std::unique_ptr<base::Timer> _leavingPendingStateTimer;

  // Identity selected state.
  base::scoped_nsobject<SigninConfirmationViewController> _confirmationVC;
  BOOL _hasConfirmationScreenReachedBottom;
}

@synthesize shouldClearData = _shouldClearData;

- (instancetype)initWithBrowserState:(ios::ChromeBrowserState*)browserState
               isPresentedOnSettings:(BOOL)isPresentedOnSettings
                         accessPoint:(signin_metrics::AccessPoint)accessPoint
                         promoAction:(signin_metrics::PromoAction)promoAction
                      signInIdentity:(ChromeIdentity*)identity {
  self = [super init];
  if (self) {
    _browserState = browserState;
    _isPresentedOnSettings = isPresentedOnSettings;
    _accessPoint = accessPoint;
    _promoAction = promoAction;

    if (identity) {
      _autoSignIn = YES;
      [self setSelectedIdentity:identity];
    }
    _identityServiceObserver.reset(
        new ChromeIdentityServiceObserverBridge(self));
    _currentState = NULL_STATE;
  }
  return self;
}

- (void)dealloc {
  // The call to -[UIControl addTarget:action:forControlEvents:] is made just
  // after the creation of those objects, so if the objects are not nil, then
  // it is safe to call -[UIControl removeTarget:action:forControlEvents:].
  // If they are nil, then the call does nothing.
  [_primaryButton removeTarget:self
                        action:@selector(onPrimaryButtonPressed:)
              forControlEvents:UIControlEventTouchDown];
  [_secondaryButton removeTarget:self
                          action:@selector(onSecondaryButtonPressed:)
                forControlEvents:UIControlEventTouchDown];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [super dealloc];
}

- (void)cancel {
  if (_alertCoordinator) {
    DCHECK(!_authenticationFlow && !_interactionManager);
    [_alertCoordinator executeCancelHandler];
    [_alertCoordinator stop];
  }
  if (_interactionManager) {
    DCHECK(!_alertCoordinator && !_authenticationFlow);
    [_interactionManager cancelAndDismissAnimated:NO];
  }
  if (_authenticationFlow) {
    DCHECK(!_alertCoordinator && !_interactionManager);
    [_authenticationFlow cancelAndDismiss];
  }
  if (!_didAcceptSignIn && _didSignIn) {
    AuthenticationServiceFactory::GetForBrowserState(_browserState)
        ->SignOut(signin_metrics::ABORT_SIGNIN, nil);
    _didSignIn = NO;
  }
  if (!_didFinishSignIn) {
    _didFinishSignIn = YES;
    [_delegate didFailSignIn:self];
  }
}

- (void)acceptSignInAndExecuteCommand:(GenericChromeCommand*)command {
  signin_metrics::LogSigninAccessPointCompleted(_accessPoint, _promoAction);
  _didAcceptSignIn = YES;
  if (!_didFinishSignIn) {
    _didFinishSignIn = YES;
    [_delegate didAcceptSignIn:self executeCommand:command];
  }
}

- (void)acceptSignInAndCommitSyncChanges {
  DCHECK(_didSignIn);
  SyncSetupServiceFactory::GetForBrowserState(_browserState)->CommitChanges();
  [self acceptSignInAndExecuteCommand:nil];
}

- (void)setPrimaryButtonStyling:(MDCButton*)button {
  [button setBackgroundColor:[[MDCPalette cr_bluePalette] tint500]
                    forState:UIControlStateNormal];
  [button setCustomTitleColor:[UIColor whiteColor]];
  [button setUnderlyingColorHint:[UIColor blackColor]];
  [button setInkColor:[UIColor colorWithWhite:1 alpha:0.2f]];
}

- (void)setSecondaryButtonStyling:(MDCButton*)button {
  [button setBackgroundColor:self.backgroundColor
                    forState:UIControlStateNormal];
  [button setCustomTitleColor:[[MDCPalette cr_bluePalette] tint500]];
  [button setUnderlyingColorHint:[UIColor whiteColor]];
  [button setInkColor:[UIColor colorWithWhite:0 alpha:0.06f]];
}

#pragma mark - Accessibility

- (BOOL)accessibilityPerformEscape {
  // Simulate a press on the secondary button.
  [self onSecondaryButtonPressed:self];
  return YES;
}

#pragma mark - Properties

- (ios::ChromeBrowserState*)browserState {
  return _browserState;
}

- (id<ChromeSigninViewControllerDelegate>)delegate {
  return _delegate;
}

- (void)setDelegate:(id<ChromeSigninViewControllerDelegate>)delegate {
  _delegate.reset(delegate);
}

- (UIColor*)backgroundColor {
  return [[MDCPalette greyPalette] tint50];
}

- (NSString*)identityPickerTitle {
  return l10n_util::GetNSString(IDS_IOS_ACCOUNT_CONSISTENCY_SETUP_TITLE);
}

- (NSString*)acceptSigninButtonTitle {
  return l10n_util::GetNSString(
      IDS_IOS_ACCOUNT_CONSISTENCY_CONFIRMATION_OK_BUTTON);
}

- (NSString*)skipSigninButtonTitle {
  return l10n_util::GetNSString(IDS_IOS_ACCOUNT_CONSISTENCY_SETUP_SKIP_BUTTON);
}

- (UIButton*)primaryButton {
  return _primaryButton;
}

- (UIButton*)secondaryButton {
  return _secondaryButton;
}

- (void)setSelectedIdentity:(ChromeIdentity*)identity {
  DCHECK(identity || (IDENTITY_PICKER_STATE == _currentState));
  _selectedIdentity.reset([identity retain]);
}

- (ChromeIdentity*)selectedIdentity {
  return _selectedIdentity;
}

#pragma mark - Authentication

- (void)handleAuthenticationError:(NSError*)error {
  // Filter out cancel and errors handled internally by ChromeIdentity.
  if (!ShouldHandleSigninError(error)) {
    return;
  }
  _alertCoordinator.reset(
      [ios_internal::ErrorCoordinator(error, nil, self) retain]);
  [_alertCoordinator start];
}

- (void)signIntoIdentity:(ChromeIdentity*)identity {
  [_delegate willStartSignIn:self];
  DCHECK(!_authenticationFlow);
  _authenticationFlow.reset([[AuthenticationFlow alloc]
          initWithBrowserState:_browserState
                      identity:identity
               shouldClearData:_shouldClearData
              postSignInAction:POST_SIGNIN_ACTION_NONE
      presentingViewController:self]);
  base::WeakNSObject<ChromeSigninViewController> weakSelf(self);
  [_authenticationFlow startSignInWithCompletion:^(BOOL success) {
    [weakSelf onAccountSigninCompletion:success];
  }];
}

- (void)openAuthenticationDialogAddIdentity {
  DCHECK(!_interactionManager);
  _interactionManager =
      ios::GetChromeBrowserProvider()
          ->GetChromeIdentityService()
          ->NewChromeIdentityInteractionManager(_browserState, self);
  base::WeakNSObject<ChromeSigninViewController> weakSelf(self);
  SigninCompletionCallback completion =
      ^(ChromeIdentity* identity, NSError* error) {
        base::scoped_nsobject<ChromeSigninViewController> strongSelf(
            [weakSelf retain]);
        if (!strongSelf || !strongSelf.get()->_interactionManager)
          return;
        // The ChromeIdentityInteractionManager is not used anymore at this
        // point.
        strongSelf.get()->_interactionManager.reset();

        if (error) {
          [strongSelf handleAuthenticationError:error];
          return;
        }
        strongSelf.get()->_addedAccount = YES;
        [strongSelf onIdentityListChanged];
        [strongSelf setSelectedIdentity:identity];
        [strongSelf changeToState:SIGNIN_PENDING_STATE];
      };
  [_delegate willStartAddAccount:self];
  [_interactionManager addAccountWithCompletion:completion];
}

- (void)onAccountSigninCompletion:(BOOL)success {
  _authenticationFlow.reset();
  if (success) {
    DCHECK(!_didSignIn);
    _didSignIn = YES;
    [_delegate didSignIn:self];
    [self changeToState:IDENTITY_SELECTED_STATE];
  } else {
    [self changeToState:IDENTITY_PICKER_STATE];
  }
}

- (void)undoSignIn {
  if (_didSignIn) {
    AuthenticationServiceFactory::GetForBrowserState(_browserState)
        ->SignOut(signin_metrics::ABORT_SIGNIN, nil);
    [_delegate didUndoSignIn:self identity:self.selectedIdentity];
    _didSignIn = NO;
  }
  if (_addedAccount) {
    // This is best effort. If the operation fails, the account will be left on
    // the device. The user will not be warned either as this call is
    // asynchronous (but undo is not), the application might be in an unknown
    // state when the forget identity operation finishes.
    ios::GetChromeBrowserProvider()->GetChromeIdentityService()->ForgetIdentity(
        self.selectedIdentity, nil);
  }
  _addedAccount = NO;
}

#pragma mark - State machine

- (void)enterState:(AuthenticationState)state {
  _ongoingStateChange = NO;
  if (_didFinishSignIn) {
    // Stop the state machine when the sign-in is done.
    _currentState = DONE_STATE;
    return;
  }
  _currentState = state;
  switch (state) {
    case NULL_STATE:
      NOTREACHED();
      break;
    case IDENTITY_PICKER_STATE:
      [self enterIdentityPickerState];
      break;
    case SIGNIN_PENDING_STATE:
      [self enterSigninPendingState];
      break;
    case IDENTITY_SELECTED_STATE:
      [self enterIdentitySelectedState];
      break;
    case DONE_STATE:
      break;
  }
}

- (void)changeToState:(AuthenticationState)nextState {
  if (_currentState == nextState)
    return;
  _ongoingStateChange = YES;
  switch (_currentState) {
    case NULL_STATE:
      DCHECK_NE(IDENTITY_SELECTED_STATE, nextState);
      [self enterState:nextState];
      return;
    case IDENTITY_PICKER_STATE:
      DCHECK_EQ(SIGNIN_PENDING_STATE, nextState);
      [self leaveIdentityPickerState:nextState];
      return;
    case SIGNIN_PENDING_STATE:
      [self leaveSigninPendingState:nextState];
      return;
    case IDENTITY_SELECTED_STATE:
      DCHECK_EQ(IDENTITY_PICKER_STATE, nextState);
      [self leaveIdentitySelectedState:nextState];
      return;
    case DONE_STATE:
      // Ignored
      return;
  }
  NOTREACHED();
}

#pragma mark - IdentityPickerState

- (void)updatePrimaryButtonTitle {
  bool hasIdentities = ios::GetChromeBrowserProvider()
                           ->GetChromeIdentityService()
                           ->HasIdentities();
  NSString* primaryButtonTitle =
      hasIdentities
          ? l10n_util::GetNSString(
                IDS_IOS_ACCOUNT_CONSISTENCY_SETUP_SIGNIN_BUTTON)
          : l10n_util::GetNSString(
                IDS_IOS_ACCOUNT_CONSISTENCY_SETUP_SIGNIN_NO_ACCOUNT_BUTTON);
  [_primaryButton setTitle:primaryButtonTitle forState:UIControlStateNormal];
  [_primaryButton setImage:nil forState:UIControlStateNormal];
  [self.view setNeedsLayout];
}

- (void)enterIdentityPickerState {
  // Reset the selected identity.
  [self setSelectedIdentity:nil];

  // Add the account selector view controller.
  _accountSelectorVC.reset([[SigninAccountSelectorViewController alloc] init]);
  _accountSelectorVC.get().delegate = self;
  [_accountSelectorVC willMoveToParentViewController:self];
  [self addChildViewController:_accountSelectorVC];
  _accountSelectorVC.get().view.frame = self.view.bounds;
  [self.view insertSubview:_accountSelectorVC.get().view
              belowSubview:_primaryButton];
  [_accountSelectorVC didMoveToParentViewController:self];

  // Update the button title.
  [self updatePrimaryButtonTitle];
  [_secondaryButton setTitle:self.skipSigninButtonTitle
                    forState:UIControlStateNormal];
  [self.view setNeedsLayout];

  HideButton(_primaryButton);
  HideButton(_secondaryButton);
  [UIView animateWithDuration:kAnimationDuration
                   animations:^{
                     ShowButton(_primaryButton);
                     ShowButton(_secondaryButton);
                   }];
}

- (void)reloadIdentityPickerState {
  // The account selector view controller reloads itself each time the list
  // of identities changes, thus there is no need to reload it.

  [self updatePrimaryButtonTitle];
}

- (void)leaveIdentityPickerState:(AuthenticationState)nextState {
  [UIView animateWithDuration:kAnimationDuration
      animations:^{
        HideButton(_primaryButton);
        HideButton(_secondaryButton);
      }
      completion:^(BOOL finished) {
        [_accountSelectorVC willMoveToParentViewController:nil];
        [[_accountSelectorVC view] removeFromSuperview];
        [_accountSelectorVC removeFromParentViewController];
        _accountSelectorVC.reset();
        [self enterState:nextState];
      }];
}

#pragma mark - SigninPendingState

- (void)enterSigninPendingState {
  [_secondaryButton setTitle:l10n_util::GetNSString(IDS_CANCEL)
                    forState:UIControlStateNormal];
  [self.view setNeedsLayout];

  _pendingStateTimer.reset(new base::ElapsedTimer());
  ShowButton(_secondaryButton);
  [_activityIndicator startAnimating];

  [self signIntoIdentity:self.selectedIdentity];
}

- (void)reloadSigninPendingState {
  BOOL isSelectedIdentityValid = ios::GetChromeBrowserProvider()
                                     ->GetChromeIdentityService()
                                     ->IsValidIdentity(self.selectedIdentity);
  if (!isSelectedIdentityValid) {
    [_authenticationFlow cancelAndDismiss];
    [self changeToState:IDENTITY_PICKER_STATE];
  }
}

- (void)leaveSigninPendingState:(AuthenticationState)nextState {
  if (!_pendingStateTimer) {
    // The controller is already leaving the signin pending state, simply update
    // the new state to take into account the last request only.
    _activityIndicatorNextState = nextState;
    return;
  }

  _activityIndicatorNextState = nextState;
  _activityIndicator.get().delegate = self;

  base::TimeDelta remainingTime =
      base::TimeDelta::FromMilliseconds(kMinimunPendingStateDurationMs) -
      _pendingStateTimer->Elapsed();
  _pendingStateTimer.reset();

  if (remainingTime.InMilliseconds() < 0) {
    [_activityIndicator stopAnimating];
  } else {
    // If the signin pending state is too fast, the screen will appear to
    // flicker. Make sure to animate for at least
    // |kMinimunPendingStateDurationMs| milliseconds.
    base::WeakNSObject<ChromeSigninViewController> weakSelf(self);
    ProceduralBlock completionBlock = ^{
      base::scoped_nsobject<ChromeSigninViewController> strongSelf(
          [weakSelf retain]);
      if (!strongSelf)
        return;
      [strongSelf.get()->_activityIndicator stopAnimating];
      strongSelf.get()->_leavingPendingStateTimer.reset();
    };
    _leavingPendingStateTimer.reset(new base::Timer(false, false));
    _leavingPendingStateTimer->Start(FROM_HERE, remainingTime,
                                     base::BindBlock(completionBlock));
  }
}

#pragma mark - IdentitySelectedState

- (void)enterIdentitySelectedState {
  _confirmationVC.reset([[SigninConfirmationViewController alloc]
      initWithIdentity:self.selectedIdentity]);
  _confirmationVC.get().delegate = self;

  _hasConfirmationScreenReachedBottom = NO;
  [_confirmationVC willMoveToParentViewController:self];
  [self addChildViewController:_confirmationVC];
  _confirmationVC.get().view.frame = self.view.bounds;
  [self.view insertSubview:_confirmationVC.get().view
              belowSubview:_primaryButton];
  [_confirmationVC didMoveToParentViewController:self];

  [self setSecondaryButtonStyling:_primaryButton];
  NSString* primaryButtonTitle = l10n_util::GetNSString(
      IDS_IOS_ACCOUNT_CONSISTENCY_CONFIRMATION_SCROLL_BUTTON);
  [_primaryButton setTitle:primaryButtonTitle forState:UIControlStateNormal];
  [_primaryButton setImage:[UIImage imageNamed:@"signin_confirmation_more"]
                  forState:UIControlStateNormal];
  NSString* secondaryButtonTitle = l10n_util::GetNSString(
      IDS_IOS_ACCOUNT_CONSISTENCY_CONFIRMATION_UNDO_BUTTON);
  [_secondaryButton setTitle:secondaryButtonTitle
                    forState:UIControlStateNormal];
  [self.view setNeedsLayout];

  HideButton(_primaryButton);
  HideButton(_secondaryButton);
  [UIView animateWithDuration:kAnimationDuration
                   animations:^{
                     ShowButton(_primaryButton);
                     ShowButton(_secondaryButton);
                   }
                   completion:nil];
}

- (void)reloadIdentitySelectedState {
  BOOL isSelectedIdentityValid = ios::GetChromeBrowserProvider()
                                     ->GetChromeIdentityService()
                                     ->IsValidIdentity(self.selectedIdentity);
  if (!isSelectedIdentityValid) {
    [self changeToState:IDENTITY_PICKER_STATE];
    return;
  }
}

- (void)leaveIdentitySelectedState:(AuthenticationState)nextState {
  [_confirmationVC willMoveToParentViewController:nil];
  [[_confirmationVC view] removeFromSuperview];
  [_confirmationVC removeFromParentViewController];
  _confirmationVC.reset();
  [self setPrimaryButtonStyling:_primaryButton];
  HideButton(_primaryButton);
  HideButton(_secondaryButton);
  [self undoSignIn];
  [self enterState:nextState];
}

#pragma mark - UIViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = self.backgroundColor;

  _primaryButton.reset([[MDCFlatButton alloc] init]);
  [self setPrimaryButtonStyling:_primaryButton];
  [_primaryButton addTarget:self
                     action:@selector(onPrimaryButtonPressed:)
           forControlEvents:UIControlEventTouchUpInside];
  HideButton(_primaryButton);
  [self.view addSubview:_primaryButton];

  _secondaryButton.reset([[MDCFlatButton alloc] init]);
  [self setSecondaryButtonStyling:_secondaryButton];
  [_secondaryButton addTarget:self
                       action:@selector(onSecondaryButtonPressed:)
             forControlEvents:UIControlEventTouchUpInside];
  [_secondaryButton setAccessibilityIdentifier:@"ic_close"];
  HideButton(_secondaryButton);
  [self.view addSubview:_secondaryButton];

  _activityIndicator.reset(
      [[MDCActivityIndicator alloc] initWithFrame:CGRectZero]);
  [_activityIndicator setDelegate:self];
  [_activityIndicator setStrokeWidth:3];
  [_activityIndicator
      setCycleColors:@[ [[MDCPalette cr_bluePalette] tint500] ]];
  [self.view addSubview:_activityIndicator];

  _gradientView.reset([[UIView alloc] initWithFrame:CGRectZero]);
  _gradientLayer.reset([[CAGradientLayer layer] retain]);
  _gradientLayer.get().colors = [NSArray
      arrayWithObjects:(id)[[UIColor colorWithWhite:1 alpha:0] CGColor],
                       (id)[self.backgroundColor CGColor], nil];
  [[_gradientView layer] insertSublayer:_gradientLayer atIndex:0];
  [self.view addSubview:_gradientView];
}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];

  if (_currentState != NULL_STATE) {
    return;
  }
  if (_autoSignIn) {
    [self enterState:SIGNIN_PENDING_STATE];
  } else {
    [self enterState:IDENTITY_PICKER_STATE];
  }
}

#pragma mark - Events

- (void)onPrimaryButtonPressed:(id)sender {
  switch (_currentState) {
    case NULL_STATE:
      NOTREACHED();
      return;
    case IDENTITY_PICKER_STATE: {
      if (_interactionManager) {
        // Adding an account is ongoing, ignore the button press.
        return;
      }
      ChromeIdentity* selectedIdentity = [_accountSelectorVC selectedIdentity];
      [self setSelectedIdentity:selectedIdentity];
      if (selectedIdentity) {
        [self changeToState:SIGNIN_PENDING_STATE];
      } else {
        [self openAuthenticationDialogAddIdentity];
      }
      return;
    }
    case SIGNIN_PENDING_STATE:
      NOTREACHED();
      return;
    case IDENTITY_SELECTED_STATE:
      if (_hasConfirmationScreenReachedBottom) {
        [self acceptSignInAndCommitSyncChanges];
      } else {
        [_confirmationVC scrollToBottom];
      }
      return;
    case DONE_STATE:
      // Ignored
      return;
  }
  NOTREACHED();
}

- (void)onSecondaryButtonPressed:(id)sender {
  switch (_currentState) {
    case NULL_STATE:
      NOTREACHED();
      return;
    case IDENTITY_PICKER_STATE:
      if (!_didFinishSignIn) {
        _didFinishSignIn = YES;
        [_delegate didSkipSignIn:self];
      }
      return;
    case SIGNIN_PENDING_STATE:
      base::RecordAction(base::UserMetricsAction("Signin_Undo_Signin"));
      [_authenticationFlow cancelAndDismiss];
      [self undoSignIn];
      [self changeToState:IDENTITY_PICKER_STATE];
      return;
    case IDENTITY_SELECTED_STATE:
      base::RecordAction(base::UserMetricsAction("Signin_Undo_Signin"));
      [self changeToState:IDENTITY_PICKER_STATE];
      return;
    case DONE_STATE:
      // Ignored
      return;
  }
  NOTREACHED();
}

#pragma mark - ChromeIdentityServiceObserver

- (void)onIdentityListChanged {
  switch (_currentState) {
    case NULL_STATE:
    case DONE_STATE:
      return;
    case IDENTITY_PICKER_STATE:
      [self reloadIdentityPickerState];
      return;
    case SIGNIN_PENDING_STATE:
      [self reloadSigninPendingState];
      return;
    case IDENTITY_SELECTED_STATE:
      [self reloadIdentitySelectedState];
      return;
  }
}

- (void)onChromeIdentityServiceWillBeDestroyed {
  _identityServiceObserver.reset();
}

#pragma mark - Layout

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];

  AuthenticationViewConstants constants;
  if ([self.traitCollection horizontalSizeClass] ==
      UIUserInterfaceSizeClassRegular) {
    constants = kRegularConstants;
  } else {
    constants = kCompactConstants;
  }

  [self layoutButtons:constants];

  CGSize viewSize = self.view.bounds.size;
  CGFloat collectionViewHeight = viewSize.height -
                                 _primaryButton.get().frame.size.height -
                                 constants.ButtonVerticalPadding;
  CGRect collectionViewFrame =
      CGRectMake(0, 0, viewSize.width, collectionViewHeight);
  [_accountSelectorVC.get().view setFrame:collectionViewFrame];
  [_confirmationVC.get().view setFrame:collectionViewFrame];

  // Layout the gradient view right above the buttons.
  CGFloat gradientOriginY = CGRectGetHeight(self.view.bounds) -
                            constants.ButtonVerticalPadding -
                            constants.ButtonHeight - constants.GradientHeight;
  [_gradientView setFrame:CGRectMake(0, gradientOriginY, viewSize.width,
                                     constants.GradientHeight)];
  [_gradientLayer setFrame:[_gradientView bounds]];

  // Layout the activity indicator in the center of the view.
  CGRect bounds = self.view.bounds;
  [_activityIndicator
      setCenter:CGPointMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds))];
}

- (void)layoutButtons:(const AuthenticationViewConstants&)constants {
  [_primaryButton titleLabel].font = [[MDFRobotoFontLoader sharedInstance]
      mediumFontOfSize:constants.SecondaryFontSize];
  [_secondaryButton titleLabel].font = [[MDFRobotoFontLoader sharedInstance]
      mediumFontOfSize:constants.SecondaryFontSize];

  LayoutRect primaryButtonLayout = LayoutRectZero;
  primaryButtonLayout.boundingWidth = CGRectGetWidth(self.view.bounds);
  primaryButtonLayout.size = [_primaryButton
      sizeThatFits:CGSizeMake(CGFLOAT_MAX, constants.ButtonHeight)];
  primaryButtonLayout.position.leading = primaryButtonLayout.boundingWidth -
                                         primaryButtonLayout.size.width -
                                         constants.ButtonHorizontalPadding;
  primaryButtonLayout.position.originY = CGRectGetHeight(self.view.bounds) -
                                         constants.ButtonVerticalPadding -
                                         constants.ButtonHeight;
  primaryButtonLayout.size.height = constants.ButtonHeight;
  [_primaryButton setFrame:LayoutRectGetRect(primaryButtonLayout)];

  UIEdgeInsets imageInsets = UIEdgeInsetsZero;
  UIEdgeInsets titleInsets = UIEdgeInsetsZero;
  if ([_primaryButton imageForState:UIControlStateNormal]) {
    // Title label should be leading, followed by the image (with some padding).
    CGFloat paddedImageWidth =
        [_primaryButton imageView].frame.size.width + kMoreButtonPadding;
    CGFloat paddedTitleWidth =
        [_primaryButton titleLabel].frame.size.width + kMoreButtonPadding;
    imageInsets = UIEdgeInsetsMake(0, paddedTitleWidth, 0, -paddedTitleWidth);
    titleInsets = UIEdgeInsetsMake(0, -paddedImageWidth, 0, paddedImageWidth);
  }
  [_primaryButton setImageEdgeInsets:imageInsets];
  [_primaryButton setTitleEdgeInsets:titleInsets];

  LayoutRect secondaryButtonLayout = primaryButtonLayout;
  secondaryButtonLayout.size = [_secondaryButton
      sizeThatFits:CGSizeMake(CGFLOAT_MAX, constants.ButtonHeight)];
  secondaryButtonLayout.position.leading = constants.ButtonHorizontalPadding;
  secondaryButtonLayout.size.height = constants.ButtonHeight;
  [_secondaryButton setFrame:LayoutRectGetRect(secondaryButtonLayout)];
}

#pragma mark - MDCActivityIndicatorDelegate

- (void)activityIndicatorAnimationDidFinish:
    (MDCActivityIndicator*)activityIndicator {
  DCHECK_EQ(SIGNIN_PENDING_STATE, _currentState);
  DCHECK_EQ(_activityIndicator, activityIndicator);

  // The activity indicator is only used in the signin pending state. Its
  // animation is stopped only when leaving the state.
  if (_activityIndicatorNextState != NULL_STATE) {
    [self enterState:_activityIndicatorNextState];
    _activityIndicatorNextState = NULL_STATE;
  }
}

#pragma mark - ChromeIdentityInteractionManagerDelegate

- (void)interactionManager:(ChromeIdentityInteractionManager*)interactionManager
     presentViewController:(UIViewController*)viewController
                  animated:(BOOL)animated
                completion:(ProceduralBlock)completion {
  [self presentViewController:viewController
                     animated:animated
                   completion:completion];
}

- (void)interactionManager:(ChromeIdentityInteractionManager*)interactionManager
    dismissViewControllerAnimated:(BOOL)animated
                       completion:(ProceduralBlock)completion {
  [self dismissViewControllerAnimated:animated completion:completion];
}

#pragma mark - SigninAccountSelectorViewControllerDelegate

- (void)accountSelectorControllerDidSelectAddAccount:
    (SigninAccountSelectorViewController*)accountSelectorController {
  DCHECK_EQ(_accountSelectorVC, accountSelectorController);
  if (_ongoingStateChange) {
    return;
  }
  [self openAuthenticationDialogAddIdentity];
}

#pragma mark - SigninConfirmationViewControllerDelegate

// Callback for when a link in the label is pressed.
- (void)signinConfirmationControllerDidTapSettingsLink:
    (SigninConfirmationViewController*)controller {
  DCHECK_EQ(_confirmationVC, controller);

  base::scoped_nsobject<GenericChromeCommand> command(
      [[GenericChromeCommand alloc] initWithTag:IDC_SHOW_ACCOUNTS_SETTINGS]);
  [self acceptSignInAndExecuteCommand:command];
}

- (void)signinConfirmationControllerDidReachBottom:
    (SigninConfirmationViewController*)controller {
  if (_hasConfirmationScreenReachedBottom) {
    return;
  }
  _hasConfirmationScreenReachedBottom = YES;
  [self setPrimaryButtonStyling:_primaryButton];
  [_primaryButton setTitle:[self acceptSigninButtonTitle]
                  forState:UIControlStateNormal];
  [_primaryButton setImage:nil forState:UIControlStateNormal];
  [self.view setNeedsLayout];
}

@end
