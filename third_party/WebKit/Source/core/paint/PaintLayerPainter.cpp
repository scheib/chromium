// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/paint/PaintLayerPainter.h"

#include "core/frame/FrameView.h"
#include "core/layout/LayoutView.h"
#include "core/paint/ClipPathClipper.h"
#include "core/paint/FilterPainter.h"
#include "core/paint/LayerClipRecorder.h"
#include "core/paint/LayoutObjectDrawingRecorder.h"
#include "core/paint/ObjectPaintProperties.h"
#include "core/paint/PaintInfo.h"
#include "core/paint/PaintLayer.h"
#include "core/paint/ScrollRecorder.h"
#include "core/paint/ScrollableAreaPainter.h"
#include "core/paint/Transform3DRecorder.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/geometry/FloatPoint3D.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/paint/CompositingRecorder.h"
#include "platform/graphics/paint/DisplayItemCacheSkipper.h"
#include "platform/graphics/paint/PaintChunkProperties.h"
#include "platform/graphics/paint/ScopedPaintChunkProperties.h"
#include "platform/graphics/paint/SubsequenceRecorder.h"
#include "platform/graphics/paint/Transform3DDisplayItem.h"
#include "platform/wtf/Optional.h"

namespace blink {

static inline bool ShouldSuppressPaintingLayer(const PaintLayer& layer) {
  // Avoid painting descendants of the root layer when stylesheets haven't
  // loaded. This avoids some FOUC.  It's ok not to draw, because later on, when
  // all the stylesheets do load, Document::styleResolverMayHaveChanged() will
  // invalidate all painted output via a call to
  // LayoutView::invalidatePaintForViewAndCompositedLayers().  We also avoid
  // caching subsequences in this mode; see shouldCreateSubsequence().
  if (layer.GetLayoutObject().GetDocument().DidLayoutWithPendingStylesheets() &&
      !layer.IsRootLayer() && !layer.GetLayoutObject().IsDocumentElement())
    return true;

  return false;
}

void PaintLayerPainter::Paint(GraphicsContext& context,
                              const LayoutRect& damage_rect,
                              const GlobalPaintFlags global_paint_flags,
                              PaintLayerFlags paint_flags) {
  PaintLayerPaintingInfo painting_info(
      &paint_layer_, LayoutRect(EnclosingIntRect(damage_rect)),
      global_paint_flags, LayoutSize());
  if (ShouldPaintLayerInSoftwareMode(global_paint_flags, paint_flags))
    Paint(context, painting_info, paint_flags);
}

static ShouldRespectOverflowClipType ShouldRespectOverflowClip(
    PaintLayerFlags paint_flags,
    const LayoutObject& layout_object) {
  return (paint_flags & kPaintLayerPaintingOverflowContents ||
          (paint_flags & kPaintLayerPaintingChildClippingMaskPhase &&
           layout_object.HasClipPath()))
             ? kIgnoreOverflowClip
             : kRespectOverflowClip;
}

bool PaintLayerPainter::PaintedOutputInvisible(
    const PaintLayerPaintingInfo& painting_info) {
  const LayoutObject& layout_object = paint_layer_.GetLayoutObject();
  if (layout_object.HasBackdropFilter())
    return false;

  // Always paint when 'will-change: opacity' is present. Reduces jank for
  // common animation implementation approaches, for example, an element that
  // starts with opacity zero and later begins to animate.
  if (layout_object.StyleRef().HasWillChangeOpacityHint())
    return false;

  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    if (layout_object.StyleRef().Opacity())
      return false;

    const EffectPaintPropertyNode* effect =
        layout_object.PaintProperties()->Effect();
    if (effect && effect->RequiresCompositingForAnimation()) {
      return false;
    }
  }

  // 0.0004f < 1/2048. With 10-bit color channels (only available on the
  // newest Macs; otherwise it's 8-bit), we see that an alpha of 1/2048 or
  // less leads to a color output of less than 0.5 in all channels, hence
  // not visible.
  static const float kMinimumVisibleOpacity = 0.0004f;
  if (paint_layer_.PaintsWithTransparency(
          painting_info.GetGlobalPaintFlags())) {
    if (layout_object.StyleRef().Opacity() < kMinimumVisibleOpacity) {
      return true;
    }
  }
  return false;
}

PaintResult PaintLayerPainter::Paint(
    GraphicsContext& context,
    const PaintLayerPaintingInfo& painting_info,
    PaintLayerFlags paint_flags) {
  if (paint_layer_.GetLayoutObject().GetFrameView()->ShouldThrottleRendering())
    return kFullyPainted;

  // https://code.google.com/p/chromium/issues/detail?id=343772
  DisableCompositingQueryAsserts disabler;

  if (paint_layer_.GetCompositingState() != kNotComposited) {
    if (painting_info.GetGlobalPaintFlags() &
        kGlobalPaintFlattenCompositingLayers) {
      // FIXME: ok, but what about GlobalPaintFlattenCompositingLayers? That's
      // for printing and drag-image.
      // FIXME: why isn't the code here global, as opposed to being set on each
      // paint() call?
      paint_flags |= kPaintLayerUncachedClipRects;
    }
  }

  // Non self-painting layers without self-painting descendants don't need to be
  // painted as their layoutObject() should properly paint itself.
  if (!paint_layer_.IsSelfPaintingLayer() &&
      !paint_layer_.HasSelfPaintingLayerDescendant())
    return kFullyPainted;

  if (ShouldSuppressPaintingLayer(paint_layer_))
    return kFullyPainted;

  // If this layer is totally invisible then there is nothing to paint. In SPv2
  // we simplify this optimization by painting even when effectively invisible
  // but skipping the painted content during layerization in
  // PaintArtifactCompositor.
  if (!RuntimeEnabledFeatures::slimmingPaintV2Enabled() &&
      PaintedOutputInvisible(painting_info)) {
    return kFullyPainted;
  }

  if (paint_layer_.PaintsWithTransparency(painting_info.GetGlobalPaintFlags()))
    paint_flags |= kPaintLayerHaveTransparency;

  if (paint_layer_.PaintsWithTransform(painting_info.GetGlobalPaintFlags()) &&
      !(paint_flags & kPaintLayerAppliedTransform))
    return PaintLayerWithTransform(context, painting_info, paint_flags);

  return PaintLayerContentsCompositingAllPhases(context, painting_info,
                                                paint_flags);
}

PaintResult PaintLayerPainter::PaintLayerContentsCompositingAllPhases(
    GraphicsContext& context,
    const PaintLayerPaintingInfo& painting_info,
    PaintLayerFlags paint_flags,
    FragmentPolicy fragment_policy) {
  DCHECK(paint_layer_.IsSelfPaintingLayer() ||
         paint_layer_.HasSelfPaintingLayerDescendant());

  PaintLayerFlags local_paint_flags =
      paint_flags & ~(kPaintLayerAppliedTransform);
  local_paint_flags |= kPaintLayerPaintingCompositingAllPhases;
  return PaintLayerContents(context, painting_info, local_paint_flags,
                            fragment_policy);
}

static bool ShouldCreateSubsequence(const PaintLayer& paint_layer,
                                    const GraphicsContext& context,
                                    const PaintLayerPaintingInfo& painting_info,
                                    PaintLayerFlags paint_flags) {
  // Caching is not needed during printing.
  if (context.Printing())
    return false;

  if (!paint_layer.SupportsSubsequenceCaching())
    return false;

  // Don't create subsequence for a composited layer because if it can be
  // cached, we can skip the whole painting in GraphicsLayer::paint() with
  // CachedDisplayItemList.  This also avoids conflict of
  // PaintLayer::previousXXX() when paintLayer is composited scrolling and is
  // painted twice for GraphicsLayers of container and scrolling contents.
  if (paint_layer.GetCompositingState() == kPaintsIntoOwnBacking)
    return false;

  // Don't create subsequence during special painting to avoid cache conflict
  // with normal painting.
  if (painting_info.GetGlobalPaintFlags() &
      kGlobalPaintFlattenCompositingLayers)
    return false;
  if (paint_flags &
      (kPaintLayerPaintingRootBackgroundOnly |
       kPaintLayerPaintingOverlayScrollbars | kPaintLayerUncachedClipRects))
    return false;

  // When in FOUC-avoidance mode, don't cache any subsequences, to avoid having
  // to invalidate all of them when leaving this mode. There is an early-out in
  // BlockPainter::paintContents that may result in nothing getting painted in
  // this mode, in addition to early-out logic in PaintLayerPainter.
  if (paint_layer.GetLayoutObject()
          .GetDocument()
          .DidLayoutWithPendingStylesheets())
    return false;

  return true;
}

static bool ShouldRepaintSubsequence(
    PaintLayer& paint_layer,
    const PaintLayerPaintingInfo& painting_info,
    ShouldRespectOverflowClipType respect_overflow_clip,
    const LayoutSize& subpixel_accumulation,
    bool& should_clear_empty_paint_phase_flags) {
  bool needs_repaint = false;

  // We should set shouldResetEmptyPaintPhaseFlags if some previously unpainted
  // objects may begin to be painted, causing a previously empty paint phase to
  // become non-empty.

  // Repaint subsequence if the layer is marked for needing repaint.
  // We don't set needsResetEmptyPaintPhase here, but clear the empty paint
  // phase flags in PaintLayer::setNeedsPaintPhaseXXX(), to ensure that we won't
  // clear previousPaintPhaseXXXEmpty flags when unrelated things changed which
  // won't cause the paint phases to become non-empty.
  if (paint_layer.NeedsRepaint())
    needs_repaint = true;

  // Repaint if layer's clip changes.
  if (!RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled()) {
    ClipRects& clip_rects =
        paint_layer.Clipper(PaintLayer::kDoNotUseGeometryMapper)
            .PaintingClipRects(painting_info.root_layer, respect_overflow_clip,
                               subpixel_accumulation);
    ClipRects* previous_clip_rects = paint_layer.PreviousClipRects();
    if (&clip_rects != previous_clip_rects &&
        (!previous_clip_rects || clip_rects != *previous_clip_rects)) {
      needs_repaint = true;
      should_clear_empty_paint_phase_flags = true;
    }
    paint_layer.SetPreviousClipRects(clip_rects);
  }

  // Repaint if previously the layer might be clipped by paintDirtyRect and
  // paintDirtyRect changes.
  if (paint_layer.PreviousPaintResult() == kMayBeClippedByPaintDirtyRect &&
      paint_layer.PreviousPaintDirtyRect() != painting_info.paint_dirty_rect) {
    needs_repaint = true;
    should_clear_empty_paint_phase_flags = true;
  }
  paint_layer.SetPreviousPaintDirtyRect(painting_info.paint_dirty_rect);

  // Repaint if scroll offset accumulation changes.
  if (painting_info.scroll_offset_accumulation !=
      paint_layer.PreviousScrollOffsetAccumulationForPainting()) {
    needs_repaint = true;
    should_clear_empty_paint_phase_flags = true;
  }
  paint_layer.SetPreviousScrollOffsetAccumulationForPainting(
      painting_info.scroll_offset_accumulation);

  return needs_repaint;
}

PaintResult PaintLayerPainter::PaintLayerContents(
    GraphicsContext& context,
    const PaintLayerPaintingInfo& painting_info_arg,
    PaintLayerFlags paint_flags,
    FragmentPolicy fragment_policy) {
  PaintResult result = kFullyPainted;

  if (paint_layer_.GetLayoutObject().GetFrameView()->ShouldThrottleRendering())
    return result;

  Optional<ScopedPaintChunkProperties> scoped_paint_chunk_properties;
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled() &&
      RuntimeEnabledFeatures::rootLayerScrollingEnabled() &&
      paint_layer_.GetLayoutObject().IsLayoutView()) {
    const auto* local_border_box_properties =
        paint_layer_.GetLayoutObject().LocalBorderBoxProperties();
    DCHECK(local_border_box_properties);
    PaintChunkProperties properties(
        context.GetPaintController().CurrentPaintChunkProperties());
    properties.property_tree_state = *local_border_box_properties;
    properties.backface_hidden =
        paint_layer_.GetLayoutObject().HasHiddenBackface();
    scoped_paint_chunk_properties.emplace(context.GetPaintController(),
                                          paint_layer_, properties);
  }

  DCHECK(paint_layer_.IsSelfPaintingLayer() ||
         paint_layer_.HasSelfPaintingLayerDescendant());
  DCHECK(!(paint_flags & kPaintLayerAppliedTransform));

  bool is_self_painting_layer = paint_layer_.IsSelfPaintingLayer();
  bool is_painting_overlay_scrollbars =
      paint_flags & kPaintLayerPaintingOverlayScrollbars;
  bool is_painting_scrolling_content =
      paint_flags & kPaintLayerPaintingCompositingScrollingPhase;
  bool is_painting_composited_foreground =
      paint_flags & kPaintLayerPaintingCompositingForegroundPhase;
  bool is_painting_composited_background =
      paint_flags & kPaintLayerPaintingCompositingBackgroundPhase;
  bool is_painting_composited_decoration =
      paint_flags & kPaintLayerPaintingCompositingDecorationPhase;
  bool is_painting_overflow_contents =
      paint_flags & kPaintLayerPaintingOverflowContents;
  // Outline always needs to be painted even if we have no visible content.
  // It is painted as part of the decoration phase which paints content that
  // is not scrolled and should be above scrolled content.
  bool should_paint_self_outline =
      is_self_painting_layer && !is_painting_overlay_scrollbars &&
      (is_painting_composited_decoration || !is_painting_scrolling_content) &&
      paint_layer_.GetLayoutObject().StyleRef().HasOutline();

  if (paint_flags & kPaintLayerPaintingRootBackgroundOnly &&
      !paint_layer_.GetLayoutObject().IsLayoutView())
    return result;

  // Ensure our lists are up to date.
  paint_layer_.StackingNode()->UpdateLayerListsIfNeeded();

  LayoutSize subpixel_accumulation =
      paint_layer_.GetCompositingState() == kPaintsIntoOwnBacking
          ? paint_layer_.SubpixelAccumulation()
          : painting_info_arg.sub_pixel_accumulation;
  ShouldRespectOverflowClipType respect_overflow_clip =
      ShouldRespectOverflowClip(paint_flags, paint_layer_.GetLayoutObject());

  Optional<SubsequenceRecorder> subsequence_recorder;
  bool should_clear_empty_paint_phase_flags = false;
  if (ShouldCreateSubsequence(paint_layer_, context, painting_info_arg,
                              paint_flags)) {
    if (!ShouldRepaintSubsequence(paint_layer_, painting_info_arg,
                                  respect_overflow_clip, subpixel_accumulation,
                                  should_clear_empty_paint_phase_flags) &&
        SubsequenceRecorder::UseCachedSubsequenceIfPossible(context,
                                                            paint_layer_))
      return result;
    DCHECK(paint_layer_.SupportsSubsequenceCaching());
    subsequence_recorder.emplace(context, paint_layer_);
  } else {
    should_clear_empty_paint_phase_flags = true;
  }

  if (should_clear_empty_paint_phase_flags) {
    paint_layer_.SetPreviousPaintPhaseDescendantOutlinesEmpty(false);
    paint_layer_.SetPreviousPaintPhaseFloatEmpty(false);
    paint_layer_.SetPreviousPaintPhaseDescendantBlockBackgroundsEmpty(false);
  }

  PaintLayerPaintingInfo painting_info = painting_info_arg;

  LayoutPoint offset_from_root;
  paint_layer_.ConvertToLayerCoords(painting_info.root_layer, offset_from_root);
  offset_from_root.Move(subpixel_accumulation);

  LayoutRect bounds = paint_layer_.PhysicalBoundingBox(offset_from_root);
  if (!painting_info.paint_dirty_rect.Contains(bounds))
    result = kMayBeClippedByPaintDirtyRect;

  // These helpers output clip and compositing operations using a RAII pattern.
  // Stack-allocated-varibles are destructed in the reverse order of
  // construction, so they are nested properly.
  Optional<ClipPathClipper> clip_path_clipper;
  // Clip-path, like border radius, must not be applied to the contents of a
  // composited-scrolling container.  It must, however, still be applied to the
  // mask layer, so that the compositor can properly mask the
  // scrolling contents and scrollbars.
  if (paint_layer_.GetLayoutObject().HasClipPath() &&
      (!paint_layer_.NeedsCompositedScrolling() ||
       (paint_flags & (kPaintLayerPaintingChildClippingMaskPhase |
                       kPaintLayerPaintingAncestorClippingMaskPhase)))) {
    painting_info.ancestor_has_clip_path_clipping = true;

    LayoutRect reference_box(paint_layer_.BoxForClipPath());
    // Note that this isn't going to work correctly if crossing a column
    // boundary. The reference box should be determined per-fragment, and hence
    // this ought to be performed after fragmentation.
    if (paint_layer_.EnclosingPaginationLayer())
      paint_layer_.ConvertFromFlowThreadToVisualBoundingBoxInAncestor(
          painting_info.root_layer, reference_box);
    else
      reference_box.MoveBy(offset_from_root);
    clip_path_clipper.emplace(
        context, *paint_layer_.GetLayoutObject().StyleRef().ClipPath(),
        paint_layer_.GetLayoutObject(), FloatRect(reference_box),
        FloatPoint(reference_box.Location()));
  }

  Optional<CompositingRecorder> compositing_recorder;
  // FIXME: this should be unified further into
  // PaintLayer::paintsWithTransparency().
  bool should_composite_for_blend_mode =
      paint_layer_.StackingNode()->IsStackingContext() &&
      paint_layer_.HasNonIsolatedDescendantWithBlendMode();
  if (should_composite_for_blend_mode ||
      paint_layer_.PaintsWithTransparency(
          painting_info.GetGlobalPaintFlags())) {
    FloatRect compositing_bounds = FloatRect(paint_layer_.PaintingExtent(
        painting_info.root_layer, painting_info.sub_pixel_accumulation,
        painting_info.GetGlobalPaintFlags()));
    compositing_recorder.emplace(
        context, paint_layer_.GetLayoutObject(),
        WebCoreCompositeToSkiaComposite(
            kCompositeSourceOver,
            paint_layer_.GetLayoutObject().Style()->BlendMode()),
        paint_layer_.GetLayoutObject().Opacity(), &compositing_bounds);
  }

  PaintLayerPaintingInfo local_painting_info(painting_info);
  local_painting_info.sub_pixel_accumulation = subpixel_accumulation;

  bool should_paint_content = paint_layer_.HasVisibleContent() &&
                              is_self_painting_layer &&
                              !is_painting_overlay_scrollbars;

  PaintLayerFragments layer_fragments;
  if (should_paint_content || should_paint_self_outline ||
      is_painting_overlay_scrollbars) {
    // Collect the fragments. This will compute the clip rectangles and paint
    // offsets for each layer fragment.
    ClipRectsCacheSlot cache_slot = (paint_flags & kPaintLayerUncachedClipRects)
                                        ? kUncachedClipRects
                                        : kPaintingClipRects;
    LayoutPoint offset_to_clipper;
    PaintLayer* paint_layer_for_fragments = &paint_layer_;
    if (paint_flags & kPaintLayerPaintingAncestorClippingMaskPhase) {
      // Compute fragments and their clips with respect to the clipping
      // container. The paint rect is in this layer's space, so convert it
      // to the clipper's layer's space. The rootLayer is also changed to
      // the clipper's layer to simplify coordinate system adjustments.
      // The change to rootLayer must persist to correctly record the clips.
      paint_layer_for_fragments =
          paint_layer_.ClippingContainer()->EnclosingLayer();
      local_painting_info.root_layer = paint_layer_for_fragments;
      paint_layer_.ConvertToLayerCoords(local_painting_info.root_layer,
                                        offset_to_clipper);
      local_painting_info.paint_dirty_rect.MoveBy(offset_to_clipper);
    }

    PaintLayer::GeometryMapperOption geometry_mapper_option =
        PaintLayer::kDoNotUseGeometryMapper;
    if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled())
      geometry_mapper_option = PaintLayer::kUseGeometryMapper;

    // TODO(trchen): We haven't decided how to handle visual fragmentation with
    // SPv2.  Related thread
    // https://groups.google.com/a/chromium.org/forum/#!topic/graphics-dev/81XuWFf-mxM
    if (fragment_policy == kForceSingleFragment ||
        RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
      paint_layer_for_fragments->AppendSingleFragmentIgnoringPagination(
          layer_fragments, local_painting_info.root_layer,
          local_painting_info.paint_dirty_rect, cache_slot,
          geometry_mapper_option, kIgnorePlatformOverlayScrollbarSize,
          respect_overflow_clip, &offset_from_root,
          local_painting_info.sub_pixel_accumulation);
    } else if (IsFixedPositionObjectInPagedMedia()) {
      PaintLayerFragments single_fragment;
      paint_layer_for_fragments->AppendSingleFragmentIgnoringPagination(
          single_fragment, local_painting_info.root_layer,
          local_painting_info.paint_dirty_rect, cache_slot,
          geometry_mapper_option, kIgnorePlatformOverlayScrollbarSize,
          respect_overflow_clip, &offset_from_root,
          local_painting_info.sub_pixel_accumulation);
      RepeatFixedPositionObjectInPages(single_fragment[0], painting_info,
                                       layer_fragments);
    } else {
      paint_layer_for_fragments->CollectFragments(
          layer_fragments, local_painting_info.root_layer,
          local_painting_info.paint_dirty_rect, cache_slot,
          geometry_mapper_option, kIgnorePlatformOverlayScrollbarSize,
          respect_overflow_clip, &offset_from_root,
          local_painting_info.sub_pixel_accumulation);
      // PaintLayer::collectFragments depends on the paint dirty rect in
      // complicated ways. For now, always assume a partially painted output
      // for fragmented content.
      if (layer_fragments.size() > 1)
        result = kMayBeClippedByPaintDirtyRect;
    }

    if (paint_flags & kPaintLayerPaintingAncestorClippingMaskPhase) {
      // Fragment offsets have been computed in the clipping container's
      // layer's coordinate system, but for the rest of painting we need
      // them in the layer coordinate. So move them and the foreground rect
      // that is also in the clipper's space.
      LayoutSize negative_offset(-offset_to_clipper.X(),
                                 -offset_to_clipper.Y());
      for (auto& fragment : layer_fragments) {
        fragment.foreground_rect.Move(negative_offset);
        fragment.pagination_offset.Move(negative_offset);
      }
    }

    if (should_paint_content) {
      // TODO(wangxianzhu): This is for old slow scrolling. Implement similar
      // optimization for slimming paint v2.
      should_paint_content = AtLeastOneFragmentIntersectsDamageRect(
          layer_fragments, local_painting_info, paint_flags, offset_from_root);
      if (!should_paint_content)
        result = kMayBeClippedByPaintDirtyRect;
    }
  }

  Optional<ScopedPaintChunkProperties> content_scoped_paint_chunk_properties;
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled() &&
      !scoped_paint_chunk_properties.has_value()) {
    // If layoutObject() is a LayoutView and root layer scrolling is enabled,
    // the LayoutView's paint properties will already have been applied at
    // the top of this method, in scopedPaintChunkProperties.
    DCHECK(!(RuntimeEnabledFeatures::rootLayerScrollingEnabled() &&
             paint_layer_.GetLayoutObject().IsLayoutView()));
    const auto* local_border_box_properties =
        paint_layer_.GetLayoutObject().LocalBorderBoxProperties();
    DCHECK(local_border_box_properties);
    PaintChunkProperties properties(
        context.GetPaintController().CurrentPaintChunkProperties());
    properties.property_tree_state = *local_border_box_properties;
    properties.backface_hidden =
        paint_layer_.GetLayoutObject().HasHiddenBackface();
    content_scoped_paint_chunk_properties.emplace(context.GetPaintController(),
                                                  paint_layer_, properties);
  }

  bool selection_only =
      local_painting_info.GetGlobalPaintFlags() & kGlobalPaintSelectionOnly;
  {  // Begin block for the lifetime of any filter.
    FilterPainter filter_painter(paint_layer_, context, offset_from_root,
                                 layer_fragments.IsEmpty()
                                     ? ClipRect()
                                     : layer_fragments[0].background_rect,
                                 local_painting_info, paint_flags);

    bool is_painting_root_layer = (&paint_layer_) == painting_info.root_layer;
    bool should_paint_background =
        should_paint_content && !selection_only &&
        (is_painting_composited_background ||
         (is_painting_root_layer &&
          !(paint_flags & kPaintLayerPaintingSkipRootBackground)));
    bool should_paint_neg_z_order_list =
        (is_painting_scrolling_content && is_painting_overflow_contents) ||
        (!is_painting_scrolling_content && is_painting_composited_background);
    bool should_paint_own_contents =
        is_painting_composited_foreground && should_paint_content;
    bool should_paint_normal_flow_and_pos_z_order_lists =
        is_painting_composited_foreground;
    bool should_paint_overlay_scrollbars = is_painting_overlay_scrollbars;

    if (should_paint_background) {
      PaintBackgroundForFragments(layer_fragments, context,
                                  painting_info.paint_dirty_rect,
                                  local_painting_info, paint_flags);
    }

    if (should_paint_neg_z_order_list) {
      if (PaintChildren(kNegativeZOrderChildren, context, painting_info,
                        paint_flags) == kMayBeClippedByPaintDirtyRect)
        result = kMayBeClippedByPaintDirtyRect;
    }

    if (should_paint_own_contents) {
      PaintForegroundForFragments(
          layer_fragments, context, painting_info.paint_dirty_rect,
          local_painting_info, selection_only, paint_flags);
    }

    if (should_paint_self_outline)
      PaintSelfOutlineForFragments(layer_fragments, context,
                                   local_painting_info, paint_flags);

    if (should_paint_normal_flow_and_pos_z_order_lists) {
      if (PaintChildren(kNormalFlowChildren | kPositiveZOrderChildren, context,
                        painting_info,
                        paint_flags) == kMayBeClippedByPaintDirtyRect)
        result = kMayBeClippedByPaintDirtyRect;
    }

    if (should_paint_overlay_scrollbars)
      PaintOverflowControlsForFragments(layer_fragments, context,
                                        local_painting_info, paint_flags);
  }  // FilterPainter block

  bool should_paint_mask =
      (paint_flags & kPaintLayerPaintingCompositingMaskPhase) &&
      should_paint_content && paint_layer_.GetLayoutObject().HasMask() &&
      !selection_only;
  bool should_paint_clipping_mask =
      (paint_flags & (kPaintLayerPaintingChildClippingMaskPhase |
                      kPaintLayerPaintingAncestorClippingMaskPhase)) &&
      should_paint_content && !selection_only;

  if (should_paint_mask) {
    PaintMaskForFragments(layer_fragments, context, local_painting_info,
                          paint_flags);
  }

  if (should_paint_clipping_mask) {
    // Paint the border radius mask for the fragments.
    PaintChildClippingMaskForFragments(layer_fragments, context,
                                       local_painting_info, paint_flags);
  }

  if (subsequence_recorder)
    paint_layer_.SetPreviousPaintResult(result);
  return result;
}

bool PaintLayerPainter::NeedsToClip(
    const PaintLayerPaintingInfo& local_painting_info,
    const ClipRect& clip_rect) {
  // Clipping will be applied by property nodes directly for SPv2.
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled())
    return false;

  return clip_rect.Rect() != local_painting_info.paint_dirty_rect ||
         clip_rect.HasRadius();
}

bool PaintLayerPainter::AtLeastOneFragmentIntersectsDamageRect(
    PaintLayerFragments& fragments,
    const PaintLayerPaintingInfo& local_painting_info,
    PaintLayerFlags local_paint_flags,
    const LayoutPoint& offset_from_root) {
  if (paint_layer_.EnclosingPaginationLayer())
    return true;  // The fragments created have already been found to intersect
                  // with the damage rect.

  if (&paint_layer_ == local_painting_info.root_layer &&
      (local_paint_flags & kPaintLayerPaintingOverflowContents))
    return true;

  for (PaintLayerFragment& fragment : fragments) {
    LayoutPoint new_offset_from_root =
        offset_from_root + fragment.pagination_offset;
    // Note that this really only works reliably on the first fragment. If the
    // layer has visible overflow and a subsequent fragment doesn't intersect
    // with the border box of the layer (i.e. only contains an overflow portion
    // of the layer), intersection will fail. The reason for this is that
    // fragment.layerBounds is set to the border box, not the bounding box, of
    // the layer.
    if (paint_layer_.IntersectsDamageRect(fragment.layer_bounds,
                                          fragment.background_rect.Rect(),
                                          new_offset_from_root))
      return true;
  }
  return false;
}

inline bool PaintLayerPainter::IsFixedPositionObjectInPagedMedia() {
  LayoutObject& object = paint_layer_.GetLayoutObject();
  LayoutView* view = object.View();
  return object.StyleRef().GetPosition() == EPosition::kFixed &&
         object.Container() == view && view->PageLogicalHeight() &&
         // TODO(crbug.com/619094): Figure out the correct behaviour for fixed
         // position objects in paged media with vertical writing modes.
         view->IsHorizontalWritingMode();
}

void PaintLayerPainter::RepeatFixedPositionObjectInPages(
    const PaintLayerFragment& single_fragment_ignored_pagination,
    const PaintLayerPaintingInfo& painting_info,
    PaintLayerFragments& layer_fragments) {
  DCHECK(IsFixedPositionObjectInPagedMedia());

  LayoutView* view = paint_layer_.GetLayoutObject().View();
  unsigned pages =
      ceilf(view->DocumentRect().Height() / view->PageLogicalHeight());

  // The fixed position object is offset from the top of the page, so remove
  // any scroll offset.
  LayoutPoint offset_from_root;
  paint_layer_.ConvertToLayerCoords(painting_info.root_layer, offset_from_root);
  LayoutSize offset_adjustment = paint_layer_.Location() - offset_from_root;
  layer_fragments.push_back(single_fragment_ignored_pagination);
  layer_fragments[0].pagination_offset += offset_adjustment;
  layer_fragments[0].layer_bounds.Move(offset_adjustment);

  LayoutPoint page_offset(LayoutUnit(), view->PageLogicalHeight());
  for (unsigned i = 1; i < pages; i++) {
    PaintLayerFragment fragment = layer_fragments[i - 1];
    fragment.pagination_offset += page_offset;
    fragment.layer_bounds.MoveBy(page_offset);
    layer_fragments.push_back(fragment);
  }
}

PaintResult PaintLayerPainter::PaintLayerWithTransform(
    GraphicsContext& context,
    const PaintLayerPaintingInfo& painting_info,
    PaintLayerFlags paint_flags) {
  TransformationMatrix layer_transform =
      paint_layer_.RenderableTransform(painting_info.GetGlobalPaintFlags());
  // If the transform can't be inverted, then don't paint anything.
  if (!layer_transform.IsInvertible())
    return kFullyPainted;

  // FIXME: We should make sure that we don't walk past paintingInfo.rootLayer
  // here.  m_paintLayer may be the "root", and then we should avoid looking at
  // its parent.
  PaintLayer* parent_layer = paint_layer_.Parent();

  PaintLayer::GeometryMapperOption geometry_mapper_option =
      PaintLayer::kDoNotUseGeometryMapper;
  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled())
    geometry_mapper_option = PaintLayer::kUseGeometryMapper;

  PaintResult result = kFullyPainted;
  PaintLayer* pagination_layer = paint_layer_.EnclosingPaginationLayer();
  PaintLayerFragments layer_fragments;
  bool is_fixed_position_object_in_paged_media =
      this->IsFixedPositionObjectInPagedMedia();
  if (!pagination_layer || is_fixed_position_object_in_paged_media) {
    // We don't need to collect any fragments in the regular way here. We have
    // already calculated a clip rectangle for the ancestry if it was needed,
    // and clipping this layer is something that can be done further down the
    // path, when the transform has been applied.
    PaintLayerFragment fragment;
    fragment.background_rect = painting_info.paint_dirty_rect;
    if (is_fixed_position_object_in_paged_media)
      RepeatFixedPositionObjectInPages(fragment, painting_info,
                                       layer_fragments);
    else
      layer_fragments.push_back(fragment);
  } else {
    // FIXME: This is a mess. Look closely at this code and the code in Layer
    // and fix any issues in it & refactor to make it obvious from code
    // structure what it does and that it's correct.
    ClipRectsCacheSlot cache_slot = (paint_flags & kPaintLayerUncachedClipRects)
                                        ? kUncachedClipRects
                                        : kPaintingClipRects;
    ShouldRespectOverflowClipType respect_overflow_clip =
        ShouldRespectOverflowClip(paint_flags, paint_layer_.GetLayoutObject());
    // Calculate the transformed bounding box in the current coordinate space,
    // to figure out which fragmentainers (e.g. columns) we need to visit.
    LayoutRect transformed_extent = PaintLayer::TransparencyClipBox(
        &paint_layer_, pagination_layer,
        PaintLayer::kPaintingTransparencyClipBox,
        PaintLayer::kRootOfTransparencyClipBox,
        painting_info.sub_pixel_accumulation,
        painting_info.GetGlobalPaintFlags());

    // FIXME: we don't check if paginationLayer is within
    // paintingInfo.rootLayer
    // here.
    pagination_layer->CollectFragments(
        layer_fragments, painting_info.root_layer,
        painting_info.paint_dirty_rect, cache_slot, geometry_mapper_option,
        kIgnorePlatformOverlayScrollbarSize, respect_overflow_clip, nullptr,
        painting_info.sub_pixel_accumulation, &transformed_extent);
    // PaintLayer::collectFragments depends on the paint dirty rect in
    // complicated ways. For now, always assume a partially painted output
    // for fragmented content.
    if (layer_fragments.size() > 1)
      result = kMayBeClippedByPaintDirtyRect;
  }

  Optional<DisplayItemCacheSkipper> cache_skipper;
  if (layer_fragments.size() > 1)
    cache_skipper.emplace(context);

  ClipRect ancestor_background_clip_rect;
  if (!RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    if (painting_info.root_layer == &paint_layer_) {
      // This works around a bug in squashed-layer painting.
      // Squashed layers paint into a backing in its compositing container's
      // space, but painting_info.root_layer points to the squashed layer
      // itself, thus PaintLayerClipper would return a clip rect in the
      // squashed layer's local space, instead of the backing's space.
      // Fortunately, CompositedLayerMapping::DoPaintTask already applied
      // appropriate ancestor clip for us, we can simply skip it.
      DCHECK_EQ(paint_layer_.GetCompositingState(), kPaintsIntoGroupedBacking);
      ancestor_background_clip_rect.SetRect(FloatClipRect());
    } else if (parent_layer) {
      // Calculate the clip rectangle that the ancestors establish.
      ClipRectsContext clip_rects_context(
          painting_info.root_layer,
          (paint_flags & kPaintLayerUncachedClipRects) ? kUncachedClipRects
                                                       : kPaintingClipRects,
          kIgnorePlatformOverlayScrollbarSize);
      if (ShouldRespectOverflowClip(paint_flags,
                                    paint_layer_.GetLayoutObject()) ==
          kIgnoreOverflowClip)
        clip_rects_context.SetIgnoreOverflowClip();
      paint_layer_.Clipper(geometry_mapper_option)
          .CalculateBackgroundClipRect(clip_rects_context,
                                       ancestor_background_clip_rect);
    }
  }

  for (const auto& fragment : layer_fragments) {
    Optional<LayerClipRecorder> clip_recorder;
    if (parent_layer && !RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
      ClipRect clip_rect_for_fragment(ancestor_background_clip_rect);
      // A fixed-position object is repeated on every page instead of paginated,
      // so we should apply the original ancestor clip rect.
      if (!is_fixed_position_object_in_paged_media)
        clip_rect_for_fragment.MoveBy(fragment.pagination_offset);
      clip_rect_for_fragment.Intersect(fragment.background_rect);
      if (clip_rect_for_fragment.IsEmpty())
        continue;
      if (NeedsToClip(painting_info, clip_rect_for_fragment)) {
        clip_recorder.emplace(context, parent_layer->GetLayoutObject(),
                              DisplayItem::kClipLayerParent,
                              clip_rect_for_fragment, painting_info.root_layer,
                              fragment.pagination_offset, paint_flags);
      }
    }
    if (PaintFragmentByApplyingTransform(context, painting_info, paint_flags,
                                         fragment.pagination_offset) ==
        kMayBeClippedByPaintDirtyRect)
      result = kMayBeClippedByPaintDirtyRect;
  }
  return result;
}

PaintResult PaintLayerPainter::PaintFragmentByApplyingTransform(
    GraphicsContext& context,
    const PaintLayerPaintingInfo& painting_info,
    PaintLayerFlags paint_flags,
    const LayoutPoint& fragment_translation) {
  // This involves subtracting out the position of the layer in our current
  // coordinate space, but preserving the accumulated error for sub-pixel
  // layout.
  LayoutPoint delta;
  paint_layer_.ConvertToLayerCoords(painting_info.root_layer, delta);
  delta.MoveBy(fragment_translation);
  delta += painting_info.sub_pixel_accumulation;
  IntPoint rounded_delta = RoundedIntPoint(delta);

  TransformationMatrix transform(
      paint_layer_.RenderableTransform(painting_info.GetGlobalPaintFlags()));
  transform.PostTranslate(rounded_delta.X(), rounded_delta.Y());

  LayoutSize new_sub_pixel_accumulation;
  if (transform.IsIdentityOrTranslation())
    new_sub_pixel_accumulation += delta - rounded_delta;
  // Otherwise discard the sub-pixel remainder because paint offset can't be
  // transformed by a non-translation transform.

  // TODO(jbroman): Put the real transform origin here, instead of using a
  // matrix with the origin baked in.
  FloatPoint3D transform_origin;
  Transform3DRecorder transform3d_recorder(
      context, paint_layer_.GetLayoutObject(),
      DisplayItem::kTransform3DElementTransform, transform, transform_origin);

  // Now do a paint with the root layer shifted to be us.
  PaintLayerPaintingInfo transformed_painting_info(
      &paint_layer_,
      LayoutRect(EnclosingIntRect(
          transform.Inverse().MapRect(painting_info.paint_dirty_rect))),
      painting_info.GetGlobalPaintFlags(), new_sub_pixel_accumulation);
  transformed_painting_info.ancestor_has_clip_path_clipping =
      painting_info.ancestor_has_clip_path_clipping;

  // Remove skip root background flag when we're painting with a new root.
  if (&paint_layer_ != painting_info.root_layer)
    paint_flags &= ~kPaintLayerPaintingSkipRootBackground;

  return PaintLayerContentsCompositingAllPhases(
      context, transformed_painting_info, paint_flags, kForceSingleFragment);
}

PaintResult PaintLayerPainter::PaintChildren(
    unsigned children_to_visit,
    GraphicsContext& context,
    const PaintLayerPaintingInfo& painting_info,
    PaintLayerFlags paint_flags) {
  PaintResult result = kFullyPainted;
  if (!paint_layer_.HasSelfPaintingLayerDescendant())
    return result;

#if DCHECK_IS_ON()
  LayerListMutationDetector mutation_checker(paint_layer_.StackingNode());
#endif

  PaintLayerStackingNodeIterator iterator(*paint_layer_.StackingNode(),
                                          children_to_visit);
  PaintLayerStackingNode* child = iterator.Next();
  if (!child)
    return result;

  IntSize scroll_offset_accumulation_for_children =
      painting_info.scroll_offset_accumulation;
  if (paint_layer_.GetLayoutObject().HasOverflowClip())
    scroll_offset_accumulation_for_children +=
        paint_layer_.GetLayoutBox()->ScrolledContentOffset();

  for (; child; child = iterator.Next()) {
    PaintLayerPainter child_painter(*child->Layer());
    // If this Layer should paint into its own backing or a grouped backing,
    // that will be done via CompositedLayerMapping::paintContents() and
    // CompositedLayerMapping::doPaintTask().
    if (!child_painter.ShouldPaintLayerInSoftwareMode(
            painting_info.GetGlobalPaintFlags(), paint_flags))
      continue;

    PaintLayerPaintingInfo child_painting_info = painting_info;
    child_painting_info.scroll_offset_accumulation =
        scroll_offset_accumulation_for_children;
    // Rare case: accumulate scroll offset of non-stacking-context ancestors up
    // to m_paintLayer.
    for (PaintLayer* parent_layer = child->Layer()->Parent();
         parent_layer != &paint_layer_; parent_layer = parent_layer->Parent()) {
      if (parent_layer->GetLayoutObject().HasOverflowClip())
        child_painting_info.scroll_offset_accumulation +=
            parent_layer->GetLayoutBox()->ScrolledContentOffset();
    }

    if (child_painter.Paint(context, child_painting_info, paint_flags) ==
        kMayBeClippedByPaintDirtyRect)
      result = kMayBeClippedByPaintDirtyRect;
  }

  return result;
}

bool PaintLayerPainter::ShouldPaintLayerInSoftwareMode(
    const GlobalPaintFlags global_paint_flags,
    PaintLayerFlags paint_flags) {
  DisableCompositingQueryAsserts disabler;

  return paint_layer_.GetCompositingState() == kNotComposited ||
         (global_paint_flags & kGlobalPaintFlattenCompositingLayers);
}

void PaintLayerPainter::PaintOverflowControlsForFragments(
    const PaintLayerFragments& layer_fragments,
    GraphicsContext& context,
    const PaintLayerPaintingInfo& local_painting_info,
    PaintLayerFlags paint_flags) {
  PaintLayerScrollableArea* scrollable_area = paint_layer_.GetScrollableArea();
  if (!scrollable_area)
    return;

  Optional<DisplayItemCacheSkipper> cache_skipper;
  if (layer_fragments.size() > 1)
    cache_skipper.emplace(context);

  for (auto& fragment : layer_fragments) {
    // We need to apply the same clips and transforms that
    // paintFragmentWithPhase would have.
    LayoutRect cull_rect = fragment.background_rect.Rect();

    Optional<LayerClipRecorder> clip_recorder;
    if (NeedsToClip(local_painting_info, fragment.background_rect)) {
      clip_recorder.emplace(context, paint_layer_.GetLayoutObject(),
                            DisplayItem::kClipLayerOverflowControls,
                            fragment.background_rect,
                            local_painting_info.root_layer,
                            fragment.pagination_offset, paint_flags);
    }

    Optional<ScrollRecorder> scroll_recorder;
    if (!RuntimeEnabledFeatures::slimmingPaintV2Enabled() &&
        !local_painting_info.scroll_offset_accumulation.IsZero()) {
      cull_rect.Move(local_painting_info.scroll_offset_accumulation);
      scroll_recorder.emplace(context, paint_layer_.GetLayoutObject(),
                              DisplayItem::kScrollOverflowControls,
                              local_painting_info.scroll_offset_accumulation);
    }

    // We pass IntPoint() as the paint offset here, because
    // ScrollableArea::paintOverflowControls just ignores it and uses the
    // offset found in a previous pass.
    CullRect snapped_cull_rect(PixelSnappedIntRect(cull_rect));
    ScrollableAreaPainter(*scrollable_area)
        .PaintOverflowControls(context, IntPoint(), snapped_cull_rect, true);
  }
}

void PaintLayerPainter::PaintFragmentWithPhase(
    PaintPhase phase,
    const PaintLayerFragment& fragment,
    GraphicsContext& context,
    const ClipRect& clip_rect,
    const PaintLayerPaintingInfo& painting_info,
    PaintLayerFlags paint_flags,
    ClipState clip_state) {
  DCHECK(paint_layer_.IsSelfPaintingLayer());

  Optional<LayerClipRecorder> clip_recorder;
  if (clip_state != kHasClipped && painting_info.clip_to_dirty_rect &&
      NeedsToClip(painting_info, clip_rect)) {
    DisplayItem::Type clip_type =
        DisplayItem::PaintPhaseToClipLayerFragmentType(phase);
    LayerClipRecorder::BorderRadiusClippingRule clipping_rule;
    switch (phase) {
      case kPaintPhaseSelfBlockBackgroundOnly:  // Background painting will
                                                // handle clipping to self.
      case kPaintPhaseSelfOutlineOnly:
      case kPaintPhaseMask:  // Mask painting will handle clipping to self.
        clipping_rule = LayerClipRecorder::kDoNotIncludeSelfForBorderRadius;
        break;
      case kPaintPhaseClippingMask:
        if (paint_flags & kPaintLayerPaintingAncestorClippingMaskPhase) {
          // The ancestor is the thing that needs to clip, so do not include
          // this layer's clips.
          clipping_rule = LayerClipRecorder::kDoNotIncludeSelfForBorderRadius;
          break;
        }
      default:
        clipping_rule = LayerClipRecorder::kIncludeSelfForBorderRadius;
        break;
    }

    // TODO(schenney): Nested border-radius clips are not applied to composited
    // children, probably due to an incorrect clipRoot.
    // https://bugs.chromium.org/p/chromium/issues/detail?id=672561
    clip_recorder.emplace(context, paint_layer_.GetLayoutObject(), clip_type,
                          clip_rect, painting_info.root_layer,
                          fragment.pagination_offset, paint_flags,
                          clipping_rule);
  }

  // If we are painting a mask for any reason and we have already processed the
  // clips, there is no need to go through the remaining painting pipeline.
  // We know that the mask just needs the area bounded by the clip rects to be
  // filled with black.
  if (clip_recorder && phase == kPaintPhaseClippingMask) {
    FillMaskingFragment(context, clip_rect);
    return;
  }

  LayoutRect new_cull_rect(clip_rect.Rect());
  Optional<ScrollRecorder> scroll_recorder;
  LayoutPoint paint_offset = -paint_layer_.LayoutBoxLocation();
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    paint_offset += paint_layer_.GetLayoutObject().PaintOffset();
    new_cull_rect.Move(painting_info.scroll_offset_accumulation);
  } else {
    paint_offset += ToSize(fragment.layer_bounds.Location());
    if (!painting_info.scroll_offset_accumulation.IsZero()) {
      // As a descendant of the root layer, m_paintLayer's painting is not
      // controlled by the ScrollRecorders created by BlockPainter of the
      // ancestor layers up to the root layer, so we need to issue
      // ScrollRecorder for this layer seperately, with the scroll offset
      // accumulated from the root layer to the parent of this layer, to get the
      // same result as ScrollRecorder in BlockPainter.
      paint_offset += painting_info.scroll_offset_accumulation;

      new_cull_rect.Move(painting_info.scroll_offset_accumulation);
      scroll_recorder.emplace(context, paint_layer_.GetLayoutObject(), phase,
                              painting_info.scroll_offset_accumulation);
    }
  }
  PaintInfo paint_info(context, PixelSnappedIntRect(new_cull_rect), phase,
                       painting_info.GetGlobalPaintFlags(), paint_flags,
                       &painting_info.root_layer->GetLayoutObject());

  paint_layer_.GetLayoutObject().Paint(paint_info, paint_offset);
}

void PaintLayerPainter::PaintBackgroundForFragments(
    const PaintLayerFragments& layer_fragments,
    GraphicsContext& context,
    const LayoutRect& transparency_paint_dirty_rect,
    const PaintLayerPaintingInfo& local_painting_info,
    PaintLayerFlags paint_flags) {
  Optional<DisplayItemCacheSkipper> cache_skipper;
  if (layer_fragments.size() > 1)
    cache_skipper.emplace(context);

  for (auto& fragment : layer_fragments)
    PaintFragmentWithPhase(kPaintPhaseSelfBlockBackgroundOnly, fragment,
                           context, fragment.background_rect,
                           local_painting_info, paint_flags, kHasNotClipped);
}

void PaintLayerPainter::PaintForegroundForFragments(
    const PaintLayerFragments& layer_fragments,
    GraphicsContext& context,
    const LayoutRect& transparency_paint_dirty_rect,
    const PaintLayerPaintingInfo& local_painting_info,
    bool selection_only,
    PaintLayerFlags paint_flags) {
  DCHECK(!(paint_flags & kPaintLayerPaintingRootBackgroundOnly));

  // Optimize clipping for the single fragment case.
  bool should_clip = local_painting_info.clip_to_dirty_rect &&
                     layer_fragments.size() == 1 &&
                     !layer_fragments[0].foreground_rect.IsEmpty();
  ClipState clip_state = kHasNotClipped;
  Optional<LayerClipRecorder> clip_recorder;
  if (should_clip &&
      NeedsToClip(local_painting_info, layer_fragments[0].foreground_rect)) {
    clip_recorder.emplace(context, paint_layer_.GetLayoutObject(),
                          DisplayItem::kClipLayerForeground,
                          layer_fragments[0].foreground_rect,
                          local_painting_info.root_layer,
                          layer_fragments[0].pagination_offset, paint_flags);
    clip_state = kHasClipped;
  }

  // We have to loop through every fragment multiple times, since we have to
  // issue paint invalidations in each specific phase in order for interleaving
  // of the fragments to work properly.
  if (selection_only) {
    PaintForegroundForFragmentsWithPhase(kPaintPhaseSelection, layer_fragments,
                                         context, local_painting_info,
                                         paint_flags, clip_state);
  } else {
    if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled() ||
        paint_layer_.NeedsPaintPhaseDescendantBlockBackgrounds()) {
      size_t size_before =
          context.GetPaintController().NewDisplayItemList().size();
      PaintForegroundForFragmentsWithPhase(
          kPaintPhaseDescendantBlockBackgroundsOnly, layer_fragments, context,
          local_painting_info, paint_flags, clip_state);
      // Don't set the empty flag if we are not painting the whole background.
      if (!(paint_flags & kPaintLayerPaintingSkipRootBackground)) {
        bool phase_is_empty =
            context.GetPaintController().NewDisplayItemList().size() ==
            size_before;
        DCHECK(phase_is_empty ||
               paint_layer_.NeedsPaintPhaseDescendantBlockBackgrounds());
        paint_layer_.SetPreviousPaintPhaseDescendantBlockBackgroundsEmpty(
            phase_is_empty);
      }
    }

    if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled() ||
        paint_layer_.NeedsPaintPhaseFloat()) {
      size_t size_before =
          context.GetPaintController().NewDisplayItemList().size();
      PaintForegroundForFragmentsWithPhase(kPaintPhaseFloat, layer_fragments,
                                           context, local_painting_info,
                                           paint_flags, clip_state);
      bool phase_is_empty =
          context.GetPaintController().NewDisplayItemList().size() ==
          size_before;
      DCHECK(phase_is_empty || paint_layer_.NeedsPaintPhaseFloat());
      paint_layer_.SetPreviousPaintPhaseFloatEmpty(phase_is_empty);
    }

    PaintForegroundForFragmentsWithPhase(kPaintPhaseForeground, layer_fragments,
                                         context, local_painting_info,
                                         paint_flags, clip_state);

    if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled() ||
        paint_layer_.NeedsPaintPhaseDescendantOutlines()) {
      size_t size_before =
          context.GetPaintController().NewDisplayItemList().size();
      PaintForegroundForFragmentsWithPhase(
          kPaintPhaseDescendantOutlinesOnly, layer_fragments, context,
          local_painting_info, paint_flags, clip_state);
      bool phase_is_empty =
          context.GetPaintController().NewDisplayItemList().size() ==
          size_before;
      DCHECK(phase_is_empty ||
             paint_layer_.NeedsPaintPhaseDescendantOutlines());
      paint_layer_.SetPreviousPaintPhaseDescendantOutlinesEmpty(phase_is_empty);
    }
  }
}

void PaintLayerPainter::PaintForegroundForFragmentsWithPhase(
    PaintPhase phase,
    const PaintLayerFragments& layer_fragments,
    GraphicsContext& context,
    const PaintLayerPaintingInfo& local_painting_info,
    PaintLayerFlags paint_flags,
    ClipState clip_state) {
  Optional<DisplayItemCacheSkipper> cache_skipper;
  if (layer_fragments.size() > 1)
    cache_skipper.emplace(context);

  for (auto& fragment : layer_fragments) {
    if (!fragment.foreground_rect.IsEmpty())
      PaintFragmentWithPhase(phase, fragment, context, fragment.foreground_rect,
                             local_painting_info, paint_flags, clip_state);
  }
}

void PaintLayerPainter::PaintSelfOutlineForFragments(
    const PaintLayerFragments& layer_fragments,
    GraphicsContext& context,
    const PaintLayerPaintingInfo& local_painting_info,
    PaintLayerFlags paint_flags) {
  Optional<DisplayItemCacheSkipper> cache_skipper;
  if (layer_fragments.size() > 1)
    cache_skipper.emplace(context);

  for (auto& fragment : layer_fragments) {
    if (!fragment.background_rect.IsEmpty())
      PaintFragmentWithPhase(kPaintPhaseSelfOutlineOnly, fragment, context,
                             fragment.background_rect, local_painting_info,
                             paint_flags, kHasNotClipped);
  }
}

void PaintLayerPainter::PaintMaskForFragments(
    const PaintLayerFragments& layer_fragments,
    GraphicsContext& context,
    const PaintLayerPaintingInfo& local_painting_info,
    PaintLayerFlags paint_flags) {
  Optional<DisplayItemCacheSkipper> cache_skipper;
  if (layer_fragments.size() > 1)
    cache_skipper.emplace(context);

  Optional<ScopedPaintChunkProperties> scoped_paint_chunk_properties;
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    const auto* object_paint_properties =
        paint_layer_.GetLayoutObject().PaintProperties();
    DCHECK(object_paint_properties && object_paint_properties->Mask());
    PaintChunkProperties properties(
        context.GetPaintController().CurrentPaintChunkProperties());
    properties.property_tree_state.SetEffect(object_paint_properties->Mask());
    scoped_paint_chunk_properties.emplace(context.GetPaintController(),
                                          paint_layer_, properties);
  }

  for (auto& fragment : layer_fragments)
    PaintFragmentWithPhase(kPaintPhaseMask, fragment, context,
                           fragment.background_rect, local_painting_info,
                           paint_flags, kHasNotClipped);
}

void PaintLayerPainter::PaintChildClippingMaskForFragments(
    const PaintLayerFragments& layer_fragments,
    GraphicsContext& context,
    const PaintLayerPaintingInfo& local_painting_info,
    PaintLayerFlags paint_flags) {
  Optional<DisplayItemCacheSkipper> cache_skipper;
  if (layer_fragments.size() > 1)
    cache_skipper.emplace(context);

  for (auto& fragment : layer_fragments)
    PaintFragmentWithPhase(kPaintPhaseClippingMask, fragment, context,
                           fragment.foreground_rect, local_painting_info,
                           paint_flags, kHasNotClipped);
}

void PaintLayerPainter::PaintOverlayScrollbars(
    GraphicsContext& context,
    const LayoutRect& damage_rect,
    const GlobalPaintFlags paint_flags) {
  if (!paint_layer_.ContainsDirtyOverlayScrollbars())
    return;

  PaintLayerPaintingInfo painting_info(
      &paint_layer_, LayoutRect(EnclosingIntRect(damage_rect)), paint_flags,
      LayoutSize());
  Paint(context, painting_info, kPaintLayerPaintingOverlayScrollbars);

  paint_layer_.SetContainsDirtyOverlayScrollbars(false);
}

void PaintLayerPainter::FillMaskingFragment(GraphicsContext& context,
                                            const ClipRect& clip_rect) {
  const LayoutObject& layout_object = paint_layer_.GetLayoutObject();
  if (LayoutObjectDrawingRecorder::UseCachedDrawingIfPossible(
          context, layout_object, kPaintPhaseClippingMask))
    return;

  IntRect snapped_clip_rect = PixelSnappedIntRect(clip_rect.Rect());
  LayoutObjectDrawingRecorder drawing_recorder(
      context, layout_object, kPaintPhaseClippingMask, snapped_clip_rect);
  context.FillRect(snapped_clip_rect, Color::kBlack);
}

}  // namespace blink
