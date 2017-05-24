// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/web/repost_form_tab_helper.h"

#import "ios/chrome/browser/ui/alert_coordinator/repost_form_coordinator.h"
#import "ios/chrome/browser/ui/util/top_view_controller.h"

DEFINE_WEB_STATE_USER_DATA_KEY(RepostFormTabHelper);

RepostFormTabHelper::RepostFormTabHelper(web::WebState* web_state)
    : web::WebStateObserver(web_state) {}

RepostFormTabHelper::~RepostFormTabHelper() = default;

void RepostFormTabHelper::PresentDialog(
    CGPoint location,
    const base::Callback<void(bool)>& callback) {
  UIViewController* top_controller =
      top_view_controller::TopPresentedViewControllerFrom(
          [UIApplication sharedApplication].keyWindow.rootViewController);

  base::Callback<void(bool)> local_callback(callback);
  coordinator_.reset([[RepostFormCoordinator alloc]
      initWithBaseViewController:top_controller
                  dialogLocation:location
                        webState:web_state()
               completionHandler:^(BOOL should_continue) {
                 local_callback.Run(should_continue);
               }]);
  [coordinator_ start];
}

void RepostFormTabHelper::DidStartNavigation(web::NavigationContext*) {
  coordinator_.reset();
}

void RepostFormTabHelper::WebStateDestroyed() {
  coordinator_.reset();
}
