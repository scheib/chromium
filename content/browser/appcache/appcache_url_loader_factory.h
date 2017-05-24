// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_APPCACHE_APPCACHE_URL_LOADER_FACTORY_H_
#define CONTENT_BROWSER_APPCACHE_APPCACHE_URL_LOADER_FACTORY_H_

#include "base/memory/ref_counted.h"
#include "content/common/url_loader_factory.mojom.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"
#include "url/gurl.h"

namespace content {
class ChromeAppCacheService;
class URLLoaderFactoryGetter;

// Implements the URLLoaderFactory mojom for AppCache requests.
class AppCacheURLLoaderFactory : public mojom::URLLoaderFactory {
 public:
  ~AppCacheURLLoaderFactory() override;

  // Factory function to create an instance of the factory.
  // The |appcache_service| parameter is used to query the underlying
  // AppCacheStorage instance to check if we can service requests from the
  // AppCache. We pass unhandled requests to the network service retrieved from
  // the |factory_getter|.
  static void CreateURLLoaderFactory(mojom::URLLoaderFactoryRequest request,
                                     ChromeAppCacheService* appcache_service,
                                     URLLoaderFactoryGetter* factory_getter);

  // mojom::URLLoaderFactory implementation.
  void CreateLoaderAndStart(
      mojom::URLLoaderAssociatedRequest url_loader_request,
      int32_t routing_id,
      int32_t request_id,
      uint32_t options,
      const ResourceRequest& request,
      mojom::URLLoaderClientPtr client) override;
  void SyncLoad(int32_t routing_id,
                int32_t request_id,
                const ResourceRequest& request,
                SyncLoadCallback callback) override;

 private:
  AppCacheURLLoaderFactory(ChromeAppCacheService* appcache_service,
                           URLLoaderFactoryGetter* factory_getter);

  // Used to query AppCacheStorage to see if a request can be served out of the
  /// AppCache.
  scoped_refptr<ChromeAppCacheService> appcache_service_;

  // Used to retrieve the network service factory to pass unhandled requests to
  // the network service.
  scoped_refptr<URLLoaderFactoryGetter> factory_getter_;

  mojo::StrongBindingSet<mojom::URLLoaderFactory> loader_factory_bindings_;

  DISALLOW_COPY_AND_ASSIGN(AppCacheURLLoaderFactory);
};

}  // namespace content

#endif  // CONTENT_BROWSER_APPCACHE_APPCACHE_URL_LOADER_FACTORY_H_
