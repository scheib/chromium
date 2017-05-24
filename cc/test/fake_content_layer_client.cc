// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/fake_content_layer_client.h"

#include <stddef.h>

#include "cc/paint/clip_display_item.h"
#include "cc/paint/drawing_display_item.h"
#include "cc/paint/paint_canvas.h"
#include "cc/paint/paint_recorder.h"
#include "cc/paint/transform_display_item.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/skia_util.h"

namespace cc {

FakeContentLayerClient::ImageData::ImageData(PaintImage img,
                                             const gfx::Point& point,
                                             const PaintFlags& flags)
    : image(std::move(img)), point(point), flags(flags) {}

FakeContentLayerClient::ImageData::ImageData(PaintImage img,
                                             const gfx::Transform& transform,
                                             const PaintFlags& flags)
    : image(std::move(img)), transform(transform), flags(flags) {}

FakeContentLayerClient::ImageData::ImageData(const ImageData& other) = default;

FakeContentLayerClient::ImageData::~ImageData() {}

FakeContentLayerClient::FakeContentLayerClient()
    : fill_with_nonsolid_color_(false),
      last_canvas_(nullptr),
      last_painting_control_(PAINTING_BEHAVIOR_NORMAL),
      reported_memory_usage_(0),
      bounds_set_(false) {}

FakeContentLayerClient::~FakeContentLayerClient() {
}

gfx::Rect FakeContentLayerClient::PaintableRegion() {
  CHECK(bounds_set_);
  return gfx::Rect(bounds_);
}

scoped_refptr<DisplayItemList>
FakeContentLayerClient::PaintContentsToDisplayList(
    PaintingControlSetting painting_control) {
  auto display_list = make_scoped_refptr(new DisplayItemList);
  display_list->SetRetainVisualRectsForTesting(true);
  PaintRecorder recorder;

  for (RectPaintVector::const_iterator it = draw_rects_.begin();
       it != draw_rects_.end(); ++it) {
    const gfx::RectF& draw_rect = it->first;
    const PaintFlags& flags = it->second;
    PaintCanvas* canvas =
        recorder.beginRecording(gfx::RectFToSkRect(draw_rect));
    canvas->drawRect(gfx::RectFToSkRect(draw_rect), flags);
    display_list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
        ToEnclosingRect(draw_rect), recorder.finishRecordingAsPicture());
  }

  for (ImageVector::const_iterator it = draw_images_.begin();
       it != draw_images_.end(); ++it) {
    if (!it->transform.IsIdentity()) {
      display_list->CreateAndAppendPairedBeginItem<TransformDisplayItem>(
          it->transform);
    }
    PaintCanvas* canvas = recorder.beginRecording(
        it->image.sk_image()->width(), it->image.sk_image()->height());
    canvas->drawImage(it->image, it->point.x(), it->point.y(), &it->flags);
    display_list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
        PaintableRegion(), recorder.finishRecordingAsPicture());
    if (!it->transform.IsIdentity()) {
      display_list->CreateAndAppendPairedEndItem<EndTransformDisplayItem>();
    }
  }

  if (fill_with_nonsolid_color_) {
    gfx::Rect draw_rect = PaintableRegion();
    bool red = true;
    while (!draw_rect.IsEmpty()) {
      PaintFlags flags;
      flags.setColor(red ? SK_ColorRED : SK_ColorBLUE);
      PaintCanvas* canvas =
          recorder.beginRecording(gfx::RectToSkRect(draw_rect));
      canvas->drawIRect(gfx::RectToSkIRect(draw_rect), flags);
      display_list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
          draw_rect, recorder.finishRecordingAsPicture());
      draw_rect.Inset(1, 1);
    }
  }

  display_list->Finalize();
  return display_list;
}

bool FakeContentLayerClient::FillsBoundsCompletely() const { return false; }

size_t FakeContentLayerClient::GetApproximateUnsharedMemoryUsage() const {
  return reported_memory_usage_;
}

}  // namespace cc
