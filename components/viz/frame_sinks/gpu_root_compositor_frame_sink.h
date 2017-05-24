// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_FRAME_SINKS_GPU_ROOT_COMPOSITOR_FRAME_SINK_H_
#define COMPONENTS_VIZ_FRAME_SINKS_GPU_ROOT_COMPOSITOR_FRAME_SINK_H_

#include "cc/ipc/frame_sink_manager.mojom.h"
#include "cc/ipc/mojo_compositor_frame_sink.mojom.h"
#include "cc/surfaces/compositor_frame_sink_support_client.h"
#include "cc/surfaces/display_client.h"
#include "cc/surfaces/local_surface_id.h"
#include "cc/surfaces/surface_id.h"
#include "components/viz/frame_sinks/gpu_compositor_frame_sink_delegate.h"
#include "mojo/public/cpp/bindings/associated_binding.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace cc {
class BeginFrameSource;
class CompositorFrameSinkSupport;
class Display;
class SurfaceManager;
}

namespace viz {

class GpuCompositorFrameSinkDelegate;

class GpuRootCompositorFrameSink
    : public NON_EXPORTED_BASE(cc::CompositorFrameSinkSupportClient),
      public NON_EXPORTED_BASE(cc::mojom::MojoCompositorFrameSink),
      public NON_EXPORTED_BASE(cc::mojom::MojoCompositorFrameSinkPrivate),
      public NON_EXPORTED_BASE(cc::mojom::DisplayPrivate),
      public NON_EXPORTED_BASE(cc::DisplayClient) {
 public:
  GpuRootCompositorFrameSink(
      GpuCompositorFrameSinkDelegate* delegate,
      cc::SurfaceManager* surface_manager,
      const cc::FrameSinkId& frame_sink_id,
      std::unique_ptr<cc::Display> display,
      std::unique_ptr<cc::BeginFrameSource> begin_frame_source,
      cc::mojom::MojoCompositorFrameSinkAssociatedRequest request,
      cc::mojom::MojoCompositorFrameSinkPrivateRequest private_request,
      cc::mojom::MojoCompositorFrameSinkClientPtr client,
      cc::mojom::DisplayPrivateAssociatedRequest display_private_request);

  ~GpuRootCompositorFrameSink() override;

  // cc::mojom::DisplayPrivate:
  void SetDisplayVisible(bool visible) override;
  void ResizeDisplay(const gfx::Size& size) override;
  void SetDisplayColorSpace(const gfx::ColorSpace& color_space) override;
  void SetOutputIsSecure(bool secure) override;
  void SetLocalSurfaceId(const cc::LocalSurfaceId& local_surface_id,
                         float scale_factor) override;

  // cc::mojom::MojoCompositorFrameSink:
  void EvictCurrentSurface() override;
  void SetNeedsBeginFrame(bool needs_begin_frame) override;
  void SubmitCompositorFrame(const cc::LocalSurfaceId& local_surface_id,
                             cc::CompositorFrame frame) override;
  void DidNotProduceFrame(const cc::BeginFrameAck& begin_frame_ack) override;

  // cc::mojom::MojoCompositorFrameSinkPrivate:
  void ClaimTemporaryReference(const cc::SurfaceId& surface_id) override;
  void RequestCopyOfSurface(
      std::unique_ptr<cc::CopyOutputRequest> request) override;

 private:
  // cc::DisplayClient:
  void DisplayOutputSurfaceLost() override;
  void DisplayWillDrawAndSwap(bool will_draw_and_swap,
                              const cc::RenderPassList& render_passes) override;
  void DisplayDidDrawAndSwap() override;

  // cc::CompositorFrameSinkSupportClient:
  void DidReceiveCompositorFrameAck(
      const cc::ReturnedResourceArray& resources) override;
  void OnBeginFrame(const cc::BeginFrameArgs& args) override;
  void ReclaimResources(const cc::ReturnedResourceArray& resources) override;
  void WillDrawSurface(const cc::LocalSurfaceId& local_surface_id,
                       const gfx::Rect& damage_rect) override;

  void OnClientConnectionLost();
  void OnPrivateConnectionLost();

  GpuCompositorFrameSinkDelegate* const delegate_;
  std::unique_ptr<cc::CompositorFrameSinkSupport> support_;

  // GpuRootCompositorFrameSink holds a Display and its BeginFrameSource if
  // it was created with a non-null gpu::SurfaceHandle.
  std::unique_ptr<cc::BeginFrameSource> display_begin_frame_source_;
  std::unique_ptr<cc::Display> display_;

  bool client_connection_lost_ = false;
  bool private_connection_lost_ = false;

  cc::mojom::MojoCompositorFrameSinkClientPtr client_;
  mojo::AssociatedBinding<cc::mojom::MojoCompositorFrameSink>
      compositor_frame_sink_binding_;
  mojo::Binding<cc::mojom::MojoCompositorFrameSinkPrivate>
      compositor_frame_sink_private_binding_;
  mojo::AssociatedBinding<cc::mojom::DisplayPrivate> display_private_binding_;

  DISALLOW_COPY_AND_ASSIGN(GpuRootCompositorFrameSink);
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_FRAME_SINKS_GPU_ROOT_COMPOSITOR_FRAME_SINK_H_
