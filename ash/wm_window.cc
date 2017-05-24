// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm_window.h"

#include "ash/ash_constants.h"
#include "ash/public/cpp/config.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/root_window_controller.h"
#include "ash/shell.h"
#include "ash/wm/resize_handle_window_targeter.h"
#include "ash/wm/resize_shadow_controller.h"
#include "ash/wm/widget_finder.h"
#include "ash/wm/window_animations.h"
#include "ash/wm/window_properties.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm_transient_window_observer.h"
#include "base/memory/ptr_util.h"
#include "services/ui/public/interfaces/window_manager_constants.mojom.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/layout_manager.h"
#include "ui/aura/mus/window_manager_delegate.h"
#include "ui/aura/mus/window_mus.h"
#include "ui/aura/mus/window_tree_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/aura/window_observer.h"
#include "ui/base/class_property.h"
#include "ui/base/hit_test.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/wm/core/coordinate_conversion.h"
#include "ui/wm/core/easy_resize_window_targeter.h"
#include "ui/wm/core/ime_util_chromeos.h"
#include "ui/wm/core/transient_window_manager.h"
#include "ui/wm/core/visibility_controller.h"
#include "ui/wm/core/window_util.h"

DECLARE_UI_CLASS_PROPERTY_TYPE(ash::WmWindow*);

namespace ash {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(WmWindow, kWmWindowKey, nullptr);

static_assert(aura::Window::kInitialId == kShellWindowId_Invalid,
              "ids must match");

namespace {

// A tentative class to set the bounds on the window.
// TODO(oshima): Once all logic is cleaned up, move this to the real layout
// manager with proper friendship.
class BoundsSetter : public aura::LayoutManager {
 public:
  BoundsSetter() {}
  ~BoundsSetter() override {}

  // aura::LayoutManager overrides:
  void OnWindowResized() override {}
  void OnWindowAddedToLayout(aura::Window* child) override {}
  void OnWillRemoveWindowFromLayout(aura::Window* child) override {}
  void OnWindowRemovedFromLayout(aura::Window* child) override {}
  void OnChildWindowVisibilityChanged(aura::Window* child,
                                      bool visible) override {}
  void SetChildBounds(aura::Window* child,
                      const gfx::Rect& requested_bounds) override {}

  void SetBounds(aura::Window* window, const gfx::Rect& bounds) {
    SetChildBoundsDirect(window, bounds);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(BoundsSetter);
};

}  // namespace

// static
bool WmWindow::default_use_empty_minimum_size_for_testing_ = false;

WmWindow::~WmWindow() {
  if (added_transient_observer_)
    ::wm::TransientWindowManager::Get(window_)->RemoveObserver(this);

  window_->RemoveObserver(this);
}

// static
const WmWindow* WmWindow::Get(const aura::Window* window) {
  if (!window)
    return nullptr;

  const WmWindow* wm_window = window->GetProperty(kWmWindowKey);
  if (wm_window)
    return wm_window;
  // WmWindow is owned by the aura::Window.
  // TODO(sky): fix constness.
  return new WmWindow(const_cast<aura::Window*>(window));
}

// static
std::vector<WmWindow*> WmWindow::FromAuraWindows(
    const std::vector<aura::Window*>& aura_windows) {
  std::vector<WmWindow*> result(aura_windows.size());
  for (size_t i = 0; i < aura_windows.size(); ++i)
    result[i] = Get(aura_windows[i]);
  return result;
}

// static
std::vector<aura::Window*> WmWindow::ToAuraWindows(
    const std::vector<WmWindow*>& windows) {
  std::vector<aura::Window*> result(windows.size());
  for (size_t i = 0; i < windows.size(); ++i)
    result[i] = WmWindow::GetAuraWindow(windows[i]);
  return result;
}

// static
const aura::Window* WmWindow::GetAuraWindow(const WmWindow* wm_window) {
  return wm_window ? static_cast<const WmWindow*>(wm_window)->aura_window()
                   : nullptr;
}

bool WmWindow::ShouldUseExtendedHitRegion() const {
  const WmWindow* parent = Get(window_->parent());
  return parent &&
         static_cast<const WmWindow*>(parent)
             ->children_use_extended_hit_region_;
}

void WmWindow::Destroy() {
  delete window_;
  // WARNING: this has been deleted.
}

const WmWindow* WmWindow::GetRootWindow() const {
  return Get(window_->GetRootWindow());
}

RootWindowController* WmWindow::GetRootWindowController() {
  aura::Window* root = window_->GetRootWindow();
  return root ? RootWindowController::ForWindow(root) : nullptr;
}

ui::wm::WindowType WmWindow::GetType() const {
  return window_->type();
}

int WmWindow::GetAppType() const {
  return window_->GetProperty(aura::client::kAppType);
}

void WmWindow::SetAppType(int app_type) const {
  window_->SetProperty(aura::client::kAppType, app_type);
}

ui::Layer* WmWindow::GetLayer() {
  return window_->layer();
}

bool WmWindow::GetLayerTargetVisibility() {
  return GetLayer()->GetTargetVisibility();
}

bool WmWindow::GetLayerVisible() {
  return GetLayer()->visible();
}

display::Display WmWindow::GetDisplayNearestWindow() {
  return display::Screen::GetScreen()->GetDisplayNearestWindow(window_);
}

bool WmWindow::HasNonClientArea() {
  return window_->delegate() ? true : false;
}

int WmWindow::GetNonClientComponent(const gfx::Point& location) {
  return window_->delegate()
             ? window_->delegate()->GetNonClientComponent(location)
             : HTNOWHERE;
}

gfx::Point WmWindow::ConvertPointToTarget(const WmWindow* target,
                                          const gfx::Point& point) const {
  gfx::Point result(point);
  aura::Window::ConvertPointToTarget(window_, GetAuraWindow(target), &result);
  return result;
}

gfx::Point WmWindow::ConvertPointToScreen(const gfx::Point& point) const {
  gfx::Point result(point);
  ::wm::ConvertPointToScreen(window_, &result);
  return result;
}

gfx::Point WmWindow::ConvertPointFromScreen(const gfx::Point& point) const {
  gfx::Point result(point);
  ::wm::ConvertPointFromScreen(window_, &result);
  return result;
}

gfx::Rect WmWindow::ConvertRectToScreen(const gfx::Rect& rect) const {
  gfx::Rect result(rect);
  ::wm::ConvertRectToScreen(window_, &result);
  return result;
}

gfx::Rect WmWindow::ConvertRectFromScreen(const gfx::Rect& rect) const {
  gfx::Rect result(rect);
  ::wm::ConvertRectFromScreen(window_, &result);
  return result;
}

gfx::Size WmWindow::GetMinimumSize() const {
  return window_->delegate() && !use_empty_minimum_size_for_testing_
             ? window_->delegate()->GetMinimumSize()
             : gfx::Size();
}

gfx::Size WmWindow::GetMaximumSize() const {
  return window_->delegate() ? window_->delegate()->GetMaximumSize()
                             : gfx::Size();
}

bool WmWindow::GetTargetVisibility() const {
  return window_->TargetVisibility();
}

bool WmWindow::IsVisible() const {
  return window_->IsVisible();
}

void WmWindow::SetOpacity(float opacity) {
  window_->layer()->SetOpacity(opacity);
}

float WmWindow::GetTargetOpacity() const {
  return window_->layer()->GetTargetOpacity();
}

gfx::Rect WmWindow::GetMinimizeAnimationTargetBoundsInScreen() const {
  return ash::GetMinimizeAnimationTargetBoundsInScreen(window_);
}

void WmWindow::SetTransform(const gfx::Transform& transform) {
  window_->SetTransform(transform);
}

gfx::Transform WmWindow::GetTargetTransform() const {
  return window_->layer()->GetTargetTransform();
}

bool WmWindow::IsSystemModal() const {
  return window_->GetProperty(aura::client::kModalKey) == ui::MODAL_TYPE_SYSTEM;
}

const wm::WindowState* WmWindow::GetWindowState() const {
  return ash::wm::GetWindowState(window_);
}

WmWindow* WmWindow::GetToplevelWindow() {
  return Get(window_->GetToplevelWindow());
}

WmWindow* WmWindow::GetToplevelWindowForFocus() {
  return Get(::wm::GetToplevelWindow(window_));
}

void WmWindow::SetParentUsingContext(WmWindow* context,
                                     const gfx::Rect& screen_bounds) {
  aura::client::ParentWindowWithContext(window_, GetAuraWindow(context),
                                        screen_bounds);
}

void WmWindow::AddChild(WmWindow* window) {
  window_->AddChild(GetAuraWindow(window));
}

void WmWindow::RemoveChild(WmWindow* child) {
  window_->RemoveChild(GetAuraWindow(child));
}

const WmWindow* WmWindow::GetParent() const {
  return Get(window_->parent());
}

const WmWindow* WmWindow::GetTransientParent() const {
  return Get(::wm::GetTransientParent(window_));
}

std::vector<WmWindow*> WmWindow::GetTransientChildren() {
  return FromAuraWindows(::wm::GetTransientChildren(window_));
}

bool WmWindow::MoveToEventRoot(const ui::Event& event) {
  return ash::wm::MoveWindowToEventRoot(window_, event);
}

void WmWindow::SetVisibilityChangesAnimated() {
  ::wm::SetWindowVisibilityChangesAnimated(window_);
}

void WmWindow::SetVisibilityAnimationType(int type) {
  ::wm::SetWindowVisibilityAnimationType(window_, type);
}

void WmWindow::SetVisibilityAnimationDuration(base::TimeDelta delta) {
  ::wm::SetWindowVisibilityAnimationDuration(window_, delta);
}

void WmWindow::SetVisibilityAnimationTransition(
    ::wm::WindowVisibilityAnimationTransition transition) {
  ::wm::SetWindowVisibilityAnimationTransition(window_, transition);
}

void WmWindow::Animate(::wm::WindowAnimationType type) {
  ::wm::AnimateWindow(window_, type);
}

void WmWindow::StopAnimatingProperty(
    ui::LayerAnimationElement::AnimatableProperty property) {
  window_->layer()->GetAnimator()->StopAnimatingProperty(property);
}

void WmWindow::SetChildWindowVisibilityChangesAnimated() {
  ::wm::SetChildWindowVisibilityChangesAnimated(window_);
}

void WmWindow::SetMasksToBounds(bool value) {
  window_->layer()->SetMasksToBounds(value);
}

void WmWindow::SetBounds(const gfx::Rect& bounds) {
  window_->SetBounds(bounds);
}

void WmWindow::SetBoundsWithTransitionDelay(const gfx::Rect& bounds,
                                            base::TimeDelta delta) {
  if (::wm::WindowAnimationsDisabled(window_)) {
    window_->SetBounds(bounds);
    return;
  }

  ui::ScopedLayerAnimationSettings settings(window_->layer()->GetAnimator());
  settings.SetTransitionDuration(delta);
  window_->SetBounds(bounds);
}

void WmWindow::SetBoundsDirect(const gfx::Rect& bounds) {
  BoundsSetter().SetBounds(window_, bounds);
  wm::SnapWindowToPixelBoundary(window_);
}

void WmWindow::SetBoundsDirectAnimated(const gfx::Rect& bounds) {
  const int kBoundsChangeSlideDurationMs = 120;

  ui::Layer* layer = window_->layer();
  ui::ScopedLayerAnimationSettings slide_settings(layer->GetAnimator());
  slide_settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  slide_settings.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(kBoundsChangeSlideDurationMs));
  SetBoundsDirect(bounds);
}

void WmWindow::SetBoundsDirectCrossFade(const gfx::Rect& bounds) {
  const gfx::Rect old_bounds = window_->bounds();

  // Create fresh layers for the window and all its children to paint into.
  // Takes ownership of the old layer and all its children, which will be
  // cleaned up after the animation completes.
  // Specify |set_bounds| to true here to keep the old bounds in the child
  // windows of |window|.
  std::unique_ptr<ui::LayerTreeOwner> old_layer_owner =
      ::wm::RecreateLayers(window_);
  ui::Layer* old_layer = old_layer_owner->root();
  DCHECK(old_layer);
  ui::Layer* new_layer = window_->layer();

  // Resize the window to the new size, which will force a layout and paint.
  SetBoundsDirect(bounds);

  // Ensure the higher-resolution layer is on top.
  bool old_on_top = (old_bounds.width() > bounds.width());
  if (old_on_top)
    old_layer->parent()->StackBelow(new_layer, old_layer);
  else
    old_layer->parent()->StackAbove(new_layer, old_layer);

  CrossFadeAnimation(window_, std::move(old_layer_owner), gfx::Tween::EASE_OUT);
}

void WmWindow::SetBoundsInScreen(const gfx::Rect& bounds_in_screen,
                                 const display::Display& dst_display) {
  window_->SetBoundsInScreen(bounds_in_screen, dst_display);
}

gfx::Rect WmWindow::GetBoundsInScreen() const {
  return window_->GetBoundsInScreen();
}

const gfx::Rect& WmWindow::GetBounds() const {
  return window_->bounds();
}

gfx::Rect WmWindow::GetTargetBounds() {
  return window_->GetTargetBounds();
}

void WmWindow::ClearRestoreBounds() {
  window_->ClearProperty(aura::client::kRestoreBoundsKey);
  window_->ClearProperty(::wm::kVirtualKeyboardRestoreBoundsKey);
}

void WmWindow::SetRestoreBoundsInScreen(const gfx::Rect& bounds) {
  window_->SetProperty(aura::client::kRestoreBoundsKey, new gfx::Rect(bounds));
}

gfx::Rect WmWindow::GetRestoreBoundsInScreen() const {
  gfx::Rect* bounds = window_->GetProperty(aura::client::kRestoreBoundsKey);
  return bounds ? *bounds : gfx::Rect();
}

bool WmWindow::Contains(const WmWindow* other) const {
  return other ? window_->Contains(static_cast<const WmWindow*>(other)->window_)
               : false;
}

void WmWindow::SetShowState(ui::WindowShowState show_state) {
  window_->SetProperty(aura::client::kShowStateKey, show_state);
}

ui::WindowShowState WmWindow::GetShowState() const {
  return window_->GetProperty(aura::client::kShowStateKey);
}

void WmWindow::SetPreFullscreenShowState(ui::WindowShowState show_state) {
  // We should never store the ui::SHOW_STATE_MINIMIZED as the show state before
  // fullscreen.
  DCHECK_NE(show_state, ui::SHOW_STATE_MINIMIZED);
  window_->SetProperty(aura::client::kPreFullscreenShowStateKey, show_state);
}

void WmWindow::SetRestoreOverrides(const gfx::Rect& bounds_override,
                                   ui::WindowShowState window_state_override) {
  if (bounds_override.IsEmpty()) {
    window_->ClearProperty(kRestoreShowStateOverrideKey);
    window_->ClearProperty(kRestoreBoundsOverrideKey);
    return;
  }
  window_->SetProperty(kRestoreShowStateOverrideKey, window_state_override);
  window_->SetProperty(kRestoreBoundsOverrideKey,
                       new gfx::Rect(bounds_override));
}

void WmWindow::SetLockedToRoot(bool value) {
  window_->SetProperty(kLockedToRootKey, value);
}

bool WmWindow::IsLockedToRoot() const {
  return window_->GetProperty(kLockedToRootKey);
}

void WmWindow::SetCapture() {
  window_->SetCapture();
}

bool WmWindow::HasCapture() {
  return window_->HasCapture();
}

void WmWindow::ReleaseCapture() {
  window_->ReleaseCapture();
}

bool WmWindow::HasRestoreBounds() const {
  return window_->GetProperty(aura::client::kRestoreBoundsKey) != nullptr;
}

bool WmWindow::CanMaximize() const {
  return (window_->GetProperty(aura::client::kResizeBehaviorKey) &
          ui::mojom::kResizeBehaviorCanMaximize) != 0;
}

bool WmWindow::CanMinimize() const {
  return (window_->GetProperty(aura::client::kResizeBehaviorKey) &
          ui::mojom::kResizeBehaviorCanMinimize) != 0;
}

bool WmWindow::CanResize() const {
  return (window_->GetProperty(aura::client::kResizeBehaviorKey) &
          ui::mojom::kResizeBehaviorCanResize) != 0;
}

bool WmWindow::CanActivate() const {
  // TODO(sky): for aura-mus need to key off CanFocus() as well, which is not
  // currently mirrored to ash.
  return ::wm::CanActivateWindow(window_);
}

void WmWindow::StackChildAtTop(WmWindow* child) {
  window_->StackChildAtTop(GetAuraWindow(child));
}

void WmWindow::StackChildAtBottom(WmWindow* child) {
  window_->StackChildAtBottom(GetAuraWindow(child));
}

void WmWindow::StackChildAbove(WmWindow* child, WmWindow* target) {
  window_->StackChildAbove(GetAuraWindow(child), GetAuraWindow(target));
}

void WmWindow::StackChildBelow(WmWindow* child, WmWindow* target) {
  window_->StackChildBelow(GetAuraWindow(child), GetAuraWindow(target));
}

void WmWindow::SetAlwaysOnTop(bool value) {
  window_->SetProperty(aura::client::kAlwaysOnTopKey, value);
}

bool WmWindow::IsAlwaysOnTop() const {
  return window_->GetProperty(aura::client::kAlwaysOnTopKey);
}

void WmWindow::Hide() {
  window_->Hide();
}

void WmWindow::Show() {
  window_->Show();
}

void WmWindow::CloseWidget() {
  if (Shell::GetAshConfig() == Config::MASH &&
      aura_window()->GetProperty(kWidgetCreationTypeKey) ==
          WidgetCreationType::FOR_CLIENT) {
    // NOTE: in the FOR_CLIENT case there is not necessarily a widget associated
    // with the window. Mash only creates widgets for top level windows if mash
    // renders the non-client frame.
    DCHECK(Shell::window_manager_client());
    Shell::window_manager_client()->RequestClose(aura_window());
    return;
  }
  views::Widget* widget = GetInternalWidgetForWindow(window_);
  DCHECK(widget);
  widget->Close();
}

void WmWindow::SetFocused() {
  aura::client::GetFocusClient(window_)->FocusWindow(window_);
}

bool WmWindow::IsFocused() const {
  return window_->HasFocus();
}

bool WmWindow::IsActive() const {
  return wm::IsActiveWindow(window_);
}

void WmWindow::Activate() {
  wm::ActivateWindow(window_);
}

void WmWindow::Deactivate() {
  wm::DeactivateWindow(window_);
}

void WmWindow::SetFullscreen(bool fullscreen) {
  ::wm::SetWindowFullscreen(window_, fullscreen);
}

void WmWindow::Maximize() {
  window_->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
}

void WmWindow::Minimize() {
  window_->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
}

void WmWindow::Unminimize() {
  window_->SetProperty(
      aura::client::kShowStateKey,
      window_->GetProperty(aura::client::kPreMinimizedShowStateKey));
  window_->ClearProperty(aura::client::kPreMinimizedShowStateKey);
}

std::vector<WmWindow*> WmWindow::GetChildren() {
  return FromAuraWindows(window_->children());
}

WmWindow* WmWindow::GetChildByShellWindowId(int id) {
  return Get(window_->GetChildById(id));
}

void WmWindow::ShowResizeShadow(int component) {
  if (Shell::GetAshConfig() == Config::MASH) {
    // TODO: http://crbug.com/640773.
    return;
  }
  ResizeShadowController* resize_shadow_controller =
      Shell::Get()->resize_shadow_controller();
  if (resize_shadow_controller)
    resize_shadow_controller->ShowShadow(window_, component);
}

void WmWindow::HideResizeShadow() {
  if (Shell::GetAshConfig() == Config::MASH) {
    // TODO: http://crbug.com/640773.
    return;
  }
  ResizeShadowController* resize_shadow_controller =
      Shell::Get()->resize_shadow_controller();
  if (resize_shadow_controller)
    resize_shadow_controller->HideShadow(window_);
}

void WmWindow::InstallResizeHandleWindowTargeter(
    ImmersiveFullscreenController* immersive_fullscreen_controller) {
  window_->SetEventTargeter(base::MakeUnique<ResizeHandleWindowTargeter>(
      window_, immersive_fullscreen_controller));
}

void WmWindow::SetBoundsInScreenBehaviorForChildren(
    BoundsInScreenBehavior behavior) {
  window_->SetProperty(
      kUsesScreenCoordinatesKey,
      behavior == BoundsInScreenBehavior::USE_SCREEN_COORDINATES);
}

void WmWindow::SetSnapsChildrenToPhysicalPixelBoundary() {
  wm::SetSnapsChildrenToPhysicalPixelBoundary(window_);
}

void WmWindow::SnapToPixelBoundaryIfNecessary() {
  wm::SnapWindowToPixelBoundary(window_);
}

void WmWindow::SetChildrenUseExtendedHitRegion() {
  children_use_extended_hit_region_ = true;
  gfx::Insets mouse_extend(-kResizeOutsideBoundsSize, -kResizeOutsideBoundsSize,
                           -kResizeOutsideBoundsSize,
                           -kResizeOutsideBoundsSize);
  gfx::Insets touch_extend =
      mouse_extend.Scale(kResizeOutsideBoundsScaleForTouch);
  // TODO: EasyResizeWindowTargeter makes it so children get events outside
  // their bounds. This only works in mash when mash is providing the non-client
  // frame. Mus needs to support an api for the WindowManager that enables
  // events to be dispatched to windows outside the windows bounds that this
  // function calls into. http://crbug.com/679056.
  window_->SetEventTargeter(base::MakeUnique<::wm::EasyResizeWindowTargeter>(
      window_, mouse_extend, touch_extend));
}

void WmWindow::AddTransientWindowObserver(WmTransientWindowObserver* observer) {
  if (!added_transient_observer_) {
    added_transient_observer_ = true;
    ::wm::TransientWindowManager::Get(window_)->AddObserver(this);
  }
  transient_observers_.AddObserver(observer);
}

void WmWindow::RemoveTransientWindowObserver(
    WmTransientWindowObserver* observer) {
  transient_observers_.RemoveObserver(observer);
  if (added_transient_observer_ &&
      !transient_observers_.might_have_observers()) {
    added_transient_observer_ = false;
    ::wm::TransientWindowManager::Get(window_)->RemoveObserver(this);
  }
}

void WmWindow::AddLimitedPreTargetHandler(ui::EventHandler* handler) {
  // In mus AddPreTargetHandler() only works for windows created by this client.
  DCHECK(Shell::GetAshConfig() != Config::MASH ||
         Shell::window_tree_client()->WasCreatedByThisClient(
             aura::WindowMus::Get(window_)));
  window_->AddPreTargetHandler(handler);
}

void WmWindow::RemoveLimitedPreTargetHandler(ui::EventHandler* handler) {
  window_->RemovePreTargetHandler(handler);
}

WmWindow::WmWindow(aura::Window* window)
    : window_(window),
      use_empty_minimum_size_for_testing_(
          default_use_empty_minimum_size_for_testing_) {
  window_->AddObserver(this);
  window_->SetProperty(kWmWindowKey, this);
}

void WmWindow::OnWindowPropertyChanged(aura::Window* window,
                                       const void* key,
                                       intptr_t old) {
  if (key == aura::client::kShowStateKey) {
    ash::wm::GetWindowState(window_)->OnWindowShowStateChanged();
    return;
  }
  if (key == aura::client::kImmersiveFullscreenKey) {
    bool enable = window_->GetProperty(aura::client::kImmersiveFullscreenKey);
    GetWindowState()->set_in_immersive_fullscreen(enable);
    return;
  }
  if (key == kWindowPinTypeKey) {
    ash::wm::GetWindowState(window_)->OnWindowPinTypeChanged();
    return;
  }
}

void WmWindow::OnTransientChildAdded(aura::Window* window,
                                     aura::Window* transient) {
  for (auto& observer : transient_observers_)
    observer.OnTransientChildAdded(this, Get(transient));
}

void WmWindow::OnTransientChildRemoved(aura::Window* window,
                                       aura::Window* transient) {
  for (auto& observer : transient_observers_)
    observer.OnTransientChildRemoved(this, Get(transient));
}

}  // namespace ash
