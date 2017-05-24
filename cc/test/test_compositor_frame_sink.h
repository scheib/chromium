// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_TEST_COMPOSITOR_FRAME_SINK_H_
#define CC_TEST_TEST_COMPOSITOR_FRAME_SINK_H_

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "cc/output/compositor_frame_sink.h"
#include "cc/output/renderer_settings.h"
#include "cc/scheduler/begin_frame_source.h"
#include "cc/surfaces/compositor_frame_sink_support_client.h"
#include "cc/surfaces/display.h"
#include "cc/surfaces/display_client.h"
#include "cc/surfaces/local_surface_id_allocator.h"
#include "cc/surfaces/surface_manager.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace cc {
class CompositorFrameSinkSupport;
class CopyOutputRequest;
class OutputSurface;

class TestCompositorFrameSinkClient {
 public:
  virtual ~TestCompositorFrameSinkClient() {}

  // This passes the ContextProvider being used by LayerTreeHostImpl which
  // can be used for the OutputSurface optionally.
  virtual std::unique_ptr<OutputSurface> CreateDisplayOutputSurface(
      scoped_refptr<ContextProvider> compositor_context_provider) = 0;

  virtual void DisplayReceivedLocalSurfaceId(
      const LocalSurfaceId& local_surface_id) = 0;
  virtual void DisplayReceivedCompositorFrame(const CompositorFrame& frame) = 0;
  virtual void DisplayWillDrawAndSwap(bool will_draw_and_swap,
                                      const RenderPassList& render_passes) = 0;
  virtual void DisplayDidDrawAndSwap() = 0;
};

// CompositorFrameSink that owns and forwards frames to a Display.
class TestCompositorFrameSink : public CompositorFrameSink,
                                public CompositorFrameSinkSupportClient,
                                public DisplayClient,
                                public ExternalBeginFrameSourceClient {
 public:
  // Pass true for |force_disable_reclaim_resources| to act like the Display
  // is out-of-process and can't return resources synchronously.
  TestCompositorFrameSink(
      scoped_refptr<ContextProvider> compositor_context_provider,
      scoped_refptr<ContextProvider> worker_context_provider,
      SharedBitmapManager* shared_bitmap_manager,
      gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager,
      const RendererSettings& renderer_settings,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      bool synchronous_composite,
      bool disable_display_vsync);
  ~TestCompositorFrameSink() override;

  // This client must be set before BindToClient() happens.
  void SetClient(TestCompositorFrameSinkClient* client) {
    test_client_ = client;
  }
  void SetEnlargePassTextureAmount(const gfx::Size& s) {
    enlarge_pass_texture_amount_ = s;
  }

  Display* display() const { return display_.get(); }

  // Will be included with the next SubmitCompositorFrame.
  void RequestCopyOfOutput(std::unique_ptr<CopyOutputRequest> request);

  // CompositorFrameSink implementation.
  bool BindToClient(CompositorFrameSinkClient* client) override;
  void DetachFromClient() override;
  void SetLocalSurfaceId(const LocalSurfaceId& local_surface_id) override;
  void SubmitCompositorFrame(CompositorFrame frame) override;
  void DidNotProduceFrame(const BeginFrameAck& ack) override;

  // CompositorFrameSinkSupportClient implementation.
  void DidReceiveCompositorFrameAck(
      const ReturnedResourceArray& resources) override;
  void OnBeginFrame(const BeginFrameArgs& args) override;
  void ReclaimResources(const ReturnedResourceArray& resources) override;
  void WillDrawSurface(const LocalSurfaceId& local_surface_id,
                       const gfx::Rect& damage_rect) override;

  // DisplayClient implementation.
  void DisplayOutputSurfaceLost() override;
  void DisplayWillDrawAndSwap(bool will_draw_and_swap,
                              const RenderPassList& render_passes) override;
  void DisplayDidDrawAndSwap() override;

 private:
  // ExternalBeginFrameSource implementation.
  void OnNeedsBeginFrames(bool needs_begin_frames) override;

  void SendCompositorFrameAckToClient();

  const bool synchronous_composite_;
  const bool disable_display_vsync_;
  const RendererSettings renderer_settings_;

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  FrameSinkId frame_sink_id_;
  // TODO(danakj): These don't need to be stored in unique_ptrs when
  // CompositorFrameSink is owned/destroyed on the compositor thread.
  std::unique_ptr<SurfaceManager> surface_manager_;
  std::unique_ptr<LocalSurfaceIdAllocator> local_surface_id_allocator_;
  LocalSurfaceId delegated_local_surface_id_;

  // Uses surface_manager_.
  std::unique_ptr<CompositorFrameSinkSupport> support_;

  std::unique_ptr<SyntheticBeginFrameSource> begin_frame_source_;
  ExternalBeginFrameSource external_begin_frame_source_;

  // Uses surface_manager_ and begin_frame_source_.
  std::unique_ptr<Display> display_;

  TestCompositorFrameSinkClient* test_client_ = nullptr;
  gfx::Size enlarge_pass_texture_amount_;

  std::vector<std::unique_ptr<CopyOutputRequest>> copy_requests_;

  base::WeakPtrFactory<TestCompositorFrameSink> weak_ptr_factory_;
};

}  // namespace cc

#endif  // CC_TEST_TEST_COMPOSITOR_FRAME_SINK_H_
