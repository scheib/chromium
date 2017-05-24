/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2005 Allan Sandfeld Jensen (kde@carewolf.com)
 *           (C) 2005, 2006 Samuel Weinig (sam.weinig@gmail.com)
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010 Apple Inc.
 *               All rights reserved.
 * Copyright (C) 2013 Adobe Systems Incorporated. All rights reserved.
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
 *
 */

#include "core/layout/LayoutBox.h"

#include <math.h>
#include <algorithm>
#include "core/dom/Document.h"
#include "core/editing/EditingUtilities.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLElement.h"
#include "core/html/HTMLFrameElementBase.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/input/EventHandler.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutAnalyzer.h"
#include "core/layout/LayoutDeprecatedFlexibleBox.h"
#include "core/layout/LayoutFlexibleBox.h"
#include "core/layout/LayoutGrid.h"
#include "core/layout/LayoutInline.h"
#include "core/layout/LayoutListMarker.h"
#include "core/layout/LayoutMultiColumnFlowThread.h"
#include "core/layout/LayoutMultiColumnSpannerPlaceholder.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/LayoutTableCell.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutAPIShim.h"
#include "core/layout/api/LayoutPartItem.h"
#include "core/layout/api/LineLayoutBlockFlow.h"
#include "core/layout/api/LineLayoutBox.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/layout/shapes/ShapeOutsideInfo.h"
#include "core/page/AutoscrollController.h"
#include "core/page/Page.h"
#include "core/page/scrolling/RootScrollerController.h"
#include "core/page/scrolling/ScrollingCoordinator.h"
#include "core/page/scrolling/SnapCoordinator.h"
#include "core/paint/BackgroundImageGeometry.h"
#include "core/paint/BoxPaintInvalidator.h"
#include "core/paint/BoxPainter.h"
#include "core/paint/PaintLayer.h"
#include "core/style/ShadowList.h"
#include "platform/LengthFunctions.h"
#include "platform/geometry/DoubleRect.h"
#include "platform/geometry/FloatQuad.h"
#include "platform/geometry/FloatRoundedRect.h"
#include "platform/wtf/PtrUtil.h"

namespace {

// Node that is currently being used as the scrolling root of a frame. See
// |effective rootScroller| in core/page/scrolling/README.md.
bool IsEffectiveRootScroller(const blink::LayoutBox* box) {
  if (!box || !box->GetNode())
    return false;

  return box->GetNode() == &box->GetDocument()
                                .GetRootScrollerController()
                                .EffectiveRootScroller();
}

}  // namespace

namespace blink {

// Used by flexible boxes when flexing this element and by table cells.
typedef WTF::HashMap<const LayoutBox*, LayoutUnit> OverrideSizeMap;

// Size of border belt for autoscroll. When mouse pointer in border belt,
// autoscroll is started.
static const int kAutoscrollBeltSize = 20;
static const unsigned kBackgroundObscurationTestMaxDepth = 4;

struct SameSizeAsLayoutBox : public LayoutBoxModelObject {
  LayoutRect frame_rect;
  LayoutSize previous_size;
  LayoutUnit intrinsic_content_logical_height;
  LayoutRectOutsets margin_box_outsets;
  LayoutUnit preferred_logical_width[2];
  void* pointers[3];
};

static_assert(sizeof(LayoutBox) == sizeof(SameSizeAsLayoutBox),
              "LayoutBox should stay small");

LayoutBox::LayoutBox(ContainerNode* node)
    : LayoutBoxModelObject(node),
      intrinsic_content_logical_height_(-1),
      min_preferred_logical_width_(-1),
      max_preferred_logical_width_(-1),
      inline_box_wrapper_(nullptr) {
  SetIsBox();
}

PaintLayerType LayoutBox::LayerTypeRequired() const {
  // hasAutoZIndex only returns true if the element is positioned or a flex-item
  // since position:static elements that are not flex-items get their z-index
  // coerced to auto.
  if (IsPositioned() || CreatesGroup() || HasClipPath() ||
      HasTransformRelatedProperty() || Style()->HasCompositorProxy() ||
      HasHiddenBackface() || HasReflection() || Style()->SpecifiesColumns() ||
      Style()->IsStackingContext() ||
      Style()->ShouldCompositeForCurrentAnimations() ||
      IsEffectiveRootScroller(this))
    return kNormalPaintLayer;

  if (HasOverflowClip())
    return kOverflowClipPaintLayer;

  return kNoPaintLayer;
}

void LayoutBox::WillBeDestroyed() {
  ClearOverrideSize();
  ClearContainingBlockOverrideSize();

  if (IsOutOfFlowPositioned())
    LayoutBlock::RemovePositionedObject(this);
  RemoveFromPercentHeightContainer();
  if (IsOrthogonalWritingModeRoot() && !DocumentBeingDestroyed())
    UnmarkOrthogonalWritingModeRoot();

  ShapeOutsideInfo::RemoveInfo(*this);

  LayoutBoxModelObject::WillBeDestroyed();
}

void LayoutBox::InsertedIntoTree() {
  LayoutBoxModelObject::InsertedIntoTree();
  AddScrollSnapMapping();

  if (IsOrthogonalWritingModeRoot())
    MarkOrthogonalWritingModeRoot();
}

void LayoutBox::WillBeRemovedFromTree() {
  if (!DocumentBeingDestroyed() && IsOrthogonalWritingModeRoot())
    UnmarkOrthogonalWritingModeRoot();

  ClearScrollSnapMapping();
  LayoutBoxModelObject::WillBeRemovedFromTree();
}

void LayoutBox::RemoveFloatingOrPositionedChildFromBlockLists() {
  DCHECK(IsFloatingOrOutOfFlowPositioned());

  if (DocumentBeingDestroyed())
    return;

  if (IsFloating()) {
    LayoutBlockFlow* parent_block_flow = nullptr;
    for (LayoutObject* curr = Parent(); curr; curr = curr->Parent()) {
      if (curr->IsLayoutBlockFlow()) {
        LayoutBlockFlow* curr_block_flow = ToLayoutBlockFlow(curr);
        if (!parent_block_flow || curr_block_flow->ContainsFloat(this))
          parent_block_flow = curr_block_flow;
      }
    }

    if (parent_block_flow) {
      parent_block_flow->MarkSiblingsWithFloatsForLayout(this);
      parent_block_flow->MarkAllDescendantsWithFloatsForLayout(this, false);
    }
  }

  if (IsOutOfFlowPositioned())
    LayoutBlock::RemovePositionedObject(this);
}

void LayoutBox::StyleWillChange(StyleDifference diff,
                                const ComputedStyle& new_style) {
  const ComputedStyle* old_style = Style();
  if (old_style) {
    LayoutFlowThread* flow_thread = FlowThreadContainingBlock();
    if (flow_thread && flow_thread != this)
      flow_thread->FlowThreadDescendantStyleWillChange(this, diff, new_style);

    // The background of the root element or the body element could propagate up
    // to the canvas. Just dirty the entire canvas when our style changes
    // substantially.
    if ((diff.NeedsFullPaintInvalidation() || diff.NeedsLayout()) &&
        GetNode() &&
        (isHTMLHtmlElement(*GetNode()) || isHTMLBodyElement(*GetNode()))) {
      View()->SetShouldDoFullPaintInvalidation();

      if (old_style->HasEntirelyFixedBackground() !=
          new_style.HasEntirelyFixedBackground())
        View()->Compositor()->SetNeedsUpdateFixedBackground();
    }

    // When a layout hint happens and an object's position style changes, we
    // have to do a layout to dirty the layout tree using the old position
    // value now.
    if (diff.NeedsFullLayout() && Parent() &&
        old_style->GetPosition() != new_style.GetPosition()) {
      if (!old_style->HasOutOfFlowPosition() &&
          new_style.HasOutOfFlowPosition()) {
        // We're about to go out of flow. Before that takes place, we need to
        // mark the current containing block chain for preferred widths
        // recalculation.
        SetNeedsLayoutAndPrefWidthsRecalc(
            LayoutInvalidationReason::kStyleChange);
      } else {
        MarkContainerChainForLayout();
      }
      if (old_style->GetPosition() == EPosition::kStatic)
        SetShouldDoFullPaintInvalidation();
      else if (new_style.HasOutOfFlowPosition())
        Parent()->SetChildNeedsLayout();
      if (IsFloating() && !IsOutOfFlowPositioned() &&
          new_style.HasOutOfFlowPosition())
        RemoveFloatingOrPositionedChildFromBlockLists();
    }
    // FIXME: This branch runs when !oldStyle, which means that layout was never
    // called so what's the point in invalidating the whole view that we never
    // painted?
  } else if (IsBody()) {
    View()->SetShouldDoFullPaintInvalidation();
  }

  LayoutBoxModelObject::StyleWillChange(diff, new_style);
}

void LayoutBox::StyleDidChange(StyleDifference diff,
                               const ComputedStyle* old_style) {
  // Horizontal writing mode definition is updated in LayoutBoxModelObject::
  // updateFromStyle, (as part of the LayoutBoxModelObject::styleDidChange call
  // below). So, we can safely cache the horizontal writing mode value before
  // style change here.
  bool old_horizontal_writing_mode = IsHorizontalWritingMode();

  LayoutBoxModelObject::StyleDidChange(diff, old_style);

  if (IsFloatingOrOutOfFlowPositioned() && old_style &&
      !old_style->IsFloating() && !old_style->HasOutOfFlowPosition() &&
      Parent() && Parent()->IsLayoutBlockFlow())
    ToLayoutBlockFlow(Parent())->ChildBecameFloatingOrOutOfFlow(this);

  const ComputedStyle& new_style = StyleRef();
  if (NeedsLayout() && old_style)
    RemoveFromPercentHeightContainer();

  if (old_horizontal_writing_mode != IsHorizontalWritingMode()) {
    if (old_style) {
      if (IsOrthogonalWritingModeRoot())
        MarkOrthogonalWritingModeRoot();
      else
        UnmarkOrthogonalWritingModeRoot();
    }

    ClearPercentHeightDescendants();
  }

  // If our zoom factor changes and we have a defined scrollLeft/Top, we need to
  // adjust that value into the new zoomed coordinate space.  Note that the new
  // scroll offset may be outside the normal min/max range of the scrollable
  // area, which is weird but OK, because the scrollable area will update its
  // min/max in updateAfterLayout().
  if (HasOverflowClip() && old_style &&
      old_style->EffectiveZoom() != new_style.EffectiveZoom()) {
    PaintLayerScrollableArea* scrollable_area = this->GetScrollableArea();
    DCHECK(scrollable_area);
    // We use getScrollOffset() rather than scrollPosition(), because scroll
    // offset is the distance from the beginning of flow for the box, which is
    // the dimension we want to preserve.
    ScrollOffset old_offset = scrollable_area->GetScrollOffset();
    if (old_offset.Width() || old_offset.Height()) {
      ScrollOffset new_offset = old_offset.ScaledBy(new_style.EffectiveZoom() /
                                                    old_style->EffectiveZoom());
      scrollable_area->SetScrollOffsetUnconditionally(new_offset);
    }
  }

  // Our opaqueness might have changed without triggering layout.
  if (diff.NeedsFullPaintInvalidation()) {
    LayoutObject* parent_to_invalidate = Parent();
    for (unsigned i = 0;
         i < kBackgroundObscurationTestMaxDepth && parent_to_invalidate; ++i) {
      parent_to_invalidate->InvalidateBackgroundObscurationStatus();
      parent_to_invalidate = parent_to_invalidate->Parent();
    }
  }

  if (IsDocumentElement() || IsBody()) {
    GetDocument().View()->RecalculateScrollbarOverlayColorTheme(
        GetDocument().View()->DocumentBackgroundColor());
    GetDocument().View()->RecalculateCustomScrollbarStyle();
    if (LayoutView* layout_view = View()) {
      if (PaintLayerScrollableArea* scrollable_area =
              layout_view->GetScrollableArea()) {
        if (scrollable_area->HorizontalScrollbar() &&
            scrollable_area->HorizontalScrollbar()->IsCustomScrollbar())
          scrollable_area->HorizontalScrollbar()->StyleChanged();
        if (scrollable_area->VerticalScrollbar() &&
            scrollable_area->VerticalScrollbar()->IsCustomScrollbar())
          scrollable_area->VerticalScrollbar()->StyleChanged();
      }
    }
  }
  UpdateShapeOutsideInfoAfterStyleChange(*Style(), old_style);
  UpdateGridPositionAfterStyleChange(old_style);

  // When we're no longer a flex item because we're now absolutely positioned,
  // we need to clear the override size so we're not affected by it anymore.
  // This technically covers too many cases (even when out-of-flow did not
  // change) but that should be harmless.
  if (IsOutOfFlowPositioned() && Parent() &&
      Parent()->StyleRef().IsDisplayFlexibleOrGridBox())
    ClearOverrideSize();

  if (LayoutMultiColumnSpannerPlaceholder* placeholder =
          this->SpannerPlaceholder())
    placeholder->LayoutObjectInFlowThreadStyleDidChange(old_style);

  UpdateBackgroundAttachmentFixedStatusAfterStyleChange();

  if (old_style) {
    LayoutFlowThread* flow_thread = FlowThreadContainingBlock();
    if (flow_thread && flow_thread != this)
      flow_thread->FlowThreadDescendantStyleDidChange(this, diff, *old_style);

    UpdateScrollSnapMappingAfterStyleChange(&new_style, old_style);

    if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled() &&
        ShouldClipOverflow()) {
      // The overflow clip paint property depends on border sizes through
      // overflowClipRect(), and border radii, so we update properties on
      // border size or radii change.
      if (!old_style->BorderSizeEquals(new_style) ||
          !old_style->RadiiEqual(new_style))
        SetNeedsPaintPropertyUpdate();
    }
  }

  if (diff.TransformChanged()) {
    if (ScrollingCoordinator* scrolling_coordinator =
            GetDocument().GetFrame()->GetPage()->GetScrollingCoordinator())
      scrolling_coordinator->NotifyTransformChanged(*this);
  }
  // Non-atomic inlines should be LayoutInline or LayoutText, not LayoutBox.
  DCHECK(!IsInline() || IsAtomicInlineLevel());
}

void LayoutBox::UpdateBackgroundAttachmentFixedStatusAfterStyleChange() {
  if (!GetFrameView())
    return;

  // On low-powered/mobile devices, preventing blitting on a scroll can cause
  // noticeable delays when scrolling a page with a fixed background image. As
  // an optimization, assuming there are no fixed positoned elements on the
  // page, we can acclerate scrolling (via blitting) if we ignore the CSS
  // property "background-attachment: fixed".
  bool ignore_fixed_background_attachment =
      RuntimeEnabledFeatures::fastMobileScrollingEnabled();
  if (ignore_fixed_background_attachment)
    return;

  // An object needs to be repainted on frame scroll when it has background-
  // attachment:fixed. LayoutView is responsible for painting root background,
  // thus the root element (and the body element if html element has no
  // background) skips painting backgrounds.
  bool is_background_attachment_fixed_object =
      !IsDocumentElement() && !BackgroundStolenForBeingBody() &&
      StyleRef().HasFixedBackgroundImage();
  if (IsLayoutView() &&
      View()->Compositor()->SupportsFixedRootBackgroundCompositing()) {
    if (StyleRef().HasEntirelyFixedBackground())
      is_background_attachment_fixed_object = false;
  }

  SetIsBackgroundAttachmentFixedObject(is_background_attachment_fixed_object);
}

void LayoutBox::UpdateShapeOutsideInfoAfterStyleChange(
    const ComputedStyle& style,
    const ComputedStyle* old_style) {
  const ShapeValue* shape_outside = style.ShapeOutside();
  const ShapeValue* old_shape_outside =
      old_style ? old_style->ShapeOutside()
                : ComputedStyle::InitialShapeOutside();

  Length shape_margin = style.ShapeMargin();
  Length old_shape_margin = old_style ? old_style->ShapeMargin()
                                      : ComputedStyle::InitialShapeMargin();

  float shape_image_threshold = style.ShapeImageThreshold();
  float old_shape_image_threshold =
      old_style ? old_style->ShapeImageThreshold()
                : ComputedStyle::InitialShapeImageThreshold();

  // FIXME: A future optimization would do a deep comparison for equality. (bug
  // 100811)
  if (shape_outside == old_shape_outside && shape_margin == old_shape_margin &&
      shape_image_threshold == old_shape_image_threshold)
    return;

  if (!shape_outside)
    ShapeOutsideInfo::RemoveInfo(*this);
  else
    ShapeOutsideInfo::EnsureInfo(*this).MarkShapeAsDirty();

  if (shape_outside || shape_outside != old_shape_outside)
    MarkShapeOutsideDependentsForLayout();
}

void LayoutBox::UpdateGridPositionAfterStyleChange(
    const ComputedStyle* old_style) {
  if (!old_style || !Parent() || !Parent()->IsLayoutGrid())
    return;

  if (old_style->GridColumnStart() == Style()->GridColumnStart() &&
      old_style->GridColumnEnd() == Style()->GridColumnEnd() &&
      old_style->GridRowStart() == Style()->GridRowStart() &&
      old_style->GridRowEnd() == Style()->GridRowEnd() &&
      old_style->Order() == Style()->Order() &&
      old_style->HasOutOfFlowPosition() == Style()->HasOutOfFlowPosition())
    return;

  // Positioned items don't participate on the layout of the grid,
  // so we don't need to mark the grid as dirty if they change positions.
  if (old_style->HasOutOfFlowPosition() && Style()->HasOutOfFlowPosition())
    return;

  // It should be possible to not dirty the grid in some cases (like moving an
  // explicitly placed grid item).
  // For now, it's more simple to just always recompute the grid.
  ToLayoutGrid(Parent())->DirtyGrid();
}

void LayoutBox::UpdateScrollSnapMappingAfterStyleChange(
    const ComputedStyle* new_style,
    const ComputedStyle* old_style) {
  SnapCoordinator* snap_coordinator = GetDocument().GetSnapCoordinator();
  if (!snap_coordinator)
    return;

  // Scroll snap type has no effect on the viewport defining element instead
  // they are handled by the LayoutView.
  bool allows_snap_container =
      GetNode() != GetDocument().ViewportDefiningElement();

  ScrollSnapType old_snap_type =
      old_style ? old_style->GetScrollSnapType() : kScrollSnapTypeNone;
  ScrollSnapType new_snap_type = new_style && allows_snap_container
                                     ? new_style->GetScrollSnapType()
                                     : kScrollSnapTypeNone;
  if (old_snap_type != new_snap_type)
    snap_coordinator->SnapContainerDidChange(*this, new_snap_type);

  Vector<LengthPoint> empty_vector;
  const Vector<LengthPoint>& old_snap_coordinate =
      old_style ? old_style->ScrollSnapCoordinate() : empty_vector;
  const Vector<LengthPoint>& new_snap_coordinate =
      new_style ? new_style->ScrollSnapCoordinate() : empty_vector;
  if (old_snap_coordinate != new_snap_coordinate)
    snap_coordinator->SnapAreaDidChange(*this, new_snap_coordinate);
}

void LayoutBox::AddScrollSnapMapping() {
  UpdateScrollSnapMappingAfterStyleChange(Style(), nullptr);
}

void LayoutBox::ClearScrollSnapMapping() {
  UpdateScrollSnapMappingAfterStyleChange(nullptr, Style());
}

void LayoutBox::UpdateFromStyle() {
  LayoutBoxModelObject::UpdateFromStyle();

  const ComputedStyle& style_to_use = StyleRef();
  SetFloating(!IsOutOfFlowPositioned() && style_to_use.IsFloating());
  SetHasTransformRelatedProperty(style_to_use.HasTransformRelatedProperty());
  SetHasReflection(style_to_use.BoxReflect());
}

void LayoutBox::UpdateLayout() {
  DCHECK(NeedsLayout());
  LayoutAnalyzer::Scope analyzer(*this);

  LayoutObject* child = SlowFirstChild();
  if (!child) {
    ClearNeedsLayout();
    return;
  }

  LayoutState state(*this);
  while (child) {
    child->LayoutIfNeeded();
    DCHECK(!child->NeedsLayout());
    child = child->NextSibling();
  }
  InvalidateBackgroundObscurationStatus();
  ClearNeedsLayout();
}

// More IE extensions.  clientWidth and clientHeight represent the interior of
// an object excluding border and scrollbar.
DISABLE_CFI_PERF
LayoutUnit LayoutBox::ClientWidth() const {
  // We need to clamp negative values. The scrollbar may be wider than the
  // padding box. Another reason: While border side values are currently limited
  // to 2^20px (a recent change in the code), if this limit is raised again in
  // the future, we'd have ill effects of saturated arithmetic otherwise.
  return (frame_rect_.Width() - BorderLeft() - BorderRight() -
          VerticalScrollbarWidth())
      .ClampNegativeToZero();
}

DISABLE_CFI_PERF
LayoutUnit LayoutBox::ClientHeight() const {
  // We need to clamp negative values. The scrollbar may be wider than the
  // padding box. Another reason: While border side values are currently limited
  // to 2^20px (a recent change in the code), if this limit is raised again in
  // the future, we'd have ill effects of saturated arithmetic otherwise.
  return (frame_rect_.Height() - BorderTop() - BorderBottom() -
          HorizontalScrollbarHeight())
      .ClampNegativeToZero();
}

int LayoutBox::PixelSnappedClientWidth() const {
  return SnapSizeToPixel(ClientWidth(), Location().X() + ClientLeft());
}

DISABLE_CFI_PERF
int LayoutBox::PixelSnappedClientHeight() const {
  return SnapSizeToPixel(ClientHeight(), Location().Y() + ClientTop());
}

int LayoutBox::PixelSnappedOffsetWidth(const Element*) const {
  return SnapSizeToPixel(OffsetWidth(), Location().X() + ClientLeft());
}

int LayoutBox::PixelSnappedOffsetHeight(const Element*) const {
  return SnapSizeToPixel(OffsetHeight(), Location().Y() + ClientTop());
}

LayoutUnit LayoutBox::ScrollWidth() const {
  if (HasOverflowClip())
    return GetScrollableArea()->ScrollWidth();
  // For objects with visible overflow, this matches IE.
  // FIXME: Need to work right with writing modes.
  if (Style()->IsLeftToRightDirection())
    return std::max(ClientWidth(), LayoutOverflowRect().MaxX() - BorderLeft());
  return ClientWidth() -
         std::min(LayoutUnit(), LayoutOverflowRect().X() - BorderLeft());
}

LayoutUnit LayoutBox::ScrollHeight() const {
  if (HasOverflowClip())
    return GetScrollableArea()->ScrollHeight();
  // For objects with visible overflow, this matches IE.
  // FIXME: Need to work right with writing modes.
  return std::max(ClientHeight(), LayoutOverflowRect().MaxY() - BorderTop());
}

LayoutUnit LayoutBox::ScrollLeft() const {
  return HasOverflowClip()
             ? LayoutUnit(GetScrollableArea()->ScrollPosition().X())
             : LayoutUnit();
}

LayoutUnit LayoutBox::ScrollTop() const {
  return HasOverflowClip()
             ? LayoutUnit(GetScrollableArea()->ScrollPosition().Y())
             : LayoutUnit();
}

int LayoutBox::PixelSnappedScrollWidth() const {
  return SnapSizeToPixel(ScrollWidth(), Location().X() + ClientLeft());
}

int LayoutBox::PixelSnappedScrollHeight() const {
  if (HasOverflowClip())
    return SnapSizeToPixel(GetScrollableArea()->ScrollHeight(),
                           Location().Y() + ClientTop());
  // For objects with visible overflow, this matches IE.
  // FIXME: Need to work right with writing modes.
  return SnapSizeToPixel(ScrollHeight(), Location().Y() + ClientTop());
}

void LayoutBox::SetScrollLeft(LayoutUnit new_left) {
  // This doesn't hit in any tests, but since the equivalent code in
  // setScrollTop does, presumably this code does as well.
  DisableCompositingQueryAsserts disabler;

  if (!HasOverflowClip())
    return;

  PaintLayerScrollableArea* scrollable_area = GetScrollableArea();
  FloatPoint new_position(new_left.ToFloat(),
                          scrollable_area->ScrollPosition().Y());
  scrollable_area->ScrollToAbsolutePosition(new_position, kScrollBehaviorAuto);
}

void LayoutBox::SetScrollTop(LayoutUnit new_top) {
  // Hits in
  // compositing/overflow/do-not-assert-on-invisible-composited-layers.html
  DisableCompositingQueryAsserts disabler;

  if (!HasOverflowClip())
    return;

  PaintLayerScrollableArea* scrollable_area = GetScrollableArea();
  FloatPoint new_position(scrollable_area->ScrollPosition().X(),
                          new_top.ToFloat());
  scrollable_area->ScrollToAbsolutePosition(new_position, kScrollBehaviorAuto);
}

void LayoutBox::ScrollToPosition(const FloatPoint& position,
                                 ScrollBehavior scroll_behavior) {
  // This doesn't hit in any tests, but since the equivalent code in
  // setScrollTop does, presumably this code does as well.
  DisableCompositingQueryAsserts disabler;

  if (!HasOverflowClip())
    return;

  GetScrollableArea()->ScrollToAbsolutePosition(position, scroll_behavior);
}

// Returns true iff we are attempting an autoscroll inside an iframe with
// scrolling="no".
static bool IsDisallowedAutoscroll(HTMLFrameOwnerElement* owner_element,
                                   FrameView* frame_view) {
  if (owner_element && IsHTMLFrameElementBase(*owner_element)) {
    HTMLFrameElementBase* frame_element_base =
        ToHTMLFrameElementBase(owner_element);
    if (Page* page = frame_view->GetFrame().GetPage()) {
      return page->GetAutoscrollController().AutoscrollInProgress() &&
             frame_element_base->ScrollingMode() == kScrollbarAlwaysOff;
    }
  }
  return false;
}

void LayoutBox::ScrollRectToVisible(const LayoutRect& rect,
                                    const ScrollAlignment& align_x,
                                    const ScrollAlignment& align_y,
                                    ScrollType scroll_type,
                                    bool make_visible_in_visual_viewport) {
  DCHECK(scroll_type == kProgrammaticScroll || scroll_type == kUserScroll);
  // Presumably the same issue as in setScrollTop. See crbug.com/343132.
  DisableCompositingQueryAsserts disabler;

  LayoutRect rect_to_scroll = rect;
  if (rect_to_scroll.Width() <= 0)
    rect_to_scroll.SetWidth(LayoutUnit(1));
  if (rect_to_scroll.Height() <= 0)
    rect_to_scroll.SetHeight(LayoutUnit(1));

  LayoutBox* parent_box = nullptr;
  LayoutRect new_rect = rect_to_scroll;

  bool restricted_by_line_clamp = false;
  if (ContainingBlock()) {
    parent_box = ContainingBlock();
    restricted_by_line_clamp =
        !ContainingBlock()->Style()->LineClamp().IsNone();
  }

  if (HasOverflowClip() && !restricted_by_line_clamp) {
    // Don't scroll to reveal an overflow layer that is restricted by the
    // -webkit-line-clamp property. This will prevent us from revealing text
    // hidden by the slider in Safari RSS.
    // TODO(eae): We probably don't need this any more as we don't share any
    //            code with the Safari RSS reeder.
    new_rect = GetScrollableArea()->ScrollIntoView(rect_to_scroll, align_x,
                                                   align_y, scroll_type);
    if (new_rect.IsEmpty())
      return;
  } else if (!parent_box && CanBeProgramaticallyScrolled()) {
    if (FrameView* frame_view = this->GetFrameView()) {
      HTMLFrameOwnerElement* owner_element = GetDocument().LocalOwner();
      if (!IsDisallowedAutoscroll(owner_element, frame_view)) {
        if (make_visible_in_visual_viewport) {
          frame_view->GetScrollableArea()->ScrollIntoView(
              rect_to_scroll, align_x, align_y, scroll_type);
        } else {
          frame_view->LayoutViewportScrollableArea()->ScrollIntoView(
              rect_to_scroll, align_x, align_y, scroll_type);
        }
        if (owner_element && owner_element->GetLayoutObject()) {
          if (frame_view->SafeToPropagateScrollToParent()) {
            parent_box = owner_element->GetLayoutObject()->EnclosingBox();
            LayoutView* parent_view = owner_element->GetLayoutObject()->View();
            new_rect = EnclosingLayoutRect(
                View()
                    ->LocalToAncestorQuad(
                        FloatRect(rect_to_scroll), parent_view,
                        kUseTransforms | kTraverseDocumentBoundaries)
                    .BoundingBox());
          } else {
            parent_box = nullptr;
          }
        }
      }
    }
  }

  // If we are fixed-position and stick to the viewport, it is useless to
  // scroll the parent.
  if (Style()->GetPosition() == EPosition::kFixed &&
      ContainerForFixedPosition() == View()) {
    return;
  }

  if (GetFrame()->GetPage()->GetAutoscrollController().AutoscrollInProgress())
    parent_box = EnclosingScrollableBox();

  if (parent_box)
    parent_box->ScrollRectToVisible(new_rect, align_x, align_y, scroll_type,
                                    make_visible_in_visual_viewport);
}

void LayoutBox::AbsoluteRects(Vector<IntRect>& rects,
                              const LayoutPoint& accumulated_offset) const {
  rects.push_back(PixelSnappedIntRect(accumulated_offset, Size()));
}

void LayoutBox::AbsoluteQuads(Vector<FloatQuad>& quads,
                              MapCoordinatesFlags mode) const {
  if (LayoutFlowThread* flow_thread = FlowThreadContainingBlock()) {
    flow_thread->AbsoluteQuadsForDescendant(*this, quads, mode);
    return;
  }
  quads.push_back(
      LocalToAbsoluteQuad(FloatRect(0, 0, frame_rect_.Width().ToFloat(),
                                    frame_rect_.Height().ToFloat()),
                          mode));
}

FloatRect LayoutBox::LocalBoundingBoxRectForAccessibility() const {
  return FloatRect(0, 0, frame_rect_.Width().ToFloat(),
                   frame_rect_.Height().ToFloat());
}

void LayoutBox::UpdateAfterLayout() {
  // Transform-origin depends on box size, so we need to update the layer
  // transform after layout.
  if (HasLayer()) {
    Layer()->UpdateTransformationMatrix();
    Layer()->UpdateSizeAndScrollingAfterLayout();
  }
}

LayoutUnit LayoutBox::LogicalHeightWithVisibleOverflow() const {
  if (!overflow_ || HasOverflowClip())
    return LogicalHeight();
  LayoutRect overflow = LayoutOverflowRect();
  if (Style()->IsHorizontalWritingMode())
    return overflow.MaxY();
  return overflow.MaxX();
}

LayoutUnit LayoutBox::ConstrainLogicalWidthByMinMax(LayoutUnit logical_width,
                                                    LayoutUnit available_width,
                                                    LayoutBlock* cb) const {
  const ComputedStyle& style_to_use = StyleRef();
  if (!style_to_use.LogicalMaxWidth().IsMaxSizeNone())
    logical_width = std::min(
        logical_width,
        ComputeLogicalWidthUsing(kMaxSize, style_to_use.LogicalMaxWidth(),
                                 available_width, cb));
  return std::max(logical_width, ComputeLogicalWidthUsing(
                                     kMinSize, style_to_use.LogicalMinWidth(),
                                     available_width, cb));
}

LayoutUnit LayoutBox::ConstrainLogicalHeightByMinMax(
    LayoutUnit logical_height,
    LayoutUnit intrinsic_content_height) const {
  const ComputedStyle& style_to_use = StyleRef();
  if (!style_to_use.LogicalMaxHeight().IsMaxSizeNone()) {
    LayoutUnit max_h = ComputeLogicalHeightUsing(
        kMaxSize, style_to_use.LogicalMaxHeight(), intrinsic_content_height);
    if (max_h != -1)
      logical_height = std::min(logical_height, max_h);
  }
  return std::max(logical_height, ComputeLogicalHeightUsing(
                                      kMinSize, style_to_use.LogicalMinHeight(),
                                      intrinsic_content_height));
}

LayoutUnit LayoutBox::ConstrainContentBoxLogicalHeightByMinMax(
    LayoutUnit logical_height,
    LayoutUnit intrinsic_content_height) const {
  // If the min/max height and logical height are both percentages we take
  // advantage of already knowing the current resolved percentage height
  // to avoid recursing up through our containing blocks again to determine it.
  const ComputedStyle& style_to_use = StyleRef();
  if (!style_to_use.LogicalMaxHeight().IsMaxSizeNone()) {
    if (style_to_use.LogicalMaxHeight().GetType() == kPercent &&
        style_to_use.LogicalHeight().GetType() == kPercent) {
      LayoutUnit available_logical_height(
          logical_height / style_to_use.LogicalHeight().Value() * 100);
      logical_height = std::min(logical_height,
                                ValueForLength(style_to_use.LogicalMaxHeight(),
                                               available_logical_height));
    } else {
      LayoutUnit max_height(ComputeContentLogicalHeight(
          kMaxSize, style_to_use.LogicalMaxHeight(), intrinsic_content_height));
      if (max_height != -1)
        logical_height = std::min(logical_height, max_height);
    }
  }

  if (style_to_use.LogicalMinHeight().GetType() == kPercent &&
      style_to_use.LogicalHeight().GetType() == kPercent) {
    LayoutUnit available_logical_height(
        logical_height / style_to_use.LogicalHeight().Value() * 100);
    logical_height =
        std::max(logical_height, ValueForLength(style_to_use.LogicalMinHeight(),
                                                available_logical_height));
  } else {
    logical_height = std::max(
        logical_height,
        ComputeContentLogicalHeight(kMinSize, style_to_use.LogicalMinHeight(),
                                    intrinsic_content_height));
  }

  return logical_height;
}

void LayoutBox::SetLocationAndUpdateOverflowControlsIfNeeded(
    const LayoutPoint& location) {
  IntSize old_pixel_snapped_border_rect_size =
      PixelSnappedBorderBoxRect().Size();
  SetLocation(location);
  if (HasLayer() && PixelSnappedBorderBoxRect().Size() !=
                        old_pixel_snapped_border_rect_size) {
    Layer()->UpdateSizeAndScrollingAfterLayout();
  }
}

IntRect LayoutBox::AbsoluteContentBox() const {
  // This is wrong with transforms and flipped writing modes.
  IntRect rect = PixelSnappedIntRect(ContentBoxRect());
  FloatPoint abs_pos = LocalToAbsolute();
  rect.Move(abs_pos.X(), abs_pos.Y());
  return rect;
}

IntSize LayoutBox::AbsoluteContentBoxOffset() const {
  IntPoint offset = RoundedIntPoint(ContentBoxOffset());
  FloatPoint abs_pos = LocalToAbsolute();
  offset.Move(abs_pos.X(), abs_pos.Y());
  return ToIntSize(offset);
}

FloatQuad LayoutBox::AbsoluteContentQuad(MapCoordinatesFlags flags) const {
  LayoutRect rect = ContentBoxRect();
  return LocalToAbsoluteQuad(FloatRect(rect), flags);
}

LayoutRect LayoutBox::BackgroundRect(BackgroundRectType rect_type) const {
  EFillBox background_box = kTextFillBox;
  // Find the largest background rect of the given opaqueness.
  if (const FillLayer* current = &(Style()->BackgroundLayers())) {
    do {
      const FillLayer* cur = current;
      current = current->Next();
      if (rect_type == kBackgroundKnownOpaqueRect) {
        if (cur->BlendMode() != kWebBlendModeNormal ||
            cur->Composite() != kCompositeSourceOver)
          continue;

        bool layer_known_opaque = false;
        // Check if the image is opaque and fills the clip.
        if (const StyleImage* image = cur->GetImage()) {
          if ((cur->RepeatX() == kRepeatFill || cur->RepeatX() == kRoundFill) &&
              (cur->RepeatY() == kRepeatFill || cur->RepeatY() == kRoundFill) &&
              image->KnownToBeOpaque(GetDocument(), StyleRef())) {
            layer_known_opaque = true;
          }
        }

        // The background color is painted into the last layer.
        if (!cur->Next()) {
          Color background_color = ResolveColor(CSSPropertyBackgroundColor);
          if (!background_color.HasAlpha())
            layer_known_opaque = true;
        }

        // If neither the image nor the color are opaque then skip this layer.
        if (!layer_known_opaque)
          continue;
      }
      EFillBox current_clip = cur->Clip();
      // Restrict clip if attachment is local.
      if (current_clip == kBorderFillBox &&
          cur->Attachment() == kLocalBackgroundAttachment)
        current_clip = kPaddingFillBox;

      // If we're asking for the clip rect, a content-box clipped fill layer can
      // be scrolled into the padding box of the overflow container.
      if (rect_type == kBackgroundClipRect && current_clip == kContentFillBox &&
          cur->Attachment() == kLocalBackgroundAttachment) {
        current_clip = kPaddingFillBox;
      }

      background_box = EnclosingFillBox(background_box, current_clip);
    } while (current);
  }
  switch (background_box) {
    case kBorderFillBox:
      return BorderBoxRect();
      break;
    case kPaddingFillBox:
      return PaddingBoxRect();
      break;
    case kContentFillBox:
      return ContentBoxRect();
      break;
    default:
      break;
  }
  return LayoutRect();
}

void LayoutBox::AddOutlineRects(Vector<LayoutRect>& rects,
                                const LayoutPoint& additional_offset,
                                IncludeBlockVisualOverflowOrNot) const {
  rects.push_back(LayoutRect(additional_offset, Size()));
}

bool LayoutBox::CanResize() const {
  // We need a special case for <iframe> because they never have
  // hasOverflowClip(). However, they do "implicitly" clip their contents, so
  // we want to allow resizing them also.
  return (HasOverflowClip() || IsLayoutIFrame()) &&
         Style()->Resize() != RESIZE_NONE;
}

void LayoutBox::AddLayerHitTestRects(LayerHitTestRects& layer_rects,
                                     const PaintLayer* current_layer,
                                     const LayoutPoint& layer_offset,
                                     const LayoutRect& container_rect) const {
  LayoutPoint adjusted_layer_offset = layer_offset + LocationOffset();
  LayoutBoxModelObject::AddLayerHitTestRects(
      layer_rects, current_layer, adjusted_layer_offset, container_rect);
}

void LayoutBox::ComputeSelfHitTestRects(Vector<LayoutRect>& rects,
                                        const LayoutPoint& layer_offset) const {
  if (!Size().IsEmpty())
    rects.push_back(LayoutRect(layer_offset, Size()));
}

int LayoutBox::VerticalScrollbarWidth() const {
  if (!HasOverflowClip() || Style()->OverflowY() == EOverflow::kOverlay)
    return 0;

  return GetScrollableArea()->VerticalScrollbarWidth();
}

int LayoutBox::HorizontalScrollbarHeight() const {
  if (!HasOverflowClip() || Style()->OverflowX() == EOverflow::kOverlay)
    return 0;

  return GetScrollableArea()->HorizontalScrollbarHeight();
}

ScrollResult LayoutBox::Scroll(ScrollGranularity granularity,
                               const FloatSize& delta) {
  // Presumably the same issue as in setScrollTop. See crbug.com/343132.
  DisableCompositingQueryAsserts disabler;

  if (!GetScrollableArea())
    return ScrollResult();

  return GetScrollableArea()->UserScroll(granularity, delta);
}

bool LayoutBox::CanBeScrolledAndHasScrollableArea() const {
  return CanBeProgramaticallyScrolled() &&
         (PixelSnappedScrollHeight() != PixelSnappedClientHeight() ||
          PixelSnappedScrollWidth() != PixelSnappedClientWidth());
}

bool LayoutBox::CanBeProgramaticallyScrolled() const {
  Node* node = this->GetNode();
  if (node && node->IsDocumentNode())
    return true;

  if (!HasOverflowClip())
    return false;

  bool has_scrollable_overflow =
      HasScrollableOverflowX() || HasScrollableOverflowY();
  if (ScrollsOverflow() && has_scrollable_overflow)
    return true;

  return node && HasEditableStyle(*node);
}

void LayoutBox::Autoscroll(const IntPoint& position_in_root_frame) {
  LocalFrame* frame = this->GetFrame();
  if (!frame)
    return;

  FrameView* frame_view = frame->View();
  if (!frame_view)
    return;

  IntPoint position_in_content =
      frame_view->RootFrameToContents(position_in_root_frame);
  ScrollRectToVisible(LayoutRect(position_in_content, LayoutSize(1, 1)),
                      ScrollAlignment::kAlignToEdgeIfNeeded,
                      ScrollAlignment::kAlignToEdgeIfNeeded, kUserScroll);
}

// There are two kinds of layoutObject that can autoscroll.
bool LayoutBox::CanAutoscroll() const {
  if (GetNode() && GetNode()->IsDocumentNode())
    return View()->GetFrameView()->IsScrollable();

  // Check for a box that can be scrolled in its own right.
  return CanBeScrolledAndHasScrollableArea();
}

// If specified point is in border belt, returned offset denotes direction of
// scrolling.
IntSize LayoutBox::CalculateAutoscrollDirection(
    const IntPoint& point_in_root_frame) const {
  if (!GetFrame())
    return IntSize();

  FrameView* frame_view = GetFrame()->View();
  if (!frame_view)
    return IntSize();

  IntRect box(AbsoluteBoundingBoxRect());
  box.Move(View()->GetFrameView()->ScrollOffsetInt());
  IntRect window_box = View()->GetFrameView()->ContentsToRootFrame(box);

  IntPoint window_autoscroll_point = point_in_root_frame;

  if (window_autoscroll_point.X() < window_box.X() + kAutoscrollBeltSize)
    window_autoscroll_point.Move(-kAutoscrollBeltSize, 0);
  else if (window_autoscroll_point.X() >
           window_box.MaxX() - kAutoscrollBeltSize)
    window_autoscroll_point.Move(kAutoscrollBeltSize, 0);

  if (window_autoscroll_point.Y() < window_box.Y() + kAutoscrollBeltSize)
    window_autoscroll_point.Move(0, -kAutoscrollBeltSize);
  else if (window_autoscroll_point.Y() >
           window_box.MaxY() - kAutoscrollBeltSize)
    window_autoscroll_point.Move(0, kAutoscrollBeltSize);

  return window_autoscroll_point - point_in_root_frame;
}

LayoutBox* LayoutBox::FindAutoscrollable(LayoutObject* layout_object) {
  while (layout_object && !(layout_object->IsBox() &&
                            ToLayoutBox(layout_object)->CanAutoscroll())) {
    // Do not start autoscroll when the node is inside a fixed-position element.
    if (layout_object->IsBox() && ToLayoutBox(layout_object)->HasLayer() &&
        ToLayoutBox(layout_object)->Layer()->FixedToViewport()) {
      return nullptr;
    }

    if (!layout_object->Parent() &&
        layout_object->GetNode() == layout_object->GetDocument() &&
        layout_object->GetDocument().LocalOwner())
      layout_object =
          layout_object->GetDocument().LocalOwner()->GetLayoutObject();
    else
      layout_object = layout_object->Parent();
  }

  return layout_object && layout_object->IsBox() ? ToLayoutBox(layout_object)
                                                 : nullptr;
}

void LayoutBox::ScrollByRecursively(const ScrollOffset& delta) {
  if (delta.IsZero())
    return;

  bool restricted_by_line_clamp = false;
  if (Parent())
    restricted_by_line_clamp = !Parent()->Style()->LineClamp().IsNone();

  if (HasOverflowClip() && !restricted_by_line_clamp) {
    PaintLayerScrollableArea* scrollable_area = this->GetScrollableArea();
    DCHECK(scrollable_area);

    ScrollOffset new_scroll_offset = scrollable_area->GetScrollOffset() + delta;
    scrollable_area->SetScrollOffset(new_scroll_offset, kProgrammaticScroll);

    // If this layer can't do the scroll we ask the next layer up that can
    // scroll to try.
    ScrollOffset remaining_scroll_offset =
        new_scroll_offset - scrollable_area->GetScrollOffset();
    if (!remaining_scroll_offset.IsZero() && Parent()) {
      if (LayoutBox* scrollable_box = EnclosingScrollableBox())
        scrollable_box->ScrollByRecursively(remaining_scroll_offset);

      LocalFrame* frame = this->GetFrame();
      if (frame && frame->GetPage())
        frame->GetPage()
            ->GetAutoscrollController()
            .UpdateAutoscrollLayoutObject();
    }
  } else if (View()->GetFrameView()) {
    // If we are here, we were called on a layoutObject that can be
    // programmatically scrolled, but doesn't have an overflow clip. Which means
    // that it is a document node that can be scrolled.
    // FIXME: Pass in DoubleSize. crbug.com/414283.
    View()->GetFrameView()->ScrollBy(delta, kUserScroll);

    // FIXME: If we didn't scroll the whole way, do we want to try looking at
    // the frames ownerElement?
    // https://bugs.webkit.org/show_bug.cgi?id=28237
  }
}

bool LayoutBox::NeedsPreferredWidthsRecalculation() const {
  return Style()->PaddingStart().IsPercentOrCalc() ||
         Style()->PaddingEnd().IsPercentOrCalc();
}

IntSize LayoutBox::OriginAdjustmentForScrollbars() const {
  IntSize size;
  int adjustment_width = VerticalScrollbarWidth();
  if (HasFlippedBlocksWritingMode() ||
      (IsHorizontalWritingMode() &&
       ShouldPlaceBlockDirectionScrollbarOnLogicalLeft())) {
    size.Expand(adjustment_width, 0);
  }
  return size;
}

IntSize LayoutBox::ScrolledContentOffset() const {
  DCHECK(HasOverflowClip());
  DCHECK(HasLayer());
  // FIXME: Return DoubleSize here. crbug.com/414283.
  PaintLayerScrollableArea* scrollable_area = GetScrollableArea();
  IntSize result =
      scrollable_area->ScrollOffsetInt() + OriginAdjustmentForScrollbars();
  if (IsHorizontalWritingMode() &&
      ShouldPlaceBlockDirectionScrollbarOnLogicalLeft())
    result.Expand(-VerticalScrollbarWidth(), 0);
  return result;
}

LayoutRect LayoutBox::ClippingRect() const {
  LayoutRect result = LayoutRect(LayoutRect::InfiniteIntRect());
  if (ShouldClipOverflow())
    result = OverflowClipRect(LayoutPoint());

  if (HasClip())
    result.Intersect(ClipRect(LayoutPoint()));

  return result;
}

bool LayoutBox::MapVisualRectToContainer(
    const LayoutObject* container_object,
    const LayoutPoint& container_offset,
    const LayoutObject* ancestor,
    VisualRectFlags visual_rect_flags,
    TransformState& transform_state) const {
  bool container_preserve_3d = container_object->Style()->Preserves3D();

  TransformState::TransformAccumulation accumulation =
      container_preserve_3d ? TransformState::kAccumulateTransform
                            : TransformState::kFlattenTransform;

  // If there is no transform on this box, adjust for container offset and
  // container scrolling, then apply container clip.
  if (!ShouldUseTransformFromContainer(container_object)) {
    transform_state.MoveBy(container_offset, accumulation);
    if (container_object->IsBox() && container_object != ancestor &&
        !ToLayoutBox(container_object)
             ->MapScrollingContentsRectToBoxSpace(transform_state, accumulation,
                                                  visual_rect_flags))
      return false;
    return true;
  }

  // Otherwise, do the following:
  // 1. Expand for pixel snapping.
  // 2. Generate transformation matrix combining, in this order
  //    a) transform,
  //    b) container offset,
  //    c) container scroll offset,
  //    d) perspective applied by container.
  // 3. Apply transform Transform+flattening.
  // 4. Apply container clip.

  // 1. Expand for pixel snapping.
  // Use EnclosingBoundingBox because we cannot properly compute pixel
  // snapping for painted elements within the transform since we don't know
  // the desired subpixel accumulation at this point, and the transform may
  // include a scale. This only makes sense for non-preserve3D.
  if (!StyleRef().Preserves3D()) {
    transform_state.Flatten();
    transform_state.SetQuad(
        FloatQuad(transform_state.LastPlanarQuad().EnclosingBoundingBox()));
  }

  // 2. Generate transformation matrix.
  // a) Transform.
  TransformationMatrix transform;
  if (Layer() && Layer()->Transform())
    transform.Multiply(Layer()->CurrentTransform());

  // b) Container offset.
  transform.PostTranslate(container_offset.X().ToFloat(),
                          container_offset.Y().ToFloat());

  // c) Container scroll offset.
  if (container_object->IsBox() && container_object != ancestor &&
      container_object->HasOverflowClip()) {
    IntSize offset = -ToLayoutBox(container_object)->ScrolledContentOffset();
    transform.PostTranslate(offset.Width(), offset.Height());
  }

  // d) Perspective applied by container.
  if (container_object && container_object->HasLayer() &&
      container_object->Style()->HasPerspective()) {
    // Perspective on the container affects us, so we have to factor it in here.
    DCHECK(container_object->HasLayer());
    FloatPoint perspective_origin =
        ToLayoutBoxModelObject(container_object)->Layer()->PerspectiveOrigin();

    TransformationMatrix perspective_matrix;
    perspective_matrix.ApplyPerspective(
        container_object->Style()->Perspective());
    perspective_matrix.ApplyTransformOrigin(perspective_origin.X(),
                                            perspective_origin.Y(), 0);

    transform = perspective_matrix * transform;
  }

  // 3. Apply transform and flatten.
  transform_state.ApplyTransform(transform, accumulation);
  if (!container_preserve_3d)
    transform_state.Flatten();

  // 4. Apply container clip.
  if (container_object->IsBox() && container_object != ancestor &&
      container_object->HasClipRelatedProperty()) {
    return ToLayoutBox(container_object)
        ->ApplyBoxClips(transform_state, accumulation, visual_rect_flags);
  }

  return true;
}

bool LayoutBox::MapScrollingContentsRectToBoxSpace(
    TransformState& transform_state,
    TransformState::TransformAccumulation accumulation,
    VisualRectFlags visual_rect_flags) const {
  if (!HasClipRelatedProperty())
    return true;

  if (HasOverflowClip()) {
    LayoutSize offset = LayoutSize(-ScrolledContentOffset());
    transform_state.Move(offset, accumulation);
  }

  return ApplyBoxClips(transform_state, accumulation, visual_rect_flags);
}

bool LayoutBox::ApplyBoxClips(
    TransformState& transform_state,
    TransformState::TransformAccumulation accumulation,
    VisualRectFlags visual_rect_flags) const {
  // This won't work fully correctly for fixed-position elements, who should
  // receive CSS clip but for whom the current object is not in the containing
  // block chain.
  LayoutRect clip_rect = ClippingRect();

  transform_state.Flatten();
  LayoutRect rect(transform_state.LastPlanarQuad().EnclosingBoundingBox());
  bool does_intersect;
  if (visual_rect_flags & kEdgeInclusive) {
    does_intersect = rect.InclusiveIntersect(clip_rect);
  } else {
    rect.Intersect(clip_rect);
    does_intersect = !rect.IsEmpty();
  }
  transform_state.SetQuad(FloatQuad(FloatRect(rect)));

  return does_intersect;
}

void LayoutBox::ComputeIntrinsicLogicalWidths(
    LayoutUnit& min_logical_width,
    LayoutUnit& max_logical_width) const {
  min_logical_width =
      MinPreferredLogicalWidth() - BorderAndPaddingLogicalWidth();
  max_logical_width =
      MaxPreferredLogicalWidth() - BorderAndPaddingLogicalWidth();
}

LayoutUnit LayoutBox::MinPreferredLogicalWidth() const {
  if (PreferredLogicalWidthsDirty()) {
#if DCHECK_IS_ON()
    SetLayoutNeededForbiddenScope layout_forbidden_scope(
        const_cast<LayoutBox&>(*this));
#endif
    const_cast<LayoutBox*>(this)->ComputePreferredLogicalWidths();
    DCHECK(!PreferredLogicalWidthsDirty());
  }

  return min_preferred_logical_width_;
}

DISABLE_CFI_PERF
LayoutUnit LayoutBox::MaxPreferredLogicalWidth() const {
  if (PreferredLogicalWidthsDirty()) {
#if DCHECK_IS_ON()
    SetLayoutNeededForbiddenScope layout_forbidden_scope(
        const_cast<LayoutBox&>(*this));
#endif
    const_cast<LayoutBox*>(this)->ComputePreferredLogicalWidths();
    DCHECK(!PreferredLogicalWidthsDirty());
  }

  return max_preferred_logical_width_;
}

bool LayoutBox::HasOverrideLogicalContentHeight() const {
  return rare_data_ && rare_data_->override_logical_content_height_ != -1;
}

bool LayoutBox::HasOverrideLogicalContentWidth() const {
  return rare_data_ && rare_data_->override_logical_content_width_ != -1;
}

void LayoutBox::SetOverrideLogicalContentHeight(LayoutUnit height) {
  DCHECK_GE(height, 0);
  EnsureRareData().override_logical_content_height_ = height;
}

void LayoutBox::SetOverrideLogicalContentWidth(LayoutUnit width) {
  DCHECK_GE(width, 0);
  EnsureRareData().override_logical_content_width_ = width;
}

void LayoutBox::ClearOverrideLogicalContentHeight() {
  if (rare_data_)
    rare_data_->override_logical_content_height_ = LayoutUnit(-1);
}

void LayoutBox::ClearOverrideLogicalContentWidth() {
  if (rare_data_)
    rare_data_->override_logical_content_width_ = LayoutUnit(-1);
}

void LayoutBox::ClearOverrideSize() {
  ClearOverrideLogicalContentHeight();
  ClearOverrideLogicalContentWidth();
}

LayoutUnit LayoutBox::OverrideLogicalContentWidth() const {
  DCHECK(HasOverrideLogicalContentWidth());
  return rare_data_->override_logical_content_width_;
}

LayoutUnit LayoutBox::OverrideLogicalContentHeight() const {
  DCHECK(HasOverrideLogicalContentHeight());
  return rare_data_->override_logical_content_height_;
}

// TODO (lajava) Now that we have implemented these functions based on physical
// direction, we'd rather remove the logical ones.
LayoutUnit LayoutBox::OverrideContainingBlockContentLogicalWidth() const {
  DCHECK(HasOverrideContainingBlockLogicalWidth());
  return rare_data_->override_containing_block_content_logical_width_;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
LayoutUnit LayoutBox::OverrideContainingBlockContentLogicalHeight() const {
  DCHECK(HasOverrideContainingBlockLogicalHeight());
  return rare_data_->override_containing_block_content_logical_height_;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
bool LayoutBox::HasOverrideContainingBlockLogicalWidth() const {
  return rare_data_ &&
         rare_data_->has_override_containing_block_content_logical_width_;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
bool LayoutBox::HasOverrideContainingBlockLogicalHeight() const {
  return rare_data_ &&
         rare_data_->has_override_containing_block_content_logical_height_;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
void LayoutBox::SetOverrideContainingBlockContentLogicalWidth(
    LayoutUnit logical_width) {
  DCHECK_GE(logical_width, LayoutUnit(-1));
  EnsureRareData().override_containing_block_content_logical_width_ =
      logical_width;
  EnsureRareData().has_override_containing_block_content_logical_width_ = true;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
void LayoutBox::SetOverrideContainingBlockContentLogicalHeight(
    LayoutUnit logical_height) {
  DCHECK_GE(logical_height, LayoutUnit(-1));
  EnsureRareData().override_containing_block_content_logical_height_ =
      logical_height;
  EnsureRareData().has_override_containing_block_content_logical_height_ = true;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
void LayoutBox::ClearContainingBlockOverrideSize() {
  if (!rare_data_)
    return;
  EnsureRareData().has_override_containing_block_content_logical_width_ = false;
  EnsureRareData().has_override_containing_block_content_logical_height_ =
      false;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
void LayoutBox::ClearOverrideContainingBlockContentLogicalHeight() {
  if (!rare_data_)
    return;
  EnsureRareData().has_override_containing_block_content_logical_height_ =
      false;
}

LayoutUnit LayoutBox::AdjustBorderBoxLogicalWidthForBoxSizing(
    float width) const {
  LayoutUnit borders_plus_padding = CollapsedBorderAndCSSPaddingLogicalWidth();
  LayoutUnit result(width);
  if (Style()->BoxSizing() == EBoxSizing::kContentBox)
    return result + borders_plus_padding;
  return std::max(result, borders_plus_padding);
}

LayoutUnit LayoutBox::AdjustBorderBoxLogicalHeightForBoxSizing(
    float height) const {
  LayoutUnit borders_plus_padding = CollapsedBorderAndCSSPaddingLogicalHeight();
  LayoutUnit result(height);
  if (Style()->BoxSizing() == EBoxSizing::kContentBox)
    return result + borders_plus_padding;
  return std::max(result, borders_plus_padding);
}

LayoutUnit LayoutBox::AdjustContentBoxLogicalWidthForBoxSizing(
    float width) const {
  LayoutUnit result(width);
  if (Style()->BoxSizing() == EBoxSizing::kBorderBox)
    result -= CollapsedBorderAndCSSPaddingLogicalWidth();
  return std::max(LayoutUnit(), result);
}

LayoutUnit LayoutBox::AdjustContentBoxLogicalHeightForBoxSizing(
    float height) const {
  LayoutUnit result(height);
  if (Style()->BoxSizing() == EBoxSizing::kBorderBox)
    result -= CollapsedBorderAndCSSPaddingLogicalHeight();
  return std::max(LayoutUnit(), result);
}

// Hit Testing
bool LayoutBox::NodeAtPoint(HitTestResult& result,
                            const HitTestLocation& location_in_container,
                            const LayoutPoint& accumulated_offset,
                            HitTestAction action) {
  LayoutPoint adjusted_location = accumulated_offset + Location();

  if (!IsEffectiveRootScroller(this)) {
    // Check if we need to do anything at all.
    // If we have clipping, then we can't have any spillout.
    LayoutRect overflow_box =
        HasOverflowClip() ? BorderBoxRect() : VisualOverflowRect();
    FlipForWritingMode(overflow_box);
    overflow_box.MoveBy(adjusted_location);
    if (!location_in_container.Intersects(overflow_box))
      return false;
  }

  bool should_hit_test_self = IsInSelfHitTestingPhase(action);

  if (should_hit_test_self && HasOverflowClip() &&
      HitTestOverflowControl(result, location_in_container, adjusted_location))
    return true;

  // TODO(pdr): We should also check for css clip in the !isSelfPaintingLayer
  //            case, similar to overflow clip below.
  bool skip_children = false;
  if (ShouldClipOverflow() && !HasSelfPaintingLayer()) {
    if (!location_in_container.Intersects(OverflowClipRect(
            adjusted_location, kExcludeOverlayScrollbarSizeForHitTesting))) {
      skip_children = true;
    } else if (Style()->HasBorderRadius()) {
      LayoutRect bounds_rect(adjusted_location, Size());
      skip_children = !location_in_container.Intersects(
          Style()->GetRoundedInnerBorderFor(bounds_rect));
    }
  }

  // TODO(pdr): We should also include checks for hit testing border radius at
  //            the layer level (see: crbug.com/568904).

  if (!skip_children &&
      HitTestChildren(result, location_in_container, adjusted_location, action))
    return true;

  if (Style()->HasBorderRadius() &&
      HitTestClippedOutByBorder(location_in_container, adjusted_location))
    return false;

  // Now hit test ourselves.
  if (should_hit_test_self &&
      VisibleToHitTestRequest(result.GetHitTestRequest())) {
    LayoutRect bounds_rect(adjusted_location, Size());
    if (location_in_container.Intersects(bounds_rect)) {
      UpdateHitTestResult(result,
                          FlipForWritingMode(location_in_container.Point() -
                                             ToLayoutSize(adjusted_location)));
      if (result.AddNodeToListBasedTestResult(NodeForHitTest(),
                                              location_in_container,
                                              bounds_rect) == kStopHitTesting)
        return true;
    }
  }

  return false;
}

bool LayoutBox::HitTestChildren(HitTestResult& result,
                                const HitTestLocation& location_in_container,
                                const LayoutPoint& accumulated_offset,
                                HitTestAction action) {
  for (LayoutObject* child = SlowLastChild(); child;
       child = child->PreviousSibling()) {
    if ((!child->HasLayer() ||
         !ToLayoutBoxModelObject(child)->Layer()->IsSelfPaintingLayer()) &&
        child->NodeAtPoint(result, location_in_container, accumulated_offset,
                           action))
      return true;
  }

  return false;
}

bool LayoutBox::HitTestClippedOutByBorder(
    const HitTestLocation& location_in_container,
    const LayoutPoint& border_box_location) const {
  LayoutRect border_rect = BorderBoxRect();
  border_rect.MoveBy(border_box_location);
  return !location_in_container.Intersects(
      Style()->GetRoundedBorderFor(border_rect));
}

void LayoutBox::Paint(const PaintInfo& paint_info,
                      const LayoutPoint& paint_offset) const {
  BoxPainter(*this).Paint(paint_info, paint_offset);
}

void LayoutBox::PaintBoxDecorationBackground(
    const PaintInfo& paint_info,
    const LayoutPoint& paint_offset) const {
  BoxPainter(*this).PaintBoxDecorationBackground(paint_info, paint_offset);
}

bool LayoutBox::GetBackgroundPaintedExtent(LayoutRect& painted_extent) const {
  DCHECK(StyleRef().HasBackground());

  // LayoutView is special in the sense that it expands to the whole canvas,
  // thus can't be handled by this function.
  DCHECK(!IsLayoutView());

  LayoutRect background_rect(BorderBoxRect());

  Color background_color = ResolveColor(CSSPropertyBackgroundColor);
  if (background_color.Alpha()) {
    painted_extent = background_rect;
    return true;
  }

  if (!Style()->BackgroundLayers().GetImage() ||
      Style()->BackgroundLayers().Next()) {
    painted_extent = background_rect;
    return true;
  }

  BackgroundImageGeometry geometry;
  // TODO(jchaffraix): This function should be rethought as it's called during
  // and outside of the paint phase. Potentially returning different results at
  // different phases.
  geometry.Calculate(*this, nullptr, nullptr, kGlobalPaintNormalPhase,
                     Style()->BackgroundLayers(), background_rect);
  if (geometry.HasNonLocalGeometry())
    return false;
  painted_extent = LayoutRect(geometry.DestRect());
  return true;
}

bool LayoutBox::BackgroundIsKnownToBeOpaqueInRect(
    const LayoutRect& local_rect) const {
  if (IsDocumentElement() || BackgroundStolenForBeingBody())
    return false;

  // If the element has appearance, it might be painted by theme.
  // We cannot be sure if theme paints the background opaque.
  // In this case it is safe to not assume opaqueness.
  // FIXME: May be ask theme if it paints opaque.
  if (Style()->HasAppearance())
    return false;
  // FIXME: Check the opaqueness of background images.

  // FIXME: Use rounded rect if border radius is present.
  if (Style()->HasBorderRadius())
    return false;
  if (HasClipPath())
    return false;
  if (Style()->HasBlendMode())
    return false;
  return BackgroundRect(kBackgroundKnownOpaqueRect).Contains(local_rect);
}

static bool IsCandidateForOpaquenessTest(const LayoutBox& child_box) {
  const ComputedStyle& child_style = child_box.StyleRef();
  if (child_style.GetPosition() != EPosition::kStatic &&
      child_box.ContainingBlock() != child_box.Parent())
    return false;
  if (child_style.Visibility() != EVisibility::kVisible ||
      child_style.ShapeOutside())
    return false;
  if (child_box.Size().IsZero())
    return false;
  if (PaintLayer* child_layer = child_box.Layer()) {
    // FIXME: perhaps this could be less conservative?
    if (child_layer->GetCompositingState() != kNotComposited)
      return false;
    // FIXME: Deal with z-index.
    if (child_style.IsStackingContext())
      return false;
    if (child_layer->HasTransformRelatedProperty() ||
        child_layer->IsTransparent() ||
        child_layer->HasFilterInducingProperty())
      return false;
    if (child_box.HasOverflowClip() && child_style.HasBorderRadius())
      return false;
  }
  return true;
}

bool LayoutBox::ForegroundIsKnownToBeOpaqueInRect(
    const LayoutRect& local_rect,
    unsigned max_depth_to_test) const {
  if (!max_depth_to_test)
    return false;
  for (LayoutObject* child = SlowFirstChild(); child;
       child = child->NextSibling()) {
    if (!child->IsBox())
      continue;
    LayoutBox* child_box = ToLayoutBox(child);
    if (!IsCandidateForOpaquenessTest(*child_box))
      continue;
    LayoutPoint child_location = child_box->Location();
    if (child_box->IsInFlowPositioned())
      child_location.Move(child_box->OffsetForInFlowPosition());
    LayoutRect child_local_rect = local_rect;
    child_local_rect.MoveBy(-child_location);
    if (child_local_rect.Y() < 0 || child_local_rect.X() < 0) {
      // If there is unobscured area above/left of a static positioned box then
      // the rect is probably not covered.
      if (!child_box->IsPositioned())
        return false;
      continue;
    }
    if (child_local_rect.MaxY() > child_box->Size().Height() ||
        child_local_rect.MaxX() > child_box->Size().Width())
      continue;
    if (child_box->BackgroundIsKnownToBeOpaqueInRect(child_local_rect))
      return true;
    if (child_box->ForegroundIsKnownToBeOpaqueInRect(child_local_rect,
                                                     max_depth_to_test - 1))
      return true;
  }
  return false;
}

DISABLE_CFI_PERF
bool LayoutBox::ComputeBackgroundIsKnownToBeObscured() const {
  if (ScrollsOverflow())
    return false;
  // Test to see if the children trivially obscure the background.
  if (!StyleRef().HasBackground())
    return false;
  // Root background painting is special.
  if (IsLayoutView())
    return false;
  // FIXME: box-shadow is painted while background painting.
  if (Style()->BoxShadow())
    return false;
  LayoutRect background_rect;
  if (!GetBackgroundPaintedExtent(background_rect))
    return false;
  return ForegroundIsKnownToBeOpaqueInRect(background_rect,
                                           kBackgroundObscurationTestMaxDepth);
}

void LayoutBox::PaintMask(const PaintInfo& paint_info,
                          const LayoutPoint& paint_offset) const {
  BoxPainter(*this).PaintMask(paint_info, paint_offset);
}

void LayoutBox::ImageChanged(WrappedImagePtr image, const IntRect*) {
  // TODO(chrishtr): support PaintInvalidationReason::kDelayedFull for animated
  // border images.
  if ((StyleRef().BorderImage().GetImage() &&
       StyleRef().BorderImage().GetImage()->Data() == image) ||
      (StyleRef().MaskBoxImage().GetImage() &&
       StyleRef().MaskBoxImage().GetImage()->Data() == image) ||
      (StyleRef().BoxReflect() && StyleRef().BoxReflect()->Mask().GetImage() &&
       StyleRef().BoxReflect()->Mask().GetImage()->Data() == image)) {
    SetShouldDoFullPaintInvalidationWithoutGeometryChange(
        PaintInvalidationReason::kImage);
  } else {
    for (const FillLayer* layer = &StyleRef().MaskLayers(); layer;
         layer = layer->Next()) {
      if (layer->GetImage() && image == layer->GetImage()->Data()) {
        SetShouldDoFullPaintInvalidationWithoutGeometryChange(
            PaintInvalidationReason::kImage);
        break;
      }
    }
  }

  if (!IsDocumentElement() && !BackgroundStolenForBeingBody()) {
    for (const FillLayer* layer = &StyleRef().BackgroundLayers(); layer;
         layer = layer->Next()) {
      if (layer->GetImage() && image == layer->GetImage()->Data()) {
        InvalidateBackgroundObscurationStatus();
        bool maybe_animated =
            layer->GetImage()->CachedImage() &&
            layer->GetImage()->CachedImage()->GetImage() &&
            layer->GetImage()->CachedImage()->GetImage()->MaybeAnimated();
        if (maybe_animated) {
          SetMayNeedPaintInvalidationAnimatedBackgroundImage();
        } else {
          SetShouldDoFullPaintInvalidationWithoutGeometryChange(
              PaintInvalidationReason::kImage);
          SetBackgroundChangedSinceLastPaintInvalidation();
        }
        break;
      }
    }
  }

  ShapeValue* shape_outside_value = Style()->ShapeOutside();
  if (!GetFrameView()->IsInPerformLayout() && IsFloating() &&
      shape_outside_value && shape_outside_value->GetImage() &&
      shape_outside_value->GetImage()->Data() == image) {
    ShapeOutsideInfo& info = ShapeOutsideInfo::EnsureInfo(*this);
    if (!info.IsComputingShape()) {
      info.MarkShapeAsDirty();
      MarkShapeOutsideDependentsForLayout();
    }
  }
}

ResourcePriority LayoutBox::ComputeResourcePriority() const {
  LayoutRect view_bounds = ViewRect();
  LayoutRect object_bounds = LayoutRect(AbsoluteContentBox());

  // The object bounds might be empty right now, so intersects will fail since
  // it doesn't deal with empty rects. Use LayoutRect::contains in that case.
  bool is_visible;
  if (!object_bounds.IsEmpty())
    is_visible = view_bounds.Intersects(object_bounds);
  else
    is_visible = view_bounds.Contains(object_bounds);

  LayoutRect screen_rect;
  if (!object_bounds.IsEmpty()) {
    screen_rect = view_bounds;
    screen_rect.Intersect(object_bounds);
  }

  int screen_area = 0;
  if (!screen_rect.IsEmpty() && is_visible)
    screen_area = (screen_rect.Width() * screen_rect.Height()).ToInt();
  return ResourcePriority(
      is_visible ? ResourcePriority::kVisible : ResourcePriority::kNotVisible,
      screen_area);
}

void LayoutBox::LocationChanged() {
  // The location may change because of layout of other objects. Should check
  // this object for paint invalidation.
  if (!NeedsLayout())
    SetMayNeedPaintInvalidation();
}

void LayoutBox::SizeChanged() {
  // The size may change because of layout of other objects. Should check this
  // object for paint invalidation.
  if (!NeedsLayout())
    SetMayNeedPaintInvalidation();

  if (GetNode() && GetNode()->IsElementNode()) {
    Element& element = ToElement(*GetNode());
    element.SetNeedsResizeObserverUpdate();
  }
}

bool LayoutBox::IntersectsVisibleViewport() const {
  LayoutRect rect = VisualOverflowRect();
  LayoutView* layout_view = View();
  while (!layout_view->GetFrame()->OwnerLayoutItem().IsNull())
    layout_view = LayoutAPIShim::LayoutObjectFrom(
                      layout_view->GetFrame()->OwnerLayoutItem())
                      ->View();
  MapToVisualRectInAncestorSpace(layout_view, rect);
  return rect.Intersects(LayoutRect(
      layout_view->GetFrameView()->GetScrollableArea()->VisibleContentRect()));
}

void LayoutBox::EnsureIsReadyForPaintInvalidation() {
  LayoutBoxModelObject::EnsureIsReadyForPaintInvalidation();

  if (MayNeedPaintInvalidationAnimatedBackgroundImage() &&
      !BackgroundIsKnownToBeObscured()) {
    SetShouldDoFullPaintInvalidationWithoutGeometryChange(
        PaintInvalidationReason::kDelayedFull);
  }

  if (FullPaintInvalidationReason() != PaintInvalidationReason::kDelayedFull ||
      !IntersectsVisibleViewport())
    return;

  // Do regular full paint invalidation if the object with
  // PaintInvalidationReason::kDelayedFull is onscreen.
  // Conservatively assume the delayed paint invalidation was caused by
  // background image change.
  SetBackgroundChangedSinceLastPaintInvalidation();
  SetShouldDoFullPaintInvalidationWithoutGeometryChange(
      PaintInvalidationReason::kFull);
}

PaintInvalidationReason LayoutBox::DeprecatedInvalidatePaint(
    const PaintInvalidationState& paint_invalidation_state) {
  if (HasBoxDecorationBackground()
      // We also paint overflow controls in background phase.
      || (HasOverflowClip() && GetScrollableArea()->HasOverflowControls())) {
    PaintLayer& layer = paint_invalidation_state.PaintingLayer();
    if (&layer.GetLayoutObject() != this)
      layer.SetNeedsPaintPhaseDescendantBlockBackgrounds();
  }

  return LayoutBoxModelObject::DeprecatedInvalidatePaint(
      paint_invalidation_state);
}

PaintInvalidationReason LayoutBox::InvalidatePaint(
    const PaintInvalidatorContext& context) const {
  return BoxPaintInvalidator(*this, context).InvalidatePaint();
}

LayoutRect LayoutBox::OverflowClipRect(
    const LayoutPoint& location,
    OverlayScrollbarClipBehavior overlay_scrollbar_clip_behavior) const {
  if (IsEffectiveRootScroller(this))
    return View()->ViewRect();

  // FIXME: When overflow-clip (CSS3) is implemented, we'll obtain the property
  // here.
  LayoutRect clip_rect = BorderBoxRect();
  clip_rect.SetLocation(location + clip_rect.Location() +
                        LayoutSize(BorderLeft(), BorderTop()));
  clip_rect.SetSize(clip_rect.Size() -
                    LayoutSize(BorderWidth(), BorderHeight()));

  if (HasOverflowClip())
    ExcludeScrollbars(clip_rect, overlay_scrollbar_clip_behavior);

  if (HasControlClip())
    clip_rect.Intersect(ControlClipRect(location));

  return clip_rect;
}

void LayoutBox::ExcludeScrollbars(
    LayoutRect& rect,
    OverlayScrollbarClipBehavior overlay_scrollbar_clip_behavior) const {
  if (PaintLayerScrollableArea* scrollable_area = this->GetScrollableArea()) {
    if (ShouldPlaceBlockDirectionScrollbarOnLogicalLeft()) {
      rect.Move(scrollable_area->VerticalScrollbarWidth(
                    overlay_scrollbar_clip_behavior),
                0);
    }
    rect.Contract(scrollable_area->VerticalScrollbarWidth(
                      overlay_scrollbar_clip_behavior),
                  scrollable_area->HorizontalScrollbarHeight(
                      overlay_scrollbar_clip_behavior));
  }
}

LayoutRect LayoutBox::ClipRect(const LayoutPoint& location) const {
  LayoutRect border_box_rect = this->BorderBoxRect();
  LayoutRect clip_rect =
      LayoutRect(border_box_rect.Location() + location, border_box_rect.Size());

  if (!Style()->ClipLeft().IsAuto()) {
    LayoutUnit c = ValueForLength(Style()->ClipLeft(), border_box_rect.Width());
    clip_rect.Move(c, LayoutUnit());
    clip_rect.Contract(c, LayoutUnit());
  }

  if (!Style()->ClipRight().IsAuto())
    clip_rect.Contract(
        Size().Width() - ValueForLength(Style()->ClipRight(), Size().Width()),
        LayoutUnit());

  if (!Style()->ClipTop().IsAuto()) {
    LayoutUnit c = ValueForLength(Style()->ClipTop(), border_box_rect.Height());
    clip_rect.Move(LayoutUnit(), c);
    clip_rect.Contract(LayoutUnit(), c);
  }

  if (!Style()->ClipBottom().IsAuto()) {
    clip_rect.Contract(LayoutUnit(),
                       Size().Height() - ValueForLength(Style()->ClipBottom(),
                                                        Size().Height()));
  }

  return clip_rect;
}

static LayoutUnit PortionOfMarginNotConsumedByFloat(LayoutUnit child_margin,
                                                    LayoutUnit content_side,
                                                    LayoutUnit offset) {
  if (child_margin <= 0)
    return LayoutUnit();
  LayoutUnit content_side_with_margin = content_side + child_margin;
  if (offset > content_side_with_margin)
    return child_margin;
  return offset - content_side;
}

LayoutUnit LayoutBox::ShrinkLogicalWidthToAvoidFloats(
    LayoutUnit child_margin_start,
    LayoutUnit child_margin_end,
    const LayoutBlockFlow* cb) const {
  LayoutUnit logical_top_position = LogicalTop();
  LayoutUnit start_offset_for_content = cb->StartOffsetForContent();
  LayoutUnit end_offset_for_content = cb->EndOffsetForContent();
  LayoutUnit logical_height = cb->LogicalHeightForChild(*this);
  LayoutUnit start_offset_for_line = cb->StartOffsetForLine(
      logical_top_position, kDoNotIndentText, logical_height);
  LayoutUnit end_offset_for_line = cb->EndOffsetForLine(
      logical_top_position, kDoNotIndentText, logical_height);

  // If there aren't any floats constraining us then allow the margins to
  // shrink/expand the width as much as they want.
  if (start_offset_for_content == start_offset_for_line &&
      end_offset_for_content == end_offset_for_line)
    return cb->AvailableLogicalWidthForLine(logical_top_position,
                                            kDoNotIndentText, logical_height) -
           child_margin_start - child_margin_end;

  LayoutUnit width =
      cb->AvailableLogicalWidthForLine(logical_top_position, kDoNotIndentText,
                                       logical_height) -
      std::max(LayoutUnit(), child_margin_start) -
      std::max(LayoutUnit(), child_margin_end);
  // We need to see if margins on either the start side or the end side can
  // contain the floats in question. If they can, then just using the line width
  // is inaccurate. In the case where a float completely fits, we don't need to
  // use the line offset at all, but can instead push all the way to the content
  // edge of the containing block. In the case where the float doesn't fit, we
  // can use the line offset, but we need to grow it by the margin to reflect
  // the fact that the margin was "consumed" by the float. Negative margins
  // aren't consumed by the float, and so we ignore them.
  width += PortionOfMarginNotConsumedByFloat(
      child_margin_start, start_offset_for_content, start_offset_for_line);
  width += PortionOfMarginNotConsumedByFloat(
      child_margin_end, end_offset_for_content, end_offset_for_line);
  return width;
}

LayoutUnit LayoutBox::ContainingBlockLogicalHeightForGetComputedStyle() const {
  if (HasOverrideContainingBlockLogicalHeight())
    return OverrideContainingBlockContentLogicalHeight();

  if (!IsPositioned())
    return ContainingBlockLogicalHeightForContent(kExcludeMarginBorderPadding);

  LayoutBoxModelObject* cb = ToLayoutBoxModelObject(Container());
  LayoutUnit height = ContainingBlockLogicalHeightForPositioned(cb);
  if (StyleRef().GetPosition() != EPosition::kAbsolute)
    height -= cb->PaddingLogicalHeight();
  return height;
}

LayoutUnit LayoutBox::ContainingBlockLogicalWidthForContent() const {
  if (HasOverrideContainingBlockLogicalWidth())
    return OverrideContainingBlockContentLogicalWidth();

  LayoutBlock* cb = ContainingBlock();
  if (IsOutOfFlowPositioned())
    return cb->ClientLogicalWidth();
  return cb->AvailableLogicalWidth();
}

LayoutUnit LayoutBox::ContainingBlockLogicalHeightForContent(
    AvailableLogicalHeightType height_type) const {
  if (HasOverrideContainingBlockLogicalHeight())
    return OverrideContainingBlockContentLogicalHeight();

  LayoutBlock* cb = ContainingBlock();
  return cb->AvailableLogicalHeight(height_type);
}

LayoutUnit LayoutBox::ContainingBlockAvailableLineWidth() const {
  LayoutBlock* cb = ContainingBlock();
  if (cb->IsLayoutBlockFlow())
    return ToLayoutBlockFlow(cb)->AvailableLogicalWidthForLine(
        LogicalTop(), kDoNotIndentText,
        AvailableLogicalHeight(kIncludeMarginBorderPadding));
  return LayoutUnit();
}

LayoutUnit LayoutBox::PerpendicularContainingBlockLogicalHeight() const {
  if (HasOverrideContainingBlockLogicalHeight())
    return OverrideContainingBlockContentLogicalHeight();

  LayoutBlock* cb = ContainingBlock();
  if (cb->HasOverrideLogicalContentHeight())
    return cb->OverrideLogicalContentHeight();

  const ComputedStyle& containing_block_style = cb->StyleRef();
  Length logical_height_length = containing_block_style.LogicalHeight();

  // FIXME: For now just support fixed heights.  Eventually should support
  // percentage heights as well.
  if (!logical_height_length.IsFixed()) {
    LayoutUnit fill_fallback_extent =
        LayoutUnit(containing_block_style.IsHorizontalWritingMode()
                       ? View()->GetFrameView()->VisibleContentSize().Height()
                       : View()->GetFrameView()->VisibleContentSize().Width());
    LayoutUnit fill_available_extent =
        ContainingBlock()->AvailableLogicalHeight(kExcludeMarginBorderPadding);
    if (fill_available_extent == -1)
      return fill_fallback_extent;
    return std::min(fill_available_extent, fill_fallback_extent);
  }

  // Use the content box logical height as specified by the style.
  return cb->AdjustContentBoxLogicalHeightForBoxSizing(
      LayoutUnit(logical_height_length.Value()));
}

void LayoutBox::MapLocalToAncestor(const LayoutBoxModelObject* ancestor,
                                   TransformState& transform_state,
                                   MapCoordinatesFlags mode) const {
  bool is_fixed_pos = Style()->GetPosition() == EPosition::kFixed;

  // If this box has a transform or contains paint, it acts as a fixed position
  // container for fixed descendants, and may itself also be fixed position. So
  // propagate 'fixed' up only if this box is fixed position.
  if (CanContainFixedPositionObjects() && !is_fixed_pos)
    mode &= ~kIsFixed;
  else if (is_fixed_pos)
    mode |= kIsFixed;

  LayoutBoxModelObject::MapLocalToAncestor(ancestor, transform_state, mode);
}

void LayoutBox::MapAncestorToLocal(const LayoutBoxModelObject* ancestor,
                                   TransformState& transform_state,
                                   MapCoordinatesFlags mode) const {
  if (this == ancestor)
    return;

  bool is_fixed_pos = Style()->GetPosition() == EPosition::kFixed;

  // If this box has a transform or contains paint, it acts as a fixed position
  // container for fixed descendants, and may itself also be fixed position. So
  // propagate 'fixed' up only if this box is fixed position.
  if (CanContainFixedPositionObjects() && !is_fixed_pos)
    mode &= ~kIsFixed;
  else if (is_fixed_pos)
    mode |= kIsFixed;

  LayoutBoxModelObject::MapAncestorToLocal(ancestor, transform_state, mode);
}

LayoutSize LayoutBox::OffsetFromContainer(const LayoutObject* o) const {
  DCHECK_EQ(o, Container());

  LayoutSize offset;
  if (IsInFlowPositioned())
    offset += OffsetForInFlowPosition();

  offset += PhysicalLocationOffset();

  if (o->HasOverflowClip())
    offset -= ToLayoutBox(o)->ScrolledContentOffset();

  if (Style()->GetPosition() == EPosition::kAbsolute &&
      o->IsInFlowPositioned() && o->IsLayoutInline())
    offset += ToLayoutInline(o)->OffsetForInFlowPositionedInline(*this);

  return offset;
}

InlineBox* LayoutBox::CreateInlineBox() {
  return new InlineBox(LineLayoutItem(this));
}

void LayoutBox::DirtyLineBoxes(bool full_layout) {
  if (inline_box_wrapper_) {
    if (full_layout) {
      inline_box_wrapper_->Destroy();
      inline_box_wrapper_ = nullptr;
    } else {
      inline_box_wrapper_->DirtyLineBoxes();
    }
  }
}

void LayoutBox::PositionLineBox(InlineBox* box) {
  if (IsOutOfFlowPositioned()) {
    // Cache the x position only if we were an INLINE type originally.
    bool originally_inline = Style()->IsOriginalDisplayInlineType();
    if (originally_inline) {
      // The value is cached in the xPos of the box.  We only need this value if
      // our object was inline originally, since otherwise it would have ended
      // up underneath the inlines.
      RootInlineBox& root = box->Root();
      root.Block().SetStaticInlinePositionForChild(LineLayoutBox(this),
                                                   box->LogicalLeft());
    } else {
      // Our object was a block originally, so we make our normal flow position
      // be just below the line box (as though all the inlines that came before
      // us got wrapped in an anonymous block, which is what would have happened
      // had we been in flow). This value was cached in the y() of the box.
      Layer()->SetStaticBlockPosition(box->LogicalTop());
    }

    if (Container()->IsLayoutInline())
      MoveWithEdgeOfInlineContainerIfNecessary(box->IsHorizontal());

    // Nuke the box.
    box->Remove(kDontMarkLineBoxes);
    box->Destroy();
  } else if (IsAtomicInlineLevel()) {
    SetLocationAndUpdateOverflowControlsIfNeeded(box->Location());
    SetInlineBoxWrapper(box);
  }
}

void LayoutBox::MoveWithEdgeOfInlineContainerIfNecessary(bool is_horizontal) {
  DCHECK(IsOutOfFlowPositioned());
  DCHECK(Container()->IsLayoutInline());
  DCHECK(Container()->IsInFlowPositioned());
  // If this object is inside a relative positioned inline and its inline
  // position is an explicit offset from the edge of its container then it will
  // need to move if its inline container has changed width. We do not track if
  // the width has changed but if we are here then we are laying out lines
  // inside it, so it probably has - mark our object for layout so that it can
  // move to the new offset created by the new width.
  if (!NormalChildNeedsLayout() &&
      !Style()->HasStaticInlinePosition(is_horizontal))
    SetChildNeedsLayout(kMarkOnlyThis);
}

void LayoutBox::DeleteLineBoxWrapper() {
  if (inline_box_wrapper_) {
    if (!DocumentBeingDestroyed())
      inline_box_wrapper_->Remove();
    inline_box_wrapper_->Destroy();
    inline_box_wrapper_ = nullptr;
  }
}

void LayoutBox::SetSpannerPlaceholder(
    LayoutMultiColumnSpannerPlaceholder& placeholder) {
  // Not expected to change directly from one spanner to another.
  CHECK(!rare_data_ || !rare_data_->spanner_placeholder_);
  EnsureRareData().spanner_placeholder_ = &placeholder;
}

void LayoutBox::ClearSpannerPlaceholder() {
  if (!rare_data_)
    return;
  rare_data_->spanner_placeholder_ = nullptr;
}

void LayoutBox::SetPaginationStrut(LayoutUnit strut) {
  if (!strut && !rare_data_)
    return;
  EnsureRareData().pagination_strut_ = strut;
}

bool LayoutBox::IsBreakBetweenControllable(EBreakBetween break_value) const {
  if (break_value == EBreakBetween::kAuto)
    return true;
  // We currently only support non-auto break-before and break-after values on
  // in-flow block level elements, which is the minimum requirement according to
  // the spec.
  if (IsInline() || IsFloatingOrOutOfFlowPositioned())
    return false;
  const LayoutBlock* curr = ContainingBlock();
  if (!curr || !curr->IsLayoutBlockFlow())
    return false;
  const LayoutView* layout_view = View();
  bool view_is_paginated = layout_view->FragmentationContext();
  if (!view_is_paginated && !FlowThreadContainingBlock())
    return false;
  while (curr) {
    if (curr == layout_view) {
      return view_is_paginated && break_value != EBreakBetween::kColumn &&
             break_value != EBreakBetween::kAvoidColumn;
    }
    if (curr->IsLayoutFlowThread()) {
      if (break_value ==
          EBreakBetween::kAvoid)  // Valid in any kind of fragmentation context.
        return true;
      bool is_multicol_value = break_value == EBreakBetween::kColumn ||
                               break_value == EBreakBetween::kAvoidColumn;
      if (ToLayoutFlowThread(curr)->IsLayoutPagedFlowThread())
        return !is_multicol_value;
      if (is_multicol_value)
        return true;
      // If this is a flow thread for a multicol container, and we have a break
      // value for paged, we need to keep looking.
    }
    if (curr->IsOutOfFlowPositioned())
      return false;
    curr = curr->ContainingBlock();
  }
  NOTREACHED();
  return false;
}

bool LayoutBox::IsBreakInsideControllable(EBreakInside break_value) const {
  if (break_value == EBreakInside::kAuto)
    return true;
  // First check multicol.
  const LayoutFlowThread* flow_thread = FlowThreadContainingBlock();
  // 'avoid-column' is only valid in a multicol context.
  if (break_value == EBreakInside::kAvoidColumn)
    return flow_thread && !flow_thread->IsLayoutPagedFlowThread();
  // 'avoid' is valid in any kind of fragmentation context.
  if (break_value == EBreakInside::kAvoid && flow_thread)
    return true;
  DCHECK(break_value == EBreakInside::kAvoidPage ||
         break_value == EBreakInside::kAvoid);
  if (View()->FragmentationContext())
    return true;  // The view is paginated, probably because we're printing.
  if (!flow_thread)
    return false;  // We're not inside any pagination context
  // We're inside a flow thread. We need to be contained by a flow thread for
  // paged overflow in order for pagination values to be valid, though.
  for (const LayoutBlock* ancestor = flow_thread; ancestor;
       ancestor = ancestor->ContainingBlock()) {
    if (ancestor->IsLayoutFlowThread() &&
        ToLayoutFlowThread(ancestor)->IsLayoutPagedFlowThread())
      return true;
  }
  return false;
}

EBreakBetween LayoutBox::BreakAfter() const {
  EBreakBetween break_value = Style()->BreakAfter();
  if (break_value == EBreakBetween::kAuto ||
      IsBreakBetweenControllable(break_value))
    return break_value;
  return EBreakBetween::kAuto;
}

EBreakBetween LayoutBox::BreakBefore() const {
  EBreakBetween break_value = Style()->BreakBefore();
  if (break_value == EBreakBetween::kAuto ||
      IsBreakBetweenControllable(break_value))
    return break_value;
  return EBreakBetween::kAuto;
}

EBreakInside LayoutBox::BreakInside() const {
  EBreakInside break_value = Style()->BreakInside();
  if (break_value == EBreakInside::kAuto ||
      IsBreakInsideControllable(break_value))
    return break_value;
  return EBreakInside::kAuto;
}

// At a class A break point [1], the break value with the highest precedence
// wins. If the two values have the same precedence (e.g. "left" and "right"),
// the value specified on a latter object wins.
//
// [1] https://drafts.csswg.org/css-break/#possible-breaks
static inline int FragmentainerBreakPrecedence(EBreakBetween break_value) {
  // "auto" has the lowest priority.
  // "avoid*" values win over "auto".
  // "avoid-page" wins over "avoid-column".
  // "avoid" wins over "avoid-page".
  // Forced break values win over "avoid".
  // Any forced page break value wins over "column" forced break.
  // More specific break values (left, right, recto, verso) wins over generic
  // "page" values.

  switch (break_value) {
    default:
      NOTREACHED();
    // fall-through
    case EBreakBetween::kAuto:
      return 0;
    case EBreakBetween::kAvoidColumn:
      return 1;
    case EBreakBetween::kAvoidPage:
      return 2;
    case EBreakBetween::kAvoid:
      return 3;
    case EBreakBetween::kColumn:
      return 4;
    case EBreakBetween::kPage:
      return 5;
    case EBreakBetween::kLeft:
    case EBreakBetween::kRight:
    case EBreakBetween::kRecto:
    case EBreakBetween::kVerso:
      return 6;
  }
}

EBreakBetween LayoutBox::JoinFragmentainerBreakValues(
    EBreakBetween first_value,
    EBreakBetween second_value) {
  if (FragmentainerBreakPrecedence(second_value) >=
      FragmentainerBreakPrecedence(first_value))
    return second_value;
  return first_value;
}

EBreakBetween LayoutBox::ClassABreakPointValue(
    EBreakBetween previous_break_after_value) const {
  // First assert that we're at a class A break point.
  DCHECK(IsBreakBetweenControllable(previous_break_after_value));

  return JoinFragmentainerBreakValues(previous_break_after_value,
                                      BreakBefore());
}

bool LayoutBox::NeedsForcedBreakBefore(
    EBreakBetween previous_break_after_value) const {
  // Forced break values are only honored when specified on in-flow objects, but
  // floats and out-of-flow positioned objects may be affected by a break-after
  // value of the previous in-flow object, even though we're not at a class A
  // break point.
  EBreakBetween break_value =
      IsFloatingOrOutOfFlowPositioned()
          ? previous_break_after_value
          : ClassABreakPointValue(previous_break_after_value);
  return IsForcedFragmentainerBreakValue(break_value);
}

bool LayoutBox::PaintedOutputOfObjectHasNoEffectRegardlessOfSize() const {
  if (HasNonCompositedScrollbars() || GetSelectionState() != SelectionNone ||
      HasBoxDecorationBackground() || StyleRef().HasBoxDecorations() ||
      StyleRef().HasVisualOverflowingEffect())
    return false;

  // Both mask and clip-path generates drawing display items that depends on
  // the size of the box.
  if (HasMask() || HasClipPath())
    return false;

  // If the box has any kind of clip, we need issue paint invalidation to cover
  // the changed part of children when the box got resized. In SPv2 this is
  // handled by detecting paint property changes.
  if (!RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    if (HasClipRelatedProperty())
      return false;
  }

  // If the box paints into its own backing, we can assume that it's painting
  // may have some effect. For example, honoring the border-radius clip on
  // a composited child paints into a mask for an otherwise non-painting
  // element, because children of that element will require the mask.
  if (HasLayer() && Layer()->GetCompositingState() == kPaintsIntoOwnBacking)
    return false;

  return true;
}

LayoutRect LayoutBox::LocalVisualRect() const {
  if (Style()->Visibility() != EVisibility::kVisible)
    return LayoutRect();

  if (HasMask() && !ShouldClipOverflow() &&
      !RuntimeEnabledFeatures::slimmingPaintV2Enabled())
    return LayoutRect(Layer()->BoxForFilterOrMask());

  return SelfVisualOverflowRect();
}

void LayoutBox::InflateVisualRectForFilterUnderContainer(
    TransformState& transform_state,
    const LayoutObject& container,
    const LayoutBoxModelObject* ancestor_to_stop_at) const {
  transform_state.Flatten();
  // Apply visual overflow caused by reflections and filters defined on objects
  // between this object and container (not included) or ancestorToStopAt
  // (included).
  LayoutSize offset_from_container = this->OffsetFromContainer(&container);
  transform_state.Move(offset_from_container);
  for (LayoutObject* parent = this->Parent(); parent && parent != container;
       parent = parent->Parent()) {
    if (parent->IsBox()) {
      // Convert rect into coordinate space of parent to apply parent's
      // reflection and filter.
      LayoutSize parent_offset =
          parent->OffsetFromAncestorContainer(&container);
      transform_state.Move(-parent_offset);
      ToLayoutBox(parent)->InflateVisualRectForFilter(transform_state);
      transform_state.Move(parent_offset);
    }
    if (parent == ancestor_to_stop_at)
      break;
  }
  transform_state.Move(-offset_from_container);
}

bool LayoutBox::MapToVisualRectInAncestorSpaceInternal(
    const LayoutBoxModelObject* ancestor,
    TransformState& transform_state,
    VisualRectFlags visual_rect_flags) const {
  InflateVisualRectForFilter(transform_state);

  if (ancestor == this)
    return true;

  AncestorSkipInfo skip_info(ancestor, true);
  LayoutObject* container = this->Container(&skip_info);
  LayoutBox* table_row_container = nullptr;
  // Skip table row because cells and rows are in the same coordinate space (see
  // below, however for more comments about when |ancestor| is the table row).
  if (IsTableCell()) {
    DCHECK(container->IsTableRow());
    DCHECK_EQ(ParentBox(), container);
    if (container != ancestor)
      container = container->Parent();
    else
      table_row_container = ToLayoutBox(container);
  }
  if (!container)
    return true;

  LayoutPoint container_offset;
  if (container->IsBox()) {
    container_offset.MoveBy(PhysicalLocation(ToLayoutBox(container)));

    // If the row is the ancestor, however, add its offset back in. In effect,
    // this passes from the joint <td> / <tr> coordinate space to the parent
    // space, then back to <tr> / <td>.
    if (table_row_container) {
      container_offset.MoveBy(
          -table_row_container->PhysicalLocation(ToLayoutBox(container)));
    }
  } else if (container->IsRuby()) {
    // TODO(wkorman): Generalize Ruby specialization and/or document more
    // clearly. See the accompanying specialization in
    // LayoutInline::mapToVisualRectInAncestorSpaceInternal.
    container_offset.MoveBy(PhysicalLocation());
  } else {
    container_offset.MoveBy(Location());
  }

  const ComputedStyle& style_to_use = StyleRef();
  EPosition position = style_to_use.GetPosition();
  if (position == EPosition::kAbsolute && container->IsInFlowPositioned() &&
      container->IsLayoutInline()) {
    container_offset.Move(
        ToLayoutInline(container)->OffsetForInFlowPositionedInline(*this));
  } else if (style_to_use.HasInFlowPosition() && Layer()) {
    // Apply the relative position offset when invalidating a rectangle. The
    // layer is translated, but the layout box isn't, so we need to do this to
    // get the right dirty rect.  Since this is called from
    // LayoutObject::setStyle, the relative position flag on the LayoutObject
    // has been cleared, so use the one on the style().
    container_offset.Move(Layer()->OffsetForInFlowPosition());
  }

  if (skip_info.FilterSkipped()) {
    InflateVisualRectForFilterUnderContainer(transform_state, *container,
                                             ancestor);
  }

  if (!MapVisualRectToContainer(container, container_offset, ancestor,
                                visual_rect_flags, transform_state))
    return false;

  if (skip_info.AncestorSkipped()) {
    bool preserve3D = container->Style()->Preserves3D();
    TransformState::TransformAccumulation accumulation =
        preserve3D ? TransformState::kAccumulateTransform
                   : TransformState::kFlattenTransform;

    // If the ancestor is below the container, then we need to map the rect into
    // ancestor's coordinates.
    LayoutSize container_offset =
        ancestor->OffsetFromAncestorContainer(container);
    transform_state.Move(-container_offset, accumulation);
    // If the ancestor is fixed, then the rect is already in its coordinates so
    // doesn't need viewport-adjusting.
    if (ancestor->Style()->GetPosition() != EPosition::kFixed &&
        container->IsLayoutView() && position == EPosition::kFixed) {
      transform_state.Move(
          ToLayoutView(container)->OffsetForFixedPosition(true), accumulation);
    }
    return true;
  }

  if (container->IsLayoutView())
    return ToLayoutView(container)->MapToVisualRectInAncestorSpaceInternal(
        ancestor, transform_state, position == EPosition::kFixed ? kIsFixed : 0,
        visual_rect_flags);
  else
    return container->MapToVisualRectInAncestorSpaceInternal(
        ancestor, transform_state, visual_rect_flags);
}

void LayoutBox::InflateVisualRectForFilter(
    TransformState& transform_state) const {
  if (!Layer() || !Layer()->PaintsWithFilters())
    return;

  transform_state.Flatten();
  LayoutRect rect(transform_state.LastPlanarQuad().BoundingBox());
  transform_state.SetQuad(
      FloatQuad(FloatRect(Layer()->MapLayoutRectForFilter(rect))));
}

void LayoutBox::UpdateLogicalWidth() {
  LogicalExtentComputedValues computed_values;
  ComputeLogicalWidth(computed_values);

  SetLogicalWidth(computed_values.extent_);
  SetLogicalLeft(computed_values.position_);
  SetMarginStart(computed_values.margins_.start_);
  SetMarginEnd(computed_values.margins_.end_);
}

static float GetMaxWidthListMarker(const LayoutBox* layout_object) {
#if DCHECK_IS_ON()
  DCHECK(layout_object);
  Node* parent_node = layout_object->GeneratingNode();
  DCHECK(parent_node);
  DCHECK(isHTMLOListElement(parent_node) || isHTMLUListElement(parent_node));
  DCHECK_NE(layout_object->Style()->TextAutosizingMultiplier(), 1);
#endif
  float max_width = 0;
  for (LayoutObject* child = layout_object->SlowFirstChild(); child;
       child = child->NextSibling()) {
    if (!child->IsListItem())
      continue;

    LayoutBox* list_item = ToLayoutBox(child);
    for (LayoutObject* item_child = list_item->SlowFirstChild(); item_child;
         item_child = item_child->NextSibling()) {
      if (!item_child->IsListMarker())
        continue;
      LayoutBox* item_marker = ToLayoutBox(item_child);
      // Make sure to compute the autosized width.
      if (item_marker->NeedsLayout())
        item_marker->UpdateLayout();
      max_width = std::max<float>(
          max_width, ToLayoutListMarker(item_marker)->LogicalWidth().ToFloat());
      break;
    }
  }
  return max_width;
}

DISABLE_CFI_PERF
void LayoutBox::ComputeLogicalWidth(
    LogicalExtentComputedValues& computed_values) const {
  computed_values.extent_ =
      Style()->ContainsSize() ? BorderAndPaddingLogicalWidth() : LogicalWidth();
  computed_values.position_ = LogicalLeft();
  computed_values.margins_.start_ = MarginStart();
  computed_values.margins_.end_ = MarginEnd();

  // The parent box is flexing us, so it has increased or decreased our
  // width.  Use the width from the style context.
  if (HasOverrideLogicalContentWidth()) {
    computed_values.extent_ =
        OverrideLogicalContentWidth() + BorderAndPaddingLogicalWidth();
    return;
  }

  if (IsOutOfFlowPositioned()) {
    ComputePositionedLogicalWidth(computed_values);
    return;
  }

  // FIXME: Account for writing-mode in flexible boxes.
  // https://bugs.webkit.org/show_bug.cgi?id=46418
  bool in_vertical_box = Parent()->IsDeprecatedFlexibleBox() &&
                         (Parent()->Style()->BoxOrient() == VERTICAL);
  bool stretching = (Parent()->Style()->BoxAlign() == BSTRETCH);
  // TODO (lajava): Stretching is the only reason why we don't want the box to
  // be treated as a replaced element, so we could perhaps refactor all this
  // logic, not only for flex and grid since alignment is intended to be applied
  // to any block.
  bool treat_as_replaced = ShouldComputeSizeAsReplaced() &&
                           (!in_vertical_box || !stretching) &&
                           (!IsGridItem() || !HasStretchedLogicalWidth());
  const ComputedStyle& style_to_use = StyleRef();
  Length logical_width_length =
      treat_as_replaced ? Length(ComputeReplacedLogicalWidth(), kFixed)
                        : style_to_use.LogicalWidth();

  LayoutBlock* cb = ContainingBlock();
  LayoutUnit container_logical_width =
      std::max(LayoutUnit(), ContainingBlockLogicalWidthForContent());
  bool has_perpendicular_containing_block =
      cb->IsHorizontalWritingMode() != IsHorizontalWritingMode();

  if (IsInline() && !IsInlineBlockOrInlineTable()) {
    // just calculate margins
    computed_values.margins_.start_ = MinimumValueForLength(
        style_to_use.MarginStart(), container_logical_width);
    computed_values.margins_.end_ = MinimumValueForLength(
        style_to_use.MarginEnd(), container_logical_width);
    if (treat_as_replaced)
      computed_values.extent_ =
          std::max(LayoutUnit(FloatValueForLength(logical_width_length, 0)) +
                       BorderAndPaddingLogicalWidth(),
                   MinPreferredLogicalWidth());
    return;
  }

  LayoutUnit container_width_in_inline_direction = container_logical_width;
  if (has_perpendicular_containing_block)
    container_width_in_inline_direction =
        PerpendicularContainingBlockLogicalHeight();

  // Width calculations
  if (treat_as_replaced) {
    computed_values.extent_ = LayoutUnit(logical_width_length.Value()) +
                              BorderAndPaddingLogicalWidth();
  } else {
    LayoutUnit preferred_width = ComputeLogicalWidthUsing(
        kMainOrPreferredSize, style_to_use.LogicalWidth(),
        container_width_in_inline_direction, cb);
    computed_values.extent_ = ConstrainLogicalWidthByMinMax(
        preferred_width, container_width_in_inline_direction, cb);
  }

  // Margin calculations.
  ComputeMarginsForDirection(
      kInlineDirection, cb, container_logical_width, computed_values.extent_,
      computed_values.margins_.start_, computed_values.margins_.end_,
      Style()->MarginStart(), Style()->MarginEnd());

  if (!has_perpendicular_containing_block && container_logical_width &&
      container_logical_width !=
          (computed_values.extent_ + computed_values.margins_.start_ +
           computed_values.margins_.end_) &&
      !IsFloating() && !IsInline() && !cb->IsFlexibleBoxIncludingDeprecated() &&
      !cb->IsLayoutGrid()) {
    LayoutUnit new_margin_total =
        container_logical_width - computed_values.extent_;
    bool has_inverted_direction = cb->Style()->IsLeftToRightDirection() !=
                                  Style()->IsLeftToRightDirection();
    if (has_inverted_direction) {
      computed_values.margins_.start_ =
          new_margin_total - computed_values.margins_.end_;
    } else {
      computed_values.margins_.end_ =
          new_margin_total - computed_values.margins_.start_;
    }
  }

  if (style_to_use.TextAutosizingMultiplier() != 1 &&
      style_to_use.MarginStart().GetType() == kFixed) {
    Node* parent_node = GeneratingNode();
    if (parent_node && (isHTMLOListElement(*parent_node) ||
                        isHTMLUListElement(*parent_node))) {
      // Make sure the markers in a list are properly positioned (i.e. not
      // chopped off) when autosized.
      const float adjusted_margin =
          (1 - 1.0 / style_to_use.TextAutosizingMultiplier()) *
          GetMaxWidthListMarker(this);
      bool has_inverted_direction = cb->Style()->IsLeftToRightDirection() !=
                                    Style()->IsLeftToRightDirection();
      if (has_inverted_direction)
        computed_values.margins_.end_ += adjusted_margin;
      else
        computed_values.margins_.start_ += adjusted_margin;
    }
  }
}

LayoutUnit LayoutBox::FillAvailableMeasure(
    LayoutUnit available_logical_width) const {
  LayoutUnit margin_start;
  LayoutUnit margin_end;
  return FillAvailableMeasure(available_logical_width, margin_start,
                              margin_end);
}

LayoutUnit LayoutBox::FillAvailableMeasure(LayoutUnit available_logical_width,
                                           LayoutUnit& margin_start,
                                           LayoutUnit& margin_end) const {
  DCHECK_GE(available_logical_width, 0);
  margin_start =
      MinimumValueForLength(Style()->MarginStart(), available_logical_width);
  margin_end =
      MinimumValueForLength(Style()->MarginEnd(), available_logical_width);
  LayoutUnit available = available_logical_width - margin_start - margin_end;
  available = std::max(available, LayoutUnit());
  return available;
}

DISABLE_CFI_PERF
LayoutUnit LayoutBox::ComputeIntrinsicLogicalWidthUsing(
    const Length& logical_width_length,
    LayoutUnit available_logical_width,
    LayoutUnit border_and_padding) const {
  if (logical_width_length.GetType() == kFillAvailable)
    return std::max(border_and_padding,
                    FillAvailableMeasure(available_logical_width));

  LayoutUnit min_logical_width;
  LayoutUnit max_logical_width;
  ComputeIntrinsicLogicalWidths(min_logical_width, max_logical_width);

  if (logical_width_length.GetType() == kMinContent)
    return min_logical_width + border_and_padding;

  if (logical_width_length.GetType() == kMaxContent)
    return max_logical_width + border_and_padding;

  if (logical_width_length.GetType() == kFitContent) {
    min_logical_width += border_and_padding;
    max_logical_width += border_and_padding;
    return std::max(min_logical_width,
                    std::min(max_logical_width,
                             FillAvailableMeasure(available_logical_width)));
  }

  NOTREACHED();
  return LayoutUnit();
}

DISABLE_CFI_PERF
LayoutUnit LayoutBox::ComputeLogicalWidthUsing(
    SizeType width_type,
    const Length& logical_width,
    LayoutUnit available_logical_width,
    const LayoutBlock* cb) const {
  DCHECK(width_type == kMinSize || width_type == kMainOrPreferredSize ||
         !logical_width.IsAuto());
  if (width_type == kMinSize && logical_width.IsAuto())
    return AdjustBorderBoxLogicalWidthForBoxSizing(0);

  if (!logical_width.IsIntrinsicOrAuto()) {
    // FIXME: If the containing block flow is perpendicular to our direction we
    // need to use the available logical height instead.
    return AdjustBorderBoxLogicalWidthForBoxSizing(
        ValueForLength(logical_width, available_logical_width));
  }

  if (logical_width.IsIntrinsic())
    return ComputeIntrinsicLogicalWidthUsing(
        logical_width, available_logical_width, BorderAndPaddingLogicalWidth());

  LayoutUnit margin_start;
  LayoutUnit margin_end;
  LayoutUnit logical_width_result =
      FillAvailableMeasure(available_logical_width, margin_start, margin_end);

  if (ShrinkToAvoidFloats() && cb->IsLayoutBlockFlow() &&
      ToLayoutBlockFlow(cb)->ContainsFloats())
    logical_width_result =
        std::min(logical_width_result,
                 ShrinkLogicalWidthToAvoidFloats(margin_start, margin_end,
                                                 ToLayoutBlockFlow(cb)));

  if (width_type == kMainOrPreferredSize &&
      SizesLogicalWidthToFitContent(logical_width)) {
    // Reset width so that any percent margins on inline children do not
    // use it when calculating min/max preferred width.
    // TODO(crbug.com/710026): Remove const_cast
    const_cast<LayoutBox*>(this)->SetLogicalWidth(LayoutUnit());
    return std::max(MinPreferredLogicalWidth(),
                    std::min(MaxPreferredLogicalWidth(), logical_width_result));
  }
  return logical_width_result;
}

bool LayoutBox::ColumnFlexItemHasStretchAlignment() const {
  // auto margins mean we don't stretch. Note that this function will only be
  // used for widths, so we don't have to check marginBefore/marginAfter.
  const auto& parent_style = Parent()->StyleRef();
  DCHECK(parent_style.IsColumnFlexDirection());
  if (StyleRef().MarginStart().IsAuto() || StyleRef().MarginEnd().IsAuto())
    return false;
  return StyleRef()
             .ResolvedAlignSelf(
                 ContainingBlock()->SelfAlignmentNormalBehavior(),
                 IsAnonymous() ? &parent_style : nullptr)
             .GetPosition() == kItemPositionStretch;
}

bool LayoutBox::IsStretchingColumnFlexItem() const {
  LayoutObject* parent = this->Parent();
  if (parent->IsDeprecatedFlexibleBox() &&
      parent->Style()->BoxOrient() == VERTICAL &&
      parent->Style()->BoxAlign() == BSTRETCH)
    return true;

  // We don't stretch multiline flexboxes because they need to apply line
  // spacing (align-content) first.
  if (parent->IsFlexibleBox() && parent->Style()->FlexWrap() == kFlexNoWrap &&
      parent->Style()->IsColumnFlexDirection() &&
      ColumnFlexItemHasStretchAlignment())
    return true;
  return false;
}

// TODO (lajava) Can/Should we move this inside specific layout classes (flex.
// grid)? Can we refactor columnFlexItemHasStretchAlignment logic?
bool LayoutBox::HasStretchedLogicalWidth() const {
  const ComputedStyle& style = StyleRef();
  if (!style.LogicalWidth().IsAuto() || style.MarginStart().IsAuto() ||
      style.MarginEnd().IsAuto())
    return false;
  LayoutBlock* cb = ContainingBlock();
  if (!cb) {
    // We are evaluating align-self/justify-self, which default to 'normal' for
    // the root element. The 'normal' value behaves like 'start' except for
    // Flexbox Items, which obviously should have a container.
    return false;
  }
  const ComputedStyle* parent_style = IsAnonymous() ? cb->Style() : nullptr;
  if (cb->IsHorizontalWritingMode() != IsHorizontalWritingMode())
    return style
               .ResolvedAlignSelf(cb->SelfAlignmentNormalBehavior(this),
                                  parent_style)
               .GetPosition() == kItemPositionStretch;
  return style
             .ResolvedJustifySelf(cb->SelfAlignmentNormalBehavior(this),
                                  parent_style)
             .GetPosition() == kItemPositionStretch;
}

bool LayoutBox::SizesLogicalWidthToFitContent(
    const Length& logical_width) const {
  if (IsFloating() || IsInlineBlockOrInlineTable() ||
      StyleRef().HasOutOfFlowPosition())
    return true;

  if (IsGridItem())
    return !HasStretchedLogicalWidth();

  // Flexible box items should shrink wrap, so we lay them out at their
  // intrinsic widths. In the case of columns that have a stretch alignment, we
  // go ahead and layout at the stretched size to avoid an extra layout when
  // applying alignment.
  if (Parent()->IsFlexibleBox()) {
    // For multiline columns, we need to apply align-content first, so we can't
    // stretch now.
    if (!Parent()->Style()->IsColumnFlexDirection() ||
        Parent()->Style()->FlexWrap() != kFlexNoWrap)
      return true;
    if (!ColumnFlexItemHasStretchAlignment())
      return true;
  }

  // Flexible horizontal boxes lay out children at their intrinsic widths. Also
  // vertical boxes that don't stretch their kids lay out their children at
  // their intrinsic widths.
  // FIXME: Think about writing-mode here.
  // https://bugs.webkit.org/show_bug.cgi?id=46473
  if (Parent()->IsDeprecatedFlexibleBox() &&
      (Parent()->Style()->BoxOrient() == HORIZONTAL ||
       Parent()->Style()->BoxAlign() != BSTRETCH))
    return true;

  // Button, input, select, textarea, and legend treat width value of 'auto' as
  // 'intrinsic' unless it's in a stretching column flexbox.
  // FIXME: Think about writing-mode here.
  // https://bugs.webkit.org/show_bug.cgi?id=46473
  if (logical_width.IsAuto() && !IsStretchingColumnFlexItem() &&
      AutoWidthShouldFitContent())
    return true;

  if (IsHorizontalWritingMode() != ContainingBlock()->IsHorizontalWritingMode())
    return true;

  return false;
}

bool LayoutBox::AutoWidthShouldFitContent() const {
  return GetNode() &&
         (isHTMLInputElement(*GetNode()) || isHTMLSelectElement(*GetNode()) ||
          isHTMLButtonElement(*GetNode()) ||
          isHTMLTextAreaElement(*GetNode()) ||
          (isHTMLLegendElement(*GetNode()) &&
           !Style()->HasOutOfFlowPosition()));
}

void LayoutBox::ComputeMarginsForDirection(MarginDirection flow_direction,
                                           const LayoutBlock* containing_block,
                                           LayoutUnit container_width,
                                           LayoutUnit child_width,
                                           LayoutUnit& margin_start,
                                           LayoutUnit& margin_end,
                                           Length margin_start_length,
                                           Length margin_end_length) const {
  // First assert that we're not calling this method on box types that don't
  // support margins.
  DCHECK(!IsTableCell());
  DCHECK(!IsTableRow());
  DCHECK(!IsTableSection());
  DCHECK(!IsLayoutTableCol());
  if (flow_direction == kBlockDirection || IsFloating() || IsInline()) {
    // Margins are calculated with respect to the logical width of
    // the containing block (8.3)
    // Inline blocks/tables and floats don't have their margins increased.
    margin_start = MinimumValueForLength(margin_start_length, container_width);
    margin_end = MinimumValueForLength(margin_end_length, container_width);
    return;
  }

  if (containing_block->IsFlexibleBox()) {
    // We need to let flexbox handle the margin adjustment - otherwise, flexbox
    // will think we're wider than we actually are and calculate line sizes
    // wrong. See also http://dev.w3.org/csswg/css-flexbox/#auto-margins
    if (margin_start_length.IsAuto())
      margin_start_length.SetValue(0);
    if (margin_end_length.IsAuto())
      margin_end_length.SetValue(0);
  }

  LayoutUnit margin_start_width =
      MinimumValueForLength(margin_start_length, container_width);
  LayoutUnit margin_end_width =
      MinimumValueForLength(margin_end_length, container_width);

  LayoutUnit available_width = container_width;
  if (AvoidsFloats() && containing_block->IsLayoutBlockFlow() &&
      ToLayoutBlockFlow(containing_block)->ContainsFloats()) {
    available_width = ContainingBlockAvailableLineWidth();
    if (ShrinkToAvoidFloats() && available_width < container_width) {
      margin_start = std::max(LayoutUnit(), margin_start_width);
      margin_end = std::max(LayoutUnit(), margin_end_width);
    }
  }

  // CSS 2.1 (10.3.3): "If 'width' is not 'auto' and 'border-left-width' +
  // 'padding-left' + 'width' + 'padding-right' + 'border-right-width' (plus any
  // of 'margin-left' or 'margin-right' that are not 'auto') is larger than the
  // width of the containing block, then any 'auto' values for 'margin-left' or
  // 'margin-right' are, for the following rules, treated as zero.
  LayoutUnit margin_box_width =
      child_width + (!Style()->Width().IsAuto()
                         ? margin_start_width + margin_end_width
                         : LayoutUnit());

  if (margin_box_width < available_width) {
    // CSS 2.1: "If both 'margin-left' and 'margin-right' are 'auto', their used
    // values are equal. This horizontally centers the element with respect to
    // the edges of the containing block."
    const ComputedStyle& containing_block_style = containing_block->StyleRef();
    if ((margin_start_length.IsAuto() && margin_end_length.IsAuto()) ||
        (!margin_start_length.IsAuto() && !margin_end_length.IsAuto() &&
         containing_block_style.GetTextAlign() == ETextAlign::kWebkitCenter)) {
      // Other browsers center the margin box for align=center elements so we
      // match them here.
      LayoutUnit centered_margin_box_start =
          std::max(LayoutUnit(), (available_width - child_width -
                                  margin_start_width - margin_end_width) /
                                     2);
      margin_start = centered_margin_box_start + margin_start_width;
      margin_end =
          available_width - child_width - margin_start + margin_end_width;
      return;
    }

    // Adjust margins for the align attribute
    if ((!containing_block_style.IsLeftToRightDirection() &&
         containing_block_style.GetTextAlign() == ETextAlign::kWebkitLeft) ||
        (containing_block_style.IsLeftToRightDirection() &&
         containing_block_style.GetTextAlign() == ETextAlign::kWebkitRight)) {
      if (containing_block_style.IsLeftToRightDirection() !=
          StyleRef().IsLeftToRightDirection()) {
        if (!margin_start_length.IsAuto())
          margin_end_length = Length(kAuto);
      } else {
        if (!margin_end_length.IsAuto())
          margin_start_length = Length(kAuto);
      }
    }

    // CSS 2.1: "If there is exactly one value specified as 'auto', its used
    // value follows from the equality."
    if (margin_end_length.IsAuto()) {
      margin_start = margin_start_width;
      margin_end = available_width - child_width - margin_start;
      return;
    }

    if (margin_start_length.IsAuto()) {
      margin_end = margin_end_width;
      margin_start = available_width - child_width - margin_end;
      return;
    }
  }

  // Either no auto margins, or our margin box width is >= the container width,
  // auto margins will just turn into 0.
  margin_start = margin_start_width;
  margin_end = margin_end_width;
}

DISABLE_CFI_PERF
void LayoutBox::UpdateLogicalHeight() {
  intrinsic_content_logical_height_ = ContentLogicalHeight();

  LogicalExtentComputedValues computed_values;
  ComputeLogicalHeight(computed_values);

  SetLogicalHeight(computed_values.extent_);
  SetLogicalTop(computed_values.position_);
  SetMarginBefore(computed_values.margins_.before_);
  SetMarginAfter(computed_values.margins_.after_);
}

static inline Length HeightForDocumentElement(const Document& document) {
  return document.documentElement()
      ->GetLayoutObject()
      ->Style()
      ->LogicalHeight();
}

void LayoutBox::ComputeLogicalHeight(
    LogicalExtentComputedValues& computed_values) const {
  LayoutUnit height = Style()->ContainsSize() ? BorderAndPaddingLogicalHeight()
                                              : LogicalHeight();
  ComputeLogicalHeight(height, LogicalTop(), computed_values);
}

void LayoutBox::ComputeLogicalHeight(
    LayoutUnit logical_height,
    LayoutUnit logical_top,
    LogicalExtentComputedValues& computed_values) const {
  computed_values.extent_ = logical_height;
  computed_values.position_ = logical_top;

  // Cell height is managed by the table.
  if (IsTableCell())
    return;

  Length h;
  if (IsOutOfFlowPositioned()) {
    ComputePositionedLogicalHeight(computed_values);
  } else {
    LayoutBlock* cb = ContainingBlock();

    // If we are perpendicular to our containing block then we need to resolve
    // our block-start and block-end margins so that if they are 'auto' we are
    // centred or aligned within the inline flow containing block: this is done
    // by computing the margins as though they are inline.
    // Note that as this is the 'sizing phase' we are using our own writing mode
    // rather than the containing block's. We use the containing block's writing
    // mode when figuring out the block-direction margins for positioning in
    // |computeAndSetBlockDirectionMargins| (i.e. margin collapsing etc.).
    // http://www.w3.org/TR/2014/CR-css-writing-modes-3-20140320/#orthogonal-flows
    MarginDirection flow_direction =
        IsHorizontalWritingMode() != cb->IsHorizontalWritingMode()
            ? kInlineDirection
            : kBlockDirection;

    // For tables, calculate margins only.
    if (IsTable()) {
      ComputeMarginsForDirection(
          flow_direction, cb, ContainingBlockLogicalWidthForContent(),
          computed_values.extent_, computed_values.margins_.before_,
          computed_values.margins_.after_, Style()->MarginBefore(),
          Style()->MarginAfter());
      return;
    }

    // FIXME: Account for writing-mode in flexible boxes.
    // https://bugs.webkit.org/show_bug.cgi?id=46418
    bool in_horizontal_box = Parent()->IsDeprecatedFlexibleBox() &&
                             Parent()->Style()->BoxOrient() == HORIZONTAL;
    bool stretching = Parent()->Style()->BoxAlign() == BSTRETCH;
    bool treat_as_replaced =
        ShouldComputeSizeAsReplaced() && (!in_horizontal_box || !stretching);
    bool check_min_max_height = false;

    // The parent box is flexing us, so it has increased or decreased our
    // height. We have to grab our cached flexible height.
    if (HasOverrideLogicalContentHeight()) {
      h = Length(OverrideLogicalContentHeight(), kFixed);
    } else if (treat_as_replaced) {
      h = Length(ComputeReplacedLogicalHeight(), kFixed);
    } else {
      h = Style()->LogicalHeight();
      check_min_max_height = true;
    }

    // Block children of horizontal flexible boxes fill the height of the box.
    // FIXME: Account for writing-mode in flexible boxes.
    // https://bugs.webkit.org/show_bug.cgi?id=46418
    if (h.IsAuto() && in_horizontal_box &&
        ToLayoutDeprecatedFlexibleBox(Parent())->IsStretchingChildren()) {
      h = Length(ParentBox()->ContentLogicalHeight() - MarginBefore() -
                     MarginAfter() - BorderAndPaddingLogicalHeight(),
                 kFixed);
      check_min_max_height = false;
    }

    LayoutUnit height_result;
    if (check_min_max_height) {
      height_result = ComputeLogicalHeightUsing(
          kMainOrPreferredSize, Style()->LogicalHeight(),
          computed_values.extent_ - BorderAndPaddingLogicalHeight());
      if (height_result == -1)
        height_result = computed_values.extent_;
      height_result = ConstrainLogicalHeightByMinMax(
          height_result,
          computed_values.extent_ - BorderAndPaddingLogicalHeight());
    } else {
      // The only times we don't check min/max height are when a fixed length
      // has been given as an override.  Just use that.  The value has already
      // been adjusted for box-sizing.
      DCHECK(h.IsFixed());
      height_result = LayoutUnit(h.Value()) + BorderAndPaddingLogicalHeight();
    }

    computed_values.extent_ = height_result;
    ComputeMarginsForDirection(
        flow_direction, cb, ContainingBlockLogicalWidthForContent(),
        computed_values.extent_, computed_values.margins_.before_,
        computed_values.margins_.after_, Style()->MarginBefore(),
        Style()->MarginAfter());
  }

  // WinIE quirk: The <html> block always fills the entire canvas in quirks
  // mode. The <body> always fills the <html> block in quirks mode. Only apply
  // this quirk if the block is normal flow and no height is specified. When
  // we're printing, we also need this quirk if the body or root has a
  // percentage height since we don't set a height in LayoutView when we're
  // printing. So without this quirk, the height has nothing to be a percentage
  // of, and it ends up being 0. That is bad.
  bool paginated_content_needs_base_height =
      GetDocument().Printing() && h.IsPercentOrCalc() &&
      (IsDocumentElement() ||
       (IsBody() &&
        HeightForDocumentElement(GetDocument()).IsPercentOrCalc())) &&
      !IsInline();
  if (StretchesToViewport() || paginated_content_needs_base_height) {
    LayoutUnit margins = CollapsedMarginBefore() + CollapsedMarginAfter();
    LayoutUnit visible_height = View()->ViewLogicalHeightForPercentages();
    if (IsDocumentElement()) {
      computed_values.extent_ =
          std::max(computed_values.extent_, visible_height - margins);
    } else {
      LayoutUnit margins_borders_padding =
          margins + ParentBox()->MarginBefore() + ParentBox()->MarginAfter() +
          ParentBox()->BorderAndPaddingLogicalHeight();
      computed_values.extent_ = std::max(
          computed_values.extent_, visible_height - margins_borders_padding);
    }
  }
}

LayoutUnit LayoutBox::ComputeLogicalHeightWithoutLayout() const {
  // TODO(cbiesinger): We should probably return something other than just
  // border + padding, but for now we have no good way to do anything else
  // without layout, so we just use that.
  LogicalExtentComputedValues computed_values;
  ComputeLogicalHeight(BorderAndPaddingLogicalHeight(), LayoutUnit(),
                       computed_values);
  return computed_values.extent_;
}

LayoutUnit LayoutBox::ComputeLogicalHeightUsing(
    SizeType height_type,
    const Length& height,
    LayoutUnit intrinsic_content_height) const {
  LayoutUnit logical_height = ComputeContentAndScrollbarLogicalHeightUsing(
      height_type, height, intrinsic_content_height);
  if (logical_height != -1) {
    if (height.IsSpecified())
      logical_height = AdjustBorderBoxLogicalHeightForBoxSizing(logical_height);
    else
      logical_height += BorderAndPaddingLogicalHeight();
  }
  return logical_height;
}

LayoutUnit LayoutBox::ComputeContentLogicalHeight(
    SizeType height_type,
    const Length& height,
    LayoutUnit intrinsic_content_height) const {
  LayoutUnit height_including_scrollbar =
      ComputeContentAndScrollbarLogicalHeightUsing(height_type, height,
                                                   intrinsic_content_height);
  if (height_including_scrollbar == -1)
    return LayoutUnit(-1);
  LayoutUnit adjusted = height_including_scrollbar;
  if (height.IsSpecified()) {
    // Keywords don't get adjusted for box-sizing
    adjusted =
        AdjustContentBoxLogicalHeightForBoxSizing(height_including_scrollbar);
  }
  return std::max(LayoutUnit(), adjusted - ScrollbarLogicalHeight());
}

LayoutUnit LayoutBox::ComputeIntrinsicLogicalContentHeightUsing(
    const Length& logical_height_length,
    LayoutUnit intrinsic_content_height,
    LayoutUnit border_and_padding) const {
  // FIXME(cbiesinger): The css-sizing spec is considering changing what
  // min-content/max-content should resolve to.
  // If that happens, this code will have to change.
  if (logical_height_length.IsMinContent() ||
      logical_height_length.IsMaxContent() ||
      logical_height_length.IsFitContent()) {
    if (IsAtomicInlineLevel())
      return IntrinsicSize().Height();
    return intrinsic_content_height;
  }
  if (logical_height_length.IsFillAvailable())
    return ContainingBlock()->AvailableLogicalHeight(
               kExcludeMarginBorderPadding) -
           border_and_padding;
  NOTREACHED();
  return LayoutUnit();
}

LayoutUnit LayoutBox::ComputeContentAndScrollbarLogicalHeightUsing(
    SizeType height_type,
    const Length& height,
    LayoutUnit intrinsic_content_height) const {
  if (height.IsAuto())
    return height_type == kMinSize ? LayoutUnit() : LayoutUnit(-1);
  // FIXME(cbiesinger): The css-sizing spec is considering changing what
  // min-content/max-content should resolve to.
  // If that happens, this code will have to change.
  if (height.IsIntrinsic()) {
    if (intrinsic_content_height == -1)
      return LayoutUnit(-1);  // Intrinsic height isn't available.
    return ComputeIntrinsicLogicalContentHeightUsing(
               height, intrinsic_content_height,
               BorderAndPaddingLogicalHeight()) +
           ScrollbarLogicalHeight();
  }
  if (height.IsFixed())
    return LayoutUnit(height.Value());
  if (height.IsPercentOrCalc())
    return ComputePercentageLogicalHeight(height);
  return LayoutUnit(-1);
}

bool LayoutBox::StretchesToViewportInQuirksMode() const {
  if (!IsDocumentElement() && !IsBody())
    return false;
  return Style()->LogicalHeight().IsAuto() &&
         !IsFloatingOrOutOfFlowPositioned() && !IsInline() &&
         !FlowThreadContainingBlock();
}

bool LayoutBox::SkipContainingBlockForPercentHeightCalculation(
    const LayoutBox* containing_block) const {
  // If the writing mode of the containing block is orthogonal to ours, it means
  // that we shouldn't skip anything, since we're going to resolve the
  // percentage height against a containing block *width*.
  if (IsHorizontalWritingMode() != containing_block->IsHorizontalWritingMode())
    return false;

  // Anonymous blocks should not impede percentage resolution on a child.
  // Examples of such anonymous blocks are blocks wrapped around inlines that
  // have block siblings (from the CSS spec) and multicol flow threads (an
  // implementation detail). Another implementation detail, ruby runs, create
  // anonymous inline-blocks, so skip those too. All other types of anonymous
  // objects, such as table-cells, will be treated just as if they were
  // non-anonymous.
  if (containing_block->IsAnonymous()) {
    EDisplay display = containing_block->StyleRef().Display();
    return display == EDisplay::kBlock || display == EDisplay::kInlineBlock;
  }

  // For quirks mode, we skip most auto-height containing blocks when computing
  // percentages.
  return GetDocument().InQuirksMode() && !containing_block->IsTableCell() &&
         !containing_block->IsOutOfFlowPositioned() &&
         !containing_block->IsLayoutGrid() &&
         containing_block->Style()->LogicalHeight().IsAuto();
}

LayoutUnit LayoutBox::ComputePercentageLogicalHeight(
    const Length& height) const {
  LayoutBlock* cb = ContainingBlock();
  const LayoutBox* containing_block_child = this;
  bool skipped_auto_height_containing_block = false;
  LayoutUnit root_margin_border_padding_height;
  while (!cb->IsLayoutView() &&
         SkipContainingBlockForPercentHeightCalculation(cb)) {
    if (cb->IsBody() || cb->IsDocumentElement())
      root_margin_border_padding_height += cb->MarginBefore() +
                                           cb->MarginAfter() +
                                           cb->BorderAndPaddingLogicalHeight();
    skipped_auto_height_containing_block = true;
    containing_block_child = cb;
    cb = cb->ContainingBlock();
  }
  cb->AddPercentHeightDescendant(const_cast<LayoutBox*>(this));

  LayoutUnit available_height(-1);
  if (IsHorizontalWritingMode() != cb->IsHorizontalWritingMode()) {
    available_height =
        containing_block_child->ContainingBlockLogicalWidthForContent();
  } else if (HasOverrideContainingBlockLogicalHeight()) {
    available_height = OverrideContainingBlockContentLogicalHeight();
  } else if (cb->IsTableCell()) {
    if (!skipped_auto_height_containing_block) {
      // Table cells violate what the CSS spec says to do with heights.
      // Basically we don't care if the cell specified a height or not. We just
      // always make ourselves be a percentage of the cell's current content
      // height.
      if (!cb->HasOverrideLogicalContentHeight()) {
        // https://drafts.csswg.org/css-tables-3/#row-layout:
        // For the purpose of calculating [the minimum height of a row],
        // descendants of table cells whose height depends on percentages
        // of their parent cell's height are considered to have an auto
        // height if they have overflow set to visible or hidden or if
        // they are replaced elements, and a 0px height if they have not.
        LayoutTableCell* cell = ToLayoutTableCell(cb);
        if (Style()->OverflowY() != EOverflow::kVisible &&
            Style()->OverflowY() != EOverflow::kHidden &&
            !ShouldBeConsideredAsReplaced() &&
            (!cell->Style()->LogicalHeight().IsAuto() ||
             !cell->Table()->Style()->LogicalHeight().IsAuto()))
          return LayoutUnit();
        return LayoutUnit(-1);
      }
      available_height = cb->OverrideLogicalContentHeight();
    }
  } else {
    available_height = cb->AvailableLogicalHeightForPercentageComputation();
  }

  if (available_height == -1)
    return available_height;

  available_height -= root_margin_border_padding_height;

  if (IsTable() && IsOutOfFlowPositioned())
    available_height += cb->PaddingLogicalHeight();

  LayoutUnit result = ValueForLength(height, available_height);
  // |overrideLogicalContentHeight| is the maximum height made available by the
  // cell to its percent height children when we decide they can determine the
  // height of the cell. If the percent height child is box-sizing:content-box
  // then we must subtract the border and padding from the cell's
  // |availableHeight| (given by |overrideLogicalContentHeight|) to arrive
  // at the child's computed height.
  bool subtract_border_and_padding =
      IsTable() ||
      (cb->IsTableCell() && !skipped_auto_height_containing_block &&
       cb->HasOverrideLogicalContentHeight() &&
       Style()->BoxSizing() == EBoxSizing::kContentBox);
  if (subtract_border_and_padding) {
    result -= BorderAndPaddingLogicalHeight();
    return std::max(LayoutUnit(), result);
  }
  return result;
}

LayoutUnit LayoutBox::ComputeReplacedLogicalWidth(
    ShouldComputePreferred should_compute_preferred) const {
  return ComputeReplacedLogicalWidthRespectingMinMaxWidth(
      ComputeReplacedLogicalWidthUsing(kMainOrPreferredSize,
                                       Style()->LogicalWidth()),
      should_compute_preferred);
}

LayoutUnit LayoutBox::ComputeReplacedLogicalWidthRespectingMinMaxWidth(
    LayoutUnit logical_width,
    ShouldComputePreferred should_compute_preferred) const {
  LayoutUnit min_logical_width =
      (should_compute_preferred == kComputePreferred &&
       Style()->LogicalMinWidth().IsPercentOrCalc())
          ? logical_width
          : ComputeReplacedLogicalWidthUsing(kMinSize,
                                             Style()->LogicalMinWidth());
  LayoutUnit max_logical_width =
      (should_compute_preferred == kComputePreferred &&
       Style()->LogicalMaxWidth().IsPercentOrCalc()) ||
              Style()->LogicalMaxWidth().IsMaxSizeNone()
          ? logical_width
          : ComputeReplacedLogicalWidthUsing(kMaxSize,
                                             Style()->LogicalMaxWidth());
  return std::max(min_logical_width,
                  std::min(logical_width, max_logical_width));
}

LayoutUnit LayoutBox::ComputeReplacedLogicalWidthUsing(
    SizeType size_type,
    const Length& logical_width) const {
  DCHECK(size_type == kMinSize || size_type == kMainOrPreferredSize ||
         !logical_width.IsAuto());
  if (size_type == kMinSize && logical_width.IsAuto())
    return AdjustContentBoxLogicalWidthForBoxSizing(LayoutUnit());

  switch (logical_width.GetType()) {
    case kFixed:
      return AdjustContentBoxLogicalWidthForBoxSizing(logical_width.Value());
    case kMinContent:
    case kMaxContent: {
      // MinContent/MaxContent don't need the availableLogicalWidth argument.
      LayoutUnit available_logical_width;
      return ComputeIntrinsicLogicalWidthUsing(logical_width,
                                               available_logical_width,
                                               BorderAndPaddingLogicalWidth()) -
             BorderAndPaddingLogicalWidth();
    }
    case kFitContent:
    case kFillAvailable:
    case kPercent:
    case kCalculated: {
      // FIXME: containingBlockLogicalWidthForContent() is wrong if the replaced
      // element's writing-mode is perpendicular to the containing block's
      // writing-mode. https://bugs.webkit.org/show_bug.cgi?id=46496
      const LayoutUnit cw = IsOutOfFlowPositioned()
                                ? ContainingBlockLogicalWidthForPositioned(
                                      ToLayoutBoxModelObject(Container()))
                                : ContainingBlockLogicalWidthForContent();
      Length container_logical_width =
          ContainingBlock()->Style()->LogicalWidth();
      // FIXME: Handle cases when containing block width is calculated or
      // viewport percent. https://bugs.webkit.org/show_bug.cgi?id=91071
      if (logical_width.IsIntrinsic())
        return ComputeIntrinsicLogicalWidthUsing(
                   logical_width, cw, BorderAndPaddingLogicalWidth()) -
               BorderAndPaddingLogicalWidth();
      if (cw > 0 || (!cw && (container_logical_width.IsFixed() ||
                             container_logical_width.IsPercentOrCalc())))
        return AdjustContentBoxLogicalWidthForBoxSizing(
            MinimumValueForLength(logical_width, cw));
      return LayoutUnit();
    }
    case kAuto:
    case kMaxSizeNone:
      return IntrinsicLogicalWidth();
    case kExtendToZoom:
    case kDeviceWidth:
    case kDeviceHeight:
      break;
  }

  NOTREACHED();
  return LayoutUnit();
}

LayoutUnit LayoutBox::ComputeReplacedLogicalHeight(LayoutUnit) const {
  return ComputeReplacedLogicalHeightRespectingMinMaxHeight(
      ComputeReplacedLogicalHeightUsing(kMainOrPreferredSize,
                                        Style()->LogicalHeight()));
}

bool LayoutBox::LogicalHeightComputesAsNone(SizeType size_type) const {
  DCHECK(size_type == kMinSize || size_type == kMaxSize);
  Length logical_height = size_type == kMinSize ? Style()->LogicalMinHeight()
                                                : Style()->LogicalMaxHeight();
  Length initial_logical_height = size_type == kMinSize
                                      ? ComputedStyle::InitialMinHeight()
                                      : ComputedStyle::InitialMaxHeight();

  if (logical_height == initial_logical_height)
    return true;

  if (LayoutBlock* cb = ContainingBlockForAutoHeightDetection(logical_height))
    return cb->HasAutoHeightOrContainingBlockWithAutoHeight();
  return false;
}

LayoutUnit LayoutBox::ComputeReplacedLogicalHeightRespectingMinMaxHeight(
    LayoutUnit logical_height) const {
  // If the height of the containing block is not specified explicitly (i.e., it
  // depends on content height), and this element is not absolutely positioned,
  // the percentage value is treated as '0' (for 'min-height') or 'none' (for
  // 'max-height').
  LayoutUnit min_logical_height;
  if (!LogicalHeightComputesAsNone(kMinSize))
    min_logical_height = ComputeReplacedLogicalHeightUsing(
        kMinSize, Style()->LogicalMinHeight());
  LayoutUnit max_logical_height = logical_height;
  if (!LogicalHeightComputesAsNone(kMaxSize))
    max_logical_height = ComputeReplacedLogicalHeightUsing(
        kMaxSize, Style()->LogicalMaxHeight());
  return std::max(min_logical_height,
                  std::min(logical_height, max_logical_height));
}

LayoutUnit LayoutBox::ComputeReplacedLogicalHeightUsing(
    SizeType size_type,
    const Length& logical_height) const {
  DCHECK(size_type == kMinSize || size_type == kMainOrPreferredSize ||
         !logical_height.IsAuto());
  if (size_type == kMinSize && logical_height.IsAuto())
    return AdjustContentBoxLogicalHeightForBoxSizing(LayoutUnit());

  switch (logical_height.GetType()) {
    case kFixed:
      return AdjustContentBoxLogicalHeightForBoxSizing(logical_height.Value());
    case kPercent:
    case kCalculated: {
      // TODO(rego): Check if we can somehow reuse
      // LayoutBox::computePercentageLogicalHeight() and/or
      // LayoutBlock::availableLogicalHeightForPercentageComputation() (see
      // http://crbug.com/635655).
      LayoutObject* cb =
          IsOutOfFlowPositioned() ? Container() : ContainingBlock();
      while (cb->IsAnonymous())
        cb = cb->ContainingBlock();
      LayoutUnit stretched_height(-1);
      if (cb->IsLayoutBlock()) {
        LayoutBlock* block = ToLayoutBlock(cb);
        block->AddPercentHeightDescendant(const_cast<LayoutBox*>(this));
        if (block->IsFlexItem())
          stretched_height =
              ToLayoutFlexibleBox(block->Parent())
                  ->ChildLogicalHeightForPercentageResolution(*block);
        else if (block->IsGridItem() &&
                 block->HasOverrideLogicalContentHeight())
          stretched_height = block->OverrideLogicalContentHeight();
      }

      if (cb->IsOutOfFlowPositioned() && cb->Style()->Height().IsAuto() &&
          !(cb->Style()->Top().IsAuto() || cb->Style()->Bottom().IsAuto())) {
        SECURITY_DCHECK(cb->IsLayoutBlock());
        LayoutBlock* block = ToLayoutBlock(cb);
        LogicalExtentComputedValues computed_values;
        block->ComputeLogicalHeight(block->LogicalHeight(), LayoutUnit(),
                                    computed_values);
        LayoutUnit new_content_height = computed_values.extent_ -
                                        block->BorderAndPaddingLogicalHeight() -
                                        block->ScrollbarLogicalHeight();
        LayoutUnit new_height =
            block->AdjustContentBoxLogicalHeightForBoxSizing(
                new_content_height);
        return AdjustContentBoxLogicalHeightForBoxSizing(
            ValueForLength(logical_height, new_height));
      }

      // FIXME: availableLogicalHeight() is wrong if the replaced element's
      // writing-mode is perpendicular to the containing block's writing-mode.
      // https://bugs.webkit.org/show_bug.cgi?id=46496
      LayoutUnit available_height;
      if (IsOutOfFlowPositioned()) {
        available_height = ContainingBlockLogicalHeightForPositioned(
            ToLayoutBoxModelObject(cb));
      } else if (stretched_height != -1) {
        available_height = stretched_height;
      } else if (HasOverrideContainingBlockLogicalHeight()) {
        available_height = OverrideContainingBlockContentLogicalHeight();
      } else {
        available_height =
            ContainingBlockLogicalHeightForContent(kIncludeMarginBorderPadding);
        // It is necessary to use the border-box to match WinIE's broken
        // box model.  This is essential for sizing inside
        // table cells using percentage heights.
        // FIXME: This needs to be made writing-mode-aware. If the cell and
        // image are perpendicular writing-modes, this isn't right.
        // https://bugs.webkit.org/show_bug.cgi?id=46997
        while (cb && !cb->IsLayoutView() &&
               (cb->Style()->LogicalHeight().IsAuto() ||
                cb->Style()->LogicalHeight().IsPercentOrCalc())) {
          if (cb->IsTableCell()) {
            // Don't let table cells squeeze percent-height replaced elements
            // <http://bugs.webkit.org/show_bug.cgi?id=15359>
            available_height =
                std::max(available_height, IntrinsicLogicalHeight());
            return ValueForLength(
                logical_height,
                available_height - BorderAndPaddingLogicalHeight());
          }
          ToLayoutBlock(cb)->AddPercentHeightDescendant(
              const_cast<LayoutBox*>(this));
          cb = cb->ContainingBlock();
        }
      }
      return AdjustContentBoxLogicalHeightForBoxSizing(
          ValueForLength(logical_height, available_height));
    }
    case kMinContent:
    case kMaxContent:
    case kFitContent:
    case kFillAvailable:
      return AdjustContentBoxLogicalHeightForBoxSizing(
          ComputeIntrinsicLogicalContentHeightUsing(logical_height,
                                                    IntrinsicLogicalHeight(),
                                                    BorderAndPaddingHeight()));
    default:
      return IntrinsicLogicalHeight();
  }
}

LayoutUnit LayoutBox::AvailableLogicalHeight(
    AvailableLogicalHeightType height_type) const {
  // http://www.w3.org/TR/CSS2/visudet.html#propdef-height - We are interested
  // in the content height.
  // FIXME: Should we pass intrinsicContentLogicalHeight() instead of -1 here?
  return ConstrainContentBoxLogicalHeightByMinMax(
      AvailableLogicalHeightUsing(Style()->LogicalHeight(), height_type),
      LayoutUnit(-1));
}

LayoutUnit LayoutBox::AvailableLogicalHeightUsing(
    const Length& h,
    AvailableLogicalHeightType height_type) const {
  if (IsLayoutView()) {
    return LayoutUnit(
        IsHorizontalWritingMode()
            ? ToLayoutView(this)->GetFrameView()->VisibleContentSize().Height()
            : ToLayoutView(this)->GetFrameView()->VisibleContentSize().Width());
  }

  // We need to stop here, since we don't want to increase the height of the
  // table artificially.  We're going to rely on this cell getting expanded to
  // some new height, and then when we lay out again we'll use the calculation
  // below.
  if (IsTableCell() && (h.IsAuto() || h.IsPercentOrCalc())) {
    if (HasOverrideLogicalContentHeight())
      return OverrideLogicalContentHeight();
    return LogicalHeight() - BorderAndPaddingLogicalHeight();
  }

  if (IsFlexItem()) {
    LayoutFlexibleBox& flex_box = ToLayoutFlexibleBox(*Parent());
    LayoutUnit stretched_height =
        flex_box.ChildLogicalHeightForPercentageResolution(*this);
    if (stretched_height != LayoutUnit(-1))
      return stretched_height;
  }

  if (h.IsPercentOrCalc() && IsOutOfFlowPositioned()) {
    // FIXME: This is wrong if the containingBlock has a perpendicular writing
    // mode.
    LayoutUnit available_height =
        ContainingBlockLogicalHeightForPositioned(ContainingBlock());
    return AdjustContentBoxLogicalHeightForBoxSizing(
        ValueForLength(h, available_height));
  }

  // FIXME: Should we pass intrinsicContentLogicalHeight() instead of -1 here?
  LayoutUnit height_including_scrollbar =
      ComputeContentAndScrollbarLogicalHeightUsing(kMainOrPreferredSize, h,
                                                   LayoutUnit(-1));
  if (height_including_scrollbar != -1)
    return std::max(LayoutUnit(), AdjustContentBoxLogicalHeightForBoxSizing(
                                      height_including_scrollbar) -
                                      ScrollbarLogicalHeight());

  // FIXME: Check logicalTop/logicalBottom here to correctly handle vertical
  // writing-mode.
  // https://bugs.webkit.org/show_bug.cgi?id=46500
  if (IsLayoutBlock() && IsOutOfFlowPositioned() &&
      Style()->Height().IsAuto() &&
      !(Style()->Top().IsAuto() || Style()->Bottom().IsAuto())) {
    LayoutBlock* block = const_cast<LayoutBlock*>(ToLayoutBlock(this));
    LogicalExtentComputedValues computed_values;
    block->ComputeLogicalHeight(block->LogicalHeight(), LayoutUnit(),
                                computed_values);
    LayoutUnit new_content_height = computed_values.extent_ -
                                    block->BorderAndPaddingLogicalHeight() -
                                    block->ScrollbarLogicalHeight();
    return AdjustContentBoxLogicalHeightForBoxSizing(new_content_height);
  }

  // FIXME: This is wrong if the containingBlock has a perpendicular writing
  // mode.
  LayoutUnit available_height =
      ContainingBlockLogicalHeightForContent(height_type);
  if (height_type == kExcludeMarginBorderPadding) {
    // FIXME: Margin collapsing hasn't happened yet, so this incorrectly removes
    // collapsed margins.
    available_height -=
        MarginBefore() + MarginAfter() + BorderAndPaddingLogicalHeight();
  }
  return available_height;
}

void LayoutBox::ComputeAndSetBlockDirectionMargins(
    const LayoutBlock* containing_block) {
  LayoutUnit margin_before;
  LayoutUnit margin_after;
  ComputeMarginsForDirection(
      kBlockDirection, containing_block,
      ContainingBlockLogicalWidthForContent(), LogicalHeight(), margin_before,
      margin_after, Style()->MarginBeforeUsing(containing_block->Style()),
      Style()->MarginAfterUsing(containing_block->Style()));
  // Note that in this 'positioning phase' of the layout we are using the
  // containing block's writing mode rather than our own when calculating
  // margins.
  // http://www.w3.org/TR/2014/CR-css-writing-modes-3-20140320/#orthogonal-flows
  containing_block->SetMarginBeforeForChild(*this, margin_before);
  containing_block->SetMarginAfterForChild(*this, margin_after);
}

LayoutUnit LayoutBox::ContainingBlockLogicalWidthForPositioned(
    const LayoutBoxModelObject* containing_block,
    bool check_for_perpendicular_writing_mode) const {
  if (check_for_perpendicular_writing_mode &&
      containing_block->IsHorizontalWritingMode() != IsHorizontalWritingMode())
    return ContainingBlockLogicalHeightForPositioned(containing_block, false);

  // Use viewport as container for top-level fixed-position elements.
  if (Style()->GetPosition() == EPosition::kFixed &&
      containing_block->IsLayoutView() && !GetDocument().Printing()) {
    const LayoutView* view = ToLayoutView(containing_block);
    if (FrameView* frame_view = view->GetFrameView()) {
      // Don't use visibleContentRect since the PaintLayer's size has not been
      // set yet.
      LayoutSize viewport_size(
          frame_view->LayoutViewportScrollableArea()->ExcludeScrollbars(
              frame_view->FrameRect().Size()));
      return LayoutUnit(containing_block->IsHorizontalWritingMode()
                            ? viewport_size.Width()
                            : viewport_size.Height());
    }
  }

  if (HasOverrideContainingBlockLogicalWidth())
    return OverrideContainingBlockContentLogicalWidth();

  // Ensure we compute our width based on the width of our rel-pos inline
  // container rather than any anonymous block created to manage a block-flow
  // ancestor of ours in the rel-pos inline's inline flow.
  if (containing_block->IsAnonymousBlock() &&
      containing_block->IsRelPositioned())
    containing_block = ToLayoutBox(containing_block)->Continuation();
  else if (containing_block->IsBox())
    return std::max(LayoutUnit(),
                    ToLayoutBox(containing_block)->ClientLogicalWidth());

  DCHECK(containing_block->IsLayoutInline());
  DCHECK(containing_block->IsInFlowPositioned());

  const LayoutInline* flow = ToLayoutInline(containing_block);
  InlineFlowBox* first = flow->FirstLineBox();
  InlineFlowBox* last = flow->LastLineBox();

  // If the containing block is empty, return a width of 0.
  if (!first || !last)
    return LayoutUnit();

  LayoutUnit from_left;
  LayoutUnit from_right;
  if (containing_block->Style()->IsLeftToRightDirection()) {
    from_left = first->LogicalLeft() + first->BorderLogicalLeft();
    from_right =
        last->LogicalLeft() + last->LogicalWidth() - last->BorderLogicalRight();
  } else {
    from_right = first->LogicalLeft() + first->LogicalWidth() -
                 first->BorderLogicalRight();
    from_left = last->LogicalLeft() + last->BorderLogicalLeft();
  }

  return std::max(LayoutUnit(), from_right - from_left);
}

LayoutUnit LayoutBox::ContainingBlockLogicalHeightForPositioned(
    const LayoutBoxModelObject* containing_block,
    bool check_for_perpendicular_writing_mode) const {
  if (check_for_perpendicular_writing_mode &&
      containing_block->IsHorizontalWritingMode() != IsHorizontalWritingMode())
    return ContainingBlockLogicalWidthForPositioned(containing_block, false);

  // Use viewport as container for top-level fixed-position elements.
  if (Style()->GetPosition() == EPosition::kFixed &&
      containing_block->IsLayoutView() && !GetDocument().Printing()) {
    const LayoutView* view = ToLayoutView(containing_block);
    if (FrameView* frame_view = view->GetFrameView()) {
      // Don't use visibleContentRect since the PaintLayer's size has not been
      // set yet.
      LayoutSize viewport_size(
          frame_view->LayoutViewportScrollableArea()->ExcludeScrollbars(
              frame_view->FrameRect().Size()));
      return containing_block->IsHorizontalWritingMode()
                 ? viewport_size.Height()
                 : viewport_size.Width();
    }
  }

  if (HasOverrideContainingBlockLogicalHeight())
    return OverrideContainingBlockContentLogicalHeight();

  if (containing_block->IsBox()) {
    const LayoutBlock* cb = containing_block->IsLayoutBlock()
                                ? ToLayoutBlock(containing_block)
                                : containing_block->ContainingBlock();
    return cb->ClientLogicalHeight();
  }

  DCHECK(containing_block->IsLayoutInline());
  DCHECK(containing_block->IsInFlowPositioned());

  const LayoutInline* flow = ToLayoutInline(containing_block);
  InlineFlowBox* first = flow->FirstLineBox();
  InlineFlowBox* last = flow->LastLineBox();

  // If the containing block is empty, return a height of 0.
  if (!first || !last)
    return LayoutUnit();

  LayoutUnit height_result;
  LayoutRect bounding_box(flow->LinesBoundingBox());
  if (containing_block->IsHorizontalWritingMode())
    height_result = bounding_box.Height();
  else
    height_result = bounding_box.Width();
  height_result -=
      (containing_block->BorderBefore() + containing_block->BorderAfter());
  return height_result;
}

static LayoutUnit AccumulateStaticOffsetForFlowThread(
    LayoutBox& layout_box,
    LayoutUnit inline_position,
    LayoutUnit& block_position) {
  if (layout_box.IsTableRow())
    return LayoutUnit();
  block_position += layout_box.LogicalTop();
  if (!layout_box.IsLayoutFlowThread())
    return LayoutUnit();
  LayoutUnit previous_inline_position = inline_position;
  // We're walking out of a flowthread here. This flow thread is not in the
  // containing block chain, so we need to convert the position from the
  // coordinate space of this flowthread to the containing coordinate space.
  ToLayoutFlowThread(layout_box)
      .FlowThreadToContainingCoordinateSpace(block_position, inline_position);
  return inline_position - previous_inline_position;
}

void LayoutBox::ComputeInlineStaticDistance(
    Length& logical_left,
    Length& logical_right,
    const LayoutBox* child,
    const LayoutBoxModelObject* container_block,
    LayoutUnit container_logical_width) {
  if (!logical_left.IsAuto() || !logical_right.IsAuto())
    return;

  LayoutObject* parent = child->Parent();
  TextDirection parent_direction = parent->Style()->Direction();

  // This method is using EnclosingBox() which is wrong for absolutely
  // positioned grid items, as they rely on the grid area. So for grid items if
  // both "left" and "right" properties are "auto", we can consider that one of
  // them (depending on the direction) is simply "0".
  if (parent->IsLayoutGrid() && parent == child->ContainingBlock()) {
    if (parent_direction == TextDirection::kLtr)
      logical_left.SetValue(kFixed, 0);
    else
      logical_right.SetValue(kFixed, 0);
    return;
  }

  // For multicol we also need to keep track of the block position, since that
  // determines which column we're in and thus affects the inline position.
  LayoutUnit static_block_position = child->Layer()->StaticBlockPosition();

  // FIXME: The static distance computation has not been patched for mixed
  // writing modes yet.
  if (parent_direction == TextDirection::kLtr) {
    LayoutUnit static_position = child->Layer()->StaticInlinePosition() -
                                 container_block->BorderLogicalLeft();
    for (LayoutObject* curr = child->Parent(); curr && curr != container_block;
         curr = curr->Container()) {
      if (curr->IsBox()) {
        static_position += ToLayoutBox(curr)->LogicalLeft();
        if (ToLayoutBox(curr)->IsInFlowPositioned())
          static_position +=
              ToLayoutBox(curr)->OffsetForInFlowPosition().Width();
        if (curr->IsInsideFlowThread())
          static_position += AccumulateStaticOffsetForFlowThread(
              *ToLayoutBox(curr), static_position, static_block_position);
      } else if (curr->IsInline()) {
        if (curr->IsInFlowPositioned()) {
          if (!curr->Style()->LogicalLeft().IsAuto())
            static_position +=
                ValueForLength(curr->Style()->LogicalLeft(),
                               curr->ContainingBlock()->AvailableWidth());
          else
            static_position -=
                ValueForLength(curr->Style()->LogicalRight(),
                               curr->ContainingBlock()->AvailableWidth());
        }
      }
    }
    logical_left.SetValue(kFixed, static_position);
  } else {
    LayoutBox* enclosing_box = child->Parent()->EnclosingBox();
    LayoutUnit static_position = child->Layer()->StaticInlinePosition() +
                                 container_logical_width +
                                 container_block->BorderLogicalLeft();
    for (LayoutObject* curr = child->Parent(); curr; curr = curr->Container()) {
      if (curr->IsBox()) {
        if (curr == enclosing_box)
          static_position -= enclosing_box->LogicalWidth();
        if (curr != container_block) {
          static_position -= ToLayoutBox(curr)->LogicalLeft();
          if (ToLayoutBox(curr)->IsInFlowPositioned())
            static_position -=
                ToLayoutBox(curr)->OffsetForInFlowPosition().Width();
          if (curr->IsInsideFlowThread())
            static_position -= AccumulateStaticOffsetForFlowThread(
                *ToLayoutBox(curr), static_position, static_block_position);
        }
      } else if (curr->IsInline()) {
        if (curr->IsInFlowPositioned()) {
          if (!curr->Style()->LogicalLeft().IsAuto())
            static_position -=
                ValueForLength(curr->Style()->LogicalLeft(),
                               curr->ContainingBlock()->AvailableWidth());
          else
            static_position +=
                ValueForLength(curr->Style()->LogicalRight(),
                               curr->ContainingBlock()->AvailableWidth());
        }
      }
      if (curr == container_block)
        break;
    }
    logical_right.SetValue(kFixed, static_position);
  }
}

void LayoutBox::ComputePositionedLogicalWidth(
    LogicalExtentComputedValues& computed_values) const {
  // QUESTIONS
  // FIXME 1: Should we still deal with these the cases of 'left' or 'right'
  // having the type 'static' in determining whether to calculate the static
  // distance?
  // NOTE: 'static' is not a legal value for 'left' or 'right' as of CSS 2.1.

  // FIXME 2: Can perhaps optimize out cases when max-width/min-width are
  // greater than or less than the computed width(). Be careful of box-sizing
  // and percentage issues.

  // The following is based off of the W3C Working Draft from April 11, 2006 of
  // CSS 2.1: Section 10.3.7 "Absolutely positioned, non-replaced elements"
  // <http://www.w3.org/TR/CSS21/visudet.html#abs-non-replaced-width>
  // (block-style-comments in this function and in
  // computePositionedLogicalWidthUsing() correspond to text from the spec)

  // We don't use containingBlock(), since we may be positioned by an enclosing
  // relative positioned inline.
  const LayoutBoxModelObject* container_block =
      ToLayoutBoxModelObject(Container());

  const LayoutUnit container_logical_width =
      ContainingBlockLogicalWidthForPositioned(container_block);

  // Use the container block's direction except when calculating the static
  // distance. This conforms with the reference results for
  // abspos-replaced-width-margin-000.htm of the CSS 2.1 test suite.
  TextDirection container_direction = container_block->Style()->Direction();

  bool is_horizontal = IsHorizontalWritingMode();
  const LayoutUnit borders_plus_padding = BorderAndPaddingLogicalWidth();
  const Length margin_logical_left =
      is_horizontal ? Style()->MarginLeft() : Style()->MarginTop();
  const Length margin_logical_right =
      is_horizontal ? Style()->MarginRight() : Style()->MarginBottom();

  Length logical_left_length = Style()->LogicalLeft();
  Length logical_right_length = Style()->LogicalRight();
  // ---------------------------------------------------------------------------
  //  For the purposes of this section and the next, the term "static position"
  //  (of an element) refers, roughly, to the position an element would have had
  //  in the normal flow. More precisely:
  //
  //  * The static position for 'left' is the distance from the left edge of the
  //    containing block to the left margin edge of a hypothetical box that
  //    would have been the first box of the element if its 'position' property
  //    had been 'static' and 'float' had been 'none'. The value is negative if
  //    the hypothetical box is to the left of the containing block.
  //  * The static position for 'right' is the distance from the right edge of
  //    the containing block to the right margin edge of the same hypothetical
  //    box as above. The value is positive if the hypothetical box is to the
  //    left of the containing block's edge.
  //
  //  But rather than actually calculating the dimensions of that hypothetical
  //  box, user agents are free to make a guess at its probable position.
  //
  //  For the purposes of calculating the static position, the containing block
  //  of fixed positioned elements is the initial containing block instead of
  //  the viewport, and all scrollable boxes should be assumed to be scrolled to
  //  their origin.
  // ---------------------------------------------------------------------------
  // see FIXME 1
  // Calculate the static distance if needed.
  ComputeInlineStaticDistance(logical_left_length, logical_right_length, this,
                              container_block, container_logical_width);

  // Calculate constraint equation values for 'width' case.
  ComputePositionedLogicalWidthUsing(
      kMainOrPreferredSize, Style()->LogicalWidth(), container_block,
      container_direction, container_logical_width, borders_plus_padding,
      logical_left_length, logical_right_length, margin_logical_left,
      margin_logical_right, computed_values);

  // Calculate constraint equation values for 'max-width' case.
  if (!Style()->LogicalMaxWidth().IsMaxSizeNone()) {
    LogicalExtentComputedValues max_values;

    ComputePositionedLogicalWidthUsing(
        kMaxSize, Style()->LogicalMaxWidth(), container_block,
        container_direction, container_logical_width, borders_plus_padding,
        logical_left_length, logical_right_length, margin_logical_left,
        margin_logical_right, max_values);

    if (computed_values.extent_ > max_values.extent_) {
      computed_values.extent_ = max_values.extent_;
      computed_values.position_ = max_values.position_;
      computed_values.margins_.start_ = max_values.margins_.start_;
      computed_values.margins_.end_ = max_values.margins_.end_;
    }
  }

  // Calculate constraint equation values for 'min-width' case.
  if (!Style()->LogicalMinWidth().IsZero() ||
      Style()->LogicalMinWidth().IsIntrinsic()) {
    LogicalExtentComputedValues min_values;

    ComputePositionedLogicalWidthUsing(
        kMinSize, Style()->LogicalMinWidth(), container_block,
        container_direction, container_logical_width, borders_plus_padding,
        logical_left_length, logical_right_length, margin_logical_left,
        margin_logical_right, min_values);

    if (computed_values.extent_ < min_values.extent_) {
      computed_values.extent_ = min_values.extent_;
      computed_values.position_ = min_values.position_;
      computed_values.margins_.start_ = min_values.margins_.start_;
      computed_values.margins_.end_ = min_values.margins_.end_;
    }
  }

  computed_values.extent_ += borders_plus_padding;
}

void LayoutBox::ComputeLogicalLeftPositionedOffset(
    LayoutUnit& logical_left_pos,
    const LayoutBox* child,
    LayoutUnit logical_width_value,
    const LayoutBoxModelObject* container_block,
    LayoutUnit container_logical_width) {
  // Deal with differing writing modes here. Our offset needs to be in the
  // containing block's coordinate space. If the containing block is flipped
  // along this axis, then we need to flip the coordinate. This can only happen
  // if the containing block is both a flipped mode and perpendicular to us.
  if (container_block->IsHorizontalWritingMode() !=
          child->IsHorizontalWritingMode() &&
      container_block->Style()->IsFlippedBlocksWritingMode()) {
    logical_left_pos =
        container_logical_width - logical_width_value - logical_left_pos;
    logical_left_pos +=
        (child->IsHorizontalWritingMode() ? container_block->BorderRight()
                                          : container_block->BorderBottom());
  } else {
    logical_left_pos +=
        (child->IsHorizontalWritingMode() ? container_block->BorderLeft()
                                          : container_block->BorderTop());
  }
}

LayoutUnit LayoutBox::ShrinkToFitLogicalWidth(
    LayoutUnit available_logical_width,
    LayoutUnit borders_plus_padding) const {
  LayoutUnit preferred_logical_width =
      MaxPreferredLogicalWidth() - borders_plus_padding;
  LayoutUnit preferred_min_logical_width =
      MinPreferredLogicalWidth() - borders_plus_padding;
  return std::min(
      std::max(preferred_min_logical_width, available_logical_width),
      preferred_logical_width);
}

void LayoutBox::ComputePositionedLogicalWidthUsing(
    SizeType width_size_type,
    Length logical_width,
    const LayoutBoxModelObject* container_block,
    TextDirection container_direction,
    LayoutUnit container_logical_width,
    LayoutUnit borders_plus_padding,
    const Length& logical_left,
    const Length& logical_right,
    const Length& margin_logical_left,
    const Length& margin_logical_right,
    LogicalExtentComputedValues& computed_values) const {
  LayoutUnit logical_width_value;

  DCHECK(width_size_type == kMinSize ||
         width_size_type == kMainOrPreferredSize || !logical_width.IsAuto());
  if (width_size_type == kMinSize && logical_width.IsAuto())
    logical_width_value = LayoutUnit();
  else if (logical_width.IsIntrinsic())
    logical_width_value =
        ComputeIntrinsicLogicalWidthUsing(
            logical_width, container_logical_width, borders_plus_padding) -
        borders_plus_padding;
  else
    logical_width_value = AdjustContentBoxLogicalWidthForBoxSizing(
        ValueForLength(logical_width, container_logical_width));

  // 'left' and 'right' cannot both be 'auto' because one would of been
  // converted to the static position already
  DCHECK(!(logical_left.IsAuto() && logical_right.IsAuto()));

  // minimumValueForLength will convert 'auto' to 0 so that it doesn't impact
  // the available space computation below.
  LayoutUnit logical_left_value =
      MinimumValueForLength(logical_left, container_logical_width);
  LayoutUnit logical_right_value =
      MinimumValueForLength(logical_right, container_logical_width);

  const LayoutUnit container_relative_logical_width =
      ContainingBlockLogicalWidthForPositioned(container_block, false);

  bool logical_width_is_auto = logical_width.IsAuto();
  bool logical_left_is_auto = logical_left.IsAuto();
  bool logical_right_is_auto = logical_right.IsAuto();
  LayoutUnit& margin_logical_left_value = Style()->IsLeftToRightDirection()
                                              ? computed_values.margins_.start_
                                              : computed_values.margins_.end_;
  LayoutUnit& margin_logical_right_value =
      Style()->IsLeftToRightDirection() ? computed_values.margins_.end_
                                        : computed_values.margins_.start_;
  if (!logical_left_is_auto && !logical_width_is_auto &&
      !logical_right_is_auto) {
    // -------------------------------------------------------------------------
    // If none of the three is 'auto': If both 'margin-left' and 'margin-
    // right' are 'auto', solve the equation under the extra constraint that
    // the two margins get equal values, unless this would make them negative,
    // in which case when direction of the containing block is 'ltr' ('rtl'),
    // set 'margin-left' ('margin-right') to zero and solve for 'margin-right'
    // ('margin-left'). If one of 'margin-left' or 'margin-right' is 'auto',
    // solve the equation for that value. If the values are over-constrained,
    // ignore the value for 'left' (in case the 'direction' property of the
    // containing block is 'rtl') or 'right' (in case 'direction' is 'ltr')
    // and solve for that value.
    // -------------------------------------------------------------------------
    // NOTE:  It is not necessary to solve for 'right' in the over constrained
    // case because the value is not used for any further calculations.

    computed_values.extent_ = logical_width_value;

    const LayoutUnit available_space =
        container_logical_width -
        (logical_left_value + computed_values.extent_ + logical_right_value +
         borders_plus_padding);

    // Margins are now the only unknown
    if (margin_logical_left.IsAuto() && margin_logical_right.IsAuto()) {
      // Both margins auto, solve for equality
      if (available_space >= 0) {
        margin_logical_left_value =
            available_space / 2;  // split the difference
        margin_logical_right_value =
            available_space -
            margin_logical_left_value;  // account for odd valued differences
      } else {
        // Use the containing block's direction rather than the parent block's
        // per CSS 2.1 reference test abspos-non-replaced-width-margin-000.
        if (container_direction == TextDirection::kLtr) {
          margin_logical_left_value = LayoutUnit();
          margin_logical_right_value = available_space;  // will be negative
        } else {
          margin_logical_left_value = available_space;  // will be negative
          margin_logical_right_value = LayoutUnit();
        }
      }
    } else if (margin_logical_left.IsAuto()) {
      // Solve for left margin
      margin_logical_right_value = ValueForLength(
          margin_logical_right, container_relative_logical_width);
      margin_logical_left_value = available_space - margin_logical_right_value;
    } else if (margin_logical_right.IsAuto()) {
      // Solve for right margin
      margin_logical_left_value =
          ValueForLength(margin_logical_left, container_relative_logical_width);
      margin_logical_right_value = available_space - margin_logical_left_value;
    } else {
      // Over-constrained, solve for left if direction is RTL
      margin_logical_left_value =
          ValueForLength(margin_logical_left, container_relative_logical_width);
      margin_logical_right_value = ValueForLength(
          margin_logical_right, container_relative_logical_width);

      // Use the containing block's direction rather than the parent block's
      // per CSS 2.1 reference test abspos-non-replaced-width-margin-000.
      if (container_direction == TextDirection::kRtl)
        logical_left_value = (available_space + logical_left_value) -
                             margin_logical_left_value -
                             margin_logical_right_value;
    }
  } else {
    // -------------------------------------------------------------------------
    // Otherwise, set 'auto' values for 'margin-left' and 'margin-right'
    // to 0, and pick the one of the following six rules that applies.
    //
    // 1. 'left' and 'width' are 'auto' and 'right' is not 'auto', then the
    //    width is shrink-to-fit. Then solve for 'left'
    //
    //              OMIT RULE 2 AS IT SHOULD NEVER BE HIT
    // ------------------------------------------------------------------
    // 2. 'left' and 'right' are 'auto' and 'width' is not 'auto', then if
    //    the 'direction' property of the containing block is 'ltr' set
    //    'left' to the static position, otherwise set 'right' to the
    //    static position. Then solve for 'left' (if 'direction is 'rtl')
    //    or 'right' (if 'direction' is 'ltr').
    // ------------------------------------------------------------------
    //
    // 3. 'width' and 'right' are 'auto' and 'left' is not 'auto', then the
    //    width is shrink-to-fit . Then solve for 'right'
    // 4. 'left' is 'auto', 'width' and 'right' are not 'auto', then solve
    //    for 'left'
    // 5. 'width' is 'auto', 'left' and 'right' are not 'auto', then solve
    //    for 'width'
    // 6. 'right' is 'auto', 'left' and 'width' are not 'auto', then solve
    //    for 'right'
    //
    // Calculation of the shrink-to-fit width is similar to calculating the
    // width of a table cell using the automatic table layout algorithm.
    // Roughly: calculate the preferred width by formatting the content without
    // breaking lines other than where explicit line breaks occur, and also
    // calculate the preferred minimum width, e.g., by trying all possible line
    // breaks. CSS 2.1 does not define the exact algorithm.
    // Thirdly, calculate the available width: this is found by solving for
    // 'width' after setting 'left' (in case 1) or 'right' (in case 3) to 0.
    //
    // Then the shrink-to-fit width is:
    // min(max(preferred minimum width, available width), preferred width).
    // -------------------------------------------------------------------------
    // NOTE: For rules 3 and 6 it is not necessary to solve for 'right'
    // because the value is not used for any further calculations.

    // Calculate margins, 'auto' margins are ignored.
    margin_logical_left_value = MinimumValueForLength(
        margin_logical_left, container_relative_logical_width);
    margin_logical_right_value = MinimumValueForLength(
        margin_logical_right, container_relative_logical_width);

    const LayoutUnit available_space =
        container_logical_width -
        (margin_logical_left_value + margin_logical_right_value +
         logical_left_value + logical_right_value + borders_plus_padding);

    // FIXME: Is there a faster way to find the correct case?
    // Use rule/case that applies.
    if (logical_left_is_auto && logical_width_is_auto &&
        !logical_right_is_auto) {
      // RULE 1: (use shrink-to-fit for width, and solve of left)
      computed_values.extent_ =
          ShrinkToFitLogicalWidth(available_space, borders_plus_padding);
      logical_left_value = available_space - computed_values.extent_;
    } else if (!logical_left_is_auto && logical_width_is_auto &&
               logical_right_is_auto) {
      // RULE 3: (use shrink-to-fit for width, and no need solve of right)
      computed_values.extent_ =
          ShrinkToFitLogicalWidth(available_space, borders_plus_padding);
    } else if (logical_left_is_auto && !logical_width_is_auto &&
               !logical_right_is_auto) {
      // RULE 4: (solve for left)
      computed_values.extent_ = logical_width_value;
      logical_left_value = available_space - computed_values.extent_;
    } else if (!logical_left_is_auto && logical_width_is_auto &&
               !logical_right_is_auto) {
      // RULE 5: (solve for width)
      if (AutoWidthShouldFitContent())
        computed_values.extent_ =
            ShrinkToFitLogicalWidth(available_space, borders_plus_padding);
      else
        computed_values.extent_ = std::max(LayoutUnit(), available_space);
    } else if (!logical_left_is_auto && !logical_width_is_auto &&
               logical_right_is_auto) {
      // RULE 6: (no need solve for right)
      computed_values.extent_ = logical_width_value;
    }
  }

  // Use computed values to calculate the horizontal position.

  // FIXME: This hack is needed to calculate the  logical left position for a
  // 'rtl' relatively positioned, inline because right now, it is using the
  // logical left position of the first line box when really it should use the
  // last line box. When this is fixed elsewhere, this block should be removed.
  if (container_block->IsLayoutInline() &&
      !container_block->Style()->IsLeftToRightDirection()) {
    const LayoutInline* flow = ToLayoutInline(container_block);
    InlineFlowBox* first_line = flow->FirstLineBox();
    InlineFlowBox* last_line = flow->LastLineBox();
    if (first_line && last_line && first_line != last_line) {
      computed_values.position_ =
          logical_left_value + margin_logical_left_value +
          last_line->BorderLogicalLeft() +
          (last_line->LogicalLeft() - first_line->LogicalLeft());
      return;
    }
  }

  if (container_block->IsBox() &&
      ToLayoutBox(container_block)->ScrollsOverflowY() &&
      ToLayoutBox(container_block)
          ->ShouldPlaceBlockDirectionScrollbarOnLogicalLeft()) {
    logical_left_value = logical_left_value +
                         ToLayoutBox(container_block)->VerticalScrollbarWidth();
  }

  computed_values.position_ = logical_left_value + margin_logical_left_value;
  ComputeLogicalLeftPositionedOffset(computed_values.position_, this,
                                     computed_values.extent_, container_block,
                                     container_logical_width);
}

void LayoutBox::ComputeBlockStaticDistance(
    Length& logical_top,
    Length& logical_bottom,
    const LayoutBox* child,
    const LayoutBoxModelObject* container_block) {
  if (!logical_top.IsAuto() || !logical_bottom.IsAuto())
    return;

  // FIXME: The static distance computation has not been patched for mixed
  // writing modes.
  LayoutUnit static_logical_top = child->Layer()->StaticBlockPosition();
  for (LayoutObject* curr = child->Parent(); curr && curr != container_block;
       curr = curr->Container()) {
    if (!curr->IsBox() || curr->IsTableRow())
      continue;
    const LayoutBox& box = *ToLayoutBox(curr);
    static_logical_top += box.LogicalTop();
    if (box.IsInFlowPositioned())
      static_logical_top += box.OffsetForInFlowPosition().Height();
    if (!box.IsLayoutFlowThread())
      continue;
    // We're walking out of a flowthread here. This flow thread is not in the
    // containing block chain, so we need to convert the position from the
    // coordinate space of this flowthread to the containing coordinate space.
    // The inline position cannot affect the block position, so we don't bother
    // calculating it.
    LayoutUnit dummy_inline_position;
    ToLayoutFlowThread(box).FlowThreadToContainingCoordinateSpace(
        static_logical_top, dummy_inline_position);
  }
  logical_top.SetValue(kFixed,
                       static_logical_top - container_block->BorderBefore());
}

void LayoutBox::ComputePositionedLogicalHeight(
    LogicalExtentComputedValues& computed_values) const {
  // The following is based off of the W3C Working Draft from April 11, 2006 of
  // CSS 2.1: Section 10.6.4 "Absolutely positioned, non-replaced elements"
  // <http://www.w3.org/TR/2005/WD-CSS21-20050613/visudet.html#abs-non-replaced-height>
  // (block-style-comments in this function and in
  // computePositionedLogicalHeightUsing()
  // correspond to text from the spec)

  // We don't use containingBlock(), since we may be positioned by an enclosing
  // relpositioned inline.
  const LayoutBoxModelObject* container_block =
      ToLayoutBoxModelObject(Container());

  const LayoutUnit container_logical_height =
      ContainingBlockLogicalHeightForPositioned(container_block);

  const ComputedStyle& style_to_use = StyleRef();
  const LayoutUnit borders_plus_padding = BorderAndPaddingLogicalHeight();
  const Length margin_before = style_to_use.MarginBefore();
  const Length margin_after = style_to_use.MarginAfter();
  Length logical_top_length = style_to_use.LogicalTop();
  Length logical_bottom_length = style_to_use.LogicalBottom();

  // ---------------------------------------------------------------------------
  // For the purposes of this section and the next, the term "static position"
  // (of an element) refers, roughly, to the position an element would have had
  // in the normal flow. More precisely, the static position for 'top' is the
  // distance from the top edge of the containing block to the top margin edge
  // of a hypothetical box that would have been the first box of the element if
  // its 'position' property had been 'static' and 'float' had been 'none'. The
  // value is negative if the hypothetical box is above the containing block.
  //
  // But rather than actually calculating the dimensions of that hypothetical
  // box, user agents are free to make a guess at its probable position.
  //
  // For the purposes of calculating the static position, the containing block
  // of fixed positioned elements is the initial containing block instead of
  // the viewport.
  // ---------------------------------------------------------------------------
  // see FIXME 1
  // Calculate the static distance if needed.
  ComputeBlockStaticDistance(logical_top_length, logical_bottom_length, this,
                             container_block);

  // Calculate constraint equation values for 'height' case.
  LayoutUnit logical_height = computed_values.extent_;
  ComputePositionedLogicalHeightUsing(
      kMainOrPreferredSize, style_to_use.LogicalHeight(), container_block,
      container_logical_height, borders_plus_padding, logical_height,
      logical_top_length, logical_bottom_length, margin_before, margin_after,
      computed_values);

  // Avoid doing any work in the common case (where the values of min-height and
  // max-height are their defaults).
  // see FIXME 2

  // Calculate constraint equation values for 'max-height' case.
  if (!style_to_use.LogicalMaxHeight().IsMaxSizeNone()) {
    LogicalExtentComputedValues max_values;

    ComputePositionedLogicalHeightUsing(
        kMaxSize, style_to_use.LogicalMaxHeight(), container_block,
        container_logical_height, borders_plus_padding, logical_height,
        logical_top_length, logical_bottom_length, margin_before, margin_after,
        max_values);

    if (computed_values.extent_ > max_values.extent_) {
      computed_values.extent_ = max_values.extent_;
      computed_values.position_ = max_values.position_;
      computed_values.margins_.before_ = max_values.margins_.before_;
      computed_values.margins_.after_ = max_values.margins_.after_;
    }
  }

  // Calculate constraint equation values for 'min-height' case.
  if (!style_to_use.LogicalMinHeight().IsZero() ||
      style_to_use.LogicalMinHeight().IsIntrinsic()) {
    LogicalExtentComputedValues min_values;

    ComputePositionedLogicalHeightUsing(
        kMinSize, style_to_use.LogicalMinHeight(), container_block,
        container_logical_height, borders_plus_padding, logical_height,
        logical_top_length, logical_bottom_length, margin_before, margin_after,
        min_values);

    if (computed_values.extent_ < min_values.extent_) {
      computed_values.extent_ = min_values.extent_;
      computed_values.position_ = min_values.position_;
      computed_values.margins_.before_ = min_values.margins_.before_;
      computed_values.margins_.after_ = min_values.margins_.after_;
    }
  }

  // Set final height value.
  computed_values.extent_ += borders_plus_padding;
}

void LayoutBox::ComputeLogicalTopPositionedOffset(
    LayoutUnit& logical_top_pos,
    const LayoutBox* child,
    LayoutUnit logical_height_value,
    const LayoutBoxModelObject* container_block,
    LayoutUnit container_logical_height) {
  // Deal with differing writing modes here. Our offset needs to be in the
  // containing block's coordinate space. If the containing block is flipped
  // along this axis, then we need to flip the coordinate.  This can only happen
  // if the containing block is both a flipped mode and perpendicular to us.
  if ((child->Style()->IsFlippedBlocksWritingMode() &&
       child->IsHorizontalWritingMode() !=
           container_block->IsHorizontalWritingMode()) ||
      (child->Style()->IsFlippedBlocksWritingMode() !=
           container_block->Style()->IsFlippedBlocksWritingMode() &&
       child->IsHorizontalWritingMode() ==
           container_block->IsHorizontalWritingMode()))
    logical_top_pos =
        container_logical_height - logical_height_value - logical_top_pos;

  // Our offset is from the logical bottom edge in a flipped environment, e.g.,
  // right for vertical-rl.
  if (container_block->Style()->IsFlippedBlocksWritingMode() &&
      child->IsHorizontalWritingMode() ==
          container_block->IsHorizontalWritingMode()) {
    if (child->IsHorizontalWritingMode())
      logical_top_pos += container_block->BorderBottom();
    else
      logical_top_pos += container_block->BorderRight();
  } else {
    if (child->IsHorizontalWritingMode())
      logical_top_pos += container_block->BorderTop();
    else
      logical_top_pos += container_block->BorderLeft();
  }
}

void LayoutBox::ComputePositionedLogicalHeightUsing(
    SizeType height_size_type,
    Length logical_height_length,
    const LayoutBoxModelObject* container_block,
    LayoutUnit container_logical_height,
    LayoutUnit borders_plus_padding,
    LayoutUnit logical_height,
    const Length& logical_top,
    const Length& logical_bottom,
    const Length& margin_before,
    const Length& margin_after,
    LogicalExtentComputedValues& computed_values) const {
  DCHECK(height_size_type == kMinSize ||
         height_size_type == kMainOrPreferredSize ||
         !logical_height_length.IsAuto());
  if (height_size_type == kMinSize && logical_height_length.IsAuto())
    logical_height_length = Length(0, kFixed);

  // 'top' and 'bottom' cannot both be 'auto' because 'top would of been
  // converted to the static position in computePositionedLogicalHeight()
  DCHECK(!(logical_top.IsAuto() && logical_bottom.IsAuto()));

  LayoutUnit logical_height_value;
  LayoutUnit content_logical_height = logical_height - borders_plus_padding;

  const LayoutUnit container_relative_logical_width =
      ContainingBlockLogicalWidthForPositioned(container_block, false);

  LayoutUnit logical_top_value;

  bool logical_height_is_auto = logical_height_length.IsAuto();
  bool logical_top_is_auto = logical_top.IsAuto();
  bool logical_bottom_is_auto = logical_bottom.IsAuto();

  LayoutUnit resolved_logical_height;
  // Height is never unsolved for tables.
  if (IsTable()) {
    resolved_logical_height = content_logical_height;
    logical_height_is_auto = false;
  } else {
    if (logical_height_length.IsIntrinsic())
      resolved_logical_height = ComputeIntrinsicLogicalContentHeightUsing(
          logical_height_length, content_logical_height, borders_plus_padding);
    else
      resolved_logical_height = AdjustContentBoxLogicalHeightForBoxSizing(
          ValueForLength(logical_height_length, container_logical_height));
  }

  if (!logical_top_is_auto && !logical_height_is_auto &&
      !logical_bottom_is_auto) {
    // -------------------------------------------------------------------------
    // If none of the three are 'auto': If both 'margin-top' and 'margin-bottom'
    // are 'auto', solve the equation under the extra constraint that the two
    // margins get equal values. If one of 'margin-top' or 'margin- bottom' is
    // 'auto', solve the equation for that value. If the values are over-
    // constrained, ignore the value for 'bottom' and solve for that value.
    // -------------------------------------------------------------------------
    // NOTE:  It is not necessary to solve for 'bottom' in the over constrained
    // case because the value is not used for any further calculations.

    logical_height_value = resolved_logical_height;
    logical_top_value = ValueForLength(logical_top, container_logical_height);

    const LayoutUnit available_space =
        container_logical_height -
        (logical_top_value + logical_height_value +
         ValueForLength(logical_bottom, container_logical_height) +
         borders_plus_padding);

    // Margins are now the only unknown
    if (margin_before.IsAuto() && margin_after.IsAuto()) {
      // Both margins auto, solve for equality
      // NOTE: This may result in negative values.
      computed_values.margins_.before_ =
          available_space / 2;  // split the difference
      computed_values.margins_.after_ =
          available_space - computed_values.margins_
                                .before_;  // account for odd valued differences
    } else if (margin_before.IsAuto()) {
      // Solve for top margin
      computed_values.margins_.after_ =
          ValueForLength(margin_after, container_relative_logical_width);
      computed_values.margins_.before_ =
          available_space - computed_values.margins_.after_;
    } else if (margin_after.IsAuto()) {
      // Solve for bottom margin
      computed_values.margins_.before_ =
          ValueForLength(margin_before, container_relative_logical_width);
      computed_values.margins_.after_ =
          available_space - computed_values.margins_.before_;
    } else {
      // Over-constrained, (no need solve for bottom)
      computed_values.margins_.before_ =
          ValueForLength(margin_before, container_relative_logical_width);
      computed_values.margins_.after_ =
          ValueForLength(margin_after, container_relative_logical_width);
    }
  } else {
    // -------------------------------------------------------------------------
    // Otherwise, set 'auto' values for 'margin-top' and 'margin-bottom'
    // to 0, and pick the one of the following six rules that applies.
    //
    // 1. 'top' and 'height' are 'auto' and 'bottom' is not 'auto', then
    //    the height is based on the content, and solve for 'top'.
    //
    //              OMIT RULE 2 AS IT SHOULD NEVER BE HIT
    // ------------------------------------------------------------------
    // 2. 'top' and 'bottom' are 'auto' and 'height' is not 'auto', then
    //    set 'top' to the static position, and solve for 'bottom'.
    // ------------------------------------------------------------------
    //
    // 3. 'height' and 'bottom' are 'auto' and 'top' is not 'auto', then
    //    the height is based on the content, and solve for 'bottom'.
    // 4. 'top' is 'auto', 'height' and 'bottom' are not 'auto', and
    //    solve for 'top'.
    // 5. 'height' is 'auto', 'top' and 'bottom' are not 'auto', and
    //    solve for 'height'.
    // 6. 'bottom' is 'auto', 'top' and 'height' are not 'auto', and
    //    solve for 'bottom'.
    // -------------------------------------------------------------------------
    // NOTE: For rules 3 and 6 it is not necessary to solve for 'bottom'
    // because the value is not used for any further calculations.

    // Calculate margins, 'auto' margins are ignored.
    computed_values.margins_.before_ =
        MinimumValueForLength(margin_before, container_relative_logical_width);
    computed_values.margins_.after_ =
        MinimumValueForLength(margin_after, container_relative_logical_width);

    const LayoutUnit available_space =
        container_logical_height -
        (computed_values.margins_.before_ + computed_values.margins_.after_ +
         borders_plus_padding);

    // Use rule/case that applies.
    if (logical_top_is_auto && logical_height_is_auto &&
        !logical_bottom_is_auto) {
      // RULE 1: (height is content based, solve of top)
      logical_height_value = content_logical_height;
      logical_top_value =
          available_space -
          (logical_height_value +
           ValueForLength(logical_bottom, container_logical_height));
    } else if (!logical_top_is_auto && logical_height_is_auto &&
               logical_bottom_is_auto) {
      // RULE 3: (height is content based, no need solve of bottom)
      logical_top_value = ValueForLength(logical_top, container_logical_height);
      logical_height_value = content_logical_height;
    } else if (logical_top_is_auto && !logical_height_is_auto &&
               !logical_bottom_is_auto) {
      // RULE 4: (solve of top)
      logical_height_value = resolved_logical_height;
      logical_top_value =
          available_space -
          (logical_height_value +
           ValueForLength(logical_bottom, container_logical_height));
    } else if (!logical_top_is_auto && logical_height_is_auto &&
               !logical_bottom_is_auto) {
      // RULE 5: (solve of height)
      logical_top_value = ValueForLength(logical_top, container_logical_height);
      logical_height_value = std::max(
          LayoutUnit(),
          available_space -
              (logical_top_value +
               ValueForLength(logical_bottom, container_logical_height)));
    } else if (!logical_top_is_auto && !logical_height_is_auto &&
               logical_bottom_is_auto) {
      // RULE 6: (no need solve of bottom)
      logical_height_value = resolved_logical_height;
      logical_top_value = ValueForLength(logical_top, container_logical_height);
    }
  }
  computed_values.extent_ = logical_height_value;

  // Use computed values to calculate the vertical position.
  computed_values.position_ =
      logical_top_value + computed_values.margins_.before_;
  ComputeLogicalTopPositionedOffset(computed_values.position_, this,
                                    logical_height_value, container_block,
                                    container_logical_height);
}

LayoutRect LayoutBox::LocalCaretRect(InlineBox* box,
                                     int caret_offset,
                                     LayoutUnit* extra_width_to_end_of_line) {
  // VisiblePositions at offsets inside containers either a) refer to the
  // positions before/after those containers (tables and select elements) or
  // b) refer to the position inside an empty block.
  // They never refer to children.
  // FIXME: Paint the carets inside empty blocks differently than the carets
  // before/after elements.
  LayoutUnit caret_width = GetFrameView()->CaretWidth();
  LayoutRect rect(Location(), LayoutSize(caret_width, Size().Height()));
  bool ltr =
      box ? box->IsLeftToRightDirection() : Style()->IsLeftToRightDirection();

  if ((!caret_offset) ^ ltr)
    rect.Move(LayoutSize(Size().Width() - caret_width, LayoutUnit()));

  if (box) {
    RootInlineBox& root_box = box->Root();
    LayoutUnit top = root_box.LineTop();
    rect.SetY(top);
    rect.SetHeight(root_box.LineBottom() - top);
  }

  // If height of box is smaller than font height, use the latter one,
  // otherwise the caret might become invisible.
  //
  // Also, if the box is not an atomic inline-level element, always use the font
  // height. This prevents the "big caret" bug described in:
  // <rdar://problem/3777804> Deleting all content in a document can result in
  // giant tall-as-window insertion point
  //
  // FIXME: ignoring :first-line, missing good reason to take care of
  const SimpleFontData* font_data = Style()->GetFont().PrimaryFont();
  LayoutUnit font_height =
      LayoutUnit(font_data ? font_data->GetFontMetrics().Height() : 0);
  if (font_height > rect.Height() || (!IsAtomicInlineLevel() && !IsTable()))
    rect.SetHeight(font_height);

  if (extra_width_to_end_of_line)
    *extra_width_to_end_of_line = Location().X() + Size().Width() - rect.MaxX();

  // Move to local coords
  rect.MoveBy(-Location());

  // FIXME: Border/padding should be added for all elements but this workaround
  // is needed because we use offsets inside an "atomic" element to represent
  // positions before and after the element in deprecated editing offsets.
  if (GetNode() &&
      !(EditingIgnoresContent(*GetNode()) || IsDisplayInsideTable(GetNode()))) {
    rect.SetX(rect.X() + BorderLeft() + PaddingLeft());
    rect.SetY(rect.Y() + PaddingTop() + BorderTop());
  }

  if (!IsHorizontalWritingMode())
    return rect.TransposedRect();

  return rect;
}

PositionWithAffinity LayoutBox::PositionForPoint(const LayoutPoint& point) {
  // no children...return this layout object's element, if there is one, and
  // offset 0
  LayoutObject* first_child = SlowFirstChild();
  if (!first_child)
    return CreatePositionWithAffinity(
        NonPseudoNode() ? FirstPositionInOrBeforeNode(NonPseudoNode())
                        : Position());

  if (IsTable() && NonPseudoNode()) {
    LayoutUnit right = Size().Width() - VerticalScrollbarWidth();
    LayoutUnit bottom = Size().Height() - HorizontalScrollbarHeight();

    if (point.X() < 0 || point.X() > right || point.Y() < 0 ||
        point.Y() > bottom) {
      if (point.X() <= right / 2)
        return CreatePositionWithAffinity(
            FirstPositionInOrBeforeNode(NonPseudoNode()));
      return CreatePositionWithAffinity(
          LastPositionInOrAfterNode(NonPseudoNode()));
    }
  }

  // Pass off to the closest child.
  LayoutUnit min_dist = LayoutUnit::Max();
  LayoutBox* closest_layout_object = nullptr;
  LayoutPoint adjusted_point = point;
  if (IsTableRow())
    adjusted_point.MoveBy(Location());

  for (LayoutObject* layout_object = first_child; layout_object;
       layout_object = layout_object->NextSibling()) {
    if ((!layout_object->SlowFirstChild() && !layout_object->IsInline() &&
         !layout_object->IsLayoutBlockFlow()) ||
        layout_object->Style()->Visibility() != EVisibility::kVisible)
      continue;

    if (!layout_object->IsBox())
      continue;

    LayoutBox* layout_box = ToLayoutBox(layout_object);

    LayoutUnit top = layout_box->BorderTop() + layout_box->PaddingTop() +
                     (IsTableRow() ? LayoutUnit() : layout_box->Location().Y());
    LayoutUnit bottom = top + layout_box->ContentHeight();
    LayoutUnit left =
        layout_box->BorderLeft() + layout_box->PaddingLeft() +
        (IsTableRow() ? LayoutUnit() : layout_box->Location().X());
    LayoutUnit right = left + layout_box->ContentWidth();

    if (point.X() <= right && point.X() >= left && point.Y() <= top &&
        point.Y() >= bottom) {
      if (layout_box->IsTableRow())
        return layout_box->PositionForPoint(point + adjusted_point -
                                            layout_box->LocationOffset());
      return layout_box->PositionForPoint(point - layout_box->LocationOffset());
    }

    // Find the distance from (x, y) to the box.  Split the space around the box
    // into 8 pieces and use a different compare depending on which piece (x, y)
    // is in.
    LayoutPoint cmp;
    if (point.X() > right) {
      if (point.Y() < top)
        cmp = LayoutPoint(right, top);
      else if (point.Y() > bottom)
        cmp = LayoutPoint(right, bottom);
      else
        cmp = LayoutPoint(right, point.Y());
    } else if (point.X() < left) {
      if (point.Y() < top)
        cmp = LayoutPoint(left, top);
      else if (point.Y() > bottom)
        cmp = LayoutPoint(left, bottom);
      else
        cmp = LayoutPoint(left, point.Y());
    } else {
      if (point.Y() < top)
        cmp = LayoutPoint(point.X(), top);
      else
        cmp = LayoutPoint(point.X(), bottom);
    }

    LayoutSize difference = cmp - point;

    LayoutUnit dist = difference.Width() * difference.Width() +
                      difference.Height() * difference.Height();
    if (dist < min_dist) {
      closest_layout_object = layout_box;
      min_dist = dist;
    }
  }

  if (closest_layout_object)
    return closest_layout_object->PositionForPoint(
        adjusted_point - closest_layout_object->LocationOffset());
  return CreatePositionWithAffinity(
      FirstPositionInOrBeforeNode(NonPseudoNode()));
}

DISABLE_CFI_PERF
bool LayoutBox::ShrinkToAvoidFloats() const {
  // Floating objects don't shrink.  Objects that don't avoid floats don't
  // shrink.
  if (IsInline() || !AvoidsFloats() || IsFloating())
    return false;

  // Only auto width objects can possibly shrink to avoid floats.
  return Style()->Width().IsAuto();
}

DISABLE_CFI_PERF
bool LayoutBox::ShouldBeConsideredAsReplaced() const {
  // Checkboxes and radioboxes are not isAtomicInlineLevel() nor do they have
  // their own layoutObject in which to override avoidFloats().
  if (IsAtomicInlineLevel())
    return true;
  Node* node = this->GetNode();
  return node && node->IsElementNode() &&
         (ToElement(node)->IsFormControlElement() ||
          isHTMLImageElement(ToElement(node)));
}

DISABLE_CFI_PERF
bool LayoutBox::AvoidsFloats() const {
  // crbug.com/460704: This should be merged with createsNewFormattingContext().
  return ShouldBeConsideredAsReplaced() || HasOverflowClip() || IsHR() ||
         IsLegend() || IsWritingModeRoot() || IsFlexItemIncludingDeprecated() ||
         Style()->GetColumnSpan() == kColumnSpanAll ||
         Style()->ContainsPaint() || Style()->ContainsLayout() ||
         Style()->Display() == EDisplay::kFlowRoot;
}

bool LayoutBox::HasNonCompositedScrollbars() const {
  if (PaintLayerScrollableArea* scrollable_area = this->GetScrollableArea()) {
    if (scrollable_area->HasHorizontalScrollbar() &&
        !scrollable_area->LayerForHorizontalScrollbar())
      return true;
    if (scrollable_area->HasVerticalScrollbar() &&
        !scrollable_area->LayerForVerticalScrollbar())
      return true;
  }
  return false;
}

void LayoutBox::UpdateFragmentationInfoForChild(LayoutBox& child) {
  LayoutState* layout_state = View()->GetLayoutState();
  DCHECK(layout_state->IsPaginated());
  child.SetOffsetToNextPage(LayoutUnit());
  if (!PageLogicalHeightForOffset(child.LogicalTop()))
    return;

  LayoutUnit logical_top = child.LogicalTop();
  LayoutUnit logical_height = child.LogicalHeightWithVisibleOverflow();
  LayoutUnit space_left = PageRemainingLogicalHeightForOffset(
      logical_top, kAssociateWithLatterPage);
  if (space_left < logical_height)
    child.SetOffsetToNextPage(space_left);
}

bool LayoutBox::ChildNeedsRelayoutForPagination(const LayoutBox& child) const {
  // TODO(mstensho): Should try to get this to work for floats too, instead of
  // just marking and bailing here.
  if (child.IsFloating())
    return true;
  const LayoutFlowThread* flow_thread = child.FlowThreadContainingBlock();
  LayoutUnit logical_top = child.LogicalTop();
  // Figure out if we really need to force re-layout of the child. We only need
  // to do this if there's a chance that we need to recalculate pagination
  // struts inside.
  if (PageLogicalHeightForOffset(logical_top)) {
    LayoutUnit logical_height = child.LogicalHeightWithVisibleOverflow();
    LayoutUnit remaining_space = PageRemainingLogicalHeightForOffset(
        logical_top, kAssociateWithLatterPage);
    if (child.OffsetToNextPage()) {
      // We need to relayout unless we're going to break at the exact same
      // location as before.
      if (child.OffsetToNextPage() != remaining_space)
        return true;
      // If column height isn't guaranteed to be uniform, we have no way of
      // telling what has happened after the first break.
      if (flow_thread && flow_thread->MayHaveNonUniformPageLogicalHeight())
        return true;
    } else if (logical_height > remaining_space) {
      // Last time we laid out this child, we didn't need to break, but now we
      // have to. So we need to relayout.
      return true;
    }
  } else if (child.OffsetToNextPage()) {
    // This child did previously break, but it won't anymore, because we no
    // longer have a known fragmentainer height.
    return true;
  }

  // It seems that we can skip layout of this child, but we need to ask the flow
  // thread for permission first. We currently cannot skip over objects
  // containing column spanners.
  return flow_thread && !flow_thread->CanSkipLayout(child);
}

void LayoutBox::MarkChildForPaginationRelayoutIfNeeded(
    LayoutBox& child,
    SubtreeLayoutScope& layout_scope) {
  DCHECK(!child.NeedsLayout());
  LayoutState* layout_state = View()->GetLayoutState();

  if (layout_state->PaginationStateChanged() ||
      (layout_state->IsPaginated() && ChildNeedsRelayoutForPagination(child)))
    layout_scope.SetChildNeedsLayout(&child);
}

void LayoutBox::MarkOrthogonalWritingModeRoot() {
  DCHECK(GetFrameView());
  GetFrameView()->AddOrthogonalWritingModeRoot(*this);
}

void LayoutBox::UnmarkOrthogonalWritingModeRoot() {
  DCHECK(GetFrameView());
  GetFrameView()->RemoveOrthogonalWritingModeRoot(*this);
}

void LayoutBox::AddVisualEffectOverflow() {
  if (!Style()->HasVisualOverflowingEffect())
    return;

  // Add in the final overflow with shadows, outsets and outline combined.
  LayoutRect visual_effect_overflow = BorderBoxRect();
  visual_effect_overflow.Expand(ComputeVisualEffectOverflowOutsets());
  AddSelfVisualOverflow(visual_effect_overflow);
}

LayoutRectOutsets LayoutBox::ComputeVisualEffectOverflowOutsets() {
  DCHECK(Style()->HasVisualOverflowingEffect());

  LayoutUnit top;
  LayoutUnit right;
  LayoutUnit bottom;
  LayoutUnit left;

  if (const ShadowList* box_shadow = Style()->BoxShadow()) {
    // FIXME: Use LayoutUnit edge outsets, and then simplify this.
    FloatRectOutsets outsets = box_shadow->RectOutsetsIncludingOriginal();
    top = LayoutUnit(outsets.Top());
    right = LayoutUnit(outsets.Right());
    bottom = LayoutUnit(outsets.Bottom());
    left = LayoutUnit(outsets.Left());
  }

  if (Style()->HasBorderImageOutsets()) {
    LayoutRectOutsets border_outsets = Style()->BorderImageOutsets();
    top = std::max(top, border_outsets.Top());
    right = std::max(right, border_outsets.Right());
    bottom = std::max(bottom, border_outsets.Bottom());
    left = std::max(left, border_outsets.Left());
  }

  // Box-shadow and border-image-outsets are in physical direction. Flip into
  // block direction.
  if (UNLIKELY(HasFlippedBlocksWritingMode()))
    std::swap(left, right);

  if (Style()->HasOutline()) {
    Vector<LayoutRect> outline_rects;
    // The result rects are in coordinates of this object's border box.
    AddOutlineRects(outline_rects, LayoutPoint(),
                    OutlineRectsShouldIncludeBlockVisualOverflow());
    LayoutRect rect = UnionRectEvenIfEmpty(outline_rects);
    SetOutlineMayBeAffectedByDescendants(rect.Size() != Size());

    int outline_outset = Style()->OutlineOutsetExtent();
    top = std::max(top, -rect.Y() + outline_outset);
    right = std::max(right, rect.MaxX() - Size().Width() + outline_outset);
    bottom = std::max(bottom, rect.MaxY() - Size().Height() + outline_outset);
    left = std::max(left, -rect.X() + outline_outset);
  }

  return LayoutRectOutsets(top, right, bottom, left);
}

DISABLE_CFI_PERF
void LayoutBox::AddOverflowFromChild(const LayoutBox& child,
                                     const LayoutSize& delta) {
  // Never allow flow threads to propagate overflow up to a parent.
  if (child.IsLayoutFlowThread())
    return;

  // Only propagate layout overflow from the child if the child isn't clipping
  // its overflow.  If it is, then its overflow is internal to it, and we don't
  // care about it. LayoutOverflowRectForPropagation takes care of this and just
  // propagates the border box rect instead.
  LayoutRect child_layout_overflow_rect =
      child.LayoutOverflowRectForPropagation();
  child_layout_overflow_rect.Move(delta);
  AddLayoutOverflow(child_layout_overflow_rect);

  // Add in visual overflow from the child.  Even if the child clips its
  // overflow, it may still have visual overflow of its own set from box shadows
  // or reflections. It is unnecessary to propagate this overflow if we are
  // clipping our own overflow.
  if (child.HasSelfPaintingLayer())
    return;
  LayoutRect child_visual_overflow_rect =
      child.VisualOverflowRectForPropagation();
  child_visual_overflow_rect.Move(delta);
  AddContentsVisualOverflow(child_visual_overflow_rect);
}

bool LayoutBox::HasTopOverflow() const {
  return !Style()->IsLeftToRightDirection() && !IsHorizontalWritingMode();
}

bool LayoutBox::HasLeftOverflow() const {
  return !Style()->IsLeftToRightDirection() && IsHorizontalWritingMode();
}

DISABLE_CFI_PERF
void LayoutBox::AddLayoutOverflow(const LayoutRect& rect) {
  if (rect.IsEmpty())
    return;

  LayoutRect client_box = NoOverflowRect();
  if (client_box.Contains(rect))
    return;

  // For overflow clip objects, we don't want to propagate overflow into
  // unreachable areas.
  LayoutRect overflow_rect(rect);
  if (HasOverflowClip() || IsLayoutView()) {
    // Overflow is in the block's coordinate space and thus is flipped for
    // vertical-rl writing
    // mode.  At this stage that is actually a simplification, since we can
    // treat vertical-lr/rl
    // as the same.
    if (HasTopOverflow())
      overflow_rect.ShiftMaxYEdgeTo(
          std::min(overflow_rect.MaxY(), client_box.MaxY()));
    else
      overflow_rect.ShiftYEdgeTo(std::max(overflow_rect.Y(), client_box.Y()));
    if (HasLeftOverflow())
      overflow_rect.ShiftMaxXEdgeTo(
          std::min(overflow_rect.MaxX(), client_box.MaxX()));
    else
      overflow_rect.ShiftXEdgeTo(std::max(overflow_rect.X(), client_box.X()));

    // Now re-test with the adjusted rectangle and see if it has become
    // unreachable or fully
    // contained.
    if (client_box.Contains(overflow_rect) || overflow_rect.IsEmpty())
      return;
  }

  if (!overflow_) {
    overflow_ =
        WTF::WrapUnique(new BoxOverflowModel(client_box, BorderBoxRect()));
  }

  overflow_->AddLayoutOverflow(overflow_rect);
}

void LayoutBox::AddSelfVisualOverflow(const LayoutRect& rect) {
  if (rect.IsEmpty())
    return;

  LayoutRect border_box = BorderBoxRect();
  if (border_box.Contains(rect))
    return;

  if (!overflow_) {
    overflow_ =
        WTF::WrapUnique(new BoxOverflowModel(NoOverflowRect(), border_box));
  }

  overflow_->AddSelfVisualOverflow(rect);
}

void LayoutBox::AddContentsVisualOverflow(const LayoutRect& rect) {
  if (rect.IsEmpty())
    return;

  // If hasOverflowClip() we always save contents visual overflow because we
  // need it
  // e.g. to determine whether to apply rounded corner clip on contents.
  // Otherwise we save contents visual overflow only if it overflows the border
  // box.
  LayoutRect border_box = BorderBoxRect();
  if (!HasOverflowClip() && border_box.Contains(rect))
    return;

  if (!overflow_) {
    overflow_ =
        WTF::WrapUnique(new BoxOverflowModel(NoOverflowRect(), border_box));
  }
  overflow_->AddContentsVisualOverflow(rect);
}

void LayoutBox::ClearLayoutOverflow() {
  if (!overflow_)
    return;

  if (!HasSelfVisualOverflow() && ContentsVisualOverflowRect().IsEmpty()) {
    ClearAllOverflows();
    return;
  }

  overflow_->SetLayoutOverflow(NoOverflowRect());
}

bool LayoutBox::PercentageLogicalHeightIsResolvable() const {
  Length fake_length(100, kPercent);
  return ComputePercentageLogicalHeight(fake_length) != -1;
}

DISABLE_CFI_PERF
bool LayoutBox::HasUnsplittableScrollingOverflow() const {
  // We will paginate as long as we don't scroll overflow in the pagination
  // direction.
  bool is_horizontal = IsHorizontalWritingMode();
  if ((is_horizontal && !ScrollsOverflowY()) ||
      (!is_horizontal && !ScrollsOverflowX()))
    return false;

  // Fragmenting scrollbars is only problematic in interactive media, e.g.
  // multicol on a screen. If we're printing, which is non-interactive media, we
  // should allow objects with non-visible overflow to be paginated as normally.
  if (GetDocument().Printing())
    return false;

  // We do have overflow. We'll still be willing to paginate as long as the
  // block has auto logical height, auto or undefined max-logical-height and a
  // zero or auto min-logical-height.
  // Note this is just a heuristic, and it's still possible to have overflow
  // under these conditions, but it should work out to be good enough for common
  // cases. Paginating overflow with scrollbars present is not the end of the
  // world and is what we used to do in the old model anyway.
  return !Style()->LogicalHeight().IsIntrinsicOrAuto() ||
         (!Style()->LogicalMaxHeight().IsIntrinsicOrAuto() &&
          !Style()->LogicalMaxHeight().IsMaxSizeNone() &&
          (!Style()->LogicalMaxHeight().IsPercentOrCalc() ||
           PercentageLogicalHeightIsResolvable())) ||
         (!Style()->LogicalMinHeight().IsIntrinsicOrAuto() &&
          Style()->LogicalMinHeight().IsPositive() &&
          (!Style()->LogicalMinHeight().IsPercentOrCalc() ||
           PercentageLogicalHeightIsResolvable()));
}

LayoutBox::PaginationBreakability LayoutBox::GetPaginationBreakability() const {
  // TODO(mstensho): It is wrong to check isAtomicInlineLevel() as we
  // actually look for replaced elements.
  if (IsAtomicInlineLevel() || HasUnsplittableScrollingOverflow() ||
      (Parent() && IsWritingModeRoot()) ||
      (IsOutOfFlowPositioned() && Style()->GetPosition() == EPosition::kFixed))
    return kForbidBreaks;

  EBreakInside break_value = BreakInside();
  if (break_value == EBreakInside::kAvoid ||
      break_value == EBreakInside::kAvoidPage ||
      break_value == EBreakInside::kAvoidColumn)
    return kAvoidBreaks;
  return kAllowAnyBreaks;
}

LayoutUnit LayoutBox::LineHeight(bool /*firstLine*/,
                                 LineDirectionMode direction,
                                 LinePositionMode /*linePositionMode*/) const {
  if (IsAtomicInlineLevel()) {
    return direction == kHorizontalLine ? MarginHeight() + Size().Height()
                                        : MarginWidth() + Size().Width();
  }
  return LayoutUnit();
}

DISABLE_CFI_PERF
int LayoutBox::BaselinePosition(FontBaseline baseline_type,
                                bool /*firstLine*/,
                                LineDirectionMode direction,
                                LinePositionMode line_position_mode) const {
  DCHECK_EQ(line_position_mode, kPositionOnContainingLine);
  if (IsAtomicInlineLevel()) {
    int result = direction == kHorizontalLine
                     ? RoundToInt(MarginHeight() + Size().Height())
                     : RoundToInt(MarginWidth() + Size().Width());
    if (baseline_type == kAlphabeticBaseline)
      return result;
    return result - result / 2;
  }
  return 0;
}

PaintLayer* LayoutBox::EnclosingFloatPaintingLayer() const {
  const LayoutObject* curr = this;
  while (curr) {
    PaintLayer* layer =
        curr->HasLayer() && curr->IsBox() ? ToLayoutBox(curr)->Layer() : 0;
    if (layer && layer->IsSelfPaintingLayer())
      return layer;
    curr = curr->Parent();
  }
  return nullptr;
}

LayoutRect LayoutBox::LogicalVisualOverflowRectForPropagation() const {
  LayoutRect rect = VisualOverflowRectForPropagation();
  if (!Parent()->StyleRef().IsHorizontalWritingMode())
    return rect.TransposedRect();
  return rect;
}

DISABLE_CFI_PERF
LayoutRect LayoutBox::RectForOverflowPropagation(const LayoutRect& rect) const {
  // If the child and parent are in the same blocks direction, then we don't
  // have to do anything fancy. Just return the rect.
  if (Parent()->StyleRef().IsFlippedBlocksWritingMode() ==
      StyleRef().IsFlippedBlocksWritingMode())
    return rect;

  // Convert the rect into parent's blocks direction by flipping along the y
  // axis.
  LayoutRect result = rect;
  result.SetX(Size().Width() - rect.MaxX());
  return result;
}

DISABLE_CFI_PERF
LayoutRect LayoutBox::LogicalLayoutOverflowRectForPropagation() const {
  LayoutRect rect = LayoutOverflowRectForPropagation();
  if (!Parent()->StyleRef().IsHorizontalWritingMode())
    return rect.TransposedRect();
  return rect;
}

DISABLE_CFI_PERF
LayoutRect LayoutBox::LayoutOverflowRectForPropagation() const {
  // Only propagate interior layout overflow if we don't clip it.
  LayoutRect rect = BorderBoxRect();
  // We want to include the margin, but only when it adds height. Quirky margins
  // don't contribute height nor do the margins of self-collapsing blocks.
  if (!StyleRef().HasMarginAfterQuirk() && !IsSelfCollapsingBlock())
    rect.Expand(IsHorizontalWritingMode()
                    ? LayoutSize(LayoutUnit(), MarginAfter())
                    : LayoutSize(MarginAfter(), LayoutUnit()));

  if (!HasOverflowClip())
    rect.Unite(LayoutOverflowRect());

  bool has_transform = HasLayer() && Layer()->Transform();
  if (IsInFlowPositioned() || has_transform) {
    // If we are relatively positioned or if we have a transform, then we have
    // to convert this rectangle into physical coordinates, apply relative
    // positioning and transforms to it, and then convert it back.
    FlipForWritingMode(rect);

    if (has_transform)
      rect = Layer()->CurrentTransform().MapRect(rect);

    if (IsInFlowPositioned())
      rect.Move(OffsetForInFlowPosition());

    // Now we need to flip back.
    FlipForWritingMode(rect);
  }

  return RectForOverflowPropagation(rect);
}

DISABLE_CFI_PERF
LayoutRect LayoutBox::NoOverflowRect() const {
  // Because of the special coordinate system used for overflow rectangles and
  // many other rectangles (not quite logical, not quite physical), we need to
  // flip the block progression coordinate in vertical-rl writing mode. In other
  // words, the rectangle returned is physical, except for the block direction
  // progression coordinate (x in vertical writing mode), which is always
  // "logical top". Apart from the flipping, this method does the same thing as
  // clientBoxRect().

  const int scroll_bar_width = VerticalScrollbarWidth();
  const int scroll_bar_height = HorizontalScrollbarHeight();
  LayoutUnit left(BorderLeft() +
                  (ShouldPlaceBlockDirectionScrollbarOnLogicalLeft()
                       ? scroll_bar_width
                       : 0));
  LayoutUnit top(BorderTop());
  LayoutUnit right(BorderRight());
  LayoutUnit bottom(BorderBottom());
  LayoutRect rect(left, top, Size().Width() - left - right,
                  Size().Height() - top - bottom);
  FlipForWritingMode(rect);
  // Subtract space occupied by scrollbars. Order is important here: first flip,
  // then subtract scrollbars. This may seem backwards and weird, since one
  // would think that a vertical scrollbar at the physical right in vertical-rl
  // ought to be at the logical left (physical right), between the logical left
  // (physical right) border and the logical left (physical right) padding. But
  // this is how the rest of the code expects us to behave. This is highly
  // related to https://bugs.webkit.org/show_bug.cgi?id=76129
  // FIXME: when the above mentioned bug is fixed, it should hopefully be
  // possible to call clientBoxRect() or paddingBoxRect() in this method, rather
  // than fiddling with the edges on our own.
  if (ShouldPlaceBlockDirectionScrollbarOnLogicalLeft())
    rect.Contract(0, scroll_bar_height);
  else
    rect.Contract(scroll_bar_width, scroll_bar_height);
  return rect;
}

LayoutRect LayoutBox::VisualOverflowRect() const {
  if (!overflow_)
    return BorderBoxRect();
  if (HasOverflowClip())
    return overflow_->SelfVisualOverflowRect();
  return UnionRect(overflow_->SelfVisualOverflowRect(),
                   overflow_->ContentsVisualOverflowRect());
}

LayoutUnit LayoutBox::OffsetLeft(const Element* parent) const {
  return AdjustedPositionRelativeTo(PhysicalLocation(), parent).X();
}

LayoutUnit LayoutBox::OffsetTop(const Element* parent) const {
  return AdjustedPositionRelativeTo(PhysicalLocation(), parent).Y();
}

LayoutPoint LayoutBox::FlipForWritingModeForChild(
    const LayoutBox* child,
    const LayoutPoint& point) const {
  if (!Style()->IsFlippedBlocksWritingMode())
    return point;

  // The child is going to add in its x(), so we have to make sure it ends up in
  // the right place.
  return LayoutPoint(point.X() + Size().Width() - child->Size().Width() -
                         (2 * child->Location().X()),
                     point.Y());
}

LayoutBox* LayoutBox::LocationContainer() const {
  // Location of a non-root SVG object derived from LayoutBox should not be
  // affected by writing-mode of the containing box (SVGRoot).
  if (IsSVGChild())
    return nullptr;

  // Normally the box's location is relative to its containing box.
  LayoutObject* container = this->Container();
  while (container && !container->IsBox())
    container = container->Container();
  return ToLayoutBox(container);
}

LayoutPoint LayoutBox::PhysicalLocation(
    const LayoutBox* flipped_blocks_container) const {
  const LayoutBox* container_box;
  if (flipped_blocks_container) {
    DCHECK_EQ(flipped_blocks_container, LocationContainer());
    container_box = flipped_blocks_container;
  } else {
    container_box = LocationContainer();
  }
  if (!container_box)
    return Location();
  return container_box->FlipForWritingModeForChild(this, Location());
}

bool LayoutBox::HasRelativeLogicalWidth() const {
  return Style()->LogicalWidth().IsPercentOrCalc() ||
         Style()->LogicalMinWidth().IsPercentOrCalc() ||
         Style()->LogicalMaxWidth().IsPercentOrCalc();
}

bool LayoutBox::HasRelativeLogicalHeight() const {
  return Style()->LogicalHeight().IsPercentOrCalc() ||
         Style()->LogicalMinHeight().IsPercentOrCalc() ||
         Style()->LogicalMaxHeight().IsPercentOrCalc();
}

static void MarkBoxForRelayoutAfterSplit(LayoutBox* box) {
  // FIXME: The table code should handle that automatically. If not,
  // we should fix it and remove the table part checks.
  if (box->IsTable()) {
    // Because we may have added some sections with already computed column
    // structures, we need to sync the table structure with them now. This
    // avoids crashes when adding new cells to the table.
    ToLayoutTable(box)->ForceSectionsRecalc();
  } else if (box->IsTableSection()) {
    ToLayoutTableSection(box)->SetNeedsCellRecalc();
  }

  box->SetNeedsLayoutAndPrefWidthsRecalcAndFullPaintInvalidation(
      LayoutInvalidationReason::kAnonymousBlockChange);
}

static void CollapseLoneAnonymousBlockChild(LayoutBox* parent,
                                            LayoutObject* child) {
  if (!child->IsAnonymousBlock() || !child->IsLayoutBlockFlow())
    return;
  if (!parent->IsLayoutBlockFlow())
    return;
  ToLayoutBlockFlow(parent)->CollapseAnonymousBlockChild(
      ToLayoutBlockFlow(child));
}

LayoutObject* LayoutBox::SplitAnonymousBoxesAroundChild(
    LayoutObject* before_child) {
  LayoutBox* box_at_top_of_new_branch = nullptr;

  while (before_child->Parent() != this) {
    LayoutBox* box_to_split = ToLayoutBox(before_child->Parent());
    if (box_to_split->SlowFirstChild() != before_child &&
        box_to_split->IsAnonymous()) {
      // We have to split the parent box into two boxes and move children
      // from |beforeChild| to end into the new post box.
      LayoutBox* post_box =
          box_to_split->CreateAnonymousBoxWithSameTypeAs(this);
      post_box->SetChildrenInline(box_to_split->ChildrenInline());
      LayoutBox* parent_box = ToLayoutBox(box_to_split->Parent());
      // We need to invalidate the |parentBox| before inserting the new node
      // so that the table paint invalidation logic knows the structure is
      // dirty. See for example LayoutTableCell:localVisualRect().
      MarkBoxForRelayoutAfterSplit(parent_box);
      parent_box->VirtualChildren()->InsertChildNode(
          parent_box, post_box, box_to_split->NextSibling());
      box_to_split->MoveChildrenTo(post_box, before_child, 0, true);

      LayoutObject* child = post_box->SlowFirstChild();
      DCHECK(child);
      if (child && !child->NextSibling())
        CollapseLoneAnonymousBlockChild(post_box, child);
      child = box_to_split->SlowFirstChild();
      DCHECK(child);
      if (child && !child->NextSibling())
        CollapseLoneAnonymousBlockChild(box_to_split, child);

      MarkBoxForRelayoutAfterSplit(box_to_split);
      MarkBoxForRelayoutAfterSplit(post_box);
      box_at_top_of_new_branch = post_box;

      before_child = post_box;
    } else {
      before_child = box_to_split;
    }
  }

  // Splitting the box means the left side of the container chain will lose any
  // percent height descendants below |boxAtTopOfNewBranch| on the right hand
  // side.
  if (box_at_top_of_new_branch) {
    box_at_top_of_new_branch->ClearPercentHeightDescendants();
    MarkBoxForRelayoutAfterSplit(this);
  }

  DCHECK_EQ(before_child->Parent(), this);
  return before_child;
}

LayoutUnit LayoutBox::OffsetFromLogicalTopOfFirstPage() const {
  LayoutState* layout_state = View()->GetLayoutState();
  if (!layout_state || !layout_state->IsPaginated())
    return LayoutUnit();

  if (layout_state->GetLayoutObject() == this) {
    LayoutSize offset = layout_state->PaginationOffset();
    return IsHorizontalWritingMode() ? offset.Height() : offset.Width();
  }

  // A LayoutBlock always establishes a layout state, and this method is only
  // meant to be called on the object currently being laid out.
  DCHECK(!IsLayoutBlock());

  // In case this box doesn't establish a layout state, try the containing
  // block.
  LayoutBlock* container_block = ContainingBlock();
  DCHECK(layout_state->GetLayoutObject() == container_block);
  return container_block->OffsetFromLogicalTopOfFirstPage() + LogicalTop();
}

void LayoutBox::SetOffsetToNextPage(LayoutUnit offset) {
  if (!rare_data_ && !offset)
    return;
  EnsureRareData().offset_to_next_page_ = offset;
}

void LayoutBox::LogicalExtentAfterUpdatingLogicalWidth(
    const LayoutUnit& new_logical_top,
    LayoutBox::LogicalExtentComputedValues& computed_values) {
  // FIXME: None of this is right for perpendicular writing-mode children.
  LayoutUnit old_logical_width = LogicalWidth();
  LayoutUnit old_logical_left = LogicalLeft();
  LayoutUnit old_margin_left = MarginLeft();
  LayoutUnit old_margin_right = MarginRight();
  LayoutUnit old_logical_top = LogicalTop();

  SetLogicalTop(new_logical_top);
  UpdateLogicalWidth();

  computed_values.extent_ = LogicalWidth();
  computed_values.position_ = LogicalLeft();
  computed_values.margins_.start_ = MarginStart();
  computed_values.margins_.end_ = MarginEnd();

  SetLogicalTop(old_logical_top);
  SetLogicalWidth(old_logical_width);
  SetLogicalLeft(old_logical_left);
  SetMarginLeft(old_margin_left);
  SetMarginRight(old_margin_right);
}

bool LayoutBox::MustInvalidateFillLayersPaintOnHeightChange(
    const FillLayer& layer) {
  // Nobody will use multiple layers without wanting fancy positioning.
  if (layer.Next())
    return true;

  // Make sure we have a valid image.
  StyleImage* img = layer.GetImage();
  if (!img || !img->CanRender())
    return false;

  if (layer.RepeatY() != kRepeatFill && layer.RepeatY() != kNoRepeatFill)
    return true;

  // TODO(alancutter): Make this work correctly for calc lengths.
  if (layer.YPosition().IsPercentOrCalc() && !layer.YPosition().IsZero())
    return true;

  if (layer.BackgroundYOrigin() != kTopEdge)
    return true;

  EFillSizeType size_type = layer.SizeType();

  if (size_type == kContain || size_type == kCover)
    return true;

  if (size_type == kSizeLength) {
    // TODO(alancutter): Make this work correctly for calc lengths.
    if (layer.SizeLength().Height().IsPercentOrCalc() &&
        !layer.SizeLength().Height().IsZero())
      return true;
    if (img->IsGeneratedImage() && layer.SizeLength().Height().IsAuto())
      return true;
  } else if (img->UsesImageContainerSize()) {
    return true;
  }

  return false;
}

bool LayoutBox::MustInvalidateFillLayersPaintOnWidthChange(
    const FillLayer& layer) {
  // Nobody will use multiple layers without wanting fancy positioning.
  if (layer.Next())
    return true;

  // Make sure we have a valid image.
  StyleImage* img = layer.GetImage();
  if (!img || !img->CanRender())
    return false;

  if (layer.RepeatX() != kRepeatFill && layer.RepeatX() != kNoRepeatFill)
    return true;

  // TODO(alancutter): Make this work correctly for calc lengths.
  if (layer.XPosition().IsPercentOrCalc() && !layer.XPosition().IsZero())
    return true;

  if (layer.BackgroundXOrigin() != kLeftEdge)
    return true;

  EFillSizeType size_type = layer.SizeType();

  if (size_type == kContain || size_type == kCover)
    return true;

  if (size_type == kSizeLength) {
    // TODO(alancutter): Make this work correctly for calc lengths.
    if (layer.SizeLength().Width().IsPercentOrCalc() &&
        !layer.SizeLength().Width().IsZero())
      return true;
    if (img->IsGeneratedImage() && layer.SizeLength().Width().IsAuto())
      return true;
  } else if (img->UsesImageContainerSize()) {
    return true;
  }

  return false;
}

bool LayoutBox::MustInvalidateBackgroundOrBorderPaintOnWidthChange() const {
  if (HasMask() &&
      MustInvalidateFillLayersPaintOnWidthChange(Style()->MaskLayers()))
    return true;

  // If we don't have a background/border/mask, then nothing to do.
  if (!HasBoxDecorationBackground())
    return false;

  if (MustInvalidateFillLayersPaintOnWidthChange(Style()->BackgroundLayers()))
    return true;

  // Our fill layers are ok. Let's check border.
  if (Style()->HasBorderDecoration() && CanRenderBorderImage())
    return true;

  return false;
}

bool LayoutBox::MustInvalidateBackgroundOrBorderPaintOnHeightChange() const {
  if (HasMask() &&
      MustInvalidateFillLayersPaintOnHeightChange(Style()->MaskLayers()))
    return true;

  // If we don't have a background/border/mask, then nothing to do.
  if (!HasBoxDecorationBackground())
    return false;

  if (MustInvalidateFillLayersPaintOnHeightChange(Style()->BackgroundLayers()))
    return true;

  // Our fill layers are ok.  Let's check border.
  if (Style()->HasBorderDecoration() && CanRenderBorderImage())
    return true;

  return false;
}

bool LayoutBox::CanRenderBorderImage() const {
  if (!Style()->HasBorderDecoration())
    return false;

  StyleImage* border_image = Style()->BorderImage().GetImage();
  return border_image && border_image->CanRender() && border_image->IsLoaded();
}

ShapeOutsideInfo* LayoutBox::GetShapeOutsideInfo() const {
  return ShapeOutsideInfo::IsEnabledFor(*this) ? ShapeOutsideInfo::Info(*this)
                                               : nullptr;
}

void LayoutBox::ClearPreviousVisualRects() {
  LayoutBoxModelObject::ClearPreviousVisualRects();
  if (PaintLayerScrollableArea* scrollable_area = this->GetScrollableArea())
    scrollable_area->ClearPreviousVisualRects();
}

void LayoutBox::SetPercentHeightContainer(LayoutBlock* container) {
  DCHECK(!container || !PercentHeightContainer());
  if (!container && !rare_data_)
    return;
  EnsureRareData().percent_height_container_ = container;
}

void LayoutBox::RemoveFromPercentHeightContainer() {
  if (!PercentHeightContainer())
    return;

  DCHECK(PercentHeightContainer()->HasPercentHeightDescendant(this));
  PercentHeightContainer()->RemovePercentHeightDescendant(this);
  // The above call should call this object's
  // setPercentHeightContainer(nullptr).
  DCHECK(!PercentHeightContainer());
}

void LayoutBox::ClearPercentHeightDescendants() {
  for (LayoutObject* curr = SlowFirstChild(); curr;
       curr = curr->NextInPreOrder(this)) {
    if (curr->IsBox())
      ToLayoutBox(curr)->RemoveFromPercentHeightContainer();
  }
}

LayoutUnit LayoutBox::PageLogicalHeightForOffset(LayoutUnit offset) const {
  LayoutView* layout_view = View();
  LayoutFlowThread* flow_thread = FlowThreadContainingBlock();
  if (!flow_thread)
    return layout_view->PageLogicalHeight();
  return flow_thread->PageLogicalHeightForOffset(
      offset + OffsetFromLogicalTopOfFirstPage());
}

bool LayoutBox::IsPageLogicalHeightKnown() const {
  if (const LayoutFlowThread* flow_thread = FlowThreadContainingBlock())
    return flow_thread->IsPageLogicalHeightKnown();
  return View()->PageLogicalHeight();
}

LayoutUnit LayoutBox::PageRemainingLogicalHeightForOffset(
    LayoutUnit offset,
    PageBoundaryRule page_boundary_rule) const {
  LayoutView* layout_view = View();
  offset += OffsetFromLogicalTopOfFirstPage();

  LayoutFlowThread* flow_thread = FlowThreadContainingBlock();
  if (!flow_thread) {
    LayoutUnit page_logical_height = layout_view->PageLogicalHeight();
    LayoutUnit remaining_height =
        page_logical_height - IntMod(offset, page_logical_height);
    if (page_boundary_rule == kAssociateWithFormerPage) {
      // An offset exactly at a page boundary will act as being part of the
      // former page in question (i.e. no remaining space), rather than being
      // part of the latter (i.e. one whole page length of remaining space).
      remaining_height = IntMod(remaining_height, page_logical_height);
    }
    return remaining_height;
  }

  return flow_thread->PageRemainingLogicalHeightForOffset(offset,
                                                          page_boundary_rule);
}

bool LayoutBox::CrossesPageBoundary(LayoutUnit offset,
                                    LayoutUnit logical_height) const {
  if (!PageLogicalHeightForOffset(offset))
    return false;
  return PageRemainingLogicalHeightForOffset(offset, kAssociateWithLatterPage) <
         logical_height;
}

LayoutUnit LayoutBox::CalculatePaginationStrutToFitContent(
    LayoutUnit offset,
    LayoutUnit strut_to_next_page,
    LayoutUnit content_logical_height) const {
  DCHECK_EQ(strut_to_next_page, PageRemainingLogicalHeightForOffset(
                                    offset, kAssociateWithLatterPage));
  // If we're a cell in a row that straddles a page then avoid the repeating
  // header group if necessary.
  if (IsTableCell()) {
    const LayoutTableCell* cell = ToLayoutTableCell(this);
    if (!cell->Row()->IsFirstRowInSectionAfterHeader())
      strut_to_next_page += cell->Table()->RowOffsetFromRepeatingHeader();
  }
  LayoutUnit next_page_logical_top = offset + strut_to_next_page;
  if (PageLogicalHeightForOffset(next_page_logical_top) >=
      content_logical_height)
    return strut_to_next_page;  // Content fits just fine in the next page or
                                // column.

  // Moving to the top of the next page or column doesn't result in enough space
  // for the content that we're trying to fit. If we're in a nested
  // fragmentation context, we may find enough space if we move to a column
  // further ahead, by effectively breaking to the next outer fragmentainer.
  LayoutFlowThread* flow_thread = FlowThreadContainingBlock();
  if (!flow_thread) {
    // If there's no flow thread, we're not nested. All pages have the same
    // height. Give up.
    return strut_to_next_page;
  }
  // Start searching for a suitable offset at the top of the next page or
  // column.
  LayoutUnit flow_thread_offset =
      OffsetFromLogicalTopOfFirstPage() + next_page_logical_top;
  return strut_to_next_page +
         flow_thread->NextLogicalTopForUnbreakableContent(
             flow_thread_offset, content_logical_height) -
         flow_thread_offset;
}

LayoutBox* LayoutBox::SnapContainer() const {
  return rare_data_ ? rare_data_->snap_container_ : nullptr;
}

void LayoutBox::SetSnapContainer(LayoutBox* new_container) {
  LayoutBox* old_container = SnapContainer();
  if (old_container == new_container)
    return;

  if (old_container)
    old_container->RemoveSnapArea(*this);

  EnsureRareData().snap_container_ = new_container;

  if (new_container)
    new_container->AddSnapArea(*this);
}

void LayoutBox::ClearSnapAreas() {
  if (SnapAreaSet* areas = SnapAreas()) {
    for (auto& snap_area : *areas)
      snap_area->rare_data_->snap_container_ = nullptr;
    areas->clear();
  }
}

void LayoutBox::AddSnapArea(const LayoutBox& snap_area) {
  EnsureRareData().EnsureSnapAreas().insert(&snap_area);
}

void LayoutBox::RemoveSnapArea(const LayoutBox& snap_area) {
  if (rare_data_ && rare_data_->snap_areas_) {
    rare_data_->snap_areas_->erase(&snap_area);
  }
}

SnapAreaSet* LayoutBox::SnapAreas() const {
  return rare_data_ ? rare_data_->snap_areas_.get() : nullptr;
}

LayoutRect LayoutBox::DebugRect() const {
  LayoutRect rect = FrameRect();

  LayoutBlock* block = ContainingBlock();
  if (block)
    block->AdjustChildDebugRect(rect);

  return rect;
}

bool LayoutBox::ShouldClipOverflow() const {
  return HasOverflowClip() || StyleRef().ContainsPaint() || HasControlClip();
}

void LayoutBox::MutableForPainting::
    SavePreviousContentBoxSizeAndLayoutOverflowRect() {
  auto& rare_data = GetLayoutBox().EnsureRareData();
  rare_data.has_previous_content_box_size_and_layout_overflow_rect_ = true;
  rare_data.previous_content_box_size_ = GetLayoutBox().ContentBoxRect().Size();
  rare_data.previous_layout_overflow_rect_ =
      GetLayoutBox().LayoutOverflowRect();
}

}  // namespace blink
