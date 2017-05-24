// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/device_keyboard_handler.h"

#include "ash/new_window_controller.h"
#include "ash/shell.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chromeos/chromeos_switches.h"
#include "content/public/browser/web_ui.h"
#include "ui/events/devices/input_device_manager.h"

namespace {

bool HasExternalKeyboard() {
  for (const ui::InputDevice& keyboard :
       ui::InputDeviceManager::GetInstance()->GetKeyboardDevices()) {
    if (keyboard.type == ui::InputDeviceType::INPUT_DEVICE_EXTERNAL)
      return true;
  }

  return false;
}

}  // namespace

namespace chromeos {
namespace settings {

KeyboardHandler::KeyboardHandler(content::WebUI* webui)
    : profile_(Profile::FromWebUI(webui)), observer_(this) {}

KeyboardHandler::~KeyboardHandler() {
}

void KeyboardHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "initializeKeyboardSettings",
      base::Bind(&KeyboardHandler::HandleInitialize,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "showKeyboardShortcutsOverlay",
      base::Bind(&KeyboardHandler::HandleShowKeyboardShortcutsOverlay,
                 base::Unretained(this)));
}

void KeyboardHandler::OnJavascriptAllowed() {
  observer_.Add(ui::InputDeviceManager::GetInstance());
}

void KeyboardHandler::OnJavascriptDisallowed() {
  observer_.RemoveAll();
}

void KeyboardHandler::OnKeyboardDeviceConfigurationChanged() {
  UpdateShowKeys();
}

void KeyboardHandler::HandleInitialize(const base::ListValue* args) {
  AllowJavascript();
  UpdateShowKeys();
}

void KeyboardHandler::HandleShowKeyboardShortcutsOverlay(
    const base::ListValue* args) const {
  ash::Shell::Get()->new_window_controller()->ShowKeyboardOverlay();
}

void KeyboardHandler::UpdateShowKeys() {
  const base::Value has_caps_lock(HasExternalKeyboard());
  const base::Value has_diamond_key(
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          chromeos::switches::kHasChromeOSDiamondKey));
  FireWebUIListener("show-keys-changed", has_caps_lock, has_diamond_key);
}

}  // namespace settings
}  // namespace chromeos
