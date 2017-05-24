// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_PUBLIC_WEB_STATE_WEB_STATE_OBSERVER_BRIDGE_H_
#define IOS_WEB_PUBLIC_WEB_STATE_WEB_STATE_OBSERVER_BRIDGE_H_

#import <Foundation/Foundation.h>

#include <string>

#import "base/ios/weak_nsobject.h"
#include "base/macros.h"
#include "ios/web/public/web_state/web_state_observer.h"

// Observes page lifecyle events from Objective-C. To use as a
// web::WebStateObserver, wrap in a web::WebStateObserverBridge.
@protocol CRWWebStateObserver<NSObject>
@optional

// Invoked by WebStateObserverBridge::NavigationItemCommitted.
- (void)webState:(web::WebState*)webState
    didCommitNavigationWithDetails:
        (const web::LoadCommittedDetails&)load_details;

// Invoked by WebStateObserverBridge::DidStartNavigation.
- (void)webState:(web::WebState*)webState
    didStartNavigation:(web::NavigationContext*)navigation;

// Invoked by WebStateObserverBridge::DidFinishNavigation.
- (void)webState:(web::WebState*)webState
    didFinishNavigation:(web::NavigationContext*)navigation;

// Invoked by WebStateObserverBridge::PageLoaded.
- (void)webState:(web::WebState*)webState didLoadPageWithSuccess:(BOOL)success;

// Invoked by WebStateObserverBridge::InterstitialDismissed.
- (void)webStateDidDismissInterstitial:(web::WebState*)webState;

// Invoked by WebStateObserverBridge::LoadProgressChanged.
- (void)webState:(web::WebState*)webState
    didChangeLoadingProgress:(double)progress;

// Invoked by WebStateObserverBridge::TitleWasSet.
- (void)webStateDidChangeTitle:(web::WebState*)webState;

// Invoked by WebStateObserverBridge::DidChangeVisibleSecurityState.
- (void)webStateDidChangeVisibleSecurityState:(web::WebState*)webState;

// Invoked by WebStateObserverBridge::DidSuppressDialog.
- (void)webStateDidSuppressDialog:(web::WebState*)webState;

// Invoked by WebStateObserverBridge::DocumentSubmitted.
- (void)webState:(web::WebState*)webState
    didSubmitDocumentWithFormNamed:(const std::string&)formName
                     userInitiated:(BOOL)userInitiated;

// Invoked by WebStateObserverBridge::FormActivityRegistered.
// TODO(ios): Method should take data transfer object rather than parameters.
- (void)webState:(web::WebState*)webState
    didRegisterFormActivityWithFormNamed:(const std::string&)formName
                               fieldName:(const std::string&)fieldName
                                    type:(const std::string&)type
                                   value:(const std::string&)value
                            inputMissing:(BOOL)inputMissing;

// Invoked by WebStateObserverBridge::FaviconUrlUpdated.
- (void)webState:(web::WebState*)webState
    didUpdateFaviconURLCandidates:
        (const std::vector<web::FaviconURL>&)candidates;

// Invoked by WebStateObserverBridge::RenderProcessGone.
- (void)renderProcessGoneForWebState:(web::WebState*)webState;

// Note: after |webStateDestroyed:| is invoked, the WebState being observed
// is no longer valid.
- (void)webStateDestroyed:(web::WebState*)webState;

// Invoked by WebStateObserverBridge::DidStopLoading.
- (void)webStateDidStopLoading:(web::WebState*)webState;

// Invoked by WebStateObserverBridge::DidStartLoading.
- (void)webStateDidStartLoading:(web::WebState*)webState;

@end

namespace web {

class WebState;

// Bridge to use an id<CRWWebStateObserver> as a web::WebStateObserver.
// Will be added/removed as an observer of the underlying WebState during
// construction/destruction. Instances should be owned by instances of the
// class they're bridging.
class WebStateObserverBridge : public web::WebStateObserver {
 public:
  WebStateObserverBridge(web::WebState* web_state,
                         id<CRWWebStateObserver> observer);
  ~WebStateObserverBridge() override;

  // web::WebStateObserver methods.
  void NavigationItemCommitted(
      const LoadCommittedDetails& load_details) override;
  void DidStartNavigation(NavigationContext* navigation_context) override;
  void DidFinishNavigation(NavigationContext* navigation_context) override;
  void PageLoaded(
      web::PageLoadCompletionStatus load_completion_status) override;
  void InterstitialDismissed() override;
  void LoadProgressChanged(double progress) override;
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

 private:
  base::WeakNSProtocol<id<CRWWebStateObserver>> observer_;
  DISALLOW_COPY_AND_ASSIGN(WebStateObserverBridge);
};

}  // namespace web

#endif  // IOS_WEB_PUBLIC_WEB_STATE_WEB_STATE_OBSERVER_BRIDGE_H_
