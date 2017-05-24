// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/clean/chrome/browser/ui/omnibox/location_bar_mediator.h"

#include "base/memory/ptr_util.h"
#include "base/scoped_observer.h"
#include "base/strings/utf_string_conversions.h"
#include "ios/chrome/browser/ui/toolbar/toolbar_model_delegate_ios.h"
#include "ios/chrome/browser/ui/toolbar/toolbar_model_impl_ios.h"
#include "ios/chrome/browser/web_state_list/web_state_list.h"
#include "ios/chrome/browser/web_state_list/web_state_list_observer_bridge.h"
#import "ios/shared/chrome/browser/ui/omnibox/location_bar_controller.h"
#import "ios/web/public/navigation_manager.h"
#import "ios/web/public/web_state/web_state.h"
#import "ios/web/public/web_state/web_state_observer_bridge.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface LocationBarMediator ()<CRWWebStateObserver,
                                  LocationBarDelegate,
                                  WebStateListObserving>
@property(nonatomic, readwrite, assign) WebStateList* webStateList;
@end

@implementation LocationBarMediator {
  // Observes the WebStateList so that this mediator can update the UI when the
  // active WebState changes.
  std::unique_ptr<WebStateListObserverBridge> _webStateListObserver;
  std::unique_ptr<ScopedObserver<WebStateList, WebStateListObserverBridge>>
      _scopedWebStateListObserver;

  // Used to update the UI in response to WebState observer notifications.  This
  // observer is always observing the currently-active WebState and may be
  // nullptr if no WebState is currently active.
  std::unique_ptr<web::WebStateObserverBridge> _webStateObserver;

  // The LocationBarController that wraps OmniboxViewIOS and
  // OmniboxTextFieldIOS.  This mediator updates the UI through |_locationBar|
  // rather than through a consumer.
  std::unique_ptr<LocationBarController> _locationBar;

  // The ToolbarModelDelegate, backed by |_webStateList|.
  std::unique_ptr<ToolbarModelDelegateIOS> _toolbarModelDelegate;

  // The ToolbarModel that backs |_locationBar|.
  std::unique_ptr<ToolbarModelImplIOS> _toolbarModel;
}

@synthesize webStateList = _webStateList;

- (instancetype)initWithWebStateList:(WebStateList*)webStateList {
  if ((self = [super init])) {
    _webStateList = webStateList;

    _webStateListObserver = base::MakeUnique<WebStateListObserverBridge>(self);
    _scopedWebStateListObserver = base::MakeUnique<
        ScopedObserver<WebStateList, WebStateListObserverBridge>>(
        _webStateListObserver.get());
    _scopedWebStateListObserver->Add(_webStateList);
    web::WebState* webState = _webStateList->GetActiveWebState();
    if (webState) {
      _webStateObserver =
          base::MakeUnique<web::WebStateObserverBridge>(webState, self);
    }

    _toolbarModelDelegate =
        base::MakeUnique<ToolbarModelDelegateIOS>(webStateList);
    _toolbarModel =
        base::MakeUnique<ToolbarModelImplIOS>(_toolbarModelDelegate.get());
  }
  return self;
}

- (void)setLocationBar:(std::unique_ptr<LocationBarController>)locationBar {
  _locationBar = std::move(locationBar);
  _locationBar->OnToolbarUpdated();
}

#pragma mark - LocationBarDelegate

- (void)loadGURLFromLocationBar:(const GURL&)url
                     transition:(ui::PageTransition)transition {
  web::WebState* webState = _webStateList->GetActiveWebState();
  DCHECK(webState);

  if (url.SchemeIs(url::kJavaScriptScheme)) {
    // TODO(crbug.com/708341): Percent-unescape the url content first.
    webState->ExecuteJavaScript(base::UTF8ToUTF16(url.GetContent()));
    return;
  } else {
    // TODO(crbug.com/708341): Use WebState::OpenURL() here, because that method
    // can handle different WindowOpenDispositions.  The code below assumes
    // CURRENT_TAB.

    // When opening a URL, force the omnibox to resign first responder.  This
    // will also close the popup.
    web::NavigationManager::WebLoadParams loadParams(url);
    loadParams.transition_type = transition;
    loadParams.is_renderer_initiated = false;
    webState->GetNavigationManager()->LoadURLWithParams(loadParams);
  }

  _locationBar->HideKeyboardAndEndEditing();
  _locationBar->OnToolbarUpdated();
}

- (void)locationBarHasBecomeFirstResponder {
  // TODO(crbug.com/708341): Implement this method or edit this comment with an
  // explanation of what this method needs to do.
}

- (void)locationBarHasResignedFirstResponder {
  // TODO(crbug.com/708341): Implement this method or edit this comment with an
  // explanation of what this method needs to do.
}

- (void)locationBarBeganEdit {
  // TODO(crbug.com/708341): Implement this method or edit this comment with an
  // explanation of what this method needs to do.
}

- (void)locationBarChanged {
  // TODO(crbug.com/708341): Implement this method or edit this comment with an
  // explanation of what this method needs to do.
}

- (web::WebState*)getWebState {
  return _webStateList->GetActiveWebState();
}

- (ToolbarModel*)toolbarModel {
  return _toolbarModel->GetToolbarModel();
}

#pragma mark - WebStateListObserver

- (void)webStateList:(WebStateList*)webStateList
    didChangeActiveWebState:(web::WebState*)newWebState
                oldWebState:(web::WebState*)oldWebState
                    atIndex:(int)atIndex
                 userAction:(BOOL)userAction {
  // If the omnibox is focused, force it to resign first responder when the
  // active tab changes.
  _locationBar->HideKeyboardAndEndEditing();

  if (newWebState) {
    _webStateObserver =
        base::MakeUnique<web::WebStateObserverBridge>(newWebState, self);
  } else {
    _webStateObserver = nullptr;
  }

  _locationBar->OnToolbarUpdated();
}

#pragma mark - WebStateObserver

// WebState navigation events that could potentially affect the contents of the
// omnibox are caught below and used to drive updates to the UI.

- (void)webState:(web::WebState*)webState
    didStartNavigation:(web::NavigationContext*)navigation {
  _locationBar->OnToolbarUpdated();
}

- (void)webState:(web::WebState*)webState
    didCommitNavigationWithDetails:
        (const web::LoadCommittedDetails&)load_details {
  _locationBar->OnToolbarUpdated();
}

- (void)webState:(web::WebState*)webState
    didFinishNavigation:(web::NavigationContext*)navigation {
  _locationBar->OnToolbarUpdated();
}

- (void)webState:(web::WebState*)webState didLoadPageWithSuccess:(BOOL)success {
  _locationBar->OnToolbarUpdated();
}

- (void)webStateDidChangeVisibleSecurityState:(web::WebState*)webState {
  _locationBar->OnToolbarUpdated();
}

- (void)webStateDestroyed:(web::WebState*)webState {
  _webStateObserver = nullptr;
}

@end
