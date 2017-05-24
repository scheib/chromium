/*
 * Copyright (C) 2006, 2007, 2008, 2009, 2010, 2011, 2012 Apple Inc. All rights
 * reserved.
 *
 * Portions are Copyright (C) 1998 Netscape Communications Corporation.
 *
 * Other contributors:
 *   Robert O'Callahan <roc+@cs.cmu.edu>
 *   David Baron <dbaron@fas.harvard.edu>
 *   Christian Biesinger <cbiesinger@gmail.com>
 *   Randall Jesup <rjesup@wgate.com>
 *   Roland Mainz <roland.mainz@informatik.med.uni-giessen.de>
 *   Josh Soref <timeless@mac.com>
 *   Boris Zbarsky <bzbarsky@mit.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Alternatively, the contents of this file may be used under the terms
 * of either the Mozilla Public License Version 1.1, found at
 * http://www.mozilla.org/MPL/ (the "MPL") or the GNU General Public
 * License Version 2.0, found at http://www.fsf.org/copyleft/gpl.html
 * (the "GPL"), in which case the provisions of the MPL or the GPL are
 * applicable instead of those above.  If you wish to allow use of your
 * version of this file only under the terms of one of those two
 * licenses (the MPL or the GPL) and not to allow others to use your
 * version of this file under the LGPL, indicate your decision by
 * deletingthe provisions above and replace them with the notice and
 * other provisions required by the MPL or the GPL, as the case may be.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under any of the LGPL, the MPL or the GPL.
 */

#include "core/paint/PaintLayerScrollableArea.h"

#include "core/css/PseudoStyleRequest.h"
#include "core/dom/AXObjectCache.h"
#include "core/dom/DOMNodeIds.h"
#include "core/dom/Node.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "core/editing/FrameSelection.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/PageScaleConstraintsSet.h"
#include "core/frame/RootFrameViewport.h"
#include "core/frame/Settings.h"
#include "core/frame/VisualViewport.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/input/EventHandler.h"
#include "core/layout/LayoutFlexibleBox.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/LayoutScrollbar.h"
#include "core/layout/LayoutScrollbarPart.h"
#include "core/layout/LayoutTheme.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutBoxItem.h"
#include "core/layout/compositing/CompositedLayerMapping.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/page/ChromeClient.h"
#include "core/page/FocusController.h"
#include "core/page/Page.h"
#include "core/page/scrolling/RootScrollerController.h"
#include "core/page/scrolling/RootScrollerUtil.h"
#include "core/page/scrolling/ScrollingCoordinator.h"
#include "core/page/scrolling/TopDocumentRootScrollerController.h"
#include "core/paint/PaintLayerFragment.h"
#include "platform/graphics/CompositorMutableProperties.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/paint/DrawingRecorder.h"
#include "platform/scroll/ScrollAnimatorBase.h"
#include "platform/scroll/ScrollbarTheme.h"
#include "public/platform/Platform.h"

namespace blink {

static LayoutRect LocalToAbsolute(LayoutBox& offset, LayoutRect rect) {
  return LayoutRect(
      offset.LocalToAbsoluteQuad(FloatQuad(FloatRect(rect)), kUseTransforms)
          .BoundingBox());
}

PaintLayerScrollableAreaRareData::PaintLayerScrollableAreaRareData() {}

const int kResizerControlExpandRatioForTouch = 2;

PaintLayerScrollableArea::PaintLayerScrollableArea(PaintLayer& layer)
    : layer_(layer),
      next_topmost_scroll_child_(0),
      topmost_scroll_child_(0),
      in_resize_mode_(false),
      scrolls_overflow_(false),
      in_overflow_relayout_(false),
      needs_composited_scrolling_(false),
      rebuild_horizontal_scrollbar_layer_(false),
      rebuild_vertical_scrollbar_layer_(false),
      needs_scroll_offset_clamp_(false),
      needs_relayout_(false),
      had_horizontal_scrollbar_before_relayout_(false),
      had_vertical_scrollbar_before_relayout_(false),
      scrollbar_manager_(*this),
      scroll_corner_(nullptr),
      resizer_(nullptr),
      scroll_anchor_(this),
      non_composited_main_thread_scrolling_reasons_(0)
#if DCHECK_IS_ON()
      ,
      has_been_disposed_(false)
#endif
{
  Node* node = Box().GetNode();
  if (node && node->IsElementNode()) {
    // We save and restore only the scrollOffset as the other scroll values are
    // recalculated.
    Element* element = ToElement(node);
    scroll_offset_ = element->SavedLayerScrollOffset();
    if (!scroll_offset_.IsZero())
      GetScrollAnimator().SetCurrentOffset(scroll_offset_);
    element->SetSavedLayerScrollOffset(ScrollOffset());
  }
  UpdateResizerAreaSet();
}

PaintLayerScrollableArea::~PaintLayerScrollableArea() {
#if DCHECK_IS_ON()
  DCHECK(has_been_disposed_);
#endif
}

void PaintLayerScrollableArea::Dispose() {
  if (InResizeMode() && !Box().DocumentBeingDestroyed()) {
    if (LocalFrame* frame = Box().GetFrame())
      frame->GetEventHandler().ResizeScrollableAreaDestroyed();
  }

  if (LocalFrame* frame = Box().GetFrame()) {
    if (FrameView* frame_view = frame->View()) {
      frame_view->RemoveScrollableArea(this);
      frame_view->RemoveAnimatingScrollableArea(this);
    }
  }

  non_composited_main_thread_scrolling_reasons_ = 0;

  if (ScrollingCoordinator* scrolling_coordinator = GetScrollingCoordinator())
    scrolling_coordinator->WillDestroyScrollableArea(this);

  if (!Box().DocumentBeingDestroyed()) {
    Node* node = Box().GetNode();
    // FIXME: Make setSavedLayerScrollOffset take DoubleSize. crbug.com/414283.
    if (node && node->IsElementNode())
      ToElement(node)->SetSavedLayerScrollOffset(scroll_offset_);
  }

  if (LocalFrame* frame = Box().GetFrame()) {
    if (FrameView* frame_view = frame->View())
      frame_view->RemoveResizerArea(Box());
  }

  Box()
      .GetDocument()
      .GetPage()
      ->GlobalRootScrollerController()
      .DidDisposeScrollableArea(*this);

  scrollbar_manager_.Dispose();

  if (scroll_corner_)
    scroll_corner_->Destroy();
  if (resizer_)
    resizer_->Destroy();

  ClearScrollableArea();

  // Note: it is not safe to call ScrollAnchor::clear if the document is being
  // destroyed, because LayoutObjectChildList::removeChildNode skips the call to
  // willBeRemovedFromTree,
  // leaving the ScrollAnchor with a stale LayoutObject pointer.
  if (RuntimeEnabledFeatures::scrollAnchoringEnabled() &&
      !Box().DocumentBeingDestroyed())
    scroll_anchor_.ClearSelf();

#if DCHECK_IS_ON()
  has_been_disposed_ = true;
#endif
}

DEFINE_TRACE(PaintLayerScrollableArea) {
  visitor->Trace(scrollbar_manager_);
  visitor->Trace(scroll_anchor_);
  ScrollableArea::Trace(visitor);
}

PlatformChromeClient* PaintLayerScrollableArea::GetChromeClient() const {
  if (Page* page = Box().GetFrame()->GetPage())
    return &page->GetChromeClient();
  return nullptr;
}

GraphicsLayer* PaintLayerScrollableArea::LayerForScrolling() const {
  return Layer()->HasCompositedLayerMapping()
             ? Layer()->GetCompositedLayerMapping()->ScrollingContentsLayer()
             : 0;
}

GraphicsLayer* PaintLayerScrollableArea::LayerForHorizontalScrollbar() const {
  // See crbug.com/343132.
  DisableCompositingQueryAsserts disabler;

  return Layer()->HasCompositedLayerMapping()
             ? Layer()
                   ->GetCompositedLayerMapping()
                   ->LayerForHorizontalScrollbar()
             : 0;
}

GraphicsLayer* PaintLayerScrollableArea::LayerForVerticalScrollbar() const {
  // See crbug.com/343132.
  DisableCompositingQueryAsserts disabler;

  return Layer()->HasCompositedLayerMapping()
             ? Layer()->GetCompositedLayerMapping()->LayerForVerticalScrollbar()
             : 0;
}

GraphicsLayer* PaintLayerScrollableArea::LayerForScrollCorner() const {
  // See crbug.com/343132.
  DisableCompositingQueryAsserts disabler;

  return Layer()->HasCompositedLayerMapping()
             ? Layer()->GetCompositedLayerMapping()->LayerForScrollCorner()
             : 0;
}

bool PaintLayerScrollableArea::ShouldUseIntegerScrollOffset() const {
  Frame* frame = Box().GetFrame();
  if (frame->GetSettings() &&
      !frame->GetSettings()->GetPreferCompositingToLCDTextEnabled())
    return true;

  return ScrollableArea::ShouldUseIntegerScrollOffset();
}

bool PaintLayerScrollableArea::IsActive() const {
  Page* page = Box().GetFrame()->GetPage();
  return page && page->GetFocusController().IsActive();
}

bool PaintLayerScrollableArea::IsScrollCornerVisible() const {
  return !ScrollCornerRect().IsEmpty();
}

static int CornerStart(const LayoutBox& box,
                       int min_x,
                       int max_x,
                       int thickness) {
  if (box.ShouldPlaceBlockDirectionScrollbarOnLogicalLeft())
    return min_x + box.StyleRef().BorderLeftWidth();
  return max_x - thickness - box.StyleRef().BorderRightWidth();
}

static IntRect CornerRect(const LayoutBox& box,
                          const Scrollbar* horizontal_scrollbar,
                          const Scrollbar* vertical_scrollbar,
                          const IntRect& bounds) {
  int horizontal_thickness;
  int vertical_thickness;
  if (!vertical_scrollbar && !horizontal_scrollbar) {
    // FIXME: This isn't right. We need to know the thickness of custom
    // scrollbars even when they don't exist in order to set the resizer square
    // size properly.
    horizontal_thickness = ScrollbarTheme::GetTheme().ScrollbarThickness();
    vertical_thickness = horizontal_thickness;
  } else if (vertical_scrollbar && !horizontal_scrollbar) {
    horizontal_thickness = vertical_scrollbar->ScrollbarThickness();
    vertical_thickness = horizontal_thickness;
  } else if (horizontal_scrollbar && !vertical_scrollbar) {
    vertical_thickness = horizontal_scrollbar->ScrollbarThickness();
    horizontal_thickness = vertical_thickness;
  } else {
    horizontal_thickness = vertical_scrollbar->ScrollbarThickness();
    vertical_thickness = horizontal_scrollbar->ScrollbarThickness();
  }
  return IntRect(
      CornerStart(box, bounds.X(), bounds.MaxX(), horizontal_thickness),
      bounds.MaxY() - vertical_thickness - box.StyleRef().BorderBottomWidth(),
      horizontal_thickness, vertical_thickness);
}

IntRect PaintLayerScrollableArea::ScrollCornerRect() const {
  // We have a scrollbar corner when a scrollbar is visible and not filling the
  // entire length of the box.
  // This happens when:
  // (a) A resizer is present and at least one scrollbar is present
  // (b) Both scrollbars are present.
  bool has_horizontal_bar = HorizontalScrollbar();
  bool has_vertical_bar = VerticalScrollbar();
  bool has_resizer = Box().Style()->Resize() != RESIZE_NONE;
  if ((has_horizontal_bar && has_vertical_bar) ||
      (has_resizer && (has_horizontal_bar || has_vertical_bar)))
    return CornerRect(Box(), HorizontalScrollbar(), VerticalScrollbar(),
                      Box().PixelSnappedBorderBoxRect());
  return IntRect();
}

IntRect PaintLayerScrollableArea::ConvertFromScrollbarToContainingFrameViewBase(
    const Scrollbar& scrollbar,
    const IntRect& scrollbar_rect) const {
  LayoutView* view = Box().View();
  if (!view)
    return scrollbar_rect;

  IntRect rect = scrollbar_rect;
  rect.Move(ScrollbarOffset(scrollbar));

  return view->GetFrameView()->ConvertFromLayoutItem(LayoutBoxItem(&Box()),
                                                     rect);
}

IntPoint
PaintLayerScrollableArea::ConvertFromScrollbarToContainingFrameViewBase(
    const Scrollbar& scrollbar,
    const IntPoint& scrollbar_point) const {
  LayoutView* view = Box().View();
  if (!view)
    return scrollbar_point;

  IntPoint point = scrollbar_point;
  point.Move(ScrollbarOffset(scrollbar));
  return view->GetFrameView()->ConvertFromLayoutItem(LayoutBoxItem(&Box()),
                                                     point);
}

IntPoint
PaintLayerScrollableArea::ConvertFromContainingFrameViewBaseToScrollbar(
    const Scrollbar& scrollbar,
    const IntPoint& parent_point) const {
  LayoutView* view = Box().View();
  if (!view)
    return parent_point;

  IntPoint point = view->GetFrameView()->ConvertToLayoutItem(
      LayoutBoxItem(&Box()), parent_point);

  point.Move(-ScrollbarOffset(scrollbar));
  return point;
}

IntPoint PaintLayerScrollableArea::ConvertFromRootFrame(
    const IntPoint& point_in_root_frame) const {
  LayoutView* view = Box().View();
  if (!view)
    return point_in_root_frame;

  return view->GetFrameView()->ConvertFromRootFrame(point_in_root_frame);
}

int PaintLayerScrollableArea::ScrollSize(
    ScrollbarOrientation orientation) const {
  IntSize scroll_dimensions =
      MaximumScrollOffsetInt() - MinimumScrollOffsetInt();
  return (orientation == kHorizontalScrollbar) ? scroll_dimensions.Width()
                                               : scroll_dimensions.Height();
}

void PaintLayerScrollableArea::UpdateScrollOffset(
    const ScrollOffset& new_offset,
    ScrollType scroll_type) {
  if (GetScrollOffset() == new_offset)
    return;

  scroll_offset_ = new_offset;

  LocalFrame* frame = Box().GetFrame();
  DCHECK(frame);

  FrameView* frame_view = Box().GetFrameView();

  TRACE_EVENT1("devtools.timeline", "ScrollLayer", "data",
               InspectorScrollLayerEvent::Data(&Box()));

  // FIXME(420741): Resolve circular dependency between scroll offset and
  // compositing state, and remove this disabler.
  DisableCompositingQueryAsserts disabler;

  // Update the positions of our child layers (if needed as only fixed layers
  // should be impacted by a scroll).  We don't update compositing layers,
  // because we need to do a deep update from the compositing ancestor.
  if (!frame_view->IsInPerformLayout()) {
    // If we're in the middle of layout, we'll just update layers once layout
    // has finished.
    Layer()->UpdateLayerPositionsAfterOverflowScroll();
    // Update regions, scrolling may change the clip of a particular region.
    frame_view->UpdateDocumentAnnotatedRegions();
    frame_view->SetNeedsUpdateGeometries();
    UpdateCompositingLayersAfterScroll();
  }

  const LayoutBoxModelObject& paint_invalidation_container =
      Box().ContainerForPaintInvalidation();

  FloatQuad quad_for_fake_mouse_move_event = FloatQuad(FloatRect(
      Layer()->GetLayoutObject().VisualRectIncludingCompositedScrolling(
          paint_invalidation_container)));

  quad_for_fake_mouse_move_event =
      paint_invalidation_container.LocalToAbsoluteQuad(
          quad_for_fake_mouse_move_event);
  frame->GetEventHandler().DispatchFakeMouseMoveEventSoonInQuad(
      quad_for_fake_mouse_move_event);

  if (scroll_type == kUserScroll || scroll_type == kCompositorScroll) {
    Page* page = frame->GetPage();
    if (page)
      page->GetChromeClient().ClearToolTip(*frame);
  }

  bool requires_paint_invalidation = true;

  if (Box().View()->Compositor()->InCompositingMode()) {
    bool only_scrolled_composited_layers =
        ScrollsOverflow() && Layer()->IsAllScrollingContentComposited() &&
        Box().Style()->BackgroundLayers().Attachment() !=
            kLocalBackgroundAttachment;

    if (UsesCompositedScrolling() || only_scrolled_composited_layers)
      requires_paint_invalidation = false;
  }

  // Only the root layer can overlap non-composited fixed-position elements.
  if (!requires_paint_invalidation && Layer()->IsRootLayer() &&
      frame_view->HasViewportConstrainedObjects()) {
    if (!frame_view->InvalidateViewportConstrainedObjects())
      requires_paint_invalidation = true;
  }

  // Just schedule a full paint invalidation of our object.
  // FIXME: This invalidation will be unnecessary in slimming paint phase 2.
  if (requires_paint_invalidation) {
    Box().SetShouldDoFullPaintInvalidationIncludingNonCompositingDescendants();
  }

  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled()) {
    // The scrollOffsetTranslation paint property depends on the scroll offset.
    // (see: PaintPropertyTreeBuilder.updateProperties(FrameView&,...) and
    // PaintPropertyTreeBuilder.updateScrollAndScrollTranslation).
    if (!RuntimeEnabledFeatures::rootLayerScrollingEnabled() &&
        Layer()->IsRootLayer()) {
      frame_view->SetNeedsPaintPropertyUpdate();
    } else {
      Box().SetNeedsPaintPropertyUpdate();
    }
  }

  // Schedule the scroll DOM event.
  if (Box().GetNode())
    Box().GetNode()->GetDocument().EnqueueScrollEventForNode(Box().GetNode());

  if (AXObjectCache* cache = Box().GetDocument().ExistingAXObjectCache())
    cache->HandleScrollPositionChanged(&Box());
  Box().View()->ClearHitTestCache();

  // Inform the FrameLoader of the new scroll position, so it can be restored
  // when navigating back.
  if (Layer()->IsRootLayer()) {
    frame_view->GetFrame().Loader().SaveScrollState();
    frame_view->DidChangeScrollOffset();
  }

  if (IsExplicitScrollType(scroll_type)) {
    if (scroll_type != kCompositorScroll)
      ShowOverlayScrollbars();
    frame_view->ClearFragmentAnchor();
    if (RuntimeEnabledFeatures::scrollAnchoringEnabled())
      GetScrollAnchor()->Clear();
  }
}

IntSize PaintLayerScrollableArea::ScrollOffsetInt() const {
  return FlooredIntSize(scroll_offset_);
}

ScrollOffset PaintLayerScrollableArea::GetScrollOffset() const {
  return scroll_offset_;
}

IntSize PaintLayerScrollableArea::MinimumScrollOffsetInt() const {
  return ToIntSize(-ScrollOrigin());
}

IntSize PaintLayerScrollableArea::MaximumScrollOffsetInt() const {
  if (!Box().HasOverflowClip())
    return ToIntSize(-ScrollOrigin());

  IntSize content_size = ContentsSize();
  IntSize visible_size =
      PixelSnappedIntRect(
          Box().OverflowClipRect(Box().Location(),
                                 kIgnorePlatformAndCSSOverlayScrollbarSize))
          .Size();

  Page* page = GetLayoutBox()->GetDocument().GetPage();
  DCHECK(page);
  TopDocumentRootScrollerController& controller =
      page->GlobalRootScrollerController();

  // The global root scroller should be clipped by the top FrameView rather
  // than it's overflow clipping box. This is to ensure that content exposed by
  // hiding the URL bar at the bottom of the screen is visible.
  if (this == controller.RootScrollerArea())
    visible_size = controller.RootScrollerVisibleArea();

  // TODO(skobes): We should really ASSERT that contentSize >= visibleSize
  // when we are not the root layer, but we can't because contentSize is
  // based on stale layout overflow data (http://crbug.com/576933).
  content_size = content_size.ExpandedTo(visible_size);

  return ToIntSize(-ScrollOrigin() + (content_size - visible_size));
}

IntRect PaintLayerScrollableArea::VisibleContentRect(
    IncludeScrollbarsInRect scrollbar_inclusion) const {
  int vertical_scrollbar_width = 0;
  int horizontal_scrollbar_height = 0;
  if (scrollbar_inclusion == kIncludeScrollbars) {
    vertical_scrollbar_width =
        (VerticalScrollbar() && !VerticalScrollbar()->IsOverlayScrollbar())
            ? VerticalScrollbar()->ScrollbarThickness()
            : 0;
    horizontal_scrollbar_height =
        (HorizontalScrollbar() && !HorizontalScrollbar()->IsOverlayScrollbar())
            ? HorizontalScrollbar()->ScrollbarThickness()
            : 0;
  }

  // TODO(szager): Handle fractional scroll offsets correctly.
  return IntRect(
      FlooredIntPoint(ScrollPosition()),
      IntSize(max(0, Layer()->size().Width() - vertical_scrollbar_width),
              max(0, Layer()->size().Height() - horizontal_scrollbar_height)));
}

void PaintLayerScrollableArea::VisibleSizeChanged() {
  ShowOverlayScrollbars();
}

int PaintLayerScrollableArea::VisibleHeight() const {
  return Layer()->size().Height();
}

int PaintLayerScrollableArea::VisibleWidth() const {
  return Layer()->size().Width();
}

LayoutSize PaintLayerScrollableArea::ClientSize() const {
  if (RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
    bool is_main_frame_root_layer =
        layer_.IsRootLayer() && Box().GetDocument().GetFrame()->IsMainFrame();
    if (is_main_frame_root_layer) {
      LayoutSize result(Box().GetFrameView()->GetLayoutSize());
      result -= IntSize(VerticalScrollbarWidth(), HorizontalScrollbarHeight());
      return result;
    }
  }
  return LayoutSize(Box().ClientWidth(), Box().ClientHeight());
}

IntSize PaintLayerScrollableArea::PixelSnappedClientSize() const {
  if (RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
    bool is_main_frame_root_layer =
        layer_.IsRootLayer() && Box().GetDocument().GetFrame()->IsMainFrame();
    if (is_main_frame_root_layer) {
      return ExcludeScrollbars(Box().GetFrameView()->GetLayoutSize());
    }
  }
  return IntSize(Box().PixelSnappedClientWidth(),
                 Box().PixelSnappedClientHeight());
}

IntSize PaintLayerScrollableArea::ContentsSize() const {
  return IntSize(PixelSnappedScrollWidth(), PixelSnappedScrollHeight());
}

void PaintLayerScrollableArea::ContentsResized() {
  ScrollableArea::ContentsResized();
  // Need to update the bounds of the scroll property.
  Box().SetNeedsPaintPropertyUpdate();
}

bool PaintLayerScrollableArea::IsScrollable() const {
  return ScrollsOverflow();
}

IntPoint PaintLayerScrollableArea::LastKnownMousePosition() const {
  return Box().GetFrame()
             ? Box().GetFrame()->GetEventHandler().LastKnownMousePosition()
             : IntPoint();
}

bool PaintLayerScrollableArea::ScrollAnimatorEnabled() const {
  if (Settings* settings = Box().GetFrame()->GetSettings())
    return settings->GetScrollAnimatorEnabled();
  return false;
}

bool PaintLayerScrollableArea::ShouldSuspendScrollAnimations() const {
  LayoutView* view = Box().View();
  if (!view)
    return true;
  return view->GetFrameView()->ShouldSuspendScrollAnimations();
}

void PaintLayerScrollableArea::ScrollbarVisibilityChanged() {
  UpdateScrollbarEnabledState();

  if (LayoutView* view = Box().View())
    return view->ClearHitTestCache();
}

void PaintLayerScrollableArea::ScrollbarFrameRectChanged() {
  // Size of non-overlay scrollbar affects overflow clip rect.
  if (!HasOverlayScrollbars())
    Box().SetNeedsPaintPropertyUpdate();
}

bool PaintLayerScrollableArea::ScrollbarsCanBeActive() const {
  LayoutView* view = Box().View();
  if (!view)
    return false;
  return view->GetFrameView()->ScrollbarsCanBeActive();
}

IntRect PaintLayerScrollableArea::ScrollableAreaBoundingBox() const {
  return Box().AbsoluteBoundingBoxRect(kTraverseDocumentBoundaries);
}

void PaintLayerScrollableArea::RegisterForAnimation() {
  if (LocalFrame* frame = Box().GetFrame()) {
    if (FrameView* frame_view = frame->View())
      frame_view->AddAnimatingScrollableArea(this);
  }
}

void PaintLayerScrollableArea::DeregisterForAnimation() {
  if (LocalFrame* frame = Box().GetFrame()) {
    if (FrameView* frame_view = frame->View())
      frame_view->RemoveAnimatingScrollableArea(this);
  }
}

bool PaintLayerScrollableArea::UserInputScrollable(
    ScrollbarOrientation orientation) const {
  if (Box().IsIntrinsicallyScrollable(orientation))
    return true;

  if (Box().IsLayoutView()) {
    ScrollbarMode h_mode;
    ScrollbarMode v_mode;
    ToLayoutView(Box()).CalculateScrollbarModes(h_mode, v_mode);
    if (orientation == kHorizontalScrollbar && h_mode == kScrollbarAlwaysOff)
      return false;
    if (orientation == kVerticalScrollbar && v_mode == kScrollbarAlwaysOff)
      return false;
  }

  EOverflow overflow_style = (orientation == kHorizontalScrollbar)
                                 ? Box().Style()->OverflowX()
                                 : Box().Style()->OverflowY();
  return (overflow_style == EOverflow::kScroll ||
          overflow_style == EOverflow::kAuto ||
          overflow_style == EOverflow::kOverlay);
}

bool PaintLayerScrollableArea::ShouldPlaceVerticalScrollbarOnLeft() const {
  return Box().ShouldPlaceBlockDirectionScrollbarOnLogicalLeft();
}

int PaintLayerScrollableArea::PageStep(ScrollbarOrientation orientation) const {
  int length = (orientation == kHorizontalScrollbar)
                   ? Box().PixelSnappedClientWidth()
                   : Box().PixelSnappedClientHeight();
  int min_page_step = static_cast<float>(length) *
                      ScrollableArea::MinFractionToStepWhenPaging();
  int page_step =
      max(min_page_step, length - ScrollableArea::MaxOverlapBetweenPages());

  return max(page_step, 1);
}

LayoutBox& PaintLayerScrollableArea::Box() const {
  return *layer_.GetLayoutBox();
}

PaintLayer* PaintLayerScrollableArea::Layer() const {
  return &layer_;
}

LayoutUnit PaintLayerScrollableArea::ScrollWidth() const {
  return overflow_rect_.Width();
}

LayoutUnit PaintLayerScrollableArea::ScrollHeight() const {
  return overflow_rect_.Height();
}

int PaintLayerScrollableArea::PixelSnappedScrollWidth() const {
  return SnapSizeToPixel(ScrollWidth(),
                         Box().ClientLeft() + Box().Location().X());
}

int PaintLayerScrollableArea::PixelSnappedScrollHeight() const {
  return SnapSizeToPixel(ScrollHeight(),
                         Box().ClientTop() + Box().Location().Y());
}

void PaintLayerScrollableArea::UpdateScrollOrigin() {
  // This should do nothing prior to first layout; the if-clause will catch
  // that.
  if (OverflowRect().IsEmpty())
    return;
  LayoutPoint scrollable_overflow =
      overflow_rect_.Location() -
      LayoutSize(Box().BorderLeft(), Box().BorderTop());
  SetScrollOrigin(FlooredIntPoint(-scrollable_overflow) +
                  Box().OriginAdjustmentForScrollbars());
}

void PaintLayerScrollableArea::UpdateScrollDimensions() {
  if (overflow_rect_.Size() != Box().LayoutOverflowRect().Size())
    ContentsResized();
  overflow_rect_ = Box().LayoutOverflowRect();
  Box().FlipForWritingMode(overflow_rect_);
  UpdateScrollOrigin();
}

void PaintLayerScrollableArea::UpdateScrollbarEnabledState() {
  bool force_disable =
      ScrollbarTheme::GetTheme().ShouldDisableInvisibleScrollbars() &&
      ScrollbarsHidden();

  if (HorizontalScrollbar())
    HorizontalScrollbar()->SetEnabled(HasHorizontalOverflow() &&
                                      !force_disable);
  if (VerticalScrollbar())
    VerticalScrollbar()->SetEnabled(HasVerticalOverflow() && !force_disable);
}

void PaintLayerScrollableArea::SetScrollOffsetUnconditionally(
    const ScrollOffset& offset,
    ScrollType scroll_type) {
  CancelScrollAnimation();
  ScrollOffsetChanged(offset, scroll_type);
}

void PaintLayerScrollableArea::UpdateAfterLayout() {
  DCHECK(Box().HasOverflowClip());

  bool relayout_is_prevented = PreventRelayoutScope::RelayoutIsPrevented();
  bool scrollbars_are_frozen =
      in_overflow_relayout_ || FreezeScrollbarsScope::ScrollbarsAreFrozen();

  if (NeedsScrollbarReconstruction()) {
    SetHasHorizontalScrollbar(false);
    SetHasVerticalScrollbar(false);
  }

  UpdateScrollDimensions();

  bool had_horizontal_scrollbar = HasHorizontalScrollbar();
  bool had_vertical_scrollbar = HasVerticalScrollbar();

  bool needs_horizontal_scrollbar;
  bool needs_vertical_scrollbar;
  ComputeScrollbarExistence(needs_horizontal_scrollbar,
                            needs_vertical_scrollbar);

  bool horizontal_scrollbar_should_change =
      needs_horizontal_scrollbar != had_horizontal_scrollbar;
  bool vertical_scrollbar_should_change =
      needs_vertical_scrollbar != had_vertical_scrollbar;

  bool scrollbars_will_change =
      !scrollbars_are_frozen &&
      (horizontal_scrollbar_should_change || vertical_scrollbar_should_change);
  if (scrollbars_will_change) {
    SetHasHorizontalScrollbar(needs_horizontal_scrollbar);
    SetHasVerticalScrollbar(needs_vertical_scrollbar);

    if (HasScrollbar())
      UpdateScrollCornerStyle();

    Layer()->UpdateSelfPaintingLayer();

    // Force an update since we know the scrollbars have changed things.
    if (Box().GetDocument().HasAnnotatedRegions())
      Box().GetDocument().SetAnnotatedRegionsDirty(true);

    // Our proprietary overflow: overlay value doesn't trigger a layout.
    if ((horizontal_scrollbar_should_change &&
         Box().Style()->OverflowX() != EOverflow::kOverlay) ||
        (vertical_scrollbar_should_change &&
         Box().Style()->OverflowY() != EOverflow::kOverlay)) {
      if ((vertical_scrollbar_should_change &&
           Box().IsHorizontalWritingMode()) ||
          (horizontal_scrollbar_should_change &&
           !Box().IsHorizontalWritingMode())) {
        Box().SetPreferredLogicalWidthsDirty();
      }
      if (relayout_is_prevented) {
        // We're not doing re-layout right now, but we still want to
        // add the scrollbar to the logical width now, to facilitate parent
        // layout.
        Box().UpdateLogicalWidth();
        PreventRelayoutScope::SetBoxNeedsLayout(*this, had_horizontal_scrollbar,
                                                had_vertical_scrollbar);
      } else {
        in_overflow_relayout_ = true;
        SubtreeLayoutScope layout_scope(Box());
        layout_scope.SetNeedsLayout(
            &Box(), LayoutInvalidationReason::kScrollbarChanged);
        if (Box().IsLayoutBlock()) {
          LayoutBlock& block = ToLayoutBlock(Box());
          block.ScrollbarsChanged(horizontal_scrollbar_should_change,
                                  vertical_scrollbar_should_change);
          block.UpdateBlockLayout(true);
        } else {
          Box().UpdateLayout();
        }
        in_overflow_relayout_ = false;
        scrollbar_manager_.DestroyDetachedScrollbars();
      }
      LayoutObject* parent = Box().Parent();
      if (parent && parent->IsFlexibleBox())
        ToLayoutFlexibleBox(parent)->ClearCachedMainSizeForChild(Box());
    }
  }

  {
    // Hits in
    // compositing/overflow/automatically-opt-into-composited-scrolling-after-style-change.html.
    DisableCompositingQueryAsserts disabler;

    UpdateScrollbarEnabledState();

    // Set up the range (and page step/line step).
    if (Scrollbar* horizontal_scrollbar = this->HorizontalScrollbar()) {
      int client_width = PixelSnappedClientSize().Width();
      horizontal_scrollbar->SetProportion(client_width,
                                          OverflowRect().Width().ToInt());
    }
    if (Scrollbar* vertical_scrollbar = this->VerticalScrollbar()) {
      int client_height = PixelSnappedClientSize().Height();
      vertical_scrollbar->SetProportion(client_height,
                                        OverflowRect().Height().ToInt());
    }
  }

  if (!scrollbars_are_frozen && HasOverlayScrollbars()) {
    if (!ScrollSize(kHorizontalScrollbar))
      SetHasHorizontalScrollbar(false);
    if (!ScrollSize(kVerticalScrollbar))
      SetHasVerticalScrollbar(false);
  }

  ClampScrollOffsetAfterOverflowChange();

  if (!scrollbars_are_frozen) {
    UpdateScrollableAreaSet(HasScrollableHorizontalOverflow() ||
                            HasScrollableVerticalOverflow());
  }

  DisableCompositingQueryAsserts disabler;
  PositionOverflowControls();
}

void PaintLayerScrollableArea::ClampScrollOffsetAfterOverflowChange() {
  // If a vertical scrollbar was removed, the min/max scroll offsets may have
  // changed, so the scroll offsets needs to be clamped.  If the scroll offset
  // did not change, but the scroll origin *did* change, we still need to notify
  // the scrollbars to update their dimensions.

  if (DelayScrollOffsetClampScope::ClampingIsDelayed()) {
    DelayScrollOffsetClampScope::SetNeedsClamp(this);
    return;
  }

  if (ScrollOriginChanged())
    SetScrollOffsetUnconditionally(ClampScrollOffset(GetScrollOffset()));
  else
    ScrollableArea::SetScrollOffset(GetScrollOffset(), kClampingScroll);

  SetNeedsScrollOffsetClamp(false);
  ResetScrollOriginChanged();
  scrollbar_manager_.DestroyDetachedScrollbars();
}

void PaintLayerScrollableArea::DidChangeGlobalRootScroller() {
  // Being the global root scroller will affect clipping size due to browser
  // controls behavior so we need to update compositing based on updated clip
  // geometry.
  if (Box().GetNode()->IsElementNode()) {
    ToElement(Box().GetNode())->SetNeedsCompositingUpdate();
    if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled())
      Box().SetNeedsPaintPropertyUpdate();
  }

  // On Android, where the VisualViewport supplies scrollbars, we need to
  // remove the PLSA's scrollbars if we become the global root scroller.
  // In general, this would be problematic as that can cause layout but this
  // should only ever apply with overlay scrollbars.
  if (Box().GetFrame()->GetSettings() &&
      Box().GetFrame()->GetSettings()->GetViewportEnabled()) {
    bool needs_horizontal_scrollbar;
    bool needs_vertical_scrollbar;
    ComputeScrollbarExistence(needs_horizontal_scrollbar,
                              needs_vertical_scrollbar);
    SetHasHorizontalScrollbar(needs_horizontal_scrollbar);
    SetHasVerticalScrollbar(needs_vertical_scrollbar);
  }
}

bool PaintLayerScrollableArea::ShouldPerformScrollAnchoring() const {
  return RuntimeEnabledFeatures::scrollAnchoringEnabled() &&
         scroll_anchor_.HasScroller() &&
         GetLayoutBox()->Style()->OverflowAnchor() != EOverflowAnchor::kNone &&
         !Box().GetDocument().FinishingOrIsPrinting();
}

FloatQuad PaintLayerScrollableArea::LocalToVisibleContentQuad(
    const FloatQuad& quad,
    const LayoutObject* local_object,
    MapCoordinatesFlags flags) const {
  LayoutBox* box = GetLayoutBox();
  if (!box)
    return quad;
  DCHECK(local_object);
  return local_object->LocalToAncestorQuad(quad, box, flags);
}

RefPtr<WebTaskRunner> PaintLayerScrollableArea::GetTimerTaskRunner() const {
  return TaskRunnerHelper::Get(TaskType::kUnspecedTimer, Box().GetFrame());
}

ScrollBehavior PaintLayerScrollableArea::ScrollBehaviorStyle() const {
  return Box().Style()->GetScrollBehavior();
}

bool PaintLayerScrollableArea::HasHorizontalOverflow() const {
  // TODO(szager): Make the algorithm for adding/subtracting overflow:auto
  // scrollbars memoryless (crbug.com/625300).  This clientWidth hack will
  // prevent the spurious horizontal scrollbar, but it can cause a converse
  // problem: it can leave a sliver of horizontal overflow hidden behind the
  // vertical scrollbar without creating a horizontal scrollbar.  This
  // converse problem seems to happen much less frequently in practice, so we
  // bias the logic towards preventing unwanted horizontal scrollbars, which
  // are more common and annoying.
  int client_width = PixelSnappedClientSize().Width();
  if (NeedsRelayout() && !HadVerticalScrollbarBeforeRelayout())
    client_width += VerticalScrollbarWidth();
  return PixelSnappedScrollWidth() > client_width;
}

bool PaintLayerScrollableArea::HasVerticalOverflow() const {
  return PixelSnappedScrollHeight() > PixelSnappedClientSize().Height();
}

bool PaintLayerScrollableArea::HasScrollableHorizontalOverflow() const {
  return HasHorizontalOverflow() && Box().ScrollsOverflowX();
}

bool PaintLayerScrollableArea::HasScrollableVerticalOverflow() const {
  return HasVerticalOverflow() && Box().ScrollsOverflowY();
}

// This function returns true if the given box requires overflow scrollbars (as
// opposed to the 'viewport' scrollbars managed by the PaintLayerCompositor).
// FIXME: we should use the same scrolling machinery for both the viewport and
// overflow. Currently, we need to avoid producing scrollbars here if they'll be
// handled externally in the RLC.
static bool CanHaveOverflowScrollbars(const LayoutBox& box) {
  return (RuntimeEnabledFeatures::rootLayerScrollingEnabled() ||
          !box.IsLayoutView()) &&
         box.GetDocument().ViewportDefiningElement() != box.GetNode();
}

void PaintLayerScrollableArea::UpdateAfterStyleChange(
    const ComputedStyle* old_style) {
  // Don't do this on first style recalc, before layout has ever happened.
  if (!OverflowRect().Size().IsZero()) {
    UpdateScrollableAreaSet(HasScrollableHorizontalOverflow() ||
                            HasScrollableVerticalOverflow());
  }

  // Whenever background changes on the scrollable element, the scroll bar
  // overlay style might need to be changed to have contrast against the
  // background.
  // Skip the need scrollbar check, because we dont know do we need a scrollbar
  // when this method get called.
  Color old_background;
  if (old_style) {
    old_background =
        old_style->VisitedDependentColor(CSSPropertyBackgroundColor);
  }
  Color new_background =
      Box().Style()->VisitedDependentColor(CSSPropertyBackgroundColor);

  if (new_background != old_background) {
    RecalculateScrollbarOverlayColorTheme(new_background);
  }

  bool needs_horizontal_scrollbar;
  bool needs_vertical_scrollbar;
  // We add auto scrollbars only during layout to prevent spurious activations.
  ComputeScrollbarExistence(needs_horizontal_scrollbar,
                            needs_vertical_scrollbar, kForbidAddingAutoBars);

  // Avoid some unnecessary computation if there were and will be no scrollbars.
  if (!HasScrollbar() && !needs_horizontal_scrollbar &&
      !needs_vertical_scrollbar)
    return;

  bool horizontal_scrollbar_changed =
      SetHasHorizontalScrollbar(needs_horizontal_scrollbar);
  bool vertical_scrollbar_changed =
      SetHasVerticalScrollbar(needs_vertical_scrollbar);

  if (Box().IsLayoutBlock() &&
      (horizontal_scrollbar_changed || vertical_scrollbar_changed)) {
    ToLayoutBlock(Box()).ScrollbarsChanged(
        horizontal_scrollbar_changed, vertical_scrollbar_changed,
        LayoutBlock::ScrollbarChangeContext::kStyleChange);
  }

  // With overflow: scroll, scrollbars are always visible but may be disabled.
  // When switching to another value, we need to re-enable them (see bug 11985).
  if (HasHorizontalScrollbar() && old_style &&
      old_style->OverflowX() == EOverflow::kScroll &&
      Box().Style()->OverflowX() != EOverflow::kScroll) {
    HorizontalScrollbar()->SetEnabled(true);
  }

  if (HasVerticalScrollbar() && old_style &&
      old_style->OverflowY() == EOverflow::kScroll &&
      Box().Style()->OverflowY() != EOverflow::kScroll) {
    VerticalScrollbar()->SetEnabled(true);
  }

  // FIXME: Need to detect a swap from custom to native scrollbars (and vice
  // versa).
  if (HorizontalScrollbar())
    HorizontalScrollbar()->StyleChanged();
  if (VerticalScrollbar())
    VerticalScrollbar()->StyleChanged();

  UpdateScrollCornerStyle();
  UpdateResizerAreaSet();
  UpdateResizerStyle();
}

bool PaintLayerScrollableArea::UpdateAfterCompositingChange() {
  Layer()->UpdateScrollingStateAfterCompositingChange();
  const bool layers_changed =
      topmost_scroll_child_ != next_topmost_scroll_child_;
  topmost_scroll_child_ = next_topmost_scroll_child_;
  next_topmost_scroll_child_ = nullptr;
  return layers_changed;
}

void PaintLayerScrollableArea::UpdateAfterOverflowRecalc() {
  UpdateScrollDimensions();
  if (Scrollbar* horizontal_scrollbar = this->HorizontalScrollbar()) {
    int client_width = PixelSnappedClientSize().Width();
    horizontal_scrollbar->SetProportion(client_width,
                                        OverflowRect().Width().ToInt());
  }
  if (Scrollbar* vertical_scrollbar = this->VerticalScrollbar()) {
    int client_height = PixelSnappedClientSize().Height();
    vertical_scrollbar->SetProportion(client_height,
                                      OverflowRect().Height().ToInt());
  }

  bool needs_horizontal_scrollbar;
  bool needs_vertical_scrollbar;
  ComputeScrollbarExistence(needs_horizontal_scrollbar,
                            needs_vertical_scrollbar);

  bool horizontal_scrollbar_should_change =
      needs_horizontal_scrollbar != HasHorizontalScrollbar();
  bool vertical_scrollbar_should_change =
      needs_vertical_scrollbar != HasVerticalScrollbar();

  if ((Box().HasAutoHorizontalScrollbar() &&
       horizontal_scrollbar_should_change) ||
      (Box().HasAutoVerticalScrollbar() && vertical_scrollbar_should_change)) {
    Box().SetNeedsLayoutAndFullPaintInvalidation(
        LayoutInvalidationReason::kUnknown);
  }

  ClampScrollOffsetAfterOverflowChange();
}

IntRect PaintLayerScrollableArea::RectForHorizontalScrollbar(
    const IntRect& border_box_rect) const {
  if (!HasHorizontalScrollbar())
    return IntRect();

  const IntRect& scroll_corner = ScrollCornerRect();

  return IntRect(HorizontalScrollbarStart(border_box_rect.X()),
                 border_box_rect.MaxY() - Box().BorderBottom().ToInt() -
                     HorizontalScrollbar()->ScrollbarThickness(),
                 border_box_rect.Width() -
                     (Box().BorderLeft() + Box().BorderRight()).ToInt() -
                     scroll_corner.Width(),
                 HorizontalScrollbar()->ScrollbarThickness());
}

IntRect PaintLayerScrollableArea::RectForVerticalScrollbar(
    const IntRect& border_box_rect) const {
  if (!HasVerticalScrollbar())
    return IntRect();

  const IntRect& scroll_corner = ScrollCornerRect();

  return IntRect(
      VerticalScrollbarStart(border_box_rect.X(), border_box_rect.MaxX()),
      border_box_rect.Y() + Box().BorderTop().ToInt(),
      VerticalScrollbar()->ScrollbarThickness(),
      border_box_rect.Height() -
          (Box().BorderTop() + Box().BorderBottom()).ToInt() -
          scroll_corner.Height());
}

int PaintLayerScrollableArea::VerticalScrollbarStart(int min_x,
                                                     int max_x) const {
  if (Box().ShouldPlaceBlockDirectionScrollbarOnLogicalLeft())
    return min_x + Box().BorderLeft().ToInt();
  return max_x - Box().BorderRight().ToInt() -
         VerticalScrollbar()->ScrollbarThickness();
}

int PaintLayerScrollableArea::HorizontalScrollbarStart(int min_x) const {
  int x = min_x + Box().BorderLeft().ToInt();
  if (Box().ShouldPlaceBlockDirectionScrollbarOnLogicalLeft())
    x += HasVerticalScrollbar()
             ? VerticalScrollbar()->ScrollbarThickness()
             : ResizerCornerRect(Box().PixelSnappedBorderBoxRect(),
                                 kResizerForPointer)
                   .Width();
  return x;
}

IntSize PaintLayerScrollableArea::ScrollbarOffset(
    const Scrollbar& scrollbar) const {
  if (&scrollbar == VerticalScrollbar()) {
    return IntSize(VerticalScrollbarStart(0, Box().Size().Width().ToInt()),
                   Box().BorderTop().ToInt());
  }

  if (&scrollbar == HorizontalScrollbar())
    return IntSize(
        HorizontalScrollbarStart(0),
        (Box().Size().Height() - Box().BorderBottom() - scrollbar.Height())
            .ToInt());

  NOTREACHED();
  return IntSize();
}

static inline const LayoutObject& ScrollbarStyleSource(
    const LayoutObject& layout_object) {
  if (Node* node = layout_object.GetNode()) {
    if (layout_object.IsLayoutView()) {
      Document& doc = node->GetDocument();
      if (Settings* settings = doc.GetSettings()) {
        if (!settings->GetAllowCustomScrollbarInMainFrame() &&
            layout_object.GetFrame() && layout_object.GetFrame()->IsMainFrame())
          return layout_object;
      }

      // Try the <body> element first as a scrollbar source.
      Element* body = doc.body();
      if (body && body->GetLayoutObject() &&
          body->GetLayoutObject()->Style()->HasPseudoStyle(kPseudoIdScrollbar))
        return *body->GetLayoutObject();

      // If the <body> didn't have a custom style, then the root element might.
      Element* doc_element = doc.documentElement();
      if (doc_element && doc_element->GetLayoutObject() &&
          doc_element->GetLayoutObject()->Style()->HasPseudoStyle(
              kPseudoIdScrollbar))
        return *doc_element->GetLayoutObject();
    }

    if (layout_object.StyleRef().HasPseudoStyle(kPseudoIdScrollbar))
      return layout_object;

    if (ShadowRoot* shadow_root = node->ContainingShadowRoot()) {
      if (shadow_root->GetType() == ShadowRootType::kUserAgent) {
        if (LayoutObject* host_layout_object =
                shadow_root->host().GetLayoutObject())
          return *host_layout_object;
      }
    }
  }

  return layout_object;
}

bool PaintLayerScrollableArea::NeedsScrollbarReconstruction() const {
  const LayoutObject& style_source = ScrollbarStyleSource(Box());
  bool should_use_custom =
      style_source.IsBox() &&
      style_source.StyleRef().HasPseudoStyle(kPseudoIdScrollbar);
  bool has_any_scrollbar = HasScrollbar();
  bool has_custom =
      (HasHorizontalScrollbar() &&
       HorizontalScrollbar()->IsCustomScrollbar()) ||
      (HasVerticalScrollbar() && VerticalScrollbar()->IsCustomScrollbar());
  bool did_custom_scrollbar_owner_changed = false;

  if (HasHorizontalScrollbar() && HorizontalScrollbar()->IsCustomScrollbar()) {
    if (style_source != ToLayoutScrollbar(HorizontalScrollbar())->StyleSource())
      did_custom_scrollbar_owner_changed = true;
  }

  if (HasVerticalScrollbar() && VerticalScrollbar()->IsCustomScrollbar()) {
    if (style_source != ToLayoutScrollbar(VerticalScrollbar())->StyleSource())
      did_custom_scrollbar_owner_changed = true;
  }

  return has_any_scrollbar &&
         ((should_use_custom != has_custom) ||
          (should_use_custom && did_custom_scrollbar_owner_changed));
}

void PaintLayerScrollableArea::ComputeScrollbarExistence(
    bool& needs_horizontal_scrollbar,
    bool& needs_vertical_scrollbar,
    ComputeScrollbarExistenceOption option) const {
  // Scrollbars may be hidden or provided by visual viewport or frame instead.
  DCHECK(Box().GetFrame()->GetSettings());
  if (VisualViewportSuppliesScrollbars() || !CanHaveOverflowScrollbars(Box()) ||
      Box().GetFrame()->GetSettings()->GetHideScrollbars()) {
    needs_horizontal_scrollbar = false;
    needs_vertical_scrollbar = false;
    return;
  }

  needs_horizontal_scrollbar = Box().ScrollsOverflowX();
  needs_vertical_scrollbar = Box().ScrollsOverflowY();

  // Don't add auto scrollbars if the box contents aren't visible.
  if (Box().HasAutoHorizontalScrollbar()) {
    if (option == kForbidAddingAutoBars)
      needs_horizontal_scrollbar &= HasHorizontalScrollbar();
    needs_horizontal_scrollbar &=
        Box().IsRooted() && this->HasHorizontalOverflow() &&
        Box().PixelSnappedClientHeight() + Box().HorizontalScrollbarHeight() >
            0;
  }

  if (Box().HasAutoVerticalScrollbar()) {
    if (option == kForbidAddingAutoBars)
      needs_vertical_scrollbar &= HasVerticalScrollbar();
    needs_vertical_scrollbar &=
        Box().IsRooted() && this->HasVerticalOverflow() &&
        Box().PixelSnappedClientWidth() + Box().VerticalScrollbarWidth() > 0;
  }

  // Look for the scrollbarModes and reset the needs Horizontal & vertical
  // Scrollbar values based on scrollbarModes, as during force style change
  // StyleResolver::styleForDocument returns documentStyle with no overflow
  // values, due to which we are destroying the scrollbars that were already
  // present.
  if (Box().IsLayoutView()) {
    ScrollbarMode h_mode;
    ScrollbarMode v_mode;
    ToLayoutView(Box()).CalculateScrollbarModes(h_mode, v_mode);
    if (h_mode == kScrollbarAlwaysOn)
      needs_horizontal_scrollbar = true;
    else if (h_mode == kScrollbarAlwaysOff)
      needs_horizontal_scrollbar = false;
    if (v_mode == kScrollbarAlwaysOn)
      needs_vertical_scrollbar = true;
    else if (v_mode == kScrollbarAlwaysOff)
      needs_vertical_scrollbar = false;
  }
}

bool PaintLayerScrollableArea::SetHasHorizontalScrollbar(bool has_scrollbar) {
  if (FreezeScrollbarsScope::ScrollbarsAreFrozen())
    return false;

  if (has_scrollbar == HasHorizontalScrollbar())
    return false;

  SetScrollbarNeedsPaintInvalidation(kHorizontalScrollbar);

  scrollbar_manager_.SetHasHorizontalScrollbar(has_scrollbar);

  UpdateScrollOrigin();

  // Destroying or creating one bar can cause our scrollbar corner to come and
  // go. We need to update the opposite scrollbar's style.
  if (HasHorizontalScrollbar())
    HorizontalScrollbar()->StyleChanged();
  if (HasVerticalScrollbar())
    VerticalScrollbar()->StyleChanged();

  SetScrollCornerNeedsPaintInvalidation();

  // Force an update since we know the scrollbars have changed things.
  if (Box().GetDocument().HasAnnotatedRegions())
    Box().GetDocument().SetAnnotatedRegionsDirty(true);
  return true;
}

bool PaintLayerScrollableArea::SetHasVerticalScrollbar(bool has_scrollbar) {
  if (FreezeScrollbarsScope::ScrollbarsAreFrozen())
    return false;

  if (has_scrollbar == HasVerticalScrollbar())
    return false;

  SetScrollbarNeedsPaintInvalidation(kVerticalScrollbar);

  scrollbar_manager_.SetHasVerticalScrollbar(has_scrollbar);

  UpdateScrollOrigin();

  // Destroying or creating one bar can cause our scrollbar corner to come and
  // go. We need to update the opposite scrollbar's style.
  if (HasHorizontalScrollbar())
    HorizontalScrollbar()->StyleChanged();
  if (HasVerticalScrollbar())
    VerticalScrollbar()->StyleChanged();

  SetScrollCornerNeedsPaintInvalidation();

  // Force an update since we know the scrollbars have changed things.
  if (Box().GetDocument().HasAnnotatedRegions())
    Box().GetDocument().SetAnnotatedRegionsDirty(true);
  return true;
}

int PaintLayerScrollableArea::VerticalScrollbarWidth(
    OverlayScrollbarClipBehavior overlay_scrollbar_clip_behavior) const {
  if (!HasVerticalScrollbar())
    return 0;
  if (overlay_scrollbar_clip_behavior ==
          kIgnorePlatformAndCSSOverlayScrollbarSize &&
      Box().Style()->OverflowY() == EOverflow::kOverlay) {
    return 0;
  }
  if ((overlay_scrollbar_clip_behavior == kIgnorePlatformOverlayScrollbarSize ||
       overlay_scrollbar_clip_behavior ==
           kIgnorePlatformAndCSSOverlayScrollbarSize ||
       !VerticalScrollbar()->ShouldParticipateInHitTesting()) &&
      VerticalScrollbar()->IsOverlayScrollbar()) {
    return 0;
  }
  return VerticalScrollbar()->ScrollbarThickness();
}

int PaintLayerScrollableArea::HorizontalScrollbarHeight(
    OverlayScrollbarClipBehavior overlay_scrollbar_clip_behavior) const {
  if (!HasHorizontalScrollbar())
    return 0;
  if (overlay_scrollbar_clip_behavior ==
          kIgnorePlatformAndCSSOverlayScrollbarSize &&
      Box().Style()->OverflowX() == EOverflow::kOverlay) {
    return 0;
  }
  if ((overlay_scrollbar_clip_behavior == kIgnorePlatformOverlayScrollbarSize ||
       overlay_scrollbar_clip_behavior ==
           kIgnorePlatformAndCSSOverlayScrollbarSize ||
       !HorizontalScrollbar()->ShouldParticipateInHitTesting()) &&
      HorizontalScrollbar()->IsOverlayScrollbar()) {
    return 0;
  }
  return HorizontalScrollbar()->ScrollbarThickness();
}

void PaintLayerScrollableArea::PositionOverflowControls() {
  if (!HasScrollbar() && !Box().CanResize())
    return;

  const IntRect border_box = Box().PixelSnappedBorderBoxRect();
  if (Scrollbar* vertical_scrollbar = this->VerticalScrollbar())
    vertical_scrollbar->SetFrameRect(RectForVerticalScrollbar(border_box));

  if (Scrollbar* horizontal_scrollbar = this->HorizontalScrollbar())
    horizontal_scrollbar->SetFrameRect(RectForHorizontalScrollbar(border_box));

  const IntRect& scroll_corner = ScrollCornerRect();
  if (scroll_corner_)
    scroll_corner_->SetFrameRect(LayoutRect(scroll_corner));

  if (resizer_)
    resizer_->SetFrameRect(
        LayoutRect(ResizerCornerRect(border_box, kResizerForPointer)));

  // FIXME, this should eventually be removed, once we are certain that
  // composited controls get correctly positioned on a compositor update. For
  // now, conservatively leaving this unchanged.
  if (Layer()->HasCompositedLayerMapping())
    Layer()->GetCompositedLayerMapping()->PositionOverflowControlsLayers();
}

void PaintLayerScrollableArea::UpdateScrollCornerStyle() {
  if (!scroll_corner_ && !HasScrollbar())
    return;
  if (!scroll_corner_ && HasOverlayScrollbars())
    return;

  const LayoutObject& style_source = ScrollbarStyleSource(Box());
  RefPtr<ComputedStyle> corner =
      Box().HasOverflowClip()
          ? style_source.GetUncachedPseudoStyle(
                PseudoStyleRequest(kPseudoIdScrollbarCorner),
                style_source.Style())
          : PassRefPtr<ComputedStyle>(nullptr);
  if (corner) {
    if (!scroll_corner_) {
      scroll_corner_ =
          LayoutScrollbarPart::CreateAnonymous(&Box().GetDocument(), this);
      scroll_corner_->SetDangerousOneWayParent(&Box());
    }
    scroll_corner_->SetStyleWithWritingModeOfParent(std::move(corner));
  } else if (scroll_corner_) {
    scroll_corner_->Destroy();
    scroll_corner_ = nullptr;
  }
}

bool PaintLayerScrollableArea::HitTestOverflowControls(
    HitTestResult& result,
    const IntPoint& local_point) {
  if (!HasScrollbar() && !Box().CanResize())
    return false;

  IntRect resize_control_rect;
  if (Box().Style()->Resize() != RESIZE_NONE) {
    resize_control_rect = ResizerCornerRect(Box().PixelSnappedBorderBoxRect(),
                                            kResizerForPointer);
    if (resize_control_rect.Contains(local_point))
      return true;
  }

  int resize_control_size = max(resize_control_rect.Height(), 0);
  if (HasVerticalScrollbar() &&
      VerticalScrollbar()->ShouldParticipateInHitTesting()) {
    LayoutRect v_bar_rect(
        VerticalScrollbarStart(0, Box().Size().Width().ToInt()),
        Box().BorderTop().ToInt(), VerticalScrollbar()->ScrollbarThickness(),
        Box().Size().Height().ToInt() -
            (Box().BorderTop() + Box().BorderBottom()).ToInt() -
            (HasHorizontalScrollbar()
                 ? HorizontalScrollbar()->ScrollbarThickness()
                 : resize_control_size));
    if (v_bar_rect.Contains(local_point)) {
      result.SetScrollbar(VerticalScrollbar());
      return true;
    }
  }

  resize_control_size = max(resize_control_rect.Width(), 0);
  if (HasHorizontalScrollbar() &&
      HorizontalScrollbar()->ShouldParticipateInHitTesting()) {
    // TODO(crbug.com/638981): Are the conversions to int intentional?
    LayoutRect h_bar_rect(
        HorizontalScrollbarStart(0),
        (Box().Size().Height() - Box().BorderBottom() -
         HorizontalScrollbar()->ScrollbarThickness())
            .ToInt(),
        (Box().Size().Width() - (Box().BorderLeft() + Box().BorderRight()) -
         (HasVerticalScrollbar() ? VerticalScrollbar()->ScrollbarThickness()
                                 : resize_control_size))
            .ToInt(),
        HorizontalScrollbar()->ScrollbarThickness());
    if (h_bar_rect.Contains(local_point)) {
      result.SetScrollbar(HorizontalScrollbar());
      return true;
    }
  }

  // FIXME: We should hit test the m_scrollCorner and pass it back through the
  // result.

  return false;
}

IntRect PaintLayerScrollableArea::ResizerCornerRect(
    const IntRect& bounds,
    ResizerHitTestType resizer_hit_test_type) const {
  if (Box().Style()->Resize() == RESIZE_NONE)
    return IntRect();
  IntRect corner =
      CornerRect(Box(), HorizontalScrollbar(), VerticalScrollbar(), bounds);

  if (resizer_hit_test_type == kResizerForTouch) {
    // We make the resizer virtually larger for touch hit testing. With the
    // expanding ratio k = ResizerControlExpandRatioForTouch, we first move
    // the resizer rect (of width w & height h), by (-w * (k-1), -h * (k-1)),
    // then expand the rect by new_w/h = w/h * k.
    int expand_ratio = kResizerControlExpandRatioForTouch - 1;
    corner.Move(-corner.Width() * expand_ratio,
                -corner.Height() * expand_ratio);
    corner.Expand(corner.Width() * expand_ratio,
                  corner.Height() * expand_ratio);
  }

  return corner;
}

IntRect PaintLayerScrollableArea::ScrollCornerAndResizerRect() const {
  IntRect scroll_corner_and_resizer = ScrollCornerRect();
  if (scroll_corner_and_resizer.IsEmpty())
    scroll_corner_and_resizer = ResizerCornerRect(
        Box().PixelSnappedBorderBoxRect(), kResizerForPointer);
  return scroll_corner_and_resizer;
}

bool PaintLayerScrollableArea::IsPointInResizeControl(
    const IntPoint& absolute_point,
    ResizerHitTestType resizer_hit_test_type) const {
  if (!Box().CanResize())
    return false;

  IntPoint local_point =
      RoundedIntPoint(Box().AbsoluteToLocal(absolute_point, kUseTransforms));
  IntRect local_bounds(0, 0, Box().PixelSnappedWidth(),
                       Box().PixelSnappedHeight());
  return ResizerCornerRect(local_bounds, resizer_hit_test_type)
      .Contains(local_point);
}

bool PaintLayerScrollableArea::HitTestResizerInFragments(
    const PaintLayerFragments& layer_fragments,
    const HitTestLocation& hit_test_location) const {
  if (!Box().CanResize())
    return false;

  if (layer_fragments.IsEmpty())
    return false;

  for (int i = layer_fragments.size() - 1; i >= 0; --i) {
    const PaintLayerFragment& fragment = layer_fragments.at(i);
    if (fragment.background_rect.Intersects(hit_test_location) &&
        ResizerCornerRect(PixelSnappedIntRect(fragment.layer_bounds),
                          kResizerForPointer)
            .Contains(hit_test_location.RoundedPoint()))
      return true;
  }

  return false;
}

void PaintLayerScrollableArea::UpdateResizerAreaSet() {
  LocalFrame* frame = Box().GetFrame();
  if (!frame)
    return;
  FrameView* frame_view = frame->View();
  if (!frame_view)
    return;
  if (Box().CanResize())
    frame_view->AddResizerArea(Box());
  else
    frame_view->RemoveResizerArea(Box());
}

void PaintLayerScrollableArea::UpdateResizerStyle() {
  if (!resizer_ && !Box().CanResize())
    return;

  const LayoutObject& style_source = ScrollbarStyleSource(Box());
  RefPtr<ComputedStyle> resizer =
      Box().HasOverflowClip()
          ? style_source.GetUncachedPseudoStyle(
                PseudoStyleRequest(kPseudoIdResizer), style_source.Style())
          : PassRefPtr<ComputedStyle>(nullptr);
  if (resizer) {
    if (!resizer_) {
      resizer_ =
          LayoutScrollbarPart::CreateAnonymous(&Box().GetDocument(), this);
      resizer_->SetDangerousOneWayParent(&Box());
    }
    resizer_->SetStyleWithWritingModeOfParent(std::move(resizer));
  } else if (resizer_) {
    resizer_->Destroy();
    resizer_ = nullptr;
  }
}

void PaintLayerScrollableArea::InvalidateAllStickyConstraints() {
  if (PaintLayerScrollableAreaRareData* d = RareData()) {
    for (PaintLayer* sticky_layer : d->sticky_constraints_map_.Keys()) {
      if (sticky_layer->GetLayoutObject().Style()->GetPosition() ==
          EPosition::kSticky)
        sticky_layer->SetNeedsCompositingInputsUpdate();
    }
    d->sticky_constraints_map_.clear();
  }
}

void PaintLayerScrollableArea::InvalidateStickyConstraintsFor(
    PaintLayer* layer,
    bool needs_compositing_update) {
  if (PaintLayerScrollableAreaRareData* d = RareData()) {
    d->sticky_constraints_map_.erase(layer);
    if (needs_compositing_update &&
        layer->GetLayoutObject().Style()->GetPosition() == EPosition::kSticky)
      layer->SetNeedsCompositingInputsUpdate();
  }
}

IntSize PaintLayerScrollableArea::OffsetFromResizeCorner(
    const IntPoint& absolute_point) const {
  // Currently the resize corner is either the bottom right corner or the bottom
  // left corner.
  // FIXME: This assumes the location is 0, 0. Is this guaranteed to always be
  // the case?
  IntSize element_size = Layer()->size();
  if (Box().ShouldPlaceBlockDirectionScrollbarOnLogicalLeft())
    element_size.SetWidth(0);
  IntPoint resizer_point = IntPoint(element_size);
  IntPoint local_point =
      RoundedIntPoint(Box().AbsoluteToLocal(absolute_point, kUseTransforms));
  return local_point - resizer_point;
}

void PaintLayerScrollableArea::Resize(const IntPoint& pos,
                                      const LayoutSize& old_offset) {
  // FIXME: This should be possible on generated content but is not right now.
  if (!InResizeMode() || !Box().CanResize() || !Box().GetNode())
    return;

  DCHECK(Box().GetNode()->IsElementNode());
  Element* element = ToElement(Box().GetNode());

  Document& document = element->GetDocument();

  float zoom_factor = Box().Style()->EffectiveZoom();

  IntSize new_offset =
      OffsetFromResizeCorner(document.View()->RootFrameToContents(pos));
  new_offset.SetWidth(new_offset.Width() / zoom_factor);
  new_offset.SetHeight(new_offset.Height() / zoom_factor);

  LayoutSize current_size = Box().Size();
  current_size.Scale(1 / zoom_factor);
  LayoutSize minimum_size =
      element->MinimumSizeForResizing().ShrunkTo(current_size);
  element->SetMinimumSizeForResizing(minimum_size);

  LayoutSize adjusted_old_offset = LayoutSize(
      old_offset.Width() / zoom_factor, old_offset.Height() / zoom_factor);
  if (Box().ShouldPlaceBlockDirectionScrollbarOnLogicalLeft()) {
    new_offset.SetWidth(-new_offset.Width());
    adjusted_old_offset.SetWidth(-adjusted_old_offset.Width());
  }

  LayoutSize difference((current_size + new_offset - adjusted_old_offset)
                            .ExpandedTo(minimum_size) -
                        current_size);

  bool is_box_sizing_border =
      Box().Style()->BoxSizing() == EBoxSizing::kBorderBox;

  EResize resize = Box().Style()->Resize();
  if (resize != RESIZE_VERTICAL && difference.Width()) {
    if (element->IsFormControlElement()) {
      // Make implicit margins from the theme explicit (see
      // <http://bugs.webkit.org/show_bug.cgi?id=9547>).
      element->SetInlineStyleProperty(CSSPropertyMarginLeft,
                                      Box().MarginLeft() / zoom_factor,
                                      CSSPrimitiveValue::UnitType::kPixels);
      element->SetInlineStyleProperty(CSSPropertyMarginRight,
                                      Box().MarginRight() / zoom_factor,
                                      CSSPrimitiveValue::UnitType::kPixels);
    }
    LayoutUnit base_width =
        Box().Size().Width() -
        (is_box_sizing_border ? LayoutUnit() : Box().BorderAndPaddingWidth());
    base_width = LayoutUnit(base_width / zoom_factor);
    element->SetInlineStyleProperty(CSSPropertyWidth,
                                    RoundToInt(base_width + difference.Width()),
                                    CSSPrimitiveValue::UnitType::kPixels);
  }

  if (resize != RESIZE_HORIZONTAL && difference.Height()) {
    if (element->IsFormControlElement()) {
      // Make implicit margins from the theme explicit (see
      // <http://bugs.webkit.org/show_bug.cgi?id=9547>).
      element->SetInlineStyleProperty(CSSPropertyMarginTop,
                                      Box().MarginTop() / zoom_factor,
                                      CSSPrimitiveValue::UnitType::kPixels);
      element->SetInlineStyleProperty(CSSPropertyMarginBottom,
                                      Box().MarginBottom() / zoom_factor,
                                      CSSPrimitiveValue::UnitType::kPixels);
    }
    LayoutUnit base_height =
        Box().Size().Height() -
        (is_box_sizing_border ? LayoutUnit() : Box().BorderAndPaddingHeight());
    base_height = LayoutUnit(base_height / zoom_factor);
    element->SetInlineStyleProperty(
        CSSPropertyHeight, RoundToInt(base_height + difference.Height()),
        CSSPrimitiveValue::UnitType::kPixels);
  }

  document.UpdateStyleAndLayout();

  // FIXME (Radar 4118564): We should also autoscroll the window as necessary to
  // keep the point under the cursor in view.
}

LayoutRect PaintLayerScrollableArea::ScrollIntoView(
    const LayoutRect& rect,
    const ScrollAlignment& align_x,
    const ScrollAlignment& align_y,
    ScrollType scroll_type) {
  LayoutRect local_expose_rect(
      Box()
          .AbsoluteToLocalQuad(FloatQuad(FloatRect(rect)), kUseTransforms)
          .BoundingBox());
  local_expose_rect.Move(-Box().BorderLeft(), -Box().BorderTop());
  LayoutRect visible_rect(LayoutPoint(), ClientSize());
  LayoutRect r = ScrollAlignment::GetRectToExpose(
      visible_rect, local_expose_rect, align_x, align_y);

  ScrollOffset old_scroll_offset = GetScrollOffset();
  ScrollOffset new_scroll_offset(ClampScrollOffset(RoundedIntSize(
      ToScrollOffset(FloatPoint(r.Location()) + old_scroll_offset))));
  SetScrollOffset(new_scroll_offset, scroll_type, kScrollBehaviorInstant);
  ScrollOffset scroll_offset_difference = GetScrollOffset() - old_scroll_offset;
  local_expose_rect.Move(-LayoutSize(scroll_offset_difference));

  LayoutRect intersect =
      LocalToAbsolute(Box(), Intersection(visible_rect, local_expose_rect));
  if (intersect.IsEmpty() && !visible_rect.IsEmpty() &&
      !local_expose_rect.IsEmpty()) {
    return LocalToAbsolute(Box(), local_expose_rect);
  }
  return intersect;
}

void PaintLayerScrollableArea::UpdateScrollableAreaSet(bool has_overflow) {
  LocalFrame* frame = Box().GetFrame();
  if (!frame)
    return;

  FrameView* frame_view = frame->View();
  if (!frame_view)
    return;

  bool is_visible_to_hit_test = Box().Style()->VisibleToHitTesting();
  bool did_scroll_overflow = scrolls_overflow_;
  if (Box().IsLayoutView()) {
    ScrollbarMode h_mode;
    ScrollbarMode v_mode;
    ToLayoutView(Box()).CalculateScrollbarModes(h_mode, v_mode);
    if (h_mode == kScrollbarAlwaysOff && v_mode == kScrollbarAlwaysOff)
      has_overflow = false;
  }
  scrolls_overflow_ = has_overflow && is_visible_to_hit_test;
  if (did_scroll_overflow == ScrollsOverflow())
    return;

  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled()) {
    // The scroll and scroll offset properties depend on |scrollsOverflow| (see:
    // PaintPropertyTreeBuilder::updateScrollAndScrollTranslation).
    Box().SetNeedsPaintPropertyUpdate();
  }

  if (scrolls_overflow_) {
    DCHECK(CanHaveOverflowScrollbars(Box()));
    frame_view->AddScrollableArea(this);
  } else {
    frame_view->RemoveScrollableArea(this);
  }
}

void PaintLayerScrollableArea::UpdateCompositingLayersAfterScroll() {
  PaintLayerCompositor* compositor = Box().View()->Compositor();
  if (compositor->InCompositingMode()) {
    if (UsesCompositedScrolling()) {
      DCHECK(Layer()->HasCompositedLayerMapping());
      Layer()->GetCompositedLayerMapping()->SetNeedsGraphicsLayerUpdate(
          kGraphicsLayerUpdateSubtree);
      compositor->SetNeedsCompositingUpdate(
          kCompositingUpdateAfterGeometryChange);
    } else {
      Layer()->SetNeedsCompositingInputsUpdate();
    }
  }
}

ScrollingCoordinator* PaintLayerScrollableArea::GetScrollingCoordinator()
    const {
  LocalFrame* frame = Box().GetFrame();
  if (!frame)
    return nullptr;

  Page* page = frame->GetPage();
  if (!page)
    return nullptr;

  return page->GetScrollingCoordinator();
}

bool PaintLayerScrollableArea::UsesCompositedScrolling() const {
  // See https://codereview.chromium.org/176633003/ for the tests that fail
  // without this disabler.
  DisableCompositingQueryAsserts disabler;
  return Layer()->HasCompositedLayerMapping() &&
         Layer()->GetCompositedLayerMapping()->ScrollingLayer();
}

bool PaintLayerScrollableArea::ShouldScrollOnMainThread() const {
  if (LocalFrame* frame = Box().GetFrame()) {
    if (frame->View()->GetMainThreadScrollingReasons())
      return true;
  }
  return ScrollableArea::ShouldScrollOnMainThread();
}

bool PaintLayerScrollableArea::ComputeNeedsCompositedScrolling(
    const LCDTextMode mode,
    const PaintLayer* layer) {
  non_composited_main_thread_scrolling_reasons_ = 0;
  if (!layer->ScrollsOverflow())
    return false;

  Node* node = layer->EnclosingNode();
  if (node && node->IsElementNode() &&
      (ToElement(node)->CompositorMutableProperties() &
       (CompositorMutableProperty::kScrollTop |
        CompositorMutableProperty::kScrollLeft)))
    return true;

  if (layer->size().IsEmpty())
    return false;

  bool needs_composited_scrolling = true;

  // TODO(flackr): Allow integer transforms as long as all of the ancestor
  // transforms are also integer.
  bool background_supports_lcd_text =
      RuntimeEnabledFeatures::compositeOpaqueScrollersEnabled() &&
      layer->GetLayoutObject().Style()->IsStackingContext() &&
      layer->GetBackgroundPaintLocation(
          &non_composited_main_thread_scrolling_reasons_) &
          kBackgroundPaintInScrollingContents &&
      layer->BackgroundIsKnownToBeOpaqueInRect(
          ToLayoutBox(layer->GetLayoutObject()).PaddingBoxRect()) &&
      !layer->CompositesWithTransform() && !layer->CompositesWithOpacity();

  if (mode == PaintLayerScrollableArea::kConsiderLCDText &&
      !layer->Compositor()->PreferCompositingToLCDTextEnabled() &&
      !background_supports_lcd_text) {
    if (layer->CompositesWithOpacity()) {
      non_composited_main_thread_scrolling_reasons_ |=
          MainThreadScrollingReason::kHasOpacityAndLCDText;
    }
    if (layer->CompositesWithTransform()) {
      non_composited_main_thread_scrolling_reasons_ |=
          MainThreadScrollingReason::kHasTransformAndLCDText;
    }
    if (!layer->BackgroundIsKnownToBeOpaqueInRect(
            ToLayoutBox(layer->GetLayoutObject()).PaddingBoxRect())) {
      non_composited_main_thread_scrolling_reasons_ |=
          MainThreadScrollingReason::kBackgroundNotOpaqueInRectAndLCDText;
    }
    if (!layer->GetLayoutObject().Style()->IsStackingContext()) {
      non_composited_main_thread_scrolling_reasons_ |=
          MainThreadScrollingReason::kIsNotStackingContextAndLCDText;
    }

    needs_composited_scrolling = false;
  }

  // TODO(schenney) Tests fail if we do not also exclude
  // layer->layoutObject().style()->hasBorderDecoration() (missing background
  // behind dashed borders). Resolve this case, or not, and update this check
  // with the results.
  if (layer->GetLayoutObject().Style()->HasBorderRadius()) {
    non_composited_main_thread_scrolling_reasons_ |=
        MainThreadScrollingReason::kHasBorderRadius;
    needs_composited_scrolling = false;
  }
  if (layer->GetLayoutObject().HasClip() ||
      layer->HasDescendantWithClipPath() || layer->HasAncestorWithClipPath()) {
    non_composited_main_thread_scrolling_reasons_ |=
        MainThreadScrollingReason::kHasClipRelatedProperty;
    needs_composited_scrolling = false;
  }

  DCHECK(!(non_composited_main_thread_scrolling_reasons_ &
           ~MainThreadScrollingReason::kNonCompositedReasons));
  return needs_composited_scrolling;
}

void PaintLayerScrollableArea::UpdateNeedsCompositedScrolling(
    LCDTextMode mode) {
  const bool needs_composited_scrolling =
      ComputeNeedsCompositedScrolling(mode, Layer());

  if (static_cast<bool>(needs_composited_scrolling_) !=
      needs_composited_scrolling) {
    needs_composited_scrolling_ = needs_composited_scrolling;
    Layer()->DidUpdateNeedsCompositedScrolling();
  }
}

void PaintLayerScrollableArea::SetTopmostScrollChild(PaintLayer* scroll_child) {
  // We only want to track the topmost scroll child for scrollable areas with
  // overlay scrollbars.
  if (!HasOverlayScrollbars())
    return;
  next_topmost_scroll_child_ = scroll_child;
}

bool PaintLayerScrollableArea::VisualViewportSuppliesScrollbars() const {
  LocalFrame* frame = Box().GetFrame();
  if (!frame || !frame->GetSettings())
    return false;

  // On desktop, we always use the layout viewport's scrollbars.
  if (!frame->GetSettings()->GetViewportEnabled())
    return false;

  const TopDocumentRootScrollerController& controller =
      GetLayoutBox()->GetDocument().GetPage()->GlobalRootScrollerController();

  return RootScrollerUtil::ScrollableAreaForRootScroller(
             controller.GlobalRootScroller()) == this;
}

bool PaintLayerScrollableArea::ScheduleAnimation() {
  if (PlatformChromeClient* client = GetChromeClient()) {
    client->ScheduleAnimation(Box().GetFrame()->View());
    return true;
  }
  return false;
}

void PaintLayerScrollableArea::ResetRebuildScrollbarLayerFlags() {
  rebuild_horizontal_scrollbar_layer_ = false;
  rebuild_vertical_scrollbar_layer_ = false;
}

CompositorAnimationHost* PaintLayerScrollableArea::GetCompositorAnimationHost()
    const {
  return layer_.GetLayoutObject().GetFrameView()->GetCompositorAnimationHost();
}

CompositorAnimationTimeline*
PaintLayerScrollableArea::GetCompositorAnimationTimeline() const {
  return layer_.GetLayoutObject()
      .GetFrameView()
      ->GetCompositorAnimationTimeline();
}

PaintLayerScrollableArea*
PaintLayerScrollableArea::ScrollbarManager::ScrollableArea() {
  return ToPaintLayerScrollableArea(scrollable_area_.Get());
}

void PaintLayerScrollableArea::ScrollbarManager::DestroyDetachedScrollbars() {
  DCHECK(!h_bar_is_attached_ || h_bar_);
  DCHECK(!v_bar_is_attached_ || v_bar_);
  if (h_bar_ && !h_bar_is_attached_)
    DestroyScrollbar(kHorizontalScrollbar);
  if (v_bar_ && !v_bar_is_attached_)
    DestroyScrollbar(kVerticalScrollbar);
}

void PaintLayerScrollableArea::ScrollbarManager::SetHasHorizontalScrollbar(
    bool has_scrollbar) {
  if (has_scrollbar) {
    // This doesn't hit in any tests, but since the equivalent code in
    // setHasVerticalScrollbar does, presumably this code does as well.
    DisableCompositingQueryAsserts disabler;
    if (!h_bar_) {
      h_bar_ = CreateScrollbar(kHorizontalScrollbar);
      h_bar_is_attached_ = 1;
      if (!h_bar_->IsCustomScrollbar())
        ScrollableArea()->DidAddScrollbar(*h_bar_, kHorizontalScrollbar);
    } else {
      h_bar_is_attached_ = 1;
    }
  } else {
    h_bar_is_attached_ = 0;
    if (!DelayScrollOffsetClampScope::ClampingIsDelayed())
      DestroyScrollbar(kHorizontalScrollbar);
  }
}

void PaintLayerScrollableArea::ScrollbarManager::SetHasVerticalScrollbar(
    bool has_scrollbar) {
  if (has_scrollbar) {
    DisableCompositingQueryAsserts disabler;
    if (!v_bar_) {
      v_bar_ = CreateScrollbar(kVerticalScrollbar);
      v_bar_is_attached_ = 1;
      if (!v_bar_->IsCustomScrollbar())
        ScrollableArea()->DidAddScrollbar(*v_bar_, kVerticalScrollbar);
    } else {
      v_bar_is_attached_ = 1;
    }
  } else {
    v_bar_is_attached_ = 0;
    if (!DelayScrollOffsetClampScope::ClampingIsDelayed())
      DestroyScrollbar(kVerticalScrollbar);
  }
}

Scrollbar* PaintLayerScrollableArea::ScrollbarManager::CreateScrollbar(
    ScrollbarOrientation orientation) {
  DCHECK(orientation == kHorizontalScrollbar ? !h_bar_is_attached_
                                             : !v_bar_is_attached_);
  Scrollbar* scrollbar = nullptr;
  const LayoutObject& style_source =
      ScrollbarStyleSource(ScrollableArea()->Box());
  bool has_custom_scrollbar_style =
      style_source.IsBox() &&
      style_source.StyleRef().HasPseudoStyle(kPseudoIdScrollbar);
  if (has_custom_scrollbar_style) {
    DCHECK(style_source.GetNode() && style_source.GetNode()->IsElementNode());
    scrollbar = LayoutScrollbar::CreateCustomScrollbar(
        ScrollableArea(), orientation, ToElement(style_source.GetNode()));
  } else {
    ScrollbarControlSize scrollbar_size = kRegularScrollbar;
    if (style_source.StyleRef().HasAppearance()) {
      scrollbar_size = LayoutTheme::GetTheme().ScrollbarControlSizeForPart(
          style_source.StyleRef().Appearance());
    }
    scrollbar = Scrollbar::Create(
        ScrollableArea(), orientation, scrollbar_size,
        &ScrollableArea()->Box().GetFrame()->GetPage()->GetChromeClient());
  }
  ScrollableArea()->Box().GetDocument().View()->AddScrollbar(scrollbar);
  return scrollbar;
}

void PaintLayerScrollableArea::ScrollbarManager::DestroyScrollbar(
    ScrollbarOrientation orientation) {
  Member<Scrollbar>& scrollbar =
      orientation == kHorizontalScrollbar ? h_bar_ : v_bar_;
  DCHECK(orientation == kHorizontalScrollbar ? !h_bar_is_attached_
                                             : !v_bar_is_attached_);
  if (!scrollbar)
    return;

  ScrollableArea()->SetScrollbarNeedsPaintInvalidation(orientation);
  if (orientation == kHorizontalScrollbar)
    ScrollableArea()->rebuild_horizontal_scrollbar_layer_ = true;
  else
    ScrollableArea()->rebuild_vertical_scrollbar_layer_ = true;

  if (!scrollbar->IsCustomScrollbar())
    ScrollableArea()->WillRemoveScrollbar(*scrollbar, orientation);

  ScrollableArea()->Box().GetDocument().View()->RemoveScrollbar(scrollbar);
  scrollbar->DisconnectFromScrollableArea();
  scrollbar = nullptr;
}

uint64_t PaintLayerScrollableArea::Id() const {
  return DOMNodeIds::IdForNode(Box().GetNode());
}

int PaintLayerScrollableArea::PreventRelayoutScope::count_ = 0;
SubtreeLayoutScope*
    PaintLayerScrollableArea::PreventRelayoutScope::layout_scope_ = nullptr;
bool PaintLayerScrollableArea::PreventRelayoutScope::relayout_needed_ = false;
PersistentHeapVector<Member<PaintLayerScrollableArea>>*
    PaintLayerScrollableArea::PreventRelayoutScope::needs_relayout_ = nullptr;

PaintLayerScrollableArea::PreventRelayoutScope::PreventRelayoutScope(
    SubtreeLayoutScope& layout_scope) {
  if (!count_) {
    DCHECK(!layout_scope_);
    DCHECK(!needs_relayout_ || needs_relayout_->IsEmpty());
    layout_scope_ = &layout_scope;
  }
  count_++;
}

PaintLayerScrollableArea::PreventRelayoutScope::~PreventRelayoutScope() {
  if (--count_ == 0) {
    if (relayout_needed_) {
      for (auto scrollable_area : *needs_relayout_) {
        DCHECK(scrollable_area->NeedsRelayout());
        LayoutBox& box = scrollable_area->Box();
        layout_scope_->SetNeedsLayout(
            &box, LayoutInvalidationReason::kScrollbarChanged);
        if (box.IsLayoutBlock()) {
          bool horizontal_scrollbar_changed =
              scrollable_area->HasHorizontalScrollbar() !=
              scrollable_area->HadHorizontalScrollbarBeforeRelayout();
          bool vertical_scrollbar_changed =
              scrollable_area->HasVerticalScrollbar() !=
              scrollable_area->HadVerticalScrollbarBeforeRelayout();
          if (horizontal_scrollbar_changed || vertical_scrollbar_changed)
            ToLayoutBlock(box).ScrollbarsChanged(horizontal_scrollbar_changed,
                                                 vertical_scrollbar_changed);
        }
        scrollable_area->SetNeedsRelayout(false);
      }

      needs_relayout_->clear();
    }
    layout_scope_ = nullptr;
  }
}

void PaintLayerScrollableArea::PreventRelayoutScope::SetBoxNeedsLayout(
    PaintLayerScrollableArea& scrollable_area,
    bool had_horizontal_scrollbar,
    bool had_vertical_scrollbar) {
  DCHECK(count_);
  DCHECK(layout_scope_);
  if (scrollable_area.NeedsRelayout())
    return;
  scrollable_area.SetNeedsRelayout(true);
  scrollable_area.SetHadHorizontalScrollbarBeforeRelayout(
      had_horizontal_scrollbar);
  scrollable_area.SetHadVerticalScrollbarBeforeRelayout(had_vertical_scrollbar);

  relayout_needed_ = true;
  if (!needs_relayout_)
    needs_relayout_ =
        new PersistentHeapVector<Member<PaintLayerScrollableArea>>();
  needs_relayout_->push_back(&scrollable_area);
}

void PaintLayerScrollableArea::PreventRelayoutScope::ResetRelayoutNeeded() {
  DCHECK_EQ(count_, 0);
  DCHECK(!needs_relayout_ || needs_relayout_->IsEmpty());
  relayout_needed_ = false;
}

int PaintLayerScrollableArea::FreezeScrollbarsScope::count_ = 0;

int PaintLayerScrollableArea::DelayScrollOffsetClampScope::count_ = 0;
PersistentHeapVector<Member<PaintLayerScrollableArea>>*
    PaintLayerScrollableArea::DelayScrollOffsetClampScope::needs_clamp_ =
        nullptr;

PaintLayerScrollableArea::DelayScrollOffsetClampScope::
    DelayScrollOffsetClampScope() {
  if (!needs_clamp_)
    needs_clamp_ = new PersistentHeapVector<Member<PaintLayerScrollableArea>>();
  DCHECK(count_ > 0 || needs_clamp_->IsEmpty());
  count_++;
}

PaintLayerScrollableArea::DelayScrollOffsetClampScope::
    ~DelayScrollOffsetClampScope() {
  if (--count_ == 0)
    DelayScrollOffsetClampScope::ClampScrollableAreas();
}

void PaintLayerScrollableArea::DelayScrollOffsetClampScope::SetNeedsClamp(
    PaintLayerScrollableArea* scrollable_area) {
  if (!scrollable_area->NeedsScrollOffsetClamp()) {
    scrollable_area->SetNeedsScrollOffsetClamp(true);
    needs_clamp_->push_back(scrollable_area);
  }
}

void PaintLayerScrollableArea::DelayScrollOffsetClampScope::
    ClampScrollableAreas() {
  for (auto& scrollable_area : *needs_clamp_)
    scrollable_area->ClampScrollOffsetAfterOverflowChange();
  delete needs_clamp_;
  needs_clamp_ = nullptr;
}

}  // namespace blink
