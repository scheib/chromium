// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/client/input/keyboard_interpreter.h"

#include "base/logging.h"
#include "remoting/client/input/text_keyboard_input_strategy.h"

namespace remoting {

KeyboardInterpreter::KeyboardInterpreter(ClientInputInjector* input_injector) {
  // TODO(nicholss): This should be configurable.
  input_strategy_.reset(new TextKeyboardInputStrategy(input_injector));
}

KeyboardInterpreter::~KeyboardInterpreter() {}

void KeyboardInterpreter::HandleTextEvent(const std::string& text,
                                          uint8_t modifiers) {
  input_strategy_->HandleTextEvent(text, modifiers);
}

void KeyboardInterpreter::HandleDeleteEvent(uint8_t modifiers) {
  input_strategy_->HandleDeleteEvent(modifiers);
}

}  // namespace remoting
