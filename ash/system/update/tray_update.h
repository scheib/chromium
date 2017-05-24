// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_UPDATE_TRAY_UPDATE_H_
#define ASH_SYSTEM_UPDATE_TRAY_UPDATE_H_

#include "ash/ash_export.h"
#include "ash/system/tray/tray_image_item.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/strings/string16.h"

namespace views {
class Label;
class View;
}

namespace ash {

namespace mojom {
enum class UpdateSeverity;
enum class UpdateType;
}

// The system update tray item. The tray icon stays visible once an update
// notification is received. The icon only disappears after a reboot to apply
// the update. Exported for test.
class ASH_EXPORT TrayUpdate : public TrayImageItem {
 public:
  explicit TrayUpdate(SystemTray* system_tray);
  ~TrayUpdate() override;

  // Shows an icon in the system tray indicating that a software update is
  // available. Once shown the icon persists until reboot. |severity| and
  // |factory_reset_required| are used to set the icon, color, and tooltip.
  void ShowUpdateIcon(mojom::UpdateSeverity severity,
                      bool factory_reset_required,
                      mojom::UpdateType update_type);

  // Shows an icon in the system tray indicating that a software update is
  // available but user's agreement is required as current connection is
  // cellular. Once shown the icon persists until reboot.
  void ShowUpdateOverCellularAvailableIcon();

 private:
  FRIEND_TEST_ALL_PREFIXES(TrayUpdateTest, VisibilityAfterUpdate);
  FRIEND_TEST_ALL_PREFIXES(TrayUpdateTest, VisibilityAfterFlashUpdate);

  class UpdateView;

  // Overridden from TrayImageItem.
  bool GetInitialVisibility() override;
  views::View* CreateDefaultView(LoginStatus status) override;
  void DestroyDefaultView() override;

  // Expose label information for testing.
  views::Label* GetLabelForTesting();
  UpdateView* update_view_;

  // If an external monitor is connected then the system tray may be created
  // after the update data is sent from chrome, so share the update info between
  // all instances.
  static bool update_required_;
  static mojom::UpdateSeverity severity_;
  static bool factory_reset_required_;
  static mojom::UpdateType update_type_;
  static bool update_over_cellular_available_;

  DISALLOW_COPY_AND_ASSIGN(TrayUpdate);
};

}  // namespace ash

#endif  // ASH_SYSTEM_UPDATE_TRAY_UPDATE_H_
