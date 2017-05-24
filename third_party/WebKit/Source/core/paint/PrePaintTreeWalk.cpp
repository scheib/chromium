// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/paint/PrePaintTreeWalk.h"

#include "core/dom/DocumentLifecycle.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/layout/LayoutMultiColumnSpannerPlaceholder.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/LayoutView.h"
#include "core/paint/PaintLayer.h"
#include "platform/graphics/paint/GeometryMapper.h"

namespace blink {

struct PrePaintTreeWalkContext {
  PrePaintTreeWalkContext()
      : tree_builder_context(
            WTF::WrapUnique(new PaintPropertyTreeBuilderContext)),
        paint_invalidator_context(WTF::WrapUnique(new PaintInvalidatorContext)),
        ancestor_overflow_paint_layer(nullptr),
        ancestor_transformed_or_root_paint_layer(nullptr) {}

  PrePaintTreeWalkContext(const PrePaintTreeWalkContext& parent_context,
                          bool needs_tree_builder_context)
      : tree_builder_context(
            WTF::WrapUnique(needs_tree_builder_context || DCHECK_IS_ON()
                                ? new PaintPropertyTreeBuilderContext(
                                      *parent_context.tree_builder_context)
                                : nullptr)),
        paint_invalidator_context(WTF::WrapUnique(new PaintInvalidatorContext(
            *parent_context.paint_invalidator_context))),
        ancestor_overflow_paint_layer(
            parent_context.ancestor_overflow_paint_layer),
        ancestor_transformed_or_root_paint_layer(
            parent_context.ancestor_transformed_or_root_paint_layer) {
#if DCHECK_IS_ON()
    if (needs_tree_builder_context)
      DCHECK(parent_context.tree_builder_context->is_actually_needed);
    tree_builder_context->is_actually_needed = needs_tree_builder_context;
#endif
  }

  // PaintPropertyTreeBuilderContext is large and can lead to stack overflows
  // when recursion is deep so this context object is allocated on the heap.
  // See: https://crbug.com/698653.
  std::unique_ptr<PaintPropertyTreeBuilderContext> tree_builder_context;

  std::unique_ptr<PaintInvalidatorContext> paint_invalidator_context;

  // The ancestor in the PaintLayer tree which has overflow clip, or
  // is the root layer. Note that it is tree ancestor, not containing
  // block or stacking ancestor.
  PaintLayer* ancestor_overflow_paint_layer;

  // The ancestor in the PaintLayer tree which has a transform or is a root
  // layer for painting (i.e. a paint invalidation container).
  PaintLayer* ancestor_transformed_or_root_paint_layer;
};

void PrePaintTreeWalk::Walk(FrameView& root_frame) {
  DCHECK(root_frame.GetFrame().GetDocument()->Lifecycle().GetState() ==
         DocumentLifecycle::kInPrePaint);

  PrePaintTreeWalkContext initial_context;
  initial_context.ancestor_transformed_or_root_paint_layer =
      root_frame.GetLayoutView()->Layer();

  // GeometryMapper depends on paint properties.
  if (NeedsTreeBuilderContextUpdate(root_frame, initial_context))
    GeometryMapper::ClearCache();

  Walk(root_frame, initial_context);
  paint_invalidator_.ProcessPendingDelayedPaintInvalidations();
}

void PrePaintTreeWalk::Walk(FrameView& frame_view,
                            const PrePaintTreeWalkContext& parent_context) {
  if (frame_view.ShouldThrottleRendering()) {
    // Skip the throttled frame. Will update it when it becomes unthrottled.
    return;
  }

  bool needs_tree_builder_context_update =
      this->NeedsTreeBuilderContextUpdate(frame_view, parent_context);
  PrePaintTreeWalkContext context(parent_context,
                                  needs_tree_builder_context_update);
  // ancestorOverflowLayer does not cross frame boundaries.
  context.ancestor_overflow_paint_layer = nullptr;
  if (context.tree_builder_context) {
    property_tree_builder_.UpdateProperties(frame_view,
                                            *context.tree_builder_context);
  }
  paint_invalidator_.InvalidatePaint(frame_view,
                                     context.tree_builder_context.get(),
                                     *context.paint_invalidator_context);

  if (LayoutView* view = frame_view.GetLayoutView()) {
    Walk(*view, context);
#if DCHECK_IS_ON()
    view->AssertSubtreeClearedPaintInvalidationFlags();
#endif
  }
  frame_view.ClearNeedsPaintPropertyUpdate();
}

static void UpdateAuxiliaryObjectProperties(const LayoutObject& object,
                                            PrePaintTreeWalkContext& context) {
  if (!RuntimeEnabledFeatures::slimmingPaintV2Enabled())
    return;

  if (!object.HasLayer())
    return;

  PaintLayer* paint_layer = object.EnclosingLayer();
  paint_layer->UpdateAncestorOverflowLayer(
      context.ancestor_overflow_paint_layer);

  if (object.StyleRef().GetPosition() == EPosition::kSticky) {
    paint_layer->GetLayoutObject().UpdateStickyPositionConstraints();

    // Sticky position constraints and ancestor overflow scroller affect the
    // sticky layer position, so we need to update it again here.
    // TODO(flackr): This should be refactored in the future to be clearer (i.e.
    // update layer position and ancestor inputs updates in the same walk).
    paint_layer->UpdateLayerPosition();
  }
  if (paint_layer->IsRootLayer() || object.HasOverflowClip())
    context.ancestor_overflow_paint_layer = paint_layer;
}

LayoutRect PrePaintTreeWalk::ComputeClipRectForContext(
    const PaintPropertyTreeBuilderFragmentContext::ContainingBlockContext&
        context,
    const EffectPaintPropertyNode* effect,
    const PropertyTreeState& ancestor_state,
    const LayoutPoint& ancestor_paint_offset) {
  PropertyTreeState local_state(context.transform, context.clip, effect);

  const auto& clip_rect =
      GeometryMapper::SourceToDestinationClipRect(local_state, ancestor_state);
  // HasRadius() is ignored because it doesn't affect descendants' visual rects.
  LayoutRect result(clip_rect.Rect());
  if (!clip_rect.IsInfinite())
    result.MoveBy(-ancestor_paint_offset);
  return result;
}

void PrePaintTreeWalk::InvalidatePaintLayerOptimizationsIfNeeded(
    const LayoutObject& object,
    PrePaintTreeWalkContext& context) {
  if (!object.HasLayer())
    return;

  PaintLayer& paint_layer = *ToLayoutBoxModelObject(object).Layer();
  if (object.StyleRef().HasTransform() ||
      &object ==
          context.paint_invalidator_context->paint_invalidation_container) {
    context.ancestor_transformed_or_root_paint_layer = &paint_layer;
  }

  // This code below checks whether any clips have changed that might:
  // (a) invalidate optimizations made for a PaintLayer that supports
  //     subsequence caching, or
  // (b) impact clipping of descendant visual rects.
  if (!paint_layer.SupportsSubsequenceCaching() &&
      !paint_layer.GetLayoutObject().HasClipRelatedProperty())
    return;

  FragmentData* fragment_data =
      &object.GetMutableForPainting().EnsureFirstFragment();
  for (auto& fragment : context.tree_builder_context->fragments) {
    DCHECK(fragment_data);
    if (InvalidatePaintLayerOptimizationsForFragment(
            object, context.ancestor_transformed_or_root_paint_layer, fragment,
            *fragment_data)) {
      context.paint_invalidator_context->subtree_flags |=
          PaintInvalidatorContext::kSubtreeVisualRectUpdate;
    }
    fragment_data = fragment_data->NextFragment();
  }
}

bool PrePaintTreeWalk::InvalidatePaintLayerOptimizationsForFragment(
    const LayoutObject& object,
    const PaintLayer* ancestor_transformed_or_root_paint_layer,
    const PaintPropertyTreeBuilderFragmentContext& context,
    FragmentData& fragment_data) {
  PaintLayer& paint_layer = *ToLayoutBoxModelObject(object).Layer();

  const auto& ancestor =
      ancestor_transformed_or_root_paint_layer->GetLayoutObject();
  PropertyTreeState ancestor_state = *ancestor.LocalBorderBoxProperties();

#ifdef CHECK_CLIP_RECTS
  auto respect_overflow_clip = kRespectOverflowClip;
#endif
  if (ancestor_transformed_or_root_paint_layer->GetCompositingState() ==
      kPaintsIntoOwnBacking) {
    const auto* ancestor_properties = ancestor.PaintProperties();
    if (ancestor_properties && ancestor_properties->OverflowClip()) {
      ancestor_state.SetClip(ancestor_properties->OverflowClip());
#ifdef CHECK_CLIP_RECTS
      respect_overflow_clip = kIgnoreOverflowClip;
#endif
    }
  }

#ifdef CHECK_CLIP_RECTS
  const auto& old_clip_rects =
      paint_layer.Clipper(PaintLayer::kDoNotUseGeometryMapper)
          .PaintingClipRects(ancestor_transformed_or_root_paint_layer,
                             respect_overflow_clip, LayoutSize());
#endif

  const LayoutPoint& ancestor_paint_offset =
      ancestor_transformed_or_root_paint_layer->GetLayoutObject().PaintOffset();

  // TODO(chrishtr): generalize this for multicol.
  const auto* effect = context.current_effect;
  auto overflow_clip_rect = ComputeClipRectForContext(
      context.current, effect, ancestor_state, ancestor_paint_offset);
#ifdef CHECK_CLIP_RECTS
  CHECK(overflow_clip_rect == old_clip_rects.OverflowClipRect().Rect())
      << " new=" << overflow_clip_rect.ToString()
      << " old=" << old_clip_rects.OverflowClipRect().Rect().ToString();
#endif

  auto fixed_clip_rect = ComputeClipRectForContext(
      context.fixed_position, effect, ancestor_state, ancestor_paint_offset);
#ifdef CHECK_CLIP_RECTS
  CHECK(fixed_clip_rect == old_clip_rects.FixedClipRect().Rect())
      << " new=" << fixed_clip_rect.ToString()
      << " old=" << old_clip_rects.FixedClipRect().Rect().ToString();
#endif

  auto pos_clip_rect = ComputeClipRectForContext(
      context.absolute_position, effect, ancestor_state, ancestor_paint_offset);
#ifdef CHECK_CLIP_RECTS
  CHECK(pos_clip_rect == old_clip_rects.PosClipRect().Rect())
      << " new=" << pos_clip_rect.ToString()
      << " old=" << old_clip_rects.PosClipRect().Rect().ToString();
#endif

  const auto* previous_clip_rects = fragment_data.PreviousClipRects();
  if (!previous_clip_rects ||
      overflow_clip_rect != previous_clip_rects->OverflowClipRect().Rect() ||
      fixed_clip_rect != previous_clip_rects->FixedClipRect().Rect() ||
      pos_clip_rect != previous_clip_rects->PosClipRect().Rect()) {
    RefPtr<ClipRects> clip_rects = ClipRects::Create();
    clip_rects->SetOverflowClipRect(overflow_clip_rect);
    clip_rects->SetFixedClipRect(fixed_clip_rect);
    clip_rects->SetPosClipRect(pos_clip_rect);
    fragment_data.SetPreviousClipRects(*clip_rects);

    paint_layer.SetNeedsRepaint();
    paint_layer.SetPreviousPaintPhaseDescendantOutlinesEmpty(false);
    paint_layer.SetPreviousPaintPhaseFloatEmpty(false);
    paint_layer.SetPreviousPaintPhaseDescendantBlockBackgroundsEmpty(false);
    // All subsequences which are contained below this paintLayer must also
    // be checked.
    return true;
  }
  return false;
}

bool PrePaintTreeWalk::NeedsTreeBuilderContextUpdate(
    const FrameView& frame_view,
    const PrePaintTreeWalkContext& context) {
  return frame_view.NeedsPaintPropertyUpdate() ||
         (frame_view.GetLayoutView() &&
          NeedsTreeBuilderContextUpdate(*frame_view.GetLayoutView(), context));
}

bool PrePaintTreeWalk::NeedsTreeBuilderContextUpdate(
    const LayoutObject& object,
    const PrePaintTreeWalkContext& parent_context) {
  return object.NeedsPaintPropertyUpdate() ||
         object.DescendantNeedsPaintPropertyUpdate() ||
         (parent_context.tree_builder_context &&
          parent_context.tree_builder_context->force_subtree_update) ||
         // If the object needs visual rect update, we should update tree
         // builder context which is needed by visual rect update.
         parent_context.paint_invalidator_context->NeedsVisualRectUpdate(
             object);
}

void PrePaintTreeWalk::ClearPreviousClipRectsForTesting(
    const LayoutObject& object) {
  object.GetMutableForPainting().FirstFragment()->ClearPreviousClipRects();
}

void PrePaintTreeWalk::Walk(const LayoutObject& object,
                            const PrePaintTreeWalkContext& parent_context) {
  // Early out from the tree walk if possible.
  bool needs_tree_builder_context_update =
      this->NeedsTreeBuilderContextUpdate(object, parent_context);
  if (!needs_tree_builder_context_update &&
      !object.ShouldCheckForPaintInvalidation())
    return;

  PrePaintTreeWalkContext context(parent_context,
                                  needs_tree_builder_context_update);

  // This must happen before updatePropertiesForSelf, because the latter reads
  // some of the state computed here.
  UpdateAuxiliaryObjectProperties(object, context);

  if (context.tree_builder_context) {
    property_tree_builder_.UpdatePropertiesForSelf(
        object, *context.tree_builder_context);
  }

  paint_invalidator_.InvalidatePaint(object, context.tree_builder_context.get(),
                                     *context.paint_invalidator_context);

  if (context.tree_builder_context) {
    property_tree_builder_.UpdatePropertiesForChildren(
        object, *context.tree_builder_context);
    InvalidatePaintLayerOptimizationsIfNeeded(object, context);
  }

  for (const LayoutObject* child = object.SlowFirstChild(); child;
       child = child->NextSibling()) {
    if (child->IsLayoutMultiColumnSpannerPlaceholder()) {
      child->GetMutableForPainting().ClearPaintFlags();
      continue;
    }
    Walk(*child, context);
  }

  if (object.IsLayoutPart()) {
    const LayoutPart& layout_part = ToLayoutPart(object);
    FrameView* frame_view = layout_part.ChildFrameView();
    if (frame_view) {
      if (context.tree_builder_context) {
        context.tree_builder_context->fragments[0].current.paint_offset +=
            layout_part.ReplacedContentRect().Location() -
            frame_view->FrameRect().Location();
        context.tree_builder_context->fragments[0].current.paint_offset =
            RoundedIntPoint(context.tree_builder_context->fragments[0]
                                .current.paint_offset);
      }
      Walk(*frame_view, context);
    }
    // TODO(pdr): Investigate RemoteFrameView (crbug.com/579281).
  }

  object.GetMutableForPainting().ClearPaintFlags();
}

}  // namespace blink
