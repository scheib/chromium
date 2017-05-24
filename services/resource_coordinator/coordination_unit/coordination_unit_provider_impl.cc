// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/coordination_unit/coordination_unit_provider_impl.h"

#include <memory>
#include <utility>

#include "base/logging.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "services/resource_coordinator/coordination_unit/coordination_unit_factory.h"
#include "services/resource_coordinator/coordination_unit/coordination_unit_impl.h"
#include "services/service_manager/public/cpp/service_context_ref.h"

namespace resource_coordinator {

CoordinationUnitProviderImpl::CoordinationUnitProviderImpl(
    service_manager::ServiceContextRefFactory* service_ref_factory)
    : service_ref_factory_(service_ref_factory) {
  DCHECK(service_ref_factory);
  service_ref_ = service_ref_factory->CreateRef();
}

CoordinationUnitProviderImpl::~CoordinationUnitProviderImpl() = default;

void CoordinationUnitProviderImpl::CreateCoordinationUnit(
    mojom::CoordinationUnitRequest request,
    const CoordinationUnitID& id) {
  std::unique_ptr<CoordinationUnitImpl> coordination_unit =
      coordination_unit_factory::CreateCoordinationUnit(
          id, service_ref_factory_->CreateRef());

  mojo::MakeStrongBinding(std::move(coordination_unit), std::move(request));
}

// static
void CoordinationUnitProviderImpl::Create(
    service_manager::ServiceContextRefFactory* service_ref_factory,
    const service_manager::BindSourceInfo& source_info,
    resource_coordinator::mojom::CoordinationUnitProviderRequest request) {
  mojo::MakeStrongBinding(
      base::MakeUnique<CoordinationUnitProviderImpl>(service_ref_factory),
      std::move(request));
}

}  // namespace resource_coordinator
