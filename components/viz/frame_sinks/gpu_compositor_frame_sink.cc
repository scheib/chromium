// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/frame_sinks/gpu_compositor_frame_sink.h"

namespace viz {

GpuCompositorFrameSink::GpuCompositorFrameSink(
    GpuCompositorFrameSinkDelegate* delegate,
    cc::SurfaceManager* surface_manager,
    const cc::FrameSinkId& frame_sink_id,
    cc::mojom::MojoCompositorFrameSinkRequest request,
    cc::mojom::MojoCompositorFrameSinkPrivateRequest
        compositor_frame_sink_private_request,
    cc::mojom::MojoCompositorFrameSinkClientPtr client)
    : delegate_(delegate),
      support_(cc::CompositorFrameSinkSupport::Create(
          this,
          surface_manager,
          frame_sink_id,
          false /* is_root */,
          true /* handles_frame_sink_id_invalidation */,
          true /* needs_sync_points */)),
      client_(std::move(client)),
      compositor_frame_sink_binding_(this, std::move(request)),
      compositor_frame_sink_private_binding_(
          this,
          std::move(compositor_frame_sink_private_request)) {
  compositor_frame_sink_binding_.set_connection_error_handler(base::Bind(
      &GpuCompositorFrameSink::OnClientConnectionLost, base::Unretained(this)));
  compositor_frame_sink_private_binding_.set_connection_error_handler(
      base::Bind(&GpuCompositorFrameSink::OnPrivateConnectionLost,
                 base::Unretained(this)));
}

GpuCompositorFrameSink::~GpuCompositorFrameSink() {}

void GpuCompositorFrameSink::EvictCurrentSurface() {
  support_->EvictCurrentSurface();
}

void GpuCompositorFrameSink::SetNeedsBeginFrame(bool needs_begin_frame) {
  support_->SetNeedsBeginFrame(needs_begin_frame);
}

void GpuCompositorFrameSink::SubmitCompositorFrame(
    const cc::LocalSurfaceId& local_surface_id,
    cc::CompositorFrame frame) {
  support_->SubmitCompositorFrame(local_surface_id, std::move(frame));
}

void GpuCompositorFrameSink::DidNotProduceFrame(
    const cc::BeginFrameAck& begin_frame_ack) {
  support_->DidNotProduceFrame(begin_frame_ack);
}

void GpuCompositorFrameSink::DidReceiveCompositorFrameAck(
    const cc::ReturnedResourceArray& resources) {
  if (client_)
    client_->DidReceiveCompositorFrameAck(resources);
}

void GpuCompositorFrameSink::ClaimTemporaryReference(
    const cc::SurfaceId& surface_id) {
  support_->ClaimTemporaryReference(surface_id);
}

void GpuCompositorFrameSink::RequestCopyOfSurface(
    std::unique_ptr<cc::CopyOutputRequest> request) {
  support_->RequestCopyOfSurface(std::move(request));
}

void GpuCompositorFrameSink::OnBeginFrame(const cc::BeginFrameArgs& args) {
  if (client_)
    client_->OnBeginFrame(args);
}

void GpuCompositorFrameSink::ReclaimResources(
    const cc::ReturnedResourceArray& resources) {
  if (client_)
    client_->ReclaimResources(resources);
}

void GpuCompositorFrameSink::WillDrawSurface(
    const cc::LocalSurfaceId& local_surface_id,
    const gfx::Rect& damage_rect) {}

void GpuCompositorFrameSink::OnClientConnectionLost() {
  client_connection_lost_ = true;
  // Request destruction of |this| only if both connections are lost.
  delegate_->OnClientConnectionLost(support_->frame_sink_id(),
                                    private_connection_lost_);
}

void GpuCompositorFrameSink::OnPrivateConnectionLost() {
  private_connection_lost_ = true;
  // Request destruction of |this| only if both connections are lost.
  delegate_->OnPrivateConnectionLost(support_->frame_sink_id(),
                                     client_connection_lost_);
}

}  // namespace viz
