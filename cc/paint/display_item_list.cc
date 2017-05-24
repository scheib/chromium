// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/paint/display_item_list.h"

#include <stddef.h>

#include <string>

#include "base/memory/ptr_util.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_event_argument.h"
#include "cc/base/math_util.h"
#include "cc/base/render_surface_filters.h"
#include "cc/debug/picture_debug_util.h"
#include "cc/paint/clip_display_item.h"
#include "cc/paint/clip_path_display_item.h"
#include "cc/paint/compositing_display_item.h"
#include "cc/paint/discardable_image_store.h"
#include "cc/paint/drawing_display_item.h"
#include "cc/paint/filter_display_item.h"
#include "cc/paint/float_clip_display_item.h"
#include "cc/paint/largest_display_item.h"
#include "cc/paint/transform_display_item.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImageFilter.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkPictureRecorder.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/skia_util.h"

namespace cc {

namespace {

// We don't perform per-layer solid color analysis when there are too many skia
// operations.
const int kOpCountThatIsOkToAnalyze = 10;

bool GetCanvasClipBounds(SkCanvas* canvas, gfx::Rect* clip_bounds) {
  SkRect canvas_clip_bounds;
  if (!canvas->getLocalClipBounds(&canvas_clip_bounds))
    return false;
  *clip_bounds = ToEnclosingRect(gfx::SkRectToRectF(canvas_clip_bounds));
  return true;
}

const int kDefaultNumDisplayItemsToReserve = 100;

NOINLINE DISABLE_CFI_PERF void RasterItem(const DisplayItem& base_item,
                                          SkCanvas* canvas,
                                          SkPicture::AbortCallback* callback) {
  switch (base_item.type) {
    case DisplayItem::CLIP: {
      const auto& item = static_cast<const ClipDisplayItem&>(base_item);
      canvas->save();
      canvas->clipRect(gfx::RectToSkRect(item.clip_rect), item.antialias);
      for (const auto& rrect : item.rounded_clip_rects) {
        if (rrect.isRect()) {
          canvas->clipRect(rrect.rect(), item.antialias);
        } else {
          canvas->clipRRect(rrect, item.antialias);
        }
      }
      break;
    }
    case DisplayItem::END_CLIP:
      canvas->restore();
      break;
    case DisplayItem::CLIP_PATH: {
      const auto& item = static_cast<const ClipPathDisplayItem&>(base_item);
      canvas->save();
      canvas->clipPath(item.clip_path, item.antialias);
      break;
    }
    case DisplayItem::END_CLIP_PATH:
      canvas->restore();
      break;
    case DisplayItem::COMPOSITING: {
      const auto& item = static_cast<const CompositingDisplayItem&>(base_item);
      SkPaint paint;
      paint.setBlendMode(item.xfermode);
      paint.setAlpha(item.alpha);
      paint.setColorFilter(item.color_filter);
      const SkRect* bounds = item.has_bounds ? &item.bounds : nullptr;
      if (item.lcd_text_requires_opaque_layer)
        canvas->saveLayer(bounds, &paint);
      else
        canvas->saveLayerPreserveLCDTextRequests(bounds, &paint);
      break;
    }
    case DisplayItem::END_COMPOSITING:
      canvas->restore();
      break;
    case DisplayItem::DRAWING: {
      const auto& item = static_cast<const DrawingDisplayItem&>(base_item);
      if (canvas->quickReject(item.picture->cullRect()))
        break;

      // TODO(enne): Maybe the PaintRecord itself could know whether this
      // was needed?  It's not clear whether these save/restore semantics
      // that SkPicture handles during playback are things that should be
      // kept around.
      canvas->save();
      item.picture->playback(canvas, callback);
      canvas->restore();
      break;
    }
    case DisplayItem::FLOAT_CLIP: {
      const auto& item = static_cast<const FloatClipDisplayItem&>(base_item);
      canvas->save();
      canvas->clipRect(gfx::RectFToSkRect(item.clip_rect));
      break;
    }
    case DisplayItem::END_FLOAT_CLIP:
      canvas->restore();
      break;
    case DisplayItem::FILTER: {
      const auto& item = static_cast<const FilterDisplayItem&>(base_item);
      canvas->save();
      canvas->translate(item.origin.x(), item.origin.y());

      sk_sp<SkImageFilter> image_filter =
          RenderSurfaceFilters::BuildImageFilter(item.filters,
                                                 item.bounds.size());
      SkRect boundaries = RectFToSkRect(item.bounds);
      boundaries.offset(-item.origin.x(), -item.origin.y());

      SkPaint paint;
      paint.setBlendMode(SkBlendMode::kSrcOver);
      paint.setImageFilter(std::move(image_filter));
      canvas->saveLayer(&boundaries, &paint);

      canvas->translate(-item.origin.x(), -item.origin.y());
      break;
    }
    case DisplayItem::END_FILTER:
      canvas->restore();
      canvas->restore();
      break;
    case DisplayItem::TRANSFORM: {
      const auto& item = static_cast<const TransformDisplayItem&>(base_item);
      canvas->save();
      if (!item.transform.IsIdentity())
        canvas->concat(item.transform.matrix());
      break;
    }
    case DisplayItem::END_TRANSFORM:
      canvas->restore();
      break;
  }
}

}  // namespace

DisplayItemList::DisplayItemList()
    : items_(LargestDisplayItemSize(),
             LargestDisplayItemSize() * kDefaultNumDisplayItemsToReserve) {}

DisplayItemList::~DisplayItemList() = default;

// Atttempts to merge a CompositingDisplayItem and DrawingDisplayItem
// into a single "draw with alpha".  This function returns true if
// it was successful.  If false, then the caller is responsible for
// drawing these items.  This is a DisplayItemList version of the
// SkRecord optimization SkRecordNoopSaveLayerDrawRestores.
static bool MergeAndDrawIfPossible(const CompositingDisplayItem& save_item,
                                   const DrawingDisplayItem& draw_item,
                                   SkCanvas* canvas) {
  if (save_item.color_filter)
    return false;
  if (save_item.xfermode != SkBlendMode::kSrcOver)
    return false;
  // TODO(enne): I believe that lcd_text_requires_opaque_layer is not
  // relevant here and that lcd text is preserved post merge, but I haven't
  // tested that.
  const PaintRecord* record = draw_item.picture.get();
  if (record->size() != 1u)
    return false;

  const PaintOp* op = record->GetFirstOp();
  if (!op->IsDrawOp())
    return false;

  op->RasterWithAlpha(canvas, save_item.alpha);
  return true;
}

void DisplayItemList::Raster(SkCanvas* canvas,
                             SkPicture::AbortCallback* callback) const {
  gfx::Rect canvas_playback_rect;
  if (!GetCanvasClipBounds(canvas, &canvas_playback_rect))
    return;

  std::vector<size_t> indices = rtree_.Search(canvas_playback_rect);
  for (size_t i = 0; i < indices.size(); ++i) {
    // We use a callback during solid color analysis on the compositor thread to
    // break out early. Since we're handling a sequence of pictures via rtree
    // query results ourselves, we have to respect the callback and early out.
    if (callback && callback->abort())
      break;

    const DisplayItem& item = items_[indices[i]];
    // Optimize empty begin/end compositing and merge begin/draw/end compositing
    // where possible.
    // TODO(enne): remove empty clips here too?
    // TODO(enne): does this happen recursively? Or is this good enough?
    if (i < indices.size() - 2 && item.type == DisplayItem::COMPOSITING) {
      const DisplayItem& second = items_[indices[i + 1]];
      const DisplayItem& third = items_[indices[i + 2]];
      if (second.type == DisplayItem::DRAWING &&
          third.type == DisplayItem::END_COMPOSITING) {
        if (MergeAndDrawIfPossible(
                static_cast<const CompositingDisplayItem&>(item),
                static_cast<const DrawingDisplayItem&>(second), canvas)) {
          i += 2;
          continue;
        }
      }
    }

    RasterItem(item, canvas, callback);
  }
}

void DisplayItemList::GrowCurrentBeginItemVisualRect(
    const gfx::Rect& visual_rect) {
  if (!begin_item_indices_.empty())
    visual_rects_[begin_item_indices_.back()].Union(visual_rect);
}

void DisplayItemList::Finalize() {
  TRACE_EVENT0("cc", "DisplayItemList::Finalize");
  DCHECK(items_.size() == visual_rects_.size())
      << "items.size() " << items_.size() << " visual_rects.size() "
      << visual_rects_.size();
  rtree_.Build(visual_rects_);

  if (!retain_visual_rects_)
    // This clears both the vector and the vector's capacity, since
    // visual_rects won't be used anymore.
    std::vector<gfx::Rect>().swap(visual_rects_);
}

bool DisplayItemList::IsSuitableForGpuRasterization() const {
  // TODO(wkorman): This is more permissive than Picture's implementation, since
  // none of the items might individually trigger a veto even though they
  // collectively have enough "bad" operations that a corresponding Picture
  // would get vetoed. See crbug.com/513016.
  return all_items_are_suitable_for_gpu_rasterization_;
}

size_t DisplayItemList::OpCount() const {
  return op_count_;
}

size_t DisplayItemList::ApproximateMemoryUsage() const {
  size_t memory_usage = sizeof(*this);

  size_t external_memory_usage = 0;
  for (const auto& item : items_) {
    size_t bytes = 0;
    switch (item.type) {
      case DisplayItem::CLIP:
        bytes = static_cast<const ClipDisplayItem&>(item).ExternalMemoryUsage();
        break;
      case DisplayItem::CLIP_PATH:
        bytes =
            static_cast<const ClipPathDisplayItem&>(item).ExternalMemoryUsage();
        break;
      case DisplayItem::COMPOSITING:
        bytes = static_cast<const CompositingDisplayItem&>(item)
                    .ExternalMemoryUsage();
        break;
      case DisplayItem::DRAWING:
        bytes =
            static_cast<const DrawingDisplayItem&>(item).ExternalMemoryUsage();
        break;
      case DisplayItem::FLOAT_CLIP:
        bytes = static_cast<const FloatClipDisplayItem&>(item)
                    .ExternalMemoryUsage();
        break;
      case DisplayItem::FILTER:
        bytes =
            static_cast<const FilterDisplayItem&>(item).ExternalMemoryUsage();
        break;
      case DisplayItem::TRANSFORM:
        bytes = static_cast<const TransformDisplayItem&>(item)
                    .ExternalMemoryUsage();
        break;
      case DisplayItem::END_CLIP:
      case DisplayItem::END_CLIP_PATH:
      case DisplayItem::END_COMPOSITING:
      case DisplayItem::END_FLOAT_CLIP:
      case DisplayItem::END_FILTER:
      case DisplayItem::END_TRANSFORM:
        break;
    }
    external_memory_usage += bytes;
  }

  // Memory outside this class due to |items_|.
  memory_usage += items_.GetCapacityInBytes() + external_memory_usage;

  // TODO(jbroman): Does anything else owned by this class substantially
  // contribute to memory usage?
  // TODO(vmpstr): Probably DiscardableImageMap is worth counting here.

  return memory_usage;
}

bool DisplayItemList::ShouldBeAnalyzedForSolidColor() const {
  return OpCount() <= kOpCountThatIsOkToAnalyze;
}

void DisplayItemList::EmitTraceSnapshot() const {
  bool include_items;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(
      TRACE_DISABLED_BY_DEFAULT("cc.debug.display_items"), &include_items);
  TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID(
      TRACE_DISABLED_BY_DEFAULT("cc.debug.display_items") ","
      TRACE_DISABLED_BY_DEFAULT("cc.debug.picture") ","
      TRACE_DISABLED_BY_DEFAULT("devtools.timeline.picture"),
      "cc::DisplayItemList", this, CreateTracedValue(include_items));
}

std::unique_ptr<base::trace_event::TracedValue>
DisplayItemList::CreateTracedValue(bool include_items) const {
  auto state = base::MakeUnique<base::trace_event::TracedValue>();
  state->BeginDictionary("params");

  if (include_items) {
    state->BeginArray("items");

    auto visual_rects_it = visual_rects_.begin();
    for (const DisplayItem& base_item : items_) {
      gfx::Rect visual_rect;
      if (visual_rects_it != visual_rects_.end()) {
        visual_rect = *visual_rects_it;
        ++visual_rects_it;
      }

      switch (base_item.type) {
        case DisplayItem::CLIP: {
          const auto& item = static_cast<const ClipDisplayItem&>(base_item);
          std::string output =
              base::StringPrintf("ClipDisplayItem rect: [%s] visualRect: [%s]",
                                 item.clip_rect.ToString().c_str(),
                                 visual_rect.ToString().c_str());
          for (const SkRRect& rounded_rect : item.rounded_clip_rects) {
            base::StringAppendF(
                &output, " rounded_rect: [rect: [%s]",
                gfx::SkRectToRectF(rounded_rect.rect()).ToString().c_str());
            base::StringAppendF(&output, " radii: [");
            SkVector upper_left_radius =
                rounded_rect.radii(SkRRect::kUpperLeft_Corner);
            base::StringAppendF(&output, "[%f,%f],", upper_left_radius.x(),
                                upper_left_radius.y());
            SkVector upper_right_radius =
                rounded_rect.radii(SkRRect::kUpperRight_Corner);
            base::StringAppendF(&output, " [%f,%f],", upper_right_radius.x(),
                                upper_right_radius.y());
            SkVector lower_right_radius =
                rounded_rect.radii(SkRRect::kLowerRight_Corner);
            base::StringAppendF(&output, " [%f,%f],", lower_right_radius.x(),
                                lower_right_radius.y());
            SkVector lower_left_radius =
                rounded_rect.radii(SkRRect::kLowerLeft_Corner);
            base::StringAppendF(&output, " [%f,%f]]", lower_left_radius.x(),
                                lower_left_radius.y());
          }
          state->AppendString(output);
          break;
        }
        case DisplayItem::END_CLIP:
          state->AppendString(
              base::StringPrintf("EndClipDisplayItem visualRect: [%s]",
                                 visual_rect.ToString().c_str()));
          break;
        case DisplayItem::CLIP_PATH: {
          const auto& item = static_cast<const ClipPathDisplayItem&>(base_item);
          state->AppendString(base::StringPrintf(
              "ClipPathDisplayItem length: %d visualRect: [%s]",
              item.clip_path.countPoints(), visual_rect.ToString().c_str()));
          break;
        }
        case DisplayItem::END_CLIP_PATH:
          state->AppendString(
              base::StringPrintf("EndClipPathDisplayItem visualRect: [%s]",
                                 visual_rect.ToString().c_str()));
          break;
        case DisplayItem::COMPOSITING: {
          const auto& item =
              static_cast<const CompositingDisplayItem&>(base_item);
          std::string output = base::StringPrintf(
              "CompositingDisplayItem alpha: %d, xfermode: %d, visualRect: "
              "[%s]",
              item.alpha, static_cast<int>(item.xfermode),
              visual_rect.ToString().c_str());
          if (item.has_bounds) {
            base::StringAppendF(
                &output, ", bounds: [%s]",
                gfx::SkRectToRectF(item.bounds).ToString().c_str());
          }
          state->AppendString(output);
          break;
        }
        case DisplayItem::END_COMPOSITING:
          state->AppendString(
              base::StringPrintf("EndCompositingDisplayItem visualRect: [%s]",
                                 visual_rect.ToString().c_str()));
          break;
        case DisplayItem::DRAWING: {
          const auto& item = static_cast<const DrawingDisplayItem&>(base_item);
          state->BeginDictionary();
          state->SetString("name", "DrawingDisplayItem");

          state->BeginArray("visualRect");
          state->AppendInteger(visual_rect.x());
          state->AppendInteger(visual_rect.y());
          state->AppendInteger(visual_rect.width());
          state->AppendInteger(visual_rect.height());
          state->EndArray();

          state->BeginArray("cullRect");
          state->AppendInteger(item.picture->cullRect().x());
          state->AppendInteger(item.picture->cullRect().y());
          state->AppendInteger(item.picture->cullRect().width());
          state->AppendInteger(item.picture->cullRect().height());
          state->EndArray();

          std::string b64_picture;
          PictureDebugUtil::SerializeAsBase64(ToSkPicture(item.picture).get(),
                                              &b64_picture);
          state->SetString("skp64", b64_picture);
          state->EndDictionary();
          break;
        }
        case DisplayItem::FILTER: {
          const auto& item = static_cast<const FilterDisplayItem&>(base_item);
          state->AppendString(base::StringPrintf(
              "FilterDisplayItem bounds: [%s] visualRect: [%s]",
              item.bounds.ToString().c_str(), visual_rect.ToString().c_str()));
          break;
        }
        case DisplayItem::END_FILTER:
          state->AppendString(
              base::StringPrintf("EndFilterDisplayItem visualRect: [%s]",
                                 visual_rect.ToString().c_str()));
          break;
        case DisplayItem::FLOAT_CLIP: {
          const auto& item =
              static_cast<const FloatClipDisplayItem&>(base_item);
          state->AppendString(base::StringPrintf(
              "FloatClipDisplayItem rect: [%s] visualRect: [%s]",
              item.clip_rect.ToString().c_str(),
              visual_rect.ToString().c_str()));
          break;
        }
        case DisplayItem::END_FLOAT_CLIP:
          state->AppendString(
              base::StringPrintf("EndFloatClipDisplayItem visualRect: [%s]",
                                 visual_rect.ToString().c_str()));
          break;
        case DisplayItem::TRANSFORM: {
          const auto& item =
              static_cast<const TransformDisplayItem&>(base_item);
          state->AppendString(base::StringPrintf(
              "TransformDisplayItem transform: [%s] visualRect: [%s]",
              item.transform.ToString().c_str(),
              visual_rect.ToString().c_str()));
          break;
        }
        case DisplayItem::END_TRANSFORM:
          state->AppendString(
              base::StringPrintf("EndTransformDisplayItem visualRect: [%s]",
                                 visual_rect.ToString().c_str()));
          break;
      }
    }
    state->EndArray();  // "items".
  }

  MathUtil::AddToTracedValue("layer_rect", rtree_.GetBounds(), state.get());
  state->EndDictionary();  // "params".

  {
    SkPictureRecorder recorder;
    gfx::Rect bounds = rtree_.GetBounds();
    SkCanvas* canvas = recorder.beginRecording(bounds.width(), bounds.height());
    canvas->translate(-bounds.x(), -bounds.y());
    canvas->clipRect(gfx::RectToSkRect(bounds));
    Raster(canvas);
    sk_sp<SkPicture> picture = recorder.finishRecordingAsPicture();

    std::string b64_picture;
    PictureDebugUtil::SerializeAsBase64(picture.get(), &b64_picture);
    state->SetString("skp64", b64_picture);
  }

  return state;
}

void DisplayItemList::GenerateDiscardableImagesMetadata() {
  // This should be only called once.
  DCHECK(image_map_.empty());
  if (!has_discardable_images_)
    return;

  gfx::Rect bounds = rtree_.GetBounds();
  DiscardableImageMap::ScopedMetadataGenerator generator(
      &image_map_, gfx::Size(bounds.right(), bounds.bottom()));
  GatherDiscardableImages(generator.image_store());
}

void DisplayItemList::GatherDiscardableImages(
    DiscardableImageStore* image_store) const {
  // TODO(khushalsagar): Could we avoid this if the data was already stored in
  // the |image_map_|?
  SkCanvas* canvas = image_store->GetNoDrawCanvas();
  for (const auto& item : items_) {
    if (item.type == DisplayItem::DRAWING) {
      const auto& drawing_item = static_cast<const DrawingDisplayItem&>(item);
      image_store->GatherDiscardableImages(drawing_item.picture.get());
    } else {
      RasterItem(item, canvas, nullptr);
    }
  }
}

void DisplayItemList::GetDiscardableImagesInRect(
    const gfx::Rect& rect,
    float contents_scale,
    const gfx::ColorSpace& target_color_space,
    std::vector<DrawImage>* images) {
  image_map_.GetDiscardableImagesInRect(rect, contents_scale,
                                        target_color_space, images);
}

gfx::Rect DisplayItemList::GetRectForImage(PaintImage::Id image_id) const {
  return image_map_.GetRectForImage(image_id);
}

}  // namespace cc
