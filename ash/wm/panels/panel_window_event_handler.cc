// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/panels/panel_window_event_handler.h"

#include "ash/shell_port.h"
#include "ash/wm/window_state.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/hit_test.h"
#include "ui/events/event.h"

namespace ash {

PanelWindowEventHandler::PanelWindowEventHandler() {}

PanelWindowEventHandler::~PanelWindowEventHandler() {}

void PanelWindowEventHandler::OnMouseEvent(ui::MouseEvent* event) {
  aura::Window* target = static_cast<aura::Window*>(event->target());
  if (!event->handled() && event->type() == ui::ET_MOUSE_PRESSED &&
      event->flags() & ui::EF_IS_DOUBLE_CLICK &&
      event->IsOnlyLeftMouseButton() &&
      target->delegate()->GetNonClientComponent(event->location()) ==
          HTCAPTION) {
    ShellPort::Get()->RecordUserMetricsAction(UMA_PANEL_MINIMIZE_CAPTION_CLICK);
    wm::GetWindowState(target)->Minimize();
    event->StopPropagation();
    return;
  }
}

void PanelWindowEventHandler::OnGestureEvent(ui::GestureEvent* event) {
  aura::Window* target = static_cast<aura::Window*>(event->target());
  if (!event->handled() && event->type() == ui::ET_GESTURE_TAP &&
      event->details().tap_count() == 2 &&
      target->delegate()->GetNonClientComponent(event->location()) ==
          HTCAPTION) {
    ShellPort::Get()->RecordUserMetricsAction(
        UMA_PANEL_MINIMIZE_CAPTION_GESTURE);
    wm::GetWindowState(target)->Minimize();
    event->StopPropagation();
    return;
  }
}

}  // namespace ash
