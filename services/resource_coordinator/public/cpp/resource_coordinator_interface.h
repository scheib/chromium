// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_RESOURCE_COORDINATOR_INTERFACE_H_
#define SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_RESOURCE_COORDINATOR_INTERFACE_H_

#include <stdint.h>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "services/resource_coordinator/public/interfaces/coordination_unit.mojom.h"

namespace service_manager {
class Connector;
}

namespace resource_coordinator {

using EventType = mojom::EventType;

class SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_EXPORT
    ResourceCoordinatorInterface {
 public:
  ResourceCoordinatorInterface(service_manager::Connector* connector,
                               const CoordinationUnitType& type);
  ResourceCoordinatorInterface(service_manager::Connector* connector,
                               const CoordinationUnitType& type,
                               const std::string& id);
  ResourceCoordinatorInterface(service_manager::Connector* connector,
                               const CoordinationUnitType& type,
                               uint64_t id);

  ~ResourceCoordinatorInterface();

  const mojom::CoordinationUnitPtr& service() const { return service_; }

  void SendEvent(const mojom::EventType& event_type);
  void AddChild(const ResourceCoordinatorInterface& child);

 private:
  void ConnectToService(service_manager::Connector* connector,
                        const CoordinationUnitID& cu_id);
  void AddChildByID(const CoordinationUnitID& child_id);

  mojom::CoordinationUnitPtr service_;

  base::ThreadChecker thread_checker_;

  // The WeakPtrFactory should come last so the weak ptrs are invalidated
  // before the rest of the member variables.
  base::WeakPtrFactory<ResourceCoordinatorInterface> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ResourceCoordinatorInterface);
};

}  // namespace resource_coordinator

#endif  // SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_RESOURCE_COORDINATOR_INTERFACE_H_
