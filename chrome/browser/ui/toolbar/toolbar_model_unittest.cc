// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/toolbar/toolbar_model.h"

#include <stddef.h>

#include "base/command_line.h"
#include "base/macros.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete_classifier_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/toolbar/toolbar_model.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/ssl_status.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/url_constants.h"
#include "ui/gfx/text_elider.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/browser/extension_registry.h"
#include "extensions/common/test_util.h"
#endif

// Test data ------------------------------------------------------------------

namespace {

struct TestItem {
  GURL url;
  base::string16 expected_text;
} test_items[] = {
    {
        GURL("view-source:http://www.google.com"),
        base::ASCIIToUTF16("view-source:www.google.com"),
    },
    {
        GURL("view-source:chrome://newtab/"),
        base::ASCIIToUTF16("view-source:chrome://newtab"),
    },
    {
        GURL("chrome-extension://fooooooooooooooooooooooooooooooo/bar.html"),
        base::ASCIIToUTF16(
            "chrome-extension://fooooooooooooooooooooooooooooooo/bar.html"),
    },
    {
        GURL(url::kAboutBlankURL), base::ASCIIToUTF16(url::kAboutBlankURL),
    },
    {
        GURL("http://searchurl/?q=tractor+supply"),
        base::ASCIIToUTF16("searchurl/?q=tractor+supply"),
    },
    {
        GURL("http://google.com/search?q=tractor+supply&espv=1"),
        base::ASCIIToUTF16("google.com/search?q=tractor+supply&espv=1"),
    },
    {
        GURL("https://google.ca/search?q=tractor+supply"),
        base::ASCIIToUTF16("https://google.ca/search?q=tractor+supply"),
    },
};

}  // namespace


// ToolbarModelTest -----------------------------------------------------------

class ToolbarModelTest : public BrowserWithTestWindowTest {
 public:
  ToolbarModelTest();
  ~ToolbarModelTest() override;

  // BrowserWithTestWindowTest:
  void SetUp() override;

 protected:
  void NavigateAndCheckText(const GURL& url,
                            const base::string16& expected_text);
  void NavigateAndCheckElided(const GURL& https_url);

 private:
  DISALLOW_COPY_AND_ASSIGN(ToolbarModelTest);
};

ToolbarModelTest::ToolbarModelTest() {
}

ToolbarModelTest::~ToolbarModelTest() {
}

void ToolbarModelTest::SetUp() {
  BrowserWithTestWindowTest::SetUp();
  AutocompleteClassifierFactory::GetInstance()->SetTestingFactoryAndUse(
      profile(), &AutocompleteClassifierFactory::BuildInstanceFor);

#if BUILDFLAG(ENABLE_EXTENSIONS)
  // Install a fake extension so that the ID in the chrome-extension test URL is
  // valid. Invalid extension URLs may result in error pages (if blocked by
  // ExtensionNavigationThrottle), which this test doesn't wish to exercise.
  ASSERT_TRUE(extensions::ExtensionRegistry::Get(profile())->AddEnabled(
      extensions::test_util::CreateEmptyExtension(
          "fooooooooooooooooooooooooooooooo")));
#endif
}

void ToolbarModelTest::NavigateAndCheckText(
    const GURL& url,
    const base::string16& expected_text) {
  // Check while loading.
  content::NavigationController* controller =
      &browser()->tab_strip_model()->GetWebContentsAt(0)->GetController();
  controller->LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_LINK,
                      std::string());
  ToolbarModel* toolbar_model = browser()->toolbar_model();
  EXPECT_EQ(expected_text, toolbar_model->GetFormattedURL(nullptr));
  EXPECT_TRUE(toolbar_model->ShouldDisplayURL());

  // Check after commit.
  CommitPendingLoad(controller);
  EXPECT_EQ(expected_text, toolbar_model->GetFormattedURL(nullptr));
  EXPECT_TRUE(toolbar_model->ShouldDisplayURL());
}

void ToolbarModelTest::NavigateAndCheckElided(const GURL& url) {
  // Check while loading.
  content::NavigationController* controller =
      &browser()->tab_strip_model()->GetWebContentsAt(0)->GetController();
  controller->LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_LINK,
                      std::string());
  ToolbarModel* toolbar_model = browser()->toolbar_model();
  const base::string16 toolbar_text_before(
      toolbar_model->GetFormattedURL(nullptr));
  EXPECT_LT(toolbar_text_before.size(), url.spec().size());
  EXPECT_TRUE(base::EndsWith(toolbar_text_before,
                             base::string16(gfx::kEllipsisUTF16),
                             base::CompareCase::SENSITIVE));
  // Check after commit.
  CommitPendingLoad(controller);
  const base::string16 toolbar_text_after(
      toolbar_model->GetFormattedURL(nullptr));
  EXPECT_LT(toolbar_text_after.size(), url.spec().size());
  EXPECT_TRUE(base::EndsWith(toolbar_text_after,
                             base::string16(gfx::kEllipsisUTF16),
                             base::CompareCase::SENSITIVE));
}

// Actual tests ---------------------------------------------------------------

// Test URL display.
TEST_F(ToolbarModelTest, ShouldDisplayURL) {
  AddTab(browser(), GURL(url::kAboutBlankURL));

  for (const TestItem& test_item : test_items) {
    NavigateAndCheckText(test_item.url, test_item.expected_text);
  }
}

TEST_F(ToolbarModelTest, ShouldElideLongURLs) {
  AddTab(browser(), GURL(url::kAboutBlankURL));
  const std::string long_text(content::kMaxURLDisplayChars + 1024, '0');
  NavigateAndCheckElided(
      GURL(std::string("https://www.foo.com/?") + long_text));
  NavigateAndCheckElided(GURL(std::string("data:abc") + long_text));
}
