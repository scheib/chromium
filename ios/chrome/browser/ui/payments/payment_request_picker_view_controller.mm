// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/payments/payment_request_picker_view_controller.h"

#import "base/logging.h"
#import "base/mac/foundation_util.h"
#import "ios/chrome/browser/ui/payments/payment_request_picker_row.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

NSString* const kPaymentRequestPickerRowAccessibilityID =
    @"kPaymentRequestPickerRowAccessibilityID";
NSString* const kPaymentRequestPickerSearchBarAccessibilityID =
    @"kPaymentRequestPickerSearchBarAccessibilityID";

namespace {
NSString* const kPaymentRequestPickerViewControllerAccessibilityID =
    @"kPaymentRequestPickerViewControllerAccessibilityID";
}  // namespace

@interface PaymentRequestPickerViewController ()<UISearchResultsUpdating>

// Search controller that contains search bar.
@property(nonatomic, strong) UISearchController* searchController;

// Full data set displayed when tableView is not filtered.
@property(nonatomic, strong) NSArray<PickerRow*>* allRows;

// Displayed rows in the tableView.
@property(nonatomic, strong) NSArray<PickerRow*>* displayedRows;

// Selected row.
@property(nonatomic, strong) PickerRow* selectedRow;

@property(nonatomic, strong)
    NSDictionary<NSString*, NSArray<PickerRow*>*>* sectionTitleToSectionRowsMap;

@end

@implementation PaymentRequestPickerViewController
@synthesize searchController = _searchController;
@synthesize allRows = _allRows;
@synthesize displayedRows = _displayedRows;
@synthesize selectedRow = _selectedRow;
@synthesize sectionTitleToSectionRowsMap = _sectionTitleToSectionRowsMap;
@synthesize delegate = _delegate;

- (instancetype)initWithRows:(NSArray<PickerRow*>*)rows
                    selected:(PickerRow*)selectedRow {
  self = [super initWithStyle:UITableViewStylePlain];
  if (self) {
    self.allRows = [rows sortedArrayUsingComparator:^NSComparisonResult(
                             PickerRow* row1, PickerRow* row2) {
      return [row1.label localizedCaseInsensitiveCompare:row2.label];
    }];
    self.selectedRow = selectedRow;
    // Default to displaying all the rows.
    self.displayedRows = self.allRows;
  }
  return self;
}

- (void)setDisplayedRows:(NSArray<PickerRow*>*)displayedRows {
  _displayedRows = displayedRows;

  // Update the mapping from section titles to rows in that section, for
  // currently displayed rows.
  [self updateSectionTitleToSectionRowsMap];
}

#pragma mark - UIViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  self.tableView.rowHeight = 48.0f;  // The same as MDCCellDefaultOneLineHeight.
  self.tableView.accessibilityIdentifier =
      kPaymentRequestPickerViewControllerAccessibilityID;

  self.searchController =
      [[UISearchController alloc] initWithSearchResultsController:nil];
  self.searchController.searchResultsUpdater = self;
  self.searchController.dimsBackgroundDuringPresentation = NO;
  self.searchController.hidesNavigationBarDuringPresentation = NO;
  self.searchController.searchBar.accessibilityIdentifier =
      kPaymentRequestPickerSearchBarAccessibilityID;
  self.tableView.tableHeaderView = self.searchController.searchBar;

  // Presentation of searchController will walk up the view controller hierarchy
  // until it finds the root view controller or one that defines a presentation
  // context. Make this class the presentation context so that the search
  // controller does not present on top of the navigation controller.
  self.definesPresentationContext = YES;
}

#pragma mark - UITableViewDataSource

- (NSArray<NSString*>*)sectionIndexTitlesForTableView:(UITableView*)tableView {
  return [self sectionTitles];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView*)tableView {
  return [[self sectionTitles] count];
}

- (NSString*)tableView:(UITableView*)tableView
    titleForHeaderInSection:(NSInteger)section {
  return [[self sectionTitles] objectAtIndex:section];
}

- (NSInteger)tableView:(UITableView*)tableView
    numberOfRowsInSection:(NSInteger)section {
  return [[self rowsInSection:section] count];
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
  UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:@"cell"];
  if (!cell) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:@"cell"];
    cell.isAccessibilityElement = YES;
    cell.accessibilityIdentifier = kPaymentRequestPickerRowAccessibilityID;
  }
  PickerRow* row =
      [[self rowsInSection:indexPath.section] objectAtIndex:indexPath.row];
  cell.textLabel.text = row.label;
  cell.accessoryType = (row == self.selectedRow)
                           ? UITableViewCellAccessoryCheckmark
                           : UITableViewCellAccessoryNone;
  if (row == self.selectedRow)
    cell.accessibilityTraits |= UIAccessibilityTraitSelected;
  else
    cell.accessibilityTraits &= ~UIAccessibilityTraitSelected;

  return cell;
}

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView*)tableView
    didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
  if (self.selectedRow) {
    NSIndexPath* oldSelectedIndexPath = [self indexPathForRow:self.selectedRow];
    self.selectedRow = nil;
    // Reload the previously selected row if it is displaying.
    if (oldSelectedIndexPath) {
      [self.tableView reloadRowsAtIndexPaths:@[ oldSelectedIndexPath ]
                            withRowAnimation:UITableViewRowAnimationFade];
    }
  }

  self.selectedRow =
      [[self rowsInSection:indexPath.section] objectAtIndex:indexPath.row];
  // Reload the newly selected row.
  [self.tableView reloadRowsAtIndexPaths:@[ indexPath ]
                        withRowAnimation:UITableViewRowAnimationFade];

  [_delegate paymentRequestPickerViewController:self
                                   didSelectRow:self.selectedRow];
}

#pragma mark - UISearchResultsUpdating

- (void)updateSearchResultsForSearchController:
    (UISearchController*)searchController {
  NSString* searchText = searchController.searchBar.text;

  // Filter |allRows| for |searchText| and reload the tableView. If |searchText|
  // is empty, tableView will be loaded with |allRows|.
  if (searchText.length != 0) {
    // The search is case-insensitive and ignores diacritics.
    NSPredicate* predicate =
        [NSPredicate predicateWithFormat:@"label CONTAINS[cd] %@", searchText];
    self.displayedRows = [self.allRows filteredArrayUsingPredicate:predicate];
  } else {
    self.displayedRows = self.allRows;
  }

  [self.tableView reloadData];
}

#pragma mark - Private

// Creates a mapping from section titles to rows in that section, for currently
// displaying rows, and updates |sectionTitleToSectionRowsMap|.
- (void)updateSectionTitleToSectionRowsMap {
  NSMutableDictionary<NSString*, NSArray<PickerRow*>*>*
      sectionTitleToSectionRowsMap = [[NSMutableDictionary alloc] init];

  for (PickerRow* row in self.displayedRows) {
    NSString* sectionTitle = [self sectionTitleForRow:row];
    NSMutableArray<PickerRow*>* sectionRows =
        base::mac::ObjCCastStrict<NSMutableArray<PickerRow*>>(
            sectionTitleToSectionRowsMap[sectionTitle]);
    if (!sectionRows)
      sectionRows = [[NSMutableArray alloc] init];
    [sectionRows addObject:row];
    [sectionTitleToSectionRowsMap setObject:sectionRows forKey:sectionTitle];
  }

  self.sectionTitleToSectionRowsMap = sectionTitleToSectionRowsMap;
}

// Returns the indexPath for |row| by calculating its section and its index
// within the section. Returns nil if the row is not currently displaying.
- (NSIndexPath*)indexPathForRow:(PickerRow*)row {
  NSString* sectionTitle = [self sectionTitleForRow:row];

  NSInteger section = [[self sectionTitles] indexOfObject:sectionTitle];
  if (section == NSNotFound)
    return nil;

  NSInteger indexInSection =
      [self.sectionTitleToSectionRowsMap[sectionTitle] indexOfObject:row];
  if (indexInSection == NSNotFound)
    return nil;

  return [NSIndexPath indexPathForRow:indexInSection inSection:section];
}

// Returns the titles for the displayed sections in the tableView.
- (NSArray<NSString*>*)sectionTitles {
  return [[self.sectionTitleToSectionRowsMap allKeys]
      sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
}

// Returns the displayed rows in the given section.
- (NSArray<PickerRow*>*)rowsInSection:(NSInteger)section {
  NSArray<NSString*>* sectionTitles = [self sectionTitles];
  DCHECK(section >= 0 && section < static_cast<NSInteger>(sectionTitles.count));

  NSString* sectionTitle = [sectionTitles objectAtIndex:section];

  return self.sectionTitleToSectionRowsMap[sectionTitle];
}

// Returns the title for the section the given row gets added to. The section
// title for a row is the capitalized first letter of the label for that row.
- (NSString*)sectionTitleForRow:(PickerRow*)row {
  return [[row.label substringToIndex:1] uppercaseString];
}

- (NSString*)description {
  return kPaymentRequestPickerViewControllerAccessibilityID;
}

@end
