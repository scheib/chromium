// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/frame/ImageBitmap.h"

#include <memory>
#include "core/html/HTMLCanvasElement.h"
#include "core/html/HTMLVideoElement.h"
#include "core/html/ImageData.h"
#include "core/offscreencanvas/OffscreenCanvas.h"
#include "platform/graphics/CanvasColorParams.h"
#include "platform/graphics/skia/SkiaUtils.h"
#include "platform/image-decoders/ImageDecoder.h"
#include "platform/wtf/CheckedNumeric.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/RefPtr.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace blink {

constexpr const char* kImageOrientationFlipY = "flipY";
constexpr const char* kImageBitmapOptionNone = "none";
constexpr const char* kImageBitmapOptionDefault = "default";
constexpr const char* kImageBitmapOptionPremultiply = "premultiply";
constexpr const char* kImageBitmapOptionResizeQualityHigh = "high";
constexpr const char* kImageBitmapOptionResizeQualityMedium = "medium";
constexpr const char* kImageBitmapOptionResizeQualityPixelated = "pixelated";
constexpr const char* kSRGBImageBitmapColorSpaceConversion = "srgb";
constexpr const char* kLinearRGBImageBitmapColorSpaceConversion = "linear-rgb";
constexpr const char* kP3ImageBitmapColorSpaceConversion = "p3";
constexpr const char* kRec2020ImageBitmapColorSpaceConversion = "rec2020";

namespace {

struct ParsedOptions {
  bool flip_y = false;
  bool premultiply_alpha = true;
  bool should_scale_input = false;
  unsigned resize_width = 0;
  unsigned resize_height = 0;
  IntRect crop_rect;
  SkFilterQuality resize_quality = kLow_SkFilterQuality;
  CanvasColorParams color_params;
  bool color_correct_rendering_enabled = false;
  bool use_global_target_color_space = false;
};

ParsedOptions DefaultOptions() {
  return ParsedOptions();
}

// The following two functions are helpers used in cropImage
static inline IntRect NormalizeRect(const IntRect& rect) {
  return IntRect(std::min(rect.X(), rect.MaxX()),
                 std::min(rect.Y(), rect.MaxY()),
                 std::max(rect.Width(), -rect.Width()),
                 std::max(rect.Height(), -rect.Height()));
}

ParsedOptions ParseOptions(const ImageBitmapOptions& options,
                           Optional<IntRect> crop_rect,
                           IntSize source_size) {
  ParsedOptions parsed_options;
  if (options.imageOrientation() == kImageOrientationFlipY) {
    parsed_options.flip_y = true;
  } else {
    parsed_options.flip_y = false;
    DCHECK(options.imageOrientation() == kImageBitmapOptionNone);
  }
  if (options.premultiplyAlpha() == kImageBitmapOptionNone) {
    parsed_options.premultiply_alpha = false;
  } else {
    parsed_options.premultiply_alpha = true;
    DCHECK(options.premultiplyAlpha() == kImageBitmapOptionDefault ||
           options.premultiplyAlpha() == kImageBitmapOptionPremultiply);
  }

  if (options.colorSpaceConversion() != kImageBitmapOptionNone) {
    parsed_options.color_correct_rendering_enabled =
        RuntimeEnabledFeatures::experimentalCanvasFeaturesEnabled() &&
        RuntimeEnabledFeatures::colorCorrectRenderingEnabled();
    if (!parsed_options.color_correct_rendering_enabled) {
      DCHECK_EQ(options.colorSpaceConversion(), kImageBitmapOptionDefault);
      if (RuntimeEnabledFeatures::colorCorrectRenderingDefaultModeEnabled()) {
        parsed_options.use_global_target_color_space = true;
      }
    } else {
      if (options.colorSpaceConversion() == kImageBitmapOptionDefault ||
          options.colorSpaceConversion() ==
              kSRGBImageBitmapColorSpaceConversion) {
        parsed_options.color_params.SetCanvasColorSpace(kSRGBCanvasColorSpace);
      } else if (options.colorSpaceConversion() ==
                 kLinearRGBImageBitmapColorSpaceConversion) {
        parsed_options.color_params.SetCanvasColorSpace(kSRGBCanvasColorSpace);
        parsed_options.color_params.SetCanvasPixelFormat(kF16CanvasPixelFormat);
      } else if (options.colorSpaceConversion() ==
                 kP3ImageBitmapColorSpaceConversion) {
        parsed_options.color_params.SetCanvasColorSpace(kP3CanvasColorSpace);
        parsed_options.color_params.SetCanvasPixelFormat(kF16CanvasPixelFormat);
      } else if (options.colorSpaceConversion() ==
                 kRec2020ImageBitmapColorSpaceConversion) {
        parsed_options.color_params.SetCanvasColorSpace(
            kRec2020CanvasColorSpace);
        parsed_options.color_params.SetCanvasPixelFormat(kF16CanvasPixelFormat);
      } else {
        NOTREACHED()
            << "Invalid ImageBitmap creation attribute colorSpaceConversion: "
            << options.colorSpaceConversion();
      }
    }
  }

  int source_width = source_size.Width();
  int source_height = source_size.Height();
  if (!crop_rect) {
    parsed_options.crop_rect = IntRect(0, 0, source_width, source_height);
  } else {
    parsed_options.crop_rect = NormalizeRect(*crop_rect);
  }
  if (!options.hasResizeWidth() && !options.hasResizeHeight()) {
    parsed_options.resize_width = parsed_options.crop_rect.Width();
    parsed_options.resize_height = parsed_options.crop_rect.Height();
  } else if (options.hasResizeWidth() && options.hasResizeHeight()) {
    parsed_options.resize_width = options.resizeWidth();
    parsed_options.resize_height = options.resizeHeight();
  } else if (options.hasResizeWidth() && !options.hasResizeHeight()) {
    parsed_options.resize_width = options.resizeWidth();
    parsed_options.resize_height = ceil(
        static_cast<float>(options.resizeWidth()) /
        parsed_options.crop_rect.Width() * parsed_options.crop_rect.Height());
  } else {
    parsed_options.resize_height = options.resizeHeight();
    parsed_options.resize_width = ceil(
        static_cast<float>(options.resizeHeight()) /
        parsed_options.crop_rect.Height() * parsed_options.crop_rect.Width());
  }
  if (static_cast<int>(parsed_options.resize_width) ==
          parsed_options.crop_rect.Width() &&
      static_cast<int>(parsed_options.resize_height) ==
          parsed_options.crop_rect.Height()) {
    parsed_options.should_scale_input = false;
    return parsed_options;
  }
  parsed_options.should_scale_input = true;

  if (options.resizeQuality() == kImageBitmapOptionResizeQualityHigh)
    parsed_options.resize_quality = kHigh_SkFilterQuality;
  else if (options.resizeQuality() == kImageBitmapOptionResizeQualityMedium)
    parsed_options.resize_quality = kMedium_SkFilterQuality;
  else if (options.resizeQuality() == kImageBitmapOptionResizeQualityPixelated)
    parsed_options.resize_quality = kNone_SkFilterQuality;
  else
    parsed_options.resize_quality = kLow_SkFilterQuality;
  return parsed_options;
}

bool DstBufferSizeHasOverflow(const ParsedOptions& options) {
  CheckedNumeric<unsigned> total_bytes = options.crop_rect.Width();
  total_bytes *= options.crop_rect.Height();
  total_bytes *=
      SkColorTypeBytesPerPixel(options.color_params.GetSkColorType());
  if (!total_bytes.IsValid())
    return true;

  if (!options.should_scale_input)
    return false;
  total_bytes = options.resize_width;
  total_bytes *= options.resize_height;
  total_bytes *=
      SkColorTypeBytesPerPixel(options.color_params.GetSkColorType());
  if (!total_bytes.IsValid())
    return true;

  return false;
}

}  // namespace

static PassRefPtr<Uint8Array> CopySkImageData(SkImage* input,
                                              const SkImageInfo& info) {
  // The function dstBufferSizeHasOverflow() is being called at the beginning of
  // each ImageBitmap() constructor, which makes sure that doing
  // width * height * bytesPerPixel will never overflow unsigned.
  unsigned width = static_cast<unsigned>(input->width());
  RefPtr<ArrayBuffer> dst_buffer =
      ArrayBuffer::CreateOrNull(width * input->height(), info.bytesPerPixel());
  if (!dst_buffer)
    return nullptr;
  RefPtr<Uint8Array> dst_pixels =
      Uint8Array::Create(dst_buffer, 0, dst_buffer->ByteLength());
  input->readPixels(info, dst_pixels->Data(), width * info.bytesPerPixel(), 0,
                    0);
  return dst_pixels;
}

static sk_sp<SkImage> NewSkImageFromRaster(const SkImageInfo& info,
                                           PassRefPtr<Uint8Array> image_pixels,
                                           unsigned image_row_bytes) {
  SkPixmap pixmap(info, image_pixels->Data(), image_row_bytes);
  return SkImage::MakeFromRaster(pixmap,
                                 [](const void*, void* pixels) {
                                   static_cast<Uint8Array*>(pixels)->Deref();
                                 },
                                 image_pixels.LeakRef());
}

static void SwizzleImageData(unsigned char* src_addr,
                             unsigned height,
                             unsigned bytes_per_row,
                             bool flip_y) {
  if (flip_y) {
    for (unsigned i = 0; i < height / 2; i++) {
      unsigned top_row_start_position = i * bytes_per_row;
      unsigned bottom_row_start_position = (height - 1 - i) * bytes_per_row;
      if (kN32_SkColorType == kBGRA_8888_SkColorType) {  // needs to swizzle
        for (unsigned j = 0; j < bytes_per_row; j += 4) {
          std::swap(src_addr[top_row_start_position + j],
                    src_addr[bottom_row_start_position + j + 2]);
          std::swap(src_addr[top_row_start_position + j + 1],
                    src_addr[bottom_row_start_position + j + 1]);
          std::swap(src_addr[top_row_start_position + j + 2],
                    src_addr[bottom_row_start_position + j]);
          std::swap(src_addr[top_row_start_position + j + 3],
                    src_addr[bottom_row_start_position + j + 3]);
        }
      } else {
        std::swap_ranges(src_addr + top_row_start_position,
                         src_addr + top_row_start_position + bytes_per_row,
                         src_addr + bottom_row_start_position);
      }
    }
  } else {
    if (kN32_SkColorType == kBGRA_8888_SkColorType)  // needs to swizzle
      for (unsigned i = 0; i < height * bytes_per_row; i += 4)
        std::swap(src_addr[i], src_addr[i + 2]);
  }
}

enum AlphaPremultiplyEnforcement {
  kEnforceAlphaPremultiply,
  kDontEnforceAlphaPremultiply,
};

static sk_sp<SkImage> FlipSkImageVertically(
    SkImage* input,
    AlphaPremultiplyEnforcement premultiply_enforcement =
        kDontEnforceAlphaPremultiply,
    const ParsedOptions& options = DefaultOptions()) {
  unsigned width = static_cast<unsigned>(input->width());
  unsigned height = static_cast<unsigned>(input->height());
  SkAlphaType alpha_type =
      ((premultiply_enforcement == kEnforceAlphaPremultiply) ||
       options.premultiply_alpha)
          ? kPremul_SkAlphaType
          : kUnpremul_SkAlphaType;
  SkImageInfo info = SkImageInfo::Make(input->width(), input->height(),
                                       options.color_params.GetSkColorType(),
                                       alpha_type, input->refColorSpace());

  unsigned image_row_bytes = width * info.bytesPerPixel();
  RefPtr<Uint8Array> image_pixels = CopySkImageData(input, info);
  if (!image_pixels)
    return nullptr;
  for (unsigned i = 0; i < height / 2; i++) {
    unsigned top_first_element = i * image_row_bytes;
    unsigned top_last_element = (i + 1) * image_row_bytes;
    unsigned bottom_first_element = (height - 1 - i) * image_row_bytes;
    std::swap_ranges(image_pixels->Data() + top_first_element,
                     image_pixels->Data() + top_last_element,
                     image_pixels->Data() + bottom_first_element);
  }
  return NewSkImageFromRaster(info, std::move(image_pixels), image_row_bytes);
}

static sk_sp<SkImage> PremulSkImageToUnPremul(
    SkImage* input,
    const ParsedOptions& options = DefaultOptions()) {
  SkImageInfo info = SkImageInfo::Make(
      input->width(), input->height(), options.color_params.GetSkColorType(),
      kUnpremul_SkAlphaType, input->refColorSpace());

  RefPtr<Uint8Array> dst_pixels = CopySkImageData(input, info);
  if (!dst_pixels)
    return nullptr;
  return NewSkImageFromRaster(
      info, std::move(dst_pixels),
      static_cast<unsigned>(input->width()) * info.bytesPerPixel());
}

static sk_sp<SkImage> UnPremulSkImageToPremul(
    SkImage* input,
    const ParsedOptions& options = DefaultOptions()) {
  SkImageInfo info = SkImageInfo::Make(
      input->width(), input->height(), options.color_params.GetSkColorType(),
      kPremul_SkAlphaType, input->refColorSpace());

  RefPtr<Uint8Array> dst_pixels = CopySkImageData(input, info);
  if (!dst_pixels)
    return nullptr;
  return NewSkImageFromRaster(
      info, std::move(dst_pixels),
      static_cast<unsigned>(input->width()) * info.bytesPerPixel());
}

static void ApplyColorSpaceConversion(sk_sp<SkImage>& image,
                                      ParsedOptions& options) {
  if (!options.color_correct_rendering_enabled &&
      !options.use_global_target_color_space)
    return;

  sk_sp<SkColorSpace> dst_color_space = nullptr;
  SkColorType dst_color_type = kN32_SkColorType;
  if (options.use_global_target_color_space) {
    dst_color_space = ColorBehavior::GlobalTargetColorSpace().ToSkColorSpace();
  } else {
    dst_color_space = options.color_params.GetSkColorSpace();
    dst_color_type = options.color_params.GetSkColorType();
  }
  if (SkColorSpace::Equals(image->colorSpace(), dst_color_space.get()))
    return;

  SkImageInfo dst_info =
      SkImageInfo::Make(image->width(), image->height(), dst_color_type,
                        image->alphaType(), dst_color_space);

  size_t size = image->width() * image->height() * dst_info.bytesPerPixel();
  sk_sp<SkData> dst_data = SkData::MakeUninitialized(size);
  if (dst_data->size() != size)
    return;
  sk_sp<SkImage> colored_image = nullptr;
  if (image->readPixels(dst_info, dst_data->writable_data(),
                        image->width() * dst_info.bytesPerPixel(), 0, 0)) {
    colored_image = SkImage::MakeRasterData(
        dst_info, dst_data, image->width() * dst_info.bytesPerPixel());
  } else {
    // The desired way to apply color space conversion on a SkImage is to use
    // SkImage::readPixels. However, if the SkImage is GPU-backed, readPixels
    // still might not work properly. In this case, we fall back to drawing
    // the SkImage to a canvas and reading back the result.
    // Skia does not support drawing to unpremul surfaces/canvases.
    sk_sp<SkImage> un_premul_image = nullptr;
    if (image->alphaType() == kUnpremul_SkAlphaType) {
      un_premul_image = UnPremulSkImageToPremul(image.get(), options);
      dst_info = dst_info.makeAlphaType(kPremul_SkAlphaType);
    }

    // If the color space of the source SkImage is null, the following code
    // does not do any color conversion. This cannot be addressed here and
    // the code that creates the SkImage must tag the SkImage with proper
    // color space.
    sk_sp<SkSurface> surface = SkSurface::MakeRaster(dst_info);
    if (!surface)
      return;
    surface->getCanvas()->drawImage(
        un_premul_image ? un_premul_image : sk_sp<SkImage>(image), 0, 0);
    colored_image = surface->makeImageSnapshot();
  }

  if (!colored_image)
    return;
  image = colored_image;
}

sk_sp<SkImage> ImageBitmap::GetSkImageFromDecoder(
    std::unique_ptr<ImageDecoder> decoder,
    SkColorType* decoded_color_type,
    sk_sp<SkColorSpace>* decoded_color_space,
    ColorSpaceInfoUpdate color_space_info_update) {
  if (!decoder->FrameCount())
    return nullptr;
  ImageFrame* frame = decoder->FrameBufferAtIndex(0);
  if (!frame || frame->GetStatus() != ImageFrame::kFrameComplete)
    return nullptr;
  DCHECK(!frame->Bitmap().isNull() && !frame->Bitmap().empty());
  sk_sp<SkImage> image = frame->FinalizePixelsAndGetImage();
  if (color_space_info_update == kUpdateColorSpaceInformation) {
    *decoded_color_type = frame->Bitmap().colorType();
    *decoded_color_space = sk_sp<SkColorSpace>(frame->Bitmap().colorSpace());
  }
  return image;
}

bool ImageBitmap::IsResizeOptionValid(const ImageBitmapOptions& options,
                                      ExceptionState& exception_state) {
  if ((options.hasResizeWidth() && options.resizeWidth() == 0) ||
      (options.hasResizeHeight() && options.resizeHeight() == 0)) {
    exception_state.ThrowDOMException(
        kInvalidStateError,
        "The resizeWidth or/and resizeHeight is equal to 0.");
    return false;
  }
  return true;
}

bool ImageBitmap::IsSourceSizeValid(int source_width,
                                    int source_height,
                                    ExceptionState& exception_state) {
  if (!source_width || !source_height) {
    exception_state.ThrowDOMException(
        kIndexSizeError, String::Format("The source %s provided is 0.",
                                        source_width ? "height" : "width"));
    return false;
  }
  return true;
}

// The parameter imageFormat indicates whether the first parameter "image" is
// unpremultiplied or not.  imageFormat = PremultiplyAlpha means the image is in
// premuliplied format For example, if the image is already in unpremultiplied
// format and we want the created ImageBitmap in the same format, then we don't
// need to use the ImageDecoder to decode the image.
static PassRefPtr<StaticBitmapImage> CropImageAndApplyColorSpaceConversion(
    Image* image,
    ParsedOptions& parsed_options,
    AlphaDisposition image_format,
    ColorBehavior color_behavior) {
  DCHECK(image);
  IntRect img_rect(IntPoint(), IntSize(image->width(), image->height()));
  const IntRect src_rect = Intersection(img_rect, parsed_options.crop_rect);

  // In the case when cropRect doesn't intersect the source image and it
  // requires a umpremul image We immediately return a transparent black image
  // with cropRect.size()
  if (src_rect.IsEmpty() && !parsed_options.premultiply_alpha) {
    SkImageInfo info = SkImageInfo::Make(
        parsed_options.resize_width, parsed_options.resize_height,
        kN32_SkColorType, kUnpremul_SkAlphaType);
    RefPtr<ArrayBuffer> dst_buffer = ArrayBuffer::CreateOrNull(
        static_cast<unsigned>(info.width()) * info.height(),
        info.bytesPerPixel());
    if (!dst_buffer)
      return nullptr;
    RefPtr<Uint8Array> dst_pixels =
        Uint8Array::Create(dst_buffer, 0, dst_buffer->ByteLength());
    return StaticBitmapImage::Create(NewSkImageFromRaster(
        info, std::move(dst_pixels),
        static_cast<unsigned>(info.width()) * info.bytesPerPixel()));
  }

  sk_sp<SkImage> skia_image = image->ImageForCurrentFrame();
  // Attempt to get raw unpremultiplied image data, executed only when skiaImage
  // is premultiplied.
  if ((((!parsed_options.premultiply_alpha && !skia_image->isOpaque()) ||
        !skia_image) &&
       image->Data() && image_format == kPremultiplyAlpha) ||
      color_behavior.IsIgnore()) {
    std::unique_ptr<ImageDecoder> decoder(ImageDecoder::Create(
        image->Data(), true,
        parsed_options.premultiply_alpha ? ImageDecoder::kAlphaPremultiplied
                                         : ImageDecoder::kAlphaNotPremultiplied,
        color_behavior));
    if (!decoder)
      return nullptr;
    SkColorType color_type = parsed_options.color_params.GetSkColorType();
    sk_sp<SkColorSpace> color_space =
        parsed_options.color_params.GetSkColorSpace();
    skia_image = ImageBitmap::GetSkImageFromDecoder(
        std::move(decoder), &color_type, &color_space,
        kUpdateColorSpaceInformation);
    if (!skia_image)
      return nullptr;
  }

  if (parsed_options.crop_rect == src_rect &&
      !parsed_options.should_scale_input) {
    sk_sp<SkImage> cropped_sk_image = skia_image->makeSubset(src_rect);
    ApplyColorSpaceConversion(cropped_sk_image, parsed_options);
    if (parsed_options.flip_y) {
      return StaticBitmapImage::Create(
          FlipSkImageVertically(cropped_sk_image.get(),
                                kDontEnforceAlphaPremultiply, parsed_options));
    }
    // Special case: The first parameter image is unpremul but we need to turn
    // it into premul.
    if (parsed_options.premultiply_alpha &&
        image_format == kDontPremultiplyAlpha) {
      return StaticBitmapImage::Create(
          UnPremulSkImageToPremul(cropped_sk_image.get()));
    }
    return StaticBitmapImage::Create(std::move(cropped_sk_image));
  }

  sk_sp<SkSurface> surface = SkSurface::MakeRasterN32Premul(
      parsed_options.resize_width, parsed_options.resize_height);
  if (!surface)
    return nullptr;
  if (src_rect.IsEmpty())
    return StaticBitmapImage::Create(surface->makeImageSnapshot());

  SkScalar dst_left = std::min(0, -parsed_options.crop_rect.X());
  SkScalar dst_top = std::min(0, -parsed_options.crop_rect.Y());
  if (parsed_options.crop_rect.X() < 0)
    dst_left = -parsed_options.crop_rect.X();
  if (parsed_options.crop_rect.Y() < 0)
    dst_top = -parsed_options.crop_rect.Y();
  if (parsed_options.flip_y) {
    surface->getCanvas()->translate(0, surface->height());
    surface->getCanvas()->scale(1, -1);
  }
  if (parsed_options.should_scale_input) {
    SkRect draw_src_rect = SkRect::MakeXYWH(
        parsed_options.crop_rect.X(), parsed_options.crop_rect.Y(),
        parsed_options.crop_rect.Width(), parsed_options.crop_rect.Height());
    SkRect draw_dst_rect = SkRect::MakeXYWH(0, 0, parsed_options.resize_width,
                                            parsed_options.resize_height);
    SkPaint paint;
    paint.setFilterQuality(parsed_options.resize_quality);
    surface->getCanvas()->drawImageRect(skia_image, draw_src_rect,
                                        draw_dst_rect, &paint);
  } else {
    surface->getCanvas()->drawImage(skia_image, dst_left, dst_top);
  }
  skia_image = surface->makeImageSnapshot();
  ApplyColorSpaceConversion(skia_image, parsed_options);

  if (parsed_options.premultiply_alpha) {
    if (image_format == kDontPremultiplyAlpha)
      return StaticBitmapImage::Create(
          UnPremulSkImageToPremul(skia_image.get()));
    return StaticBitmapImage::Create(std::move(skia_image));
  }
  return StaticBitmapImage::Create(PremulSkImageToUnPremul(skia_image.get()));
}

ImageBitmap::ImageBitmap(ImageElementBase* image,
                         Optional<IntRect> crop_rect,
                         Document* document,
                         const ImageBitmapOptions& options) {
  RefPtr<Image> input = image->CachedImage()->GetImage();
  ParsedOptions parsed_options =
      ParseOptions(options, crop_rect, image->BitmapSourceSize());
  if (DstBufferSizeHasOverflow(parsed_options))
    return;

  if (options.colorSpaceConversion() == kImageBitmapOptionNone) {
    image_ = CropImageAndApplyColorSpaceConversion(input.Get(), parsed_options,
                                                   kPremultiplyAlpha,
                                                   ColorBehavior::Ignore());
  } else {
    image_ = CropImageAndApplyColorSpaceConversion(
        input.Get(), parsed_options, kPremultiplyAlpha,
        ColorBehavior::TransformToGlobalTarget());
  }

  if (!image_)
    return;
  // In the case where the source image is lazy-decoded, m_image may not be in
  // a decoded state, we trigger it here.
  sk_sp<SkImage> sk_image = image_->ImageForCurrentFrame();
  SkPixmap pixmap;
  if (!sk_image->isTextureBacked() && !sk_image->peekPixels(&pixmap)) {
    sk_sp<SkColorSpace> dst_color_space = nullptr;
    SkColorType dst_color_type = kN32_SkColorType;
    if (parsed_options.use_global_target_color_space) {
      dst_color_space =
          ColorBehavior::GlobalTargetColorSpace().ToSkColorSpace();
    } else if (parsed_options.color_correct_rendering_enabled) {
      dst_color_space = parsed_options.color_params.GetSkColorSpace();
      dst_color_type = parsed_options.color_params.GetSkColorType();
    }
    SkImageInfo image_info =
        SkImageInfo::Make(sk_image->width(), sk_image->height(), dst_color_type,
                          kPremul_SkAlphaType, dst_color_space);
    sk_sp<SkSurface> surface = SkSurface::MakeRaster(image_info);
    surface->getCanvas()->drawImage(sk_image, 0, 0);
    image_ = StaticBitmapImage::Create(surface->makeImageSnapshot());
  }
  if (!image_)
    return;
  image_->SetOriginClean(
      !image->WouldTaintOrigin(document->GetSecurityOrigin()));
  image_->SetPremultiplied(parsed_options.premultiply_alpha);
}

ImageBitmap::ImageBitmap(HTMLVideoElement* video,
                         Optional<IntRect> crop_rect,
                         Document* document,
                         const ImageBitmapOptions& options) {
  IntSize player_size;
  if (video->GetWebMediaPlayer())
    player_size = video->GetWebMediaPlayer()->NaturalSize();
  ParsedOptions parsed_options =
      ParseOptions(options, crop_rect, video->BitmapSourceSize());
  if (DstBufferSizeHasOverflow(parsed_options))
    return;

  std::unique_ptr<ImageBuffer> buffer = ImageBuffer::Create(
      IntSize(parsed_options.resize_width, parsed_options.resize_height),
      kNonOpaque, kDoNotInitializeImagePixels);
  if (!buffer)
    return;

  IntPoint dst_point =
      IntPoint(-parsed_options.crop_rect.X(), -parsed_options.crop_rect.Y());
  if (parsed_options.flip_y) {
    buffer->Canvas()->translate(0, buffer->size().Height());
    buffer->Canvas()->scale(1, -1);
  }
  PaintFlags flags;
  if (parsed_options.should_scale_input) {
    float scale_ratio_x = static_cast<float>(parsed_options.resize_width) /
                          parsed_options.crop_rect.Width();
    float scale_ratio_y = static_cast<float>(parsed_options.resize_height) /
                          parsed_options.crop_rect.Height();
    buffer->Canvas()->scale(scale_ratio_x, scale_ratio_y);
    flags.setFilterQuality(parsed_options.resize_quality);
  }
  buffer->Canvas()->translate(dst_point.X(), dst_point.Y());
  video->PaintCurrentFrame(
      buffer->Canvas(),
      IntRect(IntPoint(), IntSize(video->videoWidth(), video->videoHeight())),
      parsed_options.should_scale_input ? &flags : nullptr);

  sk_sp<SkImage> skia_image =
      buffer->NewSkImageSnapshot(kPreferNoAcceleration, kSnapshotReasonUnknown);
  if (!parsed_options.premultiply_alpha)
    skia_image = PremulSkImageToUnPremul(skia_image.get());
  if (!skia_image)
    return;
  image_ = StaticBitmapImage::Create(std::move(skia_image));
  image_->SetOriginClean(
      !video->WouldTaintOrigin(document->GetSecurityOrigin()));
  image_->SetPremultiplied(parsed_options.premultiply_alpha);
}

ImageBitmap::ImageBitmap(HTMLCanvasElement* canvas,
                         Optional<IntRect> crop_rect,
                         const ImageBitmapOptions& options) {
  DCHECK(canvas->IsPaintable());
  RefPtr<Image> input;
  if (canvas->PlaceholderFrame()) {
    input = canvas->PlaceholderFrame();
  } else {
    input = canvas->CopiedImage(kBackBuffer, kPreferAcceleration,
                                kSnapshotReasonCreateImageBitmap);
  }
  ParsedOptions parsed_options = ParseOptions(
      options, crop_rect, IntSize(input->width(), input->height()));
  if (DstBufferSizeHasOverflow(parsed_options))
    return;

  bool is_premultiply_alpha_reverted = false;
  if (!parsed_options.premultiply_alpha) {
    parsed_options.premultiply_alpha = true;
    is_premultiply_alpha_reverted = true;
  }
  image_ = CropImageAndApplyColorSpaceConversion(
      input.Get(), parsed_options, kPremultiplyAlpha,
      ColorBehavior::TransformToGlobalTarget());
  if (!image_)
    return;
  if (is_premultiply_alpha_reverted) {
    parsed_options.premultiply_alpha = false;
    image_ = StaticBitmapImage::Create(
        PremulSkImageToUnPremul(image_->ImageForCurrentFrame().get()));
  }
  if (!image_)
    return;
  image_->SetOriginClean(canvas->OriginClean());
  image_->SetPremultiplied(parsed_options.premultiply_alpha);
}

ImageBitmap::ImageBitmap(OffscreenCanvas* offscreen_canvas,
                         Optional<IntRect> crop_rect,
                         const ImageBitmapOptions& options) {
  SourceImageStatus status;
  RefPtr<Image> input = offscreen_canvas->GetSourceImageForCanvas(
      &status, kPreferNoAcceleration, kSnapshotReasonCreateImageBitmap,
      FloatSize(offscreen_canvas->Size()));
  if (status != kNormalSourceImageStatus)
    return;
  ParsedOptions parsed_options = ParseOptions(
      options, crop_rect, IntSize(input->width(), input->height()));
  if (DstBufferSizeHasOverflow(parsed_options))
    return;

  bool is_premultiply_alpha_reverted = false;
  if (!parsed_options.premultiply_alpha) {
    parsed_options.premultiply_alpha = true;
    is_premultiply_alpha_reverted = true;
  }
  image_ = CropImageAndApplyColorSpaceConversion(
      input.Get(), parsed_options, kPremultiplyAlpha,
      ColorBehavior::TransformToGlobalTarget());
  if (!image_)
    return;
  if (is_premultiply_alpha_reverted) {
    parsed_options.premultiply_alpha = false;
    image_ = StaticBitmapImage::Create(
        PremulSkImageToUnPremul(image_->ImageForCurrentFrame().get()));
  }
  if (!image_)
    return;
  image_->SetOriginClean(offscreen_canvas->OriginClean());
  image_->SetPremultiplied(parsed_options.premultiply_alpha);
}

ImageBitmap::ImageBitmap(const void* pixel_data,
                         uint32_t width,
                         uint32_t height,
                         bool is_image_bitmap_premultiplied,
                         bool is_image_bitmap_origin_clean) {
  SkImageInfo info = SkImageInfo::MakeN32(width, height,
                                          is_image_bitmap_premultiplied
                                              ? kPremul_SkAlphaType
                                              : kUnpremul_SkAlphaType);
  SkPixmap pixmap(info, pixel_data, info.bytesPerPixel() * width);
  image_ = StaticBitmapImage::Create(SkImage::MakeRasterCopy(pixmap));
  if (!image_)
    return;
  image_->SetPremultiplied(is_image_bitmap_premultiplied);
  image_->SetOriginClean(is_image_bitmap_origin_clean);
}

static sk_sp<SkImage> ScaleSkImage(sk_sp<SkImage> sk_image,
                                   unsigned resize_width,
                                   unsigned resize_height,
                                   SkFilterQuality resize_quality,
                                   SkColorType color_type = kN32_SkColorType,
                                   sk_sp<SkColorSpace> color_space = nullptr) {
  SkImageInfo resized_info =
      SkImageInfo::Make(resize_width, resize_height, color_type,
                        kUnpremul_SkAlphaType, color_space);
  RefPtr<ArrayBuffer> dst_buffer = ArrayBuffer::CreateOrNull(
      resize_width * resize_height, resized_info.bytesPerPixel());
  if (!dst_buffer)
    return nullptr;

  if (color_type == kN32_SkColorType) {
    RefPtr<Uint8Array> resized_pixels =
        Uint8Array::Create(dst_buffer, 0, dst_buffer->ByteLength());
    SkPixmap pixmap(
        resized_info, resized_pixels->Data(),
        static_cast<unsigned>(resize_width) * resized_info.bytesPerPixel());
    sk_image->scalePixels(pixmap, resize_quality);
    return SkImage::MakeFromRaster(pixmap,
                                   [](const void*, void* pixels) {
                                     static_cast<Uint8Array*>(pixels)->Deref();
                                   },
                                   resized_pixels.LeakRef());
  }

  RefPtr<Float32Array> resized_pixels =
      Float32Array::Create(dst_buffer, 0, dst_buffer->ByteLength());
  SkPixmap pixmap(
      resized_info, resized_pixels->Data(),
      static_cast<unsigned>(resize_width) * resized_info.bytesPerPixel());
  sk_image->scalePixels(pixmap, resize_quality);
  return SkImage::MakeFromRaster(pixmap,
                                 [](const void*, void* pixels) {
                                   static_cast<Float32Array*>(pixels)->Deref();
                                 },
                                 resized_pixels.LeakRef());
}

ImageBitmap::ImageBitmap(ImageData* data,
                         Optional<IntRect> crop_rect,
                         const ImageBitmapOptions& options) {
  // TODO(xidachen): implement the resize option
  IntRect data_src_rect = IntRect(IntPoint(), data->Size());
  ParsedOptions parsed_options =
      ParseOptions(options, crop_rect, data->BitmapSourceSize());
  if (DstBufferSizeHasOverflow(parsed_options))
    return;
  IntRect src_rect = crop_rect
                         ? Intersection(parsed_options.crop_rect, data_src_rect)
                         : data_src_rect;

  // treat non-premultiplyAlpha as a special case
  if (!parsed_options.premultiply_alpha) {
    unsigned char* src_addr = data->data()->Data();

    // Using kN32 type, swizzle input if necessary.
    SkImageInfo info = SkImageInfo::Make(
        parsed_options.crop_rect.Width(), parsed_options.crop_rect.Height(),
        kN32_SkColorType, kUnpremul_SkAlphaType, data->GetSkColorSpace());
    unsigned bytes_per_pixel = static_cast<unsigned>(info.bytesPerPixel());
    unsigned src_pixel_bytes_per_row = bytes_per_pixel * data->Size().Width();
    unsigned dst_pixel_bytes_per_row =
        bytes_per_pixel * parsed_options.crop_rect.Width();
    sk_sp<SkImage> sk_image;
    if (parsed_options.crop_rect == IntRect(IntPoint(), data->Size())) {
      SwizzleImageData(src_addr, data->Size().Height(), src_pixel_bytes_per_row,
                       parsed_options.flip_y);
      sk_image = SkImage::MakeRasterCopy(
          SkPixmap(info, src_addr, dst_pixel_bytes_per_row));
      // restore the original ImageData
      SwizzleImageData(src_addr, data->Size().Height(), src_pixel_bytes_per_row,
                       parsed_options.flip_y);
    } else {
      RefPtr<ArrayBuffer> dst_buffer = ArrayBuffer::CreateOrNull(
          static_cast<unsigned>(parsed_options.crop_rect.Height()) *
              parsed_options.crop_rect.Width(),
          bytes_per_pixel);
      if (!dst_buffer)
        return;
      RefPtr<Uint8Array> copied_data_buffer =
          Uint8Array::Create(dst_buffer, 0, dst_buffer->ByteLength());
      if (!src_rect.IsEmpty()) {
        IntPoint src_point = IntPoint(
            (parsed_options.crop_rect.X() > 0) ? parsed_options.crop_rect.X()
                                               : 0,
            (parsed_options.crop_rect.Y() > 0) ? parsed_options.crop_rect.Y()
                                               : 0);
        IntPoint dst_point = IntPoint((parsed_options.crop_rect.X() >= 0)
                                          ? 0
                                          : -parsed_options.crop_rect.X(),
                                      (parsed_options.crop_rect.Y() >= 0)
                                          ? 0
                                          : -parsed_options.crop_rect.Y());
        int copy_height = data->Size().Height() - src_point.Y();
        if (parsed_options.crop_rect.Height() < copy_height)
          copy_height = parsed_options.crop_rect.Height();
        int copy_width = data->Size().Width() - src_point.X();
        if (parsed_options.crop_rect.Width() < copy_width)
          copy_width = parsed_options.crop_rect.Width();
        for (int i = 0; i < copy_height; i++) {
          unsigned src_start_copy_position =
              (i + src_point.Y()) * src_pixel_bytes_per_row +
              src_point.X() * bytes_per_pixel;
          unsigned src_end_copy_position =
              src_start_copy_position + copy_width * bytes_per_pixel;
          unsigned dst_start_copy_position;
          if (parsed_options.flip_y)
            dst_start_copy_position =
                (parsed_options.crop_rect.Height() - 1 - dst_point.Y() - i) *
                    dst_pixel_bytes_per_row +
                dst_point.X() * bytes_per_pixel;
          else
            dst_start_copy_position =
                (dst_point.Y() + i) * dst_pixel_bytes_per_row +
                dst_point.X() * bytes_per_pixel;
          for (unsigned j = 0;
               j < src_end_copy_position - src_start_copy_position; j++) {
            // swizzle when necessary
            if (kN32_SkColorType == kBGRA_8888_SkColorType) {
              if (j % 4 == 0)
                copied_data_buffer->Data()[dst_start_copy_position + j] =
                    src_addr[src_start_copy_position + j + 2];
              else if (j % 4 == 2)
                copied_data_buffer->Data()[dst_start_copy_position + j] =
                    src_addr[src_start_copy_position + j - 2];
              else
                copied_data_buffer->Data()[dst_start_copy_position + j] =
                    src_addr[src_start_copy_position + j];
            } else {
              copied_data_buffer->Data()[dst_start_copy_position + j] =
                  src_addr[src_start_copy_position + j];
            }
          }
        }
      }
      sk_image = NewSkImageFromRaster(info, std::move(copied_data_buffer),
                                      dst_pixel_bytes_per_row);
    }
    if (!sk_image)
      return;
    ApplyColorSpaceConversion(sk_image, parsed_options);
    if (parsed_options.should_scale_input) {
      image_ = StaticBitmapImage::Create(ScaleSkImage(
          sk_image, parsed_options.resize_width, parsed_options.resize_height,
          parsed_options.resize_quality,
          parsed_options.color_params.GetSkColorType(),
          data->GetSkColorSpace()));
    } else {
      image_ = StaticBitmapImage::Create(sk_image);
    }
    if (!image_)
      return;
    image_->SetPremultiplied(parsed_options.premultiply_alpha);
    return;
  }

  CanvasColorParams canvas_color_params;
  if (RuntimeEnabledFeatures::experimentalCanvasFeaturesEnabled() &&
      RuntimeEnabledFeatures::colorCorrectRenderingEnabled()) {
    ImageDataColorSettings color_settings;
    data->getColorSettings(color_settings);
    CanvasColorSpace canvas_color_space =
        ImageData::GetCanvasColorSpace(color_settings.colorSpace());
    CanvasPixelFormat canvas_pixel_format = kRGBA8CanvasPixelFormat;
    if (ImageData::GetImageDataStorageFormat(color_settings.storageFormat()) !=
        kUint8ClampedArrayStorageFormat) {
      canvas_pixel_format = kF16CanvasPixelFormat;
    }
    canvas_color_params =
        CanvasColorParams(canvas_color_space, canvas_pixel_format);
  }
  std::unique_ptr<ImageBuffer> buffer =
      ImageBuffer::Create(parsed_options.crop_rect.Size(), kNonOpaque,
                          kDoNotInitializeImagePixels, canvas_color_params);
  if (!buffer)
    return;

  if (src_rect.IsEmpty()) {
    image_ = StaticBitmapImage::Create(buffer->NewSkImageSnapshot(
        kPreferNoAcceleration, kSnapshotReasonUnknown));
    return;
  }

  IntPoint dst_point = IntPoint(std::min(0, -parsed_options.crop_rect.X()),
                                std::min(0, -parsed_options.crop_rect.Y()));
  if (parsed_options.crop_rect.X() < 0)
    dst_point.SetX(-parsed_options.crop_rect.X());
  if (parsed_options.crop_rect.Y() < 0)
    dst_point.SetY(-parsed_options.crop_rect.Y());

  buffer->PutByteArray(kUnmultiplied, data->data()->Data(), data->Size(),
                       src_rect, dst_point);
  sk_sp<SkImage> sk_image =
      buffer->NewSkImageSnapshot(kPreferNoAcceleration, kSnapshotReasonUnknown);
  if (parsed_options.flip_y)
    sk_image = FlipSkImageVertically(sk_image.get(), kEnforceAlphaPremultiply);
  if (!sk_image)
    return;
  if (parsed_options.should_scale_input) {
    sk_sp<SkSurface> surface = SkSurface::MakeRaster(SkImageInfo::MakeN32Premul(
        parsed_options.resize_width, parsed_options.resize_height,
        data->GetSkColorSpace()));
    if (!surface)
      return;
    SkPaint paint;
    paint.setFilterQuality(parsed_options.resize_quality);
    SkRect dst_draw_rect = SkRect::MakeWH(parsed_options.resize_width,
                                          parsed_options.resize_height);
    surface->getCanvas()->drawImageRect(sk_image, dst_draw_rect, &paint);
    sk_image = surface->makeImageSnapshot();
  }
  ApplyColorSpaceConversion(sk_image, parsed_options);
  image_ = StaticBitmapImage::Create(std::move(sk_image));
}

ImageBitmap::ImageBitmap(ImageBitmap* bitmap,
                         Optional<IntRect> crop_rect,
                         const ImageBitmapOptions& options) {
  RefPtr<Image> input = bitmap->BitmapImage();
  if (!input)
    return;
  ParsedOptions parsed_options =
      ParseOptions(options, crop_rect, input->Size());
  if (DstBufferSizeHasOverflow(parsed_options))
    return;

  image_ = CropImageAndApplyColorSpaceConversion(
      input.Get(), parsed_options,
      bitmap->IsPremultiplied() ? kPremultiplyAlpha : kDontPremultiplyAlpha,
      ColorBehavior::TransformToGlobalTarget());
  if (!image_)
    return;
  image_->SetOriginClean(bitmap->OriginClean());
  image_->SetPremultiplied(parsed_options.premultiply_alpha);
}

ImageBitmap::ImageBitmap(RefPtr<StaticBitmapImage> image,
                         Optional<IntRect> crop_rect,
                         const ImageBitmapOptions& options) {
  bool origin_clean = image->OriginClean();
  ParsedOptions parsed_options =
      ParseOptions(options, crop_rect, image->Size());
  if (DstBufferSizeHasOverflow(parsed_options))
    return;

  image_ = CropImageAndApplyColorSpaceConversion(
      image.Get(), parsed_options, kPremultiplyAlpha,
      ColorBehavior::TransformToGlobalTarget());
  if (!image_)
    return;

  image_->SetOriginClean(origin_clean);
  image_->SetPremultiplied(parsed_options.premultiply_alpha);
}

ImageBitmap::ImageBitmap(PassRefPtr<StaticBitmapImage> image) {
  image_ = std::move(image);
}

PassRefPtr<StaticBitmapImage> ImageBitmap::Transfer() {
  DCHECK(!IsNeutered());
  is_neutered_ = true;
  image_->Transfer();
  return std::move(image_);
}

ImageBitmap::~ImageBitmap() {}

ImageBitmap* ImageBitmap::Create(ImageElementBase* image,
                                 Optional<IntRect> crop_rect,
                                 Document* document,
                                 const ImageBitmapOptions& options) {
  return new ImageBitmap(image, crop_rect, document, options);
}

ImageBitmap* ImageBitmap::Create(HTMLVideoElement* video,
                                 Optional<IntRect> crop_rect,
                                 Document* document,
                                 const ImageBitmapOptions& options) {
  return new ImageBitmap(video, crop_rect, document, options);
}

ImageBitmap* ImageBitmap::Create(HTMLCanvasElement* canvas,
                                 Optional<IntRect> crop_rect,
                                 const ImageBitmapOptions& options) {
  return new ImageBitmap(canvas, crop_rect, options);
}

ImageBitmap* ImageBitmap::Create(OffscreenCanvas* offscreen_canvas,
                                 Optional<IntRect> crop_rect,
                                 const ImageBitmapOptions& options) {
  return new ImageBitmap(offscreen_canvas, crop_rect, options);
}

ImageBitmap* ImageBitmap::Create(ImageData* data,
                                 Optional<IntRect> crop_rect,
                                 const ImageBitmapOptions& options) {
  return new ImageBitmap(data, crop_rect, options);
}

ImageBitmap* ImageBitmap::Create(ImageBitmap* bitmap,
                                 Optional<IntRect> crop_rect,
                                 const ImageBitmapOptions& options) {
  return new ImageBitmap(bitmap, crop_rect, options);
}

ImageBitmap* ImageBitmap::Create(PassRefPtr<StaticBitmapImage> image,
                                 Optional<IntRect> crop_rect,
                                 const ImageBitmapOptions& options) {
  return new ImageBitmap(std::move(image), crop_rect, options);
}

ImageBitmap* ImageBitmap::Create(PassRefPtr<StaticBitmapImage> image) {
  return new ImageBitmap(std::move(image));
}

ImageBitmap* ImageBitmap::Create(const void* pixel_data,
                                 uint32_t width,
                                 uint32_t height,
                                 bool is_image_bitmap_premultiplied,
                                 bool is_image_bitmap_origin_clean) {
  return new ImageBitmap(pixel_data, width, height,
                         is_image_bitmap_premultiplied,
                         is_image_bitmap_origin_clean);
}

void ImageBitmap::close() {
  if (!image_ || is_neutered_)
    return;
  image_.Clear();
  is_neutered_ = true;
}

// static
ImageBitmap* ImageBitmap::Take(ScriptPromiseResolver*, sk_sp<SkImage> image) {
  return ImageBitmap::Create(StaticBitmapImage::Create(std::move(image)));
}

PassRefPtr<Uint8Array> ImageBitmap::CopyBitmapData(AlphaDisposition alpha_op,
                                                   DataColorFormat format) {
  SkImageInfo info = SkImageInfo::Make(
      width(), height(),
      (format == kRGBAColorType) ? kRGBA_8888_SkColorType : kN32_SkColorType,
      (alpha_op == kPremultiplyAlpha) ? kPremul_SkAlphaType
                                      : kUnpremul_SkAlphaType);
  RefPtr<Uint8Array> dst_pixels =
      CopySkImageData(image_->ImageForCurrentFrame().get(), info);
  return dst_pixels.Release();
}

unsigned long ImageBitmap::width() const {
  if (!image_)
    return 0;
  DCHECK_GT(image_->width(), 0);
  return image_->width();
}

unsigned long ImageBitmap::height() const {
  if (!image_)
    return 0;
  DCHECK_GT(image_->height(), 0);
  return image_->height();
}

bool ImageBitmap::IsAccelerated() const {
  return image_ && (image_->IsTextureBacked() || image_->HasMailbox());
}

IntSize ImageBitmap::Size() const {
  if (!image_)
    return IntSize();
  DCHECK_GT(image_->width(), 0);
  DCHECK_GT(image_->height(), 0);
  return IntSize(image_->width(), image_->height());
}

ScriptPromise ImageBitmap::CreateImageBitmap(ScriptState* script_state,
                                             EventTarget& event_target,
                                             Optional<IntRect> crop_rect,
                                             const ImageBitmapOptions& options,
                                             ExceptionState& exception_state) {
  if ((crop_rect && !IsSourceSizeValid(crop_rect->Width(), crop_rect->Height(),
                                       exception_state)) ||
      !IsSourceSizeValid(width(), height(), exception_state))
    return ScriptPromise();
  if (!IsResizeOptionValid(options, exception_state))
    return ScriptPromise();
  return ImageBitmapSource::FulfillImageBitmap(
      script_state, Create(this, crop_rect, options));
}

PassRefPtr<Image> ImageBitmap::GetSourceImageForCanvas(
    SourceImageStatus* status,
    AccelerationHint,
    SnapshotReason,
    const FloatSize&) {
  *status = kNormalSourceImageStatus;
  if (!image_)
    return nullptr;
  if (image_->IsPremultiplied())
    return image_;
  // Skia does not support drawing unpremul SkImage on SkCanvas.
  // Premultiply and return.
  sk_sp<SkImage> premul_sk_image =
      UnPremulSkImageToPremul(image_->ImageForCurrentFrame().get());
  return StaticBitmapImage::Create(premul_sk_image);
}

void ImageBitmap::AdjustDrawRects(FloatRect* src_rect,
                                  FloatRect* dst_rect) const {}

FloatSize ImageBitmap::ElementSize(const FloatSize&) const {
  return FloatSize(width(), height());
}

DEFINE_TRACE(ImageBitmap) {}

}  // namespace blink
