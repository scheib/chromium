// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/surfaces/compositor_frame_sink_support.h"

#include <algorithm>
#include <utility>

#include "cc/output/compositor_frame.h"
#include "cc/scheduler/begin_frame_source.h"
#include "cc/surfaces/compositor_frame_sink_support_client.h"
#include "cc/surfaces/display.h"
#include "cc/surfaces/surface.h"
#include "cc/surfaces/surface_info.h"
#include "cc/surfaces/surface_manager.h"
#include "cc/surfaces/surface_reference.h"

namespace cc {

// static
std::unique_ptr<CompositorFrameSinkSupport> CompositorFrameSinkSupport::Create(
    CompositorFrameSinkSupportClient* client,
    SurfaceManager* surface_manager,
    const FrameSinkId& frame_sink_id,
    bool is_root,
    bool handles_frame_sink_id_invalidation,
    bool needs_sync_points) {
  std::unique_ptr<CompositorFrameSinkSupport> support =
      base::WrapUnique(new CompositorFrameSinkSupport(
          client, frame_sink_id, is_root, handles_frame_sink_id_invalidation,
          needs_sync_points));
  support->Init(surface_manager);
  return support;
}

CompositorFrameSinkSupport::~CompositorFrameSinkSupport() {
  // Unregister |this| as a BeginFrameObserver so that the BeginFrameSource does
  // not call into |this| after it's deleted.
  SetNeedsBeginFrame(false);

  // For display root surfaces, the surface is no longer going to be visible
  // so make it unreachable from the top-level root.
  if (surface_manager_->using_surface_references() && is_root_ &&
      reference_tracker_.current_surface_id().is_valid())
    RemoveTopLevelRootReference(reference_tracker_.current_surface_id());

  EvictCurrentSurface();
  surface_manager_->UnregisterFrameSinkManagerClient(frame_sink_id_);
  if (handles_frame_sink_id_invalidation_)
    surface_manager_->InvalidateFrameSinkId(frame_sink_id_);
}

void CompositorFrameSinkSupport::ReturnResources(
    const ReturnedResourceArray& resources) {
  if (resources.empty())
    return;
  if (!ack_pending_count_ && client_) {
    client_->ReclaimResources(resources);
    return;
  }

  std::copy(resources.begin(), resources.end(),
            std::back_inserter(surface_returned_resources_));
}

void CompositorFrameSinkSupport::SetBeginFrameSource(
    BeginFrameSource* begin_frame_source) {
  if (begin_frame_source_ && added_frame_observer_) {
    begin_frame_source_->RemoveObserver(this);
    added_frame_observer_ = false;
  }
  begin_frame_source_ = begin_frame_source;
  UpdateNeedsBeginFramesInternal();
}

void CompositorFrameSinkSupport::EvictCurrentSurface() {
  if (!current_surface_)
    return;
  DestroyCurrentSurface();
}

void CompositorFrameSinkSupport::SetNeedsBeginFrame(bool needs_begin_frame) {
  needs_begin_frame_ = needs_begin_frame;
  UpdateNeedsBeginFramesInternal();
}

void CompositorFrameSinkSupport::DidNotProduceFrame(const BeginFrameAck& ack) {
  // TODO(eseckler): While a pending CompositorFrame exists (see TODO below), we
  // should not acknowledge immediately. Instead, we should update the ack that
  // will be sent to DisplayScheduler when the pending frame is activated.
  DCHECK_GE(ack.sequence_number, BeginFrameArgs::kStartingFrameNumber);

  // |has_damage| is not transmitted, but false by default.
  DCHECK(!ack.has_damage);
  if (begin_frame_source_)
    begin_frame_source_->DidFinishFrame(this, ack);
}

void CompositorFrameSinkSupport::SubmitCompositorFrame(
    const LocalSurfaceId& local_surface_id,
    CompositorFrame frame) {
  TRACE_EVENT0("cc", "CompositorFrameSinkSupport::SubmitCompositorFrame");
  DCHECK(local_surface_id.is_valid());
  DCHECK_GE(frame.metadata.begin_frame_ack.sequence_number,
            BeginFrameArgs::kStartingFrameNumber);
  DCHECK(!frame.render_pass_list.empty());

  ++ack_pending_count_;

  // |has_damage| is not transmitted.
  frame.metadata.begin_frame_ack.has_damage = true;

  BeginFrameAck ack = frame.metadata.begin_frame_ack;

  if (!ui::LatencyInfo::Verify(frame.metadata.latency_info,
                               "RenderWidgetHostImpl::OnSwapCompositorFrame")) {
    std::vector<ui::LatencyInfo>().swap(frame.metadata.latency_info);
  }
  for (ui::LatencyInfo& latency : frame.metadata.latency_info) {
    if (latency.latency_components().size() > 0) {
      latency.AddLatencyNumber(ui::DISPLAY_COMPOSITOR_RECEIVED_FRAME_COMPONENT,
                               0, 0);
    }
  }

  std::unique_ptr<Surface> surface;
  bool create_new_surface =
      (!current_surface_ ||
       local_surface_id != current_surface_->surface_id().local_surface_id());
  if (!create_new_surface) {
    surface = std::move(current_surface_);
  } else {
    surface = CreateSurface(local_surface_id);
  }

  surface->QueueFrame(
      std::move(frame),
      base::Bind(&CompositorFrameSinkSupport::DidReceiveCompositorFrameAck,
                 weak_factory_.GetWeakPtr()),
      base::BindRepeating(&CompositorFrameSinkSupport::WillDrawSurface,
                          weak_factory_.GetWeakPtr()));

  if (current_surface_) {
    surface->SetPreviousFrameSurface(current_surface_.get());
    DestroyCurrentSurface();
  }
  current_surface_ = std::move(surface);

  // TODO(eseckler): The CompositorFrame submitted below might not be activated
  // right away b/c of surface synchronization. We should only send the
  // BeginFrameAck to DisplayScheduler when it is activated. This also means
  // that we need to stay an active BFO while a CompositorFrame is pending.
  // See https://crbug.com/703079.
  if (begin_frame_source_)
    begin_frame_source_->DidFinishFrame(this, ack);
}

void CompositorFrameSinkSupport::UpdateSurfaceReferences(
    const SurfaceId& last_surface_id,
    const LocalSurfaceId& local_surface_id) {
  const bool surface_id_changed =
      last_surface_id.local_surface_id() != local_surface_id;

  // If this is a display root surface and the SurfaceId is changing, make the
  // new SurfaceId reachable from the top-level root.
  if (is_root_ && surface_id_changed)
    AddTopLevelRootReference(reference_tracker_.current_surface_id());

  // Add references based on CompositorFrame referenced surfaces. If the
  // SurfaceId has changed all referenced surfaces will be in this list.
  if (!reference_tracker_.references_to_add().empty()) {
    surface_manager_->AddSurfaceReferences(
        reference_tracker_.references_to_add());
  }

  // If this is a display root surface and the SurfaceId is changing, make the
  // old SurfaceId unreachable from the top-level root. This needs to happen
  // after adding all references for the new SurfaceId.
  if (is_root_ && surface_id_changed && last_surface_id.is_valid())
    RemoveTopLevelRootReference(last_surface_id);

  // Remove references based on CompositorFrame referenced surfaces. If the
  // SurfaceId has changed this list will be empty.
  if (!reference_tracker_.references_to_remove().empty()) {
    DCHECK(!surface_id_changed);
    surface_manager_->RemoveSurfaceReferences(
        reference_tracker_.references_to_remove());
  }
}

void CompositorFrameSinkSupport::AddTopLevelRootReference(
    const SurfaceId& surface_id) {
  SurfaceReference reference(surface_manager_->GetRootSurfaceId(), surface_id);
  surface_manager_->AddSurfaceReferences({reference});
}

void CompositorFrameSinkSupport::RemoveTopLevelRootReference(
    const SurfaceId& surface_id) {
  SurfaceReference reference(surface_manager_->GetRootSurfaceId(), surface_id);
  surface_manager_->RemoveSurfaceReferences({reference});
}

void CompositorFrameSinkSupport::ReferencedSurfacesChanged(
    const LocalSurfaceId& local_surface_id,
    const std::vector<SurfaceId>* active_referenced_surfaces) {
  if (!surface_manager_->using_surface_references())
    return;

  SurfaceId last_surface_id = reference_tracker_.current_surface_id();

  // Populate list of surface references to add and remove based on reference
  // surfaces in current frame compared with the last frame. The list of
  // surface references includes references from both the pending and active
  // frame if any.
  reference_tracker_.UpdateReferences(local_surface_id,
                                      active_referenced_surfaces);

  UpdateSurfaceReferences(last_surface_id, local_surface_id);
}

void CompositorFrameSinkSupport::DidReceiveCompositorFrameAck() {
  DCHECK_GT(ack_pending_count_, 0);
  ack_pending_count_--;
  if (!client_)
    return;

  client_->DidReceiveCompositorFrameAck(surface_returned_resources_);
  surface_returned_resources_.clear();
}

void CompositorFrameSinkSupport::WillDrawSurface(
    const LocalSurfaceId& local_surface_id,
    const gfx::Rect& damage_rect) {
  if (client_)
    client_->WillDrawSurface(local_surface_id, damage_rect);
}

void CompositorFrameSinkSupport::ClaimTemporaryReference(
    const SurfaceId& surface_id) {
  surface_manager_->AssignTemporaryReference(surface_id, frame_sink_id_);
}

void CompositorFrameSinkSupport::ReceiveFromChild(
    const TransferableResourceArray& resources) {
  surface_resource_holder_.ReceiveFromChild(resources);
}

void CompositorFrameSinkSupport::RefResources(
    const TransferableResourceArray& resources) {
  surface_resource_holder_.RefResources(resources);
}

void CompositorFrameSinkSupport::UnrefResources(
    const ReturnedResourceArray& resources) {
  surface_resource_holder_.UnrefResources(resources);
}

CompositorFrameSinkSupport::CompositorFrameSinkSupport(
    CompositorFrameSinkSupportClient* client,
    const FrameSinkId& frame_sink_id,
    bool is_root,
    bool handles_frame_sink_id_invalidation,
    bool needs_sync_points)
    : client_(client),
      frame_sink_id_(frame_sink_id),
      surface_resource_holder_(this),
      reference_tracker_(frame_sink_id),
      is_root_(is_root),
      needs_sync_points_(needs_sync_points),
      handles_frame_sink_id_invalidation_(handles_frame_sink_id_invalidation),
      weak_factory_(this) {}

void CompositorFrameSinkSupport::Init(SurfaceManager* surface_manager) {
  surface_manager_ = surface_manager;
  if (handles_frame_sink_id_invalidation_)
    surface_manager_->RegisterFrameSinkId(frame_sink_id_);
  surface_manager_->RegisterFrameSinkManagerClient(frame_sink_id_, this);
}

void CompositorFrameSinkSupport::OnBeginFrame(const BeginFrameArgs& args) {
  UpdateNeedsBeginFramesInternal();
  last_begin_frame_args_ = args;
  if (client_)
    client_->OnBeginFrame(args);
}

const BeginFrameArgs& CompositorFrameSinkSupport::LastUsedBeginFrameArgs()
    const {
  return last_begin_frame_args_;
}

void CompositorFrameSinkSupport::OnBeginFrameSourcePausedChanged(bool paused) {}

void CompositorFrameSinkSupport::OnSurfaceActivated(Surface* surface) {
  DCHECK(surface->HasActiveFrame());
  if (!seen_first_frame_activation_) {
    seen_first_frame_activation_ = true;

    const CompositorFrame& frame = surface->GetActiveFrame();
    gfx::Size frame_size = frame.render_pass_list.back()->output_rect.size();

    // SurfaceCreated only applies for the first Surface activation. Thus,
    // SurfaceFactory stops observing new activations after the first one.
    surface_manager_->SurfaceCreated(SurfaceInfo(
        surface->surface_id(), frame.metadata.device_scale_factor, frame_size));
  }
  // Fire SurfaceCreated first so that a temporary reference is added before it
  // is potentially transformed into a real reference by the client.
  ReferencedSurfacesChanged(surface->surface_id().local_surface_id(),
                            surface->active_referenced_surfaces());
  if (!surface_manager_->SurfaceModified(surface->surface_id())) {
    TRACE_EVENT_INSTANT0("cc", "Damage not visible.", TRACE_EVENT_SCOPE_THREAD);
    surface->RunDrawCallback();
  }
  surface_manager_->SurfaceActivated(surface);
}

void CompositorFrameSinkSupport::UpdateNeedsBeginFramesInternal() {
  if (!begin_frame_source_)
    return;

  if (needs_begin_frame_ == added_frame_observer_)
    return;

  added_frame_observer_ = needs_begin_frame_;
  if (needs_begin_frame_)
    begin_frame_source_->AddObserver(this);
  else
    begin_frame_source_->RemoveObserver(this);
}

std::unique_ptr<Surface> CompositorFrameSinkSupport::CreateSurface(
    const LocalSurfaceId& local_surface_id) {
  seen_first_frame_activation_ = false;
  std::unique_ptr<Surface> surface = surface_manager_->CreateSurface(
      weak_factory_.GetWeakPtr(), local_surface_id);
  return surface;
}

void CompositorFrameSinkSupport::DestroyCurrentSurface() {
  surface_manager_->DestroySurface(std::move(current_surface_));
}

void CompositorFrameSinkSupport::RequestCopyOfSurface(
    std::unique_ptr<CopyOutputRequest> copy_request) {
  if (!current_surface_)
    return;

  DCHECK(current_surface_->compositor_frame_sink_support().get() == this);
  current_surface_->RequestCopyOfOutput(std::move(copy_request));
  surface_manager_->SurfaceModified(current_surface_->surface_id());
}

}  // namespace cc
