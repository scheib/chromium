// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/events/blink/web_input_event_traits.h"

#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "third_party/WebKit/public/platform/WebGestureEvent.h"
#include "third_party/WebKit/public/platform/WebKeyboardEvent.h"
#include "third_party/WebKit/public/platform/WebMouseWheelEvent.h"
#include "third_party/WebKit/public/platform/WebTouchEvent.h"

using base::StringAppendF;
using base::SStringPrintf;
using blink::WebGestureEvent;
using blink::WebInputEvent;
using blink::WebKeyboardEvent;
using blink::WebMouseEvent;
using blink::WebMouseWheelEvent;
using blink::WebTouchEvent;
using blink::WebTouchPoint;

namespace ui {
namespace {

void ApppendEventDetails(const WebKeyboardEvent& event, std::string* result) {
  StringAppendF(result,
                "{\n WinCode: %d\n NativeCode: %d\n IsSystem: %d\n"
                " Text: %s\n UnmodifiedText: %s\n}",
                event.windows_key_code, event.native_key_code,
                event.is_system_key, reinterpret_cast<const char*>(event.text),
                reinterpret_cast<const char*>(event.unmodified_text));
}

void ApppendEventDetails(const WebMouseEvent& event, std::string* result) {
  StringAppendF(result,
                "{\n Button: %d\n Pos: (%f, %f)\n"
                " GlobalPos: (%f, %f)\n Movement: (%d, %d)\n Clicks: %d\n}",
                static_cast<int>(event.button), event.PositionInWidget().x,
                event.PositionInWidget().y, event.PositionInScreen().x,
                event.PositionInScreen().y, event.movement_x, event.movement_y,
                event.click_count);
}

void ApppendEventDetails(const WebMouseWheelEvent& event, std::string* result) {
  StringAppendF(result,
                "{\n Delta: (%f, %f)\n WheelTicks: (%f, %f)\n Accel: (%f, %f)\n"
                " ScrollByPage: %d\n HasPreciseScrollingDeltas: %d\n"
                " Phase: (%d, %d)",
                event.delta_x, event.delta_y, event.wheel_ticks_x,
                event.wheel_ticks_y, event.acceleration_ratio_x,
                event.acceleration_ratio_y, event.scroll_by_page,
                event.has_precise_scrolling_deltas, event.phase,
                event.momentum_phase);
}

void ApppendEventDetails(const WebGestureEvent& event, std::string* result) {
  StringAppendF(
      result,
      "{\n Pos: (%d, %d)\n GlobalPos: (%d, %d)\n SourceDevice: %d\n"
      " RawData: (%f, %f, %f, %f, %d)\n}",
      event.x, event.y, event.global_x, event.global_y, event.source_device,
      event.data.scroll_update.delta_x, event.data.scroll_update.delta_y,
      event.data.scroll_update.velocity_x, event.data.scroll_update.velocity_y,
      event.data.scroll_update.previous_update_in_sequence_prevented);
}

void ApppendTouchPointDetails(const WebTouchPoint& point, std::string* result) {
  StringAppendF(result,
                "  (ID: %d, State: %d, ScreenPos: (%f, %f), Pos: (%f, %f),"
                " Radius: (%f, %f), Rot: %f, Force: %f,"
                " Tilt: (%d, %d)),\n",
                point.id, point.state, point.screen_position.x,
                point.screen_position.y, point.position.x, point.position.y,
                point.radius_x, point.radius_y, point.rotation_angle,
                point.force, point.tilt_x, point.tilt_y);
}

void ApppendEventDetails(const WebTouchEvent& event, std::string* result) {
  StringAppendF(result,
                "{\n Touches: %u, DispatchType: %d, CausesScrolling: %d,"
                " uniqueTouchEventId: %u\n[\n",
                event.touches_length, event.dispatch_type,
                event.moved_beyond_slop_region, event.unique_touch_event_id);
  for (unsigned i = 0; i < event.touches_length; ++i)
    ApppendTouchPointDetails(event.touches[i], result);
  result->append(" ]\n}");
}

struct WebInputEventDelete {
  template <class EventType>
  bool Execute(WebInputEvent* event, void*) const {
    if (!event)
      return false;
    DCHECK_EQ(sizeof(EventType), event->size());
    delete static_cast<EventType*>(event);
    return true;
  }
};

struct WebInputEventToString {
  template <class EventType>
  bool Execute(const WebInputEvent& event, std::string* result) const {
    SStringPrintf(result, "%s (Time: %lf, Modifiers: %d)\n",
                  WebInputEvent::GetName(event.GetType()),
                  event.TimeStampSeconds(), event.GetModifiers());
    const EventType& typed_event = static_cast<const EventType&>(event);
    ApppendEventDetails(typed_event, result);
    return true;
  }
};

struct WebInputEventSize {
  template <class EventType>
  bool Execute(WebInputEvent::Type /* type */, size_t* type_size) const {
    *type_size = sizeof(EventType);
    return true;
  }
};

struct WebInputEventClone {
  template <class EventType>
  bool Execute(const WebInputEvent& event,
               WebScopedInputEvent* scoped_event) const {
    DCHECK_EQ(sizeof(EventType), event.size());
    *scoped_event = WebScopedInputEvent(
        new EventType(static_cast<const EventType&>(event)));
    return true;
  }
};

template <typename Operator, typename ArgIn, typename ArgOut>
bool Apply(Operator op,
           WebInputEvent::Type type,
           const ArgIn& arg_in,
           ArgOut* arg_out) {
  if (WebInputEvent::IsMouseEventType(type))
    return op.template Execute<WebMouseEvent>(arg_in, arg_out);
  else if (type == WebInputEvent::kMouseWheel)
    return op.template Execute<WebMouseWheelEvent>(arg_in, arg_out);
  else if (WebInputEvent::IsKeyboardEventType(type))
    return op.template Execute<WebKeyboardEvent>(arg_in, arg_out);
  else if (WebInputEvent::IsTouchEventType(type))
    return op.template Execute<WebTouchEvent>(arg_in, arg_out);
  else if (WebInputEvent::IsGestureEventType(type))
    return op.template Execute<WebGestureEvent>(arg_in, arg_out);

  NOTREACHED() << "Unknown webkit event type " << type;
  return false;
}

}  // namespace

void WebInputEventDeleter::operator()(WebInputEvent* event) const {
  if (!event)
    return;
  void* temp = nullptr;
  Apply(WebInputEventDelete(), event->GetType(), event, temp);
}

std::string WebInputEventTraits::ToString(const WebInputEvent& event) {
  std::string result;
  Apply(WebInputEventToString(), event.GetType(), event, &result);
  return result;
}

size_t WebInputEventTraits::GetSize(WebInputEvent::Type type) {
  size_t size = 0;
  Apply(WebInputEventSize(), type, type, &size);
  return size;
}

WebScopedInputEvent WebInputEventTraits::Clone(const WebInputEvent& event) {
  WebScopedInputEvent scoped_event;
  Apply(WebInputEventClone(), event.GetType(), event, &scoped_event);
  return scoped_event;
}

bool WebInputEventTraits::ShouldBlockEventStream(
    const WebInputEvent& event,
    bool raf_aligned_touch_enabled) {
  switch (event.GetType()) {
    case WebInputEvent::kContextMenu:
    case WebInputEvent::kGestureScrollBegin:
    case WebInputEvent::kGestureScrollEnd:
    case WebInputEvent::kGestureShowPress:
    case WebInputEvent::kGestureTapUnconfirmed:
    case WebInputEvent::kGestureTapDown:
    case WebInputEvent::kGestureTapCancel:
    case WebInputEvent::kGesturePinchBegin:
    case WebInputEvent::kGesturePinchEnd:
      return false;

    // TouchCancel and TouchScrollStarted should always be non-blocking.
    case WebInputEvent::kTouchCancel:
    case WebInputEvent::kTouchScrollStarted:
      DCHECK_NE(WebInputEvent::kBlocking,
                static_cast<const WebTouchEvent&>(event).dispatch_type);
      return false;

    // Touch start and touch end indicate whether they are non-blocking
    // (aka uncancelable) on the event.
    case WebInputEvent::kTouchStart:
    case WebInputEvent::kTouchEnd:
      return static_cast<const WebTouchEvent&>(event).dispatch_type ==
             WebInputEvent::kBlocking;

    case WebInputEvent::kTouchMove:
      // Non-blocking touch moves can be ack'd right away if raf_aligned
      // touch is enabled.
      if (raf_aligned_touch_enabled) {
        return static_cast<const WebTouchEvent&>(event).dispatch_type ==
               WebInputEvent::kBlocking;
      }
      // Touch move events may be non-blocking but are always explicitly
      // acknowledge by the renderer so they block the event stream.
      return true;

    case WebInputEvent::kMouseWheel:
      return static_cast<const WebMouseWheelEvent&>(event).dispatch_type ==
             WebInputEvent::kBlocking;

    default:
      return true;
  }
}

bool WebInputEventTraits::CanCauseScroll(
    const blink::WebMouseWheelEvent& event) {
#if defined(USE_AURA)
  // Scroll events generated from the mouse wheel when the control key is held
  // don't trigger scrolling. Instead, they may cause zooming.
  return event.has_precise_scrolling_deltas ||
         (event.GetModifiers() & blink::WebInputEvent::kControlKey) == 0;
#else
  return true;
#endif
}

uint32_t WebInputEventTraits::GetUniqueTouchEventId(
    const WebInputEvent& event) {
  if (WebInputEvent::IsTouchEventType(event.GetType())) {
    return static_cast<const WebTouchEvent&>(event).unique_touch_event_id;
  }
  return 0U;
}

// static
LatencyInfo WebInputEventTraits::CreateLatencyInfoForWebGestureEvent(
    const WebGestureEvent& event) {
  SourceEventType source_event_type = SourceEventType::UNKNOWN;
  if (event.source_device ==
      blink::WebGestureDevice::kWebGestureDeviceTouchpad) {
    source_event_type = SourceEventType::WHEEL;
  } else if (event.source_device ==
             blink::WebGestureDevice::kWebGestureDeviceTouchscreen) {
    source_event_type = SourceEventType::TOUCH;
  }
  LatencyInfo latency_info(source_event_type);
  return latency_info;
}

}  // namespace ui
