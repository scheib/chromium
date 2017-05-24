// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_SERVICE_WORKER_SERVICE_WORKER_PROVIDER_HOST_INFO_H_
#define CONTENT_COMMON_SERVICE_WORKER_SERVICE_WORKER_PROVIDER_HOST_INFO_H_

#include "content/common/service_worker/service_worker_types.h"

namespace content {

// Container object to create a ServiceWorkerProviderHost.
struct CONTENT_EXPORT ServiceWorkerProviderHostInfo {
  ServiceWorkerProviderHostInfo();
  ServiceWorkerProviderHostInfo(ServiceWorkerProviderHostInfo&& other);
  ServiceWorkerProviderHostInfo(int provider_id,
                                int route_id,
                                ServiceWorkerProviderType type,
                                bool is_parent_frame_secure);
  ~ServiceWorkerProviderHostInfo();

  // This is unique within its child process except for PlzNavigate. When
  // PlzNavigate is on, |provider_id| is managed on the browser process and it
  // will be unique among all of providers.
  int provider_id;

  // When this provider is created for a document, |route_id| is the frame ID of
  // it. When this provider is created for a Shared Worker, |route_id| is the
  // Shared Worker route ID. When this provider is created for a Service Worker,
  // |route_id| is MSG_ROUTING_NONE.
  int route_id;

  // This identifies whether this provider is for Service Worker controllees
  // (documents and Shared Workers) or for controllers (Service Workers).
  ServiceWorkerProviderType type;

  // |is_parent_frame_secure| is false if the provider is created for a document
  // whose parent frame is not secure from the point of view of the document;
  // that is, blink::WebFrame::canHaveSecureChild() returns false. This doesn't
  // mean the document is necessarily an insecure context, because the document
  // may have a URL whose scheme is granted an exception that allows bypassing
  // the ancestor secure context check. See the comment in
  // blink::Document::isSecureContextImpl for more details. If the provider is
  // not created for a document, or the document does not have a parent frame,
  // is_parent_frame_secure| is true.
  bool is_parent_frame_secure;

 private:
  DISALLOW_COPY_AND_ASSIGN(ServiceWorkerProviderHostInfo);
};

}  // namespace content

#endif  // CONTENT_COMMON_SERVICE_WORKER_SERVICE_WORKER_PROVIDER_HOST_INFO_H_
