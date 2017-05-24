// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_AUTHENTICATION_SIGNIN_INTERACTION_CONTROLLER_H_
#define IOS_CHROME_BROWSER_UI_AUTHENTICATION_SIGNIN_INTERACTION_CONTROLLER_H_

#import <UIKit/UIKit.h>

#import "base/ios/block_types.h"
#include "components/signin/core/browser/signin_metrics.h"
#include "ios/chrome/browser/signin/constants.h"

namespace ios {
class ChromeBrowserState;
}

@class ChromeIdentity;

// Interaction controller for sign-in related operations. This class is mainly a
// proxy for |ChromeSigninViewController|, calling directly
// |ChromeIdentityInteractionManager| for the no-accounts case.
@interface SigninInteractionController : NSObject

// Designated initializer.
// * |browserState| is the current browser state
// * |presentingViewController| is the top presented view controller.
// * |isPresentedOnSettings| indicates whether the settings view controller is
//   part of the presented view controllers stack
// * |accessPoint| represents the access point that initiated the sign-in.
- (instancetype)initWithBrowserState:(ios::ChromeBrowserState*)browserState
            presentingViewController:(UIViewController*)presentingViewController
               isPresentedOnSettings:(BOOL)isPresentedOnSettings
                         accessPoint:(signin_metrics::AccessPoint)accessPoint
                         promoAction:(signin_metrics::PromoAction)promoAction;

// Starts user sign-in.
// * |viewController| is the current view controller.
// * |identity|, if not nil, the user will be signed in without requiring user
//   input, using this Chrome identity.
// * |completion| will be called when the operation is done, and
//   |succeeded| will notify the caller on whether the user is now signed in.
- (void)signInWithViewController:(UIViewController*)viewController
                        identity:(ChromeIdentity*)identity
                      completion:(signin_ui::CompletionCallback)completion;

// Re-authenticate the user. This method will always show a sign-in web flow.
// The completion block will be called when the operation is done, and
// |succeeded| will notify the caller on whether the user has been
// correctly re-authenticated.
- (void)reAuthenticateWithCompletion:(signin_ui::CompletionCallback)completion
                      viewController:(UIViewController*)viewController;

// Starts the flow to add an identity via ChromeIdentityInteractionManager.
- (void)addAccountWithCompletion:(signin_ui::CompletionCallback)completion
                  viewController:(UIViewController*)viewController;

// Cancels any current process. Calls the completion callback when done.
- (void)cancel;

// Cancels any current process and dismisses any UI. Calls the completion
// callback when done.
- (void)cancelAndDismiss;

@end

#endif  // IOS_CHROME_BROWSER_UI_AUTHENTICATION_SIGNIN_INTERACTION_CONTROLLER_H_
