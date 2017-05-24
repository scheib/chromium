// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/extensions/test_extension_dir.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/extensions/app_launch_params.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/browser/ui/extensions/hosted_app_browser_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

#if defined(OS_MACOSX)
#include "chrome/common/chrome_features.h"
#endif

using content::WebContents;
using extensions::Extension;

namespace {

// Used by ShouldLocationBarForXXX. Performs a navigation and then checks that
// the location bar visibility is as expcted.
void NavigateAndCheckForLocationBar(Browser* browser,
                                    const std::string& url_string,
                                    bool expected_visibility) {
  GURL url(url_string);
  ui_test_utils::NavigateToURL(browser, url);
  EXPECT_EQ(expected_visibility,
      browser->hosted_app_controller()->ShouldShowLocationBar());
}

}  // namespace

class HostedAppTest : public ExtensionBrowserTest {
 public:
  HostedAppTest() : app_browser_(nullptr) {}
  ~HostedAppTest() override {}

  // testing::Test:
  void SetUp() override {
#if defined(OS_MACOSX)
    scoped_feature_list_.InitAndEnableFeature(features::kBookmarkApps);
#endif
    ExtensionBrowserTest::SetUp();
  }

 protected:
  void SetupApp(const std::string& app_folder, bool is_bookmark_app) {
    SetupApp(test_data_dir_.AppendASCII(app_folder), is_bookmark_app);
  }

  void SetupApp(const base::FilePath& app_folder, bool is_bookmark_app) {
    const Extension* app = InstallExtensionWithSourceAndFlags(
        app_folder, 1, extensions::Manifest::INTERNAL,
        is_bookmark_app ? extensions::Extension::FROM_BOOKMARK
                        : extensions::Extension::NO_FLAGS);
    ASSERT_TRUE(app);

    // Launch it in a window.
    ASSERT_TRUE(OpenApplication(AppLaunchParams(
        browser()->profile(), app, extensions::LAUNCH_CONTAINER_WINDOW,
        WindowOpenDisposition::NEW_WINDOW, extensions::SOURCE_TEST)));

    for (auto* b : *BrowserList::GetInstance()) {
      if (b == browser())
        continue;

      std::string browser_app_id =
          web_app::GetExtensionIdFromApplicationName(b->app_name());
      if (browser_app_id == app->id()) {
        app_browser_ = b;
        break;
      }
    }

    ASSERT_TRUE(app_browser_);
    ASSERT_TRUE(app_browser_ != browser());
  }

  void SetUpOnMainThread() override {
    ExtensionBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
  }

  Browser* app_browser_;

 private:
  base::test::ScopedFeatureList scoped_feature_list_;

  DISALLOW_COPY_AND_ASSIGN(HostedAppTest);
};

// Check that the location bar is shown correctly for bookmark apps.
IN_PROC_BROWSER_TEST_F(HostedAppTest,
                       ShouldShowLocationBarForBookmarkApp) {
  SetupApp("app", true);

  // Navigate to the app's launch page; the location bar should be hidden.
  NavigateAndCheckForLocationBar(
      app_browser_, "http://www.example.com/empty.html", false);

  // Navigate to another page on the same origin; the location bar should still
  // hidden.
  NavigateAndCheckForLocationBar(
      app_browser_, "http://www.example.com/blah", false);

  // Navigate to different origin; the location bar should now be visible.
  NavigateAndCheckForLocationBar(
      app_browser_, "http://www.foo.com/blah", true);
}

// Check that the location bar is shown correctly for HTTP bookmark apps when
// they navigate to a HTTPS page on the same origin.
IN_PROC_BROWSER_TEST_F(HostedAppTest,
                       ShouldShowLocationBarForHTTPBookmarkApp) {
  SetupApp("app", true);

  // Navigate to the app's launch page; the location bar should be hidden.
  NavigateAndCheckForLocationBar(
      app_browser_, "http://www.example.com/empty.html", false);

  // Navigate to the https version of the site; the location bar should
  // be hidden.
  NavigateAndCheckForLocationBar(
      app_browser_, "https://www.example.com/blah", false);
}

// Check that the location bar is shown correctly for HTTPS bookmark apps when
// they navigate to a HTTP page on the same origin.
IN_PROC_BROWSER_TEST_F(HostedAppTest,
                       ShouldShowLocationBarForHTTPSBookmarkApp) {
  SetupApp("https_app", true);

  // Navigate to the app's launch page; the location bar should be hidden.
  NavigateAndCheckForLocationBar(
      app_browser_, "https://www.example.com/empty.html", false);

  // Navigate to the http version of the site; the location bar should
  // be visible for the https version as it is now on a less secure version
  // of its host.
  NavigateAndCheckForLocationBar(
      app_browser_, "http://www.example.com/blah", true);
}

// Check that the location bar is shown correctly for normal hosted apps.
IN_PROC_BROWSER_TEST_F(HostedAppTest,
                       ShouldShowLocationBarForHostedApp) {
  SetupApp("app", false);

  // Navigate to the app's launch page; the location bar should be hidden.
  NavigateAndCheckForLocationBar(
      app_browser_, "http://www.example.com/empty.html", false);

  // Navigate to another page on the same origin; the location bar should still
  // hidden.
  NavigateAndCheckForLocationBar(
      app_browser_, "http://www.example.com/blah", false);

  // Navigate to different origin; the location bar should now be visible.
  NavigateAndCheckForLocationBar(
      app_browser_, "http://www.foo.com/blah", true);
}

// Check that the location bar is shown correctly for hosted apps that specify
// start URLs without the 'www.' prefix.
IN_PROC_BROWSER_TEST_F(HostedAppTest,
                       LocationBarForHostedAppWithoutWWW) {
  SetupApp("app_no_www", false);

  // Navigate to the app's launch page; the location bar should be hidden.
  NavigateAndCheckForLocationBar(
      app_browser_, "http://example.com/empty.html", false);

  // Navigate to the app's launch page with the 'www.' prefis; the location bar
  // should be hidden.
  NavigateAndCheckForLocationBar(
      app_browser_, "http://www.example.com/empty.html", false);

  // Navigate to different origin; the location bar should now be visible.
  NavigateAndCheckForLocationBar(
      app_browser_, "http://www.foo.com/blah", true);
}

// Check that a subframe on a regular web page can navigate to a URL that
// redirects to a hosted app.  https://crbug.com/721949.
IN_PROC_BROWSER_TEST_F(HostedAppTest, SubframeRedirectsToHostedApp) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Set up an app which covers app.com URLs.
  GURL app_url = embedded_test_server()->GetURL("app.com", "/title1.html");
  extensions::TestExtensionDir test_app_dir;
  test_app_dir.WriteManifest(base::StringPrintf(
      R"( { "name": "Hosted App",
            "version": "1",
            "manifest_version": 2,
            "app": {
              "launch": {
                "web_url": "%s"
              },
              "urls": ["*://app.com/"]
            }
          } )",
      app_url.spec().c_str()));
  SetupApp(test_app_dir.UnpackedPath(), false);

  // Navigate a regular tab to a page with a subframe.
  GURL url = embedded_test_server()->GetURL("foo.com", "/iframe.html");
  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ui_test_utils::NavigateToURL(browser(), url);

  // Navigate the subframe to a URL that redirects to a URL in the hosted app's
  // web extent.
  GURL redirect_url = embedded_test_server()->GetURL(
      "bar.com", "/server-redirect?" + app_url.spec());
  EXPECT_TRUE(NavigateIframeToURL(tab, "test", redirect_url));

  // Ensure that the frame navigated successfully and that it has correct
  // content.
  content::RenderFrameHost* subframe =
      content::ChildFrameAt(tab->GetMainFrame(), 0);
  EXPECT_EQ(app_url, subframe->GetLastCommittedURL());
  std::string result;
  EXPECT_TRUE(ExecuteScriptAndExtractString(
      subframe, "window.domAutomationController.send(document.body.innerText);",
      &result));
  EXPECT_EQ("This page has no title.", result);
}

class HostedAppVsTdiTest : public HostedAppTest {
 public:
  HostedAppVsTdiTest() {}
  ~HostedAppVsTdiTest() override {}

  void SetUpOnMainThread() override {
    scoped_feature_list_.InitAndEnableFeature(features::kTopDocumentIsolation);
    HostedAppTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;

  DISALLOW_COPY_AND_ASSIGN(HostedAppVsTdiTest);
};

// Tests that even with --top-document-isolation, app.site.com (covered by app's
// web extent) main frame and foo.site.com (not covered by app's web extent)
// share the same renderer process.  See also https://crbug.com/679011.
//
// Relevant frames in the test:
// - |app| - app.site.com/frame_tree/cross_origin_but_same_site_frames.html
//           Main frame, launch URL of the hosted app (i.e. app.launch.web_url).
// - |same_dir| - app.site.com/frame_tree/simple.htm
//                Another URL, but still covered by hosted app's web extent
//                (i.e. by app.urls).
// - |diff_dir| - app.site.com/save_page/a.htm
//                Same origin as |same_dir| and |app|, but not covered by app's
//                extent.
// - |same_site| - other.site.com/title1.htm
//                 Different origin, but same site as |app|, |same_dir|,
//                 |diff_dir|.
// - |cross_site| - cross.domain.com/title1.htm
//                  Cross-site from all the other frames.
//
// Verifications of |*_site| (e.g. EXPECT_EQ(same_dir_site, app_site) are
// sanity checks of the test setup.
//
// First real verification in the test is whether |same_dir| and |diff_dir| can
// script each other (despite their effective URLs being cross-site, because
// |same_dir| is mapped to a chrome-extension URL).  This was a functionality
// problem caused by https://crbug.com/679011.
//
// The test also verifies that all same-site frames (i.e. |app|, |same_dir|,
// |diff_dir|, |same_site|) share the same renderer process.  This was a small
// performance problem caused by https://crbug.com/679011.
IN_PROC_BROWSER_TEST_F(HostedAppVsTdiTest, ProcessAllocation) {
  // Setup and launch the hosted app.
  GURL url = embedded_test_server()->GetURL(
      "app.site.com", "/frame_tree/cross_origin_but_same_site_frames.html");

  extensions::TestExtensionDir test_app_dir;
  test_app_dir.WriteManifest(base::StringPrintf(
      R"( { "name": "Hosted App vs TDI Test",
            "version": "1",
            "manifest_version": 2,
            "app": {
              "launch": {
                "web_url": "%s"
              },
              "urls": ["*://app.site.com/frame_tree"]
            }
          } )",
      url.spec().c_str()));
  SetupApp(test_app_dir.UnpackedPath(), false);

  content::WebContents* web_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  content::WaitForLoadStop(web_contents);

  auto find_frame = [web_contents](const std::string& name) {
    return content::FrameMatchingPredicate(
        web_contents, base::Bind(&content::FrameMatchesName, name));
  };
  content::RenderFrameHost* app = web_contents->GetMainFrame();
  content::RenderFrameHost* same_dir = find_frame("SameOrigin-SamePath");
  content::RenderFrameHost* diff_dir = find_frame("SameOrigin-DifferentPath");
  content::RenderFrameHost* same_site = find_frame("OtherSubdomain-SameSite");
  content::RenderFrameHost* cross_site = find_frame("CrossSite");

  // Sanity-check sites of all relevant frames to verify test setup.
  GURL app_site = content::SiteInstance::GetSiteForURL(
      app_browser_->profile(), app->GetLastCommittedURL());
  EXPECT_EQ(extensions::kExtensionScheme, app_site.scheme());

  GURL same_dir_site = content::SiteInstance::GetSiteForURL(
      app_browser_->profile(), same_dir->GetLastCommittedURL());
  EXPECT_EQ(extensions::kExtensionScheme, same_dir_site.scheme());
  EXPECT_EQ(same_dir_site, app_site);

  GURL diff_dir_site = content::SiteInstance::GetSiteForURL(
      app_browser_->profile(), diff_dir->GetLastCommittedURL());
  EXPECT_NE(extensions::kExtensionScheme, diff_dir_site.scheme());
  EXPECT_NE(diff_dir_site, app_site);

  GURL same_site_site = content::SiteInstance::GetSiteForURL(
      app_browser_->profile(), same_site->GetLastCommittedURL());
  EXPECT_NE(extensions::kExtensionScheme, same_site_site.scheme());
  EXPECT_NE(same_site_site, app_site);
  EXPECT_EQ(same_site_site, diff_dir_site);

  GURL cross_site_site = content::SiteInstance::GetSiteForURL(
      app_browser_->profile(), cross_site->GetLastCommittedURL());
  EXPECT_NE(cross_site_site, app_site);
  EXPECT_NE(cross_site_site, same_site_site);

  // Verify that |same_dir| and |diff_dir| have the same origin according to
  // |window.origin| (even though they have different |same_dir_site| and
  // |diff_dir_site|).
  std::string same_dir_origin;
  EXPECT_TRUE(content::ExecuteScriptAndExtractString(
      same_dir, "domAutomationController.send(window.origin)",
      &same_dir_origin));

  std::string diff_dir_origin;
  EXPECT_TRUE(content::ExecuteScriptAndExtractString(
      diff_dir, "domAutomationController.send(window.origin)",
      &diff_dir_origin));

  EXPECT_EQ(diff_dir_origin, same_dir_origin);

  // Verify scriptability and process placement.
  EXPECT_EQ(same_dir->GetProcess(), app->GetProcess());
  if (!content::AreAllSitesIsolatedForTesting()) {
    // Verify that |same_dir| and |diff_dir| can script each other.
    // (they should - they have the same origin).
    std::string inner_text_from_other_frame;
    ASSERT_TRUE(content::ExecuteScriptAndExtractString(
        diff_dir,
        R"( var w = window.open('', 'SameOrigin-SamePath');
            domAutomationController.send(w.document.body.innerText); )",
        &inner_text_from_other_frame));
    EXPECT_EQ("Simple test page.", inner_text_from_other_frame);

    // Verify there are no additional processes for same-site frames.
    EXPECT_EQ(diff_dir->GetProcess(), app->GetProcess());
    EXPECT_EQ(same_site->GetProcess(), app->GetProcess());
    // TODO(lukasza): https://crbug.com/718516: For now it is okay for
    // |cross_site| to be in any process, but we should probably revisit
    // before launching --top-document-isolation more broadly.
  } else {
    // TODO(lukasza): https://crbug.com/718516: Process policy is not
    // well-defined / settled wrt relationship between 1) hosted apps and 2)
    // same-site web content outside of hosted app's extent.  When this test was
    // authored --site-per-process would put |app| in a separate renderer
    // process from |diff_dir| and |same_site|, even though such process
    // placement can be problematic (if |app| tries to synchronously script
    // |diff_dir| and/or |same_site|).
  }
}
