// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/solid_color_content_layer_client.h"

#include <stddef.h>

#include "cc/paint/drawing_display_item.h"
#include "cc/paint/paint_canvas.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_recorder.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/skia_util.h"

namespace cc {

gfx::Rect SolidColorContentLayerClient::PaintableRegion() {
  return gfx::Rect(size_);
}

scoped_refptr<DisplayItemList>
SolidColorContentLayerClient::PaintContentsToDisplayList(
    PaintingControlSetting painting_control) {
  PaintRecorder recorder;
  gfx::Rect clip(PaintableRegion());
  PaintCanvas* canvas = recorder.beginRecording(gfx::RectToSkRect(clip));

  canvas->clear(SK_ColorTRANSPARENT);

  if (border_size_ != 0) {
    PaintFlags flags;
    flags.setStyle(PaintFlags::kFill_Style);
    flags.setColor(border_color_);
    canvas->drawRect(
        SkRect::MakeXYWH(clip.x(), clip.y(), clip.width(), clip.height()),
        flags);
  }

  PaintFlags flags;
  flags.setStyle(PaintFlags::kFill_Style);
  flags.setColor(color_);
  canvas->drawRect(
      SkRect::MakeXYWH(clip.x() + border_size_, clip.y() + border_size_,
                       clip.width() - 2 * border_size_,
                       clip.height() - 2 * border_size_),
      flags);

  auto display_list = make_scoped_refptr(new DisplayItemList);
  display_list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      clip, recorder.finishRecordingAsPicture());

  display_list->Finalize();
  return display_list;
}

bool SolidColorContentLayerClient::FillsBoundsCompletely() const {
  return false;
}

size_t SolidColorContentLayerClient::GetApproximateUnsharedMemoryUsage() const {
  return 0;
}

}  // namespace cc
