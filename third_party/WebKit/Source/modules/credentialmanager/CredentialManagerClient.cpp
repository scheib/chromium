// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/credentialmanager/CredentialManagerClient.h"

#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/page/Page.h"
#include "platform/bindings/ScriptState.h"

namespace blink {

CredentialManagerClient::CredentialManagerClient(
    WebCredentialManagerClient* client)
    : client_(client) {}

CredentialManagerClient::~CredentialManagerClient() {}

DEFINE_TRACE(CredentialManagerClient) {
  Supplement<Page>::Trace(visitor);
}

// static
const char* CredentialManagerClient::SupplementName() {
  return "CredentialManagerClient";
}

// static
CredentialManagerClient* CredentialManagerClient::From(
    ExecutionContext* execution_context) {
  if (!execution_context->IsDocument() ||
      !ToDocument(execution_context)->GetPage())
    return 0;
  return From(ToDocument(execution_context)->GetPage());
}

// static
CredentialManagerClient* CredentialManagerClient::From(Page* page) {
  return static_cast<CredentialManagerClient*>(
      Supplement<Page>::From(page, SupplementName()));
}

void ProvideCredentialManagerClientTo(Page& page,
                                      CredentialManagerClient* client) {
  CredentialManagerClient::ProvideTo(
      page, CredentialManagerClient::SupplementName(), client);
}

void CredentialManagerClient::DispatchFailedSignIn(
    const WebCredential& credential,
    WebCredentialManagerClient::NotificationCallbacks* callbacks) {
  if (!client_)
    return;
  client_->DispatchFailedSignIn(credential, callbacks);
}

void CredentialManagerClient::DispatchStore(
    const WebCredential& credential,
    WebCredentialManagerClient::NotificationCallbacks* callbacks) {
  if (!client_)
    return;
  client_->DispatchStore(credential, callbacks);
}

void CredentialManagerClient::DispatchRequireUserMediation(
    WebCredentialManagerClient::NotificationCallbacks* callbacks) {
  if (!client_)
    return;
  client_->DispatchRequireUserMediation(callbacks);
}

void CredentialManagerClient::DispatchGet(
    WebCredentialMediationRequirement mediation,
    bool include_passwords,
    const WebVector<WebURL>& federations,
    WebCredentialManagerClient::RequestCallbacks* callbacks) {
  if (!client_)
    return;
  client_->DispatchGet(mediation, include_passwords, federations, callbacks);
}

}  // namespace blink
