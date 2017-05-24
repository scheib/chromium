// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/test/navigation_simulator.h"

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "content/browser/frame_host/navigation_handle_impl.h"
#include "content/browser/frame_host/navigation_request.h"
#include "content/common/frame_messages.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/browser_side_navigation_policy.h"
#include "content/test/test_navigation_url_loader.h"
#include "content/test/test_render_frame_host.h"
#include "net/base/load_flags.h"
#include "net/url_request/redirect_info.h"

namespace content {

namespace {

class NavigationThrottleCallbackRunner : public NavigationThrottle {
 public:
  NavigationThrottleCallbackRunner(
      NavigationHandle* handle,
      const base::Closure& on_will_start_request,
      const base::Closure& on_will_redirect_request,
      const base::Closure& on_will_process_response)
      : NavigationThrottle(handle),
        on_will_start_request_(on_will_start_request),
        on_will_redirect_request_(on_will_redirect_request),
        on_will_process_response_(on_will_process_response) {}

  NavigationThrottle::ThrottleCheckResult WillStartRequest() override {
    on_will_start_request_.Run();
    return NavigationThrottle::PROCEED;
  }

  NavigationThrottle::ThrottleCheckResult WillRedirectRequest() override {
    on_will_redirect_request_.Run();
    return NavigationThrottle::PROCEED;
  }

  NavigationThrottle::ThrottleCheckResult WillProcessResponse() override {
    on_will_process_response_.Run();
    return NavigationThrottle::PROCEED;
  }

  const char* GetNameForLogging() override {
    return "NavigationThrottleCallbackRunner";
  }

 private:
  base::Closure on_will_start_request_;
  base::Closure on_will_redirect_request_;
  base::Closure on_will_process_response_;
};

}  // namespace

// static
void NavigationSimulator::NavigateAndCommitFromDocument(
    const GURL& original_url,
    RenderFrameHost* render_frame_host) {
  NavigationSimulator simulator(
      original_url, static_cast<TestRenderFrameHost*>(render_frame_host));
  simulator.Commit();
}

// static
void NavigationSimulator::NavigateAndFailFromDocument(
    const GURL& original_url,
    int net_error_code,
    RenderFrameHost* render_frame_host) {
  NavigationSimulator simulator(
      original_url, static_cast<TestRenderFrameHost*>(render_frame_host));
  simulator.Fail(net_error_code);
  if (net_error_code != net::ERR_ABORTED)
    simulator.CommitErrorPage();
}

// static
std::unique_ptr<NavigationSimulator>
NavigationSimulator::CreateRendererInitiated(
    const GURL& original_url,
    RenderFrameHost* render_frame_host) {
  return base::MakeUnique<NavigationSimulator>(
      original_url, static_cast<TestRenderFrameHost*>(render_frame_host));
}

NavigationSimulator::NavigationSimulator(const GURL& original_url,
                                         TestRenderFrameHost* render_frame_host)
    : WebContentsObserver(WebContents::FromRenderFrameHost(render_frame_host)),
      render_frame_host_(render_frame_host),
      handle_(nullptr),
      navigation_url_(original_url),
      weak_factory_(this) {
  if (render_frame_host->GetParent()) {
    if (!render_frame_host->frame_tree_node()->has_committed_real_load())
      transition_ = ui::PAGE_TRANSITION_AUTO_SUBFRAME;
    else
      transition_ = ui::PAGE_TRANSITION_MANUAL_SUBFRAME;
  }
}

NavigationSimulator::~NavigationSimulator() {}

void NavigationSimulator::Start() {
  CHECK(state_ == INITIALIZATION)
      << "NavigationSimulator::Start should only be called once.";
  state_ = STARTED;

  if (IsBrowserSideNavigationEnabled()) {
    BeginNavigationParams begin_params(
        std::string(), net::LOAD_NORMAL, true /* has_user_gesture */,
        false /* skip_service_worker */, REQUEST_CONTEXT_TYPE_HYPERLINK,
        blink::WebMixedContentContextType::kBlockable,
        false,  // is_form_submission
        url::Origin());
    CommonNavigationParams common_params;
    common_params.url = navigation_url_;
    common_params.referrer = referrer_;
    common_params.transition = transition_;
    common_params.navigation_type =
        PageTransitionCoreTypeIs(transition_, ui::PAGE_TRANSITION_RELOAD)
            ? FrameMsg_Navigate_Type::RELOAD
            : FrameMsg_Navigate_Type::DIFFERENT_DOCUMENT;
    render_frame_host_->OnMessageReceived(FrameHostMsg_BeginNavigation(
        render_frame_host_->GetRoutingID(), common_params, begin_params));
    NavigationRequest* request =
        render_frame_host_->frame_tree_node()->navigation_request();

    // The request failed synchronously.
    if (!request)
      return;
    DCHECK_EQ(handle_, request->navigation_handle());
  } else {
    render_frame_host_->OnMessageReceived(
        FrameHostMsg_DidStartLoading(render_frame_host_->GetRoutingID(), true));
    render_frame_host_->OnMessageReceived(FrameHostMsg_DidStartProvisionalLoad(
        render_frame_host_->GetRoutingID(), navigation_url_,
        std::vector<GURL>(), base::TimeTicks::Now()));
    DCHECK_EQ(handle_, render_frame_host_->navigation_handle());
    handle_->WillStartRequest(
        "GET", scoped_refptr<content::ResourceRequestBodyImpl>(), referrer_,
        true /* user_gesture */, transition_, false /* is_external_protocol */,
        REQUEST_CONTEXT_TYPE_LOCATION,
        blink::WebMixedContentContextType::kNotMixedContent,
        base::Callback<void(NavigationThrottle::ThrottleCheckResult)>());
  }

  CHECK(handle_);
  WaitForThrottleChecksComplete();

  CHECK_EQ(1, num_did_start_navigation_called_);
  if (GetLastThrottleCheckResult() == NavigationThrottle::PROCEED) {
    CHECK_EQ(1, num_will_start_request_called_);
  } else {
    FailFromThrottleCheck(GetLastThrottleCheckResult());
  }
}

void NavigationSimulator::Redirect(const GURL& new_url) {
  CHECK(state_ <= STARTED) << "NavigationSimulator::Redirect should be "
                              "called before Fail or Commit";
  CHECK_EQ(0, num_did_finish_navigation_called_)
      << "NavigationSimulator::Redirect cannot be called after the "
         "navigation has finished";

  if (state_ == INITIALIZATION) {
    Start();
    if (state_ == FAILED)
      return;
  }

  navigation_url_ = new_url;

  int previous_num_will_redirect_request_called =
      num_will_redirect_request_called_;
  int previous_did_redirect_navigation_called =
      num_did_redirect_navigation_called_;

  PrepareCompleteCallbackOnHandle();
  if (IsBrowserSideNavigationEnabled()) {
    NavigationRequest* request =
        render_frame_host_->frame_tree_node()->navigation_request();
    TestNavigationURLLoader* url_loader =
        static_cast<TestNavigationURLLoader*>(request->loader_for_testing());
    CHECK(url_loader);

    net::RedirectInfo redirect_info;
    redirect_info.status_code = 302;
    redirect_info.new_method = "GET";
    redirect_info.new_url = new_url;
    redirect_info.new_first_party_for_cookies = new_url;
    redirect_info.new_referrer = referrer_.url.spec();
    redirect_info.new_referrer_policy =
        Referrer::ReferrerPolicyForUrlRequest(referrer_);

    url_loader->CallOnRequestRedirected(
        redirect_info, scoped_refptr<ResourceResponse>(new ResourceResponse));
  } else {
    handle_->WillRedirectRequest(
        new_url, "GET", referrer_.url, false /* is_external_protocol */,
        scoped_refptr<net::HttpResponseHeaders>(),
        net::HttpResponseInfo::ConnectionInfo(),
        base::Callback<void(NavigationThrottle::ThrottleCheckResult)>());
  }

  WaitForThrottleChecksComplete();

  if (GetLastThrottleCheckResult() == NavigationThrottle::PROCEED) {
    CHECK_EQ(previous_num_will_redirect_request_called + 1,
             num_will_redirect_request_called_);
    CHECK_EQ(previous_did_redirect_navigation_called + 1,
             num_did_redirect_navigation_called_);
  } else {
    FailFromThrottleCheck(GetLastThrottleCheckResult());
  }
}

void NavigationSimulator::Commit() {
  CHECK_LE(state_, STARTED) << "NavigationSimulator::Commit can only be "
                               "called once, and cannot be called after "
                               "NavigationSimulator::Fail";
  CHECK_EQ(0, num_did_finish_navigation_called_)
      << "NavigationSimulator::Commit cannot be called after the "
         "navigation has finished";

  if (state_ == INITIALIZATION) {
    Start();
    if (state_ == FAILED)
      return;
  }

  PrepareCompleteCallbackOnHandle();
  if (IsBrowserSideNavigationEnabled() &&
      render_frame_host_->frame_tree_node()->navigation_request()) {
    render_frame_host_->PrepareForCommit();
    // Synchronous failure can cause the navigation to finish here.
    if (!handle_) {
      state_ = FAILED;
      return;
    }
  }

  // Call NavigationHandle::WillProcessResponse if needed.
  // Note that the handle's state can be CANCELING if a throttle cancelled it
  // synchronously in PrepareForCommit.
  if (handle_->state_for_testing() < NavigationHandleImpl::CANCELING) {
    // Start the request_ids at 1000 to avoid collisions with request ids from
    // network resources (it should be rare to compare these in unit tests).
    static int request_id = 1000;
    GlobalRequestID global_id(render_frame_host_->GetProcess()->GetID(),
                              ++request_id);
    DCHECK(!IsBrowserSideNavigationEnabled());
    handle_->WillProcessResponse(
        render_frame_host_, scoped_refptr<net::HttpResponseHeaders>(),
        net::HttpResponseInfo::ConnectionInfo(), SSLStatus(), global_id,
        false /* should_replace_current_entry */, false /* is_download */,
        false /* is_stream */, base::Closure(),
        base::Callback<void(NavigationThrottle::ThrottleCheckResult)>());
  }

  WaitForThrottleChecksComplete();

  if (GetLastThrottleCheckResult() != NavigationThrottle::PROCEED) {
    FailFromThrottleCheck(GetLastThrottleCheckResult());
    return;
  }

  CHECK_EQ(1, num_will_process_response_called_);
  CHECK_EQ(1, num_ready_to_commit_called_);

  // Update the RenderFrameHost now that we know which RenderFrameHost will
  // commit the navigation.
  TestRenderFrameHost* new_render_frame_host =
      static_cast<TestRenderFrameHost*>(handle_->GetRenderFrameHost());
  if (!IsBrowserSideNavigationEnabled() &&
      new_render_frame_host != render_frame_host_) {
    CHECK(handle_->is_transferring());
    // Simulate the renderer transfer.
    new_render_frame_host->OnMessageReceived(FrameHostMsg_DidStartLoading(
        new_render_frame_host->GetRoutingID(), true));
    new_render_frame_host->OnMessageReceived(
        FrameHostMsg_DidStartProvisionalLoad(
            new_render_frame_host->GetRoutingID(), navigation_url_,
            std::vector<GURL>(), base::TimeTicks::Now()));
    CHECK(!handle_->is_transferring());
  }
  render_frame_host_ = new_render_frame_host;

  // Keep a pointer to the current RenderFrameHost that may be pending deletion
  // after commit.
  RenderFrameHostImpl* previous_rfh =
      render_frame_host_->frame_tree_node()->current_frame_host();

  FrameHostMsg_DidCommitProvisionalLoad_Params params;
  params.nav_entry_id = 0;
  params.url = navigation_url_;
  params.origin = url::Origin(navigation_url_);
  params.transition = transition_;
  params.should_update_history = true;
  params.did_create_new_entry =
      !render_frame_host_->GetParent() ||
      render_frame_host_->frame_tree_node()->has_committed_real_load();
  params.gesture = NavigationGestureUser;
  params.contents_mime_type = "text/html";
  params.method = "GET";
  params.http_status_code = 200;
  params.socket_address.set_host("2001:db8::1");
  params.socket_address.set_port(80);
  params.history_list_was_cleared = false;
  params.original_request_url = navigation_url_;
  params.was_within_same_document = false;
  params.page_state =
      PageState::CreateForTesting(navigation_url_, false, nullptr, nullptr);

  render_frame_host_->SendNavigateWithParams(&params);

  // Simulate the UnloadACK in the old RenderFrameHost if it was swapped out at
  // commit time.
  if (previous_rfh != render_frame_host_) {
    previous_rfh->OnMessageReceived(
        FrameHostMsg_SwapOut_ACK(previous_rfh->GetRoutingID()));
  }

  state_ = FINISHED;

  CHECK_EQ(1, num_did_finish_navigation_called_);
}

void NavigationSimulator::Fail(int error_code) {
  CHECK_LE(state_, STARTED) << "NavigationSimulator::Fail can only be "
                               "called.";
  CHECK_EQ(0, num_did_finish_navigation_called_)
      << "NavigationSimulator::Fail cannot be called after the "
         "navigation has finished";

  if (state_ == INITIALIZATION)
    Start();

  state_ = FAILED;

  bool should_result_in_error_page = error_code != net::ERR_ABORTED;
  if (IsBrowserSideNavigationEnabled()) {
    NavigationRequest* request =
        render_frame_host_->frame_tree_node()->navigation_request();
    CHECK(request);
    TestNavigationURLLoader* url_loader =
        static_cast<TestNavigationURLLoader*>(request->loader_for_testing());
    CHECK(url_loader);
    url_loader->SimulateError(error_code);
  } else {
    FrameHostMsg_DidFailProvisionalLoadWithError_Params error_params;
    error_params.error_code = error_code;
    error_params.url = navigation_url_;
    render_frame_host_->OnMessageReceived(
        FrameHostMsg_DidFailProvisionalLoadWithError(
            render_frame_host_->GetRoutingID(), error_params));
    if (!should_result_in_error_page) {
      render_frame_host_->OnMessageReceived(
          FrameHostMsg_DidStopLoading(render_frame_host_->GetRoutingID()));
    }
  }

  if (IsBrowserSideNavigationEnabled()) {
    if (should_result_in_error_page) {
      CHECK_EQ(1, num_ready_to_commit_called_);
      // Update the RenderFrameHost now that we know which RenderFrameHost will
      // commit the error page.
      render_frame_host_ =
          static_cast<TestRenderFrameHost*>(handle_->GetRenderFrameHost());
    }
  }

  if (should_result_in_error_page)
    CHECK_EQ(0, num_did_finish_navigation_called_);
  else
    CHECK_EQ(1, num_did_finish_navigation_called_);
}

void NavigationSimulator::CommitErrorPage() {
  CHECK_EQ(FAILED, state_)
      << "NavigationSimulator::CommitErrorPage can only be "
         "called once, and should be called after Fail "
         "has been called";
  CHECK_EQ(0, num_did_finish_navigation_called_)
      << "NavigationSimulator::CommitErrorPage cannot be called after the "
         "navigation has finished";

  // Keep a pointer to the current RenderFrameHost that may be pending deletion
  // after commit.
  RenderFrameHostImpl* previous_rfh =
      render_frame_host_->frame_tree_node()->current_frame_host();

  GURL error_url = GURL(kUnreachableWebDataURL);
  render_frame_host_->OnMessageReceived(FrameHostMsg_DidStartProvisionalLoad(
      render_frame_host_->GetRoutingID(), error_url, std::vector<GURL>(),
      base::TimeTicks::Now()));
  FrameHostMsg_DidCommitProvisionalLoad_Params params;
  params.nav_entry_id = 0;
  params.did_create_new_entry = true;
  params.url = navigation_url_;
  params.transition = transition_;
  params.was_within_same_document = false;
  params.url_is_unreachable = true;
  params.page_state =
      PageState::CreateForTesting(navigation_url_, false, nullptr, nullptr);
  render_frame_host_->SendNavigateWithParams(&params);

  // Simulate the UnloadACK in the old RenderFrameHost if it was swapped out at
  // commit time.
  if (previous_rfh != render_frame_host_) {
    previous_rfh->OnMessageReceived(
        FrameHostMsg_SwapOut_ACK(previous_rfh->GetRoutingID()));
  }

  state_ = FINISHED;

  CHECK_EQ(1, num_did_finish_navigation_called_);
}

void NavigationSimulator::CommitSameDocument() {
  CHECK_EQ(INITIALIZATION, state_)
      << "NavigationSimulator::CommitErrorPage should be the only "
         "navigation event function called on the NavigationSimulator";

  render_frame_host_->OnMessageReceived(
      FrameHostMsg_DidStartLoading(render_frame_host_->GetRoutingID(), false));

  FrameHostMsg_DidCommitProvisionalLoad_Params params;
  params.nav_entry_id = 0;
  params.url = navigation_url_;
  params.origin = url::Origin(navigation_url_);
  params.transition = transition_;
  params.should_update_history = true;
  params.did_create_new_entry = false;
  params.gesture = NavigationGestureUser;
  params.contents_mime_type = "text/html";
  params.method = "GET";
  params.http_status_code = 200;
  params.socket_address.set_host("2001:db8::1");
  params.socket_address.set_port(80);
  params.history_list_was_cleared = false;
  params.original_request_url = navigation_url_;
  params.was_within_same_document = true;
  params.page_state =
      PageState::CreateForTesting(navigation_url_, false, nullptr, nullptr);

  render_frame_host_->SendNavigateWithParams(&params);

  render_frame_host_->OnMessageReceived(
      FrameHostMsg_DidStopLoading(render_frame_host_->GetRoutingID()));

  state_ = FINISHED;

  CHECK_EQ(1, num_did_start_navigation_called_);
  CHECK_EQ(0, num_will_start_request_called_);
  CHECK_EQ(0, num_will_process_response_called_);
  CHECK_EQ(0, num_ready_to_commit_called_);
  CHECK_EQ(1, num_did_finish_navigation_called_);
}

void NavigationSimulator::SetTransition(ui::PageTransition transition) {
  CHECK_EQ(INITIALIZATION, state_)
      << "The transition cannot be set after the navigation has started";
  transition_ = transition;
}

void NavigationSimulator::SetReferrer(const Referrer& referrer) {
  CHECK_LE(state_, STARTED) << "The referrer cannot be set after the "
                               "navigation has committed or has failed";
  referrer_ = referrer;
}

NavigationThrottle::ThrottleCheckResult
NavigationSimulator::GetLastThrottleCheckResult() {
  return last_throttle_check_result_.value();
}

NavigationHandle* NavigationSimulator::GetNavigationHandle() const {
  CHECK_EQ(STARTED, state_);
  return handle_;
}

void NavigationSimulator::DidStartNavigation(
    NavigationHandle* navigation_handle) {
  // Check if this navigation is the one we're simulating.
  if (handle_)
    return;

  if (navigation_handle->GetURL() != navigation_url_)
    return;

  NavigationHandleImpl* handle =
      static_cast<NavigationHandleImpl*>(navigation_handle);

  if (handle->frame_tree_node() != render_frame_host_->frame_tree_node())
    return;

  handle_ = handle;

  num_did_start_navigation_called_++;

  // Add a throttle to count NavigationThrottle calls count.
  handle->RegisterThrottleForTesting(
      base::MakeUnique<NavigationThrottleCallbackRunner>(
          handle,
          base::Bind(&NavigationSimulator::OnWillStartRequest,
                     weak_factory_.GetWeakPtr()),
          base::Bind(&NavigationSimulator::OnWillRedirectRequest,
                     weak_factory_.GetWeakPtr()),
          base::Bind(&NavigationSimulator::OnWillProcessResponse,
                     weak_factory_.GetWeakPtr())));

  PrepareCompleteCallbackOnHandle();
}

void NavigationSimulator::DidRedirectNavigation(
    NavigationHandle* navigation_handle) {
  if (navigation_handle == handle_)
    num_did_redirect_navigation_called_++;
}

void NavigationSimulator::ReadyToCommitNavigation(
    NavigationHandle* navigation_handle) {
  if (navigation_handle == handle_)
    num_ready_to_commit_called_++;
}

void NavigationSimulator::DidFinishNavigation(
    NavigationHandle* navigation_handle) {
  if (navigation_handle == handle_) {
    num_did_finish_navigation_called_++;
    handle_ = nullptr;
  }
}

void NavigationSimulator::OnWillStartRequest() {
  num_will_start_request_called_++;
}

void NavigationSimulator::OnWillRedirectRequest() {
  num_will_redirect_request_called_++;
}

void NavigationSimulator::OnWillProcessResponse() {
  num_will_process_response_called_++;
}

void NavigationSimulator::WaitForThrottleChecksComplete() {
  // If last_throttle_check_result_ is set, then throttle checks completed
  // synchronously.
  if (last_throttle_check_result_)
    return;

  base::RunLoop run_loop;
  throttle_checks_wait_closure_ = run_loop.QuitClosure();
  run_loop.Run();
  throttle_checks_wait_closure_.Reset();
}

void NavigationSimulator::OnThrottleChecksComplete(
    NavigationThrottle::ThrottleCheckResult result) {
  DCHECK(!last_throttle_check_result_);
  last_throttle_check_result_ = result;
  if (throttle_checks_wait_closure_)
    throttle_checks_wait_closure_.Run();
}

void NavigationSimulator::PrepareCompleteCallbackOnHandle() {
  last_throttle_check_result_.reset();
  handle_->set_complete_callback_for_testing(
      base::Bind(&NavigationSimulator::OnThrottleChecksComplete,
                 weak_factory_.GetWeakPtr()));
}

RenderFrameHost* NavigationSimulator::GetFinalRenderFrameHost() {
  CHECK_EQ(state_, FINISHED);
  return render_frame_host_;
}

void NavigationSimulator::FailFromThrottleCheck(
    NavigationThrottle::ThrottleCheckResult result) {
  DCHECK_NE(result, NavigationThrottle::PROCEED);
  state_ = FAILED;

  // Special failure logic only needed for non-PlzNavigate case.
  if (IsBrowserSideNavigationEnabled())
    return;
  int error_code = net::OK;
  switch (result) {
    case NavigationThrottle::PROCEED:
    case NavigationThrottle::DEFER:
      NOTREACHED();
      break;
    case NavigationThrottle::CANCEL:
    case NavigationThrottle::CANCEL_AND_IGNORE:
      error_code = net::ERR_ABORTED;
      break;
    case NavigationThrottle::BLOCK_REQUEST:
      error_code = net::ERR_BLOCKED_BY_CLIENT;
      break;
    case NavigationThrottle::BLOCK_RESPONSE:
      error_code = net::ERR_BLOCKED_BY_RESPONSE;
      break;
  };

  FrameHostMsg_DidFailProvisionalLoadWithError_Params error_params;
  error_params.error_code = error_code;
  error_params.url = navigation_url_;
  render_frame_host_->OnMessageReceived(
      FrameHostMsg_DidFailProvisionalLoadWithError(
          render_frame_host_->GetRoutingID(), error_params));
  bool should_result_in_error_page = error_code != net::ERR_ABORTED;
  if (!should_result_in_error_page) {
    render_frame_host_->OnMessageReceived(
        FrameHostMsg_DidStopLoading(render_frame_host_->GetRoutingID()));
    CHECK_EQ(1, num_did_finish_navigation_called_);
  } else {
    CHECK_EQ(0, num_did_finish_navigation_called_);
  }
}

}  // namespace content
