/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "modules/indexeddb/IDBFactory.h"

#include <memory>
#include "bindings/core/v8/ExceptionState.h"
#include "bindings/modules/v8/V8BindingForModules.h"
#include "core/dom/DOMException.h"
#include "core/dom/Document.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/ExecutionContext.h"
#include "modules/indexeddb/IDBDatabase.h"
#include "modules/indexeddb/IDBDatabaseCallbacks.h"
#include "modules/indexeddb/IDBKey.h"
#include "modules/indexeddb/IDBTracing.h"
#include "modules/indexeddb/IndexedDBClient.h"
#include "platform/Histogram.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "public/platform/Platform.h"
#include "public/platform/WebSecurityOrigin.h"
#include "public/platform/modules/indexeddb/WebIDBDatabaseCallbacks.h"
#include "public/platform/modules/indexeddb/WebIDBFactory.h"

namespace blink {

static const char kPermissionDeniedErrorMessage[] =
    "The user denied permission to access the database.";

IDBFactory::IDBFactory() {}

static bool IsContextValid(ExecutionContext* context) {
  DCHECK(context->IsDocument() || context->IsWorkerGlobalScope());
  if (context->IsDocument()) {
    Document* document = ToDocument(context);
    return document->GetFrame() && document->GetPage();
  }
  return true;
}

IDBRequest* IDBFactory::GetDatabaseNames(ScriptState* script_state,
                                         ExceptionState& exception_state) {
  // TODO(jsbell): Used only by inspector; remove unneeded checks/exceptions?
  if (!IsContextValid(ExecutionContext::From(script_state)))
    return nullptr;
  if (!ExecutionContext::From(script_state)
           ->GetSecurityOrigin()
           ->CanAccessDatabase()) {
    exception_state.ThrowSecurityError(
        "access to the Indexed Database API is denied in this context.");
    return nullptr;
  }

  IDBRequest* request =
      IDBRequest::Create(script_state, IDBAny::CreateNull(), nullptr);

  if (!IndexedDBClient::From(ExecutionContext::From(script_state))
           ->AllowIndexedDB(ExecutionContext::From(script_state),
                            "Database Listing")) {
    request->EnqueueResponse(
        DOMException::Create(kUnknownError, kPermissionDeniedErrorMessage));
    return request;
  }

  Platform::Current()->IdbFactory()->GetDatabaseNames(
      request->CreateWebCallbacks().release(),
      WebSecurityOrigin(
          ExecutionContext::From(script_state)->GetSecurityOrigin()));
  return request;
}

IDBOpenDBRequest* IDBFactory::open(ScriptState* script_state,
                                   const String& name,
                                   unsigned long long version,
                                   ExceptionState& exception_state) {
  IDB_TRACE("IDBFactory::open");
  if (!version) {
    exception_state.ThrowTypeError("The version provided must not be 0.");
    return nullptr;
  }
  return OpenInternal(script_state, name, version, exception_state);
}

IDBOpenDBRequest* IDBFactory::OpenInternal(ScriptState* script_state,
                                           const String& name,
                                           int64_t version,
                                           ExceptionState& exception_state) {
  IDBDatabase::RecordApiCallsHistogram(kIDBOpenCall);
  DCHECK(version >= 1 || version == IDBDatabaseMetadata::kNoVersion);
  if (!IsContextValid(ExecutionContext::From(script_state)))
    return nullptr;
  if (!ExecutionContext::From(script_state)
           ->GetSecurityOrigin()
           ->CanAccessDatabase()) {
    exception_state.ThrowSecurityError(
        "access to the Indexed Database API is denied in this context.");
    return nullptr;
  }

  IDBDatabaseCallbacks* database_callbacks = IDBDatabaseCallbacks::Create();
  int64_t transaction_id = IDBDatabase::NextTransactionId();
  IDBOpenDBRequest* request = IDBOpenDBRequest::Create(
      script_state, database_callbacks, transaction_id, version);

  if (!IndexedDBClient::From(ExecutionContext::From(script_state))
           ->AllowIndexedDB(ExecutionContext::From(script_state), name)) {
    request->EnqueueResponse(
        DOMException::Create(kUnknownError, kPermissionDeniedErrorMessage));
    return request;
  }

  Platform::Current()->IdbFactory()->Open(
      name, version, transaction_id, request->CreateWebCallbacks().release(),
      database_callbacks->CreateWebCallbacks().release(),
      WebSecurityOrigin(
          ExecutionContext::From(script_state)->GetSecurityOrigin()));
  return request;
}

IDBOpenDBRequest* IDBFactory::open(ScriptState* script_state,
                                   const String& name,
                                   ExceptionState& exception_state) {
  IDB_TRACE("IDBFactory::open");
  return OpenInternal(script_state, name, IDBDatabaseMetadata::kNoVersion,
                      exception_state);
}

IDBOpenDBRequest* IDBFactory::deleteDatabase(ScriptState* script_state,
                                             const String& name,
                                             ExceptionState& exception_state) {
  return DeleteDatabaseInternal(script_state, name, exception_state,
                                /*force_close=*/false);
}

IDBOpenDBRequest* IDBFactory::CloseConnectionsAndDeleteDatabase(
    ScriptState* script_state,
    const String& name,
    ExceptionState& exception_state) {
  // TODO(jsbell): Used only by inspector; remove unneeded checks/exceptions?
  return DeleteDatabaseInternal(script_state, name, exception_state,
                                /*force_close=*/true);
}

IDBOpenDBRequest* IDBFactory::DeleteDatabaseInternal(
    ScriptState* script_state,
    const String& name,
    ExceptionState& exception_state,
    bool force_close) {
  IDB_TRACE("IDBFactory::deleteDatabase");
  IDBDatabase::RecordApiCallsHistogram(kIDBDeleteDatabaseCall);
  if (!IsContextValid(ExecutionContext::From(script_state)))
    return nullptr;
  if (!ExecutionContext::From(script_state)
           ->GetSecurityOrigin()
           ->CanAccessDatabase()) {
    exception_state.ThrowSecurityError(
        "access to the Indexed Database API is denied in this context.");
    return nullptr;
  }

  IDBOpenDBRequest* request = IDBOpenDBRequest::Create(
      script_state, nullptr, 0, IDBDatabaseMetadata::kDefaultVersion);

  if (!IndexedDBClient::From(ExecutionContext::From(script_state))
           ->AllowIndexedDB(ExecutionContext::From(script_state), name)) {
    request->EnqueueResponse(
        DOMException::Create(kUnknownError, kPermissionDeniedErrorMessage));
    return request;
  }

  Platform::Current()->IdbFactory()->DeleteDatabase(
      name, request->CreateWebCallbacks().release(),
      WebSecurityOrigin(
          ExecutionContext::From(script_state)->GetSecurityOrigin()),
      force_close);
  return request;
}

short IDBFactory::cmp(ScriptState* script_state,
                      const ScriptValue& first_value,
                      const ScriptValue& second_value,
                      ExceptionState& exception_state) {
  IDBKey* first = ScriptValue::To<IDBKey*>(script_state->GetIsolate(),
                                           first_value, exception_state);
  if (exception_state.HadException())
    return 0;
  DCHECK(first);
  if (!first->IsValid()) {
    exception_state.ThrowDOMException(kDataError,
                                      IDBDatabase::kNotValidKeyErrorMessage);
    return 0;
  }

  IDBKey* second = ScriptValue::To<IDBKey*>(script_state->GetIsolate(),
                                            second_value, exception_state);
  if (exception_state.HadException())
    return 0;
  DCHECK(second);
  if (!second->IsValid()) {
    exception_state.ThrowDOMException(kDataError,
                                      IDBDatabase::kNotValidKeyErrorMessage);
    return 0;
  }

  return static_cast<short>(first->Compare(second));
}

}  // namespace blink
