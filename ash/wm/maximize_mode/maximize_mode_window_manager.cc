// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/maximize_mode/maximize_mode_window_manager.h"

#include "ash/ash_switches.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/root_window_controller.h"
#include "ash/session/session_state_delegate.h"
#include "ash/shell.h"
#include "ash/shell_port.h"
#include "ash/wm/maximize_mode/maximize_mode_backdrop_delegate_impl.h"
#include "ash/wm/maximize_mode/maximize_mode_event_handler.h"
#include "ash/wm/maximize_mode/maximize_mode_window_state.h"
#include "ash/wm/mru_window_tracker.h"
#include "ash/wm/overview/window_selector_controller.h"
#include "ash/wm/window_state.h"
#include "ash/wm/wm_event.h"
#include "ash/wm/workspace_controller.h"
#include "ash/wm_window.h"
#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "base/stl_util.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/display/screen.h"

namespace ash {

namespace {

// Exits overview mode if it is currently active.
void CancelOverview() {
  WindowSelectorController* controller =
      Shell::Get()->window_selector_controller();
  if (controller->IsSelecting())
    controller->OnSelectionEnded();
}

}  // namespace

MaximizeModeWindowManager::~MaximizeModeWindowManager() {
  // Overview mode needs to be ended before exiting maximize mode to prevent
  // transforming windows which are currently in
  // overview: http://crbug.com/366605
  CancelOverview();
  for (aura::Window* window : added_windows_)
    window->RemoveObserver(this);
  added_windows_.clear();
  Shell::Get()->RemoveShellObserver(this);
  display::Screen::GetScreen()->RemoveObserver(this);
  EnableBackdropBehindTopWindowOnEachDisplay(false);
  RemoveWindowCreationObservers();
  RestoreAllWindows();
}

int MaximizeModeWindowManager::GetNumberOfManagedWindows() {
  return window_state_map_.size();
}

void MaximizeModeWindowManager::AddWindow(WmWindow* window) {
  // Only add the window if it is a direct dependent of a container window
  // and not yet tracked.
  if (!ShouldHandleWindow(window) ||
      base::ContainsKey(window_state_map_, window) ||
      !IsContainerWindow(window->GetParent()->aura_window())) {
    return;
  }

  MaximizeAndTrackWindow(window);
}

void MaximizeModeWindowManager::WindowStateDestroyed(WmWindow* window) {
  // At this time ForgetWindow() should already have been called. If not,
  // someone else must have replaced the "window manager's state object".
  DCHECK(!window->aura_window()->HasObserver(this));

  auto it = window_state_map_.find(window);
  DCHECK(it != window_state_map_.end());
  window_state_map_.erase(it);
}

void MaximizeModeWindowManager::OnOverviewModeStarting() {
  SetDeferBoundsUpdates(true);
}

void MaximizeModeWindowManager::OnOverviewModeEnded() {
  SetDeferBoundsUpdates(false);
}

void MaximizeModeWindowManager::OnWindowDestroying(aura::Window* window) {
  if (IsContainerWindow(window)) {
    // container window can be removed on display destruction.
    window->RemoveObserver(this);
    observed_container_windows_.erase(window);
  } else if (base::ContainsValue(added_windows_, window)) {
    // Added window was destroyed before being shown.
    added_windows_.erase(window);
    window->RemoveObserver(this);
  } else {
    // If a known window gets destroyed we need to remove all knowledge about
    // it.
    ForgetWindow(WmWindow::Get(window));
  }
}

void MaximizeModeWindowManager::OnWindowHierarchyChanged(
    const HierarchyChangeParams& params) {
  // A window can get removed and then re-added by a drag and drop operation.
  if (params.new_parent && IsContainerWindow(params.new_parent) &&
      !base::ContainsKey(window_state_map_, WmWindow::Get(params.target))) {
    // Don't register the window if the window is invisible. Instead,
    // wait until it becomes visible because the client may update the
    // flag to control if the window should be added.
    if (!params.target->IsVisible()) {
      if (!base::ContainsValue(added_windows_, params.target)) {
        added_windows_.insert(params.target);
        params.target->AddObserver(this);
      }
      return;
    }
    MaximizeAndTrackWindow(WmWindow::Get(params.target));
    // When the state got added, the "WM_EVENT_ADDED_TO_WORKSPACE" event got
    // already sent and we have to notify our state again.
    if (base::ContainsKey(window_state_map_, WmWindow::Get(params.target))) {
      wm::WMEvent event(wm::WM_EVENT_ADDED_TO_WORKSPACE);
      wm::GetWindowState(params.target)->OnWMEvent(&event);
    }
  }
}

void MaximizeModeWindowManager::OnWindowPropertyChanged(aura::Window* window,
                                                        const void* key,
                                                        intptr_t old) {
  // Stop managing |window| if the always-on-top property is added.
  if (key == aura::client::kAlwaysOnTopKey &&
      window->GetProperty(aura::client::kAlwaysOnTopKey)) {
    ForgetWindow(WmWindow::Get(window));
  }
}

void MaximizeModeWindowManager::OnWindowBoundsChanged(
    aura::Window* window,
    const gfx::Rect& old_bounds,
    const gfx::Rect& new_bounds) {
  if (!IsContainerWindow(window))
    return;
  // Reposition all non maximizeable windows.
  for (auto& pair : window_state_map_)
    pair.second->UpdateWindowPosition(pair.first->GetWindowState());
}

void MaximizeModeWindowManager::OnWindowVisibilityChanged(aura::Window* window,
                                                          bool visible) {
  // Skip if it's already managed.
  if (base::ContainsKey(window_state_map_, WmWindow::Get(window)))
    return;

  if (IsContainerWindow(window->parent()) &&
      base::ContainsValue(added_windows_, window) && visible) {
    added_windows_.erase(window);
    window->RemoveObserver(this);
    MaximizeAndTrackWindow(WmWindow::Get(window));
    // When the state got added, the "WM_EVENT_ADDED_TO_WORKSPACE" event got
    // already sent and we have to notify our state again.
    if (base::ContainsKey(window_state_map_, WmWindow::Get(window))) {
      wm::WMEvent event(wm::WM_EVENT_ADDED_TO_WORKSPACE);
      wm::GetWindowState(window)->OnWMEvent(&event);
    }
  }
}

void MaximizeModeWindowManager::OnDisplayAdded(
    const display::Display& display) {
  DisplayConfigurationChanged();
}

void MaximizeModeWindowManager::OnDisplayRemoved(
    const display::Display& display) {
  DisplayConfigurationChanged();
}

void MaximizeModeWindowManager::OnDisplayMetricsChanged(const display::Display&,
                                                        uint32_t) {
  // Nothing to do here.
}

void MaximizeModeWindowManager::SetIgnoreWmEventsForExit() {
  for (auto& pair : window_state_map_) {
    pair.second->set_ignore_wm_events(true);
  }
}

MaximizeModeWindowManager::MaximizeModeWindowManager() {
  // The overview mode needs to be ended before the maximize mode is started. To
  // guarantee the proper order, it will be turned off from here.
  CancelOverview();

  MaximizeAllWindows();
  AddWindowCreationObservers();
  EnableBackdropBehindTopWindowOnEachDisplay(true);
  display::Screen::GetScreen()->AddObserver(this);
  Shell::Get()->AddShellObserver(this);
  event_handler_ = ShellPort::Get()->CreateMaximizeModeEventHandler();
}

void MaximizeModeWindowManager::MaximizeAllWindows() {
  MruWindowTracker::WindowList windows =
      Shell::Get()->mru_window_tracker()->BuildWindowListIgnoreModal();
  // Add all existing Mru windows.
  for (WmWindow* window : windows)
    MaximizeAndTrackWindow(window);
}

void MaximizeModeWindowManager::RestoreAllWindows() {
  while (window_state_map_.size())
    ForgetWindow(window_state_map_.begin()->first);
}

void MaximizeModeWindowManager::SetDeferBoundsUpdates(
    bool defer_bounds_updates) {
  for (auto& pair : window_state_map_)
    pair.second->SetDeferBoundsUpdates(defer_bounds_updates);
}

void MaximizeModeWindowManager::MaximizeAndTrackWindow(WmWindow* window) {
  if (!ShouldHandleWindow(window))
    return;

  DCHECK(!base::ContainsKey(window_state_map_, window));
  window->aura_window()->AddObserver(this);

  // We create and remember a maximize mode state which will attach itself to
  // the provided state object.
  window_state_map_[window] = new MaximizeModeWindowState(window, this);
}

void MaximizeModeWindowManager::ForgetWindow(WmWindow* window) {
  WindowToState::iterator it = window_state_map_.find(window);

  // The following DCHECK could fail if our window state object was destroyed
  // earlier by someone else. However - at this point there is no other client
  // which replaces the state object and therefore this should not happen.
  DCHECK(it != window_state_map_.end());
  window->aura_window()->RemoveObserver(this);

  // By telling the state object to revert, it will switch back the old
  // State object and destroy itself, calling WindowStateDestroyed().
  it->second->LeaveMaximizeMode(it->first->GetWindowState());
  DCHECK(!base::ContainsKey(window_state_map_, window));
}

bool MaximizeModeWindowManager::ShouldHandleWindow(WmWindow* window) {
  DCHECK(window);

  // Windows with the always-on-top property should be free-floating and thus
  // not managed by us.
  if (window->IsAlwaysOnTop())
    return false;

  // If the changing bounds in the maximized/fullscreen is allowed, then
  // let the client manage it even in maximized mode.
  if (window->GetWindowState()->allow_set_bounds_direct())
    return false;

  return window->GetType() == ui::wm::WINDOW_TYPE_NORMAL;
}

void MaximizeModeWindowManager::AddWindowCreationObservers() {
  DCHECK(observed_container_windows_.empty());
  // Observe window activations/creations in the default containers on all root
  // windows.
  for (aura::Window* root : Shell::Get()->GetAllRootWindows()) {
    aura::Window* default_container =
        root->GetChildById(kShellWindowId_DefaultContainer);
    DCHECK(!base::ContainsKey(observed_container_windows_, default_container));
    default_container->AddObserver(this);
    observed_container_windows_.insert(default_container);
  }
}

void MaximizeModeWindowManager::RemoveWindowCreationObservers() {
  for (aura::Window* window : observed_container_windows_)
    window->RemoveObserver(this);
  observed_container_windows_.clear();
}

void MaximizeModeWindowManager::DisplayConfigurationChanged() {
  EnableBackdropBehindTopWindowOnEachDisplay(false);
  RemoveWindowCreationObservers();
  AddWindowCreationObservers();
  EnableBackdropBehindTopWindowOnEachDisplay(true);
}

bool MaximizeModeWindowManager::IsContainerWindow(aura::Window* window) {
  return base::ContainsKey(observed_container_windows_, window);
}

void MaximizeModeWindowManager::EnableBackdropBehindTopWindowOnEachDisplay(
    bool enable) {
  // Inform the WorkspaceLayoutManager that we want to show a backdrop behind
  // the topmost window of its container.
  for (WmWindow* root : ShellPort::Get()->GetAllRootWindows()) {
    RootWindowController* controller = root->GetRootWindowController();
    controller->workspace_controller()->SetBackdropDelegate(
        enable ? base::MakeUnique<MaximizeModeBackdropDelegateImpl>()
               : nullptr);
  }
}

}  // namespace ash
