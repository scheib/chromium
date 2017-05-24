// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "base/base64.h"
#include "base/base_switches.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "content/public/app/content_main.h"
#include "content/public/common/content_switches.h"
#include "headless/app/headless_shell.h"
#include "headless/app/headless_shell_switches.h"
#include "headless/lib/browser/headless_devtools.h"
#include "headless/public/headless_devtools_target.h"
#include "headless/public/util/deterministic_http_protocol_handler.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/http/http_util.h"
#include "ui/gfx/geometry/size.h"

#if defined(OS_WIN)
#include "components/crash/content/app/crash_switches.h"
#include "components/crash/content/app/run_as_crashpad_handler_win.h"
#include "sandbox/win/src/sandbox_types.h"
#endif

namespace headless {
namespace {
// Address where to listen to incoming DevTools connections.
const char kDevToolsHttpServerAddress[] = "127.0.0.1";
// Default file name for screenshot. Can be overriden by "--screenshot" switch.
const char kDefaultScreenshotFileName[] = "screenshot.png";
// Default file name for pdf. Can be overriden by "--print-to-pdf" switch.
const char kDefaultPDFFileName[] = "output.pdf";

bool ParseWindowSize(std::string window_size, gfx::Size* parsed_window_size) {
  int width, height = 0;
  if (sscanf(window_size.c_str(), "%d%*[x,]%d", &width, &height) >= 2 &&
      width >= 0 && height >= 0) {
    parsed_window_size->set_width(width);
    parsed_window_size->set_height(height);
    return true;
  }
  return false;
}
}  // namespace

HeadlessShell::HeadlessShell()
    : browser_(nullptr),
      devtools_client_(HeadlessDevToolsClient::Create()),
#if !defined(CHROME_MULTIPLE_DLL_CHILD)
      web_contents_(nullptr),
      browser_context_(nullptr),
#endif
      processed_page_ready_(false),
      weak_factory_(this) {
}

HeadlessShell::~HeadlessShell() {}

#if !defined(CHROME_MULTIPLE_DLL_CHILD)
void HeadlessShell::OnStart(HeadlessBrowser* browser) {
  browser_ = browser;

  HeadlessBrowserContext::Builder context_builder =
      browser_->CreateBrowserContextBuilder();
  // TODO(eseckler): These switches should also affect BrowserContexts that
  // are created via DevTools later.
  DeterministicHttpProtocolHandler* http_handler = nullptr;
  DeterministicHttpProtocolHandler* https_handler = nullptr;
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDeterministicFetch)) {
    deterministic_dispatcher_.reset(
        new DeterministicDispatcher(browser_->BrowserIOThread()));

    ProtocolHandlerMap protocol_handlers;
    protocol_handlers[url::kHttpScheme] =
        base::MakeUnique<DeterministicHttpProtocolHandler>(
            deterministic_dispatcher_.get(), browser->BrowserIOThread());
    http_handler = static_cast<DeterministicHttpProtocolHandler*>(
        protocol_handlers[url::kHttpScheme].get());
    protocol_handlers[url::kHttpsScheme] =
        base::MakeUnique<DeterministicHttpProtocolHandler>(
            deterministic_dispatcher_.get(), browser->BrowserIOThread());
    https_handler = static_cast<DeterministicHttpProtocolHandler*>(
        protocol_handlers[url::kHttpsScheme].get());

    context_builder.SetProtocolHandlers(std::move(protocol_handlers));
  }
  browser_context_ = context_builder.Build();
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDeterministicFetch)) {
    http_handler->SetHeadlessBrowserContext(browser_context_);
    https_handler->SetHeadlessBrowserContext(browser_context_);
  }
  browser_->SetDefaultBrowserContext(browser_context_);

  HeadlessWebContents::Builder builder(
      browser_context_->CreateWebContentsBuilder());
  base::CommandLine::StringVector args =
      base::CommandLine::ForCurrentProcess()->GetArgs();

  // TODO(alexclarke): Should we navigate to about:blank first if using
  // virtual time?
  if (args.empty())
#if defined(OS_WIN)
    args.push_back(L"about:blank");
#else
    args.push_back("about:blank");
#endif
  for (auto it = args.rbegin(); it != args.rend(); ++it) {
    GURL url(*it);
    HeadlessWebContents* web_contents = builder.SetInitialURL(url).Build();
    if (!web_contents) {
      LOG(ERROR) << "Navigation to " << url << " failed";
      browser_->Shutdown();
      return;
    }
    if (!web_contents_ && !RemoteDebuggingEnabled()) {
      // TODO(jzfeng): Support observing multiple targets.
      url_ = url;
      web_contents_ = web_contents;
      web_contents_->AddObserver(this);
    }
  }
}

void HeadlessShell::Shutdown() {
  if (!web_contents_)
    return;
  if (!RemoteDebuggingEnabled()) {
    devtools_client_->GetEmulation()->GetExperimental()->RemoveObserver(this);
    devtools_client_->GetInspector()->GetExperimental()->RemoveObserver(this);
    devtools_client_->GetPage()->GetExperimental()->RemoveObserver(this);
    if (web_contents_->GetDevToolsTarget()) {
      web_contents_->GetDevToolsTarget()->DetachClient(devtools_client_.get());
    }
  }
  web_contents_->RemoveObserver(this);
  web_contents_ = nullptr;
  browser_context_->Close();
  browser_->Shutdown();
}

void HeadlessShell::DevToolsTargetReady() {
  web_contents_->GetDevToolsTarget()->AttachClient(devtools_client_.get());
  devtools_client_->GetInspector()->GetExperimental()->AddObserver(this);
  devtools_client_->GetPage()->GetExperimental()->AddObserver(this);
  devtools_client_->GetPage()->Enable();

  devtools_client_->GetEmulation()->GetExperimental()->AddObserver(this);

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDeterministicFetch)) {
    devtools_client_->GetPage()->GetExperimental()->SetControlNavigations(
        headless::page::SetControlNavigationsParams::Builder()
            .SetEnabled(true)
            .Build());
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDefaultBackgroundColor)) {
    std::string color_hex =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
            switches::kDefaultBackgroundColor);
    uint32_t color;
    CHECK(base::HexStringToUInt(color_hex, &color))
        << "Expected a hex value for --default-background-color=";
    auto rgba = headless::dom::RGBA::Builder()
                    .SetR((color & 0xff000000) >> 24)
                    .SetG((color & 0x00ff0000) >> 16)
                    .SetB((color & 0x0000ff00) >> 8)
                    .SetA(color & 0x000000ff)
                    .Build();
    devtools_client_->GetEmulation()
        ->GetExperimental()
        ->SetDefaultBackgroundColorOverride(
            headless::emulation::SetDefaultBackgroundColorOverrideParams::
                Builder()
                    .SetColor(std::move(rgba))
                    .Build());
  }

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kVirtualTimeBudget)) {
    std::string budget_ms_ascii =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
            switches::kVirtualTimeBudget);
    int budget_ms;
    CHECK(base::StringToInt(budget_ms_ascii, &budget_ms))
        << "Expected an integer value for --virtual-time-budget=";
    devtools_client_->GetEmulation()->GetExperimental()->SetVirtualTimePolicy(
        emulation::SetVirtualTimePolicyParams::Builder()
            .SetPolicy(
                emulation::VirtualTimePolicy::PAUSE_IF_NETWORK_FETCHES_PENDING)
            .SetBudget(budget_ms)
            .Build());
  } else {
    // Check if the document had already finished loading by the time we
    // attached.
    PollReadyState();
  }

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kTimeout)) {
    std::string timeout_ms_ascii =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
            switches::kTimeout);
    int timeout_ms;
    CHECK(base::StringToInt(timeout_ms_ascii, &timeout_ms))
        << "Expected an integer value for --timeout=";
    browser_->BrowserMainThread()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&HeadlessShell::FetchTimeout, weak_factory_.GetWeakPtr()),
        base::TimeDelta::FromMilliseconds(timeout_ms));
  }

  // TODO(skyostil): Implement more features to demonstrate the devtools API.
}
#endif  // !defined(CHROME_MULTIPLE_DLL_CHILD)

void HeadlessShell::FetchTimeout() {
  LOG(INFO) << "Timeout.";
  devtools_client_->GetPage()->GetExperimental()->StopLoading(
      page::StopLoadingParams::Builder().Build());
}

void HeadlessShell::OnTargetCrashed(
    const inspector::TargetCrashedParams& params) {
  LOG(ERROR) << "Abnormal renderer termination.";
  // NB this never gets called if remote debugging is enabled.
  Shutdown();
}

void HeadlessShell::PollReadyState() {
  // We need to check the current location in addition to the ready state to
  // be sure the expected page is ready.
  devtools_client_->GetRuntime()->Evaluate(
      "document.readyState + ' ' + document.location.href",
      base::Bind(&HeadlessShell::OnReadyState, weak_factory_.GetWeakPtr()));
}

void HeadlessShell::OnReadyState(
    std::unique_ptr<runtime::EvaluateResult> result) {
  std::string ready_state_and_url;
  if (result->GetResult()->GetValue()->GetAsString(&ready_state_and_url)) {
    std::stringstream stream(ready_state_and_url);
    std::string ready_state;
    std::string url;
    stream >> ready_state;
    stream >> url;

    if (ready_state == "complete" &&
        (url_.spec() == url || url != "about:blank")) {
      OnPageReady();
      return;
    }
  }
}

// emulation::Observer implementation:
void HeadlessShell::OnVirtualTimeBudgetExpired(
    const emulation::VirtualTimeBudgetExpiredParams& params) {
  OnPageReady();
}

// page::Observer implementation:
void HeadlessShell::OnLoadEventFired(const page::LoadEventFiredParams& params) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kVirtualTimeBudget)) {
    return;
  }
  OnPageReady();
}

void HeadlessShell::OnNavigationRequested(
    const headless::page::NavigationRequestedParams& params) {
  deterministic_dispatcher_->NavigationRequested(
      base::MakeUnique<ShellNavigationRequest>(weak_factory_.GetWeakPtr(),
                                               params));
}

void HeadlessShell::OnPageReady() {
  if (processed_page_ready_)
    return;
  processed_page_ready_ = true;

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kDumpDom)) {
    FetchDom();
  } else if (base::CommandLine::ForCurrentProcess()->HasSwitch(
                 switches::kRepl)) {
    LOG(INFO)
        << "Type a Javascript expression to evaluate or \"quit\" to exit.";
    InputExpression();
  } else if (base::CommandLine::ForCurrentProcess()->HasSwitch(
                 switches::kScreenshot)) {
    CaptureScreenshot();
  } else if (base::CommandLine::ForCurrentProcess()->HasSwitch(
                 switches::kPrintToPDF)) {
    PrintToPDF();
  } else {
    Shutdown();
  }
}

void HeadlessShell::FetchDom() {
  devtools_client_->GetRuntime()->Evaluate(
      "document.body.outerHTML",
      base::Bind(&HeadlessShell::OnDomFetched, weak_factory_.GetWeakPtr()));
}

void HeadlessShell::OnDomFetched(
    std::unique_ptr<runtime::EvaluateResult> result) {
  if (result->HasExceptionDetails()) {
    LOG(ERROR) << "Failed to evaluate document.body.outerHTML: "
               << result->GetExceptionDetails()->GetText();
  } else {
    std::string dom;
    if (result->GetResult()->GetValue()->GetAsString(&dom)) {
      printf("%s\n", dom.c_str());
    }
  }
  Shutdown();
}

void HeadlessShell::InputExpression() {
  // Note that a real system should read user input asynchronously, because
  // otherwise all other browser activity is suspended (e.g., page loading).
  printf(">>> ");
  std::stringstream expression;
  while (true) {
    int c = fgetc(stdin);
    if (c == EOF || c == '\n') {
      break;
    }
    expression << static_cast<char>(c);
  }
  if (expression.str() == "quit") {
    Shutdown();
    return;
  }
  devtools_client_->GetRuntime()->Evaluate(
      expression.str(), base::Bind(&HeadlessShell::OnExpressionResult,
                                   weak_factory_.GetWeakPtr()));
}

void HeadlessShell::OnExpressionResult(
    std::unique_ptr<runtime::EvaluateResult> result) {
  std::unique_ptr<base::Value> value = result->Serialize();
  std::string result_json;
  base::JSONWriter::Write(*value, &result_json);
  printf("%s\n", result_json.c_str());
  InputExpression();
}

void HeadlessShell::CaptureScreenshot() {
  devtools_client_->GetPage()->GetExperimental()->CaptureScreenshot(
      page::CaptureScreenshotParams::Builder().SetFromSurface(true).Build(),
      base::Bind(&HeadlessShell::OnScreenshotCaptured,
                 weak_factory_.GetWeakPtr()));
}

void HeadlessShell::OnScreenshotCaptured(
    std::unique_ptr<page::CaptureScreenshotResult> result) {
  if (!result) {
    LOG(ERROR) << "Capture screenshot failed";
    Shutdown();
    return;
  }
  WriteFile(switches::kScreenshot, kDefaultScreenshotFileName,
            result->GetData());
}

void HeadlessShell::PrintToPDF() {
  devtools_client_->GetPage()->GetExperimental()->PrintToPDF(
      page::PrintToPDFParams::Builder()
          .SetDisplayHeaderFooter(true)
          .SetPrintBackground(true)
          .Build(),
      base::Bind(&HeadlessShell::OnPDFCreated, weak_factory_.GetWeakPtr()));
}

void HeadlessShell::OnPDFCreated(
    std::unique_ptr<page::PrintToPDFResult> result) {
  if (!result) {
    LOG(ERROR) << "Print to PDF failed";
    Shutdown();
    return;
  }
  WriteFile(switches::kPrintToPDF, kDefaultPDFFileName, result->GetData());
}

void HeadlessShell::WriteFile(const std::string& file_path_switch,
                              const std::string& default_file_name,
                              const std::string& base64_data) {
  base::FilePath file_name =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          file_path_switch);
  if (file_name.empty())
    file_name = base::FilePath().AppendASCII(default_file_name);

  file_proxy_.reset(new base::FileProxy(browser_->BrowserFileThread().get()));
  if (!file_proxy_->CreateOrOpen(
          file_name, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE,
          base::Bind(&HeadlessShell::OnFileOpened, weak_factory_.GetWeakPtr(),
                     base64_data, file_name))) {
    // Operation could not be started.
    OnFileOpened(std::string(), file_name, base::File::FILE_ERROR_FAILED);
  }
}

void HeadlessShell::OnFileOpened(const std::string& base64_data,
                                 const base::FilePath file_name,
                                 base::File::Error error_code) {
  if (!file_proxy_->IsValid()) {
    LOG(ERROR) << "Writing to file " << file_name.value()
               << " was unsuccessful, could not open file: "
               << base::File::ErrorToString(error_code);
    return;
  }

  std::string decoded_data;
  if (!base::Base64Decode(base64_data, &decoded_data)) {
    LOG(ERROR) << "Failed to decode base64 data";
    OnFileWritten(file_name, base64_data.size(), base::File::FILE_ERROR_FAILED,
                  0);
    return;
  }

  scoped_refptr<net::IOBufferWithSize> buf =
      new net::IOBufferWithSize(decoded_data.size());
  memcpy(buf->data(), decoded_data.data(), decoded_data.size());

  if (!file_proxy_->Write(
          0, buf->data(), buf->size(),
          base::Bind(&HeadlessShell::OnFileWritten, weak_factory_.GetWeakPtr(),
                     file_name, buf->size()))) {
    // Operation may have completed successfully or failed.
    OnFileWritten(file_name, buf->size(), base::File::FILE_ERROR_FAILED, 0);
  }
}

void HeadlessShell::OnFileWritten(const base::FilePath file_name,
                                  const size_t length,
                                  base::File::Error error_code,
                                  int write_result) {
  if (write_result < static_cast<int>(length)) {
    // TODO(eseckler): Support recovering from partial writes.
    LOG(ERROR) << "Writing to file " << file_name.value()
               << " was unsuccessful: "
               << base::File::ErrorToString(error_code);
  } else {
    LOG(INFO) << "Written to file " << file_name.value() << ".";
  }
  if (!file_proxy_->Close(base::Bind(&HeadlessShell::OnFileClosed,
                                     weak_factory_.GetWeakPtr()))) {
    // Operation could not be started.
    OnFileClosed(base::File::FILE_ERROR_FAILED);
  }
}

void HeadlessShell::OnFileClosed(base::File::Error error_code) {
  Shutdown();
}

bool HeadlessShell::RemoteDebuggingEnabled() const {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  return (command_line.HasSwitch(switches::kRemoteDebuggingPort) ||
          command_line.HasSwitch(switches::kRemoteDebuggingSocketFd));
}

bool ValidateCommandLine(const base::CommandLine& command_line) {
#if !defined(OS_POSIX)
  if (command_line.HasSwitch(switches::kRemoteDebuggingSocketFd)) {
    LOG(ERROR) << "Remote-debugging-socket can't be set on non-Posix systems";
    return false;
  }
#endif
  if (command_line.HasSwitch(switches::kRemoteDebuggingPort) &&
      command_line.HasSwitch(switches::kRemoteDebuggingSocketFd)) {
    LOG(ERROR) << "Remote-debugging-port and remote-debugging-socket "
               << "can't both be set.";
    return false;
  }
  if (!command_line.HasSwitch(switches::kRemoteDebuggingPort) &&
      !command_line.HasSwitch(switches::kRemoteDebuggingSocketFd)) {
    if (command_line.GetArgs().size() <= 1)
      return true;
    LOG(ERROR) << "Open multiple tabs is only supported when "
               << "remote debugging is enabled.";
    return false;
  }
  if (command_line.HasSwitch(switches::kDefaultBackgroundColor)) {
    LOG(ERROR) << "Setting default background color is disabled "
               << "when remote debugging is enabled.";
    return false;
  }
  if (command_line.HasSwitch(switches::kDumpDom)) {
    LOG(ERROR) << "Dump DOM is disabled when remote debugging is enabled.";
    return false;
  }
  if (command_line.HasSwitch(switches::kPrintToPDF)) {
    LOG(ERROR) << "Print to PDF is disabled "
               << "when remote debugging is enabled.";
    return false;
  }
  if (command_line.HasSwitch(switches::kRepl)) {
    LOG(ERROR) << "Evaluate Javascript is disabled "
               << "when remote debugging is enabled.";
    return false;
  }
  if (command_line.HasSwitch(switches::kScreenshot)) {
    LOG(ERROR) << "Capture screenshot is disabled "
               << "when remote debugging is enabled.";
    return false;
  }
  if (command_line.HasSwitch(switches::kTimeout)) {
    LOG(ERROR) << "Navigation timeout is disabled "
               << "when remote debugging is enabled.";
    return false;
  }
  if (command_line.HasSwitch(switches::kVirtualTimeBudget)) {
    LOG(ERROR) << "Virtual time budget is disabled "
               << "when remote debugging is enabled.";
    return false;
  }
  return true;
}

#if defined(OS_WIN)
int HeadlessShellMain(HINSTANCE instance,
                      sandbox::SandboxInterfaceInfo* sandbox_info) {
  base::CommandLine::Init(0, nullptr);
#if defined(HEADLESS_USE_CRASPHAD)
  std::string process_type =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          ::switches::kProcessType);
  if (process_type == crash_reporter::switches::kCrashpadHandler) {
    return crash_reporter::RunAsCrashpadHandler(
        *base::CommandLine::ForCurrentProcess(), ::switches::kProcessType);
  }
#endif  // defined(HEADLESS_USE_CRASPHAD)
  RunChildProcessIfNeeded(instance, sandbox_info);
  HeadlessBrowser::Options::Builder builder(0, nullptr);
  builder.SetInstance(instance);
  builder.SetSandboxInfo(std::move(sandbox_info));
#else
int HeadlessShellMain(int argc, const char** argv) {
  base::CommandLine::Init(argc, argv);
  RunChildProcessIfNeeded(argc, argv);
  HeadlessBrowser::Options::Builder builder(argc, argv);
#endif  // defined(OS_WIN)
  HeadlessShell shell;

  const base::CommandLine& command_line(
      *base::CommandLine::ForCurrentProcess());
  if (!ValidateCommandLine(command_line))
    return EXIT_FAILURE;

  if (command_line.HasSwitch(switches::kEnableCrashReporter))
    builder.SetCrashReporterEnabled(true);
  if (command_line.HasSwitch(switches::kCrashDumpsDir)) {
    builder.SetCrashDumpsDir(
        command_line.GetSwitchValuePath(switches::kCrashDumpsDir));
  }

  // Enable devtools if requested, either by specifying a port (and optional
  // address), or by specifying the fd of an already-open socket.
  if (command_line.HasSwitch(::switches::kRemoteDebuggingPort)) {
    std::string address = kDevToolsHttpServerAddress;
    if (command_line.HasSwitch(switches::kRemoteDebuggingAddress)) {
      address =
          command_line.GetSwitchValueASCII(switches::kRemoteDebuggingAddress);
      net::IPAddress parsed_address;
      if (!net::ParseURLHostnameToAddress(address, &parsed_address)) {
        LOG(ERROR) << "Invalid devtools server address";
        return EXIT_FAILURE;
      }
    }
    int parsed_port;
    std::string port_str =
        command_line.GetSwitchValueASCII(::switches::kRemoteDebuggingPort);
    if (!base::StringToInt(port_str, &parsed_port) ||
        !base::IsValueInRangeForNumericType<uint16_t>(parsed_port)) {
      LOG(ERROR) << "Invalid devtools server port";
      return EXIT_FAILURE;
    }
    net::IPAddress devtools_address;
    bool result = devtools_address.AssignFromIPLiteral(address);
    DCHECK(result);
    const net::IPEndPoint endpoint(devtools_address,
                                   base::checked_cast<uint16_t>(parsed_port));
    builder.EnableDevToolsServer(endpoint);
  } else if (command_line.HasSwitch(switches::kRemoteDebuggingSocketFd)) {
    int parsed_fd;
    std::string fd_str =
        command_line.GetSwitchValueASCII(switches::kRemoteDebuggingSocketFd);
    if (!base::StringToInt(fd_str, &parsed_fd) ||
        !base::IsValueInRangeForNumericType<size_t>(parsed_fd)) {
      LOG(ERROR) << "Invalid devtools server socket fd";
      return EXIT_FAILURE;
    }
    builder.EnableDevToolsServer(base::checked_cast<size_t>(parsed_fd));
  }

  if (command_line.HasSwitch(switches::kProxyServer)) {
    std::string proxy_server =
        command_line.GetSwitchValueASCII(switches::kProxyServer);
    net::HostPortPair parsed_proxy_server =
        net::HostPortPair::FromString(proxy_server);
    if (parsed_proxy_server.host().empty() || !parsed_proxy_server.port()) {
      LOG(ERROR) << "Malformed proxy server url";
      return EXIT_FAILURE;
    }
    builder.SetProxyServer(parsed_proxy_server);
  }

  if (command_line.HasSwitch(switches::kHostResolverRules)) {
    builder.SetHostResolverRules(
        command_line.GetSwitchValueASCII(switches::kHostResolverRules));
  }

  if (command_line.HasSwitch(switches::kUseGL)) {
    builder.SetGLImplementation(
        command_line.GetSwitchValueASCII(switches::kUseGL));
  }

  if (command_line.HasSwitch(switches::kUserDataDir)) {
    builder.SetUserDataDir(
        command_line.GetSwitchValuePath(switches::kUserDataDir));
    builder.SetIncognitoMode(false);
  }

  if (command_line.HasSwitch(switches::kWindowSize)) {
    std::string window_size =
        command_line.GetSwitchValueASCII(switches::kWindowSize);
    gfx::Size parsed_window_size;
    if (!ParseWindowSize(window_size, &parsed_window_size)) {
      LOG(ERROR) << "Malformed window size";
      return EXIT_FAILURE;
    }
    builder.SetWindowSize(parsed_window_size);
  }

  if (command_line.HasSwitch(switches::kHideScrollbars)) {
    builder.SetOverrideWebPreferencesCallback(
        base::Bind([](WebPreferences* preferences) {
          preferences->hide_scrollbars = true;
        }));
  }

  if (command_line.HasSwitch(switches::kUserAgent)) {
    std::string ua = command_line.GetSwitchValueASCII(switches::kUserAgent);
    if (net::HttpUtil::IsValidHeaderValue(ua))
      builder.SetUserAgent(ua);
  }

  return HeadlessBrowserMain(
      builder.Build(),
      base::Bind(&HeadlessShell::OnStart, base::Unretained(&shell)));
}

int HeadlessShellMain(const content::ContentMainParams& params) {
#if defined(OS_WIN)
  return HeadlessShellMain(params.instance, params.sandbox_info);
#else
  return HeadlessShellMain(params.argc, params.argv);
#endif
}

}  // namespace headless
