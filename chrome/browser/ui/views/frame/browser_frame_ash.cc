// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/browser_frame_ash.h"

#include "ash/ash_switches.h"
#include "ash/shell.h"
#include "ash/wm/window_properties.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_state_delegate.h"
#include "ash/wm/window_util.h"
#include "base/macros.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/views/view.h"

namespace {

// BrowserWindowStateDelegate class handles a user's fullscreen
// request (Shift+F4/F4).
class BrowserWindowStateDelegate : public ash::wm::WindowStateDelegate {
 public:
  explicit BrowserWindowStateDelegate(Browser* browser)
      : browser_(browser) {
    DCHECK(browser_);
  }
  ~BrowserWindowStateDelegate() override {}

  // Overridden from ash::wm::WindowStateDelegate.
  bool ToggleFullscreen(ash::wm::WindowState* window_state) override {
    DCHECK(window_state->IsFullscreen() || window_state->CanMaximize());
    // Windows which cannot be maximized should not be fullscreened.
    if (!window_state->IsFullscreen() && !window_state->CanMaximize())
      return true;
    chrome::ToggleFullscreenMode(browser_);
    return true;
  }
 private:
  Browser* browser_;  // not owned.

  DISALLOW_COPY_AND_ASSIGN(BrowserWindowStateDelegate);
};

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// BrowserFrameAsh, public:

// static
const char BrowserFrameAsh::kWindowName[] = "BrowserFrameAsh";

BrowserFrameAsh::BrowserFrameAsh(BrowserFrame* browser_frame,
                                 BrowserView* browser_view)
    : views::NativeWidgetAura(browser_frame),
      browser_view_(browser_view) {
  GetNativeWindow()->SetName(kWindowName);
  Browser* browser = browser_view->browser();
  ash::wm::WindowState* window_state =
      ash::wm::GetWindowState(GetNativeWindow());
  window_state->SetDelegate(std::unique_ptr<ash::wm::WindowStateDelegate>(
      new BrowserWindowStateDelegate(browser)));

  // Turn on auto window management if we don't need an explicit bounds.
  // This way the requested bounds are honored.
  if (!browser->bounds_overridden() && !browser->is_session_restore())
    SetWindowAutoManaged();
#if defined(OS_CHROMEOS)
  // For legacy reasons v1 apps (like Secure Shell) are allowed to consume keys
  // like brightness, volume, etc. Otherwise these keys are handled by the
  // Ash window manager.
  window_state->set_can_consume_system_keys(browser->is_app());
#endif  // defined(OS_CHROMEOS)
}

///////////////////////////////////////////////////////////////////////////////
// BrowserFrameAsh, views::NativeWidgetAura overrides:

void BrowserFrameAsh::OnWindowTargetVisibilityChanged(bool visible) {
  if (visible) {
    // Once the window has been shown we know the requested bounds
    // (if provided) have been honored and we can switch on window management.
    SetWindowAutoManaged();
  }
  views::NativeWidgetAura::OnWindowTargetVisibilityChanged(visible);
}

////////////////////////////////////////////////////////////////////////////////
// BrowserFrameAsh, NativeBrowserFrame implementation:

bool BrowserFrameAsh::ShouldSaveWindowPlacement() const {
  return nullptr == GetWidget()->GetNativeWindow()->GetProperty(
                        ash::kRestoreBoundsOverrideKey);
}

void BrowserFrameAsh::GetWindowPlacement(
    gfx::Rect* bounds,
    ui::WindowShowState* show_state) const {
  gfx::Rect* override_bounds = GetWidget()->GetNativeWindow()->GetProperty(
                                   ash::kRestoreBoundsOverrideKey);
  if (override_bounds && !override_bounds->IsEmpty()) {
    *bounds = *override_bounds;
    *show_state = GetWidget()->GetNativeWindow()->GetProperty(
                      ash::kRestoreShowStateOverrideKey);
  } else {
    *bounds = GetWidget()->GetRestoredBounds();
    *show_state = GetWidget()->GetNativeWindow()->GetProperty(
                      aura::client::kShowStateKey);
  }

  if (*show_state != ui::SHOW_STATE_MAXIMIZED &&
      *show_state != ui::SHOW_STATE_MINIMIZED) {
    *show_state = ui::SHOW_STATE_NORMAL;
  }
}

bool BrowserFrameAsh::PreHandleKeyboardEvent(
    const content::NativeWebKeyboardEvent& event) {
  return false;
}

bool BrowserFrameAsh::HandleKeyboardEvent(
    const content::NativeWebKeyboardEvent& event) {
  return false;
}

views::Widget::InitParams BrowserFrameAsh::GetWidgetParams() {
  views::Widget::InitParams params;
  params.native_widget = this;

  params.context = ash::Shell::GetPrimaryRootWindow();
#if defined(OS_WIN)
  // If this window is under ASH on Windows, we need it to be translucent.
  params.opacity = views::Widget::InitParams::TRANSLUCENT_WINDOW;
#endif

  return params;
}

bool BrowserFrameAsh::UseCustomFrame() const {
  return true;
}

bool BrowserFrameAsh::UsesNativeSystemMenu() const {
  return false;
}

int BrowserFrameAsh::GetMinimizeButtonOffset() const {
  return 0;
}

BrowserFrameAsh::~BrowserFrameAsh() {
}

///////////////////////////////////////////////////////////////////////////////
// BrowserFrameAsh, private:

void BrowserFrameAsh::SetWindowAutoManaged() {
  // For browser window in Chrome OS, we should only enable the auto window
  // management logic for tabbed browser.
  if (!browser_view_->browser()->is_type_popup()) {
    ash::wm::GetWindowState(GetNativeWindow())
        ->set_window_position_managed(true);
  }
}
