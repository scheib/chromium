// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/paint/PaintArtifact.h"

#include "cc/paint/display_item_list.h"
#include "platform/geometry/IntRect.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/compositing/PaintChunksToCcLayer.h"
#include "platform/graphics/paint/DrawingDisplayItem.h"
#include "platform/graphics/paint/GeometryMapper.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "public/platform/WebDisplayItemList.h"
#include "third_party/skia/include/core/SkRegion.h"

namespace blink {

namespace {

void ComputeChunkBoundsAndOpaqueness(const DisplayItemList& display_items,
                                     Vector<PaintChunk>& paint_chunks) {
  for (PaintChunk& chunk : paint_chunks) {
    FloatRect bounds;
    SkRegion known_to_be_opaque_region;
    for (const DisplayItem& item : display_items.ItemsInPaintChunk(chunk)) {
      bounds.Unite(FloatRect(item.Client().VisualRect()));
      if (!item.IsDrawing())
        continue;
      const auto& drawing = static_cast<const DrawingDisplayItem&>(item);
      if (const PaintRecord* record = drawing.GetPaintRecord().get()) {
        if (drawing.KnownToBeOpaque()) {
          // TODO(pdr): It may be too conservative to round in to the
          // enclosedIntRect.
          SkIRect conservative_rounded_rect;
          const SkRect& cull_rect = record->cullRect();
          cull_rect.roundIn(&conservative_rounded_rect);
          known_to_be_opaque_region.op(conservative_rounded_rect,
                                       SkRegion::kUnion_Op);
        }
      }
    }
    chunk.bounds = bounds;
    if (known_to_be_opaque_region.contains(EnclosingIntRect(bounds)))
      chunk.known_to_be_opaque = true;
  }
}

}  // namespace

PaintArtifact::PaintArtifact()
    : display_item_list_(0), is_suitable_for_gpu_rasterization_(true) {}

PaintArtifact::PaintArtifact(DisplayItemList display_items,
                             Vector<PaintChunk> paint_chunks,
                             bool is_suitable_for_gpu_rasterization_arg)
    : display_item_list_(std::move(display_items)),
      paint_chunks_(std::move(paint_chunks)),
      is_suitable_for_gpu_rasterization_(
          is_suitable_for_gpu_rasterization_arg) {
  ComputeChunkBoundsAndOpaqueness(display_item_list_, paint_chunks_);
}

PaintArtifact::PaintArtifact(PaintArtifact&& source)
    : display_item_list_(std::move(source.display_item_list_)),
      paint_chunks_(std::move(source.paint_chunks_)),
      is_suitable_for_gpu_rasterization_(
          source.is_suitable_for_gpu_rasterization_) {}

PaintArtifact::~PaintArtifact() {}

PaintArtifact& PaintArtifact::operator=(PaintArtifact&& source) {
  display_item_list_ = std::move(source.display_item_list_);
  paint_chunks_ = std::move(source.paint_chunks_);
  is_suitable_for_gpu_rasterization_ =
      source.is_suitable_for_gpu_rasterization_;
  return *this;
}

void PaintArtifact::Reset() {
  display_item_list_.Clear();
  paint_chunks_.clear();
}

size_t PaintArtifact::ApproximateUnsharedMemoryUsage() const {
  return sizeof(*this) + display_item_list_.MemoryUsageInBytes() +
         paint_chunks_.capacity() * sizeof(paint_chunks_[0]);
}

void PaintArtifact::Replay(const FloatRect& bounds,
                           GraphicsContext& graphics_context) const {
  TRACE_EVENT0("blink,benchmark", "PaintArtifact::replay");
  if (!RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    for (const DisplayItem& display_item : display_item_list_)
      display_item.Replay(graphics_context);
  } else {
    Replay(bounds, *graphics_context.Canvas());
  }
}

void PaintArtifact::Replay(const FloatRect& bounds,
                           PaintCanvas& canvas,
                           const PropertyTreeState& replay_state) const {
  TRACE_EVENT0("blink,benchmark", "PaintArtifact::replay");
  DCHECK(RuntimeEnabledFeatures::slimmingPaintV2Enabled());
  Vector<const PaintChunk*> pointer_paint_chunks;
  pointer_paint_chunks.ReserveInitialCapacity(PaintChunks().size());

  // TODO(chrishtr): it's sad to have to copy this vector just to turn
  // references into pointers.
  for (const auto& chunk : PaintChunks())
    pointer_paint_chunks.push_back(&chunk);
  scoped_refptr<cc::DisplayItemList> display_item_list =
      PaintChunksToCcLayer::Convert(pointer_paint_chunks, replay_state,
                                    gfx::Vector2dF(), GetDisplayItemList());
  canvas.drawDisplayItemList(display_item_list);
}

DISABLE_CFI_PERF
void PaintArtifact::AppendToWebDisplayItemList(WebDisplayItemList* list) const {
  TRACE_EVENT0("blink,benchmark", "PaintArtifact::appendToWebDisplayItemList");
  size_t visual_rect_index = 0;
  for (const DisplayItem& display_item : display_item_list_) {
    display_item.AppendToWebDisplayItemList(
        display_item_list_.VisualRect(visual_rect_index), list);
    visual_rect_index++;
  }
  list->SetIsSuitableForGpuRasterization(IsSuitableForGpuRasterization());
}

}  // namespace blink
