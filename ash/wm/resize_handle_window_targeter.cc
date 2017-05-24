// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/resize_handle_window_targeter.h"

#include "ash/ash_constants.h"
#include "ash/shared/immersive_fullscreen_controller.h"
#include "ash/wm/window_state.h"
#include "ui/aura/window.h"
#include "ui/events/event.h"

namespace ash {

ResizeHandleWindowTargeter::ResizeHandleWindowTargeter(
    aura::Window* window,
    ImmersiveFullscreenController* controller)
    : window_(window), immersive_controller_(controller) {
  wm::WindowState* window_state = wm::GetWindowState(window_);
  OnPostWindowStateTypeChange(window_state, wm::WINDOW_STATE_TYPE_DEFAULT);
  window_state->AddObserver(this);
  window_->AddObserver(this);
}

ResizeHandleWindowTargeter::~ResizeHandleWindowTargeter() {
  if (window_) {
    window_->RemoveObserver(this);
    wm::GetWindowState(window_)->RemoveObserver(this);
  }
}

void ResizeHandleWindowTargeter::OnPostWindowStateTypeChange(
    wm::WindowState* window_state,
    wm::WindowStateType old_type) {
  if (window_state->IsMaximizedOrFullscreenOrPinned()) {
    frame_border_inset_ = gfx::Insets();
  } else {
    frame_border_inset_ =
        gfx::Insets(kResizeInsideBoundsSize, kResizeInsideBoundsSize,
                    kResizeInsideBoundsSize, kResizeInsideBoundsSize);
  }
}

void ResizeHandleWindowTargeter::OnWindowDestroying(aura::Window* window) {
  CHECK_EQ(window_, window);
  wm::GetWindowState(window_)->RemoveObserver(this);
  window_ = NULL;
}

aura::Window* ResizeHandleWindowTargeter::FindTargetForLocatedEvent(
    aura::Window* window,
    ui::LocatedEvent* event) {
  if (window == window_) {
    gfx::Insets insets;
    if (immersive_controller_ && immersive_controller_->IsEnabled() &&
        !immersive_controller_->IsRevealed() && event->IsTouchEvent()) {
      // If the window is in immersive fullscreen, and top-of-window views are
      // not revealed, then touch events towards the top of the window
      // should not reach the child window so that touch gestures can be used to
      // reveal the top-of-windows views. This is needed because the child
      // window may consume touch events and prevent touch-scroll gesture from
      // being generated.
      insets = gfx::Insets(
          ImmersiveFullscreenController::kImmersiveFullscreenTopEdgeInset, 0, 0,
          0);
    } else {
      // If the event falls very close to the inside of the frame border, then
      // target the window itself, so that the window can be resized easily.
      insets = frame_border_inset_;
    }

    if (!insets.IsEmpty()) {
      gfx::Rect bounds = gfx::Rect(window_->bounds().size());
      bounds.Inset(insets);
      if (!bounds.Contains(event->location()))
        return window_;
    }
  }
  return aura::WindowTargeter::FindTargetForLocatedEvent(window, event);
}

bool ResizeHandleWindowTargeter::SubtreeShouldBeExploredForEvent(
    aura::Window* window,
    const ui::LocatedEvent& event) {
  if (window == window_) {
    // Defer to the parent's targeter on whether |window_| should be able to
    // receive the event.
    ui::EventTarget* parent =
        static_cast<ui::EventTarget*>(window)->GetParentTarget();
    if (parent) {
      aura::WindowTargeter* targeter =
          static_cast<aura::WindowTargeter*>(parent->GetEventTargeter());
      if (targeter)
        return targeter->SubtreeShouldBeExploredForEvent(window, event);
    }
  }
  return aura::WindowTargeter::SubtreeShouldBeExploredForEvent(window, event);
}

}  // namespace ash
