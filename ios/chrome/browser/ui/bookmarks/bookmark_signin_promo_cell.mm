// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/bookmarks/bookmark_signin_promo_cell.h"

#import "ios/chrome/browser/ui/authentication/signin_promo_view.h"
#import "ios/chrome/browser/ui/uikit_ui_util.h"
#include "ios/chrome/grit/ios_chromium_strings.h"
#include "ui/base/l10n/l10n_util.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
// Close button size.
const CGFloat kCloseButtonSize = 24;
}

@implementation BookmarkSigninPromoCell {
  SigninPromoView* _signinPromoView;
  UIButton* _closeButton;
}

@synthesize signinPromoView = _signinPromoView;
@synthesize closeButtonAction = _closeButtonAction;

+ (NSString*)reuseIdentifier {
  return @"BookmarkSigninPromoCell";
}

+ (BOOL)requiresConstraintBasedLayout {
  return YES;
}

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    UIView* contentView = self.contentView;
    _signinPromoView = [[SigninPromoView alloc] initWithFrame:self.bounds];
    _signinPromoView.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:_signinPromoView];
    AddSameConstraints(_signinPromoView, contentView);

    _closeButton = [[UIButton alloc]
        initWithFrame:CGRectMake(0, 0, kCloseButtonSize, kCloseButtonSize)];
    [_closeButton addTarget:self
                     action:@selector(closeButtonAction:)
           forControlEvents:UIControlEventTouchUpInside];
    _closeButton.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:_closeButton];
    [_closeButton setImage:[UIImage imageNamed:@"signin_promo_close_gray"]
                  forState:UIControlStateNormal];
    NSArray* buttonVisualConstraints =
        @[ @"H:[closeButton]-|", @"V:|-[closeButton]" ];
    NSDictionary* views = @{ @"closeButton" : _closeButton };
    ApplyVisualConstraints(buttonVisualConstraints, views);

    _signinPromoView.backgroundColor = [UIColor whiteColor];
    _signinPromoView.textLabel.text =
        l10n_util::GetNSString(IDS_IOS_SIGNIN_PROMO_BOOKMARKS);
  }
  return self;
}

- (void)layoutSubviews {
  // Adjust the text label preferredMaxLayoutWidth according self.frame.width,
  // so the text will adjust its height and not its width.
  CGFloat parentWidth = CGRectGetWidth(self.bounds);
  _signinPromoView.textLabel.preferredMaxLayoutWidth =
      parentWidth - 2 * _signinPromoView.horizontalPadding;

  // Re-layout with the new preferred width to allow the label to adjust its
  // height.
  [super layoutSubviews];
}

- (void)prepareForReuse {
  _closeButtonAction = nil;
  _signinPromoView.delegate = nil;
}

- (void)closeButtonAction:(id)sender {
  if (_closeButtonAction) {
    _closeButtonAction();
  }
}

@end
