// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/paint/paint_record.h"

#include "cc/paint/paint_op_buffer.h"
#include "third_party/skia/include/core/SkPictureRecorder.h"

namespace cc {

sk_sp<SkPicture> ToSkPicture(sk_sp<PaintRecord> record) {
  SkPictureRecorder recorder;
  SkCanvas* canvas = recorder.beginRecording(record->cullRect());
  record->playback(canvas);
  return recorder.finishRecordingAsPicture();
}

sk_sp<const SkPicture> ToSkPicture(sk_sp<const PaintRecord> record) {
  SkPictureRecorder recorder;
  SkCanvas* canvas = recorder.beginRecording(record->cullRect());
  record->playback(canvas);
  return recorder.finishRecordingAsPicture();
}

}  // namespace cc
