// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/settings/content_settings_collection_view_controller.h"

#include "ios/chrome/browser/browser_state/test_chrome_browser_state.h"
#include "ios/chrome/browser/experimental_flags.h"
#import "ios/chrome/browser/ui/collection_view/cells/collection_view_detail_item.h"
#import "ios/chrome/browser/ui/collection_view/collection_view_controller_test.h"
#include "ios/chrome/grit/ios_strings.h"
#include "ios/web/public/test/test_web_thread_bundle.h"
#include "testing/gtest_mac.h"
#include "ui/base/l10n/l10n_util.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

class ContentSettingsCollectionViewControllerTest
    : public CollectionViewControllerTest {
 protected:
  void SetUp() override {
    CollectionViewControllerTest::SetUp();
    TestChromeBrowserState::Builder test_cbs_builder;
    chrome_browser_state_ = test_cbs_builder.Build();
  }

  CollectionViewController* InstantiateController() override {
    return [[ContentSettingsCollectionViewController alloc]
        initWithBrowserState:chrome_browser_state_.get()];
  }

 private:
  web::TestWebThreadBundle thread_bundle_;
  std::unique_ptr<TestChromeBrowserState> chrome_browser_state_;
};

TEST_F(ContentSettingsCollectionViewControllerTest, TestModel) {
  CreateController();
  CheckController();
  CheckTitleWithId(IDS_IOS_CONTENT_SETTINGS_TITLE);

  ASSERT_EQ(1, NumberOfSections());
  // Compose Email section is shown only if experiment is turned on.
  bool show_compose_email_section =
      !experimental_flags::IsNativeAppLauncherEnabled();
  int expectedNumberOfItems = show_compose_email_section ? 3 : 2;
  EXPECT_EQ(expectedNumberOfItems, NumberOfItemsInSection(0));
  CheckDetailItemTextWithIds(IDS_IOS_BLOCK_POPUPS, IDS_IOS_SETTING_ON, 0, 0);
  CheckDetailItemTextWithIds(IDS_IOS_TRANSLATE_SETTING, IDS_IOS_SETTING_ON, 0,
                             1);
  if (show_compose_email_section) {
    CollectionViewDetailItem* item = GetCollectionViewItem(0, 2);
    EXPECT_NSEQ(l10n_util::GetNSString(IDS_IOS_COMPOSE_EMAIL_SETTING),
                item.text);
  }
}

}  // namespace
