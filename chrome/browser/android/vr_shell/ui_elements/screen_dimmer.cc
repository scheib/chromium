// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/vr_shell/ui_elements/screen_dimmer.h"

#include "chrome/browser/android/vr_shell/vr_shell_renderer.h"
#include "device/vr/vr_math.h"

namespace vr_shell {

ScreenDimmer::ScreenDimmer()
    : inner_color_({0.05f, 0.05f, 0.05f, 0.8f}),
      outer_color_({0, 0, 0, 0.9f}),
      opacity_(0.9f) {}

ScreenDimmer::~ScreenDimmer() = default;

void ScreenDimmer::Initialize() {
  set_fill(Fill::SELF);
}

void ScreenDimmer::Render(VrShellRenderer* renderer,
                          vr::Mat4f view_proj_matrix) const {
  vr::Mat4f m;
  vr::SetIdentityM(&m);
  vr::ScaleM(m, {2.0f, 2.0f, 1.0f}, &m);
  renderer->GetGradientQuadRenderer()->Draw(m, outer_color_, inner_color_,
                                            opacity_);
}

}  // namespace vr_shell
