// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/lock_screen_action/lock_screen_action_tray.h"

#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/session/session_controller.h"
#include "ash/shelf/shelf_constants.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/tray/tray_container.h"
#include "ash/tray_action/tray_action.h"
#include "base/logging.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/layout/fill_layout.h"

namespace ash {

namespace {

// The preferred size for the tray item view.
const int kItemViewPreferredSize = 32;

}  // namespace

// View for the tray item for the lock screen note creation action.
class LockScreenActionTray::NewNoteActionView : public views::View {
 public:
  NewNoteActionView() {
    SetLayoutManager(new views::FillLayout);
    icon_ = new views::ImageView();
    icon_->SetImage(CreateVectorIcon(kPaletteActionCreateNoteIcon,
                                     kTrayIconSize, kShelfIconColor));
    icon_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_ASH_STYLUS_TOOLS_CREATE_NOTE_ACTION));
    AddChildView(icon_);
  }

  ~NewNoteActionView() override {}

  gfx::Size CalculatePreferredSize() const override {
    return gfx::Size(kItemViewPreferredSize, kItemViewPreferredSize);
  }

  const gfx::ImageSkia& GetImage() const { return icon_->GetImage(); }

 private:
  views::ImageView* icon_;

  DISALLOW_COPY_AND_ASSIGN(NewNoteActionView);
};

LockScreenActionTray::LockScreenActionTray(WmShelf* wm_shelf)
    : TrayBackgroundView(wm_shelf),
      session_observer_(this),
      tray_action_observer_(this) {
  SetInkDropMode(InkDropMode::ON);
  SetVisible(false);
  new_note_action_view_ = new NewNoteActionView();
  tray_container()->AddChildView(new_note_action_view_);
}

LockScreenActionTray::~LockScreenActionTray() {}

void LockScreenActionTray::Initialize() {
  TrayBackgroundView::Initialize();

  session_observer_.Add(Shell::Get()->session_controller());

  TrayAction* controller = Shell::Get()->tray_action();
  tray_action_observer_.Add(controller);

  OnLockScreenNoteStateChanged(controller->GetLockScreenNoteState());
}

bool LockScreenActionTray::PerformAction(const ui::Event& event) {
  Shell::Get()->tray_action()->RequestNewLockScreenNote();
  return true;
}

base::string16 LockScreenActionTray::GetAccessibleNameForTray() {
  return l10n_util::GetStringUTF16(IDS_ASH_STYLUS_TOOLS_CREATE_NOTE_ACTION);
}

bool LockScreenActionTray::ShouldBlockShelfAutoHide() const {
  return false;
}

void LockScreenActionTray::HideBubbleWithView(
    const views::TrayBubbleView* bubble_view) {}

void LockScreenActionTray::ClickedOutsideBubble() {}

void LockScreenActionTray::OnLockStateChanged(bool locked) {
  UpdateNewNoteIcon();
}

void LockScreenActionTray::OnLockScreenNoteStateChanged(
    mojom::TrayActionState state) {
  new_note_state_ = state;

  UpdateNewNoteIcon();
}

bool LockScreenActionTray::IsStateVisible() const {
  return Shell::Get()->session_controller()->IsScreenLocked() &&
         (new_note_state_ == mojom::TrayActionState::kAvailable ||
          new_note_state_ == mojom::TrayActionState::kLaunching);
}

void LockScreenActionTray::UpdateNewNoteIcon() {
  SetIsActive(new_note_state_ == mojom::TrayActionState::kLaunching ||
              new_note_state_ == mojom::TrayActionState::kActive ||
              new_note_state_ == mojom::TrayActionState::kBackground);

  if (!IsStateVisible()) {
    SetVisible(false);
    return;
  }

  new_note_action_view_->SetEnabled(new_note_state_ !=
                                    mojom::TrayActionState::kLaunching);

  SetVisible(true);
}

const gfx::ImageSkia& LockScreenActionTray::GetImageForTesting() const {
  return new_note_action_view_->GetImage();
}

}  // namespace ash
