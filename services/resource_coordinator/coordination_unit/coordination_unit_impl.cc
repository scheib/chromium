// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/coordination_unit/coordination_unit_impl.h"

#include <memory>
#include <unordered_map>
#include <utility>

#include "base/logging.h"
#include "base/process/process_handle.h"
#include "base/strings/string_number_conversions.h"
#include "base/unguessable_token.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "services/resource_coordinator/public/cpp/coordination_unit_id.h"

namespace resource_coordinator {

namespace {

using CUIDMap = std::unordered_map<CoordinationUnitID, CoordinationUnitImpl*>;

CUIDMap& g_cu_map() {
  static CUIDMap* instance = new CUIDMap();
  return *instance;
}

}  // namespace

CoordinationUnitImpl::CoordinationUnitImpl(
    const CoordinationUnitID& id,
    std::unique_ptr<service_manager::ServiceContextRef> service_ref) {
  if (!id.id) {
    id_ = CoordinationUnitID(id.type,
                             base::UnguessableToken().Create().ToString());
  } else {
    id_ = id;
  }

  auto it = g_cu_map().insert(std::make_pair(id_, this));
  DCHECK(it.second);  // Inserted successfully

  service_ref_ = std::move(service_ref);
}

CoordinationUnitImpl::~CoordinationUnitImpl() {
  g_cu_map().erase(id_);

  for (CoordinationUnitImpl* child : children_) {
    child->RemoveParent(this);
  }

  for (CoordinationUnitImpl* parent : parents_) {
    parent->RemoveChild(this);
  }
}

bool CoordinationUnitImpl::SelfOrParentHasFlagSet(StateFlags state) {
  const base::Optional<bool>& state_flag = state_flags_[state];
  if (state_flag && *state_flag) {
    return true;
  }

  for (CoordinationUnitImpl* parent : parents_) {
    if (parent->SelfOrParentHasFlagSet(state)) {
      return true;
    }
  }

  return false;
}

void CoordinationUnitImpl::RecalcCoordinationPolicy() {
  for (CoordinationUnitImpl* child : children_) {
    child->RecalcCoordinationPolicy();
  }

  if (!policy_callback_) {
    return;
  }

  bool background_priority = !SelfOrParentHasFlagSet(kTabVisible) &&
                             !SelfOrParentHasFlagSet(kAudioPlaying);

  // Send the priority to the client if it's new or changed.
  if (!current_policy_) {
    current_policy_ = mojom::CoordinationPolicy::New();
  } else if ((current_policy_->use_background_priority ==
              background_priority) &&
             !SelfOrParentHasFlagSet(StateFlags::kTestState)) {
    return;
  }

  // current_policy_ should be kept in sync with the policy we
  // send to the client, to avoid redundant updates.
  // TODO(oysteine): Once this object becomes more complex, make
  // copying more robust.
  mojom::CoordinationPolicyPtr policy = mojom::CoordinationPolicy::New();
  policy->use_background_priority = background_priority;
  current_policy_->use_background_priority = background_priority;

  policy_callback_->SetCoordinationPolicy(std::move(policy));
}

void CoordinationUnitImpl::SendEvent(mojom::EventPtr event) {
  switch (event->type) {
    case mojom::EventType::kOnWebContentsShown:
      state_flags_[kTabVisible] = true;
      break;
    case mojom::EventType::kOnWebContentsHidden:
      state_flags_[kTabVisible] = false;
      break;
    case mojom::EventType::kOnProcessAudioStarted:
      state_flags_[kAudioPlaying] = true;
      break;
    case mojom::EventType::kOnProcessAudioStopped:
      state_flags_[kAudioPlaying] = false;
      break;
    case mojom::EventType::kTestEvent:
      state_flags_[kTestState] = true;
      break;
    default:
      return;
  }

  RecalcCoordinationPolicy();
}

void CoordinationUnitImpl::GetID(const GetIDCallback& callback) {
  callback.Run(id_);
}

void CoordinationUnitImpl::Duplicate(mojom::CoordinationUnitRequest request) {
  bindings_.AddBinding(this, std::move(request));
}

void CoordinationUnitImpl::AddChild(const CoordinationUnitID& child_id) {
  if (child_id == id_) {
    return;
  }

  auto child_iter = g_cu_map().find(child_id);
  if (child_iter != g_cu_map().end()) {
    CoordinationUnitImpl* child = child_iter->second;
    if (HasParent(child) || HasChild(child)) {
      return;
    }

    DCHECK(child->id_ == child_id);
    DCHECK(child != this);

    if (AddChild(child)) {
      child->AddParent(this);
    }
  }
}

bool CoordinationUnitImpl::AddChild(CoordinationUnitImpl* child) {
  // We don't recalculate the policy here as policies are only dependent
  // on the current CU or its parents, not its children. In other words,
  // policies only bubble down.
  return children_.count(child) ? false : children_.insert(child).second;
}

void CoordinationUnitImpl::RemoveChild(CoordinationUnitImpl* child) {
  size_t children_removed = children_.erase(child);
  DCHECK_EQ(1u, children_removed);
}

void CoordinationUnitImpl::AddParent(CoordinationUnitImpl* parent) {
  DCHECK_EQ(0u, parents_.count(parent));
  parents_.insert(parent);

  RecalcCoordinationPolicy();
}

void CoordinationUnitImpl::RemoveParent(CoordinationUnitImpl* parent) {
  size_t parents_removed = parents_.erase(parent);
  DCHECK_EQ(1u, parents_removed);

  RecalcCoordinationPolicy();
}

bool CoordinationUnitImpl::HasParent(CoordinationUnitImpl* unit) {
  for (CoordinationUnitImpl* parent : parents_) {
    if (parent == unit || parent->HasParent(unit)) {
      return true;
    }
  }

  return false;
}

bool CoordinationUnitImpl::HasChild(CoordinationUnitImpl* unit) {
  for (CoordinationUnitImpl* child : children_) {
    if (child == unit || child->HasChild(unit)) {
      return true;
    }
  }

  return false;
}

void CoordinationUnitImpl::SetCoordinationPolicyCallback(
    mojom::CoordinationPolicyCallbackPtr callback) {
  callback.set_connection_error_handler(
      base::Bind(&CoordinationUnitImpl::UnregisterCoordinationPolicyCallback,
                 base::Unretained(this)));

  policy_callback_ = std::move(callback);

  RecalcCoordinationPolicy();
}

void CoordinationUnitImpl::UnregisterCoordinationPolicyCallback() {
  policy_callback_.reset();
  current_policy_.reset();
}

double CoordinationUnitImpl::GetCPUUsageForTesting() {
  return -1.0;
}

}  // namespace resource_coordinator
