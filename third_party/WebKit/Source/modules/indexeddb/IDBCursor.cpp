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

#include "modules/indexeddb/IDBCursor.h"

#include <limits>
#include <memory>
#include "bindings/core/v8/ExceptionState.h"
#include "bindings/modules/v8/ToV8ForModules.h"
#include "bindings/modules/v8/V8BindingForModules.h"
#include "bindings/modules/v8/V8IDBRequest.h"
#include "core/dom/ExceptionCode.h"
#include "modules/IndexedDBNames.h"
#include "modules/indexeddb/IDBAny.h"
#include "modules/indexeddb/IDBDatabase.h"
#include "modules/indexeddb/IDBObjectStore.h"
#include "modules/indexeddb/IDBTracing.h"
#include "modules/indexeddb/IDBTransaction.h"
#include "platform/bindings/ScriptState.h"
#include "platform/bindings/V8PrivateProperty.h"
#include "public/platform/modules/indexeddb/WebIDBDatabase.h"
#include "public/platform/modules/indexeddb/WebIDBKeyRange.h"

using blink::WebIDBCursor;
using blink::WebIDBDatabase;

namespace blink {

IDBCursor* IDBCursor::Create(std::unique_ptr<WebIDBCursor> backend,
                             WebIDBCursorDirection direction,
                             IDBRequest* request,
                             IDBAny* source,
                             IDBTransaction* transaction) {
  return new IDBCursor(std::move(backend), direction, request, source,
                       transaction);
}

IDBCursor::IDBCursor(std::unique_ptr<WebIDBCursor> backend,
                     WebIDBCursorDirection direction,
                     IDBRequest* request,
                     IDBAny* source,
                     IDBTransaction* transaction)
    : backend_(std::move(backend)),
      request_(request),
      direction_(direction),
      source_(source),
      transaction_(transaction) {
  DCHECK(backend_);
  DCHECK(request_);
  DCHECK(source_->GetType() == IDBAny::kIDBObjectStoreType ||
         source_->GetType() == IDBAny::kIDBIndexType);
  DCHECK(transaction_);
}

IDBCursor::~IDBCursor() {}

DEFINE_TRACE(IDBCursor) {
  visitor->Trace(request_);
  visitor->Trace(source_);
  visitor->Trace(transaction_);
  visitor->Trace(key_);
  visitor->Trace(primary_key_);
}

// Keep the request's wrapper alive as long as the cursor's wrapper is alive,
// so that the same script object is seen each time the cursor is used.
v8::Local<v8::Object> IDBCursor::AssociateWithWrapper(
    v8::Isolate* isolate,
    const WrapperTypeInfo* wrapper_type,
    v8::Local<v8::Object> wrapper) {
  wrapper =
      ScriptWrappable::AssociateWithWrapper(isolate, wrapper_type, wrapper);
  if (!wrapper.IsEmpty()) {
    V8PrivateProperty::GetIDBCursorRequest(isolate).Set(
        wrapper, ToV8(request_.Get(), wrapper, isolate));
  }
  return wrapper;
}

IDBRequest* IDBCursor::update(ScriptState* script_state,
                              const ScriptValue& value,
                              ExceptionState& exception_state) {
  IDB_TRACE("IDBCursor::update");

  if (!transaction_->IsActive()) {
    exception_state.ThrowDOMException(kTransactionInactiveError,
                                      transaction_->InactiveErrorMessage());
    return nullptr;
  }
  if (transaction_->IsReadOnly()) {
    exception_state.ThrowDOMException(
        kReadOnlyError,
        "The record may not be updated inside a read-only transaction.");
    return nullptr;
  }
  if (IsDeleted()) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kSourceDeletedErrorMessage);
    return nullptr;
  }
  if (!got_value_) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kNoValueErrorMessage);
    return nullptr;
  }
  if (IsKeyCursor()) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kIsKeyCursorErrorMessage);
    return nullptr;
  }

  IDBObjectStore* object_store = EffectiveObjectStore();
  return object_store->put(script_state, kWebIDBPutModeCursorUpdate,
                           IDBAny::Create(this), value, primary_key_,
                           exception_state);
}

void IDBCursor::advance(unsigned count, ExceptionState& exception_state) {
  IDB_TRACE("IDBCursor::advance");
  if (!count) {
    exception_state.ThrowTypeError(
        "A count argument with value 0 (zero) was supplied, must be greater "
        "than 0.");
    return;
  }
  if (!transaction_->IsActive()) {
    exception_state.ThrowDOMException(kTransactionInactiveError,
                                      transaction_->InactiveErrorMessage());
    return;
  }
  if (IsDeleted()) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kSourceDeletedErrorMessage);
    return;
  }
  if (!got_value_) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kNoValueErrorMessage);
    return;
  }

  request_->SetPendingCursor(this);
  got_value_ = false;
  backend_->Advance(count, request_->CreateWebCallbacks().release());
}

void IDBCursor::continueFunction(ScriptState* script_state,
                                 const ScriptValue& key_value,
                                 ExceptionState& exception_state) {
  IDB_TRACE("IDBCursor::continue");

  if (!transaction_->IsActive()) {
    exception_state.ThrowDOMException(kTransactionInactiveError,
                                      transaction_->InactiveErrorMessage());
    return;
  }
  if (!got_value_) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kNoValueErrorMessage);
    return;
  }
  if (IsDeleted()) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kSourceDeletedErrorMessage);
    return;
  }

  IDBKey* key = key_value.IsUndefined() || key_value.IsNull()
                    ? nullptr
                    : ScriptValue::To<IDBKey*>(script_state->GetIsolate(),
                                               key_value, exception_state);
  if (exception_state.HadException())
    return;
  if (key && !key->IsValid()) {
    exception_state.ThrowDOMException(kDataError,
                                      IDBDatabase::kNotValidKeyErrorMessage);
    return;
  }
  Continue(key, nullptr, exception_state);
}

void IDBCursor::continuePrimaryKey(ScriptState* script_state,
                                   const ScriptValue& key_value,
                                   const ScriptValue& primary_key_value,
                                   ExceptionState& exception_state) {
  IDB_TRACE("IDBCursor::continuePrimaryKey");

  if (!transaction_->IsActive()) {
    exception_state.ThrowDOMException(kTransactionInactiveError,
                                      transaction_->InactiveErrorMessage());
    return;
  }

  if (IsDeleted()) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kSourceDeletedErrorMessage);
    return;
  }

  if (source_->GetType() != IDBAny::kIDBIndexType) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The cursor's source is not an index.");
    return;
  }

  if (direction_ != kWebIDBCursorDirectionNext &&
      direction_ != kWebIDBCursorDirectionPrev) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The cursor's direction is not 'next' or 'prev'.");
    return;
  }

  if (!got_value_) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kNoValueErrorMessage);
    return;
  }

  IDBKey* key = ScriptValue::To<IDBKey*>(script_state->GetIsolate(), key_value,
                                         exception_state);
  if (exception_state.HadException())
    return;
  if (!key->IsValid()) {
    exception_state.ThrowDOMException(kDataError,
                                      IDBDatabase::kNotValidKeyErrorMessage);
    return;
  }

  IDBKey* primary_key = ScriptValue::To<IDBKey*>(
      script_state->GetIsolate(), primary_key_value, exception_state);
  if (exception_state.HadException())
    return;
  if (!primary_key->IsValid()) {
    exception_state.ThrowDOMException(kDataError,
                                      IDBDatabase::kNotValidKeyErrorMessage);
    return;
  }

  Continue(key, primary_key, exception_state);
}

void IDBCursor::Continue(IDBKey* key,
                         IDBKey* primary_key,
                         ExceptionState& exception_state) {
  DCHECK(transaction_->IsActive());
  DCHECK(got_value_);
  DCHECK(!IsDeleted());
  DCHECK(!primary_key || (key && primary_key));

  if (key) {
    DCHECK(key_);
    if (direction_ == kWebIDBCursorDirectionNext ||
        direction_ == kWebIDBCursorDirectionNextNoDuplicate) {
      const bool ok =
          key_->IsLessThan(key) || (primary_key && key_->IsEqual(key) &&
                                    primary_key_->IsLessThan(primary_key));
      if (!ok) {
        exception_state.ThrowDOMException(
            kDataError,
            "The parameter is less than or equal to this cursor's position.");
        return;
      }

    } else {
      const bool ok = key->IsLessThan(key_.Get()) ||
                      (primary_key && key->IsEqual(key_.Get()) &&
                       primary_key->IsLessThan(primary_key_.Get()));
      if (!ok) {
        exception_state.ThrowDOMException(kDataError,
                                          "The parameter is greater than or "
                                          "equal to this cursor's position.");
        return;
      }
    }
  }

  // FIXME: We're not using the context from when continue was called, which
  // means the callback will be on the original context openCursor was called
  // on. Is this right?
  request_->SetPendingCursor(this);
  got_value_ = false;
  backend_->Continue(key, primary_key,
                     request_->CreateWebCallbacks().release());
}

IDBRequest* IDBCursor::deleteFunction(ScriptState* script_state,
                                      ExceptionState& exception_state) {
  IDB_TRACE("IDBCursor::delete");
  if (!transaction_->IsActive()) {
    exception_state.ThrowDOMException(kTransactionInactiveError,
                                      transaction_->InactiveErrorMessage());
    return nullptr;
  }
  if (transaction_->IsReadOnly()) {
    exception_state.ThrowDOMException(
        kReadOnlyError,
        "The record may not be deleted inside a read-only transaction.");
    return nullptr;
  }
  if (IsDeleted()) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kSourceDeletedErrorMessage);
    return nullptr;
  }
  if (!got_value_) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kNoValueErrorMessage);
    return nullptr;
  }
  if (IsKeyCursor()) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kIsKeyCursorErrorMessage);
    return nullptr;
  }
  if (!transaction_->BackendDB()) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      IDBDatabase::kDatabaseClosedErrorMessage);
    return nullptr;
  }

  IDBKeyRange* key_range = IDBKeyRange::only(primary_key_, exception_state);
  DCHECK(!exception_state.HadException());

  IDBRequest* request = IDBRequest::Create(script_state, IDBAny::Create(this),
                                           transaction_.Get());
  transaction_->BackendDB()->DeleteRange(
      transaction_->Id(), EffectiveObjectStore()->Id(), key_range,
      request->CreateWebCallbacks().release());
  return request;
}

void IDBCursor::PostSuccessHandlerCallback() {
  if (backend_)
    backend_->PostSuccessHandlerCallback();
}

void IDBCursor::Close() {
  value_.Clear();
  request_.Clear();
  backend_.reset();
}

ScriptValue IDBCursor::key(ScriptState* script_state) {
  key_dirty_ = false;
  return ScriptValue::From(script_state, key_);
}

ScriptValue IDBCursor::primaryKey(ScriptState* script_state) {
  primary_key_dirty_ = false;
  return ScriptValue::From(script_state, primary_key_);
}

ScriptValue IDBCursor::value(ScriptState* script_state) {
  DCHECK(IsCursorWithValue());

  IDBObjectStore* object_store = EffectiveObjectStore();
  IDBAny* value;
  if (!value_) {
    value = IDBAny::CreateUndefined();
  } else if (object_store->autoIncrement() &&
             !object_store->IdbKeyPath().IsNull()) {
    RefPtr<IDBValue> idb_value = IDBValue::Create(value_.Get(), primary_key_,
                                                  object_store->IdbKeyPath());
#if DCHECK_IS_ON()
    AssertPrimaryKeyValidOrInjectable(script_state, idb_value.Get());
#endif  // DCHECK_IS_ON()
    value = IDBAny::Create(idb_value.Release());
  } else {
    value = IDBAny::Create(value_);
  }

  value_dirty_ = false;
  ScriptValue script_value = ScriptValue::From(script_state, value);
  return script_value;
}

ScriptValue IDBCursor::source(ScriptState* script_state) const {
  return ScriptValue::From(script_state, source_);
}

void IDBCursor::SetValueReady(IDBKey* key,
                              IDBKey* primary_key,
                              PassRefPtr<IDBValue> value) {
  key_ = key;
  key_dirty_ = true;

  primary_key_ = primary_key;
  primary_key_dirty_ = true;

  if (IsCursorWithValue()) {
    value_ = std::move(value);
    value_dirty_ = true;
  }

  got_value_ = true;
}

IDBObjectStore* IDBCursor::EffectiveObjectStore() const {
  if (source_->GetType() == IDBAny::kIDBObjectStoreType)
    return source_->IdbObjectStore();
  return source_->IdbIndex()->objectStore();
}

bool IDBCursor::IsDeleted() const {
  if (source_->GetType() == IDBAny::kIDBObjectStoreType)
    return source_->IdbObjectStore()->IsDeleted();
  return source_->IdbIndex()->IsDeleted();
}

WebIDBCursorDirection IDBCursor::StringToDirection(
    const String& direction_string) {
  if (direction_string == IndexedDBNames::next)
    return kWebIDBCursorDirectionNext;
  if (direction_string == IndexedDBNames::nextunique)
    return kWebIDBCursorDirectionNextNoDuplicate;
  if (direction_string == IndexedDBNames::prev)
    return kWebIDBCursorDirectionPrev;
  if (direction_string == IndexedDBNames::prevunique)
    return kWebIDBCursorDirectionPrevNoDuplicate;

  NOTREACHED();
  return kWebIDBCursorDirectionNext;
}

const String& IDBCursor::direction() const {
  switch (direction_) {
    case kWebIDBCursorDirectionNext:
      return IndexedDBNames::next;

    case kWebIDBCursorDirectionNextNoDuplicate:
      return IndexedDBNames::nextunique;

    case kWebIDBCursorDirectionPrev:
      return IndexedDBNames::prev;

    case kWebIDBCursorDirectionPrevNoDuplicate:
      return IndexedDBNames::prevunique;

    default:
      NOTREACHED();
      return IndexedDBNames::next;
  }
}

}  // namespace blink
