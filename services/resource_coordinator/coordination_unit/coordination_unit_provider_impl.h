// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_RESOURCE_COORDINATOR_COORDINATION_UNIT_COORDINATION_UNIT_PROVIDER_IMPL_H_
#define SERVICES_RESOURCE_COORDINATOR_COORDINATION_UNIT_COORDINATION_UNIT_PROVIDER_IMPL_H_

#include <memory>

#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "services/resource_coordinator/public/interfaces/coordination_unit_provider.mojom.h"
#include "services/service_manager/public/cpp/bind_source_info.h"

namespace service_manager {

class ServiceContextRefFactory;
class ServiceContextRef;

}  // service_manager

namespace resource_coordinator {

class CoordinationUnitProviderImpl : public mojom::CoordinationUnitProvider {
 public:
  CoordinationUnitProviderImpl(
      service_manager::ServiceContextRefFactory* service_ref_factory);
  ~CoordinationUnitProviderImpl() override;

  static void Create(
      service_manager::ServiceContextRefFactory* service_ref_factory,
      const service_manager::BindSourceInfo& source_info,
      resource_coordinator::mojom::CoordinationUnitProviderRequest request);

  // Overridden from mojom::CoordinationUnitProvider:
  void CreateCoordinationUnit(
      resource_coordinator::mojom::CoordinationUnitRequest request,
      const CoordinationUnitID& id) override;

 private:
  service_manager::ServiceContextRefFactory* service_ref_factory_;
  std::unique_ptr<service_manager::ServiceContextRef> service_ref_;

  DISALLOW_COPY_AND_ASSIGN(CoordinationUnitProviderImpl);
};

}  // namespace resource_coordinator

#endif  // SERVICES_RESOURCE_COORDINATOR_COORDINATION_UNIT_COORDINATION_UNIT_PROVIDER_IMPL_H_
