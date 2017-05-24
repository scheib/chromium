// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/login/ui/lock_contents_view.h"

#include "ash/login/lock_screen_controller.h"
#include "ash/session/session_controller.h"
#include "ash/shell.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/layout/box_layout.h"

namespace ash {

LockContentsView::LockContentsView() {
  auto* layout = new views::BoxLayout(views::BoxLayout::kVertical, 0, 0, 0);
  SetLayoutManager(layout);

  views::Label* label = new views::Label();
  label->SetBorder(views::CreateEmptyBorder(2, 3, 4, 5));
  label->set_background(views::Background::CreateThemedSolidBackground(
      label, ui::NativeTheme::kColorId_BubbleBackground));
  label->SetText(base::ASCIIToUTF16("User"));
  AddChildView(label);

  unlock_button_ =
      views::MdTextButton::Create(this, base::ASCIIToUTF16("Unlock"));
  AddChildView(unlock_button_);
}

LockContentsView::~LockContentsView() {}

void LockContentsView::ButtonPressed(views::Button* sender,
                                     const ui::Event& event) {
  DCHECK(unlock_button_ == sender);

  // TODO: user index shouldn't be hard coded here. Complete the implementation
  // below.
  const int user_index = 0;
  const mojom::UserSession* const user_session =
      Shell::Get()->session_controller()->GetUserSession(user_index);
  Shell::Get()->lock_screen_controller()->AuthenticateUser(
      user_session->account_id, std::string(),
      false /* authenticated_by_pin */);
}

}  // namespace ash
