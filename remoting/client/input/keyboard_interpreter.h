// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_INPUT_KEYBOARD_INTERPRETER_H_
#define REMOTING_CLIENT_INPUT_KEYBOARD_INTERPRETER_H_

#include <memory>
#include <string>

#include "base/macros.h"

namespace remoting {

class KeyboardInputStrategy;
class ClientInputInjector;

// This is a class for interpreting raw keyboard input, it will delegate
// handling of text events to the selected keyboard input strategy.
class KeyboardInterpreter {
 public:
  explicit KeyboardInterpreter(ClientInputInjector* input_injector);
  ~KeyboardInterpreter();

  // Delegates to |KeyboardInputStrategy| to covert and send the input.
  void HandleTextEvent(const std::string& text, uint8_t modifiers);
  // Delegates to |KeyboardInputStrategy| to covert and send the delete.
  void HandleDeleteEvent(uint8_t modifiers);

 private:
  std::unique_ptr<KeyboardInputStrategy> input_strategy_;

  DISALLOW_COPY_AND_ASSIGN(KeyboardInterpreter);
};

}  // namespace remoting
#endif  // REMOTING_CLIENT_INPUT_KEYBOARD_INTERPRETER_H_
