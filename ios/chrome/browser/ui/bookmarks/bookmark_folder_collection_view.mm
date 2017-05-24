// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/bookmarks/bookmark_folder_collection_view.h"

#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "ios/chrome/browser/bookmarks/bookmarks_utils.h"
#include "ios/chrome/browser/experimental_flags.h"
#import "ios/chrome/browser/ui/authentication/signin_promo_view.h"
#import "ios/chrome/browser/ui/authentication/signin_promo_view_configurator.h"
#import "ios/chrome/browser/ui/authentication/signin_promo_view_consumer.h"
#import "ios/chrome/browser/ui/authentication/signin_promo_view_mediator.h"
#import "ios/chrome/browser/ui/bookmarks/bookmark_collection_cells.h"
#import "ios/chrome/browser/ui/bookmarks/bookmark_promo_cell.h"
#import "ios/chrome/browser/ui/bookmarks/bookmark_signin_promo_cell.h"
#import "ios/chrome/browser/ui/bookmarks/bookmark_utils_ios.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using bookmarks::BookmarkNode;

namespace {
// Computes the cell size based on width.
CGSize PreferredCellSizeForWidth(UICollectionViewCell* cell, CGFloat width) {
  CGRect cellFrame = cell.frame;
  cellFrame.size.width = width;
  cellFrame.size.height = CGFLOAT_MAX;
  cell.frame = cellFrame;
  [cell setNeedsLayout];
  [cell layoutIfNeeded];
  CGSize result =
      [cell systemLayoutSizeFittingSize:UILayoutFittingCompressedSize
          withHorizontalFittingPriority:UILayoutPriorityRequired
                verticalFittingPriority:UILayoutPriorityDefaultLow];
  cellFrame.size = result;
  cell.frame = cellFrame;
  return result;
}
}

@interface BookmarkFolderCollectionView ()<BookmarkPromoCellDelegate,
                                           SigninPromoViewConsumer> {
  // A vector of folders to display in the collection view.
  std::vector<const BookmarkNode*> _subFolders;
  // A vector of bookmark urls to display in the collection view.
  std::vector<const BookmarkNode*> _subItems;

  // True if the promo is visible.
  BOOL _promoVisible;

  // Mediator, helper for the sign-in promo view.
  SigninPromoViewMediator* _signinPromoViewMediator;
}
@property(nonatomic, assign) const bookmarks::BookmarkNode* folder;

// Section indices.
@property(nonatomic, readonly, assign) NSInteger promoSection;
@property(nonatomic, readonly, assign) NSInteger folderSection;
@property(nonatomic, readonly, assign) NSInteger itemsSection;
@property(nonatomic, readonly, assign) NSInteger sectionCount;

@end

@implementation BookmarkFolderCollectionView
@synthesize delegate = _delegate;
@synthesize folder = _folder;

- (instancetype)initWithBrowserState:(ios::ChromeBrowserState*)browserState
                               frame:(CGRect)frame {
  self = [super initWithBrowserState:browserState frame:frame];
  if (self) {
    [self updateCollectionView];
  }
  return self;
}

- (void)setDelegate:(id<BookmarkFolderCollectionViewDelegate>)delegate {
  _delegate = delegate;
  [self promoStateChangedAnimated:NO];
}

- (NSInteger)promoSection {
  return [self shouldShowPromoCell] ? 0 : -1;
}

- (NSInteger)folderSection {
  return [self shouldShowPromoCell] ? 1 : 0;
}

- (NSInteger)itemsSection {
  return [self shouldShowPromoCell] ? 2 : 1;
}

- (NSInteger)sectionCount {
  return [self shouldShowPromoCell] ? 3 : 2;
}

- (void)updateCollectionView {
  if (!self.bookmarkModel->loaded())
    return;

  // Regenerate the list of all bookmarks.
  _subFolders = std::vector<const BookmarkNode*>();
  _subItems = std::vector<const BookmarkNode*>();

  if (self.folder) {
    int childCount = self.folder->child_count();
    for (int i = 0; i < childCount; ++i) {
      const BookmarkNode* node = self.folder->GetChild(i);
      if (node->is_folder())
        _subFolders.push_back(node);
      else
        _subItems.push_back(node);
    }

    bookmark_utils_ios::SortFolders(&_subFolders);
  }

  [self cancelAllFaviconLoads];
  [self.collectionView reloadData];
}

#pragma mark - BookmarkModelBridgeObserver Callbacks

- (void)bookmarkModelLoaded {
  [self updateCollectionView];
}

- (void)bookmarkNodeChanged:(const BookmarkNode*)bookmarkNode {
  // The base folder changed. Do nothing.
  if (bookmarkNode == self.folder)
    return;

  // A specific cell changed. Reload that cell.
  NSIndexPath* indexPath = [self indexPathForNode:bookmarkNode];

  if (indexPath) {
    // TODO(crbug.com/603661): Ideally, we would only reload the relevant index
    // path. However, calling reloadItemsAtIndexPaths:(0,0) immediately after
    // reloadData results in a exception: NSInternalInconsistencyException
    // 'request for index path for global index 2147483645 ...'
    // One solution would be to keep track of whether we've just called
    // reloadData, but that requires experimentation to determine how long we
    // have to wait before we can safely call reloadItemsAtIndexPaths.
    [self updateCollectionView];
  }
}

- (void)bookmarkNodeFaviconChanged:
    (const bookmarks::BookmarkNode*)bookmarkNode {
  // Only urls have favicons.
  DCHECK(bookmarkNode->is_url());

  // Update image of corresponding cell.
  NSIndexPath* indexPath = [self indexPathForNode:bookmarkNode];

  if (!indexPath)
    return;

  // Check that this cell is visible.
  NSArray* visiblePaths = [self.collectionView indexPathsForVisibleItems];
  if (![visiblePaths containsObject:indexPath])
    return;

  [self loadFaviconAtIndexPath:indexPath];
}

- (NSIndexPath*)indexPathForNode:(const bookmarks::BookmarkNode*)bookmarkNode {
  NSIndexPath* indexPath = nil;
  if (bookmarkNode->is_folder()) {
    std::vector<const BookmarkNode*>::iterator it =
        std::find(_subFolders.begin(), _subFolders.end(), bookmarkNode);
    if (it != _subFolders.end()) {
      ptrdiff_t index = std::distance(_subFolders.begin(), it);
      indexPath =
          [NSIndexPath indexPathForRow:index inSection:self.folderSection];
    }
  } else if (bookmarkNode->is_url()) {
    std::vector<const BookmarkNode*>::iterator it =
        std::find(_subItems.begin(), _subItems.end(), bookmarkNode);
    if (it != _subItems.end()) {
      ptrdiff_t index = std::distance(_subItems.begin(), it);
      indexPath =
          [NSIndexPath indexPathForRow:index inSection:self.itemsSection];
    }
  }
  return indexPath;
}

- (void)bookmarkNodeDeleted:(const BookmarkNode*)node
                 fromFolder:(const BookmarkNode*)folder {
  if (self.folder == node) {
    self.folder = nil;
    [self updateCollectionView];
  }
}

- (void)bookmarkNodeChildrenChanged:(const BookmarkNode*)bookmarkNode {
  // The base folder's children changed. Reload everything.
  if (bookmarkNode == self.folder) {
    [self updateCollectionView];
    return;
  }

  // A subfolder's children changed. Reload that cell.
  std::vector<const BookmarkNode*>::iterator it =
      std::find(_subFolders.begin(), _subFolders.end(), bookmarkNode);
  if (it != _subFolders.end()) {
    // TODO(crbug.com/603661): Ideally, we would only reload the relevant index
    // path. However, calling reloadItemsAtIndexPaths:(0,0) immediately after
    // reloadData results in a exception: NSInternalInconsistencyException
    // 'request for index path for global index 2147483645 ...'
    // One solution would be to keep track of whether we've just called
    // reloadData, but that requires experimentation to determine how long we
    // have to wait before we can safely call reloadItemsAtIndexPaths.
    [self updateCollectionView];
  }
}

- (void)bookmarkNode:(const BookmarkNode*)bookmarkNode
     movedFromParent:(const BookmarkNode*)oldParent
            toParent:(const BookmarkNode*)newParent {
  if (oldParent == self.folder || newParent == self.folder) {
    // A folder was added or removed from the base folder.
    [self updateCollectionView];
  }
}

- (void)bookmarkModelRemovedAllNodes {
  self.folder = nil;
  [self updateCollectionView];
}

#pragma mark - Parent class overrides that affect functionality

- (void)collectionView:(UICollectionView*)collectionView
       willDisplayCell:(UICollectionViewCell*)cell
    forItemAtIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.section == self.itemsSection) {
    [self loadFaviconAtIndexPath:indexPath];
  }
}

- (void)didAddCellForEditingAtIndexPath:(NSIndexPath*)indexPath {
  const BookmarkNode* node = [self nodeAtIndexPath:indexPath];
  UICollectionViewCell* cell =
      [self.collectionView cellForItemAtIndexPath:indexPath];
  [self.delegate bookmarkCollectionView:self cell:cell addNodeForEditing:node];
}

- (void)didRemoveCellForEditingAtIndexPath:(NSIndexPath*)indexPath {
  const BookmarkNode* node = [self nodeAtIndexPath:indexPath];
  UICollectionViewCell* cell =
      [self.collectionView cellForItemAtIndexPath:indexPath];
  [self.delegate bookmarkCollectionView:self
                                   cell:cell
                   removeNodeForEditing:node];
}

- (void)didTapCellAtIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.section == self.promoSection) {
    // User tapped inside promo cell but not on one of the buttons. Ignore it.
    return;
  }

  const BookmarkNode* node = [self nodeAtIndexPath:indexPath];
  DCHECK(node);

  if (indexPath.section == self.folderSection) {
    [self.delegate bookmarkFolderCollectionView:self
                    selectedFolderForNavigation:node];
  } else {
    RecordBookmarkLaunch(BOOKMARK_LAUNCH_LOCATION_FOLDER);
    [self.delegate bookmarkCollectionView:self
                 selectedUrlForNavigation:node->url()];
  }
}

- (void)didTapMenuButtonAtIndexPath:(NSIndexPath*)indexPath
                             onView:(UIView*)view
                            forCell:(BookmarkItemCell*)cell {
  [self.delegate bookmarkCollectionView:self
                   wantsMenuForBookmark:[self nodeAtIndexPath:indexPath]
                                 onView:view
                                forCell:cell];
}

- (bookmark_cell::ButtonType)buttonTypeForCellAtIndexPath:
    (NSIndexPath*)indexPath {
  return self.editing ? bookmark_cell::ButtonNone : bookmark_cell::ButtonMenu;
}

- (BOOL)allowLongPressForCellAtIndexPath:(NSIndexPath*)indexPath {
  return !self.editing;
}

- (void)didLongPressCell:(UICollectionViewCell*)cell
             atIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.section == self.promoSection) {
    // User long-pressed inside promo cell. Ignore it.
    return;
  }

  [self.delegate bookmarkCollectionView:self
                       didLongPressCell:cell
                            forBookmark:[self nodeAtIndexPath:indexPath]];
}

- (BOOL)shouldSelectCellAtIndexPath:(NSIndexPath*)indexPath {
  return YES;
}

- (BOOL)cellIsSelectedForEditingAtIndexPath:(NSIndexPath*)indexPath {
  const BookmarkNode* node = [self nodeAtIndexPath:indexPath];
  const std::set<const BookmarkNode*>& editingNodes =
      [self.delegate nodesBeingEdited];
  return editingNodes.find(node) != editingNodes.end();
}

- (const BookmarkNode*)nodeAtIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.section == self.folderSection)
    return _subFolders[indexPath.row];
  if (indexPath.section == self.itemsSection)
    return _subItems[indexPath.row];

  NOTREACHED();
  return nullptr;
}

- (void)resetFolder:(const BookmarkNode*)folder {
  DCHECK(folder->is_folder());
  self.folder = folder;
  [self updateCollectionView];
}

- (CGFloat)interitemSpacingForSectionAtIndex:(NSInteger)section {
  CGFloat interitemSpacing = 0;
  SEL minimumInteritemSpacingSelector = @selector(collectionView:
                                                          layout:
                        minimumInteritemSpacingForSectionAtIndex:);
  if ([self.collectionView.delegate
          respondsToSelector:minimumInteritemSpacingSelector]) {
    id collectionViewDelegate = static_cast<id>(self.collectionView.delegate);
    interitemSpacing =
        [collectionViewDelegate collectionView:self.collectionView
                                              layout:self.collectionView
                                                         .collectionViewLayout
            minimumInteritemSpacingForSectionAtIndex:section];
  } else if ([self.collectionView.collectionViewLayout
                 isKindOfClass:[UICollectionViewFlowLayout class]]) {
    UICollectionViewFlowLayout* flowLayout =
        static_cast<UICollectionViewFlowLayout*>(
            self.collectionView.collectionViewLayout);
    interitemSpacing = flowLayout.minimumInteritemSpacing;
  }
  return interitemSpacing;
}

// Parent class override.
- (UIEdgeInsets)insetForSectionAtIndex:(NSInteger)section {
  UIEdgeInsets insets = [super insetForSectionAtIndex:section];
  if (section == self.folderSection)
    insets.bottom = [self interitemSpacingForSectionAtIndex:section] / 2.;
  else if (section == self.itemsSection)
    insets.top = [self interitemSpacingForSectionAtIndex:section] / 2.;
  else if (section == self.promoSection)
    (void)0;  // No insets to update.
  else
    NOTREACHED();
  return insets;
}

// Parent class override.
- (CGSize)cellSizeForIndexPath:(NSIndexPath*)indexPath {
  if ([self isPromoSection:indexPath.section]) {
    UICollectionViewCell* cell =
        [self.collectionView cellForItemAtIndexPath:indexPath];
    if (!cell) {
      // -[UICollectionView
      // dequeueReusableCellWithReuseIdentifier:forIndexPath:] cannot be used
      // here since this method is called by -[id<UICollectionViewDelegate>
      // collectionView:layout:sizeForItemAtIndexPath:]. This would generate
      // crash: SIGFPE, EXC_I386_DIV.
      if (experimental_flags::IsSigninPromoEnabled()) {
        DCHECK(_signinPromoViewMediator);
        BookmarkSigninPromoCell* signinPromoCell =
            [[BookmarkSigninPromoCell alloc]
                initWithFrame:CGRectMake(0, 0, 1000, 1000)];
        [[_signinPromoViewMediator createConfigurator]
            configureSigninPromoView:signinPromoCell.signinPromoView];
        cell = signinPromoCell;
      } else {
        cell = [[BookmarkPromoCell alloc] init];
      }
    }
    return PreferredCellSizeForWidth(cell, CGRectGetWidth(self.bounds));
  }
  return [super cellSizeForIndexPath:indexPath];
}

// Parent class override.
- (UICollectionViewCell*)cellAtIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.section == self.promoSection) {
    if (experimental_flags::IsSigninPromoEnabled()) {
      BookmarkSigninPromoCell* signinPromoCell = [self.collectionView
          dequeueReusableCellWithReuseIdentifier:[BookmarkSigninPromoCell
                                                     reuseIdentifier]
                                    forIndexPath:indexPath];
      signinPromoCell.signinPromoView.delegate = _signinPromoViewMediator;
      [[_signinPromoViewMediator createConfigurator]
          configureSigninPromoView:signinPromoCell.signinPromoView];
      __weak BookmarkFolderCollectionView* weakSelf = self;
      signinPromoCell.closeButtonAction = ^() {
        [weakSelf.delegate bookmarkCollectionViewDismissPromo:self];
      };
      return signinPromoCell;
    } else {
      BookmarkPromoCell* promoCell = [self.collectionView
          dequeueReusableCellWithReuseIdentifier:[BookmarkPromoCell
                                                     reuseIdentifier]
                                    forIndexPath:indexPath];
      promoCell.delegate = self;
      return promoCell;
    }
  }
  const BookmarkNode* node = [self nodeAtIndexPath:indexPath];

  if (indexPath.section == self.folderSection)
    return [self cellForFolder:node indexPath:indexPath];

  BookmarkItemCell* cell = [self cellForBookmark:node indexPath:indexPath];
  return cell;
}

- (BOOL)needsSectionHeaderForSection:(NSInteger)section {
  // Only show header when there is at least one element in the previous
  // section.
  if (section == 0)
    return NO;

  if ([self numberOfItemsInSection:(section - 1)] == 0)
    return NO;

  return YES;
}

// Parent class override.
- (CGSize)headerSizeForSection:(NSInteger)section {
  if ([self needsSectionHeaderForSection:section])
    return CGSizeMake(self.bounds.size.width,
                      [BookmarkHeaderSeparatorView preferredHeight]);

  return CGSizeZero;
}

// Parent class override.
- (UICollectionReusableView*)headerAtIndexPath:(NSIndexPath*)indexPath {
  if (![self needsSectionHeaderForSection:indexPath.section])
    return nil;

  BookmarkHeaderSeparatorView* view = [self.collectionView
      dequeueReusableSupplementaryViewOfKind:
          UICollectionElementKindSectionHeader
                         withReuseIdentifier:[BookmarkHeaderSeparatorView
                                                 reuseIdentifier]
                                forIndexPath:indexPath];
  view.backgroundColor = [UIColor colorWithWhite:1 alpha:1];
  return view;
}

- (NSInteger)numberOfItemsInSection:(NSInteger)section {
  if (section == self.folderSection)
    return _subFolders.size();
  if (section == self.itemsSection)
    return _subItems.size();
  if (section == self.promoSection)
    return 1;

  NOTREACHED();
  return -1;
}

- (NSInteger)numberOfSections {
  return self.sectionCount;
}

- (void)collectionViewScrolled {
  [self.delegate bookmarkCollectionViewDidScroll:self];
}

- (void)setEditing:(BOOL)editing animated:(BOOL)animated {
  [super setEditing:editing animated:animated];
  [self promoStateChangedAnimated:animated];
}

- (void)promoStateChangedAnimated:(BOOL)animate {
  BOOL newPromoState =
      !self.editing && self.folder &&
      self.folder->type() == BookmarkNode::MOBILE &&
      [self.delegate bookmarkCollectionViewShouldShowPromoCell:self];
  if (newPromoState != _promoVisible) {
    // This is awful, but until the old code to do the refresh when switching
    // in and out of edit mode is fixed, this is probably the cleanest thing to
    // do.
    _promoVisible = newPromoState;
    if (experimental_flags::IsSigninPromoEnabled()) {
      if (!_promoVisible) {
        _signinPromoViewMediator.consumer = nil;
        _signinPromoViewMediator = nil;
      } else {
        _signinPromoViewMediator = [[SigninPromoViewMediator alloc] init];
        _signinPromoViewMediator.consumer = self;
        _signinPromoViewMediator.accessPoint =
            signin_metrics::AccessPoint::ACCESS_POINT_BOOKMARK_MANAGER;
      }
    }
    [self.collectionView reloadData];
  }
}

#pragma mark - BookmarkPromoCellDelegate

- (void)bookmarkPromoCellDidTapSignIn:(BookmarkPromoCell*)bookmarkPromoCell {
  [self.delegate bookmarkCollectionViewShowSignIn:self];
}

- (void)bookmarkPromoCellDidTapDismiss:(BookmarkPromoCell*)bookmarkPromoCell {
  [self.delegate bookmarkCollectionViewDismissPromo:self];
}

#pragma mark - Promo Cell

- (BOOL)shouldShowPromoCell {
  return _promoVisible;
}

#pragma mark - SigninPromoViewConsumer

- (void)configureSigninPromoViewWithNewIdentity:(BOOL)newIdentity
                                   configurator:(SigninPromoViewConfigurator*)
                                                    configurator {
  DCHECK(_signinPromoViewMediator);
  if (newIdentity) {
    NSIndexSet* indexSet = [NSIndexSet indexSetWithIndex:self.promoSection];
    [self.collectionView reloadSections:indexSet];
    return;
  }
  NSIndexPath* indexPath =
      [NSIndexPath indexPathForRow:0 inSection:self.promoSection];
  BookmarkSigninPromoCell* signinPromoCell =
      static_cast<BookmarkSigninPromoCell*>(
          [self.collectionView cellForItemAtIndexPath:indexPath]);
  if (!signinPromoCell)
    return;
  [configurator configureSigninPromoView:signinPromoCell.signinPromoView];
}

@end
