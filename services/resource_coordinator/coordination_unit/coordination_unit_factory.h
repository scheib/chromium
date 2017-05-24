// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_RESOURCE_COORDINATOR_COORDINATION_UNIT_COORDINATION_UNIT_FACTORY_H_
#define SERVICES_RESOURCE_COORDINATOR_COORDINATION_UNIT_COORDINATION_UNIT_FACTORY_H_

#include <memory>

#include "services/resource_coordinator/coordination_unit/coordination_unit_impl.h"

namespace service_manager {
class ServiceContextRef;
}

namespace resource_coordinator {

struct CoordinationUnitID;

namespace coordination_unit_factory {

std::unique_ptr<CoordinationUnitImpl> CreateCoordinationUnit(
    const CoordinationUnitID& id,
    std::unique_ptr<service_manager::ServiceContextRef> service_ref);

}  // namespace coordination_unit_factory

}  // namespace resource_coordinator

#endif  // SERVICES_RESOURCE_COORDINATOR_COORDINATION_UNIT_COORDINATION_UNIT_FACTORY_H_
