// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/web_state/web_state_impl.h"

#include <stddef.h>

#include <memory>

#include "base/base64.h"
#include "base/bind.h"
#include "base/logging.h"
#import "base/mac/bind_objc_block.h"
#include "base/memory/ptr_util.h"
#import "base/test/ios/wait_util.h"
#import "ios/web/public/java_script_dialog_presenter.h"
#include "ios/web/public/load_committed_details.h"
#include "ios/web/public/test/fakes/test_browser_state.h"
#import "ios/web/public/test/fakes/test_web_state_delegate.h"
#import "ios/web/public/test/fakes/test_web_state_observer.h"
#include "ios/web/public/test/web_test.h"
#import "ios/web/public/web_state/context_menu_params.h"
#include "ios/web/public/web_state/global_web_state_observer.h"
#import "ios/web/public/web_state/web_state_delegate.h"
#include "ios/web/public/web_state/web_state_observer.h"
#import "ios/web/public/web_state/web_state_policy_decider.h"
#include "ios/web/web_state/global_web_state_event_tracker.h"
#import "ios/web/web_state/navigation_context_impl.h"
#import "ios/web/web_state/ui/crw_web_controller.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"
#include "url/gurl.h"

using testing::_;
using testing::Assign;
using testing::AtMost;
using testing::DoAll;
using testing::Return;

namespace web {
namespace {

// Test observer to check that the GlobalWebStateObserver methods are called as
// expected.
class TestGlobalWebStateObserver : public GlobalWebStateObserver {
 public:
  TestGlobalWebStateObserver()
      : GlobalWebStateObserver(),
        navigation_items_pruned_called_(false),
        navigation_item_changed_called_(false),
        navigation_item_committed_called_(false),
        did_start_loading_called_(false),
        did_stop_loading_called_(false),
        page_loaded_called_with_success_(false),
        web_state_destroyed_called_(false) {}

  // Methods returning true if the corresponding GlobalWebStateObserver method
  // has been called.
  bool navigation_items_pruned_called() const {
    return navigation_items_pruned_called_;
  }
  bool navigation_item_changed_called() const {
    return navigation_item_changed_called_;
  }
  bool navigation_item_committed_called() const {
    return navigation_item_committed_called_;
  }
  bool did_start_loading_called() const { return did_start_loading_called_; }
  bool did_stop_loading_called() const { return did_stop_loading_called_; }
  bool page_loaded_called_with_success() const {
    return page_loaded_called_with_success_;
  }
  bool web_state_destroyed_called() const {
    return web_state_destroyed_called_;
  }

 private:
  // GlobalWebStateObserver implementation:
  void NavigationItemsPruned(WebState* web_state,
                             size_t pruned_item_count) override {
    navigation_items_pruned_called_ = true;
  }
  void NavigationItemChanged(WebState* web_state) override {
    navigation_item_changed_called_ = true;
  }
  void NavigationItemCommitted(
      WebState* web_state,
      const LoadCommittedDetails& load_details) override {
    navigation_item_committed_called_ = true;
  }
  void WebStateDidStartLoading(WebState* web_state) override {
    did_start_loading_called_ = true;
  }
  void WebStateDidStopLoading(WebState* web_state) override {
    did_stop_loading_called_ = true;
  }
  void PageLoaded(WebState* web_state,
                  PageLoadCompletionStatus load_completion_status) override {
    page_loaded_called_with_success_ =
        load_completion_status == PageLoadCompletionStatus::SUCCESS;
  }
  void WebStateDestroyed(WebState* web_state) override {
    web_state_destroyed_called_ = true;
  }

  bool navigation_items_pruned_called_;
  bool navigation_item_changed_called_;
  bool navigation_item_committed_called_;
  bool did_start_loading_called_;
  bool did_stop_loading_called_;
  bool page_loaded_called_with_success_;
  bool web_state_destroyed_called_;
};

// Test decider to check that the WebStatePolicyDecider methods are called as
// expected.
class MockWebStatePolicyDecider : public WebStatePolicyDecider {
 public:
  explicit MockWebStatePolicyDecider(WebState* web_state)
      : WebStatePolicyDecider(web_state) {}
  virtual ~MockWebStatePolicyDecider() {}

  MOCK_METHOD1(ShouldAllowRequest, bool(NSURLRequest* request));
  MOCK_METHOD1(ShouldAllowResponse, bool(NSURLResponse* response));
  MOCK_METHOD0(WebStateDestroyed, void());
};

// Creates and returns an HttpResponseHeader using the string representation.
scoped_refptr<net::HttpResponseHeaders> HeadersFromString(const char* string) {
  std::string raw_string(string);
  std::string headers_string = net::HttpUtil::AssembleRawHeaders(
      raw_string.c_str(), raw_string.length());
  scoped_refptr<net::HttpResponseHeaders> headers(
      new net::HttpResponseHeaders(headers_string));
  return headers;
}

// Test callback for script commands.
// Sets |is_called| to true if it is called, and checks that the parameters
// match their expected values.
// |user_is_interacting| is not checked because Bind() has a maximum of 7
// parameters.
bool HandleScriptCommand(bool* is_called,
                         bool should_handle,
                         base::DictionaryValue* expected_value,
                         const GURL& expected_url,
                         const base::DictionaryValue& value,
                         const GURL& url,
                         bool user_is_interacting) {
  *is_called = true;
  EXPECT_TRUE(expected_value->Equals(&value));
  EXPECT_EQ(expected_url, url);
  return should_handle;
}

}  // namespace

// Test fixture for web::WebStateImpl class.
class WebStateImplTest : public web::WebTest {
 protected:
  WebStateImplTest() : web::WebTest() {
    web::WebState::CreateParams params(GetBrowserState());
    web_state_ = base::MakeUnique<web::WebStateImpl>(params);
  }

  std::unique_ptr<WebStateImpl> web_state_;
};

TEST_F(WebStateImplTest, WebUsageEnabled) {
  // Default is false.
  ASSERT_FALSE(web_state_->IsWebUsageEnabled());

  web_state_->SetWebUsageEnabled(true);
  EXPECT_TRUE(web_state_->IsWebUsageEnabled());
  EXPECT_TRUE(web_state_->GetWebController().webUsageEnabled);

  web_state_->SetWebUsageEnabled(false);
  EXPECT_FALSE(web_state_->IsWebUsageEnabled());
  EXPECT_FALSE(web_state_->GetWebController().webUsageEnabled);
}

TEST_F(WebStateImplTest, ShouldSuppressDialogs) {
  // Default is false.
  ASSERT_FALSE(web_state_->ShouldSuppressDialogs());

  web_state_->SetShouldSuppressDialogs(true);
  EXPECT_TRUE(web_state_->ShouldSuppressDialogs());
  EXPECT_TRUE(web_state_->GetWebController().shouldSuppressDialogs);

  web_state_->SetShouldSuppressDialogs(false);
  EXPECT_FALSE(web_state_->ShouldSuppressDialogs());
  EXPECT_FALSE(web_state_->GetWebController().shouldSuppressDialogs);
}

TEST_F(WebStateImplTest, ResponseHeaders) {
  GURL real_url("http://foo.com/bar");
  GURL frame_url("http://frames-r-us.com/");
  scoped_refptr<net::HttpResponseHeaders> real_headers(HeadersFromString(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Content-Language: en\r\n"
      "X-Should-Be-Here: yep\r\n"
      "\r\n"));
  scoped_refptr<net::HttpResponseHeaders> frame_headers(HeadersFromString(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/pdf\r\n"
      "Content-Language: fr\r\n"
      "X-Should-Not-Be-Here: oops\r\n"
      "\r\n"));
  // Simulate a load of a page with a frame.
  web_state_->OnHttpResponseHeadersReceived(real_headers.get(), real_url);
  web_state_->OnHttpResponseHeadersReceived(frame_headers.get(), frame_url);
  // Include a hash to be sure it's handled correctly.
  web_state_->UpdateHttpResponseHeaders(
      GURL(real_url.spec() + std::string("#baz")));

  // Verify that the right header set was kept.
  ASSERT_TRUE(web_state_->GetHttpResponseHeaders());
  EXPECT_TRUE(
      web_state_->GetHttpResponseHeaders()->HasHeader("X-Should-Be-Here"));
  EXPECT_FALSE(
      web_state_->GetHttpResponseHeaders()->HasHeader("X-Should-Not-Be-Here"));

  // And that it was parsed correctly.
  EXPECT_EQ("text/html", web_state_->GetContentsMimeType());
  EXPECT_EQ("en", web_state_->GetContentLanguageHeader());
}

TEST_F(WebStateImplTest, ResponseHeaderClearing) {
  GURL url("http://foo.com/");
  scoped_refptr<net::HttpResponseHeaders> headers(HeadersFromString(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Content-Language: en\r\n"
      "\r\n"));
  web_state_->OnHttpResponseHeadersReceived(headers.get(), url);

  // There should be no headers before loading.
  EXPECT_EQ(NULL, web_state_->GetHttpResponseHeaders());

  // There should be headers and parsed values after loading.
  web_state_->UpdateHttpResponseHeaders(url);
  ASSERT_TRUE(web_state_->GetHttpResponseHeaders());
  EXPECT_TRUE(web_state_->GetHttpResponseHeaders()->HasHeader("Content-Type"));
  EXPECT_NE("", web_state_->GetContentsMimeType());
  EXPECT_NE("", web_state_->GetContentLanguageHeader());

  // ... but not after loading another page, nor should there be specific
  // parsed values.
  web_state_->UpdateHttpResponseHeaders(GURL("http://elsewhere.com/"));
  EXPECT_EQ(NULL, web_state_->GetHttpResponseHeaders());
  EXPECT_EQ("", web_state_->GetContentsMimeType());
  EXPECT_EQ("", web_state_->GetContentLanguageHeader());
}

// Tests forwarding to WebStateObserver callbacks.
TEST_F(WebStateImplTest, ObserverTest) {
  std::unique_ptr<TestWebStateObserver> observer(
      new TestWebStateObserver(web_state_.get()));
  EXPECT_EQ(web_state_.get(), observer->web_state());

  // Test that LoadProgressChanged() is called.
  ASSERT_FALSE(observer->change_loading_progress_info());
  const double kTestLoadProgress = 0.75;
  web_state_->SendChangeLoadProgress(kTestLoadProgress);
  ASSERT_TRUE(observer->change_loading_progress_info());
  EXPECT_EQ(web_state_.get(),
            observer->change_loading_progress_info()->web_state);
  EXPECT_EQ(kTestLoadProgress,
            observer->change_loading_progress_info()->progress);

  // Test that TitleWasSet() is called.
  ASSERT_FALSE(observer->title_was_set_info());
  web_state_->OnTitleChanged();
  ASSERT_TRUE(observer->title_was_set_info());
  EXPECT_EQ(web_state_.get(), observer->title_was_set_info()->web_state);

  // Test that DidChangeVisibleSecurityState() is called.
  ASSERT_FALSE(observer->did_change_visible_security_state_info());
  web_state_->OnVisibleSecurityStateChange();
  ASSERT_TRUE(observer->did_change_visible_security_state_info());
  EXPECT_EQ(web_state_.get(),
            observer->did_change_visible_security_state_info()->web_state);

  // Test that DidSuppressDialog() is called.
  ASSERT_FALSE(observer->did_suppress_dialog_info());
  web_state_->SetShouldSuppressDialogs(true);
  web_state_->OnDialogSuppressed();
  ASSERT_TRUE(observer->did_suppress_dialog_info());
  EXPECT_EQ(web_state_.get(), observer->did_suppress_dialog_info()->web_state);

  // Test that DocumentSubmitted() is called.
  ASSERT_FALSE(observer->submit_document_info());
  std::string kTestFormName("form-name");
  BOOL user_initiated = true;
  web_state_->OnDocumentSubmitted(kTestFormName, user_initiated);
  ASSERT_TRUE(observer->submit_document_info());
  EXPECT_EQ(web_state_.get(), observer->submit_document_info()->web_state);
  EXPECT_EQ(kTestFormName, observer->submit_document_info()->form_name);
  EXPECT_EQ(user_initiated, observer->submit_document_info()->user_initiated);

  // Test that FormActivityRegistered() is called.
  ASSERT_FALSE(observer->form_activity_info());
  std::string kTestFieldName("field-name");
  std::string kTestTypeType("type");
  std::string kTestValue("value");
  web_state_->OnFormActivityRegistered(kTestFormName, kTestFieldName,
                                       kTestTypeType, kTestValue, true);
  ASSERT_TRUE(observer->form_activity_info());
  EXPECT_EQ(web_state_.get(), observer->form_activity_info()->web_state);
  EXPECT_EQ(kTestFormName, observer->form_activity_info()->form_name);
  EXPECT_EQ(kTestFieldName, observer->form_activity_info()->field_name);
  EXPECT_EQ(kTestTypeType, observer->form_activity_info()->type);
  EXPECT_EQ(kTestValue, observer->form_activity_info()->value);
  EXPECT_TRUE(observer->form_activity_info()->input_missing);

  // Test that FaviconUrlUpdated() is called.
  ASSERT_FALSE(observer->update_favicon_url_candidates_info());
  web::FaviconURL favicon_url(GURL("https://chromium.test/"),
                              web::FaviconURL::TOUCH_ICON, {gfx::Size(5, 6)});
  web_state_->OnFaviconUrlUpdated({favicon_url});
  ASSERT_TRUE(observer->update_favicon_url_candidates_info());
  EXPECT_EQ(web_state_.get(),
            observer->update_favicon_url_candidates_info()->web_state);
  ASSERT_EQ(1U,
            observer->update_favicon_url_candidates_info()->candidates.size());
  const web::FaviconURL& actual_favicon_url =
      observer->update_favicon_url_candidates_info()->candidates[0];
  EXPECT_EQ(favicon_url.icon_url, actual_favicon_url.icon_url);
  EXPECT_EQ(favicon_url.icon_type, actual_favicon_url.icon_type);
  ASSERT_EQ(favicon_url.icon_sizes.size(),
            actual_favicon_url.icon_sizes.size());
  EXPECT_EQ(favicon_url.icon_sizes[0].width(),
            actual_favicon_url.icon_sizes[0].width());
  EXPECT_EQ(favicon_url.icon_sizes[0].height(),
            actual_favicon_url.icon_sizes[0].height());

  // Test that RenderProcessGone() is called.
  SetIgnoreRenderProcessCrashesDuringTesting(true);
  ASSERT_FALSE(observer->render_process_gone_info());
  web_state_->OnRenderProcessGone();
  ASSERT_TRUE(observer->render_process_gone_info());
  EXPECT_EQ(web_state_.get(), observer->render_process_gone_info()->web_state);

  // Test that DidFinishNavigation() is called.
  ASSERT_FALSE(observer->did_finish_navigation_info());
  const GURL url("http://test");
  std::unique_ptr<web::NavigationContext> context =
      NavigationContextImpl::CreateNavigationContext(web_state_.get(), url);
  web_state_->OnNavigationFinished(context.get());
  ASSERT_TRUE(observer->did_finish_navigation_info());
  EXPECT_EQ(web_state_.get(),
            observer->did_finish_navigation_info()->web_state);
  NavigationContext* actual_context =
      observer->did_finish_navigation_info()->context.get();
  EXPECT_EQ(context->GetUrl(), actual_context->GetUrl());
  EXPECT_FALSE(actual_context->IsSameDocument());
  EXPECT_FALSE(actual_context->GetError());
  EXPECT_FALSE(actual_context->GetResponseHeaders());

  // Test that DidStartNavigation() is called.
  ASSERT_FALSE(observer->did_start_navigation_info());
  web_state_->OnNavigationStarted(context.get());
  ASSERT_TRUE(observer->did_start_navigation_info());
  EXPECT_EQ(web_state_.get(), observer->did_start_navigation_info()->web_state);
  actual_context = observer->did_start_navigation_info()->context.get();
  EXPECT_EQ(context->GetUrl(), actual_context->GetUrl());
  EXPECT_FALSE(actual_context->IsSameDocument());
  EXPECT_FALSE(actual_context->GetError());
  EXPECT_FALSE(actual_context->GetResponseHeaders());

  // Test that NavigationItemsPruned() is called.
  ASSERT_FALSE(observer->navigation_items_pruned_info());
  web_state_->OnNavigationItemsPruned(1);
  ASSERT_TRUE(observer->navigation_items_pruned_info());
  EXPECT_EQ(web_state_.get(),
            observer->navigation_items_pruned_info()->web_state);

  // Test that NavigationItemChanged() is called.
  ASSERT_FALSE(observer->navigation_item_changed_info());
  web_state_->OnNavigationItemChanged();
  ASSERT_TRUE(observer->navigation_item_changed_info());
  EXPECT_EQ(web_state_.get(),
            observer->navigation_item_changed_info()->web_state);

  // Test that NavigationItemCommitted() is called.
  ASSERT_FALSE(observer->commit_navigation_info());
  LoadCommittedDetails details;
  web_state_->OnNavigationItemCommitted(details);
  ASSERT_TRUE(observer->commit_navigation_info());
  EXPECT_EQ(web_state_.get(), observer->commit_navigation_info()->web_state);
  LoadCommittedDetails actual_details =
      observer->commit_navigation_info()->load_details;
  EXPECT_EQ(details.item, actual_details.item);
  EXPECT_EQ(details.previous_item_index, actual_details.previous_item_index);
  EXPECT_EQ(details.previous_url, actual_details.previous_url);
  EXPECT_EQ(details.is_in_page, actual_details.is_in_page);

  // Test that OnPageLoaded() is called with success when there is no error.
  ASSERT_FALSE(observer->load_page_info());
  web_state_->OnPageLoaded(url, false);
  ASSERT_TRUE(observer->load_page_info());
  EXPECT_EQ(web_state_.get(), observer->load_page_info()->web_state);
  EXPECT_FALSE(observer->load_page_info()->success);
  web_state_->OnPageLoaded(url, true);
  ASSERT_TRUE(observer->load_page_info());
  EXPECT_EQ(web_state_.get(), observer->load_page_info()->web_state);
  EXPECT_TRUE(observer->load_page_info()->success);

  // Test that OnTitleChanged() is called.
  observer = base::MakeUnique<TestWebStateObserver>(web_state_.get());
  ASSERT_FALSE(observer->title_was_set_info());
  web_state_->OnTitleChanged();
  ASSERT_TRUE(observer->title_was_set_info());
  EXPECT_EQ(web_state_.get(), observer->title_was_set_info()->web_state);

  // Test that WebStateDestroyed() is called.
  EXPECT_FALSE(observer->web_state_destroyed_info());
  web_state_.reset();
  EXPECT_TRUE(observer->web_state_destroyed_info());

  EXPECT_EQ(nullptr, observer->web_state());
}

// Tests that WebStateDelegate methods appropriately called.
TEST_F(WebStateImplTest, DelegateTest) {
  TestWebStateDelegate delegate;
  web_state_->SetDelegate(&delegate);

  // Test that CreateNewWebState() is called.
  GURL child_url("https://child.test/");
  GURL opener_url("https://opener.test/");
  EXPECT_FALSE(delegate.last_create_new_web_state_request());
  web_state_->CreateNewWebState(child_url, opener_url, true);
  TestCreateNewWebStateRequest* create_new_web_state_request =
      delegate.last_create_new_web_state_request();
  ASSERT_TRUE(create_new_web_state_request);
  EXPECT_EQ(web_state_.get(), create_new_web_state_request->web_state);
  EXPECT_EQ(child_url, create_new_web_state_request->url);
  EXPECT_EQ(opener_url, create_new_web_state_request->opener_url);
  EXPECT_TRUE(create_new_web_state_request->initiated_by_user);

  // Test that CloseWebState() is called.
  EXPECT_FALSE(delegate.last_close_web_state_request());
  web_state_->CloseWebState();
  ASSERT_TRUE(delegate.last_close_web_state_request());
  EXPECT_EQ(web_state_.get(),
            delegate.last_close_web_state_request()->web_state);

  // Test that OpenURLFromWebState() is called.
  WebState::OpenURLParams params(GURL("https://chromium.test/"), Referrer(),
                                 WindowOpenDisposition::CURRENT_TAB,
                                 ui::PAGE_TRANSITION_LINK, true);
  EXPECT_FALSE(delegate.last_open_url_request());
  web_state_->OpenURL(params);
  TestOpenURLRequest* open_url_request = delegate.last_open_url_request();
  ASSERT_TRUE(open_url_request);
  EXPECT_EQ(web_state_.get(), open_url_request->web_state);
  WebState::OpenURLParams actual_params = open_url_request->params;
  EXPECT_EQ(params.url, actual_params.url);
  EXPECT_EQ(params.referrer.url, actual_params.referrer.url);
  EXPECT_EQ(params.referrer.policy, actual_params.referrer.policy);
  EXPECT_EQ(params.disposition, actual_params.disposition);
  EXPECT_TRUE(
      PageTransitionCoreTypeIs(params.transition, actual_params.transition));
  EXPECT_EQ(params.is_renderer_initiated, actual_params.is_renderer_initiated);

  // Test that HandleContextMenu() is called.
  EXPECT_FALSE(delegate.handle_context_menu_called());
  web::ContextMenuParams context_menu_params;
  web_state_->HandleContextMenu(context_menu_params);
  EXPECT_TRUE(delegate.handle_context_menu_called());

  // Test that ShowRepostFormWarningDialog() is called.
  EXPECT_FALSE(delegate.last_repost_form_request());
  base::Callback<void(bool)> repost_callback;
  web_state_->ShowRepostFormWarningDialog(repost_callback);
  ASSERT_TRUE(delegate.last_repost_form_request());
  EXPECT_EQ(delegate.last_repost_form_request()->web_state, web_state_.get());

  // Test that GetJavaScriptDialogPresenter() is called.
  TestJavaScriptDialogPresenter* presenter =
      delegate.GetTestJavaScriptDialogPresenter();
  EXPECT_FALSE(delegate.get_java_script_dialog_presenter_called());
  EXPECT_TRUE(presenter->requested_dialogs().empty());
  EXPECT_FALSE(presenter->cancel_dialogs_called());

  __block bool callback_called = false;
  web_state_->RunJavaScriptDialog(GURL(), JAVASCRIPT_DIALOG_TYPE_ALERT, @"",
                                  nil, base::BindBlock(^(bool, NSString*) {
                                    callback_called = true;
                                  }));

  EXPECT_TRUE(delegate.get_java_script_dialog_presenter_called());
  EXPECT_EQ(1U, presenter->requested_dialogs().size());
  EXPECT_TRUE(callback_called);

  EXPECT_FALSE(presenter->cancel_dialogs_called());
  web_state_->CancelDialogs();
  EXPECT_TRUE(presenter->cancel_dialogs_called());

  // Test that OnAuthRequired() is called.
  EXPECT_FALSE(delegate.last_authentication_request());
  base::scoped_nsobject<NSURLProtectionSpace> protection_space(
      [[NSURLProtectionSpace alloc] init]);
  base::scoped_nsobject<NSURLCredential> credential(
      [[NSURLCredential alloc] init]);
  WebStateDelegate::AuthCallback callback;
  web_state_->OnAuthRequired(protection_space.get(), credential.get(),
                             callback);
  ASSERT_TRUE(delegate.last_authentication_request());
  EXPECT_EQ(delegate.last_authentication_request()->web_state,
            web_state_.get());
  EXPECT_EQ(delegate.last_authentication_request()->protection_space,
            protection_space.get());
  EXPECT_EQ(delegate.last_authentication_request()->credential,
            credential.get());
}

// Verifies that GlobalWebStateObservers are called when expected.
TEST_F(WebStateImplTest, GlobalObserverTest) {
  std::unique_ptr<TestGlobalWebStateObserver> observer(
      new TestGlobalWebStateObserver());

  // Test that NavigationItemsPruned() is called.
  EXPECT_FALSE(observer->navigation_items_pruned_called());
  web_state_->OnNavigationItemsPruned(1);
  EXPECT_TRUE(observer->navigation_items_pruned_called());

  // Test that NavigationItemChanged() is called.
  EXPECT_FALSE(observer->navigation_item_changed_called());
  web_state_->OnNavigationItemChanged();
  EXPECT_TRUE(observer->navigation_item_changed_called());

  // Test that NavigationItemCommitted() is called.
  EXPECT_FALSE(observer->navigation_item_committed_called());
  LoadCommittedDetails details;
  web_state_->OnNavigationItemCommitted(details);
  EXPECT_TRUE(observer->navigation_item_committed_called());

  // Test that WebStateDidStartLoading() is called.
  EXPECT_FALSE(observer->did_start_loading_called());
  web_state_->SetIsLoading(true);
  EXPECT_TRUE(observer->did_start_loading_called());

  // Test that WebStateDidStopLoading() is called.
  EXPECT_FALSE(observer->did_stop_loading_called());
  web_state_->SetIsLoading(false);
  EXPECT_TRUE(observer->did_stop_loading_called());

  // Test that OnPageLoaded() is called with success when there is no error.
  EXPECT_FALSE(observer->page_loaded_called_with_success());
  web_state_->OnPageLoaded(GURL("http://test"), false);
  EXPECT_FALSE(observer->page_loaded_called_with_success());
  web_state_->OnPageLoaded(GURL("http://test"), true);
  EXPECT_TRUE(observer->page_loaded_called_with_success());

  // Test that WebStateDestroyed() is called.
  EXPECT_FALSE(observer->web_state_destroyed_called());
  web_state_.reset();
  EXPECT_TRUE(observer->web_state_destroyed_called());
}

// Verifies that policy deciders are correctly called by the web state.
TEST_F(WebStateImplTest, PolicyDeciderTest) {
  MockWebStatePolicyDecider decider(web_state_.get());
  MockWebStatePolicyDecider decider2(web_state_.get());
  EXPECT_EQ(web_state_.get(), decider.web_state());

  // Test that ShouldAllowRequest() is called.
  EXPECT_CALL(decider, ShouldAllowRequest(_)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(decider2, ShouldAllowRequest(_)).Times(1).WillOnce(Return(true));
  EXPECT_TRUE(web_state_->ShouldAllowRequest(nil));

  // Test that ShouldAllowRequest() is stopping on negative answer. Only one
  // one the decider should be called.
  {
    bool decider_called = false;
    bool decider2_called = false;
    EXPECT_CALL(decider, ShouldAllowRequest(_))
        .Times(AtMost(1))
        .WillOnce(DoAll(Assign(&decider_called, true), Return(false)));
    EXPECT_CALL(decider2, ShouldAllowRequest(_))
        .Times(AtMost(1))
        .WillOnce(DoAll(Assign(&decider2_called, true), Return(false)));
    EXPECT_FALSE(web_state_->ShouldAllowRequest(nil));
    EXPECT_FALSE(decider_called && decider2_called);
  }

  // Test that ShouldAllowResponse() is called.
  EXPECT_CALL(decider, ShouldAllowResponse(_)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(decider2, ShouldAllowResponse(_)).Times(1).WillOnce(Return(true));
  EXPECT_TRUE(web_state_->ShouldAllowResponse(nil));

  // Test that ShouldAllowResponse() is stopping on negative answer. Only one
  // one the decider should be called.
  {
    bool decider_called = false;
    bool decider2_called = false;
    EXPECT_CALL(decider, ShouldAllowResponse(_))
        .Times(AtMost(1))
        .WillOnce(DoAll(Assign(&decider_called, true), Return(false)));
    EXPECT_CALL(decider2, ShouldAllowResponse(_))
        .Times(AtMost(1))
        .WillOnce(DoAll(Assign(&decider2_called, true), Return(false)));
    EXPECT_FALSE(web_state_->ShouldAllowResponse(nil));
    EXPECT_FALSE(decider_called && decider2_called);
  }

  // Test that WebStateDestroyed() is called.
  EXPECT_CALL(decider, WebStateDestroyed()).Times(1);
  EXPECT_CALL(decider2, WebStateDestroyed()).Times(1);
  web_state_.reset();
  EXPECT_EQ(nullptr, decider.web_state());
}

// Tests that script command callbacks are called correctly.
TEST_F(WebStateImplTest, ScriptCommand) {
  // Set up two script command callbacks.
  const std::string kPrefix1("prefix1");
  const std::string kCommand1("prefix1.command1");
  base::DictionaryValue value_1;
  value_1.SetString("a", "b");
  const GURL kUrl1("http://foo");
  bool is_called_1 = false;
  web_state_->AddScriptCommandCallback(
      base::Bind(&HandleScriptCommand, &is_called_1, true, &value_1, kUrl1),
      kPrefix1);

  const std::string kPrefix2("prefix2");
  const std::string kCommand2("prefix2.command2");
  base::DictionaryValue value_2;
  value_2.SetString("c", "d");
  const GURL kUrl2("http://bar");
  bool is_called_2 = false;
  web_state_->AddScriptCommandCallback(
      base::Bind(&HandleScriptCommand, &is_called_2, false, &value_2, kUrl2),
      kPrefix2);

  // Check that a irrelevant or invalid command does not trigger the callbacks.
  EXPECT_FALSE(
      web_state_->OnScriptCommandReceived("wohoo.blah", value_1, kUrl1, false));
  EXPECT_FALSE(is_called_1);
  EXPECT_FALSE(is_called_2);

  EXPECT_FALSE(web_state_->OnScriptCommandReceived(
      "prefix1ButMissingDot", value_1, kUrl1, false));
  EXPECT_FALSE(is_called_1);
  EXPECT_FALSE(is_called_2);

  // Check that only the callback matching the prefix is called, with the
  // expected parameters and return value;
  EXPECT_TRUE(
      web_state_->OnScriptCommandReceived(kCommand1, value_1, kUrl1, false));
  EXPECT_TRUE(is_called_1);
  EXPECT_FALSE(is_called_2);

  // Remove the callback and check it is no longer called.
  is_called_1 = false;
  web_state_->RemoveScriptCommandCallback(kPrefix1);
  EXPECT_FALSE(
      web_state_->OnScriptCommandReceived(kCommand1, value_1, kUrl1, false));
  EXPECT_FALSE(is_called_1);
  EXPECT_FALSE(is_called_2);

  // Check that a false return value is forwarded correctly.
  EXPECT_FALSE(
      web_state_->OnScriptCommandReceived(kCommand2, value_2, kUrl2, false));
  EXPECT_FALSE(is_called_1);
  EXPECT_TRUE(is_called_2);

  web_state_->RemoveScriptCommandCallback(kPrefix2);
}

// Tests that WebState::CreateParams::created_with_opener is translated to
// WebState::HasOpener() return values.
TEST_F(WebStateImplTest, CreatedWithOpener) {
  // Verify that the HasOpener() returns false if not specified in the create
  // params.
  EXPECT_FALSE(web_state_->HasOpener());
  // Set |created_with_opener| to true and verify that HasOpener() returns true.
  WebState::CreateParams params_with_opener =
      WebState::CreateParams(GetBrowserState());
  params_with_opener.created_with_opener = true;
  std::unique_ptr<WebState> web_state_with_opener =
      WebState::Create(params_with_opener);
  EXPECT_TRUE(web_state_with_opener->HasOpener());
}

}  // namespace web
