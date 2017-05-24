// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/PaintInvalidationState.h"

#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/layout/LayoutInline.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutAPIShim.h"
#include "core/layout/api/LayoutPartItem.h"
#include "core/layout/svg/LayoutSVGRoot.h"
#include "core/layout/svg/SVGLayoutSupport.h"
#include "core/paint/PaintInvalidator.h"
#include "core/paint/PaintLayer.h"
#include "core/paint/PaintPropertyTreeBuilder.h"
#include "platform/graphics/paint/GeometryMapper.h"

namespace blink {

static bool SupportsCachedOffsets(const LayoutObject& object) {
  // Can't compute paint offsets across objects with transforms, but if they are
  // paint invalidation containers, we don't actually need to compute *across*
  // the container, just up to it. (Also, such objects are the containing block
  // for all children.)
  return !(object.HasTransformRelatedProperty() &&
           !object.IsPaintInvalidationContainer()) &&
         !object.HasFilterInducingProperty() && !object.IsLayoutFlowThread() &&
         !object.IsLayoutMultiColumnSpannerPlaceholder() &&
         !object.StyleRef().IsFlippedBlocksWritingMode() &&
         !(object.IsLayoutBlock() && object.IsSVG());
}

PaintInvalidationState::PaintInvalidationState(
    const LayoutView& layout_view,
    Vector<const LayoutObject*>& pending_delayed_paint_invalidations)
    : current_object_(layout_view),
      forced_subtree_invalidation_flags_(0),
      clipped_(false),
      clipped_for_absolute_position_(false),
      cached_offsets_enabled_(true),
      cached_offsets_for_absolute_position_enabled_(true),
      paint_invalidation_container_(
          &layout_view.ContainerForPaintInvalidation()),
      paint_invalidation_container_for_stacked_contents_(
          paint_invalidation_container_),
      container_for_absolute_position_(layout_view),
      pending_delayed_paint_invalidations_(pending_delayed_paint_invalidations),
      painting_layer_(*layout_view.Layer()) {
  DCHECK(!RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled());

  if (!SupportsCachedOffsets(layout_view)) {
    cached_offsets_enabled_ = false;
    return;
  }

  FloatPoint point = layout_view.LocalToAncestorPoint(
      FloatPoint(), paint_invalidation_container_,
      kTraverseDocumentBoundaries | kInputIsInFrameCoordinates);
  paint_offset_ = LayoutSize(point.X(), point.Y());
  paint_offset_for_absolute_position_ = paint_offset_;
}

PaintInvalidationState::PaintInvalidationState(
    const PaintInvalidationState& parent_state,
    const LayoutObject& current_object)
    : current_object_(current_object),
      forced_subtree_invalidation_flags_(
          parent_state.forced_subtree_invalidation_flags_),
      clipped_(parent_state.clipped_),
      clipped_for_absolute_position_(
          parent_state.clipped_for_absolute_position_),
      clip_rect_(parent_state.clip_rect_),
      clip_rect_for_absolute_position_(
          parent_state.clip_rect_for_absolute_position_),
      paint_offset_(parent_state.paint_offset_),
      paint_offset_for_absolute_position_(
          parent_state.paint_offset_for_absolute_position_),
      cached_offsets_enabled_(parent_state.cached_offsets_enabled_),
      cached_offsets_for_absolute_position_enabled_(
          parent_state.cached_offsets_for_absolute_position_enabled_),
      paint_invalidation_container_(parent_state.paint_invalidation_container_),
      paint_invalidation_container_for_stacked_contents_(
          parent_state.paint_invalidation_container_for_stacked_contents_),
      container_for_absolute_position_(
          current_object.CanContainAbsolutePositionObjects()
              ? current_object
              : parent_state.container_for_absolute_position_),
      svg_transform_(parent_state.svg_transform_),
      pending_delayed_paint_invalidations_(
          parent_state.pending_delayed_paint_invalidations_),
      painting_layer_(parent_state.ChildPaintingLayer(current_object)) {
  DCHECK(!RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled());
  DCHECK_EQ(&painting_layer_, current_object.PaintingLayer());

  if (current_object == parent_state.current_object_) {
// Sometimes we create a new PaintInvalidationState from parentState on the same
// object (e.g. LayoutView, and the HorriblySlowRectMapping cases in
// LayoutBlock::invalidatePaintOfSubtreesIfNeeded()).
// TODO(wangxianzhu): Avoid this for
// RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled().
#if DCHECK_IS_ON()
    did_update_for_children_ = parent_state.did_update_for_children_;
#endif
    return;
  }

#if DCHECK_IS_ON()
  DCHECK(parent_state.did_update_for_children_);
#endif

  if (current_object.IsPaintInvalidationContainer()) {
    paint_invalidation_container_ = ToLayoutBoxModelObject(&current_object);
    if (current_object.StyleRef().IsStackingContext())
      paint_invalidation_container_for_stacked_contents_ =
          ToLayoutBoxModelObject(&current_object);
  } else if (current_object.IsLayoutView()) {
    // m_paintInvalidationContainerForStackedContents is only for stacked
    // descendants in its own frame, because it doesn't establish stacking
    // context for stacked contents in sub-frames. Contents stacked in the root
    // stacking context in this frame should use this frame's
    // paintInvalidationContainer.
    paint_invalidation_container_for_stacked_contents_ =
        paint_invalidation_container_;
  } else if (current_object.IsFloatingWithNonContainingBlockParent() ||
             current_object.IsColumnSpanAll()) {
    // In these cases, the object may belong to an ancestor of the current
    // paint invalidation container, in paint order.
    paint_invalidation_container_ =
        &current_object.ContainerForPaintInvalidation();
    cached_offsets_enabled_ = false;
  } else if (current_object.StyleRef().IsStacked() &&
             // This is to exclude some objects (e.g. LayoutText) inheriting
             // stacked style from parent but aren't actually stacked.
             current_object.HasLayer() &&
             paint_invalidation_container_ !=
                 paint_invalidation_container_for_stacked_contents_) {
    // The current object is stacked, so we should use
    // m_paintInvalidationContainerForStackedContents as its paint invalidation
    // container on which the current object is painted.
    paint_invalidation_container_ =
        paint_invalidation_container_for_stacked_contents_;
    // We are changing paintInvalidationContainer to
    // m_paintInvalidationContainerForStackedContents. Must disable cached
    // offsets because we didn't track paint offset from
    // m_paintInvalidationContainerForStackedContents.
    // TODO(wangxianzhu): There are optimization opportunities:
    // - Like what we do for fixed-position, calculate the paint offset in slow
    //   path and enable fast path for descendants if possible; or
    // - Track offset between the two paintInvalidationContainers.
    cached_offsets_enabled_ = false;
    if (forced_subtree_invalidation_flags_ &
        PaintInvalidatorContext::kSubtreeFullInvalidationForStackedContents) {
      forced_subtree_invalidation_flags_ |=
          PaintInvalidatorContext::kSubtreeFullInvalidation;
    }
  }

  if (!current_object.IsBoxModelObject() && !current_object.IsSVG())
    return;

  if (cached_offsets_enabled_ ||
      current_object == paint_invalidation_container_)
    cached_offsets_enabled_ = SupportsCachedOffsets(current_object);

  if (current_object.IsSVG()) {
    if (current_object.IsSVGRoot()) {
      svg_transform_ =
          ToLayoutSVGRoot(current_object).LocalToBorderBoxTransform();
      // Don't early return here, because the SVGRoot object needs to execute
      // the later code as a normal LayoutBox.
    } else {
      DCHECK(current_object != paint_invalidation_container_);
      svg_transform_ *= current_object.LocalToSVGParentTransform();
      return;
    }
  }

  if (current_object == paint_invalidation_container_) {
    // When we hit a new paint invalidation container, we don't need to
    // continue forcing a check for paint invalidation, since we're
    // descending into a different invalidation container. (For instance if
    // our parents were moved, the entire container will just move.)
    if (current_object != paint_invalidation_container_for_stacked_contents_) {
      // However, we need to keep the FullInvalidationForStackedContents flag
      // if the current object isn't the paint invalidation container of
      // stacked contents.
      forced_subtree_invalidation_flags_ &=
          PaintInvalidatorContext::kSubtreeFullInvalidationForStackedContents;
    } else {
      forced_subtree_invalidation_flags_ = 0;
      if (current_object != container_for_absolute_position_ &&
          cached_offsets_for_absolute_position_enabled_ &&
          cached_offsets_enabled_) {
        // The current object is the new paintInvalidationContainer for
        // absolute-position descendants but is not their container.
        // Call updateForCurrentObject() before resetting m_paintOffset to get
        // paint offset of the current object from the original
        // paintInvalidationContainerForStackingContents, then use this paint
        // offset to adjust m_paintOffsetForAbsolutePosition.
        UpdateForCurrentObject(parent_state);
        paint_offset_for_absolute_position_ -= paint_offset_;
        if (clipped_for_absolute_position_)
          clip_rect_for_absolute_position_.Move(-paint_offset_);
      }
    }

    clipped_ = false;  // Will be updated in updateForChildren().
    paint_offset_ = LayoutSize();
    return;
  }

  UpdateForCurrentObject(parent_state);
}

PaintLayer& PaintInvalidationState::ChildPaintingLayer(
    const LayoutObject& child) const {
  if (child.HasLayer() && ToLayoutBoxModelObject(child).HasSelfPaintingLayer())
    return *ToLayoutBoxModelObject(child).Layer();
  // See LayoutObject::paintingLayer() for the special-cases of floating under
  // inline and multicolumn.
  if (child.IsColumnSpanAll() || child.IsFloatingWithNonContainingBlockParent())
    return *child.PaintingLayer();
  return painting_layer_;
}

void PaintInvalidationState::UpdateForCurrentObject(
    const PaintInvalidationState& parent_state) {
  if (!cached_offsets_enabled_)
    return;

  if (current_object_.IsLayoutView()) {
    DCHECK_EQ(&parent_state.current_object_,
              LayoutAPIShim::LayoutObjectFrom(
                  ToLayoutView(current_object_).GetFrame()->OwnerLayoutItem()));
    paint_offset_ +=
        ToLayoutBox(parent_state.current_object_).ContentBoxOffset();
    // a LayoutView paints with a defined size but a pixel-rounded offset.
    paint_offset_ = LayoutSize(RoundedIntSize(paint_offset_));
    return;
  }

  EPosition position = current_object_.StyleRef().GetPosition();

  if (position == EPosition::kFixed) {
    // Use slow path to get the offset of the fixed-position, and enable fast
    // path for descendants.
    FloatPoint fixed_offset = current_object_.LocalToAncestorPoint(
        FloatPoint(), paint_invalidation_container_,
        kTraverseDocumentBoundaries);
    if (paint_invalidation_container_->IsBox()) {
      const LayoutBox* box = ToLayoutBox(paint_invalidation_container_);
      if (box->HasOverflowClip())
        fixed_offset.Move(box->ScrolledContentOffset());
    }
    paint_offset_ = LayoutSize(fixed_offset.X(), fixed_offset.Y());
    // In the above way to get paint offset, we can't get accurate clip rect, so
    // just assume no clip. Clip on fixed-position is rare, in case that
    // paintInvalidationContainer crosses frame boundary and the LayoutView is
    // clipped by something in owner document.
    clipped_ = false;
    return;
  }

  if (position == EPosition::kAbsolute) {
    cached_offsets_enabled_ = cached_offsets_for_absolute_position_enabled_;
    if (!cached_offsets_enabled_)
      return;

    paint_offset_ = paint_offset_for_absolute_position_;
    clipped_ = clipped_for_absolute_position_;
    clip_rect_ = clip_rect_for_absolute_position_;

    // Handle absolute-position block under relative-position inline.
    const LayoutObject& container =
        parent_state.container_for_absolute_position_;
    if (container.IsInFlowPositioned() && container.IsLayoutInline())
      paint_offset_ +=
          ToLayoutInline(container).OffsetForInFlowPositionedInline(
              ToLayoutBox(current_object_));
  }

  if (current_object_.IsBox())
    paint_offset_ += ToLayoutBox(current_object_).LocationOffset();

  if (current_object_.IsInFlowPositioned() && current_object_.HasLayer())
    paint_offset_ += ToLayoutBoxModelObject(current_object_)
                         .Layer()
                         ->OffsetForInFlowPosition();
}

void PaintInvalidationState::UpdateForChildren(PaintInvalidationReason reason) {
#if DCHECK_IS_ON()
  DCHECK(!did_update_for_children_);
  did_update_for_children_ = true;
#endif

  switch (reason) {
    case PaintInvalidationReason::kDelayedFull:
      pending_delayed_paint_invalidations_.push_back(&current_object_);
      break;
    case PaintInvalidationReason::kSubtree:
      forced_subtree_invalidation_flags_ |=
          (PaintInvalidatorContext::kSubtreeFullInvalidation |
           PaintInvalidatorContext::kSubtreeFullInvalidationForStackedContents);
      break;
    case PaintInvalidationReason::kSVGResource:
      forced_subtree_invalidation_flags_ |=
          PaintInvalidatorContext::kSubtreeSVGResourceChange;
      break;
    default:
      break;
  }

  UpdateForNormalChildren();

  if (current_object_ == container_for_absolute_position_) {
    if (paint_invalidation_container_ ==
        paint_invalidation_container_for_stacked_contents_) {
      cached_offsets_for_absolute_position_enabled_ = cached_offsets_enabled_;
      if (cached_offsets_enabled_) {
        paint_offset_for_absolute_position_ = paint_offset_;
        clipped_for_absolute_position_ = clipped_;
        clip_rect_for_absolute_position_ = clip_rect_;
      }
    } else {
      // Cached offsets for absolute-position are from
      // m_paintInvalidationContainer, which can't be used if the
      // absolute-position descendants will use a different
      // paintInvalidationContainer.
      // TODO(wangxianzhu): Same optimization opportunities as under isStacked()
      // condition in the PaintInvalidationState::PaintInvalidationState(...
      // LayoutObject&...).
      cached_offsets_for_absolute_position_enabled_ = false;
    }
  }
}

void PaintInvalidationState::UpdateForNormalChildren() {
  if (!cached_offsets_enabled_)
    return;

  if (!current_object_.IsBox())
    return;
  const LayoutBox& box = ToLayoutBox(current_object_);

  if (box.IsLayoutView()) {
    if (!RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
      if (box != paint_invalidation_container_) {
        paint_offset_ -=
            LayoutSize(ToLayoutView(box).GetFrameView()->GetScrollOffset());
        AddClipRectRelativeToPaintOffset(ToLayoutView(box).ViewRect());
      }
      return;
    }
  } else if (box.IsSVGRoot()) {
    const LayoutSVGRoot& svg_root = ToLayoutSVGRoot(box);
    if (svg_root.ShouldApplyViewportClip())
      AddClipRectRelativeToPaintOffset(
          LayoutRect(LayoutPoint(), LayoutSize(svg_root.PixelSnappedSize())));
  } else if (box.IsTableRow()) {
    // Child table cell's locationOffset() includes its row's locationOffset().
    paint_offset_ -= box.LocationOffset();
  }

  if (!box.HasClipRelatedProperty())
    return;

  // Do not clip or scroll for the paint invalidation container, because the
  // semantics of visual rects do not include clipping or scrolling on that
  // object.
  if (box != paint_invalidation_container_) {
    // This won't work fully correctly for fixed-position elements, who should
    // receive CSS clip but for whom the current object is not in the containing
    // block chain.
    AddClipRectRelativeToPaintOffset(box.ClippingRect());
    if (box.HasOverflowClip())
      paint_offset_ -= box.ScrolledContentOffset();
  }

  // FIXME: <http://bugs.webkit.org/show_bug.cgi?id=13443> Apply control clip if
  // present.
}

static FloatPoint SlowLocalToAncestorPoint(const LayoutObject& object,
                                           const LayoutBoxModelObject& ancestor,
                                           const FloatPoint& point) {
  if (object.IsLayoutView())
    return ToLayoutView(object).LocalToAncestorPoint(
        point, &ancestor,
        kTraverseDocumentBoundaries | kInputIsInFrameCoordinates);
  FloatPoint result = object.LocalToAncestorPoint(point, &ancestor,
                                                  kTraverseDocumentBoundaries);
  // Paint invalidation does not include scroll of the ancestor.
  if (ancestor.IsBox()) {
    const LayoutBox* box = ToLayoutBox(&ancestor);
    if (box->HasOverflowClip())
      result.Move(box->ScrolledContentOffset());
  }
  return result;
}

LayoutPoint PaintInvalidationState::ComputeLocationInBacking(
    const LayoutPoint& visual_rect_location) const {
#if DCHECK_IS_ON()
  DCHECK(!did_update_for_children_);
#endif

  // Use visual rect location for LayoutTexts because it suffices to check
  // visual rect change for layout caused invalidation.
  if (current_object_.IsText())
    return visual_rect_location;

  FloatPoint point;
  if (paint_invalidation_container_ != &current_object_) {
    if (cached_offsets_enabled_) {
      if (current_object_.IsSVGChild())
        point = svg_transform_.MapPoint(point);
      point += FloatPoint(paint_offset_);
    } else {
      point = SlowLocalToAncestorPoint(
          current_object_, *paint_invalidation_container_, FloatPoint());
    }
  }

  PaintLayer::MapPointInPaintInvalidationContainerToBacking(
      *paint_invalidation_container_, point);

  point.Move(current_object_.ScrollAdjustmentForPaintInvalidation(
      *paint_invalidation_container_));

  return LayoutPoint(point);
}

LayoutRect PaintInvalidationState::ComputeVisualRectInBacking() const {
#if DCHECK_IS_ON()
  DCHECK(!did_update_for_children_);
#endif

  if (current_object_.IsSVGChild())
    return ComputeVisualRectInBackingForSVG();

  LayoutRect rect = current_object_.LocalVisualRect();
  MapLocalRectToVisualRectInBacking(rect);
  return rect;
}

LayoutRect PaintInvalidationState::ComputeVisualRectInBackingForSVG() const {
  LayoutRect rect;
  if (cached_offsets_enabled_) {
    FloatRect svg_rect = SVGLayoutSupport::LocalVisualRect(current_object_);
    rect = SVGLayoutSupport::TransformVisualRect(current_object_,
                                                 svg_transform_, svg_rect);
    rect.Move(paint_offset_);
    if (clipped_)
      rect.Intersect(clip_rect_);
  } else {
    // TODO(wangxianzhu): Sometimes m_cachedOffsetsEnabled==false doesn't mean
    // we can't use cached m_svgTransform. We can use hybrid fast path (for SVG)
    // and slow path (for things above the SVGRoot).
    rect = SVGLayoutSupport::VisualRectInAncestorSpace(
        current_object_, *paint_invalidation_container_);
  }

  PaintLayer::MapRectInPaintInvalidationContainerToBacking(
      *paint_invalidation_container_, rect);

  current_object_.AdjustVisualRectForRasterEffects(rect);

  rect.Move(current_object_.ScrollAdjustmentForPaintInvalidation(
      *paint_invalidation_container_));

  return rect;
}

static void SlowMapToVisualRectInAncestorSpace(
    const LayoutObject& object,
    const LayoutBoxModelObject& ancestor,
    LayoutRect& rect) {
  // TODO(wkorman): The flip below is required because visual rects are
  // currently in "physical coordinates with flipped block-flow direction"
  // (see LayoutBoxModelObject.h) but we need them to be in physical
  // coordinates.
  if (object.IsBox())
    ToLayoutBox(&object)->FlipForWritingMode(rect);

  if (object.IsLayoutView()) {
    ToLayoutView(object).MapToVisualRectInAncestorSpace(
        &ancestor, rect, kInputIsInFrameCoordinates, kDefaultVisualRectFlags);
  } else {
    object.MapToVisualRectInAncestorSpace(&ancestor, rect);
  }
}

void PaintInvalidationState::MapLocalRectToPaintInvalidationContainer(
    LayoutRect& rect) const {
#if DCHECK_IS_ON()
  DCHECK(!did_update_for_children_);
#endif

  if (cached_offsets_enabled_) {
    rect.Move(paint_offset_);
    if (clipped_)
      rect.Intersect(clip_rect_);
  } else {
    SlowMapToVisualRectInAncestorSpace(current_object_,
                                       *paint_invalidation_container_, rect);
  }
}

void PaintInvalidationState::MapLocalRectToVisualRectInBacking(
    LayoutRect& rect) const {
  MapLocalRectToPaintInvalidationContainer(rect);

  PaintLayer::MapRectInPaintInvalidationContainerToBacking(
      *paint_invalidation_container_, rect);

  current_object_.AdjustVisualRectForRasterEffects(rect);

  rect.Move(current_object_.ScrollAdjustmentForPaintInvalidation(
      *paint_invalidation_container_));
}

void PaintInvalidationState::AddClipRectRelativeToPaintOffset(
    const LayoutRect& local_clip_rect) {
  LayoutRect clip_rect = local_clip_rect;
  clip_rect.Move(paint_offset_);
  if (clipped_) {
    clip_rect_.Intersect(clip_rect);
  } else {
    clip_rect_ = clip_rect;
    clipped_ = true;
  }
}

PaintLayer& PaintInvalidationState::PaintingLayer() const {
  DCHECK_EQ(&painting_layer_, current_object_.PaintingLayer());
  return painting_layer_;
}

PaintInvalidatorContextAdapter::PaintInvalidatorContextAdapter(
    const PaintInvalidationState& paint_invalidation_state)
    : PaintInvalidatorContext(),
      paint_invalidation_state_(paint_invalidation_state) {
  subtree_flags = paint_invalidation_state.forced_subtree_invalidation_flags_;
  paint_invalidation_container =
      &paint_invalidation_state.PaintInvalidationContainer();
  painting_layer = &paint_invalidation_state.PaintingLayer();
}

void PaintInvalidatorContextAdapter::MapLocalRectToVisualRectInBacking(
    const LayoutObject& object,
    LayoutRect& rect) const {
  DCHECK_EQ(&object, &paint_invalidation_state_.CurrentObject());
  paint_invalidation_state_.MapLocalRectToVisualRectInBacking(rect);
}

}  // namespace blink
