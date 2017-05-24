// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_RESOURCE_COORDINATOR_MEMORY_INSTRUMENTATION_COORDINATOR_IMPL_H_
#define SERVICES_RESOURCE_COORDINATOR_MEMORY_INSTRUMENTATION_COORDINATOR_IMPL_H_

#include <list>
#include <map>
#include <set>

#include "base/memory/ref_counted.h"
#include "base/threading/thread_checker.h"
#include "base/trace_event/memory_dump_request_args.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "services/resource_coordinator/memory_instrumentation/process_map.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/coordinator.h"
#include "services/resource_coordinator/public/interfaces/memory_instrumentation/memory_instrumentation.mojom.h"

namespace service_manager {
class Connector;
}

namespace memory_instrumentation {

// Memory instrumentation service. It serves two purposes:
// - Handles a registry of the processes that have a memory instrumentation
//   client library instance (../../public/cpp/memory).
// - Provides global (i.e. for all processes) memory snapshots on demand.
//   Global snapshots are obtained by requesting in-process snapshots from each
//   registered client and aggregating them.
class CoordinatorImpl : public Coordinator, public mojom::Coordinator {
 public:
  // The getter of the unique instance.
  static CoordinatorImpl* GetInstance();

  CoordinatorImpl(bool initialize_memory_dump_manager,
                  service_manager::Connector* connector);

  // Binds a client library to this coordinator instance.
  void BindCoordinatorRequest(
      const service_manager::BindSourceInfo& source_info,
      mojom::CoordinatorRequest) override;

  bool initialize_memory_dump_manager() const {
    return initialize_memory_dump_manager_;
  }

  // mojom::Coordinator implementation.
  void RegisterClientProcess(mojom::ClientProcessPtr) override;
  void UnregisterClientProcess(mojom::ClientProcess*);
  void RequestGlobalMemoryDump(const base::trace_event::MemoryDumpRequestArgs&,
                               const RequestGlobalMemoryDumpCallback&) override;

 protected:
  // virtual for testing.
  virtual service_manager::Identity GetClientIdentityForCurrentRequest() const;
  ~CoordinatorImpl() override;

 private:
  friend std::default_delete<CoordinatorImpl>;  // For testing
  friend class CoordinatorImplTest;             // For testing

  // Holds data for pending requests enqueued via RequestGlobalMemoryDump().
  struct QueuedMemoryDumpRequest {
    QueuedMemoryDumpRequest(
        const base::trace_event::MemoryDumpRequestArgs& args,
        const RequestGlobalMemoryDumpCallback callback);
    ~QueuedMemoryDumpRequest();
    const base::trace_event::MemoryDumpRequestArgs args;
    const RequestGlobalMemoryDumpCallback callback;

    // Collects the data received from OnProcessMemoryDumpResponse().
    std::vector<std::pair<base::ProcessId, mojom::ProcessMemoryDumpPtr>>
        process_memory_dumps;
  };

  // Holds the identy and remote reference of registered clients.
  struct ClientInfo {
    ClientInfo(const service_manager::Identity&, mojom::ClientProcessPtr);
    ~ClientInfo();

    const service_manager::Identity identity;
    const mojom::ClientProcessPtr client;
  };

  // Callback of RequestProcessMemoryDump.
  void OnProcessMemoryDumpResponse(
      mojom::ClientProcess*,
      uint64_t dump_guid,
      bool success,
      mojom::ProcessMemoryDumpPtr process_memory_dump);

  void PerformNextQueuedGlobalMemoryDump();
  void FinalizeGlobalMemoryDumpIfAllManagersReplied();

  mojo::BindingSet<mojom::Coordinator, service_manager::Identity> bindings_;

  // Map of registered client processes.
  std::map<mojom::ClientProcess*, std::unique_ptr<ClientInfo>> clients_;

  // Oustanding dump requests, enqueued via RequestGlobalMemoryDump().
  std::list<QueuedMemoryDumpRequest> queued_memory_dump_requests_;

  // When a dump, requested via RequestGlobalMemoryDump(), is in progress this
  // set contains the subset of registerd |clients_| that have not yet replied
  // to the local dump request (via RequestProcessMemoryDump()) .
  std::set<mojom::ClientProcess*> pending_clients_for_current_dump_;

  int failed_memory_dump_count_;

  const bool initialize_memory_dump_manager_;

  // Maintains a map of service_manager::Identity -> pid for registered clients.
  std::unique_ptr<ProcessMap> process_map_;

  THREAD_CHECKER(thread_checker_);
  DISALLOW_COPY_AND_ASSIGN(CoordinatorImpl);
};

}  // namespace memory_instrumentation
#endif  // SERVICES_RESOURCE_COORDINATOR_MEMORY_INSTRUMENTATION_COORDINATOR_IMPL_H_
