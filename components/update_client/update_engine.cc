// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/update_client/update_engine.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/stl_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/prefs/pref_service.h"
#include "components/update_client/component.h"
#include "components/update_client/configurator.h"
#include "components/update_client/crx_update_item.h"
#include "components/update_client/persisted_data.h"
#include "components/update_client/update_checker.h"
#include "components/update_client/update_client_errors.h"
#include "components/update_client/utils.h"

namespace update_client {

UpdateContext::UpdateContext(
    const scoped_refptr<Configurator>& config,
    bool is_foreground,
    const std::vector<std::string>& ids,
    const UpdateClient::CrxDataCallback& crx_data_callback,
    const UpdateEngine::NotifyObserversCallback& notify_observers_callback,
    const UpdateEngine::Callback& callback,
    CrxDownloader::Factory crx_downloader_factory)
    : config(config),
      is_foreground(is_foreground),
      enabled_component_updates(config->EnabledComponentUpdates()),
      ids(ids),
      crx_data_callback(crx_data_callback),
      notify_observers_callback(notify_observers_callback),
      callback(callback),
      blocking_task_runner(config->GetSequencedTaskRunner()),
      crx_downloader_factory(crx_downloader_factory) {
  for (const auto& id : ids)
    components.insert(
        std::make_pair(id, base::MakeUnique<Component>(*this, id)));
}

UpdateContext::~UpdateContext() {}

UpdateEngine::UpdateEngine(
    const scoped_refptr<Configurator>& config,
    UpdateChecker::Factory update_checker_factory,
    CrxDownloader::Factory crx_downloader_factory,
    PingManager* ping_manager,
    const NotifyObserversCallback& notify_observers_callback)
    : config_(config),
      update_checker_factory_(update_checker_factory),
      crx_downloader_factory_(crx_downloader_factory),
      ping_manager_(ping_manager),
      metadata_(new PersistedData(config->GetPrefService())),
      notify_observers_callback_(notify_observers_callback) {}

UpdateEngine::~UpdateEngine() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void UpdateEngine::Update(
    bool is_foreground,
    const std::vector<std::string>& ids,
    const UpdateClient::CrxDataCallback& crx_data_callback,
    const Callback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (IsThrottled(is_foreground)) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(callback, Error::RETRY_LATER));
    return;
  }

  const auto result = update_contexts_.insert(base::MakeUnique<UpdateContext>(
      config_, is_foreground, ids, crx_data_callback,
      notify_observers_callback_, callback, crx_downloader_factory_));

  DCHECK(result.second);

  const auto& it = result.first;
  const auto& update_context = *it;
  DCHECK(update_context);

  // Calls out to get the corresponding CrxComponent data for the CRXs in this
  // update context.
  DCHECK_EQ(ids.size(), update_context->ids.size());
  DCHECK_EQ(update_context->ids.size(), update_context->components.size());
  std::vector<CrxComponent> crx_components;
  update_context->crx_data_callback.Run(update_context->ids, &crx_components);
  DCHECK_EQ(update_context->ids.size(), crx_components.size());

  for (size_t i = 0; i != update_context->ids.size(); ++i) {
    const auto& id = update_context->ids[i];
    const auto& crx_component = crx_components[i];

    DCHECK_EQ(id, GetCrxComponentID(crx_component));
    DCHECK_EQ(1u, update_context->components.count(id));
    DCHECK(update_context->components.at(id));

    auto& component = *update_context->components.at(id);
    component.set_on_demand(update_context->is_foreground);
    component.set_crx_component(crx_component);
    component.set_previous_version(crx_component.version);
    component.set_previous_fp(crx_component.fingerprint);

    // Handle |kNew| state. This will transition the components to |kChecking|.
    component.Handle(base::Bind(&UpdateEngine::ComponentCheckingForUpdatesStart,
                                base::Unretained(this), it,
                                base::ConstRef(component)));
  }
}

void UpdateEngine::ComponentCheckingForUpdatesStart(
    const UpdateContextIterator& it,
    const Component& component) {
  DCHECK(thread_checker_.CalledOnValidThread());

  const auto& update_context = *it;
  DCHECK(update_context);

  const auto id = component.id();
  DCHECK_EQ(1u, update_context->components.count(id));
  DCHECK(update_context->components.at(id));

  // Handle |kChecking| state.
  auto& mutable_component = *update_context->components.at(id);
  mutable_component.Handle(base::Bind(
      &UpdateEngine::ComponentCheckingForUpdatesComplete,
      base::Unretained(this), it, base::ConstRef(mutable_component)));

  ++update_context->num_components_ready_to_check;
  if (update_context->num_components_ready_to_check <
      update_context->ids.size()) {
    return;
  }

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&UpdateEngine::DoUpdateCheck, base::Unretained(this), it));
}

void UpdateEngine::DoUpdateCheck(const UpdateContextIterator& it) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& update_context = *it;
  DCHECK(update_context);

  update_context->update_checker =
      update_checker_factory_(config_, metadata_.get());

  update_context->update_checker->CheckForUpdates(
      update_context->ids, update_context->components,
      config_->ExtraRequestParams(), update_context->enabled_component_updates,
      base::Bind(&UpdateEngine::UpdateCheckDone, base::Unretained(this), it));
}

void UpdateEngine::UpdateCheckDone(const UpdateContextIterator& it,
                                   int error,
                                   int retry_after_sec) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& update_context = *it;
  DCHECK(update_context);

  update_context->retry_after_sec = retry_after_sec;

  const int throttle_sec(update_context->retry_after_sec);
  DCHECK_LE(throttle_sec, 24 * 60 * 60);

  // Only positive values for throttle_sec are effective. 0 means that no
  // throttling occurs and has the effect of resetting the member.
  // Negative values are not trusted and are ignored.
  if (throttle_sec >= 0) {
    throttle_updates_until_ =
        throttle_sec ? base::TimeTicks::Now() +
                           base::TimeDelta::FromSeconds(throttle_sec)
                     : base::TimeTicks();
  }

  update_context->update_check_error = error;

  for (const auto& id : update_context->ids) {
    DCHECK_EQ(1u, update_context->components.count(id));
    DCHECK(update_context->components.at(id));

    auto& component = *update_context->components.at(id);
    component.UpdateCheckComplete();
  }
}

void UpdateEngine::ComponentCheckingForUpdatesComplete(
    const UpdateContextIterator& it,
    const Component& component) {
  DCHECK(thread_checker_.CalledOnValidThread());

  const auto& update_context = *it;
  DCHECK(update_context);

  ++update_context->num_components_checked;
  if (update_context->num_components_checked < update_context->ids.size()) {
    return;
  }

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&UpdateEngine::UpdateCheckComplete,
                            base::Unretained(this), it));
}

void UpdateEngine::UpdateCheckComplete(const UpdateContextIterator& it) {
  DCHECK(thread_checker_.CalledOnValidThread());

  const auto& update_context = *it;
  DCHECK(update_context);

  for (const auto& id : update_context->ids)
    update_context->component_queue.push(id);

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&UpdateEngine::HandleComponent, base::Unretained(this), it));
}

void UpdateEngine::HandleComponent(const UpdateContextIterator& it) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& update_context = *it;
  DCHECK(update_context);

  auto& queue = update_context->component_queue;

  if (queue.empty()) {
    const Error error = update_context->update_check_error
                            ? Error::UPDATE_CHECK_ERROR
                            : Error::NONE;
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&UpdateEngine::UpdateComplete,
                              base::Unretained(this), it, error));
    return;
  }

  const auto& id = queue.front();
  DCHECK_EQ(1u, update_context->components.count(id));
  const auto& component = update_context->components.at(id);
  DCHECK(component);

  auto& next_update_delay = (*it)->next_update_delay;
  if (!next_update_delay.is_zero() && component->IsUpdateAvailable()) {
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&UpdateEngine::HandleComponent, base::Unretained(this), it),
        next_update_delay);

    next_update_delay = base::TimeDelta();

    notify_observers_callback_.Run(
        UpdateClient::Observer::Events::COMPONENT_WAIT, id);
    return;
  }

  component->Handle(base::Bind(&UpdateEngine::HandleComponentComplete,
                               base::Unretained(this), it));
}

void UpdateEngine::HandleComponentComplete(const UpdateContextIterator& it) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& update_context = *it;
  DCHECK(update_context);

  auto& queue = update_context->component_queue;
  DCHECK(!queue.empty());

  const auto& id = queue.front();
  DCHECK_EQ(1u, update_context->components.count(id));
  const auto& component = update_context->components.at(id);
  DCHECK(component);

  if (component->IsHandled()) {
    (*it)->next_update_delay = component->GetUpdateDuration();

    if (!component->events().empty()) {
      ping_manager_->SendPing(*component);
    }

    queue.pop();
  }

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&UpdateEngine::HandleComponent, base::Unretained(this), it));
}

void UpdateEngine::UpdateComplete(const UpdateContextIterator& it,
                                  Error error) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& update_context = *it;
  DCHECK(update_context);

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(update_context->callback, error));

  update_contexts_.erase(it);
}

bool UpdateEngine::GetUpdateState(const std::string& id,
                                  CrxUpdateItem* update_item) {
  DCHECK(thread_checker_.CalledOnValidThread());
  for (const auto& context : update_contexts_) {
    const auto it = context->components.find(id);
    if (it != context->components.end()) {
      *update_item = it->second->GetCrxUpdateItem();
      return true;
    }
  }
  return false;
}

bool UpdateEngine::IsThrottled(bool is_foreground) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (is_foreground || throttle_updates_until_.is_null())
    return false;

  const auto now(base::TimeTicks::Now());

  // Throttle the calls in the interval (t - 1 day, t) to limit the effect of
  // unset clocks or clock drift.
  return throttle_updates_until_ - base::TimeDelta::FromDays(1) < now &&
         now < throttle_updates_until_;
}

void UpdateEngine::SendUninstallPing(const std::string& id,
                                     const base::Version& version,
                                     int reason,
                                     const Callback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  const auto result = update_contexts_.insert(base::MakeUnique<UpdateContext>(
      config_, false, std::vector<std::string>{id},
      UpdateClient::CrxDataCallback(), UpdateEngine::NotifyObserversCallback(),
      callback, nullptr));

  DCHECK(result.second);

  const auto& it = result.first;
  const auto& update_context = *it;
  DCHECK(update_context);
  DCHECK_EQ(1u, update_context->ids.size());
  DCHECK_EQ(1u, update_context->components.count(id));
  const auto& component = update_context->components.at(id);

  component->Uninstall(version, reason);

  update_context->component_queue.push(id);

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&UpdateEngine::HandleComponent, base::Unretained(this), it));
}

}  // namespace update_client
