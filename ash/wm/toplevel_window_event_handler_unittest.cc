// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/toplevel_window_event_handler.h"

#include "ash/public/cpp/config.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/root_window_controller.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "ash/wm/resize_shadow.h"
#include "ash/wm/resize_shadow_controller.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm/wm_event.h"
#include "ash/wm/workspace_controller.h"
#include "base/compiler_specific.h"
#include "base/threading/thread_task_runner_handle.h"
#include "services/ui/public/interfaces/window_manager_constants.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/capture_client.h"
#include "ui/aura/test/aura_test_base.h"
#include "ui/aura/test/test_window_delegate.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/base/hit_test.h"
#include "ui/display/display_layout_builder.h"
#include "ui/display/manager/display_manager.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/test/event_generator.h"
#include "ui/wm/core/window_util.h"
#include "ui/wm/public/window_move_client.h"

namespace ash {
namespace test {

namespace {

// A simple window delegate that returns the specified hit-test code when
// requested and applies a minimum size constraint if there is one.
class TestWindowDelegate : public aura::test::TestWindowDelegate {
 public:
  explicit TestWindowDelegate(int hittest_code) {
    set_window_component(hittest_code);
  }
  ~TestWindowDelegate() override {}

 private:
  // Overridden from aura::Test::TestWindowDelegate:
  void OnWindowDestroyed(aura::Window* window) override { delete this; }

  DISALLOW_COPY_AND_ASSIGN(TestWindowDelegate);
};

class ToplevelWindowEventHandlerTest : public AshTestBase {
 public:
  ToplevelWindowEventHandlerTest() {}
  ~ToplevelWindowEventHandlerTest() override {}

 protected:
  aura::Window* CreateWindow(int hittest_code) {
    TestWindowDelegate* d1 = new TestWindowDelegate(hittest_code);
    aura::Window* w1 = new aura::Window(d1);
    w1->SetType(ui::wm::WINDOW_TYPE_NORMAL);
    w1->set_id(1);
    w1->Init(ui::LAYER_TEXTURED);
    aura::Window* parent = Shell::GetContainer(Shell::GetPrimaryRootWindow(),
                                               kShellWindowId_DefaultContainer);
    parent->AddChild(w1);
    w1->SetBounds(gfx::Rect(0, 0, 100, 100));
    w1->Show();
    return w1;
  }

  void DragFromCenterBy(aura::Window* window, int dx, int dy) {
    ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(), window);
    generator.DragMouseBy(dx, dy);
  }

  void TouchDragFromCenterBy(aura::Window* window, int dx, int dy) {
    ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(), window);
    generator.PressMoveAndReleaseTouchBy(dx, dy);
  }

  std::unique_ptr<ToplevelWindowEventHandler> handler_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ToplevelWindowEventHandlerTest);
};
}

TEST_F(ToplevelWindowEventHandlerTest, Caption) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTCAPTION));
  gfx::Size size = w1->bounds().size();
  DragFromCenterBy(w1.get(), 100, 100);
  // Position should have been offset by 100,100.
  EXPECT_EQ("100,100", w1->bounds().origin().ToString());
  // Size should not have.
  EXPECT_EQ(size.ToString(), w1->bounds().size().ToString());

  TouchDragFromCenterBy(w1.get(), 100, 100);
  // Position should have been offset by 100,100.
  EXPECT_EQ("200,200", w1->bounds().origin().ToString());
  // Size should not have.
  EXPECT_EQ(size.ToString(), w1->bounds().size().ToString());
}

namespace {

void ContinueAndCompleteDrag(ui::test::EventGenerator* generator,
                             wm::WindowState* window_state,
                             aura::Window* window) {
  ASSERT_TRUE(window->HasCapture());
  ASSERT_FALSE(window_state->window_position_managed());
  generator->DragMouseBy(100, 100);
  generator->ReleaseLeftButton();
}

}  // namespace

// Tests dragging restores expected window position auto manage property
// correctly.
TEST_F(ToplevelWindowEventHandlerTest, WindowPositionAutoManagement) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTNOWHERE));
  const gfx::Size size = w1->bounds().size();
  wm::WindowState* window_state = ash::wm::GetWindowState(w1.get());
  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(), w1.get());

  // Explicitly enable window position auto management, and expect it to be
  // restored after drag completes.
  window_state->set_window_position_managed(true);
  generator.PressLeftButton();
  aura::client::WindowMoveClient* move_client =
      aura::client::GetWindowMoveClient(w1->GetRootWindow());
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&ContinueAndCompleteDrag, base::Unretained(&generator),
                 base::Unretained(window_state), base::Unretained(w1.get())));
  EXPECT_EQ(aura::client::MOVE_SUCCESSFUL,
            move_client->RunMoveLoop(w1.get(), gfx::Vector2d(100, 100),
                                     aura::client::WINDOW_MOVE_SOURCE_MOUSE));
  // Window position auto manage property should be restored to true.
  EXPECT_TRUE(window_state->window_position_managed());
  // Position should have been offset by 100,100.
  EXPECT_EQ("100,100", w1->bounds().origin().ToString());
  // Size should remain the same.
  EXPECT_EQ(size.ToString(), w1->bounds().size().ToString());

  // Explicitly disable window position auto management, and expect it to be
  // restored after drag completes.
  window_state->set_window_position_managed(false);
  generator.PressLeftButton();
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&ContinueAndCompleteDrag, base::Unretained(&generator),
                 base::Unretained(window_state), base::Unretained(w1.get())));
  EXPECT_EQ(aura::client::MOVE_SUCCESSFUL,
            move_client->RunMoveLoop(w1.get(), gfx::Vector2d(100, 100),
                                     aura::client::WINDOW_MOVE_SOURCE_MOUSE));
  // Window position auto manage property should be restored to true.
  EXPECT_FALSE(window_state->window_position_managed());
  // Position should have been offset by 100,100.
  EXPECT_EQ("200,200", w1->bounds().origin().ToString());
  // Size should remain the same.
  EXPECT_EQ(size.ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, BottomRight) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTBOTTOMRIGHT));
  gfx::Point position = w1->bounds().origin();
  DragFromCenterBy(w1.get(), 100, 100);
  // Position should not have changed.
  EXPECT_EQ(position.ToString(), w1->bounds().origin().ToString());
  // Size should have increased by 100,100.
  EXPECT_EQ(gfx::Size(200, 200).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, GrowBox) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTGROWBOX));
  TestWindowDelegate* window_delegate =
      static_cast<TestWindowDelegate*>(w1->delegate());
  window_delegate->set_minimum_size(gfx::Size(40, 40));

  gfx::Point position = w1->bounds().origin();
  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow());
  generator.MoveMouseToCenterOf(w1.get());
  generator.DragMouseBy(100, 100);
  // Position should not have changed.
  EXPECT_EQ(position.ToString(), w1->bounds().origin().ToString());
  // Size should have increased by 100,100.
  EXPECT_EQ(gfx::Size(200, 200).ToString(), w1->bounds().size().ToString());

  // Shrink the wnidow by (-100, -100).
  generator.DragMouseBy(-100, -100);
  // Position should not have changed.
  EXPECT_EQ(position.ToString(), w1->bounds().origin().ToString());
  // Size should have decreased by 100,100.
  EXPECT_EQ(gfx::Size(100, 100).ToString(), w1->bounds().size().ToString());

  // Enforce minimum size.
  generator.DragMouseBy(-60, -60);
  EXPECT_EQ(position.ToString(), w1->bounds().origin().ToString());
  EXPECT_EQ(gfx::Size(40, 40).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, Right) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTRIGHT));
  gfx::Point position = w1->bounds().origin();
  DragFromCenterBy(w1.get(), 100, 100);
  // Position should not have changed.
  EXPECT_EQ(position.ToString(), w1->bounds().origin().ToString());
  // Size should have increased by 100,0.
  EXPECT_EQ(gfx::Size(200, 100).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, Bottom) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTBOTTOM));
  gfx::Point position = w1->bounds().origin();
  DragFromCenterBy(w1.get(), 100, 100);
  // Position should not have changed.
  EXPECT_EQ(position.ToString(), w1->bounds().origin().ToString());
  // Size should have increased by 0,100.
  EXPECT_EQ(gfx::Size(100, 200).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, TopRight) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTTOPRIGHT));
  DragFromCenterBy(w1.get(), -50, 50);
  // Position should have been offset by 0,50.
  EXPECT_EQ(gfx::Point(0, 50).ToString(), w1->bounds().origin().ToString());
  // Size should have decreased by 50,50.
  EXPECT_EQ(gfx::Size(50, 50).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, Top) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTTOP));
  DragFromCenterBy(w1.get(), 50, 50);
  // Position should have been offset by 0,50.
  EXPECT_EQ(gfx::Point(0, 50).ToString(), w1->bounds().origin().ToString());
  // Size should have decreased by 0,50.
  EXPECT_EQ(gfx::Size(100, 50).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, Left) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTLEFT));
  DragFromCenterBy(w1.get(), 50, 50);
  // Position should have been offset by 50,0.
  EXPECT_EQ(gfx::Point(50, 0).ToString(), w1->bounds().origin().ToString());
  // Size should have decreased by 50,0.
  EXPECT_EQ(gfx::Size(50, 100).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, BottomLeft) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTBOTTOMLEFT));
  DragFromCenterBy(w1.get(), 50, -50);
  // Position should have been offset by 50,0.
  EXPECT_EQ(gfx::Point(50, 0).ToString(), w1->bounds().origin().ToString());
  // Size should have decreased by 50,50.
  EXPECT_EQ(gfx::Size(50, 50).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, TopLeft) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTTOPLEFT));
  DragFromCenterBy(w1.get(), 50, 50);
  // Position should have been offset by 50,50.
  EXPECT_EQ(gfx::Point(50, 50).ToString(), w1->bounds().origin().ToString());
  // Size should have decreased by 50,50.
  EXPECT_EQ(gfx::Size(50, 50).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, Client) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTCLIENT));
  gfx::Rect bounds = w1->bounds();
  DragFromCenterBy(w1.get(), 100, 100);
  // Neither position nor size should have changed.
  EXPECT_EQ(bounds.ToString(), w1->bounds().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, LeftPastMinimum) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTLEFT));
  TestWindowDelegate* window_delegate =
      static_cast<TestWindowDelegate*>(w1->delegate());
  window_delegate->set_minimum_size(gfx::Size(40, 40));

  // Simulate a large left-to-right drag.  Window width should be clamped to
  // minimum and position change should be limited as well.
  DragFromCenterBy(w1.get(), 333, 0);
  EXPECT_EQ(gfx::Point(60, 0).ToString(), w1->bounds().origin().ToString());
  EXPECT_EQ(gfx::Size(40, 100).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, RightPastMinimum) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTRIGHT));
  TestWindowDelegate* window_delegate =
      static_cast<TestWindowDelegate*>(w1->delegate());
  window_delegate->set_minimum_size(gfx::Size(40, 40));
  gfx::Point position = w1->bounds().origin();

  // Simulate a large right-to-left drag.  Window width should be clamped to
  // minimum and position should not change.
  DragFromCenterBy(w1.get(), -333, 0);
  EXPECT_EQ(position.ToString(), w1->bounds().origin().ToString());
  EXPECT_EQ(gfx::Size(40, 100).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, TopLeftPastMinimum) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTTOPLEFT));
  TestWindowDelegate* window_delegate =
      static_cast<TestWindowDelegate*>(w1->delegate());
  window_delegate->set_minimum_size(gfx::Size(40, 40));

  // Simulate a large top-left to bottom-right drag.  Window width should be
  // clamped to minimum and position should be limited.
  DragFromCenterBy(w1.get(), 333, 444);
  EXPECT_EQ(gfx::Point(60, 60).ToString(), w1->bounds().origin().ToString());
  EXPECT_EQ(gfx::Size(40, 40).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, TopRightPastMinimum) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTTOPRIGHT));
  TestWindowDelegate* window_delegate =
      static_cast<TestWindowDelegate*>(w1->delegate());
  window_delegate->set_minimum_size(gfx::Size(40, 40));

  // Simulate a large top-right to bottom-left drag.  Window size should be
  // clamped to minimum, x position should not change, and y position should
  // be clamped.
  DragFromCenterBy(w1.get(), -333, 444);
  EXPECT_EQ(gfx::Point(0, 60).ToString(), w1->bounds().origin().ToString());
  EXPECT_EQ(gfx::Size(40, 40).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, BottomLeftPastMinimum) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTBOTTOMLEFT));
  TestWindowDelegate* window_delegate =
      static_cast<TestWindowDelegate*>(w1->delegate());
  window_delegate->set_minimum_size(gfx::Size(40, 40));

  // Simulate a large bottom-left to top-right drag.  Window size should be
  // clamped to minimum, x position should be clamped, and y position should
  // not change.
  DragFromCenterBy(w1.get(), 333, -444);
  EXPECT_EQ(gfx::Point(60, 0).ToString(), w1->bounds().origin().ToString());
  EXPECT_EQ(gfx::Size(40, 40).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, BottomRightPastMinimum) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTBOTTOMRIGHT));
  TestWindowDelegate* window_delegate =
      static_cast<TestWindowDelegate*>(w1->delegate());
  window_delegate->set_minimum_size(gfx::Size(40, 40));
  gfx::Point position = w1->bounds().origin();

  // Simulate a large bottom-right to top-left drag.  Window size should be
  // clamped to minimum and position should not change.
  DragFromCenterBy(w1.get(), -333, -444);
  EXPECT_EQ(position.ToString(), w1->bounds().origin().ToString());
  EXPECT_EQ(gfx::Size(40, 40).ToString(), w1->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, BottomRightWorkArea) {
  std::unique_ptr<aura::Window> target(CreateWindow(HTBOTTOMRIGHT));
  gfx::Rect work_area = display::Screen::GetScreen()
                            ->GetDisplayNearestWindow(target.get())
                            .work_area();
  gfx::Point position = target->bounds().origin();
  // Drag further than work_area bottom.
  DragFromCenterBy(target.get(), 100, work_area.height());
  // Position should not have changed.
  EXPECT_EQ(position.ToString(), target->bounds().origin().ToString());
  // Size should have increased by 100, work_area.height() - target->bounds.y()
  EXPECT_EQ(
      gfx::Size(200, work_area.height() - target->bounds().y()).ToString(),
      target->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, BottomLeftWorkArea) {
  std::unique_ptr<aura::Window> target(CreateWindow(HTBOTTOMLEFT));
  gfx::Rect work_area = display::Screen::GetScreen()
                            ->GetDisplayNearestWindow(target.get())
                            .work_area();
  gfx::Point position = target->bounds().origin();
  // Drag further than work_area bottom.
  DragFromCenterBy(target.get(), -30, work_area.height());
  // origin is now at 70, 100.
  EXPECT_EQ(position.x() - 30, target->bounds().x());
  EXPECT_EQ(position.y(), target->bounds().y());
  // Size should have increased by 30, work_area.height() - target->bounds.y()
  EXPECT_EQ(
      gfx::Size(130, work_area.height() - target->bounds().y()).ToString(),
      target->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, BottomWorkArea) {
  std::unique_ptr<aura::Window> target(CreateWindow(HTBOTTOM));
  gfx::Rect work_area = display::Screen::GetScreen()
                            ->GetDisplayNearestWindow(target.get())
                            .work_area();
  gfx::Point position = target->bounds().origin();
  // Drag further than work_area bottom.
  DragFromCenterBy(target.get(), 0, work_area.height());
  // Position should not have changed.
  EXPECT_EQ(position.ToString(), target->bounds().origin().ToString());
  // Size should have increased by 0, work_area.height() - target->bounds.y()
  EXPECT_EQ(
      gfx::Size(100, work_area.height() - target->bounds().y()).ToString(),
      target->bounds().size().ToString());
}

TEST_F(ToplevelWindowEventHandlerTest, DontDragIfModalChild) {
  std::unique_ptr<aura::Window> w1(CreateWindow(HTCAPTION));
  std::unique_ptr<aura::Window> w2(CreateWindow(HTCAPTION));
  w2->SetBounds(gfx::Rect(100, 0, 100, 100));
  w2->SetProperty(aura::client::kModalKey, ui::MODAL_TYPE_WINDOW);
  ::wm::AddTransientChild(w1.get(), w2.get());
  gfx::Size size = w1->bounds().size();

  // Attempt to drag w1, position and size should not change because w1 has a
  // modal child.
  DragFromCenterBy(w1.get(), 100, 100);
  EXPECT_EQ("0,0", w1->bounds().origin().ToString());
  EXPECT_EQ(size.ToString(), w1->bounds().size().ToString());

  TouchDragFromCenterBy(w1.get(), 100, 100);
  EXPECT_EQ("0,0", w1->bounds().origin().ToString());
  EXPECT_EQ(size.ToString(), w1->bounds().size().ToString());
}

// Verifies we don't let windows drag to a -y location.
TEST_F(ToplevelWindowEventHandlerTest, DontDragToNegativeY) {
  std::unique_ptr<aura::Window> target(CreateWindow(HTTOP));
  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     target.get());
  generator.MoveMouseTo(0, 5);
  generator.DragMouseBy(0, -5);
  // The y location and height should not have changed.
  EXPECT_EQ(0, target->bounds().y());
  EXPECT_EQ(100, target->bounds().height());
}

// Verifies we don't let windows go bigger than the display width.
TEST_F(ToplevelWindowEventHandlerTest, DontGotWiderThanScreen) {
  std::unique_ptr<aura::Window> target(CreateWindow(HTRIGHT));
  gfx::Rect work_area = display::Screen::GetScreen()
                            ->GetDisplayNearestWindow(target.get())
                            .bounds();
  DragFromCenterBy(target.get(), work_area.width() * 2, 0);
  // The y location and height should not have changed.
  EXPECT_EQ(work_area.width(), target->bounds().width());
}

// Verifies that touch-gestures drag the window correctly.
TEST_F(ToplevelWindowEventHandlerTest, GestureDrag) {
  std::unique_ptr<aura::Window> target(CreateTestWindowInShellWithDelegate(
      new TestWindowDelegate(HTCAPTION), 0, gfx::Rect(0, 0, 100, 100)));
  wm::WindowState* window_state = wm::GetWindowState(target.get());
  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     target.get());
  gfx::Rect old_bounds = target->bounds();
  gfx::Point location(5, 5);
  target->SetProperty(aura::client::kResizeBehaviorKey,
                      ui::mojom::kResizeBehaviorCanResize |
                          ui::mojom::kResizeBehaviorCanMaximize |
                          ui::mojom::kResizeBehaviorCanMinimize);

  gfx::Point end = location;

  // Snap right;
  end.Offset(100, 0);
  generator.GestureScrollSequence(location, end,
                                  base::TimeDelta::FromMilliseconds(5), 10);
  RunAllPendingInMessageLoop();

  // Verify that the window has moved after the gesture.
  EXPECT_NE(old_bounds.ToString(), target->bounds().ToString());
  EXPECT_EQ(wm::WINDOW_STATE_TYPE_RIGHT_SNAPPED, window_state->GetStateType());

  old_bounds = target->bounds();

  // Snap left.
  end = location = target->GetBoundsInRootWindow().CenterPoint();
  end.Offset(-100, 0);
  generator.GestureScrollSequence(location, end,
                                  base::TimeDelta::FromMilliseconds(5), 10);
  RunAllPendingInMessageLoop();

  EXPECT_NE(old_bounds.ToString(), target->bounds().ToString());
  EXPECT_EQ(wm::WINDOW_STATE_TYPE_LEFT_SNAPPED, window_state->GetStateType());

  gfx::Rect bounds_before_maximization = target->bounds();
  bounds_before_maximization.Offset(0, 100);
  target->SetBounds(bounds_before_maximization);
  old_bounds = target->bounds();

  // Maximize.
  end = location = target->GetBoundsInRootWindow().CenterPoint();
  end.Offset(0, -100);
  generator.GestureScrollSequence(location, end,
                                  base::TimeDelta::FromMilliseconds(5), 10);
  RunAllPendingInMessageLoop();

  EXPECT_NE(old_bounds.ToString(), target->bounds().ToString());
  EXPECT_TRUE(window_state->IsMaximized());
  EXPECT_EQ(old_bounds.ToString(),
            window_state->GetRestoreBoundsInScreen().ToString());

  window_state->Restore();
  target->SetBounds(old_bounds);

  // Minimize.
  end = location = target->GetBoundsInRootWindow().CenterPoint();
  end.Offset(0, 100);
  generator.GestureScrollSequence(location, end,
                                  base::TimeDelta::FromMilliseconds(5), 10);
  RunAllPendingInMessageLoop();
  EXPECT_NE(old_bounds.ToString(), target->bounds().ToString());
  EXPECT_TRUE(window_state->IsMinimized());
  EXPECT_TRUE(window_state->unminimize_to_restore_bounds());
  EXPECT_EQ(old_bounds.ToString(),
            window_state->GetRestoreBoundsInScreen().ToString());
}

// Tests that a gesture cannot minimize an unminimizeable window.
TEST_F(ToplevelWindowEventHandlerTest,
       GestureAttemptMinimizeUnminimizeableWindow) {
  std::unique_ptr<aura::Window> target(CreateWindow(HTCAPTION));
  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     target.get());
  gfx::Point location(5, 5);
  target->SetProperty(aura::client::kResizeBehaviorKey,
                      ui::mojom::kResizeBehaviorCanMaximize);

  gfx::Point end = location;
  end.Offset(0, 100);
  generator.GestureScrollSequence(location, end,
                                  base::TimeDelta::FromMilliseconds(5), 10);
  RunAllPendingInMessageLoop();
  EXPECT_FALSE(wm::GetWindowState(target.get())->IsMinimized());
}

TEST_F(ToplevelWindowEventHandlerTest, GestureDragToRestore) {
  std::unique_ptr<aura::Window> window(CreateTestWindowInShellWithDelegate(
      new TestWindowDelegate(HTCAPTION), 0, gfx::Rect(10, 20, 30, 40)));
  window->Show();
  wm::WindowState* window_state = wm::GetWindowState(window.get());
  window_state->Activate();

  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     window.get());
  gfx::Rect old_bounds = window->bounds();
  gfx::Point location, end;
  end = location = window->GetBoundsInRootWindow().CenterPoint();
  end.Offset(0, 100);
  generator.GestureScrollSequence(location, end,
                                  base::TimeDelta::FromMilliseconds(5), 10);
  RunAllPendingInMessageLoop();
  EXPECT_NE(old_bounds.ToString(), window->bounds().ToString());
  EXPECT_TRUE(window_state->IsMinimized());
  EXPECT_TRUE(window_state->unminimize_to_restore_bounds());
  EXPECT_EQ(old_bounds.ToString(),
            window_state->GetRestoreBoundsInScreen().ToString());
}

// Tests that an unresizable window cannot be dragged or snapped using gestures.
TEST_F(ToplevelWindowEventHandlerTest, GestureDragForUnresizableWindow) {
  std::unique_ptr<aura::Window> target(CreateWindow(HTCAPTION));
  wm::WindowState* window_state = wm::GetWindowState(target.get());

  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     target.get());
  gfx::Rect old_bounds = target->bounds();
  gfx::Point location(5, 5);

  target->SetProperty(aura::client::kResizeBehaviorKey,
                      ui::mojom::kResizeBehaviorNone);

  gfx::Point end = location;

  // Try to snap right. The window is not resizable. So it should not snap.
  end.Offset(100, 0);
  generator.GestureScrollSequence(location, end,
                                  base::TimeDelta::FromMilliseconds(5), 10);
  RunAllPendingInMessageLoop();

  // Verify that the window has moved after the gesture.
  gfx::Rect expected_bounds(old_bounds);
  expected_bounds.Offset(gfx::Vector2d(100, 0));
  EXPECT_EQ(expected_bounds.ToString(), target->bounds().ToString());

  // Verify that the window did not snap left.
  EXPECT_TRUE(window_state->IsNormalStateType());

  old_bounds = target->bounds();

  // Try to snap left. It should not snap.
  end = location = target->GetBoundsInRootWindow().CenterPoint();
  end.Offset(-100, 0);
  generator.GestureScrollSequence(location, end,
                                  base::TimeDelta::FromMilliseconds(5), 10);
  RunAllPendingInMessageLoop();

  // Verify that the window has moved after the gesture.
  expected_bounds = old_bounds;
  expected_bounds.Offset(gfx::Vector2d(-100, 0));
  EXPECT_EQ(expected_bounds.ToString(), target->bounds().ToString());

  // Verify that the window did not snap left.
  EXPECT_TRUE(window_state->IsNormalStateType());
}

// Tests that dragging multiple windows at the same time is not allowed.
TEST_F(ToplevelWindowEventHandlerTest, GestureDragMultipleWindows) {
  std::unique_ptr<aura::Window> target(CreateTestWindowInShellWithDelegate(
      new TestWindowDelegate(HTCAPTION), 0, gfx::Rect(0, 0, 100, 100)));
  std::unique_ptr<aura::Window> notmoved(CreateTestWindowInShellWithDelegate(
      new TestWindowDelegate(HTCAPTION), 1, gfx::Rect(100, 0, 100, 100)));

  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     target.get());
  gfx::Point location(5, 5);

  // Send some touch events to start dragging |target|.
  generator.MoveTouch(location);
  generator.PressTouch();
  location.Offset(40, 5);
  generator.MoveTouch(location);

  // Try to drag |notmoved| window. This should not move the window.
  {
    gfx::Rect bounds = notmoved->bounds();
    ui::test::EventGenerator gen(Shell::GetPrimaryRootWindow(), notmoved.get());
    gfx::Point start = notmoved->bounds().origin() + gfx::Vector2d(10, 10);
    gfx::Point end = start + gfx::Vector2d(100, 10);
    gen.GestureScrollSequence(start, end, base::TimeDelta::FromMilliseconds(10),
                              10);
    EXPECT_EQ(bounds.ToString(), notmoved->bounds().ToString());
  }
}

// Verifies pressing escape resets the bounds to the original bounds.
TEST_F(ToplevelWindowEventHandlerTest, EscapeReverts) {
  std::unique_ptr<aura::Window> target(CreateWindow(HTBOTTOMRIGHT));
  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     target.get());
  generator.PressLeftButton();
  generator.MoveMouseBy(10, 11);

  // Execute any scheduled draws so that pending mouse events are processed.
  RunAllPendingInMessageLoop();

  EXPECT_EQ("0,0 110x111", target->bounds().ToString());
  generator.PressKey(ui::VKEY_ESCAPE, 0);
  generator.ReleaseKey(ui::VKEY_ESCAPE, 0);
  EXPECT_EQ("0,0 100x100", target->bounds().ToString());
}

// Verifies window minimization/maximization completes drag.
TEST_F(ToplevelWindowEventHandlerTest, MinimizeMaximizeCompletes) {
  // Once window is minimized, window dragging completes.
  {
    std::unique_ptr<aura::Window> target(CreateWindow(HTCAPTION));
    target->Focus();
    ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                       target.get());
    generator.PressLeftButton();
    generator.MoveMouseBy(10, 11);
    RunAllPendingInMessageLoop();
    EXPECT_EQ("10,11 100x100", target->bounds().ToString());
    wm::WindowState* window_state = wm::GetWindowState(target.get());
    window_state->Minimize();
    window_state->Restore();

    generator.PressLeftButton();
    generator.MoveMouseBy(10, 11);
    RunAllPendingInMessageLoop();
    EXPECT_EQ("10,11 100x100", target->bounds().ToString());
  }

  // Once window is maximized, window dragging completes.
  {
    std::unique_ptr<aura::Window> target(CreateWindow(HTCAPTION));
    target->Focus();
    ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                       target.get());
    generator.PressLeftButton();
    generator.MoveMouseBy(10, 11);
    RunAllPendingInMessageLoop();
    EXPECT_EQ("10,11 100x100", target->bounds().ToString());
    wm::WindowState* window_state = wm::GetWindowState(target.get());
    window_state->Maximize();
    window_state->Restore();

    generator.PressLeftButton();
    generator.MoveMouseBy(10, 11);
    RunAllPendingInMessageLoop();
    EXPECT_EQ("10,11 100x100", target->bounds().ToString());
  }
}

// Verifies that a drag cannot be started via
// aura::client::WindowMoveClient::RunMoveLoop() while another drag is already
// in progress.
TEST_F(ToplevelWindowEventHandlerTest, RunMoveLoopFailsDuringInProgressDrag) {
  std::unique_ptr<aura::Window> window1(CreateWindow(HTCAPTION));
  EXPECT_EQ("0,0 100x100", window1->bounds().ToString());
  std::unique_ptr<aura::Window> window2(CreateWindow(HTCAPTION));

  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     window1.get());
  window1->Focus();
  generator.PressLeftButton();
  generator.MoveMouseBy(10, 11);
  EXPECT_EQ("10,11 100x100", window1->bounds().ToString());

  aura::client::WindowMoveClient* move_client =
      aura::client::GetWindowMoveClient(window2->GetRootWindow());
  EXPECT_EQ(aura::client::MOVE_CANCELED,
            move_client->RunMoveLoop(window2.get(), gfx::Vector2d(),
                                     aura::client::WINDOW_MOVE_SOURCE_MOUSE));

  generator.ReleaseLeftButton();
  EXPECT_EQ("10,11 100x100", window1->bounds().ToString());
}

namespace {

void SendMouseReleaseAndReleaseCapture(ui::test::EventGenerator* generator,
                                       aura::Window* window) {
  generator->ReleaseLeftButton();
  window->ReleaseCapture();
}

}  // namespace

// Test that a drag is successful even if ET_MOUSE_CAPTURE_CHANGED is sent
// immediately after the mouse release. views::Widget has this behavior.
TEST_F(ToplevelWindowEventHandlerTest, CaptureLossAfterMouseRelease) {
  std::unique_ptr<aura::Window> window(CreateWindow(HTNOWHERE));
  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     window.get());
  generator.PressLeftButton();
  window->SetCapture();

  aura::client::WindowMoveClient* move_client =
      aura::client::GetWindowMoveClient(window->GetRootWindow());
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&SendMouseReleaseAndReleaseCapture,
                 base::Unretained(&generator), base::Unretained(window.get())));
  EXPECT_EQ(aura::client::MOVE_SUCCESSFUL,
            move_client->RunMoveLoop(window.get(), gfx::Vector2d(),
                                     aura::client::WINDOW_MOVE_SOURCE_MOUSE));
}

namespace {

// Checks that |window| has capture and releases capture.
void CheckHasCaptureAndReleaseCapture(aura::Window* window) {
  ASSERT_TRUE(window->HasCapture());
  window->ReleaseCapture();
}

}  // namespace

// Test that releasing capture completes an in-progress gesture drag.
TEST_F(ToplevelWindowEventHandlerTest, GestureDragCaptureLoss) {
  std::unique_ptr<aura::Window> window(CreateWindow(HTNOWHERE));
  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(),
                                     window.get());

  aura::client::WindowMoveClient* move_client =
      aura::client::GetWindowMoveClient(window->GetRootWindow());
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&CheckHasCaptureAndReleaseCapture,
                            base::Unretained(window.get())));
  EXPECT_EQ(aura::client::MOVE_SUCCESSFUL,
            move_client->RunMoveLoop(window.get(), gfx::Vector2d(),
                                     aura::client::WINDOW_MOVE_SOURCE_TOUCH));
}

// Tests that dragging a snapped window to another display updates the window's
// bounds correctly.
TEST_F(ToplevelWindowEventHandlerTest, DragSnappedWindowToExternalDisplay) {
  // TODO: SetLayoutForCurrentDisplays() needs to ported to mash.
  // http://crbug.com/698043.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  UpdateDisplay("940x550,940x550");
  int64_t primary_id = display::Screen::GetScreen()->GetPrimaryDisplay().id();
  int64_t secondary_id = display_manager()->GetSecondaryDisplay().id();
  display::DisplayLayoutBuilder builder(primary_id);
  builder.SetSecondaryPlacement(secondary_id, display::DisplayPlacement::TOP,
                                0);
  display_manager()->SetLayoutForCurrentDisplays(builder.Build());

  const gfx::Size initial_window_size(330, 230);
  std::unique_ptr<aura::Window> w1(CreateTestWindowInShellWithDelegateAndType(
      new TestWindowDelegate(HTCAPTION), ui::wm::WINDOW_TYPE_NORMAL, 0,
      gfx::Rect(initial_window_size)));

  // Snap the window to the right.
  wm::WindowState* window_state = wm::GetWindowState(w1.get());
  ASSERT_TRUE(window_state->CanSnap());
  const wm::WMEvent event(wm::WM_EVENT_CYCLE_SNAP_RIGHT);
  window_state->OnWMEvent(&event);
  ASSERT_TRUE(window_state->IsSnapped());

  // Drag the window to the secondary display.
  ui::test::EventGenerator generator(Shell::GetPrimaryRootWindow(), w1.get());
  generator.DragMouseTo(472, -462);

  // Expect the window is no longer snapped and its size was restored to the
  // initial size.
  EXPECT_FALSE(window_state->IsSnapped());
  EXPECT_EQ(initial_window_size.ToString(), w1->bounds().size().ToString());

  // The window is now fully contained in the secondary display.
  EXPECT_TRUE(display_manager()->GetSecondaryDisplay().bounds().Contains(
      w1->GetBoundsInScreen()));
}

// Showing the resize shadows when the mouse is over the window edges is tested
// in resize_shadow_and_cursor_test.cc

}  // namespace test
}  // namespace ash
