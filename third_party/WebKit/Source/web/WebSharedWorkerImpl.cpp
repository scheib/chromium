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

#include "web/WebSharedWorkerImpl.h"

#include <memory>
#include "core/dom/Document.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/events/MessageEvent.h"
#include "core/exported/WebDataSourceImpl.h"
#include "core/frame/WebLocalFrameBase.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/loader/FrameLoadRequest.h"
#include "core/loader/FrameLoader.h"
#include "core/loader/ThreadableLoadingContext.h"
#include "core/loader/WorkerFetchContext.h"
#include "core/probe/CoreProbes.h"
#include "core/workers/ParentFrameTaskRunners.h"
#include "core/workers/SharedWorkerGlobalScope.h"
#include "core/workers/SharedWorkerThread.h"
#include "core/workers/WorkerClients.h"
#include "core/workers/WorkerContentSettingsClient.h"
#include "core/workers/WorkerGlobalScope.h"
#include "core/workers/WorkerInspectorProxy.h"
#include "core/workers/WorkerLoaderProxy.h"
#include "core/workers/WorkerScriptLoader.h"
#include "core/workers/WorkerThreadStartupData.h"
#include "platform/CrossThreadFunctional.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/heap/Handle.h"
#include "platform/heap/Persistent.h"
#include "platform/loader/fetch/ResourceResponse.h"
#include "platform/network/ContentSecurityPolicyParsers.h"
#include "platform/weborigin/KURL.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "platform/wtf/Functional.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/Platform.h"
#include "public/platform/WebMessagePortChannel.h"
#include "public/platform/WebString.h"
#include "public/platform/WebURL.h"
#include "public/platform/WebURLRequest.h"
#include "public/platform/WebWorkerFetchContext.h"
#include "public/platform/modules/serviceworker/WebServiceWorkerNetworkProvider.h"
#include "public/web/WebDevToolsAgent.h"
#include "public/web/WebFrame.h"
#include "public/web/WebSettings.h"
#include "public/web/WebView.h"
#include "public/web/WebWorkerContentSettingsClientProxy.h"
#include "web/IndexedDBClientImpl.h"
#include "web/LocalFileSystemClient.h"

namespace blink {

// TODO(toyoshim): Share implementation with WebEmbeddedWorkerImpl as much as
// possible.

WebSharedWorkerImpl::WebSharedWorkerImpl(WebSharedWorkerClient* client)
    : web_view_(nullptr),
      main_frame_(nullptr),
      asked_to_terminate_(false),
      worker_inspector_proxy_(WorkerInspectorProxy::Create()),
      client_(client),
      pause_worker_context_on_start_(false),
      is_paused_on_start_(false),
      creation_address_space_(kWebAddressSpacePublic) {
  DCHECK(IsMainThread());
}

WebSharedWorkerImpl::~WebSharedWorkerImpl() {
  DCHECK(IsMainThread());
  DCHECK(web_view_);
  // Detach the client before closing the view to avoid getting called back.
  main_frame_->SetClient(0);

  web_view_->Close();
  main_frame_->Close();
  if (loader_proxy_)
    loader_proxy_->DetachProvider(this);
}

void WebSharedWorkerImpl::TerminateWorkerThread() {
  DCHECK(IsMainThread());
  if (asked_to_terminate_)
    return;
  asked_to_terminate_ = true;
  if (main_script_loader_) {
    main_script_loader_->Cancel();
    main_script_loader_.Clear();
    client_->WorkerScriptLoadFailed();
    delete this;
    return;
  }
  if (worker_thread_)
    worker_thread_->Terminate();
  worker_inspector_proxy_->WorkerThreadTerminated();
}

void WebSharedWorkerImpl::InitializeLoader(bool data_saver_enabled) {
  DCHECK(IsMainThread());

  // Create 'shadow page'. This page is never displayed, it is used to proxy the
  // loading requests from the worker context to the rest of WebKit and Chromium
  // infrastructure.
  DCHECK(!web_view_);
  web_view_ = WebView::Create(nullptr, kWebPageVisibilityStateVisible);
  // FIXME: http://crbug.com/363843. This needs to find a better way to
  // not create graphics layers.
  web_view_->GetSettings()->SetAcceleratedCompositingEnabled(false);
  web_view_->GetSettings()->SetDataSaverEnabled(data_saver_enabled);
  // FIXME: Settings information should be passed to the Worker process from
  // Browser process when the worker is created (similar to
  // RenderThread::OnCreateNewView).
  main_frame_ = ToWebLocalFrameBase(WebLocalFrame::Create(
      WebTreeScopeType::kDocument, this,
      Platform::Current()->GetInterfaceProvider(), nullptr));
  web_view_->SetMainFrame(main_frame_.Get());
  main_frame_->SetDevToolsAgentClient(this);

  // If we were asked to pause worker context on start and wait for debugger
  // then it is the good time to do that.
  client_->WorkerReadyForInspection();
  if (pause_worker_context_on_start_) {
    is_paused_on_start_ = true;
    return;
  }
  LoadShadowPage();
}

std::unique_ptr<WebApplicationCacheHost>
WebSharedWorkerImpl::CreateApplicationCacheHost(
    WebApplicationCacheHostClient* appcache_host_client) {
  DCHECK(IsMainThread());
  return client_->CreateApplicationCacheHost(appcache_host_client);
}

void WebSharedWorkerImpl::LoadShadowPage() {
  DCHECK(IsMainThread());

  // Construct substitute data source for the 'shadow page'. We only need it
  // to have same origin as the worker so the loading checks work correctly.
  CString content("");
  RefPtr<SharedBuffer> buffer(
      SharedBuffer::Create(content.data(), content.length()));
  main_frame_->GetFrame()->Loader().Load(
      FrameLoadRequest(0, ResourceRequest(url_),
                       SubstituteData(buffer, "text/html", "UTF-8", KURL())));
}

void WebSharedWorkerImpl::FrameDetached(WebLocalFrame* frame, DetachType type) {
  DCHECK(type == DetachType::kRemove && frame->Parent());
  DCHECK(frame->FrameWidget());

  frame->Close();
}

void WebSharedWorkerImpl::DidFinishDocumentLoad() {
  DCHECK(IsMainThread());
  DCHECK(!loading_document_);
  DCHECK(!main_script_loader_);
  main_frame_->DataSource()->SetServiceWorkerNetworkProvider(
      client_->CreateServiceWorkerNetworkProvider());
  main_script_loader_ = WorkerScriptLoader::Create();
  main_script_loader_->SetRequestContext(
      WebURLRequest::kRequestContextSharedWorker);
  loading_document_ = main_frame_->GetFrame()->GetDocument();

  CrossOriginRequestPolicy cross_origin_request_policy =
      (static_cast<KURL>(url_)).ProtocolIsData() ? kAllowCrossOriginRequests
                                                 : kDenyCrossOriginRequests;

  main_script_loader_->LoadAsynchronously(
      *loading_document_.Get(), url_, cross_origin_request_policy,
      creation_address_space_,
      Bind(&WebSharedWorkerImpl::DidReceiveScriptLoaderResponse,
           WTF::Unretained(this)),
      Bind(&WebSharedWorkerImpl::OnScriptLoaderFinished,
           WTF::Unretained(this)));
  // Do nothing here since onScriptLoaderFinished() might have been already
  // invoked and |this| might have been deleted at this point.
}

void WebSharedWorkerImpl::SendProtocolMessage(int session_id,
                                              int call_id,
                                              const WebString& message,
                                              const WebString& state) {
  DCHECK(IsMainThread());
  client_->SendDevToolsMessage(session_id, call_id, message, state);
}

void WebSharedWorkerImpl::ResumeStartup() {
  DCHECK(IsMainThread());
  bool is_paused_on_start = is_paused_on_start_;
  is_paused_on_start_ = false;
  if (is_paused_on_start)
    LoadShadowPage();
}

WebDevToolsAgentClient::WebKitClientMessageLoop*
WebSharedWorkerImpl::CreateClientMessageLoop() {
  DCHECK(IsMainThread());
  return client_->CreateDevToolsMessageLoop();
}

void WebSharedWorkerImpl::CountFeature(UseCounter::Feature feature) {
  DCHECK(IsMainThread());
  client_->CountFeature(feature);
}

void WebSharedWorkerImpl::PostMessageToPageInspector(const String& message) {
  DCHECK(IsMainThread());
  worker_inspector_proxy_->DispatchMessageFromWorker(message);
}

void WebSharedWorkerImpl::DidCloseWorkerGlobalScope() {
  DCHECK(IsMainThread());
  client_->WorkerContextClosed();
  TerminateWorkerThread();
}

void WebSharedWorkerImpl::DidTerminateWorkerThread() {
  DCHECK(IsMainThread());
  client_->WorkerContextDestroyed();
  // The lifetime of this proxy is controlled by the worker context.
  delete this;
}

ThreadableLoadingContext* WebSharedWorkerImpl::GetThreadableLoadingContext() {
  if (!loading_context_) {
    loading_context_ =
        ThreadableLoadingContext::Create(*ToDocument(loading_document_.Get()));
  }
  return loading_context_;
}

void WebSharedWorkerImpl::Connect(
    std::unique_ptr<WebMessagePortChannel> web_channel) {
  DCHECK(IsMainThread());
  // The HTML spec requires to queue a connect event using the DOM manipulation
  // task source.
  // https://html.spec.whatwg.org/multipage/workers.html#shared-workers-and-the-sharedworker-interface
  TaskRunnerHelper::Get(TaskType::kDOMManipulation, GetWorkerThread())
      ->PostTask(
          BLINK_FROM_HERE,
          CrossThreadBind(&WebSharedWorkerImpl::ConnectTaskOnWorkerThread,
                          WTF::CrossThreadUnretained(this),
                          WTF::Passed(std::move(web_channel))));
}

void WebSharedWorkerImpl::ConnectTaskOnWorkerThread(
    std::unique_ptr<WebMessagePortChannel> channel) {
  // Wrap the passed-in channel in a MessagePort, and send it off via a connect
  // event.
  DCHECK(worker_thread_->IsCurrentThread());
  WorkerGlobalScope* worker_global_scope =
      ToWorkerGlobalScope(worker_thread_->GlobalScope());
  MessagePort* port = MessagePort::Create(*worker_global_scope);
  port->Entangle(std::move(channel));
  SECURITY_DCHECK(worker_global_scope->IsSharedWorkerGlobalScope());
  worker_global_scope->DispatchEvent(CreateConnectEvent(port));
}

void WebSharedWorkerImpl::StartWorkerContext(
    const WebURL& url,
    const WebString& name,
    const WebString& content_security_policy,
    WebContentSecurityPolicyType policy_type,
    WebAddressSpace creation_address_space,
    bool data_saver_enabled) {
  DCHECK(IsMainThread());
  url_ = url;
  name_ = name;
  creation_address_space_ = creation_address_space;
  InitializeLoader(data_saver_enabled);
}

void WebSharedWorkerImpl::DidReceiveScriptLoaderResponse() {
  DCHECK(IsMainThread());
  probe::didReceiveScriptResponse(loading_document_,
                                  main_script_loader_->Identifier());
  client_->SelectAppCacheID(main_script_loader_->AppCacheID());
}

void WebSharedWorkerImpl::OnScriptLoaderFinished() {
  DCHECK(IsMainThread());
  DCHECK(loading_document_);
  DCHECK(main_script_loader_);
  if (asked_to_terminate_)
    return;
  if (main_script_loader_->Failed()) {
    main_script_loader_->Cancel();
    client_->WorkerScriptLoadFailed();

    // The SharedWorker was unable to load the initial script, so
    // shut it down right here.
    delete this;
    return;
  }

  // FIXME: this document's origin is pristine and without any extra privileges
  // (e.g. GrantUniversalAccess) that can be overriden in regular documents
  // via WebPreference by embedders. (crbug.com/254993)
  SecurityOrigin* starter_origin = loading_document_->GetSecurityOrigin();

  WorkerClients* worker_clients = WorkerClients::Create();
  ProvideLocalFileSystemToWorker(worker_clients,
                                 LocalFileSystemClient::Create());
  WebSecurityOrigin web_security_origin(loading_document_->GetSecurityOrigin());
  ProvideContentSettingsClientToWorker(
      worker_clients,
      WTF::WrapUnique(client_->CreateWorkerContentSettingsClientProxy(
          web_security_origin)));
  ProvideIndexedDBClientToWorker(worker_clients,
                                 IndexedDBClientImpl::Create(*worker_clients));

  if (RuntimeEnabledFeatures::offMainThreadFetchEnabled()) {
    std::unique_ptr<WebWorkerFetchContext> web_worker_fetch_context =
        client_->CreateWorkerFetchContext(
            WebLocalFrameBase::FromFrame(main_frame_->GetFrame())
                ->DataSource()
                ->GetServiceWorkerNetworkProvider());
    DCHECK(web_worker_fetch_context);
    // TODO(horo): Set more information about the context (ex: AppCacheHostID)
    // to |web_worker_fetch_context|.
    web_worker_fetch_context->SetDataSaverEnabled(
        main_frame_->GetFrame()->GetSettings()->GetDataSaverEnabled());
    ProvideWorkerFetchContextToWorker(worker_clients,
                                      std::move(web_worker_fetch_context));
  }

  ContentSecurityPolicy* content_security_policy =
      main_script_loader_->ReleaseContentSecurityPolicy();
  WorkerThreadStartMode start_mode =
      worker_inspector_proxy_->WorkerStartMode(loading_document_);
  std::unique_ptr<WorkerSettings> worker_settings = WTF::WrapUnique(
      new WorkerSettings(main_frame_->GetFrame()->GetSettings()));
  WorkerV8Settings worker_v8_settings = WorkerV8Settings::Default();
  worker_v8_settings.atomics_wait_mode_ =
      WorkerV8Settings::AtomicsWaitMode::kAllow;
  std::unique_ptr<WorkerThreadStartupData> startup_data =
      WorkerThreadStartupData::Create(
          url_, loading_document_->UserAgent(),
          main_script_loader_->SourceText(), nullptr, start_mode,
          content_security_policy ? content_security_policy->Headers().get()
                                  : nullptr,
          main_script_loader_->GetReferrerPolicy(), starter_origin,
          worker_clients, main_script_loader_->ResponseAddressSpace(),
          main_script_loader_->OriginTrialTokens(), std::move(worker_settings),
          worker_v8_settings);

  // SharedWorker can sometimes run tasks that are initiated by/associated with
  // a document's frame but these documents can be from a different process. So
  // we intentionally populate the task runners with null document in order to
  // use the thread's default task runner. Note that |m_document| should not be
  // used as it's a dummy document for loading that doesn't represent the frame
  // of any associated document.
  ParentFrameTaskRunners* task_runners =
      ParentFrameTaskRunners::Create(nullptr);

  loader_proxy_ = WorkerLoaderProxy::Create(this);
  reporting_proxy_ = new WebSharedWorkerReportingProxyImpl(this, task_runners);
  worker_thread_ =
      SharedWorkerThread::Create(name_, loader_proxy_, *reporting_proxy_);
  probe::scriptImported(loading_document_, main_script_loader_->Identifier(),
                        main_script_loader_->SourceText());
  main_script_loader_.Clear();

  GetWorkerThread()->Start(std::move(startup_data), task_runners);
  worker_inspector_proxy_->WorkerThreadCreated(ToDocument(loading_document_),
                                               GetWorkerThread(), url_);
  client_->WorkerScriptLoaded();
}

void WebSharedWorkerImpl::TerminateWorkerContext() {
  DCHECK(IsMainThread());
  TerminateWorkerThread();
}

void WebSharedWorkerImpl::PauseWorkerContextOnStart() {
  pause_worker_context_on_start_ = true;
}

void WebSharedWorkerImpl::AttachDevTools(const WebString& host_id,
                                         int session_id) {
  WebDevToolsAgent* devtools_agent = main_frame_->DevToolsAgent();
  if (devtools_agent)
    devtools_agent->Attach(host_id, session_id);
}

void WebSharedWorkerImpl::ReattachDevTools(const WebString& host_id,
                                           int session_id,
                                           const WebString& saved_state) {
  WebDevToolsAgent* devtools_agent = main_frame_->DevToolsAgent();
  if (devtools_agent)
    devtools_agent->Reattach(host_id, session_id, saved_state);
  ResumeStartup();
}

void WebSharedWorkerImpl::DetachDevTools(int session_id) {
  WebDevToolsAgent* devtools_agent = main_frame_->DevToolsAgent();
  if (devtools_agent)
    devtools_agent->Detach(session_id);
}

void WebSharedWorkerImpl::DispatchDevToolsMessage(int session_id,
                                                  int call_id,
                                                  const WebString& method,
                                                  const WebString& message) {
  if (asked_to_terminate_)
    return;
  WebDevToolsAgent* devtools_agent = main_frame_->DevToolsAgent();
  if (devtools_agent)
    devtools_agent->DispatchOnInspectorBackend(session_id, call_id, method,
                                               message);
}

WebSharedWorker* WebSharedWorker::Create(WebSharedWorkerClient* client) {
  return new WebSharedWorkerImpl(client);
}

}  // namespace blink
