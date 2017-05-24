// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/clean/chrome/browser/ui/tab_grid/tab_grid_view_controller.h"

#include "base/mac/foundation_util.h"
#import "ios/chrome/browser/ui/tab_switcher/tab_switcher_panel_overlay_view.h"
#import "ios/clean/chrome/browser/ui/actions/settings_actions.h"
#import "ios/clean/chrome/browser/ui/actions/tab_grid_actions.h"
#import "ios/clean/chrome/browser/ui/commands/settings_commands.h"
#import "ios/clean/chrome/browser/ui/commands/tab_grid_commands.h"
#import "ios/clean/chrome/browser/ui/commands/tools_menu_commands.h"
#import "ios/clean/chrome/browser/ui/tab_collection/tab_collection_tab_cell.h"
#import "ios/clean/chrome/browser/ui/tab_grid/mdc_floating_button+cr_tab_grid.h"
#import "ios/clean/chrome/browser/ui/tab_grid/tab_grid_collection_view_layout.h"
#import "ios/clean/chrome/browser/ui/tab_grid/tab_grid_toolbar.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface TabGridViewController ()<SettingsActions, TabGridActions>
@property(nonatomic, weak) UIView* noTabsOverlay;
@property(nonatomic, weak) TabGridToolbar* toolbar;
@property(nonatomic, weak) MDCFloatingButton* floatingNewTabButton;
@end

@implementation TabGridViewController
@synthesize dispatcher = _dispatcher;
@synthesize noTabsOverlay = _noTabsOverlay;
@synthesize toolbar = _toolbar;
@synthesize floatingNewTabButton = _floatingNewTabButton;

#pragma mark - Required subclass override

- (UICollectionViewLayout*)collectionViewLayout {
  return [[TabGridCollectionViewLayout alloc] init];
}

- (void)showTabAtIndex:(int)index {
  [self.dispatcher showTabGridTabAtIndex:index];
}

- (void)closeTabAtIndex:(int)index {
  [self.dispatcher closeTabGridTabAtIndex:index];
}

#pragma mark - View lifecyle

- (void)viewDidLoad {
  [super viewDidLoad];

  TabGridToolbar* toolbar = [[TabGridToolbar alloc] init];
  self.toolbar = toolbar;
  [self.view addSubview:self.toolbar];
  self.toolbar.translatesAutoresizingMaskIntoConstraints = NO;
  [NSLayoutConstraint activateConstraints:@[
    [self.toolbar.topAnchor constraintEqualToAnchor:self.view.topAnchor],
    [self.toolbar.heightAnchor
        constraintEqualToAnchor:self.topLayoutGuide.heightAnchor
                       constant:self.toolbar.intrinsicContentSize.height],
    [self.toolbar.leadingAnchor
        constraintEqualToAnchor:self.view.leadingAnchor],
    [self.toolbar.trailingAnchor
        constraintEqualToAnchor:self.view.trailingAnchor]
  ]];

  if (self.items.count == 0) {
    [self addNoTabsOverlay];
  }
}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];
  MDCFloatingButton* floatingNewTabButton =
      [MDCFloatingButton cr_tabGridNewTabButton];
  self.floatingNewTabButton = floatingNewTabButton;
  [self.floatingNewTabButton
      setFrame:[MDCFloatingButton
                   cr_frameForTabGridNewTabButtonInRect:self.view.bounds]];
  [self.view addSubview:self.floatingNewTabButton];
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  self.tabs.contentInset =
      UIEdgeInsetsMake(CGRectGetMaxY(self.toolbar.frame), 0, 0, 0);
}

- (void)viewWillTransitionToSize:(CGSize)size
       withTransitionCoordinator:
           (id<UIViewControllerTransitionCoordinator>)coordinator {
  // We need to dismiss the ToolsMenu everytime the Toolbar frame changes
  // (e.g. Size changes, rotation changes, etc.)
  [self.dispatcher closeToolsMenu];
}

#pragma mark - SettingsActions

- (void)showSettings:(id)sender {
  [self.dispatcher showSettings];
}

#pragma mark - ToolsMenuActions

- (void)showToolsMenu:(id)sender {
  [self.dispatcher showToolsMenu];
}

#pragma mark - TabGridActions

- (void)showTabGrid:(id)sender {
  [self.dispatcher showTabGrid];
}

- (void)createNewTab:(id)sender {
  [self.dispatcher createAndShowNewTabInTabGrid];
}

#pragma mark - ZoomTransitionDelegate methods

- (CGRect)rectForZoomWithKey:(NSObject*)key inView:(UIView*)view {
  // If key is nil we return a CGRect based on the toolbar position, if not it
  // was set by the TabGrid and we return a CGRect based on the IndexPath of the
  // key.
  if (!key) {
    return [self.toolbar rectForZoomWithKey:key inView:view];
  }

  NSIndexPath* cellPath = base::mac::ObjCCastStrict<NSIndexPath>(key);
  UICollectionViewCell* cell = [self.tabs cellForItemAtIndexPath:cellPath];
  return [view convertRect:cell.bounds fromView:cell];
}

#pragma mark - TabGridConsumer methods

- (void)addNoTabsOverlay {
  // PLACEHOLDER: The new tab grid will have a completely different zero tab
  // overlay from the tab switcher. Also, the overlay will be above the recent
  // tabs section.
  TabSwitcherPanelOverlayView* overlayView =
      [[TabSwitcherPanelOverlayView alloc] initWithFrame:self.tabs.bounds
                                            browserState:nil];
  overlayView.overlayType =
      TabSwitcherPanelOverlayType::OVERLAY_PANEL_USER_NO_OPEN_TABS;
  overlayView.autoresizingMask =
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  [self.tabs addSubview:overlayView];
  self.noTabsOverlay = overlayView;
}

- (void)removeNoTabsOverlay {
  [self.noTabsOverlay removeFromSuperview];
}

@end
