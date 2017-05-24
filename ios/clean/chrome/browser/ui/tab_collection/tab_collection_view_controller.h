// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CLEAN_CHROME_BROWSER_UI_TAB_COLLECTION_TAB_COLLECTION_VIEW_CONTROLLER_H_
#define IOS_CLEAN_CHROME_BROWSER_UI_TAB_COLLECTION_TAB_COLLECTION_VIEW_CONTROLLER_H_

#import <UIKit/UIKit.h>

#import "ios/clean/chrome/browser/ui/tab_collection/tab_collection_consumer.h"

@protocol TabCollectionDataSource;

// Controller for a scrolling view displaying square cells that represent
// the user's open tabs.
@interface TabCollectionViewController
    : UIViewController<TabCollectionConsumer, UICollectionViewDataSource>
// A collection view of tabs.
@property(nonatomic, weak, readonly) UICollectionView* tabs;
// Model for collection view.
@property(nonatomic, strong, readonly)
    NSMutableArray<TabCollectionItem*>* items;
@end

#endif  // IOS_CLEAN_CHROME_BROWSER_UI_TAB_COLLECTION_TAB_COLLECTION_VIEW_CONTROLLER_H_
