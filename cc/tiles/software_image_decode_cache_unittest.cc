// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/tiles/software_image_decode_cache.h"

#include "cc/paint/draw_image.h"
#include "cc/resources/resource_format.h"
#include "cc/test/test_tile_task_runner.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkRefCnt.h"

namespace cc {
namespace {

gfx::ColorSpace DefaultColorSpace() {
  return gfx::ColorSpace::CreateSRGB();
}

size_t kLockedMemoryLimitBytes = 128 * 1024 * 1024;
class TestSoftwareImageDecodeCache : public SoftwareImageDecodeCache {
 public:
  TestSoftwareImageDecodeCache()
      : SoftwareImageDecodeCache(ResourceFormat::RGBA_8888,
                                 kLockedMemoryLimitBytes) {}
};

sk_sp<SkImage> CreateImageWithColorSpace(int width,
                                         int height,
                                         const gfx::ColorSpace& color_space) {
  SkBitmap bitmap;
  bitmap.allocPixels(
      SkImageInfo::MakeN32Premul(width, height, color_space.ToSkColorSpace()));
  return SkImage::MakeFromBitmap(bitmap);
}

sk_sp<SkImage> CreateImage(int width, int height) {
  return CreateImageWithColorSpace(width, height, DefaultColorSpace());
}

SkMatrix CreateMatrix(const SkSize& scale, bool is_decomposable) {
  SkMatrix matrix;
  matrix.setScale(scale.width(), scale.height());

  if (!is_decomposable) {
    // Perspective is not decomposable, add it.
    matrix[SkMatrix::kMPersp0] = 0.1f;
  }

  return matrix;
}

PaintImage::Id s_paint_image_id = PaintImage::GetNextId();

PaintImage CreatePaintImage(sk_sp<SkImage> image) {
  return PaintImage(s_paint_image_id, image);
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyNoneQuality) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  DrawImage draw_image(CreatePaintImage(image),
                       SkIRect::MakeWH(image->width(), image->height()),
                       kNone_SkFilterQuality,
                       CreateMatrix(SkSize::Make(0.5f, 1.5f), is_decomposable),
                       DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kNone_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  // Since the original decode will be used, the locked_bytes is that of the
  // original image.
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest,
     ImageKeyLowQualityIncreasedToMediumIfDownscale) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  DrawImage draw_image(CreatePaintImage(image),
                       SkIRect::MakeWH(image->width(), image->height()),
                       kLow_SkFilterQuality,
                       CreateMatrix(SkSize::Make(0.5f, 1.5f), is_decomposable),
                       DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kMedium_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_FALSE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyLowQualityKeptLowIfUpscale) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  DrawImage draw_image(CreatePaintImage(image),
                       SkIRect::MakeWH(image->width(), image->height()),
                       kLow_SkFilterQuality,
                       CreateMatrix(SkSize::Make(1.5f, 1.5f), is_decomposable),
                       DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyMediumQuality) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 1.5f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(quality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_FALSE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyMediumQualityDropToLowIfEnlarging) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.5f, 1.5f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyMediumQualityDropToLowIfIdentity) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.f, 1.f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest,
     ImageKeyMediumQualityDropToLowIfNearlyIdentity) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.001f, 1.001f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest,
     ImageKeyMediumQualityDropToLowIfNearlyIdentity2) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.999f, 0.999f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest,
     ImageKeyMediumQualityDropToLowIfNotDecomposable) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = false;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 1.5f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyMediumQualityAt1_5Scale) {
  sk_sp<SkImage> image = CreateImage(500, 200);
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.5f, 1.5f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(500, key.target_size().width());
  EXPECT_EQ(200, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(500u * 200u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyMediumQualityAt1_0cale) {
  sk_sp<SkImage> image = CreateImage(500, 200);
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.f, 1.f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(500, key.target_size().width());
  EXPECT_EQ(200, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(500u * 200u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyMediumQualityAt0_75Scale) {
  sk_sp<SkImage> image = CreateImage(500, 200);
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.75f, 0.75f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(quality, key.filter_quality());
  EXPECT_EQ(500, key.target_size().width());
  EXPECT_EQ(200, key.target_size().height());
  EXPECT_FALSE(key.can_use_original_size_decode());
  EXPECT_EQ(500u * 200u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyMediumQualityAt0_5Scale) {
  sk_sp<SkImage> image = CreateImage(500, 200);
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(quality, key.filter_quality());
  EXPECT_EQ(250, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_FALSE(key.can_use_original_size_decode());
  EXPECT_EQ(250u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyMediumQualityAt0_49Scale) {
  sk_sp<SkImage> image = CreateImage(500, 200);
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.49f, 0.49f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(quality, key.filter_quality());
  EXPECT_EQ(250, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_FALSE(key.can_use_original_size_decode());
  EXPECT_EQ(250u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyMediumQualityAt0_1Scale) {
  sk_sp<SkImage> image = CreateImage(500, 200);
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.1f, 0.1f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(quality, key.filter_quality());
  EXPECT_EQ(62, key.target_size().width());
  EXPECT_EQ(25, key.target_size().height());
  EXPECT_FALSE(key.can_use_original_size_decode());
  EXPECT_EQ(62u * 25u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyMediumQualityAt0_01Scale) {
  sk_sp<SkImage> image = CreateImage(500, 200);
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.01f, 0.01f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(quality, key.filter_quality());
  EXPECT_EQ(7, key.target_size().width());
  EXPECT_EQ(3, key.target_size().height());
  EXPECT_FALSE(key.can_use_original_size_decode());
  EXPECT_EQ(7u * 3u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest,
     ImageKeyPartialDowscalesDropsHighQualityToMedium) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 1.5f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kMedium_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_FALSE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest,
     ImageKeyFullDowscalesDropsHighQualityToMedium) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.2f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kMedium_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(50, key.target_size().width());
  EXPECT_EQ(50, key.target_size().height());
  EXPECT_FALSE(key.can_use_original_size_decode());
  EXPECT_EQ(50u * 50u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyDowscalesHighQuality) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(2.5f, 1.5f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(quality, key.filter_quality());
  EXPECT_EQ(250, key.target_size().width());
  EXPECT_EQ(150, key.target_size().height());
  EXPECT_FALSE(key.can_use_original_size_decode());
  EXPECT_EQ(250u * 150u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyHighQualityDropToMediumIfTooLarge) {
  // Just over 64MB when scaled.
  sk_sp<SkImage> image = CreateImage(4555, 2048);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  // At least one dimension should scale down, so that medium quality doesn't
  // become low.
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.9f, 2.f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kMedium_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(4555, key.target_size().width());
  EXPECT_EQ(2048, key.target_size().height());
  EXPECT_FALSE(key.can_use_original_size_decode());
  EXPECT_EQ(4555u * 2048u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest,
     ImageKeyHighQualityDropToLowIfNotDecomposable) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = false;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 1.5f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageKeyHighQualityDropToLowIfIdentity) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.f, 1.f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest,
     ImageKeyHighQualityDropToLowIfNearlyIdentity) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.001f, 1.001f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest,
     ImageKeyHighQualityDropToLowIfNearlyIdentity2) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.999f, 0.999f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, OriginalDecodesAreEqual) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kNone_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kNone_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_TRUE(key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());

  DrawImage another_draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.5f, 1.5), is_decomposable),
      DefaultColorSpace());

  auto another_key = ImageDecodeCacheKey::FromDrawImage(another_draw_image);
  EXPECT_EQ(image->uniqueID(), another_key.image_id());
  EXPECT_EQ(kNone_SkFilterQuality, another_key.filter_quality());
  EXPECT_EQ(100, another_key.target_size().width());
  EXPECT_EQ(100, another_key.target_size().height());
  EXPECT_TRUE(another_key.can_use_original_size_decode());
  EXPECT_EQ(100u * 100u * 4u, another_key.locked_bytes());

  EXPECT_TRUE(key == another_key);
}

TEST(SoftwareImageDecodeCacheTest, ImageRectDoesNotContainSrcRect) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image),
      SkIRect::MakeXYWH(25, 35, image->width(), image->height()), quality,
      CreateMatrix(SkSize::Make(1.f, 1.f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kLow_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(100, key.target_size().width());
  EXPECT_EQ(100, key.target_size().height());
  EXPECT_EQ(gfx::Rect(25, 35, 75, 65), key.src_rect());
  EXPECT_EQ(100u * 100u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, ImageRectDoesNotContainSrcRectWithScale) {
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image),
      SkIRect::MakeXYWH(20, 30, image->width(), image->height()), quality,
      CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());

  auto key = ImageDecodeCacheKey::FromDrawImage(draw_image);
  EXPECT_EQ(image->uniqueID(), key.image_id());
  EXPECT_EQ(kMedium_SkFilterQuality, key.filter_quality());
  EXPECT_EQ(40, key.target_size().width());
  EXPECT_EQ(35, key.target_size().height());
  EXPECT_EQ(gfx::Rect(20, 30, 80, 70), key.src_rect());
  EXPECT_EQ(40u * 35u * 4u, key.locked_bytes());
}

TEST(SoftwareImageDecodeCacheTest, GetTaskForImageSameImage) {
  TestSoftwareImageDecodeCache cache;
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  DrawImage another_draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      another_draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task.get() == another_task.get());

  TestTileTaskRunner::ProcessTask(task.get());

  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, GetTaskForImageSameImageDifferentQuality) {
  TestSoftwareImageDecodeCache cache;
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;

  DrawImage high_quality_draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      kHigh_SkFilterQuality,
      CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> high_quality_task;
  bool need_unref = cache.GetTaskForImageAndRef(high_quality_draw_image,
                                                ImageDecodeCache::TracingInfo(),
                                                &high_quality_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(high_quality_task);

  DrawImage none_quality_draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      kNone_SkFilterQuality,
      CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> none_quality_task;
  need_unref = cache.GetTaskForImageAndRef(none_quality_draw_image,
                                           ImageDecodeCache::TracingInfo(),
                                           &none_quality_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(none_quality_task);
  EXPECT_TRUE(high_quality_task.get() != none_quality_task.get());

  TestTileTaskRunner::ProcessTask(high_quality_task.get());
  TestTileTaskRunner::ProcessTask(none_quality_task.get());

  cache.UnrefImage(high_quality_draw_image);
  cache.UnrefImage(none_quality_draw_image);
}

TEST(SoftwareImageDecodeCacheTest, GetTaskForImageSameImageDifferentSize) {
  TestSoftwareImageDecodeCache cache;
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage half_size_draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> half_size_task;
  bool need_unref = cache.GetTaskForImageAndRef(
      half_size_draw_image, ImageDecodeCache::TracingInfo(), &half_size_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(half_size_task);

  DrawImage quarter_size_draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.25f, 0.25f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> quarter_size_task;
  need_unref = cache.GetTaskForImageAndRef(quarter_size_draw_image,
                                           ImageDecodeCache::TracingInfo(),
                                           &quarter_size_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(quarter_size_task);
  EXPECT_TRUE(half_size_task.get() != quarter_size_task.get());

  TestTileTaskRunner::ProcessTask(half_size_task.get());
  TestTileTaskRunner::ProcessTask(quarter_size_task.get());

  cache.UnrefImage(half_size_draw_image);
  cache.UnrefImage(quarter_size_draw_image);
}

TEST(SoftwareImageDecodeCacheTest, GetTaskForImageDifferentImage) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> first_image = CreateImage(100, 100);
  DrawImage first_draw_image(
      CreatePaintImage(first_image),
      SkIRect::MakeWH(first_image->width(), first_image->height()), quality,
      CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> first_task;
  bool need_unref = cache.GetTaskForImageAndRef(
      first_draw_image, ImageDecodeCache::TracingInfo(), &first_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(first_task);

  sk_sp<SkImage> second_image = CreateImage(100, 100);
  DrawImage second_draw_image(
      CreatePaintImage(second_image),
      SkIRect::MakeWH(second_image->width(), second_image->height()), quality,
      CreateMatrix(SkSize::Make(0.25f, 0.25f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> second_task;
  need_unref = cache.GetTaskForImageAndRef(
      second_draw_image, ImageDecodeCache::TracingInfo(), &second_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(second_task);
  EXPECT_TRUE(first_task.get() != second_task.get());

  TestTileTaskRunner::ProcessTask(first_task.get());
  TestTileTaskRunner::ProcessTask(second_task.get());

  cache.UnrefImage(first_draw_image);
  cache.UnrefImage(second_draw_image);
}

// crbug.com/709341
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetTaskForImageDifferentColorSpace \
  DISABLED_GetTaskForImageDifferentColorSpace
#else
#define MAYBE_GetTaskForImageDifferentColorSpace \
  GetTaskForImageDifferentColorSpace
#endif
TEST(SoftwareImageDecodeCacheTest, MAYBE_GetTaskForImageDifferentColorSpace) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  gfx::ColorSpace color_space_a(gfx::ColorSpace::PrimaryID::XYZ_D50,
                                gfx::ColorSpace::TransferID::IEC61966_2_1);
  gfx::ColorSpace color_space_b(gfx::ColorSpace::PrimaryID::SMPTE170M,
                                gfx::ColorSpace::TransferID::IEC61966_2_1);
  gfx::ColorSpace color_space_c = gfx::ColorSpace::CreateSRGB();

  sk_sp<SkImage> image = CreateImageWithColorSpace(100, 100, color_space_a);
  DrawImage first_draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
      color_space_b);
  scoped_refptr<TileTask> first_task;
  bool need_unref = cache.GetTaskForImageAndRef(
      first_draw_image, ImageDecodeCache::TracingInfo(), &first_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(first_task);

  DrawImage second_draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
      color_space_c);
  scoped_refptr<TileTask> second_task;
  need_unref = cache.GetTaskForImageAndRef(
      second_draw_image, ImageDecodeCache::TracingInfo(), &second_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(second_task);
  EXPECT_TRUE(first_task.get() != second_task.get());

  DrawImage third_draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
      color_space_b);
  scoped_refptr<TileTask> third_task;
  need_unref = cache.GetTaskForImageAndRef(
      third_draw_image, ImageDecodeCache::TracingInfo(), &third_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(third_task);
  EXPECT_TRUE(first_task.get() == third_task.get());

  TestTileTaskRunner::ProcessTask(first_task.get());
  TestTileTaskRunner::ProcessTask(second_task.get());

  cache.UnrefImage(first_draw_image);
  cache.UnrefImage(second_draw_image);
  cache.UnrefImage(third_draw_image);
}

TEST(SoftwareImageDecodeCacheTest, GetTaskForImageAlreadyDecoded) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ScheduleTask(task.get());
  TestTileTaskRunner::RunTask(task.get());

  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_FALSE(another_task);

  TestTileTaskRunner::CompleteTask(task.get());

  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, GetTaskForImageAlreadyPrerolled) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kLow_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.f, 1.f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ScheduleTask(task.get());
  TestTileTaskRunner::RunTask(task.get());

  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_FALSE(another_task);

  TestTileTaskRunner::CompleteTask(task.get());

  scoped_refptr<TileTask> third_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &third_task);
  EXPECT_TRUE(need_unref);
  EXPECT_FALSE(third_task);

  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, GetTaskForImageCanceledGetsNewTask) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(another_task.get() == task.get());

  // Didn't run the task, complete it (it was canceled).
  TestTileTaskRunner::CancelTask(task.get());
  TestTileTaskRunner::CompleteTask(task.get());

  // Fully cancel everything (so the raster would unref things).
  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);

  // Here a new task is created.
  scoped_refptr<TileTask> third_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &third_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(third_task);
  EXPECT_FALSE(third_task.get() == task.get());

  TestTileTaskRunner::ProcessTask(third_task.get());

  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest,
     GetTaskForImageCanceledWhileReffedGetsNewTask) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(another_task.get() == task.get());

  // Didn't run the task, complete it (it was canceled).
  TestTileTaskRunner::CancelTask(task.get());
  TestTileTaskRunner::CompleteTask(task.get());

  // Note that here, everything is reffed, but a new task is created. This is
  // possible with repeated schedule/cancel operations.
  scoped_refptr<TileTask> third_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &third_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(third_task);
  EXPECT_FALSE(third_task.get() == task.get());

  TestTileTaskRunner::ProcessTask(third_task.get());

  // 3 Unrefs!
  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, GetDecodedImageForDraw) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_EQ(50, decoded_draw_image.image()->width());
  EXPECT_EQ(50, decoded_draw_image.image()->height());
  EXPECT_FLOAT_EQ(0.5f, decoded_draw_image.scale_adjustment().width());
  EXPECT_FLOAT_EQ(0.5f, decoded_draw_image.scale_adjustment().height());
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_FALSE(decoded_draw_image.is_scale_adjustment_identity());
  EXPECT_FALSE(decoded_draw_image.is_at_raster_decode());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest,
     GetDecodedImageForDrawWithNonContainedSrcRect) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image),
      SkIRect::MakeXYWH(20, 30, image->width(), image->height()), quality,
      CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_EQ(40, decoded_draw_image.image()->width());
  EXPECT_EQ(35, decoded_draw_image.image()->height());
  EXPECT_FLOAT_EQ(0.5f, decoded_draw_image.scale_adjustment().width());
  EXPECT_FLOAT_EQ(0.5f, decoded_draw_image.scale_adjustment().height());
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_FALSE(decoded_draw_image.is_scale_adjustment_identity());
  EXPECT_FALSE(decoded_draw_image.is_at_raster_decode());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, GetDecodedImageForDrawAtRasterDecode) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_EQ(50, decoded_draw_image.image()->width());
  EXPECT_EQ(50, decoded_draw_image.image()->height());
  EXPECT_FLOAT_EQ(0.5f, decoded_draw_image.scale_adjustment().width());
  EXPECT_FLOAT_EQ(0.5f, decoded_draw_image.scale_adjustment().height());
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_FALSE(decoded_draw_image.is_scale_adjustment_identity());
  EXPECT_TRUE(decoded_draw_image.is_at_raster_decode());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
}

TEST(SoftwareImageDecodeCacheTest,
     GetDecodedImageForDrawAtRasterDecodeMultipleTimes) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_EQ(50, decoded_draw_image.image()->width());
  EXPECT_EQ(50, decoded_draw_image.image()->height());
  EXPECT_FLOAT_EQ(0.5f, decoded_draw_image.scale_adjustment().width());
  EXPECT_FLOAT_EQ(0.5f, decoded_draw_image.scale_adjustment().height());
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_FALSE(decoded_draw_image.is_scale_adjustment_identity());
  EXPECT_TRUE(decoded_draw_image.is_at_raster_decode());

  DecodedDrawImage another_decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_EQ(decoded_draw_image.image()->uniqueID(),
            another_decoded_draw_image.image()->uniqueID());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.DrawWithImageFinished(draw_image, another_decoded_draw_image);
}

TEST(SoftwareImageDecodeCacheTest,
     GetDecodedImageForDrawAtRasterDecodeDoesNotPreventTasks) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_EQ(50, decoded_draw_image.image()->width());
  EXPECT_EQ(50, decoded_draw_image.image()->height());
  EXPECT_FLOAT_EQ(0.5f, decoded_draw_image.scale_adjustment().width());
  EXPECT_FLOAT_EQ(0.5f, decoded_draw_image.scale_adjustment().height());
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_FALSE(decoded_draw_image.is_scale_adjustment_identity());
  EXPECT_TRUE(decoded_draw_image.is_at_raster_decode());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage another_decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  // This should get the new decoded/locked image, not the one we're using at
  // raster.
  // TODO(vmpstr): We can possibly optimize this so that the decode simply moves
  // the image to the right spot.
  EXPECT_NE(decoded_draw_image.image()->uniqueID(),
            another_decoded_draw_image.image()->uniqueID());
  EXPECT_FALSE(another_decoded_draw_image.is_at_raster_decode());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.DrawWithImageFinished(draw_image, another_decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest,
     GetDecodedImageForDrawAtRasterDecodeIsUsedForLockedCache) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_EQ(50, decoded_draw_image.image()->width());
  EXPECT_EQ(50, decoded_draw_image.image()->height());
  EXPECT_FLOAT_EQ(0.5f, decoded_draw_image.scale_adjustment().width());
  EXPECT_FLOAT_EQ(0.5f, decoded_draw_image.scale_adjustment().height());
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_FALSE(decoded_draw_image.is_scale_adjustment_identity());
  EXPECT_TRUE(decoded_draw_image.is_at_raster_decode());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  // If we finish the draw here, then we will use it for the locked decode
  // instead of decoding again.
  cache.DrawWithImageFinished(draw_image, decoded_draw_image);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage another_decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  // This should get the decoded/locked image which we originally decoded at
  // raster time, since it's now in the locked cache.
  EXPECT_EQ(decoded_draw_image.image()->uniqueID(),
            another_decoded_draw_image.image()->uniqueID());
  EXPECT_FALSE(another_decoded_draw_image.is_at_raster_decode());

  cache.DrawWithImageFinished(draw_image, another_decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, ZeroSizedImagesAreSkipped) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.f, 0.f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_FALSE(task);
  EXPECT_FALSE(need_unref);

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_FALSE(decoded_draw_image.image());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
}

TEST(SoftwareImageDecodeCacheTest, NonOverlappingSrcRectImagesAreSkipped) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image),
      SkIRect::MakeXYWH(150, 150, image->width(), image->height()), quality,
      CreateMatrix(SkSize::Make(1.f, 1.f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_FALSE(task);
  EXPECT_FALSE(need_unref);

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_FALSE(decoded_draw_image.image());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
}

TEST(SoftwareImageDecodeCacheTest, LowQualityFilterIsHandled) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kLow_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.f, 1.f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(task);
  EXPECT_TRUE(need_unref);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  // If we decoded the image and cached it, it would be stored in a different
  // SkImage object.
  EXPECT_TRUE(decoded_draw_image.image() != image);

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, LowQualityScaledSubrectIsHandled) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kLow_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(CreatePaintImage(image),
                       SkIRect::MakeXYWH(10, 10, 80, 80), quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(task);
  EXPECT_TRUE(need_unref);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  // If we decoded the image and cached it, it would be stored in a different
  // SkImage object.
  EXPECT_TRUE(decoded_draw_image.image() != image);
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  // Low quality will be upgraded to medium and mip-mapped.
  EXPECT_FALSE(decoded_draw_image.is_scale_adjustment_identity());
  EXPECT_EQ(0.5f, decoded_draw_image.scale_adjustment().width());
  EXPECT_EQ(0.5f, decoded_draw_image.scale_adjustment().height());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, NoneQualityScaledSubrectIsHandled) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kNone_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(CreatePaintImage(image),
                       SkIRect::MakeXYWH(10, 10, 80, 80), quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(task);
  EXPECT_TRUE(need_unref);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  // If we decoded the image and cached it, it would be stored in a different
  // SkImage object.
  EXPECT_TRUE(decoded_draw_image.image() != image);
  EXPECT_EQ(kNone_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_TRUE(decoded_draw_image.is_scale_adjustment_identity());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, MediumQualityAt01_5ScaleIsHandled) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(500, 200);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.5f, 1.5f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(task);
  EXPECT_TRUE(need_unref);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  // If we decoded the image and cached it, it would be stored in a different
  // SkImageObject.
  EXPECT_TRUE(decoded_draw_image.image() != image);
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_EQ(500, decoded_draw_image.image()->width());
  EXPECT_EQ(200, decoded_draw_image.image()->height());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, MediumQualityAt1_0ScaleIsHandled) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(500, 200);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.f, 1.f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(task);
  EXPECT_TRUE(need_unref);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  // If we decoded the image and cached it, it would be stored in a different
  // SkImageObject.
  EXPECT_TRUE(decoded_draw_image.image() != image);
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_EQ(500, decoded_draw_image.image()->width());
  EXPECT_EQ(200, decoded_draw_image.image()->height());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, MediumQualityAt0_75ScaleIsHandled) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(500, 200);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.75f, 0.75f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(task);
  EXPECT_TRUE(need_unref);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  // If we decoded the image and cached it, it would be stored in a different
  // SkImageObject.
  EXPECT_TRUE(decoded_draw_image.image() != image);
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_EQ(500, decoded_draw_image.image()->width());
  EXPECT_EQ(200, decoded_draw_image.image()->height());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, MediumQualityAt0_5ScaleIsHandled) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(500, 200);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(task);
  EXPECT_TRUE(need_unref);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  // If we decoded the image and cached it, it would be stored in a different
  // SkImageObject.
  EXPECT_TRUE(decoded_draw_image.image() != image);
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_EQ(250, decoded_draw_image.image()->width());
  EXPECT_EQ(100, decoded_draw_image.image()->height());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, MediumQualityAt0_49ScaleIsHandled) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(500, 200);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.49f, 0.49f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(task);
  EXPECT_TRUE(need_unref);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  // If we decoded the image and cached it, it would be stored in a different
  // SkImageObject.
  EXPECT_TRUE(decoded_draw_image.image() != image);
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_EQ(250, decoded_draw_image.image()->width());
  EXPECT_EQ(100, decoded_draw_image.image()->height());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, MediumQualityAt0_1ScaleIsHandled) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(500, 200);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.1f, 0.1f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(task);
  EXPECT_TRUE(need_unref);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  // If we decoded the image and cached it, it would be stored in a different
  // SkImageObject.
  EXPECT_TRUE(decoded_draw_image.image() != image);
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_EQ(62, decoded_draw_image.image()->width());
  EXPECT_EQ(25, decoded_draw_image.image()->height());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, MediumQualityAt0_01ScaleIsHandled) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(500, 200);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.01f, 0.01f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(task);
  EXPECT_TRUE(need_unref);

  TestTileTaskRunner::ProcessTask(task.get());

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  // If we decoded the image and cached it, it would be stored in a different
  // SkImageObject.
  EXPECT_TRUE(decoded_draw_image.image() != image);
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image.filter_quality());
  EXPECT_EQ(7, decoded_draw_image.image()->width());
  EXPECT_EQ(3, decoded_draw_image.image()->height());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(SoftwareImageDecodeCacheTest, MediumQualityAt0_001ScaleIsHandled) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(500, 200);
  DrawImage draw_image(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.001f, 0.001f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_FALSE(task);
  EXPECT_FALSE(need_unref);

  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_FALSE(decoded_draw_image.image());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
}

TEST(SoftwareImageDecodeCacheTest,
     MediumQualityImagesAreTheSameAt0_5And0_49Scale) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kMedium_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(500, 200);
  DrawImage draw_image_50(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  DrawImage draw_image_49(
      CreatePaintImage(image), SkIRect::MakeWH(image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(0.49f, 0.49f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task_50;
  bool need_unref_50 = cache.GetTaskForImageAndRef(
      draw_image_50, ImageDecodeCache::TracingInfo(), &task_50);
  EXPECT_TRUE(task_50);
  EXPECT_TRUE(need_unref_50);
  scoped_refptr<TileTask> task_49;
  bool need_unref_49 = cache.GetTaskForImageAndRef(
      draw_image_49, ImageDecodeCache::TracingInfo(), &task_49);
  EXPECT_TRUE(task_49);
  EXPECT_TRUE(need_unref_49);

  TestTileTaskRunner::ProcessTask(task_49.get());

  DecodedDrawImage decoded_draw_image_50 =
      cache.GetDecodedImageForDraw(draw_image_50);
  EXPECT_TRUE(decoded_draw_image_50.image());
  DecodedDrawImage decoded_draw_image_49 =
      cache.GetDecodedImageForDraw(draw_image_49);
  EXPECT_TRUE(decoded_draw_image_49.image());
  // If we decoded the image and cached it, it would be stored in a different
  // SkImageObject.
  EXPECT_TRUE(decoded_draw_image_50.image() != image);
  EXPECT_TRUE(decoded_draw_image_49.image() != image);
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image_50.filter_quality());
  EXPECT_EQ(kLow_SkFilterQuality, decoded_draw_image_49.filter_quality());
  EXPECT_EQ(250, decoded_draw_image_50.image()->width());
  EXPECT_EQ(250, decoded_draw_image_49.image()->width());
  EXPECT_EQ(100, decoded_draw_image_50.image()->height());
  EXPECT_EQ(100, decoded_draw_image_49.image()->height());

  EXPECT_EQ(decoded_draw_image_50.image(), decoded_draw_image_49.image());

  cache.DrawWithImageFinished(draw_image_50, decoded_draw_image_50);
  cache.UnrefImage(draw_image_50);
  cache.DrawWithImageFinished(draw_image_49, decoded_draw_image_49);
  cache.UnrefImage(draw_image_49);
}

TEST(SoftwareImageDecodeCacheTest, ClearCache) {
  TestSoftwareImageDecodeCache cache;
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  for (int i = 0; i < 10; ++i) {
    sk_sp<SkImage> image = CreateImage(100, 100);
    DrawImage draw_image(
        CreatePaintImage(image),
        SkIRect::MakeWH(image->width(), image->height()), quality,
        CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
        DefaultColorSpace());
    scoped_refptr<TileTask> task;
    bool need_unref = cache.GetTaskForImageAndRef(
        draw_image, ImageDecodeCache::TracingInfo(), &task);
    EXPECT_TRUE(need_unref);
    EXPECT_TRUE(task);
    TestTileTaskRunner::ProcessTask(task.get());
    cache.UnrefImage(draw_image);
  }

  EXPECT_EQ(10u, cache.GetNumCacheEntriesForTesting());

  // Tell our cache to clear resources.
  cache.ClearCache();

  EXPECT_EQ(0u, cache.GetNumCacheEntriesForTesting());
}

}  // namespace
}  // namespace cc
