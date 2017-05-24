// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <vector>

#include "ash/accessibility_delegate.h"
#include "ash/accessibility_types.h"
#include "ash/drag_drop/drag_drop_controller.h"
#include "ash/public/cpp/config.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/shelf/wm_shelf.h"
#include "ash/shell.h"
#include "ash/system/tray/system_tray.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/shelf_view_test_api.h"
#include "ash/test/shell_test_api.h"
#include "ash/test/test_app_list_view_presenter_impl.h"
#include "ash/wm/maximize_mode/maximize_mode_controller.h"
#include "ash/wm/overview/window_grid.h"
#include "ash/wm/overview/window_selector.h"
#include "ash/wm/overview/window_selector_controller.h"
#include "ash/wm/overview/window_selector_item.h"
#include "ash/wm/panels/panel_layout_manager.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm/wm_event.h"
#include "ash/wm/workspace/workspace_window_resizer.h"
#include "ash/wm_window.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/user_action_tester.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/test/test_windows.h"
#include "ui/aura/window.h"
#include "ui/base/hit_test.h"
#include "ui/compositor/scoped_animation_duration_scale_mode.h"
#include "ui/display/display_layout.h"
#include "ui/display/manager/display_manager.h"
#include "ui/display/test/display_manager_test_api.h"
#include "ui/events/event_utils.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/transform.h"
#include "ui/gfx/transform_util.h"
#include "ui/wm/core/coordinate_conversion.h"
#include "ui/wm/core/window_util.h"
#include "ui/wm/public/activation_delegate.h"

namespace ash {
namespace {

// The label covers selector item windows with a padding in order to prevent
// them from receiving user input events while in overview.
static const int kWindowMargin = 5;

// The overview mode header overlaps original window header. This value is used
// to set top inset property on the windows.
static const int kHeaderHeight = 32;

const char kActiveWindowChangedFromOverview[] =
    "WindowSelector_ActiveWindowChanged";

class NonActivatableActivationDelegate
    : public aura::client::ActivationDelegate {
 public:
  bool ShouldActivate() const override { return false; }
};

void CancelDrag(DragDropController* controller, bool* canceled) {
  if (controller->IsDragDropInProgress()) {
    *canceled = true;
    controller->DragCancel();
  }
}

float GetItemScale(const gfx::Rect& source,
                   const gfx::Rect& target,
                   int top_view_inset,
                   int title_height) {
  return ScopedTransformOverviewWindow::GetItemScale(
      source.size(), target.size(), top_view_inset, title_height);
}

}  // namespace

// TODO(bruthig): Move all non-simple method definitions out of class
// declaration.
class WindowSelectorTest : public test::AshTestBase {
 public:
  WindowSelectorTest() {}
  ~WindowSelectorTest() override {}

  void SetUp() override {
    test::AshTestBase::SetUp();

    shelf_view_test_.reset(new test::ShelfViewTestAPI(
        GetPrimaryShelf()->GetShelfViewForTesting()));
    shelf_view_test_->SetAnimationDuration(1);
    ScopedTransformOverviewWindow::SetImmediateCloseForTests();
  }

  aura::Window* CreateWindow(const gfx::Rect& bounds) {
    aura::Window* window =
        CreateTestWindowInShellWithDelegate(&delegate_, -1, bounds);
    window->SetProperty(aura::client::kTopViewInset, kHeaderHeight);
    return window;
  }

  aura::Window* CreateWindowWithId(const gfx::Rect& bounds, int id) {
    aura::Window* window =
        CreateTestWindowInShellWithDelegate(&delegate_, id, bounds);
    window->SetProperty(aura::client::kTopViewInset, kHeaderHeight);
    return window;
  }
  aura::Window* CreateNonActivatableWindow(const gfx::Rect& bounds) {
    aura::Window* window = CreateWindow(bounds);
    aura::client::SetActivationDelegate(window,
                                        &non_activatable_activation_delegate_);
    EXPECT_FALSE(wm::CanActivateWindow(window));
    return window;
  }

  // Creates a Widget containing a Window with the given |bounds|. This should
  // be used when the test requires a Widget. For example any test that will
  // cause a window to be closed via
  // views::Widget::GetWidgetForNativeView(window)->Close().
  std::unique_ptr<views::Widget> CreateWindowWidget(const gfx::Rect& bounds) {
    std::unique_ptr<views::Widget> widget(new views::Widget);
    views::Widget::InitParams params;
    params.bounds = bounds;
    params.type = views::Widget::InitParams::TYPE_WINDOW;
    params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
    widget->Init(params);
    widget->Show();
    WmWindow* window = WmWindow::Get(widget->GetNativeWindow());
    window->aura_window()->SetProperty(aura::client::kTopViewInset,
                                       kHeaderHeight);
    ParentWindowInPrimaryRootWindow(widget->GetNativeWindow());
    return widget;
  }

  aura::Window* CreatePanelWindow(const gfx::Rect& bounds) {
    aura::Window* window = CreateTestWindowInShellWithDelegateAndType(
        nullptr, ui::wm::WINDOW_TYPE_PANEL, 0, bounds);
    static int id = 0;
    std::string shelf_id(ShelfID(base::IntToString(id++)).Serialize());
    window->SetProperty(kShelfIDKey, new std::string(shelf_id));
    window->SetProperty(aura::client::kTopViewInset, kHeaderHeight);
    shelf_view_test()->RunMessageLoopUntilAnimationsDone();
    return window;
  }

  bool WindowsOverlapping(aura::Window* window1, aura::Window* window2) {
    gfx::Rect window1_bounds = GetTransformedTargetBounds(window1);
    gfx::Rect window2_bounds = GetTransformedTargetBounds(window2);
    return window1_bounds.Intersects(window2_bounds);
  }

  WindowSelectorController* window_selector_controller() {
    return Shell::Get()->window_selector_controller();
  }

  WindowSelector* window_selector() {
    return window_selector_controller()->window_selector_.get();
  }

  void ToggleOverview() { window_selector_controller()->ToggleOverview(); }

  aura::Window* GetOverviewWindowForMinimizedState(int index,
                                                   aura::Window* window) {
    WindowSelectorItem* selector = GetWindowItemForWindow(index, window);
    return WmWindow::GetAuraWindow(
        selector->GetOverviewWindowForMinimizedStateForTest());
  }

  gfx::Rect GetTransformedBounds(aura::Window* window) {
    gfx::Rect bounds_in_screen = window->layer()->bounds();
    ::wm::ConvertRectToScreen(window->parent(), &bounds_in_screen);
    gfx::RectF bounds(bounds_in_screen);
    gfx::Transform transform(gfx::TransformAboutPivot(
        gfx::ToFlooredPoint(bounds.origin()), window->layer()->transform()));
    transform.TransformRect(&bounds);
    return gfx::ToEnclosingRect(bounds);
  }

  gfx::Rect GetTransformedTargetBounds(aura::Window* window) {
    gfx::Rect bounds_in_screen = window->layer()->GetTargetBounds();
    ::wm::ConvertRectToScreen(window->parent(), &bounds_in_screen);
    gfx::RectF bounds(bounds_in_screen);
    gfx::Transform transform(
        gfx::TransformAboutPivot(gfx::ToFlooredPoint(bounds.origin()),
                                 window->layer()->GetTargetTransform()));
    transform.TransformRect(&bounds);
    return gfx::ToEnclosingRect(bounds);
  }

  gfx::Rect GetTransformedBoundsInRootWindow(aura::Window* window) {
    gfx::RectF bounds = gfx::RectF(gfx::SizeF(window->bounds().size()));
    aura::Window* root = window->GetRootWindow();
    CHECK(window->layer());
    CHECK(root->layer());
    gfx::Transform transform;
    if (!window->layer()->GetTargetTransformRelativeTo(root->layer(),
                                                       &transform)) {
      return gfx::Rect();
    }
    transform.TransformRect(&bounds);
    return gfx::ToEnclosingRect(bounds);
  }

  void ClickWindow(aura::Window* window) {
    ui::test::EventGenerator event_generator(window->GetRootWindow(), window);
    event_generator.ClickLeftButton();
  }

  void SendKey(ui::KeyboardCode key) {
    ui::test::EventGenerator event_generator(Shell::GetPrimaryRootWindow());
    event_generator.PressKey(key, 0);
    event_generator.ReleaseKey(key, 0);
  }

  void SendCtrlKey(ui::KeyboardCode key) {
    ui::test::EventGenerator event_generator(Shell::GetPrimaryRootWindow());
    event_generator.PressKey(ui::VKEY_CONTROL, ui::EF_CONTROL_DOWN);
    event_generator.PressKey(key, ui::EF_CONTROL_DOWN);
    event_generator.ReleaseKey(key, ui::EF_CONTROL_DOWN);
    event_generator.ReleaseKey(ui::VKEY_CONTROL, ui::EF_NONE);
  }

  bool IsSelecting() { return window_selector_controller()->IsSelecting(); }

  const std::vector<std::unique_ptr<WindowSelectorItem>>& GetWindowItemsForRoot(
      int index) {
    return window_selector()->grid_list_[index]->window_list();
  }

  WindowSelectorItem* GetWindowItemForWindow(int grid_index,
                                             aura::Window* window) {
    const std::vector<std::unique_ptr<WindowSelectorItem>>& windows =
        GetWindowItemsForRoot(grid_index);
    auto iter =
        std::find_if(windows.cbegin(), windows.cend(),
                     [window](const std::unique_ptr<WindowSelectorItem>& item) {
                       return item->Contains(WmWindow::Get(window));
                     });
    if (iter == windows.end())
      return nullptr;
    return iter->get();
  }

  gfx::SlideAnimation* GetBackgroundViewAnimationForWindow(
      int grid_index,
      aura::Window* window) {
    return GetWindowItemForWindow(grid_index, window)
        ->GetBackgroundViewAnimation();
  }

  // Selects |window| in the active overview session by cycling through all
  // windows in overview until it is found. Returns true if |window| was found,
  // false otherwise.
  bool SelectWindow(const aura::Window* window) {
    if (GetSelectedWindow() == nullptr)
      SendKey(ui::VKEY_TAB);
    const aura::Window* start_window = GetSelectedWindow();
    if (start_window == window)
      return true;
    do {
      SendKey(ui::VKEY_TAB);
    } while (GetSelectedWindow() != window &&
             GetSelectedWindow() != start_window);
    return GetSelectedWindow() == window;
  }

  const aura::Window* GetSelectedWindow() {
    WindowSelector* ws = window_selector();
    WindowSelectorItem* item =
        ws->grid_list_[ws->selected_grid_index_]->SelectedWindow();
    if (!item)
      return nullptr;
    return WmWindow::GetAuraWindow(item->GetWindow());
  }

  bool selection_widget_active() {
    WindowSelector* ws = window_selector();
    return ws->grid_list_[ws->selected_grid_index_]->is_selecting();
  }

  bool showing_filter_widget() {
    return window_selector()
        ->text_filter_widget_->GetNativeWindow()
        ->layer()
        ->GetTargetTransform()
        .IsIdentity();
  }

  views::Widget* GetCloseButton(WindowSelectorItem* window) {
    return window->close_button_->GetWidget();
  }

  views::Label* GetLabelView(WindowSelectorItem* window) {
    return window->label_view_;
  }

  // Tests that a window is contained within a given WindowSelectorItem, and
  // that both the window and its matching close button are within the same
  // screen.
  void IsWindowAndCloseButtonInScreen(aura::Window* window,
                                      WindowSelectorItem* window_item) {
    aura::Window* root_window =
        WmWindow::GetAuraWindow(window_item->root_window());
    EXPECT_TRUE(window_item->Contains(WmWindow::Get(window)));
    EXPECT_TRUE(root_window->GetBoundsInScreen().Contains(
        GetTransformedTargetBounds(window)));
    EXPECT_TRUE(
        root_window->GetBoundsInScreen().Contains(GetTransformedTargetBounds(
            GetCloseButton(window_item)->GetNativeView())));
  }

  void FilterItems(const base::StringPiece& pattern) {
    window_selector()->ContentsChanged(nullptr, base::UTF8ToUTF16(pattern));
  }

  test::ShelfViewTestAPI* shelf_view_test() { return shelf_view_test_.get(); }

  views::Widget* text_filter_widget() {
    return window_selector()->text_filter_widget_.get();
  }

 private:
  aura::test::TestWindowDelegate delegate_;
  NonActivatableActivationDelegate non_activatable_activation_delegate_;
  std::unique_ptr<test::ShelfViewTestAPI> shelf_view_test_;

  DISALLOW_COPY_AND_ASSIGN(WindowSelectorTest);
};

// Tests that the text field in the overview menu is repositioned and resized
// after a screen rotation.
TEST_F(WindowSelectorTest, OverviewScreenRotation) {
  // TODO: fails in mash because rotation is not supported.
  // http://crbug.com/695556
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  gfx::Rect bounds(0, 0, 400, 300);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> panel1(CreatePanelWindow(bounds));

  // In overview mode the windows should no longer overlap and the text filter
  // widget should be focused.
  ToggleOverview();

  views::Widget* text_filter = text_filter_widget();
  UpdateDisplay("400x300");

  // The text filter position is calculated as:
  // x: 0.5 * (total_bounds.width() -
  //           std::min(kTextFilterWidth, total_bounds.width())).
  // y: -kTextFilterHeight (since there's no text in the filter) - 2.
  // w: std::min(kTextFilterWidth, total_bounds.width()).
  // h: kTextFilterHeight.
  gfx::Rect expected_bounds(60, -42, 280, 40);
  EXPECT_EQ(expected_bounds.ToString(),
            text_filter->GetClientAreaBoundsInScreen().ToString());

  // Rotates the display, which triggers the WindowSelector's
  // RepositionTextFilterOnDisplayMetricsChange method.
  UpdateDisplay("400x300/r");

  // Uses the same formulas as above using width = 300, height = 400.
  expected_bounds = gfx::Rect(10, -42, 280, 40);
  EXPECT_EQ(expected_bounds.ToString(),
            text_filter->GetClientAreaBoundsInScreen().ToString());
}

// Tests that an a11y alert is sent on entering overview mode.
TEST_F(WindowSelectorTest, A11yAlertOnOverviewMode) {
  gfx::Rect bounds(0, 0, 400, 400);
  AccessibilityDelegate* delegate = Shell::Get()->accessibility_delegate();
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  EXPECT_NE(delegate->GetLastAccessibilityAlert(),
            A11Y_ALERT_WINDOW_OVERVIEW_MODE_ENTERED);
  ToggleOverview();
  EXPECT_EQ(delegate->GetLastAccessibilityAlert(),
            A11Y_ALERT_WINDOW_OVERVIEW_MODE_ENTERED);
}

// Tests that there are no crashes when there is not enough screen space
// available to show all of the windows.
TEST_F(WindowSelectorTest, SmallDisplay) {
  UpdateDisplay("3x1");
  gfx::Rect bounds(0, 0, 1, 1);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window3(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window4(CreateWindow(bounds));
  window1->SetProperty(aura::client::kTopViewInset, 0);
  window2->SetProperty(aura::client::kTopViewInset, 0);
  window3->SetProperty(aura::client::kTopViewInset, 0);
  window4->SetProperty(aura::client::kTopViewInset, 0);
  ToggleOverview();
}

// Tests entering overview mode with two windows and selecting one by clicking.
TEST_F(WindowSelectorTest, Basic) {
  gfx::Rect bounds(0, 0, 400, 400);
  aura::Window* root_window = Shell::GetPrimaryRootWindow();
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  std::unique_ptr<aura::Window> panel1(CreatePanelWindow(bounds));
  std::unique_ptr<aura::Window> panel2(CreatePanelWindow(bounds));

  EXPECT_TRUE(WindowsOverlapping(window1.get(), window2.get()));
  EXPECT_TRUE(WindowsOverlapping(panel1.get(), panel2.get()));
  wm::ActivateWindow(window2.get());
  EXPECT_FALSE(wm::IsActiveWindow(window1.get()));
  EXPECT_TRUE(wm::IsActiveWindow(window2.get()));
  EXPECT_EQ(window2.get(), wm::GetFocusedWindow());
  // TODO: mash doesn't support CursorClient. http://crbug.com/637853.
  if (Shell::GetAshConfig() != Config::MASH) {
    // Hide the cursor before entering overview to test that it will be shown.
    aura::client::GetCursorClient(root_window)->HideCursor();
  }

  // In overview mode the windows should no longer overlap and the text filter
  // widget should be focused.
  ToggleOverview();
  EXPECT_EQ(text_filter_widget()->GetNativeWindow(), wm::GetFocusedWindow());
  EXPECT_FALSE(WindowsOverlapping(window1.get(), window2.get()));
  EXPECT_FALSE(WindowsOverlapping(window1.get(), panel1.get()));
  EXPECT_FALSE(WindowsOverlapping(panel1.get(), panel2.get()));

  // Clicking window 1 should activate it.
  ClickWindow(window1.get());
  EXPECT_TRUE(wm::IsActiveWindow(window1.get()));
  EXPECT_FALSE(wm::IsActiveWindow(window2.get()));
  EXPECT_EQ(window1.get(), wm::GetFocusedWindow());

  // TODO: mash doesn't support CursorClient. http://crbug.com/637853.
  if (Shell::GetAshConfig() != Config::MASH) {
    // Cursor should have been unlocked.
    EXPECT_FALSE(aura::client::GetCursorClient(root_window)->IsCursorLocked());
  }
}

// Tests activating minimized window.
TEST_F(WindowSelectorTest, ActivateMinimized) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window(CreateWindow(bounds));

  wm::WindowState* window_state = wm::GetWindowState(window.get());
  wm::WMEvent minimize_event(wm::WM_EVENT_MINIMIZE);
  window_state->OnWMEvent(&minimize_event);
  EXPECT_FALSE(window->IsVisible());
  EXPECT_EQ(0.f, window->layer()->GetTargetOpacity());
  EXPECT_EQ(wm::WINDOW_STATE_TYPE_MINIMIZED,
            wm::GetWindowState(window.get())->GetStateType());

  ToggleOverview();

  EXPECT_FALSE(window->IsVisible());
  EXPECT_EQ(0.f, window->layer()->GetTargetOpacity());
  EXPECT_EQ(wm::WINDOW_STATE_TYPE_MINIMIZED,
            wm::GetWindowState(window.get())->GetStateType());
  aura::Window* window_for_minimized_window =
      GetOverviewWindowForMinimizedState(0, window.get());
  EXPECT_TRUE(window_for_minimized_window);

  const gfx::Point point =
      GetTransformedBoundsInRootWindow(window_for_minimized_window)
          .CenterPoint();
  ui::test::EventGenerator event_generator(window->GetRootWindow(), point);
  event_generator.ClickLeftButton();

  EXPECT_FALSE(IsSelecting());

  EXPECT_TRUE(window->IsVisible());
  EXPECT_EQ(1.f, window->layer()->GetTargetOpacity());
  EXPECT_EQ(wm::WINDOW_STATE_TYPE_NORMAL,
            wm::GetWindowState(window.get())->GetStateType());
}

// Tests that entering overview mode with an App-list active properly focuses
// and activates the overview text filter window.
TEST_F(WindowSelectorTest, TextFilterActive) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  wm::ActivateWindow(window1.get());

  EXPECT_TRUE(wm::IsActiveWindow(window1.get()));
  EXPECT_EQ(window1.get(), wm::GetFocusedWindow());

  Shell::Get()->ToggleAppList();

  // Activating overview cancels the App-list which normally would activate the
  // previously active |window1|. Overview mode should properly transfer focus
  // and activation to the text filter widget.
  ToggleOverview();
  EXPECT_FALSE(wm::IsActiveWindow(window1.get()));
  EXPECT_TRUE(wm::IsActiveWindow(wm::GetFocusedWindow()));
  EXPECT_EQ(text_filter_widget()->GetNativeWindow(), wm::GetFocusedWindow());
}

// Tests that the ordering of windows is stable across different overview
// sessions even when the windows have the same bounds.
TEST_F(WindowSelectorTest, WindowsOrder) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindowWithId(bounds, 1));
  std::unique_ptr<aura::Window> window2(CreateWindowWithId(bounds, 2));
  std::unique_ptr<aura::Window> window3(CreateWindowWithId(bounds, 3));

  // The order of windows in overview mode is MRU.
  wm::GetWindowState(window1.get())->Activate();
  ToggleOverview();
  const std::vector<std::unique_ptr<WindowSelectorItem>>& overview1 =
      GetWindowItemsForRoot(0);
  EXPECT_EQ(1, overview1[0]->GetWindow()->aura_window()->id());
  EXPECT_EQ(3, overview1[1]->GetWindow()->aura_window()->id());
  EXPECT_EQ(2, overview1[2]->GetWindow()->aura_window()->id());
  ToggleOverview();

  // Activate the second window.
  wm::GetWindowState(window2.get())->Activate();
  ToggleOverview();
  const std::vector<std::unique_ptr<WindowSelectorItem>>& overview2 =
      GetWindowItemsForRoot(0);

  // The order should be MRU.
  EXPECT_EQ(2, overview2[0]->GetWindow()->aura_window()->id());
  EXPECT_EQ(1, overview2[1]->GetWindow()->aura_window()->id());
  EXPECT_EQ(3, overview2[2]->GetWindow()->aura_window()->id());
  ToggleOverview();
}

// Tests selecting a window by tapping on it.
TEST_F(WindowSelectorTest, BasicGesture) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  wm::ActivateWindow(window1.get());
  EXPECT_EQ(window1.get(), wm::GetFocusedWindow());
  ToggleOverview();
  EXPECT_EQ(text_filter_widget()->GetNativeWindow(), wm::GetFocusedWindow());
  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     window2.get());
  generator.GestureTapAt(
      GetTransformedTargetBounds(window2.get()).CenterPoint());
  EXPECT_EQ(window2.get(), wm::GetFocusedWindow());
}

// Tests that the user action WindowSelector_ActiveWindowChanged is
// recorded when the mouse/touchscreen/keyboard are used to select a window
// in overview mode which is different from the previously-active window.
TEST_F(WindowSelectorTest, ActiveWindowChangedUserActionRecorded) {
  // TODO: fails because of metrics. http://crbug.com/698129.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  base::UserActionTester user_action_tester;
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));

  // Tap on |window2| to activate it and exit overview.
  wm::ActivateWindow(window1.get());
  ToggleOverview();
  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     window2.get());
  generator.GestureTapAt(
      GetTransformedTargetBounds(window2.get()).CenterPoint());
  EXPECT_EQ(
      1, user_action_tester.GetActionCount(kActiveWindowChangedFromOverview));

  // Click on |window2| to activate it and exit overview.
  wm::ActivateWindow(window1.get());
  ToggleOverview();
  ClickWindow(window2.get());
  EXPECT_EQ(
      2, user_action_tester.GetActionCount(kActiveWindowChangedFromOverview));

  // Select |window2| using the arrow keys. Activate it (and exit overview) by
  // pressing the return key.
  wm::ActivateWindow(window1.get());
  ToggleOverview();
  ASSERT_TRUE(SelectWindow(window2.get()));
  SendKey(ui::VKEY_RETURN);
  EXPECT_EQ(
      3, user_action_tester.GetActionCount(kActiveWindowChangedFromOverview));
}

// Tests that the user action WindowSelector_ActiveWindowChanged is not
// recorded when the mouse/touchscreen/keyboard are used to select the
// already-active window from overview mode. Also verifies that entering and
// exiting overview without selecting a window does not record the action.
TEST_F(WindowSelectorTest, ActiveWindowChangedUserActionNotRecorded) {
  base::UserActionTester user_action_tester;
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));

  // Set |window1| to be initially active.
  wm::ActivateWindow(window1.get());
  ToggleOverview();

  // Tap on |window1| to exit overview.
  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     window1.get());
  generator.GestureTapAt(
      GetTransformedTargetBounds(window1.get()).CenterPoint());
  EXPECT_EQ(
      0, user_action_tester.GetActionCount(kActiveWindowChangedFromOverview));

  // |window1| remains active. Click on it to exit overview.
  ASSERT_EQ(window1.get(), wm::GetFocusedWindow());
  ToggleOverview();
  ClickWindow(window1.get());
  EXPECT_EQ(
      0, user_action_tester.GetActionCount(kActiveWindowChangedFromOverview));

  // |window1| remains active. Select using the keyboard.
  ASSERT_EQ(window1.get(), wm::GetFocusedWindow());
  ToggleOverview();
  ASSERT_TRUE(SelectWindow(window1.get()));
  SendKey(ui::VKEY_RETURN);
  EXPECT_EQ(
      0, user_action_tester.GetActionCount(kActiveWindowChangedFromOverview));

  // Entering and exiting overview without user input should not record
  // the action.
  ToggleOverview();
  ToggleOverview();
  EXPECT_EQ(
      0, user_action_tester.GetActionCount(kActiveWindowChangedFromOverview));
}

// Tests that the user action WindowSelector_ActiveWindowChanged is not
// recorded when overview mode exits as a result of closing its only window.
TEST_F(WindowSelectorTest, ActiveWindowChangedUserActionWindowClose) {
  base::UserActionTester user_action_tester;
  std::unique_ptr<views::Widget> widget =
      CreateWindowWidget(gfx::Rect(0, 0, 400, 400));

  ToggleOverview();

  aura::Window* window = widget->GetNativeWindow();
  gfx::Rect bounds = GetTransformedBoundsInRootWindow(window);
  gfx::Point point(bounds.top_right().x() - 1, bounds.top_right().y() + 5);
  ui::test::EventGenerator event_generator(window->GetRootWindow(), point);

  ASSERT_FALSE(widget->IsClosed());
  event_generator.ClickLeftButton();
  ASSERT_TRUE(widget->IsClosed());
  EXPECT_EQ(
      0, user_action_tester.GetActionCount(kActiveWindowChangedFromOverview));
}

// Tests that we do not crash and overview mode remains engaged if the desktop
// is tapped while a finger is already down over a window.
TEST_F(WindowSelectorTest, NoCrashWithDesktopTap) {
  std::unique_ptr<aura::Window> window(
      CreateWindow(gfx::Rect(200, 300, 250, 450)));

  ToggleOverview();

  gfx::Rect bounds = GetTransformedBoundsInRootWindow(window.get());
  ui::test::EventGenerator event_generator(window->GetRootWindow(),
                                           bounds.CenterPoint());

  // Press down on the window.
  const int kTouchId = 19;
  event_generator.PressTouchId(kTouchId);

  // Tap on the desktop, which should not cause a crash. Overview mode should
  // be disengaged.
  event_generator.GestureTapAt(gfx::Point(0, 0));
  EXPECT_FALSE(IsSelecting());

  event_generator.ReleaseTouchId(kTouchId);
}

// Tests that we do not crash and a window is selected when appropriate when
// we click on a window during touch.
TEST_F(WindowSelectorTest, ClickOnWindowDuringTouch) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  wm::ActivateWindow(window2.get());
  EXPECT_FALSE(wm::IsActiveWindow(window1.get()));
  EXPECT_TRUE(wm::IsActiveWindow(window2.get()));

  ToggleOverview();

  gfx::Rect window1_bounds = GetTransformedBoundsInRootWindow(window1.get());
  ui::test::EventGenerator event_generator(window1->GetRootWindow(),
                                           window1_bounds.CenterPoint());

  // Clicking on |window2| while touching on |window1| should not cause a
  // crash, overview mode should be disengaged and |window2| should be active.
  const int kTouchId = 19;
  event_generator.PressTouchId(kTouchId);
  event_generator.MoveMouseToCenterOf(window2.get());
  event_generator.ClickLeftButton();
  EXPECT_FALSE(IsSelecting());
  EXPECT_TRUE(wm::IsActiveWindow(window2.get()));
  event_generator.ReleaseTouchId(kTouchId);

  ToggleOverview();

  // Clicking on |window1| while touching on |window1| should not cause
  // a crash, overview mode should be disengaged, and |window1| should
  // be active.
  event_generator.MoveMouseToCenterOf(window1.get());
  event_generator.PressTouchId(kTouchId);
  event_generator.ClickLeftButton();
  EXPECT_FALSE(IsSelecting());
  EXPECT_TRUE(wm::IsActiveWindow(window1.get()));
  EXPECT_FALSE(wm::IsActiveWindow(window2.get()));
  event_generator.ReleaseTouchId(kTouchId);
}

// Tests that a window does not receive located events when in overview mode.
TEST_F(WindowSelectorTest, WindowDoesNotReceiveEvents) {
  gfx::Rect window_bounds(20, 10, 200, 300);
  aura::Window* root_window = Shell::GetPrimaryRootWindow();
  std::unique_ptr<aura::Window> window(CreateWindow(window_bounds));

  gfx::Point point1(window_bounds.x() + 10, window_bounds.y() + 10);

  ui::MouseEvent event1(ui::ET_MOUSE_PRESSED, point1, point1,
                        ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE);

  ui::EventTarget* root_target = root_window;
  ui::EventTargeter* targeter =
      root_window->GetHost()->dispatcher()->GetDefaultEventTargeter();

  // The event should target the window because we are still not in overview
  // mode.
  EXPECT_EQ(window.get(), targeter->FindTargetForEvent(root_target, &event1));

  ToggleOverview();

  // The bounds have changed, take that into account.
  gfx::Rect bounds = GetTransformedBoundsInRootWindow(window.get());
  gfx::Point point2(bounds.x() + 10, bounds.y() + 10);
  ui::MouseEvent event2(ui::ET_MOUSE_PRESSED, point2, point2,
                        ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE);

  // Now the transparent window should be intercepting this event.
  EXPECT_NE(window.get(), targeter->FindTargetForEvent(root_target, &event2));
}

// Tests that clicking on the close button effectively closes the window.
TEST_F(WindowSelectorTest, CloseButton) {
  std::unique_ptr<views::Widget> widget =
      CreateWindowWidget(gfx::Rect(0, 0, 400, 400));

  std::unique_ptr<views::Widget> minimized_widget =
      CreateWindowWidget(gfx::Rect(400, 0, 400, 400));
  minimized_widget->Minimize();

  ToggleOverview();

  aura::Window* window = widget->GetNativeWindow();
  gfx::Rect bounds = GetTransformedBoundsInRootWindow(window);
  gfx::Point point(bounds.top_right().x() - 1, bounds.top_right().y() + 5);
  ui::test::EventGenerator event_generator(window->GetRootWindow(), point);

  EXPECT_FALSE(widget->IsClosed());
  event_generator.ClickLeftButton();
  EXPECT_TRUE(widget->IsClosed());

  EXPECT_TRUE(IsSelecting());

  aura::Window* window_for_minimized_window =
      GetOverviewWindowForMinimizedState(0,
                                         minimized_widget->GetNativeWindow());
  ASSERT_TRUE(window_for_minimized_window);
  const gfx::Rect rect =
      GetTransformedBoundsInRootWindow(window_for_minimized_window);

  event_generator.MoveMouseTo(
      gfx::Point(rect.top_right().x() - 10, rect.top_right().y() - 10));

  EXPECT_FALSE(minimized_widget->IsClosed());
  event_generator.ClickLeftButton();
  EXPECT_TRUE(minimized_widget->IsClosed());

  // All minimized windows are closed, so it should exit overview mode.
  RunAllPendingInMessageLoop();
  EXPECT_FALSE(IsSelecting());
}

// Tests minimizing/unminimizing in overview mode.
TEST_F(WindowSelectorTest, MinimizeUnminimizei) {
  std::unique_ptr<views::Widget> widget =
      CreateWindowWidget(gfx::Rect(0, 0, 400, 400));
  aura::Window* window = widget->GetNativeWindow();

  ToggleOverview();

  EXPECT_FALSE(GetOverviewWindowForMinimizedState(0, window));
  widget->Minimize();
  EXPECT_TRUE(widget->IsMinimized());
  EXPECT_TRUE(IsSelecting());

  EXPECT_TRUE(GetOverviewWindowForMinimizedState(0, window));

  widget->Restore();
  EXPECT_FALSE(widget->IsMinimized());

  EXPECT_FALSE(GetOverviewWindowForMinimizedState(0, window));
  EXPECT_TRUE(IsSelecting());
}

// Tests that clicking on the close button on a secondary display effectively
// closes the window.
TEST_F(WindowSelectorTest, CloseButtonOnMultipleDisplay) {
  UpdateDisplay("600x400,600x400");
  aura::Window::Windows root_windows = Shell::GetAllRootWindows();

  std::unique_ptr<aura::Window> window1(
      CreateWindow(gfx::Rect(650, 300, 250, 450)));

  // We need a widget for the close button to work because windows are closed
  // via the widget. We also use the widget to determine if the window has been
  // closed or not. We explicity create the widget so that the window can be
  // parented to a non-primary root window.
  std::unique_ptr<views::Widget> widget(new views::Widget);
  views::Widget::InitParams params;
  params.bounds = gfx::Rect(650, 0, 400, 400);
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.parent = window1->parent();
  widget->Init(params);
  widget->Show();
  WmWindow* window = WmWindow::Get(widget->GetNativeWindow());
  window->aura_window()->SetProperty(aura::client::kTopViewInset,
                                     kHeaderHeight);

  ASSERT_EQ(root_windows[1], window1->GetRootWindow());

  ToggleOverview();

  aura::Window* window2 = widget->GetNativeWindow();
  gfx::Rect bounds = GetTransformedBoundsInRootWindow(window2);
  gfx::Point point(bounds.top_right().x() - 1, bounds.top_right().y() + 5);
  ui::test::EventGenerator event_generator(window2->GetRootWindow(), point);

  EXPECT_FALSE(widget->IsClosed());
  event_generator.ClickLeftButton();
  EXPECT_TRUE(widget->IsClosed());
}

// Tests entering overview mode with two windows and selecting one.
TEST_F(WindowSelectorTest, FullscreenWindow) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  std::unique_ptr<aura::Window> panel1(CreatePanelWindow(bounds));
  wm::ActivateWindow(window1.get());

  const wm::WMEvent toggle_fullscreen_event(wm::WM_EVENT_TOGGLE_FULLSCREEN);
  wm::GetWindowState(window1.get())->OnWMEvent(&toggle_fullscreen_event);
  // The panel is hidden in fullscreen mode.
  EXPECT_FALSE(panel1->IsVisible());
  EXPECT_TRUE(wm::GetWindowState(window1.get())->IsFullscreen());

  // Enter overview and select the fullscreen window.
  ToggleOverview();

  // The panel becomes temporarily visible for the overview.
  EXPECT_TRUE(panel1->IsVisible());
  ClickWindow(window1.get());

  // The window is still fullscreen as it was selected. The panel should again
  // be hidden.
  EXPECT_TRUE(wm::GetWindowState(window1.get())->IsFullscreen());
  EXPECT_FALSE(panel1->IsVisible());

  // Entering overview and selecting another window, the previous window remains
  // fullscreen.
  // TODO(flackr): Currently the panel remains hidden, but should become visible
  // again.
  ToggleOverview();
  ClickWindow(window2.get());
  EXPECT_TRUE(wm::GetWindowState(window1.get())->IsFullscreen());
}

// Tests that entering overview when a fullscreen window is active in maximized
// mode correctly applies the transformations to the window and correctly
// updates the window bounds on exiting overview mode: http://crbug.com/401664.
TEST_F(WindowSelectorTest, FullscreenWindowMaximizeMode) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  Shell::Get()->maximize_mode_controller()->EnableMaximizeModeWindowManager(
      true);
  wm::ActivateWindow(window2.get());
  wm::ActivateWindow(window1.get());
  gfx::Rect normal_window_bounds(window1->bounds());
  const wm::WMEvent toggle_fullscreen_event(wm::WM_EVENT_TOGGLE_FULLSCREEN);
  wm::GetWindowState(window1.get())->OnWMEvent(&toggle_fullscreen_event);
  gfx::Rect fullscreen_window_bounds(window1->bounds());
  EXPECT_NE(normal_window_bounds.ToString(),
            fullscreen_window_bounds.ToString());
  EXPECT_EQ(fullscreen_window_bounds.ToString(),
            window2->GetTargetBounds().ToString());
  ToggleOverview();
  // Window 2 would normally resize to normal window bounds on showing the shelf
  // for overview but this is deferred until overview is exited.
  EXPECT_EQ(fullscreen_window_bounds.ToString(),
            window2->GetTargetBounds().ToString());
  EXPECT_FALSE(WindowsOverlapping(window1.get(), window2.get()));
  ToggleOverview();

  // Since the fullscreen window is still active, window2 will still have the
  // larger bounds.
  EXPECT_EQ(fullscreen_window_bounds.ToString(),
            window2->GetTargetBounds().ToString());

  // Enter overview again and select window 2. Selecting window 2 should show
  // the shelf bringing window2 back to the normal bounds.
  ToggleOverview();
  ClickWindow(window2.get());
  EXPECT_EQ(normal_window_bounds.ToString(),
            window2->GetTargetBounds().ToString());
}

// Tests that beginning window selection hides the app list.
TEST_F(WindowSelectorTest, SelectingHidesAppList) {
  // TODO: fails in mash because of AppListPresenter. http://crbug.com/696028.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));

  // The tested behavior relies on the app list presenter delegate.
  test::TestAppListViewPresenterImpl app_list_presenter_impl;

  app_list_presenter_impl.Show(
      display::Screen::GetScreen()->GetPrimaryDisplay().id());
  EXPECT_TRUE(app_list_presenter_impl.IsVisible());

  ToggleOverview();
  EXPECT_FALSE(app_list_presenter_impl.IsVisible());
  ToggleOverview();
}

// Tests that a minimized window's visibility and layer visibility is correctly
// changed when entering overview and restored when leaving overview mode.
// Crashes after the skia roll in http://crrev.com/274114.
// http://crbug.com/379570
TEST_F(WindowSelectorTest, DISABLED_MinimizedWindowVisibility) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  wm::WindowState* window_state = wm::GetWindowState(window1.get());
  window_state->Minimize();
  EXPECT_FALSE(window1->IsVisible());
  EXPECT_FALSE(window1->layer()->GetTargetVisibility());
  {
    ui::ScopedAnimationDurationScaleMode test_duration_mode(
        ui::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
    ToggleOverview();
    EXPECT_TRUE(window1->IsVisible());
    EXPECT_TRUE(window1->layer()->GetTargetVisibility());
  }
  {
    ui::ScopedAnimationDurationScaleMode test_duration_mode(
        ui::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
    ToggleOverview();
    EXPECT_FALSE(window1->IsVisible());
    EXPECT_FALSE(window1->layer()->GetTargetVisibility());
  }
}

// Tests that it is safe to destroy a window while the overview header animation
// is still active. See http://crbug.com/646350.
TEST_F(WindowSelectorTest, SafeToDestroyWindowDuringAnimation) {
  gfx::Rect bounds(0, 0, 400, 400);
  {
    // Quickly enter and exit overview mode to activate header animations.
    std::unique_ptr<aura::Window> window(CreateWindow(bounds));
    ui::ScopedAnimationDurationScaleMode test_duration_mode(
        ui::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
    ToggleOverview();
    EXPECT_TRUE(IsSelecting());

    gfx::SlideAnimation* animation =
        GetBackgroundViewAnimationForWindow(0, window.get());
    ASSERT_NE(nullptr, animation);
    ToggleOverview();
    EXPECT_FALSE(IsSelecting());
    if (animation)
      EXPECT_TRUE(animation->is_animating());

    // Close the window while the overview header animation is active.
    window.reset();

    // Progress animation to the end - should not crash.
    if (animation) {
      animation->SetCurrentValue(1.0);
      animation->Reset(1.0);
    }
  }
}

// Tests that a bounds change during overview is corrected for.
TEST_F(WindowSelectorTest, BoundsChangeDuringOverview) {
  std::unique_ptr<aura::Window> window(CreateWindow(gfx::Rect(0, 0, 400, 400)));
  // Use overview headers above the window in this test.
  window->SetProperty(aura::client::kTopViewInset, 0);
  ToggleOverview();
  gfx::Rect overview_bounds = GetTransformedTargetBounds(window.get());
  window->SetBounds(gfx::Rect(200, 0, 200, 200));
  gfx::Rect new_overview_bounds = GetTransformedTargetBounds(window.get());
  EXPECT_EQ(overview_bounds.x(), new_overview_bounds.x());
  EXPECT_EQ(overview_bounds.y(), new_overview_bounds.y());
  EXPECT_EQ(overview_bounds.width(), new_overview_bounds.width());
  EXPECT_EQ(overview_bounds.height(), new_overview_bounds.height());
  ToggleOverview();
}

// Tests that a newly created window aborts overview.
TEST_F(WindowSelectorTest, NewWindowCancelsOveriew) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  ToggleOverview();
  EXPECT_TRUE(IsSelecting());

  // A window being created should exit overview mode.
  std::unique_ptr<aura::Window> window3(CreateWindow(bounds));
  EXPECT_FALSE(IsSelecting());
}

// Tests that a window activation exits overview mode.
TEST_F(WindowSelectorTest, ActivationCancelsOveriew) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  window2->Focus();
  ToggleOverview();
  EXPECT_TRUE(IsSelecting());

  // A window being activated should exit overview mode.
  window1->Focus();
  EXPECT_FALSE(IsSelecting());

  // window1 should be focused after exiting even though window2 was focused on
  // entering overview because we exited due to an activation.
  EXPECT_EQ(window1.get(), wm::GetFocusedWindow());
}

// Tests that exiting overview mode without selecting a window restores focus
// to the previously focused window.
TEST_F(WindowSelectorTest, CancelRestoresFocus) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window(CreateWindow(bounds));
  wm::ActivateWindow(window.get());
  EXPECT_EQ(window.get(), wm::GetFocusedWindow());

  // In overview mode, the text filter widget should be focused.
  ToggleOverview();
  EXPECT_EQ(text_filter_widget()->GetNativeWindow(), wm::GetFocusedWindow());

  // If canceling overview mode, focus should be restored.
  ToggleOverview();
  EXPECT_EQ(window.get(), wm::GetFocusedWindow());
}

// Tests that overview mode is exited if the last remaining window is destroyed.
TEST_F(WindowSelectorTest, LastWindowDestroyed) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  ToggleOverview();

  window1.reset();
  window2.reset();
  EXPECT_FALSE(IsSelecting());
}

// Tests that entering overview mode restores a window to its original
// target location.
TEST_F(WindowSelectorTest, QuickReentryRestoresInitialTransform) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window(CreateWindow(bounds));
  gfx::Rect initial_bounds = GetTransformedBounds(window.get());
  ToggleOverview();
  // Quickly exit and reenter overview mode. The window should still be
  // animating when we reenter. We cannot short circuit animations for this but
  // we also don't have to wait for them to complete.
  {
    ui::ScopedAnimationDurationScaleMode test_duration_mode(
        ui::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
    ToggleOverview();
    ToggleOverview();
  }
  EXPECT_NE(initial_bounds, GetTransformedTargetBounds(window.get()));
  ToggleOverview();
  EXPECT_FALSE(IsSelecting());
  EXPECT_EQ(initial_bounds, GetTransformedTargetBounds(window.get()));
}

// Tests that windows with modal child windows are transformed with the modal
// child even though not activatable themselves.
TEST_F(WindowSelectorTest, ModalChild) {
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> child1(CreateWindow(bounds));
  child1->SetProperty(aura::client::kModalKey, ui::MODAL_TYPE_WINDOW);
  ::wm::AddTransientChild(window1.get(), child1.get());
  EXPECT_EQ(window1->parent(), child1->parent());
  ToggleOverview();
  EXPECT_TRUE(window1->IsVisible());
  EXPECT_TRUE(child1->IsVisible());
  EXPECT_EQ(GetTransformedTargetBounds(child1.get()),
            GetTransformedTargetBounds(window1.get()));
  ToggleOverview();
}

// Tests that clicking a modal window's parent activates the modal window in
// overview.
TEST_F(WindowSelectorTest, ClickModalWindowParent) {
  std::unique_ptr<aura::Window> window1(
      CreateWindow(gfx::Rect(0, 0, 180, 180)));
  std::unique_ptr<aura::Window> child1(
      CreateWindow(gfx::Rect(200, 0, 180, 180)));
  child1->SetProperty(aura::client::kModalKey, ui::MODAL_TYPE_WINDOW);
  ::wm::AddTransientChild(window1.get(), child1.get());
  EXPECT_FALSE(WindowsOverlapping(window1.get(), child1.get()));
  EXPECT_EQ(window1->parent(), child1->parent());
  ToggleOverview();
  // Given that their relative positions are preserved, the windows should still
  // not overlap.
  EXPECT_FALSE(WindowsOverlapping(window1.get(), child1.get()));
  ClickWindow(window1.get());
  EXPECT_FALSE(IsSelecting());

  // Clicking on window1 should activate child1.
  EXPECT_TRUE(wm::IsActiveWindow(child1.get()));
}

// Tests that windows remain on the display they are currently on in overview
// mode, and that the close buttons are on matching displays.
TEST_F(WindowSelectorTest, MultipleDisplays) {
  UpdateDisplay("600x400,600x400");
  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  gfx::Rect bounds1(0, 0, 400, 400);
  gfx::Rect bounds2(650, 0, 400, 400);

  std::unique_ptr<aura::Window> window1(CreateWindow(bounds1));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds1));
  std::unique_ptr<aura::Window> window3(CreateWindow(bounds2));
  std::unique_ptr<aura::Window> window4(CreateWindow(bounds2));
  std::unique_ptr<aura::Window> panel1(CreatePanelWindow(bounds1));
  std::unique_ptr<aura::Window> panel2(CreatePanelWindow(bounds1));
  std::unique_ptr<aura::Window> panel3(CreatePanelWindow(bounds2));
  std::unique_ptr<aura::Window> panel4(CreatePanelWindow(bounds2));
  EXPECT_EQ(root_windows[0], window1->GetRootWindow());
  EXPECT_EQ(root_windows[0], window2->GetRootWindow());
  EXPECT_EQ(root_windows[1], window3->GetRootWindow());
  EXPECT_EQ(root_windows[1], window4->GetRootWindow());

  EXPECT_EQ(root_windows[0], panel1->GetRootWindow());
  EXPECT_EQ(root_windows[0], panel2->GetRootWindow());
  EXPECT_EQ(root_windows[1], panel3->GetRootWindow());
  EXPECT_EQ(root_windows[1], panel4->GetRootWindow());

  // In overview mode, each window remains in the same root window.
  ToggleOverview();
  EXPECT_EQ(root_windows[0], window1->GetRootWindow());
  EXPECT_EQ(root_windows[0], window2->GetRootWindow());
  EXPECT_EQ(root_windows[1], window3->GetRootWindow());
  EXPECT_EQ(root_windows[1], window4->GetRootWindow());
  EXPECT_EQ(root_windows[0], panel1->GetRootWindow());
  EXPECT_EQ(root_windows[0], panel2->GetRootWindow());
  EXPECT_EQ(root_windows[1], panel3->GetRootWindow());
  EXPECT_EQ(root_windows[1], panel4->GetRootWindow());

  // Window indices are based on top-down order. The reverse of our creation.
  IsWindowAndCloseButtonInScreen(window1.get(),
                                 GetWindowItemForWindow(0, window1.get()));
  IsWindowAndCloseButtonInScreen(window2.get(),
                                 GetWindowItemForWindow(0, window2.get()));
  IsWindowAndCloseButtonInScreen(window3.get(),
                                 GetWindowItemForWindow(1, window3.get()));
  IsWindowAndCloseButtonInScreen(window4.get(),
                                 GetWindowItemForWindow(1, window4.get()));

  IsWindowAndCloseButtonInScreen(panel1.get(),
                                 GetWindowItemForWindow(0, panel1.get()));
  IsWindowAndCloseButtonInScreen(panel2.get(),
                                 GetWindowItemForWindow(0, panel2.get()));
  IsWindowAndCloseButtonInScreen(panel3.get(),
                                 GetWindowItemForWindow(1, panel3.get()));
  IsWindowAndCloseButtonInScreen(panel4.get(),
                                 GetWindowItemForWindow(1, panel4.get()));
}

// Tests shutting down during overview.
TEST_F(WindowSelectorTest, Shutdown) {
  gfx::Rect bounds(0, 0, 400, 400);
  // These windows will be deleted when the test exits and the Shell instance
  // is shut down.
  aura::Window* window1(CreateWindow(bounds));
  aura::Window* window2(CreateWindow(bounds));
  aura::Window* window3(CreatePanelWindow(bounds));
  aura::Window* window4(CreatePanelWindow(bounds));

  wm::ActivateWindow(window4);
  wm::ActivateWindow(window3);
  wm::ActivateWindow(window2);
  wm::ActivateWindow(window1);

  ToggleOverview();
}

// Tests removing a display during overview.
TEST_F(WindowSelectorTest, RemoveDisplay) {
  // TODO: hits CHECK in stl as order of |ShelfModel::items_| is wrong.
  // http://crbug.com/698878.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  UpdateDisplay("400x400,400x400");
  gfx::Rect bounds1(0, 0, 100, 100);
  gfx::Rect bounds2(450, 0, 100, 100);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds1));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds2));
  std::unique_ptr<aura::Window> window3(CreatePanelWindow(bounds1));
  std::unique_ptr<aura::Window> window4(CreatePanelWindow(bounds2));

  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  EXPECT_EQ(root_windows[0], window1->GetRootWindow());
  EXPECT_EQ(root_windows[1], window2->GetRootWindow());
  EXPECT_EQ(root_windows[0], window3->GetRootWindow());
  EXPECT_EQ(root_windows[1], window4->GetRootWindow());

  wm::ActivateWindow(window4.get());
  wm::ActivateWindow(window3.get());
  wm::ActivateWindow(window2.get());
  wm::ActivateWindow(window1.get());

  ToggleOverview();
  EXPECT_TRUE(IsSelecting());
  UpdateDisplay("400x400");
  EXPECT_FALSE(IsSelecting());
}

// Tests starting overview during a drag and drop tracking operation.
// TODO(flackr): Fix memory corruption crash when running locally (not failing
// on bots). See http://crbug.com/342528.
TEST_F(WindowSelectorTest, DISABLED_DragDropInProgress) {
  bool drag_canceled_by_test = false;
  gfx::Rect bounds(0, 0, 400, 400);
  std::unique_ptr<aura::Window> window(CreateWindow(bounds));
  test::ShellTestApi shell_test_api(Shell::Get());
  DragDropController* drag_drop_controller =
      shell_test_api.drag_drop_controller();
  ui::OSExchangeData data;
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&WindowSelectorTest::ToggleOverview, base::Unretained(this)));
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&CancelDrag, drag_drop_controller, &drag_canceled_by_test));
  data.SetString(base::UTF8ToUTF16("I am being dragged"));
  drag_drop_controller->StartDragAndDrop(
      data, window->GetRootWindow(), window.get(), gfx::Point(5, 5),
      ui::DragDropTypes::DRAG_MOVE, ui::DragDropTypes::DRAG_EVENT_SOURCE_MOUSE);
  RunAllPendingInMessageLoop();
  EXPECT_FALSE(drag_canceled_by_test);
  ASSERT_TRUE(IsSelecting());
  RunAllPendingInMessageLoop();
}

// Test that a label is created under the window on entering overview mode.
TEST_F(WindowSelectorTest, CreateLabelUnderWindow) {
  std::unique_ptr<aura::Window> window(CreateWindow(gfx::Rect(0, 0, 300, 500)));
  base::string16 window_title = base::UTF8ToUTF16("My window");
  window->SetTitle(window_title);
  ToggleOverview();
  WindowSelectorItem* window_item = GetWindowItemsForRoot(0).back().get();
  views::Label* label = GetLabelView(window_item);
  // Has the label view been created?
  ASSERT_TRUE(label);

  // Verify the label matches the window title.
  EXPECT_EQ(label->text(), window_title);

  // Update the window title and check that the label is updated, too.
  base::string16 updated_title = base::UTF8ToUTF16("Updated title");
  window->SetTitle(updated_title);
  EXPECT_EQ(label->text(), updated_title);

  // Labels are located based on target_bounds, not the actual window item
  // bounds.
  gfx::Rect label_bounds = label->GetWidget()->GetWindowBoundsInScreen();
  label_bounds.Inset(kWindowMargin, kWindowMargin);
  EXPECT_EQ(window_item->target_bounds(), label_bounds);
}

// Tests that overview updates the window positions if the display orientation
// changes.
TEST_F(WindowSelectorTest, DisplayOrientationChanged) {
  // TODO: fails in mash because rotation is not supported.
  // http://crbug.com/695556
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  aura::Window* root_window = Shell::Get()->GetPrimaryRootWindow();
  UpdateDisplay("600x200");
  EXPECT_EQ("0,0 600x200", root_window->bounds().ToString());
  gfx::Rect window_bounds(0, 0, 150, 150);
  std::vector<std::unique_ptr<aura::Window>> windows;
  for (int i = 0; i < 3; i++)
    windows.push_back(base::WrapUnique(CreateWindow(window_bounds)));

  ToggleOverview();
  for (const auto& window : windows) {
    EXPECT_TRUE(root_window->bounds().Contains(
        GetTransformedTargetBounds(window.get())));
  }

  // Rotate the display, windows should be repositioned to be within the screen
  // bounds.
  UpdateDisplay("600x200/r");
  EXPECT_EQ("0,0 200x600", root_window->bounds().ToString());
  for (const auto& window : windows) {
    EXPECT_TRUE(root_window->bounds().Contains(
        GetTransformedTargetBounds(window.get())));
  }
}

// Tests traversing some windows in overview mode with the tab key.
TEST_F(WindowSelectorTest, BasicTabKeyNavigation) {
  gfx::Rect bounds(0, 0, 100, 100);
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  ToggleOverview();

  const std::vector<std::unique_ptr<WindowSelectorItem>>& overview_windows =
      GetWindowItemsForRoot(0);
  SendKey(ui::VKEY_TAB);
  EXPECT_EQ(GetSelectedWindow(),
            WmWindow::GetAuraWindow(overview_windows[0]->GetWindow()));
  SendKey(ui::VKEY_TAB);
  EXPECT_EQ(GetSelectedWindow(),
            WmWindow::GetAuraWindow(overview_windows[1]->GetWindow()));
  SendKey(ui::VKEY_TAB);
  EXPECT_EQ(GetSelectedWindow(),
            WmWindow::GetAuraWindow(overview_windows[0]->GetWindow()));
}

// Tests that pressing Ctrl+W while a window is selected in overview closes it.
TEST_F(WindowSelectorTest, CloseWindowWithKey) {
  gfx::Rect bounds(0, 0, 100, 100);
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  std::unique_ptr<views::Widget> widget =
      CreateWindowWidget(gfx::Rect(0, 0, 400, 400));
  aura::Window* window1 = widget->GetNativeWindow();
  ToggleOverview();

  SendKey(ui::VKEY_RIGHT);
  EXPECT_EQ(window1, GetSelectedWindow());
  SendCtrlKey(ui::VKEY_W);
  EXPECT_TRUE(widget->IsClosed());
}

// Tests traversing some windows in overview mode with the arrow keys in every
// possible direction.
TEST_F(WindowSelectorTest, BasicArrowKeyNavigation) {
  const size_t test_windows = 9;
  UpdateDisplay("800x600");
  std::vector<std::unique_ptr<aura::Window>> windows;
  for (size_t i = test_windows; i > 0; i--) {
    windows.push_back(
        base::WrapUnique(CreateWindowWithId(gfx::Rect(0, 0, 100, 100), i)));
  }

  ui::KeyboardCode arrow_keys[] = {ui::VKEY_RIGHT, ui::VKEY_DOWN, ui::VKEY_LEFT,
                                   ui::VKEY_UP};
  // The rows contain variable number of items making vertical navigation not
  // feasible. [Down] is equivalent to [Right] and [Up] is equivalent to [Left].
  int index_path_for_direction[][test_windows + 1] = {
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 1},  // Right
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 1},  // Down (same as Right)
      {9, 8, 7, 6, 5, 4, 3, 2, 1, 9},  // Left
      {9, 8, 7, 6, 5, 4, 3, 2, 1, 9}   // Up (same as Left)
  };

  for (size_t key_index = 0; key_index < arraysize(arrow_keys); key_index++) {
    ToggleOverview();
    const std::vector<std::unique_ptr<WindowSelectorItem>>& overview_windows =
        GetWindowItemsForRoot(0);
    for (size_t i = 0; i < test_windows + 1; i++) {
      SendKey(arrow_keys[key_index]);
      // TODO(flackr): Add a more readable error message by constructing a
      // string from the window IDs.
      const int index = index_path_for_direction[key_index][i];
      EXPECT_EQ(GetSelectedWindow()->id(),
                overview_windows[index - 1]->GetWindow()->aura_window()->id());
    }
    ToggleOverview();
  }
}

// Tests basic selection across multiple monitors.
TEST_F(WindowSelectorTest, BasicMultiMonitorArrowKeyNavigation) {
  UpdateDisplay("400x400,400x400");
  gfx::Rect bounds1(0, 0, 100, 100);
  gfx::Rect bounds2(450, 0, 100, 100);
  std::unique_ptr<aura::Window> window4(CreateWindow(bounds2));
  std::unique_ptr<aura::Window> window3(CreateWindow(bounds2));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds1));
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds1));

  ToggleOverview();

  const std::vector<std::unique_ptr<WindowSelectorItem>>& overview_root1 =
      GetWindowItemsForRoot(0);
  const std::vector<std::unique_ptr<WindowSelectorItem>>& overview_root2 =
      GetWindowItemsForRoot(1);
  SendKey(ui::VKEY_RIGHT);
  EXPECT_EQ(GetSelectedWindow(),
            WmWindow::GetAuraWindow(overview_root1[0]->GetWindow()));
  SendKey(ui::VKEY_RIGHT);
  EXPECT_EQ(GetSelectedWindow(),
            WmWindow::GetAuraWindow(overview_root1[1]->GetWindow()));
  SendKey(ui::VKEY_RIGHT);
  EXPECT_EQ(GetSelectedWindow(),
            WmWindow::GetAuraWindow(overview_root2[0]->GetWindow()));
  SendKey(ui::VKEY_RIGHT);
  EXPECT_EQ(GetSelectedWindow(),
            WmWindow::GetAuraWindow(overview_root2[1]->GetWindow()));
}

// Tests first monitor when display order doesn't match left to right screen
// positions.
TEST_F(WindowSelectorTest, MultiMonitorReversedOrder) {
  // TODO: SetLayoutForCurrentDisplays() needs to ported to mash.
  // http://crbug.com/698043.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  UpdateDisplay("400x400,400x400");
  Shell::Get()->display_manager()->SetLayoutForCurrentDisplays(
      display::test::CreateDisplayLayout(display_manager(),
                                         display::DisplayPlacement::LEFT, 0));
  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  gfx::Rect bounds1(-350, 0, 100, 100);
  gfx::Rect bounds2(0, 0, 100, 100);
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds2));
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds1));
  EXPECT_EQ(root_windows[1], window1->GetRootWindow());
  EXPECT_EQ(root_windows[0], window2->GetRootWindow());

  ToggleOverview();

  // Coming from the left to right, we should select window1 first being on the
  // display to the left.
  SendKey(ui::VKEY_RIGHT);
  EXPECT_EQ(GetSelectedWindow(), window1.get());

  ToggleOverview();
  ToggleOverview();

  // Coming from right to left, we should select window2 first being on the
  // display on the right.
  SendKey(ui::VKEY_LEFT);
  EXPECT_EQ(GetSelectedWindow(), window2.get());
}

// Tests three monitors where the grid becomes empty on one of the monitors.
TEST_F(WindowSelectorTest, ThreeMonitor) {
  UpdateDisplay("400x400,400x400,400x400");
  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  gfx::Rect bounds1(0, 0, 100, 100);
  gfx::Rect bounds2(400, 0, 100, 100);
  gfx::Rect bounds3(800, 0, 100, 100);
  std::unique_ptr<aura::Window> window3(CreateWindow(bounds3));
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds2));
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds1));
  EXPECT_EQ(root_windows[0], window1->GetRootWindow());
  EXPECT_EQ(root_windows[1], window2->GetRootWindow());
  EXPECT_EQ(root_windows[2], window3->GetRootWindow());

  ToggleOverview();

  SendKey(ui::VKEY_RIGHT);
  SendKey(ui::VKEY_RIGHT);
  SendKey(ui::VKEY_RIGHT);
  EXPECT_EQ(GetSelectedWindow(), window3.get());

  // If the last window on a display closes it should select the previous
  // display's window.
  window3.reset();
  EXPECT_EQ(GetSelectedWindow(), window2.get());
  ToggleOverview();

  window3.reset(CreateWindow(bounds3));
  ToggleOverview();
  SendKey(ui::VKEY_RIGHT);
  SendKey(ui::VKEY_RIGHT);
  SendKey(ui::VKEY_RIGHT);

  // If the window on the second display is removed, the selected window should
  // remain window3.
  EXPECT_EQ(GetSelectedWindow(), window3.get());
  window2.reset();
  EXPECT_EQ(GetSelectedWindow(), window3.get());
}

// Tests selecting a window in overview mode with the return key.
TEST_F(WindowSelectorTest, SelectWindowWithReturnKey) {
  gfx::Rect bounds(0, 0, 100, 100);
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  ToggleOverview();

  // Pressing the return key without a selection widget should not do anything.
  SendKey(ui::VKEY_RETURN);
  EXPECT_TRUE(IsSelecting());

  // Select the first window.
  ASSERT_TRUE(SelectWindow(window1.get()));
  SendKey(ui::VKEY_RETURN);
  ASSERT_FALSE(IsSelecting());
  EXPECT_TRUE(wm::IsActiveWindow(window1.get()));

  // Select the second window.
  ToggleOverview();
  ASSERT_TRUE(SelectWindow(window2.get()));
  SendKey(ui::VKEY_RETURN);
  EXPECT_FALSE(IsSelecting());
  EXPECT_TRUE(wm::IsActiveWindow(window2.get()));
}

// Tests that overview mode hides the callout widget.
TEST_F(WindowSelectorTest, WindowOverviewHidesCalloutWidgets) {
  std::unique_ptr<aura::Window> panel1(
      CreatePanelWindow(gfx::Rect(0, 0, 100, 100)));
  WmWindow* wm_panel1 = WmWindow::Get(panel1.get());
  std::unique_ptr<aura::Window> panel2(
      CreatePanelWindow(gfx::Rect(0, 0, 100, 100)));
  WmWindow* wm_panel2 = WmWindow::Get(panel2.get());
  PanelLayoutManager* panel_manager = PanelLayoutManager::Get(wm_panel1);

  // By default, panel callout widgets are visible.
  EXPECT_TRUE(panel_manager->GetCalloutWidgetForPanel(wm_panel1)->IsVisible());
  EXPECT_TRUE(panel_manager->GetCalloutWidgetForPanel(wm_panel2)->IsVisible());

  // Toggling the overview should hide the callout widgets.
  ToggleOverview();
  EXPECT_FALSE(panel_manager->GetCalloutWidgetForPanel(wm_panel1)->IsVisible());
  EXPECT_FALSE(panel_manager->GetCalloutWidgetForPanel(wm_panel2)->IsVisible());

  // Ending the overview should show them again.
  ToggleOverview();
  EXPECT_TRUE(panel_manager->GetCalloutWidgetForPanel(wm_panel1)->IsVisible());
  EXPECT_TRUE(panel_manager->GetCalloutWidgetForPanel(wm_panel2)->IsVisible());
}

// Creates three windows and tests filtering them by title.
TEST_F(WindowSelectorTest, BasicTextFiltering) {
  gfx::Rect bounds(0, 0, 100, 100);
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window0(CreateWindow(bounds));
  base::string16 window2_title = base::UTF8ToUTF16("Highway to test");
  base::string16 window1_title = base::UTF8ToUTF16("For those about to test");
  base::string16 window0_title = base::UTF8ToUTF16("We salute you");
  window0->SetTitle(window0_title);
  window1->SetTitle(window1_title);
  window2->SetTitle(window2_title);
  ToggleOverview();

  EXPECT_FALSE(selection_widget_active());
  EXPECT_FALSE(showing_filter_widget());
  FilterItems("Test");

  // The selection widget should appear when filtering starts, and should be
  // selecting one of the matching windows above.
  EXPECT_TRUE(selection_widget_active());
  EXPECT_TRUE(showing_filter_widget());
  // window0 does not contain the text "test".
  EXPECT_NE(GetSelectedWindow(), window0.get());

  // Window 0 has no "test" on it so it should be the only dimmed item.
  const int grid_index = 0;
  EXPECT_TRUE(GetWindowItemForWindow(grid_index, window0.get())->dimmed());
  EXPECT_FALSE(GetWindowItemForWindow(grid_index, window1.get())->dimmed());
  EXPECT_FALSE(GetWindowItemForWindow(grid_index, window2.get())->dimmed());

  // No items match the search.
  FilterItems("I'm testing 'n testing");
  EXPECT_TRUE(GetWindowItemForWindow(grid_index, window0.get())->dimmed());
  EXPECT_TRUE(GetWindowItemForWindow(grid_index, window1.get())->dimmed());
  EXPECT_TRUE(GetWindowItemForWindow(grid_index, window2.get())->dimmed());

  // All the items should match the empty string. The filter widget should also
  // disappear.
  FilterItems("");
  EXPECT_FALSE(showing_filter_widget());
  EXPECT_FALSE(GetWindowItemForWindow(grid_index, window0.get())->dimmed());
  EXPECT_FALSE(GetWindowItemForWindow(grid_index, window1.get())->dimmed());
  EXPECT_FALSE(GetWindowItemForWindow(grid_index, window2.get())->dimmed());

  FilterItems("Foo");

  EXPECT_NE(1.0f, window0->layer()->GetTargetOpacity());
  EXPECT_NE(1.0f, window1->layer()->GetTargetOpacity());
  EXPECT_NE(1.0f, window2->layer()->GetTargetOpacity());

  ToggleOverview();

  EXPECT_EQ(1.0f, window0->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window1->layer()->GetTargetOpacity());
  EXPECT_EQ(1.0f, window2->layer()->GetTargetOpacity());
}

// Tests selecting in the overview with dimmed and undimmed items.
TEST_F(WindowSelectorTest, TextFilteringSelection) {
  gfx::Rect bounds(0, 0, 100, 100);
  std::unique_ptr<aura::Window> window2(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  std::unique_ptr<aura::Window> window0(CreateWindow(bounds));
  base::string16 window2_title = base::UTF8ToUTF16("Rock and roll");
  base::string16 window1_title = base::UTF8ToUTF16("Rock and");
  base::string16 window0_title = base::UTF8ToUTF16("Rock");
  window0->SetTitle(window0_title);
  window1->SetTitle(window1_title);
  window2->SetTitle(window2_title);
  ToggleOverview();
  EXPECT_TRUE(SelectWindow(window0.get()));
  EXPECT_TRUE(selection_widget_active());

  // Dim the first item, the selection should jump to the next item.
  FilterItems("Rock and");
  EXPECT_NE(GetSelectedWindow(), window0.get());

  // Cycle the selection, the dimmed window should not be selected.
  EXPECT_FALSE(SelectWindow(window0.get()));

  // Dimming all the items should hide the selection widget.
  FilterItems("Pop");
  EXPECT_FALSE(selection_widget_active());

  // Undimming one window should automatically select it.
  FilterItems("Rock and roll");
  EXPECT_EQ(GetSelectedWindow(), window2.get());
}

// Tests that transferring focus from the text filter to a window that is not a
// top level window does not cancel overview mode.
TEST_F(WindowSelectorTest, ShowTextFilterMenu) {
  gfx::Rect bounds(0, 0, 100, 100);
  std::unique_ptr<aura::Window> window0(CreateWindow(bounds));
  base::string16 window0_title = base::UTF8ToUTF16("Test");
  window0->SetTitle(window0_title);
  wm::GetWindowState(window0.get())->Minimize();
  ToggleOverview();

  EXPECT_FALSE(selection_widget_active());
  EXPECT_FALSE(showing_filter_widget());
  FilterItems("Test");

  EXPECT_TRUE(selection_widget_active());
  EXPECT_TRUE(showing_filter_widget());

  // Open system bubble shifting focus from the text filter.
  SystemTray* tray = GetPrimarySystemTray();
  tray->ShowDefaultView(BUBBLE_CREATE_NEW);
  RunAllPendingInMessageLoop();

  // This should not cancel overview mode.
  ASSERT_TRUE(IsSelecting());
  EXPECT_TRUE(selection_widget_active());
  EXPECT_TRUE(showing_filter_widget());

  // Click text filter to bring focus back.
  ui::test::EventGenerator& generator = GetEventGenerator();
  gfx::Point point_in_text_filter =
      text_filter_widget()->GetWindowBoundsInScreen().CenterPoint();
  generator.MoveMouseTo(point_in_text_filter);
  generator.ClickLeftButton();
  EXPECT_TRUE(IsSelecting());

  // Cancel overview mode.
  ToggleOverview();
  ASSERT_FALSE(IsSelecting());
}

// Tests clicking on the desktop itself to cancel overview mode.
TEST_F(WindowSelectorTest, CancelOverviewOnMouseClick) {
  // Overview disabled by default.
  EXPECT_FALSE(IsSelecting());

  // Point and bounds selected so that they don't intersect. This causes
  // events located at the point to be passed to WallpaperController,
  // and not the window.
  gfx::Point point_in_background_page(0, 0);
  gfx::Rect bounds(10, 10, 100, 100);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  ui::test::EventGenerator& generator = GetEventGenerator();
  // Move mouse to point in the background page. Sending an event here will pass
  // it to the WallpaperController in both regular and overview mode.
  generator.MoveMouseTo(point_in_background_page);

  // Clicking on the background page while not in overview should not toggle
  // overview.
  generator.ClickLeftButton();
  EXPECT_FALSE(IsSelecting());

  // Switch to overview mode.
  ToggleOverview();
  ASSERT_TRUE(IsSelecting());

  // Click should now exit overview mode.
  generator.ClickLeftButton();
  EXPECT_FALSE(IsSelecting());
}

// Tests tapping on the desktop itself to cancel overview mode.
TEST_F(WindowSelectorTest, CancelOverviewOnTap) {
  // Overview disabled by default.
  EXPECT_FALSE(IsSelecting());

  // Point and bounds selected so that they don't intersect. This causes
  // events located at the point to be passed to WallpaperController,
  // and not the window.
  gfx::Point point_in_background_page(0, 0);
  gfx::Rect bounds(10, 10, 100, 100);
  std::unique_ptr<aura::Window> window1(CreateWindow(bounds));
  ui::test::EventGenerator& generator = GetEventGenerator();

  // Tapping on the background page while not in overview should not toggle
  // overview.
  generator.GestureTapAt(point_in_background_page);
  EXPECT_FALSE(IsSelecting());

  // Switch to overview mode.
  ToggleOverview();
  ASSERT_TRUE(IsSelecting());

  // Tap should now exit overview mode.
  generator.GestureTapAt(point_in_background_page);
  EXPECT_FALSE(IsSelecting());
}

// Tests that transformed Rect scaling preserves its aspect ratio.
// The window scale is determined by the target height and so the test is
// actually testing that the width is calculated correctly. Since all
// calculations are done with floating point values and then safely converted to
// integers (using ceiled and floored values where appropriate), the
// expectations are forgiving (use *_NEAR) within a single pixel.
TEST_F(WindowSelectorTest, TransformedRectMaintainsAspect) {
  gfx::Rect rect(50, 50, 200, 400);
  gfx::Rect bounds(100, 100, 50, 50);
  gfx::Rect transformed_rect =
      ScopedTransformOverviewWindow::ShrinkRectToFitPreservingAspectRatio(
          rect, bounds, 0, 0);
  float scale = GetItemScale(rect, bounds, 0, 0);
  EXPECT_NEAR(scale * rect.width(), transformed_rect.width(), 1);
  EXPECT_NEAR(scale * rect.height(), transformed_rect.height(), 1);

  rect = gfx::Rect(50, 50, 400, 200);
  scale = GetItemScale(rect, bounds, 0, 0);
  transformed_rect =
      ScopedTransformOverviewWindow::ShrinkRectToFitPreservingAspectRatio(
          rect, bounds, 0, 0);
  EXPECT_NEAR(scale * rect.width(), transformed_rect.width(), 1);
  EXPECT_NEAR(scale * rect.height(), transformed_rect.height(), 1);

  rect = gfx::Rect(50, 50, 25, 25);
  scale = GetItemScale(rect, bounds, 0, 0);
  transformed_rect =
      ScopedTransformOverviewWindow::ShrinkRectToFitPreservingAspectRatio(
          rect, bounds, 0, 0);
  EXPECT_NEAR(scale * rect.width(), transformed_rect.width(), 1);
  EXPECT_NEAR(scale * rect.height(), transformed_rect.height(), 1);

  rect = gfx::Rect(50, 50, 25, 50);
  scale = GetItemScale(rect, bounds, 0, 0);
  transformed_rect =
      ScopedTransformOverviewWindow::ShrinkRectToFitPreservingAspectRatio(
          rect, bounds, 0, 0);
  EXPECT_NEAR(scale * rect.width(), transformed_rect.width(), 1);
  EXPECT_NEAR(scale * rect.height(), transformed_rect.height(), 1);

  rect = gfx::Rect(50, 50, 50, 25);
  scale = GetItemScale(rect, bounds, 0, 0);
  transformed_rect =
      ScopedTransformOverviewWindow::ShrinkRectToFitPreservingAspectRatio(
          rect, bounds, 0, 0);
  EXPECT_NEAR(scale * rect.width(), transformed_rect.width(), 1);
  EXPECT_NEAR(scale * rect.height(), transformed_rect.height(), 1);
}

// Tests that transformed Rect fits in target bounds and is vertically centered.
TEST_F(WindowSelectorTest, TransformedRectIsCentered) {
  gfx::Rect rect(50, 50, 200, 400);
  gfx::Rect bounds(100, 100, 50, 50);
  gfx::Rect transformed_rect =
      ScopedTransformOverviewWindow::ShrinkRectToFitPreservingAspectRatio(
          rect, bounds, 0, 0);
  EXPECT_GE(transformed_rect.x(), bounds.x());
  EXPECT_LE(transformed_rect.right(), bounds.right());
  EXPECT_GE(transformed_rect.y(), bounds.y());
  EXPECT_LE(transformed_rect.bottom(), bounds.bottom());
  EXPECT_NEAR(transformed_rect.x() - bounds.x(),
              bounds.right() - transformed_rect.right(), 1);
  EXPECT_NEAR(transformed_rect.y() - bounds.y(),
              bounds.bottom() - transformed_rect.bottom(), 1);
}

// Tests that transformed Rect fits in target bounds and is vertically centered
// when inset and header height are specified.
TEST_F(WindowSelectorTest, TransformedRectIsCenteredWithInset) {
  gfx::Rect rect(50, 50, 400, 200);
  gfx::Rect bounds(100, 100, 50, 50);
  const int inset = 20;
  const int header_height = 10;
  const float scale = GetItemScale(rect, bounds, inset, header_height);
  gfx::Rect transformed_rect =
      ScopedTransformOverviewWindow::ShrinkRectToFitPreservingAspectRatio(
          rect, bounds, inset, header_height);
  // The |rect| width does not fit and therefore it gets centered outside
  // |bounds| starting before |bounds.x()| and ending after |bounds.right()|.
  EXPECT_LE(transformed_rect.x(), bounds.x());
  EXPECT_GE(transformed_rect.right(), bounds.right());
  EXPECT_GE(
      transformed_rect.y() + gfx::ToCeiledInt(scale * inset) - header_height,
      bounds.y());
  EXPECT_LE(transformed_rect.bottom(), bounds.bottom());
  EXPECT_NEAR(transformed_rect.x() - bounds.x(),
              bounds.right() - transformed_rect.right(), 1);
  EXPECT_NEAR(
      transformed_rect.y() + (int)(scale * inset) - header_height - bounds.y(),
      bounds.bottom() - transformed_rect.bottom(), 1);
}

// Start dragging a window and activate overview mode. This test should not
// crash or DCHECK inside aura::Window::StackChildRelativeTo().
TEST_F(WindowSelectorTest, OverviewWhileDragging) {
  const gfx::Rect bounds(10, 10, 100, 100);
  std::unique_ptr<aura::Window> window(CreateWindow(bounds));
  std::unique_ptr<WindowResizer> resizer(
      CreateWindowResizer(window.get(), gfx::Point(), HTCAPTION,
                          aura::client::WINDOW_MOVE_SOURCE_MOUSE));
  ASSERT_TRUE(resizer.get());
  gfx::Point location = resizer->GetInitialLocation();
  location.Offset(20, 20);
  resizer->Drag(location, 0);
  ToggleOverview();
  resizer->RevertDrag();
}

}  // namespace ash
