// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "headless/lib/headless_content_main_delegate.h"

#include <utility>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/lazy_instance.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/trace_event/trace_event.h"
#include "components/crash/content/app/breakpad_linux.h"
#include "content/public/browser/browser_main_runner.h"
#include "content/public/common/content_switches.h"
#include "headless/lib/browser/headless_browser_impl.h"
#include "headless/lib/browser/headless_content_browser_client.h"
#include "headless/lib/headless_crash_reporter_client.h"
#include "headless/lib/headless_macros.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_switches.h"
#include "ui/gfx/switches.h"
#include "ui/gl/gl_switches.h"
#include "ui/ozone/public/ozone_switches.h"

#ifdef HEADLESS_USE_EMBEDDED_RESOURCES
#include "headless/embedded_resource_pak.h"
#endif

#if defined(OS_MACOSX) || defined(OS_WIN)
#include "components/crash/content/app/crashpad.h"
#endif

#if !defined(CHROME_MULTIPLE_DLL_BROWSER)
#include "headless/lib/renderer/headless_content_renderer_client.h"
#endif

namespace headless {
namespace {
// Keep in sync with content/common/content_constants_internal.h.
#if !defined(CHROME_MULTIPLE_DLL_CHILD)
// TODO(skyostil): Add a tracing test for this.
const int kTraceEventBrowserProcessSortIndex = -6;
#endif

HeadlessContentMainDelegate* g_current_headless_content_main_delegate = nullptr;

base::LazyInstance<HeadlessCrashReporterClient>::Leaky g_headless_crash_client =
    LAZY_INSTANCE_INITIALIZER;

const char kLogFileName[] = "CHROME_LOG_FILE";
}  // namespace

HeadlessContentMainDelegate::HeadlessContentMainDelegate(
    std::unique_ptr<HeadlessBrowserImpl> browser)
    : content_client_(browser->options()), browser_(std::move(browser)) {
  DCHECK(!g_current_headless_content_main_delegate);
  g_current_headless_content_main_delegate = this;
}

HeadlessContentMainDelegate::~HeadlessContentMainDelegate() {
  DCHECK(g_current_headless_content_main_delegate == this);
  g_current_headless_content_main_delegate = nullptr;
}

bool HeadlessContentMainDelegate::BasicStartupComplete(int* exit_code) {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  // Make sure all processes know that we're in headless mode.
  if (!command_line->HasSwitch(switches::kHeadless))
    command_line->AppendSwitch(switches::kHeadless);

  if (browser_->options()->single_process_mode)
    command_line->AppendSwitch(switches::kSingleProcess);

  if (browser_->options()->disable_sandbox)
    command_line->AppendSwitch(switches::kNoSandbox);

#if defined(USE_OZONE)
  // The headless backend is automatically chosen for a headless build, but also
  // adding it here allows us to run in a non-headless build too.
  command_line->AppendSwitchASCII(switches::kOzonePlatform, "headless");
#endif

  if (!browser_->options()->gl_implementation.empty()) {
    command_line->AppendSwitchASCII(switches::kUseGL,
                                    browser_->options()->gl_implementation);
  } else {
    command_line->AppendSwitch(switches::kDisableGpu);
  }

  SetContentClient(&content_client_);
  return false;
}

void HeadlessContentMainDelegate::InitLogging(
    const base::CommandLine& command_line) {
  const std::string process_type =
      command_line.GetSwitchValueASCII(switches::kProcessType);
#if !defined(OS_WIN)
  if (!command_line.HasSwitch(switches::kEnableLogging))
    return;
#else
  // Child processes in Windows are not able to initialize logging.
  if (!process_type.empty())
    return;
#endif  // !defined(OS_WIN)

  logging::LoggingDestination log_mode;
  base::FilePath log_filename(FILE_PATH_LITERAL("chrome_debug.log"));
  if (command_line.GetSwitchValueASCII(switches::kEnableLogging) == "stderr") {
    log_mode = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  } else {
    base::FilePath custom_filename(
        command_line.GetSwitchValuePath(switches::kEnableLogging));
    if (custom_filename.empty()) {
      log_mode = logging::LOG_TO_ALL;
    } else {
      log_mode = logging::LOG_TO_FILE;
      log_filename = custom_filename;
    }
  }

  if (command_line.HasSwitch(switches::kLoggingLevel) &&
      logging::GetMinLogLevel() >= 0) {
    std::string log_level =
        command_line.GetSwitchValueASCII(switches::kLoggingLevel);
    int level = 0;
    if (base::StringToInt(log_level, &level) && level >= 0 &&
        level < logging::LOG_NUM_SEVERITIES) {
      logging::SetMinLogLevel(level);
    } else {
      DLOG(WARNING) << "Bad log level: " << log_level;
    }
  }

  base::FilePath log_path;
  logging::LoggingSettings settings;

  if (PathService::Get(base::DIR_MODULE, &log_path)) {
    log_path = log_path.Append(log_filename);
  } else {
    log_path = log_filename;
  }

  std::string filename;
  std::unique_ptr<base::Environment> env(base::Environment::Create());
  if (env->GetVar(kLogFileName, &filename) && !filename.empty()) {
    log_path = base::FilePath::FromUTF8Unsafe(filename);
  }

  settings.logging_dest = log_mode;
  settings.log_file = log_path.value().c_str();
  settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  settings.delete_old = process_type.empty() ? logging::DELETE_OLD_LOG_FILE
                                             : logging::APPEND_TO_OLD_LOG_FILE;
  bool success = logging::InitLogging(settings);
  DCHECK(success);
}

void HeadlessContentMainDelegate::InitCrashReporter(
    const base::CommandLine& command_line) {
  const std::string process_type =
      command_line.GetSwitchValueASCII(switches::kProcessType);
  crash_reporter::SetCrashReporterClient(g_headless_crash_client.Pointer());
  g_headless_crash_client.Pointer()->set_crash_dumps_dir(
      browser_->options()->crash_dumps_dir);

#if defined(HEADLESS_USE_BREAKPAD)
  if (!browser_->options()->enable_crash_reporter) {
    DCHECK(!breakpad::IsCrashReporterEnabled());
    return;
  }
  if (process_type != switches::kZygoteProcess)
    breakpad::InitCrashReporter(process_type);
#elif defined(OS_MACOSX)
  crash_reporter::InitializeCrashpad(process_type.empty(), process_type);
// Avoid adding this dependency in Windows Chrome component build, since
// crashpad is already enabled.
// TODO(dvallet): Ideally we would also want to avoid this for component build.
#elif defined(OS_WIN) && !defined(CHROME_MULTIPLE_DLL)
  crash_reporter::InitializeCrashpadWithEmbeddedHandler(process_type.empty(),
                                                        process_type);
#endif  // defined(HEADLESS_USE_BREAKPAD)
}

void HeadlessContentMainDelegate::PreSandboxStartup() {
  const base::CommandLine& command_line(
      *base::CommandLine::ForCurrentProcess());
#if defined(OS_WIN)
  // Windows always needs to initialize logging, otherwise you get a renderer
  // crash.
  InitLogging(command_line);
#else
  if (command_line.HasSwitch(switches::kEnableLogging))
    InitLogging(command_line);
#endif  // defined(OS_WIN)
  InitCrashReporter(command_line);
  InitializeResourceBundle();
}

#if !defined(CHROME_MULTIPLE_DLL_CHILD)
int HeadlessContentMainDelegate::RunProcess(
    const std::string& process_type,
    const content::MainFunctionParams& main_function_params) {

  if (!process_type.empty())
    return -1;

  base::trace_event::TraceLog::GetInstance()->SetProcessName("HeadlessBrowser");
  base::trace_event::TraceLog::GetInstance()->SetProcessSortIndex(
      kTraceEventBrowserProcessSortIndex);

  std::unique_ptr<content::BrowserMainRunner> browser_runner(
      content::BrowserMainRunner::Create());

  int exit_code = browser_runner->Initialize(main_function_params);
  DCHECK_LT(exit_code, 0) << "content::BrowserMainRunner::Initialize failed in "
                             "HeadlessContentMainDelegate::RunProcess";

  browser_->RunOnStartCallback();
  browser_runner->Run();
  browser_.reset();
  browser_runner->Shutdown();

  // Return value >=0 here to disable calling content::BrowserMain.
  return 0;
}
#endif  // !defined(CHROME_MULTIPLE_DLL_CHILD)

#if !defined(OS_MACOSX) && defined(OS_POSIX) && !defined(OS_ANDROID)
void HeadlessContentMainDelegate::ZygoteForked() {
  const base::CommandLine& command_line(
      *base::CommandLine::ForCurrentProcess());
  const std::string process_type =
      command_line.GetSwitchValueASCII(switches::kProcessType);
  // Unconditionally try to turn on crash reporting since we do not have access
  // to the latest browser options at this point when testing. Breakpad will
  // bail out gracefully if the browser process hasn't enabled crash reporting.
#if defined(HEADLESS_USE_BREAKPAD)
  breakpad::InitCrashReporter(process_type);
#endif
}
#endif

// static
HeadlessContentMainDelegate* HeadlessContentMainDelegate::GetInstance() {
  return g_current_headless_content_main_delegate;
}

// static
void HeadlessContentMainDelegate::InitializeResourceBundle() {
  base::FilePath dir_module;
  base::FilePath pak_file;
  bool result = PathService::Get(base::DIR_MODULE, &dir_module);
  DCHECK(result);

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  const std::string locale = command_line->GetSwitchValueASCII(switches::kLang);
  ui::ResourceBundle::InitSharedInstanceWithLocale(
      locale, nullptr, ui::ResourceBundle::DO_NOT_LOAD_COMMON_RESOURCES);

#ifdef HEADLESS_USE_EMBEDDED_RESOURCES
  ResourceBundle::GetSharedInstance().AddDataPackFromBuffer(
      base::StringPiece(
          reinterpret_cast<const char*>(kHeadlessResourcePak.contents),
          kHeadlessResourcePak.length),
      ui::SCALE_FACTOR_NONE);
#else
  // Try loading the headless library pak file first. If it doesn't exist (i.e.,
  // when we're running with the --headless switch), fall back to the browser's
  // resource pak.
  pak_file = dir_module.Append(FILE_PATH_LITERAL("headless_lib.pak"));
  if (!base::PathExists(pak_file))
    pak_file = dir_module.Append(FILE_PATH_LITERAL("resources.pak"));
#if defined(OS_MACOSX) && !defined(COMPONENT_BUILD)
  // In non component builds, check if fall back in Resources/ folder is
  // available.
  if (!base::PathExists(pak_file))
    pak_file = dir_module.Append(FILE_PATH_LITERAL("Resources/resources.pak"));
#endif
  ResourceBundle::GetSharedInstance().AddDataPackFromPath(
      pak_file, ui::SCALE_FACTOR_NONE);
#endif
}

#if !defined(CHROME_MULTIPLE_DLL_CHILD)
content::ContentBrowserClient*
HeadlessContentMainDelegate::CreateContentBrowserClient() {
  browser_client_ =
      base::MakeUnique<HeadlessContentBrowserClient>(browser_.get());
  return browser_client_.get();
}
#endif  // !defined(CHROME_MULTIPLE_DLL_CHILD)

#if !defined(CHROME_MULTIPLE_DLL_BROWSER)
content::ContentRendererClient*
HeadlessContentMainDelegate::CreateContentRendererClient() {
  renderer_client_ = base::MakeUnique<HeadlessContentRendererClient>();
  return renderer_client_.get();
}
#endif  // !defined(CHROME_MULTIPLE_DLL_BROWSER)

}  // namespace headless
