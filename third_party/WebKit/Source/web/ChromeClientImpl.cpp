/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
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

#include "web/ChromeClientImpl.h"

#include <memory>
#include "bindings/core/v8/ScriptController.h"
#include "core/HTMLNames.h"
#include "core/dom/AXObjectCache.h"
#include "core/dom/Document.h"
#include "core/dom/Fullscreen.h"
#include "core/dom/Node.h"
#include "core/events/UIEventWithKeyState.h"
#include "core/events/WebInputEventConversion.h"
#include "core/exported/WebFileChooserCompletionImpl.h"
#include "core/exported/WebPluginContainerBase.h"
#include "core/exported/WebViewBase.h"
#include "core/frame/FrameView.h"
#include "core/frame/Settings.h"
#include "core/frame/UseCounter.h"
#include "core/frame/VisualViewport.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/forms/ColorChooser.h"
#include "core/html/forms/ColorChooserClient.h"
#include "core/html/forms/DateTimeChooser.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/compositing/CompositedSelection.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/FrameLoadRequest.h"
#include "core/page/ChromeClient.h"
#include "core/page/Page.h"
#include "core/page/PopupOpeningObserver.h"
#include "modules/accessibility/AXObjectImpl.h"
#include "modules/audio_output_devices/AudioOutputDeviceClient.h"
#include "modules/installedapp/InstalledAppController.h"
#include "modules/mediastream/UserMediaController.h"
#include "modules/navigatorcontentutils/NavigatorContentUtils.h"
#include "modules/presentation/PresentationController.h"
#include "modules/push_messaging/PushController.h"
#include "modules/screen_orientation/ScreenOrientationControllerImpl.h"
#include "modules/vr/VRController.h"
#include "platform/Cursor.h"
#include "platform/FileChooser.h"
#include "platform/Histogram.h"
#include "platform/KeyboardCodes.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/WebFrameScheduler.h"
#include "platform/animation/CompositorAnimationHost.h"
#include "platform/exported/WrappedResourceRequest.h"
#include "platform/geometry/IntRect.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/TouchAction.h"
#include "platform/scheduler/renderer/web_view_scheduler.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "platform/wtf/Optional.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/text/CString.h"
#include "platform/wtf/text/CharacterNames.h"
#include "platform/wtf/text/StringBuilder.h"
#include "platform/wtf/text/StringConcatenate.h"
#include "public/platform/WebCursorInfo.h"
#include "public/platform/WebFloatRect.h"
#include "public/platform/WebInputEvent.h"
#include "public/platform/WebRect.h"
#include "public/platform/WebURLRequest.h"
#include "public/web/WebAXObject.h"
#include "public/web/WebAutofillClient.h"
#include "public/web/WebColorChooser.h"
#include "public/web/WebColorSuggestion.h"
#include "public/web/WebConsoleMessage.h"
#include "public/web/WebFrameClient.h"
#include "public/web/WebInputElement.h"
#include "public/web/WebKit.h"
#include "public/web/WebNode.h"
#include "public/web/WebPageImportanceSignals.h"
#include "public/web/WebPlugin.h"
#include "public/web/WebPopupMenuInfo.h"
#include "public/web/WebSelection.h"
#include "public/web/WebSettings.h"
#include "public/web/WebTextDirection.h"
#include "public/web/WebUserGestureIndicator.h"
#include "public/web/WebUserGestureToken.h"
#include "public/web/WebViewClient.h"
#include "public/web/WebWindowFeatures.h"
#include "web/AudioOutputDeviceClientImpl.h"
#include "web/ColorChooserPopupUIController.h"
#include "web/ColorChooserUIController.h"
#include "web/DateTimeChooserImpl.h"
#include "web/DevToolsEmulator.h"
#include "web/ExternalDateTimeChooser.h"
#include "web/ExternalPopupMenu.h"
#include "web/IndexedDBClientImpl.h"
#include "web/LocalFileSystemClient.h"
#include "web/NavigatorContentUtilsClientImpl.h"
#include "web/PopupMenuImpl.h"
#include "web/WebFrameWidgetImpl.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebRemoteFrameImpl.h"
#include "web/WebSettingsImpl.h"

namespace blink {

namespace {

const char* DialogTypeToString(ChromeClient::DialogType dialog_type) {
  switch (dialog_type) {
    case ChromeClient::kAlertDialog:
      return "alert";
    case ChromeClient::kConfirmDialog:
      return "confirm";
    case ChromeClient::kPromptDialog:
      return "prompt";
    case ChromeClient::kHTMLDialog:
      NOTREACHED();
  }
  NOTREACHED();
  return "";
}

const char* DismissalTypeToString(Document::PageDismissalType dismissal_type) {
  switch (dismissal_type) {
    case Document::kBeforeUnloadDismissal:
      return "beforeunload";
    case Document::kPageHideDismissal:
      return "pagehide";
    case Document::kUnloadVisibilityChangeDismissal:
      return "visibilitychange";
    case Document::kUnloadDismissal:
      return "unload";
    case Document::kNoDismissal:
      NOTREACHED();
  }
  NOTREACHED();
  return "";
}

}  // namespace

class CompositorAnimationTimeline;

// Converts a AXObjectCache::AXNotification to a WebAXEvent
static WebAXEvent ToWebAXEvent(AXObjectCache::AXNotification notification) {
  // These enums have the same values; enforced in AssertMatchingEnums.cpp.
  return static_cast<WebAXEvent>(notification);
}

ChromeClientImpl::ChromeClientImpl(WebViewBase* web_view)
    : web_view_(web_view),
      cursor_overridden_(false),
      did_request_non_empty_tool_tip_(false) {}

ChromeClientImpl::~ChromeClientImpl() {}

ChromeClientImpl* ChromeClientImpl::Create(WebViewBase* web_view) {
  return new ChromeClientImpl(web_view);
}

void* ChromeClientImpl::WebView() const {
  return static_cast<void*>(web_view_);
}

void ChromeClientImpl::ChromeDestroyed() {
  // Our lifetime is bound to the WebViewBase.
}

void ChromeClientImpl::SetWindowRect(const IntRect& r, LocalFrame& frame) {
  DCHECK_EQ(&frame, web_view_->MainFrameImpl()->GetFrame());
  WebWidgetClient* client =
      WebLocalFrameImpl::FromFrame(&frame)->FrameWidget()->Client();
  client->SetWindowRect(r);
}

IntRect ChromeClientImpl::RootWindowRect() {
  WebRect rect;
  if (web_view_->Client()) {
    rect = web_view_->Client()->RootWindowRect();
  } else {
    // These numbers will be fairly wrong. The window's x/y coordinates will
    // be the top left corner of the screen and the size will be the content
    // size instead of the window size.
    rect.width = web_view_->Size().width;
    rect.height = web_view_->Size().height;
  }
  return IntRect(rect);
}

IntRect ChromeClientImpl::PageRect() {
  // We hide the details of the window's border thickness from the web page by
  // simple re-using the window position here.  So, from the point-of-view of
  // the web page, the window has no border.
  return RootWindowRect();
}

void ChromeClientImpl::Focus() {
  if (web_view_->Client())
    web_view_->Client()->DidFocus();
}

bool ChromeClientImpl::CanTakeFocus(WebFocusType) {
  // For now the browser can always take focus if we're not running layout
  // tests.
  return !LayoutTestMode();
}

void ChromeClientImpl::TakeFocus(WebFocusType type) {
  if (!web_view_->Client())
    return;
  if (type == kWebFocusTypeBackward)
    web_view_->Client()->FocusPrevious();
  else
    web_view_->Client()->FocusNext();
}

void ChromeClientImpl::FocusedNodeChanged(Node* from_node, Node* to_node) {
  if (!web_view_->Client())
    return;

  web_view_->Client()->FocusedNodeChanged(WebNode(from_node), WebNode(to_node));

  WebURL focus_url;
  if (to_node && to_node->IsElementNode() && ToElement(to_node)->IsLiveLink() &&
      to_node->ShouldHaveFocusAppearance())
    focus_url = ToElement(to_node)->HrefURL();
  web_view_->Client()->SetKeyboardFocusURL(focus_url);
}

bool ChromeClientImpl::HadFormInteraction() const {
  return web_view_->PageImportanceSignals() &&
         web_view_->PageImportanceSignals()->HadFormInteraction();
}

void ChromeClientImpl::StartDragging(LocalFrame* frame,
                                     const WebDragData& drag_data,
                                     WebDragOperationsMask mask,
                                     const WebImage& drag_image,
                                     const WebPoint& drag_image_offset) {
  WebLocalFrameImpl* web_frame = WebLocalFrameImpl::FromFrame(frame);
  WebReferrerPolicy policy = web_frame->GetDocument().GetReferrerPolicy();
  web_frame->LocalRoot()->FrameWidget()->StartDragging(
      policy, drag_data, mask, drag_image, drag_image_offset);
}

bool ChromeClientImpl::AcceptsLoadDrops() const {
  return !web_view_->Client() || web_view_->Client()->AcceptsLoadDrops();
}

namespace {

void UpdatePolicyForEvent(const WebInputEvent* input_event,
                          NavigationPolicy* policy) {
  if (!input_event)
    return;

  unsigned short button_number = 0;
  if (input_event->GetType() == WebInputEvent::kMouseUp) {
    const WebMouseEvent* mouse_event =
        static_cast<const WebMouseEvent*>(input_event);

    switch (mouse_event->button) {
      case WebMouseEvent::Button::kLeft:
        button_number = 0;
        break;
      case WebMouseEvent::Button::kMiddle:
        button_number = 1;
        break;
      case WebMouseEvent::Button::kRight:
        button_number = 2;
        break;
      default:
        return;
    }
  } else if ((WebInputEvent::IsKeyboardEventType(input_event->GetType()) &&
              static_cast<const WebKeyboardEvent*>(input_event)
                      ->windows_key_code == VKEY_RETURN) ||
             WebInputEvent::IsGestureEventType(input_event->GetType())) {
    // Keyboard and gesture events can simulate mouse events.
    button_number = 0;
  } else {
    return;
  }

  bool ctrl = input_event->GetModifiers() & WebInputEvent::kControlKey;
  bool shift = input_event->GetModifiers() & WebInputEvent::kShiftKey;
  bool alt = input_event->GetModifiers() & WebInputEvent::kAltKey;
  bool meta = input_event->GetModifiers() & WebInputEvent::kMetaKey;

  NavigationPolicy user_policy = *policy;
  NavigationPolicyFromMouseEvent(button_number, ctrl, shift, alt, meta,
                                 &user_policy);

  // When the input event suggests a download, but the navigation was initiated
  // by script, we should not override it.
  if (user_policy == kNavigationPolicyDownload &&
      *policy != kNavigationPolicyIgnore)
    return;

  // User and app agree that we want a new window; let the app override the
  // decorations.
  if (user_policy == kNavigationPolicyNewWindow &&
      *policy == kNavigationPolicyNewPopup)
    return;
  *policy = user_policy;
}

WebNavigationPolicy GetNavigationPolicy(const WindowFeatures& features) {
  // If the window features didn't enable the toolbar, or this window wasn't
  // created by a user gesture, show as a popup instead of a new tab.
  //
  // Note: this previously also checked that menubar, resizable, scrollbar, and
  // statusbar are enabled too. When no feature string is specified, these
  // features default to enabled (and the window opens as a new tab). However,
  // when a feature string is specified, any *unspecified* features default to
  // disabled, often causing the window to open as a popup instead.
  //
  // As specifying menubar, resizable, scrollbar, and statusbar have no effect
  // on the UI, just ignore them and only consider whether or not the toolbar is
  // enabled, which matches Firefox's behavior.
  bool as_popup = !features.tool_bar_visible;

  NavigationPolicy policy = kNavigationPolicyNewForegroundTab;
  if (as_popup)
    policy = kNavigationPolicyNewPopup;
  UpdatePolicyForEvent(WebViewBase::CurrentInputEvent(), &policy);

  return static_cast<WebNavigationPolicy>(policy);
}

WebNavigationPolicy EffectiveNavigationPolicy(
    NavigationPolicy navigation_policy,
    const WindowFeatures& features) {
  WebNavigationPolicy policy =
      static_cast<WebNavigationPolicy>(navigation_policy);
  if (policy == kWebNavigationPolicyIgnore)
    return GetNavigationPolicy(features);
  if (policy == kWebNavigationPolicyNewBackgroundTab &&
      GetNavigationPolicy(features) != kWebNavigationPolicyNewBackgroundTab &&
      !UIEventWithKeyState::NewTabModifierSetFromIsolatedWorld())
    return kWebNavigationPolicyNewForegroundTab;

  return policy;
}

}  // namespace

Page* ChromeClientImpl::CreateWindow(LocalFrame* frame,
                                     const FrameLoadRequest& r,
                                     const WindowFeatures& features,
                                     NavigationPolicy navigation_policy) {
  if (!web_view_->Client())
    return nullptr;

  if (!frame->GetPage() || frame->GetPage()->Suspended())
    return nullptr;

  WebNavigationPolicy policy =
      EffectiveNavigationPolicy(navigation_policy, features);
  DCHECK(frame->GetDocument());
  Fullscreen::FullyExitFullscreen(*frame->GetDocument());

  WebViewBase* new_view =
      static_cast<WebViewBase*>(web_view_->Client()->CreateView(
          WebLocalFrameImpl::FromFrame(frame),
          WrappedResourceRequest(r.GetResourceRequest()), features,
          r.FrameName(), policy,
          r.GetShouldSetOpener() == kNeverSetOpener || features.noopener));
  if (!new_view)
    return nullptr;
  return new_view->GetPage();
}

void ChromeClientImpl::DidOverscroll(const FloatSize& overscroll_delta,
                                     const FloatSize& accumulated_overscroll,
                                     const FloatPoint& position_in_viewport,
                                     const FloatSize& velocity_in_viewport) {
  if (!web_view_->Client())
    return;

  web_view_->Client()->DidOverscroll(overscroll_delta, accumulated_overscroll,
                                     position_in_viewport,
                                     velocity_in_viewport);
}

void ChromeClientImpl::Show(NavigationPolicy navigation_policy) {
  if (web_view_->Client())
    web_view_->Client()->Show(
        EffectiveNavigationPolicy(navigation_policy, window_features_));
}

void ChromeClientImpl::SetToolbarsVisible(bool value) {
  window_features_.tool_bar_visible = value;
}

bool ChromeClientImpl::ToolbarsVisible() {
  return window_features_.tool_bar_visible;
}

void ChromeClientImpl::SetStatusbarVisible(bool value) {
  window_features_.status_bar_visible = value;
}

bool ChromeClientImpl::StatusbarVisible() {
  return window_features_.status_bar_visible;
}

void ChromeClientImpl::SetScrollbarsVisible(bool value) {
  window_features_.scrollbars_visible = value;
  if (WebLocalFrameImpl* web_frame =
          ToWebLocalFrameImpl(web_view_->MainFrame()))
    web_frame->SetCanHaveScrollbars(value);
}

bool ChromeClientImpl::ScrollbarsVisible() {
  return window_features_.scrollbars_visible;
}

void ChromeClientImpl::SetMenubarVisible(bool value) {
  window_features_.menu_bar_visible = value;
}

bool ChromeClientImpl::MenubarVisible() {
  return window_features_.menu_bar_visible;
}

void ChromeClientImpl::SetResizable(bool value) {
  window_features_.resizable = value;
}

bool ChromeClientImpl::ShouldReportDetailedMessageForSource(
    LocalFrame& local_frame,
    const String& url) {
  WebLocalFrameImpl* webframe =
      WebLocalFrameImpl::FromFrame(&local_frame.LocalFrameRoot());
  return webframe && webframe->Client() &&
         webframe->Client()->ShouldReportDetailedMessageForSource(url);
}

void ChromeClientImpl::AddMessageToConsole(LocalFrame* local_frame,
                                           MessageSource source,
                                           MessageLevel level,
                                           const String& message,
                                           unsigned line_number,
                                           const String& source_id,
                                           const String& stack_trace) {
  WebLocalFrameImpl* frame = WebLocalFrameImpl::FromFrame(local_frame);
  if (frame && frame->Client()) {
    frame->Client()->DidAddMessageToConsole(
        WebConsoleMessage(static_cast<WebConsoleMessage::Level>(level),
                          message),
        source_id, line_number, stack_trace);
  }
}

bool ChromeClientImpl::CanOpenBeforeUnloadConfirmPanel() {
  return !!web_view_->Client();
}

bool ChromeClientImpl::OpenBeforeUnloadConfirmPanelDelegate(LocalFrame* frame,
                                                            bool is_reload) {
  NotifyPopupOpeningObservers();
  WebLocalFrameImpl* webframe = WebLocalFrameImpl::FromFrame(frame);
  return webframe->Client() &&
         webframe->Client()->RunModalBeforeUnloadDialog(is_reload);
}

void ChromeClientImpl::CloseWindowSoon() {
  if (web_view_->Client())
    web_view_->Client()->CloseWidgetSoon();
}

// Although a LocalFrame is passed in, we don't actually use it, since we
// already know our own m_webView.
bool ChromeClientImpl::OpenJavaScriptAlertDelegate(LocalFrame* frame,
                                                   const String& message) {
  NotifyPopupOpeningObservers();
  WebLocalFrameImpl* webframe = WebLocalFrameImpl::FromFrame(frame);
  if (webframe->Client()) {
    if (WebUserGestureIndicator::IsProcessingUserGesture())
      WebUserGestureIndicator::CurrentUserGestureToken().SetJavascriptPrompt();
    webframe->Client()->RunModalAlertDialog(message);
    return true;
  }
  return false;
}

// See comments for openJavaScriptAlertDelegate().
bool ChromeClientImpl::OpenJavaScriptConfirmDelegate(LocalFrame* frame,
                                                     const String& message) {
  NotifyPopupOpeningObservers();
  WebLocalFrameImpl* webframe = WebLocalFrameImpl::FromFrame(frame);
  if (webframe->Client()) {
    if (WebUserGestureIndicator::IsProcessingUserGesture())
      WebUserGestureIndicator::CurrentUserGestureToken().SetJavascriptPrompt();
    return webframe->Client()->RunModalConfirmDialog(message);
  }
  return false;
}

// See comments for openJavaScriptAlertDelegate().
bool ChromeClientImpl::OpenJavaScriptPromptDelegate(LocalFrame* frame,
                                                    const String& message,
                                                    const String& default_value,
                                                    String& result) {
  NotifyPopupOpeningObservers();
  WebLocalFrameImpl* webframe = WebLocalFrameImpl::FromFrame(frame);
  if (webframe->Client()) {
    if (WebUserGestureIndicator::IsProcessingUserGesture())
      WebUserGestureIndicator::CurrentUserGestureToken().SetJavascriptPrompt();
    WebString actual_value;
    bool ok = webframe->Client()->RunModalPromptDialog(message, default_value,
                                                       &actual_value);
    if (ok)
      result = actual_value;
    return ok;
  }
  return false;
}

void ChromeClientImpl::SetStatusbarText(const String& message) {
  if (web_view_->Client())
    web_view_->Client()->SetStatusText(message);
}

bool ChromeClientImpl::TabsToLinks() {
  return web_view_->TabsToLinks();
}

void ChromeClientImpl::InvalidateRect(const IntRect& update_rect) {
  if (!update_rect.IsEmpty())
    web_view_->InvalidateRect(update_rect);
}

void ChromeClientImpl::ScheduleAnimation(
    const PlatformFrameView* platform_frame_view) {
  DCHECK(platform_frame_view->IsFrameView());
  LocalFrame& frame =
      ToFrameView(platform_frame_view)->GetFrame().LocalFrameRoot();
  // If the frame is still being created, it might not yet have a WebWidget.
  // FIXME: Is this the right thing to do? Is there a way to avoid having
  // a local frame root that doesn't have a WebWidget? During initialization
  // there is no content to draw so this call serves no purpose.
  if (WebLocalFrameImpl::FromFrame(&frame) &&
      WebLocalFrameImpl::FromFrame(&frame)->FrameWidget())
    WebLocalFrameImpl::FromFrame(&frame)->FrameWidget()->ScheduleAnimation();
}

IntRect ChromeClientImpl::ViewportToScreen(
    const IntRect& rect_in_viewport,
    const PlatformFrameView* platform_frame_view) const {
  WebRect screen_rect(rect_in_viewport);

  DCHECK(platform_frame_view->IsFrameView());
  const FrameView* view = ToFrameView(platform_frame_view);
  LocalFrame& frame = view->GetFrame().LocalFrameRoot();

  WebWidgetClient* client =
      WebLocalFrameImpl::FromFrame(&frame)->FrameWidget()->Client();

  if (client) {
    client->ConvertViewportToWindow(&screen_rect);
    WebRect view_rect = client->ViewRect();
    screen_rect.x += view_rect.x;
    screen_rect.y += view_rect.y;
  }

  return screen_rect;
}

float ChromeClientImpl::WindowToViewportScalar(const float scalar_value) const {
  if (!web_view_->Client())
    return scalar_value;
  WebFloatRect viewport_rect(0, 0, scalar_value, 0);
  web_view_->Client()->ConvertWindowToViewport(&viewport_rect);
  return viewport_rect.width;
}

WebScreenInfo ChromeClientImpl::GetScreenInfo() const {
  return web_view_->Client() ? web_view_->Client()->GetScreenInfo()
                             : WebScreenInfo();
}

WTF::Optional<IntRect> ChromeClientImpl::VisibleContentRectForPainting() const {
  return web_view_->GetDevToolsEmulator()->VisibleContentRectForPainting();
}

void ChromeClientImpl::ContentsSizeChanged(LocalFrame* frame,
                                           const IntSize& size) const {
  web_view_->DidChangeContentsSize();

  WebLocalFrameImpl* webframe = WebLocalFrameImpl::FromFrame(frame);
  webframe->DidChangeContentsSize(size);
}

void ChromeClientImpl::PageScaleFactorChanged() const {
  web_view_->PageScaleFactorChanged();
}

void ChromeClientImpl::MainFrameScrollOffsetChanged() const {
  web_view_->MainFrameScrollOffsetChanged();
}

float ChromeClientImpl::ClampPageScaleFactorToLimits(float scale) const {
  return web_view_->ClampPageScaleFactorToLimits(scale);
}

void ChromeClientImpl::ResizeAfterLayout(LocalFrame* frame) const {
  web_view_->ResizeAfterLayout(WebLocalFrameImpl::FromFrame(frame));
}

void ChromeClientImpl::LayoutUpdated(LocalFrame* frame) const {
  web_view_->LayoutUpdated(WebLocalFrameImpl::FromFrame(frame));
}

void ChromeClientImpl::ShowMouseOverURL(const HitTestResult& result) {
  if (!web_view_->Client())
    return;

  WebURL url;

  // Ignore URL if hitTest include scrollbar since we might have both a
  // scrollbar and an element in the case of overlay scrollbars.
  if (!result.GetScrollbar()) {
    // Find out if the mouse is over a link, and if so, let our UI know...
    if (result.IsLiveLink() &&
        !result.AbsoluteLinkURL().GetString().IsEmpty()) {
      url = result.AbsoluteLinkURL();
    } else if (result.InnerNode() &&
               (isHTMLObjectElement(*result.InnerNode()) ||
                isHTMLEmbedElement(*result.InnerNode()))) {
      LayoutObject* object = result.InnerNode()->GetLayoutObject();
      if (object && object->IsLayoutPart()) {
        PluginView* plugin_view = ToLayoutPart(object)->Plugin();
        if (plugin_view && plugin_view->IsPluginContainer()) {
          WebPluginContainerBase* plugin =
              ToWebPluginContainerBase(plugin_view);
          url = plugin->Plugin()->LinkAtPosition(
              result.RoundedPointInInnerNodeFrame());
        }
      }
    }
  }

  web_view_->Client()->SetMouseOverURL(url);
}

void ChromeClientImpl::SetToolTip(LocalFrame& frame,
                                  const String& tooltip_text,
                                  TextDirection dir) {
  WebLocalFrameImpl* web_frame =
      WebLocalFrameImpl::FromFrame(&frame)->LocalRoot();
  if (!tooltip_text.IsEmpty()) {
    web_frame->FrameWidget()->Client()->SetToolTipText(tooltip_text,
                                                       ToWebTextDirection(dir));
    did_request_non_empty_tool_tip_ = true;
  } else if (did_request_non_empty_tool_tip_) {
    // WebWidgetClient::setToolTipText will send an IPC message.  We'd like to
    // reduce the number of setToolTipText calls.
    web_frame->FrameWidget()->Client()->SetToolTipText(tooltip_text,
                                                       ToWebTextDirection(dir));
    did_request_non_empty_tool_tip_ = false;
  }
}

void ChromeClientImpl::DispatchViewportPropertiesDidChange(
    const ViewportDescription& description) const {
  web_view_->UpdatePageDefinedViewportConstraints(description);
}

void ChromeClientImpl::PrintDelegate(LocalFrame* frame) {
  NotifyPopupOpeningObservers();
  if (web_view_->Client())
    web_view_->Client()->PrintPage(WebLocalFrameImpl::FromFrame(frame));
}

ColorChooser* ChromeClientImpl::OpenColorChooser(
    LocalFrame* frame,
    ColorChooserClient* chooser_client,
    const Color&) {
  NotifyPopupOpeningObservers();
  ColorChooserUIController* controller = nullptr;
  if (RuntimeEnabledFeatures::pagePopupEnabled())
    controller =
        ColorChooserPopupUIController::Create(frame, this, chooser_client);
  else
    controller = ColorChooserUIController::Create(frame, chooser_client);
  controller->OpenUI();
  return controller;
}

DateTimeChooser* ChromeClientImpl::OpenDateTimeChooser(
    DateTimeChooserClient* picker_client,
    const DateTimeChooserParameters& parameters) {
  NotifyPopupOpeningObservers();
  if (RuntimeEnabledFeatures::inputMultipleFieldsUIEnabled())
    return DateTimeChooserImpl::Create(this, picker_client, parameters);
  return ExternalDateTimeChooser::Create(this, web_view_->Client(),
                                         picker_client, parameters);
}

void ChromeClientImpl::OpenFileChooser(LocalFrame* frame,
                                       PassRefPtr<FileChooser> file_chooser) {
  NotifyPopupOpeningObservers();
  WebFrameClient* client = WebLocalFrameImpl::FromFrame(frame)->Client();
  if (!client)
    return;

  WebFileChooserParams params;
  params.multi_select = file_chooser->GetSettings().allows_multiple_files;
  params.directory = file_chooser->GetSettings().allows_directory_upload;
  params.accept_types = file_chooser->GetSettings().AcceptTypes();
  params.selected_files = file_chooser->GetSettings().selected_files;
  params.use_media_capture = file_chooser->GetSettings().use_media_capture;
  params.need_local_path = file_chooser->GetSettings().allows_directory_upload;
  params.requestor = frame->GetDocument()->Url();

  WebFileChooserCompletionImpl* chooser_completion =
      new WebFileChooserCompletionImpl(std::move(file_chooser));
  if (client->RunFileChooser(params, chooser_completion))
    return;
  // Choosing failed, so do callback with an empty list.
  chooser_completion->DidChooseFile(WebVector<WebString>());
}

void ChromeClientImpl::EnumerateChosenDirectory(FileChooser* file_chooser) {
  WebViewClient* client = web_view_->Client();
  if (!client)
    return;

  WebFileChooserCompletionImpl* chooser_completion =
      new WebFileChooserCompletionImpl(file_chooser);

  DCHECK(file_chooser);
  DCHECK(file_chooser->GetSettings().selected_files.size());

  // If the enumeration can't happen, call the callback with an empty list.
  if (!client->EnumerateChosenDirectory(
          file_chooser->GetSettings().selected_files[0], chooser_completion))
    chooser_completion->DidChooseFile(WebVector<WebString>());
}

Cursor ChromeClientImpl::LastSetCursorForTesting() const {
  return last_set_mouse_cursor_for_testing_;
}

void ChromeClientImpl::SetCursor(const Cursor& cursor,
                                 LocalFrame* local_frame) {
  last_set_mouse_cursor_for_testing_ = cursor;
  SetCursor(WebCursorInfo(cursor), local_frame);
}

void ChromeClientImpl::SetCursor(const WebCursorInfo& cursor,
                                 LocalFrame* local_frame) {
  if (cursor_overridden_)
    return;

#if OS(MACOSX)
  // On Mac the mousemove event propagates to both the popup and main window.
  // If a popup is open we don't want the main window to change the cursor.
  if (web_view_->HasOpenedPopup())
    return;
#endif

  LocalFrame& local_root = local_frame->LocalFrameRoot();
  if (WebFrameWidgetBase* widget =
          WebLocalFrameImpl::FromFrame(&local_root)->FrameWidget())
    widget->Client()->DidChangeCursor(cursor);
}

void ChromeClientImpl::SetCursorForPlugin(const WebCursorInfo& cursor,
                                          LocalFrame* local_frame) {
  SetCursor(cursor, local_frame);
}

void ChromeClientImpl::SetCursorOverridden(bool overridden) {
  cursor_overridden_ = overridden;
}

void ChromeClientImpl::PostAccessibilityNotification(
    AXObject* axObject,
    AXObjectCache::AXNotification notification) {
  AXObjectImpl* obj = ToAXObjectImpl(axObject);

  // Alert assistive technology about the accessibility object notification.
  if (!obj || !obj->GetDocument())
    return;

  WebLocalFrameImpl* webframe = WebLocalFrameImpl::FromFrame(
      obj->GetDocument()->AxObjectCacheOwner().GetFrame());
  if (webframe && webframe->Client())
    webframe->Client()->PostAccessibilityEvent(WebAXObject(obj),
                                               ToWebAXEvent(notification));
}

String ChromeClientImpl::AcceptLanguages() {
  return web_view_->Client()->AcceptLanguages();
}

void ChromeClientImpl::AttachRootGraphicsLayer(GraphicsLayer* root_layer,
                                               LocalFrame* local_frame) {
  DCHECK(!RuntimeEnabledFeatures::slimmingPaintV2Enabled());
  WebLocalFrameImpl* web_frame =
      WebLocalFrameImpl::FromFrame(local_frame)->LocalRoot();

  // This method can be called while the frame is being detached. In that
  // case, the rootLayer is null, and the widget is already destroyed.
  DCHECK(web_frame->FrameWidget() || !root_layer);
  if (web_frame->FrameWidget())
    web_frame->FrameWidget()->SetRootGraphicsLayer(root_layer);
}

void ChromeClientImpl::AttachRootLayer(WebLayer* root_layer,
                                       LocalFrame* local_frame) {
  WebLocalFrameImpl* web_frame =
      WebLocalFrameImpl::FromFrame(local_frame)->LocalRoot();

  // This method can be called while the frame is being detached. In that
  // case, the rootLayer is null, and the widget is already destroyed.
  DCHECK(web_frame->FrameWidget() || !root_layer);
  if (web_frame->FrameWidget())
    web_frame->FrameWidget()->SetRootLayer(root_layer);
}

void ChromeClientImpl::AttachCompositorAnimationTimeline(
    CompositorAnimationTimeline* compositor_timeline,
    LocalFrame* local_frame) {
  WebLocalFrameImpl* web_frame =
      WebLocalFrameImpl::FromFrame(local_frame)->LocalRoot();
  if (CompositorAnimationHost* animation_host =
          web_frame->FrameWidget()->AnimationHost())
    animation_host->AddTimeline(*compositor_timeline);
}

void ChromeClientImpl::DetachCompositorAnimationTimeline(
    CompositorAnimationTimeline* compositor_timeline,
    LocalFrame* local_frame) {
  WebLocalFrameImpl* web_frame =
      WebLocalFrameImpl::FromFrame(local_frame)->LocalRoot();

  // This method can be called when the frame is being detached, after the
  // widget is destroyed.
  if (web_frame->FrameWidget()) {
    if (CompositorAnimationHost* animation_host =
            web_frame->FrameWidget()->AnimationHost())
      animation_host->RemoveTimeline(*compositor_timeline);
  }
}

void ChromeClientImpl::EnterFullscreen(LocalFrame& frame) {
  web_view_->EnterFullscreen(frame);
}

void ChromeClientImpl::ExitFullscreen(LocalFrame& frame) {
  web_view_->ExitFullscreen(frame);
}

void ChromeClientImpl::FullscreenElementChanged(Element* from_element,
                                                Element* to_element) {
  web_view_->FullscreenElementChanged(from_element, to_element);
}

void ChromeClientImpl::ClearCompositedSelection(LocalFrame* frame) {
  LocalFrame& local_root = frame->LocalFrameRoot();
  WebFrameWidgetBase* widget =
      WebLocalFrameImpl::FromFrame(&local_root)->FrameWidget();
  WebWidgetClient* client = widget->Client();
  if (!client)
    return;

  if (WebLayerTreeView* layer_tree_view = widget->GetLayerTreeView())
    layer_tree_view->ClearSelection();
}

void ChromeClientImpl::UpdateCompositedSelection(
    LocalFrame* frame,
    const CompositedSelection& selection) {
  LocalFrame& local_root = frame->LocalFrameRoot();
  WebFrameWidgetBase* widget =
      WebLocalFrameImpl::FromFrame(&local_root)->FrameWidget();
  WebWidgetClient* client = widget->Client();
  if (!client)
    return;

  if (WebLayerTreeView* layer_tree_view = widget->GetLayerTreeView())
    layer_tree_view->RegisterSelection(WebSelection(selection));
}

bool ChromeClientImpl::HasOpenedPopup() const {
  return web_view_->HasOpenedPopup();
}

PopupMenu* ChromeClientImpl::OpenPopupMenu(LocalFrame& frame,
                                           HTMLSelectElement& select) {
  NotifyPopupOpeningObservers();
  if (WebViewBase::UseExternalPopupMenus())
    return new ExternalPopupMenu(frame, select, *web_view_);

  DCHECK(RuntimeEnabledFeatures::pagePopupEnabled());
  return PopupMenuImpl::Create(this, select);
}

PagePopup* ChromeClientImpl::OpenPagePopup(PagePopupClient* client) {
  return web_view_->OpenPagePopup(client);
}

void ChromeClientImpl::ClosePagePopup(PagePopup* popup) {
  web_view_->ClosePagePopup(popup);
}

DOMWindow* ChromeClientImpl::PagePopupWindowForTesting() const {
  return web_view_->PagePopupWindow();
}

bool ChromeClientImpl::ShouldOpenModalDialogDuringPageDismissal(
    LocalFrame& frame,
    DialogType dialog_type,
    const String& dialog_message,
    Document::PageDismissalType dismissal_type) const {
  String message = String("Blocked ") + DialogTypeToString(dialog_type) + "('" +
                   dialog_message + "') during " +
                   DismissalTypeToString(dismissal_type) + ".";
  WebLocalFrameImpl::FromFrame(frame)->AddMessageToConsole(
      WebConsoleMessage(WebConsoleMessage::kLevelError, message));

  return false;
}

WebLayerTreeView* ChromeClientImpl::GetWebLayerTreeView(LocalFrame* frame) {
  WebLocalFrameImpl* web_frame = WebLocalFrameImpl::FromFrame(frame);
  return web_frame->LocalRoot()->FrameWidget()->GetLayerTreeView();
}

WebLocalFrameBase* ChromeClientImpl::GetWebLocalFrameBase(LocalFrame* frame) {
  return WebLocalFrameImpl::FromFrame(frame);
}

WebRemoteFrameBase* ChromeClientImpl::GetWebRemoteFrameBase(
    RemoteFrame& frame) {
  return WebRemoteFrameImpl::FromFrame(frame);
}

void ChromeClientImpl::SetEventListenerProperties(
    LocalFrame* frame,
    WebEventListenerClass event_class,
    WebEventListenerProperties properties) {
  // |frame| might be null if called via TreeScopeAdopter::
  // moveNodeToNewDocument() and the new document has no frame attached.
  // Since a document without a frame cannot attach one later, it is safe to
  // exit early.
  if (!frame)
    return;

  WebLocalFrameImpl* web_frame = WebLocalFrameImpl::FromFrame(frame);
  WebFrameWidgetBase* widget = web_frame->LocalRoot()->FrameWidget();
  // The widget may be nullptr if the frame is provisional.
  // TODO(dcheng): This needs to be cleaned up at some point.
  // https://crbug.com/578349
  if (!widget) {
    // If we hit a provisional frame, we expect it to be during initialization
    // in which case the |properties| should be 'nothing'.
    DCHECK(properties == WebEventListenerProperties::kNothing);
    return;
  }

  // This relies on widget always pointing to a WebFrameWidgetImpl when
  // |frame| points to an OOPIF frame, i.e. |frame|'s mainFrame() is
  // remote.
  WebWidgetClient* client = widget->Client();
  if (WebLayerTreeView* tree_view = widget->GetLayerTreeView()) {
    tree_view->SetEventListenerProperties(event_class, properties);
    if (event_class == WebEventListenerClass::kTouchStartOrMove) {
      client->HasTouchEventHandlers(
          properties != WebEventListenerProperties::kNothing ||
          tree_view->EventListenerProperties(
              WebEventListenerClass::kTouchEndOrCancel) !=
              WebEventListenerProperties::kNothing);
    } else if (event_class == WebEventListenerClass::kTouchEndOrCancel) {
      client->HasTouchEventHandlers(
          properties != WebEventListenerProperties::kNothing ||
          tree_view->EventListenerProperties(
              WebEventListenerClass::kTouchStartOrMove) !=
              WebEventListenerProperties::kNothing);
    }
  } else {
    client->HasTouchEventHandlers(true);
  }
}

void ChromeClientImpl::UpdateEventRectsForSubframeIfNecessary(
    LocalFrame* frame) {
  WebLocalFrameImpl* web_frame = WebLocalFrameImpl::FromFrame(frame);
  WebFrameWidgetBase* widget = web_frame->LocalRoot()->FrameWidget();
  if (WebLayerTreeView* tree_view = widget->GetLayerTreeView())
    tree_view->UpdateEventRectsForSubframeIfNecessary();
}

void ChromeClientImpl::BeginLifecycleUpdates() {
  if (WebLayerTreeView* tree_view = web_view_->LayerTreeView()) {
    tree_view->SetDeferCommits(false);
    tree_view->SetNeedsBeginFrame();
  }
}

WebEventListenerProperties ChromeClientImpl::EventListenerProperties(
    LocalFrame* frame,
    WebEventListenerClass event_class) const {
  if (!frame)
    return WebEventListenerProperties::kNothing;

  WebFrameWidgetBase* widget =
      WebLocalFrameImpl::FromFrame(frame)->LocalRoot()->FrameWidget();

  if (!widget || !widget->GetLayerTreeView())
    return WebEventListenerProperties::kNothing;
  return widget->GetLayerTreeView()->EventListenerProperties(event_class);
}

void ChromeClientImpl::SetHasScrollEventHandlers(LocalFrame* frame,
                                                 bool has_event_handlers) {
  // |frame| might be null if called via TreeScopeAdopter::
  // moveNodeToNewDocument() and the new document has no frame attached.
  // Since a document without a frame cannot attach one later, it is safe to
  // exit early.
  if (!frame)
    return;

  WebFrameWidgetBase* widget =
      WebLocalFrameImpl::FromFrame(frame)->LocalRoot()->FrameWidget();
  // While a frame is shutting down, we may get called after the layerTreeView
  // is gone: in this case we always expect |hasEventHandlers| to be false.
  DCHECK(!widget || widget->GetLayerTreeView() || !has_event_handlers);
  if (widget && widget->GetLayerTreeView())
    widget->GetLayerTreeView()->SetHaveScrollEventHandlers(has_event_handlers);
}

void ChromeClientImpl::SetTouchAction(LocalFrame* frame,
                                      TouchAction touch_action) {
  DCHECK(frame);
  WebLocalFrameImpl* web_frame = WebLocalFrameImpl::FromFrame(frame);
  WebFrameWidgetBase* widget = web_frame->LocalRoot()->FrameWidget();
  if (!widget)
    return;

  if (WebWidgetClient* client = widget->Client())
    client->SetTouchAction(static_cast<TouchAction>(touch_action));
}

bool ChromeClientImpl::RequestPointerLock(LocalFrame* frame) {
  LocalFrame& local_root = frame->LocalFrameRoot();
  return WebLocalFrameImpl::FromFrame(&local_root)
      ->FrameWidget()
      ->Client()
      ->RequestPointerLock();
}

void ChromeClientImpl::RequestPointerUnlock(LocalFrame* frame) {
  LocalFrame& local_root = frame->LocalFrameRoot();
  return WebLocalFrameImpl::FromFrame(&local_root)
      ->FrameWidget()
      ->Client()
      ->RequestPointerUnlock();
}

void ChromeClientImpl::AnnotatedRegionsChanged() {
  if (WebViewClient* client = web_view_->Client())
    client->DraggableRegionsChanged();
}

void ChromeClientImpl::DidAssociateFormControlsAfterLoad(LocalFrame* frame) {
  WebLocalFrameImpl* webframe = WebLocalFrameImpl::FromFrame(frame);
  if (webframe->AutofillClient())
    webframe->AutofillClient()->DidAssociateFormControlsDynamically();
}

void ChromeClientImpl::ShowVirtualKeyboardOnElementFocus(LocalFrame& frame) {
  WebLocalFrameImpl::FromFrame(frame.LocalFrameRoot())
      ->FrameWidget()
      ->Client()
      ->ShowVirtualKeyboardOnElementFocus();
}

void ChromeClientImpl::ShowUnhandledTapUIIfNeeded(
    IntPoint tapped_position_in_viewport,
    Node* tapped_node,
    bool page_changed) {
  if (web_view_->Client())
    web_view_->Client()->ShowUnhandledTapUIIfNeeded(
        WebPoint(tapped_position_in_viewport), WebNode(tapped_node),
        page_changed);
}

void ChromeClientImpl::OnMouseDown(Node& mouse_down_node) {
  if (auto* fill_client =
          WebLocalFrameImpl::FromFrame(mouse_down_node.GetDocument().GetFrame())
              ->AutofillClient()) {
    fill_client->DidReceiveLeftMouseDownOrGestureTapInNode(
        WebNode(&mouse_down_node));
  }
}

void ChromeClientImpl::HandleKeyboardEventOnTextField(
    HTMLInputElement& input_element,
    KeyboardEvent& event) {
  WebLocalFrameImpl* webframe =
      WebLocalFrameImpl::FromFrame(input_element.GetDocument().GetFrame());
  if (webframe->AutofillClient())
    webframe->AutofillClient()->TextFieldDidReceiveKeyDown(
        WebInputElement(&input_element), WebKeyboardEventBuilder(event));
}

void ChromeClientImpl::DidChangeValueInTextField(
    HTMLFormControlElement& element) {
  Document& doc = element.GetDocument();
  WebLocalFrameImpl* webframe = WebLocalFrameImpl::FromFrame(doc.GetFrame());
  if (webframe->AutofillClient())
    webframe->AutofillClient()->TextFieldDidChange(
        WebFormControlElement(&element));

  UseCounter::Count(doc, doc.IsSecureContext()
                             ? UseCounter::kFieldEditInSecureContext
                             : UseCounter::kFieldEditInNonSecureContext);
  web_view_->PageImportanceSignals()->SetHadFormInteraction();
}

void ChromeClientImpl::DidEndEditingOnTextField(
    HTMLInputElement& input_element) {
  WebLocalFrameImpl* webframe =
      WebLocalFrameImpl::FromFrame(input_element.GetDocument().GetFrame());
  if (webframe->AutofillClient())
    webframe->AutofillClient()->TextFieldDidEndEditing(
        WebInputElement(&input_element));
}

void ChromeClientImpl::OpenTextDataListChooser(HTMLInputElement& input) {
  NotifyPopupOpeningObservers();
  WebLocalFrameImpl* webframe =
      WebLocalFrameImpl::FromFrame(input.GetDocument().GetFrame());
  if (webframe->AutofillClient())
    webframe->AutofillClient()->OpenTextDataListChooser(
        WebInputElement(&input));
}

void ChromeClientImpl::TextFieldDataListChanged(HTMLInputElement& input) {
  WebLocalFrameImpl* webframe =
      WebLocalFrameImpl::FromFrame(input.GetDocument().GetFrame());
  if (webframe->AutofillClient())
    webframe->AutofillClient()->DataListOptionsChanged(WebInputElement(&input));
}

void ChromeClientImpl::AjaxSucceeded(LocalFrame* frame) {
  WebLocalFrameImpl* webframe = WebLocalFrameImpl::FromFrame(frame);
  if (webframe->AutofillClient())
    webframe->AutofillClient()->AjaxSucceeded();
}

void ChromeClientImpl::RegisterViewportLayers() const {
  if (web_view_->RootGraphicsLayer() && web_view_->LayerTreeView())
    web_view_->RegisterViewportLayersWithCompositor();
}

void ChromeClientImpl::DidUpdateBrowserControls() const {
  web_view_->DidUpdateBrowserControls();
}

CompositorWorkerProxyClient*
ChromeClientImpl::CreateCompositorWorkerProxyClient(LocalFrame* frame) {
  WebLocalFrameImpl* web_frame = WebLocalFrameImpl::FromFrame(frame);
  return web_frame->LocalRoot()
      ->FrameWidget()
      ->CreateCompositorWorkerProxyClient();
}

AnimationWorkletProxyClient*
ChromeClientImpl::CreateAnimationWorkletProxyClient(LocalFrame* frame) {
  WebLocalFrameImpl* web_frame = WebLocalFrameImpl::FromFrame(frame);
  return web_frame->LocalRoot()
      ->FrameWidget()
      ->CreateAnimationWorkletProxyClient();
}

void ChromeClientImpl::RegisterPopupOpeningObserver(
    PopupOpeningObserver* observer) {
  DCHECK(observer);
  popup_opening_observers_.push_back(observer);
}

void ChromeClientImpl::UnregisterPopupOpeningObserver(
    PopupOpeningObserver* observer) {
  size_t index = popup_opening_observers_.Find(observer);
  DCHECK_NE(index, kNotFound);
  popup_opening_observers_.erase(index);
}

void ChromeClientImpl::NotifyPopupOpeningObservers() const {
  const Vector<PopupOpeningObserver*> observers(popup_opening_observers_);
  for (const auto& observer : observers)
    observer->WillOpenPopup();
}

FloatSize ChromeClientImpl::ElasticOverscroll() const {
  return web_view_->ElasticOverscroll();
}

void ChromeClientImpl::DidObserveNonGetFetchFromScript() const {
  if (web_view_->PageImportanceSignals())
    web_view_->PageImportanceSignals()->SetIssuedNonGetFetchFromScript();
}

std::unique_ptr<WebFrameScheduler> ChromeClientImpl::CreateFrameScheduler(
    BlameContext* blame_context) {
  return web_view_->Scheduler()->CreateFrameScheduler(blame_context);
}

double ChromeClientImpl::LastFrameTimeMonotonic() const {
  return web_view_->LastFrameTimeMonotonic();
}

void ChromeClientImpl::InstallSupplements(LocalFrame& frame) {
  WebLocalFrameImpl* web_frame = WebLocalFrameImpl::FromFrame(&frame);
  WebFrameClient* client = web_frame->Client();
  DCHECK(client);
  ProvidePushControllerTo(frame, client->PushClient());
  ProvideUserMediaTo(frame,
                     UserMediaClientImpl::Create(client->UserMediaClient()));
  ProvideIndexedDBClientTo(frame, IndexedDBClientImpl::Create(frame));
  ProvideLocalFileSystemTo(frame, LocalFileSystemClient::Create());
  NavigatorContentUtils::ProvideTo(
      *frame.DomWindow()->navigator(),
      NavigatorContentUtilsClientImpl::Create(web_frame));

  ScreenOrientationControllerImpl::ProvideTo(
      frame, client->GetWebScreenOrientationClient());
  if (RuntimeEnabledFeatures::presentationEnabled())
    PresentationController::ProvideTo(frame, client->PresentationClient());
  if (RuntimeEnabledFeatures::audioOutputDevicesEnabled()) {
    ProvideAudioOutputDeviceClientTo(frame,
                                     new AudioOutputDeviceClientImpl(frame));
  }
  InstalledAppController::ProvideTo(frame, client->GetRelatedAppsFetcher());
}

}  // namespace blink
