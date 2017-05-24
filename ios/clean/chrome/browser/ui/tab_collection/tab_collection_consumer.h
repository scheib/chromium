// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CLEAN_CHROME_BROWSER_UI_TAB_COLLECTION_TAB_COLLECTION_CONSUMER_H_
#define IOS_CLEAN_CHROME_BROWSER_UI_TAB_COLLECTION_TAB_COLLECTION_CONSUMER_H_

@class TabCollectionItem;

// Interface to support insert/delete/updates to a tab collection.
@protocol TabCollectionConsumer

// Inserts |item| into tab collection at |index|.
- (void)insertItem:(TabCollectionItem*)item atIndex:(int)index;

// Deletes |index| from tab collection.
- (void)deleteItemAtIndex:(int)index;

// Moves item from |fromIndex| to |toIndex|.
- (void)moveItemFromIndex:(int)fromIndex toIndex:(int)toIndex;

// Replaces item at |index| with |item|.
- (void)replaceItemAtIndex:(int)index withItem:(TabCollectionItem*)item;

// Selects the item at |index|.
- (void)selectItemAtIndex:(int)index;

// Populates tab collection with |items|.
- (void)populateItems:(NSArray<TabCollectionItem*>*)items;
@end

#endif  // IOS_CLEAN_CHROME_BROWSER_UI_TAB_COLLECTION_TAB_COLLECTION_CONSUMER_H_
