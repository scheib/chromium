// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shelf/shelf_window_watcher.h"

#include <memory>
#include <utility>

#include "ash/public/cpp/config.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/shelf/shelf_constants.h"
#include "ash/shelf/shelf_model.h"
#include "ash/shelf/shelf_window_watcher_item_delegate.h"
#include "ash/shell.h"
#include "ash/shell_port.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm_window.h"
#include "base/strings/string_number_conversions.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/resources/grit/ui_resources.h"
#include "ui/wm/public/activation_client.h"

namespace ash {
namespace {

// Returns the shelf item type, with special temporary behavior for Mash:
// Mash provides a default shelf item type (TYPE_APP) for non-ignored windows.
ShelfItemType GetShelfItemType(aura::Window* window) {
  // TODO(msw): Remove Mash default ShelfItemType assignment. crbug.com/722496
  if (Shell::GetAshConfig() == Config::MASH &&
      window->GetProperty(kShelfItemTypeKey) == TYPE_UNDEFINED &&
      !wm::GetWindowState(window)->ignored_by_shelf()) {
    return TYPE_APP;
  }
  return static_cast<ShelfItemType>(window->GetProperty(kShelfItemTypeKey));
}

// Returns the shelf id, with special temporary behavior for Mash:
// Mash provides a default shelf ids for non-ignored windows.
ShelfID GetShelfID(aura::Window* window) {
  // TODO(msw): Remove Mash default ShelfID assignment. crbug.com/722496
  if (Shell::GetAshConfig() == Config::MASH &&
      !window->GetProperty(kShelfIDKey) &&
      !wm::GetWindowState(window)->ignored_by_shelf()) {
    static int id = 0;
    const ash::ShelfID shelf_id(base::IntToString(id++));
    window->SetProperty(kShelfIDKey, new std::string(shelf_id.Serialize()));
    return shelf_id;
  }
  return ShelfID::Deserialize(window->GetProperty(kShelfIDKey));
}

// Update the ShelfItem from relevant window properties.
void UpdateShelfItemForWindow(ShelfItem* item, aura::Window* window) {
  item->id = GetShelfID(window);
  item->type = GetShelfItemType(window);

  item->status = STATUS_RUNNING;
  if (wm::IsActiveWindow(window))
    item->status = STATUS_ACTIVE;
  else if (window->GetProperty(aura::client::kDrawAttentionKey))
    item->status = STATUS_ATTENTION;

  // Prefer app icons over window icons, they're typically larger.
  gfx::ImageSkia* image = window->GetProperty(aura::client::kAppIconKey);
  if (!image || image->isNull())
    image = window->GetProperty(aura::client::kWindowIconKey);
  if (!image || image->isNull()) {
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    item->image = rb.GetImageNamed(IDR_DEFAULT_FAVICON_32).AsImageSkia();
  } else {
    item->image = *image;
  }

  item->title = window->GetTitle();

  // Do not show tooltips for visible attached app panel windows.
  item->shows_tooltip = item->type != TYPE_APP_PANEL || !window->IsVisible() ||
                        !window->GetProperty(kPanelAttachedKey);
}

}  // namespace

ShelfWindowWatcher::ContainerWindowObserver::ContainerWindowObserver(
    ShelfWindowWatcher* window_watcher)
    : window_watcher_(window_watcher) {}

ShelfWindowWatcher::ContainerWindowObserver::~ContainerWindowObserver() {}

void ShelfWindowWatcher::ContainerWindowObserver::OnWindowHierarchyChanged(
    const HierarchyChangeParams& params) {
  if (!params.old_parent && params.new_parent &&
      (params.new_parent->id() == kShellWindowId_DefaultContainer ||
       params.new_parent->id() == kShellWindowId_PanelContainer)) {
    // A new window was created in the default container or the panel container.
    window_watcher_->OnUserWindowAdded(params.target);
  }
}

void ShelfWindowWatcher::ContainerWindowObserver::OnWindowDestroying(
    aura::Window* window) {
  window_watcher_->OnContainerWindowDestroying(window);
}

////////////////////////////////////////////////////////////////////////////////

ShelfWindowWatcher::UserWindowObserver::UserWindowObserver(
    ShelfWindowWatcher* window_watcher)
    : window_watcher_(window_watcher) {}

ShelfWindowWatcher::UserWindowObserver::~UserWindowObserver() {}

void ShelfWindowWatcher::UserWindowObserver::OnWindowPropertyChanged(
    aura::Window* window,
    const void* key,
    intptr_t old) {
  // ShelfIDs should never change except when replacing Mash temporary defaults.
  // TODO(msw): Remove Mash default ShelfID handling. crbug.com/722496
  if (Shell::GetAshConfig() == Config::MASH && key == kShelfIDKey) {
    ShelfID old_id = ShelfID::Deserialize(reinterpret_cast<std::string*>(old));
    ShelfID new_id = ShelfID::Deserialize(window->GetProperty(kShelfIDKey));
    if (old_id != new_id && !old_id.IsNull() && !new_id.IsNull()) {
      // Id changing is not supported; remove the item and it will be re-added.
      window_watcher_->user_windows_with_items_.erase(window);
      const int index = window_watcher_->model_->ItemIndexByID(old_id);
      window_watcher_->model_->RemoveItemAt(index);
    }
  }

  if (key == aura::client::kAppIconKey || key == aura::client::kWindowIconKey ||
      key == aura::client::kDrawAttentionKey || key == kPanelAttachedKey ||
      key == kShelfItemTypeKey || key == kShelfIDKey) {
    window_watcher_->OnUserWindowPropertyChanged(window);
  }
}

void ShelfWindowWatcher::UserWindowObserver::OnWindowDestroying(
    aura::Window* window) {
  window_watcher_->OnUserWindowDestroying(window);
}

void ShelfWindowWatcher::UserWindowObserver::OnWindowVisibilityChanged(
    aura::Window* window,
    bool visible) {
  // OnWindowVisibilityChanged() is called for descendants too. We only care
  // about changes to the visibility of windows we know about.
  if (!window_watcher_->observed_user_windows_.IsObserving(window))
    return;

  // The tooltip behavior for panel windows depends on the panel visibility.
  window_watcher_->OnUserWindowPropertyChanged(window);
}

void ShelfWindowWatcher::UserWindowObserver::OnWindowTitleChanged(
    aura::Window* window) {
  window_watcher_->OnUserWindowPropertyChanged(window);
}

////////////////////////////////////////////////////////////////////////////////

ShelfWindowWatcher::ShelfWindowWatcher(ShelfModel* model)
    : model_(model),
      container_window_observer_(this),
      user_window_observer_(this),
      observed_container_windows_(&container_window_observer_),
      observed_user_windows_(&user_window_observer_) {
  Shell::Get()->activation_client()->AddObserver(this);
  for (const auto& display : display::Screen::GetScreen()->GetAllDisplays())
    OnDisplayAdded(display);
  display::Screen::GetScreen()->AddObserver(this);
}

ShelfWindowWatcher::~ShelfWindowWatcher() {
  display::Screen::GetScreen()->RemoveObserver(this);
  Shell::Get()->activation_client()->RemoveObserver(this);
}

void ShelfWindowWatcher::AddShelfItem(aura::Window* window) {
  user_windows_with_items_.insert(window);
  ShelfItem item;
  UpdateShelfItemForWindow(&item, window);

  // ShelfWindowWatcher[ItemDelegate] doesn't support multiple windows per item,
  // but this can happen in Mash (eg. when multiple browser windows are open).
  // Assign a unique launch id in this case to avoid crashing on DCHECKs.
  // TODO(msw): Remove Mash duplicate ShelfID handling. crbug.com/722496
  if (Shell::GetAshConfig() == Config::MASH &&
      model_->ItemIndexByID(item.id) > 0) {
    static int id = 0;
    item.id.launch_id = base::IntToString(id++);
  }

  model_->SetShelfItemDelegate(item.id,
                               base::MakeUnique<ShelfWindowWatcherItemDelegate>(
                                   item.id, WmWindow::Get(window)));
  // Panels are inserted on the left so as not to push all existing panels over.
  model_->AddAt(item.type == TYPE_APP_PANEL ? 0 : model_->item_count(), item);
}

void ShelfWindowWatcher::RemoveShelfItem(aura::Window* window) {
  user_windows_with_items_.erase(window);
  const ShelfID shelf_id = GetShelfID(window);
  DCHECK(!shelf_id.IsNull());
  const int index = model_->ItemIndexByID(shelf_id);
  DCHECK_GE(index, 0);
  model_->RemoveItemAt(index);
}

void ShelfWindowWatcher::OnContainerWindowDestroying(aura::Window* container) {
  observed_container_windows_.Remove(container);
}

int ShelfWindowWatcher::GetShelfItemIndexForWindow(aura::Window* window) const {
  const ShelfID shelf_id = GetShelfID(window);
  return shelf_id.IsNull() ? -1 : model_->ItemIndexByID(shelf_id);
}

void ShelfWindowWatcher::OnUserWindowAdded(aura::Window* window) {
  // The window may already be tracked from a prior display or parent container.
  if (observed_user_windows_.IsObserving(window))
    return;

  observed_user_windows_.Add(window);

  // Add, update, or remove a ShelfItem for |window|, as needed.
  OnUserWindowPropertyChanged(window);
}

void ShelfWindowWatcher::OnUserWindowDestroying(aura::Window* window) {
  if (observed_user_windows_.IsObserving(window))
    observed_user_windows_.Remove(window);

  if (user_windows_with_items_.count(window) > 0)
    RemoveShelfItem(window);
  DCHECK_EQ(0u, user_windows_with_items_.count(window));
}

void ShelfWindowWatcher::OnUserWindowPropertyChanged(aura::Window* window) {
  if (GetShelfItemType(window) == TYPE_UNDEFINED ||
      GetShelfID(window).IsNull()) {
    // Remove |window|'s ShelfItem if it was added by this ShelfWindowWatcher.
    if (user_windows_with_items_.count(window) > 0)
      RemoveShelfItem(window);
    return;
  }

  // Update an existing ShelfItem for |window| when a property has changed.
  int index = GetShelfItemIndexForWindow(window);
  if (index > 0) {
    ShelfItem item = model_->items()[index];
    UpdateShelfItemForWindow(&item, window);
    model_->Set(index, item);
    return;
  }

  // Creates a new ShelfItem for |window|.
  AddShelfItem(window);
}

void ShelfWindowWatcher::OnWindowActivated(ActivationReason reason,
                                           aura::Window* gained_active,
                                           aura::Window* lost_active) {
  if (gained_active && user_windows_with_items_.count(gained_active) > 0)
    OnUserWindowPropertyChanged(gained_active);
  if (lost_active && user_windows_with_items_.count(lost_active) > 0)
    OnUserWindowPropertyChanged(lost_active);
}

void ShelfWindowWatcher::OnDisplayAdded(const display::Display& new_display) {
  aura::Window* root =
      ShellPort::Get()->GetRootWindowForDisplayId(new_display.id());

  // When the primary root window's display is removed, the existing root window
  // is taken over by the new display, and the observer is already set.
  aura::Window* default_container =
      root->GetChildById(kShellWindowId_DefaultContainer);
  if (!observed_container_windows_.IsObserving(default_container)) {
    for (aura::Window* window : default_container->children())
      OnUserWindowAdded(window);
    observed_container_windows_.Add(default_container);
  }
  aura::Window* panel_container =
      root->GetChildById(kShellWindowId_PanelContainer);
  if (!observed_container_windows_.IsObserving(panel_container)) {
    for (aura::Window* window : panel_container->children())
      OnUserWindowAdded(window);
    observed_container_windows_.Add(panel_container);
  }
}

void ShelfWindowWatcher::OnDisplayRemoved(const display::Display& old_display) {
}

void ShelfWindowWatcher::OnDisplayMetricsChanged(const display::Display&,
                                                 uint32_t) {}

}  // namespace ash
