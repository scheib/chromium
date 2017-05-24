/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "modules/indexeddb/IDBTransaction.h"

#include <memory>

#include "bindings/core/v8/V8BindingForTesting.h"
#include "core/dom/DOMException.h"
#include "core/dom/Document.h"
#include "core/dom/ExceptionCode.h"
#include "modules/indexeddb/IDBDatabase.h"
#include "modules/indexeddb/IDBDatabaseCallbacks.h"
#include "modules/indexeddb/MockWebIDBDatabase.h"
#include "platform/SharedBuffer.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "v8/include/v8.h"

namespace blink {
namespace {

void DeactivateNewTransactions(v8::Isolate* isolate) {
  V8PerIsolateData::From(isolate)->RunEndOfScopeTasks();
}

class FakeIDBDatabaseCallbacks final : public IDBDatabaseCallbacks {
 public:
  static FakeIDBDatabaseCallbacks* Create() {
    return new FakeIDBDatabaseCallbacks();
  }
  void OnVersionChange(int64_t old_version, int64_t new_version) override {}
  void OnForcedClose() override {}
  void OnAbort(int64_t transaction_id, DOMException* error) override {}
  void OnComplete(int64_t transaction_id) override {}

 private:
  FakeIDBDatabaseCallbacks() {}
};

TEST(IDBTransactionTest, EnsureLifetime) {
  V8TestingScope scope;
  std::unique_ptr<MockWebIDBDatabase> backend = MockWebIDBDatabase::Create();
  EXPECT_CALL(*backend, Close()).Times(1);
  Persistent<IDBDatabase> db = IDBDatabase::Create(
      scope.GetExecutionContext(), std::move(backend),
      FakeIDBDatabaseCallbacks::Create(), scope.GetIsolate());

  const int64_t kTransactionId = 1234;
  HashSet<String> transaction_scope = HashSet<String>();
  transaction_scope.insert("test-store-name");
  Persistent<IDBTransaction> transaction =
      IDBTransaction::CreateNonVersionChange(
          scope.GetScriptState(), kTransactionId, transaction_scope,
          kWebIDBTransactionModeReadOnly, db.Get());
  PersistentHeapHashSet<WeakMember<IDBTransaction>> live_transactions;
  live_transactions.insert(transaction);

  ThreadState::Current()->CollectAllGarbage();
  EXPECT_EQ(1u, live_transactions.size());

  Persistent<IDBRequest> request = IDBRequest::Create(
      scope.GetScriptState(), IDBAny::CreateUndefined(), transaction.Get());
  DeactivateNewTransactions(scope.GetIsolate());

  request.Clear();  // The transaction is holding onto the request.
  ThreadState::Current()->CollectAllGarbage();
  EXPECT_EQ(1u, live_transactions.size());

  // This will generate an Abort() call to the back end which is dropped by the
  // fake proxy, so an explicit OnAbort call is made.
  scope.GetExecutionContext()->NotifyContextDestroyed();
  transaction->OnAbort(DOMException::Create(kAbortError, "Aborted"));
  transaction.Clear();

  ThreadState::Current()->CollectAllGarbage();
  EXPECT_EQ(0u, live_transactions.size());
}

TEST(IDBTransactionTest, TransactionFinish) {
  V8TestingScope scope;
  const int64_t kTransactionId = 1234;

  std::unique_ptr<MockWebIDBDatabase> backend = MockWebIDBDatabase::Create();
  EXPECT_CALL(*backend, Commit(kTransactionId)).Times(1);
  EXPECT_CALL(*backend, Close()).Times(1);
  Persistent<IDBDatabase> db = IDBDatabase::Create(
      scope.GetExecutionContext(), std::move(backend),
      FakeIDBDatabaseCallbacks::Create(), scope.GetIsolate());

  HashSet<String> transaction_scope = HashSet<String>();
  transaction_scope.insert("test-store-name");
  Persistent<IDBTransaction> transaction =
      IDBTransaction::CreateNonVersionChange(
          scope.GetScriptState(), kTransactionId, transaction_scope,
          kWebIDBTransactionModeReadOnly, db.Get());
  PersistentHeapHashSet<WeakMember<IDBTransaction>> live_transactions;
  live_transactions.insert(transaction);

  ThreadState::Current()->CollectAllGarbage();
  EXPECT_EQ(1u, live_transactions.size());

  DeactivateNewTransactions(scope.GetIsolate());

  ThreadState::Current()->CollectAllGarbage();
  EXPECT_EQ(1u, live_transactions.size());

  transaction.Clear();

  ThreadState::Current()->CollectAllGarbage();
  EXPECT_EQ(1u, live_transactions.size());

  // Stop the context, so events don't get queued (which would keep the
  // transaction alive).
  scope.GetExecutionContext()->NotifyContextDestroyed();

  // Fire an abort to make sure this doesn't free the transaction during use.
  // The test will not fail if it is, but ASAN would notice the error.
  db->OnAbort(kTransactionId, DOMException::Create(kAbortError, "Aborted"));

  // OnAbort() should have cleared the transaction's reference to the database.
  ThreadState::Current()->CollectAllGarbage();
  EXPECT_EQ(0u, live_transactions.size());
}

}  // namespace
}  // namespace blink
