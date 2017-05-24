// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AnimatableFontVariationSettings_h
#define AnimatableFontVariationSettings_h

#include "core/animation/animatable/AnimatableValue.h"

#include "platform/fonts/opentype/FontSettings.h"

namespace blink {

class AnimatableFontVariationSettings final : public AnimatableValue {
 public:
  ~AnimatableFontVariationSettings() override {}
  static PassRefPtr<AnimatableFontVariationSettings> Create(
      PassRefPtr<FontVariationSettings> settings) {
    return AdoptRef(new AnimatableFontVariationSettings(std::move(settings)));
  }

 private:
  explicit AnimatableFontVariationSettings(
      PassRefPtr<FontVariationSettings> settings)
      : settings_(std::move(settings)) {}
  AnimatableType GetType() const override { return kTypeFontVariationSettings; }
  bool EqualTo(const AnimatableValue* other) const final;

  const RefPtr<FontVariationSettings> settings_;
};

DEFINE_ANIMATABLE_VALUE_TYPE_CASTS(AnimatableFontVariationSettings,
                                   IsFontVariationSettings());

}  // namespace blink

#endif  // AnimatableFontVariationSettings_h
