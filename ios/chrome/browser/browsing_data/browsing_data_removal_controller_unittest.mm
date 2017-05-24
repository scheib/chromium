// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/browsing_data/browsing_data_removal_controller.h"

#include <memory>

#import "base/test/ios/wait_util.h"
#include "ios/chrome/browser/browser_state/test_chrome_browser_state.h"
#include "ios/web/public/test/web_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#import "third_party/ocmock/gtest_support.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

typedef web::WebTest BrowsingDataRemovalControllerTest;

// Tests that |removeIOSSpecificIncognitoBrowsingDataFromBrowserState:| can
// finish performing its operation even when a BrowserState is destroyed.
TEST_F(BrowsingDataRemovalControllerTest, PerformAfterBrowserStateDestruction) {
  __block BOOL block_was_called = NO;
  id mock_delegate = [OCMockObject
      mockForProtocol:@protocol(BrowsingDataRemovalControllerDelegate)];
  BrowsingDataRemovalController* removal_controller =
      [[BrowsingDataRemovalController alloc] initWithDelegate:mock_delegate];

  TestChromeBrowserState::Builder builder;
  std::unique_ptr<TestChromeBrowserState> browser_state = builder.Build();
  ios::ChromeBrowserState* otr_browser_state =
      browser_state->GetOffTheRecordChromeBrowserState();
  int remove_all_mask = ~0;
  [removal_controller
      removeIOSSpecificIncognitoBrowsingDataFromBrowserState:otr_browser_state
                                                        mask:remove_all_mask
                                           completionHandler:^{
                                             block_was_called = YES;
                                           }];
  // Destroy the BrowserState immediately.
  browser_state->DestroyOffTheRecordChromeBrowserState();

  base::test::ios::WaitUntilCondition(^bool() {
    return block_was_called;
  });
}
