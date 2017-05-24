// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/public/web_state/web_state_observer_bridge.h"

namespace web {

WebStateObserverBridge::WebStateObserverBridge(web::WebState* webState,
                                               id<CRWWebStateObserver> observer)
    : web::WebStateObserver(webState), observer_(observer) {
}

WebStateObserverBridge::~WebStateObserverBridge() {
}

void WebStateObserverBridge::NavigationItemCommitted(
    const web::LoadCommittedDetails& load_detatils) {
  SEL selector = @selector(webState:didCommitNavigationWithDetails:);
  if ([observer_ respondsToSelector:selector]) {
    [observer_ webState:web_state()
        didCommitNavigationWithDetails:load_detatils];
  }
}

void WebStateObserverBridge::DidStartNavigation(
    web::NavigationContext* navigation_context) {
  if ([observer_ respondsToSelector:@selector(webState:didStartNavigation:)]) {
    [observer_ webState:web_state() didStartNavigation:navigation_context];
  }
}

void WebStateObserverBridge::DidFinishNavigation(
    web::NavigationContext* navigation_context) {
  if ([observer_ respondsToSelector:@selector(webState:didFinishNavigation:)]) {
    [observer_ webState:web_state() didFinishNavigation:navigation_context];
  }
}

void WebStateObserverBridge::DidStartLoading() {
  SEL selector = @selector(webStateDidStartLoading:);
  if ([observer_ respondsToSelector:selector]) {
    [observer_ webStateDidStartLoading:web_state()];
  }
}

void WebStateObserverBridge::DidStopLoading() {
  SEL selector = @selector(webStateDidStopLoading:);
  if ([observer_ respondsToSelector:selector]) {
    [observer_ webStateDidStopLoading:web_state()];
  }
}

void WebStateObserverBridge::PageLoaded(
    web::PageLoadCompletionStatus load_completion_status) {
  SEL selector = @selector(webState:didLoadPageWithSuccess:);
  if ([observer_ respondsToSelector:selector]) {
    BOOL success = NO;
    switch (load_completion_status) {
      case PageLoadCompletionStatus::SUCCESS:
        success = YES;
        break;
      case PageLoadCompletionStatus::FAILURE:
        success = NO;
        break;
    }
    [observer_ webState:web_state() didLoadPageWithSuccess:success];
  }
}

void WebStateObserverBridge::InterstitialDismissed() {
  SEL selector = @selector(webStateDidDismissInterstitial:);
  if ([observer_ respondsToSelector:selector])
    [observer_ webStateDidDismissInterstitial:web_state()];
}

void WebStateObserverBridge::LoadProgressChanged(double progress) {
  SEL selector = @selector(webState:didChangeLoadingProgress:);
  if ([observer_ respondsToSelector:selector])
    [observer_ webState:web_state() didChangeLoadingProgress:progress];
}

void WebStateObserverBridge::TitleWasSet() {
  if ([observer_ respondsToSelector:@selector(webStateDidChangeTitle:)])
    [observer_ webStateDidChangeTitle:web_state()];
}

void WebStateObserverBridge::DidChangeVisibleSecurityState() {
  SEL selector = @selector(webStateDidChangeVisibleSecurityState:);
  if ([observer_ respondsToSelector:selector])
    [observer_ webStateDidChangeVisibleSecurityState:web_state()];
}

void WebStateObserverBridge::DidSuppressDialog() {
  if ([observer_ respondsToSelector:@selector(webStateDidSuppressDialog:)])
    [observer_ webStateDidSuppressDialog:web_state()];
}

void WebStateObserverBridge::DocumentSubmitted(const std::string& form_name,
                                               bool user_initiated) {
  SEL selector =
      @selector(webState:didSubmitDocumentWithFormNamed:userInitiated:);
  if ([observer_ respondsToSelector:selector]) {
    [observer_ webState:web_state()
        didSubmitDocumentWithFormNamed:form_name
                         userInitiated:user_initiated];
  }
}

void WebStateObserverBridge::FormActivityRegistered(
    const std::string& form_name,
    const std::string& field_name,
    const std::string& type,
    const std::string& value,
    bool input_missing) {
  SEL selector = @selector(webState:
      didRegisterFormActivityWithFormNamed:
                                 fieldName:
                                      type:
                                     value:
                              inputMissing:);
  if ([observer_ respondsToSelector:selector]) {
    [observer_ webState:web_state()
        didRegisterFormActivityWithFormNamed:form_name
                                   fieldName:field_name
                                        type:type
                                       value:value
                                inputMissing:input_missing];
  }
}

void WebStateObserverBridge::FaviconUrlUpdated(
    const std::vector<FaviconURL>& candidates) {
  SEL selector = @selector(webState:didUpdateFaviconURLCandidates:);
  if ([observer_ respondsToSelector:selector])
    [observer_ webState:web_state() didUpdateFaviconURLCandidates:candidates];
}

void WebStateObserverBridge::RenderProcessGone() {
  if ([observer_ respondsToSelector:@selector(renderProcessGoneForWebState:)])
    [observer_ renderProcessGoneForWebState:web_state()];
}

void WebStateObserverBridge::WebStateDestroyed() {
  SEL selector = @selector(webStateDestroyed:);
  if ([observer_ respondsToSelector:selector]) {
    // |webStateDestroyed:| may delete |this|, so don't expect |this| to be
    // valid afterwards.
    [observer_ webStateDestroyed:web_state()];
  }
}

}  // namespace web
