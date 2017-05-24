// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/service_worker/service_worker_provider_host_info.h"

#include "ipc/ipc_message.h"

namespace content {

ServiceWorkerProviderHostInfo::ServiceWorkerProviderHostInfo()
    : provider_id(kInvalidServiceWorkerProviderId),
      route_id(MSG_ROUTING_NONE),
      type(SERVICE_WORKER_PROVIDER_UNKNOWN),
      is_parent_frame_secure(false) {}

ServiceWorkerProviderHostInfo::ServiceWorkerProviderHostInfo(
    ServiceWorkerProviderHostInfo&& other)
    : provider_id(other.provider_id),
      route_id(other.route_id),
      type(other.type),
      is_parent_frame_secure(other.is_parent_frame_secure) {
  other.provider_id = kInvalidServiceWorkerProviderId;
  other.route_id = MSG_ROUTING_NONE;
  other.type = SERVICE_WORKER_PROVIDER_UNKNOWN;
  other.is_parent_frame_secure = false;
}

ServiceWorkerProviderHostInfo::ServiceWorkerProviderHostInfo(
    int provider_id,
    int route_id,
    ServiceWorkerProviderType type,
    bool is_parent_frame_secure)
    : provider_id(provider_id),
      route_id(route_id),
      type(type),
      is_parent_frame_secure(is_parent_frame_secure) {}

ServiceWorkerProviderHostInfo::~ServiceWorkerProviderHostInfo() {}

}  // namespace content
