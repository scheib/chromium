// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/paint/ClipPathClipper.h"

#include "core/dom/ElementTraversal.h"
#include "core/layout/svg/LayoutSVGResourceClipper.h"
#include "core/layout/svg/SVGLayoutSupport.h"
#include "core/layout/svg/SVGResources.h"
#include "core/layout/svg/SVGResourcesCache.h"
#include "core/paint/LayoutObjectDrawingRecorder.h"
#include "core/paint/PaintInfo.h"
#include "core/paint/TransformRecorder.h"
#include "core/style/ClipPathOperation.h"
#include "platform/graphics/paint/ClipPathDisplayItem.h"
#include "platform/graphics/paint/ClipPathRecorder.h"
#include "platform/graphics/paint/DrawingDisplayItem.h"
#include "platform/graphics/paint/PaintController.h"
#include "platform/graphics/paint/PaintRecordBuilder.h"

namespace blink {

namespace {

class SVGClipExpansionCycleHelper {
 public:
  SVGClipExpansionCycleHelper(LayoutSVGResourceClipper& clip) : clip_(clip) {
    clip.BeginClipExpansion();
  }
  ~SVGClipExpansionCycleHelper() { clip_.EndClipExpansion(); }

 private:
  LayoutSVGResourceClipper& clip_;
};

LayoutSVGResourceClipper* ResolveElementReference(
    const LayoutObject& layout_object,
    const ReferenceClipPathOperation& reference_clip_path_operation) {
  if (layout_object.IsSVGChild()) {
    // The reference will have been resolved in
    // SVGResources::buildResources, so we can just use the LayoutObject's
    // SVGResources.
    SVGResources* resources =
        SVGResourcesCache::CachedResourcesForLayoutObject(&layout_object);
    return resources ? resources->Clipper() : nullptr;
  }
  // TODO(fs): Doesn't work with external SVG references (crbug.com/109212.)
  Node* target_node = layout_object.GetNode();
  if (!target_node)
    return nullptr;
  SVGElement* element =
      reference_clip_path_operation.FindElement(target_node->GetTreeScope());
  if (!isSVGClipPathElement(element) || !element->GetLayoutObject())
    return nullptr;
  return ToLayoutSVGResourceClipper(
      ToLayoutSVGResourceContainer(element->GetLayoutObject()));
}

}  // namespace

ClipPathClipper::ClipPathClipper(GraphicsContext& context,
                                 ClipPathOperation& clip_path_operation,
                                 const LayoutObject& layout_object,
                                 const FloatRect& reference_box,
                                 const FloatPoint& origin)
    : resource_clipper_(nullptr),
      clipper_state_(ClipperState::kNotApplied),
      layout_object_(layout_object),
      context_(context) {
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled())
    return;
  if (clip_path_operation.GetType() == ClipPathOperation::SHAPE) {
    ShapeClipPathOperation& shape =
        ToShapeClipPathOperation(clip_path_operation);
    if (!shape.IsValid())
      return;
    clip_path_recorder_.emplace(context, layout_object,
                                shape.GetPath(reference_box));
    clipper_state_ = ClipperState::kAppliedPath;
  } else {
    DCHECK_EQ(clip_path_operation.GetType(), ClipPathOperation::REFERENCE);
    resource_clipper_ = ResolveElementReference(
        layout_object, ToReferenceClipPathOperation(clip_path_operation));
    if (!resource_clipper_)
      return;
    // Compute the (conservative) bounds of the clip-path.
    FloatRect clip_path_bounds =
        resource_clipper_->ResourceBoundingBox(reference_box);
    // When SVG applies the clip, and the coordinate system is "userspace on
    // use", we must explicitly pass in the offset to have the clip paint in the
    // correct location. When the coordinate system is "object bounding box" the
    // offset should already be accounted for in the reference box.
    FloatPoint origin_translation;
    if (resource_clipper_->ClipPathUnits() ==
        SVGUnitTypes::kSvgUnitTypeUserspaceonuse) {
      clip_path_bounds.MoveBy(origin);
      origin_translation = origin;
    }
    if (!PrepareEffect(reference_box, clip_path_bounds, origin_translation)) {
      // Indicate there is no cleanup to do.
      resource_clipper_ = nullptr;
      return;
    }
  }
}

ClipPathClipper::~ClipPathClipper() {
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled())
    return;
  if (resource_clipper_)
    FinishEffect();
}

bool ClipPathClipper::PrepareEffect(const FloatRect& target_bounding_box,
                                    const FloatRect& visual_rect,
                                    const FloatPoint& layer_position_offset) {
  DCHECK_EQ(clipper_state_, ClipperState::kNotApplied);
  SECURITY_DCHECK(!resource_clipper_->NeedsLayout());

  resource_clipper_->ClearInvalidationMask();

  if (resource_clipper_->HasCycle())
    return false;

  SVGClipExpansionCycleHelper in_clip_expansion_change(*resource_clipper_);

  AffineTransform animated_local_transform =
      toSVGClipPathElement(resource_clipper_->GetElement())
          ->CalculateTransform(SVGElement::kIncludeMotionTransform);
  // When drawing a clip for non-SVG elements, the CTM does not include the zoom
  // factor.  In this case, we need to apply the zoom scale explicitly - but
  // only for clips with userSpaceOnUse units (the zoom is accounted for
  // objectBoundingBox-resolved lengths).
  if (!layout_object_.IsSVG() && resource_clipper_->ClipPathUnits() ==
                                     SVGUnitTypes::kSvgUnitTypeUserspaceonuse) {
    DCHECK(resource_clipper_->Style());
    animated_local_transform.Scale(resource_clipper_->Style()->EffectiveZoom());
  }

  // First, try to apply the clip as a clipPath.
  Path clip_path;
  if (resource_clipper_->AsPath(animated_local_transform, target_bounding_box,
                                clip_path)) {
    AffineTransform position_transform;
    position_transform.Translate(layer_position_offset.X(),
                                 layer_position_offset.Y());
    clip_path.Transform(position_transform);
    clipper_state_ = ClipperState::kAppliedPath;
    context_.GetPaintController().CreateAndAppend<BeginClipPathDisplayItem>(
        layout_object_, clip_path);
    return true;
  }

  // Fall back to masking.
  clipper_state_ = ClipperState::kAppliedMask;

  // Begin compositing the clip mask.
  mask_clip_recorder_.emplace(context_, layout_object_, SkBlendMode::kSrcOver,
                              1, &visual_rect);
  {
    if (!DrawClipAsMask(target_bounding_box, visual_rect,
                        animated_local_transform, layer_position_offset)) {
      // End the clip mask's compositor.
      mask_clip_recorder_.reset();
      return false;
    }
  }

  // Masked content layer start.
  mask_content_recorder_.emplace(context_, layout_object_, SkBlendMode::kSrcIn,
                                 1, &visual_rect);

  return true;
}

bool ClipPathClipper::DrawClipAsMask(const FloatRect& target_bounding_box,
                                     const FloatRect& target_visual_rect,
                                     const AffineTransform& local_transform,
                                     const FloatPoint& layer_position_offset) {
  if (LayoutObjectDrawingRecorder::UseCachedDrawingIfPossible(
          context_, layout_object_, DisplayItem::kSVGClip))
    return true;

  PaintRecordBuilder mask_builder(target_visual_rect, nullptr, &context_);
  GraphicsContext& mask_context = mask_builder.Context();
  {
    TransformRecorder recorder(mask_context, layout_object_, local_transform);

    // Apply any clip-path clipping this clipPath (nested shape/clipPath.)
    Optional<ClipPathClipper> nested_clip_path_clipper;
    if (ClipPathOperation* clip_path_operation =
            resource_clipper_->StyleRef().ClipPath()) {
      nested_clip_path_clipper.emplace(mask_context, *clip_path_operation,
                                       *resource_clipper_, target_bounding_box,
                                       layer_position_offset);
    }

    {
      AffineTransform content_transform;
      if (resource_clipper_->ClipPathUnits() ==
          SVGUnitTypes::kSvgUnitTypeObjectboundingbox) {
        content_transform.Translate(target_bounding_box.X(),
                                    target_bounding_box.Y());
        content_transform.ScaleNonUniform(target_bounding_box.Width(),
                                          target_bounding_box.Height());
      }
      SubtreeContentTransformScope content_transform_scope(content_transform);

      TransformRecorder content_transform_recorder(mask_context, layout_object_,
                                                   content_transform);
      mask_context.GetPaintController().CreateAndAppend<DrawingDisplayItem>(
          layout_object_, DisplayItem::kSVGClip,
          resource_clipper_->CreatePaintRecord());
    }
  }

  LayoutObjectDrawingRecorder drawing_recorder(
      context_, layout_object_, DisplayItem::kSVGClip, target_visual_rect);
  context_.DrawRecord(mask_builder.EndRecording());
  return true;
}

void ClipPathClipper::FinishEffect() {
  switch (clipper_state_) {
    case ClipperState::kAppliedPath:
      // Path-only clipping, no layers to restore but we need to emit an end to
      // the clip path display item.
      context_.GetPaintController().EndItem<EndClipPathDisplayItem>(
          layout_object_);
      break;
    case ClipperState::kAppliedMask:
      // Transfer content -> clip mask (SrcIn)
      mask_content_recorder_.reset();

      // Transfer clip mask -> bg (SrcOver)
      mask_clip_recorder_.reset();
      break;
    case ClipperState::kNotApplied:
      NOTREACHED();
      break;
  }
}

}  // namespace blink
