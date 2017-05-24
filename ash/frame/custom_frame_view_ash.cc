// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/frame/custom_frame_view_ash.h"

#include <algorithm>
#include <vector>

#include "ash/frame/caption_buttons/frame_caption_button_container_view.h"
#include "ash/frame/frame_border_hit_test.h"
#include "ash/frame/header_view.h"
#include "ash/shared/immersive_fullscreen_controller.h"
#include "ash/shared/immersive_fullscreen_controller_delegate.h"
#include "ash/shell_port.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_state_delegate.h"
#include "ash/wm/window_state_observer.h"
#include "ash/wm_window.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/view.h"
#include "ui/views/view_targeter.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace ash {

namespace {

///////////////////////////////////////////////////////////////////////////////
// CustomFrameViewAshWindowStateDelegate

// Handles a user's fullscreen request (Shift+F4/F4). Puts the window into
// immersive fullscreen if immersive fullscreen is enabled for non-browser
// windows.
class CustomFrameViewAshWindowStateDelegate : public wm::WindowStateDelegate,
                                              public wm::WindowStateObserver,
                                              public aura::WindowObserver {
 public:
  CustomFrameViewAshWindowStateDelegate(wm::WindowState* window_state,
                                        CustomFrameViewAsh* custom_frame_view,
                                        bool enable_immersive)
      : window_state_(nullptr) {
    // Add a window state observer to exit fullscreen properly in case
    // fullscreen is exited without going through
    // WindowState::ToggleFullscreen(). This is the case when exiting
    // immersive fullscreen via the "Restore" window control.
    // TODO(pkotwicz): This is a hack. Remove ASAP. http://crbug.com/319048
    window_state_ = window_state;
    window_state_->AddObserver(this);
    window_state_->window()->aura_window()->AddObserver(this);

    if (!enable_immersive)
      return;

    immersive_fullscreen_controller_ =
        ShellPort::Get()->CreateImmersiveFullscreenController();
    if (immersive_fullscreen_controller_) {
      custom_frame_view->InitImmersiveFullscreenControllerForView(
          immersive_fullscreen_controller_.get());
    }
  }
  ~CustomFrameViewAshWindowStateDelegate() override {
    if (window_state_) {
      window_state_->RemoveObserver(this);
      window_state_->window()->aura_window()->RemoveObserver(this);
    }
  }

 private:
  // Overridden from wm::WindowStateDelegate:
  bool ToggleFullscreen(wm::WindowState* window_state) override {
    bool enter_fullscreen = !window_state->IsFullscreen();
    if (enter_fullscreen)
      window_state_->window()->SetShowState(ui::SHOW_STATE_FULLSCREEN);
    else
      window_state->Restore();
    if (immersive_fullscreen_controller_) {
      immersive_fullscreen_controller_->SetEnabled(
          ImmersiveFullscreenController::WINDOW_TYPE_OTHER, enter_fullscreen);
    }
    return true;
  }
  // Overridden from aura::WindowObserver:
  void OnWindowDestroying(aura::Window* window) override {
    window_state_->RemoveObserver(this);
    window->RemoveObserver(this);
    window_state_ = nullptr;
  }
  // Overridden from wm::WindowStateObserver:
  void OnPostWindowStateTypeChange(wm::WindowState* window_state,
                                   wm::WindowStateType old_type) override {
    if (!window_state->IsFullscreen() && !window_state->IsMinimized() &&
        immersive_fullscreen_controller_ &&
        immersive_fullscreen_controller_->IsEnabled()) {
      immersive_fullscreen_controller_->SetEnabled(
          ImmersiveFullscreenController::WINDOW_TYPE_OTHER, false);
    }
  }

  wm::WindowState* window_state_;
  std::unique_ptr<ImmersiveFullscreenController>
      immersive_fullscreen_controller_;

  DISALLOW_COPY_AND_ASSIGN(CustomFrameViewAshWindowStateDelegate);
};

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// CustomFrameViewAsh::OverlayView

// View which takes up the entire widget and contains the HeaderView. HeaderView
// is a child of OverlayView to avoid creating a larger texture than necessary
// when painting the HeaderView to its own layer.
class CustomFrameViewAsh::OverlayView : public views::View,
                                        public views::ViewTargeterDelegate {
 public:
  explicit OverlayView(HeaderView* header_view);
  ~OverlayView() override;

  void SetHeaderHeight(base::Optional<int> height);

  // views::View:
  void Layout() override;
  const char* GetClassName() const override { return "OverlayView"; }

 private:
  // views::ViewTargeterDelegate:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override;

  HeaderView* header_view_;

  base::Optional<int> header_height_;

  DISALLOW_COPY_AND_ASSIGN(OverlayView);
};

CustomFrameViewAsh::OverlayView::OverlayView(HeaderView* header_view)
    : header_view_(header_view) {
  AddChildView(header_view);
  SetEventTargeter(
      std::unique_ptr<views::ViewTargeter>(new views::ViewTargeter(this)));
}

CustomFrameViewAsh::OverlayView::~OverlayView() {}

void CustomFrameViewAsh::OverlayView::SetHeaderHeight(
    base::Optional<int> height) {
  if (header_height_ == height)
    return;

  header_height_ = height;
  Layout();
}

///////////////////////////////////////////////////////////////////////////////
// CustomFrameViewAsh::OverlayView, views::View overrides:

void CustomFrameViewAsh::OverlayView::Layout() {
  // Layout |header_view_| because layout affects the result of
  // GetPreferredOnScreenHeight().
  header_view_->Layout();

  int onscreen_height = header_height_
                            ? *header_height_
                            : header_view_->GetPreferredOnScreenHeight();
  if (onscreen_height == 0) {
    header_view_->SetVisible(false);
  } else {
    const int height =
        header_height_ ? *header_height_ : header_view_->GetPreferredHeight();
    header_view_->SetBounds(0, onscreen_height - height, width(), height);
    header_view_->SetVisible(true);
  }
}

///////////////////////////////////////////////////////////////////////////////
// CustomFrameViewAsh::OverlayView, views::ViewTargeterDelegate overrides:

bool CustomFrameViewAsh::OverlayView::DoesIntersectRect(
    const views::View* target,
    const gfx::Rect& rect) const {
  CHECK_EQ(target, this);
  // Grab events in the header view. Return false for other events so that they
  // can be handled by the client view.
  return header_view_->HitTestRect(rect);
}

////////////////////////////////////////////////////////////////////////////////
// CustomFrameViewAsh, public:

// static
const char CustomFrameViewAsh::kViewClassName[] = "CustomFrameViewAsh";

CustomFrameViewAsh::CustomFrameViewAsh(
    views::Widget* frame,
    ImmersiveFullscreenControllerDelegate* immersive_delegate,
    bool enable_immersive,
    mojom::WindowStyle window_style)
    : frame_(frame),
      header_view_(new HeaderView(frame, window_style)),
      overlay_view_(new OverlayView(header_view_)),
      immersive_delegate_(immersive_delegate ? immersive_delegate
                                             : header_view_) {
  WmWindow* frame_window = WmWindow::Get(frame->GetNativeWindow());
  frame_window->InstallResizeHandleWindowTargeter(nullptr);
  // |header_view_| is set as the non client view's overlay view so that it can
  // overlay the web contents in immersive fullscreen.
  frame->non_client_view()->SetOverlayView(overlay_view_);
  frame_window->aura_window()->SetProperty(
      aura::client::kTopViewColor, header_view_->GetInactiveFrameColor());

  // A delegate for a more complex way of fullscreening the window may already
  // be set. This is the case for packaged apps.
  wm::WindowState* window_state = frame_window->GetWindowState();
  if (!window_state->HasDelegate()) {
    window_state->SetDelegate(std::unique_ptr<wm::WindowStateDelegate>(
        new CustomFrameViewAshWindowStateDelegate(window_state, this,
                                                  enable_immersive)));
  }
}

CustomFrameViewAsh::~CustomFrameViewAsh() {}

void CustomFrameViewAsh::InitImmersiveFullscreenControllerForView(
    ImmersiveFullscreenController* immersive_fullscreen_controller) {
  immersive_fullscreen_controller->Init(immersive_delegate_, frame_,
                                        header_view_);
}

void CustomFrameViewAsh::SetFrameColors(SkColor active_frame_color,
                                        SkColor inactive_frame_color) {
  header_view_->SetFrameColors(active_frame_color, inactive_frame_color);
  WmWindow* frame_window = WmWindow::Get(frame_->GetNativeWindow());
  frame_window->aura_window()->SetProperty(
      aura::client::kTopViewColor, header_view_->GetInactiveFrameColor());
}

void CustomFrameViewAsh::SetHeaderHeight(base::Optional<int> height) {
  overlay_view_->SetHeaderHeight(height);
}

views::View* CustomFrameViewAsh::header_view() {
  return header_view_;
}

////////////////////////////////////////////////////////////////////////////////
// CustomFrameViewAsh, views::NonClientFrameView overrides:

gfx::Rect CustomFrameViewAsh::GetBoundsForClientView() const {
  gfx::Rect client_bounds = bounds();
  client_bounds.Inset(0, NonClientTopBorderHeight(), 0, 0);
  return client_bounds;
}

gfx::Rect CustomFrameViewAsh::GetWindowBoundsForClientBounds(
    const gfx::Rect& client_bounds) const {
  gfx::Rect window_bounds = client_bounds;
  window_bounds.Inset(0, -NonClientTopBorderHeight(), 0, 0);
  return window_bounds;
}

int CustomFrameViewAsh::NonClientHitTest(const gfx::Point& point) {
  return FrameBorderNonClientHitTest(
      this, header_view_->caption_button_container(), point);
}

void CustomFrameViewAsh::GetWindowMask(const gfx::Size& size,
                                       gfx::Path* window_mask) {
  // No window masks in Aura.
}

void CustomFrameViewAsh::ResetWindowControls() {
  header_view_->ResetWindowControls();
}

void CustomFrameViewAsh::UpdateWindowIcon() {}

void CustomFrameViewAsh::UpdateWindowTitle() {
  header_view_->SchedulePaintForTitle();
}

void CustomFrameViewAsh::SizeConstraintsChanged() {
  header_view_->SizeConstraintsChanged();
}

void CustomFrameViewAsh::ActivationChanged(bool active) {
  // The icons differ between active and inactive.
  header_view_->SchedulePaint();
}

////////////////////////////////////////////////////////////////////////////////
// CustomFrameViewAsh, views::View overrides:

gfx::Size CustomFrameViewAsh::CalculatePreferredSize() const {
  gfx::Size pref = frame_->client_view()->GetPreferredSize();
  gfx::Rect bounds(0, 0, pref.width(), pref.height());
  return frame_->non_client_view()
      ->GetWindowBoundsForClientBounds(bounds)
      .size();
}

void CustomFrameViewAsh::Layout() {
  views::NonClientFrameView::Layout();
  WmWindow* frame_window = WmWindow::Get(frame_->GetNativeWindow());
  frame_window->aura_window()->SetProperty(aura::client::kTopViewInset,
                                           NonClientTopBorderHeight());
}

const char* CustomFrameViewAsh::GetClassName() const {
  return kViewClassName;
}

gfx::Size CustomFrameViewAsh::GetMinimumSize() const {
  gfx::Size min_client_view_size(frame_->client_view()->GetMinimumSize());
  return gfx::Size(
      std::max(header_view_->GetMinimumWidth(), min_client_view_size.width()),
      NonClientTopBorderHeight() + min_client_view_size.height());
}

gfx::Size CustomFrameViewAsh::GetMaximumSize() const {
  gfx::Size max_client_size(frame_->client_view()->GetMaximumSize());
  int width = 0;
  int height = 0;

  if (max_client_size.width() > 0)
    width = std::max(header_view_->GetMinimumWidth(), max_client_size.width());
  if (max_client_size.height() > 0)
    height = NonClientTopBorderHeight() + max_client_size.height();

  return gfx::Size(width, height);
}

void CustomFrameViewAsh::SchedulePaintInRect(const gfx::Rect& r) {
  // We may end up here before |header_view_| has been added to the Widget.
  if (header_view_->GetWidget()) {
    // The HeaderView is not a child of CustomFrameViewAsh. Redirect the paint
    // to HeaderView instead.
    gfx::RectF to_paint(r);
    views::View::ConvertRectToTarget(this, header_view_, &to_paint);
    header_view_->SchedulePaintInRect(gfx::ToEnclosingRect(to_paint));
  } else {
    views::NonClientFrameView::SchedulePaintInRect(r);
  }
}

void CustomFrameViewAsh::VisibilityChanged(views::View* starting_from,
                                           bool is_visible) {
  if (is_visible)
    header_view_->UpdateAvatarIcon();
}

views::View* CustomFrameViewAsh::GetHeaderView() {
  return header_view_;
}

const views::View* CustomFrameViewAsh::GetAvatarIconViewForTest() const {
  return header_view_->avatar_icon();
}

////////////////////////////////////////////////////////////////////////////////
// CustomFrameViewAsh, private:

// views::NonClientFrameView:
bool CustomFrameViewAsh::DoesIntersectRect(const views::View* target,
                                           const gfx::Rect& rect) const {
  CHECK_EQ(target, this);
  // NonClientView hit tests the NonClientFrameView first instead of going in
  // z-order. Return false so that events get to the OverlayView.
  return false;
}

FrameCaptionButtonContainerView*
CustomFrameViewAsh::GetFrameCaptionButtonContainerViewForTest() {
  return header_view_->caption_button_container();
}

int CustomFrameViewAsh::NonClientTopBorderHeight() const {
  return frame_->IsFullscreen() ? 0 : header_view_->GetPreferredHeight();
}

}  // namespace ash
