// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/palette/palette_utils.h"

#include "ash/ash_switches.h"
#include "ash/palette_delegate.h"
#include "ash/shelf/wm_shelf.h"
#include "ash/shell.h"
#include "ash/system/palette/palette_tray.h"
#include "ash/system/status_area_widget.h"
#include "base/command_line.h"
#include "ui/display/display.h"
#include "ui/events/devices/input_device_manager.h"
#include "ui/events/devices/touchscreen_device.h"
#include "ui/gfx/geometry/point.h"

namespace ash {
namespace palette_utils {

bool HasStylusInput() {
  // Allow the user to force enable or disable by passing a switch. If both are
  // present, enabling takes precedence over disabling.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kAshForceEnableStylusTools)) {
    return true;
  }

  // Check to see if the hardware reports it is stylus capable.
  for (const ui::TouchscreenDevice& device :
       ui::InputDeviceManager::GetInstance()->GetTouchscreenDevices()) {
    if (device.is_stylus &&
        device.type == ui::InputDeviceType::INPUT_DEVICE_INTERNAL) {
      return true;
    }
  }

  return false;
}

bool IsPaletteEnabledOnEveryDisplay() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kAshEnablePaletteOnAllDisplays);
}

bool ShouldShowPalette() {
  return HasStylusInput() &&
         (display::Display::HasInternalDisplay() ||
          IsPaletteEnabledOnEveryDisplay()) &&
         Shell::Get()->palette_delegate() &&
         Shell::Get()->palette_delegate()->ShouldShowPalette();
}

bool PaletteContainsPointInScreen(const gfx::Point& point) {
  for (aura::Window* window : Shell::GetAllRootWindows()) {
    PaletteTray* palette_tray =
        WmShelf::ForWindow(window)->GetStatusAreaWidget()->palette_tray();
    if (palette_tray && palette_tray->ContainsPointInScreen(point))
      return true;
  }

  return false;
}

}  // namespace palette_utils
}  // namespace ash
