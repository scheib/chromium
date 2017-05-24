// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/workspace/workspace_event_handler.h"

#include "ash/shell_port.h"
#include "ash/wm/window_state.h"
#include "ash/wm/wm_event.h"
#include "ash/wm_window.h"
#include "ui/base/hit_test.h"
#include "ui/events/event.h"

namespace ash {

WorkspaceEventHandler::WorkspaceEventHandler() : click_component_(HTNOWHERE) {}

WorkspaceEventHandler::~WorkspaceEventHandler() {}

void WorkspaceEventHandler::OnMouseEvent(ui::MouseEvent* event,
                                         WmWindow* target) {
  if (event->type() == ui::ET_MOUSE_PRESSED && event->IsOnlyLeftMouseButton() &&
      ((event->flags() & (ui::EF_IS_DOUBLE_CLICK | ui::EF_IS_TRIPLE_CLICK)) ==
       0)) {
    click_component_ = target->GetNonClientComponent(event->location());
  }

  if (event->handled())
    return;

  switch (event->type()) {
    case ui::ET_MOUSE_MOVED: {
      int component = target->GetNonClientComponent(event->location());
      multi_window_resize_controller_.Show(target->aura_window(), component,
                                           event->location());
      break;
    }
    case ui::ET_MOUSE_ENTERED:
      break;
    case ui::ET_MOUSE_CAPTURE_CHANGED:
    case ui::ET_MOUSE_EXITED:
      break;
    case ui::ET_MOUSE_PRESSED: {
      wm::WindowState* target_state = target->GetWindowState();

      if (event->IsOnlyLeftMouseButton()) {
        if (event->flags() & ui::EF_IS_DOUBLE_CLICK) {
          int component = target->GetNonClientComponent(event->location());
          if (component == HTCAPTION && component == click_component_) {
            ShellPort::Get()->RecordUserMetricsAction(
                UMA_TOGGLE_MAXIMIZE_CAPTION_CLICK);
            const wm::WMEvent wm_event(wm::WM_EVENT_TOGGLE_MAXIMIZE_CAPTION);
            target_state->OnWMEvent(&wm_event);
            event->StopPropagation();
          }
          click_component_ = HTNOWHERE;
        }
      } else {
        click_component_ = HTNOWHERE;
      }

      HandleVerticalResizeDoubleClick(target_state, event);
      break;
    }
    default:
      break;
  }
}

void WorkspaceEventHandler::OnGestureEvent(ui::GestureEvent* event,
                                           WmWindow* target) {
  if (event->handled() || event->type() != ui::ET_GESTURE_TAP)
    return;

  int previous_target_component = click_component_;
  click_component_ = target->GetNonClientComponent(event->location());

  if (click_component_ != HTCAPTION)
    return;

  if (event->details().tap_count() != 2) {
    ShellPort::Get()->RecordGestureAction(GESTURE_FRAMEVIEW_TAP);
    return;
  }

  if (click_component_ == previous_target_component) {
    ShellPort::Get()->RecordUserMetricsAction(
        UMA_TOGGLE_MAXIMIZE_CAPTION_GESTURE);
    ShellPort::Get()->RecordGestureAction(GESTURE_MAXIMIZE_DOUBLETAP);
    const wm::WMEvent wm_event(wm::WM_EVENT_TOGGLE_MAXIMIZE_CAPTION);
    target->GetWindowState()->OnWMEvent(&wm_event);
    event->StopPropagation();
  }
  click_component_ = HTNOWHERE;
}

void WorkspaceEventHandler::HandleVerticalResizeDoubleClick(
    wm::WindowState* target_state,
    ui::MouseEvent* event) {
  WmWindow* target = target_state->window();
  if (event->flags() & ui::EF_IS_DOUBLE_CLICK) {
    int component = target->GetNonClientComponent(event->location());
    if (component == HTBOTTOM || component == HTTOP) {
      ShellPort::Get()->RecordUserMetricsAction(
          UMA_TOGGLE_SINGLE_AXIS_MAXIMIZE_BORDER_CLICK);
      const wm::WMEvent wm_event(wm::WM_EVENT_TOGGLE_VERTICAL_MAXIMIZE);
      target_state->OnWMEvent(&wm_event);
      event->StopPropagation();
    } else if (component == HTLEFT || component == HTRIGHT) {
      ShellPort::Get()->RecordUserMetricsAction(
          UMA_TOGGLE_SINGLE_AXIS_MAXIMIZE_BORDER_CLICK);
      const wm::WMEvent wm_event(wm::WM_EVENT_TOGGLE_HORIZONTAL_MAXIMIZE);
      target_state->OnWMEvent(&wm_event);
      event->StopPropagation();
    }
  }
}

}  // namespace ash
