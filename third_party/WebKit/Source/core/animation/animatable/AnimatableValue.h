/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef AnimatableValue_h
#define AnimatableValue_h

#include "core/CoreExport.h"
#include "core/css/CSSValue.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/RefCounted.h"

namespace blink {

class CORE_EXPORT AnimatableValue : public RefCounted<AnimatableValue> {
 public:
  virtual ~AnimatableValue() {}

  static PassRefPtr<AnimatableValue> Interpolate(const AnimatableValue*,
                                                 const AnimatableValue*,
                                                 double fraction);
  static bool UsesDefaultInterpolation(const AnimatableValue* from,
                                       const AnimatableValue* to) {
    return !from->IsSameType(to) || from->UsesDefaultInterpolationWith(to);
  }

  bool Equals(const AnimatableValue* value) const {
    return IsSameType(value) && EqualTo(value);
  }
  bool Equals(const AnimatableValue& value) const { return Equals(&value); }

  bool IsClipPathOperation() const {
    return GetType() == kTypeClipPathOperation;
  }
  bool IsColor() const { return GetType() == kTypeColor; }
  bool IsDouble() const { return GetType() == kTypeDouble; }
  bool IsDoubleAndBool() const { return GetType() == kTypeDoubleAndBool; }
  bool IsFilterOperations() const { return GetType() == kTypeFilterOperations; }
  bool IsFontVariationSettings() const {
    return GetType() == kTypeFontVariationSettings;
  }
  bool IsImage() const { return GetType() == kTypeImage; }
  bool IsLength() const { return GetType() == kTypeLength; }
  bool IsLengthBox() const { return GetType() == kTypeLengthBox; }
  bool IsLengthBoxAndBool() const { return GetType() == kTypeLengthBoxAndBool; }
  bool IsLengthPoint() const { return GetType() == kTypeLengthPoint; }
  bool IsLengthPoint3D() const { return GetType() == kTypeLengthPoint3D; }
  bool IsLengthSize() const { return GetType() == kTypeLengthSize; }
  bool IsPath() const { return GetType() == kTypePath; }
  bool IsRepeatable() const { return GetType() == kTypeRepeatable; }
  bool IsSVGLength() const { return GetType() == kTypeSVGLength; }
  bool IsSVGPaint() const { return GetType() == kTypeSVGPaint; }
  bool IsShadow() const { return GetType() == kTypeShadow; }
  bool IsShapeValue() const { return GetType() == kTypeShapeValue; }
  bool IsStrokeDasharrayList() const {
    return GetType() == kTypeStrokeDasharrayList;
  }
  bool IsTransform() const { return GetType() == kTypeTransform; }
  bool IsUnknown() const { return GetType() == kTypeUnknown; }
  bool IsVisibility() const { return GetType() == kTypeVisibility; }

  bool IsSameType(const AnimatableValue* value) const {
    DCHECK(value);
    return value->GetType() == GetType();
  }

 protected:
  enum AnimatableType {
    kTypeClipPathOperation,
    kTypeColor,
    kTypeDouble,
    kTypeDoubleAndBool,
    kTypeFilterOperations,
    kTypeFontVariationSettings,
    kTypeImage,
    kTypeLength,
    kTypeLengthBox,
    kTypeLengthBoxAndBool,
    kTypeLengthPoint,
    kTypeLengthPoint3D,
    kTypeLengthSize,
    kTypePath,
    kTypeRepeatable,
    kTypeSVGLength,
    kTypeSVGPaint,
    kTypeShadow,
    kTypeShapeValue,
    kTypeStrokeDasharrayList,
    kTypeTransform,
    kTypeUnknown,
    kTypeVisibility,
  };

  virtual bool UsesDefaultInterpolationWith(
      const AnimatableValue* value) const {
    NOTREACHED();
    return false;
  }
  virtual PassRefPtr<AnimatableValue> InterpolateTo(const AnimatableValue*,
                                                    double fraction) const {
    NOTREACHED();
    return nullptr;
  }
  static PassRefPtr<AnimatableValue> DefaultInterpolateTo(
      const AnimatableValue* left,
      const AnimatableValue* right,
      double fraction) {
    return TakeConstRef((fraction < 0.5) ? left : right);
  }

  template <class T>
  static PassRefPtr<T> TakeConstRef(const T* value) {
    return PassRefPtr<T>(const_cast<T*>(value));
  }

 private:
  virtual AnimatableType GetType() const = 0;
  // Implementations can assume that the object being compared has the same type
  // as the object this is called on
  virtual bool EqualTo(const AnimatableValue*) const = 0;

  template <class Keyframe>
  friend class KeyframeEffectModel;
};

#define DEFINE_ANIMATABLE_VALUE_TYPE_CASTS(thisType, predicate)         \
  DEFINE_TYPE_CASTS(thisType, AnimatableValue, value, value->predicate, \
                    value.predicate)

}  // namespace blink

#endif  // AnimatableValue_h
