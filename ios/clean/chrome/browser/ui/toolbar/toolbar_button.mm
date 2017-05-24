// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/clean/chrome/browser/ui/toolbar/toolbar_button.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@implementation ToolbarButton
@synthesize visibilityMask = _visibilityMask;
@synthesize hiddenInCurrentSizeClass = _hiddenInCurrentSizeClass;
@synthesize hiddenInCurrentState = _hiddenInCurrentState;

+ (instancetype)toolbarButtonWithImageForNormalState:(UIImage*)normalImage
                            imageForHighlightedState:(UIImage*)highlightedImage
                               imageForDisabledState:(UIImage*)disabledImage {
  ToolbarButton* button = [[self class] buttonWithType:UIButtonTypeCustom];
  [button setImage:normalImage forState:UIControlStateNormal];
  [button setImage:highlightedImage forState:UIControlStateHighlighted];
  [button setImage:disabledImage forState:UIControlStateDisabled];
  button.translatesAutoresizingMaskIntoConstraints = NO;
  return button;
}

#pragma mark - Public Methods

- (void)updateHiddenInCurrentSizeClass {
  BOOL newHiddenValue = YES;
  switch (self.traitCollection.horizontalSizeClass) {
    case UIUserInterfaceSizeClassRegular:
      newHiddenValue =
          !(self.visibilityMask & ToolbarComponentVisibilityRegularWidth);
      break;
    case UIUserInterfaceSizeClassCompact:
      // First check if the button should be visible only when it's enabled,
      // if not, check if it should be visible in this case.
      if (self.visibilityMask &
          ToolbarComponentVisibilityCompactWidthOnlyWhenEnabled) {
        newHiddenValue = !self.enabled;
      } else if (self.visibilityMask & ToolbarComponentVisibilityCompactWidth) {
        newHiddenValue = NO;
      }
      break;
    case UIUserInterfaceSizeClassUnspecified:
    default:
      break;
  }
  if (newHiddenValue != self.hiddenInCurrentSizeClass) {
    self.hiddenInCurrentSizeClass = newHiddenValue;
    [self setHiddenForCurrentStateAndSizeClass];
  }
}

- (void)setHiddenForCurrentStateAndSizeClass {
  self.hidden = self.hiddenInCurrentState || self.hiddenInCurrentSizeClass;
}

@end
