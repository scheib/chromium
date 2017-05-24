// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/public/cpp/client_compositor_frame_sink.h"

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "cc/output/begin_frame_args.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/compositor_frame_sink_client.h"

namespace ui {

// static
std::unique_ptr<ClientCompositorFrameSink> ClientCompositorFrameSink::Create(
    scoped_refptr<cc::ContextProvider> context_provider,
    gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager,
    std::unique_ptr<ClientCompositorFrameSinkBinding>*
        compositor_frame_sink_binding,
    bool enable_surface_synchronization) {
  cc::mojom::MojoCompositorFrameSinkPtr compositor_frame_sink;
  cc::mojom::MojoCompositorFrameSinkClientPtr compositor_frame_sink_client;
  cc::mojom::MojoCompositorFrameSinkClientRequest
      compositor_frame_sink_client_request =
          MakeRequest(&compositor_frame_sink_client);

  compositor_frame_sink_binding->reset(new ClientCompositorFrameSinkBinding(
      MakeRequest(&compositor_frame_sink),
      compositor_frame_sink_client.PassInterface()));
  return base::WrapUnique(new ClientCompositorFrameSink(
      std::move(context_provider), gpu_memory_buffer_manager,
      compositor_frame_sink.PassInterface(),
      std::move(compositor_frame_sink_client_request),
      enable_surface_synchronization));
}

ClientCompositorFrameSink::~ClientCompositorFrameSink() {}

bool ClientCompositorFrameSink::BindToClient(
    cc::CompositorFrameSinkClient* client) {
  if (!cc::CompositorFrameSink::BindToClient(client))
    return false;

  DCHECK(!thread_checker_);
  thread_checker_.reset(new base::ThreadChecker());
  compositor_frame_sink_.Bind(std::move(compositor_frame_sink_info_));
  client_binding_.reset(
      new mojo::Binding<cc::mojom::MojoCompositorFrameSinkClient>(
          this, std::move(client_request_)));

  begin_frame_source_ = base::MakeUnique<cc::ExternalBeginFrameSource>(this);

  client->SetBeginFrameSource(begin_frame_source_.get());
  return true;
}

void ClientCompositorFrameSink::DetachFromClient() {
  client_->SetBeginFrameSource(nullptr);
  begin_frame_source_.reset();
  client_binding_.reset();
  compositor_frame_sink_.reset();
  cc::CompositorFrameSink::DetachFromClient();
}

void ClientCompositorFrameSink::SetLocalSurfaceId(
    const cc::LocalSurfaceId& local_surface_id) {
  DCHECK(local_surface_id.is_valid());
  DCHECK(enable_surface_synchronization_);
  local_surface_id_ = local_surface_id;
}

void ClientCompositorFrameSink::SubmitCompositorFrame(
    cc::CompositorFrame frame) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  if (!compositor_frame_sink_)
    return;

  DCHECK(frame.metadata.begin_frame_ack.has_damage);
  DCHECK_LE(cc::BeginFrameArgs::kStartingFrameNumber,
            frame.metadata.begin_frame_ack.sequence_number);

  gfx::Size frame_size = frame.render_pass_list.back()->output_rect.size();
  if (!local_surface_id_.is_valid() ||
      frame_size != last_submitted_frame_size_) {
    last_submitted_frame_size_ = frame_size;
    if (!enable_surface_synchronization_)
      local_surface_id_ = id_allocator_.GenerateId();
  }
  compositor_frame_sink_->SubmitCompositorFrame(local_surface_id_,
                                                std::move(frame));
}

void ClientCompositorFrameSink::DidNotProduceFrame(
    const cc::BeginFrameAck& ack) {
  DCHECK(!ack.has_damage);
  DCHECK_LE(cc::BeginFrameArgs::kStartingFrameNumber, ack.sequence_number);
  compositor_frame_sink_->DidNotProduceFrame(ack);
}

ClientCompositorFrameSink::ClientCompositorFrameSink(
    scoped_refptr<cc::ContextProvider> context_provider,
    gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager,
    cc::mojom::MojoCompositorFrameSinkPtrInfo compositor_frame_sink_info,
    cc::mojom::MojoCompositorFrameSinkClientRequest client_request,
    bool enable_surface_synchronization)
    : cc::CompositorFrameSink(std::move(context_provider),
                              nullptr,
                              gpu_memory_buffer_manager,
                              nullptr),
      compositor_frame_sink_info_(std::move(compositor_frame_sink_info)),
      client_request_(std::move(client_request)),
      enable_surface_synchronization_(enable_surface_synchronization) {}

void ClientCompositorFrameSink::DidReceiveCompositorFrameAck(
    const cc::ReturnedResourceArray& resources) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  if (!client_)
    return;
  client_->ReclaimResources(resources);
  client_->DidReceiveCompositorFrameAck();
}

void ClientCompositorFrameSink::OnBeginFrame(
    const cc::BeginFrameArgs& begin_frame_args) {
  begin_frame_source_->OnBeginFrame(begin_frame_args);
}

void ClientCompositorFrameSink::ReclaimResources(
    const cc::ReturnedResourceArray& resources) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  if (!client_)
    return;
  client_->ReclaimResources(resources);
}

void ClientCompositorFrameSink::OnNeedsBeginFrames(bool needs_begin_frames) {
  compositor_frame_sink_->SetNeedsBeginFrame(needs_begin_frames);
}

ClientCompositorFrameSinkBinding::~ClientCompositorFrameSinkBinding() {}

ClientCompositorFrameSinkBinding::ClientCompositorFrameSinkBinding(
    cc::mojom::MojoCompositorFrameSinkRequest compositor_frame_sink_request,
    cc::mojom::MojoCompositorFrameSinkClientPtrInfo
        compositor_frame_sink_client)
    : compositor_frame_sink_request_(std::move(compositor_frame_sink_request)),
      compositor_frame_sink_client_(std::move(compositor_frame_sink_client)) {}

cc::mojom::MojoCompositorFrameSinkRequest
ClientCompositorFrameSinkBinding::TakeFrameSinkRequest() {
  return std::move(compositor_frame_sink_request_);
}

cc::mojom::MojoCompositorFrameSinkClientPtrInfo
ClientCompositorFrameSinkBinding::TakeFrameSinkClient() {
  return std::move(compositor_frame_sink_client_);
}

}  // namespace ui
