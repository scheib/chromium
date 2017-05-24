// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/dom/ModuleScript.h"

#include "bindings/core/v8/ScriptValue.h"
#include "core/dom/ScriptModuleResolver.h"
#include "platform/bindings/ScriptState.h"
#include "v8/include/v8.h"

namespace blink {

ModuleScript* ModuleScript::Create(
    const String& source_text,
    Modulator* modulator,
    const KURL& base_url,
    const String& nonce,
    ParserDisposition parser_state,
    WebURLRequest::FetchCredentialsMode credentials_mode,
    AccessControlStatus access_control_status,
    const TextPosition& start_position) {
  // https://html.spec.whatwg.org/#creating-a-module-script
  // Step 1. Let script be a new module script that this algorithm will
  // subsequently initialize.
  // Step 2. Set script's settings object to the environment settings object
  // provided.
  // Note: "script's settings object" will be "modulator".

  // Delegate to Modulator::compileModule to process Steps 3-6.
  ScriptModule result = modulator->CompileModule(
      source_text, base_url.GetString(), access_control_status, start_position);
  // Step 6: "...return null, and abort these steps."
  if (result.IsNull())
    return nullptr;

  return CreateInternal(source_text, modulator, result, base_url, nonce,
                        parser_state, credentials_mode);
}

ModuleScript* ModuleScript::CreateInternal(
    const String& source_text,
    Modulator* modulator,
    ScriptModule result,
    const KURL& base_url,
    const String& nonce,
    ParserDisposition parser_state,
    WebURLRequest::FetchCredentialsMode credentials_mode) {
  // https://html.spec.whatwg.org/#creating-a-module-script
  // Step 7. Set script's module record to result.
  // Step 8. Set script's base URL to the script base URL provided.
  // Step 9. Set script's cryptographic nonce to the cryptographic nonce
  // provided.
  // Step 10. Set script's parser state to the parser state.
  // Step 11. Set script's credentials mode to the credentials mode provided.
  // Step 12. Return script.
  // [not specced] |source_text| is saved for CSP checks.
  ModuleScript* module_script =
      new ModuleScript(modulator, result, base_url, nonce, parser_state,
                       credentials_mode, source_text);

  // Step 5, a part of ParseModule(): Passing script as the last parameter
  // here ensures result.[[HostDefined]] will be script.
  modulator->GetScriptModuleResolver()->RegisterModuleScript(module_script);

  return module_script;
}

ModuleScript* ModuleScript::CreateForTest(
    Modulator* modulator,
    ScriptModule record,
    const KURL& base_url,
    const String& nonce,
    ParserDisposition parser_state,
    WebURLRequest::FetchCredentialsMode credentials_mode) {
  String dummy_source_text = "";
  return CreateInternal(dummy_source_text, modulator, record, base_url, nonce,
                        parser_state, credentials_mode);
}

void ModuleScript::SetInstantiationErrorAndClearRecord(ScriptValue error) {
  // Implements Step 7.1 of:
  // https://html.spec.whatwg.org/multipage/webappapis.html#internal-module-script-graph-fetching-procedure

  // "set script's instantiation state to "errored", ..."
  DCHECK_EQ(instantiation_state_, ModuleInstantiationState::kUninstantiated);
  instantiation_state_ = ModuleInstantiationState::kErrored;

  // "its instantiation error to instantiationStatus.[[Value]], and ..."
  DCHECK(!error.IsEmpty());
  {
    ScriptState::Scope scope(error.GetScriptState());
    instantiation_error_.Set(error.GetIsolate(), error.V8Value());
  }

  // "its module record to null."
  record_ = ScriptModule();
}

void ModuleScript::SetInstantiationSuccess() {
  // Implements Step 7.2 of:
  // https://html.spec.whatwg.org/multipage/webappapis.html#internal-module-script-graph-fetching-procedure

  // "set script's instantiation state to "instantiated"."
  DCHECK_EQ(instantiation_state_, ModuleInstantiationState::kUninstantiated);
  instantiation_state_ = ModuleInstantiationState::kInstantiated;
}

DEFINE_TRACE(ModuleScript) {
  visitor->Trace(settings_object_);
  Script::Trace(visitor);
}
DEFINE_TRACE_WRAPPERS(ModuleScript) {
  visitor->TraceWrappers(instantiation_error_);
}

bool ModuleScript::IsEmpty() const {
  return false;
}

bool ModuleScript::CheckMIMETypeBeforeRunScript(Document* context_document,
                                                const SecurityOrigin*) const {
  // We don't check MIME type here because we check the MIME type in
  // ModuleScriptLoader::WasModuleLoadSuccessful().
  return true;
}

void ModuleScript::RunScript(LocalFrame* frame, const SecurityOrigin*) const {
  settings_object_->ExecuteModule(this);
}

String ModuleScript::InlineSourceTextForCSP() const {
  return source_text_;
}

}  // namespace blink
