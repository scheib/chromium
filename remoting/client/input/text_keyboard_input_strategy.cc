// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/client/input/text_keyboard_input_strategy.h"

#include "remoting/client/input/client_input_injector.h"
#include "remoting/client/native_device_keymap.h"
#include "ui/events/keycodes/dom/dom_code.h"

namespace remoting {

TextKeyboardInputStrategy::TextKeyboardInputStrategy(
    ClientInputInjector* input_injector)
    : input_injector_(input_injector) {}

TextKeyboardInputStrategy::~TextKeyboardInputStrategy() {}

// KeyboardInputStrategy

void TextKeyboardInputStrategy::HandleTextEvent(const std::string& text,
                                                uint8_t modifiers) {
  // TODO(nicholss): Handle modifers.
  input_injector_->SendTextEvent(text);
}

void TextKeyboardInputStrategy::HandleDeleteEvent(uint8_t modifiers) {
  std::queue<KeyEvent> keys = ConvertDeleteEvent(modifiers);
  while (!keys.empty()) {
    KeyEvent key = keys.front();
    input_injector_->SendKeyEvent(0, key.keycode, key.keydown);
    keys.pop();
  }
}

std::queue<KeyEvent> TextKeyboardInputStrategy::ConvertDeleteEvent(
    uint8_t modifiers) {
  std::queue<KeyEvent> keys;
  // TODO(nicholss): Handle modifers.
  // Key press.
  keys.push({static_cast<uint32_t>(ui::DomCode::BACKSPACE), true});

  // Key release.
  keys.push({static_cast<uint32_t>(ui::DomCode::BACKSPACE), false});

  return keys;
}

}  // namespace remoting
