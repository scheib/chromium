// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_browser_main_android.h"

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "base/task_scheduler/post_task.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/android/mojo/chrome_interface_registrar_android.h"
#include "chrome/browser/android/preferences/clipboard_android.h"
#include "chrome/browser/android/seccomp_support_detector.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/descriptors_android.h"
#include "components/crash/content/app/breakpad_linux.h"
#include "components/crash/content/browser/crash_dump_manager_android.h"
#include "components/signin/core/browser/signin_manager.h"
#include "content/public/browser/android/compositor.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/main_function_params.h"
#include "net/android/network_change_notifier_factory_android.h"
#include "net/base/network_change_notifier.h"
#include "ui/base/resource/resource_bundle_android.h"
#include "ui/base/ui_base_paths.h"

namespace {

void DeleteFileTask(
    const base::FilePath& file_path) {
  if (base::PathExists(file_path))
    base::DeleteFile(file_path, false);
}

} // namespace

ChromeBrowserMainPartsAndroid::ChromeBrowserMainPartsAndroid(
    const content::MainFunctionParams& parameters)
    : ChromeBrowserMainParts(parameters) {
}

ChromeBrowserMainPartsAndroid::~ChromeBrowserMainPartsAndroid() {
}

int ChromeBrowserMainPartsAndroid::PreCreateThreads() {
  TRACE_EVENT0("startup", "ChromeBrowserMainPartsAndroid::PreCreateThreads")

  // The CrashDumpManager must be registered before any child process is
  // created (as it needs to be notified during child process
  // creation). Such processes are created on the PROCESS_LAUNCHER
  // thread, and so the observer is initialized and the manager
  // registered before that thread is created.
  breakpad::CrashDumpObserver::Create();

#if defined(GOOGLE_CHROME_BUILD)
  // TODO(jcivelli): we should not initialize the crash-reporter when it was not
  // enabled. Right now if it is disabled we still generate the minidumps but we
  // do not upload them.
  bool breakpad_enabled = true;
#else
  bool breakpad_enabled = false;
#endif

  // Allow Breakpad to be enabled in Chromium builds for testing purposes.
  if (!breakpad_enabled)
    breakpad_enabled = base::CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kEnableCrashReporterForTesting);

  if (breakpad_enabled) {
    base::FilePath crash_dump_dir;
    PathService::Get(chrome::DIR_CRASH_DUMPS, &crash_dump_dir);
    breakpad::CrashDumpObserver::GetInstance()->RegisterClient(
        base::MakeUnique<breakpad::CrashDumpManager>(
            crash_dump_dir, kAndroidMinidumpDescriptor));
  }

  // Auto-detect based on en-US whether locale .pak files are store uncompressed
  // (monochrome) vs extracted (non-monochrome).
  ui::SetLocalePaksStoredInApk(
      !ui::GetPathForAndroidLocalePakWithinApk("en-US").empty());

  return ChromeBrowserMainParts::PreCreateThreads();
}

void ChromeBrowserMainPartsAndroid::PostProfileInit() {
  ChromeBrowserMainParts::PostProfileInit();

  // Previously we stored information related to salient images for bookmarks
  // in a local file. We replaced the salient images with favicons. As part
  // of the clean up, the local file needs to be deleted. See crbug.com/499415.
  base::FilePath bookmark_image_file_path =
      profile()->GetPath().Append("BookmarkImageAndUrlStore.db");
  content::BrowserThread::PostDelayedTask(
      content::BrowserThread::FILE, FROM_HERE,
      base::Bind(&DeleteFileTask, bookmark_image_file_path),
      base::TimeDelta::FromMinutes(1));

  // Idempotent.  Needs to be called once on startup.  If
  // InitializeClipboardAndroidFromLocalState() is called multiple times (e.g.,
  // once per profile load), that's okay; the additional calls don't change
  // anything.
  android::InitClipboardAndroidFromLocalState(g_browser_process->local_state());

  // Start watching the preferences that need to be backed up backup using
  // Android backup, so that we create a new backup if they change.
  backup_watcher_.reset(new chrome::android::ChromeBackupWatcher(profile()));
}

void ChromeBrowserMainPartsAndroid::PreEarlyInitialization() {
  TRACE_EVENT0("startup",
    "ChromeBrowserMainPartsAndroid::PreEarlyInitialization")
  net::NetworkChangeNotifier::SetFactory(
      new net::NetworkChangeNotifierFactoryAndroid());

  content::Compositor::Initialize();

  // Chrome on Android does not use default MessageLoop. It has its own
  // Android specific MessageLoop.
  DCHECK(!main_message_loop_.get());

  // Create and start the MessageLoop.
  // This is a critical point in the startup process.
  {
    TRACE_EVENT0("startup",
      "ChromeBrowserMainPartsAndroid::PreEarlyInitialization:CreateUiMsgLoop");
    main_message_loop_.reset(new base::MessageLoopForUI);
  }

  {
    TRACE_EVENT0("startup",
      "ChromeBrowserMainPartsAndroid::PreEarlyInitialization:StartUiMsgLoop");
    base::MessageLoopForUI::current()->Start();
  }

  ChromeBrowserMainParts::PreEarlyInitialization();
}

void ChromeBrowserMainPartsAndroid::PostBrowserStart() {
  ChromeBrowserMainParts::PostBrowserStart();

  base::PostDelayedTaskWithTraits(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BACKGROUND},
      base::Bind(&ReportSeccompSupport), base::TimeDelta::FromMinutes(1));

  RegisterChromeJavaMojoInterfaces();
}

void ChromeBrowserMainPartsAndroid::ShowMissingLocaleMessageBox() {
  NOTREACHED();
}
