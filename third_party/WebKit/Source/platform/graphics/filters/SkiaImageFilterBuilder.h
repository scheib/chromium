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

#ifndef SkiaImageFilterBuilder_h
#define SkiaImageFilterBuilder_h

#include "platform/PlatformExport.h"
#include "platform/graphics/InterpolationSpace.h"
#include "platform/graphics/paint/PaintRecord.h"
#include "third_party/skia/include/core/SkRefCnt.h"

class SkImageFilter;

namespace blink {

class BoxReflection;
class FilterEffect;

namespace SkiaImageFilterBuilder {

PLATFORM_EXPORT sk_sp<SkImageFilter> Build(
    FilterEffect*,
    InterpolationSpace,
    bool requires_pm_color_validation = true);

PLATFORM_EXPORT sk_sp<SkImageFilter> TransformInterpolationSpace(
    sk_sp<SkImageFilter> input,
    InterpolationSpace src_interpolation_space,
    InterpolationSpace dst_interpolation_space);

PLATFORM_EXPORT void PopulateSourceGraphicImageFilters(
    FilterEffect* source_graphic,
    sk_sp<SkImageFilter> input,
    InterpolationSpace input_interpolation_space);
PLATFORM_EXPORT void BuildSourceGraphic(FilterEffect*, sk_sp<PaintRecord>);

PLATFORM_EXPORT sk_sp<SkImageFilter> BuildBoxReflectFilter(
    const BoxReflection&,
    sk_sp<SkImageFilter> input);

}  // namespace SkiaImageFilterBuilder
}  // namespace blink

#endif
