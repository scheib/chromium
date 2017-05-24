// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/window_state.h"

#include <utility>

#include "ash/public/cpp/window_properties.h"
#include "ash/public/interfaces/window_pin_type.mojom.h"
#include "ash/screen_util.h"
#include "ash/wm/default_state.h"
#include "ash/wm/window_positioning_utils.h"
#include "ash/wm/window_properties.h"
#include "ash/wm/window_state_delegate.h"
#include "ash/wm/window_state_observer.h"
#include "ash/wm/window_util.h"
#include "ash/wm/wm_event.h"
#include "ash/wm_window.h"
#include "base/auto_reset.h"
#include "ui/aura/window.h"

namespace ash {
namespace wm {

namespace {

WMEventType WMEventTypeFromShowState(ui::WindowShowState requested_show_state) {
  switch (requested_show_state) {
    case ui::SHOW_STATE_DEFAULT:
    case ui::SHOW_STATE_NORMAL:
      return WM_EVENT_NORMAL;
    case ui::SHOW_STATE_MINIMIZED:
      return WM_EVENT_MINIMIZE;
    case ui::SHOW_STATE_MAXIMIZED:
      return WM_EVENT_MAXIMIZE;
    case ui::SHOW_STATE_FULLSCREEN:
      return WM_EVENT_FULLSCREEN;
    case ui::SHOW_STATE_INACTIVE:
      return WM_EVENT_SHOW_INACTIVE;

    case ui::SHOW_STATE_END:
      NOTREACHED() << "No WMEvent defined for the show state:"
                   << requested_show_state;
  }
  return WM_EVENT_NORMAL;
}

WMEventType WMEventTypeFromWindowPinType(ash::mojom::WindowPinType type) {
  switch (type) {
    case ash::mojom::WindowPinType::NONE:
      return WM_EVENT_NORMAL;
    case ash::mojom::WindowPinType::PINNED:
      return WM_EVENT_PIN;
    case ash::mojom::WindowPinType::TRUSTED_PINNED:
      return WM_EVENT_TRUSTED_PIN;
  }
  NOTREACHED() << "No WMEvent defined for the window pin type:" << type;
  return WM_EVENT_NORMAL;
}

}  // namespace

WindowState::~WindowState() {}

bool WindowState::HasDelegate() const {
  return !!delegate_;
}

void WindowState::SetDelegate(std::unique_ptr<WindowStateDelegate> delegate) {
  DCHECK(!delegate_.get());
  delegate_ = std::move(delegate);
}

WindowStateType WindowState::GetStateType() const {
  return current_state_->GetType();
}

bool WindowState::IsMinimized() const {
  return GetStateType() == WINDOW_STATE_TYPE_MINIMIZED;
}

bool WindowState::IsMaximized() const {
  return GetStateType() == WINDOW_STATE_TYPE_MAXIMIZED;
}

bool WindowState::IsFullscreen() const {
  return GetStateType() == WINDOW_STATE_TYPE_FULLSCREEN;
}

bool WindowState::IsMaximizedOrFullscreenOrPinned() const {
  return GetStateType() == WINDOW_STATE_TYPE_MAXIMIZED ||
         GetStateType() == WINDOW_STATE_TYPE_FULLSCREEN || IsPinned();
}

bool WindowState::IsSnapped() const {
  return GetStateType() == WINDOW_STATE_TYPE_LEFT_SNAPPED ||
         GetStateType() == WINDOW_STATE_TYPE_RIGHT_SNAPPED;
}

bool WindowState::IsPinned() const {
  return GetStateType() == WINDOW_STATE_TYPE_PINNED ||
         GetStateType() == WINDOW_STATE_TYPE_TRUSTED_PINNED;
}

bool WindowState::IsTrustedPinned() const {
  return GetStateType() == WINDOW_STATE_TYPE_TRUSTED_PINNED;
}

bool WindowState::IsNormalStateType() const {
  return GetStateType() == WINDOW_STATE_TYPE_NORMAL ||
         GetStateType() == WINDOW_STATE_TYPE_DEFAULT;
}

bool WindowState::IsNormalOrSnapped() const {
  return IsNormalStateType() || IsSnapped();
}

bool WindowState::IsActive() const {
  return window_->IsActive();
}

bool WindowState::IsUserPositionable() const {
  return (window_->GetType() == ui::wm::WINDOW_TYPE_NORMAL ||
          window_->GetType() == ui::wm::WINDOW_TYPE_PANEL);
}

bool WindowState::CanMaximize() const {
  // Window must allow maximization and have no maximum width or height.
  if (!window_->CanMaximize())
    return false;

  if (!window_->HasNonClientArea())
    return true;

  gfx::Size max_size = window_->GetMaximumSize();
  return !max_size.width() && !max_size.height();
}

bool WindowState::CanMinimize() const {
  return window_->CanMinimize();
}

bool WindowState::CanResize() const {
  return window_->CanResize();
}

bool WindowState::CanActivate() const {
  return window_->CanActivate();
}

bool WindowState::CanSnap() const {
  if (!CanResize() || window_->GetType() == ui::wm::WINDOW_TYPE_PANEL ||
      window_->GetTransientParent()) {
    return false;
  }
  // If a window cannot be maximized, assume it cannot snap either.
  // TODO(oshima): We should probably snap if the maximum size is greater than
  // the snapped size.
  return CanMaximize();
}

bool WindowState::HasRestoreBounds() const {
  return window_->HasRestoreBounds();
}

void WindowState::Maximize() {
  window_->Maximize();
}

void WindowState::Minimize() {
  window_->Minimize();
}

void WindowState::Unminimize() {
  window_->Unminimize();
}

void WindowState::Activate() {
  window_->Activate();
}

void WindowState::Deactivate() {
  window_->Deactivate();
}

void WindowState::Restore() {
  if (!IsNormalStateType()) {
    const WMEvent event(WM_EVENT_NORMAL);
    OnWMEvent(&event);
  }
}

void WindowState::DisableAlwaysOnTop(WmWindow* window_on_top) {
  if (GetAlwaysOnTop()) {
    // |window_| is hidden first to avoid canceling fullscreen mode when it is
    // no longer always on top and gets added to default container. This avoids
    // sending redundant OnFullscreenStateChanged to the layout manager. The
    // |window_| visibility is restored after it no longer obscures the
    // |window_on_top|.
    bool visible = window_->IsVisible();
    if (visible)
      window_->Hide();
    window_->SetAlwaysOnTop(false);
    // Technically it is possible that a |window_| could make itself
    // always_on_top really quickly. This is probably not a realistic case but
    // check if the two windows are in the same container just in case.
    if (window_on_top && window_on_top->GetParent() == window_->GetParent())
      window_->GetParent()->StackChildAbove(window_on_top, window_);
    if (visible)
      window_->Show();
    cached_always_on_top_ = true;
  }
}

void WindowState::RestoreAlwaysOnTop() {
  if (delegate() && delegate()->RestoreAlwaysOnTop(this))
    return;
  if (cached_always_on_top_) {
    cached_always_on_top_ = false;
    window_->SetAlwaysOnTop(true);
  }
}

void WindowState::OnWMEvent(const WMEvent* event) {
  current_state_->OnWMEvent(this, event);
}

void WindowState::SaveCurrentBoundsForRestore() {
  gfx::Rect bounds_in_screen =
      window_->GetParent()->ConvertRectToScreen(window_->GetBounds());
  SetRestoreBoundsInScreen(bounds_in_screen);
}

gfx::Rect WindowState::GetRestoreBoundsInScreen() const {
  return window_->GetRestoreBoundsInScreen();
}

gfx::Rect WindowState::GetRestoreBoundsInParent() const {
  return window_->GetParent()->ConvertRectFromScreen(
      GetRestoreBoundsInScreen());
}

void WindowState::SetRestoreBoundsInScreen(const gfx::Rect& bounds) {
  window_->SetRestoreBoundsInScreen(bounds);
}

void WindowState::SetRestoreBoundsInParent(const gfx::Rect& bounds) {
  SetRestoreBoundsInScreen(window_->GetParent()->ConvertRectToScreen(bounds));
}

void WindowState::ClearRestoreBounds() {
  window_->ClearRestoreBounds();
}

std::unique_ptr<WindowState::State> WindowState::SetStateObject(
    std::unique_ptr<WindowState::State> new_state) {
  current_state_->DetachState(this);
  std::unique_ptr<WindowState::State> old_object = std::move(current_state_);
  current_state_ = std::move(new_state);
  current_state_->AttachState(this, old_object.get());
  return old_object;
}

void WindowState::SetPreAutoManageWindowBounds(const gfx::Rect& bounds) {
  pre_auto_manage_window_bounds_.reset(new gfx::Rect(bounds));
}

void WindowState::AddObserver(WindowStateObserver* observer) {
  observer_list_.AddObserver(observer);
}

void WindowState::RemoveObserver(WindowStateObserver* observer) {
  observer_list_.RemoveObserver(observer);
}

void WindowState::set_bounds_changed_by_user(bool bounds_changed_by_user) {
  bounds_changed_by_user_ = bounds_changed_by_user;
  if (bounds_changed_by_user)
    pre_auto_manage_window_bounds_.reset();
}

void WindowState::CreateDragDetails(const gfx::Point& point_in_parent,
                                    int window_component,
                                    aura::client::WindowMoveSource source) {
  drag_details_.reset(
      new DragDetails(window_, point_in_parent, window_component, source));
}

void WindowState::DeleteDragDetails() {
  drag_details_.reset();
}

void WindowState::SetAndClearRestoreBounds() {
  DCHECK(HasRestoreBounds());
  SetBoundsInScreen(GetRestoreBoundsInScreen());
  ClearRestoreBounds();
}

void WindowState::OnWindowShowStateChanged() {
  if (!ignore_property_change_) {
    WMEvent event(WMEventTypeFromShowState(GetShowState()));
    OnWMEvent(&event);
  }
}

void WindowState::OnWindowPinTypeChanged() {
  if (!ignore_property_change_) {
    WMEvent event(WMEventTypeFromWindowPinType(GetPinType()));
    OnWMEvent(&event);
  }
}

WindowState::WindowState(WmWindow* window)
    : window_(window),
      window_position_managed_(false),
      bounds_changed_by_user_(false),
      ignored_by_shelf_(false),
      can_consume_system_keys_(false),
      unminimize_to_restore_bounds_(false),
      in_immersive_fullscreen_(false),
      hide_shelf_when_fullscreen_(true),
      autohide_shelf_when_maximized_or_fullscreen_(false),
      minimum_visibility_(false),
      can_be_dragged_(true),
      cached_always_on_top_(false),
      ignore_property_change_(false),
      current_state_(new DefaultState(ToWindowStateType(GetShowState()))) {}

bool WindowState::GetAlwaysOnTop() const {
  return window_->IsAlwaysOnTop();
}

ui::WindowShowState WindowState::GetShowState() const {
  return window_->GetShowState();
}

ash::mojom::WindowPinType WindowState::GetPinType() const {
  return window_->aura_window()->GetProperty(kWindowPinTypeKey);
}

void WindowState::SetBoundsInScreen(const gfx::Rect& bounds_in_screen) {
  gfx::Rect bounds_in_parent =
      window_->GetParent()->ConvertRectFromScreen(bounds_in_screen);
  window_->SetBounds(bounds_in_parent);
}

void WindowState::AdjustSnappedBounds(gfx::Rect* bounds) {
  if (is_dragged() || !IsSnapped())
    return;
  gfx::Rect maximized_bounds =
      ScreenUtil::GetMaximizedWindowBoundsInParent(window_->aura_window());
  if (GetStateType() == WINDOW_STATE_TYPE_LEFT_SNAPPED)
    bounds->set_x(maximized_bounds.x());
  else if (GetStateType() == WINDOW_STATE_TYPE_RIGHT_SNAPPED)
    bounds->set_x(maximized_bounds.right() - bounds->width());
  bounds->set_y(maximized_bounds.y());
  bounds->set_height(maximized_bounds.height());
}

void WindowState::UpdateWindowPropertiesFromStateType() {
  ui::WindowShowState new_window_state =
      ToWindowShowState(current_state_->GetType());
  if (new_window_state != GetShowState()) {
    base::AutoReset<bool> resetter(&ignore_property_change_, true);
    window_->SetShowState(new_window_state);
  }

  // sync up current window show state with PinType property.
  ash::mojom::WindowPinType pin_type = ash::mojom::WindowPinType::NONE;
  if (GetStateType() == WINDOW_STATE_TYPE_PINNED)
    pin_type = ash::mojom::WindowPinType::PINNED;
  else if (GetStateType() == WINDOW_STATE_TYPE_TRUSTED_PINNED)
    pin_type = ash::mojom::WindowPinType::TRUSTED_PINNED;
  if (pin_type != GetPinType()) {
    base::AutoReset<bool> resetter(&ignore_property_change_, true);
    window_->aura_window()->SetProperty(kWindowPinTypeKey, pin_type);
  }
}

void WindowState::NotifyPreStateTypeChange(
    WindowStateType old_window_state_type) {
  for (auto& observer : observer_list_)
    observer.OnPreWindowStateTypeChange(this, old_window_state_type);
}

void WindowState::NotifyPostStateTypeChange(
    WindowStateType old_window_state_type) {
  for (auto& observer : observer_list_)
    observer.OnPostWindowStateTypeChange(this, old_window_state_type);
}

void WindowState::SetBoundsDirect(const gfx::Rect& bounds) {
  gfx::Rect actual_new_bounds(bounds);
  // Ensure we don't go smaller than our minimum bounds in "normal" window
  // modes
  if (window_->HasNonClientArea() && !IsMaximized() && !IsFullscreen()) {
    // Get the minimum usable size of the minimum size and the screen size.
    gfx::Size min_size = window_->GetMinimumSize();
    min_size.SetToMin(window_->GetDisplayNearestWindow().work_area().size());

    actual_new_bounds.set_width(
        std::max(min_size.width(), actual_new_bounds.width()));
    actual_new_bounds.set_height(
        std::max(min_size.height(), actual_new_bounds.height()));
  }
  window_->SetBoundsDirect(actual_new_bounds);
}

void WindowState::SetBoundsConstrained(const gfx::Rect& bounds) {
  gfx::Rect work_area_in_parent =
      ScreenUtil::GetDisplayWorkAreaBoundsInParent(window_->aura_window());
  gfx::Rect child_bounds(bounds);
  AdjustBoundsSmallerThan(work_area_in_parent.size(), &child_bounds);
  SetBoundsDirect(child_bounds);
}

void WindowState::SetBoundsDirectAnimated(const gfx::Rect& bounds) {
  window_->SetBoundsDirectAnimated(bounds);
}

void WindowState::SetBoundsDirectCrossFade(const gfx::Rect& new_bounds) {
  // Some test results in invoking CrossFadeToBounds when window is not visible.
  // No animation is necessary in that case, thus just change the bounds and
  // quit.
  if (!window_->GetTargetVisibility()) {
    SetBoundsConstrained(new_bounds);
    return;
  }

  window_->SetBoundsDirectCrossFade(new_bounds);
}

WindowState* GetActiveWindowState() {
  aura::Window* active = GetActiveWindow();
  return active ? GetWindowState(active) : nullptr;
}

WindowState* GetWindowState(aura::Window* window) {
  if (!window)
    return nullptr;
  WindowState* settings = window->GetProperty(kWindowStateKey);
  if (!settings) {
    settings = new WindowState(WmWindow::Get(window));
    window->SetProperty(kWindowStateKey, settings);
  }
  return settings;
}

const WindowState* GetWindowState(const aura::Window* window) {
  return GetWindowState(const_cast<aura::Window*>(window));
}

}  // namespace wm
}  // namespace ash
