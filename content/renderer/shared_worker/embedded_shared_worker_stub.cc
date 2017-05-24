// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/shared_worker/embedded_shared_worker_stub.h"

#include <stdint.h>
#include <utility>

#include "base/feature_list.h"
#include "base/threading/thread_task_runner_handle.h"
#include "content/child/appcache/appcache_dispatcher.h"
#include "content/child/appcache/web_application_cache_host_impl.h"
#include "content/child/request_extra_data.h"
#include "content/child/scoped_child_process_reference.h"
#include "content/child/service_worker/service_worker_handle_reference.h"
#include "content/child/service_worker/service_worker_network_provider.h"
#include "content/child/service_worker/service_worker_provider_context.h"
#include "content/child/shared_worker_devtools_agent.h"
#include "content/child/webmessageportchannel_impl.h"
#include "content/common/worker_messages.h"
#include "content/common/worker_url_loader_factory_provider.mojom.h"
#include "content/public/common/appcache_info.h"
#include "content/public/common/content_features.h"
#include "content/public/common/origin_util.h"
#include "content/renderer/devtools/devtools_agent.h"
#include "content/renderer/render_thread_impl.h"
#include "content/renderer/renderer_blink_platform_impl.h"
#include "content/renderer/service_worker/worker_fetch_context_impl.h"
#include "content/renderer/shared_worker/embedded_shared_worker_content_settings_client_proxy.h"
#include "ipc/ipc_message_macros.h"
#include "third_party/WebKit/public/platform/InterfaceProvider.h"
#include "third_party/WebKit/public/platform/URLConversion.h"
#include "third_party/WebKit/public/platform/WebSecurityOrigin.h"
#include "third_party/WebKit/public/platform/modules/serviceworker/WebServiceWorkerNetworkProvider.h"
#include "third_party/WebKit/public/web/WebSharedWorker.h"
#include "third_party/WebKit/public/web/WebSharedWorkerClient.h"
#include "url/origin.h"

namespace content {

namespace {

class SharedWorkerWebApplicationCacheHostImpl
    : public WebApplicationCacheHostImpl {
 public:
  SharedWorkerWebApplicationCacheHostImpl(
      blink::WebApplicationCacheHostClient* client)
      : WebApplicationCacheHostImpl(
            client,
            RenderThreadImpl::current()->appcache_dispatcher()->backend_proxy(),
            kAppCacheNoHostId) {}

  // Main resource loading is different for workers. The main resource is
  // loaded by the worker using WorkerScriptLoader.
  // These overrides are stubbed out.
  void WillStartMainResourceRequest(
      blink::WebURLRequest&,
      const blink::WebApplicationCacheHost*) override {}
  void DidReceiveResponseForMainResource(
      const blink::WebURLResponse&) override {}
  void DidReceiveDataForMainResource(const char* data, unsigned len) override {}
  void DidFinishLoadingMainResource(bool success) override {}

  // Cache selection is also different for workers. We know at construction
  // time what cache to select and do so then.
  // These overrides are stubbed out.
  void SelectCacheWithoutManifest() override {}
  bool SelectCacheWithManifest(const blink::WebURL& manifestURL) override {
    return true;
  }
};

// Called on the main thread only and blink owns it.
class WebServiceWorkerNetworkProviderImpl
    : public blink::WebServiceWorkerNetworkProvider {
 public:
  WebServiceWorkerNetworkProviderImpl(
      std::unique_ptr<ServiceWorkerNetworkProvider> provider,
      bool is_secure_context)
      : provider_(std::move(provider)), is_secure_context_(is_secure_context) {}

  // Blink calls this method for each request starting with the main script,
  // we tag them with the provider id.
  void WillSendRequest(blink::WebURLRequest& request) override {
    std::unique_ptr<RequestExtraData> extra_data(new RequestExtraData);
    extra_data->set_service_worker_provider_id(provider_->provider_id());
    extra_data->set_initiated_in_secure_context(is_secure_context_);
    request.SetExtraData(extra_data.release());
    // If the provider does not have a controller at this point, the renderer
    // expects subresource requests to never be handled by a controlling service
    // worker, so set the ServiceWorkerMode to skip local workers here.
    // Otherwise, a service worker that is in the process of becoming the
    // controller (i.e., via claim()) on the browser-side could handle the
    // request and break the assumptions of the renderer.
    if (request.GetRequestContext() !=
            blink::WebURLRequest::kRequestContextSharedWorker &&
        !provider_->IsControlledByServiceWorker() &&
        request.GetServiceWorkerMode() !=
            blink::WebURLRequest::ServiceWorkerMode::kNone) {
      request.SetServiceWorkerMode(
          blink::WebURLRequest::ServiceWorkerMode::kForeign);
    }
  }

  bool IsControlledByServiceWorker() override {
    return provider_->IsControlledByServiceWorker();
  }

  int64_t ServiceWorkerID() override {
    if (provider_->context()->controller())
      return provider_->context()->controller()->version_id();
    return kInvalidServiceWorkerVersionId;
  }

 private:
  std::unique_ptr<ServiceWorkerNetworkProvider> provider_;
  const bool is_secure_context_;
};

}  // namespace

EmbeddedSharedWorkerStub::EmbeddedSharedWorkerStub(
    const GURL& url,
    const base::string16& name,
    const base::string16& content_security_policy,
    blink::WebContentSecurityPolicyType security_policy_type,
    blink::WebAddressSpace creation_address_space,
    bool pause_on_start,
    int route_id,
    bool data_saver_enabled)
    : route_id_(route_id), name_(name), url_(url) {
  RenderThreadImpl::current()->AddEmbeddedWorkerRoute(route_id_, this);
  impl_ = blink::WebSharedWorker::Create(this);
  if (pause_on_start) {
    // Pause worker context when it starts and wait until either DevTools client
    // is attached or explicit resume notification is received.
    impl_->PauseWorkerContextOnStart();
  }
  worker_devtools_agent_.reset(
      new SharedWorkerDevToolsAgent(route_id, impl_));
  impl_->StartWorkerContext(
      url, blink::WebString::FromUTF16(name_),
      blink::WebString::FromUTF16(content_security_policy),
      security_policy_type, creation_address_space, data_saver_enabled);
}

EmbeddedSharedWorkerStub::~EmbeddedSharedWorkerStub() {
  RenderThreadImpl::current()->RemoveEmbeddedWorkerRoute(route_id_);
  DCHECK(!impl_);
}

bool EmbeddedSharedWorkerStub::OnMessageReceived(
    const IPC::Message& message) {
  if (worker_devtools_agent_->OnMessageReceived(message))
    return true;
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(EmbeddedSharedWorkerStub, message)
    IPC_MESSAGE_HANDLER(WorkerMsg_TerminateWorkerContext,
                        OnTerminateWorkerContext)
    IPC_MESSAGE_HANDLER(WorkerMsg_Connect, OnConnect)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void EmbeddedSharedWorkerStub::OnChannelError() {
  OnTerminateWorkerContext();
}

void EmbeddedSharedWorkerStub::WorkerReadyForInspection() {
  Send(new WorkerHostMsg_WorkerReadyForInspection(route_id_));
}

void EmbeddedSharedWorkerStub::WorkerScriptLoaded() {
  Send(new WorkerHostMsg_WorkerScriptLoaded(route_id_));
  running_ = true;
  // Process any pending connections.
  for (auto& item : pending_channels_)
    ConnectToChannel(item.first, std::move(item.second));
  pending_channels_.clear();
}

void EmbeddedSharedWorkerStub::WorkerScriptLoadFailed() {
  Send(new WorkerHostMsg_WorkerScriptLoadFailed(route_id_));
  pending_channels_.clear();
  Shutdown();
}

void EmbeddedSharedWorkerStub::CountFeature(uint32_t feature) {
  Send(new WorkerHostMsg_CountFeature(route_id_, feature));
}

void EmbeddedSharedWorkerStub::WorkerContextClosed() {
  Send(new WorkerHostMsg_WorkerContextClosed(route_id_));
}

void EmbeddedSharedWorkerStub::WorkerContextDestroyed() {
  Send(new WorkerHostMsg_WorkerContextDestroyed(route_id_));
  Shutdown();
}

void EmbeddedSharedWorkerStub::SelectAppCacheID(long long app_cache_id) {
  if (app_cache_host_) {
    // app_cache_host_ could become stale as it's owned by blink's
    // DocumentLoader. This method is assumed to be called while it's valid.
    app_cache_host_->backend()->SelectCacheForSharedWorker(
        app_cache_host_->host_id(), app_cache_id);
  }
}

blink::WebNotificationPresenter*
EmbeddedSharedWorkerStub::NotificationPresenter() {
  // TODO(horo): delete this method if we have no plan to implement this.
  NOTREACHED();
  return NULL;
}

std::unique_ptr<blink::WebApplicationCacheHost>
EmbeddedSharedWorkerStub::CreateApplicationCacheHost(
    blink::WebApplicationCacheHostClient* client) {
  std::unique_ptr<WebApplicationCacheHostImpl> host =
      base::MakeUnique<SharedWorkerWebApplicationCacheHostImpl>(client);
  app_cache_host_ = host.get();
  return std::move(host);
}

blink::WebWorkerContentSettingsClientProxy*
EmbeddedSharedWorkerStub::CreateWorkerContentSettingsClientProxy(
    const blink::WebSecurityOrigin& origin) {
  return new EmbeddedSharedWorkerContentSettingsClientProxy(
      url::Origin(origin).GetURL(), origin.IsUnique(), route_id_,
      ChildThreadImpl::current()->thread_safe_sender());
}

std::unique_ptr<blink::WebServiceWorkerNetworkProvider>
EmbeddedSharedWorkerStub::CreateServiceWorkerNetworkProvider() {
  // Create a content::ServiceWorkerNetworkProvider for this data source so
  // we can observe its requests.
  std::unique_ptr<ServiceWorkerNetworkProvider> provider(
      new ServiceWorkerNetworkProvider(
          route_id_, SERVICE_WORKER_PROVIDER_FOR_SHARED_WORKER,
          true /* is_parent_frame_secure */));

  // Blink is responsible for deleting the returned object.
  return base::MakeUnique<WebServiceWorkerNetworkProviderImpl>(
      std::move(provider), IsOriginSecure(url_));
}

void EmbeddedSharedWorkerStub::SendDevToolsMessage(
    int session_id,
    int call_id,
    const blink::WebString& message,
    const blink::WebString& state) {
  worker_devtools_agent_->SendDevToolsMessage(
      session_id, call_id, message, state);
}

blink::WebDevToolsAgentClient::WebKitClientMessageLoop*
EmbeddedSharedWorkerStub::CreateDevToolsMessageLoop() {
  return DevToolsAgent::createMessageLoopWrapper();
}

std::unique_ptr<blink::WebWorkerFetchContext>
EmbeddedSharedWorkerStub::CreateWorkerFetchContext(
    blink::WebServiceWorkerNetworkProvider* web_network_provider) {
  DCHECK(base::FeatureList::IsEnabled(features::kOffMainThreadFetch));
  mojom::WorkerURLLoaderFactoryProviderPtr worker_url_loader_factory_provider;
  RenderThreadImpl::current()
      ->blink_platform_impl()
      ->GetInterfaceProvider()
      ->GetInterface(mojo::MakeRequest(&worker_url_loader_factory_provider));
  std::unique_ptr<WorkerFetchContextImpl> worker_fetch_context =
      base::MakeUnique<WorkerFetchContextImpl>(
          worker_url_loader_factory_provider.PassInterface());
  // TODO(horo): To get the correct first_party_to_cookies for the shared
  // worker, we need to check the all documents bounded by the shared worker.
  // (crbug.com/723553)
  // https://tools.ietf.org/html/draft-west-first-party-cookies-07#section-2.1.2
  worker_fetch_context->set_first_party_for_cookies(url_);
  // TODO(horo): Currently we treat the worker context as secure if the origin
  // of the shared worker script url is secure. But according to the spec, if
  // the creation context is not secure, we should treat the worker as
  // non-secure. crbug.com/723575
  // https://w3c.github.io/webappsec-secure-contexts/#examples-shared-workers
  worker_fetch_context->set_is_secure_context(IsOriginSecure(url_));
  if (web_network_provider) {
    ServiceWorkerNetworkProvider* network_provider =
        ServiceWorkerNetworkProvider::FromWebServiceWorkerNetworkProvider(
            web_network_provider);
    worker_fetch_context->set_service_worker_provider_id(
        network_provider->provider_id());
    worker_fetch_context->set_is_controlled_by_service_worker(
        network_provider->IsControlledByServiceWorker());
  }
  return std::move(worker_fetch_context);
}

void EmbeddedSharedWorkerStub::Shutdown() {
  // WebSharedWorker must be already deleted in the blink side
  // when this is called.
  impl_ = nullptr;
  delete this;
}

bool EmbeddedSharedWorkerStub::Send(IPC::Message* message) {
  return RenderThreadImpl::current()->Send(message);
}

void EmbeddedSharedWorkerStub::ConnectToChannel(
    int connection_request_id,
    std::unique_ptr<WebMessagePortChannelImpl> channel) {
  impl_->Connect(std::move(channel));
  Send(new WorkerHostMsg_WorkerConnected(connection_request_id, route_id_));
}

void EmbeddedSharedWorkerStub::OnConnect(int connection_request_id,
                                         const MessagePort& port) {
  auto channel = base::MakeUnique<WebMessagePortChannelImpl>(port);
  if (running_) {
    ConnectToChannel(connection_request_id, std::move(channel));
  } else {
    // If two documents try to load a SharedWorker at the same time, the
    // WorkerMsg_Connect for one of the documents can come in before the
    // worker is started. Just queue up the connect and deliver it once the
    // worker starts.
    pending_channels_.emplace_back(
        std::make_pair(connection_request_id, std::move(channel)));
  }
}

void EmbeddedSharedWorkerStub::OnTerminateWorkerContext() {
  // After this we wouldn't get any IPC for this stub.
  running_ = false;
  impl_->TerminateWorkerContext();
}

}  // namespace content
