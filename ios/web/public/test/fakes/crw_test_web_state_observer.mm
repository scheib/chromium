// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/public/test/fakes/crw_test_web_state_observer.h"

#include "base/memory/ptr_util.h"
#import "ios/web/public/web_state/navigation_context.h"
#import "ios/web/web_state/navigation_context_impl.h"
#include "net/http/http_response_headers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace web {
TestFormActivityInfo::TestFormActivityInfo() {}
TestFormActivityInfo::~TestFormActivityInfo() = default;
TestUpdateFaviconUrlCandidatesInfo::TestUpdateFaviconUrlCandidatesInfo() {}
TestUpdateFaviconUrlCandidatesInfo::~TestUpdateFaviconUrlCandidatesInfo() =
    default;
}

@implementation CRWTestWebStateObserver {
  // Arguments passed to |webState:didStartNavigation:|.
  std::unique_ptr<web::TestDidStartNavigationInfo> _didStartNavigationInfo;
  // Arguments passed to |webState:didFinishNavigationForURL:|.
  std::unique_ptr<web::TestDidFinishNavigationInfo> _didFinishNavigationInfo;
  // Arguments passed to |webState:didCommitNavigationWithDetails:|.
  std::unique_ptr<web::TestCommitNavigationInfo> _commitNavigationInfo;
  // Arguments passed to |webState:didLoadPageWithSuccess:|.
  std::unique_ptr<web::TestLoadPageInfo> _loadPageInfo;
  // Arguments passed to |webStateDidDismissInterstitial:|.
  std::unique_ptr<web::TestDismissInterstitialInfo> _dismissInterstitialInfo;
  // Arguments passed to |webState:didChangeLoadingProgress:|.
  std::unique_ptr<web::TestChangeLoadingProgressInfo>
      _changeLoadingProgressInfo;
  // Arguments passed to |webStateDidChangeTitle:|.
  std::unique_ptr<web::TestTitleWasSetInfo> _titleWasSetInfo;
  // Arguments passed to |webStateDidChangeVisibleSecurityState:|.
  std::unique_ptr<web::TestDidChangeVisibleSecurityStateInfo>
      _didChangeVisibleSecurityStateInfo;
  // Arguments passed to |webStateDidSuppressDialog:|.
  std::unique_ptr<web::TestDidSuppressDialogInfo> _didSuppressDialogInfo;
  // Arguments passed to
  // |webState:didSubmitDocumentWithFormNamed:userInitiated:|.
  std::unique_ptr<web::TestSubmitDocumentInfo> _submitDocumentInfo;
  // Arguments passed to
  // |webState:didRegisterFormActivityWithFormNamed:fieldName:type:value:|.
  std::unique_ptr<web::TestFormActivityInfo> _formActivityInfo;
  // Arguments passed to |webState:didUpdateFaviconURLCandidates|.
  std::unique_ptr<web::TestUpdateFaviconUrlCandidatesInfo>
      _updateFaviconUrlCandidatesInfo;
  // Arguments passed to |webState:renderProcessGoneForWebState:|.
  std::unique_ptr<web::TestRenderProcessGoneInfo> _renderProcessGoneInfo;
  // Arguments passed to |webStateDestroyed:|.
  std::unique_ptr<web::TestWebStateDestroyedInfo> _webStateDestroyedInfo;
  // Arguments passed to |webStateDidStopLoading:|.
  std::unique_ptr<web::TestStopLoadingInfo> _stopLoadingInfo;
  // Arguments passed to |webStateDidStartLoading:|.
  std::unique_ptr<web::TestStartLoadingInfo> _startLoadingInfo;
}

- (web::TestDidStartNavigationInfo*)didStartNavigationInfo {
  return _didStartNavigationInfo.get();
}

- (web::TestDidFinishNavigationInfo*)didFinishNavigationInfo {
  return _didFinishNavigationInfo.get();
}

- (web::TestCommitNavigationInfo*)commitNavigationInfo {
  return _commitNavigationInfo.get();
}

- (web::TestLoadPageInfo*)loadPageInfo {
  return _loadPageInfo.get();
}

- (web::TestDismissInterstitialInfo*)dismissInterstitialInfo {
  return _dismissInterstitialInfo.get();
}

- (web::TestChangeLoadingProgressInfo*)changeLoadingProgressInfo {
  return _changeLoadingProgressInfo.get();
}

- (web::TestTitleWasSetInfo*)titleWasSetInfo {
  return _titleWasSetInfo.get();
}

- (web::TestDidChangeVisibleSecurityStateInfo*)
    didChangeVisibleSecurityStateInfo {
  return _didChangeVisibleSecurityStateInfo.get();
}

- (web::TestDidSuppressDialogInfo*)didSuppressDialogInfo {
  return _didSuppressDialogInfo.get();
}

- (web::TestSubmitDocumentInfo*)submitDocumentInfo {
  return _submitDocumentInfo.get();
}

- (web::TestFormActivityInfo*)formActivityInfo {
  return _formActivityInfo.get();
}

- (web::TestUpdateFaviconUrlCandidatesInfo*)updateFaviconUrlCandidatesInfo {
  return _updateFaviconUrlCandidatesInfo.get();
}

- (web::TestRenderProcessGoneInfo*)renderProcessGoneInfo {
  return _renderProcessGoneInfo.get();
}

- (web::TestWebStateDestroyedInfo*)webStateDestroyedInfo {
  return _webStateDestroyedInfo.get();
}

- (web::TestStopLoadingInfo*)stopLoadingInfo {
  return _stopLoadingInfo.get();
}

- (web::TestStartLoadingInfo*)startLoadingInfo {
  return _startLoadingInfo.get();
}

#pragma mark CRWWebStateObserver methods -

- (void)webState:(web::WebState*)webState
    didStartNavigation:(web::NavigationContext*)navigation {
  ASSERT_TRUE(!navigation->GetError() || !navigation->IsSameDocument());
  _didStartNavigationInfo = base::MakeUnique<web::TestDidStartNavigationInfo>();
  _didStartNavigationInfo->web_state = webState;
  std::unique_ptr<web::NavigationContextImpl> context =
      web::NavigationContextImpl::CreateNavigationContext(
          navigation->GetWebState(), navigation->GetUrl());
  context->SetIsSameDocument(navigation->IsSameDocument());
  context->SetError(navigation->GetError());
  _didStartNavigationInfo->context = std::move(context);
}

- (void)webState:(web::WebState*)webState
    didCommitNavigationWithDetails:
        (const web::LoadCommittedDetails&)load_details {
  _commitNavigationInfo = base::MakeUnique<web::TestCommitNavigationInfo>();
  _commitNavigationInfo->web_state = webState;
  _commitNavigationInfo->load_details = load_details;
}

- (void)webState:(web::WebState*)webState
    didFinishNavigation:(web::NavigationContext*)navigation {
  ASSERT_TRUE(!navigation->GetError() || !navigation->IsSameDocument());
  _didFinishNavigationInfo =
      base::MakeUnique<web::TestDidFinishNavigationInfo>();
  _didFinishNavigationInfo->web_state = webState;
  std::unique_ptr<web::NavigationContextImpl> context =
      web::NavigationContextImpl::CreateNavigationContext(
          navigation->GetWebState(), navigation->GetUrl());
  context->SetIsSameDocument(navigation->IsSameDocument());
  context->SetError(navigation->GetError());
  _didFinishNavigationInfo->context = std::move(context);
}

- (void)webState:(web::WebState*)webState didLoadPageWithSuccess:(BOOL)success {
  _loadPageInfo = base::MakeUnique<web::TestLoadPageInfo>();
  _loadPageInfo->web_state = webState;
  _loadPageInfo->success = success;
}

- (void)webStateDidDismissInterstitial:(web::WebState*)webState {
  _dismissInterstitialInfo =
      base::MakeUnique<web::TestDismissInterstitialInfo>();
  _dismissInterstitialInfo->web_state = webState;
}

- (void)webState:(web::WebState*)webState
    didChangeLoadingProgress:(double)progress {
  _changeLoadingProgressInfo =
      base::MakeUnique<web::TestChangeLoadingProgressInfo>();
  _changeLoadingProgressInfo->web_state = webState;
  _changeLoadingProgressInfo->progress = progress;
}

- (void)webStateDidChangeTitle:(web::WebState*)webState {
  _titleWasSetInfo = base::MakeUnique<web::TestTitleWasSetInfo>();
  _titleWasSetInfo->web_state = webState;
}

- (void)webStateDidChangeVisibleSecurityState:(web::WebState*)webState {
  _didChangeVisibleSecurityStateInfo =
      base::MakeUnique<web::TestDidChangeVisibleSecurityStateInfo>();
  _didChangeVisibleSecurityStateInfo->web_state = webState;
}

- (void)webStateDidSuppressDialog:(web::WebState*)webState {
  _didSuppressDialogInfo = base::MakeUnique<web::TestDidSuppressDialogInfo>();
  _didSuppressDialogInfo->web_state = webState;
}

- (void)webState:(web::WebState*)webState
    didSubmitDocumentWithFormNamed:(const std::string&)formName
                     userInitiated:(BOOL)userInitiated {
  _submitDocumentInfo = base::MakeUnique<web::TestSubmitDocumentInfo>();
  _submitDocumentInfo->web_state = webState;
  _submitDocumentInfo->form_name = formName;
  _submitDocumentInfo->user_initiated = userInitiated;
}

- (void)webState:(web::WebState*)webState
    didRegisterFormActivityWithFormNamed:(const std::string&)formName
                               fieldName:(const std::string&)fieldName
                                    type:(const std::string&)type
                                   value:(const std::string&)value
                            inputMissing:(BOOL)inputMissing {
  _formActivityInfo = base::MakeUnique<web::TestFormActivityInfo>();
  _formActivityInfo->web_state = webState;
  _formActivityInfo->form_name = formName;
  _formActivityInfo->field_name = fieldName;
  _formActivityInfo->type = type;
  _formActivityInfo->value = value;
  _formActivityInfo->input_missing = inputMissing;
}

- (void)webState:(web::WebState*)webState
    didUpdateFaviconURLCandidates:
        (const std::vector<web::FaviconURL>&)candidates {
  _updateFaviconUrlCandidatesInfo =
      base::MakeUnique<web::TestUpdateFaviconUrlCandidatesInfo>();
  _updateFaviconUrlCandidatesInfo->web_state = webState;
  _updateFaviconUrlCandidatesInfo->candidates = candidates;
}

- (void)renderProcessGoneForWebState:(web::WebState*)webState {
  _renderProcessGoneInfo = base::MakeUnique<web::TestRenderProcessGoneInfo>();
  _renderProcessGoneInfo->web_state = webState;
}

- (void)webStateDestroyed:(web::WebState*)webState {
  _webStateDestroyedInfo = base::MakeUnique<web::TestWebStateDestroyedInfo>();
  _webStateDestroyedInfo->web_state = webState;
}

- (void)webStateDidStopLoading:(web::WebState*)webState {
  _stopLoadingInfo = base::MakeUnique<web::TestStopLoadingInfo>();
  _stopLoadingInfo->web_state = webState;
}

- (void)webStateDidStartLoading:(web::WebState*)webState {
  _startLoadingInfo = base::MakeUnique<web::TestStartLoadingInfo>();
  _startLoadingInfo->web_state = webState;
}

@end
