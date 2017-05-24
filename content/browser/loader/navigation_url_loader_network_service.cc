// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/loader/navigation_url_loader_network_service.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/memory/ptr_util.h"
#include "base/trace_event/trace_event.h"
#include "content/browser/appcache/appcache_navigation_handle.h"
#include "content/browser/blob_storage/chrome_blob_storage_context.h"
#include "content/browser/frame_host/frame_tree_node.h"
#include "content/browser/frame_host/navigation_request_info.h"
#include "content/browser/loader/navigation_resource_handler.h"
#include "content/browser/loader/navigation_resource_throttle.h"
#include "content/browser/loader/navigation_url_loader_delegate.h"
#include "content/browser/resource_context_impl.h"
#include "content/browser/service_worker/service_worker_navigation_handle.h"
#include "content/browser/service_worker/service_worker_navigation_handle_core.h"
#include "content/browser/service_worker/service_worker_request_handler.h"
#include "content/browser/storage_partition_impl.h"
#include "content/browser/url_loader_factory_getter.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/browser/webui/url_data_manager_backend.h"
#include "content/browser/webui/web_ui_url_loader_factory.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/navigation_data.h"
#include "content/public/browser/navigation_ui_data.h"
#include "content/public/browser/ssl_status.h"
#include "content/public/browser/stream_handle.h"
#include "content/public/common/referrer.h"
#include "content/public/common/url_constants.h"
#include "net/base/load_flags.h"
#include "net/url_request/url_request_context.h"

namespace content {

namespace {

// Request ID for browser initiated requests. We start at -2 on the same lines
// as ResourceDispatcherHostImpl.
int g_next_request_id = -2;

WebContents* GetWebContentsFromFrameTreeNodeID(int frame_tree_node_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  FrameTreeNode* frame_tree_node =
      FrameTreeNode::GloballyFindByID(frame_tree_node_id);
  if (!frame_tree_node)
    return nullptr;

  return WebContentsImpl::FromFrameTreeNode(frame_tree_node);
}

void PrepareNavigationStartOnIO(
    std::unique_ptr<ResourceRequest> resource_request,
    ResourceContext* resource_context,
    ServiceWorkerNavigationHandleCore* service_worker_navigation_handle_core,
    AppCacheNavigationHandleCore* appcache_handle_core,
    NavigationRequestInfo* request_info,
    mojom::URLLoaderFactoryPtrInfo factory_from_ui,
    scoped_refptr<URLLoaderFactoryGetter> url_loader_factory_getter,
    const base::Callback<WebContents*(void)>& web_contents_getter,
    mojom::URLLoaderAssociatedRequest url_loader_request,
    mojom::URLLoaderClientPtr url_loader_client_to_pass,
    std::unique_ptr<service_manager::Connector> connector) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  const ResourceType resource_type = request_info->is_main_frame
                                         ? RESOURCE_TYPE_MAIN_FRAME
                                         : RESOURCE_TYPE_SUB_FRAME;

  if (resource_request->request_body) {
    AttachRequestBodyBlobDataHandles(resource_request->request_body.get(),
                                     resource_context);
  }

  mojom::URLLoaderFactoryPtr url_loader_factory_ptr;
  if (service_worker_navigation_handle_core) {
    RequestContextFrameType frame_type =
        request_info->is_main_frame ? REQUEST_CONTEXT_FRAME_TYPE_TOP_LEVEL
                                    : REQUEST_CONTEXT_FRAME_TYPE_NESTED;

    storage::BlobStorageContext* blob_storage_context = GetBlobStorageContext(
        GetChromeBlobStorageContextForResourceContext(resource_context));
    url_loader_factory_ptr =
        ServiceWorkerRequestHandler::InitializeForNavigationNetworkService(
            *resource_request, resource_context,
            service_worker_navigation_handle_core, blob_storage_context,
            request_info->begin_params.skip_service_worker, resource_type,
            request_info->begin_params.request_context_type, frame_type,
            request_info->are_ancestors_secure,
            request_info->common_params.post_data, web_contents_getter);
  }

  // If we haven't gotten one from the above, then use the one the UI thread
  // gave us, or otherwise fallback to the default.
  mojom::URLLoaderFactory* factory;
  if (url_loader_factory_ptr) {
    factory = url_loader_factory_ptr.get();
  } else {
    if (factory_from_ui.is_valid()) {
      url_loader_factory_ptr.Bind(std::move(factory_from_ui));
      factory = url_loader_factory_ptr.get();
    } else {
      if (appcache_handle_core) {
        factory = url_loader_factory_getter->GetAppCacheFactory()->get();
      } else {
        factory = url_loader_factory_getter->GetNetworkFactory()->get();
      }
    }
  }

  factory->CreateLoaderAndStart(
      std::move(url_loader_request), 0 /* routing_id? */, 0 /* request_id? */,
      mojom::kURLLoadOptionSendSSLInfo, *resource_request,
      std::move(url_loader_client_to_pass));
}

}  // namespace

NavigationURLLoaderNetworkService::NavigationURLLoaderNetworkService(
    ResourceContext* resource_context,
    StoragePartition* storage_partition,
    std::unique_ptr<NavigationRequestInfo> request_info,
    std::unique_ptr<NavigationUIData> navigation_ui_data,
    ServiceWorkerNavigationHandle* service_worker_navigation_handle,
    AppCacheNavigationHandle* appcache_handle,
    NavigationURLLoaderDelegate* delegate)
    : delegate_(delegate),
      binding_(this),
      request_info_(std::move(request_info)) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP1(
      "navigation", "Navigation timeToResponseStarted", this,
      request_info_->common_params.navigation_start, "FrameTreeNode id",
      request_info_->frame_tree_node_id);

  // TODO(scottmg): Port over stuff from RDHI::BeginNavigationRequest() here.
  auto new_request = base::MakeUnique<ResourceRequest>();

  new_request->method = request_info_->common_params.method;
  new_request->url = request_info_->common_params.url;
  new_request->first_party_for_cookies = request_info_->first_party_for_cookies;
  new_request->priority = net::HIGHEST;

  // The code below to set fields like request_initiator, referrer, etc has
  // been copied from ResourceDispatcherHostImpl. We did not refactor the
  // common code into a function, because RDHI uses accessor functions on the
  // URLRequest class to set these fields. whereas we use ResourceRequest here.
  new_request->request_initiator = request_info_->begin_params.initiator_origin;
  new_request->referrer = request_info_->common_params.referrer.url;
  new_request->referrer_policy = request_info_->common_params.referrer.policy;
  new_request->headers = request_info_->begin_params.headers;

  int load_flags = request_info_->begin_params.load_flags;
  load_flags |= net::LOAD_VERIFY_EV_CERT;
  if (request_info_->is_main_frame)
    load_flags |= net::LOAD_MAIN_FRAME_DEPRECATED;

  // Sync loads should have maximum priority and should be the only
  // requests that have the ignore limits flag set.
  DCHECK(!(load_flags & net::LOAD_IGNORE_LIMITS));

  new_request->load_flags = load_flags;

  new_request->request_body = request_info_->common_params.post_data.get();

  mojom::URLLoaderAssociatedRequest loader_associated_request =
      mojo::MakeRequest(&url_loader_associated_ptr_);
  mojom::URLLoaderClientPtr url_loader_client_ptr_to_pass;
  binding_.Bind(mojo::MakeRequest(&url_loader_client_ptr_to_pass));

  // Check if a web UI scheme wants to handle this request.
  mojom::URLLoaderFactoryPtrInfo factory_ptr_info;

  const auto& schemes = URLDataManagerBackend::GetWebUISchemes();
  if (std::find(schemes.begin(), schemes.end(), new_request->url.scheme()) !=
      schemes.end()) {
    FrameTreeNode* frame_tree_node =
        FrameTreeNode::GloballyFindByID(request_info_->frame_tree_node_id);
    factory_ptr_info = GetWebUIURLLoader(frame_tree_node).PassInterface();
  }

  g_next_request_id--;

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&PrepareNavigationStartOnIO,
                 base::Passed(std::move(new_request)), resource_context,
                 service_worker_navigation_handle
                     ? service_worker_navigation_handle->core()
                     : nullptr,
                 appcache_handle ? appcache_handle->core() : nullptr,
                 request_info_.get(), base::Passed(std::move(factory_ptr_info)),
                 static_cast<StoragePartitionImpl*>(storage_partition)
                     ->url_loader_factory_getter(),
                 base::Bind(&GetWebContentsFromFrameTreeNodeID,
                            request_info_->frame_tree_node_id),
                 base::Passed(std::move(loader_associated_request)),
                 base::Passed(std::move(url_loader_client_ptr_to_pass)),
                 base::Passed(ServiceManagerConnection::GetForProcess()
                                  ->GetConnector()
                                  ->Clone())));
}

NavigationURLLoaderNetworkService::~NavigationURLLoaderNetworkService() {}

void NavigationURLLoaderNetworkService::FollowRedirect() {
  url_loader_associated_ptr_->FollowRedirect();
}

void NavigationURLLoaderNetworkService::ProceedWithResponse() {}

void NavigationURLLoaderNetworkService::OnReceiveResponse(
    const ResourceResponseHead& head,
    const base::Optional<net::SSLInfo>& ssl_info,
    mojom::DownloadedTempFilePtr downloaded_file) {
  // TODO(scottmg): This needs to do more of what
  // NavigationResourceHandler::OnReponseStarted() does. Or maybe in
  // OnStartLoadingResponseBody().
  if (ssl_info && ssl_info->cert)
    NavigationResourceHandler::GetSSLStatusForRequest(*ssl_info, &ssl_status_);
  response_ = base::MakeRefCounted<ResourceResponse>();
  response_->head = head;
}

void NavigationURLLoaderNetworkService::OnReceiveRedirect(
    const net::RedirectInfo& redirect_info,
    const ResourceResponseHead& head) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  scoped_refptr<ResourceResponse> response(new ResourceResponse());
  response->head = head;
  delegate_->OnRequestRedirected(redirect_info, response);
}

void NavigationURLLoaderNetworkService::OnDataDownloaded(
    int64_t data_length,
    int64_t encoded_length) {}

void NavigationURLLoaderNetworkService::OnUploadProgress(
    int64_t current_position,
    int64_t total_size,
    OnUploadProgressCallback callback) {}

void NavigationURLLoaderNetworkService::OnReceiveCachedMetadata(
    const std::vector<uint8_t>& data) {}

void NavigationURLLoaderNetworkService::OnTransferSizeUpdated(
    int32_t transfer_size_diff) {}

void NavigationURLLoaderNetworkService::OnStartLoadingResponseBody(
    mojo::ScopedDataPipeConsumerHandle body) {
  DCHECK(response_);

  TRACE_EVENT_ASYNC_END2("navigation", "Navigation timeToResponseStarted", this,
                         "&NavigationURLLoaderNetworkService", this, "success",
                         true);

  // Temporarily, we pass both a stream (null) and the data pipe to the
  // delegate until PlzNavigate has shipped and we can be comfortable fully
  // switching to the data pipe.
  delegate_->OnResponseStarted(response_, nullptr, std::move(body), ssl_status_,
                               std::unique_ptr<NavigationData>(),
                               GlobalRequestID(-1, g_next_request_id),
                               false /* is_download? */, false /* is_stream */);
}

void NavigationURLLoaderNetworkService::OnComplete(
    const ResourceRequestCompletionStatus& completion_status) {
  if (completion_status.error_code != net::OK) {
    TRACE_EVENT_ASYNC_END2("navigation", "Navigation timeToResponseStarted",
                           this, "&NavigationURLLoaderNetworkService", this,
                           "success", false);

    delegate_->OnRequestFailed(completion_status.exists_in_cache,
                               completion_status.error_code);
  }
}

}  // namespace content
