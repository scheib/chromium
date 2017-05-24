// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SERVICE_PROCESS_SERVICE_PROCESS_CONTROL_H_
#define CHROME_BROWSER_SERVICE_PROCESS_SERVICE_PROCESS_CONTROL_H_

#include <stdint.h>

#include <memory>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/cancelable_callback.h"
#include "base/id_map.h"
#include "base/memory/singleton.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "build/build_config.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ipc/ipc_channel_proxy.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_sender.h"

namespace base {
class CommandLine;
}

namespace cloud_print {
struct CloudPrintProxyInfo;
}  // namespace cloud_print

namespace mojo {
namespace edk {
class PeerConnection;
}
}

// A ServiceProcessControl works as a portal between the service process and
// the browser process.
//
// It is used to start and terminate the service process. It is also used
// to send and receive IPC messages from the service process.
//
// THREADING
//
// This class is accessed on the UI thread through some UI actions. It then
// talks to the IPC channel on the IO thread.
class ServiceProcessControl : public IPC::Sender,
                              public IPC::Listener,
                              public content::NotificationObserver {
 public:
  enum ServiceProcessEvent {
    SERVICE_EVENT_INITIALIZE,
    SERVICE_EVENT_ENABLED_ON_LAUNCH,
    SERVICE_EVENT_ENABLE,
    SERVICE_EVENT_DISABLE,
    SERVICE_EVENT_DISABLE_BY_POLICY,
    SERVICE_EVENT_LAUNCH,
    SERVICE_EVENT_LAUNCHED,
    SERVICE_EVENT_LAUNCH_FAILED,
    SERVICE_EVENT_CHANNEL_CONNECTED,
    SERVICE_EVENT_CHANNEL_ERROR,
    SERVICE_EVENT_INFO_REQUEST,
    SERVICE_EVENT_INFO_REPLY,
    SERVICE_EVENT_HISTOGRAMS_REQUEST,
    SERVICE_EVENT_HISTOGRAMS_REPLY,
    SERVICE_PRINTERS_REQUEST,
    SERVICE_PRINTERS_REPLY,
    SERVICE_EVENT_MAX,
  };

  using iterator = IDMap<ServiceProcessControl*>::iterator;
  using MessageQueue = std::queue<IPC::Message>;
  using CloudPrintProxyInfoCallback =
      base::Callback<void(const cloud_print::CloudPrintProxyInfo&)>;
  using PrintersCallback =
      base::Callback<void(const std::vector<std::string>&)>;

  // Returns the singleton instance of this class.
  static ServiceProcessControl* GetInstance();

  // Return true if this object is connected to the service.
  // Virtual for testing.
  virtual bool IsConnected() const;

  // If no service process is currently running, creates a new service process
  // and connects to it. If a service process is already running this method
  // will try to connect to it.
  // |success_task| is called when we have successfully launched the process
  // and connected to it.
  // |failure_task| is called when we failed to connect to the service process.
  // It is OK to pass the same value for |success_task| and |failure_task|. In
  // this case, the task is invoked on success or failure.
  // Note that if we are already connected to service process then
  // |success_task| can be invoked in the context of the Launch call.
  // Virtual for testing.
  virtual void Launch(const base::Closure& success_task,
                      const base::Closure& failure_task);

  // Disconnect the IPC channel from the service process.
  // Virtual for testing.
  virtual void Disconnect();

  // IPC::Listener implementation.
  bool OnMessageReceived(const IPC::Message& message) override;
  void OnChannelConnected(int32_t peer_pid) override;
  void OnChannelError() override;

  // IPC::Sender implementation
  bool Send(IPC::Message* message) override;

  // content::NotificationObserver implementation.
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  // Send a shutdown message to the service process. IPC channel will be
  // destroyed after calling this method.
  // Return true if the message was sent.
  // Virtual for testing.
  virtual bool Shutdown();

  // Send request for cloud print proxy info (enabled state, email, proxy id).
  // The callback gets the information when received.
  // Returns true if request was sent. Callback will be called only in case of
  // reply from service. The method resets any previous callback.
  // This call starts service if needed.
  bool GetCloudPrintProxyInfo(
      const CloudPrintProxyInfoCallback& cloud_print_status_callback);

  // Send request for histograms collected in service process.
  // Returns true if request was sent, and callback will be called in case of
  // success or timeout. The method resets any previous callback.
  // Returns false if service is not running or other failure, callback will not
  // be called in this case.
  bool GetHistograms(const base::Closure& cloud_print_status_callback,
                     const base::TimeDelta& timeout);

  // Send request for printers available for cloud print proxy.
  // The callback gets the information when received.
  // Returns true if request was sent. Callback will be called only in case of
  // reply from service. The method resets any previous callback.
  // This call starts service if needed.
  bool GetPrinters(const PrintersCallback& enumerate_printers_callback);

 private:
  // This class is responsible for launching the service process on the
  // PROCESS_LAUNCHER thread.
  class Launcher
      : public base::RefCountedThreadSafe<ServiceProcessControl::Launcher> {
   public:
    explicit Launcher(std::unique_ptr<base::CommandLine> cmd_line);
    // Execute the command line to start the process asynchronously. After the
    // command is executed |task| is called with the process handle on the UI
    // thread.
    void Run(const base::Closure& task);

    bool launched() const { return launched_; }

   private:
    friend class base::RefCountedThreadSafe<ServiceProcessControl::Launcher>;
    virtual ~Launcher();

#if !defined(OS_MACOSX)
    void DoDetectLaunched();
#endif  // !OS_MACOSX

    void DoRun();
    void Notify();
    std::unique_ptr<base::CommandLine> cmd_line_;
    base::Closure notify_task_;
    bool launched_;
    uint32_t retry_count_;
    base::Process process_;
  };

  friend class MockServiceProcessControl;
  friend class CloudPrintProxyPolicyStartupTest;

  ServiceProcessControl();
  ~ServiceProcessControl() override;

  friend struct base::DefaultSingletonTraits<ServiceProcessControl>;

  typedef std::vector<base::Closure> TaskList;

  // Message handlers
  void OnCloudPrintProxyInfo(
      const cloud_print::CloudPrintProxyInfo& proxy_info);
  void OnHistograms(const std::vector<std::string>& pickled_histograms);
  void OnPrinters(const std::vector<std::string>& printers);

  // Runs callback provided in |GetHistograms()|.
  void RunHistogramsCallback();

  // Helper method to invoke all the callbacks based on success or failure.
  void RunConnectDoneTasks();

  // Method called by Launcher when the service process is launched.
  void OnProcessLaunched();

  // Used internally to connect to the service process.
  void ConnectInternal();

  // Called when ConnectInternal's async work is done.
  void OnPeerConnectionComplete(
      std::unique_ptr<mojo::edk::PeerConnection> connection);

  // Takes ownership of the pointer. Split out for testing.
  void SetChannel(std::unique_ptr<IPC::ChannelProxy> channel);

  static void RunAllTasksHelper(TaskList* task_list);

  std::unique_ptr<mojo::edk::PeerConnection> peer_connection_;

  // IPC channel to the service process.
  std::unique_ptr<IPC::ChannelProxy> channel_;

  // Service process launcher.
  scoped_refptr<Launcher> launcher_;

  // Callbacks that get invoked when the channel is successfully connected.
  TaskList connect_success_tasks_;
  // Callbacks that get invoked when there was a connection failure.
  TaskList connect_failure_tasks_;

  // Callback that gets invoked when a printers is received from
  // the cloud print proxy.
  PrintersCallback printers_callback_;

  // Callback that gets invoked when a status message is received from
  // the cloud print proxy.
  CloudPrintProxyInfoCallback cloud_print_info_callback_;

  // Callback that gets invoked when a message with histograms is received from
  // the service process.
  base::Closure histograms_callback_;

  content::NotificationRegistrar registrar_;

  // Callback that gets invoked if service didn't reply in time.
  base::CancelableClosure histograms_timeout_callback_;

  base::WeakPtrFactory<ServiceProcessControl> weak_factory_;
};

#endif  // CHROME_BROWSER_SERVICE_PROCESS_SERVICE_PROCESS_CONTROL_H_
