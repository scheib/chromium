// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/credentialmanager/CredentialsContainer.h"

#include <memory>

#include "bindings/core/v8/V8BindingForTesting.h"
#include "bindings/core/v8/V8GCController.h"
#include "core/dom/Document.h"
#include "modules/credentialmanager/CredentialManagerClient.h"
#include "modules/credentialmanager/CredentialRequestOptions.h"
#include "public/platform/WebCredential.h"
#include "public/platform/WebCredentialMediationRequirement.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

using ::testing::_;
using ::testing::SaveArg;

namespace {

class MockCredentialManagerClient : public WebCredentialManagerClient {
 public:
  MOCK_METHOD2(DispatchFailedSignIn,
               void(const WebCredential&, NotificationCallbacks*));
  MOCK_METHOD2(DispatchStore,
               void(const WebCredential&, NotificationCallbacks*));
  MOCK_METHOD1(DispatchRequireUserMediation, void(NotificationCallbacks*));
  MOCK_METHOD4(DispatchGet,
               void(WebCredentialMediationRequirement,
                    bool,
                    const WebVector<WebURL>& federations,
                    RequestCallbacks*));
};

}  // namespace

// Make a call to CredentialsContainer::get(). Destroy v8::Context.
// Make sure that the renderer doesn't crash if got a credential.
TEST(CredentialsContainerTest, TestGetWithDocumentDestroyed) {
  CredentialsContainer* credential_container = CredentialsContainer::Create();
  std::unique_ptr<WebCredentialManagerClient::RequestCallbacks> get_callback;
  {
    // Set up.
    V8TestingScope scope;
    scope.GetDocument().SetSecurityOrigin(
        SecurityOrigin::CreateFromString("https://example.test"));
    testing::StrictMock<MockCredentialManagerClient> mock_client;
    ProvideCredentialManagerClientTo(scope.GetPage(),
                                     new CredentialManagerClient(&mock_client));

    // Request a credential.
    WebCredentialManagerClient::RequestCallbacks* callback = nullptr;
    EXPECT_CALL(mock_client, DispatchGet(_, _, _, _))
        .WillOnce(SaveArg<3>(&callback));
    credential_container->get(scope.GetScriptState(),
                              CredentialRequestOptions());

    ASSERT_TRUE(callback);
    get_callback.reset(callback);
  }

  // Garbage collect v8::Context.
  V8GCController::CollectAllGarbageForTesting(v8::Isolate::GetCurrent());

  // Invoking the callback shouldn't crash.
  get_callback->OnSuccess(std::unique_ptr<WebCredential>());
}

}  // namespace blink
