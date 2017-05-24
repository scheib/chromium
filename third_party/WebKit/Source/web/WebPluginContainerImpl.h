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

#ifndef WebPluginContainerImpl_h
#define WebPluginContainerImpl_h

#include "core/dom/ContextLifecycleObserver.h"
#include "core/exported/WebPluginContainerBase.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Compiler.h"
#include "platform/wtf/PassRefPtr.h"
#include "platform/wtf/Vector.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/WebCoalescedInputEvent.h"
#include "public/platform/WebTouchEvent.h"
#include "public/web/WebPluginContainer.h"
#include "web/WebExport.h"

namespace blink {

class GestureEvent;
class HTMLFrameOwnerElement;
class HTMLPlugInElement;
class IntRect;
class KeyboardEvent;
class MouseEvent;
class ResourceError;
class ResourceResponse;
class TouchEvent;
class WebPlugin;
class WheelEvent;
struct WebPrintParams;
struct WebPrintPresetOptions;

class WEB_EXPORT WebPluginContainerImpl final : public WebPluginContainerBase {
  USING_PRE_FINALIZER(WebPluginContainerImpl, PreFinalize);

 public:
  static WebPluginContainerImpl* Create(HTMLPlugInElement* element,
                                        WebPlugin* web_plugin) {
    return new WebPluginContainerImpl(element, web_plugin);
  }
  ~WebPluginContainerImpl() override;

  // PluginView methods
  void SetParent(FrameView*) override;
  FrameView* Parent() const override { return parent_; };
  void SetParentVisible(bool) override;
  WebLayer* PlatformLayer() const override;
  v8::Local<v8::Object> ScriptableObject(v8::Isolate*) override;
  bool SupportsKeyboardFocus() const override;
  bool SupportsInputMethod() const override;
  bool CanProcessDrag() const override;
  bool WantsWheelEvents() override;
  void UpdateAllLifecyclePhases() override;
  void InvalidatePaint() override { IssuePaintInvalidations(); }
  void InvalidateRect(const IntRect&);
  void SetFocused(bool, WebFocusType) override;
  void HandleEvent(Event*) override;
  void FrameRectsChanged() override;
  void GeometryMayHaveChanged() override;
  bool IsPluginContainer() const override { return true; }
  bool IsErrorplaceholder() override;
  void EventListenersRemoved() override;

  // FrameOrPlugin methods
  void SetFrameRect(const IntRect& frame_rect) override {
    frame_rect_ = frame_rect;
  }
  const IntRect& FrameRect() const override { return frame_rect_; }
  void Paint(GraphicsContext&, const CullRect&) const override;
  void Show() override;
  void Hide() override;

  // WebPluginContainer methods
  WebElement GetElement() override;
  WebDocument GetDocument() override;
  void DispatchProgressEvent(const WebString& type,
                             bool length_computable,
                             unsigned long long loaded,
                             unsigned long long total,
                             const WebString& url) override;
  void EnqueueMessageEvent(const WebDOMMessageEvent&) override;
  void Invalidate() override;
  void InvalidateRect(const WebRect&) override;
  void ScrollRect(const WebRect&) override;
  void ScheduleAnimation() override;
  void ReportGeometry() override;
  v8::Local<v8::Object> V8ObjectForElement() override;
  WebString ExecuteScriptURL(const WebURL&, bool popups_allowed) override;
  void LoadFrameRequest(const WebURLRequest&, const WebString& target) override;
  bool IsRectTopmost(const WebRect&) override;
  void RequestTouchEventType(TouchEventRequestType) override;
  void SetWantsWheelEvents(bool) override;
  WebPoint RootFrameToLocalPoint(const WebPoint&) override;
  WebPoint LocalToRootFramePoint(const WebPoint&) override;

  // Non-Oilpan, this cannot be null. With Oilpan, it will be
  // null when in a disposed state, pending finalization during the next GC.
  WebPlugin* Plugin() override { return web_plugin_; }
  void SetPlugin(WebPlugin*) override;

  float DeviceScaleFactor() override;
  float PageScaleFactor() override;
  float PageZoomFactor() override;

  void SetWebLayer(WebLayer*) override;

  void RequestFullscreen() override;
  bool IsFullscreenElement() const override;
  void CancelFullscreen() override;

  // Printing interface. The plugin can support custom printing
  // (which means it controls the layout, number of pages etc).
  // Whether the plugin supports its own paginated print. The other print
  // interface methods are called only if this method returns true.
  bool SupportsPaginatedPrint() const override;
  // If the plugin content should not be scaled to the printable area of
  // the page, then this method should return true.
  bool IsPrintScalingDisabled() const override;
  // Returns true on success and sets the out parameter to the print preset
  // options for the document.
  bool GetPrintPresetOptionsFromDocument(WebPrintPresetOptions*) const override;
  // Sets up printing at the specified WebPrintParams. Returns the number of
  // pages to be printed at these settings.
  int PrintBegin(const WebPrintParams&) const override;
  // Prints the page specified by pageNumber (0-based index) into the supplied
  // canvas.
  void PrintPage(int page_number,
                 GraphicsContext&,
                 const IntRect& paint_rect) override;
  // Ends the print operation.
  void PrintEnd() override;

  // Copy the selected text.
  void Copy();

  // Pass the edit command to the plugin.
  bool ExecuteEditCommand(const WebString& name) override;
  bool ExecuteEditCommand(const WebString& name,
                          const WebString& value) override;

  // Resource load events for the plugin's source data:
  void DidReceiveResponse(const ResourceResponse&) override;
  void DidReceiveData(const char* data, int data_length) override;
  void DidFinishLoading() override;
  void DidFailLoading(const ResourceError&) override;

  DECLARE_VIRTUAL_TRACE();
  // USING_PRE_FINALIZER does not allow for virtual dispatch from the finalizer
  // method. Here we call Dispose() which does the correct virtual dispatch.
  void PreFinalize() { Dispose(); }
  void Dispose() override;

 private:
  // Sets |windowRect| to the content rect of the plugin in screen space.
  // Sets |clippedAbsoluteRect| to the visible rect for the plugin, clipped to
  // the visible screen of the root frame, in local space of the plugin.
  // Sets |unclippedAbsoluteRect| to the visible rect for the plugin (but
  // without also clipping to the screen), in local space of the plugin.
  void ComputeClipRectsForPlugin(
      const HTMLFrameOwnerElement* plugin_owner_element,
      IntRect& window_rect,
      IntRect& clipped_local_rect,
      IntRect& unclipped_int_local_rect) const;

  WebPluginContainerImpl(HTMLPlugInElement*, WebPlugin*);

  WebTouchEvent TransformTouchEvent(const WebInputEvent&);
  WebCoalescedInputEvent TransformCoalescedTouchEvent(
      const WebCoalescedInputEvent&);

  void HandleMouseEvent(MouseEvent*);
  void HandleDragEvent(MouseEvent*);
  void HandleWheelEvent(WheelEvent*);
  void HandleKeyboardEvent(KeyboardEvent*);
  void HandleTouchEvent(TouchEvent*);
  void HandleGestureEvent(GestureEvent*);

  void SynthesizeMouseEventIfPossible(TouchEvent*);

  void FocusPlugin();

  void IssuePaintInvalidations();

  void CalculateGeometry(IntRect& window_rect,
                         IntRect& clip_rect,
                         IntRect& unobscured_rect) override;

  friend class WebPluginContainerTest;

  Member<FrameView> parent_;
  Member<HTMLPlugInElement> element_;
  WebPlugin* web_plugin_;
  WebLayer* web_layer_;
  IntRect frame_rect_;
  IntRect pending_invalidation_rect_;
  TouchEventRequestType touch_event_request_type_;
  bool wants_wheel_events_;
  bool self_visible_;
  bool parent_visible_;
  bool is_disposed_;
};

DEFINE_TYPE_CASTS(WebPluginContainerImpl,
                  PluginView,
                  plugin,
                  plugin->IsPluginContainer(),
                  plugin.IsPluginContainer());
// Unlike FrameViewBase, we need not worry about object type for container.
// WebPluginContainerImpl is the only subclass of WebPluginContainer.
DEFINE_TYPE_CASTS(WebPluginContainerImpl,
                  WebPluginContainer,
                  container,
                  true,
                  true);

}  // namespace blink

#endif
