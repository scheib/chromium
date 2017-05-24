// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/display/manager/chromeos/default_touch_transform_setter.h"

#include "ui/display/manager/chromeos/touch_device_transform.h"
#include "ui/events/devices/device_data_manager.h"

namespace display {

DefaultTouchTransformSetter::DefaultTouchTransformSetter() = default;

DefaultTouchTransformSetter::~DefaultTouchTransformSetter() = default;

void DefaultTouchTransformSetter::ConfigureTouchDevices(
    const std::map<int32_t, double>& scales,
    const std::vector<TouchDeviceTransform>& transforms) {
  ui::DeviceDataManager* device_manager = ui::DeviceDataManager::GetInstance();
  device_manager->ClearTouchDeviceAssociations();
  for (auto& device_scale_pair : scales) {
    device_manager->UpdateTouchRadiusScale(device_scale_pair.first,
                                           device_scale_pair.second);
  }
  for (const TouchDeviceTransform& transform : transforms) {
    device_manager->UpdateTouchInfoForDisplay(
        transform.display_id, transform.device_id, transform.transform);
  }
}

}  // namespace display
