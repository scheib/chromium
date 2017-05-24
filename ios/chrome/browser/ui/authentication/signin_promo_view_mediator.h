// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_AUTHENTICATION_SIGNIN_PROMO_VIEW_MEDIATOR_H_
#define IOS_CHROME_BROWSER_UI_AUTHENTICATION_SIGNIN_PROMO_VIEW_MEDIATOR_H_

#import <Foundation/Foundation.h>

#import "ios/chrome/browser/ui/authentication/signin_promo_view.h"
#import "ios/chrome/browser/ui/authentication/signin_promo_view_delegate.h"

@class ChromeIdentity;
@class SigninPromoViewConfigurator;
@protocol SigninPromoViewConsumer;

// Class that monitors the available identities and creates
// SigninPromoViewConfigurator. This class makes the link between the model and
// the view. The consumer will receive notification if default identity is
// changed or updated.
@interface SigninPromoViewMediator : NSObject<SigninPromoViewDelegate>

// Consumer to handle identity update notifications.
@property(nonatomic, weak) id<SigninPromoViewConsumer> consumer;

// Chrome identity used to configure the view in a warm state mode. Otherwise
// contains nil.
@property(nonatomic, readonly, strong) ChromeIdentity* defaultIdentity;

// Access point used to send user action metrics.
@property(nonatomic) signin_metrics::AccessPoint accessPoint;

- (SigninPromoViewConfigurator*)createConfigurator;

@end

#endif  // IOS_CHROME_BROWSER_UI_AUTHENTICATION_SIGNIN_PROMO_VIEW_MEDIATOR_H_
