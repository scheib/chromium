// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/screen_security/screen_tray_item.h"

#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/system/tray/system_tray.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/tray/tray_popup_item_style.h"
#include "ash/system/tray/tray_popup_utils.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/message_center/message_center.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"

namespace ash {
namespace tray {

// ScreenTrayView implementations.
ScreenTrayView::ScreenTrayView(ScreenTrayItem* screen_tray_item)
    : TrayItemView(screen_tray_item), screen_tray_item_(screen_tray_item) {
  CreateImageView();
  image_view()->SetImage(
      gfx::CreateVectorIcon(vector_icons::kScreenShareIcon, kTrayIconColor));
  Update();
}

ScreenTrayView::~ScreenTrayView() {}

void ScreenTrayView::Update() {
  SetVisible(screen_tray_item_->is_started());
}

// ScreenStatusView implementations.
ScreenStatusView::ScreenStatusView(ScreenTrayItem* screen_tray_item,
                                   const base::string16& label_text,
                                   const base::string16& stop_button_text)
    : screen_tray_item_(screen_tray_item),
      icon_(nullptr),
      label_(nullptr),
      stop_button_(nullptr),
      label_text_(label_text),
      stop_button_text_(stop_button_text) {
  CreateItems();
  TriView* tri_view(TrayPopupUtils::CreateDefaultRowView());
  SetLayoutManager(new views::FillLayout);
  AddChildView(tri_view);
  tri_view->AddView(TriView::Container::START, icon_);
  tri_view->AddView(TriView::Container::CENTER, label_);
  tri_view->AddView(TriView::Container::END, stop_button_);
  // There should be |kTrayPopupButtonEndMargin| padding on both sides of the
  // stop button. There is already |kTrayPopupLabelHorizontalPadding| padding on
  // the right of the label.
  tri_view->SetContainerBorder(
      TriView::Container::END,
      views::CreateEmptyBorder(
          0, kTrayPopupButtonEndMargin - kTrayPopupLabelHorizontalPadding, 0,
          kTrayPopupButtonEndMargin));
  if (screen_tray_item_)
    UpdateFromScreenTrayItem();
}

ScreenStatusView::~ScreenStatusView() {}

void ScreenStatusView::ButtonPressed(views::Button* sender,
                                     const ui::Event& event) {
  DCHECK(sender == stop_button_);
  screen_tray_item_->Stop();
  screen_tray_item_->RecordStoppedFromDefaultViewMetric();
}

void ScreenStatusView::CreateItems() {
  icon_ = TrayPopupUtils::CreateMainImageView();
  icon_->SetImage(gfx::CreateVectorIcon(
      kSystemMenuScreenShareIcon, TrayPopupItemStyle::GetIconColor(
                                      TrayPopupItemStyle::ColorStyle::ACTIVE)));
  label_ = TrayPopupUtils::CreateDefaultLabel();
  label_->SetMultiLine(true);
  label_->SetText(label_text_);
  TrayPopupItemStyle style(TrayPopupItemStyle::FontStyle::DEFAULT_VIEW_LABEL);
  style.SetupLabel(label_);

  stop_button_ = TrayPopupUtils::CreateTrayPopupButton(this, stop_button_text_);
}

void ScreenStatusView::UpdateFromScreenTrayItem() {
  SetVisible(screen_tray_item_->is_started());
}

ScreenNotificationDelegate::ScreenNotificationDelegate(
    ScreenTrayItem* screen_tray)
    : screen_tray_(screen_tray) {}

ScreenNotificationDelegate::~ScreenNotificationDelegate() {}

void ScreenNotificationDelegate::ButtonClick(int button_index) {
  DCHECK_EQ(0, button_index);
  screen_tray_->Stop();
  screen_tray_->RecordStoppedFromNotificationViewMetric();
}

}  // namespace tray

ScreenTrayItem::ScreenTrayItem(SystemTray* system_tray, UmaType uma_type)
    : SystemTrayItem(system_tray, uma_type),
      tray_view_(nullptr),
      default_view_(nullptr),
      is_started_(false),
      stop_callback_(base::Bind(&base::DoNothing)) {}

ScreenTrayItem::~ScreenTrayItem() {}

void ScreenTrayItem::Update() {
  if (tray_view_)
    tray_view_->Update();
  if (default_view_)
    default_view_->UpdateFromScreenTrayItem();
  if (is_started_) {
    CreateOrUpdateNotification();
  } else {
    message_center::MessageCenter::Get()->RemoveNotification(
        GetNotificationId(), false /* by_user */);
  }
}

void ScreenTrayItem::Start(const base::Closure& stop_callback) {
  stop_callback_ = stop_callback;
  is_started_ = true;

  if (tray_view_)
    tray_view_->Update();

  if (default_view_)
    default_view_->UpdateFromScreenTrayItem();

  if (!system_tray()->HasSystemBubbleType(
          SystemTrayBubble::BUBBLE_TYPE_DEFAULT)) {
    CreateOrUpdateNotification();
  }
}

void ScreenTrayItem::Stop() {
  is_started_ = false;
  Update();

  if (stop_callback_.is_null())
    return;

  base::Closure callback = stop_callback_;
  stop_callback_.Reset();
  callback.Run();
}

views::View* ScreenTrayItem::CreateTrayView(LoginStatus status) {
  tray_view_ = new tray::ScreenTrayView(this);
  return tray_view_;
}

void ScreenTrayItem::RecordStoppedFromDefaultViewMetric() {}

void ScreenTrayItem::RecordStoppedFromNotificationViewMetric() {}

void ScreenTrayItem::DestroyTrayView() {
  tray_view_ = nullptr;
}

void ScreenTrayItem::DestroyDefaultView() {
  default_view_ = nullptr;
}

}  // namespace ash
