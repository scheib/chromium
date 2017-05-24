// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/appcache/appcache_url_loader_factory.h"

#include "base/bind.h"
#include "base/logging.h"
#include "content/browser/appcache/appcache_entry.h"
#include "content/browser/appcache/appcache_policy.h"
#include "content/browser/appcache/appcache_request.h"
#include "content/browser/appcache/appcache_storage.h"
#include "content/browser/appcache/chrome_appcache_service.h"
#include "content/browser/url_loader_factory_getter.h"
#include "content/common/network_service.mojom.h"
#include "content/common/resource_request.h"
#include "content/common/url_loader.mojom.h"
#include "content/common/url_loader_factory.mojom.h"
#include "content/public/browser/browser_thread.h"
#include "mojo/public/cpp/bindings/associated_binding.h"
#include "mojo/public/cpp/bindings/associated_interface_ptr.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace content {

namespace {

// Handles AppCache URL loads for the network service.
class AppCacheURLLoader : public AppCacheStorage::Delegate,
                          public mojom::URLLoader {
 public:
  AppCacheURLLoader(const ResourceRequest& request,
                    mojom::URLLoaderAssociatedRequest url_loader_request,
                    int32_t routing_id,
                    int32_t request_id,
                    mojom::URLLoaderClientPtr client_info,
                    ChromeAppCacheService* appcache_service,
                    URLLoaderFactoryGetter* factory_getter)
      : request_(request),
        routing_id_(routing_id),
        request_id_(request_id),
        client_info_(std::move(client_info)),
        appcache_service_(appcache_service),
        factory_getter_(factory_getter),
        binding_(this, std::move(url_loader_request)) {
    binding_.set_connection_error_handler(base::Bind(
        &AppCacheURLLoader::OnConnectionError, base::Unretained(this)));
  }

  ~AppCacheURLLoader() override {}

  void Start() {
    // If the origin does not exist in the AppCache usage map, then we can
    // safely call the network service here.
    if (appcache_service_->storage()->usage_map()->find(
            request_.url.GetOrigin()) ==
        appcache_service_->storage()->usage_map()->end()) {
      factory_getter_->GetNetworkFactory()->get()->CreateLoaderAndStart(
          mojo::MakeRequest(&network_loader_request_), routing_id_, request_id_,
          mojom::kURLLoadOptionSendSSLInfo, request_, std::move(client_info_));
      return;
    }

    appcache_service_->storage()->FindResponseForMainRequest(request_.url,
                                                             GURL(), this);
  }

  // mojom::URLLoader implementation:
  void FollowRedirect() override { network_loader_request_->FollowRedirect(); }

  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override {
    DCHECK(false);
  }

 private:
  // AppCacheStorage::Delegate methods.
  void OnMainResponseFound(const GURL& url,
                           const AppCacheEntry& entry,
                           const GURL& fallback_url,
                           const AppCacheEntry& fallback_entry,
                           int64_t cache_id,
                           int64_t group_id,
                           const GURL& manifest_url) override {
    AppCachePolicy* policy = appcache_service_->appcache_policy();
    bool was_blocked_by_policy =
        !manifest_url.is_empty() && policy &&
        !policy->CanLoadAppCache(manifest_url,
                                 request_.first_party_for_cookies);

    if (was_blocked_by_policy || !entry.has_response_id() ||
        cache_id == kAppCacheNoCacheId) {
      factory_getter_->GetNetworkFactory()->get()->CreateLoaderAndStart(
          mojo::MakeRequest(&network_loader_request_), routing_id_, request_id_,
          mojom::kURLLoadOptionSendSSLInfo, request_, std::move(client_info_));
    } else {
      DLOG(WARNING) << "AppCache found for url " << url
                    << " Returning AppCache factory\n";
      // TODO(ananta)
      // Provide the plumbing to initiate AppCache requests here.
      factory_getter_->GetNetworkFactory()->get()->CreateLoaderAndStart(
          mojo::MakeRequest(&network_loader_request_), routing_id_, request_id_,
          mojom::kURLLoadOptionSendSSLInfo, request_, std::move(client_info_));
    }
  }

  void OnConnectionError() { delete this; }

  // The current request.
  ResourceRequest request_;

  // URLLoader proxy for the network service.
  mojom::URLLoaderAssociatedPtr network_loader_request_;

  // Routing id of the request. This is 0 for navigation requests. For
  // subresource requests it is non zero.
  int routing_id_;

  // Request id.
  int request_id_;

  // The URLLoaderClient pointer. We call this interface with notifications
  // about the URL load
  mojom::URLLoaderClientPtr client_info_;

  // Used to query AppCacheStorage to see if a request can be served out of the
  /// AppCache.
  scoped_refptr<ChromeAppCacheService> appcache_service_;

  // Used to retrieve the network service factory to pass requests to the
  // network service.
  scoped_refptr<URLLoaderFactoryGetter> factory_getter_;

  // Binds the URLLoaderClient with us.
  mojo::AssociatedBinding<mojom::URLLoader> binding_;

  DISALLOW_COPY_AND_ASSIGN(AppCacheURLLoader);
};

}  // namespace

// Implements the URLLoaderFactory mojom for AppCache requests.
AppCacheURLLoaderFactory::AppCacheURLLoaderFactory(
    ChromeAppCacheService* appcache_service,
    URLLoaderFactoryGetter* factory_getter)
    : appcache_service_(appcache_service), factory_getter_(factory_getter) {}

AppCacheURLLoaderFactory::~AppCacheURLLoaderFactory() {}

// static
void AppCacheURLLoaderFactory::CreateURLLoaderFactory(
    mojom::URLLoaderFactoryRequest request,
    ChromeAppCacheService* appcache_service,
    URLLoaderFactoryGetter* factory_getter) {
  std::unique_ptr<AppCacheURLLoaderFactory> factory_instance(
      new AppCacheURLLoaderFactory(appcache_service, factory_getter));
  AppCacheURLLoaderFactory* raw_factory = factory_instance.get();
  raw_factory->loader_factory_bindings_.AddBinding(std::move(factory_instance),
                                                   std::move(request));
}

void AppCacheURLLoaderFactory::CreateLoaderAndStart(
    mojom::URLLoaderAssociatedRequest url_loader_request,
    int32_t routing_id,
    int32_t request_id,
    uint32_t options,
    const ResourceRequest& request,
    mojom::URLLoaderClientPtr client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  // This will get deleted when the connection is dropped by the client.
  AppCacheURLLoader* loader = new AppCacheURLLoader(
      request, std::move(url_loader_request), routing_id, request_id,
      std::move(client), appcache_service_.get(), factory_getter_.get());
  loader->Start();
}

void AppCacheURLLoaderFactory::SyncLoad(int32_t routing_id,
                                        int32_t request_id,
                                        const ResourceRequest& request,
                                        SyncLoadCallback callback) {
  NOTREACHED();
}

}  // namespace content