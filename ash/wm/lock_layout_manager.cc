// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/lock_layout_manager.h"

#include "ash/keyboard/keyboard_observer_register.h"
#include "ash/shell.h"
#include "ash/wm/lock_window_state.h"
#include "ash/wm/window_state.h"
#include "ash/wm/wm_event.h"
#include "ash/wm_window.h"
#include "ui/events/event.h"
#include "ui/keyboard/keyboard_controller.h"
#include "ui/keyboard/keyboard_util.h"

namespace ash {

LockLayoutManager::LockLayoutManager(aura::Window* window)
    : wm::WmSnapToPixelLayoutManager(),
      window_(window),
      root_window_(window->GetRootWindow()),
      keyboard_observer_(this) {
  Shell::Get()->AddShellObserver(this);
  root_window_->AddObserver(this);
  if (keyboard::KeyboardController::GetInstance())
    keyboard_observer_.Add(keyboard::KeyboardController::GetInstance());
}

LockLayoutManager::~LockLayoutManager() {
  if (root_window_)
    root_window_->RemoveObserver(this);

  for (aura::Window* child : window_->children())
    child->RemoveObserver(this);

  Shell::Get()->RemoveShellObserver(this);
}

void LockLayoutManager::OnWindowResized() {
  const wm::WMEvent event(wm::WM_EVENT_WORKAREA_BOUNDS_CHANGED);
  AdjustWindowsForWorkAreaChange(&event);
}

void LockLayoutManager::OnWindowAddedToLayout(aura::Window* child) {
  child->AddObserver(this);

  // LockWindowState replaces default WindowState of a child.
  wm::WindowState* window_state =
      LockWindowState::SetLockWindowState(WmWindow::Get(child));
  wm::WMEvent event(wm::WM_EVENT_ADDED_TO_WORKSPACE);
  window_state->OnWMEvent(&event);
}

void LockLayoutManager::OnWillRemoveWindowFromLayout(aura::Window* child) {
  child->RemoveObserver(this);
}

void LockLayoutManager::OnWindowRemovedFromLayout(aura::Window* child) {}

void LockLayoutManager::OnChildWindowVisibilityChanged(aura::Window* child,
                                                       bool visible) {}

void LockLayoutManager::SetChildBounds(aura::Window* child,
                                       const gfx::Rect& requested_bounds) {
  wm::WindowState* window_state = wm::GetWindowState(child);
  wm::SetBoundsEvent event(wm::WM_EVENT_SET_BOUNDS, requested_bounds);
  window_state->OnWMEvent(&event);
}

void LockLayoutManager::OnWindowDestroying(aura::Window* window) {
  window->RemoveObserver(this);
  if (root_window_ == window)
    root_window_ = nullptr;
}

void LockLayoutManager::OnWindowBoundsChanged(aura::Window* window,
                                              const gfx::Rect& old_bounds,
                                              const gfx::Rect& new_bounds) {
  if (root_window_ == window) {
    const wm::WMEvent wm_event(wm::WM_EVENT_DISPLAY_BOUNDS_CHANGED);
    AdjustWindowsForWorkAreaChange(&wm_event);
  }
}

void LockLayoutManager::OnVirtualKeyboardStateChanged(
    bool activated,
    aura::Window* root_window) {
  UpdateKeyboardObserverFromStateChanged(activated, root_window, root_window_,
                                         &keyboard_observer_);
}

void LockLayoutManager::OnKeyboardBoundsChanging(const gfx::Rect& new_bounds) {
  keyboard_bounds_ = new_bounds;
  OnWindowResized();
}

void LockLayoutManager::OnKeyboardClosed() {
  keyboard_observer_.RemoveAll();
}

void LockLayoutManager::AdjustWindowsForWorkAreaChange(
    const wm::WMEvent* event) {
  DCHECK(event->type() == wm::WM_EVENT_DISPLAY_BOUNDS_CHANGED ||
         event->type() == wm::WM_EVENT_WORKAREA_BOUNDS_CHANGED);

  for (aura::Window* child : window_->children())
    wm::GetWindowState(child)->OnWMEvent(event);
}

}  // namespace ash
