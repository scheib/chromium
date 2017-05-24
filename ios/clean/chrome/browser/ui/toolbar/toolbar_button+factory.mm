// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/clean/chrome/browser/ui/toolbar/toolbar_button+factory.h"

#import "ios/chrome/browser/ui/rtl_geometry.h"
#import "ios/chrome/browser/ui/uikit_ui_util.h"
#include "ios/chrome/grit/ios_theme_resources.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@implementation ToolbarButton (Factory)

#pragma mark - ToolbarButton Setup

+ (instancetype)backToolbarButton {
  ToolbarButton* backButton = [self
      toolbarButtonWithImageForNormalState:NativeReversableImage(
                                               IDR_IOS_TOOLBAR_LIGHT_BACK, YES)
                  imageForHighlightedState:
                      NativeReversableImage(IDR_IOS_TOOLBAR_LIGHT_BACK_PRESSED,
                                            YES)
                     imageForDisabledState:
                         NativeReversableImage(
                             IDR_IOS_TOOLBAR_LIGHT_BACK_DISABLED, YES)];
  return backButton;
}

+ (instancetype)forwardToolbarButton {
  ToolbarButton* forwardButton = [self
      toolbarButtonWithImageForNormalState:NativeReversableImage(
                                               IDR_IOS_TOOLBAR_LIGHT_FORWARD,
                                               YES)
                  imageForHighlightedState:
                      NativeReversableImage(
                          IDR_IOS_TOOLBAR_LIGHT_FORWARD_PRESSED, YES)
                     imageForDisabledState:
                         NativeReversableImage(
                             IDR_IOS_TOOLBAR_LIGHT_FORWARD_DISABLED, YES)];
  return forwardButton;
}

+ (instancetype)tabSwitcherStripToolbarButton {
  ToolbarButton* tabSwitcherStripButton = [self
      toolbarButtonWithImageForNormalState:NativeImage(
                                               IDR_IOS_TOOLBAR_LIGHT_OVERVIEW)
                  imageForHighlightedState:
                      NativeImage(IDR_IOS_TOOLBAR_LIGHT_OVERVIEW_PRESSED)
                     imageForDisabledState:
                         NativeImage(IDR_IOS_TOOLBAR_LIGHT_OVERVIEW_DISABLED)];
  return tabSwitcherStripButton;
}

+ (instancetype)tabSwitcherGridToolbarButton {
  ToolbarButton* tabSwitcherGridButton =
      [self toolbarButtonWithImageForNormalState:
                [UIImage imageNamed:@"tabswitcher_tab_switcher_button"]
                        imageForHighlightedState:nil
                           imageForDisabledState:nil];
  return tabSwitcherGridButton;
}

+ (instancetype)toolsMenuToolbarButton {
  ToolbarButton* toolsMenuButton = [self
      toolbarButtonWithImageForNormalState:NativeImage(
                                               IDR_IOS_TOOLBAR_LIGHT_TOOLS)
                  imageForHighlightedState:
                      NativeImage(IDR_IOS_TOOLBAR_LIGHT_TOOLS_PRESSED)
                     imageForDisabledState:nil];
  [toolsMenuButton setImageEdgeInsets:UIEdgeInsetsMakeDirected(0, -3, 0, 0)];
  return toolsMenuButton;
}

+ (instancetype)shareToolbarButton {
  ToolbarButton* shareButton = [self
      toolbarButtonWithImageForNormalState:NativeImage(
                                               IDR_IOS_TOOLBAR_LIGHT_SHARE)
                  imageForHighlightedState:
                      NativeImage(IDR_IOS_TOOLBAR_LIGHT_SHARE_PRESSED)
                     imageForDisabledState:
                         NativeImage(IDR_IOS_TOOLBAR_LIGHT_SHARE_DISABLED)];
  return shareButton;
}

+ (instancetype)reloadToolbarButton {
  ToolbarButton* reloadButton = [self
      toolbarButtonWithImageForNormalState:NativeReversableImage(
                                               IDR_IOS_TOOLBAR_LIGHT_RELOAD,
                                               YES)
                  imageForHighlightedState:
                      NativeReversableImage(
                          IDR_IOS_TOOLBAR_LIGHT_RELOAD_PRESSED, YES)
                     imageForDisabledState:
                         NativeReversableImage(
                             IDR_IOS_TOOLBAR_LIGHT_RELOAD_DISABLED, YES)];
  return reloadButton;
}

+ (instancetype)stopToolbarButton {
  ToolbarButton* stopButton = [self
      toolbarButtonWithImageForNormalState:NativeImage(
                                               IDR_IOS_TOOLBAR_LIGHT_STOP)
                  imageForHighlightedState:
                      NativeImage(IDR_IOS_TOOLBAR_LIGHT_STOP_PRESSED)
                     imageForDisabledState:
                         NativeImage(IDR_IOS_TOOLBAR_LIGHT_STOP_DISABLED)];
  return stopButton;
}

+ (instancetype)starToolbarButton {
  ToolbarButton* starButton =
      [self toolbarButtonWithImageForNormalState:NativeImage(
                                                     IDR_IOS_TOOLBAR_LIGHT_STAR)
                        imageForHighlightedState:
                            NativeImage(IDR_IOS_TOOLBAR_LIGHT_STAR_PRESSED)
                           imageForDisabledState:nil];
  return starButton;
}

@end
