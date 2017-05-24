// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/command_line.h"
#include "base/run_loop.h"
#include "build/build_config.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/common/child_process_messages.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_process_host_observer.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_service.mojom.h"
#include "content/shell/browser/shell.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/media_switches.h"
#include "media/base/test_data_util.h"
#include "media/mojo/features.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#endif

namespace content {
namespace {

int RenderProcessHostCount() {
  content::RenderProcessHost::iterator hosts =
      content::RenderProcessHost::AllHostsIterator();
  int count = 0;
  while (!hosts.IsAtEnd()) {
    if (hosts.GetCurrentValue()->HasConnection())
      count++;
    hosts.Advance();
  }
  return count;
}

class RenderProcessHostTest : public ContentBrowserTest,
                              public RenderProcessHostObserver {
 public:
  RenderProcessHostTest() : process_exits_(0), host_destructions_(0) {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kIgnoreAutoplayRestrictionsForTests);
  }

 protected:
  void set_process_exit_callback(const base::Closure& callback) {
    process_exit_callback_ = callback;
  }

  // RenderProcessHostObserver:
  void RenderProcessExited(RenderProcessHost* host,
                           base::TerminationStatus status,
                           int exit_code) override {
    ++process_exits_;
    if (!process_exit_callback_.is_null())
      process_exit_callback_.Run();
  }
  void RenderProcessHostDestroyed(RenderProcessHost* host) override {
    ++host_destructions_;
  }

  int process_exits_;
  int host_destructions_;
  base::Closure process_exit_callback_;
};

// Sometimes the renderer process's ShutdownRequest (corresponding to the
// ViewMsg_WasSwappedOut from a previous navigation) doesn't arrive until after
// the browser process decides to re-use the renderer for a new purpose.  This
// test makes sure the browser doesn't let the renderer die in that case.  See
// http://crbug.com/87176.
IN_PROC_BROWSER_TEST_F(RenderProcessHostTest,
                       ShutdownRequestFromActiveTabIgnored) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url = embedded_test_server()->GetURL("/simple_page.html");
  NavigateToURL(shell(), test_url);
  RenderProcessHost* rph =
      shell()->web_contents()->GetMainFrame()->GetProcess();

  host_destructions_ = 0;
  process_exits_ = 0;
  rph->AddObserver(this);
  ChildProcessHostMsg_ShutdownRequest msg;
  rph->OnMessageReceived(msg);

  // If the RPH sends a mistaken ChildProcessMsg_Shutdown, the renderer process
  // will take some time to die. Wait for a second tab to load in order to give
  // that time to happen.
  NavigateToURL(CreateBrowser(), test_url);

  EXPECT_EQ(0, process_exits_);
  if (!host_destructions_)
    rph->RemoveObserver(this);
}

IN_PROC_BROWSER_TEST_F(RenderProcessHostTest,
                       GuestsAreNotSuitableHosts) {
  // Set max renderers to 1 to force running out of processes.
  content::RenderProcessHost::SetMaxRendererProcessCount(1);

  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url = embedded_test_server()->GetURL("/simple_page.html");
  NavigateToURL(shell(), test_url);
  RenderProcessHost* rph =
      shell()->web_contents()->GetMainFrame()->GetProcess();
  // Make it believe it's a guest.
  reinterpret_cast<RenderProcessHostImpl*>(rph)->
      set_is_for_guests_only_for_testing(true);
  EXPECT_EQ(1, RenderProcessHostCount());

  // Navigate to a different page.
  GURL::Replacements replace_host;
  replace_host.SetHostStr("localhost");
  GURL another_url = embedded_test_server()->GetURL("/simple_page.html");
  another_url = another_url.ReplaceComponents(replace_host);
  NavigateToURL(CreateBrowser(), another_url);

  // Expect that we got another process (the guest renderer was not reused).
  EXPECT_EQ(2, RenderProcessHostCount());
}

class ShellCloser : public RenderProcessHostObserver {
 public:
  ShellCloser(Shell* shell, std::string* logging_string)
      : shell_(shell), logging_string_(logging_string) {}

 protected:
  // RenderProcessHostObserver:
  void RenderProcessExited(RenderProcessHost* host,
                           base::TerminationStatus status,
                           int exit_code) override {
    logging_string_->append("ShellCloser::RenderProcessExited ");
    shell_->Close();
  }

  void RenderProcessHostDestroyed(RenderProcessHost* host) override {
    logging_string_->append("ShellCloser::RenderProcessHostDestroyed ");
  }

  Shell* shell_;
  std::string* logging_string_;
};

class ObserverLogger : public RenderProcessHostObserver {
 public:
  explicit ObserverLogger(std::string* logging_string)
      : logging_string_(logging_string), host_destroyed_(false) {}

  bool host_destroyed() { return host_destroyed_; }

 protected:
  // RenderProcessHostObserver:
  void RenderProcessExited(RenderProcessHost* host,
                           base::TerminationStatus status,
                           int exit_code) override {
    logging_string_->append("ObserverLogger::RenderProcessExited ");
  }

  void RenderProcessHostDestroyed(RenderProcessHost* host) override {
    logging_string_->append("ObserverLogger::RenderProcessHostDestroyed ");
    host_destroyed_ = true;
  }

  std::string* logging_string_;
  bool host_destroyed_;
};

IN_PROC_BROWSER_TEST_F(RenderProcessHostTest,
                       AllProcessExitedCallsBeforeAnyHostDestroyedCalls) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url = embedded_test_server()->GetURL("/simple_page.html");
  NavigateToURL(shell(), test_url);

  std::string logging_string;
  ShellCloser shell_closer(shell(), &logging_string);
  ObserverLogger observer_logger(&logging_string);
  RenderProcessHost* rph =
      shell()->web_contents()->GetMainFrame()->GetProcess();

  // Ensure that the ShellCloser observer is first, so that it will have first
  // dibs on the ProcessExited callback.
  rph->AddObserver(&shell_closer);
  rph->AddObserver(&observer_logger);

  // This will crash the render process, and start all the callbacks.
  // We can't use NavigateToURL here since it accesses the shell() after
  // navigating, which the shell_closer deletes.
  NavigateToURLBlockUntilNavigationsComplete(
      shell(), GURL(kChromeUICrashURL), 1);

  // The key here is that all the RenderProcessExited callbacks precede all the
  // RenderProcessHostDestroyed callbacks.
  EXPECT_EQ("ShellCloser::RenderProcessExited "
            "ObserverLogger::RenderProcessExited "
            "ShellCloser::RenderProcessHostDestroyed "
            "ObserverLogger::RenderProcessHostDestroyed ", logging_string);

  // If the test fails, and somehow the RPH is still alive somehow, at least
  // deregister the observers so that the test fails and doesn't also crash.
  if (!observer_logger.host_destroyed()) {
    rph->RemoveObserver(&shell_closer);
    rph->RemoveObserver(&observer_logger);
  }
}

IN_PROC_BROWSER_TEST_F(RenderProcessHostTest, KillProcessOnBadMojoMessage) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url = embedded_test_server()->GetURL("/simple_page.html");
  NavigateToURL(shell(), test_url);
  RenderProcessHost* rph =
      shell()->web_contents()->GetMainFrame()->GetProcess();

  host_destructions_ = 0;
  process_exits_ = 0;
  rph->AddObserver(this);

  mojom::TestServicePtr service;
  BindInterface(rph, &service);

  base::RunLoop run_loop;
  set_process_exit_callback(run_loop.QuitClosure());

  // Should reply with a bad message and cause process death.
  service->DoSomething(base::Bind(&base::DoNothing));

  run_loop.Run();

  EXPECT_EQ(1, process_exits_);
  EXPECT_EQ(0, host_destructions_);
  if (!host_destructions_)
    rph->RemoveObserver(this);
}

class MediaStopObserver : public WebContentsObserver {
 public:
  MediaStopObserver(WebContents* web_contents, base::Closure quit_closure)
      : WebContentsObserver(web_contents),
        quit_closure_(std::move(quit_closure)) {}
  ~MediaStopObserver() override {}

  void MediaStoppedPlaying(
      const WebContentsObserver::MediaPlayerInfo& media_info,
      const WebContentsObserver::MediaPlayerId& id) override {
    quit_closure_.Run();
  }

 private:
  base::Closure quit_closure_;
};

// Tests that audio stream counts (used for process priority calculations) are
// properly set and cleared during media playback and renderer terminations.
//
// Note: This test can't run when the Mojo Renderer is used since it does not
// create audio streams through the normal audio pathways; at present this is
// only used by Chromecast.
#if BUILDFLAG(ENABLE_MOJO_RENDERER)
#define KillProcessZerosAudioStreams DISABLED_KillProcessZerosAudioStreams
#endif
IN_PROC_BROWSER_TEST_F(RenderProcessHostTest, KillProcessZerosAudioStreams) {
  embedded_test_server()->ServeFilesFromSourceDirectory(
      media::GetTestDataPath());
  ASSERT_TRUE(embedded_test_server()->Start());
  NavigateToURL(shell(), embedded_test_server()->GetURL("/sfx_s16le.wav"));
  RenderProcessHostImpl* rph = static_cast<RenderProcessHostImpl*>(
      shell()->web_contents()->GetMainFrame()->GetProcess());

  {
    // Wait for media playback to complete. We use the stop signal instead of
    // the start signal here since the start signal does not mean the audio
    // has actually started playing yet. Whereas the stop signal is sent before
    // the audio device is actually torn down.
    base::RunLoop run_loop;
    MediaStopObserver stop_observer(shell()->web_contents(),
                                    run_loop.QuitClosure());
    run_loop.Run();

    // No point in running the rest of the test if this is wrong.
    ASSERT_EQ(1, rph->get_audio_stream_count_for_testing());
  }

  host_destructions_ = 0;
  process_exits_ = 0;
  rph->AddObserver(this);

  mojom::TestServicePtr service;
  BindInterface(rph, &service);

  {
    // Force a bad message event to occur which will terminate the renderer.
    // Note: We post task the QuitClosure since RenderProcessExited() is called
    // before destroying BrowserMessageFilters; and the next portion of the test
    // must run after these notifications have been delivered.
    base::RunLoop run_loop;
    set_process_exit_callback(media::BindToCurrentLoop(run_loop.QuitClosure()));
    service->DoSomething(base::Bind(&base::DoNothing));
    run_loop.Run();
  }

  {
    // Cycle UI and IO loop once to ensure OnChannelClosing() has been delivered
    // to audio stream owners and they get a chance to notify of stream closure.
    base::RunLoop run_loop;
    BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                            media::BindToCurrentLoop(run_loop.QuitClosure()));
    run_loop.Run();
  }

  // Verify shutdown went as expected.
  EXPECT_EQ(0, rph->get_audio_stream_count_for_testing());
  EXPECT_EQ(1, process_exits_);
  EXPECT_EQ(0, host_destructions_);
  if (!host_destructions_)
    rph->RemoveObserver(this);
}

}  // namespace
}  // namespace content
