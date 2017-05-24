// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/coordination_unit/process_coordination_unit_impl.h"

#include <memory>
#include <utility>

#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/process/process_metrics.h"
#include "base/time/time.h"

#if defined(OS_MACOSX)
#include "services/service_manager/public/cpp/standalone_service/mach_broker.h"
#endif

#if defined(OS_WIN)
#include <windows.h>
#endif

namespace service_manager {
class ServiceContextRef;
}

namespace resource_coordinator {

struct CoordinationUnitID;

namespace {

const int kCPUProfilingIntervalInSeconds = 5;

}  // namespace

ProcessCoordinationUnitImpl::ProcessCoordinationUnitImpl(
    const CoordinationUnitID& id,
    std::unique_ptr<service_manager::ServiceContextRef> service_ref)
    : CoordinationUnitImpl(id, std::move(service_ref)), cpu_usage_(-1.0) {
  // ProcessCoordinationUnit ids should correspond to its pid
  base::ProcessId pid = id.id;
#if defined(OS_WIN)
  base::Process process =
      base::Process::OpenWithAccess(pid, PROCESS_QUERY_INFORMATION);
#else
  base::Process process = base::Process::Open(pid);
#endif
  base::ProcessHandle process_handle = process.Handle();

#if defined(OS_MACOSX)
  process_metrics_ = base::ProcessMetrics::CreateProcessMetrics(
      process_handle,
      service_manager::MachBroker::GetInstance()->port_provider());
#else
  process_metrics_ = base::ProcessMetrics::CreateProcessMetrics(process_handle);
#endif

  repeating_timer_.Start(FROM_HERE, base::TimeDelta(), this,
                         &ProcessCoordinationUnitImpl::MeasureProcessCPUUsage);
}

ProcessCoordinationUnitImpl::~ProcessCoordinationUnitImpl() = default;

void ProcessCoordinationUnitImpl::MeasureProcessCPUUsage() {
  cpu_usage_ = process_metrics_->GetPlatformIndependentCPUUsage();

  repeating_timer_.Start(
      FROM_HERE, base::TimeDelta::FromSeconds(kCPUProfilingIntervalInSeconds),
      this, &ProcessCoordinationUnitImpl::MeasureProcessCPUUsage);
}

double ProcessCoordinationUnitImpl::GetCPUUsageForTesting() {
  return cpu_usage_;
}

}  // namespace resource_coordinator
