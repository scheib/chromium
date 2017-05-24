/*
 * Copyright (C) 2006, 2007 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Igalia S.L
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/page/ContextMenuController.h"

#include <memory>
#include "core/dom/Document.h"
#include "core/dom/Node.h"
#include "core/events/Event.h"
#include "core/events/MouseEvent.h"
#include "core/events/RelatedEvent.h"
#include "core/frame/LocalFrame.h"
#include "core/html/HTMLMenuElement.h"
#include "core/input/EventHandler.h"
#include "core/page/ContextMenuClient.h"
#include "core/page/ContextMenuProvider.h"
#include "core/page/CustomContextMenuProvider.h"
#include "platform/ContextMenu.h"
#include "platform/ContextMenuItem.h"
#include "platform/wtf/PtrUtil.h"

namespace blink {

using namespace HTMLNames;

ContextMenuController::ContextMenuController(Page*, ContextMenuClient* client)
    : client_(client) {
  DCHECK(client);
}

ContextMenuController::~ContextMenuController() {}

ContextMenuController* ContextMenuController::Create(
    Page* page,
    ContextMenuClient* client) {
  return new ContextMenuController(page, client);
}

DEFINE_TRACE(ContextMenuController) {
  visitor->Trace(menu_provider_);
  visitor->Trace(hit_test_result_);
}

void ContextMenuController::ClearContextMenu() {
  context_menu_.reset();
  if (menu_provider_)
    menu_provider_->ContextMenuCleared();
  menu_provider_ = nullptr;
  client_->ClearContextMenu();
  hit_test_result_ = HitTestResult();
}

void ContextMenuController::DocumentDetached(Document* document) {
  if (Node* inner_node = hit_test_result_.InnerNode()) {
    // Invalidate the context menu info if its target document is detached.
    if (inner_node->GetDocument() == document)
      ClearContextMenu();
  }
}

void ContextMenuController::PopulateCustomContextMenu(const Event& event) {
  if (!RuntimeEnabledFeatures::contextMenuEnabled())
    return;

  Node* node = event.target()->ToNode();
  if (!node || !node->IsHTMLElement())
    return;

  HTMLElement& element = ToHTMLElement(*node);
  HTMLMenuElement* menu_element = element.AssignedContextMenu();
  if (!menu_element || !DeprecatedEqualIgnoringCase(
                           menu_element->FastGetAttribute(typeAttr), "context"))
    return;
  RelatedEvent* related_event =
      RelatedEvent::Create(EventTypeNames::show, true, true, node);
  if (menu_element->DispatchEvent(related_event) !=
      DispatchEventResult::kNotCanceled)
    return;
  if (menu_element != element.AssignedContextMenu())
    return;
  menu_provider_ = CustomContextMenuProvider::Create(*menu_element, element);
  menu_provider_->PopulateContextMenu(context_menu_.get());
}

void ContextMenuController::HandleContextMenuEvent(Event* event) {
  context_menu_ = CreateContextMenu(event);
  if (!context_menu_)
    return;
  PopulateCustomContextMenu(*event);
  ShowContextMenu(event);
}

void ContextMenuController::ShowContextMenuAtPoint(
    LocalFrame* frame,
    float x,
    float y,
    ContextMenuProvider* menu_provider) {
  menu_provider_ = menu_provider;

  LayoutPoint location(x, y);
  context_menu_ = CreateContextMenu(frame, location);
  if (!context_menu_) {
    ClearContextMenu();
    return;
  }

  menu_provider_->PopulateContextMenu(context_menu_.get());
  ShowContextMenu(nullptr);
}

std::unique_ptr<ContextMenu> ContextMenuController::CreateContextMenu(
    Event* event) {
  DCHECK(event);

  if (!event->IsMouseEvent())
    return nullptr;

  MouseEvent* mouse_event = ToMouseEvent(event);
  return CreateContextMenu(event->target()->ToNode()->GetDocument().GetFrame(),
                           LayoutPoint(mouse_event->AbsoluteLocation()));
}

std::unique_ptr<ContextMenu> ContextMenuController::CreateContextMenu(
    LocalFrame* frame,
    const LayoutPoint& location) {
  HitTestRequest::HitTestRequestType type =
      HitTestRequest::kReadOnly | HitTestRequest::kActive;
  HitTestResult result(type, location);

  if (frame)
    result = frame->GetEventHandler().HitTestResultAtPoint(location, type);

  if (!result.InnerNodeOrImageMapImage())
    return nullptr;

  hit_test_result_ = result;

  return WTF::WrapUnique(new ContextMenu);
}

void ContextMenuController::ShowContextMenu(Event* event) {
  bool from_touch = false;
  if (event && event->IsMouseEvent()) {
    MouseEvent* mouse_event = static_cast<MouseEvent*>(event);
    from_touch = mouse_event->FromTouch();
  }

  if (client_->ShowContextMenu(context_menu_.get(), from_touch) && event)
    event->SetDefaultHandled();
}

void ContextMenuController::ContextMenuItemSelected(
    const ContextMenuItem* item) {
  DCHECK(item->GetType() == kActionType ||
         item->GetType() == kCheckableActionType);

  if (item->Action() < kContextMenuItemBaseCustomTag ||
      item->Action() > kContextMenuItemLastCustomTag)
    return;

  DCHECK(menu_provider_);
  menu_provider_->ContextMenuItemSelected(item);
}

}  // namespace blink
