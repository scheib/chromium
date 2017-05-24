// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/default_state.h"

#include "ash/public/cpp/shell_window_ids.h"
#include "ash/root_window_controller.h"
#include "ash/screen_util.h"
#include "ash/shell.h"
#include "ash/shell_port.h"
#include "ash/wm/screen_pinning_controller.h"
#include "ash/wm/window_animation_types.h"
#include "ash/wm/window_parenting_utils.h"
#include "ash/wm/window_positioning_utils.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_state_delegate.h"
#include "ash/wm/window_state_util.h"
#include "ash/wm/wm_event.h"
#include "ash/wm_window.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"

namespace ash {
namespace wm {
namespace {

// This specifies how much percent (30%) of a window rect
// must be visible when the window is added to the workspace.
const float kMinimumPercentOnScreenArea = 0.3f;

// When a window that has restore bounds at least as large as a work area is
// unmaximized, inset the bounds slightly so that they are not exactly the same.
// This makes it easier to resize the window.
const int kMaximizedWindowInset = 10;  // DIPs.

bool IsMinimizedWindowState(const WindowStateType state_type) {
  return state_type == WINDOW_STATE_TYPE_MINIMIZED;
}

void MoveToDisplayForRestore(WindowState* window_state) {
  if (!window_state->HasRestoreBounds())
    return;
  const gfx::Rect restore_bounds = window_state->GetRestoreBoundsInScreen();

  // Move only if the restore bounds is outside of
  // the display. There is no information about in which
  // display it should be restored, so this is best guess.
  // TODO(oshima): Restore information should contain the
  // work area information like WindowResizer does for the
  // last window location.
  gfx::Rect display_area =
      window_state->window()->GetDisplayNearestWindow().bounds();

  if (!display_area.Intersects(restore_bounds)) {
    const display::Display& display =
        display::Screen::GetScreen()->GetDisplayMatching(restore_bounds);
    RootWindowController* new_root_controller =
        Shell::Get()->GetRootWindowControllerWithDisplayId(display.id());
    if (new_root_controller->GetRootWindow() !=
        window_state->window()->GetRootWindow()->aura_window()) {
      aura::Window* new_container =
          new_root_controller->GetRootWindow()->GetChildById(
              window_state->window()->GetParent()->aura_window()->id());
      new_container->AddChild(window_state->window()->aura_window());
    }
  }
}

void CycleSnap(WindowState* window_state, WMEventType event) {
  wm::WindowStateType desired_snap_state =
      event == WM_EVENT_CYCLE_SNAP_LEFT ? wm::WINDOW_STATE_TYPE_LEFT_SNAPPED
                                        : wm::WINDOW_STATE_TYPE_RIGHT_SNAPPED;

  if (window_state->CanSnap() &&
      window_state->GetStateType() != desired_snap_state &&
      window_state->window()->GetType() != ui::wm::WINDOW_TYPE_PANEL) {
    const wm::WMEvent event(desired_snap_state ==
                                    wm::WINDOW_STATE_TYPE_LEFT_SNAPPED
                                ? wm::WM_EVENT_SNAP_LEFT
                                : wm::WM_EVENT_SNAP_RIGHT);
    window_state->OnWMEvent(&event);
    return;
  }

  if (window_state->IsSnapped()) {
    window_state->Restore();
    return;
  }
  window_state->window()->Animate(::wm::WINDOW_ANIMATION_TYPE_BOUNCE);
}

}  // namespace

DefaultState::DefaultState(WindowStateType initial_state_type)
    : state_type_(initial_state_type), stored_window_state_(nullptr) {}
DefaultState::~DefaultState() {}

void DefaultState::OnWMEvent(WindowState* window_state, const WMEvent* event) {
  if (ProcessWorkspaceEvents(window_state, event))
    return;

  // Do not change the PINNED window state if this is not unpin event.
  if (window_state->IsTrustedPinned() && event->type() != WM_EVENT_NORMAL)
    return;

  if (ProcessCompoundEvents(window_state, event))
    return;

  WindowStateType current_state_type = window_state->GetStateType();
  WindowStateType next_state_type = WINDOW_STATE_TYPE_NORMAL;
  switch (event->type()) {
    case WM_EVENT_NORMAL:
      next_state_type = WINDOW_STATE_TYPE_NORMAL;
      break;
    case WM_EVENT_MAXIMIZE:
      next_state_type = WINDOW_STATE_TYPE_MAXIMIZED;
      break;
    case WM_EVENT_MINIMIZE:
      next_state_type = WINDOW_STATE_TYPE_MINIMIZED;
      break;
    case WM_EVENT_FULLSCREEN:
      next_state_type = WINDOW_STATE_TYPE_FULLSCREEN;
      break;
    case WM_EVENT_SNAP_LEFT:
      next_state_type = WINDOW_STATE_TYPE_LEFT_SNAPPED;
      break;
    case WM_EVENT_SNAP_RIGHT:
      next_state_type = WINDOW_STATE_TYPE_RIGHT_SNAPPED;
      break;
    case WM_EVENT_SET_BOUNDS:
      SetBounds(window_state, static_cast<const SetBoundsEvent*>(event));
      return;
    case WM_EVENT_SHOW_INACTIVE:
      next_state_type = WINDOW_STATE_TYPE_INACTIVE;
      break;
    case WM_EVENT_PIN:
    case WM_EVENT_TRUSTED_PIN:
      // If there already is a pinned window, it is not allowed to set it
      // to this window.
      // TODO(hidehiko): If a system modal window is openening, the pinning
      // probably should fail.
      if (Shell::Get()->screen_pinning_controller()->IsPinned()) {
        LOG(ERROR) << "An PIN event will be failed since another window is "
                   << "already in pinned mode.";
        next_state_type = current_state_type;
      } else {
        next_state_type = event->type() == WM_EVENT_PIN
                              ? WINDOW_STATE_TYPE_PINNED
                              : WINDOW_STATE_TYPE_TRUSTED_PINNED;
      }
      break;
    case WM_EVENT_TOGGLE_MAXIMIZE_CAPTION:
    case WM_EVENT_TOGGLE_MAXIMIZE:
    case WM_EVENT_TOGGLE_VERTICAL_MAXIMIZE:
    case WM_EVENT_TOGGLE_HORIZONTAL_MAXIMIZE:
    case WM_EVENT_TOGGLE_FULLSCREEN:
    case WM_EVENT_CYCLE_SNAP_LEFT:
    case WM_EVENT_CYCLE_SNAP_RIGHT:
    case WM_EVENT_CENTER:
      NOTREACHED() << "Compound event should not reach here:" << event;
      return;
    case WM_EVENT_ADDED_TO_WORKSPACE:
    case WM_EVENT_WORKAREA_BOUNDS_CHANGED:
    case WM_EVENT_DISPLAY_BOUNDS_CHANGED:
      NOTREACHED() << "Workspace event should not reach here:" << event;
      return;
  }

  if (next_state_type == current_state_type && window_state->IsSnapped()) {
    aura::Window* window = window_state->window()->aura_window();
    gfx::Rect snapped_bounds =
        event->type() == WM_EVENT_SNAP_LEFT
            ? GetDefaultLeftSnappedWindowBoundsInParent(window)
            : GetDefaultRightSnappedWindowBoundsInParent(window);
    window_state->SetBoundsDirectAnimated(snapped_bounds);
    return;
  }

  if (event->type() == WM_EVENT_SNAP_LEFT ||
      event->type() == WM_EVENT_SNAP_RIGHT) {
    window_state->set_bounds_changed_by_user(true);
  }

  EnterToNextState(window_state, next_state_type);
}

WindowStateType DefaultState::GetType() const {
  return state_type_;
}

void DefaultState::AttachState(WindowState* window_state,
                               WindowState::State* state_in_previous_mode) {
  DCHECK_EQ(stored_window_state_, window_state);

  ReenterToCurrentState(window_state, state_in_previous_mode);

  // If the display has changed while in the another mode,
  // we need to let windows know the change.
  display::Display current_display =
      window_state->window()->GetDisplayNearestWindow();
  if (stored_display_state_.bounds() != current_display.bounds()) {
    const WMEvent event(wm::WM_EVENT_DISPLAY_BOUNDS_CHANGED);
    window_state->OnWMEvent(&event);
  } else if (stored_display_state_.work_area() != current_display.work_area()) {
    const WMEvent event(wm::WM_EVENT_WORKAREA_BOUNDS_CHANGED);
    window_state->OnWMEvent(&event);
  }
}

void DefaultState::DetachState(WindowState* window_state) {
  stored_window_state_ = window_state;
  stored_bounds_ = window_state->window()->GetBounds();
  stored_restore_bounds_ = window_state->HasRestoreBounds()
                               ? window_state->GetRestoreBoundsInParent()
                               : gfx::Rect();
  // Remember the display state so that in case of the display change
  // while in the other mode, we can perform necessary action to
  // restore the window state to the proper state for the current
  // display.
  stored_display_state_ = window_state->window()->GetDisplayNearestWindow();
}

// static
bool DefaultState::ProcessCompoundEvents(WindowState* window_state,
                                         const WMEvent* event) {
  WmWindow* window = window_state->window();

  switch (event->type()) {
    case WM_EVENT_TOGGLE_MAXIMIZE_CAPTION:
      if (window_state->IsFullscreen()) {
        const wm::WMEvent event(wm::WM_EVENT_TOGGLE_FULLSCREEN);
        window_state->OnWMEvent(&event);
      } else if (window_state->IsMaximized()) {
        window_state->Restore();
      } else if (window_state->IsNormalOrSnapped()) {
        if (window_state->CanMaximize())
          window_state->Maximize();
      }
      return true;
    case WM_EVENT_TOGGLE_MAXIMIZE:
      if (window_state->IsFullscreen()) {
        const wm::WMEvent event(wm::WM_EVENT_TOGGLE_FULLSCREEN);
        window_state->OnWMEvent(&event);
      } else if (window_state->IsMaximized()) {
        window_state->Restore();
      } else if (window_state->CanMaximize()) {
        window_state->Maximize();
      }
      return true;
    case WM_EVENT_TOGGLE_VERTICAL_MAXIMIZE: {
      gfx::Rect work_area =
          ScreenUtil::GetDisplayWorkAreaBoundsInParent(window->aura_window());

      // Maximize vertically if:
      // - The window does not have a max height defined.
      // - The window has the normal state type. Snapped windows are excluded
      //   because they are already maximized vertically and reverting to the
      //   restored bounds looks weird.
      if (window->GetMaximumSize().height() != 0 ||
          !window_state->IsNormalStateType()) {
        return true;
      }
      if (window_state->HasRestoreBounds() &&
          (window->GetBounds().height() == work_area.height() &&
           window->GetBounds().y() == work_area.y())) {
        window_state->SetAndClearRestoreBounds();
      } else {
        window_state->SaveCurrentBoundsForRestore();
        window->SetBounds(gfx::Rect(window->GetBounds().x(), work_area.y(),
                                    window->GetBounds().width(),
                                    work_area.height()));
      }
      return true;
    }
    case WM_EVENT_TOGGLE_HORIZONTAL_MAXIMIZE: {
      // Maximize horizontally if:
      // - The window does not have a max width defined.
      // - The window is snapped or has the normal state type.
      if (window->GetMaximumSize().width() != 0)
        return true;
      if (!window_state->IsNormalOrSnapped())
        return true;
      gfx::Rect work_area =
          ScreenUtil::GetDisplayWorkAreaBoundsInParent(window->aura_window());
      if (window_state->IsNormalStateType() &&
          window_state->HasRestoreBounds() &&
          (window->GetBounds().width() == work_area.width() &&
           window->GetBounds().x() == work_area.x())) {
        window_state->SetAndClearRestoreBounds();
      } else {
        gfx::Rect new_bounds(work_area.x(), window->GetBounds().y(),
                             work_area.width(), window->GetBounds().height());

        gfx::Rect restore_bounds = window->GetBounds();
        if (window_state->IsSnapped()) {
          window_state->SetRestoreBoundsInParent(new_bounds);
          window_state->Restore();

          // The restore logic prevents a window from being restored to bounds
          // which match the workspace bounds exactly so it is necessary to set
          // the bounds again below.
        }

        window_state->SetRestoreBoundsInParent(restore_bounds);
        window->SetBounds(new_bounds);
      }
      return true;
    }
    case WM_EVENT_TOGGLE_FULLSCREEN:
      ToggleFullScreen(window_state, window_state->delegate());
      return true;
    case WM_EVENT_CYCLE_SNAP_LEFT:
    case WM_EVENT_CYCLE_SNAP_RIGHT:
      CycleSnap(window_state, event->type());
      return true;
    case WM_EVENT_CENTER:
      CenterWindow(window_state);
      return true;
    case WM_EVENT_NORMAL:
    case WM_EVENT_MAXIMIZE:
    case WM_EVENT_MINIMIZE:
    case WM_EVENT_FULLSCREEN:
    case WM_EVENT_PIN:
    case WM_EVENT_TRUSTED_PIN:
    case WM_EVENT_SNAP_LEFT:
    case WM_EVENT_SNAP_RIGHT:
    case WM_EVENT_SET_BOUNDS:
    case WM_EVENT_SHOW_INACTIVE:
      break;
    case WM_EVENT_ADDED_TO_WORKSPACE:
    case WM_EVENT_WORKAREA_BOUNDS_CHANGED:
    case WM_EVENT_DISPLAY_BOUNDS_CHANGED:
      NOTREACHED() << "Workspace event should not reach here:" << event;
      break;
  }
  return false;
}

bool DefaultState::ProcessWorkspaceEvents(WindowState* window_state,
                                          const WMEvent* event) {
  switch (event->type()) {
    case WM_EVENT_ADDED_TO_WORKSPACE: {
      // When a window is dragged and dropped onto a different
      // root window, the bounds will be updated after they are added
      // to the root window.
      // If a window is opened as maximized or fullscreen, its bounds may be
      // empty, so update the bounds now before checking empty.
      if (window_state->is_dragged() ||
          window_state->allow_set_bounds_direct() ||
          SetMaximizedOrFullscreenBounds(window_state)) {
        return true;
      }

      WmWindow* window = window_state->window();
      gfx::Rect bounds = window->GetBounds();

      // Don't adjust window bounds if the bounds are empty as this
      // happens when a new views::Widget is created.
      if (bounds.IsEmpty())
        return true;

      // Only windows of type WINDOW_TYPE_NORMAL or WINDOW_TYPE_PANEL need to be
      // adjusted to have minimum visibility, because they are positioned by the
      // user and user should always be able to interact with them. Other
      // windows are positioned programmatically.
      if (!window_state->IsUserPositionable())
        return true;

      // Use entire display instead of workarea. The logic ensures 30%
      // visibility which should be enough to see where the window gets
      // moved.
      gfx::Rect display_area =
          ScreenUtil::GetDisplayBoundsInParent(window->aura_window());
      int min_width = bounds.width() * wm::kMinimumPercentOnScreenArea;
      int min_height = bounds.height() * wm::kMinimumPercentOnScreenArea;
      wm::AdjustBoundsToEnsureWindowVisibility(display_area, min_width,
                                               min_height, &bounds);
      window_state->AdjustSnappedBounds(&bounds);
      if (window->GetBounds() != bounds)
        window_state->SetBoundsConstrained(bounds);
      return true;
    }
    case WM_EVENT_DISPLAY_BOUNDS_CHANGED: {
      if (window_state->is_dragged() ||
          window_state->allow_set_bounds_direct() ||
          SetMaximizedOrFullscreenBounds(window_state)) {
        return true;
      }
      gfx::Rect work_area_in_parent =
          ScreenUtil::GetDisplayWorkAreaBoundsInParent(
              window_state->window()->aura_window());
      gfx::Rect bounds = window_state->window()->GetTargetBounds();
      // When display bounds has changed, make sure the entire window is fully
      // visible.
      bounds.AdjustToFit(work_area_in_parent);
      window_state->AdjustSnappedBounds(&bounds);
      if (window_state->window()->GetTargetBounds() != bounds)
        window_state->SetBoundsDirectAnimated(bounds);
      return true;
    }
    case WM_EVENT_WORKAREA_BOUNDS_CHANGED: {
      // Don't resize the maximized window when the desktop is covered
      // by fullscreen window. crbug.com/504299.
      bool in_fullscreen =
          window_state->window()
              ->GetRootWindowController()
              ->GetWorkspaceWindowState() == WORKSPACE_WINDOW_STATE_FULL_SCREEN;
      if (in_fullscreen && window_state->IsMaximized())
        return true;

      if (window_state->is_dragged() ||
          window_state->allow_set_bounds_direct() ||
          SetMaximizedOrFullscreenBounds(window_state)) {
        return true;
      }
      gfx::Rect work_area_in_parent =
          ScreenUtil::GetDisplayWorkAreaBoundsInParent(
              window_state->window()->aura_window());
      gfx::Rect bounds = window_state->window()->GetTargetBounds();
      if (!window_state->window()->GetTransientParent()) {
        wm::AdjustBoundsToEnsureMinimumWindowVisibility(work_area_in_parent,
                                                        &bounds);
      }
      window_state->AdjustSnappedBounds(&bounds);
      if (window_state->window()->GetTargetBounds() != bounds)
        window_state->SetBoundsDirectAnimated(bounds);
      return true;
    }
    case WM_EVENT_TOGGLE_MAXIMIZE_CAPTION:
    case WM_EVENT_TOGGLE_MAXIMIZE:
    case WM_EVENT_TOGGLE_VERTICAL_MAXIMIZE:
    case WM_EVENT_TOGGLE_HORIZONTAL_MAXIMIZE:
    case WM_EVENT_TOGGLE_FULLSCREEN:
    case WM_EVENT_CYCLE_SNAP_LEFT:
    case WM_EVENT_CYCLE_SNAP_RIGHT:
    case WM_EVENT_CENTER:
    case WM_EVENT_NORMAL:
    case WM_EVENT_MAXIMIZE:
    case WM_EVENT_MINIMIZE:
    case WM_EVENT_FULLSCREEN:
    case WM_EVENT_PIN:
    case WM_EVENT_TRUSTED_PIN:
    case WM_EVENT_SNAP_LEFT:
    case WM_EVENT_SNAP_RIGHT:
    case WM_EVENT_SET_BOUNDS:
    case WM_EVENT_SHOW_INACTIVE:
      break;
  }
  return false;
}

// static
bool DefaultState::SetMaximizedOrFullscreenBounds(WindowState* window_state) {
  DCHECK(!window_state->is_dragged());
  DCHECK(!window_state->allow_set_bounds_direct());
  if (window_state->IsMaximized()) {
    window_state->SetBoundsDirect(ScreenUtil::GetMaximizedWindowBoundsInParent(
        window_state->window()->aura_window()));
    return true;
  }
  if (window_state->IsFullscreen()) {
    window_state->SetBoundsDirect(ScreenUtil::GetDisplayBoundsInParent(
        window_state->window()->aura_window()));
    return true;
  }
  return false;
}

// static
void DefaultState::SetBounds(WindowState* window_state,
                             const SetBoundsEvent* event) {
  if (window_state->is_dragged() || window_state->allow_set_bounds_direct()) {
    // TODO(oshima|varkha): Is this still needed? crbug.com/485612.
    window_state->SetBoundsDirect(event->requested_bounds());
  } else if (window_state->IsSnapped()) {
    gfx::Rect work_area_in_parent =
        ScreenUtil::GetDisplayWorkAreaBoundsInParent(
            window_state->window()->aura_window());
    gfx::Rect child_bounds(event->requested_bounds());
    wm::AdjustBoundsSmallerThan(work_area_in_parent.size(), &child_bounds);
    window_state->AdjustSnappedBounds(&child_bounds);
    window_state->SetBoundsDirect(child_bounds);
  } else if (!SetMaximizedOrFullscreenBounds(window_state)) {
    window_state->SetBoundsConstrained(event->requested_bounds());
  }
}

void DefaultState::EnterToNextState(WindowState* window_state,
                                    WindowStateType next_state_type) {
  // Do nothing if  we're already in the same state.
  if (state_type_ == next_state_type)
    return;

  WindowStateType previous_state_type = state_type_;
  state_type_ = next_state_type;

  window_state->UpdateWindowPropertiesFromStateType();
  window_state->NotifyPreStateTypeChange(previous_state_type);

  if (window_state->window()->GetParent()) {
    if (!window_state->HasRestoreBounds() &&
        (previous_state_type == WINDOW_STATE_TYPE_DEFAULT ||
         previous_state_type == WINDOW_STATE_TYPE_NORMAL) &&
        !window_state->IsMinimized() && !window_state->IsNormalStateType()) {
      window_state->SaveCurrentBoundsForRestore();
    }

    // When restoring from a minimized state, we want to restore to the
    // previous bounds. However, we want to maintain the restore bounds.
    // (The restore bounds are set if a user maximized the window in one
    // axis by double clicking the window border for example).
    gfx::Rect restore_bounds_in_screen;
    if (previous_state_type == WINDOW_STATE_TYPE_MINIMIZED &&
        window_state->IsNormalStateType() && window_state->HasRestoreBounds() &&
        !window_state->unminimize_to_restore_bounds()) {
      restore_bounds_in_screen = window_state->GetRestoreBoundsInScreen();
      window_state->SaveCurrentBoundsForRestore();
    }

    if (window_state->IsMaximizedOrFullscreenOrPinned())
      MoveToDisplayForRestore(window_state);

    UpdateBoundsFromState(window_state, previous_state_type);

    // Normal state should have no restore bounds unless it's
    // unminimized.
    if (!restore_bounds_in_screen.IsEmpty())
      window_state->SetRestoreBoundsInScreen(restore_bounds_in_screen);
    else if (window_state->IsNormalStateType())
      window_state->ClearRestoreBounds();
  }
  window_state->NotifyPostStateTypeChange(previous_state_type);

  if (next_state_type == WINDOW_STATE_TYPE_PINNED ||
      previous_state_type == WINDOW_STATE_TYPE_PINNED ||
      next_state_type == WINDOW_STATE_TYPE_TRUSTED_PINNED ||
      previous_state_type == WINDOW_STATE_TYPE_TRUSTED_PINNED) {
    Shell::Get()->screen_pinning_controller()->SetPinnedWindow(
        window_state->window());
  }
}

void DefaultState::ReenterToCurrentState(
    WindowState* window_state,
    WindowState::State* state_in_previous_mode) {
  WindowStateType previous_state_type = state_in_previous_mode->GetType();

  // A state change should not move a window into or out of full screen or
  // pinned since these are "special mode" the user wanted to be in and
  // should be respected as such.
  if (previous_state_type == wm::WINDOW_STATE_TYPE_FULLSCREEN ||
      previous_state_type == wm::WINDOW_STATE_TYPE_PINNED ||
      previous_state_type == wm::WINDOW_STATE_TYPE_TRUSTED_PINNED) {
    state_type_ = previous_state_type;
  } else if (state_type_ == wm::WINDOW_STATE_TYPE_FULLSCREEN ||
             state_type_ == wm::WINDOW_STATE_TYPE_PINNED ||
             state_type_ == wm::WINDOW_STATE_TYPE_TRUSTED_PINNED) {
    state_type_ = previous_state_type;
  }

  window_state->UpdateWindowPropertiesFromStateType();
  window_state->NotifyPreStateTypeChange(previous_state_type);

  if ((state_type_ == wm::WINDOW_STATE_TYPE_NORMAL ||
       state_type_ == wm::WINDOW_STATE_TYPE_DEFAULT) &&
      !stored_bounds_.IsEmpty()) {
    // Use the restore mechanism to set the bounds for
    // the window in normal state. This also covers unminimize case.
    window_state->SetRestoreBoundsInParent(stored_bounds_);
  }

  UpdateBoundsFromState(window_state, state_in_previous_mode->GetType());

  // Then restore the restore bounds to their previous value.
  if (!stored_restore_bounds_.IsEmpty())
    window_state->SetRestoreBoundsInParent(stored_restore_bounds_);
  else
    window_state->ClearRestoreBounds();

  window_state->NotifyPostStateTypeChange(previous_state_type);
}

void DefaultState::UpdateBoundsFromState(WindowState* window_state,
                                         WindowStateType previous_state_type) {
  aura::Window* window = window_state->window()->aura_window();
  gfx::Rect bounds_in_parent;
  switch (state_type_) {
    case WINDOW_STATE_TYPE_LEFT_SNAPPED:
    case WINDOW_STATE_TYPE_RIGHT_SNAPPED:
      bounds_in_parent =
          state_type_ == WINDOW_STATE_TYPE_LEFT_SNAPPED
              ? GetDefaultLeftSnappedWindowBoundsInParent(window)
              : GetDefaultRightSnappedWindowBoundsInParent(window);
      break;

    case WINDOW_STATE_TYPE_DEFAULT:
    case WINDOW_STATE_TYPE_NORMAL: {
      gfx::Rect work_area_in_parent =
          ScreenUtil::GetDisplayWorkAreaBoundsInParent(window);
      if (window_state->HasRestoreBounds()) {
        bounds_in_parent = window_state->GetRestoreBoundsInParent();
        // Check if the |window|'s restored size is bigger than the working area
        // This may happen if a window was resized to maximized bounds or if the
        // display resolution changed while the window was maximized.
        if (previous_state_type == WINDOW_STATE_TYPE_MAXIMIZED &&
            bounds_in_parent.width() >= work_area_in_parent.width() &&
            bounds_in_parent.height() >= work_area_in_parent.height()) {
          bounds_in_parent = work_area_in_parent;
          bounds_in_parent.Inset(kMaximizedWindowInset, kMaximizedWindowInset,
                                 kMaximizedWindowInset, kMaximizedWindowInset);
        }
      } else {
        bounds_in_parent = window->bounds();
      }
      // Make sure that part of the window is always visible.
      if (!window_state->is_dragged()) {
        // Avoid doing this while the window is being dragged as its root
        // window hasn't been updated yet in the case of dragging to another
        // display. crbug.com/666836.
        wm::AdjustBoundsToEnsureMinimumWindowVisibility(work_area_in_parent,
                                                        &bounds_in_parent);
      }
      break;
    }
    case WINDOW_STATE_TYPE_MAXIMIZED:
      bounds_in_parent = ScreenUtil::GetMaximizedWindowBoundsInParent(window);
      break;

    case WINDOW_STATE_TYPE_FULLSCREEN:
    case WINDOW_STATE_TYPE_PINNED:
    case WINDOW_STATE_TYPE_TRUSTED_PINNED:
      bounds_in_parent = ScreenUtil::GetDisplayBoundsInParent(window);
      break;

    case WINDOW_STATE_TYPE_MINIMIZED:
      break;
    case WINDOW_STATE_TYPE_INACTIVE:
    case WINDOW_STATE_TYPE_END:
    case WINDOW_STATE_TYPE_AUTO_POSITIONED:
      return;
  }

  if (!window_state->IsMinimized()) {
    if (IsMinimizedWindowState(previous_state_type) ||
        window_state->IsFullscreen() || window_state->IsPinned()) {
      window_state->SetBoundsDirect(bounds_in_parent);
    } else if (window_state->IsMaximized() ||
               IsMaximizedOrFullscreenOrPinnedWindowStateType(
                   previous_state_type)) {
      window_state->SetBoundsDirectCrossFade(bounds_in_parent);
    } else if (window_state->is_dragged()) {
      // SetBoundsDirectAnimated does not work when the window gets reparented.
      // TODO(oshima): Consider fixing it and reenable the animation.
      window_state->SetBoundsDirect(bounds_in_parent);
    } else {
      window_state->SetBoundsDirectAnimated(bounds_in_parent);
    }
  }

  if (window_state->IsMinimized()) {
    // Save the previous show state so that we can correctly restore it after
    // exiting the minimized mode.
    window->SetProperty(aura::client::kPreMinimizedShowStateKey,
                        ToWindowShowState(previous_state_type));
    ::wm::SetWindowVisibilityAnimationType(
        window, WINDOW_VISIBILITY_ANIMATION_TYPE_MINIMIZE);

    window->Hide();
    if (window_state->IsActive())
      window_state->Deactivate();
  } else if ((window->layer()->GetTargetVisibility() ||
              IsMinimizedWindowState(previous_state_type)) &&
             !window->layer()->visible()) {
    // The layer may be hidden if the window was previously minimized. Make
    // sure it's visible.
    window->Show();
    if (IsMinimizedWindowState(previous_state_type) &&
        !window_state->IsMaximizedOrFullscreenOrPinned()) {
      window_state->set_unminimize_to_restore_bounds(false);
    }
  }
}

// static
void DefaultState::CenterWindow(WindowState* window_state) {
  if (!window_state->IsNormalOrSnapped())
    return;
  WmWindow* window = window_state->window();
  if (window_state->IsSnapped()) {
    gfx::Rect center_in_screen = window->GetDisplayNearestWindow().work_area();
    gfx::Size size = window_state->HasRestoreBounds()
                         ? window_state->GetRestoreBoundsInScreen().size()
                         : window->GetBounds().size();
    center_in_screen.ClampToCenteredSize(size);
    window_state->SetRestoreBoundsInScreen(center_in_screen);
    window_state->Restore();
  } else {
    gfx::Rect center_in_parent =
        ScreenUtil::GetDisplayWorkAreaBoundsInParent(window->aura_window());
    center_in_parent.ClampToCenteredSize(window->GetBounds().size());
    window_state->SetBoundsDirectAnimated(center_in_parent);
  }
  // Centering window is treated as if a user moved and resized the window.
  window_state->set_bounds_changed_by_user(true);
}

}  // namespace wm
}  // namespace ash
