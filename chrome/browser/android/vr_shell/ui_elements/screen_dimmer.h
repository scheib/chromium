// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_UI_ELEMENTS_SCREEN_DIMMER_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_UI_ELEMENTS_SCREEN_DIMMER_H_

#include "base/macros.h"
#include "chrome/browser/android/vr_shell/ui_elements/ui_element.h"
#include "device/vr/vr_types.h"

namespace vr_shell {

class ScreenDimmer : public UiElement {
 public:
  ScreenDimmer();
  ~ScreenDimmer() override;

  void Initialize() final;

  // UiElement interface.
  void Render(VrShellRenderer* renderer,
              vr::Mat4f view_proj_matrix) const final;

 private:
  vr::Colorf inner_color_;
  vr::Colorf outer_color_;
  float opacity_;

  DISALLOW_COPY_AND_ASSIGN(ScreenDimmer);
};

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_UI_ELEMENTS_SCREEN_DIMMER_H_
