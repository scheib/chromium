// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/accelerators/accelerator_controller.h"

#include "ash/accelerators/accelerator_table.h"
#include "ash/accessibility_delegate.h"
#include "ash/accessibility_types.h"
#include "ash/ash_switches.h"
#include "ash/ime_control_delegate.h"
#include "ash/public/cpp/config.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/session/session_controller.h"
#include "ash/shell.h"
#include "ash/shell_port.h"
#include "ash/system/brightness_control_delegate.h"
#include "ash/system/keyboard_brightness_control_delegate.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/lock_state_controller_test_api.h"
#include "ash/test/test_screenshot_delegate.h"
#include "ash/test/test_session_state_animator.h"
#include "ash/wm/lock_state_controller.h"
#include "ash/wm/panels/panel_layout_manager.h"
#include "ash/wm/window_positioning_utils.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm/wm_event.h"
#include "ash/wm_window.h"
#include "base/command_line.h"
#include "base/test/user_action_tester.h"
#include "services/ui/public/interfaces/window_manager_constants.mojom.h"
#include "ui/app_list/presenter/app_list.h"
#include "ui/app_list/presenter/test/test_app_list_presenter.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/test/test_window_delegate.h"
#include "ui/aura/test/test_windows.h"
#include "ui/aura/window.h"
#include "ui/base/ime/chromeos/fake_ime_keyboard.h"
#include "ui/base/ime/chromeos/ime_keyboard.h"
#include "ui/base/ime/chromeos/input_method_manager.h"
#include "ui/base/ime/chromeos/mock_input_method_manager.h"
#include "ui/display/manager/display_manager.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/event_sink.h"
#include "ui/events/test/event_generator.h"
#include "ui/message_center/message_center.h"
#include "ui/views/widget/widget.h"

#if defined(USE_X11)
#include <X11/Xlib.h>
#include "ui/events/test/events_test_utils_x11.h"
#endif

namespace ash {

namespace {

class TestTarget : public ui::AcceleratorTarget {
 public:
  TestTarget() : accelerator_pressed_count_(0), accelerator_repeat_count_(0) {}
  ~TestTarget() override {}

  int accelerator_pressed_count() const { return accelerator_pressed_count_; }

  int accelerator_repeat_count() const { return accelerator_repeat_count_; }

  void reset() {
    accelerator_pressed_count_ = 0;
    accelerator_repeat_count_ = 0;
  }

  // Overridden from ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

 private:
  int accelerator_pressed_count_;
  int accelerator_repeat_count_;

  DISALLOW_COPY_AND_ASSIGN(TestTarget);
};

ui::Accelerator CreateReleaseAccelerator(ui::KeyboardCode key_code,
                                         int modifiers) {
  ui::Accelerator accelerator(key_code, modifiers);
  accelerator.set_key_state(ui::Accelerator::KeyState::RELEASED);
  return accelerator;
}

class DummyBrightnessControlDelegate : public BrightnessControlDelegate {
 public:
  DummyBrightnessControlDelegate()
      : handle_brightness_down_count_(0), handle_brightness_up_count_(0) {}
  ~DummyBrightnessControlDelegate() override {}

  void HandleBrightnessDown(const ui::Accelerator& accelerator) override {
    ++handle_brightness_down_count_;
    last_accelerator_ = accelerator;
  }
  void HandleBrightnessUp(const ui::Accelerator& accelerator) override {
    ++handle_brightness_up_count_;
    last_accelerator_ = accelerator;
  }
  void SetBrightnessPercent(double percent, bool gradual) override {}
  void GetBrightnessPercent(
      const base::Callback<void(double)>& callback) override {
    callback.Run(100.0);
  }

  int handle_brightness_down_count() const {
    return handle_brightness_down_count_;
  }
  int handle_brightness_up_count() const { return handle_brightness_up_count_; }
  const ui::Accelerator& last_accelerator() const { return last_accelerator_; }

 private:
  int handle_brightness_down_count_;
  int handle_brightness_up_count_;
  ui::Accelerator last_accelerator_;

  DISALLOW_COPY_AND_ASSIGN(DummyBrightnessControlDelegate);
};

class DummyImeControlDelegate : public ImeControlDelegate {
 public:
  DummyImeControlDelegate()
      : handle_next_ime_count_(0),
        handle_previous_ime_count_(0),
        handle_switch_ime_count_(0) {}
  ~DummyImeControlDelegate() override {}

  bool CanCycleIme() override { return true; }
  void HandleNextIme() override { ++handle_next_ime_count_; }
  void HandlePreviousIme() override { ++handle_previous_ime_count_; }
  bool CanSwitchIme(const ui::Accelerator& accelerator) override {
    return true;
  }
  void HandleSwitchIme(const ui::Accelerator& accelerator) override {
    ++handle_switch_ime_count_;
  }

  int handle_next_ime_count() const { return handle_next_ime_count_; }
  int handle_previous_ime_count() const { return handle_previous_ime_count_; }
  int handle_switch_ime_count() const { return handle_switch_ime_count_; }

 private:
  int handle_next_ime_count_;
  int handle_previous_ime_count_;
  int handle_switch_ime_count_;

  DISALLOW_COPY_AND_ASSIGN(DummyImeControlDelegate);
};

class DummyKeyboardBrightnessControlDelegate
    : public KeyboardBrightnessControlDelegate {
 public:
  DummyKeyboardBrightnessControlDelegate()
      : handle_keyboard_brightness_down_count_(0),
        handle_keyboard_brightness_up_count_(0) {}
  ~DummyKeyboardBrightnessControlDelegate() override {}

  void HandleKeyboardBrightnessDown(
      const ui::Accelerator& accelerator) override {
    ++handle_keyboard_brightness_down_count_;
    last_accelerator_ = accelerator;
  }

  void HandleKeyboardBrightnessUp(const ui::Accelerator& accelerator) override {
    ++handle_keyboard_brightness_up_count_;
    last_accelerator_ = accelerator;
  }

  int handle_keyboard_brightness_down_count() const {
    return handle_keyboard_brightness_down_count_;
  }

  int handle_keyboard_brightness_up_count() const {
    return handle_keyboard_brightness_up_count_;
  }

  const ui::Accelerator& last_accelerator() const { return last_accelerator_; }

 private:
  int handle_keyboard_brightness_down_count_;
  int handle_keyboard_brightness_up_count_;
  ui::Accelerator last_accelerator_;

  DISALLOW_COPY_AND_ASSIGN(DummyKeyboardBrightnessControlDelegate);
};

bool TestTarget::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (accelerator.IsRepeat())
    ++accelerator_repeat_count_;
  else
    ++accelerator_pressed_count_;
  return true;
}

bool TestTarget::CanHandleAccelerators() const {
  return true;
}

}  // namespace

class AcceleratorControllerTest : public test::AshTestBase {
 public:
  AcceleratorControllerTest() {}
  ~AcceleratorControllerTest() override {}

 protected:
  static AcceleratorController* GetController();

  static bool ProcessInController(const ui::Accelerator& accelerator) {
    if (accelerator.key_state() == ui::Accelerator::KeyState::RELEASED) {
      // If the |accelerator| should trigger on release, then we store the
      // pressed version of it first in history then the released one to
      // simulate what happens in reality.
      ui::Accelerator pressed_accelerator = accelerator;
      pressed_accelerator.set_key_state(ui::Accelerator::KeyState::PRESSED);
      GetController()->accelerator_history()->StoreCurrentAccelerator(
          pressed_accelerator);
    }
    GetController()->accelerator_history()->StoreCurrentAccelerator(
        accelerator);
    return GetController()->Process(accelerator);
  }

  static const ui::Accelerator& GetPreviousAccelerator() {
    return GetController()->accelerator_history()->previous_accelerator();
  }

  static const ui::Accelerator& GetCurrentAccelerator() {
    return GetController()->accelerator_history()->current_accelerator();
  }

  // Several functions to access ExitWarningHandler (as friend).
  static void StubForTest(ExitWarningHandler* ewh) {
    ewh->stub_timer_for_test_ = true;
  }
  static void Reset(ExitWarningHandler* ewh) {
    ewh->state_ = ExitWarningHandler::IDLE;
  }
  static void SimulateTimerExpired(ExitWarningHandler* ewh) {
    ewh->TimerAction();
  }
  static bool is_ui_shown(ExitWarningHandler* ewh) { return !!ewh->widget_; }
  static bool is_idle(ExitWarningHandler* ewh) {
    return ewh->state_ == ExitWarningHandler::IDLE;
  }
  static bool is_exiting(ExitWarningHandler* ewh) {
    return ewh->state_ == ExitWarningHandler::EXITING;
  }

  void SetBrightnessControlDelegate(
      std::unique_ptr<BrightnessControlDelegate> delegate) {
    Shell::Get()->brightness_control_delegate_ = std::move(delegate);
  }

  void SetKeyboardBrightnessControlDelegate(
      std::unique_ptr<KeyboardBrightnessControlDelegate> delegate) {
    Shell::Get()->keyboard_brightness_control_delegate_ = std::move(delegate);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(AcceleratorControllerTest);
};

AcceleratorController* AcceleratorControllerTest::GetController() {
  return Shell::Get()->accelerator_controller();
}

// Double press of exit shortcut => exiting
TEST_F(AcceleratorControllerTest, ExitWarningHandlerTestDoublePress) {
  ui::Accelerator press(ui::VKEY_Q, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN);
  ui::Accelerator release(press);
  release.set_key_state(ui::Accelerator::KeyState::RELEASED);
  ExitWarningHandler* ewh = GetController()->GetExitWarningHandlerForTest();
  ASSERT_TRUE(ewh);
  StubForTest(ewh);
  EXPECT_TRUE(is_idle(ewh));
  EXPECT_FALSE(is_ui_shown(ewh));
  EXPECT_TRUE(ProcessInController(press));
  EXPECT_FALSE(ProcessInController(release));
  EXPECT_FALSE(is_idle(ewh));
  EXPECT_TRUE(is_ui_shown(ewh));
  EXPECT_TRUE(ProcessInController(press));  // second press before timer.
  EXPECT_FALSE(ProcessInController(release));
  SimulateTimerExpired(ewh);
  EXPECT_TRUE(is_exiting(ewh));
  EXPECT_FALSE(is_ui_shown(ewh));
  Reset(ewh);
}

// Single press of exit shortcut before timer => idle
TEST_F(AcceleratorControllerTest, ExitWarningHandlerTestSinglePress) {
  ui::Accelerator press(ui::VKEY_Q, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN);
  ui::Accelerator release(press);
  release.set_key_state(ui::Accelerator::KeyState::RELEASED);
  ExitWarningHandler* ewh = GetController()->GetExitWarningHandlerForTest();
  ASSERT_TRUE(ewh);
  StubForTest(ewh);
  EXPECT_TRUE(is_idle(ewh));
  EXPECT_FALSE(is_ui_shown(ewh));
  EXPECT_TRUE(ProcessInController(press));
  EXPECT_FALSE(ProcessInController(release));
  EXPECT_FALSE(is_idle(ewh));
  EXPECT_TRUE(is_ui_shown(ewh));
  SimulateTimerExpired(ewh);
  EXPECT_TRUE(is_idle(ewh));
  EXPECT_FALSE(is_ui_shown(ewh));
  Reset(ewh);
}

// Shutdown ash with exit warning bubble open should not crash.
TEST_F(AcceleratorControllerTest, LingeringExitWarningBubble) {
  ExitWarningHandler* ewh = GetController()->GetExitWarningHandlerForTest();
  ASSERT_TRUE(ewh);
  StubForTest(ewh);

  // Trigger once to show the bubble.
  ewh->HandleAccelerator();
  EXPECT_FALSE(is_idle(ewh));
  EXPECT_TRUE(is_ui_shown(ewh));

  // Exit ash and there should be no crash
}

TEST_F(AcceleratorControllerTest, Register) {
  TestTarget target;
  const ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  const ui::Accelerator accelerator_b(ui::VKEY_B, ui::EF_NONE);
  const ui::Accelerator accelerator_c(ui::VKEY_C, ui::EF_NONE);
  const ui::Accelerator accelerator_d(ui::VKEY_D, ui::EF_NONE);

  GetController()->Register(
      {accelerator_a, accelerator_b, accelerator_c, accelerator_d}, &target);

  // The registered accelerators are processed.
  EXPECT_TRUE(ProcessInController(accelerator_a));
  EXPECT_TRUE(ProcessInController(accelerator_b));
  EXPECT_TRUE(ProcessInController(accelerator_c));
  EXPECT_TRUE(ProcessInController(accelerator_d));
  EXPECT_EQ(4, target.accelerator_pressed_count());
}

TEST_F(AcceleratorControllerTest, RegisterMultipleTarget) {
  const ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  TestTarget target1;
  GetController()->Register({accelerator_a}, &target1);
  TestTarget target2;
  GetController()->Register({accelerator_a}, &target2);

  // If multiple targets are registered with the same accelerator, the target
  // registered later processes the accelerator.
  EXPECT_TRUE(ProcessInController(accelerator_a));
  EXPECT_EQ(0, target1.accelerator_pressed_count());
  EXPECT_EQ(1, target2.accelerator_pressed_count());
}

TEST_F(AcceleratorControllerTest, Unregister) {
  const ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  const ui::Accelerator accelerator_b(ui::VKEY_B, ui::EF_NONE);
  TestTarget target;
  GetController()->Register({accelerator_a, accelerator_b}, &target);

  // Unregistering a different accelerator does not affect the other
  // accelerator.
  GetController()->Unregister(accelerator_b, &target);
  EXPECT_TRUE(ProcessInController(accelerator_a));
  EXPECT_EQ(1, target.accelerator_pressed_count());

  // The unregistered accelerator is no longer processed.
  target.reset();
  GetController()->Unregister(accelerator_a, &target);
  EXPECT_FALSE(ProcessInController(accelerator_a));
  EXPECT_EQ(0, target.accelerator_pressed_count());
}

TEST_F(AcceleratorControllerTest, UnregisterAll) {
  const ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  const ui::Accelerator accelerator_b(ui::VKEY_B, ui::EF_NONE);
  TestTarget target1;
  GetController()->Register({accelerator_a, accelerator_b}, &target1);
  const ui::Accelerator accelerator_c(ui::VKEY_C, ui::EF_NONE);
  TestTarget target2;
  GetController()->Register({accelerator_c}, &target2);
  GetController()->UnregisterAll(&target1);

  // All the accelerators registered for |target1| are no longer processed.
  EXPECT_FALSE(ProcessInController(accelerator_a));
  EXPECT_FALSE(ProcessInController(accelerator_b));
  EXPECT_EQ(0, target1.accelerator_pressed_count());

  // UnregisterAll with a different target does not affect the other target.
  EXPECT_TRUE(ProcessInController(accelerator_c));
  EXPECT_EQ(1, target2.accelerator_pressed_count());
}

TEST_F(AcceleratorControllerTest, Process) {
  const ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  TestTarget target1;
  GetController()->Register({accelerator_a}, &target1);

  // The registered accelerator is processed.
  EXPECT_TRUE(ProcessInController(accelerator_a));
  EXPECT_EQ(1, target1.accelerator_pressed_count());

  // The non-registered accelerator is not processed.
  const ui::Accelerator accelerator_b(ui::VKEY_B, ui::EF_NONE);
  EXPECT_FALSE(ProcessInController(accelerator_b));
}

TEST_F(AcceleratorControllerTest, IsRegistered) {
  const ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  const ui::Accelerator accelerator_shift_a(ui::VKEY_A, ui::EF_SHIFT_DOWN);
  TestTarget target;
  GetController()->Register({accelerator_a}, &target);
  EXPECT_TRUE(GetController()->IsRegistered(accelerator_a));
  EXPECT_FALSE(GetController()->IsRegistered(accelerator_shift_a));
  GetController()->UnregisterAll(&target);
  EXPECT_FALSE(GetController()->IsRegistered(accelerator_a));
}

TEST_F(AcceleratorControllerTest, WindowSnap) {
  std::unique_ptr<aura::Window> window(
      CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  wm::WindowState* window_state = wm::GetWindowState(window.get());

  window_state->Activate();

  {
    GetController()->PerformActionIfEnabled(WINDOW_CYCLE_SNAP_LEFT);
    gfx::Rect expected_bounds =
        wm::GetDefaultLeftSnappedWindowBoundsInParent(window.get());
    EXPECT_EQ(expected_bounds.ToString(), window->bounds().ToString());
  }
  {
    GetController()->PerformActionIfEnabled(WINDOW_CYCLE_SNAP_RIGHT);
    gfx::Rect expected_bounds =
        wm::GetDefaultRightSnappedWindowBoundsInParent(window.get());
    EXPECT_EQ(expected_bounds.ToString(), window->bounds().ToString());
  }
  {
    gfx::Rect normal_bounds = window_state->GetRestoreBoundsInParent();

    GetController()->PerformActionIfEnabled(TOGGLE_MAXIMIZED);
    EXPECT_TRUE(window_state->IsMaximized());
    EXPECT_NE(normal_bounds.ToString(), window->bounds().ToString());

    GetController()->PerformActionIfEnabled(TOGGLE_MAXIMIZED);
    EXPECT_FALSE(window_state->IsMaximized());
    // Window gets restored to its restore bounds since side-maximized state
    // is treated as a "maximized" state.
    EXPECT_EQ(normal_bounds.ToString(), window->bounds().ToString());

    GetController()->PerformActionIfEnabled(TOGGLE_MAXIMIZED);
    GetController()->PerformActionIfEnabled(WINDOW_CYCLE_SNAP_LEFT);
    EXPECT_FALSE(window_state->IsMaximized());

    GetController()->PerformActionIfEnabled(TOGGLE_MAXIMIZED);
    GetController()->PerformActionIfEnabled(WINDOW_CYCLE_SNAP_RIGHT);
    EXPECT_FALSE(window_state->IsMaximized());

    GetController()->PerformActionIfEnabled(TOGGLE_MAXIMIZED);
    EXPECT_TRUE(window_state->IsMaximized());
    GetController()->PerformActionIfEnabled(WINDOW_MINIMIZE);
    EXPECT_FALSE(window_state->IsMaximized());
    EXPECT_TRUE(window_state->IsMinimized());
    window_state->Restore();
    window_state->Activate();
  }
  {
    GetController()->PerformActionIfEnabled(WINDOW_MINIMIZE);
    EXPECT_TRUE(window_state->IsMinimized());
  }
}

// Tests that window snapping works.
TEST_F(AcceleratorControllerTest, TestRepeatedSnap) {
  std::unique_ptr<aura::Window> window(
      CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));

  wm::WindowState* window_state = wm::GetWindowState(window.get());
  window_state->Activate();

  // Snap right.
  GetController()->PerformActionIfEnabled(WINDOW_CYCLE_SNAP_RIGHT);
  gfx::Rect normal_bounds = window_state->GetRestoreBoundsInParent();
  gfx::Rect expected_bounds =
      wm::GetDefaultRightSnappedWindowBoundsInParent(window.get());
  EXPECT_EQ(expected_bounds.ToString(), window->bounds().ToString());
  EXPECT_TRUE(window_state->IsSnapped());
  // Snap right again ->> becomes normal.
  GetController()->PerformActionIfEnabled(WINDOW_CYCLE_SNAP_RIGHT);
  EXPECT_TRUE(window_state->IsNormalStateType());
  EXPECT_EQ(normal_bounds.ToString(), window->bounds().ToString());
  // Snap right.
  GetController()->PerformActionIfEnabled(WINDOW_CYCLE_SNAP_RIGHT);
  EXPECT_TRUE(window_state->IsSnapped());
  // Snap left.
  GetController()->PerformActionIfEnabled(WINDOW_CYCLE_SNAP_LEFT);
  EXPECT_TRUE(window_state->IsSnapped());
  expected_bounds = wm::GetDefaultLeftSnappedWindowBoundsInParent(window.get());
  EXPECT_EQ(expected_bounds.ToString(), window->bounds().ToString());
  // Snap left again ->> becomes normal.
  GetController()->PerformActionIfEnabled(WINDOW_CYCLE_SNAP_LEFT);
  EXPECT_TRUE(window_state->IsNormalStateType());
  EXPECT_EQ(normal_bounds.ToString(), window->bounds().ToString());
}

TEST_F(AcceleratorControllerTest, RotateScreen) {
  // TODO: needs GetDisplayInfo http://crbug.com/622480.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  display::Display display = display::Screen::GetScreen()->GetPrimaryDisplay();
  display::Display::Rotation initial_rotation =
      GetActiveDisplayRotation(display.id());
  ui::test::EventGenerator& generator = GetEventGenerator();
  generator.PressKey(ui::VKEY_BROWSER_REFRESH,
                     ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  generator.ReleaseKey(ui::VKEY_BROWSER_REFRESH,
                       ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  display::Display::Rotation new_rotation =
      GetActiveDisplayRotation(display.id());
  // |new_rotation| is determined by the AcceleratorControllerDelegate.
  EXPECT_NE(initial_rotation, new_rotation);
}

TEST_F(AcceleratorControllerTest, AutoRepeat) {
  ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  TestTarget target_a;
  GetController()->Register({accelerator_a}, &target_a);
  ui::Accelerator accelerator_b(ui::VKEY_B, ui::EF_CONTROL_DOWN);
  TestTarget target_b;
  GetController()->Register({accelerator_b}, &target_b);

  ui::test::EventGenerator& generator = GetEventGenerator();
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  generator.ReleaseKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);

  EXPECT_EQ(1, target_a.accelerator_pressed_count());
  EXPECT_EQ(0, target_a.accelerator_repeat_count());

  // Long press should generate one
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN | ui::EF_IS_REPEAT);
  EXPECT_EQ(2, target_a.accelerator_pressed_count());
  EXPECT_EQ(1, target_a.accelerator_repeat_count());
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN | ui::EF_IS_REPEAT);
  EXPECT_EQ(2, target_a.accelerator_pressed_count());
  EXPECT_EQ(2, target_a.accelerator_repeat_count());
  generator.ReleaseKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  EXPECT_EQ(2, target_a.accelerator_pressed_count());
  EXPECT_EQ(2, target_a.accelerator_repeat_count());

  // Long press was intercepted by another key press.
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN | ui::EF_IS_REPEAT);
  generator.PressKey(ui::VKEY_B, ui::EF_CONTROL_DOWN);
  generator.ReleaseKey(ui::VKEY_B, ui::EF_CONTROL_DOWN);
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN | ui::EF_IS_REPEAT);
  generator.ReleaseKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);

  EXPECT_EQ(1, target_b.accelerator_pressed_count());
  EXPECT_EQ(0, target_b.accelerator_repeat_count());
  EXPECT_EQ(4, target_a.accelerator_pressed_count());
  EXPECT_EQ(4, target_a.accelerator_repeat_count());
}

TEST_F(AcceleratorControllerTest, Previous) {
  ui::test::EventGenerator& generator = GetEventGenerator();
  generator.PressKey(ui::VKEY_VOLUME_MUTE, ui::EF_NONE);
  generator.ReleaseKey(ui::VKEY_VOLUME_MUTE, ui::EF_NONE);

  EXPECT_EQ(ui::VKEY_VOLUME_MUTE, GetPreviousAccelerator().key_code());
  EXPECT_EQ(ui::EF_NONE, GetPreviousAccelerator().modifiers());

  generator.PressKey(ui::VKEY_TAB, ui::EF_CONTROL_DOWN);
  generator.ReleaseKey(ui::VKEY_TAB, ui::EF_CONTROL_DOWN);

  EXPECT_EQ(ui::VKEY_TAB, GetPreviousAccelerator().key_code());
  EXPECT_EQ(ui::EF_CONTROL_DOWN, GetPreviousAccelerator().modifiers());
}

TEST_F(AcceleratorControllerTest, DontRepeatToggleFullscreen) {
  const AcceleratorData accelerators[] = {
      {true, ui::VKEY_J, ui::EF_ALT_DOWN, TOGGLE_FULLSCREEN},
      {true, ui::VKEY_K, ui::EF_ALT_DOWN, TOGGLE_FULLSCREEN},
  };
  GetController()->RegisterAccelerators(accelerators, arraysize(accelerators));

  views::Widget::InitParams params(views::Widget::InitParams::TYPE_WINDOW);
  params.context = CurrentContext();
  params.bounds = gfx::Rect(5, 5, 20, 20);
  views::Widget* widget = new views::Widget;
  widget->Init(params);
  widget->Show();
  widget->Activate();
  widget->GetNativeView()->SetProperty(aura::client::kResizeBehaviorKey,
                                       ui::mojom::kResizeBehaviorCanMaximize);

  ui::test::EventGenerator& generator = GetEventGenerator();
  wm::WindowState* window_state = wm::GetWindowState(widget->GetNativeView());

  // Toggling not suppressed.
  generator.PressKey(ui::VKEY_J, ui::EF_ALT_DOWN);
  EXPECT_TRUE(window_state->IsFullscreen());

  // The same accelerator - toggling suppressed.
  generator.PressKey(ui::VKEY_J, ui::EF_ALT_DOWN | ui::EF_IS_REPEAT);
  EXPECT_TRUE(window_state->IsFullscreen());

  // Different accelerator.
  generator.PressKey(ui::VKEY_K, ui::EF_ALT_DOWN);
  EXPECT_FALSE(window_state->IsFullscreen());
}

// TODO(oshima): Fix this test to use EventGenerator.
#if defined(USE_X11)
TEST_F(AcceleratorControllerTest, ProcessOnce) {
  // The IME event filter interferes with the basic key event propagation we
  // attempt to do here, so we disable it.
  DisableIME();
  ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  TestTarget target;
  GetController()->Register({accelerator_a}, &target);

  // The accelerator is processed only once.
  ui::EventSink* sink = Shell::GetPrimaryRootWindow()->GetHost()->event_sink();

  ui::ScopedXI2Event key_event;
  key_event.InitKeyEvent(ui::ET_KEY_PRESSED, ui::VKEY_A, 0);
  ui::KeyEvent key_event1(key_event);
  ui::EventDispatchDetails details = sink->OnEventFromSource(&key_event1);
  EXPECT_TRUE(key_event1.handled() || details.dispatcher_destroyed);

  ui::KeyEvent key_event2('A', ui::VKEY_A, ui::EF_NONE);
  details = sink->OnEventFromSource(&key_event2);
  EXPECT_FALSE(key_event2.handled() || details.dispatcher_destroyed);

  key_event.InitKeyEvent(ui::ET_KEY_RELEASED, ui::VKEY_A, 0);
  ui::KeyEvent key_event3(key_event);
  details = sink->OnEventFromSource(&key_event3);
  EXPECT_FALSE(key_event3.handled() || details.dispatcher_destroyed);
  EXPECT_EQ(1, target.accelerator_pressed_count());
}
#endif

TEST_F(AcceleratorControllerTest, GlobalAccelerators) {
  // TODO: TestScreenshotDelegate is null in mash http://crbug.com/632111.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  // CycleBackward
  EXPECT_TRUE(ProcessInController(
      ui::Accelerator(ui::VKEY_TAB, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN)));
  // CycleForward
  EXPECT_TRUE(
      ProcessInController(ui::Accelerator(ui::VKEY_TAB, ui::EF_ALT_DOWN)));
  // CycleLinear
  EXPECT_TRUE(ProcessInController(
      ui::Accelerator(ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_NONE)));

  // The "Take Screenshot", "Take Partial Screenshot", volume, brightness, and
  // keyboard brightness accelerators are only defined on ChromeOS.
  {
    test::TestScreenshotDelegate* delegate = GetScreenshotDelegate();
    delegate->set_can_take_screenshot(false);
    EXPECT_TRUE(ProcessInController(
        ui::Accelerator(ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_CONTROL_DOWN)));
    EXPECT_TRUE(
        ProcessInController(ui::Accelerator(ui::VKEY_PRINT, ui::EF_NONE)));
    EXPECT_TRUE(ProcessInController(ui::Accelerator(
        ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));

    delegate->set_can_take_screenshot(true);
    EXPECT_EQ(0, delegate->handle_take_screenshot_count());
    EXPECT_TRUE(ProcessInController(
        ui::Accelerator(ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_CONTROL_DOWN)));
    EXPECT_EQ(1, delegate->handle_take_screenshot_count());
    EXPECT_TRUE(
        ProcessInController(ui::Accelerator(ui::VKEY_PRINT, ui::EF_NONE)));
    EXPECT_EQ(2, delegate->handle_take_screenshot_count());
    EXPECT_TRUE(ProcessInController(ui::Accelerator(
        ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));
    EXPECT_EQ(2, delegate->handle_take_screenshot_count());
  }
  const ui::Accelerator volume_mute(ui::VKEY_VOLUME_MUTE, ui::EF_NONE);
  const ui::Accelerator volume_down(ui::VKEY_VOLUME_DOWN, ui::EF_NONE);
  const ui::Accelerator volume_up(ui::VKEY_VOLUME_UP, ui::EF_NONE);
  {
    base::UserActionTester user_action_tester;
    ui::AcceleratorHistory* history = GetController()->accelerator_history();

    EXPECT_EQ(0, user_action_tester.GetActionCount("Accel_VolumeMute_F8"));
    EXPECT_TRUE(ProcessInController(volume_mute));
    EXPECT_EQ(1, user_action_tester.GetActionCount("Accel_VolumeMute_F8"));
    EXPECT_EQ(volume_mute, history->current_accelerator());

    EXPECT_EQ(0, user_action_tester.GetActionCount("Accel_VolumeDown_F9"));
    EXPECT_TRUE(ProcessInController(volume_down));
    EXPECT_EQ(1, user_action_tester.GetActionCount("Accel_VolumeDown_F9"));
    EXPECT_EQ(volume_down, history->current_accelerator());

    EXPECT_EQ(0, user_action_tester.GetActionCount("Accel_VolumeUp_F10"));
    EXPECT_TRUE(ProcessInController(volume_up));
    EXPECT_EQ(volume_up, history->current_accelerator());
    EXPECT_EQ(1, user_action_tester.GetActionCount("Accel_VolumeUp_F10"));
  }
  // Brightness
  // ui::VKEY_BRIGHTNESS_DOWN/UP are not defined on Windows.
  const ui::Accelerator brightness_down(ui::VKEY_BRIGHTNESS_DOWN, ui::EF_NONE);
  const ui::Accelerator brightness_up(ui::VKEY_BRIGHTNESS_UP, ui::EF_NONE);
  {
    DummyBrightnessControlDelegate* delegate =
        new DummyBrightnessControlDelegate;
    SetBrightnessControlDelegate(
        std::unique_ptr<BrightnessControlDelegate>(delegate));
    EXPECT_EQ(0, delegate->handle_brightness_down_count());
    EXPECT_TRUE(ProcessInController(brightness_down));
    EXPECT_EQ(1, delegate->handle_brightness_down_count());
    EXPECT_EQ(brightness_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_brightness_up_count());
    EXPECT_TRUE(ProcessInController(brightness_up));
    EXPECT_EQ(1, delegate->handle_brightness_up_count());
    EXPECT_EQ(brightness_up, delegate->last_accelerator());
  }

  // Keyboard brightness
  const ui::Accelerator alt_brightness_down(ui::VKEY_BRIGHTNESS_DOWN,
                                            ui::EF_ALT_DOWN);
  const ui::Accelerator alt_brightness_up(ui::VKEY_BRIGHTNESS_UP,
                                          ui::EF_ALT_DOWN);
  {
    EXPECT_TRUE(ProcessInController(alt_brightness_down));
    EXPECT_TRUE(ProcessInController(alt_brightness_up));
    DummyKeyboardBrightnessControlDelegate* delegate =
        new DummyKeyboardBrightnessControlDelegate;
    SetKeyboardBrightnessControlDelegate(
        std::unique_ptr<KeyboardBrightnessControlDelegate>(delegate));
    EXPECT_EQ(0, delegate->handle_keyboard_brightness_down_count());
    EXPECT_TRUE(ProcessInController(alt_brightness_down));
    EXPECT_EQ(1, delegate->handle_keyboard_brightness_down_count());
    EXPECT_EQ(alt_brightness_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_keyboard_brightness_up_count());
    EXPECT_TRUE(ProcessInController(alt_brightness_up));
    EXPECT_EQ(1, delegate->handle_keyboard_brightness_up_count());
    EXPECT_EQ(alt_brightness_up, delegate->last_accelerator());
  }

  // Exit
  ExitWarningHandler* ewh = GetController()->GetExitWarningHandlerForTest();
  ASSERT_TRUE(ewh);
  StubForTest(ewh);
  EXPECT_TRUE(is_idle(ewh));
  EXPECT_FALSE(is_ui_shown(ewh));
  EXPECT_TRUE(ProcessInController(
      ui::Accelerator(ui::VKEY_Q, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));
  EXPECT_FALSE(is_idle(ewh));
  EXPECT_TRUE(is_ui_shown(ewh));
  SimulateTimerExpired(ewh);
  EXPECT_TRUE(is_idle(ewh));
  EXPECT_FALSE(is_ui_shown(ewh));
  Reset(ewh);

  // New tab
  EXPECT_TRUE(
      ProcessInController(ui::Accelerator(ui::VKEY_T, ui::EF_CONTROL_DOWN)));

  // New incognito window
  EXPECT_TRUE(ProcessInController(
      ui::Accelerator(ui::VKEY_N, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));

  // New window
  EXPECT_TRUE(
      ProcessInController(ui::Accelerator(ui::VKEY_N, ui::EF_CONTROL_DOWN)));

  // Restore tab
  EXPECT_TRUE(ProcessInController(
      ui::Accelerator(ui::VKEY_T, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));

  // Show task manager
  EXPECT_TRUE(
      ProcessInController(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_SHIFT_DOWN)));

  // Open file manager
  EXPECT_TRUE(ProcessInController(
      ui::Accelerator(ui::VKEY_M, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN)));

  // Lock screen
  // NOTE: Accelerators that do not work on the lock screen need to be
  // tested before the sequence below is invoked because it causes a side
  // effect of locking the screen.
  EXPECT_TRUE(
      ProcessInController(ui::Accelerator(ui::VKEY_L, ui::EF_COMMAND_DOWN)));
}

TEST_F(AcceleratorControllerTest, GlobalAcceleratorsToggleAppList) {
  app_list::test::TestAppListPresenter test_app_list_presenter;
  Shell::Get()->app_list()->SetAppListPresenter(
      test_app_list_presenter.CreateInterfacePtrAndBind());
  AccessibilityDelegate* delegate = Shell::Get()->accessibility_delegate();

  // The press event should not toggle the AppList, the release should instead.
  EXPECT_FALSE(
      ProcessInController(ui::Accelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  RunAllPendingInMessageLoop();
  EXPECT_EQ(ui::VKEY_LWIN, GetCurrentAccelerator().key_code());
  EXPECT_EQ(0u, test_app_list_presenter.toggle_count());

  EXPECT_TRUE(ProcessInController(
      CreateReleaseAccelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  RunAllPendingInMessageLoop();
  EXPECT_EQ(1u, test_app_list_presenter.toggle_count());
  EXPECT_EQ(ui::VKEY_LWIN, GetPreviousAccelerator().key_code());

  // When spoken feedback is on, the AppList should not toggle.
  delegate->ToggleSpokenFeedback(A11Y_NOTIFICATION_NONE);
  EXPECT_FALSE(
      ProcessInController(ui::Accelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  EXPECT_FALSE(ProcessInController(
      CreateReleaseAccelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  delegate->ToggleSpokenFeedback(A11Y_NOTIFICATION_NONE);
  RunAllPendingInMessageLoop();
  EXPECT_EQ(1u, test_app_list_presenter.toggle_count());

  // Turning off spoken feedback should allow the AppList to toggle again.
  EXPECT_FALSE(
      ProcessInController(ui::Accelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  EXPECT_TRUE(ProcessInController(
      CreateReleaseAccelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  RunAllPendingInMessageLoop();
  EXPECT_EQ(2u, test_app_list_presenter.toggle_count());

  // The press of VKEY_BROWSER_SEARCH should toggle the AppList
  EXPECT_TRUE(ProcessInController(
      ui::Accelerator(ui::VKEY_BROWSER_SEARCH, ui::EF_NONE)));
  RunAllPendingInMessageLoop();
  EXPECT_EQ(3u, test_app_list_presenter.toggle_count());
  EXPECT_FALSE(ProcessInController(
      CreateReleaseAccelerator(ui::VKEY_BROWSER_SEARCH, ui::EF_NONE)));
  RunAllPendingInMessageLoop();
  EXPECT_EQ(3u, test_app_list_presenter.toggle_count());
}

TEST_F(AcceleratorControllerTest, ImeGlobalAccelerators) {
  // Test IME shortcuts.
  ui::Accelerator control_space_down(ui::VKEY_SPACE, ui::EF_CONTROL_DOWN);
  ui::Accelerator control_space_up(ui::VKEY_SPACE, ui::EF_CONTROL_DOWN);
  control_space_up.set_key_state(ui::Accelerator::KeyState::RELEASED);
  const ui::Accelerator convert(ui::VKEY_CONVERT, ui::EF_NONE);
  const ui::Accelerator non_convert(ui::VKEY_NONCONVERT, ui::EF_NONE);
  const ui::Accelerator wide_half_1(ui::VKEY_DBE_SBCSCHAR, ui::EF_NONE);
  const ui::Accelerator wide_half_2(ui::VKEY_DBE_DBCSCHAR, ui::EF_NONE);
  const ui::Accelerator hangul(ui::VKEY_HANGUL, ui::EF_NONE);
  EXPECT_FALSE(ProcessInController(control_space_down));
  EXPECT_FALSE(ProcessInController(control_space_up));
  EXPECT_FALSE(ProcessInController(convert));
  EXPECT_FALSE(ProcessInController(non_convert));
  EXPECT_FALSE(ProcessInController(wide_half_1));
  EXPECT_FALSE(ProcessInController(wide_half_2));
  EXPECT_FALSE(ProcessInController(hangul));
  DummyImeControlDelegate* delegate = new DummyImeControlDelegate;
  GetController()->SetImeControlDelegate(
      std::unique_ptr<ImeControlDelegate>(delegate));
  EXPECT_EQ(0, delegate->handle_previous_ime_count());
  EXPECT_TRUE(ProcessInController(control_space_down));
  EXPECT_EQ(1, delegate->handle_previous_ime_count());
  EXPECT_TRUE(ProcessInController(control_space_up));
  EXPECT_EQ(1, delegate->handle_previous_ime_count());
  EXPECT_EQ(0, delegate->handle_switch_ime_count());
  EXPECT_TRUE(ProcessInController(convert));
  EXPECT_EQ(1, delegate->handle_switch_ime_count());
  EXPECT_TRUE(ProcessInController(non_convert));
  EXPECT_EQ(2, delegate->handle_switch_ime_count());
  EXPECT_TRUE(ProcessInController(wide_half_1));
  EXPECT_EQ(3, delegate->handle_switch_ime_count());
  EXPECT_TRUE(ProcessInController(wide_half_2));
  EXPECT_EQ(4, delegate->handle_switch_ime_count());
  EXPECT_TRUE(ProcessInController(hangul));
  EXPECT_EQ(5, delegate->handle_switch_ime_count());
}

// TODO(nona|mazda): Remove this when crbug.com/139556 in a better way.
TEST_F(AcceleratorControllerTest, ImeGlobalAcceleratorsWorkaround139556) {
  // The workaround for crbug.com/139556 depends on the fact that we don't
  // use Shift+Alt+Enter/Space with ET_KEY_PRESSED as an accelerator. Test it.
  const ui::Accelerator shift_alt_return_press(
      ui::VKEY_RETURN, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);
  EXPECT_FALSE(ProcessInController(shift_alt_return_press));
  const ui::Accelerator shift_alt_space_press(
      ui::VKEY_SPACE, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);
  EXPECT_FALSE(ProcessInController(shift_alt_space_press));
}

TEST_F(AcceleratorControllerTest, PreferredReservedAccelerators) {
  // Power key is reserved on chromeos.
  EXPECT_TRUE(GetController()->IsReserved(
      ui::Accelerator(ui::VKEY_POWER, ui::EF_NONE)));
  EXPECT_FALSE(GetController()->IsPreferred(
      ui::Accelerator(ui::VKEY_POWER, ui::EF_NONE)));

  // ALT+Tab are not reserved but preferred.
  EXPECT_FALSE(GetController()->IsReserved(
      ui::Accelerator(ui::VKEY_TAB, ui::EF_ALT_DOWN)));
  EXPECT_FALSE(GetController()->IsReserved(
      ui::Accelerator(ui::VKEY_TAB, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN)));
  EXPECT_TRUE(GetController()->IsPreferred(
      ui::Accelerator(ui::VKEY_TAB, ui::EF_ALT_DOWN)));
  EXPECT_TRUE(GetController()->IsPreferred(
      ui::Accelerator(ui::VKEY_TAB, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN)));

  // Others are not reserved nor preferred
  EXPECT_FALSE(GetController()->IsReserved(
      ui::Accelerator(ui::VKEY_PRINT, ui::EF_NONE)));
  EXPECT_FALSE(GetController()->IsPreferred(
      ui::Accelerator(ui::VKEY_PRINT, ui::EF_NONE)));
  EXPECT_FALSE(
      GetController()->IsReserved(ui::Accelerator(ui::VKEY_TAB, ui::EF_NONE)));
  EXPECT_FALSE(
      GetController()->IsPreferred(ui::Accelerator(ui::VKEY_TAB, ui::EF_NONE)));
  EXPECT_FALSE(
      GetController()->IsReserved(ui::Accelerator(ui::VKEY_A, ui::EF_NONE)));
  EXPECT_FALSE(
      GetController()->IsPreferred(ui::Accelerator(ui::VKEY_A, ui::EF_NONE)));
}

namespace {

class TestInputMethodManager
    : public chromeos::input_method::MockInputMethodManager {
 public:
  TestInputMethodManager() = default;
  ~TestInputMethodManager() override = default;

  // MockInputMethodManager:
  chromeos::input_method::ImeKeyboard* GetImeKeyboard() override {
    return &keyboard_;
  }

 private:
  chromeos::input_method::FakeImeKeyboard keyboard_;

  DISALLOW_COPY_AND_ASSIGN(TestInputMethodManager);
};

class ToggleCapsLockTest : public AcceleratorControllerTest {
 public:
  ToggleCapsLockTest() = default;
  ~ToggleCapsLockTest() override = default;

  void SetUp() override {
    AcceleratorControllerTest::SetUp();
    chromeos::input_method::InputMethodManager::Initialize(
        new TestInputMethodManager);
  }

  void TearDown() override {
    chromeos::input_method::InputMethodManager::Shutdown();
    AcceleratorControllerTest::TearDown();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ToggleCapsLockTest);
};

// Tests the five combinations of the TOGGLE_CAPS_LOCK accelerator.
TEST_F(ToggleCapsLockTest, ToggleCapsLockAccelerators) {
  chromeos::input_method::InputMethodManager* input_method_manager =
      chromeos::input_method::InputMethodManager::Get();
  ASSERT_TRUE(input_method_manager);
  EXPECT_FALSE(input_method_manager->GetImeKeyboard()->CapsLockIsEnabled());

  // 1. Press Alt, Press Search, Release Search, Release Alt.
  // Note when you press Alt then press search, the key_code at this point is
  // VKEY_LWIN (for search) and Alt is the modifier.
  const ui::Accelerator press_alt_then_search(ui::VKEY_LWIN, ui::EF_ALT_DOWN);
  EXPECT_FALSE(ProcessInController(press_alt_then_search));
  // When you release Search before Alt, the key_code is still VKEY_LWIN and
  // Alt is still the modifier.
  const ui::Accelerator release_search_before_alt(
      CreateReleaseAccelerator(ui::VKEY_LWIN, ui::EF_ALT_DOWN));
  EXPECT_TRUE(ProcessInController(release_search_before_alt));
  EXPECT_TRUE(input_method_manager->GetImeKeyboard()->CapsLockIsEnabled());
  input_method_manager->GetImeKeyboard()->SetCapsLockEnabled(false);

  // 2. Press Search, Press Alt, Release Search, Release Alt.
  const ui::Accelerator press_search_then_alt(ui::VKEY_MENU,
                                              ui::EF_COMMAND_DOWN);
  EXPECT_FALSE(ProcessInController(press_search_then_alt));
  EXPECT_TRUE(ProcessInController(release_search_before_alt));
  EXPECT_TRUE(input_method_manager->GetImeKeyboard()->CapsLockIsEnabled());
  input_method_manager->GetImeKeyboard()->SetCapsLockEnabled(false);

  // 3. Press Alt, Press Search, Release Alt, Release Search.
  EXPECT_FALSE(ProcessInController(press_alt_then_search));
  const ui::Accelerator release_alt_before_search(
      CreateReleaseAccelerator(ui::VKEY_MENU, ui::EF_COMMAND_DOWN));
  EXPECT_TRUE(ProcessInController(release_alt_before_search));
  EXPECT_TRUE(input_method_manager->GetImeKeyboard()->CapsLockIsEnabled());
  input_method_manager->GetImeKeyboard()->SetCapsLockEnabled(false);

  // 4. Press Search, Press Alt, Release Alt, Release Search.
  EXPECT_FALSE(ProcessInController(press_search_then_alt));
  EXPECT_TRUE(ProcessInController(release_alt_before_search));
  EXPECT_TRUE(input_method_manager->GetImeKeyboard()->CapsLockIsEnabled());
  input_method_manager->GetImeKeyboard()->SetCapsLockEnabled(false);

  // 5. Press Caps Lock, Release Caps Lock.
  const ui::Accelerator press_caps_lock(ui::VKEY_CAPITAL, ui::EF_NONE);
  EXPECT_FALSE(ProcessInController(press_caps_lock));
  EXPECT_FALSE(input_method_manager->GetImeKeyboard()->CapsLockIsEnabled());
  const ui::Accelerator release_caps_lock(
      CreateReleaseAccelerator(ui::VKEY_CAPITAL, ui::EF_NONE));
  EXPECT_TRUE(ProcessInController(release_caps_lock));
  EXPECT_TRUE(input_method_manager->GetImeKeyboard()->CapsLockIsEnabled());
}

class PreferredReservedAcceleratorsTest : public test::AshTestBase {
 public:
  PreferredReservedAcceleratorsTest() {}
  ~PreferredReservedAcceleratorsTest() override {}

  // test::AshTestBase:
  void SetUp() override {
    AshTestBase::SetUp();
    Shell::Get()->lock_state_controller()->set_animator_for_test(
        new test::TestSessionStateAnimator);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(PreferredReservedAcceleratorsTest);
};

}  // namespace

TEST_F(PreferredReservedAcceleratorsTest, AcceleratorsWithFullscreen) {
  // TODO: needs LockStateController ported: http://crbug.com/632189.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  aura::Window* w1 = CreateTestWindowInShellWithId(0);
  aura::Window* w2 = CreateTestWindowInShellWithId(1);
  wm::ActivateWindow(w1);

  wm::WMEvent fullscreen(wm::WM_EVENT_FULLSCREEN);
  wm::WindowState* w1_state = wm::GetWindowState(w1);
  w1_state->OnWMEvent(&fullscreen);
  ASSERT_TRUE(w1_state->IsFullscreen());

  ui::test::EventGenerator& generator = GetEventGenerator();

  // Power key (reserved) should always be handled.
  test::LockStateControllerTestApi test_api(
      Shell::Get()->lock_state_controller());
  EXPECT_FALSE(test_api.is_animating_lock());
  generator.PressKey(ui::VKEY_POWER, ui::EF_NONE);
  EXPECT_TRUE(test_api.is_animating_lock());

  auto press_and_release_alt_tab = [&generator]() {
    generator.PressKey(ui::VKEY_TAB, ui::EF_ALT_DOWN);
    // Release the alt key to trigger the window activation.
    generator.ReleaseKey(ui::VKEY_MENU, ui::EF_NONE);
  };

  // A fullscreen window can consume ALT-TAB (preferred).
  ASSERT_EQ(w1, wm::GetActiveWindow());
  press_and_release_alt_tab();
  ASSERT_EQ(w1, wm::GetActiveWindow());
  ASSERT_NE(w2, wm::GetActiveWindow());

  // ALT-TAB is non repeatable. Press A to cancel the
  // repeat record.
  generator.PressKey(ui::VKEY_A, ui::EF_NONE);
  generator.ReleaseKey(ui::VKEY_A, ui::EF_NONE);

  // A normal window shouldn't consume preferred accelerator.
  wm::WMEvent normal(wm::WM_EVENT_NORMAL);
  w1_state->OnWMEvent(&normal);
  ASSERT_FALSE(w1_state->IsFullscreen());

  EXPECT_EQ(w1, wm::GetActiveWindow());
  press_and_release_alt_tab();
  ASSERT_NE(w1, wm::GetActiveWindow());
  ASSERT_EQ(w2, wm::GetActiveWindow());
}

TEST_F(PreferredReservedAcceleratorsTest, AcceleratorsWithPinned) {
  // TODO: needs LockStateController ported: http://crbug.com/632189.
  if (Shell::GetAshConfig() == Config::MASH)
    return;
  aura::Window* w1 = CreateTestWindowInShellWithId(0);
  aura::Window* w2 = CreateTestWindowInShellWithId(1);
  wm::ActivateWindow(w1);

  {
    wm::WMEvent pin_event(wm::WM_EVENT_PIN);
    wm::WindowState* w1_state = wm::GetWindowState(w1);
    w1_state->OnWMEvent(&pin_event);
    ASSERT_TRUE(w1_state->IsPinned());
  }

  ui::test::EventGenerator& generator = GetEventGenerator();

  // Power key (reserved) should always be handled.
  test::LockStateControllerTestApi test_api(
      Shell::Get()->lock_state_controller());
  EXPECT_FALSE(test_api.is_animating_lock());
  generator.PressKey(ui::VKEY_POWER, ui::EF_NONE);
  EXPECT_TRUE(test_api.is_animating_lock());

  // A pinned window can consume ALT-TAB (preferred), but no side effect.
  ASSERT_EQ(w1, wm::GetActiveWindow());
  generator.PressKey(ui::VKEY_TAB, ui::EF_ALT_DOWN);
  generator.ReleaseKey(ui::VKEY_TAB, ui::EF_ALT_DOWN);
  ASSERT_EQ(w1, wm::GetActiveWindow());
  ASSERT_NE(w2, wm::GetActiveWindow());
}

TEST_F(AcceleratorControllerTest, DisallowedAtModalWindow) {
  // TODO: TestScreenshotDelegate is null in mash http://crbug.com/632111.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  std::set<AcceleratorAction> all_actions;
  for (size_t i = 0; i < kAcceleratorDataLength; ++i)
    all_actions.insert(kAcceleratorData[i].action);
  std::set<AcceleratorAction> all_debug_actions;
  for (size_t i = 0; i < kDebugAcceleratorDataLength; ++i)
    all_debug_actions.insert(kDebugAcceleratorData[i].action);
  std::set<AcceleratorAction> all_dev_actions;
  for (size_t i = 0; i < kDeveloperAcceleratorDataLength; ++i)
    all_dev_actions.insert(kDeveloperAcceleratorData[i].action);

  std::set<AcceleratorAction> actionsAllowedAtModalWindow;
  for (size_t k = 0; k < kActionsAllowedAtModalWindowLength; ++k)
    actionsAllowedAtModalWindow.insert(kActionsAllowedAtModalWindow[k]);
  for (const auto& action : actionsAllowedAtModalWindow) {
    EXPECT_TRUE(all_actions.find(action) != all_actions.end() ||
                all_debug_actions.find(action) != all_debug_actions.end() ||
                all_dev_actions.find(action) != all_dev_actions.end())
        << " action from kActionsAllowedAtModalWindow"
        << " not found in kAcceleratorData, kDebugAcceleratorData or"
        << " kDeveloperAcceleratorData action: " << action;
  }
  std::unique_ptr<aura::Window> window(
      CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  wm::ActivateWindow(window.get());
  ShellPort::Get()->SimulateModalWindowOpenForTesting(true);
  for (const auto& action : all_actions) {
    if (actionsAllowedAtModalWindow.find(action) ==
        actionsAllowedAtModalWindow.end()) {
      EXPECT_TRUE(GetController()->PerformActionIfEnabled(action))
          << " for action (disallowed at modal window): " << action;
    }
  }
  //  Testing of top row (F5-F10) accelerators that should still work
  //  when a modal window is open
  //
  // Screenshot
  {
    test::TestScreenshotDelegate* delegate = GetScreenshotDelegate();
    delegate->set_can_take_screenshot(false);
    EXPECT_TRUE(ProcessInController(
        ui::Accelerator(ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_CONTROL_DOWN)));
    EXPECT_TRUE(
        ProcessInController(ui::Accelerator(ui::VKEY_PRINT, ui::EF_NONE)));
    EXPECT_TRUE(ProcessInController(ui::Accelerator(
        ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));
    delegate->set_can_take_screenshot(true);
    EXPECT_EQ(0, delegate->handle_take_screenshot_count());
    EXPECT_TRUE(ProcessInController(
        ui::Accelerator(ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_CONTROL_DOWN)));
    EXPECT_EQ(1, delegate->handle_take_screenshot_count());
    EXPECT_TRUE(
        ProcessInController(ui::Accelerator(ui::VKEY_PRINT, ui::EF_NONE)));
    EXPECT_EQ(2, delegate->handle_take_screenshot_count());
    EXPECT_TRUE(ProcessInController(ui::Accelerator(
        ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));
    EXPECT_EQ(2, delegate->handle_take_screenshot_count());
  }
  // Brightness
  const ui::Accelerator brightness_down(ui::VKEY_BRIGHTNESS_DOWN, ui::EF_NONE);
  const ui::Accelerator brightness_up(ui::VKEY_BRIGHTNESS_UP, ui::EF_NONE);
  {
    DummyBrightnessControlDelegate* delegate =
        new DummyBrightnessControlDelegate;
    SetBrightnessControlDelegate(
        std::unique_ptr<BrightnessControlDelegate>(delegate));
    EXPECT_EQ(0, delegate->handle_brightness_down_count());
    EXPECT_TRUE(ProcessInController(brightness_down));
    EXPECT_EQ(1, delegate->handle_brightness_down_count());
    EXPECT_EQ(brightness_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_brightness_up_count());
    EXPECT_TRUE(ProcessInController(brightness_up));
    EXPECT_EQ(1, delegate->handle_brightness_up_count());
    EXPECT_EQ(brightness_up, delegate->last_accelerator());
  }
  // Volume
  const ui::Accelerator volume_mute(ui::VKEY_VOLUME_MUTE, ui::EF_NONE);
  const ui::Accelerator volume_down(ui::VKEY_VOLUME_DOWN, ui::EF_NONE);
  const ui::Accelerator volume_up(ui::VKEY_VOLUME_UP, ui::EF_NONE);
  {
    base::UserActionTester user_action_tester;
    ui::AcceleratorHistory* history = GetController()->accelerator_history();

    EXPECT_EQ(0, user_action_tester.GetActionCount("Accel_VolumeMute_F8"));
    EXPECT_TRUE(ProcessInController(volume_mute));
    EXPECT_EQ(1, user_action_tester.GetActionCount("Accel_VolumeMute_F8"));
    EXPECT_EQ(volume_mute, history->current_accelerator());

    EXPECT_EQ(0, user_action_tester.GetActionCount("Accel_VolumeDown_F9"));
    EXPECT_TRUE(ProcessInController(volume_down));
    EXPECT_EQ(1, user_action_tester.GetActionCount("Accel_VolumeDown_F9"));
    EXPECT_EQ(volume_down, history->current_accelerator());

    EXPECT_EQ(0, user_action_tester.GetActionCount("Accel_VolumeUp_F10"));
    EXPECT_TRUE(ProcessInController(volume_up));
    EXPECT_EQ(volume_up, history->current_accelerator());
    EXPECT_EQ(1, user_action_tester.GetActionCount("Accel_VolumeUp_F10"));
  }
}

TEST_F(AcceleratorControllerTest, DisallowedWithNoWindow) {
  AccessibilityDelegate* delegate = Shell::Get()->accessibility_delegate();

  for (size_t i = 0; i < kActionsNeedingWindowLength; ++i) {
    delegate->TriggerAccessibilityAlert(A11Y_ALERT_NONE);
    EXPECT_TRUE(
        GetController()->PerformActionIfEnabled(kActionsNeedingWindow[i]));
    EXPECT_EQ(delegate->GetLastAccessibilityAlert(), A11Y_ALERT_WINDOW_NEEDED);
  }

  // Make sure we don't alert if we do have a window.
  std::unique_ptr<aura::Window> window;
  for (size_t i = 0; i < kActionsNeedingWindowLength; ++i) {
    window.reset(CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
    wm::ActivateWindow(window.get());
    delegate->TriggerAccessibilityAlert(A11Y_ALERT_NONE);
    GetController()->PerformActionIfEnabled(kActionsNeedingWindow[i]);
    EXPECT_NE(delegate->GetLastAccessibilityAlert(), A11Y_ALERT_WINDOW_NEEDED);
  }

  // Don't alert if we have a minimized window either.
  for (size_t i = 0; i < kActionsNeedingWindowLength; ++i) {
    window.reset(CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
    wm::ActivateWindow(window.get());
    GetController()->PerformActionIfEnabled(WINDOW_MINIMIZE);
    delegate->TriggerAccessibilityAlert(A11Y_ALERT_NONE);
    GetController()->PerformActionIfEnabled(kActionsNeedingWindow[i]);
    EXPECT_NE(delegate->GetLastAccessibilityAlert(), A11Y_ALERT_WINDOW_NEEDED);
  }
}

namespace {

// defines a class to test the behavior of deprecated accelerators.
class DeprecatedAcceleratorTester : public AcceleratorControllerTest {
 public:
  DeprecatedAcceleratorTester() {}
  ~DeprecatedAcceleratorTester() override {}

  void SetUp() override {
    AcceleratorControllerTest::SetUp();

    // For testing the deprecated and new IME shortcuts.
    DummyImeControlDelegate* delegate = new DummyImeControlDelegate;
    GetController()->SetImeControlDelegate(
        std::unique_ptr<ImeControlDelegate>(delegate));
  }

  ui::Accelerator CreateAccelerator(const AcceleratorData& data) const {
    ui::Accelerator result(data.keycode, data.modifiers);
    result.set_key_state(data.trigger_on_press
                             ? ui::Accelerator::KeyState::PRESSED
                             : ui::Accelerator::KeyState::RELEASED);
    return result;
  }

  void ResetStateIfNeeded() {
    if (Shell::Get()->session_controller()->IsScreenLocked() ||
        Shell::Get()->session_controller()->IsUserSessionBlocked()) {
      UnblockUserSession();
    }
  }

  bool ContainsDeprecatedAcceleratorNotification(const char* const id) const {
    return nullptr != message_center()->FindVisibleNotificationById(id);
  }

  bool IsMessageCenterEmpty() const {
    return message_center()->GetVisibleNotifications().empty();
  }

  void RemoveAllNotifications() const {
    message_center()->RemoveAllNotifications(
        false /* by_user */, message_center::MessageCenter::RemoveType::ALL);
  }

  message_center::MessageCenter* message_center() const {
    return message_center::MessageCenter::Get();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DeprecatedAcceleratorTester);
};

}  // namespace

TEST_F(DeprecatedAcceleratorTester, TestDeprecatedAcceleratorsBehavior) {
  // TODO: disabled because of UnblockUserSession() not working:
  // http://crbug.com/632201.
  if (Shell::GetAshConfig() == Config::MASH)
    return;
  for (size_t i = 0; i < kDeprecatedAcceleratorsLength; ++i) {
    const AcceleratorData& entry = kDeprecatedAccelerators[i];

    auto itr = GetController()->actions_with_deprecations_.find(entry.action);
    ASSERT_TRUE(itr != GetController()->actions_with_deprecations_.end());
    const DeprecatedAcceleratorData* data = itr->second;

    EXPECT_TRUE(IsMessageCenterEmpty());
    ui::Accelerator deprecated_accelerator = CreateAccelerator(entry);
    if (data->deprecated_enabled)
      EXPECT_TRUE(ProcessInController(deprecated_accelerator));
    else
      EXPECT_FALSE(ProcessInController(deprecated_accelerator));

    // We expect to see a notification in the message center.
    EXPECT_TRUE(
        ContainsDeprecatedAcceleratorNotification(data->uma_histogram_name));
    RemoveAllNotifications();

    // If the action is LOCK_SCREEN, we must reset the state by unlocking the
    // screen before we proceed testing the rest of accelerators.
    ResetStateIfNeeded();
  }
}

TEST_F(DeprecatedAcceleratorTester, TestNewAccelerators) {
  // Add below the new accelerators that replaced the deprecated ones (if any).
  const AcceleratorData kNewAccelerators[] = {
      {true, ui::VKEY_L, ui::EF_COMMAND_DOWN, LOCK_SCREEN},
      {true, ui::VKEY_SPACE, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN, NEXT_IME},
      {true, ui::VKEY_ESCAPE, ui::EF_COMMAND_DOWN, SHOW_TASK_MANAGER},
      {true, ui::VKEY_K, ui::EF_SHIFT_DOWN | ui::EF_COMMAND_DOWN,
       SHOW_IME_MENU_BUBBLE},
  };

  EXPECT_TRUE(IsMessageCenterEmpty());

  for (auto data : kNewAccelerators) {
    EXPECT_TRUE(ProcessInController(CreateAccelerator(data)));

    // Expect no notifications from the new accelerators.
    EXPECT_TRUE(IsMessageCenterEmpty());

    // If the action is LOCK_SCREEN, we must reset the state by unlocking the
    // screen before we proceed testing the rest of accelerators.
    ResetStateIfNeeded();
  }
}

}  // namespace ash
