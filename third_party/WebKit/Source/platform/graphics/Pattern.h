/*
 * Copyright (C) 2006, 2007, 2008 Apple Computer, Inc.  All rights reserved.
 * Copyright (C) 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2007-2008 Torch Mobile, Inc.
 * Copyright (C) 2013 Google, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef Pattern_h
#define Pattern_h

#include "platform/PlatformExport.h"
#include "platform/graphics/Image.h"
#include "platform/graphics/paint/PaintRecord.h"
#include "platform/graphics/paint/PaintShader.h"
#include "third_party/skia/include/core/SkRefCnt.h"

#include "platform/wtf/Noncopyable.h"
#include "platform/wtf/PassRefPtr.h"
#include "platform/wtf/RefCounted.h"

class SkMatrix;

namespace blink {

class PLATFORM_EXPORT Pattern : public RefCounted<Pattern> {
  WTF_MAKE_NONCOPYABLE(Pattern);

 public:
  enum RepeatMode {
    kRepeatModeX = 1 << 0,
    kRepeatModeY = 1 << 1,

    kRepeatModeNone = 0,
    kRepeatModeXY = kRepeatModeX | kRepeatModeY
  };

  static PassRefPtr<Pattern> CreateImagePattern(PassRefPtr<Image>,
                                                RepeatMode = kRepeatModeXY);
  static PassRefPtr<Pattern> CreatePaintRecordPattern(
      sk_sp<PaintRecord>,
      RepeatMode = kRepeatModeXY);
  virtual ~Pattern();

  void ApplyToFlags(PaintFlags&, const SkMatrix&);

  bool IsRepeatX() const { return repeat_mode_ & kRepeatModeX; }
  bool IsRepeatY() const { return repeat_mode_ & kRepeatModeY; }
  bool IsRepeatXY() const { return repeat_mode_ == kRepeatModeXY; }

  virtual bool IsTextureBacked() const { return false; }

 protected:
  virtual sk_sp<PaintShader> CreateShader(const SkMatrix&) = 0;
  virtual bool IsLocalMatrixChanged(const SkMatrix&) const;

  RepeatMode repeat_mode_;

  Pattern(RepeatMode);
  mutable sk_sp<PaintShader> cached_shader_;
};

}  // namespace blink

#endif
