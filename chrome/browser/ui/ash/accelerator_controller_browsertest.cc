// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/accelerators/accelerator_controller.h"

#include "ash/shell.h"
#include "ash/wm/window_state.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "ui/events/test/event_generator.h"

#if defined(USE_X11)
#include "ui/events/test/events_test_utils_x11.h"
#endif

#if defined(USE_X11)
typedef InProcessBrowserTest AcceleratorControllerBrowserTest;

// Test that pressing and holding Alt+ toggles the maximized state exactly once.
// This test is a browser test to test that the EF_IS_REPEAT flag is correctly
// passed down to AcceleratorController (via a conversion to WebInputEvent).
// See crbug.com/434743
IN_PROC_BROWSER_TEST_F(AcceleratorControllerBrowserTest,
                       RepeatedToggleMaximizeViaAccelerator) {
  ASSERT_TRUE(ash::Shell::HasInstance()) << "No Instance";
  AddTabAtIndex(0,
                GURL("data:text/html;base64,<html></html>"),
                ui::PAGE_TRANSITION_TYPED);
  browser()->window()->Show();

  ui::Accelerator accelerator(ui::VKEY_OEM_PLUS, ui::EF_ALT_DOWN);
  ash::AcceleratorController* accelerator_controller =
      ash::Shell::Get()->accelerator_controller();
  ASSERT_TRUE(accelerator_controller->IsRegistered(accelerator));

  ash::wm::WindowState* window_state = ash::wm::GetActiveWindowState();
  EXPECT_FALSE(window_state->IsMaximized());

  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());
  generator.PressKey(ui::VKEY_OEM_PLUS, ui::EF_ALT_DOWN);
  EXPECT_TRUE(window_state->IsMaximized());

  generator.PressKey(ui::VKEY_OEM_PLUS, ui::EF_ALT_DOWN);

  generator.ReleaseKey(ui::VKEY_OEM_PLUS, ui::EF_ALT_DOWN);
  EXPECT_TRUE(window_state->IsMaximized());
}
#endif
