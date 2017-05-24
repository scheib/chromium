// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CLEAN_CHROME_BROWSER_UI_TAB_COLLECTION_TAB_COLLECTION_MEDIATOR_H_
#define IOS_CLEAN_CHROME_BROWSER_UI_TAB_COLLECTION_TAB_COLLECTION_MEDIATOR_H_

#import "ios/chrome/browser/web_state_list/web_state_list_observer_bridge.h"

@protocol TabCollectionConsumer;
class WebStateList;

// Mediator listens for web state list changes, then updates the consumer.
// This also serves as data source for a tab collection.
@interface TabCollectionMediator : NSObject<WebStateListObserving>

// The source of changes and backing of the data source.
@property(nonatomic, assign) WebStateList* webStateList;

// This consumer is updated whenever there are relevant changes to the web state
// list.
@property(nonatomic, weak) id<TabCollectionConsumer> consumer;

// Stops observing all objects.
- (void)disconnect;

@end

#endif  // IOS_CLEAN_CHROME_BROWSER_UI_TAB_COLLECTION_TAB_COLLECTION_MEDIATOR_H_
