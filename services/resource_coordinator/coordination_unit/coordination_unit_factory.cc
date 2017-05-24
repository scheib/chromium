// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/coordination_unit/coordination_unit_factory.h"

#include <memory>
#include <utility>

#include "services/resource_coordinator/coordination_unit/coordination_unit_impl.h"
#include "services/resource_coordinator/coordination_unit/process_coordination_unit_impl.h"
#include "services/resource_coordinator/public/cpp/coordination_unit_id.h"
#include "services/resource_coordinator/public/cpp/coordination_unit_types.h"
#include "services/service_manager/public/cpp/service_context_ref.h"

namespace resource_coordinator {

namespace coordination_unit_factory {

std::unique_ptr<CoordinationUnitImpl> CreateCoordinationUnit(
    const CoordinationUnitID& id,
    std::unique_ptr<service_manager::ServiceContextRef> service_ref) {
  switch (id.type) {
    case CoordinationUnitType::kProcess:
      return base::MakeUnique<ProcessCoordinationUnitImpl>(
          id, std::move(service_ref));
    default:
      return base::MakeUnique<CoordinationUnitImpl>(id, std::move(service_ref));
  }
}

}  // namespace coordination_unit_factory

}  // namespace resource_coordinator
