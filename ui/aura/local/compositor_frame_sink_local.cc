// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/local/compositor_frame_sink_local.h"

#include "cc/output/compositor_frame_sink_client.h"
#include "cc/surfaces/compositor_frame_sink_support.h"
#include "ui/aura/client/cursor_client.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"

namespace aura {

CompositorFrameSinkLocal::CompositorFrameSinkLocal(
    const cc::FrameSinkId& frame_sink_id,
    cc::SurfaceManager* surface_manager)
    : cc::CompositorFrameSink(nullptr, nullptr, nullptr, nullptr),
      frame_sink_id_(frame_sink_id),
      surface_manager_(surface_manager) {}

CompositorFrameSinkLocal::~CompositorFrameSinkLocal() {}

bool CompositorFrameSinkLocal::BindToClient(
    cc::CompositorFrameSinkClient* client) {
  if (!cc::CompositorFrameSink::BindToClient(client))
    return false;
  DCHECK(!thread_checker_);
  thread_checker_ = base::MakeUnique<base::ThreadChecker>();

  support_ = cc::CompositorFrameSinkSupport::Create(
      this, surface_manager_, frame_sink_id_, false /* is_root */,
      true /* handles_frame_sink_id_invalidation */,
      true /* needs_sync_points */);
  begin_frame_source_ = base::MakeUnique<cc::ExternalBeginFrameSource>(this);
  client->SetBeginFrameSource(begin_frame_source_.get());
  return true;
}

void CompositorFrameSinkLocal::SetSurfaceChangedCallback(
    const SurfaceChangedCallback& callback) {
  DCHECK(!surface_changed_callback_);
  surface_changed_callback_ = callback;
}

void CompositorFrameSinkLocal::DetachFromClient() {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  client_->SetBeginFrameSource(nullptr);
  begin_frame_source_.reset();
  support_->EvictCurrentSurface();
  support_.reset();
  thread_checker_.reset();
  cc::CompositorFrameSink::DetachFromClient();
}

void CompositorFrameSinkLocal::SubmitCompositorFrame(
    cc::CompositorFrame frame) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  DCHECK(frame.metadata.begin_frame_ack.has_damage);
  DCHECK_LE(cc::BeginFrameArgs::kStartingFrameNumber,
            frame.metadata.begin_frame_ack.sequence_number);

  cc::LocalSurfaceId old_local_surface_id = local_surface_id_;
  if (!frame.render_pass_list.empty()) {
    const auto& frame_size = frame.render_pass_list.back()->output_rect.size();
    if (frame_size != last_submitted_frame_size_ ||
        !local_surface_id_.is_valid()) {
      last_submitted_frame_size_ = frame_size;
      local_surface_id_ = id_allocator_.GenerateId();
    }
  }
  support_->SubmitCompositorFrame(local_surface_id_, std::move(frame));

  if (local_surface_id_ != old_local_surface_id) {
    surface_changed_callback_.Run(
        cc::SurfaceId(frame_sink_id_, local_surface_id_),
        last_submitted_frame_size_);
  }
}

void CompositorFrameSinkLocal::DidNotProduceFrame(
    const cc::BeginFrameAck& ack) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  DCHECK(!ack.has_damage);
  DCHECK_LE(cc::BeginFrameArgs::kStartingFrameNumber, ack.sequence_number);
  support_->DidNotProduceFrame(ack);
}

void CompositorFrameSinkLocal::DidReceiveCompositorFrameAck(
    const cc::ReturnedResourceArray& resources) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  if (!client_)
    return;
  if (!resources.empty())
    client_->ReclaimResources(resources);
  client_->DidReceiveCompositorFrameAck();
}

void CompositorFrameSinkLocal::OnBeginFrame(const cc::BeginFrameArgs& args) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  begin_frame_source_->OnBeginFrame(args);
}

void CompositorFrameSinkLocal::ReclaimResources(
    const cc::ReturnedResourceArray& resources) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  if (!client_)
    return;
  client_->ReclaimResources(resources);
}

void CompositorFrameSinkLocal::OnNeedsBeginFrames(bool needs_begin_frames) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  support_->SetNeedsBeginFrame(needs_begin_frames);
}

}  // namespace aura
