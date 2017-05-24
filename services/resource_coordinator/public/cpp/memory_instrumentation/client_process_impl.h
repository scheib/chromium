// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_INSTRUMENTATION_CLIENT_PROCESS_IMPL_H_
#define SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_INSTRUMENTATION_CLIENT_PROCESS_IMPL_H_

#include "base/compiler_specific.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/lock.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/memory_dump_request_args.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/coordinator.h"
#include "services/resource_coordinator/public/cpp/resource_coordinator_export.h"
#include "services/resource_coordinator/public/interfaces/memory_instrumentation/memory_instrumentation.mojom.h"
#include "services/service_manager/public/cpp/connector.h"

namespace memory_instrumentation {

// This is the bridge between MemoryDumpManager and the Coordinator service.
// This indirection is needed to avoid a dependency from //base, where
// MemoryDumpManager lives, to //services, where the Coordinator service lives.
//
// This cannot just be implemented by the Coordinator service, because there is
// no Coordinator service in child processes. So, in a child process, the
// local dump manager remotely connects to the Coordinator service. In the
// browser process, it locally connects to the Coordinator service.
class SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_EXPORT ClientProcessImpl
    : public NON_EXPORTED_BASE(mojom::ClientProcess) {
 public:
  class SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_EXPORT Config {
   public:
    Config(service_manager::Connector* connector,
           const std::string& service_name,
           mojom::ProcessType process_type)
        : connector_(connector),
          service_name_(service_name),
          process_type_(process_type),
          coordinator_(nullptr) {}
    Config(Coordinator* coordinator, mojom::ProcessType process_type)
        : connector_(nullptr),
          process_type_(process_type),
          coordinator_(coordinator) {}
    ~Config();

    service_manager::Connector* connector() const { return connector_; }

    const std::string& service_name() const { return service_name_; }

    mojom::ProcessType process_type() const { return process_type_; }

    Coordinator* coordinator() const { return coordinator_; }

   private:
    service_manager::Connector* connector_;
    const std::string service_name_;
    const mojom::ProcessType process_type_;
    Coordinator* coordinator_;
    bool is_test_config_;
  };

  static void CreateInstance(const Config& config);

  // Implements base::trace_event::MemoryDumpManager::RequestGlobalDumpCallback.
  // NOTE: Use MemoryDumpManager::RequestGlobalDump() to request gobal dump.
  void RequestGlobalMemoryDump(
      const base::trace_event::MemoryDumpRequestArgs& args,
      const base::trace_event::GlobalMemoryDumpCallback& callback);

  Config config() { return config_; }
  void SetAsNonCoordinatorForTesting();

 private:
  friend std::default_delete<ClientProcessImpl>;  // For testing
  friend class ClientProcessImplTest;

  ClientProcessImpl(const Config& config);
  ~ClientProcessImpl() override;

  // mojom::ClientProcess implementation. The Coordinator calls this.
  void RequestProcessMemoryDump(
      const base::trace_event::MemoryDumpRequestArgs& args,
      const RequestProcessMemoryDumpCallback& callback) override;

  // Callback passed to base::MemoryDumpManager::CreateProcessDump().
  void OnProcessMemoryDumpDone(
      const RequestProcessMemoryDumpCallback&,
      uint64_t dump_guid,
      bool success,
      const base::Optional<base::trace_event::MemoryDumpCallbackResult>&);

  // A proxy callback for updating |pending_memory_dump_guid_|.
  void MemoryDumpCallbackProxy(
      const base::trace_event::GlobalMemoryDumpCallback& callback,
      uint64_t dump_guid,
      bool success,
      mojom::GlobalMemoryDumpPtr global_memory_dump);

  mojom::CoordinatorPtr coordinator_;
  mojo::Binding<mojom::ClientProcess> binding_;
  const Config config_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  uint64_t pending_memory_dump_guid_;

  base::Lock pending_memory_dump_guid_lock_;

  DISALLOW_COPY_AND_ASSIGN(ClientProcessImpl);
};

}  // namespace memory_instrumentation

#endif  // SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_INSTRUMENTATION_CLIENT_PROCESS_IMPL_H_
