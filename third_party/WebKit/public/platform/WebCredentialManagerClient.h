// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WebCredentialManagerClient_h
#define WebCredentialManagerClient_h

#include "public/platform/WebCallbacks.h"
#include "public/platform/WebCredentialManagerError.h"
#include "public/platform/WebCredentialMediationRequirement.h"
#include "public/platform/WebVector.h"

#include <memory>

namespace blink {

class WebCredential;
class WebURL;

// WebCredentialManagerClient is an interface which allows an embedder to
// implement 'navigator.credential.*' calls which are defined in the
// 'credentialmanager' module.
class WebCredentialManagerClient {
 public:
  typedef WebCallbacks<std::unique_ptr<WebCredential>,
                       WebCredentialManagerError>
      RequestCallbacks;
  typedef WebCallbacks<void, WebCredentialManagerError> NotificationCallbacks;

  // Ownership of the callback is transferred to the callee for each of
  // the following methods.
  virtual void DispatchFailedSignIn(const WebCredential&,
                                    NotificationCallbacks*) {}
  virtual void DispatchStore(const WebCredential&, NotificationCallbacks*) {}
  virtual void DispatchRequireUserMediation(NotificationCallbacks*) {}
  virtual void DispatchGet(WebCredentialMediationRequirement mediation,
                           bool include_passwords,
                           const WebVector<WebURL>& federations,
                           RequestCallbacks*) {}

 protected:
  virtual ~WebCredentialManagerClient() {}
};

}  // namespace blink

#endif  // WebCredentialManager_h
