// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/testing/DummyModulator.h"

#include "bindings/core/v8/ScriptValue.h"
#include "core/dom/ScriptModuleResolver.h"

namespace blink {

namespace {

class EmptyScriptModuleResolver final : public ScriptModuleResolver {
 public:
  EmptyScriptModuleResolver() {}

  // We ignore RegisterModuleScript() calls caused by
  // ModuleScript::CreateForTest().
  void RegisterModuleScript(ModuleScript*) override {}

  ScriptModule Resolve(const String& specifier,
                       const ScriptModule& referrer,
                       ExceptionState&) override {
    NOTREACHED();
    return ScriptModule();
  }
};

}  // namespace

DummyModulator::DummyModulator() : resolver_(new EmptyScriptModuleResolver()) {}

DummyModulator::~DummyModulator() {}

DEFINE_TRACE(DummyModulator) {
  visitor->Trace(resolver_);
  Modulator::Trace(visitor);
}

ReferrerPolicy DummyModulator::GetReferrerPolicy() {
  NOTREACHED();
  return kReferrerPolicyDefault;
}

SecurityOrigin* DummyModulator::GetSecurityOrigin() {
  NOTREACHED();
  return nullptr;
}

ScriptModuleResolver* DummyModulator::GetScriptModuleResolver() {
  return resolver_.Get();
}

WebTaskRunner* DummyModulator::TaskRunner() {
  NOTREACHED();
  return nullptr;
};

void DummyModulator::FetchTree(const ModuleScriptFetchRequest&,
                               ModuleTreeClient*) {
  NOTREACHED();
}

void DummyModulator::FetchTreeInternal(const ModuleScriptFetchRequest&,
                                       const AncestorList&,
                                       ModuleGraphLevel,
                                       ModuleTreeClient*) {
  NOTREACHED();
};

void DummyModulator::FetchSingle(const ModuleScriptFetchRequest&,
                                 ModuleGraphLevel,
                                 SingleModuleClient*) {
  NOTREACHED();
}

void DummyModulator::FetchDescendantsForInlineScript(ModuleScript*,
                                                     ModuleTreeClient*) {
  NOTREACHED();
}

ModuleScript* DummyModulator::GetFetchedModuleScript(const KURL&) {
  NOTREACHED();
  return nullptr;
}

void DummyModulator::FetchNewSingleModule(const ModuleScriptFetchRequest&,
                                          ModuleGraphLevel,
                                          ModuleScriptLoaderClient*) {
  NOTREACHED();
}

bool DummyModulator::HasValidContext() {
  return true;
}

ScriptModule DummyModulator::CompileModule(const String& script,
                                           const String& url_str,
                                           AccessControlStatus,
                                           const TextPosition&) {
  NOTREACHED();
  return ScriptModule();
}

ScriptValue DummyModulator::InstantiateModule(ScriptModule) {
  NOTREACHED();
  return ScriptValue();
}

ScriptValue DummyModulator::GetInstantiationError(const ModuleScript*) {
  NOTREACHED();
  return ScriptValue();
}

Vector<String> DummyModulator::ModuleRequestsFromScriptModule(ScriptModule) {
  NOTREACHED();
  return Vector<String>();
}

void DummyModulator::ExecuteModule(const ModuleScript*) {
  NOTREACHED();
}

}  // namespace blink
