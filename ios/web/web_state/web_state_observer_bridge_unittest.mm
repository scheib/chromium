// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/public/web_state/web_state_observer_bridge.h"

#import "base/mac/scoped_nsobject.h"
#include "base/memory/ptr_util.h"
#include "ios/web/public/favicon_url.h"
#import "ios/web/public/test/fakes/crw_test_web_state_observer.h"
#import "ios/web/public/test/fakes/test_web_state.h"
#import "ios/web/public/web_state/web_state_observer_bridge.h"
#import "ios/web/web_state/navigation_context_impl.h"
#include "net/http/http_response_headers.h"
#include "testing/platform_test.h"

namespace web {
namespace {
const char kRawResponseHeaders[] =
    "HTTP/1.1 200 OK\0"
    "Content-Length: 450\0"
    "Connection: keep-alive\0";
}  // namespace

// Test fixture to test WebStateObserverBridge class.
class WebStateObserverBridgeTest : public PlatformTest {
 protected:
  WebStateObserverBridgeTest()
      : observer_([[CRWTestWebStateObserver alloc] init]),
        bridge_(base::MakeUnique<WebStateObserverBridge>(&test_web_state_,
                                                         observer_.get())),
        response_headers_(new net::HttpResponseHeaders(
            std::string(kRawResponseHeaders, sizeof(kRawResponseHeaders)))) {}

  web::TestWebState test_web_state_;
  base::scoped_nsobject<CRWTestWebStateObserver> observer_;
  std::unique_ptr<WebStateObserverBridge> bridge_;
  scoped_refptr<net::HttpResponseHeaders> response_headers_;
};

// Tests |webState:didStartNavigation:| forwarding.
TEST_F(WebStateObserverBridgeTest, DidStartNavigation) {
  ASSERT_FALSE([observer_ didStartNavigationInfo]);

  GURL url("https://chromium.test/");
  std::unique_ptr<web::NavigationContext> context =
      web::NavigationContextImpl::CreateNavigationContext(&test_web_state_,
                                                          url);
  bridge_->DidStartNavigation(context.get());

  ASSERT_TRUE([observer_ didStartNavigationInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ didStartNavigationInfo]->web_state);
  web::NavigationContext* actual_context =
      [observer_ didStartNavigationInfo]->context.get();
  ASSERT_TRUE(actual_context);
  EXPECT_EQ(&test_web_state_, actual_context->GetWebState());
  EXPECT_EQ(context->IsSameDocument(), actual_context->IsSameDocument());
  EXPECT_EQ(context->GetError(), actual_context->GetError());
  EXPECT_EQ(context->GetUrl(), actual_context->GetUrl());
  EXPECT_EQ(context->GetResponseHeaders(),
            actual_context->GetResponseHeaders());
}

// Tests |webState:didFinishNavigation:| forwarding.
TEST_F(WebStateObserverBridgeTest, DidFinishNavigation) {
  ASSERT_FALSE([observer_ didFinishNavigationInfo]);

  GURL url("https://chromium.test/");
  std::unique_ptr<web::NavigationContext> context =
      web::NavigationContextImpl::CreateNavigationContext(&test_web_state_,
                                                          url);
  bridge_->DidFinishNavigation(context.get());

  ASSERT_TRUE([observer_ didFinishNavigationInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ didFinishNavigationInfo]->web_state);
  web::NavigationContext* actual_context =
      [observer_ didFinishNavigationInfo]->context.get();
  ASSERT_TRUE(actual_context);
  EXPECT_EQ(&test_web_state_, actual_context->GetWebState());
  EXPECT_EQ(context->IsSameDocument(), actual_context->IsSameDocument());
  EXPECT_EQ(context->GetError(), actual_context->GetError());
  EXPECT_EQ(context->GetUrl(), actual_context->GetUrl());
  EXPECT_EQ(context->GetResponseHeaders(),
            actual_context->GetResponseHeaders());
}

// Tests |webState:didCommitNavigationWithDetails:| forwarding.
TEST_F(WebStateObserverBridgeTest, NavigationItemCommitted) {
  ASSERT_FALSE([observer_ commitNavigationInfo]);

  LoadCommittedDetails load_details;
  load_details.item = reinterpret_cast<web::NavigationItem*>(1);
  load_details.previous_item_index = 15;
  load_details.previous_url = GURL("https://chromium.test/");
  load_details.is_in_page = true;

  bridge_->NavigationItemCommitted(load_details);

  ASSERT_TRUE([observer_ commitNavigationInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ commitNavigationInfo]->web_state);
  EXPECT_EQ(load_details.item,
            [observer_ commitNavigationInfo]->load_details.item);
  EXPECT_EQ(load_details.previous_item_index,
            [observer_ commitNavigationInfo]->load_details.previous_item_index);
  EXPECT_EQ(load_details.previous_url,
            [observer_ commitNavigationInfo]->load_details.previous_url);
  EXPECT_EQ(load_details.is_in_page,
            [observer_ commitNavigationInfo]->load_details.is_in_page);
}

// Tests |webState:didLoadPageWithSuccess:| forwarding.
TEST_F(WebStateObserverBridgeTest, PageLoaded) {
  ASSERT_FALSE([observer_ loadPageInfo]);

  // Forwarding PageLoadCompletionStatus::SUCCESS.
  bridge_->PageLoaded(PageLoadCompletionStatus::SUCCESS);
  ASSERT_TRUE([observer_ loadPageInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ loadPageInfo]->web_state);
  EXPECT_TRUE([observer_ loadPageInfo]->success);

  // Forwarding PageLoadCompletionStatus::FAILURE.
  bridge_->PageLoaded(PageLoadCompletionStatus::FAILURE);
  ASSERT_TRUE([observer_ loadPageInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ loadPageInfo]->web_state);
  EXPECT_FALSE([observer_ loadPageInfo]->success);
}

// Tests |webState:webStateDidDismissInterstitial:| forwarding.
TEST_F(WebStateObserverBridgeTest, InterstitialDismissed) {
  ASSERT_FALSE([observer_ dismissInterstitialInfo]);

  bridge_->InterstitialDismissed();
  ASSERT_TRUE([observer_ dismissInterstitialInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ dismissInterstitialInfo]->web_state);
}

// Tests |webState:didChangeLoadingProgress:| forwarding.
TEST_F(WebStateObserverBridgeTest, LoadProgressChanged) {
  ASSERT_FALSE([observer_ changeLoadingProgressInfo]);

  const double kTestLoadProgress = 0.75;
  bridge_->LoadProgressChanged(kTestLoadProgress);
  ASSERT_TRUE([observer_ changeLoadingProgressInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ changeLoadingProgressInfo]->web_state);
  EXPECT_EQ(kTestLoadProgress, [observer_ changeLoadingProgressInfo]->progress);
}

// Tests |webStateDidChangeTitle:| forwarding.
TEST_F(WebStateObserverBridgeTest, TitleWasSet) {
  ASSERT_FALSE([observer_ titleWasSetInfo]);

  bridge_->TitleWasSet();
  ASSERT_TRUE([observer_ titleWasSetInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ titleWasSetInfo]->web_state);
}

// Tests |webStateDidChangeVisibleSecurityState:| forwarding.
TEST_F(WebStateObserverBridgeTest, DidChangeVisibleSecurityState) {
  ASSERT_FALSE([observer_ didChangeVisibleSecurityStateInfo]);

  bridge_->DidChangeVisibleSecurityState();
  ASSERT_TRUE([observer_ didChangeVisibleSecurityStateInfo]);
  EXPECT_EQ(&test_web_state_,
            [observer_ didChangeVisibleSecurityStateInfo]->web_state);
}

// Tests |webStateDidSuppressDialog:| forwarding.
TEST_F(WebStateObserverBridgeTest, DidSuppressDialog) {
  ASSERT_FALSE([observer_ didSuppressDialogInfo]);

  bridge_->DidSuppressDialog();
  ASSERT_TRUE([observer_ didSuppressDialogInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ didSuppressDialogInfo]->web_state);
}

// Tests |webState:didSubmitDocumentWithFormNamed:userInitiated:| forwarding.
TEST_F(WebStateObserverBridgeTest, DocumentSubmitted) {
  ASSERT_FALSE([observer_ submitDocumentInfo]);

  std::string kTestFormName("form-name");
  BOOL user_initiated = YES;
  bridge_->DocumentSubmitted(kTestFormName, user_initiated);
  ASSERT_TRUE([observer_ submitDocumentInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ submitDocumentInfo]->web_state);
  EXPECT_EQ(kTestFormName, [observer_ submitDocumentInfo]->form_name);
  EXPECT_EQ(user_initiated, [observer_ submitDocumentInfo]->user_initiated);
}

// Tests |webState:didRegisterFormActivityWithFormNamed:...| forwarding.
TEST_F(WebStateObserverBridgeTest, FormActivityRegistered) {
  ASSERT_FALSE([observer_ formActivityInfo]);

  std::string kTestFormName("form-name");
  std::string kTestFieldName("field-name");
  std::string kTestTypeType("type");
  std::string kTestValue("value");
  bridge_->FormActivityRegistered(kTestFormName, kTestFieldName, kTestTypeType,
                                  kTestValue, true);
  ASSERT_TRUE([observer_ formActivityInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ formActivityInfo]->web_state);
  EXPECT_EQ(kTestFormName, [observer_ formActivityInfo]->form_name);
  EXPECT_EQ(kTestFieldName, [observer_ formActivityInfo]->field_name);
  EXPECT_EQ(kTestTypeType, [observer_ formActivityInfo]->type);
  EXPECT_EQ(kTestValue, [observer_ formActivityInfo]->value);
  EXPECT_TRUE([observer_ formActivityInfo]->input_missing);
}

// Tests |webState:didUpdateFaviconURLCandidates:| forwarding.
TEST_F(WebStateObserverBridgeTest, FaviconUrlUpdated) {
  ASSERT_FALSE([observer_ updateFaviconUrlCandidatesInfo]);

  web::FaviconURL url(GURL("https://chromium.test/"),
                      web::FaviconURL::TOUCH_ICON, {gfx::Size(5, 6)});

  bridge_->FaviconUrlUpdated({url});
  ASSERT_TRUE([observer_ updateFaviconUrlCandidatesInfo]);
  EXPECT_EQ(&test_web_state_,
            [observer_ updateFaviconUrlCandidatesInfo]->web_state);
  ASSERT_EQ(1U, [observer_ updateFaviconUrlCandidatesInfo]->candidates.size());
  const web::FaviconURL& actual_url =
      [observer_ updateFaviconUrlCandidatesInfo]->candidates[0];
  EXPECT_EQ(url.icon_url, actual_url.icon_url);
  EXPECT_EQ(url.icon_type, actual_url.icon_type);
  ASSERT_EQ(url.icon_sizes.size(), actual_url.icon_sizes.size());
  EXPECT_EQ(url.icon_sizes[0].width(), actual_url.icon_sizes[0].width());
  EXPECT_EQ(url.icon_sizes[0].height(), actual_url.icon_sizes[0].height());
}

// Tests |webState:renderProcessGoneForWebState:| forwarding.
TEST_F(WebStateObserverBridgeTest, RenderProcessGone) {
  ASSERT_FALSE([observer_ renderProcessGoneInfo]);

  bridge_->RenderProcessGone();
  ASSERT_TRUE([observer_ renderProcessGoneInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ renderProcessGoneInfo]->web_state);
}

// Tests |webState:webStateDestroyed:| forwarding.
TEST_F(WebStateObserverBridgeTest, WebStateDestroyed) {
  ASSERT_FALSE([observer_ webStateDestroyedInfo]);

  bridge_->WebStateDestroyed();
  ASSERT_TRUE([observer_ webStateDestroyedInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ webStateDestroyedInfo]->web_state);
}

// Tests |webState:webStateDidStopLoading:| forwarding.
TEST_F(WebStateObserverBridgeTest, DidStopLoading) {
  ASSERT_FALSE([observer_ stopLoadingInfo]);

  bridge_->DidStopLoading();
  ASSERT_TRUE([observer_ stopLoadingInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ stopLoadingInfo]->web_state);
}

// Tests |webState:webStateDidStartLoading:| forwarding.
TEST_F(WebStateObserverBridgeTest, DidStartLoading) {
  ASSERT_FALSE([observer_ startLoadingInfo]);

  bridge_->DidStartLoading();
  ASSERT_TRUE([observer_ startLoadingInfo]);
  EXPECT_EQ(&test_web_state_, [observer_ startLoadingInfo]->web_state);
}

}  // namespace web
