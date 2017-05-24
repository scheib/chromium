// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/paint/DrawingDisplayItem.h"

#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/paint/PaintCanvas.h"
#include "public/platform/WebDisplayItemList.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkData.h"

namespace blink {

void DrawingDisplayItem::Replay(GraphicsContext& context) const {
  if (record_)
    context.DrawRecord(record_);
}

void DrawingDisplayItem::AppendToWebDisplayItemList(
    const IntRect& visual_rect,
    WebDisplayItemList* list) const {
  if (record_)
    list->AppendDrawingItem(visual_rect, record_);
}

bool DrawingDisplayItem::DrawsContent() const {
  return record_.get();
}

int DrawingDisplayItem::NumberOfSlowPaths() const {
  return record_ ? record_->numSlowPaths() : 0;
}

#ifndef NDEBUG
void DrawingDisplayItem::DumpPropertiesAsDebugString(
    StringBuilder& string_builder) const {
  DisplayItem::DumpPropertiesAsDebugString(string_builder);
  if (record_) {
    string_builder.Append(
        String::Format(", rect: [%f,%f %fx%f]", record_->cullRect().x(),
                       record_->cullRect().y(), record_->cullRect().width(),
                       record_->cullRect().height()));
  }
}
#endif

static bool RecordsEqual(sk_sp<const PaintRecord> record1,
                         sk_sp<const PaintRecord> record2) {
  if (record1->size() != record2->size())
    return false;

  // TODO(enne): PaintRecord should have an operator==
  sk_sp<SkData> data1 = ToSkPicture(record1)->serialize();
  sk_sp<SkData> data2 = ToSkPicture(record2)->serialize();
  return data1->equals(data2.get());
}

static SkBitmap RecordToBitmap(sk_sp<const PaintRecord> record) {
  SkBitmap bitmap;
  SkRect rect = record->cullRect();
  bitmap.allocPixels(SkImageInfo::MakeN32Premul(rect.width(), rect.height()));
  SkiaPaintCanvas canvas(bitmap);
  canvas.clear(SK_ColorTRANSPARENT);
  canvas.translate(-rect.x(), -rect.y());
  canvas.drawPicture(std::move(record));
  return bitmap;
}

static bool BitmapsEqual(sk_sp<const PaintRecord> record1,
                         sk_sp<const PaintRecord> record2) {
  SkRect rect = record1->cullRect();
  if (rect != record2->cullRect())
    return false;

  SkBitmap bitmap1 = RecordToBitmap(record1);
  SkBitmap bitmap2 = RecordToBitmap(record2);
  int mismatch_count = 0;
  const int kMaxMismatches = 10;
  for (int y = 0; y < rect.height() && mismatch_count < kMaxMismatches; ++y) {
    for (int x = 0; x < rect.width() && mismatch_count < kMaxMismatches; ++x) {
      SkColor pixel1 = bitmap1.getColor(x, y);
      SkColor pixel2 = bitmap2.getColor(x, y);
      if (pixel1 != pixel2) {
        LOG(ERROR) << "x=" << x << " y=" << y << " " << std::hex << pixel1
                   << " vs " << std::hex << pixel2;
        ++mismatch_count;
      }
    }
  }
  return !mismatch_count;
}

bool DrawingDisplayItem::Equals(const DisplayItem& other) const {
  if (!DisplayItem::Equals(other))
    return false;

  const sk_sp<const PaintRecord>& record = this->GetPaintRecord();
  const sk_sp<const PaintRecord>& other_record =
      static_cast<const DrawingDisplayItem&>(other).GetPaintRecord();

  if (!record && !other_record)
    return true;
  if (!record || !other_record)
    return false;

  if (RecordsEqual(record, other_record))
    return true;

  // Sometimes the client may produce different records for the same visual
  // result, which should be treated as equal.
  return BitmapsEqual(std::move(record), std::move(other_record));
}

}  // namespace blink
