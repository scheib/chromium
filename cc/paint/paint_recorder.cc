// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/paint/paint_recorder.h"

#include "cc/paint/paint_op_buffer.h"

namespace cc {

PaintRecorder::PaintRecorder() = default;

PaintRecorder::~PaintRecorder() = default;

PaintCanvas* PaintRecorder::beginRecording(const SkRect& bounds) {
  buffer_.reset(new PaintOpBuffer(bounds));
  canvas_.emplace(buffer_.get());
  return getRecordingCanvas();
}

sk_sp<PaintRecord> PaintRecorder::finishRecordingAsPicture() {
  // SkPictureRecorder users expect that their saves are automatically
  // closed for them.
  //
  // NOTE: Blink paint in general doesn't appear to need this, but the
  // RecordingImageBufferSurface::fallBackToRasterCanvas finishing off the
  // current frame depends on this.  Maybe we could remove this assumption and
  // just have callers do it.
  canvas_->restoreToCount(1);

  // Some users (e.g. printing) use the existence of the recording canvas
  // to know if recording is finished, so reset it here.
  canvas_.reset();

  buffer_->ShrinkToFit();
  return std::move(buffer_);
}

}  // namespace cc
