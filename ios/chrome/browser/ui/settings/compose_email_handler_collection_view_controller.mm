// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/settings/compose_email_handler_collection_view_controller.h"

#include "base/mac/foundation_util.h"
#import "ios/chrome/browser/ui/collection_view/cells/collection_view_text_item.h"
#import "ios/chrome/browser/web/mailto_handler.h"
#import "ios/chrome/browser/web/mailto_url_rewriter.h"
#include "ios/chrome/grit/ios_strings.h"
#import "ios/third_party/material_components_ios/src/components/Palettes/src/MDCPalettes.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/l10n_util_mac.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

typedef NS_ENUM(NSInteger, SectionIdentifier) {
  SectionIdentifierMailtoHandlers = kSectionIdentifierEnumZero,
};

typedef NS_ENUM(NSInteger, ItemType) {
  ItemTypeMailtoHandlers = kItemTypeEnumZero,
};

}  // namespace

@interface ComposeEmailHandlerCollectionViewController () {
  MailtoURLRewriter* _rewriter;
}

// Returns the MailtoHandler at |indexPath|.
- (MailtoHandler*)handlerAtIndexPath:(NSIndexPath*)indexPath;
@end

@implementation ComposeEmailHandlerCollectionViewController

- (instancetype)initWithRewriter:(MailtoURLRewriter*)rewriter {
  self = [super initWithStyle:CollectionViewControllerStyleAppBar];
  if (self) {
    self.title = l10n_util::GetNSString(IDS_IOS_COMPOSE_EMAIL_SETTING);
    self.collectionViewAccessibilityIdentifier =
        @"compose_email_handler_view_controller";
    _rewriter = rewriter;
    [self loadModel];
  }
  return self;
}

- (void)loadModel {
  [super loadModel];
  CollectionViewModel* model = self.collectionViewModel;
  [model addSectionWithIdentifier:SectionIdentifierMailtoHandlers];

  // Since this is a one-of-several selection UI, there must be more than one
  // choice available to the user. If this UI is being presented when there is
  // only one choice, it is considered a software error.
  NSArray<MailtoHandler*>* handlers = [_rewriter defaultHandlers];
  NSString* currentHandlerID = [_rewriter defaultHandlerID];
  for (MailtoHandler* handler in handlers) {
    CollectionViewTextItem* item =
        [[CollectionViewTextItem alloc] initWithType:ItemTypeMailtoHandlers];
    [item setText:[handler appName]];
    if ([handler isAvailable]) {
      [item setTextColor:[[MDCPalette greyPalette] tint900]];
      [item setAccessibilityTraits:UIAccessibilityTraitButton];
    } else {
      [item setTextColor:[[MDCPalette greyPalette] tint500]];
      [item setAccessibilityTraits:UIAccessibilityTraitNotEnabled];
    }
    if ([currentHandlerID isEqualToString:[handler appStoreID]])
      [item setAccessoryType:MDCCollectionViewCellAccessoryCheckmark];
    [model addItem:item
        toSectionWithIdentifier:SectionIdentifierMailtoHandlers];
  }
}

#pragma mark - UICollectionViewDelegate

- (BOOL)collectionView:(UICollectionView*)collectionView
    shouldSelectItemAtIndexPath:(NSIndexPath*)indexPath {
  // Disallow selection if the handler for the tapped row is not available.
  return [[self handlerAtIndexPath:indexPath] isAvailable] &&
         [super collectionView:collectionView
             shouldSelectItemAtIndexPath:indexPath];
}

- (BOOL)collectionView:(UICollectionView*)collectionView
    shouldHighlightItemAtIndexPath:(NSIndexPath*)indexPath {
  // Disallow highlight (ripple effect) if the handler for the tapped row is not
  // available.
  return [[self handlerAtIndexPath:indexPath] isAvailable] &&
         [super collectionView:collectionView
             shouldHighlightItemAtIndexPath:indexPath];
}

- (void)collectionView:(UICollectionView*)collectionView
    didSelectItemAtIndexPath:(NSIndexPath*)indexPath {
  [super collectionView:collectionView didSelectItemAtIndexPath:indexPath];
  CollectionViewModel* model = self.collectionViewModel;

  // The items created in -loadModel are all MailtoHandlers type.
  CollectionViewTextItem* selectedItem =
      base::mac::ObjCCastStrict<CollectionViewTextItem>(
          [model itemAtIndexPath:indexPath]);
  DCHECK_EQ(ItemTypeMailtoHandlers, selectedItem.type);

  // Do nothing if the tapped row is already chosen as the default.
  if (selectedItem.accessoryType == MDCCollectionViewCellAccessoryCheckmark)
    return;

  // Iterate through the rows and remove the checkmark from any that has it.
  NSMutableArray* modifiedItems = [NSMutableArray array];
  for (id item in
       [model itemsInSectionWithIdentifier:SectionIdentifierMailtoHandlers]) {
    CollectionViewTextItem* textItem =
        base::mac::ObjCCastStrict<CollectionViewTextItem>(item);
    DCHECK_EQ(ItemTypeMailtoHandlers, textItem.type);
    if (textItem == selectedItem) {
      // Shows the checkmark on the new default mailto: URL handler.
      textItem.accessoryType = MDCCollectionViewCellAccessoryCheckmark;
      [modifiedItems addObject:textItem];
    } else if (textItem.accessoryType ==
               MDCCollectionViewCellAccessoryCheckmark) {
      // Unchecks any currently checked selection.
      textItem.accessoryType = MDCCollectionViewCellAccessoryNone;
      [modifiedItems addObject:textItem];
    }
  }

  // Sets the Mail client app that will handle mailto:// URLs.
  MailtoHandler* handler = [self handlerAtIndexPath:indexPath];
  DCHECK([handler isAvailable]);
  [_rewriter setDefaultHandlerID:[handler appStoreID]];

  [self reconfigureCellsForItems:modifiedItems];
}

#pragma mark - Private

- (MailtoHandler*)handlerAtIndexPath:(NSIndexPath*)indexPath {
  CollectionViewModel* model = self.collectionViewModel;
  NSUInteger handlerIndex = [model indexInItemTypeForIndexPath:indexPath];
  return [[_rewriter defaultHandlers] objectAtIndex:handlerIndex];
}

@end
