/*
 * Copyright (C) 2014 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "web/WebFrameWidgetImpl.h"

#include <memory>

#include "core/dom/DocumentUserGestureToken.h"
#include "core/editing/CompositionUnderlineVectorBuilder.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/Editor.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/InputMethodController.h"
#include "core/editing/PlainTextRange.h"
#include "core/events/WebInputEventConversion.h"
#include "core/exported/WebPluginContainerBase.h"
#include "core/exported/WebViewBase.h"
#include "core/frame/FrameView.h"
#include "core/frame/RemoteFrame.h"
#include "core/frame/Settings.h"
#include "core/frame/VisualViewport.h"
#include "core/html/HTMLTextAreaElement.h"
#include "core/input/EventHandler.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/page/ContextMenuController.h"
#include "core/page/FocusController.h"
#include "core/page/Page.h"
#include "core/page/PointerLockController.h"
#include "platform/KeyboardCodes.h"
#include "platform/WebFrameScheduler.h"
#include "platform/animation/CompositorAnimationHost.h"
#include "platform/graphics/Color.h"
#include "platform/graphics/CompositorMutatorClient.h"
#include "platform/wtf/AutoReset.h"
#include "platform/wtf/PtrUtil.h"
#include "public/web/WebAutofillClient.h"
#include "public/web/WebPlugin.h"
#include "public/web/WebRange.h"
#include "public/web/WebWidgetClient.h"
#include "web/AnimationWorkletProxyClientImpl.h"
#include "web/CompositorMutatorImpl.h"
#include "web/CompositorWorkerProxyClientImpl.h"
#include "web/ContextMenuAllowedScope.h"
#include "web/WebDevToolsAgentImpl.h"
#include "web/WebInputMethodControllerImpl.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebPagePopupImpl.h"
#include "web/WebRemoteFrameImpl.h"
#include "web/WebViewFrameWidget.h"

namespace blink {

// WebFrameWidget ------------------------------------------------------------

WebFrameWidget* WebFrameWidget::Create(WebWidgetClient* client,
                                       WebLocalFrame* local_root) {
  DCHECK(client) << "A valid WebWidgetClient must be supplied.";
  // Pass the WebFrameWidget's self-reference to the caller.
  return WebFrameWidgetImpl::Create(client, local_root);
}

WebFrameWidget* WebFrameWidget::Create(WebWidgetClient* client,
                                       WebView* web_view,
                                       WebLocalFrame* main_frame) {
  DCHECK(client) << "A valid WebWidgetClient must be supplied.";
  return new WebViewFrameWidget(*client, static_cast<WebViewBase&>(*web_view),
                                ToWebLocalFrameImpl(*main_frame));
}

WebFrameWidgetImpl* WebFrameWidgetImpl::Create(WebWidgetClient* client,
                                               WebLocalFrame* local_root) {
  DCHECK(client) << "A valid WebWidgetClient must be supplied.";
  // Pass the WebFrameWidgetImpl's self-reference to the caller.
  return new WebFrameWidgetImpl(
      client, local_root);  // SelfKeepAlive is set in constructor.
}

WebFrameWidgetImpl::WebFrameWidgetImpl(WebWidgetClient* client,
                                       WebLocalFrame* local_root)
    : client_(client),
      local_root_(ToWebLocalFrameImpl(local_root)),
      mutator_(nullptr),
      layer_tree_view_(nullptr),
      root_layer_(nullptr),
      root_graphics_layer_(nullptr),
      is_accelerated_compositing_active_(false),
      layer_tree_view_closed_(false),
      suppress_next_keypress_event_(false),
      background_color_override_enabled_(false),
      background_color_override_(Color::kTransparent),
      base_background_color_override_enabled_(false),
      base_background_color_override_(Color::kTransparent),
      ime_accept_events_(true),
      self_keep_alive_(this) {
  DCHECK(local_root_->GetFrame()->IsLocalRoot());
  InitializeLayerTreeView();
  local_root_->SetFrameWidget(this);

  if (local_root->Parent())
    SetBackgroundColorOverride(Color::kTransparent);
}

WebFrameWidgetImpl::~WebFrameWidgetImpl() {}

DEFINE_TRACE(WebFrameWidgetImpl) {
  visitor->Trace(local_root_);
  visitor->Trace(mouse_capture_node_);
}

// WebWidget ------------------------------------------------------------------

void WebFrameWidgetImpl::Close() {
  local_root_->SetFrameWidget(nullptr);
  local_root_ = nullptr;
  // Reset the delegate to prevent notifications being sent as we're being
  // deleted.
  client_ = nullptr;

  mutator_ = nullptr;
  layer_tree_view_ = nullptr;
  root_layer_ = nullptr;
  root_graphics_layer_ = nullptr;
  animation_host_ = nullptr;

  self_keep_alive_.Clear();
}

WebSize WebFrameWidgetImpl::Size() {
  return size_;
}

void WebFrameWidgetImpl::Resize(const WebSize& new_size) {
  if (size_ == new_size)
    return;

  FrameView* view = local_root_->GetFrameView();
  if (!view)
    return;

  size_ = new_size;

  UpdateMainFrameLayoutSize();

  view->Resize(size_);

  // FIXME: In WebViewImpl this layout was a precursor to setting the minimum
  // scale limit.  It is not clear if this is necessary for frame-level widget
  // resize.
  if (view->NeedsLayout())
    view->UpdateLayout();

  // FIXME: Investigate whether this is needed; comment from eseidel suggests
  // that this function is flawed.
  SendResizeEventAndRepaint();
}

void WebFrameWidgetImpl::SendResizeEventAndRepaint() {
  // FIXME: This is wrong. The FrameView is responsible sending a resizeEvent
  // as part of layout. Layout is also responsible for sending invalidations
  // to the embedder. This method and all callers may be wrong. -- eseidel.
  if (local_root_->GetFrameView()) {
    // Enqueues the resize event.
    local_root_->GetFrame()->GetDocument()->EnqueueResizeEvent();
  }

  DCHECK(client_);
  if (IsAcceleratedCompositingActive()) {
    UpdateLayerTreeViewport();
  } else {
    WebRect damaged_rect(0, 0, size_.width, size_.height);
    client_->DidInvalidateRect(damaged_rect);
  }
}

void WebFrameWidgetImpl::ResizeVisualViewport(const WebSize& new_size) {
  // TODO(alexmos, kenrb): resizing behavior such as this should be changed
  // to use Page messages.  This uses the visual viewport size to set size on
  // both the WebViewImpl size and the Page's VisualViewport. If there are
  // multiple OOPIFs on a page, this will currently be set redundantly by
  // each of them. See https://crbug.com/599688.
  View()->Resize(new_size);

  View()->DidUpdateFullscreenSize();
}

void WebFrameWidgetImpl::UpdateMainFrameLayoutSize() {
  if (!local_root_)
    return;

  FrameView* view = local_root_->GetFrameView();
  if (!view)
    return;

  WebSize layout_size = size_;

  view->SetLayoutSize(layout_size);
}

void WebFrameWidgetImpl::DidEnterFullscreen() {
  View()->DidEnterFullscreen();
}

void WebFrameWidgetImpl::DidExitFullscreen() {
  View()->DidExitFullscreen();
}

void WebFrameWidgetImpl::SetSuppressFrameRequestsWorkaroundFor704763Only(
    bool suppress_frame_requests) {
  GetPage()->Animator().SetSuppressFrameRequestsWorkaroundFor704763Only(
      suppress_frame_requests);
}
void WebFrameWidgetImpl::BeginFrame(double last_frame_time_monotonic) {
  TRACE_EVENT1("blink", "WebFrameWidgetImpl::beginFrame", "frameTime",
               last_frame_time_monotonic);
  DCHECK(last_frame_time_monotonic);
  PageWidgetDelegate::Animate(*GetPage(), last_frame_time_monotonic);
}

void WebFrameWidgetImpl::UpdateAllLifecyclePhases() {
  TRACE_EVENT0("blink", "WebFrameWidgetImpl::updateAllLifecyclePhases");
  if (!local_root_)
    return;

  if (WebDevToolsAgentImpl* devtools = local_root_->DevToolsAgentImpl())
    devtools->PaintOverlay();
  PageWidgetDelegate::UpdateAllLifecyclePhases(*GetPage(),
                                               *local_root_->GetFrame());
  UpdateLayerTreeBackgroundColor();
}

void WebFrameWidgetImpl::Paint(WebCanvas* canvas, const WebRect& rect) {
  // Out-of-process iframes require compositing.
  NOTREACHED();
}

void WebFrameWidgetImpl::UpdateLayerTreeViewport() {
  if (!GetPage() || !layer_tree_view_)
    return;

  // Pass the limits even though this is for subframes, as the limits will be
  // needed in setting the raster scale.
  layer_tree_view_->SetPageScaleFactorAndLimits(
      1, View()->MinimumPageScaleFactor(), View()->MaximumPageScaleFactor());
}

void WebFrameWidgetImpl::UpdateLayerTreeBackgroundColor() {
  if (!layer_tree_view_)
    return;

  WebColor color = BackgroundColor();
  layer_tree_view_->SetBackgroundColor(color);
}

void WebFrameWidgetImpl::UpdateLayerTreeDeviceScaleFactor() {
  DCHECK(GetPage());
  DCHECK(layer_tree_view_);

  float device_scale_factor = GetPage()->DeviceScaleFactorDeprecated();
  layer_tree_view_->SetDeviceScaleFactor(device_scale_factor);
}

void WebFrameWidgetImpl::SetBackgroundColorOverride(WebColor color) {
  background_color_override_enabled_ = true;
  background_color_override_ = color;
  UpdateLayerTreeBackgroundColor();
}

void WebFrameWidgetImpl::ClearBackgroundColorOverride() {
  background_color_override_enabled_ = false;
  UpdateLayerTreeBackgroundColor();
}

void WebFrameWidgetImpl::SetBaseBackgroundColorOverride(WebColor color) {
  if (base_background_color_override_enabled_ &&
      base_background_color_override_ == color) {
    return;
  }

  base_background_color_override_enabled_ = true;
  base_background_color_override_ = color;
  // Force lifecycle update to ensure we're good to call
  // FrameView::setBaseBackgroundColor().
  local_root_->GetFrameView()->UpdateLifecycleToCompositingCleanPlusScrolling();
  UpdateBaseBackgroundColor();
}

void WebFrameWidgetImpl::ClearBaseBackgroundColorOverride() {
  if (!base_background_color_override_enabled_)
    return;

  base_background_color_override_enabled_ = false;
  // Force lifecycle update to ensure we're good to call
  // FrameView::setBaseBackgroundColor().
  local_root_->GetFrameView()->UpdateLifecycleToCompositingCleanPlusScrolling();
  UpdateBaseBackgroundColor();
}

void WebFrameWidgetImpl::LayoutAndPaintAsync(
    WebLayoutAndPaintAsyncCallback* callback) {
  layer_tree_view_->LayoutAndPaintAsync(callback);
}

void WebFrameWidgetImpl::CompositeAndReadbackAsync(
    WebCompositeAndReadbackAsyncCallback* callback) {
  layer_tree_view_->CompositeAndReadbackAsync(callback);
}

void WebFrameWidgetImpl::ThemeChanged() {
  FrameView* view = local_root_->GetFrameView();

  WebRect damaged_rect(0, 0, size_.width, size_.height);
  view->InvalidateRect(damaged_rect);
}

const WebInputEvent* WebFrameWidgetImpl::current_input_event_ = nullptr;

WebInputEventResult WebFrameWidgetImpl::HandleInputEvent(
    const WebCoalescedInputEvent& coalesced_event) {
  const WebInputEvent& input_event = coalesced_event.Event();
  TRACE_EVENT1("input", "WebFrameWidgetImpl::handleInputEvent", "type",
               WebInputEvent::GetName(input_event.GetType()));

  // If a drag-and-drop operation is in progress, ignore input events.
  if (doing_drag_and_drop_)
    return WebInputEventResult::kHandledSuppressed;

  // Don't handle events once we've started shutting down.
  if (!GetPage())
    return WebInputEventResult::kNotHandled;

  if (local_root_) {
    if (WebDevToolsAgentImpl* devtools = local_root_->DevToolsAgentImpl()) {
      if (devtools->HandleInputEvent(input_event))
        return WebInputEventResult::kHandledSuppressed;
    }
  }

  // Report the event to be NOT processed by WebKit, so that the browser can
  // handle it appropriately.
  if (IgnoreInputEvents())
    return WebInputEventResult::kNotHandled;

  // FIXME: pass event to m_localRoot's WebDevToolsAgentImpl once available.

  AutoReset<const WebInputEvent*> current_event_change(&current_input_event_,
                                                       &input_event);

  DCHECK(client_);
  if (client_->IsPointerLocked() &&
      WebInputEvent::IsMouseEventType(input_event.GetType())) {
    PointerLockMouseEvent(input_event);
    return WebInputEventResult::kHandledSystem;
  }

  if (mouse_capture_node_ &&
      WebInputEvent::IsMouseEventType(input_event.GetType())) {
    TRACE_EVENT1("input", "captured mouse event", "type",
                 input_event.GetType());
    // Save m_mouseCaptureNode since mouseCaptureLost() will clear it.
    Node* node = mouse_capture_node_;

    // Not all platforms call mouseCaptureLost() directly.
    if (input_event.GetType() == WebInputEvent::kMouseUp)
      MouseCaptureLost();

    std::unique_ptr<UserGestureIndicator> gesture_indicator;

    AtomicString event_type;
    switch (input_event.GetType()) {
      case WebInputEvent::kMouseMove:
        event_type = EventTypeNames::mousemove;
        break;
      case WebInputEvent::kMouseLeave:
        event_type = EventTypeNames::mouseout;
        break;
      case WebInputEvent::kMouseDown:
        event_type = EventTypeNames::mousedown;
        gesture_indicator = WTF::WrapUnique(
            new UserGestureIndicator(DocumentUserGestureToken::Create(
                &node->GetDocument(), UserGestureToken::kNewGesture)));
        mouse_capture_gesture_token_ = gesture_indicator->CurrentToken();
        break;
      case WebInputEvent::kMouseUp:
        event_type = EventTypeNames::mouseup;
        gesture_indicator = WTF::WrapUnique(
            new UserGestureIndicator(mouse_capture_gesture_token_.Release()));
        break;
      default:
        NOTREACHED();
    }

    WebMouseEvent transformed_event =
        TransformWebMouseEvent(local_root_->GetFrameView(),
                               static_cast<const WebMouseEvent&>(input_event));
    node->DispatchMouseEvent(transformed_event, event_type,
                             transformed_event.click_count);
    return WebInputEventResult::kHandledSystem;
  }

  return PageWidgetDelegate::HandleInputEvent(*this, coalesced_event,
                                              local_root_->GetFrame());
}

void WebFrameWidgetImpl::SetCursorVisibilityState(bool is_visible) {
  GetPage()->SetIsCursorVisible(is_visible);
}

bool WebFrameWidgetImpl::HasTouchEventHandlersAt(const WebPoint& point) {
  // FIXME: Implement this. Note that the point must be divided by
  // pageScaleFactor.
  return true;
}

Color WebFrameWidgetImpl::BaseBackgroundColor() const {
  return base_background_color_override_enabled_
             ? base_background_color_override_
             : base_background_color_;
}

void WebFrameWidgetImpl::SetBaseBackgroundColor(WebColor color) {
  if (base_background_color_ == color)
    return;

  base_background_color_ = color;
  UpdateBaseBackgroundColor();
}

void WebFrameWidgetImpl::UpdateBaseBackgroundColor() {
  local_root_->GetFrameView()->SetBaseBackgroundColor(BaseBackgroundColor());
}

WebInputMethodControllerImpl*
WebFrameWidgetImpl::GetActiveWebInputMethodController() const {
  return WebInputMethodControllerImpl::FromFrame(FocusedLocalFrameInWidget());
}

void WebFrameWidgetImpl::ScheduleAnimation() {
  if (layer_tree_view_) {
    layer_tree_view_->SetNeedsBeginFrame();
    return;
  }
  DCHECK(client_);
  client_->ScheduleAnimation();
}

CompositorMutatorImpl& WebFrameWidgetImpl::Mutator() {
  if (!mutator_) {
    std::unique_ptr<CompositorMutatorClient> mutator_client =
        CompositorMutatorImpl::CreateClient();
    mutator_ = static_cast<CompositorMutatorImpl*>(mutator_client->Mutator());
    layer_tree_view_->SetMutatorClient(std::move(mutator_client));
  }

  return *mutator_;
}

CompositorWorkerProxyClient*
WebFrameWidgetImpl::CreateCompositorWorkerProxyClient() {
  return new CompositorWorkerProxyClientImpl(&Mutator());
}

AnimationWorkletProxyClient*
WebFrameWidgetImpl::CreateAnimationWorkletProxyClient() {
  return new AnimationWorkletProxyClientImpl(&Mutator());
}

void WebFrameWidgetImpl::ApplyViewportDeltas(
    const WebFloatSize& visual_viewport_delta,
    const WebFloatSize& main_frame_delta,
    const WebFloatSize& elastic_overscroll_delta,
    float page_scale_delta,
    float browser_controls_delta) {
  // FIXME: To be implemented.
}

void WebFrameWidgetImpl::MouseCaptureLost() {
  TRACE_EVENT_ASYNC_END0("input", "capturing mouse", this);
  mouse_capture_node_ = nullptr;
}

void WebFrameWidgetImpl::SetFocus(bool enable) {
  GetPage()->GetFocusController().SetFocused(enable);
  if (enable) {
    GetPage()->GetFocusController().SetActive(true);
    LocalFrame* focused_frame = GetPage()->GetFocusController().FocusedFrame();
    if (focused_frame) {
      Element* element = focused_frame->GetDocument()->FocusedElement();
      if (element && focused_frame->Selection()
                         .ComputeVisibleSelectionInDOMTreeDeprecated()
                         .IsNone()) {
        // If the selection was cleared while the WebView was not
        // focused, then the focus element shows with a focus ring but
        // no caret and does respond to keyboard inputs.
        focused_frame->GetDocument()->UpdateStyleAndLayoutTree();
        if (element->IsTextControl()) {
          element->UpdateFocusAppearance(SelectionBehaviorOnFocus::kRestore);
        } else if (HasEditableStyle(*element)) {
          // updateFocusAppearance() selects all the text of
          // contentseditable DIVs. So we set the selection explicitly
          // instead. Note that this has the side effect of moving the
          // caret back to the beginning of the text.
          Position position(element, 0);
          focused_frame->Selection().SetSelection(
              SelectionInDOMTree::Builder().Collapse(position).Build());
        }
      }
    }
    ime_accept_events_ = true;
  } else {
    LocalFrame* focused_frame = FocusedLocalFrameInWidget();
    if (focused_frame) {
      // Finish an ongoing composition to delete the composition node.
      if (focused_frame->GetInputMethodController().HasComposition()) {
        // TODO(editing-dev): The use of
        // updateStyleAndLayoutIgnorePendingStylesheets needs to be audited.
        // See http://crbug.com/590369 for more details.
        focused_frame->GetDocument()
            ->UpdateStyleAndLayoutIgnorePendingStylesheets();

        focused_frame->GetInputMethodController().FinishComposingText(
            InputMethodController::kKeepSelection);
      }
      ime_accept_events_ = false;
    }
  }
}

// TODO(ekaramad):This method is almost duplicated in WebViewImpl as well. This
// code needs to be refactored  (http://crbug.com/629721).
WebRange WebFrameWidgetImpl::CompositionRange() {
  LocalFrame* focused = FocusedLocalFrameAvailableForIme();
  if (!focused)
    return WebRange();

  const EphemeralRange range =
      focused->GetInputMethodController().CompositionEphemeralRange();
  if (range.IsNull())
    return WebRange();

  Element* editable =
      focused->Selection().RootEditableElementOrDocumentElement();
  DCHECK(editable);

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  editable->GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  return PlainTextRange::Create(*editable, range);
}

WebColor WebFrameWidgetImpl::BackgroundColor() const {
  if (background_color_override_enabled_)
    return background_color_override_;
  if (!local_root_->GetFrameView())
    return base_background_color_;
  FrameView* view = local_root_->GetFrameView();
  return view->DocumentBackgroundColor().Rgb();
}

// TODO(ekaramad):This method is almost duplicated in WebViewImpl as well. This
// code needs to be refactored  (http://crbug.com/629721).
bool WebFrameWidgetImpl::SelectionBounds(WebRect& anchor,
                                         WebRect& focus) const {
  const LocalFrame* local_frame = FocusedLocalFrameInWidget();
  if (!local_frame)
    return false;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  local_frame->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      local_frame->GetDocument()->Lifecycle());

  FrameSelection& selection = local_frame->Selection();
  if (selection.ComputeVisibleSelectionInDOMTree().IsNone())
    return false;

  if (selection.ComputeVisibleSelectionInDOMTree().IsCaret()) {
    anchor = focus = selection.AbsoluteCaretBounds();
  } else {
    const EphemeralRange selected_range =
        selection.ComputeVisibleSelectionInDOMTree()
            .ToNormalizedEphemeralRange();
    if (selected_range.IsNull())
      return false;
    anchor = local_frame->GetEditor().FirstRectForRange(
        EphemeralRange(selected_range.StartPosition()));
    focus = local_frame->GetEditor().FirstRectForRange(
        EphemeralRange(selected_range.EndPosition()));
  }

  // FIXME: This doesn't apply page scale. This should probably be contents to
  // viewport. crbug.com/459293.
  IntRect scaled_anchor(local_frame->View()->ContentsToRootFrame(anchor));
  IntRect scaled_focus(local_frame->View()->ContentsToRootFrame(focus));

  anchor = scaled_anchor;
  focus = scaled_focus;

  if (!selection.ComputeVisibleSelectionInDOMTree().IsBaseFirst())
    std::swap(anchor, focus);
  return true;
}

// TODO(ekaramad):This method is almost duplicated in WebViewImpl as well. This
// code needs to be refactored  (http://crbug.com/629721).
bool WebFrameWidgetImpl::SelectionTextDirection(WebTextDirection& start,
                                                WebTextDirection& end) const {
  const LocalFrame* frame = FocusedLocalFrameInWidget();
  if (!frame)
    return false;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  frame->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  FrameSelection& selection = frame->Selection();
  if (selection.ComputeVisibleSelectionInDOMTree()
          .ToNormalizedEphemeralRange()
          .IsNull())
    return false;
  start = ToWebTextDirection(
      PrimaryDirectionOf(*selection.ComputeVisibleSelectionInDOMTreeDeprecated()
                              .Start()
                              .AnchorNode()));
  end = ToWebTextDirection(
      PrimaryDirectionOf(*selection.ComputeVisibleSelectionInDOMTreeDeprecated()
                              .end()
                              .AnchorNode()));
  return true;
}

// TODO(ekaramad):This method is almost duplicated in WebViewImpl as well. This
// code needs to be refactored  (http://crbug.com/629721).
bool WebFrameWidgetImpl::IsSelectionAnchorFirst() const {
  if (const LocalFrame* frame = FocusedLocalFrameInWidget()) {
    return frame->Selection()
        .ComputeVisibleSelectionInDOMTreeDeprecated()
        .IsBaseFirst();
  }
  return false;
}

// TODO(ekaramad):This method is almost duplicated in WebViewImpl as well. This
// code needs to be refactored  (http://crbug.com/629721).
WebRange WebFrameWidgetImpl::CaretOrSelectionRange() {
  LocalFrame* focused = FocusedLocalFrameInWidget();
  if (!focused)
    return WebRange();

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  focused->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  return focused->GetInputMethodController().GetSelectionOffsets();
}

void WebFrameWidgetImpl::SetTextDirection(WebTextDirection direction) {
  // The Editor::setBaseWritingDirection() function checks if we can change
  // the text direction of the selected node and updates its DOM "dir"
  // attribute and its CSS "direction" property.
  // So, we just call the function as Safari does.
  const LocalFrame* focused = FocusedLocalFrameInWidget();
  if (!focused)
    return;

  Editor& editor = focused->GetEditor();
  if (!editor.CanEdit())
    return;

  switch (direction) {
    case kWebTextDirectionDefault:
      editor.SetBaseWritingDirection(NaturalWritingDirection);
      break;

    case kWebTextDirectionLeftToRight:
      editor.SetBaseWritingDirection(LeftToRightWritingDirection);
      break;

    case kWebTextDirectionRightToLeft:
      editor.SetBaseWritingDirection(RightToLeftWritingDirection);
      break;

    default:
      NOTIMPLEMENTED();
      break;
  }
}

bool WebFrameWidgetImpl::IsAcceleratedCompositingActive() const {
  return is_accelerated_compositing_active_;
}

void WebFrameWidgetImpl::WillCloseLayerTreeView() {
  if (layer_tree_view_) {
    GetPage()->WillCloseLayerTreeView(*layer_tree_view_,
                                      local_root_->GetFrame()->View());
  }

  SetIsAcceleratedCompositingActive(false);
  mutator_ = nullptr;
  layer_tree_view_ = nullptr;
  animation_host_ = nullptr;
  layer_tree_view_closed_ = true;
}

// TODO(ekaramad):This method is almost duplicated in WebViewImpl as well. This
// code needs to be refactored  (http://crbug.com/629721).
bool WebFrameWidgetImpl::GetCompositionCharacterBounds(
    WebVector<WebRect>& bounds) {
  WebRange range = CompositionRange();
  if (range.IsEmpty())
    return false;

  LocalFrame* frame = FocusedLocalFrameInWidget();
  if (!frame)
    return false;

  WebLocalFrameImpl* web_local_frame = WebLocalFrameImpl::FromFrame(frame);
  size_t character_count = range.length();
  size_t offset = range.StartOffset();
  WebVector<WebRect> result(character_count);
  WebRect webrect;
  for (size_t i = 0; i < character_count; ++i) {
    if (!web_local_frame->FirstRectForCharacterRange(offset + i, 1, webrect)) {
      DLOG(ERROR) << "Could not retrieve character rectangle at " << i;
      return false;
    }
    result[i] = webrect;
  }

  bounds.Swap(result);
  return true;
}

void WebFrameWidgetImpl::SetRemoteViewportIntersection(
    const WebRect& viewport_intersection) {
  // Remote viewports are only applicable to local frames with remote ancestors.
  DCHECK(local_root_->Parent() && local_root_->Parent()->IsWebRemoteFrame());

  if (local_root_->GetFrameView()) {
    local_root_->GetFrameView()->SetViewportIntersectionFromParent(
        viewport_intersection);
  }
}

void WebFrameWidgetImpl::HandleMouseLeave(LocalFrame& main_frame,
                                          const WebMouseEvent& event) {
  // FIXME: WebWidget doesn't have the method below.
  // m_client->setMouseOverURL(WebURL());
  PageWidgetEventHandler::HandleMouseLeave(main_frame, event);
}

void WebFrameWidgetImpl::HandleMouseDown(LocalFrame& main_frame,
                                         const WebMouseEvent& event) {
  WebViewBase* view_impl = View();
  // If there is a popup open, close it as the user is clicking on the page
  // (outside of the popup). We also save it so we can prevent a click on an
  // element from immediately reopening the same popup.
  RefPtr<WebPagePopupImpl> page_popup;
  if (event.button == WebMouseEvent::Button::kLeft) {
    page_popup = ToWebPagePopupImpl(view_impl->GetPagePopup());
    view_impl->HidePopups();
  }

  // Take capture on a mouse down on a plugin so we can send it mouse events.
  // If the hit node is a plugin but a scrollbar is over it don't start mouse
  // capture because it will interfere with the scrollbar receiving events.
  IntPoint point(event.PositionInWidget().x, event.PositionInWidget().y);
  if (event.button == WebMouseEvent::Button::kLeft) {
    point = local_root_->GetFrameView()->RootFrameToContents(point);
    HitTestResult result(
        local_root_->GetFrame()->GetEventHandler().HitTestResultAtPoint(point));
    result.SetToShadowHostIfInRestrictedShadowRoot();
    Node* hit_node = result.InnerNode();

    if (!result.GetScrollbar() && hit_node && hit_node->GetLayoutObject() &&
        hit_node->GetLayoutObject()->IsEmbeddedObject()) {
      mouse_capture_node_ = hit_node;
      TRACE_EVENT_ASYNC_BEGIN0("input", "capturing mouse", this);
    }
  }

  PageWidgetEventHandler::HandleMouseDown(main_frame, event);

  if (event.button == WebMouseEvent::Button::kLeft && mouse_capture_node_)
    mouse_capture_gesture_token_ =
        main_frame.GetEventHandler().TakeLastMouseDownGestureToken();

  if (view_impl->GetPagePopup() && page_popup &&
      ToWebPagePopupImpl(view_impl->GetPagePopup())
          ->HasSamePopupClient(page_popup.Get())) {
    // That click triggered a page popup that is the same as the one we just
    // closed.  It needs to be closed.
    view_impl->HidePopups();
  }

  // Dispatch the contextmenu event regardless of if the click was swallowed.
  if (!GetPage()->GetSettings().GetShowContextMenuOnMouseUp()) {
#if OS(MACOSX)
    if (event.button == WebMouseEvent::Button::kRight ||
        (event.button == WebMouseEvent::Button::kLeft &&
         event.GetModifiers() & WebMouseEvent::kControlKey))
      MouseContextMenu(event);
#else
    if (event.button == WebMouseEvent::Button::kRight)
      MouseContextMenu(event);
#endif
  }
}

void WebFrameWidgetImpl::MouseContextMenu(const WebMouseEvent& event) {
  GetPage()->GetContextMenuController().ClearContextMenu();

  WebMouseEvent transformed_event =
      TransformWebMouseEvent(local_root_->GetFrameView(), event);
  IntPoint position_in_root_frame =
      FlooredIntPoint(transformed_event.PositionInRootFrame());

  // Find the right target frame. See issue 1186900.
  HitTestResult result = HitTestResultForRootFramePos(position_in_root_frame);
  Frame* target_frame;
  if (result.InnerNodeOrImageMapImage())
    target_frame = result.InnerNodeOrImageMapImage()->GetDocument().GetFrame();
  else
    target_frame = GetPage()->GetFocusController().FocusedOrMainFrame();

  // This will need to be changed to a nullptr check when focus control
  // is refactored, at which point focusedOrMainFrame will never return a
  // RemoteFrame.
  // See https://crbug.com/341918.
  if (!target_frame->IsLocalFrame())
    return;

  LocalFrame* target_local_frame = ToLocalFrame(target_frame);

  {
    ContextMenuAllowedScope scope;
    target_local_frame->GetEventHandler().SendContextMenuEvent(
        transformed_event, nullptr);
  }
  // Actually showing the context menu is handled by the ContextMenuClient
  // implementation...
}

void WebFrameWidgetImpl::HandleMouseUp(LocalFrame& main_frame,
                                       const WebMouseEvent& event) {
  PageWidgetEventHandler::HandleMouseUp(main_frame, event);

  if (GetPage()->GetSettings().GetShowContextMenuOnMouseUp()) {
    // Dispatch the contextmenu event regardless of if the click was swallowed.
    // On Mac/Linux, we handle it on mouse down, not up.
    if (event.button == WebMouseEvent::Button::kRight)
      MouseContextMenu(event);
  }
}

WebInputEventResult WebFrameWidgetImpl::HandleMouseWheel(
    LocalFrame& main_frame,
    const WebMouseWheelEvent& event) {
  View()->HidePopups();
  return PageWidgetEventHandler::HandleMouseWheel(main_frame, event);
}

WebInputEventResult WebFrameWidgetImpl::HandleGestureEvent(
    const WebGestureEvent& event) {
  DCHECK(client_);
  WebInputEventResult event_result = WebInputEventResult::kNotHandled;
  bool event_cancelled = false;

  WebViewBase* view_impl = View();
  switch (event.GetType()) {
    case WebInputEvent::kGestureScrollBegin:
    case WebInputEvent::kGestureScrollEnd:
    case WebInputEvent::kGestureScrollUpdate:
    case WebInputEvent::kGestureTap:
    case WebInputEvent::kGestureTapUnconfirmed:
    case WebInputEvent::kGestureTapDown:
      // Touch pinch zoom and scroll on the page (outside of a popup) must hide
      // the popup. In case of a touch scroll or pinch zoom, this function is
      // called with GestureTapDown rather than a GSB/GSU/GSE or GPB/GPU/GPE.
      // When we close a popup because of a GestureTapDown, we also save it so
      // we can prevent the following GestureTap from immediately reopening the
      // same popup.
      view_impl->SetLastHiddenPagePopup(
          ToWebPagePopupImpl(view_impl->GetPagePopup()));
      View()->HidePopups();
    case WebInputEvent::kGestureTapCancel:
      View()->SetLastHiddenPagePopup(nullptr);
    case WebInputEvent::kGestureShowPress:
    case WebInputEvent::kGestureDoubleTap:
    case WebInputEvent::kGestureTwoFingerTap:
    case WebInputEvent::kGestureLongPress:
    case WebInputEvent::kGestureLongTap:
      break;
    case WebInputEvent::kGestureFlingStart:
    case WebInputEvent::kGestureFlingCancel:
      client_->DidHandleGestureEvent(event, event_cancelled);
      return WebInputEventResult::kNotHandled;
    default:
      NOTREACHED();
  }
  LocalFrame* frame = local_root_->GetFrame();
  WebGestureEvent scaled_event = TransformWebGestureEvent(frame->View(), event);
  event_result = frame->GetEventHandler().HandleGestureEvent(scaled_event);
  client_->DidHandleGestureEvent(event, event_cancelled);
  return event_result;
}

WebInputEventResult WebFrameWidgetImpl::HandleKeyEvent(
    const WebKeyboardEvent& event) {
  DCHECK((event.GetType() == WebInputEvent::kRawKeyDown) ||
         (event.GetType() == WebInputEvent::kKeyDown) ||
         (event.GetType() == WebInputEvent::kKeyUp));

  // Please refer to the comments explaining the m_suppressNextKeypressEvent
  // member.
  // The m_suppressNextKeypressEvent is set if the KeyDown is handled by
  // Webkit. A keyDown event is typically associated with a keyPress(char)
  // event and a keyUp event. We reset this flag here as this is a new keyDown
  // event.
  suppress_next_keypress_event_ = false;

  Frame* focused_frame = FocusedCoreFrame();
  if (!focused_frame || !focused_frame->IsLocalFrame())
    return WebInputEventResult::kNotHandled;

  LocalFrame* frame = ToLocalFrame(focused_frame);

  WebInputEventResult result = frame->GetEventHandler().KeyEvent(event);
  if (result != WebInputEventResult::kNotHandled) {
    if (WebInputEvent::kRawKeyDown == event.GetType()) {
      // Suppress the next keypress event unless the focused node is a plugin
      // node.  (Flash needs these keypress events to handle non-US keyboards.)
      Element* element = FocusedElement();
      if (!element || !element->GetLayoutObject() ||
          !element->GetLayoutObject()->IsEmbeddedObject())
        suppress_next_keypress_event_ = true;
    }
    return result;
  }

#if !OS(MACOSX)
  const WebInputEvent::Type kContextMenuKeyTriggeringEventType =
#if OS(WIN)
      WebInputEvent::kKeyUp;
#else
      WebInputEvent::kRawKeyDown;
#endif
  const WebInputEvent::Type kShiftF10TriggeringEventType =
      WebInputEvent::kRawKeyDown;

  bool is_unmodified_menu_key =
      !(event.GetModifiers() & WebInputEvent::kInputModifiers) &&
      event.windows_key_code == VKEY_APPS;
  bool is_shift_f10 = (event.GetModifiers() & WebInputEvent::kInputModifiers) ==
                          WebInputEvent::kShiftKey &&
                      event.windows_key_code == VKEY_F10;
  if ((is_unmodified_menu_key &&
       event.GetType() == kContextMenuKeyTriggeringEventType) ||
      (is_shift_f10 && event.GetType() == kShiftF10TriggeringEventType)) {
    View()->SendContextMenuEvent(event);
    return WebInputEventResult::kHandledSystem;
  }
#endif  // !OS(MACOSX)

  return WebInputEventResult::kNotHandled;
}

WebInputEventResult WebFrameWidgetImpl::HandleCharEvent(
    const WebKeyboardEvent& event) {
  DCHECK_EQ(event.GetType(), WebInputEvent::kChar);

  // Please refer to the comments explaining the m_suppressNextKeypressEvent
  // member.  The m_suppressNextKeypressEvent is set if the KeyDown is
  // handled by Webkit. A keyDown event is typically associated with a
  // keyPress(char) event and a keyUp event. We reset this flag here as it
  // only applies to the current keyPress event.
  bool suppress = suppress_next_keypress_event_;
  suppress_next_keypress_event_ = false;

  LocalFrame* frame = ToLocalFrame(FocusedCoreFrame());
  if (!frame)
    return suppress ? WebInputEventResult::kHandledSuppressed
                    : WebInputEventResult::kNotHandled;

  EventHandler& handler = frame->GetEventHandler();

  if (!event.IsCharacterKey())
    return WebInputEventResult::kHandledSuppressed;

  // Accesskeys are triggered by char events and can't be suppressed.
  // It is unclear whether a keypress should be dispatched as well
  // crbug.com/563507
  if (handler.HandleAccessKey(event))
    return WebInputEventResult::kHandledSystem;

  // Safari 3.1 does not pass off windows system key messages (WM_SYSCHAR) to
  // the eventHandler::keyEvent. We mimic this behavior on all platforms since
  // for now we are converting other platform's key events to windows key
  // events.
  if (event.is_system_key)
    return WebInputEventResult::kNotHandled;

  if (suppress)
    return WebInputEventResult::kHandledSuppressed;

  WebInputEventResult result = handler.KeyEvent(event);
  if (result != WebInputEventResult::kNotHandled)
    return result;

  return WebInputEventResult::kNotHandled;
}

Frame* WebFrameWidgetImpl::FocusedCoreFrame() const {
  return GetPage() ? GetPage()->GetFocusController().FocusedOrMainFrame()
                   : nullptr;
}

Element* WebFrameWidgetImpl::FocusedElement() const {
  LocalFrame* frame = GetPage()->GetFocusController().FocusedFrame();
  if (!frame)
    return nullptr;

  Document* document = frame->GetDocument();
  if (!document)
    return nullptr;

  return document->FocusedElement();
}

void WebFrameWidgetImpl::InitializeLayerTreeView() {
  DCHECK(client_);
  DCHECK(!mutator_);
  layer_tree_view_ = client_->InitializeLayerTreeView();
  if (layer_tree_view_ && layer_tree_view_->CompositorAnimationHost()) {
    animation_host_ = WTF::MakeUnique<CompositorAnimationHost>(
        layer_tree_view_->CompositorAnimationHost());
  }

  if (WebDevToolsAgentImpl* dev_tools = local_root_->DevToolsAgentImpl())
    dev_tools->LayerTreeViewChanged(layer_tree_view_);

  GetPage()->GetSettings().SetAcceleratedCompositingEnabled(layer_tree_view_);
  if (layer_tree_view_) {
    GetPage()->LayerTreeViewInitialized(*layer_tree_view_,
                                        local_root_->GetFrame()->View());

    // TODO(kenrb): Currently GPU rasterization is always enabled for OOPIFs.
    // This is okay because it is only necessarily to set the trigger to false
    // for certain cases that affect the top-level frame, but it would be better
    // to be consistent with the top-level frame. Ideally the logic should
    // be moved from WebViewImpl into WebFrameWidget and used for all local
    // frame roots. https://crbug.com/712794
    layer_tree_view_->HeuristicsForGpuRasterizationUpdated(true);
  }

  // FIXME: only unittests, click to play, Android priting, and printing (for
  // headers and footers) make this assert necessary. We should make them not
  // hit this code and then delete allowsBrokenNullLayerTreeView.
  DCHECK(layer_tree_view_ || client_->AllowsBrokenNullLayerTreeView());
}

void WebFrameWidgetImpl::SetIsAcceleratedCompositingActive(bool active) {
  // In the middle of shutting down; don't try to spin back up a compositor.
  // FIXME: compositing startup/shutdown should be refactored so that it
  // turns on explicitly rather than lazily, which causes this awkwardness.
  if (layer_tree_view_closed_)
    return;

  DCHECK(!active || layer_tree_view_);

  if (is_accelerated_compositing_active_ == active)
    return;

  if (active) {
    TRACE_EVENT0("blink",
                 "WebViewImpl::setIsAcceleratedCompositingActive(true)");
    layer_tree_view_->SetRootLayer(*root_layer_);

    layer_tree_view_->SetVisible(GetPage()->IsPageVisible());
    UpdateLayerTreeDeviceScaleFactor();
    UpdateLayerTreeBackgroundColor();
    UpdateLayerTreeViewport();
    is_accelerated_compositing_active_ = true;
  }
}

PaintLayerCompositor* WebFrameWidgetImpl::Compositor() const {
  LocalFrame* frame = local_root_->GetFrame();
  if (!frame || !frame->GetDocument() ||
      frame->GetDocument()->GetLayoutViewItem().IsNull())
    return nullptr;

  return frame->GetDocument()->GetLayoutViewItem().Compositor();
}

void WebFrameWidgetImpl::SetRootGraphicsLayer(GraphicsLayer* layer) {
  root_graphics_layer_ = layer;
  root_layer_ = layer ? layer->PlatformLayer() : nullptr;

  SetIsAcceleratedCompositingActive(layer);

  if (!layer_tree_view_)
    return;

  if (root_layer_)
    layer_tree_view_->SetRootLayer(*root_layer_);
  else
    layer_tree_view_->ClearRootLayer();
}

void WebFrameWidgetImpl::SetRootLayer(WebLayer* layer) {
  root_layer_ = layer;

  SetIsAcceleratedCompositingActive(layer);

  if (!layer_tree_view_)
    return;

  if (root_layer_)
    layer_tree_view_->SetRootLayer(*root_layer_);
  else
    layer_tree_view_->ClearRootLayer();
}

WebLayerTreeView* WebFrameWidgetImpl::GetLayerTreeView() const {
  return layer_tree_view_;
}

CompositorAnimationHost* WebFrameWidgetImpl::AnimationHost() const {
  return animation_host_.get();
}

HitTestResult WebFrameWidgetImpl::CoreHitTestResultAt(
    const WebPoint& point_in_viewport) {
  DocumentLifecycle::AllowThrottlingScope throttling_scope(
      local_root_->GetFrame()->GetDocument()->Lifecycle());
  FrameView* view = local_root_->GetFrameView();
  IntPoint point_in_root_frame =
      view->ContentsToFrame(view->ViewportToContents(point_in_viewport));
  return HitTestResultForRootFramePos(point_in_root_frame);
}

void WebFrameWidgetImpl::SetVisibilityState(
    WebPageVisibilityState visibility_state) {
  if (layer_tree_view_)
    layer_tree_view_->SetVisible(visibility_state ==
                                 kWebPageVisibilityStateVisible);
}

HitTestResult WebFrameWidgetImpl::HitTestResultForRootFramePos(
    const IntPoint& pos_in_root_frame) {
  IntPoint doc_point(
      local_root_->GetFrame()->View()->RootFrameToContents(pos_in_root_frame));
  HitTestResult result =
      local_root_->GetFrame()->GetEventHandler().HitTestResultAtPoint(
          doc_point, HitTestRequest::kReadOnly | HitTestRequest::kActive);
  result.SetToShadowHostIfInRestrictedShadowRoot();
  return result;
}

LocalFrame* WebFrameWidgetImpl::FocusedLocalFrameInWidget() const {
  LocalFrame* frame = GetPage()->GetFocusController().FocusedFrame();
  return (frame && frame->LocalFrameRoot() == local_root_->GetFrame())
             ? frame
             : nullptr;
}

WebPlugin* WebFrameWidgetImpl::FocusedPluginIfInputMethodSupported(
    LocalFrame* frame) const {
  WebPluginContainerBase* container =
      WebLocalFrameImpl::CurrentPluginContainer(frame);
  if (container && container->SupportsInputMethod())
    return container->Plugin();
  return nullptr;
}

LocalFrame* WebFrameWidgetImpl::FocusedLocalFrameAvailableForIme() const {
  if (!ime_accept_events_)
    return nullptr;
  return FocusedLocalFrameInWidget();
}

}  // namespace blink
