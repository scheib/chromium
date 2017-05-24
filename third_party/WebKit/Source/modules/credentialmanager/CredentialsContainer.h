// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CredentialsContainer_h
#define CredentialsContainer_h

#include "modules/ModulesExport.h"
#include "platform/bindings/ScriptWrappable.h"
#include "platform/heap/Handle.h"

namespace blink {

class Credential;
class CredentialCreationOptions;
class CredentialRequestOptions;
class ExceptionState;
class ScriptPromise;
class ScriptState;

class MODULES_EXPORT CredentialsContainer final
    : public GarbageCollected<CredentialsContainer>,
      public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static CredentialsContainer* Create();

  // CredentialsContainer.idl
  ScriptPromise get(ScriptState*, const CredentialRequestOptions&);
  ScriptPromise store(ScriptState*, Credential* = 0);
  ScriptPromise create(ScriptState*,
                       const CredentialCreationOptions&,
                       ExceptionState&);
  ScriptPromise requireUserMediation(ScriptState*);

  DEFINE_INLINE_VIRTUAL_TRACE() {}

 private:
  CredentialsContainer();
};

}  // namespace blink

#endif  // CredentialsContainer_h
