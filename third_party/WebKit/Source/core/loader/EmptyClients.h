/*
 * Copyright (C) 2006 Eric Seidel (eric@webkit.org)
 * Copyright (C) 2008, 2009, 2010, 2011, 2012 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2012 Samsung Electronics. All rights reserved.
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

#ifndef EmptyClients_h
#define EmptyClients_h

#include <memory>

#include "core/CoreExport.h"
#include "core/frame/ContentSettingsClient.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/RemoteFrameClient.h"
#include "core/page/ChromeClient.h"
#include "core/page/ContextMenuClient.h"
#include "core/page/EditorClient.h"
#include "core/page/Page.h"
#include "core/page/SpellCheckerClient.h"
#include "platform/DragImage.h"
#include "platform/WebFrameScheduler.h"
#include "platform/geometry/FloatPoint.h"
#include "platform/geometry/FloatRect.h"
#include "platform/geometry/IntRect.h"
#include "platform/graphics/TouchAction.h"
#include "platform/heap/Handle.h"
#include "platform/loader/fetch/ResourceError.h"
#include "platform/text/TextCheckerClient.h"
#include "platform/wtf/Forward.h"
#include "public/platform/Platform.h"
#include "public/platform/WebFocusType.h"
#include "public/platform/WebScreenInfo.h"
#include "public/platform/WebURLLoader.h"
#include "v8/include/v8.h"

/*
 This file holds empty Client stubs for use by WebCore.

 Viewless element needs to create a dummy Page->LocalFrame->FrameView tree for
 use in parsing or executing JavaScript. This tree depends heavily on Clients
 (usually provided by WebKit classes).

 This file was first created for SVGImage as it had no way to access the current
 Page (nor should it, since Images are not tied to a page). See
 http://bugs.webkit.org/show_bug.cgi?id=5971 for the original discussion about
 this file.

 Ideally, whenever you change a Client class, you should add a stub here.
 Brittle, yes. Unfortunate, yes. Hopefully temporary.
*/

namespace blink {

class CORE_EXPORT EmptyChromeClient : public ChromeClient {
 public:
  static EmptyChromeClient* Create() { return new EmptyChromeClient; }

  ~EmptyChromeClient() override {}
  void ChromeDestroyed() override {}

  void* WebView() const override { return nullptr; }
  void SetWindowRect(const IntRect&, LocalFrame&) override {}
  IntRect RootWindowRect() override { return IntRect(); }

  IntRect PageRect() override { return IntRect(); }

  void Focus() override {}

  bool CanTakeFocus(WebFocusType) override { return false; }
  void TakeFocus(WebFocusType) override {}

  void FocusedNodeChanged(Node*, Node*) override {}
  Page* CreateWindow(LocalFrame*,
                     const FrameLoadRequest&,
                     const WindowFeatures&,
                     NavigationPolicy) override {
    return nullptr;
  }
  void Show(NavigationPolicy) override {}

  void DidOverscroll(const FloatSize&,
                     const FloatSize&,
                     const FloatPoint&,
                     const FloatSize&) override {}

  void BeginLifecycleUpdates() override {}

  bool HadFormInteraction() const override { return false; }

  void StartDragging(LocalFrame*,
                     const WebDragData&,
                     WebDragOperationsMask,
                     const WebImage& drag_image,
                     const WebPoint& drag_image_offset) {}
  bool AcceptsLoadDrops() const override { return true; }

  void SetToolbarsVisible(bool) override {}
  bool ToolbarsVisible() override { return false; }

  void SetStatusbarVisible(bool) override {}
  bool StatusbarVisible() override { return false; }

  void SetScrollbarsVisible(bool) override {}
  bool ScrollbarsVisible() override { return false; }

  void SetMenubarVisible(bool) override {}
  bool MenubarVisible() override { return false; }

  void SetResizable(bool) override {}

  bool ShouldReportDetailedMessageForSource(LocalFrame&,
                                            const String&) override {
    return false;
  }
  void AddMessageToConsole(LocalFrame*,
                           MessageSource,
                           MessageLevel,
                           const String&,
                           unsigned,
                           const String&,
                           const String&) override {}

  bool CanOpenBeforeUnloadConfirmPanel() override { return false; }
  bool OpenBeforeUnloadConfirmPanelDelegate(LocalFrame*, bool) override {
    return true;
  }

  void CloseWindowSoon() override {}

  bool OpenJavaScriptAlertDelegate(LocalFrame*, const String&) override {
    return false;
  }
  bool OpenJavaScriptConfirmDelegate(LocalFrame*, const String&) override {
    return false;
  }
  bool OpenJavaScriptPromptDelegate(LocalFrame*,
                                    const String&,
                                    const String&,
                                    String&) override {
    return false;
  }

  bool HasOpenedPopup() const override { return false; }
  PopupMenu* OpenPopupMenu(LocalFrame&, HTMLSelectElement&) override;
  PagePopup* OpenPagePopup(PagePopupClient*) override { return nullptr; }
  void ClosePagePopup(PagePopup*) override{};
  DOMWindow* PagePopupWindowForTesting() const override { return nullptr; }

  void SetStatusbarText(const String&) override {}

  bool TabsToLinks() override { return false; }

  void InvalidateRect(const IntRect&) override {}
  void ScheduleAnimation(const PlatformFrameView*) override {}

  IntRect ViewportToScreen(const IntRect& r,
                           const PlatformFrameView*) const override {
    return r;
  }
  float WindowToViewportScalar(const float s) const override { return s; }
  WebScreenInfo GetScreenInfo() const override { return WebScreenInfo(); }
  void ContentsSizeChanged(LocalFrame*, const IntSize&) const override {}

  void ShowMouseOverURL(const HitTestResult&) override {}

  void SetToolTip(LocalFrame&, const String&, TextDirection) override {}

  void PrintDelegate(LocalFrame*) override {}

  void EnumerateChosenDirectory(FileChooser*) override {}

  ColorChooser* OpenColorChooser(LocalFrame*,
                                 ColorChooserClient*,
                                 const Color&) override;
  DateTimeChooser* OpenDateTimeChooser(
      DateTimeChooserClient*,
      const DateTimeChooserParameters&) override;
  void OpenTextDataListChooser(HTMLInputElement&) override;

  void OpenFileChooser(LocalFrame*, PassRefPtr<FileChooser>) override;

  void SetCursor(const Cursor&, LocalFrame* local_root) override {}
  void SetCursorOverridden(bool) override {}
  Cursor LastSetCursorForTesting() const override { return PointerCursor(); }

  void AttachRootGraphicsLayer(GraphicsLayer*, LocalFrame* local_root) override;
  void AttachRootLayer(WebLayer*, LocalFrame* local_root) override {}

  void SetEventListenerProperties(LocalFrame*,
                                  WebEventListenerClass,
                                  WebEventListenerProperties) override {}
  WebEventListenerProperties EventListenerProperties(
      LocalFrame*,
      WebEventListenerClass event_class) const override {
    return WebEventListenerProperties::kNothing;
  }
  void UpdateEventRectsForSubframeIfNecessary(LocalFrame* frame) override {}
  void SetHasScrollEventHandlers(LocalFrame*, bool) override {}

  void SetTouchAction(LocalFrame*, TouchAction) override {}

  void DidAssociateFormControlsAfterLoad(LocalFrame*) override {}

  void AnnotatedRegionsChanged() override {}
  String AcceptLanguages() override;

  CompositorWorkerProxyClient* CreateCompositorWorkerProxyClient(
      LocalFrame*) override {
    return nullptr;
  }

  AnimationWorkletProxyClient* CreateAnimationWorkletProxyClient(
      LocalFrame*) override {
    return nullptr;
  }

  void RegisterPopupOpeningObserver(PopupOpeningObserver*) override {}
  void UnregisterPopupOpeningObserver(PopupOpeningObserver*) override {}
  void NotifyPopupOpeningObservers() const {}

  void SetCursorForPlugin(const WebCursorInfo&, LocalFrame*) override {}

  std::unique_ptr<WebFrameScheduler> CreateFrameScheduler(
      BlameContext*) override;
};

class CORE_EXPORT EmptyLocalFrameClient : public LocalFrameClient {
  WTF_MAKE_NONCOPYABLE(EmptyLocalFrameClient);

 public:
  static EmptyLocalFrameClient* Create() { return new EmptyLocalFrameClient; }
  ~EmptyLocalFrameClient() override {}

  bool HasWebView() const override { return true; }  // mainly for assertions

  bool InShadowTree() const override { return false; }

  Frame* Opener() const override { return 0; }
  void SetOpener(Frame*) override {}

  Frame* Parent() const override { return 0; }
  Frame* Top() const override { return 0; }
  Frame* NextSibling() const override { return 0; }
  Frame* FirstChild() const override { return 0; }
  void WillBeDetached() override {}
  void Detached(FrameDetachType) override {}
  void FrameFocused() const override {}

  void DispatchWillSendRequest(ResourceRequest&) override {}
  void DispatchDidReceiveResponse(const ResourceResponse&) override {}
  void DispatchDidLoadResourceFromMemoryCache(
      const ResourceRequest&,
      const ResourceResponse&) override {}

  void DispatchDidHandleOnloadEvents() override {}
  void DispatchDidReceiveServerRedirectForProvisionalLoad() override {}
  void DispatchWillCommitProvisionalLoad() override {}
  void DispatchDidStartProvisionalLoad(DocumentLoader*,
                                       ResourceRequest&) override {}
  void DispatchDidReceiveTitle(const String&) override {}
  void DispatchDidChangeIcons(IconType) override {}
  void DispatchDidCommitLoad(HistoryItem*, HistoryCommitType) override {}
  void DispatchDidFailProvisionalLoad(const ResourceError&,
                                      HistoryCommitType) override {}
  void DispatchDidFailLoad(const ResourceError&, HistoryCommitType) override {}
  void DispatchDidFinishDocumentLoad() override {}
  void DispatchDidFinishLoad() override {}
  void DispatchDidChangeThemeColor() override {}

  NavigationPolicy DecidePolicyForNavigation(
      const ResourceRequest&,
      DocumentLoader*,
      NavigationType,
      NavigationPolicy,
      bool,
      bool,
      HTMLFormElement*,
      ContentSecurityPolicyDisposition) override;

  void DispatchWillSendSubmitEvent(HTMLFormElement*) override;
  void DispatchWillSubmitForm(HTMLFormElement*) override;

  void DidStartLoading(LoadStartType) override {}
  void ProgressEstimateChanged(double) override {}
  void DidStopLoading() override {}

  void LoadURLExternally(const ResourceRequest&,
                         NavigationPolicy,
                         const String&,
                         bool) override {}
  void LoadErrorPage(int reason) override {}

  DocumentLoader* CreateDocumentLoader(LocalFrame*,
                                       const ResourceRequest&,
                                       const SubstituteData&,
                                       ClientRedirectPolicy) override;

  String UserAgent() override { return ""; }

  String DoNotTrackValue() override { return String(); }

  void TransitionToCommittedForNewPage() override {}

  bool NavigateBackForward(int offset) const override { return false; }
  void DidDisplayInsecureContent() override {}
  void DidContainInsecureFormAction() override {}
  void DidRunInsecureContent(SecurityOrigin*, const KURL&) override {}
  void DidDetectXSS(const KURL&, bool) override {}
  void DidDispatchPingLoader(const KURL&) override {}
  void DidDisplayContentWithCertificateErrors(const KURL&) override {}
  void DidRunContentWithCertificateErrors(const KURL&) override {}
  void SelectorMatchChanged(const Vector<String>&,
                            const Vector<String>&) override {}
  LocalFrame* CreateFrame(const FrameLoadRequest&,
                          const AtomicString&,
                          HTMLFrameOwnerElement*) override;
  PluginView* CreatePlugin(HTMLPlugInElement*,
                           const KURL&,
                           const Vector<String>&,
                           const Vector<String>&,
                           const String&,
                           bool,
                           DetachedPluginPolicy) override;
  bool CanCreatePluginWithoutRenderer(const String& mime_type) const override {
    return false;
  }
  std::unique_ptr<WebMediaPlayer> CreateWebMediaPlayer(
      HTMLMediaElement&,
      const WebMediaPlayerSource&,
      WebMediaPlayerClient*) override;
  WebRemotePlaybackClient* CreateWebRemotePlaybackClient(
      HTMLMediaElement&) override;

  ObjectContentType GetObjectContentType(const KURL&,
                                         const String&,
                                         bool) override {
    return ObjectContentType();
  }

  void DidCreateNewDocument() override {}
  void DispatchDidClearWindowObjectInMainWorld() override {}
  void DocumentElementAvailable() override {}
  void RunScriptsAtDocumentElementAvailable() override {}
  void RunScriptsAtDocumentReady(bool) override {}
  void RunScriptsAtDocumentIdle() override {}

  void DidCreateScriptContext(v8::Local<v8::Context>, int world_id) override {}
  void WillReleaseScriptContext(v8::Local<v8::Context>, int world_id) override {
  }
  bool AllowScriptExtensions() override { return false; }

  WebCookieJar* CookieJar() const override { return 0; }

  std::unique_ptr<WebServiceWorkerProvider> CreateServiceWorkerProvider()
      override;
  ContentSettingsClient& GetContentSettingsClient() override;
  std::unique_ptr<WebApplicationCacheHost> CreateApplicationCacheHost(
      WebApplicationCacheHostClient*) override;

  TextCheckerClient& GetTextCheckerClient() const override;
  std::unique_ptr<WebURLLoader> CreateURLLoader() override {
    // TODO(yhirano): Stop using Platform::CreateURLLoader() here.
    return Platform::Current()->CreateURLLoader();
  }

 protected:
  EmptyLocalFrameClient() {}

  ContentSettingsClient content_settings_client_;
};

class CORE_EXPORT EmptyTextCheckerClient : public TextCheckerClient {
  WTF_MAKE_NONCOPYABLE(EmptyTextCheckerClient);
  USING_FAST_MALLOC(EmptyTextCheckerClient);

 public:
  EmptyTextCheckerClient() {}

  void CheckSpellingOfString(const String&, int*, int*) override {}
  void RequestCheckingOfString(TextCheckingRequest*) override;
  void CancelAllPendingRequests() override;
};

class EmptySpellCheckerClient : public SpellCheckerClient {
  WTF_MAKE_NONCOPYABLE(EmptySpellCheckerClient);
  USING_FAST_MALLOC(EmptySpellCheckerClient);

 public:
  EmptySpellCheckerClient() {}
  ~EmptySpellCheckerClient() override {}

  bool IsSpellCheckingEnabled() override { return false; }
  void ToggleSpellCheckingEnabled() override {}
  void UpdateSpellingUIWithMisspelledWord(const String&) override {}
  void ShowSpellingUI(bool) override {}
  bool SpellingUIIsShowing() override { return false; }
};

class EmptyEditorClient final : public EditorClient {
  WTF_MAKE_NONCOPYABLE(EmptyEditorClient);
  USING_FAST_MALLOC(EmptyEditorClient);

 public:
  EmptyEditorClient() {}
  ~EmptyEditorClient() override {}

  void RespondToChangedContents() override {}
  void RespondToChangedSelection(LocalFrame*, SelectionType) override {}

  bool CanCopyCut(LocalFrame*, bool default_value) const override {
    return default_value;
  }
  bool CanPaste(LocalFrame*, bool default_value) const override {
    return default_value;
  }

  bool HandleKeyboardEvent(LocalFrame*) override { return false; }
};

class EmptyContextMenuClient final : public ContextMenuClient {
  WTF_MAKE_NONCOPYABLE(EmptyContextMenuClient);
  USING_FAST_MALLOC(EmptyContextMenuClient);

 public:
  EmptyContextMenuClient() {}
  ~EmptyContextMenuClient() override {}
  bool ShowContextMenu(const ContextMenu*, bool) override { return false; }
  void ClearContextMenu() override {}
};

class CORE_EXPORT EmptyRemoteFrameClient
    : NON_EXPORTED_BASE(public RemoteFrameClient) {
  WTF_MAKE_NONCOPYABLE(EmptyRemoteFrameClient);

 public:
  EmptyRemoteFrameClient();

  // RemoteFrameClient implementation.
  void Navigate(const ResourceRequest&,
                bool should_replace_current_entry) override {}
  void Reload(FrameLoadType, ClientRedirectPolicy) override {}
  unsigned BackForwardLength() override { return 0; }
  void ForwardPostMessage(MessageEvent*,
                          PassRefPtr<SecurityOrigin> target,
                          LocalFrame* source_frame) const override {}
  void FrameRectsChanged(const IntRect& frame_rect) override {}
  void UpdateRemoteViewportIntersection(
      const IntRect& viewport_intersection) override {}
  void AdvanceFocus(WebFocusType, LocalFrame* source) override {}
  void VisibilityChanged(bool visible) override {}

  // FrameClient implementation.
  bool InShadowTree() const override { return false; }
  void WillBeDetached() override {}
  void Detached(FrameDetachType) override {}
  Frame* Opener() const override { return nullptr; }
  void SetOpener(Frame*) override {}
  Frame* Parent() const override { return nullptr; }
  Frame* Top() const override { return nullptr; }
  Frame* NextSibling() const override { return nullptr; }
  Frame* FirstChild() const override { return nullptr; }
  void FrameFocused() const override {}
};

CORE_EXPORT void FillWithEmptyClients(Page::PageClients&);

}  // namespace blink

#endif  // EmptyClients_h
