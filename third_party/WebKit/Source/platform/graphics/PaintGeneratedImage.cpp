// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/PaintGeneratedImage.h"

#include "platform/geometry/FloatRect.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/paint/PaintRecord.h"

namespace blink {

void PaintGeneratedImage::Draw(PaintCanvas* canvas,
                               const PaintFlags& flags,
                               const FloatRect& dest_rect,
                               const FloatRect& src_rect,
                               RespectImageOrientationEnum,
                               ImageClampingMode) {
  PaintCanvasAutoRestore ar(canvas, true);
  canvas->clipRect(dest_rect);
  canvas->translate(dest_rect.X(), dest_rect.Y());
  if (dest_rect.Size() != src_rect.Size())
    canvas->scale(dest_rect.Width() / src_rect.Width(),
                  dest_rect.Height() / src_rect.Height());
  canvas->translate(-src_rect.X(), -src_rect.Y());
  SkRect bounds = record_->cullRect();
  canvas->saveLayer(&bounds, &flags);
  canvas->drawPicture(record_);
}

void PaintGeneratedImage::DrawTile(GraphicsContext& context,
                                   const FloatRect& src_rect) {
  context.DrawRecord(record_);
}

}  // namespace blink
