// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/clean/chrome/browser/ui/tab_strip/tab_strip_coordinator.h"

#import "ios/chrome/browser/web_state_list/web_state_list.h"
#import "ios/clean/chrome/browser/ui/commands/tab_strip_commands.h"
#import "ios/clean/chrome/browser/ui/tab_collection/tab_collection_mediator.h"
#import "ios/clean/chrome/browser/ui/tab_strip/tab_strip_view_controller.h"
#import "ios/shared/chrome/browser/ui/browser_list/browser.h"
#import "ios/shared/chrome/browser/ui/commands/command_dispatcher.h"
#include "ios/web/public/web_state/web_state.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface TabStripCoordinator ()<TabStripCommands>
@property(nonatomic, strong) TabStripViewController* viewController;
@property(nonatomic, strong) TabCollectionMediator* mediator;
@property(nonatomic, readonly) WebStateList& webStateList;
@end

@implementation TabStripCoordinator
@synthesize viewController = _viewController;
@synthesize mediator = _mediator;

#pragma mark - Properties

- (WebStateList&)webStateList {
  return self.browser->web_state_list();
}

#pragma mark - BrowserCoordinator

- (void)start {
  CommandDispatcher* dispatcher = self.browser->dispatcher();
  [dispatcher startDispatchingToTarget:self
                           forSelector:@selector(showTabStripTabAtIndex:)];
  [dispatcher startDispatchingToTarget:self
                           forSelector:@selector(closeTabStripTabAtIndex:)];

  self.viewController = [[TabStripViewController alloc] init];
  self.mediator = [[TabCollectionMediator alloc] init];
  self.mediator.webStateList = &self.webStateList;
  self.mediator.consumer = self.viewController;
  self.viewController.dispatcher = static_cast<id>(self.browser->dispatcher());

  [super start];
}

- (void)stop {
  [super stop];
  [self.mediator disconnect];
  [self.browser->dispatcher() stopDispatchingToTarget:self];
}

#pragma mark - TabStripCommands

- (void)showTabStripTabAtIndex:(int)index {
  self.webStateList.ActivateWebStateAt(index);
}

- (void)closeTabStripTabAtIndex:(int)index {
  self.webStateList.CloseWebStateAt(index);
}

@end
