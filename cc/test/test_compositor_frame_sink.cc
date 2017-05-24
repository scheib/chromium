// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/test_compositor_frame_sink.h"

#include <stdint.h>
#include <utility>

#include "base/memory/ptr_util.h"
#include "base/single_thread_task_runner.h"
#include "cc/output/begin_frame_args.h"
#include "cc/output/compositor_frame_sink_client.h"
#include "cc/output/copy_output_request.h"
#include "cc/output/direct_renderer.h"
#include "cc/output/output_surface.h"
#include "cc/output/texture_mailbox_deleter.h"
#include "cc/surfaces/compositor_frame_sink_support.h"

namespace cc {

static constexpr FrameSinkId kCompositorFrameSinkId(1, 1);

TestCompositorFrameSink::TestCompositorFrameSink(
    scoped_refptr<ContextProvider> compositor_context_provider,
    scoped_refptr<ContextProvider> worker_context_provider,
    SharedBitmapManager* shared_bitmap_manager,
    gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager,
    const RendererSettings& renderer_settings,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    bool synchronous_composite,
    bool disable_display_vsync)
    : CompositorFrameSink(std::move(compositor_context_provider),
                          std::move(worker_context_provider),
                          gpu_memory_buffer_manager,
                          shared_bitmap_manager),
      synchronous_composite_(synchronous_composite),
      disable_display_vsync_(disable_display_vsync),
      renderer_settings_(renderer_settings),
      task_runner_(std::move(task_runner)),
      frame_sink_id_(kCompositorFrameSinkId),
      surface_manager_(new SurfaceManager),
      local_surface_id_allocator_(new LocalSurfaceIdAllocator()),
      external_begin_frame_source_(this),
      weak_ptr_factory_(this) {
  // Always use sync tokens so that code paths in resource provider that deal
  // with sync tokens are tested.
  capabilities_.delegated_sync_points_required = true;
}

TestCompositorFrameSink::~TestCompositorFrameSink() {
  DCHECK(copy_requests_.empty());
}

void TestCompositorFrameSink::RequestCopyOfOutput(
    std::unique_ptr<CopyOutputRequest> request) {
  copy_requests_.push_back(std::move(request));
}

bool TestCompositorFrameSink::BindToClient(CompositorFrameSinkClient* client) {
  if (!CompositorFrameSink::BindToClient(client))
    return false;

  std::unique_ptr<OutputSurface> display_output_surface =
      test_client_->CreateDisplayOutputSurface(context_provider());
  bool display_context_shared_with_compositor =
      display_output_surface->context_provider() == context_provider();

  std::unique_ptr<DisplayScheduler> scheduler;
  if (!synchronous_composite_) {
    if (disable_display_vsync_) {
      begin_frame_source_.reset(new BackToBackBeginFrameSource(
          base::MakeUnique<DelayBasedTimeSource>(task_runner_.get())));
    } else {
      begin_frame_source_.reset(new DelayBasedBeginFrameSource(
          base::MakeUnique<DelayBasedTimeSource>(task_runner_.get())));
      begin_frame_source_->SetAuthoritativeVSyncInterval(
          base::TimeDelta::FromMilliseconds(1000.f /
                                            renderer_settings_.refresh_rate));
    }
    scheduler.reset(new DisplayScheduler(
        task_runner_.get(),
        display_output_surface->capabilities().max_frames_pending));
  }

  display_.reset(
      new Display(shared_bitmap_manager(), gpu_memory_buffer_manager(),
                  renderer_settings_, frame_sink_id_, begin_frame_source_.get(),
                  std::move(display_output_surface), std::move(scheduler),
                  base::MakeUnique<TextureMailboxDeleter>(task_runner_.get())));

  // We want the Display's OutputSurface to hear about lost context, and when
  // this shares a context with it we should not be listening for lost context
  // callbacks on the context here.
  if (display_context_shared_with_compositor && context_provider())
    context_provider()->SetLostContextCallback(base::Closure());

  constexpr bool is_root = false;
  constexpr bool handles_frame_sink_id_invalidation = true;
  constexpr bool needs_sync_points = true;
  support_ = CompositorFrameSinkSupport::Create(
      this, surface_manager_.get(), frame_sink_id_, is_root,
      handles_frame_sink_id_invalidation, needs_sync_points);
  client_->SetBeginFrameSource(&external_begin_frame_source_);

  display_->Initialize(this, surface_manager_.get());
  display_->renderer_for_testing()->SetEnlargePassTextureAmountForTesting(
      enlarge_pass_texture_amount_);
  display_->SetVisible(true);
  return true;
}

void TestCompositorFrameSink::DetachFromClient() {
  client_->SetBeginFrameSource(nullptr);
  support_ = nullptr;
  display_ = nullptr;
  begin_frame_source_ = nullptr;
  local_surface_id_allocator_ = nullptr;
  surface_manager_ = nullptr;
  test_client_ = nullptr;
  CompositorFrameSink::DetachFromClient();
}

void TestCompositorFrameSink::SetLocalSurfaceId(
    const LocalSurfaceId& local_surface_id) {
  test_client_->DisplayReceivedLocalSurfaceId(local_surface_id);
}

void TestCompositorFrameSink::SubmitCompositorFrame(CompositorFrame frame) {
  DCHECK(frame.metadata.begin_frame_ack.has_damage);
  DCHECK_LE(BeginFrameArgs::kStartingFrameNumber,
            frame.metadata.begin_frame_ack.sequence_number);
  test_client_->DisplayReceivedCompositorFrame(frame);

  if (!delegated_local_surface_id_.is_valid()) {
    delegated_local_surface_id_ = local_surface_id_allocator_->GenerateId();
  }
  display_->SetLocalSurfaceId(delegated_local_surface_id_,
                              frame.metadata.device_scale_factor);

  gfx::Size frame_size = frame.render_pass_list.back()->output_rect.size();
  display_->Resize(frame_size);

  support_->SubmitCompositorFrame(delegated_local_surface_id_,
                                  std::move(frame));

  for (std::unique_ptr<CopyOutputRequest>& copy_request : copy_requests_) {
    support_->RequestCopyOfSurface(std::move(copy_request));
  }
  copy_requests_.clear();

  if (!display_->has_scheduler()) {
    display_->DrawAndSwap();
    // Post this to get a new stack frame so that we exit this function before
    // calling the client to tell it that it is done.
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&TestCompositorFrameSink::SendCompositorFrameAckToClient,
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

void TestCompositorFrameSink::DidNotProduceFrame(const BeginFrameAck& ack) {
  DCHECK(!ack.has_damage);
  DCHECK_LE(BeginFrameArgs::kStartingFrameNumber, ack.sequence_number);
  support_->DidNotProduceFrame(ack);
}

void TestCompositorFrameSink::DidReceiveCompositorFrameAck(
    const ReturnedResourceArray& resources) {
  ReclaimResources(resources);
  // In synchronous mode, we manually send acks and this method should not be
  // used.
  if (!display_->has_scheduler())
    return;
  client_->DidReceiveCompositorFrameAck();
}

void TestCompositorFrameSink::OnBeginFrame(const BeginFrameArgs& args) {
  external_begin_frame_source_.OnBeginFrame(args);
}

void TestCompositorFrameSink::ReclaimResources(
    const ReturnedResourceArray& resources) {
  client_->ReclaimResources(resources);
}

void TestCompositorFrameSink::WillDrawSurface(
    const LocalSurfaceId& local_surface_id,
    const gfx::Rect& damage_rect) {}

void TestCompositorFrameSink::DisplayOutputSurfaceLost() {
  client_->DidLoseCompositorFrameSink();
}

void TestCompositorFrameSink::DisplayWillDrawAndSwap(
    bool will_draw_and_swap,
    const RenderPassList& render_passes) {
  test_client_->DisplayWillDrawAndSwap(will_draw_and_swap, render_passes);
}

void TestCompositorFrameSink::DisplayDidDrawAndSwap() {
  test_client_->DisplayDidDrawAndSwap();
}

void TestCompositorFrameSink::OnNeedsBeginFrames(bool needs_begin_frames) {
  support_->SetNeedsBeginFrame(needs_begin_frames);
}

void TestCompositorFrameSink::SendCompositorFrameAckToClient() {
  client_->DidReceiveCompositorFrameAck();
}

}  // namespace cc
