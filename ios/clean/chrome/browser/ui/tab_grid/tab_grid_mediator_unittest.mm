// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/clean/chrome/browser/ui/tab_grid/tab_grid_mediator.h"

#include "base/memory/ptr_util.h"
#include "ios/chrome/browser/web_state_list/fake_web_state_list_delegate.h"
#include "ios/chrome/browser/web_state_list/web_state_list.h"
#import "ios/chrome/browser/web_state_list/web_state_list_observer_bridge.h"
#import "ios/clean/chrome/browser/ui/tab_grid/tab_grid_consumer.h"
#import "ios/web/public/test/fakes/test_navigation_manager.h"
#import "ios/web/public/test/fakes/test_web_state.h"
#include "testing/platform_test.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#include "third_party/ocmock/gtest_support.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

class TabGridMediatorTest : public PlatformTest {
 public:
  TabGridMediatorTest() {
    SetUpEmptyWebStateList();
    mediator_ = [[TabGridMediator alloc] init];
    mediator_.webStateList = web_state_list_.get();
  }
  ~TabGridMediatorTest() override { [mediator_ disconnect]; }

 protected:
  void SetUpEmptyWebStateList() {
    web_state_list_ = base::MakeUnique<WebStateList>(&web_state_list_delegate_);
  }

  void InsertWebStateAt(int index) {
    auto web_state = base::MakeUnique<web::TestWebState>();
    web_state_list_->InsertWebState(index, std::move(web_state));
  }

  void SetConsumer() {
    consumer_ = OCMProtocolMock(@protocol(TabGridConsumer));
    mediator_.consumer = consumer_;
  }

  TabGridMediator* mediator_;
  std::unique_ptr<WebStateList> web_state_list_;
  FakeWebStateListDelegate web_state_list_delegate_;
  id consumer_;
};

// Tests that the noTabsOverlay is removed when a web state is inserted when
// the list is empty.
TEST_F(TabGridMediatorTest, TestRemoveNoTabsOverlay) {
  SetConsumer();
  InsertWebStateAt(0);
  [[consumer_ verify] removeNoTabsOverlay];
}

// Tests that the noTabsOverlay is added when the web state list becomes empty.
TEST_F(TabGridMediatorTest, TestAddNoTabsOverlay) {
  SetConsumer();
  InsertWebStateAt(0);
  web_state_list_->CloseWebStateAt(0);
  [[consumer_ verify] addNoTabsOverlay];
}

}  // namespace
