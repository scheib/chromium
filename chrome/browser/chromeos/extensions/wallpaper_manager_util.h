// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_WALLPAPER_MANAGER_UTIL_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_WALLPAPER_MANAGER_UTIL_H_

class Profile;

namespace wallpaper_manager_util {

extern const char kAndroidWallpapersAppPackage[];
extern const char kAndroidWallpapersAppActivity[];

bool ShouldUseAndroidWallpapersApp(Profile* profile);

// Opens wallpaper manager application.
void OpenWallpaperManager();

}  // namespace wallpaper_manager_util

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_WALLPAPER_MANAGER_UTIL_H_
