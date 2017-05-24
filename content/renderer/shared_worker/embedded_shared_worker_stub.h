// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_SHARED_WORKER_EMBEDDED_SHARED_WORKER_STUB_H_
#define CONTENT_RENDERER_SHARED_WORKER_EMBEDDED_SHARED_WORKER_STUB_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "content/child/child_message_filter.h"
#include "content/child/scoped_child_process_reference.h"
#include "ipc/ipc_listener.h"
#include "third_party/WebKit/public/platform/WebAddressSpace.h"
#include "third_party/WebKit/public/platform/WebContentSecurityPolicy.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/web/WebSharedWorkerClient.h"
#include "url/gurl.h"

namespace blink {
class WebApplicationCacheHost;
class WebApplicationCacheHostClient;
class WebNotificationPresenter;
class WebSecurityOrigin;
class WebSharedWorker;
class WebWorkerContentSettingsClientProxy;
}

namespace content {
class MessagePort;
class SharedWorkerDevToolsAgent;
class WebApplicationCacheHostImpl;
class WebMessagePortChannelImpl;

// A stub class to receive IPC from browser process and talk to
// blink::WebSharedWorker. Implements blink::WebSharedWorkerClient.
// This class is self-destruct (no one explicitly owns this), and
// deletes itself (via private Shutdown() method) when either one of
// following methods is called by blink::WebSharedWorker:
// - workerScriptLoadFailed() or
// - workerContextDestroyed()
//
// In either case the corresponding blink::WebSharedWorker also deletes
// itself.
class EmbeddedSharedWorkerStub : public IPC::Listener,
                                 public blink::WebSharedWorkerClient {
 public:
  EmbeddedSharedWorkerStub(
      const GURL& url,
      const base::string16& name,
      const base::string16& content_security_policy,
      blink::WebContentSecurityPolicyType security_policy_type,
      blink::WebAddressSpace creation_address_space,
      bool pause_on_start,
      int route_id,
      bool data_saver_enabled);

  // IPC::Listener implementation.
  bool OnMessageReceived(const IPC::Message& message) override;
  void OnChannelError() override;

  // blink::WebSharedWorkerClient implementation.
  void CountFeature(uint32_t feature) override;
  void WorkerContextClosed() override;
  void WorkerContextDestroyed() override;
  void WorkerReadyForInspection() override;
  void WorkerScriptLoaded() override;
  void WorkerScriptLoadFailed() override;
  void SelectAppCacheID(long long) override;
  blink::WebNotificationPresenter* NotificationPresenter() override;
  std::unique_ptr<blink::WebApplicationCacheHost> CreateApplicationCacheHost(
      blink::WebApplicationCacheHostClient*) override;
  blink::WebWorkerContentSettingsClientProxy*
  CreateWorkerContentSettingsClientProxy(
      const blink::WebSecurityOrigin& origin) override;
  std::unique_ptr<blink::WebServiceWorkerNetworkProvider>
  CreateServiceWorkerNetworkProvider() override;
  void SendDevToolsMessage(int session_id,
                           int call_id,
                           const blink::WebString& message,
                           const blink::WebString& state) override;
  blink::WebDevToolsAgentClient::WebKitClientMessageLoop*
  CreateDevToolsMessageLoop() override;
  std::unique_ptr<blink::WebWorkerFetchContext> CreateWorkerFetchContext(
      blink::WebServiceWorkerNetworkProvider*) override;

 private:
  ~EmbeddedSharedWorkerStub() override;

  void Shutdown();
  bool Send(IPC::Message* message);

  // WebSharedWorker will own |channel|.
  void ConnectToChannel(int connection_request_id,
                        std::unique_ptr<WebMessagePortChannelImpl> channel);

  void OnConnect(int connection_request_id,
                 const MessagePort& sent_message_port);
  void OnTerminateWorkerContext();

  int route_id_;
  base::string16 name_;
  bool running_ = false;
  GURL url_;
  blink::WebSharedWorker* impl_ = nullptr;
  std::unique_ptr<SharedWorkerDevToolsAgent> worker_devtools_agent_;

  using PendingChannel = std::pair<int /* connection_request_id */,
                                   std::unique_ptr<WebMessagePortChannelImpl>>;
  std::vector<PendingChannel> pending_channels_;

  ScopedChildProcessReference process_ref_;
  WebApplicationCacheHostImpl* app_cache_host_ = nullptr;  // Not owned.
  DISALLOW_COPY_AND_ASSIGN(EmbeddedSharedWorkerStub);
};

}  // namespace content

#endif  // CONTENT_RENDERER_SHARED_WORKER_EMBEDDED_SHARED_WORKER_STUB_H_
