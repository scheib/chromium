// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_unpositioned_float.h"

#include "core/style/ComputedStyle.h"

namespace blink {

bool NGUnpositionedFloat::IsLeft() const {
  return node->Style().Floating() == EFloat::kLeft;
}

bool NGUnpositionedFloat::IsRight() const {
  return node->Style().Floating() == EFloat::kRight;
}

EClear NGUnpositionedFloat::ClearType() const {
  return node->Style().Clear();
}

}  // namespace blink
