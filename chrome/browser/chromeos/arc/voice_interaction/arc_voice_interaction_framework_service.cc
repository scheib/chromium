// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/voice_interaction/arc_voice_interaction_framework_service.h"

#include <utility>
#include <vector>

#include "ash/accelerators/accelerator_controller.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/shell.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/containers/flat_set.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/task_scheduler/post_task.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chromeos/chromeos_switches.h"
#include "components/arc/arc_bridge_service.h"
#include "components/arc/instance_holder.h"
#include "content/public/browser/browser_thread.h"
#include "ui/aura/window.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_owner.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_util.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/snapshot/snapshot.h"
#include "ui/snapshot/snapshot_aura.h"
#include "ui/wm/core/window_util.h"
#include "ui/wm/public/activation_client.h"

namespace arc {

namespace {

void ScreenshotCallback(
    const mojom::VoiceInteractionFrameworkHost::CaptureFocusedWindowCallback&
        callback,
    scoped_refptr<base::RefCountedMemory> data) {
  if (data.get() == nullptr) {
    callback.Run(std::vector<uint8_t>{});
    return;
  }
  std::vector<uint8_t> result(data->front(), data->front() + data->size());
  callback.Run(result);
}

std::unique_ptr<ui::LayerTreeOwner> CreateLayerTreeForSnapshot(
    aura::Window* root_window) {
  using LayerSet = base::flat_set<const ui::Layer*>;
  LayerSet blocked_layers;
  for (auto* browser : *BrowserList::GetInstance()) {
    if (browser->profile()->IsOffTheRecord())
      blocked_layers.insert(browser->window()->GetNativeWindow()->layer());
  }

  LayerSet excluded_layers;
  // For the best UX the metalayer has to be excluded from the snapshot.
  // It is currently impossible to identify the metalayer among others layers
  // under kShellWindowId_SystemModalContainer. Other layers in this container
  // are not relevant for this kind of snapshot, so it is safe to exclude all
  // of them.
  aura::Window* modal_container = ash::Shell::GetContainer(
      root_window, ash::kShellWindowId_SystemModalContainer);
  if (modal_container != nullptr)
    excluded_layers.insert(modal_container->layer());

  auto layer_tree_owner = ::wm::RecreateLayersWithClosure(
      root_window, base::BindRepeating(
                       [](LayerSet blocked_layers, LayerSet excluded_layers,
                          ui::LayerOwner* owner) -> std::unique_ptr<ui::Layer> {
                         // Parent layer is excluded meaning that it's pointless
                         // to clone current child and all its descendants. This
                         // reduces the number of layers to create.
                         if (blocked_layers.count(owner->layer()->parent()))
                           return nullptr;
                         if (blocked_layers.count(owner->layer())) {
                           auto layer = base::MakeUnique<ui::Layer>(
                               ui::LayerType::LAYER_SOLID_COLOR);
                           layer->SetBounds(owner->layer()->bounds());
                           layer->SetColor(SK_ColorBLACK);
                           return layer;
                         }
                         if (excluded_layers.count(owner->layer()))
                           return nullptr;
                         return owner->RecreateLayer();
                       },
                       std::move(blocked_layers), std::move(excluded_layers)));

  // layer_tree_owner cannot be null since we are starting off from the root
  // window, which could never be incognito.
  DCHECK(layer_tree_owner);

  auto* root_layer = layer_tree_owner->root();
  root_window->layer()->Add(root_layer);
  root_window->layer()->StackAtBottom(root_layer);
  return layer_tree_owner;
}

void EncodeAndReturnImage(
    const ArcVoiceInteractionFrameworkService::CaptureFullscreenCallback&
        callback,
    std::unique_ptr<ui::LayerTreeOwner> old_layer_owner,
    const gfx::Image& image) {
  old_layer_owner.reset();
  base::PostTaskWithTraitsAndReplyWithResult(
      FROM_HERE,
      base::TaskTraits{base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::Bind(
          [](const gfx::Image& image) -> std::vector<uint8_t> {
            std::vector<uint8_t> res;
            gfx::JPEG1xEncodedDataFromImage(image, 100, &res);
            return res;
          },
          image),
      callback);
}

}  // namespace

// static
const char ArcVoiceInteractionFrameworkService::kArcServiceName[] =
    "arc::ArcVoiceInteractionFrameworkService";

ArcVoiceInteractionFrameworkService::ArcVoiceInteractionFrameworkService(
    ArcBridgeService* bridge_service)
    : ArcService(bridge_service), binding_(this) {
  arc_bridge_service()->voice_interaction_framework()->AddObserver(this);
}

ArcVoiceInteractionFrameworkService::~ArcVoiceInteractionFrameworkService() {
  arc_bridge_service()->voice_interaction_framework()->RemoveObserver(this);
}

void ArcVoiceInteractionFrameworkService::OnInstanceReady() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  mojom::VoiceInteractionFrameworkInstance* framework_instance =
      ARC_GET_INSTANCE_FOR_METHOD(
          arc_bridge_service()->voice_interaction_framework(), Init);
  DCHECK(framework_instance);
  framework_instance->Init(binding_.CreateInterfacePtrAndBind());

  // TODO(updowndota): Move the dynamic shortcuts to accelerator_controller.cc
  // to prevent several issues.
  ash::Shell::Get()->accelerator_controller()->Register(
      {ui::Accelerator(ui::VKEY_A, ui::EF_COMMAND_DOWN)}, this);
  // Temporary shortcut added to enable the metalayer experiment.
  ash::Shell::Get()->accelerator_controller()->Register(
      {ui::Accelerator(ui::VKEY_A, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN)},
      this);
  // Temporary shortcut added for UX/PM exploration.
  ash::Shell::Get()->accelerator_controller()->Register(
      {ui::Accelerator(ui::VKEY_SPACE, ui::EF_COMMAND_DOWN)}, this);
}

void ArcVoiceInteractionFrameworkService::OnInstanceClosed() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  ash::Shell::Get()->accelerator_controller()->UnregisterAll(this);
  if (!metalayer_closed_callback_.is_null())
    base::ResetAndReturn(&metalayer_closed_callback_).Run();
  metalayer_enabled_ = false;
}

bool ArcVoiceInteractionFrameworkService::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (accelerator.IsShiftDown()) {
    // Temporary, used for debugging.
    // Does not take into account or update the palette state.
    mojom::VoiceInteractionFrameworkInstance* framework_instance =
        ARC_GET_INSTANCE_FOR_METHOD(
            arc_bridge_service()->voice_interaction_framework(),
            SetMetalayerVisibility);
    DCHECK(framework_instance);
    framework_instance->SetMetalayerVisibility(true);
  } else {
    mojom::VoiceInteractionFrameworkInstance* framework_instance =
        ARC_GET_INSTANCE_FOR_METHOD(
            arc_bridge_service()->voice_interaction_framework(),
            StartVoiceInteractionSession);
    DCHECK(framework_instance);
    framework_instance->StartVoiceInteractionSession();
  }

  return true;
}

bool ArcVoiceInteractionFrameworkService::CanHandleAccelerators() const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return true;
}

void ArcVoiceInteractionFrameworkService::CaptureFocusedWindow(
    const CaptureFocusedWindowCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  aura::Window* window =
      ash::Shell::Get()->activation_client()->GetActiveWindow();

  if (window == nullptr) {
    callback.Run(std::vector<uint8_t>{});
    return;
  }
  ui::GrabWindowSnapshotAsyncJPEG(
      window, gfx::Rect(window->bounds().size()),
      base::CreateTaskRunnerWithTraits(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING}),
      base::Bind(&ScreenshotCallback, callback));
}

void ArcVoiceInteractionFrameworkService::CaptureFullscreen(
    const CaptureFullscreenCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // Since ARC currently only runs in primary display, we restrict
  // the screenshot to it.
  aura::Window* window = ash::Shell::GetPrimaryRootWindow();
  DCHECK(window);

  auto old_layer_owner = CreateLayerTreeForSnapshot(window);
  ui::GrabLayerSnapshotAsync(
      old_layer_owner->root(), gfx::Rect(window->bounds().size()),
      base::Bind(&EncodeAndReturnImage, callback,
                 base::Passed(std::move(old_layer_owner))));
}

void ArcVoiceInteractionFrameworkService::OnMetalayerClosed() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!metalayer_closed_callback_.is_null())
    base::ResetAndReturn(&metalayer_closed_callback_).Run();
}

void ArcVoiceInteractionFrameworkService::SetMetalayerEnabled(bool enabled) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  metalayer_enabled_ = enabled;
  if (!metalayer_enabled_ && !metalayer_closed_callback_.is_null())
    base::ResetAndReturn(&metalayer_closed_callback_).Run();
}

bool ArcVoiceInteractionFrameworkService::IsMetalayerSupported() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return metalayer_enabled_;
}

void ArcVoiceInteractionFrameworkService::ShowMetalayer(
    const base::Closure& closed) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!metalayer_closed_callback_.is_null()) {
    LOG(ERROR) << "Metalayer is already enabled";
    return;
  }
  metalayer_closed_callback_ = closed;
  SetMetalayerVisibility(true);
}

void ArcVoiceInteractionFrameworkService::HideMetalayer() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (metalayer_closed_callback_.is_null()) {
    LOG(ERROR) << "Metalayer is already hidden";
    return;
  }
  metalayer_closed_callback_ = base::Closure();
  SetMetalayerVisibility(false);
}

void ArcVoiceInteractionFrameworkService::SetMetalayerVisibility(bool visible) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  mojom::VoiceInteractionFrameworkInstance* framework_instance =
      ARC_GET_INSTANCE_FOR_METHOD(
          arc_bridge_service()->voice_interaction_framework(),
          SetMetalayerVisibility);
  if (!framework_instance) {
    if (!metalayer_closed_callback_.is_null())
      base::ResetAndReturn(&metalayer_closed_callback_).Run();
    return;
  }
  framework_instance->SetMetalayerVisibility(visible);
}

}  // namespace arc
