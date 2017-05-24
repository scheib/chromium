// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/PartitionAllocMemoryDumpProvider.h"

#include <unordered_map>

#include "base/strings/stringprintf.h"
#include "base/trace_event/heap_profiler_allocation_context_tracker.h"
#include "base/trace_event/heap_profiler_allocation_register.h"
#include "base/trace_event/process_memory_dump.h"
#include "base/trace_event/trace_event_memory_overhead.h"
#include "platform/wtf/allocator/Partitions.h"
#include "platform/wtf/text/WTFString.h"

namespace blink {

namespace {

using namespace WTF;

void ReportAllocation(void* address, size_t size, const char* type_name) {
  PartitionAllocMemoryDumpProvider::Instance()->insert(address, size,
                                                       type_name);
}

void ReportFree(void* address) {
  PartitionAllocMemoryDumpProvider::Instance()->Remove(address);
}

const char kPartitionAllocDumpName[] = "partition_alloc";
const char kPartitionsDumpName[] = "partitions";

std::string GetPartitionDumpName(const char* partition_name) {
  return base::StringPrintf("%s/%s/%s", kPartitionAllocDumpName,
                            kPartitionsDumpName, partition_name);
}

// This class is used to invert the dependency of PartitionAlloc on the
// PartitionAllocMemoryDumpProvider. This implements an interface that will
// be called with memory statistics for each bucket in the allocator.
class PartitionStatsDumperImpl final : public WTF::PartitionStatsDumper {
  DISALLOW_NEW();
  WTF_MAKE_NONCOPYABLE(PartitionStatsDumperImpl);

 public:
  PartitionStatsDumperImpl(
      base::trace_event::ProcessMemoryDump* memory_dump,
      base::trace_event::MemoryDumpLevelOfDetail level_of_detail)
      : memory_dump_(memory_dump), uid_(0), total_active_bytes_(0) {}

  // PartitionStatsDumper implementation.
  void PartitionDumpTotals(const char* partition_name,
                           const WTF::PartitionMemoryStats*) override;
  void PartitionsDumpBucketStats(
      const char* partition_name,
      const WTF::PartitionBucketMemoryStats*) override;

  size_t TotalActiveBytes() const { return total_active_bytes_; }

 private:
  base::trace_event::ProcessMemoryDump* memory_dump_;
  unsigned long uid_;
  size_t total_active_bytes_;
};

void PartitionStatsDumperImpl::PartitionDumpTotals(
    const char* partition_name,
    const WTF::PartitionMemoryStats* memory_stats) {
  total_active_bytes_ += memory_stats->total_active_bytes;
  std::string dump_name = GetPartitionDumpName(partition_name);
  base::trace_event::MemoryAllocatorDump* allocator_dump =
      memory_dump_->CreateAllocatorDump(dump_name);
  allocator_dump->AddScalar("size", "bytes",
                            memory_stats->total_resident_bytes);
  allocator_dump->AddScalar("allocated_objects_size", "bytes",
                            memory_stats->total_active_bytes);
  allocator_dump->AddScalar("virtual_size", "bytes",
                            memory_stats->total_mmapped_bytes);
  allocator_dump->AddScalar("virtual_committed_size", "bytes",
                            memory_stats->total_committed_bytes);
  allocator_dump->AddScalar("decommittable_size", "bytes",
                            memory_stats->total_decommittable_bytes);
  allocator_dump->AddScalar("discardable_size", "bytes",
                            memory_stats->total_discardable_bytes);
}

void PartitionStatsDumperImpl::PartitionsDumpBucketStats(
    const char* partition_name,
    const WTF::PartitionBucketMemoryStats* memory_stats) {
  DCHECK(memory_stats->is_valid);
  std::string dump_name = GetPartitionDumpName(partition_name);
  if (memory_stats->is_direct_map) {
    dump_name.append(base::StringPrintf("/directMap_%lu", ++uid_));
  } else {
    dump_name.append(base::StringPrintf(
        "/bucket_%u", static_cast<unsigned>(memory_stats->bucket_slot_size)));
  }

  base::trace_event::MemoryAllocatorDump* allocator_dump =
      memory_dump_->CreateAllocatorDump(dump_name);
  allocator_dump->AddScalar("size", "bytes", memory_stats->resident_bytes);
  allocator_dump->AddScalar("allocated_objects_size", "bytes",
                            memory_stats->active_bytes);
  allocator_dump->AddScalar("slot_size", "bytes",
                            memory_stats->bucket_slot_size);
  allocator_dump->AddScalar("decommittable_size", "bytes",
                            memory_stats->decommittable_bytes);
  allocator_dump->AddScalar("discardable_size", "bytes",
                            memory_stats->discardable_bytes);
  allocator_dump->AddScalar("total_pages_size", "bytes",
                            memory_stats->allocated_page_size);
  allocator_dump->AddScalar("active_pages", "objects",
                            memory_stats->num_active_pages);
  allocator_dump->AddScalar("full_pages", "objects",
                            memory_stats->num_full_pages);
  allocator_dump->AddScalar("empty_pages", "objects",
                            memory_stats->num_empty_pages);
  allocator_dump->AddScalar("decommitted_pages", "objects",
                            memory_stats->num_decommitted_pages);
}

}  // namespace

PartitionAllocMemoryDumpProvider* PartitionAllocMemoryDumpProvider::Instance() {
  DEFINE_STATIC_LOCAL(PartitionAllocMemoryDumpProvider, instance, ());
  return &instance;
}

bool PartitionAllocMemoryDumpProvider::OnMemoryDump(
    const base::trace_event::MemoryDumpArgs& args,
    base::trace_event::ProcessMemoryDump* memory_dump) {
  using base::trace_event::MemoryDumpLevelOfDetail;

  MemoryDumpLevelOfDetail level_of_detail = args.level_of_detail;
  if (allocation_register_.is_enabled()) {
    // Overhead should always be reported, regardless of light vs. heavy.
    base::trace_event::TraceEventMemoryOverhead overhead;
    std::unordered_map<base::trace_event::AllocationContext,
                       base::trace_event::AllocationMetrics>
        metrics_by_context;
    // Dump only the overhead estimation in non-detailed dumps.
    if (level_of_detail == MemoryDumpLevelOfDetail::DETAILED) {
      allocation_register_.UpdateAndReturnsMetrics(metrics_by_context);
    }
    allocation_register_.EstimateTraceMemoryOverhead(&overhead);
    memory_dump->DumpHeapUsage(metrics_by_context, overhead, "partition_alloc");
  }

  PartitionStatsDumperImpl partition_stats_dumper(memory_dump, level_of_detail);

  base::trace_event::MemoryAllocatorDump* partitions_dump =
      memory_dump->CreateAllocatorDump(base::StringPrintf(
          "%s/%s", kPartitionAllocDumpName, kPartitionsDumpName));

  // This method calls memoryStats.partitionsDumpBucketStats with memory
  // statistics.
  WTF::Partitions::DumpMemoryStats(
      level_of_detail != MemoryDumpLevelOfDetail::DETAILED,
      &partition_stats_dumper);

  base::trace_event::MemoryAllocatorDump* allocated_objects_dump =
      memory_dump->CreateAllocatorDump(Partitions::kAllocatedObjectPoolName);
  allocated_objects_dump->AddScalar("size", "bytes",
                                    partition_stats_dumper.TotalActiveBytes());
  memory_dump->AddOwnershipEdge(allocated_objects_dump->guid(),
                                partitions_dump->guid());

  return true;
}

// |m_allocationRegister| should be initialized only when necessary to avoid
// waste of memory.
PartitionAllocMemoryDumpProvider::PartitionAllocMemoryDumpProvider() {}

PartitionAllocMemoryDumpProvider::~PartitionAllocMemoryDumpProvider() {}

void PartitionAllocMemoryDumpProvider::OnHeapProfilingEnabled(bool enabled) {
  if (enabled) {
    allocation_register_.SetEnabled();
    WTF::PartitionAllocHooks::SetAllocationHook(ReportAllocation);
    WTF::PartitionAllocHooks::SetFreeHook(ReportFree);
  } else {
    WTF::PartitionAllocHooks::SetAllocationHook(nullptr);
    WTF::PartitionAllocHooks::SetFreeHook(nullptr);
    allocation_register_.SetDisabled();
  }
}

void PartitionAllocMemoryDumpProvider::insert(void* address,
                                              size_t size,
                                              const char* type_name) {
  base::trace_event::AllocationContext context;
  if (!base::trace_event::AllocationContextTracker::
           GetInstanceForCurrentThread()
               ->GetContextSnapshot(&context))
    return;

  context.type_name = type_name;
  if (!allocation_register_.is_enabled())
    return;
  allocation_register_.Insert(address, size, context);
}

void PartitionAllocMemoryDumpProvider::Remove(void* address) {
  if (!allocation_register_.is_enabled())
    return;
  allocation_register_.Remove(address);
}

}  // namespace blink
