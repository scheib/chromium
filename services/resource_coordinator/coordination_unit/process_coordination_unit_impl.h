// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_RESOURCE_COORDINATOR_COORDINATION_UNIT_PROCESS_COORDINATION_UNIT_IMPL_H_
#define SERVICES_RESOURCE_COORDINATOR_COORDINATION_UNIT_PROCESS_COORDINATION_UNIT_IMPL_H_

#include <memory>

#include "base/macros.h"
#include "base/process/process_metrics.h"
#include "base/timer/timer.h"
#include "services/resource_coordinator/coordination_unit/coordination_unit_impl.h"

namespace service_manager {
class ServiceContextRef;
}

namespace resource_coordinator {

struct CoordinationUnitID;

class ProcessCoordinationUnitImpl : public CoordinationUnitImpl {
 public:
  ProcessCoordinationUnitImpl(
      const CoordinationUnitID& id,
      std::unique_ptr<service_manager::ServiceContextRef> service_ref);
  ~ProcessCoordinationUnitImpl() override;
  void MeasureProcessCPUUsage();
  double GetCPUUsageForTesting() override;

 private:
  std::unique_ptr<base::ProcessMetrics> process_metrics_;
  base::OneShotTimer repeating_timer_;
  double cpu_usage_;

  DISALLOW_COPY_AND_ASSIGN(ProcessCoordinationUnitImpl);
};

}  // namespace resource_coordinator

#endif  // SERVICES_RESOURCE_COORDINATOR_COORDINATION_UNIT_PROCESS_COORDINATION_UNIT_IMPL_H_
