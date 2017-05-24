/*
 * Copyright (C) 1998, 1999 Torben Weis <weis@kde.org>
 *                     1999 Lars Knoll <knoll@kde.org>
 *                     1999 Antti Koivisto <koivisto@kde.org>
 *                     2000 Dirk Mueller <mueller@kde.org>
 * Copyright (C) 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
 *           (C) 2006 Graham Dennis (graham.dennis@gmail.com)
 *           (C) 2006 Alexey Proskuryakov (ap@nypop.com)
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "core/frame/FrameView.h"

#include <algorithm>
#include <memory>
#include "core/HTMLNames.h"
#include "core/MediaTypeNames.h"
#include "core/animation/DocumentAnimations.h"
#include "core/css/FontFaceSet.h"
#include "core/dom/AXObjectCache.h"
#include "core/dom/DOMNodeIds.h"
#include "core/dom/ElementVisibilityObserver.h"
#include "core/dom/Fullscreen.h"
#include "core/dom/IntersectionObserverCallback.h"
#include "core/dom/IntersectionObserverController.h"
#include "core/dom/IntersectionObserverInit.h"
#include "core/dom/ResizeObserverController.h"
#include "core/dom/StyleChangeReason.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/editing/DragCaret.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/RenderedPosition.h"
#include "core/editing/markers/DocumentMarkerController.h"
#include "core/events/ErrorEvent.h"
#include "core/frame/BrowserControls.h"
#include "core/frame/EventHandlerRegistry.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/Location.h"
#include "core/frame/PageScaleConstraintsSet.h"
#include "core/frame/RemoteFrame.h"
#include "core/frame/RemoteFrameView.h"
#include "core/frame/Settings.h"
#include "core/frame/VisualViewport.h"
#include "core/html/HTMLFrameElement.h"
#include "core/html/HTMLPlugInElement.h"
#include "core/html/TextControlElement.h"
#include "core/html/parser/TextResourceDecoder.h"
#include "core/input/EventHandler.h"
#include "core/inspector/InspectorTraceEvents.h"
#include "core/layout/LayoutAnalyzer.h"
#include "core/layout/LayoutCounter.h"
#include "core/layout/LayoutEmbeddedObject.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/LayoutScrollbar.h"
#include "core/layout/LayoutScrollbarPart.h"
#include "core/layout/LayoutView.h"
#include "core/layout/ScrollAlignment.h"
#include "core/layout/TextAutosizer.h"
#include "core/layout/TracedLayoutObject.h"
#include "core/layout/api/LayoutBoxModel.h"
#include "core/layout/api/LayoutItem.h"
#include "core/layout/api/LayoutPartItem.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/compositing/CompositedLayerMapping.h"
#include "core/layout/compositing/CompositedSelection.h"
#include "core/layout/compositing/CompositingInputsUpdater.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/layout/svg/LayoutSVGRoot.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/FrameLoader.h"
#include "core/page/AutoscrollController.h"
#include "core/page/ChromeClient.h"
#include "core/page/FocusController.h"
#include "core/page/FrameTree.h"
#include "core/page/Page.h"
#include "core/page/PrintContext.h"
#include "core/page/scrolling/RootScrollerUtil.h"
#include "core/page/scrolling/ScrollingCoordinator.h"
#include "core/page/scrolling/TopDocumentRootScrollerController.h"
#include "core/paint/BlockPaintInvalidator.h"
#include "core/paint/FramePainter.h"
#include "core/paint/PaintLayer.h"
#include "core/paint/PaintTiming.h"
#include "core/paint/PrePaintTreeWalk.h"
#include "core/plugins/PluginView.h"
#include "core/probe/CoreProbes.h"
#include "core/style/ComputedStyle.h"
#include "core/svg/SVGDocumentExtensions.h"
#include "core/svg/SVGSVGElement.h"
#include "platform/Histogram.h"
#include "platform/Language.h"
#include "platform/PlatformChromeClient.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/ScriptForbiddenScope.h"
#include "platform/WebFrameScheduler.h"
#include "platform/fonts/FontCache.h"
#include "platform/geometry/DoubleRect.h"
#include "platform/geometry/FloatRect.h"
#include "platform/geometry/LayoutRect.h"
#include "platform/geometry/TransformState.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/GraphicsLayerDebugInfo.h"
#include "platform/graphics/compositing/PaintArtifactCompositor.h"
#include "platform/graphics/paint/CullRect.h"
#include "platform/graphics/paint/PaintController.h"
#include "platform/graphics/paint/ScopedPaintChunkProperties.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/instrumentation/tracing/TracedValue.h"
#include "platform/json/JSONValues.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/scroll/ScrollAnimatorBase.h"
#include "platform/scroll/ScrollbarTheme.h"
#include "platform/scroll/ScrollerSizeMetrics.h"
#include "platform/text/TextStream.h"
#include "platform/wtf/CurrentTime.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/StdLibExtras.h"
#include "public/platform/WebDisplayItemList.h"

// Used to check for dirty layouts violating document lifecycle rules.
// If arg evaluates to true, the program will continue. If arg evaluates to
// false, program will crash if DCHECK_IS_ON() or return false from the current
// function.
#define CHECK_FOR_DIRTY_LAYOUT(arg) \
  do {                              \
    if (!(arg)) {                   \
      NOTREACHED();                 \
      return false;                 \
    }                               \
  } while (false)

namespace {

// Page dimensions in pixels at 72 DPI.
constexpr int kA4PortraitPageWidth = 595;
constexpr int kA4PortraitPageHeight = 842;
constexpr int kLetterPortraitPageWidth = 612;
constexpr int kLetterPortraitPageHeight = 792;

}  // namespace

namespace blink {

namespace {

void SetNeedsCompositingUpdate(blink::LayoutViewItem layout_view_item,
                               blink::CompositingUpdateType update_type) {
  if (PaintLayerCompositor* compositor =
          !layout_view_item.IsNull() ? layout_view_item.Compositor() : nullptr)
    compositor->SetNeedsCompositingUpdate(update_type);
}

}  // namespace

using namespace HTMLNames;

// The maximum number of updatePlugins iterations that should be done before
// returning.
static const unsigned kMaxUpdatePluginsIterations = 2;
static const double kResourcePriorityUpdateDelayAfterScroll = 0.250;

static bool g_initial_track_all_paint_invalidations = false;

FrameView::FrameView(LocalFrame& frame, IntRect frame_rect)
    : frame_(frame),
      frame_rect_(frame_rect),
      parent_(nullptr),
      display_mode_(kWebDisplayModeBrowser),
      can_have_scrollbars_(true),
      has_pending_layout_(false),
      in_synchronous_post_layout_(false),
      post_layout_tasks_timer_(
          TaskRunnerHelper::Get(TaskType::kUnspecedTimer, &frame),
          this,
          &FrameView::PostLayoutTimerFired),
      update_plugins_timer_(
          TaskRunnerHelper::Get(TaskType::kUnspecedTimer, &frame),
          this,
          &FrameView::UpdatePluginsTimerFired),
      base_background_color_(Color::kWhite),
      media_type_(MediaTypeNames::screen),
      safe_to_propagate_scroll_to_parent_(true),
      scroll_corner_(nullptr),
      sticky_position_object_count_(0),
      input_events_scale_factor_for_emulation_(1),
      layout_size_fixed_to_frame_size_(true),
      did_scroll_timer_(TaskRunnerHelper::Get(TaskType::kUnspecedTimer, &frame),
                        this,
                        &FrameView::DidScrollTimerFired),
      needs_update_geometries_(false),
      horizontal_scrollbar_mode_(kScrollbarAuto),
      vertical_scrollbar_mode_(kScrollbarAuto),
      horizontal_scrollbar_lock_(false),
      vertical_scrollbar_lock_(false),
      scrollbars_suppressed_(false),
      in_update_scrollbars_(false),
      frame_timing_requests_dirty_(true),
      hidden_for_throttling_(false),
      subtree_throttled_(false),
      lifecycle_updates_throttled_(false),
      needs_paint_property_update_(true),
      current_update_lifecycle_phases_target_state_(
          DocumentLifecycle::kUninitialized),
      scroll_anchor_(this),
      scrollbar_manager_(*this),
      needs_scrollbars_update_(false),
      suppress_adjust_view_size_(false),
      allows_layout_invalidation_after_layout_clean_(true),
      main_thread_scrolling_reasons_(0) {
  Init();
}

FrameView* FrameView::Create(LocalFrame& frame) {
  FrameView* view = new FrameView(frame, IntRect());
  view->Show();
  return view;
}

FrameView* FrameView::Create(LocalFrame& frame, const IntSize& initial_size) {
  FrameView* view = new FrameView(frame, IntRect(IntPoint(), initial_size));
  view->SetLayoutSizeInternal(initial_size);

  view->Show();
  return view;
}

FrameView::~FrameView() {
#if DCHECK_IS_ON()
  DCHECK(has_been_disposed_);
#endif
}

DEFINE_TRACE(FrameView) {
  visitor->Trace(frame_);
  visitor->Trace(parent_);
  visitor->Trace(fragment_anchor_);
  visitor->Trace(scrollable_areas_);
  visitor->Trace(animating_scrollable_areas_);
  visitor->Trace(auto_size_info_);
  visitor->Trace(plugins_);
  visitor->Trace(scrollbars_);
  visitor->Trace(viewport_scrollable_area_);
  visitor->Trace(visibility_observer_);
  visitor->Trace(scroll_anchor_);
  visitor->Trace(anchoring_adjustment_queue_);
  visitor->Trace(scrollbar_manager_);
  visitor->Trace(print_context_);
  ScrollableArea::Trace(visitor);
}

void FrameView::Reset() {
  // The compositor throttles the main frame using deferred commits, we can't
  // throttle it here or it seems the root compositor doesn't get setup
  // properly.
  if (RuntimeEnabledFeatures::
          renderingPipelineThrottlingLoadingIframesEnabled())
    lifecycle_updates_throttled_ = !GetFrame().IsMainFrame();
  has_pending_layout_ = false;
  layout_scheduling_enabled_ = true;
  in_synchronous_post_layout_ = false;
  layout_count_ = 0;
  nested_layout_count_ = 0;
  post_layout_tasks_timer_.Stop();
  update_plugins_timer_.Stop();
  first_layout_ = true;
  safe_to_propagate_scroll_to_parent_ = true;
  last_viewport_size_ = IntSize();
  last_zoom_factor_ = 1.0f;
  tracked_object_paint_invalidations_ =
      WTF::WrapUnique(g_initial_track_all_paint_invalidations
                          ? new Vector<ObjectPaintInvalidation>
                          : nullptr);
  visually_non_empty_character_count_ = 0;
  visually_non_empty_pixel_count_ = 0;
  is_visually_non_empty_ = false;
  main_thread_scrolling_reasons_ = 0;
  layout_object_counter_.Reset();
  ClearFragmentAnchor();
  viewport_constrained_objects_.reset();
  layout_subtree_root_list_.Clear();
  orthogonal_writing_mode_root_list_.Clear();
}

template <typename Function>
void FrameView::ForAllChildViewsAndPlugins(const Function& function) {
  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    // TODO(https://crbug.com/719439) This code would be simpler if
    // Frame::View() returned a common base class for FrameView and
    // RemoteFrameView.
    FrameOrPlugin* child_view = nullptr;
    if (child->IsLocalFrame())
      child_view = ToLocalFrame(child)->View();
    else if (child->IsRemoteFrame())
      child_view = ToRemoteFrame(child)->View();

    if (child_view)
      function(*child_view);
  }

  for (const auto& plugin : plugins_) {
    function(*plugin);
  }
}

template <typename Function>
void FrameView::ForAllChildFrameViews(const Function& function) {
  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (!child->IsLocalFrame())
      continue;
    if (FrameView* child_view = ToLocalFrame(child)->View())
      function(*child_view);
  }
}

// Call function for each non-throttled frame view in pre tree order.
// Note it needs a null check of the frame's layoutView to access it in case of
// detached frames.
template <typename Function>
void FrameView::ForAllNonThrottledFrameViews(const Function& function) {
  if (ShouldThrottleRendering())
    return;

  function(*this);

  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (!child->IsLocalFrame())
      continue;
    if (FrameView* child_view = ToLocalFrame(child)->View())
      child_view->ForAllNonThrottledFrameViews(function);
  }
}

void FrameView::Init() {
  Reset();

  size_ = LayoutSize();

  // Propagate the marginwidth/height and scrolling modes to the view.
  if (frame_->Owner() &&
      frame_->Owner()->ScrollingMode() == kScrollbarAlwaysOff)
    SetCanHaveScrollbars(false);
}

void FrameView::SetupRenderThrottling() {
  if (visibility_observer_)
    return;

  // We observe the frame owner element instead of the document element, because
  // if the document has no content we can falsely think the frame is invisible.
  // Note that this means we cannot throttle top-level frames or (currently)
  // frames whose owner element is remote.
  Element* target_element = GetFrame().DeprecatedLocalOwner();
  if (!target_element)
    return;

  visibility_observer_ = new ElementVisibilityObserver(
      target_element, WTF::Bind(
                          [](FrameView* frame_view, bool is_visible) {
                            if (!frame_view)
                              return;
                            frame_view->UpdateRenderThrottlingStatus(
                                !is_visible, frame_view->subtree_throttled_);
                          },
                          WrapWeakPersistent(this)));
  visibility_observer_->Start();
}

void FrameView::Dispose() {
  CHECK(!IsInPerformLayout());

  if (ScrollAnimatorBase* scroll_animator = ExistingScrollAnimator())
    scroll_animator->CancelAnimation();
  CancelProgrammaticScrollAnimation();

  DetachScrollbars();

  if (ScrollingCoordinator* scrolling_coordinator =
          this->GetScrollingCoordinator())
    scrolling_coordinator->WillDestroyScrollableArea(this);

  Page* page = frame_->GetPage();
  // TODO(dcheng): It's wrong that the frame can be detached before the
  // FrameView. Figure out what's going on and fix FrameView to be disposed with
  // the correct timing.
  if (page)
    page->GlobalRootScrollerController().DidDisposeScrollableArea(*this);

  // We need to clear the RootFrameViewport's animator since it gets called
  // from non-GC'd objects and RootFrameViewport will still have a pointer to
  // this class.
  if (viewport_scrollable_area_)
    viewport_scrollable_area_->ClearScrollableArea();

  ClearScrollableArea();

  // Destroy |m_autoSizeInfo| as early as possible, to avoid dereferencing
  // partially destroyed |this| via |m_autoSizeInfo->m_frameView|.
  auto_size_info_.Clear();

  post_layout_tasks_timer_.Stop();
  did_scroll_timer_.Stop();

  // FIXME: Do we need to do something here for OOPI?
  HTMLFrameOwnerElement* owner_element = frame_->DeprecatedLocalOwner();
  // TODO(dcheng): It seems buggy that we can have an owner element that points
  // to another FrameOrPlugin. This can happen when a plugin element loads a
  // frame (FrameOrPlugin A of type FrameView) and then loads a plugin
  // (FrameOrPlugin B of type WebPluginContainerImpl). In this case, the frame's
  // view is A and the frame element's OwnedWidget is B. See
  // https://crbug.com/673170 for an example.
  if (owner_element && owner_element->OwnedWidget() == this)
    owner_element->SetWidget(nullptr);

  ClearPrintContext();

#if DCHECK_IS_ON()
  has_been_disposed_ = true;
#endif
}

void FrameView::DetachScrollbars() {
  // Previously, we detached custom scrollbars as early as possible to prevent
  // Document::detachLayoutTree() from messing with the view such that its
  // scroll bars won't be torn down. However, scripting in
  // Document::detachLayoutTree() is forbidden
  // now, so it's not clear if these edge cases can still happen.
  // However, for Oilpan, we still need to remove the native scrollbars before
  // we lose the connection to the ChromeClient, so we just unconditionally
  // detach any scrollbars now.
  scrollbar_manager_.Dispose();

  if (scroll_corner_) {
    scroll_corner_->Destroy();
    scroll_corner_ = nullptr;
  }
}

void FrameView::ScrollbarManager::SetHasHorizontalScrollbar(
    bool has_scrollbar) {
  if (has_scrollbar == HasHorizontalScrollbar())
    return;

  if (has_scrollbar) {
    h_bar_ = CreateScrollbar(kHorizontalScrollbar);
    h_bar_is_attached_ = 1;
    scrollable_area_->DidAddScrollbar(*h_bar_, kHorizontalScrollbar);
    h_bar_->StyleChanged();
  } else {
    h_bar_is_attached_ = 0;
    DestroyScrollbar(kHorizontalScrollbar);
  }

  scrollable_area_->SetScrollCornerNeedsPaintInvalidation();
}

void FrameView::ScrollbarManager::SetHasVerticalScrollbar(bool has_scrollbar) {
  if (has_scrollbar == HasVerticalScrollbar())
    return;

  if (has_scrollbar) {
    v_bar_ = CreateScrollbar(kVerticalScrollbar);
    v_bar_is_attached_ = 1;
    scrollable_area_->DidAddScrollbar(*v_bar_, kVerticalScrollbar);
    v_bar_->StyleChanged();
  } else {
    v_bar_is_attached_ = 0;
    DestroyScrollbar(kVerticalScrollbar);
  }

  scrollable_area_->SetScrollCornerNeedsPaintInvalidation();
}

Scrollbar* FrameView::ScrollbarManager::CreateScrollbar(
    ScrollbarOrientation orientation) {
  Element* custom_scrollbar_element = nullptr;
  LayoutBox* box = scrollable_area_->GetLayoutBox();
  if (box->GetDocument().View()->ShouldUseCustomScrollbars(
          custom_scrollbar_element)) {
    return LayoutScrollbar::CreateCustomScrollbar(
        scrollable_area_.Get(), orientation, custom_scrollbar_element);
  }

  // Nobody set a custom style, so we just use a native scrollbar.
  return Scrollbar::Create(scrollable_area_.Get(), orientation,
                           kRegularScrollbar,
                           &box->GetFrame()->GetPage()->GetChromeClient());
}

void FrameView::ScrollbarManager::DestroyScrollbar(
    ScrollbarOrientation orientation) {
  Member<Scrollbar>& scrollbar =
      orientation == kHorizontalScrollbar ? h_bar_ : v_bar_;
  DCHECK(orientation == kHorizontalScrollbar ? !h_bar_is_attached_
                                             : !v_bar_is_attached_);
  if (!scrollbar)
    return;

  scrollable_area_->WillRemoveScrollbar(*scrollbar, orientation);
  scrollbar->DisconnectFromScrollableArea();
  scrollbar = nullptr;
}

void FrameView::RecalculateCustomScrollbarStyle() {
  bool did_style_change = false;
  if (HorizontalScrollbar() && HorizontalScrollbar()->IsCustomScrollbar()) {
    HorizontalScrollbar()->StyleChanged();
    did_style_change = true;
  }
  if (VerticalScrollbar() && VerticalScrollbar()->IsCustomScrollbar()) {
    VerticalScrollbar()->StyleChanged();
    did_style_change = true;
  }
  if (did_style_change) {
    UpdateScrollbarGeometry();
    UpdateScrollCorner();
    PositionScrollbarLayers();
  }
}

void FrameView::InvalidateAllCustomScrollbarsOnActiveChanged() {
  bool uses_window_inactive_selector =
      frame_->GetDocument()->GetStyleEngine().UsesWindowInactiveSelector();

  ForAllChildFrameViews([](FrameView& frame_view) {
    frame_view.InvalidateAllCustomScrollbarsOnActiveChanged();
  });

  for (const auto& scrollbar : scrollbars_) {
    if (uses_window_inactive_selector && scrollbar->IsCustomScrollbar())
      scrollbar->StyleChanged();
  }

  if (uses_window_inactive_selector)
    RecalculateCustomScrollbarStyle();
}

void FrameView::Clear() {
  Reset();
  SetScrollbarsSuppressed(true);
}

bool FrameView::DidFirstLayout() const {
  return !first_layout_;
}

void FrameView::InvalidateRect(const IntRect& rect) {
  LayoutPartItem layout_item = frame_->OwnerLayoutItem();
  if (layout_item.IsNull())
    return;

  IntRect paint_invalidation_rect = rect;
  paint_invalidation_rect.Move(
      (layout_item.BorderLeft() + layout_item.PaddingLeft()).ToInt(),
      (layout_item.BorderTop() + layout_item.PaddingTop()).ToInt());
  // FIXME: We should not allow paint invalidation out of paint invalidation
  // state. crbug.com/457415
  DisablePaintInvalidationStateAsserts paint_invalidation_assert_disabler;
  layout_item.InvalidatePaintRectangle(LayoutRect(paint_invalidation_rect));
}

void FrameView::SetFrameRect(const IntRect& frame_rect) {
  if (frame_rect == frame_rect_)
    return;

  const bool width_changed = frame_rect_.Width() != frame_rect.Width();
  const bool height_changed = frame_rect_.Height() != frame_rect.Height();
  frame_rect_ = frame_rect;

  needs_scrollbars_update_ |= width_changed || height_changed;

  FrameRectsChanged();

  UpdateParentScrollableAreaSet();

  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled() &&
      !RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
    // The overflow clip property depends on the frame size and the pre
    // translation property depends on the frame location.
    SetNeedsPaintPropertyUpdate();
  }

  if (auto layout_view_item = this->GetLayoutViewItem())
    layout_view_item.SetMayNeedPaintInvalidation();

  if (width_changed || height_changed) {
    ViewportSizeChanged(width_changed, height_changed);

    if (frame_->IsMainFrame())
      frame_->GetPage()->GetVisualViewport().MainFrameDidChangeSize();

    GetFrame().Loader().RestoreScrollPositionAndViewState();
  }
}

Page* FrameView::GetPage() const {
  return GetFrame().GetPage();
}

LayoutView* FrameView::GetLayoutView() const {
  return GetFrame().ContentLayoutObject();
}

LayoutViewItem FrameView::GetLayoutViewItem() const {
  return LayoutViewItem(GetFrame().ContentLayoutObject());
}

ScrollingCoordinator* FrameView::GetScrollingCoordinator() const {
  Page* p = GetPage();
  return p ? p->GetScrollingCoordinator() : 0;
}

CompositorAnimationHost* FrameView::GetCompositorAnimationHost() const {
  // When m_animationHost is not nullptr, this is the FrameView for an OOPIF.
  if (animation_host_)
    return animation_host_.get();

  if (&frame_->LocalFrameRoot() != frame_)
    return frame_->LocalFrameRoot().View()->GetCompositorAnimationHost();

  if (!frame_->IsMainFrame())
    return nullptr;

  ScrollingCoordinator* c = GetScrollingCoordinator();
  return c ? c->GetCompositorAnimationHost() : nullptr;
}

CompositorAnimationTimeline* FrameView::GetCompositorAnimationTimeline() const {
  if (animation_timeline_)
    return animation_timeline_.get();

  if (&frame_->LocalFrameRoot() != frame_)
    return frame_->LocalFrameRoot().View()->GetCompositorAnimationTimeline();

  if (!frame_->IsMainFrame())
    return nullptr;

  ScrollingCoordinator* c = GetScrollingCoordinator();
  return c ? c->GetCompositorAnimationTimeline() : nullptr;
}

LayoutBox* FrameView::GetLayoutBox() const {
  return GetLayoutView();
}

FloatQuad FrameView::LocalToVisibleContentQuad(
    const FloatQuad& quad,
    const LayoutObject* local_object,
    MapCoordinatesFlags flags) const {
  LayoutBox* box = GetLayoutBox();
  if (!box)
    return quad;
  DCHECK(local_object);
  FloatQuad result = local_object->LocalToAncestorQuad(quad, box, flags);
  result.Move(-GetScrollOffset());
  return result;
}

RefPtr<WebTaskRunner> FrameView::GetTimerTaskRunner() const {
  return TaskRunnerHelper::Get(TaskType::kUnspecedTimer, frame_.Get());
}

void FrameView::SetCanHaveScrollbars(bool can_have_scrollbars) {
  can_have_scrollbars_ = can_have_scrollbars;

  ScrollbarMode new_vertical_mode = vertical_scrollbar_mode_;
  if (can_have_scrollbars && vertical_scrollbar_mode_ == kScrollbarAlwaysOff)
    new_vertical_mode = kScrollbarAuto;
  else if (!can_have_scrollbars)
    new_vertical_mode = kScrollbarAlwaysOff;

  ScrollbarMode new_horizontal_mode = horizontal_scrollbar_mode_;
  if (can_have_scrollbars && horizontal_scrollbar_mode_ == kScrollbarAlwaysOff)
    new_horizontal_mode = kScrollbarAuto;
  else if (!can_have_scrollbars)
    new_horizontal_mode = kScrollbarAlwaysOff;

  SetScrollbarModes(new_horizontal_mode, new_vertical_mode);
}

bool FrameView::ShouldUseCustomScrollbars(
    Element*& custom_scrollbar_element) const {
  custom_scrollbar_element = nullptr;

  if (Settings* settings = frame_->GetSettings()) {
    if (!settings->GetAllowCustomScrollbarInMainFrame() &&
        frame_->IsMainFrame())
      return false;
  }
  Document* doc = frame_->GetDocument();

  // Try the <body> element first as a scrollbar source.
  Element* body = doc ? doc->body() : 0;
  if (body && body->GetLayoutObject() &&
      body->GetLayoutObject()->Style()->HasPseudoStyle(kPseudoIdScrollbar)) {
    custom_scrollbar_element = body;
    return true;
  }

  // If the <body> didn't have a custom style, then the root element might.
  Element* doc_element = doc ? doc->documentElement() : 0;
  if (doc_element && doc_element->GetLayoutObject() &&
      doc_element->GetLayoutObject()->Style()->HasPseudoStyle(
          kPseudoIdScrollbar)) {
    custom_scrollbar_element = doc_element;
    return true;
  }

  return false;
}

Scrollbar* FrameView::CreateScrollbar(ScrollbarOrientation orientation) {
  return scrollbar_manager_.CreateScrollbar(orientation);
}

void FrameView::SetContentsSize(const IntSize& size) {
  if (size == ContentsSize())
    return;

  contents_size_ = size;
  needs_scrollbars_update_ = true;
  ScrollableArea::ContentsResized();

  Page* page = GetFrame().GetPage();
  if (!page)
    return;

  page->GetChromeClient().ContentsSizeChanged(frame_.Get(), size);

  // Ensure the scrollToFragmentAnchor is called before
  // restoreScrollPositionAndViewState when reload
  ScrollToFragmentAnchor();
  GetFrame().Loader().RestoreScrollPositionAndViewState();
}

void FrameView::AdjustViewSize() {
  if (suppress_adjust_view_size_)
    return;

  LayoutViewItem layout_view_item = this->GetLayoutViewItem();
  if (layout_view_item.IsNull())
    return;

  DCHECK_EQ(frame_->View(), this);

  const IntRect rect = layout_view_item.DocumentRect();
  const IntSize& size = rect.Size();

  const IntPoint origin(-rect.X(), -rect.Y());
  if (ScrollOrigin() != origin)
    SetScrollOrigin(origin);

  SetContentsSize(size);
}

void FrameView::AdjustViewSizeAndLayout() {
  AdjustViewSize();
  if (NeedsLayout()) {
    AutoReset<bool> suppress_adjust_view_size(&suppress_adjust_view_size_,
                                              true);
    UpdateLayout();
  }
}

void FrameView::UpdateAcceleratedCompositingSettings() {
  if (LayoutViewItem layout_view_item = this->GetLayoutViewItem())
    layout_view_item.Compositor()->UpdateAcceleratedCompositingSettings();
}

void FrameView::RecalcOverflowAfterStyleChange() {
  LayoutViewItem layout_view_item = this->GetLayoutViewItem();
  CHECK(!layout_view_item.IsNull());
  if (!layout_view_item.NeedsOverflowRecalcAfterStyleChange())
    return;

  layout_view_item.RecalcOverflowAfterStyleChange();

  // Changing overflow should notify scrolling coordinator to ensures that it
  // updates non-fast scroll rects even if there is no layout.
  if (ScrollingCoordinator* scrolling_coordinator =
          this->GetScrollingCoordinator())
    scrolling_coordinator->NotifyOverflowUpdated();

  IntRect document_rect = layout_view_item.DocumentRect();
  if (ScrollOrigin() == -document_rect.Location() &&
      ContentsSize() == document_rect.Size())
    return;

  if (NeedsLayout())
    return;

  // If the visualViewport supplies scrollbars, we won't get a paint
  // invalidation from computeScrollbarExistence so we need to force one.
  if (VisualViewportSuppliesScrollbars())
    layout_view_item.SetMayNeedPaintInvalidation();

  // TODO(pdr): This should be refactored to just block scrollbar updates as
  // we are not in a scrollbar update here and m_inUpdateScrollbars has other
  // side effects. This scope is only for preventing a synchronous layout from
  // scroll origin changes which would not be allowed during style recalc.
  InUpdateScrollbarsScope in_update_scrollbars_scope(this);

  bool should_have_horizontal_scrollbar = false;
  bool should_have_vertical_scrollbar = false;
  ComputeScrollbarExistence(should_have_horizontal_scrollbar,
                            should_have_vertical_scrollbar,
                            document_rect.Size());

  bool has_horizontal_scrollbar = HorizontalScrollbar();
  bool has_vertical_scrollbar = VerticalScrollbar();
  if (has_horizontal_scrollbar != should_have_horizontal_scrollbar ||
      has_vertical_scrollbar != should_have_vertical_scrollbar) {
    SetNeedsLayout();
    return;
  }

  AdjustViewSize();
  UpdateScrollbarGeometry();
  SetNeedsPaintPropertyUpdate();

  if (ScrollOriginChanged())
    SetNeedsLayout();
}

bool FrameView::UsesCompositedScrolling() const {
  LayoutViewItem layout_view = this->GetLayoutViewItem();
  if (layout_view.IsNull())
    return false;
  if (frame_->GetSettings() &&
      frame_->GetSettings()->GetPreferCompositingToLCDTextEnabled())
    return layout_view.Compositor()->InCompositingMode();
  return false;
}

bool FrameView::ShouldScrollOnMainThread() const {
  if (GetMainThreadScrollingReasons())
    return true;
  return ScrollableArea::ShouldScrollOnMainThread();
}

GraphicsLayer* FrameView::LayerForScrolling() const {
  LayoutViewItem layout_view = this->GetLayoutViewItem();
  if (layout_view.IsNull())
    return nullptr;
  return layout_view.Compositor()->FrameScrollLayer();
}

GraphicsLayer* FrameView::LayerForHorizontalScrollbar() const {
  LayoutViewItem layout_view = this->GetLayoutViewItem();
  if (layout_view.IsNull())
    return nullptr;
  return layout_view.Compositor()->LayerForHorizontalScrollbar();
}

GraphicsLayer* FrameView::LayerForVerticalScrollbar() const {
  LayoutViewItem layout_view = this->GetLayoutViewItem();
  if (layout_view.IsNull())
    return nullptr;
  return layout_view.Compositor()->LayerForVerticalScrollbar();
}

GraphicsLayer* FrameView::LayerForScrollCorner() const {
  LayoutViewItem layout_view = this->GetLayoutViewItem();
  if (layout_view.IsNull())
    return nullptr;
  return layout_view.Compositor()->LayerForScrollCorner();
}

bool FrameView::IsEnclosedInCompositingLayer() const {
  // FIXME: It's a bug that compositing state isn't always up to date when this
  // is called. crbug.com/366314
  DisableCompositingQueryAsserts disabler;

  LayoutItem frame_owner_layout_item = frame_->OwnerLayoutItem();
  return !frame_owner_layout_item.IsNull() &&
         frame_owner_layout_item.EnclosingLayer()
             ->EnclosingLayerForPaintInvalidationCrossingFrameBoundaries();
}

void FrameView::CountObjectsNeedingLayout(unsigned& needs_layout_objects,
                                          unsigned& total_objects,
                                          bool& is_subtree) {
  needs_layout_objects = 0;
  total_objects = 0;
  is_subtree = IsSubtreeLayout();
  if (is_subtree)
    layout_subtree_root_list_.CountObjectsNeedingLayout(needs_layout_objects,
                                                        total_objects);
  else
    LayoutSubtreeRootList::CountObjectsNeedingLayoutInRoot(
        GetLayoutView(), needs_layout_objects, total_objects);
}

inline void FrameView::ForceLayoutParentViewIfNeeded() {
  LayoutPartItem owner_layout_item = frame_->OwnerLayoutItem();
  if (owner_layout_item.IsNull() || !owner_layout_item.GetFrame())
    return;

  LayoutReplaced* content_box = EmbeddedReplacedContent();
  if (!content_box)
    return;

  LayoutSVGRoot* svg_root = ToLayoutSVGRoot(content_box);
  if (svg_root->EverHadLayout() && !svg_root->NeedsLayout())
    return;

  // If the embedded SVG document appears the first time, the ownerLayoutObject
  // has already finished layout without knowing about the existence of the
  // embedded SVG document, because LayoutReplaced embeddedReplacedContent()
  // returns 0, as long as the embedded document isn't loaded yet. Before
  // bothering to lay out the SVG document, mark the ownerLayoutObject needing
  // layout and ask its FrameView for a layout. After that the
  // LayoutEmbeddedObject (ownerLayoutObject) carries the correct size, which
  // LayoutSVGRoot::computeReplacedLogicalWidth/Height rely on, when laying out
  // for the first time, or when the LayoutSVGRoot size has changed dynamically
  // (eg. via <script>).
  FrameView* frame_view = owner_layout_item.GetFrame()->View();

  // Mark the owner layoutObject as needing layout.
  owner_layout_item.SetNeedsLayoutAndPrefWidthsRecalcAndFullPaintInvalidation(
      LayoutInvalidationReason::kUnknown);

  // Synchronously enter layout, to layout the view containing the host
  // object/embed/iframe.
  DCHECK(frame_view);
  frame_view->UpdateLayout();
}

void FrameView::PerformPreLayoutTasks() {
  TRACE_EVENT0("blink,benchmark", "FrameView::performPreLayoutTasks");
  Lifecycle().AdvanceTo(DocumentLifecycle::kInPreLayout);

  // Don't schedule more layouts, we're in one.
  AutoReset<bool> change_scheduling_enabled(&layout_scheduling_enabled_, false);

  if (!nested_layout_count_ && !in_synchronous_post_layout_ &&
      post_layout_tasks_timer_.IsActive()) {
    // This is a new top-level layout. If there are any remaining tasks from the
    // previous layout, finish them now.
    in_synchronous_post_layout_ = true;
    PerformPostLayoutTasks();
    in_synchronous_post_layout_ = false;
  }

  bool was_resized = WasViewportResized();
  Document* document = frame_->GetDocument();
  if (was_resized)
    document->SetResizedForViewportUnits();

  // Viewport-dependent or device-dependent media queries may cause us to need
  // completely different style information.
  bool main_frame_rotation =
      frame_->IsMainFrame() && frame_->GetSettings() &&
      frame_->GetSettings()->GetMainFrameResizesAreOrientationChanges();
  if ((was_resized &&
       document->GetStyleEngine().MediaQueryAffectedByViewportChange()) ||
      (was_resized && main_frame_rotation &&
       document->GetStyleEngine().MediaQueryAffectedByDeviceChange())) {
    document->MediaQueryAffectingValueChanged();
  } else if (was_resized) {
    document->EvaluateMediaQueryList();
  }

  document->UpdateStyleAndLayoutTree();
  Lifecycle().AdvanceTo(DocumentLifecycle::kStyleClean);

  if (was_resized)
    document->ClearResizedForViewportUnits();

  if (ShouldPerformScrollAnchoring())
    scroll_anchor_.NotifyBeforeLayout();
}

bool FrameView::ShouldPerformScrollAnchoring() const {
  return RuntimeEnabledFeatures::scrollAnchoringEnabled() &&
         !RuntimeEnabledFeatures::rootLayerScrollingEnabled() &&
         scroll_anchor_.HasScroller() &&
         GetLayoutBox()->Style()->OverflowAnchor() != EOverflowAnchor::kNone &&
         !frame_->GetDocument()->FinishingOrIsPrinting();
}

static inline void LayoutFromRootObject(LayoutObject& root) {
  LayoutState layout_state(root);
  root.UpdateLayout();
}

void FrameView::PrepareLayoutAnalyzer() {
  bool is_tracing = false;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(
      TRACE_DISABLED_BY_DEFAULT("blink.debug.layout"), &is_tracing);
  if (!is_tracing) {
    analyzer_.reset();
    return;
  }
  if (!analyzer_)
    analyzer_ = WTF::MakeUnique<LayoutAnalyzer>();
  analyzer_->Reset();
}

std::unique_ptr<TracedValue> FrameView::AnalyzerCounters() {
  if (!analyzer_)
    return TracedValue::Create();
  std::unique_ptr<TracedValue> value = analyzer_->ToTracedValue();
  value->SetString("host",
                   GetLayoutViewItem().GetDocument().location()->host());
  value->SetString(
      "frame",
      String::Format("0x%" PRIxPTR, reinterpret_cast<uintptr_t>(frame_.Get())));
  value->SetInteger("contentsHeightAfterLayout",
                    GetLayoutViewItem().DocumentRect().Height());
  value->SetInteger("visibleHeight", VisibleHeight());
  value->SetInteger(
      "approximateBlankCharacterCount",
      FontFaceSet::ApproximateBlankCharacterCount(*frame_->GetDocument()));
  return value;
}

#define PERFORM_LAYOUT_TRACE_CATEGORIES \
  "blink,benchmark,rail," TRACE_DISABLED_BY_DEFAULT("blink.debug.layout")

void FrameView::PerformLayout(bool in_subtree_layout) {
  DCHECK(in_subtree_layout || layout_subtree_root_list_.IsEmpty());

  int contents_height_before_layout =
      GetLayoutViewItem().DocumentRect().Height();
  TRACE_EVENT_BEGIN1(PERFORM_LAYOUT_TRACE_CATEGORIES,
                     "FrameView::performLayout", "contentsHeightBeforeLayout",
                     contents_height_before_layout);
  PrepareLayoutAnalyzer();

  ScriptForbiddenScope forbid_script;

  if (in_subtree_layout && HasOrthogonalWritingModeRoots()) {
    // If we're going to lay out from each subtree root, rather than once from
    // LayoutView, we need to merge the depth-ordered orthogonal writing mode
    // root list into the depth-ordered list of subtrees scheduled for
    // layout. Otherwise, during layout of one such subtree, we'd risk skipping
    // over a subtree of objects needing layout.
    DCHECK(!layout_subtree_root_list_.IsEmpty());
    ScheduleOrthogonalWritingModeRootsForLayout();
  }

  DCHECK(!IsInPerformLayout());
  Lifecycle().AdvanceTo(DocumentLifecycle::kInPerformLayout);

  // performLayout is the actual guts of layout().
  // FIXME: The 300 other lines in layout() probably belong in other helper
  // functions so that a single human could understand what layout() is actually
  // doing.

  ForceLayoutParentViewIfNeeded();

  if (in_subtree_layout) {
    if (analyzer_)
      analyzer_->Increment(LayoutAnalyzer::kPerformLayoutRootLayoutObjects,
                           layout_subtree_root_list_.size());
    for (auto& root : layout_subtree_root_list_.Ordered()) {
      if (!root->NeedsLayout())
        continue;
      LayoutFromRootObject(*root);

      // We need to ensure that we mark up all layoutObjects up to the
      // LayoutView for paint invalidation. This simplifies our code as we just
      // always do a full tree walk.
      if (LayoutItem container = LayoutItem(root->Container()))
        container.SetMayNeedPaintInvalidation();
    }
    layout_subtree_root_list_.Clear();
  } else {
    if (HasOrthogonalWritingModeRoots() &&
        !RuntimeEnabledFeatures::layoutNGEnabled())
      LayoutOrthogonalWritingModeRoots();
    GetLayoutView()->UpdateLayout();
  }

  frame_->GetDocument()->Fetcher()->UpdateAllImageResourcePriorities();

  Lifecycle().AdvanceTo(DocumentLifecycle::kAfterPerformLayout);

  TRACE_EVENT_END1(PERFORM_LAYOUT_TRACE_CATEGORIES, "FrameView::performLayout",
                   "counters", AnalyzerCounters());
  FirstMeaningfulPaintDetector::From(*frame_->GetDocument())
      .MarkNextPaintAsMeaningfulIfNeeded(
          layout_object_counter_, contents_height_before_layout,
          GetLayoutViewItem().DocumentRect().Height(), VisibleHeight());
}

void FrameView::ScheduleOrPerformPostLayoutTasks() {
  if (post_layout_tasks_timer_.IsActive())
    return;

  if (!in_synchronous_post_layout_) {
    in_synchronous_post_layout_ = true;
    // Calls resumeScheduledEvents()
    PerformPostLayoutTasks();
    in_synchronous_post_layout_ = false;
  }

  if (!post_layout_tasks_timer_.IsActive() &&
      (NeedsLayout() || in_synchronous_post_layout_)) {
    // If we need layout or are already in a synchronous call to
    // postLayoutTasks(), defer FrameView updates and event dispatch until
    // after we return.  postLayoutTasks() can make us need to update again, and
    // we can get stuck in a nasty cycle unless we call it through the timer
    // here.
    post_layout_tasks_timer_.StartOneShot(0, BLINK_FROM_HERE);
    if (NeedsLayout())
      UpdateLayout();
  }
}

void FrameView::UpdateLayout() {
  // We should never layout a Document which is not in a LocalFrame.
  DCHECK(frame_);
  DCHECK_EQ(frame_->View(), this);
  DCHECK(frame_->GetPage());

  ScriptForbiddenScope forbid_script;

  if (IsInPerformLayout() || ShouldThrottleRendering() ||
      !frame_->GetDocument()->IsActive())
    return;

  TRACE_EVENT0("blink,benchmark", "FrameView::layout");

  if (auto_size_info_)
    auto_size_info_->AutoSizeIfNeeded();

  has_pending_layout_ = false;
  DocumentLifecycle::Scope lifecycle_scope(Lifecycle(),
                                           DocumentLifecycle::kLayoutClean);

  Document* document = frame_->GetDocument();
  TRACE_EVENT_BEGIN1("devtools.timeline", "Layout", "beginData",
                     InspectorLayoutEvent::BeginData(this));
  probe::UpdateLayout probe(document);

  PerformPreLayoutTasks();

  // TODO(crbug.com/460956): The notion of a single root for layout is no longer
  // applicable. Remove or update this code.
  LayoutObject* root_for_this_layout = GetLayoutView();

  FontCachePurgePreventer font_cache_purge_preventer;
  {
    AutoReset<bool> change_scheduling_enabled(&layout_scheduling_enabled_,
                                              false);
    nested_layout_count_++;

    UpdateCounters();

    // If the layout view was marked as needing layout after we added items in
    // the subtree roots we need to clear the roots and do the layout from the
    // layoutView.
    if (GetLayoutViewItem().NeedsLayout())
      ClearLayoutSubtreeRootsAndMarkContainingBlocks();
    GetLayoutViewItem().ClearHitTestCache();

    bool in_subtree_layout = IsSubtreeLayout();

    // TODO(crbug.com/460956): The notion of a single root for layout is no
    // longer applicable. Remove or update this code.
    if (in_subtree_layout)
      root_for_this_layout = layout_subtree_root_list_.RandomRoot();

    if (!root_for_this_layout) {
      // FIXME: Do we need to set m_size here?
      NOTREACHED();
      return;
    }

    if (!in_subtree_layout) {
      ClearLayoutSubtreeRootsAndMarkContainingBlocks();
      Node* body = document->body();
      if (body && body->GetLayoutObject()) {
        if (isHTMLFrameSetElement(*body)) {
          body->GetLayoutObject()->SetChildNeedsLayout();
        } else if (isHTMLBodyElement(*body)) {
          if (!first_layout_ && size_.Height() != GetLayoutSize().Height() &&
              body->GetLayoutObject()->EnclosingBox()->StretchesToViewport())
            body->GetLayoutObject()->SetChildNeedsLayout();
        }
      }

      ScrollbarMode h_mode;
      ScrollbarMode v_mode;
      GetLayoutView()->CalculateScrollbarModes(h_mode, v_mode);

      // Now set our scrollbar state for the layout.
      ScrollbarMode current_h_mode = HorizontalScrollbarMode();
      ScrollbarMode current_v_mode = VerticalScrollbarMode();

      if (first_layout_) {
        SetScrollbarsSuppressed(true);

        first_layout_ = false;
        last_viewport_size_ = GetLayoutSize(kIncludeScrollbars);
        last_zoom_factor_ = GetLayoutViewItem().Style()->Zoom();

        // Set the initial vMode to AlwaysOn if we're auto.
        if (v_mode == kScrollbarAuto) {
          // This causes a vertical scrollbar to appear.
          SetVerticalScrollbarMode(kScrollbarAlwaysOn);
        }
        // Set the initial hMode to AlwaysOff if we're auto.
        if (h_mode == kScrollbarAuto) {
          // This causes a horizontal scrollbar to disappear.
          SetHorizontalScrollbarMode(kScrollbarAlwaysOff);
        }

        SetScrollbarModes(h_mode, v_mode);
        SetScrollbarsSuppressed(false);
      } else if (h_mode != current_h_mode || v_mode != current_v_mode) {
        SetScrollbarModes(h_mode, v_mode);
      }

      UpdateScrollbarsIfNeeded();

      LayoutSize old_size = size_;

      size_ = LayoutSize(GetLayoutSize());

      if (old_size != size_ && !first_layout_) {
        LayoutBox* root_layout_object =
            document->documentElement()
                ? document->documentElement()->GetLayoutBox()
                : 0;
        LayoutBox* body_layout_object = root_layout_object && document->body()
                                            ? document->body()->GetLayoutBox()
                                            : 0;
        if (body_layout_object && body_layout_object->StretchesToViewport())
          body_layout_object->SetChildNeedsLayout();
        else if (root_layout_object &&
                 root_layout_object->StretchesToViewport())
          root_layout_object->SetChildNeedsLayout();
      }
    }

    TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID(
        TRACE_DISABLED_BY_DEFAULT("blink.debug.layout.trees"), "LayoutTree",
        this, TracedLayoutObject::Create(*GetLayoutView(), false));

    IntSize old_size(Size());

    PerformLayout(in_subtree_layout);
    UpdateScrollbars();
    UpdateParentScrollableAreaSet();

    IntSize new_size(Size());
    if (old_size != new_size) {
      needs_scrollbars_update_ = true;
      SetNeedsLayout();
      MarkViewportConstrainedObjectsForLayout(
          old_size.Width() != new_size.Width(),
          old_size.Height() != new_size.Height());
    }

    if (NeedsLayout()) {
      AutoReset<bool> suppress(&suppress_adjust_view_size_, true);
      UpdateLayout();
    }

    DCHECK(layout_subtree_root_list_.IsEmpty());
  }  // Reset m_layoutSchedulingEnabled to its previous value.
  CheckDoesNotNeedLayout();

  frame_timing_requests_dirty_ = true;

  // FIXME: Could find the common ancestor layer of all dirty subtrees and mark
  // from there. crbug.com/462719
  GetLayoutViewItem().EnclosingLayer()->UpdateLayerPositionsAfterLayout();

  TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID(
      TRACE_DISABLED_BY_DEFAULT("blink.debug.layout.trees"), "LayoutTree", this,
      TracedLayoutObject::Create(*GetLayoutView(), true));

  GetLayoutViewItem().Compositor()->DidLayout();

  layout_count_++;

  if (AXObjectCache* cache = document->AxObjectCache()) {
    const KURL& url = document->Url();
    if (url.IsValid() && !url.IsAboutBlankURL())
      cache->HandleLayoutComplete(document);
  }
  UpdateDocumentAnnotatedRegions();
  CheckDoesNotNeedLayout();

  ScheduleOrPerformPostLayoutTasks();
  CheckDoesNotNeedLayout();

  // FIXME: The notion of a single root for layout is no longer applicable.
  // Remove or update this code. crbug.com/460596
  TRACE_EVENT_END1("devtools.timeline", "Layout", "endData",
                   InspectorLayoutEvent::EndData(root_for_this_layout));
  probe::didChangeViewport(frame_.Get());

  nested_layout_count_--;
  if (nested_layout_count_)
    return;

#if DCHECK_IS_ON()
  // Post-layout assert that nobody was re-marked as needing layout during
  // layout.
  GetLayoutView()->AssertSubtreeIsLaidOut();
#endif

  GetFrame().GetDocument()->LayoutUpdated();
  CheckDoesNotNeedLayout();
}

void FrameView::DeprecatedInvalidateTree(
    const PaintInvalidationState& paint_invalidation_state) {
  DCHECK(!RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled());

  if (ShouldThrottleRendering())
    return;

  Lifecycle().AdvanceTo(DocumentLifecycle::kInPaintInvalidation);

  CHECK(!GetLayoutViewItem().IsNull());
  LayoutViewItem root_for_paint_invalidation = GetLayoutViewItem();
  DCHECK(!root_for_paint_invalidation.NeedsLayout());

  TRACE_EVENT1("blink", "FrameView::invalidateTree", "root",
               root_for_paint_invalidation.DebugName().Ascii());

  InvalidatePaint(paint_invalidation_state);
  root_for_paint_invalidation.DeprecatedInvalidateTree(
      paint_invalidation_state);

#if DCHECK_IS_ON()
  GetLayoutView()->AssertSubtreeClearedPaintInvalidationFlags();
#endif

  Lifecycle().AdvanceTo(DocumentLifecycle::kPaintInvalidationClean);
}

void FrameView::InvalidatePaint(
    const PaintInvalidationState& paint_invalidation_state) {
  CHECK(!GetLayoutViewItem().IsNull());
  if (!RuntimeEnabledFeatures::rootLayerScrollingEnabled())
    InvalidatePaintOfScrollControlsIfNeeded(paint_invalidation_state);
}

void FrameView::SetNeedsPaintPropertyUpdate() {
  needs_paint_property_update_ = true;
  if (RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
    if (auto* layout_view = this->GetLayoutView()) {
      layout_view->SetNeedsPaintPropertyUpdate();
      return;
    }
  }
  if (LayoutObject* owner = GetFrame().OwnerLayoutObject())
    owner->SetNeedsPaintPropertyUpdate();
}

void FrameView::SetSubtreeNeedsPaintPropertyUpdate() {
  SetNeedsPaintPropertyUpdate();
  GetLayoutView()->SetSubtreeNeedsPaintPropertyUpdate();
}

IntRect FrameView::ComputeVisibleArea() {
  // Return our clipping bounds in the root frame.
  IntRect us(FrameRect());
  if (FrameView* parent = ParentFrameView()) {
    us = parent->ContentsToRootFrame(us);
    IntRect parent_rect = parent->ComputeVisibleArea();
    if (parent_rect.IsEmpty())
      return IntRect();

    us.Intersect(parent_rect);
  }

  return us;
}

FloatSize FrameView::ViewportSizeForViewportUnits() const {
  float zoom = 1;
  if (!frame_->GetDocument() || !frame_->GetDocument()->Printing())
    zoom = GetFrame().PageZoomFactor();

  LayoutViewItem layout_view_item = this->GetLayoutViewItem();
  if (layout_view_item.IsNull())
    return FloatSize();

  FloatSize layout_size;
  layout_size.SetWidth(layout_view_item.ViewWidth(kIncludeScrollbars) / zoom);
  layout_size.SetHeight(layout_view_item.ViewHeight(kIncludeScrollbars) / zoom);

  BrowserControls& browser_controls = frame_->GetPage()->GetBrowserControls();
  if (RuntimeEnabledFeatures::inertTopControlsEnabled() &&
      browser_controls.PermittedState() != kWebBrowserControlsHidden) {
    // We use the layoutSize rather than frameRect to calculate viewport units
    // so that we get correct results on mobile where the page is laid out into
    // a rect that may be larger than the viewport (e.g. the 980px fallback
    // width for desktop pages). Since the layout height is statically set to
    // be the viewport with browser controls showing, we add the browser
    // controls height, compensating for page scale as well, since we want to
    // use the viewport with browser controls hidden for vh (to match Safari).
    int viewport_width = frame_->GetPage()->GetVisualViewport().Size().Width();
    if (frame_->IsMainFrame() && layout_size.Width() && viewport_width) {
      float page_scale_at_layout_width = viewport_width / layout_size.Width();
      layout_size.Expand(
          0, browser_controls.Height() / page_scale_at_layout_width);
    }
  }

  return layout_size;
}

FloatSize FrameView::ViewportSizeForMediaQueries() const {
  FloatSize viewport_size(GetLayoutSize(kIncludeScrollbars));
  if (!frame_->GetDocument() || !frame_->GetDocument()->Printing())
    viewport_size.Scale(1 / GetFrame().PageZoomFactor());
  return viewport_size;
}

DocumentLifecycle& FrameView::Lifecycle() const {
  DCHECK(frame_);
  DCHECK(frame_->GetDocument());
  return frame_->GetDocument()->Lifecycle();
}

LayoutReplaced* FrameView::EmbeddedReplacedContent() const {
  LayoutViewItem layout_view_item = this->GetLayoutViewItem();
  if (layout_view_item.IsNull())
    return nullptr;

  LayoutObject* first_child = GetLayoutView()->FirstChild();
  if (!first_child || !first_child->IsBox())
    return nullptr;

  // Currently only embedded SVG documents participate in the size-negotiation
  // logic.
  if (first_child->IsSVGRoot())
    return ToLayoutSVGRoot(first_child);

  return nullptr;
}

void FrameView::AddPart(LayoutPart* object) {
  parts_.insert(object);
}

void FrameView::RemovePart(LayoutPart* object) {
  parts_.erase(object);
}

void FrameView::UpdateGeometries() {
  Vector<RefPtr<LayoutPart>> parts;
  CopyToVector(parts_, parts);

  for (auto part : parts) {
    // Script or plugins could detach the frame so abort processing if that
    // happens.
    if (GetLayoutViewItem().IsNull())
      break;

    if (part->GetFrameOrPlugin()) {
      if (FrameView* frame_view = part->ChildFrameView()) {
        bool did_need_layout = frame_view->NeedsLayout();
        part->UpdateGeometry();
        if (!did_need_layout && !frame_view->ShouldThrottleRendering())
          frame_view->CheckDoesNotNeedLayout();
      } else {
        part->UpdateGeometry();
      }
    }
  }
}

void FrameView::AddPartToUpdate(LayoutEmbeddedObject& object) {
  DCHECK(IsInPerformLayout());
  // Tell the DOM element that it needs a Plugin update.
  Node* node = object.GetNode();
  DCHECK(node);
  if (isHTMLObjectElement(*node) || isHTMLEmbedElement(*node))
    ToHTMLPlugInElement(node)->SetNeedsPluginUpdate(true);

  part_update_set_.insert(&object);
}

void FrameView::SetDisplayMode(WebDisplayMode mode) {
  if (mode == display_mode_)
    return;

  display_mode_ = mode;

  if (frame_->GetDocument())
    frame_->GetDocument()->MediaQueryAffectingValueChanged();
}

void FrameView::SetDisplayShape(DisplayShape display_shape) {
  if (display_shape == display_shape_)
    return;

  display_shape_ = display_shape;

  if (frame_->GetDocument())
    frame_->GetDocument()->MediaQueryAffectingValueChanged();
}

void FrameView::SetMediaType(const AtomicString& media_type) {
  DCHECK(frame_->GetDocument());
  media_type_ = media_type;
  frame_->GetDocument()->MediaQueryAffectingValueChanged();
}

AtomicString FrameView::MediaType() const {
  // See if we have an override type.
  if (frame_->GetSettings() &&
      !frame_->GetSettings()->GetMediaTypeOverride().IsEmpty())
    return AtomicString(frame_->GetSettings()->GetMediaTypeOverride());
  return media_type_;
}

void FrameView::AdjustMediaTypeForPrinting(bool printing) {
  if (printing) {
    if (media_type_when_not_printing_.IsNull())
      media_type_when_not_printing_ = MediaType();
    SetMediaType(MediaTypeNames::print);
  } else {
    if (!media_type_when_not_printing_.IsNull())
      SetMediaType(media_type_when_not_printing_);
    media_type_when_not_printing_ = g_null_atom;
  }

  frame_->GetDocument()->SetNeedsStyleRecalc(
      kSubtreeStyleChange, StyleChangeReasonForTracing::Create(
                               StyleChangeReason::kStyleSheetChange));
}

bool FrameView::ContentsInCompositedLayer() const {
  LayoutViewItem layout_view_item = this->GetLayoutViewItem();
  return !layout_view_item.IsNull() &&
         layout_view_item.GetCompositingState() == kPaintsIntoOwnBacking;
}

void FrameView::AddBackgroundAttachmentFixedObject(LayoutObject* object) {
  DCHECK(!background_attachment_fixed_objects_.Contains(object));

  background_attachment_fixed_objects_.insert(object);
  if (ScrollingCoordinator* scrolling_coordinator =
          this->GetScrollingCoordinator())
    scrolling_coordinator
        ->FrameViewHasBackgroundAttachmentFixedObjectsDidChange(this);

  // Ensure main thread scrolling reasons are recomputed.
  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled()) {
    SetNeedsPaintPropertyUpdate();
    // The object's scroll properties are not affected by its own background.
    object->SetAncestorsNeedPaintPropertyUpdateForMainThreadScrolling();
  }
}

void FrameView::RemoveBackgroundAttachmentFixedObject(LayoutObject* object) {
  DCHECK(background_attachment_fixed_objects_.Contains(object));

  background_attachment_fixed_objects_.erase(object);
  if (ScrollingCoordinator* scrolling_coordinator =
          this->GetScrollingCoordinator())
    scrolling_coordinator
        ->FrameViewHasBackgroundAttachmentFixedObjectsDidChange(this);

  // Ensure main thread scrolling reasons are recomputed.
  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled()) {
    SetNeedsPaintPropertyUpdate();
    // The object's scroll properties are not affected by its own background.
    object->SetAncestorsNeedPaintPropertyUpdateForMainThreadScrolling();
  }
}

void FrameView::AddViewportConstrainedObject(LayoutObject& object) {
  if (!viewport_constrained_objects_) {
    viewport_constrained_objects_ =
        WTF::WrapUnique(new ViewportConstrainedObjectSet);
  }

  if (!viewport_constrained_objects_->Contains(&object)) {
    viewport_constrained_objects_->insert(&object);

    if (ScrollingCoordinator* scrolling_coordinator =
            this->GetScrollingCoordinator())
      scrolling_coordinator->FrameViewFixedObjectsDidChange(this);
  }
}

void FrameView::RemoveViewportConstrainedObject(LayoutObject& object) {
  if (viewport_constrained_objects_ &&
      viewport_constrained_objects_->Contains(&object)) {
    viewport_constrained_objects_->erase(&object);

    if (ScrollingCoordinator* scrolling_coordinator =
            this->GetScrollingCoordinator())
      scrolling_coordinator->FrameViewFixedObjectsDidChange(this);
  }
}

void FrameView::ViewportSizeChanged(bool width_changed, bool height_changed) {
  DCHECK(width_changed || height_changed);
  DCHECK(frame_->GetPage());

  bool root_layer_scrolling_enabled =
      RuntimeEnabledFeatures::rootLayerScrollingEnabled();

  if (LayoutView* layout_view = this->GetLayoutView()) {
    // If this is the main frame, we might have got here by hiding/showing the
    // top controls.  In that case, layout won't be triggered, so we need to
    // clamp the scroll offset here.
    if (GetFrame().IsMainFrame()) {
      if (root_layer_scrolling_enabled) {
        layout_view->GetScrollableArea()
            ->ClampScrollOffsetAfterOverflowChange();
      } else {
        AdjustScrollOffsetFromUpdateScrollbars();
      }
    }

    if (layout_view->UsesCompositing()) {
      if (root_layer_scrolling_enabled) {
        layout_view->Layer()->SetNeedsCompositingInputsUpdate();
        if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled())
          SetNeedsPaintPropertyUpdate();
      } else {
        layout_view->Compositor()->FrameViewDidChangeSize();
      }
    }
  }

  if (frame_->IsMainFrame())
    frame_->GetPage()->GlobalRootScrollerController().DidResizeViewport();

  ShowOverlayScrollbars();

  if (RuntimeEnabledFeatures::inertTopControlsEnabled() && GetLayoutView() &&
      frame_->IsMainFrame() &&
      frame_->GetPage()->GetBrowserControls().Height()) {
    if (GetLayoutView()->Style()->HasFixedBackgroundImage()) {
      // In the case where we don't change layout size from top control resizes,
      // we wont perform a layout. If we have a fixed background image however,
      // the background layer needs to get resized so we should request a layout
      // explicitly.
      PaintLayer* layer = GetLayoutView()->Layer();
      if (GetLayoutView()->Compositor()->NeedsFixedRootBackgroundLayer(layer)) {
        SetNeedsLayout();
      } else {
        // If root layer scrolls is on, we've already issued a full invalidation
        // above.
        GetLayoutView()->SetShouldDoFullPaintInvalidationOnResizeIfNeeded(
            width_changed, height_changed);
      }
    } else if (height_changed) {
      // If the document rect doesn't fill the full view height, hiding the
      // URL bar will expose area outside the current LayoutView so we need to
      // paint additional background. If RLS is on, we've already invalidated
      // above.
      LayoutViewItem lvi = GetLayoutViewItem();
      DCHECK(!lvi.IsNull());
      if (lvi.DocumentRect().Height() < lvi.ViewRect().Height()) {
        lvi.SetShouldDoFullPaintInvalidation(
            PaintInvalidationReason::kGeometry);
      }
    }
  }

  if (GetFrame().GetDocument() && !IsInPerformLayout())
    MarkViewportConstrainedObjectsForLayout(width_changed, height_changed);
}

void FrameView::MarkViewportConstrainedObjectsForLayout(bool width_changed,
                                                        bool height_changed) {
  if (!HasViewportConstrainedObjects() || !(width_changed || height_changed))
    return;

  for (const auto& viewport_constrained_object :
       *viewport_constrained_objects_) {
    LayoutObject* layout_object = viewport_constrained_object;
    const ComputedStyle& style = layout_object->StyleRef();
    if (width_changed) {
      if (style.Width().IsFixed() &&
          (style.Left().IsAuto() || style.Right().IsAuto()))
        layout_object->SetNeedsPositionedMovementLayout();
      else
        layout_object->SetNeedsLayoutAndFullPaintInvalidation(
            LayoutInvalidationReason::kSizeChanged);
    }
    if (height_changed) {
      if (style.Height().IsFixed() &&
          (style.Top().IsAuto() || style.Bottom().IsAuto()))
        layout_object->SetNeedsPositionedMovementLayout();
      else
        layout_object->SetNeedsLayoutAndFullPaintInvalidation(
            LayoutInvalidationReason::kSizeChanged);
    }
  }
}

IntPoint FrameView::LastKnownMousePosition() const {
  return frame_->GetEventHandler().LastKnownMousePosition();
}

bool FrameView::ShouldSetCursor() const {
  Page* page = GetFrame().GetPage();
  return page && page->VisibilityState() != kPageVisibilityStateHidden &&
         !frame_->GetEventHandler().IsMousePositionUnknown() &&
         page->GetFocusController().IsActive();
}

void FrameView::ScrollContentsIfNeededRecursive() {
  ForAllNonThrottledFrameViews(
      [](FrameView& frame_view) { frame_view.ScrollContentsIfNeeded(); });
}

void FrameView::InvalidateBackgroundAttachmentFixedObjects() {
  for (const auto& layout_object : background_attachment_fixed_objects_) {
    layout_object->SetShouldDoFullPaintInvalidation(
        PaintInvalidationReason::kBackground);
  }
}

bool FrameView::HasBackgroundAttachmentFixedDescendants(
    const LayoutObject& object) const {
  for (const auto* potential_descendant :
       background_attachment_fixed_objects_) {
    if (potential_descendant == &object)
      continue;
    if (potential_descendant->IsDescendantOf(&object))
      return true;
  }
  return false;
}

bool FrameView::InvalidateViewportConstrainedObjects() {
  bool fast_path_allowed = true;
  for (const auto& viewport_constrained_object :
       *viewport_constrained_objects_) {
    LayoutObject* layout_object = viewport_constrained_object;
    LayoutItem layout_item = LayoutItem(layout_object);
    DCHECK(layout_item.Style()->HasViewportConstrainedPosition());
    DCHECK(layout_item.HasLayer());
    PaintLayer* layer = LayoutBoxModel(layout_item).Layer();

    if (layer->IsPaintInvalidationContainer())
      continue;

    if (layer->SubtreeIsInvisible())
      continue;

    // invalidate even if there is an ancestor with a filter that moves pixels.
    layout_item
        .SetShouldDoFullPaintInvalidationIncludingNonCompositingDescendants();

    TRACE_EVENT_INSTANT1(
        TRACE_DISABLED_BY_DEFAULT("devtools.timeline.invalidationTracking"),
        "ScrollInvalidationTracking", TRACE_EVENT_SCOPE_THREAD, "data",
        InspectorScrollInvalidationTrackingEvent::Data(*layout_object));

    // If the fixed layer has a blur/drop-shadow filter applied on at least one
    // of its parents, we cannot scroll using the fast path, otherwise the
    // outsets of the filter will be moved around the page.
    if (layer->HasAncestorWithFilterThatMovesPixels())
      fast_path_allowed = false;
  }
  return fast_path_allowed;
}

bool FrameView::ScrollContentsFastPath(const IntSize& scroll_delta) {
  if (!ContentsInCompositedLayer())
    return false;

  InvalidateBackgroundAttachmentFixedObjects();

  if (!viewport_constrained_objects_ ||
      viewport_constrained_objects_->IsEmpty()) {
    probe::didChangeViewport(frame_.Get());
    return true;
  }

  if (!InvalidateViewportConstrainedObjects())
    return false;

  probe::didChangeViewport(frame_.Get());
  return true;
}

void FrameView::ScrollContentsSlowPath() {
  TRACE_EVENT0("blink", "FrameView::scrollContentsSlowPath");
  // We need full invalidation during slow scrolling. For slimming paint, full
  // invalidation of the LayoutView is not enough. We also need to invalidate
  // all of the objects.
  // FIXME: Find out what are enough to invalidate in slow path scrolling.
  // crbug.com/451090#9.
  DCHECK(!GetLayoutViewItem().IsNull());
  if (ContentsInCompositedLayer())
    GetLayoutViewItem()
        .Layer()
        ->GetCompositedLayerMapping()
        ->SetContentsNeedDisplay();
  else
    GetLayoutViewItem()
        .SetShouldDoFullPaintInvalidationIncludingNonCompositingDescendants();

  if (ContentsInCompositedLayer()) {
    IntRect update_rect = VisibleContentRect();
    DCHECK(!GetLayoutViewItem().IsNull());
    // FIXME: We should not allow paint invalidation out of paint invalidation
    // state. crbug.com/457415
    DisablePaintInvalidationStateAsserts disabler;
    GetLayoutViewItem().InvalidatePaintRectangle(LayoutRect(update_rect));
  }
  LayoutPartItem frame_layout_item = frame_->OwnerLayoutItem();
  if (!frame_layout_item.IsNull()) {
    if (IsEnclosedInCompositingLayer()) {
      LayoutRect rect(
          frame_layout_item.BorderLeft() + frame_layout_item.PaddingLeft(),
          frame_layout_item.BorderTop() + frame_layout_item.PaddingTop(),
          LayoutUnit(VisibleWidth()), LayoutUnit(VisibleHeight()));
      // FIXME: We should not allow paint invalidation out of paint invalidation
      // state. crbug.com/457415
      DisablePaintInvalidationStateAsserts disabler;
      frame_layout_item.InvalidatePaintRectangle(rect);
      return;
    }
  }
}

void FrameView::RestoreScrollbar() {
  SetScrollbarsSuppressed(false);
}

void FrameView::ProcessUrlFragment(const KURL& url,
                                   UrlFragmentBehavior behavior) {
  // If our URL has no ref, then we have no place we need to jump to.
  // OTOH If CSS target was set previously, we want to set it to 0, recalc
  // and possibly paint invalidation because :target pseudo class may have been
  // set (see bug 11321).
  // Similarly for svg, if we had a previous svgView() then we need to reset
  // the initial view if we don't have a fragment.
  if (!url.HasFragmentIdentifier() && !frame_->GetDocument()->CssTarget() &&
      !frame_->GetDocument()->IsSVGDocument())
    return;

  String fragment_identifier = url.FragmentIdentifier();
  if (ProcessUrlFragmentHelper(fragment_identifier, behavior))
    return;

  // Try again after decoding the ref, based on the document's encoding.
  if (frame_->GetDocument()->Encoding().IsValid())
    ProcessUrlFragmentHelper(
        DecodeURLEscapeSequences(fragment_identifier,
                                 frame_->GetDocument()->Encoding()),
        behavior);
}

bool FrameView::ProcessUrlFragmentHelper(const String& name,
                                         UrlFragmentBehavior behavior) {
  DCHECK(frame_->GetDocument());

  if (behavior == kUrlFragmentScroll &&
      !frame_->GetDocument()->IsRenderingReady()) {
    frame_->GetDocument()->SetGotoAnchorNeededAfterStylesheetsLoad(true);
    return false;
  }

  frame_->GetDocument()->SetGotoAnchorNeededAfterStylesheetsLoad(false);

  Element* anchor_node = frame_->GetDocument()->FindAnchor(name);

  // Setting to null will clear the current target.
  frame_->GetDocument()->SetCSSTarget(anchor_node);

  if (frame_->GetDocument()->IsSVGDocument()) {
    if (SVGSVGElement* svg =
            SVGDocumentExtensions::rootElement(*frame_->GetDocument())) {
      svg->SetupInitialView(name, anchor_node);
      if (!anchor_node)
        return true;
    }
  }

  // Implement the rule that "" and "top" both mean top of page as in other
  // browsers.
  if (!anchor_node &&
      !(name.IsEmpty() || DeprecatedEqualIgnoringCase(name, "top")))
    return false;

  if (behavior == kUrlFragmentScroll)
    SetFragmentAnchor(anchor_node ? static_cast<Node*>(anchor_node)
                                  : frame_->GetDocument());

  // If the anchor accepts keyboard focus and fragment scrolling is allowed,
  // move focus there to aid users relying on keyboard navigation.
  // If anchorNode is not focusable or fragment scrolling is not allowed,
  // clear focus, which matches the behavior of other browsers.
  if (anchor_node) {
    frame_->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
    if (behavior == kUrlFragmentScroll && anchor_node->IsFocusable()) {
      anchor_node->focus();
    } else {
      if (behavior == kUrlFragmentScroll)
        frame_->GetDocument()->SetSequentialFocusNavigationStartingPoint(
            anchor_node);
      frame_->GetDocument()->ClearFocusedElement();
    }
  }
  return true;
}

void FrameView::SetFragmentAnchor(Node* anchor_node) {
  DCHECK(anchor_node);
  fragment_anchor_ = anchor_node;

  // We need to update the layout tree before scrolling.
  frame_->GetDocument()->UpdateStyleAndLayoutTree();

  // If layout is needed, we will scroll in performPostLayoutTasks. Otherwise,
  // scroll immediately.
  LayoutViewItem layout_view_item = this->GetLayoutViewItem();
  if (!layout_view_item.IsNull() && layout_view_item.NeedsLayout())
    UpdateLayout();
  else
    ScrollToFragmentAnchor();
}

void FrameView::ClearFragmentAnchor() {
  fragment_anchor_ = nullptr;
}

void FrameView::DidUpdateElasticOverscroll() {
  Page* page = GetFrame().GetPage();
  if (!page)
    return;
  FloatSize elastic_overscroll = page->GetChromeClient().ElasticOverscroll();
  if (HorizontalScrollbar()) {
    float delta =
        elastic_overscroll.Width() - HorizontalScrollbar()->ElasticOverscroll();
    if (delta != 0) {
      HorizontalScrollbar()->SetElasticOverscroll(elastic_overscroll.Width());
      GetScrollAnimator().NotifyContentAreaScrolled(FloatSize(delta, 0),
                                                    kCompositorScroll);
      SetScrollbarNeedsPaintInvalidation(kHorizontalScrollbar);
    }
  }
  if (VerticalScrollbar()) {
    float delta =
        elastic_overscroll.Height() - VerticalScrollbar()->ElasticOverscroll();
    if (delta != 0) {
      VerticalScrollbar()->SetElasticOverscroll(elastic_overscroll.Height());
      GetScrollAnimator().NotifyContentAreaScrolled(FloatSize(0, delta),
                                                    kCompositorScroll);
      SetScrollbarNeedsPaintInvalidation(kVerticalScrollbar);
    }
  }
}

IntSize FrameView::GetLayoutSize(
    IncludeScrollbarsInRect scrollbar_inclusion) const {
  return scrollbar_inclusion == kExcludeScrollbars
             ? ExcludeScrollbars(layout_size_)
             : layout_size_;
}

void FrameView::SetLayoutSize(const IntSize& size) {
  DCHECK(!LayoutSizeFixedToFrameSize());

  SetLayoutSizeInternal(size);
}

void FrameView::DidScrollTimerFired(TimerBase*) {
  if (frame_->GetDocument() &&
      !frame_->GetDocument()->GetLayoutViewItem().IsNull())
    frame_->GetDocument()->Fetcher()->UpdateAllImageResourcePriorities();
}

void FrameView::UpdateLayersAndCompositingAfterScrollIfNeeded() {
  // Nothing to do after scrolling if there are no fixed position elements.
  if (!HasViewportConstrainedObjects())
    return;

  // Update sticky position objects which are stuck to the viewport. In order to
  // correctly compute the sticky position offsets the layers must be visited
  // top-down, so start at the 'root' sticky elements and recurse downwards.
  for (const auto& viewport_constrained_object :
       *viewport_constrained_objects_) {
    LayoutObject* layout_object = viewport_constrained_object;
    if (layout_object->Style()->GetPosition() != EPosition::kSticky)
      continue;

    PaintLayer* layer = ToLayoutBoxModelObject(layout_object)->Layer();

    // This method can be called during layout at which point the ancestor
    // overflow layer may not be set yet. We can safely skip such cases as we
    // will revisit this method during compositing inputs update.
    if (!layer->AncestorOverflowLayer())
      continue;

    StickyConstraintsMap constraints_map = layer->AncestorOverflowLayer()
                                               ->GetScrollableArea()
                                               ->GetStickyConstraintsMap();
    if (constraints_map.Contains(layer) &&
        !constraints_map.at(layer).HasAncestorStickyElement()) {
      // TODO(skobes): Resolve circular dependency between scroll offset and
      // compositing state, and remove this disabler. https://crbug.com/420741
      DisableCompositingQueryAsserts disabler;
      layer->UpdateLayerPositionsAfterOverflowScroll();
      layout_object->SetMayNeedPaintInvalidationSubtree();
    }
  }

  // If there fixed position elements, scrolling may cause compositing layers to
  // change.  Update FrameView and layer positions after scrolling, but only
  // if we're not inside of layout.
  if (!nested_layout_count_) {
    UpdateGeometries();
    LayoutViewItem layout_view_item = this->GetLayoutViewItem();
    if (!layout_view_item.IsNull())
      layout_view_item.Layer()->SetNeedsCompositingInputsUpdate();
  }
}

bool FrameView::ComputeCompositedSelection(LocalFrame& frame,
                                           CompositedSelection& selection) {
  if (!frame.View() || frame.View()->ShouldThrottleRendering())
    return false;

  const VisibleSelection& visible_selection =
      frame.Selection().ComputeVisibleSelectionInDOMTree();
  if (!frame.Selection().IsHandleVisible() || frame.Selection().IsHidden())
    return false;

  // Non-editable caret selections lack any kind of UI affordance, and
  // needn't be tracked by the client.
  if (visible_selection.IsCaret() && !visible_selection.IsContentEditable())
    return false;

  VisiblePosition visible_start(visible_selection.VisibleStart());
  RenderedPosition rendered_start(visible_start);
  rendered_start.PositionInGraphicsLayerBacking(selection.start, true);
  if (!selection.start.layer)
    return false;

  VisiblePosition visible_end(visible_selection.VisibleEnd());
  RenderedPosition rendered_end(visible_end);
  rendered_end.PositionInGraphicsLayerBacking(selection.end, false);
  if (!selection.end.layer)
    return false;

  selection.type = visible_selection.GetSelectionType();
  selection.start.is_text_direction_rtl |=
      PrimaryDirectionOf(*visible_selection.Start().AnchorNode()) ==
      TextDirection::kRtl;
  selection.end.is_text_direction_rtl |=
      PrimaryDirectionOf(*visible_selection.end().AnchorNode()) ==
      TextDirection::kRtl;

  return true;
}

void FrameView::UpdateCompositedSelectionIfNeeded() {
  if (!RuntimeEnabledFeatures::compositedSelectionUpdateEnabled())
    return;

  TRACE_EVENT0("blink", "FrameView::updateCompositedSelectionIfNeeded");

  Page* page = GetFrame().GetPage();
  DCHECK(page);

  CompositedSelection selection;
  LocalFrame* focused_frame = page->GetFocusController().FocusedFrame();
  LocalFrame* local_frame =
      (focused_frame &&
       (focused_frame->LocalFrameRoot() == frame_->LocalFrameRoot()))
          ? focused_frame
          : nullptr;

  if (local_frame && ComputeCompositedSelection(*local_frame, selection)) {
    page->GetChromeClient().UpdateCompositedSelection(local_frame, selection);
  } else {
    if (!local_frame) {
      // Clearing the mainframe when there is no focused frame (and hence
      // no localFrame) is legacy behaviour, and implemented here to
      // satisfy ParameterizedWebFrameTest.CompositedSelectionBoundsCleared's
      // first check that the composited selection has been cleared even
      // though no frame has focus yet. If this is not desired, then the
      // expectation needs to be removed from the test.
      local_frame = &frame_->LocalFrameRoot();
    }

    if (local_frame)
      page->GetChromeClient().ClearCompositedSelection(local_frame);
  }
}

PlatformChromeClient* FrameView::GetChromeClient() const {
  Page* page = GetFrame().GetPage();
  if (!page)
    return nullptr;
  return &page->GetChromeClient();
}

void FrameView::ContentsResized() {
  if (frame_->IsMainFrame() && frame_->GetDocument()) {
    if (TextAutosizer* text_autosizer =
            frame_->GetDocument()->GetTextAutosizer())
      text_autosizer->UpdatePageInfoInAllFrames();
  }

  ScrollableArea::ContentsResized();
  SetNeedsLayout();
}

void FrameView::ScrollbarExistenceDidChange() {
  // We check to make sure the view is attached to a frame() as this method can
  // be triggered before the view is attached by LocalFrame::createView(...)
  // setting various values such as setScrollBarModes(...) for example.  An
  // ASSERT is triggered when a view is layout before being attached to a
  // frame().
  if (!GetFrame().View())
    return;

  Element* custom_scrollbar_element = nullptr;

  bool uses_overlay_scrollbars =
      ScrollbarTheme::GetTheme().UsesOverlayScrollbars() &&
      !ShouldUseCustomScrollbars(custom_scrollbar_element);

  if (!uses_overlay_scrollbars && NeedsLayout())
    UpdateLayout();

  if (!GetLayoutViewItem().IsNull() && GetLayoutViewItem().UsesCompositing()) {
    GetLayoutViewItem().Compositor()->FrameViewScrollbarsExistenceDidChange();

    if (!uses_overlay_scrollbars)
      GetLayoutViewItem().Compositor()->FrameViewDidChangeSize();
  }
}

void FrameView::HandleLoadCompleted() {
  // Once loading has completed, allow autoSize one last opportunity to
  // reduce the size of the frame.
  if (auto_size_info_)
    auto_size_info_->AutoSizeIfNeeded();

  // If there is a pending layout, the fragment anchor will be cleared when it
  // finishes.
  if (!NeedsLayout())
    ClearFragmentAnchor();

  if (!scrollable_areas_)
    return;
  for (const auto& scrollable_area : *scrollable_areas_) {
    if (!scrollable_area->IsPaintLayerScrollableArea())
      continue;
    PaintLayerScrollableArea* paint_layer_scrollable_area =
        ToPaintLayerScrollableArea(scrollable_area);
    if (paint_layer_scrollable_area->ScrollsOverflow() &&
        !paint_layer_scrollable_area->Layer()->IsRootLayer()) {
      DEFINE_STATIC_LOCAL(
          CustomCountHistogram, scrollable_area_size_histogram,
          ("Event.Scroll.ScrollerSize.OnLoad", 1, kScrollerSizeLargestBucket,
           kScrollerSizeBucketCount));
      scrollable_area_size_histogram.Count(
          paint_layer_scrollable_area->VisibleContentRect().Width() *
          paint_layer_scrollable_area->VisibleContentRect().Height());
    }
  }
}

void FrameView::ClearLayoutSubtreeRoot(const LayoutObject& root) {
  layout_subtree_root_list_.Remove(const_cast<LayoutObject&>(root));
}

void FrameView::ClearLayoutSubtreeRootsAndMarkContainingBlocks() {
  layout_subtree_root_list_.ClearAndMarkContainingBlocksForLayout();
}

void FrameView::AddOrthogonalWritingModeRoot(LayoutBox& root) {
  DCHECK(!root.IsLayoutScrollbarPart());
  orthogonal_writing_mode_root_list_.Add(root);
}

void FrameView::RemoveOrthogonalWritingModeRoot(LayoutBox& root) {
  orthogonal_writing_mode_root_list_.Remove(root);
}

bool FrameView::HasOrthogonalWritingModeRoots() const {
  return !orthogonal_writing_mode_root_list_.IsEmpty();
}

static inline void RemoveFloatingObjectsForSubtreeRoot(LayoutObject& root) {
  // TODO(kojii): Under certain conditions, moveChildTo() defers
  // removeFloatingObjects() until the containing block layouts. For
  // instance, when descendants of the moving child is floating,
  // removeChildNode() does not clear them. In such cases, at this
  // point, FloatingObjects may contain old or even deleted objects.
  // Dealing this in markAllDescendantsWithFloatsForLayout() could
  // solve, but since that is likely to suffer the performance and
  // since the containing block of orthogonal writing mode roots
  // having floats is very rare, prefer to re-create
  // FloatingObjects.
  if (LayoutBlock* cb = root.ContainingBlock()) {
    if ((cb->NormalChildNeedsLayout() || cb->SelfNeedsLayout()) &&
        cb->IsLayoutBlockFlow()) {
      ToLayoutBlockFlow(cb)->RemoveFloatingObjectsFromDescendants();
    }
  }
}

static bool PrepareOrthogonalWritingModeRootForLayout(LayoutObject& root) {
  DCHECK(root.IsBox() && ToLayoutBox(root).IsOrthogonalWritingModeRoot());
  if (!root.NeedsLayout() || root.IsOutOfFlowPositioned() ||
      root.IsColumnSpanAll() ||
      !root.StyleRef().LogicalHeight().IsIntrinsicOrAuto() ||
      ToLayoutBox(root).IsGridItem())
    return false;

  RemoveFloatingObjectsForSubtreeRoot(root);
  return true;
}

void FrameView::LayoutOrthogonalWritingModeRoots() {
  for (auto& root : orthogonal_writing_mode_root_list_.Ordered()) {
    if (PrepareOrthogonalWritingModeRootForLayout(*root))
      LayoutFromRootObject(*root);
  }
}

void FrameView::ScheduleOrthogonalWritingModeRootsForLayout() {
  for (auto& root : orthogonal_writing_mode_root_list_.Ordered()) {
    if (PrepareOrthogonalWritingModeRootForLayout(*root))
      layout_subtree_root_list_.Add(*root);
  }
}

bool FrameView::CheckLayoutInvalidationIsAllowed() const {
  if (allows_layout_invalidation_after_layout_clean_)
    return true;

  // If we are updating all lifecycle phases beyond LayoutClean, we don't expect
  // dirty layout after LayoutClean.
  CHECK_FOR_DIRTY_LAYOUT(Lifecycle().GetState() <
                         DocumentLifecycle::kLayoutClean);

  return true;
}

void FrameView::ScheduleRelayout() {
  DCHECK(frame_->View() == this);

  if (!layout_scheduling_enabled_)
    return;
  // TODO(crbug.com/590856): It's still broken when we choose not to crash when
  // the check fails.
  if (!CheckLayoutInvalidationIsAllowed())
    return;
  if (!NeedsLayout())
    return;
  if (!frame_->GetDocument()->ShouldScheduleLayout())
    return;
  TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("devtools.timeline"),
                       "InvalidateLayout", TRACE_EVENT_SCOPE_THREAD, "data",
                       InspectorInvalidateLayoutEvent::Data(frame_.Get()));

  ClearLayoutSubtreeRootsAndMarkContainingBlocks();

  if (has_pending_layout_)
    return;
  has_pending_layout_ = true;

  if (!ShouldThrottleRendering())
    GetPage()->Animator().ScheduleVisualUpdate(frame_.Get());
}

void FrameView::ScheduleRelayoutOfSubtree(LayoutObject* relayout_root) {
  DCHECK(frame_->View() == this);

  // TODO(crbug.com/590856): It's still broken when we choose not to crash when
  // the check fails.
  if (!CheckLayoutInvalidationIsAllowed())
    return;

  // FIXME: Should this call shouldScheduleLayout instead?
  if (!frame_->GetDocument()->IsActive())
    return;

  LayoutView* layout_view = this->GetLayoutView();
  if (layout_view && layout_view->NeedsLayout()) {
    if (relayout_root)
      relayout_root->MarkContainerChainForLayout(false);
    return;
  }

  if (relayout_root == layout_view)
    layout_subtree_root_list_.ClearAndMarkContainingBlocksForLayout();
  else
    layout_subtree_root_list_.Add(*relayout_root);

  if (layout_scheduling_enabled_) {
    has_pending_layout_ = true;

    if (!ShouldThrottleRendering())
      GetPage()->Animator().ScheduleVisualUpdate(frame_.Get());

    Lifecycle().EnsureStateAtMost(DocumentLifecycle::kStyleClean);
  }
  TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("devtools.timeline"),
                       "InvalidateLayout", TRACE_EVENT_SCOPE_THREAD, "data",
                       InspectorInvalidateLayoutEvent::Data(frame_.Get()));
}

bool FrameView::LayoutPending() const {
  // FIXME: This should check Document::lifecycle instead.
  return has_pending_layout_;
}

bool FrameView::IsInPerformLayout() const {
  return Lifecycle().GetState() == DocumentLifecycle::kInPerformLayout;
}

bool FrameView::NeedsLayout() const {
  // This can return true in cases where the document does not have a body yet.
  // Document::shouldScheduleLayout takes care of preventing us from scheduling
  // layout in that case.

  LayoutViewItem layout_view_item = this->GetLayoutViewItem();
  return LayoutPending() ||
         (!layout_view_item.IsNull() && layout_view_item.NeedsLayout()) ||
         IsSubtreeLayout();
}

NOINLINE bool FrameView::CheckDoesNotNeedLayout() const {
  CHECK_FOR_DIRTY_LAYOUT(!LayoutPending());
  CHECK_FOR_DIRTY_LAYOUT(GetLayoutViewItem().IsNull() ||
                         !GetLayoutViewItem().NeedsLayout());
  CHECK_FOR_DIRTY_LAYOUT(!IsSubtreeLayout());
  return true;
}

void FrameView::SetNeedsLayout() {
  LayoutViewItem layout_view_item = this->GetLayoutViewItem();
  if (layout_view_item.IsNull())
    return;
  // TODO(crbug.com/590856): It's still broken if we choose not to crash when
  // the check fails.
  if (!CheckLayoutInvalidationIsAllowed())
    return;
  layout_view_item.SetNeedsLayout(LayoutInvalidationReason::kUnknown);
}

bool FrameView::HasOpaqueBackground() const {
  return !base_background_color_.HasAlpha();
}

Color FrameView::BaseBackgroundColor() const {
  return base_background_color_;
}

void FrameView::SetBaseBackgroundColor(const Color& background_color) {
  if (base_background_color_ == background_color)
    return;

  base_background_color_ = background_color;

  if (!GetLayoutViewItem().IsNull() &&
      GetLayoutViewItem().Layer()->HasCompositedLayerMapping()) {
    CompositedLayerMapping* composited_layer_mapping =
        GetLayoutViewItem().Layer()->GetCompositedLayerMapping();
    composited_layer_mapping->UpdateContentsOpaque();
    if (composited_layer_mapping->MainGraphicsLayer())
      composited_layer_mapping->MainGraphicsLayer()->SetNeedsDisplay();
  }
  RecalculateScrollbarOverlayColorTheme(DocumentBackgroundColor());

  if (!ShouldThrottleRendering())
    GetPage()->Animator().ScheduleVisualUpdate(frame_.Get());
}

void FrameView::UpdateBaseBackgroundColorRecursively(
    const Color& base_background_color) {
  ForAllNonThrottledFrameViews([base_background_color](FrameView& frame_view) {
    frame_view.SetBaseBackgroundColor(base_background_color);
  });
}

void FrameView::ScrollToFragmentAnchor() {
  Node* anchor_node = fragment_anchor_;
  if (!anchor_node)
    return;

  // Scrolling is disabled during updateScrollbars (see
  // isProgrammaticallyScrollable).  Bail now to avoid clearing m_fragmentAnchor
  // before we actually have a chance to scroll.
  if (in_update_scrollbars_)
    return;

  if (anchor_node->GetLayoutObject()) {
    LayoutRect rect;
    if (anchor_node != frame_->GetDocument()) {
      rect = anchor_node->BoundingBox();
    } else if (RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
      if (Element* document_element = frame_->GetDocument()->documentElement())
        rect = document_element->BoundingBox();
    }

    Frame* boundary_frame = frame_->FindUnsafeParentScrollPropagationBoundary();

    // FIXME: Handle RemoteFrames
    if (boundary_frame && boundary_frame->IsLocalFrame())
      ToLocalFrame(boundary_frame)
          ->View()
          ->SetSafeToPropagateScrollToParent(false);

    // Scroll nested layers and frames to reveal the anchor.
    // Align to the top and to the closest side (this matches other browsers).
    anchor_node->GetLayoutObject()->ScrollRectToVisible(
        rect, ScrollAlignment::kAlignToEdgeIfNeeded,
        ScrollAlignment::kAlignTopAlways);

    if (boundary_frame && boundary_frame->IsLocalFrame())
      ToLocalFrame(boundary_frame)
          ->View()
          ->SetSafeToPropagateScrollToParent(true);

    if (AXObjectCache* cache = frame_->GetDocument()->ExistingAXObjectCache())
      cache->HandleScrolledToAnchor(anchor_node);
  }

  // The fragment anchor should only be maintained while the frame is still
  // loading.  If the frame is done loading, clear the anchor now. Otherwise,
  // restore it since it may have been cleared during scrollRectToVisible.
  fragment_anchor_ =
      frame_->GetDocument()->IsLoadCompleted() ? nullptr : anchor_node;
}

bool FrameView::UpdatePlugins() {
  // This is always called from UpdatePluginsTimerFired.
  // update_plugins_timer should only be scheduled if we have FrameViews to
  // update. Thus I believe we can stop checking isEmpty here, and just ASSERT
  // isEmpty:
  // FIXME: This assert has been temporarily removed due to
  // https://crbug.com/430344
  if (nested_layout_count_ > 1 || part_update_set_.IsEmpty())
    return true;

  // Need to swap because script will run inside the below loop and invalidate
  // the iterator.
  EmbeddedObjectSet objects;
  objects.swap(part_update_set_);

  for (const auto& embedded_object : objects) {
    LayoutEmbeddedObject& object = *embedded_object;
    HTMLPlugInElement* element = ToHTMLPlugInElement(object.GetNode());

    // The object may have already been destroyed (thus node cleared),
    // but FrameView holds a manual ref, so it won't have been deleted.
    if (!element)
      continue;

    // No need to update if it's already crashed or known to be missing.
    if (object.ShowsUnavailablePluginIndicator())
      continue;

    if (element->NeedsPluginUpdate())
      element->UpdatePlugin();
    object.UpdateGeometry();

    // Prevent plugins from causing infinite updates of themselves.
    // FIXME: Do we really need to prevent this?
    part_update_set_.erase(&object);
  }

  return part_update_set_.IsEmpty();
}

void FrameView::UpdatePluginsTimerFired(TimerBase*) {
  DCHECK(!IsInPerformLayout());
  for (unsigned i = 0; i < kMaxUpdatePluginsIterations; ++i) {
    if (UpdatePlugins())
      return;
  }
}

void FrameView::FlushAnyPendingPostLayoutTasks() {
  DCHECK(!IsInPerformLayout());
  if (post_layout_tasks_timer_.IsActive())
    PerformPostLayoutTasks();
  if (update_plugins_timer_.IsActive()) {
    update_plugins_timer_.Stop();
    UpdatePluginsTimerFired(nullptr);
  }
}

void FrameView::ScheduleUpdatePluginsIfNecessary() {
  DCHECK(!IsInPerformLayout());
  if (update_plugins_timer_.IsActive() || part_update_set_.IsEmpty())
    return;
  update_plugins_timer_.StartOneShot(0, BLINK_FROM_HERE);
}

void FrameView::PerformPostLayoutTasks() {
  // FIXME: We can reach here, even when the page is not active!
  // http/tests/inspector/elements/html-link-import.html and many other
  // tests hit that case.
  // We should DCHECK(isActive()); or at least return early if we can!

  // Always called before or after performLayout(), part of the highest-level
  // layout() call.
  DCHECK(!IsInPerformLayout());
  TRACE_EVENT0("blink,benchmark", "FrameView::performPostLayoutTasks");

  post_layout_tasks_timer_.Stop();

  frame_->Selection().DidLayout();

  DCHECK(frame_->GetDocument());

  FontFaceSet::DidLayout(*frame_->GetDocument());
  // Cursor update scheduling is done by the local root, which is the main frame
  // if there are no RemoteFrame ancestors in the frame tree. Use of
  // localFrameRoot() is discouraged but will change when cursor update
  // scheduling is moved from EventHandler to PageEventHandler.
  GetFrame().LocalFrameRoot().GetEventHandler().ScheduleCursorUpdate();

  UpdateGeometries();

  // Plugins could have torn down the page inside updateGeometries().
  if (GetLayoutViewItem().IsNull())
    return;

  ScheduleUpdatePluginsIfNecessary();

  if (ScrollingCoordinator* scrolling_coordinator =
          this->GetScrollingCoordinator())
    scrolling_coordinator->NotifyGeometryChanged();

  ScrollToFragmentAnchor();
  SendResizeEventIfNeeded();
}

bool FrameView::WasViewportResized() {
  DCHECK(frame_);
  LayoutViewItem layout_view_item = this->GetLayoutViewItem();
  if (layout_view_item.IsNull())
    return false;
  DCHECK(layout_view_item.Style());
  return (GetLayoutSize(kIncludeScrollbars) != last_viewport_size_ ||
          layout_view_item.Style()->Zoom() != last_zoom_factor_);
}

void FrameView::SendResizeEventIfNeeded() {
  DCHECK(frame_);

  LayoutViewItem layout_view_item = this->GetLayoutViewItem();
  if (layout_view_item.IsNull() || layout_view_item.GetDocument().Printing())
    return;

  if (!WasViewportResized())
    return;

  last_viewport_size_ = GetLayoutSize(kIncludeScrollbars);
  last_zoom_factor_ = layout_view_item.Style()->Zoom();

  if (RuntimeEnabledFeatures::visualViewportAPIEnabled())
    frame_->GetDocument()->EnqueueVisualViewportResizeEvent();

  frame_->GetDocument()->EnqueueResizeEvent();

  if (frame_->IsMainFrame())
    probe::didResizeMainFrame(frame_.Get());
}

void FrameView::PostLayoutTimerFired(TimerBase*) {
  PerformPostLayoutTasks();
}

void FrameView::UpdateCounters() {
  LayoutView* view = GetLayoutView();
  if (!view->HasLayoutCounters())
    return;

  for (LayoutObject* layout_object = view; layout_object;
       layout_object = layout_object->NextInPreOrder()) {
    if (!layout_object->IsCounter())
      continue;

    ToLayoutCounter(layout_object)->UpdateCounter();
  }
}

bool FrameView::ShouldUseIntegerScrollOffset() const {
  if (frame_->GetSettings() &&
      !frame_->GetSettings()->GetPreferCompositingToLCDTextEnabled())
    return true;

  return ScrollableArea::ShouldUseIntegerScrollOffset();
}

bool FrameView::IsActive() const {
  Page* page = GetFrame().GetPage();
  return page && page->GetFocusController().IsActive();
}

void FrameView::InvalidatePaintForTickmarks() {
  if (Scrollbar* scrollbar = VerticalScrollbar())
    scrollbar->SetNeedsPaintInvalidation(
        static_cast<ScrollbarPart>(~kThumbPart));
}

void FrameView::GetTickmarks(Vector<IntRect>& tickmarks) const {
  if (!tickmarks_.IsEmpty()) {
    tickmarks = tickmarks_;
    return;
  }
  tickmarks =
      GetFrame().GetDocument()->Markers().RenderedRectsForTextMatchMarkers();
}

void FrameView::SetInputEventsTransformForEmulation(
    const IntSize& offset,
    float content_scale_factor) {
  input_events_offset_for_emulation_ = offset;
  input_events_scale_factor_for_emulation_ = content_scale_factor;
}

IntSize FrameView::InputEventsOffsetForEmulation() const {
  return input_events_offset_for_emulation_;
}

float FrameView::InputEventsScaleFactor() const {
  float page_scale = frame_->GetPage()->GetVisualViewport().Scale();
  return page_scale * input_events_scale_factor_for_emulation_;
}

bool FrameView::ScrollbarsCanBeActive() const {
  if (frame_->View() != this)
    return false;

  return !!frame_->GetDocument();
}

void FrameView::ScrollbarVisibilityChanged() {
  UpdateScrollbarEnabledState();
  LayoutViewItem view_item = GetLayoutViewItem();
  if (!view_item.IsNull())
    view_item.ClearHitTestCache();
}

void FrameView::ScrollbarFrameRectChanged() {
  SetNeedsPaintPropertyUpdate();
}

IntRect FrameView::ScrollableAreaBoundingBox() const {
  LayoutPartItem owner_layout_item = GetFrame().OwnerLayoutItem();
  if (owner_layout_item.IsNull())
    return FrameRect();

  return owner_layout_item.AbsoluteContentQuad(kTraverseDocumentBoundaries)
      .EnclosingBoundingBox();
}

bool FrameView::IsScrollable() const {
  return GetScrollingReasons() == kScrollable;
}

bool FrameView::IsProgrammaticallyScrollable() {
  return !in_update_scrollbars_;
}

FrameView::ScrollingReasons FrameView::GetScrollingReasons() const {
  // Check for:
  // 1) If there an actual overflow.
  // 2) display:none or visibility:hidden set to self or inherited.
  // 3) overflow{-x,-y}: hidden;
  // 4) scrolling: no;

  // Covers #1
  IntSize contents_size = this->ContentsSize();
  IntSize visible_content_size = VisibleContentRect().Size();
  if ((contents_size.Height() <= visible_content_size.Height() &&
       contents_size.Width() <= visible_content_size.Width()))
    return kNotScrollableNoOverflow;

  // Covers #2.
  // FIXME: Do we need to fix this for OOPI?
  HTMLFrameOwnerElement* owner = frame_->DeprecatedLocalOwner();
  if (owner && (!owner->GetLayoutObject() ||
                !owner->GetLayoutObject()->VisibleToHitTesting()))
    return kNotScrollableNotVisible;

  // Cover #3 and #4.
  ScrollbarMode horizontal_mode;
  ScrollbarMode vertical_mode;
  GetLayoutView()->CalculateScrollbarModes(horizontal_mode, vertical_mode);
  if (horizontal_mode == kScrollbarAlwaysOff &&
      vertical_mode == kScrollbarAlwaysOff)
    return kNotScrollableExplicitlyDisabled;

  return kScrollable;
}

void FrameView::UpdateParentScrollableAreaSet() {
  if (RuntimeEnabledFeatures::rootLayerScrollingEnabled())
    return;

  // That ensures that only inner frames are cached.
  FrameView* parent_frame_view = ParentFrameView();
  if (!parent_frame_view)
    return;

  if (!IsScrollable()) {
    parent_frame_view->RemoveScrollableArea(this);
    return;
  }

  parent_frame_view->AddScrollableArea(this);
}

bool FrameView::ShouldSuspendScrollAnimations() const {
  return !frame_->GetDocument()->LoadEventFinished();
}

void FrameView::ScrollbarStyleChanged() {
  // FIXME: Why does this only apply to the main frame?
  if (!frame_->IsMainFrame())
    return;
  AdjustScrollbarOpacity();
  ContentsResized();
  UpdateScrollbars();
  PositionScrollbarLayers();
}

bool FrameView::ScheduleAnimation() {
  if (PlatformChromeClient* client = GetChromeClient()) {
    client->ScheduleAnimation(this);
    return true;
  }
  return false;
}

void FrameView::NotifyPageThatContentAreaWillPaint() const {
  Page* page = frame_->GetPage();
  if (!page)
    return;

  ContentAreaWillPaint();

  if (!scrollable_areas_)
    return;

  for (const auto& scrollable_area : *scrollable_areas_) {
    if (!scrollable_area->ScrollbarsCanBeActive())
      continue;

    scrollable_area->ContentAreaWillPaint();
  }
}

bool FrameView::ScrollAnimatorEnabled() const {
  return frame_->GetSettings() &&
         frame_->GetSettings()->GetScrollAnimatorEnabled();
}

void FrameView::UpdateDocumentAnnotatedRegions() const {
  Document* document = frame_->GetDocument();
  if (!document->HasAnnotatedRegions())
    return;
  Vector<AnnotatedRegionValue> new_regions;
  CollectAnnotatedRegions(*(document->GetLayoutBox()), new_regions);
  if (new_regions == document->AnnotatedRegions())
    return;
  document->SetAnnotatedRegions(new_regions);
  if (Page* page = frame_->GetPage())
    page->GetChromeClient().AnnotatedRegionsChanged();
}

void FrameView::DidAttachDocument() {
  Page* page = frame_->GetPage();
  DCHECK(page);

  DCHECK(frame_->GetDocument());

  if (frame_->IsMainFrame()) {
    ScrollableArea& visual_viewport = frame_->GetPage()->GetVisualViewport();
    ScrollableArea* layout_viewport = LayoutViewportScrollableArea();
    DCHECK(layout_viewport);

    RootFrameViewport* root_frame_viewport =
        RootFrameViewport::Create(visual_viewport, *layout_viewport);
    viewport_scrollable_area_ = root_frame_viewport;

    page->GlobalRootScrollerController().InitializeViewportScrollCallback(
        *root_frame_viewport);
  }
}

void FrameView::UpdateScrollCorner() {
  RefPtr<ComputedStyle> corner_style;
  IntRect corner_rect = ScrollCornerRect();
  Document* doc = frame_->GetDocument();

  if (doc && !corner_rect.IsEmpty()) {
    // Try the <body> element first as a scroll corner source.
    if (Element* body = doc->body()) {
      if (LayoutObject* layout_object = body->GetLayoutObject())
        corner_style = layout_object->GetUncachedPseudoStyle(
            PseudoStyleRequest(kPseudoIdScrollbarCorner),
            layout_object->Style());
    }

    if (!corner_style) {
      // If the <body> didn't have a custom style, then the root element might.
      if (Element* doc_element = doc->documentElement()) {
        if (LayoutObject* layout_object = doc_element->GetLayoutObject())
          corner_style = layout_object->GetUncachedPseudoStyle(
              PseudoStyleRequest(kPseudoIdScrollbarCorner),
              layout_object->Style());
      }
    }

    if (!corner_style) {
      // If we have an owning ipage/LocalFrame element, then it can set the
      // custom scrollbar also.
      LayoutPartItem layout_item = frame_->OwnerLayoutItem();
      if (!layout_item.IsNull())
        corner_style = layout_item.GetUncachedPseudoStyle(
            PseudoStyleRequest(kPseudoIdScrollbarCorner), layout_item.Style());
    }
  }

  if (corner_style) {
    if (!scroll_corner_)
      scroll_corner_ = LayoutScrollbarPart::CreateAnonymous(doc, this);
    scroll_corner_->SetStyleWithWritingModeOfParent(std::move(corner_style));
    SetScrollCornerNeedsPaintInvalidation();
  } else if (scroll_corner_) {
    scroll_corner_->Destroy();
    scroll_corner_ = nullptr;
  }
}

Color FrameView::DocumentBackgroundColor() const {
  // The LayoutView's background color is set in
  // Document::inheritHtmlAndBodyElementStyles.  Blend this with the base
  // background color of the FrameView. This should match the color drawn by
  // ViewPainter::paintBoxDecorationBackground.
  Color result = BaseBackgroundColor();
  LayoutItem document_layout_object = GetLayoutViewItem();
  if (!document_layout_object.IsNull())
    result = result.Blend(
        document_layout_object.ResolveColor(CSSPropertyBackgroundColor));
  return result;
}

FrameView* FrameView::ParentFrameView() const {
  if (!Parent())
    return nullptr;

  Frame* parent_frame = frame_->Tree().Parent();
  if (parent_frame && parent_frame->IsLocalFrame())
    return ToLocalFrame(parent_frame)->View();

  return nullptr;
}

void FrameView::DidChangeGlobalRootScroller() {
  // Being the global root scroller will affect clipping size due to browser
  // controls behavior so we need to update compositing based on updated clip
  // geometry.
  LayoutViewItem view = GetLayoutViewItem();
  SetNeedsCompositingUpdate(view, kCompositingUpdateAfterGeometryChange);
  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled())
    SetNeedsPaintPropertyUpdate();

  // Avoid drawing two sets of scrollbars when visual viewport provides
  // scrollbars.
  if (frame_->GetSettings() && frame_->GetSettings()->GetViewportEnabled())
    VisualViewportScrollbarsChanged();
}

// TODO(pdr): This logic is similar to adjustScrollbarExistence and the common
// logic should be factored into a helper.
void FrameView::VisualViewportScrollbarsChanged() {
  bool has_horizontal_scrollbar = HorizontalScrollbar();
  bool has_vertical_scrollbar = VerticalScrollbar();
  bool should_have_horizontal_scrollbar = false;
  bool should_have_vertical_scrollbar = false;
  ComputeScrollbarExistence(should_have_horizontal_scrollbar,
                            should_have_vertical_scrollbar, ContentsSize());
  scrollbar_manager_.SetHasHorizontalScrollbar(
      should_have_horizontal_scrollbar);
  scrollbar_manager_.SetHasVerticalScrollbar(should_have_vertical_scrollbar);

  if (has_horizontal_scrollbar != should_have_horizontal_scrollbar ||
      has_vertical_scrollbar != should_have_vertical_scrollbar) {
    ScrollbarExistenceDidChange();

    if (!VisualViewportSuppliesScrollbars())
      UpdateScrollbarGeometry();
  }
}

void FrameView::UpdateGeometriesIfNeeded() {
  if (!needs_update_geometries_)
    return;

  needs_update_geometries_ = false;

  UpdateGeometries();
}

void FrameView::UpdateAllLifecyclePhases() {
  GetFrame().LocalFrameRoot().View()->UpdateLifecyclePhasesInternal(
      DocumentLifecycle::kPaintClean);
}

// TODO(chrishtr): add a scrolling update lifecycle phase.
void FrameView::UpdateLifecycleToCompositingCleanPlusScrolling() {
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    UpdateAllLifecyclePhasesExceptPaint();
  } else {
    GetFrame().LocalFrameRoot().View()->UpdateLifecyclePhasesInternal(
        DocumentLifecycle::kCompositingClean);
  }
}

void FrameView::UpdateLifecycleToCompositingInputsClean() {
  // When SPv2 is enabled, the standard compositing lifecycle steps do not
  // exist; compositing is done after paint instead.
  DCHECK(!RuntimeEnabledFeatures::slimmingPaintV2Enabled());
  GetFrame().LocalFrameRoot().View()->UpdateLifecyclePhasesInternal(
      DocumentLifecycle::kCompositingInputsClean);
}

void FrameView::UpdateAllLifecyclePhasesExceptPaint() {
  GetFrame().LocalFrameRoot().View()->UpdateLifecyclePhasesInternal(
      DocumentLifecycle::kPrePaintClean);
}

void FrameView::UpdateLifecycleToLayoutClean() {
  GetFrame().LocalFrameRoot().View()->UpdateLifecyclePhasesInternal(
      DocumentLifecycle::kLayoutClean);
}

void FrameView::ScheduleVisualUpdateForPaintInvalidationIfNeeded() {
  LocalFrame& local_frame_root = GetFrame().LocalFrameRoot();
  if (local_frame_root.View()->current_update_lifecycle_phases_target_state_ <
          DocumentLifecycle::kPaintInvalidationClean ||
      Lifecycle().GetState() >= DocumentLifecycle::kPrePaintClean) {
    // Schedule visual update to process the paint invalidation in the next
    // cycle.
    local_frame_root.ScheduleVisualUpdateUnlessThrottled();
  }
  // Otherwise the paint invalidation will be handled in paint invalidation
  // phase of this cycle.
}

void FrameView::NotifyResizeObservers() {
  // Controller exists only if ResizeObserver was created.
  if (!GetFrame().GetDocument()->GetResizeObserverController())
    return;

  ResizeObserverController& resize_controller =
      frame_->GetDocument()->EnsureResizeObserverController();

  DCHECK(Lifecycle().GetState() >= DocumentLifecycle::kLayoutClean);

  size_t min_depth = 0;
  for (min_depth = resize_controller.GatherObservations(0);
       min_depth != ResizeObserverController::kDepthBottom;
       min_depth = resize_controller.GatherObservations(min_depth)) {
    resize_controller.DeliverObservations();
    GetFrame().GetDocument()->UpdateStyleAndLayout();
  }

  if (resize_controller.SkippedObservations()) {
    resize_controller.ClearObservations();
    ErrorEvent* error = ErrorEvent::Create(
        "ResizeObserver loop limit exceeded",
        SourceLocation::Capture(frame_->GetDocument()), nullptr);
    frame_->GetDocument()->DispatchErrorEvent(error, kNotSharableCrossOrigin);
    // Ensure notifications will get delivered in next cycle.
    if (FrameView* frame_view = frame_->View())
      frame_view->ScheduleAnimation();
  }

  DCHECK(!GetLayoutView()->NeedsLayout());
}

void FrameView::DispatchEventsForPrintingOnAllFrames() {
  DCHECK(frame_->IsMainFrame());
  for (Frame* current_frame = frame_; current_frame;
       current_frame = current_frame->Tree().TraverseNext(frame_)) {
    if (current_frame->IsLocalFrame())
      ToLocalFrame(current_frame)->GetDocument()->DispatchEventsForPrinting();
  }
}

void FrameView::SetupPrintContext() {
  if (frame_->GetDocument()->Printing())
    return;
  if (!print_context_)
    print_context_ = new PrintContext(frame_);
  if (frame_->GetSettings())
    frame_->GetSettings()->SetShouldPrintBackgrounds(true);
  bool is_us = DefaultLanguage() == "en-US";
  int width = is_us ? kLetterPortraitPageWidth : kA4PortraitPageWidth;
  int height = is_us ? kLetterPortraitPageHeight : kA4PortraitPageHeight;
  print_context_->BeginPrintMode(width, height);
  print_context_->ComputePageRects(FloatSize(width, height));
  DispatchEventsForPrintingOnAllFrames();
}

void FrameView::ClearPrintContext() {
  if (!print_context_)
    return;
  print_context_->EndPrintMode();
  print_context_.Clear();
}

// TODO(leviw): We don't assert lifecycle information from documents in child
// PluginViews.
void FrameView::UpdateLifecyclePhasesInternal(
    DocumentLifecycle::LifecycleState target_state) {
  if (current_update_lifecycle_phases_target_state_ !=
      DocumentLifecycle::kUninitialized) {
    NOTREACHED() << "FrameView::updateLifecyclePhasesInternal() reentrance";
    return;
  }

  // This must be called from the root frame, since it recurses down, not up.
  // Otherwise the lifecycles of the frames might be out of sync.
  DCHECK(frame_->IsLocalRoot());

  // Only the following target states are supported.
  DCHECK(target_state == DocumentLifecycle::kLayoutClean ||
         target_state == DocumentLifecycle::kCompositingInputsClean ||
         target_state == DocumentLifecycle::kCompositingClean ||
         target_state == DocumentLifecycle::kPrePaintClean ||
         target_state == DocumentLifecycle::kPaintClean);

  if (!frame_->GetDocument()->IsActive())
    return;

  AutoReset<DocumentLifecycle::LifecycleState> target_state_scope(
      &current_update_lifecycle_phases_target_state_, target_state);

  if (ShouldThrottleRendering()) {
    UpdateViewportIntersectionsForSubtree(
        std::min(target_state, DocumentLifecycle::kCompositingClean));
    return;
  }

  if (RuntimeEnabledFeatures::printBrowserEnabled())
    SetupPrintContext();
  else
    ClearPrintContext();

  UpdateStyleAndLayoutIfNeededRecursive();
  DCHECK(Lifecycle().GetState() >= DocumentLifecycle::kLayoutClean);

  if (target_state == DocumentLifecycle::kLayoutClean) {
    UpdateViewportIntersectionsForSubtree(target_state);
    return;
  }

  ForAllNonThrottledFrameViews([](FrameView& frame_view) {
    frame_view.PerformScrollAnchoringAdjustments();
  });

  if (target_state == DocumentLifecycle::kPaintClean) {
    ForAllNonThrottledFrameViews(
        [](FrameView& frame_view) { frame_view.NotifyResizeObservers(); });
  }

  if (LayoutViewItem view = GetLayoutViewItem()) {
    ForAllNonThrottledFrameViews([](FrameView& frame_view) {
      frame_view.CheckDoesNotNeedLayout();
      frame_view.allows_layout_invalidation_after_layout_clean_ = false;
    });

    {
      TRACE_EVENT1("devtools.timeline", "UpdateLayerTree", "data",
                   InspectorUpdateLayerTreeEvent::Data(frame_.Get()));

      if (!RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
        view.Compositor()->UpdateIfNeededRecursive(target_state);
      } else {
        ForAllNonThrottledFrameViews([](FrameView& frame_view) {
          frame_view.GetLayoutView()->Layer()->UpdateDescendantDependentFlags();
          frame_view.GetLayoutView()->CommitPendingSelection();
        });
      }

      if (target_state >= DocumentLifecycle::kCompositingClean) {
        ScrollContentsIfNeededRecursive();

        frame_->GetPage()
            ->GlobalRootScrollerController()
            .DidUpdateCompositing();
      }

      if (target_state >= DocumentLifecycle::kPrePaintClean) {
        if (!RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled())
          DeprecatedInvalidateTreeRecursive();

        if (!RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
          if (view.Compositor()->InCompositingMode())
            GetScrollingCoordinator()->UpdateAfterCompositingChangeIfNeeded();
        }

        // This is needed since, at present, the ScrollingCoordinator doesn't
        // send rects for oopif sub-frames.
        // TODO(wjmaclean): Remove this pathway when ScrollingCoordinator
        // operates on a per-frame basis. https://crbug.com/680606
        GetFrame()
            .GetPage()
            ->GetChromeClient()
            .UpdateEventRectsForSubframeIfNecessary(&frame_->LocalFrameRoot());
        UpdateCompositedSelectionIfNeeded();

        // TODO(pdr): prePaint should be under the "Paint" devtools timeline
        // step for slimming paint v2.
        PrePaint();
      }
    }

    if (target_state == DocumentLifecycle::kPaintClean) {
      if (!frame_->GetDocument()->Printing() ||
          RuntimeEnabledFeatures::printBrowserEnabled())
        PaintTree();

      if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
        Optional<CompositorElementIdSet> composited_element_ids =
            CompositorElementIdSet();
        PushPaintArtifactToCompositor(composited_element_ids.value());
        DocumentAnimations::UpdateAnimations(GetLayoutView()->GetDocument(),
                                             DocumentLifecycle::kPaintClean,
                                             composited_element_ids);
      }

      DCHECK(!frame_->Selection().NeedsLayoutSelectionUpdate());
      DCHECK((frame_->GetDocument()->Printing() &&
              Lifecycle().GetState() == DocumentLifecycle::kPrePaintClean) ||
             Lifecycle().GetState() == DocumentLifecycle::kPaintClean);
    }

    ForAllNonThrottledFrameViews([](FrameView& frame_view) {
      frame_view.CheckDoesNotNeedLayout();
      frame_view.allows_layout_invalidation_after_layout_clean_ = true;
    });
  }

  UpdateViewportIntersectionsForSubtree(target_state);
}

void FrameView::EnqueueScrollAnchoringAdjustment(
    ScrollableArea* scrollable_area) {
  anchoring_adjustment_queue_.insert(scrollable_area);
}

void FrameView::PerformScrollAnchoringAdjustments() {
  for (WeakMember<ScrollableArea>& scroller : anchoring_adjustment_queue_) {
    if (scroller) {
      DCHECK(scroller->GetScrollAnchor());
      scroller->GetScrollAnchor()->Adjust();
    }
  }
  anchoring_adjustment_queue_.clear();
}

void FrameView::PrePaint() {
  TRACE_EVENT0("blink", "FrameView::prePaint");

  if (!paint_controller_)
    paint_controller_ = PaintController::Create();

  ForAllNonThrottledFrameViews([](FrameView& frame_view) {
    frame_view.Lifecycle().AdvanceTo(DocumentLifecycle::kInPrePaint);
    if (frame_view.CanThrottleRendering()) {
      // This frame can be throttled but not throttled, meaning we are not in an
      // AllowThrottlingScope. Now this frame may contain dirty paint flags, and
      // we need to propagate the flags into the ancestor chain so that
      // PrePaintTreeWalk can reach this frame.
      frame_view.SetNeedsPaintPropertyUpdate();
      if (auto owner = frame_view.GetFrame().OwnerLayoutItem())
        owner.SetMayNeedPaintInvalidation();
    }
  });

  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled()) {
    SCOPED_BLINK_UMA_HISTOGRAM_TIMER("Blink.PrePaint.UpdateTime");
    PrePaintTreeWalk().Walk(*this);
  }

  ForAllNonThrottledFrameViews([](FrameView& frame_view) {
    frame_view.Lifecycle().AdvanceTo(DocumentLifecycle::kPrePaintClean);
  });
}

void FrameView::PaintTree() {
  TRACE_EVENT0("blink", "FrameView::paintTree");
  SCOPED_BLINK_UMA_HISTOGRAM_TIMER("Blink.Paint.UpdateTime");

  DCHECK(GetFrame() == GetPage()->MainFrame() ||
         (!GetFrame().Tree().Parent()->IsLocalFrame()));

  LayoutViewItem view = GetLayoutViewItem();
  DCHECK(!view.IsNull());
  ForAllNonThrottledFrameViews([](FrameView& frame_view) {
    frame_view.Lifecycle().AdvanceTo(DocumentLifecycle::kInPaint);
  });

  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    if (GetLayoutView()->Layer()->NeedsRepaint()) {
      GraphicsContext graphics_context(*paint_controller_);
      if (RuntimeEnabledFeatures::printBrowserEnabled())
        graphics_context.SetPrinting(true);
      Paint(graphics_context, CullRect(LayoutRect::InfiniteIntRect()));
      paint_controller_->CommitNewDisplayItems(LayoutSize());
    }
  } else {
    // A null graphics layer can occur for painting of SVG images that are not
    // parented into the main frame tree, or when the FrameView is the main
    // frame view of a page overlay. The page overlay is in the layer tree of
    // the host page and will be painted during painting of the host page.
    if (GraphicsLayer* root_graphics_layer =
            view.Compositor()->RootGraphicsLayer()) {
      PaintGraphicsLayerRecursively(root_graphics_layer);
    }

    // TODO(sataya.m):Main frame doesn't create RootFrameViewport in some
    // webkit_unit_tests (http://crbug.com/644788).
    if (viewport_scrollable_area_) {
      if (GraphicsLayer* layer_for_horizontal_scrollbar =
              viewport_scrollable_area_->LayerForHorizontalScrollbar()) {
        PaintGraphicsLayerRecursively(layer_for_horizontal_scrollbar);
      }
      if (GraphicsLayer* layer_for_vertical_scrollbar =
              viewport_scrollable_area_->LayerForVerticalScrollbar()) {
        PaintGraphicsLayerRecursively(layer_for_vertical_scrollbar);
      }
      if (GraphicsLayer* layer_for_scroll_corner =
              viewport_scrollable_area_->LayerForScrollCorner()) {
        PaintGraphicsLayerRecursively(layer_for_scroll_corner);
      }
    }
  }

  ForAllNonThrottledFrameViews([](FrameView& frame_view) {
    frame_view.Lifecycle().AdvanceTo(DocumentLifecycle::kPaintClean);
    LayoutViewItem layout_view_item = frame_view.GetLayoutViewItem();
    if (!layout_view_item.IsNull())
      layout_view_item.Layer()->ClearNeedsRepaintRecursively();
  });
}

void FrameView::PaintGraphicsLayerRecursively(GraphicsLayer* graphics_layer) {
  DCHECK(!RuntimeEnabledFeatures::slimmingPaintV2Enabled());
  if (graphics_layer->DrawsContent()) {
    graphics_layer->Paint(nullptr);
  }

  if (GraphicsLayer* mask_layer = graphics_layer->MaskLayer())
    PaintGraphicsLayerRecursively(mask_layer);
  if (GraphicsLayer* contents_clipping_mask_layer =
          graphics_layer->ContentsClippingMaskLayer())
    PaintGraphicsLayerRecursively(contents_clipping_mask_layer);

  for (auto& child : graphics_layer->Children())
    PaintGraphicsLayerRecursively(child);
}

void FrameView::PushPaintArtifactToCompositor(
    CompositorElementIdSet& composited_element_ids) {
  TRACE_EVENT0("blink", "FrameView::pushPaintArtifactToCompositor");

  DCHECK(RuntimeEnabledFeatures::slimmingPaintV2Enabled());

  Page* page = GetFrame().GetPage();
  if (!page)
    return;

  if (!paint_artifact_compositor_) {
    paint_artifact_compositor_ = PaintArtifactCompositor::Create();
    page->GetChromeClient().AttachRootLayer(
        paint_artifact_compositor_->GetWebLayer(), &GetFrame());
  }

  SCOPED_BLINK_UMA_HISTOGRAM_TIMER("Blink.Compositing.UpdateTime");

  paint_artifact_compositor_->Update(
      paint_controller_->GetPaintArtifact(),
      paint_controller_->PaintChunksRasterInvalidationTrackingMap(),
      is_storing_composited_layer_debug_info_, composited_element_ids);
}

std::unique_ptr<JSONObject> FrameView::CompositedLayersAsJSON(
    LayerTreeFlags flags) {
  return GetFrame()
      .LocalFrameRoot()
      .View()
      ->paint_artifact_compositor_->LayersAsJSON(flags);
}

void FrameView::UpdateStyleAndLayoutIfNeededRecursive() {
  SCOPED_BLINK_UMA_HISTOGRAM_TIMER("Blink.StyleAndLayout.UpdateTime");
  UpdateStyleAndLayoutIfNeededRecursiveInternal();
}

void FrameView::UpdateStyleAndLayoutIfNeededRecursiveInternal() {
  if (ShouldThrottleRendering() || !frame_->GetDocument()->IsActive())
    return;

  ScopedFrameBlamer frame_blamer(frame_);
  TRACE_EVENT0("blink", "FrameView::updateStyleAndLayoutIfNeededRecursive");

  // We have to crawl our entire subtree looking for any FrameViews that need
  // layout and make sure they are up to date.
  // Mac actually tests for intersection with the dirty region and tries not to
  // update layout for frames that are outside the dirty region.  Not only does
  // this seem pointless (since those frames will have set a zero timer to
  // layout anyway), but it is also incorrect, since if two frames overlap, the
  // first could be excluded from the dirty region but then become included
  // later by the second frame adding rects to the dirty region when it lays
  // out.

  frame_->GetDocument()->UpdateStyleAndLayoutTree();

  CHECK(!ShouldThrottleRendering());
  CHECK(frame_->GetDocument()->IsActive());
  CHECK(!nested_layout_count_);

  if (NeedsLayout())
    UpdateLayout();

  CheckDoesNotNeedLayout();

  // WebView plugins need to update regardless of whether the
  // LayoutEmbeddedObject that owns them needed layout.
  // TODO(leviw): This currently runs the entire lifecycle on plugin WebViews.
  // We should have a way to only run these other Documents to the same
  // lifecycle stage as this frame.
  for (const auto& plugin : plugins_) {
    plugin->UpdateAllLifecyclePhases();
  }
  CheckDoesNotNeedLayout();

  // FIXME: Calling layout() shouldn't trigger script execution or have any
  // observable effects on the frame tree but we're not quite there yet.
  HeapVector<Member<FrameView>> frame_views;
  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (!child->IsLocalFrame())
      continue;
    if (FrameView* view = ToLocalFrame(child)->View())
      frame_views.push_back(view);
  }

  for (const auto& frame_view : frame_views)
    frame_view->UpdateStyleAndLayoutIfNeededRecursiveInternal();

  // These asserts ensure that parent frames are clean, when child frames
  // finished updating layout and style.
  CheckDoesNotNeedLayout();
#if DCHECK_IS_ON()
  frame_->GetDocument()->GetLayoutView()->AssertLaidOut();
#endif

  UpdateGeometriesIfNeeded();

  if (Lifecycle().GetState() < DocumentLifecycle::kLayoutClean)
    Lifecycle().AdvanceTo(DocumentLifecycle::kLayoutClean);

  // Ensure that we become visually non-empty eventually.
  // TODO(esprehn): This should check isRenderingReady() instead.
  if (GetFrame().GetDocument()->HasFinishedParsing() &&
      GetFrame().Loader().StateMachine()->CommittedFirstRealDocumentLoad())
    is_visually_non_empty_ = true;

  GetFrame().Selection().UpdateStyleAndLayoutIfNeeded();
  GetFrame().GetPage()->GetDragCaret().UpdateStyleAndLayoutIfNeeded();
}

void FrameView::DeprecatedInvalidateTreeRecursive() {
  SCOPED_BLINK_UMA_HISTOGRAM_TIMER("Blink.PaintInvalidation.UpdateTime");
  {
    // For comparison to SlimmingPaintInvalidation.
    SCOPED_BLINK_UMA_HISTOGRAM_TIMER("Blink.PrePaint.UpdateTime");
    DeprecatedInvalidateTreeRecursiveInternal();
  }
}

void FrameView::DeprecatedInvalidateTreeRecursiveInternal() {
  DCHECK(!RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled());
  CHECK(GetLayoutView());

  // We need to stop recursing here since a child frame view might not be
  // throttled even though we are (e.g., it didn't compute its visibility yet).
  if (ShouldThrottleRendering())
    return;
  TRACE_EVENT0("blink", "FrameView::invalidateTreeIfNeededRecursiveInternal");

  Vector<const LayoutObject*> pending_delayed_paint_invalidations;
  PaintInvalidationState root_paint_invalidation_state(
      *GetLayoutView(), pending_delayed_paint_invalidations);

  if (Lifecycle().GetState() < DocumentLifecycle::kPaintInvalidationClean)
    DeprecatedInvalidateTree(root_paint_invalidation_state);

  // Some frames may be not reached during the above DeprecatedInvalidateTree
  // because
  // - the frame is a detached frame; or
  // - it didn't need paint invalidation.
  // We need to call invalidateTreeIfNeededRecursiveInternal() for such frames
  // to finish required paint invalidation and advance their life cycle state.
  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (child->IsLocalFrame()) {
      FrameView& child_frame_view = *ToLocalFrame(child)->View();
      // The children frames can be in any state, including stopping.
      // Thus we have to check that it makes sense to do paint
      // invalidation onto them here.
      if (!child_frame_view.GetLayoutView())
        continue;
      child_frame_view.DeprecatedInvalidateTreeRecursiveInternal();
    }
  }

  // Process objects needing paint invalidation on the next frame. See the
  // definition of PaintInvalidationDelayedFull for more details.
  for (auto& target : pending_delayed_paint_invalidations) {
    target->GetMutableForPainting().SetShouldDoFullPaintInvalidation(
        PaintInvalidationReason::kDelayedFull);
  }
}

void FrameView::EnableAutoSizeMode(const IntSize& min_size,
                                   const IntSize& max_size) {
  if (!auto_size_info_)
    auto_size_info_ = FrameViewAutoSizeInfo::Create(this);

  auto_size_info_->ConfigureAutoSizeMode(min_size, max_size);
  SetLayoutSizeFixedToFrameSize(true);
  SetNeedsLayout();
  ScheduleRelayout();
}

void FrameView::DisableAutoSizeMode() {
  if (!auto_size_info_)
    return;

  SetLayoutSizeFixedToFrameSize(false);
  SetNeedsLayout();
  ScheduleRelayout();

  // Since autosize mode forces the scrollbar mode, change them to being auto.
  SetVerticalScrollbarLock(false);
  SetHorizontalScrollbarLock(false);
  SetScrollbarModes(kScrollbarAuto, kScrollbarAuto);
  auto_size_info_.Clear();
}

void FrameView::ForceLayoutForPagination(const FloatSize& page_size,
                                         const FloatSize& original_page_size,
                                         float maximum_shrink_factor) {
  // Dumping externalRepresentation(m_frame->layoutObject()).ascii() is a good
  // trick to see the state of things before and after the layout
  if (LayoutView* layout_view = this->GetLayoutView()) {
    float page_logical_width = layout_view->Style()->IsHorizontalWritingMode()
                                   ? page_size.Width()
                                   : page_size.Height();
    float page_logical_height = layout_view->Style()->IsHorizontalWritingMode()
                                    ? page_size.Height()
                                    : page_size.Width();

    LayoutUnit floored_page_logical_width =
        static_cast<LayoutUnit>(page_logical_width);
    LayoutUnit floored_page_logical_height =
        static_cast<LayoutUnit>(page_logical_height);
    layout_view->SetLogicalWidth(floored_page_logical_width);
    layout_view->SetPageLogicalHeight(floored_page_logical_height);
    layout_view->SetNeedsLayoutAndPrefWidthsRecalcAndFullPaintInvalidation(
        LayoutInvalidationReason::kPrintingChanged);
    UpdateLayout();

    // If we don't fit in the given page width, we'll lay out again. If we don't
    // fit in the page width when shrunk, we will lay out at maximum shrink and
    // clip extra content.
    // FIXME: We are assuming a shrink-to-fit printing implementation.  A
    // cropping implementation should not do this!
    bool horizontal_writing_mode =
        layout_view->Style()->IsHorizontalWritingMode();
    const LayoutRect& document_rect = LayoutRect(layout_view->DocumentRect());
    LayoutUnit doc_logical_width = horizontal_writing_mode
                                       ? document_rect.Width()
                                       : document_rect.Height();
    if (doc_logical_width > page_logical_width) {
      FloatSize expected_page_size(
          std::min<float>(document_rect.Width().ToFloat(),
                          page_size.Width() * maximum_shrink_factor),
          std::min<float>(document_rect.Height().ToFloat(),
                          page_size.Height() * maximum_shrink_factor));
      FloatSize max_page_size = frame_->ResizePageRectsKeepingRatio(
          FloatSize(original_page_size.Width(), original_page_size.Height()),
          expected_page_size);
      page_logical_width = horizontal_writing_mode ? max_page_size.Width()
                                                   : max_page_size.Height();
      page_logical_height = horizontal_writing_mode ? max_page_size.Height()
                                                    : max_page_size.Width();

      floored_page_logical_width = static_cast<LayoutUnit>(page_logical_width);
      floored_page_logical_height =
          static_cast<LayoutUnit>(page_logical_height);
      layout_view->SetLogicalWidth(floored_page_logical_width);
      layout_view->SetPageLogicalHeight(floored_page_logical_height);
      layout_view->SetNeedsLayoutAndPrefWidthsRecalcAndFullPaintInvalidation(
          LayoutInvalidationReason::kPrintingChanged);
      UpdateLayout();

      const LayoutRect& updated_document_rect =
          LayoutRect(layout_view->DocumentRect());
      LayoutUnit doc_logical_height = horizontal_writing_mode
                                          ? updated_document_rect.Height()
                                          : updated_document_rect.Width();
      LayoutUnit doc_logical_top = horizontal_writing_mode
                                       ? updated_document_rect.Y()
                                       : updated_document_rect.X();
      LayoutUnit doc_logical_right = horizontal_writing_mode
                                         ? updated_document_rect.MaxX()
                                         : updated_document_rect.MaxY();
      LayoutUnit clipped_logical_left;
      if (!layout_view->Style()->IsLeftToRightDirection())
        clipped_logical_left =
            LayoutUnit(doc_logical_right - page_logical_width);
      LayoutRect overflow(clipped_logical_left, doc_logical_top,
                          LayoutUnit(page_logical_width), doc_logical_height);

      if (!horizontal_writing_mode)
        overflow = overflow.TransposedRect();
      AdjustViewSizeAndLayout();
      // This is how we clip in case we overflow again.
      layout_view->ClearLayoutOverflow();
      layout_view->AddLayoutOverflow(overflow);
      return;
    }
  }

  if (TextAutosizer* text_autosizer = frame_->GetDocument()->GetTextAutosizer())
    text_autosizer->UpdatePageInfo();
  AdjustViewSizeAndLayout();
}

IntRect FrameView::ConvertFromLayoutItem(
    const LayoutItem& layout_item,
    const IntRect& layout_object_rect) const {
  // Convert from page ("absolute") to FrameView coordinates.
  LayoutRect rect = EnclosingLayoutRect(
      layout_item.LocalToAbsoluteQuad(FloatRect(layout_object_rect))
          .BoundingBox());
  rect.Move(LayoutSize(-GetScrollOffset()));
  return PixelSnappedIntRect(rect);
}

IntRect FrameView::ConvertToLayoutItem(const LayoutItem& layout_item,
                                       const IntRect& frame_rect) const {
  IntRect rect_in_content = FrameToContents(frame_rect);

  // Convert from FrameView coords into page ("absolute") coordinates.
  rect_in_content.Move(ScrollOffsetInt());

  // FIXME: we don't have a way to map an absolute rect down to a local quad, so
  // just move the rect for now.
  rect_in_content.SetLocation(RoundedIntPoint(
      layout_item.AbsoluteToLocal(rect_in_content.Location(), kUseTransforms)));
  return rect_in_content;
}

IntPoint FrameView::ConvertFromLayoutItem(
    const LayoutItem& layout_item,
    const IntPoint& layout_object_point) const {
  IntPoint point = RoundedIntPoint(
      layout_item.LocalToAbsolute(layout_object_point, kUseTransforms));

  // Convert from page ("absolute") to FrameView coordinates.
  point.Move(-ScrollOffsetInt());
  return point;
}

IntPoint FrameView::ConvertToLayoutItem(const LayoutItem& layout_item,
                                        const IntPoint& frame_point) const {
  IntPoint point = frame_point;

  // Convert from FrameView coords into page ("absolute") coordinates.
  point += IntSize(ScrollX(), ScrollY());

  return RoundedIntPoint(layout_item.AbsoluteToLocal(point, kUseTransforms));
}

IntPoint FrameView::ConvertSelfToChild(const FrameOrPlugin& child,
                                       const IntPoint& point) const {
  IntPoint new_point = point;
  new_point = FrameToContents(point);
  new_point.MoveBy(-child.FrameRect().Location());
  return new_point;
}

IntRect FrameView::ConvertToContainingFrameViewBase(
    const IntRect& local_rect) const {
  if (parent_) {
    // Get our layoutObject in the parent view
    LayoutPartItem layout_item = frame_->OwnerLayoutItem();
    if (layout_item.IsNull())
      return local_rect;

    IntRect rect(local_rect);
    // Add borders and padding??
    rect.Move((layout_item.BorderLeft() + layout_item.PaddingLeft()).ToInt(),
              (layout_item.BorderTop() + layout_item.PaddingTop()).ToInt());
    return parent_->ConvertFromLayoutItem(layout_item, rect);
  }

  return local_rect;
}

IntRect FrameView::ConvertFromContainingFrameViewBase(
    const IntRect& parent_rect) const {
  if (parent_) {
    IntRect local_rect = parent_rect;
    local_rect.SetLocation(
        parent_->ConvertSelfToChild(*this, local_rect.Location()));
    return local_rect;
  }

  return parent_rect;
}

IntPoint FrameView::ConvertToContainingFrameViewBase(
    const IntPoint& local_point) const {
  if (parent_) {
    // Get our layoutObject in the parent view
    LayoutPartItem layout_item = frame_->OwnerLayoutItem();
    if (layout_item.IsNull())
      return local_point;

    IntPoint point(local_point);

    // Add borders and padding
    point.Move((layout_item.BorderLeft() + layout_item.PaddingLeft()).ToInt(),
               (layout_item.BorderTop() + layout_item.PaddingTop()).ToInt());
    return parent_->ConvertFromLayoutItem(layout_item, point);
  }

  return local_point;
}

IntPoint FrameView::ConvertFromContainingFrameViewBase(
    const IntPoint& parent_point) const {
  if (parent_) {
    // Get our layoutObject in the parent view
    LayoutPartItem layout_item = frame_->OwnerLayoutItem();
    if (layout_item.IsNull())
      return parent_point;

    IntPoint point = parent_->ConvertToLayoutItem(layout_item, parent_point);
    // Subtract borders and padding
    point.Move((-layout_item.BorderLeft() - layout_item.PaddingLeft()).ToInt(),
               (-layout_item.BorderTop() - layout_item.PaddingTop()).ToInt());
    return point;
  }

  return parent_point;
}

void FrameView::SetInitialTracksPaintInvalidationsForTesting(
    bool track_paint_invalidations) {
  g_initial_track_all_paint_invalidations = track_paint_invalidations;
}

void FrameView::SetTracksPaintInvalidations(bool track_paint_invalidations) {
  if (track_paint_invalidations == IsTrackingPaintInvalidations())
    return;

  // Ensure the document is up-to-date before tracking invalidations.
  UpdateAllLifecyclePhases();

  for (Frame* frame = &frame_->Tree().Top(); frame;
       frame = frame->Tree().TraverseNext()) {
    if (!frame->IsLocalFrame())
      continue;
    if (LayoutViewItem layout_view = ToLocalFrame(frame)->ContentLayoutItem()) {
      layout_view.GetFrameView()->tracked_object_paint_invalidations_ =
          WTF::WrapUnique(track_paint_invalidations
                              ? new Vector<ObjectPaintInvalidation>
                              : nullptr);
      if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
        if (!paint_controller_)
          paint_controller_ = PaintController::Create();
        paint_controller_->SetTracksRasterInvalidations(
            track_paint_invalidations);
        if (paint_artifact_compositor_)
          paint_artifact_compositor_->ResetTrackedRasterInvalidations();
      } else {
        layout_view.Compositor()->SetTracksRasterInvalidations(
            track_paint_invalidations);
      }
    }
  }

  TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("blink.invalidation"),
                       "FrameView::setTracksPaintInvalidations",
                       TRACE_EVENT_SCOPE_GLOBAL, "enabled",
                       track_paint_invalidations);
}

void FrameView::TrackObjectPaintInvalidation(const DisplayItemClient& client,
                                             PaintInvalidationReason reason) {
  if (!tracked_object_paint_invalidations_)
    return;

  ObjectPaintInvalidation invalidation = {client.DebugName(), reason};
  tracked_object_paint_invalidations_->push_back(invalidation);
}

std::unique_ptr<JSONArray> FrameView::TrackedObjectPaintInvalidationsAsJSON()
    const {
  if (!tracked_object_paint_invalidations_)
    return nullptr;

  std::unique_ptr<JSONArray> result = JSONArray::Create();
  for (Frame* frame = &frame_->Tree().Top(); frame;
       frame = frame->Tree().TraverseNext()) {
    if (!frame->IsLocalFrame())
      continue;
    if (LayoutViewItem layout_view = ToLocalFrame(frame)->ContentLayoutItem()) {
      if (!layout_view.GetFrameView()->tracked_object_paint_invalidations_)
        continue;
      for (const auto& item :
           *layout_view.GetFrameView()->tracked_object_paint_invalidations_) {
        std::unique_ptr<JSONObject> item_json = JSONObject::Create();
        item_json->SetString("object", item.name);
        item_json->SetString("reason",
                             PaintInvalidationReasonToString(item.reason));
        result->PushObject(std::move(item_json));
      }
    }
  }
  return result;
}

void FrameView::AddResizerArea(LayoutBox& resizer_box) {
  if (!resizer_areas_)
    resizer_areas_ = WTF::WrapUnique(new ResizerAreaSet);
  resizer_areas_->insert(&resizer_box);
}

void FrameView::RemoveResizerArea(LayoutBox& resizer_box) {
  if (!resizer_areas_)
    return;

  ResizerAreaSet::iterator it = resizer_areas_->find(&resizer_box);
  if (it != resizer_areas_->end())
    resizer_areas_->erase(it);
}

void FrameView::AddScrollableArea(ScrollableArea* scrollable_area) {
  DCHECK(scrollable_area);
  if (!scrollable_areas_)
    scrollable_areas_ = new ScrollableAreaSet;
  scrollable_areas_->insert(scrollable_area);

  if (ScrollingCoordinator* scrolling_coordinator =
          this->GetScrollingCoordinator())
    scrolling_coordinator->ScrollableAreasDidChange();
}

void FrameView::RemoveScrollableArea(ScrollableArea* scrollable_area) {
  if (!scrollable_areas_)
    return;
  scrollable_areas_->erase(scrollable_area);

  if (ScrollingCoordinator* scrolling_coordinator =
          this->GetScrollingCoordinator())
    scrolling_coordinator->ScrollableAreasDidChange();
}

void FrameView::AddAnimatingScrollableArea(ScrollableArea* scrollable_area) {
  DCHECK(scrollable_area);
  if (!animating_scrollable_areas_)
    animating_scrollable_areas_ = new ScrollableAreaSet;
  animating_scrollable_areas_->insert(scrollable_area);
}

void FrameView::RemoveAnimatingScrollableArea(ScrollableArea* scrollable_area) {
  if (!animating_scrollable_areas_)
    return;
  animating_scrollable_areas_->erase(scrollable_area);
}

void FrameView::SetParent(FrameView* parent) {
  if (parent == parent_)
    return;

  DCHECK(!parent || !parent_);
  if (!parent || !parent->IsVisible())
    SetParentVisible(false);
  parent_ = parent;
  if (parent && parent->IsVisible())
    SetParentVisible(true);

  UpdateParentScrollableAreaSet();
  SetupRenderThrottling();

  if (ParentFrameView())
    subtree_throttled_ = ParentFrameView()->CanThrottleRendering();
}

void FrameView::RemoveChild(FrameOrPlugin* child) {
  DCHECK(child->Parent() == this);

  if (child->IsFrameView()) {
    if (!RuntimeEnabledFeatures::rootLayerScrollingEnabled())
      RemoveScrollableArea(ToFrameView(child));
  } else if (child->IsPluginView()) {
    PluginView* plugin = ToPluginView(child);
    DCHECK(plugins_.Contains(plugin));
    plugins_.erase(plugin);
  }

  child->SetParent(nullptr);
}

void FrameView::AddPlugin(PluginView* plugin) {
  DCHECK(!plugins_.Contains(plugin));
  plugins_.insert(plugin);
}

void FrameView::RemoveScrollbar(Scrollbar* scrollbar) {
  DCHECK(scrollbars_.Contains(scrollbar));
  scrollbars_.erase(scrollbar);
}

void FrameView::AddScrollbar(Scrollbar* scrollbar) {
  DCHECK(!scrollbars_.Contains(scrollbar));
  scrollbars_.insert(scrollbar);
}

bool FrameView::VisualViewportSuppliesScrollbars() {
  // On desktop, we always use the layout viewport's scrollbars.
  if (!frame_->GetSettings() || !frame_->GetSettings()->GetViewportEnabled() ||
      !frame_->GetDocument() || !frame_->GetPage())
    return false;

  const TopDocumentRootScrollerController& controller =
      frame_->GetPage()->GlobalRootScrollerController();

  if (!LayoutViewportScrollableArea())
    return false;

  return RootScrollerUtil::ScrollableAreaForRootScroller(
             controller.GlobalRootScroller()) == LayoutViewportScrollableArea();
}

AXObjectCache* FrameView::AxObjectCache() const {
  if (GetFrame().GetDocument())
    return GetFrame().GetDocument()->ExistingAXObjectCache();
  return nullptr;
}

void FrameView::SetCursor(const Cursor& cursor) {
  Page* page = GetFrame().GetPage();
  if (!page || frame_->GetEventHandler().IsMousePositionUnknown())
    return;
  page->GetChromeClient().SetCursor(cursor, frame_);
}

void FrameView::FrameRectsChanged() {
  TRACE_EVENT0("blink", "FrameView::frameRectsChanged");
  if (LayoutSizeFixedToFrameSize())
    SetLayoutSizeInternal(FrameRect().Size());

  ForAllChildViewsAndPlugins([](FrameOrPlugin& frame_or_plugin) {
    frame_or_plugin.FrameRectsChanged();
  });
}

void FrameView::SetLayoutSizeInternal(const IntSize& size) {
  if (layout_size_ == size)
    return;

  layout_size_ = size;
  ContentsResized();
}

void FrameView::DidAddScrollbar(Scrollbar& scrollbar,
                                ScrollbarOrientation orientation) {
  ScrollableArea::DidAddScrollbar(scrollbar, orientation);
}

PaintLayer* FrameView::Layer() const {
  LayoutViewItem layout_view = GetLayoutViewItem();
  if (layout_view.IsNull() || !layout_view.Compositor())
    return nullptr;

  return layout_view.Compositor()->RootLayer();
}

IntSize FrameView::MaximumScrollOffsetInt() const {
  // Make the same calculation as in CC's LayerImpl::MaxScrollOffset()
  // FIXME: We probably shouldn't be storing the bounds in a float.
  // crbug.com/422331.
  IntSize visible_size = VisibleContentSize(kExcludeScrollbars);
  IntSize content_bounds = ContentsSize();

  Page* page = frame_->GetPage();
  DCHECK(page);

  // We need to perform this const_cast since maximumScrollOffsetInt is a const
  // method but we can't make layoutViewportScrollableArea const since it can
  // return |this|. Once root-layer-scrolls ships layoutViewportScrollableArea
  // can be made const.
  const ScrollableArea* layout_viewport =
      const_cast<FrameView*>(this)->LayoutViewportScrollableArea();
  TopDocumentRootScrollerController& controller =
      page->GlobalRootScrollerController();
  if (layout_viewport == controller.RootScrollerArea())
    visible_size = controller.RootScrollerVisibleArea();

  IntSize maximum_offset =
      ToIntSize(-ScrollOrigin() + (content_bounds - visible_size));
  return maximum_offset.ExpandedTo(MinimumScrollOffsetInt());
}

void FrameView::SetScrollbarModes(ScrollbarMode horizontal_mode,
                                  ScrollbarMode vertical_mode,
                                  bool horizontal_lock,
                                  bool vertical_lock) {
  bool needs_update = false;

  // If the page's overflow setting has disabled scrolling, do not allow
  // anything to override that setting, http://crbug.com/426447
  LayoutObject* viewport = ViewportLayoutObject();
  if (viewport && !ShouldIgnoreOverflowHidden()) {
    if (viewport->Style()->OverflowX() == EOverflow::kHidden)
      horizontal_mode = kScrollbarAlwaysOff;
    if (viewport->Style()->OverflowY() == EOverflow::kHidden)
      vertical_mode = kScrollbarAlwaysOff;
  }

  if (horizontal_mode != HorizontalScrollbarMode() &&
      !horizontal_scrollbar_lock_) {
    horizontal_scrollbar_mode_ = horizontal_mode;
    needs_update = true;
  }

  if (vertical_mode != VerticalScrollbarMode() && !vertical_scrollbar_lock_) {
    vertical_scrollbar_mode_ = vertical_mode;
    needs_update = true;
  }

  if (horizontal_lock)
    SetHorizontalScrollbarLock();

  if (vertical_lock)
    SetVerticalScrollbarLock();

  if (!needs_update)
    return;

  UpdateScrollbars();

  if (!LayerForScrolling())
    return;
  WebLayer* layer = LayerForScrolling()->PlatformLayer();
  if (!layer)
    return;
  layer->SetUserScrollable(UserInputScrollable(kHorizontalScrollbar),
                           UserInputScrollable(kVerticalScrollbar));
}

IntSize FrameView::VisibleContentSize(
    IncludeScrollbarsInRect scrollbar_inclusion) const {
  return scrollbar_inclusion == kExcludeScrollbars
             ? ExcludeScrollbars(FrameRect().Size())
             : FrameRect().Size();
}

IntRect FrameView::VisibleContentRect(
    IncludeScrollbarsInRect scrollbar_inclusion) const {
  return IntRect(IntPoint(FlooredIntSize(scroll_offset_)),
                 VisibleContentSize(scrollbar_inclusion));
}

IntSize FrameView::ContentsSize() const {
  return contents_size_;
}

void FrameView::ClipPaintRect(FloatRect* paint_rect) const {
  // Paint the whole rect if "mainFrameClipsContent" is false, meaning that
  // WebPreferences::record_whole_document is true.
  if (!frame_->GetSettings()->GetMainFrameClipsContent())
    return;

  paint_rect->Intersect(
      GetPage()->GetChromeClient().VisibleContentRectForPainting().value_or(
          VisibleContentRect()));
}

IntSize FrameView::MinimumScrollOffsetInt() const {
  return IntSize(-ScrollOrigin().X(), -ScrollOrigin().Y());
}

void FrameView::AdjustScrollbarOpacity() {
  if (HorizontalScrollbar() && LayerForHorizontalScrollbar()) {
    bool is_opaque_scrollbar = !HorizontalScrollbar()->IsOverlayScrollbar();
    LayerForHorizontalScrollbar()->SetContentsOpaque(is_opaque_scrollbar);
  }
  if (VerticalScrollbar() && LayerForVerticalScrollbar()) {
    bool is_opaque_scrollbar = !VerticalScrollbar()->IsOverlayScrollbar();
    LayerForVerticalScrollbar()->SetContentsOpaque(is_opaque_scrollbar);
  }
}

int FrameView::ScrollSize(ScrollbarOrientation orientation) const {
  Scrollbar* scrollbar =
      ((orientation == kHorizontalScrollbar) ? HorizontalScrollbar()
                                             : VerticalScrollbar());

  // If no scrollbars are present, the content may still be scrollable.
  if (!scrollbar) {
    IntSize scroll_size = contents_size_ - VisibleContentRect().Size();
    scroll_size.ClampNegativeToZero();
    return orientation == kHorizontalScrollbar ? scroll_size.Width()
                                               : scroll_size.Height();
  }

  return scrollbar->TotalSize() - scrollbar->VisibleSize();
}

void FrameView::UpdateScrollOffset(const ScrollOffset& offset,
                                   ScrollType scroll_type) {
  ScrollOffset scroll_delta = offset - scroll_offset_;
  if (scroll_delta.IsZero())
    return;

  if (RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
    // Don't scroll the FrameView!
    NOTREACHED();
  }

  scroll_offset_ = offset;

  if (!ScrollbarsSuppressed())
    pending_scroll_delta_ += scroll_delta;

  UpdateLayersAndCompositingAfterScrollIfNeeded();

  Document* document = frame_->GetDocument();
  document->EnqueueScrollEventForNode(document);

  frame_->GetEventHandler().DispatchFakeMouseMoveEventSoon();
  if (scroll_type == kUserScroll || scroll_type == kCompositorScroll) {
    Page* page = GetFrame().GetPage();
    if (page)
      page->GetChromeClient().ClearToolTip(*frame_);
  }

  LayoutViewItem layout_view_item = document->GetLayoutViewItem();
  if (!layout_view_item.IsNull()) {
    if (layout_view_item.UsesCompositing())
      layout_view_item.Compositor()->FrameViewDidScroll();
    layout_view_item.ClearHitTestCache();
  }

  did_scroll_timer_.StartOneShot(kResourcePriorityUpdateDelayAfterScroll,
                                 BLINK_FROM_HERE);

  if (AXObjectCache* cache = frame_->GetDocument()->ExistingAXObjectCache())
    cache->HandleScrollPositionChanged(this);

  GetFrame().Loader().SaveScrollState();
  DidChangeScrollOffset();

  if (scroll_type == kCompositorScroll && frame_->IsMainFrame()) {
    if (DocumentLoader* document_loader = frame_->Loader().GetDocumentLoader())
      document_loader->GetInitialScrollState().was_scrolled_by_user = true;
  }

  if (IsExplicitScrollType(scroll_type)) {
    if (scroll_type != kCompositorScroll)
      ShowOverlayScrollbars();
    ClearFragmentAnchor();
    ClearScrollAnchor();
  }
}

void FrameView::DidChangeScrollOffset() {
  GetFrame().Loader().Client()->DidChangeScrollOffset();
  if (GetFrame().IsMainFrame())
    GetFrame().GetPage()->GetChromeClient().MainFrameScrollOffsetChanged();
}

void FrameView::ClearScrollAnchor() {
  if (!RuntimeEnabledFeatures::scrollAnchoringEnabled())
    return;
  scroll_anchor_.Clear();
}

bool FrameView::HasOverlayScrollbars() const {
  return (HorizontalScrollbar() &&
          HorizontalScrollbar()->IsOverlayScrollbar()) ||
         (VerticalScrollbar() && VerticalScrollbar()->IsOverlayScrollbar());
}

void FrameView::ComputeScrollbarExistence(
    bool& new_has_horizontal_scrollbar,
    bool& new_has_vertical_scrollbar,
    const IntSize& doc_size,
    ComputeScrollbarExistenceOption option) {
  if ((frame_->GetSettings() && frame_->GetSettings()->GetHideScrollbars()) ||
      VisualViewportSuppliesScrollbars()) {
    new_has_horizontal_scrollbar = false;
    new_has_vertical_scrollbar = false;
    return;
  }

  bool has_horizontal_scrollbar = HorizontalScrollbar();
  bool has_vertical_scrollbar = VerticalScrollbar();

  new_has_horizontal_scrollbar = has_horizontal_scrollbar;
  new_has_vertical_scrollbar = has_vertical_scrollbar;

  if (RuntimeEnabledFeatures::rootLayerScrollingEnabled())
    return;

  ScrollbarMode h_scroll = horizontal_scrollbar_mode_;
  ScrollbarMode v_scroll = vertical_scrollbar_mode_;

  if (h_scroll != kScrollbarAuto)
    new_has_horizontal_scrollbar = (h_scroll == kScrollbarAlwaysOn);
  if (v_scroll != kScrollbarAuto)
    new_has_vertical_scrollbar = (v_scroll == kScrollbarAlwaysOn);

  if (scrollbars_suppressed_ ||
      (h_scroll != kScrollbarAuto && v_scroll != kScrollbarAuto))
    return;

  if (h_scroll == kScrollbarAuto)
    new_has_horizontal_scrollbar = doc_size.Width() > VisibleWidth();
  if (v_scroll == kScrollbarAuto)
    new_has_vertical_scrollbar = doc_size.Height() > VisibleHeight();

  if (HasOverlayScrollbars())
    return;

  IntSize full_visible_size = VisibleContentRect(kIncludeScrollbars).Size();

  bool attempt_to_remove_scrollbars =
      (option == kFirstPass && doc_size.Width() <= full_visible_size.Width() &&
       doc_size.Height() <= full_visible_size.Height());
  if (attempt_to_remove_scrollbars) {
    if (h_scroll == kScrollbarAuto)
      new_has_horizontal_scrollbar = false;
    if (v_scroll == kScrollbarAuto)
      new_has_vertical_scrollbar = false;
  }
}

void FrameView::UpdateScrollbarEnabledState() {
  bool force_disabled =
      ScrollbarTheme::GetTheme().ShouldDisableInvisibleScrollbars() &&
      ScrollbarsHidden();

  if (HorizontalScrollbar()) {
    HorizontalScrollbar()->SetEnabled(ContentsWidth() > VisibleWidth() &&
                                      !force_disabled);
  }
  if (VerticalScrollbar()) {
    VerticalScrollbar()->SetEnabled(ContentsHeight() > VisibleHeight() &&
                                    !force_disabled);
  }
}

void FrameView::UpdateScrollbarGeometry() {
  UpdateScrollbarEnabledState();
  if (HorizontalScrollbar()) {
    int thickness = HorizontalScrollbar()->ScrollbarThickness();
    IntRect old_rect(HorizontalScrollbar()->FrameRect());
    IntRect h_bar_rect(
        (ShouldPlaceVerticalScrollbarOnLeft() && VerticalScrollbar())
            ? VerticalScrollbar()->Width()
            : 0,
        Height() - thickness,
        Width() - (VerticalScrollbar() ? VerticalScrollbar()->Width() : 0),
        thickness);
    HorizontalScrollbar()->SetFrameRect(h_bar_rect);
    if (old_rect != HorizontalScrollbar()->FrameRect())
      SetScrollbarNeedsPaintInvalidation(kHorizontalScrollbar);

    HorizontalScrollbar()->SetProportion(VisibleWidth(), ContentsWidth());
    HorizontalScrollbar()->OffsetDidChange();
  }

  if (VerticalScrollbar()) {
    int thickness = VerticalScrollbar()->ScrollbarThickness();
    IntRect old_rect(VerticalScrollbar()->FrameRect());
    IntRect v_bar_rect(
        ShouldPlaceVerticalScrollbarOnLeft() ? 0 : (Width() - thickness), 0,
        thickness,
        Height() -
            (HorizontalScrollbar() ? HorizontalScrollbar()->Height() : 0));
    VerticalScrollbar()->SetFrameRect(v_bar_rect);
    if (old_rect != VerticalScrollbar()->FrameRect())
      SetScrollbarNeedsPaintInvalidation(kVerticalScrollbar);

    VerticalScrollbar()->SetProportion(VisibleHeight(), ContentsHeight());
    VerticalScrollbar()->OffsetDidChange();
  }
}

bool FrameView::AdjustScrollbarExistence(
    ComputeScrollbarExistenceOption option) {
  DCHECK(in_update_scrollbars_);

  // If we came in here with the view already needing a layout, then go ahead
  // and do that first.  (This will be the common case, e.g., when the page
  // changes due to window resizing for example).  This layout will not re-enter
  // updateScrollbars and does not count towards our max layout pass total.
  if (!scrollbars_suppressed_)
    ScrollbarExistenceDidChange();

  bool has_horizontal_scrollbar = HorizontalScrollbar();
  bool has_vertical_scrollbar = VerticalScrollbar();

  bool new_has_horizontal_scrollbar = false;
  bool new_has_vertical_scrollbar = false;
  ComputeScrollbarExistence(new_has_horizontal_scrollbar,
                            new_has_vertical_scrollbar, ContentsSize(), option);

  bool scrollbar_existence_changed =
      has_horizontal_scrollbar != new_has_horizontal_scrollbar ||
      has_vertical_scrollbar != new_has_vertical_scrollbar;
  if (!scrollbar_existence_changed)
    return false;

  scrollbar_manager_.SetHasHorizontalScrollbar(new_has_horizontal_scrollbar);
  scrollbar_manager_.SetHasVerticalScrollbar(new_has_vertical_scrollbar);

  if (scrollbars_suppressed_)
    return true;

  if (!HasOverlayScrollbars())
    SetNeedsLayout();
  ScrollbarExistenceDidChange();
  return true;
}

bool FrameView::NeedsScrollbarReconstruction() const {
  Scrollbar* scrollbar = HorizontalScrollbar();
  if (!scrollbar)
    scrollbar = VerticalScrollbar();
  if (!scrollbar) {
    // We have no scrollbar to reconstruct.
    return false;
  }
  Element* style_source = nullptr;
  bool needs_custom = ShouldUseCustomScrollbars(style_source);
  bool is_custom = scrollbar->IsCustomScrollbar();
  if (needs_custom != is_custom) {
    // We have a native scrollbar that should be custom, or vice versa.
    return true;
  }
  if (!needs_custom) {
    // We have a native scrollbar that should remain native.
    return false;
  }
  DCHECK(needs_custom && is_custom);
  DCHECK(style_source);
  if (ToLayoutScrollbar(scrollbar)->StyleSource() !=
      style_source->GetLayoutObject()) {
    // We have a custom scrollbar with a stale m_owner.
    return true;
  }
  return false;
}

bool FrameView::ShouldIgnoreOverflowHidden() const {
  return frame_->GetSettings()->GetIgnoreMainFrameOverflowHiddenQuirk() &&
         frame_->IsMainFrame();
}

void FrameView::UpdateScrollbarsIfNeeded() {
  if (needs_scrollbars_update_ || NeedsScrollbarReconstruction() ||
      ScrollOriginChanged())
    UpdateScrollbars();
}

void FrameView::UpdateScrollbars() {
  needs_scrollbars_update_ = false;

  if (RuntimeEnabledFeatures::rootLayerScrollingEnabled())
    return;

  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled())
    SetNeedsPaintPropertyUpdate();

  // Avoid drawing two sets of scrollbars when visual viewport is enabled.
  if (VisualViewportSuppliesScrollbars()) {
    scrollbar_manager_.SetHasHorizontalScrollbar(false);
    scrollbar_manager_.SetHasVerticalScrollbar(false);
    AdjustScrollOffsetFromUpdateScrollbars();
    return;
  }

  if (in_update_scrollbars_)
    return;
  InUpdateScrollbarsScope in_update_scrollbars_scope(this);

  bool scrollbar_existence_changed = false;

  if (NeedsScrollbarReconstruction()) {
    scrollbar_manager_.SetHasHorizontalScrollbar(false);
    scrollbar_manager_.SetHasVerticalScrollbar(false);
    scrollbar_existence_changed = true;
  }

  int max_update_scrollbars_pass =
      HasOverlayScrollbars() || scrollbars_suppressed_ ? 1 : 3;
  for (int update_scrollbars_pass = 0;
       update_scrollbars_pass < max_update_scrollbars_pass;
       update_scrollbars_pass++) {
    if (!AdjustScrollbarExistence(update_scrollbars_pass ? kIncremental
                                                         : kFirstPass))
      break;
    scrollbar_existence_changed = true;
  }

  UpdateScrollbarGeometry();

  if (scrollbar_existence_changed) {
    // FIXME: Is frameRectsChanged really necessary here? Have any frame rects
    // changed?
    FrameRectsChanged();
    PositionScrollbarLayers();
    UpdateScrollCorner();
  }

  AdjustScrollOffsetFromUpdateScrollbars();
}

void FrameView::AdjustScrollOffsetFromUpdateScrollbars() {
  ScrollOffset clamped = ClampScrollOffset(GetScrollOffset());
  if (clamped != GetScrollOffset() || ScrollOriginChanged())
    SetScrollOffset(clamped, kClampingScroll);
}

void FrameView::ScrollContentsIfNeeded() {
  if (pending_scroll_delta_.IsZero())
    return;
  ScrollOffset scroll_delta = pending_scroll_delta_;
  pending_scroll_delta_ = ScrollOffset();
  // FIXME: Change scrollContents() to take DoubleSize. crbug.com/414283.
  ScrollContents(FlooredIntSize(scroll_delta));
}

void FrameView::ScrollContents(const IntSize& scroll_delta) {
  PlatformChromeClient* client = GetChromeClient();
  if (!client)
    return;

  TRACE_EVENT0("blink", "FrameView::scrollContents");

  if (!ScrollContentsFastPath(-scroll_delta))
    ScrollContentsSlowPath();

  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled() &&
      !RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
    // Need to update scroll translation property.
    SetNeedsPaintPropertyUpdate();
  }

  // This call will move children with native FrameViews (plugins) and
  // invalidate them as well.
  FrameRectsChanged();
}

IntPoint FrameView::ContentsToFrame(
    const IntPoint& point_in_content_space) const {
  return point_in_content_space - ScrollOffsetInt();
}

IntRect FrameView::ContentsToFrame(const IntRect& rect_in_content_space) const {
  return IntRect(ContentsToFrame(rect_in_content_space.Location()),
                 rect_in_content_space.Size());
}

FloatPoint FrameView::FrameToContents(const FloatPoint& point_in_frame) const {
  return point_in_frame + GetScrollOffset();
}

IntPoint FrameView::FrameToContents(const IntPoint& point_in_frame) const {
  return point_in_frame + ScrollOffsetInt();
}

IntRect FrameView::FrameToContents(const IntRect& rect_in_frame) const {
  return IntRect(FrameToContents(rect_in_frame.Location()),
                 rect_in_frame.Size());
}

IntPoint FrameView::RootFrameToContents(
    const IntPoint& root_frame_point) const {
  IntPoint frame_point = ConvertFromRootFrame(root_frame_point);
  return FrameToContents(frame_point);
}

IntRect FrameView::RootFrameToContents(const IntRect& root_frame_rect) const {
  return IntRect(RootFrameToContents(root_frame_rect.Location()),
                 root_frame_rect.Size());
}

IntPoint FrameView::ContentsToRootFrame(const IntPoint& contents_point) const {
  IntPoint frame_point = ContentsToFrame(contents_point);
  return ConvertToRootFrame(frame_point);
}

IntRect FrameView::ContentsToRootFrame(const IntRect& contents_rect) const {
  IntRect rect_in_frame = ContentsToFrame(contents_rect);
  return ConvertToRootFrame(rect_in_frame);
}

FloatPoint FrameView::RootFrameToContents(
    const FloatPoint& point_in_root_frame) const {
  FloatPoint frame_point = ConvertFromRootFrame(point_in_root_frame);
  return FrameToContents(frame_point);
}

IntRect FrameView::ViewportToContents(const IntRect& rect_in_viewport) const {
  IntRect rect_in_root_frame =
      frame_->GetPage()->GetVisualViewport().ViewportToRootFrame(
          rect_in_viewport);
  IntRect frame_rect = ConvertFromRootFrame(rect_in_root_frame);
  return FrameToContents(frame_rect);
}

IntPoint FrameView::ViewportToContents(
    const IntPoint& point_in_viewport) const {
  IntPoint point_in_root_frame =
      frame_->GetPage()->GetVisualViewport().ViewportToRootFrame(
          point_in_viewport);
  IntPoint point_in_frame = ConvertFromRootFrame(point_in_root_frame);
  return FrameToContents(point_in_frame);
}

IntRect FrameView::ContentsToViewport(const IntRect& rect_in_contents) const {
  IntRect rect_in_frame = ContentsToFrame(rect_in_contents);
  IntRect rect_in_root_frame = ConvertToRootFrame(rect_in_frame);
  return frame_->GetPage()->GetVisualViewport().RootFrameToViewport(
      rect_in_root_frame);
}

IntPoint FrameView::ContentsToViewport(
    const IntPoint& point_in_contents) const {
  IntPoint point_in_frame = ContentsToFrame(point_in_contents);
  IntPoint point_in_root_frame = ConvertToRootFrame(point_in_frame);
  return frame_->GetPage()->GetVisualViewport().RootFrameToViewport(
      point_in_root_frame);
}

IntRect FrameView::ContentsToScreen(const IntRect& rect) const {
  PlatformChromeClient* client = GetChromeClient();
  if (!client)
    return IntRect();
  return client->ViewportToScreen(ContentsToViewport(rect), this);
}

IntPoint FrameView::SoonToBeRemovedUnscaledViewportToContents(
    const IntPoint& point_in_viewport) const {
  IntPoint point_in_root_frame = FlooredIntPoint(
      frame_->GetPage()->GetVisualViewport().ViewportCSSPixelsToRootFrame(
          point_in_viewport));
  IntPoint point_in_this_frame = ConvertFromRootFrame(point_in_root_frame);
  return FrameToContents(point_in_this_frame);
}

Scrollbar* FrameView::ScrollbarAtFramePoint(const IntPoint& point_in_frame) {
  if (HorizontalScrollbar() &&
      HorizontalScrollbar()->ShouldParticipateInHitTesting() &&
      HorizontalScrollbar()->FrameRect().Contains(point_in_frame))
    return HorizontalScrollbar();
  if (VerticalScrollbar() &&
      VerticalScrollbar()->ShouldParticipateInHitTesting() &&
      VerticalScrollbar()->FrameRect().Contains(point_in_frame))
    return VerticalScrollbar();
  return nullptr;
}

static void PositionScrollbarLayer(GraphicsLayer* graphics_layer,
                                   Scrollbar* scrollbar) {
  if (!graphics_layer || !scrollbar)
    return;

  IntRect scrollbar_rect = scrollbar->FrameRect();
  graphics_layer->SetPosition(scrollbar_rect.Location());

  if (scrollbar_rect.Size() == graphics_layer->Size())
    return;

  graphics_layer->SetSize(FloatSize(scrollbar_rect.Size()));

  if (graphics_layer->HasContentsLayer()) {
    graphics_layer->SetContentsRect(
        IntRect(0, 0, scrollbar_rect.Width(), scrollbar_rect.Height()));
    return;
  }

  graphics_layer->SetDrawsContent(true);
  graphics_layer->SetNeedsDisplay();
}

static void PositionScrollCornerLayer(GraphicsLayer* graphics_layer,
                                      const IntRect& corner_rect) {
  if (!graphics_layer)
    return;
  graphics_layer->SetDrawsContent(!corner_rect.IsEmpty());
  graphics_layer->SetPosition(corner_rect.Location());
  if (corner_rect.Size() != graphics_layer->Size())
    graphics_layer->SetNeedsDisplay();
  graphics_layer->SetSize(FloatSize(corner_rect.Size()));
}

void FrameView::PositionScrollbarLayers() {
  PositionScrollbarLayer(LayerForHorizontalScrollbar(), HorizontalScrollbar());
  PositionScrollbarLayer(LayerForVerticalScrollbar(), VerticalScrollbar());
  PositionScrollCornerLayer(LayerForScrollCorner(), ScrollCornerRect());
}

bool FrameView::UpdateAfterCompositingChange() {
  if (ScrollOriginChanged()) {
    // If the scroll origin changed, we need to update the layer position on
    // the compositor since the offset itself might not have changed.
    LayoutViewItem layout_view_item = this->GetLayoutViewItem();
    if (!layout_view_item.IsNull() && layout_view_item.UsesCompositing())
      layout_view_item.Compositor()->FrameViewDidScroll();
    ResetScrollOriginChanged();
  }
  return false;
}

bool FrameView::UserInputScrollable(ScrollbarOrientation orientation) const {
  Document* document = GetFrame().GetDocument();
  Element* fullscreen_element = Fullscreen::FullscreenElementFrom(*document);
  if (fullscreen_element && fullscreen_element != document->documentElement())
    return false;

  if (RuntimeEnabledFeatures::rootLayerScrollingEnabled())
    return false;

  ScrollbarMode mode = (orientation == kHorizontalScrollbar)
                           ? horizontal_scrollbar_mode_
                           : vertical_scrollbar_mode_;

  return mode == kScrollbarAuto || mode == kScrollbarAlwaysOn;
}

bool FrameView::ShouldPlaceVerticalScrollbarOnLeft() const {
  return false;
}

LayoutRect FrameView::ScrollIntoView(const LayoutRect& rect_in_content,
                                     const ScrollAlignment& align_x,
                                     const ScrollAlignment& align_y,
                                     ScrollType scroll_type) {
  LayoutRect view_rect(VisibleContentRect());
  LayoutRect expose_rect = ScrollAlignment::GetRectToExpose(
      view_rect, rect_in_content, align_x, align_y);
  if (expose_rect != view_rect) {
    SetScrollOffset(
        ScrollOffset(expose_rect.X().ToFloat(), expose_rect.Y().ToFloat()),
        scroll_type);
  }

  // Scrolling the FrameView cannot change the input rect's location relative to
  // the document.
  return rect_in_content;
}

IntRect FrameView::ScrollCornerRect() const {
  IntRect corner_rect;

  if (HasOverlayScrollbars())
    return corner_rect;

  if (HorizontalScrollbar() && Width() - HorizontalScrollbar()->Width() > 0) {
    corner_rect.Unite(IntRect(ShouldPlaceVerticalScrollbarOnLeft()
                                  ? 0
                                  : HorizontalScrollbar()->Width(),
                              Height() - HorizontalScrollbar()->Height(),
                              Width() - HorizontalScrollbar()->Width(),
                              HorizontalScrollbar()->Height()));
  }

  if (VerticalScrollbar() && Height() - VerticalScrollbar()->Height() > 0) {
    corner_rect.Unite(IntRect(ShouldPlaceVerticalScrollbarOnLeft()
                                  ? 0
                                  : (Width() - VerticalScrollbar()->Width()),
                              VerticalScrollbar()->Height(),
                              VerticalScrollbar()->Width(),
                              Height() - VerticalScrollbar()->Height()));
  }

  return corner_rect;
}

bool FrameView::IsScrollCornerVisible() const {
  return !ScrollCornerRect().IsEmpty();
}

ScrollBehavior FrameView::ScrollBehaviorStyle() const {
  Element* scroll_element = frame_->GetDocument()->scrollingElement();
  LayoutObject* layout_object =
      scroll_element ? scroll_element->GetLayoutObject() : nullptr;
  if (layout_object &&
      layout_object->Style()->GetScrollBehavior() == kScrollBehaviorSmooth)
    return kScrollBehaviorSmooth;

  return kScrollBehaviorInstant;
}

void FrameView::Paint(GraphicsContext& context,
                      const CullRect& cull_rect) const {
  Paint(context, kGlobalPaintNormalPhase, cull_rect);
}

void FrameView::Paint(GraphicsContext& context,
                      const GlobalPaintFlags global_paint_flags,
                      const CullRect& cull_rect) const {
  FramePainter(*this).Paint(context, global_paint_flags, cull_rect);
}

void FrameView::PaintContents(GraphicsContext& context,
                              const GlobalPaintFlags global_paint_flags,
                              const IntRect& damage_rect) const {
  FramePainter(*this).PaintContents(context, global_paint_flags, damage_rect);
}

bool FrameView::IsPointInScrollbarCorner(const IntPoint& point_in_root_frame) {
  if (!ScrollbarCornerPresent())
    return false;

  IntPoint frame_point = ConvertFromRootFrame(point_in_root_frame);

  if (HorizontalScrollbar()) {
    int horizontal_scrollbar_y_min = HorizontalScrollbar()->FrameRect().Y();
    int horizontal_scrollbar_y_max =
        HorizontalScrollbar()->FrameRect().Y() +
        HorizontalScrollbar()->FrameRect().Height();
    int horizontal_scrollbar_x_min = HorizontalScrollbar()->FrameRect().X() +
                                     HorizontalScrollbar()->FrameRect().Width();

    return frame_point.Y() > horizontal_scrollbar_y_min &&
           frame_point.Y() < horizontal_scrollbar_y_max &&
           frame_point.X() > horizontal_scrollbar_x_min;
  }

  int vertical_scrollbar_x_min = VerticalScrollbar()->FrameRect().X();
  int vertical_scrollbar_x_max = VerticalScrollbar()->FrameRect().X() +
                                 VerticalScrollbar()->FrameRect().Width();
  int vertical_scrollbar_y_min = VerticalScrollbar()->FrameRect().Y() +
                                 VerticalScrollbar()->FrameRect().Height();

  return frame_point.X() > vertical_scrollbar_x_min &&
         frame_point.X() < vertical_scrollbar_x_max &&
         frame_point.Y() > vertical_scrollbar_y_min;
}

bool FrameView::ScrollbarCornerPresent() const {
  return (HorizontalScrollbar() &&
          Width() - HorizontalScrollbar()->Width() > 0) ||
         (VerticalScrollbar() && Height() - VerticalScrollbar()->Height() > 0);
}

IntRect FrameView::ConvertToRootFrame(const IntRect& local_rect) const {
  if (parent_) {
    IntRect parent_rect = ConvertToContainingFrameViewBase(local_rect);
    return parent_->ConvertToRootFrame(parent_rect);
  }
  return local_rect;
}

IntPoint FrameView::ConvertToRootFrame(const IntPoint& local_point) const {
  if (parent_) {
    IntPoint parent_point = ConvertToContainingFrameViewBase(local_point);
    return parent_->ConvertToRootFrame(parent_point);
  }
  return local_point;
}

IntRect FrameView::ConvertFromRootFrame(
    const IntRect& rect_in_root_frame) const {
  if (parent_) {
    IntRect parent_rect = parent_->ConvertFromRootFrame(rect_in_root_frame);
    return ConvertFromContainingFrameViewBase(parent_rect);
  }
  return rect_in_root_frame;
}

IntPoint FrameView::ConvertFromRootFrame(
    const IntPoint& point_in_root_frame) const {
  if (parent_) {
    IntPoint parent_point = parent_->ConvertFromRootFrame(point_in_root_frame);
    return ConvertFromContainingFrameViewBase(parent_point);
  }
  return point_in_root_frame;
}

FloatPoint FrameView::ConvertFromRootFrame(
    const FloatPoint& point_in_root_frame) const {
  // FrameViews / windows are required to be IntPoint aligned, but we may
  // need to convert FloatPoint values within them (eg. for event
  // co-ordinates).
  IntPoint floored_point = FlooredIntPoint(point_in_root_frame);
  FloatPoint parent_point = ConvertFromRootFrame(floored_point);
  FloatSize window_fraction = point_in_root_frame - floored_point;
  // Use linear interpolation handle any fractional value (eg. for iframes
  // subject to a transform beyond just a simple translation).
  // FIXME: Add FloatPoint variants of all co-ordinate space conversion APIs.
  if (!window_fraction.IsEmpty()) {
    const int kFactor = 1000;
    IntPoint parent_line_end = ConvertFromRootFrame(
        floored_point + RoundedIntSize(window_fraction.ScaledBy(kFactor)));
    FloatSize parent_fraction =
        (parent_line_end - parent_point).ScaledBy(1.0f / kFactor);
    parent_point.Move(parent_fraction);
  }
  return parent_point;
}

IntPoint FrameView::ConvertFromContainingFrameViewBaseToScrollbar(
    const Scrollbar& scrollbar,
    const IntPoint& parent_point) const {
  IntPoint new_point = parent_point;
  // Scrollbars won't be transformed within us
  new_point.MoveBy(-scrollbar.Location());
  return new_point;
}

void FrameView::SetParentVisible(bool visible) {
  if (IsParentVisible() == visible)
    return;

  // As parent visibility changes, we may need to recomposite this frame view
  // and potentially child frame views.
  SetNeedsCompositingUpdate(GetLayoutViewItem(), kCompositingUpdateRebuildTree);

  parent_visible_ = visible;

  if (!IsSelfVisible())
    return;

  ForAllChildViewsAndPlugins([visible](FrameOrPlugin& frame_or_plugin) {
    frame_or_plugin.SetParentVisible(visible);
  });
}

void FrameView::Show() {
  if (!IsSelfVisible()) {
    SetSelfVisible(true);
    if (ScrollingCoordinator* scrolling_coordinator =
            this->GetScrollingCoordinator())
      scrolling_coordinator->FrameViewVisibilityDidChange();
    SetNeedsCompositingUpdate(GetLayoutViewItem(),
                              kCompositingUpdateRebuildTree);
    UpdateParentScrollableAreaSet();
    if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled() &&
        !RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
      // The existance of scrolling properties depends on visibility through
      // isScrollable() so ensure properties are updated if visibility changes.
      SetNeedsPaintPropertyUpdate();
    }
    if (IsParentVisible()) {
      ForAllChildViewsAndPlugins([](FrameOrPlugin& frame_or_plugin) {
        frame_or_plugin.SetParentVisible(true);
      });
    }
  }
}

void FrameView::Hide() {
  if (IsSelfVisible()) {
    if (IsParentVisible()) {
      ForAllChildViewsAndPlugins([](FrameOrPlugin& frame_or_plugin) {
        frame_or_plugin.SetParentVisible(false);
      });
    }
    SetSelfVisible(false);
    if (ScrollingCoordinator* scrolling_coordinator =
            this->GetScrollingCoordinator())
      scrolling_coordinator->FrameViewVisibilityDidChange();
    SetNeedsCompositingUpdate(GetLayoutViewItem(),
                              kCompositingUpdateRebuildTree);
    UpdateParentScrollableAreaSet();
    if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled() &&
        !RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
      // The existance of scrolling properties depends on visibility through
      // isScrollable() so ensure properties are updated if visibility changes.
      SetNeedsPaintPropertyUpdate();
    }
  }
}

int FrameView::ViewportWidth() const {
  int viewport_width = GetLayoutSize(kIncludeScrollbars).Width();
  return AdjustForAbsoluteZoom(viewport_width, GetLayoutView());
}

ScrollableArea* FrameView::GetScrollableArea() {
  if (viewport_scrollable_area_)
    return viewport_scrollable_area_.Get();

  return LayoutViewportScrollableArea();
}

ScrollableArea* FrameView::LayoutViewportScrollableArea() {
  if (!RuntimeEnabledFeatures::rootLayerScrollingEnabled())
    return this;

  LayoutViewItem layout_view_item = this->GetLayoutViewItem();
  return layout_view_item.IsNull() ? nullptr
                                   : layout_view_item.GetScrollableArea();
}

RootFrameViewport* FrameView::GetRootFrameViewport() {
  return viewport_scrollable_area_.Get();
}

LayoutObject* FrameView::ViewportLayoutObject() const {
  if (Document* document = GetFrame().GetDocument()) {
    if (Element* element = document->ViewportDefiningElement())
      return element->GetLayoutObject();
  }
  return nullptr;
}

void FrameView::CollectAnnotatedRegions(
    LayoutObject& layout_object,
    Vector<AnnotatedRegionValue>& regions) const {
  // LayoutTexts don't have their own style, they just use their parent's style,
  // so we don't want to include them.
  if (layout_object.IsText())
    return;

  layout_object.AddAnnotatedRegions(regions);
  for (LayoutObject* curr = layout_object.SlowFirstChild(); curr;
       curr = curr->NextSibling())
    CollectAnnotatedRegions(*curr, regions);
}

void FrameView::UpdateViewportIntersectionsForSubtree(
    DocumentLifecycle::LifecycleState target_state) {
  // TODO(dcheng): Since FrameView tree updates are deferred, FrameViews
  // might still be in the FrameView hierarchy even though the associated
  // Document is already detached. Investigate if this check and a similar check
  // in lifecycle updates are still needed when there are no more deferred
  // FrameView updates: https://crbug.com/561683
  if (!GetFrame().GetDocument()->IsActive())
    return;

  if (target_state == DocumentLifecycle::kPaintClean) {
    RecordDeferredLoadingStats();
    if (!NeedsLayout()) {
      // Notify javascript IntersectionObservers
      if (GetFrame().GetDocument()->GetIntersectionObserverController()) {
        GetFrame()
            .GetDocument()
            ->GetIntersectionObserverController()
            ->ComputeTrackedIntersectionObservations();
      }
    }
  }

  // Don't throttle display:none frames (see updateRenderThrottlingStatus).
  HTMLFrameOwnerElement* owner_element = frame_->DeprecatedLocalOwner();
  if (hidden_for_throttling_ && owner_element &&
      !owner_element->GetLayoutObject()) {
    // No need to notify children because descendants of display:none frames
    // should remain throttled.
    UpdateRenderThrottlingStatus(hidden_for_throttling_, subtree_throttled_,
                                 kDontForceThrottlingInvalidation,
                                 kDontNotifyChildren);
  }

  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (child->IsRemoteFrame()) {
      if (RemoteFrameView* view = ToRemoteFrame(child)->View())
        view->UpdateRemoteViewportIntersection();
      continue;
    }
    if (FrameView* view = ToLocalFrame(child)->View())
      view->UpdateViewportIntersectionsForSubtree(target_state);
  }
}

void FrameView::UpdateRenderThrottlingStatusForTesting() {
  visibility_observer_->DeliverObservationsForTesting();
}

void FrameView::CrossOriginStatusChanged() {
  // Cross-domain status is not stored as a dirty bit within FrameView,
  // so force-invalidate throttling status when it changes regardless of
  // previous or new value.
  UpdateRenderThrottlingStatus(hidden_for_throttling_, subtree_throttled_,
                               kForceThrottlingInvalidation);
}

void FrameView::UpdateRenderThrottlingStatus(
    bool hidden,
    bool subtree_throttled,
    ForceThrottlingInvalidationBehavior force_throttling_invalidation_behavior,
    NotifyChildrenBehavior notify_children_behavior) {
  TRACE_EVENT0("blink", "FrameView::updateRenderThrottlingStatus");
  DCHECK(!IsInPerformLayout());
  DCHECK(!frame_->GetDocument() || !frame_->GetDocument()->InStyleRecalc());
  bool was_throttled = CanThrottleRendering();

  // Note that we disallow throttling of 0x0 and display:none frames because
  // some sites use them to drive UI logic.
  HTMLFrameOwnerElement* owner_element = frame_->DeprecatedLocalOwner();
  hidden_for_throttling_ = hidden && !FrameRect().IsEmpty() &&
                           (owner_element && owner_element->GetLayoutObject());
  subtree_throttled_ = subtree_throttled;

  bool is_throttled = CanThrottleRendering();
  bool became_unthrottled = was_throttled && !is_throttled;

  // If this FrameView became unthrottled or throttled, we must make sure all
  // its children are notified synchronously. Otherwise we 1) might attempt to
  // paint one of the children with an out-of-date layout before
  // |updateRenderThrottlingStatus| has made it throttled or 2) fail to
  // unthrottle a child whose parent is unthrottled by a later notification.
  if (notify_children_behavior == kNotifyChildren &&
      (was_throttled != is_throttled ||
       force_throttling_invalidation_behavior ==
           kForceThrottlingInvalidation)) {
    ForAllChildFrameViews([is_throttled](FrameView& frame_view) {
      frame_view.UpdateRenderThrottlingStatus(frame_view.hidden_for_throttling_,
                                              is_throttled);
    });
  }

  ScrollingCoordinator* scrolling_coordinator = this->GetScrollingCoordinator();
  if (became_unthrottled ||
      force_throttling_invalidation_behavior == kForceThrottlingInvalidation) {
    // ScrollingCoordinator needs to update according to the new throttling
    // status.
    if (scrolling_coordinator)
      scrolling_coordinator->NotifyGeometryChanged();
    // Start ticking animation frames again if necessary.
    if (GetPage())
      GetPage()->Animator().ScheduleVisualUpdate(frame_.Get());
    // Force a full repaint of this frame to ensure we are not left with a
    // partially painted version of this frame's contents if we skipped
    // painting them while the frame was throttled.
    LayoutViewItem layout_view_item = this->GetLayoutViewItem();
    if (!layout_view_item.IsNull())
      layout_view_item.InvalidatePaintForViewAndCompositedLayers();
    // Also need to update all paint properties that might be skipped while
    // the frame was throttled.
    SetSubtreeNeedsPaintPropertyUpdate();
  }

  bool has_handlers =
      frame_->GetPage() &&
      frame_->GetPage()->GetEventHandlerRegistry().HasEventHandlers(
          EventHandlerRegistry::kTouchStartOrMoveEventBlocking);
  if (was_throttled != CanThrottleRendering() && scrolling_coordinator &&
      has_handlers)
    scrolling_coordinator->TouchEventTargetRectsDidChange();

  if (frame_->FrameScheduler()) {
    frame_->FrameScheduler()->SetFrameVisible(!hidden_for_throttling_);
    frame_->FrameScheduler()->SetCrossOrigin(frame_->IsCrossOriginSubframe());
  }

#if DCHECK_IS_ON()
  // Make sure we never have an unthrottled frame inside a throttled one.
  FrameView* parent = ParentFrameView();
  while (parent) {
    DCHECK(CanThrottleRendering() || !parent->CanThrottleRendering());
    parent = parent->ParentFrameView();
  }
#endif
}

void FrameView::RecordDeferredLoadingStats() {
  if (!GetFrame().GetDocument()->GetFrame() ||
      !GetFrame().IsCrossOriginSubframe())
    return;

  FrameView* parent = ParentFrameView();
  if (!parent) {
    HTMLFrameOwnerElement* element = GetFrame().DeprecatedLocalOwner();
    // We would fall into an else block on some teardowns and other weird cases.
    if (!element || !element->GetLayoutObject()) {
      GetFrame().GetDocument()->RecordDeferredLoadReason(
          WouldLoadReason::kNoParent);
    }
    return;
  }
  // Small inaccuracy: frames with origins that match the top level might be
  // nested in a cross-origin frame. To keep code simpler, count such frames as
  // WouldLoadVisible, even when their parent is offscreen.
  WouldLoadReason why_parent_loaded = WouldLoadReason::kVisible;
  if (parent->ParentFrameView() && parent->GetFrame().IsCrossOriginSubframe())
    why_parent_loaded = parent->GetFrame().GetDocument()->DeferredLoadReason();

  // If the parent wasn't loaded, the children won't be either.
  if (why_parent_loaded == WouldLoadReason::kCreated)
    return;
  // These frames are never meant to be seen so we will need to load them.
  if (FrameRect().IsEmpty() || FrameRect().MaxY() < 0 ||
      FrameRect().MaxX() < 0) {
    GetFrame().GetDocument()->RecordDeferredLoadReason(why_parent_loaded);
    return;
  }

  IntRect parent_rect = parent->FrameRect();
  // First clause: for this rough data collection we assume the user never
  // scrolls right.
  if (FrameRect().X() >= parent_rect.Width() || parent_rect.Height() <= 0)
    return;

  int this_frame_screens_away = 0;
  // If an frame is created above the current scoll position, this logic counts
  // it as visible.
  if (FrameRect().Y() > parent->GetScrollOffset().Height()) {
    this_frame_screens_away =
        (FrameRect().Y() - parent->GetScrollOffset().Height()) /
        parent_rect.Height();
  }
  DCHECK_GE(this_frame_screens_away, 0);

  int parent_screens_away = 0;
  if (why_parent_loaded <= WouldLoadReason::kVisible) {
    parent_screens_away = static_cast<int>(WouldLoadReason::kVisible) -
                          static_cast<int>(why_parent_loaded);
  }

  int total_screens_away = this_frame_screens_away + parent_screens_away;

  // We're collecting data for frames that are at most 3 screens away.
  if (total_screens_away > 3)
    return;

  GetFrame().GetDocument()->RecordDeferredLoadReason(
      static_cast<WouldLoadReason>(static_cast<int>(WouldLoadReason::kVisible) -
                                   total_screens_away));
}

bool FrameView::ShouldThrottleRendering() const {
  return CanThrottleRendering() && frame_->GetDocument() &&
         Lifecycle().ThrottlingAllowed();
}

bool FrameView::CanThrottleRendering() const {
  if (lifecycle_updates_throttled_)
    return true;
  if (!RuntimeEnabledFeatures::renderingPipelineThrottlingEnabled())
    return false;
  if (subtree_throttled_)
    return true;
  // We only throttle hidden cross-origin frames. This is to avoid a situation
  // where an ancestor frame directly depends on the pipeline timing of a
  // descendant and breaks as a result of throttling. The rationale is that
  // cross-origin frames must already communicate with asynchronous messages,
  // so they should be able to tolerate some delay in receiving replies from a
  // throttled peer.
  return hidden_for_throttling_ && frame_->IsCrossOriginSubframe();
}

void FrameView::BeginLifecycleUpdates() {
  // Avoid pumping frames for the initially empty document.
  if (!GetFrame().Loader().StateMachine()->CommittedFirstRealDocumentLoad())
    return;
  lifecycle_updates_throttled_ = false;
  if (auto owner = GetFrame().OwnerLayoutItem())
    owner.SetMayNeedPaintInvalidation();
  SetupRenderThrottling();
  UpdateRenderThrottlingStatus(hidden_for_throttling_, subtree_throttled_);
  // The compositor will "defer commits" for the main frame until we
  // explicitly request them.
  if (GetFrame().IsMainFrame())
    GetFrame().GetPage()->GetChromeClient().BeginLifecycleUpdates();
}

void FrameView::SetInitialViewportSize(const IntSize& viewport_size) {
  if (viewport_size == initial_viewport_size_)
    return;

  initial_viewport_size_ = viewport_size;
  if (Document* document = frame_->GetDocument())
    document->GetStyleEngine().InitialViewportChanged();
}

int FrameView::InitialViewportWidth() const {
  DCHECK(frame_->IsMainFrame());
  return initial_viewport_size_.Width();
}

int FrameView::InitialViewportHeight() const {
  DCHECK(frame_->IsMainFrame());
  return initial_viewport_size_.Height();
}

bool FrameView::HasVisibleSlowRepaintViewportConstrainedObjects() const {
  if (!ViewportConstrainedObjects())
    return false;

  for (const LayoutObject* layout_object : *ViewportConstrainedObjects()) {
    DCHECK(layout_object->IsBoxModelObject() && layout_object->HasLayer());
    DCHECK(layout_object->Style()->GetPosition() == EPosition::kFixed ||
           layout_object->Style()->GetPosition() == EPosition::kSticky);
    PaintLayer* layer = ToLayoutBoxModelObject(layout_object)->Layer();

    // Whether the Layer sticks to the viewport is a tree-depenent
    // property and our viewportConstrainedObjects collection is maintained
    // with only LayoutObject-level information.
    if (!layer->FixedToViewport() && !layer->SticksToScroller())
      continue;

    // If the whole subtree is invisible, there's no reason to scroll on
    // the main thread because we don't need to generate invalidations
    // for invisible content.
    if (layer->SubtreeIsInvisible())
      continue;

    // We're only smart enough to scroll viewport-constrainted objects
    // in the compositor if they have their own backing or they paint
    // into a grouped back (which necessarily all have the same viewport
    // constraints).
    CompositingState compositing_state = layer->GetCompositingState();
    if (compositing_state != kPaintsIntoOwnBacking &&
        compositing_state != kPaintsIntoGroupedBacking)
      return true;
  }
  return false;
}

void FrameView::UpdateSubFrameScrollOnMainReason(
    const Frame& frame,
    MainThreadScrollingReasons parent_reason) {
  MainThreadScrollingReasons reasons = parent_reason;

  if (!GetPage()->GetSettings().GetThreadedScrollingEnabled())
    reasons |= MainThreadScrollingReason::kThreadedScrollingDisabled;

  if (!frame.IsLocalFrame())
    return;

  FrameView& frame_view = *ToLocalFrame(frame).View();
  if (frame_view.ShouldThrottleRendering())
    return;

  reasons |= frame_view.MainThreadScrollingReasonsPerFrame();
  if (GraphicsLayer* layer_for_scrolling = ToLocalFrame(frame)
                                               .View()
                                               ->LayoutViewportScrollableArea()
                                               ->LayerForScrolling()) {
    if (WebLayer* platform_layer_for_scrolling =
            layer_for_scrolling->PlatformLayer()) {
      if (reasons) {
        platform_layer_for_scrolling->AddMainThreadScrollingReasons(reasons);
      } else {
        // Clear all main thread scrolling reasons except the one that's set
        // if there is a running scroll animation.
        platform_layer_for_scrolling->ClearMainThreadScrollingReasons(
            ~MainThreadScrollingReason::kHandlingScrollFromMainThread);
      }
    }
  }

  Frame* child = frame.Tree().FirstChild();
  while (child) {
    UpdateSubFrameScrollOnMainReason(*child, reasons);
    child = child->Tree().NextSibling();
  }

  if (frame.IsMainFrame())
    main_thread_scrolling_reasons_ = reasons;
  DCHECK(!MainThreadScrollingReason::HasNonCompositedScrollReasons(
      main_thread_scrolling_reasons_));
}

MainThreadScrollingReasons FrameView::MainThreadScrollingReasonsPerFrame()
    const {
  MainThreadScrollingReasons reasons =
      static_cast<MainThreadScrollingReasons>(0);

  if (ShouldThrottleRendering())
    return reasons;

  if (HasBackgroundAttachmentFixedObjects())
    reasons |= MainThreadScrollingReason::kHasBackgroundAttachmentFixedObjects;

  ScrollingReasons scrolling_reasons = GetScrollingReasons();
  const bool may_be_scrolled_by_input = (scrolling_reasons == kScrollable);
  const bool may_be_scrolled_by_script =
      may_be_scrolled_by_input ||
      (scrolling_reasons == kNotScrollableExplicitlyDisabled);

  // TODO(awoloszyn) Currently crbug.com/304810 will let certain
  // overflow:hidden elements scroll on the compositor thread, so we should
  // not let this move there path as an optimization, when we have
  // slow-repaint elements.
  if (may_be_scrolled_by_script &&
      HasVisibleSlowRepaintViewportConstrainedObjects()) {
    reasons |=
        MainThreadScrollingReason::kHasNonLayerViewportConstrainedObjects;
  }
  return reasons;
}

MainThreadScrollingReasons FrameView::GetMainThreadScrollingReasons() const {
  MainThreadScrollingReasons reasons =
      static_cast<MainThreadScrollingReasons>(0);

  if (!GetPage()->GetSettings().GetThreadedScrollingEnabled())
    reasons |= MainThreadScrollingReason::kThreadedScrollingDisabled;

  if (!GetPage()->MainFrame()->IsLocalFrame())
    return reasons;

  // TODO(alexmos,kenrb): For OOPIF, local roots that are different from
  // the main frame can't be used in the calculation, since they use
  // different compositors with unrelated state, which breaks some of the
  // calculations below.
  if (&frame_->LocalFrameRoot() != GetPage()->MainFrame())
    return reasons;

  // Walk the tree to the root. Use the gathered reasons to determine
  // whether the target frame should be scrolled on main thread regardless
  // other subframes on the same page.
  for (Frame* frame = frame_; frame; frame = frame->Tree().Parent()) {
    if (!frame->IsLocalFrame())
      continue;
    reasons |=
        ToLocalFrame(frame)->View()->MainThreadScrollingReasonsPerFrame();
  }

  DCHECK(!MainThreadScrollingReason::HasNonCompositedScrollReasons(reasons));
  return reasons;
}

String FrameView::MainThreadScrollingReasonsAsText() const {
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    DCHECK(Lifecycle().GetState() >= DocumentLifecycle::kPrePaintClean);

    // Slimming paint v2 stores main thread scrolling reasons on property
    // trees instead of in |m_mainThreadScrollingReasons|.
    MainThreadScrollingReasons reasons = 0;
    if (const auto* scroll_translation = this->ScrollTranslation())
      reasons |=
          scroll_translation->ScrollNode()->GetMainThreadScrollingReasons();
    return String(
        MainThreadScrollingReason::mainThreadScrollingReasonsAsText(reasons)
            .c_str());
  }

  DCHECK(Lifecycle().GetState() >= DocumentLifecycle::kCompositingClean);
  if (LayerForScrolling() && LayerForScrolling()->PlatformLayer()) {
    String result(
        MainThreadScrollingReason::mainThreadScrollingReasonsAsText(
            LayerForScrolling()->PlatformLayer()->MainThreadScrollingReasons())
            .c_str());
    return result;
  }

  String result(MainThreadScrollingReason::mainThreadScrollingReasonsAsText(
                    main_thread_scrolling_reasons_)
                    .c_str());
  return result;
}

void FrameView::SetViewportIntersectionFromParent(
    const IntRect& viewport_intersection) {
  if (remote_viewport_intersection_ != viewport_intersection) {
    remote_viewport_intersection_ = viewport_intersection;
    ScheduleAnimation();
  }
}

IntRect FrameView::RemoteViewportIntersection() {
  IntRect intersection(remote_viewport_intersection_);
  intersection.Move(ScrollOffsetInt());
  return intersection;
}

void FrameView::MapQuadToAncestorFrameIncludingScrollOffset(
    LayoutRect& rect,
    const LayoutObject* descendant,
    const LayoutView* ancestor,
    MapCoordinatesFlags mode) {
  FloatQuad mapped_quad = descendant->LocalToAncestorQuad(
      FloatQuad(FloatRect(rect)), ancestor, mode);
  rect = LayoutRect(mapped_quad.BoundingBox());

  // localToAncestorQuad accounts for scroll offset if it encounters a remote
  // frame in the ancestor chain, otherwise it needs to be added explicitly.
  if (GetFrame().LocalFrameRoot() == GetFrame().Tree().Top() ||
      (ancestor &&
       ancestor->GetFrame()->LocalFrameRoot() == GetFrame().LocalFrameRoot())) {
    FrameView* ancestor_view =
        (ancestor ? ancestor->GetFrameView()
                  : ToLocalFrame(GetFrame().Tree().Top()).View());
    LayoutSize scroll_position = LayoutSize(ancestor_view->GetScrollOffset());
    rect.Move(-scroll_position);
  }
}

bool FrameView::MapToVisualRectInTopFrameSpace(LayoutRect& rect) {
  // This is the top-level frame, so no mapping necessary.
  if (frame_->IsMainFrame())
    return true;

  LayoutRect viewport_intersection_rect(RemoteViewportIntersection());
  rect.Intersect(viewport_intersection_rect);
  if (rect.IsEmpty())
    return false;
  return true;
}

void FrameView::ApplyTransformForTopFrameSpace(
    TransformState& transform_state) {
  // This is the top-level frame, so no mapping necessary.
  if (frame_->IsMainFrame())
    return;

  LayoutRect viewport_intersection_rect(RemoteViewportIntersection());
  transform_state.Move(LayoutSize(-viewport_intersection_rect.X(),
                                  -viewport_intersection_rect.Y()));
}

void FrameView::SetAnimationTimeline(
    std::unique_ptr<CompositorAnimationTimeline> timeline) {
  animation_timeline_ = std::move(timeline);
}

void FrameView::SetAnimationHost(
    std::unique_ptr<CompositorAnimationHost> host) {
  animation_host_ = std::move(host);
}

LayoutUnit FrameView::CaretWidth() const {
  return LayoutUnit(GetChromeClient()->WindowToViewportScalar(1));
}

}  // namespace blink
