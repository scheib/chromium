// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/workspace/multi_window_resize_controller.h"

#include "ash/ash_constants.h"
#include "ash/frame/custom_frame_view_ash.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/shell_test_api.h"
#include "ash/test/workspace_controller_test_api.h"
#include "ash/test/workspace_event_handler_test_helper.h"
#include "ash/wm/window_util.h"
#include "ash/wm/workspace_controller.h"
#include "ash/wm_window.h"
#include "base/stl_util.h"
#include "ui/aura/test/test_window_delegate.h"
#include "ui/aura/window.h"
#include "ui/base/hit_test.h"
#include "ui/events/test/event_generator.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace ash {

namespace {

// WidgetDelegate for a resizable widget which creates a NonClientFrameView
// which is actually used in Ash.
class TestWidgetDelegate : public views::WidgetDelegateView {
 public:
  TestWidgetDelegate() {}
  ~TestWidgetDelegate() override {}

  // views::WidgetDelegateView:
  bool CanResize() const override { return true; }

  views::NonClientFrameView* CreateNonClientFrameView(
      views::Widget* widget) override {
    return new CustomFrameViewAsh(widget);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestWidgetDelegate);
};

}  // namespace

class MultiWindowResizeControllerTest : public test::AshTestBase {
 public:
  MultiWindowResizeControllerTest() : resize_controller_(NULL) {}
  ~MultiWindowResizeControllerTest() override {}

  void SetUp() override {
    test::AshTestBase::SetUp();
    WorkspaceController* wc =
        test::ShellTestApi(Shell::Get()).workspace_controller();
    WorkspaceEventHandler* event_handler =
        test::WorkspaceControllerTestApi(wc).GetEventHandler();
    resize_controller_ =
        WorkspaceEventHandlerTestHelper(event_handler).resize_controller();
  }

 protected:
  aura::Window* CreateTestWindow(aura::WindowDelegate* delegate,
                                 const gfx::Rect& bounds) {
    aura::Window* window = new aura::Window(delegate);
    window->SetType(ui::wm::WINDOW_TYPE_NORMAL);
    window->Init(ui::LAYER_TEXTURED);
    ParentWindowInPrimaryRootWindow(window);
    window->SetBounds(bounds);
    window->Show();
    return window;
  }

  void ShowNow() { resize_controller_->ShowNow(); }

  bool IsShowing() { return resize_controller_->IsShowing(); }

  bool HasPendingShow() { return resize_controller_->show_timer_.IsRunning(); }

  void Hide() { resize_controller_->Hide(); }

  bool HasTarget(aura::Window* window) {
    if (!resize_controller_->windows_.is_valid())
      return false;
    if (resize_controller_->windows_.window1 == window ||
        resize_controller_->windows_.window2 == window) {
      return true;
    }
    return base::ContainsValue(resize_controller_->windows_.other_windows,
                               window);
  }

  bool IsOverWindows(const gfx::Point& loc) {
    return resize_controller_->IsOverWindows(loc);
  }

  views::Widget* resize_widget() {
    return resize_controller_->resize_widget_.get();
  }

  MultiWindowResizeController* resize_controller_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MultiWindowResizeControllerTest);
};

// Assertions around moving mouse over 2 windows.
TEST_F(MultiWindowResizeControllerTest, BasicTests) {
  aura::test::TestWindowDelegate delegate1;
  std::unique_ptr<aura::Window> w1(
      CreateTestWindow(&delegate1, gfx::Rect(0, 0, 100, 100)));
  delegate1.set_window_component(HTRIGHT);
  aura::test::TestWindowDelegate delegate2;
  std::unique_ptr<aura::Window> w2(
      CreateTestWindow(&delegate2, gfx::Rect(100, 0, 100, 100)));
  delegate2.set_window_component(HTRIGHT);
  ui::test::EventGenerator generator(w1->GetRootWindow());
  generator.MoveMouseTo(w1->bounds().CenterPoint());
  EXPECT_TRUE(HasPendingShow());
  EXPECT_TRUE(IsShowing());

  // Force a show now.
  ShowNow();
  EXPECT_FALSE(HasPendingShow());
  EXPECT_TRUE(IsShowing());

  EXPECT_FALSE(IsOverWindows(gfx::Point(200, 200)));

  // Have to explicitly invoke this as MouseWatcher listens for native events.
  resize_controller_->MouseMovedOutOfHost();
  EXPECT_FALSE(HasPendingShow());
  EXPECT_FALSE(IsShowing());
}

// Test the behavior of IsOverWindows().
TEST_F(MultiWindowResizeControllerTest, IsOverWindows) {
  // Create the following layout:
  //  __________________
  //  | w1     | w2     |
  //  |        |________|
  //  |        | w3     |
  //  |________|________|
  std::unique_ptr<views::Widget> w1(new views::Widget);
  views::Widget::InitParams params1;
  params1.delegate = new TestWidgetDelegate;
  params1.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params1.bounds = gfx::Rect(100, 200);
  params1.context = CurrentContext();
  w1->Init(params1);
  w1->Show();

  std::unique_ptr<views::Widget> w2(new views::Widget);
  views::Widget::InitParams params2;
  params2.delegate = new TestWidgetDelegate;
  params2.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params2.bounds = gfx::Rect(100, 0, 100, 100);
  params2.context = CurrentContext();
  w2->Init(params2);
  w2->Show();

  std::unique_ptr<views::Widget> w3(new views::Widget);
  views::Widget::InitParams params3;
  params3.delegate = new TestWidgetDelegate;
  params3.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params3.bounds = gfx::Rect(100, 100, 100, 100);
  params3.context = CurrentContext();
  w3->Init(params3);
  w3->Show();

  ui::test::EventGenerator& generator = GetEventGenerator();
  generator.MoveMouseTo(gfx::Point(100, 150));
  EXPECT_TRUE(HasPendingShow());
  EXPECT_TRUE(IsShowing());
  ShowNow();
  EXPECT_TRUE(IsShowing());

  // Check that the multi-window resize handle does not hide while the mouse is
  // over a window's resize area. A window's resize area extends outside the
  // window's bounds.
  EXPECT_TRUE(w3->IsActive());
  ASSERT_LT(kResizeInsideBoundsSize, kResizeOutsideBoundsSize);

  EXPECT_TRUE(IsOverWindows(gfx::Point(100, 150)));
  EXPECT_TRUE(IsOverWindows(gfx::Point(100 - kResizeOutsideBoundsSize, 150)));
  EXPECT_FALSE(
      IsOverWindows(gfx::Point(100 - kResizeOutsideBoundsSize - 1, 150)));
  EXPECT_TRUE(
      IsOverWindows(gfx::Point(100 + kResizeInsideBoundsSize - 1, 150)));
  EXPECT_FALSE(IsOverWindows(gfx::Point(100 + kResizeInsideBoundsSize, 150)));
  EXPECT_FALSE(
      IsOverWindows(gfx::Point(100 + kResizeOutsideBoundsSize - 1, 150)));

  w1->Activate();
  EXPECT_TRUE(IsOverWindows(gfx::Point(100, 150)));
  EXPECT_TRUE(IsOverWindows(gfx::Point(100 - kResizeInsideBoundsSize, 150)));
  EXPECT_FALSE(
      IsOverWindows(gfx::Point(100 - kResizeInsideBoundsSize - 1, 150)));
  EXPECT_FALSE(IsOverWindows(gfx::Point(100 - kResizeOutsideBoundsSize, 150)));
  EXPECT_TRUE(
      IsOverWindows(gfx::Point(100 + kResizeOutsideBoundsSize - 1, 150)));
  EXPECT_FALSE(IsOverWindows(gfx::Point(100 + kResizeOutsideBoundsSize, 150)));

  // Check that the multi-window resize handles eventually hide if the mouse
  // moves between |w1| and |w2|.
  EXPECT_FALSE(IsOverWindows(gfx::Point(100, 50)));
}

// Makes sure deleting a window hides.
TEST_F(MultiWindowResizeControllerTest, DeleteWindow) {
  aura::test::TestWindowDelegate delegate1;
  std::unique_ptr<aura::Window> w1(
      CreateTestWindow(&delegate1, gfx::Rect(0, 0, 100, 100)));
  delegate1.set_window_component(HTRIGHT);
  aura::test::TestWindowDelegate delegate2;
  std::unique_ptr<aura::Window> w2(
      CreateTestWindow(&delegate2, gfx::Rect(100, 0, 100, 100)));
  delegate2.set_window_component(HTRIGHT);
  ui::test::EventGenerator generator(w1->GetRootWindow());
  generator.MoveMouseTo(w1->bounds().CenterPoint());
  EXPECT_TRUE(HasPendingShow());
  EXPECT_TRUE(IsShowing());

  // Force a show now.
  ShowNow();
  EXPECT_FALSE(HasPendingShow());
  EXPECT_TRUE(IsShowing());

  // Move the mouse over the resize widget.
  ASSERT_TRUE(resize_widget());
  gfx::Rect bounds(resize_widget()->GetWindowBoundsInScreen());
  generator.MoveMouseTo(bounds.x() + 1, bounds.y() + 1);
  EXPECT_FALSE(HasPendingShow());
  EXPECT_TRUE(IsShowing());

  // Move the resize widget
  generator.PressLeftButton();
  generator.MoveMouseTo(bounds.x() + 10, bounds.y() + 10);

  // Delete w2.
  w2.reset();
  EXPECT_TRUE(resize_widget() == NULL);
  EXPECT_FALSE(HasPendingShow());
  EXPECT_FALSE(IsShowing());
  EXPECT_FALSE(HasTarget(w1.get()));
}

// Tests resizing.
TEST_F(MultiWindowResizeControllerTest, Drag) {
  aura::test::TestWindowDelegate delegate1;
  std::unique_ptr<aura::Window> w1(
      CreateTestWindow(&delegate1, gfx::Rect(0, 0, 100, 100)));
  delegate1.set_window_component(HTRIGHT);
  aura::test::TestWindowDelegate delegate2;
  std::unique_ptr<aura::Window> w2(
      CreateTestWindow(&delegate2, gfx::Rect(100, 0, 100, 100)));
  delegate2.set_window_component(HTRIGHT);
  ui::test::EventGenerator generator(w1->GetRootWindow());
  generator.MoveMouseTo(w1->bounds().CenterPoint());
  EXPECT_TRUE(HasPendingShow());
  EXPECT_TRUE(IsShowing());

  // Force a show now.
  ShowNow();
  EXPECT_FALSE(HasPendingShow());
  EXPECT_TRUE(IsShowing());

  // Move the mouse over the resize widget.
  ASSERT_TRUE(resize_widget());
  gfx::Rect bounds(resize_widget()->GetWindowBoundsInScreen());
  generator.MoveMouseTo(bounds.x() + 1, bounds.y() + 1);
  EXPECT_FALSE(HasPendingShow());
  EXPECT_TRUE(IsShowing());

  // Move the resize widget
  generator.PressLeftButton();
  generator.MoveMouseTo(bounds.x() + 11, bounds.y() + 10);
  generator.ReleaseLeftButton();

  EXPECT_TRUE(resize_widget());
  EXPECT_FALSE(HasPendingShow());
  EXPECT_TRUE(IsShowing());
  EXPECT_EQ("0,0 110x100", w1->bounds().ToString());
  EXPECT_EQ("110,0 100x100", w2->bounds().ToString());
}

// Makes sure three windows are picked up.
TEST_F(MultiWindowResizeControllerTest, Three) {
  aura::test::TestWindowDelegate delegate1;
  std::unique_ptr<aura::Window> w1(
      CreateTestWindow(&delegate1, gfx::Rect(0, 0, 100, 100)));
  delegate1.set_window_component(HTRIGHT);
  aura::test::TestWindowDelegate delegate2;
  std::unique_ptr<aura::Window> w2(
      CreateTestWindow(&delegate2, gfx::Rect(100, 0, 100, 100)));
  delegate2.set_window_component(HTRIGHT);
  aura::test::TestWindowDelegate delegate3;
  std::unique_ptr<aura::Window> w3(
      CreateTestWindow(&delegate3, gfx::Rect(200, 0, 100, 100)));
  delegate3.set_window_component(HTRIGHT);

  ui::test::EventGenerator generator(w1->GetRootWindow());
  generator.MoveMouseTo(w1->bounds().CenterPoint());
  EXPECT_TRUE(HasPendingShow());
  EXPECT_TRUE(IsShowing());
  EXPECT_FALSE(HasTarget(w3.get()));

  ShowNow();
  EXPECT_FALSE(HasPendingShow());
  EXPECT_TRUE(IsShowing());

  // w3 should be picked up when resize is started.
  gfx::Rect bounds(resize_widget()->GetWindowBoundsInScreen());
  generator.MoveMouseTo(bounds.x() + 1, bounds.y() + 1);
  generator.PressLeftButton();
  generator.MoveMouseTo(bounds.x() + 11, bounds.y() + 10);

  EXPECT_TRUE(HasTarget(w3.get()));

  // Release the mouse. The resizer should still be visible and a subsequent
  // press should not trigger a DCHECK.
  generator.ReleaseLeftButton();
  EXPECT_TRUE(IsShowing());
  generator.PressLeftButton();
}

// Tests that clicking outside of the resize handle dismisses it.
TEST_F(MultiWindowResizeControllerTest, ClickOutside) {
  aura::test::TestWindowDelegate delegate1;
  std::unique_ptr<aura::Window> w1(
      CreateTestWindow(&delegate1, gfx::Rect(0, 0, 100, 100)));
  delegate1.set_window_component(HTRIGHT);
  aura::test::TestWindowDelegate delegate2;
  std::unique_ptr<aura::Window> w2(
      CreateTestWindow(&delegate2, gfx::Rect(100, 0, 100, 100)));
  delegate2.set_window_component(HTLEFT);

  ui::test::EventGenerator& generator(GetEventGenerator());
  gfx::Point w1_center_in_screen = w1->GetBoundsInScreen().CenterPoint();
  generator.MoveMouseTo(w1_center_in_screen);
  EXPECT_TRUE(HasPendingShow());
  EXPECT_TRUE(IsShowing());
  ShowNow();
  EXPECT_TRUE(IsShowing());

  gfx::Rect resize_widget_bounds_in_screen =
      resize_widget()->GetWindowBoundsInScreen();

  // Clicking on the resize handle should not do anything.
  generator.MoveMouseTo(resize_widget_bounds_in_screen.CenterPoint());
  generator.ClickLeftButton();
  EXPECT_TRUE(IsShowing());

  // Clicking outside the resize handle should immediately hide the resize
  // handle.
  EXPECT_FALSE(resize_widget_bounds_in_screen.Contains(w1_center_in_screen));
  generator.MoveMouseTo(w1_center_in_screen);
  generator.ClickLeftButton();
  EXPECT_FALSE(IsShowing());
}

}  // namespace ash
