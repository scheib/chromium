// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/overview/window_grid.h"

#include <algorithm>
#include <functional>
#include <set>
#include <utility>
#include <vector>

#include "ash/ash_switches.h"
#include "ash/public/cpp/shelf_types.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/root_window_controller.h"
#include "ash/screen_util.h"
#include "ash/shelf/wm_shelf.h"
#include "ash/wm/overview/cleanup_animation_observer.h"
#include "ash/wm/overview/scoped_overview_animation_settings.h"
#include "ash/wm/overview/scoped_overview_animation_settings_factory.h"
#include "ash/wm/overview/window_selector.h"
#include "ash/wm/overview/window_selector_delegate.h"
#include "ash/wm/overview/window_selector_item.h"
#include "ash/wm/window_state.h"
#include "ash/wm_window.h"
#include "base/command_line.h"
#include "base/i18n/string_search.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/pathops/SkPathOps.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/safe_integer_conversions.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/painter.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/shadow.h"
#include "ui/wm/core/shadow_types.h"
#include "ui/wm/core/window_animations.h"

namespace ash {
namespace {

using Windows = std::vector<WmWindow*>;

// A comparator for locating a given target window.
struct WindowSelectorItemComparator {
  explicit WindowSelectorItemComparator(const WmWindow* target_window)
      : target(target_window) {}

  bool operator()(std::unique_ptr<WindowSelectorItem>& window) const {
    return window->GetWindow() == target;
  }

  const WmWindow* target;
};

// Time it takes for the selector widget to move to the next target. The same
// time is used for fading out shield widget when the overview mode is opened
// or closed.
const int kOverviewSelectorTransitionMilliseconds = 250;

// The color and opacity of the screen shield in overview.
const SkColor kShieldColor = SkColorSetARGB(255, 0, 0, 0);
const float kShieldOpacity = 0.7f;

// The color and opacity of the overview selector.
const SkColor kWindowSelectionColor = SkColorSetARGB(51, 255, 255, 255);
const SkColor kWindowSelectionBorderColor = SkColorSetARGB(76, 255, 255, 255);

// Border thickness of overview selector.
const int kWindowSelectionBorderThickness = 1;

// Corner radius of the overview selector border.
const int kWindowSelectionRadius = 4;

// In the conceptual overview table, the window margin is the space reserved
// around the window within the cell. This margin does not overlap so the
// closest distance between adjacent windows will be twice this amount.
const int kWindowMargin = 5;

// Windows are not allowed to get taller than this.
const int kMaxHeight = 512;

// Margins reserved in the overview mode.
const float kOverviewInsetRatio = 0.05f;

// Additional vertical inset reserved for windows in overview mode.
const float kOverviewVerticalInset = 0.1f;

// A View having rounded corners and a specified background color which is
// only painted within the bounds defined by the rounded corners.
// TODO(varkha): This duplicates code from RoundedImageView. Refactor these
//               classes and move into ui/views.
class RoundedRectView : public views::View {
 public:
  RoundedRectView(int corner_radius, SkColor background)
      : corner_radius_(corner_radius), background_(background) {}

  ~RoundedRectView() override {}

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);

    SkScalar radius = SkIntToScalar(corner_radius_);
    const SkScalar kRadius[8] = {radius, radius, radius, radius,
                                 radius, radius, radius, radius};
    SkPath path;
    gfx::Rect bounds(size());
    bounds.set_height(bounds.height() + radius);
    path.addRoundRect(gfx::RectToSkRect(bounds), kRadius);

    canvas->ClipPath(path, true);
    canvas->DrawColor(background_);
  }

 private:
  int corner_radius_;
  SkColor background_;

  DISALLOW_COPY_AND_ASSIGN(RoundedRectView);
};

// BackgroundWith1PxBorder renders a solid background color, with a one pixel
// border with rounded corners. This accounts for the scaling of the canvas, so
// that the border is 1 pixel thick regardless of display scaling.
class BackgroundWith1PxBorder : public views::Background {
 public:
  BackgroundWith1PxBorder(SkColor background,
                          SkColor border_color,
                          int border_thickness,
                          int corner_radius);

  void Paint(gfx::Canvas* canvas, views::View* view) const override;

 private:
  // Color for the one pixel border.
  SkColor border_color_;

  // Thickness of border inset.
  int border_thickness_;

  // Corner radius of the inside edge of the roundrect border stroke.
  int corner_radius_;

  DISALLOW_COPY_AND_ASSIGN(BackgroundWith1PxBorder);
};

BackgroundWith1PxBorder::BackgroundWith1PxBorder(SkColor background,
                                                 SkColor border_color,
                                                 int border_thickness,
                                                 int corner_radius)
    : border_color_(border_color),
      border_thickness_(border_thickness),
      corner_radius_(corner_radius) {
  SetNativeControlColor(background);
}

void BackgroundWith1PxBorder::Paint(gfx::Canvas* canvas,
                                    views::View* view) const {
  gfx::RectF border_rect_f(view->GetContentsBounds());

  gfx::ScopedCanvas scoped_canvas(canvas);
  const float scale = canvas->UndoDeviceScaleFactor();
  border_rect_f.Scale(scale);
  const float inset = border_thickness_ * scale - 0.5f;
  border_rect_f.Inset(inset, inset);

  SkPath path;
  const SkScalar scaled_corner_radius =
      SkFloatToScalar(corner_radius_ * scale + 0.5f);
  path.addRoundRect(gfx::RectFToSkRect(border_rect_f), scaled_corner_radius,
                    scaled_corner_radius);

  cc::PaintFlags flags;
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1);
  flags.setAntiAlias(true);

  SkPath stroke_path;
  flags.getFillPath(path, &stroke_path);

  SkPath fill_path;
  Op(path, stroke_path, kDifference_SkPathOp, &fill_path);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(get_color());
  canvas->sk_canvas()->drawPath(fill_path, flags);

  if (border_thickness_ > 0) {
    flags.setColor(border_color_);
    canvas->sk_canvas()->drawPath(stroke_path, flags);
  }
}

// Returns the vector for the fade in animation.
gfx::Vector2d GetSlideVectorForFadeIn(WindowSelector::Direction direction,
                                      const gfx::Rect& bounds) {
  gfx::Vector2d vector;
  switch (direction) {
    case WindowSelector::UP:
    case WindowSelector::LEFT:
      vector.set_x(-bounds.width());
      break;
    case WindowSelector::DOWN:
    case WindowSelector::RIGHT:
      vector.set_x(bounds.width());
      break;
  }
  return vector;
}

// Creates and returns a background translucent widget parented in
// |root_window|'s default container and having |background_color|.
// When |border_thickness| is non-zero, a border is created having
// |border_color|, otherwise |border_color| parameter is ignored.
// The new background widget starts with |initial_opacity| and then fades in.
views::Widget* CreateBackgroundWidget(WmWindow* root_window,
                                      ui::LayerType layer_type,
                                      SkColor background_color,
                                      int border_thickness,
                                      int border_radius,
                                      SkColor border_color,
                                      float initial_opacity) {
  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params;
  params.type = views::Widget::InitParams::TYPE_POPUP;
  params.keep_on_top = false;
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.opacity = views::Widget::InitParams::TRANSLUCENT_WINDOW;
  params.layer_type = layer_type;
  params.accept_events = false;
  widget->set_focus_on_creation(false);
  // Parenting in kShellWindowId_WallpaperContainer allows proper layering of
  // the shield and selection widgets. Since that container is created with
  // USE_LOCAL_COORDINATES BoundsInScreenBehavior local bounds in |root_window_|
  // need to be provided.
  root_window->GetRootWindowController()->ConfigureWidgetInitParamsForContainer(
      widget, kShellWindowId_WallpaperContainer, &params);
  widget->Init(params);
  WmWindow* widget_window = WmWindow::Get(widget->GetNativeWindow());
  // Disable the "bounce in" animation when showing the window.
  widget_window->SetVisibilityAnimationTransition(::wm::ANIMATE_NONE);
  // The background widget should not activate the shelf when passing under it.
  widget_window->GetWindowState()->set_ignored_by_shelf(true);
  if (params.layer_type == ui::LAYER_SOLID_COLOR) {
    widget_window->GetLayer()->SetColor(background_color);
  } else {
    views::View* content_view =
        new RoundedRectView(border_radius, SK_ColorTRANSPARENT);
    content_view->set_background(new BackgroundWith1PxBorder(
        background_color, border_color, border_thickness, border_radius));
    widget->SetContentsView(content_view);
  }
  widget_window->GetParent()->StackChildAtTop(widget_window);
  widget->Show();
  widget_window->SetOpacity(initial_opacity);
  return widget;
}

bool IsMinimizedStateType(wm::WindowStateType type) {
  return type == wm::WINDOW_STATE_TYPE_MINIMIZED;
}

}  // namespace

WindowGrid::WindowGrid(WmWindow* root_window,
                       const std::vector<WmWindow*>& windows,
                       WindowSelector* window_selector)
    : root_window_(root_window),
      window_selector_(window_selector),
      window_observer_(this),
      window_state_observer_(this),
      selected_index_(0),
      num_columns_(0),
      prepared_for_overview_(false) {
  std::vector<WmWindow*> windows_in_root;
  for (auto* window : windows) {
    if (window->GetRootWindow() == root_window)
      windows_in_root.push_back(window);
  }

  for (auto* window : windows_in_root) {
    window_observer_.Add(window->aura_window());
    window_state_observer_.Add(window->GetWindowState());
    window_list_.push_back(
        base::MakeUnique<WindowSelectorItem>(window, window_selector_));
  }
}

WindowGrid::~WindowGrid() {}

void WindowGrid::Shutdown() {
  for (const auto& window : window_list_)
    window->Shutdown();

  if (shield_widget_) {
    // Fade out the shield widget. This animation continues past the lifetime
    // of |this|.
    WmWindow* widget_window = WmWindow::Get(shield_widget_->GetNativeWindow());
    ui::ScopedLayerAnimationSettings animation_settings(
        widget_window->GetLayer()->GetAnimator());
    animation_settings.SetTransitionDuration(base::TimeDelta::FromMilliseconds(
        kOverviewSelectorTransitionMilliseconds));
    animation_settings.SetTweenType(gfx::Tween::EASE_OUT);
    animation_settings.SetPreemptionStrategy(
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    // CleanupAnimationObserver will delete itself (and the shield widget) when
    // the opacity animation is complete.
    // Ownership over the observer is passed to the window_selector_->delegate()
    // which has longer lifetime so that animations can continue even after the
    // overview mode is shut down.
    views::Widget* shield_widget = shield_widget_.get();
    std::unique_ptr<CleanupAnimationObserver> observer(
        new CleanupAnimationObserver(std::move(shield_widget_)));
    animation_settings.AddObserver(observer.get());
    window_selector_->delegate()->AddDelayedAnimationObserver(
        std::move(observer));
    shield_widget->SetOpacity(0.f);
  }
}

void WindowGrid::PrepareForOverview() {
  InitShieldWidget();
  for (const auto& window : window_list_)
    window->PrepareForOverview();
  prepared_for_overview_ = true;
}

void WindowGrid::PositionWindows(bool animate) {
  if (window_selector_->is_shut_down() || window_list_.empty())
    return;
  DCHECK(shield_widget_.get());
  // Keep the background shield widget covering the whole screen.
  WmWindow* widget_window = WmWindow::Get(shield_widget_->GetNativeWindow());
  const gfx::Rect bounds = widget_window->GetParent()->GetBounds();
  widget_window->SetBounds(bounds);
  gfx::Rect total_bounds = root_window_->ConvertRectToScreen(
      ScreenUtil::GetDisplayWorkAreaBoundsInParent(
          root_window_->GetChildByShellWindowId(kShellWindowId_DefaultContainer)
              ->aura_window()));
  // Windows occupy vertically centered area with additional vertical insets.
  int horizontal_inset =
      gfx::ToFlooredInt(std::min(kOverviewInsetRatio * total_bounds.width(),
                                 kOverviewInsetRatio * total_bounds.height()));
  int vertical_inset =
      horizontal_inset +
      kOverviewVerticalInset * (total_bounds.height() - 2 * horizontal_inset);
  total_bounds.Inset(std::max(0, horizontal_inset - kWindowMargin),
                     std::max(0, vertical_inset - kWindowMargin));
  std::vector<gfx::Rect> rects;

  // Keep track of the lowest coordinate.
  int max_bottom = total_bounds.y();

  // Right bound of the narrowest row.
  int min_right = total_bounds.right();
  // Right bound of the widest row.
  int max_right = total_bounds.x();

  // Keep track of the difference between the narrowest and the widest row.
  // Initially this is set to the worst it can ever be assuming the windows fit.
  int width_diff = total_bounds.width();

  // Initially allow the windows to occupy all available width. Shrink this
  // available space horizontally to find the breakdown into rows that achieves
  // the minimal |width_diff|.
  int right_bound = total_bounds.right();

  // Determine the optimal height bisecting between |low_height| and
  // |high_height|. Once this optimal height is known, |height_fixed| is set to
  // true and the rows are balanced by repeatedly squeezing the widest row to
  // cause windows to overflow to the subsequent rows.
  int low_height = 2 * kWindowMargin;
  int high_height =
      std::max(low_height, static_cast<int>(total_bounds.height() + 1));
  int height = 0.5 * (low_height + high_height);
  bool height_fixed = false;

  // Repeatedly try to fit the windows |rects| within |right_bound|.
  // If a maximum |height| is found such that all window |rects| fit, this
  // fitting continues while shrinking the |right_bound| in order to balance the
  // rows. If the windows fit the |right_bound| would have been decremented at
  // least once so it needs to be incremented once before getting out of this
  // loop and one additional pass made to actually fit the |rects|.
  // If the |rects| cannot fit (e.g. there are too many windows) the bisection
  // will still finish and we might increment the |right_bound| once pixel extra
  // which is acceptable since there is an unused margin on the right.
  bool make_last_adjustment = false;
  while (true) {
    gfx::Rect overview_bounds(total_bounds);
    overview_bounds.set_width(right_bound - total_bounds.x());
    bool windows_fit = FitWindowRectsInBounds(
        overview_bounds, std::min(kMaxHeight + 2 * kWindowMargin, height),
        &rects, &max_bottom, &min_right, &max_right);

    if (height_fixed) {
      if (!windows_fit) {
        // Revert the previous change to |right_bound| and do one last pass.
        right_bound++;
        make_last_adjustment = true;
        break;
      }
      // Break if all the windows are zero-width at the current scale.
      if (max_right <= total_bounds.x())
        break;
    } else {
      // Find the optimal row height bisecting between |low_height| and
      // |high_height|.
      if (windows_fit)
        low_height = height;
      else
        high_height = height;
      height = 0.5 * (low_height + high_height);
      // When height can no longer be improved, start balancing the rows.
      if (height == low_height)
        height_fixed = true;
    }

    if (windows_fit && height_fixed) {
      if (max_right - min_right <= width_diff) {
        // Row alignment is getting better. Try to shrink the |right_bound| in
        // order to squeeze the widest row.
        right_bound = max_right - 1;
        width_diff = max_right - min_right;
      } else {
        // Row alignment is getting worse.
        // Revert the previous change to |right_bound| and do one last pass.
        right_bound++;
        make_last_adjustment = true;
        break;
      }
    }
  }
  // Once the windows in |window_list_| no longer fit, the change to
  // |right_bound| was reverted. Perform one last pass to position the |rects|.
  if (make_last_adjustment) {
    gfx::Rect overview_bounds(total_bounds);
    overview_bounds.set_width(right_bound - total_bounds.x());
    FitWindowRectsInBounds(overview_bounds,
                           std::min(kMaxHeight + 2 * kWindowMargin, height),
                           &rects, &max_bottom, &min_right, &max_right);
  }
  // Position the windows centering the left-aligned rows vertically.
  gfx::Vector2d offset(0, (total_bounds.bottom() - max_bottom) / 2);
  for (size_t i = 0; i < window_list_.size(); ++i) {
    window_list_[i]->SetBounds(
        rects[i] + offset,
        animate
            ? OverviewAnimationType::OVERVIEW_ANIMATION_LAY_OUT_SELECTOR_ITEMS
            : OverviewAnimationType::OVERVIEW_ANIMATION_NONE);
  }

  // If the selection widget is active, reposition it without any animation.
  if (selection_widget_)
    MoveSelectionWidgetToTarget(animate);
}

bool WindowGrid::Move(WindowSelector::Direction direction, bool animate) {
  bool recreate_selection_widget = false;
  bool out_of_bounds = false;
  bool changed_selection_index = false;
  gfx::Rect old_bounds;
  if (SelectedWindow()) {
    old_bounds = SelectedWindow()->target_bounds();
    // Make the old selected window header non-transparent first.
    SelectedWindow()->SetSelected(false);
  }

  // [up] key is equivalent to [left] key and [down] key is equivalent to
  // [right] key.
  if (!selection_widget_) {
    switch (direction) {
      case WindowSelector::UP:
      case WindowSelector::LEFT:
        selected_index_ = window_list_.size() - 1;
        break;
      case WindowSelector::DOWN:
      case WindowSelector::RIGHT:
        selected_index_ = 0;
        break;
    }
    changed_selection_index = true;
  }
  while (!changed_selection_index ||
         (!out_of_bounds && window_list_[selected_index_]->dimmed())) {
    switch (direction) {
      case WindowSelector::UP:
      case WindowSelector::LEFT:
        if (selected_index_ == 0)
          out_of_bounds = true;
        selected_index_--;
        break;
      case WindowSelector::DOWN:
      case WindowSelector::RIGHT:
        if (selected_index_ >= window_list_.size() - 1)
          out_of_bounds = true;
        selected_index_++;
        break;
    }
    if (!out_of_bounds && SelectedWindow()) {
      if (SelectedWindow()->target_bounds().y() != old_bounds.y())
        recreate_selection_widget = true;
    }
    changed_selection_index = true;
  }
  MoveSelectionWidget(direction, recreate_selection_widget, out_of_bounds,
                      animate);

  // Make the new selected window header fully transparent.
  if (SelectedWindow())
    SelectedWindow()->SetSelected(true);
  return out_of_bounds;
}

WindowSelectorItem* WindowGrid::SelectedWindow() const {
  if (!selection_widget_)
    return nullptr;
  CHECK(selected_index_ < window_list_.size());
  return window_list_[selected_index_].get();
}

bool WindowGrid::Contains(const WmWindow* window) const {
  for (const auto& window_item : window_list_) {
    if (window_item->Contains(window))
      return true;
  }
  return false;
}

void WindowGrid::FilterItems(const base::string16& pattern) {
  base::i18n::FixedPatternStringSearchIgnoringCaseAndAccents finder(pattern);
  for (const auto& window : window_list_) {
    if (finder.Search(window->GetWindow()->aura_window()->GetTitle(), nullptr,
                      nullptr)) {
      window->SetDimmed(false);
    } else {
      window->SetDimmed(true);
      if (selection_widget_ && SelectedWindow() == window.get()) {
        SelectedWindow()->SetSelected(false);
        selection_widget_.reset();
        selector_shadow_.reset();
      }
    }
  }
}

void WindowGrid::WindowClosing(WindowSelectorItem* window) {
  if (!selection_widget_ || SelectedWindow() != window)
    return;
  WmWindow* selection_widget_window =
      WmWindow::Get(selection_widget_->GetNativeWindow());
  std::unique_ptr<ScopedOverviewAnimationSettings> animation_settings_label =
      ScopedOverviewAnimationSettingsFactory::Get()
          ->CreateOverviewAnimationSettings(
              OverviewAnimationType::OVERVIEW_ANIMATION_CLOSING_SELECTOR_ITEM,
              selection_widget_window);
  selection_widget_->SetOpacity(0.f);
}

void WindowGrid::OnWindowDestroying(aura::Window* window) {
  window_observer_.Remove(window);
  window_state_observer_.Remove(wm::GetWindowState(window));
  auto iter = std::find_if(window_list_.begin(), window_list_.end(),
                           WindowSelectorItemComparator(WmWindow::Get(window)));

  DCHECK(iter != window_list_.end());

  size_t removed_index = iter - window_list_.begin();
  window_list_.erase(iter);

  if (empty()) {
    // If the grid is now empty, notify the window selector so that it erases us
    // from its grid list.
    window_selector_->OnGridEmpty(this);
    return;
  }

  // If selecting, update the selection index.
  if (selection_widget_) {
    bool send_focus_alert = selected_index_ == removed_index;
    if (selected_index_ >= removed_index && selected_index_ != 0)
      selected_index_--;
    SelectedWindow()->SetSelected(true);
    if (send_focus_alert)
      SelectedWindow()->SendAccessibleSelectionEvent();
  }

  PositionWindows(true);
}

void WindowGrid::OnWindowBoundsChanged(aura::Window* window,
                                       const gfx::Rect& old_bounds,
                                       const gfx::Rect& new_bounds) {
  // During preparation, window bounds can change. Ignore bounds
  // change notifications in this case; we'll reposition soon.
  if (!prepared_for_overview_)
    return;

  auto iter = std::find_if(window_list_.begin(), window_list_.end(),
                           WindowSelectorItemComparator(WmWindow::Get(window)));
  DCHECK(iter != window_list_.end());

  // Immediately finish any active bounds animation.
  window->layer()->GetAnimator()->StopAnimatingProperty(
      ui::LayerAnimationElement::BOUNDS);
  PositionWindows(false);
}

void WindowGrid::OnPostWindowStateTypeChange(wm::WindowState* window_state,
                                             wm::WindowStateType old_type) {
  // During preparation, window state can change, e.g. updating shelf
  // visibility may show the temporarily hidden (minimized) panels.
  if (!prepared_for_overview_)
    return;

  wm::WindowStateType new_type = window_state->GetStateType();
  if (IsMinimizedStateType(old_type) == IsMinimizedStateType(new_type))
    return;

  auto iter =
      std::find_if(window_list_.begin(), window_list_.end(),
                   [window_state](std::unique_ptr<WindowSelectorItem>& item) {
                     return item->Contains(window_state->window());
                   });
  if (iter != window_list_.end()) {
    (*iter)->OnMinimizedStateChanged();
    PositionWindows(false);
  }
}

void WindowGrid::InitShieldWidget() {
  // TODO(varkha): The code assumes that SHELF_BACKGROUND_MAXIMIZED is
  // synonymous with a black shelf background. Update this code if that
  // assumption is no longer valid.
  const float initial_opacity =
      (WmShelf::ForWindow(root_window_->aura_window())->GetBackgroundType() ==
       SHELF_BACKGROUND_MAXIMIZED)
          ? 1.f
          : 0.f;
  shield_widget_.reset(
      CreateBackgroundWidget(root_window_, ui::LAYER_SOLID_COLOR, kShieldColor,
                             0, 0, SK_ColorTRANSPARENT, initial_opacity));
  WmWindow* widget_window = WmWindow::Get(shield_widget_->GetNativeWindow());
  const gfx::Rect bounds = widget_window->GetParent()->GetBounds();
  widget_window->SetBounds(bounds);
  widget_window->aura_window()->SetName("OverviewModeShield");

  ui::ScopedLayerAnimationSettings animation_settings(
      widget_window->GetLayer()->GetAnimator());
  animation_settings.SetTransitionDuration(base::TimeDelta::FromMilliseconds(
      kOverviewSelectorTransitionMilliseconds));
  animation_settings.SetTweenType(gfx::Tween::EASE_OUT);
  animation_settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  shield_widget_->SetOpacity(kShieldOpacity);
}

void WindowGrid::InitSelectionWidget(WindowSelector::Direction direction) {
  selection_widget_.reset(CreateBackgroundWidget(
      root_window_, ui::LAYER_TEXTURED, kWindowSelectionColor,
      kWindowSelectionBorderThickness, kWindowSelectionRadius,
      kWindowSelectionBorderColor, 0.f));
  WmWindow* widget_window = WmWindow::Get(selection_widget_->GetNativeWindow());
  const gfx::Rect target_bounds =
      root_window_->ConvertRectFromScreen(SelectedWindow()->target_bounds());
  gfx::Vector2d fade_out_direction =
      GetSlideVectorForFadeIn(direction, target_bounds);
  widget_window->SetBounds(target_bounds - fade_out_direction);
  widget_window->aura_window()->SetName("OverviewModeSelector");

  selector_shadow_.reset(new ::wm::Shadow());
  selector_shadow_->Init(::wm::ShadowElevation::LARGE);
  selector_shadow_->layer()->SetVisible(true);
  selection_widget_->GetLayer()->SetMasksToBounds(false);
  selection_widget_->GetLayer()->Add(selector_shadow_->layer());
  selector_shadow_->SetContentBounds(gfx::Rect(target_bounds.size()));
}

void WindowGrid::MoveSelectionWidget(WindowSelector::Direction direction,
                                     bool recreate_selection_widget,
                                     bool out_of_bounds,
                                     bool animate) {
  // If the selection widget is already active, fade it out in the selection
  // direction.
  if (selection_widget_ && (recreate_selection_widget || out_of_bounds)) {
    // Animate the old selection widget and then destroy it.
    views::Widget* old_selection = selection_widget_.get();
    WmWindow* old_selection_window =
        WmWindow::Get(old_selection->GetNativeWindow());
    gfx::Vector2d fade_out_direction =
        GetSlideVectorForFadeIn(direction, old_selection_window->GetBounds());

    ui::ScopedLayerAnimationSettings animation_settings(
        old_selection_window->GetLayer()->GetAnimator());
    animation_settings.SetTransitionDuration(base::TimeDelta::FromMilliseconds(
        kOverviewSelectorTransitionMilliseconds));
    animation_settings.SetPreemptionStrategy(
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    animation_settings.SetTweenType(gfx::Tween::FAST_OUT_LINEAR_IN);
    // CleanupAnimationObserver will delete itself (and the widget) when the
    // motion animation is complete.
    // Ownership over the observer is passed to the window_selector_->delegate()
    // which has longer lifetime so that animations can continue even after the
    // overview mode is shut down.
    std::unique_ptr<CleanupAnimationObserver> observer(
        new CleanupAnimationObserver(std::move(selection_widget_)));
    animation_settings.AddObserver(observer.get());
    window_selector_->delegate()->AddDelayedAnimationObserver(
        std::move(observer));
    old_selection->SetOpacity(0.f);
    old_selection_window->SetBounds(old_selection_window->GetBounds() +
                                    fade_out_direction);
    old_selection->Hide();
  }
  if (out_of_bounds)
    return;

  if (!selection_widget_)
    InitSelectionWidget(direction);
  // Send an a11y alert so that if ChromeVox is enabled, the item label is
  // read.
  SelectedWindow()->SendAccessibleSelectionEvent();
  // The selection widget is moved to the newly selected item in the same
  // grid.
  MoveSelectionWidgetToTarget(animate);
}

void WindowGrid::MoveSelectionWidgetToTarget(bool animate) {
  gfx::Rect bounds =
      root_window_->ConvertRectFromScreen(SelectedWindow()->target_bounds());
  if (animate) {
    WmWindow* selection_widget_window =
        WmWindow::Get(selection_widget_->GetNativeWindow());
    ui::ScopedLayerAnimationSettings animation_settings(
        selection_widget_window->GetLayer()->GetAnimator());
    animation_settings.SetTransitionDuration(base::TimeDelta::FromMilliseconds(
        kOverviewSelectorTransitionMilliseconds));
    animation_settings.SetTweenType(gfx::Tween::EASE_IN_OUT);
    animation_settings.SetPreemptionStrategy(
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    selection_widget_->SetBounds(bounds);
    selection_widget_->SetOpacity(1.f);

    if (selector_shadow_) {
      ui::ScopedLayerAnimationSettings animation_settings_shadow(
          selector_shadow_->shadow_layer()->GetAnimator());
      animation_settings_shadow.SetTransitionDuration(
          base::TimeDelta::FromMilliseconds(
              kOverviewSelectorTransitionMilliseconds));
      animation_settings_shadow.SetTweenType(gfx::Tween::EASE_IN_OUT);
      animation_settings_shadow.SetPreemptionStrategy(
          ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
      bounds.Inset(1, 1);
      selector_shadow_->SetContentBounds(
          gfx::Rect(gfx::Point(1, 1), bounds.size()));
    }
    return;
  }
  selection_widget_->SetBounds(bounds);
  selection_widget_->SetOpacity(1.f);
  if (selector_shadow_) {
    bounds.Inset(1, 1);
    selector_shadow_->SetContentBounds(
        gfx::Rect(gfx::Point(1, 1), bounds.size()));
  }
}

bool WindowGrid::FitWindowRectsInBounds(const gfx::Rect& bounds,
                                        int height,
                                        std::vector<gfx::Rect>* rects,
                                        int* max_bottom,
                                        int* min_right,
                                        int* max_right) {
  rects->resize(window_list_.size());
  bool windows_fit = true;

  // Start in the top-left corner of |bounds|.
  int left = bounds.x();
  int top = bounds.y();

  // Keep track of the lowest coordinate.
  *max_bottom = bounds.y();

  // Right bound of the narrowest row.
  *min_right = bounds.right();
  // Right bound of the widest row.
  *max_right = bounds.x();

  // All elements are of same height and only the height is necessary to
  // determine each item's scale.
  const gfx::Size item_size(0, height);
  size_t i = 0;
  for (const auto& window : window_list_) {
    const gfx::Rect target_bounds = window->GetTargetBoundsInScreen();
    const int width =
        std::max(1, gfx::ToFlooredInt(target_bounds.width() *
                                      window->GetItemScale(item_size)) +
                        2 * kWindowMargin);
    if (left + width > bounds.right()) {
      // Move to the next row if possible.
      if (*min_right > left)
        *min_right = left;
      if (*max_right < left)
        *max_right = left;
      top += height;

      // Check if the new row reaches the bottom or if the first item in the new
      // row does not fit within the available width.
      if (top + height > bounds.bottom() ||
          bounds.x() + width > bounds.right()) {
        windows_fit = false;
        break;
      }
      left = bounds.x();
    }

    // Position the current rect.
    (*rects)[i].SetRect(left, top, width, height);

    // Increment horizontal position using sanitized positive |width()|.
    left += (*rects)[i].width();

    if (++i == window_list_.size()) {
      // Update the narrowest and widest row width for the last row.
      if (*min_right > left)
        *min_right = left;
      if (*max_right < left)
        *max_right = left;
    }
    *max_bottom = top + height;
  }
  return windows_fit;
}

}  // namespace ash
