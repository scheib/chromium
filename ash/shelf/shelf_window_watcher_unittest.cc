// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shelf/shelf_window_watcher.h"

#include "ash/public/cpp/config.h"
#include "ash/public/cpp/shelf_item.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/root_window_controller.h"
#include "ash/session/session_controller.h"
#include "ash/shelf/shelf_model.h"
#include "ash/shell.h"
#include "ash/shell_port.h"
#include "ash/test/ash_test_base.h"
#include "ash/wm/window_resizer.h"
#include "ash/wm/window_state.h"
#include "ash/wm_window.h"
#include "base/strings/string_number_conversions.h"
#include "ui/aura/window.h"
#include "ui/base/hit_test.h"
#include "ui/views/widget/widget.h"

namespace ash {

class ShelfWindowWatcherTest : public test::AshTestBase {
 public:
  ShelfWindowWatcherTest() : model_(nullptr) {}
  ~ShelfWindowWatcherTest() override {}

  void SetUp() override {
    test::AshTestBase::SetUp();
    model_ = Shell::Get()->shelf_model();
  }

  void TearDown() override {
    model_ = nullptr;
    test::AshTestBase::TearDown();
  }

  static ShelfID CreateShelfItem(aura::Window* window) {
    static int id = 0;
    ShelfID shelf_id(base::IntToString(id++));
    window->SetProperty(kShelfIDKey, new std::string(shelf_id.Serialize()));
    window->SetProperty(kShelfItemTypeKey, static_cast<int32_t>(TYPE_DIALOG));
    return shelf_id;
  }

 protected:
  ShelfModel* model_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ShelfWindowWatcherTest);
};

// Ensure shelf items are added and removed as windows are opened and closed.
TEST_F(ShelfWindowWatcherTest, OpenAndClose) {
  // ShelfModel only has an APP_LIST item.
  EXPECT_EQ(1, model_->item_count());

  // Adding windows with valid ShelfItemType properties adds shelf items.
  std::unique_ptr<views::Widget> widget1 =
      CreateTestWidget(nullptr, kShellWindowId_DefaultContainer, gfx::Rect());
  CreateShelfItem(widget1->GetNativeWindow());
  EXPECT_EQ(2, model_->item_count());
  std::unique_ptr<views::Widget> widget2 =
      CreateTestWidget(nullptr, kShellWindowId_DefaultContainer, gfx::Rect());
  CreateShelfItem(widget2->GetNativeWindow());
  EXPECT_EQ(3, model_->item_count());

  // Each ShelfItem is removed when the associated window is destroyed.
  widget1.reset();
  EXPECT_EQ(2, model_->item_count());
  widget2.reset();
  EXPECT_EQ(1, model_->item_count());
}

TEST_F(ShelfWindowWatcherTest, CreateAndRemoveShelfItemProperties) {
  // TODO: investigate failure in mash. http://crbug.com/695562.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  // ShelfModel only has an APP_LIST item.
  EXPECT_EQ(1, model_->item_count());

  // Creating windows without a valid ShelfItemType does not add items.
  std::unique_ptr<views::Widget> widget1 =
      CreateTestWidget(nullptr, kShellWindowId_DefaultContainer, gfx::Rect());
  std::unique_ptr<views::Widget> widget2 =
      CreateTestWidget(nullptr, kShellWindowId_DefaultContainer, gfx::Rect());
  EXPECT_EQ(1, model_->item_count());

  // Create a ShelfItem for the first window.
  ShelfID id_w1 = CreateShelfItem(widget1->GetNativeWindow());
  EXPECT_EQ(2, model_->item_count());

  int index_w1 = model_->ItemIndexByID(id_w1);
  EXPECT_EQ(STATUS_RUNNING, model_->items()[index_w1].status);

  // Create a ShelfItem for the second window.
  ShelfID id_w2 = CreateShelfItem(widget2->GetNativeWindow());
  EXPECT_EQ(3, model_->item_count());

  int index_w2 = model_->ItemIndexByID(id_w2);
  EXPECT_EQ(STATUS_ACTIVE, model_->items()[index_w2].status);

  // ShelfItem is removed when the item type window property is cleared.
  widget1->GetNativeWindow()->SetProperty(kShelfItemTypeKey,
                                          static_cast<int32_t>(TYPE_UNDEFINED));
  EXPECT_EQ(2, model_->item_count());
  widget2->GetNativeWindow()->SetProperty(kShelfItemTypeKey,
                                          static_cast<int32_t>(TYPE_UNDEFINED));
  EXPECT_EQ(1, model_->item_count());
  // Clearing twice doesn't do anything.
  widget2->GetNativeWindow()->SetProperty(kShelfItemTypeKey,
                                          static_cast<int32_t>(TYPE_UNDEFINED));
  EXPECT_EQ(1, model_->item_count());
}

TEST_F(ShelfWindowWatcherTest, ActivateWindow) {
  // TODO: investigate failure in mash. http://crbug.com/695562.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  // ShelfModel only has an APP_LIST item.
  EXPECT_EQ(1, model_->item_count());
  std::unique_ptr<views::Widget> widget1 =
      CreateTestWidget(nullptr, kShellWindowId_DefaultContainer, gfx::Rect());
  std::unique_ptr<views::Widget> widget2 =
      CreateTestWidget(nullptr, kShellWindowId_DefaultContainer, gfx::Rect());

  // Create a ShelfItem for the first window.
  ShelfID id_w1 = CreateShelfItem(widget1->GetNativeWindow());
  EXPECT_EQ(2, model_->item_count());
  int index_w1 = model_->ItemIndexByID(id_w1);
  EXPECT_EQ(STATUS_RUNNING, model_->items()[index_w1].status);

  // Create a ShelfItem for the second window.
  ShelfID id_w2 = CreateShelfItem(widget2->GetNativeWindow());
  EXPECT_EQ(3, model_->item_count());
  int index_w2 = model_->ItemIndexByID(id_w2);
  EXPECT_EQ(STATUS_RUNNING, model_->items()[index_w1].status);
  EXPECT_EQ(STATUS_ACTIVE, model_->items()[index_w2].status);

  // The ShelfItem for the first window is active when the window is activated.
  widget1->Activate();
  EXPECT_EQ(STATUS_ACTIVE, model_->items()[index_w1].status);
  EXPECT_EQ(STATUS_RUNNING, model_->items()[index_w2].status);

  // The ShelfItem for the second window is active when the window is activated.
  widget2->Activate();
  EXPECT_EQ(STATUS_RUNNING, model_->items()[index_w1].status);
  EXPECT_EQ(STATUS_ACTIVE, model_->items()[index_w2].status);
}

TEST_F(ShelfWindowWatcherTest, UpdateWindowProperty) {
  // TODO: investigate failure in mash. http://crbug.com/695562.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  // ShelfModel only has an APP_LIST item.
  EXPECT_EQ(1, model_->item_count());

  std::unique_ptr<views::Widget> widget =
      CreateTestWidget(nullptr, kShellWindowId_DefaultContainer, gfx::Rect());

  // Create a ShelfItem for |window|.
  ShelfID id = CreateShelfItem(widget->GetNativeWindow());
  EXPECT_EQ(2, model_->item_count());

  int index = model_->ItemIndexByID(id);
  EXPECT_EQ(STATUS_ACTIVE, model_->items()[index].status);

  // Update the window's ShelfItemType.
  widget->GetNativeWindow()->SetProperty(kShelfItemTypeKey,
                                         static_cast<int32_t>(TYPE_APP));
  // No new item is created after updating a launcher item.
  EXPECT_EQ(2, model_->item_count());
  // index and id are not changed after updating a launcher item.
  EXPECT_EQ(index, model_->ItemIndexByID(id));
  EXPECT_EQ(id, model_->items()[index].id);
}

TEST_F(ShelfWindowWatcherTest, MaximizeAndRestoreWindow) {
  // TODO: investigate failure in mash. http://crbug.com/695562.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  // ShelfModel only has an APP_LIST item.
  EXPECT_EQ(1, model_->item_count());

  std::unique_ptr<views::Widget> widget =
      CreateTestWidget(nullptr, kShellWindowId_DefaultContainer, gfx::Rect());
  wm::WindowState* window_state = wm::GetWindowState(widget->GetNativeWindow());

  // Create a ShelfItem for the window.
  ShelfID id = CreateShelfItem(widget->GetNativeWindow());
  EXPECT_EQ(2, model_->item_count());

  int index = model_->ItemIndexByID(id);
  EXPECT_EQ(STATUS_ACTIVE, model_->items()[index].status);

  // Maximize the window.
  EXPECT_FALSE(window_state->IsMaximized());
  window_state->Maximize();
  EXPECT_TRUE(window_state->IsMaximized());
  // No new item is created after maximizing the window.
  EXPECT_EQ(2, model_->item_count());
  // index and id are not changed after maximizing the window.
  EXPECT_EQ(index, model_->ItemIndexByID(id));
  EXPECT_EQ(id, model_->items()[index].id);

  // Restore the window.
  window_state->Restore();
  EXPECT_FALSE(window_state->IsMaximized());
  // No new item is created after restoring the window.
  EXPECT_EQ(2, model_->item_count());
  // Index and id are not changed after maximizing the window.
  EXPECT_EQ(index, model_->ItemIndexByID(id));
  EXPECT_EQ(id, model_->items()[index].id);
}

// Check |window|'s item is not changed during the dragging.
// TODO(simonhong): Add a test for removing a Window during the dragging.
TEST_F(ShelfWindowWatcherTest, DragWindow) {
  // TODO: investigate failure in mash. http://crbug.com/695562.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  // ShelfModel only has an APP_LIST item.
  EXPECT_EQ(1, model_->item_count());

  std::unique_ptr<views::Widget> widget =
      CreateTestWidget(nullptr, kShellWindowId_DefaultContainer, gfx::Rect());

  // Create a ShelfItem for the window.
  ShelfID id = CreateShelfItem(widget->GetNativeWindow());
  EXPECT_EQ(2, model_->item_count());

  int index = model_->ItemIndexByID(id);
  EXPECT_EQ(STATUS_ACTIVE, model_->items()[index].status);

  // Simulate dragging of the window and check its item is not changed.
  std::unique_ptr<WindowResizer> resizer(
      CreateWindowResizer(widget->GetNativeWindow(), gfx::Point(), HTCAPTION,
                          aura::client::WINDOW_MOVE_SOURCE_MOUSE));
  ASSERT_TRUE(resizer.get());
  resizer->Drag(gfx::Point(50, 50), 0);
  resizer->CompleteDrag();

  // Index and id are not changed after dragging the window.
  EXPECT_EQ(index, model_->ItemIndexByID(id));
  EXPECT_EQ(id, model_->items()[index].id);
}

// Ensure shelf items are added and removed as panels are opened and closed.
TEST_F(ShelfWindowWatcherTest, PanelWindow) {
  // TODO: investigate failure in mash. http://crbug.com/695562.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  // ShelfModel only has an APP_LIST item.
  EXPECT_EQ(1, model_->item_count());

  // Adding windows with valid ShelfItemType properties adds shelf items.
  std::unique_ptr<views::Widget> widget1 =
      CreateTestWidget(nullptr, kShellWindowId_DefaultContainer, gfx::Rect());
  aura::Window* window1 = widget1->GetNativeWindow();
  window1->SetProperty(kShelfIDKey, new std::string(ShelfID("a").Serialize()));
  window1->SetProperty(kShelfItemTypeKey, static_cast<int32_t>(TYPE_APP));
  EXPECT_EQ(2, model_->item_count());
  std::unique_ptr<views::Widget> widget2 =
      CreateTestWidget(nullptr, kShellWindowId_DefaultContainer, gfx::Rect());
  aura::Window* window2 = widget2->GetNativeWindow();
  window2->SetProperty(kShelfIDKey, new std::string(ShelfID("b").Serialize()));
  window2->SetProperty(kShelfItemTypeKey, static_cast<int32_t>(TYPE_DIALOG));
  EXPECT_EQ(3, model_->item_count());

  // Create a panel-type widget to mimic Chrome's app panel windows.
  views::Widget panel_widget;
  views::Widget::InitParams panel_params(views::Widget::InitParams::TYPE_PANEL);
  panel_params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  Shell::GetPrimaryRootWindowController()
      ->ConfigureWidgetInitParamsForContainer(
          &panel_widget, kShellWindowId_PanelContainer, &panel_params);
  panel_widget.Init(panel_params);
  panel_widget.Show();
  aura::Window* panel_window = panel_widget.GetNativeWindow();
  panel_window->SetProperty(kShelfIDKey,
                            new std::string(ShelfID("c").Serialize()));
  panel_window->SetProperty(kShelfItemTypeKey,
                            static_cast<int32_t>(TYPE_APP_PANEL));
  EXPECT_EQ(4, model_->item_count());

  // Each ShelfItem is removed when the associated window is destroyed.
  panel_widget.CloseNow();
  EXPECT_EQ(3, model_->item_count());
  widget2.reset();
  EXPECT_EQ(2, model_->item_count());
  widget1.reset();
  EXPECT_EQ(1, model_->item_count());
}

TEST_F(ShelfWindowWatcherTest, DontCreateShelfEntriesForChildWindows) {
  const int initial_item_count = model_->item_count();

  std::unique_ptr<aura::Window> window(
      base::MakeUnique<aura::Window>(nullptr, ui::wm::WINDOW_TYPE_NORMAL));
  window->Init(ui::LAYER_NOT_DRAWN);
  window->SetProperty(kShelfIDKey, new std::string(ShelfID("foo").Serialize()));
  window->SetProperty(kShelfItemTypeKey, static_cast<int32_t>(TYPE_APP));
  Shell::GetPrimaryRootWindow()
      ->GetChildById(kShellWindowId_DefaultContainer)
      ->AddChild(window.get());
  window->Show();
  EXPECT_EQ(initial_item_count + 1, model_->item_count());

  std::unique_ptr<aura::Window> child_window(
      base::MakeUnique<aura::Window>(nullptr, ui::wm::WINDOW_TYPE_NORMAL));
  child_window->Init(ui::LAYER_NOT_DRAWN);
  child_window->SetProperty(kShelfItemTypeKey, static_cast<int32_t>(TYPE_APP));
  window->AddChild(child_window.get());
  child_window->Show();
  // |child_window| should not result in adding a new entry.
  EXPECT_EQ(initial_item_count + 1, model_->item_count());

  child_window.reset();
  window.reset();
  EXPECT_EQ(initial_item_count, model_->item_count());
}

// Ensures ShelfWindowWatcher supports windows opened prior to session start.
using ShelfWindowWatcherSessionStartTest = test::NoSessionAshTestBase;
TEST_F(ShelfWindowWatcherSessionStartTest, PreExistingWindow) {
  ShelfModel* model = Shell::Get()->shelf_model();
  ASSERT_FALSE(
      Shell::Get()->session_controller()->IsActiveUserSessionStarted());

  // ShelfModel only has an APP_LIST item.
  EXPECT_EQ(1, model->item_count());

  // Construct a window that should get a shelf item once the session starts.
  std::unique_ptr<views::Widget> widget =
      CreateTestWidget(nullptr, kShellWindowId_DefaultContainer, gfx::Rect());
  ShelfWindowWatcherTest::CreateShelfItem(widget->GetNativeWindow());
  EXPECT_EQ(1, model->item_count());

  // Start the test user session; ShelfWindowWatcher will find the open window.
  SetSessionStarted(true);
  EXPECT_EQ(2, model->item_count());
}

}  // namespace ash
