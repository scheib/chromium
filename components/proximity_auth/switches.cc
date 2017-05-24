// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/proximity_auth/switches.h"

namespace proximity_auth {
namespace switches {

// Enables discovery of the phone over Bluetooth Low Energy.
const char kEnableBluetoothLowEnergyDiscovery[] =
    "enable-proximity-auth-bluetooth-low-energy-discovery";

// Enables the use of EasyUnlock to log into the Chromebook.
extern const char kEnableChromeOSLogin[] =
    "enable-proximity-auth-chromeos-login";

// Enables close proximity detection. This allows the user to set a setting to
// require very close proximity between the remote device and the local device
// in order to unlock the local device, which trades off convenience for
// security.
const char kEnableProximityDetection[] =
    "enable-proximity-auth-proximity-detection";

// Force easy unlock app loading in test.
// TODO(xiyuan): Remove this when app could be bundled with Chrome.
const char kForceLoadEasyUnlockAppInTests[] =
    "force-load-easy-unlock-app-in-tests";

}  // namespace switches
}  // namespace proximity_auth
