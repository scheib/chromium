// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_WINDOW_H_
#define ASH_WM_WINDOW_H_

#include <memory>
#include <vector>

#include "ash/ash_export.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "base/strings/string16.h"
#include "base/time/time.h"
#include "ui/aura/window_observer.h"
#include "ui/base/ui_base_types.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/wm/core/transient_window_observer.h"
#include "ui/wm/core/window_animations.h"
#include "ui/wm/public/window_types.h"

namespace display {
class Display;
}

namespace gfx {
class Point;
class Rect;
class Size;
class Transform;
}

namespace ui {
class EventHandler;
class Layer;
}

namespace ash {

class ImmersiveFullscreenController;
class RootWindowController;
class WmTransientWindowObserver;
class WmWindowTestApi;
enum class WmWindowProperty;

namespace wm {
class WindowState;
}

// WmWindow abstracts away differences between ash running in classic mode
// and ash running with aura-mus.
//
// WmWindow is tied to the life of the underlying aura::Window. Use the
// static Get() function to obtain a WmWindow from an aura::Window.
class ASH_EXPORT WmWindow : public aura::WindowObserver,
                            public ::wm::TransientWindowObserver {
 public:
  // See comments in SetBoundsInScreen().
  enum class BoundsInScreenBehavior {
    USE_LOCAL_COORDINATES,
    USE_SCREEN_COORDINATES,
  };

  using Windows = std::vector<WmWindow*>;

  // NOTE: this class is owned by the corresponding window. You shouldn't delete
  // TODO(sky): friend deleter and make private.
  ~WmWindow() override;

  // Returns a WmWindow for an aura::Window, creating if necessary. |window| may
  // be null, in which case null is returned.
  static WmWindow* Get(aura::Window* window) {
    return const_cast<WmWindow*>(Get(const_cast<const aura::Window*>(window)));
  }
  static const WmWindow* Get(const aura::Window* window);

  static std::vector<WmWindow*> FromAuraWindows(
      const std::vector<aura::Window*>& aura_windows);
  static std::vector<aura::Window*> ToAuraWindows(
      const std::vector<WmWindow*>& windows);

  // Convenience for wm_window->aura_window(). Returns null if |wm_window| is
  // null.
  static aura::Window* GetAuraWindow(WmWindow* wm_window) {
    return const_cast<aura::Window*>(
        GetAuraWindow(const_cast<const WmWindow*>(wm_window)));
  }
  static const aura::Window* GetAuraWindow(const WmWindow* wm_window);

  aura::Window* aura_window() { return window_; }
  const aura::Window* aura_window() const { return window_; }

  // See description of |children_use_extended_hit_region_|.
  bool ShouldUseExtendedHitRegion() const;

  void Destroy();

  WmWindow* GetRootWindow() {
    return const_cast<WmWindow*>(
        const_cast<const WmWindow*>(this)->GetRootWindow());
  }
  const WmWindow* GetRootWindow() const;
  RootWindowController* GetRootWindowController();

  // See shell_window_ids.h for list of known ids.
  WmWindow* GetChildByShellWindowId(int id);

  ui::wm::WindowType GetType() const;
  int GetAppType() const;
  void SetAppType(int app_type) const;

  ui::Layer* GetLayer();

  // TODO(sky): these are temporary until GetLayer() always returns non-null.
  bool GetLayerTargetVisibility();
  bool GetLayerVisible();

  display::Display GetDisplayNearestWindow();

  bool HasNonClientArea();
  int GetNonClientComponent(const gfx::Point& location);
  gfx::Point ConvertPointToTarget(const WmWindow* target,
                                  const gfx::Point& point) const;

  gfx::Point ConvertPointToScreen(const gfx::Point& point) const;
  gfx::Point ConvertPointFromScreen(const gfx::Point& point) const;
  gfx::Rect ConvertRectToScreen(const gfx::Rect& rect) const;
  gfx::Rect ConvertRectFromScreen(const gfx::Rect& rect) const;

  gfx::Size GetMinimumSize() const;
  gfx::Size GetMaximumSize() const;

  // Returns the visibility requested by this window. IsVisible() takes into
  // account the visibility of the layer and ancestors, where as this tracks
  // whether Show() without a Hide() has been invoked.
  bool GetTargetVisibility() const;

  bool IsVisible() const;

  void SetOpacity(float opacity);
  float GetTargetOpacity() const;

  gfx::Rect GetMinimizeAnimationTargetBoundsInScreen() const;

  void SetTransform(const gfx::Transform& transform);
  gfx::Transform GetTargetTransform() const;

  bool IsSystemModal() const;

  wm::WindowState* GetWindowState() {
    return const_cast<wm::WindowState*>(
        const_cast<const WmWindow*>(this)->GetWindowState());
  }
  const wm::WindowState* GetWindowState() const;

  // The implementation of this matches aura::Window::GetToplevelWindow().
  WmWindow* GetToplevelWindow();

  // The implementation of this matches
  // aura::client::ActivationClient::GetToplevelWindow().
  WmWindow* GetToplevelWindowForFocus();

  // See aura::client::ParentWindowWithContext() for details of what this does.
  void SetParentUsingContext(WmWindow* context, const gfx::Rect& screen_bounds);
  void AddChild(WmWindow* window);
  void RemoveChild(WmWindow* child);

  WmWindow* GetParent() {
    return const_cast<WmWindow*>(
        const_cast<const WmWindow*>(this)->GetParent());
  }
  const WmWindow* GetParent() const;

  WmWindow* GetTransientParent() {
    return const_cast<WmWindow*>(
        const_cast<const WmWindow*>(this)->GetTransientParent());
  }
  const WmWindow* GetTransientParent() const;
  std::vector<WmWindow*> GetTransientChildren();

  // Moves this to the display where |event| occurred; returns true if moved.
  bool MoveToEventRoot(const ui::Event& event);

  // See wm::SetWindowVisibilityChangesAnimated() for details on what this
  // does.
  void SetVisibilityChangesAnimated();
  // |type| is WindowVisibilityAnimationType. Has to be an int to match aura.
  void SetVisibilityAnimationType(int type);
  void SetVisibilityAnimationDuration(base::TimeDelta delta);
  void SetVisibilityAnimationTransition(
      ::wm::WindowVisibilityAnimationTransition transition);
  void Animate(::wm::WindowAnimationType type);
  void StopAnimatingProperty(
      ui::LayerAnimationElement::AnimatableProperty property);
  void SetChildWindowVisibilityChangesAnimated();

  // See description in ui::Layer.
  void SetMasksToBounds(bool value);
  void SetBounds(const gfx::Rect& bounds);
  void SetBoundsWithTransitionDelay(const gfx::Rect& bounds,
                                    base::TimeDelta delta);
  // Sets the bounds in such a way that LayoutManagers are circumvented.
  void SetBoundsDirect(const gfx::Rect& bounds);
  void SetBoundsDirectAnimated(const gfx::Rect& bounds);
  void SetBoundsDirectCrossFade(const gfx::Rect& bounds);

  // Sets the bounds in two distinct ways. The exact behavior is dictated by
  // the value of BoundsInScreenBehavior set on the parent:
  //
  // USE_LOCAL_COORDINATES: the bounds are applied as is to the window. In other
  //   words this behaves the same as if SetBounds(bounds_in_screen) was used.
  //   This is the default.
  // USE_SCREEN_COORDINATES: the bounds are actual screen bounds and converted
  //   from the display. In this case the window may move to a different
  //   display if allowed (see SetLockedToRoot()).
  void SetBoundsInScreen(const gfx::Rect& bounds_in_screen,
                         const display::Display& dst_display);
  gfx::Rect GetBoundsInScreen() const;
  const gfx::Rect& GetBounds() const;
  gfx::Rect GetTargetBounds();
  void ClearRestoreBounds();
  void SetRestoreBoundsInScreen(const gfx::Rect& bounds);
  gfx::Rect GetRestoreBoundsInScreen() const;

  bool Contains(const WmWindow* other) const;

  void SetShowState(ui::WindowShowState show_state);
  ui::WindowShowState GetShowState() const;

  void SetPreFullscreenShowState(ui::WindowShowState show_state);

  // Sets the restore bounds and show state overrides. These values take
  // precedence over the restore bounds and restore show state (if set).
  // If |bounds_override| is empty the values are cleared.
  void SetRestoreOverrides(const gfx::Rect& bounds_override,
                           ui::WindowShowState window_state_override);

  // If |value| is true the window can not be moved to another root, regardless
  // of the bounds set on it.
  void SetLockedToRoot(bool value);
  bool IsLockedToRoot() const;

  void SetCapture();
  bool HasCapture();
  void ReleaseCapture();

  bool HasRestoreBounds() const;
  bool CanMaximize() const;
  bool CanMinimize() const;
  bool CanResize() const;
  bool CanActivate() const;

  void StackChildAtTop(WmWindow* child);
  void StackChildAtBottom(WmWindow* child);
  void StackChildAbove(WmWindow* child, WmWindow* target);
  void StackChildBelow(WmWindow* child, WmWindow* target);

  void SetAlwaysOnTop(bool value);
  bool IsAlwaysOnTop() const;

  void Hide();
  void Show();

  // Requests the window to close and destroy itself. This is intended to
  // forward to an associated widget.
  void CloseWidget();

  void SetFocused();
  bool IsFocused() const;

  bool IsActive() const;
  void Activate();
  void Deactivate();

  void SetFullscreen(bool fullscreen);

  void Maximize();
  void Minimize();
  void Unminimize();

  std::vector<WmWindow*> GetChildren();

  // Shows/hides the resize shadow. |component| is the component to show the
  // shadow for (one of the constants in ui/base/hit_test.h).
  void ShowResizeShadow(int component);
  void HideResizeShadow();

  // Installs a resize handler on the window that makes it easier to resize
  // the window. See ResizeHandleWindowTargeter for the specifics.
  void InstallResizeHandleWindowTargeter(
      ImmersiveFullscreenController* immersive_fullscreen_controller);

  // See description in SetBoundsInScreen().
  void SetBoundsInScreenBehaviorForChildren(BoundsInScreenBehavior behavior);

  // See description of SnapToPixelBoundaryIfNecessary().
  void SetSnapsChildrenToPhysicalPixelBoundary();

  // If an ancestor has been set to snap children to pixel boundaries, then
  // snaps the layer associated with this window to the layer associated with
  // the ancestor.
  void SnapToPixelBoundaryIfNecessary();

  // Makes the hit region for children slightly larger for easier resizing.
  void SetChildrenUseExtendedHitRegion();

  void AddTransientWindowObserver(WmTransientWindowObserver* observer);
  void RemoveTransientWindowObserver(WmTransientWindowObserver* observer);

  // Adds or removes a handler to receive events targeted at this window, before
  // this window handles the events itself; the handler does not recieve events
  // from embedded windows. This only supports windows with internal widgets;
  // see ash::GetInternalWidgetForWindow(). Ownership of the handler is not
  // transferred.
  //
  // Also note that the target of these events is always an aura::Window.
  void AddLimitedPreTargetHandler(ui::EventHandler* handler);
  void RemoveLimitedPreTargetHandler(ui::EventHandler* handler);

 private:
  friend class WmWindowTestApi;

  explicit WmWindow(aura::Window* window);

  // aura::WindowObserver:
  void OnWindowPropertyChanged(aura::Window* window,
                               const void* key,
                               intptr_t old) override;

  // ::wm::TransientWindowObserver overrides:
  void OnTransientChildAdded(aura::Window* window,
                             aura::Window* transient) override;
  void OnTransientChildRemoved(aura::Window* window,
                               aura::Window* transient) override;

  aura::Window* window_;

  bool added_transient_observer_ = false;
  base::ObserverList<WmTransientWindowObserver> transient_observers_;

  // If true child windows should get a slightly larger hit region to make
  // resizing easier.
  bool children_use_extended_hit_region_ = false;

  // Default value for |use_empty_minimum_size_for_testing_|.
  static bool default_use_empty_minimum_size_for_testing_;

  // If true the minimum size is 0x0, default is minimum size comes from widget.
  bool use_empty_minimum_size_for_testing_;

  DISALLOW_COPY_AND_ASSIGN(WmWindow);
};

}  // namespace ash

#endif  // ASH_WM_WINDOW_H_
