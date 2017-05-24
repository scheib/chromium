/*
 * Copyright (C) 2009 Apple Inc. All rights reserved.
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

#include "platform/graphics/GraphicsLayer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include "SkImageFilter.h"
#include "SkMatrix44.h"
#include "base/trace_event/trace_event_argument.h"
#include "cc/layers/layer.h"
#include "platform/DragImage.h"
#include "platform/geometry/FloatRect.h"
#include "platform/geometry/LayoutRect.h"
#include "platform/geometry/Region.h"
#include "platform/graphics/BitmapImage.h"
#include "platform/graphics/CompositorFilterOperations.h"
#include "platform/graphics/FirstPaintInvalidationTracking.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/Image.h"
#include "platform/graphics/LinkHighlight.h"
#include "platform/graphics/paint/DrawingRecorder.h"
#include "platform/graphics/paint/PaintCanvas.h"
#include "platform/graphics/paint/PaintController.h"
#include "platform/graphics/paint/RasterInvalidationTracking.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/json/JSONValues.h"
#include "platform/scroll/ScrollableArea.h"
#include "platform/text/TextStream.h"
#include "platform/wtf/CurrentTime.h"
#include "platform/wtf/HashMap.h"
#include "platform/wtf/HashSet.h"
#include "platform/wtf/MathExtras.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/text/StringUTF8Adaptor.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/Platform.h"
#include "public/platform/WebCompositorSupport.h"
#include "public/platform/WebFloatPoint.h"
#include "public/platform/WebFloatRect.h"
#include "public/platform/WebLayer.h"
#include "public/platform/WebPoint.h"
#include "public/platform/WebSize.h"

#ifndef NDEBUG
#include <stdio.h>
#endif

namespace blink {

template class RasterInvalidationTrackingMap<const GraphicsLayer>;
static RasterInvalidationTrackingMap<const GraphicsLayer>&
GetRasterInvalidationTrackingMap() {
  DEFINE_STATIC_LOCAL(RasterInvalidationTrackingMap<const GraphicsLayer>, map,
                      ());
  return map;
}

std::unique_ptr<GraphicsLayer> GraphicsLayer::Create(
    GraphicsLayerClient* client) {
  return WTF::WrapUnique(new GraphicsLayer(client));
}

GraphicsLayer::GraphicsLayer(GraphicsLayerClient* client)
    : client_(client),
      background_color_(Color::kTransparent),
      opacity_(1),
      blend_mode_(kWebBlendModeNormal),
      has_transform_origin_(false),
      contents_opaque_(false),
      should_flatten_transform_(true),
      backface_visibility_(true),
      draws_content_(false),
      contents_visible_(true),
      is_root_for_isolated_group_(false),
      has_scroll_parent_(false),
      has_clip_parent_(false),
      painted_(false),
      is_tracking_raster_invalidations_(
          client && client->IsTrackingRasterInvalidations()),
      painting_phase_(kGraphicsLayerPaintAllWithOverflowClip),
      parent_(0),
      mask_layer_(0),
      contents_clipping_mask_layer_(0),
      paint_count_(0),
      contents_layer_(0),
      contents_layer_id_(0),
      scrollable_area_(nullptr),
      rendering_context3d_(0) {
#if DCHECK_IS_ON()
  if (client_)
    client_->VerifyNotPainting();
#endif
  content_layer_delegate_ = WTF::MakeUnique<ContentLayerDelegate>(this);
  layer_ = Platform::Current()->CompositorSupport()->CreateContentLayer(
      content_layer_delegate_.get());
  layer_->Layer()->SetDrawsContent(draws_content_ && contents_visible_);
  layer_->Layer()->SetLayerClient(this);
}

GraphicsLayer::~GraphicsLayer() {
  for (size_t i = 0; i < link_highlights_.size(); ++i)
    link_highlights_[i]->ClearCurrentGraphicsLayer();
  link_highlights_.clear();

#if DCHECK_IS_ON()
  if (client_)
    client_->VerifyNotPainting();
#endif

  RemoveAllChildren();
  RemoveFromParent();

  GetRasterInvalidationTrackingMap().Remove(this);
  DCHECK(!parent_);
}

LayoutRect GraphicsLayer::VisualRect() const {
  LayoutRect bounds = LayoutRect(FloatPoint(), Size());
  bounds.Move(OffsetFromLayoutObjectWithSubpixelAccumulation());
  return bounds;
}

void GraphicsLayer::SetHasWillChangeTransformHint(
    bool has_will_change_transform) {
  layer_->Layer()->SetHasWillChangeTransformHint(has_will_change_transform);
}

void GraphicsLayer::SetParent(GraphicsLayer* layer) {
#if DCHECK_IS_ON()
  DCHECK(!layer || !layer->HasAncestor(this));
#endif
  parent_ = layer;
}

#if DCHECK_IS_ON()

bool GraphicsLayer::HasAncestor(GraphicsLayer* ancestor) const {
  for (GraphicsLayer* curr = Parent(); curr; curr = curr->Parent()) {
    if (curr == ancestor)
      return true;
  }

  return false;
}

#endif

bool GraphicsLayer::SetChildren(const GraphicsLayerVector& new_children) {
  // If the contents of the arrays are the same, nothing to do.
  if (new_children == children_)
    return false;

  RemoveAllChildren();

  size_t list_size = new_children.size();
  for (size_t i = 0; i < list_size; ++i)
    AddChildInternal(new_children[i]);

  UpdateChildList();

  return true;
}

void GraphicsLayer::AddChildInternal(GraphicsLayer* child_layer) {
  DCHECK_NE(child_layer, this);

  if (child_layer->Parent())
    child_layer->RemoveFromParent();

  child_layer->SetParent(this);
  children_.push_back(child_layer);

  // Don't call updateChildList here, this function is used in cases where it
  // should not be called until all children are processed.
}

void GraphicsLayer::AddChild(GraphicsLayer* child_layer) {
  AddChildInternal(child_layer);
  UpdateChildList();
}

void GraphicsLayer::AddChildBelow(GraphicsLayer* child_layer,
                                  GraphicsLayer* sibling) {
  DCHECK_NE(child_layer, this);
  child_layer->RemoveFromParent();

  bool found = false;
  for (unsigned i = 0; i < children_.size(); i++) {
    if (sibling == children_[i]) {
      children_.insert(i, child_layer);
      found = true;
      break;
    }
  }

  child_layer->SetParent(this);

  if (!found)
    children_.push_back(child_layer);

  UpdateChildList();
}

void GraphicsLayer::RemoveAllChildren() {
  while (!children_.IsEmpty()) {
    GraphicsLayer* cur_layer = children_.back();
    DCHECK(cur_layer->Parent());
    cur_layer->RemoveFromParent();
  }
}

void GraphicsLayer::RemoveFromParent() {
  if (parent_) {
    // We use reverseFind so that removeAllChildren() isn't n^2.
    parent_->children_.erase(parent_->children_.ReverseFind(this));
    SetParent(0);
  }

  PlatformLayer()->RemoveFromParent();
}

void GraphicsLayer::SetOffsetFromLayoutObject(
    const IntSize& offset,
    ShouldSetNeedsDisplay should_set_needs_display) {
  SetOffsetDoubleFromLayoutObject(offset);
}

void GraphicsLayer::SetOffsetDoubleFromLayoutObject(
    const DoubleSize& offset,
    ShouldSetNeedsDisplay should_set_needs_display) {
  if (offset == offset_from_layout_object_)
    return;

  offset_from_layout_object_ = offset;
  PlatformLayer()->SetFiltersOrigin(FloatPoint() - ToFloatSize(offset));

  // If the compositing layer offset changes, we need to repaint.
  if (should_set_needs_display == kSetNeedsDisplay)
    SetNeedsDisplay();
}

LayoutSize GraphicsLayer::OffsetFromLayoutObjectWithSubpixelAccumulation()
    const {
  return LayoutSize(OffsetFromLayoutObject()) +
         Client()->SubpixelAccumulation();
}

IntRect GraphicsLayer::InterestRect() {
  return previous_interest_rect_;
}

void GraphicsLayer::Paint(const IntRect* interest_rect,
                          GraphicsContext::DisabledMode disabled_mode) {
  if (PaintWithoutCommit(interest_rect, disabled_mode)) {
    GetPaintController().CommitNewDisplayItems(
        OffsetFromLayoutObjectWithSubpixelAccumulation());
    if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled()) {
      sk_sp<PaintRecord> record = CaptureRecord();
      CheckPaintUnderInvalidations(record);
      RasterInvalidationTracking& tracking =
          GetRasterInvalidationTrackingMap().Add(this);
      tracking.last_painted_record = std::move(record);
      tracking.last_interest_rect = previous_interest_rect_;
      tracking.invalidation_region_since_last_paint = Region();
    }
  }
}

bool GraphicsLayer::PaintWithoutCommit(
    const IntRect* interest_rect,
    GraphicsContext::DisabledMode disabled_mode) {
  DCHECK(DrawsContent());

  if (!client_)
    return false;
  if (FirstPaintInvalidationTracking::IsEnabled())
    debug_info_.ClearAnnotatedInvalidateRects();
  IncrementPaintCount();

  IntRect new_interest_rect;
  if (!interest_rect) {
    new_interest_rect =
        client_->ComputeInterestRect(this, previous_interest_rect_);
    interest_rect = &new_interest_rect;
  }

  if (!GetPaintController().SubsequenceCachingIsDisabled() &&
      !client_->NeedsRepaint(*this) && !GetPaintController().CacheIsEmpty() &&
      previous_interest_rect_ == *interest_rect) {
    return false;
  }

  GraphicsContext context(GetPaintController(), disabled_mode, nullptr);

  previous_interest_rect_ = *interest_rect;
  client_->PaintContents(this, context, painting_phase_, *interest_rect);
  return true;
}

void GraphicsLayer::UpdateChildList() {
  WebLayer* child_host = layer_->Layer();
  child_host->RemoveAllChildren();

  ClearContentsLayerIfUnregistered();

  if (contents_layer_) {
    // FIXME: Add the contents layer in the correct order with negative z-order
    // children. This does not currently cause visible rendering issues because
    // contents layers are only used for replaced elements that don't have
    // children.
    child_host->AddChild(contents_layer_);
  }

  for (size_t i = 0; i < children_.size(); ++i)
    child_host->AddChild(children_[i]->PlatformLayer());

  for (size_t i = 0; i < link_highlights_.size(); ++i)
    child_host->AddChild(link_highlights_[i]->Layer());
}

void GraphicsLayer::UpdateLayerIsDrawable() {
  // For the rest of the accelerated compositor code, there is no reason to make
  // a distinction between drawsContent and contentsVisible. So, for
  // m_layer->layer(), these two flags are combined here. |m_contentsLayer|
  // shouldn't receive the drawsContent flag, so it is only given
  // contentsVisible.

  layer_->Layer()->SetDrawsContent(draws_content_ && contents_visible_);
  if (WebLayer* contents_layer = ContentsLayerIfRegistered())
    contents_layer->SetDrawsContent(contents_visible_);

  if (draws_content_) {
    layer_->Layer()->Invalidate();
    for (size_t i = 0; i < link_highlights_.size(); ++i)
      link_highlights_[i]->Invalidate();
  }
}

void GraphicsLayer::UpdateContentsRect() {
  WebLayer* contents_layer = ContentsLayerIfRegistered();
  if (!contents_layer)
    return;

  contents_layer->SetPosition(
      FloatPoint(contents_rect_.X(), contents_rect_.Y()));
  contents_layer->SetBounds(
      IntSize(contents_rect_.Width(), contents_rect_.Height()));

  if (contents_clipping_mask_layer_) {
    if (contents_clipping_mask_layer_->Size() != contents_rect_.Size()) {
      contents_clipping_mask_layer_->SetSize(FloatSize(contents_rect_.Size()));
      contents_clipping_mask_layer_->SetNeedsDisplay();
    }
    contents_clipping_mask_layer_->SetPosition(FloatPoint());
    contents_clipping_mask_layer_->SetOffsetFromLayoutObject(
        OffsetFromLayoutObject() +
        IntSize(contents_rect_.Location().X(), contents_rect_.Location().Y()));
  }
}

static HashSet<int>* g_registered_layer_set;

void GraphicsLayer::RegisterContentsLayer(WebLayer* layer) {
  if (!g_registered_layer_set)
    g_registered_layer_set = new HashSet<int>;
  if (g_registered_layer_set->Contains(layer->Id()))
    CRASH();
  g_registered_layer_set->insert(layer->Id());
}

void GraphicsLayer::UnregisterContentsLayer(WebLayer* layer) {
  DCHECK(g_registered_layer_set);
  if (!g_registered_layer_set->Contains(layer->Id()))
    CRASH();
  g_registered_layer_set->erase(layer->Id());
}

void GraphicsLayer::SetContentsTo(WebLayer* layer) {
  bool children_changed = false;
  if (layer) {
    DCHECK(g_registered_layer_set);
    if (!g_registered_layer_set->Contains(layer->Id()))
      CRASH();
    if (contents_layer_id_ != layer->Id()) {
      SetupContentsLayer(layer);
      children_changed = true;
    }
    UpdateContentsRect();
  } else {
    if (contents_layer_) {
      children_changed = true;

      // The old contents layer will be removed via updateChildList.
      contents_layer_ = 0;
      contents_layer_id_ = 0;
    }
  }

  if (children_changed)
    UpdateChildList();
}

void GraphicsLayer::SetupContentsLayer(WebLayer* contents_layer) {
  DCHECK(contents_layer);
  contents_layer_ = contents_layer;
  contents_layer_id_ = contents_layer_->Id();

  contents_layer_->SetLayerClient(this);
  contents_layer_->SetTransformOrigin(FloatPoint3D());
  contents_layer_->SetUseParentBackfaceVisibility(true);

  // It is necessary to call setDrawsContent as soon as we receive the new
  // contentsLayer, for the correctness of early exit conditions in
  // setDrawsContent() and setContentsVisible().
  contents_layer_->SetDrawsContent(contents_visible_);

  // Insert the content layer first. Video elements require this, because they
  // have shadow content that must display in front of the video.
  layer_->Layer()->InsertChild(contents_layer_, 0);
  WebLayer* border_web_layer =
      contents_clipping_mask_layer_
          ? contents_clipping_mask_layer_->PlatformLayer()
          : 0;
  contents_layer_->SetMaskLayer(border_web_layer);

  contents_layer_->SetRenderingContext(rendering_context3d_);
}

void GraphicsLayer::ClearContentsLayerIfUnregistered() {
  if (!contents_layer_id_ ||
      g_registered_layer_set->Contains(contents_layer_id_))
    return;

  contents_layer_ = 0;
  contents_layer_id_ = 0;
}

GraphicsLayerDebugInfo& GraphicsLayer::DebugInfo() {
  return debug_info_;
}

WebLayer* GraphicsLayer::ContentsLayerIfRegistered() {
  ClearContentsLayerIfUnregistered();
  return contents_layer_;
}

void GraphicsLayer::SetTracksRasterInvalidations(
    bool tracks_raster_invalidations) {
  ResetTrackedRasterInvalidations();
  is_tracking_raster_invalidations_ = tracks_raster_invalidations;
}

void GraphicsLayer::ResetTrackedRasterInvalidations() {
  RasterInvalidationTracking* tracking =
      GetRasterInvalidationTrackingMap().Find(this);
  if (!tracking)
    return;

  if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled())
    tracking->invalidations.clear();
  else
    GetRasterInvalidationTrackingMap().Remove(this);
}

bool GraphicsLayer::HasTrackedRasterInvalidations() const {
  if (auto* tracking = GetRasterInvalidationTracking())
    return !tracking->invalidations.IsEmpty();
  return false;
}

const RasterInvalidationTracking* GraphicsLayer::GetRasterInvalidationTracking()
    const {
  return GetRasterInvalidationTrackingMap().Find(this);
}

void GraphicsLayer::TrackRasterInvalidation(const DisplayItemClient& client,
                                            const IntRect& rect,
                                            PaintInvalidationReason reason) {
  if (!IsTrackingOrCheckingRasterInvalidations() || rect.IsEmpty())
    return;

  RasterInvalidationTracking& tracking =
      GetRasterInvalidationTrackingMap().Add(this);

  if (is_tracking_raster_invalidations_) {
    RasterInvalidationInfo info;
    info.client = &client;
    info.client_debug_name = client.DebugName();
    info.rect = rect;
    info.reason = reason;
    tracking.invalidations.push_back(info);
  }

  if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled()) {
    // TODO(crbug.com/496260): Some antialiasing effects overflow the paint
    // invalidation rect.
    IntRect r = rect;
    r.Inflate(1);
    tracking.invalidation_region_since_last_paint.Unite(r);
  }
}

template <typename T>
static std::unique_ptr<JSONArray> PointAsJSONArray(const T& point) {
  std::unique_ptr<JSONArray> array = JSONArray::Create();
  array->PushDouble(point.X());
  array->PushDouble(point.Y());
  return array;
}

template <typename T>
static std::unique_ptr<JSONArray> SizeAsJSONArray(const T& size) {
  std::unique_ptr<JSONArray> array = JSONArray::Create();
  array->PushDouble(size.Width());
  array->PushDouble(size.Height());
  return array;
}

static double RoundCloseToZero(double number) {
  return std::abs(number) < 1e-7 ? 0 : number;
}

static std::unique_ptr<JSONArray> TransformAsJSONArray(
    const TransformationMatrix& t) {
  std::unique_ptr<JSONArray> array = JSONArray::Create();
  {
    std::unique_ptr<JSONArray> row = JSONArray::Create();
    row->PushDouble(RoundCloseToZero(t.M11()));
    row->PushDouble(RoundCloseToZero(t.M12()));
    row->PushDouble(RoundCloseToZero(t.M13()));
    row->PushDouble(RoundCloseToZero(t.M14()));
    array->PushArray(std::move(row));
  }
  {
    std::unique_ptr<JSONArray> row = JSONArray::Create();
    row->PushDouble(RoundCloseToZero(t.M21()));
    row->PushDouble(RoundCloseToZero(t.M22()));
    row->PushDouble(RoundCloseToZero(t.M23()));
    row->PushDouble(RoundCloseToZero(t.M24()));
    array->PushArray(std::move(row));
  }
  {
    std::unique_ptr<JSONArray> row = JSONArray::Create();
    row->PushDouble(RoundCloseToZero(t.M31()));
    row->PushDouble(RoundCloseToZero(t.M32()));
    row->PushDouble(RoundCloseToZero(t.M33()));
    row->PushDouble(RoundCloseToZero(t.M34()));
    array->PushArray(std::move(row));
  }
  {
    std::unique_ptr<JSONArray> row = JSONArray::Create();
    row->PushDouble(RoundCloseToZero(t.M41()));
    row->PushDouble(RoundCloseToZero(t.M42()));
    row->PushDouble(RoundCloseToZero(t.M43()));
    row->PushDouble(RoundCloseToZero(t.M44()));
    array->PushArray(std::move(row));
  }
  return array;
}

static String PointerAsString(const void* ptr) {
  TextStream ts;
  ts << ptr;
  return ts.Release();
}

std::unique_ptr<JSONObject> GraphicsLayer::LayerTreeAsJSON(
    LayerTreeFlags flags) const {
  RenderingContextMap rendering_context_map;
  if (flags & kOutputAsLayerTree)
    return LayerTreeAsJSONInternal(flags, rendering_context_map);
  std::unique_ptr<JSONObject> json = JSONObject::Create();
  std::unique_ptr<JSONArray> layers_array = JSONArray::Create();
  for (auto& child : children_)
    child->LayersAsJSONArray(flags, rendering_context_map, layers_array.get());
  json->SetArray("layers", std::move(layers_array));
  return json;
}

std::unique_ptr<JSONObject> GraphicsLayer::LayerAsJSONInternal(
    LayerTreeFlags flags,
    RenderingContextMap& rendering_context_map) const {
  std::unique_ptr<JSONObject> json = JSONObject::Create();

  if (flags & kLayerTreeIncludesDebugInfo)
    json->SetString("this", PointerAsString(this));

  json->SetString("name", DebugName());

  if (position_ != FloatPoint())
    json->SetArray("position", PointAsJSONArray(position_));

  if (flags & kLayerTreeIncludesDebugInfo &&
      offset_from_layout_object_ != DoubleSize()) {
    json->SetArray("offsetFromLayoutObject",
                   SizeAsJSONArray(offset_from_layout_object_));
  }

  if (has_transform_origin_ &&
      transform_origin_ !=
          FloatPoint3D(size_.Width() * 0.5f, size_.Height() * 0.5f, 0))
    json->SetArray("transformOrigin", PointAsJSONArray(transform_origin_));

  if (size_ != IntSize())
    json->SetArray("bounds", SizeAsJSONArray(size_));

  if (opacity_ != 1)
    json->SetDouble("opacity", opacity_);

  if (blend_mode_ != kWebBlendModeNormal) {
    json->SetString("blendMode",
                    CompositeOperatorName(kCompositeSourceOver, blend_mode_));
  }

  if (is_root_for_isolated_group_)
    json->SetBoolean("isolate", is_root_for_isolated_group_);

  if (contents_opaque_)
    json->SetBoolean("contentsOpaque", contents_opaque_);

  if (!should_flatten_transform_)
    json->SetBoolean("shouldFlattenTransform", should_flatten_transform_);

  if (rendering_context3d_) {
    RenderingContextMap::const_iterator it =
        rendering_context_map.find(rendering_context3d_);
    int context_id = rendering_context_map.size() + 1;
    if (it == rendering_context_map.end())
      rendering_context_map.Set(rendering_context3d_, context_id);
    else
      context_id = it->value;

    json->SetInteger("3dRenderingContext", context_id);
  }

  if (draws_content_)
    json->SetBoolean("drawsContent", draws_content_);

  if (!contents_visible_)
    json->SetBoolean("contentsVisible", contents_visible_);

  if (!backface_visibility_) {
    json->SetString("backfaceVisibility",
                    backface_visibility_ ? "visible" : "hidden");
  }

  if (flags & kLayerTreeIncludesDebugInfo)
    json->SetString("client", PointerAsString(client_));

  if (background_color_.Alpha()) {
    json->SetString("backgroundColor",
                    background_color_.NameForLayoutTreeAsText());
  }

  if (!transform_.IsIdentity())
    json->SetArray("transform", TransformAsJSONArray(transform_));

  if (flags & kLayerTreeIncludesPaintInvalidations)
    GetRasterInvalidationTrackingMap().AsJSON(this, json.get());

  if ((flags & kLayerTreeIncludesPaintingPhases) && painting_phase_) {
    std::unique_ptr<JSONArray> painting_phases_json = JSONArray::Create();
    if (painting_phase_ & kGraphicsLayerPaintBackground)
      painting_phases_json->PushString("GraphicsLayerPaintBackground");
    if (painting_phase_ & kGraphicsLayerPaintForeground)
      painting_phases_json->PushString("GraphicsLayerPaintForeground");
    if (painting_phase_ & kGraphicsLayerPaintMask)
      painting_phases_json->PushString("GraphicsLayerPaintMask");
    if (painting_phase_ & kGraphicsLayerPaintChildClippingMask)
      painting_phases_json->PushString("GraphicsLayerPaintChildClippingMask");
    if (painting_phase_ & kGraphicsLayerPaintAncestorClippingMask)
      painting_phases_json->PushString(
          "GraphicsLayerPaintAncestorClippingMask");
    if (painting_phase_ & kGraphicsLayerPaintOverflowContents)
      painting_phases_json->PushString("GraphicsLayerPaintOverflowContents");
    if (painting_phase_ & kGraphicsLayerPaintCompositedScroll)
      painting_phases_json->PushString("GraphicsLayerPaintCompositedScroll");
    if (painting_phase_ & kGraphicsLayerPaintDecoration)
      painting_phases_json->PushString("GraphicsLayerPaintDecoration");
    json->SetArray("paintingPhases", std::move(painting_phases_json));
  }

  if (flags & kLayerTreeIncludesClipAndScrollParents) {
    if (has_scroll_parent_)
      json->SetBoolean("hasScrollParent", true);
    if (has_clip_parent_)
      json->SetBoolean("hasClipParent", true);
  }

  if (flags &
      (kLayerTreeIncludesDebugInfo | kLayerTreeIncludesCompositingReasons)) {
    bool debug = flags & kLayerTreeIncludesDebugInfo;
    std::unique_ptr<JSONArray> compositing_reasons_json = JSONArray::Create();
    for (size_t i = 0; i < kNumberOfCompositingReasons; ++i) {
      if (debug_info_.GetCompositingReasons() &
          kCompositingReasonStringMap[i].reason) {
        compositing_reasons_json->PushString(
            debug ? kCompositingReasonStringMap[i].description
                  : kCompositingReasonStringMap[i].short_name);
      }
    }
    json->SetArray("compositingReasons", std::move(compositing_reasons_json));

    std::unique_ptr<JSONArray> squashing_disallowed_reasons_json =
        JSONArray::Create();
    for (size_t i = 0; i < kNumberOfSquashingDisallowedReasons; ++i) {
      if (debug_info_.GetSquashingDisallowedReasons() &
          kSquashingDisallowedReasonStringMap[i].reason) {
        squashing_disallowed_reasons_json->PushString(
            debug ? kSquashingDisallowedReasonStringMap[i].description
                  : kSquashingDisallowedReasonStringMap[i].short_name);
      }
    }
    json->SetArray("squashingDisallowedReasons",
                   std::move(squashing_disallowed_reasons_json));
  }

  if (mask_layer_) {
    std::unique_ptr<JSONArray> mask_layer_json = JSONArray::Create();
    mask_layer_json->PushObject(
        mask_layer_->LayerAsJSONInternal(flags, rendering_context_map));
    json->SetArray("maskLayer", std::move(mask_layer_json));
  }

  if (contents_clipping_mask_layer_) {
    std::unique_ptr<JSONArray> contents_clipping_mask_layer_json =
        JSONArray::Create();
    contents_clipping_mask_layer_json->PushObject(
        contents_clipping_mask_layer_->LayerAsJSONInternal(
            flags, rendering_context_map));
    json->SetArray("contentsClippingMaskLayer",
                   std::move(contents_clipping_mask_layer_json));
  }

  return json;
}

std::unique_ptr<JSONObject> GraphicsLayer::LayerTreeAsJSONInternal(
    LayerTreeFlags flags,
    RenderingContextMap& rendering_context_map) const {
  std::unique_ptr<JSONObject> json =
      LayerAsJSONInternal(flags, rendering_context_map);

  if (children_.size()) {
    std::unique_ptr<JSONArray> children_json = JSONArray::Create();
    for (size_t i = 0; i < children_.size(); i++) {
      children_json->PushObject(
          children_[i]->LayerTreeAsJSONInternal(flags, rendering_context_map));
    }
    json->SetArray("children", std::move(children_json));
  }

  return json;
}

void GraphicsLayer::LayersAsJSONArray(
    LayerTreeFlags flags,
    RenderingContextMap& rendering_context_map,
    JSONArray* json_array) const {
  json_array->PushObject(LayerAsJSONInternal(flags, rendering_context_map));

  if (children_.size()) {
    for (auto& child : children_)
      child->LayersAsJSONArray(flags, rendering_context_map, json_array);
  }
}

String GraphicsLayer::LayerTreeAsText(LayerTreeFlags flags) const {
  return LayerTreeAsJSON(flags)->ToPrettyJSONString();
}

static const cc::Layer* CcLayerForWebLayer(const WebLayer* web_layer) {
  return web_layer ? web_layer->CcLayer() : nullptr;
}

String GraphicsLayer::DebugName(cc::Layer* layer) const {
  String name;
  if (!client_)
    return name;

  String highlight_debug_name;
  for (size_t i = 0; i < link_highlights_.size(); ++i) {
    if (layer == CcLayerForWebLayer(link_highlights_[i]->Layer())) {
      highlight_debug_name = "LinkHighlight[" + String::Number(i) + "] for " +
                             client_->DebugName(this);
      break;
    }
  }

  if (layer->id() == contents_layer_id_) {
    name = "ContentsLayer for " + client_->DebugName(this);
  } else if (!highlight_debug_name.IsEmpty()) {
    name = highlight_debug_name;
  } else if (layer == CcLayerForWebLayer(layer_->Layer())) {
    name = client_->DebugName(this);
  } else {
    NOTREACHED();
  }
  return name;
}

void GraphicsLayer::SetCompositingReasons(CompositingReasons reasons) {
  debug_info_.SetCompositingReasons(reasons);
}

void GraphicsLayer::SetSquashingDisallowedReasons(
    SquashingDisallowedReasons reasons) {
  debug_info_.SetSquashingDisallowedReasons(reasons);
}

void GraphicsLayer::SetOwnerNodeId(int node_id) {
  debug_info_.SetOwnerNodeId(node_id);
}

void GraphicsLayer::SetPosition(const FloatPoint& point) {
  position_ = point;
  PlatformLayer()->SetPosition(position_);
}

void GraphicsLayer::SetSize(const FloatSize& size) {
  // We are receiving negative sizes here that cause assertions to fail in the
  // compositor. Clamp them to 0 to avoid those assertions.
  // FIXME: This should be an DCHECK instead, as negative sizes should not exist
  // in WebCore.
  FloatSize clamped_size = size;
  if (clamped_size.Width() < 0 || clamped_size.Height() < 0)
    clamped_size = FloatSize();

  if (clamped_size == size_)
    return;

  size_ = clamped_size;

  layer_->Layer()->SetBounds(FlooredIntSize(size_));
  // Note that we don't resize m_contentsLayer. It's up the caller to do that.
}

void GraphicsLayer::SetTransform(const TransformationMatrix& transform) {
  transform_ = transform;
  PlatformLayer()->SetTransform(TransformationMatrix::ToSkMatrix44(transform_));
}

void GraphicsLayer::SetTransformOrigin(const FloatPoint3D& transform_origin) {
  has_transform_origin_ = true;
  transform_origin_ = transform_origin;
  PlatformLayer()->SetTransformOrigin(transform_origin);
}

void GraphicsLayer::SetShouldFlattenTransform(bool should_flatten) {
  if (should_flatten == should_flatten_transform_)
    return;

  should_flatten_transform_ = should_flatten;

  layer_->Layer()->SetShouldFlattenTransform(should_flatten);
}

void GraphicsLayer::SetRenderingContext(int context) {
  if (rendering_context3d_ == context)
    return;

  rendering_context3d_ = context;
  layer_->Layer()->SetRenderingContext(context);

  if (contents_layer_)
    contents_layer_->SetRenderingContext(rendering_context3d_);
}

bool GraphicsLayer::MasksToBounds() const {
  return layer_->Layer()->MasksToBounds();
}

void GraphicsLayer::SetMasksToBounds(bool masks_to_bounds) {
  layer_->Layer()->SetMasksToBounds(masks_to_bounds);
}

void GraphicsLayer::SetDrawsContent(bool draws_content) {
  // NOTE: This early-exit is only correct because we also properly call
  // WebLayer::setDrawsContent() whenever |m_contentsLayer| is set to a new
  // layer in setupContentsLayer().
  if (draws_content == draws_content_)
    return;

  draws_content_ = draws_content;
  UpdateLayerIsDrawable();

  if (!draws_content && paint_controller_)
    paint_controller_.reset();
}

void GraphicsLayer::SetContentsVisible(bool contents_visible) {
  // NOTE: This early-exit is only correct because we also properly call
  // WebLayer::setDrawsContent() whenever |m_contentsLayer| is set to a new
  // layer in setupContentsLayer().
  if (contents_visible == contents_visible_)
    return;

  contents_visible_ = contents_visible;
  UpdateLayerIsDrawable();
}

void GraphicsLayer::SetClipParent(WebLayer* parent) {
  has_clip_parent_ = !!parent;
  layer_->Layer()->SetClipParent(parent);
}

void GraphicsLayer::SetScrollParent(WebLayer* parent) {
  has_scroll_parent_ = !!parent;
  layer_->Layer()->SetScrollParent(parent);
}

void GraphicsLayer::SetBackgroundColor(const Color& color) {
  if (color == background_color_)
    return;

  background_color_ = color;
  layer_->Layer()->SetBackgroundColor(background_color_.Rgb());
}

void GraphicsLayer::SetContentsOpaque(bool opaque) {
  contents_opaque_ = opaque;
  layer_->Layer()->SetOpaque(contents_opaque_);
  ClearContentsLayerIfUnregistered();
  if (contents_layer_)
    contents_layer_->SetOpaque(opaque);
}

void GraphicsLayer::SetMaskLayer(GraphicsLayer* mask_layer) {
  if (mask_layer == mask_layer_)
    return;

  mask_layer_ = mask_layer;
  WebLayer* mask_web_layer = mask_layer_ ? mask_layer_->PlatformLayer() : 0;
  layer_->Layer()->SetMaskLayer(mask_web_layer);
}

void GraphicsLayer::SetContentsClippingMaskLayer(
    GraphicsLayer* contents_clipping_mask_layer) {
  if (contents_clipping_mask_layer == contents_clipping_mask_layer_)
    return;

  contents_clipping_mask_layer_ = contents_clipping_mask_layer;
  WebLayer* contents_layer = ContentsLayerIfRegistered();
  if (!contents_layer)
    return;
  WebLayer* contents_clipping_mask_web_layer =
      contents_clipping_mask_layer_
          ? contents_clipping_mask_layer_->PlatformLayer()
          : 0;
  contents_layer->SetMaskLayer(contents_clipping_mask_web_layer);
  UpdateContentsRect();
}

void GraphicsLayer::SetBackfaceVisibility(bool visible) {
  backface_visibility_ = visible;
  PlatformLayer()->SetDoubleSided(backface_visibility_);
}

void GraphicsLayer::SetOpacity(float opacity) {
  float clamped_opacity = clampTo(opacity, 0.0f, 1.0f);
  opacity_ = clamped_opacity;
  PlatformLayer()->SetOpacity(opacity);
}

void GraphicsLayer::SetBlendMode(WebBlendMode blend_mode) {
  if (blend_mode_ == blend_mode)
    return;
  blend_mode_ = blend_mode;
  PlatformLayer()->SetBlendMode(blend_mode);
}

void GraphicsLayer::SetIsRootForIsolatedGroup(bool isolated) {
  if (is_root_for_isolated_group_ == isolated)
    return;
  is_root_for_isolated_group_ = isolated;
  PlatformLayer()->SetIsRootForIsolatedGroup(isolated);
}

void GraphicsLayer::SetContentsNeedsDisplay() {
  if (WebLayer* contents_layer = ContentsLayerIfRegistered()) {
    contents_layer->Invalidate();
    TrackRasterInvalidation(*this, contents_rect_,
                            PaintInvalidationReason::kFull);
  }
}

void GraphicsLayer::SetNeedsDisplay() {
  if (!DrawsContent())
    return;

  // TODO(chrishtr): Stop invalidating the rects once
  // FrameView::paintRecursively() does so.
  layer_->Layer()->Invalidate();
  for (size_t i = 0; i < link_highlights_.size(); ++i)
    link_highlights_[i]->Invalidate();
  GetPaintController().InvalidateAll();

  TrackRasterInvalidation(*this, IntRect(IntPoint(), ExpandedIntSize(size_)),
                          PaintInvalidationReason::kFull);
}

DISABLE_CFI_PERF
void GraphicsLayer::SetNeedsDisplayInRect(
    const IntRect& rect,
    PaintInvalidationReason invalidation_reason,
    const DisplayItemClient& client) {
  if (!DrawsContent())
    return;

  layer_->Layer()->InvalidateRect(rect);
  if (FirstPaintInvalidationTracking::IsEnabled())
    debug_info_.AppendAnnotatedInvalidateRect(rect, invalidation_reason);
  for (size_t i = 0; i < link_highlights_.size(); ++i)
    link_highlights_[i]->Invalidate();

  TrackRasterInvalidation(client, rect, invalidation_reason);
}

void GraphicsLayer::SetContentsRect(const IntRect& rect) {
  if (rect == contents_rect_)
    return;

  contents_rect_ = rect;
  UpdateContentsRect();
}

void GraphicsLayer::SetContentsToImage(
    Image* image,
    RespectImageOrientationEnum respect_image_orientation) {
  PaintImage paint_image;
  if (image)
    paint_image = image->PaintImageForCurrentFrame();

  if (paint_image && image->IsBitmapImage() &&
      respect_image_orientation == kRespectImageOrientation) {
    ImageOrientation image_orientation =
        ToBitmapImage(image)->CurrentFrameOrientation();
    paint_image =
        DragImage::ResizeAndOrientImage(paint_image, image_orientation);
  }

  if (paint_image) {
    if (!image_layer_) {
      image_layer_ =
          Platform::Current()->CompositorSupport()->CreateImageLayer();
      RegisterContentsLayer(image_layer_->Layer());
    }
    image_layer_->SetImage(std::move(paint_image));
    image_layer_->Layer()->SetOpaque(image->CurrentFrameKnownToBeOpaque());
    UpdateContentsRect();
  } else if (image_layer_) {
    UnregisterContentsLayer(image_layer_->Layer());
    image_layer_.reset();
  }

  SetContentsTo(image_layer_ ? image_layer_->Layer() : 0);
}

WebLayer* GraphicsLayer::PlatformLayer() const {
  return layer_->Layer();
}

void GraphicsLayer::SetFilters(CompositorFilterOperations filters) {
  PlatformLayer()->SetFilters(filters.ReleaseCcFilterOperations());
}

void GraphicsLayer::SetBackdropFilters(CompositorFilterOperations filters) {
  PlatformLayer()->SetBackgroundFilters(filters.ReleaseCcFilterOperations());
}

void GraphicsLayer::SetStickyPositionConstraint(
    const WebLayerStickyPositionConstraint& sticky_constraint) {
  layer_->Layer()->SetStickyPositionConstraint(sticky_constraint);
}

void GraphicsLayer::SetFilterQuality(SkFilterQuality filter_quality) {
  if (image_layer_)
    image_layer_->SetNearestNeighbor(filter_quality == kNone_SkFilterQuality);
}

void GraphicsLayer::SetPaintingPhase(GraphicsLayerPaintingPhase phase) {
  if (painting_phase_ == phase)
    return;
  painting_phase_ = phase;
  SetNeedsDisplay();
}

void GraphicsLayer::AddLinkHighlight(LinkHighlight* link_highlight) {
  DCHECK(link_highlight && !link_highlights_.Contains(link_highlight));
  link_highlights_.push_back(link_highlight);
  link_highlight->Layer()->SetLayerClient(this);
  UpdateChildList();
}

void GraphicsLayer::RemoveLinkHighlight(LinkHighlight* link_highlight) {
  link_highlights_.erase(link_highlights_.Find(link_highlight));
  UpdateChildList();
}

void GraphicsLayer::SetScrollableArea(ScrollableArea* scrollable_area,
                                      bool is_visual_viewport) {
  if (scrollable_area_ == scrollable_area)
    return;

  scrollable_area_ = scrollable_area;

  // VisualViewport scrolling may involve pinch zoom and gets routed through
  // WebViewImpl explicitly rather than via ScrollableArea::didScroll since it
  // needs to be set in tandem with the page scale delta.
  if (is_visual_viewport)
    layer_->Layer()->SetScrollClient(nullptr);
  else
    layer_->Layer()->SetScrollClient(scrollable_area);
}

std::unique_ptr<base::trace_event::ConvertableToTraceFormat>
GraphicsLayer::TakeDebugInfo(cc::Layer* layer) {
  std::unique_ptr<base::trace_event::TracedValue> traced_value(
      debug_info_.AsTracedValue());
  traced_value->SetString(
      "layer_name", WTF::StringUTF8Adaptor(DebugName(layer)).AsStringPiece());
  return std::move(traced_value);
}

void GraphicsLayer::didUpdateMainThreadScrollingReasons() {
  debug_info_.SetMainThreadScrollingReasons(
      PlatformLayer()->MainThreadScrollingReasons());
}

void GraphicsLayer::didChangeScrollbarsHidden(bool hidden) {
  if (scrollable_area_)
    scrollable_area_->SetScrollbarsHidden(hidden);
}

PaintController& GraphicsLayer::GetPaintController() {
  CHECK(DrawsContent());
  if (!paint_controller_)
    paint_controller_ = PaintController::Create();
  return *paint_controller_;
}

void GraphicsLayer::SetElementId(const CompositorElementId& id) {
  if (WebLayer* layer = PlatformLayer())
    layer->SetElementId(id);
}

CompositorElementId GraphicsLayer::GetElementId() const {
  if (WebLayer* layer = PlatformLayer())
    return layer->GetElementId();
  return CompositorElementId();
}

void GraphicsLayer::SetCompositorMutableProperties(uint32_t properties) {
  if (WebLayer* layer = PlatformLayer())
    layer->SetCompositorMutableProperties(properties);
}

sk_sp<PaintRecord> GraphicsLayer::CaptureRecord() {
  if (!DrawsContent())
    return nullptr;

  FloatRect bounds(IntRect(IntPoint(0, 0), ExpandedIntSize(Size())));
  GraphicsContext graphics_context(GetPaintController(),
                                   GraphicsContext::kNothingDisabled, nullptr);
  graphics_context.BeginRecording(bounds);
  GetPaintController().GetPaintArtifact().Replay(bounds, graphics_context);
  return graphics_context.EndRecording();
}

static bool PixelComponentsDiffer(int c1, int c2) {
  // Compare strictly for saturated values.
  if (c1 == 0 || c1 == 255 || c2 == 0 || c2 == 255)
    return c1 != c2;
  // Tolerate invisible differences that may occur in gradients etc.
  return abs(c1 - c2) > 2;
}

static bool PixelsDiffer(SkColor p1, SkColor p2) {
  return PixelComponentsDiffer(SkColorGetA(p1), SkColorGetA(p2)) ||
         PixelComponentsDiffer(SkColorGetR(p1), SkColorGetR(p2)) ||
         PixelComponentsDiffer(SkColorGetG(p1), SkColorGetG(p2)) ||
         PixelComponentsDiffer(SkColorGetB(p1), SkColorGetB(p2));
}

void GraphicsLayer::CheckPaintUnderInvalidations(
    sk_sp<PaintRecord> new_record) {
  if (!DrawsContent())
    return;

  RasterInvalidationTracking* tracking =
      GetRasterInvalidationTrackingMap().Find(this);
  if (!tracking)
    return;

  if (!tracking->last_painted_record)
    return;

  IntRect rect = Intersection(tracking->last_interest_rect, InterestRect());
  if (rect.IsEmpty())
    return;

  SkBitmap old_bitmap;
  old_bitmap.allocPixels(
      SkImageInfo::MakeN32Premul(rect.Width(), rect.Height()));
  {
    SkiaPaintCanvas canvas(old_bitmap);
    canvas.clear(SK_ColorTRANSPARENT);
    canvas.translate(-rect.X(), -rect.Y());
    canvas.drawPicture(tracking->last_painted_record);
  }

  SkBitmap new_bitmap;
  new_bitmap.allocPixels(
      SkImageInfo::MakeN32Premul(rect.Width(), rect.Height()));
  {
    SkiaPaintCanvas canvas(new_bitmap);
    canvas.clear(SK_ColorTRANSPARENT);
    canvas.translate(-rect.X(), -rect.Y());
    canvas.drawPicture(std::move(new_record));
  }

  int mismatching_pixels = 0;
  static const int kMaxMismatchesToReport = 50;
  for (int bitmap_y = 0; bitmap_y < rect.Height(); ++bitmap_y) {
    int layer_y = bitmap_y + rect.Y();
    for (int bitmap_x = 0; bitmap_x < rect.Width(); ++bitmap_x) {
      int layer_x = bitmap_x + rect.X();
      SkColor old_pixel = old_bitmap.getColor(bitmap_x, bitmap_y);
      SkColor new_pixel = new_bitmap.getColor(bitmap_x, bitmap_y);
      if (PixelsDiffer(old_pixel, new_pixel) &&
          !tracking->invalidation_region_since_last_paint.Contains(
              IntPoint(layer_x, layer_y))) {
        if (mismatching_pixels < kMaxMismatchesToReport) {
          UnderRasterInvalidation under_invalidation = {layer_x, layer_y,
                                                        old_pixel, new_pixel};
          tracking->under_invalidations.push_back(under_invalidation);
          LOG(ERROR) << DebugName()
                     << " Uninvalidated old/new pixels mismatch at " << layer_x
                     << "," << layer_y << " old:" << std::hex << old_pixel
                     << " new:" << new_pixel;
        } else if (mismatching_pixels == kMaxMismatchesToReport) {
          LOG(ERROR) << "and more...";
        }
        ++mismatching_pixels;
        *new_bitmap.getAddr32(bitmap_x, bitmap_y) =
            SkColorSetARGB(0xFF, 0xA0, 0, 0);  // Dark red.
      } else {
        *new_bitmap.getAddr32(bitmap_x, bitmap_y) = SK_ColorTRANSPARENT;
      }
    }
  }

  // Visualize under-invalidations by overlaying the new bitmap (containing red
  // pixels indicating under-invalidations, and transparent pixels otherwise)
  // onto the painting.
  PaintRecorder recorder;
  recorder.beginRecording(rect);
  recorder.getRecordingCanvas()->drawBitmap(new_bitmap, rect.X(), rect.Y());
  sk_sp<PaintRecord> record = recorder.finishRecordingAsPicture();
  GetPaintController().AppendDebugDrawingAfterCommit(
      *this, record, OffsetFromLayoutObjectWithSubpixelAccumulation());
}

}  // namespace blink

#ifndef NDEBUG
void showGraphicsLayerTree(const blink::GraphicsLayer* layer) {
  if (!layer) {
    LOG(INFO) << "Cannot showGraphicsLayerTree for (nil).";
    return;
  }

  String output = layer->LayerTreeAsText(blink::kLayerTreeIncludesDebugInfo);
  LOG(INFO) << output.Utf8().data();
}
#endif
