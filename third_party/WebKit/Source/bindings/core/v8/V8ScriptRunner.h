/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef V8ScriptRunner_h
#define V8ScriptRunner_h

#include <stdint.h>

#include "bindings/core/v8/ScriptValue.h"
#include "bindings/core/v8/V8CacheOptions.h"
#include "core/CoreExport.h"
#include "platform/bindings/ScriptState.h"
#include "platform/bindings/V8BindingMacros.h"
#include "platform/loader/fetch/AccessControlStatus.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/text/TextPosition.h"
#include "platform/wtf/text/WTFString.h"
#include "v8/include/v8.h"

namespace blink {

class CachedMetadataHandler;
class ScriptResource;
class ScriptSourceCode;
class ExecutionContext;
class ScriptStreamer;

class CORE_EXPORT V8ScriptRunner final {
  STATIC_ONLY(V8ScriptRunner);

 public:
  // For the following methods, the caller sites have to hold
  // a HandleScope and a ContextScope.
  static v8::MaybeLocal<v8::Script> CompileScript(const ScriptSourceCode&,
                                                  v8::Isolate*,
                                                  AccessControlStatus,
                                                  V8CacheOptions);
  static v8::MaybeLocal<v8::Script> CompileScript(const String&,
                                                  const String& file_name,
                                                  const String& source_map_url,
                                                  const TextPosition&,
                                                  v8::Isolate*,
                                                  CachedMetadataHandler*,
                                                  AccessControlStatus,
                                                  V8CacheOptions);
  // CachedMetadataHandler is set when metadata caching is supported. For
  // normal scripe resources, CachedMetadataHandler is from ScriptResource.
  // For worker script, ScriptResource is null but CachedMetadataHandler may be
  // set. When ScriptStreamer is set, ScriptResource must be set.
  static v8::MaybeLocal<v8::Script> CompileScript(v8::Local<v8::String>,
                                                  const String& file_name,
                                                  const String& source_map_url,
                                                  const TextPosition&,
                                                  v8::Isolate*,
                                                  ScriptResource*,
                                                  ScriptStreamer*,
                                                  CachedMetadataHandler*,
                                                  AccessControlStatus,
                                                  V8CacheOptions);
  static v8::MaybeLocal<v8::Module> CompileModule(v8::Isolate*,
                                                  const String& source,
                                                  const String& file_name,
                                                  AccessControlStatus,
                                                  const TextPosition&);
  static v8::MaybeLocal<v8::Value> RunCompiledScript(v8::Isolate*,
                                                     v8::Local<v8::Script>,
                                                     ExecutionContext*);
  static v8::MaybeLocal<v8::Value> CompileAndRunInternalScript(
      v8::Local<v8::String>,
      v8::Isolate*,
      const String& = String(),
      const TextPosition& = TextPosition());
  static v8::MaybeLocal<v8::Value> RunCompiledInternalScript(
      v8::Isolate*,
      v8::Local<v8::Script>);
  static v8::MaybeLocal<v8::Value> CallAsConstructor(
      v8::Isolate*,
      v8::Local<v8::Object>,
      ExecutionContext*,
      int argc = 0,
      v8::Local<v8::Value> argv[] = 0);
  static v8::MaybeLocal<v8::Value> CallInternalFunction(
      v8::Local<v8::Function>,
      v8::Local<v8::Value> receiver,
      int argc,
      v8::Local<v8::Value> info[],
      v8::Isolate*);
  static v8::MaybeLocal<v8::Value> CallFunction(v8::Local<v8::Function>,
                                                ExecutionContext*,
                                                v8::Local<v8::Value> receiver,
                                                int argc,
                                                v8::Local<v8::Value> info[],
                                                v8::Isolate*);
  static v8::MaybeLocal<v8::Value> EvaluateModule(v8::Local<v8::Module>,
                                                  v8::Local<v8::Context>,
                                                  v8::Isolate*);

  // Only to be used from ScriptModule::ReportException().
  static void ReportExceptionForModule(v8::Isolate*,
                                       v8::Local<v8::Value> exception,
                                       const String& file_name);

  static uint32_t TagForParserCache(CachedMetadataHandler*);
  static uint32_t TagForCodeCache(CachedMetadataHandler*);
  static void SetCacheTimeStamp(CachedMetadataHandler*);

  // Utilities for calling functions added to the V8 extras binding object.

  template <size_t N>
  static v8::MaybeLocal<v8::Value> CallExtra(ScriptState* script_state,
                                             const char* name,
                                             v8::Local<v8::Value> (&args)[N]) {
    return CallExtraHelper(script_state, name, N, args);
  }

  template <size_t N>
  static v8::Local<v8::Value> CallExtraOrCrash(
      ScriptState* script_state,
      const char* name,
      v8::Local<v8::Value> (&args)[N]) {
    return CallExtraHelper(script_state, name, N, args).ToLocalChecked();
  }

  // Use V8ThrowException instead of this function unless absolutely needed.
  static void ThrowException(v8::Isolate*,
                             v8::Local<v8::Value> exception,
                             const v8::ScriptOrigin&);

 private:
  static v8::MaybeLocal<v8::Value> CallExtraHelper(ScriptState*,
                                                   const char* name,
                                                   size_t num_args,
                                                   v8::Local<v8::Value>* args);
};

}  // namespace blink

#endif  // V8ScriptRunner_h
