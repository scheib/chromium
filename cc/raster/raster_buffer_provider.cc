// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/raster/raster_buffer_provider.h"

#include <stddef.h>

#include "base/trace_event/trace_event.h"
#include "cc/raster/raster_source.h"
#include "cc/raster/texture_compressor.h"
#include "cc/resources/platform_color.h"
#include "cc/resources/resource_format_utils.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkMath.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "ui/gfx/geometry/axis_transform2d.h"

namespace cc {

RasterBufferProvider::RasterBufferProvider() {}

RasterBufferProvider::~RasterBufferProvider() {}

namespace {

// TODO(enne): http://crbug.com/721744.  Add CHECKs for conditions that would
// cause Skia to not create a surface here to diagnose what's going wrong.  This
// replicates SkSurfaceValidateRasterInfo and needs to be kept in sync with
// the corresponding Skia code.  This code should be removed as quickly as
// possible once a diagnosis is made.
void CheckValidRasterInfo(const SkImageInfo& info,
                          void* pixels,
                          size_t row_bytes) {
  CHECK(pixels);
  CHECK(!info.isEmpty());

  static const size_t kMaxTotalSize = SK_MaxS32;

  int shift = 0;
  switch (info.colorType()) {
    case kAlpha_8_SkColorType:
      CHECK(!info.colorSpace());
      shift = 0;
      break;
    case kRGB_565_SkColorType:
      CHECK(!info.colorSpace());
      shift = 1;
      break;
    case kN32_SkColorType:
      if (info.colorSpace())
        CHECK(info.colorSpace()->gammaCloseToSRGB());
      shift = 2;
      break;
    case kRGBA_F16_SkColorType:
      if (info.colorSpace())
        CHECK(info.colorSpace()->gammaIsLinear());
      shift = 3;
      break;
    default:
      CHECK(false) << "Unknown color type";
      break;
  }

  static constexpr size_t kIgnoreRowBytesValue = static_cast<size_t>(~0);
  if (kIgnoreRowBytesValue == row_bytes)
    return;

  uint64_t min_row_bytes = static_cast<uint64_t>(info.width()) << shift;
  CHECK_LE(min_row_bytes, row_bytes);

  size_t aligned_row_bytes = row_bytes >> shift << shift;
  CHECK_EQ(aligned_row_bytes, row_bytes);

  uint64_t size = sk_64_mul(info.height(), row_bytes);
  CHECK_LE(size, kMaxTotalSize);
}

bool IsSupportedPlaybackToMemoryFormat(ResourceFormat format) {
  switch (format) {
    case RGBA_4444:
    case RGBA_8888:
    case BGRA_8888:
    case ETC1:
      return true;
    case ALPHA_8:
    case LUMINANCE_8:
    case RGB_565:
    case RED_8:
    case LUMINANCE_F16:
    case RGBA_F16:
      return false;
  }
  NOTREACHED();
  return false;
}

}  // anonymous namespace

// static
void RasterBufferProvider::PlaybackToMemory(
    void* memory,
    ResourceFormat format,
    const gfx::Size& size,
    size_t stride,
    const RasterSource* raster_source,
    const gfx::Rect& canvas_bitmap_rect,
    const gfx::Rect& canvas_playback_rect,
    const gfx::AxisTransform2d& transform,
    const gfx::ColorSpace& target_color_space,
    const RasterSource::PlaybackSettings& playback_settings) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("cc.debug"),
               "RasterBufferProvider::PlaybackToMemory");

  DCHECK(IsSupportedPlaybackToMemoryFormat(format)) << format;

  // Uses kPremul_SkAlphaType since the result is not known to be opaque.
  SkImageInfo info =
      SkImageInfo::MakeN32(size.width(), size.height(), kPremul_SkAlphaType);

  // Use unknown pixel geometry to disable LCD text.
  SkSurfaceProps surface_props(0, kUnknown_SkPixelGeometry);
  if (raster_source->CanUseLCDText()) {
    // LegacyFontHost will get LCD text and skia figures out what type to use.
    surface_props = SkSurfaceProps(SkSurfaceProps::kLegacyFontHost_InitType);
  }

  if (!stride)
    stride = info.minRowBytes();
  DCHECK_GT(stride, 0u);

  switch (format) {
    case RGBA_8888:
    case BGRA_8888:
    case RGBA_F16: {
      CheckValidRasterInfo(info, memory, stride);
      sk_sp<SkSurface> surface =
          SkSurface::MakeRasterDirect(info, memory, stride, &surface_props);
      CHECK(surface);
      raster_source->PlaybackToCanvas(surface->getCanvas(), target_color_space,
                                      canvas_bitmap_rect, canvas_playback_rect,
                                      transform, playback_settings);
      return;
    }
    case RGBA_4444:
    case ETC1: {
      sk_sp<SkSurface> surface = SkSurface::MakeRaster(info, &surface_props);
      // TODO(reveman): Improve partial raster support by reducing the size of
      // playback rect passed to PlaybackToCanvas. crbug.com/519070
      raster_source->PlaybackToCanvas(surface->getCanvas(), target_color_space,
                                      canvas_bitmap_rect, canvas_bitmap_rect,
                                      transform, playback_settings);

      if (format == ETC1) {
        TRACE_EVENT0("cc",
                     "RasterBufferProvider::PlaybackToMemory::CompressETC1");
        DCHECK_EQ(size.width() % 4, 0);
        DCHECK_EQ(size.height() % 4, 0);
        std::unique_ptr<TextureCompressor> texture_compressor =
            TextureCompressor::Create(TextureCompressor::kFormatETC1);
        SkPixmap pixmap;
        surface->peekPixels(&pixmap);
        texture_compressor->Compress(
            reinterpret_cast<const uint8_t*>(pixmap.addr()),
            reinterpret_cast<uint8_t*>(memory), size.width(), size.height(),
            TextureCompressor::kQualityHigh);
      } else {
        TRACE_EVENT0("cc",
                     "RasterBufferProvider::PlaybackToMemory::ConvertRGBA4444");
        SkImageInfo dst_info =
            info.makeColorType(ResourceFormatToClosestSkColorType(format));
        bool rv = surface->readPixels(dst_info, memory, stride, 0, 0);
        DCHECK(rv);
      }
      return;
    }
    case ALPHA_8:
    case LUMINANCE_8:
    case RGB_565:
    case RED_8:
    case LUMINANCE_F16:
      NOTREACHED();
      return;
  }

  NOTREACHED();
}

bool RasterBufferProvider::ResourceFormatRequiresSwizzle(
    ResourceFormat format) {
  switch (format) {
    case RGBA_8888:
    case BGRA_8888:
      // Initialize resource using the preferred PlatformColor component
      // order and swizzle in the shader instead of in software.
      return !PlatformColor::SameComponentOrder(format);
    case RGBA_4444:
    case ETC1:
    case ALPHA_8:
    case LUMINANCE_8:
    case RGB_565:
    case RED_8:
    case LUMINANCE_F16:
    case RGBA_F16:
      return false;
  }
  NOTREACHED();
  return false;
}

}  // namespace cc
