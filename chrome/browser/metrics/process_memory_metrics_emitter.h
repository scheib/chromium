// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_METRICS_PROCESS_MEMORY_METRICS_EMITTER_H_
#define CHROME_BROWSER_METRICS_PROCESS_MEMORY_METRICS_EMITTER_H_

#include "base/memory/ref_counted.h"
#include "services/resource_coordinator/public/interfaces/memory_instrumentation/memory_instrumentation.mojom.h"

// This class asynchronously fetches memory metrics for each process, and then
// emits UMA metrics from those metrics.
// Each instance is self-owned, and will delete itself once it has finished
// emitting metrics.
// This class is an analog to MetricsMemoryDetails, but uses the resource
// coordinator service to fetch data, rather than doing all the processing
// manually.
class ProcessMemoryMetricsEmitter
    : public base::RefCountedThreadSafe<ProcessMemoryMetricsEmitter> {
 public:
  ProcessMemoryMetricsEmitter();

  // This must be called on the main thread of the browser process.
  void FetchAndEmitProcessMemoryMetrics();

 protected:
  virtual ~ProcessMemoryMetricsEmitter();

  // Virtual for testing.
  virtual void ReceivedMemoryDump(
      uint64_t dump_guid,
      bool success,
      memory_instrumentation::mojom::GlobalMemoryDumpPtr ptr);

 private:
  friend class base::RefCountedThreadSafe<ProcessMemoryMetricsEmitter>;

  memory_instrumentation::mojom::CoordinatorPtr coordinator_;

  DISALLOW_COPY_AND_ASSIGN(ProcessMemoryMetricsEmitter);
};

#endif  // CHROME_BROWSER_METRICS_PROCESS_MEMORY_METRICS_EMITTER_H_
