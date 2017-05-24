// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/gpu/renderer_compositor_frame_sink.h"

#include <utility>

#include "base/command_line.h"
#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/compositor_frame_sink_client.h"
#include "cc/output/managed_memory_policy.h"
#include "content/common/view_messages.h"
#include "content/public/common/content_switches.h"
#include "content/renderer/gpu/frame_swap_message_queue.h"
#include "content/renderer/render_thread_impl.h"
#include "gpu/command_buffer/client/context_support.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/ipc/client/command_buffer_proxy_impl.h"
#include "ipc/ipc_sync_channel.h"
#include "services/ui/public/cpp/gpu/context_provider_command_buffer.h"

namespace content {

RendererCompositorFrameSink::RendererCompositorFrameSink(
    int32_t routing_id,
    std::unique_ptr<cc::SyntheticBeginFrameSource> synthetic_begin_frame_source,
    scoped_refptr<cc::ContextProvider> context_provider,
    scoped_refptr<cc::ContextProvider> worker_context_provider,
    gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager,
    cc::SharedBitmapManager* shared_bitmap_manager,
    scoped_refptr<FrameSwapMessageQueue> swap_frame_message_queue)
    : CompositorFrameSink(std::move(context_provider),
                          std::move(worker_context_provider),
                          gpu_memory_buffer_manager,
                          shared_bitmap_manager),
      compositor_frame_sink_filter_(
          RenderThreadImpl::current()->compositor_message_filter()),
      message_sender_(RenderThreadImpl::current()->sync_message_filter()),
      frame_swap_message_queue_(swap_frame_message_queue),
      synthetic_begin_frame_source_(std::move(synthetic_begin_frame_source)),
      external_begin_frame_source_(
          synthetic_begin_frame_source_
              ? nullptr
              : base::MakeUnique<cc::ExternalBeginFrameSource>(this)),
      routing_id_(routing_id),
      sink_client_binding_(this) {
  DCHECK(compositor_frame_sink_filter_);
  DCHECK(frame_swap_message_queue_);
  DCHECK(message_sender_);
  thread_checker_.DetachFromThread();
  EstablishMojoConnection();
}

RendererCompositorFrameSink::RendererCompositorFrameSink(
    int32_t routing_id,
    std::unique_ptr<cc::SyntheticBeginFrameSource> synthetic_begin_frame_source,
    scoped_refptr<cc::VulkanContextProvider> vulkan_context_provider,
    scoped_refptr<FrameSwapMessageQueue> swap_frame_message_queue)
    : CompositorFrameSink(std::move(vulkan_context_provider)),
      compositor_frame_sink_filter_(
          RenderThreadImpl::current()->compositor_message_filter()),
      message_sender_(RenderThreadImpl::current()->sync_message_filter()),
      frame_swap_message_queue_(swap_frame_message_queue),
      synthetic_begin_frame_source_(std::move(synthetic_begin_frame_source)),
      external_begin_frame_source_(
          synthetic_begin_frame_source_
              ? nullptr
              : base::MakeUnique<cc::ExternalBeginFrameSource>(this)),
      routing_id_(routing_id),
      sink_client_binding_(this) {
  DCHECK(compositor_frame_sink_filter_);
  DCHECK(frame_swap_message_queue_);
  DCHECK(message_sender_);
  thread_checker_.DetachFromThread();
  EstablishMojoConnection();
}

RendererCompositorFrameSink::~RendererCompositorFrameSink() = default;

bool RendererCompositorFrameSink::BindToClient(
    cc::CompositorFrameSinkClient* client) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!cc::CompositorFrameSink::BindToClient(client))
    return false;

  sink_.Bind(std::move(sink_ptr_info_));
  sink_client_binding_.Bind(std::move(sink_client_request_));

  if (synthetic_begin_frame_source_)
    client_->SetBeginFrameSource(synthetic_begin_frame_source_.get());
  else
    client_->SetBeginFrameSource(external_begin_frame_source_.get());

  compositor_frame_sink_proxy_ = new RendererCompositorFrameSinkProxy(this);
  compositor_frame_sink_filter_handler_ =
      base::Bind(&RendererCompositorFrameSinkProxy::OnMessageReceived,
                 compositor_frame_sink_proxy_);
  compositor_frame_sink_filter_->AddHandlerOnCompositorThread(
      routing_id_, compositor_frame_sink_filter_handler_);
  return true;
}

void RendererCompositorFrameSink::DetachFromClient() {
  DCHECK(thread_checker_.CalledOnValidThread());
  client_->SetBeginFrameSource(nullptr);
  // Destroy the begin frame source on the same thread it was bound on.
  // The CompositorFrameSink itself is destroyed on the main thread.
  external_begin_frame_source_ = nullptr;
  synthetic_begin_frame_source_ = nullptr;
  compositor_frame_sink_proxy_->ClearCompositorFrameSink();
  compositor_frame_sink_filter_->RemoveHandlerOnCompositorThread(
      routing_id_, compositor_frame_sink_filter_handler_);
  sink_.reset();
  sink_client_binding_.Close();
  cc::CompositorFrameSink::DetachFromClient();
}

void RendererCompositorFrameSink::SubmitCompositorFrame(
    cc::CompositorFrame frame) {
  // We should only submit CompositorFrames with valid BeginFrameAcks.
  DCHECK(frame.metadata.begin_frame_ack.has_damage);
  DCHECK_LE(cc::BeginFrameArgs::kStartingFrameNumber,
            frame.metadata.begin_frame_ack.sequence_number);
  auto new_surface_properties =
      RenderWidgetSurfaceProperties::FromCompositorFrame(frame);
  if (!local_surface_id_.is_valid() ||
      new_surface_properties != current_surface_properties_) {
    local_surface_id_ = id_allocator_.GenerateId();
    current_surface_properties_ = new_surface_properties;
  }

  {
    std::unique_ptr<FrameSwapMessageQueue::SendMessageScope>
        send_message_scope =
            frame_swap_message_queue_->AcquireSendMessageScope();
    std::vector<std::unique_ptr<IPC::Message>> messages;
    frame_swap_message_queue_->DrainMessages(&messages);
    std::vector<IPC::Message> messages_to_send;
    FrameSwapMessageQueue::TransferMessages(&messages, &messages_to_send);
    uint32_t frame_token = 0;
    if (!messages_to_send.empty())
      frame_token = frame_swap_message_queue_->AllocateFrameToken();
    frame.metadata.frame_token = frame_token;
    sink_->SubmitCompositorFrame(local_surface_id_, std::move(frame));
    if (frame_token) {
      message_sender_->Send(new ViewHostMsg_FrameSwapMessages(
          routing_id_, frame_token, messages_to_send));
    }
    // ~send_message_scope.
  }
}

void RendererCompositorFrameSink::DidNotProduceFrame(
    const cc::BeginFrameAck& ack) {
  DCHECK(!ack.has_damage);
  DCHECK_LE(cc::BeginFrameArgs::kStartingFrameNumber, ack.sequence_number);
  sink_->DidNotProduceFrame(ack);
}

void RendererCompositorFrameSink::OnMessageReceived(
    const IPC::Message& message) {
  DCHECK(thread_checker_.CalledOnValidThread());
  IPC_BEGIN_MESSAGE_MAP(RendererCompositorFrameSink, message)
    IPC_MESSAGE_HANDLER(ViewMsg_BeginFrame, OnBeginFrameIPC)
  IPC_END_MESSAGE_MAP()
}

void RendererCompositorFrameSink::OnBeginFrameIPC(
    const cc::BeginFrameArgs& args) {
  if (external_begin_frame_source_)
    external_begin_frame_source_->OnBeginFrame(args);
}

void RendererCompositorFrameSink::DidReceiveCompositorFrameAck(
    const cc::ReturnedResourceArray& resources) {
  ReclaimResources(resources);
  client_->DidReceiveCompositorFrameAck();
}

void RendererCompositorFrameSink::OnBeginFrame(const cc::BeginFrameArgs& args) {
  // See crbug.com/709689.
  NOTREACHED() << "BeginFrames are delivered using Chrome IPC.";
}

void RendererCompositorFrameSink::ReclaimResources(
    const cc::ReturnedResourceArray& resources) {
  client_->ReclaimResources(resources);
}

void RendererCompositorFrameSink::OnNeedsBeginFrames(bool needs_begin_frames) {
  sink_->SetNeedsBeginFrame(needs_begin_frames);
}

void RendererCompositorFrameSink::EstablishMojoConnection() {
  cc::mojom::MojoCompositorFrameSinkPtr sink;
  cc::mojom::MojoCompositorFrameSinkRequest sink_request =
      mojo::MakeRequest(&sink);
  cc::mojom::MojoCompositorFrameSinkClientPtr sink_client;
  sink_client_request_ = mojo::MakeRequest(&sink_client);
  RenderThreadImpl::current()->GetFrameSinkProvider()->CreateForWidget(
      routing_id_, std::move(sink_request), std::move(sink_client));
  sink_ptr_info_ = sink.PassInterface();
}

}  // namespace content
