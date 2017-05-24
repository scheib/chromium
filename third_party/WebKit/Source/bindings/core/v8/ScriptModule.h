// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ScriptModule_h
#define ScriptModule_h

#include "core/CoreExport.h"
#include "platform/bindings/ScriptState.h"
#include "platform/bindings/SharedPersistent.h"
#include "platform/loader/fetch/AccessControlStatus.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/Vector.h"
#include "platform/wtf/text/TextPosition.h"
#include "platform/wtf/text/WTFString.h"
#include "v8/include/v8.h"

namespace blink {

// ScriptModule wraps a handle to a v8::Module for use in core.
//
// Using ScriptModules needs a ScriptState and its scope to operate in. You
// should always provide the same ScriptState and not try to reuse ScriptModules
// across different contexts.
// Currently all ScriptModule users can easily access its context Modulator, so
// we use it to fill ScriptState in.
class CORE_EXPORT ScriptModule final {
  DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();

 public:
  static ScriptModule Compile(
      v8::Isolate*,
      const String& source,
      const String& file_name,
      AccessControlStatus,
      const TextPosition& start_position = TextPosition::MinimumPosition());

  // TODO(kouhei): Remove copy ctor
  ScriptModule();
  ScriptModule(WTF::HashTableDeletedValueType);
  ~ScriptModule();

  // Returns exception, if any.
  ScriptValue Instantiate(ScriptState*);

  void Evaluate(ScriptState*) const;
  static void ReportException(ScriptState*,
                              v8::Local<v8::Value> exception,
                              const String& file_name);

  Vector<String> ModuleRequests(ScriptState*);

  bool IsHashTableDeletedValue() const {
    return module_.IsHashTableDeletedValue();
  }

  bool operator==(const blink::ScriptModule& other) const {
    if (IsHashTableDeletedValue() && other.IsHashTableDeletedValue())
      return true;

    if (IsHashTableDeletedValue() || other.IsHashTableDeletedValue())
      return false;

    blink::SharedPersistent<v8::Module>* left = module_.Get();
    blink::SharedPersistent<v8::Module>* right = other.module_.Get();
    if (left == right)
      return true;
    if (!left || !right)
      return false;
    return *left == *right;
  }

  bool operator!=(const blink::ScriptModule& other) const {
    return !(*this == other);
  }

  bool IsNull() const { return !module_ || module_->IsEmpty(); }

 private:
  ScriptModule(v8::Isolate*, v8::Local<v8::Module>);

  static v8::MaybeLocal<v8::Module> ResolveModuleCallback(
      v8::Local<v8::Context>,
      v8::Local<v8::String> specifier,
      v8::Local<v8::Module> referrer);

  RefPtr<SharedPersistent<v8::Module>> module_;
  unsigned identity_hash_ = 0;

  friend struct ScriptModuleHash;
};

struct ScriptModuleHash {
  STATIC_ONLY(ScriptModuleHash);

 public:
  static unsigned GetHash(const blink::ScriptModule& key) {
    return key.identity_hash_;
  }

  static bool Equal(const blink::ScriptModule& a,
                    const blink::ScriptModule& b) {
    return a == b;
  }

  static constexpr bool safe_to_compare_to_empty_or_deleted = true;
};

}  // namespace blink

namespace WTF {

template <>
struct DefaultHash<blink::ScriptModule> {
  using Hash = blink::ScriptModuleHash;
};

template <>
struct HashTraits<blink::ScriptModule>
    : public SimpleClassHashTraits<blink::ScriptModule> {};

}  // namespace WTF

#endif  // ScriptModule_h
