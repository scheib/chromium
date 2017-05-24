// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_GPU_RENDER_WIDGET_COMPOSITOR_H_
#define CONTENT_RENDERER_GPU_RENDER_WIDGET_COMPOSITOR_H_

#include <stdint.h>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "cc/input/browser_controls_state.h"
#include "cc/output/managed_memory_policy.h"
#include "cc/output/swap_promise.h"
#include "cc/trees/layer_tree_host_client.h"
#include "cc/trees/layer_tree_host_single_thread_client.h"
#include "cc/trees/layer_tree_settings.h"
#include "cc/trees/swap_promise_monitor.h"
#include "content/common/content_export.h"
#include "content/renderer/gpu/compositor_dependencies.h"
#include "third_party/WebKit/public/platform/WebLayerTreeView.h"
#include "ui/gfx/geometry/rect.h"

namespace base {
class CommandLine;
}

namespace cc {

class AnimationHost;
class InputHandler;
class Layer;
class LayerTreeHost;
class MutatorHost;
}

namespace gfx {
class ColorSpace;
}

namespace ui {
class LatencyInfo;
}

namespace content {

class RenderWidgetCompositorDelegate;
struct ScreenInfo;

class CONTENT_EXPORT RenderWidgetCompositor
    : NON_EXPORTED_BASE(public blink::WebLayerTreeView),
      NON_EXPORTED_BASE(public cc::LayerTreeHostClient),
      NON_EXPORTED_BASE(public cc::LayerTreeHostSingleThreadClient) {
  using ReportTimeCallback = base::Callback<void(bool, double)>;

 public:
  // Attempt to construct and initialize a compositor instance for the widget
  // with the given settings. Returns NULL if initialization fails.
  static std::unique_ptr<RenderWidgetCompositor> Create(
      RenderWidgetCompositorDelegate* delegate,
      CompositorDependencies* compositor_deps);

  ~RenderWidgetCompositor() override;

  static cc::LayerTreeSettings GenerateLayerTreeSettings(
      const base::CommandLine& cmd,
      CompositorDependencies* compositor_deps,
      float device_scale_factor,
      bool is_for_subframe,
      const ScreenInfo& screen_info);
  static std::unique_ptr<cc::LayerTreeHost> CreateLayerTreeHost(
      cc::LayerTreeHostClient* client,
      cc::LayerTreeHostSingleThreadClient* single_thread_client,
      cc::MutatorHost* mutator_host,
      CompositorDependencies* deps,
      float device_scale_factor,
      const ScreenInfo& screen_info);

  void Initialize(std::unique_ptr<cc::LayerTreeHost> layer_tree_host,
                  std::unique_ptr<cc::AnimationHost> animation_host);

  static cc::ManagedMemoryPolicy GetGpuMemoryPolicy(
      const cc::ManagedMemoryPolicy& policy);

  void SetNeverVisible();
  const base::WeakPtr<cc::InputHandler>& GetInputHandler();
  void SetNeedsDisplayOnAllLayers();
  void SetRasterizeOnlyVisibleContent();
  void SetNeedsRedrawRect(gfx::Rect damage_rect);
  // Like setNeedsRedraw but forces the frame to be drawn, without early-outs.
  // Redraw will be forced after the next commit
  void SetNeedsForcedRedraw();
  // Calling CreateLatencyInfoSwapPromiseMonitor() to get a scoped
  // LatencyInfoSwapPromiseMonitor. During the life time of the
  // LatencyInfoSwapPromiseMonitor, if SetNeedsCommit() or
  // SetNeedsUpdateLayers() is called on LayerTreeHost, the original latency
  // info will be turned into a LatencyInfoSwapPromise.
  std::unique_ptr<cc::SwapPromiseMonitor> CreateLatencyInfoSwapPromiseMonitor(
      ui::LatencyInfo* latency);
  // Calling QueueSwapPromise() to directly queue a SwapPromise into
  // LayerTreeHost.
  void QueueSwapPromise(std::unique_ptr<cc::SwapPromise> swap_promise);
  int GetSourceFrameNumber() const;
  void NotifyInputThrottledUntilCommit();
  const cc::Layer* GetRootLayer() const;
  int ScheduleMicroBenchmark(
      const std::string& name,
      std::unique_ptr<base::Value> value,
      const base::Callback<void(std::unique_ptr<base::Value>)>& callback);
  bool SendMessageToMicroBenchmark(int id, std::unique_ptr<base::Value> value);
  void SetFrameSinkId(const cc::FrameSinkId& frame_sink_id);
  void SetPaintedDeviceScaleFactor(float device_scale);
  void SetRasterColorSpace(const gfx::ColorSpace& color_space);
  void SetIsForOopif(bool is_for_oopif);
  void SetContentSourceId(uint32_t source_id);
  void SetLocalSurfaceId(const cc::LocalSurfaceId& local_surface_id);

  // WebLayerTreeView implementation.
  cc::FrameSinkId GetFrameSinkId() override;
  void SetRootLayer(const blink::WebLayer& layer) override;
  void ClearRootLayer() override;
  cc::AnimationHost* CompositorAnimationHost() override;
  void SetViewportSize(const blink::WebSize& device_viewport_size) override;
  blink::WebSize GetViewportSize() const override;
  virtual blink::WebFloatPoint adjustEventPointForPinchZoom(
      const blink::WebFloatPoint& point) const;
  void SetDeviceScaleFactor(float device_scale) override;
  void SetBackgroundColor(blink::WebColor color) override;
  void SetVisible(bool visible) override;
  void SetPageScaleFactorAndLimits(float page_scale_factor,
                                   float minimum,
                                   float maximum) override;
  void StartPageScaleAnimation(const blink::WebPoint& destination,
                               bool use_anchor,
                               float new_page_scale,
                               double duration_sec) override;
  bool HasPendingPageScaleAnimation() const override;
  void HeuristicsForGpuRasterizationUpdated(bool matches_heuristics) override;
  void SetNeedsBeginFrame() override;
  void DidStopFlinging() override;
  void LayoutAndPaintAsync(
      blink::WebLayoutAndPaintAsyncCallback* callback) override;
  void CompositeAndReadbackAsync(
      blink::WebCompositeAndReadbackAsyncCallback* callback) override;
  void SetDeferCommits(bool defer_commits) override;
  // TODO(pdr): Refactor to use a struct like LayerTreeHost::ViewportLayers.
  void RegisterViewportLayers(
      const blink::WebLayer* overscrollElasticityLayer,
      const blink::WebLayer* pageScaleLayer,
      const blink::WebLayer* innerViewportContainerLayer,
      const blink::WebLayer* outerViewportContainerLayer,
      const blink::WebLayer* innerViewportScrollLayer,
      const blink::WebLayer* outerViewportScrollLayer) override;
  void ClearViewportLayers() override;
  void RegisterSelection(const blink::WebSelection& selection) override;
  void ClearSelection() override;
  void SetMutatorClient(
      std::unique_ptr<blink::WebCompositorMutatorClient>) override;
  void ForceRecalculateRasterScales() override;
  void SetEventListenerProperties(
      blink::WebEventListenerClass eventClass,
      blink::WebEventListenerProperties properties) override;
  void UpdateEventRectsForSubframeIfNecessary() override;
  blink::WebEventListenerProperties EventListenerProperties(
      blink::WebEventListenerClass eventClass) const override;
  void SetHaveScrollEventHandlers(bool) override;
  bool HaveScrollEventHandlers() const override;
  int LayerTreeId() const override;
  void SetShowFPSCounter(bool show) override;
  void SetShowPaintRects(bool show) override;
  void SetShowDebugBorders(bool show) override;
  void SetShowScrollBottleneckRects(bool show) override;
  void NotifySwapTime(ReportTimeCallback callback) override;

  void UpdateBrowserControlsState(blink::WebBrowserControlsState constraints,
                                  blink::WebBrowserControlsState current,
                                  bool animate) override;
  void SetBrowserControlsHeight(float height, bool shrink) override;
  void SetBrowserControlsShownRatio(float) override;
  // TODO(ianwen): Move this method to WebLayerTreeView and implement main
  // thread scrolling.
  virtual void setBottomControlsHeight(float height);

  // cc::LayerTreeHostClient implementation.
  void WillBeginMainFrame() override;
  void DidBeginMainFrame() override;
  void BeginMainFrame(const cc::BeginFrameArgs& args) override;
  void BeginMainFrameNotExpectedSoon() override;
  void BeginMainFrameNotExpectedUntil(base::TimeTicks time) override;
  void UpdateLayerTreeHost() override;
  void ApplyViewportDeltas(const gfx::Vector2dF& inner_delta,
                           const gfx::Vector2dF& outer_delta,
                           const gfx::Vector2dF& elastic_overscroll_delta,
                           float page_scale,
                           float top_controls_delta) override;
  void RecordWheelAndTouchScrollingCount(bool has_scrolled_by_wheel,
                                         bool has_scrolled_by_touch) override;
  void RequestNewCompositorFrameSink() override;
  void DidInitializeCompositorFrameSink() override;
  void DidFailToInitializeCompositorFrameSink() override;
  void WillCommit() override;
  void DidCommit() override;
  void DidCommitAndDrawFrame() override;
  void DidReceiveCompositorFrameAck() override;
  void DidCompletePageScaleAnimation() override;
  bool IsForSubframe() override;

  // cc::LayerTreeHostSingleThreadClient implementation.
  void RequestScheduleAnimation() override;
  void DidSubmitCompositorFrame() override;
  void DidLoseCompositorFrameSink() override;

  enum {
    COMPOSITOR_FRAME_SINK_RETRIES_BEFORE_FALLBACK = 4,
    MAX_COMPOSITOR_FRAME_SINK_RETRIES = 5,
  };

 protected:
  friend class RenderViewImplScaleFactorTest;

  RenderWidgetCompositor(RenderWidgetCompositorDelegate* delegate,
                         CompositorDependencies* compositor_deps);

  cc::LayerTreeHost* layer_tree_host() { return layer_tree_host_.get(); }

 private:
  void SetCompositorFrameSink(
      std::unique_ptr<cc::CompositorFrameSink> compositor_frame_sink);
  void LayoutAndUpdateLayers();
  void InvokeLayoutAndPaintCallback();
  bool CompositeIsSynchronous() const;
  void SynchronouslyComposite();

  int num_failed_recreate_attempts_;
  RenderWidgetCompositorDelegate* const delegate_;
  CompositorDependencies* const compositor_deps_;
  const bool threaded_;
  std::unique_ptr<cc::AnimationHost> animation_host_;
  std::unique_ptr<cc::LayerTreeHost> layer_tree_host_;
  bool never_visible_;
  bool is_for_oopif_;

  blink::WebLayoutAndPaintAsyncCallback* layout_and_paint_async_callback_;

  cc::FrameSinkId frame_sink_id_;

  base::WeakPtrFactory<RenderWidgetCompositor> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(RenderWidgetCompositor);
};

}  // namespace content

#endif  // CONTENT_RENDERER_GPU_RENDER_WIDGET_COMPOSITOR_H_
