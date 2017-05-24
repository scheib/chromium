// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_FRAME_SINKS_GPU_COMPOSITOR_FRAME_SINK_H_
#define COMPONENTS_VIZ_FRAME_SINKS_GPU_COMPOSITOR_FRAME_SINK_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "cc/ipc/mojo_compositor_frame_sink.mojom.h"
#include "cc/surfaces/compositor_frame_sink_support.h"
#include "cc/surfaces/compositor_frame_sink_support_client.h"
#include "cc/surfaces/local_surface_id.h"
#include "cc/surfaces/surface_id.h"
#include "components/viz/frame_sinks/gpu_compositor_frame_sink_delegate.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace viz {

// Server side representation of a WindowSurface.
class GpuCompositorFrameSink
    : public NON_EXPORTED_BASE(cc::CompositorFrameSinkSupportClient),
      public NON_EXPORTED_BASE(cc::mojom::MojoCompositorFrameSink),
      public NON_EXPORTED_BASE(cc::mojom::MojoCompositorFrameSinkPrivate) {
 public:
  GpuCompositorFrameSink(
      GpuCompositorFrameSinkDelegate* delegate,
      cc::SurfaceManager* surface_manager,
      const cc::FrameSinkId& frame_sink_id,
      cc::mojom::MojoCompositorFrameSinkRequest request,
      cc::mojom::MojoCompositorFrameSinkPrivateRequest private_request,
      cc::mojom::MojoCompositorFrameSinkClientPtr client);

  ~GpuCompositorFrameSink() override;

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
  // cc::CompositorFrameSinkSupportClient implementation:
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

  bool client_connection_lost_ = false;
  bool private_connection_lost_ = false;

  cc::mojom::MojoCompositorFrameSinkClientPtr client_;
  mojo::Binding<cc::mojom::MojoCompositorFrameSink>
      compositor_frame_sink_binding_;
  mojo::Binding<cc::mojom::MojoCompositorFrameSinkPrivate>
      compositor_frame_sink_private_binding_;

  DISALLOW_COPY_AND_ASSIGN(GpuCompositorFrameSink);
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_FRAME_SINKS_GPU_COMPOSITOR_FRAME_SINK_H_
