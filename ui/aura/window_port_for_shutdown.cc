// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/window_port_for_shutdown.h"

#include "base/memory/ptr_util.h"
#include "cc/output/compositor_frame_sink.h"
#include "ui/aura/window.h"

namespace aura {

WindowPortForShutdown::WindowPortForShutdown() {}

WindowPortForShutdown::~WindowPortForShutdown() {}

// static
void WindowPortForShutdown::Install(aura::Window* window) {
  window->port_owner_.reset(new WindowPortForShutdown);
  window->port_ = window->port_owner_.get();
}

void WindowPortForShutdown::OnPreInit(Window* window) {}

void WindowPortForShutdown::OnDeviceScaleFactorChanged(
    float device_scale_factor) {}

void WindowPortForShutdown::OnWillAddChild(Window* child) {}

void WindowPortForShutdown::OnWillRemoveChild(Window* child) {}

void WindowPortForShutdown::OnWillMoveChild(size_t current_index,
                                            size_t dest_index) {}

void WindowPortForShutdown::OnVisibilityChanged(bool visible) {}

void WindowPortForShutdown::OnDidChangeBounds(const gfx::Rect& old_bounds,
                                              const gfx::Rect& new_bounds) {}

std::unique_ptr<ui::PropertyData> WindowPortForShutdown::OnWillChangeProperty(
    const void* key) {
  return nullptr;
}

void WindowPortForShutdown::OnPropertyChanged(
    const void* key,
    int64_t old_value,
    std::unique_ptr<ui::PropertyData> data) {}

std::unique_ptr<cc::CompositorFrameSink>
WindowPortForShutdown::CreateCompositorFrameSink() {
  return nullptr;
}

cc::SurfaceId WindowPortForShutdown::GetSurfaceId() const {
  return cc::SurfaceId();
}

void WindowPortForShutdown::OnWindowAddedToRootWindow() {}

void WindowPortForShutdown::OnWillRemoveWindowFromRootWindow() {}

}  // namespace aura
