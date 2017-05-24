/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/graphics/filters/SkiaImageFilterBuilder.h"

#include "SkBlurImageFilter.h"
#include "SkColorFilterImageFilter.h"
#include "SkColorMatrixFilter.h"
#include "SkTableColorFilter.h"
#include "platform/graphics/BoxReflection.h"
#include "platform/graphics/filters/FilterEffect.h"
#include "platform/graphics/paint/PaintCanvas.h"
#include "platform/graphics/paint/PaintRecord.h"
#include "platform/graphics/skia/SkiaUtils.h"
#include "third_party/skia/include/effects/SkImageSource.h"
#include "third_party/skia/include/effects/SkOffsetImageFilter.h"
#include "third_party/skia/include/effects/SkPictureImageFilter.h"
#include "third_party/skia/include/effects/SkXfermodeImageFilter.h"

namespace blink {
namespace SkiaImageFilterBuilder {

void PopulateSourceGraphicImageFilters(
    FilterEffect* source_graphic,
    sk_sp<SkImageFilter> input,
    InterpolationSpace input_interpolation_space) {
  // Prepopulate SourceGraphic with two image filters: one with a null image
  // filter, and the other with a colorspace conversion filter.
  // We don't know what color space the interior nodes will request, so we
  // have to initialize SourceGraphic with both options.
  // Since we know SourceGraphic is always PM-valid, we also use these for
  // the PM-validated options.
  sk_sp<SkImageFilter> device_filter = TransformInterpolationSpace(
      input, input_interpolation_space, kInterpolationSpaceSRGB);
  sk_sp<SkImageFilter> linear_filter = TransformInterpolationSpace(
      input, input_interpolation_space, kInterpolationSpaceLinear);
  source_graphic->SetImageFilter(kInterpolationSpaceSRGB, false, device_filter);
  source_graphic->SetImageFilter(kInterpolationSpaceLinear, false,
                                 linear_filter);
  source_graphic->SetImageFilter(kInterpolationSpaceSRGB, true, device_filter);
  source_graphic->SetImageFilter(kInterpolationSpaceLinear, true,
                                 linear_filter);
}

sk_sp<SkImageFilter> Build(
    FilterEffect* effect,
    InterpolationSpace interpolation_space,
    bool destination_requires_valid_pre_multiplied_pixels) {
  if (!effect)
    return nullptr;

  bool requires_pm_color_validation =
      effect->MayProduceInvalidPreMultipliedPixels() &&
      destination_requires_valid_pre_multiplied_pixels;

  if (SkImageFilter* filter = effect->GetImageFilter(
          interpolation_space, requires_pm_color_validation))
    return sk_ref_sp(filter);

  // Note that we may still need the color transform even if the filter is null
  sk_sp<SkImageFilter> orig_filter =
      requires_pm_color_validation
          ? effect->CreateImageFilter()
          : effect->CreateImageFilterWithoutValidation();
  sk_sp<SkImageFilter> filter = TransformInterpolationSpace(
      orig_filter, effect->OperatingInterpolationSpace(), interpolation_space);
  effect->SetImageFilter(interpolation_space, requires_pm_color_validation,
                         filter);
  if (filter.get() != orig_filter.get())
    effect->SetImageFilter(effect->OperatingInterpolationSpace(),
                           requires_pm_color_validation,
                           std::move(orig_filter));
  return filter;
}

sk_sp<SkImageFilter> TransformInterpolationSpace(
    sk_sp<SkImageFilter> input,
    InterpolationSpace src_interpolation_space,
    InterpolationSpace dst_interpolation_space) {
  sk_sp<SkColorFilter> color_filter =
      InterpolationSpaceUtilities::CreateInterpolationSpaceFilter(
          src_interpolation_space, dst_interpolation_space);
  if (!color_filter)
    return input;

  return SkColorFilterImageFilter::Make(std::move(color_filter),
                                        std::move(input));
}

void BuildSourceGraphic(FilterEffect* source_graphic,
                        sk_sp<PaintRecord> record) {
  DCHECK(record);
  SkRect cull_rect = record->cullRect();
  sk_sp<SkImageFilter> filter =
      SkPictureImageFilter::Make(ToSkPicture(record), cull_rect);
  PopulateSourceGraphicImageFilters(
      source_graphic, std::move(filter),
      source_graphic->OperatingInterpolationSpace());
}

static const float kMaxMaskBufferSize =
    50.f * 1024.f * 1024.f / 4.f;  // 50MB / 4 bytes per pixel

sk_sp<SkImageFilter> BuildBoxReflectFilter(const BoxReflection& reflection,
                                           sk_sp<SkImageFilter> input) {
  sk_sp<SkImageFilter> masked_input;
  if (sk_sp<PaintRecord> mask_record = reflection.Mask()) {
    // Since PaintRecords can't be serialized to the browser process, first
    // raster the mask to a bitmap, then encode it in an SkImageSource, which
    // can be serialized.
    SkBitmap bitmap;
    const SkRect cull_rect = mask_record->cullRect();
    if (static_cast<float>(cull_rect.width()) *
            static_cast<float>(cull_rect.height()) <
        kMaxMaskBufferSize) {
      bitmap.allocPixels(
          SkImageInfo::MakeN32Premul(cull_rect.width(), cull_rect.height()));
      SkiaPaintCanvas canvas(bitmap);
      canvas.clear(SK_ColorTRANSPARENT);
      canvas.translate(-cull_rect.x(), -cull_rect.y());
      canvas.drawPicture(mask_record);
      sk_sp<SkImage> image = SkImage::MakeFromBitmap(bitmap);

      // SkXfermodeImageFilter can choose an excessively large size if the
      // mask is smaller than the filtered contents (due to overflow).
      // http://skbug.com/5210
      SkImageFilter::CropRect crop_rect(mask_record->cullRect());
      masked_input = SkXfermodeImageFilter::Make(
          SkBlendMode::kSrcIn,
          SkOffsetImageFilter::Make(cull_rect.x(), cull_rect.y(),
                                    SkImageSource::Make(image)),
          input, &crop_rect);
    } else {
      // If the buffer is excessively big, give up and make an
      // SkPictureImageFilter anyway, even if it might not render.
      SkImageFilter::CropRect crop_rect(mask_record->cullRect());
      masked_input = SkXfermodeImageFilter::Make(
          SkBlendMode::kSrcOver,
          SkPictureImageFilter::Make(ToSkPicture(std::move(mask_record))),
          input, &crop_rect);
    }
  } else {
    masked_input = input;
  }
  sk_sp<SkImageFilter> flip_image_filter = SkImageFilter::MakeMatrixFilter(
      reflection.ReflectionMatrix(), kLow_SkFilterQuality,
      std::move(masked_input));
  return SkXfermodeImageFilter::Make(SkBlendMode::kSrcOver,
                                     std::move(flip_image_filter),
                                     std::move(input), nullptr);
}

}  // namespace SkiaImageFilterBuilder
}  // namespace blink
