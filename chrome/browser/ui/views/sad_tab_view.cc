// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/sad_tab_view.h"

#include <string>

#include "base/metrics/histogram_macros.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/native_theme/common_theme.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr int kMaxContentWidth = 600;
constexpr int kMinColumnWidth = 120;
constexpr int kTitleBottomSpacing = 13;
constexpr int kBulletBottomSpacing = 1;
constexpr int kBulletWidth = 20;
constexpr int kBulletPadding = 13;

views::Label* CreateFormattedLabel(const base::string16& message) {
  views::Label* label = new views::Label(message);

  label->SetMultiLine(true);
  label->SetEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetLineHeight(views::kPanelSubVerticalSpacing);

  return label;
}

}  // namespace

SadTabView::SadTabView(content::WebContents* web_contents,
                       chrome::SadTabKind kind)
    : SadTab(web_contents, kind) {
  set_background(views::Background::CreateThemedSolidBackground(
      this, ui::NativeTheme::kColorId_DialogBackground));

  views::GridLayout* layout = new views::GridLayout(this);
  SetLayoutManager(layout);

  const int column_set_id = 0;
  views::ColumnSet* columns = layout->AddColumnSet(column_set_id);
  columns->AddPaddingColumn(1, views::kPanelSubVerticalSpacing);
  columns->AddColumn(views::GridLayout::LEADING, views::GridLayout::LEADING, 0,
                     views::GridLayout::USE_PREF, 0, kMinColumnWidth);
  columns->AddColumn(views::GridLayout::TRAILING, views::GridLayout::LEADING, 0,
                     views::GridLayout::USE_PREF, 0, kMinColumnWidth);
  columns->AddPaddingColumn(1, views::kPanelSubVerticalSpacing);

  views::ImageView* image = new views::ImageView();

  image->SetImage(
      gfx::CreateVectorIcon(kCrashedTabIcon, 48, gfx::kChromeIconGrey));
  layout->AddPaddingRow(1, views::kPanelVerticalSpacing);
  layout->StartRow(0, column_set_id);
  layout->AddView(image, 2, 1);

  title_ = new views::Label(l10n_util::GetStringUTF16(GetTitle()));
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  title_->SetFontList(rb.GetFontList(ui::ResourceBundle::LargeFont));
  title_->SetMultiLine(true);
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  layout->StartRowWithPadding(0, column_set_id, 0,
                              views::kPanelVerticalSpacing);
  layout->AddView(title_, 2, 1);

  message_ = CreateFormattedLabel(l10n_util::GetStringUTF16(GetMessage()));
  layout->StartRowWithPadding(0, column_set_id, 0, kTitleBottomSpacing);
  layout->AddView(message_, 2, 1, views::GridLayout::LEADING,
                  views::GridLayout::LEADING);
  size_t bullet_id = 0;
  int bullet_string_id = GetSubMessage(bullet_id);
  if (bullet_string_id) {
    const int bullet_columnset_id = 1;
    views::ColumnSet* column_set = layout->AddColumnSet(bullet_columnset_id);
    column_set->AddPaddingColumn(1, views::kPanelSubVerticalSpacing);
    column_set->AddColumn(views::GridLayout::TRAILING,
                          views::GridLayout::LEADING, 0,
                          views::GridLayout::FIXED, kBulletWidth, 0);
    column_set->AddPaddingColumn(0, kBulletPadding);
    column_set->AddColumn(views::GridLayout::LEADING,
                          views::GridLayout::LEADING, 0,
                          views::GridLayout::USE_PREF,
                          0,  // No fixed width.
                          0);
    column_set->AddPaddingColumn(1, views::kPanelSubVerticalSpacing);

    while (bullet_string_id) {
      const base::string16 bullet_character(base::WideToUTF16(L"\u2022"));
      views::Label* bullet_label = CreateFormattedLabel(bullet_character);
      views::Label* label =
          CreateFormattedLabel(l10n_util::GetStringUTF16(bullet_string_id));

      layout->StartRowWithPadding(0, bullet_columnset_id, 0,
                                  kBulletBottomSpacing);
      layout->AddView(bullet_label);
      layout->AddView(label);

      bullet_labels_.push_back(label);
      bullet_string_id = GetSubMessage(++bullet_id);
    }
  }

  action_button_ = views::MdTextButton::CreateSecondaryUiBlueButton(
      this, l10n_util::GetStringUTF16(GetButtonTitle()));
  help_link_ = new views::Link(l10n_util::GetStringUTF16(GetHelpLinkTitle()));
  help_link_->set_listener(this);
  layout->StartRowWithPadding(0, column_set_id, 0,
                              views::kPanelVerticalSpacing);
  layout->AddView(help_link_, 1, 1, views::GridLayout::LEADING,
                  views::GridLayout::CENTER);
  layout->AddView(action_button_, 1, 1, views::GridLayout::TRAILING,
                  views::GridLayout::LEADING);

  layout->AddPaddingRow(2, views::kPanelSubVerticalSpacing);

  views::Widget::InitParams sad_tab_params(
      views::Widget::InitParams::TYPE_CONTROL);

  // It is not possible to create a native_widget_win that has no parent in
  // and later re-parent it.
  // TODO(avi): This is a cheat. Can this be made cleaner?
  sad_tab_params.parent = web_contents->GetNativeView();

  set_owned_by_client();

  views::Widget* sad_tab = new views::Widget;
  sad_tab->Init(sad_tab_params);
  sad_tab->SetContentsView(this);

  views::Widget::ReparentNativeView(sad_tab->GetNativeView(),
                                    web_contents->GetNativeView());
  gfx::Rect bounds = web_contents->GetContainerBounds();
  sad_tab->SetBounds(gfx::Rect(bounds.size()));
}

SadTabView::~SadTabView() {
  if (GetWidget())
    GetWidget()->Close();
}

void SadTabView::LinkClicked(views::Link* source, int event_flags) {
  PerformAction(Action::HELP_LINK);
}

void SadTabView::ButtonPressed(views::Button* sender,
                               const ui::Event& event) {
  DCHECK_EQ(action_button_, sender);
  PerformAction(Action::BUTTON);
}

void SadTabView::Layout() {
  // Specify the maximum message width explicitly.
  const int max_width =
      std::min(width() - views::kPanelSubVerticalSpacing * 2, kMaxContentWidth);

  message_->SizeToFit(max_width);
  title_->SizeToFit(max_width);

  // Bullet labels need to take into acocunt the size of the bullet.
  for (views::Label* label : bullet_labels_)
    label->SizeToFit(max_width - kBulletWidth - kBulletPadding);

  View::Layout();
}

void SadTabView::OnPaint(gfx::Canvas* canvas) {
  if (!painted_) {
    RecordFirstPaint();
    painted_ = true;
  }
  View::OnPaint(canvas);
}

namespace chrome {

SadTab* SadTab::Create(content::WebContents* web_contents,
                       SadTabKind kind) {
  return new SadTabView(web_contents, kind);
}

}  // namespace chrome
