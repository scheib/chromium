// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/authentication/signin_promo_view.h"

#include "base/logging.h"
#import "ios/chrome/browser/ui/authentication/signin_promo_view_delegate.h"
#import "ios/chrome/browser/ui/colors/MDCPalette+CrAdditions.h"
#import "ios/chrome/browser/ui/uikit_ui_util.h"
#include "ios/chrome/grit/ios_chromium_strings.h"
#import "ios/third_party/material_components_ios/src/components/Buttons/src/MaterialButtons.h"
#import "ios/third_party/material_components_ios/src/components/Typography/src/MaterialTypography.h"
#include "ui/base/l10n/l10n_util.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
// Horizontal padding for label and buttons.
const CGFloat kHorizontalPadding = 40;
// Vertical padding for the image and the label.
const CGFloat kVerticalPadding = 12;
// Vertical padding for buttons.
const CGFloat kButtonVerticalPadding = 6;
// Image size for warm state.
const CGFloat kProfileImageFixedSize = 48;
// Button height.
const CGFloat kButtonHeight = 36;
}

@implementation SigninPromoView {
  NSArray<NSLayoutConstraint*>* _coldStateConstraints;
  NSArray<NSLayoutConstraint*>* _warmStateConstraints;
  signin_metrics::AccessPoint _accessPoint;
}

@synthesize delegate = _delegate;
@synthesize mode = _mode;
@synthesize imageView = _imageView;
@synthesize textLabel = _textLabel;
@synthesize primaryButton = _primaryButton;
@synthesize secondaryButton = _secondaryButton;

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    self.isAccessibilityElement = YES;

    // Adding subviews.
    self.clipsToBounds = YES;
    _imageView = [[UIImageView alloc] init];
    _imageView.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_imageView];

    _textLabel = [[UILabel alloc] init];
    _textLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_textLabel];

    _primaryButton = [[MDCFlatButton alloc] init];
    _primaryButton.translatesAutoresizingMaskIntoConstraints = NO;
    _primaryButton.accessibilityIdentifier = @"signin_promo_primary_button";
    _primaryButton.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [_primaryButton addTarget:self
                       action:@selector(onPrimaryButtonAction:)
             forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:_primaryButton];

    _secondaryButton = [[MDCFlatButton alloc] init];
    _secondaryButton.translatesAutoresizingMaskIntoConstraints = NO;
    _secondaryButton.accessibilityIdentifier = @"signin_promo_secondary_button";
    [_secondaryButton addTarget:self
                         action:@selector(onSecondaryButtonAction:)
               forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:_secondaryButton];

    // Adding style.
    _imageView.contentMode = UIViewContentModeCenter;
    _imageView.layer.masksToBounds = YES;
    _imageView.contentMode = UIViewContentModeScaleAspectFit;

    _textLabel.font = [MDCTypography buttonFont];
    _textLabel.textColor = [[MDCPalette greyPalette] tint900];
    _textLabel.numberOfLines = 0;
    _textLabel.textAlignment = NSTextAlignmentCenter;

    [_primaryButton setBackgroundColor:[[MDCPalette cr_bluePalette] tint500]
                              forState:UIControlStateNormal];
    _primaryButton.customTitleColor = [UIColor whiteColor];
    _primaryButton.inkColor = [UIColor colorWithWhite:1 alpha:0.2];

    _secondaryButton.customTitleColor = [[MDCPalette cr_bluePalette] tint500];
    _secondaryButton.uppercaseTitle = NO;

    // Adding constraints.
    NSDictionary* metrics = @{
      @"kButtonHeight" : @(kButtonHeight),
      @"kButtonVerticalPadding" : @(kButtonVerticalPadding),
      @"kButtonVerticalPaddingx2" : @(kButtonVerticalPadding * 2),
      @"kHorizontalPadding" : @(kHorizontalPadding),
      @"kVerticalPadding" : @(kVerticalPadding),
      @"kVerticalPaddingx2" : @(kVerticalPadding * 2),
      @"kVerticalPaddingkButtonVerticalPadding" :
          @(kVerticalPadding + kButtonVerticalPadding),
    };
    NSDictionary* views = @{
      @"imageView" : _imageView,
      @"primaryButton" : _primaryButton,
      @"secondaryButton" : _secondaryButton,
      @"textLabel" : _textLabel,
    };

    // Constraints shared between modes.
    NSString* sharedVerticalConstraints =
        @"V:|-kVerticalPaddingx2-[imageView]-kVerticalPadding-[textLabel]-"
        @"kVerticalPaddingkButtonVerticalPadding-[primaryButton(kButtonHeight)"
        @"]";
    NSArray* visualConstraints = @[
      sharedVerticalConstraints,
      @"H:|-kHorizontalPadding-[primaryButton]-kHorizontalPadding-|"
    ];
    ApplyVisualConstraintsWithMetricsAndOptions(
        visualConstraints, views, metrics, NSLayoutFormatAlignAllCenterX);

    // Constraints for cold state mode.
    NSArray* coldStateVisualConstraints = @[
      @"V:[primaryButton]-kVerticalPaddingkButtonVerticalPadding-|",
    ];
    _coldStateConstraints = VisualConstraintsWithMetrics(
        coldStateVisualConstraints, views, metrics);

    // Constraints for warm state mode.
    NSString* warmStateVerticalConstraints =
        @"V:[primaryButton]-kButtonVerticalPaddingx2-[secondaryButton("
        @"kButtonHeight)]-kVerticalPaddingkButtonVerticalPadding-|";
    NSArray* warmStateVisualConstraints = @[
      warmStateVerticalConstraints,
      @"H:|-kHorizontalPadding-[secondaryButton]-kHorizontalPadding-|",
    ];
    _warmStateConstraints = VisualConstraintsWithMetrics(
        warmStateVisualConstraints, views, metrics);

    _mode = SigninPromoViewModeColdState;
    [self activateColdMode];
  }
  return self;
}

- (void)setMode:(SigninPromoViewMode)mode {
  if (mode == _mode) {
    return;
  }
  _mode = mode;
  switch (_mode) {
    case SigninPromoViewModeColdState:
      [self activateColdMode];
      return;
    case SigninPromoViewModeWarmState:
      [self activateWarmMode];
      return;
  }
  NOTREACHED();
}

- (void)activateColdMode {
  DCHECK_EQ(_mode, SigninPromoViewModeColdState);
  [NSLayoutConstraint deactivateConstraints:_warmStateConstraints];
  [NSLayoutConstraint activateConstraints:_coldStateConstraints];
  UIImage* logo = nil;
#if defined(GOOGLE_CHROME_BUILD)
  logo = [UIImage imageNamed:@"signin_promo_logo_chrome_color"];
#else
  logo = [UIImage imageNamed:@"signin_promo_logo_chromium_color"];
#endif  // defined(GOOGLE_CHROME_BUILD)
  DCHECK(logo);
  _imageView.image = logo;
  [_primaryButton
      setTitle:l10n_util::GetNSString(IDS_IOS_OPTIONS_IMPORT_DATA_TITLE_SIGNIN)
      forState:UIControlStateNormal];
  _secondaryButton.hidden = YES;
}

- (void)activateWarmMode {
  DCHECK_EQ(_mode, SigninPromoViewModeWarmState);
  [NSLayoutConstraint deactivateConstraints:_coldStateConstraints];
  [NSLayoutConstraint activateConstraints:_warmStateConstraints];
  _secondaryButton.hidden = NO;
}

- (void)setProfileImage:(UIImage*)image {
  DCHECK_EQ(SigninPromoViewModeWarmState, _mode);
  _imageView.image = CircularImageFromImage(image, kProfileImageFixedSize);
}

- (void)accessibilityPrimaryAction:(id)unused {
  [_primaryButton sendActionsForControlEvents:UIControlEventTouchUpInside];
}

- (void)accessibilitySecondaryAction:(id)unused {
  [_secondaryButton sendActionsForControlEvents:UIControlEventTouchUpInside];
}

- (CGFloat)horizontalPadding {
  return kHorizontalPadding;
}

- (void)onPrimaryButtonAction:(id)unused {
  switch (_mode) {
    case SigninPromoViewModeColdState:
      [_delegate signinPromoViewDidTapSigninWithNewAccount:self];
      break;
    case SigninPromoViewModeWarmState:
      [_delegate signinPromoViewDidTapSigninWithDefaultAccount:self];
      break;
  }
}

- (void)onSecondaryButtonAction:(id)unused {
  [_delegate signinPromoViewDidTapSigninWithOtherAccount:self];
}

#pragma mark - NSObject(Accessibility)

- (NSArray<UIAccessibilityCustomAction*>*)accessibilityCustomActions {
  NSString* primaryActionName =
      [_primaryButton titleForState:UIControlStateNormal];
  UIAccessibilityCustomAction* primaryCustomAction =
      [[UIAccessibilityCustomAction alloc]
          initWithName:primaryActionName
                target:self
              selector:@selector(accessibilityPrimaryAction:)];
  if (_mode == SigninPromoViewModeColdState) {
    return @[ primaryCustomAction ];
  }
  NSString* secondaryActionName =
      [_secondaryButton titleForState:UIControlStateNormal];
  UIAccessibilityCustomAction* secondaryCustomAction =
      [[UIAccessibilityCustomAction alloc]
          initWithName:secondaryActionName
                target:self
              selector:@selector(accessibilitySecondaryAction:)];
  return @[ primaryCustomAction, secondaryCustomAction ];
}

- (NSString*)accessibilityLabel {
  return _textLabel.text;
}

@end
