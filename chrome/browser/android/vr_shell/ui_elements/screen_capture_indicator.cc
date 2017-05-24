// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/vr_shell/ui_elements/screen_capture_indicator.h"

#include "base/memory/ptr_util.h"
#include "chrome/browser/android/vr_shell/textures/system_indicator_texture.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"

namespace vr_shell {

ScreenCaptureIndicator::ScreenCaptureIndicator(int preferred_width)
    : TexturedElement(preferred_width),
      texture_(base::MakeUnique<SystemIndicatorTexture>(
          vector_icons::kScreenShareIcon,
          IDS_SCREEN_CAPTURE_NOTIFICATION_TEXT_2)) {}

ScreenCaptureIndicator::~ScreenCaptureIndicator() = default;

UiTexture* ScreenCaptureIndicator::GetTexture() const {
  return texture_.get();
}

}  // namespace vr_shell
