/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 * Copyright (C) 2014 Opera Software ASA. All rights reserved.
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

#include "web/WebPluginContainerImpl.h"

#include "bindings/core/v8/ScriptController.h"
#include "bindings/core/v8/ScriptSourceCode.h"
#include "bindings/core/v8/V8BindingForCore.h"
#include "bindings/core/v8/V8Element.h"
#include "core/HTMLNames.h"
#include "core/clipboard/DataObject.h"
#include "core/clipboard/DataTransfer.h"
#include "core/dom/DocumentUserGestureToken.h"
#include "core/dom/ExecutionContext.h"
#include "core/dom/Fullscreen.h"
#include "core/events/DragEvent.h"
#include "core/events/EventQueue.h"
#include "core/events/GestureEvent.h"
#include "core/events/KeyboardEvent.h"
#include "core/events/MouseEvent.h"
#include "core/events/ProgressEvent.h"
#include "core/events/ResourceProgressEvent.h"
#include "core/events/TouchEvent.h"
#include "core/events/WebInputEventConversion.h"
#include "core/events/WheelEvent.h"
#include "core/exported/WebDataSourceImpl.h"
#include "core/exported/WebViewBase.h"
#include "core/frame/EventHandlerRegistry.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/WebLocalFrameBase.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/html/HTMLFormElement.h"
#include "core/html/HTMLPlugInElement.h"
#include "core/input/EventHandler.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutBox.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutPartItem.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/loader/FrameLoadRequest.h"
#include "core/page/ChromeClient.h"
#include "core/page/FocusController.h"
#include "core/page/Page.h"
#include "core/page/scrolling/ScrollingCoordinator.h"
#include "core/paint/LayoutObjectDrawingRecorder.h"
#include "core/paint/PaintLayer.h"
#include "platform/KeyboardCodes.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/UserGestureIndicator.h"
#include "platform/exported/WrappedResourceResponse.h"
#include "platform/geometry/LayoutRect.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/paint/CullRect.h"
#include "platform/graphics/paint/ForeignLayerDisplayItem.h"
#include "platform/scroll/ScrollAnimatorBase.h"
#include "platform/scroll/ScrollbarTheme.h"
#include "platform/wtf/Assertions.h"
#include "public/platform/Platform.h"
#include "public/platform/WebClipboard.h"
#include "public/platform/WebCoalescedInputEvent.h"
#include "public/platform/WebCompositorSupport.h"
#include "public/platform/WebCursorInfo.h"
#include "public/platform/WebDragData.h"
#include "public/platform/WebExternalTextureLayer.h"
#include "public/platform/WebInputEvent.h"
#include "public/platform/WebRect.h"
#include "public/platform/WebString.h"
#include "public/platform/WebURL.h"
#include "public/platform/WebURLError.h"
#include "public/platform/WebURLRequest.h"
#include "public/web/WebDOMMessageEvent.h"
#include "public/web/WebDocument.h"
#include "public/web/WebElement.h"
#include "public/web/WebFrameClient.h"
#include "public/web/WebPlugin.h"
#include "public/web/WebPrintParams.h"
#include "public/web/WebPrintPresetOptions.h"
#include "public/web/WebViewClient.h"

namespace blink {

// Public methods --------------------------------------------------------------

void WebPluginContainerImpl::SetParent(FrameView* parent) {
  DCHECK(!parent || !parent_);
  if (!parent || !parent->IsVisible())
    SetParentVisible(false);
  parent_ = parent;
  if (parent && parent->IsVisible())
    SetParentVisible(true);
}

void WebPluginContainerImpl::UpdateAllLifecyclePhases() {
  if (!web_plugin_)
    return;

  web_plugin_->UpdateAllLifecyclePhases();
}

void WebPluginContainerImpl::Paint(GraphicsContext& context,
                                   const CullRect& cull_rect) const {
  if (!parent_)
    return;

  // Don't paint anything if the plugin doesn't intersect.
  if (!cull_rect.IntersectsCullRect(frame_rect_))
    return;

  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled() && web_layer_) {
    // With Slimming Paint v2, composited plugins should have their layers
    // inserted rather than invoking WebPlugin::paint.
    RecordForeignLayer(context, *element_->GetLayoutObject(),
                       DisplayItem::kForeignLayerPlugin, web_layer_,
                       frame_rect_.Location(), frame_rect_.Size());
    return;
  }

  if (LayoutObjectDrawingRecorder::UseCachedDrawingIfPossible(
          context, *element_->GetLayoutObject(), DisplayItem::Type::kWebPlugin))
    return;

  LayoutObjectDrawingRecorder drawing_recorder(
      context, *element_->GetLayoutObject(), DisplayItem::Type::kWebPlugin,
      cull_rect.rect_);
  context.Save();

  // The plugin is positioned in the root frame's coordinates, so it needs to
  // be painted in them too.
  IntPoint origin = parent_->ContentsToRootFrame(IntPoint(0, 0));
  context.Translate(static_cast<float>(-origin.X()),
                    static_cast<float>(-origin.Y()));

  WebCanvas* canvas = context.Canvas();

  IntRect window_rect = parent_->ContentsToRootFrame(cull_rect.rect_);
  web_plugin_->Paint(canvas, window_rect);

  context.Restore();
}

void WebPluginContainerImpl::InvalidateRect(const IntRect& rect) {
  if (!parent_)
    return;

  LayoutBox* layout_object = ToLayoutBox(element_->GetLayoutObject());
  if (!layout_object)
    return;

  IntRect dirty_rect = rect;
  dirty_rect.Move(
      (layout_object->BorderLeft() + layout_object->PaddingLeft()).ToInt(),
      (layout_object->BorderTop() + layout_object->PaddingTop()).ToInt());

  pending_invalidation_rect_.Unite(dirty_rect);

  layout_object->SetMayNeedPaintInvalidation();
}

void WebPluginContainerImpl::SetFocused(bool focused, WebFocusType focus_type) {
  web_plugin_->UpdateFocus(focused, focus_type);
}

bool WebPluginContainerImpl::IsErrorplaceholder() {
  if (!web_plugin_)
    return false;
  return web_plugin_->IsErrorPlaceholder();
}

void WebPluginContainerImpl::Show() {
  self_visible_ = true;
  web_plugin_->UpdateVisibility(true);
}

void WebPluginContainerImpl::Hide() {
  self_visible_ = false;
  web_plugin_->UpdateVisibility(false);
}

void WebPluginContainerImpl::HandleEvent(Event* event) {
  // The events we pass are defined at:
  //    http://devedge-temp.mozilla.org/library/manuals/2002/plugin/1.0/structures5.html#1000000
  // Don't take the documentation as truth, however.  There are many cases
  // where mozilla behaves differently than the spec.
  if (event->IsMouseEvent())
    HandleMouseEvent(ToMouseEvent(event));
  else if (event->IsWheelEvent())
    HandleWheelEvent(ToWheelEvent(event));
  else if (event->IsKeyboardEvent())
    HandleKeyboardEvent(ToKeyboardEvent(event));
  else if (event->IsTouchEvent())
    HandleTouchEvent(ToTouchEvent(event));
  else if (event->IsGestureEvent())
    HandleGestureEvent(ToGestureEvent(event));
  else if (event->IsDragEvent() && web_plugin_->CanProcessDrag())
    HandleDragEvent(ToDragEvent(event));

  // FIXME: it would be cleaner if FrameViewBase::handleEvent returned
  // true/false and HTMLPluginElement called setDefaultHandled or
  // defaultEventHandler.
  if (!event->DefaultHandled())
    element_->Node::DefaultEventHandler(event);
}

void WebPluginContainerImpl::FrameRectsChanged() {
  ReportGeometry();
}

void WebPluginContainerImpl::GeometryMayHaveChanged() {
  ReportGeometry();
}

void WebPluginContainerImpl::EventListenersRemoved() {
  // We're no longer registered to receive touch events, so don't try to remove
  // the touch event handlers in our destructor.
  touch_event_request_type_ = kTouchEventRequestTypeNone;
}

void WebPluginContainerImpl::SetParentVisible(bool parent_visible) {
  // We override this function to make sure that geometry updates are sent
  // over to the plugin. For e.g. when a plugin is instantiated it does not
  // have a valid parent. As a result the first geometry update from webkit
  // is ignored. This function is called when the plugin eventually gets a
  // parent.

  if (parent_visible_ == parent_visible)
    return;  // No change.

  parent_visible_ = parent_visible;
  if (!self_visible_)
    return;  // This widget has explicitely been marked as not visible.

  if (web_plugin_)
    web_plugin_->UpdateVisibility(parent_visible_ && self_visible_);
}

void WebPluginContainerImpl::SetPlugin(WebPlugin* plugin) {
  if (plugin == web_plugin_)
    return;

  element_->ResetInstance();
  web_plugin_ = plugin;
  is_disposed_ = false;
}

float WebPluginContainerImpl::DeviceScaleFactor() {
  Page* page = element_->GetDocument().GetPage();
  if (!page)
    return 1.0;
  return page->DeviceScaleFactorDeprecated();
}

float WebPluginContainerImpl::PageScaleFactor() {
  Page* page = element_->GetDocument().GetPage();
  if (!page)
    return 1.0;
  return page->PageScaleFactor();
}

float WebPluginContainerImpl::PageZoomFactor() {
  LocalFrame* frame = element_->GetDocument().GetFrame();
  if (!frame)
    return 1.0;
  return frame->PageZoomFactor();
}

void WebPluginContainerImpl::SetWebLayer(WebLayer* layer) {
  if (web_layer_ == layer)
    return;

  if (web_layer_)
    GraphicsLayer::UnregisterContentsLayer(web_layer_);
  if (layer)
    GraphicsLayer::RegisterContentsLayer(layer);

  web_layer_ = layer;

  if (element_)
    element_->SetNeedsCompositingUpdate();
}

void WebPluginContainerImpl::RequestFullscreen() {
  Fullscreen::RequestFullscreen(*element_);
}

bool WebPluginContainerImpl::IsFullscreenElement() const {
  return Fullscreen::IsCurrentFullScreenElement(*element_);
}

void WebPluginContainerImpl::CancelFullscreen() {
  Fullscreen::FullyExitFullscreen(element_->GetDocument());
}

bool WebPluginContainerImpl::SupportsPaginatedPrint() const {
  return web_plugin_->SupportsPaginatedPrint();
}

bool WebPluginContainerImpl::IsPrintScalingDisabled() const {
  return web_plugin_->IsPrintScalingDisabled();
}

bool WebPluginContainerImpl::GetPrintPresetOptionsFromDocument(
    WebPrintPresetOptions* preset_options) const {
  return web_plugin_->GetPrintPresetOptionsFromDocument(preset_options);
}

int WebPluginContainerImpl::PrintBegin(
    const WebPrintParams& print_params) const {
  return web_plugin_->PrintBegin(print_params);
}

void WebPluginContainerImpl::PrintPage(int page_number,
                                       GraphicsContext& gc,
                                       const IntRect& print_rect) {
  if (LayoutObjectDrawingRecorder::UseCachedDrawingIfPossible(
          gc, *element_->GetLayoutObject(), DisplayItem::Type::kWebPlugin))
    return;

  LayoutObjectDrawingRecorder drawing_recorder(gc, *element_->GetLayoutObject(),
                                               DisplayItem::Type::kWebPlugin,
                                               print_rect);
  gc.Save();

  WebCanvas* canvas = gc.Canvas();
  web_plugin_->PrintPage(page_number, canvas);
  gc.Restore();
}

void WebPluginContainerImpl::PrintEnd() {
  web_plugin_->PrintEnd();
}

void WebPluginContainerImpl::Copy() {
  if (!web_plugin_->HasSelection())
    return;

  Platform::Current()->Clipboard()->WriteHTML(
      web_plugin_->SelectionAsMarkup(), WebURL(),
      web_plugin_->SelectionAsText(), false);
}

bool WebPluginContainerImpl::ExecuteEditCommand(const WebString& name) {
  if (web_plugin_->ExecuteEditCommand(name))
    return true;

  if (name != "Copy")
    return false;

  Copy();
  return true;
}

bool WebPluginContainerImpl::ExecuteEditCommand(const WebString& name,
                                                const WebString& value) {
  return web_plugin_->ExecuteEditCommand(name, value);
}

WebElement WebPluginContainerImpl::GetElement() {
  return WebElement(element_);
}

WebDocument WebPluginContainerImpl::GetDocument() {
  return WebDocument(&element_->GetDocument());
}

void WebPluginContainerImpl::DispatchProgressEvent(const WebString& type,
                                                   bool length_computable,
                                                   unsigned long long loaded,
                                                   unsigned long long total,
                                                   const WebString& url) {
  ProgressEvent* event;
  if (url.IsEmpty()) {
    event = ProgressEvent::Create(type, length_computable, loaded, total);
  } else {
    event = ResourceProgressEvent::Create(type, length_computable, loaded,
                                          total, url);
  }
  element_->DispatchEvent(event);
}

void WebPluginContainerImpl::EnqueueMessageEvent(
    const WebDOMMessageEvent& event) {
  static_cast<Event*>(event)->SetTarget(element_);
  element_->GetExecutionContext()->GetEventQueue()->EnqueueEvent(event);
}

void WebPluginContainerImpl::Invalidate() {
  InvalidateRect(IntRect(0, 0, frame_rect_.Width(), frame_rect_.Height()));
}

void WebPluginContainerImpl::InvalidateRect(const WebRect& rect) {
  InvalidateRect(static_cast<IntRect>(rect));
}

void WebPluginContainerImpl::ScrollRect(const WebRect& rect) {
  InvalidateRect(rect);
}

void WebPluginContainerImpl::ScheduleAnimation() {
  if (auto* frame_view = element_->GetDocument().View())
    frame_view->ScheduleAnimation();
}

void WebPluginContainerImpl::ReportGeometry() {
  // We cannot compute geometry without a parent or layoutObject.
  if (!parent_ || !element_ || !element_->GetLayoutObject() || !web_plugin_)
    return;

  IntRect window_rect, clip_rect, unobscured_rect;
  CalculateGeometry(window_rect, clip_rect, unobscured_rect);
  web_plugin_->UpdateGeometry(window_rect, clip_rect, unobscured_rect,
                              self_visible_);
}

v8::Local<v8::Object> WebPluginContainerImpl::V8ObjectForElement() {
  LocalFrame* frame = element_->GetDocument().GetFrame();
  if (!frame)
    return v8::Local<v8::Object>();

  if (!element_->GetDocument().CanExecuteScripts(kNotAboutToExecuteScript))
    return v8::Local<v8::Object>();

  ScriptState* script_state = ToScriptStateForMainWorld(frame);
  if (!script_state)
    return v8::Local<v8::Object>();

  v8::Local<v8::Value> v8value =
      ToV8(element_.Get(), script_state->GetContext()->Global(),
           script_state->GetIsolate());
  if (v8value.IsEmpty())
    return v8::Local<v8::Object>();
  DCHECK(v8value->IsObject());

  return v8::Local<v8::Object>::Cast(v8value);
}

WebString WebPluginContainerImpl::ExecuteScriptURL(const WebURL& url,
                                                   bool popups_allowed) {
  LocalFrame* frame = element_->GetDocument().GetFrame();
  if (!frame)
    return WebString();

  const KURL& kurl = url;
  DCHECK(kurl.ProtocolIs("javascript"));

  String script = DecodeURLEscapeSequences(
      kurl.GetString().Substring(strlen("javascript:")));

  if (!element_->GetDocument().GetContentSecurityPolicy()->AllowJavaScriptURLs(
          element_, script, element_->GetDocument().Url(), OrdinalNumber())) {
    return WebString();
  }

  UserGestureIndicator gesture_indicator(
      popups_allowed ? DocumentUserGestureToken::Create(
                           frame->GetDocument(), UserGestureToken::kNewGesture)
                     : nullptr);
  v8::HandleScope handle_scope(ToIsolate(frame));
  v8::Local<v8::Value> result =
      frame->GetScriptController().ExecuteScriptInMainWorldAndReturnValue(
          ScriptSourceCode(script));

  // Failure is reported as a null string.
  if (result.IsEmpty() || !result->IsString())
    return WebString();
  return ToCoreString(v8::Local<v8::String>::Cast(result));
}

void WebPluginContainerImpl::LoadFrameRequest(const WebURLRequest& request,
                                              const WebString& target) {
  LocalFrame* frame = element_->GetDocument().GetFrame();
  if (!frame || !frame->Loader().GetDocumentLoader())
    return;  // FIXME: send a notification in this case?

  FrameLoadRequest frame_request(frame->GetDocument(),
                                 request.ToResourceRequest(), target);
  frame->Loader().Load(frame_request);
}

bool WebPluginContainerImpl::IsRectTopmost(const WebRect& rect) {
  // Disallow access to the frame during dispose(), because it is not guaranteed
  // to be valid memory once this object has started disposal. In particular,
  // we might be being disposed because the frame has already be deleted and
  // then something else dropped the
  // last reference to the this object.
  if (is_disposed_ || !element_)
    return false;

  LocalFrame* frame = element_->GetDocument().GetFrame();
  if (!frame)
    return false;

  IntRect document_rect(frame_rect_.X() + rect.x, frame_rect_.Y() + rect.y,
                        rect.width, rect.height);
  // hitTestResultAtPoint() takes a padding rectangle.
  // FIXME: We'll be off by 1 when the width or height is even.
  LayoutPoint center = document_rect.Center();
  // Make the rect we're checking (the point surrounded by padding rects)
  // contained inside the requested rect. (Note that -1/2 is 0.)
  LayoutSize padding((document_rect.Width() - 1) / 2,
                     (document_rect.Height() - 1) / 2);
  HitTestResult result = frame->GetEventHandler().HitTestResultAtPoint(
      center,
      HitTestRequest::kReadOnly | HitTestRequest::kActive |
          HitTestRequest::kListBased,
      padding);
  const HitTestResult::NodeSet& nodes = result.ListBasedTestResult();
  if (nodes.size() != 1)
    return false;
  return nodes.front().Get() == element_;
}

void WebPluginContainerImpl::RequestTouchEventType(
    TouchEventRequestType request_type) {
  if (touch_event_request_type_ == request_type || !element_)
    return;

  if (Page* page = element_->GetDocument().GetPage()) {
    EventHandlerRegistry& registry = page->GetEventHandlerRegistry();
    if (request_type != kTouchEventRequestTypeNone &&
        touch_event_request_type_ == kTouchEventRequestTypeNone)
      registry.DidAddEventHandler(
          *element_, EventHandlerRegistry::kTouchStartOrMoveEventBlocking);
    else if (request_type == kTouchEventRequestTypeNone &&
             touch_event_request_type_ != kTouchEventRequestTypeNone)
      registry.DidRemoveEventHandler(
          *element_, EventHandlerRegistry::kTouchStartOrMoveEventBlocking);
  }
  touch_event_request_type_ = request_type;
}

void WebPluginContainerImpl::SetWantsWheelEvents(bool wants_wheel_events) {
  if (wants_wheel_events_ == wants_wheel_events)
    return;
  if (Page* page = element_->GetDocument().GetPage()) {
    EventHandlerRegistry& registry = page->GetEventHandlerRegistry();
    if (wants_wheel_events)
      registry.DidAddEventHandler(*element_,
                                  EventHandlerRegistry::kWheelEventBlocking);
    else
      registry.DidRemoveEventHandler(*element_,
                                     EventHandlerRegistry::kWheelEventBlocking);
  }

  wants_wheel_events_ = wants_wheel_events;
  if (Page* page = element_->GetDocument().GetPage()) {
    if (ScrollingCoordinator* scrolling_coordinator =
            page->GetScrollingCoordinator()) {
      if (parent_)
        scrolling_coordinator->NotifyGeometryChanged();
    }
  }
}

WebPoint WebPluginContainerImpl::RootFrameToLocalPoint(
    const WebPoint& point_in_root_frame) {
  if (!parent_)
    return point_in_root_frame;
  WebPoint point_in_content = parent_->RootFrameToContents(point_in_root_frame);
  return RoundedIntPoint(element_->GetLayoutObject()->AbsoluteToLocal(
      FloatPoint(point_in_content), kUseTransforms));
}

WebPoint WebPluginContainerImpl::LocalToRootFramePoint(
    const WebPoint& point_in_local) {
  if (!parent_)
    return point_in_local;
  IntPoint absolute_point =
      RoundedIntPoint(element_->GetLayoutObject()->LocalToAbsolute(
          FloatPoint(point_in_local), kUseTransforms));
  return parent_->ContentsToRootFrame(absolute_point);
}

void WebPluginContainerImpl::DidReceiveResponse(
    const ResourceResponse& response) {
  // Make sure that the plugin receives window geometry before data, or else
  // plugins misbehave.
  FrameRectsChanged();

  WrappedResourceResponse url_response(response);
  web_plugin_->DidReceiveResponse(url_response);
}

void WebPluginContainerImpl::DidReceiveData(const char* data, int data_length) {
  web_plugin_->DidReceiveData(data, data_length);
}

void WebPluginContainerImpl::DidFinishLoading() {
  web_plugin_->DidFinishLoading();
}

void WebPluginContainerImpl::DidFailLoading(const ResourceError& error) {
  web_plugin_->DidFailLoading(error);
}

WebLayer* WebPluginContainerImpl::PlatformLayer() const {
  return web_layer_;
}

v8::Local<v8::Object> WebPluginContainerImpl::ScriptableObject(
    v8::Isolate* isolate) {
  // With Oilpan, on plugin element detach dispose() will be called to safely
  // clear out references, including the pre-emptive destruction of the plugin.
  //
  // It clearly has no scriptable object if in such a disposed state.
  if (!web_plugin_)
    return v8::Local<v8::Object>();

  v8::Local<v8::Object> object = web_plugin_->V8ScriptableObject(isolate);

  // If the plugin has been destroyed and the reference on the stack is the
  // only one left, then don't return the scriptable object.
  if (!web_plugin_)
    return v8::Local<v8::Object>();

  return object;
}

bool WebPluginContainerImpl::SupportsKeyboardFocus() const {
  return web_plugin_->SupportsKeyboardFocus();
}

bool WebPluginContainerImpl::SupportsInputMethod() const {
  return web_plugin_->SupportsInputMethod();
}

bool WebPluginContainerImpl::CanProcessDrag() const {
  return web_plugin_->CanProcessDrag();
}

bool WebPluginContainerImpl::WantsWheelEvents() {
  return wants_wheel_events_;
}

// Private methods -------------------------------------------------------------

WebPluginContainerImpl::WebPluginContainerImpl(HTMLPlugInElement* element,
                                               WebPlugin* web_plugin)
    : WebPluginContainerBase(element->GetDocument().GetFrame()),
      element_(element),
      web_plugin_(web_plugin),
      web_layer_(nullptr),
      touch_event_request_type_(kTouchEventRequestTypeNone),
      wants_wheel_events_(false),
      self_visible_(false),
      parent_visible_(false),
      is_disposed_(false) {}

WebPluginContainerImpl::~WebPluginContainerImpl() {
  // The plugin container must have been disposed of by now.
  DCHECK(!web_plugin_);
}

void WebPluginContainerImpl::Dispose() {
  is_disposed_ = true;

  RequestTouchEventType(kTouchEventRequestTypeNone);
  SetWantsWheelEvents(false);

  if (web_plugin_) {
    CHECK(web_plugin_->Container() == this);
    web_plugin_->Destroy();
    web_plugin_ = nullptr;
  }

  if (web_layer_) {
    GraphicsLayer::UnregisterContentsLayer(web_layer_);
    web_layer_ = nullptr;
  }
}

DEFINE_TRACE(WebPluginContainerImpl) {
  visitor->Trace(parent_);
  visitor->Trace(element_);
  WebPluginContainerBase::Trace(visitor);
}

void WebPluginContainerImpl::HandleMouseEvent(MouseEvent* event) {
  // We cache the parent FrameView here as the plugin widget could be deleted
  // in the call to HandleEvent. See http://b/issue?id=1362948
  FrameView* parent_view = parent_;

  // TODO(dtapuska): Move WebMouseEventBuilder into the anonymous namespace
  // in this class.
  WebMouseEventBuilder transformed_event(
      parent_, LayoutItem(element_->GetLayoutObject()), *event);
  if (transformed_event.GetType() == WebInputEvent::kUndefined)
    return;

  if (event->type() == EventTypeNames::mousedown)
    FocusPlugin();

  WebCursorInfo cursor_info;
  if (web_plugin_ && web_plugin_->HandleInputEvent(
                         WebCoalescedInputEvent(transformed_event),
                         cursor_info) != WebInputEventResult::kNotHandled)
    event->SetDefaultHandled();

  // A windowless plugin can change the cursor in response to a mouse move
  // event.  We need to reflect the changed cursor in the frame view as the
  // mouse is moved in the boundaries of the windowless plugin.
  Page* page = parent_view->GetFrame().GetPage();
  if (!page)
    return;
  page->GetChromeClient().SetCursorForPlugin(
      cursor_info, &parent_view->GetFrame().LocalFrameRoot());
}

void WebPluginContainerImpl::HandleDragEvent(MouseEvent* event) {
  DCHECK(event->IsDragEvent());

  WebDragStatus drag_status = kWebDragStatusUnknown;
  if (event->type() == EventTypeNames::dragenter)
    drag_status = kWebDragStatusEnter;
  else if (event->type() == EventTypeNames::dragleave)
    drag_status = kWebDragStatusLeave;
  else if (event->type() == EventTypeNames::dragover)
    drag_status = kWebDragStatusOver;
  else if (event->type() == EventTypeNames::drop)
    drag_status = kWebDragStatusDrop;

  if (drag_status == kWebDragStatusUnknown)
    return;

  DataTransfer* data_transfer = event->getDataTransfer();
  WebDragData drag_data = data_transfer->GetDataObject()->ToWebDragData();
  WebDragOperationsMask drag_operation_mask =
      static_cast<WebDragOperationsMask>(data_transfer->SourceOperation());
  WebPoint drag_screen_location(event->screenX(), event->screenY());
  WebPoint drag_location(
      event->AbsoluteLocation().X() - frame_rect_.Location().X(),
      event->AbsoluteLocation().Y() - frame_rect_.Location().Y());

  web_plugin_->HandleDragStatusUpdate(drag_status, drag_data,
                                      drag_operation_mask, drag_location,
                                      drag_screen_location);
}

void WebPluginContainerImpl::HandleWheelEvent(WheelEvent* event) {
  WebFloatPoint absolute_location = event->NativeEvent().PositionInRootFrame();

  // Translate the root frame position to content coordinates.
  if (parent_) {
    absolute_location = parent_->RootFrameToContents(absolute_location);
  }

  IntPoint local_point =
      RoundedIntPoint(element_->GetLayoutObject()->AbsoluteToLocal(
          absolute_location, kUseTransforms));
  WebMouseWheelEvent translated_event = event->NativeEvent().FlattenTransform();
  translated_event.SetPositionInWidget(local_point.X(), local_point.Y());

  WebCursorInfo cursor_info;
  if (web_plugin_->HandleInputEvent(WebCoalescedInputEvent(translated_event),
                                    cursor_info) !=
      WebInputEventResult::kNotHandled)
    event->SetDefaultHandled();
}

void WebPluginContainerImpl::HandleKeyboardEvent(KeyboardEvent* event) {
  WebKeyboardEventBuilder web_event(*event);
  if (web_event.GetType() == WebInputEvent::kUndefined)
    return;

  if (web_event.GetType() == WebInputEvent::kKeyDown) {
#if OS(MACOSX)
    if ((web_event.GetModifiers() & WebInputEvent::kInputModifiers) ==
            WebInputEvent::kMetaKey
#else
    if ((web_event.GetModifiers() & WebInputEvent::kInputModifiers) ==
            WebInputEvent::kControlKey
#endif
        && (web_event.windows_key_code == VKEY_C ||
            web_event.windows_key_code == VKEY_INSERT)
        // Only copy if there's a selection, so that we only ever do this
        // for Pepper plugins that support copying.  Windowless NPAPI
        // plugins will get the event as before.
        && web_plugin_->HasSelection()) {
      Copy();
      event->SetDefaultHandled();
      return;
    }
  }

  // Give the client a chance to issue edit comamnds.
  WebLocalFrameBase* web_frame =
      WebLocalFrameBase::FromFrame(element_->GetDocument().GetFrame());
  if (web_plugin_->SupportsEditCommands())
    web_frame->Client()->HandleCurrentKeyboardEvent();

  WebCursorInfo cursor_info;
  if (web_plugin_->HandleInputEvent(WebCoalescedInputEvent(web_event),
                                    cursor_info) !=
      WebInputEventResult::kNotHandled)
    event->SetDefaultHandled();
}

WebTouchEvent WebPluginContainerImpl::TransformTouchEvent(
    const WebInputEvent& event) {
  DCHECK(blink::WebInputEvent::IsTouchEventType(event.GetType()));
  const WebTouchEvent* touch_event = static_cast<const WebTouchEvent*>(&event);
  WebTouchEvent transformed_event = touch_event->FlattenTransform();

  for (unsigned i = 0; i < transformed_event.touches_length; ++i) {
    WebFloatPoint absolute_location = transformed_event.touches[i].position;

    // Translate the root frame position to content coordinates.
    if (parent_) {
      absolute_location = parent_->RootFrameToContents(absolute_location);
    }

    IntPoint local_point =
        RoundedIntPoint(element_->GetLayoutObject()->AbsoluteToLocal(
            absolute_location, kUseTransforms));
    transformed_event.touches[i].position.x = local_point.X();
    transformed_event.touches[i].position.y = local_point.Y();
  }
  return transformed_event;
}

WebCoalescedInputEvent WebPluginContainerImpl::TransformCoalescedTouchEvent(
    const WebCoalescedInputEvent& coalesced_event) {
  WebCoalescedInputEvent transformed_event(
      TransformTouchEvent(coalesced_event.Event()),
      std::vector<const WebInputEvent*>());
  for (size_t i = 0; i < coalesced_event.CoalescedEventSize(); ++i) {
    transformed_event.AddCoalescedEvent(
        TransformTouchEvent(coalesced_event.CoalescedEvent(i)));
  }
  return transformed_event;
}

void WebPluginContainerImpl::HandleTouchEvent(TouchEvent* event) {
  switch (touch_event_request_type_) {
    case kTouchEventRequestTypeNone:
      return;
    case kTouchEventRequestTypeRaw: {
      if (!event->NativeEvent())
        return;

      if (event->type() == EventTypeNames::touchstart)
        FocusPlugin();

      WebCoalescedInputEvent transformed_event =
          TransformCoalescedTouchEvent(*event->NativeEvent());

      WebCursorInfo cursor_info;
      if (web_plugin_->HandleInputEvent(transformed_event, cursor_info) !=
          WebInputEventResult::kNotHandled)
        event->SetDefaultHandled();
      // FIXME: Can a plugin change the cursor from a touch-event callback?
      return;
    }
    case kTouchEventRequestTypeSynthesizedMouse:
      SynthesizeMouseEventIfPossible(event);
      return;
  }
}

void WebPluginContainerImpl::HandleGestureEvent(GestureEvent* event) {
  if (event->NativeEvent().GetType() == WebInputEvent::kUndefined)
    return;
  if (event->NativeEvent().GetType() == WebInputEvent::kGestureTapDown)
    FocusPlugin();

  // Take a copy of the event and translate it into the coordinate
  // system of the plugin.
  WebGestureEvent translated_event = event->NativeEvent();
  WebFloatPoint absolute_root_frame_location =
      event->NativeEvent().PositionInRootFrame();
  IntPoint local_point =
      RoundedIntPoint(element_->GetLayoutObject()->AbsoluteToLocal(
          absolute_root_frame_location, kUseTransforms));
  translated_event.FlattenTransform();
  translated_event.x = local_point.X();
  translated_event.y = local_point.Y();

  WebCursorInfo cursor_info;
  if (web_plugin_->HandleInputEvent(WebCoalescedInputEvent(translated_event),
                                    cursor_info) !=
      WebInputEventResult::kNotHandled) {
    event->SetDefaultHandled();
    return;
  }

  // FIXME: Can a plugin change the cursor from a touch-event callback?
}

void WebPluginContainerImpl::SynthesizeMouseEventIfPossible(TouchEvent* event) {
  WebMouseEventBuilder web_event(
      parent_, LayoutItem(element_->GetLayoutObject()), *event);
  if (web_event.GetType() == WebInputEvent::kUndefined)
    return;

  WebCursorInfo cursor_info;
  if (web_plugin_->HandleInputEvent(WebCoalescedInputEvent(web_event),
                                    cursor_info) !=
      WebInputEventResult::kNotHandled)
    event->SetDefaultHandled();
}

void WebPluginContainerImpl::FocusPlugin() {
  LocalFrame& containing_frame = parent_->GetFrame();
  if (Page* current_page = containing_frame.GetPage())
    current_page->GetFocusController().SetFocusedElement(element_,
                                                         &containing_frame);
  else
    containing_frame.GetDocument()->SetFocusedElement(
        element_, FocusParams(SelectionBehaviorOnFocus::kNone,
                              kWebFocusTypeNone, nullptr));
}

void WebPluginContainerImpl::IssuePaintInvalidations() {
  if (pending_invalidation_rect_.IsEmpty())
    return;

  LayoutBox* layout_object = ToLayoutBox(element_->GetLayoutObject());
  if (!layout_object)
    return;

  layout_object->InvalidatePaintRectangle(
      LayoutRect(pending_invalidation_rect_));
  pending_invalidation_rect_ = IntRect();
}

void WebPluginContainerImpl::ComputeClipRectsForPlugin(
    const HTMLFrameOwnerElement* owner_element,
    IntRect& window_rect,
    IntRect& clipped_local_rect,
    IntRect& unclipped_int_local_rect) const {
  DCHECK(owner_element);

  if (!owner_element->GetLayoutObject()) {
    clipped_local_rect = IntRect();
    unclipped_int_local_rect = IntRect();
    return;
  }

  LayoutView* root_view = element_->GetDocument().View()->GetLayoutView();
  while (root_view->GetFrame()->OwnerLayoutObject())
    root_view = root_view->GetFrame()->OwnerLayoutObject()->View();

  LayoutBox* box = ToLayoutBox(owner_element->GetLayoutObject());

  // Note: FrameRect() for this plugin is equal to contentBoxRect, mapped to the
  // containing view space, and rounded off.
  // See LayoutPart.cpp::updateGeometryInternal. To remove the lossy
  // effect of rounding off, use contentBoxRect directly.
  LayoutRect unclipped_absolute_rect(box->ContentBoxRect());
  box->MapToVisualRectInAncestorSpace(root_view, unclipped_absolute_rect);

  // The frameRect is already in absolute space of the local frame to the
  // plugin.
  window_rect = frame_rect_;
  // Map up to the root frame.
  LayoutRect layout_window_rect =
      LayoutRect(element_->GetDocument()
                     .View()
                     ->GetLayoutViewItem()
                     .LocalToAbsoluteQuad(FloatQuad(FloatRect(frame_rect_)),
                                          kTraverseDocumentBoundaries)
                     .BoundingBox());
  // Finally, adjust for scrolling of the root frame, which the above does not
  // take into account.
  layout_window_rect.MoveBy(-root_view->ViewRect().Location());
  window_rect = PixelSnappedIntRect(layout_window_rect);

  LayoutRect layout_clipped_local_rect = unclipped_absolute_rect;
  LayoutRect unclipped_layout_local_rect = layout_clipped_local_rect;
  layout_clipped_local_rect.Intersect(
      LayoutRect(root_view->GetFrameView()->VisibleContentRect()));

  unclipped_int_local_rect =
      box->AbsoluteToLocalQuad(FloatRect(unclipped_layout_local_rect),
                               kTraverseDocumentBoundaries | kUseTransforms)
          .EnclosingBoundingBox();
  // As a performance optimization, map the clipped rect separately if is
  // different than the unclipped rect.
  if (layout_clipped_local_rect != unclipped_layout_local_rect)
    clipped_local_rect =
        box->AbsoluteToLocalQuad(FloatRect(layout_clipped_local_rect),
                                 kTraverseDocumentBoundaries | kUseTransforms)
            .EnclosingBoundingBox();
  else
    clipped_local_rect = unclipped_int_local_rect;
}

void WebPluginContainerImpl::CalculateGeometry(IntRect& window_rect,
                                               IntRect& clip_rect,
                                               IntRect& unobscured_rect) {
  // document().layoutView() can be null when we receive messages from the
  // plugins while we are destroying a frame.
  // FIXME: Can we just check m_element->document().isActive() ?
  if (!element_->GetLayoutObject()
           ->GetDocument()
           .GetLayoutViewItem()
           .IsNull()) {
    // Take our element and get the clip rect from the enclosing layer and
    // frame view.
    ComputeClipRectsForPlugin(element_, window_rect, clip_rect,
                              unobscured_rect);
  }
}

}  // namespace blink
