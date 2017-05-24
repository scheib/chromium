// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/frame/EventHandlerRegistry.h"

#include "core/events/EventListenerOptions.h"
#include "core/events/EventUtil.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/LocalFrame.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/page/ChromeClient.h"
#include "core/page/scrolling/ScrollingCoordinator.h"

namespace blink {

namespace {

WebEventListenerProperties GetWebEventListenerProperties(bool has_blocking,
                                                         bool has_passive) {
  if (has_blocking && has_passive)
    return WebEventListenerProperties::kBlockingAndPassive;
  if (has_blocking)
    return WebEventListenerProperties::kBlocking;
  if (has_passive)
    return WebEventListenerProperties::kPassive;
  return WebEventListenerProperties::kNothing;
}

}  // namespace

EventHandlerRegistry::EventHandlerRegistry(Page& page) : page_(&page) {}

EventHandlerRegistry::~EventHandlerRegistry() {
  for (size_t i = 0; i < kEventHandlerClassCount; ++i) {
    EventHandlerClass handler_class = static_cast<EventHandlerClass>(i);
    CheckConsistency(handler_class);
  }
}

bool EventHandlerRegistry::EventTypeToClass(
    const AtomicString& event_type,
    const AddEventListenerOptions& options,
    EventHandlerClass* result) {
  if (event_type == EventTypeNames::scroll) {
    *result = kScrollEvent;
  } else if (event_type == EventTypeNames::wheel ||
             event_type == EventTypeNames::mousewheel) {
    *result = options.passive() ? kWheelEventPassive : kWheelEventBlocking;
  } else if (event_type == EventTypeNames::touchend ||
             event_type == EventTypeNames::touchcancel) {
    *result = options.passive() ? kTouchEndOrCancelEventPassive
                                : kTouchEndOrCancelEventBlocking;
  } else if (event_type == EventTypeNames::touchstart ||
             event_type == EventTypeNames::touchmove) {
    *result = options.passive() ? kTouchStartOrMoveEventPassive
                                : kTouchStartOrMoveEventBlocking;
  } else if (EventUtil::IsPointerEventType(event_type)) {
    // The EventHandlerClass is TouchStartOrMoveEventPassive since
    // the pointer events never block scrolling and the compositor
    // only needs to know about the touch listeners.
    *result = kTouchStartOrMoveEventPassive;
#if DCHECK_IS_ON()
  } else if (event_type == EventTypeNames::load ||
             event_type == EventTypeNames::mousemove ||
             event_type == EventTypeNames::touchstart) {
    *result = kEventsForTesting;
#endif
  } else {
    return false;
  }
  return true;
}

const EventTargetSet* EventHandlerRegistry::EventHandlerTargets(
    EventHandlerClass handler_class) const {
  CheckConsistency(handler_class);
  return &targets_[handler_class];
}

bool EventHandlerRegistry::HasEventHandlers(
    EventHandlerClass handler_class) const {
  CheckConsistency(handler_class);
  return targets_[handler_class].size();
}

bool EventHandlerRegistry::UpdateEventHandlerTargets(
    ChangeOperation op,
    EventHandlerClass handler_class,
    EventTarget* target) {
  EventTargetSet* targets = &targets_[handler_class];
  if (op == kAdd) {
    if (!targets->insert(target).is_new_entry) {
      // Just incremented refcount, no real change.
      return false;
    }
  } else {
    DCHECK(op == kRemove || op == kRemoveAll);
    DCHECK(op == kRemoveAll || targets->Contains(target));

    if (op == kRemoveAll) {
      if (!targets->Contains(target))
        return false;
      targets->RemoveAll(target);
    } else {
      if (!targets->erase(target)) {
        // Just decremented refcount, no real update.
        return false;
      }
    }
  }
  return true;
}

void EventHandlerRegistry::UpdateEventHandlerInternal(
    ChangeOperation op,
    EventHandlerClass handler_class,
    EventTarget* target) {
  bool had_handlers = targets_[handler_class].size();
  bool target_set_changed =
      UpdateEventHandlerTargets(op, handler_class, target);
  bool has_handlers = targets_[handler_class].size();

  if (had_handlers != has_handlers) {
    LocalFrame* frame = nullptr;
    if (Node* node = target->ToNode()) {
      frame = node->GetDocument().GetFrame();
    } else if (LocalDOMWindow* dom_window = target->ToLocalDOMWindow()) {
      frame = dom_window->GetFrame();
    } else {
      NOTREACHED() << "Unexpected target type for event handler.";
    }

    NotifyHasHandlersChanged(frame, handler_class, has_handlers);
  }

  if (target_set_changed)
    NotifyDidAddOrRemoveEventHandlerTarget(handler_class);
}

void EventHandlerRegistry::UpdateEventHandlerOfType(
    ChangeOperation op,
    const AtomicString& event_type,
    const AddEventListenerOptions& options,
    EventTarget* target) {
  EventHandlerClass handler_class;
  if (!EventTypeToClass(event_type, options, &handler_class))
    return;
  UpdateEventHandlerInternal(op, handler_class, target);
}

void EventHandlerRegistry::DidAddEventHandler(
    EventTarget& target,
    const AtomicString& event_type,
    const AddEventListenerOptions& options) {
  UpdateEventHandlerOfType(kAdd, event_type, options, &target);
}

void EventHandlerRegistry::DidRemoveEventHandler(
    EventTarget& target,
    const AtomicString& event_type,
    const AddEventListenerOptions& options) {
  UpdateEventHandlerOfType(kRemove, event_type, options, &target);
}

void EventHandlerRegistry::DidAddEventHandler(EventTarget& target,
                                              EventHandlerClass handler_class) {
  UpdateEventHandlerInternal(kAdd, handler_class, &target);
}

void EventHandlerRegistry::DidRemoveEventHandler(
    EventTarget& target,
    EventHandlerClass handler_class) {
  UpdateEventHandlerInternal(kRemove, handler_class, &target);
}

void EventHandlerRegistry::DidMoveIntoPage(EventTarget& target) {
  if (!target.HasEventListeners())
    return;

  // This code is not efficient at all.
  Vector<AtomicString> event_types = target.EventTypes();
  for (size_t i = 0; i < event_types.size(); ++i) {
    EventListenerVector* listeners = target.GetEventListeners(event_types[i]);
    if (!listeners)
      continue;
    for (unsigned count = listeners->size(); count > 0; --count) {
      EventHandlerClass handler_class;
      if (!EventTypeToClass(event_types[i], (*listeners)[count - 1].Options(),
                            &handler_class))
        continue;

      DidAddEventHandler(target, handler_class);
    }
  }
}

void EventHandlerRegistry::DidMoveOutOfPage(EventTarget& target) {
  DidRemoveAllEventHandlers(target);
}

void EventHandlerRegistry::DidRemoveAllEventHandlers(EventTarget& target) {
  for (size_t i = 0; i < kEventHandlerClassCount; ++i) {
    EventHandlerClass handler_class = static_cast<EventHandlerClass>(i);
    UpdateEventHandlerInternal(kRemoveAll, handler_class, &target);
  }
}

void EventHandlerRegistry::NotifyHasHandlersChanged(
    LocalFrame* frame,
    EventHandlerClass handler_class,
    bool has_active_handlers) {
  switch (handler_class) {
    case kScrollEvent:
      page_->GetChromeClient().SetHasScrollEventHandlers(frame,
                                                         has_active_handlers);
      break;
    case kWheelEventBlocking:
    case kWheelEventPassive:
      page_->GetChromeClient().SetEventListenerProperties(
          frame, WebEventListenerClass::kMouseWheel,
          GetWebEventListenerProperties(HasEventHandlers(kWheelEventBlocking),
                                        HasEventHandlers(kWheelEventPassive)));
      break;
    case kTouchStartOrMoveEventBlocking:
    case kTouchStartOrMoveEventPassive:
      page_->GetChromeClient().SetEventListenerProperties(
          frame, WebEventListenerClass::kTouchStartOrMove,
          GetWebEventListenerProperties(
              HasEventHandlers(kTouchStartOrMoveEventBlocking),
              HasEventHandlers(kTouchStartOrMoveEventPassive)));
      break;
    case kTouchEndOrCancelEventBlocking:
    case kTouchEndOrCancelEventPassive:
      page_->GetChromeClient().SetEventListenerProperties(
          frame, WebEventListenerClass::kTouchEndOrCancel,
          GetWebEventListenerProperties(
              HasEventHandlers(kTouchEndOrCancelEventBlocking),
              HasEventHandlers(kTouchEndOrCancelEventPassive)));
      break;
#if DCHECK_IS_ON()
    case kEventsForTesting:
      break;
#endif
    default:
      NOTREACHED();
      break;
  }
}

void EventHandlerRegistry::NotifyDidAddOrRemoveEventHandlerTarget(
    EventHandlerClass handler_class) {
  ScrollingCoordinator* scrolling_coordinator =
      page_->GetScrollingCoordinator();
  if (scrolling_coordinator && handler_class == kTouchStartOrMoveEventBlocking)
    scrolling_coordinator->TouchEventTargetRectsDidChange();
}

DEFINE_TRACE(EventHandlerRegistry) {
  visitor->Trace(page_);
  visitor->template RegisterWeakMembers<
      EventHandlerRegistry, &EventHandlerRegistry::ClearWeakMembers>(this);
}

void EventHandlerRegistry::ClearWeakMembers(Visitor* visitor) {
  Vector<UntracedMember<EventTarget>> dead_targets;
  for (size_t i = 0; i < kEventHandlerClassCount; ++i) {
    EventHandlerClass handler_class = static_cast<EventHandlerClass>(i);
    const EventTargetSet* targets = &targets_[handler_class];
    for (const auto& event_target : *targets) {
      Node* node = event_target.key->ToNode();
      LocalDOMWindow* window = event_target.key->ToLocalDOMWindow();
      if (node && !ThreadHeap::IsHeapObjectAlive(node)) {
        dead_targets.push_back(node);
      } else if (window && !ThreadHeap::IsHeapObjectAlive(window)) {
        dead_targets.push_back(window);
      }
    }
  }
  for (size_t i = 0; i < dead_targets.size(); ++i)
    DidRemoveAllEventHandlers(*dead_targets[i]);
}

void EventHandlerRegistry::DocumentDetached(Document& document) {
  // Remove all event targets under the detached document.
  for (size_t handler_class_index = 0;
       handler_class_index < kEventHandlerClassCount; ++handler_class_index) {
    EventHandlerClass handler_class =
        static_cast<EventHandlerClass>(handler_class_index);
    Vector<UntracedMember<EventTarget>> targets_to_remove;
    const EventTargetSet* targets = &targets_[handler_class];
    for (const auto& event_target : *targets) {
      if (Node* node = event_target.key->ToNode()) {
        for (Document* doc = &node->GetDocument(); doc;
             doc = doc->LocalOwner() ? &doc->LocalOwner()->GetDocument() : 0) {
          if (doc == &document) {
            targets_to_remove.push_back(event_target.key);
            break;
          }
        }
      } else if (event_target.key->ToLocalDOMWindow()) {
        // DOMWindows may outlive their documents, so we shouldn't remove their
        // handlers here.
      } else {
        NOTREACHED();
      }
    }
    for (size_t i = 0; i < targets_to_remove.size(); ++i)
      UpdateEventHandlerInternal(kRemoveAll, handler_class,
                                 targets_to_remove[i]);
  }
}

void EventHandlerRegistry::CheckConsistency(
    EventHandlerClass handler_class) const {
#if DCHECK_IS_ON()
  const EventTargetSet* targets = &targets_[handler_class];
  for (const auto& event_target : *targets) {
    if (Node* node = event_target.key->ToNode()) {
      // See the comment for |documentDetached| if either of these assertions
      // fails.
      DCHECK(node->GetDocument().GetPage());
      DCHECK(node->GetDocument().GetPage() == page_);
    } else if (LocalDOMWindow* window = event_target.key->ToLocalDOMWindow()) {
      // If any of these assertions fail, LocalDOMWindow failed to unregister
      // its handlers properly.
      DCHECK(window->GetFrame());
      DCHECK(window->GetFrame()->GetPage());
      DCHECK(window->GetFrame()->GetPage() == page_);
    }
  }
#endif  // DCHECK_IS_ON()
}

}  // namespace blink
