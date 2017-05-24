// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/subresource_filter/content/browser/content_subresource_filter_throttle_manager.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "components/subresource_filter/content/browser/activation_state_computing_navigation_throttle.h"
#include "components/subresource_filter/content/browser/async_document_subresource_filter.h"
#include "components/subresource_filter/content/browser/page_load_statistics.h"
#include "components/subresource_filter/content/browser/subframe_navigation_filtering_throttle.h"
#include "components/subresource_filter/content/common/subresource_filter_messages.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"

namespace subresource_filter {

ContentSubresourceFilterThrottleManager::
    ContentSubresourceFilterThrottleManager(
        Delegate* delegate,
        VerifiedRulesetDealer::Handle* dealer_handle,
        content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      dealer_handle_(dealer_handle),
      delegate_(delegate),
      weak_ptr_factory_(this) {}

ContentSubresourceFilterThrottleManager::
    ~ContentSubresourceFilterThrottleManager() {}

void ContentSubresourceFilterThrottleManager::NotifyPageActivationComputed(
    content::NavigationHandle* navigation_handle,
    const ActivationState& activation_state) {
  DCHECK(navigation_handle->IsInMainFrame());
  DCHECK(!navigation_handle->HasCommitted());
  auto it = ongoing_activation_throttles_.find(navigation_handle);
  if (it != ongoing_activation_throttles_.end()) {
    it->second->NotifyPageActivationWithRuleset(EnsureRulesetHandle(),
                                                activation_state);
  }
}

void ContentSubresourceFilterThrottleManager::RenderFrameDeleted(
    content::RenderFrameHost* frame_host) {
  activated_frame_hosts_.erase(frame_host);
  DestroyRulesetHandleIfNoLongerUsed();
}

// Pull the AsyncDocumentSubresourceFilter and its associated ActivationState
// out of the activation state computing throttle. Store it for later filtering
// of subframe navigations.
void ContentSubresourceFilterThrottleManager::ReadyToCommitNavigation(
    content::NavigationHandle* navigation_handle) {
  auto throttle = ongoing_activation_throttles_.find(navigation_handle);
  if (throttle == ongoing_activation_throttles_.end())
    return;

  // A filter with DISABLED activation indicates a corrupted ruleset.
  AsyncDocumentSubresourceFilter* filter = throttle->second->filter();
  if (!filter || navigation_handle->GetNetErrorCode() != net::OK ||
      filter->activation_state().activation_level ==
          ActivationLevel::DISABLED) {
    return;
  }

  throttle->second->WillSendActivationToRenderer();

  content::RenderFrameHost* frame_host =
      navigation_handle->GetRenderFrameHost();
  frame_host->Send(new SubresourceFilterMsg_ActivateForNextCommittedLoad(
      frame_host->GetRoutingID(), filter->activation_state()));
}

void ContentSubresourceFilterThrottleManager::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  // Do nothing if the navigation finished in the same document. Just make sure
  // to not leak throttle pointers.
  if (!navigation_handle->HasCommitted() ||
      navigation_handle->IsSameDocument()) {
    ongoing_activation_throttles_.erase(navigation_handle);
    return;
  }

  auto throttle = ongoing_activation_throttles_.find(navigation_handle);
  std::unique_ptr<AsyncDocumentSubresourceFilter> filter;
  if (throttle != ongoing_activation_throttles_.end()) {
    filter = throttle->second->ReleaseFilter();
    ongoing_activation_throttles_.erase(throttle);
  }

  if (navigation_handle->IsInMainFrame()) {
    current_committed_load_has_notified_disallowed_load_ = false;
    statistics_.reset();
    if (filter) {
      statistics_ =
          base::MakeUnique<PageLoadStatistics>(filter->activation_state());
    }
  }

  // Make sure |activated_frame_hosts_| is updated or cleaned up depending on
  // this navigation's activation state.
  content::RenderFrameHost* frame_host =
      navigation_handle->GetRenderFrameHost();
  if (filter) {
    filter->set_first_disallowed_load_callback(base::Bind(
        &ContentSubresourceFilterThrottleManager::MaybeCallFirstDisallowedLoad,
        weak_ptr_factory_.GetWeakPtr()));
    activated_frame_hosts_[frame_host] = std::move(filter);
  } else {
    activated_frame_hosts_.erase(frame_host);
  }
  DestroyRulesetHandleIfNoLongerUsed();
}

void ContentSubresourceFilterThrottleManager::DidFinishLoad(
    content::RenderFrameHost* render_frame_host,
    const GURL& validated_url) {
  if (!statistics_ || render_frame_host->GetParent())
    return;
  statistics_->OnDidFinishLoad();
}

bool ContentSubresourceFilterThrottleManager::OnMessageReceived(
    const IPC::Message& message,
    content::RenderFrameHost* render_frame_host) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ContentSubresourceFilterThrottleManager, message)
    IPC_MESSAGE_HANDLER(SubresourceFilterHostMsg_DidDisallowFirstSubresource,
                        MaybeCallFirstDisallowedLoad)
    IPC_MESSAGE_HANDLER(SubresourceFilterHostMsg_DocumentLoadStatistics,
                        OnDocumentLoadStatistics)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void ContentSubresourceFilterThrottleManager::MaybeAppendNavigationThrottles(
    content::NavigationHandle* navigation_handle,
    std::vector<std::unique_ptr<content::NavigationThrottle>>* throttles) {
  DCHECK(!navigation_handle->IsSameDocument());
  if (!dealer_handle_)
    return;
  if (auto filtering_throttle =
          MaybeCreateSubframeNavigationFilteringThrottle(navigation_handle)) {
    throttles->push_back(std::move(filtering_throttle));
  }
  if (auto activation_throttle =
          MaybeCreateActivationStateComputingThrottle(navigation_handle)) {
    ongoing_activation_throttles_[navigation_handle] =
        activation_throttle.get();
    throttles->push_back(std::move(activation_throttle));
  }
}

bool ContentSubresourceFilterThrottleManager::ShouldDisallowNewWindow() {
  auto it = activated_frame_hosts_.find(web_contents()->GetMainFrame());
  if (it == activated_frame_hosts_.end())
    return false;
  const ActivationState state = it->second->activation_state();
  // This should trigger the standard popup blocking UI, so don't force the
  // subresource filter specific UI here.
  return state.activation_level == ActivationLevel::ENABLED &&
         !state.filtering_disabled_for_document &&
         !state.generic_blocking_rules_disabled;
}

std::unique_ptr<SubframeNavigationFilteringThrottle>
ContentSubresourceFilterThrottleManager::
    MaybeCreateSubframeNavigationFilteringThrottle(
        content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInMainFrame())
    return nullptr;
  AsyncDocumentSubresourceFilter* parent_filter =
      GetParentFrameFilter(navigation_handle);
  return parent_filter ? base::MakeUnique<SubframeNavigationFilteringThrottle>(
                             navigation_handle, parent_filter)
                       : nullptr;
}

std::unique_ptr<ActivationStateComputingNavigationThrottle>
ContentSubresourceFilterThrottleManager::
    MaybeCreateActivationStateComputingThrottle(
        content::NavigationHandle* navigation_handle) {
  // Main frames: create unconditionally.
  if (navigation_handle->IsInMainFrame()) {
    return ActivationStateComputingNavigationThrottle::CreateForMainFrame(
        navigation_handle);
  }

  // Subframes: create only for frames with activated parents.
  AsyncDocumentSubresourceFilter* parent_filter =
      GetParentFrameFilter(navigation_handle);
  if (!parent_filter)
    return nullptr;
  DCHECK(ruleset_handle_);
  return ActivationStateComputingNavigationThrottle::CreateForSubframe(
      navigation_handle, ruleset_handle_.get(),
      parent_filter->activation_state());
}

AsyncDocumentSubresourceFilter*
ContentSubresourceFilterThrottleManager::GetParentFrameFilter(
    content::NavigationHandle* child_frame_navigation) {
  DCHECK(!child_frame_navigation->IsInMainFrame());
  content::RenderFrameHost* parent = child_frame_navigation->GetParentFrame();
  DCHECK(parent);
  auto it = activated_frame_hosts_.find(parent);
  return it == activated_frame_hosts_.end() ? nullptr : it->second.get();
}

void ContentSubresourceFilterThrottleManager::MaybeCallFirstDisallowedLoad() {
  if (current_committed_load_has_notified_disallowed_load_)
    return;
  delegate_->OnFirstSubresourceLoadDisallowed();
  current_committed_load_has_notified_disallowed_load_ = true;
}

VerifiedRuleset::Handle*
ContentSubresourceFilterThrottleManager::EnsureRulesetHandle() {
  if (!ruleset_handle_)
    ruleset_handle_ = base::MakeUnique<VerifiedRuleset::Handle>(dealer_handle_);
  return ruleset_handle_.get();
}

void ContentSubresourceFilterThrottleManager::
    DestroyRulesetHandleIfNoLongerUsed() {
  if (activated_frame_hosts_.size() + ongoing_activation_throttles_.size() ==
      0u) {
    ruleset_handle_.reset();
  }
}

void ContentSubresourceFilterThrottleManager::OnDocumentLoadStatistics(
    const DocumentLoadStatistics& statistics) {
  if (statistics_)
    statistics_->OnDocumentLoadStatistics(statistics);
}

}  // namespace subresource_filter
