// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DummyModulator_h
#define DummyModulator_h

#include "bindings/core/v8/ScriptModule.h"
#include "core/dom/Modulator.h"
#include "platform/heap/Handle.h"

namespace blink {

class ModuleScriptLoaderClient;
class ScriptModuleResolver;
class WebTaskRunner;
class ModuleScriptFetchRequest;

// DummyModulator provides empty Modulator interface implementation w/
// NOTREACHED().
//
// DummyModulator is useful for unit-testing.
// Not all module implementation components require full-blown Modulator
// implementation. Unit tests can implement a subset of Modulator interface
// which is exercised from unit-under-test.
class DummyModulator : public Modulator {
  DISALLOW_COPY_AND_ASSIGN(DummyModulator);

 public:
  DummyModulator();
  virtual ~DummyModulator();
  DECLARE_TRACE();

  ScriptModuleResolver* GetScriptModuleResolver() override;
  WebTaskRunner* TaskRunner() override;
  ReferrerPolicy GetReferrerPolicy() override;
  SecurityOrigin* GetSecurityOrigin() override;

  void FetchTree(const ModuleScriptFetchRequest&, ModuleTreeClient*) override;
  void FetchTreeInternal(const ModuleScriptFetchRequest&,
                         const AncestorList&,
                         ModuleGraphLevel,
                         ModuleTreeClient*) override;
  void FetchSingle(const ModuleScriptFetchRequest&,
                   ModuleGraphLevel,
                   SingleModuleClient*) override;
  void FetchDescendantsForInlineScript(ModuleScript*,
                                       ModuleTreeClient*) override;
  ModuleScript* GetFetchedModuleScript(const KURL&) override;
  void FetchNewSingleModule(const ModuleScriptFetchRequest&,
                            ModuleGraphLevel,
                            ModuleScriptLoaderClient*) override;
  bool HasValidContext() override;
  ScriptModule CompileModule(const String& script,
                             const String& url_str,
                             AccessControlStatus,
                             const TextPosition&) override;
  ScriptValue InstantiateModule(ScriptModule) override;
  ScriptValue GetInstantiationError(const ModuleScript*) override;
  Vector<String> ModuleRequestsFromScriptModule(ScriptModule) override;
  void ExecuteModule(const ModuleScript*) override;

  Member<ScriptModuleResolver> resolver_;
};

}  // namespace blink

#endif  // DummyModulator_h
