// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/settings/compose_email_handler_collection_view_controller.h"

#import "ios/chrome/browser/ui/collection_view/cells/collection_view_text_item.h"
#import "ios/chrome/browser/ui/collection_view/collection_view_controller_test.h"
#import "ios/chrome/browser/web/fake_mailto_handler_helpers.h"
#import "ios/chrome/browser/web/mailto_handler_system_mail.h"
#import "ios/chrome/browser/web/mailto_url_rewriter.h"
#include "ios/chrome/grit/ios_strings.h"
#import "ios/third_party/material_components_ios/src/components/Palettes/src/MDCPalettes.h"
#include "testing/gtest_mac.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#pragma mark - MailtoURLRewriter private interface for testing.

@interface MailtoURLRewriter ()
- (void)addMailtoApps:(NSArray<MailtoHandler*>*)handlerApps;
@end

#pragma mark - ComposeEmailHandlerCollectionViewControllerTest

class ComposeEmailHandlerCollectionViewControllerTest
    : public CollectionViewControllerTest {
 protected:
  // Before CreateController() is called, set |handers_| and optionally
  // |defaultHandlerID_| ivars. They will be used to seed the construction of
  // the MailtoURLRewriter which in turn used for the construction of the
  // CollectionViewController.
  CollectionViewController* InstantiateController() override {
    rewriter_ = [[MailtoURLRewriter alloc] init];
    [rewriter_ addMailtoApps:handlers_];
    if (defaultHandlerID_)
      [rewriter_ setDefaultHandlerID:defaultHandlerID_];
    return [[ComposeEmailHandlerCollectionViewController alloc]
        initWithRewriter:rewriter_];
  }

  // |handlers_| and |defaultHandlerID_| must be set before first call to
  // CreateController().
  NSArray<MailtoHandler*>* handlers_;
  NSString* defaultHandlerID_;
  MailtoURLRewriter* rewriter_;
};

TEST_F(ComposeEmailHandlerCollectionViewControllerTest, TestConstructor) {
  handlers_ = @[
    [[MailtoHandlerSystemMail alloc] init],
    [[FakeMailtoHandlerGmailInstalled alloc] init],
    [[FakeMailtoHandlerForTesting alloc] init]
  ];
  CreateController();
  CheckController();
  CheckTitleWithId(IDS_IOS_COMPOSE_EMAIL_SETTING);

  // Checks that there is one section with all the available MailtoHandler
  // objects listed.
  ASSERT_EQ(1, NumberOfSections());
  // Array returned by -defaultHandlers is sorted by the name of the Mail app
  // and may not be in the same order as |handlers_|.
  NSArray<MailtoHandler*>* handlers = [rewriter_ defaultHandlers];
  int number_of_handlers = [handlers count];
  EXPECT_EQ(number_of_handlers, NumberOfItemsInSection(0));
  for (int index = 0; index < number_of_handlers; ++index) {
    MailtoHandler* handler = handlers[index];
    CollectionViewTextItem* item = GetCollectionViewItem(0, index);
    // Checks that the title displayed is the name of the MailtoHandler.
    EXPECT_NSEQ([handler appName], item.text);
    EXPECT_FALSE(item.detailText);
    // Checks that text cells are displayed differently depending on the
    // availability of the handlers.
    UIColor* darkestTint = [[MDCPalette greyPalette] tint900];
    if ([handler isAvailable]) {
      EXPECT_EQ(darkestTint, item.textColor);
      EXPECT_NE(UIAccessibilityTraitNotEnabled, item.accessibilityTraits);
    } else {
      EXPECT_NE(darkestTint, item.textColor);
      EXPECT_EQ(UIAccessibilityTraitNotEnabled, item.accessibilityTraits);
    }
  }
}

TEST_F(ComposeEmailHandlerCollectionViewControllerTest, TestSelection) {
  handlers_ = @[
    [[MailtoHandlerSystemMail alloc] init],
    [[FakeMailtoHandlerGmailInstalled alloc] init]
  ];
  // The UI will come up with the first handler listed in |handlers_|
  // in the selected state.
  defaultHandlerID_ = [handlers_[0] appStoreID];
  CreateController();
  CheckController();

  // Have an observer to make sure that selecting in the UI causes the
  // observer to be called.
  CountingMailtoURLRewriterObserver* observer =
      [[CountingMailtoURLRewriterObserver alloc] init];
  [rewriter_ setObserver:observer];

  // The array of |handlers| here is sorted for display and may not be in the
  // same order as |handlers_|. Finds another entry in the |handlers| that is
  // not currently selected and use that as the new selection. This test
  // must set up at least two handlers in |handlers_| which guarantees that
  // a new |selection| must be found, thus the DCHECK_GE.
  NSArray<MailtoHandler*>* handlers = [rewriter_ defaultHandlers];
  int selection = -1;
  int number_of_handlers = [handlers count];
  for (int index = 0; index < number_of_handlers; ++index) {
    if (![defaultHandlerID_ isEqualToString:[handlers[index] appStoreID]]) {
      selection = index;
      break;
    }
  }
  DCHECK_GE(selection, 0);
  // Trigger a selection in the Collection View Controller.
  [controller() collectionView:[controller() collectionView]
      didSelectItemAtIndexPath:[NSIndexPath indexPathForRow:selection
                                                  inSection:0]];
  // Verify that the observer has been called and new selection has been set.
  EXPECT_EQ(1, [observer changeCount]);
  EXPECT_NSEQ([handlers[selection] appStoreID], [rewriter_ defaultHandlerID]);
}
