// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/root_window_controller.h"

#include <algorithm>
#include <queue>
#include <vector>

#include "ash/ash_constants.h"
#include "ash/ash_switches.h"
#include "ash/ash_touch_exploration_manager_chromeos.h"
#include "ash/focus_cycler.h"
#include "ash/high_contrast/high_contrast_controller.h"
#include "ash/host/ash_window_tree_host.h"
#include "ash/login_status.h"
#include "ash/public/cpp/config.h"
#include "ash/public/cpp/shelf_types.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/root_window_settings.h"
#include "ash/screen_util.h"
#include "ash/session/session_controller.h"
#include "ash/shelf/shelf_layout_manager.h"
#include "ash/shelf/shelf_widget.h"
#include "ash/shelf/shelf_window_targeter.h"
#include "ash/shelf/wm_shelf.h"
#include "ash/shell.h"
#include "ash/shell_delegate.h"
#include "ash/shell_port.h"
#include "ash/system/status_area_layout_manager.h"
#include "ash/system/status_area_widget.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "ash/touch/touch_hud_debug.h"
#include "ash/touch/touch_hud_projection.h"
#include "ash/touch/touch_observer_hud.h"
#include "ash/wallpaper/wallpaper_delegate.h"
#include "ash/wallpaper/wallpaper_widget_controller.h"
#include "ash/wm/always_on_top_controller.h"
#include "ash/wm/boot_splash_screen_chromeos.h"
#include "ash/wm/container_finder.h"
#include "ash/wm/fullscreen_window_finder.h"
#include "ash/wm/lock_layout_manager.h"
#include "ash/wm/panels/attached_panel_window_targeter.h"
#include "ash/wm/panels/panel_layout_manager.h"
#include "ash/wm/panels/panel_window_event_handler.h"
#include "ash/wm/root_window_layout_manager.h"
#include "ash/wm/stacking_controller.h"
#include "ash/wm/switchable_windows.h"
#include "ash/wm/system_modal_container_layout_manager.h"
#include "ash/wm/system_wallpaper_controller.h"
#include "ash/wm/window_properties.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm/workspace/workspace_layout_manager.h"
#include "ash/wm/workspace_controller.h"
#include "ash/wm_window.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/time/time.h"
#include "chromeos/chromeos_switches.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/drag_drop_client.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/mus/window_mus.h"
#include "ui/aura/mus/window_port_mus.h"
#include "ui/aura/mus/window_tree_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/aura/window_observer.h"
#include "ui/aura/window_tracker.h"
#include "ui/base/models/menu_model.h"
#include "ui/chromeos/touch_exploration_controller.h"
#include "ui/display/types/display_constants.h"
#include "ui/events/event_utils.h"
#include "ui/keyboard/keyboard_controller.h"
#include "ui/keyboard/keyboard_util.h"
#include "ui/views/controls/menu/menu_model_adapter.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view_model.h"
#include "ui/views/view_model_utils.h"
#include "ui/wm/core/capture_controller.h"
#include "ui/wm/core/coordinate_conversion.h"
#include "ui/wm/core/visibility_controller.h"
#include "ui/wm/core/window_util.h"
#include "ui/wm/public/tooltip_client.h"
#include "ui/wm/public/window_types.h"

namespace ash {
namespace {

// Duration for the animation that hides the boot splash screen, in
// milliseconds.  This should be short enough in relation to
// wm/window_animation.cc's brightness/grayscale fade animation that the login
// wallpaper image animation isn't hidden by the splash screen animation.
const int kBootSplashScreenHideDurationMs = 500;

bool IsWindowAboveContainer(aura::Window* window,
                            aura::Window* blocking_container) {
  std::vector<aura::Window*> target_path;
  std::vector<aura::Window*> blocking_path;

  while (window) {
    target_path.push_back(window);
    window = window->parent();
  }

  while (blocking_container) {
    blocking_path.push_back(blocking_container);
    blocking_container = blocking_container->parent();
  }

  // The root window is put at the end so that we compare windows at
  // the same depth.
  while (!blocking_path.empty()) {
    if (target_path.empty())
      return false;

    aura::Window* target = target_path.back();
    target_path.pop_back();
    aura::Window* blocking = blocking_path.back();
    blocking_path.pop_back();

    // Still on the same path, continue.
    if (target == blocking)
      continue;

    // This can happen only if unparented window is passed because
    // first element must be the same root.
    if (!target->parent() || !blocking->parent())
      return false;

    aura::Window* common_parent = target->parent();
    DCHECK_EQ(common_parent, blocking->parent());
    aura::Window::Windows windows = common_parent->children();
    auto blocking_iter = std::find(windows.begin(), windows.end(), blocking);
    // If the target window is above blocking window, the window can handle
    // events.
    return std::find(blocking_iter, windows.end(), target) != windows.end();
  }

  return true;
}

// Scales |value| that is originally between 0 and |src_max| to be between
// 0 and |dst_max|.
float ToRelativeValue(int value, int src_max, int dst_max) {
  return static_cast<float>(value) / static_cast<float>(src_max) * dst_max;
}

// Uses ToRelativeValue() to scale the origin of |bounds_in_out|. The
// width/height are not changed.
void MoveOriginRelativeToSize(const gfx::Size& src_size,
                              const gfx::Size& dst_size,
                              gfx::Rect* bounds_in_out) {
  gfx::Point origin = bounds_in_out->origin();
  bounds_in_out->set_origin(gfx::Point(
      ToRelativeValue(origin.x(), src_size.width(), dst_size.width()),
      ToRelativeValue(origin.y(), src_size.height(), dst_size.height())));
}

// Reparents |window| to |new_parent|.
void ReparentWindow(aura::Window* window, aura::Window* new_parent) {
  const gfx::Size src_size = window->parent()->bounds().size();
  const gfx::Size dst_size = new_parent->bounds().size();
  // Update the restore bounds to make it relative to the display.
  wm::WindowState* state = wm::GetWindowState(window);
  gfx::Rect restore_bounds;
  const bool has_restore_bounds = state->HasRestoreBounds();

  const bool update_bounds = state->IsNormalOrSnapped() || state->IsMinimized();
  gfx::Rect work_area_in_new_parent =
      ScreenUtil::GetDisplayWorkAreaBoundsInParent(new_parent);

  gfx::Rect local_bounds;
  if (update_bounds) {
    local_bounds = state->window()->GetBounds();
    MoveOriginRelativeToSize(src_size, dst_size, &local_bounds);
    local_bounds.AdjustToFit(work_area_in_new_parent);
  }

  if (has_restore_bounds) {
    restore_bounds = state->GetRestoreBoundsInParent();
    MoveOriginRelativeToSize(src_size, dst_size, &restore_bounds);
    restore_bounds.AdjustToFit(work_area_in_new_parent);
  }

  new_parent->AddChild(window);

  // Docked windows have bounds handled by the layout manager in AddChild().
  if (update_bounds)
    window->SetBounds(local_bounds);

  if (has_restore_bounds)
    state->SetRestoreBoundsInParent(restore_bounds);
}

// Reparents the appropriate set of windows from |src| to |dst|.
void ReparentAllWindows(aura::Window* src, aura::Window* dst) {
  // Set of windows to move.
  const int kContainerIdsToMove[] = {
      kShellWindowId_DefaultContainer,
      kShellWindowId_PanelContainer,
      kShellWindowId_AlwaysOnTopContainer,
      kShellWindowId_SystemModalContainer,
      kShellWindowId_LockSystemModalContainer,
      kShellWindowId_UnparentedControlContainer,
      kShellWindowId_OverlayContainer,
  };
  const int kExtraContainerIdsToMoveInUnifiedMode[] = {
      kShellWindowId_LockScreenContainer,
      kShellWindowId_LockScreenWallpaperContainer,
  };
  std::vector<int> container_ids(
      kContainerIdsToMove,
      kContainerIdsToMove + arraysize(kContainerIdsToMove));
  // Check the display mode as this is also necessary when trasitioning between
  // mirror and unified mode.
  if (ShellPort::Get()->IsInUnifiedModeIgnoreMirroring()) {
    for (int id : kExtraContainerIdsToMoveInUnifiedMode)
      container_ids.push_back(id);
  }

  for (int id : container_ids) {
    aura::Window* src_container = src->GetChildById(id);
    aura::Window* dst_container = dst->GetChildById(id);
    while (!src_container->children().empty()) {
      // Restart iteration from the source container windows each time as they
      // may change as a result of moving other windows.
      const aura::Window::Windows& src_container_children =
          src_container->children();
      auto iter = src_container_children.begin();
      while (iter != src_container_children.end() &&
             SystemModalContainerLayoutManager::IsModalBackground(*iter)) {
        ++iter;
      }
      // If the entire window list is modal background windows then stop.
      if (iter == src_container_children.end())
        break;
      ReparentWindow(*iter, dst_container);
    }
  }
}

// Creates a new window for use as a container.
// TODO(sky): This should create an aura::Window. http://crbug.com/671246.
WmWindow* CreateContainer(int window_id, const char* name, WmWindow* parent) {
  aura::Window* window = new aura::Window(nullptr, ui::wm::WINDOW_TYPE_UNKNOWN);
  window->Init(ui::LAYER_NOT_DRAWN);
  if (Shell::GetAshConfig() != Config::CLASSIC) {
    aura::WindowPortMus::Get(window)->SetEventTargetingPolicy(
        ui::mojom::EventTargetingPolicy::DESCENDANTS_ONLY);
  }
  window->set_id(window_id);
  window->SetName(name);
  parent->aura_window()->AddChild(window);
  if (window_id != kShellWindowId_UnparentedControlContainer)
    window->Show();
  return WmWindow::Get(window);
}

// TODO(sky): This should take an aura::Window. http://crbug.com/671246.
bool ShouldDestroyWindowInCloseChildWindows(WmWindow* window) {
  if (!WmWindow::GetAuraWindow(window)->owned_by_parent())
    return false;

  if (Shell::GetAshConfig() != Config::MASH)
    return true;

  aura::WindowMus* window_mus =
      aura::WindowMus::Get(WmWindow::GetAuraWindow(window));
  return Shell::window_tree_client()->WasCreatedByThisClient(window_mus) ||
         Shell::window_tree_client()->IsRoot(window_mus);
}

}  // namespace

// static
std::vector<RootWindowController*>*
    RootWindowController::root_window_controllers_ = nullptr;

RootWindowController::~RootWindowController() {
  Shutdown();
  ash_host_.reset();
  mus_window_tree_host_.reset();
  // The CaptureClient needs to be around for as long as the RootWindow is
  // valid.
  capture_client_.reset();
  if (animating_wallpaper_widget_controller_.get())
    animating_wallpaper_widget_controller_->StopAnimating();
  root_window_controllers_->erase(std::find(root_window_controllers_->begin(),
                                            root_window_controllers_->end(),
                                            this));
}

void RootWindowController::CreateForPrimaryDisplay(AshWindowTreeHost* host) {
  RootWindowController* controller = new RootWindowController(host, nullptr);
  controller->Init(RootWindowType::PRIMARY);
}

void RootWindowController::CreateForSecondaryDisplay(AshWindowTreeHost* host) {
  RootWindowController* controller = new RootWindowController(host, nullptr);
  controller->Init(RootWindowType::SECONDARY);
}

// static
RootWindowController* RootWindowController::ForWindow(
    const aura::Window* window) {
  DCHECK(window);
  CHECK(Shell::HasInstance());
  return GetRootWindowController(window->GetRootWindow());
}

// static
RootWindowController* RootWindowController::ForTargetRootWindow() {
  CHECK(Shell::HasInstance());
  return GetRootWindowController(Shell::GetRootWindowForNewWindows());
}

void RootWindowController::ConfigureWidgetInitParamsForContainer(
    views::Widget* widget,
    int shell_container_id,
    views::Widget::InitParams* init_params) {
  init_params->parent = GetContainer(shell_container_id);
}

aura::WindowTreeHost* RootWindowController::GetHost() {
  return window_tree_host_;
}

const aura::WindowTreeHost* RootWindowController::GetHost() const {
  return window_tree_host_;
}

aura::Window* RootWindowController::GetRootWindow() {
  return GetHost()->window();
}

const aura::Window* RootWindowController::GetRootWindow() const {
  return GetHost()->window();
}

const WmWindow* RootWindowController::GetWindow() const {
  return WmWindow::Get(GetRootWindow());
}

wm::WorkspaceWindowState RootWindowController::GetWorkspaceWindowState() {
  return workspace_controller_ ? workspace_controller()->GetWindowState()
                               : wm::WORKSPACE_WINDOW_STATE_DEFAULT;
}

void RootWindowController::InitializeShelf() {
  if (shelf_initialized_)
    return;
  shelf_initialized_ = true;
  wm_shelf_->NotifyShelfInitialized();

  // TODO(jamescook): Pass |wm_shelf_| into the constructors for these layout
  // managers.
  if (panel_layout_manager_)
    panel_layout_manager_->SetShelf(wm_shelf_.get());

  // TODO(jamescook): Eliminate this. Refactor AttachedPanelWidgetTargeter's
  // access to WmShelf.
  Shell::Get()->NotifyShelfCreatedForRootWindow(WmWindow::Get(GetRootWindow()));

  wm_shelf_->shelf_widget()->PostCreateShelf();
}

ShelfLayoutManager* RootWindowController::GetShelfLayoutManager() {
  return wm_shelf_->shelf_layout_manager();
}

SystemModalContainerLayoutManager*
RootWindowController::GetSystemModalLayoutManager(aura::Window* window) {
  aura::Window* modal_container = nullptr;
  if (window) {
    aura::Window* window_container = wm::GetContainerForWindow(window);
    if (window_container &&
        window_container->id() >= kShellWindowId_LockScreenContainer) {
      modal_container = GetContainer(kShellWindowId_LockSystemModalContainer);
    } else {
      modal_container = GetContainer(kShellWindowId_SystemModalContainer);
    }
  } else {
    int modal_window_id =
        Shell::Get()->session_controller()->IsUserSessionBlocked()
            ? kShellWindowId_LockSystemModalContainer
            : kShellWindowId_SystemModalContainer;
    modal_container = GetContainer(modal_window_id);
  }
  return modal_container ? static_cast<SystemModalContainerLayoutManager*>(
                               modal_container->layout_manager())
                         : nullptr;
}

StatusAreaWidget* RootWindowController::GetStatusAreaWidget() {
  ShelfWidget* shelf_widget = wm_shelf_->shelf_widget();
  return shelf_widget ? shelf_widget->status_area_widget() : nullptr;
}

SystemTray* RootWindowController::GetSystemTray() {
  // We assume in throughout the code that this will not return NULL. If code
  // triggers this for valid reasons, it should test status_area_widget first.
  CHECK(wm_shelf_->shelf_widget()->status_area_widget());
  return wm_shelf_->shelf_widget()->status_area_widget()->system_tray();
}

bool RootWindowController::CanWindowReceiveEvents(aura::Window* window) {
  if (GetRootWindow() != window->GetRootWindow())
    return false;

  // Always allow events to fall through to the virtual keyboard even if
  // displaying a system modal dialog.
  if (IsVirtualKeyboardWindow(window))
    return true;

  aura::Window* blocking_container = nullptr;

  int modal_container_id = 0;
  if (Shell::Get()->session_controller()->IsUserSessionBlocked()) {
    blocking_container =
        GetContainer(kShellWindowId_LockScreenContainersContainer);
    modal_container_id = kShellWindowId_LockSystemModalContainer;
  } else {
    modal_container_id = kShellWindowId_SystemModalContainer;
  }
  aura::Window* modal_container = GetContainer(modal_container_id);
  SystemModalContainerLayoutManager* modal_layout_manager = nullptr;
  modal_layout_manager = static_cast<SystemModalContainerLayoutManager*>(
      modal_container->layout_manager());

  if (modal_layout_manager->has_window_dimmer())
    blocking_container = modal_container;
  else
    modal_container = nullptr;  // Don't check modal dialogs.

  // In normal session.
  if (!blocking_container)
    return true;

  if (!IsWindowAboveContainer(window, blocking_container))
    return false;

  // If the window is in the target modal container, only allow the top most
  // one.
  if (modal_container && modal_container->Contains(window))
    return modal_layout_manager->IsPartOfActiveModalWindow(window);

  return true;
}

aura::Window* RootWindowController::FindEventTarget(
    const gfx::Point& location_in_screen) {
  gfx::Point location_in_root(location_in_screen);
  aura::Window* root_window = GetRootWindow();
  ::wm::ConvertPointFromScreen(root_window, &location_in_root);
  ui::MouseEvent test_event(ui::ET_MOUSE_MOVED, location_in_root,
                            location_in_root, ui::EventTimeForNow(),
                            ui::EF_NONE, ui::EF_NONE);
  ui::EventTarget* event_handler =
      root_window->GetHost()
          ->dispatcher()
          ->GetDefaultEventTargeter()
          ->FindTargetForEvent(root_window, &test_event);
  return static_cast<aura::Window*>(event_handler);
}

gfx::Point RootWindowController::GetLastMouseLocationInRoot() {
  return window_tree_host_->dispatcher()->GetLastMouseLocationInRoot();
}

aura::Window* RootWindowController::GetContainer(int container_id) {
  return GetRootWindow()->GetChildById(container_id);
}

const aura::Window* RootWindowController::GetContainer(int container_id) const {
  return window_tree_host_->window()->GetChildById(container_id);
}

const WmWindow* RootWindowController::GetWmContainer(int container_id) const {
  const aura::Window* window = GetContainer(container_id);
  return WmWindow::Get(window);
}

void RootWindowController::SetWallpaperWidgetController(
    WallpaperWidgetController* controller) {
  wallpaper_widget_controller_.reset(controller);
}

void RootWindowController::SetAnimatingWallpaperWidgetController(
    AnimatingWallpaperWidgetController* controller) {
  if (animating_wallpaper_widget_controller_.get())
    animating_wallpaper_widget_controller_->StopAnimating();
  animating_wallpaper_widget_controller_.reset(controller);
}

void RootWindowController::OnInitialWallpaperAnimationStarted() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kAshAnimateFromBootSplashScreen) &&
      boot_splash_screen_.get()) {
    // Make the splash screen fade out so it doesn't obscure the wallpaper's
    // brightness/grayscale animation.
    boot_splash_screen_->StartHideAnimation(
        base::TimeDelta::FromMilliseconds(kBootSplashScreenHideDurationMs));
  }
}

void RootWindowController::OnWallpaperAnimationFinished(views::Widget* widget) {
  // Make sure the wallpaper is visible.
  system_wallpaper_->SetColor(SK_ColorBLACK);
  boot_splash_screen_.reset();
  Shell::Get()->wallpaper_delegate()->OnWallpaperAnimationFinished();
  // Only removes old component when wallpaper animation finished. If we
  // remove the old one before the new wallpaper is done fading in there will
  // be a white flash during the animation.
  if (animating_wallpaper_widget_controller()) {
    WallpaperWidgetController* controller =
        animating_wallpaper_widget_controller()->GetController(true);
    DCHECK_EQ(controller->widget(), widget);
    // Release the old controller and close its wallpaper widget.
    SetWallpaperWidgetController(controller);
  }
}

void RootWindowController::Shutdown() {
  Shell::Get()->RemoveShellObserver(this);

  touch_exploration_manager_.reset();

  ResetRootForNewWindowsIfNecessary();

  CloseChildWindows();
  aura::Window* root_window = GetRootWindow();
  GetRootWindowSettings(root_window)->controller = nullptr;
  // Forget with the display ID so that display lookup
  // ends up with invalid display.
  GetRootWindowSettings(root_window)->display_id = display::kInvalidDisplayId;
  if (ash_host_)
    ash_host_->PrepareForShutdown();

  system_wallpaper_.reset();
  aura::client::SetScreenPositionClient(root_window, nullptr);
}

void RootWindowController::CloseChildWindows() {
  // NOTE: this may be called multiple times.

  // Deactivate keyboard container before closing child windows and shutting
  // down associated layout managers.
  DeactivateKeyboard(keyboard::KeyboardController::GetInstance());

  // |panel_layout_manager_| needs to be shut down before windows are destroyed.
  if (panel_layout_manager_) {
    panel_layout_manager_->Shutdown();
    panel_layout_manager_ = nullptr;
  }

  WmShelf* shelf = GetShelf();
  shelf->ShutdownShelfWidget();

  workspace_controller_.reset();

  // Explicitly destroy top level windows. We do this because such windows may
  // query the RootWindow for state.
  aura::WindowTracker non_toplevel_windows;
  WmWindow* root = GetWindow();
  non_toplevel_windows.Add(root->aura_window());
  while (!non_toplevel_windows.windows().empty()) {
    aura::Window* non_toplevel_window = non_toplevel_windows.Pop();
    aura::WindowTracker toplevel_windows;
    for (aura::Window* child : non_toplevel_window->children()) {
      if (!ShouldDestroyWindowInCloseChildWindows(WmWindow::Get(child)))
        continue;
      if (child->delegate())
        toplevel_windows.Add(child);
      else
        non_toplevel_windows.Add(child);
    }
    while (!toplevel_windows.windows().empty())
      delete toplevel_windows.Pop();
  }
  // And then remove the containers.
  while (!root->GetChildren().empty()) {
    WmWindow* child = root->GetChildren()[0];
    if (ShouldDestroyWindowInCloseChildWindows(child))
      child->Destroy();
    else
      root->RemoveChild(child);
  }

  shelf->DestroyShelfWidget();

  aura::client::SetDragDropClient(GetRootWindow(), nullptr);
  aura::client::SetTooltipClient(GetRootWindow(), nullptr);
}

void RootWindowController::MoveWindowsTo(aura::Window* dst) {
  // Clear the workspace controller, so it doesn't incorrectly update the shelf.
  workspace_controller_.reset();
  ReparentAllWindows(GetRootWindow(), dst);
}

void RootWindowController::UpdateShelfVisibility() {
  wm_shelf_->UpdateVisibilityState();
}

void RootWindowController::InitTouchHuds() {
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kAshTouchHud))
    set_touch_hud_debug(new TouchHudDebug(GetRootWindow()));
  if (Shell::Get()->is_touch_hud_projection_enabled())
    EnableTouchHudProjection();
}

aura::Window* RootWindowController::GetWindowForFullscreenMode() {
  return wm::GetWindowForFullscreenMode(GetRootWindow());
}

void RootWindowController::ActivateKeyboard(
    keyboard::KeyboardController* keyboard_controller) {
  if (!keyboard::IsKeyboardEnabled() ||
      GetContainer(kShellWindowId_VirtualKeyboardContainer)) {
    return;
  }
  DCHECK(keyboard_controller);
  Shell::Get()->NotifyVirtualKeyboardActivated(true, GetRootWindow());
  aura::Window* parent = GetContainer(kShellWindowId_ImeWindowParentContainer);
  DCHECK(parent);
  aura::Window* keyboard_container = keyboard_controller->GetContainerWindow();
  keyboard_container->set_id(kShellWindowId_VirtualKeyboardContainer);
  parent->AddChild(keyboard_container);
}

void RootWindowController::DeactivateKeyboard(
    keyboard::KeyboardController* keyboard_controller) {
  if (!keyboard_controller ||
      !keyboard_controller->keyboard_container_initialized()) {
    return;
  }
  aura::Window* keyboard_container = keyboard_controller->GetContainerWindow();
  if (keyboard_container->GetRootWindow() == GetRootWindow()) {
    aura::Window* parent =
        GetContainer(kShellWindowId_ImeWindowParentContainer);
    DCHECK(parent);
    // Virtual keyboard may be deactivated while still showing, hide the
    // keyboard before removing it from view hierarchy.
    keyboard_controller->HideKeyboard(
        keyboard::KeyboardController::HIDE_REASON_AUTOMATIC);
    parent->RemoveChild(keyboard_container);
    Shell::Get()->NotifyVirtualKeyboardActivated(false, GetRootWindow());
  }
}

bool RootWindowController::IsVirtualKeyboardWindow(aura::Window* window) {
  aura::Window* parent = GetContainer(kShellWindowId_ImeWindowParentContainer);
  return parent ? parent->Contains(window) : false;
}

void RootWindowController::SetTouchAccessibilityAnchorPoint(
    const gfx::Point& anchor_point) {
  if (touch_exploration_manager_)
    touch_exploration_manager_->SetTouchAccessibilityAnchorPoint(anchor_point);
}

void RootWindowController::ShowContextMenu(const gfx::Point& location_in_screen,
                                           ui::MenuSourceType source_type) {
  ShellDelegate* delegate = Shell::Get()->shell_delegate();
  DCHECK(delegate);
  menu_model_.reset(delegate->CreateContextMenu(wm_shelf_.get(), nullptr));
  if (!menu_model_)
    return;

  menu_model_adapter_ = base::MakeUnique<views::MenuModelAdapter>(
      menu_model_.get(),
      base::Bind(&RootWindowController::OnMenuClosed, base::Unretained(this)));

  // The wallpaper controller may not be set yet if the user clicked on the
  // status area before the initial animation completion. See crbug.com/222218
  if (!wallpaper_widget_controller())
    return;

  menu_runner_ = base::MakeUnique<views::MenuRunner>(
      menu_model_adapter_->CreateMenu(), views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(wallpaper_widget_controller()->widget(), nullptr,
                          gfx::Rect(location_in_screen, gfx::Size()),
                          views::MENU_ANCHOR_TOPLEFT, source_type);
}

void RootWindowController::UpdateAfterLoginStatusChange(LoginStatus status) {
  StatusAreaWidget* status_area_widget =
      wm_shelf_->shelf_widget()->status_area_widget();
  if (status_area_widget)
    status_area_widget->UpdateAfterLoginStatusChange(status);
}

////////////////////////////////////////////////////////////////////////////////
// RootWindowController, private:

RootWindowController::RootWindowController(
    AshWindowTreeHost* ash_host,
    aura::WindowTreeHost* window_tree_host)
    : ash_host_(ash_host),
      mus_window_tree_host_(window_tree_host),
      window_tree_host_(ash_host ? ash_host->AsWindowTreeHost()
                                 : window_tree_host),
      wm_shelf_(base::MakeUnique<WmShelf>()) {
  DCHECK((ash_host && !window_tree_host) || (!ash_host && window_tree_host));

  if (!root_window_controllers_)
    root_window_controllers_ = new std::vector<RootWindowController*>;
  root_window_controllers_->push_back(this);

  aura::Window* root_window = GetRootWindow();
  GetRootWindowSettings(root_window)->controller = this;

  stacking_controller_.reset(new StackingController);
  aura::client::SetWindowParentingClient(root_window,
                                         stacking_controller_.get());
  capture_client_.reset(new ::wm::ScopedCaptureClient(root_window));
}

void RootWindowController::Init(RootWindowType root_window_type) {
  aura::Window* root_window = GetRootWindow();
  Shell* shell = Shell::Get();
  shell->InitRootWindow(root_window);

  CreateContainers();
  ShellPort::Get()->OnCreatedRootWindowContainers(this);

  CreateSystemWallpaper(root_window_type);

  InitLayoutManagers();
  InitTouchHuds();

  if (Shell::GetPrimaryRootWindowController()
          ->GetSystemModalLayoutManager(nullptr)
          ->has_window_dimmer()) {
    GetSystemModalLayoutManager(nullptr)->CreateModalBackground();
  }

  shell->AddShellObserver(this);

  root_window_layout_manager_->OnWindowResized();
  if (root_window_type == RootWindowType::PRIMARY) {
    if (Shell::GetAshConfig() != Config::MASH)
      shell->InitKeyboard();
  } else {
    window_tree_host_->Show();

    // At the login screen the shelf will be hidden because its container window
    // is hidden. InitializeShelf() will make it visible.
    InitializeShelf();

    // Notify shell observers about new root window.
    shell->OnRootWindowAdded(WmWindow::Get(root_window));
  }

  // TODO: AshTouchExplorationManager doesn't work with mus.
  // http://crbug.com/679782
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kAshDisableTouchExplorationMode) &&
      Shell::GetAshConfig() != Config::MASH) {
    touch_exploration_manager_.reset(new AshTouchExplorationManager(this));
  }
}

void RootWindowController::InitLayoutManagers() {
  // Create the shelf and status area widgets.
  DCHECK(!wm_shelf_->shelf_widget());
  GetShelf()->CreateShelfWidget(GetWindow());

  aura::Window* root = GetRootWindow();
  root_window_layout_manager_ = new wm::RootWindowLayoutManager(root);
  root->SetLayoutManager(root_window_layout_manager_);

  aura::Window* default_container =
      GetContainer(kShellWindowId_DefaultContainer);
  // Installs WorkspaceLayoutManager on |default_container|.
  workspace_controller_.reset(new WorkspaceController(default_container));

  aura::Window* modal_container =
      GetContainer(kShellWindowId_SystemModalContainer);
  modal_container->SetLayoutManager(
      new SystemModalContainerLayoutManager(modal_container));

  aura::Window* lock_modal_container =
      GetContainer(kShellWindowId_LockSystemModalContainer);
  DCHECK(lock_modal_container);
  lock_modal_container->SetLayoutManager(
      new SystemModalContainerLayoutManager(lock_modal_container));

  aura::Window* lock_container =
      GetContainer(kShellWindowId_LockScreenContainer);
  DCHECK(lock_container);
  lock_container->SetLayoutManager(new LockLayoutManager(lock_container));

  WmWindow* always_on_top_container =
      GetWmContainer(kShellWindowId_AlwaysOnTopContainer);
  DCHECK(always_on_top_container);
  always_on_top_controller_ =
      base::MakeUnique<AlwaysOnTopController>(always_on_top_container);

  // Create Panel layout manager
  WmWindow* wm_panel_container = GetWmContainer(kShellWindowId_PanelContainer);
  panel_layout_manager_ = new PanelLayoutManager(wm_panel_container);
  wm_panel_container->aura_window()->SetLayoutManager(panel_layout_manager_);

  wm::WmSnapToPixelLayoutManager::InstallOnContainers(root);

  // Make it easier to resize windows that partially overlap the shelf. Must
  // occur after the ShelfLayoutManager is constructed by ShelfWidget.
  aura::Window* shelf_container = GetContainer(kShellWindowId_ShelfContainer);
  WmWindow* wm_shelf_container = WmWindow::Get(shelf_container);
  shelf_container->SetEventTargeter(base::MakeUnique<ShelfWindowTargeter>(
      wm_shelf_container, wm_shelf_.get()));
  aura::Window* status_container = GetContainer(kShellWindowId_StatusContainer);
  WmWindow* wm_status_container = WmWindow::Get(status_container);
  status_container->SetEventTargeter(base::MakeUnique<ShelfWindowTargeter>(
      wm_status_container, wm_shelf_.get()));

  panel_container_handler_ = base::MakeUnique<PanelWindowEventHandler>();
  GetContainer(kShellWindowId_PanelContainer)
      ->AddPreTargetHandler(panel_container_handler_.get());

  // Install an AttachedPanelWindowTargeter on the panel container to make it
  // easier to correctly target shelf buttons with touch.
  gfx::Insets mouse_extend(-kResizeOutsideBoundsSize, -kResizeOutsideBoundsSize,
                           -kResizeOutsideBoundsSize,
                           -kResizeOutsideBoundsSize);
  gfx::Insets touch_extend =
      mouse_extend.Scale(kResizeOutsideBoundsScaleForTouch);
  aura::Window* panel_container = GetContainer(kShellWindowId_PanelContainer);
  panel_container->SetEventTargeter(std::unique_ptr<ui::EventTargeter>(
      new AttachedPanelWindowTargeter(panel_container, mouse_extend,
                                      touch_extend, panel_layout_manager())));
}

void RootWindowController::CreateContainers() {
  WmWindow* root = GetWindow();
  // For screen rotation animation: add a NOT_DRAWN layer in between the
  // root_window's layer and its current children so that we only need to
  // initiate two LayerAnimationSequences. One for the new layers and one for
  // the old layers.
  WmWindow* screen_rotation_container = CreateContainer(
      kShellWindowId_ScreenRotationContainer, "ScreenRotationContainer", root);

  // These containers are just used by PowerButtonController to animate groups
  // of containers simultaneously without messing up the current transformations
  // on those containers. These are direct children of the
  // screen_rotation_container window; all of the other containers are their
  // children.

  // The wallpaper container is not part of the lock animation, so it is not
  // included in those animate groups. When the screen is locked, the wallpaper
  // is moved to the lock screen wallpaper container (and moved back on unlock).
  // Ensure that there's an opaque layer occluding the non-lock-screen layers.
  WmWindow* wallpaper_container =
      CreateContainer(kShellWindowId_WallpaperContainer, "WallpaperContainer",
                      screen_rotation_container);
  wallpaper_container->SetChildWindowVisibilityChangesAnimated();

  WmWindow* non_lock_screen_containers = CreateContainer(
      kShellWindowId_NonLockScreenContainersContainer,
      "NonLockScreenContainersContainer", screen_rotation_container);
  // Clip all windows inside this container, as half pixel of the window's
  // texture may become visible when the screen is scaled. crbug.com/368591.
  non_lock_screen_containers->SetMasksToBounds(true);

  WmWindow* lock_wallpaper_containers = CreateContainer(
      kShellWindowId_LockScreenWallpaperContainer,
      "LockScreenWallpaperContainer", screen_rotation_container);
  lock_wallpaper_containers->SetChildWindowVisibilityChangesAnimated();

  WmWindow* lock_screen_containers = CreateContainer(
      kShellWindowId_LockScreenContainersContainer,
      "LockScreenContainersContainer", screen_rotation_container);
  WmWindow* lock_screen_related_containers = CreateContainer(
      kShellWindowId_LockScreenRelatedContainersContainer,
      "LockScreenRelatedContainersContainer", screen_rotation_container);

  CreateContainer(kShellWindowId_UnparentedControlContainer,
                  "UnparentedControlContainer", non_lock_screen_containers);

  WmWindow* default_container =
      CreateContainer(kShellWindowId_DefaultContainer, "DefaultContainer",
                      non_lock_screen_containers);
  default_container->SetChildWindowVisibilityChangesAnimated();
  default_container->SetSnapsChildrenToPhysicalPixelBoundary();
  default_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);
  default_container->SetChildrenUseExtendedHitRegion();

  WmWindow* always_on_top_container =
      CreateContainer(kShellWindowId_AlwaysOnTopContainer,
                      "AlwaysOnTopContainer", non_lock_screen_containers);
  always_on_top_container->SetChildWindowVisibilityChangesAnimated();
  always_on_top_container->SetSnapsChildrenToPhysicalPixelBoundary();
  always_on_top_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);

  WmWindow* shelf_container =
      CreateContainer(kShellWindowId_ShelfContainer, "ShelfContainer",
                      non_lock_screen_containers);
  shelf_container->SetSnapsChildrenToPhysicalPixelBoundary();
  shelf_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);
  shelf_container->SetLockedToRoot(true);

  WmWindow* panel_container =
      CreateContainer(kShellWindowId_PanelContainer, "PanelContainer",
                      non_lock_screen_containers);
  panel_container->SetSnapsChildrenToPhysicalPixelBoundary();
  panel_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);

  WmWindow* shelf_bubble_container =
      CreateContainer(kShellWindowId_ShelfBubbleContainer,
                      "ShelfBubbleContainer", non_lock_screen_containers);
  shelf_bubble_container->SetSnapsChildrenToPhysicalPixelBoundary();
  shelf_bubble_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);
  shelf_bubble_container->SetLockedToRoot(true);

  WmWindow* app_list_container =
      CreateContainer(kShellWindowId_AppListContainer, "AppListContainer",
                      non_lock_screen_containers);
  app_list_container->SetSnapsChildrenToPhysicalPixelBoundary();
  app_list_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);

  WmWindow* modal_container =
      CreateContainer(kShellWindowId_SystemModalContainer,
                      "SystemModalContainer", non_lock_screen_containers);
  modal_container->SetSnapsChildrenToPhysicalPixelBoundary();
  modal_container->SetChildWindowVisibilityChangesAnimated();
  modal_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);
  modal_container->SetChildrenUseExtendedHitRegion();

  // TODO(beng): Figure out if we can make this use
  // SystemModalContainerEventFilter instead of stops_event_propagation.
  WmWindow* lock_container =
      CreateContainer(kShellWindowId_LockScreenContainer, "LockScreenContainer",
                      lock_screen_containers);
  lock_container->SetSnapsChildrenToPhysicalPixelBoundary();
  lock_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);
  // TODO(beng): stopsevents

  WmWindow* lock_modal_container =
      CreateContainer(kShellWindowId_LockSystemModalContainer,
                      "LockSystemModalContainer", lock_screen_containers);
  lock_modal_container->SetSnapsChildrenToPhysicalPixelBoundary();
  lock_modal_container->SetChildWindowVisibilityChangesAnimated();
  lock_modal_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);
  lock_modal_container->SetChildrenUseExtendedHitRegion();

  WmWindow* status_container =
      CreateContainer(kShellWindowId_StatusContainer, "StatusContainer",
                      lock_screen_related_containers);
  status_container->SetSnapsChildrenToPhysicalPixelBoundary();
  status_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);
  status_container->SetLockedToRoot(true);

  WmWindow* settings_bubble_container =
      CreateContainer(kShellWindowId_SettingBubbleContainer,
                      "SettingBubbleContainer", lock_screen_related_containers);
  settings_bubble_container->SetChildWindowVisibilityChangesAnimated();
  settings_bubble_container->SetSnapsChildrenToPhysicalPixelBoundary();
  settings_bubble_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);
  settings_bubble_container->SetLockedToRoot(true);

  WmWindow* virtual_keyboard_parent_container = CreateContainer(
      kShellWindowId_ImeWindowParentContainer, "VirtualKeyboardParentContainer",
      lock_screen_related_containers);
  virtual_keyboard_parent_container->SetSnapsChildrenToPhysicalPixelBoundary();
  virtual_keyboard_parent_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);

  WmWindow* menu_container =
      CreateContainer(kShellWindowId_MenuContainer, "MenuContainer",
                      lock_screen_related_containers);
  menu_container->SetChildWindowVisibilityChangesAnimated();
  menu_container->SetSnapsChildrenToPhysicalPixelBoundary();
  menu_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);

  WmWindow* drag_drop_container = CreateContainer(
      kShellWindowId_DragImageAndTooltipContainer,
      "DragImageAndTooltipContainer", lock_screen_related_containers);
  drag_drop_container->SetChildWindowVisibilityChangesAnimated();
  drag_drop_container->SetSnapsChildrenToPhysicalPixelBoundary();
  drag_drop_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);

  WmWindow* overlay_container =
      CreateContainer(kShellWindowId_OverlayContainer, "OverlayContainer",
                      lock_screen_related_containers);
  overlay_container->SetSnapsChildrenToPhysicalPixelBoundary();
  overlay_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);

  WmWindow* mouse_cursor_container =
      CreateContainer(kShellWindowId_MouseCursorContainer,
                      "MouseCursorContainer", screen_rotation_container);
  mouse_cursor_container->SetBoundsInScreenBehaviorForChildren(
      WmWindow::BoundsInScreenBehavior::USE_SCREEN_COORDINATES);

  CreateContainer(kShellWindowId_PowerButtonAnimationContainer,
                  "PowerButtonAnimationContainer", screen_rotation_container);
}

void RootWindowController::CreateSystemWallpaper(
    RootWindowType root_window_type) {
  SkColor color = SK_ColorBLACK;
  // The splash screen appears on the primary display at boot. If this is a
  // secondary monitor (either connected at boot or connected later) or if the
  // browser restarted for a second login then don't use the boot color.
  const bool is_boot_splash_screen =
      root_window_type == RootWindowType::PRIMARY &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          chromeos::switches::kFirstExecAfterBoot);
  if (is_boot_splash_screen)
    color = kChromeOsBootColor;
  system_wallpaper_.reset(
      new SystemWallpaperController(GetRootWindow(), color));

  // Make a copy of the system's boot splash screen so we can composite it
  // onscreen until the wallpaper is ready.
  if (is_boot_splash_screen &&
      (base::CommandLine::ForCurrentProcess()->HasSwitch(
           switches::kAshCopyHostBackgroundAtBoot) ||
       base::CommandLine::ForCurrentProcess()->HasSwitch(
           switches::kAshAnimateFromBootSplashScreen)))
    boot_splash_screen_.reset(new BootSplashScreen(GetHost()));
}

void RootWindowController::EnableTouchHudProjection() {
  if (touch_hud_projection_)
    return;
  set_touch_hud_projection(new TouchHudProjection(GetRootWindow()));
}

void RootWindowController::DisableTouchHudProjection() {
  if (!touch_hud_projection_)
    return;
  touch_hud_projection_->Remove();
}

void RootWindowController::ResetRootForNewWindowsIfNecessary() {
  // Change the target root window before closing child windows. If any child
  // being removed triggers a relayout of the shelf it will try to build a
  // window list adding windows from the target root window's containers which
  // may have already gone away.
  aura::Window* root = GetRootWindow();
  if (Shell::GetRootWindowForNewWindows() == root) {
    // The root window for new windows is being destroyed. Switch to the primary
    // root window if possible.
    aura::Window* primary_root = Shell::GetPrimaryRootWindow();
    Shell::Get()->set_root_window_for_new_windows(
        primary_root == root ? nullptr : primary_root);
  }
}

void RootWindowController::OnMenuClosed() {
  menu_runner_.reset();
  menu_model_adapter_.reset();
  menu_model_.reset();
  wm_shelf_->UpdateVisibilityState();
}

void RootWindowController::OnTouchHudProjectionToggled(bool enabled) {
  if (enabled)
    EnableTouchHudProjection();
  else
    DisableTouchHudProjection();
}

RootWindowController* GetRootWindowController(const aura::Window* root_window) {
  return root_window ? GetRootWindowSettings(root_window)->controller : nullptr;
}

}  // namespace ash
