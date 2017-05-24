// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CLEAN_CHROME_BROWSER_UI_TOOLS_MENU_OVERFLOW_CONTROLS_STACKVIEW_H_
#define IOS_CLEAN_CHROME_BROWSER_UI_TOOLS_MENU_OVERFLOW_CONTROLS_STACKVIEW_H_

#import <UIKit/UIKit.h>

@class ToolbarButton;

// StackView subclass that contains the Overflow Toolbar Buttons that will be
// inserted in the first row of ToolMenu in compact widths.
@interface MenuOverflowControlsStackView : UIStackView
// Share ToolbarButton.
@property(nonatomic, strong) ToolbarButton* shareButton;
// Reload ToolbarButton.
@property(nonatomic, strong) ToolbarButton* reloadButton;
// Stop ToolbarButton.
@property(nonatomic, strong) ToolbarButton* stopButton;
@end

#endif  // IOS_CLEAN_CHROME_BROWSER_UI_TOOLS_MENU_OVERFLOW_CONTROLS_STACKVIEW_H_
