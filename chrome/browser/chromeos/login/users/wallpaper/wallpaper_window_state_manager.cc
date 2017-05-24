// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/users/wallpaper/wallpaper_window_state_manager.h"

#include "ash/shell.h"
#include "ash/wm/mru_window_tracker.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm_window.h"
#include "chrome/browser/ui/ash/ash_util.h"
#include "ui/aura/window.h"

namespace chromeos {

// static
WallpaperWindowStateManager* g_window_state_manager = nullptr;

// static
void WallpaperWindowStateManager::MinimizeInactiveWindows(
    const std::string& user_id_hash) {
  if (ash_util::IsRunningInMash()) {
    NOTIMPLEMENTED();
    return;
  }

  if (!g_window_state_manager)
    g_window_state_manager = new WallpaperWindowStateManager();
  g_window_state_manager->BuildWindowListAndMinimizeInactiveForUser(
      user_id_hash, ash::wm::GetActiveWindow());
}

// static
void WallpaperWindowStateManager::RestoreWindows(
    const std::string& user_id_hash) {
  if (ash_util::IsRunningInMash()) {
    NOTIMPLEMENTED();
    return;
  }

  if (!g_window_state_manager) {
    DCHECK(false) << "This should only be called after calling "
                  << "MinimizeInactiveWindows.";
    return;
  }

  g_window_state_manager->RestoreMinimizedWindows(user_id_hash);
  if (g_window_state_manager->user_id_hash_window_list_map_.empty()) {
    delete g_window_state_manager;
    g_window_state_manager = nullptr;
  }
}

WallpaperWindowStateManager::WallpaperWindowStateManager() {}

WallpaperWindowStateManager::~WallpaperWindowStateManager() {}

void WallpaperWindowStateManager::BuildWindowListAndMinimizeInactiveForUser(
    const std::string& user_id_hash,
    aura::Window* active_window) {
  if (user_id_hash_window_list_map_.find(user_id_hash) ==
      user_id_hash_window_list_map_.end()) {
    user_id_hash_window_list_map_[user_id_hash] = std::set<aura::Window*>();
  }
  std::set<aura::Window*>* results =
      &user_id_hash_window_list_map_[user_id_hash];

  std::vector<aura::Window*> windows = ash::WmWindow::ToAuraWindows(
      ash::Shell::Get()->mru_window_tracker()->BuildWindowListIgnoreModal());

  for (std::vector<aura::Window*>::iterator iter = windows.begin();
       iter != windows.end(); ++iter) {
    // Ignore active window and minimized windows.
    if (*iter == active_window || ash::wm::GetWindowState(*iter)->IsMinimized())
      continue;

    if (!(*iter)->HasObserver(this))
      (*iter)->AddObserver(this);

    results->insert(*iter);
    ash::wm::GetWindowState(*iter)->Minimize();
  }
}

void WallpaperWindowStateManager::RestoreMinimizedWindows(
    const std::string& user_id_hash) {
  UserIDHashWindowListMap::iterator it =
      user_id_hash_window_list_map_.find(user_id_hash);
  if (it == user_id_hash_window_list_map_.end()) {
    DCHECK(false) << "This should only be called after calling "
                  << "MinimizeInactiveWindows.";
    return;
  }

  std::set<aura::Window*> removed_windows;
  removed_windows.swap(it->second);
  user_id_hash_window_list_map_.erase(it);

  for (std::set<aura::Window*>::iterator iter = removed_windows.begin();
       iter != removed_windows.end(); ++iter) {
    ash::wm::GetWindowState(*iter)->Unminimize();
    RemoveObserverIfUnreferenced(*iter);
  }
}

void WallpaperWindowStateManager::RemoveObserverIfUnreferenced(
    aura::Window* window) {
  for (UserIDHashWindowListMap::iterator iter =
           user_id_hash_window_list_map_.begin();
       iter != user_id_hash_window_list_map_.end(); ++iter) {
    if (iter->second.find(window) != iter->second.end())
      return;
  }
  // Remove observer if |window| is not observed by any users.
  window->RemoveObserver(this);
}

void WallpaperWindowStateManager::OnWindowDestroyed(aura::Window* window) {
  for (UserIDHashWindowListMap::iterator iter =
           user_id_hash_window_list_map_.begin();
       iter != user_id_hash_window_list_map_.end(); ++iter) {
    iter->second.erase(window);
  }
}

void WallpaperWindowStateManager::OnWindowStackingChanged(
    aura::Window* window) {
  // If user interacted with the |window| while wallpaper picker is opening,
  // removes the |window| from observed list.
  for (auto iter = user_id_hash_window_list_map_.begin();
       iter != user_id_hash_window_list_map_.end(); ++iter) {
    iter->second.erase(window);
  }
  window->RemoveObserver(this);
}

}  // namespace chromeos
