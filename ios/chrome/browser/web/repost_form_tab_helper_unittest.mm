// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/web/repost_form_tab_helper.h"

#import <UIKit/UIKit.h>

#include "base/bind.h"
#include "base/mac/foundation_util.h"
#include "base/test/ios/wait_util.h"
#include "components/strings/grit/components_strings.h"
#include "ios/chrome/browser/ui/ui_util.h"
#import "ios/chrome/test/scoped_key_window.h"
#import "ios/web/public/test/fakes/test_web_state.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"
#include "ui/base/l10n/l10n_util.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
// Test location passed to RepostFormTabHelper.
const CGFloat kDialogHLocation = 10;
const CGFloat kDialogVLocation = 20;

// No-op callback for RepostFormTabHelper.
void IgnoreBool(bool) {}
}  // namespace

// Test fixture for RepostFormTabHelper class.
class RepostFormTabHelperTest : public PlatformTest {
 protected:
  RepostFormTabHelperTest()
      : web_state_(new web::TestWebState()),
        location_(CGPointMake(kDialogHLocation, kDialogVLocation)),
        view_controller_([[UIViewController alloc] init]) {
    UIView* view = [[UIView alloc] initWithFrame:view_controller_.view.bounds];
    [view_controller_.view addSubview:view];
    [scoped_key_window_.Get() setRootViewController:view_controller_];

    web_state_->SetView(view);
    web_state_->SetWebUsageEnabled(true);

    RepostFormTabHelper::CreateForWebState(web_state_.get());
  }

  // Presents a repost form dialog using RepostFormTabHelperTest.
  void PresentDialog() {
    ASSERT_FALSE(GetAlertController());
    auto* helper = RepostFormTabHelper::FromWebState(web_state_.get());
    helper->PresentDialog(location_, base::Bind(&IgnoreBool));
    ASSERT_TRUE(GetAlertController());
  }

  // Return presented view controller as UIAlertController.
  UIAlertController* GetAlertController() const {
    return base::mac::ObjCCastStrict<UIAlertController>(
        view_controller_.presentedViewController);
  }

 protected:
  std::unique_ptr<web::TestWebState> web_state_;

 private:
  CGPoint location_;
  ScopedKeyWindow scoped_key_window_;
  UIViewController* view_controller_;
};

// Tests presented repost form dialog.
TEST_F(RepostFormTabHelperTest, Preseting) {
  PresentDialog();
  EXPECT_FALSE(GetAlertController().title);
  EXPECT_TRUE([GetAlertController().message
      containsString:l10n_util::GetNSString(IDS_HTTP_POST_WARNING_TITLE)]);
  EXPECT_TRUE([GetAlertController().message
      containsString:l10n_util::GetNSString(IDS_HTTP_POST_WARNING)]);
  if (IsIPadIdiom()) {
    UIPopoverPresentationController* popover_presentation_controller =
        GetAlertController().popoverPresentationController;
    CGRect source_rect = popover_presentation_controller.sourceRect;
    EXPECT_EQ(kDialogHLocation, CGRectGetMinX(source_rect));
    EXPECT_EQ(kDialogVLocation, CGRectGetMinY(source_rect));
  }
}

// Tests that dialog is dismissed when WebState is destroyed.
TEST_F(RepostFormTabHelperTest, DismissingOnWebViewDestruction) {
  PresentDialog();
  web_state_.reset();
  base::test::ios::WaitUntilCondition(^{
    return GetAlertController() != nil;
  });
}

// Tests that dialog is dismissed after provisional navigation has started.
TEST_F(RepostFormTabHelperTest, DismissingOnNavigationStart) {
  PresentDialog();
  web_state_->OnNavigationStarted(nullptr /* navigation_context */);
  base::test::ios::WaitUntilCondition(^{
    return GetAlertController() != nil;
  });
}
