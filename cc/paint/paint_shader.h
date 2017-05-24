// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_PAINT_PAINT_SHADER_H_
#define CC_PAINT_PAINT_SHADER_H_

#include "cc/paint/paint_record.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkShader.h"

namespace cc {
using PaintShader = SkShader;

inline sk_sp<PaintShader> WrapSkShader(sk_sp<SkShader> shader) {
  return shader;
}

inline sk_sp<PaintShader> MakePaintShaderImage(sk_sp<const SkImage> image,
                                               SkShader::TileMode tx,
                                               SkShader::TileMode ty,
                                               const SkMatrix* local_matrix) {
  return image->makeShader(tx, ty, local_matrix);
}

inline sk_sp<PaintShader> MakePaintShaderRecord(sk_sp<PaintRecord> record,
                                                SkShader::TileMode tx,
                                                SkShader::TileMode ty,
                                                const SkMatrix* local_matrix,
                                                const SkRect* tile) {
  return SkShader::MakePictureShader(ToSkPicture(record), tx, ty, local_matrix,
                                     tile);
}

}  // namespace cc

#endif  // CC_PAINT_PAINT_SHADER_H_
