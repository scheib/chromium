// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/paint/DisplayItemList.h"

#include "SkTypes.h"
#include "platform/graphics/paint/DrawingDisplayItem.h"
#include "platform/graphics/paint/PaintFlags.h"
#include "platform/graphics/paint/PaintRecorder.h"
#include "platform/graphics/skia/SkiaUtils.h"
#include "platform/testing/FakeDisplayItemClient.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {
namespace {

#define EXPECT_RECT_EQ(expected, actual)                \
  do {                                                  \
    const IntRect& actual_rect = actual;                \
    EXPECT_EQ(expected.X(), actual_rect.X());           \
    EXPECT_EQ(expected.Y(), actual_rect.Y());           \
    EXPECT_EQ(expected.Width(), actual_rect.Width());   \
    EXPECT_EQ(expected.Height(), actual_rect.Height()); \
  } while (false)

static const size_t kDefaultListBytes = 10 * 1024;

class DisplayItemListTest : public ::testing::Test {
 public:
  DisplayItemListTest() : list_(kDefaultListBytes) {}

  DisplayItemList list_;
  FakeDisplayItemClient client_;
};

static sk_sp<PaintRecord> CreateRectRecord(const IntRect& bounds) {
  PaintRecorder recorder;
  PaintCanvas* canvas =
      recorder.beginRecording(bounds.Width(), bounds.Height());
  canvas->drawRect(
      SkRect::MakeXYWH(bounds.X(), bounds.Y(), bounds.Width(), bounds.Height()),
      PaintFlags());
  return recorder.finishRecordingAsPicture();
}

TEST_F(DisplayItemListTest, AppendVisualRect_Simple) {
  IntRect drawing_bounds(5, 6, 7, 8);
  list_.AllocateAndConstruct<DrawingDisplayItem>(
      client_, DisplayItem::Type::kDocumentBackground,
      CreateRectRecord(drawing_bounds), true);
  list_.AppendVisualRect(drawing_bounds);

  EXPECT_EQ(static_cast<size_t>(1), list_.size());
  EXPECT_RECT_EQ(drawing_bounds, list_.VisualRect(0));
}

TEST_F(DisplayItemListTest, AppendVisualRect_BlockContainingDrawing) {
  // TODO(wkorman): Note the visual rects for the paired begin/end are now
  // irrelevant as they're overwritten in cc::DisplayItemList when rebuilt to
  // represent the union of all drawing display item visual rects between the
  // pair. We should consider revising Blink's display item list in some form
  // so as to only store visual rects for drawing display items.
  IntRect drawing_bounds(5, 6, 1, 1);
  list_.AllocateAndConstruct<DrawingDisplayItem>(
      client_, DisplayItem::Type::kDocumentBackground,
      CreateRectRecord(drawing_bounds), true);
  list_.AppendVisualRect(drawing_bounds);

  EXPECT_EQ(static_cast<size_t>(1), list_.size());
  EXPECT_RECT_EQ(drawing_bounds, list_.VisualRect(0));
}
}  // namespace
}  // namespace blink
