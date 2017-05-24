// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CredentialManagerClient_h
#define CredentialManagerClient_h

#include "core/page/Page.h"
#include "modules/ModulesExport.h"
#include "platform/Supplementable.h"
#include "public/platform/WebCredentialManagerClient.h"
#include "public/platform/WebCredentialMediationRequirement.h"
#include "public/platform/WebVector.h"

namespace blink {

class ExecutionContext;
class Page;
class WebCredential;
class WebURL;

// CredentialManagerClient lives as a supplement to Page, and wraps the
// embedder-provided WebCredentialManagerClient's methods to make them visible
// to the bindings code.
class MODULES_EXPORT CredentialManagerClient final
    : public GarbageCollectedFinalized<CredentialManagerClient>,
      public Supplement<Page> {
  USING_GARBAGE_COLLECTED_MIXIN(CredentialManagerClient);

 public:
  explicit CredentialManagerClient(WebCredentialManagerClient*);
  virtual ~CredentialManagerClient();
  DECLARE_VIRTUAL_TRACE();

  static const char* SupplementName();
  static CredentialManagerClient* From(Page*);
  static CredentialManagerClient* From(ExecutionContext*);

  // Ownership of the callback is transferred to the callee for each of
  // the following methods.
  virtual void DispatchFailedSignIn(
      const WebCredential&,
      WebCredentialManagerClient::NotificationCallbacks*);
  virtual void DispatchStore(
      const WebCredential&,
      WebCredentialManagerClient::NotificationCallbacks*);
  virtual void DispatchRequireUserMediation(
      WebCredentialManagerClient::NotificationCallbacks*);
  virtual void DispatchGet(WebCredentialMediationRequirement,
                           bool include_passwords,
                           const WebVector<WebURL>& federations,
                           WebCredentialManagerClient::RequestCallbacks*);

 private:
  WebCredentialManagerClient* client_;
};

MODULES_EXPORT void ProvideCredentialManagerClientTo(Page&,
                                                     CredentialManagerClient*);

}  // namespace blink

#endif  // CredentialManagerClient_h
