// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/update/tray_update.h"

#include "ash/metrics/user_metrics_action.h"
#include "ash/public/interfaces/update.mojom.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/shell.h"
#include "ash/shell_port.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/system/tray/system_tray.h"
#include "ash/system/tray/system_tray_controller.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/tray/tray_popup_item_style.h"
#include "ash/system/tray/tray_popup_utils.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"

namespace ash {
namespace {

// Returns the color to use for the update icon when the update severity is
// |severity|. If |for_menu| is true, the icon color for the system menu is
// given, otherwise the icon color for the system tray is given.
SkColor IconColorForUpdateSeverity(mojom::UpdateSeverity severity,
                                   bool for_menu) {
  const SkColor default_color = for_menu ? kMenuIconColor : kTrayIconColor;
  switch (severity) {
    case mojom::UpdateSeverity::NONE:
      return default_color;
    case mojom::UpdateSeverity::LOW:
      return for_menu ? gfx::kGoogleGreen700 : kTrayIconColor;
    case mojom::UpdateSeverity::ELEVATED:
      return for_menu ? gfx::kGoogleYellow700 : gfx::kGoogleYellow300;
    case mojom::UpdateSeverity::HIGH:
    case mojom::UpdateSeverity::SEVERE:
    case mojom::UpdateSeverity::CRITICAL:
      return for_menu ? gfx::kGoogleRed700 : gfx::kGoogleRed300;
    default:
      NOTREACHED();
      break;
  }
  return default_color;
}

}  // namespace

// static
bool TrayUpdate::update_required_ = false;
// static
mojom::UpdateSeverity TrayUpdate::severity_ = mojom::UpdateSeverity::NONE;
// static
bool TrayUpdate::factory_reset_required_ = false;
// static
bool TrayUpdate::update_over_cellular_available_ = false;

mojom::UpdateType TrayUpdate::update_type_ = mojom::UpdateType::SYSTEM;

// The "restart to update" item in the system tray menu.
class TrayUpdate::UpdateView : public ActionableView {
 public:
  explicit UpdateView(TrayUpdate* owner)
      : ActionableView(owner, TrayPopupInkDropStyle::FILL_BOUNDS),
        update_label_(nullptr) {
    SetLayoutManager(new views::FillLayout);

    ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
    TriView* tri_view = TrayPopupUtils::CreateDefaultRowView();
    AddChildView(tri_view);
    views::ImageView* image = TrayPopupUtils::CreateMainImageView();
    image->SetImage(gfx::CreateVectorIcon(
        kSystemMenuUpdateIcon,
        IconColorForUpdateSeverity(owner->severity_, true)));
    tri_view->AddView(TriView::Container::START, image);

    base::string16 label_text;
    if (owner->factory_reset_required_) {
      label_text = bundle.GetLocalizedString(
          IDS_ASH_STATUS_TRAY_RESTART_AND_POWERWASH_TO_UPDATE);
    } else if (owner->update_type_ == mojom::UpdateType::FLASH) {
      label_text = bundle.GetLocalizedString(IDS_ASH_STATUS_TRAY_UPDATE_FLASH);
    } else if (!owner->update_required_ &&
               owner->update_over_cellular_available_) {
      label_text = bundle.GetLocalizedString(
          IDS_ASH_STATUS_TRAY_UPDATE_OVER_CELLULAR_AVAILABLE);
    } else {
      label_text = bundle.GetLocalizedString(IDS_ASH_STATUS_TRAY_UPDATE);
    }

    SetAccessibleName(label_text);
    update_label_ = TrayPopupUtils::CreateDefaultLabel();
    update_label_->SetText(label_text);

    TrayPopupItemStyle style(TrayPopupItemStyle::FontStyle::DEFAULT_VIEW_LABEL);
    style.SetupLabel(update_label_);
    tri_view->AddView(TriView::Container::CENTER, update_label_);

    SetInkDropMode(InkDropHostView::InkDropMode::ON);
  }

  ~UpdateView() override {}

  views::Label* update_label_;

 private:
  // Overridden from ActionableView.
  bool PerformAction(const ui::Event& /* event */) override {
    DCHECK(update_required_ || update_over_cellular_available_);
    if (update_required_) {
      Shell::Get()->system_tray_controller()->RequestRestartForUpdate();
      ShellPort::Get()->RecordUserMetricsAction(
          UMA_STATUS_AREA_OS_UPDATE_DEFAULT_SELECTED);
    } else {
      // Shows the about chrome OS page and checks for update after the page is
      // loaded.
      Shell::Get()->system_tray_controller()->ShowAboutChromeOS();
    }
    CloseSystemBubble();
    return true;
  }

  DISALLOW_COPY_AND_ASSIGN(UpdateView);
};

TrayUpdate::TrayUpdate(SystemTray* system_tray)
    : TrayImageItem(system_tray, kSystemTrayUpdateIcon, UMA_UPDATE) {}

TrayUpdate::~TrayUpdate() {}

bool TrayUpdate::GetInitialVisibility() {
  // If chrome tells ash there is an update available before this item's system
  // tray is constructed then show the icon.
  return update_required_ || update_over_cellular_available_;
}

views::View* TrayUpdate::CreateDefaultView(LoginStatus status) {
  if (update_required_ || update_over_cellular_available_) {
    update_view_ = new UpdateView(this);
    return update_view_;
  }
  return nullptr;
}

void TrayUpdate::DestroyDefaultView() {
  update_view_ = nullptr;
}

void TrayUpdate::ShowUpdateIcon(mojom::UpdateSeverity severity,
                                bool factory_reset_required,
                                mojom::UpdateType update_type) {
  // Cache update info so we can create the default view when the menu opens.
  update_required_ = true;
  severity_ = severity;
  factory_reset_required_ = factory_reset_required;
  update_type_ = update_type;

  // Show the icon in the tray.
  SetIconColor(IconColorForUpdateSeverity(severity_, false));
  tray_view()->SetVisible(true);
}

views::Label* TrayUpdate::GetLabelForTesting() {
  return update_view_ ? update_view_->update_label_ : nullptr;
}

void TrayUpdate::ShowUpdateOverCellularAvailableIcon() {
  update_over_cellular_available_ = true;

  // TODO(weidongg/691108): adjust severity according the amount of time passing
  // after update is available over cellular connection.
  // Use low severity for update available over cellular connection.
  SetIconColor(IconColorForUpdateSeverity(mojom::UpdateSeverity::LOW, false));
  tray_view()->SetVisible(true);
}

}  // namespace ash
