/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009 Apple Inc.
 *               All rights reserved.
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

#include "core/layout/LayoutView.h"

#include <inttypes.h>
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/editing/FrameSelection.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLIFrameElement.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutGeometryMap.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/ViewFragmentationContext.h"
#include "core/layout/api/LayoutAPIShim.h"
#include "core/layout/api/LayoutPartItem.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/layout/svg/LayoutSVGRoot.h"
#include "core/page/ChromeClient.h"
#include "core/page/Page.h"
#include "core/paint/PaintLayer.h"
#include "core/paint/ViewPaintInvalidator.h"
#include "core/paint/ViewPainter.h"
#include "core/svg/SVGDocumentExtensions.h"
#include "platform/Histogram.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/geometry/FloatQuad.h"
#include "platform/geometry/TransformState.h"
#include "platform/graphics/paint/PaintController.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/instrumentation/tracing/TracedValue.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/Platform.h"

namespace blink {

namespace {

class HitTestLatencyRecorder {
 public:
  HitTestLatencyRecorder(bool allows_child_frame_content)
      : start_(WTF::MonotonicallyIncreasingTime()),
        allows_child_frame_content_(allows_child_frame_content) {}

  ~HitTestLatencyRecorder() {
    int duration = static_cast<int>(
        (WTF::MonotonicallyIncreasingTime() - start_) * 1000000);

    if (allows_child_frame_content_) {
      DEFINE_STATIC_LOCAL(CustomCountHistogram, recursive_latency_histogram,
                          ("Event.Latency.HitTestRecursive", 0, 10000000, 100));
      recursive_latency_histogram.Count(duration);
    } else {
      DEFINE_STATIC_LOCAL(CustomCountHistogram, latency_histogram,
                          ("Event.Latency.HitTest", 0, 10000000, 100));
      latency_histogram.Count(duration);
    }
  }

 private:
  double start_;
  bool allows_child_frame_content_;
};

}  // namespace

LayoutView::LayoutView(Document* document)
    : LayoutBlockFlow(document),
      frame_view_(document->View()),
      layout_state_(nullptr),
      layout_quote_head_(nullptr),
      layout_counter_count_(0),
      hit_test_count_(0),
      hit_test_cache_hits_(0),
      hit_test_cache_(HitTestCache::Create()) {
  // init LayoutObject attributes
  SetInline(false);

  min_preferred_logical_width_ = LayoutUnit();
  max_preferred_logical_width_ = LayoutUnit();

  SetPreferredLogicalWidthsDirty(kMarkOnlyThis);

  SetPositionState(EPosition::kAbsolute);  // to 0,0 :)
}

LayoutView::~LayoutView() {}

bool LayoutView::HitTest(HitTestResult& result) {
  // We have to recursively update layout/style here because otherwise, when the
  // hit test recurses into a child document, it could trigger a layout on the
  // parent document, which can destroy PaintLayer that are higher up in the
  // call stack, leading to crashes.
  // Note that Document::updateLayout calls its parent's updateLayout.
  // Note that if an iframe has its render pipeline throttled, it will not
  // update layout here, and it will also not propagate the hit test into the
  // iframe's inner document.
  GetFrameView()->UpdateLifecycleToCompositingCleanPlusScrolling();
  HitTestLatencyRecorder hit_test_latency_recorder(
      result.GetHitTestRequest().AllowsChildFrameContent());
  return HitTestNoLifecycleUpdate(result);
}

bool LayoutView::HitTestNoLifecycleUpdate(HitTestResult& result) {
  TRACE_EVENT_BEGIN0("blink,devtools.timeline", "HitTest");
  hit_test_count_++;

  DCHECK(!result.GetHitTestLocation().IsRectBasedTest() ||
         result.GetHitTestRequest().ListBased());

  uint64_t dom_tree_version = GetDocument().DomTreeVersion();
  HitTestResult cache_result = result;
  bool hit_layer = false;
  if (hit_test_cache_->LookupCachedResult(cache_result, dom_tree_version)) {
    hit_test_cache_hits_++;
    hit_layer = true;
    result = cache_result;
  } else {
    hit_layer = Layer()->HitTest(result);

    // FrameView scrollbars are not the same as Layer scrollbars tested by
    // Layer::hitTestOverflowControls, so we need to test FrameView scrollbars
    // separately here. Note that it's important we do this after the hit test
    // above, because that may overwrite the entire HitTestResult when it finds
    // a hit.
    IntPoint frame_point = GetFrameView()->ContentsToFrame(
        result.GetHitTestLocation().RoundedPoint());
    if (Scrollbar* frame_scrollbar =
            GetFrameView()->ScrollbarAtFramePoint(frame_point))
      result.SetScrollbar(frame_scrollbar);

    // If hitTestResult include scrollbar, innerNode should be the parent of the
    // scrollbar.
    if (result.GetScrollbar()) {
      // Clear innerNode if we hit a scrollbar whose ScrollableArea isn't
      // associated with a LayoutBox so we aren't hitting some random element
      // below too.
      result.SetInnerNode(nullptr);
      result.SetURLElement(nullptr);
      ScrollableArea* scrollable_area =
          result.GetScrollbar()->GetScrollableArea();
      if (scrollable_area && scrollable_area->GetLayoutBox() &&
          scrollable_area->GetLayoutBox()->GetNode()) {
        Node* node = scrollable_area->GetLayoutBox()->GetNode();
        result.SetInnerNode(node);
        result.SetURLElement(node->EnclosingLinkEventParentOrSelf());
      }
    }

    if (hit_layer)
      hit_test_cache_->AddCachedResult(result, dom_tree_version);
  }

  TRACE_EVENT_END1(
      "blink,devtools.timeline", "HitTest", "endData",
      InspectorHitTestEvent::EndData(result.GetHitTestRequest(),
                                     result.GetHitTestLocation(), result));
  return hit_layer;
}

void LayoutView::ClearHitTestCache() {
  hit_test_cache_->Clear();
  LayoutPartItem frame_layout_item = GetFrame()->OwnerLayoutItem();
  if (!frame_layout_item.IsNull())
    frame_layout_item.View().ClearHitTestCache();
}

void LayoutView::ComputeLogicalHeight(
    LayoutUnit logical_height,
    LayoutUnit,
    LogicalExtentComputedValues& computed_values) const {
  computed_values.extent_ = LayoutUnit(ViewLogicalHeightForBoxSizing());
}

void LayoutView::UpdateLogicalWidth() {
  SetLogicalWidth(LayoutUnit(ViewLogicalWidthForBoxSizing()));
}

bool LayoutView::IsChildAllowed(LayoutObject* child,
                                const ComputedStyle&) const {
  return child->IsBox();
}

bool LayoutView::CanHaveChildren() const {
  FrameOwner* owner = GetFrame()->Owner();
  if (!owner)
    return true;
  if (!RuntimeEnabledFeatures::displayNoneIFrameCreatesNoLayoutObjectEnabled())
    return true;
  return !owner->IsDisplayNone();
}

void LayoutView::LayoutContent() {
  DCHECK(NeedsLayout());

  LayoutBlockFlow::UpdateLayout();

#if DCHECK_IS_ON()
  CheckLayoutState();
#endif
}

#if DCHECK_IS_ON()
void LayoutView::CheckLayoutState() {
  DCHECK(!layout_state_->Next());
}
#endif

void LayoutView::SetShouldDoFullPaintInvalidationOnResizeIfNeeded(
    bool width_changed,
    bool height_changed) {
  // When background-attachment is 'fixed', we treat the viewport (instead of
  // the 'root' i.e. html or body) as the background positioning area, and we
  // should fully invalidate on viewport resize if the background image is not
  // composited and needs full paint invalidation on background positioning area
  // resize.
  if (Style()->HasFixedBackgroundImage() &&
      (!compositor_ || !compositor_->NeedsFixedRootBackgroundLayer(Layer()))) {
    if ((width_changed && MustInvalidateFillLayersPaintOnWidthChange(
                              Style()->BackgroundLayers())) ||
        (height_changed && MustInvalidateFillLayersPaintOnHeightChange(
                               Style()->BackgroundLayers())))
      SetShouldDoFullPaintInvalidation(PaintInvalidationReason::kBackground);
  }
}

void LayoutView::UpdateLayout() {
  if (!GetDocument().Paginated())
    SetPageLogicalHeight(LayoutUnit());

  // TODO(wangxianzhu): Move this into ViewPaintInvalidator when
  // rootLayerScrolling is permanently enabled.
  IncludeScrollbarsInRect include_scrollbars =
      RuntimeEnabledFeatures::rootLayerScrollingEnabled() ? kIncludeScrollbars
                                                          : kExcludeScrollbars;
  SetShouldDoFullPaintInvalidationOnResizeIfNeeded(
      OffsetWidth() != GetLayoutSize(include_scrollbars).Width(),
      OffsetHeight() != GetLayoutSize(include_scrollbars).Height());

  if (PageLogicalHeight() && ShouldUsePrintingLayout()) {
    min_preferred_logical_width_ = max_preferred_logical_width_ =
        LogicalWidth();
    if (!fragmentation_context_) {
      fragmentation_context_ =
          WTF::WrapUnique(new ViewFragmentationContext(*this));
      pagination_state_changed_ = true;
    }
  } else if (fragmentation_context_) {
    fragmentation_context_.reset();
    pagination_state_changed_ = true;
  }

  SubtreeLayoutScope layout_scope(*this);

  // Use calcWidth/Height to get the new width/height, since this will take the
  // full page zoom factor into account.
  bool relayout_children =
      !ShouldUsePrintingLayout() &&
      (!frame_view_ || LogicalWidth() != ViewLogicalWidthForBoxSizing() ||
       LogicalHeight() != ViewLogicalHeightForBoxSizing());

  if (relayout_children) {
    layout_scope.SetChildNeedsLayout(this);
    for (LayoutObject* child = FirstChild(); child;
         child = child->NextSibling()) {
      if (child->IsSVGRoot())
        continue;

      if ((child->IsBox() && ToLayoutBox(child)->HasRelativeLogicalHeight()) ||
          child->Style()->LogicalHeight().IsPercentOrCalc() ||
          child->Style()->LogicalMinHeight().IsPercentOrCalc() ||
          child->Style()->LogicalMaxHeight().IsPercentOrCalc())
        layout_scope.SetChildNeedsLayout(child);
    }

    if (GetDocument().SvgExtensions())
      GetDocument()
          .AccessSVGExtensions()
          .InvalidateSVGRootsWithRelativeLengthDescendents(&layout_scope);
  }

  DCHECK(!layout_state_);
  if (!NeedsLayout())
    return;

  LayoutState root_layout_state(*this);

  LayoutContent();

#if DCHECK_IS_ON()
  CheckLayoutState();
#endif
  ClearNeedsLayout();
}

LayoutRect LayoutView::VisualOverflowRect() const {
  // In root layer scrolling mode, the LayoutView performs overflow clipping
  // like a regular scrollable div.
  if (RuntimeEnabledFeatures::rootLayerScrollingEnabled())
    return LayoutBlockFlow::VisualOverflowRect();

  // Ditto when not in compositing mode.
  if (!UsesCompositing())
    return LayoutBlockFlow::VisualOverflowRect();

  // In normal compositing mode, LayoutView doesn't actually apply clipping
  // on its descendants. Instead their visual overflow is propagated to
  // compositor()->m_rootContentLayer for accelerated scrolling.
  return LayoutOverflowRect();
}

LayoutRect LayoutView::LocalVisualRect() const {
  // TODO(wangxianzhu): This is only required without rootLayerScrolls (though
  // it is also correct but unnecessary with rootLayerScrolls) because of the
  // special LayoutView overflow model.
  LayoutRect rect = VisualOverflowRect();
  rect.Unite(LayoutRect(rect.Location(), ViewRect().Size()));
  return rect;
}

void LayoutView::MapLocalToAncestor(const LayoutBoxModelObject* ancestor,
                                    TransformState& transform_state,
                                    MapCoordinatesFlags mode) const {
  if (!ancestor && mode & kUseTransforms &&
      ShouldUseTransformFromContainer(0)) {
    TransformationMatrix t;
    GetTransformFromContainer(0, LayoutSize(), t);
    transform_state.ApplyTransform(t);
  }

  if ((mode & kIsFixed) && frame_view_) {
    transform_state.Move(OffsetForFixedPosition());
    // IsFixed flag is only applicable within this LayoutView.
    mode &= ~kIsFixed;
  }

  if (ancestor == this)
    return;

  if (mode & kTraverseDocumentBoundaries) {
    LayoutPartItem parent_doc_layout_item = GetFrame()->OwnerLayoutItem();
    if (!parent_doc_layout_item.IsNull()) {
      if (!(mode & kInputIsInFrameCoordinates)) {
        transform_state.Move(
            LayoutSize(-GetFrame()->View()->GetScrollOffset()));
      } else {
        // The flag applies to immediate LayoutView only.
        mode &= ~kInputIsInFrameCoordinates;
      }

      transform_state.Move(parent_doc_layout_item.ContentBoxOffset());

      parent_doc_layout_item.MapLocalToAncestor(ancestor, transform_state,
                                                mode);
    } else {
      GetFrameView()->ApplyTransformForTopFrameSpace(transform_state);
    }
  }
}

const LayoutObject* LayoutView::PushMappingToContainer(
    const LayoutBoxModelObject* ancestor_to_stop_at,
    LayoutGeometryMap& geometry_map) const {
  LayoutSize offset;
  LayoutObject* container = nullptr;

  if (geometry_map.GetMapCoordinatesFlags() & kTraverseDocumentBoundaries) {
    if (LayoutPart* parent_doc_layout_object = ToLayoutPart(
            LayoutAPIShim::LayoutObjectFrom(GetFrame()->OwnerLayoutItem()))) {
      offset = -LayoutSize(frame_view_->GetScrollOffset());
      offset += parent_doc_layout_object->ContentBoxOffset();
      container = parent_doc_layout_object;
    }
  }

  // If a container was specified, and was not 0 or the LayoutView, then we
  // should have found it by now unless we're traversing to a parent document.
  DCHECK(!ancestor_to_stop_at || ancestor_to_stop_at == this || container);

  if ((!ancestor_to_stop_at || container) &&
      ShouldUseTransformFromContainer(container)) {
    TransformationMatrix t;
    GetTransformFromContainer(container, LayoutSize(), t);
    geometry_map.Push(this, t, kContainsFixedPosition,
                      OffsetForFixedPosition());
  } else {
    geometry_map.Push(this, offset, 0, OffsetForFixedPosition());
  }

  return container;
}

void LayoutView::MapAncestorToLocal(const LayoutBoxModelObject* ancestor,
                                    TransformState& transform_state,
                                    MapCoordinatesFlags mode) const {
  if (this != ancestor && (mode & kTraverseDocumentBoundaries)) {
    if (LayoutPart* parent_doc_layout_object = ToLayoutPart(
            LayoutAPIShim::LayoutObjectFrom(GetFrame()->OwnerLayoutItem()))) {
      // A LayoutView is a containing block for fixed-position elements, so
      // don't carry this state across frames.
      parent_doc_layout_object->MapAncestorToLocal(ancestor, transform_state,
                                                   mode & ~kIsFixed);

      transform_state.Move(parent_doc_layout_object->ContentBoxOffset());
      transform_state.Move(LayoutSize(-GetFrame()->View()->GetScrollOffset()));
    }
  } else {
    DCHECK(this == ancestor || !ancestor);
  }

  if (mode & kIsFixed)
    transform_state.Move(OffsetForFixedPosition());
}

void LayoutView::ComputeSelfHitTestRects(Vector<LayoutRect>& rects,
                                         const LayoutPoint&) const {
  // Record the entire size of the contents of the frame. Note that we don't
  // just use the viewport size (containing block) here because we want to
  // ensure this includes all children (so we can avoid walking them
  // explicitly).
  rects.push_back(LayoutRect(LayoutPoint::Zero(),
                             LayoutSize(GetFrameView()->ContentsSize())));
}

PaintInvalidationReason LayoutView::InvalidatePaint(
    const PaintInvalidatorContext& context) const {
  return ViewPaintInvalidator(*this, context).InvalidatePaint();
}

void LayoutView::Paint(const PaintInfo& paint_info,
                       const LayoutPoint& paint_offset) const {
  ViewPainter(*this).Paint(paint_info, paint_offset);
}

void LayoutView::PaintBoxDecorationBackground(const PaintInfo& paint_info,
                                              const LayoutPoint&) const {
  ViewPainter(*this).PaintBoxDecorationBackground(paint_info);
}

static void SetShouldDoFullPaintInvalidationForViewAndAllDescendantsInternal(
    LayoutObject* object) {
  object->SetShouldDoFullPaintInvalidation();
  for (LayoutObject* child = object->SlowFirstChild(); child;
       child = child->NextSibling()) {
    SetShouldDoFullPaintInvalidationForViewAndAllDescendantsInternal(child);
  }
}

void LayoutView::SetShouldDoFullPaintInvalidationForViewAndAllDescendants() {
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled())
    SetShouldDoFullPaintInvalidationIncludingNonCompositingDescendants();
  else
    SetShouldDoFullPaintInvalidationForViewAndAllDescendantsInternal(this);
}

void LayoutView::InvalidatePaintForViewAndCompositedLayers() {
  SetShouldDoFullPaintInvalidationIncludingNonCompositingDescendants();

  // The only way we know how to hit these ASSERTS below this point is via the
  // Chromium OS login screen.
  DisableCompositingQueryAsserts disabler;

  if (Compositor()->InCompositingMode())
    Compositor()->FullyInvalidatePaint();
}

bool LayoutView::MapToVisualRectInAncestorSpace(
    const LayoutBoxModelObject* ancestor,
    LayoutRect& rect,
    MapCoordinatesFlags mode,
    VisualRectFlags visual_rect_flags) const {
  TransformState transform_state(TransformState::kApplyTransformDirection,
                                 FloatQuad(FloatRect(rect)));
  bool retval = MapToVisualRectInAncestorSpaceInternal(
      ancestor, transform_state, mode, visual_rect_flags);
  transform_state.Flatten();
  rect = LayoutRect(transform_state.LastPlanarQuad().BoundingBox());
  return retval;
}

bool LayoutView::MapToVisualRectInAncestorSpaceInternal(
    const LayoutBoxModelObject* ancestor,
    TransformState& transform_state,
    VisualRectFlags visual_rect_flags) const {
  return MapToVisualRectInAncestorSpaceInternal(ancestor, transform_state, 0,
                                                visual_rect_flags);
}

bool LayoutView::MapToVisualRectInAncestorSpaceInternal(
    const LayoutBoxModelObject* ancestor,
    TransformState& transform_state,
    MapCoordinatesFlags mode,
    VisualRectFlags visual_rect_flags) const {
  if (mode & kIsFixed)
    transform_state.Move(OffsetForFixedPosition(true));

  // Apply our transform if we have one (because of full page zooming).
  if (Layer() && Layer()->Transform()) {
    transform_state.ApplyTransform(Layer()->CurrentTransform(),
                                   TransformState::kFlattenTransform);
  }

  transform_state.Flatten();

  if (ancestor == this)
    return true;

  Element* owner = GetDocument().LocalOwner();
  if (!owner) {
    LayoutRect rect(transform_state.LastPlanarQuad().BoundingBox());
    bool retval = GetFrameView()->MapToVisualRectInTopFrameSpace(rect);
    transform_state.SetQuad(FloatQuad(FloatRect(rect)));
    return retval;
  }

  if (LayoutBox* obj = owner->GetLayoutBox()) {
    LayoutRect rect(transform_state.LastPlanarQuad().BoundingBox());
    if (!(mode & kInputIsInFrameCoordinates)) {
      // Intersect the viewport with the visual rect.
      LayoutRect view_rectangle = ViewRect();
      if (visual_rect_flags & kEdgeInclusive) {
        if (!rect.InclusiveIntersect(view_rectangle)) {
          transform_state.SetQuad(FloatQuad(FloatRect(rect)));
          return false;
        }
      } else {
        rect.Intersect(view_rectangle);
      }

      // Adjust for scroll offset of the view.
      rect.MoveBy(-view_rectangle.Location());
    }
    // Frames are painted at rounded-int position. Since we cannot efficiently
    // compute the subpixel offset of painting at this point in a a bottom-up
    // walk, round to the enclosing int rect, which will enclose the actual
    // visible rect.
    rect = LayoutRect(EnclosingIntRect(rect));

    // Adjust for frame border.
    rect.Move(obj->ContentBoxOffset());
    transform_state.SetQuad(FloatQuad(FloatRect(rect)));

    return obj->MapToVisualRectInAncestorSpaceInternal(
        ancestor, transform_state, visual_rect_flags);
  }

  // This can happen, e.g., if the iframe element has display:none.
  transform_state.SetQuad(FloatQuad(FloatRect()));
  return false;
}

LayoutSize LayoutView::OffsetForFixedPosition(
    bool include_pending_scroll) const {
  FloatSize adjustment;
  if (frame_view_) {
    adjustment += frame_view_->GetScrollOffset();

    // FIXME: Paint invalidation should happen after scroll updates, so there
    // should be no pending scroll delta.
    // However, we still have paint invalidation during layout, so we can't
    // DCHECK for now. crbug.com/434950.
    // DCHECK(m_frameView->pendingScrollDelta().isZero());
    // If we have a pending scroll, invalidate the previous scroll position.
    if (include_pending_scroll && !frame_view_->PendingScrollDelta().IsZero())
      adjustment -= frame_view_->PendingScrollDelta();
  }

  if (HasOverflowClip())
    adjustment += FloatSize(ScrolledContentOffset());

  return RoundedLayoutSize(adjustment);
}

void LayoutView::AbsoluteRects(Vector<IntRect>& rects,
                               const LayoutPoint& accumulated_offset) const {
  rects.push_back(
      PixelSnappedIntRect(accumulated_offset, LayoutSize(Layer()->size())));
}

void LayoutView::AbsoluteQuads(Vector<FloatQuad>& quads,
                               MapCoordinatesFlags mode) const {
  quads.push_back(LocalToAbsoluteQuad(
      FloatRect(FloatPoint(), FloatSize(Layer()->size())), mode));
}

void LayoutView::ClearSelection() {
  frame_view_->GetFrame().Selection().ClearLayoutSelection();
}

void LayoutView::CommitPendingSelection() {
  TRACE_EVENT0("blink", "LayoutView::commitPendingSelection");
  DCHECK(!NeedsLayout());
  frame_view_->GetFrame().Selection().CommitAppearanceIfNeeded();
}

bool LayoutView::ShouldUsePrintingLayout() const {
  if (!GetDocument().Printing() || !frame_view_)
    return false;
  return frame_view_->GetFrame().ShouldUsePrintingLayout();
}

LayoutRect LayoutView::ViewRect() const {
  if (ShouldUsePrintingLayout())
    return LayoutRect(LayoutPoint(), Size());
  if (frame_view_)
    return LayoutRect(frame_view_->VisibleContentRect());
  return LayoutRect();
}

LayoutRect LayoutView::OverflowClipRect(
    const LayoutPoint& location,
    OverlayScrollbarClipBehavior overlay_scrollbar_clip_behavior) const {
  LayoutRect rect = ViewRect();
  if (rect.IsEmpty())
    return LayoutBox::OverflowClipRect(location,
                                       overlay_scrollbar_clip_behavior);

  rect.SetLocation(location);
  if (HasOverflowClip())
    ExcludeScrollbars(rect, overlay_scrollbar_clip_behavior);

  return rect;
}

void LayoutView::CalculateScrollbarModes(ScrollbarMode& h_mode,
                                         ScrollbarMode& v_mode) const {
#define RETURN_SCROLLBAR_MODE(mode) \
  {                                 \
    h_mode = v_mode = mode;         \
    return;                         \
  }

  LocalFrame* frame = GetFrame();
  if (!frame)
    RETURN_SCROLLBAR_MODE(kScrollbarAlwaysOff);

  if (FrameOwner* owner = frame->Owner()) {
    // Setting scrolling="no" on an iframe element disables scrolling.
    if (owner->ScrollingMode() == kScrollbarAlwaysOff)
      RETURN_SCROLLBAR_MODE(kScrollbarAlwaysOff);
  }

  Document& document = GetDocument();
  if (Node* body = document.body()) {
    // Framesets can't scroll.
    if (isHTMLFrameSetElement(body) && body->GetLayoutObject())
      RETURN_SCROLLBAR_MODE(kScrollbarAlwaysOff);
  }

  if (FrameView* frameView = GetFrameView()) {
    // Scrollbars can be disabled by FrameView::setCanHaveScrollbars.
    if (!frameView->CanHaveScrollbars())
      RETURN_SCROLLBAR_MODE(kScrollbarAlwaysOff);
  }

  Element* viewportDefiningElement = document.ViewportDefiningElement();
  if (!viewportDefiningElement)
    RETURN_SCROLLBAR_MODE(kScrollbarAuto);

  LayoutObject* viewport = viewportDefiningElement->GetLayoutObject();
  if (!viewport)
    RETURN_SCROLLBAR_MODE(kScrollbarAuto);

  const ComputedStyle* style = viewport->Style();
  if (!style)
    RETURN_SCROLLBAR_MODE(kScrollbarAuto);

  if (viewport->IsSVGRoot()) {
    // Don't allow overflow to affect <img> and css backgrounds
    if (ToLayoutSVGRoot(viewport)->IsEmbeddedThroughSVGImage())
      RETURN_SCROLLBAR_MODE(kScrollbarAuto);

    // FIXME: evaluate if we can allow overflow for these cases too.
    // Overflow is always hidden when stand-alone SVG documents are embedded.
    if (ToLayoutSVGRoot(viewport)
            ->IsEmbeddedThroughFrameContainingSVGDocument())
      RETURN_SCROLLBAR_MODE(kScrollbarAlwaysOff);
  }

  h_mode = v_mode = kScrollbarAuto;

  EOverflow overflow_x = style->OverflowX();
  EOverflow overflow_y = style->OverflowY();

  bool shouldIgnoreOverflowHidden = false;
  if (Settings* settings = document.GetSettings()) {
    if (settings->GetIgnoreMainFrameOverflowHiddenQuirk() &&
        frame->IsMainFrame())
      shouldIgnoreOverflowHidden = true;
  }
  if (!shouldIgnoreOverflowHidden) {
    if (overflow_x == EOverflow::kHidden)
      h_mode = kScrollbarAlwaysOff;
    if (overflow_y == EOverflow::kHidden)
      v_mode = kScrollbarAlwaysOff;
  }

  if (overflow_x == EOverflow::kScroll)
    h_mode = kScrollbarAlwaysOn;
  if (overflow_y == EOverflow::kScroll)
    v_mode = kScrollbarAlwaysOn;

#undef RETURN_SCROLLBAR_MODE
}

IntRect LayoutView::DocumentRect() const {
  LayoutRect overflow_rect(LayoutOverflowRect());
  FlipForWritingMode(overflow_rect);
  // TODO(crbug.com/650768): The pixel snapping looks incorrect.
  return PixelSnappedIntRect(overflow_rect);
}

bool LayoutView::RootBackgroundIsEntirelyFixed() const {
  return Style()->HasEntirelyFixedBackground();
}

IntSize LayoutView::GetLayoutSize(
    IncludeScrollbarsInRect scrollbar_inclusion) const {
  if (ShouldUsePrintingLayout())
    return IntSize(Size().Width().ToInt(), PageLogicalHeight().ToInt());

  if (!frame_view_)
    return IntSize();

  IntSize result = frame_view_->GetLayoutSize(kIncludeScrollbars);
  if (scrollbar_inclusion == kExcludeScrollbars)
    result =
        frame_view_->LayoutViewportScrollableArea()->ExcludeScrollbars(result);
  return result;
}

int LayoutView::ViewLogicalWidth(
    IncludeScrollbarsInRect scrollbar_inclusion) const {
  return Style()->IsHorizontalWritingMode() ? ViewWidth(scrollbar_inclusion)
                                            : ViewHeight(scrollbar_inclusion);
}

int LayoutView::ViewLogicalHeight(
    IncludeScrollbarsInRect scrollbar_inclusion) const {
  return Style()->IsHorizontalWritingMode() ? ViewHeight(scrollbar_inclusion)
                                            : ViewWidth(scrollbar_inclusion);
}

int LayoutView::ViewLogicalWidthForBoxSizing() const {
  return ViewLogicalWidth(RuntimeEnabledFeatures::rootLayerScrollingEnabled()
                              ? kIncludeScrollbars
                              : kExcludeScrollbars);
}

int LayoutView::ViewLogicalHeightForBoxSizing() const {
  return ViewLogicalHeight(RuntimeEnabledFeatures::rootLayerScrollingEnabled()
                               ? kIncludeScrollbars
                               : kExcludeScrollbars);
}

LayoutUnit LayoutView::ViewLogicalHeightForPercentages() const {
  if (ShouldUsePrintingLayout())
    return PageLogicalHeight();
  return LayoutUnit(ViewLogicalHeight());
}

float LayoutView::ZoomFactor() const {
  return frame_view_->GetFrame().PageZoomFactor();
}

void LayoutView::UpdateAfterLayout() {
  // Unlike every other layer, the root PaintLayer takes its size from the
  // layout viewport size.  The call to AdjustViewSize() will update the
  // frame's contents size, which will also update the page's minimum scale
  // factor.  The call to ResizeAfterLayout() will calculate the layout viewport
  // size based on the page minimum scale factor, and then update the FrameView
  // with the new size.
  if (HasOverflowClip())
    GetScrollableArea()->ClampScrollOffsetAfterOverflowChange();
  LocalFrame& frame = GetFrameView()->GetFrame();
  if (!GetDocument().Printing())
    GetFrameView()->AdjustViewSize();
  frame.GetChromeClient().ResizeAfterLayout(&frame);
  LayoutBlockFlow::UpdateAfterLayout();
}

void LayoutView::UpdateHitTestResult(HitTestResult& result,
                                     const LayoutPoint& point) {
  if (result.InnerNode())
    return;

  Node* node = GetDocument().documentElement();
  if (node) {
    LayoutPoint adjusted_point = point;
    OffsetForContents(adjusted_point);
    result.SetNodeAndPosition(node, adjusted_point);
  }
}

bool LayoutView::UsesCompositing() const {
  return compositor_ && compositor_->StaleInCompositingMode();
}

PaintLayerCompositor* LayoutView::Compositor() {
  if (!compositor_)
    compositor_ = WTF::WrapUnique(new PaintLayerCompositor(*this));

  return compositor_.get();
}

void LayoutView::SetIsInWindow(bool is_in_window) {
  if (compositor_)
    compositor_->SetIsInWindow(is_in_window);
#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
  // We don't invalidate layers during Document::detachLayoutTree(), so must
  // clear the should-keep-alive DisplayItemClients which may be deleted before
  // the layers being subsequence owners.
  if (!is_in_window && Layer())
    Layer()->EndShouldKeepAliveAllClientsRecursive();
#endif
}

IntervalArena* LayoutView::GetIntervalArena() {
  if (!interval_arena_)
    interval_arena_ = IntervalArena::Create();
  return interval_arena_.Get();
}

bool LayoutView::BackgroundIsKnownToBeOpaqueInRect(const LayoutRect&) const {
  // FIXME: Remove this main frame check. Same concept applies to subframes too.
  if (!GetFrame()->IsMainFrame())
    return false;

  return frame_view_->HasOpaqueBackground();
}

FloatSize LayoutView::ViewportSizeForViewportUnits() const {
  return GetFrameView() ? GetFrameView()->ViewportSizeForViewportUnits()
                        : FloatSize();
}

void LayoutView::WillBeDestroyed() {
  // TODO(wangxianzhu): This is a workaround of crbug.com/570706.
  // Should find and fix the root cause.
  if (PaintLayer* layer = this->Layer())
    layer->SetNeedsRepaint();
  LayoutBlockFlow::WillBeDestroyed();
  compositor_.reset();
}

void LayoutView::UpdateFromStyle() {
  LayoutBlockFlow::UpdateFromStyle();

  // LayoutView of the main frame is responsible for painting base background.
  if (GetDocument().IsInMainFrame())
    SetHasBoxDecorationBackground(true);
}

bool LayoutView::AllowsOverflowClip() const {
  return RuntimeEnabledFeatures::rootLayerScrollingEnabled();
}

ScrollResult LayoutView::Scroll(ScrollGranularity granularity,
                                const FloatSize& delta) {
  // TODO(bokan): We shouldn't need this specialization but we currently do
  // because of the Windows pan scrolling path. That should go through a more
  // normalized ScrollManager-like scrolling path and we should get rid of
  // of this override. All frame scrolling should be handled by
  // ViewportScrollCallback.

  if (!GetFrameView())
    return ScrollResult(false, false, delta.Width(), delta.Height());

  return GetFrameView()->GetScrollableArea()->UserScroll(granularity, delta);
}

LayoutRect LayoutView::DebugRect() const {
  LayoutRect rect;
  LayoutBlock* block = ContainingBlock();
  if (block)
    block->AdjustChildDebugRect(rect);

  rect.SetWidth(LayoutUnit(ViewWidth(kIncludeScrollbars)));
  rect.SetHeight(LayoutUnit(ViewHeight(kIncludeScrollbars)));

  return rect;
}

bool LayoutView::PaintedOutputOfObjectHasNoEffectRegardlessOfSize() const {
  // Frame scroll corner is painted using LayoutView as the display item client.
  if (!RuntimeEnabledFeatures::rootLayerScrollingEnabled() &&
      (GetFrameView()->HorizontalScrollbar() ||
       GetFrameView()->VerticalScrollbar()))
    return false;

  return LayoutBlockFlow::PaintedOutputOfObjectHasNoEffectRegardlessOfSize();
}

}  // namespace blink
