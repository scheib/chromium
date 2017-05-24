// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_UI_GESTURE_INTERPRETER_H_
#define REMOTING_CLIENT_UI_GESTURE_INTERPRETER_H_

#include <memory>

#include "remoting/client/ui/desktop_viewport.h"
#include "remoting/client/ui/fling_animation.h"
#include "remoting/client/ui/input_strategy.h"
#include "remoting/proto/event.pb.h"

namespace remoting {

class ChromotingSession;
class RendererProxy;

// This is a class for interpreting a raw touch input into actions like moving
// the viewport and injecting mouse clicks.
class GestureInterpreter {
 public:
  enum GestureState { GESTURE_BEGAN, GESTURE_CHANGED, GESTURE_ENDED };

  enum InputMode {
    UNDEFINED_INPUT_MODE,
    DIRECT_INPUT_MODE,
    TRACKPAD_INPUT_MODE
  };

  GestureInterpreter(RendererProxy* renderer, ChromotingSession* input_stub);
  ~GestureInterpreter();

  // Must be called right after the renderer is ready.
  void SetInputMode(InputMode mode);

  // Returns the current input mode.
  InputMode GetInputMode() const;

  // Coordinates of the OpenGL view surface will be used.

  // Called during a two-finger pinching gesture. This can happen in conjunction
  // with Pan().
  void Zoom(float pivot_x, float pivot_y, float scale, GestureState state);

  // Called whenever the user did a pan gesture. It can be one-finger pan, no
  // matter dragging in on or not, or two-finger pan in conjunction with zoom.
  // Two-finger pan without zoom is consider a scroll gesture.
  void Pan(float translation_x, float translation_y);

  // Called when the user did a one-finger tap.
  void Tap(float x, float y);

  void TwoFingerTap(float x, float y);

  // Caller is expected to call both Pan() and Drag() when dragging is in
  // progress.
  void Drag(float x, float y, GestureState state);

  // Called when the user has just done a one-finger pan (no dragging or
  // zooming) and the pan gesture still has some final velocity.
  void OneFingerFling(float velocity_x, float velocity_y);

  // Called during a two-finger scroll (panning without zooming) gesture.
  void Scroll(float x, float y, float dx, float dy);

  // Called when the user has just done a scroll gesture and the scroll gesture
  // still has some final velocity.
  void ScrollWithVelocity(float velocity_x, float velocity_y);

  // Called to process one animation frame.
  void ProcessAnimations();

  void OnSurfaceSizeChanged(int width, int height);
  void OnDesktopSizeChanged(int width, int height);

 private:
  void PanWithoutAbortAnimations(float translation_x, float translation_y);

  void ScrollWithoutAbortAnimations(float dx, float dy);

  void AbortAnimations();

  void InjectMouseClick(float x,
                        float y,
                        protocol::MouseEvent_MouseButton button);

  void InjectCursorPosition(float x, float y);

  void SetGestureInProgress(InputStrategy::Gesture gesture,
                            bool is_in_progress);

  // Tracks the touch point and gets back the cursor position from the input
  // strategy.
  ViewMatrix::Point TrackAndGetPosition(float touch_x, float touch_y);

  // Starts the given feedback at (cursor_x, cursor_y) if the feedback radius
  // is non-zero.
  void StartInputFeedback(float cursor_x,
                          float cursor_y,
                          InputStrategy::InputFeedbackType feedback_type);

  InputMode input_mode_ = UNDEFINED_INPUT_MODE;
  std::unique_ptr<InputStrategy> input_strategy_;
  DesktopViewport viewport_;
  RendererProxy* renderer_;
  ChromotingSession* input_stub_;
  InputStrategy::Gesture gesture_in_progress_;

  FlingAnimation pan_animation_;
  FlingAnimation scroll_animation_;

  // GestureInterpreter is neither copyable nor movable.
  GestureInterpreter(const GestureInterpreter&) = delete;
  GestureInterpreter& operator=(const GestureInterpreter&) = delete;
};

}  // namespace remoting
#endif  // REMOTING_CLIENT_UI_GESTURE_INTERPRETER_H_
