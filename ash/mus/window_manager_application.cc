// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/mus/window_manager_application.h"

#include <utility>

#include "ash/mojo_interface_factory.h"
#include "ash/mus/network_connect_delegate_mus.h"
#include "ash/mus/window_manager.h"
#include "ash/public/cpp/config.h"
#include "ash/shell_delegate.h"
#include "ash/system/power/power_status.h"
#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chromeos/audio/cras_audio_handler.h"
#include "chromeos/cryptohome/system_salt_getter.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/network/network_connect.h"
#include "chromeos/network/network_handler.h"
#include "chromeos/system/fake_statistics_provider.h"
#include "device/bluetooth/bluetooth_adapter_factory.h"
#include "device/bluetooth/dbus/bluez_dbus_manager.h"
#include "services/service_manager/public/cpp/connector.h"
#include "services/service_manager/public/cpp/service_context.h"
#include "services/ui/common/accelerator_util.h"
#include "ui/aura/env.h"
#include "ui/aura/mus/window_tree_client.h"
#include "ui/events/event.h"
#include "ui/message_center/message_center.h"
#include "ui/views/mus/aura_init.h"

namespace ash {
namespace mus {

WindowManagerApplication::WindowManagerApplication(
    bool show_primary_host_on_connect,
    Config ash_config,
    std::unique_ptr<ash::ShellDelegate> shell_delegate)
    : show_primary_host_on_connect_(show_primary_host_on_connect),
      shell_delegate_(std::move(shell_delegate)),
      ash_config_(ash_config) {}

WindowManagerApplication::~WindowManagerApplication() {
  // Verify that we created a WindowManager before attempting to tear everything
  // down. In some fast running tests OnStart may never have been called.
  if (!window_manager_.get())
    return;

  // Destroy the WindowManager while still valid. This way we ensure
  // OnWillDestroyRootWindowController() is called (if it hasn't been already).
  window_manager_.reset();

  if (blocking_pool_) {
    // Like BrowserThreadImpl, the goal is to make it impossible for ash to
    // 'infinite loop' during shutdown, but to reasonably expect that all
    // BLOCKING_SHUTDOWN tasks queued during shutdown get run. There's nothing
    // particularly scientific about the number chosen.
    const int kMaxNewShutdownBlockingTasks = 1000;
    blocking_pool_->Shutdown(kMaxNewShutdownBlockingTasks);
  }

  statistics_provider_.reset();
  ShutdownComponents();
}

service_manager::Connector* WindowManagerApplication::GetConnector() {
  return context() ? context()->connector() : nullptr;
}

void WindowManagerApplication::InitWindowManager(
    std::unique_ptr<aura::WindowTreeClient> window_tree_client,
    const scoped_refptr<base::SequencedWorkerPool>& blocking_pool,
    bool init_network_handler) {
  // Tests may have already set the WindowTreeClient.
  if (!aura::Env::GetInstance()->HasWindowTreeClient())
    aura::Env::GetInstance()->SetWindowTreeClient(window_tree_client.get());
  InitializeComponents(init_network_handler);

  // TODO(jamescook): Refactor StatisticsProvider so we can get just the data
  // we need in ash. Right now StatisticsProviderImpl launches the crossystem
  // binary to get system data, which we don't want to do twice on startup.
  statistics_provider_.reset(
      new chromeos::system::ScopedFakeStatisticsProvider());
  statistics_provider_->SetMachineStatistic("initial_locale", "en-US");
  statistics_provider_->SetMachineStatistic("keyboard_layout", "");

  window_manager_->Init(std::move(window_tree_client), blocking_pool,
                        std::move(shell_delegate_));
}

void WindowManagerApplication::InitializeComponents(bool init_network_handler) {
  message_center::MessageCenter::Initialize();

  // Must occur after mojo::ApplicationRunner has initialized AtExitManager, but
  // before WindowManager::Init(). Tests might initialize their own instance.
  if (!chromeos::DBusThreadManager::IsInitialized()) {
    chromeos::DBusThreadManager::Initialize(
        chromeos::DBusThreadManager::PROCESS_ASH);
    dbus_thread_manager_initialized_ = true;
  }

  // See ChromeBrowserMainPartsChromeos for ordering details.
  bluez::BluezDBusManager::Initialize(
      chromeos::DBusThreadManager::Get()->GetSystemBus(),
      chromeos::DBusThreadManager::Get()->IsUsingFakes());
  if (init_network_handler && !chromeos::NetworkHandler::IsInitialized()) {
    chromeos::NetworkHandler::Initialize();
    network_handler_initialized_ = true;
  }
  network_connect_delegate_.reset(new NetworkConnectDelegateMus());
  chromeos::NetworkConnect::Initialize(network_connect_delegate_.get());
  // TODO(jamescook): Initialize real audio handler.
  chromeos::CrasAudioHandler::InitializeForTesting();
  chromeos::SystemSaltGetter::Initialize();
}

void WindowManagerApplication::ShutdownComponents() {
  // NOTE: PowerStatus is shutdown by Shell.
  chromeos::SystemSaltGetter::Shutdown();
  chromeos::CrasAudioHandler::Shutdown();
  chromeos::NetworkConnect::Shutdown();
  network_connect_delegate_.reset();
  // We may not have started the NetworkHandler.
  if (network_handler_initialized_)
    chromeos::NetworkHandler::Shutdown();
  device::BluetoothAdapterFactory::Shutdown();
  bluez::BluezDBusManager::Shutdown();
  if (dbus_thread_manager_initialized_)
    chromeos::DBusThreadManager::Shutdown();
  message_center::MessageCenter::Shutdown();
}

void WindowManagerApplication::OnStart() {
  mojo_interface_factory::RegisterInterfaces(
      &registry_, base::ThreadTaskRunnerHandle::Get());

  aura_init_ = base::MakeUnique<views::AuraInit>(
      context()->connector(), context()->identity(), "ash_mus_resources.pak",
      "ash_mus_resources_200.pak", nullptr,
      views::AuraInit::Mode::AURA_MUS_WINDOW_MANAGER);
  window_manager_ = base::MakeUnique<WindowManager>(
      context()->connector(), ash_config_, show_primary_host_on_connect_);

  std::unique_ptr<aura::WindowTreeClient> window_tree_client =
      base::MakeUnique<aura::WindowTreeClient>(
          context()->connector(), window_manager_.get(), window_manager_.get());
  const bool automatically_create_display_roots =
      window_manager_->config() == Config::MASH;
  window_tree_client->ConnectAsWindowManager(
      automatically_create_display_roots);

  const size_t kMaxNumberThreads = 3u;  // Matches that of content.
  const char kThreadNamePrefix[] = "MashBlocking";
  blocking_pool_ = new base::SequencedWorkerPool(
      kMaxNumberThreads, kThreadNamePrefix, base::TaskPriority::USER_VISIBLE);
  const bool init_network_handler = true;
  InitWindowManager(std::move(window_tree_client), blocking_pool_,
                    init_network_handler);
}

void WindowManagerApplication::OnBindInterface(
    const service_manager::BindSourceInfo& source_info,
    const std::string& interface_name,
    mojo::ScopedMessagePipeHandle interface_pipe) {
  registry_.BindInterface(source_info, interface_name,
                          std::move(interface_pipe));
}

}  // namespace mus
}  // namespace ash
