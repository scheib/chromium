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

#include "bindings/core/v8/V8ScriptRunner.h"

#include "bindings/core/v8/BindingSecurity.h"
#include "bindings/core/v8/ScriptSourceCode.h"
#include "bindings/core/v8/ScriptStreamer.h"
#include "bindings/core/v8/V8BindingForCore.h"
#include "bindings/core/v8/V8GCController.h"
#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/LocalFrame.h"
#include "core/inspector/InspectorTraceEvents.h"
#include "core/inspector/ThreadDebugger.h"
#include "core/loader/resource/ScriptResource.h"
#include "core/probe/CoreProbes.h"
#include "platform/Histogram.h"
#include "platform/ScriptForbiddenScope.h"
#include "platform/bindings/V8ThrowException.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/loader/fetch/CachedMetadata.h"
#include "platform/wtf/CurrentTime.h"
#include "public/platform/Platform.h"

#if OS(WIN)
#include <malloc.h>
#else
#include <alloca.h>
#endif

namespace blink {

namespace {

// Used to throw an exception before we exceed the C++ stack and crash.
// This limit was arrived at arbitrarily. crbug.com/449744
const int kMaxRecursionDepth = 44;

class V8CompileHistogram {
 public:
  enum Cacheability { kCacheable, kNoncacheable, kInlineScript };
  explicit V8CompileHistogram(Cacheability);
  ~V8CompileHistogram();

 private:
  Cacheability cacheability_;
  double time_stamp_;
};

V8CompileHistogram::V8CompileHistogram(
    V8CompileHistogram::Cacheability cacheability)
    : cacheability_(cacheability), time_stamp_(WTF::CurrentTime()) {}

V8CompileHistogram::~V8CompileHistogram() {
  int64_t elapsed_micro_seconds =
      static_cast<int64_t>((WTF::CurrentTime() - time_stamp_) * 1000000);
  switch (cacheability_) {
    case kCacheable: {
      DEFINE_THREAD_SAFE_STATIC_LOCAL(
          CustomCountHistogram, compile_cacheable_histogram,
          new CustomCountHistogram("V8.CompileCacheableMicroSeconds", 0,
                                   1000000, 50));
      compile_cacheable_histogram.Count(elapsed_micro_seconds);
      break;
    }
    case kNoncacheable: {
      DEFINE_THREAD_SAFE_STATIC_LOCAL(
          CustomCountHistogram, compile_non_cacheable_histogram,
          new CustomCountHistogram("V8.CompileNoncacheableMicroSeconds", 0,
                                   1000000, 50));
      compile_non_cacheable_histogram.Count(elapsed_micro_seconds);
      break;
    }
    case kInlineScript: {
      DEFINE_THREAD_SAFE_STATIC_LOCAL(
          CustomCountHistogram, compile_inline_histogram,
          new CustomCountHistogram("V8.CompileInlineScriptMicroSeconds", 0,
                                   1000000, 50));
      compile_inline_histogram.Count(elapsed_micro_seconds);
      break;
    }
  }
}

// In order to make sure all pending messages to be processed in
// v8::Function::Call, we don't call throwStackOverflowException
// directly. Instead, we create a v8::Function of
// throwStackOverflowException and call it.
void ThrowStackOverflowException(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  V8ThrowException::ThrowRangeError(info.GetIsolate(),
                                    "Maximum call stack size exceeded.");
}

void ThrowScriptForbiddenException(v8::Isolate* isolate) {
  V8ThrowException::ThrowError(isolate, "Script execution is forbidden.");
}

v8::Local<v8::Value> ThrowStackOverflowExceptionIfNeeded(v8::Isolate* isolate) {
  if (V8PerIsolateData::From(isolate)->IsHandlingRecursionLevelError()) {
    // If we are already handling a recursion level error, we should
    // not invoke v8::Function::Call.
    return v8::Undefined(isolate);
  }
  v8::MicrotasksScope microtasks_scope(
      isolate, v8::MicrotasksScope::kDoNotRunMicrotasks);
  V8PerIsolateData::From(isolate)->SetIsHandlingRecursionLevelError(true);
  v8::Local<v8::Value> result =
      v8::Function::New(isolate->GetCurrentContext(),
                        ThrowStackOverflowException, v8::Local<v8::Value>(), 0,
                        v8::ConstructorBehavior::kThrow)
          .ToLocalChecked()
          ->Call(v8::Undefined(isolate), 0, 0);
  V8PerIsolateData::From(isolate)->SetIsHandlingRecursionLevelError(false);
  return result;
}

// Compile a script without any caching or compile options.
v8::MaybeLocal<v8::Script> CompileWithoutOptions(
    V8CompileHistogram::Cacheability cacheability,
    v8::Isolate* isolate,
    v8::Local<v8::String> code,
    v8::ScriptOrigin origin) {
  V8CompileHistogram histogram_scope(cacheability);
  v8::ScriptCompiler::Source source(code, origin);
  return v8::ScriptCompiler::Compile(isolate->GetCurrentContext(), &source,
                                     v8::ScriptCompiler::kNoCompileOptions);
}

// Compile a script, and consume a V8 cache that was generated previously.
static v8::MaybeLocal<v8::Script> CompileAndConsumeCache(
    CachedMetadataHandler* cache_handler,
    PassRefPtr<CachedMetadata> cached_metadata,
    v8::ScriptCompiler::CompileOptions compile_options,
    v8::Isolate* isolate,
    v8::Local<v8::String> code,
    v8::ScriptOrigin origin) {
  V8CompileHistogram histogram_scope(V8CompileHistogram::kCacheable);
  const char* data = cached_metadata->Data();
  int length = cached_metadata->size();
  v8::ScriptCompiler::CachedData* cached_data =
      new v8::ScriptCompiler::CachedData(
          reinterpret_cast<const uint8_t*>(data), length,
          v8::ScriptCompiler::CachedData::BufferNotOwned);
  v8::ScriptCompiler::Source source(code, origin, cached_data);
  v8::MaybeLocal<v8::Script> script = v8::ScriptCompiler::Compile(
      isolate->GetCurrentContext(), &source, compile_options);
  if (cached_data->rejected)
    cache_handler->ClearCachedMetadata(CachedMetadataHandler::kSendToPlatform);
  return script;
}

// Compile a script, and produce a V8 cache for future use.
v8::MaybeLocal<v8::Script> CompileAndProduceCache(
    CachedMetadataHandler* cache_handler,
    uint32_t tag,
    v8::ScriptCompiler::CompileOptions compile_options,
    CachedMetadataHandler::CacheType cache_type,
    v8::Isolate* isolate,
    v8::Local<v8::String> code,
    v8::ScriptOrigin origin) {
  V8CompileHistogram histogram_scope(V8CompileHistogram::kCacheable);
  v8::ScriptCompiler::Source source(code, origin);
  v8::MaybeLocal<v8::Script> script = v8::ScriptCompiler::Compile(
      isolate->GetCurrentContext(), &source, compile_options);
  const v8::ScriptCompiler::CachedData* cached_data = source.GetCachedData();
  if (cached_data) {
    const char* data = reinterpret_cast<const char*>(cached_data->data);
    int length = cached_data->length;
    if (length > 1024) {
      // Omit histogram samples for small cache data to avoid outliers.
      int cache_size_ratio = static_cast<int>(100.0 * length / code->Length());
      DEFINE_THREAD_SAFE_STATIC_LOCAL(
          CustomCountHistogram, code_cache_size_histogram,
          new CustomCountHistogram("V8.CodeCacheSizeRatio", 0, 10000, 50));
      code_cache_size_histogram.Count(cache_size_ratio);
    }
    cache_handler->ClearCachedMetadata(CachedMetadataHandler::kCacheLocally);
    cache_handler->SetCachedMetadata(tag, data, length, cache_type);
  }
  return script;
}

// Compile a script, and consume or produce a V8 Cache, depending on whether the
// given resource already has cached data available.
v8::MaybeLocal<v8::Script> CompileAndConsumeOrProduce(
    CachedMetadataHandler* cache_handler,
    uint32_t tag,
    v8::ScriptCompiler::CompileOptions consume_options,
    v8::ScriptCompiler::CompileOptions produce_options,
    CachedMetadataHandler::CacheType cache_type,
    v8::Isolate* isolate,
    v8::Local<v8::String> code,
    v8::ScriptOrigin origin) {
  RefPtr<CachedMetadata> code_cache(cache_handler->GetCachedMetadata(tag));
  return code_cache.Get()
             ? CompileAndConsumeCache(cache_handler, code_cache,
                                      consume_options, isolate, code, origin)
             : CompileAndProduceCache(cache_handler, tag, produce_options,
                                      cache_type, isolate, code, origin);
}

enum CacheTagKind {
  kCacheTagParser = 0,
  kCacheTagCode = 1,
  kCacheTagTimeStamp = 3,
  kCacheTagLast
};

static const int kCacheTagKindSize = 2;

uint32_t CacheTag(CacheTagKind kind, CachedMetadataHandler* cache_handler) {
  static_assert((1 << kCacheTagKindSize) >= kCacheTagLast,
                "CacheTagLast must be large enough");

  static uint32_t v8_cache_data_version =
      v8::ScriptCompiler::CachedDataVersionTag() << kCacheTagKindSize;

  // A script can be (successfully) interpreted with different encodings,
  // depending on the page it appears in. The cache doesn't know anything
  // about encodings, but the cached data is specific to one encoding. If we
  // later load the script from the cache and interpret it with a different
  // encoding, the cached data is not valid for that encoding.
  return (v8_cache_data_version | kind) +
         StringHash::GetHash(cache_handler->Encoding());
}

// Check previously stored timestamp.
bool IsResourceHotForCaching(CachedMetadataHandler* cache_handler,
                             int hot_hours) {
  const double cache_within_seconds = hot_hours * 60 * 60;
  uint32_t tag = CacheTag(kCacheTagTimeStamp, cache_handler);
  RefPtr<CachedMetadata> cached_metadata =
      cache_handler->GetCachedMetadata(tag);
  if (!cached_metadata)
    return false;
  double time_stamp;
  const int size = sizeof(time_stamp);
  DCHECK_EQ(cached_metadata->size(), static_cast<unsigned long>(size));
  memcpy(&time_stamp, cached_metadata->Data(), size);
  return (WTF::CurrentTime() - time_stamp) < cache_within_seconds;
}

// Final compile call for a streamed compilation. Most decisions have already
// been made, but we need to write back data into the cache.
v8::MaybeLocal<v8::Script> PostStreamCompile(
    V8CacheOptions cache_options,
    CachedMetadataHandler* cache_handler,
    ScriptStreamer* streamer,
    v8::Isolate* isolate,
    v8::Local<v8::String> code,
    v8::ScriptOrigin origin) {
  V8CompileHistogram histogram_scope(V8CompileHistogram::kNoncacheable);
  v8::MaybeLocal<v8::Script> script = v8::ScriptCompiler::Compile(
      isolate->GetCurrentContext(), streamer->Source(), code, origin);

  if (!cache_handler)
    return script;

  // If the non-streaming compiler uses the parser cache, retrieve and store
  // the cache data. If the code cache uses time stamp as heuristic, set that
  // time stamp.
  switch (cache_options) {
    case kV8CacheOptionsParse: {
      const v8::ScriptCompiler::CachedData* new_cached_data =
          streamer->Source()->GetCachedData();
      if (!new_cached_data)
        break;
      CachedMetadataHandler::CacheType cache_type =
          (cache_options == kV8CacheOptionsParse)
              ? CachedMetadataHandler::kSendToPlatform
              : CachedMetadataHandler::kCacheLocally;
      cache_handler->ClearCachedMetadata(cache_type);
      cache_handler->SetCachedMetadata(
          CacheTag(kCacheTagParser, cache_handler),
          reinterpret_cast<const char*>(new_cached_data->data),
          new_cached_data->length, cache_type);
      break;
    }

    case kV8CacheOptionsDefault:
    case kV8CacheOptionsCode:
      V8ScriptRunner::SetCacheTimeStamp(cache_handler);
      break;

    case kV8CacheOptionsAlways:
      // Currently V8CacheOptionsAlways doesn't support streaming.
      NOTREACHED();
    case kV8CacheOptionsNone:
      break;
  }
  return script;
}

typedef Function<v8::MaybeLocal<v8::Script>(v8::Isolate*,
                                            v8::Local<v8::String>,
                                            v8::ScriptOrigin)>
    CompileFn;

// A notation convenience: WTF::bind<...> needs to be given the right argument
// types. We have an awful lot of bind calls below, all with the same types, so
// this local bind lets WTF::bind to all the work, but 'knows' the right
// parameter types.
// This version isn't quite as smart as the real WTF::bind, though, so you
// sometimes may still have to call the original.
template <typename... A>
std::unique_ptr<CompileFn> Bind(const A&... args) {
  return WTF::Bind(args...);
}

// Select a compile function from any of the above, mainly depending on
// cacheOptions.
static std::unique_ptr<CompileFn> SelectCompileFunction(
    V8CacheOptions cache_options,
    CachedMetadataHandler* cache_handler,
    PassRefPtr<CachedMetadata> code_cache,
    v8::Local<v8::String> code,
    V8CompileHistogram::Cacheability cacheability_if_no_handler) {
  static const int kMinimalCodeLength = 1024;
  static const int kHotHours = 72;

  // Caching is not available in this case.
  if (!cache_handler)
    return Bind(CompileWithoutOptions, cacheability_if_no_handler);

  if (cache_options == kV8CacheOptionsNone)
    return Bind(CompileWithoutOptions, V8CompileHistogram::kCacheable);

  // Caching is not worthwhile for small scripts.  Do not use caching
  // unless explicitly expected, indicated by the cache option.
  if (code->Length() < kMinimalCodeLength)
    return Bind(CompileWithoutOptions, V8CompileHistogram::kCacheable);

  // The cacheOptions will guide our strategy:
  switch (cache_options) {
    case kV8CacheOptionsParse:
      // Use parser-cache; in-memory only.
      return Bind(CompileAndConsumeOrProduce, WrapPersistent(cache_handler),
                  CacheTag(kCacheTagParser, cache_handler),
                  v8::ScriptCompiler::kConsumeParserCache,
                  v8::ScriptCompiler::kProduceParserCache,
                  CachedMetadataHandler::kCacheLocally);
      break;

    case kV8CacheOptionsDefault:
    case kV8CacheOptionsCode:
    case kV8CacheOptionsAlways: {
      // Use code caching for recently seen resources.
      // Use compression depending on the cache option.
      if (code_cache) {
        return Bind(CompileAndConsumeCache, WrapPersistent(cache_handler),
                    std::move(code_cache),
                    v8::ScriptCompiler::kConsumeCodeCache);
      }
      if (cache_options != kV8CacheOptionsAlways &&
          !IsResourceHotForCaching(cache_handler, kHotHours)) {
        V8ScriptRunner::SetCacheTimeStamp(cache_handler);
        return Bind(CompileWithoutOptions, V8CompileHistogram::kCacheable);
      }
      uint32_t code_cache_tag = CacheTag(kCacheTagCode, cache_handler);
      return Bind(CompileAndProduceCache, WrapPersistent(cache_handler),
                  code_cache_tag, v8::ScriptCompiler::kProduceCodeCache,
                  CachedMetadataHandler::kSendToPlatform);
      break;
    }

    case kV8CacheOptionsNone:
      // Shouldn't happen, as this is handled above.
      // Case is here so that compiler can check all cases are handled.
      NOTREACHED();
      break;
  }

  // All switch branches should return and we should never get here.
  // But some compilers aren't sure, hence this default.
  NOTREACHED();
  return Bind(CompileWithoutOptions, V8CompileHistogram::kCacheable);
}

// Select a compile function for a streaming compile.
std::unique_ptr<CompileFn> SelectCompileFunction(V8CacheOptions cache_options,
                                                 ScriptResource* resource,
                                                 ScriptStreamer* streamer) {
  // We don't stream scripts which don't have a Resource.
  DCHECK(resource);
  // Failed resources should never get this far.
  DCHECK(!resource->ErrorOccurred());
  DCHECK(streamer->IsFinished());
  DCHECK(!streamer->StreamingSuppressed());
  return WTF::Bind(PostStreamCompile, cache_options,
                   WrapPersistent(resource->CacheHandler()),
                   WrapPersistent(streamer));
}
}  // namespace

v8::MaybeLocal<v8::Script> V8ScriptRunner::CompileScript(
    const ScriptSourceCode& source,
    v8::Isolate* isolate,
    AccessControlStatus access_control_status,
    V8CacheOptions cache_options) {
  if (source.Source().length() >= v8::String::kMaxLength) {
    V8ThrowException::ThrowError(isolate, "Source file too large.");
    return v8::Local<v8::Script>();
  }
  return CompileScript(
      V8String(isolate, source.Source()), source.Url(), source.SourceMapUrl(),
      source.StartPosition(), isolate, source.GetResource(), source.Streamer(),
      source.GetResource() ? source.GetResource()->CacheHandler() : nullptr,
      access_control_status, cache_options);
}

v8::MaybeLocal<v8::Script> V8ScriptRunner::CompileScript(
    const String& code,
    const String& file_name,
    const String& source_map_url,
    const TextPosition& text_position,
    v8::Isolate* isolate,
    CachedMetadataHandler* cache_metadata_handler,
    AccessControlStatus access_control_status,
    V8CacheOptions v8_cache_options) {
  if (code.length() >= v8::String::kMaxLength) {
    V8ThrowException::ThrowError(isolate, "Source file too large.");
    return v8::Local<v8::Script>();
  }
  return CompileScript(V8String(isolate, code), file_name, source_map_url,
                       text_position, isolate, nullptr, nullptr,
                       cache_metadata_handler, access_control_status,
                       v8_cache_options);
}

v8::MaybeLocal<v8::Script> V8ScriptRunner::CompileScript(
    v8::Local<v8::String> code,
    const String& file_name,
    const String& source_map_url,
    const TextPosition& script_start_position,
    v8::Isolate* isolate,
    ScriptResource* resource,
    ScriptStreamer* streamer,
    CachedMetadataHandler* cache_handler,
    AccessControlStatus access_control_status,
    V8CacheOptions cache_options) {
  TRACE_EVENT2(
      "v8,devtools.timeline", "v8.compile", "fileName", file_name.Utf8(),
      "data",
      InspectorCompileScriptEvent::Data(file_name, script_start_position));

  DCHECK(!streamer || resource);
  DCHECK(!resource || resource->CacheHandler() == cache_handler);

  // NOTE: For compatibility with WebCore, ScriptSourceCode's line starts at
  // 1, whereas v8 starts at 0.
  v8::ScriptOrigin origin(
      V8String(isolate, file_name),
      v8::Integer::New(isolate, script_start_position.line_.ZeroBasedInt()),
      v8::Integer::New(isolate, script_start_position.column_.ZeroBasedInt()),
      v8::Boolean::New(isolate, access_control_status == kSharableCrossOrigin),
      v8::Local<v8::Integer>(), V8String(isolate, source_map_url),
      v8::Boolean::New(isolate, access_control_status == kOpaqueResource));

  V8CompileHistogram::Cacheability cacheability_if_no_handler =
      V8CompileHistogram::Cacheability::kNoncacheable;
  if (!cache_handler && (script_start_position.line_.ZeroBasedInt() == 0) &&
      (script_start_position.column_.ZeroBasedInt() == 0))
    cacheability_if_no_handler =
        V8CompileHistogram::Cacheability::kInlineScript;

  RefPtr<CachedMetadata> code_cache(
      cache_handler ? cache_handler->GetCachedMetadata(
                          CacheTag(kCacheTagCode, cache_handler))
                    : nullptr);
  std::unique_ptr<CompileFn> compile_fn =
      streamer ? SelectCompileFunction(cache_options, resource, streamer)
               : SelectCompileFunction(cache_options, cache_handler, code_cache,
                                       code, cacheability_if_no_handler);

  return (*compile_fn)(isolate, code, origin);
}

v8::MaybeLocal<v8::Module> V8ScriptRunner::CompileModule(
    v8::Isolate* isolate,
    const String& source,
    const String& file_name,
    AccessControlStatus access_control_status,
    const TextPosition& start_position) {
  TRACE_EVENT1("v8", "v8.compileModule", "fileName", file_name.Utf8());
  // TODO(adamk): Add Inspector integration?
  // TODO(adamk): Pass more info into ScriptOrigin.
  v8::ScriptOrigin origin(
      V8String(isolate, file_name),
      v8::Integer::New(isolate, start_position.line_.ZeroBasedInt()),
      v8::Integer::New(isolate, start_position.column_.ZeroBasedInt()),
      v8::Boolean::New(isolate, access_control_status == kSharableCrossOrigin),
      v8::Local<v8::Integer>(),    // script id
      v8::String::Empty(isolate),  // source_map_url
      v8::Boolean::New(isolate, access_control_status == kOpaqueResource),
      v8::False(isolate),  // is_wasm
      v8::True(isolate));  // is_module

  v8::ScriptCompiler::Source script_source(V8String(isolate, source), origin);
  return v8::ScriptCompiler::CompileModule(isolate, &script_source);
}

void V8ScriptRunner::ReportExceptionForModule(v8::Isolate* isolate,
                                              v8::Local<v8::Value> exception,
                                              const String& file_name) {
  // |origin| is for compiling a fragment that throws |exception|.
  // Therefore |is_module| is false and |access_control_status| is
  // kSharableCrossOrigin.
  AccessControlStatus access_control_status = kSharableCrossOrigin;
  v8::ScriptOrigin origin(
      V8String(isolate, file_name),
      v8::Integer::New(isolate, 0),  // line_offset
      v8::Integer::New(isolate, 0),  // col_offset
      v8::Boolean::New(isolate, access_control_status == kSharableCrossOrigin),
      v8::Local<v8::Integer>(),    // script id
      v8::String::Empty(isolate),  // source_map_url
      v8::Boolean::New(isolate, access_control_status == kOpaqueResource),
      v8::False(isolate),   // is_wasm
      v8::False(isolate));  // is_module

  ThrowException(isolate, exception, origin);
}

v8::MaybeLocal<v8::Value> V8ScriptRunner::RunCompiledScript(
    v8::Isolate* isolate,
    v8::Local<v8::Script> script,
    ExecutionContext* context) {
  DCHECK(!script.IsEmpty());
  ScopedFrameBlamer frame_blamer(
      context->IsDocument() ? ToDocument(context)->GetFrame() : nullptr);
  TRACE_EVENT1("v8", "v8.run", "fileName",
               TRACE_STR_COPY(*v8::String::Utf8Value(
                   script->GetUnboundScript()->GetScriptName())));

  if (v8::MicrotasksScope::GetCurrentDepth(isolate) >= kMaxRecursionDepth)
    return ThrowStackOverflowExceptionIfNeeded(isolate);

  CHECK(!context->IsIteratingOverObservers());

  // Run the script and keep track of the current recursion depth.
  v8::MaybeLocal<v8::Value> result;
  {
    if (ScriptForbiddenScope::IsScriptForbidden()) {
      ThrowScriptForbiddenException(isolate);
      return v8::MaybeLocal<v8::Value>();
    }
    v8::MicrotasksScope microtasks_scope(isolate,
                                         v8::MicrotasksScope::kRunMicrotasks);
    probe::ExecuteScript probe(context);
    result = script->Run(isolate->GetCurrentContext());
  }

  CHECK(!isolate->IsDead());
  return result;
}

v8::MaybeLocal<v8::Value> V8ScriptRunner::CompileAndRunInternalScript(
    v8::Local<v8::String> source,
    v8::Isolate* isolate,
    const String& file_name,
    const TextPosition& script_start_position) {
  v8::Local<v8::Script> script;
  if (!V8ScriptRunner::CompileScript(
           source, file_name, String(), script_start_position, isolate, nullptr,
           nullptr, nullptr, kSharableCrossOrigin, kV8CacheOptionsDefault)
           .ToLocal(&script))
    return v8::MaybeLocal<v8::Value>();

  TRACE_EVENT0("v8", "v8.run");
  v8::MicrotasksScope microtasks_scope(
      isolate, v8::MicrotasksScope::kDoNotRunMicrotasks);
  v8::MaybeLocal<v8::Value> result = script->Run(isolate->GetCurrentContext());
  CHECK(!isolate->IsDead());
  return result;
}

v8::MaybeLocal<v8::Value> V8ScriptRunner::RunCompiledInternalScript(
    v8::Isolate* isolate,
    v8::Local<v8::Script> script) {
  TRACE_EVENT0("v8", "v8.run");
  v8::MicrotasksScope microtasks_scope(
      isolate, v8::MicrotasksScope::kDoNotRunMicrotasks);
  v8::MaybeLocal<v8::Value> result = script->Run(isolate->GetCurrentContext());
  CHECK(!isolate->IsDead());
  return result;
}

v8::MaybeLocal<v8::Value> V8ScriptRunner::CallAsConstructor(
    v8::Isolate* isolate,
    v8::Local<v8::Object> constructor,
    ExecutionContext* context,
    int argc,
    v8::Local<v8::Value> argv[]) {
  TRACE_EVENT0("v8", "v8.callAsConstructor");

  int depth = v8::MicrotasksScope::GetCurrentDepth(isolate);
  if (depth >= kMaxRecursionDepth)
    return v8::MaybeLocal<v8::Value>(
        ThrowStackOverflowExceptionIfNeeded(isolate));

  CHECK(!context->IsIteratingOverObservers());

  if (ScriptForbiddenScope::IsScriptForbidden()) {
    ThrowScriptForbiddenException(isolate);
    return v8::MaybeLocal<v8::Value>();
  }

  // TODO(dominicc): When inspector supports tracing object
  // invocation, change this to use v8::Object instead of
  // v8::Function. All callers use functions because
  // CustomElementRegistry#define's IDL signature is Function.
  CHECK(constructor->IsFunction());
  v8::Local<v8::Function> function = constructor.As<v8::Function>();

  v8::MicrotasksScope microtasks_scope(isolate,
                                       v8::MicrotasksScope::kRunMicrotasks);
  probe::CallFunction probe(context, function, depth);
  v8::MaybeLocal<v8::Value> result =
      constructor->CallAsConstructor(isolate->GetCurrentContext(), argc, argv);
  CHECK(!isolate->IsDead());
  return result;
}

v8::MaybeLocal<v8::Value> V8ScriptRunner::CallFunction(
    v8::Local<v8::Function> function,
    ExecutionContext* context,
    v8::Local<v8::Value> receiver,
    int argc,
    v8::Local<v8::Value> args[],
    v8::Isolate* isolate) {
  LocalFrame* frame =
      context->IsDocument() ? ToDocument(context)->GetFrame() : nullptr;
  ScopedFrameBlamer frame_blamer(frame);
  TRACE_EVENT0("v8", "v8.callFunction");

  int depth = v8::MicrotasksScope::GetCurrentDepth(isolate);
  if (depth >= kMaxRecursionDepth)
    return v8::MaybeLocal<v8::Value>(
        ThrowStackOverflowExceptionIfNeeded(isolate));

  CHECK(!context->IsIteratingOverObservers());

  if (ScriptForbiddenScope::IsScriptForbidden()) {
    ThrowScriptForbiddenException(isolate);
    return v8::MaybeLocal<v8::Value>();
  }

  DCHECK(!frame || BindingSecurity::ShouldAllowAccessToFrame(
                       ToLocalDOMWindow(function->CreationContext()), frame,
                       BindingSecurity::ErrorReportOption::kDoNotReport));
  CHECK(!ThreadState::Current()->IsWrapperTracingForbidden());
  v8::MicrotasksScope microtasks_scope(isolate,
                                       v8::MicrotasksScope::kRunMicrotasks);
  probe::CallFunction probe(context, function, depth);
  v8::MaybeLocal<v8::Value> result =
      function->Call(isolate->GetCurrentContext(), receiver, argc, args);
  CHECK(!isolate->IsDead());

  return result;
}

v8::MaybeLocal<v8::Value> V8ScriptRunner::CallInternalFunction(
    v8::Local<v8::Function> function,
    v8::Local<v8::Value> receiver,
    int argc,
    v8::Local<v8::Value> args[],
    v8::Isolate* isolate) {
  TRACE_EVENT0("v8", "v8.callFunction");
  CHECK(!ThreadState::Current()->IsWrapperTracingForbidden());
  v8::MicrotasksScope microtasks_scope(
      isolate, v8::MicrotasksScope::kDoNotRunMicrotasks);
  v8::MaybeLocal<v8::Value> result =
      function->Call(isolate->GetCurrentContext(), receiver, argc, args);
  CHECK(!isolate->IsDead());
  return result;
}

v8::MaybeLocal<v8::Value> V8ScriptRunner::EvaluateModule(
    v8::Local<v8::Module> module,
    v8::Local<v8::Context> context,
    v8::Isolate* isolate) {
  TRACE_EVENT0("v8", "v8.evaluateModule");
  v8::MicrotasksScope microtasks_scope(isolate,
                                       v8::MicrotasksScope::kRunMicrotasks);
  return module->Evaluate(context);
}

uint32_t V8ScriptRunner::TagForParserCache(
    CachedMetadataHandler* cache_handler) {
  return CacheTag(kCacheTagParser, cache_handler);
}

uint32_t V8ScriptRunner::TagForCodeCache(CachedMetadataHandler* cache_handler) {
  return CacheTag(kCacheTagCode, cache_handler);
}

// Store a timestamp to the cache as hint.
void V8ScriptRunner::SetCacheTimeStamp(CachedMetadataHandler* cache_handler) {
  double now = WTF::CurrentTime();
  uint32_t tag = CacheTag(kCacheTagTimeStamp, cache_handler);
  cache_handler->ClearCachedMetadata(CachedMetadataHandler::kCacheLocally);
  cache_handler->SetCachedMetadata(tag, reinterpret_cast<char*>(&now),
                                   sizeof(now),
                                   CachedMetadataHandler::kSendToPlatform);
}

void V8ScriptRunner::ThrowException(v8::Isolate* isolate,
                                    v8::Local<v8::Value> exception,
                                    const v8::ScriptOrigin& origin) {
  // In order for the current TryCatch to catch this exception and
  // call MessageCallback when SetVerbose(true), create a v8::Function
  // that calls isolate->throwException().
  // Unlike throwStackOverflowExceptionIfNeeded(), create a temporary Script
  // with the specified ScriptOrigin. When the exception was created but not
  // thrown yet, the ScriptOrigin of the thrower is set to the exception.
  // v8::Function::New() has empty ScriptOrigin, and thus the exception will
  // be "muted" (sanitized in our terminology) if CORS does not allow.
  // https://html.spec.whatwg.org/multipage/webappapis.html#report-the-error
  // Avoid compile and run scripts when API is available: crbug.com/639739
  v8::Local<v8::Script> script =
      CompileWithoutOptions(
          V8CompileHistogram::Cacheability::kNoncacheable, isolate,
          V8AtomicString(isolate, "((e) => { throw e; })"), origin)
          .ToLocalChecked();
  v8::Local<v8::Function> thrower = RunCompiledInternalScript(isolate, script)
                                        .ToLocalChecked()
                                        .As<v8::Function>();
  v8::Local<v8::Value> args[] = {exception};
  CallInternalFunction(thrower, thrower, WTF_ARRAY_LENGTH(args), args, isolate);
}

v8::MaybeLocal<v8::Value> V8ScriptRunner::CallExtraHelper(
    ScriptState* script_state,
    const char* name,
    size_t num_args,
    v8::Local<v8::Value>* args) {
  v8::Isolate* isolate = script_state->GetIsolate();
  v8::Local<v8::Value> function_value;
  v8::Local<v8::Context> context = script_state->GetContext();
  if (!context->GetExtrasBindingObject()
           ->Get(context, V8AtomicString(isolate, name))
           .ToLocal(&function_value))
    return v8::MaybeLocal<v8::Value>();
  v8::Local<v8::Function> function = function_value.As<v8::Function>();
  return V8ScriptRunner::CallInternalFunction(function, v8::Undefined(isolate),
                                              num_args, args, isolate);
}

}  // namespace blink
