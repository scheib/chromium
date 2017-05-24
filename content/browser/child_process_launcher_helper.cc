// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/child_process_launcher_helper.h"

#include "base/command_line.h"
#include "base/metrics/histogram_macros.h"
#include "content/browser/child_process_launcher.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/sandboxed_process_launcher_delegate.h"
#include "mojo/edk/embedder/platform_channel_pair.h"

namespace content {
namespace internal {

namespace {

void RecordHistogramsOnLauncherThread(base::TimeDelta launch_time) {
  DCHECK_CURRENTLY_ON(BrowserThread::PROCESS_LAUNCHER);
  // Log the launch time, separating out the first one (which will likely be
  // slower due to the rest of the browser initializing at the same time).
  static bool done_first_launch = false;
  if (done_first_launch) {
    UMA_HISTOGRAM_TIMES("MPArch.ChildProcessLaunchSubsequent", launch_time);
  } else {
    UMA_HISTOGRAM_TIMES("MPArch.ChildProcessLaunchFirst", launch_time);
    done_first_launch = true;
  }
}

}  // namespace

ChildProcessLauncherHelper::Process::Process(Process&& other)
  : process(std::move(other.process))
#if defined(OS_LINUX)
   , zygote(other.zygote)
#endif
{
}

ChildProcessLauncherHelper::Process&
ChildProcessLauncherHelper::Process::Process::operator=(
    ChildProcessLauncherHelper::Process&& other) {
  DCHECK_NE(this, &other);
  process = std::move(other.process);
#if defined(OS_LINUX)
  zygote = other.zygote;
#endif
  return *this;
}

ChildProcessLauncherHelper::ChildProcessLauncherHelper(
    int child_process_id,
    BrowserThread::ID client_thread_id,
    std::unique_ptr<base::CommandLine> command_line,
    std::unique_ptr<SandboxedProcessLauncherDelegate> delegate,
    const base::WeakPtr<ChildProcessLauncher>& child_process_launcher,
    bool terminate_on_shutdown)
    : child_process_id_(child_process_id),
      client_thread_id_(client_thread_id),
      command_line_(std::move(command_line)),
      delegate_(std::move(delegate)),
      child_process_launcher_(child_process_launcher),
      terminate_on_shutdown_(terminate_on_shutdown) {
}

ChildProcessLauncherHelper::~ChildProcessLauncherHelper() {
}

void ChildProcessLauncherHelper::StartLaunchOnClientThread() {
  DCHECK_CURRENTLY_ON(client_thread_id_);

  BeforeLaunchOnClientThread();

  mojo_server_handle_ = PrepareMojoPipeHandlesOnClientThread();
  if (!mojo_server_handle_.is_valid()) {
    mojo::edk::PlatformChannelPair channel_pair;
    mojo_server_handle_ = channel_pair.PassServerHandle();
    mojo_client_handle_ = channel_pair.PassClientHandle();
  }

  BrowserThread::PostTask(
      BrowserThread::PROCESS_LAUNCHER, FROM_HERE,
      base::Bind(&ChildProcessLauncherHelper::LaunchOnLauncherThread, this));
}

void ChildProcessLauncherHelper::LaunchOnLauncherThread() {
  DCHECK_CURRENTLY_ON(BrowserThread::PROCESS_LAUNCHER);

  begin_launch_time_ = base::TimeTicks::Now();

  std::unique_ptr<FileMappedForLaunch> files_to_register = GetFilesToMap();

  bool is_synchronous_launch = true;
  int launch_result = LAUNCH_RESULT_FAILURE;
  base::LaunchOptions options;
  BeforeLaunchOnLauncherThread(*files_to_register, &options);

  Process process = LaunchProcessOnLauncherThread(options,
                                                  std::move(files_to_register),
                                                  &is_synchronous_launch,
                                                  &launch_result);

  AfterLaunchOnLauncherThread(process, options);

  if (is_synchronous_launch) {
    PostLaunchOnLauncherThread(std::move(process), launch_result);
  }
}

void ChildProcessLauncherHelper::PostLaunchOnLauncherThread(
    ChildProcessLauncherHelper::Process process,
    int launch_result) {
  // Release the client handle now that the process has been started (the pipe
  // may not signal when the process dies otherwise and we would not detect the
  // child process died).
  mojo_client_handle_.reset();

  if (process.process.IsValid()) {
    RecordHistogramsOnLauncherThread(base::TimeTicks::Now() -
                                     begin_launch_time_);
  }

  BrowserThread::PostTask(
      client_thread_id_, FROM_HERE,
      base::Bind(&ChildProcessLauncherHelper::PostLaunchOnClientThread, this,
                 base::Passed(&process), launch_result));
}

void ChildProcessLauncherHelper::PostLaunchOnClientThread(
    ChildProcessLauncherHelper::Process process,
    int error_code) {
  if (child_process_launcher_) {
    child_process_launcher_->Notify(
        std::move(process), std::move(mojo_server_handle_), error_code);
  } else if (process.process.IsValid() && terminate_on_shutdown_) {
    // Client is gone, terminate the process.
    ForceNormalProcessTerminationAsync(std::move(process));
  }
}

std::string ChildProcessLauncherHelper::GetProcessType() {
  return command_line()->GetSwitchValueASCII(switches::kProcessType);
}

// static
void ChildProcessLauncherHelper::ForceNormalProcessTerminationAsync(
    ChildProcessLauncherHelper::Process process) {
  if (BrowserThread::CurrentlyOn(BrowserThread::PROCESS_LAUNCHER)) {
    ForceNormalProcessTerminationSync(std::move(process));
    return;
  }
  // On Posix, EnsureProcessTerminated can lead to 2 seconds of sleep!
  // So don't do this on the UI/IO threads.
  BrowserThread::PostTask(
      BrowserThread::PROCESS_LAUNCHER, FROM_HERE,
      base::Bind(&ChildProcessLauncherHelper::ForceNormalProcessTerminationSync,
                 base::Passed(&process)));
}

}  // namespace internal
}  // namespace content
