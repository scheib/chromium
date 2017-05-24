/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
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

// How ownership works
// -------------------
//
// Big oh represents a refcounted relationship: owner O--- ownee
//
// WebView (for the toplevel frame only)
//    O
//    |           WebFrame
//    |              O
//    |              |
//   Page O------- LocalFrame (m_mainFrame) O-------O FrameView
//                   ||
//                   ||
//               FrameLoader
//
// FrameLoader and LocalFrame are formerly one object that was split apart
// because it got too big. They basically have the same lifetime, hence the
// double line.
//
// From the perspective of the embedder, WebFrame is simply an object that it
// allocates by calling WebFrame::create() and must be freed by calling close().
// Internally, WebFrame is actually refcounted and it holds a reference to its
// corresponding LocalFrame in blink.
//
// Oilpan: the middle objects + Page in the above diagram are Oilpan heap
// allocated, WebView and FrameView are currently not. In terms of ownership
// and control, the relationships stays the same, but the references from the
// off-heap WebView to the on-heap Page is handled by a Persistent<>, not a
// RefPtr<>. Similarly, the mutual strong references between the on-heap
// LocalFrame and the off-heap FrameView is through a RefPtr (from LocalFrame
// to FrameView), and a Persistent refers to the LocalFrame in the other
// direction.
//
// From the embedder's point of view, the use of Oilpan brings no changes.
// close() must still be used to signal that the embedder is through with the
// WebFrame.  Calling it will bring about the release and finalization of the
// frame object, and everything underneath.
//
// How frames are destroyed
// ------------------------
//
// The main frame is never destroyed and is re-used. The FrameLoader is re-used
// and a reference to the main frame is kept by the Page.
//
// When frame content is replaced, all subframes are destroyed. This happens
// in Frame::detachChildren for each subframe in a pre-order depth-first
// traversal. Note that child node order may not match DOM node order!
// detachChildren() (virtually) calls Frame::detach(), which again calls
// LocalFrameClient::detached(). This triggers WebFrame to clear its reference
// to LocalFrame. LocalFrameClient::detached() also notifies the embedder via
// WebFrameClient that the frame is detached. Most embedders will invoke
// close() on the WebFrame at this point, triggering its deletion unless
// something else is still retaining a reference.
//
// The client is expected to be set whenever the WebLocalFrameImpl is attached
// to the DOM.

#include "web/WebLocalFrameImpl.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "bindings/core/v8/BindingSecurity.h"
#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/ScriptController.h"
#include "bindings/core/v8/ScriptSourceCode.h"
#include "bindings/core/v8/ScriptValue.h"
#include "bindings/core/v8/SourceLocation.h"
#include "bindings/core/v8/V8BindingForCore.h"
#include "bindings/core/v8/V8GCController.h"
#include "core/HTMLNames.h"
#include "core/dom/Document.h"
#include "core/dom/DocumentUserGestureToken.h"
#include "core/dom/IconURL.h"
#include "core/dom/MessagePort.h"
#include "core/dom/Node.h"
#include "core/dom/NodeTraversal.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "core/editing/CompositionUnderlineVectorBuilder.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/Editor.h"
#include "core/editing/FindInPageCoordinates.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/InputMethodController.h"
#include "core/editing/PlainTextRange.h"
#include "core/editing/TextAffinity.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/editing/markers/DocumentMarkerController.h"
#include "core/editing/serializers/Serialization.h"
#include "core/editing/spellcheck/SpellChecker.h"
#include "core/exported/SharedWorkerRepositoryClientImpl.h"
#include "core/exported/WebAssociatedURLLoaderImpl.h"
#include "core/exported/WebDataSourceImpl.h"
#include "core/exported/WebPluginContainerBase.h"
#include "core/exported/WebViewBase.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/PageScaleConstraintsSet.h"
#include "core/frame/RemoteFrame.h"
#include "core/frame/ScreenOrientationController.h"
#include "core/frame/Settings.h"
#include "core/frame/SmartClip.h"
#include "core/frame/SuspendableScriptExecutor.h"
#include "core/frame/UseCounter.h"
#include "core/frame/VisualViewport.h"
#include "core/html/HTMLAnchorElement.h"
#include "core/html/HTMLCollection.h"
#include "core/html/HTMLFormElement.h"
#include "core/html/HTMLFrameElementBase.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/html/HTMLHeadElement.h"
#include "core/html/HTMLImageElement.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/HTMLLinkElement.h"
#include "core/html/PluginDocument.h"
#include "core/input/EventHandler.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutObject.h"
#include "core/layout/api/LayoutPartItem.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/FrameLoadRequest.h"
#include "core/loader/FrameLoader.h"
#include "core/loader/HistoryItem.h"
#include "core/loader/MixedContentChecker.h"
#include "core/loader/NavigationScheduler.h"
#include "core/page/FocusController.h"
#include "core/page/FrameTree.h"
#include "core/page/Page.h"
#include "core/page/PrintContext.h"
#include "core/paint/PaintLayer.h"
#include "core/paint/TransformRecorder.h"
#include "core/timing/DOMWindowPerformance.h"
#include "core/timing/Performance.h"
#include "platform/ScriptForbiddenScope.h"
#include "platform/UserGestureIndicator.h"
#include "platform/WebFrameScheduler.h"
#include "platform/bindings/DOMWrapperWorld.h"
#include "platform/bindings/V8PerIsolateData.h"
#include "platform/clipboard/ClipboardUtilities.h"
#include "platform/fonts/FontCache.h"
#include "platform/graphics/Color.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/GraphicsLayerClient.h"
#include "platform/graphics/paint/ClipRecorder.h"
#include "platform/graphics/paint/DisplayItemCacheSkipper.h"
#include "platform/graphics/paint/DrawingRecorder.h"
#include "platform/graphics/paint/PaintRecordBuilder.h"
#include "platform/graphics/skia/SkiaUtils.h"
#include "platform/heap/Handle.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/loader/fetch/FetchContext.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/loader/fetch/ResourceRequest.h"
#include "platform/loader/fetch/SubstituteData.h"
#include "platform/scroll/ScrollTypes.h"
#include "platform/scroll/ScrollbarTheme.h"
#include "platform/weborigin/KURL.h"
#include "platform/weborigin/SchemeRegistry.h"
#include "platform/weborigin/SecurityPolicy.h"
#include "platform/wtf/CurrentTime.h"
#include "platform/wtf/HashMap.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/WebDoubleSize.h"
#include "public/platform/WebFloatPoint.h"
#include "public/platform/WebFloatRect.h"
#include "public/platform/WebLayer.h"
#include "public/platform/WebPoint.h"
#include "public/platform/WebRect.h"
#include "public/platform/WebSecurityOrigin.h"
#include "public/platform/WebSize.h"
#include "public/platform/WebURLError.h"
#include "public/platform/WebURLLoader.h"
#include "public/platform/WebVector.h"
#include "public/web/WebAssociatedURLLoaderOptions.h"
#include "public/web/WebAutofillClient.h"
#include "public/web/WebConsoleMessage.h"
#include "public/web/WebDOMEvent.h"
#include "public/web/WebDocument.h"
#include "public/web/WebFindOptions.h"
#include "public/web/WebFormElement.h"
#include "public/web/WebFrameClient.h"
#include "public/web/WebFrameOwnerProperties.h"
#include "public/web/WebHistoryItem.h"
#include "public/web/WebIconURL.h"
#include "public/web/WebInputElement.h"
#include "public/web/WebKit.h"
#include "public/web/WebNode.h"
#include "public/web/WebPerformance.h"
#include "public/web/WebPlugin.h"
#include "public/web/WebPrintParams.h"
#include "public/web/WebPrintPresetOptions.h"
#include "public/web/WebRange.h"
#include "public/web/WebScriptSource.h"
#include "public/web/WebSerializedScriptValue.h"
#include "public/web/WebTreeScopeType.h"
#include "skia/ext/platform_canvas.h"
#include "web/RemoteFrameOwner.h"
#include "web/TextCheckerClientImpl.h"
#include "web/TextFinder.h"
#include "web/WebDevToolsAgentImpl.h"
#include "web/WebFrameWidgetImpl.h"
#include "web/WebRemoteFrameImpl.h"

namespace blink {

static int g_frame_count = 0;

static HeapVector<ScriptSourceCode> CreateSourcesVector(
    const WebScriptSource* sources_in,
    unsigned num_sources) {
  HeapVector<ScriptSourceCode> sources;
  sources.Append(sources_in, num_sources);
  return sources;
}

WebPluginContainerBase* WebLocalFrameImpl::PluginContainerFromFrame(
    LocalFrame* frame) {
  if (!frame)
    return 0;
  if (!frame->GetDocument() || !frame->GetDocument()->IsPluginDocument())
    return 0;
  PluginDocument* plugin_document = ToPluginDocument(frame->GetDocument());
  return ToWebPluginContainerBase(plugin_document->GetPluginView());
}

WebPluginContainerBase* WebLocalFrameImpl::CurrentPluginContainer(
    LocalFrame* frame,
    Node* node) {
  WebPluginContainerBase* plugin_container = PluginContainerFromFrame(frame);
  if (plugin_container)
    return plugin_container;

  if (!node) {
    DCHECK(frame->GetDocument());
    node = frame->GetDocument()->FocusedElement();
  }
  return ToWebPluginContainerBase(WebNode::PluginContainerFromNode(node));
}

// Simple class to override some of PrintContext behavior. Some of the methods
// made virtual so that they can be overridden by ChromePluginPrintContext.
class ChromePrintContext : public PrintContext {
  WTF_MAKE_NONCOPYABLE(ChromePrintContext);

 public:
  explicit ChromePrintContext(LocalFrame* frame)
      : PrintContext(frame), printed_page_width_(0) {}

  ~ChromePrintContext() override {}

  virtual void BeginPrintMode(float width, float height) {
    DCHECK(!printed_page_width_);
    printed_page_width_ = width;
    printed_page_height_ = height;
    PrintContext::BeginPrintMode(printed_page_width_, height);
  }

  virtual float GetPageShrink(int page_number) const {
    IntRect page_rect = page_rects_[page_number];
    return printed_page_width_ / page_rect.Width();
  }

  float SpoolSinglePage(WebCanvas* canvas, int page_number) {
    DispatchEventsForPrintingOnAllFrames();
    if (!GetFrame()->GetDocument() ||
        GetFrame()->GetDocument()->GetLayoutViewItem().IsNull())
      return 0;

    GetFrame()->View()->UpdateAllLifecyclePhasesExceptPaint();
    if (!GetFrame()->GetDocument() ||
        GetFrame()->GetDocument()->GetLayoutViewItem().IsNull())
      return 0;

    // The page rect gets scaled and translated, so specify the entire
    // print content area here as the recording rect.
    IntRect bounds(0, 0, printed_page_height_, printed_page_width_);
    PaintRecordBuilder builder(bounds, &canvas->getMetaData());
    builder.Context().SetPrinting(true);
    builder.Context().BeginRecording(bounds);
    float scale = SpoolPage(builder.Context(), page_number, bounds);
    canvas->drawPicture(builder.Context().EndRecording());
    return scale;
  }

  void SpoolAllPagesWithBoundaries(WebCanvas* canvas,
                                   const FloatSize& page_size_in_pixels) {
    DispatchEventsForPrintingOnAllFrames();
    if (!GetFrame()->GetDocument() ||
        GetFrame()->GetDocument()->GetLayoutViewItem().IsNull())
      return;

    GetFrame()->View()->UpdateAllLifecyclePhasesExceptPaint();
    if (!GetFrame()->GetDocument() ||
        GetFrame()->GetDocument()->GetLayoutViewItem().IsNull())
      return;

    ComputePageRects(page_size_in_pixels);

    const float page_width = page_size_in_pixels.Width();
    size_t num_pages = PageRects().size();
    int total_height = num_pages * (page_size_in_pixels.Height() + 1) - 1;
    IntRect all_pages_rect(0, 0, page_width, total_height);

    PaintRecordBuilder builder(all_pages_rect, &canvas->getMetaData());
    GraphicsContext& context = builder.Context();
    context.SetPrinting(true);
    context.BeginRecording(all_pages_rect);

    // Fill the whole background by white.
    context.FillRect(FloatRect(0, 0, page_width, total_height), Color::kWhite);

    int current_height = 0;
    for (size_t page_index = 0; page_index < num_pages; page_index++) {
      // Draw a line for a page boundary if this isn't the first page.
      if (page_index > 0) {
        context.Save();
        context.SetStrokeColor(Color(0, 0, 255));
        context.SetFillColor(Color(0, 0, 255));
        context.DrawLine(IntPoint(0, current_height),
                         IntPoint(page_width, current_height));
        context.Restore();
      }

      AffineTransform transform;
      transform.Translate(0, current_height);
#if OS(WIN) || OS(MACOSX)
      // Account for the disabling of scaling in spoolPage. In the context
      // of spoolAllPagesWithBoundaries the scale HAS NOT been pre-applied.
      float scale = GetPageShrink(page_index);
      transform.Scale(scale, scale);
#endif
      context.Save();
      context.ConcatCTM(transform);

      SpoolPage(context, page_index, all_pages_rect);

      context.Restore();

      current_height += page_size_in_pixels.Height() + 1;
    }
    canvas->drawPicture(context.EndRecording());
  }

 protected:
  // Spools the printed page, a subrect of frame(). Skip the scale step.
  // NativeTheme doesn't play well with scaling. Scaling is done browser side
  // instead. Returns the scale to be applied.
  // On Linux, we don't have the problem with NativeTheme, hence we let WebKit
  // do the scaling and ignore the return value.
  virtual float SpoolPage(GraphicsContext& context,
                          int page_number,
                          const IntRect& bounds) {
    IntRect page_rect = page_rects_[page_number];
    float scale = printed_page_width_ / page_rect.Width();

    AffineTransform transform;
#if OS(POSIX) && !OS(MACOSX)
    transform.Scale(scale);
#endif
    transform.Translate(static_cast<float>(-page_rect.X()),
                        static_cast<float>(-page_rect.Y()));
    context.Save();
    context.ConcatCTM(transform);
    context.ClipRect(page_rect);

    PaintRecordBuilder builder(bounds, &context.Canvas()->getMetaData(),
                               &context);

    // The local scope is so that the cache skipper is destroyed before
    // we call endRecording().
    {
      DisplayItemCacheSkipper skipper(builder.Context());
      GetFrame()->View()->PaintContents(builder.Context(),
                                        kGlobalPaintNormalPhase, page_rect);

      DrawingRecorder line_boundary_recorder(
          builder.Context(), builder,
          DisplayItem::kPrintedContentDestinationLocations, page_rect);
      OutputLinkedDestinations(builder.Context(), page_rect);
    }
    context.DrawRecord(builder.EndRecording());

    context.Restore();

    return scale;
  }

 private:
  void DispatchEventsForPrintingOnAllFrames() {
    HeapVector<Member<Document>> documents;
    for (Frame* current_frame = GetFrame(); current_frame;
         current_frame = current_frame->Tree().TraverseNext(GetFrame())) {
      if (current_frame->IsLocalFrame())
        documents.push_back(ToLocalFrame(current_frame)->GetDocument());
    }

    for (auto& doc : documents)
      doc->DispatchEventsForPrinting();
  }

  // Set when printing.
  float printed_page_width_;
  float printed_page_height_;
};

// Simple class to override some of PrintContext behavior. This is used when
// the frame hosts a plugin that supports custom printing. In this case, we
// want to delegate all printing related calls to the plugin.
class ChromePluginPrintContext final : public ChromePrintContext {
 public:
  ChromePluginPrintContext(LocalFrame* frame,
                           WebPluginContainerBase* plugin,
                           const WebPrintParams& print_params)
      : ChromePrintContext(frame),
        plugin_(plugin),
        print_params_(print_params) {}

  ~ChromePluginPrintContext() override {}

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(plugin_);
    ChromePrintContext::Trace(visitor);
  }

  void BeginPrintMode(float width, float height) override {}

  void EndPrintMode() override { plugin_->PrintEnd(); }

  float GetPageShrink(int page_number) const override {
    // We don't shrink the page (maybe we should ask the widget ??)
    return 1.0;
  }

  void ComputePageRects(const FloatSize& print_size) override {
    IntRect rect(FloatRect(FloatPoint(0, 0), print_size));
    print_params_.print_content_area = rect;
    page_rects_.Fill(rect, plugin_->PrintBegin(print_params_));
  }

  void ComputePageRectsWithPageSize(
      const FloatSize& page_size_in_pixels) override {
    NOTREACHED();
  }

 protected:
  // Spools the printed page, a subrect of frame(). Skip the scale step.
  // NativeTheme doesn't play well with scaling. Scaling is done browser side
  // instead. Returns the scale to be applied.
  float SpoolPage(GraphicsContext& context,
                  int page_number,
                  const IntRect& bounds) override {
    IntRect page_rect = page_rects_[page_number];
    PaintRecordBuilder builder(bounds, &context.Canvas()->getMetaData());
    // The local scope is so that the cache skipper is destroyed before
    // we call endRecording().
    {
      DisplayItemCacheSkipper skipper(builder.Context());
      plugin_->PrintPage(page_number, builder.Context(), page_rect);
    }
    context.DrawRecord(builder.EndRecording());

    return 1.0;
  }

 private:
  // Set when printing.
  Member<WebPluginContainerBase> plugin_;
  WebPrintParams print_params_;
};

static WebDataSource* DataSourceForDocLoader(DocumentLoader* loader) {
  return loader ? WebDataSourceImpl::FromDocumentLoader(loader) : 0;
}

// WebFrame -------------------------------------------------------------------

int WebFrame::InstanceCount() {
  return g_frame_count;
}

WebLocalFrame* WebLocalFrame::FrameForCurrentContext() {
  v8::Local<v8::Context> context =
      v8::Isolate::GetCurrent()->GetCurrentContext();
  if (context.IsEmpty())
    return 0;
  return FrameForContext(context);
}

WebLocalFrame* WebLocalFrame::FrameForContext(v8::Local<v8::Context> context) {
  return WebLocalFrameImpl::FromFrame(ToLocalFrameIfNotDetached(context));
}

WebLocalFrame* WebLocalFrame::FromFrameOwnerElement(const WebElement& element) {
  return WebLocalFrameImpl::FromFrameOwnerElement(element);
}

bool WebLocalFrameImpl::IsWebLocalFrame() const {
  return true;
}

WebLocalFrame* WebLocalFrameImpl::ToWebLocalFrame() {
  return this;
}

bool WebLocalFrameImpl::IsWebRemoteFrame() const {
  return false;
}

WebRemoteFrame* WebLocalFrameImpl::ToWebRemoteFrame() {
  NOTREACHED();
  return 0;
}

void WebLocalFrameImpl::Close() {
  WebLocalFrame::Close();

  client_ = nullptr;

  if (dev_tools_agent_)
    dev_tools_agent_.Clear();

  self_keep_alive_.Clear();

  if (print_context_)
    PrintEnd();
}

WebString WebLocalFrameImpl::AssignedName() const {
  return GetFrame()->Tree().GetName();
}

void WebLocalFrameImpl::SetName(const WebString& name) {
  GetFrame()->Tree().SetName(name);
}

WebVector<WebIconURL> WebLocalFrameImpl::IconURLs(int icon_types_mask) const {
  // The URL to the icon may be in the header. As such, only
  // ask the loader for the icon if it's finished loading.
  if (GetFrame()->GetDocument()->LoadEventFinished())
    return GetFrame()->GetDocument()->IconURLs(icon_types_mask);
  return WebVector<WebIconURL>();
}

void WebLocalFrameImpl::SetContentSettingsClient(
    WebContentSettingsClient* client) {
  content_settings_client_.SetClient(client);
}

void WebLocalFrameImpl::SetSharedWorkerRepositoryClient(
    WebSharedWorkerRepositoryClient* client) {
  shared_worker_repository_client_ =
      SharedWorkerRepositoryClientImpl::Create(client);
}

ScrollableArea* WebLocalFrameImpl::LayoutViewportScrollableArea() const {
  if (FrameView* view = GetFrameView())
    return view->LayoutViewportScrollableArea();
  return nullptr;
}

bool WebLocalFrameImpl::IsFocused() const {
  if (!ViewImpl() || !ViewImpl()->GetPage())
    return false;

  return this ==
         WebFrame::FromFrame(
             ViewImpl()->GetPage()->GetFocusController().FocusedFrame());
}

WebSize WebLocalFrameImpl::GetScrollOffset() const {
  if (ScrollableArea* scrollable_area = LayoutViewportScrollableArea())
    return scrollable_area->ScrollOffsetInt();
  return WebSize();
}

void WebLocalFrameImpl::SetScrollOffset(const WebSize& offset) {
  if (ScrollableArea* scrollable_area = LayoutViewportScrollableArea()) {
    scrollable_area->SetScrollOffset(ScrollOffset(offset.width, offset.height),
                                     kProgrammaticScroll);
  }
}

WebSize WebLocalFrameImpl::ContentsSize() const {
  if (FrameView* view = GetFrameView())
    return view->ContentsSize();
  return WebSize();
}

bool WebLocalFrameImpl::HasVisibleContent() const {
  LayoutPartItem layout_item = GetFrame()->OwnerLayoutItem();
  if (!layout_item.IsNull() &&
      layout_item.Style()->Visibility() != EVisibility::kVisible) {
    return false;
  }

  if (FrameView* view = GetFrameView())
    return view->VisibleWidth() > 0 && view->VisibleHeight() > 0;
  return false;
}

WebRect WebLocalFrameImpl::VisibleContentRect() const {
  if (FrameView* view = GetFrameView())
    return view->VisibleContentRect();
  return WebRect();
}

bool WebLocalFrameImpl::HasHorizontalScrollbar() const {
  return GetFrame() && GetFrame()->View() &&
         GetFrame()->View()->HorizontalScrollbar();
}

bool WebLocalFrameImpl::HasVerticalScrollbar() const {
  return GetFrame() && GetFrame()->View() &&
         GetFrame()->View()->VerticalScrollbar();
}

WebView* WebLocalFrameImpl::View() const {
  return ViewImpl();
}

WebDocument WebLocalFrameImpl::GetDocument() const {
  if (!GetFrame() || !GetFrame()->GetDocument())
    return WebDocument();
  return WebDocument(GetFrame()->GetDocument());
}

WebPerformance WebLocalFrameImpl::Performance() const {
  if (!GetFrame())
    return WebPerformance();
  return WebPerformance(
      DOMWindowPerformance::performance(*(GetFrame()->DomWindow())));
}

void WebLocalFrameImpl::DispatchUnloadEvent() {
  if (!GetFrame())
    return;
  SubframeLoadingDisabler disabler(GetFrame()->GetDocument());
  GetFrame()->Loader().DispatchUnloadEvent();
}

void WebLocalFrameImpl::ExecuteScript(const WebScriptSource& source) {
  DCHECK(GetFrame());
  TextPosition position(OrdinalNumber::FromOneBasedInt(source.start_line),
                        OrdinalNumber::First());
  v8::HandleScope handle_scope(ToIsolate(GetFrame()));
  GetFrame()->GetScriptController().ExecuteScriptInMainWorld(
      ScriptSourceCode(source.code, source.url, position));
}

void WebLocalFrameImpl::ExecuteScriptInIsolatedWorld(
    int world_id,
    const WebScriptSource* sources_in,
    unsigned num_sources) {
  DCHECK(GetFrame());
  CHECK_GT(world_id, 0);
  CHECK_LT(world_id, DOMWrapperWorld::kEmbedderWorldIdLimit);

  HeapVector<ScriptSourceCode> sources =
      CreateSourcesVector(sources_in, num_sources);
  v8::HandleScope handle_scope(ToIsolate(GetFrame()));
  GetFrame()->GetScriptController().ExecuteScriptInIsolatedWorld(world_id,
                                                                 sources, 0);
}

void WebLocalFrameImpl::SetIsolatedWorldSecurityOrigin(
    int world_id,
    const WebSecurityOrigin& security_origin) {
  DCHECK(GetFrame());
  DOMWrapperWorld::SetIsolatedWorldSecurityOrigin(world_id,
                                                  security_origin.Get());
}

void WebLocalFrameImpl::SetIsolatedWorldContentSecurityPolicy(
    int world_id,
    const WebString& policy) {
  DCHECK(GetFrame());
  DOMWrapperWorld::SetIsolatedWorldContentSecurityPolicy(world_id, policy);
}

void WebLocalFrameImpl::SetIsolatedWorldHumanReadableName(
    int world_id,
    const WebString& human_readable_name) {
  DCHECK(GetFrame());
  DOMWrapperWorld::SetIsolatedWorldHumanReadableName(world_id,
                                                     human_readable_name);
}

void WebLocalFrameImpl::AddMessageToConsole(const WebConsoleMessage& message) {
  DCHECK(GetFrame());

  MessageLevel web_core_message_level = kInfoMessageLevel;
  switch (message.level) {
    case WebConsoleMessage::kLevelVerbose:
      web_core_message_level = kVerboseMessageLevel;
      break;
    case WebConsoleMessage::kLevelInfo:
      web_core_message_level = kInfoMessageLevel;
      break;
    case WebConsoleMessage::kLevelWarning:
      web_core_message_level = kWarningMessageLevel;
      break;
    case WebConsoleMessage::kLevelError:
      web_core_message_level = kErrorMessageLevel;
      break;
  }

  GetFrame()->GetDocument()->AddConsoleMessage(ConsoleMessage::Create(
      kOtherMessageSource, web_core_message_level, message.text,
      SourceLocation::Create(message.url, message.line_number,
                             message.column_number, nullptr)));
}

void WebLocalFrameImpl::CollectGarbage() {
  if (!GetFrame())
    return;
  if (!GetFrame()->GetSettings()->GetScriptEnabled())
    return;
  V8GCController::CollectGarbage(v8::Isolate::GetCurrent());
}

v8::Local<v8::Value> WebLocalFrameImpl::ExecuteScriptAndReturnValue(
    const WebScriptSource& source) {
  DCHECK(GetFrame());

  TextPosition position(OrdinalNumber::FromOneBasedInt(source.start_line),
                        OrdinalNumber::First());
  return GetFrame()
      ->GetScriptController()
      .ExecuteScriptInMainWorldAndReturnValue(
          ScriptSourceCode(source.code, source.url, position));
}

void WebLocalFrameImpl::RequestExecuteScriptAndReturnValue(
    const WebScriptSource& source,
    bool user_gesture,
    WebScriptExecutionCallback* callback) {
  DCHECK(GetFrame());

  RefPtr<DOMWrapperWorld> main_world = &DOMWrapperWorld::MainWorld();
  SuspendableScriptExecutor* executor = SuspendableScriptExecutor::Create(
      GetFrame(), std::move(main_world), CreateSourcesVector(&source, 1),
      user_gesture, callback);
  executor->Run();
}

void WebLocalFrameImpl::RequestExecuteV8Function(
    v8::Local<v8::Context> context,
    v8::Local<v8::Function> function,
    v8::Local<v8::Value> receiver,
    int argc,
    v8::Local<v8::Value> argv[],
    WebScriptExecutionCallback* callback) {
  DCHECK(GetFrame());
  SuspendableScriptExecutor::CreateAndRun(GetFrame(), ToIsolate(GetFrame()),
                                          context, function, receiver, argc,
                                          argv, callback);
}

void WebLocalFrameImpl::ExecuteScriptInIsolatedWorld(
    int world_id,
    const WebScriptSource* sources_in,
    unsigned num_sources,
    WebVector<v8::Local<v8::Value>>* results) {
  DCHECK(GetFrame());
  CHECK_GT(world_id, 0);
  CHECK_LT(world_id, DOMWrapperWorld::kEmbedderWorldIdLimit);

  HeapVector<ScriptSourceCode> sources =
      CreateSourcesVector(sources_in, num_sources);

  if (results) {
    Vector<v8::Local<v8::Value>> script_results;
    GetFrame()->GetScriptController().ExecuteScriptInIsolatedWorld(
        world_id, sources, &script_results);
    WebVector<v8::Local<v8::Value>> v8_results(script_results.size());
    for (unsigned i = 0; i < script_results.size(); i++)
      v8_results[i] =
          v8::Local<v8::Value>::New(ToIsolate(GetFrame()), script_results[i]);
    results->Swap(v8_results);
  } else {
    v8::HandleScope handle_scope(ToIsolate(GetFrame()));
    GetFrame()->GetScriptController().ExecuteScriptInIsolatedWorld(world_id,
                                                                   sources, 0);
  }
}

void WebLocalFrameImpl::RequestExecuteScriptInIsolatedWorld(
    int world_id,
    const WebScriptSource* sources_in,
    unsigned num_sources,
    bool user_gesture,
    ScriptExecutionType option,
    WebScriptExecutionCallback* callback) {
  DCHECK(GetFrame());
  CHECK_GT(world_id, 0);
  CHECK_LT(world_id, DOMWrapperWorld::kEmbedderWorldIdLimit);

  RefPtr<DOMWrapperWorld> isolated_world =
      DOMWrapperWorld::EnsureIsolatedWorld(ToIsolate(GetFrame()), world_id);
  SuspendableScriptExecutor* executor = SuspendableScriptExecutor::Create(
      GetFrame(), std::move(isolated_world),
      CreateSourcesVector(sources_in, num_sources), user_gesture, callback);
  switch (option) {
    case kAsynchronousBlockingOnload:
      executor->RunAsync(SuspendableScriptExecutor::kOnloadBlocking);
      break;
    case kAsynchronous:
      executor->RunAsync(SuspendableScriptExecutor::kNonBlocking);
      break;
    case kSynchronous:
      executor->Run();
      break;
  }
}

// TODO(bashi): Consider returning MaybeLocal.
v8::Local<v8::Value> WebLocalFrameImpl::CallFunctionEvenIfScriptDisabled(
    v8::Local<v8::Function> function,
    v8::Local<v8::Value> receiver,
    int argc,
    v8::Local<v8::Value> argv[]) {
  DCHECK(GetFrame());
  v8::Local<v8::Value> result;
  if (!V8ScriptRunner::CallFunction(
           function, GetFrame()->GetDocument(), receiver, argc,
           static_cast<v8::Local<v8::Value>*>(argv), ToIsolate(GetFrame()))
           .ToLocal(&result))
    return v8::Local<v8::Value>();
  return result;
}

v8::Local<v8::Context> WebLocalFrameImpl::MainWorldScriptContext() const {
  ScriptState* script_state = ToScriptStateForMainWorld(GetFrame());
  DCHECK(script_state);
  return script_state->GetContext();
}

bool WebFrame::ScriptCanAccess(WebFrame* target) {
  return BindingSecurity::ShouldAllowAccessToFrame(
      CurrentDOMWindow(MainThreadIsolate()), ToCoreFrame(*target),
      BindingSecurity::ErrorReportOption::kDoNotReport);
}

void WebLocalFrameImpl::Reload(WebFrameLoadType load_type) {
  // TODO(clamy): Remove this function once RenderFrame calls load for all
  // requests.
  ReloadWithOverrideURL(KURL(), load_type);
}

void WebLocalFrameImpl::ReloadWithOverrideURL(const WebURL& override_url,
                                              WebFrameLoadType load_type) {
  // TODO(clamy): Remove this function once RenderFrame calls load for all
  // requests.
  DCHECK(GetFrame());
  DCHECK(IsReloadLoadType(static_cast<FrameLoadType>(load_type)));
  WebURLRequest request = RequestForReload(load_type, override_url);
  if (request.IsNull())
    return;
  Load(request, load_type, WebHistoryItem(), kWebHistoryDifferentDocumentLoad,
       false);
}

void WebLocalFrameImpl::ReloadImage(const WebNode& web_node) {
  const Node* node = web_node.ConstUnwrap<Node>();
  if (isHTMLImageElement(*node)) {
    const HTMLImageElement& image_element = toHTMLImageElement(*node);
    image_element.ForceReload();
  }
}

void WebLocalFrameImpl::ReloadLoFiImages() {
  GetFrame()->GetDocument()->Fetcher()->ReloadLoFiImages();
}

void WebLocalFrameImpl::LoadRequest(const WebURLRequest& request) {
  // TODO(clamy): Remove this function once RenderFrame calls load for all
  // requests.
  Load(request, WebFrameLoadType::kStandard, WebHistoryItem(),
       kWebHistoryDifferentDocumentLoad, false);
}

void WebLocalFrameImpl::LoadHTMLString(const WebData& data,
                                       const WebURL& base_url,
                                       const WebURL& unreachable_url,
                                       bool replace) {
  DCHECK(GetFrame());
  LoadData(data, WebString::FromUTF8("text/html"), WebString::FromUTF8("UTF-8"),
           base_url, unreachable_url, replace, WebFrameLoadType::kStandard,
           WebHistoryItem(), kWebHistoryDifferentDocumentLoad, false);
}

void WebLocalFrameImpl::StopLoading() {
  if (!GetFrame())
    return;
  // FIXME: Figure out what we should really do here. It seems like a bug
  // that FrameLoader::stopLoading doesn't call stopAllLoaders.
  GetFrame()->Loader().StopAllLoaders();
}

WebDataSource* WebLocalFrameImpl::ProvisionalDataSource() const {
  DCHECK(GetFrame());
  return DataSourceForDocLoader(
      GetFrame()->Loader().ProvisionalDocumentLoader());
}

WebDataSource* WebLocalFrameImpl::DataSource() const {
  DCHECK(GetFrame());
  return DataSourceForDocLoader(GetFrame()->Loader().GetDocumentLoader());
}

void WebLocalFrameImpl::EnableViewSourceMode(bool enable) {
  if (GetFrame())
    GetFrame()->SetInViewSourceMode(enable);
}

bool WebLocalFrameImpl::IsViewSourceModeEnabled() const {
  if (!GetFrame())
    return false;
  return GetFrame()->InViewSourceMode();
}

void WebLocalFrameImpl::SetReferrerForRequest(WebURLRequest& request,
                                              const WebURL& referrer_url) {
  String referrer = referrer_url.IsEmpty()
                        ? GetFrame()->GetDocument()->OutgoingReferrer()
                        : String(referrer_url.GetString());
  request.ToMutableResourceRequest().SetHTTPReferrer(
      SecurityPolicy::GenerateReferrer(
          GetFrame()->GetDocument()->GetReferrerPolicy(), request.Url(),
          referrer));
}

WebAssociatedURLLoader* WebLocalFrameImpl::CreateAssociatedURLLoader(
    const WebAssociatedURLLoaderOptions& options) {
  return new WebAssociatedURLLoaderImpl(GetFrame()->GetDocument(), options);
}

unsigned WebLocalFrameImpl::UnloadListenerCount() const {
  return GetFrame()->DomWindow()->PendingUnloadEventListeners();
}

void WebLocalFrameImpl::ReplaceSelection(const WebString& text) {
  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  GetFrame()->GetEditor().ReplaceSelection(text);
}

void WebLocalFrameImpl::SetMarkedText(const WebString& text,
                                      unsigned location,
                                      unsigned length) {
  Vector<CompositionUnderline> decorations;
  GetFrame()->GetInputMethodController().SetComposition(text, decorations,
                                                        location, length);
}

void WebLocalFrameImpl::UnmarkText() {
  GetFrame()->GetInputMethodController().CancelComposition();
}

bool WebLocalFrameImpl::HasMarkedText() const {
  return GetFrame()->GetInputMethodController().HasComposition();
}

WebRange WebLocalFrameImpl::MarkedRange() const {
  return GetFrame()->GetInputMethodController().CompositionEphemeralRange();
}

bool WebLocalFrameImpl::FirstRectForCharacterRange(
    unsigned location,
    unsigned length,
    WebRect& rect_in_viewport) const {
  if ((location + length < location) && (location + length))
    length = 0;

  Element* editable =
      GetFrame()->Selection().RootEditableElementOrDocumentElement();
  if (!editable)
    return false;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  see http://crbug.com/590369 for more details.
  editable->GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  const EphemeralRange range =
      PlainTextRange(location, location + length).CreateRange(*editable);
  if (range.IsNull())
    return false;
  IntRect int_rect = GetFrame()->GetEditor().FirstRectForRange(range);
  rect_in_viewport = WebRect(int_rect);
  rect_in_viewport = GetFrame()->View()->ContentsToViewport(rect_in_viewport);
  return true;
}

size_t WebLocalFrameImpl::CharacterIndexForPoint(
    const WebPoint& point_in_viewport) const {
  if (!GetFrame())
    return kNotFound;

  IntPoint point = GetFrame()->View()->ViewportToContents(point_in_viewport);
  HitTestResult result = GetFrame()->GetEventHandler().HitTestResultAtPoint(
      point, HitTestRequest::kReadOnly | HitTestRequest::kActive);
  const EphemeralRange range =
      GetFrame()->RangeForPoint(result.RoundedPointInInnerNodeFrame());
  if (range.IsNull())
    return kNotFound;
  Element* editable =
      GetFrame()->Selection().RootEditableElementOrDocumentElement();
  DCHECK(editable);
  return PlainTextRange::Create(*editable, range).Start();
}

bool WebLocalFrameImpl::ExecuteCommand(const WebString& name) {
  DCHECK(GetFrame());

  if (name.length() <= 2)
    return false;

  // Since we don't have NSControl, we will convert the format of command
  // string and call the function on Editor directly.
  String command = name;

  // Make sure the first letter is upper case.
  command.replace(0, 1, command.Substring(0, 1).UpperASCII());

  // Remove the trailing ':' if existing.
  if (command[command.length() - 1] == UChar(':'))
    command = command.Substring(0, command.length() - 1);

  Node* plugin_lookup_context_node =
      context_menu_node_ && name == "Copy" ? context_menu_node_ : nullptr;
  WebPluginContainerBase* plugin_container =
      CurrentPluginContainer(GetFrame(), plugin_lookup_context_node);
  if (plugin_container && plugin_container->ExecuteEditCommand(name))
    return true;

  return GetFrame()->GetEditor().ExecuteCommand(command);
}

bool WebLocalFrameImpl::ExecuteCommand(const WebString& name,
                                       const WebString& value) {
  DCHECK(GetFrame());

  WebPluginContainerBase* plugin_container = CurrentPluginContainer(GetFrame());
  if (plugin_container && plugin_container->ExecuteEditCommand(name, value))
    return true;

  return GetFrame()->GetEditor().ExecuteCommand(name, value);
}

bool WebLocalFrameImpl::IsCommandEnabled(const WebString& name) const {
  DCHECK(GetFrame());
  return GetFrame()->GetEditor().CreateCommand(name).IsEnabled();
}

void WebLocalFrameImpl::EnableSpellChecking(bool enable) {
  if (enable == IsSpellCheckingEnabled())
    return;
  GetFrame()->GetSpellChecker().ToggleSpellCheckingEnabled();
}

bool WebLocalFrameImpl::IsSpellCheckingEnabled() const {
  return GetFrame()->GetSpellChecker().IsSpellCheckingEnabled();
}

void WebLocalFrameImpl::ReplaceMisspelledRange(const WebString& text) {
  // If this caret selection has two or more markers, this function replace the
  // range covered by the first marker with the specified word as Microsoft Word
  // does.
  if (PluginContainerFromFrame(GetFrame()))
    return;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  see http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  GetFrame()->GetSpellChecker().ReplaceMisspelledRange(text);
}

void WebLocalFrameImpl::RemoveSpellingMarkers() {
  GetFrame()->GetSpellChecker().RemoveSpellingMarkers();
}

void WebLocalFrameImpl::RemoveSpellingMarkersUnderWords(
    const WebVector<WebString>& words) {
  Vector<String> converted_words;
  converted_words.Append(words.Data(), words.size());
  GetFrame()->RemoveSpellingMarkersUnderWords(converted_words);
}

bool WebLocalFrameImpl::HasSelection() const {
  WebPluginContainerBase* plugin_container =
      PluginContainerFromFrame(GetFrame());
  if (plugin_container)
    return plugin_container->Plugin()->HasSelection();

  // frame()->selection()->isNone() never returns true.
  return GetFrame()
             ->Selection()
             .ComputeVisibleSelectionInDOMTreeDeprecated()
             .Start() != GetFrame()
                             ->Selection()
                             .ComputeVisibleSelectionInDOMTreeDeprecated()
                             .end();
}

WebRange WebLocalFrameImpl::SelectionRange() const {
  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  return GetFrame()
      ->Selection()
      .ComputeVisibleSelectionInDOMTreeDeprecated()
      .ToNormalizedEphemeralRange();
}

WebString WebLocalFrameImpl::SelectionAsText() const {
  WebPluginContainerBase* plugin_container =
      PluginContainerFromFrame(GetFrame());
  if (plugin_container)
    return plugin_container->Plugin()->SelectionAsText();

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  String text = GetFrame()->Selection().SelectedText(
      TextIteratorBehavior::EmitsObjectReplacementCharacterBehavior());
#if OS(WIN)
  ReplaceNewlinesWithWindowsStyleNewlines(text);
#endif
  ReplaceNBSPWithSpace(text);
  return text;
}

WebString WebLocalFrameImpl::SelectionAsMarkup() const {
  WebPluginContainerBase* plugin_container =
      PluginContainerFromFrame(GetFrame());
  if (plugin_container)
    return plugin_container->Plugin()->SelectionAsMarkup();

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  // Selection normalization and markup generation require clean layout.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  return GetFrame()->Selection().SelectedHTMLForClipboard();
}

void WebLocalFrameImpl::SelectWordAroundPosition(LocalFrame* frame,
                                                 VisiblePosition position) {
  TRACE_EVENT0("blink", "WebLocalFrameImpl::selectWordAroundPosition");

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  see http://crbug.com/590369 for more details.
  frame->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  frame->Selection().SelectWordAroundPosition(position);
}

bool WebLocalFrameImpl::SelectWordAroundCaret() {
  TRACE_EVENT0("blink", "WebLocalFrameImpl::selectWordAroundCaret");
  FrameSelection& selection = GetFrame()->Selection();
  if (selection.ComputeVisibleSelectionInDOMTreeDeprecated().IsNone() ||
      selection.ComputeVisibleSelectionInDOMTreeDeprecated().IsRange())
    return false;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  see http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  return GetFrame()->Selection().SelectWordAroundPosition(
      selection.ComputeVisibleSelectionInDOMTreeDeprecated().VisibleStart());
}

void WebLocalFrameImpl::SelectRange(const WebPoint& base_in_viewport,
                                    const WebPoint& extent_in_viewport) {
  MoveRangeSelection(base_in_viewport, extent_in_viewport);
}

void WebLocalFrameImpl::SelectRange(
    const WebRange& web_range,
    HandleVisibilityBehavior handle_visibility_behavior) {
  TRACE_EVENT0("blink", "WebLocalFrameImpl::selectRange");

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  see http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  const EphemeralRange& range = web_range.CreateEphemeralRange(GetFrame());
  if (range.IsNull())
    return;

  FrameSelection& selection = GetFrame()->Selection();
  const bool show_handles =
      handle_visibility_behavior == kShowSelectionHandle ||
      (handle_visibility_behavior == kPreserveHandleVisibility &&
       selection.IsHandleVisible());
  selection.SetSelection(SelectionInDOMTree::Builder()
                             .SetBaseAndExtent(range)
                             .SetAffinity(VP_DEFAULT_AFFINITY)
                             .SetIsHandleVisible(show_handles)
                             .SetIsDirectional(false)
                             .Build(),
                         kNotUserTriggered);
}

WebString WebLocalFrameImpl::RangeAsText(const WebRange& web_range) {
  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  see http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      GetFrame()->GetDocument()->Lifecycle());

  return PlainText(
      web_range.CreateEphemeralRange(GetFrame()),
      TextIteratorBehavior::EmitsObjectReplacementCharacterBehavior());
}

void WebLocalFrameImpl::MoveRangeSelectionExtent(const WebPoint& point) {
  TRACE_EVENT0("blink", "WebLocalFrameImpl::moveRangeSelectionExtent");

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  GetFrame()->Selection().MoveRangeSelectionExtent(
      GetFrame()->View()->ViewportToContents(point));
}

void WebLocalFrameImpl::MoveRangeSelection(
    const WebPoint& base_in_viewport,
    const WebPoint& extent_in_viewport,
    WebFrame::TextGranularity granularity) {
  TRACE_EVENT0("blink", "WebLocalFrameImpl::moveRangeSelection");

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  blink::TextGranularity blink_granularity = blink::kCharacterGranularity;
  if (granularity == WebFrame::kWordGranularity)
    blink_granularity = blink::kWordGranularity;
  GetFrame()->Selection().MoveRangeSelection(
      VisiblePositionForViewportPoint(base_in_viewport),
      VisiblePositionForViewportPoint(extent_in_viewport), blink_granularity);
}

void WebLocalFrameImpl::MoveCaretSelection(const WebPoint& point_in_viewport) {
  TRACE_EVENT0("blink", "WebLocalFrameImpl::moveCaretSelection");

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  see http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  const IntPoint point_in_contents =
      GetFrame()->View()->ViewportToContents(point_in_viewport);
  GetFrame()->Selection().MoveCaretSelection(point_in_contents);
}

bool WebLocalFrameImpl::SetEditableSelectionOffsets(int start, int end) {
  TRACE_EVENT0("blink", "WebLocalFrameImpl::setEditableSelectionOffsets");

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  return GetFrame()->GetInputMethodController().SetEditableSelectionOffsets(
      PlainTextRange(start, end));
}

bool WebLocalFrameImpl::SetCompositionFromExistingText(
    int composition_start,
    int composition_end,
    const WebVector<WebCompositionUnderline>& underlines) {
  TRACE_EVENT0("blink", "WebLocalFrameImpl::setCompositionFromExistingText");
  if (!GetFrame()->GetEditor().CanEdit())
    return false;

  InputMethodController& input_method_controller =
      GetFrame()->GetInputMethodController();

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  input_method_controller.SetCompositionFromExistingText(
      CompositionUnderlineVectorBuilder::Build(underlines), composition_start,
      composition_end);

  return true;
}

void WebLocalFrameImpl::ExtendSelectionAndDelete(int before, int after) {
  TRACE_EVENT0("blink", "WebLocalFrameImpl::extendSelectionAndDelete");
  if (WebPlugin* plugin = FocusedPluginIfInputMethodSupported()) {
    plugin->ExtendSelectionAndDelete(before, after);
    return;
  }

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  GetFrame()->GetInputMethodController().ExtendSelectionAndDelete(before,
                                                                  after);
}

void WebLocalFrameImpl::DeleteSurroundingText(int before, int after) {
  TRACE_EVENT0("blink", "WebLocalFrameImpl::deleteSurroundingText");
  if (WebPlugin* plugin = FocusedPluginIfInputMethodSupported()) {
    plugin->DeleteSurroundingText(before, after);
    return;
  }

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  GetFrame()->GetInputMethodController().DeleteSurroundingText(before, after);
}

void WebLocalFrameImpl::DeleteSurroundingTextInCodePoints(int before,
                                                          int after) {
  TRACE_EVENT0("blink", "WebLocalFrameImpl::deleteSurroundingTextInCodePoints");
  if (WebPlugin* plugin = FocusedPluginIfInputMethodSupported()) {
    plugin->DeleteSurroundingTextInCodePoints(before, after);
    return;
  }

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  GetFrame()->GetInputMethodController().DeleteSurroundingTextInCodePoints(
      before, after);
}

void WebLocalFrameImpl::SetCaretVisible(bool visible) {
  GetFrame()->Selection().SetCaretVisible(visible);
}

VisiblePosition WebLocalFrameImpl::VisiblePositionForViewportPoint(
    const WebPoint& point_in_viewport) {
  return VisiblePositionForContentsPoint(
      GetFrame()->View()->ViewportToContents(point_in_viewport), GetFrame());
}

WebPlugin* WebLocalFrameImpl::FocusedPluginIfInputMethodSupported() {
  WebPluginContainerBase* container =
      WebLocalFrameImpl::CurrentPluginContainer(GetFrame());
  if (container && container->SupportsInputMethod())
    return container->Plugin();
  return 0;
}

int WebLocalFrameImpl::PrintBegin(const WebPrintParams& print_params,
                                  const WebNode& constrain_to_node) {
  DCHECK(!GetFrame()->GetDocument()->IsFrameSet());
  WebPluginContainerBase* plugin_container = nullptr;
  if (constrain_to_node.IsNull()) {
    // If this is a plugin document, check if the plugin supports its own
    // printing. If it does, we will delegate all printing to that.
    plugin_container = PluginContainerFromFrame(GetFrame());
  } else {
    // We only support printing plugin nodes for now.
    plugin_container =
        ToWebPluginContainerBase(constrain_to_node.PluginContainer());
  }

  if (plugin_container && plugin_container->SupportsPaginatedPrint()) {
    print_context_ = new ChromePluginPrintContext(GetFrame(), plugin_container,
                                                  print_params);
  } else {
    print_context_ = new ChromePrintContext(GetFrame());
  }

  FloatSize size(static_cast<float>(print_params.print_content_area.width),
                 static_cast<float>(print_params.print_content_area.height));
  print_context_->BeginPrintMode(size.Width(), size.Height());
  print_context_->ComputePageRects(size);

  return static_cast<int>(print_context_->PageCount());
}

float WebLocalFrameImpl::GetPrintPageShrink(int page) {
  DCHECK(print_context_);
  DCHECK_GE(page, 0);
  return print_context_->GetPageShrink(page);
}

float WebLocalFrameImpl::PrintPage(int page, WebCanvas* canvas) {
  DCHECK(print_context_);
  DCHECK_GE(page, 0);
  DCHECK(GetFrame());
  DCHECK(GetFrame()->GetDocument());

  return print_context_->SpoolSinglePage(canvas, page);
}

void WebLocalFrameImpl::PrintEnd() {
  DCHECK(print_context_);
  print_context_->EndPrintMode();
  print_context_.Clear();
}

bool WebLocalFrameImpl::IsPrintScalingDisabledForPlugin(const WebNode& node) {
  WebPluginContainerBase* plugin_container =
      node.IsNull() ? PluginContainerFromFrame(GetFrame())
                    : ToWebPluginContainerBase(node.PluginContainer());

  if (!plugin_container || !plugin_container->SupportsPaginatedPrint())
    return false;

  return plugin_container->IsPrintScalingDisabled();
}

bool WebLocalFrameImpl::GetPrintPresetOptionsForPlugin(
    const WebNode& node,
    WebPrintPresetOptions* preset_options) {
  WebPluginContainerBase* plugin_container =
      node.IsNull() ? PluginContainerFromFrame(GetFrame())
                    : ToWebPluginContainerBase(node.PluginContainer());

  if (!plugin_container || !plugin_container->SupportsPaginatedPrint())
    return false;

  return plugin_container->GetPrintPresetOptionsFromDocument(preset_options);
}

bool WebLocalFrameImpl::HasCustomPageSizeStyle(int page_index) {
  return GetFrame()
             ->GetDocument()
             ->StyleForPage(page_index)
             ->GetPageSizeType() != PAGE_SIZE_AUTO;
}

bool WebLocalFrameImpl::IsPageBoxVisible(int page_index) {
  return GetFrame()->GetDocument()->IsPageBoxVisible(page_index);
}

void WebLocalFrameImpl::PageSizeAndMarginsInPixels(int page_index,
                                                   WebDoubleSize& page_size,
                                                   int& margin_top,
                                                   int& margin_right,
                                                   int& margin_bottom,
                                                   int& margin_left) {
  DoubleSize size = page_size;
  GetFrame()->GetDocument()->PageSizeAndMarginsInPixels(
      page_index, size, margin_top, margin_right, margin_bottom, margin_left);
  page_size = size;
}

WebString WebLocalFrameImpl::PageProperty(const WebString& property_name,
                                          int page_index) {
  DCHECK(print_context_);
  return print_context_->PageProperty(GetFrame(), property_name.Utf8().data(),
                                      page_index);
}

void WebLocalFrameImpl::PrintPagesWithBoundaries(
    WebCanvas* canvas,
    const WebSize& page_size_in_pixels) {
  DCHECK(print_context_);

  print_context_->SpoolAllPagesWithBoundaries(
      canvas, FloatSize(page_size_in_pixels.width, page_size_in_pixels.height));
}

WebRect WebLocalFrameImpl::SelectionBoundsRect() const {
  return HasSelection() ? WebRect(IntRect(GetFrame()->Selection().Bounds()))
                        : WebRect();
}

WebString WebLocalFrameImpl::LayerTreeAsText(bool show_debug_info) const {
  if (!GetFrame())
    return WebString();

  return WebString(GetFrame()->LayerTreeAsText(
      show_debug_info ? kLayerTreeIncludesDebugInfo : kLayerTreeNormal));
}

// WebLocalFrameImpl public --------------------------------------------------

WebLocalFrame* WebLocalFrame::Create(
    WebTreeScopeType scope,
    WebFrameClient* client,
    blink::InterfaceProvider* interface_provider,
    blink::InterfaceRegistry* interface_registry,
    WebFrame* opener) {
  return WebLocalFrameImpl::Create(scope, client, interface_provider,
                                   interface_registry, opener);
}

WebLocalFrame* WebLocalFrame::CreateProvisional(
    WebFrameClient* client,
    blink::InterfaceProvider* interface_provider,
    blink::InterfaceRegistry* interface_registry,
    WebRemoteFrame* old_web_frame,
    WebSandboxFlags flags) {
  return WebLocalFrameImpl::CreateProvisional(
      client, interface_provider, interface_registry, old_web_frame, flags);
}

WebLocalFrameImpl* WebLocalFrameImpl::Create(
    WebTreeScopeType scope,
    WebFrameClient* client,
    blink::InterfaceProvider* interface_provider,
    blink::InterfaceRegistry* interface_registry,
    WebFrame* opener) {
  WebLocalFrameImpl* frame = new WebLocalFrameImpl(
      scope, client, interface_provider, interface_registry);
  frame->SetOpener(opener);
  return frame;
}

WebLocalFrameImpl* WebLocalFrameImpl::CreateProvisional(
    WebFrameClient* client,
    blink::InterfaceProvider* interface_provider,
    blink::InterfaceRegistry* interface_registry,
    WebRemoteFrame* old_web_frame,
    WebSandboxFlags flags) {
  DCHECK(client);
  WebLocalFrameImpl* web_frame = new WebLocalFrameImpl(
      old_web_frame, client, interface_provider, interface_registry);
  Frame* old_frame = ToWebRemoteFrameImpl(old_web_frame)->GetFrame();
  web_frame->SetParent(old_web_frame->Parent());
  web_frame->SetOpener(old_web_frame->Opener());
  // Note: this *always* temporarily sets a frame owner, even for main frames!
  // When a core Frame is created with no owner, it attempts to set itself as
  // the main frame of the Page. However, this is a provisional frame, and may
  // disappear, so Page::m_mainFrame can't be updated just yet.
  // Note 2: Becuase the dummy owner is still the owner when the initial empty
  // document is created, the initial empty document will not inherit the
  // correct sandbox flags. However, since the provisional frame is inivisible
  // to the rest of the page, the initial document is also invisible and
  // unscriptable. Once the provisional frame gets properly attached and is
  // observable, it will have the real FrameOwner, and any subsequent real
  // documents will correctly inherit sandbox flags from the owner.
  web_frame->InitializeCoreFrame(*old_frame->GetPage(),
                                 DummyFrameOwner::Create(),
                                 old_frame->Tree().GetName());

  LocalFrame* new_frame = web_frame->GetFrame();
  new_frame->SetOwner(old_frame->Owner());
  if (new_frame->Owner() && new_frame->Owner()->IsRemote()) {
    ToRemoteFrameOwner(new_frame->Owner())
        ->SetSandboxFlags(static_cast<SandboxFlags>(flags));
  }
  return web_frame;
}

WebLocalFrameImpl::WebLocalFrameImpl(
    WebTreeScopeType scope,
    WebFrameClient* client,
    blink::InterfaceProvider* interface_provider,
    blink::InterfaceRegistry* interface_registry)
    : WebLocalFrameBase(scope),
      local_frame_client_impl_(LocalFrameClientImpl::Create(this)),
      frame_widget_(0),
      client_(client),
      autofill_client_(0),
      input_events_scale_factor_for_emulation_(1),
      interface_provider_(interface_provider),
      interface_registry_(interface_registry),
      web_dev_tools_frontend_(0),
      input_method_controller_(new WebInputMethodControllerImpl(this)),
      text_checker_client_(new TextCheckerClientImpl(this)),
      self_keep_alive_(this) {
  DCHECK(client_);
  g_frame_count++;
}

WebLocalFrameImpl::WebLocalFrameImpl(
    WebRemoteFrame* old_web_frame,
    WebFrameClient* client,
    blink::InterfaceProvider* interface_provider,
    blink::InterfaceRegistry* interface_registry)
    : WebLocalFrameImpl(old_web_frame->InShadowTree()
                            ? WebTreeScopeType::kShadow
                            : WebTreeScopeType::kDocument,
                        client,
                        interface_provider,
                        interface_registry) {}

WebLocalFrameImpl::~WebLocalFrameImpl() {
  // The widget for the frame, if any, must have already been closed.
  DCHECK(!frame_widget_);
  g_frame_count--;
}

DEFINE_TRACE(WebLocalFrameImpl) {
  visitor->Trace(local_frame_client_impl_);
  visitor->Trace(frame_);
  visitor->Trace(dev_tools_agent_);
  visitor->Trace(text_finder_);
  visitor->Trace(print_context_);
  visitor->Trace(context_menu_node_);
  visitor->Trace(text_checker_client_);
  WebLocalFrameBase::Trace(visitor);
  // TODO(slangley): Call this from WebLocalFrameBase, once WebFrame is in core.
  WebFrame::TraceFrames(visitor, this);
}

void WebLocalFrameImpl::SetCoreFrame(LocalFrame* frame) {
  frame_ = frame;
}

void WebLocalFrameImpl::InitializeCoreFrame(Page& page,
                                            FrameOwner* owner,
                                            const AtomicString& name) {
  SetCoreFrame(LocalFrame::Create(local_frame_client_impl_.Get(), page, owner,
                                  interface_provider_, interface_registry_));
  frame_->Tree().SetName(name);
  // We must call init() after frame_ is assigned because it is referenced
  // during init().
  frame_->Init();
  CHECK(frame_);
  CHECK(frame_->Loader().StateMachine()->IsDisplayingInitialEmptyDocument());
  if (!Parent() && !Opener() &&
      frame_->GetSettings()->GetShouldReuseGlobalForUnownedMainFrame()) {
    frame_->GetDocument()->GetSecurityOrigin()->GrantUniversalAccess();
  }

  if (!owner) {
    // This trace event is needed to detect the main frame of the
    // renderer in telemetry metrics. See crbug.com/692112#c11.
    TRACE_EVENT_INSTANT1("loading", "markAsMainFrame", TRACE_EVENT_SCOPE_THREAD,
                         "frame", frame_);
  }
}

LocalFrame* WebLocalFrameImpl::CreateChildFrame(
    const FrameLoadRequest& request,
    const AtomicString& name,
    HTMLFrameOwnerElement* owner_element) {
  DCHECK(client_);
  TRACE_EVENT0("blink", "WebLocalFrameImpl::createChildframe");
  WebTreeScopeType scope =
      GetFrame()->GetDocument() == owner_element->GetTreeScope()
          ? WebTreeScopeType::kDocument
          : WebTreeScopeType::kShadow;
  WebFrameOwnerProperties owner_properties(
      owner_element->BrowsingContextContainerName(),
      owner_element->ScrollingMode(), owner_element->MarginWidth(),
      owner_element->MarginHeight(), owner_element->AllowFullscreen(),
      owner_element->AllowPaymentRequest(), owner_element->IsDisplayNone(),
      owner_element->Csp(), owner_element->AllowedFeatures());
  // FIXME: Using subResourceAttributeName as fallback is not a perfect
  // solution. subResourceAttributeName returns just one attribute name. The
  // element might not have the attribute, and there might be other attributes
  // which can identify the element.
  WebLocalFrameImpl* webframe_child =
      ToWebLocalFrameImpl(client_->CreateChildFrame(
          this, scope, name,
          owner_element->getAttribute(
              owner_element->SubResourceAttributeName()),
          static_cast<WebSandboxFlags>(owner_element->GetSandboxFlags()),
          owner_element->ContainerPolicy(), owner_properties));
  if (!webframe_child)
    return nullptr;

  webframe_child->InitializeCoreFrame(*GetFrame()->GetPage(), owner_element,
                                      name);
  DCHECK(webframe_child->Parent());

  FrameLoadRequest new_request = request;
  FrameLoadType child_load_type = kFrameLoadTypeStandard;
  HistoryItem* child_item = nullptr;

  if (!GetFrame()->GetDocument()->LoadEventFinished()) {
    FrameLoadType load_type =
        GetFrame()->Loader().GetDocumentLoader()->LoadType();
    switch (load_type) {
      case kFrameLoadTypeStandard:
      case kFrameLoadTypeReplaceCurrentItem:
      case kFrameLoadTypeInitialInChildFrame:
        break;

      // If we're moving in the back/forward list, we might want to replace the
      // content of this child frame with whatever was there at that point.
      case kFrameLoadTypeBackForward:
      case kFrameLoadTypeInitialHistoryLoad:
        child_item = webframe_child->Client()->HistoryItemForNewChildFrame();
        if (child_item) {
          child_load_type = kFrameLoadTypeInitialHistoryLoad;
          new_request =
              FrameLoadRequest(request.OriginDocument(),
                               child_item->GenerateResourceRequest(
                                   WebCachePolicy::kUseProtocolCachePolicy));
        }
        break;

      // We're in a middle of a reload. The FrameLoadType is propagated to its
      // children only if it is a ReloadBypassingCache, else it becomes a
      // standard load.
      case kFrameLoadTypeReload:
        break;
      case kFrameLoadTypeReloadBypassingCache:
        child_load_type = kFrameLoadTypeReloadBypassingCache;
        new_request.GetResourceRequest().SetCachePolicy(
            WebCachePolicy::kBypassingCache);
        break;
    }
  }

  webframe_child->GetFrame()->Loader().Load(new_request, child_load_type,
                                            child_item);

  // Note a synchronous navigation (about:blank) would have already processed
  // onload, so it is possible for the child frame to have already been
  // detached by script in the page.
  if (!webframe_child->Parent())
    return nullptr;
  return webframe_child->GetFrame();
}

void WebLocalFrameImpl::DidChangeContentsSize(const IntSize& size) {
  if (text_finder_ && text_finder_->TotalMatchCount() > 0)
    text_finder_->IncreaseMarkerVersion();
}

void WebLocalFrameImpl::CreateFrameView() {
  TRACE_EVENT0("blink", "WebLocalFrameImpl::createFrameView");

  DCHECK(GetFrame());  // If frame() doesn't exist, we probably didn't init
                       // properly.

  WebViewBase* web_view = ViewImpl();

  // Check if we're shutting down.
  if (!web_view->GetPage())
    return;

  bool is_main_frame = !Parent();
  IntSize initial_size = (is_main_frame || !FrameWidget())
                             ? web_view->MainFrameSize()
                             : (IntSize)FrameWidget()->Size();
  Color base_background_color = web_view->BaseBackgroundColor();
  if (!is_main_frame && Parent()->IsWebRemoteFrame())
    base_background_color = Color::kTransparent;

  GetFrame()->CreateView(initial_size, base_background_color);
  if (is_main_frame) {
    GetFrame()->View()->SetInitialViewportSize(
        web_view->GetPageScaleConstraintsSet().InitialViewportSize());
  }
  if (web_view->ShouldAutoResize() && GetFrame()->IsLocalRoot())
    GetFrame()->View()->EnableAutoSizeMode(web_view->MinAutoSize(),
                                           web_view->MaxAutoSize());

  GetFrame()->View()->SetInputEventsTransformForEmulation(
      input_events_offset_for_emulation_,
      input_events_scale_factor_for_emulation_);
  GetFrame()->View()->SetDisplayMode(web_view->DisplayMode());
}

WebLocalFrameImpl* WebLocalFrameImpl::FromFrame(LocalFrame* frame) {
  if (!frame)
    return nullptr;
  return FromFrame(*frame);
}

WebLocalFrameImpl* WebLocalFrameImpl::FromFrame(LocalFrame& frame) {
  LocalFrameClient* client = frame.Loader().Client();
  if (!client || !client->IsLocalFrameClientImpl())
    return nullptr;
  return ToWebLocalFrameImpl(ToLocalFrameClientImpl(client)->GetWebFrame());
}

WebLocalFrameImpl* WebLocalFrameImpl::FromFrameOwnerElement(Element* element) {
  if (!element->IsFrameOwnerElement())
    return nullptr;
  return FromFrame(
      ToLocalFrame(ToHTMLFrameOwnerElement(element)->ContentFrame()));
}

WebViewBase* WebLocalFrameImpl::ViewImpl() const {
  if (!GetFrame())
    return nullptr;
  return WebViewBase::FromPage(GetFrame()->GetPage());
}

WebDataSourceImpl* WebLocalFrameImpl::DataSourceImpl() const {
  return static_cast<WebDataSourceImpl*>(DataSource());
}

WebDataSourceImpl* WebLocalFrameImpl::ProvisionalDataSourceImpl() const {
  return static_cast<WebDataSourceImpl*>(ProvisionalDataSource());
}

void WebLocalFrameImpl::SetFindEndstateFocusAndSelection() {
  if (!text_finder_ || !text_finder_->ActiveMatchFrame())
    return;

  if (Range* active_match = text_finder_->ActiveMatch()) {
    // If the user has set the selection since the match was found, we
    // don't focus anything.
    VisibleSelection selection(
        GetFrame()->Selection().ComputeVisibleSelectionInDOMTreeDeprecated());
    if (!selection.IsNone())
      return;

    // Need to clean out style and layout state before querying
    // Element::isFocusable().
    GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

    // Try to find the first focusable node up the chain, which will, for
    // example, focus links if we have found text within the link.
    Node* node = active_match->FirstNode();
    if (node && node->IsInShadowTree()) {
      if (Node* host = node->OwnerShadowHost()) {
        if (isHTMLInputElement(*host) || isHTMLTextAreaElement(*host))
          node = host;
      }
    }
    const EphemeralRange active_match_range(active_match);
    if (node) {
      for (Node& runner : NodeTraversal::InclusiveAncestorsOf(*node)) {
        if (!runner.IsElementNode())
          continue;
        Element& element = ToElement(runner);
        if (element.IsFocusable()) {
          // Found a focusable parent node. Set the active match as the
          // selection and focus to the focusable node.
          GetFrame()->Selection().SetSelection(
              SelectionInDOMTree::Builder()
                  .SetBaseAndExtent(active_match_range)
                  .Build());
          GetFrame()->GetDocument()->SetFocusedElement(
              &element, FocusParams(SelectionBehaviorOnFocus::kNone,
                                    kWebFocusTypeNone, nullptr));
          return;
        }
      }
    }

    // Iterate over all the nodes in the range until we find a focusable node.
    // This, for example, sets focus to the first link if you search for
    // text and text that is within one or more links.
    for (Node& runner : active_match_range.Nodes()) {
      if (!runner.IsElementNode())
        continue;
      Element& element = ToElement(runner);
      if (element.IsFocusable()) {
        GetFrame()->GetDocument()->SetFocusedElement(
            &element, FocusParams(SelectionBehaviorOnFocus::kNone,
                                  kWebFocusTypeNone, nullptr));
        return;
      }
    }

    // No node related to the active match was focusable, so set the
    // active match as the selection (so that when you end the Find session,
    // you'll have the last thing you found highlighted) and make sure that
    // we have nothing focused (otherwise you might have text selected but
    // a link focused, which is weird).
    GetFrame()->Selection().SetSelection(
        SelectionInDOMTree::Builder()
            .SetBaseAndExtent(active_match_range)
            .Build());
    GetFrame()->GetDocument()->ClearFocusedElement();

    // Finally clear the active match, for two reasons:
    // We just finished the find 'session' and we don't want future (potentially
    // unrelated) find 'sessions' operations to start at the same place.
    // The WebLocalFrameImpl could get reused and the activeMatch could end up
    // pointing to a document that is no longer valid. Keeping an invalid
    // reference around is just asking for trouble.
    text_finder_->ResetActiveMatch();
  }
}

void WebLocalFrameImpl::DidFail(const ResourceError& error,
                                bool was_provisional,
                                HistoryCommitType commit_type) {
  if (!Client())
    return;
  WebURLError web_error = error;
  WebHistoryCommitType web_commit_type =
      static_cast<WebHistoryCommitType>(commit_type);

  if (WebPluginContainerBase* plugin = PluginContainerFromFrame(GetFrame()))
    plugin->DidFailLoading(error);

  if (was_provisional)
    Client()->DidFailProvisionalLoad(web_error, web_commit_type);
  else
    Client()->DidFailLoad(web_error, web_commit_type);
}

void WebLocalFrameImpl::DidFinish() {
  if (!Client())
    return;

  if (WebPluginContainerBase* plugin = PluginContainerFromFrame(GetFrame()))
    plugin->DidFinishLoading();

  Client()->DidFinishLoad();
}

void WebLocalFrameImpl::SetCanHaveScrollbars(bool can_have_scrollbars) {
  if (FrameView* view = GetFrameView())
    view->SetCanHaveScrollbars(can_have_scrollbars);
}

void WebLocalFrameImpl::SetInputEventsTransformForEmulation(
    const IntSize& offset,
    float content_scale_factor) {
  input_events_offset_for_emulation_ = offset;
  input_events_scale_factor_for_emulation_ = content_scale_factor;
  if (GetFrame()->View())
    GetFrame()->View()->SetInputEventsTransformForEmulation(
        input_events_offset_for_emulation_,
        input_events_scale_factor_for_emulation_);
}

void WebLocalFrameImpl::LoadJavaScriptURL(const KURL& url) {
  // This is copied from ScriptController::executeScriptIfJavaScriptURL.
  // Unfortunately, we cannot just use that method since it is private, and
  // it also doesn't quite behave as we require it to for bookmarklets. The
  // key difference is that we need to suppress loading the string result
  // from evaluating the JS URL if executing the JS URL resulted in a
  // location change. We also allow a JS URL to be loaded even if scripts on
  // the page are otherwise disabled.

  if (!GetFrame()->GetDocument() || !GetFrame()->GetPage())
    return;

  Document* owner_document(GetFrame()->GetDocument());

  // Protect privileged pages against bookmarklets and other javascript
  // manipulations.
  if (SchemeRegistry::ShouldTreatURLSchemeAsNotAllowingJavascriptURLs(
          GetFrame()->GetDocument()->Url().Protocol()))
    return;

  String script = DecodeURLEscapeSequences(
      url.GetString().Substring(strlen("javascript:")));
  UserGestureIndicator gesture_indicator(DocumentUserGestureToken::Create(
      GetFrame()->GetDocument(), UserGestureToken::kNewGesture));
  v8::HandleScope handle_scope(ToIsolate(GetFrame()));
  v8::Local<v8::Value> result =
      GetFrame()->GetScriptController().ExecuteScriptInMainWorldAndReturnValue(
          ScriptSourceCode(script));
  if (result.IsEmpty() || !result->IsString())
    return;
  String script_result = ToCoreString(v8::Local<v8::String>::Cast(result));
  if (!GetFrame()->GetNavigationScheduler().LocationChangePending())
    GetFrame()->Loader().ReplaceDocumentWhileExecutingJavaScriptURL(
        script_result, owner_document);
}

HitTestResult WebLocalFrameImpl::HitTestResultForVisualViewportPos(
    const IntPoint& pos_in_viewport) {
  IntPoint root_frame_point(
      GetFrame()->GetPage()->GetVisualViewport().ViewportToRootFrame(
          pos_in_viewport));
  IntPoint doc_point(GetFrame()->View()->RootFrameToContents(root_frame_point));
  HitTestResult result = GetFrame()->GetEventHandler().HitTestResultAtPoint(
      doc_point, HitTestRequest::kReadOnly | HitTestRequest::kActive);
  result.SetToShadowHostIfInRestrictedShadowRoot();
  return result;
}

static void EnsureFrameLoaderHasCommitted(FrameLoader& frame_loader) {
  // Internally, Blink uses CommittedMultipleRealLoads to track whether the
  // next commit should create a new history item or not. Ensure we have
  // reached that state.
  if (frame_loader.StateMachine()->CommittedMultipleRealLoads())
    return;
  frame_loader.StateMachine()->AdvanceTo(
      FrameLoaderStateMachine::kCommittedMultipleRealLoads);
}

void WebLocalFrameImpl::SetAutofillClient(WebAutofillClient* autofill_client) {
  autofill_client_ = autofill_client;
}

WebAutofillClient* WebLocalFrameImpl::AutofillClient() {
  return autofill_client_;
}

void WebLocalFrameImpl::SetDevToolsAgentClient(
    WebDevToolsAgentClient* dev_tools_client) {
  DCHECK(dev_tools_client);
  dev_tools_agent_ = WebDevToolsAgentImpl::Create(this, dev_tools_client);
}

WebDevToolsAgent* WebLocalFrameImpl::DevToolsAgent() {
  return dev_tools_agent_.Get();
}

WebLocalFrameImpl* WebLocalFrameImpl::LocalRoot() {
  // This can't use the LocalFrame::localFrameRoot, since it may be called
  // when the WebLocalFrame exists but the core LocalFrame does not.
  // TODO(alexmos, dcheng): Clean this up to only calculate this in one place.
  WebLocalFrameImpl* local_root = this;
  while (local_root->Parent() && local_root->Parent()->IsWebLocalFrame())
    local_root = ToWebLocalFrameImpl(local_root->Parent());
  return local_root;
}

void WebLocalFrameImpl::SendPings(const WebURL& destination_url) {
  DCHECK(GetFrame());
  DCHECK(context_menu_node_.Get());
  Element* anchor = context_menu_node_->EnclosingLinkEventParentOrSelf();
  if (isHTMLAnchorElement(anchor))
    toHTMLAnchorElement(anchor)->SendPings(destination_url);
}

bool WebLocalFrameImpl::DispatchBeforeUnloadEvent(bool is_reload) {
  if (!GetFrame())
    return true;

  return GetFrame()->Loader().ShouldClose(is_reload);
}

WebURLRequest WebLocalFrameImpl::RequestFromHistoryItem(
    const WebHistoryItem& item,
    WebCachePolicy cache_policy) const {
  HistoryItem* history_item = item;
  return WrappedResourceRequest(
      history_item->GenerateResourceRequest(cache_policy));
}

WebURLRequest WebLocalFrameImpl::RequestForReload(
    WebFrameLoadType load_type,
    const WebURL& override_url) const {
  DCHECK(GetFrame());
  ResourceRequest request = GetFrame()->Loader().ResourceRequestForReload(
      static_cast<FrameLoadType>(load_type), override_url);
  return WrappedResourceRequest(request);
}

void WebLocalFrameImpl::Load(const WebURLRequest& request,
                             WebFrameLoadType web_frame_load_type,
                             const WebHistoryItem& item,
                             WebHistoryLoadType web_history_load_type,
                             bool is_client_redirect) {
  DCHECK(GetFrame());
  DCHECK(!request.IsNull());
  const ResourceRequest& resource_request = request.ToResourceRequest();

  if (resource_request.Url().ProtocolIs("javascript") &&
      web_frame_load_type == WebFrameLoadType::kStandard) {
    LoadJavaScriptURL(resource_request.Url());
    return;
  }

  FrameLoadRequest frame_request = FrameLoadRequest(nullptr, resource_request);
  if (is_client_redirect)
    frame_request.SetClientRedirect(ClientRedirectPolicy::kClientRedirect);
  HistoryItem* history_item = item;
  GetFrame()->Loader().Load(
      frame_request, static_cast<FrameLoadType>(web_frame_load_type),
      history_item, static_cast<HistoryLoadType>(web_history_load_type));
}

void WebLocalFrameImpl::LoadData(const WebData& data,
                                 const WebString& mime_type,
                                 const WebString& text_encoding,
                                 const WebURL& base_url,
                                 const WebURL& unreachable_url,
                                 bool replace,
                                 WebFrameLoadType web_frame_load_type,
                                 const WebHistoryItem& item,
                                 WebHistoryLoadType web_history_load_type,
                                 bool is_client_redirect) {
  DCHECK(GetFrame());

  // If we are loading substitute data to replace an existing load, then
  // inherit all of the properties of that original request. This way,
  // reload will re-attempt the original request. It is essential that
  // we only do this when there is an unreachableURL since a non-empty
  // unreachableURL informs FrameLoader::reload to load unreachableURL
  // instead of the currently loaded URL.
  ResourceRequest request;
  HistoryItem* history_item = item;
  DocumentLoader* provisional_document_loader =
      GetFrame()->Loader().ProvisionalDocumentLoader();
  if (replace && !unreachable_url.IsEmpty() && provisional_document_loader) {
    request = provisional_document_loader->OriginalRequest();
    // When replacing a failed back/forward provisional navigation with an error
    // page, retain the HistoryItem for the failed provisional navigation
    // and reuse it for the error page navigation.
    if (provisional_document_loader->LoadType() == kFrameLoadTypeBackForward) {
      history_item = provisional_document_loader->GetHistoryItem();
      web_frame_load_type = WebFrameLoadType::kBackForward;
    }
  }
  request.SetURL(base_url);
  request.SetCheckForBrowserSideNavigation(false);

  FrameLoadRequest frame_request(
      0, request,
      SubstituteData(data, mime_type, text_encoding, unreachable_url));
  DCHECK(frame_request.GetSubstituteData().IsValid());
  frame_request.SetReplacesCurrentItem(replace);
  if (is_client_redirect)
    frame_request.SetClientRedirect(ClientRedirectPolicy::kClientRedirect);

  GetFrame()->Loader().Load(
      frame_request, static_cast<FrameLoadType>(web_frame_load_type),
      history_item, static_cast<HistoryLoadType>(web_history_load_type));
}

WebLocalFrame::FallbackContentResult
WebLocalFrameImpl::MaybeRenderFallbackContent(const WebURLError& error) const {
  DCHECK(GetFrame());

  if (!GetFrame()->Owner() || !GetFrame()->Owner()->CanRenderFallbackContent())
    return NoFallbackContent;

  // provisionalDocumentLoader() can be null if a navigation started and
  // completed (e.g. about:blank) while waiting for the navigation that wants
  // to show fallback content.
  if (!GetFrame()->Loader().ProvisionalDocumentLoader())
    return NoLoadInProgress;

  GetFrame()->Loader().ProvisionalDocumentLoader()->LoadFailed(error);
  return FallbackRendered;
}

// Called when a navigation is blocked because a Content Security Policy (CSP)
// is infringed.
void WebLocalFrameImpl::ReportContentSecurityPolicyViolation(
    const blink::WebContentSecurityPolicyViolation& violation) {
  AddMessageToConsole(blink::WebConsoleMessage(
      WebConsoleMessage::kLevelError, violation.console_message,
      violation.source_location.url, violation.source_location.line_number,
      violation.source_location.column_number));

  std::unique_ptr<SourceLocation> source_location = SourceLocation::Create(
      violation.source_location.url, violation.source_location.line_number,
      violation.source_location.column_number, nullptr);

  DCHECK(GetFrame() && GetFrame()->GetDocument());
  Document* document = GetFrame()->GetDocument();
  Vector<String> report_endpoints;
  for (const WebString& end_point : violation.report_endpoints)
    report_endpoints.push_back(end_point);
  document->GetContentSecurityPolicy()->ReportViolation(
      violation.directive, /* directiveText */
      ContentSecurityPolicy::GetDirectiveType(
          violation.effective_directive), /* effectiveType */
      violation.console_message,          /* consoleMessage */
      violation.blocked_url,              /* blockedUrl */
      report_endpoints,                   /* reportEndpoints */
      violation.header,                   /* header */
      static_cast<ContentSecurityPolicyHeaderType>(violation.disposition),
      ContentSecurityPolicy::ViolationType::kURLViolation, /* ViolationType */
      std::move(source_location), nullptr,                 /* LocalFrame */
      violation.after_redirect ? RedirectStatus::kFollowedRedirect
                               : RedirectStatus::kNoRedirect,
      nullptr); /* Element */
}

bool WebLocalFrameImpl::IsLoading() const {
  if (!GetFrame() || !GetFrame()->GetDocument())
    return false;
  return GetFrame()
             ->Loader()
             .StateMachine()
             ->IsDisplayingInitialEmptyDocument() ||
         GetFrame()->Loader().HasProvisionalNavigation() ||
         !GetFrame()->GetDocument()->LoadEventFinished();
}

bool WebLocalFrameImpl::IsNavigationScheduledWithin(
    double interval_in_seconds) const {
  return GetFrame() &&
         GetFrame()->GetNavigationScheduler().IsNavigationScheduledWithin(
             interval_in_seconds);
}

void WebLocalFrameImpl::SetCommittedFirstRealLoad() {
  DCHECK(GetFrame());
  EnsureFrameLoaderHasCommitted(GetFrame()->Loader());
}

void WebLocalFrameImpl::SetHasReceivedUserGesture() {
  if (GetFrame())
    GetFrame()->SetDocumentHasReceivedUserGesture();
}

void WebLocalFrameImpl::BlinkFeatureUsageReport(const std::set<int>& features) {
  DCHECK(!features.empty());
  // Assimilate all features used/performed by the browser into UseCounter.
  for (int feature : features) {
    UseCounter::Count(GetFrame(), static_cast<UseCounter::Feature>(feature));
  }
}

void WebLocalFrameImpl::MixedContentFound(
    const WebURL& main_resource_url,
    const WebURL& mixed_content_url,
    WebURLRequest::RequestContext request_context,
    bool was_allowed,
    bool had_redirect,
    const WebSourceLocation& source_location) {
  DCHECK(GetFrame());
  std::unique_ptr<SourceLocation> source;
  if (!source_location.url.IsNull()) {
    source =
        SourceLocation::Create(source_location.url, source_location.line_number,
                               source_location.column_number, nullptr);
  }
  MixedContentChecker::MixedContentFound(
      GetFrame(), main_resource_url, mixed_content_url, request_context,
      was_allowed, had_redirect, std::move(source));
}

void WebLocalFrameImpl::SendOrientationChangeEvent() {
  if (!GetFrame())
    return;

  // Screen Orientation API
  if (ScreenOrientationController::From(*GetFrame()))
    ScreenOrientationController::From(*GetFrame())->NotifyOrientationChanged();

  // Legacy window.orientation API
  if (RuntimeEnabledFeatures::orientationEventEnabled() &&
      GetFrame()->DomWindow())
    GetFrame()->DomWindow()->SendOrientationChangeEvent();
}

void WebLocalFrameImpl::DidCallAddSearchProvider() {
  UseCounter::Count(GetFrame(), UseCounter::kExternalAddSearchProvider);
}

void WebLocalFrameImpl::DidCallIsSearchProviderInstalled() {
  UseCounter::Count(GetFrame(), UseCounter::kExternalIsSearchProviderInstalled);
}

void WebLocalFrameImpl::RequestFind(int identifier,
                                    const WebString& search_text,
                                    const WebFindOptions& options) {
  // Send "no results" if this frame has no visible content.
  if (!HasVisibleContent() && !options.force) {
    Client()->ReportFindInPageMatchCount(identifier, 0 /* count */,
                                         true /* finalUpdate */);
    return;
  }

  WebRange current_selection = SelectionRange();
  bool result = false;
  bool active_now = false;

  // Search for an active match only if this frame is focused or if this is a
  // find next request.
  if (IsFocused() || options.find_next) {
    result = Find(identifier, search_text, options, false /* wrapWithinFrame */,
                  &active_now);
  }

  if (result && !options.find_next) {
    // Indicate that at least one match has been found. 1 here means
    // possibly more matches could be coming.
    Client()->ReportFindInPageMatchCount(identifier, 1 /* count */,
                                         false /* finalUpdate */);
  }

  // There are three cases in which scoping is needed:
  //
  // (1) This is an initial find request (|options.findNext| is false). This
  // will be the first scoping effort for this find session.
  //
  // (2) Something has been selected since the last search. This means that we
  // cannot just increment the current match ordinal; we need to re-generate
  // it.
  //
  // (3) TextFinder::Find() found what should be the next match (|result| is
  // true), but was unable to activate it (|activeNow| is false). This means
  // that the text containing this match was dynamically added since the last
  // scope of the frame. The frame needs to be re-scoped so that any matches
  // in the new text can be highlighted and included in the reported number of
  // matches.
  //
  // If none of these cases are true, then we just report the current match
  // count without scoping.
  if (/* (1) */ options.find_next && /* (2) */ current_selection.IsNull() &&
      /* (3) */ !(result && !active_now)) {
    // Force report of the actual count.
    IncreaseMatchCount(0, identifier);
    return;
  }

  // Start a new scoping request. If the scoping function determines that it
  // needs to scope, it will defer until later.
  EnsureTextFinder().StartScopingStringMatches(identifier, search_text,
                                               options);
}

bool WebLocalFrameImpl::Find(int identifier,
                             const WebString& search_text,
                             const WebFindOptions& options,
                             bool wrap_within_frame,
                             bool* active_now) {
  if (!GetFrame())
    return false;

  // Unlikely, but just in case we try to find-in-page on a detached frame.
  DCHECK(GetFrame()->GetPage());

  // Up-to-date, clean tree is required for finding text in page, since it
  // relies on TextIterator to look over the text.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  return EnsureTextFinder().Find(identifier, search_text, options,
                                 wrap_within_frame, active_now);
}

void WebLocalFrameImpl::StopFinding(StopFindAction action) {
  bool clear_selection = action == kStopFindActionClearSelection;
  if (clear_selection)
    ExecuteCommand(WebString::FromUTF8("Unselect"));

  if (text_finder_) {
    if (!clear_selection)
      SetFindEndstateFocusAndSelection();
    text_finder_->StopFindingAndClearSelection();
  }

  if (action == kStopFindActionActivateSelection && IsFocused()) {
    WebDocument doc = GetDocument();
    if (!doc.IsNull()) {
      WebElement element = doc.FocusedElement();
      if (!element.IsNull())
        element.SimulateClick();
    }
  }
}

void WebLocalFrameImpl::IncreaseMatchCount(int count, int identifier) {
  EnsureTextFinder().IncreaseMatchCount(identifier, count);
}

void WebLocalFrameImpl::DispatchMessageEventWithOriginCheck(
    const WebSecurityOrigin& intended_target_origin,
    const WebDOMEvent& event) {
  DCHECK(!event.IsNull());
  GetFrame()->DomWindow()->DispatchMessageEventWithOriginCheck(
      intended_target_origin.Get(), event,
      SourceLocation::Create(String(), 0, 0, nullptr));
}

int WebLocalFrameImpl::FindMatchMarkersVersion() const {
  if (text_finder_)
    return text_finder_->FindMatchMarkersVersion();
  return 0;
}

int WebLocalFrameImpl::SelectNearestFindMatch(const WebFloatPoint& point,
                                              WebRect* selection_rect) {
  return EnsureTextFinder().SelectNearestFindMatch(point, selection_rect);
}

float WebLocalFrameImpl::DistanceToNearestFindMatch(
    const WebFloatPoint& point) {
  float nearest_distance;
  EnsureTextFinder().NearestFindMatch(point, &nearest_distance);
  return nearest_distance;
}

WebFloatRect WebLocalFrameImpl::ActiveFindMatchRect() {
  if (text_finder_)
    return text_finder_->ActiveFindMatchRect();
  return WebFloatRect();
}

void WebLocalFrameImpl::FindMatchRects(WebVector<WebFloatRect>& output_rects) {
  EnsureTextFinder().FindMatchRects(output_rects);
}

void WebLocalFrameImpl::SetTickmarks(const WebVector<WebRect>& tickmarks) {
  if (GetFrameView()) {
    Vector<IntRect> tickmarks_converted(tickmarks.size());
    for (size_t i = 0; i < tickmarks.size(); ++i)
      tickmarks_converted[i] = tickmarks[i];
    GetFrameView()->SetTickmarks(tickmarks_converted);
  }
}

void WebLocalFrameImpl::WillBeDetached() {
  if (dev_tools_agent_)
    dev_tools_agent_->WillBeDestroyed();
}

void WebLocalFrameImpl::WillDetachParent() {
  // Do not expect string scoping results from any frames that got detached
  // in the middle of the operation.
  if (text_finder_ && text_finder_->ScopingInProgress()) {
    // There is a possibility that the frame being detached was the only
    // pending one. We need to make sure final replies can be sent.
    text_finder_->FlushCurrentScoping();

    text_finder_->CancelPendingScopingEffort();
  }
}

TextFinder* WebLocalFrameImpl::GetTextFinder() const {
  return text_finder_;
}

TextFinder& WebLocalFrameImpl::EnsureTextFinder() {
  if (!text_finder_)
    text_finder_ = TextFinder::Create(*this);

  return *text_finder_;
}

void WebLocalFrameImpl::SetFrameWidget(WebFrameWidgetBase* frame_widget) {
  frame_widget_ = frame_widget;
}

WebFrameWidgetBase* WebLocalFrameImpl::FrameWidget() const {
  return frame_widget_;
}

std::unique_ptr<WebURLLoader> WebLocalFrameImpl::CreateURLLoader() {
  return client_->CreateURLLoader();
}

void WebLocalFrameImpl::CopyImageAt(const WebPoint& pos_in_viewport) {
  HitTestResult result = HitTestResultForVisualViewportPos(pos_in_viewport);
  if (!isHTMLCanvasElement(result.InnerNodeOrImageMapImage()) &&
      result.AbsoluteImageURL().IsEmpty()) {
    // There isn't actually an image at these coordinates.  Might be because
    // the window scrolled while the context menu was open or because the page
    // changed itself between when we thought there was an image here and when
    // we actually tried to retreive the image.
    //
    // FIXME: implement a cache of the most recent HitTestResult to avoid having
    //        to do two hit tests.
    return;
  }

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  GetFrame()->GetEditor().CopyImage(result);
}

void WebLocalFrameImpl::SaveImageAt(const WebPoint& pos_in_viewport) {
  Node* node = HitTestResultForVisualViewportPos(pos_in_viewport)
                   .InnerNodeOrImageMapImage();
  if (!node || !(isHTMLCanvasElement(*node) || isHTMLImageElement(*node)))
    return;

  String url = ToElement(*node).ImageSourceURL();
  if (!KURL(KURL(), url).ProtocolIsData())
    return;

  client_->SaveImageFromDataURL(url);
}

void WebLocalFrameImpl::SetEngagementLevel(mojom::EngagementLevel level) {
  GetFrame()->GetDocument()->SetEngagementLevel(level);
}

WebSandboxFlags WebLocalFrameImpl::EffectiveSandboxFlags() const {
  if (!GetFrame())
    return WebSandboxFlags::kNone;
  return static_cast<WebSandboxFlags>(
      GetFrame()->Loader().EffectiveSandboxFlags());
}

void WebLocalFrameImpl::ForceSandboxFlags(WebSandboxFlags flags) {
  GetFrame()->Loader().ForceSandboxFlags(static_cast<SandboxFlags>(flags));
}

void WebLocalFrameImpl::ClearActiveFindMatch() {
  EnsureTextFinder().ClearActiveFindMatch();
}

void WebLocalFrameImpl::UsageCountChromeLoadTimes(const WebString& metric) {
  UseCounter::Feature feature = UseCounter::kChromeLoadTimesUnknown;
  if (metric == "requestTime") {
    feature = UseCounter::kChromeLoadTimesRequestTime;
  } else if (metric == "startLoadTime") {
    feature = UseCounter::kChromeLoadTimesStartLoadTime;
  } else if (metric == "commitLoadTime") {
    feature = UseCounter::kChromeLoadTimesCommitLoadTime;
  } else if (metric == "finishDocumentLoadTime") {
    feature = UseCounter::kChromeLoadTimesFinishDocumentLoadTime;
  } else if (metric == "finishLoadTime") {
    feature = UseCounter::kChromeLoadTimesFinishLoadTime;
  } else if (metric == "firstPaintTime") {
    feature = UseCounter::kChromeLoadTimesFirstPaintTime;
  } else if (metric == "firstPaintAfterLoadTime") {
    feature = UseCounter::kChromeLoadTimesFirstPaintAfterLoadTime;
  } else if (metric == "navigationType") {
    feature = UseCounter::kChromeLoadTimesNavigationType;
  } else if (metric == "wasFetchedViaSpdy") {
    feature = UseCounter::kChromeLoadTimesWasFetchedViaSpdy;
  } else if (metric == "wasNpnNegotiated") {
    feature = UseCounter::kChromeLoadTimesWasNpnNegotiated;
  } else if (metric == "npnNegotiatedProtocol") {
    feature = UseCounter::kChromeLoadTimesNpnNegotiatedProtocol;
  } else if (metric == "wasAlternateProtocolAvailable") {
    feature = UseCounter::kChromeLoadTimesWasAlternateProtocolAvailable;
  } else if (metric == "connectionInfo") {
    feature = UseCounter::kChromeLoadTimesConnectionInfo;
  }
  UseCounter::Count(GetFrame(), feature);
}

base::SingleThreadTaskRunner* WebLocalFrameImpl::TimerTaskRunner() {
  return GetFrame()
      ->FrameScheduler()
      ->TimerTaskRunner()
      ->ToSingleThreadTaskRunner();
}

base::SingleThreadTaskRunner* WebLocalFrameImpl::LoadingTaskRunner() {
  return GetFrame()
      ->FrameScheduler()
      ->LoadingTaskRunner()
      ->ToSingleThreadTaskRunner();
}

base::SingleThreadTaskRunner* WebLocalFrameImpl::UnthrottledTaskRunner() {
  return GetFrame()
      ->FrameScheduler()
      ->UnthrottledTaskRunner()
      ->ToSingleThreadTaskRunner();
}

WebInputMethodControllerImpl* WebLocalFrameImpl::GetInputMethodController()
    const {
  return input_method_controller_.get();
}

void WebLocalFrameImpl::ExtractSmartClipData(WebRect rect_in_viewport,
                                             WebString& clip_text,
                                             WebString& clip_html) {
  SmartClipData clip_data = SmartClip(GetFrame()).DataForRect(rect_in_viewport);
  clip_text = clip_data.ClipData();

  WebPoint start_point(rect_in_viewport.x, rect_in_viewport.y);
  WebPoint end_point(rect_in_viewport.x + rect_in_viewport.width,
                     rect_in_viewport.y + rect_in_viewport.height);
  VisiblePosition start_visible_position =
      VisiblePositionForViewportPoint(start_point);
  VisiblePosition end_visible_position =
      VisiblePositionForViewportPoint(end_point);

  Position start_position = start_visible_position.DeepEquivalent();
  Position end_position = end_visible_position.DeepEquivalent();

  // document() will return null if -webkit-user-select is set to none.
  if (!start_position.GetDocument() || !end_position.GetDocument())
    return;

  if (start_position.CompareTo(end_position) <= 0) {
    clip_html =
        CreateMarkup(start_position, end_position, kAnnotateForInterchange,
                     ConvertBlocksToInlines::kNotConvert, kResolveNonLocalURLs);
  } else {
    clip_html =
        CreateMarkup(end_position, start_position, kAnnotateForInterchange,
                     ConvertBlocksToInlines::kNotConvert, kResolveNonLocalURLs);
  }
}

TextCheckerClient& WebLocalFrameImpl::GetTextCheckerClient() const {
  return *text_checker_client_;
}

void WebLocalFrameImpl::SetTextCheckClient(
    WebTextCheckClient* text_check_client) {
  text_check_client_ = text_check_client;
}

}  // namespace blink
