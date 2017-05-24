// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_handle.h"
#include "base/run_loop.h"
#include "services/resource_coordinator/coordination_unit/coordination_unit_factory.h"
#include "services/resource_coordinator/coordination_unit/coordination_unit_impl.h"
#include "services/resource_coordinator/coordination_unit/coordination_unit_impl_unittest_util.h"
#include "services/resource_coordinator/public/cpp/coordination_unit_id.h"
#include "services/resource_coordinator/public/cpp/coordination_unit_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace resource_coordinator {

namespace {

class ProcessCoordinationUnitImplTest : public CoordinationUnitImplTestBase {};

}  // namespace

TEST_F(ProcessCoordinationUnitImplTest, MeasureCPUUsage) {
  base::ProcessId current_pid = base::GetCurrentProcId();
  CoordinationUnitID cu_id(CoordinationUnitType::kProcess, current_pid);

  std::unique_ptr<CoordinationUnitImpl> coordination_unit_ =
      coordination_unit_factory::CreateCoordinationUnit(
          cu_id, service_context_ref_factory()->CreateRef());

  base::RunLoop().RunUntilIdle();

  EXPECT_LE(0.0, coordination_unit_->GetCPUUsageForTesting());
}

}  // namespace resource_coordinator
