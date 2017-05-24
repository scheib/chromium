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

#include "core/exported/SharedWorkerRepositoryClientImpl.h"

#include <memory>
#include <utility>
#include "core/dom/ExecutionContext.h"
#include "core/events/Event.h"
#include "core/frame/Settings.h"
#include "core/frame/UseCounter.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/probe/CoreProbes.h"
#include "core/workers/SharedWorker.h"
#include "platform/loader/fetch/ResourceResponse.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/WebContentSecurityPolicy.h"
#include "public/platform/WebMessagePortChannel.h"
#include "public/platform/WebString.h"
#include "public/platform/WebURL.h"
#include "public/web/WebKit.h"
#include "public/web/WebSharedWorker.h"
#include "public/web/WebSharedWorkerConnectListener.h"
#include "public/web/WebSharedWorkerCreationErrors.h"
#include "public/web/WebSharedWorkerRepositoryClient.h"

namespace blink {

// Implementation of the callback interface passed to the embedder. This will be
// destructed when a connection to a shared worker is established.
class SharedWorkerConnectListener final
    : public WebSharedWorkerConnectListener {
 public:
  explicit SharedWorkerConnectListener(SharedWorker* worker)
      : worker_(worker) {}

  ~SharedWorkerConnectListener() override {
    DCHECK(!worker_->IsBeingConnected());
  }

  // WebSharedWorkerConnectListener overrides.

  void WorkerCreated(WebWorkerCreationError creation_error) override {
    worker_->SetIsBeingConnected(true);

    // No nested workers (for now) - connect() should only be called from
    // document context.
    DCHECK(worker_->GetExecutionContext()->IsDocument());
    Document* document = ToDocument(worker_->GetExecutionContext());
    bool is_secure_context = worker_->GetExecutionContext()->IsSecureContext();
    switch (creation_error) {
      case kWebWorkerCreationErrorNone:
        return;
      case kWebWorkerCreationErrorSecureContextMismatch:
        UseCounter::Feature feature =
            is_secure_context
                ? UseCounter::kNonSecureSharedWorkerAccessedFromSecureContext
                : UseCounter::kSecureSharedWorkerAccessedFromNonSecureContext;
        UseCounter::Count(document, feature);
        return;
    }
  }

  void ScriptLoadFailed() override {
    worker_->DispatchEvent(Event::CreateCancelable(EventTypeNames::error));
    worker_->SetIsBeingConnected(false);
  }

  void Connected() override { worker_->SetIsBeingConnected(false); }

  void CountFeature(uint32_t feature) override {
    UseCounter::Count(worker_->GetExecutionContext(),
                      static_cast<UseCounter::Feature>(feature));
  }

  Persistent<SharedWorker> worker_;
};

static WebSharedWorkerRepositoryClient::DocumentID GetId(void* document) {
  DCHECK(document);
  return reinterpret_cast<WebSharedWorkerRepositoryClient::DocumentID>(
      document);
}

void SharedWorkerRepositoryClientImpl::Connect(
    SharedWorker* worker,
    std::unique_ptr<WebMessagePortChannel> port,
    const KURL& url,
    const String& name) {
  DCHECK(client_);

  // No nested workers (for now) - connect() should only be called from document
  // context.
  DCHECK(worker->GetExecutionContext()->IsDocument());
  Document* document = ToDocument(worker->GetExecutionContext());

  // TODO(estark): this is broken, as it only uses the first header
  // when multiple might have been sent. Fix by making the
  // SharedWorkerConnectListener interface take a map that can contain
  // multiple headers.
  std::unique_ptr<Vector<CSPHeaderAndType>> headers =
      worker->GetExecutionContext()->GetContentSecurityPolicy()->Headers();
  WebString header;
  WebContentSecurityPolicyType header_type =
      kWebContentSecurityPolicyTypeReport;

  if (headers->size() > 0) {
    header = (*headers)[0].first;
    header_type =
        static_cast<WebContentSecurityPolicyType>((*headers)[0].second);
  }

  bool is_secure_context = worker->GetExecutionContext()->IsSecureContext();
  std::unique_ptr<WebSharedWorkerConnectListener> listener =
      WTF::MakeUnique<SharedWorkerConnectListener>(worker);
  client_->Connect(
      url, name, GetId(document), header, header_type,
      worker->GetExecutionContext()->GetSecurityContext().AddressSpace(),
      is_secure_context ? kWebSharedWorkerCreationContextTypeSecure
                        : kWebSharedWorkerCreationContextTypeNonsecure,
      document->GetFrame()->GetSettings()->GetDataSaverEnabled(),
      std::move(port), std::move(listener));
}

void SharedWorkerRepositoryClientImpl::DocumentDetached(Document* document) {
  DCHECK(client_);
  client_->DocumentDetached(GetId(document));
}

SharedWorkerRepositoryClientImpl::SharedWorkerRepositoryClientImpl(
    WebSharedWorkerRepositoryClient* client)
    : client_(client) {}

}  // namespace blink
