// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_PUBLIC_TEST_FAKES_TEST_WEB_STATE_OBSERVER_H_
#define IOS_WEB_PUBLIC_TEST_FAKES_TEST_WEB_STATE_OBSERVER_H_

#include "ios/web/public/test/fakes/test_web_state_observer_util.h"
#include "ios/web/public/web_state/web_state_observer.h"

namespace web {

class WebState;

// Test observer to check that the WebStateObserver methods are called as
// expected.
class TestWebStateObserver : public WebStateObserver {
 public:
  TestWebStateObserver(WebState* web_state);
  ~TestWebStateObserver() override;

  // Arguments passed to |DidStartNavigation|.
  web::TestDidStartNavigationInfo* did_start_navigation_info() {
    return did_start_navigation_info_.get();
  }
  // Arguments passed to |DidFinishNavigation|.
  web::TestDidFinishNavigationInfo* did_finish_navigation_info() {
    return did_finish_navigation_info_.get();
  }
  // Arguments passed to |NavigationItemCommitted|.
  web::TestCommitNavigationInfo* commit_navigation_info() {
    return commit_navigation_info_.get();
  }
  // Arguments passed to |PageLoaded|.
  web::TestLoadPageInfo* load_page_info() { return load_page_info_.get(); }
  // Arguments passed to |InterstitialDismissed|.
  web::TestDismissInterstitialInfo* dismiss_interstitial_info() {
    return dismiss_interstitial_info_.get();
  }
  // Arguments passed to |LoadProgressChanged|.
  web::TestChangeLoadingProgressInfo* change_loading_progress_info() {
    return change_loading_progress_info_.get();
  }
  // Arguments passed to |NavigationItemsPruned|.
  web::TestNavigationItemsPrunedInfo* navigation_items_pruned_info() {
    return navigation_items_pruned_info_.get();
  }
  // Arguments passed to |NavigationItemChanged|.
  web::TestNavigationItemChangedInfo* navigation_item_changed_info() {
    return navigation_item_changed_info_.get();
  }
  // Arguments passed to |TitleWasSet|.
  web::TestTitleWasSetInfo* title_was_set_info() {
    return title_was_set_info_.get();
  }
  // Arguments passed to |DidChangeVisibleSecurityState|.
  web::TestDidChangeVisibleSecurityStateInfo*
  did_change_visible_security_state_info() {
    return did_change_visible_security_state_info_.get();
  }
  // Arguments passed to |DidSuppressDialog|.
  web::TestDidSuppressDialogInfo* did_suppress_dialog_info() {
    return did_suppress_dialog_info_.get();
  }
  // Arguments passed to |DocumentSubmitted|.
  web::TestSubmitDocumentInfo* submit_document_info() {
    return submit_document_info_.get();
  }
  // Arguments passed to |FormActivityRegistered|.
  web::TestFormActivityInfo* form_activity_info() {
    return form_activity_info_.get();
  }
  // Arguments passed to |FaviconUrlUpdated|.
  web::TestUpdateFaviconUrlCandidatesInfo*
  update_favicon_url_candidates_info() {
    return update_favicon_url_candidates_info_.get();
  }
  // Arguments passed to |RenderProcessGone|.
  web::TestRenderProcessGoneInfo* render_process_gone_info() {
    return render_process_gone_info_.get();
  };
  // Arguments passed to |WebStateDestroyed|.
  web::TestWebStateDestroyedInfo* web_state_destroyed_info() {
    return web_state_destroyed_info_.get();
  };
  // Arguments passed to |DidStartLoading|.
  web::TestStopLoadingInfo* stop_loading_info() {
    return stop_loading_info_.get();
  }
  // Arguments passed to |DidStopLoading|.
  web::TestStartLoadingInfo* start_loading_info() {
    return start_loading_info_.get();
  }

 private:
  // WebStateObserver implementation:
  void NavigationItemCommitted(const LoadCommittedDetails&) override;
  void PageLoaded(PageLoadCompletionStatus load_completion_status) override;
  void InterstitialDismissed() override;
  void LoadProgressChanged(double progress) override;
  void NavigationItemsPruned(size_t pruned_item_count) override;
  void NavigationItemChanged() override;
  void DidStartNavigation(NavigationContext* context) override;
  void DidFinishNavigation(NavigationContext* context) override;
  void TitleWasSet() override;
  void DidChangeVisibleSecurityState() override;
  void DidSuppressDialog() override;
  void DocumentSubmitted(const std::string& form_name,
                         bool user_initiated) override;
  void FormActivityRegistered(const std::string& form_name,
                              const std::string& field_name,
                              const std::string& type,
                              const std::string& value,
                              bool input_missing) override;
  void FaviconUrlUpdated(const std::vector<FaviconURL>& candidates) override;
  void RenderProcessGone() override;
  void WebStateDestroyed() override;
  void DidStartLoading() override;
  void DidStopLoading() override;

  std::unique_ptr<web::TestCommitNavigationInfo> commit_navigation_info_;
  std::unique_ptr<web::TestLoadPageInfo> load_page_info_;
  std::unique_ptr<web::TestDismissInterstitialInfo> dismiss_interstitial_info_;
  std::unique_ptr<web::TestChangeLoadingProgressInfo>
      change_loading_progress_info_;
  std::unique_ptr<web::TestNavigationItemsPrunedInfo>
      navigation_items_pruned_info_;
  std::unique_ptr<web::TestNavigationItemChangedInfo>
      navigation_item_changed_info_;
  std::unique_ptr<web::TestDidStartNavigationInfo> did_start_navigation_info_;
  std::unique_ptr<web::TestDidFinishNavigationInfo> did_finish_navigation_info_;
  std::unique_ptr<web::TestTitleWasSetInfo> title_was_set_info_;
  std::unique_ptr<web::TestDidChangeVisibleSecurityStateInfo>
      did_change_visible_security_state_info_;
  std::unique_ptr<web::TestDidSuppressDialogInfo> did_suppress_dialog_info_;
  std::unique_ptr<web::TestSubmitDocumentInfo> submit_document_info_;
  std::unique_ptr<web::TestFormActivityInfo> form_activity_info_;
  std::unique_ptr<web::TestUpdateFaviconUrlCandidatesInfo>
      update_favicon_url_candidates_info_;
  std::unique_ptr<web::TestRenderProcessGoneInfo> render_process_gone_info_;
  std::unique_ptr<web::TestWebStateDestroyedInfo> web_state_destroyed_info_;
  std::unique_ptr<web::TestStartLoadingInfo> start_loading_info_;
  std::unique_ptr<web::TestStopLoadingInfo> stop_loading_info_;
};

}  // namespace web

#endif  // IOS_WEB_PUBLIC_TEST_FAKES_TEST_WEB_STATE_OBSERVER_H_
