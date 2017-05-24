// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/reading_list/reading_list_collection_view_controller.h"

#include "base/bind.h"
#include "base/logging.h"
#import "base/mac/foundation_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/strings/sys_string_conversions.h"
#include "base/time/time.h"
#include "components/reading_list/core/reading_list_entry.h"
#include "components/strings/grit/components_strings.h"
#include "components/url_formatter/url_formatter.h"
#include "ios/chrome/browser/reading_list/offline_url_utils.h"
#import "ios/chrome/browser/ui/alert_coordinator/action_sheet_coordinator.h"
#import "ios/chrome/browser/ui/collection_view/cells/collection_view_text_item.h"
#import "ios/chrome/browser/ui/collection_view/collection_view_model.h"
#import "ios/chrome/browser/ui/favicon/favicon_attributes_provider.h"
#import "ios/chrome/browser/ui/favicon/favicon_view.h"
#import "ios/chrome/browser/ui/material_components/utils.h"
#import "ios/chrome/browser/ui/reading_list/reading_list_collection_view_item.h"
#import "ios/chrome/browser/ui/reading_list/reading_list_collection_view_item_accessibility_delegate.h"
#import "ios/chrome/browser/ui/reading_list/reading_list_data_sink.h"
#import "ios/chrome/browser/ui/reading_list/reading_list_data_source.h"
#import "ios/chrome/browser/ui/reading_list/reading_list_empty_collection_background.h"
#import "ios/chrome/browser/ui/reading_list/reading_list_toolbar.h"
#import "ios/chrome/browser/ui/reading_list/reading_list_utils.h"
#import "ios/chrome/browser/ui/uikit_ui_util.h"
#include "ios/chrome/browser/ui/url_loader.h"
#import "ios/chrome/browser/ui/util/pasteboard_util.h"
#include "ios/chrome/grit/ios_strings.h"
#import "ios/third_party/material_components_ios/src/components/AppBar/src/MaterialAppBar.h"
#import "ios/third_party/material_components_ios/src/components/Palettes/src/MaterialPalettes.h"
#include "ios/web/public/referrer.h"
#include "ios/web/public/web_state/web_state.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/base/window_open_disposition.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

typedef NS_ENUM(NSInteger, SectionIdentifier) {
  SectionIdentifierUnread = kSectionIdentifierEnumZero,
  SectionIdentifierRead,
};

typedef NS_ENUM(NSInteger, ItemType) {
  ItemTypeHeader = kItemTypeEnumZero,
  ItemTypeItem,
};

// Typedef for a block taking a GURL as parameter and returning nothing.
typedef void (^EntryUpdater)(const GURL&);

// Type for map used to sort ReadingListEntry by timestamp. Multiple entries can
// have the same timestamp.
using ItemsMapByDate = std::multimap<int64_t, ReadingListCollectionViewItem*>;
}

@interface ReadingListCollectionViewController ()<

    ReadingListCollectionViewItemAccessibilityDelegate,
    ReadingListDataSink> {
  // Toolbar with the actions.
  ReadingListToolbar* _toolbar;
  // Action sheet presenting the subactions of the toolbar.
  AlertCoordinator* _actionSheet;
  UIView* _emptyCollectionBackground;

  // Whether the data source has pending modifications.
  BOOL _dataSourceHasBeenModified;
}

// Lazily instantiated.
@property(nonatomic, strong, readonly)
    FaviconAttributesProvider* attributesProvider;
// Whether the data source modifications should be taken into account.
@property(nonatomic, assign) BOOL shouldMonitorDataSource;

// Handles "Done" button touches.
- (void)donePressed;
// Loads all the items in all sections.
- (void)loadItems;
// Fills section |sectionIdentifier| with the items from |map| in reverse order
// of the map key.
- (void)loadItemsFromMap:(const ItemsMapByDate&)map
               toSection:(SectionIdentifier)sectionIdentifier;
// Whether the data source has changed.
- (BOOL)hasDataSourceChanged;
// Returns whether there is a difference between the elements contained in the
// |sectionIdentifier| and those in the |map|.
- (BOOL)section:(SectionIdentifier)sectionIdentifier
    isDifferentOfMap:(ItemsMapByDate&)map;
// Reloads the data if a changed occured during editing
- (void)applyPendingUpdates;
// Convenience method to create cell items for reading list entries.
- (ReadingListCollectionViewItem*)cellItemForReadingListEntry:
    (const ReadingListEntry&)entry;
// Sets and fetches the |faviconURL| of this |item|, then reconfigures it.
- (void)setItem:(ReadingListCollectionViewItem*)item
     faviconURL:(const GURL&)url;
// Fills the |unread_map| and the |read_map| with the corresponding
// ReadingListCollectionViewItem from the data source.
- (void)fillUnreadMap:(ItemsMapByDate&)unread_map
              readMap:(ItemsMapByDate&)read_map;
// Returns whether there are elements in the section identified by
// |sectionIdentifier|.
- (BOOL)hasItemInSection:(SectionIdentifier)sectionIdentifier;
// Adds an empty background if needed.
- (void)collectionIsEmpty;
// Handles a long press.
- (void)handleLongPress:(UILongPressGestureRecognizer*)gestureRecognizer;
// Returns the ReadingListEntry associated with the |item|. If there is not such
// an entry, returns nullptr.
- (const ReadingListEntry*)readingListEntryForItem:
    (ReadingListCollectionViewItem*)item;
// Updates the toolbar state according to the selected items.
- (void)updateToolbarState;
// Displays an action sheet to let the user choose to mark all the elements as
// read or as unread. Used when nothing is selected.
- (void)markAllItemsAs;
// Displays an action sheet to let the user choose to mark all the selected
// elements as read or as unread. Used if read and unread elements are selected.
- (void)markMixedItemsAs;
// Marks all items as read.
- (void)markAllRead;
// Marks all items as unread.
- (void)markAllUnread;
// Marks the items at |indexPaths| as read.
- (void)markItemsReadAtIndexPath:(NSArray*)indexPaths;
// Marks the items at |indexPaths| as unread.
- (void)markItemsUnreadAtIndexPath:(NSArray*)indexPaths;
// Deletes all the read items.
- (void)deleteAllReadItems;
// Deletes all the items at |indexPath|.
- (void)deleteItemsAtIndexPaths:(NSArray*)indexPaths;
// Initializes |_actionSheet| with |self| as base view controller, and the
// toolbar's mark button as anchor point.
- (void)initializeActionSheet;
// Exits the editing mode and update the toolbar state with animation.
- (void)exitEditingModeAnimated:(BOOL)animated;
// Applies |updater| to the URL of every cell in the section |identifier|. The
// updates are done in reverse order of the cells in the section to keep the
// order. The monitoring of the data source updates are suspended during this
// time.
- (void)updateItemsInSectionIdentifier:(SectionIdentifier)identifier
                     usingEntryUpdater:(EntryUpdater)updater;
// Applies |updater| to the URL of every element in |indexPaths|. The updates
// are done in reverse order |indexPaths| to keep the order. The monitoring of
// the data source updates are suspended during this time.
- (void)updateIndexPaths:(NSArray<NSIndexPath*>*)indexPaths
       usingEntryUpdater:(EntryUpdater)updater;
// Logs the deletions histograms for the entry with |url|.
- (void)logDeletionHistogramsForEntry:(const GURL&)url;
// Move all the items from |sourceSectionIdentifier| to
// |destinationSectionIdentifier| and removes the empty section from the
// collection.
- (void)moveItemsFromSection:(SectionIdentifier)sourceSectionIdentifier
                   toSection:(SectionIdentifier)destinationSectionIdentifier;
// Move the currently selected elements to |sectionIdentifier| and removes the
// empty sections.
- (void)moveSelectedItems:(NSArray*)sortedIndexPaths
                toSection:(SectionIdentifier)sectionIdentifier;
// Makes sure |sectionIdentifier| exists with the correct header.
// Returns the index of the new section in the collection view; NSIntegerMax if
// no section has been created.
- (NSInteger)initializeSection:(SectionIdentifier)sectionIdentifier;
// Returns the header for the |sectionIdentifier|.
- (CollectionViewTextItem*)headerForSection:
    (SectionIdentifier)sectionIdentifier;
// Removes the empty sections from the collection and the model.
- (void)removeEmptySections;

@end

@implementation ReadingListCollectionViewController

@synthesize audience = _audience;
@synthesize delegate = _delegate;
@synthesize dataSource = _dataSource;

@synthesize largeIconService = _largeIconService;
@synthesize attributesProvider = _attributesProvider;
@synthesize shouldMonitorDataSource = _shouldMonitorDataSource;

#pragma mark lifecycle

- (instancetype)initWithDataSource:(id<ReadingListDataSource>)dataSource
                  largeIconService:(favicon::LargeIconService*)largeIconService
                           toolbar:(ReadingListToolbar*)toolbar {
  self = [super initWithStyle:CollectionViewControllerStyleAppBar];
  if (self) {
    _toolbar = toolbar;

    _dataSource = dataSource;
    _largeIconService = largeIconService;
    _emptyCollectionBackground =
        [[ReadingListEmptyCollectionBackground alloc] init];

    _shouldMonitorDataSource = YES;
    _dataSourceHasBeenModified = NO;

    _dataSource.dataSink = self;
  }
  return self;
}

#pragma mark - properties

- (FaviconAttributesProvider*)attributesProvider {
  if (_attributesProvider) {
    return _attributesProvider;
  }

  // Accept any favicon even the smallest ones (16x16) as it is better than the
  // fallback icon.
  // Pass 1 as minimum size to avoid empty favicons.
  _attributesProvider = [[FaviconAttributesProvider alloc]
      initWithFaviconSize:kFaviconPreferredSize
           minFaviconSize:1
         largeIconService:self.largeIconService];
  return _attributesProvider;
}

- (void)setToolbarState:(ReadingListToolbarState)toolbarState {
  [_toolbar setState:toolbarState];
}

- (void)setAudience:(id<ReadingListCollectionViewControllerAudience>)audience {
  _audience = audience;
  if (self.dataSource.ready) {
    [audience readingListHasItems:self.dataSource.hasElements];
  }
}

#pragma mark - UIViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  self.title = l10n_util::GetNSString(IDS_IOS_TOOLS_MENU_READING_LIST);

  // Add "Done" button.
  UIBarButtonItem* doneItem = [[UIBarButtonItem alloc]
      initWithTitle:l10n_util::GetNSString(IDS_IOS_READING_LIST_DONE_BUTTON)
              style:UIBarButtonItemStylePlain
             target:self
             action:@selector(donePressed)];
  doneItem.accessibilityIdentifier = @"Done";
  self.navigationItem.rightBarButtonItem = doneItem;

  // Customize collection view settings.
  self.styler.cellStyle = MDCCollectionViewCellStyleCard;
  self.styler.separatorInset = UIEdgeInsetsMake(0, 16, 0, 16);

  UILongPressGestureRecognizer* longPressRecognizer =
      [[UILongPressGestureRecognizer alloc]
          initWithTarget:self
                  action:@selector(handleLongPress:)];
  longPressRecognizer.numberOfTouchesRequired = 1;
  [self.collectionView addGestureRecognizer:longPressRecognizer];
}

#pragma mark - UICollectionViewDelegate

- (void)collectionView:(UICollectionView*)collectionView
    didSelectItemAtIndexPath:(NSIndexPath*)indexPath {
  [super collectionView:collectionView didSelectItemAtIndexPath:indexPath];

  if (self.editor.editing) {
    [self updateToolbarState];
  } else {
    ReadingListCollectionViewItem* readingListItem =
        base::mac::ObjCCastStrict<ReadingListCollectionViewItem>(
            [self.collectionViewModel itemAtIndexPath:indexPath]);

    [self.delegate readingListCollectionViewController:self
                                              openItem:readingListItem];
  }
}

- (void)collectionView:(UICollectionView*)collectionView
    didDeselectItemAtIndexPath:(NSIndexPath*)indexPath {
  [super collectionView:collectionView didDeselectItemAtIndexPath:indexPath];
  if (self.editor.editing) {
    // When deselecting an item, if we are editing, we want to update the
    // toolbar base on the selected items.
    [self updateToolbarState];
  }
}

#pragma mark - MDCCollectionViewController

- (void)updateFooterInfoBarIfNecessary {
  // No-op. This prevents the default infobar from showing.
  // TODO(crbug.com/653547): Remove this once the MDC adds an option for
  // preventing the infobar from showing.
}

#pragma mark - MDCCollectionViewStylingDelegate

- (CGFloat)collectionView:(UICollectionView*)collectionView
    cellHeightAtIndexPath:(NSIndexPath*)indexPath {
  NSInteger type = [self.collectionViewModel itemTypeForIndexPath:indexPath];
  if (type == ItemTypeItem)
    return MDCCellDefaultTwoLineHeight;
  else
    return MDCCellDefaultOneLineHeight;
}

#pragma mark - MDCCollectionViewEditingDelegate

- (BOOL)collectionViewAllowsEditing:(nonnull UICollectionView*)collectionView {
  return YES;
}

#pragma mark - ReadingListDataSink

- (void)dataSourceReady:(id<ReadingListDataSource>)dataSource {
  [self loadModel];
  if ([self isViewLoaded]) {
    [self.collectionView reloadData];
  }
}

- (void)readingListModelDidApplyChanges {
  if (!self.shouldMonitorDataSource) {
    return;
  }

  // If we are editing and monitoring the model updates, set a flag to reload
  // the data at the end of the editing.
  if ([self.editor isEditing]) {
    _dataSourceHasBeenModified = YES;
    return;
  }

  // Ignore single element updates when the data source is doing batch updates.
  if ([self.dataSource isPerformingBatchUpdates]) {
    return;
  }

  if ([self hasDataSourceChanged])
    [self reloadData];
}

- (void)readingListModelCompletedBatchUpdates {
  if (!self.shouldMonitorDataSource) {
    return;
  }

  if ([self hasDataSourceChanged])
    [self reloadData];
}

#pragma mark - Public methods

- (void)reloadData {
  [self loadModel];
  if ([self isViewLoaded]) {
    [self.collectionView reloadData];
  }
}

- (void)willBeDismissed {
  [self.dataSource dataSinkWillBeDismissed];
  [_actionSheet stop];
}

#pragma mark - ReadingListCollectionViewItemAccessibilityDelegate

- (BOOL)isEntryRead:(ReadingListCollectionViewItem*)entry {
  const ReadingListEntry* readingListEntry =
      [self readingListEntryForItem:entry];

  if (!readingListEntry) {
    return NO;
  }

  return readingListEntry->IsRead();
}

- (void)deleteEntry:(ReadingListCollectionViewItem*)entry {
  const ReadingListEntry* readingListEntry =
      [self readingListEntryForItem:entry];

  if (!readingListEntry) {
    return;
  }

  SectionIdentifier sectionIdentifier = SectionIdentifierUnread;
  if (readingListEntry->IsRead()) {
    sectionIdentifier = SectionIdentifierRead;
  }
  if (![self.collectionViewModel hasItem:entry
                 inSectionWithIdentifier:sectionIdentifier]) {
    return;
  }

  [self deleteItemsAtIndexPaths:@[ [self.collectionViewModel
                                    indexPathForItem:entry] ]];
}

- (void)openEntryInNewTab:(ReadingListCollectionViewItem*)entry {
  [self.delegate readingListCollectionViewController:self
                                   openNewTabWithURL:entry.url
                                           incognito:NO];
}

- (void)openEntryInNewIncognitoTab:(ReadingListCollectionViewItem*)entry {
  [self.delegate readingListCollectionViewController:self
                                   openNewTabWithURL:entry.url
                                           incognito:YES];
}

- (void)copyEntryURL:(ReadingListCollectionViewItem*)entry {
  StoreURLInPasteboard(entry.url);
}

- (void)openEntryOffline:(ReadingListCollectionViewItem*)entry {
  const ReadingListEntry* readingListEntry =
      [self readingListEntryForItem:entry];

  if (!readingListEntry) {
    return;
  }

  if (readingListEntry->DistilledState() == ReadingListEntry::PROCESSED) {
    const GURL entryURL = readingListEntry->URL();
    GURL offlineURL = reading_list::OfflineURLForPath(
        readingListEntry->DistilledPath(), entryURL,
        readingListEntry->DistilledURL());

    [self.delegate readingListCollectionViewController:self
                                        openOfflineURL:offlineURL
                                 correspondingEntryURL:entryURL];
  }
}

- (void)markEntryRead:(ReadingListCollectionViewItem*)entry {
  if (![self.collectionViewModel hasItem:entry
                 inSectionWithIdentifier:SectionIdentifierUnread]) {
    return;
  }
  [self markItemsReadAtIndexPath:@[ [self.collectionViewModel
                                     indexPathForItem:entry] ]];
}

- (void)markEntryUnread:(ReadingListCollectionViewItem*)entry {
  if (![self.collectionViewModel hasItem:entry
                 inSectionWithIdentifier:SectionIdentifierRead]) {
    return;
  }
  [self markItemsUnreadAtIndexPath:@[ [self.collectionViewModel
                                       indexPathForItem:entry] ]];
}

#pragma mark - Private methods

- (void)donePressed {
  if ([self.editor isEditing]) {
    [self exitEditingModeAnimated:NO];
  }
  [self dismiss];
}

- (void)dismiss {
  [self.delegate dismissReadingListCollectionViewController:self];
}

- (void)loadModel {
  [super loadModel];

  if (!self.dataSource.hasElements) {
    [self collectionIsEmpty];
  } else {
    self.collectionView.alwaysBounceVertical = YES;
    [self loadItems];
    self.collectionView.backgroundView = nil;
    [self.audience readingListHasItems:YES];
  }
}

- (void)loadItemsFromMap:(const ItemsMapByDate&)map
               toSection:(SectionIdentifier)sectionIdentifier {
  if (map.size() == 0) {
    return;
  }
  CollectionViewModel* model = self.collectionViewModel;
  [model addSectionWithIdentifier:sectionIdentifier];
  [model setHeader:[self headerForSection:sectionIdentifier]
      forSectionWithIdentifier:sectionIdentifier];
  // Reverse iterate to add newer entries at the top.
  ItemsMapByDate::const_reverse_iterator iterator = map.rbegin();
  for (; iterator != map.rend(); iterator++) {
    [model addItem:iterator->second toSectionWithIdentifier:sectionIdentifier];
  }
}

- (void)loadItems {
  ItemsMapByDate read_map;
  ItemsMapByDate unread_map;
  [self fillUnreadMap:unread_map readMap:read_map];
  [self loadItemsFromMap:unread_map toSection:SectionIdentifierUnread];
  [self loadItemsFromMap:read_map toSection:SectionIdentifierRead];

  BOOL hasRead = read_map.size() > 0;
  [_toolbar setHasReadItem:hasRead];
}

- (void)fillUnreadMap:(ItemsMapByDate&)unread_map
              readMap:(ItemsMapByDate&)read_map {
  for (const auto& url : [self.dataSource keys]) {
    const ReadingListEntry* entry = [self.dataSource entryWithURL:url];
    ReadingListCollectionViewItem* item =
        [self cellItemForReadingListEntry:*entry];
    item.accessibilityDelegate = self;
    if (entry->IsRead()) {
      read_map.insert(std::make_pair(entry->UpdateTime(), item));
    } else {
      unread_map.insert(std::make_pair(entry->UpdateTime(), item));
    }
  }
}

- (void)applyPendingUpdates {
  if (_dataSourceHasBeenModified) {
    [self reloadData];
  }
}

- (BOOL)hasDataSourceChanged {
  ItemsMapByDate read_map;
  ItemsMapByDate unread_map;
  [self fillUnreadMap:unread_map readMap:read_map];

  if ([self section:SectionIdentifierRead isDifferentOfMap:read_map])
    return YES;
  if ([self section:SectionIdentifierUnread isDifferentOfMap:unread_map])
    return YES;

  return NO;
}

- (BOOL)section:(SectionIdentifier)sectionIdentifier
    isDifferentOfMap:(ItemsMapByDate&)map {
  if (![self.collectionViewModel
          hasSectionForSectionIdentifier:sectionIdentifier]) {
    return !map.empty();
  }

  NSArray* items =
      [self.collectionViewModel itemsInSectionWithIdentifier:sectionIdentifier];
  if ([items count] != map.size())
    return YES;

  NSMutableArray<ReadingListCollectionViewItem*>* itemsToReconfigure =
      [NSMutableArray array];

  NSInteger index = 0;
  ItemsMapByDate::const_reverse_iterator iterator = map.rbegin();
  for (; iterator != map.rend(); iterator++) {
    ReadingListCollectionViewItem* oldItem =
        base::mac::ObjCCastStrict<ReadingListCollectionViewItem>(items[index]);
    ReadingListCollectionViewItem* newItem = iterator->second;
    if (oldItem.url == newItem.url) {
      if (![oldItem isEqual:newItem]) {
        oldItem.title = newItem.title;
        oldItem.subtitle = newItem.subtitle;
        oldItem.distillationState = newItem.distillationState;
        oldItem.distillationDate = newItem.distillationDate;
        oldItem.distillationSize = newItem.distillationSize;
        [itemsToReconfigure addObject:oldItem];
      }
      if (oldItem.faviconPageURL != newItem.faviconPageURL) {
        [self setItem:oldItem faviconURL:newItem.faviconPageURL];
      }
    }
    if (![oldItem isEqual:newItem]) {
      return YES;
    }
    index++;
  }
  [self reconfigureCellsForItems:itemsToReconfigure];
  return NO;
}

- (ReadingListCollectionViewItem*)cellItemForReadingListEntry:
    (const ReadingListEntry&)entry {
  GURL url = entry.URL();
  ReadingListCollectionViewItem* item = [[ReadingListCollectionViewItem alloc]
           initWithType:ItemTypeItem
                    url:url
      distillationState:reading_list::UIStatusFromModelStatus(
                            entry.DistilledState())];

  [self setItem:item
      faviconURL:entry.DistilledURL().is_valid() ? entry.DistilledURL() : url];

  BOOL has_distillation_details =
      entry.DistilledState() == ReadingListEntry::PROCESSED &&
      entry.DistillationSize() != 0 && entry.DistillationTime() != 0;
  NSString* fullUrlString =
      base::SysUTF16ToNSString(url_formatter::FormatUrl(url));
  NSString* urlString =
      base::SysUTF16ToNSString(url_formatter::FormatUrl(url.GetOrigin()));
  NSString* title = base::SysUTF8ToNSString(entry.Title());
  item.title = [title length] ? title : fullUrlString;
  item.subtitle = urlString;
  item.distillationDate =
      has_distillation_details ? entry.DistillationTime() : 0;
  item.distillationSize =
      has_distillation_details ? entry.DistillationSize() : 0;
  return item;
}

- (void)setItem:(ReadingListCollectionViewItem*)item
     faviconURL:(const GURL&)url {
  item.faviconPageURL = url;

  __weak ReadingListCollectionViewItem* weakItem = item;
  __weak ReadingListCollectionViewController* weakSelf = self;
  void (^completionBlock)(FaviconAttributes* attributes) =
      ^(FaviconAttributes* attributes) {
        ReadingListCollectionViewItem* strongItem = weakItem;
        ReadingListCollectionViewController* strongSelf = weakSelf;
        if (!strongSelf || !strongItem) {
          return;
        }

        strongItem.attributes = attributes;

        if ([strongSelf.collectionViewModel hasItem:strongItem]) {
          [strongSelf reconfigureCellsForItems:@[ strongItem ]];
        }
      };

  [self.attributesProvider fetchFaviconAttributesForURL:url
                                             completion:completionBlock];
}

- (BOOL)hasItemInSection:(SectionIdentifier)sectionIdentifier {
  if (![self.collectionViewModel
          hasSectionForSectionIdentifier:sectionIdentifier]) {
    // No section.
    return NO;
  }

  NSInteger section =
      [self.collectionViewModel sectionForSectionIdentifier:sectionIdentifier];
  NSInteger numberOfItems =
      [self.collectionViewModel numberOfItemsInSection:section];

  return numberOfItems > 0;
}

- (void)collectionIsEmpty {
  if (self.collectionView.backgroundView) {
    return;
  }
  // The collection is empty, add background.
  self.collectionView.alwaysBounceVertical = NO;
  self.collectionView.backgroundView = _emptyCollectionBackground;
  [self.audience readingListHasItems:NO];
}

- (void)handleLongPress:(UILongPressGestureRecognizer*)gestureRecognizer {
  if (self.editor.editing ||
      gestureRecognizer.state != UIGestureRecognizerStateBegan) {
    return;
  }

  CGPoint touchLocation =
      [gestureRecognizer locationOfTouch:0 inView:self.collectionView];
  NSIndexPath* touchedItemIndexPath =
      [self.collectionView indexPathForItemAtPoint:touchLocation];
  if (!touchedItemIndexPath ||
      ![self.collectionViewModel hasItemAtIndexPath:touchedItemIndexPath]) {
    // Make sure there is an item at this position.
    return;
  }
  CollectionViewItem* touchedItem =
      [self.collectionViewModel itemAtIndexPath:touchedItemIndexPath];

  if (touchedItem == [self.collectionViewModel
                         headerForSection:touchedItemIndexPath.section] ||
      ![touchedItem isKindOfClass:[ReadingListCollectionViewItem class]]) {
    // Do not trigger context menu on headers.
    return;
  }

  ReadingListCollectionViewItem* readingListItem =
      base::mac::ObjCCastStrict<ReadingListCollectionViewItem>(touchedItem);

  [self.delegate readingListCollectionViewController:self
                           displayContextMenuForItem:readingListItem
                                             atPoint:touchLocation];
}

- (const ReadingListEntry*)readingListEntryForItem:
    (ReadingListCollectionViewItem*)item {
  const ReadingListEntry* readingListEntry =
      [self.dataSource entryWithURL:item.url];

  return readingListEntry;
}

#pragma mark - ReadingListToolbarDelegate

- (void)markPressed {
  if (![self.editor isEditing]) {
    return;
  }
  switch ([_toolbar state]) {
    case NoneSelected:
      [self markAllItemsAs];
      break;
    case OnlyUnreadSelected:
      [self markItemsReadAtIndexPath:self.collectionView
                                         .indexPathsForSelectedItems];
      break;
    case OnlyReadSelected:
      [self markItemsUnreadAtIndexPath:self.collectionView
                                           .indexPathsForSelectedItems];
      break;
    case MixedItemsSelected:
      [self markMixedItemsAs];
      break;
  }
}

- (void)deletePressed {
  if (![self.editor isEditing]) {
    return;
  }
  if ([_toolbar state] == NoneSelected) {
    [self deleteAllReadItems];
  } else {
    [self
        deleteItemsAtIndexPaths:self.collectionView.indexPathsForSelectedItems];
  }
}
- (void)enterEditingModePressed {
  if ([self.editor isEditing]) {
    return;
  }
  self.toolbarState = NoneSelected;
  [self.editor setEditing:YES animated:YES];
  [_toolbar setEditing:YES];
}

- (void)exitEditingModePressed {
  if (![self.editor isEditing]) {
    return;
  }
  [self exitEditingModeAnimated:YES];
}

#pragma mark - Private methods - Toolbar

- (void)updateToolbarState {
  BOOL readSelected = NO;
  BOOL unreadSelected = NO;

  if ([self.collectionView.indexPathsForSelectedItems count] == 0) {
    // No entry selected.
    self.toolbarState = NoneSelected;
    return;
  }

  // Sections for section identifiers.
  NSInteger sectionRead = NSNotFound;
  NSInteger sectionUnread = NSNotFound;

  if ([self hasItemInSection:SectionIdentifierRead]) {
    sectionRead = [self.collectionViewModel
        sectionForSectionIdentifier:SectionIdentifierRead];
  }
  if ([self hasItemInSection:SectionIdentifierUnread]) {
    sectionUnread = [self.collectionViewModel
        sectionForSectionIdentifier:SectionIdentifierUnread];
  }

  // Check selected sections.
  for (NSIndexPath* index in self.collectionView.indexPathsForSelectedItems) {
    if (index.section == sectionRead) {
      readSelected = YES;
    } else if (index.section == sectionUnread) {
      unreadSelected = YES;
    }
  }

  // Update toolbar state.
  if (readSelected) {
    if (unreadSelected) {
      // Read and Unread selected.
      self.toolbarState = MixedItemsSelected;
    } else {
      // Read selected.
      self.toolbarState = OnlyReadSelected;
    }
  } else if (unreadSelected) {
    // Unread selected.
    self.toolbarState = OnlyUnreadSelected;
  }
}

- (void)markAllItemsAs {
  [self initializeActionSheet];
  __weak ReadingListCollectionViewController* weakSelf = self;
  [_actionSheet addItemWithTitle:l10n_util::GetNSStringWithFixup(
                                     IDS_IOS_READING_LIST_MARK_ALL_READ_ACTION)
                          action:^{
                            [weakSelf markAllRead];
                          }
                           style:UIAlertActionStyleDefault];
  [_actionSheet
      addItemWithTitle:l10n_util::GetNSStringWithFixup(
                           IDS_IOS_READING_LIST_MARK_ALL_UNREAD_ACTION)
                action:^{
                  [weakSelf markAllUnread];
                }
                 style:UIAlertActionStyleDefault];
  [_actionSheet start];
}

- (void)markMixedItemsAs {
  [self initializeActionSheet];
  __weak ReadingListCollectionViewController* weakSelf = self;
  [_actionSheet
      addItemWithTitle:l10n_util::GetNSStringWithFixup(
                           IDS_IOS_READING_LIST_MARK_READ_BUTTON)
                action:^{
                  [weakSelf
                      markItemsReadAtIndexPath:weakSelf.collectionView
                                                   .indexPathsForSelectedItems];
                }
                 style:UIAlertActionStyleDefault];
  [_actionSheet
      addItemWithTitle:l10n_util::GetNSStringWithFixup(
                           IDS_IOS_READING_LIST_MARK_UNREAD_BUTTON)
                action:^{
                  [weakSelf
                      markItemsUnreadAtIndexPath:
                          weakSelf.collectionView.indexPathsForSelectedItems];
                }
                 style:UIAlertActionStyleDefault];
  [_actionSheet start];
}

- (void)initializeActionSheet {
  _actionSheet = [_toolbar actionSheetForMarkWithBaseViewController:self];

  [_actionSheet addItemWithTitle:l10n_util::GetNSStringWithFixup(IDS_CANCEL)
                          action:nil
                           style:UIAlertActionStyleCancel];
}

- (void)markAllRead {
  if (![self.editor isEditing]) {
    return;
  }
  if (![self hasItemInSection:SectionIdentifierUnread]) {
    [self exitEditingModeAnimated:YES];
    return;
  }

  [self updateItemsInSectionIdentifier:SectionIdentifierUnread
                     usingEntryUpdater:^(const GURL& url) {
                       [self.dataSource setReadStatus:YES forURL:url];
                     }];

  [self exitEditingModeAnimated:YES];
  [self moveItemsFromSection:SectionIdentifierUnread
                   toSection:SectionIdentifierRead];
}

- (void)markAllUnread {
  if (![self hasItemInSection:SectionIdentifierRead]) {
    [self exitEditingModeAnimated:YES];
    return;
  }

  [self updateItemsInSectionIdentifier:SectionIdentifierRead
                     usingEntryUpdater:^(const GURL& url) {
                       [self.dataSource setReadStatus:NO forURL:url];
                     }];

  [self exitEditingModeAnimated:YES];
  [self moveItemsFromSection:SectionIdentifierRead
                   toSection:SectionIdentifierUnread];
}

- (void)markItemsReadAtIndexPath:(NSArray*)indexPaths {
  base::RecordAction(base::UserMetricsAction("MobileReadingListMarkRead"));
  NSArray* sortedIndexPaths =
      [indexPaths sortedArrayUsingSelector:@selector(compare:)];
  [self updateIndexPaths:sortedIndexPaths
       usingEntryUpdater:^(const GURL& url) {
         [self.dataSource setReadStatus:YES forURL:url];
       }];

  [self exitEditingModeAnimated:YES];
  [self moveSelectedItems:sortedIndexPaths toSection:SectionIdentifierRead];
}

- (void)markItemsUnreadAtIndexPath:(NSArray*)indexPaths {
  base::RecordAction(base::UserMetricsAction("MobileReadingListMarkUnread"));
  NSArray* sortedIndexPaths =
      [indexPaths sortedArrayUsingSelector:@selector(compare:)];
  [self updateIndexPaths:sortedIndexPaths
       usingEntryUpdater:^(const GURL& url) {
         [self.dataSource setReadStatus:NO forURL:url];
       }];

  [self exitEditingModeAnimated:YES];
  [self moveSelectedItems:sortedIndexPaths toSection:SectionIdentifierUnread];
}

- (void)deleteAllReadItems {
  base::RecordAction(base::UserMetricsAction("MobileReadingListDeleteRead"));
  if (![self hasItemInSection:SectionIdentifierRead]) {
    [self exitEditingModeAnimated:YES];
    return;
  }

  [self updateItemsInSectionIdentifier:SectionIdentifierRead
                     usingEntryUpdater:^(const GURL& url) {
                       [self logDeletionHistogramsForEntry:url];
                       [self.dataSource removeEntryWithURL:url];
                     }];

  [self exitEditingModeAnimated:YES];
  [self.collectionView performBatchUpdates:^{
    NSInteger readSection = [self.collectionViewModel
        sectionForSectionIdentifier:SectionIdentifierRead];
    [self.collectionView
        deleteSections:[NSIndexSet indexSetWithIndex:readSection]];
    [self.collectionViewModel
        removeSectionWithIdentifier:SectionIdentifierRead];
  }
      completion:^(BOOL) {
        // Reload data to take into account possible sync events.
        [self applyPendingUpdates];
      }];
  // As we modified the section in the batch update block, remove the section in
  // another block.
  [self removeEmptySections];
}

- (void)deleteItemsAtIndexPaths:(NSArray*)indexPaths {
  [self updateIndexPaths:indexPaths
       usingEntryUpdater:^(const GURL& url) {
         [self logDeletionHistogramsForEntry:url];
         [self.dataSource removeEntryWithURL:url];
       }];

  [self exitEditingModeAnimated:YES];

  [self.collectionView performBatchUpdates:^{
    [self collectionView:self.collectionView
        willDeleteItemsAtIndexPaths:indexPaths];

    [self.collectionView deleteItemsAtIndexPaths:indexPaths];
  }
      completion:^(BOOL) {
        // Reload data to take into account possible sync events.
        [self applyPendingUpdates];
      }];
  // As we modified the section in the batch update block, remove the section in
  // another block.
  [self removeEmptySections];
}

- (void)updateItemsInSectionIdentifier:(SectionIdentifier)identifier
                     usingEntryUpdater:(EntryUpdater)updater {
  self.shouldMonitorDataSource = NO;
  auto token = [self.dataSource beginBatchUpdates];
  NSArray* readItems =
      [self.collectionViewModel itemsInSectionWithIdentifier:identifier];
  // Read the objects in reverse order to keep the order (last modified first).
  for (id item in [readItems reverseObjectEnumerator]) {
    ReadingListCollectionViewItem* readingListItem =
        base::mac::ObjCCastStrict<ReadingListCollectionViewItem>(item);
    if (updater)
      updater(readingListItem.url);
  }
  token.reset();
  self.shouldMonitorDataSource = YES;
}

- (void)updateIndexPaths:(NSArray<NSIndexPath*>*)indexPaths
       usingEntryUpdater:(EntryUpdater)updater {
  self.shouldMonitorDataSource = NO;
  auto token = [self.dataSource beginBatchUpdates];
  // Read the objects in reverse order to keep the order (last modified first).
  for (NSIndexPath* index in [indexPaths reverseObjectEnumerator]) {
    CollectionViewItem* cell = [self.collectionViewModel itemAtIndexPath:index];
    ReadingListCollectionViewItem* readingListItem =
        base::mac::ObjCCastStrict<ReadingListCollectionViewItem>(cell);
    if (updater)
      updater(readingListItem.url);
  }
  // Leave the batch update while it is not monitored.
  token.reset();
  self.shouldMonitorDataSource = YES;
}

- (void)logDeletionHistogramsForEntry:(const GURL&)url {
  const ReadingListEntry* entry = [self.dataSource entryWithURL:url];

  if (!entry)
    return;

  int64_t firstRead = entry->FirstReadTime();
  if (firstRead > 0) {
    // Log 0 if the entry has never been read.
    firstRead = (base::Time::Now() - base::Time::UnixEpoch()).InMicroseconds() -
                firstRead;
    // Convert it to hours.
    firstRead = firstRead / base::Time::kMicrosecondsPerHour;
  }
  UMA_HISTOGRAM_COUNTS_10000("ReadingList.FirstReadAgeOnDeletion", firstRead);

  int64_t age = (base::Time::Now() - base::Time::UnixEpoch()).InMicroseconds() -
                entry->CreationTime();
  // Convert it to hours.
  age = age / base::Time::kMicrosecondsPerHour;
  if (entry->IsRead())
    UMA_HISTOGRAM_COUNTS_10000("ReadingList.Read.AgeOnDeletion", age);
  else
    UMA_HISTOGRAM_COUNTS_10000("ReadingList.Unread.AgeOnDeletion", age);
}

- (void)moveItemsFromSection:(SectionIdentifier)sourceSectionIdentifier
                   toSection:(SectionIdentifier)destinationSectionIdentifier {
  NSInteger sourceSection = [self.collectionViewModel
      sectionForSectionIdentifier:sourceSectionIdentifier];
  NSInteger numberOfSourceItems =
      [self.collectionViewModel numberOfItemsInSection:sourceSection];

  NSMutableArray* sortedIndexPaths = [NSMutableArray array];

  for (int index = 0; index < numberOfSourceItems; index++) {
    NSIndexPath* itemIndex =
        [NSIndexPath indexPathForItem:index inSection:sourceSection];
    [sortedIndexPaths addObject:itemIndex];
  }

  [self moveSelectedItems:sortedIndexPaths
                toSection:destinationSectionIdentifier];
}

- (void)moveSelectedItems:(NSArray*)sortedIndexPaths
                toSection:(SectionIdentifier)sectionIdentifier {
  // Reconfigure cells, allowing the custom actions to be updated.
  [self reconfigureCellsAtIndexPaths:sortedIndexPaths];

  NSInteger sectionCreatedIndex = [self initializeSection:sectionIdentifier];

  [self.collectionView performBatchUpdates:^{
    NSInteger section = [self.collectionViewModel
        sectionForSectionIdentifier:sectionIdentifier];

    NSInteger newItemIndex = 0;
    for (NSIndexPath* index in sortedIndexPaths) {
      // The |sortedIndexPaths| is a copy of the index paths before the
      // destination section has been added if necessary. The section part of
      // the index potentially needs to be updated.
      NSInteger updatedSection = index.section;
      if (updatedSection >= sectionCreatedIndex)
        updatedSection++;
      if (updatedSection == section) {
        // The item is already in the targeted section, there is no need to move
        // it.
        continue;
      }

      NSIndexPath* updatedIndex =
          [NSIndexPath indexPathForItem:index.item inSection:updatedSection];
      NSIndexPath* indexForModel =
          [NSIndexPath indexPathForItem:index.item - newItemIndex
                              inSection:updatedSection];

      // Index of the item in the new section. The newItemIndex is the index of
      // this item in the targeted section.
      NSIndexPath* newIndexPath =
          [NSIndexPath indexPathForItem:newItemIndex++ inSection:section];

      [self collectionView:self.collectionView
          willMoveItemAtIndexPath:indexForModel
                      toIndexPath:newIndexPath];
      [self.collectionView moveItemAtIndexPath:updatedIndex
                                   toIndexPath:newIndexPath];
    }
  }
      completion:^(BOOL) {
        // Reload data to take into account possible sync events.
        [self applyPendingUpdates];
      }];
  // As we modified the section in the batch update block, remove the section in
  // another block.
  [self removeEmptySections];
}

- (NSInteger)initializeSection:(SectionIdentifier)sectionIdentifier {
  if (![self.collectionViewModel
          hasSectionForSectionIdentifier:sectionIdentifier]) {
    // The new section IndexPath will be 1 if it is the read section with
    // items in the unread section, 0 otherwise.
    BOOL hasNonEmptySectionAbove =
        sectionIdentifier == SectionIdentifierRead &&
        [self hasItemInSection:SectionIdentifierUnread];
    NSInteger sectionIndex = hasNonEmptySectionAbove ? 1 : 0;

    [self.collectionView performBatchUpdates:^{
      [self.collectionViewModel insertSectionWithIdentifier:sectionIdentifier
                                                    atIndex:sectionIndex];

      [self.collectionViewModel
                         setHeader:[self headerForSection:sectionIdentifier]
          forSectionWithIdentifier:sectionIdentifier];

      [self.collectionView
          insertSections:[NSIndexSet indexSetWithIndex:sectionIndex]];
    }
                                  completion:nil];

    return sectionIndex;
  }
  return NSIntegerMax;
}

- (CollectionViewTextItem*)headerForSection:
    (SectionIdentifier)sectionIdentifier {
  CollectionViewTextItem* header =
      [[CollectionViewTextItem alloc] initWithType:ItemTypeHeader];

  switch (sectionIdentifier) {
    case SectionIdentifierRead:
      header.text = l10n_util::GetNSString(IDS_IOS_READING_LIST_READ_HEADER);
      break;
    case SectionIdentifierUnread:
      header.text = l10n_util::GetNSString(IDS_IOS_READING_LIST_UNREAD_HEADER);
      break;
  }
  header.textColor = [[MDCPalette greyPalette] tint500];
  return header;
}

- (void)removeEmptySections {
  [self.collectionView performBatchUpdates:^{

    SectionIdentifier a[] = {SectionIdentifierRead, SectionIdentifierUnread};
    for (size_t i = 0; i < arraysize(a); i++) {
      SectionIdentifier sectionIdentifier = a[i];

      if ([self.collectionViewModel
              hasSectionForSectionIdentifier:sectionIdentifier] &&
          ![self hasItemInSection:sectionIdentifier]) {
        NSInteger section = [self.collectionViewModel
            sectionForSectionIdentifier:sectionIdentifier];

        [self.collectionView
            deleteSections:[NSIndexSet indexSetWithIndex:section]];
        [self.collectionViewModel
            removeSectionWithIdentifier:sectionIdentifier];
      }
    }
  }
                                completion:nil];
  if (!self.dataSource.hasElements) {
    [self collectionIsEmpty];
  } else {
    [_toolbar setHasReadItem:self.dataSource.hasRead];
  }
}

- (void)exitEditingModeAnimated:(BOOL)animated {
  [_actionSheet stop];
  [self.editor setEditing:NO animated:animated];
  [_toolbar setEditing:NO];
}

@end
