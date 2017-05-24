// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/loader/modulescript/ModuleScriptLoader.h"

#include "bindings/core/v8/V8BindingForCore.h"
#include "core/dom/Document.h"
#include "core/dom/Modulator.h"
#include "core/dom/ModuleScript.h"
#include "core/loader/modulescript/ModuleScriptFetchRequest.h"
#include "core/loader/modulescript/ModuleScriptLoaderClient.h"
#include "core/loader/modulescript/ModuleScriptLoaderRegistry.h"
#include "core/testing/DummyModulator.h"
#include "core/testing/DummyPageHolder.h"
#include "platform/heap/Handle.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/loader/testing/FetchTestingPlatformSupport.h"
#include "platform/loader/testing/MockFetchContext.h"
#include "platform/testing/URLTestHelpers.h"
#include "platform/testing/UnitTestHelpers.h"
#include "public/platform/Platform.h"
#include "public/platform/WebURLLoaderMockFactory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

namespace {

class TestModuleScriptLoaderClient final
    : public GarbageCollectedFinalized<TestModuleScriptLoaderClient>,
      public ModuleScriptLoaderClient {
  USING_GARBAGE_COLLECTED_MIXIN(TestModuleScriptLoaderClient);

 public:
  TestModuleScriptLoaderClient() = default;
  ~TestModuleScriptLoaderClient() override {}

  DEFINE_INLINE_TRACE() { visitor->Trace(module_script_); }

  void NotifyNewSingleModuleFinished(ModuleScript* module_script) override {
    was_notify_finished_ = true;
    module_script_ = module_script;
  }

  bool WasNotifyFinished() const { return was_notify_finished_; }
  ModuleScript* GetModuleScript() { return module_script_; }

 private:
  bool was_notify_finished_ = false;
  Member<ModuleScript> module_script_;
};

class ModuleScriptLoaderTestModulator final : public DummyModulator {
 public:
  ModuleScriptLoaderTestModulator(RefPtr<ScriptState> script_state,
                                  RefPtr<SecurityOrigin> security_origin)
      : script_state_(std::move(script_state)),
        security_origin_(std::move(security_origin)) {}

  ~ModuleScriptLoaderTestModulator() override {}

  SecurityOrigin* GetSecurityOrigin() override {
    return security_origin_.Get();
  }

  ScriptModule CompileModule(const String& script,
                             const String& url_str,
                             AccessControlStatus access_control_status,
                             const TextPosition& position) override {
    ScriptState::Scope scope(script_state_.Get());
    return ScriptModule::Compile(script_state_->GetIsolate(),
                                 "export default 'foo';", "",
                                 access_control_status);
  }

  DECLARE_TRACE();

 private:
  RefPtr<ScriptState> script_state_;
  RefPtr<SecurityOrigin> security_origin_;
};

DEFINE_TRACE(ModuleScriptLoaderTestModulator) {
  DummyModulator::Trace(visitor);
}

}  // namespace

class ModuleScriptLoaderTest : public ::testing::Test {
  DISALLOW_COPY_AND_ASSIGN(ModuleScriptLoaderTest);

 public:
  ModuleScriptLoaderTest() = default;
  void SetUp() override;

  LocalFrame& GetFrame() { return dummy_page_holder_->GetFrame(); }
  Document& GetDocument() { return dummy_page_holder_->GetDocument(); }
  ResourceFetcher* Fetcher() { return fetcher_.Get(); }
  Modulator* GetModulator() { return modulator_.Get(); }

 protected:
  ScopedTestingPlatformSupport<FetchTestingPlatformSupport> platform_;
  std::unique_ptr<DummyPageHolder> dummy_page_holder_;
  Persistent<ResourceFetcher> fetcher_;
  Persistent<Modulator> modulator_;
};

void ModuleScriptLoaderTest::SetUp() {
  platform_->AdvanceClockSeconds(1.);  // For non-zero DocumentParserTimings
  dummy_page_holder_ = DummyPageHolder::Create(IntSize(500, 500));
  GetDocument().SetURL(KURL(KURL(), "https://example.test"));
  fetcher_ = ResourceFetcher::Create(
      MockFetchContext::Create(MockFetchContext::kShouldLoadNewResource));
  modulator_ = new ModuleScriptLoaderTestModulator(
      ToScriptStateForMainWorld(&GetFrame()),
      GetDocument().GetSecurityOrigin());
}

TEST_F(ModuleScriptLoaderTest, fetchDataURL) {
  ModuleScriptLoaderRegistry* registry = ModuleScriptLoaderRegistry::Create();
  KURL url(KURL(), "data:text/javascript,export default 'grapes';");
  ModuleScriptFetchRequest module_request(
      url, String(), kParserInserted, WebURLRequest::kFetchCredentialsModeOmit);
  TestModuleScriptLoaderClient* client = new TestModuleScriptLoaderClient;
  registry->Fetch(module_request, ModuleGraphLevel::kTopLevelModuleFetch,
                  GetModulator(), Fetcher(), client);

  EXPECT_TRUE(client->WasNotifyFinished())
      << "ModuleScriptLoader should finish synchronously.";
  ASSERT_TRUE(client->GetModuleScript());
  EXPECT_EQ(client->GetModuleScript()->InstantiationState(),
            ModuleInstantiationState::kUninstantiated);
}

TEST_F(ModuleScriptLoaderTest, fetchInvalidURL) {
  ModuleScriptLoaderRegistry* registry = ModuleScriptLoaderRegistry::Create();
  KURL url;
  EXPECT_FALSE(url.IsValid());
  ModuleScriptFetchRequest module_request(
      url, String(), kParserInserted, WebURLRequest::kFetchCredentialsModeOmit);
  TestModuleScriptLoaderClient* client = new TestModuleScriptLoaderClient;
  registry->Fetch(module_request, ModuleGraphLevel::kTopLevelModuleFetch,
                  GetModulator(), Fetcher(), client);

  EXPECT_TRUE(client->WasNotifyFinished())
      << "ModuleScriptLoader should finish synchronously.";
  EXPECT_FALSE(client->GetModuleScript());
}

TEST_F(ModuleScriptLoaderTest, fetchURL) {
  KURL url(kParsedURLString, "http://127.0.0.1:8000/module.js");
  URLTestHelpers::RegisterMockedURLLoad(
      url, testing::WebTestDataPath("module.js"), "text/javascript");

  ModuleScriptLoaderRegistry* registry = ModuleScriptLoaderRegistry::Create();
  ModuleScriptFetchRequest module_request(
      url, String(), kParserInserted, WebURLRequest::kFetchCredentialsModeOmit);
  TestModuleScriptLoaderClient* client = new TestModuleScriptLoaderClient;
  registry->Fetch(module_request, ModuleGraphLevel::kTopLevelModuleFetch,
                  GetModulator(), Fetcher(), client);

  EXPECT_FALSE(client->WasNotifyFinished())
      << "ModuleScriptLoader unexpectedly finished synchronously.";
  platform_->GetURLLoaderMockFactory()->ServeAsynchronousRequests();

  EXPECT_TRUE(client->WasNotifyFinished());
  EXPECT_FALSE(client->GetModuleScript());
}

}  // namespace blink
