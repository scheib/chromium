// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/frame_host/navigation_request.h"

#include <utility>

#include "base/memory/ptr_util.h"
#include "content/browser/appcache/appcache_navigation_handle.h"
#include "content/browser/appcache/chrome_appcache_service.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/devtools/render_frame_devtools_agent_host.h"
#include "content/browser/frame_host/debug_urls.h"
#include "content/browser/frame_host/frame_tree.h"
#include "content/browser/frame_host/frame_tree_node.h"
#include "content/browser/frame_host/navigation_controller_impl.h"
#include "content/browser/frame_host/navigation_handle_impl.h"
#include "content/browser/frame_host/navigation_request_info.h"
#include "content/browser/frame_host/navigator.h"
#include "content/browser/frame_host/navigator_impl.h"
#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/browser/loader/navigation_url_loader.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/service_worker/service_worker_context_wrapper.h"
#include "content/browser/service_worker/service_worker_navigation_handle.h"
#include "content/browser/site_instance_impl.h"
#include "content/common/appcache_interfaces.h"
#include "content/common/resource_request_body_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_data.h"
#include "content/public/browser/navigation_ui_data.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/stream_handle.h"
#include "content/public/common/appcache_info.h"
#include "content/public/common/content_client.h"
#include "content/public/common/origin_util.h"
#include "content/public/common/request_context_type.h"
#include "content/public/common/resource_response.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/web_preferences.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "net/http/http_request_headers.h"
#include "net/url_request/redirect_info.h"
#include "third_party/WebKit/public/platform/WebMixedContentContextType.h"
#include "third_party/WebKit/public/web/WebSandboxFlags.h"

namespace content {

namespace {

// Returns the net load flags to use based on the navigation type.
// TODO(clamy): Remove the blink code that sets the caching flags when
// PlzNavigate launches.
void UpdateLoadFlagsWithCacheFlags(
    int* load_flags,
    FrameMsg_Navigate_Type::Value navigation_type,
    bool is_post) {
  switch (navigation_type) {
    case FrameMsg_Navigate_Type::RELOAD:
    case FrameMsg_Navigate_Type::RELOAD_ORIGINAL_REQUEST_URL:
      *load_flags |= net::LOAD_VALIDATE_CACHE;
      break;
    case FrameMsg_Navigate_Type::RELOAD_BYPASSING_CACHE:
      *load_flags |= net::LOAD_BYPASS_CACHE;
      break;
    case FrameMsg_Navigate_Type::RESTORE:
      *load_flags |= net::LOAD_SKIP_CACHE_VALIDATION;
      break;
    case FrameMsg_Navigate_Type::RESTORE_WITH_POST:
      *load_flags |=
          net::LOAD_ONLY_FROM_CACHE | net::LOAD_SKIP_CACHE_VALIDATION;
      break;
    case FrameMsg_Navigate_Type::SAME_DOCUMENT:
    case FrameMsg_Navigate_Type::DIFFERENT_DOCUMENT:
      if (is_post)
        *load_flags |= net::LOAD_VALIDATE_CACHE;
      break;
    case FrameMsg_Navigate_Type::HISTORY_SAME_DOCUMENT:
    case FrameMsg_Navigate_Type::HISTORY_DIFFERENT_DOCUMENT:
      if (is_post) {
        *load_flags |=
            net::LOAD_ONLY_FROM_CACHE | net::LOAD_SKIP_CACHE_VALIDATION;
      } else {
        *load_flags |= net::LOAD_SKIP_CACHE_VALIDATION;
      }
      break;
  }
}

// TODO(clamy): This should be function in FrameTreeNode.
bool IsSecureFrame(FrameTreeNode* frame) {
  while (frame) {
    if (!IsPotentiallyTrustworthyOrigin(frame->current_origin()))
      return false;
    frame = frame->parent();
  }
  return true;
}

// This should match blink::ResourceRequest::needsHTTPOrigin.
bool NeedsHTTPOrigin(net::HttpRequestHeaders* headers,
                     const std::string& method) {
  // Don't add an Origin header if it is already present.
  if (headers->HasHeader(net::HttpRequestHeaders::kOrigin))
    return false;

  // Don't send an Origin header for GET or HEAD to avoid privacy issues.
  // For example, if an intranet page has a hyperlink to an external web
  // site, we don't want to include the Origin of the request because it
  // will leak the internal host name. Similar privacy concerns have lead
  // to the widespread suppression of the Referer header at the network
  // layer.
  if (method == "GET" || method == "HEAD")
    return false;

  // For non-GET and non-HEAD methods, always send an Origin header so the
  // server knows we support this feature.
  return true;
}

// TODO(clamy): This should match what's happening in
// blink::FrameFetchContext::addAdditionalRequestHeaders.
void AddAdditionalRequestHeaders(net::HttpRequestHeaders* headers,
                                 const GURL& url,
                                 FrameMsg_Navigate_Type::Value navigation_type,
                                 BrowserContext* browser_context,
                                 const std::string& method,
                                 const std::string user_agent_override,
                                 FrameTreeNode* frame_tree_node) {
  if (!url.SchemeIsHTTPOrHTTPS())
    return;

  bool is_reload =
      navigation_type == FrameMsg_Navigate_Type::RELOAD ||
      navigation_type == FrameMsg_Navigate_Type::RELOAD_BYPASSING_CACHE ||
      navigation_type == FrameMsg_Navigate_Type::RELOAD_ORIGINAL_REQUEST_URL;
  if (is_reload)
    headers->RemoveHeader("Save-Data");

  if (GetContentClient()->browser()->IsDataSaverEnabled(browser_context))
    headers->SetHeaderIfMissing("Save-Data", "on");

  headers->SetHeaderIfMissing(net::HttpRequestHeaders::kUserAgent,
                              user_agent_override.empty()
                                  ? GetContentClient()->GetUserAgent()
                                  : user_agent_override);

  // Check whether DevTools wants to override user agent for this request
  // after setting the default user agent.
  std::string devtools_user_agent =
      RenderFrameDevToolsAgentHost::UserAgentOverride(frame_tree_node);
  if (!devtools_user_agent.empty()) {
    headers->SetHeader(net::HttpRequestHeaders::kUserAgent,
                       devtools_user_agent);
  }

  // Tack an 'Upgrade-Insecure-Requests' header to outgoing navigational
  // requests, as described in
  // https://w3c.github.io/webappsec/specs/upgrade/#feature-detect
  headers->AddHeaderFromString("Upgrade-Insecure-Requests: 1");

  // Next, set the HTTP Origin if needed.
  if (!NeedsHTTPOrigin(headers, method))
    return;

  // Create a unique origin.
  url::Origin origin;
  if (frame_tree_node->IsMainFrame()) {
    // For main frame, the origin is the url currently loading.
    origin = url::Origin(url);
  } else if ((frame_tree_node->effective_sandbox_flags() &
              blink::WebSandboxFlags::kOrigin) ==
             blink::WebSandboxFlags::kNone) {
    // The origin should be the origin of the root, except for sandboxed
    // frames which have a unique origin.
    origin = frame_tree_node->frame_tree()->root()->current_origin();
  }

  headers->SetHeader(net::HttpRequestHeaders::kOrigin, origin.Serialize());
}

}  // namespace

// static
std::unique_ptr<NavigationRequest> NavigationRequest::CreateBrowserInitiated(
    FrameTreeNode* frame_tree_node,
    const GURL& dest_url,
    const Referrer& dest_referrer,
    const FrameNavigationEntry& frame_entry,
    const NavigationEntryImpl& entry,
    FrameMsg_Navigate_Type::Value navigation_type,
    PreviewsState previews_state,
    bool is_same_document_history_load,
    bool is_history_navigation_in_new_child,
    const scoped_refptr<ResourceRequestBodyImpl>& post_body,
    const base::TimeTicks& navigation_start,
    NavigationControllerImpl* controller) {
  // A form submission happens either because the navigation is a
  // renderer-initiated form submission that took the OpenURL path or a
  // back/forward/reload navigation the does a form resubmission.
  scoped_refptr<ResourceRequestBodyImpl> request_body;
  if (post_body) {
    // Standard form submission from the renderer.
    request_body = post_body;
  } else if (frame_entry.method() == "POST") {
    // Form resubmission during a back/forward/reload navigation.
    request_body = frame_entry.GetPostData();
  }
  // TODO(arthursonzogni): Form submission with the "GET" method is possible.
  // This is not currently handled here.
  bool is_form_submission = !!request_body;

  base::Optional<url::Origin> initiator =
      frame_tree_node->IsMainFrame()
          ? base::Optional<url::Origin>()
          : base::Optional<url::Origin>(
                frame_tree_node->frame_tree()->root()->current_origin());

  // While the navigation was started via the LoadURL path it may have come from
  // the renderer in the first place as part of OpenURL.
  bool browser_initiated = !entry.is_renderer_initiated();

  CommonNavigationParams common_params = entry.ConstructCommonNavigationParams(
      frame_entry, request_body, dest_url, dest_referrer, navigation_type,
      previews_state, navigation_start);

  std::unique_ptr<NavigationRequest> navigation_request(new NavigationRequest(
      frame_tree_node, common_params,
      BeginNavigationParams(entry.extra_headers(), net::LOAD_NORMAL,
                            false,  // has_user_gestures
                            false,  // skip_service_worker
                            REQUEST_CONTEXT_TYPE_LOCATION,
                            blink::WebMixedContentContextType::kBlockable,
                            is_form_submission, initiator),
      entry.ConstructRequestNavigationParams(
          frame_entry, common_params.url, common_params.method,
          is_history_navigation_in_new_child,
          entry.GetSubframeUniqueNames(frame_tree_node),
          frame_tree_node->has_committed_real_load(),
          controller->GetPendingEntryIndex() == -1,
          controller->GetIndexOfEntry(&entry),
          controller->GetLastCommittedEntryIndex(),
          controller->GetEntryCount()),
      browser_initiated,
      true,  // may_transfer
      &frame_entry, &entry));
  return navigation_request;
}

// static
std::unique_ptr<NavigationRequest> NavigationRequest::CreateRendererInitiated(
    FrameTreeNode* frame_tree_node,
    const CommonNavigationParams& common_params,
    const BeginNavigationParams& begin_params,
    int current_history_list_offset,
    int current_history_list_length) {
  // Only normal navigations to a different document or reloads are expected.
  // - Renderer-initiated fragment-navigations never take place in the browser,
  //   even with PlzNavigate.
  // - Restore-navigations are always browser-initiated.
  // - History-navigations use the browser-initiated path, event the ones that
  //   are initiated by a javascript script, please see the IPC message
  //   ViewHostMsg_GoToEntryAtOffset.
  DCHECK(FrameMsg_Navigate_Type::IsReload(common_params.navigation_type) ||
         common_params.navigation_type ==
             FrameMsg_Navigate_Type::DIFFERENT_DOCUMENT);

  // TODO(clamy): See how we should handle override of the user agent when the
  // navigation may start in a renderer and commit in another one.
  // TODO(clamy): See if the navigation start time should be measured in the
  // renderer and sent to the browser instead of being measured here.
  // TODO(clamy): The pending history list offset should be properly set.
  RequestNavigationParams request_params(
      false,                // is_overriding_user_agent
      std::vector<GURL>(),  // redirects
      common_params.url, common_params.method,
      false,                          // can_load_local_resources
      PageState(),                    // page_state
      0,                              // nav_entry_id
      false,                          // is_history_navigation_in_new_child
      std::map<std::string, bool>(),  // subframe_unique_names
      frame_tree_node->has_committed_real_load(),
      false,  // intended_as_new_entry
      -1,     // pending_history_list_offset
      current_history_list_offset, current_history_list_length,
      false,  // is_view_source
      false,  // should_clear_history_list
      begin_params.has_user_gesture);
  std::unique_ptr<NavigationRequest> navigation_request(new NavigationRequest(
      frame_tree_node, common_params, begin_params, request_params,
      false,  // browser_initiated
      false,  // may_transfer
      nullptr, nullptr));
  return navigation_request;
}

NavigationRequest::NavigationRequest(
    FrameTreeNode* frame_tree_node,
    const CommonNavigationParams& common_params,
    const BeginNavigationParams& begin_params,
    const RequestNavigationParams& request_params,
    bool browser_initiated,
    bool may_transfer,
    const FrameNavigationEntry* frame_entry,
    const NavigationEntryImpl* entry)
    : frame_tree_node_(frame_tree_node),
      common_params_(common_params),
      begin_params_(begin_params),
      request_params_(request_params),
      browser_initiated_(browser_initiated),
      state_(NOT_STARTED),
      restore_type_(RestoreType::NONE),
      is_view_source_(false),
      bindings_(NavigationEntryImpl::kInvalidBindings),
      response_should_be_rendered_(true),
      associated_site_instance_type_(AssociatedSiteInstanceType::NONE),
      may_transfer_(may_transfer) {
  DCHECK(!browser_initiated || (entry != nullptr && frame_entry != nullptr));
  TRACE_EVENT_ASYNC_BEGIN2("navigation", "NavigationRequest", this,
                           "frame_tree_node",
                           frame_tree_node_->frame_tree_node_id(), "url",
                           common_params_.url.possibly_invalid_spec());

  // Sanitize the referrer.
  common_params_.referrer =
      Referrer::SanitizeForRequest(common_params_.url, common_params_.referrer);

  if (may_transfer) {
    FrameNavigationEntry* frame_entry = entry->GetFrameEntry(frame_tree_node);
    if (frame_entry) {
      source_site_instance_ = frame_entry->source_site_instance();
      dest_site_instance_ = frame_entry->site_instance();
    }

    restore_type_ = entry->restore_type();
    is_view_source_ = entry->IsViewSourceMode();
    bindings_ = entry->bindings();
  } else {
    // This is needed to have about:blank and data URLs commit in the same
    // SiteInstance as the initiating renderer.
    source_site_instance_ =
        frame_tree_node->current_frame_host()->GetSiteInstance();
  }

  // Update the load flags with cache information.
  UpdateLoadFlagsWithCacheFlags(&begin_params_.load_flags,
                                common_params_.navigation_type,
                                common_params_.method == "POST");

  // Add necessary headers that may not be present in the BeginNavigationParams.
  std::string user_agent_override;
  if (entry && entry->GetIsOverridingUserAgent()) {
    user_agent_override =
        frame_tree_node_->navigator()->GetDelegate()->GetUserAgentOverride();
  }

  net::HttpRequestHeaders headers;
  headers.AddHeadersFromString(begin_params_.headers);
  AddAdditionalRequestHeaders(
      &headers, common_params_.url, common_params_.navigation_type,
      frame_tree_node_->navigator()->GetController()->GetBrowserContext(),
      common_params.method, user_agent_override, frame_tree_node);
  begin_params_.headers = headers.ToString();
}

NavigationRequest::~NavigationRequest() {
  TRACE_EVENT_ASYNC_END0("navigation", "NavigationRequest", this);
}

void NavigationRequest::BeginNavigation() {
  DCHECK(!loader_);
  DCHECK(state_ == NOT_STARTED || state_ == WAITING_FOR_RENDERER_RESPONSE);
  TRACE_EVENT_ASYNC_STEP_INTO0("navigation", "NavigationRequest", this,
                               "BeginNavigation");
  state_ = STARTED;
  RenderFrameDevToolsAgentHost::OnBeforeNavigation(navigation_handle_.get());

  if (ShouldMakeNetworkRequestForURL(common_params_.url) &&
      !navigation_handle_->IsSameDocument()) {
    // It's safe to use base::Unretained because this NavigationRequest owns
    // the NavigationHandle where the callback will be stored.
    // TODO(clamy): pass the method to the NavigationHandle instead of a
    // boolean.
    bool is_external_protocol =
        !GetContentClient()->browser()->IsHandledURL(common_params_.url);
    navigation_handle_->WillStartRequest(
        common_params_.method, common_params_.post_data,
        Referrer::SanitizeForRequest(common_params_.url,
                                     common_params_.referrer),
        begin_params_.has_user_gesture, common_params_.transition,
        is_external_protocol, begin_params_.request_context_type,
        begin_params_.mixed_content_context_type,
        base::Bind(&NavigationRequest::OnStartChecksComplete,
                   base::Unretained(this)));
    return;
  }

  // There is no need to make a network request for this navigation, so commit
  // it immediately.
  TRACE_EVENT_ASYNC_STEP_INTO0("navigation", "NavigationRequest", this,
                               "ResponseStarted");
  state_ = RESPONSE_STARTED;

  // Select an appropriate RenderFrameHost.
  RenderFrameHostImpl* render_frame_host =
      frame_tree_node_->render_manager()->GetFrameHostForNavigation(*this);
  NavigatorImpl::CheckWebUIRendererDoesNotDisplayNormalURL(render_frame_host,
                                                           common_params_.url);

  // Inform the NavigationHandle that the navigation will commit.
  navigation_handle_->ReadyToCommitNavigation(render_frame_host);

  CommitNavigation();
}

void NavigationRequest::SetWaitingForRendererResponse() {
  TRACE_EVENT_ASYNC_STEP_INTO0("navigation", "NavigationRequest", this,
                               "WaitingForRendererResponse");
  DCHECK(state_ == NOT_STARTED);
  state_ = WAITING_FOR_RENDERER_RESPONSE;
}

void NavigationRequest::CreateNavigationHandle(int pending_nav_entry_id) {
  DCHECK_EQ(frame_tree_node_->navigation_request(), this);
  FrameTreeNode* frame_tree_node = frame_tree_node_;

  std::vector<GURL> redirect_chain;
  if (!begin_params_.client_side_redirect_url.is_empty())
    redirect_chain.push_back(begin_params_.client_side_redirect_url);
  redirect_chain.push_back(common_params_.url);

  std::unique_ptr<NavigationHandleImpl> navigation_handle =
      NavigationHandleImpl::Create(common_params_.url, redirect_chain,
                                   frame_tree_node_, !browser_initiated_,
                                   FrameMsg_Navigate_Type::IsSameDocument(
                                       common_params_.navigation_type),
                                   common_params_.navigation_start,
                                   pending_nav_entry_id,
                                   false,  // started_in_context_menu
                                   common_params_.should_check_main_world_csp,
                                   begin_params_.is_form_submission);

  if (!frame_tree_node->navigation_request()) {
    // A callback could have cancelled this request synchronously in which case
    // |this| is deleted.
    return;
  }

  navigation_handle_ = std::move(navigation_handle);

  if (!begin_params_.searchable_form_url.is_empty()) {
    navigation_handle_->set_searchable_form_url(
        begin_params_.searchable_form_url);
    navigation_handle_->set_searchable_form_encoding(
        begin_params_.searchable_form_encoding);
  }

  if (common_params_.source_location) {
    navigation_handle_->set_source_location(
        common_params_.source_location.value());
  }
}

void NavigationRequest::TransferNavigationHandleOwnership(
    RenderFrameHostImpl* render_frame_host) {
  render_frame_host->SetNavigationHandle(std::move(navigation_handle_));
}

void NavigationRequest::OnRequestRedirected(
    const net::RedirectInfo& redirect_info,
    const scoped_refptr<ResourceResponse>& response) {
  // If a redirect occurs, the original site instance we thought is the
  // destination could change.
  dest_site_instance_ = nullptr;

  // If the navigation is no longer a POST, the POST data should be reset.
  if (redirect_info.new_method != "POST")
    common_params_.post_data = nullptr;

  // Mark time for the Navigation Timing API.
  if (request_params_.navigation_timing.redirect_start.is_null()) {
    request_params_.navigation_timing.redirect_start =
        request_params_.navigation_timing.fetch_start;
  }
  request_params_.navigation_timing.redirect_end = base::TimeTicks::Now();
  request_params_.navigation_timing.fetch_start = base::TimeTicks::Now();

  request_params_.redirect_response.push_back(response->head);
  request_params_.redirect_infos.push_back(redirect_info);

  request_params_.redirects.push_back(common_params_.url);
  common_params_.url = redirect_info.new_url;
  common_params_.method = redirect_info.new_method;
  common_params_.referrer.url = GURL(redirect_info.new_referrer);
  common_params_.referrer =
      Referrer::SanitizeForRequest(common_params_.url, common_params_.referrer);

  // For non browser initiated navigations we need to check if the source has
  // access to the URL. We always allow browser initiated requests.
  // TODO(clamy): Kill the renderer if FilterURL fails?
  GURL url = common_params_.url;
  if (!browser_initiated_ && source_site_instance()) {
    source_site_instance()->GetProcess()->FilterURL(false, &url);
    // FilterURL sets the URL to about:blank if the CSP checks prevent the
    // renderer from accessing it.
    if ((url == url::kAboutBlankURL) && (url != common_params_.url)) {
      navigation_handle_->set_net_error_code(net::ERR_ABORTED);
      frame_tree_node_->ResetNavigationRequest(false, true);
      return;
    }
  }

  // It's safe to use base::Unretained because this NavigationRequest owns the
  // NavigationHandle where the callback will be stored.
  bool is_external_protocol =
      !GetContentClient()->browser()->IsHandledURL(common_params_.url);
  navigation_handle_->WillRedirectRequest(
      common_params_.url, common_params_.method, common_params_.referrer.url,
      is_external_protocol, response->head.headers,
      response->head.connection_info,
      base::Bind(&NavigationRequest::OnRedirectChecksComplete,
                 base::Unretained(this)));
}

void NavigationRequest::OnResponseStarted(
    const scoped_refptr<ResourceResponse>& response,
    std::unique_ptr<StreamHandle> body,
    mojo::ScopedDataPipeConsumerHandle consumer_handle,
    const SSLStatus& ssl_status,
    std::unique_ptr<NavigationData> navigation_data,
    const GlobalRequestID& request_id,
    bool is_download,
    bool is_stream) {
  DCHECK(state_ == STARTED);
  DCHECK(response);
  TRACE_EVENT_ASYNC_STEP_INTO0("navigation", "NavigationRequest", this,
                               "OnResponseStarted");
  state_ = RESPONSE_STARTED;

  // Check if the response should be sent to a renderer.
  response_should_be_rendered_ =
      !is_download && (!response->head.headers.get() ||
                       (response->head.headers->response_code() != 204 &&
                        response->head.headers->response_code() != 205));

  // Response that will not commit should be marked as aborted in the
  // NavigationHandle.
  if (!response_should_be_rendered_)
    navigation_handle_->set_net_error_code(net::ERR_ABORTED);

  // Update the service worker params of the request params.
  bool did_create_service_worker_host =
      navigation_handle_->service_worker_handle() &&
      navigation_handle_->service_worker_handle()
              ->service_worker_provider_host_id() !=
          kInvalidServiceWorkerProviderId;
  request_params_.service_worker_provider_id =
      did_create_service_worker_host
          ? navigation_handle_->service_worker_handle()
                ->service_worker_provider_host_id()
          : kInvalidServiceWorkerProviderId;

  request_params_.appcache_host_id =
      navigation_handle_->appcache_handle()
          ? navigation_handle_->appcache_handle()->appcache_host_id()
          : kAppCacheNoHostId;

  // Update the previews state of the request.
  common_params_.previews_state = response->head.previews_state;

  // Select an appropriate renderer to commit the navigation.
  RenderFrameHostImpl* render_frame_host = nullptr;
  if (response_should_be_rendered_) {
    render_frame_host =
        frame_tree_node_->render_manager()->GetFrameHostForNavigation(*this);
    NavigatorImpl::CheckWebUIRendererDoesNotDisplayNormalURL(
        render_frame_host, common_params_.url);
  }
  DCHECK(render_frame_host || !response_should_be_rendered_);

  if (!browser_initiated_ && render_frame_host &&
      render_frame_host != frame_tree_node_->current_frame_host()) {
    // Reset the source location information if the navigation will not commit
    // in the current renderer process. This information originated in another
    // process (the current one), it should not be transferred to the new one.
    common_params_.source_location.reset();

    // Allow the embedder to cancel the cross-process commit if needed.
    // TODO(clamy): Rename ShouldTransferNavigation once PlzNavigate ships.
    if (!frame_tree_node_->navigator()->GetDelegate()->ShouldTransferNavigation(
            frame_tree_node_->IsMainFrame())) {
      navigation_handle_->set_net_error_code(net::ERR_ABORTED);
      frame_tree_node_->ResetNavigationRequest(false, true);
      return;
    }
  }

  if (navigation_data)
    navigation_handle_->set_navigation_data(std::move(navigation_data));

  // Store the response and the StreamHandle until checks have been processed.
  response_ = response;
  body_ = std::move(body);
  handle_ = std::move(consumer_handle);

  // Check if the navigation should be allowed to proceed.
  navigation_handle_->WillProcessResponse(
      render_frame_host, response->head.headers.get(),
      response->head.connection_info, ssl_status, request_id,
      common_params_.should_replace_current_entry, is_download, is_stream,
      base::Closure(),
      base::Bind(&NavigationRequest::OnWillProcessResponseChecksComplete,
                 base::Unretained(this)));
}

void NavigationRequest::OnRequestFailed(bool has_stale_copy_in_cache,
                                        int net_error) {
  DCHECK(state_ == STARTED || state_ == RESPONSE_STARTED);
  TRACE_EVENT_ASYNC_STEP_INTO1("navigation", "NavigationRequest", this,
                               "OnRequestFailed", "error", net_error);
  state_ = FAILED;
  navigation_handle_->set_net_error_code(static_cast<net::Error>(net_error));

  // With PlzNavigate, debug URLs will give a failed navigation because the
  // WebUI backend won't find a handler for them. They will be processed in the
  // renderer, however do not discard the pending entry so that the URL bar
  // shows them correctly.
  if (!IsRendererDebugURL(common_params_.url)) {
    frame_tree_node_->navigator()->DiscardPendingEntryIfNeeded(
        navigation_handle_.get());
  }

  // If the request was canceled by the user do not show an error page.
  if (net_error == net::ERR_ABORTED) {
    frame_tree_node_->ResetNavigationRequest(false, true);
    return;
  }

  // Decide whether to leave the error page in the original process.
  // * If this was a renderer-initiated navigation, and the request is blocked
  //   because the initiating document wasn't allowed to make the request,
  //   commit the error in the existing process. This is a strategy to to avoid
  //   creating a process for the destination, which may belong to an origin
  //   with a higher privilege level.
  // * Error pages resulting from errors like network outage, no network, or DNS
  //   error can reasonably expect that a reload at a later point in time would
  //   work. These should be allowed to transfer away from the current process:
  //   they do belong to whichever process that will host the destination URL,
  //   as a reload will end up committing in that process anyway.
  // * Error pages that arise during browser-initiated navigations to blocked
  //   URLs should be allowed to transfer away from the current process, which
  //   didn't request the navigation and may have a higher privilege level than
  //   the blocked destination.
  RenderFrameHostImpl* render_frame_host = nullptr;
  if (net_error == net::ERR_BLOCKED_BY_CLIENT && !browser_initiated()) {
    render_frame_host = frame_tree_node_->current_frame_host();
  } else {
    render_frame_host =
        frame_tree_node_->render_manager()->GetFrameHostForNavigation(*this);
  }

  // Don't ask the renderer to commit an URL if the browser will kill it when
  // it does.
  DCHECK(render_frame_host->CanCommitURL(common_params_.url));

  NavigatorImpl::CheckWebUIRendererDoesNotDisplayNormalURL(render_frame_host,
                                                           common_params_.url);

  TransferNavigationHandleOwnership(render_frame_host);
  render_frame_host->navigation_handle()->ReadyToCommitNavigation(
      render_frame_host);
  render_frame_host->FailedNavigation(common_params_, begin_params_,
                                      request_params_, has_stale_copy_in_cache,
                                      net_error);
}

void NavigationRequest::OnRequestStarted(base::TimeTicks timestamp) {
  if (frame_tree_node_->IsMainFrame()) {
    TRACE_EVENT_ASYNC_END_WITH_TIMESTAMP0(
        "navigation", "Navigation timeToNetworkStack", navigation_handle_.get(),
        timestamp);
  }

  frame_tree_node_->navigator()->LogResourceRequestTime(timestamp,
                                                        common_params_.url);
}

void NavigationRequest::OnStartChecksComplete(
    NavigationThrottle::ThrottleCheckResult result) {
  DCHECK(result != NavigationThrottle::DEFER);
  DCHECK(result != NavigationThrottle::BLOCK_RESPONSE);

  if (on_start_checks_complete_closure_)
    on_start_checks_complete_closure_.Run();

  // Abort the request if needed. This will destroy the NavigationRequest.
  if (result == NavigationThrottle::CANCEL_AND_IGNORE ||
      result == NavigationThrottle::CANCEL) {
    // TODO(clamy): distinguish between CANCEL and CANCEL_AND_IGNORE.
    OnRequestFailed(false, net::ERR_ABORTED);

    // DO NOT ADD CODE after this. The previous call to OnRequestFailed has
    // destroyed the NavigationRequest.
    return;
  }

  if (result == NavigationThrottle::BLOCK_REQUEST) {
    OnRequestFailed(false, net::ERR_BLOCKED_BY_CLIENT);

    // DO NOT ADD CODE after this. The previous call to OnRequestFailed has
    // destroyed the NavigationRequest.
    return;
  }

  // Use the SiteInstance of the navigating RenderFrameHost to get access to
  // the StoragePartition. Using the url of the navigation will result in a
  // wrong StoragePartition being picked when a WebView is navigating.
  DCHECK_NE(AssociatedSiteInstanceType::NONE, associated_site_instance_type_);
  RenderFrameHostImpl* navigating_frame_host =
      associated_site_instance_type_ == AssociatedSiteInstanceType::SPECULATIVE
          ? frame_tree_node_->render_manager()->speculative_frame_host()
          : frame_tree_node_->current_frame_host();
  DCHECK(navigating_frame_host);

  BrowserContext* browser_context =
      frame_tree_node_->navigator()->GetController()->GetBrowserContext();
  StoragePartition* partition = BrowserContext::GetStoragePartition(
      browser_context, navigating_frame_host->GetSiteInstance());
  DCHECK(partition);

  // Only initialize the ServiceWorkerNavigationHandle if it can be created for
  // this frame.
  bool can_create_service_worker =
      (frame_tree_node_->pending_sandbox_flags() &
       blink::WebSandboxFlags::kOrigin) != blink::WebSandboxFlags::kOrigin;
  request_params_.should_create_service_worker = can_create_service_worker;
  if (can_create_service_worker) {
    ServiceWorkerContextWrapper* service_worker_context =
        static_cast<ServiceWorkerContextWrapper*>(
            partition->GetServiceWorkerContext());
    navigation_handle_->InitServiceWorkerHandle(service_worker_context);
  }

  if (IsSchemeSupportedForAppCache(common_params_.url)) {
    if (navigating_frame_host->GetRenderViewHost()
            ->GetWebkitPreferences()
            .application_cache_enabled) {
      navigation_handle_->InitAppCacheHandle(
          static_cast<ChromeAppCacheService*>(partition->GetAppCacheService()));
    }
  }

  // Mark the fetch_start (Navigation Timing API).
  request_params_.navigation_timing.fetch_start = base::TimeTicks::Now();

  // TODO(mkwst): This is incorrect. It ought to use the definition from
  // 'Document::firstPartyForCookies()' in Blink, which walks the ancestor tree
  // and verifies that all origins are PSL-matches (and special-cases extension
  // URLs).
  const GURL& first_party_for_cookies =
      frame_tree_node_->IsMainFrame()
          ? common_params_.url
          : frame_tree_node_->frame_tree()->root()->current_url();
  bool parent_is_main_frame = !frame_tree_node_->parent()
                                  ? false
                                  : frame_tree_node_->parent()->IsMainFrame();

  std::unique_ptr<NavigationUIData> navigation_ui_data;
  if (navigation_handle_->navigation_ui_data())
    navigation_ui_data = navigation_handle_->navigation_ui_data()->Clone();

  bool is_for_guests_only =
      navigation_handle_->GetStartingSiteInstance()->GetSiteURL().
          SchemeIs(kGuestScheme);

  bool report_raw_headers =
      RenderFrameDevToolsAgentHost::IsNetworkHandlerEnabled(frame_tree_node_);

  loader_ = NavigationURLLoader::Create(
      browser_context->GetResourceContext(), partition,
      base::MakeUnique<NavigationRequestInfo>(
          common_params_, begin_params_, first_party_for_cookies,
          frame_tree_node_->IsMainFrame(), parent_is_main_frame,
          IsSecureFrame(frame_tree_node_->parent()),
          frame_tree_node_->frame_tree_node_id(), is_for_guests_only,
          report_raw_headers, navigating_frame_host->GetVisibilityState()),
      std::move(navigation_ui_data),
      navigation_handle_->service_worker_handle(),
      navigation_handle_->appcache_handle(), this);
}

void NavigationRequest::OnRedirectChecksComplete(
    NavigationThrottle::ThrottleCheckResult result) {
  DCHECK(result != NavigationThrottle::DEFER);
  DCHECK(result != NavigationThrottle::BLOCK_RESPONSE);

  // Abort the request if needed. This will destroy the NavigationRequest.
  if (result == NavigationThrottle::CANCEL_AND_IGNORE ||
      result == NavigationThrottle::CANCEL) {
    // TODO(clamy): distinguish between CANCEL and CANCEL_AND_IGNORE.
    OnRequestFailed(false, net::ERR_ABORTED);

    // DO NOT ADD CODE after this. The previous call to OnRequestFailed has
    // destroyed the NavigationRequest.
    return;
  }

  if (result == NavigationThrottle::BLOCK_REQUEST) {
    OnRequestFailed(false, net::ERR_BLOCKED_BY_CLIENT);

    // DO NOT ADD CODE after this. The previous call to OnRequestFailed has
    // destroyed the NavigationRequest.
    return;
  }

  loader_->FollowRedirect();
}

void NavigationRequest::OnWillProcessResponseChecksComplete(
    NavigationThrottle::ThrottleCheckResult result) {
  DCHECK(result != NavigationThrottle::DEFER);

  // If the NavigationThrottles allowed the navigation to continue, have the
  // processing of the response resume in the network stack.
  if (result == NavigationThrottle::PROCEED)
    loader_->ProceedWithResponse();

  // Abort the request if needed. This includes requests that were blocked by
  // NavigationThrottles and requests that should not commit (e.g. downloads,
  // 204/205s). This will destroy the NavigationRequest.
  if (result == NavigationThrottle::CANCEL_AND_IGNORE ||
      result == NavigationThrottle::CANCEL || !response_should_be_rendered_) {
    // TODO(clamy): distinguish between CANCEL and CANCEL_AND_IGNORE.
    OnRequestFailed(false, net::ERR_ABORTED);

    // DO NOT ADD CODE after this. The previous call to OnRequestFailed has
    // destroyed the NavigationRequest.
    return;
  }

  if (result == NavigationThrottle::BLOCK_RESPONSE) {
    OnRequestFailed(false, net::ERR_BLOCKED_BY_RESPONSE);
    // DO NOT ADD CODE after this. The previous call to OnRequestFailed has
    // destroyed the NavigationRequest.
    return;
  }

  CommitNavigation();

  // DO NOT ADD CODE after this. The previous call to CommitNavigation caused
  // the destruction of the NavigationRequest.
}

void NavigationRequest::CommitNavigation() {
  DCHECK(response_ || !ShouldMakeNetworkRequestForURL(common_params_.url) ||
         navigation_handle_->IsSameDocument());
  DCHECK(!common_params_.url.SchemeIs(url::kJavaScriptScheme));

  // Retrieve the RenderFrameHost that needs to commit the navigation.
  RenderFrameHostImpl* render_frame_host =
      navigation_handle_->GetRenderFrameHost();
  DCHECK(render_frame_host ==
             frame_tree_node_->render_manager()->current_frame_host() ||
         render_frame_host ==
             frame_tree_node_->render_manager()->speculative_frame_host());

  TransferNavigationHandleOwnership(render_frame_host);

  DCHECK_EQ(request_params_.has_user_gesture, begin_params_.has_user_gesture);

  render_frame_host->CommitNavigation(response_.get(), std::move(body_),
                                      std::move(handle_), common_params_,
                                      request_params_, is_view_source_);

  frame_tree_node_->ResetNavigationRequest(true, true);
}

}  // namespace content
