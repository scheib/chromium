// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/favicon/content/content_favicon_driver.h"

#include "base/location.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/test/scoped_feature_list.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/favicon/core/favicon_handler.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon/core/features.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/resource_dispatcher_host.h"
#include "content/public/browser/resource_dispatcher_host_delegate.h"
#include "net/base/load_flags.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/url_request/url_request.h"
#include "url/url_constants.h"

namespace {

// Tracks whether the URL passed to the constructor is requested and whether
// the request bypasses the cache.
class TestResourceDispatcherHostDelegate
    : public content::ResourceDispatcherHostDelegate {
 public:
  explicit TestResourceDispatcherHostDelegate(const GURL& url)
      : url_(url), was_requested_(false), bypassed_cache_(false) {}
  ~TestResourceDispatcherHostDelegate() override {}

  void Reset() {
    was_requested_ = false;
    bypassed_cache_ = false;
  }

  // Resturns whether |url_| was requested.
  bool was_requested() const { return was_requested_; }

  // Returns whether any of the requests bypassed the HTTP cache.
  bool bypassed_cache() const { return bypassed_cache_; }

 private:
  // content::ResourceDispatcherHostDelegate:
  bool ShouldBeginRequest(const std::string& method,
                          const GURL& url,
                          content::ResourceType resource_type,
                          content::ResourceContext* resource_context) override {
    return true;
  }

  void RequestBeginning(net::URLRequest* request,
                        content::ResourceContext* resource_context,
                        content::AppCacheService* appcache_service,
                        content::ResourceType resource_type,
                        std::vector<std::unique_ptr<content::ResourceThrottle>>*
                            throttles) override {
    if (request->url() == url_) {
      was_requested_ = true;
      if (request->load_flags() & net::LOAD_BYPASS_CACHE)
        bypassed_cache_ = true;
    }
  }

 private:
  GURL url_;
  bool was_requested_;
  bool bypassed_cache_;

  DISALLOW_COPY_AND_ASSIGN(TestResourceDispatcherHostDelegate);
};

// Checks whether the FaviconDriver is waiting for a download to complete or
// for data from the FaviconService.
class FaviconDriverPendingTaskChecker {
 public:
  virtual ~FaviconDriverPendingTaskChecker() {}

  virtual bool HasPendingTasks() = 0;
};

// Waits for the following the finish:
// - The pending navigation.
// - FaviconHandler's pending favicon database requests.
// - FaviconHandler's pending downloads.
class PendingTaskWaiter : public content::NotificationObserver {
 public:
  PendingTaskWaiter(content::WebContents* web_contents,
                    FaviconDriverPendingTaskChecker* checker)
      : checker_(checker),
        load_stopped_(false),
        weak_factory_(this) {
    registrar_.Add(this, content::NOTIFICATION_LOAD_STOP,
                   content::Source<content::NavigationController>(
                       &web_contents->GetController()));
  }
  ~PendingTaskWaiter() override {}

  void Wait() {
    if (load_stopped_ && !checker_->HasPendingTasks())
      return;

    base::RunLoop run_loop;
    quit_closure_ = run_loop.QuitClosure();
    run_loop.Run();
  }

 private:
  // content::NotificationObserver:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override {
    if (type == content::NOTIFICATION_LOAD_STOP)
      load_stopped_ = true;

    // We need to poll periodically because Delegate::OnFaviconUpdated() is not
    // guaranteed to be called upon completion of the last database request /
    // download. In particular, OnFaviconUpdated() might not be called if a
    // database request confirms the data sent in the previous
    // OnFaviconUpdated() call.
    CheckStopWaitingPeriodically();
  }

  void CheckStopWaitingPeriodically() {
    EndLoopIfCanStopWaiting();
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&PendingTaskWaiter::CheckStopWaitingPeriodically,
                       weak_factory_.GetWeakPtr()),
        base::TimeDelta::FromSeconds(1));
  }

  void EndLoopIfCanStopWaiting() {
    if (!quit_closure_.is_null() &&
        load_stopped_ &&
        !checker_->HasPendingTasks()) {
      quit_closure_.Run();
    }
  }

  FaviconDriverPendingTaskChecker* checker_;  // Not owned.
  bool load_stopped_;
  base::Closure quit_closure_;
  content::NotificationRegistrar registrar_;
  base::WeakPtrFactory<PendingTaskWaiter> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(PendingTaskWaiter);
};

}  // namespace

class ContentFaviconDriverTest : public InProcessBrowserTest,
                                 public FaviconDriverPendingTaskChecker {
 public:
  ContentFaviconDriverTest() {}
  ~ContentFaviconDriverTest() override {}

  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  // FaviconDriverPendingTaskChecker:
  bool HasPendingTasks() override {
    return favicon::ContentFaviconDriver::FromWebContents(web_contents())
        ->HasPendingTasksForTest();
  }

  favicon_base::FaviconRawBitmapResult GetFaviconForPageURL(const GURL& url) {
    favicon::FaviconService* favicon_service =
        FaviconServiceFactory::GetForProfile(
            browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);

    std::vector<favicon_base::FaviconRawBitmapResult> results;
    base::CancelableTaskTracker tracker;
    base::RunLoop loop;
    favicon_service->GetFaviconForPageURL(
        url, favicon_base::FAVICON, /*desired_size_in_dip=*/0,
        base::Bind(
            [](std::vector<favicon_base::FaviconRawBitmapResult>* save_results,
               base::RunLoop* loop,
               const std::vector<favicon_base::FaviconRawBitmapResult>&
                   results) {
              *save_results = results;
              loop->Quit();
            },
            &results, &loop),
        &tracker);
    loop.Run();
    for (const favicon_base::FaviconRawBitmapResult& result : results) {
      if (result.is_valid())
        return result;
    }
    return favicon_base::FaviconRawBitmapResult();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ContentFaviconDriverTest);
};

// Test that when a user reloads a page ignoring the cache that the favicon is
// is redownloaded and (not returned from either the favicon cache or the HTTP
// cache).
IN_PROC_BROWSER_TEST_F(ContentFaviconDriverTest, ReloadBypassingCache) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = embedded_test_server()->GetURL("/favicon/page_with_favicon.html");
  GURL icon_url = embedded_test_server()->GetURL("/favicon/icon.png");

  std::unique_ptr<TestResourceDispatcherHostDelegate> delegate(
      new TestResourceDispatcherHostDelegate(icon_url));
  content::ResourceDispatcherHost::Get()->SetDelegate(delegate.get());

  // Initial visit in order to populate the cache.
  {
    PendingTaskWaiter waiter(web_contents(), this);
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), url, WindowOpenDisposition::CURRENT_TAB,
        ui_test_utils::BROWSER_TEST_NONE);
    waiter.Wait();
  }
  ASSERT_TRUE(delegate->was_requested());
  EXPECT_FALSE(delegate->bypassed_cache());
  delegate->Reset();

  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));

  // A normal visit should fetch the favicon from either the favicon database or
  // the HTTP cache.
  {
    PendingTaskWaiter waiter(web_contents(), this);
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), url, WindowOpenDisposition::CURRENT_TAB,
        ui_test_utils::BROWSER_TEST_NONE);
    waiter.Wait();
  }
  EXPECT_FALSE(delegate->bypassed_cache());
  delegate->Reset();

  // A reload ignoring the cache should refetch the favicon from the website.
  {
    PendingTaskWaiter waiter(web_contents(), this);
    chrome::ExecuteCommand(browser(), IDC_RELOAD_BYPASSING_CACHE);
    waiter.Wait();
  }
  ASSERT_TRUE(delegate->was_requested());
  EXPECT_TRUE(delegate->bypassed_cache());
}

// Test that loading a page that contains icons only in the Web Manifest causes
// those icons to be used.
IN_PROC_BROWSER_TEST_F(ContentFaviconDriverTest, LoadIconFromWebManifest) {
  base::test::ScopedFeatureList override_features;
  override_features.InitAndEnableFeature(favicon::kFaviconsFromWebManifest);

  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = embedded_test_server()->GetURL("/favicon/page_with_manifest.html");
  GURL icon_url = embedded_test_server()->GetURL("/favicon/icon.png");

  std::unique_ptr<TestResourceDispatcherHostDelegate> delegate(
      new TestResourceDispatcherHostDelegate(icon_url));
  content::ResourceDispatcherHost::Get()->SetDelegate(delegate.get());

  PendingTaskWaiter waiter(web_contents(), this);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_NONE);
  waiter.Wait();

  EXPECT_TRUE(delegate->was_requested());
}

// Test that loading a page that contains a Web Manifest without icons and a
// regular favicon in the HTML reports the icon. The regular icon is initially
// cached in the Favicon database.
IN_PROC_BROWSER_TEST_F(ContentFaviconDriverTest,
                       LoadRegularIconDespiteWebManifestWithoutIcons) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = embedded_test_server()->GetURL(
      "/favicon/page_with_manifest_without_icons.html");
  GURL icon_url = embedded_test_server()->GetURL("/favicon/icon.png");

  // Initial visit with the feature still disabled, to populate the cache.
  {
    PendingTaskWaiter waiter(web_contents(), this);
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), url, WindowOpenDisposition::CURRENT_TAB,
        ui_test_utils::BROWSER_TEST_NONE);
    waiter.Wait();
  }
  ASSERT_NE(nullptr, GetFaviconForPageURL(url).bitmap_data);

  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));

  // Enable the feature and visit the page again.
  base::test::ScopedFeatureList override_features;
  override_features.InitAndEnableFeature(favicon::kFaviconsFromWebManifest);

  {
    PendingTaskWaiter waiter(web_contents(), this);
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), url, WindowOpenDisposition::CURRENT_TAB,
        ui_test_utils::BROWSER_TEST_NONE);
    waiter.Wait();
  }

  EXPECT_NE(nullptr, GetFaviconForPageURL(url).bitmap_data);
}
