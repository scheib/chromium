// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/panels/panel_layout_manager.h"

#include <algorithm>
#include <map>
#include <utility>

#include "ash/keyboard/keyboard_observer_register.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/root_window_controller.h"
#include "ash/shelf/wm_shelf.h"
#include "ash/shell.h"
#include "ash/shell_port.h"
#include "ash/wm/overview/window_selector_controller.h"
#include "ash/wm/window_animation_types.h"
#include "ash/wm/window_parenting_utils.h"
#include "ash/wm/window_properties.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm_window.h"
#include "base/auto_reset.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/window_delegate.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/background.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/public/activation_client.h"

using aura::Window;

namespace ash {
namespace {

const int kPanelIdealSpacing = 4;

const float kMaxHeightFactor = .80f;
const float kMaxWidthFactor = .50f;

// Duration for panel animations.
const int kPanelSlideDurationMilliseconds = 50;
const int kCalloutFadeDurationMilliseconds = 50;

// Offset used when sliding panel in/out of the shelf. Used for minimizing,
// restoring and the initial showing of a panel.
const int kPanelSlideInOffset = 20;

// Callout arrow dimensions.
const int kArrowWidth = 18;
const int kArrowHeight = 9;

class CalloutWidgetBackground : public views::Background {
 public:
  CalloutWidgetBackground() : alignment_(SHELF_ALIGNMENT_BOTTOM) {}

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    SkPath path;
    switch (alignment_) {
      case SHELF_ALIGNMENT_BOTTOM:
      case SHELF_ALIGNMENT_BOTTOM_LOCKED:
        path.moveTo(SkIntToScalar(0), SkIntToScalar(0));
        path.lineTo(SkIntToScalar(kArrowWidth / 2),
                    SkIntToScalar(kArrowHeight));
        path.lineTo(SkIntToScalar(kArrowWidth), SkIntToScalar(0));
        break;
      case SHELF_ALIGNMENT_LEFT:
        path.moveTo(SkIntToScalar(kArrowHeight), SkIntToScalar(kArrowWidth));
        path.lineTo(SkIntToScalar(0), SkIntToScalar(kArrowWidth / 2));
        path.lineTo(SkIntToScalar(kArrowHeight), SkIntToScalar(0));
        break;
      case SHELF_ALIGNMENT_RIGHT:
        path.moveTo(SkIntToScalar(0), SkIntToScalar(0));
        path.lineTo(SkIntToScalar(kArrowHeight),
                    SkIntToScalar(kArrowWidth / 2));
        path.lineTo(SkIntToScalar(0), SkIntToScalar(kArrowWidth));
        break;
    }
    // Hard code the arrow color for now.
    cc::PaintFlags flags;
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(SkColorSetARGB(0xff, 0xe5, 0xe5, 0xe5));
    canvas->DrawPath(path, flags);
  }

  ShelfAlignment alignment() { return alignment_; }

  void set_alignment(ShelfAlignment alignment) { alignment_ = alignment; }

 private:
  ShelfAlignment alignment_;

  DISALLOW_COPY_AND_ASSIGN(CalloutWidgetBackground);
};

struct VisiblePanelPositionInfo {
  VisiblePanelPositionInfo()
      : min_major(0),
        max_major(0),
        major_pos(0),
        major_length(0),
        window(nullptr),
        slide_in(false) {}

  int min_major;
  int max_major;
  int major_pos;
  int major_length;
  WmWindow* window;
  bool slide_in;
};

bool CompareWindowMajor(const VisiblePanelPositionInfo& win1,
                        const VisiblePanelPositionInfo& win2) {
  return win1.major_pos < win2.major_pos;
}

void FanOutPanels(std::vector<VisiblePanelPositionInfo>::iterator first,
                  std::vector<VisiblePanelPositionInfo>::iterator last) {
  int num_panels = last - first;
  if (num_panels == 1) {
    (*first).major_pos = std::max(
        (*first).min_major, std::min((*first).max_major, (*first).major_pos));
  }
  if (num_panels <= 1)
    return;

  if (num_panels == 2) {
    // If there are two adjacent overlapping windows, separate them by the
    // minimum major_length necessary.
    std::vector<VisiblePanelPositionInfo>::iterator second = first + 1;
    int separation = (*first).major_length / 2 + (*second).major_length / 2 +
                     kPanelIdealSpacing;
    int overlap = (*first).major_pos + separation - (*second).major_pos;
    (*first).major_pos =
        std::max((*first).min_major, (*first).major_pos - overlap / 2);
    (*second).major_pos =
        std::min((*second).max_major, (*first).major_pos + separation);
    // Recalculate the first panel position in case the second one was
    // constrained on the right.
    (*first).major_pos =
        std::max((*first).min_major, (*second).major_pos - separation);
    return;
  }

  // If there are more than two overlapping windows, fan them out from minimum
  // position to maximum position equally spaced.
  int delta = ((*(last - 1)).max_major - (*first).min_major) / (num_panels - 1);
  int major_pos = (*first).min_major;
  for (std::vector<VisiblePanelPositionInfo>::iterator iter = first;
       iter != last; ++iter) {
    (*iter).major_pos =
        std::max((*iter).min_major, std::min((*iter).max_major, major_pos));
    major_pos += delta;
  }
}

bool BoundsAdjacent(const gfx::Rect& bounds1, const gfx::Rect& bounds2) {
  return bounds1.x() == bounds2.right() || bounds1.y() == bounds2.bottom() ||
         bounds1.right() == bounds2.x() || bounds1.bottom() == bounds2.y();
}

gfx::Vector2d GetSlideInAnimationOffset(ShelfAlignment alignment) {
  gfx::Vector2d offset;
  if (alignment == SHELF_ALIGNMENT_LEFT)
    offset.set_x(-kPanelSlideInOffset);
  else if (alignment == SHELF_ALIGNMENT_RIGHT)
    offset.set_x(kPanelSlideInOffset);
  else
    offset.set_y(kPanelSlideInOffset);
  return offset;
}

}  // namespace

class PanelCalloutWidget : public views::Widget {
 public:
  explicit PanelCalloutWidget(WmWindow* container) : background_(nullptr) {
    InitWidget(container);
  }

  // Updates the bounds based on the shelf alignment.
  void UpdateBounds(WmShelf* shelf) {
    WmWindow* window = WmWindow::Get(this->GetNativeWindow());
    gfx::Rect callout_bounds = window->GetBounds();
    if (shelf->IsHorizontalAlignment()) {
      callout_bounds.set_width(kArrowWidth);
      callout_bounds.set_height(kArrowHeight);
    } else {
      callout_bounds.set_width(kArrowHeight);
      callout_bounds.set_height(kArrowWidth);
    }
    WmWindow* parent = window->GetParent();
    // It's important this go through WmWindow and not Widget. Going through
    // Widget means it may move do a different screen, we don't want that.
    window->SetBounds(callout_bounds);
    // Setting the bounds should not trigger changing the parent.
    DCHECK_EQ(parent, window->GetParent());
    if (background_->alignment() != shelf->alignment()) {
      background_->set_alignment(shelf->alignment());
      SchedulePaintInRect(gfx::Rect(callout_bounds.size()));
    }
  }

 private:
  void InitWidget(WmWindow* parent) {
    views::Widget::InitParams params;
    params.type = views::Widget::InitParams::TYPE_POPUP;
    params.opacity = views::Widget::InitParams::TRANSLUCENT_WINDOW;
    params.keep_on_top = true;
    params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
    params.bounds = parent->ConvertRectToScreen(gfx::Rect());
    params.bounds.set_width(kArrowWidth);
    params.bounds.set_height(kArrowHeight);
    params.accept_events = false;
    parent->GetRootWindowController()->ConfigureWidgetInitParamsForContainer(
        this, parent->aura_window()->id(), &params);
    set_focus_on_creation(false);
    Init(params);
    WmWindow* widget_window = WmWindow::Get(this->GetNativeWindow());
    DCHECK_EQ(widget_window->GetRootWindow(), parent->GetRootWindow());
    views::View* content_view = new views::View;
    background_ = new CalloutWidgetBackground;
    content_view->set_background(background_);
    SetContentsView(content_view);
    widget_window->GetLayer()->SetOpacity(0);
  }

  // Weak pointer owned by this widget's content view.
  CalloutWidgetBackground* background_;

  DISALLOW_COPY_AND_ASSIGN(PanelCalloutWidget);
};

views::Widget* PanelLayoutManager::PanelInfo::CalloutWidget() {
  return callout_widget;
}

////////////////////////////////////////////////////////////////////////////////
// PanelLayoutManager public implementation:
PanelLayoutManager::PanelLayoutManager(WmWindow* panel_container)
    : panel_container_(panel_container),
      root_window_controller_(panel_container->GetRootWindowController()),
      in_add_window_(false),
      in_layout_(false),
      show_callout_widgets_(true),
      dragged_panel_(nullptr),
      shelf_(nullptr),
      last_active_panel_(nullptr),
      keyboard_observer_(this),
      weak_factory_(this) {
  DCHECK(panel_container);
  Shell::Get()->activation_client()->AddObserver(this);
  ShellPort::Get()->AddDisplayObserver(this);
  Shell::Get()->AddShellObserver(this);
}

PanelLayoutManager::~PanelLayoutManager() {
  Shutdown();
}

// static
PanelLayoutManager* PanelLayoutManager::Get(WmWindow* window) {
  if (!window)
    return nullptr;

  return static_cast<PanelLayoutManager*>(
      window->GetRootWindow()
          ->aura_window()
          ->GetChildById(kShellWindowId_PanelContainer)
          ->layout_manager());
}

void PanelLayoutManager::Shutdown() {
  if (shelf_) {
    shelf_->RemoveObserver(this);
    shelf_ = nullptr;
  }
  for (PanelList::iterator iter = panel_windows_.begin();
       iter != panel_windows_.end(); ++iter) {
    delete iter->callout_widget;
  }
  panel_windows_.clear();
  Shell::Get()->activation_client()->RemoveObserver(this);
  ShellPort::Get()->RemoveDisplayObserver(this);
  Shell::Get()->RemoveShellObserver(this);
}

void PanelLayoutManager::StartDragging(WmWindow* panel) {
  DCHECK(!dragged_panel_);
  dragged_panel_ = panel;
  Relayout();
}

void PanelLayoutManager::FinishDragging() {
  dragged_panel_ = nullptr;
  Relayout();
}

void PanelLayoutManager::SetShelf(WmShelf* shelf) {
  DCHECK(!shelf_);
  shelf_ = shelf;
  shelf_->AddObserver(this);
  WillChangeVisibilityState(shelf_->GetVisibilityState());
}

void PanelLayoutManager::ToggleMinimize(WmWindow* panel) {
  DCHECK(panel->GetParent() == panel_container_);
  wm::WindowState* window_state = panel->GetWindowState();
  if (window_state->IsMinimized())
    window_state->Restore();
  else
    window_state->Minimize();
}

void PanelLayoutManager::SetShowCalloutWidgets(bool show) {
  if (show_callout_widgets_ == show)
    return;
  show_callout_widgets_ = show;
  UpdateCallouts();
}

views::Widget* PanelLayoutManager::GetCalloutWidgetForPanel(WmWindow* panel) {
  DCHECK(panel->GetParent() == panel_container_);
  PanelList::iterator found =
      std::find(panel_windows_.begin(), panel_windows_.end(), panel);
  DCHECK(found != panel_windows_.end());
  return found->callout_widget;
}

////////////////////////////////////////////////////////////////////////////////
// PanelLayoutManager, aura::LayoutManager implementation:
void PanelLayoutManager::OnWindowResized() {
  Relayout();
}

void PanelLayoutManager::OnWindowAddedToLayout(Window* child) {
  if (child->type() == ui::wm::WINDOW_TYPE_POPUP)
    return;
  if (in_add_window_)
    return;
  base::AutoReset<bool> auto_reset_in_add_window(&in_add_window_, true);
  if (!child->GetProperty(kPanelAttachedKey)) {
    // This should only happen when a window is added to panel container as a
    // result of bounds change from within the application during a drag.
    // If so we have already stopped the drag and should reparent the panel
    // back to appropriate container and ignore it.
    // TODO(varkha): Updating bounds during a drag can cause problems and a more
    // general solution is needed. See http://crbug.com/251813 .
    Window* old_parent = child->parent();
    aura::client::ParentWindowWithContext(
        child, child, child->GetRootWindow()->GetBoundsInScreen());
    wm::ReparentTransientChildrenOfChild(child, old_parent, child->parent());
    DCHECK(child->parent()->id() != kShellWindowId_PanelContainer);
    return;
  }
  PanelInfo panel_info;
  panel_info.window = WmWindow::Get(child);
  panel_info.callout_widget = new PanelCalloutWidget(panel_container_);
  panel_info.slide_in = WmWindow::Get(child) != dragged_panel_;
  panel_windows_.push_back(panel_info);
  child->AddObserver(this);
  wm::GetWindowState(child)->AddObserver(this);
  Relayout();
}

void PanelLayoutManager::OnWillRemoveWindowFromLayout(Window* child) {}

void PanelLayoutManager::OnWindowRemovedFromLayout(Window* child) {
  if (child->type() == ui::wm::WINDOW_TYPE_POPUP)
    return;

  PanelList::iterator found = std::find(
      panel_windows_.begin(), panel_windows_.end(), WmWindow::Get(child));
  if (found != panel_windows_.end()) {
    delete found->callout_widget;
    panel_windows_.erase(found);
  }
  if (restore_windows_on_shelf_visible_)
    restore_windows_on_shelf_visible_->Remove(child);
  child->RemoveObserver(this);
  wm::GetWindowState(child)->RemoveObserver(this);

  if (dragged_panel_ == WmWindow::Get(child))
    dragged_panel_ = nullptr;

  if (last_active_panel_ == WmWindow::Get(child))
    last_active_panel_ = nullptr;

  Relayout();
}

void PanelLayoutManager::OnChildWindowVisibilityChanged(Window* child,
                                                        bool visible) {
  if (visible)
    wm::GetWindowState(child)->Restore();
  Relayout();
}

void PanelLayoutManager::SetChildBounds(Window* child,
                                        const gfx::Rect& requested_bounds) {
  gfx::Rect bounds(requested_bounds);
  const gfx::Rect& max_bounds = panel_container_->GetRootWindow()->GetBounds();
  const int max_width = max_bounds.width() * kMaxWidthFactor;
  const int max_height = max_bounds.height() * kMaxHeightFactor;
  if (bounds.width() > max_width)
    bounds.set_width(max_width);
  if (bounds.height() > max_height)
    bounds.set_height(max_height);

  // Reposition dragged panel in the panel order.
  if (dragged_panel_ == WmWindow::Get(child)) {
    PanelList::iterator dragged_panel_iter =
        std::find(panel_windows_.begin(), panel_windows_.end(), dragged_panel_);
    DCHECK(dragged_panel_iter != panel_windows_.end());
    PanelList::iterator new_position;
    for (new_position = panel_windows_.begin();
         new_position != panel_windows_.end(); ++new_position) {
      const gfx::Rect& bounds = (*new_position).window->GetBounds();
      if (bounds.x() + bounds.width() / 2 <= requested_bounds.x())
        break;
    }
    if (new_position != dragged_panel_iter) {
      PanelInfo dragged_panel_info = *dragged_panel_iter;
      panel_windows_.erase(dragged_panel_iter);
      panel_windows_.insert(new_position, dragged_panel_info);
    }
  }
  // Respect the minimum size of the window.
  if (child->delegate()) {
    const gfx::Size min_size = child->delegate()->GetMinimumSize();
    bounds.set_width(std::max(min_size.width(), bounds.width()));
    bounds.set_height(std::max(min_size.height(), bounds.height()));
  }

  SetChildBoundsDirect(child, bounds);
  wm::SnapWindowToPixelBoundary(child);
  Relayout();
}

////////////////////////////////////////////////////////////////////////////////
// PanelLayoutManager, ShellObserver implementation:

void PanelLayoutManager::OnOverviewModeEnded() {
  Relayout();
}

void PanelLayoutManager::OnShelfAlignmentChanged(WmWindow* root_window) {
  if (root_window_controller_->GetWindow() == root_window)
    Relayout();
}

void PanelLayoutManager::OnVirtualKeyboardStateChanged(
    bool activated,
    aura::Window* root_window) {
  UpdateKeyboardObserverFromStateChanged(
      activated, root_window, panel_container_->GetRootWindow()->aura_window(),
      &keyboard_observer_);
}

/////////////////////////////////////////////////////////////////////////////
// PanelLayoutManager, WindowObserver implementation:

void PanelLayoutManager::OnWindowPropertyChanged(aura::Window* window,
                                                 const void* key,
                                                 intptr_t old) {
  // Trigger a relayout to position the panels whenever the panel icon is set
  // or changes.
  if (key == kShelfIDKey)
    Relayout();
}

/////////////////////////////////////////////////////////////////////////////
// PanelLayoutManager, WindowStateObserver implementation:

void PanelLayoutManager::OnPostWindowStateTypeChange(
    wm::WindowState* window_state,
    wm::WindowStateType old_type) {
  // If the shelf is currently hidden then windows will not actually be shown
  // but the set to restore when the shelf becomes visible is updated.
  if (restore_windows_on_shelf_visible_) {
    if (window_state->IsMinimized()) {
      MinimizePanel(window_state->window());
      restore_windows_on_shelf_visible_->Remove(
          window_state->window()->aura_window());
    } else {
      restore_windows_on_shelf_visible_->Add(
          window_state->window()->aura_window());
    }
    return;
  }

  if (window_state->IsMinimized())
    MinimizePanel(window_state->window());
  else
    RestorePanel(window_state->window());
}

////////////////////////////////////////////////////////////////////////////////
// PanelLayoutManager, aura::client::ActivationChangeObserver implementation:

void PanelLayoutManager::OnWindowActivated(ActivationReason reason,
                                           aura::Window* gained_active,
                                           aura::Window* lost_active) {
  WmWindow* wm_gained_active = WmWindow::Get(gained_active);
  // Ignore if the panel that is not managed by this was activated.
  if (wm_gained_active &&
      wm_gained_active->GetType() == ui::wm::WINDOW_TYPE_PANEL &&
      wm_gained_active->GetParent() == panel_container_) {
    UpdateStacking(wm_gained_active);
    UpdateCallouts();
  }
}

////////////////////////////////////////////////////////////////////////////////
// PanelLayoutManager, WmDisplayObserver::Observer implementation:

void PanelLayoutManager::OnDisplayConfigurationChanged() {
  Relayout();
}

////////////////////////////////////////////////////////////////////////////////
// PanelLayoutManager, ShelfLayoutManagerObserver implementation:

void PanelLayoutManager::WillChangeVisibilityState(
    ShelfVisibilityState new_state) {
  // On entering / leaving full screen mode the shelf visibility state is
  // changed to / from SHELF_HIDDEN. In this state, panel windows should hide
  // to allow the full-screen application to use the full screen.
  bool shelf_hidden = new_state == ash::SHELF_HIDDEN;
  if (!shelf_hidden) {
    if (restore_windows_on_shelf_visible_) {
      std::unique_ptr<aura::WindowTracker> restore_windows(
          std::move(restore_windows_on_shelf_visible_));
      for (aura::Window* window : restore_windows->windows())
        RestorePanel(WmWindow::Get(window));
    }
    return;
  }

  if (restore_windows_on_shelf_visible_)
    return;
  std::unique_ptr<aura::WindowTracker> minimized_windows(
      new aura::WindowTracker);
  for (PanelList::iterator iter = panel_windows_.begin();
       iter != panel_windows_.end();) {
    WmWindow* window = iter->window;
    // Minimizing a panel window may remove it from the panel_windows_ list.
    // Advance the iterator before minimizing it: http://crbug.com/393047.
    ++iter;
    if (window != dragged_panel_ && window->IsVisible()) {
      minimized_windows->Add(window->aura_window());
      window->GetWindowState()->Minimize();
    }
  }
  restore_windows_on_shelf_visible_ = std::move(minimized_windows);
}

void PanelLayoutManager::OnShelfIconPositionsChanged() {
  // TODO: As this is called for every animation step now. Relayout needs to be
  // updated to use current icon position instead of use the ideal bounds so
  // that the panels slide with their icons instead of jumping.
  Relayout();
}

////////////////////////////////////////////////////////////////////////////////
// PanelLayoutManager private implementation:

void PanelLayoutManager::MinimizePanel(WmWindow* panel) {
  // Clusterfuzz can trigger panel accelerators before the shelf is created.
  // TODO(jamescook): Revert this after http://crbug.com/648964 is fixed.
  if (!shelf_)
    return;

  panel->SetVisibilityAnimationType(
      wm::WINDOW_VISIBILITY_ANIMATION_TYPE_MINIMIZE);
  ui::Layer* layer = panel->GetLayer();
  ui::ScopedLayerAnimationSettings panel_slide_settings(layer->GetAnimator());
  panel_slide_settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  panel_slide_settings.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(kPanelSlideDurationMilliseconds));
  gfx::Rect bounds(panel->GetBounds());
  bounds.Offset(GetSlideInAnimationOffset(shelf_->GetAlignment()));
  panel->SetBoundsDirect(bounds);
  panel->Hide();
  layer->SetOpacity(0);
  if (panel->IsActive())
    panel->Deactivate();
  Relayout();
}

void PanelLayoutManager::RestorePanel(WmWindow* panel) {
  PanelList::iterator found =
      std::find(panel_windows_.begin(), panel_windows_.end(), panel);
  DCHECK(found != panel_windows_.end());
  found->slide_in = true;
  Relayout();
}

void PanelLayoutManager::Relayout() {
  if (!shelf_ || !shelf_->GetWindow())
    return;

  // Suppress layouts during overview mode because changing window bounds
  // interfered with overview mode animations. However, layouts need to be done
  // when the WindowSelectorController is restoring minimized windows so that
  // they actually become visible.
  WindowSelectorController* window_selector_controller =
      Shell::Get()->window_selector_controller();
  if (in_layout_ ||
      (window_selector_controller->IsSelecting() &&
       !window_selector_controller->IsRestoringMinimizedWindows())) {
    return;
  }

  base::AutoReset<bool> auto_reset_in_layout(&in_layout_, true);

  const bool horizontal = shelf_->IsHorizontalAlignment();
  gfx::Rect shelf_bounds = panel_container_->ConvertRectFromScreen(
      shelf_->GetWindow()->GetBoundsInScreen());
  int panel_start_bounds = kPanelIdealSpacing;
  int panel_end_bounds =
      horizontal ? panel_container_->GetBounds().width() - kPanelIdealSpacing
                 : panel_container_->GetBounds().height() - kPanelIdealSpacing;
  WmWindow* active_panel = nullptr;
  std::vector<VisiblePanelPositionInfo> visible_panels;
  for (PanelList::iterator iter = panel_windows_.begin();
       iter != panel_windows_.end(); ++iter) {
    WmWindow* panel = iter->window;
    iter->callout_widget->UpdateBounds(shelf_);

    // Consider the dragged panel as part of the layout as long as it is
    // touching the shelf.
    if ((!panel->IsVisible() && !iter->slide_in) ||
        (panel == dragged_panel_ &&
         !BoundsAdjacent(panel->GetBounds(), shelf_bounds))) {
      continue;
    }

    // If the shelf is currently hidden (full-screen mode), minimize panel until
    // full-screen mode is exited. When a panel is dragged from another display
    // the shelf state does not update before the panel is added so we exclude
    // the dragged panel.
    if (panel != dragged_panel_ && restore_windows_on_shelf_visible_) {
      panel->GetWindowState()->Minimize();
      restore_windows_on_shelf_visible_->Add(panel->aura_window());
      continue;
    }

    gfx::Rect icon_bounds = shelf_->GetScreenBoundsOfItemIconForWindow(panel);

    // If both the icon width and height are 0 then there is no icon in the
    // shelf. If the shelf is hidden, one of the height or width will be
    // 0 but the position in the shelf and major dimension is still reported
    // correctly and the panel can be aligned above where the hidden icon is.
    if (icon_bounds.width() == 0 && icon_bounds.height() == 0)
      continue;

    if (panel->IsFocused() ||
        panel->Contains(WmWindow::Get(wm::GetFocusedWindow()))) {
      DCHECK(!active_panel);
      active_panel = panel;
    }
    icon_bounds = panel_container_->ConvertRectFromScreen(icon_bounds);
    gfx::Point icon_origin = icon_bounds.origin();
    VisiblePanelPositionInfo position_info;
    int icon_start = horizontal ? icon_origin.x() : icon_origin.y();
    int icon_end =
        icon_start + (horizontal ? icon_bounds.width() : icon_bounds.height());
    position_info.major_length =
        horizontal ? panel->GetBounds().width() : panel->GetBounds().height();
    position_info.min_major =
        std::max(panel_start_bounds + position_info.major_length / 2,
                 icon_end - position_info.major_length / 2);
    position_info.max_major =
        std::min(icon_start + position_info.major_length / 2,
                 panel_end_bounds - position_info.major_length / 2);
    position_info.major_pos = (icon_start + icon_end) / 2;
    position_info.window = panel;
    position_info.slide_in = iter->slide_in;
    iter->slide_in = false;
    visible_panels.push_back(position_info);
  }

  // Sort panels by their X positions and fan out groups of overlapping panels.
  // The fan out method may result in new overlapping panels however given that
  // the panels start at least a full panel width apart this overlap will
  // never completely obscure a panel.
  // TODO(flackr): Rearrange panels if new overlaps are introduced.
  std::sort(visible_panels.begin(), visible_panels.end(), CompareWindowMajor);
  size_t first_overlapping_panel = 0;
  for (size_t i = 1; i < visible_panels.size(); ++i) {
    if (visible_panels[i - 1].major_pos +
            visible_panels[i - 1].major_length / 2 <
        visible_panels[i].major_pos - visible_panels[i].major_length / 2) {
      FanOutPanels(visible_panels.begin() + first_overlapping_panel,
                   visible_panels.begin() + i);
      first_overlapping_panel = i;
    }
  }
  FanOutPanels(visible_panels.begin() + first_overlapping_panel,
               visible_panels.end());

  for (size_t i = 0; i < visible_panels.size(); ++i) {
    if (visible_panels[i].window == dragged_panel_)
      continue;
    bool slide_in = visible_panels[i].slide_in;
    gfx::Rect bounds = visible_panels[i].window->GetTargetBounds();
    if (shelf_->alignment() == SHELF_ALIGNMENT_LEFT)
      bounds.set_x(shelf_bounds.right());
    else if (shelf_->alignment() == SHELF_ALIGNMENT_RIGHT)
      bounds.set_x(shelf_bounds.x() - bounds.width());
    else
      bounds.set_y(shelf_bounds.y() - bounds.height());
    bool on_shelf = visible_panels[i].window->GetTargetBounds() == bounds;

    if (horizontal) {
      bounds.set_x(visible_panels[i].major_pos -
                   visible_panels[i].major_length / 2);
    } else {
      bounds.set_y(visible_panels[i].major_pos -
                   visible_panels[i].major_length / 2);
    }

    ui::Layer* layer = visible_panels[i].window->GetLayer();
    if (slide_in) {
      // New windows shift up from the shelf into position and fade in.
      layer->SetOpacity(0);
      gfx::Rect initial_bounds(bounds);
      initial_bounds.Offset(GetSlideInAnimationOffset(shelf_->alignment()));
      visible_panels[i].window->SetBoundsDirect(initial_bounds);
      // Set on shelf so that the panel animates into its target position.
      on_shelf = true;
    }

    if (on_shelf) {
      ui::ScopedLayerAnimationSettings panel_slide_settings(
          layer->GetAnimator());
      panel_slide_settings.SetPreemptionStrategy(
          ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
      panel_slide_settings.SetTransitionDuration(
          base::TimeDelta::FromMilliseconds(kPanelSlideDurationMilliseconds));
      visible_panels[i].window->SetBoundsDirect(bounds);
      if (slide_in) {
        layer->SetOpacity(1);
        visible_panels[i].window->Show();
      }
    } else {
      // If the shelf moved don't animate, move immediately to the new
      // target location.
      visible_panels[i].window->SetBoundsDirect(bounds);
    }
  }

  UpdateStacking(active_panel);
  UpdateCallouts();
}

void PanelLayoutManager::UpdateStacking(WmWindow* active_panel) {
  // Clusterfuzz can trigger panel accelerators before the shelf is created.
  // TODO(jamescook): Revert this after http://crbug.com/648964 is fixed.
  if (!shelf_)
    return;

  if (!active_panel) {
    if (!last_active_panel_)
      return;
    active_panel = last_active_panel_;
  }

  // We want to to stack the panels like a deck of cards:
  // ,--,--,--,-------.--.--.
  // |  |  |  |       |  |  |
  // |  |  |  |       |  |  |
  //
  // We use the middle of each panel to figure out how to stack the panels. This
  // allows us to update the stacking when a panel is being dragged around by
  // the titlebar--even though it doesn't update the shelf icon positions, we
  // still want the visual effect.
  std::map<int, WmWindow*> window_ordering;
  const bool horizontal = shelf_->IsHorizontalAlignment();
  for (PanelList::const_iterator it = panel_windows_.begin();
       it != panel_windows_.end(); ++it) {
    gfx::Rect bounds = it->window->GetBounds();
    window_ordering.insert(
        std::make_pair(horizontal ? bounds.x() + bounds.width() / 2
                                  : bounds.y() + bounds.height() / 2,
                       it->window));
  }

  WmWindow* previous_panel = nullptr;
  for (std::map<int, WmWindow*>::const_iterator it = window_ordering.begin();
       it != window_ordering.end() && it->second != active_panel; ++it) {
    if (previous_panel)
      panel_container_->StackChildAbove(it->second, previous_panel);
    previous_panel = it->second;
  }

  previous_panel = nullptr;
  for (std::map<int, WmWindow*>::const_reverse_iterator it =
           window_ordering.rbegin();
       it != window_ordering.rend() && it->second != active_panel; ++it) {
    if (previous_panel)
      panel_container_->StackChildAbove(it->second, previous_panel);
    previous_panel = it->second;
  }

  panel_container_->StackChildAtTop(active_panel);
  if (dragged_panel_ && dragged_panel_->GetParent() == panel_container_)
    panel_container_->StackChildAtTop(dragged_panel_);
  last_active_panel_ = active_panel;
}

void PanelLayoutManager::UpdateCallouts() {
  // Clusterfuzz can trigger panel accelerators before the shelf is created.
  // TODO(jamescook): Revert this after http://crbug.com/648964 is fixed.
  if (!shelf_)
    return;

  const bool horizontal = shelf_->IsHorizontalAlignment();
  for (PanelList::iterator iter = panel_windows_.begin();
       iter != panel_windows_.end(); ++iter) {
    WmWindow* panel = iter->window;
    views::Widget* callout_widget = iter->callout_widget;
    WmWindow* callout_widget_window =
        WmWindow::Get(callout_widget->GetNativeWindow());

    gfx::Rect current_bounds = panel->GetBoundsInScreen();
    gfx::Rect bounds =
        panel->GetParent()->ConvertRectToScreen(panel->GetTargetBounds());
    gfx::Rect icon_bounds = shelf_->GetScreenBoundsOfItemIconForWindow(panel);
    if (icon_bounds.IsEmpty() || !panel->GetLayer()->GetTargetVisibility() ||
        panel == dragged_panel_ || !show_callout_widgets_) {
      callout_widget->Hide();
      callout_widget_window->GetLayer()->SetOpacity(0);
      continue;
    }

    gfx::Rect callout_bounds = callout_widget->GetWindowBoundsInScreen();
    gfx::Vector2d slide_vector = bounds.origin() - current_bounds.origin();
    int slide_distance = horizontal ? slide_vector.x() : slide_vector.y();
    int distance_until_over_panel = 0;
    if (horizontal) {
      callout_bounds.set_x(icon_bounds.x() +
                           (icon_bounds.width() - callout_bounds.width()) / 2);
      distance_until_over_panel =
          std::max(current_bounds.x() - callout_bounds.x(),
                   callout_bounds.right() - current_bounds.right());
    } else {
      callout_bounds.set_y(icon_bounds.y() +
                           (icon_bounds.height() - callout_bounds.height()) /
                               2);
      distance_until_over_panel =
          std::max(current_bounds.y() - callout_bounds.y(),
                   callout_bounds.bottom() - current_bounds.bottom());
    }
    if (shelf_->GetAlignment() == SHELF_ALIGNMENT_LEFT)
      callout_bounds.set_x(bounds.x() - callout_bounds.width());
    else if (shelf_->GetAlignment() == SHELF_ALIGNMENT_RIGHT)
      callout_bounds.set_x(bounds.right());
    else
      callout_bounds.set_y(bounds.bottom());
    callout_bounds = callout_widget_window->GetParent()->ConvertRectFromScreen(
        callout_bounds);

    callout_widget_window->SetBoundsDirect(callout_bounds);
    DCHECK_EQ(panel_container_, callout_widget_window->GetParent());
    DCHECK_EQ(panel_container_, panel->GetParent());
    panel_container_->StackChildAbove(callout_widget_window, panel);

    ui::Layer* layer = callout_widget_window->GetLayer();
    // If the panel is not over the callout position or has just become visible
    // then fade in the callout.
    if ((distance_until_over_panel > 0 || layer->GetTargetOpacity() < 1)) {
      if (distance_until_over_panel > 0 &&
          slide_distance >= distance_until_over_panel) {
        // If the panel is not yet over the callout, then delay fading in
        // the callout until after the panel should be over it.
        int delay = kPanelSlideDurationMilliseconds *
                    distance_until_over_panel / slide_distance;
        layer->SetOpacity(0);
        layer->GetAnimator()->StopAnimating();
        layer->GetAnimator()->SchedulePauseForProperties(
            base::TimeDelta::FromMilliseconds(delay),
            ui::LayerAnimationElement::OPACITY);
      }
      ui::ScopedLayerAnimationSettings callout_settings(layer->GetAnimator());
      callout_settings.SetPreemptionStrategy(
          ui::LayerAnimator::REPLACE_QUEUED_ANIMATIONS);
      callout_settings.SetTransitionDuration(
          base::TimeDelta::FromMilliseconds(kCalloutFadeDurationMilliseconds));
      layer->SetOpacity(1);
    }

    // Show after changing the opacity animation. This way we don't have a
    // state where the widget is visible but the opacity is 0.
    callout_widget->Show();
  }
}

////////////////////////////////////////////////////////////////////////////////
// keyboard::KeyboardControllerObserver implementation:

void PanelLayoutManager::OnKeyboardBoundsChanging(
    const gfx::Rect& keyboard_bounds) {
  gfx::Rect parent_bounds = panel_container_->GetBounds();
  int available_space = parent_bounds.height() - keyboard_bounds.height();
  for (PanelList::iterator iter = panel_windows_.begin();
       iter != panel_windows_.end(); ++iter) {
    WmWindow* panel = iter->window;
    wm::WindowState* panel_state = panel->GetWindowState();
    if (keyboard_bounds.height() > 0) {
      // Save existing bounds, so that we can restore them when the keyboard
      // hides.
      panel_state->SaveCurrentBoundsForRestore();

      gfx::Rect panel_bounds =
          panel->GetParent()->ConvertRectToScreen(panel->GetTargetBounds());
      int delta = panel_bounds.height() - available_space;
      // Ensure panels are not pushed above the parent boundaries, shrink any
      // panels that violate this constraint.
      if (delta > 0) {
        panel->SetBoundsDirect(
            gfx::Rect(panel_bounds.x(), panel_bounds.y() + delta,
                      panel_bounds.width(), panel_bounds.height() - delta));
      }
    } else if (panel_state->HasRestoreBounds()) {
      // Keyboard hidden, restore original bounds if they exist.
      panel->SetBoundsDirect(panel_state->GetRestoreBoundsInScreen());
    }
  }
  // This bounds change will have caused a change to the Shelf which does not
  // propogate automatically to this class, so manually recalculate bounds.
  OnWindowResized();
}

void PanelLayoutManager::OnKeyboardClosed() {
  keyboard_observer_.RemoveAll();
}

}  // namespace ash
