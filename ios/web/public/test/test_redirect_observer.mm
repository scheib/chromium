// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/web/public/test/test_redirect_observer.h"

#include "base/memory/ptr_util.h"
#include "base/supports_user_data.h"
#import "ios/web/public/navigation_item.h"
#import "ios/web/public/navigation_manager.h"
#include "ios/web/public/web_state/navigation_context.h"
#import "ios/web/public/web_state/web_state.h"

namespace {
// The key under which TestRedirectObservers are stored in a WebState's user
// data.
const void* const kTestRedirectObserverKey = &kTestRedirectObserverKey;
}  // namespace

namespace web {

#pragma mark - TestRedirectObserverUserDataWrapper

// Wrapper class used to associated TestRedirectObservers with their WebStates.
class TestRedirectObserverUserDataWrapper
    : public base::SupportsUserData::Data {
 public:
  static TestRedirectObserverUserDataWrapper* FromWebState(
      web::WebState* web_state) {
    DCHECK(web_state);
    TestRedirectObserverUserDataWrapper* wrapper =
        static_cast<TestRedirectObserverUserDataWrapper*>(
            web_state->GetUserData(kTestRedirectObserverKey));
    if (!wrapper)
      wrapper = new TestRedirectObserverUserDataWrapper(web_state);
    return wrapper;
  }

  explicit TestRedirectObserverUserDataWrapper(web::WebState* web_state)
      : redirect_observer_(web_state) {
    DCHECK(web_state);
    web_state->SetUserData(kTestRedirectObserverKey, base::WrapUnique(this));
  }

  web::TestRedirectObserver* redirect_observer() { return &redirect_observer_; }

 private:
  web::TestRedirectObserver redirect_observer_;
};

#pragma mark - TestRedirectObserver

TestRedirectObserver::TestRedirectObserver(WebState* web_state)
    : WebStateObserver(web_state) {}

TestRedirectObserver::~TestRedirectObserver() {}

// static
TestRedirectObserver* TestRedirectObserver::FromWebState(
    web::WebState* web_state) {
  return TestRedirectObserverUserDataWrapper::FromWebState(web_state)
      ->redirect_observer();
}

void TestRedirectObserver::BeginObservingRedirectsForUrl(const GURL& url) {
  expected_urls_.insert(url);
}

GURL TestRedirectObserver::GetFinalUrlForUrl(const GURL& url) {
  for (auto redirect_chain_for_item : redirect_chains_) {
    RedirectChain redirect_chain = redirect_chain_for_item.second;
    if (redirect_chain.original_url == url)
      return redirect_chain.final_url;
  }
  // If load for |url| did not occur after BeginObservingRedirectsForUrl() is
  // called, there will be no final redirected URL.
  return GURL();
}

void TestRedirectObserver::DidStartNavigation(NavigationContext* context) {
  GURL url = context->GetUrl();
  NavigationItem* item = web_state()->GetNavigationManager()->GetVisibleItem();
  DCHECK(item);
  if (redirect_chains_.find(item) != redirect_chains_.end()) {
    // If the redirect chain for the pending NavigationItem is already being
    // tracked, add the new URL to the end of the chain.
    redirect_chains_[item].final_url = url;
  } else if (expected_urls_.find(url) != expected_urls_.end()) {
    // If a load has begun for an expected URL, begin observing the redirect
    // chain for that NavigationItem.
    expected_urls_.erase(url);
    RedirectChain redirect_chain;
    redirect_chain.original_url = url;
    redirect_chain.final_url = url;
    redirect_chains_[item] = redirect_chain;
  }
}

}  // namespace web
