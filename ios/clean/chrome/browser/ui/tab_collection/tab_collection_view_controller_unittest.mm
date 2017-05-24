// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/clean/chrome/browser/ui/tab_collection/tab_collection_view_controller.h"

#import "ios/clean/chrome/browser/ui/tab_collection/tab_collection_consumer.h"
#import "ios/clean/chrome/browser/ui/tab_collection/tab_collection_item.h"
#include "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"
#include "testing/platform_test.h"
#include "third_party/ocmock/gtest_support.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface TestTabCollectionViewController : TabCollectionViewController
@property(nonatomic, readwrite) NSMutableArray<TabCollectionItem*>* items;
@end

@implementation TestTabCollectionViewController
@dynamic items;
@end

class TabCollectionViewControllerTest : public PlatformTest {
 public:
  TabCollectionViewControllerTest() {
    view_controller_ = [[TestTabCollectionViewController alloc] init];
    TabCollectionItem* item0 = [[TabCollectionItem alloc] init];
    item0.title = @"Item0";
    TabCollectionItem* item1 = [[TabCollectionItem alloc] init];
    item1.title = @"Item1";
    view_controller_.items = [@[ item0, item1 ] mutableCopy];
  }

 protected:
  TestTabCollectionViewController* view_controller_;
};

// Tests that an item is inserted.
TEST_F(TabCollectionViewControllerTest, TestInsertItem) {
  [view_controller_ insertItem:[[TabCollectionItem alloc] init] atIndex:0];
  EXPECT_EQ(3, static_cast<int>(view_controller_.items.count));
}

// Tests that an item is deleted.
TEST_F(TabCollectionViewControllerTest, TestDeleteItem) {
  [view_controller_ deleteItemAtIndex:0];
  EXPECT_EQ(1, static_cast<int>(view_controller_.items.count));
}

// Tests that an item is moved.
TEST_F(TabCollectionViewControllerTest, TestMoveItem) {
  [view_controller_ moveItemFromIndex:0 toIndex:1];
  EXPECT_NSEQ(@"Item1", view_controller_.items[0].title);
}

// Tests that an item is replaced.
TEST_F(TabCollectionViewControllerTest, TestReplaceItem) {
  TabCollectionItem* item = [[TabCollectionItem alloc] init];
  item.title = @"NewItem";
  [view_controller_ replaceItemAtIndex:0 withItem:item];
  EXPECT_NSEQ(@"NewItem", view_controller_.items[0].title);
}

// Tests that items are initialized.
TEST_F(TabCollectionViewControllerTest, TestInitializeItems) {
  TabCollectionItem* item = [[TabCollectionItem alloc] init];
  item.title = @"NewItem";
  [view_controller_ populateItems:@[ item ]];
  EXPECT_NSEQ(@"NewItem", view_controller_.items[0].title);
}
