// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/coordination_unit/coordination_unit_impl_unittest_util.h"

#include "base/bind.h"
#include "base/run_loop.h"

namespace resource_coordinator {

namespace {

void OnLastServiceRefDestroyed() {
  // No-op. This is required by service_manager::ServiceContextRefFactory
  // construction but not needed for the tests.
}

}  // namespace

CoordinationUnitImplTestBase::CoordinationUnitImplTestBase()
    : service_ref_factory_(base::Bind(&OnLastServiceRefDestroyed)),
      provider_(&service_ref_factory_) {}

CoordinationUnitImplTestBase::~CoordinationUnitImplTestBase() = default;

void CoordinationUnitImplTestBase::TearDown() {
  base::RunLoop().RunUntilIdle();
}

}  // namespace resource_coordinator
