// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/scoped_temp_dir.h"
#include "base/macros.h"
#include "base/test/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "chrome/browser/page_load_metrics/metrics_web_contents_observer.h"
#include "chrome/browser/page_load_metrics/observers/aborts_page_load_metrics_observer.h"
#include "chrome/browser/page_load_metrics/observers/core_page_load_metrics_observer.h"
#include "chrome/browser/page_load_metrics/observers/document_write_page_load_metrics_observer.h"
#include "chrome/browser/page_load_metrics/observers/no_state_prefetch_page_load_metrics_observer.h"
#include "chrome/browser/page_load_metrics/page_load_tracker.h"
#include "chrome/browser/prerender/prerender_histograms.h"
#include "chrome/browser/prerender/prerender_origin.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/download_test_observer.h"
#include "net/base/net_errors.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/failing_http_transaction_factory.h"
#include "net/http/http_cache.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/url_request/url_request_failed_job.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"

namespace {
using IPCType =
    page_load_metrics::MetricsWebContentsObserver::TestingObserver::IPCType;

void FailAllNetworkTransactions(net::URLRequestContextGetter* getter) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  net::HttpCache* cache(
      getter->GetURLRequestContext()->http_transaction_factory()->GetCache());
  DCHECK(cache);
  std::unique_ptr<net::FailingHttpTransactionFactory> factory =
      base::MakeUnique<net::FailingHttpTransactionFactory>(cache->GetSession(),
                                                           net::ERR_FAILED);
  // Throw away old version; since this is a browser test, there is no
  // need to restore the old state.
  cache->SetHttpNetworkTransactionFactoryForTesting(std::move(factory));
}

// Waits until specified timing and metadata expectations are satisfied.
class PageLoadMetricsWaiter
    : public page_load_metrics::MetricsWebContentsObserver::TestingObserver {
 public:
  // A bitvector to express which timing fields to match on.
  enum class TimingField : int {
    FIRST_LAYOUT = 1 << 0,
    FIRST_PAINT = 1 << 1,
    FIRST_CONTENTFUL_PAINT = 1 << 2,
    FIRST_MEANINGFUL_PAINT = 1 << 3,
    STYLE_UPDATE_BEFORE_FCP = 1 << 4,
    DOCUMENT_WRITE_BLOCK_RELOAD = 1 << 5,
    LOAD_EVENT = 1 << 6
  };

  explicit PageLoadMetricsWaiter(content::WebContents* web_contents)
      : TestingObserver(web_contents) {}

  ~PageLoadMetricsWaiter() override { DCHECK_EQ(nullptr, run_loop_.get()); }

  // Add the given expectation to match on.
  void AddMainFrameExpectation(TimingField field) {
    main_frame_expected_fields_.Set(field);
  }
  void AddSubFrameExpectation(TimingField field) {
    child_frame_expected_fields_.Set(field);
  }

  // Whether the given TimingField was observed in the main frame.
  bool DidObserveInMainFrame(TimingField field) {
    return observed_main_frame_fields_.IsSet(field);
  }

  // Waits for a TimingUpdated IPC that matches the fields set by
  // |AddMainFrameExpectation| and |AddSubFrameExpectation|. All matching fields
  // must be set in a TimingUpdated IPC for it to end this wait.
  void Wait() {
    if (expectations_satisfied())
      return;

    run_loop_.reset(new base::RunLoop());
    run_loop_->Run();
    run_loop_.reset(nullptr);

    EXPECT_TRUE(expectations_satisfied());
  }

 private:
  // Manages a bitset of TimingFields.
  class TimingFieldBitSet {
   public:
    TimingFieldBitSet() {}

    // Returns whether this bitset has all bits unset.
    bool Empty() const { return bitmask_ == 0; }

    // Returns whether this bitset has the given bit set.
    bool IsSet(TimingField field) const {
      return (bitmask_ & static_cast<int>(field)) != 0;
    }

    // Sets the bit for the given |field|.
    void Set(TimingField field) { bitmask_ |= static_cast<int>(field); }

    // Merges bits set in |other| into this bitset.
    void Merge(const TimingFieldBitSet& other) { bitmask_ |= other.bitmask_; }

    // Clears all bits set in the |other| bitset.
    void ClearMatching(const TimingFieldBitSet& other) {
      bitmask_ &= ~other.bitmask_;
    }

   private:
    int bitmask_ = 0;
  };

  static TimingFieldBitSet GetMatchedBits(
      const page_load_metrics::mojom::PageLoadTiming& timing,
      const page_load_metrics::mojom::PageLoadMetadata& metadata) {
    TimingFieldBitSet matched_bits;
    if (timing.document_timing->first_layout)
      matched_bits.Set(TimingField::FIRST_LAYOUT);
    if (timing.document_timing->load_event_start)
      matched_bits.Set(TimingField::LOAD_EVENT);
    if (timing.paint_timing->first_paint)
      matched_bits.Set(TimingField::FIRST_PAINT);
    if (timing.paint_timing->first_contentful_paint)
      matched_bits.Set(TimingField::FIRST_CONTENTFUL_PAINT);
    if (timing.paint_timing->first_meaningful_paint)
      matched_bits.Set(TimingField::FIRST_MEANINGFUL_PAINT);
    if (timing.style_sheet_timing->update_style_duration_before_fcp)
      matched_bits.Set(TimingField::STYLE_UPDATE_BEFORE_FCP);
    if (metadata.behavior_flags &
        blink::WebLoadingBehaviorFlag::
            kWebLoadingBehaviorDocumentWriteBlockReload)
      matched_bits.Set(TimingField::DOCUMENT_WRITE_BLOCK_RELOAD);

    return matched_bits;
  }

  void OnTimingUpdated(
      bool is_main_frame,
      const page_load_metrics::mojom::PageLoadTiming& timing,
      const page_load_metrics::mojom::PageLoadMetadata& metadata) override {
    if (expectations_satisfied())
      return;

    TimingFieldBitSet matched_bits = GetMatchedBits(timing, metadata);
    if (is_main_frame) {
      main_frame_expected_fields_.ClearMatching(matched_bits);
      observed_main_frame_fields_.Merge(matched_bits);
    } else {
      child_frame_expected_fields_.ClearMatching(matched_bits);
    }

    if (expectations_satisfied() && run_loop_)
      run_loop_->Quit();
  }

  bool expectations_satisfied() const {
    return child_frame_expected_fields_.Empty() &&
           main_frame_expected_fields_.Empty();
  }

  std::unique_ptr<base::RunLoop> run_loop_;

  TimingFieldBitSet child_frame_expected_fields_;
  TimingFieldBitSet main_frame_expected_fields_;

  TimingFieldBitSet observed_main_frame_fields_;
};

using TimingField = PageLoadMetricsWaiter::TimingField;

class IPCTypeVerifier
    : public page_load_metrics::MetricsWebContentsObserver::TestingObserver {
 public:
  explicit IPCTypeVerifier(content::WebContents* web_contents)
      : TestingObserver(web_contents) {}

  ~IPCTypeVerifier() override { AssertValidUpdateState(); }

  void ExpectNoTimingUpdates() { expect_updates_ = false; }

  void DidReceiveTimingUpdate(IPCType type) override {
    ++num_updates_;
    const bool is_mojo_enabled =
        base::FeatureList::IsEnabled(features::kPageLoadMetricsMojofication);
    switch (type) {
      case IPCType::LEGACY:
        ASSERT_FALSE(is_mojo_enabled);
        break;
      case IPCType::MOJO:
        ASSERT_TRUE(is_mojo_enabled);
        break;
      default:
        FAIL() << "Unknown IPCType";
        break;
    }
  }

 private:
  void AssertValidUpdateState() {
    ASSERT_EQ(expect_updates_, num_updates_ > 0);
  }

  bool expect_updates_ = true;
  int num_updates_ = 0;
};

}  // namespace

class PageLoadMetricsBrowserTest : public InProcessBrowserTest,
                                   public testing::WithParamInterface<IPCType> {
 public:
  PageLoadMetricsBrowserTest() {}
  ~PageLoadMetricsBrowserTest() override {}

 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);

    // We need to set the feature state before the render process is created,
    // in order for it to inherit the feature state from the browser process.
    // SetUp() runs too early, and SetUpOnMainThread() runs too late.
    const IPCType ipc_type = GetParam();
    switch (ipc_type) {
      case IPCType::LEGACY:
        scoped_feature_list_.InitAndDisableFeature(
            features::kPageLoadMetricsMojofication);
        break;
      case IPCType::MOJO:
        scoped_feature_list_.InitAndEnableFeature(
            features::kPageLoadMetricsMojofication);
        break;
      default:
        CHECK(false) << "Unknown IPCType.";
        break;
    }
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    ipc_type_verifier_ = base::MakeUnique<IPCTypeVerifier>(web_contents);
  }

  // Force navigation to a new page, so the currently tracked page load runs its
  // OnComplete callback. You should prefer to use PageLoadMetricsWaiter, and
  // only use NavigateToUntrackedUrl for cases where the waiter isn't
  // sufficient.
  void NavigateToUntrackedUrl() {
    ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));
  }

  bool NoPageLoadMetricsRecorded() {
    // Determine whether any 'public' page load metrics are recorded. We exclude
    // 'internal' metrics as these may be recorded for debugging purposes.
    size_t total_pageload_histograms =
        histogram_tester_.GetTotalCountsForPrefix("PageLoad.").size();
    size_t total_internal_histograms =
        histogram_tester_.GetTotalCountsForPrefix("PageLoad.Internal.").size();
    DCHECK_GE(total_pageload_histograms, total_internal_histograms);
    return total_pageload_histograms - total_internal_histograms == 0;
  }

  std::unique_ptr<PageLoadMetricsWaiter> CreatePageLoadMetricsWaiter() {
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    return base::MakeUnique<PageLoadMetricsWaiter>(web_contents);
  }

  void ExpectNoTimingUpdates() { ipc_type_verifier_->ExpectNoTimingUpdates(); }

  base::test::ScopedFeatureList scoped_feature_list_;
  base::HistogramTester histogram_tester_;
  std::unique_ptr<IPCTypeVerifier> ipc_type_verifier_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PageLoadMetricsBrowserTest);
};

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, NoNavigation) {
  ExpectNoTimingUpdates();
  ASSERT_TRUE(embedded_test_server()->Start());
  EXPECT_TRUE(NoPageLoadMetricsRecorded());
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, NewPage) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_PAINT);
  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/title1.html"));
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramDomContentLoaded, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstPaint, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramParseDuration, 1);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramParseBlockedOnScriptLoad, 1);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramParseBlockedOnScriptExecution, 1);

  // Force navigation to another page, which should force logging of histograms
  // persisted at the end of the page load lifetime.
  NavigateToUntrackedUrl();
  histogram_tester_.ExpectTotalCount(internal::kHistogramTotalBytes, 1);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramPageTimingForegroundDuration, 1);

  // Verify that NoPageLoadMetricsRecorded returns false when PageLoad metrics
  // have been recorded.
  EXPECT_FALSE(NoPageLoadMetricsRecorded());
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, NoPaintForEmptyDocument) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_LAYOUT);
  waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/empty.html"));
  waiter->Wait();
  EXPECT_FALSE(waiter->DidObserveInMainFrame(TimingField::FIRST_PAINT));

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstPaint, 0);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstContentfulPaint,
                                     0);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest,
                       NoPaintForEmptyDocumentInChildFrame) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL a_url(
      embedded_test_server()->GetURL("/page_load_metrics/empty_iframe.html"));

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_LAYOUT);
  waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
  waiter->AddSubFrameExpectation(TimingField::FIRST_LAYOUT);
  waiter->AddSubFrameExpectation(TimingField::LOAD_EVENT);
  ui_test_utils::NavigateToURL(browser(), a_url);
  waiter->Wait();
  EXPECT_FALSE(waiter->DidObserveInMainFrame(TimingField::FIRST_PAINT));

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstPaint, 0);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstContentfulPaint,
                                     0);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, PaintInChildFrame) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL a_url(embedded_test_server()->GetURL("/page_load_metrics/iframe.html"));
  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_LAYOUT);
  waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
  waiter->AddSubFrameExpectation(TimingField::FIRST_PAINT);
  waiter->AddSubFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);
  ui_test_utils::NavigateToURL(browser(), a_url);
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstPaint, 1);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, PaintInMultipleChildFrames) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL a_url(embedded_test_server()->GetURL("/page_load_metrics/iframes.html"));

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_LAYOUT);
  waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
  waiter->AddSubFrameExpectation(TimingField::FIRST_PAINT);
  waiter->AddSubFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);
  ui_test_utils::NavigateToURL(browser(), a_url);
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstPaint, 1);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, PaintInMainAndChildFrame) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL a_url(embedded_test_server()->GetURL(
      "/page_load_metrics/main_frame_with_iframe.html"));

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_LAYOUT);
  waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
  waiter->AddMainFrameExpectation(TimingField::FIRST_PAINT);
  waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);
  waiter->AddSubFrameExpectation(TimingField::FIRST_PAINT);
  waiter->AddSubFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);
  ui_test_utils::NavigateToURL(browser(), a_url);
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstPaint, 1);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, SameDocumentNavigation) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_LAYOUT);
  waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/title1.html"));
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramDomContentLoaded, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 1);

  // Perform a same-document navigation. No additional metrics should be logged.
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html#hash"));
  NavigateToUntrackedUrl();

  histogram_tester_.ExpectTotalCount(internal::kHistogramDomContentLoaded, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 1);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, SameUrlNavigation) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_LAYOUT);
  waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/title1.html"));
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramDomContentLoaded, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 1);

  waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_LAYOUT);
  waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/title1.html"));
  waiter->Wait();

  // We expect one histogram sample for each navigation to title1.html.
  histogram_tester_.ExpectTotalCount(internal::kHistogramDomContentLoaded, 2);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 2);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 2);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, NonHtmlMainResource) {
  ASSERT_TRUE(embedded_test_server()->Start());

  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/circle.svg"));
  NavigateToUntrackedUrl();
  ExpectNoTimingUpdates();
  EXPECT_TRUE(NoPageLoadMetricsRecorded());
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, NonHttpOrHttpsUrl) {
  ASSERT_TRUE(embedded_test_server()->Start());

  ui_test_utils::NavigateToURL(browser(), GURL(chrome::kChromeUIVersionURL));
  NavigateToUntrackedUrl();
  ExpectNoTimingUpdates();
  EXPECT_TRUE(NoPageLoadMetricsRecorded());
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, HttpErrorPage) {
  ASSERT_TRUE(embedded_test_server()->Start());

  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/page_load_metrics/404.html"));
  NavigateToUntrackedUrl();
  ExpectNoTimingUpdates();
  EXPECT_TRUE(NoPageLoadMetricsRecorded());
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, ChromeErrorPage) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Configure the network stack to fail all attempted loads with a network
  // error, which will cause Chrome to display an error page.
  scoped_refptr<net::URLRequestContextGetter> url_request_context_getter =
      browser()->profile()->GetRequestContext();
  content::BrowserThread::PostTask(
      content::BrowserThread::IO, FROM_HERE,
      base::BindOnce(&FailAllNetworkTransactions,
                     base::RetainedRef(url_request_context_getter)));

  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/title1.html"));
  NavigateToUntrackedUrl();
  ExpectNoTimingUpdates();
  EXPECT_TRUE(NoPageLoadMetricsRecorded());
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, Ignore204Pages) {
  ASSERT_TRUE(embedded_test_server()->Start());

  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/page204.html"));
  NavigateToUntrackedUrl();
  ExpectNoTimingUpdates();
  EXPECT_TRUE(NoPageLoadMetricsRecorded());
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, IgnoreDownloads) {
  ASSERT_TRUE(embedded_test_server()->Start());

  base::ThreadRestrictions::ScopedAllowIO allow_io;
  base::ScopedTempDir downloads_directory;
  ASSERT_TRUE(downloads_directory.CreateUniqueTempDir());
  browser()->profile()->GetPrefs()->SetFilePath(
      prefs::kDownloadDefaultDirectory, downloads_directory.GetPath());
  content::DownloadTestObserverTerminal downloads_observer(
      content::BrowserContext::GetDownloadManager(browser()->profile()),
      1,  // == wait_count (only waiting for "download-test3.gif").
      content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);

  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/download-test3.gif"));
  downloads_observer.WaitForFinished();

  NavigateToUntrackedUrl();
  ExpectNoTimingUpdates();
  EXPECT_TRUE(NoPageLoadMetricsRecorded());
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, PreloadDocumentWrite) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);

  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/page_load_metrics/document_write_external_script.html"));
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(
      internal::kHistogramDocWriteParseStartToFirstContentfulPaint, 1);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, NoPreloadDocumentWrite) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/page_load_metrics/document_write_no_script.html"));
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstContentfulPaint,
                                     1);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramDocWriteParseStartToFirstContentfulPaint, 0);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, NoDocumentWrite) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);

  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/title1.html"));
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstContentfulPaint,
                                     1);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramDocWriteParseStartToFirstContentfulPaint, 0);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramDocWriteBlockParseStartToFirstContentfulPaint, 0);
  histogram_tester_.ExpectTotalCount(internal::kHistogramDocWriteBlockCount, 0);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, DocumentWriteBlock) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);

  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/page_load_metrics/document_write_script_block.html"));
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(
      internal::kHistogramDocWriteBlockParseStartToFirstContentfulPaint, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramDocWriteBlockCount, 1);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, DocumentWriteReload) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/page_load_metrics/document_write_script_block.html"));
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(
      internal::kHistogramDocWriteBlockParseStartToFirstContentfulPaint, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramDocWriteBlockCount, 1);

  // Reload should not log the histogram as the script is not blocked.
  auto reload_waiter = CreatePageLoadMetricsWaiter();
  reload_waiter->AddMainFrameExpectation(
      TimingField::DOCUMENT_WRITE_BLOCK_RELOAD);
  reload_waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/page_load_metrics/document_write_script_block.html"));
  reload_waiter->Wait();

  histogram_tester_.ExpectTotalCount(
      internal::kHistogramDocWriteBlockReloadCount, 1);

  reload_waiter = CreatePageLoadMetricsWaiter();
  reload_waiter->AddMainFrameExpectation(
      TimingField::DOCUMENT_WRITE_BLOCK_RELOAD);
  reload_waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/page_load_metrics/document_write_script_block.html"));
  reload_waiter->Wait();

  histogram_tester_.ExpectTotalCount(
      internal::kHistogramDocWriteBlockParseStartToFirstContentfulPaint, 1);

  histogram_tester_.ExpectTotalCount(
      internal::kHistogramDocWriteBlockReloadCount, 2);
  histogram_tester_.ExpectTotalCount(internal::kHistogramDocWriteBlockCount, 1);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, DocumentWriteAsync) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/page_load_metrics/document_write_async_script.html"));
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstContentfulPaint,
                                     1);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramDocWriteBlockParseStartToFirstContentfulPaint, 0);
  histogram_tester_.ExpectTotalCount(internal::kHistogramDocWriteBlockCount, 0);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, DocumentWriteSameDomain) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/page_load_metrics/document_write_external_script.html"));
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstContentfulPaint,
                                     1);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramDocWriteBlockParseStartToFirstContentfulPaint, 0);
  histogram_tester_.ExpectTotalCount(internal::kHistogramDocWriteBlockCount, 0);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, NoDocumentWriteScript) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/page_load_metrics/document_write_no_script.html"));
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstContentfulPaint,
                                     1);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramDocWriteBlockParseStartToFirstContentfulPaint, 0);
  histogram_tester_.ExpectTotalCount(internal::kHistogramDocWriteBlockCount, 0);
}

// TODO(crbug.com/712935): Flaky on Linux dbg.
#if defined(OS_LINUX) && !defined(NDEBUG)
#define MAYBE_BadXhtml DISABLED_BadXhtml
#else
#define MAYBE_BadXhtml BadXhtml
#endif
IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, MAYBE_BadXhtml) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_PAINT);

  // When an XHTML page contains invalid XML, it causes a paint of the error
  // message without a layout. Page load metrics currently treats this as an
  // error. Eventually, we'll fix this by special casing the handling of
  // documents with non-well-formed XML on the blink side. See crbug.com/627607
  // for more.
  ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("/page_load_metrics/badxml.xhtml"));

  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 0);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstPaint, 0);
  histogram_tester_.ExpectTotalCount(page_load_metrics::internal::kErrorEvents,
                                     1);
  histogram_tester_.ExpectBucketCount(
      page_load_metrics::internal::kErrorEvents,
      page_load_metrics::ERR_BAD_TIMING_IPC_INVALID_TIMING, 1);

  histogram_tester_.ExpectTotalCount(
      page_load_metrics::internal::kPageLoadTimingStatus, 1);
  histogram_tester_.ExpectBucketCount(
      page_load_metrics::internal::kPageLoadTimingStatus,
      page_load_metrics::internal::INVALID_ORDER_FIRST_LAYOUT_FIRST_PAINT, 1);
}

// Test code that aborts provisional navigations.
// TODO(csharrison): Move these to unit tests once the navigation API in content
// properly calls NavigationHandle/NavigationThrottle methods.
IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, AbortNewNavigation) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url(embedded_test_server()->GetURL("/title1.html"));
  chrome::NavigateParams params(browser(), url, ui::PAGE_TRANSITION_LINK);
  content::TestNavigationManager manager(
      browser()->tab_strip_model()->GetActiveWebContents(), url);

  chrome::Navigate(&params);
  EXPECT_TRUE(manager.WaitForRequestStart());

  GURL url2(embedded_test_server()->GetURL("/title2.html"));
  chrome::NavigateParams params2(browser(), url2,
                                 ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
  chrome::Navigate(&params2);
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(
      internal::kHistogramAbortNewNavigationBeforeCommit, 1);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, AbortReload) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url(embedded_test_server()->GetURL("/title1.html"));
  chrome::NavigateParams params(browser(), url, ui::PAGE_TRANSITION_LINK);
  content::TestNavigationManager manager(
      browser()->tab_strip_model()->GetActiveWebContents(), url);

  chrome::Navigate(&params);
  EXPECT_TRUE(manager.WaitForRequestStart());

  chrome::NavigateParams params2(browser(), url, ui::PAGE_TRANSITION_RELOAD);

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
  chrome::Navigate(&params2);
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(
      internal::kHistogramAbortReloadBeforeCommit, 1);
}

// TODO(crbug.com/675061): Flaky on Win7 dbg.
#if defined(OS_WIN)
#define MAYBE_AbortClose DISABLED_AbortClose
#else
#define MAYBE_AbortClose AbortClose
#endif
IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, MAYBE_AbortClose) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url(embedded_test_server()->GetURL("/title1.html"));
  chrome::NavigateParams params(browser(), url, ui::PAGE_TRANSITION_LINK);
  content::TestNavigationManager manager(
      browser()->tab_strip_model()->GetActiveWebContents(), url);

  chrome::Navigate(&params);
  EXPECT_TRUE(manager.WaitForRequestStart());

  browser()->tab_strip_model()->GetActiveWebContents()->Close();

  manager.WaitForNavigationFinished();

  histogram_tester_.ExpectTotalCount(internal::kHistogramAbortCloseBeforeCommit,
                                     1);
  ExpectNoTimingUpdates();
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, AbortMultiple) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url(embedded_test_server()->GetURL("/title1.html"));
  chrome::NavigateParams params(browser(), url, ui::PAGE_TRANSITION_LINK);
  content::TestNavigationManager manager(
      browser()->tab_strip_model()->GetActiveWebContents(), url);

  chrome::Navigate(&params);
  EXPECT_TRUE(manager.WaitForRequestStart());

  GURL url2(embedded_test_server()->GetURL("/title2.html"));
  chrome::NavigateParams params2(browser(), url2, ui::PAGE_TRANSITION_TYPED);
  content::TestNavigationManager manager2(
      browser()->tab_strip_model()->GetActiveWebContents(), url2);
  chrome::Navigate(&params2);

  EXPECT_TRUE(manager2.WaitForRequestStart());
  manager.WaitForNavigationFinished();

  GURL url3(embedded_test_server()->GetURL("/title3.html"));
  chrome::NavigateParams params3(browser(), url3, ui::PAGE_TRANSITION_TYPED);

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
  chrome::Navigate(&params3);
  waiter->Wait();

  manager2.WaitForNavigationFinished();

  histogram_tester_.ExpectTotalCount(
      internal::kHistogramAbortNewNavigationBeforeCommit, 2);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest,
                       NoAbortMetricsOnClientRedirect) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL first_url(embedded_test_server()->GetURL("/title1.html"));
  ui_test_utils::NavigateToURL(browser(), first_url);

  GURL second_url(embedded_test_server()->GetURL("/title2.html"));
  chrome::NavigateParams params(browser(), second_url,
                                ui::PAGE_TRANSITION_LINK);
  content::TestNavigationManager manager(
      browser()->tab_strip_model()->GetActiveWebContents(), second_url);
  chrome::Navigate(&params);
  EXPECT_TRUE(manager.WaitForRequestStart());

  {
    auto waiter = CreatePageLoadMetricsWaiter();
    waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
    content::TestNavigationManager reload_manager(
        browser()->tab_strip_model()->GetActiveWebContents(), first_url);
    EXPECT_TRUE(content::ExecuteScript(
        browser()->tab_strip_model()->GetActiveWebContents(),
        "window.location.reload();"));
    waiter->Wait();
  }

  manager.WaitForNavigationFinished();

  EXPECT_TRUE(histogram_tester_
                  .GetTotalCountsForPrefix("PageLoad.Experimental.AbortTiming.")
                  .empty());
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest,
                       FirstMeaningfulPaintRecorded) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_MEANINGFUL_PAINT);
  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/title1.html"));
  waiter->Wait();

  histogram_tester_.ExpectUniqueSample(
      internal::kHistogramFirstMeaningfulPaintStatus,
      internal::FIRST_MEANINGFUL_PAINT_RECORDED, 1);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramFirstMeaningfulPaint, 1);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramParseStartToFirstMeaningfulPaint, 1);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest,
                       FirstMeaningfulPaintNotRecorded) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);

  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/page_load_metrics/page_with_active_connections.html"));
  waiter->Wait();

  // Navigate away before a FMP is reported.
  NavigateToUntrackedUrl();

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstContentfulPaint,
                                     1);
  histogram_tester_.ExpectUniqueSample(
      internal::kHistogramFirstMeaningfulPaintStatus,
      internal::FIRST_MEANINGFUL_PAINT_DID_NOT_REACH_NETWORK_STABLE, 1);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramFirstMeaningfulPaint, 0);
  histogram_tester_.ExpectTotalCount(
      internal::kHistogramParseStartToFirstMeaningfulPaint, 0);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest,
                       NoStatePrefetchObserverCacheable) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);

  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/title1.html"));

  waiter->Wait();

  histogram_tester_.ExpectTotalCount(
      "Prerender.none_PrefetchTTFCP.Reference.NoStore.Visible", 0);
  histogram_tester_.ExpectTotalCount(
      "Prerender.none_PrefetchTTFCP.Reference.Cacheable.Visible", 1);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest,
                       NoStatePrefetchObserverNoStore) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::FIRST_CONTENTFUL_PAINT);

  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/nostore.html"));

  waiter->Wait();

  histogram_tester_.ExpectTotalCount(
      "Prerender.none_PrefetchTTFCP.Reference.NoStore.Visible", 1);
  histogram_tester_.ExpectTotalCount(
      "Prerender.none_PrefetchTTFCP.Reference.Cacheable.Visible", 0);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, CSSTiming) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::STYLE_UPDATE_BEFORE_FCP);

  // Careful: Blink code clamps timestamps to 5us, so any CSS parsing we do here
  // must take >> 5us, otherwise we'll log 0 for the value and it will remain
  // unset here.
  ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("/page_load_metrics/page_with_css.html"));
  waiter->Wait();

  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstContentfulPaint,
                                     1);
  histogram_tester_.ExpectTotalCount(
      "PageLoad.CSSTiming.Parse.BeforeFirstContentfulPaint", 1);
  histogram_tester_.ExpectTotalCount(
      "PageLoad.CSSTiming.Update.BeforeFirstContentfulPaint", 1);
  histogram_tester_.ExpectTotalCount(
      "PageLoad.CSSTiming.ParseAndUpdate.BeforeFirstContentfulPaint", 1);
}

IN_PROC_BROWSER_TEST_P(PageLoadMetricsBrowserTest, PayloadSize) {
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreatePageLoadMetricsWaiter();
  waiter->AddMainFrameExpectation(TimingField::LOAD_EVENT);
  ui_test_utils::NavigateToURL(browser(), embedded_test_server()->GetURL(
                                              "/page_load_metrics/large.html"));
  waiter->Wait();

  // Payload histograms are only logged when a page load terminates, so force
  // navigation to another page.
  NavigateToUntrackedUrl();

  histogram_tester_.ExpectTotalCount(internal::kHistogramTotalBytes, 1);

  // Verify that there is a single sample recorded in the 10kB bucket (the size
  // of the main HTML response).
  histogram_tester_.ExpectBucketCount(internal::kHistogramTotalBytes, 10, 1);
}

INSTANTIATE_TEST_CASE_P(
    /* no prefix */,
    PageLoadMetricsBrowserTest,
    testing::Values(IPCType::LEGACY, IPCType::MOJO));
