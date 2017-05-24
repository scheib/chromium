/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "web/LinkHighlightImpl.h"

#include <memory>
#include "core/dom/DOMNodeIds.h"
#include "core/dom/LayoutTreeBuilderTraversal.h"
#include "core/dom/Node.h"
#include "core/exported/WebViewBase.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/layout/LayoutBoxModelObject.h"
#include "core/layout/LayoutObject.h"
#include "core/layout/compositing/CompositedLayerMapping.h"
#include "core/paint/PaintLayer.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/animation/CompositorAnimation.h"
#include "platform/animation/CompositorAnimationCurve.h"
#include "platform/animation/CompositorFloatAnimationCurve.h"
#include "platform/animation/CompositorTargetProperty.h"
#include "platform/animation/TimingFunction.h"
#include "platform/graphics/Color.h"
#include "platform/graphics/CompositorElementId.h"
#include "platform/graphics/CompositorMutableProperties.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/paint/DrawingRecorder.h"
#include "platform/graphics/paint/PaintCanvas.h"
#include "platform/graphics/paint/PaintRecorder.h"
#include "platform/wtf/CurrentTime.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/Vector.h"
#include "public/platform/Platform.h"
#include "public/platform/WebCompositorSupport.h"
#include "public/platform/WebContentLayer.h"
#include "public/platform/WebDisplayItemList.h"
#include "public/platform/WebFloatPoint.h"
#include "public/platform/WebLayer.h"
#include "public/platform/WebRect.h"
#include "public/platform/WebSize.h"
#include "public/web/WebKit.h"
#include "third_party/skia/include/core/SkMatrix44.h"
#include "ui/gfx/geometry/rect.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebSettingsImpl.h"

namespace blink {

std::unique_ptr<LinkHighlightImpl> LinkHighlightImpl::Create(
    Node* node,
    WebViewBase* owning_web_view) {
  return WTF::WrapUnique(new LinkHighlightImpl(node, owning_web_view));
}

LinkHighlightImpl::LinkHighlightImpl(Node* node, WebViewBase* owning_web_view)
    : node_(node),
      owning_web_view_(owning_web_view),
      current_graphics_layer_(0),
      is_scrolling_graphics_layer_(false),
      geometry_needs_update_(false),
      is_animating_(false),
      start_time_(MonotonicallyIncreasingTime()) {
  DCHECK(node_);
  DCHECK(owning_web_view);
  WebCompositorSupport* compositor_support =
      Platform::Current()->CompositorSupport();
  DCHECK(compositor_support);
  content_layer_ = compositor_support->CreateContentLayer(this);
  clip_layer_ = compositor_support->CreateLayer();
  clip_layer_->SetTransformOrigin(WebFloatPoint3D());
  clip_layer_->AddChild(content_layer_->Layer());

  compositor_player_ = CompositorAnimationPlayer::Create();
  DCHECK(compositor_player_);
  compositor_player_->SetAnimationDelegate(this);
  if (owning_web_view_->LinkHighlightsTimeline())
    owning_web_view_->LinkHighlightsTimeline()->PlayerAttached(*this);

  CompositorElementId element_id = CompositorElementIdFromDOMNodeId(
      DOMNodeIds::IdForNode(node),
      CompositorElementIdNamespace::kLinkHighlight);
  compositor_player_->AttachElement(element_id);
  content_layer_->Layer()->SetDrawsContent(true);
  content_layer_->Layer()->SetOpacity(1);
  content_layer_->Layer()->SetElementId(element_id);
  geometry_needs_update_ = true;
}

LinkHighlightImpl::~LinkHighlightImpl() {
  if (compositor_player_->IsElementAttached())
    compositor_player_->DetachElement();
  if (owning_web_view_->LinkHighlightsTimeline())
    owning_web_view_->LinkHighlightsTimeline()->PlayerDestroyed(*this);
  compositor_player_->SetAnimationDelegate(nullptr);
  compositor_player_.reset();

  ClearGraphicsLayerLinkHighlightPointer();
  ReleaseResources();
}

WebContentLayer* LinkHighlightImpl::ContentLayer() {
  return content_layer_.get();
}

WebLayer* LinkHighlightImpl::ClipLayer() {
  return clip_layer_.get();
}

void LinkHighlightImpl::ReleaseResources() {
  node_.Clear();
}

void LinkHighlightImpl::AttachLinkHighlightToCompositingLayer(
    const LayoutBoxModelObject& paint_invalidation_container) {
  GraphicsLayer* new_graphics_layer =
      paint_invalidation_container.Layer()->GraphicsLayerBacking(
          node_->GetLayoutObject());
  is_scrolling_graphics_layer_ = false;
  // FIXME: There should always be a GraphicsLayer. See crbug.com/431961.
  if (paint_invalidation_container.Layer()->NeedsCompositedScrolling() &&
      node_->GetLayoutObject() != &paint_invalidation_container) {
    is_scrolling_graphics_layer_ = true;
  }
  if (!new_graphics_layer)
    return;

  clip_layer_->SetTransform(SkMatrix44(SkMatrix44::kIdentity_Constructor));

  if (current_graphics_layer_ != new_graphics_layer) {
    if (current_graphics_layer_)
      ClearGraphicsLayerLinkHighlightPointer();

    current_graphics_layer_ = new_graphics_layer;
    current_graphics_layer_->AddLinkHighlight(this);
  }
}

static void AddQuadToPath(const FloatQuad& quad, Path& path) {
  // FIXME: Make this create rounded quad-paths, just like the axis-aligned
  // case.
  path.MoveTo(quad.P1());
  path.AddLineTo(quad.P2());
  path.AddLineTo(quad.P3());
  path.AddLineTo(quad.P4());
  path.CloseSubpath();
}

void LinkHighlightImpl::ComputeQuads(const Node& node,
                                     Vector<FloatQuad>& out_quads) const {
  if (!node.GetLayoutObject())
    return;

  LayoutObject* layout_object = node.GetLayoutObject();

  // For inline elements, absoluteQuads will return a line box based on the
  // line-height and font metrics, which is technically incorrect as replaced
  // elements like images should use their intristic height and expand the
  // linebox  as needed. To get an appropriately sized highlight we descend
  // into the children and have them add their boxes.
  if (layout_object->IsLayoutInline()) {
    for (Node* child = LayoutTreeBuilderTraversal::FirstChild(node); child;
         child = LayoutTreeBuilderTraversal::NextSibling(*child))
      ComputeQuads(*child, out_quads);
  } else {
    // FIXME: this does not need to be absolute, just in the paint invalidation
    // container's space.
    layout_object->AbsoluteQuads(out_quads, kTraverseDocumentBoundaries);
  }
}

bool LinkHighlightImpl::ComputeHighlightLayerPathAndPosition(
    const LayoutBoxModelObject& paint_invalidation_container) {
  if (!node_ || !node_->GetLayoutObject() || !current_graphics_layer_)
    return false;

  // FIXME: This is defensive code to avoid crashes such as those described in
  // crbug.com/440887. This should be cleaned up once we fix the root cause of
  // of the paint invalidation container not being composited.
  if (!paint_invalidation_container.Layer()->GetCompositedLayerMapping() &&
      !paint_invalidation_container.Layer()->GroupedMapping())
    return false;

  // Get quads for node in absolute coordinates.
  Vector<FloatQuad> quads;
  ComputeQuads(*node_, quads);
  DCHECK(quads.size());
  Path new_path;

  for (size_t quad_index = 0; quad_index < quads.size(); ++quad_index) {
    FloatQuad absolute_quad = quads[quad_index];

    // Scrolling content layers have the same offset from layout object as the
    // non-scrolling layers. Thus we need to adjust for their scroll offset.
    if (is_scrolling_graphics_layer_) {
      FloatPoint scroll_position = paint_invalidation_container.Layer()
                                       ->GetScrollableArea()
                                       ->ScrollPosition();
      absolute_quad.Move(ToScrollOffset(scroll_position));
    }

    absolute_quad.SetP1(RoundedIntPoint(absolute_quad.P1()));
    absolute_quad.SetP2(RoundedIntPoint(absolute_quad.P2()));
    absolute_quad.SetP3(RoundedIntPoint(absolute_quad.P3()));
    absolute_quad.SetP4(RoundedIntPoint(absolute_quad.P4()));
    FloatQuad transformed_quad =
        paint_invalidation_container.AbsoluteToLocalQuad(
            absolute_quad, kUseTransforms | kTraverseDocumentBoundaries);
    FloatPoint offset_to_backing;

    PaintLayer::MapPointInPaintInvalidationContainerToBacking(
        paint_invalidation_container, offset_to_backing);

    // Adjust for offset from LayoutObject.
    offset_to_backing.Move(-current_graphics_layer_->OffsetFromLayoutObject());

    transformed_quad.Move(ToFloatSize(offset_to_backing));

    // FIXME: for now, we'll only use rounded paths if we have a single node
    // quad. The reason for this is that we may sometimes get a chain of
    // adjacent boxes (e.g. for text nodes) which end up looking like sausage
    // links: these should ideally be merged into a single rect before creating
    // the path, but that's another CL.
    if (quads.size() == 1 && transformed_quad.IsRectilinear() &&
        !owning_web_view_->SettingsImpl()->MockGestureTapHighlightsEnabled()) {
      FloatSize rect_rounding_radii(3, 3);
      new_path.AddRoundedRect(transformed_quad.BoundingBox(),
                              rect_rounding_radii);
    } else {
      AddQuadToPath(transformed_quad, new_path);
    }
  }

  FloatRect bounding_rect = new_path.BoundingRect();
  new_path.Translate(-ToFloatSize(bounding_rect.Location()));

  bool path_has_changed = !(new_path == path_);
  if (path_has_changed) {
    path_ = new_path;
    content_layer_->Layer()->SetBounds(EnclosingIntRect(bounding_rect).Size());
  }

  content_layer_->Layer()->SetPosition(bounding_rect.Location());

  return path_has_changed;
}

gfx::Rect LinkHighlightImpl::PaintableRegion() {
  return gfx::Rect(0, 0, ContentLayer()->Layer()->Bounds().width,
                   ContentLayer()->Layer()->Bounds().height);
}

void LinkHighlightImpl::PaintContents(
    WebDisplayItemList* web_display_item_list,
    WebContentLayerClient::PaintingControlSetting painting_control) {
  if (!node_ || !node_->GetLayoutObject())
    return;

  PaintRecorder recorder;
  gfx::Rect visual_rect = PaintableRegion();
  PaintCanvas* canvas =
      recorder.beginRecording(visual_rect.width(), visual_rect.height());

  PaintFlags flags;
  flags.setStyle(PaintFlags::kFill_Style);
  flags.setAntiAlias(true);
  flags.setColor(node_->GetLayoutObject()->Style()->TapHighlightColor().Rgb());
  canvas->drawPath(path_.GetSkPath(), flags);

  web_display_item_list->AppendDrawingItem(
      WebRect(visual_rect.x(), visual_rect.y(), visual_rect.width(),
              visual_rect.height()),
      recorder.finishRecordingAsPicture());
}

void LinkHighlightImpl::StartHighlightAnimationIfNeeded() {
  if (is_animating_)
    return;

  is_animating_ = true;
  const float kStartOpacity = 1;
  // FIXME: Should duration be configurable?
  const float kFadeDuration = 0.1f;
  const float kMinPreFadeDuration = 0.1f;

  content_layer_->Layer()->SetOpacity(kStartOpacity);

  std::unique_ptr<CompositorFloatAnimationCurve> curve =
      CompositorFloatAnimationCurve::Create();

  const auto& timing_function = *CubicBezierTimingFunction::Preset(
      CubicBezierTimingFunction::EaseType::EASE);

  curve->AddKeyframe(
      CompositorFloatKeyframe(0, kStartOpacity, timing_function));
  // Make sure we have displayed for at least minPreFadeDuration before starting
  // to fade out.
  float extra_duration_required = std::max(
      0.f, kMinPreFadeDuration -
               static_cast<float>(MonotonicallyIncreasingTime() - start_time_));
  if (extra_duration_required)
    curve->AddKeyframe(CompositorFloatKeyframe(extra_duration_required,
                                               kStartOpacity, timing_function));
  // For layout tests we don't fade out.
  curve->AddKeyframe(CompositorFloatKeyframe(
      kFadeDuration + extra_duration_required,
      LayoutTestMode() ? kStartOpacity : 0, timing_function));

  std::unique_ptr<CompositorAnimation> animation = CompositorAnimation::Create(
      *curve, CompositorTargetProperty::OPACITY, 0, 0);

  content_layer_->Layer()->SetDrawsContent(true);
  compositor_player_->AddAnimation(std::move(animation));

  Invalidate();
  owning_web_view_->MainFrameImpl()->FrameWidget()->ScheduleAnimation();
}

void LinkHighlightImpl::ClearGraphicsLayerLinkHighlightPointer() {
  if (current_graphics_layer_) {
    current_graphics_layer_->RemoveLinkHighlight(this);
    current_graphics_layer_ = 0;
  }
}

void LinkHighlightImpl::NotifyAnimationStarted(double, int) {}

void LinkHighlightImpl::NotifyAnimationFinished(double, int) {
  // Since WebViewImpl may hang on to us for a while, make sure we
  // release resources as soon as possible.
  ClearGraphicsLayerLinkHighlightPointer();
  ReleaseResources();
}

class LinkHighlightDisplayItemClientForTracking : public DisplayItemClient {
  String DebugName() const final { return "LinkHighlight"; }
  LayoutRect VisualRect() const final { return LayoutRect(); }
};

void LinkHighlightImpl::UpdateGeometry() {
  // To avoid unnecessary updates (e.g. other entities have requested animations
  // from our WebViewImpl), only proceed if we actually requested an update.
  if (!geometry_needs_update_)
    return;

  geometry_needs_update_ = false;

  bool has_layout_object = node_ && node_->GetLayoutObject();
  if (has_layout_object) {
    const LayoutBoxModelObject& paint_invalidation_container =
        node_->GetLayoutObject()->ContainerForPaintInvalidation();
    AttachLinkHighlightToCompositingLayer(paint_invalidation_container);
    if (ComputeHighlightLayerPathAndPosition(paint_invalidation_container)) {
      // We only need to invalidate the layer if the highlight size has changed,
      // otherwise we can just re-position the layer without needing to
      // repaint.
      content_layer_->Layer()->Invalidate();

      if (current_graphics_layer_) {
        current_graphics_layer_->TrackRasterInvalidation(
            LinkHighlightDisplayItemClientForTracking(),
            EnclosingIntRect(
                FloatRect(Layer()->GetPosition().x, Layer()->GetPosition().y,
                          Layer()->Bounds().width, Layer()->Bounds().height)),
            PaintInvalidationReason::kFull);
      }
    }
  } else {
    ClearGraphicsLayerLinkHighlightPointer();
    ReleaseResources();
  }
}

void LinkHighlightImpl::ClearCurrentGraphicsLayer() {
  current_graphics_layer_ = 0;
  geometry_needs_update_ = true;
}

void LinkHighlightImpl::Invalidate() {
  // Make sure we update geometry on the next callback from
  // WebViewImpl::layout().
  geometry_needs_update_ = true;
}

WebLayer* LinkHighlightImpl::Layer() {
  return ClipLayer();
}

CompositorAnimationPlayer* LinkHighlightImpl::CompositorPlayer() const {
  return compositor_player_.get();
}

}  // namespace blink
