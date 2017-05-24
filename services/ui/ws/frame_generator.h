// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_WS_FRAME_GENERATOR_H_
#define SERVICES_UI_WS_FRAME_GENERATOR_H_

#include <memory>

#include "base/macros.h"
#include "cc/ipc/mojo_compositor_frame_sink.mojom.h"
#include "cc/scheduler/begin_frame_source.h"
#include "cc/surfaces/local_surface_id_allocator.h"
#include "cc/surfaces/surface_id.h"
#include "cc/surfaces/surface_info.h"
#include "services/ui/ws/compositor_frame_sink_client_binding.h"
#include "ui/gfx/geometry/rect.h"

namespace cc {
class RenderPass;
}

namespace ui {
namespace ws {

// Responsible for redrawing the display in response to the redraw requests by
// submitting CompositorFrames to the owned CompositorFrameSink.
class FrameGenerator : public cc::mojom::MojoCompositorFrameSinkClient {
 public:
  FrameGenerator();
  ~FrameGenerator() override;

  void SetDeviceScaleFactor(float device_scale_factor);
  void SetHighContrastMode(bool enabled);

  // Updates the WindowManager's SurfaceInfo.
  void OnSurfaceCreated(const cc::SurfaceInfo& surface_info);

  void OnWindowDamaged();
  void OnWindowSizeChanged(const gfx::Size& pixel_size);
  void Bind(std::unique_ptr<cc::mojom::MojoCompositorFrameSink>
                compositor_frame_sink);

 private:
  // cc::mojom::MojoCompositorFrameSinkClient implementation:
  void DidReceiveCompositorFrameAck(
      const cc::ReturnedResourceArray& resources) override;
  void OnBeginFrame(const cc::BeginFrameArgs& args) override;
  void ReclaimResources(const cc::ReturnedResourceArray& resources) override;

  // Generates the CompositorFrame.
  cc::CompositorFrame GenerateCompositorFrame();

  // DrawWindow creates SurfaceDrawQuad for the window manager and appends it to
  // the provided cc::RenderPass.
  void DrawWindow(cc::RenderPass* pass);

  void SetNeedsBeginFrame(bool needs_begin_frame);

  float device_scale_factor_ = 1.f;
  gfx::Size pixel_size_;

  std::unique_ptr<cc::mojom::MojoCompositorFrameSink> compositor_frame_sink_;
  cc::BeginFrameArgs last_begin_frame_args_;
  cc::BeginFrameAck current_begin_frame_ack_;
  bool high_contrast_mode_enabled_ = false;
  gfx::Size last_submitted_frame_size_;
  cc::LocalSurfaceId local_surface_id_;
  cc::LocalSurfaceIdAllocator id_allocator_;
  float last_device_scale_factor_ = 0.0f;

  cc::SurfaceInfo window_manager_surface_info_;

  DISALLOW_COPY_AND_ASSIGN(FrameGenerator);
};

}  // namespace ws

}  // namespace ui

#endif  // SERVICES_UI_WS_FRAME_GENERATOR_H_
