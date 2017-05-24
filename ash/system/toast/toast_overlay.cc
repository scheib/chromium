// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/toast/toast_overlay.h"

#include "ash/public/cpp/ash_typography.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/root_window_controller.h"
#include "ash/shelf/wm_shelf.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/wm/core/window_animations.h"

namespace ash {

namespace {

// Offset of the overlay from the edge of the work area.
const int kOffset = 5;

// Duration of slide animation when overlay is shown or hidden.
const int kSlideAnimationDurationMs = 100;

// Colors for the dismiss button.
const SkColor kButtonBackgroundColor = SkColorSetARGB(0xFF, 0x32, 0x32, 0x32);
const SkColor kButtonTextColor = SkColorSetARGB(0xFF, 0x7B, 0xAA, 0xF7);

// These values are in DIP.
const int kToastHorizontalSpacing = 16;
const int kToastVerticalSpacing = 16;
const int kToastMaximumWidth = 568;
const int kToastMinimumWidth = 288;

// Returns the work area bounds for the root window where new windows are added
// (including new toasts).
gfx::Rect GetUserWorkAreaBounds() {
  return WmShelf::ForWindow(Shell::GetRootWindowForNewWindows())
      ->GetUserWorkAreaBounds();
}

}  // anonymous namespace

///////////////////////////////////////////////////////////////////////////////
//  ToastOverlayLabel
class ToastOverlayLabel : public views::Label {
 public:
  explicit ToastOverlayLabel(const base::string16& label);
  ~ToastOverlayLabel() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(ToastOverlayLabel);
};

ToastOverlayLabel::ToastOverlayLabel(const base::string16& label)
    : Label(label, CONTEXT_TOAST_OVERLAY) {
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetAutoColorReadabilityEnabled(false);
  SetMultiLine(true);
  SetEnabledColor(SK_ColorWHITE);
  SetDisabledColor(SK_ColorWHITE);
  SetSubpixelRenderingEnabled(false);

  int verticalSpacing =
      kToastVerticalSpacing - (GetPreferredSize().height() - GetBaseline());
  SetBorder(views::CreateEmptyBorder(verticalSpacing, kToastHorizontalSpacing,
                                     verticalSpacing, kToastHorizontalSpacing));
}

ToastOverlayLabel::~ToastOverlayLabel() {}

///////////////////////////////////////////////////////////////////////////////
//  ToastOverlayButton
class ToastOverlayButton : public views::LabelButton {
 public:
  explicit ToastOverlayButton(views::ButtonListener* listener,
                              const base::string16& label);
  ~ToastOverlayButton() override {}

 private:
  friend class ToastOverlay;  // for ToastOverlay::ClickDismissButtonForTesting.

  DISALLOW_COPY_AND_ASSIGN(ToastOverlayButton);
};

ToastOverlayButton::ToastOverlayButton(views::ButtonListener* listener,
                                       const base::string16& text)
    : views::LabelButton(listener, text, CONTEXT_TOAST_OVERLAY) {
  SetInkDropMode(InkDropMode::ON);
  set_has_ink_drop_action_on_click(true);
  set_ink_drop_base_color(SK_ColorWHITE);

  SetEnabledTextColors(kButtonTextColor);

  // Treat the space below the baseline as a margin.
  int verticalSpacing = kToastVerticalSpacing -
                        (GetPreferredSize().height() - label()->GetBaseline());
  SetBorder(views::CreateEmptyBorder(verticalSpacing, kToastHorizontalSpacing,
                                     verticalSpacing, kToastHorizontalSpacing));
}

///////////////////////////////////////////////////////////////////////////////
//  ToastOverlayView
class ToastOverlayView : public views::View, public views::ButtonListener {
 public:
  // This object is not owned by the views hierarchy or by the widget.
  ToastOverlayView(ToastOverlay* overlay,
                   const base::string16& text,
                   const base::Optional<base::string16>& dismiss_text);
  ~ToastOverlayView() override;

  // views::View overrides:
  void OnPaint(gfx::Canvas* canvas) override;

  ToastOverlayButton* button() { return button_; }

 private:
  // views::View overrides:
  gfx::Size GetMaximumSize() const override;
  gfx::Size GetMinimumSize() const override;

  // views::ButtonListener overrides:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

  ToastOverlay* overlay_ = nullptr;       // weak
  ToastOverlayButton* button_ = nullptr;  // weak

  DISALLOW_COPY_AND_ASSIGN(ToastOverlayView);
};

ToastOverlayView::ToastOverlayView(
    ToastOverlay* overlay,
    const base::string16& text,
    const base::Optional<base::string16>& dismiss_text)
    : overlay_(overlay) {
  auto* layout = new views::BoxLayout(views::BoxLayout::kHorizontal, 0, 0, 0);
  SetLayoutManager(layout);

  if (dismiss_text.has_value()) {
    button_ = new ToastOverlayButton(
        this, dismiss_text.value().empty()
                  ? l10n_util::GetStringUTF16(IDS_ASH_TOAST_DISMISS_BUTTON)
                  : dismiss_text.value());
  }

  ToastOverlayLabel* label = new ToastOverlayLabel(text);
  AddChildView(label);
  layout->SetFlexForView(label, 1);

  if (button_) {
    label->SetMaximumWidth(
        GetMaximumSize().width() - button_->GetPreferredSize().width() -
        kToastHorizontalSpacing * 2 - kToastHorizontalSpacing * 2);
    AddChildView(button_);
  }
}

ToastOverlayView::~ToastOverlayView() {}

void ToastOverlayView::OnPaint(gfx::Canvas* canvas) {
  cc::PaintFlags flags;
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(kButtonBackgroundColor);
  canvas->DrawRoundRect(GetLocalBounds(), 2, flags);
  views::View::OnPaint(canvas);
}

gfx::Size ToastOverlayView::GetMinimumSize() const {
  return gfx::Size(kToastMinimumWidth, 0);
}

gfx::Size ToastOverlayView::GetMaximumSize() const {
  gfx::Rect work_area_bounds = GetUserWorkAreaBounds();
  return gfx::Size(kToastMaximumWidth, work_area_bounds.height() - kOffset * 2);
}

void ToastOverlayView::ButtonPressed(views::Button* sender,
                                     const ui::Event& event) {
  DCHECK_EQ(button_, sender);
  overlay_->Show(false);
}

///////////////////////////////////////////////////////////////////////////////
//  ToastOverlay
ToastOverlay::ToastOverlay(Delegate* delegate,
                           const base::string16& text,
                           base::Optional<base::string16> dismiss_text)
    : delegate_(delegate),
      text_(text),
      dismiss_text_(dismiss_text),
      overlay_widget_(new views::Widget),
      overlay_view_(new ToastOverlayView(this, text, dismiss_text)),
      widget_size_(overlay_view_->GetPreferredSize()) {
  views::Widget::InitParams params;
  params.type = views::Widget::InitParams::TYPE_POPUP;
  params.name = "ToastOverlay";
  params.opacity = views::Widget::InitParams::TRANSLUCENT_WINDOW;
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.accept_events = true;
  params.keep_on_top = true;
  params.remove_standard_frame = true;
  params.bounds = CalculateOverlayBounds();
  // Show toasts above the app list and below the lock screen.
  GetRootWindowController(Shell::GetRootWindowForNewWindows())
      ->ConfigureWidgetInitParamsForContainer(
          overlay_widget_.get(), kShellWindowId_SystemModalContainer, &params);
  overlay_widget_->Init(params);
  overlay_widget_->SetVisibilityChangedAnimationsEnabled(true);
  overlay_widget_->SetContentsView(overlay_view_.get());
  overlay_widget_->SetBounds(CalculateOverlayBounds());

  aura::Window* overlay_window = overlay_widget_->GetNativeWindow();
  ::wm::SetWindowVisibilityAnimationType(
      overlay_window, ::wm::WINDOW_VISIBILITY_ANIMATION_TYPE_VERTICAL);
  ::wm::SetWindowVisibilityAnimationDuration(
      overlay_window,
      base::TimeDelta::FromMilliseconds(kSlideAnimationDurationMs));
}

ToastOverlay::~ToastOverlay() {
  overlay_widget_->Close();
}

void ToastOverlay::Show(bool visible) {
  if (overlay_widget_->GetLayer()->GetTargetVisibility() == visible)
    return;

  ui::LayerAnimator* animator = overlay_widget_->GetLayer()->GetAnimator();
  DCHECK(animator);
  if (animator->is_animating()) {
    // Showing during hiding animation doesn't happen since, ToastOverlay should
    // be one-time-use and not be reused.
    DCHECK(!visible);

    return;
  }

  base::TimeDelta original_duration = animator->GetTransitionDuration();
  ui::ScopedLayerAnimationSettings animation_settings(animator);
  // ScopedLayerAnimationSettings ctor chanes the transition duration, so change
  // back it to the original value (should be zero).
  animation_settings.SetTransitionDuration(original_duration);

  animation_settings.AddObserver(this);

  if (visible) {
    overlay_widget_->Show();

    // Notify accessibility about the overlay.
    overlay_view_->NotifyAccessibilityEvent(ui::AX_EVENT_ALERT, false);
  } else {
    overlay_widget_->Hide();
  }
}

gfx::Rect ToastOverlay::CalculateOverlayBounds() {
  gfx::Rect bounds = GetUserWorkAreaBounds();
  int target_y = bounds.bottom() - widget_size_.height() - kOffset;
  bounds.ClampToCenteredSize(widget_size_);
  bounds.set_y(target_y);
  return bounds;
}

void ToastOverlay::OnImplicitAnimationsScheduled() {}

void ToastOverlay::OnImplicitAnimationsCompleted() {
  if (!overlay_widget_->GetLayer()->GetTargetVisibility())
    delegate_->OnClosed();
}

views::Widget* ToastOverlay::widget_for_testing() {
  return overlay_widget_.get();
}

ToastOverlayButton* ToastOverlay::dismiss_button_for_testing() {
  return overlay_view_->button();
}

void ToastOverlay::ClickDismissButtonForTesting(const ui::Event& event) {
  DCHECK(overlay_view_->button());
  overlay_view_->button()->NotifyClick(event);
}

}  // namespace ash
