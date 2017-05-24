/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/page/scrolling/ScrollingCoordinator.h"

#include "core/dom/Document.h"
#include "core/dom/Fullscreen.h"
#include "core/dom/Node.h"
#include "core/frame/EventHandlerRegistry.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/frame/VisualViewport.h"
#include "core/html/HTMLElement.h"
#include "core/layout/LayoutGeometryMap.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/api/LayoutPartItem.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/compositing/CompositedLayerMapping.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/page/ChromeClient.h"
#include "core/page/Page.h"
#include "core/plugins/PluginView.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/animation/CompositorAnimationHost.h"
#include "platform/animation/CompositorAnimationTimeline.h"
#include "platform/exported/WebScrollbarImpl.h"
#include "platform/exported/WebScrollbarThemeGeometryNative.h"
#include "platform/geometry/Region.h"
#include "platform/geometry/TransformState.h"
#include "platform/graphics/CompositorElementId.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#if OS(MACOSX)
#include "platform/mac/ScrollAnimatorMac.h"
#endif
#include <memory>
#include <utility>
#include "platform/scroll/MainThreadScrollingReason.h"
#include "platform/scroll/ScrollAnimatorBase.h"
#include "platform/scroll/ScrollbarTheme.h"
#include "platform/wtf/text/StringBuilder.h"
#include "public/platform/Platform.h"
#include "public/platform/WebCompositorSupport.h"
#include "public/platform/WebLayerPositionConstraint.h"
#include "public/platform/WebLayerTreeView.h"
#include "public/platform/WebScrollbarLayer.h"
#include "public/platform/WebScrollbarThemeGeometry.h"
#include "public/platform/WebScrollbarThemePainter.h"

using blink::WebLayer;
using blink::WebLayerPositionConstraint;
using blink::WebRect;
using blink::WebScrollbarLayer;
using blink::WebVector;

namespace {

WebLayer* toWebLayer(blink::GraphicsLayer* layer) {
  return layer ? layer->PlatformLayer() : nullptr;
}

}  // namespace

namespace blink {

ScrollingCoordinator* ScrollingCoordinator::Create(Page* page) {
  return new ScrollingCoordinator(page);
}

ScrollingCoordinator::ScrollingCoordinator(Page* page)
    : page_(page),
      scroll_gesture_region_is_dirty_(false),
      touch_event_target_rects_are_dirty_(false),
      should_scroll_on_main_thread_dirty_(false),
      was_frame_scrollable_(false),
      last_main_thread_scrolling_reasons_(0) {}

ScrollingCoordinator::~ScrollingCoordinator() {
  DCHECK(!page_);
}

DEFINE_TRACE(ScrollingCoordinator) {
  visitor->Trace(page_);
  visitor->Trace(horizontal_scrollbars_);
  visitor->Trace(vertical_scrollbars_);
}

void ScrollingCoordinator::SetShouldHandleScrollGestureOnMainThreadRegion(
    const Region& region) {
  if (!page_->MainFrame()->IsLocalFrame() ||
      !page_->DeprecatedLocalMainFrame()->View())
    return;
  if (WebLayer* scroll_layer = toWebLayer(page_->DeprecatedLocalMainFrame()
                                              ->View()
                                              ->LayoutViewportScrollableArea()
                                              ->LayerForScrolling())) {
    Vector<IntRect> rects = region.Rects();
    WebVector<WebRect> web_rects(rects.size());
    for (size_t i = 0; i < rects.size(); ++i)
      web_rects[i] = rects[i];
    scroll_layer->SetNonFastScrollableRegion(web_rects);
  }
}

void ScrollingCoordinator::NotifyGeometryChanged() {
  scroll_gesture_region_is_dirty_ = true;
  touch_event_target_rects_are_dirty_ = true;
  should_scroll_on_main_thread_dirty_ = true;
}

void ScrollingCoordinator::NotifyTransformChanged(const LayoutBox& box) {
  DCHECK(page_);
  if (!page_->MainFrame()->IsLocalFrame() ||
      !page_->DeprecatedLocalMainFrame()->View())
    return;

  if (page_->DeprecatedLocalMainFrame()->View()->NeedsLayout())
    return;

  for (PaintLayer* layer = box.EnclosingLayer(); layer;
       layer = layer->Parent()) {
    if (layers_with_touch_rects_.Contains(layer)) {
      touch_event_target_rects_are_dirty_ = true;
      return;
    }
  }
}
void ScrollingCoordinator::NotifyOverflowUpdated() {
  scroll_gesture_region_is_dirty_ = true;
}

void ScrollingCoordinator::FrameViewVisibilityDidChange() {
  scroll_gesture_region_is_dirty_ = true;
}

void ScrollingCoordinator::ScrollableAreasDidChange() {
  DCHECK(page_);
  if (!page_->MainFrame()->IsLocalFrame() ||
      !page_->DeprecatedLocalMainFrame()->View())
    return;

  // Layout may update scrollable area bounding boxes. It also sets the same
  // dirty flag making this one redundant (See
  // |ScrollingCoordinator::notifyGeometryChanged|).
  // So if layout is expected, ignore this call allowing scrolling coordinator
  // to be notified post-layout to recompute gesture regions.
  if (page_->DeprecatedLocalMainFrame()->View()->NeedsLayout())
    return;

  scroll_gesture_region_is_dirty_ = true;
}

void ScrollingCoordinator::UpdateAfterCompositingChangeIfNeeded() {
  if (!page_->MainFrame()->IsLocalFrame())
    return;

  if (!ShouldUpdateAfterCompositingChange())
    return;

  TRACE_EVENT0("input",
               "ScrollingCoordinator::updateAfterCompositingChangeIfNeeded");

  if (scroll_gesture_region_is_dirty_) {
    // Compute the region of the page where we can't handle scroll gestures and
    // mousewheel events
    // on the impl thread. This currently includes:
    // 1. All scrollable areas, such as subframes, overflow divs and list boxes,
    //    whose composited scrolling are not enabled. We need to do this even if
    //    the frame view whose layout was updated is not the main frame.
    // 2. Resize control areas, e.g. the small rect at the right bottom of
    //    div/textarea/iframe when CSS property "resize" is enabled.
    // 3. Plugin areas.
    Region should_handle_scroll_gesture_on_main_thread_region =
        ComputeShouldHandleScrollGestureOnMainThreadRegion(
            page_->DeprecatedLocalMainFrame());
    SetShouldHandleScrollGestureOnMainThreadRegion(
        should_handle_scroll_gesture_on_main_thread_region);
    scroll_gesture_region_is_dirty_ = false;
  }

  if (touch_event_target_rects_are_dirty_) {
    UpdateTouchEventTargetRectsIfNeeded();
    touch_event_target_rects_are_dirty_ = false;
  }

  FrameView* frame_view = ToLocalFrame(page_->MainFrame())->View();
  bool frame_is_scrollable =
      frame_view && frame_view->LayoutViewportScrollableArea()->IsScrollable();
  if (should_scroll_on_main_thread_dirty_ ||
      was_frame_scrollable_ != frame_is_scrollable) {
    SetShouldUpdateScrollLayerPositionOnMainThread(
        frame_view->GetMainThreadScrollingReasons());

    // Need to update scroll on main thread reasons for subframe because
    // subframe (e.g. iframe with background-attachment:fixed) should
    // scroll on main thread while the main frame scrolls on impl.
    frame_view->UpdateSubFrameScrollOnMainReason(*(page_->MainFrame()), 0);
    should_scroll_on_main_thread_dirty_ = false;
  }
  was_frame_scrollable_ = frame_is_scrollable;

  if (WebLayer* layout_viewport_scroll_layer =
          frame_view ? toWebLayer(frame_view->LayoutViewportScrollableArea()
                                      ->LayerForScrolling())
                     : nullptr) {
    if (!RuntimeEnabledFeatures::rootLayerScrollingEnabled())
      layout_viewport_scroll_layer->SetBounds(frame_view->ContentsSize());

    // If there is a non-root fullscreen element, prevent the viewport from
    // scrolling.
    Document* main_frame_document =
        page_->DeprecatedLocalMainFrame()->GetDocument();
    Element* fullscreen_element =
        Fullscreen::FullscreenElementFrom(*main_frame_document);
    WebLayer* visual_viewport_scroll_layer =
        toWebLayer(page_->GetVisualViewport().ScrollLayer());

    if (visual_viewport_scroll_layer) {
      if (fullscreen_element &&
          fullscreen_element != main_frame_document->documentElement())
        visual_viewport_scroll_layer->SetUserScrollable(false, false);
      else
        visual_viewport_scroll_layer->SetUserScrollable(true, true);
    }

    if (!RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
      layout_viewport_scroll_layer->SetUserScrollable(
          frame_view->UserInputScrollable(kHorizontalScrollbar),
          frame_view->UserInputScrollable(kVerticalScrollbar));
    }
  }

  if (!RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
    const FrameTree& tree = page_->MainFrame()->Tree();
    for (const Frame* child = tree.FirstChild(); child;
         child = child->Tree().NextSibling()) {
      if (!child->IsLocalFrame())
        continue;
      FrameView* frame_view = ToLocalFrame(child)->View();
      if (!frame_view || frame_view->ShouldThrottleRendering())
        continue;
      if (WebLayer* scroll_layer = toWebLayer(frame_view->LayerForScrolling()))
        scroll_layer->SetBounds(frame_view->ContentsSize());
    }
  }
}

void ScrollingCoordinator::SetLayerIsContainerForFixedPositionLayers(
    GraphicsLayer* layer,
    bool enable) {
  if (WebLayer* scrollable_layer = toWebLayer(layer))
    scrollable_layer->SetIsContainerForFixedPositionLayers(enable);
}

static void ClearPositionConstraintExceptForLayer(GraphicsLayer* layer,
                                                  GraphicsLayer* except) {
  if (layer && layer != except && toWebLayer(layer))
    toWebLayer(layer)->SetPositionConstraint(WebLayerPositionConstraint());
}

static WebLayerPositionConstraint ComputePositionConstraint(
    const PaintLayer* layer) {
  DCHECK(layer->HasCompositedLayerMapping());
  do {
    if (layer->GetLayoutObject().Style()->GetPosition() == EPosition::kFixed) {
      const LayoutObject& fixed_position_object = layer->GetLayoutObject();
      bool fixed_to_right = !fixed_position_object.Style()->Right().IsAuto();
      bool fixed_to_bottom = !fixed_position_object.Style()->Bottom().IsAuto();
      return WebLayerPositionConstraint::FixedPosition(fixed_to_right,
                                                       fixed_to_bottom);
    }

    layer = layer->Parent();

    // Composited layers that inherit a fixed position state will be positioned
    // with respect to the nearest compositedLayerMapping's GraphicsLayer.
    // So, once we find a layer that has its own compositedLayerMapping, we can
    // stop searching for a fixed position LayoutObject.
  } while (layer && !layer->HasCompositedLayerMapping());
  return WebLayerPositionConstraint();
}

void ScrollingCoordinator::UpdateLayerPositionConstraint(PaintLayer* layer) {
  DCHECK(layer->HasCompositedLayerMapping());
  CompositedLayerMapping* composited_layer_mapping =
      layer->GetCompositedLayerMapping();
  GraphicsLayer* main_layer = composited_layer_mapping->ChildForSuperlayers();

  // Avoid unnecessary commits
  ClearPositionConstraintExceptForLayer(
      composited_layer_mapping->SquashingContainmentLayer(), main_layer);
  ClearPositionConstraintExceptForLayer(
      composited_layer_mapping->AncestorClippingLayer(), main_layer);
  ClearPositionConstraintExceptForLayer(
      composited_layer_mapping->MainGraphicsLayer(), main_layer);

  if (WebLayer* scrollable_layer = toWebLayer(main_layer))
    scrollable_layer->SetPositionConstraint(ComputePositionConstraint(layer));
}

void ScrollingCoordinator::WillDestroyScrollableArea(
    ScrollableArea* scrollable_area) {
  RemoveWebScrollbarLayer(scrollable_area, kHorizontalScrollbar);
  RemoveWebScrollbarLayer(scrollable_area, kVerticalScrollbar);
}

void ScrollingCoordinator::RemoveWebScrollbarLayer(
    ScrollableArea* scrollable_area,
    ScrollbarOrientation orientation) {
  ScrollbarMap& scrollbars = orientation == kHorizontalScrollbar
                                 ? horizontal_scrollbars_
                                 : vertical_scrollbars_;
  if (std::unique_ptr<WebScrollbarLayer> scrollbar_layer =
          scrollbars.Take(scrollable_area))
    GraphicsLayer::UnregisterContentsLayer(scrollbar_layer->Layer());
}

static uint64_t NextScrollbarId() {
  static ScrollbarId next_scrollbar_id = 0;
  return ++next_scrollbar_id;
}

static std::unique_ptr<WebScrollbarLayer> CreateScrollbarLayer(
    Scrollbar& scrollbar,
    float device_scale_factor) {
  ScrollbarTheme& theme = scrollbar.GetTheme();
  WebScrollbarThemePainter painter(theme, scrollbar, device_scale_factor);
  std::unique_ptr<WebScrollbarThemeGeometry> geometry(
      WebScrollbarThemeGeometryNative::Create(theme));

  std::unique_ptr<WebScrollbarLayer> scrollbar_layer;
  if (theme.UsesOverlayScrollbars() && theme.UsesNinePatchThumbResource()) {
    scrollbar_layer =
        Platform::Current()->CompositorSupport()->CreateOverlayScrollbarLayer(
            WebScrollbarImpl::Create(&scrollbar), painter, std::move(geometry));
    scrollbar_layer->SetElementId(CompositorElementIdFromScrollbarId(
        NextScrollbarId(), CompositorElementIdNamespace::kScrollbar));
  } else {
    scrollbar_layer =
        Platform::Current()->CompositorSupport()->CreateScrollbarLayer(
            WebScrollbarImpl::Create(&scrollbar), painter, std::move(geometry));
    scrollbar_layer->SetElementId(CompositorElementIdFromScrollbarId(
        NextScrollbarId(), CompositorElementIdNamespace::kScrollbar));
  }
  GraphicsLayer::RegisterContentsLayer(scrollbar_layer->Layer());
  return scrollbar_layer;
}

std::unique_ptr<WebScrollbarLayer>
ScrollingCoordinator::CreateSolidColorScrollbarLayer(
    ScrollbarOrientation orientation,
    int thumb_thickness,
    int track_start,
    bool is_left_side_vertical_scrollbar) {
  WebScrollbar::Orientation web_orientation =
      (orientation == kHorizontalScrollbar) ? WebScrollbar::kHorizontal
                                            : WebScrollbar::kVertical;
  std::unique_ptr<WebScrollbarLayer> scrollbar_layer =
      Platform::Current()->CompositorSupport()->CreateSolidColorScrollbarLayer(
          web_orientation, thumb_thickness, track_start,
          is_left_side_vertical_scrollbar);
  scrollbar_layer->SetElementId(CompositorElementIdFromScrollbarId(
      NextScrollbarId(), CompositorElementIdNamespace::kScrollbar));
  GraphicsLayer::RegisterContentsLayer(scrollbar_layer->Layer());
  return scrollbar_layer;
}

static void DetachScrollbarLayer(GraphicsLayer* scrollbar_graphics_layer) {
  DCHECK(scrollbar_graphics_layer);

  scrollbar_graphics_layer->SetContentsToPlatformLayer(nullptr);
  scrollbar_graphics_layer->SetDrawsContent(true);
}

static void SetupScrollbarLayer(GraphicsLayer* scrollbar_graphics_layer,
                                WebScrollbarLayer* scrollbar_layer,
                                WebLayer* scroll_layer) {
  DCHECK(scrollbar_graphics_layer);
  DCHECK(scrollbar_layer);

  if (!scroll_layer) {
    DetachScrollbarLayer(scrollbar_graphics_layer);
    return;
  }
  scrollbar_layer->SetScrollLayer(scroll_layer);
  scrollbar_graphics_layer->SetContentsToPlatformLayer(
      scrollbar_layer->Layer());
  scrollbar_graphics_layer->SetDrawsContent(false);
}

WebScrollbarLayer* ScrollingCoordinator::AddWebScrollbarLayer(
    ScrollableArea* scrollable_area,
    ScrollbarOrientation orientation,
    std::unique_ptr<WebScrollbarLayer> scrollbar_layer) {
  ScrollbarMap& scrollbars = orientation == kHorizontalScrollbar
                                 ? horizontal_scrollbars_
                                 : vertical_scrollbars_;
  return scrollbars.insert(scrollable_area, std::move(scrollbar_layer))
      .stored_value->value.get();
}

WebScrollbarLayer* ScrollingCoordinator::GetWebScrollbarLayer(
    ScrollableArea* scrollable_area,
    ScrollbarOrientation orientation) {
  ScrollbarMap& scrollbars = orientation == kHorizontalScrollbar
                                 ? horizontal_scrollbars_
                                 : vertical_scrollbars_;
  return scrollbars.at(scrollable_area);
}

void ScrollingCoordinator::ScrollableAreaScrollbarLayerDidChange(
    ScrollableArea* scrollable_area,
    ScrollbarOrientation orientation) {
  if (!page_ || !page_->MainFrame())
    return;

  bool is_main_frame = IsForMainFrame(scrollable_area);
  GraphicsLayer* scrollbar_graphics_layer =
      orientation == kHorizontalScrollbar
          ? scrollable_area->LayerForHorizontalScrollbar()
          : scrollable_area->LayerForVerticalScrollbar();

  if (scrollbar_graphics_layer) {
    Scrollbar& scrollbar = orientation == kHorizontalScrollbar
                               ? *scrollable_area->HorizontalScrollbar()
                               : *scrollable_area->VerticalScrollbar();
    if (scrollbar.IsCustomScrollbar()) {
      DetachScrollbarLayer(scrollbar_graphics_layer);
      scrollbar_graphics_layer->PlatformLayer()->AddMainThreadScrollingReasons(
          MainThreadScrollingReason::kCustomScrollbarScrolling);
      return;
    }

    // Invalidate custom scrollbar scrolling reason in case a custom
    // scrollbar becomes a non-custom one.
    scrollbar_graphics_layer->PlatformLayer()->ClearMainThreadScrollingReasons(
        MainThreadScrollingReason::kCustomScrollbarScrolling);
    WebScrollbarLayer* scrollbar_layer =
        GetWebScrollbarLayer(scrollable_area, orientation);
    if (!scrollbar_layer) {
      Settings* settings = page_->MainFrame()->GetSettings();

      std::unique_ptr<WebScrollbarLayer> web_scrollbar_layer;
      if (settings->GetUseSolidColorScrollbars()) {
        DCHECK(RuntimeEnabledFeatures::overlayScrollbarsEnabled());
        web_scrollbar_layer = CreateSolidColorScrollbarLayer(
            orientation, scrollbar.GetTheme().ThumbThickness(scrollbar),
            scrollbar.GetTheme().TrackPosition(scrollbar),
            scrollable_area->ShouldPlaceVerticalScrollbarOnLeft());
      } else {
        web_scrollbar_layer = CreateScrollbarLayer(
            scrollbar, page_->DeviceScaleFactorDeprecated());
      }
      scrollbar_layer = AddWebScrollbarLayer(scrollable_area, orientation,
                                             std::move(web_scrollbar_layer));
    }

    WebLayer* scroll_layer = toWebLayer(scrollable_area->LayerForScrolling());
    SetupScrollbarLayer(scrollbar_graphics_layer, scrollbar_layer,
                        scroll_layer);

    // Root layer non-overlay scrollbars should be marked opaque to disable
    // blending.
    bool is_opaque_scrollbar = !scrollbar.IsOverlayScrollbar();
    scrollbar_graphics_layer->SetContentsOpaque(is_main_frame &&
                                                is_opaque_scrollbar);
  } else {
    RemoveWebScrollbarLayer(scrollable_area, orientation);
  }
}

bool ScrollingCoordinator::ScrollableAreaScrollLayerDidChange(
    ScrollableArea* scrollable_area) {
  if (!page_ || !page_->MainFrame())
    return false;

  GraphicsLayer* scroll_layer = scrollable_area->LayerForScrolling();

  if (scroll_layer) {
    bool is_for_visual_viewport =
        scrollable_area == &page_->GetVisualViewport();
    scroll_layer->SetScrollableArea(scrollable_area, is_for_visual_viewport);
  }

  WebLayer* web_layer = toWebLayer(scrollable_area->LayerForScrolling());
  WebLayer* container_layer = toWebLayer(scrollable_area->LayerForContainer());
  if (web_layer) {
    web_layer->SetScrollClipLayer(container_layer);
    FloatPoint scroll_position(scrollable_area->ScrollOrigin() +
                               scrollable_area->GetScrollOffset());
    web_layer->SetScrollPosition(scroll_position);

    web_layer->SetBounds(scrollable_area->ContentsSize());
    bool can_scroll_x =
        scrollable_area->UserInputScrollable(kHorizontalScrollbar);
    bool can_scroll_y =
        scrollable_area->UserInputScrollable(kVerticalScrollbar);
    web_layer->SetUserScrollable(can_scroll_x, can_scroll_y);
  }
  if (WebScrollbarLayer* scrollbar_layer =
          GetWebScrollbarLayer(scrollable_area, kHorizontalScrollbar)) {
    GraphicsLayer* horizontal_scrollbar_layer =
        scrollable_area->LayerForHorizontalScrollbar();
    if (horizontal_scrollbar_layer)
      SetupScrollbarLayer(horizontal_scrollbar_layer, scrollbar_layer,
                          web_layer);
  }
  if (WebScrollbarLayer* scrollbar_layer =
          GetWebScrollbarLayer(scrollable_area, kVerticalScrollbar)) {
    GraphicsLayer* vertical_scrollbar_layer =
        scrollable_area->LayerForVerticalScrollbar();

    if (vertical_scrollbar_layer)
      SetupScrollbarLayer(vertical_scrollbar_layer, scrollbar_layer, web_layer);
  }

  // Update the viewport layer registration if the outer viewport may have
  // changed.
  if (RuntimeEnabledFeatures::rootLayerScrollingEnabled() &&
      IsForRootLayer(scrollable_area))
    page_->GetChromeClient().RegisterViewportLayers();

  CompositorAnimationTimeline* timeline;
  // FrameView::compositorAnimationTimeline() can indirectly return
  // m_programmaticScrollAnimatorTimeline if it does not have its own
  // timeline.
  if (scrollable_area->IsFrameView()) {
    timeline = ToFrameView(scrollable_area)->GetCompositorAnimationTimeline();
  } else if (scrollable_area->IsPaintLayerScrollableArea()) {
    timeline = ToPaintLayerScrollableArea(scrollable_area)
                   ->GetCompositorAnimationTimeline();
  } else {
    timeline = programmatic_scroll_animator_timeline_.get();
  }
  scrollable_area->LayerForScrollingDidChange(timeline);

  return !!web_layer;
}

using GraphicsLayerHitTestRects =
    WTF::HashMap<const GraphicsLayer*, Vector<LayoutRect>>;

// In order to do a DFS cross-frame walk of the Layer tree, we need to know
// which Layers have child frames inside of them. This computes a mapping for
// the current frame which we can consult while walking the layers of that
// frame.  Whenever we descend into a new frame, a new map will be created.
using LayerFrameMap =
    HeapHashMap<const PaintLayer*, HeapVector<Member<const LocalFrame>>>;
static void MakeLayerChildFrameMap(const LocalFrame* current_frame,
                                   LayerFrameMap* map) {
  map->clear();
  const FrameTree& tree = current_frame->Tree();
  for (const Frame* child = tree.FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (!child->IsLocalFrame())
      continue;
    const LayoutItem owner_layout_item = ToLocalFrame(child)->OwnerLayoutItem();
    if (owner_layout_item.IsNull())
      continue;
    const PaintLayer* containing_layer = owner_layout_item.EnclosingLayer();
    LayerFrameMap::iterator iter = map->find(containing_layer);
    if (iter == map->end())
      map->insert(containing_layer, HeapVector<Member<const LocalFrame>>())
          .stored_value->value.push_back(ToLocalFrame(child));
    else
      iter->value.push_back(ToLocalFrame(child));
  }
}

static void ProjectRectsToGraphicsLayerSpaceRecursive(
    const PaintLayer* cur_layer,
    const LayerHitTestRects& layer_rects,
    GraphicsLayerHitTestRects& graphics_rects,
    LayoutGeometryMap& geometry_map,
    HashSet<const PaintLayer*>& layers_with_rects,
    LayerFrameMap& layer_child_frame_map) {
  // If this layer is throttled, ignore it.
  if (cur_layer->GetLayoutObject().GetFrameView() &&
      cur_layer->GetLayoutObject().GetFrameView()->ShouldThrottleRendering())
    return;
  // Project any rects for the current layer
  LayerHitTestRects::const_iterator layer_iter = layer_rects.find(cur_layer);
  if (layer_iter != layer_rects.end()) {
    // Find the enclosing composited layer when it's in another document (for
    // non-composited iframes).
    const PaintLayer* composited_layer =
        layer_iter->key
            ->EnclosingLayerForPaintInvalidationCrossingFrameBoundaries();
    DCHECK(composited_layer);

    // Find the appropriate GraphicsLayer for the composited Layer.
    GraphicsLayer* graphics_layer =
        composited_layer->GraphicsLayerBacking(&cur_layer->GetLayoutObject());

    GraphicsLayerHitTestRects::iterator gl_iter =
        graphics_rects.find(graphics_layer);
    Vector<LayoutRect>* gl_rects;
    if (gl_iter == graphics_rects.end())
      gl_rects = &graphics_rects.insert(graphics_layer, Vector<LayoutRect>())
                      .stored_value->value;
    else
      gl_rects = &gl_iter->value;

    // Transform each rect to the co-ordinate space of the graphicsLayer.
    for (size_t i = 0; i < layer_iter->value.size(); ++i) {
      LayoutRect rect = layer_iter->value[i];
      if (composited_layer != cur_layer) {
        FloatQuad compositor_quad = geometry_map.MapToAncestor(
            FloatRect(rect), &composited_layer->GetLayoutObject());
        rect = LayoutRect(compositor_quad.BoundingBox());
        // If the enclosing composited layer itself is scrolled, we have to undo
        // the subtraction of its scroll offset since we want the offset
        // relative to the scrolling content, not the element itself.
        if (composited_layer->GetLayoutObject().HasOverflowClip())
          rect.Move(composited_layer->GetLayoutBox()->ScrolledContentOffset());
      }
      PaintLayer::MapRectInPaintInvalidationContainerToBacking(
          composited_layer->GetLayoutObject(), rect);
      rect.Move(-graphics_layer->OffsetFromLayoutObject());

      gl_rects->push_back(rect);
    }
  }

  // Walk child layers of interest
  for (const PaintLayer* child_layer = cur_layer->FirstChild(); child_layer;
       child_layer = child_layer->NextSibling()) {
    if (layers_with_rects.Contains(child_layer)) {
      geometry_map.PushMappingsToAncestor(child_layer, cur_layer);
      ProjectRectsToGraphicsLayerSpaceRecursive(
          child_layer, layer_rects, graphics_rects, geometry_map,
          layers_with_rects, layer_child_frame_map);
      geometry_map.PopMappingsToAncestor(cur_layer);
    }
  }

  // If this layer has any frames of interest as a child of it, walk those (with
  // an updated frame map).
  LayerFrameMap::iterator map_iter = layer_child_frame_map.find(cur_layer);
  if (map_iter != layer_child_frame_map.end()) {
    for (size_t i = 0; i < map_iter->value.size(); i++) {
      const LocalFrame* child_frame = map_iter->value[i];
      const PaintLayer* child_layer =
          child_frame->View()->GetLayoutViewItem().Layer();
      if (layers_with_rects.Contains(child_layer)) {
        LayerFrameMap new_layer_child_frame_map;
        MakeLayerChildFrameMap(child_frame, &new_layer_child_frame_map);
        geometry_map.PushMappingsToAncestor(child_layer, cur_layer);
        ProjectRectsToGraphicsLayerSpaceRecursive(
            child_layer, layer_rects, graphics_rects, geometry_map,
            layers_with_rects, new_layer_child_frame_map);
        geometry_map.PopMappingsToAncestor(cur_layer);
      }
    }
  }
}

static void ProjectRectsToGraphicsLayerSpace(
    LocalFrame* main_frame,
    const LayerHitTestRects& layer_rects,
    GraphicsLayerHitTestRects& graphics_rects) {
  TRACE_EVENT0("input",
               "ScrollingCoordinator::projectRectsToGraphicsLayerSpace");
  bool touch_handler_in_child_frame = false;

  // We have a set of rects per Layer, we need to map them to their bounding
  // boxes in their enclosing composited layer. To do this most efficiently
  // we'll walk the Layer tree using LayoutGeometryMap. First record all the
  // branches we should traverse in the tree (including all documents on the
  // page).
  HashSet<const PaintLayer*> layers_with_rects;
  for (const auto& layer_rect : layer_rects) {
    const PaintLayer* layer = layer_rect.key;
    do {
      if (!layers_with_rects.insert(layer).is_new_entry)
        break;

      if (layer->Parent()) {
        layer = layer->Parent();
      } else {
        LayoutItem parent_doc_layout_item =
            layer->GetLayoutObject().GetFrame()->OwnerLayoutItem();
        if (!parent_doc_layout_item.IsNull()) {
          layer = parent_doc_layout_item.EnclosingLayer();
          touch_handler_in_child_frame = true;
        }
      }
    } while (layer);
  }

  // Now walk the layer projecting rects while maintaining a LayoutGeometryMap
  MapCoordinatesFlags flags = kUseTransforms;
  if (touch_handler_in_child_frame)
    flags |= kTraverseDocumentBoundaries;
  PaintLayer* root_layer = main_frame->ContentLayoutItem().Layer();
  LayoutGeometryMap geometry_map(flags);
  geometry_map.PushMappingsToAncestor(root_layer, 0);
  LayerFrameMap layer_child_frame_map;
  MakeLayerChildFrameMap(main_frame, &layer_child_frame_map);
  ProjectRectsToGraphicsLayerSpaceRecursive(
      root_layer, layer_rects, graphics_rects, geometry_map, layers_with_rects,
      layer_child_frame_map);
}

void ScrollingCoordinator::UpdateTouchEventTargetRectsIfNeeded() {
  TRACE_EVENT0("input",
               "ScrollingCoordinator::updateTouchEventTargetRectsIfNeeded");

  // TODO(chrishtr): implement touch event target rects for SPv2.
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled())
    return;

  LayerHitTestRects touch_event_target_rects;
  ComputeTouchEventTargetRects(touch_event_target_rects);
  SetTouchEventTargetRects(touch_event_target_rects);
}

void ScrollingCoordinator::Reset() {
  for (const auto& scrollbar : horizontal_scrollbars_)
    GraphicsLayer::UnregisterContentsLayer(scrollbar.value->Layer());
  for (const auto& scrollbar : vertical_scrollbars_)
    GraphicsLayer::UnregisterContentsLayer(scrollbar.value->Layer());

  horizontal_scrollbars_.clear();
  vertical_scrollbars_.clear();
  layers_with_touch_rects_.clear();
  was_frame_scrollable_ = false;

  last_main_thread_scrolling_reasons_ = 0;
  if (!RuntimeEnabledFeatures::rootLayerScrollingEnabled())
    SetShouldUpdateScrollLayerPositionOnMainThread(0);
}

// Note that in principle this could be called more often than
// computeTouchEventTargetRects, for example during a non-composited scroll
// (although that's not yet implemented - crbug.com/261307).
void ScrollingCoordinator::SetTouchEventTargetRects(
    LayerHitTestRects& layer_rects) {
  TRACE_EVENT0("input", "ScrollingCoordinator::setTouchEventTargetRects");

  // Ensure we have an entry for each composited layer that previously had rects
  // (so that old ones will get cleared out). Note that ideally we'd track this
  // on GraphicsLayer instead of Layer, but we have no good hook into the
  // lifetime of a GraphicsLayer.
  GraphicsLayerHitTestRects graphics_layer_rects;
  for (const PaintLayer* layer : layers_with_touch_rects_) {
    if (layer->GetLayoutObject().GetFrameView() &&
        layer->GetLayoutObject().GetFrameView()->ShouldThrottleRendering()) {
      continue;
    }
    GraphicsLayer* main_graphics_layer =
        layer->GraphicsLayerBacking(&layer->GetLayoutObject());
    if (main_graphics_layer)
      graphics_layer_rects.insert(main_graphics_layer, Vector<LayoutRect>());
    GraphicsLayer* scrolling_contents_layer = layer->GraphicsLayerBacking();
    if (scrolling_contents_layer &&
        scrolling_contents_layer != main_graphics_layer)
      graphics_layer_rects.insert(scrolling_contents_layer,
                                  Vector<LayoutRect>());
  }

  layers_with_touch_rects_.clear();
  for (const auto& layer_rect : layer_rects) {
    if (!layer_rect.value.IsEmpty()) {
      const PaintLayer* composited_layer =
          layer_rect.key
              ->EnclosingLayerForPaintInvalidationCrossingFrameBoundaries();
      DCHECK(composited_layer);
      layers_with_touch_rects_.insert(composited_layer);
    }
  }

  ProjectRectsToGraphicsLayerSpace(page_->DeprecatedLocalMainFrame(),
                                   layer_rects, graphics_layer_rects);

  for (const auto& layer_rect : graphics_layer_rects) {
    const GraphicsLayer* graphics_layer = layer_rect.key;
    WebVector<WebTouchInfo> touch(layer_rect.value.size());
    for (size_t i = 0; i < layer_rect.value.size(); ++i) {
      touch[i].rect = EnclosingIntRect(layer_rect.value[i]);
      // TODO(xidachen): route the real value here
      touch[i].touch_action = TouchAction::kTouchActionNone;
    }
    graphics_layer->PlatformLayer()->SetTouchEventHandlerRegion(touch);
  }
}

void ScrollingCoordinator::TouchEventTargetRectsDidChange() {
  DCHECK(page_);
  if (!page_->MainFrame()->IsLocalFrame() ||
      !page_->DeprecatedLocalMainFrame()->View())
    return;

  // Wait until after layout to update.
  if (page_->DeprecatedLocalMainFrame()->View()->NeedsLayout())
    return;

  // FIXME: scheduleAnimation() is just a method of forcing the compositor to
  // realize that it needs to commit here. We should expose a cleaner API for
  // this.
  LayoutViewItem layout_view =
      page_->DeprecatedLocalMainFrame()->ContentLayoutItem();
  if (!layout_view.IsNull() && layout_view.Compositor() &&
      layout_view.Compositor()->StaleInCompositingMode())
    page_->DeprecatedLocalMainFrame()->View()->ScheduleAnimation();

  touch_event_target_rects_are_dirty_ = true;
}

void ScrollingCoordinator::UpdateScrollParentForGraphicsLayer(
    GraphicsLayer* child,
    const PaintLayer* parent) {
  WebLayer* scroll_parent_web_layer = nullptr;
  if (parent && parent->HasCompositedLayerMapping())
    scroll_parent_web_layer = toWebLayer(
        parent->GetCompositedLayerMapping()->ScrollingContentsLayer());

  child->SetScrollParent(scroll_parent_web_layer);
}

void ScrollingCoordinator::UpdateClipParentForGraphicsLayer(
    GraphicsLayer* child,
    const PaintLayer* parent) {
  WebLayer* clip_parent_web_layer = nullptr;
  if (parent && parent->HasCompositedLayerMapping())
    clip_parent_web_layer =
        toWebLayer(parent->GetCompositedLayerMapping()->ParentForSublayers());

  child->SetClipParent(clip_parent_web_layer);
}

void ScrollingCoordinator::WillDestroyLayer(PaintLayer* layer) {
  layers_with_touch_rects_.erase(layer);
}

void ScrollingCoordinator::SetShouldUpdateScrollLayerPositionOnMainThread(
    MainThreadScrollingReasons main_thread_scrolling_reasons) {
  if (!page_->MainFrame()->IsLocalFrame() ||
      !page_->DeprecatedLocalMainFrame()->View())
    return;

  GraphicsLayer* visual_viewport_layer =
      page_->GetVisualViewport().ScrollLayer();
  WebLayer* visual_viewport_scroll_layer = toWebLayer(visual_viewport_layer);
  GraphicsLayer* layer = page_->DeprecatedLocalMainFrame()
                             ->View()
                             ->LayoutViewportScrollableArea()
                             ->LayerForScrolling();
  if (WebLayer* scroll_layer = toWebLayer(layer)) {
    last_main_thread_scrolling_reasons_ = main_thread_scrolling_reasons;
    if (main_thread_scrolling_reasons) {
      ScrollableArea* scrollable_area = layer->GetScrollableArea();
      if (scrollable_area) {
        if (ScrollAnimatorBase* scroll_animator =
                scrollable_area->ExistingScrollAnimator()) {
          DCHECK(RuntimeEnabledFeatures::slimmingPaintV2Enabled() ||
                 page_->DeprecatedLocalMainFrame()
                         ->GetDocument()
                         ->Lifecycle()
                         .GetState() >= DocumentLifecycle::kCompositingClean);
          scroll_animator->TakeOverCompositorAnimation();
        }
      }
      scroll_layer->AddMainThreadScrollingReasons(
          main_thread_scrolling_reasons);
      if (visual_viewport_scroll_layer) {
        if (ScrollAnimatorBase* scroll_animator =
                visual_viewport_layer->GetScrollableArea()
                    ->ExistingScrollAnimator()) {
          DCHECK(RuntimeEnabledFeatures::slimmingPaintV2Enabled() ||
                 page_->DeprecatedLocalMainFrame()
                         ->GetDocument()
                         ->Lifecycle()
                         .GetState() >= DocumentLifecycle::kCompositingClean);
          scroll_animator->TakeOverCompositorAnimation();
        }
        visual_viewport_scroll_layer->AddMainThreadScrollingReasons(
            main_thread_scrolling_reasons);
      }
    } else {
      // Clear all main thread scrolling reasons except the one that's set
      // if there is a running scroll animation.
      uint32_t main_thread_scrolling_reasons_to_clear = ~0u;
      main_thread_scrolling_reasons_to_clear &=
          ~MainThreadScrollingReason::kHandlingScrollFromMainThread;
      scroll_layer->ClearMainThreadScrollingReasons(
          main_thread_scrolling_reasons_to_clear);
      if (visual_viewport_scroll_layer)
        visual_viewport_scroll_layer->ClearMainThreadScrollingReasons(
            main_thread_scrolling_reasons_to_clear);
    }
  }
}

void ScrollingCoordinator::LayerTreeViewInitialized(
    WebLayerTreeView& layer_tree_view,
    FrameView* view) {
  if (Platform::Current()->IsThreadedAnimationEnabled() &&
      layer_tree_view.CompositorAnimationHost()) {
    std::unique_ptr<CompositorAnimationTimeline> timeline =
        CompositorAnimationTimeline::Create();
    std::unique_ptr<CompositorAnimationHost> host =
        WTF::MakeUnique<CompositorAnimationHost>(
            layer_tree_view.CompositorAnimationHost());
    if (view && view->GetFrame().LocalFrameRoot() != page_->MainFrame()) {
      view->SetAnimationHost(std::move(host));
      view->SetAnimationTimeline(std::move(timeline));
      view->GetCompositorAnimationHost()->AddTimeline(
          *view->GetCompositorAnimationTimeline());
    } else {
      animation_host_ = std::move(host);
      programmatic_scroll_animator_timeline_ = std::move(timeline);
      animation_host_->AddTimeline(
          *programmatic_scroll_animator_timeline_.get());
    }
  }
}

void ScrollingCoordinator::WillCloseLayerTreeView(
    WebLayerTreeView& layer_tree_view,
    FrameView* view) {
  if (view && view->GetFrame().LocalFrameRoot() != page_->MainFrame()) {
    view->GetCompositorAnimationHost()->RemoveTimeline(
        *view->GetCompositorAnimationTimeline());
    view->SetAnimationTimeline(nullptr);
    view->SetAnimationHost(nullptr);
  } else if (programmatic_scroll_animator_timeline_) {
    animation_host_->RemoveTimeline(
        *programmatic_scroll_animator_timeline_.get());
    programmatic_scroll_animator_timeline_ = nullptr;
    animation_host_ = nullptr;
  }
}

void ScrollingCoordinator::WillBeDestroyed() {
  DCHECK(page_);

  page_ = nullptr;
  for (const auto& scrollbar : horizontal_scrollbars_)
    GraphicsLayer::UnregisterContentsLayer(scrollbar.value->Layer());
  for (const auto& scrollbar : vertical_scrollbars_)
    GraphicsLayer::UnregisterContentsLayer(scrollbar.value->Layer());
}

bool ScrollingCoordinator::CoordinatesScrollingForFrameView(
    FrameView* frame_view) const {
  DCHECK(IsMainThread());

  // We currently only support composited mode.
  LayoutViewItem layout_view = frame_view->GetFrame().ContentLayoutItem();
  if (layout_view.IsNull())
    return false;
  return layout_view.UsesCompositing();
}

Region ScrollingCoordinator::ComputeShouldHandleScrollGestureOnMainThreadRegion(
    const LocalFrame* frame) const {
  Region should_handle_scroll_gesture_on_main_thread_region;
  FrameView* frame_view = frame->View();
  if (!frame_view || frame_view->ShouldThrottleRendering() ||
      !frame_view->IsVisible())
    return should_handle_scroll_gesture_on_main_thread_region;

  if (const FrameView::ScrollableAreaSet* scrollable_areas =
          frame_view->ScrollableAreas()) {
    for (const ScrollableArea* scrollable_area : *scrollable_areas) {
      if (scrollable_area->IsFrameView() &&
          ToFrameView(scrollable_area)->ShouldThrottleRendering())
        continue;
      // Composited scrollable areas can be scrolled off the main thread.
      if (scrollable_area->UsesCompositedScrolling())
        continue;
      IntRect box = scrollable_area->ScrollableAreaBoundingBox();
      should_handle_scroll_gesture_on_main_thread_region.Unite(box);
    }
  }

  // We use GestureScrollBegin/Update/End for moving the resizer handle. So we
  // mark these small resizer areas as non-fast-scrollable to allow the scroll
  // gestures to be passed to main thread if they are targeting the resizer
  // area. (Resizing is done in EventHandler.cpp on main thread).
  if (const FrameView::ResizerAreaSet* resizer_areas =
          frame_view->ResizerAreas()) {
    for (const LayoutBox* box : *resizer_areas) {
      PaintLayerScrollableArea* scrollable_area =
          box->Layer()->GetScrollableArea();
      IntRect bounds = box->AbsoluteBoundingBoxRect();
      // Get the corner in local coords.
      IntRect corner =
          scrollable_area->ResizerCornerRect(bounds, kResizerForTouch);
      // Map corner to top-frame coords.
      corner = scrollable_area->Box()
                   .LocalToAbsoluteQuad(FloatRect(corner),
                                        kTraverseDocumentBoundaries)
                   .EnclosingBoundingBox();
      should_handle_scroll_gesture_on_main_thread_region.Unite(corner);
    }
  }

  for (const auto& plugin : frame_view->Plugins()) {
    if (plugin->WantsWheelEvents()) {
      IntRect box = frame_view->ConvertToRootFrame(plugin->FrameRect());
      should_handle_scroll_gesture_on_main_thread_region.Unite(box);
    }
  }

  const FrameTree& tree = frame->Tree();
  for (Frame* sub_frame = tree.FirstChild(); sub_frame;
       sub_frame = sub_frame->Tree().NextSibling()) {
    if (sub_frame->IsLocalFrame()) {
      should_handle_scroll_gesture_on_main_thread_region.Unite(
          ComputeShouldHandleScrollGestureOnMainThreadRegion(
              ToLocalFrame(sub_frame)));
    }
  }

  return should_handle_scroll_gesture_on_main_thread_region;
}

static void AccumulateDocumentTouchEventTargetRects(LayerHitTestRects& rects,
                                                    const Document* document) {
  DCHECK(document);
  const EventTargetSet* targets =
      document->GetPage()->GetEventHandlerRegistry().EventHandlerTargets(
          EventHandlerRegistry::kTouchStartOrMoveEventBlocking);
  if (!targets)
    return;

  // If there's a handler on the window, document, html or body element (fairly
  // common in practice), then we can quickly mark the entire document and skip
  // looking at any other handlers.  Note that technically a handler on the body
  // doesn't cover the whole document, but it's reasonable to be conservative
  // and report the whole document anyway.
  //
  // Fullscreen HTML5 video when OverlayFullscreenVideo is enabled is
  // implemented by replacing the root cc::layer with the video layer so doing
  // this optimization causes the compositor to think that there are no
  // handlers, therefore skip it.
  if (!document->GetLayoutViewItem().Compositor()->InOverlayFullscreenVideo()) {
    for (const auto& event_target : *targets) {
      EventTarget* target = event_target.key;
      Node* node = target->ToNode();
      LocalDOMWindow* window = target->ToLocalDOMWindow();
      // If the target is inside a throttled frame, skip it.
      if (window && window->GetFrame()->View() &&
          window->GetFrame()->View()->ShouldThrottleRendering())
        continue;
      if (node && node->GetDocument().View() &&
          node->GetDocument().View()->ShouldThrottleRendering())
        continue;
      if (window || node == document || node == document->documentElement() ||
          node == document->body()) {
        if (LayoutViewItem layout_view = document->GetLayoutViewItem()) {
          layout_view.ComputeLayerHitTestRects(rects);
        }
        return;
      }
    }
  }

  for (const auto& event_target : *targets) {
    EventTarget* target = event_target.key;
    Node* node = target->ToNode();
    if (!node || !node->isConnected())
      continue;

    // If the document belongs to an invisible subframe it does not have a
    // composited layer and should be skipped.
    if (node->GetDocument().IsInInvisibleSubframe())
      continue;

    // If the node belongs to a throttled frame, skip it.
    if (node->GetDocument().View() &&
        node->GetDocument().View()->ShouldThrottleRendering())
      continue;

    if (node->IsDocumentNode() && node != document) {
      AccumulateDocumentTouchEventTargetRects(rects, ToDocument(node));
    } else if (LayoutObject* layout_object = node->GetLayoutObject()) {
      // If the set also contains one of our ancestor nodes then processing
      // this node would be redundant.
      bool has_touch_event_target_ancestor = false;
      for (Node& ancestor : NodeTraversal::AncestorsOf(*node)) {
        if (has_touch_event_target_ancestor)
          break;
        if (targets->Contains(&ancestor))
          has_touch_event_target_ancestor = true;
      }
      if (!has_touch_event_target_ancestor) {
        // Walk up the tree to the outermost non-composited scrollable layer.
        PaintLayer* enclosing_non_composited_scroll_layer = nullptr;
        for (PaintLayer* parent = layout_object->EnclosingLayer();
             parent && parent->GetCompositingState() == kNotComposited;
             parent = parent->Parent()) {
          if (parent->ScrollsOverflow())
            enclosing_non_composited_scroll_layer = parent;
        }

        // Report the whole non-composited scroll layer as a touch hit rect
        // because any rects inside of it may move around relative to their
        // enclosing composited layer without causing the rects to be
        // recomputed. Non-composited scrolling occurs on the main thread, so
        // we're not getting much benefit from compositor touch hit testing in
        // this case anyway.
        if (enclosing_non_composited_scroll_layer)
          enclosing_non_composited_scroll_layer->ComputeSelfHitTestRects(rects);

        layout_object->ComputeLayerHitTestRects(rects);
      }
    }
  }
}

void ScrollingCoordinator::ComputeTouchEventTargetRects(
    LayerHitTestRects& rects) {
  TRACE_EVENT0("input", "ScrollingCoordinator::computeTouchEventTargetRects");

  Document* document = page_->DeprecatedLocalMainFrame()->GetDocument();
  if (!document || !document->View())
    return;

  AccumulateDocumentTouchEventTargetRects(rects, document);
}

void ScrollingCoordinator::
    FrameViewHasBackgroundAttachmentFixedObjectsDidChange(
        FrameView* frame_view) {
  DCHECK(IsMainThread());
  DCHECK(page_);

  if (!CoordinatesScrollingForFrameView(frame_view))
    return;

  should_scroll_on_main_thread_dirty_ = true;
}

void ScrollingCoordinator::FrameViewFixedObjectsDidChange(
    FrameView* frame_view) {
  DCHECK(IsMainThread());
  DCHECK(page_);

  if (!CoordinatesScrollingForFrameView(frame_view))
    return;

  should_scroll_on_main_thread_dirty_ = true;
}

bool ScrollingCoordinator::IsForRootLayer(
    ScrollableArea* scrollable_area) const {
  if (!page_->MainFrame()->IsLocalFrame())
    return false;

  // FIXME(305811): Refactor for OOPI.
  LayoutViewItem layout_view_item =
      page_->DeprecatedLocalMainFrame()->View()->GetLayoutViewItem();
  return layout_view_item.IsNull()
             ? false
             : scrollable_area == layout_view_item.Layer()->GetScrollableArea();
}

bool ScrollingCoordinator::IsForMainFrame(
    ScrollableArea* scrollable_area) const {
  if (!page_->MainFrame()->IsLocalFrame())
    return false;

  // FIXME(305811): Refactor for OOPI.
  return scrollable_area == page_->DeprecatedLocalMainFrame()->View();
}

void ScrollingCoordinator::FrameViewRootLayerDidChange(FrameView* frame_view) {
  DCHECK(IsMainThread());
  DCHECK(page_);

  if (!CoordinatesScrollingForFrameView(frame_view))
    return;

  NotifyGeometryChanged();
}

bool ScrollingCoordinator::FrameScrollerIsDirty() const {
  FrameView* frame_view = page_->MainFrame()->IsLocalFrame()
                              ? page_->DeprecatedLocalMainFrame()->View()
                              : nullptr;
  bool frame_is_scrollable =
      frame_view && frame_view->LayoutViewportScrollableArea()->IsScrollable();
  if (frame_is_scrollable != was_frame_scrollable_)
    return true;

  if (WebLayer* scroll_layer =
          frame_view ? toWebLayer(frame_view->LayoutViewportScrollableArea()
                                      ->LayerForScrolling())
                     : nullptr) {
    return WebSize(
               frame_view->LayoutViewportScrollableArea()->ContentsSize()) !=
           scroll_layer->Bounds();
  }
  return false;
}

}  // namespace blink
