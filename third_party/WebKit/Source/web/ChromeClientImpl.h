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

#ifndef ChromeClientImpl_h
#define ChromeClientImpl_h

#include <memory>
#include "core/page/ChromeClient.h"
#include "core/page/WindowFeatures.h"
#include "platform/graphics/TouchAction.h"
#include "public/web/WebNavigationPolicy.h"
#include "web/WebExport.h"

namespace blink {

class PagePopup;
class PagePopupClient;
class WebViewBase;
struct WebCursorInfo;

// Handles window-level notifications from core on behalf of a WebView.
class WEB_EXPORT ChromeClientImpl final : public ChromeClient {
 public:
  static ChromeClientImpl* Create(WebViewBase*);
  ~ChromeClientImpl() override;

  void* WebView() const override;

  // ChromeClient methods:
  void ChromeDestroyed() override;
  void SetWindowRect(const IntRect&, LocalFrame&) override;
  IntRect RootWindowRect() override;
  IntRect PageRect() override;
  void Focus() override;
  bool CanTakeFocus(WebFocusType) override;
  void TakeFocus(WebFocusType) override;
  void FocusedNodeChanged(Node* from_node, Node* to_node) override;
  void BeginLifecycleUpdates() override;
  bool HadFormInteraction() const override;
  void StartDragging(LocalFrame*,
                     const WebDragData&,
                     WebDragOperationsMask,
                     const WebImage& drag_image,
                     const WebPoint& drag_image_offset) override;
  bool AcceptsLoadDrops() const override;
  Page* CreateWindow(LocalFrame*,
                     const FrameLoadRequest&,
                     const WindowFeatures&,
                     NavigationPolicy) override;
  void Show(NavigationPolicy) override;
  void DidOverscroll(const FloatSize& overscroll_delta,
                     const FloatSize& accumulated_overscroll,
                     const FloatPoint& position_in_viewport,
                     const FloatSize& velocity_in_viewport) override;
  void SetToolbarsVisible(bool) override;
  bool ToolbarsVisible() override;
  void SetStatusbarVisible(bool) override;
  bool StatusbarVisible() override;
  void SetScrollbarsVisible(bool) override;
  bool ScrollbarsVisible() override;
  void SetMenubarVisible(bool) override;
  bool MenubarVisible() override;
  void SetResizable(bool) override;
  bool ShouldReportDetailedMessageForSource(LocalFrame&,
                                            const String&) override;
  void AddMessageToConsole(LocalFrame*,
                           MessageSource,
                           MessageLevel,
                           const String& message,
                           unsigned line_number,
                           const String& source_id,
                           const String& stack_trace) override;
  bool CanOpenBeforeUnloadConfirmPanel() override;
  bool OpenBeforeUnloadConfirmPanelDelegate(LocalFrame*,
                                            bool is_reload) override;
  void CloseWindowSoon() override;
  bool OpenJavaScriptAlertDelegate(LocalFrame*, const String&) override;
  bool OpenJavaScriptConfirmDelegate(LocalFrame*, const String&) override;
  bool OpenJavaScriptPromptDelegate(LocalFrame*,
                                    const String& message,
                                    const String& default_value,
                                    String& result) override;
  void SetStatusbarText(const String& message) override;
  bool TabsToLinks() override;
  void InvalidateRect(const IntRect&) override;
  void ScheduleAnimation(const PlatformFrameView*) override;
  IntRect ViewportToScreen(const IntRect&,
                           const PlatformFrameView*) const override;
  float WindowToViewportScalar(const float) const override;
  WebScreenInfo GetScreenInfo() const override;
  WTF::Optional<IntRect> VisibleContentRectForPainting() const override;
  void ContentsSizeChanged(LocalFrame*, const IntSize&) const override;
  void PageScaleFactorChanged() const override;
  float ClampPageScaleFactorToLimits(float scale) const override;
  void MainFrameScrollOffsetChanged() const override;
  void ResizeAfterLayout(LocalFrame*) const override;
  void LayoutUpdated(LocalFrame*) const override;
  void ShowMouseOverURL(const HitTestResult&) override;
  void SetToolTip(LocalFrame&, const String&, TextDirection) override;
  void DispatchViewportPropertiesDidChange(
      const ViewportDescription&) const override;
  void PrintDelegate(LocalFrame*) override;
  void AnnotatedRegionsChanged() override;
  ColorChooser* OpenColorChooser(LocalFrame*,
                                 ColorChooserClient*,
                                 const Color&) override;
  DateTimeChooser* OpenDateTimeChooser(
      DateTimeChooserClient*,
      const DateTimeChooserParameters&) override;
  void OpenFileChooser(LocalFrame*, PassRefPtr<FileChooser>) override;
  void EnumerateChosenDirectory(FileChooser*) override;
  void SetCursor(const Cursor&, LocalFrame*) override;
  void SetCursorOverridden(bool) override;
  Cursor LastSetCursorForTesting() const override;
  // The client keeps track of which touch/mousewheel event types have handlers,
  // and if they do, whether the handlers are passive and/or blocking. This
  // allows the client to know which optimizations can be used for the
  // associated event classes.
  void SetEventListenerProperties(LocalFrame*,
                                  WebEventListenerClass,
                                  WebEventListenerProperties) override;
  WebEventListenerProperties EventListenerProperties(
      LocalFrame*,
      WebEventListenerClass) const override;
  void UpdateEventRectsForSubframeIfNecessary(LocalFrame*);
  // Informs client about the existence of handlers for scroll events so
  // appropriate scroll optimizations can be chosen.
  void SetHasScrollEventHandlers(LocalFrame*, bool has_event_handlers) override;
  void SetTouchAction(LocalFrame*, TouchAction) override;

  void AttachRootGraphicsLayer(GraphicsLayer*, LocalFrame* local_root) override;

  void AttachRootLayer(WebLayer*, LocalFrame* local_root) override;

  void AttachCompositorAnimationTimeline(CompositorAnimationTimeline*,
                                         LocalFrame*) override;
  void DetachCompositorAnimationTimeline(CompositorAnimationTimeline*,
                                         LocalFrame*) override;

  void EnterFullscreen(LocalFrame&) override;
  void ExitFullscreen(LocalFrame&) override;
  void FullscreenElementChanged(Element*, Element*) override;

  void ClearCompositedSelection(LocalFrame*) override;
  void UpdateCompositedSelection(LocalFrame*,
                                 const CompositedSelection&) override;

  // ChromeClient methods:
  void PostAccessibilityNotification(AXObject*,
                                     AXObjectCache::AXNotification) override;
  String AcceptLanguages() override;
  void SetCursorForPlugin(const WebCursorInfo&, LocalFrame*) override;

  // ChromeClientImpl:
  void SetNewWindowNavigationPolicy(WebNavigationPolicy);

  bool HasOpenedPopup() const override;
  PopupMenu* OpenPopupMenu(LocalFrame&, HTMLSelectElement&) override;
  PagePopup* OpenPagePopup(PagePopupClient*) override;
  void ClosePagePopup(PagePopup*) override;
  DOMWindow* PagePopupWindowForTesting() const override;

  bool ShouldOpenModalDialogDuringPageDismissal(
      LocalFrame&,
      DialogType,
      const String& dialog_message,
      Document::PageDismissalType) const override;

  bool RequestPointerLock(LocalFrame*) override;
  void RequestPointerUnlock(LocalFrame*) override;

  // AutofillClient pass throughs:
  void DidAssociateFormControlsAfterLoad(LocalFrame*) override;
  void HandleKeyboardEventOnTextField(HTMLInputElement&,
                                      KeyboardEvent&) override;
  void DidChangeValueInTextField(HTMLFormControlElement&) override;
  void DidEndEditingOnTextField(HTMLInputElement&) override;
  void OpenTextDataListChooser(HTMLInputElement&) override;
  void TextFieldDataListChanged(HTMLInputElement&) override;
  void AjaxSucceeded(LocalFrame*) override;

  void ShowVirtualKeyboardOnElementFocus(LocalFrame&) override;

  void RegisterViewportLayers() const override;

  void ShowUnhandledTapUIIfNeeded(IntPoint, Node*, bool) override;
  void OnMouseDown(Node&) override;
  void DidUpdateBrowserControls() const override;

  CompositorWorkerProxyClient* CreateCompositorWorkerProxyClient(
      LocalFrame*) override;
  AnimationWorkletProxyClient* CreateAnimationWorkletProxyClient(
      LocalFrame*) override;

  FloatSize ElasticOverscroll() const override;

  void DidObserveNonGetFetchFromScript() const override;

  std::unique_ptr<WebFrameScheduler> CreateFrameScheduler(
      BlameContext*) override;

  double LastFrameTimeMonotonic() const override;

  void RegisterPopupOpeningObserver(PopupOpeningObserver*) override;
  void UnregisterPopupOpeningObserver(PopupOpeningObserver*) override;
  void NotifyPopupOpeningObservers() const override;

  void InstallSupplements(LocalFrame&) override;

  WebLayerTreeView* GetWebLayerTreeView(LocalFrame*) override;

  WebLocalFrameBase* GetWebLocalFrameBase(LocalFrame*) override;

  WebRemoteFrameBase* GetWebRemoteFrameBase(RemoteFrame&) override;

 private:
  explicit ChromeClientImpl(WebViewBase*);

  bool IsChromeClientImpl() const override { return true; }

  void SetCursor(const WebCursorInfo&, LocalFrame*);

  WebViewBase* web_view_;  // Weak pointer.
  WindowFeatures window_features_;
  Vector<PopupOpeningObserver*> popup_opening_observers_;
  Cursor last_set_mouse_cursor_for_testing_;
  bool cursor_overridden_;
  bool did_request_non_empty_tool_tip_;
};

DEFINE_TYPE_CASTS(ChromeClientImpl,
                  ChromeClient,
                  client,
                  client->IsChromeClientImpl(),
                  client.IsChromeClientImpl());

}  // namespace blink

#endif
