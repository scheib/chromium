// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/paint/display_item_list.h"

#include <stddef.h>

#include <vector>

#include "base/memory/ptr_util.h"
#include "base/trace_event/trace_event_argument.h"
#include "cc/base/filter_operation.h"
#include "cc/base/filter_operations.h"
#include "cc/paint/clip_display_item.h"
#include "cc/paint/clip_path_display_item.h"
#include "cc/paint/compositing_display_item.h"
#include "cc/paint/drawing_display_item.h"
#include "cc/paint/filter_display_item.h"
#include "cc/paint/float_clip_display_item.h"
#include "cc/paint/paint_canvas.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_record.h"
#include "cc/paint/paint_recorder.h"
#include "cc/paint/skia_paint_canvas.h"
#include "cc/paint/transform_display_item.h"
#include "cc/test/geometry_test_utils.h"
#include "cc/test/pixel_test_utils.h"
#include "cc/test/skia_common.h"
#include "cc/test/test_skcanvas.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/effects/SkColorMatrixFilter.h"
#include "third_party/skia/include/effects/SkImageSource.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/skia_util.h"

namespace cc {

namespace {

bool CompareN32Pixels(void* actual_pixels,
                      void* expected_pixels,
                      int width,
                      int height) {
  if (memcmp(actual_pixels, expected_pixels, 4 * width * height) == 0)
    return true;

  SkImageInfo actual_info = SkImageInfo::MakeN32Premul(width, height);
  SkBitmap actual_bitmap;
  actual_bitmap.installPixels(actual_info, actual_pixels,
                              actual_info.minRowBytes());

  SkImageInfo expected_info = SkImageInfo::MakeN32Premul(width, height);
  SkBitmap expected_bitmap;
  expected_bitmap.installPixels(expected_info, expected_pixels,
                                expected_info.minRowBytes());

  std::string gen_bmp_data_url = GetPNGDataUrl(actual_bitmap);
  std::string ref_bmp_data_url = GetPNGDataUrl(expected_bitmap);

  LOG(ERROR) << "Pixels do not match!";
  LOG(ERROR) << "Actual: " << gen_bmp_data_url;
  LOG(ERROR) << "Expected: " << ref_bmp_data_url;
  return false;
}

const gfx::Rect kVisualRect(0, 0, 42, 42);

sk_sp<const PaintRecord> CreateRectPicture(const gfx::Rect& bounds) {
  PaintRecorder recorder;
  PaintCanvas* canvas =
      recorder.beginRecording(bounds.width(), bounds.height());
  canvas->drawRect(
      SkRect::MakeXYWH(bounds.x(), bounds.y(), bounds.width(), bounds.height()),
      PaintFlags());
  return recorder.finishRecordingAsPicture();
}

sk_sp<const PaintRecord> CreateRectPictureWithAlpha(const gfx::Rect& bounds,
                                                    uint8_t alpha) {
  PaintRecorder recorder;
  PaintCanvas* canvas =
      recorder.beginRecording(bounds.width(), bounds.height());
  PaintFlags flags;
  flags.setAlpha(alpha);
  canvas->drawRect(
      SkRect::MakeXYWH(bounds.x(), bounds.y(), bounds.width(), bounds.height()),
      flags);
  return recorder.finishRecordingAsPicture();
}

void AppendFirstSerializationTestPicture(scoped_refptr<DisplayItemList> list,
                                         const gfx::Size& layer_size) {
  gfx::PointF offset(2.f, 3.f);
  PaintRecorder recorder;

  PaintFlags red_paint;
  red_paint.setColor(SK_ColorRED);

  PaintCanvas* canvas = recorder.beginRecording(SkRect::MakeXYWH(
      offset.x(), offset.y(), layer_size.width(), layer_size.height()));
  canvas->translate(offset.x(), offset.y());
  canvas->drawRect(SkRect::MakeWH(4, 4), red_paint);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      kVisualRect, recorder.finishRecordingAsPicture());
}

}  // namespace

TEST(DisplayItemListTest, SingleDrawingItem) {
  gfx::Rect layer_rect(100, 100);
  PaintRecorder recorder;
  PaintFlags blue_flags;
  blue_flags.setColor(SK_ColorBLUE);
  PaintFlags red_paint;
  red_paint.setColor(SK_ColorRED);
  unsigned char pixels[4 * 100 * 100] = {0};
  auto list = make_scoped_refptr(new DisplayItemList);

  gfx::PointF offset(8.f, 9.f);
  gfx::RectF recording_rect(offset, gfx::SizeF(layer_rect.size()));
  PaintCanvas* canvas =
      recorder.beginRecording(gfx::RectFToSkRect(recording_rect));
  canvas->translate(offset.x(), offset.y());
  canvas->drawRect(SkRect::MakeLTRB(0.f, 0.f, 60.f, 60.f), red_paint);
  canvas->drawRect(SkRect::MakeLTRB(50.f, 50.f, 75.f, 75.f), blue_flags);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      kVisualRect, recorder.finishRecordingAsPicture());
  list->Finalize();
  DrawDisplayList(pixels, layer_rect, list);

  SkBitmap expected_bitmap;
  unsigned char expected_pixels[4 * 100 * 100] = {0};
  SkImageInfo info =
      SkImageInfo::MakeN32Premul(layer_rect.width(), layer_rect.height());
  expected_bitmap.installPixels(info, expected_pixels, info.minRowBytes());
  SkiaPaintCanvas expected_canvas(expected_bitmap);
  expected_canvas.clipRect(gfx::RectToSkRect(layer_rect));
  expected_canvas.drawRect(
      SkRect::MakeLTRB(0.f + offset.x(), 0.f + offset.y(), 60.f + offset.x(),
                       60.f + offset.y()),
      red_paint);
  expected_canvas.drawRect(
      SkRect::MakeLTRB(50.f + offset.x(), 50.f + offset.y(), 75.f + offset.x(),
                       75.f + offset.y()),
      blue_flags);

  EXPECT_TRUE(CompareN32Pixels(pixels, expected_pixels, 100, 100));
}

TEST(DisplayItemListTest, ClipItem) {
  gfx::Rect layer_rect(100, 100);
  PaintRecorder recorder;
  PaintFlags blue_flags;
  blue_flags.setColor(SK_ColorBLUE);
  PaintFlags red_paint;
  red_paint.setColor(SK_ColorRED);
  unsigned char pixels[4 * 100 * 100] = {0};
  auto list = make_scoped_refptr(new DisplayItemList);

  gfx::PointF first_offset(8.f, 9.f);
  gfx::RectF first_recording_rect(first_offset, gfx::SizeF(layer_rect.size()));
  PaintCanvas* canvas =
      recorder.beginRecording(gfx::RectFToSkRect(first_recording_rect));
  canvas->translate(first_offset.x(), first_offset.y());
  canvas->drawRect(SkRect::MakeWH(60, 60), red_paint);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      kVisualRect, recorder.finishRecordingAsPicture());

  gfx::Rect clip_rect(60, 60, 10, 10);
  list->CreateAndAppendPairedBeginItem<ClipDisplayItem>(
      clip_rect, std::vector<SkRRect>(), true);

  gfx::PointF second_offset(2.f, 3.f);
  gfx::RectF second_recording_rect(second_offset,
                                   gfx::SizeF(layer_rect.size()));
  canvas = recorder.beginRecording(gfx::RectFToSkRect(second_recording_rect));
  canvas->translate(second_offset.x(), second_offset.y());
  canvas->drawRect(SkRect::MakeLTRB(50.f, 50.f, 75.f, 75.f), blue_flags);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      kVisualRect, recorder.finishRecordingAsPicture());

  list->CreateAndAppendPairedEndItem<EndClipDisplayItem>();
  list->Finalize();

  DrawDisplayList(pixels, layer_rect, list);

  SkBitmap expected_bitmap;
  unsigned char expected_pixels[4 * 100 * 100] = {0};
  SkImageInfo info =
      SkImageInfo::MakeN32Premul(layer_rect.width(), layer_rect.height());
  expected_bitmap.installPixels(info, expected_pixels, info.minRowBytes());
  SkiaPaintCanvas expected_canvas(expected_bitmap);
  expected_canvas.clipRect(gfx::RectToSkRect(layer_rect));
  expected_canvas.drawRect(
      SkRect::MakeLTRB(0.f + first_offset.x(), 0.f + first_offset.y(),
                       60.f + first_offset.x(), 60.f + first_offset.y()),
      red_paint);
  expected_canvas.clipRect(gfx::RectToSkRect(clip_rect));
  expected_canvas.drawRect(
      SkRect::MakeLTRB(50.f + second_offset.x(), 50.f + second_offset.y(),
                       75.f + second_offset.x(), 75.f + second_offset.y()),
      blue_flags);

  EXPECT_TRUE(CompareN32Pixels(pixels, expected_pixels, 100, 100));
}

TEST(DisplayItemListTest, TransformItem) {
  gfx::Rect layer_rect(100, 100);
  PaintRecorder recorder;
  PaintFlags blue_flags;
  blue_flags.setColor(SK_ColorBLUE);
  PaintFlags red_paint;
  red_paint.setColor(SK_ColorRED);
  unsigned char pixels[4 * 100 * 100] = {0};
  auto list = make_scoped_refptr(new DisplayItemList);

  gfx::PointF first_offset(8.f, 9.f);
  gfx::RectF first_recording_rect(first_offset, gfx::SizeF(layer_rect.size()));
  PaintCanvas* canvas =
      recorder.beginRecording(gfx::RectFToSkRect(first_recording_rect));
  canvas->translate(first_offset.x(), first_offset.y());
  canvas->drawRect(SkRect::MakeWH(60, 60), red_paint);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      kVisualRect, recorder.finishRecordingAsPicture());

  gfx::Transform transform;
  transform.Rotate(45.0);
  list->CreateAndAppendPairedBeginItem<TransformDisplayItem>(transform);

  gfx::PointF second_offset(2.f, 3.f);
  gfx::RectF second_recording_rect(second_offset,
                                   gfx::SizeF(layer_rect.size()));
  canvas = recorder.beginRecording(gfx::RectFToSkRect(second_recording_rect));
  canvas->translate(second_offset.x(), second_offset.y());
  canvas->drawRect(SkRect::MakeLTRB(50.f, 50.f, 75.f, 75.f), blue_flags);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      kVisualRect, recorder.finishRecordingAsPicture());

  list->CreateAndAppendPairedEndItem<EndTransformDisplayItem>();
  list->Finalize();

  DrawDisplayList(pixels, layer_rect, list);

  SkBitmap expected_bitmap;
  unsigned char expected_pixels[4 * 100 * 100] = {0};
  SkImageInfo info =
      SkImageInfo::MakeN32Premul(layer_rect.width(), layer_rect.height());
  expected_bitmap.installPixels(info, expected_pixels, info.minRowBytes());
  SkiaPaintCanvas expected_canvas(expected_bitmap);
  expected_canvas.clipRect(gfx::RectToSkRect(layer_rect));
  expected_canvas.drawRect(
      SkRect::MakeLTRB(0.f + first_offset.x(), 0.f + first_offset.y(),
                       60.f + first_offset.x(), 60.f + first_offset.y()),
      red_paint);
  expected_canvas.setMatrix(transform.matrix());
  expected_canvas.drawRect(
      SkRect::MakeLTRB(50.f + second_offset.x(), 50.f + second_offset.y(),
                       75.f + second_offset.x(), 75.f + second_offset.y()),
      blue_flags);

  EXPECT_TRUE(CompareN32Pixels(pixels, expected_pixels, 100, 100));
}

TEST(DisplayItemListTest, FilterItem) {
  gfx::Rect layer_rect(100, 100);
  FilterOperations filters;
  unsigned char pixels[4 * 100 * 100] = {0};
  auto list = make_scoped_refptr(new DisplayItemList);

  sk_sp<SkSurface> source_surface = SkSurface::MakeRasterN32Premul(50, 50);
  SkCanvas* source_canvas = source_surface->getCanvas();
  source_canvas->clear(SkColorSetRGB(128, 128, 128));
  sk_sp<SkImage> source_image = source_surface->makeImageSnapshot();

  // For most SkImageFilters, the |dst| bounds computed by computeFastBounds are
  // dependent on the provided |src| bounds. This means, for example, that
  // translating |src| results in a corresponding translation of |dst|. But this
  // is not the case for all SkImageFilters; for some of them (e.g.
  // SkImageSource), the computation of |dst| in computeFastBounds doesn't
  // involve |src| at all. Incorrectly assuming such a relationship (e.g. by
  // translating |dst| after it is computed by computeFastBounds, rather than
  // translating |src| before it provided to computedFastBounds) can cause
  // incorrect clipping of filter output. To test for this, we include an
  // SkImageSource filter in |filters|. Here, |src| is |filter_bounds|, defined
  // below.
  sk_sp<SkImageFilter> image_filter = SkImageSource::Make(source_image);
  filters.Append(FilterOperation::CreateReferenceFilter(image_filter));
  filters.Append(FilterOperation::CreateBrightnessFilter(0.5f));
  gfx::RectF filter_bounds(10.f, 10.f, 50.f, 50.f);
  list->CreateAndAppendPairedBeginItem<FilterDisplayItem>(
      filters, filter_bounds, filter_bounds.origin());

  // Include a rect drawing so that filter is actually applied to something.
  {
    PaintRecorder recorder;

    PaintFlags red_paint;
    red_paint.setColor(SK_ColorRED);

    PaintCanvas* canvas = recorder.beginRecording(
        SkRect::MakeXYWH(0, 0, layer_rect.width(), layer_rect.height()));
    canvas->drawRect(
        SkRect::MakeLTRB(filter_bounds.x(), filter_bounds.y(),
                         filter_bounds.right(), filter_bounds.bottom()),
        red_paint);
    list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
        ToNearestRect(filter_bounds), recorder.finishRecordingAsPicture());
  }

  list->CreateAndAppendPairedEndItem<EndFilterDisplayItem>();
  list->Finalize();

  DrawDisplayList(pixels, layer_rect, list);

  SkBitmap expected_bitmap;
  unsigned char expected_pixels[4 * 100 * 100] = {0};
  PaintFlags paint;
  paint.setColor(SkColorSetRGB(64, 64, 64));
  SkImageInfo info =
      SkImageInfo::MakeN32Premul(layer_rect.width(), layer_rect.height());
  expected_bitmap.installPixels(info, expected_pixels, info.minRowBytes());
  SkiaPaintCanvas expected_canvas(expected_bitmap);
  expected_canvas.drawRect(RectFToSkRect(filter_bounds), paint);

  EXPECT_TRUE(CompareN32Pixels(pixels, expected_pixels, 100, 100));
}

TEST(DisplayItemListTest, ApproximateMemoryUsage) {
  const int kNumCommandsInTestSkPicture = 1000;
  size_t memory_usage;

  // Make an PaintRecord whose size is known.
  gfx::Rect layer_rect(100, 100);
  PaintRecorder recorder;
  PaintFlags blue_flags;
  blue_flags.setColor(SK_ColorBLUE);
  PaintCanvas* canvas = recorder.beginRecording(gfx::RectToSkRect(layer_rect));
  for (int i = 0; i < kNumCommandsInTestSkPicture; i++)
    canvas->drawRect(SkRect(), blue_flags);
  sk_sp<PaintRecord> record = recorder.finishRecordingAsPicture();
  size_t record_size = record->bytes_used();
  ASSERT_GE(record_size, kNumCommandsInTestSkPicture * sizeof(SkRect));

  auto list = make_scoped_refptr(new DisplayItemList);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(kVisualRect, record);
  list->Finalize();
  memory_usage = list->ApproximateMemoryUsage();
  EXPECT_GE(memory_usage, record_size);
  EXPECT_LE(memory_usage, 2 * record_size);
}

TEST(DisplayItemListTest, AsValueWithNoItems) {
  auto list = make_scoped_refptr(new DisplayItemList);
  list->SetRetainVisualRectsForTesting(true);
  list->Finalize();

  std::string value = list->CreateTracedValue(true)->ToString();
  EXPECT_EQ(value.find("\"layer_rect\": [0,0,0,0]"), std::string::npos);
  EXPECT_NE(value.find("\"items\":[]"), std::string::npos);
  EXPECT_EQ(value.find("visualRect: [0,0 42x42]"), std::string::npos);
  EXPECT_NE(value.find("\"skp64\":"), std::string::npos);

  value = list->CreateTracedValue(false)->ToString();
  EXPECT_EQ(value.find("\"layer_rect\": [0,0,0,0]"), std::string::npos);
  EXPECT_EQ(value.find("\"items\":"), std::string::npos);
  EXPECT_EQ(value.find("visualRect: [0,0 42x42]"), std::string::npos);
  EXPECT_NE(value.find("\"skp64\":"), std::string::npos);
}

TEST(DisplayItemListTest, AsValueWithItems) {
  gfx::Rect layer_rect = gfx::Rect(1, 2, 8, 9);
  auto list = make_scoped_refptr(new DisplayItemList);
  list->SetRetainVisualRectsForTesting(true);
  gfx::Transform transform;
  transform.Translate(6.f, 7.f);
  list->CreateAndAppendPairedBeginItem<TransformDisplayItem>(transform);
  AppendFirstSerializationTestPicture(list, layer_rect.size());
  list->CreateAndAppendPairedEndItem<EndTransformDisplayItem>();
  list->Finalize();

  std::string value = list->CreateTracedValue(true)->ToString();
  EXPECT_EQ(value.find("\"layer_rect\": [0,0,42,42]"), std::string::npos);
  EXPECT_NE(value.find("{\"items\":[\"TransformDisplayItem"),
            std::string::npos);
  EXPECT_NE(value.find("visualRect: [0,0 42x42]"), std::string::npos);
  EXPECT_NE(value.find("\"skp64\":"), std::string::npos);

  value = list->CreateTracedValue(false)->ToString();
  EXPECT_EQ(value.find("\"layer_rect\": [0,0,42,42]"), std::string::npos);
  EXPECT_EQ(value.find("{\"items\":[\"TransformDisplayItem"),
            std::string::npos);
  EXPECT_EQ(value.find("visualRect: [0,0 42x42]"), std::string::npos);
  EXPECT_NE(value.find("\"skp64\":"), std::string::npos);
}

TEST(DisplayItemListTest, SizeEmpty) {
  auto list = make_scoped_refptr(new DisplayItemList);
  EXPECT_EQ(0u, list->size());
}

TEST(DisplayItemListTest, SizeOne) {
  auto list = make_scoped_refptr(new DisplayItemList);
  gfx::Rect drawing_bounds(5, 6, 1, 1);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_bounds, CreateRectPicture(drawing_bounds));
  EXPECT_EQ(1u, list->size());
}

TEST(DisplayItemListTest, SizeMultiple) {
  auto list = make_scoped_refptr(new DisplayItemList);
  gfx::Rect clip_bounds(5, 6, 7, 8);
  list->CreateAndAppendPairedBeginItem<ClipDisplayItem>(
      clip_bounds, std::vector<SkRRect>(), true);
  list->CreateAndAppendPairedEndItem<EndClipDisplayItem>();
  EXPECT_EQ(2u, list->size());
}

TEST(DisplayItemListTest, AppendVisualRectSimple) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // One drawing: D.

  gfx::Rect drawing_bounds(5, 6, 7, 8);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_bounds, CreateRectPicture(drawing_bounds));

  EXPECT_EQ(1u, list->size());
  EXPECT_RECT_EQ(drawing_bounds, list->VisualRectForTesting(0));
}

TEST(DisplayItemListTest, AppendVisualRectEmptyBlock) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // One block: B1, E1.

  gfx::Rect clip_bounds(5, 6, 7, 8);
  list->CreateAndAppendPairedBeginItem<ClipDisplayItem>(
      clip_bounds, std::vector<SkRRect>(), true);

  list->CreateAndAppendPairedEndItem<EndClipDisplayItem>();

  EXPECT_EQ(2u, list->size());
  EXPECT_RECT_EQ(gfx::Rect(), list->VisualRectForTesting(0));
  EXPECT_RECT_EQ(gfx::Rect(), list->VisualRectForTesting(1));
}

TEST(DisplayItemListTest, AppendVisualRectEmptyBlockContainingEmptyBlock) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // Two nested blocks: B1, B2, E2, E1.

  gfx::Rect clip_bounds(5, 6, 7, 8);
  list->CreateAndAppendPairedBeginItem<ClipDisplayItem>(
      clip_bounds, std::vector<SkRRect>(), true);
  list->CreateAndAppendPairedBeginItem<TransformDisplayItem>(gfx::Transform());
  list->CreateAndAppendPairedEndItem<EndTransformDisplayItem>();
  list->CreateAndAppendPairedEndItem<EndClipDisplayItem>();

  EXPECT_EQ(4u, list->size());
  EXPECT_RECT_EQ(gfx::Rect(), list->VisualRectForTesting(0));
  EXPECT_RECT_EQ(gfx::Rect(), list->VisualRectForTesting(1));
  EXPECT_RECT_EQ(gfx::Rect(), list->VisualRectForTesting(2));
  EXPECT_RECT_EQ(gfx::Rect(), list->VisualRectForTesting(3));
}

TEST(DisplayItemListTest, AppendVisualRectBlockContainingDrawing) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // One block with one drawing: B1, Da, E1.

  gfx::Rect clip_bounds(5, 6, 7, 8);
  list->CreateAndAppendPairedBeginItem<ClipDisplayItem>(
      clip_bounds, std::vector<SkRRect>(), true);

  gfx::Rect drawing_bounds(5, 6, 1, 1);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_bounds, CreateRectPicture(drawing_bounds));

  list->CreateAndAppendPairedEndItem<EndClipDisplayItem>();

  EXPECT_EQ(3u, list->size());
  EXPECT_RECT_EQ(drawing_bounds, list->VisualRectForTesting(0));
  EXPECT_RECT_EQ(drawing_bounds, list->VisualRectForTesting(1));
  EXPECT_RECT_EQ(drawing_bounds, list->VisualRectForTesting(2));
}

TEST(DisplayItemListTest, AppendVisualRectBlockContainingEscapedDrawing) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // One block with one drawing: B1, Da (escapes), E1.

  gfx::Rect clip_bounds(5, 6, 7, 8);
  list->CreateAndAppendPairedBeginItem<ClipDisplayItem>(
      clip_bounds, std::vector<SkRRect>(), true);

  gfx::Rect drawing_bounds(1, 2, 3, 4);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_bounds, CreateRectPicture(drawing_bounds));

  list->CreateAndAppendPairedEndItem<EndClipDisplayItem>();

  EXPECT_EQ(3u, list->size());
  EXPECT_RECT_EQ(drawing_bounds, list->VisualRectForTesting(0));
  EXPECT_RECT_EQ(drawing_bounds, list->VisualRectForTesting(1));
  EXPECT_RECT_EQ(drawing_bounds, list->VisualRectForTesting(2));
}

TEST(DisplayItemListTest,
     AppendVisualRectDrawingFollowedByBlockContainingEscapedDrawing) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // One drawing followed by one block with one drawing: Da, B1, Db (escapes),
  // E1.

  gfx::Rect drawing_a_bounds(1, 2, 3, 4);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_a_bounds, CreateRectPicture(drawing_a_bounds));

  gfx::Rect clip_bounds(5, 6, 7, 8);
  list->CreateAndAppendPairedBeginItem<ClipDisplayItem>(
      clip_bounds, std::vector<SkRRect>(), true);

  gfx::Rect drawing_b_bounds(13, 14, 1, 1);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_b_bounds, CreateRectPicture(drawing_b_bounds));

  list->CreateAndAppendPairedEndItem<EndClipDisplayItem>();

  EXPECT_EQ(4u, list->size());
  EXPECT_RECT_EQ(drawing_a_bounds, list->VisualRectForTesting(0));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(1));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(2));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(3));
}

TEST(DisplayItemListTest, AppendVisualRectTwoBlocksTwoDrawings) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // Multiple nested blocks with drawings amidst: B1, Da, B2, Db, E2, E1.

  gfx::Rect clip_bounds(5, 6, 7, 8);
  list->CreateAndAppendPairedBeginItem<ClipDisplayItem>(
      clip_bounds, std::vector<SkRRect>(), true);

  gfx::Rect drawing_a_bounds(5, 6, 1, 1);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_a_bounds, CreateRectPicture(drawing_a_bounds));

  list->CreateAndAppendPairedBeginItem<TransformDisplayItem>(gfx::Transform());

  gfx::Rect drawing_b_bounds(7, 8, 1, 1);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_b_bounds, CreateRectPicture(drawing_b_bounds));

  list->CreateAndAppendPairedEndItem<EndTransformDisplayItem>();
  list->CreateAndAppendPairedEndItem<EndClipDisplayItem>();

  EXPECT_EQ(6u, list->size());
  gfx::Rect merged_drawing_bounds = gfx::Rect(drawing_a_bounds);
  merged_drawing_bounds.Union(drawing_b_bounds);
  EXPECT_RECT_EQ(merged_drawing_bounds, list->VisualRectForTesting(0));
  EXPECT_RECT_EQ(drawing_a_bounds, list->VisualRectForTesting(1));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(2));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(3));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(4));
  EXPECT_RECT_EQ(merged_drawing_bounds, list->VisualRectForTesting(5));
}

TEST(DisplayItemListTest,
     AppendVisualRectTwoBlocksTwoDrawingsInnerDrawingEscaped) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // Multiple nested blocks with drawings amidst: B1, Da, B2, Db (escapes), E2,
  // E1.

  gfx::Rect clip_bounds(5, 6, 7, 8);
  list->CreateAndAppendPairedBeginItem<ClipDisplayItem>(
      clip_bounds, std::vector<SkRRect>(), true);

  gfx::Rect drawing_a_bounds(5, 6, 1, 1);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_a_bounds, CreateRectPicture(drawing_a_bounds));

  list->CreateAndAppendPairedBeginItem<TransformDisplayItem>(gfx::Transform());

  gfx::Rect drawing_b_bounds(1, 2, 3, 4);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_b_bounds, CreateRectPicture(drawing_b_bounds));

  list->CreateAndAppendPairedEndItem<EndTransformDisplayItem>();
  list->CreateAndAppendPairedEndItem<EndClipDisplayItem>();

  EXPECT_EQ(6u, list->size());
  gfx::Rect merged_drawing_bounds = gfx::Rect(drawing_a_bounds);
  merged_drawing_bounds.Union(drawing_b_bounds);
  EXPECT_RECT_EQ(merged_drawing_bounds, list->VisualRectForTesting(0));
  EXPECT_RECT_EQ(drawing_a_bounds, list->VisualRectForTesting(1));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(2));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(3));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(4));
  EXPECT_RECT_EQ(merged_drawing_bounds, list->VisualRectForTesting(5));
}

TEST(DisplayItemListTest,
     AppendVisualRectTwoBlocksTwoDrawingsOuterDrawingEscaped) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // Multiple nested blocks with drawings amidst: B1, Da (escapes), B2, Db, E2,
  // E1.

  gfx::Rect clip_bounds(5, 6, 7, 8);
  list->CreateAndAppendPairedBeginItem<ClipDisplayItem>(
      clip_bounds, std::vector<SkRRect>(), true);

  gfx::Rect drawing_a_bounds(1, 2, 3, 4);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_a_bounds, CreateRectPicture(drawing_a_bounds));

  list->CreateAndAppendPairedBeginItem<TransformDisplayItem>(gfx::Transform());

  gfx::Rect drawing_b_bounds(7, 8, 1, 1);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_b_bounds, CreateRectPicture(drawing_b_bounds));

  list->CreateAndAppendPairedEndItem<EndTransformDisplayItem>();
  list->CreateAndAppendPairedEndItem<EndClipDisplayItem>();

  EXPECT_EQ(6u, list->size());
  gfx::Rect merged_drawing_bounds = gfx::Rect(drawing_a_bounds);
  merged_drawing_bounds.Union(drawing_b_bounds);
  EXPECT_RECT_EQ(merged_drawing_bounds, list->VisualRectForTesting(0));
  EXPECT_RECT_EQ(drawing_a_bounds, list->VisualRectForTesting(1));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(2));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(3));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(4));
  EXPECT_RECT_EQ(merged_drawing_bounds, list->VisualRectForTesting(5));
}

TEST(DisplayItemListTest,
     AppendVisualRectTwoBlocksTwoDrawingsBothDrawingsEscaped) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // Multiple nested blocks with drawings amidst:
  // B1, Da (escapes to the right), B2, Db (escapes to the left), E2, E1.

  gfx::Rect clip_bounds(5, 6, 7, 8);
  list->CreateAndAppendPairedBeginItem<ClipDisplayItem>(
      clip_bounds, std::vector<SkRRect>(), true);

  gfx::Rect drawing_a_bounds(13, 14, 1, 1);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_a_bounds, CreateRectPicture(drawing_a_bounds));

  list->CreateAndAppendPairedBeginItem<TransformDisplayItem>(gfx::Transform());

  gfx::Rect drawing_b_bounds(1, 2, 3, 4);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      drawing_b_bounds, CreateRectPicture(drawing_b_bounds));

  list->CreateAndAppendPairedEndItem<EndTransformDisplayItem>();
  list->CreateAndAppendPairedEndItem<EndClipDisplayItem>();

  EXPECT_EQ(6u, list->size());
  gfx::Rect merged_drawing_bounds = gfx::Rect(drawing_a_bounds);
  merged_drawing_bounds.Union(drawing_b_bounds);
  EXPECT_RECT_EQ(merged_drawing_bounds, list->VisualRectForTesting(0));
  EXPECT_RECT_EQ(drawing_a_bounds, list->VisualRectForTesting(1));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(2));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(3));
  EXPECT_RECT_EQ(drawing_b_bounds, list->VisualRectForTesting(4));
  EXPECT_RECT_EQ(merged_drawing_bounds, list->VisualRectForTesting(5));
}

TEST(DisplayItemListTest, AppendVisualRectOneFilterNoDrawings) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // One filter containing no drawings: Bf, Ef

  gfx::Rect filter_bounds(5, 6, 1, 1);
  list->CreateAndAppendPairedBeginItemWithVisualRect<FilterDisplayItem>(
      filter_bounds, FilterOperations(), gfx::RectF(filter_bounds),
      gfx::PointF(filter_bounds.origin()));

  list->CreateAndAppendPairedEndItem<EndFilterDisplayItem>();

  EXPECT_EQ(2u, list->size());
  EXPECT_RECT_EQ(filter_bounds, list->VisualRectForTesting(0));
  EXPECT_RECT_EQ(filter_bounds, list->VisualRectForTesting(1));
}

TEST(DisplayItemListTest, AppendVisualRectBlockContainingFilterNoDrawings) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // One block containing one filter and no drawings: B1, Bf, Ef, E1.

  gfx::Rect clip_bounds(5, 6, 7, 8);
  list->CreateAndAppendPairedBeginItem<ClipDisplayItem>(
      clip_bounds, std::vector<SkRRect>(), true);

  gfx::Rect filter_bounds(5, 6, 1, 1);
  list->CreateAndAppendPairedBeginItemWithVisualRect<FilterDisplayItem>(
      filter_bounds, FilterOperations(), gfx::RectF(filter_bounds),
      gfx::PointF(filter_bounds.origin()));

  list->CreateAndAppendPairedEndItem<EndFilterDisplayItem>();
  list->CreateAndAppendPairedEndItem<EndClipDisplayItem>();

  EXPECT_EQ(4u, list->size());
  EXPECT_RECT_EQ(filter_bounds, list->VisualRectForTesting(0));
  EXPECT_RECT_EQ(filter_bounds, list->VisualRectForTesting(1));
  EXPECT_RECT_EQ(filter_bounds, list->VisualRectForTesting(2));
  EXPECT_RECT_EQ(filter_bounds, list->VisualRectForTesting(3));
}

// Verify that raster time optimizations for compositing item / draw single op /
// end compositing item can be collapsed together into a single draw op
// with the opacity from the compositing item folded in.
TEST(DisplayItemListTest, SaveDrawRestore) {
  auto list = make_scoped_refptr(new DisplayItemList);

  list->CreateAndAppendPairedBeginItem<CompositingDisplayItem>(
      80, SkBlendMode::kSrcOver, nullptr, nullptr, false);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      kVisualRect, CreateRectPictureWithAlpha(kVisualRect, 40));
  list->CreateAndAppendPairedEndItem<EndCompositingDisplayItem>();
  list->Finalize();

  SaveCountingCanvas canvas;
  list->Raster(&canvas);

  EXPECT_EQ(0, canvas.save_count_);
  EXPECT_EQ(0, canvas.restore_count_);
  EXPECT_EQ(gfx::RectToSkRect(kVisualRect), canvas.draw_rect_);

  float expected_alpha = 80 * 40 / 255.f;
  EXPECT_LE(std::abs(expected_alpha - canvas.paint_.getAlpha()), 1.f);
}

// Verify that compositing item / end compositing item is a noop.
// Here we're testing that Skia does an optimization that skips
// save/restore with nothing in between.  If skia stops doing this
// then we should reimplement this optimization in display list raster.
TEST(DisplayItemListTest, SaveRestoreNoops) {
  auto list = make_scoped_refptr(new DisplayItemList);

  list->CreateAndAppendPairedBeginItem<CompositingDisplayItem>(
      80, SkBlendMode::kSrcOver, nullptr, nullptr, false);
  list->CreateAndAppendPairedEndItem<EndCompositingDisplayItem>();
  list->CreateAndAppendPairedBeginItem<CompositingDisplayItem>(
      255, SkBlendMode::kSrcOver, nullptr, nullptr, false);
  list->CreateAndAppendPairedEndItem<EndCompositingDisplayItem>();
  list->CreateAndAppendPairedBeginItem<CompositingDisplayItem>(
      255, SkBlendMode::kSrc, nullptr, nullptr, false);
  list->CreateAndAppendPairedEndItem<EndCompositingDisplayItem>();
  list->Finalize();

  SaveCountingCanvas canvas;
  list->Raster(&canvas);

  EXPECT_EQ(0, canvas.save_count_);
  EXPECT_EQ(0, canvas.restore_count_);
}

// The same as SaveDrawRestore, but with save flags that prevent the
// optimization.
TEST(DisplayItemListTest, SaveDrawRestoreFail_BadSaveFlags) {
  auto list = make_scoped_refptr(new DisplayItemList);

  // Use a blend mode that's not compatible with the SaveDrawRestore
  // optimization.
  list->CreateAndAppendPairedBeginItem<CompositingDisplayItem>(
      80, SkBlendMode::kSrc, nullptr, nullptr, false);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(
      kVisualRect, CreateRectPictureWithAlpha(kVisualRect, 40));
  list->CreateAndAppendPairedEndItem<EndCompositingDisplayItem>();
  list->Finalize();

  SaveCountingCanvas canvas;
  list->Raster(&canvas);

  EXPECT_EQ(1, canvas.save_count_);
  EXPECT_EQ(1, canvas.restore_count_);
  EXPECT_EQ(gfx::RectToSkRect(kVisualRect), canvas.draw_rect_);
  EXPECT_LE(40, canvas.paint_.getAlpha());
}

// The same as SaveDrawRestore, but with too many ops in the PaintRecord.
TEST(DisplayItemListTest, SaveDrawRestoreFail_TooManyOps) {
  sk_sp<const PaintRecord> record;
  {
    PaintRecorder recorder;
    PaintCanvas* canvas =
        recorder.beginRecording(kVisualRect.width(), kVisualRect.height());
    PaintFlags flags;
    flags.setAlpha(40);
    canvas->drawRect(gfx::RectToSkRect(kVisualRect), flags);
    // Add an extra op here.
    canvas->drawRect(gfx::RectToSkRect(kVisualRect), flags);
    record = recorder.finishRecordingAsPicture();
  }
  EXPECT_GT(record->size(), 1u);

  auto list = make_scoped_refptr(new DisplayItemList);

  list->CreateAndAppendPairedBeginItem<CompositingDisplayItem>(
      80, SkBlendMode::kSrcOver, nullptr, nullptr, false);
  list->CreateAndAppendDrawingItem<DrawingDisplayItem>(kVisualRect,
                                                       std::move(record));
  list->CreateAndAppendPairedEndItem<EndCompositingDisplayItem>();
  list->Finalize();

  SaveCountingCanvas canvas;
  list->Raster(&canvas);

  EXPECT_EQ(1, canvas.save_count_);
  EXPECT_EQ(1, canvas.restore_count_);
  EXPECT_EQ(gfx::RectToSkRect(kVisualRect), canvas.draw_rect_);
  EXPECT_LE(40, canvas.paint_.getAlpha());
}

}  // namespace cc
