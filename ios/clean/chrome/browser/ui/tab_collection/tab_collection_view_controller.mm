// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/clean/chrome/browser/ui/tab_collection/tab_collection_view_controller.h"

#include "base/logging.h"
#include "base/mac/foundation_util.h"
#include "base/strings/sys_string_conversions.h"
#import "ios/clean/chrome/browser/ui/tab_collection/tab_collection_item.h"
#import "ios/clean/chrome/browser/ui/tab_collection/tab_collection_tab_cell.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface TabCollectionViewController ()<UICollectionViewDelegate,
                                          SessionCellDelegate>
@property(nonatomic, readwrite) UICollectionView* tabs;
@property(nonatomic, readwrite) NSMutableArray<TabCollectionItem*>* items;
@property(nonatomic, assign) int selectedIndex;
@end

@implementation TabCollectionViewController
@synthesize tabs = _tabs;
@synthesize items = _items;
@synthesize selectedIndex = _selectedIndex;

#pragma mark - UIViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  UICollectionView* tabs =
      [[UICollectionView alloc] initWithFrame:CGRectZero
                         collectionViewLayout:[self collectionViewLayout]];
  tabs.translatesAutoresizingMaskIntoConstraints = NO;
  tabs.backgroundColor = [UIColor blackColor];

  [self.view addSubview:tabs];
  self.tabs = tabs;
  self.tabs.dataSource = self;
  self.tabs.delegate = self;
  [self.tabs registerClass:[TabCollectionTabCell class]
      forCellWithReuseIdentifier:[TabCollectionTabCell identifier]];

  [NSLayoutConstraint activateConstraints:@[
    [self.tabs.topAnchor constraintEqualToAnchor:self.view.topAnchor],
    [self.tabs.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
    [self.tabs.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
    [self.tabs.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
  ]];

  [self selectItemAtIndex:self.selectedIndex];
}

- (UIStatusBarStyle)preferredStatusBarStyle {
  return UIStatusBarStyleLightContent;
}

#pragma mark - Required subclass override

- (UICollectionViewLayout*)collectionViewLayout {
  NOTREACHED() << "You must override "
               << base::SysNSStringToUTF8(NSStringFromSelector(_cmd))
               << " in a subclass.";
  return nil;
}

- (void)showTabAtIndex:(int)index {
  NOTREACHED() << "You must override "
               << base::SysNSStringToUTF8(NSStringFromSelector(_cmd))
               << " in a subclass.";
}

- (void)closeTabAtIndex:(int)index {
  NOTREACHED() << "You must override "
               << base::SysNSStringToUTF8(NSStringFromSelector(_cmd))
               << " in a subclass.";
}

#pragma mark - UICollectionViewDataSource methods

- (NSInteger)numberOfSectionsInCollectionView:
    (UICollectionView*)collectionView {
  return 1;
}

- (NSInteger)collectionView:(UICollectionView*)collectionView
     numberOfItemsInSection:(NSInteger)section {
  return static_cast<NSInteger>(self.items.count);
}

- (UICollectionViewCell*)collectionView:(UICollectionView*)collectionView
                 cellForItemAtIndexPath:(nonnull NSIndexPath*)indexPath {
  TabCollectionTabCell* cell =
      base::mac::ObjCCastStrict<TabCollectionTabCell>([collectionView
          dequeueReusableCellWithReuseIdentifier:[TabCollectionTabCell
                                                     identifier]
                                    forIndexPath:indexPath]);
  cell.delegate = self;
  [cell setSessionType:TabSwitcherSessionType::REGULAR_SESSION];
  DCHECK_LE(indexPath.item, INT_MAX);
  int index = static_cast<int>(indexPath.item);
  [cell setAppearanceForTabTitle:self.items[index].title
                         favicon:nil
                        cellSize:CGSizeZero];
  return cell;
}

#pragma mark - UICollectionViewDelegate methods

- (BOOL)collectionView:(UICollectionView*)collectionView
    shouldSelectItemAtIndexPath:(NSIndexPath*)indexPath {
  // Prevent user selection of items.
  return NO;
}

#pragma mark - SessionCellDelegate

- (TabSwitcherCache*)tabSwitcherCache {
  // PLACEHOLDER: return image cache.
  return nil;
}

- (void)cellPressed:(UICollectionViewCell*)cell {
  NSInteger item = [[self.tabs indexPathForCell:cell] item];
  DCHECK_LE(item, INT_MAX);
  int index = static_cast<int>(item);
  [self showTabAtIndex:index];
}

- (void)deleteButtonPressedForCell:(UICollectionViewCell*)cell {
  NSInteger item = [[self.tabs indexPathForCell:cell] item];
  DCHECK_LE(item, INT_MAX);
  int index = static_cast<int>(item);
  [self closeTabAtIndex:index];
}

#pragma mark - TabCollectionConsumer methods

- (void)insertItem:(TabCollectionItem*)item atIndex:(int)index {
  DCHECK_LE(static_cast<NSUInteger>(index), self.items.count);
  [self.items insertObject:item atIndex:index];
  [self.tabs insertItemsAtIndexPaths:@[ [self indexPathForIndex:index] ]];
}

- (void)deleteItemAtIndex:(int)index {
  DCHECK_LT(static_cast<NSUInteger>(index), self.items.count);
  [self.items removeObjectAtIndex:index];
  [self.tabs deleteItemsAtIndexPaths:@[ [self indexPathForIndex:index] ]];
}

- (void)moveItemFromIndex:(int)fromIndex toIndex:(int)toIndex {
  TabCollectionItem* item = self.items[fromIndex];
  [self.items removeObjectAtIndex:fromIndex];
  [self.items insertObject:item atIndex:toIndex];
  [self.tabs moveItemAtIndexPath:[self indexPathForIndex:fromIndex]
                     toIndexPath:[self indexPathForIndex:toIndex]];
}

- (void)replaceItemAtIndex:(int)index withItem:(TabCollectionItem*)item {
  [self.items removeObjectAtIndex:index];
  [self.items insertObject:item atIndex:index];
}

- (void)selectItemAtIndex:(int)index {
  self.selectedIndex = index;
  [self.tabs selectItemAtIndexPath:[self indexPathForIndex:index]
                          animated:YES
                    scrollPosition:UITableViewScrollPositionNone];
}

- (void)populateItems:(NSArray<TabCollectionItem*>*)items {
  self.items = [items mutableCopy];
  [self.tabs reloadData];
}

#pragma mark - Private

- (NSIndexPath*)indexPathForIndex:(int)index {
  return [NSIndexPath indexPathForItem:index inSection:0];
}

@end
