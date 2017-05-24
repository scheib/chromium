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

#ifndef IDBRequest_h
#define IDBRequest_h

#include <memory>

#include "bindings/core/v8/ScriptValue.h"
#include "core/dom/DOMStringList.h"
#include "core/dom/SuspendableObject.h"
#include "core/events/EventListener.h"
#include "core/events/EventTarget.h"
#include "modules/EventModules.h"
#include "modules/ModulesExport.h"
#include "modules/indexeddb/IDBAny.h"
#include "modules/indexeddb/IDBTransaction.h"
#include "modules/indexeddb/IndexedDB.h"
#include "platform/bindings/ActiveScriptWrappable.h"
#include "platform/bindings/ScriptState.h"
#include "platform/blob/BlobData.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/HashMap.h"
#include "public/platform/WebBlobInfo.h"
#include "public/platform/modules/indexeddb/WebIDBCursor.h"
#include "public/platform/modules/indexeddb/WebIDBTypes.h"

namespace blink {

class DOMException;
class ExceptionState;
class IDBCursor;
struct IDBDatabaseMetadata;
class IDBValue;

class MODULES_EXPORT IDBRequest : public EventTargetWithInlineData,
                                  public ActiveScriptWrappable<IDBRequest>,
                                  public SuspendableObject {
  DEFINE_WRAPPERTYPEINFO();
  USING_GARBAGE_COLLECTED_MIXIN(IDBRequest);

 public:
  static IDBRequest* Create(ScriptState*, IDBAny* source, IDBTransaction*);
  ~IDBRequest() override;
  DECLARE_VIRTUAL_TRACE();

  v8::Isolate* GetIsolate() const { return isolate_; }
  ScriptValue result(ScriptState*, ExceptionState&);
  DOMException* error(ExceptionState&) const;
  ScriptValue source(ScriptState*) const;
  IDBTransaction* transaction() const { return transaction_.Get(); }

  bool isResultDirty() const { return result_dirty_; }
  IDBAny* ResultAsAny() const { return result_; }

  // Requests made during index population are implementation details and so
  // events should not be visible to script.
  void PreventPropagation() { prevent_propagation_ = true; }

  // Defined in the IDL
  enum ReadyState { PENDING = 1, DONE = 2, kEarlyDeath = 3 };

  const String& readyState() const;

  // Returns a new WebIDBCallbacks for this request.
  //
  // Each call must be paired with a WebCallbacksDestroyed() call. Most requests
  // have a single WebIDBCallbacks instance created for them.
  //
  // Requests used to open and iterate cursors are special, because they are
  // reused between openCursor() and continue() / advance() calls. These
  // requests have a new WebIDBCallbacks instance created for each of the
  // above-mentioned calls that they are involved in.
  std::unique_ptr<WebIDBCallbacks> CreateWebCallbacks();
  void WebCallbacksDestroyed() {
    DCHECK(web_callbacks_);
    web_callbacks_ = nullptr;
  }
#if DCHECK_IS_ON()
  WebIDBCallbacks* WebCallbacks() const { return web_callbacks_; }
#endif  // DCHECK_IS_ON()

  DEFINE_ATTRIBUTE_EVENT_LISTENER(success);
  DEFINE_ATTRIBUTE_EVENT_LISTENER(error);

  void SetCursorDetails(IndexedDB::CursorType, WebIDBCursorDirection);
  void SetPendingCursor(IDBCursor*);
  void Abort();

  void EnqueueResponse(DOMException*);
  void EnqueueResponse(std::unique_ptr<WebIDBCursor>,
                       IDBKey*,
                       IDBKey* primary_key,
                       PassRefPtr<IDBValue>);
  void EnqueueResponse(IDBKey*);
  void EnqueueResponse(PassRefPtr<IDBValue>);
  void EnqueueResponse(const Vector<RefPtr<IDBValue>>&);
  void EnqueueResponse();
  void EnqueueResponse(IDBKey*, IDBKey* primary_key, PassRefPtr<IDBValue>);

  // Only used in webkitGetDatabaseNames(), which is deprecated and hopefully
  // going away soon.
  void EnqueueResponse(const Vector<String>&);

  // Overridden by IDBOpenDBRequest.
  virtual void EnqueueResponse(int64_t);

  // Only IDBOpenDBRequest instances should receive these:
  virtual void EnqueueBlocked(int64_t old_version) { NOTREACHED(); }
  virtual void EnqueueUpgradeNeeded(int64_t old_version,
                                    std::unique_ptr<WebIDBDatabase>,
                                    const IDBDatabaseMetadata&,
                                    WebIDBDataLoss,
                                    String data_loss_message) {
    NOTREACHED();
  }
  virtual void EnqueueResponse(std::unique_ptr<WebIDBDatabase>,
                               const IDBDatabaseMetadata&) {
    NOTREACHED();
  }

  // ScriptWrappable
  bool HasPendingActivity() const final;

  // SuspendableObject
  void ContextDestroyed(ExecutionContext*) override;

  // EventTarget
  const AtomicString& InterfaceName() const override;
  ExecutionContext* GetExecutionContext() const final;
  void UncaughtExceptionInEventHandler() final;

  // Called by a version change transaction that has finished to set this
  // request back from DONE (following "upgradeneeded") back to PENDING (for
  // the upcoming "success" or "error").
  void TransactionDidFinishAndDispatch();

  IDBCursor* GetResultCursor() const;

  void StorePutOperationBlobs(
      HashMap<String, RefPtr<BlobDataHandle>> blob_handles) {
    transit_blob_handles_ = std::move(blob_handles);
  }

 protected:
  IDBRequest(ScriptState*, IDBAny* source, IDBTransaction*);
  void EnqueueEvent(Event*);
  void DequeueEvent(Event*);
  virtual bool ShouldEnqueueEvent() const;
  void EnqueueResultInternal(IDBAny*);
  void SetResult(IDBAny*);

  // EventTarget
  DispatchEventResult DispatchEventInternal(Event*) override;

  Member<IDBTransaction> transaction_;
  ReadyState ready_state_ = PENDING;
  bool request_aborted_ = false;  // May be aborted by transaction then receive
                                  // async onsuccess; ignore vs. assert.
  // Maintain the isolate so that all externally allocated memory can be
  // registered against it.
  v8::Isolate* isolate_;

 private:
  void SetResultCursor(IDBCursor*,
                       IDBKey*,
                       IDBKey* primary_key,
                       PassRefPtr<IDBValue>);
  void AckReceivedBlobs(const IDBValue*);
  void AckReceivedBlobs(const Vector<RefPtr<IDBValue>>&);

  void ClearPutOperationBlobs() { transit_blob_handles_.clear(); }

  Member<IDBAny> source_;
  Member<IDBAny> result_;
  Member<DOMException> error_;

  bool has_pending_activity_ = true;
  HeapVector<Member<Event>> enqueued_events_;

  // Only used if the result type will be a cursor.
  IndexedDB::CursorType cursor_type_ = IndexedDB::kCursorKeyAndValue;
  WebIDBCursorDirection cursor_direction_ = kWebIDBCursorDirectionNext;
  // When a cursor is continued/advanced, |result_| is cleared and
  // |pendingCursor_| holds it.
  Member<IDBCursor> pending_cursor_;
  // New state is not applied to the cursor object until the event is
  // dispatched.
  Member<IDBKey> cursor_key_;
  Member<IDBKey> cursor_primary_key_;
  RefPtr<IDBValue> cursor_value_;

  HashMap<String, RefPtr<BlobDataHandle>> transit_blob_handles_;

  bool did_fire_upgrade_needed_event_ = false;
  bool prevent_propagation_ = false;
  bool result_dirty_ = true;

  // Transactions should be aborted after event dispatch if an exception was
  // not caught. This is cleared before dispatch, set by a call to
  // UncaughtExceptionInEventHandler() during dispatch, and checked afterwards
  // to abort if necessary.
  bool did_throw_in_event_handler_ = false;

  // Pointer back to the WebIDBCallbacks that holds a persistent reference to
  // this object.
  WebIDBCallbacks* web_callbacks_ = nullptr;
};

}  // namespace blink

#endif  // IDBRequest_h
