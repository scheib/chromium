// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "chrome/browser/ui/ash/launcher/arc_app_window_launcher_controller.h"

#include <string>

#include "ash/display/screen_orientation_controller_chromeos.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/shared/app_types.h"
#include "ash/shelf/shelf_model.h"
#include "ash/shell.h"
#include "ash/wm/maximize_mode/maximize_mode_controller.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm_window.h"
#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/chromeos/arc/arc_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"
#include "chrome/browser/ui/ash/launcher/arc_app_window_launcher_item_controller.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager.h"
#include "components/arc/arc_bridge_service.h"
#include "components/exo/shell_surface.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/user_manager/user_manager.h"
#include "third_party/WebKit/public/platform/modules/screen_orientation/WebScreenOrientationLockType.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/env.h"
#include "ui/base/base_window.h"
#include "ui/display/display.h"
#include "ui/display/manager/display_manager.h"
#include "ui/views/widget/widget.h"

namespace {

enum class FullScreenMode {
  NOT_DEFINED,  // Fullscreen mode was not defined.
  ACTIVE,       // Fullscreen is activated for an app.
  NON_ACTIVE,   // Fullscreen was not activated for an app.
};

blink::WebScreenOrientationLockType BlinkOrientationLockFromMojom(
    arc::mojom::OrientationLock orientation_lock) {
  DCHECK_NE(arc::mojom::OrientationLock::CURRENT, orientation_lock);
  switch (orientation_lock) {
    case arc::mojom::OrientationLock::PORTRAIT:
      return blink::kWebScreenOrientationLockPortrait;
    case arc::mojom::OrientationLock::LANDSCAPE:
      return blink::kWebScreenOrientationLockLandscape;
    case arc::mojom::OrientationLock::PORTRAIT_PRIMARY:
      return blink::kWebScreenOrientationLockPortraitPrimary;
    case arc::mojom::OrientationLock::LANDSCAPE_PRIMARY:
      return blink::kWebScreenOrientationLockLandscapePrimary;
    case arc::mojom::OrientationLock::PORTRAIT_SECONDARY:
      return blink::kWebScreenOrientationLockPortraitSecondary;
    case arc::mojom::OrientationLock::LANDSCAPE_SECONDARY:
      return blink::kWebScreenOrientationLockLandscapeSecondary;
    default:
      return blink::kWebScreenOrientationLockAny;
  }
}

}  // namespace

using ash::ScreenOrientationController;

// The information about the arc application window which has to be kept
// even when its AppWindow is not present.
class ArcAppWindowLauncherController::AppWindowInfo {
 public:
  explicit AppWindowInfo(const arc::ArcAppShelfId& app_shelf_id)
      : app_shelf_id_(app_shelf_id) {}
  ~AppWindowInfo() = default;

  const arc::ArcAppShelfId& app_shelf_id() const { return app_shelf_id_; }

  bool has_requested_orientation_lock() const {
    return has_requested_orientation_lock_;
  }

  void set_requested_orientation_lock(arc::mojom::OrientationLock lock) {
    has_requested_orientation_lock_ = true;
    requested_orientation_lock_ = lock;
  }

  arc::mojom::OrientationLock requested_orientation_lock() const {
    return requested_orientation_lock_;
  }

  void set_lock_completion_behavior(
      ScreenOrientationController::LockCompletionBehavior lock_behavior) {
    lock_completion_behavior_ = lock_behavior;
  }

  ScreenOrientationController::LockCompletionBehavior lock_completion_behavior()
      const {
    return lock_completion_behavior_;
  }

  void set_app_window(std::unique_ptr<AppWindow> window) {
    app_window_ = std::move(window);
  }

  AppWindow* app_window() { return app_window_.get(); }

 private:
  const arc::ArcAppShelfId app_shelf_id_;
  bool has_requested_orientation_lock_ = false;

  // If true, the orientation should be locked to the specific
  // orientation after the requested_orientation_lock is applied.
  // This is meaningful only if the orientation is one of ::NONE,
  // ::PORTRAIT or ::LANDSCAPE.
  ScreenOrientationController::LockCompletionBehavior
      lock_completion_behavior_ =
          ScreenOrientationController::LockCompletionBehavior::None;
  arc::mojom::OrientationLock requested_orientation_lock_ =
      arc::mojom::OrientationLock::NONE;
  std::unique_ptr<AppWindow> app_window_;

  DISALLOW_COPY_AND_ASSIGN(AppWindowInfo);
};

// A ui::BaseWindow for a chromeos launcher to control ARC applications.
class ArcAppWindowLauncherController::AppWindow : public ui::BaseWindow {
 public:
  AppWindow(int task_id,
            const arc::ArcAppShelfId& app_shelf_id,
            views::Widget* widget,
            ArcAppWindowLauncherController* owner)
      : task_id_(task_id),
        app_shelf_id_(app_shelf_id),
        widget_(widget),
        owner_(owner) {}
  ~AppWindow() = default;

  void SetController(ArcAppWindowLauncherItemController* controller) {
    DCHECK(!controller_ && controller);
    controller_ = controller;
  }

  void ResetController() { controller_ = nullptr; }

  void SetFullscreenMode(FullScreenMode mode) {
    DCHECK(mode != FullScreenMode::NOT_DEFINED);
    fullscreen_mode_ = mode;
  }

  FullScreenMode fullscreen_mode() const { return fullscreen_mode_; }

  int task_id() const { return task_id_; }

  const arc::ArcAppShelfId& app_shelf_id() const { return app_shelf_id_; }

  const ash::ShelfID& shelf_id() const { return shelf_id_; }

  void set_shelf_id(const ash::ShelfID& shelf_id) { shelf_id_ = shelf_id; }

  views::Widget* widget() const { return widget_; }

  ArcAppWindowLauncherItemController* controller() { return controller_; }

  // ui::BaseWindow:
  bool IsActive() const override {
    return widget_->IsActive() && owner_->active_task_id_ == task_id_;
  }

  bool IsMaximized() const override {
    NOTREACHED();
    return false;
  }

  bool IsMinimized() const override {
    NOTREACHED();
    return false;
  }

  bool IsFullscreen() const override {
    NOTREACHED();
    return false;
  }

  gfx::NativeWindow GetNativeWindow() const override {
    return widget_->GetNativeWindow();
  }

  gfx::Rect GetRestoredBounds() const override {
    NOTREACHED();
    return gfx::Rect();
  }

  ui::WindowShowState GetRestoredState() const override {
    NOTREACHED();
    return ui::SHOW_STATE_NORMAL;
  }

  gfx::Rect GetBounds() const override {
    NOTREACHED();
    return gfx::Rect();
  }

  void Show() override { widget_->Show(); }

  void ShowInactive() override { NOTREACHED(); }

  void Hide() override { NOTREACHED(); }

  void Close() override { arc::CloseTask(task_id_); }

  void Activate() override { widget_->Activate(); }

  void Deactivate() override { NOTREACHED(); }

  void Maximize() override { NOTREACHED(); }

  void Minimize() override { widget_->Minimize(); }

  void Restore() override { NOTREACHED(); }

  void SetBounds(const gfx::Rect& bounds) override { NOTREACHED(); }

  void FlashFrame(bool flash) override { NOTREACHED(); }

  bool IsAlwaysOnTop() const override {
    NOTREACHED();
    return false;
  }

  void SetAlwaysOnTop(bool always_on_top) override { NOTREACHED(); }

 private:
  const int task_id_;
  const arc::ArcAppShelfId app_shelf_id_;
  ash::ShelfID shelf_id_;
  FullScreenMode fullscreen_mode_ = FullScreenMode::NOT_DEFINED;
  // Unowned pointers
  views::Widget* const widget_;
  ArcAppWindowLauncherController* owner_;
  ArcAppWindowLauncherItemController* controller_ = nullptr;
  // Unowned pointer, represents host ARC window.

  DISALLOW_COPY_AND_ASSIGN(AppWindow);
};

ArcAppWindowLauncherController::ArcAppWindowLauncherController(
    ChromeLauncherController* owner)
    : AppWindowLauncherController(owner) {
  if (arc::IsArcAllowedForProfile(owner->profile())) {
    observed_profile_ = owner->profile();
    StartObserving(observed_profile_);
  }
}

ArcAppWindowLauncherController::~ArcAppWindowLauncherController() {
  if (observed_profile_)
    StopObserving(observed_profile_);
  if (observing_shell_)
    ash::Shell::Get()->RemoveShellObserver(this);
}

void ArcAppWindowLauncherController::ActiveUserChanged(
    const std::string& user_email) {
  const std::string& primary_user_email = user_manager::UserManager::Get()
                                              ->GetPrimaryUser()
                                              ->GetAccountId()
                                              .GetUserEmail();
  if (user_email == primary_user_email) {
    // Restore existing ARC window and create controllers for them.
    AttachControllerToWindowsIfNeeded();

    // Make sure that we created items for all apps, not only which have a
    // window.
    for (const auto& info : task_id_to_app_window_info_)
      AttachControllerToTask(info.first, *info.second);

    // Update active status.
    OnTaskSetActive(active_task_id_);
  } else {
    // Remove all ARC apps and destroy its controllers. There is no mapping
    // task id to app window because it is not safe when controller is missing.
    for (auto& it : task_id_to_app_window_info_)
      UnregisterApp(it.second.get());

    // Some controllers might have no windows attached, for example background
    // task when foreground tasks is in full screen.
    for (const auto& it : app_shelf_group_to_controller_map_)
      owner()->CloseLauncherItem(it.second->shelf_id());
    app_shelf_group_to_controller_map_.clear();
  }
}

void ArcAppWindowLauncherController::AdditionalUserAddedToSession(
    Profile* profile) {
  DCHECK(!arc::IsArcAllowedForProfile(profile));
}

void ArcAppWindowLauncherController::OnWindowInitialized(aura::Window* window) {
  // An arc window has type WINDOW_TYPE_NORMAL, a WindowDelegate and
  // is a top level views widget.
  if (window->type() != ui::wm::WINDOW_TYPE_NORMAL || !window->delegate())
    return;
  views::Widget* widget = views::Widget::GetWidgetForNativeWindow(window);
  if (!widget || !widget->is_top_level())
    return;
  observed_windows_.push_back(window);
  window->AddObserver(this);
}

void ArcAppWindowLauncherController::OnWindowVisibilityChanged(
    aura::Window* window,
    bool visible) {
  // Attach window to multi-user manager now to let it manage visibility state
  // of the ARC window correctly.
  if (GetWindowTaskId(window) > 0) {
    chrome::MultiUserWindowManager::GetInstance()->SetWindowOwner(
        window,
        user_manager::UserManager::Get()->GetPrimaryUser()->GetAccountId());
  }

  // The application id property should be set at this time. It is important to
  // have window->IsVisible set to true before attaching to a controller because
  // the window is registered in multi-user manager and this manager may
  // consider this new window as hidden for current profile. Multi-user manager
  // uses OnWindowVisibilityChanging event to update window state.
  if (visible && observed_profile_ == owner()->profile())
    AttachControllerToWindowIfNeeded(window);
}

void ArcAppWindowLauncherController::OnWindowDestroying(aura::Window* window) {
  auto it =
      std::find(observed_windows_.begin(), observed_windows_.end(), window);
  DCHECK(it != observed_windows_.end());
  observed_windows_.erase(it);
  window->RemoveObserver(this);

  auto info_it = std::find_if(
      task_id_to_app_window_info_.begin(), task_id_to_app_window_info_.end(),
      [window](TaskIdToAppWindowInfo::value_type& pair) {
        return pair.second->app_window() &&
               pair.second->app_window()->GetNativeWindow() == window;
      });
  if (info_it != task_id_to_app_window_info_.end()) {
    // Note, window may be recreated in some cases, so do not close controller
    // on window destroying. Controller will be closed onTaskDestroyed event
    // which is generated when actual task is destroyed.
    UnregisterApp(info_it->second.get());
  }
}

ArcAppWindowLauncherController::AppWindowInfo*
ArcAppWindowLauncherController::GetAppWindowInfoForTask(int task_id) {
  const auto it = task_id_to_app_window_info_.find(task_id);
  return it == task_id_to_app_window_info_.end() ? nullptr : it->second.get();
}

ArcAppWindowLauncherController::AppWindow*
ArcAppWindowLauncherController::GetAppWindowForTask(int task_id) {
  AppWindowInfo* info = GetAppWindowInfoForTask(task_id);
  return info ? info->app_window() : nullptr;
}

void ArcAppWindowLauncherController::AttachControllerToWindowsIfNeeded() {
  for (auto* window : observed_windows_)
    AttachControllerToWindowIfNeeded(window);
}

void ArcAppWindowLauncherController::AttachControllerToWindowIfNeeded(
    aura::Window* window) {
  const int task_id = GetWindowTaskId(window);
  if (task_id <= 0)
    return;

  // We need to add the observer after exo started observing shell
  // because we want to update the orientation after exo send
  // the layout switch information.
  if (!observing_shell_) {
    observing_shell_ = true;
    ash::Shell::Get()->AddShellObserver(this);
  }

  // Check if we have controller for this task.
  if (GetAppWindowForTask(task_id))
    return;

  window->SetProperty(aura::client::kAppType,
                      static_cast<int>(ash::AppType::ARC_APP));

  // Create controller if we have task info.
  AppWindowInfo* info = GetAppWindowInfoForTask(task_id);
  if (!info) {
    VLOG(1) << "Could not find AppWindowInfo for task:" << task_id;
    return;
  }

  views::Widget* widget = views::Widget::GetWidgetForNativeWindow(window);
  DCHECK(widget);
  DCHECK(!info->app_window());
  info->set_app_window(
      base::MakeUnique<AppWindow>(task_id, info->app_shelf_id(), widget, this));
  RegisterApp(info);
  DCHECK(info->app_window()->controller());
  const ash::ShelfID shelf_id(info->app_window()->shelf_id());
  window->SetProperty(ash::kShelfIDKey, new std::string(shelf_id.Serialize()));
  if (ash::Shell::Get()
          ->maximize_mode_controller()
          ->IsMaximizeModeWindowManagerEnabled()) {
    SetOrientationLockForAppWindow(info->app_window());
  }
}

void ArcAppWindowLauncherController::OnAppReadyChanged(
    const std::string& arc_app_id,
    bool ready) {
  if (!ready)
    OnAppRemoved(arc_app_id);
}

std::vector<int> ArcAppWindowLauncherController::GetTaskIdsForApp(
    const std::string& arc_app_id) const {
  // Note, AppWindow is optional part for a task and it may be not created if
  // another full screen Android app is currently active. Use
  // |task_id_to_app_window_info_| that keeps currently running tasks info.
  std::vector<int> task_ids;
  for (const auto& it : task_id_to_app_window_info_) {
    const AppWindowInfo* app_window_info = it.second.get();
    if (app_window_info->app_shelf_id().app_id() == arc_app_id)
      task_ids.push_back(it.first);
  }

  return task_ids;
}

void ArcAppWindowLauncherController::OnAppRemoved(
    const std::string& arc_app_id) {
  const std::vector<int> task_ids_to_remove = GetTaskIdsForApp(arc_app_id);
  for (const auto task_id : task_ids_to_remove)
    OnTaskDestroyed(task_id);
  DCHECK(GetTaskIdsForApp(arc_app_id).empty());
}

void ArcAppWindowLauncherController::OnTaskCreated(
    int task_id,
    const std::string& package_name,
    const std::string& activity_name,
    const std::string& intent) {
  DCHECK(!GetAppWindowForTask(task_id));
  const std::string arc_app_id =
      ArcAppListPrefs::GetAppId(package_name, activity_name);
  const arc::ArcAppShelfId arc_app_shelf_id =
      arc::ArcAppShelfId::FromIntentAndAppId(intent, arc_app_id);
  task_id_to_app_window_info_[task_id] =
      base::MakeUnique<AppWindowInfo>(arc_app_shelf_id);
  // Don't create shelf icon for non-primary user.
  if (observed_profile_ != owner()->profile())
    return;

  AttachControllerToWindowsIfNeeded();

  // Some tasks can be started in background and might have no window until
  // pushed to the front. We need its representation on the shelf to give a user
  // control over it.
  AttachControllerToTask(task_id, *task_id_to_app_window_info_[task_id]);
}

void ArcAppWindowLauncherController::OnTaskDestroyed(int task_id) {
  auto it = task_id_to_app_window_info_.find(task_id);
  if (it == task_id_to_app_window_info_.end())
    return;

  UnregisterApp(it->second.get());

  // Check if we may close controller now, at this point we can safely remove
  // controllers without window.
  auto it_controller =
      app_shelf_group_to_controller_map_.find(it->second->app_shelf_id());
  if (it_controller != app_shelf_group_to_controller_map_.end()) {
    it_controller->second->RemoveTaskId(task_id);
    if (!it_controller->second->HasAnyTasks()) {
      owner()->CloseLauncherItem(it_controller->second->shelf_id());
      app_shelf_group_to_controller_map_.erase(it_controller);
    }
  }

  task_id_to_app_window_info_.erase(it);
}

void ArcAppWindowLauncherController::OnTaskSetActive(int32_t task_id) {
  if (observed_profile_ != owner()->profile()) {
    active_task_id_ = task_id;
    return;
  }

  AppWindow* previous_app_window = GetAppWindowForTask(active_task_id_);
  if (previous_app_window) {
    owner()->SetItemStatus(previous_app_window->shelf_id(),
                           ash::STATUS_RUNNING);
    previous_app_window->SetFullscreenMode(
        previous_app_window->widget() &&
                previous_app_window->widget()->IsFullscreen()
            ? FullScreenMode::ACTIVE
            : FullScreenMode::NON_ACTIVE);
  }

  active_task_id_ = task_id;

  AppWindow* current_app_window = GetAppWindowForTask(task_id);
  if (current_app_window) {
    owner()->SetItemStatus(
        current_app_window->shelf_id(),
        current_app_window->widget() && current_app_window->IsActive()
            ? ash::STATUS_ACTIVE
            : ash::STATUS_RUNNING);
    // TODO(reveman): Figure out how to support fullscreen in interleaved
    // window mode.
    // if (new_active_app_it->second->widget()) {
    //   new_active_app_it->second->widget()->SetFullscreen(
    //       new_active_app_it->second->fullscreen_mode() ==
    //       FullScreenMode::ACTIVE);
    // }
  }
}

void ArcAppWindowLauncherController::OnTaskOrientationLockRequested(
    int32_t task_id,
    const arc::mojom::OrientationLock orientation_lock) {
  // Don't save to AppInfo in prefs because this is requested in runtime.
  AppWindowInfo* info = GetAppWindowInfoForTask(task_id);
  DCHECK(info);
  if (!info)
    return;

  if (orientation_lock == arc::mojom::OrientationLock::CURRENT) {
    info->set_lock_completion_behavior(
        ScreenOrientationController::LockCompletionBehavior::DisableSensor);
    if (!info->has_requested_orientation_lock()) {
      info->set_requested_orientation_lock(arc::mojom::OrientationLock::NONE);
    }
  } else {
    info->set_requested_orientation_lock(orientation_lock);
    info->set_lock_completion_behavior(
        ScreenOrientationController::LockCompletionBehavior::None);
  }

  if (ash::Shell::Get()
          ->maximize_mode_controller()
          ->IsMaximizeModeWindowManagerEnabled()) {
    AppWindow* app_window = info->app_window();
    if (app_window)
      SetOrientationLockForAppWindow(app_window);
  }
}

AppWindowLauncherItemController*
ArcAppWindowLauncherController::ControllerForWindow(aura::Window* window) {
  if (!window)
    return nullptr;

  AppWindow* app_window = GetAppWindowForTask(active_task_id_);
  if (app_window &&
      app_window->widget() == views::Widget::GetWidgetForNativeWindow(window)) {
    return app_window->controller();
  }

  for (auto& it : task_id_to_app_window_info_) {
    AppWindow* app_window = it.second->app_window();
    if (app_window &&
        app_window->widget() ==
            views::Widget::GetWidgetForNativeWindow(window)) {
      return it.second->app_window()->controller();
    }
  }

  return nullptr;
}

void ArcAppWindowLauncherController::OnWindowActivated(
    aura::client::ActivationChangeObserver::ActivationReason reason,
    aura::Window* gained_active,
    aura::Window* lost_active) {
  AppWindowLauncherController::OnWindowActivated(reason, gained_active,
                                                 lost_active);
  OnTaskSetActive(active_task_id_);
}

void ArcAppWindowLauncherController::OnMaximizeModeStarted() {
  for (auto& it : task_id_to_app_window_info_) {
    AppWindow* app_window = it.second->app_window();
    if (app_window)
      SetOrientationLockForAppWindow(app_window);
  }
}

void ArcAppWindowLauncherController::OnMaximizeModeEnded() {
  ash::ScreenOrientationController* orientation_controller =
      ash::Shell::Get()->screen_orientation_controller();
  // Don't unlock one by one because it'll switch to next rotation.
  orientation_controller->UnlockAll();
}

void ArcAppWindowLauncherController::StartObserving(Profile* profile) {
  aura::Env* env = aura::Env::GetInstanceDontCreate();
  if (env)
    env->AddObserver(this);
  ArcAppListPrefs* prefs = ArcAppListPrefs::Get(profile);
  DCHECK(prefs);
  prefs->AddObserver(this);
}

void ArcAppWindowLauncherController::StopObserving(Profile* profile) {
  for (auto* window : observed_windows_)
    window->RemoveObserver(this);
  ArcAppListPrefs* prefs = ArcAppListPrefs::Get(profile);
  prefs->RemoveObserver(this);
  aura::Env* env = aura::Env::GetInstanceDontCreate();
  if (env)
    env->RemoveObserver(this);
}

ArcAppWindowLauncherItemController*
ArcAppWindowLauncherController::AttachControllerToTask(
    int task_id,
    const AppWindowInfo& app_window_info) {
  const arc::ArcAppShelfId& app_shelf_id = app_window_info.app_shelf_id();
  const auto it = app_shelf_group_to_controller_map_.find(app_shelf_id);
  if (it != app_shelf_group_to_controller_map_.end()) {
    DCHECK_EQ(it->second->app_id(), app_shelf_id.ToString());
    it->second->AddTaskId(task_id);
    return it->second;
  }

  std::unique_ptr<ArcAppWindowLauncherItemController> controller =
      base::MakeUnique<ArcAppWindowLauncherItemController>(
          app_shelf_id.ToString());
  ArcAppWindowLauncherItemController* item_controller = controller.get();
  const ash::ShelfID shelf_id(app_shelf_id.ToString());
  if (!owner()->GetItem(shelf_id)) {
    owner()->CreateAppLauncherItem(std::move(controller), ash::STATUS_RUNNING);
  } else {
    owner()->shelf_model()->SetShelfItemDelegate(shelf_id,
                                                 std::move(controller));
    owner()->SetItemStatus(shelf_id, ash::STATUS_RUNNING);
  }
  item_controller->AddTaskId(task_id);
  app_shelf_group_to_controller_map_[app_shelf_id] = item_controller;
  return item_controller;
}

void ArcAppWindowLauncherController::RegisterApp(
    AppWindowInfo* app_window_info) {
  AppWindow* app_window = app_window_info->app_window();
  ArcAppWindowLauncherItemController* controller =
      AttachControllerToTask(app_window->task_id(), *app_window_info);
  DCHECK(!controller->app_id().empty());
  const ash::ShelfID shelf_id(controller->app_id());
  DCHECK(owner()->GetItem(shelf_id));

  controller->AddWindow(app_window);
  owner()->SetItemStatus(shelf_id, ash::STATUS_RUNNING);
  app_window->SetController(controller);
  app_window->set_shelf_id(shelf_id);
}

void ArcAppWindowLauncherController::UnregisterApp(
    AppWindowInfo* app_window_info) {
  AppWindow* app_window = app_window_info->app_window();
  if (!app_window)
    return;

  ArcAppWindowLauncherItemController* controller = app_window->controller();
  if (controller)
    controller->RemoveWindow(app_window);
  app_window->ResetController();
  app_window_info->set_app_window(nullptr);
}

void ArcAppWindowLauncherController::SetOrientationLockForAppWindow(
    AppWindow* app_window) {
  ash::WmWindow* window =
      ash::WmWindow::Get(app_window->widget()->GetNativeWindow());
  if (!window)
    return;
  AppWindowInfo* info = GetAppWindowInfoForTask(app_window->task_id());
  arc::mojom::OrientationLock orientation_lock;

  ScreenOrientationController::LockCompletionBehavior lock_completion_behavior =
      ScreenOrientationController::LockCompletionBehavior::None;
  if (info->has_requested_orientation_lock()) {
    orientation_lock = info->requested_orientation_lock();
    lock_completion_behavior = info->lock_completion_behavior();
  } else {
    ArcAppListPrefs* prefs = ArcAppListPrefs::Get(observed_profile_);
    std::unique_ptr<ArcAppListPrefs::AppInfo> app_info =
        prefs->GetApp(info->app_shelf_id().app_id());
    if (!app_info)
      return;
    orientation_lock = app_info->orientation_lock;
    if (orientation_lock == arc::mojom::OrientationLock::CURRENT) {
      orientation_lock = arc::mojom::OrientationLock::NONE;
      lock_completion_behavior =
          ScreenOrientationController::LockCompletionBehavior::DisableSensor;
    }
  }
  ash::Shell* shell = ash::Shell::Get();
  shell->screen_orientation_controller()->LockOrientationForWindow(
      window, BlinkOrientationLockFromMojom(orientation_lock),
      lock_completion_behavior);
}

// static
int ArcAppWindowLauncherController::GetWindowTaskId(aura::Window* window) {
  const std::string arc_app_id = exo::ShellSurface::GetApplicationId(window);
  if (arc_app_id.empty())
    return -1;

  int task_id = -1;
  if (sscanf(arc_app_id.c_str(), "org.chromium.arc.%d", &task_id) != 1)
    return -1;

  return task_id;
}
