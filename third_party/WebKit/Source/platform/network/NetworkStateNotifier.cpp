/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/network/NetworkStateNotifier.h"

#include "platform/CrossThreadFunctional.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/Functional.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/StdLibExtras.h"
#include "platform/wtf/Threading.h"

namespace blink {

template <>
struct CrossThreadCopier<NetworkStateNotifier::NetworkState>
    : public CrossThreadCopierPassThrough<NetworkStateNotifier::NetworkState> {
  STATIC_ONLY(CrossThreadCopier);
};

NetworkStateNotifier& GetNetworkStateNotifier() {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(NetworkStateNotifier, network_state_notifier,
                                  new NetworkStateNotifier);
  return network_state_notifier;
}

NetworkStateNotifier::ScopedNotifier::ScopedNotifier(
    NetworkStateNotifier& notifier)
    : notifier_(notifier) {
  DCHECK(IsMainThread());
  before_ = notifier_.has_override_ ? notifier_.override_ : notifier_.state_;
}

NetworkStateNotifier::ScopedNotifier::~ScopedNotifier() {
  DCHECK(IsMainThread());
  const NetworkState& after =
      notifier_.has_override_ ? notifier_.override_ : notifier_.state_;
  if ((after.type != before_.type ||
       after.max_bandwidth_mbps != before_.max_bandwidth_mbps ||
       after.http_rtt != before_.http_rtt ||
       after.transport_rtt != before_.transport_rtt ||
       after.downlink_throughput_mbps != before_.downlink_throughput_mbps) &&
      before_.connection_initialized) {
    notifier_.NotifyObservers(notifier_.connection_observers_,
                              ObserverType::CONNECTION_TYPE, after);
  }
  if (after.on_line != before_.on_line && before_.on_line_initialized) {
    notifier_.NotifyObservers(notifier_.on_line_state_observers_,
                              ObserverType::ONLINE_STATE, after);
  }
}

void NetworkStateNotifier::SetOnLine(bool on_line) {
  DCHECK(IsMainThread());
  ScopedNotifier notifier(*this);
  {
    MutexLocker locker(mutex_);
    state_.on_line_initialized = true;
    state_.on_line = on_line;
  }
}

void NetworkStateNotifier::SetWebConnection(WebConnectionType type,
                                            double max_bandwidth_mbps) {
  DCHECK(IsMainThread());
  ScopedNotifier notifier(*this);
  {
    MutexLocker locker(mutex_);
    state_.connection_initialized = true;
    state_.type = type;
    state_.max_bandwidth_mbps = max_bandwidth_mbps;
  }
}

void NetworkStateNotifier::SetNetworkQuality(TimeDelta http_rtt,
                                             TimeDelta transport_rtt,
                                             int downlink_throughput_kbps) {
  DCHECK(IsMainThread());
  ScopedNotifier notifier(*this);
  {
    MutexLocker locker(mutex_);

    state_.http_rtt = base::nullopt;
    state_.transport_rtt = base::nullopt;
    state_.downlink_throughput_mbps = base::nullopt;

    if (http_rtt.InMilliseconds() >= 0)
      state_.http_rtt = http_rtt;

    if (transport_rtt.InMilliseconds() >= 0)
      state_.transport_rtt = transport_rtt;

    if (downlink_throughput_kbps >= 0) {
      state_.downlink_throughput_mbps =
          static_cast<double>(downlink_throughput_kbps) / 1000;
    }
  }
}

void NetworkStateNotifier::AddConnectionObserver(
    NetworkStateObserver* observer,
    PassRefPtr<WebTaskRunner> task_runner) {
  AddObserver(connection_observers_, observer, std::move(task_runner));
}

void NetworkStateNotifier::AddOnLineObserver(
    NetworkStateObserver* observer,
    PassRefPtr<WebTaskRunner> task_runner) {
  AddObserver(on_line_state_observers_, observer, std::move(task_runner));
}

void NetworkStateNotifier::RemoveConnectionObserver(
    NetworkStateObserver* observer,
    PassRefPtr<WebTaskRunner> task_runner) {
  RemoveObserver(connection_observers_, observer, std::move(task_runner));
}

void NetworkStateNotifier::RemoveOnLineObserver(
    NetworkStateObserver* observer,
    PassRefPtr<WebTaskRunner> task_runner) {
  RemoveObserver(on_line_state_observers_, observer, std::move(task_runner));
}

void NetworkStateNotifier::SetOverride(bool on_line,
                                       WebConnectionType type,
                                       double max_bandwidth_mbps) {
  DCHECK(IsMainThread());
  ScopedNotifier notifier(*this);
  {
    MutexLocker locker(mutex_);
    has_override_ = true;
    override_.on_line_initialized = true;
    override_.on_line = on_line;
    override_.connection_initialized = true;
    override_.type = type;
    override_.max_bandwidth_mbps = max_bandwidth_mbps;
  }
}

void NetworkStateNotifier::ClearOverride() {
  DCHECK(IsMainThread());
  ScopedNotifier notifier(*this);
  {
    MutexLocker locker(mutex_);
    has_override_ = false;
  }
}

void NetworkStateNotifier::NotifyObservers(ObserverListMap& map,
                                           ObserverType type,
                                           const NetworkState& state) {
  DCHECK(IsMainThread());
  MutexLocker locker(mutex_);
  for (const auto& entry : map) {
    RefPtr<WebTaskRunner> task_runner = entry.key;
    task_runner->PostTask(
        BLINK_FROM_HERE,
        CrossThreadBind(&NetworkStateNotifier::NotifyObserversOnTaskRunner,
                        CrossThreadUnretained(this),
                        CrossThreadUnretained(&map), type, task_runner, state));
  }
}

void NetworkStateNotifier::NotifyObserversOnTaskRunner(
    ObserverListMap* map,
    ObserverType type,
    RefPtr<WebTaskRunner> task_runner,
    const NetworkState& state) {
  ObserverList* observer_list = LockAndFindObserverList(*map, task_runner);

  // The context could have been removed before the notification task got to
  // run.
  if (!observer_list)
    return;

  DCHECK(task_runner->RunsTasksOnCurrentThread());

  observer_list->iterating = true;

  for (size_t i = 0; i < observer_list->observers.size(); ++i) {
    // Observers removed during iteration are zeroed out, skip them.
    if (!observer_list->observers[i])
      continue;
    switch (type) {
      case ObserverType::ONLINE_STATE:
        observer_list->observers[i]->OnLineStateChange(state.on_line);
        continue;
      case ObserverType::CONNECTION_TYPE:
        observer_list->observers[i]->ConnectionChange(
            state.type, state.max_bandwidth_mbps, state.http_rtt,
            state.transport_rtt, state.downlink_throughput_mbps);
        continue;
    }
    NOTREACHED();
  }

  observer_list->iterating = false;

  if (!observer_list->zeroed_observers.IsEmpty())
    CollectZeroedObservers(*map, observer_list, std::move(task_runner));
}

void NetworkStateNotifier::AddObserver(ObserverListMap& map,
                                       NetworkStateObserver* observer,
                                       PassRefPtr<WebTaskRunner> task_runner) {
  DCHECK(task_runner->RunsTasksOnCurrentThread());
  DCHECK(observer);

  MutexLocker locker(mutex_);
  ObserverListMap::AddResult result =
      map.insert(std::move(task_runner), nullptr);
  if (result.is_new_entry)
    result.stored_value->value = WTF::MakeUnique<ObserverList>();

  DCHECK(result.stored_value->value->observers.Find(observer) == kNotFound);
  result.stored_value->value->observers.push_back(observer);
}

void NetworkStateNotifier::RemoveObserver(ObserverListMap& map,
                                          NetworkStateObserver* observer,
                                          RefPtr<WebTaskRunner> task_runner) {
  DCHECK(task_runner->RunsTasksOnCurrentThread());
  DCHECK(observer);

  ObserverList* observer_list = LockAndFindObserverList(map, task_runner);
  if (!observer_list)
    return;

  Vector<NetworkStateObserver*>& observers = observer_list->observers;
  size_t index = observers.Find(observer);
  if (index != kNotFound) {
    observers[index] = 0;
    observer_list->zeroed_observers.push_back(index);
  }

  if (!observer_list->iterating && !observer_list->zeroed_observers.IsEmpty())
    CollectZeroedObservers(map, observer_list, std::move(task_runner));
}

NetworkStateNotifier::ObserverList*
NetworkStateNotifier::LockAndFindObserverList(
    ObserverListMap& map,
    PassRefPtr<WebTaskRunner> task_runner) {
  MutexLocker locker(mutex_);
  ObserverListMap::iterator it = map.find(task_runner);
  return it == map.end() ? nullptr : it->value.get();
}

void NetworkStateNotifier::CollectZeroedObservers(
    ObserverListMap& map,
    ObserverList* list,
    PassRefPtr<WebTaskRunner> task_runner) {
  DCHECK(task_runner->RunsTasksOnCurrentThread());
  DCHECK(!list->iterating);

  // If any observers were removed during the iteration they will have
  // 0 values, clean them up.
  for (size_t i = 0; i < list->zeroed_observers.size(); ++i)
    list->observers.erase(list->zeroed_observers[i]);

  list->zeroed_observers.clear();

  if (list->observers.IsEmpty()) {
    MutexLocker locker(mutex_);
    map.erase(task_runner);  // deletes list
  }
}

}  // namespace blink
