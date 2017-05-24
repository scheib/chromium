// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/multi_user/user_switch_animator_chromeos.h"

#include "ash/root_window_controller.h"
#include "ash/shelf/shelf_layout_manager.h"
#include "ash/shelf/shelf_widget.h"
#include "ash/shelf/wm_shelf.h"
#include "ash/shell.h"
#include "ash/shell_port.h"
#include "ash/wallpaper/wallpaper_delegate.h"
#include "ash/wm/mru_window_tracker.h"
#include "ash/wm/window_positioner.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm_window.h"
#include "base/macros.h"
#include "chrome/browser/chromeos/login/users/wallpaper/wallpaper_manager.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_notification_blocker_chromeos.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager_chromeos.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/display/display.h"
#include "ui/wm/core/window_util.h"
#include "ui/wm/public/activation_client.h"

namespace chrome {

namespace {

// The minimal possible animation time for animations which should happen
// "instantly".
const int kMinimalAnimationTimeMS = 1;

// logic while the user gets switched.
class UserChangeActionDisabler {
 public:
  UserChangeActionDisabler() {
    ash::WindowPositioner::DisableAutoPositioning(true);
    ash::Shell::Get()->mru_window_tracker()->SetIgnoreActivations(true);
  }

  ~UserChangeActionDisabler() {
    ash::WindowPositioner::DisableAutoPositioning(false);
    ash::Shell::Get()->mru_window_tracker()->SetIgnoreActivations(false);
  }
 private:

  DISALLOW_COPY_AND_ASSIGN(UserChangeActionDisabler);
};

// Defines an animation watcher for the 'hide' animation of the first maximized
// window we encounter while looping through the old user's windows. This is
// to observe the end of the animation so that we can destruct the old detached
// layer of the window.
class MaximizedWindowAnimationWatcher : public ui::LayerAnimationObserver {
 public:
  MaximizedWindowAnimationWatcher(ui::LayerAnimator* animator_to_watch,
                                  ui::LayerTreeOwner* old_layer)
      : animator_(animator_to_watch), old_layer_(old_layer) {
    animator_->AddObserver(this);
  }

  ~MaximizedWindowAnimationWatcher() override {
    animator_->RemoveObserver(this);
  }

  // ui::LayerAnimationObserver:
  void OnLayerAnimationEnded(ui::LayerAnimationSequence* sequence) override {
    delete this;
  }

  void OnLayerAnimationAborted(ui::LayerAnimationSequence* sequence) override {
    delete this;
  }

  void OnLayerAnimationScheduled(
      ui::LayerAnimationSequence* sequence) override {}

 private:
  ui::LayerAnimator* animator_;
  std::unique_ptr<ui::LayerTreeOwner> old_layer_;

  DISALLOW_COPY_AND_ASSIGN(MaximizedWindowAnimationWatcher);
};

// Modifies the given |window_list| such that the most-recently used window (if
// any, and if it exists in |window_list|) will be the last window in the list.
void PutMruWindowLast(std::vector<aura::Window*>* window_list) {
  DCHECK(window_list);
  auto* active_window = ash::wm::GetActiveWindow();
  if (!active_window)
    return;

  auto itr = std::find(window_list->begin(), window_list->end(), active_window);
  if (itr != window_list->end()) {
    window_list->erase(itr);
    window_list->push_back(active_window);
  }
}

}  // namespace

UserSwitchAnimatorChromeOS::UserSwitchAnimatorChromeOS(
    MultiUserWindowManagerChromeOS* owner,
    const AccountId& new_account_id,
    int animation_speed_ms)
    : owner_(owner),
      new_account_id_(new_account_id),
      animation_speed_ms_(animation_speed_ms),
      animation_step_(ANIMATION_STEP_HIDE_OLD_USER),
      screen_cover_(GetScreenCover(NULL)),
      windows_by_account_id_() {
  ash::Shell::Get()->DismissAppList();
  BuildUserToWindowsListMap();
  AdvanceUserTransitionAnimation();

  if (!animation_speed_ms_) {
    FinalizeAnimation();
  } else {
    user_changed_animation_timer_.reset(new base::Timer(
        FROM_HERE,
        base::TimeDelta::FromMilliseconds(animation_speed_ms_),
        base::Bind(
            &UserSwitchAnimatorChromeOS::AdvanceUserTransitionAnimation,
            base::Unretained(this)),
        true));
    user_changed_animation_timer_->Reset();
  }
}

UserSwitchAnimatorChromeOS::~UserSwitchAnimatorChromeOS() {
  FinalizeAnimation();
}

// static
bool UserSwitchAnimatorChromeOS::CoversScreen(aura::Window* window) {
  // Full screen covers the screen naturally. Since a normal window can have the
  // same size as the work area, we only compare the bounds against the work
  // area.
  if (ash::wm::GetWindowState(window)->IsFullscreen())
    return true;
  gfx::Rect bounds = window->GetBoundsInRootWindow();
  gfx::Rect work_area =
      display::Screen::GetScreen()->GetDisplayNearestWindow(window).work_area();
  bounds.Intersect(work_area);
  return work_area == bounds;
}

void UserSwitchAnimatorChromeOS::AdvanceUserTransitionAnimation() {
  DCHECK_NE(animation_step_, ANIMATION_STEP_ENDED);

  TransitionWallpaper(animation_step_);
  TransitionUserShelf(animation_step_);
  TransitionWindows(animation_step_);

  // Advance to the next step.
  switch (animation_step_) {
    case ANIMATION_STEP_HIDE_OLD_USER:
      animation_step_ = ANIMATION_STEP_SHOW_NEW_USER;
      break;
    case ANIMATION_STEP_SHOW_NEW_USER:
      animation_step_ = ANIMATION_STEP_FINALIZE;
      break;
    case ANIMATION_STEP_FINALIZE:
      user_changed_animation_timer_.reset();
      animation_step_ = ANIMATION_STEP_ENDED;
      owner_->NotifyAfterUserSwitchAnimationFinished();
      break;
    case ANIMATION_STEP_ENDED:
      NOTREACHED();
      break;
  }
}

void UserSwitchAnimatorChromeOS::CancelAnimation() {
  animation_step_ = ANIMATION_STEP_ENDED;
}

void UserSwitchAnimatorChromeOS::FinalizeAnimation() {
  user_changed_animation_timer_.reset();
  while (ANIMATION_STEP_ENDED != animation_step_)
    AdvanceUserTransitionAnimation();
}

void UserSwitchAnimatorChromeOS::TransitionWallpaper(
    AnimationStep animation_step) {
  // Handle the wallpaper switch.
  ash::WallpaperDelegate* wallpaper_delegate =
      ash::Shell::Get()->wallpaper_delegate();
  if (animation_step == ANIMATION_STEP_HIDE_OLD_USER) {
    // Set the wallpaper cross dissolve animation duration to our complete
    // animation cycle for a fade in and fade out.
    int duration =
        NO_USER_COVERS_SCREEN == screen_cover_ ? (2 * animation_speed_ms_) : 0;
    wallpaper_delegate->SetAnimationDurationOverride(
        std::max(duration, kMinimalAnimationTimeMS));
    if (screen_cover_ != NEW_USER_COVERS_SCREEN) {
      chromeos::WallpaperManager::Get()->SetUserWallpaperNow(new_account_id_);
      wallpaper_user_id_for_test_ =
          (NO_USER_COVERS_SCREEN == screen_cover_ ? "->" : "") +
          new_account_id_.Serialize();
    }
  } else if (animation_step == ANIMATION_STEP_FINALIZE) {
    // Revert the wallpaper cross dissolve animation duration back to the
    // default.
    if (screen_cover_ == NEW_USER_COVERS_SCREEN)
      chromeos::WallpaperManager::Get()->SetUserWallpaperNow(new_account_id_);

    // Coming here the wallpaper user id is the final result. No matter how we
    // got here.
    wallpaper_user_id_for_test_ = new_account_id_.Serialize();
    wallpaper_delegate->SetAnimationDurationOverride(0);
  }
}

void UserSwitchAnimatorChromeOS::TransitionUserShelf(
    AnimationStep animation_step) {
  ChromeLauncherController* chrome_launcher_controller =
      ChromeLauncherController::instance();
  // The shelf animation duration override.
  int duration_override = animation_speed_ms_;
  // Handle the shelf order of items. This is done once the old user is hidden.
  if (animation_step == ANIMATION_STEP_SHOW_NEW_USER) {
    // Some unit tests have no ChromeLauncherController.
    if (chrome_launcher_controller)
      chrome_launcher_controller->ActiveUserChanged(
          new_account_id_.GetUserEmail());
    // Hide the black rectangle on top of each shelf again.
    for (aura::Window* window : ash::Shell::GetAllRootWindows()) {
      ash::ShelfWidget* shelf = ash::WmShelf::ForWindow(window)->shelf_widget();
      shelf->HideShelfBehindBlackBar(false, duration_override);
    }
    // We kicked off the shelf animation above and the override can be
    // removed.
    duration_override = 0;
  }

  if (!animation_speed_ms_ || animation_step == ANIMATION_STEP_FINALIZE)
    return;

  // Note: The animation duration override will be set before the old user gets
  // hidden and reset after the animations for the new user got kicked off.
  ash::Shell::RootWindowControllerList controller =
      ash::Shell::Get()->GetAllRootWindowControllers();
  for (ash::Shell::RootWindowControllerList::iterator iter = controller.begin();
       iter != controller.end(); ++iter) {
    (*iter)->GetShelfLayoutManager()->SetAnimationDurationOverride(
        duration_override);
  }

  if (animation_step != ANIMATION_STEP_HIDE_OLD_USER)
    return;

  // For each root window hide the shelf.
  for (aura::Window* window : ash::Shell::GetAllRootWindows()) {
    // Hiding the shelf will cause a resize on a maximized window.
    // If the shelf is then shown for the following user in the same location,
    // the window gets resized again. Since each resize can cause a considerable
    // CPU usage and therefore effect jank, we should avoid hiding the shelf if
    // the start and end location are the same and cover the shelf instead with
    // a black rectangle on top.
    ash::WmShelf* shelf = ash::WmShelf::ForWindow(window);
    if (GetScreenCover(window) != NO_USER_COVERS_SCREEN &&
        (!chrome_launcher_controller ||
         !chrome_launcher_controller->ShelfBoundsChangesProbablyWithUser(
             shelf, new_account_id_))) {
      shelf->shelf_widget()->HideShelfBehindBlackBar(true, duration_override);
    } else {
      // This shelf change is only part of the animation and will be updated by
      // ChromeLauncherController::ActiveUserChanged() to the new users value.
      // Note that the user preference will not be changed.
      shelf->SetAutoHideBehavior(ash::SHELF_AUTO_HIDE_ALWAYS_HIDDEN);
    }
  }
}

void UserSwitchAnimatorChromeOS::TransitionWindows(
    AnimationStep animation_step) {
  // Disable the window position manager and the MRU window tracker temporarily.
  UserChangeActionDisabler disabler;

  // Animation duration.
  int duration = std::max(kMinimalAnimationTimeMS, 2 * animation_speed_ms_);

  switch (animation_step) {
    case ANIMATION_STEP_HIDE_OLD_USER: {
      // Hide the old users.
      for (auto& user_pair : windows_by_account_id_) {
        auto& show_for_account_id = user_pair.first;
        if (show_for_account_id == new_account_id_)
          continue;

        bool found_foreground_maximized_window = false;

        // We hide the windows such that the MRU window is the last one to be
        // hidden, at which point all other windows have already been hidden,
        // and hence the FocusController will not be able to find a next
        // activateable window to restore focus to, and so we don't change
        // window order (crbug.com/424307).
        PutMruWindowLast(&(user_pair.second));
        for (auto* window : user_pair.second) {
          auto* window_state = ash::wm::GetWindowState(window);

          // Minimized visiting windows (minimized windows with an owner
          // different than that of the for_show_account_id) should retrun to
          // their
          // original owners' desktops.
          MultiUserWindowManagerChromeOS::WindowToEntryMap::const_iterator itr =
              owner_->window_to_entry().find(window);
          DCHECK(itr != owner_->window_to_entry().end());
          if (show_for_account_id != itr->second->owner() &&
              window_state->IsMinimized()) {
            owner_->ShowWindowForUserIntern(window, itr->second->owner());
            window_state->Unminimize();
            continue;
          }

          if (!found_foreground_maximized_window && CoversScreen(window) &&
              screen_cover_ == BOTH_USERS_COVER_SCREEN) {
            // Maximized windows should be hidden, but visually kept visible
            // in order to prevent showing the background while the animation is
            // in progress. Therefore we detach the old layer and recreate fresh
            // ones. The old layers will be destructed at the animation step
            // |ANIMATION_STEP_FINALIZE|.
            // old_layers_.push_back(wm::RecreateLayers(window));
            // We only want to do this for the first (foreground) maximized
            // window we encounter.
            found_foreground_maximized_window = true;
            ui::LayerTreeOwner* old_layer =
                wm::RecreateLayers(window).release();
            window->layer()->parent()->StackAtBottom(old_layer->root());
            new MaximizedWindowAnimationWatcher(window->layer()->GetAnimator(),
                                                old_layer);
          }

          owner_->SetWindowVisibility(window, false, duration);
        }
      }

      // Show new user.
      auto new_user_itr = windows_by_account_id_.find(new_account_id_);
      if (new_user_itr == windows_by_account_id_.end())
        return;

      for (auto* window : new_user_itr->second) {
        auto entry = owner_->window_to_entry().find(window);
        DCHECK(entry != owner_->window_to_entry().end());

        if (entry->second->show())
          owner_->SetWindowVisibility(window, true, duration);
      }

      break;
    }
    case ANIMATION_STEP_SHOW_NEW_USER: {
      // In order to make the animation look better, we had to move the code
      // that shows the new user to the previous step. Hence, we do nothing
      // here.
      break;
    }
    case ANIMATION_STEP_FINALIZE: {
      // Reactivate the MRU window of the new user.
      aura::Window::Windows mru_list = ash::WmWindow::ToAuraWindows(
          ash::Shell::Get()->mru_window_tracker()->BuildMruWindowList());
      if (!mru_list.empty()) {
        aura::Window* window = mru_list[0];
        ash::wm::WindowState* window_state = ash::wm::GetWindowState(window);
        if (owner_->IsWindowOnDesktopOfUser(window, new_account_id_) &&
            !window_state->IsMinimized()) {
          // Several unit tests come here without an activation client.
          aura::client::ActivationClient* client =
              aura::client::GetActivationClient(window->GetRootWindow());
          if (client)
            client->ActivateWindow(window);
        }
      }

      owner_->notification_blocker()->ActiveUserChanged(new_account_id_);
      break;
    }
    case ANIMATION_STEP_ENDED:
      NOTREACHED();
      break;
  }
}

UserSwitchAnimatorChromeOS::TransitioningScreenCover
UserSwitchAnimatorChromeOS::GetScreenCover(aura::Window* root_window) {
  TransitioningScreenCover cover = NO_USER_COVERS_SCREEN;
  for (MultiUserWindowManagerChromeOS::WindowToEntryMap::const_iterator it_map =
           owner_->window_to_entry().begin();
       it_map != owner_->window_to_entry().end();
       ++it_map) {
    aura::Window* window = it_map->first;
    if (root_window && window->GetRootWindow() != root_window)
      continue;
    if (window->IsVisible() && CoversScreen(window)) {
      if (cover == NEW_USER_COVERS_SCREEN)
        return BOTH_USERS_COVER_SCREEN;
      else
        cover = OLD_USER_COVERS_SCREEN;
    } else if (owner_->IsWindowOnDesktopOfUser(window, new_account_id_) &&
               CoversScreen(window)) {
      if (cover == OLD_USER_COVERS_SCREEN)
        return BOTH_USERS_COVER_SCREEN;
      else
        cover = NEW_USER_COVERS_SCREEN;
    }
  }
  return cover;
}

void UserSwitchAnimatorChromeOS::BuildUserToWindowsListMap() {
  // This is to be called only at the time this animation is constructed.
  DCHECK(windows_by_account_id_.empty());

  // For each unique parent window, we enumerate its children windows, and
  // for each child if it's in the |window_to_entry()| map, we add it to the
  // |windows_by_account_id_| map.
  // This gives us a list of windows per each user that is in the same order
  // they were created in their parent windows.
  std::set<aura::Window*> parent_windows;
  auto& window_to_entry_map = owner_->window_to_entry();
  for (auto& window_entry_pair : window_to_entry_map) {
    aura::Window* parent_window = window_entry_pair.first->parent();
    if (parent_windows.find(parent_window) == parent_windows.end()) {
      parent_windows.insert(parent_window);
      for (auto* child_window : parent_window->children()) {
        auto itr = window_to_entry_map.find(child_window);
        if (itr != window_to_entry_map.end()) {
          windows_by_account_id_[itr->second->show_for_user()].push_back(
              child_window);
        }
      }
    }
  }
}

}  // namespace chrome
