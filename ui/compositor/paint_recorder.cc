// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/compositor/paint_recorder.h"

#include "cc/paint/display_item_list.h"
#include "cc/paint/drawing_display_item.h"
#include "cc/paint/paint_recorder.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "ui/compositor/paint_cache.h"
#include "ui/compositor/paint_context.h"
#include "ui/gfx/skia_util.h"

namespace ui {

// This class records a reference to the context, the canvas returned
// by its recorder_, and the cache. Thus all 3 of these must remain
// valid for the lifetime of this object.
PaintRecorder::PaintRecorder(const PaintContext& context,
                             const gfx::Size& recording_size,
                             PaintCache* cache)
    : context_(context),
      canvas_(context.recorder_->beginRecording(
                  gfx::RectToSkRect(gfx::Rect(recording_size))),
              context.device_scale_factor_),
      cache_(cache),
      bounds_in_layer_(context.ToLayerSpaceBounds(recording_size)) {
#if DCHECK_IS_ON()
  DCHECK(!context.inside_paint_recorder_);
  context.inside_paint_recorder_ = true;
#endif
}

PaintRecorder::PaintRecorder(const PaintContext& context,
                             const gfx::Size& recording_size)
    : PaintRecorder(context, recording_size, nullptr) {
}

PaintRecorder::~PaintRecorder() {
#if DCHECK_IS_ON()
  context_.inside_paint_recorder_ = false;
#endif
  const auto& item =
      context_.list_->CreateAndAppendDrawingItem<cc::DrawingDisplayItem>(
          bounds_in_layer_, context_.recorder_->finishRecordingAsPicture());
  if (cache_)
    cache_->SetCache(item);
}

}  // namespace ui
