// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/web/public/test/fakes/test_web_state_observer.h"

#include "base/memory/ptr_util.h"
#import "ios/web/public/web_state/navigation_context.h"
#include "ios/web/public/web_state/web_state.h"
#include "ios/web/web_state/navigation_context_impl.h"
#include "net/http/http_response_headers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace web {

TestWebStateObserver::TestWebStateObserver(WebState* web_state)
    : WebStateObserver(web_state) {}
TestWebStateObserver::~TestWebStateObserver() = default;

void TestWebStateObserver::NavigationItemCommitted(
    const LoadCommittedDetails& load_details) {
  commit_navigation_info_ = base::MakeUnique<web::TestCommitNavigationInfo>();
  commit_navigation_info_->web_state = web_state();
  commit_navigation_info_->load_details = load_details;
}

void TestWebStateObserver::PageLoaded(
    PageLoadCompletionStatus load_completion_status) {
  load_page_info_ = base::MakeUnique<web::TestLoadPageInfo>();
  load_page_info_->web_state = web_state();
  load_page_info_->success =
      load_completion_status == PageLoadCompletionStatus::SUCCESS;
}

void TestWebStateObserver::InterstitialDismissed() {
  dismiss_interstitial_info_ =
      base::MakeUnique<web::TestDismissInterstitialInfo>();
  dismiss_interstitial_info_->web_state = web_state();
}

void TestWebStateObserver::LoadProgressChanged(double progress) {
  change_loading_progress_info_ =
      base::MakeUnique<web::TestChangeLoadingProgressInfo>();
  change_loading_progress_info_->web_state = web_state();
  change_loading_progress_info_->progress = progress;
}

void TestWebStateObserver::NavigationItemsPruned(size_t pruned_item_count) {
  navigation_items_pruned_info_ =
      base::MakeUnique<web::TestNavigationItemsPrunedInfo>();
  navigation_items_pruned_info_->web_state = web_state();
  navigation_items_pruned_info_->count = pruned_item_count;
}

void TestWebStateObserver::NavigationItemChanged() {
  navigation_item_changed_info_ =
      base::MakeUnique<web::TestNavigationItemChangedInfo>();
  navigation_item_changed_info_->web_state = web_state();
}

void TestWebStateObserver::DidStartNavigation(NavigationContext* navigation) {
  ASSERT_TRUE(!navigation->GetError() || !navigation->IsSameDocument());
  did_start_navigation_info_ =
      base::MakeUnique<web::TestDidStartNavigationInfo>();
  did_start_navigation_info_->web_state = web_state();
  std::unique_ptr<web::NavigationContextImpl> context =
      web::NavigationContextImpl::CreateNavigationContext(
          navigation->GetWebState(), navigation->GetUrl());
  context->SetIsSameDocument(navigation->IsSameDocument());
  context->SetError(navigation->GetError());
  did_start_navigation_info_->context = std::move(context);
}

void TestWebStateObserver::DidFinishNavigation(NavigationContext* navigation) {
  ASSERT_TRUE(!navigation->GetError() || !navigation->IsSameDocument());
  did_finish_navigation_info_ =
      base::MakeUnique<web::TestDidFinishNavigationInfo>();
  did_finish_navigation_info_->web_state = web_state();
  std::unique_ptr<web::NavigationContextImpl> context =
      web::NavigationContextImpl::CreateNavigationContext(
          navigation->GetWebState(), navigation->GetUrl());
  context->SetIsSameDocument(navigation->IsSameDocument());
  context->SetError(navigation->GetError());
  did_finish_navigation_info_->context = std::move(context);
}

void TestWebStateObserver::TitleWasSet() {
  title_was_set_info_ = base::MakeUnique<web::TestTitleWasSetInfo>();
  title_was_set_info_->web_state = web_state();
}

void TestWebStateObserver::DidChangeVisibleSecurityState() {
  did_change_visible_security_state_info_ =
      base::MakeUnique<web::TestDidChangeVisibleSecurityStateInfo>();
  did_change_visible_security_state_info_->web_state = web_state();
}

void TestWebStateObserver::DidSuppressDialog() {
  did_suppress_dialog_info_ =
      base::MakeUnique<web::TestDidSuppressDialogInfo>();
  did_suppress_dialog_info_->web_state = web_state();
}

void TestWebStateObserver::DocumentSubmitted(const std::string& form_name,
                                             bool user_initiated) {
  submit_document_info_ = base::MakeUnique<web::TestSubmitDocumentInfo>();
  submit_document_info_->web_state = web_state();
  submit_document_info_->form_name = form_name;
  submit_document_info_->user_initiated = user_initiated;
}

void TestWebStateObserver::FormActivityRegistered(const std::string& form_name,
                                                  const std::string& field_name,
                                                  const std::string& type,
                                                  const std::string& value,
                                                  bool input_missing) {
  form_activity_info_ = base::MakeUnique<web::TestFormActivityInfo>();
  form_activity_info_->web_state = web_state();
  form_activity_info_->form_name = form_name;
  form_activity_info_->field_name = field_name;
  form_activity_info_->type = type;
  form_activity_info_->value = value;
  form_activity_info_->input_missing = input_missing;
}

void TestWebStateObserver::FaviconUrlUpdated(
    const std::vector<FaviconURL>& candidates) {
  update_favicon_url_candidates_info_ =
      base::MakeUnique<web::TestUpdateFaviconUrlCandidatesInfo>();
  update_favicon_url_candidates_info_->web_state = web_state();
  update_favicon_url_candidates_info_->candidates = candidates;
}

void TestWebStateObserver::RenderProcessGone() {
  render_process_gone_info_ =
      base::MakeUnique<web::TestRenderProcessGoneInfo>();
  render_process_gone_info_->web_state = web_state();
}

void TestWebStateObserver::WebStateDestroyed() {
  EXPECT_TRUE(web_state()->IsBeingDestroyed());
  web_state_destroyed_info_ =
      base::MakeUnique<web::TestWebStateDestroyedInfo>();
  web_state_destroyed_info_->web_state = web_state();
  Observe(nullptr);
}

void TestWebStateObserver::DidStartLoading() {
  start_loading_info_ = base::MakeUnique<web::TestStartLoadingInfo>();
  start_loading_info_->web_state = web_state();
}

void TestWebStateObserver::DidStopLoading() {
  stop_loading_info_ = base::MakeUnique<web::TestStopLoadingInfo>();
  stop_loading_info_->web_state = web_state();
}

}  // namespace web
