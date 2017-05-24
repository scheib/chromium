// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PaintRecordPattern_h
#define PaintRecordPattern_h

#include "platform/graphics/Pattern.h"
#include "third_party/skia/include/core/SkRefCnt.h"

namespace blink {

// TODO(enne): rename this
class PLATFORM_EXPORT PaintRecordPattern final : public Pattern {
 public:
  static PassRefPtr<PaintRecordPattern> Create(sk_sp<PaintRecord>, RepeatMode);

  ~PaintRecordPattern() override;

 protected:
  sk_sp<PaintShader> CreateShader(const SkMatrix&) override;

 private:
  PaintRecordPattern(sk_sp<PaintRecord>, RepeatMode);

  sk_sp<PaintRecord> tile_record_;
};

}  // namespace blink

#endif
