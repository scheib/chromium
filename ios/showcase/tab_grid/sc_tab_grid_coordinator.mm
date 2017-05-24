// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/showcase/tab_grid/sc_tab_grid_coordinator.h"

#import "base/format_macros.h"
#import "ios/clean/chrome/browser/ui/commands/settings_commands.h"
#import "ios/clean/chrome/browser/ui/commands/tab_grid_commands.h"
#import "ios/clean/chrome/browser/ui/commands/tools_menu_commands.h"
#import "ios/clean/chrome/browser/ui/tab_collection/tab_collection_consumer.h"
#import "ios/clean/chrome/browser/ui/tab_collection/tab_collection_item.h"
#import "ios/clean/chrome/browser/ui/tab_grid/tab_grid_view_controller.h"
#import "ios/showcase/common/protocol_alerter.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface SCTabGridCoordinator ()
@property(nonatomic, strong) TabGridViewController* viewController;
@property(nonatomic, strong) ProtocolAlerter* alerter;
@end

@implementation SCTabGridCoordinator
@synthesize baseViewController = _baseViewController;
@synthesize viewController = _viewController;
@synthesize alerter = _alerter;

- (void)start {
  self.alerter = [[ProtocolAlerter alloc] initWithProtocols:@[
    @protocol(SettingsCommands), @protocol(TabGridCommands),
    @protocol(ToolsMenuCommands)
  ]];
  self.alerter.baseViewController = self.baseViewController;

  self.viewController = [[TabGridViewController alloc] init];
  self.viewController.title = @"Tab Grid";
  self.viewController.dispatcher =
      static_cast<id<SettingsCommands, TabGridCommands, ToolsMenuCommands>>(
          self.alerter);

  TabCollectionItem* item0 = [[TabCollectionItem alloc] init];
  item0.title = @"Tab 0";
  [self.viewController populateItems:@[ item0 ]];

  [self.baseViewController setHidesBarsOnSwipe:YES];
  [self.baseViewController pushViewController:self.viewController animated:YES];
}

@end
