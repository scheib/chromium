// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/surfaces/direct_compositor_frame_sink.h"

#include "base/bind.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/compositor_frame_sink_client.h"
#include "cc/surfaces/display.h"
#include "cc/surfaces/frame_sink_id.h"
#include "cc/surfaces/local_surface_id_allocator.h"
#include "cc/surfaces/surface.h"
#include "cc/surfaces/surface_manager.h"

namespace cc {

DirectCompositorFrameSink::DirectCompositorFrameSink(
    const FrameSinkId& frame_sink_id,
    SurfaceManager* surface_manager,
    Display* display,
    scoped_refptr<ContextProvider> context_provider,
    scoped_refptr<ContextProvider> worker_context_provider,
    gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager,
    SharedBitmapManager* shared_bitmap_manager)
    : CompositorFrameSink(std::move(context_provider),
                          std::move(worker_context_provider),
                          gpu_memory_buffer_manager,
                          shared_bitmap_manager),
      frame_sink_id_(frame_sink_id),
      surface_manager_(surface_manager),
      display_(display) {
  DCHECK(thread_checker_.CalledOnValidThread());
  capabilities_.must_always_swap = true;
  // Display and DirectCompositorFrameSink share a GL context, so sync
  // points aren't needed when passing resources between them.
  capabilities_.delegated_sync_points_required = false;
}

DirectCompositorFrameSink::DirectCompositorFrameSink(
    const FrameSinkId& frame_sink_id,
    SurfaceManager* surface_manager,
    Display* display,
    scoped_refptr<VulkanContextProvider> vulkan_context_provider)
    : CompositorFrameSink(std::move(vulkan_context_provider)),
      frame_sink_id_(frame_sink_id),
      surface_manager_(surface_manager),
      display_(display) {
  DCHECK(thread_checker_.CalledOnValidThread());
  capabilities_.must_always_swap = true;
}

DirectCompositorFrameSink::~DirectCompositorFrameSink() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

bool DirectCompositorFrameSink::BindToClient(
    CompositorFrameSinkClient* client) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!CompositorFrameSink::BindToClient(client))
    return false;

  // We want the Display's output surface to hear about lost context, and since
  // this shares a context with it, we should not be listening for lost context
  // callbacks on the context here.
  if (auto* cp = context_provider())
    cp->SetLostContextCallback(base::Closure());

  constexpr bool is_root = true;
  constexpr bool handles_frame_sink_id_invalidation = false;
  support_ = CompositorFrameSinkSupport::Create(
      this, surface_manager_, frame_sink_id_, is_root,
      handles_frame_sink_id_invalidation,
      capabilities_.delegated_sync_points_required);
  begin_frame_source_ = base::MakeUnique<ExternalBeginFrameSource>(this);
  client_->SetBeginFrameSource(begin_frame_source_.get());

  // Avoid initializing GL context here, as this should be sharing the
  // Display's context.
  display_->Initialize(this, surface_manager_);
  return true;
}

void DirectCompositorFrameSink::DetachFromClient() {
  client_->SetBeginFrameSource(nullptr);
  begin_frame_source_.reset();

  // Unregister the SurfaceFactoryClient here instead of the dtor so that only
  // one client is alive for this namespace at any given time.
  support_.reset();

  CompositorFrameSink::DetachFromClient();
}

void DirectCompositorFrameSink::SubmitCompositorFrame(CompositorFrame frame) {
  DCHECK(frame.metadata.begin_frame_ack.has_damage);
  DCHECK_LE(BeginFrameArgs::kStartingFrameNumber,
            frame.metadata.begin_frame_ack.sequence_number);

  gfx::Size frame_size = frame.render_pass_list.back()->output_rect.size();
  if (frame_size.IsEmpty() || frame_size != last_swap_frame_size_) {
    delegated_local_surface_id_ = local_surface_id_allocator_.GenerateId();
    last_swap_frame_size_ = frame_size;
  }
  display_->SetLocalSurfaceId(delegated_local_surface_id_,
                              frame.metadata.device_scale_factor);

  support_->SubmitCompositorFrame(delegated_local_surface_id_,
                                  std::move(frame));
}

void DirectCompositorFrameSink::DidNotProduceFrame(const BeginFrameAck& ack) {
  DCHECK(!ack.has_damage);
  DCHECK_LE(BeginFrameArgs::kStartingFrameNumber, ack.sequence_number);
  support_->DidNotProduceFrame(ack);
}

void DirectCompositorFrameSink::DisplayOutputSurfaceLost() {
  is_lost_ = true;
  client_->DidLoseCompositorFrameSink();
}

void DirectCompositorFrameSink::DisplayWillDrawAndSwap(
    bool will_draw_and_swap,
    const RenderPassList& render_passes) {
  // This notification is not relevant to our client outside of tests.
}

void DirectCompositorFrameSink::DisplayDidDrawAndSwap() {
  // This notification is not relevant to our client outside of tests. We
  // unblock the client from DidDrawCallback() when the surface is going to
  // be drawn.
}

void DirectCompositorFrameSink::DidReceiveCompositorFrameAck(
    const ReturnedResourceArray& resources) {
  client_->ReclaimResources(resources);
  client_->DidReceiveCompositorFrameAck();
}

void DirectCompositorFrameSink::OnBeginFrame(const BeginFrameArgs& args) {
  begin_frame_source_->OnBeginFrame(args);
}

void DirectCompositorFrameSink::ReclaimResources(
    const ReturnedResourceArray& resources) {
  client_->ReclaimResources(resources);
}

void DirectCompositorFrameSink::WillDrawSurface(
    const LocalSurfaceId& local_surface_id,
    const gfx::Rect& damage_rect) {
  // TODO(staraz): Implement this.
}

void DirectCompositorFrameSink::OnNeedsBeginFrames(bool needs_begin_frame) {
  support_->SetNeedsBeginFrame(needs_begin_frame);
}

}  // namespace cc
