// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/shared_worker/shared_worker_repository.h"

#include "content/common/view_messages.h"
#include "content/renderer/render_frame_impl.h"
#include "content/renderer/shared_worker/websharedworker_proxy.h"
#include "third_party/WebKit/public/web/WebSharedWorkerConnectListener.h"

namespace content {

SharedWorkerRepository::SharedWorkerRepository(RenderFrameImpl* render_frame)
    : render_frame_(render_frame){};

SharedWorkerRepository::~SharedWorkerRepository() = default;

void SharedWorkerRepository::Connect(
    const blink::WebURL& url,
    const blink::WebString& name,
    DocumentID document_id,
    const blink::WebString& content_security_policy,
    blink::WebContentSecurityPolicyType security_policy_type,
    blink::WebAddressSpace creation_address_space,
    blink::WebSharedWorkerCreationContextType creation_context_type,
    bool data_saver_enabled,
    std::unique_ptr<blink::WebMessagePortChannel> channel,
    std::unique_ptr<blink::WebSharedWorkerConnectListener> listener) {
  documents_with_workers_.insert(document_id);

  ViewHostMsg_CreateWorker_Params params;
  params.url = url;
  params.name = name.Utf16();
  params.content_security_policy = content_security_policy.Utf16();
  params.security_policy_type = security_policy_type;
  params.document_id = document_id;
  params.render_frame_route_id = render_frame_->GetRoutingID();
  params.creation_address_space = creation_address_space;
  params.creation_context_type = creation_context_type;
  params.data_saver_enabled = data_saver_enabled;
  ViewHostMsg_CreateWorker_Reply reply;

  // This proxy will self-destruct when a worker is destroyed.
  new WebSharedWorkerProxy(std::move(listener), params, std::move(channel));
}

void SharedWorkerRepository::DocumentDetached(DocumentID document) {
  std::set<DocumentID>::iterator iter = documents_with_workers_.find(document);
  if (iter != documents_with_workers_.end()) {
    // Notify the browser process that the document has shut down.
    render_frame_->Send(new ViewHostMsg_DocumentDetached(document));
    documents_with_workers_.erase(iter);
  }
}

}  // namespace content
