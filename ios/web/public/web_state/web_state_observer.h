// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_PUBLIC_WEB_STATE_WEB_STATE_OBSERVER_H_
#define IOS_WEB_PUBLIC_WEB_STATE_WEB_STATE_OBSERVER_H_

#include <stddef.h>

#include <string>
#include <vector>

#include "base/macros.h"

namespace web {

struct FaviconURL;
class NavigationContext;
struct LoadCommittedDetails;
class WebState;
class TestWebState;
class WebStateImpl;

enum class PageLoadCompletionStatus : bool { SUCCESS = 0, FAILURE = 1 };

// An observer API implemented by classes which are interested in various page
// load events from WebState.
class WebStateObserver {
 public:
  // Returns the web state associated with this observer.
  WebState* web_state() const { return web_state_; }

  // This method is invoked when committed navigation items have been pruned.
  virtual void NavigationItemsPruned(size_t pruned_item_count) {}

  // This method is invoked either when NavigationItem's titile did change or
  // when window.location.replace JavaScript API was called.
  // DEPRECATED. Use |TitleWasSet| to listen for title changes and
  // |DidFinishNavigation| for |window.location.replace|.
  // TODO(crbug.com/720786): Remove this method.
  virtual void NavigationItemChanged() {}

  // This method is invoked when a new non-pending navigation item is created.
  // This corresponds to one NavigationManager item being created
  // (in the case of new navigations) or renavigated to (for back/forward
  // navigations).
  virtual void NavigationItemCommitted(
      const LoadCommittedDetails& load_details) {}

  // Called when a navigation started in the WebState for the main frame.
  // |navigation_context| is unique to a specific navigation. The same
  // NavigationContext will be provided on subsequent call to
  // DidFinishNavigation() when related to this navigation. Observers should
  // clear any references to |navigation_context| in DidFinishNavigation(), just
  // before it is destroyed.
  //
  // This is also fired by same-document navigations, such as fragment
  // navigations or pushState/replaceState, which will not result in a document
  // change. To filter these out, use NavigationContext::IsSameDocument().
  //
  // More than one navigation can be ongoing in the same frame at the same
  // time. Each will get its own NavigationHandle.
  //
  // There is no guarantee that DidFinishNavigation() will be called for any
  // particular navigation before DidStartNavigation is called on the next.
  virtual void DidStartNavigation(NavigationContext* navigation_context) {}

  // Called when a navigation finished in the WebState for the main frame. This
  // happens when a navigation is committed, aborted or replaced by a new one.
  // To know if the navigation has resulted in an error page, use
  // NavigationContext::GetError().
  //
  // If this is called because the navigation committed, then the document load
  // will still be ongoing in the WebState returned by |navigation_context|.
  // Use the document loads events such as DidStopLoading
  // and related methods to listen for continued events from this
  // WebState.
  //
  // This is also fired by same-document navigations, such as fragment
  // navigations or pushState/replaceState, which will not result in a document
  // change. To filter these out, use NavigationContext::IsSameDocument().
  //
  // |navigation_context| will be destroyed at the end of this call, so do not
  // keep a reference to it afterward.
  virtual void DidFinishNavigation(NavigationContext* navigation_context) {}

  // Called when the current page has started loading.
  virtual void DidStartLoading() {}

  // Called when the current page has stopped loading.
  virtual void DidStopLoading() {}

  // Called when the current page is loaded.
  virtual void PageLoaded(PageLoadCompletionStatus load_completion_status) {}

  // Called when the interstitial is dismissed by the user.
  virtual void InterstitialDismissed() {}

  // Notifies the observer that the page has made some progress loading.
  // |progress| is a value between 0.0 (nothing loaded) to 1.0 (page fully
  // loaded).
  virtual void LoadProgressChanged(double progress) {}

  // Called when the title of the WebState is set.
  virtual void TitleWasSet() {}

  // Called when the visible security state of the page changes.
  virtual void DidChangeVisibleSecurityState() {}

  // Called when a JavaScript dialog or window open request was suppressed.
  // NOTE: Called only if WebState::SetShouldSuppressDialogs() was called with
  // false.
  virtual void DidSuppressDialog() {}

  // Called on form submission. |user_initiated| is true if the user
  // interacted with the page.
  virtual void DocumentSubmitted(const std::string& form_name,
                                 bool user_initiated) {}

  // Called when the user is typing on a form field, with |error| indicating if
  // there is any error when parsing the form field information.
  virtual void FormActivityRegistered(const std::string& form_name,
                                      const std::string& field_name,
                                      const std::string& type,
                                      const std::string& value,
                                      bool input_missing) {}

  // Invoked when new favicon URL candidates are received.
  virtual void FaviconUrlUpdated(const std::vector<FaviconURL>& candidates) {}

  // Called when the web process is terminated (usually by crashing, though
  // possibly by other means).
  virtual void RenderProcessGone() {}

  // Invoked when the WebState is being destroyed. Gives subclasses a chance
  // to cleanup.
  virtual void WebStateDestroyed() {}

 protected:
  // Use this constructor when the object is tied to a single WebState for
  // its entire lifetime.
  explicit WebStateObserver(WebState* web_state);

  // Use this constructor when the object wants to observe a WebState for
  // part of its lifetime.  It can then call Observe() to start and stop
  // observing.
  WebStateObserver();

  virtual ~WebStateObserver();

  // Start observing a different WebState; used with the default constructor.
  void Observe(WebState* web_state);

 private:
  friend class WebStateImpl;
  friend class TestWebState;

  // Stops observing the current web state.
  void ResetWebState();

  WebState* web_state_;

  DISALLOW_COPY_AND_ASSIGN(WebStateObserver);
};

}  // namespace web

#endif  // IOS_WEB_PUBLIC_WEB_STATE_WEB_STATE_OBSERVER_H_
