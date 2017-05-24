// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/service_manager/common_browser_interfaces.h"

#include <memory>
#include <utility>

#include "base/callback.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/task_runner.h"
#include "components/discardable_memory/service/discardable_shared_memory_manager.h"
#include "content/browser/browser_main_loop.h"
#include "content/public/common/connection_filter.h"
#include "content/public/common/service_manager_connection.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "services/resource_coordinator/memory_instrumentation/coordinator_impl.h"
#include "services/service_manager/public/cpp/binder_registry.h"

namespace content {

namespace {

void BindMemoryCoordinatorRequest(
    const service_manager::BindSourceInfo& source_info,
    memory_instrumentation::mojom::CoordinatorRequest request) {
  auto* coordinator = memory_instrumentation::CoordinatorImpl::GetInstance();
  if (coordinator)
    coordinator->BindCoordinatorRequest(source_info, std::move(request));
}

class ConnectionFilterImpl : public ConnectionFilter {
 public:
  ConnectionFilterImpl()
      : main_thread_task_runner_(base::ThreadTaskRunnerHandle::Get()) {
    RegisterMainThreadInterface(base::Bind(&BindMemoryCoordinatorRequest));

    auto* browser_main_loop = BrowserMainLoop::GetInstance();
    if (browser_main_loop) {
      auto* manager = browser_main_loop->discardable_shared_memory_manager();
      if (manager) {
        registry_.AddInterface(base::Bind(
            &discardable_memory::DiscardableSharedMemoryManager::Bind,
            base::Unretained(manager)));
      }
    }
  }

  ~ConnectionFilterImpl() override {}

 private:
  template <typename Interface>
  using InterfaceBinder =
      base::Callback<void(const service_manager::BindSourceInfo&,
                          mojo::InterfaceRequest<Interface>)>;

  // ConnectionFilter:
  void OnBindInterface(const service_manager::BindSourceInfo& source_info,
                       const std::string& interface_name,
                       mojo::ScopedMessagePipeHandle* interface_pipe,
                       service_manager::Connector* connector) override {
    if (registry_.CanBindInterface(interface_name)) {
      registry_.BindInterface(source_info, interface_name,
                              std::move(*interface_pipe));
    }
  }

  template <typename Interface>
  static void BindOnTaskRunner(
      const scoped_refptr<base::TaskRunner>& task_runner,
      const InterfaceBinder<Interface>& binder,
      const service_manager::BindSourceInfo& source_info,
      mojo::InterfaceRequest<Interface> request) {
    task_runner->PostTask(
        FROM_HERE, base::BindOnce(binder, source_info, std::move(request)));
  }

  template <typename Interface>
  void RegisterMainThreadInterface(const InterfaceBinder<Interface>& binder) {
    registry_.AddInterface(base::Bind(&BindOnTaskRunner<Interface>,
                                      main_thread_task_runner_, binder));
  }

  const scoped_refptr<base::TaskRunner> main_thread_task_runner_;
  service_manager::BinderRegistry registry_;

  DISALLOW_COPY_AND_ASSIGN(ConnectionFilterImpl);
};

}  // namespace

void RegisterCommonBrowserInterfaces(ServiceManagerConnection* connection) {
  connection->AddConnectionFilter(base::MakeUnique<ConnectionFilterImpl>());
}

}  // namespace content
