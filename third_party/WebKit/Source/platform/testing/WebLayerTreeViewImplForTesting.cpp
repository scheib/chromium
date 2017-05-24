// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/testing/WebLayerTreeViewImplForTesting.h"

#include "base/threading/thread_task_runner_handle.h"
#include "cc/animation/animation_host.h"
#include "cc/animation/animation_timeline.h"
#include "cc/blink/web_layer_impl.h"
#include "cc/layers/layer.h"
#include "cc/trees/layer_tree_host.h"
#include "cc/trees/layer_tree_settings.h"
#include "public/platform/Platform.h"
#include "public/platform/WebLayer.h"
#include "public/platform/WebLayerTreeView.h"
#include "public/platform/WebSize.h"

namespace blink {

WebLayerTreeViewImplForTesting::WebLayerTreeViewImplForTesting()
    : WebLayerTreeViewImplForTesting(DefaultLayerTreeSettings()) {}

WebLayerTreeViewImplForTesting::WebLayerTreeViewImplForTesting(
    const cc::LayerTreeSettings& settings) {
  animation_host_ = cc::AnimationHost::CreateMainInstance();
  cc::LayerTreeHost::InitParams params;
  params.client = this;
  params.settings = &settings;
  params.main_task_runner = base::ThreadTaskRunnerHandle::Get();
  params.task_graph_runner = &task_graph_runner_;
  params.mutator_host = animation_host_.get();
  layer_tree_host_ = cc::LayerTreeHost::CreateSingleThreaded(this, &params);
  DCHECK(layer_tree_host_);
}

WebLayerTreeViewImplForTesting::~WebLayerTreeViewImplForTesting() {}

// static
cc::LayerTreeSettings
WebLayerTreeViewImplForTesting::DefaultLayerTreeSettings() {
  cc::LayerTreeSettings settings;

  // For web contents, layer transforms should scale up the contents of layers
  // to keep content always crisp when possible.
  settings.layer_transforms_should_scale_layer_contents = true;

  return settings;
}

bool WebLayerTreeViewImplForTesting::HasLayer(const WebLayer& layer) {
  return layer.CcLayer()->GetLayerTreeHostForTesting() ==
         layer_tree_host_.get();
}

void WebLayerTreeViewImplForTesting::SetRootLayer(const blink::WebLayer& root) {
  layer_tree_host_->SetRootLayer(
      static_cast<const cc_blink::WebLayerImpl*>(&root)->layer());
}

void WebLayerTreeViewImplForTesting::ClearRootLayer() {
  layer_tree_host_->SetRootLayer(scoped_refptr<cc::Layer>());
}

cc::AnimationHost* WebLayerTreeViewImplForTesting::CompositorAnimationHost() {
  return animation_host_.get();
}

void WebLayerTreeViewImplForTesting::SetViewportSize(
    const WebSize& unused_deprecated,
    const WebSize& device_viewport_size) {
  gfx::Size gfx_size(std::max(0, device_viewport_size.width),
                     std::max(0, device_viewport_size.height));
  layer_tree_host_->SetViewportSize(gfx_size);
}

void WebLayerTreeViewImplForTesting::SetViewportSize(
    const WebSize& device_viewport_size) {
  gfx::Size gfx_size(std::max(0, device_viewport_size.width),
                     std::max(0, device_viewport_size.height));
  layer_tree_host_->SetViewportSize(gfx_size);
}

WebSize WebLayerTreeViewImplForTesting::GetViewportSize() const {
  return WebSize(layer_tree_host_->device_viewport_size().width(),
                 layer_tree_host_->device_viewport_size().height());
}

void WebLayerTreeViewImplForTesting::SetDeviceScaleFactor(
    float device_scale_factor) {
  layer_tree_host_->SetDeviceScaleFactor(device_scale_factor);
}

void WebLayerTreeViewImplForTesting::SetBackgroundColor(WebColor color) {
  layer_tree_host_->set_background_color(color);
  layer_tree_host_->set_has_transparent_background(SkColorGetA(color) <
                                                   SK_AlphaOPAQUE);
}

void WebLayerTreeViewImplForTesting::SetVisible(bool visible) {
  layer_tree_host_->SetVisible(visible);
}

void WebLayerTreeViewImplForTesting::SetPageScaleFactorAndLimits(
    float page_scale_factor,
    float minimum,
    float maximum) {
  layer_tree_host_->SetPageScaleFactorAndLimits(page_scale_factor, minimum,
                                                maximum);
}

void WebLayerTreeViewImplForTesting::StartPageScaleAnimation(
    const blink::WebPoint& scroll,
    bool use_anchor,
    float new_page_scale,
    double duration_sec) {}

void WebLayerTreeViewImplForTesting::SetNeedsBeginFrame() {
  layer_tree_host_->SetNeedsAnimate();
}

void WebLayerTreeViewImplForTesting::DidStopFlinging() {}

void WebLayerTreeViewImplForTesting::SetDeferCommits(bool defer_commits) {
  layer_tree_host_->SetDeferCommits(defer_commits);
}

void WebLayerTreeViewImplForTesting::UpdateLayerTreeHost() {}

void WebLayerTreeViewImplForTesting::ApplyViewportDeltas(
    const gfx::Vector2dF& inner_delta,
    const gfx::Vector2dF& outer_delta,
    const gfx::Vector2dF& elastic_overscroll_delta,
    float page_scale,
    float browser_controls_delta) {}

void WebLayerTreeViewImplForTesting::RecordWheelAndTouchScrollingCount(
    bool has_scrolled_by_wheel,
    bool has_scrolled_by_touch) {}

void WebLayerTreeViewImplForTesting::RequestNewCompositorFrameSink() {
  // Intentionally do not create and set an CompositorFrameSink.
}

void WebLayerTreeViewImplForTesting::DidFailToInitializeCompositorFrameSink() {
  NOTREACHED();
}

void WebLayerTreeViewImplForTesting::RegisterViewportLayers(
    const blink::WebLayer* overscroll_elasticity_layer,
    const blink::WebLayer* page_scale_layer,
    const blink::WebLayer* inner_viewport_container_layer,
    const blink::WebLayer* outer_viewport_container_layer,
    const blink::WebLayer* inner_viewport_scroll_layer,
    const blink::WebLayer* outer_viewport_scroll_layer) {
  cc::LayerTreeHost::ViewportLayers viewport_layers;
  if (overscroll_elasticity_layer) {
    viewport_layers.overscroll_elasticity =
        static_cast<const cc_blink::WebLayerImpl*>(overscroll_elasticity_layer)
            ->layer();
  }
  viewport_layers.page_scale =
      static_cast<const cc_blink::WebLayerImpl*>(page_scale_layer)->layer();
  if (inner_viewport_container_layer) {
    viewport_layers.inner_viewport_container =
        static_cast<const cc_blink::WebLayerImpl*>(
            inner_viewport_container_layer)
            ->layer();
  }
  if (outer_viewport_container_layer) {
    viewport_layers.outer_viewport_container =
        static_cast<const cc_blink::WebLayerImpl*>(
            outer_viewport_container_layer)
            ->layer();
  }
  viewport_layers.inner_viewport_scroll =
      static_cast<const cc_blink::WebLayerImpl*>(inner_viewport_scroll_layer)
          ->layer();
  if (outer_viewport_scroll_layer) {
    viewport_layers.outer_viewport_scroll =
        static_cast<const cc_blink::WebLayerImpl*>(outer_viewport_scroll_layer)
            ->layer();
  }
  layer_tree_host_->RegisterViewportLayers(viewport_layers);
}

void WebLayerTreeViewImplForTesting::ClearViewportLayers() {
  layer_tree_host_->RegisterViewportLayers(cc::LayerTreeHost::ViewportLayers());
}

void WebLayerTreeViewImplForTesting::RegisterSelection(
    const blink::WebSelection& selection) {}

void WebLayerTreeViewImplForTesting::ClearSelection() {}

void WebLayerTreeViewImplForTesting::SetEventListenerProperties(
    blink::WebEventListenerClass event_class,
    blink::WebEventListenerProperties properties) {
  // Equality of static_cast is checked in render_widget_compositor.cc.
  layer_tree_host_->SetEventListenerProperties(
      static_cast<cc::EventListenerClass>(event_class),
      static_cast<cc::EventListenerProperties>(properties));
}

blink::WebEventListenerProperties
WebLayerTreeViewImplForTesting::EventListenerProperties(
    blink::WebEventListenerClass event_class) const {
  // Equality of static_cast is checked in render_widget_compositor.cc.
  return static_cast<blink::WebEventListenerProperties>(
      layer_tree_host_->event_listener_properties(
          static_cast<cc::EventListenerClass>(event_class)));
}

void WebLayerTreeViewImplForTesting::SetHaveScrollEventHandlers(
    bool have_eent_handlers) {
  layer_tree_host_->SetHaveScrollEventHandlers(have_eent_handlers);
}

bool WebLayerTreeViewImplForTesting::HaveScrollEventHandlers() const {
  return layer_tree_host_->have_scroll_event_handlers();
}

bool WebLayerTreeViewImplForTesting::IsForSubframe() {
  return false;
}

}  // namespace blink
