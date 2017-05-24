// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/vr_shell/ui_elements/exit_warning.h"

#include "base/memory/ptr_util.h"
#include "chrome/browser/android/vr_shell/textures/exit_warning_texture.h"

namespace vr_shell {

ExitWarning::ExitWarning(int preferred_width)
    : TexturedElement(preferred_width),
      texture_(base::MakeUnique<ExitWarningTexture>()) {}

ExitWarning::~ExitWarning() = default;

UiTexture* ExitWarning::GetTexture() const {
  return texture_.get();
}

}  // namespace vr_shell
