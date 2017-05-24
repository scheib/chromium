// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef Body_h
#define Body_h

#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptValue.h"
#include "core/dom/ContextLifecycleObserver.h"
#include "modules/ModulesExport.h"
#include "platform/bindings/ActiveScriptWrappable.h"
#include "platform/bindings/ScriptWrappable.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/text/WTFString.h"

namespace blink {

class BodyStreamBuffer;
class ExecutionContext;
class ScriptState;

// This class represents Body mix-in defined in the fetch spec
// https://fetch.spec.whatwg.org/#body-mixin.
//
// Note: This class has body stream and its predicate whereas in the current
// spec only Response has it and Request has a byte stream defined in the
// Encoding spec. The spec should be fixed shortly to be aligned with this
// implementation.
class MODULES_EXPORT Body : public GarbageCollected<Body>,
                            public ScriptWrappable,
                            public ActiveScriptWrappable<Body>,
                            public ContextClient {
  WTF_MAKE_NONCOPYABLE(Body);
  DEFINE_WRAPPERTYPEINFO();
  USING_GARBAGE_COLLECTED_MIXIN(Body);

 public:
  explicit Body(ExecutionContext*);

  ScriptPromise arrayBuffer(ScriptState*);
  ScriptPromise blob(ScriptState*);
  ScriptPromise formData(ScriptState*);
  ScriptPromise json(ScriptState*);
  ScriptPromise text(ScriptState*);
  ScriptValue body(ScriptState*);
  virtual BodyStreamBuffer* BodyBuffer() = 0;
  virtual const BodyStreamBuffer* BodyBuffer() const = 0;

  virtual bool bodyUsed();
  bool IsBodyLocked();

  // ScriptWrappable override.
  bool HasPendingActivity() const override;

  DEFINE_INLINE_VIRTUAL_TRACE() { ContextClient::Trace(visitor); }

 private:
  // TODO(e_hakkinen): Fix |MimeType()| to always contain parameters and
  // remove |ContentType()|.
  virtual String ContentType() const = 0;
  virtual String MimeType() const = 0;

  // Body consumption algorithms will reject with a TypeError in a number of
  // error conditions. This method wraps those up into one call which returns
  // an empty ScriptPromise if the consumption may proceed, and a
  // ScriptPromise rejected with a TypeError if it ought to be blocked.
  ScriptPromise RejectInvalidConsumption(ScriptState*);
};

}  // namespace blink

#endif  // Body_h
