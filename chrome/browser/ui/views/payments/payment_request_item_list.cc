// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/payments/payment_request_item_list.h"

#include "chrome/browser/ui/views/payments/payment_request_dialog_view_ids.h"
#include "chrome/browser/ui/views/payments/payment_request_row_view.h"
#include "chrome/browser/ui/views/payments/payment_request_views_util.h"
#include "components/payments/content/payment_request_state.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/vector_icons/vector_icons.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view.h"

namespace payments {

namespace {

const SkColor kCheckmarkColor = 0xFF609265;

}  // namespace

PaymentRequestItemList::Item::Item(PaymentRequestSpec* spec,
                                   PaymentRequestState* state,
                                   PaymentRequestItemList* list,
                                   bool selected,
                                   bool show_edit_button)
    : spec_(spec),
      state_(state),
      list_(list),
      selected_(selected),
      show_edit_button_(show_edit_button) {}

PaymentRequestItemList::Item::~Item() {}

views::View* PaymentRequestItemList::Item::GetItemView() {
  if (!item_view_) {
    item_view_ = CreateItemView();
    item_view_->set_owned_by_client();
  }

  return item_view_.get();
}

void PaymentRequestItemList::Item::SetSelected(bool selected, bool notify) {
  selected_ = selected;

  // This could be called before CreateItemView, so before |checkmark_| is
  // instantiated.
  if (checkmark_)
    checkmark_->SetVisible(selected_);

  if (notify)
    SelectedStateChanged();
}

std::unique_ptr<views::ImageView> PaymentRequestItemList::Item::CreateCheckmark(
    bool selected) {
  std::unique_ptr<views::ImageView> checkmark =
      base::MakeUnique<views::ImageView>();
  checkmark->set_id(static_cast<int>(DialogViewID::CHECKMARK_VIEW));
  checkmark->set_can_process_events_within_subtree(false);
  checkmark->set_owned_by_client();
  checkmark->SetImage(
      gfx::CreateVectorIcon(views::kMenuCheckIcon, kCheckmarkColor));
  checkmark->SetVisible(selected);
  return checkmark;
}

std::unique_ptr<views::View> PaymentRequestItemList::Item::CreateExtraView() {
  return nullptr;
}

std::unique_ptr<views::View> PaymentRequestItemList::Item::CreateItemView() {
  std::unique_ptr<views::View> content = CreateContentView();

  const gfx::Insets row_insets(
      kPaymentRequestRowVerticalInsets, kPaymentRequestRowHorizontalInsets,
      kPaymentRequestRowVerticalInsets,
      kPaymentRequestRowHorizontalInsets + kPaymentRequestRowExtraRightInset);
  std::unique_ptr<PaymentRequestRowView> row =
      base::MakeUnique<PaymentRequestRowView>(this,
                                              /* clickable= */ IsEnabled(),
                                              row_insets);
  views::GridLayout* layout = new views::GridLayout(row.get());
  row->SetLayoutManager(layout);

  // Add a column for the item's content view.
  views::ColumnSet* columns = layout->AddColumnSet(0);
  columns->AddColumn(views::GridLayout::FILL, views::GridLayout::LEADING, 1,
                     views::GridLayout::USE_PREF, 0, 0);

  // Add a column for the checkmark shown next to the selected profile.
  columns->AddColumn(views::GridLayout::TRAILING, views::GridLayout::CENTER, 0,
                     views::GridLayout::USE_PREF, 0, 0);

  // The space between the checkmark, extra view, and edit button.
  constexpr int kExtraViewSpacing = 16;
  std::unique_ptr<views::View> extra_view = CreateExtraView();
  if (extra_view) {
    columns->AddPaddingColumn(0, kExtraViewSpacing);
    // Add a column for the extra_view, which comes after the checkmark.
    columns->AddColumn(views::GridLayout::TRAILING, views::GridLayout::CENTER,
                       0, views::GridLayout::USE_PREF, 0, 0);
  }

  constexpr int kEditIconSize = 16;
  if (show_edit_button_) {
    columns->AddPaddingColumn(0, kExtraViewSpacing);
    // Add a column for the edit_button if it exists.
    columns->AddColumn(views::GridLayout::TRAILING, views::GridLayout::CENTER,
                       0, views::GridLayout::FIXED, kEditIconSize,
                       kEditIconSize);
  }

  layout->StartRow(0, 0);
  content->set_can_process_events_within_subtree(false);
  layout->AddView(content.release());

  checkmark_ = CreateCheckmark(selected());
  layout->AddView(checkmark_.get());

  if (extra_view)
    layout->AddView(extra_view.release());

  if (show_edit_button_) {
    views::ImageButton* edit_button = views::CreateVectorImageButton(this);
    const SkColor icon_color =
        color_utils::DeriveDefaultIconColor(SK_ColorBLACK);
    edit_button->SetImage(
        views::Button::STATE_NORMAL,
        gfx::CreateVectorIcon(ui::kEditIcon, kEditIconSize, icon_color));
    edit_button->set_ink_drop_base_color(icon_color);
    edit_button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    edit_button->set_id(static_cast<int>(DialogViewID::EDIT_ITEM_BUTTON));
    layout->AddView(edit_button);
  }

  return std::move(row);
}

void PaymentRequestItemList::Item::ButtonPressed(views::Button* sender,
                                                 const ui::Event& event) {
  if (sender->id() == static_cast<int>(DialogViewID::EDIT_ITEM_BUTTON)) {
    EditButtonPressed();
  } else if (CanBeSelected()) {
    list()->SelectItem(this);
  } else {
    PerformSelectionFallback();
  }
}

PaymentRequestItemList::PaymentRequestItemList() : selected_item_(nullptr) {}

PaymentRequestItemList::~PaymentRequestItemList() {}

void PaymentRequestItemList::AddItem(
    std::unique_ptr<PaymentRequestItemList::Item> item) {
  DCHECK_EQ(this, item->list());
  items_.push_back(std::move(item));
  if (items_.back()->selected()) {
    if (selected_item_)
      selected_item_->SetSelected(/*selected=*/false, /*notify=*/false);
    selected_item_ = items_.back().get();
  }
}

std::unique_ptr<views::View> PaymentRequestItemList::CreateListView() {
  std::unique_ptr<views::View> content_view = base::MakeUnique<views::View>();

  views::BoxLayout* layout = new views::BoxLayout(
      views::BoxLayout::kVertical, 0, kPaymentRequestRowVerticalInsets, 0);
  content_view->SetLayoutManager(layout);

  for (auto& item : items_) {
    content_view->AddChildView(item->GetItemView());
  }

  return content_view;
}

void PaymentRequestItemList::SelectItem(PaymentRequestItemList::Item* item) {
  DCHECK_EQ(this, item->list());
  if (selected_item_ == item)
    return;

  UnselectSelectedItem();

  selected_item_ = item;
  item->SetSelected(/*selected=*/true, /*notify=*/true);
}

void PaymentRequestItemList::UnselectSelectedItem() {
  // It's possible that no item is currently selected, either during list
  // creation or in the middle of the selection operation when the previously
  // selected item has been deselected but the new one isn't selected yet.
  if (selected_item_)
    selected_item_->SetSelected(/*selected=*/false, /*notify=*/true);

  selected_item_ = nullptr;
}

}  // namespace payments
