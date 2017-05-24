/*
 * Copyright (C) 2011, 2012 Google Inc. All rights reserved.
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

#include "web/WebViewImpl.h"

#include <memory>
#include "core/CSSValueKeywords.h"
#include "core/HTMLNames.h"
#include "core/clipboard/DataObject.h"
#include "core/dom/ContextFeaturesClientImpl.h"
#include "core/dom/Document.h"
#include "core/dom/DocumentUserGestureToken.h"
#include "core/dom/Fullscreen.h"
#include "core/dom/LayoutTreeBuilderTraversal.h"
#include "core/dom/Text.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/Editor.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/InputMethodController.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/editing/serializers/HTMLInterchange.h"
#include "core/editing/serializers/Serialization.h"
#include "core/events/KeyboardEvent.h"
#include "core/events/UIEventWithKeyState.h"
#include "core/events/WebInputEventConversion.h"
#include "core/events/WheelEvent.h"
#include "core/exported/WebPluginContainerBase.h"
#include "core/frame/BrowserControls.h"
#include "core/frame/EventHandlerRegistry.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/PageScaleConstraintsSet.h"
#include "core/frame/RemoteFrame.h"
#include "core/frame/ResizeViewportAnchor.h"
#include "core/frame/RotationViewportAnchor.h"
#include "core/frame/Settings.h"
#include "core/frame/UseCounter.h"
#include "core/frame/VisualViewport.h"
#include "core/html/HTMLMediaElement.h"
#include "core/html/HTMLPlugInElement.h"
#include "core/html/HTMLTextAreaElement.h"
#include "core/input/EventHandler.h"
#include "core/input/TouchActionUtil.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/TextAutosizer.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/loader/FrameLoadRequest.h"
#include "core/loader/FrameLoader.h"
#include "core/loader/FrameLoaderStateMachine.h"
#include "core/page/ContextMenuController.h"
#include "core/page/ContextMenuProvider.h"
#include "core/page/FocusController.h"
#include "core/page/FrameTree.h"
#include "core/page/Page.h"
#include "core/page/PagePopupClient.h"
#include "core/page/PointerLockController.h"
#include "core/page/ScopedPageSuspender.h"
#include "core/page/TouchDisambiguation.h"
#include "core/page/ValidationMessageClientImpl.h"
#include "core/page/scrolling/TopDocumentRootScrollerController.h"
#include "core/paint/PaintLayer.h"
#include "core/timing/DOMWindowPerformance.h"
#include "core/timing/Performance.h"
#include "modules/accessibility/AXObjectCacheImpl.h"
#include "modules/accessibility/AXObjectImpl.h"
#include "modules/credentialmanager/CredentialManagerClient.h"
#include "modules/encryptedmedia/MediaKeysController.h"
#include "modules/speech/SpeechRecognitionClientProxy.h"
#include "modules/storage/StorageNamespaceController.h"
#include "modules/webdatabase/DatabaseClient.h"
#include "modules/webgl/WebGLRenderingContext.h"
#include "platform/ContextMenu.h"
#include "platform/ContextMenuItem.h"
#include "platform/Cursor.h"
#include "platform/Histogram.h"
#include "platform/KeyboardCodes.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/UserGestureIndicator.h"
#include "platform/animation/CompositorAnimationHost.h"
#include "platform/exported/WebActiveGestureAnimation.h"
#include "platform/fonts/FontCache.h"
#include "platform/geometry/FloatRect.h"
#include "platform/graphics/Color.h"
#include "platform/graphics/CompositorMutatorClient.h"
#include "platform/graphics/FirstPaintInvalidationTracking.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/Image.h"
#include "platform/graphics/ImageBuffer.h"
#include "platform/graphics/gpu/DrawingBuffer.h"
#include "platform/graphics/paint/DrawingRecorder.h"
#include "platform/image-decoders/ImageDecoder.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/loader/fetch/UniqueIdentifier.h"
#include "platform/scheduler/child/web_scheduler.h"
#include "platform/scheduler/renderer/web_view_scheduler.h"
#include "platform/scroll/ScrollbarTheme.h"
#include "platform/weborigin/SchemeRegistry.h"
#include "platform/wtf/AutoReset.h"
#include "platform/wtf/CurrentTime.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/RefPtr.h"
#include "public/platform/Platform.h"
#include "public/platform/WebCompositeAndReadbackAsyncCallback.h"
#include "public/platform/WebCompositorSupport.h"
#include "public/platform/WebFloatPoint.h"
#include "public/platform/WebGestureCurve.h"
#include "public/platform/WebImage.h"
#include "public/platform/WebLayerTreeView.h"
#include "public/platform/WebTextInputInfo.h"
#include "public/platform/WebURLRequest.h"
#include "public/platform/WebVector.h"
#include "public/web/WebAXObject.h"
#include "public/web/WebActiveWheelFlingParameters.h"
#include "public/web/WebAutofillClient.h"
#include "public/web/WebConsoleMessage.h"
#include "public/web/WebElement.h"
#include "public/web/WebFrame.h"
#include "public/web/WebFrameClient.h"
#include "public/web/WebFrameWidget.h"
#include "public/web/WebHitTestResult.h"
#include "public/web/WebInputElement.h"
#include "public/web/WebMeaningfulLayout.h"
#include "public/web/WebMediaPlayerAction.h"
#include "public/web/WebNode.h"
#include "public/web/WebPlugin.h"
#include "public/web/WebPluginAction.h"
#include "public/web/WebRange.h"
#include "public/web/WebScopedUserGesture.h"
#include "public/web/WebSelection.h"
#include "public/web/WebViewClient.h"
#include "public/web/WebWindowFeatures.h"
#include "web/AnimationWorkletProxyClientImpl.h"
#include "web/CompositorMutatorImpl.h"
#include "web/CompositorWorkerProxyClientImpl.h"
#include "web/ContextMenuAllowedScope.h"
#include "web/DedicatedWorkerMessagingProxyProviderImpl.h"
#include "web/DevToolsEmulator.h"
#include "web/FullscreenController.h"
#include "web/LinkHighlightImpl.h"
#include "web/PageOverlay.h"
#include "web/PrerendererClientImpl.h"
#include "web/StorageQuotaClientImpl.h"
#include "web/WebDevToolsAgentImpl.h"
#include "web/WebInputMethodControllerImpl.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebRemoteFrameImpl.h"
#include "web/WebSettingsImpl.h"

#if USE(DEFAULT_RENDER_THEME)
#include "core/layout/LayoutThemeDefault.h"
#endif

// Get rid of WTF's pow define so we can use std::pow.
#undef pow
#include <cmath>  // for std::pow

// The following constants control parameters for automated scaling of webpages
// (such as due to a double tap gesture or find in page etc.). These are
// experimentally determined.
static const int touchPointPadding = 32;
static const int nonUserInitiatedPointPadding = 11;
static const float minScaleDifference = 0.01f;
static const float doubleTapZoomContentDefaultMargin = 5;
static const float doubleTapZoomContentMinimumMargin = 2;
static const double doubleTapZoomAnimationDurationInSeconds = 0.25;
static const float doubleTapZoomAlreadyLegibleRatio = 1.2f;

static const double multipleTargetsZoomAnimationDurationInSeconds = 0.25;
static const double findInPageAnimationDurationInSeconds = 0;

// Constants for viewport anchoring on resize.
static const float viewportAnchorCoordX = 0.5f;
static const float viewportAnchorCoordY = 0;

// Constants for zooming in on a focused text field.
static const double scrollAndScaleAnimationDurationInSeconds = 0.2;
static const int minReadableCaretHeight = 16;
static const int minReadableCaretHeightForTextArea = 13;
static const float minScaleChangeToTriggerZoom = 1.5f;
static const float leftBoxRatio = 0.3f;
static const int caretPadding = 10;

namespace blink {

// Change the text zoom level by kTextSizeMultiplierRatio each time the user
// zooms text in or out (ie., change by 20%).  The min and max values limit
// text zoom to half and 3x the original text size.  These three values match
// those in Apple's port in WebKit/WebKit/WebView/WebView.mm
const double WebView::kTextSizeMultiplierRatio = 1.2;
const double WebView::kMinTextSizeMultiplier = 0.5;
const double WebView::kMaxTextSizeMultiplier = 3.0;

// Used to defer all page activity in cases where the embedder wishes to run
// a nested event loop. Using a stack enables nesting of message loop
// invocations.
static Vector<std::unique_ptr<ScopedPageSuspender>>& PageSuspenderStack() {
  DEFINE_STATIC_LOCAL(Vector<std::unique_ptr<ScopedPageSuspender>>,
                      suspender_stack, ());
  return suspender_stack;
}

static bool g_should_use_external_popup_menus = false;

namespace {

class EmptyEventListener final : public EventListener {
 public:
  static EmptyEventListener* Create() { return new EmptyEventListener(); }

  bool operator==(const EventListener& other) const override {
    return this == &other;
  }

 private:
  EmptyEventListener() : EventListener(kCPPEventListenerType) {}

  void handleEvent(ExecutionContext* execution_context, Event*) override {}
};

class ColorOverlay final : public PageOverlay::Delegate {
 public:
  explicit ColorOverlay(WebColor color) : color_(color) {}

 private:
  void PaintPageOverlay(const PageOverlay& page_overlay,
                        GraphicsContext& graphics_context,
                        const WebSize& size) const override {
    if (DrawingRecorder::UseCachedDrawingIfPossible(
            graphics_context, page_overlay, DisplayItem::kPageOverlay))
      return;
    FloatRect rect(0, 0, size.width, size.height);
    DrawingRecorder drawing_recorder(graphics_context, page_overlay,
                                     DisplayItem::kPageOverlay, rect);
    graphics_context.FillRect(rect, color_);
  }

  WebColor color_;
};

}  // namespace

// WebView ----------------------------------------------------------------

WebView* WebView::Create(WebViewClient* client,
                         WebPageVisibilityState visibility_state) {
  return WebViewImpl::Create(client, visibility_state);
}

WebViewBase* WebViewImpl::Create(WebViewClient* client,
                                 WebPageVisibilityState visibility_state) {
  // Pass the WebViewImpl's self-reference to the caller.
  return AdoptRef(new WebViewImpl(client, visibility_state)).LeakRef();
}

const WebInputEvent* WebViewBase::CurrentInputEvent() {
  return WebViewImpl::CurrentInputEvent();
}

void WebView::SetUseExternalPopupMenus(bool use_external_popup_menus) {
  g_should_use_external_popup_menus = use_external_popup_menus;
}

void WebView::UpdateVisitedLinkState(unsigned long long link_hash) {
  Page::VisitedStateChanged(link_hash);
}

void WebView::ResetVisitedLinkState(bool invalidate_visited_link_hashes) {
  Page::AllVisitedStateChanged(invalidate_visited_link_hashes);
}

void WebView::WillEnterModalLoop() {
  PageSuspenderStack().push_back(WTF::MakeUnique<ScopedPageSuspender>());
}

void WebView::DidExitModalLoop() {
  DCHECK(PageSuspenderStack().size());
  PageSuspenderStack().pop_back();
}

void WebViewImpl::SetMainFrame(WebFrame* frame) {
  WebFrame::InitializeCoreFrame(*frame, *GetPage());
}

void WebViewImpl::SetCredentialManagerClient(
    WebCredentialManagerClient* web_credential_manager_client) {
  DCHECK(page_);
  ProvideCredentialManagerClientTo(
      *page_, new CredentialManagerClient(web_credential_manager_client));
}

void WebViewImpl::SetPrerendererClient(
    WebPrerendererClient* prerenderer_client) {
  DCHECK(page_);
  ProvidePrerendererClientTo(
      *page_, new PrerendererClientImpl(*page_, prerenderer_client));
}

void WebViewImpl::SetSpellCheckClient(WebSpellCheckClient* spell_check_client) {
  spell_check_client_ = spell_check_client;
}

// static
HashSet<WebViewBase*>& WebViewBase::AllInstances() {
  DEFINE_STATIC_LOCAL(HashSet<WebViewBase*>, all_instances, ());
  return all_instances;
}

WebViewImpl::WebViewImpl(WebViewClient* client,
                         WebPageVisibilityState visibility_state)
    : client_(client),
      spell_check_client_(nullptr),
      chrome_client_impl_(ChromeClientImpl::Create(this)),
      context_menu_client_impl_(this),
      editor_client_impl_(this),
      spell_checker_client_impl_(this),
      storage_client_impl_(this),
      should_auto_resize_(false),
      zoom_level_(0),
      minimum_zoom_level_(ZoomFactorToZoomLevel(kMinTextSizeMultiplier)),
      maximum_zoom_level_(ZoomFactorToZoomLevel(kMaxTextSizeMultiplier)),
      zoom_factor_for_device_scale_factor_(0.f),
      maximum_legible_scale_(1),
      double_tap_zoom_page_scale_factor_(0),
      double_tap_zoom_pending_(false),
      enable_fake_page_scale_animation_for_testing_(false),
      fake_page_scale_animation_page_scale_factor_(0),
      fake_page_scale_animation_use_anchor_(false),
      compositor_device_scale_factor_override_(0),
      suppress_next_keypress_event_(false),
      ime_accept_events_(true),
      dev_tools_emulator_(nullptr),
      tabs_to_links_(false),
      layer_tree_view_(nullptr),
      root_layer_(nullptr),
      root_graphics_layer_(nullptr),
      visual_viewport_container_layer_(nullptr),
      matches_heuristics_for_gpu_rasterization_(false),
      fling_modifier_(0),
      fling_source_device_(kWebGestureDeviceUninitialized),
      fullscreen_controller_(FullscreenController::Create(this)),
      base_background_color_(Color::kWhite),
      base_background_color_override_enabled_(false),
      base_background_color_override_(Color::kTransparent),
      background_color_override_enabled_(false),
      background_color_override_(Color::kTransparent),
      zoom_factor_override_(0),
      should_dispatch_first_visually_non_empty_layout_(false),
      should_dispatch_first_layout_after_finished_parsing_(false),
      should_dispatch_first_layout_after_finished_loading_(false),
      display_mode_(kWebDisplayModeBrowser),
      elastic_overscroll_(FloatSize()),
      mutator_(nullptr),
      scheduler_(WTF::WrapUnique(Platform::Current()
                                     ->CurrentThread()
                                     ->Scheduler()
                                     ->CreateWebViewScheduler(this, this)
                                     .release())),
      last_frame_time_monotonic_(0),
      override_compositor_visibility_(false) {
  Page::PageClients page_clients;
  page_clients.chrome_client = chrome_client_impl_.Get();
  page_clients.context_menu_client = &context_menu_client_impl_;
  page_clients.editor_client = &editor_client_impl_;
  page_clients.spell_checker_client = &spell_checker_client_impl_;

  page_ = Page::CreateOrdinary(page_clients);
  MediaKeysController::ProvideMediaKeysTo(*page_, &media_keys_client_impl_);
  ProvideSpeechRecognitionTo(
      *page_, SpeechRecognitionClientProxy::Create(
                  client ? client->SpeechRecognizer() : nullptr));
  ProvideContextFeaturesTo(*page_, ContextFeaturesClientImpl::Create());
  ProvideDatabaseClientTo(*page_, new DatabaseClient);

  ProvideStorageQuotaClientTo(*page_, StorageQuotaClientImpl::Create());
  page_->SetValidationMessageClient(ValidationMessageClientImpl::Create(*this));
  ProvideDedicatedWorkerMessagingProxyProviderTo(
      *page_, DedicatedWorkerMessagingProxyProviderImpl::Create(*page_));
  StorageNamespaceController::ProvideStorageNamespaceTo(*page_,
                                                        &storage_client_impl_);

  SetVisibilityState(visibility_state, true);

  InitializeLayerTreeView();

  dev_tools_emulator_ = DevToolsEmulator::Create(this);

  AllInstances().insert(this);

  page_importance_signals_.SetObserver(client);
  resize_viewport_anchor_ = new ResizeViewportAnchor(*page_);
}

WebViewImpl::~WebViewImpl() {
  DCHECK(!page_);

  // Each highlight uses m_owningWebViewImpl->m_linkHighlightsTimeline
  // in destructor. m_linkHighlightsTimeline might be destroyed earlier
  // than m_linkHighlights.
  DCHECK(link_highlights_.IsEmpty());
}

WebDevToolsAgentImpl* WebViewImpl::MainFrameDevToolsAgentImpl() {
  WebLocalFrameImpl* main_frame = MainFrameImpl();
  return main_frame ? main_frame->DevToolsAgentImpl() : nullptr;
}

WebLocalFrameImpl* WebViewImpl::MainFrameImpl() const {
  return page_ && page_->MainFrame() && page_->MainFrame()->IsLocalFrame()
             ? WebLocalFrameImpl::FromFrame(page_->DeprecatedLocalMainFrame())
             : nullptr;
}

bool WebViewImpl::TabKeyCyclesThroughElements() const {
  DCHECK(page_);
  return page_->TabKeyCyclesThroughElements();
}

void WebViewImpl::SetTabKeyCyclesThroughElements(bool value) {
  if (page_)
    page_->SetTabKeyCyclesThroughElements(value);
}

void WebViewImpl::HandleMouseLeave(LocalFrame& main_frame,
                                   const WebMouseEvent& event) {
  client_->SetMouseOverURL(WebURL());
  PageWidgetEventHandler::HandleMouseLeave(main_frame, event);
}

void WebViewImpl::HandleMouseDown(LocalFrame& main_frame,
                                  const WebMouseEvent& event) {
  // If there is a popup open, close it as the user is clicking on the page
  // (outside of the popup). We also save it so we can prevent a click on an
  // element from immediately reopening the same popup.
  RefPtr<WebPagePopupImpl> page_popup;
  if (event.button == WebMouseEvent::Button::kLeft) {
    page_popup = page_popup_;
    HidePopups();
    DCHECK(!page_popup_);
  }

  // Take capture on a mouse down on a plugin so we can send it mouse events.
  // If the hit node is a plugin but a scrollbar is over it don't start mouse
  // capture because it will interfere with the scrollbar receiving events.
  IntPoint point(event.PositionInWidget().x, event.PositionInWidget().y);
  if (event.button == WebMouseEvent::Button::kLeft &&
      page_->MainFrame()->IsLocalFrame()) {
    point =
        page_->DeprecatedLocalMainFrame()->View()->RootFrameToContents(point);
    HitTestResult result(page_->DeprecatedLocalMainFrame()
                             ->GetEventHandler()
                             .HitTestResultAtPoint(point));
    result.SetToShadowHostIfInRestrictedShadowRoot();
    Node* hit_node = result.InnerNodeOrImageMapImage();

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

  if (page_popup_ && page_popup &&
      page_popup_->HasSamePopupClient(page_popup.Get())) {
    // That click triggered a page popup that is the same as the one we just
    // closed.  It needs to be closed.
    CancelPagePopup();
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

void WebViewImpl::SetDisplayMode(WebDisplayMode mode) {
  display_mode_ = mode;
  if (!MainFrameImpl() || !MainFrameImpl()->GetFrameView())
    return;

  MainFrameImpl()->GetFrameView()->SetDisplayMode(mode);
}

void WebViewImpl::MouseContextMenu(const WebMouseEvent& event) {
  if (!MainFrameImpl() || !MainFrameImpl()->GetFrameView())
    return;

  page_->GetContextMenuController().ClearContextMenu();

  WebMouseEvent transformed_event =
      TransformWebMouseEvent(MainFrameImpl()->GetFrameView(), event);
  IntPoint position_in_root_frame =
      FlooredIntPoint(transformed_event.PositionInRootFrame());

  // Find the right target frame. See issue 1186900.
  HitTestResult result = HitTestResultForRootFramePos(position_in_root_frame);
  Frame* target_frame;
  if (result.InnerNodeOrImageMapImage())
    target_frame = result.InnerNodeOrImageMapImage()->GetDocument().GetFrame();
  else
    target_frame = page_->GetFocusController().FocusedOrMainFrame();

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

void WebViewImpl::HandleMouseUp(LocalFrame& main_frame,
                                const WebMouseEvent& event) {
  PageWidgetEventHandler::HandleMouseUp(main_frame, event);

  if (GetPage()->GetSettings().GetShowContextMenuOnMouseUp()) {
    // Dispatch the contextmenu event regardless of if the click was swallowed.
    // On Mac/Linux, we handle it on mouse down, not up.
    if (event.button == WebMouseEvent::Button::kRight)
      MouseContextMenu(event);
  }
}

WebInputEventResult WebViewImpl::HandleMouseWheel(
    LocalFrame& main_frame,
    const WebMouseWheelEvent& event) {
  // Halt an in-progress fling on a wheel tick.
  if (!event.has_precise_scrolling_deltas)
    EndActiveFlingAnimation();

  HidePopups();
  return PageWidgetEventHandler::HandleMouseWheel(main_frame, event);
}

WebGestureEvent WebViewImpl::CreateGestureScrollEventFromFling(
    WebInputEvent::Type type,
    WebGestureDevice source_device) const {
  WebGestureEvent gesture_event(type, fling_modifier_,
                                WTF::MonotonicallyIncreasingTime());
  gesture_event.source_device = source_device;
  gesture_event.x = position_on_fling_start_.x;
  gesture_event.y = position_on_fling_start_.y;
  gesture_event.global_x = global_position_on_fling_start_.x;
  gesture_event.global_y = global_position_on_fling_start_.y;
  return gesture_event;
}

bool WebViewImpl::ScrollBy(const WebFloatSize& delta,
                           const WebFloatSize& velocity) {
  DCHECK_NE(fling_source_device_, kWebGestureDeviceUninitialized);
  if (!page_ || !page_->MainFrame() || !page_->MainFrame()->IsLocalFrame() ||
      !page_->DeprecatedLocalMainFrame()->View())
    return false;

  if (fling_source_device_ == kWebGestureDeviceTouchpad) {
    bool enable_touchpad_scroll_latching =
        RuntimeEnabledFeatures::touchpadAndWheelScrollLatchingEnabled();
    WebMouseWheelEvent synthetic_wheel(WebInputEvent::kMouseWheel,
                                       fling_modifier_,
                                       WTF::MonotonicallyIncreasingTime());
    const float kTickDivisor = WheelEvent::kTickMultiplier;

    synthetic_wheel.delta_x = delta.width;
    synthetic_wheel.delta_y = delta.height;
    synthetic_wheel.wheel_ticks_x = delta.width / kTickDivisor;
    synthetic_wheel.wheel_ticks_y = delta.height / kTickDivisor;
    synthetic_wheel.has_precise_scrolling_deltas = true;
    synthetic_wheel.SetPositionInWidget(position_on_fling_start_.x,
                                        position_on_fling_start_.y);
    synthetic_wheel.SetPositionInScreen(global_position_on_fling_start_.x,
                                        global_position_on_fling_start_.y);

    if (HandleMouseWheel(*page_->DeprecatedLocalMainFrame(), synthetic_wheel) !=
        WebInputEventResult::kNotHandled)
      return true;

    if (!enable_touchpad_scroll_latching) {
      WebGestureEvent synthetic_scroll_begin =
          CreateGestureScrollEventFromFling(WebInputEvent::kGestureScrollBegin,
                                            kWebGestureDeviceTouchpad);
      synthetic_scroll_begin.data.scroll_begin.delta_x_hint = delta.width;
      synthetic_scroll_begin.data.scroll_begin.delta_y_hint = delta.height;
      synthetic_scroll_begin.data.scroll_begin.inertial_phase =
          WebGestureEvent::kMomentumPhase;
      HandleGestureEvent(synthetic_scroll_begin);
    }

    WebGestureEvent synthetic_scroll_update = CreateGestureScrollEventFromFling(
        WebInputEvent::kGestureScrollUpdate, kWebGestureDeviceTouchpad);
    synthetic_scroll_update.data.scroll_update.delta_x = delta.width;
    synthetic_scroll_update.data.scroll_update.delta_y = delta.height;
    synthetic_scroll_update.data.scroll_update.velocity_x = velocity.width;
    synthetic_scroll_update.data.scroll_update.velocity_y = velocity.height;
    synthetic_scroll_update.data.scroll_update.inertial_phase =
        WebGestureEvent::kMomentumPhase;
    bool scroll_update_handled = HandleGestureEvent(synthetic_scroll_update) !=
                                 WebInputEventResult::kNotHandled;

    if (!enable_touchpad_scroll_latching) {
      WebGestureEvent synthetic_scroll_end = CreateGestureScrollEventFromFling(
          WebInputEvent::kGestureScrollEnd, kWebGestureDeviceTouchpad);
      synthetic_scroll_end.data.scroll_end.inertial_phase =
          WebGestureEvent::kMomentumPhase;
      HandleGestureEvent(synthetic_scroll_end);
    }

    return scroll_update_handled;
  } else {
    WebGestureEvent synthetic_gesture_event = CreateGestureScrollEventFromFling(
        WebInputEvent::kGestureScrollUpdate, fling_source_device_);
    synthetic_gesture_event.data.scroll_update.prevent_propagation = true;
    synthetic_gesture_event.data.scroll_update.delta_x = delta.width;
    synthetic_gesture_event.data.scroll_update.delta_y = delta.height;
    synthetic_gesture_event.data.scroll_update.velocity_x = velocity.width;
    synthetic_gesture_event.data.scroll_update.velocity_y = velocity.height;
    synthetic_gesture_event.data.scroll_update.inertial_phase =
        WebGestureEvent::kMomentumPhase;

    return HandleGestureEvent(synthetic_gesture_event) !=
           WebInputEventResult::kNotHandled;
  }
}

WebInputEventResult WebViewImpl::HandleGestureEvent(
    const WebGestureEvent& event) {
  if (!client_ || !client_->CanHandleGestureEvent()) {
    return WebInputEventResult::kNotHandled;
  }

  WebInputEventResult event_result = WebInputEventResult::kNotHandled;
  bool event_cancelled = false;  // for disambiguation

  // Special handling for slow-path fling gestures.
  switch (event.GetType()) {
    case WebInputEvent::kGestureFlingStart: {
      if (MainFrameImpl()
              ->GetFrame()
              ->GetEventHandler()
              .IsScrollbarHandlingGestures())
        break;
      EndActiveFlingAnimation();
      position_on_fling_start_ = WebPoint(event.x, event.y);
      global_position_on_fling_start_ =
          WebPoint(event.global_x, event.global_y);
      fling_modifier_ = event.GetModifiers();
      fling_source_device_ = event.source_device;
      DCHECK_NE(fling_source_device_, kWebGestureDeviceUninitialized);
      std::unique_ptr<WebGestureCurve> fling_curve =
          Platform::Current()->CreateFlingAnimationCurve(
              event.source_device,
              WebFloatPoint(event.data.fling_start.velocity_x,
                            event.data.fling_start.velocity_y),
              WebSize());
      DCHECK(fling_curve);
      gesture_animation_ = WebActiveGestureAnimation::CreateAtAnimationStart(
          std::move(fling_curve), this);
      MainFrameImpl()->FrameWidget()->ScheduleAnimation();
      event_result = WebInputEventResult::kHandledSystem;

      WebGestureEvent scaled_event =
          TransformWebGestureEvent(MainFrameImpl()->GetFrameView(), event);
      // Plugins may need to see GestureFlingStart to balance
      // GestureScrollBegin (since the former replaces GestureScrollEnd when
      // transitioning to a fling).
      // TODO(dtapuska): Why isn't the response used?
      MainFrameImpl()->GetFrame()->GetEventHandler().HandleGestureScrollEvent(
          scaled_event);

      client_->DidHandleGestureEvent(event, event_cancelled);
      return WebInputEventResult::kHandledSystem;
    }
    case WebInputEvent::kGestureFlingCancel:
      if (EndActiveFlingAnimation())
        event_result = WebInputEventResult::kHandledSuppressed;

      client_->DidHandleGestureEvent(event, event_cancelled);
      return event_result;
    default:
      break;
  }

  WebGestureEvent scaled_event =
      TransformWebGestureEvent(MainFrameImpl()->GetFrameView(), event);

  // Special handling for double tap and scroll events as we don't want to
  // hit test for them.
  switch (event.GetType()) {
    case WebInputEvent::kGestureDoubleTap:
      if (web_settings_->DoubleTapToZoomEnabled() &&
          MinimumPageScaleFactor() != MaximumPageScaleFactor()) {
        AnimateDoubleTapZoom(
            FlooredIntPoint(scaled_event.PositionInRootFrame()));
      }
      // GestureDoubleTap is currently only used by Android for zooming. For
      // WebCore, GestureTap with tap count = 2 is used instead. So we drop
      // GestureDoubleTap here.
      event_result = WebInputEventResult::kHandledSystem;
      client_->DidHandleGestureEvent(event, event_cancelled);
      return event_result;
    case WebInputEvent::kGestureScrollBegin:
    case WebInputEvent::kGestureScrollEnd:
    case WebInputEvent::kGestureScrollUpdate:
    case WebInputEvent::kGestureFlingStart:
      // Scrolling-related gesture events invoke EventHandler recursively for
      // each frame down the chain, doing a single-frame hit-test per frame.
      // This matches handleWheelEvent.  Perhaps we could simplify things by
      // rewriting scroll handling to work inner frame out, and then unify with
      // other gesture events.
      event_result = MainFrameImpl()
                         ->GetFrame()
                         ->GetEventHandler()
                         .HandleGestureScrollEvent(scaled_event);
      client_->DidHandleGestureEvent(event, event_cancelled);
      return event_result;
    case WebInputEvent::kGesturePinchBegin:
    case WebInputEvent::kGesturePinchEnd:
    case WebInputEvent::kGesturePinchUpdate:
      return WebInputEventResult::kNotHandled;
    default:
      break;
  }

  // Hit test across all frames and do touch adjustment as necessary for the
  // event type.
  GestureEventWithHitTestResults targeted_event =
      page_->DeprecatedLocalMainFrame()->GetEventHandler().TargetGestureEvent(
          scaled_event);

  // Handle link highlighting outside the main switch to avoid getting lost in
  // the complicated set of cases handled below.
  switch (event.GetType()) {
    case WebInputEvent::kGestureShowPress:
      // Queue a highlight animation, then hand off to regular handler.
      EnableTapHighlightAtPoint(targeted_event);
      break;
    case WebInputEvent::kGestureTapCancel:
    case WebInputEvent::kGestureTap:
    case WebInputEvent::kGestureLongPress:
      for (size_t i = 0; i < link_highlights_.size(); ++i)
        link_highlights_[i]->StartHighlightAnimationIfNeeded();
      break;
    default:
      break;
  }

  switch (event.GetType()) {
    case WebInputEvent::kGestureTap: {
      // Don't trigger a disambiguation popup on sites designed for mobile
      // devices.  Instead, assume that the page has been designed with big
      // enough buttons and links.  Don't trigger a disambiguation popup when
      // screencasting, since it's implemented outside of compositor pipeline
      // and is not being screencasted itself. This leads to bad user
      // experience.
      WebDevToolsAgentImpl* dev_tools = MainFrameDevToolsAgentImpl();
      VisualViewport& visual_viewport = GetPage()->GetVisualViewport();
      bool screencast_enabled = dev_tools && dev_tools->ScreencastEnabled();
      if (event.data.tap.width > 0 &&
          !visual_viewport.ShouldDisableDesktopWorkarounds() &&
          !screencast_enabled) {
        IntRect bounding_box(visual_viewport.ViewportToRootFrame(
            IntRect(event.x - event.data.tap.width / 2,
                    event.y - event.data.tap.height / 2, event.data.tap.width,
                    event.data.tap.height)));

        // TODO(bokan): We shouldn't pass details of the VisualViewport offset
        // to render_view_impl.  crbug.com/459591
        WebSize visual_viewport_offset =
            FlooredIntSize(visual_viewport.GetScrollOffset());

        if (web_settings_->MultiTargetTapNotificationEnabled()) {
          Vector<IntRect> good_targets;
          HeapVector<Member<Node>> highlight_nodes;
          FindGoodTouchTargets(bounding_box, MainFrameImpl()->GetFrame(),
                               good_targets, highlight_nodes);
          // FIXME: replace touch adjustment code when numberOfGoodTargets == 1?
          // Single candidate case is currently handled by:
          // https://bugs.webkit.org/show_bug.cgi?id=85101
          if (good_targets.size() >= 2 && client_ &&
              client_->DidTapMultipleTargets(visual_viewport_offset,
                                             bounding_box, good_targets)) {
            // Stash the position of the node that would've been used absent
            // disambiguation, for UMA purposes.
            last_tap_disambiguation_best_candidate_position_ =
                targeted_event.GetHitTestResult().RoundedPointInMainFrame() -
                RoundedIntSize(targeted_event.GetHitTestResult().LocalPoint());

            EnableTapHighlights(highlight_nodes);
            for (size_t i = 0; i < link_highlights_.size(); ++i)
              link_highlights_[i]->StartHighlightAnimationIfNeeded();
            event_result = WebInputEventResult::kHandledSystem;
            event_cancelled = true;
            break;
          }
        }
      }

      event_result =
          MainFrameImpl()->GetFrame()->GetEventHandler().HandleGestureEvent(
              targeted_event);
      if (page_popup_ && last_hidden_page_popup_ &&
          page_popup_->HasSamePopupClient(last_hidden_page_popup_.Get())) {
        // The tap triggered a page popup that is the same as the one we just
        // closed. It needs to be closed.
        CancelPagePopup();
      }
      last_hidden_page_popup_ = nullptr;
      break;
    }
    case WebInputEvent::kGestureTwoFingerTap:
    case WebInputEvent::kGestureLongPress:
    case WebInputEvent::kGestureLongTap: {
      if (!MainFrameImpl() || !MainFrameImpl()->GetFrameView())
        break;

      page_->GetContextMenuController().ClearContextMenu();
      {
        ContextMenuAllowedScope scope;
        event_result =
            MainFrameImpl()->GetFrame()->GetEventHandler().HandleGestureEvent(
                targeted_event);
      }

      break;
    }
    case WebInputEvent::kGestureTapDown: {
      // Touch pinch zoom and scroll on the page (outside of a popup) must hide
      // the popup. In case of a touch scroll or pinch zoom, this function is
      // called with GestureTapDown rather than a GSB/GSU/GSE or GPB/GPU/GPE.
      // When we close a popup because of a GestureTapDown, we also save it so
      // we can prevent the following GestureTap from immediately reopening the
      // same popup.
      last_hidden_page_popup_ = page_popup_;
      HidePopups();
      DCHECK(!page_popup_);
      event_result =
          MainFrameImpl()->GetFrame()->GetEventHandler().HandleGestureEvent(
              targeted_event);
      break;
    }
    case WebInputEvent::kGestureTapCancel: {
      last_hidden_page_popup_ = nullptr;
      event_result =
          MainFrameImpl()->GetFrame()->GetEventHandler().HandleGestureEvent(
              targeted_event);
      break;
    }
    case WebInputEvent::kGestureShowPress: {
      event_result =
          MainFrameImpl()->GetFrame()->GetEventHandler().HandleGestureEvent(
              targeted_event);
      break;
    }
    case WebInputEvent::kGestureTapUnconfirmed: {
      event_result =
          MainFrameImpl()->GetFrame()->GetEventHandler().HandleGestureEvent(
              targeted_event);
      break;
    }
    default: { NOTREACHED(); }
  }
  client_->DidHandleGestureEvent(event, event_cancelled);
  return event_result;
}

namespace {
// This enum is used to back a histogram, and should therefore be treated as
// append-only.
enum TapDisambiguationResult {
  kUmaTapDisambiguationOther = 0,
  kUmaTapDisambiguationBackButton = 1,
  kUmaTapDisambiguationTappedOutside = 2,
  kUmaTapDisambiguationTappedInsideDeprecated = 3,
  kUmaTapDisambiguationTappedInsideSameNode = 4,
  kUmaTapDisambiguationTappedInsideDifferentNode = 5,
  kUmaTapDisambiguationCount = 6,
};

void RecordTapDisambiguation(TapDisambiguationResult result) {
  UMA_HISTOGRAM_ENUMERATION("Touchscreen.TapDisambiguation", result,
                            kUmaTapDisambiguationCount);
}

}  // namespace

void WebViewImpl::ResolveTapDisambiguation(double timestamp_seconds,
                                           WebPoint tap_viewport_offset,
                                           bool is_long_press) {
  WebGestureEvent event(is_long_press ? WebInputEvent::kGestureLongPress
                                      : WebInputEvent::kGestureTap,
                        WebInputEvent::kNoModifiers, timestamp_seconds);

  event.x = tap_viewport_offset.x;
  event.y = tap_viewport_offset.y;
  event.source_device = blink::kWebGestureDeviceTouchscreen;

  {
    // Compute UMA stat about whether the node selected by disambiguation UI was
    // different from the one preferred by the regular hit-testing + adjustment
    // logic.
    WebGestureEvent scaled_event =
        TransformWebGestureEvent(MainFrameImpl()->GetFrameView(), event);
    GestureEventWithHitTestResults targeted_event =
        page_->DeprecatedLocalMainFrame()->GetEventHandler().TargetGestureEvent(
            scaled_event);
    WebPoint node_position =
        targeted_event.GetHitTestResult().RoundedPointInMainFrame() -
        RoundedIntSize(targeted_event.GetHitTestResult().LocalPoint());
    TapDisambiguationResult result =
        (node_position == last_tap_disambiguation_best_candidate_position_)
            ? kUmaTapDisambiguationTappedInsideSameNode
            : kUmaTapDisambiguationTappedInsideDifferentNode;
    RecordTapDisambiguation(result);
  }

  HandleGestureEvent(event);
}

WebInputEventResult WebViewImpl::HandleSyntheticWheelFromTouchpadPinchEvent(
    const WebGestureEvent& pinch_event) {
  DCHECK_EQ(pinch_event.GetType(), WebInputEvent::kGesturePinchUpdate);

  // For pinch gesture events, match typical trackpad behavior on Windows by
  // sending fake wheel events with the ctrl modifier set when we see trackpad
  // pinch gestures.  Ideally we'd someday get a platform 'pinch' event and
  // send that instead.
  WebMouseWheelEvent wheel_event(
      WebInputEvent::kMouseWheel,
      pinch_event.GetModifiers() | WebInputEvent::kControlKey,
      pinch_event.TimeStampSeconds());
  wheel_event.SetPositionInWidget(pinch_event.x, pinch_event.y);
  wheel_event.SetPositionInScreen(pinch_event.global_x, pinch_event.global_y);
  wheel_event.delta_x = 0;

  // The function to convert scales to deltaY values is designed to be
  // compatible with websites existing use of wheel events, and with existing
  // Windows trackpad behavior.  In particular, we want:
  //  - deltas should accumulate via addition: f(s1*s2)==f(s1)+f(s2)
  //  - deltas should invert via negation: f(1/s) == -f(s)
  //  - zoom in should be positive: f(s) > 0 iff s > 1
  //  - magnitude roughly matches wheels: f(2) > 25 && f(2) < 100
  //  - a formula that's relatively easy to use from JavaScript
  // Note that 'wheel' event deltaY values have their sign inverted.  So to
  // convert a wheel deltaY back to a scale use Math.exp(-deltaY/100).
  DCHECK_GT(pinch_event.data.pinch_update.scale, 0);
  wheel_event.delta_y = 100.0f * log(pinch_event.data.pinch_update.scale);
  wheel_event.has_precise_scrolling_deltas = true;
  wheel_event.wheel_ticks_x = 0;
  wheel_event.wheel_ticks_y = pinch_event.data.pinch_update.scale > 1 ? 1 : -1;

  return HandleInputEvent(blink::WebCoalescedInputEvent(wheel_event));
}

void WebViewImpl::TransferActiveWheelFlingAnimation(
    const WebActiveWheelFlingParameters& parameters) {
  TRACE_EVENT0("blink", "WebViewImpl::transferActiveWheelFlingAnimation");
  DCHECK(!gesture_animation_);
  position_on_fling_start_ = parameters.point;
  global_position_on_fling_start_ = parameters.global_point;
  fling_modifier_ = parameters.modifiers;
  std::unique_ptr<WebGestureCurve> curve =
      Platform::Current()->CreateFlingAnimationCurve(
          parameters.source_device, WebFloatPoint(parameters.delta),
          parameters.cumulative_scroll);
  DCHECK(curve);
  gesture_animation_ = WebActiveGestureAnimation::CreateWithTimeOffset(
      std::move(curve), this, parameters.start_time);
  DCHECK_NE(parameters.source_device, kWebGestureDeviceUninitialized);
  fling_source_device_ = parameters.source_device;
  MainFrameImpl()->FrameWidget()->ScheduleAnimation();
}

bool WebViewImpl::EndActiveFlingAnimation() {
  if (gesture_animation_) {
    gesture_animation_.reset();
    fling_source_device_ = kWebGestureDeviceUninitialized;
    if (layer_tree_view_)
      layer_tree_view_->DidStopFlinging();
    return true;
  }
  return false;
}

bool WebViewImpl::StartPageScaleAnimation(const IntPoint& target_position,
                                          bool use_anchor,
                                          float new_scale,
                                          double duration_in_seconds) {
  VisualViewport& visual_viewport = GetPage()->GetVisualViewport();
  WebPoint clamped_point = target_position;
  if (!use_anchor) {
    clamped_point =
        visual_viewport.ClampDocumentOffsetAtScale(target_position, new_scale);
    if (!duration_in_seconds) {
      SetPageScaleFactor(new_scale);

      FrameView* view = MainFrameImpl()->GetFrameView();
      if (view && view->GetScrollableArea()) {
        view->GetScrollableArea()->SetScrollOffset(
            ScrollOffset(clamped_point.x, clamped_point.y),
            kProgrammaticScroll);
      }

      return false;
    }
  }
  if (use_anchor && new_scale == PageScaleFactor())
    return false;

  if (enable_fake_page_scale_animation_for_testing_) {
    fake_page_scale_animation_target_position_ = target_position;
    fake_page_scale_animation_use_anchor_ = use_anchor;
    fake_page_scale_animation_page_scale_factor_ = new_scale;
  } else {
    if (!layer_tree_view_)
      return false;
    layer_tree_view_->StartPageScaleAnimation(target_position, use_anchor,
                                              new_scale, duration_in_seconds);
  }
  return true;
}

void WebViewImpl::EnableFakePageScaleAnimationForTesting(bool enable) {
  enable_fake_page_scale_animation_for_testing_ = enable;
}

void WebViewImpl::SetShowFPSCounter(bool show) {
  if (layer_tree_view_) {
    TRACE_EVENT0("blink", "WebViewImpl::setShowFPSCounter");
    layer_tree_view_->SetShowFPSCounter(show);
  }
}

void WebViewImpl::SetShowPaintRects(bool show) {
  if (layer_tree_view_) {
    TRACE_EVENT0("blink", "WebViewImpl::setShowPaintRects");
    layer_tree_view_->SetShowPaintRects(show);
  }
  FirstPaintInvalidationTracking::SetEnabledForShowPaintRects(show);
}

void WebViewImpl::SetShowDebugBorders(bool show) {
  if (layer_tree_view_)
    layer_tree_view_->SetShowDebugBorders(show);
}

void WebViewImpl::SetShowScrollBottleneckRects(bool show) {
  if (layer_tree_view_)
    layer_tree_view_->SetShowScrollBottleneckRects(show);
}

void WebViewImpl::AcceptLanguagesChanged() {
  if (client_)
    FontCache::AcceptLanguagesChanged(client_->AcceptLanguages());

  if (!GetPage())
    return;

  GetPage()->AcceptLanguagesChanged();
}

void WebViewImpl::ReportIntervention(const WebString& message) {
  if (!MainFrameImpl())
    return;
  WebConsoleMessage console_message(WebConsoleMessage::kLevelWarning, message);
  MainFrameImpl()->AddMessageToConsole(console_message);
}

float WebViewImpl::ExpensiveBackgroundThrottlingCPUBudget() {
  return SettingsImpl()->ExpensiveBackgroundThrottlingCPUBudget();
}

float WebViewImpl::ExpensiveBackgroundThrottlingInitialBudget() {
  return SettingsImpl()->ExpensiveBackgroundThrottlingInitialBudget();
}

float WebViewImpl::ExpensiveBackgroundThrottlingMaxBudget() {
  return SettingsImpl()->ExpensiveBackgroundThrottlingMaxBudget();
}

float WebViewImpl::ExpensiveBackgroundThrottlingMaxDelay() {
  return SettingsImpl()->ExpensiveBackgroundThrottlingMaxDelay();
}

WebInputEventResult WebViewImpl::HandleKeyEvent(const WebKeyboardEvent& event) {
  DCHECK((event.GetType() == WebInputEvent::kRawKeyDown) ||
         (event.GetType() == WebInputEvent::kKeyDown) ||
         (event.GetType() == WebInputEvent::kKeyUp));
  TRACE_EVENT2("input", "WebViewImpl::handleKeyEvent", "type",
               WebInputEvent::GetName(event.GetType()), "text",
               String(event.text).Utf8());

  // Halt an in-progress fling on a key event.
  EndActiveFlingAnimation();

  // Please refer to the comments explaining the m_suppressNextKeypressEvent
  // member.
  // The m_suppressNextKeypressEvent is set if the KeyDown is handled by
  // Webkit. A keyDown event is typically associated with a keyPress(char)
  // event and a keyUp event. We reset this flag here as this is a new keyDown
  // event.
  suppress_next_keypress_event_ = false;

  // If there is a popup, it should be the one processing the event, not the
  // page.
  if (page_popup_) {
    page_popup_->HandleKeyEvent(event);
    // We need to ignore the next Char event after this otherwise pressing
    // enter when selecting an item in the popup will go to the page.
    if (WebInputEvent::kRawKeyDown == event.GetType())
      suppress_next_keypress_event_ = true;
    return WebInputEventResult::kHandledSystem;
  }

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
      if (element && element->GetLayoutObject() &&
          element->GetLayoutObject()->IsEmbeddedObject()) {
        if (event.windows_key_code == VKEY_TAB) {
          // If the plugin supports keyboard focus then we should not send a tab
          // keypress event.
          PluginView* plugin_view =
              ToLayoutPart(element->GetLayoutObject())->Plugin();
          if (plugin_view && plugin_view->IsPluginContainer()) {
            WebPluginContainerBase* plugin =
                ToWebPluginContainerBase(plugin_view);
            if (plugin && plugin->SupportsKeyboardFocus())
              suppress_next_keypress_event_ = true;
          }
        }
      } else {
        suppress_next_keypress_event_ = true;
      }
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
    SendContextMenuEvent(event);
    return WebInputEventResult::kHandledSystem;
  }
#endif  // !OS(MACOSX)

  return WebInputEventResult::kNotHandled;
}

WebInputEventResult WebViewImpl::HandleCharEvent(
    const WebKeyboardEvent& event) {
  DCHECK_EQ(event.GetType(), WebInputEvent::kChar);
  TRACE_EVENT1("input", "WebViewImpl::handleCharEvent", "text",
               String(event.text).Utf8());

  // Please refer to the comments explaining the m_suppressNextKeypressEvent
  // member.  The m_suppressNextKeypressEvent is set if the KeyDown is
  // handled by Webkit. A keyDown event is typically associated with a
  // keyPress(char) event and a keyUp event. We reset this flag here as it
  // only applies to the current keyPress event.
  bool suppress = suppress_next_keypress_event_;
  suppress_next_keypress_event_ = false;

  // If there is a popup, it should be the one processing the event, not the
  // page.
  if (page_popup_)
    return page_popup_->HandleKeyEvent(event);

  LocalFrame* frame = ToLocalFrame(FocusedCoreFrame());
  if (!frame)
    return suppress ? WebInputEventResult::kHandledSuppressed
                    : WebInputEventResult::kNotHandled;

  EventHandler& handler = frame->GetEventHandler();

  if (!event.IsCharacterKey())
    return WebInputEventResult::kHandledSuppressed;

  // Accesskeys are triggered by char events and can't be suppressed.
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

WebRect WebViewImpl::ComputeBlockBound(const WebPoint& point_in_root_frame,
                                       bool ignore_clipping) {
  if (!MainFrameImpl())
    return WebRect();

  // Use the point-based hit test to find the node.
  IntPoint point = MainFrameImpl()->GetFrameView()->RootFrameToContents(
      IntPoint(point_in_root_frame.x, point_in_root_frame.y));
  HitTestRequest::HitTestRequestType hit_type =
      HitTestRequest::kReadOnly | HitTestRequest::kActive |
      (ignore_clipping ? HitTestRequest::kIgnoreClipping : 0);
  HitTestResult result =
      MainFrameImpl()->GetFrame()->GetEventHandler().HitTestResultAtPoint(
          point, hit_type);
  result.SetToShadowHostIfInRestrictedShadowRoot();

  Node* node = result.InnerNodeOrImageMapImage();
  if (!node)
    return WebRect();

  // Find the block type node based on the hit node.
  // FIXME: This wants to walk flat tree with
  // LayoutTreeBuilderTraversal::parent().
  while (node &&
         (!node->GetLayoutObject() || node->GetLayoutObject()->IsInline()))
    node = LayoutTreeBuilderTraversal::Parent(*node);

  // Return the bounding box in the root frame's coordinate space.
  if (node) {
    IntRect point_in_root_frame = node->Node::PixelSnappedBoundingBox();
    LocalFrame* frame = node->GetDocument().GetFrame();
    return frame->View()->ContentsToRootFrame(point_in_root_frame);
  }
  return WebRect();
}

WebRect WebViewImpl::WidenRectWithinPageBounds(const WebRect& source,
                                               int target_margin,
                                               int minimum_margin) {
  WebSize max_size;
  if (MainFrame())
    max_size = MainFrame()->ContentsSize();
  IntSize scroll_offset;
  if (MainFrame())
    scroll_offset = MainFrame()->GetScrollOffset();
  int left_margin = target_margin;
  int right_margin = target_margin;

  const int absolute_source_x = source.x + scroll_offset.Width();
  if (left_margin > absolute_source_x) {
    left_margin = absolute_source_x;
    right_margin = std::max(left_margin, minimum_margin);
  }

  const int maximum_right_margin =
      max_size.width - (source.width + absolute_source_x);
  if (right_margin > maximum_right_margin) {
    right_margin = maximum_right_margin;
    left_margin = std::min(left_margin, std::max(right_margin, minimum_margin));
  }

  const int new_width = source.width + left_margin + right_margin;
  const int new_x = source.x - left_margin;

  DCHECK_GE(new_width, 0);
  DCHECK_LE(scroll_offset.Width() + new_x + new_width, max_size.width);

  return WebRect(new_x, source.y, new_width, source.height);
}

float WebViewImpl::MaximumLegiblePageScale() const {
  // Pages should be as legible as on desktop when at dpi scale, so no
  // need to zoom in further when automatically determining zoom level
  // (after double tap, find in page, etc), though the user should still
  // be allowed to manually pinch zoom in further if they desire.
  if (GetPage()) {
    return maximum_legible_scale_ *
           GetPage()->GetSettings().GetAccessibilityFontScaleFactor();
  }
  return maximum_legible_scale_;
}

void WebViewImpl::ComputeScaleAndScrollForBlockRect(
    const WebPoint& hit_point_in_root_frame,
    const WebRect& block_rect_in_root_frame,
    float padding,
    float default_scale_when_already_legible,
    float& scale,
    WebPoint& scroll) {
  scale = PageScaleFactor();
  scroll.x = scroll.y = 0;

  WebRect rect = block_rect_in_root_frame;

  if (!rect.IsEmpty()) {
    float default_margin = doubleTapZoomContentDefaultMargin;
    float minimum_margin = doubleTapZoomContentMinimumMargin;
    // We want the margins to have the same physical size, which means we
    // need to express them in post-scale size. To do that we'd need to know
    // the scale we're scaling to, but that depends on the margins. Instead
    // we express them as a fraction of the target rectangle: this will be
    // correct if we end up fully zooming to it, and won't matter if we
    // don't.
    rect = WidenRectWithinPageBounds(
        rect, static_cast<int>(default_margin * rect.width / size_.width),
        static_cast<int>(minimum_margin * rect.width / size_.width));
    // Fit block to screen, respecting limits.
    scale = static_cast<float>(size_.width) / rect.width;
    scale = std::min(scale, MaximumLegiblePageScale());
    if (PageScaleFactor() < default_scale_when_already_legible)
      scale = std::max(scale, default_scale_when_already_legible);
    scale = ClampPageScaleFactorToLimits(scale);
  }

  // FIXME: If this is being called for auto zoom during find in page,
  // then if the user manually zooms in it'd be nice to preserve the
  // relative increase in zoom they caused (if they zoom out then it's ok
  // to zoom them back in again). This isn't compatible with our current
  // double-tap zoom strategy (fitting the containing block to the screen)
  // though.

  float screen_width = size_.width / scale;
  float screen_height = size_.height / scale;

  // Scroll to vertically align the block.
  if (rect.height < screen_height) {
    // Vertically center short blocks.
    rect.y -= 0.5 * (screen_height - rect.height);
  } else {
    // Ensure position we're zooming to (+ padding) isn't off the bottom of
    // the screen.
    rect.y = std::max<float>(
        rect.y, hit_point_in_root_frame.y + padding - screen_height);
  }  // Otherwise top align the block.

  // Do the same thing for horizontal alignment.
  if (rect.width < screen_width)
    rect.x -= 0.5 * (screen_width - rect.width);
  else
    rect.x = std::max<float>(
        rect.x, hit_point_in_root_frame.x + padding - screen_width);
  scroll.x = rect.x;
  scroll.y = rect.y;

  scale = ClampPageScaleFactorToLimits(scale);
  scroll = MainFrameImpl()->GetFrameView()->RootFrameToContents(scroll);
  scroll =
      GetPage()->GetVisualViewport().ClampDocumentOffsetAtScale(scroll, scale);
}

static Node* FindCursorDefiningAncestor(Node* node, LocalFrame* frame) {
  // Go up the tree to find the node that defines a mouse cursor style
  while (node) {
    if (node->GetLayoutObject()) {
      ECursor cursor = node->GetLayoutObject()->Style()->Cursor();
      if (cursor != ECursor::kAuto ||
          frame->GetEventHandler().UseHandCursor(node, node->IsLink()))
        break;
    }
    node = LayoutTreeBuilderTraversal::Parent(*node);
  }

  return node;
}

static bool ShowsHandCursor(Node* node, LocalFrame* frame) {
  if (!node || !node->GetLayoutObject())
    return false;

  ECursor cursor = node->GetLayoutObject()->Style()->Cursor();
  return cursor == ECursor::kPointer ||
         (cursor == ECursor::kAuto &&
          frame->GetEventHandler().UseHandCursor(node, node->IsLink()));
}

Node* WebViewImpl::BestTapNode(
    const GestureEventWithHitTestResults& targeted_tap_event) {
  TRACE_EVENT0("input", "WebViewImpl::bestTapNode");

  if (!page_ || !page_->MainFrame())
    return nullptr;

  Node* best_touch_node = targeted_tap_event.GetHitTestResult().InnerNode();
  if (!best_touch_node)
    return nullptr;

  // We might hit something like an image map that has no layoutObject on it
  // Walk up the tree until we have a node with an attached layoutObject
  while (!best_touch_node->GetLayoutObject()) {
    best_touch_node = LayoutTreeBuilderTraversal::Parent(*best_touch_node);
    if (!best_touch_node)
      return nullptr;
  }

  // Editable nodes should not be highlighted (e.g., <input>)
  if (HasEditableStyle(*best_touch_node))
    return nullptr;

  Node* cursor_defining_ancestor = FindCursorDefiningAncestor(
      best_touch_node, page_->DeprecatedLocalMainFrame());
  // We show a highlight on tap only when the current node shows a hand cursor
  if (!cursor_defining_ancestor ||
      !ShowsHandCursor(cursor_defining_ancestor,
                       page_->DeprecatedLocalMainFrame())) {
    return nullptr;
  }

  // We should pick the largest enclosing node with hand cursor set. We do this
  // by first jumping up to cursorDefiningAncestor (which is already known to
  // have hand cursor set). Then we locate the next cursor-defining ancestor up
  // in the the tree and repeat the jumps as long as the node has hand cursor
  // set.
  do {
    best_touch_node = cursor_defining_ancestor;
    cursor_defining_ancestor = FindCursorDefiningAncestor(
        LayoutTreeBuilderTraversal::Parent(*best_touch_node),
        page_->DeprecatedLocalMainFrame());
  } while (cursor_defining_ancestor &&
           ShowsHandCursor(cursor_defining_ancestor,
                           page_->DeprecatedLocalMainFrame()));

  return best_touch_node;
}

void WebViewImpl::EnableTapHighlightAtPoint(
    const GestureEventWithHitTestResults& targeted_tap_event) {
  Node* touch_node = BestTapNode(targeted_tap_event);

  HeapVector<Member<Node>> highlight_nodes;
  highlight_nodes.push_back(touch_node);

  EnableTapHighlights(highlight_nodes);
}

void WebViewImpl::EnableTapHighlights(
    HeapVector<Member<Node>>& highlight_nodes) {
  if (highlight_nodes.IsEmpty())
    return;

  // Always clear any existing highlight when this is invoked, even if we
  // don't get a new target to highlight.
  link_highlights_.clear();

  for (size_t i = 0; i < highlight_nodes.size(); ++i) {
    Node* node = highlight_nodes[i];

    if (!node || !node->GetLayoutObject())
      continue;

    Color highlight_color =
        node->GetLayoutObject()->Style()->TapHighlightColor();
    // Safari documentation for -webkit-tap-highlight-color says if the
    // specified color has 0 alpha, then tap highlighting is disabled.
    // http://developer.apple.com/library/safari/#documentation/appleapplications/reference/safaricssref/articles/standardcssproperties.html
    if (!highlight_color.Alpha())
      continue;

    link_highlights_.push_back(LinkHighlightImpl::Create(node, this));
  }

  UpdateAllLifecyclePhases();
}

void WebViewImpl::AnimateDoubleTapZoom(const IntPoint& point_in_root_frame) {
  if (!MainFrameImpl())
    return;

  WebRect block_bounds = ComputeBlockBound(point_in_root_frame, false);
  float scale;
  WebPoint scroll;

  ComputeScaleAndScrollForBlockRect(
      point_in_root_frame, block_bounds, touchPointPadding,
      MinimumPageScaleFactor() * doubleTapZoomAlreadyLegibleRatio, scale,
      scroll);

  bool still_at_previous_double_tap_scale =
      (PageScaleFactor() == double_tap_zoom_page_scale_factor_ &&
       double_tap_zoom_page_scale_factor_ != MinimumPageScaleFactor()) ||
      double_tap_zoom_pending_;

  bool scale_unchanged = fabs(PageScaleFactor() - scale) < minScaleDifference;
  bool should_zoom_out = block_bounds.IsEmpty() || scale_unchanged ||
                         still_at_previous_double_tap_scale;

  bool is_animating;

  if (should_zoom_out) {
    scale = MinimumPageScaleFactor();
    IntPoint target_position =
        MainFrameImpl()->GetFrameView()->RootFrameToContents(
            point_in_root_frame);
    is_animating = StartPageScaleAnimation(
        target_position, true, scale, doubleTapZoomAnimationDurationInSeconds);
  } else {
    is_animating = StartPageScaleAnimation(
        scroll, false, scale, doubleTapZoomAnimationDurationInSeconds);
  }

  // TODO(dglazkov): The only reason why we're using isAnimating and not just
  // checking for m_layerTreeView->hasPendingPageScaleAnimation() is because of
  // fake page scale animation plumbing for testing, which doesn't actually
  // initiate a page scale animation.
  if (is_animating) {
    double_tap_zoom_page_scale_factor_ = scale;
    double_tap_zoom_pending_ = true;
  }
}

void WebViewImpl::ZoomToFindInPageRect(const WebRect& rect_in_root_frame) {
  if (!MainFrameImpl())
    return;

  WebRect block_bounds = ComputeBlockBound(
      WebPoint(rect_in_root_frame.x + rect_in_root_frame.width / 2,
               rect_in_root_frame.y + rect_in_root_frame.height / 2),
      true);

  if (block_bounds.IsEmpty()) {
    // Keep current scale (no need to scroll as x,y will normally already
    // be visible). FIXME: Revisit this if it isn't always true.
    return;
  }

  float scale;
  WebPoint scroll;

  ComputeScaleAndScrollForBlockRect(
      WebPoint(rect_in_root_frame.x, rect_in_root_frame.y), block_bounds,
      nonUserInitiatedPointPadding, MinimumPageScaleFactor(), scale, scroll);

  StartPageScaleAnimation(scroll, false, scale,
                          findInPageAnimationDurationInSeconds);
}

bool WebViewImpl::ZoomToMultipleTargetsRect(const WebRect& rect_in_root_frame) {
  if (!MainFrameImpl())
    return false;

  float scale;
  WebPoint scroll;

  ComputeScaleAndScrollForBlockRect(
      WebPoint(rect_in_root_frame.x, rect_in_root_frame.y), rect_in_root_frame,
      nonUserInitiatedPointPadding, MinimumPageScaleFactor(), scale, scroll);

  if (scale <= PageScaleFactor())
    return false;

  StartPageScaleAnimation(scroll, false, scale,
                          multipleTargetsZoomAnimationDurationInSeconds);
  return true;
}

bool WebViewImpl::HasTouchEventHandlersAt(const WebPoint& point) {
  // FIXME: Implement this. Note that the point must be divided by
  // pageScaleFactor.
  return true;
}

#if !OS(MACOSX)
// Mac has no way to open a context menu based on a keyboard event.
WebInputEventResult WebViewImpl::SendContextMenuEvent(
    const WebKeyboardEvent& event) {
  // The contextMenuController() holds onto the last context menu that was
  // popped up on the page until a new one is created. We need to clear
  // this menu before propagating the event through the DOM so that we can
  // detect if we create a new menu for this event, since we won't create
  // a new menu if the DOM swallows the event and the defaultEventHandler does
  // not run.
  GetPage()->GetContextMenuController().ClearContextMenu();

  {
    ContextMenuAllowedScope scope;
    Frame* focused_frame = GetPage()->GetFocusController().FocusedOrMainFrame();
    if (!focused_frame->IsLocalFrame())
      return WebInputEventResult::kNotHandled;
    // Firefox reveal focus based on "keydown" event but not "contextmenu"
    // event, we match FF.
    if (Element* focused_element =
            ToLocalFrame(focused_frame)->GetDocument()->FocusedElement())
      focused_element->scrollIntoViewIfNeeded();
    return ToLocalFrame(focused_frame)
        ->GetEventHandler()
        .SendContextMenuEventForKey(nullptr);
  }
}
#else
WebInputEventResult WebViewImpl::SendContextMenuEvent(
    const WebKeyboardEvent& event) {
  return WebInputEventResult::kNotHandled;
}
#endif

void WebViewImpl::ShowContextMenuAtPoint(float x,
                                         float y,
                                         ContextMenuProvider* menu_provider) {
  if (!GetPage()->MainFrame()->IsLocalFrame())
    return;
  {
    ContextMenuAllowedScope scope;
    GetPage()->GetContextMenuController().ClearContextMenu();
    GetPage()->GetContextMenuController().ShowContextMenuAtPoint(
        GetPage()->DeprecatedLocalMainFrame(), x, y, menu_provider);
  }
}

void WebViewImpl::ShowContextMenuForElement(WebElement element) {
  if (!GetPage())
    return;

  GetPage()->GetContextMenuController().ClearContextMenu();
  {
    ContextMenuAllowedScope scope;
    if (LocalFrame* focused_frame =
            ToLocalFrame(GetPage()->GetFocusController().FocusedOrMainFrame()))
      focused_frame->GetEventHandler().SendContextMenuEventForKey(
          element.Unwrap<Element>());
  }
}

PagePopup* WebViewImpl::OpenPagePopup(PagePopupClient* client) {
  DCHECK(client);
  if (HasOpenedPopup())
    HidePopups();
  DCHECK(!page_popup_);

  WebWidget* popup_widget = client_->CreatePopupMenu(kWebPopupTypePage);
  // createPopupMenu returns nullptr if this renderer process is about to die.
  if (!popup_widget)
    return nullptr;
  page_popup_ = ToWebPagePopupImpl(popup_widget);
  if (!page_popup_->Initialize(this, client)) {
    page_popup_->ClosePopup();
    page_popup_ = nullptr;
  }
  EnablePopupMouseWheelEventListener(WebLocalFrameImpl::FromFrame(
      client->OwnerElement().GetDocument().GetFrame()->LocalFrameRoot()));
  return page_popup_.Get();
}

void WebViewImpl::ClosePagePopup(PagePopup* popup) {
  DCHECK(popup);
  WebPagePopupImpl* popup_impl = ToWebPagePopupImpl(popup);
  DCHECK_EQ(page_popup_.Get(), popup_impl);
  if (page_popup_.Get() != popup_impl)
    return;
  page_popup_->ClosePopup();
}

void WebViewImpl::CleanupPagePopup() {
  page_popup_ = nullptr;
  DisablePopupMouseWheelEventListener();
}

void WebViewImpl::CancelPagePopup() {
  if (page_popup_)
    page_popup_->Cancel();
}

void WebViewImpl::EnablePopupMouseWheelEventListener(
    WebLocalFrameImpl* local_root) {
  DCHECK(!popup_mouse_wheel_event_listener_);
  Document* document = local_root->GetDocument();
  DCHECK(document);
  // We register an empty event listener, EmptyEventListener, so that mouse
  // wheel events get sent to the WebView.
  popup_mouse_wheel_event_listener_ = EmptyEventListener::Create();
  document->addEventListener(EventTypeNames::mousewheel,
                             popup_mouse_wheel_event_listener_, false);
  local_root_with_empty_mouse_wheel_listener_ = local_root;
}

void WebViewImpl::DisablePopupMouseWheelEventListener() {
  // TODO(kenrb): Concerns the same as in enablePopupMouseWheelEventListener.
  // See https://crbug.com/566130
  DCHECK(popup_mouse_wheel_event_listener_);
  Document* document =
      local_root_with_empty_mouse_wheel_listener_->GetDocument();
  DCHECK(document);
  // Document may have already removed the event listener, for instance, due
  // to a navigation, but remove it anyway.
  document->removeEventListener(EventTypeNames::mousewheel,
                                popup_mouse_wheel_event_listener_.Release(),
                                false);
  local_root_with_empty_mouse_wheel_listener_ = nullptr;
}

LocalDOMWindow* WebViewImpl::PagePopupWindow() const {
  return page_popup_ ? page_popup_->Window() : nullptr;
}

Frame* WebViewImpl::FocusedCoreFrame() const {
  return page_ ? page_->GetFocusController().FocusedOrMainFrame() : nullptr;
}

WebViewBase* WebViewBase::FromPage(Page* page) {
  return WebViewImpl::FromPage(page);
}

WebViewBase* WebViewImpl::FromPage(Page* page) {
  return page ? static_cast<WebViewBase*>(page->GetChromeClient().WebView())
              : nullptr;
}

// WebWidget ------------------------------------------------------------------

void WebViewImpl::Close() {
  DCHECK(AllInstances().Contains(this));
  AllInstances().erase(this);

  if (page_) {
    // Initiate shutdown for the entire frameset.  This will cause a lot of
    // notifications to be sent.
    page_->WillBeDestroyed();
    page_.Clear();
  }

  // Reset the delegate to prevent notifications being sent as we're being
  // deleted.
  client_ = nullptr;

  Deref();  // Balances ref() acquired in WebView::create
}

WebSize WebViewImpl::Size() {
  return size_;
}

void WebViewImpl::ResizeVisualViewport(const WebSize& new_size) {
  GetPage()->GetVisualViewport().SetSize(new_size);
  GetPage()->GetVisualViewport().ClampToBoundaries();
}

void WebViewImpl::UpdateICBAndResizeViewport() {
  // We'll keep the initial containing block size from changing when the top
  // controls hide so that the ICB will always be the same size as the
  // viewport with the browser controls shown.
  IntSize icb_size = size_;
  if (RuntimeEnabledFeatures::inertTopControlsEnabled() &&
      GetBrowserControls().PermittedState() == kWebBrowserControlsBoth &&
      !GetBrowserControls().ShrinkViewport())
    icb_size.Expand(0, -GetBrowserControls().Height());

  GetPageScaleConstraintsSet().DidChangeInitialContainingBlockSize(icb_size);

  UpdatePageDefinedViewportConstraints(
      MainFrameImpl()->GetFrame()->GetDocument()->GetViewportDescription());
  UpdateMainFrameLayoutSize();

  GetPage()->GetVisualViewport().SetSize(size_);

  if (MainFrameImpl()->GetFrameView()) {
    MainFrameImpl()->GetFrameView()->SetInitialViewportSize(icb_size);
    if (!MainFrameImpl()->GetFrameView()->NeedsLayout())
      resize_viewport_anchor_->ResizeFrameView(MainFrameSize());
  }
}

void WebViewImpl::UpdateBrowserControlsState(WebBrowserControlsState constraint,
                                             WebBrowserControlsState current,
                                             bool animate) {
  WebBrowserControlsState old_permitted_state =
      GetBrowserControls().PermittedState();

  GetBrowserControls().UpdateConstraintsAndState(constraint, current, animate);

  // If the controls are going from a locked hidden to unlocked state, or vice
  // versa, the ICB size needs to change but we can't rely on getting a
  // WebViewImpl::resize since the top controls shown state may not have
  // changed.
  if ((old_permitted_state == kWebBrowserControlsHidden &&
       constraint == kWebBrowserControlsBoth) ||
      (old_permitted_state == kWebBrowserControlsBoth &&
       constraint == kWebBrowserControlsHidden)) {
    UpdateICBAndResizeViewport();
  }

  if (layer_tree_view_)
    layer_tree_view_->UpdateBrowserControlsState(constraint, current, animate);
}

void WebViewImpl::DidUpdateBrowserControls() {
  if (layer_tree_view_) {
    layer_tree_view_->SetBrowserControlsShownRatio(
        GetBrowserControls().ShownRatio());
    layer_tree_view_->SetBrowserControlsHeight(
        GetBrowserControls().Height(), GetBrowserControls().ShrinkViewport());
  }

  WebLocalFrameImpl* main_frame = MainFrameImpl();
  if (!main_frame)
    return;

  FrameView* view = main_frame->GetFrameView();
  if (!view)
    return;

  VisualViewport& visual_viewport = GetPage()->GetVisualViewport();

  {
    // This object will save the current visual viewport offset w.r.t. the
    // document and restore it when the object goes out of scope. It's
    // needed since the browser controls adjustment will change the maximum
    // scroll offset and we may need to reposition them to keep the user's
    // apparent position unchanged.
    ResizeViewportAnchor::ResizeScope resize_scope(*resize_viewport_anchor_);

    float browser_controls_viewport_adjustment =
        GetBrowserControls().LayoutHeight() -
        GetBrowserControls().ContentOffset();
    visual_viewport.SetBrowserControlsAdjustment(
        browser_controls_viewport_adjustment);
  }
}

BrowserControls& WebViewImpl::GetBrowserControls() {
  return GetPage()->GetBrowserControls();
}

void WebViewImpl::ResizeViewWhileAnchored(float browser_controls_height,
                                          bool browser_controls_shrink_layout) {
  DCHECK(MainFrameImpl());

  GetBrowserControls().SetHeight(browser_controls_height,
                                 browser_controls_shrink_layout);

  {
    // Avoids unnecessary invalidations while various bits of state in
    // TextAutosizer are updated.
    TextAutosizer::DeferUpdatePageInfo defer_update_page_info(GetPage());
    FrameView* frame_view = MainFrameImpl()->GetFrameView();
    IntRect old_rect = frame_view->FrameRect();
    UpdateICBAndResizeViewport();
    IntRect new_rect = frame_view->FrameRect();
    frame_view->MarkViewportConstrainedObjectsForLayout(
        old_rect.Width() != new_rect.Width(),
        old_rect.Height() != new_rect.Height());
  }

  fullscreen_controller_->UpdateSize();

  // Update lifecyle phases immediately to recalculate the minimum scale limit
  // for rotation anchoring, and to make sure that no lifecycle states are
  // stale if this WebView is embedded in another one.
  UpdateAllLifecyclePhases();
}

void WebViewImpl::ResizeWithBrowserControls(
    const WebSize& new_size,
    float browser_controls_height,
    bool browser_controls_shrink_layout) {
  if (should_auto_resize_)
    return;

  if (size_ == new_size &&
      GetBrowserControls().Height() == browser_controls_height &&
      GetBrowserControls().ShrinkViewport() == browser_controls_shrink_layout)
    return;

  if (GetPage()->MainFrame() && !GetPage()->MainFrame()->IsLocalFrame()) {
    // Viewport resize for a remote main frame does not require any
    // particular action, but the state needs to reflect the correct size
    // so that it can be used for initalization if the main frame gets
    // swapped to a LocalFrame at a later time.
    size_ = new_size;
    GetPageScaleConstraintsSet().DidChangeInitialContainingBlockSize(size_);
    GetPage()->GetVisualViewport().SetSize(size_);
    return;
  }

  WebLocalFrameImpl* main_frame = MainFrameImpl();
  if (!main_frame)
    return;

  FrameView* view = main_frame->GetFrameView();
  if (!view)
    return;

  VisualViewport& visual_viewport = GetPage()->GetVisualViewport();

  bool is_rotation =
      GetPage()->GetSettings().GetMainFrameResizesAreOrientationChanges() &&
      size_.width && ContentsSize().Width() && new_size.width != size_.width &&
      !fullscreen_controller_->IsFullscreenOrTransitioning();
  size_ = new_size;

  FloatSize viewport_anchor_coords(viewportAnchorCoordX, viewportAnchorCoordY);
  if (is_rotation) {
    RotationViewportAnchor anchor(*view, visual_viewport,
                                  viewport_anchor_coords,
                                  GetPageScaleConstraintsSet());
    ResizeViewWhileAnchored(browser_controls_height,
                            browser_controls_shrink_layout);
  } else {
    ResizeViewportAnchor::ResizeScope resize_scope(*resize_viewport_anchor_);
    ResizeViewWhileAnchored(browser_controls_height,
                            browser_controls_shrink_layout);
  }
  SendResizeEventAndRepaint();
}

void WebViewImpl::Resize(const WebSize& new_size) {
  if (should_auto_resize_ || size_ == new_size)
    return;

  ResizeWithBrowserControls(new_size, GetBrowserControls().Height(),
                            GetBrowserControls().ShrinkViewport());
}

void WebViewImpl::DidEnterFullscreen() {
  fullscreen_controller_->DidEnterFullscreen();
}

void WebViewImpl::DidExitFullscreen() {
  fullscreen_controller_->DidExitFullscreen();
}

void WebViewImpl::DidUpdateFullscreenSize() {
  fullscreen_controller_->UpdateSize();
}

void WebViewImpl::SetSuppressFrameRequestsWorkaroundFor704763Only(
    bool suppress_frame_requests) {
  page_->Animator().SetSuppressFrameRequestsWorkaroundFor704763Only(
      suppress_frame_requests);
}
void WebViewImpl::BeginFrame(double last_frame_time_monotonic) {
  TRACE_EVENT1("blink", "WebViewImpl::beginFrame", "frameTime",
               last_frame_time_monotonic);
  DCHECK(last_frame_time_monotonic);

  // Create synthetic wheel events as necessary for fling.
  if (gesture_animation_) {
    if (gesture_animation_->Animate(last_frame_time_monotonic))
      MainFrameImpl()->FrameWidget()->ScheduleAnimation();
    else {
      DCHECK_NE(fling_source_device_, kWebGestureDeviceUninitialized);
      WebGestureDevice last_fling_source_device = fling_source_device_;
      EndActiveFlingAnimation();

      WebGestureEvent end_scroll_event = CreateGestureScrollEventFromFling(
          WebInputEvent::kGestureScrollEnd, last_fling_source_device);
      MainFrameImpl()->GetFrame()->GetEventHandler().HandleGestureScrollEnd(
          end_scroll_event);
    }
  }

  if (!MainFrameImpl())
    return;

  last_frame_time_monotonic_ = last_frame_time_monotonic;

  DocumentLifecycle::AllowThrottlingScope throttling_scope(
      MainFrameImpl()->GetFrame()->GetDocument()->Lifecycle());
  PageWidgetDelegate::Animate(*page_, last_frame_time_monotonic);
}

void WebViewImpl::UpdateAllLifecyclePhases() {
  TRACE_EVENT0("blink", "WebViewImpl::updateAllLifecyclePhases");
  if (!MainFrameImpl())
    return;

  DocumentLifecycle::AllowThrottlingScope throttling_scope(
      MainFrameImpl()->GetFrame()->GetDocument()->Lifecycle());
  UpdateLayerTreeBackgroundColor();

  PageWidgetDelegate::UpdateAllLifecyclePhases(*page_,
                                               *MainFrameImpl()->GetFrame());

  if (WebDevToolsAgentImpl* devtools = MainFrameDevToolsAgentImpl())
    devtools->PaintOverlay();
  if (page_color_overlay_)
    page_color_overlay_->GetGraphicsLayer()->Paint(nullptr);

  // TODO(chrishtr): link highlights don't currently paint themselves, it's
  // still driven by cc.  Fix this.
  for (size_t i = 0; i < link_highlights_.size(); ++i)
    link_highlights_[i]->UpdateGeometry();

  if (FrameView* view = MainFrameImpl()->GetFrameView()) {
    LocalFrame* frame = MainFrameImpl()->GetFrame();
    WebWidgetClient* client =
        WebLocalFrameImpl::FromFrame(frame)->FrameWidget()->Client();

    if (should_dispatch_first_visually_non_empty_layout_ &&
        view->IsVisuallyNonEmpty()) {
      should_dispatch_first_visually_non_empty_layout_ = false;
      // TODO(esprehn): Move users of this callback to something
      // better, the heuristic for "visually non-empty" is bad.
      client->DidMeaningfulLayout(WebMeaningfulLayout::kVisuallyNonEmpty);
    }

    if (should_dispatch_first_layout_after_finished_parsing_ &&
        frame->GetDocument()->HasFinishedParsing()) {
      should_dispatch_first_layout_after_finished_parsing_ = false;
      client->DidMeaningfulLayout(WebMeaningfulLayout::kFinishedParsing);
    }

    if (should_dispatch_first_layout_after_finished_loading_ &&
        frame->GetDocument()->IsLoadCompleted()) {
      should_dispatch_first_layout_after_finished_loading_ = false;
      client->DidMeaningfulLayout(WebMeaningfulLayout::kFinishedLoading);
    }
  }
}

void WebViewImpl::Paint(WebCanvas* canvas, const WebRect& rect) {
  // This should only be used when compositing is not being used for this
  // WebView, and it is painting into the recording of its parent.
  DCHECK(!IsAcceleratedCompositingActive());

  double paint_start = CurrentTime();
  PageWidgetDelegate::Paint(*page_, canvas, rect,
                            *page_->DeprecatedLocalMainFrame());
  double paint_end = CurrentTime();
  double pixels_per_sec =
      (rect.width * rect.height) / (paint_end - paint_start);
  DEFINE_STATIC_LOCAL(CustomCountHistogram, software_paint_duration_histogram,
                      ("Renderer4.SoftwarePaintDurationMS", 0, 120, 30));
  software_paint_duration_histogram.Count((paint_end - paint_start) * 1000);
  DEFINE_STATIC_LOCAL(CustomCountHistogram, software_paint_rate_histogram,
                      ("Renderer4.SoftwarePaintMegapixPerSecond", 10, 210, 30));
  software_paint_rate_histogram.Count(pixels_per_sec / 1000000);
}

#if OS(ANDROID)
void WebViewImpl::PaintIgnoringCompositing(WebCanvas* canvas,
                                           const WebRect& rect) {
  // This is called on a composited WebViewImpl, but we will ignore it,
  // producing all possible content of the WebViewImpl into the WebCanvas.
  DCHECK(IsAcceleratedCompositingActive());
  PageWidgetDelegate::PaintIgnoringCompositing(
      *page_, canvas, rect, *page_->DeprecatedLocalMainFrame());
}
#endif

void WebViewImpl::LayoutAndPaintAsync(
    WebLayoutAndPaintAsyncCallback* callback) {
  layer_tree_view_->LayoutAndPaintAsync(callback);
}

void WebViewImpl::CompositeAndReadbackAsync(
    WebCompositeAndReadbackAsyncCallback* callback) {
  layer_tree_view_->CompositeAndReadbackAsync(callback);
}

void WebViewImpl::ThemeChanged() {
  if (!GetPage())
    return;
  if (!GetPage()->MainFrame()->IsLocalFrame())
    return;
  FrameView* view = GetPage()->DeprecatedLocalMainFrame()->View();

  WebRect damaged_rect(0, 0, size_.width, size_.height);
  view->InvalidateRect(damaged_rect);
}

void WebViewImpl::EnterFullscreen(LocalFrame& frame) {
  fullscreen_controller_->EnterFullscreen(frame);
}

void WebViewImpl::ExitFullscreen(LocalFrame& frame) {
  fullscreen_controller_->ExitFullscreen(frame);
}

void WebViewImpl::FullscreenElementChanged(Element* from_element,
                                           Element* to_element) {
  fullscreen_controller_->FullscreenElementChanged(from_element, to_element);
}

bool WebViewImpl::HasHorizontalScrollbar() {
  return MainFrameImpl()
      ->GetFrameView()
      ->LayoutViewportScrollableArea()
      ->HorizontalScrollbar();
}

bool WebViewImpl::HasVerticalScrollbar() {
  return MainFrameImpl()
      ->GetFrameView()
      ->LayoutViewportScrollableArea()
      ->VerticalScrollbar();
}

const WebInputEvent* WebViewImpl::current_input_event_ = nullptr;

WebInputEventResult WebViewImpl::HandleInputEvent(
    const WebCoalescedInputEvent& coalesced_event) {
  const WebInputEvent& input_event = coalesced_event.Event();
  // TODO(dcheng): The fact that this is getting called when there is no local
  // main frame is problematic and probably indicates a bug in the input event
  // routing code.
  if (!MainFrameImpl())
    return WebInputEventResult::kNotHandled;

  GetPage()->GetVisualViewport().StartTrackingPinchStats();

  TRACE_EVENT1("input,rail", "WebViewImpl::handleInputEvent", "type",
               WebInputEvent::GetName(input_event.GetType()));

  // If a drag-and-drop operation is in progress, ignore input events.
  if (MainFrameImpl()->FrameWidget()->DoingDragAndDrop())
    return WebInputEventResult::kHandledSuppressed;

  if (dev_tools_emulator_->HandleInputEvent(input_event))
    return WebInputEventResult::kHandledSuppressed;

  if (WebDevToolsAgentImpl* devtools = MainFrameDevToolsAgentImpl()) {
    if (devtools->HandleInputEvent(input_event))
      return WebInputEventResult::kHandledSuppressed;
  }

  // Report the event to be NOT processed by WebKit, so that the browser can
  // handle it appropriately.
  if (WebFrameWidgetBase::IgnoreInputEvents())
    return WebInputEventResult::kNotHandled;

  AutoReset<const WebInputEvent*> current_event_change(&current_input_event_,
                                                       &input_event);
  UIEventWithKeyState::ClearNewTabModifierSetFromIsolatedWorld();

  bool is_pointer_locked = false;
  if (WebFrameWidgetBase* widget = MainFrameImpl()->FrameWidget()) {
    if (WebWidgetClient* client = widget->Client())
      is_pointer_locked = client->IsPointerLocked();
  }

  if (is_pointer_locked &&
      WebInputEvent::IsMouseEventType(input_event.GetType())) {
    MainFrameImpl()->FrameWidget()->PointerLockMouseEvent(input_event);
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
        TransformWebMouseEvent(MainFrameImpl()->GetFrameView(),
                               static_cast<const WebMouseEvent&>(input_event));
    node->DispatchMouseEvent(transformed_event, event_type,
                             transformed_event.click_count);
    return WebInputEventResult::kHandledSystem;
  }

  // FIXME: This should take in the intended frame, not the local frame root.
  WebInputEventResult result = PageWidgetDelegate::HandleInputEvent(
      *this, coalesced_event, MainFrameImpl()->GetFrame());
  if (result != WebInputEventResult::kNotHandled)
    return result;

  // Unhandled pinch events should adjust the scale.
  if (input_event.GetType() == WebInputEvent::kGesturePinchUpdate) {
    const WebGestureEvent& pinch_event =
        static_cast<const WebGestureEvent&>(input_event);

    // For touchpad gestures synthesize a Windows-like wheel event
    // to send to any handlers that may exist. Not necessary for touchscreen
    // as touch events would have already been sent for the gesture.
    if (pinch_event.source_device == kWebGestureDeviceTouchpad) {
      result = HandleSyntheticWheelFromTouchpadPinchEvent(pinch_event);
      if (result != WebInputEventResult::kNotHandled)
        return result;
    }

    if (pinch_event.data.pinch_update.zoom_disabled)
      return WebInputEventResult::kNotHandled;

    if (GetPage()->GetVisualViewport().MagnifyScaleAroundAnchor(
            pinch_event.data.pinch_update.scale,
            FloatPoint(pinch_event.x, pinch_event.y)))
      return WebInputEventResult::kHandledSystem;
  }

  return WebInputEventResult::kNotHandled;
}

void WebViewImpl::SetCursorVisibilityState(bool is_visible) {
  if (page_)
    page_->SetIsCursorVisible(is_visible);
}

void WebViewImpl::MouseCaptureLost() {
  TRACE_EVENT_ASYNC_END0("input", "capturing mouse", this);
  mouse_capture_node_ = nullptr;
}

void WebViewImpl::SetFocus(bool enable) {
  page_->GetFocusController().SetFocused(enable);
  if (enable) {
    page_->GetFocusController().SetActive(true);
    LocalFrame* focused_frame = page_->GetFocusController().FocusedFrame();
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
    HidePopups();

    // Clear focus on the currently focused frame if any.
    if (!page_)
      return;

    LocalFrame* frame = page_->MainFrame() && page_->MainFrame()->IsLocalFrame()
                            ? page_->DeprecatedLocalMainFrame()
                            : nullptr;
    if (!frame)
      return;

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

// TODO(ekaramad):This method is almost duplicated in WebFrameWidgetImpl as
// well. This code needs to be refactored  (http://crbug.com/629721).
WebRange WebViewImpl::CompositionRange() {
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

// TODO(ekaramad):This method is almost duplicated in WebFrameWidgetImpl as
// well. This code needs to be refactored  (http://crbug.com/629721).
bool WebViewImpl::SelectionBounds(WebRect& anchor, WebRect& focus) const {
  const Frame* frame = FocusedCoreFrame();
  if (!frame || !frame->IsLocalFrame())
    return false;

  const LocalFrame* local_frame = ToLocalFrame(frame);
  if (!local_frame)
    return false;
  FrameSelection& selection = local_frame->Selection();
  if (!selection.IsAvailable() || selection.GetSelectionInDOMTree().IsNone())
    return false;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  local_frame->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (selection.ComputeVisibleSelectionInDOMTree().IsNone()) {
    // plugins/mouse-capture-inside-shadow.html reaches here.
    return false;
  }

  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      local_frame->GetDocument()->Lifecycle());

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

  anchor = local_frame->View()->ContentsToViewport(anchor);
  focus = local_frame->View()->ContentsToViewport(focus);

  if (!selection.ComputeVisibleSelectionInDOMTree().IsBaseFirst())
    std::swap(anchor, focus);
  return true;
}

// TODO(ekaramad):This method is almost duplicated in WebFrameWidgetImpl as
// well. This code needs to be refactored  (http://crbug.com/629721).
WebPlugin* WebViewImpl::FocusedPluginIfInputMethodSupported(LocalFrame* frame) {
  WebPluginContainerBase* container =
      WebLocalFrameImpl::CurrentPluginContainer(frame);
  if (container && container->SupportsInputMethod())
    return container->Plugin();
  return nullptr;
}

// TODO(ekaramad):This method is almost duplicated in WebFrameWidgetImpl as
// well. This code needs to be refactored  (http://crbug.com/629721).
bool WebViewImpl::SelectionTextDirection(WebTextDirection& start,
                                         WebTextDirection& end) const {
  const LocalFrame* frame = FocusedLocalFrameInWidget();
  if (!frame)
    return false;

  const FrameSelection& selection = frame->Selection();
  if (!selection.IsAvailable()) {
    // plugins/mouse-capture-inside-shadow.html reaches here.
    return false;
  }

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  frame->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

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

// TODO(ekaramad):This method is almost duplicated in WebFrameWidgetImpl as
// well. This code needs to be refactored  (http://crbug.com/629721).
bool WebViewImpl::IsSelectionAnchorFirst() const {
  const LocalFrame* frame = FocusedLocalFrameInWidget();
  if (!frame)
    return false;

  FrameSelection& selection = frame->Selection();
  if (!selection.IsAvailable()) {
    // plugins/mouse-capture-inside-shadow.html reaches here.
    return false;
  }
  return selection.ComputeVisibleSelectionInDOMTreeDeprecated().IsBaseFirst();
}

WebColor WebViewImpl::BackgroundColor() const {
  if (background_color_override_enabled_)
    return background_color_override_;
  if (!page_)
    return BaseBackgroundColor().Rgb();
  if (!page_->MainFrame())
    return BaseBackgroundColor().Rgb();
  if (!page_->MainFrame()->IsLocalFrame())
    return BaseBackgroundColor().Rgb();
  FrameView* view = page_->DeprecatedLocalMainFrame()->View();
  if (!view)
    return BaseBackgroundColor().Rgb();
  return view->DocumentBackgroundColor().Rgb();
}

WebPagePopupImpl* WebViewImpl::GetPagePopup() const {
  return page_popup_.Get();
}

// TODO(ekaramad):This method is almost duplicated in WebFrameWidgetImpl as
// well. This code needs to be refactored  (http://crbug.com/629721).
WebRange WebViewImpl::CaretOrSelectionRange() {
  const LocalFrame* focused = FocusedLocalFrameInWidget();
  if (!focused)
    return WebRange();

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  focused->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  return focused->GetInputMethodController().GetSelectionOffsets();
}

// TODO(ekaramad):This method is almost duplicated in WebFrameWidgetImpl as
// well. This code needs to be refactored  (http://crbug.com/629721).
void WebViewImpl::SetTextDirection(WebTextDirection direction) {
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

bool WebViewImpl::IsAcceleratedCompositingActive() const {
  return root_layer_;
}

void WebViewImpl::WillCloseLayerTreeView() {
  if (link_highlights_timeline_) {
    link_highlights_.clear();
    DetachCompositorAnimationTimeline(link_highlights_timeline_.get());
    link_highlights_timeline_.reset();
  }

  if (layer_tree_view_)
    GetPage()->WillCloseLayerTreeView(*layer_tree_view_, nullptr);

  SetRootLayer(nullptr);
  animation_host_ = nullptr;

  mutator_ = nullptr;
  layer_tree_view_ = nullptr;
}

void WebViewImpl::DidAcquirePointerLock() {
  if (MainFrameImpl())
    MainFrameImpl()->FrameWidget()->DidAcquirePointerLock();
}

void WebViewImpl::DidNotAcquirePointerLock() {
  if (MainFrameImpl())
    MainFrameImpl()->FrameWidget()->DidNotAcquirePointerLock();
}

void WebViewImpl::DidLosePointerLock() {
  // Make sure that the main frame wasn't swapped-out when the pointer lock is
  // lost. There's a race that can happen when a pointer lock is requested, but
  // the browser swaps out the main frame while the pointer lock request is in
  // progress. This won't be needed once the main frame is refactored to not use
  // the WebViewImpl as its WebWidget.
  if (MainFrameImpl())
    MainFrameImpl()->FrameWidget()->DidLosePointerLock();
}

// TODO(ekaramad):This method is almost duplicated in WebFrameWidgetImpl as
// well. This code needs to be refactored  (http://crbug.com/629721).
bool WebViewImpl::GetCompositionCharacterBounds(WebVector<WebRect>& bounds) {
  WebRange range = CompositionRange();
  if (range.IsEmpty())
    return false;

  WebLocalFrame* frame = FocusedFrame();

  // Only consider frames whose local root is the main frame. For other
  // local frames which have different local roots, the corresponding
  // WebFrameWidget will handle this task.
  if (frame->LocalRoot() != MainFrameImpl())
    return false;

  size_t character_count = range.length();
  size_t offset = range.StartOffset();
  WebVector<WebRect> result(character_count);
  WebRect webrect;
  for (size_t i = 0; i < character_count; ++i) {
    if (!frame->FirstRectForCharacterRange(offset + i, 1, webrect)) {
      DLOG(ERROR) << "Could not retrieve character rectangle at " << i;
      return false;
    }
    result[i] = webrect;
  }
  bounds.Swap(result);
  return true;
}

// WebView --------------------------------------------------------------------

WebSettingsImpl* WebViewImpl::SettingsImpl() {
  if (!web_settings_)
    web_settings_ = WTF::WrapUnique(
        new WebSettingsImpl(&page_->GetSettings(), dev_tools_emulator_.Get()));
  DCHECK(web_settings_);
  return web_settings_.get();
}

WebSettings* WebViewImpl::GetSettings() {
  return SettingsImpl();
}

WebString WebViewImpl::PageEncoding() const {
  if (!page_)
    return WebString();

  if (!page_->MainFrame()->IsLocalFrame())
    return WebString();

  // FIXME: Is this check needed?
  if (!page_->DeprecatedLocalMainFrame()->GetDocument()->Loader())
    return WebString();

  return page_->DeprecatedLocalMainFrame()->GetDocument()->EncodingName();
}

WebFrame* WebViewImpl::MainFrame() {
  return WebFrame::FromFrame(page_ ? page_->MainFrame() : nullptr);
}

WebFrame* WebViewImpl::FindFrameByName(const WebString& name,
                                       WebFrame* relative_to_frame) {
  // FIXME: Either this should only deal with WebLocalFrames or it should move
  // to WebFrame.
  if (!relative_to_frame)
    relative_to_frame = MainFrame();
  Frame* frame = ToWebLocalFrameImpl(relative_to_frame)->GetFrame();
  frame = frame->Tree().Find(name);
  if (!frame || !frame->IsLocalFrame())
    return nullptr;
  return WebLocalFrameImpl::FromFrame(ToLocalFrame(frame));
}

WebLocalFrame* WebViewImpl::FocusedFrame() {
  Frame* frame = FocusedCoreFrame();
  // TODO(yabinh): focusedCoreFrame() should always return a local frame, and
  // the following check should be unnecessary.
  // See crbug.com/625068
  if (!frame || !frame->IsLocalFrame())
    return nullptr;
  return WebLocalFrameImpl::FromFrame(ToLocalFrame(frame));
}

void WebViewImpl::SetFocusedFrame(WebFrame* frame) {
  if (!frame) {
    // Clears the focused frame if any.
    Frame* focused_frame = FocusedCoreFrame();
    if (focused_frame && focused_frame->IsLocalFrame())
      ToLocalFrame(focused_frame)->Selection().SetFrameIsFocused(false);
    return;
  }
  LocalFrame* core_frame = ToWebLocalFrameImpl(frame)->GetFrame();
  core_frame->GetPage()->GetFocusController().SetFocusedFrame(core_frame);
}

void WebViewImpl::FocusDocumentView(WebFrame* frame) {
  // This is currently only used when replicating focus changes for
  // cross-process frames, and |notifyEmbedder| is disabled to avoid sending
  // duplicate frameFocused updates from FocusController to the browser
  // process, which already knows the latest focused frame.
  GetPage()->GetFocusController().FocusDocumentView(
      WebFrame::ToCoreFrame(*frame), false /* notifyEmbedder */);
}

void WebViewImpl::SetInitialFocus(bool reverse) {
  if (!page_)
    return;
  Frame* frame = GetPage()->GetFocusController().FocusedOrMainFrame();
  if (frame->IsLocalFrame()) {
    if (Document* document = ToLocalFrame(frame)->GetDocument())
      document->ClearFocusedElement();
  }
  GetPage()->GetFocusController().SetInitialFocus(
      reverse ? kWebFocusTypeBackward : kWebFocusTypeForward);
}

void WebViewImpl::ClearFocusedElement() {
  Frame* frame = FocusedCoreFrame();
  if (!frame || !frame->IsLocalFrame())
    return;

  LocalFrame* local_frame = ToLocalFrame(frame);

  Document* document = local_frame->GetDocument();
  if (!document)
    return;

  Element* old_focused_element = document->FocusedElement();
  document->ClearFocusedElement();
  if (!old_focused_element)
    return;

  // If a text field has focus, we need to make sure the selection controller
  // knows to remove selection from it. Otherwise, the text field is still
  // processing keyboard events even though focus has been moved to the page and
  // keystrokes get eaten as a result.
  document->UpdateStyleAndLayoutTree();
  if (HasEditableStyle(*old_focused_element) ||
      old_focused_element->IsTextControl())
    local_frame->Selection().Clear();
}

// TODO(dglazkov): Remove and replace with Node:hasEditableStyle.
// http://crbug.com/612560
static bool IsElementEditable(const Element* element) {
  element->GetDocument().UpdateStyleAndLayoutTree();
  if (HasEditableStyle(*element))
    return true;

  if (element->IsTextControl()) {
    if (!ToTextControlElement(element)->IsDisabledOrReadOnly())
      return true;
  }

  return EqualIgnoringASCIICase(element->getAttribute(HTMLNames::roleAttr),
                                "textbox");
}

bool WebViewImpl::ScrollFocusedEditableElementIntoRect(
    const WebRect& rect_in_viewport) {
  LocalFrame* frame =
      GetPage()->MainFrame() && GetPage()->MainFrame()->IsLocalFrame()
          ? GetPage()->DeprecatedLocalMainFrame()
          : nullptr;
  Element* element = FocusedElement();
  if (!frame || !frame->View() || !element)
    return false;

  if (!IsElementEditable(element))
    return false;

  element->GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  bool zoom_in_to_legible_scale =
      web_settings_->AutoZoomFocusedNodeToLegibleScale() &&
      !GetPage()->GetVisualViewport().ShouldDisableDesktopWorkarounds();

  if (zoom_in_to_legible_scale) {
    // When deciding whether to zoom in on a focused text box, we should decide
    // not to zoom in if the user won't be able to zoom out. e.g if the textbox
    // is within a touch-action: none container the user can't zoom back out.
    TouchAction action = TouchActionUtil::ComputeEffectiveTouchAction(*element);
    if (!(action & TouchAction::kTouchActionPinchZoom))
      zoom_in_to_legible_scale = false;
  }

  float scale;
  IntPoint scroll;
  bool need_animation;
  ComputeScaleAndScrollForFocusedNode(element, zoom_in_to_legible_scale, scale,
                                      scroll, need_animation);
  if (need_animation)
    StartPageScaleAnimation(scroll, false, scale,
                            scrollAndScaleAnimationDurationInSeconds);

  return true;
}

void WebViewImpl::SmoothScroll(int target_x, int target_y, long duration_ms) {
  IntPoint target_position(target_x, target_y);
  StartPageScaleAnimation(target_position, false, PageScaleFactor(),
                          (double)duration_ms / 1000);
}

void WebViewImpl::ComputeScaleAndScrollForFocusedNode(
    Node* focused_node,
    bool zoom_in_to_legible_scale,
    float& new_scale,
    IntPoint& new_scroll,
    bool& need_animation) {
  VisualViewport& visual_viewport = GetPage()->GetVisualViewport();

  WebRect caret_in_viewport, unused_end;
  SelectionBounds(caret_in_viewport, unused_end);

  // 'caretInDocument' is rect encompassing the blinking cursor relative to the
  // root document.
  IntRect caret_in_document = MainFrameImpl()->GetFrameView()->FrameToContents(
      visual_viewport.ViewportToRootFrame(caret_in_viewport));
  IntRect textbox_rect_in_document =
      MainFrameImpl()->GetFrameView()->FrameToContents(
          focused_node->GetDocument().View()->ContentsToRootFrame(
              PixelSnappedIntRect(focused_node->Node::BoundingBox())));

  if (!zoom_in_to_legible_scale) {
    new_scale = PageScaleFactor();
  } else {
    // Pick a scale which is reasonably readable. This is the scale at which
    // the caret height will become minReadableCaretHeightForNode (adjusted
    // for dpi and font scale factor).
    const int min_readable_caret_height_for_node =
        textbox_rect_in_document.Height() >= 2 * caret_in_document.Height()
            ? minReadableCaretHeightForTextArea
            : minReadableCaretHeight;
    new_scale = ClampPageScaleFactorToLimits(
        MaximumLegiblePageScale() * min_readable_caret_height_for_node /
        caret_in_document.Height());
    new_scale = std::max(new_scale, PageScaleFactor());
  }
  const float delta_scale = new_scale / PageScaleFactor();

  need_animation = false;

  // If we are at less than the target zoom level, zoom in.
  if (delta_scale > minScaleChangeToTriggerZoom)
    need_animation = true;
  else
    new_scale = PageScaleFactor();

  // If the caret is offscreen, then animate.
  if (!visual_viewport.VisibleRectInDocument().Contains(caret_in_document))
    need_animation = true;

  // If the box is partially offscreen and it's possible to bring it fully
  // onscreen, then animate.
  if (visual_viewport.VisibleRect().Width() >=
          textbox_rect_in_document.Width() &&
      visual_viewport.VisibleRect().Height() >=
          textbox_rect_in_document.Height() &&
      !visual_viewport.VisibleRectInDocument().Contains(
          textbox_rect_in_document))
    need_animation = true;

  if (!need_animation)
    return;

  FloatSize target_viewport_size(visual_viewport.Size());
  target_viewport_size.Scale(1 / new_scale);

  if (textbox_rect_in_document.Width() <= target_viewport_size.Width()) {
    // Field is narrower than screen. Try to leave padding on left so field's
    // label is visible, but it's more important to ensure entire field is
    // onscreen.
    int ideal_left_padding = target_viewport_size.Width() * leftBoxRatio;
    int max_left_padding_keeping_box_onscreen =
        target_viewport_size.Width() - textbox_rect_in_document.Width();
    new_scroll.SetX(textbox_rect_in_document.X() -
                    std::min<int>(ideal_left_padding,
                                  max_left_padding_keeping_box_onscreen));
  } else {
    // Field is wider than screen. Try to left-align field, unless caret would
    // be offscreen, in which case right-align the caret.
    new_scroll.SetX(std::max<int>(textbox_rect_in_document.X(),
                                  caret_in_document.X() +
                                      caret_in_document.Width() + caretPadding -
                                      target_viewport_size.Width()));
  }
  if (textbox_rect_in_document.Height() <= target_viewport_size.Height()) {
    // Field is shorter than screen. Vertically center it.
    new_scroll.SetY(
        textbox_rect_in_document.Y() -
        (target_viewport_size.Height() - textbox_rect_in_document.Height()) /
            2);
  } else {
    // Field is taller than screen. Try to top align field, unless caret would
    // be offscreen, in which case bottom-align the caret.
    new_scroll.SetY(
        std::max<int>(textbox_rect_in_document.Y(),
                      caret_in_document.Y() + caret_in_document.Height() +
                          caretPadding - target_viewport_size.Height()));
  }
}

void WebViewImpl::AdvanceFocus(bool reverse) {
  GetPage()->GetFocusController().AdvanceFocus(reverse ? kWebFocusTypeBackward
                                                       : kWebFocusTypeForward);
}

void WebViewImpl::AdvanceFocusAcrossFrames(WebFocusType type,
                                           WebRemoteFrame* from,
                                           WebLocalFrame* to) {
  // TODO(alexmos): Pass in proper with sourceCapabilities.
  GetPage()->GetFocusController().AdvanceFocusAcrossFrames(
      type, ToWebRemoteFrameImpl(from)->GetFrame(),
      ToWebLocalFrameImpl(to)->GetFrame());
}

double WebViewImpl::ZoomLevel() {
  return zoom_level_;
}

void WebViewImpl::PropagateZoomFactorToLocalFrameRoots(Frame* frame,
                                                       float zoom_factor) {
  if (frame->IsLocalRoot()) {
    LocalFrame* local_frame = ToLocalFrame(frame);
    if (!WebLocalFrameImpl::PluginContainerFromFrame(local_frame))
      local_frame->SetPageZoomFactor(zoom_factor);
  }

  for (Frame* child = frame->Tree().FirstChild(); child;
       child = child->Tree().NextSibling())
    PropagateZoomFactorToLocalFrameRoots(child, zoom_factor);
}

double WebViewImpl::SetZoomLevel(double zoom_level) {
  if (zoom_level < minimum_zoom_level_)
    zoom_level_ = minimum_zoom_level_;
  else if (zoom_level > maximum_zoom_level_)
    zoom_level_ = maximum_zoom_level_;
  else
    zoom_level_ = zoom_level;

  float zoom_factor =
      zoom_factor_override_
          ? zoom_factor_override_
          : static_cast<float>(ZoomLevelToZoomFactor(zoom_level_));
  if (zoom_factor_for_device_scale_factor_) {
    if (compositor_device_scale_factor_override_) {
      // Adjust the page's DSF so that DevicePixelRatio becomes
      // m_zoomFactorForDeviceScaleFactor.
      GetPage()->SetDeviceScaleFactorDeprecated(
          zoom_factor_for_device_scale_factor_ /
          compositor_device_scale_factor_override_);
      zoom_factor *= compositor_device_scale_factor_override_;
    } else {
      GetPage()->SetDeviceScaleFactorDeprecated(1.f);
      zoom_factor *= zoom_factor_for_device_scale_factor_;
    }
  }
  PropagateZoomFactorToLocalFrameRoots(page_->MainFrame(), zoom_factor);

  return zoom_level_;
}

void WebViewImpl::ZoomLimitsChanged(double minimum_zoom_level,
                                    double maximum_zoom_level) {
  minimum_zoom_level_ = minimum_zoom_level;
  maximum_zoom_level_ = maximum_zoom_level;
  client_->ZoomLimitsChanged(minimum_zoom_level_, maximum_zoom_level_);
}

float WebViewImpl::TextZoomFactor() {
  return MainFrameImpl()->GetFrame()->TextZoomFactor();
}

float WebViewImpl::SetTextZoomFactor(float text_zoom_factor) {
  LocalFrame* frame = MainFrameImpl()->GetFrame();
  if (WebLocalFrameImpl::PluginContainerFromFrame(frame))
    return 1;

  frame->SetTextZoomFactor(text_zoom_factor);

  return text_zoom_factor;
}

double WebView::ZoomLevelToZoomFactor(double zoom_level) {
  return pow(kTextSizeMultiplierRatio, zoom_level);
}

double WebView::ZoomFactorToZoomLevel(double factor) {
  // Since factor = 1.2^level, level = log(factor) / log(1.2)
  return log(factor) / log(kTextSizeMultiplierRatio);
}

float WebViewImpl::PageScaleFactor() const {
  if (!GetPage())
    return 1;

  return GetPage()->GetVisualViewport().Scale();
}

float WebViewImpl::ClampPageScaleFactorToLimits(float scale_factor) const {
  return GetPageScaleConstraintsSet().FinalConstraints().ClampToConstraints(
      scale_factor);
}

void WebViewImpl::SetVisualViewportOffset(const WebFloatPoint& offset) {
  DCHECK(GetPage());
  GetPage()->GetVisualViewport().SetLocation(offset);
}

WebFloatPoint WebViewImpl::VisualViewportOffset() const {
  DCHECK(GetPage());
  return GetPage()->GetVisualViewport().VisibleRect().Location();
}

WebFloatSize WebViewImpl::VisualViewportSize() const {
  DCHECK(GetPage());
  return GetPage()->GetVisualViewport().VisibleRect().Size();
}

void WebViewImpl::ScrollAndRescaleViewports(
    float scale_factor,
    const IntPoint& main_frame_origin,
    const FloatPoint& visual_viewport_origin) {
  if (!GetPage())
    return;

  if (!MainFrameImpl())
    return;

  FrameView* view = MainFrameImpl()->GetFrameView();
  if (!view)
    return;

  // Order is important: visual viewport location is clamped based on
  // main frame scroll position and visual viewport scale.

  view->SetScrollOffset(ToScrollOffset(main_frame_origin), kProgrammaticScroll);

  SetPageScaleFactor(scale_factor);

  GetPage()->GetVisualViewport().SetLocation(visual_viewport_origin);
}

void WebViewImpl::SetPageScaleFactorAndLocation(float scale_factor,
                                                const FloatPoint& location) {
  DCHECK(GetPage());

  GetPage()->GetVisualViewport().SetScaleAndLocation(
      ClampPageScaleFactorToLimits(scale_factor), location);
}

void WebViewImpl::SetPageScaleFactor(float scale_factor) {
  DCHECK(GetPage());

  scale_factor = ClampPageScaleFactorToLimits(scale_factor);
  if (scale_factor == PageScaleFactor())
    return;

  GetPage()->GetVisualViewport().SetScale(scale_factor);
}

void WebViewImpl::SetDeviceScaleFactor(float scale_factor) {
  if (!GetPage())
    return;

  GetPage()->SetDeviceScaleFactorDeprecated(scale_factor);

  if (layer_tree_view_)
    UpdateLayerTreeDeviceScaleFactor();
}

void WebViewImpl::SetZoomFactorForDeviceScaleFactor(
    float zoom_factor_for_device_scale_factor) {
  zoom_factor_for_device_scale_factor_ = zoom_factor_for_device_scale_factor;
  if (!layer_tree_view_)
    return;
  SetZoomLevel(zoom_level_);
}

void WebViewImpl::SetDeviceColorProfile(const gfx::ICCProfile& color_profile) {
  ColorBehavior::SetGlobalTargetColorProfile(color_profile);
}

void WebViewImpl::EnableAutoResizeMode(const WebSize& min_size,
                                       const WebSize& max_size) {
  should_auto_resize_ = true;
  min_auto_size_ = min_size;
  max_auto_size_ = max_size;
  ConfigureAutoResizeMode();
}

void WebViewImpl::DisableAutoResizeMode() {
  should_auto_resize_ = false;
  ConfigureAutoResizeMode();
}

void WebViewImpl::SetDefaultPageScaleLimits(float min_scale, float max_scale) {
  return GetPage()->SetDefaultPageScaleLimits(min_scale, max_scale);
}

void WebViewImpl::SetInitialPageScaleOverride(
    float initial_page_scale_factor_override) {
  PageScaleConstraints constraints =
      GetPageScaleConstraintsSet().UserAgentConstraints();
  constraints.initial_scale = initial_page_scale_factor_override;

  if (constraints == GetPageScaleConstraintsSet().UserAgentConstraints())
    return;

  GetPageScaleConstraintsSet().SetNeedsReset(true);
  GetPage()->SetUserAgentPageScaleConstraints(constraints);
}

void WebViewImpl::SetMaximumLegibleScale(float maximum_legible_scale) {
  maximum_legible_scale_ = maximum_legible_scale;
}

void WebViewImpl::SetIgnoreViewportTagScaleLimits(bool ignore) {
  PageScaleConstraints constraints =
      GetPageScaleConstraintsSet().UserAgentConstraints();
  if (ignore) {
    constraints.minimum_scale =
        GetPageScaleConstraintsSet().DefaultConstraints().minimum_scale;
    constraints.maximum_scale =
        GetPageScaleConstraintsSet().DefaultConstraints().maximum_scale;
  } else {
    constraints.minimum_scale = -1;
    constraints.maximum_scale = -1;
  }
  GetPage()->SetUserAgentPageScaleConstraints(constraints);
}

IntSize WebViewImpl::MainFrameSize() {
  // The frame size should match the viewport size at minimum scale, since the
  // viewport must always be contained by the frame.
  FloatSize frame_size(size_);
  frame_size.Scale(1 / MinimumPageScaleFactor());
  return ExpandedIntSize(frame_size);
}

PageScaleConstraintsSet& WebViewImpl::GetPageScaleConstraintsSet() const {
  return GetPage()->GetPageScaleConstraintsSet();
}

void WebViewImpl::RefreshPageScaleFactorAfterLayout() {
  if (!MainFrame() || !GetPage() || !GetPage()->MainFrame() ||
      !GetPage()->MainFrame()->IsLocalFrame() ||
      !GetPage()->DeprecatedLocalMainFrame()->View())
    return;
  FrameView* view = GetPage()->DeprecatedLocalMainFrame()->View();

  UpdatePageDefinedViewportConstraints(
      MainFrameImpl()->GetFrame()->GetDocument()->GetViewportDescription());
  GetPageScaleConstraintsSet().ComputeFinalConstraints();

  int vertical_scrollbar_width = 0;
  if (view->VerticalScrollbar() &&
      !view->VerticalScrollbar()->IsOverlayScrollbar())
    vertical_scrollbar_width = view->VerticalScrollbar()->Width();
  GetPageScaleConstraintsSet().AdjustFinalConstraintsToContentsSize(
      ContentsSize(), vertical_scrollbar_width,
      GetSettings()->ShrinksViewportContentToFit());

  float new_page_scale_factor = PageScaleFactor();
  if (GetPageScaleConstraintsSet().NeedsReset() &&
      GetPageScaleConstraintsSet().FinalConstraints().initial_scale != -1) {
    new_page_scale_factor =
        GetPageScaleConstraintsSet().FinalConstraints().initial_scale;
    GetPageScaleConstraintsSet().SetNeedsReset(false);
  }
  SetPageScaleFactor(new_page_scale_factor);

  UpdateLayerTreeViewport();
}

void WebViewImpl::UpdatePageDefinedViewportConstraints(
    const ViewportDescription& description) {
  if (!GetPage() || (!size_.width && !size_.height) ||
      !GetPage()->MainFrame()->IsLocalFrame())
    return;

  if (!GetSettings()->ViewportEnabled()) {
    GetPageScaleConstraintsSet().ClearPageDefinedConstraints();
    UpdateMainFrameLayoutSize();

    // If we don't support mobile viewports, allow GPU rasterization.
    matches_heuristics_for_gpu_rasterization_ = true;
    if (layer_tree_view_)
      layer_tree_view_->HeuristicsForGpuRasterizationUpdated(
          matches_heuristics_for_gpu_rasterization_);
    return;
  }

  Document* document = GetPage()->DeprecatedLocalMainFrame()->GetDocument();

  matches_heuristics_for_gpu_rasterization_ =
      description.MatchesHeuristicsForGpuRasterization();
  if (layer_tree_view_)
    layer_tree_view_->HeuristicsForGpuRasterizationUpdated(
        matches_heuristics_for_gpu_rasterization_);

  Length default_min_width = document->ViewportDefaultMinWidth();
  if (default_min_width.IsAuto())
    default_min_width = Length(kExtendToZoom);

  ViewportDescription adjusted_description = description;
  if (SettingsImpl()->ViewportMetaLayoutSizeQuirk() &&
      adjusted_description.type == ViewportDescription::kViewportMeta) {
    const int kLegacyWidthSnappingMagicNumber = 320;
    if (adjusted_description.max_width.IsFixed() &&
        adjusted_description.max_width.Value() <=
            kLegacyWidthSnappingMagicNumber)
      adjusted_description.max_width = Length(kDeviceWidth);
    if (adjusted_description.max_height.IsFixed() &&
        adjusted_description.max_height.Value() <= size_.height)
      adjusted_description.max_height = Length(kDeviceHeight);
    adjusted_description.min_width = adjusted_description.max_width;
    adjusted_description.min_height = adjusted_description.max_height;
  }

  float old_initial_scale =
      GetPageScaleConstraintsSet().PageDefinedConstraints().initial_scale;
  GetPageScaleConstraintsSet().UpdatePageDefinedConstraints(
      adjusted_description, default_min_width);

  if (SettingsImpl()->ClobberUserAgentInitialScaleQuirk() &&
      GetPageScaleConstraintsSet().UserAgentConstraints().initial_scale != -1 &&
      GetPageScaleConstraintsSet().UserAgentConstraints().initial_scale *
              DeviceScaleFactor() <=
          1) {
    if (description.max_width == Length(kDeviceWidth) ||
        (description.max_width.GetType() == kAuto &&
         GetPageScaleConstraintsSet().PageDefinedConstraints().initial_scale ==
             1.0f))
      SetInitialPageScaleOverride(-1);
  }

  Settings& page_settings = GetPage()->GetSettings();
  GetPageScaleConstraintsSet().AdjustForAndroidWebViewQuirks(
      adjusted_description, default_min_width.IntValue(), DeviceScaleFactor(),
      SettingsImpl()->SupportDeprecatedTargetDensityDPI(),
      page_settings.GetWideViewportQuirkEnabled(),
      page_settings.GetUseWideViewport(),
      page_settings.GetLoadWithOverviewMode(),
      SettingsImpl()->ViewportMetaNonUserScalableQuirk());
  float new_initial_scale =
      GetPageScaleConstraintsSet().PageDefinedConstraints().initial_scale;
  if (old_initial_scale != new_initial_scale && new_initial_scale != -1) {
    GetPageScaleConstraintsSet().SetNeedsReset(true);
    if (MainFrameImpl() && MainFrameImpl()->GetFrameView())
      MainFrameImpl()->GetFrameView()->SetNeedsLayout();
  }

  if (LocalFrame* frame = GetPage()->DeprecatedLocalMainFrame()) {
    if (TextAutosizer* text_autosizer =
            frame->GetDocument()->GetTextAutosizer())
      text_autosizer->UpdatePageInfoInAllFrames();
  }

  UpdateMainFrameLayoutSize();
}

void WebViewImpl::UpdateMainFrameLayoutSize() {
  if (should_auto_resize_ || !MainFrameImpl())
    return;

  FrameView* view = MainFrameImpl()->GetFrameView();
  if (!view)
    return;

  WebSize layout_size = size_;

  if (GetSettings()->ViewportEnabled())
    layout_size = GetPageScaleConstraintsSet().GetLayoutSize();

  if (GetPage()->GetSettings().GetForceZeroLayoutHeight())
    layout_size.height = 0;

  view->SetLayoutSize(layout_size);
}

IntSize WebViewImpl::ContentsSize() const {
  if (!GetPage()->MainFrame()->IsLocalFrame())
    return IntSize();
  LayoutViewItem root =
      GetPage()->DeprecatedLocalMainFrame()->ContentLayoutItem();
  if (root.IsNull())
    return IntSize();
  return root.DocumentRect().Size();
}

WebSize WebViewImpl::ContentsPreferredMinimumSize() {
  if (MainFrameImpl())
    MainFrameImpl()
        ->GetFrame()
        ->View()
        ->UpdateLifecycleToCompositingCleanPlusScrolling();

  Document* document = page_->MainFrame()->IsLocalFrame()
                           ? page_->DeprecatedLocalMainFrame()->GetDocument()
                           : nullptr;
  if (!document || document->GetLayoutViewItem().IsNull() ||
      !document->documentElement() ||
      !document->documentElement()->GetLayoutBox())
    return WebSize();

  // Needed for computing MinPreferredWidth.
  FontCachePurgePreventer fontCachePurgePreventer;
  int width_scaled = document->GetLayoutViewItem()
                         .MinPreferredLogicalWidth()
                         .Round();  // Already accounts for zoom.
  int height_scaled =
      document->documentElement()->GetLayoutBox()->ScrollHeight().Round();
  return IntSize(width_scaled, height_scaled);
}

float WebViewImpl::DefaultMinimumPageScaleFactor() const {
  return GetPageScaleConstraintsSet().DefaultConstraints().minimum_scale;
}

float WebViewImpl::DefaultMaximumPageScaleFactor() const {
  return GetPageScaleConstraintsSet().DefaultConstraints().maximum_scale;
}

float WebViewImpl::MinimumPageScaleFactor() const {
  return GetPageScaleConstraintsSet().FinalConstraints().minimum_scale;
}

float WebViewImpl::MaximumPageScaleFactor() const {
  return GetPageScaleConstraintsSet().FinalConstraints().maximum_scale;
}

void WebViewImpl::ResetScaleStateImmediately() {
  GetPageScaleConstraintsSet().SetNeedsReset(true);
}

void WebViewImpl::ResetScrollAndScaleState() {
  GetPage()->GetVisualViewport().Reset();

  if (!GetPage()->MainFrame()->IsLocalFrame())
    return;

  if (FrameView* frame_view = ToLocalFrame(GetPage()->MainFrame())->View()) {
    ScrollableArea* scrollable_area =
        frame_view->LayoutViewportScrollableArea();

    if (!scrollable_area->GetScrollOffset().IsZero())
      scrollable_area->SetScrollOffset(ScrollOffset(), kProgrammaticScroll);
  }

  GetPageScaleConstraintsSet().SetNeedsReset(true);
}

void WebViewImpl::PerformMediaPlayerAction(const WebMediaPlayerAction& action,
                                           const WebPoint& location) {
  HitTestResult result = HitTestResultForViewportPos(location);
  Node* node = result.InnerNode();
  if (!isHTMLVideoElement(*node) && !isHTMLAudioElement(*node))
    return;

  HTMLMediaElement* media_element = ToHTMLMediaElement(node);
  switch (action.type) {
    case WebMediaPlayerAction::kPlay:
      if (action.enable)
        media_element->Play();
      else
        media_element->pause();
      break;
    case WebMediaPlayerAction::kMute:
      media_element->setMuted(action.enable);
      break;
    case WebMediaPlayerAction::kLoop:
      media_element->SetLoop(action.enable);
      break;
    case WebMediaPlayerAction::kControls:
      media_element->SetBooleanAttribute(HTMLNames::controlsAttr,
                                         action.enable);
      break;
    default:
      NOTREACHED();
  }
}

void WebViewImpl::PerformPluginAction(const WebPluginAction& action,
                                      const WebPoint& location) {
  // FIXME: Location is probably in viewport coordinates
  HitTestResult result = HitTestResultForRootFramePos(location);
  Node* node = result.InnerNode();
  if (!isHTMLObjectElement(*node) && !isHTMLEmbedElement(*node))
    return;

  LayoutObject* object = node->GetLayoutObject();
  if (object && object->IsLayoutPart()) {
    PluginView* plugin_view = ToLayoutPart(object)->Plugin();
    if (plugin_view && plugin_view->IsPluginContainer()) {
      WebPluginContainerBase* plugin = ToWebPluginContainerBase(plugin_view);
      switch (action.type) {
        case WebPluginAction::kRotate90Clockwise:
          plugin->Plugin()->RotateView(WebPlugin::kRotationType90Clockwise);
          break;
        case WebPluginAction::kRotate90Counterclockwise:
          plugin->Plugin()->RotateView(
              WebPlugin::kRotationType90Counterclockwise);
          break;
        default:
          NOTREACHED();
      }
    }
  }
}

void WebViewImpl::AudioStateChanged(bool is_audio_playing) {
  scheduler_->AudioStateChanged(is_audio_playing);
}

WebHitTestResult WebViewImpl::HitTestResultAt(const WebPoint& point) {
  return CoreHitTestResultAt(point);
}

HitTestResult WebViewImpl::CoreHitTestResultAt(
    const WebPoint& point_in_viewport) {
  DocumentLifecycle::AllowThrottlingScope throttling_scope(
      MainFrameImpl()->GetFrame()->GetDocument()->Lifecycle());
  FrameView* view = MainFrameImpl()->GetFrameView();
  IntPoint point_in_root_frame =
      view->ContentsToFrame(view->ViewportToContents(point_in_viewport));
  return HitTestResultForRootFramePos(point_in_root_frame);
}

void WebViewImpl::SendResizeEventAndRepaint() {
  // FIXME: This is wrong. The FrameView is responsible sending a resizeEvent
  // as part of layout. Layout is also responsible for sending invalidations
  // to the embedder. This method and all callers may be wrong. -- eseidel.
  if (MainFrameImpl()->GetFrameView()) {
    // Enqueues the resize event.
    MainFrameImpl()->GetFrame()->GetDocument()->EnqueueResizeEvent();
  }

  if (client_) {
    if (layer_tree_view_) {
      UpdateLayerTreeViewport();
    } else {
      WebRect damaged_rect(0, 0, size_.width, size_.height);
      client_->WidgetClient()->DidInvalidateRect(damaged_rect);
    }
  }
}

void WebViewImpl::ConfigureAutoResizeMode() {
  if (!MainFrameImpl() || !MainFrameImpl()->GetFrame() ||
      !MainFrameImpl()->GetFrame()->View())
    return;

  if (should_auto_resize_)
    MainFrameImpl()->GetFrame()->View()->EnableAutoSizeMode(min_auto_size_,
                                                            max_auto_size_);
  else
    MainFrameImpl()->GetFrame()->View()->DisableAutoSizeMode();
}

unsigned long WebViewImpl::CreateUniqueIdentifierForRequest() {
  return CreateUniqueIdentifier();
}

void WebViewImpl::SetCompositorDeviceScaleFactorOverride(
    float device_scale_factor) {
  if (compositor_device_scale_factor_override_ == device_scale_factor)
    return;
  compositor_device_scale_factor_override_ = device_scale_factor;
  if (zoom_factor_for_device_scale_factor_) {
    SetZoomLevel(ZoomLevel());
    return;
  }
  if (GetPage() && layer_tree_view_)
    UpdateLayerTreeDeviceScaleFactor();
}

void WebViewImpl::SetDeviceEmulationTransform(
    const TransformationMatrix& transform) {
  if (transform == device_emulation_transform_)
    return;
  device_emulation_transform_ = transform;
  UpdateDeviceEmulationTransform();
}

TransformationMatrix WebViewImpl::GetDeviceEmulationTransformForTesting()
    const {
  return device_emulation_transform_;
}

void WebViewImpl::EnableDeviceEmulation(
    const WebDeviceEmulationParams& params) {
  dev_tools_emulator_->EnableDeviceEmulation(params);
}

void WebViewImpl::DisableDeviceEmulation() {
  dev_tools_emulator_->DisableDeviceEmulation();
}

WebAXObject WebViewImpl::AccessibilityObject() {
  if (!MainFrameImpl())
    return WebAXObject();

  Document* document = MainFrameImpl()->GetFrame()->GetDocument();
  return WebAXObject(ToAXObjectCacheImpl(document->AxObjectCache())->Root());
}

void WebViewImpl::PerformCustomContextMenuAction(unsigned action) {
  if (!page_)
    return;
  ContextMenu* menu = page_->GetContextMenuController().GetContextMenu();
  if (!menu)
    return;
  const ContextMenuItem* item = menu->ItemWithAction(
      static_cast<ContextMenuAction>(kContextMenuItemBaseCustomTag + action));
  if (item)
    page_->GetContextMenuController().ContextMenuItemSelected(item);
  page_->GetContextMenuController().ClearContextMenu();
}

void WebViewImpl::ShowContextMenu() {
  if (!GetPage())
    return;

  GetPage()->GetContextMenuController().ClearContextMenu();
  {
    ContextMenuAllowedScope scope;
    if (LocalFrame* focused_frame =
            ToLocalFrame(GetPage()->GetFocusController().FocusedOrMainFrame()))
      focused_frame->GetEventHandler().SendContextMenuEventForKey(nullptr);
  }
}

void WebViewImpl::DidCloseContextMenu() {
  LocalFrame* frame = page_->GetFocusController().FocusedFrame();
  if (frame)
    frame->Selection().SetCaretBlinkingSuspended(false);
}

void WebViewImpl::HidePopups() {
  CancelPagePopup();
}

WebInputMethodControllerImpl* WebViewImpl::GetActiveWebInputMethodController()
    const {
  return WebInputMethodControllerImpl::FromFrame(FocusedLocalFrameInWidget());
}

Color WebViewImpl::BaseBackgroundColor() const {
  return base_background_color_override_enabled_
             ? base_background_color_override_
             : base_background_color_;
}

void WebViewImpl::SetBaseBackgroundColor(WebColor color) {
  if (base_background_color_ == color)
    return;

  base_background_color_ = color;
  UpdateBaseBackgroundColor();
}

void WebViewImpl::SetBaseBackgroundColorOverride(WebColor color) {
  if (base_background_color_override_enabled_ &&
      base_background_color_override_ == color) {
    return;
  }

  base_background_color_override_enabled_ = true;
  base_background_color_override_ = color;
  if (MainFrameImpl()) {
    // Force lifecycle update to ensure we're good to call
    // FrameView::setBaseBackgroundColor().
    MainFrameImpl()
        ->GetFrame()
        ->View()
        ->UpdateLifecycleToCompositingCleanPlusScrolling();
  }
  UpdateBaseBackgroundColor();
}

void WebViewImpl::ClearBaseBackgroundColorOverride() {
  if (!base_background_color_override_enabled_)
    return;

  base_background_color_override_enabled_ = false;
  if (MainFrameImpl()) {
    // Force lifecycle update to ensure we're good to call
    // FrameView::setBaseBackgroundColor().
    MainFrameImpl()
        ->GetFrame()
        ->View()
        ->UpdateLifecycleToCompositingCleanPlusScrolling();
  }
  UpdateBaseBackgroundColor();
}

void WebViewImpl::UpdateBaseBackgroundColor() {
  Color color = BaseBackgroundColor();
  if (page_->MainFrame() && page_->MainFrame()->IsLocalFrame()) {
    FrameView* view = page_->DeprecatedLocalMainFrame()->View();
    view->SetBaseBackgroundColor(color);
    view->UpdateBaseBackgroundColorRecursively(color);
  }
}

void WebViewImpl::SetIsActive(bool active) {
  if (GetPage())
    GetPage()->GetFocusController().SetActive(active);
}

bool WebViewImpl::IsActive() const {
  return GetPage() ? GetPage()->GetFocusController().IsActive() : false;
}

void WebViewImpl::SetDomainRelaxationForbidden(bool forbidden,
                                               const WebString& scheme) {
  SchemeRegistry::SetDomainRelaxationForbiddenForURLScheme(forbidden,
                                                           String(scheme));
}

void WebViewImpl::SetWindowFeatures(const WebWindowFeatures& features) {
  page_->GetChromeClient().SetWindowFeatures(features);
}

void WebViewImpl::SetOpenedByDOM() {
  page_->SetOpenedByDOM();
}

void WebViewImpl::SetSelectionColors(unsigned active_background_color,
                                     unsigned active_foreground_color,
                                     unsigned inactive_background_color,
                                     unsigned inactive_foreground_color) {
#if USE(DEFAULT_RENDER_THEME)
  LayoutThemeDefault::SetSelectionColors(
      active_background_color, active_foreground_color,
      inactive_background_color, inactive_foreground_color);
  LayoutTheme::GetTheme().PlatformColorsDidChange();
#endif
}

void WebViewImpl::DidCommitLoad(bool is_new_navigation,
                                bool is_navigation_within_page) {
  if (!is_navigation_within_page) {
    should_dispatch_first_visually_non_empty_layout_ = true;
    should_dispatch_first_layout_after_finished_parsing_ = true;
    should_dispatch_first_layout_after_finished_loading_ = true;

    if (is_new_navigation) {
      GetPageScaleConstraintsSet().SetNeedsReset(true);
      page_importance_signals_.OnCommitLoad();
    }
  }

  // Give the visual viewport's scroll layer its initial size.
  GetPage()->GetVisualViewport().MainFrameDidChangeSize();

  // Make sure link highlight from previous page is cleared.
  link_highlights_.clear();
  EndActiveFlingAnimation();
}

void WebViewImpl::ResizeAfterLayout(WebLocalFrameImpl* webframe) {
  LocalFrame* frame = webframe->GetFrame();
  if (!client_ || !client_->CanUpdateLayout() || !frame->IsMainFrame())
    return;

  if (should_auto_resize_) {
    WebSize frame_size = frame->View()->FrameRect().Size();
    if (frame_size != size_) {
      size_ = frame_size;

      GetPage()->GetVisualViewport().SetSize(size_);
      GetPageScaleConstraintsSet().DidChangeInitialContainingBlockSize(size_);
      frame->View()->SetInitialViewportSize(size_);

      client_->DidAutoResize(size_);
      SendResizeEventAndRepaint();
    }
  }

  if (GetPageScaleConstraintsSet().ConstraintsDirty())
    RefreshPageScaleFactorAfterLayout();

  resize_viewport_anchor_->ResizeFrameView(MainFrameSize());
}

void WebViewImpl::LayoutUpdated(WebLocalFrameImpl* webframe) {
  LocalFrame* frame = webframe->GetFrame();
  if (!client_ || !frame->IsMainFrame())
    return;

  UpdatePageOverlays();

  fullscreen_controller_->DidUpdateLayout();
  client_->DidUpdateLayout();
}

void WebViewImpl::DidChangeContentsSize() {
  GetPageScaleConstraintsSet().DidChangeContentsSize(ContentsSize(),
                                                     PageScaleFactor());
}

void WebViewImpl::PageScaleFactorChanged() {
  GetPageScaleConstraintsSet().SetNeedsReset(false);
  UpdateLayerTreeViewport();
  client_->PageScaleFactorChanged();
  dev_tools_emulator_->MainFrameScrollOrScaleChanged();
}

void WebViewImpl::MainFrameScrollOffsetChanged() {
  dev_tools_emulator_->MainFrameScrollOrScaleChanged();
}

bool WebViewBase::UseExternalPopupMenus() {
  return WebViewImpl::UseExternalPopupMenus();
}

bool WebViewImpl::UseExternalPopupMenus() {
  return g_should_use_external_popup_menus;
}

void WebViewImpl::SetBackgroundColorOverride(WebColor color) {
  background_color_override_enabled_ = true;
  background_color_override_ = color;
  UpdateLayerTreeBackgroundColor();
}

void WebViewImpl::ClearBackgroundColorOverride() {
  background_color_override_enabled_ = false;
  UpdateLayerTreeBackgroundColor();
}

void WebViewImpl::SetZoomFactorOverride(float zoom_factor) {
  zoom_factor_override_ = zoom_factor;
  SetZoomLevel(ZoomLevel());
}

void WebViewImpl::SetPageOverlayColor(WebColor color) {
  if (page_color_overlay_)
    page_color_overlay_.reset();

  if (color == Color::kTransparent)
    return;

  page_color_overlay_ = PageOverlay::Create(
      MainFrameImpl(), WTF::MakeUnique<ColorOverlay>(color));

  // Run compositing update before calling updatePageOverlays.
  MainFrameImpl()
      ->GetFrameView()
      ->UpdateLifecycleToCompositingCleanPlusScrolling();

  UpdatePageOverlays();
}

WebPageImportanceSignals* WebViewImpl::PageImportanceSignals() {
  return &page_importance_signals_;
}

Element* WebViewImpl::FocusedElement() const {
  LocalFrame* frame = page_->GetFocusController().FocusedFrame();
  if (!frame)
    return nullptr;

  Document* document = frame->GetDocument();
  if (!document)
    return nullptr;

  return document->FocusedElement();
}

HitTestResult WebViewImpl::HitTestResultForViewportPos(
    const IntPoint& pos_in_viewport) {
  IntPoint root_frame_point(
      page_->GetVisualViewport().ViewportToRootFrame(pos_in_viewport));
  return HitTestResultForRootFramePos(root_frame_point);
}

HitTestResult WebViewImpl::HitTestResultForRootFramePos(
    const IntPoint& pos_in_root_frame) {
  if (!page_->MainFrame()->IsLocalFrame())
    return HitTestResult();
  IntPoint doc_point(
      page_->DeprecatedLocalMainFrame()->View()->RootFrameToContents(
          pos_in_root_frame));
  HitTestResult result =
      page_->DeprecatedLocalMainFrame()->GetEventHandler().HitTestResultAtPoint(
          doc_point, HitTestRequest::kReadOnly | HitTestRequest::kActive);
  result.SetToShadowHostIfInRestrictedShadowRoot();
  return result;
}

WebHitTestResult WebViewImpl::HitTestResultForTap(
    const WebPoint& tap_point_window_pos,
    const WebSize& tap_area) {
  if (!page_->MainFrame()->IsLocalFrame())
    return HitTestResult();

  WebGestureEvent tap_event(WebInputEvent::kGestureTap,
                            WebInputEvent::kNoModifiers,
                            WTF::MonotonicallyIncreasingTime());
  tap_event.x = tap_point_window_pos.x;
  tap_event.y = tap_point_window_pos.y;
  // GestureTap is only ever from a touchscreen.
  tap_event.source_device = kWebGestureDeviceTouchscreen;
  tap_event.data.tap.tap_count = 1;
  tap_event.data.tap.width = tap_area.width;
  tap_event.data.tap.height = tap_area.height;

  WebGestureEvent scaled_event =
      TransformWebGestureEvent(MainFrameImpl()->GetFrameView(), tap_event);

  HitTestResult result =
      page_->DeprecatedLocalMainFrame()
          ->GetEventHandler()
          .HitTestResultForGestureEvent(
              scaled_event, HitTestRequest::kReadOnly | HitTestRequest::kActive)
          .GetHitTestResult();

  result.SetToShadowHostIfInRestrictedShadowRoot();
  return result;
}

void WebViewImpl::SetTabsToLinks(bool enable) {
  tabs_to_links_ = enable;
}

bool WebViewImpl::TabsToLinks() const {
  return tabs_to_links_;
}

void WebViewImpl::RegisterViewportLayersWithCompositor() {
  DCHECK(layer_tree_view_);

  if (!GetPage()->MainFrame() || !GetPage()->MainFrame()->IsLocalFrame())
    return;

  Document* document = GetPage()->DeprecatedLocalMainFrame()->GetDocument();

  DCHECK(document);

  // Get the outer viewport scroll layers.
  GraphicsLayer* layout_viewport_container_layer =
      GetPage()->GlobalRootScrollerController().RootContainerLayer();
  WebLayer* layout_viewport_container_web_layer =
      layout_viewport_container_layer
          ? layout_viewport_container_layer->PlatformLayer()
          : nullptr;

  GraphicsLayer* layout_viewport_scroll_layer =
      GetPage()->GlobalRootScrollerController().RootScrollerLayer();
  WebLayer* layout_viewport_scroll_web_layer =
      layout_viewport_scroll_layer
          ? layout_viewport_scroll_layer->PlatformLayer()
          : nullptr;

  VisualViewport& visual_viewport = GetPage()->GetVisualViewport();

  // TODO(bokan): This was moved here from when registerViewportLayers was a
  // part of VisualViewport and maybe doesn't belong here. See comment inside
  // the mehtod.
  visual_viewport.SetScrollLayerOnScrollbars(layout_viewport_scroll_web_layer);

  layer_tree_view_->RegisterViewportLayers(
      visual_viewport.OverscrollElasticityLayer()->PlatformLayer(),
      visual_viewport.PageScaleLayer()->PlatformLayer(),
      visual_viewport.ContainerLayer()->PlatformLayer(),
      layout_viewport_container_web_layer,
      visual_viewport.ScrollLayer()->PlatformLayer(),
      layout_viewport_scroll_web_layer);
}

void WebViewImpl::SetRootGraphicsLayer(GraphicsLayer* graphics_layer) {
  if (!layer_tree_view_)
    return;

  // In SPv2, setRootLayer is used instead.
  DCHECK(!RuntimeEnabledFeatures::slimmingPaintV2Enabled());

  VisualViewport& visual_viewport = GetPage()->GetVisualViewport();
  visual_viewport.AttachLayerTree(graphics_layer);
  if (graphics_layer) {
    root_graphics_layer_ = visual_viewport.RootGraphicsLayer();
    visual_viewport_container_layer_ = visual_viewport.ContainerLayer();
    root_layer_ = root_graphics_layer_->PlatformLayer();
    UpdateDeviceEmulationTransform();
    layer_tree_view_->SetRootLayer(*root_layer_);
    // We register viewport layers here since there may not be a layer
    // tree view prior to this point.
    RegisterViewportLayersWithCompositor();

    // TODO(enne): Work around page visibility changes not being
    // propagated to the WebView in some circumstances.  This needs to
    // be refreshed here when setting a new root layer to avoid being
    // stuck in a presumed incorrectly invisible state.
    layer_tree_view_->SetVisible(GetPage()->IsPageVisible());
  } else {
    root_graphics_layer_ = nullptr;
    visual_viewport_container_layer_ = nullptr;
    root_layer_ = nullptr;
    // This means that we're transitioning to a new page. Suppress
    // commits until Blink generates invalidations so we don't
    // attempt to paint too early in the next page load.
    layer_tree_view_->SetDeferCommits(true);
    layer_tree_view_->ClearRootLayer();
    layer_tree_view_->ClearViewportLayers();
    if (WebDevToolsAgentImpl* dev_tools = MainFrameDevToolsAgentImpl())
      dev_tools->RootLayerCleared();
  }
}

void WebViewImpl::SetRootLayer(WebLayer* layer) {
  if (!layer_tree_view_)
    return;

  if (layer) {
    root_layer_ = layer;
    layer_tree_view_->SetRootLayer(*root_layer_);
    layer_tree_view_->SetVisible(GetPage()->IsPageVisible());
  } else {
    root_layer_ = nullptr;
    // This means that we're transitioning to a new page. Suppress
    // commits until Blink generates invalidations so we don't
    // attempt to paint too early in the next page load.
    layer_tree_view_->SetDeferCommits(true);
    layer_tree_view_->ClearRootLayer();
    layer_tree_view_->ClearViewportLayers();
    if (WebDevToolsAgentImpl* dev_tools = MainFrameDevToolsAgentImpl())
      dev_tools->RootLayerCleared();
  }
}

void WebViewImpl::InvalidateRect(const IntRect& rect) {
  if (layer_tree_view_) {
    UpdateLayerTreeViewport();
  } else if (client_) {
    // This is only for WebViewPlugin.
    client_->WidgetClient()->DidInvalidateRect(rect);
  }
}

PaintLayerCompositor* WebViewImpl::Compositor() const {
  WebLocalFrameImpl* frame = MainFrameImpl();
  if (!frame)
    return nullptr;

  Document* document = frame->GetFrame()->GetDocument();
  if (!document || document->GetLayoutViewItem().IsNull())
    return nullptr;

  return document->GetLayoutViewItem().Compositor();
}

GraphicsLayer* WebViewImpl::RootGraphicsLayer() {
  return root_graphics_layer_;
}

void WebViewImpl::ScheduleAnimationForWidget() {
  if (layer_tree_view_) {
    layer_tree_view_->SetNeedsBeginFrame();
    return;
  }
  if (client_)
    client_->WidgetClient()->ScheduleAnimation();
}

void WebViewImpl::AttachCompositorAnimationTimeline(
    CompositorAnimationTimeline* timeline) {
  if (animation_host_)
    animation_host_->AddTimeline(*timeline);
}

void WebViewImpl::DetachCompositorAnimationTimeline(
    CompositorAnimationTimeline* timeline) {
  if (animation_host_)
    animation_host_->RemoveTimeline(*timeline);
}

void WebViewImpl::InitializeLayerTreeView() {
  if (client_) {
    layer_tree_view_ = client_->InitializeLayerTreeView();
    if (layer_tree_view_ && layer_tree_view_->CompositorAnimationHost()) {
      animation_host_ = WTF::MakeUnique<CompositorAnimationHost>(
          layer_tree_view_->CompositorAnimationHost());
    }
  }

  if (WebDevToolsAgentImpl* dev_tools = MainFrameDevToolsAgentImpl())
    dev_tools->LayerTreeViewChanged(layer_tree_view_);

  page_->GetSettings().SetAcceleratedCompositingEnabled(layer_tree_view_);
  if (layer_tree_view_) {
    page_->LayerTreeViewInitialized(*layer_tree_view_, nullptr);
    // We don't yet have a page loaded at this point of the initialization of
    // WebViewImpl, so don't allow cc to commit any frames Blink might
    // try to create in the meantime.
    layer_tree_view_->SetDeferCommits(true);
  }

  // FIXME: only unittests, click to play, Android printing, and printing (for
  // headers and footers) make this assert necessary. We should make them not
  // hit this code and then delete allowsBrokenNullLayerTreeView.
  DCHECK(layer_tree_view_ || !client_ ||
         client_->WidgetClient()->AllowsBrokenNullLayerTreeView());

  if (Platform::Current()->IsThreadedAnimationEnabled() && layer_tree_view_) {
    link_highlights_timeline_ = CompositorAnimationTimeline::Create();
    AttachCompositorAnimationTimeline(link_highlights_timeline_.get());
  }
}

void WebViewImpl::ApplyViewportDeltas(
    const WebFloatSize& visual_viewport_delta,
    // TODO(bokan): This parameter is to be removed but requires adjusting many
    // callsites.
    const WebFloatSize&,
    const WebFloatSize& elastic_overscroll_delta,
    float page_scale_delta,
    float browser_controls_shown_ratio_delta) {
  VisualViewport& visual_viewport = GetPage()->GetVisualViewport();

  // Store the desired offsets the visual viewport before setting the top
  // controls ratio since doing so will change the bounds and move the
  // viewports to keep the offsets valid. The compositor may have already
  // done that so we don't want to double apply the deltas here.
  FloatPoint visual_viewport_offset = visual_viewport.VisibleRect().Location();
  visual_viewport_offset.Move(visual_viewport_delta.width,
                              visual_viewport_delta.height);

  GetBrowserControls().SetShownRatio(GetBrowserControls().ShownRatio() +
                                     browser_controls_shown_ratio_delta);

  SetPageScaleFactorAndLocation(PageScaleFactor() * page_scale_delta,
                                visual_viewport_offset);

  if (page_scale_delta != 1) {
    double_tap_zoom_pending_ = false;
    visual_viewport.UserDidChangeScale();
  }

  elastic_overscroll_ += elastic_overscroll_delta;

  if (MainFrameImpl() && MainFrameImpl()->GetFrameView())
    MainFrameImpl()->GetFrameView()->DidUpdateElasticOverscroll();
}

void WebViewImpl::RecordWheelAndTouchScrollingCount(
    bool has_scrolled_by_wheel,
    bool has_scrolled_by_touch) {
  if (!GetPage() || !GetPage()->MainFrame())
    return;

  if (has_scrolled_by_wheel)
    UseCounter::Count(GetPage()->MainFrame(), UseCounter::kScrollByWheel);
  if (has_scrolled_by_touch)
    UseCounter::Count(GetPage()->MainFrame(), UseCounter::kScrollByTouch);
}

void WebViewImpl::UpdateLayerTreeViewport() {
  if (!GetPage() || !layer_tree_view_)
    return;

  layer_tree_view_->SetPageScaleFactorAndLimits(
      PageScaleFactor(), MinimumPageScaleFactor(), MaximumPageScaleFactor());
}

void WebViewImpl::UpdateLayerTreeBackgroundColor() {
  if (!layer_tree_view_)
    return;
  layer_tree_view_->SetBackgroundColor(BackgroundColor());
}

void WebViewImpl::UpdateLayerTreeDeviceScaleFactor() {
  DCHECK(GetPage());
  DCHECK(layer_tree_view_);

  float device_scale_factor = compositor_device_scale_factor_override_
                                  ? compositor_device_scale_factor_override_
                                  : GetPage()->DeviceScaleFactorDeprecated();
  layer_tree_view_->SetDeviceScaleFactor(device_scale_factor);
}

void WebViewImpl::UpdateDeviceEmulationTransform() {
  if (!visual_viewport_container_layer_)
    return;

  // When the device emulation transform is updated, to avoid incorrect
  // scales and fuzzy raster from the compositor, force all content to
  // pick ideal raster scales.
  visual_viewport_container_layer_->SetTransform(device_emulation_transform_);
  layer_tree_view_->ForceRecalculateRasterScales();
}

WebViewScheduler* WebViewImpl::Scheduler() const {
  return scheduler_.get();
}

void WebViewImpl::SetVisibilityState(WebPageVisibilityState visibility_state,
                                     bool is_initial_state) {
  DCHECK(visibility_state == kWebPageVisibilityStateVisible ||
         visibility_state == kWebPageVisibilityStateHidden ||
         visibility_state == kWebPageVisibilityStatePrerender);

  if (GetPage())
    page_->SetVisibilityState(
        static_cast<PageVisibilityState>(static_cast<int>(visibility_state)),
        is_initial_state);

  bool visible = visibility_state == kWebPageVisibilityStateVisible;
  if (layer_tree_view_ && !override_compositor_visibility_)
    layer_tree_view_->SetVisible(visible);
  scheduler_->SetPageVisible(visible);
}

void WebViewImpl::SetCompositorVisibility(bool is_visible) {
  if (!is_visible)
    override_compositor_visibility_ = true;
  else
    override_compositor_visibility_ = false;
  if (layer_tree_view_)
    layer_tree_view_->SetVisible(is_visible);
}

void WebViewImpl::ForceNextWebGLContextCreationToFail() {
  WebGLRenderingContext::ForceNextWebGLContextCreationToFail();
}

void WebViewImpl::ForceNextDrawingBufferCreationToFail() {
  DrawingBuffer::ForceNextDrawingBufferCreationToFail();
}

CompositorMutatorImpl& WebViewImpl::Mutator() {
  if (!mutator_) {
    std::unique_ptr<CompositorMutatorClient> mutator_client =
        CompositorMutatorImpl::CreateClient();
    mutator_ = static_cast<CompositorMutatorImpl*>(mutator_client->Mutator());
    layer_tree_view_->SetMutatorClient(std::move(mutator_client));
  }

  return *mutator_;
}

CompositorWorkerProxyClient* WebViewImpl::CreateCompositorWorkerProxyClient() {
  return new CompositorWorkerProxyClientImpl(&Mutator());
}

AnimationWorkletProxyClient* WebViewImpl::CreateAnimationWorkletProxyClient() {
  return new AnimationWorkletProxyClientImpl(&Mutator());
}

void WebViewImpl::UpdatePageOverlays() {
  if (page_color_overlay_)
    page_color_overlay_->Update();
  if (WebDevToolsAgentImpl* devtools = MainFrameDevToolsAgentImpl())
    devtools->LayoutOverlay();
}

float WebViewImpl::DeviceScaleFactor() const {
  // TODO(oshima): Investigate if this should return the ScreenInfo's scale
  // factor rather than page's scale factor, which can be 1 in use-zoom-for-dsf
  // mode.
  if (!GetPage())
    return 1;

  return GetPage()->DeviceScaleFactorDeprecated();
}

LocalFrame* WebViewImpl::FocusedLocalFrameInWidget() const {
  if (!MainFrameImpl())
    return nullptr;

  LocalFrame* focused_frame = ToLocalFrame(FocusedCoreFrame());
  if (focused_frame->LocalFrameRoot() != MainFrameImpl()->GetFrame())
    return nullptr;
  return focused_frame;
}

LocalFrame* WebViewImpl::FocusedLocalFrameAvailableForIme() const {
  return ime_accept_events_ ? FocusedLocalFrameInWidget() : nullptr;
}

}  // namespace blink
