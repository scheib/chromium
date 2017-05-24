// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/animation/animatable/AnimatableFontVariationSettings.h"

namespace blink {

bool AnimatableFontVariationSettings::EqualTo(
    const AnimatableValue* value) const {
  return DataEquivalent(settings_,
                        ToAnimatableFontVariationSettings(value)->settings_);
}

}  // namespace blink
