// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/exo/shell_surface.h"

#include "ash/accessibility_delegate.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/public/interfaces/window_pin_type.mojom.h"
#include "ash/shell.h"
#include "ash/shell_port.h"
#include "ash/test/shell_test_api.h"
#include "ash/test/workspace_controller_test_api.h"
#include "ash/wm/window_state.h"
#include "ash/wm/wm_event.h"
#include "ash/wm/workspace/workspace_window_resizer.h"
#include "ash/wm_window.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "components/exo/buffer.h"
#include "components/exo/display.h"
#include "components/exo/sub_surface.h"
#include "components/exo/surface.h"
#include "components/exo/test/exo_test_base.h"
#include "components/exo/test/exo_test_helper.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window.h"
#include "ui/base/hit_test.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/shadow.h"
#include "ui/wm/core/shadow_controller.h"
#include "ui/wm/core/shadow_types.h"
#include "ui/wm/core/window_util.h"

namespace exo {
namespace {

using ShellSurfaceTest = test::ExoTestBase;

uint32_t ConfigureFullscreen(uint32_t serial,
                             const gfx::Size& size,
                             ash::wm::WindowStateType state_type,
                             bool resizing,
                             bool activated,
                             const gfx::Vector2d& origin_offset) {
  EXPECT_EQ(ash::wm::WINDOW_STATE_TYPE_FULLSCREEN, state_type);
  return serial;
}

wm::ShadowElevation GetShadowElevation(aura::Window* window) {
  return window->GetProperty(wm::kShadowElevationKey);
}

bool IsWidgetPinned(views::Widget* widget) {
  ash::mojom::WindowPinType type =
      widget->GetNativeWindow()->GetProperty(ash::kWindowPinTypeKey);
  return type == ash::mojom::WindowPinType::PINNED ||
         type == ash::mojom::WindowPinType::TRUSTED_PINNED;
}

TEST_F(ShellSurfaceTest, AcknowledgeConfigure) {
  gfx::Size buffer_size(32, 32);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  surface->Attach(buffer.get());
  surface->Commit();

  gfx::Point origin(100, 100);
  shell_surface->GetWidget()->SetBounds(gfx::Rect(origin, buffer_size));
  EXPECT_EQ(origin.ToString(),
            surface->window()->GetBoundsInRootWindow().origin().ToString());

  const uint32_t kSerial = 1;
  shell_surface->set_configure_callback(
      base::Bind(&ConfigureFullscreen, kSerial));
  shell_surface->SetFullscreen(true);

  // Surface origin should not change until configure request is acknowledged.
  EXPECT_EQ(origin.ToString(),
            surface->window()->GetBoundsInRootWindow().origin().ToString());

  shell_surface->AcknowledgeConfigure(kSerial);
  std::unique_ptr<Buffer> fullscreen_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(
          CurrentContext()->bounds().size())));
  surface->Attach(fullscreen_buffer.get());
  surface->Commit();

  EXPECT_EQ(gfx::Point().ToString(),
            surface->window()->GetBoundsInRootWindow().origin().ToString());
}

TEST_F(ShellSurfaceTest, SetParent) {
  gfx::Size buffer_size(256, 256);
  std::unique_ptr<Buffer> parent_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> parent_surface(new Surface);
  std::unique_ptr<ShellSurface> parent_shell_surface(
      new ShellSurface(parent_surface.get()));

  parent_surface->Attach(parent_buffer.get());
  parent_surface->Commit();

  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  shell_surface->SetParent(parent_shell_surface.get());

  surface->Attach(buffer.get());
  surface->Commit();
  EXPECT_EQ(
      parent_shell_surface->GetWidget()->GetNativeWindow(),
      wm::GetTransientParent(shell_surface->GetWidget()->GetNativeWindow()));
}

TEST_F(ShellSurfaceTest, Maximize) {
  gfx::Size buffer_size(256, 256);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  surface->Attach(buffer.get());
  shell_surface->Maximize();
  surface->Commit();
  EXPECT_EQ(CurrentContext()->bounds().width(),
            shell_surface->GetWidget()->GetWindowBoundsInScreen().width());
}

TEST_F(ShellSurfaceTest, Minimize) {
  gfx::Size buffer_size(256, 256);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  // Minimizing can be performed before the surface is committed.
  shell_surface->Minimize();
  EXPECT_TRUE(shell_surface->GetWidget()->IsMinimized());

  // Confirm that attaching and commiting doesn't reset the state.
  surface->Attach(buffer.get());
  surface->Commit();
  EXPECT_TRUE(shell_surface->GetWidget()->IsMinimized());

  shell_surface->Restore();
  EXPECT_FALSE(shell_surface->GetWidget()->IsMinimized());

  shell_surface->Minimize();
  EXPECT_TRUE(shell_surface->GetWidget()->IsMinimized());
}

TEST_F(ShellSurfaceTest, Restore) {
  gfx::Size buffer_size(256, 256);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  surface->Attach(buffer.get());
  surface->Commit();
  // Note: Remove contents to avoid issues with maximize animations in tests.
  shell_surface->Maximize();
  shell_surface->Restore();
  EXPECT_EQ(
      buffer_size.ToString(),
      shell_surface->GetWidget()->GetWindowBoundsInScreen().size().ToString());
}

TEST_F(ShellSurfaceTest, SetFullscreen) {
  gfx::Size buffer_size(256, 256);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  shell_surface->SetFullscreen(true);
  surface->Attach(buffer.get());
  surface->Commit();
  EXPECT_EQ(CurrentContext()->bounds().ToString(),
            shell_surface->GetWidget()->GetWindowBoundsInScreen().ToString());
}

TEST_F(ShellSurfaceTest, SetPinned) {
  gfx::Size buffer_size(256, 256);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  shell_surface->SetPinned(ash::mojom::WindowPinType::TRUSTED_PINNED);
  EXPECT_TRUE(IsWidgetPinned(shell_surface->GetWidget()));

  shell_surface->SetPinned(ash::mojom::WindowPinType::NONE);
  EXPECT_FALSE(IsWidgetPinned(shell_surface->GetWidget()));

  shell_surface->SetPinned(ash::mojom::WindowPinType::PINNED);
  EXPECT_TRUE(IsWidgetPinned(shell_surface->GetWidget()));

  shell_surface->SetPinned(ash::mojom::WindowPinType::NONE);
  EXPECT_FALSE(IsWidgetPinned(shell_surface->GetWidget()));
}

TEST_F(ShellSurfaceTest, SetSystemUiVisibility) {
  gfx::Size buffer_size(256, 256);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  surface->Attach(buffer.get());
  surface->Commit();

  shell_surface->SetSystemUiVisibility(true);
  EXPECT_TRUE(
      ash::wm::GetWindowState(shell_surface->GetWidget()->GetNativeWindow())
          ->autohide_shelf_when_maximized_or_fullscreen());

  shell_surface->SetSystemUiVisibility(false);
  EXPECT_FALSE(
      ash::wm::GetWindowState(shell_surface->GetWidget()->GetNativeWindow())
          ->autohide_shelf_when_maximized_or_fullscreen());
}

TEST_F(ShellSurfaceTest, SetTitle) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  shell_surface->SetTitle(base::string16(base::ASCIIToUTF16("test")));
  surface->Commit();
}

TEST_F(ShellSurfaceTest, SetApplicationId) {
  gfx::Size buffer_size(64, 64);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  EXPECT_EQ(nullptr, shell_surface->GetWidget());
  shell_surface->SetApplicationId("pre-widget-id");

  surface->Attach(buffer.get());
  surface->Commit();
  aura::Window* window = shell_surface->GetWidget()->GetNativeWindow();
  EXPECT_EQ("pre-widget-id", ShellSurface::GetApplicationId(window));
  shell_surface->SetApplicationId("test");
  EXPECT_EQ("test", ShellSurface::GetApplicationId(window));
}

TEST_F(ShellSurfaceTest, Move) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  // Map shell surface.
  surface->Commit();

  // The interactive move should end when surface is destroyed.
  shell_surface->Move();

  // Test that destroying the shell surface before move ends is OK.
  shell_surface.reset();
}

TEST_F(ShellSurfaceTest, Resize) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  // Map shell surface.
  surface->Commit();

  // The interactive resize should end when surface is destroyed.
  shell_surface->Resize(HTBOTTOMRIGHT);

  // Test that destroying the surface before resize ends is OK.
  surface.reset();
}

TEST_F(ShellSurfaceTest, SetGeometry) {
  gfx::Size buffer_size(64, 64);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  gfx::Rect geometry(16, 16, 32, 32);
  shell_surface->SetGeometry(geometry);
  surface->Attach(buffer.get());
  surface->Commit();
  EXPECT_EQ(
      geometry.size().ToString(),
      shell_surface->GetWidget()->GetWindowBoundsInScreen().size().ToString());
  EXPECT_EQ(gfx::Rect(gfx::Point() - geometry.OffsetFromOrigin(), buffer_size)
                .ToString(),
            surface->window()->bounds().ToString());
}

TEST_F(ShellSurfaceTest, SetScale) {
  gfx::Size buffer_size(64, 64);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  double scale = 1.5;
  shell_surface->SetScale(scale);
  surface->Attach(buffer.get());
  surface->Commit();

  gfx::Transform transform;
  transform.Scale(1.0 / scale, 1.0 / scale);
  EXPECT_EQ(transform.ToString(),
            surface->window()->layer()->GetTargetTransform().ToString());
}

TEST_F(ShellSurfaceTest, SetTopInset) {
  gfx::Size buffer_size(64, 64);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  surface->Attach(buffer.get());
  surface->Commit();

  aura::Window* window = shell_surface->GetWidget()->GetNativeWindow();
  ASSERT_TRUE(window);
  EXPECT_EQ(0, window->GetProperty(aura::client::kTopViewInset));
  int top_inset_height = 20;
  shell_surface->SetTopInset(top_inset_height);
  surface->Commit();
  EXPECT_EQ(top_inset_height, window->GetProperty(aura::client::kTopViewInset));
}

void Close(int* close_call_count) {
  (*close_call_count)++;
}

TEST_F(ShellSurfaceTest, CloseCallback) {
  gfx::Size buffer_size(64, 64);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  int close_call_count = 0;
  shell_surface->set_close_callback(
      base::Bind(&Close, base::Unretained(&close_call_count)));

  surface->Attach(buffer.get());
  surface->Commit();

  EXPECT_EQ(0, close_call_count);
  shell_surface->GetWidget()->Close();
  EXPECT_EQ(1, close_call_count);

  ui::KeyEvent event(ui::ET_KEY_PRESSED, ui::VKEY_W, ui::EF_CONTROL_DOWN);
  shell_surface->GetWidget()->OnKeyEvent(&event);
  EXPECT_EQ(2, close_call_count);
}

void DestroyShellSurface(std::unique_ptr<ShellSurface>* shell_surface) {
  shell_surface->reset();
}

TEST_F(ShellSurfaceTest, SurfaceDestroyedCallback) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  shell_surface->set_surface_destroyed_callback(
      base::Bind(&DestroyShellSurface, base::Unretained(&shell_surface)));

  surface->Commit();

  EXPECT_TRUE(shell_surface.get());
  surface.reset();
  EXPECT_FALSE(shell_surface.get());
}

uint32_t Configure(gfx::Size* suggested_size,
                   ash::wm::WindowStateType* has_state_type,
                   bool* is_resizing,
                   bool* is_active,
                   const gfx::Size& size,
                   ash::wm::WindowStateType state_type,
                   bool resizing,
                   bool activated,
                   const gfx::Vector2d& origin_offset) {
  *suggested_size = size;
  *has_state_type = state_type;
  *is_resizing = resizing;
  *is_active = activated;
  return 0;
}

TEST_F(ShellSurfaceTest, ConfigureCallback) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  gfx::Size suggested_size;
  ash::wm::WindowStateType has_state_type = ash::wm::WINDOW_STATE_TYPE_NORMAL;
  bool is_resizing = false;
  bool is_active = false;
  shell_surface->set_configure_callback(
      base::Bind(&Configure, base::Unretained(&suggested_size),
                 base::Unretained(&has_state_type),
                 base::Unretained(&is_resizing), base::Unretained(&is_active)));
  shell_surface->Maximize();
  shell_surface->AcknowledgeConfigure(0);
  EXPECT_EQ(CurrentContext()->bounds().width(), suggested_size.width());
  EXPECT_EQ(ash::wm::WINDOW_STATE_TYPE_MAXIMIZED, has_state_type);
  shell_surface->Restore();
  shell_surface->AcknowledgeConfigure(0);

  shell_surface->SetFullscreen(true);
  shell_surface->AcknowledgeConfigure(0);
  EXPECT_EQ(CurrentContext()->bounds().size().ToString(),
            suggested_size.ToString());
  EXPECT_EQ(ash::wm::WINDOW_STATE_TYPE_FULLSCREEN, has_state_type);
  shell_surface->SetFullscreen(false);
  shell_surface->AcknowledgeConfigure(0);

  gfx::Size buffer_size(64, 64);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  surface->Attach(buffer.get());
  surface->Commit();
  shell_surface->GetWidget()->Activate();
  shell_surface->AcknowledgeConfigure(0);
  EXPECT_TRUE(is_active);
  shell_surface->GetWidget()->Deactivate();
  shell_surface->AcknowledgeConfigure(0);
  EXPECT_FALSE(is_active);

  EXPECT_FALSE(is_resizing);
  shell_surface->Resize(HTBOTTOMRIGHT);
  shell_surface->AcknowledgeConfigure(0);
  EXPECT_TRUE(is_resizing);
}

TEST_F(ShellSurfaceTest, ModalWindow) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(
      surface.get(), nullptr, ShellSurface::BoundsMode::SHELL, gfx::Point(),
      true, false, ash::kShellWindowId_SystemModalContainer));
  gfx::Size desktop_size(640, 480);
  std::unique_ptr<Buffer> desktop_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(desktop_size)));
  surface->Attach(desktop_buffer.get());
  surface->SetInputRegion(SkRegion());
  surface->Commit();

  EXPECT_FALSE(ash::ShellPort::Get()->IsSystemModalWindowOpen());

  // Creating a surface without input region should not make it modal.
  std::unique_ptr<Display> display(new Display);
  std::unique_ptr<Surface> child = display->CreateSurface();
  gfx::Size buffer_size(128, 128);
  std::unique_ptr<Buffer> child_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  child->Attach(child_buffer.get());
  std::unique_ptr<SubSurface> sub_surface(
      display->CreateSubSurface(child.get(), surface.get()));
  surface->SetSubSurfacePosition(child.get(), gfx::Point(10, 10));
  child->Commit();
  surface->Commit();
  EXPECT_FALSE(ash::ShellPort::Get()->IsSystemModalWindowOpen());

  // Making the surface opaque shouldn't make it modal either.
  child->SetBlendMode(SkBlendMode::kSrc);
  child->Commit();
  surface->Commit();
  EXPECT_FALSE(ash::ShellPort::Get()->IsSystemModalWindowOpen());

  // Setting input regions won't make it modal either.
  surface->SetInputRegion(
      SkRegion(gfx::RectToSkIRect(gfx::Rect(10, 10, 100, 100))));
  surface->Commit();
  EXPECT_FALSE(ash::ShellPort::Get()->IsSystemModalWindowOpen());

  // Only SetSystemModal changes modality.
  shell_surface->SetSystemModal(true);
  EXPECT_TRUE(ash::ShellPort::Get()->IsSystemModalWindowOpen());

  shell_surface->SetSystemModal(false);
  EXPECT_FALSE(ash::ShellPort::Get()->IsSystemModalWindowOpen());
}

TEST_F(ShellSurfaceTest, ModalWindowSetSystemModalBeforeCommit) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(
      surface.get(), nullptr, ShellSurface::BoundsMode::SHELL, gfx::Point(),
      true, false, ash::kShellWindowId_SystemModalContainer));
  gfx::Size desktop_size(640, 480);
  std::unique_ptr<Buffer> desktop_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(desktop_size)));
  surface->Attach(desktop_buffer.get());
  surface->SetInputRegion(SkRegion());

  // Set SetSystemModal before any commit happens. Widget is not created at
  // this time.
  EXPECT_FALSE(shell_surface->GetWidget());
  shell_surface->SetSystemModal(true);

  surface->Commit();

  // It is expected that modal window is shown.
  EXPECT_TRUE(shell_surface->GetWidget());
  EXPECT_TRUE(ash::ShellPort::Get()->IsSystemModalWindowOpen());

  // Now widget is created and setting modal state should be applied
  // immediately.
  shell_surface->SetSystemModal(false);
  EXPECT_FALSE(ash::ShellPort::Get()->IsSystemModalWindowOpen());
}

TEST_F(ShellSurfaceTest, PopupWindow) {
  Surface parent_surface;
  ShellSurface parent(&parent_surface);
  const gfx::Rect parent_bounds(100, 100, 300, 300);

  Buffer parent_buffer(
      exo_test_helper()->CreateGpuMemoryBuffer(parent_bounds.size()));
  parent_surface.Attach(&parent_buffer);
  parent_surface.Commit();

  parent.GetWidget()->SetBounds(parent_bounds);

  Display display;
  Surface popup_surface;
  const gfx::Rect popup_bounds(10, 10, 100, 100);
  std::unique_ptr<ShellSurface> popup = display.CreatePopupShellSurface(
      &popup_surface, &parent, popup_bounds.origin());

  Buffer popup_buffer(
      exo_test_helper()->CreateGpuMemoryBuffer(popup_bounds.size()));
  popup_surface.Attach(&popup_buffer);
  popup_surface.Commit();

  // Popup bounds are relative to parent.
  EXPECT_EQ(gfx::Rect(parent_bounds.origin() + popup_bounds.OffsetFromOrigin(),
                      popup_bounds.size()),
            popup->GetWidget()->GetWindowBoundsInScreen());

  const gfx::Rect geometry(5, 5, 90, 90);
  popup->SetGeometry(geometry);
  popup_surface.Commit();

  // Popup position is fixed, and geometry is relative to it.
  EXPECT_EQ(gfx::Rect(parent_bounds.origin() +
                      popup_bounds.OffsetFromOrigin() +
                      geometry.OffsetFromOrigin(),
                      geometry.size()),
            popup->GetWidget()->GetWindowBoundsInScreen());
}

TEST_F(ShellSurfaceTest, SurfaceShadow) {
  gfx::Size buffer_size(128, 128);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(
      surface.get(), nullptr, ShellSurface::BoundsMode::SHELL, gfx::Point(),
      true, false, ash::kShellWindowId_DefaultContainer));
  surface->Attach(buffer.get());
  surface->Commit();

  aura::Window* window = shell_surface->GetWidget()->GetNativeWindow();

  // 1) Initial state, no shadow.
  wm::Shadow* shadow = wm::ShadowController::GetShadowForWindow(window);
  ASSERT_TRUE(shadow);
  EXPECT_FALSE(shadow->layer()->visible());

  std::unique_ptr<Display> display(new Display);

  // 2) Just creating a sub surface won't create a shadow.
  std::unique_ptr<Surface> child = display->CreateSurface();
  std::unique_ptr<Buffer> child_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  child->Attach(child_buffer.get());
  std::unique_ptr<SubSurface> sub_surface(
      display->CreateSubSurface(child.get(), surface.get()));
  surface->Commit();

  EXPECT_FALSE(shadow->layer()->visible());

  // 3) Create a shadow.
  shell_surface->SetRectangularSurfaceShadow(gfx::Rect(10, 10, 100, 100));
  surface->Commit();
  EXPECT_TRUE(shadow->layer()->visible());

  gfx::Rect before = shadow->layer()->bounds();

  // 4) Shadow bounds is independent of the sub surface.
  gfx::Size new_buffer_size(256, 256);
  std::unique_ptr<Buffer> new_child_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(new_buffer_size)));
  child->Attach(new_child_buffer.get());
  child->Commit();
  surface->Commit();

  EXPECT_EQ(before, shadow->layer()->bounds());

  // 4) Updating the widget's window bounds should not change the shadow bounds.
  window->SetBounds(gfx::Rect(10, 10, 100, 100));
  EXPECT_EQ(before, shadow->layer()->bounds());

  // 5) This should disable shadow.
  shell_surface->SetRectangularSurfaceShadow(gfx::Rect());
  surface->Commit();

  EXPECT_EQ(wm::ShadowElevation::NONE, GetShadowElevation(window));
  EXPECT_FALSE(shadow->layer()->visible());

  // 6) This should enable non surface shadow again.
  shell_surface->SetRectangularSurfaceShadow(gfx::Rect(10, 10, 100, 100));
  surface->Commit();

  EXPECT_EQ(wm::ShadowElevation::DEFAULT, GetShadowElevation(window));
  EXPECT_TRUE(shadow->layer()->visible());

  // For surface shadow, the underlay is placed at the bottom of shell surfaces.
  EXPECT_EQ(surface->window(), shell_surface->shadow_underlay()->parent());
  EXPECT_EQ(window, shell_surface->shadow_overlay()->parent());

  EXPECT_EQ(*surface->window()->children().begin(),
            shell_surface->shadow_underlay());
}

TEST_F(ShellSurfaceTest, NonSurfaceShadow) {
  gfx::Size buffer_size(128, 128);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(
      surface.get(), nullptr, ShellSurface::BoundsMode::SHELL, gfx::Point(),
      true, false, ash::kShellWindowId_DefaultContainer));
  surface->Attach(buffer.get());
  surface->Commit();

  aura::Window* window = shell_surface->GetWidget()->GetNativeWindow();

  // 1) Initial state, no shadow.
  wm::Shadow* shadow = wm::ShadowController::GetShadowForWindow(window);
  ASSERT_TRUE(shadow);
  EXPECT_FALSE(shadow->layer()->visible());

  std::unique_ptr<Display> display(new Display);

  // 2) Just creating a sub surface won't create a shadow.
  std::unique_ptr<Surface> child = display->CreateSurface();
  std::unique_ptr<Buffer> child_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  child->Attach(child_buffer.get());
  std::unique_ptr<SubSurface> sub_surface(
      display->CreateSubSurface(child.get(), surface.get()));
  surface->Commit();

  EXPECT_FALSE(shadow->layer()->visible());

  // 3) Enable a shadow.
  shell_surface->SetRectangularShadowEnabled(true);
  surface->Commit();
  EXPECT_TRUE(shadow->layer()->visible());

  gfx::Rect before = shadow->layer()->bounds();

  // 4) Shadow bounds is independent of the sub surface.
  gfx::Size new_buffer_size(256, 256);
  std::unique_ptr<Buffer> new_child_buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(new_buffer_size)));
  child->Attach(new_child_buffer.get());
  child->Commit();
  surface->Commit();

  EXPECT_EQ(before, shadow->layer()->bounds());

  // 4) Updating the widget's window bounds should change the non surface shadow
  // bounds.
  const gfx::Rect new_bounds(50, 50, 100, 100);
  window->SetBounds(new_bounds);
  EXPECT_NE(before, shadow->layer()->bounds());
  EXPECT_NE(new_bounds, shadow->layer()->bounds());

  // 5) This should disable shadow.
  shell_surface->SetRectangularShadowEnabled(false);
  surface->Commit();

  EXPECT_EQ(wm::ShadowElevation::NONE, GetShadowElevation(window));
  EXPECT_FALSE(shadow->layer()->visible());

  // 6) This should enable non surface shadow.
  shell_surface->SetRectangularShadowEnabled(true);
  surface->Commit();

  EXPECT_EQ(wm::ShadowElevation::DEFAULT, GetShadowElevation(window));
  EXPECT_TRUE(shadow->layer()->visible());

  // For no surface shadow, both of underlay and overlay should be stacked
  // below the surface window.
  EXPECT_EQ(window, shell_surface->shadow_underlay()->parent());
  EXPECT_EQ(window, shell_surface->shadow_overlay()->parent());

  // Shadow overlay should be stacked just above the shadow underlay.
  auto underlay_it =
      std::find(window->children().begin(), window->children().end(),
                shell_surface->shadow_underlay());
  ASSERT_NE(underlay_it, window->children().end());
  ASSERT_NE(std::next(underlay_it), window->children().end());
  EXPECT_EQ(*std::next(underlay_it), shell_surface->shadow_overlay());
}

TEST_F(ShellSurfaceTest, ShadowWithStateChange) {
  gfx::Size buffer_size(64, 64);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(
      surface.get(), nullptr, ShellSurface::BoundsMode::CLIENT, gfx::Point(),
      true, false, ash::kShellWindowId_DefaultContainer));

  // Postion the widget at 10,10 so that we get non zero offset.
  const gfx::Size content_size(100, 100);
  const gfx::Rect original_bounds(gfx::Point(10, 10), content_size);
  shell_surface->SetGeometry(original_bounds);
  surface->Attach(buffer.get());
  surface->Commit();

  // Placing a shadow at screen origin will make the shadow's origin (-10, -10).
  const gfx::Rect shadow_bounds(content_size);

  // Expected shadow position/bounds in parent coordinates.
  const gfx::Point expected_shadow_origin(-10, -10);
  const gfx::Rect expected_shadow_bounds(expected_shadow_origin, content_size);

  views::Widget* widget = shell_surface->GetWidget();
  aura::Window* window = widget->GetNativeWindow();
  wm::Shadow* shadow = wm::ShadowController::GetShadowForWindow(window);

  shell_surface->SetRectangularSurfaceShadow(shadow_bounds);
  surface->Commit();
  EXPECT_EQ(wm::ShadowElevation::DEFAULT, GetShadowElevation(window));

  // Shadow overlay bounds.
  EXPECT_TRUE(shadow->layer()->visible());
  // Origin must be in sync.
  EXPECT_EQ(expected_shadow_origin,
            shadow->layer()->parent()->bounds().origin());

  const gfx::Rect work_area =
      display::Screen::GetScreen()->GetPrimaryDisplay().work_area();
  // Maximizing window hides the shadow.
  widget->Maximize();
  ASSERT_TRUE(widget->IsMaximized());
  EXPECT_FALSE(shadow->layer()->visible());

  shell_surface->SetRectangularSurfaceShadow(work_area);
  surface->Commit();
  EXPECT_FALSE(shadow->layer()->visible());

  // Restoring bounds will re-enable shadow. It's content size is set to work
  // area,/ thus not visible until new bounds is committed.
  widget->Restore();
  EXPECT_TRUE(shadow->layer()->visible());
  const gfx::Rect shadow_in_maximized(expected_shadow_origin, work_area.size());
  EXPECT_EQ(shadow_in_maximized, shadow->layer()->parent()->bounds());

  // The bounds is updated.
  shell_surface->SetRectangularSurfaceShadow(shadow_bounds);
  surface->Commit();
  EXPECT_EQ(expected_shadow_bounds, shadow->layer()->parent()->bounds());
}

TEST_F(ShellSurfaceTest, ShadowWithTransform) {
  gfx::Size buffer_size(64, 64);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(
      surface.get(), nullptr, ShellSurface::BoundsMode::CLIENT, gfx::Point(),
      true, false, ash::kShellWindowId_DefaultContainer));

  // Postion the widget at 10,10 so that we get non zero offset.
  const gfx::Size content_size(100, 100);
  const gfx::Rect original_bounds(gfx::Point(10, 10), content_size);
  shell_surface->SetGeometry(original_bounds);
  surface->Attach(buffer.get());
  surface->Commit();

  aura::Window* window = shell_surface->GetWidget()->GetNativeWindow();
  wm::Shadow* shadow = wm::ShadowController::GetShadowForWindow(window);

  // Placing a shadow at screen origin will make the shadow's origin (-10, -10).
  const gfx::Rect shadow_bounds(content_size);

  // Shadow bounds relative to its parent should not be affected by a transform.
  gfx::Transform transform;
  transform.Translate(50, 50);
  window->SetTransform(transform);
  shell_surface->SetRectangularSurfaceShadow(shadow_bounds);
  surface->Commit();
  EXPECT_TRUE(shadow->layer()->visible());
  EXPECT_EQ(gfx::Rect(-10, -10, 100, 100), shadow->layer()->parent()->bounds());
}

TEST_F(ShellSurfaceTest, ShadowStartMaximized) {
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(
      surface.get(), nullptr, ShellSurface::BoundsMode::CLIENT, gfx::Point(),
      true, false, ash::kShellWindowId_DefaultContainer));
  shell_surface->Maximize();
  views::Widget* widget = shell_surface->GetWidget();
  aura::Window* window = widget->GetNativeWindow();

  // There is no shadow when started in maximized state.
  EXPECT_FALSE(wm::ShadowController::GetShadowForWindow(window));

  // Sending a shadow bounds in maximized state won't create a shaodw.
  shell_surface->SetRectangularSurfaceShadow(gfx::Rect(10, 10, 100, 100));
  surface->Commit();

  EXPECT_FALSE(wm::ShadowController::GetShadowForWindow(window));
  // Underlay should be created even without shadow.
  ASSERT_TRUE(shell_surface->shadow_underlay());
  EXPECT_TRUE(shell_surface->shadow_underlay()->IsVisible());

  shell_surface->SetRectangularSurfaceShadow(gfx::Rect(0, 0, 0, 0));
  // Underlay should be created even without shadow.
  ASSERT_TRUE(shell_surface->shadow_underlay());
  EXPECT_TRUE(shell_surface->shadow_underlay()->IsVisible());
  shell_surface->SetRectangularShadowEnabled(false);
  surface->Commit();
  // No underlay if there is no shadow.
  EXPECT_FALSE(shell_surface->shadow_underlay());

  shell_surface->SetRectangularShadowEnabled(true);
  shell_surface->SetRectangularSurfaceShadow(gfx::Rect(10, 10, 100, 100));
  surface->Commit();

  // Restore the window and make sure the shadow is created, visible and
  // has the latest bounds.
  widget->Restore();
  wm::Shadow* shadow = wm::ShadowController::GetShadowForWindow(window);
  ASSERT_TRUE(shadow);
  EXPECT_TRUE(shadow->layer()->visible());
  EXPECT_EQ(gfx::Rect(10, 10, 100, 100), shadow->layer()->parent()->bounds());
}

TEST_F(ShellSurfaceTest, ToggleFullscreen) {
  gfx::Size buffer_size(256, 256);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));

  surface->Attach(buffer.get());
  surface->Commit();
  EXPECT_EQ(
      buffer_size.ToString(),
      shell_surface->GetWidget()->GetWindowBoundsInScreen().size().ToString());

  shell_surface->Maximize();
  EXPECT_EQ(CurrentContext()->bounds().width(),
            shell_surface->GetWidget()->GetWindowBoundsInScreen().width());

  ash::wm::WMEvent event(ash::wm::WM_EVENT_TOGGLE_FULLSCREEN);
  ash::WmWindow* window =
      ash::WmWindow::Get(shell_surface->GetWidget()->GetNativeWindow());

  // Enter fullscreen mode.
  window->GetWindowState()->OnWMEvent(&event);

  EXPECT_EQ(CurrentContext()->bounds().ToString(),
            shell_surface->GetWidget()->GetWindowBoundsInScreen().ToString());

  // Leave fullscreen mode.
  window->GetWindowState()->OnWMEvent(&event);

  // Check that shell surface is maximized.
  EXPECT_EQ(CurrentContext()->bounds().width(),
            shell_surface->GetWidget()->GetWindowBoundsInScreen().width());
}

TEST_F(ShellSurfaceTest, MaximizedAndImmersiveFullscreenBackdrop) {
  ash::WorkspaceController* wc =
      ash::test::ShellTestApi(ash::Shell::Get()).workspace_controller();
  ash::test::WorkspaceControllerTestApi test_helper(wc);

  const gfx::Size display_size =
      display::Screen::GetScreen()->GetPrimaryDisplay().size();
  const gfx::Size buffer_size(display_size);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(
      surface.get(), nullptr, ShellSurface::BoundsMode::CLIENT, gfx::Point(),
      true, false, ash::kShellWindowId_DefaultContainer));

  surface->Attach(buffer.get());

  gfx::Rect shadow_bounds(10, 10, 100, 100);
  shell_surface->SetGeometry(shadow_bounds);
  shell_surface->SetRectangularSurfaceShadow(shadow_bounds);
  surface->Commit();
  EXPECT_EQ(shadow_bounds,
            shell_surface->GetWidget()->GetWindowBoundsInScreen());
  ASSERT_EQ(shadow_bounds, shell_surface->shadow_underlay()->bounds());
  EXPECT_EQ(display::Screen::GetScreen()->GetPrimaryDisplay().size(),
            shell_surface->surface_for_testing()->window()->bounds().size());

  EXPECT_FALSE(test_helper.GetBackdropWindow());

  ash::wm::WMEvent fullscreen_event(ash::wm::WM_EVENT_TOGGLE_FULLSCREEN);
  ash::WmWindow* window =
      ash::WmWindow::Get(shell_surface->GetWidget()->GetNativeWindow());

  // Enter immersive fullscreen mode. Shadow underlay is fullscreen.
  window->GetWindowState()->OnWMEvent(&fullscreen_event);

  EXPECT_TRUE(test_helper.GetBackdropWindow());

  // Leave fullscreen mode. Shadow underlay is restored.
  window->GetWindowState()->OnWMEvent(&fullscreen_event);
  EXPECT_FALSE(test_helper.GetBackdropWindow());

  ash::wm::WMEvent maximize_event(ash::wm::WM_EVENT_TOGGLE_MAXIMIZE);

  // Enter maximized mode.
  window->GetWindowState()->OnWMEvent(&maximize_event);
  EXPECT_TRUE(test_helper.GetBackdropWindow());

  // Leave maximized mode.
  window->GetWindowState()->OnWMEvent(&maximize_event);
  EXPECT_FALSE(test_helper.GetBackdropWindow());
}

// Make sure that a surface shell started in maximize creates deprecated
// shadow correctly.
TEST_F(ShellSurfaceTest,
       StartMaximizedThenMinimizeWithSetRectangularShadow_DEPRECATED) {
  const gfx::Size display_size =
      display::Screen::GetScreen()->GetPrimaryDisplay().size();
  const gfx::Size buffer_size(display_size);
  std::unique_ptr<Buffer> buffer(
      new Buffer(exo_test_helper()->CreateGpuMemoryBuffer(buffer_size)));
  std::unique_ptr<Surface> surface(new Surface);
  std::unique_ptr<ShellSurface> shell_surface(new ShellSurface(
      surface.get(), nullptr, ShellSurface::BoundsMode::CLIENT, gfx::Point(),
      true, false, ash::kShellWindowId_DefaultContainer));

  // Start in maximized.
  shell_surface->Maximize();
  surface->Attach(buffer.get());
  surface->Commit();

  gfx::Rect shadow_bounds =
      display::Screen::GetScreen()->GetPrimaryDisplay().bounds();
  shell_surface->SetGeometry(shadow_bounds);
  shell_surface->SetRectangularShadow_DEPRECATED(shadow_bounds);
  surface->Commit();
  EXPECT_EQ(shadow_bounds,
            shell_surface->GetWidget()->GetWindowBoundsInScreen());
  ASSERT_EQ(shadow_bounds, shell_surface->shadow_underlay()->bounds());
  EXPECT_EQ(display::Screen::GetScreen()->GetPrimaryDisplay().size(),
            shell_surface->surface_for_testing()->window()->bounds().size());

  ash::wm::WMEvent minimize_event(ash::wm::WM_EVENT_MINIMIZE);
  ash::WmWindow* window =
      ash::WmWindow::Get(shell_surface->GetWidget()->GetNativeWindow());
  window->GetWindowState()->OnWMEvent(&minimize_event);
}

}  // namespace
}  // namespace exo
