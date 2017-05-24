// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/memory_instrumentation/coordinator_impl.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/memory_dump_request_args.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/client_process_impl.h"
#include "services/resource_coordinator/public/interfaces/memory_instrumentation/constants.mojom.h"
#include "services/resource_coordinator/public/interfaces/memory_instrumentation/memory_instrumentation.mojom.h"
#include "services/service_manager/public/cpp/identity.h"

#if defined(OS_MACOSX) && !defined(OS_IOS)
#include "base/mac/mac_util.h"
#endif

namespace {

memory_instrumentation::CoordinatorImpl* g_coordinator_impl;

// See design docs linked in the bugs for the rationale of the computation:
// - Linux/Android: https://crbug.com/707019 .
// - Mac OS: https://crbug.com/707021 .
// - Win: https://crbug.com/707022 .
uint32_t CalculatePrivateFootprintKb(
    base::trace_event::MemoryDumpCallbackResult::OSMemDump& os_dump) {
#if defined(OS_LINUX) || defined(OS_ANDROID)
  uint64_t rss_anon_bytes = os_dump.platform_private_footprint.rss_anon_bytes;
  uint64_t vm_swap_bytes = os_dump.platform_private_footprint.vm_swap_bytes;
  return (rss_anon_bytes + vm_swap_bytes) / 1024;
#elif defined(OS_MACOSX)
  // TODO(erikchen): This calculation is close, but not fully accurate. It
  // overcounts by anonymous shared memory.
  if (base::mac::IsAtLeastOS10_12()) {
    uint64_t phys_footprint_bytes =
        os_dump.platform_private_footprint.phys_footprint_bytes;
    return phys_footprint_bytes / 1024;
  } else {
    uint64_t internal_bytes = os_dump.platform_private_footprint.internal_bytes;
    uint64_t compressed_bytes =
        os_dump.platform_private_footprint.compressed_bytes;
    return (internal_bytes + compressed_bytes) / 1024;
  }
#else
  return 0;
#endif
}

}  // namespace

namespace memory_instrumentation {

// static
CoordinatorImpl* CoordinatorImpl::GetInstance() {
  return g_coordinator_impl;
}

CoordinatorImpl::CoordinatorImpl(bool initialize_memory_dump_manager,
                                 service_manager::Connector* connector)
    : failed_memory_dump_count_(0),
      initialize_memory_dump_manager_(initialize_memory_dump_manager) {
  if (initialize_memory_dump_manager) {
    // TODO(primiano): the current state where the coordinator also creates a
    // client (ClientProcessImpl) is contra-intuitive. BrowserMainLoop
    // should be doing this.
    ClientProcessImpl::CreateInstance(
        ClientProcessImpl::Config(this, mojom::ProcessType::BROWSER));
    base::trace_event::MemoryDumpManager::GetInstance()->set_tracing_process_id(
        mojom::kServiceTracingProcessId);
  }
  process_map_ = base::MakeUnique<ProcessMap>(connector);
  DCHECK(!g_coordinator_impl);
  g_coordinator_impl = this;
}

CoordinatorImpl::~CoordinatorImpl() {
  g_coordinator_impl = nullptr;
}

service_manager::Identity CoordinatorImpl::GetClientIdentityForCurrentRequest()
    const {
  return bindings_.dispatch_context();
}

void CoordinatorImpl::BindCoordinatorRequest(
    const service_manager::BindSourceInfo& source_info,
    mojom::CoordinatorRequest request) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  bindings_.AddBinding(this, std::move(request), source_info.identity);
}

void CoordinatorImpl::RequestGlobalMemoryDump(
    const base::trace_event::MemoryDumpRequestArgs& args,
    const RequestGlobalMemoryDumpCallback& callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  bool another_dump_already_in_progress = !queued_memory_dump_requests_.empty();

  // If this is a periodic or peak memory dump request and there already is
  // another request in the queue with the same level of detail, there's no
  // point in enqueuing this request.
  if (another_dump_already_in_progress &&
      args.dump_type !=
          base::trace_event::MemoryDumpType::EXPLICITLY_TRIGGERED &&
      args.dump_type != base::trace_event::MemoryDumpType::SUMMARY_ONLY) {
    for (const auto& request : queued_memory_dump_requests_) {
      if (request.args.level_of_detail == args.level_of_detail) {
        VLOG(1) << base::trace_event::MemoryDumpManager::kLogPrefix << " ("
                << base::trace_event::MemoryDumpTypeToString(args.dump_type)
                << ") skipped because another dump request with the same "
                   "level of detail ("
                << base::trace_event::MemoryDumpLevelOfDetailToString(
                       args.level_of_detail)
                << ") is already in the queue";
        callback.Run(args.dump_guid, false /* success */,
                     nullptr /* global_memory_dump */);
        return;
      }
    }
  }

  queued_memory_dump_requests_.emplace_back(args, callback);

  // If another dump is already in progress, this dump will automatically be
  // scheduled when the other dump finishes.
  if (another_dump_already_in_progress)
    return;

  PerformNextQueuedGlobalMemoryDump();
}

void CoordinatorImpl::RegisterClientProcess(
    mojom::ClientProcessPtr client_process_ptr) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  mojom::ClientProcess* client_process = client_process_ptr.get();
  client_process_ptr.set_connection_error_handler(
      base::Bind(&CoordinatorImpl::UnregisterClientProcess,
                 base::Unretained(this), client_process));
  auto client_info = base::MakeUnique<ClientInfo>(
      GetClientIdentityForCurrentRequest(), std::move(client_process_ptr));
  auto iterator_and_inserted =
      clients_.emplace(client_process, std::move(client_info));
  DCHECK(iterator_and_inserted.second);
}

void CoordinatorImpl::UnregisterClientProcess(
    mojom::ClientProcess* client_process) {
  // Check if we are waiting for an ack from this client process.
  if (pending_clients_for_current_dump_.count(client_process)) {
    DCHECK(!queued_memory_dump_requests_.empty());
    OnProcessMemoryDumpResponse(
        client_process, queued_memory_dump_requests_.front().args.dump_guid,
        false /* success */, nullptr /* process_memory_dump */);
  }
  size_t num_deleted = clients_.erase(client_process);
  DCHECK(num_deleted == 1);
}

void CoordinatorImpl::PerformNextQueuedGlobalMemoryDump() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(!queued_memory_dump_requests_.empty());
  const base::trace_event::MemoryDumpRequestArgs& args =
      queued_memory_dump_requests_.front().args;

  // No need to treat the service process different than other processes. The
  // service process will register itself as a ClientProcess and
  // will be treated like other client processes.
  pending_clients_for_current_dump_.clear();
  failed_memory_dump_count_ = 0;
  for (const auto& kv : clients_) {
    mojom::ClientProcess* client = kv.second->client.get();
    pending_clients_for_current_dump_.insert(client);
    auto callback = base::Bind(&CoordinatorImpl::OnProcessMemoryDumpResponse,
                               base::Unretained(this), client);
    client->RequestProcessMemoryDump(args, callback);
  }
  // Run the callback in case there are no client processes registered.
  FinalizeGlobalMemoryDumpIfAllManagersReplied();
}

void CoordinatorImpl::OnProcessMemoryDumpResponse(
    mojom::ClientProcess* client_process,
    uint64_t dump_guid,
    bool success,
    mojom::ProcessMemoryDumpPtr process_memory_dump) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto it = pending_clients_for_current_dump_.find(client_process);

  if (queued_memory_dump_requests_.empty() ||
      queued_memory_dump_requests_.front().args.dump_guid != dump_guid ||
      it == pending_clients_for_current_dump_.end()) {
    VLOG(1) << "Received unexpected memory dump response: " << dump_guid;
    return;
  }
  if (process_memory_dump) {
    base::ProcessId pid = base::kNullProcessId;
    auto it = clients_.find(client_process);
    if (it != clients_.end()) {
      pid = process_map_->GetProcessId(it->second->identity);
    }

    queued_memory_dump_requests_.front().process_memory_dumps.push_back(
        std::make_pair(pid, std::move(process_memory_dump)));
  }

  pending_clients_for_current_dump_.erase(it);

  if (!success) {
    ++failed_memory_dump_count_;
    VLOG(1) << base::trace_event::MemoryDumpManager::kLogPrefix
            << " failed because of NACK from client";
  }

  FinalizeGlobalMemoryDumpIfAllManagersReplied();
}

void CoordinatorImpl::FinalizeGlobalMemoryDumpIfAllManagersReplied() {
  if (pending_clients_for_current_dump_.size() > 0)
    return;

  DCHECK(!queued_memory_dump_requests_.empty());
  QueuedMemoryDumpRequest& request = queued_memory_dump_requests_.front();

  // Reconstruct a map of pid -> ProcessMemoryDump by reassembling the responses
  // received by the clients for this dump. In some cases the response coming
  // from one client can also provide the dump of OS counters for other
  // processes. A concrete case is Linux, where the browser process provides
  // details for the child processes to get around sandbox restrictions on
  // opening /proc pseudo files.
  std::map<base::ProcessId, mojom::ProcessMemoryDumpPtr> finalized_pmds;
  for (auto& result : request.process_memory_dumps) {
    mojom::ProcessMemoryDumpPtr& pmd = finalized_pmds[result.first];
    if (!pmd)
      pmd = mojom::ProcessMemoryDump::New();
    pmd->chrome_dump = result.second->chrome_dump;

    // TODO(hjd): We should have a better way to tell if os_dump is filled.
    if (result.second->os_dump.resident_set_kb > 0) {
      DCHECK_EQ(0u, pmd->os_dump.resident_set_kb);
      pmd->os_dump = result.second->os_dump;
    }

    for (auto& pair : result.second->extra_processes_dump) {
      base::ProcessId pid = pair.first;
      mojom::ProcessMemoryDumpPtr& pmd = finalized_pmds[pid];
      if (!pmd)
        pmd = mojom::ProcessMemoryDump::New();
      DCHECK_EQ(0u, pmd->os_dump.resident_set_kb);
      pmd->os_dump = result.second->extra_processes_dump[pid];
    }

    pmd->process_type = result.second->process_type;
  }

  mojom::GlobalMemoryDumpPtr global_dump(mojom::GlobalMemoryDump::New());
  for (auto& pair : finalized_pmds) {
    // It's possible that the renderer has died but we still have an os_dump,
    // because those were computed from the browser proces before the renderer
    // died. We should skip these.
    // TODO(hjd): We should have a better way to tell if a chrome_dump is
    // filled.
    mojom::ProcessMemoryDumpPtr& pmd = pair.second;
    if (!pmd || !pmd->chrome_dump.malloc_total_kb)
      continue;
    pmd->private_footprint = CalculatePrivateFootprintKb(pmd->os_dump);
    global_dump->process_dumps.push_back(std::move(pmd));
  }

  const auto& callback = request.callback;
  const bool global_success = failed_memory_dump_count_ == 0;
  callback.Run(request.args.dump_guid, global_success, std::move(global_dump));
  queued_memory_dump_requests_.pop_front();

  // Schedule the next queued dump (if applicable).
  if (!queued_memory_dump_requests_.empty()) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::Bind(&CoordinatorImpl::PerformNextQueuedGlobalMemoryDump,
                   base::Unretained(this)));
  }
}

CoordinatorImpl::QueuedMemoryDumpRequest::QueuedMemoryDumpRequest(
    const base::trace_event::MemoryDumpRequestArgs& args,
    const RequestGlobalMemoryDumpCallback callback)
    : args(args), callback(callback) {}

CoordinatorImpl::QueuedMemoryDumpRequest::~QueuedMemoryDumpRequest() {}

CoordinatorImpl::ClientInfo::ClientInfo(
    const service_manager::Identity& identity,
    mojom::ClientProcessPtr client)
    : identity(identity), client(std::move(client)) {}
CoordinatorImpl::ClientInfo::~ClientInfo() {}

}  // namespace memory_instrumentation
