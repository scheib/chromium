// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_DISPLAY_MANAGER_CHROMEOS_TOUCH_DEVICE_TRANSFORM_H_
#define UI_DISPLAY_MANAGER_CHROMEOS_TOUCH_DEVICE_TRANSFORM_H_

#include <stdint.h>

#include "ui/display/manager/display_manager_export.h"
#include "ui/gfx/transform.h"

namespace display {

struct DISPLAY_MANAGER_EXPORT TouchDeviceTransform {
  TouchDeviceTransform();
  ~TouchDeviceTransform();

  int64_t display_id;
  int32_t device_id;
  gfx::Transform transform;
};

}  // namespace display

#endif  // UI_DISPLAY_MANAGER_CHROMEOS_TOUCH_DEVICE_TRANSFORM_H_
