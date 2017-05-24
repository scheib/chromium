/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WebSharedWorkerImpl_h
#define WebSharedWorkerImpl_h

#include "public/web/WebSharedWorker.h"

#include <memory>
#include "core/dom/ExecutionContext.h"
#include "core/workers/WorkerLoaderProxy.h"
#include "core/workers/WorkerThread.h"
#include "platform/wtf/RefPtr.h"
#include "public/platform/Platform.h"
#include "public/platform/WebAddressSpace.h"
#include "public/platform/WebContentSecurityPolicy.h"
#include "public/web/WebDevToolsAgentClient.h"
#include "public/web/WebFrameClient.h"
#include "public/web/WebSharedWorkerClient.h"
#include "web/WebSharedWorkerReportingProxyImpl.h"

namespace blink {

class WebApplicationCacheHost;
class WebApplicationCacheHostClient;
class WebLocalFrameBase;
class WebServiceWorkerNetworkProvider;
class WebSharedWorkerClient;
class WebString;
class WebURL;
class WebView;
class WorkerInspectorProxy;
class WorkerScriptLoader;

// This class is used by the worker process code to talk to the SharedWorker
// implementation. This is basically accessed on the main thread, but some
// methods must be called from a worker thread. Such methods are suffixed with
// *OnWorkerThread or have header comments.
class WebSharedWorkerImpl final : public WebFrameClient,
                                  public WebSharedWorker,
                                  public WebDevToolsAgentClient,
                                  private WorkerLoaderProxyProvider {
 public:
  explicit WebSharedWorkerImpl(WebSharedWorkerClient*);

  // WebFrameClient methods to support resource loading thru the 'shadow page'.
  std::unique_ptr<WebApplicationCacheHost> CreateApplicationCacheHost(
      WebApplicationCacheHostClient*) override;
  void FrameDetached(WebLocalFrame*, DetachType) override;
  void DidFinishDocumentLoad() override;

  // WebDevToolsAgentClient overrides.
  void SendProtocolMessage(int session_id,
                           int call_id,
                           const WebString&,
                           const WebString&) override;
  void ResumeStartup() override;
  WebDevToolsAgentClient::WebKitClientMessageLoop* CreateClientMessageLoop()
      override;

  // WebSharedWorker methods:
  void StartWorkerContext(const WebURL&,
                          const WebString& name,
                          const WebString& content_security_policy,
                          WebContentSecurityPolicyType,
                          WebAddressSpace,
                          bool data_saver_enabled) override;
  void Connect(std::unique_ptr<WebMessagePortChannel>) override;
  void TerminateWorkerContext() override;

  void PauseWorkerContextOnStart() override;
  void AttachDevTools(const WebString& host_id, int session_id) override;
  void ReattachDevTools(const WebString& host_id,
                        int sesion_id,
                        const WebString& saved_state) override;
  void DetachDevTools(int session_id) override;
  void DispatchDevToolsMessage(int session_id,
                               int call_id,
                               const WebString& method,
                               const WebString& message) override;

  std::unique_ptr<blink::WebURLLoader> CreateURLLoader() override {
    // TODO(yhirano): Stop using Platform::CreateURLLoader() here.
    return Platform::Current()->CreateURLLoader();
  }

  // Callback methods for WebSharedWorkerReportingProxyImpl.
  void CountFeature(UseCounter::Feature);
  void PostMessageToPageInspector(const String& message);
  void DidCloseWorkerGlobalScope();
  void DidTerminateWorkerThread();

 private:
  ~WebSharedWorkerImpl() override;

  WorkerThread* GetWorkerThread() { return worker_thread_.get(); }

  // Shuts down the worker thread.
  void TerminateWorkerThread();

  // Creates the shadow loader used for worker network requests.
  void InitializeLoader(bool data_saver_enabled);

  void LoadShadowPage();
  void DidReceiveScriptLoaderResponse();
  void OnScriptLoaderFinished();

  void ConnectTaskOnWorkerThread(std::unique_ptr<WebMessagePortChannel>);

  // WorkerLoaderProxyProvider
  ThreadableLoadingContext* GetThreadableLoadingContext() override;

  // 'shadow page' - created to proxy loading requests from the worker.
  // Will be accessed by worker thread when posting tasks.
  Persistent<ExecutionContext> loading_document_;
  Persistent<ThreadableLoadingContext> loading_context_;
  WebView* web_view_;
  Persistent<WebLocalFrameBase> main_frame_;
  bool asked_to_terminate_;

  std::unique_ptr<WebServiceWorkerNetworkProvider> network_provider_;

  Persistent<WorkerInspectorProxy> worker_inspector_proxy_;

  Persistent<WebSharedWorkerReportingProxyImpl> reporting_proxy_;
  std::unique_ptr<WorkerThread> worker_thread_;

  WebSharedWorkerClient* client_;

  bool pause_worker_context_on_start_;
  bool is_paused_on_start_;

  // Kept around only while main script loading is ongoing.
  RefPtr<WorkerScriptLoader> main_script_loader_;

  RefPtr<WorkerLoaderProxy> loader_proxy_;

  WebURL url_;
  WebString name_;
  WebAddressSpace creation_address_space_;
};

}  // namespace blink

#endif  // WebSharedWorkerImpl_h
