// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/public/cpp/memory_instrumentation/client_process_impl.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/lock.h"
#include "base/trace_event/memory_dump_request_args.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/coordinator.h"
#include "services/resource_coordinator/public/interfaces/memory_instrumentation/memory_instrumentation.mojom.h"
#include "services/service_manager/public/cpp/connector.h"

namespace memory_instrumentation {

ClientProcessImpl::Config::~Config() {}

// static
void ClientProcessImpl::CreateInstance(const Config& config) {
  static ClientProcessImpl* instance = nullptr;
  if (!instance) {
    instance = new ClientProcessImpl(config);
  } else {
    NOTREACHED();
  }
}

ClientProcessImpl::ClientProcessImpl(const Config& config)
    : binding_(this),
      config_(config),
      task_runner_(nullptr),
      pending_memory_dump_guid_(0) {
  if (config.connector() != nullptr) {
    config.connector()->BindInterface(config.service_name(),
                                      mojo::MakeRequest(&coordinator_));
  } else {
    task_runner_ = base::ThreadTaskRunnerHandle::Get();
    config.coordinator()->BindCoordinatorRequest(
        service_manager::BindSourceInfo(), mojo::MakeRequest(&coordinator_));
  }
  coordinator_->RegisterClientProcess(binding_.CreateInterfacePtrAndBind());

  // Only one process should handle periodic dumping.
  bool is_coordinator_process = !!config.coordinator();
  base::trace_event::MemoryDumpManager::GetInstance()->Initialize(
      base::BindRepeating(&ClientProcessImpl::RequestGlobalMemoryDump,
                          base::Unretained(this)),
      is_coordinator_process);
}

ClientProcessImpl::~ClientProcessImpl() {}

void ClientProcessImpl::RequestProcessMemoryDump(
    const base::trace_event::MemoryDumpRequestArgs& args,
    const RequestProcessMemoryDumpCallback& callback) {
  base::trace_event::MemoryDumpManager::GetInstance()->CreateProcessDump(
      args, base::Bind(&ClientProcessImpl::OnProcessMemoryDumpDone,
                       base::Unretained(this), callback));
}

void ClientProcessImpl::OnProcessMemoryDumpDone(
    const RequestProcessMemoryDumpCallback& callback,
    uint64_t dump_guid,
    bool success,
    const base::Optional<base::trace_event::MemoryDumpCallbackResult>& result) {
  mojom::ProcessMemoryDumpPtr process_memory_dump(
      mojom::ProcessMemoryDump::New());
  process_memory_dump->process_type = config_.process_type();
  if (result) {
    process_memory_dump->os_dump = result->os_dump;
    process_memory_dump->chrome_dump = result->chrome_dump;
    for (const auto& kv : result->extra_processes_dump) {
      const base::ProcessId pid = kv.first;
      const base::trace_event::MemoryDumpCallbackResult::OSMemDump&
          os_mem_dump = kv.second;
      DCHECK_EQ(0u, process_memory_dump->extra_processes_dump.count(pid));
      process_memory_dump->extra_processes_dump[pid] = os_mem_dump;
    }
  }
  callback.Run(dump_guid, success, std::move(process_memory_dump));
}

void ClientProcessImpl::RequestGlobalMemoryDump(
    const base::trace_event::MemoryDumpRequestArgs& args,
    const base::trace_event::GlobalMemoryDumpCallback& callback) {
  // Note: This condition is here to match the old behavior. If the delegate is
  // in the browser process, we do not drop parallel requests in the delegate
  // and so they will be queued by the Coordinator service (see
  // CoordinatorImpl::RequestGlobalMemoryDump). If the delegate is in a child
  // process, parallel requests will be cancelled.
  //
  // TODO(primiano): Remove all this boilerplate. There should be no need of
  // any lock, proxy, callback adaption or early out. The service is able to
  // deal with queueing.
  if (task_runner_) {
    auto callback_proxy =
        base::Bind(&ClientProcessImpl::MemoryDumpCallbackProxy,
                   base::Unretained(this), callback);
    task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&mojom::Coordinator::RequestGlobalMemoryDump,
                   base::Unretained(coordinator_.get()), args, callback_proxy));
    return;
  }

  bool early_out_because_of_another_dump_pending = false;
  {
    base::AutoLock lock(pending_memory_dump_guid_lock_);
    if (pending_memory_dump_guid_)
      early_out_because_of_another_dump_pending = true;
    else
      pending_memory_dump_guid_ = args.dump_guid;
  }
  if (early_out_because_of_another_dump_pending) {
    callback.Run(args.dump_guid, false);
    return;
  }

  auto callback_proxy = base::Bind(&ClientProcessImpl::MemoryDumpCallbackProxy,
                                   base::Unretained(this), callback);
  coordinator_->RequestGlobalMemoryDump(args, callback_proxy);
}

void ClientProcessImpl::MemoryDumpCallbackProxy(
    const base::trace_event::GlobalMemoryDumpCallback& callback,
    uint64_t dump_guid,
    bool success,
    mojom::GlobalMemoryDumpPtr) {
  {
    base::AutoLock lock(pending_memory_dump_guid_lock_);
    pending_memory_dump_guid_ = 0;
  }

  // The GlobalMemoryDumpPtr argument is ignored. The actual data of the dump
  // is exposed only through the service and is not passed back to base.
  // TODO(primiano): All these roundtrips are transitional until we move all
  // the clients of memory-infra to use directly the service. crbug.com/720352 .
  callback.Run(dump_guid, success);
}

void ClientProcessImpl::SetAsNonCoordinatorForTesting() {
  task_runner_ = nullptr;
}

}  // namespace memory_instrumentation
