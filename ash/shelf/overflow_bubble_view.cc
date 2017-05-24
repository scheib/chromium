// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shelf/overflow_bubble_view.h"

#include <algorithm>

#include "ash/public/cpp/shell_window_ids.h"
#include "ash/root_window_controller.h"
#include "ash/shelf/shelf_constants.h"
#include "ash/shelf/wm_shelf.h"
#include "ash/shell.h"
#include "ash/wm_window.h"
#include "base/i18n/rtl.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace {

// Padding at the two ends of the bubble.
const int kEndPadding = 16;

// Distance between overflow bubble and the main shelf.
const int kDistanceToMainShelf = 4;

}  // namespace

OverflowBubbleView::OverflowBubbleView(WmShelf* wm_shelf)
    : wm_shelf_(wm_shelf),
      shelf_view_(nullptr),
      background_animator_(SHELF_BACKGROUND_OVERLAP,
                           // Don't pass the WmShelf so the translucent color is
                           // always used.
                           nullptr,
                           Shell::Get()->wallpaper_controller()) {
  DCHECK(wm_shelf_);

  background_animator_.AddObserver(this);
}

OverflowBubbleView::~OverflowBubbleView() {
  background_animator_.RemoveObserver(this);
}

void OverflowBubbleView::InitOverflowBubble(views::View* anchor,
                                            views::View* shelf_view) {
  shelf_view_ = shelf_view;

  SetAnchorView(anchor);
  set_arrow(views::BubbleBorder::NONE);
  set_background(nullptr);
  if (wm_shelf_->IsHorizontalAlignment())
    set_margins(gfx::Insets(0, kEndPadding));
  else
    set_margins(gfx::Insets(kEndPadding, 0));
  set_shadow(views::BubbleBorder::NO_ASSETS);
  // Overflow bubble should not get focus. If it get focus when it is shown,
  // active state item is changed to running state.
  set_can_activate(false);

  // Makes bubble view has a layer and clip its children layers.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetMasksToBounds(true);

  // Calls into OnBeforeBubbleWidgetInit to set the window parent container.
  views::BubbleDialogDelegateView::CreateBubble(this);
  AddChildView(shelf_view_);
}

void OverflowBubbleView::ScrollByXOffset(int x_offset) {
  const gfx::Rect visible_bounds(GetContentsBounds());
  const gfx::Size contents_size(shelf_view_->GetPreferredSize());

  DCHECK_GE(contents_size.width(), visible_bounds.width());
  int x = std::min(contents_size.width() - visible_bounds.width(),
                   std::max(0, scroll_offset_.x() + x_offset));
  scroll_offset_.set_x(x);
}

void OverflowBubbleView::ScrollByYOffset(int y_offset) {
  const gfx::Rect visible_bounds(GetContentsBounds());
  const gfx::Size contents_size(shelf_view_->GetPreferredSize());

  DCHECK_GE(contents_size.width(), visible_bounds.width());
  int y = std::min(contents_size.height() - visible_bounds.height(),
                   std::max(0, scroll_offset_.y() + y_offset));
  scroll_offset_.set_y(y);
}

gfx::Size OverflowBubbleView::CalculatePreferredSize() const {
  gfx::Size preferred_size = shelf_view_->GetPreferredSize();

  const gfx::Rect monitor_rect =
      display::Screen::GetScreen()
          ->GetDisplayNearestPoint(GetAnchorRect().CenterPoint())
          .work_area();
  if (!monitor_rect.IsEmpty()) {
    if (wm_shelf_->IsHorizontalAlignment()) {
      preferred_size.set_width(std::min(
          preferred_size.width(), monitor_rect.width() - 2 * kEndPadding));
    } else {
      preferred_size.set_height(std::min(
          preferred_size.height(), monitor_rect.height() - 2 * kEndPadding));
    }
  }

  return preferred_size;
}

void OverflowBubbleView::Layout() {
  shelf_view_->SetBoundsRect(
      gfx::Rect(gfx::PointAtOffsetFromOrigin(-scroll_offset_),
                shelf_view_->GetPreferredSize()));
}

void OverflowBubbleView::ChildPreferredSizeChanged(views::View* child) {
  // When contents size is changed, ContentsBounds should be updated before
  // calculating scroll offset.
  SizeToContents();

  // Ensures |shelf_view_| is still visible.
  if (wm_shelf_->IsHorizontalAlignment())
    ScrollByXOffset(0);
  else
    ScrollByYOffset(0);
  Layout();
}

bool OverflowBubbleView::OnMouseWheel(const ui::MouseWheelEvent& event) {
  // The MouseWheelEvent was changed to support both X and Y offsets
  // recently, but the behavior of this function was retained to continue
  // using Y offsets only. Might be good to simply scroll in both
  // directions as in OverflowBubbleView::OnScrollEvent.
  if (wm_shelf_->IsHorizontalAlignment())
    ScrollByXOffset(-event.y_offset());
  else
    ScrollByYOffset(-event.y_offset());
  Layout();

  return true;
}

void OverflowBubbleView::OnScrollEvent(ui::ScrollEvent* event) {
  ScrollByXOffset(-event->x_offset());
  ScrollByYOffset(-event->y_offset());
  Layout();
  event->SetHandled();
}

int OverflowBubbleView::GetDialogButtons() const {
  return ui::DIALOG_BUTTON_NONE;
}

void OverflowBubbleView::OnBeforeBubbleWidgetInit(
    views::Widget::InitParams* params,
    views::Widget* bubble_widget) const {
  // Place the bubble in the same root window as the anchor.
  WmWindow::Get(anchor_widget()->GetNativeWindow())
      ->GetRootWindowController()
      ->ConfigureWidgetInitParamsForContainer(
          bubble_widget, kShellWindowId_ShelfBubbleContainer, params);
}

gfx::Rect OverflowBubbleView::GetBubbleBounds() {
  const gfx::Size content_size = GetPreferredSize();
  const gfx::Rect anchor_rect = GetAnchorRect();
  gfx::Rect monitor_rect =
      display::Screen::GetScreen()
          ->GetDisplayNearestPoint(anchor_rect.CenterPoint())
          .work_area();

  if (wm_shelf_->IsHorizontalAlignment()) {
    gfx::Rect bounds(
        base::i18n::IsRTL()
            ? anchor_rect.x() - kEndPadding
            : anchor_rect.right() - content_size.width() - kEndPadding,
        anchor_rect.y() - kDistanceToMainShelf - content_size.height(),
        content_size.width() + 2 * kEndPadding, content_size.height());
    if (bounds.x() < monitor_rect.x())
      bounds.Offset(monitor_rect.x() - bounds.x(), 0);
    else if (bounds.right() > monitor_rect.right())
      bounds.Offset(monitor_rect.right() - bounds.right(), 0);
    return bounds;
  }
  gfx::Rect bounds(
      0, anchor_rect.bottom() - content_size.height() - kEndPadding,
      content_size.width(), content_size.height() + 2 * kEndPadding);
  if (wm_shelf_->alignment() == SHELF_ALIGNMENT_LEFT)
    bounds.set_x(anchor_rect.right() + kDistanceToMainShelf);
  else
    bounds.set_x(anchor_rect.x() - kDistanceToMainShelf - content_size.width());
  if (bounds.y() < monitor_rect.y())
    bounds.Offset(0, monitor_rect.y() - bounds.y());
  else if (bounds.bottom() > monitor_rect.bottom())
    bounds.Offset(monitor_rect.bottom() - bounds.bottom(), 0);
  return bounds;
}

void OverflowBubbleView::UpdateShelfBackground(SkColor color) {
  set_color(color);
}

}  // namespace ash
