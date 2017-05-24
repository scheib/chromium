// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UPGRADE_DETECTOR_H_
#define CHROME_BROWSER_UPGRADE_DETECTOR_H_

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "base/timer/timer.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/upgrade_observer.h"
#include "ui/base/idle/idle.h"
#include "ui/gfx/image/image.h"

class PrefRegistrySimple;
class UpgradeObserver;

///////////////////////////////////////////////////////////////////////////////
// UpgradeDetector
//
// This class is a singleton class that monitors when an upgrade happens in the
// background. We basically ask Omaha what it thinks the latest version is and
// if our version is lower we send out a notification upon:
//   a) Detecting an upgrade and...
//   b) When we think the user should be notified about the upgrade.
// The latter happens much later, since we don't want to be too annoying.
//
class UpgradeDetector {
 public:
  // The Homeland Security Upgrade Advisory System.
  enum UpgradeNotificationAnnoyanceLevel {
    UPGRADE_ANNOYANCE_NONE = 0,  // What? Me worry?
    UPGRADE_ANNOYANCE_LOW,       // Green.
    UPGRADE_ANNOYANCE_ELEVATED,  // Yellow.
    UPGRADE_ANNOYANCE_HIGH,      // Red.
    UPGRADE_ANNOYANCE_SEVERE,    // Orange.
    UPGRADE_ANNOYANCE_CRITICAL,  // Red exclamation mark.
  };

  // Returns the singleton implementation instance.
  static UpgradeDetector* GetInstance();

  virtual ~UpgradeDetector();

  static void RegisterPrefs(PrefRegistrySimple* registry);

  // Whether the user should be notified about an upgrade.
  bool notify_upgrade() const { return notify_upgrade_; }

  // Whether the upgrade recommendation is due to Chrome being outdated.
  bool is_outdated_install() const {
    return upgrade_available_ == UPGRADE_NEEDED_OUTDATED_INSTALL;
  }

  // Whether the upgrade recommendation is due to Chrome being outdated AND
  // auto-update is turned off.
  bool is_outdated_install_no_au() const {
    return upgrade_available_ == UPGRADE_NEEDED_OUTDATED_INSTALL_NO_AU;
  }

  // Notifify this object that the user has acknowledged the critical update
  // so we don't need to complain about it for now.
  void acknowledge_critical_update() {
    critical_update_acknowledged_ = true;
  }

  // Whether the user has acknowledged the critical update.
  bool critical_update_acknowledged() const {
    return critical_update_acknowledged_;
  }

  bool is_factory_reset_required() const { return is_factory_reset_required_; }

  // Retrieves the right icon based on the degree of severity (see
  // UpgradeNotificationAnnoyanceLevel, each level has an an accompanying icon
  // to go with it) to display within the app menu.
  gfx::Image GetIcon();

  UpgradeNotificationAnnoyanceLevel upgrade_notification_stage() const {
    return upgrade_notification_stage_;
  }

  void AddObserver(UpgradeObserver* observer);

  void RemoveObserver(UpgradeObserver* observer);

 protected:
  enum UpgradeAvailable {
    // If no update is available and current install is recent enough.
    UPGRADE_AVAILABLE_NONE,
    // If a regular update is available.
    UPGRADE_AVAILABLE_REGULAR,
    // If a critical update to Chrome has been installed, such as a zero-day
    // fix.
    UPGRADE_AVAILABLE_CRITICAL,
    // If no update to Chrome has been installed for more than the recommended
    // time.
    UPGRADE_NEEDED_OUTDATED_INSTALL,
    // If no update to Chrome has been installed for more than the recommended
    // time AND auto-update is turned off.
    UPGRADE_NEEDED_OUTDATED_INSTALL_NO_AU,
  };

  UpgradeDetector();

  // Sends out UPGRADE_RECOMMENDED notification and set notify_upgrade_.
  void NotifyUpgradeRecommended();

  // The function that sends out a notification that lets the rest of the UI
  // know we should notify the user that a new update is available to download
  // over cellular connection.
  void NotifyUpdateOverCellularAvailable();

  // Triggers a critical update, which starts a timer that checks the machine
  // idle state. Protected and virtual so that it could be overridden by tests.
  virtual void TriggerCriticalUpdate();

  UpgradeAvailable upgrade_available() const { return upgrade_available_; }
  void set_upgrade_available(UpgradeAvailable available) {
    upgrade_available_ = available;
  }

  void set_best_effort_experiment_updates_available(bool available) {
    best_effort_experiment_updates_available_ = available;
  }

  bool critical_experiment_updates_available() const {
    return critical_experiment_updates_available_;
  }
  void set_critical_experiment_updates_available(bool available) {
    critical_experiment_updates_available_ = available;
  }

  void set_critical_update_acknowledged(bool acknowledged) {
    critical_update_acknowledged_ = acknowledged;
  }

  void set_upgrade_notification_stage(UpgradeNotificationAnnoyanceLevel stage) {
    upgrade_notification_stage_ = stage;
  }

  void set_is_factory_reset_required(bool is_factory_reset_required) {
    is_factory_reset_required_ = is_factory_reset_required;
  }

 private:
  FRIEND_TEST_ALL_PREFIXES(AppMenuModelTest, Basics);
  FRIEND_TEST_ALL_PREFIXES(SystemTrayClientTest, UpdateTrayIcon);

  // Initiates an Idle check. See IdleCallback below.
  void CheckIdle();

  // The callback for the IdleCheck. Tells us whether Chrome has received any
  // input events since the specified time.
  void IdleCallback(ui::IdleState state);

  // Triggers a global notification of the specified |type|.
  void TriggerNotification(chrome::NotificationType type);

  // Whether any software updates are available (experiment updates are tracked
  // separately via additional member variables below).
  UpgradeAvailable upgrade_available_;

  // Whether "best effort" experiment updates are available.
  bool best_effort_experiment_updates_available_;

  // Whether "critical" experiment updates are available.
  bool critical_experiment_updates_available_;

  // Whether the user has acknowledged the critical update.
  bool critical_update_acknowledged_;

  // Whether a factory reset is needed to complete an update.
  bool is_factory_reset_required_;

  // A timer to check to see if we've been idle for long enough to show the
  // critical warning. Should only be set if |upgrade_available_| is
  // UPGRADE_AVAILABLE_CRITICAL.
  base::RepeatingTimer idle_check_timer_;

  // The stage at which the annoyance level for upgrade notifications is at.
  UpgradeNotificationAnnoyanceLevel upgrade_notification_stage_;

  // Whether we have waited long enough after detecting an upgrade (to see
  // is we should start nagging about upgrading).
  bool notify_upgrade_;

  base::ObserverList<UpgradeObserver> observer_list_;

  DISALLOW_COPY_AND_ASSIGN(UpgradeDetector);
};

#endif  // CHROME_BROWSER_UPGRADE_DETECTOR_H_
