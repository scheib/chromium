// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/fetch/GlobalFetch.h"

#include "core/frame/LocalDOMWindow.h"
#include "core/frame/UseCounter.h"
#include "core/probe/CoreProbes.h"
#include "core/workers/WorkerGlobalScope.h"
#include "modules/fetch/FetchManager.h"
#include "modules/fetch/Request.h"
#include "platform/Supplementable.h"
#include "platform/heap/Handle.h"

namespace blink {

namespace {

template <typename T>
class GlobalFetchImpl final
    : public GarbageCollectedFinalized<GlobalFetchImpl<T>>,
      public GlobalFetch::ScopedFetcher,
      public Supplement<T> {
  USING_GARBAGE_COLLECTED_MIXIN(GlobalFetchImpl);

 public:
  static ScopedFetcher* From(T& supplementable,
                             ExecutionContext* execution_context) {
    GlobalFetchImpl* supplement = static_cast<GlobalFetchImpl*>(
        Supplement<T>::From(supplementable, SupplementName()));
    if (!supplement) {
      supplement = new GlobalFetchImpl(execution_context);
      Supplement<T>::ProvideTo(supplementable, SupplementName(), supplement);
    }
    return supplement;
  }

  ScriptPromise Fetch(ScriptState* script_state,
                      const RequestInfo& input,
                      const Dictionary& init,
                      ExceptionState& exception_state) override {
    ExecutionContext* execution_context = fetch_manager_->GetExecutionContext();
    if (!script_state->ContextIsValid() || !execution_context) {
      // TODO(yhirano): Should this be moved to bindings?
      exception_state.ThrowTypeError("The global scope is shutting down.");
      return ScriptPromise();
    }

    // "Let |r| be the associated request of the result of invoking the
    // initial value of Request as constructor with |input| and |init| as
    // arguments. If this throws an exception, reject |p| with it."
    Request* r = Request::Create(script_state, input, init, exception_state);
    if (exception_state.HadException())
      return ScriptPromise();

    probe::willSendXMLHttpOrFetchNetworkRequest(execution_context, r->url());
    return fetch_manager_->Fetch(script_state,
                                 r->PassRequestData(script_state));
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(fetch_manager_);
    ScopedFetcher::Trace(visitor);
    Supplement<T>::Trace(visitor);
  }

 private:
  explicit GlobalFetchImpl(ExecutionContext* execution_context)
      : fetch_manager_(FetchManager::Create(execution_context)) {}

  static const char* SupplementName() { return "GlobalFetch"; }

  Member<FetchManager> fetch_manager_;
};

}  // namespace

GlobalFetch::ScopedFetcher::~ScopedFetcher() {}

GlobalFetch::ScopedFetcher* GlobalFetch::ScopedFetcher::From(
    LocalDOMWindow& window) {
  return GlobalFetchImpl<LocalDOMWindow>::From(window,
                                               window.GetExecutionContext());
}

GlobalFetch::ScopedFetcher* GlobalFetch::ScopedFetcher::From(
    WorkerGlobalScope& worker) {
  return GlobalFetchImpl<WorkerGlobalScope>::From(worker,
                                                  worker.GetExecutionContext());
}

DEFINE_TRACE(GlobalFetch::ScopedFetcher) {}

ScriptPromise GlobalFetch::fetch(ScriptState* script_state,
                                 LocalDOMWindow& window,
                                 const RequestInfo& input,
                                 const Dictionary& init,
                                 ExceptionState& exception_state) {
  UseCounter::Count(window.GetExecutionContext(), UseCounter::kFetch);
  if (!window.GetFrame()) {
    exception_state.ThrowTypeError("The global scope is shutting down.");
    return ScriptPromise();
  }
  return ScopedFetcher::From(window)->Fetch(script_state, input, init,
                                            exception_state);
}

ScriptPromise GlobalFetch::fetch(ScriptState* script_state,
                                 WorkerGlobalScope& worker,
                                 const RequestInfo& input,
                                 const Dictionary& init,
                                 ExceptionState& exception_state) {
  UseCounter::Count(worker.GetExecutionContext(), UseCounter::kFetch);
  return ScopedFetcher::From(worker)->Fetch(script_state, input, init,
                                            exception_state);
}

}  // namespace blink
