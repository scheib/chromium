// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/delegated_frame_host.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "base/time/default_tick_clock.h"
#include "cc/base/switches.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/copy_output_request.h"
#include "cc/resources/single_release_callback.h"
#include "cc/resources/texture_mailbox.h"
#include "cc/surfaces/compositor_frame_sink_support.h"
#include "cc/surfaces/surface.h"
#include "cc/surfaces/surface_hittest.h"
#include "cc/surfaces/surface_manager.h"
#include "components/viz/display_compositor/gl_helper.h"
#include "content/browser/compositor/surface_utils.h"
#include "content/browser/gpu/compositor_util.h"
#include "content/browser/renderer_host/compositor_resize_lock.h"
#include "content/browser/renderer_host/render_widget_host_view_frame_subscriber.h"
#include "content/public/common/content_switches.h"
#include "media/base/video_frame.h"
#include "media/base/video_util.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/effects/SkLumaColorFilter.h"
#include "ui/gfx/geometry/dip_util.h"

namespace content {

////////////////////////////////////////////////////////////////////////////////
// DelegatedFrameHost

DelegatedFrameHost::DelegatedFrameHost(const cc::FrameSinkId& frame_sink_id,
                                       DelegatedFrameHostClient* client)
    : frame_sink_id_(frame_sink_id),
      client_(client),
      compositor_(nullptr),
      tick_clock_(new base::DefaultTickClock()),
      skipped_frames_(false),
      background_color_(SK_ColorRED),
      current_scale_factor_(1.f),
      frame_evictor_(new viz::FrameEvictor(this)) {
  ImageTransportFactory* factory = ImageTransportFactory::GetInstance();
  factory->GetContextFactory()->AddObserver(this);
  factory->GetContextFactoryPrivate()->GetSurfaceManager()->RegisterFrameSinkId(
      frame_sink_id_);
  CreateCompositorFrameSinkSupport();
}

void DelegatedFrameHost::WasShown(const ui::LatencyInfo& latency_info) {
  frame_evictor_->SetVisible(true);

  if (!has_frame_ && !released_front_lock_.get()) {
    if (compositor_)
      released_front_lock_ = compositor_->GetCompositorLock(nullptr);
  }

  if (compositor_) {
    compositor_->SetLatencyInfo(latency_info);
  }
}

bool DelegatedFrameHost::HasSavedFrame() {
  return frame_evictor_->HasFrame();
}

void DelegatedFrameHost::WasHidden() {
  frame_evictor_->SetVisible(false);
  released_front_lock_ = NULL;
}

void DelegatedFrameHost::MaybeCreateResizeLock() {
  DCHECK(!resize_lock_);

  if (!compositor_)
    return;

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableResizeLock))
    return;

  if (!has_frame_)
    return;

  if (!client_->DelegatedFrameCanCreateResizeLock())
    return;

  gfx::Size desired_size = client_->DelegatedFrameHostDesiredSizeInDIP();
  if (desired_size.IsEmpty())
    return;
  if (desired_size == current_frame_size_in_dip_)
    return;

  resize_lock_ = client_->DelegatedFrameHostCreateResizeLock();
  bool locked = resize_lock_->Lock();
  DCHECK(locked);
}

void DelegatedFrameHost::CopyFromCompositingSurface(
    const gfx::Rect& src_subrect,
    const gfx::Size& output_size,
    const ReadbackRequestCallback& callback,
    const SkColorType preferred_color_type) {
  // Only ARGB888 and RGB565 supported as of now.
  bool format_support = ((preferred_color_type == kAlpha_8_SkColorType) ||
                         (preferred_color_type == kRGB_565_SkColorType) ||
                         (preferred_color_type == kN32_SkColorType));
  DCHECK(format_support);
  if (!CanCopyFromCompositingSurface()) {
    callback.Run(SkBitmap(), content::READBACK_SURFACE_UNAVAILABLE);
    return;
  }

  std::unique_ptr<cc::CopyOutputRequest> request =
      cc::CopyOutputRequest::CreateRequest(
          base::Bind(&CopyFromCompositingSurfaceHasResult, output_size,
                     preferred_color_type, callback));
  if (!src_subrect.IsEmpty())
    request->set_area(src_subrect);
  RequestCopyOfOutput(std::move(request));
}

void DelegatedFrameHost::CopyFromCompositingSurfaceToVideoFrame(
    const gfx::Rect& src_subrect,
    scoped_refptr<media::VideoFrame> target,
    const base::Callback<void(const gfx::Rect&, bool)>& callback) {
  if (!CanCopyFromCompositingSurface()) {
    callback.Run(gfx::Rect(), false);
    return;
  }

  std::unique_ptr<cc::CopyOutputRequest> request =
      cc::CopyOutputRequest::CreateRequest(base::Bind(
          &DelegatedFrameHost::CopyFromCompositingSurfaceHasResultForVideo,
          AsWeakPtr(),  // For caching the ReadbackYUVInterface on this class.
          nullptr, std::move(target), callback));
  if (!src_subrect.IsEmpty())
    request->set_area(src_subrect);
  RequestCopyOfOutput(std::move(request));
}

bool DelegatedFrameHost::CanCopyFromCompositingSurface() const {
  return compositor_ &&
         client_->DelegatedFrameHostGetLayer()->has_external_content();
}

void DelegatedFrameHost::BeginFrameSubscription(
    std::unique_ptr<RenderWidgetHostViewFrameSubscriber> subscriber) {
  frame_subscriber_ = std::move(subscriber);
}

void DelegatedFrameHost::EndFrameSubscription() {
  idle_frame_subscriber_textures_.clear();
  frame_subscriber_.reset();
}

cc::FrameSinkId DelegatedFrameHost::GetFrameSinkId() {
  return frame_sink_id_;
}

cc::SurfaceId DelegatedFrameHost::SurfaceIdAtPoint(
    cc::SurfaceHittestDelegate* delegate,
    const gfx::Point& point,
    gfx::Point* transformed_point) {
  cc::SurfaceId surface_id(frame_sink_id_, local_surface_id_);
  if (!surface_id.is_valid())
    return surface_id;
  cc::SurfaceHittest hittest(delegate, GetSurfaceManager());
  gfx::Transform target_transform;
  cc::SurfaceId target_local_surface_id =
      hittest.GetTargetSurfaceAtPoint(surface_id, point, &target_transform);
  *transformed_point = point;
  if (target_local_surface_id.is_valid())
    target_transform.TransformPoint(transformed_point);
  return target_local_surface_id;
}

bool DelegatedFrameHost::TransformPointToLocalCoordSpace(
    const gfx::Point& point,
    const cc::SurfaceId& original_surface,
    gfx::Point* transformed_point) {
  cc::SurfaceId surface_id(frame_sink_id_, local_surface_id_);
  if (!surface_id.is_valid())
    return false;
  *transformed_point = point;
  if (original_surface == surface_id)
    return true;

  cc::SurfaceHittest hittest(nullptr, GetSurfaceManager());
  return hittest.TransformPointToTargetSurface(original_surface, surface_id,
                                               transformed_point);
}

bool DelegatedFrameHost::TransformPointToCoordSpaceForView(
    const gfx::Point& point,
    RenderWidgetHostViewBase* target_view,
    gfx::Point* transformed_point) {
  if (!has_frame_)
    return false;

  return target_view->TransformPointToLocalCoordSpace(
      point, cc::SurfaceId(frame_sink_id_, local_surface_id_),
      transformed_point);
}

void DelegatedFrameHost::SetNeedsBeginFrames(bool needs_begin_frames) {
  needs_begin_frame_ = needs_begin_frames;
  support_->SetNeedsBeginFrame(needs_begin_frames);
}

void DelegatedFrameHost::DidNotProduceFrame(const cc::BeginFrameAck& ack) {
  DidFinishFrame(ack);

  cc::BeginFrameAck modified_ack = ack;
  if (skipped_frames_) {
    // If we skipped the last frame(s), we didn't incorporate the last
    // CompositorFrame's damage, so need to wait for the next one before
    // confirming newer sequence numbers.
    modified_ack.has_damage = false;
    modified_ack.latest_confirmed_sequence_number =
        latest_confirmed_begin_frame_sequence_number_;
  }

  support_->DidNotProduceFrame(modified_ack);
}

bool DelegatedFrameHost::ShouldSkipFrame(const gfx::Size& size_in_dip) {
  if (!resize_lock_)
    return false;
  // Allow a single renderer frame through even though there's a resize lock
  // currently in place.
  if (allow_one_renderer_frame_during_resize_lock_) {
    allow_one_renderer_frame_during_resize_lock_ = false;
    return false;
  }
  return size_in_dip != resize_lock_->expected_size();
}

void DelegatedFrameHost::WasResized() {
  if (client_->DelegatedFrameHostDesiredSizeInDIP() !=
          current_frame_size_in_dip_ &&
      !client_->DelegatedFrameHostIsVisible())
    EvictDelegatedFrame();
  // If |create_resize_lock_after_commit_| is true, we're waiting to recreate
  // an expired resize lock after the next UI frame is submitted, so don't
  // make a lock here.
  if (!resize_lock_ && !create_resize_lock_after_commit_)
    MaybeCreateResizeLock();
  UpdateGutters();
}

SkColor DelegatedFrameHost::GetGutterColor() const {
  // In fullscreen mode resizing is uncommon, so it makes more sense to
  // make the initial switch to fullscreen mode look better by using black as
  // the gutter color.
  return client_->DelegatedFrameHostGetGutterColor(background_color_);
}

void DelegatedFrameHost::UpdateGutters() {
  if (!has_frame_) {
    right_gutter_.reset();
    bottom_gutter_.reset();
    return;
  }

  if (current_frame_size_in_dip_.width() <
      client_->DelegatedFrameHostDesiredSizeInDIP().width()) {
    right_gutter_.reset(new ui::Layer(ui::LAYER_SOLID_COLOR));
    right_gutter_->SetColor(GetGutterColor());
    int width = client_->DelegatedFrameHostDesiredSizeInDIP().width() -
                current_frame_size_in_dip_.width();
    // The right gutter also includes the bottom-right corner, if necessary.
    int height = client_->DelegatedFrameHostDesiredSizeInDIP().height();
    right_gutter_->SetBounds(
        gfx::Rect(current_frame_size_in_dip_.width(), 0, width, height));

    client_->DelegatedFrameHostGetLayer()->Add(right_gutter_.get());
  } else {
    right_gutter_.reset();
  }

  if (current_frame_size_in_dip_.height() <
      client_->DelegatedFrameHostDesiredSizeInDIP().height()) {
    bottom_gutter_.reset(new ui::Layer(ui::LAYER_SOLID_COLOR));
    bottom_gutter_->SetColor(GetGutterColor());
    int width = current_frame_size_in_dip_.width();
    int height = client_->DelegatedFrameHostDesiredSizeInDIP().height() -
                 current_frame_size_in_dip_.height();
    bottom_gutter_->SetBounds(
        gfx::Rect(0, current_frame_size_in_dip_.height(), width, height));
    client_->DelegatedFrameHostGetLayer()->Add(bottom_gutter_.get());
  } else {
    bottom_gutter_.reset();
  }
}

gfx::Size DelegatedFrameHost::GetRequestedRendererSize() const {
  if (resize_lock_)
    return resize_lock_->expected_size();
  else
    return client_->DelegatedFrameHostDesiredSizeInDIP();
}

void DelegatedFrameHost::CheckResizeLock() {
  if (!resize_lock_ ||
      resize_lock_->expected_size() != current_frame_size_in_dip_)
    return;

  // Since we got the size we were looking for, unlock the compositor. But delay
  // the release of the lock until we've kicked a frame with the new texture, to
  // avoid resizing the UI before we have a chance to draw a "good" frame.
  resize_lock_->UnlockCompositor();
}

void DelegatedFrameHost::AttemptFrameSubscriberCapture(
    const gfx::Rect& damage_rect) {
  if (!frame_subscriber() || !CanCopyFromCompositingSurface())
    return;

  const base::TimeTicks now = tick_clock_->NowTicks();
  base::TimeTicks present_time;
  if (vsync_interval_ <= base::TimeDelta()) {
    present_time = now;
  } else {
    const int64_t intervals_elapsed = (now - vsync_timebase_) / vsync_interval_;
    present_time = vsync_timebase_ + (intervals_elapsed + 1) * vsync_interval_;
  }

  scoped_refptr<media::VideoFrame> frame;
  RenderWidgetHostViewFrameSubscriber::DeliverFrameCallback callback;
  if (!frame_subscriber()->ShouldCaptureFrame(damage_rect, present_time, &frame,
                                              &callback))
    return;

  // Get a texture to re-use; else, create a new one.
  scoped_refptr<OwnedMailbox> subscriber_texture;
  if (!idle_frame_subscriber_textures_.empty()) {
    subscriber_texture = idle_frame_subscriber_textures_.back();
    idle_frame_subscriber_textures_.pop_back();
  } else if (viz::GLHelper* helper =
                 ImageTransportFactory::GetInstance()->GetGLHelper()) {
    subscriber_texture = new OwnedMailbox(helper);
  }

  std::unique_ptr<cc::CopyOutputRequest> request =
      cc::CopyOutputRequest::CreateRequest(base::Bind(
          &DelegatedFrameHost::CopyFromCompositingSurfaceHasResultForVideo,
          AsWeakPtr(), subscriber_texture, frame,
          base::Bind(callback, present_time)));
  // Setting the source in this copy request asks that the layer abort any prior
  // uncommitted copy requests made on behalf of the same frame subscriber.
  // This will not affect any of the copy requests spawned elsewhere from
  // DelegatedFrameHost (e.g., a call to CopyFromCompositingSurface() for
  // screenshots) since those copy requests do not specify |frame_subscriber()|
  // as a source.
  request->set_source(frame_subscriber()->GetSourceIdForCopyRequest());
  if (subscriber_texture.get()) {
    request->SetTextureMailbox(cc::TextureMailbox(
        subscriber_texture->mailbox(), subscriber_texture->sync_token(),
        subscriber_texture->target()));
  }

  // To avoid unnecessary browser composites, try to go directly to the Surface
  // rather than through the Layer (which goes through the browser compositor).
  if (has_frame_ && request_copy_of_output_callback_for_testing_.is_null()) {
    support_->RequestCopyOfSurface(std::move(request));
  } else {
    RequestCopyOfOutput(std::move(request));
  }
}

void DelegatedFrameHost::DidCreateNewRendererCompositorFrameSink(
    cc::mojom::MojoCompositorFrameSinkClient* renderer_compositor_frame_sink) {
  ResetCompositorFrameSinkSupport();
  renderer_compositor_frame_sink_ = renderer_compositor_frame_sink;
  CreateCompositorFrameSinkSupport();
  has_frame_ = false;
}

void DelegatedFrameHost::SubmitCompositorFrame(
    const cc::LocalSurfaceId& local_surface_id,
    cc::CompositorFrame frame) {
#if defined(OS_CHROMEOS)
  DCHECK(!resize_lock_ || !client_->IsAutoResizeEnabled());
#endif
  float frame_device_scale_factor = frame.metadata.device_scale_factor;
  cc::BeginFrameAck ack(frame.metadata.begin_frame_ack);

  DCHECK(!frame.render_pass_list.empty());

  cc::RenderPass* root_pass = frame.render_pass_list.back().get();

  gfx::Size frame_size = root_pass->output_rect.size();
  gfx::Size frame_size_in_dip =
      gfx::ConvertSizeToDIP(frame_device_scale_factor, frame_size);

  gfx::Rect damage_rect = root_pass->damage_rect;
  damage_rect.Intersect(gfx::Rect(frame_size));
  gfx::Rect damage_rect_in_dip =
      gfx::ConvertRectToDIP(frame_device_scale_factor, damage_rect);

  if (ShouldSkipFrame(frame_size_in_dip)) {
    cc::ReturnedResourceArray resources;
    cc::TransferableResource::ReturnResources(frame.resource_list, &resources);

    skipped_latency_info_list_.insert(skipped_latency_info_list_.end(),
                                      frame.metadata.latency_info.begin(),
                                      frame.metadata.latency_info.end());

    renderer_compositor_frame_sink_->DidReceiveCompositorFrameAck(resources);

    skipped_frames_ = true;
    DidNotProduceFrame(ack);
    return;
  }

  // If we are allowing one renderer frame through, this would ensure the frame
  // gets through even if we regrab the lock after the UI compositor makes one
  // frame. If the renderer frame beats the UI compositor, then we don't need to
  // allow any more, though.
  allow_one_renderer_frame_during_resize_lock_ = false;

  if (skipped_frames_) {
    skipped_frames_ = false;
    damage_rect = gfx::Rect(frame_size);
    damage_rect_in_dip = gfx::Rect(frame_size_in_dip);

    // Give the same damage rect to the compositor.
    cc::RenderPass* root_pass = frame.render_pass_list.back().get();
    root_pass->damage_rect = damage_rect;
  }

  background_color_ = frame.metadata.root_background_color;

  if (frame_size.IsEmpty()) {
    DCHECK(frame.resource_list.empty());
    EvictDelegatedFrame();
  } else {
    ImageTransportFactory* factory = ImageTransportFactory::GetInstance();
    cc::SurfaceManager* manager =
        factory->GetContextFactoryPrivate()->GetSurfaceManager();

    frame.metadata.latency_info.insert(frame.metadata.latency_info.end(),
                                       skipped_latency_info_list_.begin(),
                                       skipped_latency_info_list_.end());
    skipped_latency_info_list_.clear();

    support_->SubmitCompositorFrame(local_surface_id, std::move(frame));

    if (local_surface_id != local_surface_id_ || !has_frame_) {
      // manager must outlive compositors using it.
      cc::SurfaceId surface_id(frame_sink_id_, local_surface_id);
      cc::SurfaceInfo surface_info(surface_id, frame_device_scale_factor,
                                   frame_size);
      client_->DelegatedFrameHostGetLayer()->SetShowPrimarySurface(
          surface_info, manager->reference_factory());
      current_surface_size_ = frame_size;
      current_scale_factor_ = frame_device_scale_factor;
    }

    has_frame_ = true;
  }
  local_surface_id_ = local_surface_id;

  released_front_lock_ = NULL;
  current_frame_size_in_dip_ = frame_size_in_dip;
  CheckResizeLock();

  UpdateGutters();

  if (!damage_rect_in_dip.IsEmpty()) {
    client_->DelegatedFrameHostGetLayer()->OnDelegatedFrameDamage(
        damage_rect_in_dip);
  }

  if (has_frame_) {
    frame_evictor_->SwappedFrame(client_->DelegatedFrameHostIsVisible());
  }
  // Note: the frame may have been evicted immediately.

  DidFinishFrame(ack);
}

void DelegatedFrameHost::ClearDelegatedFrame() {
  EvictDelegatedFrame();
}

void DelegatedFrameHost::DidReceiveCompositorFrameAck(
    const cc::ReturnedResourceArray& resources) {
  renderer_compositor_frame_sink_->DidReceiveCompositorFrameAck(resources);
}

void DelegatedFrameHost::ReclaimResources(
    const cc::ReturnedResourceArray& resources) {
  renderer_compositor_frame_sink_->ReclaimResources(resources);
}

void DelegatedFrameHost::WillDrawSurface(const cc::LocalSurfaceId& id,
                                         const gfx::Rect& damage_rect) {
  if (id != local_surface_id_)
    return;
  AttemptFrameSubscriberCapture(damage_rect);
}

void DelegatedFrameHost::OnBeginFrame(const cc::BeginFrameArgs& args) {
  client_->OnBeginFrame(args);
}

void DelegatedFrameHost::EvictDelegatedFrame() {
  if (!has_frame_)
    return;
  client_->DelegatedFrameHostGetLayer()->SetShowSolidColorContent();
  support_->EvictCurrentSurface();
  has_frame_ = false;
  resize_lock_.reset();
  frame_evictor_->DiscardedFrame();
  UpdateGutters();
}

// static
void DelegatedFrameHost::ReturnSubscriberTexture(
    base::WeakPtr<DelegatedFrameHost> dfh,
    scoped_refptr<OwnedMailbox> subscriber_texture,
    const gpu::SyncToken& sync_token) {
  if (!subscriber_texture.get())
    return;
  if (!dfh)
    return;

  subscriber_texture->UpdateSyncToken(sync_token);

  if (dfh->frame_subscriber_ && subscriber_texture->texture_id())
    dfh->idle_frame_subscriber_textures_.push_back(subscriber_texture);
}

// static
void DelegatedFrameHost::CopyFromCompositingSurfaceFinishedForVideo(
    scoped_refptr<media::VideoFrame> video_frame,
    base::WeakPtr<DelegatedFrameHost> dfh,
    const base::Callback<void(bool)>& callback,
    scoped_refptr<OwnedMailbox> subscriber_texture,
    std::unique_ptr<cc::SingleReleaseCallback> release_callback,
    bool result) {
  callback.Run(result);

  gpu::SyncToken sync_token;
  if (result) {
    viz::GLHelper* gl_helper =
        ImageTransportFactory::GetInstance()->GetGLHelper();
    gl_helper->GenerateSyncToken(&sync_token);
  }
  if (release_callback) {
    // A release callback means the texture came from the compositor, so there
    // should be no |subscriber_texture|.
    DCHECK(!subscriber_texture.get());
    const bool lost_resource = !sync_token.HasData();
    release_callback->Run(sync_token, lost_resource);
  }
  ReturnSubscriberTexture(dfh, subscriber_texture, sync_token);
}

// static
void DelegatedFrameHost::CopyFromCompositingSurfaceHasResultForVideo(
    base::WeakPtr<DelegatedFrameHost> dfh,
    scoped_refptr<OwnedMailbox> subscriber_texture,
    scoped_refptr<media::VideoFrame> video_frame,
    const base::Callback<void(const gfx::Rect&, bool)>& callback,
    std::unique_ptr<cc::CopyOutputResult> result) {
  base::ScopedClosureRunner scoped_callback_runner(
      base::Bind(callback, gfx::Rect(), false));
  base::ScopedClosureRunner scoped_return_subscriber_texture(base::Bind(
      &ReturnSubscriberTexture, dfh, subscriber_texture, gpu::SyncToken()));

  if (!dfh)
    return;
  if (result->IsEmpty())
    return;
  if (result->size().IsEmpty())
    return;

  // Compute the dest size we want after the letterboxing resize. Make the
  // coordinates and sizes even because we letterbox in YUV space
  // (see CopyRGBToVideoFrame). They need to be even for the UV samples to
  // line up correctly.
  // The video frame's visible_rect() and the result's size() are both physical
  // pixels.
  gfx::Rect region_in_frame = media::ComputeLetterboxRegion(
      video_frame->visible_rect(), result->size());
  region_in_frame =
      gfx::Rect(region_in_frame.x() & ~1, region_in_frame.y() & ~1,
                region_in_frame.width() & ~1, region_in_frame.height() & ~1);
  if (region_in_frame.IsEmpty())
    return;

  if (!result->HasTexture()) {
    DCHECK(result->HasBitmap());
    std::unique_ptr<SkBitmap> bitmap = result->TakeBitmap();
    // Scale the bitmap to the required size, if necessary.
    SkBitmap scaled_bitmap;
    if (result->size() != region_in_frame.size()) {
      skia::ImageOperations::ResizeMethod method =
          skia::ImageOperations::RESIZE_GOOD;
      scaled_bitmap = skia::ImageOperations::Resize(*bitmap.get(), method,
                                                    region_in_frame.width(),
                                                    region_in_frame.height());
    } else {
      scaled_bitmap = *bitmap.get();
    }

    media::CopyRGBToVideoFrame(
        reinterpret_cast<uint8_t*>(scaled_bitmap.getPixels()),
        scaled_bitmap.rowBytes(), region_in_frame, video_frame.get());

    ignore_result(scoped_callback_runner.Release());
    callback.Run(region_in_frame, true);
    return;
  }

  ImageTransportFactory* factory = ImageTransportFactory::GetInstance();
  viz::GLHelper* gl_helper = factory->GetGLHelper();
  if (!gl_helper)
    return;
  if (subscriber_texture.get() && !subscriber_texture->texture_id())
    return;

  cc::TextureMailbox texture_mailbox;
  std::unique_ptr<cc::SingleReleaseCallback> release_callback;
  result->TakeTexture(&texture_mailbox, &release_callback);
  DCHECK(texture_mailbox.IsTexture());

  gfx::Rect result_rect(result->size());

  viz::ReadbackYUVInterface* yuv_readback_pipeline =
      dfh->yuv_readback_pipeline_.get();
  if (yuv_readback_pipeline == NULL ||
      yuv_readback_pipeline->scaler()->SrcSize() != result_rect.size() ||
      yuv_readback_pipeline->scaler()->SrcSubrect() != result_rect ||
      yuv_readback_pipeline->scaler()->DstSize() != region_in_frame.size()) {
    // The scaler chosen here is based on performance measurements of full
    // end-to-end systems.  When down-scaling, always use the "fast" scaler
    // because it performs well on both low- and high- end machines, provides
    // decent image quality, and doesn't overwhelm downstream video encoders
    // with too much entropy (which can drastically increase CPU utilization).
    // When up-scaling, always use "best" because the quality improvement is
    // huge with insignificant performance penalty.  Note that this strategy
    // differs from single-frame snapshot capture.
    viz::GLHelper::ScalerQuality quality =
        ((result_rect.size().width() < region_in_frame.size().width()) &&
         (result_rect.size().height() < region_in_frame.size().height()))
            ? viz::GLHelper::SCALER_QUALITY_BEST
            : viz::GLHelper::SCALER_QUALITY_FAST;

    DVLOG(1) << "Re-creating YUV readback pipeline for source rect "
             << result_rect.ToString() << " and destination size "
             << region_in_frame.size().ToString();

    dfh->yuv_readback_pipeline_.reset(gl_helper->CreateReadbackPipelineYUV(
        quality, result_rect.size(), result_rect, region_in_frame.size(), true,
        true));
    yuv_readback_pipeline = dfh->yuv_readback_pipeline_.get();
  }

  ignore_result(scoped_callback_runner.Release());
  ignore_result(scoped_return_subscriber_texture.Release());

  base::Callback<void(bool result)> finished_callback = base::Bind(
      &DelegatedFrameHost::CopyFromCompositingSurfaceFinishedForVideo,
      video_frame, dfh->AsWeakPtr(), base::Bind(callback, region_in_frame),
      subscriber_texture, base::Passed(&release_callback));
  yuv_readback_pipeline->ReadbackYUV(
      texture_mailbox.mailbox(), texture_mailbox.sync_token(),
      video_frame->visible_rect(),
      video_frame->stride(media::VideoFrame::kYPlane),
      video_frame->data(media::VideoFrame::kYPlane),
      video_frame->stride(media::VideoFrame::kUPlane),
      video_frame->data(media::VideoFrame::kUPlane),
      video_frame->stride(media::VideoFrame::kVPlane),
      video_frame->data(media::VideoFrame::kVPlane), region_in_frame.origin(),
      finished_callback);
  media::LetterboxYUV(video_frame.get(), region_in_frame);
}

////////////////////////////////////////////////////////////////////////////////
// DelegatedFrameHost, ui::CompositorObserver implementation:

void DelegatedFrameHost::OnCompositingDidCommit(ui::Compositor* compositor) {
  // If |create_resize_lock_after_commit_| then we should have popped the old
  // lock already.
  DCHECK(!resize_lock_ || !create_resize_lock_after_commit_);

  if (resize_lock_ &&
      resize_lock_->expected_size() == current_frame_size_in_dip_) {
    resize_lock_.reset();
    // We had a lock but the UI may have resized in the meantime.
    create_resize_lock_after_commit_ = true;
  }

  if (create_resize_lock_after_commit_) {
    create_resize_lock_after_commit_ = false;
    MaybeCreateResizeLock();
  }
}

void DelegatedFrameHost::OnCompositingStarted(ui::Compositor* compositor,
                                              base::TimeTicks start_time) {
  last_draw_ended_ = start_time;
}

void DelegatedFrameHost::OnCompositingEnded(ui::Compositor* compositor) {}

void DelegatedFrameHost::OnCompositingLockStateChanged(
    ui::Compositor* compositor) {
  if (resize_lock_ && resize_lock_->timed_out()) {
    // A compositor lock that is part of a resize lock timed out. We allow
    // the UI to produce a frame before locking it again, so we don't lock here.
    // We release the |resize_lock_| though to allow any other resizes that are
    // desired at the same time since we're allowing the UI to make a frame
    // which will gutter anyways.
    resize_lock_.reset();
    create_resize_lock_after_commit_ = true;
    // Because this timed out, we're going to allow the UI to update and lock
    // again. We would allow renderer frames through during this time if they
    // came late, but would stop them again once the UI finished its frame. We
    // want to allow the slow renderer to show us one frame even if its wrong
    // since we're guttering anyways, but not unlimited number of frames as that
    // would be a waste of power.
    allow_one_renderer_frame_during_resize_lock_ = true;
  }
}

void DelegatedFrameHost::OnCompositingShuttingDown(ui::Compositor* compositor) {
  DCHECK_EQ(compositor, compositor_);
  ResetCompositor();
  DCHECK(!compositor_);
}

void DelegatedFrameHost::OnUpdateVSyncParameters(base::TimeTicks timebase,
                                                 base::TimeDelta interval) {
  vsync_timebase_ = timebase;
  vsync_interval_ = interval;
}

////////////////////////////////////////////////////////////////////////////////
// DelegatedFrameHost, ImageTransportFactoryObserver implementation:

void DelegatedFrameHost::OnLostResources() {
  EvictDelegatedFrame();
  idle_frame_subscriber_textures_.clear();
  yuv_readback_pipeline_.reset();
}

////////////////////////////////////////////////////////////////////////////////
// DelegatedFrameHost, private:

DelegatedFrameHost::~DelegatedFrameHost() {
  DCHECK(!compositor_);
  ImageTransportFactory* factory = ImageTransportFactory::GetInstance();
  factory->GetContextFactory()->RemoveObserver(this);

  ResetCompositorFrameSinkSupport();

  factory->GetContextFactoryPrivate()
      ->GetSurfaceManager()
      ->InvalidateFrameSinkId(frame_sink_id_);

  DCHECK(!vsync_manager_.get());
}

void DelegatedFrameHost::SetCompositor(ui::Compositor* compositor) {
  DCHECK(!compositor_);
  if (!compositor)
    return;
  compositor_ = compositor;
  compositor_->AddObserver(this);
  DCHECK(!vsync_manager_.get());
  vsync_manager_ = compositor_->vsync_manager();
  vsync_manager_->AddObserver(this);

  compositor_->AddFrameSink(frame_sink_id_);
}

void DelegatedFrameHost::ResetCompositor() {
  if (!compositor_)
    return;
  resize_lock_.reset();
  if (compositor_->HasObserver(this))
    compositor_->RemoveObserver(this);
  if (vsync_manager_) {
    vsync_manager_->RemoveObserver(this);
    vsync_manager_ = nullptr;
  }

  compositor_->RemoveFrameSink(frame_sink_id_);
  compositor_ = nullptr;
}

void DelegatedFrameHost::LockResources() {
  DCHECK(local_surface_id_.is_valid());
  frame_evictor_->LockFrame();
}

void DelegatedFrameHost::RequestCopyOfOutput(
    std::unique_ptr<cc::CopyOutputRequest> request) {
  // If a specific area has not been requested, set one to ensure correct
  // clipping occurs.
  if (!request->has_area())
    request->set_area(gfx::Rect(current_frame_size_in_dip_));

  if (request_copy_of_output_callback_for_testing_.is_null()) {
    client_->DelegatedFrameHostGetLayer()->RequestCopyOfOutput(
        std::move(request));
  } else {
    request_copy_of_output_callback_for_testing_.Run(std::move(request));
  }
}

void DelegatedFrameHost::UnlockResources() {
  DCHECK(local_surface_id_.is_valid());
  frame_evictor_->UnlockFrame();
}

void DelegatedFrameHost::CreateCompositorFrameSinkSupport() {
  DCHECK(!support_);
  constexpr bool is_root = false;
  constexpr bool handles_frame_sink_id_invalidation = false;
  constexpr bool needs_sync_points = true;
  ImageTransportFactory* factory = ImageTransportFactory::GetInstance();
  support_ = cc::CompositorFrameSinkSupport::Create(
      this, factory->GetContextFactoryPrivate()->GetSurfaceManager(),
      frame_sink_id_, is_root, handles_frame_sink_id_invalidation,
      needs_sync_points);
  if (compositor_)
    compositor_->AddFrameSink(frame_sink_id_);
  if (needs_begin_frame_)
    support_->SetNeedsBeginFrame(true);
}

void DelegatedFrameHost::ResetCompositorFrameSinkSupport() {
  if (!support_)
    return;
  if (compositor_)
    compositor_->RemoveFrameSink(frame_sink_id_);
  support_.reset();
}

void DelegatedFrameHost::DidFinishFrame(const cc::BeginFrameAck& ack) {
  if (ack.source_id != latest_confirmed_begin_frame_source_id_) {
    // Source changed, we don't know our freshness anymore.
    latest_confirmed_begin_frame_sequence_number_ =
        cc::BeginFrameArgs::kInvalidFrameNumber;
  }

  if (!skipped_frames_) {
    latest_confirmed_begin_frame_source_id_ = ack.source_id;
    latest_confirmed_begin_frame_sequence_number_ =
        ack.latest_confirmed_sequence_number;
  }
}

}  // namespace content
