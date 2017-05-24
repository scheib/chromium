// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/wm_toplevel_window_event_handler.h"

#include "ash/shell_port.h"
#include "ash/wm/window_resizer.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_state_observer.h"
#include "ash/wm/wm_event.h"
#include "ash/wm_window.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/base/hit_test.h"
#include "ui/events/event.h"

namespace {
const double kMinHorizVelocityForWindowSwipe = 1100;
const double kMinVertVelocityForWindowMinimize = 1000;
}

namespace ash {
namespace wm {

namespace {

// Returns whether |window| can be moved via a two finger drag given
// the hittest results of the two fingers.
bool CanStartTwoFingerMove(WmWindow* window,
                           int window_component1,
                           int window_component2) {
  // We allow moving a window via two fingers when the hittest components are
  // HTCLIENT. This is done so that a window can be dragged via two fingers when
  // the tab strip is full and hitting the caption area is difficult. We check
  // the window type and the state type so that we do not steal touches from the
  // web contents.
  if (!window->GetWindowState()->IsNormalOrSnapped() ||
      window->GetType() != ui::wm::WINDOW_TYPE_NORMAL) {
    return false;
  }
  int component1_behavior =
      WindowResizer::GetBoundsChangeForWindowComponent(window_component1);
  int component2_behavior =
      WindowResizer::GetBoundsChangeForWindowComponent(window_component2);
  return (component1_behavior & WindowResizer::kBoundsChange_Resizes) == 0 &&
         (component2_behavior & WindowResizer::kBoundsChange_Resizes) == 0;
}

// Returns whether |window| can be moved or resized via one finger given
// |window_component|.
bool CanStartOneFingerDrag(int window_component) {
  return WindowResizer::GetBoundsChangeForWindowComponent(window_component) !=
         0;
}

// Returns the window component containing |event|'s location.
int GetWindowComponent(WmWindow* window, const ui::LocatedEvent& event) {
  return window->GetNonClientComponent(event.location());
}

}  // namespace

// ScopedWindowResizer ---------------------------------------------------------

// Wraps a WindowResizer and installs an observer on its target window.  When
// the window is destroyed ResizerWindowDestroyed() is invoked back on the
// WmToplevelWindowEventHandler to clean up.
class WmToplevelWindowEventHandler::ScopedWindowResizer
    : public aura::WindowObserver,
      public wm::WindowStateObserver {
 public:
  ScopedWindowResizer(WmToplevelWindowEventHandler* handler,
                      std::unique_ptr<WindowResizer> resizer);
  ~ScopedWindowResizer() override;

  // Returns true if the drag moves the window and does not resize.
  bool IsMove() const;

  WindowResizer* resizer() { return resizer_.get(); }

  // WindowObserver overrides:
  void OnWindowDestroying(aura::Window* window) override;

  // WindowStateObserver overrides:
  void OnPreWindowStateTypeChange(wm::WindowState* window_state,
                                  wm::WindowStateType type) override;

 private:
  WmToplevelWindowEventHandler* handler_;
  std::unique_ptr<WindowResizer> resizer_;

  // Whether ScopedWindowResizer grabbed capture.
  bool grabbed_capture_;

  DISALLOW_COPY_AND_ASSIGN(ScopedWindowResizer);
};

WmToplevelWindowEventHandler::ScopedWindowResizer::ScopedWindowResizer(
    WmToplevelWindowEventHandler* handler,
    std::unique_ptr<WindowResizer> resizer)
    : handler_(handler), resizer_(std::move(resizer)), grabbed_capture_(false) {
  aura::Window* target = resizer_->GetTarget();
  target->AddObserver(this);
  GetWindowState(target)->AddObserver(this);

  if (!target->HasCapture()) {
    grabbed_capture_ = true;
    target->SetCapture();
  }
}

WmToplevelWindowEventHandler::ScopedWindowResizer::~ScopedWindowResizer() {
  aura::Window* target = resizer_->GetTarget();
  target->RemoveObserver(this);
  GetWindowState(target)->RemoveObserver(this);
  if (grabbed_capture_)
    target->ReleaseCapture();
}

bool WmToplevelWindowEventHandler::ScopedWindowResizer::IsMove() const {
  return resizer_->details().bounds_change ==
         WindowResizer::kBoundsChange_Repositions;
}

void WmToplevelWindowEventHandler::ScopedWindowResizer::
    OnPreWindowStateTypeChange(wm::WindowState* window_state,
                               wm::WindowStateType old) {
  handler_->CompleteDrag(DragResult::SUCCESS);
}

void WmToplevelWindowEventHandler::ScopedWindowResizer::OnWindowDestroying(
    aura::Window* window) {
  DCHECK_EQ(resizer_->GetTarget(), window);
  handler_->ResizerWindowDestroyed();
}

// WmToplevelWindowEventHandler
// --------------------------------------------------

WmToplevelWindowEventHandler::WmToplevelWindowEventHandler()
    : first_finger_hittest_(HTNOWHERE) {
  ShellPort::Get()->AddDisplayObserver(this);
}

WmToplevelWindowEventHandler::~WmToplevelWindowEventHandler() {
  ShellPort::Get()->RemoveDisplayObserver(this);
}

void WmToplevelWindowEventHandler::OnKeyEvent(ui::KeyEvent* event) {
  if (window_resizer_.get() && event->type() == ui::ET_KEY_PRESSED &&
      event->key_code() == ui::VKEY_ESCAPE) {
    CompleteDrag(DragResult::REVERT);
  }
}

void WmToplevelWindowEventHandler::OnMouseEvent(ui::MouseEvent* event,
                                                WmWindow* target) {
  if (event->handled())
    return;
  if ((event->flags() &
       (ui::EF_MIDDLE_MOUSE_BUTTON | ui::EF_RIGHT_MOUSE_BUTTON)) != 0)
    return;

  if (event->type() == ui::ET_MOUSE_CAPTURE_CHANGED) {
    // Capture is grabbed when both gesture and mouse drags start. Handle
    // capture loss regardless of which type of drag is in progress.
    HandleCaptureLost(event);
    return;
  }

  if (in_gesture_drag_)
    return;

  switch (event->type()) {
    case ui::ET_MOUSE_PRESSED:
      HandleMousePressed(target, event);
      break;
    case ui::ET_MOUSE_DRAGGED:
      HandleDrag(target, event);
      break;
    case ui::ET_MOUSE_RELEASED:
      HandleMouseReleased(target, event);
      break;
    case ui::ET_MOUSE_MOVED:
      HandleMouseMoved(target, event);
      break;
    case ui::ET_MOUSE_EXITED:
      HandleMouseExited(target, event);
      break;
    default:
      break;
  }
}

void WmToplevelWindowEventHandler::OnGestureEvent(ui::GestureEvent* event,
                                                  WmWindow* target) {
  if (event->handled())
    return;
  if (!target->HasNonClientArea())
    return;

  if (window_resizer_.get() && !in_gesture_drag_)
    return;

  if (window_resizer_.get() &&
      window_resizer_->resizer()->GetTarget() != target->aura_window()) {
    return;
  }

  if (event->details().touch_points() > 2) {
    if (CompleteDrag(DragResult::SUCCESS))
      event->StopPropagation();
    return;
  }

  switch (event->type()) {
    case ui::ET_GESTURE_TAP_DOWN: {
      int component = GetWindowComponent(target, *event);
      if (!(WindowResizer::GetBoundsChangeForWindowComponent(component) &
            WindowResizer::kBoundsChange_Resizes))
        return;
      target->ShowResizeShadow(component);
      return;
    }
    case ui::ET_GESTURE_END: {
      target->HideResizeShadow();

      if (window_resizer_.get() &&
          (event->details().touch_points() == 1 ||
           !CanStartOneFingerDrag(first_finger_hittest_))) {
        CompleteDrag(DragResult::SUCCESS);
        event->StopPropagation();
      }
      return;
    }
    case ui::ET_GESTURE_BEGIN: {
      if (event->details().touch_points() == 1) {
        first_finger_hittest_ = GetWindowComponent(target, *event);
      } else if (window_resizer_.get()) {
        if (!window_resizer_->IsMove()) {
          // The transition from resizing with one finger to resizing with two
          // fingers causes unintended resizing because the location of
          // ET_GESTURE_SCROLL_UPDATE jumps from the position of the first
          // finger to the position in the middle of the two fingers. For this
          // reason two finger resizing is not supported.
          CompleteDrag(DragResult::SUCCESS);
          event->StopPropagation();
        }
      } else {
        int second_finger_hittest = GetWindowComponent(target, *event);
        if (CanStartTwoFingerMove(target, first_finger_hittest_,
                                  second_finger_hittest)) {
          gfx::Point location_in_parent =
              event->details().bounding_box().CenterPoint();
          AttemptToStartDrag(target, location_in_parent, HTCAPTION,
                             aura::client::WINDOW_MOVE_SOURCE_TOUCH,
                             EndClosure());
          event->StopPropagation();
        }
      }
      return;
    }
    case ui::ET_GESTURE_SCROLL_BEGIN: {
      // The one finger drag is not started in ET_GESTURE_BEGIN to avoid the
      // window jumping upon initiating a two finger drag. When a one finger
      // drag is converted to a two finger drag, a jump occurs because the
      // location of the ET_GESTURE_SCROLL_UPDATE event switches from the single
      // finger's position to the position in the middle of the two fingers.
      if (window_resizer_.get())
        return;
      int component = GetWindowComponent(target, *event);
      if (!CanStartOneFingerDrag(component))
        return;
      gfx::Point location_in_parent(
          target->ConvertPointToTarget(target->GetParent(), event->location()));
      AttemptToStartDrag(target, location_in_parent, component,
                         aura::client::WINDOW_MOVE_SOURCE_TOUCH, EndClosure());
      event->StopPropagation();
      return;
    }
    default:
      break;
  }

  if (!window_resizer_.get())
    return;

  switch (event->type()) {
    case ui::ET_GESTURE_SCROLL_UPDATE:
      HandleDrag(target, event);
      event->StopPropagation();
      return;
    case ui::ET_GESTURE_SCROLL_END:
      // We must complete the drag here instead of as a result of ET_GESTURE_END
      // because otherwise the drag will be reverted when EndMoveLoop() is
      // called.
      // TODO(pkotwicz): Pass drag completion status to
      // WindowMoveClient::EndMoveLoop().
      CompleteDrag(DragResult::SUCCESS);
      event->StopPropagation();
      return;
    case ui::ET_SCROLL_FLING_START:
      CompleteDrag(DragResult::SUCCESS);

      // TODO(pkotwicz): Fix tests which inadvertantly start flings and check
      // window_resizer_->IsMove() instead of the hittest component at |event|'s
      // location.
      if (GetWindowComponent(target, *event) != HTCAPTION ||
          !target->GetWindowState()->IsNormalOrSnapped()) {
        return;
      }

      if (event->details().velocity_y() > kMinVertVelocityForWindowMinimize) {
        SetWindowStateTypeFromGesture(target, wm::WINDOW_STATE_TYPE_MINIMIZED);
      } else if (event->details().velocity_y() <
                 -kMinVertVelocityForWindowMinimize) {
        SetWindowStateTypeFromGesture(target, wm::WINDOW_STATE_TYPE_MAXIMIZED);
      } else if (event->details().velocity_x() >
                 kMinHorizVelocityForWindowSwipe) {
        SetWindowStateTypeFromGesture(target,
                                      wm::WINDOW_STATE_TYPE_RIGHT_SNAPPED);
      } else if (event->details().velocity_x() <
                 -kMinHorizVelocityForWindowSwipe) {
        SetWindowStateTypeFromGesture(target,
                                      wm::WINDOW_STATE_TYPE_LEFT_SNAPPED);
      }
      event->StopPropagation();
      return;
    case ui::ET_GESTURE_SWIPE:
      DCHECK_GT(event->details().touch_points(), 0);
      if (event->details().touch_points() == 1)
        return;
      if (!target->GetWindowState()->IsNormalOrSnapped())
        return;

      CompleteDrag(DragResult::SUCCESS);

      if (event->details().swipe_down()) {
        SetWindowStateTypeFromGesture(target, wm::WINDOW_STATE_TYPE_MINIMIZED);
      } else if (event->details().swipe_up()) {
        SetWindowStateTypeFromGesture(target, wm::WINDOW_STATE_TYPE_MAXIMIZED);
      } else if (event->details().swipe_right()) {
        SetWindowStateTypeFromGesture(target,
                                      wm::WINDOW_STATE_TYPE_RIGHT_SNAPPED);
      } else {
        SetWindowStateTypeFromGesture(target,
                                      wm::WINDOW_STATE_TYPE_LEFT_SNAPPED);
      }
      event->StopPropagation();
      return;
    default:
      return;
  }
}

bool WmToplevelWindowEventHandler::AttemptToStartDrag(
    WmWindow* window,
    const gfx::Point& point_in_parent,
    int window_component,
    aura::client::WindowMoveSource source,
    const EndClosure& end_closure) {
  if (window_resizer_.get())
    return false;
  std::unique_ptr<WindowResizer> resizer(CreateWindowResizer(
      window->aura_window(), point_in_parent, window_component, source));
  if (!resizer)
    return false;

  end_closure_ = end_closure;
  window_resizer_.reset(new ScopedWindowResizer(this, std::move(resizer)));

  pre_drag_window_bounds_ = window->GetBounds();
  in_gesture_drag_ = (source == aura::client::WINDOW_MOVE_SOURCE_TOUCH);
  return true;
}

void WmToplevelWindowEventHandler::RevertDrag() {
  CompleteDrag(DragResult::REVERT);
}

bool WmToplevelWindowEventHandler::CompleteDrag(DragResult result) {
  if (!window_resizer_)
    return false;

  std::unique_ptr<ScopedWindowResizer> resizer(std::move(window_resizer_));
  switch (result) {
    case DragResult::SUCCESS:
      resizer->resizer()->CompleteDrag();
      break;
    case DragResult::REVERT:
      resizer->resizer()->RevertDrag();
      break;
    case DragResult::WINDOW_DESTROYED:
      // We explicitly do not invoke RevertDrag() since that may do things to
      // the window that was destroyed.
      break;
  }

  first_finger_hittest_ = HTNOWHERE;
  in_gesture_drag_ = false;
  if (!end_closure_.is_null()) {
    // Clear local state in case running the closure deletes us.
    EndClosure end_closure = end_closure_;
    end_closure_.Reset();
    end_closure.Run(result);
  }
  return true;
}

void WmToplevelWindowEventHandler::HandleMousePressed(WmWindow* target,
                                                      ui::MouseEvent* event) {
  if (event->phase() != ui::EP_PRETARGET || !target->HasNonClientArea())
    return;

  // We also update the current window component here because for the
  // mouse-drag-release-press case, where the mouse is released and
  // pressed without mouse move event.
  int component = GetWindowComponent(target, *event);
  if ((event->flags() & (ui::EF_IS_DOUBLE_CLICK | ui::EF_IS_TRIPLE_CLICK)) ==
          0 &&
      WindowResizer::GetBoundsChangeForWindowComponent(component)) {
    gfx::Point location_in_parent(
        target->ConvertPointToTarget(target->GetParent(), event->location()));
    AttemptToStartDrag(target, location_in_parent, component,
                       aura::client::WINDOW_MOVE_SOURCE_MOUSE, EndClosure());
    // Set as handled so that other event handlers do no act upon the event
    // but still receive it so that they receive both parts of each pressed/
    // released pair.
    event->SetHandled();
  } else {
    CompleteDrag(DragResult::SUCCESS);
  }
}

void WmToplevelWindowEventHandler::HandleMouseReleased(WmWindow* target,
                                                       ui::MouseEvent* event) {
  if (event->phase() == ui::EP_PRETARGET)
    CompleteDrag(DragResult::SUCCESS);
}

void WmToplevelWindowEventHandler::HandleDrag(WmWindow* target,
                                              ui::LocatedEvent* event) {
  // This function only be triggered to move window
  // by mouse drag or touch move event.
  DCHECK(event->type() == ui::ET_MOUSE_DRAGGED ||
         event->type() == ui::ET_TOUCH_MOVED ||
         event->type() == ui::ET_GESTURE_SCROLL_UPDATE);

  // Drag actions are performed pre-target handling to prevent spurious mouse
  // moves from the move/size operation from being sent to the target.
  if (event->phase() != ui::EP_PRETARGET)
    return;

  if (!window_resizer_)
    return;
  window_resizer_->resizer()->Drag(
      target->ConvertPointToTarget(target->GetParent(), event->location()),
      event->flags());
  event->StopPropagation();
}

void WmToplevelWindowEventHandler::HandleMouseMoved(WmWindow* target,
                                                    ui::LocatedEvent* event) {
  // Shadow effects are applied after target handling. Note that we don't
  // respect ER_HANDLED here right now since we have not had a reason to allow
  // the target to cancel shadow rendering.
  if (event->phase() != ui::EP_POSTTARGET || !target->HasNonClientArea())
    return;

  // TODO(jamescook): Move the resize cursor update code into here from
  // CompoundEventFilter?
  if (event->flags() & ui::EF_IS_NON_CLIENT) {
    int component = target->GetNonClientComponent(event->location());
    target->ShowResizeShadow(component);
  } else {
    target->HideResizeShadow();
  }
}

void WmToplevelWindowEventHandler::HandleMouseExited(WmWindow* target,
                                                     ui::LocatedEvent* event) {
  // Shadow effects are applied after target handling. Note that we don't
  // respect ER_HANDLED here right now since we have not had a reason to allow
  // the target to cancel shadow rendering.
  if (event->phase() != ui::EP_POSTTARGET)
    return;

  target->HideResizeShadow();
}

void WmToplevelWindowEventHandler::HandleCaptureLost(ui::LocatedEvent* event) {
  if (event->phase() == ui::EP_PRETARGET) {
    // We complete the drag instead of reverting it, as reverting it will result
    // in a weird behavior when a dragged tab produces a modal dialog while the
    // drag is in progress. crbug.com/558201.
    CompleteDrag(DragResult::SUCCESS);
  }
}

void WmToplevelWindowEventHandler::SetWindowStateTypeFromGesture(
    WmWindow* window,
    wm::WindowStateType new_state_type) {
  wm::WindowState* window_state = window->GetWindowState();
  // TODO(oshima): Move extra logic (set_unminimize_to_restore_bounds,
  // SetRestoreBoundsInParent) that modifies the window state
  // into WindowState.
  switch (new_state_type) {
    case wm::WINDOW_STATE_TYPE_MINIMIZED:
      if (window_state->CanMinimize()) {
        window_state->Minimize();
        window_state->set_unminimize_to_restore_bounds(true);
        window_state->SetRestoreBoundsInParent(pre_drag_window_bounds_);
      }
      break;
    case wm::WINDOW_STATE_TYPE_MAXIMIZED:
      if (window_state->CanMaximize()) {
        window_state->SetRestoreBoundsInParent(pre_drag_window_bounds_);
        window_state->Maximize();
      }
      break;
    case wm::WINDOW_STATE_TYPE_LEFT_SNAPPED:
      if (window_state->CanSnap()) {
        window_state->SetRestoreBoundsInParent(pre_drag_window_bounds_);
        const wm::WMEvent event(wm::WM_EVENT_SNAP_LEFT);
        window_state->OnWMEvent(&event);
      }
      break;
    case wm::WINDOW_STATE_TYPE_RIGHT_SNAPPED:
      if (window_state->CanSnap()) {
        window_state->SetRestoreBoundsInParent(pre_drag_window_bounds_);
        const wm::WMEvent event(wm::WM_EVENT_SNAP_RIGHT);
        window_state->OnWMEvent(&event);
      }
      break;
    default:
      NOTREACHED();
  }
}

void WmToplevelWindowEventHandler::ResizerWindowDestroyed() {
  CompleteDrag(DragResult::WINDOW_DESTROYED);
}

void WmToplevelWindowEventHandler::OnDisplayConfigurationChanging() {
  CompleteDrag(DragResult::REVERT);
}

}  // namespace wm
}  // namespace ash
