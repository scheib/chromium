// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_TEST_NAVIGATION_SIMULATOR_H_
#define CONTENT_PUBLIC_TEST_NAVIGATION_SIMULATOR_H_

#include <memory>

#include "base/callback.h"
#include "base/optional.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/referrer.h"
#include "content/public/test/navigation_simulator.h"
#include "ui/base/page_transition_types.h"

class GURL;

namespace content {

class NavigationHandle;
class NavigationHandleImpl;
class RenderFrameHost;
class TestRenderFrameHost;
struct Referrer;

// An interface for simulating a navigation in unit tests. Currently this only
// supports renderer-initiated navigations.
// Note: this should not be used in browser tests.
// TODO(clamy): support browser-initiated navigations.
class NavigationSimulator : public WebContentsObserver {
 public:
  // Simulates a renderer-initiated navigation to |url| started in
  // |render_frame_host| from start to commit.
  static void NavigateAndCommitFromDocument(const GURL& original_url,
                                            RenderFrameHost* render_frame_host);

  // Simulates a failed renderer-initiated navigation to |url| started in
  // |render_frame_host| from start to commit.
  static void NavigateAndFailFromDocument(const GURL& original_url,
                                          int net_error_code,
                                          RenderFrameHost* render_frame_host);

  // ---------------------------------------------------------------------------

  // All the following methods should be used when more precise control over the
  // navigation is needed.

  // Creates a NavigationSimulator that will be used to simulate a
  // renderer-initiated navigation to |original_url| started by
  // |render_frame_host|.
  static std::unique_ptr<NavigationSimulator> CreateRendererInitiated(
      const GURL& original_url,
      RenderFrameHost* render_frame_host);

  NavigationSimulator(const GURL& original_url,
                      TestRenderFrameHost* render_frame_host);
  ~NavigationSimulator() override;

  // --------------------------------------------------------------------------

  // The following functions should be used to simulate events happening during
  // a navigation.
  //
  // Example of usage for a successful renderer-initiated navigation:
  //   unique_ptr<NavigationSimulator> simulator =
  //       NavigationSimulator::CreateRendererInitiated(
  //           original_url, render_frame_host);
  //   simulator->SetTransition(ui::PAGE_TRANSITION_LINK);
  //   simulator->Start();
  //   for (GURL redirect_url : redirects)
  //     simulator->Redirect(redirect_url);
  //   simulator->Commit();
  //
  // Example of usage for a failed renderer-initiated navigation:
  //   unique_ptr<NavigationSimulator> simulator =
  //       NavigationSimulator::CreateRendererInitiated(
  //           original_url, render_frame_host);
  //   simulator->SetTransition(ui::PAGE_TRANSITION_LINK);
  //   simulator->Start();
  //   for (GURL redirect_url : redirects)
  //     simulator->Redirect(redirect_url);
  //   simulator->Fail(net::ERR_TIMED_OUT);
  //   simulator->CommitErrorPage();
  //
  // Example of usage for a same-page renderer-initiated navigation:
  //   unique_ptr<NavigationSimulator> simulator =
  //       NavigationSimulator::CreateRendererInitiated(
  //           original_url, render_frame_host);
  //   simulator->CommitSameDocument();
  //
  // Example of usage for a renderer-initiated navigation which is cancelled by
  // a throttle upon redirecting. Note that registering the throttle is done
  // elsewhere:
  //   unique_ptr<NavigationSimulator> simulator =
  //       NavigationSimulator::CreateRendererInitiated(
  //           original_url, render_frame_host);
  //   simulator->SetTransition(ui::PAGE_TRANSITION_LINK);
  //   simulator->Start();
  //   simulator->Redirect(redirect_url);
  //   EXPECT_EQ(NavigationThrottle::CANCEL,
  //             simulator->GetLastThrottleCheckResult());

  // Simulates the start of the navigation.
  virtual void Start();

  // Simulates a redirect to |new_url| for the navigation.
  virtual void Redirect(const GURL& new_url);

  // Simulates the commit of the navigation in the RenderFrameHost.
  virtual void Commit();

  // Simulates the navigation failing with the error code |error_code|.
  virtual void Fail(int error_code);

  // Simulates the commit of an error page following a navigation failure.
  virtual void CommitErrorPage();

  // Simulates the commit of a same-document navigation, ie fragment navigations
  // or pushState/popState navigations.
  virtual void CommitSameDocument();

  // Must be called after the simulated navigation or an error page has
  // committed. Returns the RenderFrameHost the navigation committed in.
  virtual RenderFrameHost* GetFinalRenderFrameHost();

  // --------------------------------------------------------------------------

  // The following functions are used to specify the parameters of the
  // navigation. Changes should be  made before calling |Start|, unless they are
  // meant to apply to a redirect. In that case, they should be made before
  // calling |Redirect|.

  // The following parameters are constant during the navigation and may only be
  // specified before calling |Start|.
  virtual void SetTransition(ui::PageTransition transition);

  // The following parameters can change during redirects. They should be
  // specified before calling |Start| if they need to apply to the navigation to
  // the original url. Otherwise, they should be specified before calling
  // |Redirect|.
  virtual void SetReferrer(const Referrer& referrer);

  // Gets the last throttle check result computed by the navigation throttles.
  // It is an error to call this before Start() is called.
  virtual NavigationThrottle::ThrottleCheckResult GetLastThrottleCheckResult();

  // Returns the NavigationHandle associated with the navigation being
  // simulated. It is an error to call this before Start() or after the
  // navigation has finished (successfully or not).
  virtual NavigationHandle* GetNavigationHandle() const;

 private:
  // WebContentsObserver:
  void DidStartNavigation(NavigationHandle* navigation_handle) override;
  void DidRedirectNavigation(NavigationHandle* navigation_handle) override;
  void ReadyToCommitNavigation(NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(NavigationHandle* navigation_handle) override;

  void OnWillStartRequest();
  void OnWillRedirectRequest();
  void OnWillProcessResponse();

  // This method will block waiting for throttle checks to complete.
  void WaitForThrottleChecksComplete();

  // Sets |last_throttle_check_result_| and calls
  // |throttle_checks_wait_closure_|.
  void OnThrottleChecksComplete(NavigationThrottle::ThrottleCheckResult result);

  // Helper method to set the OnThrottleChecksComplete callback on the
  // NavigationHandle.
  void PrepareCompleteCallbackOnHandle();

  // Simulates the DidFailProvisionalLoad IPC following a NavigationThrottle
  // cancelling the navigation.
  // PlzNavigate: this is not needed.
  void FailFromThrottleCheck(NavigationThrottle::ThrottleCheckResult result);

  enum State {
    INITIALIZATION,
    STARTED,
    FAILED,
    FINISHED,
  };

  State state_ = INITIALIZATION;

  // The renderer associated with this navigation.
  TestRenderFrameHost* render_frame_host_;

  // The NavigationHandle associated with this navigation.
  NavigationHandleImpl* handle_;

  GURL navigation_url_;
  Referrer referrer_;
  ui::PageTransition transition_ = ui::PAGE_TRANSITION_LINK;

  // These are used to sanity check the content/public/ API calls emitted as
  // part of the navigation.
  int num_did_start_navigation_called_ = 0;
  int num_will_start_request_called_ = 0;
  int num_will_redirect_request_called_ = 0;
  int num_did_redirect_navigation_called_ = 0;
  int num_will_process_response_called_ = 0;
  int num_ready_to_commit_called_ = 0;
  int num_did_finish_navigation_called_ = 0;

  // Holds the last ThrottleCheckResult calculated by the navigation's
  // throttles. Will be unset before WillStartRequest is finished. Will be unset
  // while throttles are being run, but before they finish.
  base::Optional<NavigationThrottle::ThrottleCheckResult>
      last_throttle_check_result_;

  // Closure that is set when WaitForThrottleChecksComplete is called.
  base::Closure throttle_checks_wait_closure_;

  base::WeakPtrFactory<NavigationSimulator> weak_factory_;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_TEST_NAVIGATION_SIMULATOR_H_
