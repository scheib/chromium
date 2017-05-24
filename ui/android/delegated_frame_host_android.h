// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_ANDROID_DELEGATED_FRAME_HOST_ANDROID_H_
#define UI_ANDROID_DELEGATED_FRAME_HOST_ANDROID_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "cc/output/copy_output_request.h"
#include "cc/resources/returned_resource.h"
#include "cc/surfaces/compositor_frame_sink_support.h"
#include "cc/surfaces/compositor_frame_sink_support_client.h"
#include "cc/surfaces/surface_info.h"
#include "ui/android/ui_android_export.h"

namespace cc {

class CompositorFrame;
class SurfaceManager;
class SurfaceLayer;
enum class SurfaceDrawStatus;

}  // namespace cc

namespace ui {
class ViewAndroid;
class WindowAndroidCompositor;

class UI_ANDROID_EXPORT DelegatedFrameHostAndroid
    : public cc::CompositorFrameSinkSupportClient,
      public cc::ExternalBeginFrameSourceClient {
 public:
  class Client {
   public:
    virtual void SetBeginFrameSource(
        cc::BeginFrameSource* begin_frame_source) = 0;
    virtual void DidReceiveCompositorFrameAck() = 0;
    virtual void ReclaimResources(const cc::ReturnedResourceArray&) = 0;
  };

  DelegatedFrameHostAndroid(ViewAndroid* view,
                            cc::SurfaceManager* surface_manager,
                            Client* client,
                            const cc::FrameSinkId& frame_sink_id);

  ~DelegatedFrameHostAndroid() override;

  void SubmitCompositorFrame(const cc::LocalSurfaceId& local_surface_id,
                             cc::CompositorFrame frame);
  void DidNotProduceFrame(const cc::BeginFrameAck& ack);

  void DestroyDelegatedContent();

  bool HasDelegatedContent() const;

  cc::FrameSinkId GetFrameSinkId() const;

  // Should only be called when the host has a content layer.
  void RequestCopyOfSurface(
      WindowAndroidCompositor* compositor,
      const gfx::Rect& src_subrect_in_pixel,
      cc::CopyOutputRequest::CopyOutputRequestCallback result_callback);

  void CompositorFrameSinkChanged();

  // Called when this DFH is attached/detached from a parent browser compositor
  // and needs to be attached to the surface hierarchy.
  void AttachToCompositor(WindowAndroidCompositor* compositor);
  void DetachFromCompositor();

  // Returns the ID for the current Surface.
  cc::SurfaceId SurfaceId() const;

 private:
  // cc::CompositorFrameSinkSupportClient implementation.
  void DidReceiveCompositorFrameAck(
      const cc::ReturnedResourceArray& resources) override;
  void OnBeginFrame(const cc::BeginFrameArgs& args) override;
  void ReclaimResources(const cc::ReturnedResourceArray& resources) override;
  void WillDrawSurface(const cc::LocalSurfaceId& local_surface_id,
                       const gfx::Rect& damage_rect) override;

  // cc::ExternalBeginFrameSourceClient implementation.
  void OnNeedsBeginFrames(bool needs_begin_frames) override;

  void CreateNewCompositorFrameSinkSupport();

  const cc::FrameSinkId frame_sink_id_;

  ViewAndroid* view_;

  cc::SurfaceManager* surface_manager_;
  WindowAndroidCompositor* registered_parent_compositor_ = nullptr;
  Client* client_;

  std::unique_ptr<cc::CompositorFrameSinkSupport> support_;
  cc::ExternalBeginFrameSource begin_frame_source_;

  cc::SurfaceInfo surface_info_;
  bool has_transparent_background_ = false;

  scoped_refptr<cc::SurfaceLayer> content_layer_;

  DISALLOW_COPY_AND_ASSIGN(DelegatedFrameHostAndroid);
};

}  // namespace ui

#endif  // UI_ANDROID_DELEGATED_FRAME_HOST_ANDROID_H_
