// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web/WebInputMethodControllerImpl.h"

#include "core/InputTypeNames.h"
#include "core/dom/DocumentUserGestureToken.h"
#include "core/editing/CompositionUnderlineVectorBuilder.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/Editor.h"
#include "core/editing/EphemeralRange.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/InputMethodController.h"
#include "core/editing/PlainTextRange.h"
#include "core/exported/WebPluginContainerBase.h"
#include "core/frame/LocalFrame.h"
#include "core/page/FocusController.h"
#include "core/page/Page.h"
#include "platform/UserGestureIndicator.h"
#include "public/platform/WebString.h"
#include "public/web/WebPlugin.h"
#include "public/web/WebRange.h"
#include "web/WebLocalFrameImpl.h"

namespace blink {

WebInputMethodControllerImpl::WebInputMethodControllerImpl(
    WebLocalFrameImpl* web_local_frame)
    : web_local_frame_(web_local_frame) {}

WebInputMethodControllerImpl::~WebInputMethodControllerImpl() {}

// static
WebInputMethodControllerImpl* WebInputMethodControllerImpl::FromFrame(
    LocalFrame* frame) {
  WebLocalFrameImpl* web_local_frame_impl = WebLocalFrameImpl::FromFrame(frame);
  return web_local_frame_impl ? web_local_frame_impl->GetInputMethodController()
                              : nullptr;
}

DEFINE_TRACE(WebInputMethodControllerImpl) {
  visitor->Trace(web_local_frame_);
}

bool WebInputMethodControllerImpl::SetComposition(
    const WebString& text,
    const WebVector<WebCompositionUnderline>& underlines,
    const WebRange& replacement_range,
    int selection_start,
    int selection_end) {
  if (WebPlugin* plugin = FocusedPluginIfInputMethodSupported()) {
    return plugin->SetComposition(text, underlines, replacement_range,
                                  selection_start, selection_end);
  }

  // We should use this |editor| object only to complete the ongoing
  // composition.
  if (!GetFrame()->GetEditor().CanEdit() &&
      !GetInputMethodController().HasComposition())
    return false;

  // Select the range to be replaced with the composition later.
  if (!replacement_range.IsNull())
    web_local_frame_->SelectRange(replacement_range);

  // We should verify the parent node of this IME composition node are
  // editable because JavaScript may delete a parent node of the composition
  // node. In this case, WebKit crashes while deleting texts from the parent
  // node, which doesn't exist any longer.
  const EphemeralRange range =
      GetInputMethodController().CompositionEphemeralRange();
  if (range.IsNotNull()) {
    Node* node = range.StartPosition().ComputeContainerNode();
    GetFrame()->GetDocument()->UpdateStyleAndLayoutTree();
    if (!node || !HasEditableStyle(*node))
      return false;
  }

  UserGestureIndicator gesture_indicator(DocumentUserGestureToken::Create(
      GetFrame()->GetDocument(), UserGestureToken::kNewGesture));

  // When the range of composition underlines overlap with the range between
  // selectionStart and selectionEnd, WebKit somehow won't paint the selection
  // at all (see InlineTextBox::paint() function in InlineTextBox.cpp).
  // But the selection range actually takes effect.
  GetInputMethodController().SetComposition(
      String(text), CompositionUnderlineVectorBuilder::Build(underlines),
      selection_start, selection_end);

  return text.IsEmpty() || GetInputMethodController().HasComposition();
}

bool WebInputMethodControllerImpl::FinishComposingText(
    ConfirmCompositionBehavior selection_behavior) {
  // TODO(ekaramad): Here and in other IME calls we should expect the
  // call to be made when our frame is focused. This, however, is not the case
  // all the time. For instance, resetInputMethod call on RenderViewImpl could
  // be after losing the focus on frame. But since we return the core frame
  // in WebViewImpl::focusedLocalFrameInWidget(), we will reach here with
  // |m_webLocalFrame| not focused on page.

  if (WebPlugin* plugin = FocusedPluginIfInputMethodSupported())
    return plugin->FinishComposingText(selection_behavior);

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  return GetInputMethodController().FinishComposingText(
      selection_behavior == WebInputMethodController::kKeepSelection
          ? InputMethodController::kKeepSelection
          : InputMethodController::kDoNotKeepSelection);
}

bool WebInputMethodControllerImpl::CommitText(
    const WebString& text,
    const WebVector<WebCompositionUnderline>& underlines,
    const WebRange& replacement_range,
    int relative_caret_position) {
  UserGestureIndicator gesture_indicator(DocumentUserGestureToken::Create(
      GetFrame()->GetDocument(), UserGestureToken::kNewGesture));

  if (WebPlugin* plugin = FocusedPluginIfInputMethodSupported()) {
    return plugin->CommitText(text, underlines, replacement_range,
                              relative_caret_position);
  }

  // Select the range to be replaced with the composition later.
  if (!replacement_range.IsNull())
    web_local_frame_->SelectRange(replacement_range);

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  return GetInputMethodController().CommitText(
      text, CompositionUnderlineVectorBuilder::Build(underlines),
      relative_caret_position);
}

WebTextInputInfo WebInputMethodControllerImpl::TextInputInfo() {
  return GetFrame()->GetInputMethodController().TextInputInfo();
}

WebTextInputType WebInputMethodControllerImpl::TextInputType() {
  return GetFrame()->GetInputMethodController().TextInputType();
}

LocalFrame* WebInputMethodControllerImpl::GetFrame() const {
  return web_local_frame_->GetFrame();
}

InputMethodController& WebInputMethodControllerImpl::GetInputMethodController()
    const {
  return GetFrame()->GetInputMethodController();
}

WebPlugin* WebInputMethodControllerImpl::FocusedPluginIfInputMethodSupported()
    const {
  WebPluginContainerBase* container =
      WebLocalFrameImpl::CurrentPluginContainer(GetFrame());
  if (container && container->SupportsInputMethod())
    return container->Plugin();
  return nullptr;
}

}  // namespace blink
