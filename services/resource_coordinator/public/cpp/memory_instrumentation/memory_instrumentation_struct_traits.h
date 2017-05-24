// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_INSTRUMENTATION_MEMORY_INSTRUMENTATION_STRUCT_TRAITS_H_
#define SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_INSTRUMENTATION_MEMORY_INSTRUMENTATION_STRUCT_TRAITS_H_

#include "base/process/process_handle.h"
#include "base/trace_event/memory_dump_request_args.h"
#include "base/trace_event/process_memory_totals.h"
#include "mojo/common/common_custom_types_struct_traits.h"
#include "services/resource_coordinator/public/cpp/resource_coordinator_export.h"
#include "services/resource_coordinator/public/interfaces/memory_instrumentation/memory_instrumentation.mojom.h"

namespace mojo {

template <>
struct SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_EXPORT
    EnumTraits<memory_instrumentation::mojom::DumpType,
               base::trace_event::MemoryDumpType> {
  static memory_instrumentation::mojom::DumpType ToMojom(
      base::trace_event::MemoryDumpType type);
  static bool FromMojom(memory_instrumentation::mojom::DumpType input,
                        base::trace_event::MemoryDumpType* out);
};

template <>
struct SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_EXPORT
    EnumTraits<memory_instrumentation::mojom::LevelOfDetail,
               base::trace_event::MemoryDumpLevelOfDetail> {
  static memory_instrumentation::mojom::LevelOfDetail ToMojom(
      base::trace_event::MemoryDumpLevelOfDetail level_of_detail);
  static bool FromMojom(memory_instrumentation::mojom::LevelOfDetail input,
                        base::trace_event::MemoryDumpLevelOfDetail* out);
};

template <>
struct SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_EXPORT
    StructTraits<memory_instrumentation::mojom::RequestArgsDataView,
                 base::trace_event::MemoryDumpRequestArgs> {
  static uint64_t dump_guid(
      const base::trace_event::MemoryDumpRequestArgs& args) {
    return args.dump_guid;
  }
  static base::trace_event::MemoryDumpType dump_type(
      const base::trace_event::MemoryDumpRequestArgs& args) {
    return args.dump_type;
  }
  static base::trace_event::MemoryDumpLevelOfDetail level_of_detail(
      const base::trace_event::MemoryDumpRequestArgs& args) {
    return args.level_of_detail;
  }
  static bool Read(memory_instrumentation::mojom::RequestArgsDataView input,
                   base::trace_event::MemoryDumpRequestArgs* out);
};

template <>
struct SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_EXPORT StructTraits<
    memory_instrumentation::mojom::PlatformPrivateFootprintDataView,
    base::trace_event::ProcessMemoryTotals::PlatformPrivateFootprint> {
  static uint64_t phys_footprint_bytes(
      const base::trace_event::ProcessMemoryTotals::PlatformPrivateFootprint&
          args) {
    return args.phys_footprint_bytes;
  }
  static uint64_t internal_bytes(
      const base::trace_event::ProcessMemoryTotals::PlatformPrivateFootprint&
          args) {
    return args.internal_bytes;
  }
  static uint64_t compressed_bytes(
      const base::trace_event::ProcessMemoryTotals::PlatformPrivateFootprint&
          args) {
    return args.compressed_bytes;
  }
  static uint64_t rss_anon_bytes(
      const base::trace_event::ProcessMemoryTotals::PlatformPrivateFootprint&
          args) {
    return args.rss_anon_bytes;
  }
  static uint64_t vm_swap_bytes(
      const base::trace_event::ProcessMemoryTotals::PlatformPrivateFootprint&
          args) {
    return args.vm_swap_bytes;
  }
  static bool Read(
      memory_instrumentation::mojom::PlatformPrivateFootprintDataView input,
      base::trace_event::ProcessMemoryTotals::PlatformPrivateFootprint* out);
};

template <>
struct SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_EXPORT
    StructTraits<memory_instrumentation::mojom::ChromeMemDumpDataView,
                 base::trace_event::MemoryDumpCallbackResult::ChromeMemDump> {
  static uint32_t malloc_total_kb(
      const base::trace_event::MemoryDumpCallbackResult::ChromeMemDump& args) {
    return args.malloc_total_kb;
  }
  static uint32_t partition_alloc_total_kb(
      const base::trace_event::MemoryDumpCallbackResult::ChromeMemDump& args) {
    return args.partition_alloc_total_kb;
  }
  static uint32_t blink_gc_total_kb(
      const base::trace_event::MemoryDumpCallbackResult::ChromeMemDump& args) {
    return args.blink_gc_total_kb;
  }
  static uint32_t v8_total_kb(
      const base::trace_event::MemoryDumpCallbackResult::ChromeMemDump& args) {
    return args.v8_total_kb;
  }
  static bool Read(
      memory_instrumentation::mojom::ChromeMemDumpDataView input,
      base::trace_event::MemoryDumpCallbackResult::ChromeMemDump* out);
};

template <>
struct SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_EXPORT
    StructTraits<memory_instrumentation::mojom::OSMemDumpDataView,
                 base::trace_event::MemoryDumpCallbackResult::OSMemDump> {
  static uint32_t resident_set_kb(
      const base::trace_event::MemoryDumpCallbackResult::OSMemDump& args) {
    return args.resident_set_kb;
  }
  static base::trace_event::ProcessMemoryTotals::PlatformPrivateFootprint
  platform_private_footprint(
      const base::trace_event::MemoryDumpCallbackResult::OSMemDump& args) {
    return args.platform_private_footprint;
  }
  static bool Read(memory_instrumentation::mojom::OSMemDumpDataView input,
                   base::trace_event::MemoryDumpCallbackResult::OSMemDump* out);
};

}  // namespace mojo

#endif  // SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_INSTRUMENTATION_MEMORY_INSTRUMENTATION_STRUCT_TRAITS_H_
