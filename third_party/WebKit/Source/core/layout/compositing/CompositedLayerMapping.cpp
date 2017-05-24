/*
 * Copyright (C) 2009, 2010, 2011 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/layout/compositing/CompositedLayerMapping.h"

#include <memory>

#include "core/HTMLNames.h"
#include "core/dom/DOMNodeIds.h"
#include "core/frame/FrameView.h"
#include "core/frame/RemoteFrame.h"
#include "core/frame/Settings.h"
#include "core/frame/VisualViewport.h"
#include "core/html/HTMLCanvasElement.h"
#include "core/html/HTMLIFrameElement.h"
#include "core/html/HTMLMediaElement.h"
#include "core/html/HTMLVideoElement.h"
#include "core/html/canvas/CanvasRenderingContext.h"
#include "core/layout/LayoutEmbeddedObject.h"
#include "core/layout/LayoutHTMLCanvas.h"
#include "core/layout/LayoutImage.h"
#include "core/layout/LayoutInline.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/LayoutVideo.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutAPIShim.h"
#include "core/layout/api/LayoutPartItem.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/loader/resource/ImageResourceContent.h"
#include "core/page/ChromeClient.h"
#include "core/page/Page.h"
#include "core/page/scrolling/ScrollingCoordinator.h"
#include "core/page/scrolling/StickyPositionScrollingConstraints.h"
#include "core/page/scrolling/TopDocumentRootScrollerController.h"
#include "core/paint/FramePaintTiming.h"
#include "core/paint/ObjectPaintInvalidator.h"
#include "core/paint/PaintInfo.h"
#include "core/paint/PaintLayerPainter.h"
#include "core/paint/PaintLayerStackingNodeIterator.h"
#include "core/paint/ScrollableAreaPainter.h"
#include "core/paint/TransformRecorder.h"
#include "core/plugins/PluginView.h"
#include "core/probe/CoreProbes.h"
#include "platform/LengthFunctions.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/fonts/FontCache.h"
#include "platform/geometry/TransformState.h"
#include "platform/graphics/BitmapImage.h"
#include "platform/graphics/CompositorFilterOperations.h"
#include "platform/graphics/CompositorMutableProperties.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/paint/ClipDisplayItem.h"
#include "platform/graphics/paint/CullRect.h"
#include "platform/graphics/paint/DrawingRecorder.h"
#include "platform/graphics/paint/PaintController.h"
#include "platform/graphics/paint/PaintRecordBuilder.h"
#include "platform/graphics/paint/TransformDisplayItem.h"
#include "platform/wtf/CurrentTime.h"
#include "platform/wtf/text/StringBuilder.h"
#include "public/platform/WebLayerStickyPositionConstraint.h"

namespace blink {

using namespace HTMLNames;

static IntRect ClipBox(LayoutBox& layout_object);

static IntRect ContentsRect(const LayoutObject& layout_object) {
  if (!layout_object.IsBox())
    return IntRect();
  if (layout_object.IsCanvas()) {
    return PixelSnappedIntRect(
        ToLayoutHTMLCanvas(layout_object).ReplacedContentRect());
  }
  if (layout_object.IsVideo()) {
    return PixelSnappedIntRect(
        ToLayoutVideo(layout_object).ReplacedContentRect());
  }

  return PixelSnappedIntRect(ToLayoutBox(layout_object).ContentBoxRect());
}

static IntRect BackgroundRect(const LayoutObject& layout_object) {
  if (!layout_object.IsBox())
    return IntRect();

  LayoutRect rect;
  const LayoutBox& box = ToLayoutBox(layout_object);
  return PixelSnappedIntRect(box.BackgroundRect(kBackgroundClipRect));
}

static inline bool IsCompositedCanvas(const LayoutObject& layout_object) {
  if (layout_object.IsCanvas()) {
    HTMLCanvasElement* canvas = toHTMLCanvasElement(layout_object.GetNode());
    if (CanvasRenderingContext* context = canvas->RenderingContext())
      return context->IsComposited();
  }
  return false;
}

static inline bool IsPlaceholderCanvas(const LayoutObject& layout_object) {
  if (layout_object.IsCanvas()) {
    HTMLCanvasElement* canvas = toHTMLCanvasElement(layout_object.GetNode());
    return canvas->SurfaceLayerBridge();
  }
  return false;
}

static bool HasBoxDecorationsOrBackgroundImage(const ComputedStyle& style) {
  return style.HasBoxDecorations() || style.HasBackgroundImage();
}

static bool ContentLayerSupportsDirectBackgroundComposition(
    const LayoutObject& layout_object) {
  // No support for decorations - border, border-radius or outline.
  // Only simple background - solid color or transparent.
  if (HasBoxDecorationsOrBackgroundImage(layout_object.StyleRef()))
    return false;

  // If there is no background, there is nothing to support.
  if (!layout_object.Style()->HasBackground())
    return true;

  // Simple background that is contained within the contents rect.
  return ContentsRect(layout_object).Contains(BackgroundRect(layout_object));
}

static WebLayer* PlatformLayerForPlugin(LayoutObject& layout_object) {
  if (!layout_object.IsEmbeddedObject())
    return nullptr;
  PluginView* plugin = ToLayoutEmbeddedObject(layout_object).Plugin();
  return plugin ? plugin->PlatformLayer() : nullptr;
}

static inline bool IsAcceleratedContents(LayoutObject& layout_object) {
  return IsCompositedCanvas(layout_object) ||
         (layout_object.IsEmbeddedObject() &&
          ToLayoutEmbeddedObject(layout_object)
              .RequiresAcceleratedCompositing()) ||
         layout_object.IsVideo();
}

CompositedLayerMapping::CompositedLayerMapping(PaintLayer& layer)
    : owning_layer_(layer),
      content_offset_in_compositing_layer_dirty_(false),
      pending_update_scope_(kGraphicsLayerUpdateNone),
      is_main_frame_layout_view_layer_(false),
      background_layer_paints_fixed_root_background_(false),
      scrolling_contents_are_empty_(false),
      background_paints_onto_scrolling_contents_layer_(false),
      background_paints_onto_graphics_layer_(false),
      draws_background_onto_content_layer_(false) {
  if (layer.IsRootLayer() && GetLayoutObject().GetFrame()->IsMainFrame())
    is_main_frame_layout_view_layer_ = true;

  CreatePrimaryGraphicsLayer();
}

CompositedLayerMapping::~CompositedLayerMapping() {
  // Hits in compositing/squashing/squash-onto-nephew.html.
  DisableCompositingQueryAsserts disabler;

  // Do not leave the destroyed pointer dangling on any Layers that painted to
  // this mapping's squashing layer.
  for (size_t i = 0; i < squashed_layers_.size(); ++i) {
    PaintLayer* old_squashed_layer = squashed_layers_[i].paint_layer;
    // Assert on incorrect mappings between layers and groups
    DCHECK_EQ(old_squashed_layer->GroupedMapping(), this);
    if (old_squashed_layer->GroupedMapping() == this) {
      old_squashed_layer->SetGroupedMapping(
          0, PaintLayer::kDoNotInvalidateLayerAndRemoveFromMapping);
      old_squashed_layer->SetLostGroupedMapping(true);
    }
  }

  UpdateClippingLayers(false, false, false);
  UpdateOverflowControlsLayers(false, false, false, false);
  UpdateChildTransformLayer(false);
  UpdateForegroundLayer(false);
  UpdateBackgroundLayer(false);
  UpdateMaskLayer(false);
  UpdateChildClippingMaskLayer(false);
  UpdateScrollingLayers(false);
  UpdateSquashingLayers(false);
  DestroyGraphicsLayers();
}

std::unique_ptr<GraphicsLayer> CompositedLayerMapping::CreateGraphicsLayer(
    CompositingReasons reasons,
    SquashingDisallowedReasons squashing_disallowed_reasons) {
  std::unique_ptr<GraphicsLayer> graphics_layer = GraphicsLayer::Create(this);

  graphics_layer->SetCompositingReasons(reasons);
  graphics_layer->SetSquashingDisallowedReasons(squashing_disallowed_reasons);
  if (Node* owning_node = owning_layer_.GetLayoutObject().GetNode())
    graphics_layer->SetOwnerNodeId(DOMNodeIds::IdForNode(owning_node));

  return graphics_layer;
}

void CompositedLayerMapping::CreatePrimaryGraphicsLayer() {
  graphics_layer_ =
      CreateGraphicsLayer(owning_layer_.GetCompositingReasons(),
                          owning_layer_.GetSquashingDisallowedReasons());

  UpdateOpacity(GetLayoutObject().StyleRef());
  UpdateTransform(GetLayoutObject().StyleRef());
  UpdateFilters(GetLayoutObject().StyleRef());
  UpdateBackdropFilters(GetLayoutObject().StyleRef());
  UpdateLayerBlendMode(GetLayoutObject().StyleRef());
  UpdateIsRootForIsolatedGroup();
}

void CompositedLayerMapping::DestroyGraphicsLayers() {
  if (graphics_layer_)
    graphics_layer_->RemoveFromParent();

  ancestor_clipping_layer_ = nullptr;
  ancestor_clipping_mask_layer_ = nullptr;
  graphics_layer_ = nullptr;
  foreground_layer_ = nullptr;
  background_layer_ = nullptr;
  child_containment_layer_ = nullptr;
  child_transform_layer_ = nullptr;
  mask_layer_ = nullptr;
  child_clipping_mask_layer_ = nullptr;

  scrolling_layer_ = nullptr;
  scrolling_contents_layer_ = nullptr;
}

void CompositedLayerMapping::UpdateOpacity(const ComputedStyle& style) {
  graphics_layer_->SetOpacity(CompositingOpacity(style.Opacity()));
}

void CompositedLayerMapping::UpdateTransform(const ComputedStyle& style) {
  // FIXME: This could use m_owningLayer.transform(), but that currently has
  // transform-origin baked into it, and we don't want that.
  TransformationMatrix t;
  if (owning_layer_.HasTransformRelatedProperty()) {
    style.ApplyTransform(
        t, LayoutSize(ToLayoutBox(GetLayoutObject()).PixelSnappedSize()),
        ComputedStyle::kExcludeTransformOrigin,
        ComputedStyle::kIncludeMotionPath,
        ComputedStyle::kIncludeIndependentTransformProperties);
    MakeMatrixRenderable(t, Compositor()->HasAcceleratedCompositing());
  }

  graphics_layer_->SetTransform(t);
}

void CompositedLayerMapping::UpdateFilters(const ComputedStyle& style) {
  graphics_layer_->SetFilters(
      OwningLayer().CreateCompositorFilterOperationsForFilter(style));
}

void CompositedLayerMapping::UpdateBackdropFilters(const ComputedStyle& style) {
  graphics_layer_->SetBackdropFilters(
      OwningLayer().CreateCompositorFilterOperationsForBackdropFilter(style));
}

void CompositedLayerMapping::UpdateStickyConstraints(
    const ComputedStyle& style,
    const PaintLayer* compositing_container) {
  bool sticky = style.GetPosition() == EPosition::kSticky;
  const PaintLayer* ancestor_overflow_layer =
      owning_layer_.AncestorOverflowLayer();
  // TODO(flackr): Do we still need this?
  if (sticky) {
    if (!ancestor_overflow_layer->IsRootLayer()) {
      sticky = ancestor_overflow_layer->NeedsCompositedScrolling();
    } else {
      sticky = GetLayoutObject().View()->GetFrameView()->IsScrollable();
    }
  }

  WebLayerStickyPositionConstraint web_constraint;
  if (sticky) {
    const StickyConstraintsMap& constraints_map =
        ancestor_overflow_layer->GetScrollableArea()->GetStickyConstraintsMap();
    const StickyPositionScrollingConstraints& constraints =
        constraints_map.at(&owning_layer_);

    // Find the layout offset of the unshifted sticky box within its parent
    // composited layer. This information is used by the compositor side to
    // compute the additional offset required to keep the element stuck under
    // compositor scrolling.
    //
    // Starting from the scroll container relative location, removing the
    // enclosing layer's offset and the content offset in the composited layer
    // results in the parent-layer relative offset.
    FloatPoint parent_relative_sticky_box_offset =
        constraints.ScrollContainerRelativeStickyBoxRect().Location();

    // The enclosing layers offset returned from |convertToLayerCoords| must be
    // adjusted for both scroll and ancestor sticky elements.
    LayoutPoint enclosing_layer_offset;
    compositing_container->ConvertToLayerCoords(ancestor_overflow_layer,
                                                enclosing_layer_offset);
    DCHECK(!ScrollParent() || ScrollParent() == ancestor_overflow_layer);
    if (!ScrollParent() && compositing_container != ancestor_overflow_layer) {
      enclosing_layer_offset += LayoutSize(
          ancestor_overflow_layer->GetScrollableArea()->GetScrollOffset());
    }
    // TODO(smcgruer): Until http://crbug.com/702229 is fixed, the nearest
    // sticky ancestor may be non-composited which will make this offset wrong.
    if (const LayoutBoxModelObject* ancestor =
            constraints.NearestStickyAncestor()) {
      enclosing_layer_offset -=
          RoundedIntSize(constraints_map.at(ancestor->Layer())
                             .GetTotalContainingBlockStickyOffset());
    }

    DCHECK(!content_offset_in_compositing_layer_dirty_);
    parent_relative_sticky_box_offset.MoveBy(
        FloatPoint(-enclosing_layer_offset) -
        FloatSize(ContentOffsetInCompositingLayer()));

    if (compositing_container != ancestor_overflow_layer) {
      parent_relative_sticky_box_offset.MoveBy(
          FloatPoint(compositing_container->GetCompositedLayerMapping()
                         ->ContentOffsetInCompositingLayer()));
    }

    web_constraint.is_sticky = true;
    web_constraint.is_anchored_left =
        constraints.GetAnchorEdges() &
        StickyPositionScrollingConstraints::kAnchorEdgeLeft;
    web_constraint.is_anchored_right =
        constraints.GetAnchorEdges() &
        StickyPositionScrollingConstraints::kAnchorEdgeRight;
    web_constraint.is_anchored_top =
        constraints.GetAnchorEdges() &
        StickyPositionScrollingConstraints::kAnchorEdgeTop;
    web_constraint.is_anchored_bottom =
        constraints.GetAnchorEdges() &
        StickyPositionScrollingConstraints::kAnchorEdgeBottom;
    web_constraint.left_offset = constraints.LeftOffset();
    web_constraint.right_offset = constraints.RightOffset();
    web_constraint.top_offset = constraints.TopOffset();
    web_constraint.bottom_offset = constraints.BottomOffset();
    web_constraint.parent_relative_sticky_box_offset =
        RoundedIntPoint(parent_relative_sticky_box_offset);
    web_constraint.scroll_container_relative_sticky_box_rect =
        EnclosingIntRect(constraints.ScrollContainerRelativeStickyBoxRect());
    web_constraint.scroll_container_relative_containing_block_rect =
        EnclosingIntRect(
            constraints.ScrollContainerRelativeContainingBlockRect());
    // TODO(smcgruer): Until http://crbug.com/702229 is fixed, the nearest
    // sticky layers may not be composited and we may incorrectly end up with
    // invalid layer IDs.
    LayoutBoxModelObject* sticky_box_shifting_ancestor =
        constraints.NearestStickyBoxShiftingStickyBox();
    if (sticky_box_shifting_ancestor &&
        sticky_box_shifting_ancestor->Layer()->GetCompositedLayerMapping()) {
      web_constraint.nearest_layer_shifting_sticky_box =
          sticky_box_shifting_ancestor->Layer()
              ->GetCompositedLayerMapping()
              ->MainGraphicsLayer()
              ->PlatformLayer()
              ->Id();
    }
    LayoutBoxModelObject* containing_block_shifting_ancestor =
        constraints.NearestStickyBoxShiftingContainingBlock();
    if (containing_block_shifting_ancestor &&
        containing_block_shifting_ancestor->Layer()
            ->GetCompositedLayerMapping()) {
      web_constraint.nearest_layer_shifting_containing_block =
          containing_block_shifting_ancestor->Layer()
              ->GetCompositedLayerMapping()
              ->MainGraphicsLayer()
              ->PlatformLayer()
              ->Id();
    }
  }

  graphics_layer_->SetStickyPositionConstraint(web_constraint);
}

void CompositedLayerMapping::UpdateLayerBlendMode(const ComputedStyle& style) {
  SetBlendMode(style.BlendMode());
}

void CompositedLayerMapping::UpdateIsRootForIsolatedGroup() {
  bool isolate = owning_layer_.ShouldIsolateCompositedDescendants();

  // non stacking context layers should never isolate
  DCHECK(owning_layer_.StackingNode()->IsStackingContext() || !isolate);

  graphics_layer_->SetIsRootForIsolatedGroup(isolate);
}

void CompositedLayerMapping::
    UpdateBackgroundPaintsOntoScrollingContentsLayer() {
  // We can only paint the background onto the scrolling contents layer if
  // it would be visually correct and we are using composited scrolling meaning
  // we have a scrolling contents layer to paint it into.
  BackgroundPaintLocation paint_location =
      owning_layer_.GetBackgroundPaintLocation();
  bool should_paint_onto_scrolling_contents_layer =
      paint_location & kBackgroundPaintInScrollingContents &&
      owning_layer_.GetScrollableArea()->UsesCompositedScrolling();
  if (should_paint_onto_scrolling_contents_layer !=
      BackgroundPaintsOntoScrollingContentsLayer()) {
    background_paints_onto_scrolling_contents_layer_ =
        should_paint_onto_scrolling_contents_layer;
    // The scrolling contents layer needs to be updated for changed
    // m_backgroundPaintsOntoScrollingContentsLayer.
    if (HasScrollingLayer())
      scrolling_contents_layer_->SetNeedsDisplay();
  }
  bool should_paint_onto_graphics_layer =
      !background_paints_onto_scrolling_contents_layer_ ||
      paint_location & kBackgroundPaintInGraphicsLayer;
  if (should_paint_onto_graphics_layer !=
      !!background_paints_onto_graphics_layer_) {
    background_paints_onto_graphics_layer_ = should_paint_onto_graphics_layer;
    // The graphics layer needs to be updated for changed
    // m_backgroundPaintsOntoGraphicsLayer.
    graphics_layer_->SetNeedsDisplay();
  }
}

void CompositedLayerMapping::UpdateContentsOpaque() {
  if (IsCompositedCanvas(GetLayoutObject())) {
    CanvasRenderingContext* context =
        toHTMLCanvasElement(GetLayoutObject().GetNode())->RenderingContext();
    WebLayer* layer = context ? context->PlatformLayer() : nullptr;
    // Determine whether the external texture layer covers the whole graphics
    // layer. This may not be the case if there are box decorations or
    // shadows.
    if (layer &&
        layer->Bounds() == graphics_layer_->PlatformLayer()->Bounds()) {
      // Determine whether the rendering context's external texture layer is
      // opaque.
      if (!context->CreationAttributes().alpha()) {
        graphics_layer_->SetContentsOpaque(true);
      } else {
        graphics_layer_->SetContentsOpaque(
            !Color(layer->BackgroundColor()).HasAlpha());
      }
    } else {
      graphics_layer_->SetContentsOpaque(false);
    }
  } else if (background_layer_) {
    graphics_layer_->SetContentsOpaque(false);
    background_layer_->SetContentsOpaque(
        owning_layer_.BackgroundIsKnownToBeOpaqueInRect(CompositedBounds()));
  } else if (IsPlaceholderCanvas(GetLayoutObject())) {
    // TODO(crbug.com/705019): Contents could be opaque, but that cannot be
    // determined from the main thread. Or can it?
    graphics_layer_->SetContentsOpaque(false);
  } else {
    // For non-root layers, background is painted by the scrolling contents
    // layer if all backgrounds are background attachment local, otherwise
    // background is painted by the primary graphics layer.
    if (HasScrollingLayer() &&
        background_paints_onto_scrolling_contents_layer_) {
      // Backgrounds painted onto the foreground are clipped by the padding box
      // rect.
      // TODO(flackr): This should actually check the entire overflow rect
      // within the scrolling contents layer but since we currently only trigger
      // this for solid color backgrounds the answer will be the same.
      scrolling_contents_layer_->SetContentsOpaque(
          owning_layer_.BackgroundIsKnownToBeOpaqueInRect(
              ToLayoutBox(GetLayoutObject()).PaddingBoxRect()));

      if (owning_layer_.GetBackgroundPaintLocation() &
          kBackgroundPaintInGraphicsLayer) {
        graphics_layer_->SetContentsOpaque(
            owning_layer_.BackgroundIsKnownToBeOpaqueInRect(
                CompositedBounds()));
      } else {
        // If we only paint the background onto the scrolling contents layer we
        // are going to leave a hole in the m_graphicsLayer where the background
        // is so it is not opaque.
        graphics_layer_->SetContentsOpaque(false);
      }
    } else {
      if (HasScrollingLayer())
        scrolling_contents_layer_->SetContentsOpaque(false);
      graphics_layer_->SetContentsOpaque(
          owning_layer_.BackgroundIsKnownToBeOpaqueInRect(CompositedBounds()));
    }
  }
}

void CompositedLayerMapping::UpdateRasterizationPolicy() {
  bool allow_transformed_rasterization =
      !RequiresCompositing(owning_layer_.GetCompositingReasons() &
                           ~kCompositingReasonSquashingDisallowed);
  graphics_layer_->ContentLayer()->SetAllowTransformedRasterization(
      allow_transformed_rasterization);
  if (squashing_layer_)
    squashing_layer_->ContentLayer()->SetAllowTransformedRasterization(true);
}

void CompositedLayerMapping::UpdateCompositedBounds() {
  DCHECK_EQ(owning_layer_.Compositor()->Lifecycle().GetState(),
            DocumentLifecycle::kInCompositingUpdate);
  // FIXME: if this is really needed for performance, it would be better to
  // store it on Layer.
  composited_bounds_ = owning_layer_.BoundingBoxForCompositing();
  content_offset_in_compositing_layer_dirty_ = true;
}

void CompositedLayerMapping::UpdateAfterPartResize() {
  if (GetLayoutObject().IsLayoutPart()) {
    if (PaintLayerCompositor* inner_compositor =
            PaintLayerCompositor::FrameContentsCompositor(
                ToLayoutPart(GetLayoutObject()))) {
      inner_compositor->FrameViewDidChangeSize();
      // We can floor this point because our frameviews are always aligned to
      // pixel boundaries.
      DCHECK(composited_bounds_.Location() ==
             FlooredIntPoint(composited_bounds_.Location()));
      inner_compositor->FrameViewDidChangeLocation(
          FlooredIntPoint(ContentsBox().Location()));
    }
  }
}

void CompositedLayerMapping::UpdateCompositingReasons() {
  // All other layers owned by this mapping will have the same compositing
  // reason for their lifetime, so they are initialized only when created.
  graphics_layer_->SetCompositingReasons(owning_layer_.GetCompositingReasons());
  graphics_layer_->SetSquashingDisallowedReasons(
      owning_layer_.GetSquashingDisallowedReasons());
}

bool CompositedLayerMapping::AncestorRoundedCornersWontClip(
    const LayoutBoxModelObject& child,
    const LayoutBoxModelObject& clipping_ancestor) {
  LayoutRect local_visual_rect = composited_bounds_;
  child.MapToVisualRectInAncestorSpace(&clipping_ancestor, local_visual_rect);
  FloatRoundedRect rounded_clip_rect =
      clipping_ancestor.Style()->GetRoundedInnerBorderFor(
          clipping_ancestor.LocalVisualRect());
  FloatRect inner_clip_rect = rounded_clip_rect.RadiusCenterRect();
  // The first condition catches cases where the child is certainly inside
  // the rounded corner portion of the border, and cannot be clipped by
  // the rounded portion. The second catches cases where the child is
  // entirely outside the rectangular border (ignoring rounded corners) so
  // is also unaffected by the rounded corners. In both cases the existing
  // rectangular clip is adequate and the mask is unnecessary.
  return inner_clip_rect.Contains(FloatRect(local_visual_rect)) ||
         !local_visual_rect.Intersects(
             EnclosingLayoutRect(rounded_clip_rect.Rect()));
}

void CompositedLayerMapping::
    OwningLayerClippedOrMaskedByLayerNotAboveCompositedAncestor(
        const PaintLayer* scroll_parent,
        bool& owning_layer_is_clipped,
        bool& owning_layer_is_masked) {
  owning_layer_is_clipped = false;
  owning_layer_is_masked = false;

  if (!owning_layer_.Parent())
    return;

  const PaintLayer* compositing_ancestor =
      owning_layer_.EnclosingLayerWithCompositedLayerMapping(kExcludeSelf);
  if (!compositing_ancestor)
    return;

  const LayoutBoxModelObject* clipping_container =
      owning_layer_.ClippingContainer();
  if (!clipping_container)
    return;

  if (clipping_container->EnclosingLayer() == scroll_parent)
    return;

  if (compositing_ancestor->GetLayoutObject().IsDescendantOf(
          clipping_container))
    return;

  // We ignore overflow clip here; we want composited overflow content to
  // behave as if it lives in an unclipped universe so it can prepaint, etc.
  // This means that we need to check if we are actually clipped before
  // setting up m_ancestorClippingLayer otherwise
  // updateAncestorClippingLayerGeometry will fail as the clip rect will be
  // infinite.
  // FIXME: this should use cached clip rects, but this sometimes give
  // inaccurate results (and trips the ASSERTS in PaintLayerClipper).
  ClipRectsContext clip_rects_context(compositing_ancestor, kUncachedClipRects,
                                      kIgnorePlatformOverlayScrollbarSize);
  clip_rects_context.SetIgnoreOverflowClip();

  ClipRect clip_rect;
  owning_layer_.Clipper(PaintLayer::kDoNotUseGeometryMapper)
      .CalculateBackgroundClipRect(clip_rects_context, clip_rect);
  IntRect parent_clip_rect = PixelSnappedIntRect(clip_rect.Rect());
  owning_layer_is_clipped = parent_clip_rect != LayoutRect::InfiniteIntRect();

  // TODO(schenney): CSS clips are not applied to composited children, and
  // should be via mask or by compositing the parent too.
  // https://bugs.chromium.org/p/chromium/issues/detail?id=615870
  DCHECK(clipping_container->Style());
  owning_layer_is_masked =
      owning_layer_is_clipped &&
      clipping_container->Style()->HasBorderRadius() &&
      !AncestorRoundedCornersWontClip(GetLayoutObject(), *clipping_container);
}

const PaintLayer* CompositedLayerMapping::ScrollParent() {
  const PaintLayer* scroll_parent = owning_layer_.ScrollParent();
  if (scroll_parent && !scroll_parent->NeedsCompositedScrolling())
    return nullptr;
  return scroll_parent;
}

bool CompositedLayerMapping::UpdateGraphicsLayerConfiguration() {
  DCHECK_EQ(owning_layer_.Compositor()->Lifecycle().GetState(),
            DocumentLifecycle::kInCompositingUpdate);

  // Note carefully: here we assume that the compositing state of all
  // descendants have been updated already, so it is legitimate to compute and
  // cache the composited bounds for this layer.
  UpdateCompositedBounds();

  PaintLayerCompositor* compositor = this->Compositor();
  LayoutObject& layout_object = this->GetLayoutObject();
  const ComputedStyle& style = layout_object.StyleRef();

  bool layer_config_changed = false;
  SetBackgroundLayerPaintsFixedRootBackground(
      compositor->NeedsFixedRootBackgroundLayer(&owning_layer_));

  // The background layer is currently only used for fixed root backgrounds.
  if (UpdateBackgroundLayer(background_layer_paints_fixed_root_background_))
    layer_config_changed = true;

  if (UpdateForegroundLayer(
          compositor->NeedsContentsCompositingLayer(&owning_layer_)))
    layer_config_changed = true;

  bool needs_descendants_clipping_layer =
      compositor->ClipsCompositingDescendants(&owning_layer_);

  // Our scrolling layer will clip.
  if (owning_layer_.NeedsCompositedScrolling())
    needs_descendants_clipping_layer = false;

  const PaintLayer* scroll_parent = this->ScrollParent();

  // This is required because compositing layers are parented according to the
  // z-order hierarchy, yet clipping goes down the layoutObject hierarchy. Thus,
  // a PaintLayer can be clipped by a PaintLayer that is an ancestor in the
  // layoutObject hierarchy, but a sibling in the z-order hierarchy. Further,
  // that sibling need not be composited at all. In such scenarios, an ancestor
  // clipping layer is necessary to apply the composited clip for this layer.
  bool needs_ancestor_clip = false;
  bool needs_ancestor_clipping_mask = false;
  OwningLayerClippedOrMaskedByLayerNotAboveCompositedAncestor(
      scroll_parent, needs_ancestor_clip, needs_ancestor_clipping_mask);
  if (UpdateClippingLayers(needs_ancestor_clip, needs_ancestor_clipping_mask,
                           needs_descendants_clipping_layer))
    layer_config_changed = true;

  bool scrolling_config_changed = false;
  if (UpdateScrollingLayers(owning_layer_.NeedsCompositedScrolling())) {
    layer_config_changed = true;
    scrolling_config_changed = true;
  }

  // If the outline needs to draw over the composited scrolling contents layer
  // or scrollbar layers it needs to be drawn into a separate layer.
  int min_border_width =
      std::min(layout_object.Style()->BorderTopWidth(),
               std::min(layout_object.Style()->BorderLeftWidth(),
                        std::min(layout_object.Style()->BorderRightWidth(),
                                 layout_object.Style()->BorderBottomWidth())));
  bool needs_decoration_outline_layer =
      owning_layer_.GetScrollableArea() &&
      owning_layer_.GetScrollableArea()->UsesCompositedScrolling() &&
      layout_object.Style()->HasOutline() &&
      layout_object.Style()->OutlineOffset() < -min_border_width;

  if (UpdateDecorationOutlineLayer(needs_decoration_outline_layer))
    layer_config_changed = true;

  if (UpdateOverflowControlsLayers(
          RequiresHorizontalScrollbarLayer(), RequiresVerticalScrollbarLayer(),
          RequiresScrollCornerLayer(), needs_ancestor_clip))
    layer_config_changed = true;

  bool has_perspective = style.HasPerspective();
  bool needs_child_transform_layer = has_perspective && layout_object.IsBox();
  if (UpdateChildTransformLayer(needs_child_transform_layer))
    layer_config_changed = true;

  if (UpdateSquashingLayers(!squashed_layers_.IsEmpty()))
    layer_config_changed = true;

  UpdateScrollParent(scroll_parent);
  UpdateClipParent(scroll_parent);

  if (layer_config_changed)
    UpdateInternalHierarchy();

  if (scrolling_config_changed) {
    if (layout_object.View())
      compositor->ScrollingLayerDidChange(&owning_layer_);
  }

  // A mask layer is not part of the hierarchy proper, it's an auxiliary layer
  // that's plugged into another GraphicsLayer that is part of the hierarchy.
  // It has no parent or child GraphicsLayer. For that reason, we process it
  // here, after the hierarchy has been updated.
  bool mask_layer_changed = UpdateMaskLayer(layout_object.HasMask());
  if (mask_layer_changed)
    graphics_layer_->SetMaskLayer(mask_layer_.get());

  bool has_child_clipping_layer =
      compositor->ClipsCompositingDescendants(&owning_layer_) &&
      (HasClippingLayer() || HasScrollingLayer());
  // If we have a border radius or clip path on a scrolling layer, we need a
  // clipping mask to properly clip the scrolled contents, even if there are no
  // composited descendants.
  bool has_clip_path = style.ClipPath();
  bool needs_child_clipping_mask =
      (has_clip_path || style.HasBorderRadius()) &&
      (has_child_clipping_layer || IsAcceleratedContents(layout_object) ||
       HasScrollingLayer());

  GraphicsLayer* layer_to_apply_child_clipping_mask = nullptr;
  bool should_apply_child_clipping_mask_on_contents = false;
  if (needs_child_clipping_mask) {
    if (has_clip_path) {
      // Clip path clips the entire subtree, including scrollbars. It must be
      // attached directly onto the main m_graphicsLayer.
      layer_to_apply_child_clipping_mask = graphics_layer_.get();
    } else if (HasClippingLayer()) {
      layer_to_apply_child_clipping_mask = ClippingLayer();
    } else if (HasScrollingLayer()) {
      layer_to_apply_child_clipping_mask = ScrollingLayer();
    } else if (IsAcceleratedContents(layout_object)) {
      should_apply_child_clipping_mask_on_contents = true;
    }
  }

  UpdateChildClippingMaskLayer(needs_child_clipping_mask);

  if (layer_to_apply_child_clipping_mask == graphics_layer_.get()) {
    if (graphics_layer_->MaskLayer() != child_clipping_mask_layer_.get()) {
      graphics_layer_->SetMaskLayer(child_clipping_mask_layer_.get());
      mask_layer_changed = true;
    }
  } else if (graphics_layer_->MaskLayer() &&
             graphics_layer_->MaskLayer() != mask_layer_.get()) {
    graphics_layer_->SetMaskLayer(nullptr);
    mask_layer_changed = true;
  }
  if (HasClippingLayer())
    ClippingLayer()->SetMaskLayer(layer_to_apply_child_clipping_mask ==
                                          ClippingLayer()
                                      ? child_clipping_mask_layer_.get()
                                      : nullptr);
  if (HasScrollingLayer())
    ScrollingLayer()->SetMaskLayer(layer_to_apply_child_clipping_mask ==
                                           ScrollingLayer()
                                       ? child_clipping_mask_layer_.get()
                                       : nullptr);
  graphics_layer_->SetContentsClippingMaskLayer(
      should_apply_child_clipping_mask_on_contents
          ? child_clipping_mask_layer_.get()
          : nullptr);

  UpdateBackgroundColor();

  if (layout_object.IsImage()) {
    if (IsDirectlyCompositedImage()) {
      UpdateImageContents();
    } else if (graphics_layer_->HasContentsLayer()) {
      graphics_layer_->SetContentsToImage(nullptr);
    }
  }

  if (WebLayer* layer = PlatformLayerForPlugin(layout_object)) {
    graphics_layer_->SetContentsToPlatformLayer(layer);
  } else if (layout_object.GetNode() &&
             layout_object.GetNode()->IsFrameOwnerElement() &&
             ToHTMLFrameOwnerElement(layout_object.GetNode())->ContentFrame()) {
    Frame* frame =
        ToHTMLFrameOwnerElement(layout_object.GetNode())->ContentFrame();
    if (frame->IsRemoteFrame()) {
      WebLayer* layer = ToRemoteFrame(frame)->GetWebLayer();
      graphics_layer_->SetContentsToPlatformLayer(layer);
    }
  } else if (layout_object.IsVideo()) {
    HTMLMediaElement* media_element =
        ToHTMLMediaElement(layout_object.GetNode());
    graphics_layer_->SetContentsToPlatformLayer(media_element->PlatformLayer());
  } else if (IsPlaceholderCanvas(layout_object)) {
    HTMLCanvasElement* canvas = toHTMLCanvasElement(layout_object.GetNode());
    graphics_layer_->SetContentsToPlatformLayer(
        canvas->SurfaceLayerBridge()->GetWebLayer());
    layer_config_changed = true;
  } else if (IsCompositedCanvas(layout_object)) {
    HTMLCanvasElement* canvas = toHTMLCanvasElement(layout_object.GetNode());
    if (CanvasRenderingContext* context = canvas->RenderingContext())
      graphics_layer_->SetContentsToPlatformLayer(context->PlatformLayer());
    layer_config_changed = true;
  }
  if (layout_object.IsLayoutPart()) {
    if (PaintLayerCompositor::AttachFrameContentLayersToIframeLayer(
            ToLayoutPart(layout_object)))
      layer_config_changed = true;
  }

  // Changes to either the internal hierarchy or the mask layer have an impact
  // on painting phases, so we need to update when either are updated.
  if (layer_config_changed || mask_layer_changed)
    UpdatePaintingPhases();

  UpdateElementIdAndCompositorMutableProperties();

  graphics_layer_->SetHasWillChangeTransformHint(
      style.HasWillChangeTransformHint());

  if (style.Preserves3D() && style.HasOpacity() &&
      owning_layer_.Has3DTransformedDescendant())
    UseCounter::Count(layout_object.GetDocument(),
                      UseCounter::kOpacityWithPreserve3DQuirk);

  return layer_config_changed;
}

static IntRect ClipBox(LayoutBox& layout_object) {
  // TODO(chrishtr): pixel snapping is most likely incorrect here.
  return PixelSnappedIntRect(layout_object.ClippingRect());
}

static LayoutPoint ComputeOffsetFromCompositedAncestor(
    const PaintLayer* layer,
    const PaintLayer* composited_ancestor,
    const LayoutPoint& local_representative_point_for_fragmentation) {
  // Add in the offset of the composited bounds from the coordinate space of
  // the PaintLayer, since visualOffsetFromAncestor() requires the pre-offset
  // input to be in the space of the PaintLayer. We also need to add in this
  // offset before computation of visualOffsetFromAncestor(), because it affects
  // fragmentation offset if compositedAncestor crosses a pagination boundary.
  //
  // Currently, visual fragmentation for composited layers is not implemented.
  // For fragmented contents, we paint in the logical coordinates of the flow
  // thread, then split the result by fragment boundary and paste each part
  // into each fragment's physical position.
  // Since composited layers don't support visual fragmentation, we have to
  // choose a "representative" fragment to position the painted contents. This
  // is where localRepresentativePointForFragmentation comes into play.
  // The fragment that the representative point resides in will be chosen as
  // the representative fragment for layer position purpose.
  // For layers that are not fragmented, the point doesn't affect behavior as
  // there is one and only one fragment.
  LayoutPoint offset = layer->VisualOffsetFromAncestor(
      composited_ancestor, local_representative_point_for_fragmentation);
  if (composited_ancestor)
    offset.Move(composited_ancestor->GetCompositedLayerMapping()
                    ->OwningLayer()
                    .SubpixelAccumulation());
  offset.MoveBy(-local_representative_point_for_fragmentation);
  return offset;
}

void CompositedLayerMapping::ComputeBoundsOfOwningLayer(
    const PaintLayer* composited_ancestor,
    IntRect& local_bounds,
    IntRect& compositing_bounds_relative_to_composited_ancestor,
    LayoutPoint& offset_from_composited_ancestor,
    IntPoint& snapped_offset_from_composited_ancestor) {
  // HACK(chrishtr): adjust for position of inlines.
  LayoutPoint local_representative_point_for_fragmentation;
  if (owning_layer_.GetLayoutObject().IsLayoutInline()) {
    local_representative_point_for_fragmentation =
        ToLayoutInline(owning_layer_.GetLayoutObject()).FirstLineBoxTopLeft();
  }
  offset_from_composited_ancestor = ComputeOffsetFromCompositedAncestor(
      &owning_layer_, composited_ancestor,
      local_representative_point_for_fragmentation);
  snapped_offset_from_composited_ancestor =
      IntPoint(offset_from_composited_ancestor.X().Round(),
               offset_from_composited_ancestor.Y().Round());

  LayoutSize subpixel_accumulation;
  if (!owning_layer_.Transform() ||
      owning_layer_.Transform()->IsIdentityOrTranslation()) {
    subpixel_accumulation = offset_from_composited_ancestor -
                            snapped_offset_from_composited_ancestor;
  }
  // Otherwise discard the sub-pixel remainder because paint offset can't be
  // transformed by a non-translation transform.
  owning_layer_.SetSubpixelAccumulation(subpixel_accumulation);

  // Move the bounds by the subpixel accumulation so that it pixel-snaps
  // relative to absolute pixels instead of local coordinates.
  LayoutRect local_raw_compositing_bounds = CompositedBounds();
  local_raw_compositing_bounds.Move(subpixel_accumulation);
  local_bounds = PixelSnappedIntRect(local_raw_compositing_bounds);

  compositing_bounds_relative_to_composited_ancestor = local_bounds;
  compositing_bounds_relative_to_composited_ancestor.MoveBy(
      snapped_offset_from_composited_ancestor);
}

void CompositedLayerMapping::UpdateSquashingLayerGeometry(
    const IntPoint& graphics_layer_parent_location,
    const PaintLayer* compositing_container,
    Vector<GraphicsLayerPaintInfo>& layers,
    GraphicsLayer* squashing_layer,
    LayoutPoint* offset_from_transformed_ancestor,
    Vector<PaintLayer*>& layers_needing_paint_invalidation) {
  if (!squashing_layer)
    return;

  LayoutPoint compositing_container_offset_from_parent_graphics_layer =
      -graphics_layer_parent_location;
  if (compositing_container)
    compositing_container_offset_from_parent_graphics_layer +=
        compositing_container->SubpixelAccumulation();

#if 0 && DCHECK_IS_ON()
  // TODO(trchen): We should enable this for below comment out |DCHECK()| once
  // we have simple reproduce case and fix it. See http://crbug.com/646437 for
  // details.
  const PaintLayer* commonTransformAncestor = nullptr;
  if (compositingContainer && compositingContainer->transform())
    commonTransformAncestor = compositingContainer;
  else if (compositingContainer)
    commonTransformAncestor = compositingContainer->transformAncestor();
#endif
  // FIXME: Cache these offsets.
  LayoutPoint compositing_container_offset_from_transformed_ancestor;
  if (compositing_container && !compositing_container->Transform())
    compositing_container_offset_from_transformed_ancestor =
        compositing_container->ComputeOffsetFromTransformedAncestor();

  LayoutRect total_squash_bounds;
  for (size_t i = 0; i < layers.size(); ++i) {
    LayoutRect squashed_bounds =
        layers[i].paint_layer->BoundingBoxForCompositing();

    // Store the local bounds of the Layer subtree before applying the offset.
    layers[i].composited_bounds = squashed_bounds;

#if 0 && DCHECK_IS_ON()
    // TODO(trchen): We should enable this |DCHECK()| once we have simple
    // reproduce case and fix it. See http://crbug.com/646437 for details.
    DCHECK(layers[i].paintLayer->transformAncestor() ==
           commonTransformAncestor);
#endif
    LayoutPoint squashed_layer_offset_from_transformed_ancestor =
        layers[i].paint_layer->ComputeOffsetFromTransformedAncestor();
    LayoutSize squashed_layer_offset_from_compositing_container =
        squashed_layer_offset_from_transformed_ancestor -
        compositing_container_offset_from_transformed_ancestor;

    squashed_bounds.Move(squashed_layer_offset_from_compositing_container);
    total_squash_bounds.Unite(squashed_bounds);
  }

  // The totalSquashBounds is positioned with respect to compositingContainer.
  // But the squashingLayer needs to be positioned with respect to the
  // graphicsLayerParent.  The conversion between compositingContainer and the
  // graphicsLayerParent is already computed as
  // compositingContainerOffsetFromParentGraphicsLayer.
  total_squash_bounds.MoveBy(
      compositing_container_offset_from_parent_graphics_layer);
  const IntRect squash_layer_bounds = EnclosingIntRect(total_squash_bounds);
  const IntPoint squash_layer_origin = squash_layer_bounds.Location();
  const LayoutSize squash_layer_origin_in_compositing_container_space =
      squash_layer_origin -
      compositing_container_offset_from_parent_graphics_layer;

  // Now that the squashing bounds are known, we can convert the PaintLayer
  // painting offsets from compositingContainer space to the squashing layer
  // space.
  //
  // The painting offset we want to compute for each squashed PaintLayer is
  // essentially the position of the squashed PaintLayer described w.r.t.
  // compositingContainer's origin.  So we just need to convert that point from
  // compositingContainer space to the squashing layer's space. This is done by
  // subtracting squashLayerOriginInCompositingContainerSpace, but then the
  // offset overall needs to be negated because that's the direction that the
  // painting code expects the offset to be.
  for (size_t i = 0; i < layers.size(); ++i) {
    const LayoutPoint squashed_layer_offset_from_transformed_ancestor =
        layers[i].paint_layer->ComputeOffsetFromTransformedAncestor();
    const LayoutSize offset_from_squash_layer_origin =
        (squashed_layer_offset_from_transformed_ancestor -
         compositing_container_offset_from_transformed_ancestor) -
        squash_layer_origin_in_compositing_container_space;

    IntSize new_offset_from_layout_object =
        -IntSize(offset_from_squash_layer_origin.Width().Round(),
                 offset_from_squash_layer_origin.Height().Round());
    LayoutSize subpixel_accumulation =
        offset_from_squash_layer_origin + new_offset_from_layout_object;
    if (layers[i].offset_from_layout_object_set &&
        layers[i].offset_from_layout_object != new_offset_from_layout_object) {
      // It is ok to issue paint invalidation here, because all of the geometry
      // needed to correctly invalidate paint is computed by this point.
      DisablePaintInvalidationStateAsserts disabler;
      ObjectPaintInvalidator(layers[i].paint_layer->GetLayoutObject())
          .InvalidatePaintIncludingNonCompositingDescendants();

      TRACE_LAYER_INVALIDATION(layers[i].paint_layer,
                               InspectorLayerInvalidationTrackingEvent::
                                   kSquashingLayerGeometryWasUpdated);
      layers_needing_paint_invalidation.push_back(layers[i].paint_layer);
    }
    layers[i].offset_from_layout_object = new_offset_from_layout_object;
    layers[i].offset_from_layout_object_set = true;

    layers[i].paint_layer->SetSubpixelAccumulation(subpixel_accumulation);
  }

  squashing_layer->SetPosition(squash_layer_bounds.Location());
  squashing_layer->SetSize(FloatSize(squash_layer_bounds.Size()));

  *offset_from_transformed_ancestor =
      compositing_container_offset_from_transformed_ancestor;
  offset_from_transformed_ancestor->Move(
      squash_layer_origin_in_compositing_container_space);

  for (size_t i = 0; i < layers.size(); ++i)
    layers[i].local_clip_rect_for_squashed_layer =
        LocalClipRectForSquashedLayer(owning_layer_, layers[i], layers);
}

void CompositedLayerMapping::UpdateGraphicsLayerGeometry(
    const PaintLayer* compositing_container,
    const PaintLayer* compositing_stacking_context,
    Vector<PaintLayer*>& layers_needing_paint_invalidation) {
  DCHECK_EQ(owning_layer_.Compositor()->Lifecycle().GetState(),
            DocumentLifecycle::kInCompositingUpdate);

  // Set transform property, if it is not animating. We have to do this here
  // because the transform is affected by the layer dimensions.
  if (!GetLayoutObject().Style()->IsRunningTransformAnimationOnCompositor())
    UpdateTransform(GetLayoutObject().StyleRef());

  // Set opacity, if it is not animating.
  if (!GetLayoutObject().Style()->IsRunningOpacityAnimationOnCompositor())
    UpdateOpacity(GetLayoutObject().StyleRef());

  if (!GetLayoutObject().Style()->IsRunningFilterAnimationOnCompositor())
    UpdateFilters(GetLayoutObject().StyleRef());

  if (!GetLayoutObject()
           .Style()
           ->IsRunningBackdropFilterAnimationOnCompositor())
    UpdateBackdropFilters(GetLayoutObject().StyleRef());

  // We compute everything relative to the enclosing compositing layer.
  IntRect ancestor_compositing_bounds;
  if (compositing_container) {
    DCHECK(compositing_container->HasCompositedLayerMapping());
    ancestor_compositing_bounds =
        compositing_container->GetCompositedLayerMapping()
            ->PixelSnappedCompositedBounds();
  }

  IntRect local_compositing_bounds;
  IntRect relative_compositing_bounds;
  LayoutPoint offset_from_composited_ancestor;
  IntPoint snapped_offset_from_composited_ancestor;
  ComputeBoundsOfOwningLayer(compositing_container, local_compositing_bounds,
                             relative_compositing_bounds,
                             offset_from_composited_ancestor,
                             snapped_offset_from_composited_ancestor);

  IntPoint graphics_layer_parent_location;
  ComputeGraphicsLayerParentLocation(compositing_container,
                                     ancestor_compositing_bounds,
                                     graphics_layer_parent_location);

  // Might update graphicsLayerParentLocation.
  UpdateAncestorClippingLayerGeometry(compositing_container,
                                      snapped_offset_from_composited_ancestor,
                                      graphics_layer_parent_location);

  FloatSize contents_size(relative_compositing_bounds.Size());

  UpdateMainGraphicsLayerGeometry(relative_compositing_bounds,
                                  local_compositing_bounds,
                                  graphics_layer_parent_location);
  UpdateOverflowControlsHostLayerGeometry(compositing_stacking_context,
                                          compositing_container,
                                          graphics_layer_parent_location);
  UpdateContentsOffsetInCompositingLayer(
      snapped_offset_from_composited_ancestor, graphics_layer_parent_location);
  UpdateStickyConstraints(GetLayoutObject().StyleRef(), compositing_container);
  UpdateSquashingLayerGeometry(
      graphics_layer_parent_location, compositing_container, squashed_layers_,
      squashing_layer_.get(),
      &squashing_layer_offset_from_transformed_ancestor_,
      layers_needing_paint_invalidation);

  // If we have a layer that clips children, position it.
  IntRect clipping_box;
  if (child_containment_layer_ && GetLayoutObject().IsBox())
    clipping_box = ClipBox(ToLayoutBox(GetLayoutObject()));

  UpdateChildTransformLayerGeometry();
  UpdateChildContainmentLayerGeometry(clipping_box, local_compositing_bounds);

  UpdateMaskLayerGeometry();
  UpdateTransformGeometry(snapped_offset_from_composited_ancestor,
                          relative_compositing_bounds);
  UpdateForegroundLayerGeometry(contents_size, clipping_box);
  UpdateBackgroundLayerGeometry(contents_size);
  // TODO(yigu): Currently the decoration layer uses the same contentSize
  // as background layer and foreground layer. There are scenarios that
  // the sizes could be different. The actual size of the decoration layer
  // should be calculated separately.
  // The size of the background layer should be different as well. We need to
  // check whether we are painting the decoration layer into the background and
  // then ignore or consider the outline when determining the contentSize.
  UpdateDecorationOutlineLayerGeometry(contents_size);
  UpdateScrollingLayerGeometry(local_compositing_bounds);
  UpdateChildClippingMaskLayerGeometry();

  if (owning_layer_.GetScrollableArea() &&
      owning_layer_.GetScrollableArea()->ScrollsOverflow())
    owning_layer_.GetScrollableArea()->PositionOverflowControls();

  UpdateLayerBlendMode(GetLayoutObject().StyleRef());
  UpdateIsRootForIsolatedGroup();
  UpdateContentsRect();
  UpdateBackgroundColor();
  UpdateDrawsContent();
  UpdateElementIdAndCompositorMutableProperties();
  UpdateBackgroundPaintsOntoScrollingContentsLayer();
  UpdateContentsOpaque();
  UpdateRasterizationPolicy();
  UpdateAfterPartResize();
  UpdateRenderingContext();
  UpdateShouldFlattenTransform();
  UpdateChildrenTransform();
  UpdateScrollParent(ScrollParent());
  RegisterScrollingLayers();

  UpdateCompositingReasons();
}

void CompositedLayerMapping::UpdateMainGraphicsLayerGeometry(
    const IntRect& relative_compositing_bounds,
    const IntRect& local_compositing_bounds,
    const IntPoint& graphics_layer_parent_location) {
  graphics_layer_->SetPosition(FloatPoint(
      relative_compositing_bounds.Location() - graphics_layer_parent_location));
  graphics_layer_->SetOffsetFromLayoutObject(
      ToIntSize(local_compositing_bounds.Location()));

  FloatSize old_size = graphics_layer_->Size();
  const FloatSize contents_size(relative_compositing_bounds.Size());
  if (old_size != contents_size)
    graphics_layer_->SetSize(contents_size);

  // m_graphicsLayer is the corresponding GraphicsLayer for this PaintLayer and
  // its non-compositing descendants. So, the visibility flag for
  // m_graphicsLayer should be true if there are any non-compositing visible
  // layers.
  bool contents_visible = owning_layer_.HasVisibleContent() ||
                          HasVisibleNonCompositingDescendant(&owning_layer_);
  graphics_layer_->SetContentsVisible(contents_visible);

  graphics_layer_->SetBackfaceVisibility(
      GetLayoutObject().Style()->BackfaceVisibility() ==
      kBackfaceVisibilityVisible);
}

void CompositedLayerMapping::ComputeGraphicsLayerParentLocation(
    const PaintLayer* compositing_container,
    const IntRect& ancestor_compositing_bounds,
    IntPoint& graphics_layer_parent_location) {
  if (compositing_container &&
      compositing_container->GetCompositedLayerMapping()->HasClippingLayer() &&
      compositing_container->GetLayoutObject().IsBox()) {
    // If the compositing ancestor has a layer to clip children, we parent in
    // that, and therefore position relative to it.
    IntRect clipping_box =
        ClipBox(ToLayoutBox(compositing_container->GetLayoutObject()));
    graphics_layer_parent_location =
        clipping_box.Location() +
        RoundedIntSize(compositing_container->SubpixelAccumulation());
  } else if (compositing_container &&
             compositing_container->GetCompositedLayerMapping()
                 ->ChildTransformLayer()) {
    // Similarly, if the compositing ancestor has a child transform layer, we
    // parent in that, and therefore position relative to it. It's already taken
    // into account the contents offset, so we do not need to here.
    graphics_layer_parent_location =
        RoundedIntPoint(compositing_container->SubpixelAccumulation());
  } else if (compositing_container) {
    graphics_layer_parent_location = ancestor_compositing_bounds.Location();
  } else {
    graphics_layer_parent_location =
        GetLayoutObject().View()->DocumentRect().Location();
  }

  if (compositing_container &&
      compositing_container->NeedsCompositedScrolling()) {
    LayoutBox& layout_box =
        ToLayoutBox(compositing_container->GetLayoutObject());
    IntSize scroll_offset = layout_box.ScrolledContentOffset();
    IntPoint scroll_origin =
        compositing_container->GetScrollableArea()->ScrollOrigin();
    scroll_origin.Move(-layout_box.BorderLeft().ToInt(),
                       -layout_box.BorderTop().ToInt());
    graphics_layer_parent_location = -(scroll_origin + scroll_offset);
  }
}

void CompositedLayerMapping::UpdateAncestorClippingLayerGeometry(
    const PaintLayer* compositing_container,
    const IntPoint& snapped_offset_from_composited_ancestor,
    IntPoint& graphics_layer_parent_location) {
  if (!compositing_container || !ancestor_clipping_layer_)
    return;

  ClipRectsContext clip_rects_context(compositing_container,
                                      kPaintingClipRectsIgnoringOverflowClip,
                                      kIgnorePlatformOverlayScrollbarSize);

  ClipRect parent_clip_rect;
  owning_layer_.Clipper(PaintLayer::kDoNotUseGeometryMapper)
      .CalculateBackgroundClipRect(clip_rects_context, parent_clip_rect);

  IntRect snapped_parent_clip_rect(
      PixelSnappedIntRect(parent_clip_rect.Rect()));

  DCHECK(snapped_parent_clip_rect != LayoutRect::InfiniteIntRect());
  ancestor_clipping_layer_->SetPosition(FloatPoint(
      snapped_parent_clip_rect.Location() - graphics_layer_parent_location));
  ancestor_clipping_layer_->SetSize(FloatSize(snapped_parent_clip_rect.Size()));

  // backgroundRect is relative to compositingContainer, so subtract
  // snappedOffsetFromCompositedAncestor.X/snappedOffsetFromCompositedAncestor.Y
  // to get back to local coords.
  ancestor_clipping_layer_->SetOffsetFromLayoutObject(
      snapped_parent_clip_rect.Location() -
      snapped_offset_from_composited_ancestor);

  if (ancestor_clipping_mask_layer_) {
    ancestor_clipping_mask_layer_->SetOffsetFromLayoutObject(
        ancestor_clipping_layer_->OffsetFromLayoutObject());
    ancestor_clipping_mask_layer_->SetSize(ancestor_clipping_layer_->Size());
    ancestor_clipping_mask_layer_->SetNeedsDisplay();
  }

  // The primary layer is then parented in, and positioned relative to this
  // clipping layer.
  graphics_layer_parent_location = snapped_parent_clip_rect.Location();
}

void CompositedLayerMapping::UpdateOverflowControlsHostLayerGeometry(
    const PaintLayer* compositing_stacking_context,
    const PaintLayer* compositing_container,
    IntPoint graphics_layer_parent_location) {
  if (!overflow_controls_host_layer_)
    return;

  // To position and clip the scrollbars correctly, m_overflowControlsHostLayer
  // should match our border box rect, which is at the origin of our
  // LayoutObject. Its position is computed in various ways depending on who its
  // parent GraphicsLayer is going to be.
  LayoutPoint host_layer_position;

  if (NeedsToReparentOverflowControls()) {
    CompositedLayerMapping* stacking_clm =
        compositing_stacking_context->GetCompositedLayerMapping();
    DCHECK(stacking_clm);

    // Either m_overflowControlsHostLayer or
    // m_overflowControlsAncestorClippingLayer (if it exists) will be a child of
    // the main GraphicsLayer of the compositing stacking context.
    IntSize stacking_offset_from_layout_object =
        stacking_clm->MainGraphicsLayer()->OffsetFromLayoutObject();

    if (overflow_controls_ancestor_clipping_layer_) {
      overflow_controls_ancestor_clipping_layer_->SetSize(
          ancestor_clipping_layer_->Size());
      overflow_controls_ancestor_clipping_layer_->SetOffsetFromLayoutObject(
          ancestor_clipping_layer_->OffsetFromLayoutObject());
      overflow_controls_ancestor_clipping_layer_->SetMasksToBounds(true);

      FloatPoint position;
      if (compositing_stacking_context == compositing_container) {
        position = ancestor_clipping_layer_->GetPosition();
      } else {
        // graphicsLayerParentLocation is the location of
        // m_ancestorClippingLayer relative to compositingContainer (including
        // any offset from compositingContainer's m_childContainmentLayer).
        LayoutPoint offset = LayoutPoint(graphics_layer_parent_location);
        compositing_container->ConvertToLayerCoords(
            compositing_stacking_context, offset);
        position =
            FloatPoint(offset) - FloatSize(stacking_offset_from_layout_object);
      }

      overflow_controls_ancestor_clipping_layer_->SetPosition(position);
      host_layer_position.Move(
          -ancestor_clipping_layer_->OffsetFromLayoutObject());
    } else {
      // The controls are in the same 2D space as the compositing container, so
      // we can map them into the space of the container.
      TransformState transform_state(TransformState::kApplyTransformDirection,
                                     FloatPoint());
      owning_layer_.GetLayoutObject().MapLocalToAncestor(
          &compositing_stacking_context->GetLayoutObject(), transform_state,
          kApplyContainerFlip);
      transform_state.Flatten();
      host_layer_position = LayoutPoint(transform_state.LastPlanarPoint());
      if (PaintLayerScrollableArea* scrollable_area =
              compositing_stacking_context->GetScrollableArea()) {
        host_layer_position.Move(
            LayoutSize(ToFloatSize(scrollable_area->ScrollPosition())));
      }
      host_layer_position.Move(-stacking_offset_from_layout_object);
    }
  } else {
    host_layer_position.Move(-graphics_layer_->OffsetFromLayoutObject());
  }

  overflow_controls_host_layer_->SetPosition(FloatPoint(host_layer_position));

  const IntRect border_box =
      ToLayoutBox(owning_layer_.GetLayoutObject()).PixelSnappedBorderBoxRect();
  overflow_controls_host_layer_->SetSize(FloatSize(border_box.Size()));
  overflow_controls_host_layer_->SetMasksToBounds(true);
  overflow_controls_host_layer_->SetBackfaceVisibility(
      owning_layer_.GetLayoutObject().Style()->BackfaceVisibility() ==
      kBackfaceVisibilityVisible);
}

void CompositedLayerMapping::UpdateChildContainmentLayerGeometry(
    const IntRect& clipping_box,
    const IntRect& local_compositing_bounds) {
  if (!child_containment_layer_)
    return;

  FloatPoint clip_position_in_layout_object_space(
      clipping_box.Location() - local_compositing_bounds.Location() +
      RoundedIntSize(owning_layer_.SubpixelAccumulation()));

  // If there are layers between the the child containment layer and
  // m_graphicsLayer (eg, the child transform layer), we must adjust the clip
  // position to get it in the correct space.
  FloatPoint clip_position_in_parent_space =
      clip_position_in_layout_object_space;
  for (GraphicsLayer* ancestor = child_containment_layer_->Parent();
       ancestor != MainGraphicsLayer(); ancestor = ancestor->Parent())
    clip_position_in_parent_space -= ToFloatSize(ancestor->GetPosition());

  child_containment_layer_->SetPosition(clip_position_in_parent_space);
  child_containment_layer_->SetSize(FloatSize(clipping_box.Size()));
  child_containment_layer_->SetOffsetFromLayoutObject(
      ToIntSize(clipping_box.Location()));
  if (child_clipping_mask_layer_ && !scrolling_layer_ &&
      !GetLayoutObject().Style()->ClipPath()) {
    child_clipping_mask_layer_->SetSize(child_containment_layer_->Size());
    child_clipping_mask_layer_->SetOffsetFromLayoutObject(
        child_containment_layer_->OffsetFromLayoutObject());
  }
}

void CompositedLayerMapping::UpdateChildTransformLayerGeometry() {
  if (!child_transform_layer_)
    return;
  const IntRect border_box =
      ToLayoutBox(owning_layer_.GetLayoutObject()).PixelSnappedBorderBoxRect();
  child_transform_layer_->SetSize(FloatSize(border_box.Size()));
  child_transform_layer_->SetPosition(
      FloatPoint(ContentOffsetInCompositingLayer()));
}

void CompositedLayerMapping::UpdateMaskLayerGeometry() {
  if (!mask_layer_)
    return;

  if (mask_layer_->Size() != graphics_layer_->Size()) {
    mask_layer_->SetSize(graphics_layer_->Size());
    mask_layer_->SetNeedsDisplay();
  }
  mask_layer_->SetPosition(FloatPoint());
  mask_layer_->SetOffsetFromLayoutObject(
      graphics_layer_->OffsetFromLayoutObject());
}

void CompositedLayerMapping::UpdateTransformGeometry(
    const IntPoint& snapped_offset_from_composited_ancestor,
    const IntRect& relative_compositing_bounds) {
  if (owning_layer_.HasTransformRelatedProperty()) {
    const LayoutRect border_box =
        ToLayoutBox(GetLayoutObject()).BorderBoxRect();

    // Get layout bounds in the coords of compositingContainer to match
    // relativeCompositingBounds.
    IntRect layer_bounds = PixelSnappedIntRect(
        ToLayoutPoint(owning_layer_.SubpixelAccumulation()), border_box.Size());
    layer_bounds.MoveBy(snapped_offset_from_composited_ancestor);

    // Update properties that depend on layer dimensions
    FloatPoint3D transform_origin =
        ComputeTransformOrigin(IntRect(IntPoint(), layer_bounds.Size()));

    // |transformOrigin| is in the local space of this layer.
    // layerBounds - relativeCompositingBounds converts to the space of the
    // compositing bounds relative to the composited ancestor. This does not
    // apply to the z direction, since the page is 2D.
    FloatPoint3D composited_transform_origin(
        layer_bounds.X() - relative_compositing_bounds.X() +
            transform_origin.X(),
        layer_bounds.Y() - relative_compositing_bounds.Y() +
            transform_origin.Y(),
        transform_origin.Z());
    graphics_layer_->SetTransformOrigin(composited_transform_origin);
  } else {
    FloatPoint3D composited_transform_origin(
        relative_compositing_bounds.Width() * 0.5f,
        relative_compositing_bounds.Height() * 0.5f, 0.f);
    graphics_layer_->SetTransformOrigin(composited_transform_origin);
  }
}

void CompositedLayerMapping::UpdateScrollingLayerGeometry(
    const IntRect& local_compositing_bounds) {
  if (!scrolling_layer_)
    return;

  DCHECK(scrolling_contents_layer_);
  LayoutBox& layout_box = ToLayoutBox(GetLayoutObject());
  IntRect overflow_clip_rect =
      PixelSnappedIntRect(layout_box.OverflowClipRect(LayoutPoint()));

  // When a m_childTransformLayer exists, local content offsets for the
  // m_scrollingLayer have already been applied. Otherwise, we apply them here.
  IntSize local_content_offset(0, 0);
  if (!child_transform_layer_) {
    local_content_offset =
        RoundedIntPoint(owning_layer_.SubpixelAccumulation()) -
        local_compositing_bounds.Location();
  }
  scrolling_layer_->SetPosition(
      FloatPoint(overflow_clip_rect.Location() + local_content_offset));
  scrolling_layer_->SetSize(FloatSize(overflow_clip_rect.Size()));

  IntSize old_scrolling_layer_offset =
      scrolling_layer_->OffsetFromLayoutObject();
  scrolling_layer_->SetOffsetFromLayoutObject(
      -ToIntSize(overflow_clip_rect.Location()));

  if (child_clipping_mask_layer_ && !GetLayoutObject().Style()->ClipPath()) {
    child_clipping_mask_layer_->SetPosition(scrolling_layer_->GetPosition());
    child_clipping_mask_layer_->SetSize(scrolling_layer_->Size());
    child_clipping_mask_layer_->SetOffsetFromLayoutObject(
        ToIntSize(overflow_clip_rect.Location()));
  }

  bool overflow_clip_rect_offset_changed =
      old_scrolling_layer_offset != scrolling_layer_->OffsetFromLayoutObject();

  IntSize scroll_size(layout_box.PixelSnappedScrollWidth(),
                      layout_box.PixelSnappedScrollHeight());
  if (overflow_clip_rect_offset_changed)
    scrolling_contents_layer_->SetNeedsDisplay();

  FloatPoint scroll_position =
      owning_layer_.GetScrollableArea()->ScrollPosition();
  DoubleSize scrolling_contents_offset(
      overflow_clip_rect.Location().X() - scroll_position.X(),
      overflow_clip_rect.Location().Y() - scroll_position.Y());
  // The scroll offset change is compared using floating point so that
  // fractional scroll offset change can be propagated to compositor.
  if (scrolling_contents_offset != scrolling_contents_offset_ ||
      scroll_size != scrolling_contents_layer_->Size()) {
    bool coordinator_handles_offset =
        Compositor()->ScrollingLayerDidChange(&owning_layer_);
    scrolling_contents_layer_->SetPosition(
        coordinator_handles_offset ? FloatPoint()
                                   : FloatPoint(-ToFloatSize(scroll_position)));
  }
  scrolling_contents_offset_ = scrolling_contents_offset;

  scrolling_contents_layer_->SetSize(FloatSize(scroll_size));

  IntPoint scrolling_contents_layer_offset_from_layout_object;
  if (PaintLayerScrollableArea* scrollable_area =
          owning_layer_.GetScrollableArea()) {
    scrolling_contents_layer_offset_from_layout_object =
        -scrollable_area->ScrollOrigin();
  }
  scrolling_contents_layer_offset_from_layout_object.MoveBy(
      overflow_clip_rect.Location());

  scrolling_contents_layer_->SetOffsetDoubleFromLayoutObject(
      ToIntSize(scrolling_contents_layer_offset_from_layout_object),
      GraphicsLayer::kDontSetNeedsDisplay);

  if (foreground_layer_) {
    if (foreground_layer_->Size() != scrolling_contents_layer_->Size())
      foreground_layer_->SetSize(scrolling_contents_layer_->Size());
    foreground_layer_->SetNeedsDisplay();
    foreground_layer_->SetOffsetFromLayoutObject(
        scrolling_contents_layer_->OffsetFromLayoutObject());
  }
}

void CompositedLayerMapping::UpdateChildClippingMaskLayerGeometry() {
  if (!child_clipping_mask_layer_ || !GetLayoutObject().Style()->ClipPath() ||
      !GetLayoutObject().IsBox())
    return;
  LayoutBox& layout_box = ToLayoutBox(GetLayoutObject());
  IntRect padding_box = EnclosingIntRect(layout_box.PaddingBoxRect());

  child_clipping_mask_layer_->SetPosition(graphics_layer_->GetPosition());
  child_clipping_mask_layer_->SetSize(graphics_layer_->Size());
  child_clipping_mask_layer_->SetOffsetFromLayoutObject(
      ToIntSize(padding_box.Location()));

  // NOTE: also some stuff happening in updateChildContainmentLayerGeometry().
}

void CompositedLayerMapping::UpdateForegroundLayerGeometry(
    const FloatSize& relative_compositing_bounds_size,
    const IntRect& clipping_box) {
  if (!foreground_layer_)
    return;

  FloatSize foreground_size = relative_compositing_bounds_size;
  IntSize foreground_offset = graphics_layer_->OffsetFromLayoutObject();
  foreground_layer_->SetPosition(FloatPoint());

  if (HasClippingLayer()) {
    // If we have a clipping layer (which clips descendants), then the
    // foreground layer is a child of it, so that it gets correctly sorted with
    // children. In that case, position relative to the clipping layer.
    foreground_size = FloatSize(clipping_box.Size());
    foreground_offset = ToIntSize(clipping_box.Location());
  } else if (child_transform_layer_) {
    // Things are different if we have a child transform layer rather
    // than a clipping layer. In this case, we want to actually change
    // the position of the layer (to compensate for our ancestor
    // compositing PaintLayer's position) rather than leave the position the
    // same and use offset-from-layoutObject + size to describe a clipped
    // "window" onto the clipped layer.

    foreground_layer_->SetPosition(-child_transform_layer_->GetPosition());
  }

  if (foreground_size != foreground_layer_->Size()) {
    foreground_layer_->SetSize(foreground_size);
    foreground_layer_->SetNeedsDisplay();
  }
  foreground_layer_->SetOffsetFromLayoutObject(foreground_offset);

  // NOTE: there is some more configuring going on in
  // updateScrollingLayerGeometry().
}

void CompositedLayerMapping::UpdateBackgroundLayerGeometry(
    const FloatSize& relative_compositing_bounds_size) {
  if (!background_layer_)
    return;

  FloatSize background_size = relative_compositing_bounds_size;
  if (BackgroundLayerPaintsFixedRootBackground()) {
    FrameView* frame_view = ToLayoutView(GetLayoutObject()).GetFrameView();
    background_size = FloatSize(frame_view->VisibleContentRect().Size());
  }
  background_layer_->SetPosition(FloatPoint());
  if (background_size != background_layer_->Size()) {
    background_layer_->SetSize(background_size);
    background_layer_->SetNeedsDisplay();
  }
  background_layer_->SetOffsetFromLayoutObject(
      graphics_layer_->OffsetFromLayoutObject());
}

void CompositedLayerMapping::UpdateDecorationOutlineLayerGeometry(
    const FloatSize& relative_compositing_bounds_size) {
  if (!decoration_outline_layer_)
    return;
  FloatSize decoration_size = relative_compositing_bounds_size;
  decoration_outline_layer_->SetPosition(FloatPoint());
  if (decoration_size != decoration_outline_layer_->Size()) {
    decoration_outline_layer_->SetSize(decoration_size);
    decoration_outline_layer_->SetNeedsDisplay();
  }
  decoration_outline_layer_->SetOffsetFromLayoutObject(
      graphics_layer_->OffsetFromLayoutObject());
}

void CompositedLayerMapping::UpdateInternalHierarchy() {
  // m_foregroundLayer has to be inserted in the correct order with child
  // layers, so it's not inserted here.
  if (ancestor_clipping_layer_)
    ancestor_clipping_layer_->RemoveAllChildren();

  graphics_layer_->RemoveFromParent();

  if (ancestor_clipping_layer_)
    ancestor_clipping_layer_->AddChild(graphics_layer_.get());

  // Layer to which children should be attached as we build the hierarchy.
  GraphicsLayer* bottom_layer = graphics_layer_.get();
  auto update_bottom_layer = [&bottom_layer](GraphicsLayer* layer) {
    if (layer) {
      bottom_layer->AddChild(layer);
      bottom_layer = layer;
    }
  };

  update_bottom_layer(child_transform_layer_.get());
  update_bottom_layer(child_containment_layer_.get());
  update_bottom_layer(scrolling_layer_.get());

  // Now constructing the subtree for the overflow controls.
  bottom_layer = graphics_layer_.get();
  // TODO(pdr): Ensure painting uses the correct GraphicsLayer when root layer
  // scrolls is enabled.  crbug.com/638719
  if (is_main_frame_layout_view_layer_ &&
      !RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    bottom_layer = GetLayoutObject()
                       .GetFrame()
                       ->GetPage()
                       ->GetVisualViewport()
                       .ContainerLayer();
  }
  update_bottom_layer(overflow_controls_ancestor_clipping_layer_.get());
  update_bottom_layer(overflow_controls_host_layer_.get());
  if (layer_for_horizontal_scrollbar_)
    overflow_controls_host_layer_->AddChild(
        layer_for_horizontal_scrollbar_.get());
  if (layer_for_vertical_scrollbar_)
    overflow_controls_host_layer_->AddChild(
        layer_for_vertical_scrollbar_.get());
  if (layer_for_scroll_corner_)
    overflow_controls_host_layer_->AddChild(layer_for_scroll_corner_.get());

  // Now add the DecorationOutlineLayer as a subtree to GraphicsLayer
  if (decoration_outline_layer_.get())
    graphics_layer_->AddChild(decoration_outline_layer_.get());

  // The squashing containment layer, if it exists, becomes a no-op parent.
  if (squashing_layer_) {
    DCHECK((ancestor_clipping_layer_ && !squashing_containment_layer_) ||
           (!ancestor_clipping_layer_ && squashing_containment_layer_));

    if (squashing_containment_layer_) {
      squashing_containment_layer_->RemoveAllChildren();
      squashing_containment_layer_->AddChild(graphics_layer_.get());
      squashing_containment_layer_->AddChild(squashing_layer_.get());
    } else {
      // The ancestor clipping layer is already set up and has m_graphicsLayer
      // under it.
      ancestor_clipping_layer_->AddChild(squashing_layer_.get());
    }
  }
}

void CompositedLayerMapping::UpdatePaintingPhases() {
  graphics_layer_->SetPaintingPhase(PaintingPhaseForPrimaryLayer());
  if (scrolling_contents_layer_) {
    GraphicsLayerPaintingPhase paint_phase =
        kGraphicsLayerPaintOverflowContents |
        kGraphicsLayerPaintCompositedScroll;
    if (!foreground_layer_)
      paint_phase |= kGraphicsLayerPaintForeground;
    scrolling_contents_layer_->SetPaintingPhase(paint_phase);
  }
  if (foreground_layer_) {
    GraphicsLayerPaintingPhase paint_phase = kGraphicsLayerPaintForeground;
    if (scrolling_contents_layer_)
      paint_phase |= kGraphicsLayerPaintOverflowContents;
    foreground_layer_->SetPaintingPhase(paint_phase);
  }
}

void CompositedLayerMapping::UpdateContentsRect() {
  graphics_layer_->SetContentsRect(PixelSnappedIntRect(ContentsBox()));
}

void CompositedLayerMapping::UpdateContentsOffsetInCompositingLayer(
    const IntPoint& snapped_offset_from_composited_ancestor,
    const IntPoint& graphics_layer_parent_location) {
  // m_graphicsLayer is positioned relative to our compositing ancestor
  // PaintLayer, but it's not positioned at the origin of m_owningLayer, it's
  // offset by m_contentBounds.location(). This is what
  // contentOffsetInCompositingLayer is meant to capture, roughly speaking
  // (ignoring rounding and subpixel accumulation).
  //
  // Our ancestor graphics layers in this CLM (m_graphicsLayer and potentially
  // m_ancestorClippingLayer) have pixel snapped, so if we don't adjust this
  // offset, we'll see accumulated rounding errors due to that snapping.
  //
  // In order to ensure that we account for this rounding, we compute
  // contentsOffsetInCompositingLayer in a somewhat roundabout way.
  //
  // our position = (desired position) - (inherited graphics layer offset).
  //
  // Precisely,
  // Offset = snappedOffsetFromCompositedAncestor -
  //          offsetDueToAncestorGraphicsLayers (See code below)
  //        = snappedOffsetFromCompositedAncestor -
  //          (m_graphicsLayer->position() + graphicsLayerParentLocation)
  //        = snappedOffsetFromCompositedAncestor -
  //          (relativeCompositingBounds.location() -
  //              graphicsLayerParentLocation +
  //              graphicsLayerParentLocation)
  //          (See updateMainGraphicsLayerGeometry)
  //        = snappedOffsetFromCompositedAncestor -
  //          relativeCompositingBounds.location()
  //        = snappedOffsetFromCompositedAncestor -
  //          (pixelSnappedIntRect(contentBounds.location()) +
  //              snappedOffsetFromCompositedAncestor)
  //          (See computeBoundsOfOwningLayer)
  //      = -pixelSnappedIntRect(contentBounds.location())
  //
  // As you can see, we've ended up at the same spot
  // (-contentBounds.location()), but by subtracting off our ancestor graphics
  // layers positions, we can be sure we've accounted correctly for any pixel
  // snapping due to ancestor graphics layers.
  //
  // And drawing of composited children takes into account the subpixel
  // accumulation of this CLM already (through its own
  // graphicsLayerParentLocation it appears).
  FloatPoint offset_due_to_ancestor_graphics_layers =
      graphics_layer_->GetPosition() + graphics_layer_parent_location;
  content_offset_in_compositing_layer_ =
      LayoutSize(snapped_offset_from_composited_ancestor -
                 offset_due_to_ancestor_graphics_layers);
  content_offset_in_compositing_layer_dirty_ = false;
}

void CompositedLayerMapping::UpdateDrawsContent() {
  bool in_overlay_fullscreen_video = false;
  if (GetLayoutObject().IsVideo()) {
    HTMLVideoElement* video_element =
        toHTMLVideoElement(GetLayoutObject().GetNode());
    if (video_element->IsFullscreen() &&
        video_element->UsesOverlayFullscreenVideo())
      in_overlay_fullscreen_video = true;
  }
  bool has_painted_content =
      in_overlay_fullscreen_video ? false : ContainsPaintedContent();
  graphics_layer_->SetDrawsContent(has_painted_content);

  if (scrolling_layer_) {
    // m_scrollingLayer never has backing store.
    // m_scrollingContentsLayer only needs backing store if the scrolled
    // contents need to paint.
    scrolling_contents_are_empty_ =
        !owning_layer_.HasVisibleContent() ||
        !(GetLayoutObject().StyleRef().HasBackground() ||
          GetLayoutObject().HasBackdropFilter() || PaintsChildren());
    scrolling_contents_layer_->SetDrawsContent(!scrolling_contents_are_empty_);
  }

  draws_background_onto_content_layer_ = false;

  if (has_painted_content && IsCompositedCanvas(GetLayoutObject())) {
    CanvasRenderingContext* context =
        toHTMLCanvasElement(GetLayoutObject().GetNode())->RenderingContext();
    // Content layer may be null if context is lost.
    if (WebLayer* content_layer = context->PlatformLayer()) {
      Color bg_color(Color::kTransparent);
      if (ContentLayerSupportsDirectBackgroundComposition(GetLayoutObject())) {
        bg_color = LayoutObjectBackgroundColor();
        has_painted_content = false;
        draws_background_onto_content_layer_ = true;
      }
      content_layer->SetBackgroundColor(bg_color.Rgb());
    }
  }

  // FIXME: we could refine this to only allocate backings for one of these
  // layers if possible.
  if (foreground_layer_)
    foreground_layer_->SetDrawsContent(has_painted_content);

  // TODO(yigu): The background should no longer setDrawsContent(true) if we
  // only have an outline and we are drawing the outline into the decoration
  // layer (i.e. if there is nothing actually drawn into the
  // background anymore.)
  // "hasPaintedContent" should be calculated in a way that does not take the
  // outline into consideration.
  if (background_layer_)
    background_layer_->SetDrawsContent(has_painted_content);

  if (decoration_outline_layer_)
    decoration_outline_layer_->SetDrawsContent(true);

  if (ancestor_clipping_mask_layer_)
    ancestor_clipping_mask_layer_->SetDrawsContent(true);

  if (mask_layer_)
    mask_layer_->SetDrawsContent(true);

  if (child_clipping_mask_layer_)
    child_clipping_mask_layer_->SetDrawsContent(true);
}

void CompositedLayerMapping::UpdateChildrenTransform() {
  if (GraphicsLayer* child_transform_layer = this->ChildTransformLayer()) {
    child_transform_layer->SetTransform(OwningLayer().PerspectiveTransform());
    child_transform_layer->SetTransformOrigin(
        OwningLayer().PerspectiveOrigin());
  }

  UpdateShouldFlattenTransform();
}

// Return true if the layers changed.
bool CompositedLayerMapping::UpdateClippingLayers(
    bool needs_ancestor_clip,
    bool needs_ancestor_clipping_mask,
    bool needs_descendant_clip) {
  bool layers_changed = false;

  if (needs_ancestor_clip) {
    if (!ancestor_clipping_layer_) {
      ancestor_clipping_layer_ =
          CreateGraphicsLayer(kCompositingReasonLayerForAncestorClip);
      ancestor_clipping_layer_->SetMasksToBounds(true);
      ancestor_clipping_layer_->SetShouldFlattenTransform(false);
      layers_changed = true;
    }
  } else if (ancestor_clipping_layer_) {
    if (ancestor_clipping_mask_layer_) {
      ancestor_clipping_mask_layer_->RemoveFromParent();
      ancestor_clipping_mask_layer_ = nullptr;
    }
    ancestor_clipping_layer_->RemoveFromParent();
    ancestor_clipping_layer_ = nullptr;
    layers_changed = true;
  }

  if (needs_ancestor_clipping_mask) {
    DCHECK(ancestor_clipping_layer_);
    if (!ancestor_clipping_mask_layer_) {
      ancestor_clipping_mask_layer_ =
          CreateGraphicsLayer(kCompositingReasonLayerForAncestorClippingMask);
      ancestor_clipping_mask_layer_->SetPaintingPhase(
          kGraphicsLayerPaintAncestorClippingMask);
      ancestor_clipping_layer_->SetMaskLayer(
          ancestor_clipping_mask_layer_.get());
      layers_changed = true;
    }
  } else if (ancestor_clipping_mask_layer_) {
    ancestor_clipping_mask_layer_->RemoveFromParent();
    ancestor_clipping_mask_layer_ = nullptr;
    ancestor_clipping_layer_->SetMaskLayer(nullptr);
    layers_changed = true;
  }

  if (needs_descendant_clip) {
    // We don't need a child containment layer if we're the main frame layout
    // view layer. It's redundant as the frame clip above us will handle this
    // clipping.
    if (!child_containment_layer_ && !is_main_frame_layout_view_layer_) {
      child_containment_layer_ =
          CreateGraphicsLayer(kCompositingReasonLayerForDescendantClip);
      child_containment_layer_->SetMasksToBounds(true);
      layers_changed = true;
    }
  } else if (HasClippingLayer()) {
    child_containment_layer_->RemoveFromParent();
    child_containment_layer_ = nullptr;
    layers_changed = true;
  }

  return layers_changed;
}

bool CompositedLayerMapping::UpdateChildTransformLayer(
    bool needs_child_transform_layer) {
  bool layers_changed = false;

  if (needs_child_transform_layer) {
    if (!child_transform_layer_) {
      child_transform_layer_ =
          CreateGraphicsLayer(kCompositingReasonLayerForPerspective);
      child_transform_layer_->SetDrawsContent(false);
      layers_changed = true;
    }
  } else if (child_transform_layer_) {
    child_transform_layer_->RemoveFromParent();
    child_transform_layer_ = nullptr;
    layers_changed = true;
  }

  return layers_changed;
}

void CompositedLayerMapping::SetBackgroundLayerPaintsFixedRootBackground(
    bool background_layer_paints_fixed_root_background) {
  background_layer_paints_fixed_root_background_ =
      background_layer_paints_fixed_root_background;
}

bool CompositedLayerMapping::ToggleScrollbarLayerIfNeeded(
    std::unique_ptr<GraphicsLayer>& layer,
    bool needs_layer,
    CompositingReasons reason) {
  if (needs_layer == !!layer)
    return false;
  layer = needs_layer ? CreateGraphicsLayer(reason) : nullptr;

  if (PaintLayerScrollableArea* scrollable_area =
          owning_layer_.GetScrollableArea()) {
    if (ScrollingCoordinator* scrolling_coordinator =
            owning_layer_.GetScrollingCoordinator()) {
      if (reason == kCompositingReasonLayerForHorizontalScrollbar)
        scrolling_coordinator->ScrollableAreaScrollbarLayerDidChange(
            scrollable_area, kHorizontalScrollbar);
      else if (reason == kCompositingReasonLayerForVerticalScrollbar)
        scrolling_coordinator->ScrollableAreaScrollbarLayerDidChange(
            scrollable_area, kVerticalScrollbar);
    }
  }
  return true;
}

bool CompositedLayerMapping::UpdateOverflowControlsLayers(
    bool needs_horizontal_scrollbar_layer,
    bool needs_vertical_scrollbar_layer,
    bool needs_scroll_corner_layer,
    bool needs_ancestor_clip) {
  if (PaintLayerScrollableArea* scrollable_area =
          owning_layer_.GetScrollableArea()) {
    // If the scrollable area is marked as needing a new scrollbar layer,
    // destroy the layer now so that it will be created again below.
    if (layer_for_horizontal_scrollbar_ && needs_horizontal_scrollbar_layer &&
        scrollable_area->ShouldRebuildHorizontalScrollbarLayer())
      ToggleScrollbarLayerIfNeeded(
          layer_for_horizontal_scrollbar_, false,
          kCompositingReasonLayerForHorizontalScrollbar);
    if (layer_for_vertical_scrollbar_ && needs_vertical_scrollbar_layer &&
        scrollable_area->ShouldRebuildVerticalScrollbarLayer())
      ToggleScrollbarLayerIfNeeded(layer_for_vertical_scrollbar_, false,
                                   kCompositingReasonLayerForVerticalScrollbar);
    scrollable_area->ResetRebuildScrollbarLayerFlags();

    if (scrolling_contents_layer_ &&
        scrollable_area->NeedsShowScrollbarLayers()) {
      scrolling_contents_layer_->PlatformLayer()->ShowScrollbars();
      scrollable_area->DidShowScrollbarLayers();
    }
  }

  // If the subtree is invisible, we don't actually need scrollbar layers.
  // Only do this check if at least one of the bits is currently true.
  // This is important because this method is called during the destructor
  // of CompositedLayerMapping, which may happen during style recalc,
  // and therefore visible content status may be invalid.
  if (needs_horizontal_scrollbar_layer || needs_vertical_scrollbar_layer ||
      needs_scroll_corner_layer) {
    bool invisible = owning_layer_.SubtreeIsInvisible();
    needs_horizontal_scrollbar_layer &= !invisible;
    needs_vertical_scrollbar_layer &= !invisible;
    needs_scroll_corner_layer &= !invisible;
  }

  bool horizontal_scrollbar_layer_changed = ToggleScrollbarLayerIfNeeded(
      layer_for_horizontal_scrollbar_, needs_horizontal_scrollbar_layer,
      kCompositingReasonLayerForHorizontalScrollbar);
  bool vertical_scrollbar_layer_changed = ToggleScrollbarLayerIfNeeded(
      layer_for_vertical_scrollbar_, needs_vertical_scrollbar_layer,
      kCompositingReasonLayerForVerticalScrollbar);
  bool scroll_corner_layer_changed = ToggleScrollbarLayerIfNeeded(
      layer_for_scroll_corner_, needs_scroll_corner_layer,
      kCompositingReasonLayerForScrollCorner);

  bool needs_overflow_controls_host_layer = needs_horizontal_scrollbar_layer ||
                                            needs_vertical_scrollbar_layer ||
                                            needs_scroll_corner_layer;
  ToggleScrollbarLayerIfNeeded(overflow_controls_host_layer_,
                               needs_overflow_controls_host_layer,
                               kCompositingReasonLayerForOverflowControlsHost);
  bool needs_overflow_ancestor_clip_layer =
      needs_overflow_controls_host_layer && needs_ancestor_clip;
  ToggleScrollbarLayerIfNeeded(overflow_controls_ancestor_clipping_layer_,
                               needs_overflow_ancestor_clip_layer,
                               kCompositingReasonLayerForOverflowControlsHost);

  return horizontal_scrollbar_layer_changed ||
         vertical_scrollbar_layer_changed || scroll_corner_layer_changed;
}

void CompositedLayerMapping::PositionOverflowControlsLayers() {
  if (GraphicsLayer* layer = LayerForHorizontalScrollbar()) {
    Scrollbar* h_bar = owning_layer_.GetScrollableArea()->HorizontalScrollbar();
    if (h_bar) {
      layer->SetPosition(h_bar->FrameRect().Location());
      layer->SetSize(FloatSize(h_bar->FrameRect().Size()));
      if (layer->HasContentsLayer())
        layer->SetContentsRect(IntRect(IntPoint(), h_bar->FrameRect().Size()));
    }
    layer->SetDrawsContent(h_bar && !layer->HasContentsLayer());
  }

  if (GraphicsLayer* layer = LayerForVerticalScrollbar()) {
    Scrollbar* v_bar = owning_layer_.GetScrollableArea()->VerticalScrollbar();
    if (v_bar) {
      layer->SetPosition(v_bar->FrameRect().Location());
      layer->SetSize(FloatSize(v_bar->FrameRect().Size()));
      if (layer->HasContentsLayer())
        layer->SetContentsRect(IntRect(IntPoint(), v_bar->FrameRect().Size()));
    }
    layer->SetDrawsContent(v_bar && !layer->HasContentsLayer());
  }

  if (GraphicsLayer* layer = LayerForScrollCorner()) {
    const IntRect& scroll_corner_and_resizer =
        owning_layer_.GetScrollableArea()->ScrollCornerAndResizerRect();
    layer->SetPosition(FloatPoint(scroll_corner_and_resizer.Location()));
    layer->SetSize(FloatSize(scroll_corner_and_resizer.Size()));
    layer->SetDrawsContent(!scroll_corner_and_resizer.IsEmpty());
  }
}

enum ApplyToGraphicsLayersModeFlags {
  kApplyToLayersAffectedByPreserve3D = (1 << 0),
  kApplyToSquashingLayer = (1 << 1),
  kApplyToScrollbarLayers = (1 << 2),
  kApplyToBackgroundLayer = (1 << 3),
  kApplyToMaskLayers = (1 << 4),
  kApplyToContentLayers = (1 << 5),
  kApplyToChildContainingLayers =
      (1 << 6),  // layers between m_graphicsLayer and children
  kApplyToNonScrollingContentLayers = (1 << 7),
  kApplyToScrollingContentLayers = (1 << 8),
  kApplyToDecorationOutlineLayer = (1 << 9),
  kApplyToAllGraphicsLayers =
      (kApplyToSquashingLayer | kApplyToScrollbarLayers |
       kApplyToBackgroundLayer |
       kApplyToMaskLayers |
       kApplyToLayersAffectedByPreserve3D |
       kApplyToContentLayers |
       kApplyToScrollingContentLayers |
       kApplyToDecorationOutlineLayer)
};
typedef unsigned ApplyToGraphicsLayersMode;

template <typename Func>
static void ApplyToGraphicsLayers(const CompositedLayerMapping* mapping,
                                  const Func& f,
                                  ApplyToGraphicsLayersMode mode) {
  DCHECK(mode);

  if ((mode & kApplyToLayersAffectedByPreserve3D) &&
      mapping->ChildTransformLayer())
    f(mapping->ChildTransformLayer());
  if (((mode & kApplyToLayersAffectedByPreserve3D) ||
       (mode & kApplyToContentLayers) ||
       (mode & kApplyToNonScrollingContentLayers)) &&
      mapping->MainGraphicsLayer())
    f(mapping->MainGraphicsLayer());
  if (((mode & kApplyToLayersAffectedByPreserve3D) ||
       (mode & kApplyToChildContainingLayers)) &&
      mapping->ClippingLayer())
    f(mapping->ClippingLayer());
  if (((mode & kApplyToLayersAffectedByPreserve3D) ||
       (mode & kApplyToChildContainingLayers)) &&
      mapping->ScrollingLayer())
    f(mapping->ScrollingLayer());
  if (((mode & kApplyToLayersAffectedByPreserve3D) ||
       (mode & kApplyToContentLayers) ||
       (mode & kApplyToChildContainingLayers) ||
       (mode & kApplyToScrollingContentLayers)) &&
      mapping->ScrollingContentsLayer())
    f(mapping->ScrollingContentsLayer());
  if (((mode & kApplyToLayersAffectedByPreserve3D) ||
       (mode & kApplyToContentLayers) ||
       (mode & kApplyToScrollingContentLayers)) &&
      mapping->ForegroundLayer())
    f(mapping->ForegroundLayer());

  if ((mode & kApplyToChildContainingLayers) && mapping->ChildTransformLayer())
    f(mapping->ChildTransformLayer());

  if ((mode & kApplyToSquashingLayer) && mapping->SquashingLayer())
    f(mapping->SquashingLayer());

  if (((mode & kApplyToMaskLayers) || (mode & kApplyToContentLayers) ||
       (mode & kApplyToNonScrollingContentLayers)) &&
      mapping->MaskLayer())
    f(mapping->MaskLayer());
  if (((mode & kApplyToMaskLayers) || (mode & kApplyToContentLayers) ||
       (mode & kApplyToNonScrollingContentLayers)) &&
      mapping->ChildClippingMaskLayer())
    f(mapping->ChildClippingMaskLayer());
  if (((mode & kApplyToMaskLayers) || (mode & kApplyToContentLayers) ||
       (mode & kApplyToNonScrollingContentLayers)) &&
      mapping->AncestorClippingMaskLayer())
    f(mapping->AncestorClippingMaskLayer());

  if (((mode & kApplyToBackgroundLayer) || (mode & kApplyToContentLayers) ||
       (mode & kApplyToNonScrollingContentLayers)) &&
      mapping->BackgroundLayer())
    f(mapping->BackgroundLayer());

  if ((mode & kApplyToScrollbarLayers) &&
      mapping->LayerForHorizontalScrollbar())
    f(mapping->LayerForHorizontalScrollbar());
  if ((mode & kApplyToScrollbarLayers) && mapping->LayerForVerticalScrollbar())
    f(mapping->LayerForVerticalScrollbar());
  if ((mode & kApplyToScrollbarLayers) && mapping->LayerForScrollCorner())
    f(mapping->LayerForScrollCorner());

  if (((mode & kApplyToDecorationOutlineLayer) ||
       (mode & kApplyToNonScrollingContentLayers)) &&
      mapping->DecorationOutlineLayer())
    f(mapping->DecorationOutlineLayer());
}

struct UpdateRenderingContextFunctor {
  void operator()(GraphicsLayer* layer) const {
    layer->SetRenderingContext(rendering_context);
  }
  int rendering_context;
};

void CompositedLayerMapping::UpdateRenderingContext() {
  // All layers but the squashing layer (which contains 'alien' content) should
  // be included in this rendering context.
  int id = 0;

  // NB, it is illegal at this point to query an ancestor's compositing state.
  // Some compositing reasons depend on the compositing state of ancestors. So
  // if we want a rendering context id for the context root, we cannot ask for
  // the id of its associated WebLayer now; it may not have one yet. We could do
  // a second pass after doing the compositing updates to get these ids, but
  // this would actually be harmful. We do not want to attach any semantic
  // meaning to the context id other than the fact that they group a number of
  // layers together for the sake of 3d sorting. So instead we will ask the
  // compositor to vend us an arbitrary, but consistent id.
  if (PaintLayer* root = owning_layer_.RenderingContextRoot()) {
    if (Node* node = root->GetLayoutObject().GetNode())
      id = static_cast<int>(PtrHash<Node>::GetHash(node));
  }

  UpdateRenderingContextFunctor functor = {id};
  ApplyToGraphicsLayers<UpdateRenderingContextFunctor>(
      this, functor, kApplyToAllGraphicsLayers);
}

struct UpdateShouldFlattenTransformFunctor {
  void operator()(GraphicsLayer* layer) const {
    layer->SetShouldFlattenTransform(should_flatten);
  }
  bool should_flatten;
};

void CompositedLayerMapping::UpdateShouldFlattenTransform() {
  // All CLM-managed layers that could affect a descendant layer should update
  // their should-flatten-transform value (the other layers' transforms don't
  // matter here).
  UpdateShouldFlattenTransformFunctor functor = {
      !owning_layer_.ShouldPreserve3D()};
  ApplyToGraphicsLayersMode mode = kApplyToLayersAffectedByPreserve3D;
  ApplyToGraphicsLayers(this, functor, mode);

  // Note, if we apply perspective, we have to set should flatten differently
  // so that the transform propagates to child layers correctly.
  if (HasChildTransformLayer()) {
    ApplyToGraphicsLayers(
        this,
        [](GraphicsLayer* layer) { layer->SetShouldFlattenTransform(false); },
        kApplyToChildContainingLayers);
  }

  // Regardless, mark the graphics layer, scrolling layer and scrolling block
  // selection layer (if they exist) as not flattening. Having them flatten
  // causes unclipped render surfaces which cause bugs.
  // http://crbug.com/521768
  if (HasScrollingLayer()) {
    graphics_layer_->SetShouldFlattenTransform(false);
    scrolling_layer_->SetShouldFlattenTransform(false);
  }
}

struct AnimatingData {
  STACK_ALLOCATED()
  Persistent<Node> owning_node = nullptr;
  Persistent<Element> animating_element = nullptr;
  const ComputedStyle* animating_style = nullptr;
};

void GetAnimatingData(PaintLayer& paint_layer, AnimatingData& data) {
  if (!RuntimeEnabledFeatures::compositorWorkerEnabled())
    return;

  data.owning_node = paint_layer.GetLayoutObject().GetNode();

  if (!data.owning_node)
    return;

  Document& document = data.owning_node->GetDocument();
  Element* scrolling_element = document.ScrollingElementNoLayout();
  if (data.owning_node->IsElementNode() &&
      (!RuntimeEnabledFeatures::rootLayerScrollingEnabled() ||
       data.owning_node != scrolling_element)) {
    data.animating_element = ToElement(data.owning_node);
    data.animating_style = paint_layer.GetLayoutObject().Style();
  } else if (data.owning_node->IsDocumentNode() &&
             RuntimeEnabledFeatures::rootLayerScrollingEnabled()) {
    data.owning_node = data.animating_element = scrolling_element;
    if (scrolling_element && scrolling_element->GetLayoutObject())
      data.animating_style = scrolling_element->GetLayoutObject()->Style();
  }
}

// Some background on when you receive an element id or mutable properties.
//
// element id:
//   If you have a compositor proxy, an animation, or you're a scroller (and
//   might impl animate).
//
// mutable properties:
//   Only if you have a compositor proxy.
//
// The element id for the scroll layers is assigned when they're constructed,
// since this is unconditional. However, the element id for the primary layer as
// well as the mutable properties for all layers may change according to the
// rules above so we update those values here.
void CompositedLayerMapping::UpdateElementIdAndCompositorMutableProperties() {
  uint32_t primary_mutable_properties = CompositorMutableProperty::kNone;
  uint32_t scroll_mutable_properties = CompositorMutableProperty::kNone;

  AnimatingData data;
  GetAnimatingData(owning_layer_, data);

  CompositorElementId element_id;
  if (data.animating_style && data.animating_style->HasCompositorProxy()) {
    // Compositor proxy element ids cannot be based on PaintLayers, since
    // those are not kept alive by script across frames.
    element_id = CompositorElementIdFromDOMNodeId(
        DOMNodeIds::IdForNode(data.owning_node),
        CompositorElementIdNamespace::kPrimaryCompositorProxy);

    uint32_t compositor_mutable_properties =
        data.animating_element->CompositorMutableProperties();
    primary_mutable_properties = (CompositorMutableProperty::kOpacity |
                                  CompositorMutableProperty::kTransform) &
                                 compositor_mutable_properties;
    scroll_mutable_properties = (CompositorMutableProperty::kScrollLeft |
                                 CompositorMutableProperty::kScrollTop) &
                                compositor_mutable_properties;
  } else {
    element_id = CompositorElementIdFromPaintLayerId(
        owning_layer_.UniqueId(), CompositorElementIdNamespace::kPrimary);
  }

  graphics_layer_->SetElementId(element_id);
  graphics_layer_->SetCompositorMutableProperties(primary_mutable_properties);

  // We always set the elementId for m_scrollingContentsLayer since it can be
  // animated for smooth scrolling, so we don't need to set it conditionally
  // here.
  if (scrolling_contents_layer_.get())
    scrolling_contents_layer_->SetCompositorMutableProperties(
        scroll_mutable_properties);
}

bool CompositedLayerMapping::UpdateForegroundLayer(
    bool needs_foreground_layer) {
  bool layer_changed = false;
  if (needs_foreground_layer) {
    if (!foreground_layer_) {
      foreground_layer_ =
          CreateGraphicsLayer(kCompositingReasonLayerForForeground);
      layer_changed = true;
    }
  } else if (foreground_layer_) {
    foreground_layer_->RemoveFromParent();
    foreground_layer_ = nullptr;
    layer_changed = true;
  }

  return layer_changed;
}

bool CompositedLayerMapping::UpdateBackgroundLayer(
    bool needs_background_layer) {
  bool layer_changed = false;
  if (needs_background_layer) {
    if (!background_layer_) {
      background_layer_ =
          CreateGraphicsLayer(kCompositingReasonLayerForBackground);
      background_layer_->SetTransformOrigin(FloatPoint3D());
      background_layer_->SetPaintingPhase(kGraphicsLayerPaintBackground);
      layer_changed = true;
    }
  } else {
    if (background_layer_) {
      background_layer_->RemoveFromParent();
      background_layer_ = nullptr;
      layer_changed = true;
    }
  }

  if (layer_changed &&
      !owning_layer_.GetLayoutObject().DocumentBeingDestroyed())
    Compositor()->RootFixedBackgroundsChanged();

  return layer_changed;
}

bool CompositedLayerMapping::UpdateDecorationOutlineLayer(
    bool needs_decoration_outline_layer) {
  bool layer_changed = false;
  if (needs_decoration_outline_layer) {
    if (!decoration_outline_layer_) {
      decoration_outline_layer_ =
          CreateGraphicsLayer(kCompositingReasonLayerForDecoration);
      decoration_outline_layer_->SetPaintingPhase(
          kGraphicsLayerPaintDecoration);
      layer_changed = true;
    }
  } else if (decoration_outline_layer_) {
    decoration_outline_layer_ = nullptr;
    layer_changed = true;
  }

  return layer_changed;
}

bool CompositedLayerMapping::UpdateMaskLayer(bool needs_mask_layer) {
  bool layer_changed = false;
  if (needs_mask_layer) {
    if (!mask_layer_) {
      mask_layer_ = CreateGraphicsLayer(kCompositingReasonLayerForMask);
      mask_layer_->SetPaintingPhase(kGraphicsLayerPaintMask);
      layer_changed = true;
    }
  } else if (mask_layer_) {
    mask_layer_ = nullptr;
    layer_changed = true;
  }

  return layer_changed;
}

void CompositedLayerMapping::UpdateChildClippingMaskLayer(
    bool needs_child_clipping_mask_layer) {
  if (needs_child_clipping_mask_layer) {
    if (!child_clipping_mask_layer_) {
      child_clipping_mask_layer_ =
          CreateGraphicsLayer(kCompositingReasonLayerForClippingMask);
      child_clipping_mask_layer_->SetPaintingPhase(
          kGraphicsLayerPaintChildClippingMask);
    }
    return;
  }
  child_clipping_mask_layer_ = nullptr;
}

bool CompositedLayerMapping::UpdateScrollingLayers(
    bool needs_scrolling_layers) {
  ScrollingCoordinator* scrolling_coordinator =
      owning_layer_.GetScrollingCoordinator();

  bool layer_changed = false;
  if (needs_scrolling_layers) {
    if (!scrolling_layer_) {
      // Outer layer which corresponds with the scroll view.
      scrolling_layer_ =
          CreateGraphicsLayer(kCompositingReasonLayerForScrollingContainer);
      scrolling_layer_->SetDrawsContent(false);
      scrolling_layer_->SetMasksToBounds(true);

      // Inner layer which renders the content that scrolls.
      scrolling_contents_layer_ =
          CreateGraphicsLayer(kCompositingReasonLayerForScrollingContents);

      AnimatingData data;
      GetAnimatingData(owning_layer_, data);

      CompositorElementId element_id;
      if (data.animating_style && data.animating_style->HasCompositorProxy()) {
        element_id = CompositorElementIdFromDOMNodeId(
            DOMNodeIds::IdForNode(data.owning_node),
            CompositorElementIdNamespace::kScrollCompositorProxy);
      } else {
        element_id = CompositorElementIdFromPaintLayerId(
            owning_layer_.UniqueId(), CompositorElementIdNamespace::kScroll);
      }

      scrolling_contents_layer_->SetElementId(element_id);

      scrolling_layer_->AddChild(scrolling_contents_layer_.get());

      layer_changed = true;
      if (scrolling_coordinator) {
        scrolling_coordinator->ScrollableAreaScrollLayerDidChange(
            owning_layer_.GetScrollableArea());
        scrolling_coordinator->ScrollableAreasDidChange();
      }
    }
  } else if (scrolling_layer_) {
    scrolling_layer_ = nullptr;
    scrolling_contents_layer_ = nullptr;
    layer_changed = true;
    if (scrolling_coordinator) {
      scrolling_coordinator->ScrollableAreaScrollLayerDidChange(
          owning_layer_.GetScrollableArea());
      scrolling_coordinator->ScrollableAreasDidChange();
    }
  }

  return layer_changed;
}

static void UpdateScrollParentForGraphicsLayer(
    GraphicsLayer* layer,
    GraphicsLayer* topmost_layer,
    const PaintLayer* scroll_parent,
    ScrollingCoordinator* scrolling_coordinator) {
  if (!layer)
    return;

  // Only the topmost layer has a scroll parent. All other layers have a null
  // scroll parent.
  if (layer != topmost_layer)
    scroll_parent = 0;

  scrolling_coordinator->UpdateScrollParentForGraphicsLayer(layer,
                                                            scroll_parent);
}

void CompositedLayerMapping::UpdateScrollParent(
    const PaintLayer* scroll_parent) {
  if (ScrollingCoordinator* scrolling_coordinator =
          owning_layer_.GetScrollingCoordinator()) {
    GraphicsLayer* topmost_layer = ChildForSuperlayers();
    UpdateScrollParentForGraphicsLayer(squashing_containment_layer_.get(),
                                       topmost_layer, scroll_parent,
                                       scrolling_coordinator);
    UpdateScrollParentForGraphicsLayer(ancestor_clipping_layer_.get(),
                                       topmost_layer, scroll_parent,
                                       scrolling_coordinator);
    UpdateScrollParentForGraphicsLayer(graphics_layer_.get(), topmost_layer,
                                       scroll_parent, scrolling_coordinator);
  }
}

static void UpdateClipParentForGraphicsLayer(
    GraphicsLayer* layer,
    GraphicsLayer* topmost_layer,
    const PaintLayer* clip_parent,
    ScrollingCoordinator* scrolling_coordinator) {
  if (!layer)
    return;

  // Only the topmost layer has a scroll parent. All other layers have a null
  // scroll parent.
  if (layer != topmost_layer)
    clip_parent = 0;

  scrolling_coordinator->UpdateClipParentForGraphicsLayer(layer, clip_parent);
}

void CompositedLayerMapping::UpdateClipParent(const PaintLayer* scroll_parent) {
  const PaintLayer* clip_parent = nullptr;
  bool have_ancestor_clip_layer = false;
  bool have_ancestor_mask_layer = false;
  OwningLayerClippedOrMaskedByLayerNotAboveCompositedAncestor(
      scroll_parent, have_ancestor_clip_layer, have_ancestor_mask_layer);
  if (!have_ancestor_clip_layer) {
    clip_parent = owning_layer_.ClipParent();
    if (clip_parent)
      clip_parent =
          clip_parent->EnclosingLayerWithCompositedLayerMapping(kIncludeSelf);
  }

  if (ScrollingCoordinator* scrolling_coordinator =
          owning_layer_.GetScrollingCoordinator()) {
    GraphicsLayer* topmost_layer = ChildForSuperlayers();
    UpdateClipParentForGraphicsLayer(squashing_containment_layer_.get(),
                                     topmost_layer, clip_parent,
                                     scrolling_coordinator);
    UpdateClipParentForGraphicsLayer(ancestor_clipping_layer_.get(),
                                     topmost_layer, clip_parent,
                                     scrolling_coordinator);
    UpdateClipParentForGraphicsLayer(graphics_layer_.get(), topmost_layer,
                                     clip_parent, scrolling_coordinator);
  }
}

void CompositedLayerMapping::RegisterScrollingLayers() {
  // Register fixed position layers and their containers with the scrolling
  // coordinator.
  ScrollingCoordinator* scrolling_coordinator =
      owning_layer_.GetScrollingCoordinator();
  if (!scrolling_coordinator)
    return;

  scrolling_coordinator->UpdateLayerPositionConstraint(&owning_layer_);

  // Page scale is applied as a transform on the root layout view layer. Because
  // the scroll layer is further up in the hierarchy, we need to avoid marking
  // the root layout view layer as a container.
  bool is_container =
      owning_layer_.GetLayoutObject().CanContainFixedPositionObjects() &&
      !owning_layer_.IsRootLayer();
  scrolling_coordinator->SetLayerIsContainerForFixedPositionLayers(
      graphics_layer_.get(), is_container);
  // Fixed-pos descendants inherits the space that has all CSS property applied,
  // including perspective, overflow scroll/clip. Thus we also mark every layers
  // below the main graphics layer so transforms implemented by them don't get
  // skipped.
  ApplyToGraphicsLayers(
      this,
      [scrolling_coordinator, is_container](GraphicsLayer* layer) {
        scrolling_coordinator->SetLayerIsContainerForFixedPositionLayers(
            layer, is_container);
      },
      kApplyToChildContainingLayers);
}

bool CompositedLayerMapping::UpdateSquashingLayers(
    bool needs_squashing_layers) {
  bool layers_changed = false;

  if (needs_squashing_layers) {
    if (!squashing_layer_) {
      squashing_layer_ =
          CreateGraphicsLayer(kCompositingReasonLayerForSquashingContents);
      squashing_layer_->SetDrawsContent(true);
      layers_changed = true;
    }

    if (ancestor_clipping_layer_) {
      if (squashing_containment_layer_) {
        squashing_containment_layer_->RemoveFromParent();
        squashing_containment_layer_ = nullptr;
        layers_changed = true;
      }
    } else {
      if (!squashing_containment_layer_) {
        squashing_containment_layer_ =
            CreateGraphicsLayer(kCompositingReasonLayerForSquashingContainer);
        squashing_containment_layer_->SetShouldFlattenTransform(false);
        layers_changed = true;
      }
    }

    DCHECK((ancestor_clipping_layer_ && !squashing_containment_layer_) ||
           (!ancestor_clipping_layer_ && squashing_containment_layer_));
    DCHECK(squashing_layer_);
  } else {
    if (squashing_layer_) {
      squashing_layer_->RemoveFromParent();
      squashing_layer_ = nullptr;
      layers_changed = true;
    }
    if (squashing_containment_layer_) {
      squashing_containment_layer_->RemoveFromParent();
      squashing_containment_layer_ = nullptr;
      layers_changed = true;
    }
    DCHECK(!squashing_layer_);
    DCHECK(!squashing_containment_layer_);
  }

  return layers_changed;
}

GraphicsLayerPaintingPhase
CompositedLayerMapping::PaintingPhaseForPrimaryLayer() const {
  unsigned phase = 0;
  if (!background_layer_)
    phase |= kGraphicsLayerPaintBackground;
  if (!foreground_layer_)
    phase |= kGraphicsLayerPaintForeground;
  if (!mask_layer_)
    phase |= kGraphicsLayerPaintMask;
  if (!decoration_outline_layer_)
    phase |= kGraphicsLayerPaintDecoration;

  if (scrolling_contents_layer_) {
    phase &= ~kGraphicsLayerPaintForeground;
    phase |= kGraphicsLayerPaintCompositedScroll;
  }

  return static_cast<GraphicsLayerPaintingPhase>(phase);
}

float CompositedLayerMapping::CompositingOpacity(
    float layout_object_opacity) const {
  float final_opacity = layout_object_opacity;

  for (PaintLayer* curr = owning_layer_.Parent(); curr; curr = curr->Parent()) {
    // We only care about parents that are stacking contexts.
    // Recall that opacity creates stacking context.
    if (!curr->StackingNode()->IsStackingContext())
      continue;

    // If we found a composited layer, regardless of whether it actually
    // paints into it, we want to compute opacity relative to it. So we can
    // break here.
    //
    // FIXME: with grouped backings, a composited descendant will have to
    // continue past the grouped (squashed) layers that its parents may
    // contribute to. This whole confusion can be avoided by specifying
    // explicitly the composited ancestor where we would stop accumulating
    // opacity.
    if (curr->GetCompositingState() == kPaintsIntoOwnBacking)
      break;

    final_opacity *= curr->GetLayoutObject().Opacity();
  }

  return final_opacity;
}

Color CompositedLayerMapping::LayoutObjectBackgroundColor() const {
  return GetLayoutObject().ResolveColor(CSSPropertyBackgroundColor);
}

void CompositedLayerMapping::UpdateBackgroundColor() {
  graphics_layer_->SetBackgroundColor(LayoutObjectBackgroundColor());
}

bool CompositedLayerMapping::PaintsChildren() const {
  if (owning_layer_.HasVisibleContent() &&
      owning_layer_.HasNonEmptyChildLayoutObjects())
    return true;

  if (HasVisibleNonCompositingDescendant(&owning_layer_))
    return true;

  return false;
}

static bool IsCompositedPlugin(LayoutObject& layout_object) {
  return layout_object.IsEmbeddedObject() &&
         ToLayoutEmbeddedObject(layout_object).RequiresAcceleratedCompositing();
}

bool CompositedLayerMapping::HasVisibleNonCompositingDescendant(
    PaintLayer* parent) {
  if (!parent->HasVisibleDescendant())
    return false;

  // FIXME: We shouldn't be called with a stale z-order lists. See bug 85512.
  parent->StackingNode()->UpdateLayerListsIfNeeded();

#if DCHECK_IS_ON()
  LayerListMutationDetector mutation_checker(parent->StackingNode());
#endif

  PaintLayerStackingNodeIterator normal_flow_iterator(*parent->StackingNode(),
                                                      kAllChildren);
  while (PaintLayerStackingNode* cur_node = normal_flow_iterator.Next()) {
    PaintLayer* cur_layer = cur_node->Layer();
    if (cur_layer->HasCompositedLayerMapping())
      continue;
    if (cur_layer->HasVisibleContent() ||
        HasVisibleNonCompositingDescendant(cur_layer))
      return true;
  }

  return false;
}

bool CompositedLayerMapping::ContainsPaintedContent() const {
  if (GetLayoutObject().IsImage() && IsDirectlyCompositedImage())
    return false;

  LayoutObject& layout_object = this->GetLayoutObject();
  // FIXME: we could optimize cases where the image, video or canvas is known to
  // fill the border box entirely, and set background color on the layer in that
  // case, instead of allocating backing store and painting.
  if (layout_object.IsVideo() &&
      ToLayoutVideo(layout_object).ShouldDisplayVideo())
    return owning_layer_.HasBoxDecorationsOrBackground();

  if (owning_layer_.HasVisibleBoxDecorations())
    return true;

  if (layout_object.HasMask())  // masks require special treatment
    return true;

  if (layout_object.IsAtomicInlineLevel() && !IsCompositedPlugin(layout_object))
    return true;

  if (layout_object.IsLayoutMultiColumnSet())
    return true;

  if (layout_object.GetNode() && layout_object.GetNode()->IsDocumentNode()) {
    // Look to see if the root object has a non-simple background
    LayoutObject* root_object =
        layout_object.GetDocument().documentElement()
            ? layout_object.GetDocument().documentElement()->GetLayoutObject()
            : 0;
    // Reject anything that has a border, a border-radius or outline,
    // or is not a simple background (no background, or solid color).
    if (root_object &&
        HasBoxDecorationsOrBackgroundImage(root_object->StyleRef()))
      return true;

    // Now look at the body's layoutObject.
    HTMLElement* body = layout_object.GetDocument().body();
    LayoutObject* body_object =
        isHTMLBodyElement(body) ? body->GetLayoutObject() : 0;
    if (body_object &&
        HasBoxDecorationsOrBackgroundImage(body_object->StyleRef()))
      return true;
  }

  // FIXME: it's O(n^2). A better solution is needed.
  return PaintsChildren();
}

// An image can be directly composited if it's the sole content of the layer,
// and has no box decorations or clipping that require painting. Direct
// compositing saves a backing store.
bool CompositedLayerMapping::IsDirectlyCompositedImage() const {
  DCHECK(GetLayoutObject().IsImage());
  LayoutImage& image_layout_object = ToLayoutImage(GetLayoutObject());

  if (owning_layer_.HasBoxDecorationsOrBackground() ||
      image_layout_object.HasClip() || image_layout_object.HasClipPath() ||
      image_layout_object.HasObjectFit())
    return false;

  if (ImageResourceContent* cached_image = image_layout_object.CachedImage()) {
    if (!cached_image->HasImage())
      return false;

    Image* image = cached_image->GetImage();
    if (!image->IsBitmapImage())
      return false;

    return true;
  }

  return false;
}

void CompositedLayerMapping::ContentChanged(ContentChangeType change_type) {
  if ((change_type == kImageChanged) && GetLayoutObject().IsImage() &&
      IsDirectlyCompositedImage()) {
    UpdateImageContents();
    return;
  }

  if (change_type == kCanvasChanged && IsCompositedCanvas(GetLayoutObject())) {
    graphics_layer_->SetContentsNeedsDisplay();
    return;
  }
}

void CompositedLayerMapping::UpdateImageContents() {
  DCHECK(GetLayoutObject().IsImage());
  LayoutImage& image_layout_object = ToLayoutImage(GetLayoutObject());

  ImageResourceContent* cached_image = image_layout_object.CachedImage();
  if (!cached_image)
    return;

  Image* image = cached_image->GetImage();
  if (!image)
    return;

  // This is a no-op if the layer doesn't have an inner layer for the image.
  graphics_layer_->SetContentsToImage(
      image, LayoutObject::ShouldRespectImageOrientation(&image_layout_object));

  graphics_layer_->SetFilterQuality(
      GetLayoutObject().Style()->ImageRendering() == kImageRenderingPixelated
          ? kNone_SkFilterQuality
          : kLow_SkFilterQuality);

  // Prevent double-drawing: https://bugs.webkit.org/show_bug.cgi?id=58632
  UpdateDrawsContent();

  // Image animation is "lazy", in that it automatically stops unless someone is
  // drawing the image. So we have to kick the animation each time; this has the
  // downside that the image will keep animating, even if its layer is not
  // visible.
  image->StartAnimation();
}

FloatPoint3D CompositedLayerMapping::ComputeTransformOrigin(
    const IntRect& border_box) const {
  const ComputedStyle& style = GetLayoutObject().StyleRef();

  FloatPoint3D origin;
  origin.SetX(
      FloatValueForLength(style.TransformOriginX(), border_box.Width()));
  origin.SetY(
      FloatValueForLength(style.TransformOriginY(), border_box.Height()));
  origin.SetZ(style.TransformOriginZ());

  return origin;
}

// Return the offset from the top-left of this compositing layer at which the
// LayoutObject's contents are painted.
LayoutSize CompositedLayerMapping::ContentOffsetInCompositingLayer() const {
  DCHECK(!content_offset_in_compositing_layer_dirty_);
  return content_offset_in_compositing_layer_;
}

LayoutRect CompositedLayerMapping::ContentsBox() const {
  LayoutRect contents_box = LayoutRect(ContentsRect(GetLayoutObject()));
  contents_box.Move(ContentOffsetInCompositingLayer());
  return contents_box;
}

bool CompositedLayerMapping::NeedsToReparentOverflowControls() const {
  return owning_layer_.GetScrollableArea() &&
         owning_layer_.GetScrollableArea()->HasOverlayScrollbars() &&
         owning_layer_.GetScrollableArea()->TopmostScrollChild();
}

GraphicsLayer* CompositedLayerMapping::DetachLayerForOverflowControls() {
  GraphicsLayer* host = overflow_controls_ancestor_clipping_layer_.get();
  if (!host)
    host = overflow_controls_host_layer_.get();
  host->RemoveFromParent();
  return host;
}

GraphicsLayer* CompositedLayerMapping::ParentForSublayers() const {
  if (scrolling_contents_layer_)
    return scrolling_contents_layer_.get();

  if (child_containment_layer_)
    return child_containment_layer_.get();

  if (child_transform_layer_)
    return child_transform_layer_.get();

  return graphics_layer_.get();
}

void CompositedLayerMapping::SetSublayers(
    const GraphicsLayerVector& sublayers) {
  GraphicsLayer* overflow_controls_container =
      overflow_controls_ancestor_clipping_layer_
          ? overflow_controls_ancestor_clipping_layer_.get()
          : overflow_controls_host_layer_.get();
  GraphicsLayer* parent = ParentForSublayers();
  bool needs_overflow_controls_reattached =
      overflow_controls_container &&
      overflow_controls_container->Parent() == parent;

  parent->SetChildren(sublayers);

  // If we have scrollbars, but are not using composited scrolling, then
  // parentForSublayers may return m_graphicsLayer.  In that case, the above
  // call to setChildren has clobbered the overflow controls host layer, so we
  // need to reattach it.
  if (needs_overflow_controls_reattached)
    parent->AddChild(overflow_controls_container);
}

GraphicsLayer* CompositedLayerMapping::ChildForSuperlayers() const {
  if (squashing_containment_layer_)
    return squashing_containment_layer_.get();

  if (ancestor_clipping_layer_)
    return ancestor_clipping_layer_.get();

  return graphics_layer_.get();
}

void CompositedLayerMapping::SetBlendMode(WebBlendMode blend_mode) {
  if (ancestor_clipping_layer_) {
    ancestor_clipping_layer_->SetBlendMode(blend_mode);
    graphics_layer_->SetBlendMode(kWebBlendModeNormal);
  } else {
    graphics_layer_->SetBlendMode(blend_mode);
  }
}

GraphicsLayerUpdater::UpdateType CompositedLayerMapping::UpdateTypeForChildren(
    GraphicsLayerUpdater::UpdateType update_type) const {
  if (pending_update_scope_ >= kGraphicsLayerUpdateSubtree)
    return GraphicsLayerUpdater::kForceUpdate;
  return update_type;
}

struct SetContentsNeedsDisplayFunctor {
  void operator()(GraphicsLayer* layer) const {
    if (layer->DrawsContent())
      layer->SetNeedsDisplay();
  }
};

void CompositedLayerMapping::SetSquashingContentsNeedDisplay() {
  ApplyToGraphicsLayers(this, SetContentsNeedsDisplayFunctor(),
                        kApplyToSquashingLayer);
}

void CompositedLayerMapping::SetContentsNeedDisplay() {
  // FIXME: need to split out paint invalidations for the background.
  ApplyToGraphicsLayers(this, SetContentsNeedsDisplayFunctor(),
                        kApplyToContentLayers);
}

struct SetContentsNeedsDisplayInRectFunctor {
  void operator()(GraphicsLayer* layer) const {
    if (layer->DrawsContent()) {
      IntRect layer_dirty_rect = r;
      layer_dirty_rect.Move(-layer->OffsetFromLayoutObject());
      layer->SetNeedsDisplayInRect(layer_dirty_rect, invalidation_reason,
                                   client);
    }
  }

  IntRect r;
  PaintInvalidationReason invalidation_reason;
  const DisplayItemClient& client;
};

void CompositedLayerMapping::SetContentsNeedDisplayInRect(
    const LayoutRect& r,
    PaintInvalidationReason invalidation_reason,
    const DisplayItemClient& client) {
  DCHECK(!owning_layer_.GetLayoutObject().UsesCompositedScrolling());
  // TODO(wangxianzhu): Enable the following assert after paint invalidation for
  // spv2 is ready.
  // DCHECK(!RuntimeEnabledFeatures::slimmingPaintV2Enabled());

  SetContentsNeedsDisplayInRectFunctor functor = {
      EnclosingIntRect(LayoutRect(
          r.Location() + owning_layer_.SubpixelAccumulation(), r.Size())),
      invalidation_reason, client};
  ApplyToGraphicsLayers(this, functor, kApplyToContentLayers);
}

void CompositedLayerMapping::SetNonScrollingContentsNeedDisplayInRect(
    const LayoutRect& r,
    PaintInvalidationReason invalidation_reason,
    const DisplayItemClient& client) {
  DCHECK(owning_layer_.GetLayoutObject().UsesCompositedScrolling());
  // TODO(wangxianzhu): Enable the following assert after paint invalidation for
  // spv2 is ready.
  // DCHECK(!RuntimeEnabledFeatures::slimmingPaintV2Enabled());

  SetContentsNeedsDisplayInRectFunctor functor = {
      EnclosingIntRect(LayoutRect(
          r.Location() + owning_layer_.SubpixelAccumulation(), r.Size())),
      invalidation_reason, client};
  ApplyToGraphicsLayers(this, functor, kApplyToNonScrollingContentLayers);
}

void CompositedLayerMapping::SetScrollingContentsNeedDisplayInRect(
    const LayoutRect& r,
    PaintInvalidationReason invalidation_reason,
    const DisplayItemClient& client) {
  DCHECK(owning_layer_.GetLayoutObject().UsesCompositedScrolling());
  // TODO(wangxianzhu): Enable the following assert after paint invalidation for
  // spv2 is ready.
  // DCHECK(!RuntimeEnabledFeatures::slimmingPaintV2Enabled());

  SetContentsNeedsDisplayInRectFunctor functor = {
      EnclosingIntRect(LayoutRect(
          r.Location() + owning_layer_.SubpixelAccumulation(), r.Size())),
      invalidation_reason, client};
  ApplyToGraphicsLayers(this, functor, kApplyToScrollingContentLayers);
}

const GraphicsLayerPaintInfo* CompositedLayerMapping::ContainingSquashedLayer(
    const LayoutObject* layout_object,
    const Vector<GraphicsLayerPaintInfo>& layers,
    unsigned max_squashed_layer_index) {
  if (!layout_object)
    return nullptr;
  for (size_t i = 0; i < layers.size() && i < max_squashed_layer_index; ++i) {
    if (layout_object->IsDescendantOf(
            &layers[i].paint_layer->GetLayoutObject()))
      return &layers[i];
  }
  return nullptr;
}

const GraphicsLayerPaintInfo* CompositedLayerMapping::ContainingSquashedLayer(
    const LayoutObject* layout_object,
    unsigned max_squashed_layer_index) {
  return CompositedLayerMapping::ContainingSquashedLayer(
      layout_object, squashed_layers_, max_squashed_layer_index);
}

IntRect CompositedLayerMapping::LocalClipRectForSquashedLayer(
    const PaintLayer& reference_layer,
    const GraphicsLayerPaintInfo& paint_info,
    const Vector<GraphicsLayerPaintInfo>& layers) {
  const LayoutObject* clipping_container =
      paint_info.paint_layer->ClippingContainer();
  if (clipping_container == reference_layer.ClippingContainer())
    return LayoutRect::InfiniteIntRect();

  DCHECK(clipping_container);

  const GraphicsLayerPaintInfo* ancestor_paint_info =
      ContainingSquashedLayer(clipping_container, layers, layers.size());
  // Must be there, otherwise
  // CompositingLayerAssigner::canSquashIntoCurrentSquashingOwner would have
  // disallowed squashing.
  DCHECK(ancestor_paint_info);

  // FIXME: this is a potential performance issue. We should consider caching
  // these clip rects or otherwise optimizing.
  ClipRectsContext clip_rects_context(ancestor_paint_info->paint_layer,
                                      kUncachedClipRects);
  ClipRect parent_clip_rect;
  paint_info.paint_layer->Clipper(PaintLayer::kDoNotUseGeometryMapper)
      .CalculateBackgroundClipRect(clip_rects_context, parent_clip_rect);

  IntRect snapped_parent_clip_rect(
      PixelSnappedIntRect(parent_clip_rect.Rect()));
  DCHECK(snapped_parent_clip_rect != LayoutRect::InfiniteIntRect());

  // Convert from ancestor to local coordinates.
  IntSize ancestor_to_local_offset =
      paint_info.offset_from_layout_object -
      ancestor_paint_info->offset_from_layout_object;
  snapped_parent_clip_rect.Move(ancestor_to_local_offset);
  return snapped_parent_clip_rect;
}

void CompositedLayerMapping::DoPaintTask(
    const GraphicsLayerPaintInfo& paint_info,
    const GraphicsLayer& graphics_layer,
    const PaintLayerFlags& paint_layer_flags,
    GraphicsContext& context,
    const IntRect& clip /* In the coords of rootLayer */) const {
  FontCachePurgePreventer font_cache_purge_preventer;

  IntSize offset = paint_info.offset_from_layout_object;
  AffineTransform translation;
  translation.Translate(-offset.Width(), -offset.Height());
  TransformRecorder transform_recorder(context, graphics_layer, translation);

  // The dirtyRect is in the coords of the painting root.
  IntRect dirty_rect(clip);
  dirty_rect.Move(offset);

  if (paint_layer_flags & (kPaintLayerPaintingOverflowContents |
                           kPaintLayerPaintingAncestorClippingMaskPhase)) {
    dirty_rect.Move(
        RoundedIntSize(paint_info.paint_layer->SubpixelAccumulation()));
  } else {
    LayoutRect bounds = paint_info.composited_bounds;
    bounds.Move(paint_info.paint_layer->SubpixelAccumulation());
    dirty_rect.Intersect(PixelSnappedIntRect(bounds));
  }

#if DCHECK_IS_ON()
  if (!GetLayoutObject().View()->GetFrame() ||
      !GetLayoutObject().View()->GetFrame()->ShouldThrottleRendering())
    paint_info.paint_layer->GetLayoutObject().AssertSubtreeIsLaidOut();
#endif

  float device_scale_factor = blink::DeviceScaleFactorDeprecated(
      paint_info.paint_layer->GetLayoutObject().GetFrame());
  context.SetDeviceScaleFactor(device_scale_factor);

  if (paint_info.paint_layer->GetCompositingState() !=
      kPaintsIntoGroupedBacking) {
    // FIXME: GraphicsLayers need a way to split for multicol.
    PaintLayerPaintingInfo painting_info(
        paint_info.paint_layer, LayoutRect(dirty_rect), kGlobalPaintNormalPhase,
        paint_info.paint_layer->SubpixelAccumulation());
    PaintLayerPainter(*paint_info.paint_layer)
        .PaintLayerContents(context, painting_info, paint_layer_flags);

    if (paint_info.paint_layer->ContainsDirtyOverlayScrollbars())
      PaintLayerPainter(*paint_info.paint_layer)
          .PaintLayerContents(
              context, painting_info,
              paint_layer_flags | kPaintLayerPaintingOverlayScrollbars);
  } else {
    PaintLayerPaintingInfo painting_info(
        paint_info.paint_layer, LayoutRect(dirty_rect), kGlobalPaintNormalPhase,
        paint_info.paint_layer->SubpixelAccumulation());

    // PaintLayer::paintLayer assumes that the caller clips to the passed rect.
    // Squashed layers need to do this clipping in software, since there is no
    // graphics layer to clip them precisely. Furthermore, in some cases we
    // squash layers that need clipping in software from clipping ancestors (see
    // CompositedLayerMapping::localClipRectForSquashedLayer()).
    // FIXME: Is it correct to clip to dirtyRect in slimming paint mode?
    // FIXME: Combine similar code here and LayerClipRecorder.
    dirty_rect.Intersect(paint_info.local_clip_rect_for_squashed_layer);
    context.GetPaintController().CreateAndAppend<ClipDisplayItem>(
        graphics_layer, DisplayItem::kClipLayerOverflowControls, dirty_rect);

    PaintLayerPainter(*paint_info.paint_layer)
        .Paint(context, painting_info, paint_layer_flags);
    context.GetPaintController().EndItem<EndClipDisplayItem>(
        graphics_layer, DisplayItem::ClipTypeToEndClipType(
                            DisplayItem::kClipLayerOverflowControls));
  }
}

static void PaintScrollbar(const Scrollbar* scrollbar,
                           GraphicsContext& context,
                           const IntRect& clip) {
  if (!scrollbar)
    return;

  const IntRect& scrollbar_rect = scrollbar->FrameRect();
  TransformRecorder transform_recorder(
      context, *scrollbar,
      AffineTransform::Translation(-scrollbar_rect.X(), -scrollbar_rect.Y()));
  IntRect transformed_clip = clip;
  transformed_clip.MoveBy(scrollbar_rect.Location());
  scrollbar->Paint(context, CullRect(transformed_clip));
}

// TODO(eseckler): Make recording distance configurable, e.g. for use in
// headless, where we would like to record an exact area.
// Note however that the minimum value for this constant is the size of a
// raster tile. This is because the raster system is not able to raster a
// tile that is not completely covered by a display list. If the constant
// were less than the size of a tile, then a tile which partially overlaps
// the screen may not be rastered.
static const int kPixelDistanceToRecord = 4000;

IntRect CompositedLayerMapping::RecomputeInterestRect(
    const GraphicsLayer* graphics_layer) const {
  FloatRect graphics_layer_bounds(FloatPoint(), graphics_layer->Size());

  IntSize offset_from_anchor_layout_object;
  const LayoutBoxModelObject* anchor_layout_object;
  if (graphics_layer == squashing_layer_.get()) {
    // TODO(chrishtr): this is a speculative fix for crbug.com/561306. However,
    // it should never be the case that m_squashingLayer exists,
    // yet m_squashedLayers.size() == 0. There must be a bug elsewhere.
    if (squashed_layers_.size() == 0)
      return IntRect();
    // All squashed layers have the same clip and transform space, so we can use
    // the first squashed layer's layoutObject to map the squashing layer's
    // bounds into viewport space, with offsetFromAnchorLayoutObject to
    // translate squashing layer's bounds into the first squashed layer's space.
    anchor_layout_object = &squashed_layers_[0].paint_layer->GetLayoutObject();
    offset_from_anchor_layout_object =
        squashed_layers_[0].offset_from_layout_object;
  } else {
    DCHECK(graphics_layer == graphics_layer_.get() ||
           graphics_layer == scrolling_contents_layer_.get());
    anchor_layout_object = &owning_layer_.GetLayoutObject();
    offset_from_anchor_layout_object = graphics_layer->OffsetFromLayoutObject();
    AdjustForCompositedScrolling(graphics_layer,
                                 offset_from_anchor_layout_object);
  }

  // Start with the bounds of the graphics layer in the space of the anchor
  // LayoutObject.
  FloatRect graphics_layer_bounds_in_object_space(graphics_layer_bounds);
  graphics_layer_bounds_in_object_space.Move(offset_from_anchor_layout_object);

  // Now map the bounds to its visible content rect in root view space,
  // including applying clips along the way.
  LayoutRect graphics_layer_bounds_in_root_view_space(
      graphics_layer_bounds_in_object_space);
  LayoutView* root_view = anchor_layout_object->View();
  while (!root_view->GetFrame()->OwnerLayoutItem().IsNull())
    root_view = LayoutAPIShim::LayoutObjectFrom(
                    root_view->GetFrame()->OwnerLayoutItem())
                    ->View();
  anchor_layout_object->MapToVisualRectInAncestorSpace(
      root_view, graphics_layer_bounds_in_root_view_space);
  FloatRect visible_content_rect(graphics_layer_bounds_in_root_view_space);
  root_view->GetFrameView()->ClipPaintRect(&visible_content_rect);

  IntRect enclosing_graphics_layer_bounds(
      EnclosingIntRect(graphics_layer_bounds));

  // Map the visible content rect from root view space to local graphics layer
  // space.
  IntRect local_interest_rect;
  // If the visible content rect is empty, then it makes no sense to map it back
  // since there is nothing to map.
  if (!visible_content_rect.IsEmpty()) {
    local_interest_rect =
        anchor_layout_object
            ->AbsoluteToLocalQuad(visible_content_rect,
                                  kUseTransforms | kTraverseDocumentBoundaries)
            .EnclosingBoundingBox();
    local_interest_rect.Move(-offset_from_anchor_layout_object);
    // TODO(chrishtr): the code below is a heuristic, instead we should detect
    // and return whether the mapping failed.  In some cases,
    // absoluteToLocalQuad can fail to map back to the local space, due to
    // passing through non-invertible transforms or floating-point accuracy
    // issues. Examples include rotation near 90 degrees or perspective. In such
    // cases, fall back to painting the first kPixelDistanceToRecord pixels in
    // each direction.
    local_interest_rect.Intersect(enclosing_graphics_layer_bounds);
  }
  // Expand by interest rect padding amount.
  local_interest_rect.Inflate(kPixelDistanceToRecord);
  local_interest_rect.Intersect(enclosing_graphics_layer_bounds);
  return local_interest_rect;
}

static const int kMinimumDistanceBeforeRepaint = 512;

bool CompositedLayerMapping::InterestRectChangedEnoughToRepaint(
    const IntRect& previous_interest_rect,
    const IntRect& new_interest_rect,
    const IntSize& layer_size) {
  if (previous_interest_rect.IsEmpty() && new_interest_rect.IsEmpty())
    return false;

  // Repaint when going from empty to not-empty, to cover cases where the layer
  // is painted for the first time, or otherwise becomes visible.
  if (previous_interest_rect.IsEmpty())
    return true;

  // Repaint if the new interest rect includes area outside of a skirt around
  // the existing interest rect.
  IntRect expanded_previous_interest_rect(previous_interest_rect);
  expanded_previous_interest_rect.Inflate(kMinimumDistanceBeforeRepaint);
  if (!expanded_previous_interest_rect.Contains(new_interest_rect))
    return true;

  // Even if the new interest rect doesn't include enough new area to satisfy
  // the condition above, repaint anyway if it touches a layer edge not touched
  // by the existing interest rect.  Because it's impossible to expose more area
  // in the direction, repainting cannot be deferred until the exposed new area
  // satisfies the condition above.
  if (new_interest_rect.X() == 0 && previous_interest_rect.X() != 0)
    return true;
  if (new_interest_rect.Y() == 0 && previous_interest_rect.Y() != 0)
    return true;
  if (new_interest_rect.MaxX() == layer_size.Width() &&
      previous_interest_rect.MaxX() != layer_size.Width())
    return true;
  if (new_interest_rect.MaxY() == layer_size.Height() &&
      previous_interest_rect.MaxY() != layer_size.Height())
    return true;

  return false;
}

IntRect CompositedLayerMapping::ComputeInterestRect(
    const GraphicsLayer* graphics_layer,
    const IntRect& previous_interest_rect) const {
  // Use the previous interest rect if it covers the whole layer.
  IntRect whole_layer_rect =
      IntRect(IntPoint(), ExpandedIntSize(graphics_layer->Size()));
  if (!NeedsRepaint(*graphics_layer) &&
      previous_interest_rect == whole_layer_rect)
    return previous_interest_rect;

  if (graphics_layer != graphics_layer_.get() &&
      graphics_layer != squashing_layer_.get() &&
      graphics_layer != scrolling_contents_layer_.get())
    return whole_layer_rect;

  IntRect new_interest_rect = RecomputeInterestRect(graphics_layer);
  if (NeedsRepaint(*graphics_layer) ||
      InterestRectChangedEnoughToRepaint(
          previous_interest_rect, new_interest_rect,
          ExpandedIntSize(graphics_layer->Size())))
    return new_interest_rect;
  return previous_interest_rect;
}

LayoutSize CompositedLayerMapping::SubpixelAccumulation() const {
  return owning_layer_.SubpixelAccumulation();
}

bool CompositedLayerMapping::NeedsRepaint(
    const GraphicsLayer& graphics_layer) const {
  return IsScrollableAreaLayer(&graphics_layer) ? true
                                                : owning_layer_.NeedsRepaint();
}

void CompositedLayerMapping::AdjustForCompositedScrolling(
    const GraphicsLayer* graphics_layer,
    IntSize& offset) const {
  if (graphics_layer == scrolling_contents_layer_.get() ||
      graphics_layer == foreground_layer_.get()) {
    if (PaintLayerScrollableArea* scrollable_area =
            owning_layer_.GetScrollableArea()) {
      if (scrollable_area->UsesCompositedScrolling()) {
        // Note: this is the offset from the beginning of flow of the block, not
        // the offset from the top/left of the overflow rect.
        // offsetFromLayoutObject adds the origin offset from top/left to the
        // beginning of flow.
        ScrollOffset scroll_offset = scrollable_area->GetScrollOffset();
        offset.Expand(-scroll_offset.Width(), -scroll_offset.Height());
      }
    }
  }
}

void CompositedLayerMapping::PaintContents(
    const GraphicsLayer* graphics_layer,
    GraphicsContext& context,
    GraphicsLayerPaintingPhase graphics_layer_painting_phase,
    const IntRect& interest_rect) const {
  FramePaintTiming frame_paint_timing(context, GetLayoutObject().GetFrame());

  // https://code.google.com/p/chromium/issues/detail?id=343772
  DisableCompositingQueryAsserts disabler;
  // Allow throttling to make sure no painting paths (e.g.,
  // ContentLayerDelegate::paintContents) try to paint throttled content.
  DocumentLifecycle::AllowThrottlingScope allow_throttling(
      owning_layer_.GetLayoutObject().GetDocument().Lifecycle());
#if DCHECK_IS_ON()
  // FIXME: once the state machine is ready, this can be removed and we can
  // refer to that instead.
  if (Page* page = GetLayoutObject().GetFrame()->GetPage())
    page->SetIsPainting(true);
#endif

  TRACE_EVENT1(
      "devtools.timeline,rail", "Paint", "data",
      InspectorPaintEvent::Data(&owning_layer_.GetLayoutObject(),
                                LayoutRect(interest_rect), graphics_layer));

  PaintLayerFlags paint_layer_flags = 0;
  if (graphics_layer_painting_phase & kGraphicsLayerPaintBackground)
    paint_layer_flags |= kPaintLayerPaintingCompositingBackgroundPhase;
  else
    paint_layer_flags |= kPaintLayerPaintingSkipRootBackground;
  if (graphics_layer_painting_phase & kGraphicsLayerPaintForeground)
    paint_layer_flags |= kPaintLayerPaintingCompositingForegroundPhase;
  if (graphics_layer_painting_phase & kGraphicsLayerPaintMask)
    paint_layer_flags |= kPaintLayerPaintingCompositingMaskPhase;
  if (graphics_layer_painting_phase & kGraphicsLayerPaintChildClippingMask)
    paint_layer_flags |= kPaintLayerPaintingChildClippingMaskPhase;
  if (graphics_layer_painting_phase & kGraphicsLayerPaintAncestorClippingMask)
    paint_layer_flags |= kPaintLayerPaintingAncestorClippingMaskPhase;
  if (graphics_layer_painting_phase & kGraphicsLayerPaintOverflowContents)
    paint_layer_flags |= kPaintLayerPaintingOverflowContents;
  if (graphics_layer_painting_phase & kGraphicsLayerPaintCompositedScroll)
    paint_layer_flags |= kPaintLayerPaintingCompositingScrollingPhase;
  if (graphics_layer_painting_phase & kGraphicsLayerPaintDecoration)
    paint_layer_flags |= kPaintLayerPaintingCompositingDecorationPhase;

  if (graphics_layer == background_layer_.get())
    paint_layer_flags |= kPaintLayerPaintingRootBackgroundOnly;
  else if (Compositor()->FixedRootBackgroundLayer() &&
           owning_layer_.IsRootLayer())
    paint_layer_flags |= kPaintLayerPaintingSkipRootBackground;

  if (graphics_layer == graphics_layer_.get() ||
      graphics_layer == foreground_layer_.get() ||
      graphics_layer == background_layer_.get() ||
      graphics_layer == mask_layer_.get() ||
      graphics_layer == child_clipping_mask_layer_.get() ||
      graphics_layer == scrolling_contents_layer_.get() ||
      graphics_layer == decoration_outline_layer_.get() ||
      graphics_layer == ancestor_clipping_mask_layer_.get()) {
    bool paint_root_background_onto_scrolling_contents_layer =
        background_paints_onto_scrolling_contents_layer_;
    DCHECK(!paint_root_background_onto_scrolling_contents_layer ||
           (!background_layer_ && !foreground_layer_));
    if (paint_root_background_onto_scrolling_contents_layer) {
      if (graphics_layer == scrolling_contents_layer_.get())
        paint_layer_flags &= ~kPaintLayerPaintingSkipRootBackground;
      else if (!background_paints_onto_graphics_layer_)
        paint_layer_flags |= kPaintLayerPaintingSkipRootBackground;
    }

    GraphicsLayerPaintInfo paint_info;
    paint_info.paint_layer = &owning_layer_;
    paint_info.composited_bounds = CompositedBounds();
    paint_info.offset_from_layout_object =
        graphics_layer->OffsetFromLayoutObject();
    AdjustForCompositedScrolling(graphics_layer,
                                 paint_info.offset_from_layout_object);

    // We have to use the same root as for hit testing, because both methods can
    // compute and cache clipRects.
    DoPaintTask(paint_info, *graphics_layer, paint_layer_flags, context,
                interest_rect);
  } else if (graphics_layer == squashing_layer_.get()) {
    for (size_t i = 0; i < squashed_layers_.size(); ++i)
      DoPaintTask(squashed_layers_[i], *graphics_layer, paint_layer_flags,
                  context, interest_rect);
  } else if (IsScrollableAreaLayer(graphics_layer)) {
    PaintScrollableArea(graphics_layer, context, interest_rect);
  }
  probe::didPaint(owning_layer_.GetLayoutObject().GetFrame(), graphics_layer,
                  context, LayoutRect(interest_rect));
#if DCHECK_IS_ON()
  if (Page* page = GetLayoutObject().GetFrame()->GetPage())
    page->SetIsPainting(false);
#endif
}

void CompositedLayerMapping::PaintScrollableArea(
    const GraphicsLayer* graphics_layer,
    GraphicsContext& context,
    const IntRect& interest_rect) const {
  // Note the composited scrollable area painted here is never associated with a
  // frame. For painting frame ScrollableAreas, see
  // PaintLayerCompositor::paintContents.

  if (DrawingRecorder::UseCachedDrawingIfPossible(
          context, *graphics_layer, DisplayItem::kScrollbarCompositedScrollbar))
    return;

  FloatRect layer_bounds(FloatPoint(), graphics_layer->Size());
  PaintRecordBuilder builder(layer_bounds, nullptr, &context);
  PaintLayerScrollableArea* scrollable_area = owning_layer_.GetScrollableArea();
  if (graphics_layer == LayerForHorizontalScrollbar()) {
    PaintScrollbar(scrollable_area->HorizontalScrollbar(), builder.Context(),
                   interest_rect);
  } else if (graphics_layer == LayerForVerticalScrollbar()) {
    PaintScrollbar(scrollable_area->VerticalScrollbar(), builder.Context(),
                   interest_rect);
  } else if (graphics_layer == LayerForScrollCorner()) {
    // Note that scroll corners always paint into local space, whereas
    // scrollbars paint in the space of their containing frame.
    IntPoint scroll_corner_and_resizer_location =
        scrollable_area->ScrollCornerAndResizerRect().Location();
    CullRect cull_rect(interest_rect);
    ScrollableAreaPainter(*scrollable_area)
        .PaintScrollCorner(builder.Context(),
                           -scroll_corner_and_resizer_location, cull_rect);
    ScrollableAreaPainter(*scrollable_area)
        .PaintResizer(builder.Context(), -scroll_corner_and_resizer_location,
                      cull_rect);
  }
  // Replay the painted scrollbar content with the GraphicsLayer backing as the
  // DisplayItemClient in order for the resulting DrawingDisplayItem to produce
  // the correct visualRect (i.e., the bounds of the involved GraphicsLayer).
  DrawingRecorder drawing_recorder(context, *graphics_layer,
                                   DisplayItem::kScrollbarCompositedScrollbar,
                                   layer_bounds);
  context.Canvas()->drawPicture(builder.EndRecording());
}

bool CompositedLayerMapping::IsScrollableAreaLayer(
    const GraphicsLayer* graphics_layer) const {
  return graphics_layer == LayerForHorizontalScrollbar() ||
         graphics_layer == LayerForVerticalScrollbar() ||
         graphics_layer == LayerForScrollCorner();
}

bool CompositedLayerMapping::IsTrackingRasterInvalidations() const {
  GraphicsLayerClient* client = Compositor();
  return client ? client->IsTrackingRasterInvalidations() : false;
}

#if DCHECK_IS_ON()
void CompositedLayerMapping::VerifyNotPainting() {
  DCHECK(!GetLayoutObject().GetFrame()->GetPage() ||
         !GetLayoutObject().GetFrame()->GetPage()->IsPainting());
}
#endif

// Only used for performance benchmark testing. Intended to be a
// sufficiently-unique element id name to allow picking out the target element
// for invalidation.
static const char kTestPaintInvalidationTargetName[] =
    "blinkPaintInvalidationTarget";

void CompositedLayerMapping::InvalidateTargetElementForTesting() {
  // The below is an artificial construct formed intentionally to focus a
  // microbenchmark on the cost of paint with a partial invalidation.
  Element* target_element =
      owning_layer_.GetLayoutObject().GetDocument().getElementById(
          AtomicString(kTestPaintInvalidationTargetName));
  // TODO(wkorman): If we don't find the expected target element, we could
  // consider walking to the first leaf node so that the partial-invalidation
  // benchmark mode still provides some value when running on generic pages.
  if (!target_element)
    return;
  LayoutObject* target_object = target_element->GetLayoutObject();
  if (!target_object)
    return;
  target_object->EnclosingLayer()->SetNeedsRepaint();
  // TODO(wkorman): Consider revising the below to invalidate all
  // non-compositing descendants as well.
  target_object->InvalidateDisplayItemClients(
      PaintInvalidationReason::kForTesting);
}

IntRect CompositedLayerMapping::PixelSnappedCompositedBounds() const {
  LayoutRect bounds = composited_bounds_;
  bounds.Move(owning_layer_.SubpixelAccumulation());
  return PixelSnappedIntRect(bounds);
}

bool CompositedLayerMapping::InvalidateLayerIfNoPrecedingEntry(
    size_t index_to_clear) {
  PaintLayer* layer_to_remove = squashed_layers_[index_to_clear].paint_layer;
  size_t previous_index = 0;
  for (; previous_index < index_to_clear; ++previous_index) {
    if (squashed_layers_[previous_index].paint_layer == layer_to_remove)
      break;
  }
  if (previous_index == index_to_clear &&
      layer_to_remove->GroupedMapping() == this) {
    Compositor()->PaintInvalidationOnCompositingChange(layer_to_remove);
    return true;
  }
  return false;
}

bool CompositedLayerMapping::UpdateSquashingLayerAssignment(
    PaintLayer* squashed_layer,
    size_t next_squashed_layer_index) {
  GraphicsLayerPaintInfo paint_info;
  paint_info.paint_layer = squashed_layer;
  // NOTE: composited bounds are updated elsewhere
  // NOTE: offsetFromLayoutObject is updated elsewhere

  // Change tracking on squashing layers: at the first sign of something
  // changed, just invalidate the layer.
  // FIXME: Perhaps we can find a tighter more clever mechanism later.
  if (next_squashed_layer_index < squashed_layers_.size()) {
    if (paint_info.paint_layer ==
        squashed_layers_[next_squashed_layer_index].paint_layer)
      return false;

    // Must invalidate before adding the squashed layer to the mapping.
    Compositor()->PaintInvalidationOnCompositingChange(squashed_layer);

    // If the layer which was previously at |nextSquashedLayerIndex| is not
    // earlier in the grouped mapping, invalidate its current backing now, since
    // it will move later or be removed from the squashing layer.
    InvalidateLayerIfNoPrecedingEntry(next_squashed_layer_index);

    squashed_layers_.insert(next_squashed_layer_index, paint_info);
  } else {
    // Must invalidate before adding the squashed layer to the mapping.
    Compositor()->PaintInvalidationOnCompositingChange(squashed_layer);
    squashed_layers_.push_back(paint_info);
  }
  squashed_layer->SetGroupedMapping(
      this, PaintLayer::kInvalidateLayerAndRemoveFromMapping);

  return true;
}

void CompositedLayerMapping::RemoveLayerFromSquashingGraphicsLayer(
    const PaintLayer* layer) {
  size_t layer_index = 0;
  for (; layer_index < squashed_layers_.size(); ++layer_index) {
    if (squashed_layers_[layer_index].paint_layer == layer)
      break;
  }

  // Assert on incorrect mappings between layers and groups
  DCHECK_LT(layer_index, squashed_layers_.size());
  if (layer_index == squashed_layers_.size())
    return;

  squashed_layers_.erase(layer_index);
}

#if DCHECK_IS_ON()
bool CompositedLayerMapping::VerifyLayerInSquashingVector(
    const PaintLayer* layer) {
  for (size_t layer_index = 0; layer_index < squashed_layers_.size();
       ++layer_index) {
    if (squashed_layers_[layer_index].paint_layer == layer)
      return true;
  }

  return false;
}
#endif

void CompositedLayerMapping::FinishAccumulatingSquashingLayers(
    size_t next_squashed_layer_index,
    Vector<PaintLayer*>& layers_needing_paint_invalidation) {
  if (next_squashed_layer_index < squashed_layers_.size()) {
    // Any additional squashed Layers in the array no longer belong here, but
    // they might have been added already at an earlier index. Clear pointers on
    // those that do not appear in the valid set before removing all the extra
    // entries.
    for (size_t i = next_squashed_layer_index; i < squashed_layers_.size();
         ++i) {
      if (InvalidateLayerIfNoPrecedingEntry(i))
        squashed_layers_[i].paint_layer->SetGroupedMapping(
            nullptr, PaintLayer::kDoNotInvalidateLayerAndRemoveFromMapping);
      layers_needing_paint_invalidation.push_back(
          squashed_layers_[i].paint_layer);
    }

    squashed_layers_.erase(next_squashed_layer_index,
                           squashed_layers_.size() - next_squashed_layer_index);
  }
}

String CompositedLayerMapping::DebugName(
    const GraphicsLayer* graphics_layer) const {
  String name;
  if (graphics_layer == graphics_layer_.get()) {
    name = owning_layer_.DebugName();
  } else if (graphics_layer == squashing_containment_layer_.get()) {
    name = "Squashing Containment Layer";
  } else if (graphics_layer == squashing_layer_.get()) {
    name = "Squashing Layer (first squashed layer: " +
           (squashed_layers_.size() > 0
                ? squashed_layers_[0].paint_layer->DebugName()
                : "") +
           ")";
  } else if (graphics_layer == ancestor_clipping_layer_.get()) {
    name = "Ancestor Clipping Layer";
  } else if (graphics_layer == ancestor_clipping_mask_layer_.get()) {
    name = "Ancestor Clipping Mask Layer";
  } else if (graphics_layer == foreground_layer_.get()) {
    name = owning_layer_.DebugName() + " (foreground) Layer";
  } else if (graphics_layer == background_layer_.get()) {
    name = owning_layer_.DebugName() + " (background) Layer";
  } else if (graphics_layer == child_containment_layer_.get()) {
    name = "Child Containment Layer";
  } else if (graphics_layer == child_transform_layer_.get()) {
    name = "Child Transform Layer";
  } else if (graphics_layer == mask_layer_.get()) {
    name = "Mask Layer";
  } else if (graphics_layer == child_clipping_mask_layer_.get()) {
    name = "Child Clipping Mask Layer";
  } else if (graphics_layer == layer_for_horizontal_scrollbar_.get()) {
    name = "Horizontal Scrollbar Layer";
  } else if (graphics_layer == layer_for_vertical_scrollbar_.get()) {
    name = "Vertical Scrollbar Layer";
  } else if (graphics_layer == layer_for_scroll_corner_.get()) {
    name = "Scroll Corner Layer";
  } else if (graphics_layer == overflow_controls_host_layer_.get()) {
    name = "Overflow Controls Host Layer";
  } else if (graphics_layer ==
             overflow_controls_ancestor_clipping_layer_.get()) {
    name = "Overflow Controls Ancestor Clipping Layer";
  } else if (graphics_layer == scrolling_layer_.get()) {
    name = "Scrolling Layer";
  } else if (graphics_layer == scrolling_contents_layer_.get()) {
    name = "Scrolling Contents Layer";
  } else if (graphics_layer == decoration_outline_layer_.get()) {
    name = "Decoration Layer";
  } else {
    NOTREACHED();
  }

  return name;
}

}  // namespace blink
