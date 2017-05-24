// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ARC_ARC_UTIL_H_
#define COMPONENTS_ARC_ARC_UTIL_H_

// This file contains utility to see ARC functionality status controlled by
// outside of ARC, e.g. CommandLine flag, attribute of global data/state,
// users' preferences, and FeatureList.

namespace aura {
class Window;
}  // namespace aura

namespace base {
class CommandLine;
}  // namespace base

namespace arc {

// Returns true if ARC is installed and the current device is officially
// supported to run ARC.
// Note that, to run ARC practically, it is necessary to meet more conditions,
// e.g., ARC supports only on Primary User Profile. To see if ARC can actually
// run for the profile etc., arc::ArcSessionManager::IsAllowedForProfile() is
// the function for that purpose. Please see also its comment, too.
// Also note that, ARC singleton classes (e.g. ArcSessionManager,
// ArcServiceManager, ArcServiceLauncher) are instantiated regardless of this
// check, so it is ok to access them directly.
bool IsArcAvailable();

// Returns true if ARC should always start within the primary user session
// (opted in user or not), and other supported mode such as guest and Kiosk
// mode.
bool ShouldArcAlwaysStart();

// Enables to always start ARC for testing, by appending the command line flag.
void SetArcAlwaysStartForTesting();

// Returns true if ARC is installed and running ARC kiosk apps on the current
// device is officially supported.
// It doesn't follow that ARC is available for user sessions and
// IsArcAvailable() will return true, although the reverse should be.
// This is used to distinguish special cases when ARC kiosk is available on
// the device, but is not yet supported for regular user sessions.
// In most cases, IsArcAvailable() check should be used instead of this.
// Also not that this function may return true when ARC is not running in
// Kiosk mode, it checks only ARC Kiosk availability.
bool IsArcKioskAvailable();

// For testing ARC in browser tests, this function should be called in
// SetUpCommandLine(), and its argument should be passed to this function.
// Also, in unittests, this can be called in SetUp() with
// base::CommandLine::ForCurrentProcess().
// |command_line| must not be nullptr.
void SetArcAvailableCommandLineForTesting(base::CommandLine* command_line);

// Returns true if ARC should run under Kiosk mode for the current profile.
// As it can return true only when user is already initialized, it implies
// that ARC availability was checked before and IsArcKioskAvailable()
// should also return true in that case.
bool IsArcKioskMode();

// Checks if opt-in verification was disabled by switch in command line.
// In most cases, it is disabled for testing purpose.
bool IsArcOptInVerificationDisabled();

// Returns true if the |window|'s aura::client::kAppType is ARC_APP. When
// |window| is nullptr, returns false.
bool IsArcAppWindow(aura::Window* window);

}  // namespace arc

#endif  // COMPONENTS_ARC_ARC_UTIL_H_
