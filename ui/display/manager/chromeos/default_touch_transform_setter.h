// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_DISPLAY_MANAGER_CHROMEOS_DEFAULT_TOUCH_TRANSFORM_SETTER_H_
#define UI_DISPLAY_MANAGER_CHROMEOS_DEFAULT_TOUCH_TRANSFORM_SETTER_H_

#include "base/macros.h"
#include "ui/display/manager/chromeos/touch_transform_setter.h"

namespace display {

class DISPLAY_MANAGER_EXPORT DefaultTouchTransformSetter
    : public TouchTransformSetter {
 public:
  DefaultTouchTransformSetter();
  ~DefaultTouchTransformSetter() override;

  // TouchTransformSetter:
  void ConfigureTouchDevices(
      const std::map<int32_t, double>& scales,
      const std::vector<TouchDeviceTransform>& transforms) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(DefaultTouchTransformSetter);
};

}  // namespace display

#endif  // UI_DISPLAY_MANAGER_CHROMEOS_DEFAULT_TOUCH_TRANSFORM_SETTER_H_
