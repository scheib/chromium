// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/frame_host/navigation_handle_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/browser_side_navigation_policy.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/request_context_type.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/navigation_handle_observer.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "content/shell/browser/shell.h"
#include "content/test/content_browser_test_utils_internal.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/url_request/url_request_failed_job.h"
#include "ui/base/page_transition_types.h"
#include "url/url_constants.h"

namespace content {

namespace {

// A test NavigationThrottle that will return pre-determined checks and run
// callbacks when the various NavigationThrottle methods are called. It is
// not instantiated directly but through a TestNavigationThrottleInstaller.
class TestNavigationThrottle : public NavigationThrottle {
 public:
  TestNavigationThrottle(
      NavigationHandle* handle,
      NavigationThrottle::ThrottleCheckResult will_start_result,
      NavigationThrottle::ThrottleCheckResult will_redirect_result,
      NavigationThrottle::ThrottleCheckResult will_process_result,
      base::Closure did_call_will_start,
      base::Closure did_call_will_redirect,
      base::Closure did_call_will_process)
      : NavigationThrottle(handle),
        will_start_result_(will_start_result),
        will_redirect_result_(will_redirect_result),
        will_process_result_(will_process_result),
        did_call_will_start_(did_call_will_start),
        did_call_will_redirect_(did_call_will_redirect),
        did_call_will_process_(did_call_will_process) {}
  ~TestNavigationThrottle() override {}

  const char* GetNameForLogging() override { return "TestNavigationThrottle"; }

  void Resume() { navigation_handle()->Resume(); }

  RequestContextType request_context_type() { return request_context_type_; }

 private:
  // NavigationThrottle implementation.
  NavigationThrottle::ThrottleCheckResult WillStartRequest() override {
    NavigationHandleImpl* navigation_handle_impl =
        static_cast<NavigationHandleImpl*>(navigation_handle());
    CHECK_NE(REQUEST_CONTEXT_TYPE_UNSPECIFIED,
             navigation_handle_impl->request_context_type());
    request_context_type_ = navigation_handle_impl->request_context_type();

    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE, did_call_will_start_);
    return will_start_result_;
  }

  NavigationThrottle::ThrottleCheckResult WillRedirectRequest() override {
    NavigationHandleImpl* navigation_handle_impl =
        static_cast<NavigationHandleImpl*>(navigation_handle());
    CHECK_EQ(request_context_type_,
             navigation_handle_impl->request_context_type());

    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            did_call_will_redirect_);
    return will_redirect_result_;
  }

  NavigationThrottle::ThrottleCheckResult WillProcessResponse() override {
    NavigationHandleImpl* navigation_handle_impl =
        static_cast<NavigationHandleImpl*>(navigation_handle());
    CHECK_EQ(request_context_type_,
             navigation_handle_impl->request_context_type());

    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            did_call_will_process_);
    return will_process_result_;
  }

  NavigationThrottle::ThrottleCheckResult will_start_result_;
  NavigationThrottle::ThrottleCheckResult will_redirect_result_;
  NavigationThrottle::ThrottleCheckResult will_process_result_;
  base::Closure did_call_will_start_;
  base::Closure did_call_will_redirect_;
  base::Closure did_call_will_process_;
  RequestContextType request_context_type_ = REQUEST_CONTEXT_TYPE_UNSPECIFIED;
};

// Install a TestNavigationThrottle on all following requests and allows waiting
// for various NavigationThrottle related events. Waiting works only for the
// immediately next navigation. New instances are needed to wait for further
// navigations.
class TestNavigationThrottleInstaller : public WebContentsObserver {
 public:
  TestNavigationThrottleInstaller(
      WebContents* web_contents,
      NavigationThrottle::ThrottleCheckResult will_start_result,
      NavigationThrottle::ThrottleCheckResult will_redirect_result,
      NavigationThrottle::ThrottleCheckResult will_process_result)
      : WebContentsObserver(web_contents),
        will_start_result_(will_start_result),
        will_redirect_result_(will_redirect_result),
        will_process_result_(will_process_result),
        weak_factory_(this) {}
  ~TestNavigationThrottleInstaller() override {}

  TestNavigationThrottle* navigation_throttle() { return navigation_throttle_; }

  void WaitForThrottleWillStart() {
    if (will_start_called_)
      return;
    will_start_loop_runner_ = new MessageLoopRunner();
    will_start_loop_runner_->Run();
    will_start_loop_runner_ = nullptr;
  }

  void WaitForThrottleWillRedirect() {
    if (will_redirect_called_)
      return;
    will_redirect_loop_runner_ = new MessageLoopRunner();
    will_redirect_loop_runner_->Run();
    will_redirect_loop_runner_ = nullptr;
  }

  void WaitForThrottleWillProcess() {
    if (will_process_called_)
      return;
    will_process_loop_runner_ = new MessageLoopRunner();
    will_process_loop_runner_->Run();
    will_process_loop_runner_ = nullptr;
  }

  int will_start_called() { return will_start_called_; }
  int will_redirect_called() { return will_redirect_called_; }
  int will_process_called() { return will_process_called_; }

  int install_count() { return install_count_; }

 private:
  void DidStartNavigation(NavigationHandle* handle) override {
    std::unique_ptr<NavigationThrottle> throttle(new TestNavigationThrottle(
        handle, will_start_result_, will_redirect_result_, will_process_result_,
        base::Bind(&TestNavigationThrottleInstaller::DidCallWillStartRequest,
                   weak_factory_.GetWeakPtr()),
        base::Bind(&TestNavigationThrottleInstaller::DidCallWillRedirectRequest,
                   weak_factory_.GetWeakPtr()),
        base::Bind(&TestNavigationThrottleInstaller::DidCallWillProcessResponse,
                   weak_factory_.GetWeakPtr())));
    navigation_throttle_ = static_cast<TestNavigationThrottle*>(throttle.get());
    handle->RegisterThrottleForTesting(std::move(throttle));
    ++install_count_;
  }

  void DidFinishNavigation(NavigationHandle* handle) override {
    if (!navigation_throttle_)
      return;

    if (handle == navigation_throttle_->navigation_handle())
      navigation_throttle_ = nullptr;
  }

  void DidCallWillStartRequest() {
    will_start_called_++;
    if (will_start_loop_runner_)
      will_start_loop_runner_->Quit();
  }

  void DidCallWillRedirectRequest() {
    will_redirect_called_++;
    if (will_redirect_loop_runner_)
      will_redirect_loop_runner_->Quit();
  }

  void DidCallWillProcessResponse() {
    will_process_called_++;
    if (will_process_loop_runner_)
      will_process_loop_runner_->Quit();
  }

  NavigationThrottle::ThrottleCheckResult will_start_result_;
  NavigationThrottle::ThrottleCheckResult will_redirect_result_;
  NavigationThrottle::ThrottleCheckResult will_process_result_;
  int will_start_called_ = 0;
  int will_redirect_called_ = 0;
  int will_process_called_ = 0;
  TestNavigationThrottle* navigation_throttle_ = nullptr;
  int install_count_ = 0;
  scoped_refptr<MessageLoopRunner> will_start_loop_runner_;
  scoped_refptr<MessageLoopRunner> will_redirect_loop_runner_;
  scoped_refptr<MessageLoopRunner> will_process_loop_runner_;

  // The throttle installer can be deleted before all tasks posted by its
  // throttles are run, so it must be referenced via weak pointers.
  base::WeakPtrFactory<TestNavigationThrottleInstaller> weak_factory_;
};

// Records all navigation start URLs from the WebContents.
class NavigationStartUrlRecorder : public WebContentsObserver {
 public:
  NavigationStartUrlRecorder(WebContents* web_contents)
      : WebContentsObserver(web_contents) {}

  void DidStartNavigation(NavigationHandle* navigation_handle) override {
    urls_.push_back(navigation_handle->GetURL());
  }

  const std::vector<GURL>& urls() const { return urls_; }

 private:
  std::vector<GURL> urls_;
};

}  // namespace

class NavigationHandleImplBrowserTest : public ContentBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    SetupCrossSiteRedirector(embedded_test_server());
    ASSERT_TRUE(embedded_test_server()->Start());
  }
};

// Ensure that PageTransition is properly set on the NavigationHandle.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, VerifyPageTransition) {
  {
    // Test browser initiated navigation, which should have a PageTransition as
    // if it came from the omnibox.
    GURL url(embedded_test_server()->GetURL("/title1.html"));
    NavigationHandleObserver observer(shell()->web_contents(), url);
    ui::PageTransition expected_transition = ui::PageTransitionFromInt(
        ui::PAGE_TRANSITION_TYPED | ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);

    EXPECT_TRUE(NavigateToURL(shell(), url));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_EQ(url, observer.last_committed_url());
    EXPECT_TRUE(ui::PageTransitionTypeIncludingQualifiersIs(
        observer.page_transition(), expected_transition));
  }

  {
    // Test navigating to a page with subframe. The subframe will have
    // PageTransition of type AUTO_SUBFRAME.
    GURL url(
        embedded_test_server()->GetURL("/frame_tree/page_with_one_frame.html"));
    NavigationHandleObserver observer(
        shell()->web_contents(),
        embedded_test_server()->GetURL("/cross-site/baz.com/title1.html"));
    ui::PageTransition expected_transition =
        ui::PageTransitionFromInt(ui::PAGE_TRANSITION_AUTO_SUBFRAME);

    EXPECT_TRUE(NavigateToURL(shell(), url));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_EQ(embedded_test_server()->GetURL("baz.com", "/title1.html"),
              observer.last_committed_url());
    EXPECT_TRUE(ui::PageTransitionTypeIncludingQualifiersIs(
        observer.page_transition(), expected_transition));
    EXPECT_FALSE(observer.is_main_frame());
  }
}

// Ensure that the following methods on NavigationHandle behave correctly:
// * IsInMainFrame
// * IsParentMainFrame
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, VerifyFrameTree) {
  GURL main_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(c))"));
  GURL b_url(embedded_test_server()->GetURL(
      "b.com", "/cross_site_iframe_factory.html?b(c())"));
  GURL c_url(embedded_test_server()->GetURL(
      "c.com", "/cross_site_iframe_factory.html?c()"));

  NavigationHandleObserver main_observer(shell()->web_contents(), main_url);
  NavigationHandleObserver b_observer(shell()->web_contents(), b_url);
  NavigationHandleObserver c_observer(shell()->web_contents(), c_url);

  EXPECT_TRUE(NavigateToURL(shell(), main_url));

  FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                            ->GetFrameTree()
                            ->root();

  // Verify the main frame.
  EXPECT_TRUE(main_observer.has_committed());
  EXPECT_FALSE(main_observer.is_error());
  EXPECT_EQ(main_url, main_observer.last_committed_url());
  EXPECT_TRUE(main_observer.is_main_frame());
  EXPECT_EQ(root->frame_tree_node_id(), main_observer.frame_tree_node_id());

  // Verify the b.com frame.
  EXPECT_TRUE(b_observer.has_committed());
  EXPECT_FALSE(b_observer.is_error());
  EXPECT_EQ(b_url, b_observer.last_committed_url());
  EXPECT_FALSE(b_observer.is_main_frame());
  EXPECT_TRUE(b_observer.is_parent_main_frame());
  EXPECT_EQ(root->child_at(0)->frame_tree_node_id(),
            b_observer.frame_tree_node_id());

  // Verify the c.com frame.
  EXPECT_TRUE(c_observer.has_committed());
  EXPECT_FALSE(c_observer.is_error());
  EXPECT_EQ(c_url, c_observer.last_committed_url());
  EXPECT_FALSE(c_observer.is_main_frame());
  EXPECT_FALSE(c_observer.is_parent_main_frame());
  EXPECT_EQ(root->child_at(0)->child_at(0)->frame_tree_node_id(),
            c_observer.frame_tree_node_id());
}

// Ensure that the WasRedirected() method on NavigationHandle behaves correctly.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, VerifyRedirect) {
  {
    GURL url(embedded_test_server()->GetURL("/title1.html"));
    NavigationHandleObserver observer(shell()->web_contents(), url);

    EXPECT_TRUE(NavigateToURL(shell(), url));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_FALSE(observer.was_redirected());
  }

  {
    GURL url(embedded_test_server()->GetURL("/cross-site/baz.com/title1.html"));
    NavigationHandleObserver observer(shell()->web_contents(), url);

    NavigateToURL(shell(), url);

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_TRUE(observer.was_redirected());
  }
}

// Ensure that the IsRendererInitiated() method on NavigationHandle behaves
// correctly.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest,
                       VerifyRendererInitiated) {
  {
    // Test browser initiated navigation.
    GURL url(embedded_test_server()->GetURL("/title1.html"));
    NavigationHandleObserver observer(shell()->web_contents(), url);

    EXPECT_TRUE(NavigateToURL(shell(), url));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_FALSE(observer.is_renderer_initiated());
  }

  {
    // Test a main frame + subframes navigation.
    GURL main_url(embedded_test_server()->GetURL(
        "a.com", "/cross_site_iframe_factory.html?a(b(c))"));
    GURL b_url(embedded_test_server()->GetURL(
        "b.com", "/cross_site_iframe_factory.html?b(c())"));
    GURL c_url(embedded_test_server()->GetURL(
        "c.com", "/cross_site_iframe_factory.html?c()"));

    NavigationHandleObserver main_observer(shell()->web_contents(), main_url);
    NavigationHandleObserver b_observer(shell()->web_contents(), b_url);
    NavigationHandleObserver c_observer(shell()->web_contents(), c_url);

    EXPECT_TRUE(NavigateToURL(shell(), main_url));

    // Verify that the main frame navigation is not renderer initiated.
    EXPECT_TRUE(main_observer.has_committed());
    EXPECT_FALSE(main_observer.is_error());
    EXPECT_FALSE(main_observer.is_renderer_initiated());

    // Verify that the subframe navigations are renderer initiated.
    EXPECT_TRUE(b_observer.has_committed());
    EXPECT_FALSE(b_observer.is_error());
    EXPECT_TRUE(b_observer.is_renderer_initiated());
    EXPECT_TRUE(c_observer.has_committed());
    EXPECT_FALSE(c_observer.is_error());
    EXPECT_TRUE(c_observer.is_renderer_initiated());
  }

  {
    // Test a pushState navigation.
    GURL url(embedded_test_server()->GetURL(
        "a.com", "/cross_site_iframe_factory.html?a(a())"));
    EXPECT_TRUE(NavigateToURL(shell(), url));

    FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                              ->GetFrameTree()
                              ->root();

    NavigationHandleObserver observer(
        shell()->web_contents(),
        embedded_test_server()->GetURL("a.com", "/bar"));
    EXPECT_TRUE(ExecuteScript(root->child_at(0),
                              "window.history.pushState({}, '', 'bar');"));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_TRUE(observer.is_renderer_initiated());
  }
}

// Ensure that methods on NavigationHandle behave correctly with an iframe that
// navigates to its srcdoc attribute.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, VerifySrcdoc) {
  GURL url(embedded_test_server()->GetURL(
      "/frame_tree/page_with_srcdoc_frame.html"));
  NavigationHandleObserver observer(shell()->web_contents(),
                                    GURL(kAboutSrcDocURL));

  EXPECT_TRUE(NavigateToURL(shell(), url));

  EXPECT_TRUE(observer.has_committed());
  EXPECT_FALSE(observer.is_error());
  EXPECT_EQ(GURL(kAboutSrcDocURL), observer.last_committed_url());
}

// Ensure that the IsSameDocument() method on NavigationHandle behaves
// correctly.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, VerifySameDocument) {
  GURL url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(a())"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                            ->GetFrameTree()
                            ->root();
  {
    NavigationHandleObserver observer(
        shell()->web_contents(),
        embedded_test_server()->GetURL("a.com", "/foo"));
    EXPECT_TRUE(ExecuteScript(root->child_at(0),
                              "window.history.pushState({}, '', 'foo');"));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_TRUE(observer.is_same_document());
  }
  {
    NavigationHandleObserver observer(
        shell()->web_contents(),
        embedded_test_server()->GetURL("a.com", "/bar"));
    EXPECT_TRUE(ExecuteScript(root->child_at(0),
                              "window.history.replaceState({}, '', 'bar');"));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_TRUE(observer.is_same_document());
  }
  {
    NavigationHandleObserver observer(
        shell()->web_contents(),
        embedded_test_server()->GetURL("a.com", "/bar#frag"));
    EXPECT_TRUE(
        ExecuteScript(root->child_at(0), "window.location.replace('#frag');"));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_TRUE(observer.is_same_document());
  }

  GURL about_blank_url(url::kAboutBlankURL);
  {
    NavigationHandleObserver observer(shell()->web_contents(), about_blank_url);
    EXPECT_TRUE(ExecuteScript(
        root, "document.body.appendChild(document.createElement('iframe'));"));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_FALSE(observer.is_same_document());
    EXPECT_EQ(about_blank_url, observer.last_committed_url());
  }
  {
    NavigationHandleObserver observer(shell()->web_contents(), about_blank_url);
    NavigateFrameToURL(root->child_at(0), about_blank_url);

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_FALSE(observer.is_same_document());
    EXPECT_EQ(about_blank_url, observer.last_committed_url());
  }
}

// Ensure that a NavigationThrottle can cancel the navigation at navigation
// start.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, ThrottleCancelStart) {
  GURL start_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), start_url));

  GURL redirect_url(
      embedded_test_server()->GetURL("/cross-site/bar.com/title2.html"));
  NavigationHandleObserver observer(shell()->web_contents(), redirect_url);
  TestNavigationThrottleInstaller installer(
      shell()->web_contents(), NavigationThrottle::CANCEL,
      NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);

  EXPECT_FALSE(NavigateToURL(shell(), redirect_url));

  EXPECT_FALSE(observer.has_committed());
  EXPECT_TRUE(observer.is_error());

  // The navigation should have been canceled before being redirected.
  EXPECT_FALSE(observer.was_redirected());
  EXPECT_EQ(shell()->web_contents()->GetLastCommittedURL(), start_url);
}

// Ensure that a NavigationThrottle can cancel the navigation when a navigation
// is redirected.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest,
                       ThrottleCancelRedirect) {
  GURL start_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), start_url));

  // A navigation with a redirect should be canceled.
  {
    GURL redirect_url(
        embedded_test_server()->GetURL("/cross-site/bar.com/title2.html"));
    NavigationHandleObserver observer(shell()->web_contents(), redirect_url);
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::CANCEL, NavigationThrottle::PROCEED);

    EXPECT_FALSE(NavigateToURL(shell(), redirect_url));

    EXPECT_FALSE(observer.has_committed());
    EXPECT_TRUE(observer.is_error());
    EXPECT_TRUE(observer.was_redirected());
    EXPECT_EQ(shell()->web_contents()->GetLastCommittedURL(), start_url);
  }

  // A navigation without redirects should be successful.
  {
    GURL no_redirect_url(embedded_test_server()->GetURL("/title2.html"));
    NavigationHandleObserver observer(shell()->web_contents(), no_redirect_url);
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::CANCEL, NavigationThrottle::PROCEED);

    EXPECT_TRUE(NavigateToURL(shell(), no_redirect_url));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_FALSE(observer.was_redirected());
    EXPECT_EQ(shell()->web_contents()->GetLastCommittedURL(), no_redirect_url);
  }
}

// Ensure that a NavigationThrottle can cancel the navigation when the response
// is received.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest,
                       ThrottleCancelResponse) {
  GURL start_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), start_url));

  GURL redirect_url(
      embedded_test_server()->GetURL("/cross-site/bar.com/title2.html"));
  NavigationHandleObserver observer(shell()->web_contents(), redirect_url);
  TestNavigationThrottleInstaller installer(
      shell()->web_contents(), NavigationThrottle::PROCEED,
      NavigationThrottle::PROCEED, NavigationThrottle::CANCEL);

  EXPECT_FALSE(NavigateToURL(shell(), redirect_url));

  EXPECT_FALSE(observer.has_committed());
  EXPECT_TRUE(observer.is_error());
  // The navigation should have been redirected first, and then canceled when
  // the response arrived.
  EXPECT_TRUE(observer.was_redirected());
  EXPECT_EQ(shell()->web_contents()->GetLastCommittedURL(), start_url);
}

// Ensure that a NavigationThrottle can defer and resume the navigation at
// navigation start, navigation redirect and response received.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, ThrottleDefer) {
  GURL start_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), start_url));

  GURL redirect_url(
      embedded_test_server()->GetURL("/cross-site/bar.com/title2.html"));
  TestNavigationObserver navigation_observer(shell()->web_contents(), 1);
  NavigationHandleObserver observer(shell()->web_contents(), redirect_url);
  TestNavigationThrottleInstaller installer(
      shell()->web_contents(), NavigationThrottle::DEFER,
      NavigationThrottle::DEFER, NavigationThrottle::DEFER);

  shell()->LoadURL(redirect_url);

  // Wait for WillStartRequest.
  installer.WaitForThrottleWillStart();
  EXPECT_EQ(1, installer.will_start_called());
  EXPECT_EQ(0, installer.will_redirect_called());
  EXPECT_EQ(0, installer.will_process_called());
  installer.navigation_throttle()->Resume();

  // Wait for WillRedirectRequest.
  installer.WaitForThrottleWillRedirect();
  EXPECT_EQ(1, installer.will_start_called());
  EXPECT_EQ(1, installer.will_redirect_called());
  EXPECT_EQ(0, installer.will_process_called());
  installer.navigation_throttle()->Resume();

  // Wait for WillProcessResponse.
  installer.WaitForThrottleWillProcess();
  EXPECT_EQ(1, installer.will_start_called());
  EXPECT_EQ(1, installer.will_redirect_called());
  EXPECT_EQ(1, installer.will_process_called());
  installer.navigation_throttle()->Resume();

  // Wait for the end of the navigation.
  navigation_observer.Wait();

  EXPECT_TRUE(observer.has_committed());
  EXPECT_TRUE(observer.was_redirected());
  EXPECT_FALSE(observer.is_error());
  EXPECT_EQ(shell()->web_contents()->GetLastCommittedURL(),
            GURL(embedded_test_server()->GetURL("bar.com", "/title2.html")));
}

// Checks that the RequestContextType value is properly set.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest,
                       VerifyRequestContextTypeForFrameTree) {
  GURL main_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(c))"));
  GURL b_url(embedded_test_server()->GetURL(
      "b.com", "/cross_site_iframe_factory.html?b(c())"));
  GURL c_url(embedded_test_server()->GetURL(
      "c.com", "/cross_site_iframe_factory.html?c()"));

  TestNavigationThrottleInstaller installer(
      shell()->web_contents(), NavigationThrottle::PROCEED,
      NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);
  TestNavigationManager main_manager(shell()->web_contents(), main_url);
  TestNavigationManager b_manager(shell()->web_contents(), b_url);
  TestNavigationManager c_manager(shell()->web_contents(), c_url);
  NavigationStartUrlRecorder url_recorder(shell()->web_contents());

  // Starts and verifies the main frame navigation.
  shell()->LoadURL(main_url);
  EXPECT_TRUE(main_manager.WaitForRequestStart());
  // For each navigation a new throttle should have been installed.
  EXPECT_EQ(1, installer.install_count());
  // Checks the only URL recorded so far is the one expected for the main frame.
  EXPECT_EQ(main_url, url_recorder.urls().back());
  EXPECT_EQ(1ul, url_recorder.urls().size());
  // Checks the main frame RequestContextType.
  EXPECT_EQ(REQUEST_CONTEXT_TYPE_LOCATION,
            installer.navigation_throttle()->request_context_type());

  // Ditto for frame b navigation.
  main_manager.WaitForNavigationFinished();
  EXPECT_TRUE(b_manager.WaitForRequestStart());
  EXPECT_EQ(2, installer.install_count());
  EXPECT_EQ(b_url, url_recorder.urls().back());
  EXPECT_EQ(2ul, url_recorder.urls().size());
  EXPECT_EQ(REQUEST_CONTEXT_TYPE_LOCATION,
            installer.navigation_throttle()->request_context_type());

  // Ditto for frame c navigation.
  b_manager.WaitForNavigationFinished();
  EXPECT_TRUE(c_manager.WaitForRequestStart());
  EXPECT_EQ(3, installer.install_count());
  EXPECT_EQ(c_url, url_recorder.urls().back());
  EXPECT_EQ(3ul, url_recorder.urls().size());
  EXPECT_EQ(REQUEST_CONTEXT_TYPE_LOCATION,
            installer.navigation_throttle()->request_context_type());

  // Lets the final navigation finish so that we conclude running the
  // RequestContextType checks that happen in TestNavigationThrottle.
  c_manager.WaitForNavigationFinished();
  // Confirms the last navigation did finish.
  EXPECT_FALSE(installer.navigation_throttle());
}

// Checks that the RequestContextType value is properly set for an hyper-link.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest,
                       VerifyHyperlinkRequestContextType) {
  GURL link_url(embedded_test_server()->GetURL("/title2.html"));
  GURL document_url(embedded_test_server()->GetURL("/simple_links.html"));

  TestNavigationThrottleInstaller installer(
      shell()->web_contents(), NavigationThrottle::PROCEED,
      NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);
  TestNavigationManager link_manager(shell()->web_contents(), link_url);
  NavigationStartUrlRecorder url_recorder(shell()->web_contents());

  // Navigate to a page with a link.
  EXPECT_TRUE(NavigateToURL(shell(), document_url));
  EXPECT_EQ(document_url, url_recorder.urls().back());
  EXPECT_EQ(1ul, url_recorder.urls().size());

  // Starts the navigation from a link click and then check it.
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  bool success = false;
  EXPECT_TRUE(ExecuteScriptAndExtractBool(
      shell(), "window.domAutomationController.send(clickSameSiteLink());",
      &success));
  EXPECT_TRUE(success);
  EXPECT_TRUE(link_manager.WaitForRequestStart());
  EXPECT_EQ(link_url, url_recorder.urls().back());
  EXPECT_EQ(2ul, url_recorder.urls().size());
  EXPECT_EQ(REQUEST_CONTEXT_TYPE_HYPERLINK,
            installer.navigation_throttle()->request_context_type());

  // Finishes the last navigation.
  link_manager.WaitForNavigationFinished();
  EXPECT_FALSE(installer.navigation_throttle());
}

// Checks that the RequestContextType value is properly set for an form (POST).
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest,
                       VerifyFormRequestContextType) {
  GURL document_url(
      embedded_test_server()->GetURL("/session_history/form.html"));
  GURL post_url(embedded_test_server()->GetURL("/echotitle"));

  TestNavigationThrottleInstaller installer(
      shell()->web_contents(), NavigationThrottle::PROCEED,
      NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);
  TestNavigationManager post_manager(shell()->web_contents(), post_url);
  NavigationStartUrlRecorder url_recorder(shell()->web_contents());

  // Navigate to a page with a form.
  EXPECT_TRUE(NavigateToURL(shell(), document_url));
  EXPECT_EQ(document_url, url_recorder.urls().back());
  EXPECT_EQ(1ul, url_recorder.urls().size());

  // Executes the form POST navigation and then check it.
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  GURL submit_url("javascript:submitForm('isubmit')");
  shell()->LoadURL(submit_url);
  EXPECT_TRUE(post_manager.WaitForRequestStart());
  EXPECT_EQ(post_url, url_recorder.urls().back());
  EXPECT_EQ(2ul, url_recorder.urls().size());
  EXPECT_EQ(REQUEST_CONTEXT_TYPE_FORM,
            installer.navigation_throttle()->request_context_type());

  // Finishes the last navigation.
  post_manager.WaitForNavigationFinished();
  EXPECT_FALSE(installer.navigation_throttle());
}

// Checks that the error code is properly set on the NavigationHandle when a
// NavigationThrottle cancels.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest,
                       ErrorCodeOnThrottleCancelNavigation) {
  const GURL kUrl = embedded_test_server()->GetURL("/title1.html");
  const GURL kRedirectingUrl =
      embedded_test_server()->GetURL("/server-redirect?" + kUrl.spec());

  {
    // Set up a NavigationThrottle that will cancel the navigation in
    // WillStartRequest.
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::CANCEL_AND_IGNORE,
        NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);
    NavigationHandleObserver observer(shell()->web_contents(), kUrl);

    // Try to navigate to the url. The navigation should be canceled and the
    // NavigationHandle should have the right error code.
    EXPECT_FALSE(NavigateToURL(shell(), kUrl));
    EXPECT_EQ(net::ERR_ABORTED, observer.net_error_code());
  }

  {
    // Set up a NavigationThrottle that will cancel the navigation in
    // WillRedirectRequest.
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::CANCEL_AND_IGNORE, NavigationThrottle::PROCEED);
    NavigationHandleObserver observer(shell()->web_contents(), kRedirectingUrl);

    // Try to navigate to the url. The navigation should be canceled and the
    // NavigationHandle should have the right error code.
    EXPECT_FALSE(NavigateToURL(shell(), kRedirectingUrl));
    EXPECT_EQ(net::ERR_ABORTED, observer.net_error_code());
  }

  {
    // Set up a NavigationThrottle that will cancel the navigation in
    // WillProcessResponse.
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::PROCEED, NavigationThrottle::CANCEL_AND_IGNORE);
    NavigationHandleObserver observer(shell()->web_contents(), kUrl);

    // Try to navigate to the url. The navigation should be canceled and the
    // NavigationHandle should have the right error code.
    EXPECT_FALSE(NavigateToURL(shell(), kUrl));
    EXPECT_EQ(net::ERR_ABORTED, observer.net_error_code());
  }

  {
    // Set up a NavigationThrottle that will block the navigation in
    // WillStartRequest.
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::BLOCK_REQUEST,
        NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);
    NavigationHandleObserver observer(shell()->web_contents(), kUrl);

    // Try to navigate to the url. The navigation should be canceled and the
    // NavigationHandle should have the right error code.
    EXPECT_FALSE(NavigateToURL(shell(), kUrl));
    EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, observer.net_error_code());
  }

  // Using BLOCK_REQUEST on redirect is available only with PlzNavigate.
  if (IsBrowserSideNavigationEnabled()) {
    // Set up a NavigationThrottle that will block the navigation in
    // WillRedirectRequest.
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::BLOCK_REQUEST, NavigationThrottle::PROCEED);
    NavigationHandleObserver observer(shell()->web_contents(), kRedirectingUrl);

    // Try to navigate to the url. The navigation should be canceled and the
    // NavigationHandle should have the right error code.
    EXPECT_FALSE(NavigateToURL(shell(), kRedirectingUrl));
    EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, observer.net_error_code());
  }
}

// Specialized test that verifies the NavigationHandle gets the HTTPS upgraded
// URL from the very beginning of the navigation.
class NavigationHandleImplHttpsUpgradeBrowserTest
    : public NavigationHandleImplBrowserTest {
 public:
  void CheckHttpsUpgradedIframeNavigation(const GURL& start_url,
                                          const GURL& iframe_secure_url) {
    ASSERT_TRUE(start_url.SchemeIs(url::kHttpScheme));
    ASSERT_TRUE(iframe_secure_url.SchemeIs(url::kHttpsScheme));

    NavigationStartUrlRecorder url_recorder(shell()->web_contents());
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);
    TestNavigationManager navigation_manager(shell()->web_contents(),
                                             iframe_secure_url);

    // Load the page and wait for the frame load with the expected URL.
    // Note: if the test times out while waiting then a navigation to
    // iframe_secure_url never happened and the expected upgrade may not be
    // working.
    shell()->LoadURL(start_url);
    EXPECT_TRUE(navigation_manager.WaitForRequestStart());

    // The main frame should have finished navigating while the iframe should
    // have just started.
    EXPECT_EQ(2, installer.will_start_called());
    EXPECT_EQ(0, installer.will_redirect_called());
    EXPECT_EQ(1, installer.will_process_called());

    // Check the correct start URLs have been registered.
    EXPECT_EQ(iframe_secure_url, url_recorder.urls().back());
    EXPECT_EQ(start_url, url_recorder.urls().front());
    EXPECT_EQ(2ul, url_recorder.urls().size());
  }
};

// Tests that the start URL is HTTPS upgraded for a same site navigation.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplHttpsUpgradeBrowserTest,
                       StartUrlIsHttpsUpgradedSameSite) {
  GURL start_url(
      embedded_test_server()->GetURL("/https_upgrade_same_site.html"));

  // Builds the expected upgraded same site URL.
  GURL::Replacements replace_scheme;
  replace_scheme.SetSchemeStr("https");
  GURL cross_site_iframe_secure_url = embedded_test_server()
                                          ->GetURL("/title1.html")
                                          .ReplaceComponents(replace_scheme);

  CheckHttpsUpgradedIframeNavigation(start_url, cross_site_iframe_secure_url);
}

// Tests that the start URL is HTTPS upgraded for a cross site navigation.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplHttpsUpgradeBrowserTest,
                       StartUrlIsHttpsUpgradedCrossSite) {
  GURL start_url(
      embedded_test_server()->GetURL("/https_upgrade_cross_site.html"));
  GURL cross_site_iframe_secure_url("https://other.com/title1.html");

  CheckHttpsUpgradedIframeNavigation(start_url, cross_site_iframe_secure_url);
}

// Ensure that browser-initiated same-document navigations are detected and
// don't issue network requests.  See crbug.com/663777.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest,
                       SameDocumentBrowserInitiatedNoReload) {
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  GURL url_fragment_1(embedded_test_server()->GetURL("/title1.html#id_1"));
  GURL url_fragment_2(embedded_test_server()->GetURL("/title1.html#id_2"));

  // 1) Perform a new-document navigation.
  {
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);
    NavigationHandleObserver observer(shell()->web_contents(), url);
    EXPECT_TRUE(NavigateToURL(shell(), url));
    EXPECT_EQ(1, installer.will_start_called());
    EXPECT_EQ(1, installer.will_process_called());
    EXPECT_FALSE(observer.is_same_document());
  }

  // 2) Perform a same-document navigation by adding a fragment.
  {
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);
    NavigationHandleObserver observer(shell()->web_contents(), url_fragment_1);
    EXPECT_TRUE(NavigateToURL(shell(), url_fragment_1));
    EXPECT_EQ(0, installer.will_start_called());
    EXPECT_EQ(0, installer.will_process_called());
    EXPECT_TRUE(observer.is_same_document());
  }

  // 3) Perform a same-document navigation by modifying the fragment.
  {
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);
    NavigationHandleObserver observer(shell()->web_contents(), url_fragment_2);
    EXPECT_TRUE(NavigateToURL(shell(), url_fragment_2));
    EXPECT_EQ(0, installer.will_start_called());
    EXPECT_EQ(0, installer.will_process_called());
    EXPECT_TRUE(observer.is_same_document());
  }

  // 4) Redo the last navigation, but this time it should trigger a reload.
  {
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);
    NavigationHandleObserver observer(shell()->web_contents(), url_fragment_2);
    EXPECT_TRUE(NavigateToURL(shell(), url_fragment_2));
    EXPECT_EQ(1, installer.will_start_called());
    EXPECT_EQ(1, installer.will_process_called());
    EXPECT_FALSE(observer.is_same_document());
  }

  // 5) Perform a new-document navigation by removing the fragment.
  {
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);
    NavigationHandleObserver observer(shell()->web_contents(), url);
    EXPECT_TRUE(NavigateToURL(shell(), url));
    EXPECT_EQ(1, installer.will_start_called());
    EXPECT_EQ(1, installer.will_process_called());
    EXPECT_FALSE(observer.is_same_document());
  }
}

// Record and list the navigations that are started and finished.
class NavigationLogger : public WebContentsObserver {
 public:
  NavigationLogger(WebContents* web_contents)
      : WebContentsObserver(web_contents) {}

  void DidStartNavigation(NavigationHandle* navigation_handle) override {
    started_navigation_urls_.push_back(navigation_handle->GetURL());
  }

  void DidFinishNavigation(NavigationHandle* navigation_handle) override {
    finished_navigation_urls_.push_back(navigation_handle->GetURL());
  }

  const std::vector<GURL>& started_navigation_urls() const {
    return started_navigation_urls_;
  }
  const std::vector<GURL>& finished_navigation_urls() const {
    return finished_navigation_urls_;
  }

 private:
  std::vector<GURL> started_navigation_urls_;
  std::vector<GURL> finished_navigation_urls_;
};

// There was a bug without PlzNavigate that happened when a navigation was
// blocked after a redirect. Blink didn't know about the redirect and tried
// to commit an error page to the pre-redirect URL. The result was that the
// NavigationHandle was not found on the browser-side and a new NavigationHandle
// created for committing the error page. This test makes sure that only one
// NavigationHandle is used for committing the error page.
// See https://crbug.com/695421
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, BlockedOnRedirect) {
  // Returning BLOCK_REQUEST is not supported yet without PlzNavigate. It will
  // call a CHECK(false).
  // TODO(arthursonzogni) Provide support for BLOCK_REQUEST without PlzNavigate
  // once https://crbug.com/695421 is fixed.
  if (!IsBrowserSideNavigationEnabled())
    return;

  const GURL kUrl = embedded_test_server()->GetURL("/title1.html");
  const GURL kRedirectingUrl =
      embedded_test_server()->GetURL("/server-redirect?" + kUrl.spec());

  // Set up a NavigationThrottle that will block the navigation in
  // WillRedirectRequest.
  TestNavigationThrottleInstaller installer(
      shell()->web_contents(), NavigationThrottle::PROCEED,
      NavigationThrottle::BLOCK_REQUEST, NavigationThrottle::PROCEED);
  NavigationHandleObserver observer(shell()->web_contents(), kRedirectingUrl);
  NavigationLogger logger(shell()->web_contents());

  // Try to navigate to the url. The navigation should be canceled and the
  // NavigationHandle should have the right error code.
  EXPECT_FALSE(NavigateToURL(shell(), kRedirectingUrl));
  // EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, observer.net_error_code());

  // Only one navigation is expected to happen.
  std::vector<GURL> started_navigation = {kRedirectingUrl};
  EXPECT_EQ(started_navigation, logger.started_navigation_urls());

  std::vector<GURL> finished_navigation = {kUrl};
  EXPECT_EQ(finished_navigation, logger.finished_navigation_urls());
}

// Tests that when a navigation starts while there's an existing one, the first
// one has the right error code set on its navigation handle.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, ErrorCodeOnCancel) {
  GURL slow_url = embedded_test_server()->GetURL("/slow?60");
  NavigationHandleObserver observer(shell()->web_contents(), slow_url);
  shell()->LoadURL(slow_url);

  GURL url2(embedded_test_server()->GetURL("/title1.html"));
  TestNavigationObserver same_tab_observer(
      shell()->web_contents(), 1);
  shell()->LoadURL(url2);
  same_tab_observer.Wait();

  EXPECT_EQ(net::ERR_ABORTED, observer.net_error_code());
}

// Tests that when a renderer-initiated request redirects to a URL that the
// renderer can't access, the right error code is set on the NavigationHandle.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, ErrorCodeOnRedirect) {
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  GURL redirect_url = embedded_test_server()->GetURL(
      std::string("/server-redirect?") + kChromeUINetworkErrorsListingURL);
  NavigationHandleObserver observer(shell()->web_contents(), redirect_url);
  TestNavigationObserver same_tab_observer(shell()->web_contents(), 1);
  EXPECT_TRUE(
      ExecuteScript(shell(), base::StringPrintf("location.href = '%s';",
                                                redirect_url.spec().c_str())));
  same_tab_observer.Wait();
  EXPECT_EQ(net::ERR_ABORTED, observer.net_error_code());
}

// Tests that when a navigation is aborted (i.e. because of beforeunload), the
// right error code is set on the NavigationHandle.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest,
                       ErrorCodeOnAbortedNavigation) {
  // Without PlzNavigate, NavigationHandles aren't created until after the
  // beforeunload handler runs.
  if (!IsBrowserSideNavigationEnabled())
    return;
  GURL url(
      embedded_test_server()->GetURL("/render_frame_host/beforeunload.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  GURL new_url("/title1.html");
  NavigationHandleObserver observer(shell()->web_contents(), new_url);
  TestNavigationManager navigation_waiter(shell()->web_contents(), new_url);
  PrepContentsForBeforeUnloadTest(shell()->web_contents());
  SetShouldProceedOnBeforeUnload(shell(), false);

  shell()->LoadURL(new_url);
  WaitForAppModalDialog(shell());
  static_cast<WebContentsImpl*>(shell()->web_contents())
      ->CancelModalDialogsForRenderManager();
  navigation_waiter.WaitForNavigationFinished();
  EXPECT_EQ(net::ERR_ABORTED, observer.net_error_code());
}

// This class allows running tests with PlzNavigate enabled, regardless of
// default test configuration.
class PlzNavigateNavigationHandleImplBrowserTest : public ContentBrowserTest {
 public:
  PlzNavigateNavigationHandleImplBrowserTest() {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kEnableBrowserSideNavigation);
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
  }
};

// Test to verify that error pages caused by NavigationThrottle blocking a
// request from being made are properly committed in the original process
// that requested the navigation.
IN_PROC_BROWSER_TEST_F(PlzNavigateNavigationHandleImplBrowserTest,
                       ErrorPageBlockedNavigation) {
  SetupCrossSiteRedirector(embedded_test_server());
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL start_url(embedded_test_server()->GetURL("foo.com", "/title1.html"));
  GURL blocked_url(embedded_test_server()->GetURL("bar.com", "/title2.html"));

  {
    NavigationHandleObserver observer(shell()->web_contents(), start_url);
    EXPECT_TRUE(NavigateToURL(shell(), start_url));
    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
  }

  scoped_refptr<SiteInstance> site_instance =
      shell()->web_contents()->GetMainFrame()->GetSiteInstance();

  auto installer = base::MakeUnique<TestNavigationThrottleInstaller>(
      shell()->web_contents(), NavigationThrottle::BLOCK_REQUEST,
      NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);

  {
    // A blocked, renderer-initiated navigation should commit an error page
    // in the process that originated the navigation.
    NavigationHandleObserver observer(shell()->web_contents(), blocked_url);
    TestNavigationObserver navigation_observer(shell()->web_contents(), 1);
    EXPECT_TRUE(
        ExecuteScript(shell(), base::StringPrintf("location.href = '%s'",
                                                  blocked_url.spec().c_str())));
    navigation_observer.Wait();
    EXPECT_TRUE(observer.has_committed());
    EXPECT_TRUE(observer.is_error());
    EXPECT_EQ(site_instance,
              shell()->web_contents()->GetMainFrame()->GetSiteInstance());
  }

  {
    // Reloading the page should not transfer processes.
    NavigationHandleObserver observer(shell()->web_contents(), blocked_url);
    TestNavigationObserver navigation_observer(shell()->web_contents(), 1);

    shell()->Reload();
    navigation_observer.Wait();
    EXPECT_TRUE(observer.has_committed());
    EXPECT_TRUE(observer.is_error());
    EXPECT_EQ(site_instance,
              shell()->web_contents()->GetMainFrame()->GetSiteInstance());
  }

  installer.reset();

  {
    // With the throttle uninstalled, going back should return to |start_url| in
    // the same process, and clear the error page.
    NavigationHandleObserver observer(shell()->web_contents(), start_url);
    TestNavigationObserver navigation_observer(shell()->web_contents(), 1);

    shell()->GoBackOrForward(-1);
    navigation_observer.Wait();
    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_EQ(site_instance,
              shell()->web_contents()->GetMainFrame()->GetSiteInstance());
  }

  installer = base::MakeUnique<TestNavigationThrottleInstaller>(
      shell()->web_contents(), NavigationThrottle::BLOCK_REQUEST,
      NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);

  {
    // A blocked, browser-initiated navigation should commit an error page in a
    // different process.
    NavigationHandleObserver observer(shell()->web_contents(), blocked_url);
    TestNavigationObserver navigation_observer(shell()->web_contents(), 1);

    EXPECT_FALSE(NavigateToURL(shell(), blocked_url));

    navigation_observer.Wait();
    EXPECT_TRUE(observer.has_committed());
    EXPECT_TRUE(observer.is_error());
    EXPECT_NE(site_instance,
              shell()->web_contents()->GetMainFrame()->GetSiteInstance());
  }
}

// Test to verify that error pages caused by network error or other
// recoverable error are properly committed in the process for the
// destination URL.
IN_PROC_BROWSER_TEST_F(PlzNavigateNavigationHandleImplBrowserTest,
                       ErrorPageNetworkError) {
  SetupCrossSiteRedirector(embedded_test_server());
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL start_url(embedded_test_server()->GetURL("foo.com", "/title1.html"));
  GURL error_url(
      net::URLRequestFailedJob::GetMockHttpUrl(net::ERR_CONNECTION_RESET));
  EXPECT_NE(start_url.host(), error_url.host());
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                          base::Bind(&net::URLRequestFailedJob::AddUrlHandler));

  {
    NavigationHandleObserver observer(shell()->web_contents(), start_url);
    EXPECT_TRUE(NavigateToURL(shell(), start_url));
    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
  }

  scoped_refptr<SiteInstance> site_instance =
      shell()->web_contents()->GetMainFrame()->GetSiteInstance();
  {
    NavigationHandleObserver observer(shell()->web_contents(), error_url);
    EXPECT_FALSE(NavigateToURL(shell(), error_url));
    EXPECT_TRUE(observer.has_committed());
    EXPECT_TRUE(observer.is_error());
    EXPECT_NE(site_instance,
              shell()->web_contents()->GetMainFrame()->GetSiteInstance());
  }
}

}  // namespace content
