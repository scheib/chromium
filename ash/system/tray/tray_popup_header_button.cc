// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/tray/tray_popup_header_button.h"

#include "ash/ash_constants.h"
#include "ash/system/tray/tray_constants.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/background.h"
#include "ui/views/painter.h"

namespace ash {

namespace {

const gfx::ImageSkia* GetImageForResourceId(int resource_id) {
  ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
  return bundle.GetImageNamed(resource_id).ToImageSkia();
}

}  // namespace

// static
const char TrayPopupHeaderButton::kViewClassName[] =
    "tray/TrayPopupHeaderButton";

TrayPopupHeaderButton::TrayPopupHeaderButton(views::ButtonListener* listener,
                                             const gfx::ImageSkia& icon,
                                             int accessible_name_id)
    : views::ToggleImageButton(listener) {
  Initialize(icon, accessible_name_id);
}

TrayPopupHeaderButton::TrayPopupHeaderButton(views::ButtonListener* listener,
                                             int enabled_resource_id,
                                             int disabled_resource_id,
                                             int enabled_resource_id_hover,
                                             int disabled_resource_id_hover,
                                             int accessible_name_id)
    : views::ToggleImageButton(listener) {
  Initialize(*GetImageForResourceId(enabled_resource_id), accessible_name_id);
  ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
  SetToggledImage(views::Button::STATE_NORMAL,
                  bundle.GetImageNamed(disabled_resource_id).ToImageSkia());
  SetImage(views::Button::STATE_HOVERED,
           *bundle.GetImageNamed(enabled_resource_id_hover).ToImageSkia());
  SetToggledImage(
      views::Button::STATE_HOVERED,
      bundle.GetImageNamed(disabled_resource_id_hover).ToImageSkia());
}

TrayPopupHeaderButton::~TrayPopupHeaderButton() {}

const char* TrayPopupHeaderButton::GetClassName() const {
  return kViewClassName;
}

gfx::Size TrayPopupHeaderButton::CalculatePreferredSize() const {
  return gfx::Size(kTrayPopupItemMinHeight, kTrayPopupItemMinHeight);
}

void TrayPopupHeaderButton::StateChanged(ButtonState old_state) {
  if (state() == STATE_HOVERED || state() == STATE_PRESSED) {
    set_background(views::Background::CreateSolidBackground(
        kTrayPopupHoverBackgroundColor));
  } else {
    set_background(nullptr);
  }
  SchedulePaint();
}

void TrayPopupHeaderButton::Initialize(const gfx::ImageSkia& icon,
                                       int accessible_name_id) {
  ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
  SetImage(views::Button::STATE_NORMAL, icon);
  SetImageAlignment(views::ImageButton::ALIGN_CENTER,
                    views::ImageButton::ALIGN_MIDDLE);
  SetAccessibleName(bundle.GetLocalizedString(accessible_name_id));
  SetFocusForPlatform();

  SetFocusPainter(views::Painter::CreateSolidFocusPainter(
      kFocusBorderColor, gfx::Insets(1, 2, 2, 3)));
}

}  // namespace ash
