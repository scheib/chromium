// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UPGRADE_OBSERVER_H_
#define CHROME_BROWSER_UPGRADE_OBSERVER_H_

#include "base/macros.h"

class UpgradeObserver {
 public:
  // Triggered when a software update is available, but downloading requires
  // user's agreement as current connection is cellular.
  virtual void OnUpdateOverCellularAvailable() = 0;

 protected:
  virtual ~UpgradeObserver() {}
};

#endif  // CHROME_BROWSER_UPGRADE_OBSERVER_H_
