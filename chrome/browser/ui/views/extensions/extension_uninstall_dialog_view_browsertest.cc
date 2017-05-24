// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/macros.h"
#include "base/run_loop.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_uninstall_dialog.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/value_builder.h"

namespace {

const char kUninstallUrl[] = "https://www.google.com/";

const char kReferrerId[] = "chrome-remove-extension-dialog";

// A preference key storing the url loaded when an extension is uninstalled.
const char kUninstallUrlPrefKey[] = "uninstall_url";

scoped_refptr<extensions::Extension> BuildTestExtension() {
  return extensions::ExtensionBuilder()
      .SetManifest(extensions::DictionaryBuilder()
                       .Set("name", "foo")
                       .Set("version", "1.0")
                       .Build())
      .Build();
}

std::string GetActiveUrl(Browser* browser) {
  return browser->tab_strip_model()
      ->GetActiveWebContents()
      ->GetLastCommittedURL()
      .spec();
}

void SetUninstallURL(extensions::ExtensionPrefs* prefs,
                     const std::string& extension_id) {
  prefs->UpdateExtensionPref(extension_id, kUninstallUrlPrefKey,
                             base::MakeUnique<base::Value>(kUninstallUrl));
}

class TestExtensionUninstallDialogDelegate
    : public extensions::ExtensionUninstallDialog::Delegate {
 public:
  explicit TestExtensionUninstallDialogDelegate(
      const base::Closure& quit_closure)
      : quit_closure_(quit_closure), canceled_(false) {}

  ~TestExtensionUninstallDialogDelegate() override {}

  bool canceled() { return canceled_; }

 private:
  void OnExtensionUninstallDialogClosed(bool did_start_uninstall,
                                        const base::string16& error) override {
    canceled_ = !did_start_uninstall;
    quit_closure_.Run();
  }

  base::Closure quit_closure_;
  bool canceled_;

  DISALLOW_COPY_AND_ASSIGN(TestExtensionUninstallDialogDelegate);
};

}  // namespace

typedef InProcessBrowserTest ExtensionUninstallDialogViewBrowserTest;

// Test that ExtensionUninstallDialog cancels the uninstall if the Window which
// is passed to ExtensionUninstallDialog::Create() is destroyed before
// ExtensionUninstallDialogDelegateView is created.
IN_PROC_BROWSER_TEST_F(ExtensionUninstallDialogViewBrowserTest,
                       TrackParentWindowDestruction) {
  scoped_refptr<extensions::Extension> extension(BuildTestExtension());
  extensions::ExtensionSystem::Get(browser()->profile())->extension_service()
      ->AddExtension(extension.get());

  base::RunLoop run_loop;
  TestExtensionUninstallDialogDelegate delegate(run_loop.QuitClosure());
  std::unique_ptr<extensions::ExtensionUninstallDialog> dialog(
      extensions::ExtensionUninstallDialog::Create(
          browser()->profile(), browser()->window()->GetNativeWindow(),
          &delegate));
  browser()->window()->Close();
  content::RunAllPendingInMessageLoop();

  dialog->ConfirmUninstall(extension.get(),
                           extensions::UNINSTALL_REASON_FOR_TESTING,
                           extensions::UNINSTALL_SOURCE_FOR_TESTING);
  run_loop.Run();
  EXPECT_TRUE(delegate.canceled());
}

// Test that ExtensionUninstallDialog cancels the uninstall if the Window which
// is passed to ExtensionUninstallDialog::Create() is destroyed after
// ExtensionUninstallDialogDelegateView is created.
IN_PROC_BROWSER_TEST_F(ExtensionUninstallDialogViewBrowserTest,
                       TrackParentWindowDestructionAfterViewCreation) {
  scoped_refptr<extensions::Extension> extension(BuildTestExtension());
  extensions::ExtensionSystem::Get(browser()->profile())->extension_service()
      ->AddExtension(extension.get());

  base::RunLoop run_loop;
  TestExtensionUninstallDialogDelegate delegate(run_loop.QuitClosure());
  std::unique_ptr<extensions::ExtensionUninstallDialog> dialog(
      extensions::ExtensionUninstallDialog::Create(
          browser()->profile(), browser()->window()->GetNativeWindow(),
          &delegate));
  content::RunAllPendingInMessageLoop();

  dialog->ConfirmUninstall(extension.get(),
                           extensions::UNINSTALL_REASON_FOR_TESTING,
                           extensions::UNINSTALL_SOURCE_FOR_TESTING);

  content::RunAllPendingInMessageLoop();

  // Kill parent window.
  browser()->window()->Close();
  run_loop.Run();
  EXPECT_TRUE(delegate.canceled());
}

// Test that when the user clicks Uninstall on the ExtensionUninstallDialog, the
// extension's uninstall url (when it is specified) should open and be the
// active tab.
// DISABLED: Flaky on Win 10 x64
IN_PROC_BROWSER_TEST_F(
    ExtensionUninstallDialogViewBrowserTest,
    DISABLED_EnsureExtensionUninstallURLIsActiveTabAfterUninstall) {
  scoped_refptr<extensions::Extension> extension(BuildTestExtension());
  ExtensionService* extension_service =
      extensions::ExtensionSystem::Get(browser()->profile())
          ->extension_service();
  extension_service->AddExtension(extension.get());
  SetUninstallURL(
      extensions::ExtensionPrefs::Get(extension_service->GetBrowserContext()),
      extension->id());

  // Auto-confirm the uninstall dialog.
  extensions::ScopedTestDialogAutoConfirm auto_confirm(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);

  base::RunLoop run_loop;
  TestExtensionUninstallDialogDelegate delegate(run_loop.QuitClosure());
  std::unique_ptr<extensions::ExtensionUninstallDialog> dialog(
      extensions::ExtensionUninstallDialog::Create(
          browser()->profile(), browser()->window()->GetNativeWindow(),
          &delegate));
  content::RunAllPendingInMessageLoop();

  dialog->ConfirmUninstall(extension,
                           // UNINSTALL_REASON_USER_INITIATED is used to trigger
                           // complete uninstallation.
                           extensions::UNINSTALL_REASON_USER_INITIATED,
                           extensions::UNINSTALL_SOURCE_FOR_TESTING);

  content::RunAllPendingInMessageLoop();

  // There should be 2 tabs open: chrome://about and the extension's uninstall
  // url.
  EXPECT_EQ(2, browser()->tab_strip_model()->count());
  // This navigation can fail, since the uninstall url isn't hooked up to the
  // test server. That's fine, since we only care about the intended target,
  // which is valid.
  content::WaitForLoadStop(
      browser()->tab_strip_model()->GetActiveWebContents());
  // Verifying that the extension's uninstall url is the active tab.
  EXPECT_EQ(kUninstallUrl, GetActiveUrl(browser()));

  run_loop.Run();
  // The delegate should not be canceled because the user chose to uninstall the
  // extension, which should be successful.
  EXPECT_TRUE(!delegate.canceled());
}

// Test that when the user clicks the Report Abuse checkbox and clicks Uninstall
// on the ExtensionUninstallDialog, the extension's uninstall url (when it is
// specified) and the CWS Report Abuse survey are opened in the browser, also
// testing that the CWS survey is the active tab.
#if defined(OS_WIN)
// Flaky on windows: http://crbug.com/725197
#define MAYBE_EnsureCWSReportAbusePageIsActiveTabAfterUninstall \
  DISABLED_EnsureCWSReportAbusePageIsActiveTabAfterUninstall
#else
#define MAYBE_EnsureCWSReportAbusePageIsActiveTabAfterUninstall \
  EnsureCWSReportAbusePageIsActiveTabAfterUninstall
#endif
IN_PROC_BROWSER_TEST_F(
    ExtensionUninstallDialogViewBrowserTest,
    MAYBE_EnsureCWSReportAbusePageIsActiveTabAfterUninstall) {
  scoped_refptr<extensions::Extension> extension(BuildTestExtension());
  ExtensionService* extension_service =
      extensions::ExtensionSystem::Get(browser()->profile())
          ->extension_service();
  SetUninstallURL(
      extensions::ExtensionPrefs::Get(extension_service->GetBrowserContext()),
      extension->id());
  extension_service->AddExtension(extension.get());

  // Auto-confirm the uninstall dialog.
  extensions::ScopedTestDialogAutoConfirm auto_confirm(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT_AND_OPTION);

  base::RunLoop run_loop;
  TestExtensionUninstallDialogDelegate delegate(run_loop.QuitClosure());
  std::unique_ptr<extensions::ExtensionUninstallDialog> dialog(
      extensions::ExtensionUninstallDialog::Create(
          browser()->profile(), browser()->window()->GetNativeWindow(),
          &delegate));
  content::RunAllPendingInMessageLoop();

  dialog->ConfirmUninstall(extension,
                           // UNINSTALL_REASON_USER_INITIATED is used to trigger
                           // complete uninstallation.
                           extensions::UNINSTALL_REASON_USER_INITIATED,
                           extensions::UNINSTALL_SOURCE_FOR_TESTING);

  content::RunAllPendingInMessageLoop();
  // There should be 3 tabs open: chrome://about, the extension's uninstall url,
  // and the CWS Report Abuse survey.
  EXPECT_EQ(3, browser()->tab_strip_model()->count());
  // This navigation can fail, since the webstore report abuse url isn't hooked
  // up to the test server. That's fine, since we only care about the intended
  // target, which is valid.
  content::WaitForLoadStop(
      browser()->tab_strip_model()->GetActiveWebContents());
  // The CWS Report Abuse survey should be the active tab.
  EXPECT_EQ(
      extension_urls::GetWebstoreReportAbuseUrl(extension->id(), kReferrerId),
      GetActiveUrl(browser()));
  // Similar to the scenario above, this navigation can fail. The uninstall url
  // isn't hooked up to our test server.
  content::WaitForLoadStop(browser()->tab_strip_model()->GetWebContentsAt(1));
  // Verifying that the extension's uninstall url was opened. It should not be
  // the active tab.
  EXPECT_EQ(kUninstallUrl, browser()
                               ->tab_strip_model()
                               ->GetWebContentsAt(1)
                               ->GetLastCommittedURL()
                               .spec());

  run_loop.Run();
  // The delegate should not be canceled because the user chose to uninstall the
  // extension, which should be successful.
  EXPECT_TRUE(!delegate.canceled());
}
