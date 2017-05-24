// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/test_runner/mock_credential_manager_client.h"

#include <memory>
#include <utility>

#include "third_party/WebKit/public/platform/WebCredential.h"

namespace test_runner {

MockCredentialManagerClient::MockCredentialManagerClient()
    : error_(blink::kWebCredentialManagerNoError) {}

MockCredentialManagerClient::~MockCredentialManagerClient() {}

void MockCredentialManagerClient::SetResponse(
    blink::WebCredential* credential) {
  credential_.reset(credential);
}

void MockCredentialManagerClient::SetError(const std::string& error) {
  if (error == "pending")
    error_ = blink::kWebCredentialManagerPendingRequestError;
  if (error == "disabled")
    error_ = blink::kWebCredentialManagerDisabledError;
  if (error == "unknown")
    error_ = blink::kWebCredentialManagerUnknownError;
  if (error.empty())
    error_ = blink::kWebCredentialManagerNoError;
}

void MockCredentialManagerClient::DispatchStore(
    const blink::WebCredential&,
    blink::WebCredentialManagerClient::NotificationCallbacks* callbacks) {
  callbacks->OnSuccess();
  delete callbacks;
}

void MockCredentialManagerClient::DispatchRequireUserMediation(
    NotificationCallbacks* callbacks) {
  callbacks->OnSuccess();
  delete callbacks;
}

void MockCredentialManagerClient::DispatchGet(
    blink::WebCredentialMediationRequirement mediation,
    bool include_passwords,
    const blink::WebVector<blink::WebURL>& federations,
    RequestCallbacks* callbacks) {
  if (error_ != blink::kWebCredentialManagerNoError)
    callbacks->OnError(error_);
  else
    callbacks->OnSuccess(std::move(credential_));
  delete callbacks;
}

}  // namespace test_runner
