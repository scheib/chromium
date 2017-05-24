// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_USERS_WALLPAPER_WALLPAPER_WINDOW_STATE_MANAGER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_USERS_WALLPAPER_WALLPAPER_WINDOW_STATE_MANAGER_H_

#include <map>
#include <set>
#include <string>

#include "base/macros.h"
#include "ui/aura/window_observer.h"

namespace aura {
class Window;
}

namespace chromeos {

// WallpaperWindowStateManager remembers which windows have been minimized in
// order to restore them when the wallpaper viewer is hidden.
class WallpaperWindowStateManager : public aura::WindowObserver {
 public:
  typedef std::map<std::string, std::set<aura::Window*>>
      UserIDHashWindowListMap;

  // Minimizes all windows except the active window.
  static void MinimizeInactiveWindows(const std::string& user_id_hash);

  // Unminimizes all minimized windows restoring them to their previous state.
  // This should only be called after calling MinimizeInactiveWindows.
  static void RestoreWindows(const std::string& user_id_hash);

 private:
  WallpaperWindowStateManager();

  ~WallpaperWindowStateManager() override;

  // Store all unminimized windows except |active_window| and minimize them.
  // All the windows are saved in a map and the key value is |user_id_hash|.
  void BuildWindowListAndMinimizeInactiveForUser(
      const std::string& user_id_hash,
      aura::Window* active_window);

  // Unminimize all the stored windows for |user_id_hash|.
  void RestoreMinimizedWindows(const std::string& user_id_hash);

  // Remove the observer from |window| if |window| is no longer referenced in
  // user_id_hash_window_list_map_.
  void RemoveObserverIfUnreferenced(aura::Window* window);

  // aura::WindowObserver overrides.
  void OnWindowDestroyed(aura::Window* window) override;

  // aura::WindowObserver overrides.
  void OnWindowStackingChanged(aura::Window* window) override;

  // Map of user id hash and associated list of minimized windows.
  UserIDHashWindowListMap user_id_hash_window_list_map_;

  DISALLOW_COPY_AND_ASSIGN(WallpaperWindowStateManager);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_USERS_WALLPAPER_WALLPAPER_WINDOW_STATE_MANAGER_H_
