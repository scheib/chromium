// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "build/build_config.h"
#include "content/browser/dom_storage/dom_storage_context_wrapper.h"
#include "content/browser/dom_storage/local_storage_context_mojo.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/common/dom_storage/dom_storage_types.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_paths.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"

namespace content {

// This browser test is aimed towards exercising the DOMStorage system
// from end-to-end.
class DOMStorageBrowserTest : public ContentBrowserTest {
 public:
  DOMStorageBrowserTest() {}

  void SimpleTest(const GURL& test_url, bool incognito) {
    // The test page will perform tests then navigate to either
    // a #pass or #fail ref.
    Shell* the_browser = incognito ? CreateOffTheRecordBrowser() : shell();
    NavigateToURLBlockUntilNavigationsComplete(the_browser, test_url, 2);
    std::string result =
        the_browser->web_contents()->GetLastCommittedURL().ref();
    if (result != "pass") {
      std::string js_result;
      ASSERT_TRUE(ExecuteScriptAndExtractString(
          the_browser, "window.domAutomationController.send(getLog())",
          &js_result));
      FAIL() << "Failed: " << js_result;
    }
  }
};

class MojoDOMStorageBrowserTest : public DOMStorageBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    ContentBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kMojoLocalStorage);
  }

  LocalStorageContextMojo* context() {
    return static_cast<DOMStorageContextWrapper*>(
               BrowserContext::GetDefaultStoragePartition(
                   shell()->web_contents()->GetBrowserContext())
                   ->GetDOMStorageContext())
        ->mojo_state_;
  }

  void EnsureConnected() {
    base::RunLoop run_loop;
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::BindOnce(
            &LocalStorageContextMojo::RunWhenConnected,
            base::Unretained(context()),
            base::BindOnce(base::IgnoreResult(&base::TaskRunner::PostTask),
                           base::ThreadTaskRunnerHandle::Get(), FROM_HERE,
                           run_loop.QuitClosure())));
    run_loop.Run();
  }
};

static const bool kIncognito = true;
static const bool kNotIncognito = false;

IN_PROC_BROWSER_TEST_F(DOMStorageBrowserTest, SanityCheck) {
  SimpleTest(GetTestUrl("dom_storage", "sanity_check.html"), kNotIncognito);
}

IN_PROC_BROWSER_TEST_F(DOMStorageBrowserTest, SanityCheckIncognito) {
  SimpleTest(GetTestUrl("dom_storage", "sanity_check.html"), kIncognito);
}

IN_PROC_BROWSER_TEST_F(DOMStorageBrowserTest, PRE_DataPersists) {
  SimpleTest(GetTestUrl("dom_storage", "store_data.html"), kNotIncognito);
}

// http://crbug.com/654704 PRE_ tests aren't supported on Android.
#if defined(OS_ANDROID)
#define MAYBE_DataPersists DISABLED_DataPersists
#else
#define MAYBE_DataPersists DataPersists
#endif
IN_PROC_BROWSER_TEST_F(DOMStorageBrowserTest, MAYBE_DataPersists) {
  SimpleTest(GetTestUrl("dom_storage", "verify_data.html"), kNotIncognito);
}

IN_PROC_BROWSER_TEST_F(MojoDOMStorageBrowserTest, SanityCheck) {
  SimpleTest(GetTestUrl("dom_storage", "sanity_check.html"), kNotIncognito);
}

IN_PROC_BROWSER_TEST_F(MojoDOMStorageBrowserTest, SanityCheckIncognito) {
  SimpleTest(GetTestUrl("dom_storage", "sanity_check.html"), kIncognito);
}

IN_PROC_BROWSER_TEST_F(MojoDOMStorageBrowserTest, PRE_DataPersists) {
  EnsureConnected();
  SimpleTest(GetTestUrl("dom_storage", "store_data.html"), kNotIncognito);
}

IN_PROC_BROWSER_TEST_F(MojoDOMStorageBrowserTest, MAYBE_DataPersists) {
  SimpleTest(GetTestUrl("dom_storage", "verify_data.html"), kNotIncognito);
}

class DOMStorageMigrationBrowserTest : public DOMStorageBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    ContentBrowserTest::SetUpCommandLine(command_line);
    // Only enable mojo local storage if this is not a PRE_ test.
    const testing::TestInfo* test =
        testing::UnitTest::GetInstance()->current_test_info();
    if (!base::StartsWith(test->name(), "PRE_", base::CompareCase::SENSITIVE))
      command_line->AppendSwitch(switches::kMojoLocalStorage);
  }
};

IN_PROC_BROWSER_TEST_F(DOMStorageMigrationBrowserTest, PRE_DataMigrates) {
  SimpleTest(GetTestUrl("dom_storage", "store_data.html"), kNotIncognito);
}

// http://crbug.com/654704 PRE_ tests aren't supported on Android.
#if defined(OS_ANDROID)
#define MAYBE_DataMigrates DISABLED_DataMigrates
#else
#define MAYBE_DataMigrates DataMigrates
#endif
IN_PROC_BROWSER_TEST_F(DOMStorageMigrationBrowserTest, MAYBE_DataMigrates) {
  SimpleTest(GetTestUrl("dom_storage", "verify_data.html"), kNotIncognito);
}

}  // namespace content
