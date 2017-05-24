// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/proxy_config/ios/proxy_service_factory.h"

#include <utility>

#include "base/memory/ptr_util.h"
#include "components/proxy_config/pref_proxy_config_tracker_impl.h"
#include "ios/web/public/web_thread.h"
#include "net/proxy/proxy_config_service.h"
#include "net/proxy/proxy_service.h"

// static
std::unique_ptr<net::ProxyConfigService>
ProxyServiceFactory::CreateProxyConfigService(PrefProxyConfigTracker* tracker) {
  std::unique_ptr<net::ProxyConfigService> base_service(
      net::ProxyService::CreateSystemProxyConfigService(
          web::WebThread::GetTaskRunnerForThread(web::WebThread::IO),
          web::WebThread::GetTaskRunnerForThread(web::WebThread::FILE)));
  return tracker->CreateTrackingProxyConfigService(std::move(base_service));
}

// static
std::unique_ptr<PrefProxyConfigTracker>
ProxyServiceFactory::CreatePrefProxyConfigTrackerOfProfile(
    PrefService* browser_state_prefs,
    PrefService* local_state_prefs) {
  return base::MakeUnique<PrefProxyConfigTrackerImpl>(
      browser_state_prefs,
      web::WebThread::GetTaskRunnerForThread(web::WebThread::IO));
}

// static
std::unique_ptr<PrefProxyConfigTracker>
ProxyServiceFactory::CreatePrefProxyConfigTrackerOfLocalState(
    PrefService* local_state_prefs) {
  return base::MakeUnique<PrefProxyConfigTrackerImpl>(
      local_state_prefs,
      web::WebThread::GetTaskRunnerForThread(web::WebThread::IO));
}

// static
std::unique_ptr<net::ProxyService> ProxyServiceFactory::CreateProxyService(
    net::NetLog* net_log,
    net::URLRequestContext* context,
    net::NetworkDelegate* network_delegate,
    std::unique_ptr<net::ProxyConfigService> proxy_config_service,
    bool quick_check_enabled) {
  DCHECK_CURRENTLY_ON(web::WebThread::IO);
  std::unique_ptr<net::ProxyService> proxy_service(
      net::ProxyService::CreateUsingSystemProxyResolver(
          std::move(proxy_config_service), net_log));
  proxy_service->set_quick_check_enabled(quick_check_enabled);
  return proxy_service;
}
