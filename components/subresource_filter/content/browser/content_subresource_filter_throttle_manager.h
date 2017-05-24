// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SUBRESOURCE_FILTER_CONTENT_BROWSER_CONTENT_SUBRESOURCE_FILTER_THROTTLE_MANAGER_H_
#define COMPONENTS_SUBRESOURCE_FILTER_CONTENT_BROWSER_CONTENT_SUBRESOURCE_FILTER_THROTTLE_MANAGER_H_

#include <memory>
#include <unordered_map>
#include <vector>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "components/subresource_filter/content/browser/verified_ruleset_dealer.h"
#include "components/subresource_filter/core/common/activation_state.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class NavigationHandle;
class NavigationThrottle;
class RenderFrameHost;
}  // namespace content

namespace IPC {
class Message;
}  // namespace IPC

namespace subresource_filter {

class AsyncDocumentSubresourceFilter;
class ActivationStateComputingNavigationThrottle;
class SubframeNavigationFilteringThrottle;
class PageLoadStatistics;
struct DocumentLoadStatistics;

// The ContentSubresourceFilterThrottleManager manages NavigationThrottles in
// order to calculate frame activation states and subframe navigation filtering,
// within a given WebContents. It contains a mapping of all activated
// RenderFrameHosts, along with their associated DocumentSubresourceFilters.
//
// The class is designed to be used by a Delegate, which shares lifetime with
// this class (aka the typical lifetime of a WebContentsObserver). The delegate
// will be notified of the first disallowed subresource load for a top level
// navgation, and has veto power for frame activation.
class ContentSubresourceFilterThrottleManager
    : public content::WebContentsObserver {
 public:
  // It is expected that the Delegate outlives |this|, and manages the lifetime
  // of this class.
  class Delegate {
   public:
    // The embedder may be interested in displaying UI to the user when the
    // first load is disallowed for a given page load.
    virtual void OnFirstSubresourceLoadDisallowed() {}
  };

  ContentSubresourceFilterThrottleManager(
      Delegate* delegate,
      VerifiedRulesetDealer::Handle* dealer_handle,
      content::WebContents* web_contents);
  ~ContentSubresourceFilterThrottleManager() override;

  // Sets the desired page-level |activation_state| for the currently ongoing
  // page load, identified by its main-frame |navigation_handle|. To be called
  // by the embedder at the latest in the WillProcessResponse stage from a
  // NavigationThrottle that was registered before the throttles created by this
  // manager in MaybeAppendNavigationThrottles(). If this method is not called
  // for a main-frame navigation, the default behavior is no activation for that
  // page load.
  void NotifyPageActivationComputed(
      content::NavigationHandle* navigation_handle,
      const ActivationState& activation_state);

  // This method inspects |navigation_handle| and attaches navigation throttles
  // appropriately, based on the current state of frame activation.
  //
  // 1. Subframe navigation filtering throttles are appended if the parent
  // frame is activated.
  // 2. Activation state computing throttles are appended if either the
  // navigation is a main frame navigation, or if the parent frame is activated.
  //
  // Note that there is currently no constraints on the ordering of throttles.
  void MaybeAppendNavigationThrottles(
      content::NavigationHandle* navigation_handle,
      std::vector<std::unique_ptr<content::NavigationThrottle>>* throttles);

  // Returns whether or not the current WebContents is allowed to create a new
  // window.
  bool ShouldDisallowNewWindow();

  VerifiedRuleset::Handle* ruleset_handle_for_testing() {
    return ruleset_handle_.get();
  }

 protected:
  // content::WebContentsObserver:
  void RenderFrameDeleted(content::RenderFrameHost* frame_host) override;
  void ReadyToCommitNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishLoad(content::RenderFrameHost* render_frame_host,
                     const GURL& validated_url) override;
  bool OnMessageReceived(const IPC::Message& message,
                         content::RenderFrameHost* render_frame_host) override;

 private:
  std::unique_ptr<SubframeNavigationFilteringThrottle>
  MaybeCreateSubframeNavigationFilteringThrottle(
      content::NavigationHandle* navigation_handle);
  std::unique_ptr<ActivationStateComputingNavigationThrottle>
  MaybeCreateActivationStateComputingThrottle(
      content::NavigationHandle* navigation_handle);

  // Will return nullptr if the parent frame of this navigation is not
  // activated (and therefore has no subresource filter).
  AsyncDocumentSubresourceFilter* GetParentFrameFilter(
      content::NavigationHandle* child_frame_navigation);

  // Calls OnFirstSubresourceLoadDisallowed on the Delegate at most once per
  // committed, non-same-page navigation in the main frame.
  void MaybeCallFirstDisallowedLoad();

  VerifiedRuleset::Handle* EnsureRulesetHandle();
  void DestroyRulesetHandleIfNoLongerUsed();

  void OnDocumentLoadStatistics(const DocumentLoadStatistics& statistics);

  // For each RenderFrameHost where the last committed load has subresource
  // filtering activated, owns the corresponding AsyncDocumentSubresourceFilter.
  std::unordered_map<content::RenderFrameHost*,
                     std::unique_ptr<AsyncDocumentSubresourceFilter>>
      activated_frame_hosts_;

  // For each ongoing navigation that requires activation state computation,
  // keeps track of the throttle that is carrying out that computation, so that
  // the result can be retrieved when the navigation is ready to commit.
  std::unordered_map<content::NavigationHandle*,
                     ActivationStateComputingNavigationThrottle*>
      ongoing_activation_throttles_;

  // Lazily instantiated in EnsureRulesetHandle when the first page level
  // activation is triggered. Will go away when there are no more activated
  // RenderFrameHosts (i.e. activated_frame_hosts_ is empty).
  std::unique_ptr<VerifiedRuleset::Handle> ruleset_handle_;

  std::unique_ptr<PageLoadStatistics> statistics_;

  // True if the current committed main frame load in this WebContents has
  // notified the delegate that a subresource was disallowed. The callback
  // should only be called at most once per main frame load.
  bool current_committed_load_has_notified_disallowed_load_ = false;

  // These members outlive this class.
  VerifiedRulesetDealer::Handle* dealer_handle_;
  Delegate* delegate_;

  base::WeakPtrFactory<ContentSubresourceFilterThrottleManager>
      weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ContentSubresourceFilterThrottleManager);
};

}  // namespace subresource_filter

#endif  // COMPONENTS_SUBRESOURCE_FILTER_CONTENT_BROWSER_CONTENT_SUBRESOURCE_FILTER_THROTTLE_MANAGER_H_
