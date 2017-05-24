/*
 * Copyright (C) 2006, 2007, 2008, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
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

#include "core/frame/LocalDOMWindow.h"

#include "bindings/core/v8/ScriptController.h"
#include "bindings/core/v8/SourceLocation.h"
#include "bindings/core/v8/WindowProxy.h"
#include "core/css/CSSComputedStyleDeclaration.h"
#include "core/css/CSSRuleList.h"
#include "core/css/DOMWindowCSS.h"
#include "core/css/MediaQueryList.h"
#include "core/css/MediaQueryMatcher.h"
#include "core/css/StyleMedia.h"
#include "core/css/resolver/StyleResolver.h"
#include "core/dom/DOMImplementation.h"
#include "core/dom/DocumentUserGestureToken.h"
#include "core/dom/FrameRequestCallback.h"
#include "core/dom/SandboxFlags.h"
#include "core/dom/SinkDocument.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/dom/custom/CustomElementRegistry.h"
#include "core/editing/Editor.h"
#include "core/events/DOMWindowEventQueue.h"
#include "core/events/HashChangeEvent.h"
#include "core/events/MessageEvent.h"
#include "core/events/PageTransitionEvent.h"
#include "core/events/PopStateEvent.h"
#include "core/events/ScopedEventQueue.h"
#include "core/frame/BarProp.h"
#include "core/frame/DOMVisualViewport.h"
#include "core/frame/EventHandlerRegistry.h"
#include "core/frame/External.h"
#include "core/frame/FrameConsole.h"
#include "core/frame/FrameView.h"
#include "core/frame/History.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/Navigator.h"
#include "core/frame/Screen.h"
#include "core/frame/ScrollToOptions.h"
#include "core/frame/Settings.h"
#include "core/frame/SuspendableTimer.h"
#include "core/frame/VisualViewport.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/input/EventHandler.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/inspector/InspectorTraceEvents.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/appcache/ApplicationCache.h"
#include "core/page/ChromeClient.h"
#include "core/page/CreateWindow.h"
#include "core/page/Page.h"
#include "core/page/WindowFeatures.h"
#include "core/page/scrolling/ScrollingCoordinator.h"
#include "core/probe/CoreProbes.h"
#include "core/timing/DOMWindowPerformance.h"
#include "core/timing/Performance.h"
#include "platform/EventDispatchForbiddenScope.h"
#include "platform/Histogram.h"
#include "platform/WebFrameScheduler.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/scroll/ScrollbarTheme.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "platform/weborigin/Suborigin.h"
#include "public/platform/Platform.h"
#include "public/platform/WebScreenInfo.h"
#include "public/platform/site_engagement.mojom-blink.h"

#include <memory>

namespace blink {

// Timeout for link preloads to be used after window.onload
static const int kUnusedPreloadTimeoutInSeconds = 3;

class PostMessageTimer final
    : public GarbageCollectedFinalized<PostMessageTimer>,
      public SuspendableTimer {
  USING_GARBAGE_COLLECTED_MIXIN(PostMessageTimer);

 public:
  PostMessageTimer(LocalDOMWindow& window,
                   MessageEvent* event,
                   PassRefPtr<SecurityOrigin> target_origin,
                   std::unique_ptr<SourceLocation> location,
                   UserGestureToken* user_gesture_token)
      : SuspendableTimer(window.document(), TaskType::kPostedMessage),
        event_(event),
        window_(&window),
        target_origin_(std::move(target_origin)),
        location_(std::move(location)),
        user_gesture_token_(user_gesture_token),
        disposal_allowed_(true) {
    probe::AsyncTaskScheduled(window.document(), "postMessage", this);
  }

  MessageEvent* Event() const { return event_; }
  SecurityOrigin* TargetOrigin() const { return target_origin_.Get(); }
  std::unique_ptr<SourceLocation> TakeLocation() {
    return std::move(location_);
  }
  UserGestureToken* GetUserGestureToken() const {
    return user_gesture_token_.Get();
  }
  void ContextDestroyed(ExecutionContext* destroyed_context) override {
    SuspendableTimer::ContextDestroyed(destroyed_context);

    if (disposal_allowed_)
      Dispose();
  }

  // Eager finalization is needed to promptly stop this timer object.
  // (see DOMTimer comment for more.)
  EAGERLY_FINALIZE();
  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(event_);
    visitor->Trace(window_);
    SuspendableTimer::Trace(visitor);
  }

  // TODO(alexclarke): Override timerTaskRunner() to pass in a document specific
  // default task runner.

 private:
  void Fired() override {
    probe::AsyncTask async_task(window_->document(), this);
    disposal_allowed_ = false;
    window_->PostMessageTimerFired(this);
    Dispose();
    // Oilpan optimization: unregister as an observer right away.
    ClearContext();
  }

  void Dispose() { window_->RemovePostMessageTimer(this); }

  Member<MessageEvent> event_;
  Member<LocalDOMWindow> window_;
  RefPtr<SecurityOrigin> target_origin_;
  std::unique_ptr<SourceLocation> location_;
  RefPtr<UserGestureToken> user_gesture_token_;
  bool disposal_allowed_;
};

static void UpdateSuddenTerminationStatus(
    LocalDOMWindow* dom_window,
    bool added_listener,
    LocalFrameClient::SuddenTerminationDisablerType disabler_type) {
  Platform::Current()->SuddenTerminationChanged(!added_listener);
  if (dom_window->GetFrame() && dom_window->GetFrame()->Loader().Client())
    dom_window->GetFrame()->Loader().Client()->SuddenTerminationDisablerChanged(
        added_listener, disabler_type);
}

using DOMWindowSet = PersistentHeapHashCountedSet<WeakMember<LocalDOMWindow>>;

static DOMWindowSet& WindowsWithUnloadEventListeners() {
  DEFINE_STATIC_LOCAL(DOMWindowSet, windows_with_unload_event_listeners, ());
  return windows_with_unload_event_listeners;
}

static DOMWindowSet& WindowsWithBeforeUnloadEventListeners() {
  DEFINE_STATIC_LOCAL(DOMWindowSet, windows_with_before_unload_event_listeners,
                      ());
  return windows_with_before_unload_event_listeners;
}

static void AddUnloadEventListener(LocalDOMWindow* dom_window) {
  DOMWindowSet& set = WindowsWithUnloadEventListeners();
  if (set.IsEmpty()) {
    UpdateSuddenTerminationStatus(dom_window, true,
                                  LocalFrameClient::kUnloadHandler);
  }

  set.insert(dom_window);
}

static void RemoveUnloadEventListener(LocalDOMWindow* dom_window) {
  DOMWindowSet& set = WindowsWithUnloadEventListeners();
  DOMWindowSet::iterator it = set.find(dom_window);
  if (it == set.end())
    return;
  set.erase(it);
  if (set.IsEmpty()) {
    UpdateSuddenTerminationStatus(dom_window, false,
                                  LocalFrameClient::kUnloadHandler);
  }
}

static void RemoveAllUnloadEventListeners(LocalDOMWindow* dom_window) {
  DOMWindowSet& set = WindowsWithUnloadEventListeners();
  DOMWindowSet::iterator it = set.find(dom_window);
  if (it == set.end())
    return;
  set.RemoveAll(it);
  if (set.IsEmpty()) {
    UpdateSuddenTerminationStatus(dom_window, false,
                                  LocalFrameClient::kUnloadHandler);
  }
}

static void AddBeforeUnloadEventListener(LocalDOMWindow* dom_window) {
  DOMWindowSet& set = WindowsWithBeforeUnloadEventListeners();
  if (set.IsEmpty()) {
    UpdateSuddenTerminationStatus(dom_window, true,
                                  LocalFrameClient::kBeforeUnloadHandler);
  }

  set.insert(dom_window);
}

static void RemoveBeforeUnloadEventListener(LocalDOMWindow* dom_window) {
  DOMWindowSet& set = WindowsWithBeforeUnloadEventListeners();
  DOMWindowSet::iterator it = set.find(dom_window);
  if (it == set.end())
    return;
  set.erase(it);
  if (set.IsEmpty()) {
    UpdateSuddenTerminationStatus(dom_window, false,
                                  LocalFrameClient::kBeforeUnloadHandler);
  }
}

static void RemoveAllBeforeUnloadEventListeners(LocalDOMWindow* dom_window) {
  DOMWindowSet& set = WindowsWithBeforeUnloadEventListeners();
  DOMWindowSet::iterator it = set.find(dom_window);
  if (it == set.end())
    return;
  set.RemoveAll(it);
  if (set.IsEmpty()) {
    UpdateSuddenTerminationStatus(dom_window, false,
                                  LocalFrameClient::kBeforeUnloadHandler);
  }
}

static bool AllowsBeforeUnloadListeners(LocalDOMWindow* window) {
  DCHECK(window);
  LocalFrame* frame = window->GetFrame();
  if (!frame)
    return false;
  return frame->IsMainFrame();
}

unsigned LocalDOMWindow::PendingUnloadEventListeners() const {
  return WindowsWithUnloadEventListeners().count(
      const_cast<LocalDOMWindow*>(this));
}

bool LocalDOMWindow::AllowPopUp(LocalFrame& first_frame) {
  if (UserGestureIndicator::ProcessingUserGesture())
    return true;

  Settings* settings = first_frame.GetSettings();
  return settings && settings->GetJavaScriptCanOpenWindowsAutomatically();
}

bool LocalDOMWindow::AllowPopUp() {
  return GetFrame() && AllowPopUp(*GetFrame());
}

LocalDOMWindow::LocalDOMWindow(LocalFrame& frame)
    : DOMWindow(frame),
      visual_viewport_(DOMVisualViewport::Create(this)),
      unused_preloads_timer_(
          TaskRunnerHelper::Get(TaskType::kUnspecedTimer, &frame),
          this,
          &LocalDOMWindow::WarnUnusedPreloads),
      should_print_when_finished_loading_(false),
      custom_elements_(this, nullptr) {}

void LocalDOMWindow::ClearDocument() {
  if (!document_)
    return;

  DCHECK(!document_->IsActive());

  // FIXME: This should be part of SuspendableObject shutdown
  ClearEventQueue();

  unused_preloads_timer_.Stop();
  document_->ClearDOMWindow();
  document_ = nullptr;
}

void LocalDOMWindow::ClearEventQueue() {
  if (!event_queue_)
    return;
  event_queue_->Close();
  event_queue_.Clear();
}

void LocalDOMWindow::AcceptLanguagesChanged() {
  if (navigator_)
    navigator_->SetLanguagesChanged();

  DispatchEvent(Event::Create(EventTypeNames::languagechange));
}

Document* LocalDOMWindow::CreateDocument(const String& mime_type,
                                         const DocumentInit& init,
                                         bool force_xhtml) {
  Document* document = nullptr;
  if (force_xhtml) {
    // This is a hack for XSLTProcessor. See
    // XSLTProcessor::createDocumentFromSource().
    document = Document::Create(init);
  } else {
    document = DOMImplementation::createDocument(
        mime_type, init,
        init.GetFrame() ? init.GetFrame()->InViewSourceMode() : false);
    if (document->IsPluginDocument() && document->IsSandboxed(kSandboxPlugins))
      document = SinkDocument::Create(init);
  }

  return document;
}

LocalDOMWindow* LocalDOMWindow::From(const ScriptState* script_state) {
  v8::HandleScope scope(script_state->GetIsolate());
  return blink::ToLocalDOMWindow(script_state->GetContext());
}

Document* LocalDOMWindow::InstallNewDocument(const String& mime_type,
                                             const DocumentInit& init,
                                             bool force_xhtml) {
  DCHECK_EQ(init.GetFrame(), GetFrame());

  ClearDocument();

  document_ = CreateDocument(mime_type, init, force_xhtml);
  event_queue_ = DOMWindowEventQueue::Create(document_.Get());
  document_->Initialize();

  if (!GetFrame())
    return document_;

  GetFrame()->GetScriptController().UpdateDocument();
  document_->UpdateViewportDescription();

  if (GetFrame()->GetPage() && GetFrame()->View()) {
    if (ScrollingCoordinator* scrolling_coordinator =
            GetFrame()->GetPage()->GetScrollingCoordinator()) {
      scrolling_coordinator->ScrollableAreaScrollbarLayerDidChange(
          GetFrame()->View(), kHorizontalScrollbar);
      scrolling_coordinator->ScrollableAreaScrollbarLayerDidChange(
          GetFrame()->View(), kVerticalScrollbar);
      scrolling_coordinator->ScrollableAreaScrollLayerDidChange(
          GetFrame()->View());
    }
  }

  GetFrame()->Selection().UpdateSecureKeyboardEntryIfActive();

  if (GetFrame()->IsCrossOriginSubframe())
    document_->RecordDeferredLoadReason(WouldLoadReason::kCreated);

  return document_;
}

EventQueue* LocalDOMWindow::GetEventQueue() const {
  return event_queue_.Get();
}

void LocalDOMWindow::EnqueueWindowEvent(Event* event) {
  if (!event_queue_)
    return;
  event->SetTarget(this);
  event_queue_->EnqueueEvent(event);
}

void LocalDOMWindow::EnqueueDocumentEvent(Event* event) {
  if (!event_queue_)
    return;
  event->SetTarget(document_.Get());
  event_queue_->EnqueueEvent(event);
}

void LocalDOMWindow::DispatchWindowLoadEvent() {
#if DCHECK_IS_ON()
  DCHECK(!EventDispatchForbiddenScope::IsEventDispatchForbidden());
#endif
  // Delay 'load' event if we are in EventQueueScope.  This is a short-term
  // workaround to avoid Editing code crashes.  We should always dispatch
  // 'load' event asynchronously.  crbug.com/569511.
  if (ScopedEventQueue::Instance()->ShouldQueueEvents() && document_) {
    TaskRunnerHelper::Get(TaskType::kNetworking, document_)
        ->PostTask(BLINK_FROM_HERE,
                   WTF::Bind(&LocalDOMWindow::DispatchLoadEvent,
                             WrapPersistent(this)));
    return;
  }
  DispatchLoadEvent();
}

void LocalDOMWindow::DocumentWasClosed() {
  DispatchWindowLoadEvent();
  EnqueuePageshowEvent(kPageshowEventNotPersisted);
  if (pending_state_object_)
    EnqueuePopstateEvent(std::move(pending_state_object_));
}

void LocalDOMWindow::EnqueuePageshowEvent(PageshowEventPersistence persisted) {
  // FIXME: https://bugs.webkit.org/show_bug.cgi?id=36334 Pageshow event needs
  // to fire asynchronously.  As per spec pageshow must be triggered
  // asynchronously.  However to be compatible with other browsers blink fires
  // pageshow synchronously unless we are in EventQueueScope.
  if (ScopedEventQueue::Instance()->ShouldQueueEvents() && document_) {
      EnqueueWindowEvent(
        PageTransitionEvent::Create(EventTypeNames::pageshow, persisted));
    return;
  }
  DispatchEvent(
      PageTransitionEvent::Create(EventTypeNames::pageshow, persisted),
      document_.Get());
}

void LocalDOMWindow::EnqueueHashchangeEvent(const String& old_url,
                                            const String& new_url) {
  EnqueueWindowEvent(HashChangeEvent::Create(old_url, new_url));
}

void LocalDOMWindow::EnqueuePopstateEvent(
    PassRefPtr<SerializedScriptValue> state_object) {
  // FIXME: https://bugs.webkit.org/show_bug.cgi?id=36202 Popstate event needs
  // to fire asynchronously
  DispatchEvent(PopStateEvent::Create(std::move(state_object), history()));
}

void LocalDOMWindow::StatePopped(
    PassRefPtr<SerializedScriptValue> state_object) {
  if (!GetFrame())
    return;

  // Per step 11 of section 6.5.9 (history traversal) of the HTML5 spec, we
  // defer firing of popstate until we're in the complete state.
  if (document()->IsLoadCompleted())
    EnqueuePopstateEvent(std::move(state_object));
  else
    pending_state_object_ = std::move(state_object);
}

LocalDOMWindow::~LocalDOMWindow() {
  // Cleared when detaching document.
  DCHECK(!event_queue_);
}

void LocalDOMWindow::Dispose() {
  // Oilpan: should the LocalDOMWindow be GCed along with its LocalFrame without
  // the frame having first notified its observers of imminent destruction, the
  // LocalDOMWindow will not have had an opportunity to remove event listeners.
  //
  // Arrange for that removal to happen using a prefinalizer action. Making
  // LocalDOMWindow eager finalizable is problematic as other eagerly finalized
  // objects may well want to access their associated LocalDOMWindow from their
  // destructors.
  if (!GetFrame())
    return;

  RemoveAllEventListeners();
}

ExecutionContext* LocalDOMWindow::GetExecutionContext() const {
  return document_.Get();
}

const LocalDOMWindow* LocalDOMWindow::ToLocalDOMWindow() const {
  return this;
}

LocalDOMWindow* LocalDOMWindow::ToLocalDOMWindow() {
  return this;
}

MediaQueryList* LocalDOMWindow::matchMedia(const String& media) {
  return document() ? document()->GetMediaQueryMatcher().MatchMedia(media)
                    : nullptr;
}

void LocalDOMWindow::FrameDestroyed() {
  RemoveAllEventListeners();
  DisconnectFromFrame();
}

void LocalDOMWindow::RegisterEventListenerObserver(
    EventListenerObserver* event_listener_observer) {
  event_listener_observers_.insert(event_listener_observer);
}

void LocalDOMWindow::Reset() {
  DCHECK(document());
  DCHECK(document()->IsContextDestroyed());
  FrameDestroyed();

  screen_ = nullptr;
  history_ = nullptr;
  locationbar_ = nullptr;
  menubar_ = nullptr;
  personalbar_ = nullptr;
  scrollbars_ = nullptr;
  statusbar_ = nullptr;
  toolbar_ = nullptr;
  navigator_ = nullptr;
  media_ = nullptr;
  custom_elements_ = nullptr;
  application_cache_ = nullptr;
}

void LocalDOMWindow::SendOrientationChangeEvent() {
  DCHECK(RuntimeEnabledFeatures::orientationEventEnabled());
  DCHECK(GetFrame()->IsLocalRoot());

  // Before dispatching the event, build a list of all frames in the page
  // to send the event to, to mitigate side effects from event handlers
  // potentially interfering with others.
  HeapVector<Member<LocalFrame>> frames;
  frames.push_back(GetFrame());
  for (size_t i = 0; i < frames.size(); i++) {
    for (Frame* child = frames[i]->Tree().FirstChild(); child;
         child = child->Tree().NextSibling()) {
      if (child->IsLocalFrame())
        frames.push_back(ToLocalFrame(child));
    }
  }

  for (LocalFrame* frame : frames) {
    frame->DomWindow()->DispatchEvent(
        Event::Create(EventTypeNames::orientationchange));
  }
}

int LocalDOMWindow::orientation() const {
  DCHECK(RuntimeEnabledFeatures::orientationEventEnabled());

  if (!GetFrame() || !GetFrame()->GetPage())
    return 0;

  int orientation = GetFrame()
                        ->GetPage()
                        ->GetChromeClient()
                        .GetScreenInfo()
                        .orientation_angle;
  // For backward compatibility, we want to return a value in the range of
  // [-90; 180] instead of [0; 360[ because window.orientation used to behave
  // like that in WebKit (this is a WebKit proprietary API).
  if (orientation == 270)
    return -90;
  return orientation;
}

Screen* LocalDOMWindow::screen() const {
  if (!screen_)
    screen_ = Screen::Create(GetFrame());
  return screen_.Get();
}

History* LocalDOMWindow::history() const {
  if (!history_)
    history_ = History::Create(GetFrame());
  return history_.Get();
}

BarProp* LocalDOMWindow::locationbar() const {
  if (!locationbar_)
    locationbar_ = BarProp::Create(GetFrame(), BarProp::kLocationbar);
  return locationbar_.Get();
}

BarProp* LocalDOMWindow::menubar() const {
  if (!menubar_)
    menubar_ = BarProp::Create(GetFrame(), BarProp::kMenubar);
  return menubar_.Get();
}

BarProp* LocalDOMWindow::personalbar() const {
  if (!personalbar_)
    personalbar_ = BarProp::Create(GetFrame(), BarProp::kPersonalbar);
  return personalbar_.Get();
}

BarProp* LocalDOMWindow::scrollbars() const {
  if (!scrollbars_)
    scrollbars_ = BarProp::Create(GetFrame(), BarProp::kScrollbars);
  return scrollbars_.Get();
}

BarProp* LocalDOMWindow::statusbar() const {
  if (!statusbar_)
    statusbar_ = BarProp::Create(GetFrame(), BarProp::kStatusbar);
  return statusbar_.Get();
}

BarProp* LocalDOMWindow::toolbar() const {
  if (!toolbar_)
    toolbar_ = BarProp::Create(GetFrame(), BarProp::kToolbar);
  return toolbar_.Get();
}

FrameConsole* LocalDOMWindow::GetFrameConsole() const {
  if (!IsCurrentlyDisplayedInFrame())
    return nullptr;
  return &GetFrame()->Console();
}

ApplicationCache* LocalDOMWindow::applicationCache() const {
  if (!IsCurrentlyDisplayedInFrame())
    return nullptr;
  if (!application_cache_)
    application_cache_ = ApplicationCache::Create(GetFrame());
  return application_cache_.Get();
}

Navigator* LocalDOMWindow::navigator() const {
  if (!navigator_)
    navigator_ = Navigator::Create(GetFrame());
  return navigator_.Get();
}

void LocalDOMWindow::SchedulePostMessage(MessageEvent* event,
                                         PassRefPtr<SecurityOrigin> target,
                                         Document* source) {
  // Allowing unbounded amounts of messages to build up for a suspended context
  // is problematic; consider imposing a limit or other restriction if this
  // surfaces often as a problem (see crbug.com/587012).
  std::unique_ptr<SourceLocation> location = SourceLocation::Capture(source);
  PostMessageTimer* timer =
      new PostMessageTimer(*this, event, std::move(target), std::move(location),
                           UserGestureIndicator::CurrentToken());
  timer->StartOneShot(0, BLINK_FROM_HERE);
  timer->SuspendIfNeeded();
  post_message_timers_.insert(timer);
}

void LocalDOMWindow::PostMessageTimerFired(PostMessageTimer* timer) {
  if (!IsCurrentlyDisplayedInFrame())
    return;

  MessageEvent* event = timer->Event();

  UserGestureIndicator gesture_indicator(DocumentUserGestureToken::Adopt(
      document(), timer->GetUserGestureToken()));

  event->EntangleMessagePorts(document());

  DispatchMessageEventWithOriginCheck(timer->TargetOrigin(), event,
                                      timer->TakeLocation());
}

void LocalDOMWindow::RemovePostMessageTimer(PostMessageTimer* timer) {
  post_message_timers_.erase(timer);
}

void LocalDOMWindow::DispatchMessageEventWithOriginCheck(
    SecurityOrigin* intended_target_origin,
    Event* event,
    std::unique_ptr<SourceLocation> location) {
  if (intended_target_origin) {
    // Check target origin now since the target document may have changed since
    // the timer was scheduled.
    SecurityOrigin* security_origin = document()->GetSecurityOrigin();
    bool valid_target =
        intended_target_origin->IsSameSchemeHostPortAndSuborigin(
            security_origin);
    if (security_origin->HasSuborigin() &&
        security_origin->GetSuborigin()->PolicyContains(
            Suborigin::SuboriginPolicyOptions::kUnsafePostMessageReceive))
      valid_target =
          intended_target_origin->IsSameSchemeHostPort(security_origin);

    if (!valid_target) {
      String message = ExceptionMessages::FailedToExecute(
          "postMessage", "DOMWindow",
          "The target origin provided ('" + intended_target_origin->ToString() +
              "') does not match the recipient window's origin ('" +
              document()->GetSecurityOrigin()->ToString() + "').");
      ConsoleMessage* console_message =
          ConsoleMessage::Create(kSecurityMessageSource, kErrorMessageLevel,
                                 message, std::move(location));
      GetFrameConsole()->AddMessage(console_message);
      return;
    }
  }

  KURL sender(kParsedURLString, static_cast<MessageEvent*>(event)->origin());
  if (!document()->GetContentSecurityPolicy()->AllowConnectToSource(
          sender, RedirectStatus::kNoRedirect,
          SecurityViolationReportingPolicy::kSuppressReporting)) {
    UseCounter::Count(
        GetFrame(), UseCounter::kPostMessageIncomingWouldBeBlockedByConnectSrc);
  }

  DispatchEvent(event);
}

DOMSelection* LocalDOMWindow::getSelection() {
  if (!IsCurrentlyDisplayedInFrame())
    return nullptr;

  return document()->GetSelection();
}

Element* LocalDOMWindow::frameElement() const {
  if (!(GetFrame() && GetFrame()->Owner() && GetFrame()->Owner()->IsLocal()))
    return nullptr;

  return ToHTMLFrameOwnerElement(GetFrame()->Owner());
}

void LocalDOMWindow::blur() {}

void LocalDOMWindow::print(ScriptState* script_state) {
  if (!GetFrame())
    return;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return;

  if (script_state &&
      v8::MicrotasksScope::IsRunningMicrotasks(script_state->GetIsolate())) {
    UseCounter::Count(document(), UseCounter::kDuring_Microtask_Print);
  }

  if (GetFrame()->IsLoading()) {
    should_print_when_finished_loading_ = true;
    return;
  }

  UseCounter::CountCrossOriginIframe(*document(),
                                     UseCounter::kCrossOriginWindowPrint);

  should_print_when_finished_loading_ = false;
  page->GetChromeClient().Print(GetFrame());
}

void LocalDOMWindow::stop() {
  if (!GetFrame())
    return;
  GetFrame()->Loader().StopAllLoaders();
}

void LocalDOMWindow::alert(ScriptState* script_state, const String& message) {
  if (!GetFrame())
    return;

  if (document()->IsSandboxed(kSandboxModals)) {
    UseCounter::Count(document(), UseCounter::kDialogInSandboxedContext);
    GetFrameConsole()->AddMessage(ConsoleMessage::Create(
        kSecurityMessageSource, kErrorMessageLevel,
        "Ignored call to 'alert()'. The document is sandboxed, and the "
        "'allow-modals' keyword is not set."));
    return;
  }

  switch (document()->GetEngagementLevel()) {
    case mojom::blink::EngagementLevel::NONE:
      UseCounter::Count(document(), UseCounter::kAlertEngagementNone);
      break;
    case mojom::blink::EngagementLevel::MINIMAL:
      UseCounter::Count(document(), UseCounter::kAlertEngagementMinimal);
      break;
    case mojom::blink::EngagementLevel::LOW:
      UseCounter::Count(document(), UseCounter::kAlertEngagementLow);
      break;
    case mojom::blink::EngagementLevel::MEDIUM:
      UseCounter::Count(document(), UseCounter::kAlertEngagementMedium);
      break;
    case mojom::blink::EngagementLevel::HIGH:
      UseCounter::Count(document(), UseCounter::kAlertEngagementHigh);
      break;
    case mojom::blink::EngagementLevel::MAX:
      UseCounter::Count(document(), UseCounter::kAlertEngagementMax);
      break;
  }

  if (v8::MicrotasksScope::IsRunningMicrotasks(script_state->GetIsolate())) {
    UseCounter::Count(document(), UseCounter::kDuring_Microtask_Alert);
  }

  document()->UpdateStyleAndLayoutTree();

  Page* page = GetFrame()->GetPage();
  if (!page)
    return;

  UseCounter::CountCrossOriginIframe(*document(),
                                     UseCounter::kCrossOriginWindowAlert);

  page->GetChromeClient().OpenJavaScriptAlert(GetFrame(), message);
}

bool LocalDOMWindow::confirm(ScriptState* script_state, const String& message) {
  if (!GetFrame())
    return false;

  if (document()->IsSandboxed(kSandboxModals)) {
    UseCounter::Count(document(), UseCounter::kDialogInSandboxedContext);
    GetFrameConsole()->AddMessage(ConsoleMessage::Create(
        kSecurityMessageSource, kErrorMessageLevel,
        "Ignored call to 'confirm()'. The document is sandboxed, and the "
        "'allow-modals' keyword is not set."));
    return false;
  }

  switch (document()->GetEngagementLevel()) {
    case mojom::blink::EngagementLevel::NONE:
      UseCounter::Count(document(), UseCounter::kConfirmEngagementNone);
      break;
    case mojom::blink::EngagementLevel::MINIMAL:
      UseCounter::Count(document(), UseCounter::kConfirmEngagementMinimal);
      break;
    case mojom::blink::EngagementLevel::LOW:
      UseCounter::Count(document(), UseCounter::kConfirmEngagementLow);
      break;
    case mojom::blink::EngagementLevel::MEDIUM:
      UseCounter::Count(document(), UseCounter::kConfirmEngagementMedium);
      break;
    case mojom::blink::EngagementLevel::HIGH:
      UseCounter::Count(document(), UseCounter::kConfirmEngagementHigh);
      break;
    case mojom::blink::EngagementLevel::MAX:
      UseCounter::Count(document(), UseCounter::kConfirmEngagementMax);
      break;
  }

  if (v8::MicrotasksScope::IsRunningMicrotasks(script_state->GetIsolate())) {
    UseCounter::Count(document(), UseCounter::kDuring_Microtask_Confirm);
  }

  document()->UpdateStyleAndLayoutTree();

  Page* page = GetFrame()->GetPage();
  if (!page)
    return false;

  UseCounter::CountCrossOriginIframe(*document(),
                                     UseCounter::kCrossOriginWindowConfirm);

  return page->GetChromeClient().OpenJavaScriptConfirm(GetFrame(), message);
}

String LocalDOMWindow::prompt(ScriptState* script_state,
                              const String& message,
                              const String& default_value) {
  if (!GetFrame())
    return String();

  if (document()->IsSandboxed(kSandboxModals)) {
    UseCounter::Count(document(), UseCounter::kDialogInSandboxedContext);
    GetFrameConsole()->AddMessage(ConsoleMessage::Create(
        kSecurityMessageSource, kErrorMessageLevel,
        "Ignored call to 'prompt()'. The document is sandboxed, and the "
        "'allow-modals' keyword is not set."));
    return String();
  }

  switch (document()->GetEngagementLevel()) {
    case mojom::blink::EngagementLevel::NONE:
      UseCounter::Count(document(), UseCounter::kPromptEngagementNone);
      break;
    case mojom::blink::EngagementLevel::MINIMAL:
      UseCounter::Count(document(), UseCounter::kPromptEngagementMinimal);
      break;
    case mojom::blink::EngagementLevel::LOW:
      UseCounter::Count(document(), UseCounter::kPromptEngagementLow);
      break;
    case mojom::blink::EngagementLevel::MEDIUM:
      UseCounter::Count(document(), UseCounter::kPromptEngagementMedium);
      break;
    case mojom::blink::EngagementLevel::HIGH:
      UseCounter::Count(document(), UseCounter::kPromptEngagementHigh);
      break;
    case mojom::blink::EngagementLevel::MAX:
      UseCounter::Count(document(), UseCounter::kPromptEngagementMax);
      break;
  }

  if (v8::MicrotasksScope::IsRunningMicrotasks(script_state->GetIsolate())) {
    UseCounter::Count(document(), UseCounter::kDuring_Microtask_Prompt);
  }

  document()->UpdateStyleAndLayoutTree();

  Page* page = GetFrame()->GetPage();
  if (!page)
    return String();

  String return_value;
  if (page->GetChromeClient().OpenJavaScriptPrompt(GetFrame(), message,
                                                   default_value, return_value))
    return return_value;

  UseCounter::CountCrossOriginIframe(*document(),
                                     UseCounter::kCrossOriginWindowPrompt);

  return String();
}

bool LocalDOMWindow::find(const String& string,
                          bool case_sensitive,
                          bool backwards,
                          bool wrap,
                          bool whole_word,
                          bool /*searchInFrames*/,
                          bool /*showDialog*/) const {
  if (!IsCurrentlyDisplayedInFrame())
    return false;

  // Up-to-date, clean tree is required for finding text in page, since it
  // relies on TextIterator to look over the text.
  document()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  // FIXME (13016): Support searchInFrames and showDialog
  FindOptions options =
      (backwards ? kBackwards : 0) | (case_sensitive ? 0 : kCaseInsensitive) |
      (wrap ? kWrapAround : 0) | (whole_word ? kWholeWord | kAtWordStarts : 0);
  return GetFrame()->GetEditor().FindString(string, options);
}

bool LocalDOMWindow::offscreenBuffering() const {
  return true;
}

int LocalDOMWindow::outerHeight() const {
  if (!GetFrame())
    return 0;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return 0;

  ChromeClient& chrome_client = page->GetChromeClient();
  if (page->GetSettings().GetReportScreenSizeInPhysicalPixelsQuirk())
    return lroundf(chrome_client.RootWindowRect().Height() *
                   chrome_client.GetScreenInfo().device_scale_factor);
  return chrome_client.RootWindowRect().Height();
}

int LocalDOMWindow::outerWidth() const {
  if (!GetFrame())
    return 0;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return 0;

  ChromeClient& chrome_client = page->GetChromeClient();
  if (page->GetSettings().GetReportScreenSizeInPhysicalPixelsQuirk())
    return lroundf(chrome_client.RootWindowRect().Width() *
                   chrome_client.GetScreenInfo().device_scale_factor);

  return chrome_client.RootWindowRect().Width();
}

FloatSize LocalDOMWindow::GetViewportSize(
    IncludeScrollbarsInRect scrollbar_inclusion) const {
  if (!GetFrame())
    return FloatSize();

  FrameView* view = GetFrame()->View();
  if (!view)
    return FloatSize();

  Page* page = GetFrame()->GetPage();
  if (!page)
    return FloatSize();

  // The main frame's viewport size depends on the page scale. If viewport is
  // enabled, the initial page scale depends on the content width and is set
  // after a layout, perform one now so queries during page load will use the
  // up to date viewport.
  bool affectedByScale =
      page->GetSettings().GetViewportEnabled() && GetFrame()->IsMainFrame();
  bool affectedByScrollbars =
      scrollbar_inclusion == kExcludeScrollbars &&
      !ScrollbarTheme::GetTheme().UsesOverlayScrollbars();

  if (affectedByScale || affectedByScrollbars)
    document()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  // FIXME: This is potentially too much work. We really only need to know the
  // dimensions of the parent frame's layoutObject.
  if (Frame* parent = GetFrame()->Tree().Parent()) {
    if (parent && parent->IsLocalFrame())
      ToLocalFrame(parent)
          ->GetDocument()
          ->UpdateStyleAndLayoutIgnorePendingStylesheets();
  }

  return GetFrame()->IsMainFrame() &&
                 !page->GetSettings().GetInertVisualViewport()
             ? FloatSize(page->GetVisualViewport().VisibleRect().Size())
             : FloatSize(view->VisibleContentRect(scrollbar_inclusion).Size());
}

int LocalDOMWindow::innerHeight() const {
  if (!GetFrame())
    return 0;

  FloatSize viewport_size = GetViewportSize(kIncludeScrollbars);
  return AdjustForAbsoluteZoom(ExpandedIntSize(viewport_size).Height(),
                               GetFrame()->PageZoomFactor());
}

int LocalDOMWindow::innerWidth() const {
  if (!GetFrame())
    return 0;

  FloatSize viewport_size = GetViewportSize(kIncludeScrollbars);
  return AdjustForAbsoluteZoom(ExpandedIntSize(viewport_size).Width(),
                               GetFrame()->PageZoomFactor());
}

int LocalDOMWindow::screenX() const {
  if (!GetFrame())
    return 0;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return 0;

  ChromeClient& chrome_client = page->GetChromeClient();
  if (page->GetSettings().GetReportScreenSizeInPhysicalPixelsQuirk())
    return lroundf(chrome_client.RootWindowRect().X() *
                   chrome_client.GetScreenInfo().device_scale_factor);
  return chrome_client.RootWindowRect().X();
}

int LocalDOMWindow::screenY() const {
  if (!GetFrame())
    return 0;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return 0;

  ChromeClient& chrome_client = page->GetChromeClient();
  if (page->GetSettings().GetReportScreenSizeInPhysicalPixelsQuirk())
    return lroundf(chrome_client.RootWindowRect().Y() *
                   chrome_client.GetScreenInfo().device_scale_factor);
  return chrome_client.RootWindowRect().Y();
}

double LocalDOMWindow::scrollX() const {
  if (!GetFrame() || !GetFrame()->GetPage())
    return 0;

  if (!GetFrame()->GetPage()->GetSettings().GetInertVisualViewport())
    return visual_viewport_->pageX();

  FrameView* view = GetFrame()->View();
  if (!view)
    return 0;

  document()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  double viewport_x =
      view->LayoutViewportScrollableArea()->GetScrollOffset().Width();
  return AdjustScrollForAbsoluteZoom(viewport_x, GetFrame()->PageZoomFactor());
}

double LocalDOMWindow::scrollY() const {
  if (!GetFrame() || !GetFrame()->GetPage())
    return 0;

  if (!GetFrame()->GetPage()->GetSettings().GetInertVisualViewport())
    return visual_viewport_->pageY();

  FrameView* view = GetFrame()->View();
  if (!view)
    return 0;

  document()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  double viewport_y =
      view->LayoutViewportScrollableArea()->GetScrollOffset().Height();
  return AdjustScrollForAbsoluteZoom(viewport_y, GetFrame()->PageZoomFactor());
}

DOMVisualViewport* LocalDOMWindow::visualViewport() {
  if (!GetFrame())
    return nullptr;

  return visual_viewport_;
}

const AtomicString& LocalDOMWindow::name() const {
  if (!IsCurrentlyDisplayedInFrame())
    return g_null_atom;

  return GetFrame()->Tree().GetName();
}

void LocalDOMWindow::setName(const AtomicString& name) {
  if (!IsCurrentlyDisplayedInFrame())
    return;

  // Avoid calling out to notify the embedder if the browsing context name
  // didn't change. This is important to avoid violating the browser assumption
  // that the unique name doesn't change if the browsing context name doesn't
  // change.
  // TODO(dcheng): This comment is indicative of a problematic layering
  // violation. The browser should not be relying on the renderer to get this
  // correct; unique name calculation should be moved up into the browser.
  if (name == GetFrame()->Tree().GetName())
    return;

  GetFrame()->Tree().SetName(name);
  DCHECK(GetFrame()->Loader().Client());
  GetFrame()->Loader().Client()->DidChangeName(name);
}

void LocalDOMWindow::setStatus(const String& string) {
  status_ = string;

  if (!GetFrame())
    return;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return;

  page->GetChromeClient().SetStatusbarText(status_);
}

void LocalDOMWindow::setDefaultStatus(const String& string) {
  default_status_ = string;

  if (!GetFrame())
    return;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return;

  page->GetChromeClient().SetStatusbarText(default_status_);
}

String LocalDOMWindow::origin() const {
  return GetExecutionContext()->GetSecurityOrigin()->ToString();
}

Document* LocalDOMWindow::document() const {
  return document_.Get();
}

StyleMedia* LocalDOMWindow::styleMedia() const {
  if (!media_)
    media_ = StyleMedia::Create(GetFrame());
  return media_.Get();
}

CSSStyleDeclaration* LocalDOMWindow::getComputedStyle(
    Element* elt,
    const String& pseudo_elt) const {
  DCHECK(elt);
  return CSSComputedStyleDeclaration::Create(elt, false, pseudo_elt);
}

CSSRuleList* LocalDOMWindow::getMatchedCSSRules(
    Element* element,
    const String& pseudo_element) const {
  if (!element)
    return nullptr;

  if (!IsCurrentlyDisplayedInFrame())
    return nullptr;

  unsigned colon_start =
      pseudo_element[0] == ':' ? (pseudo_element[1] == ':' ? 2 : 1) : 0;
  CSSSelector::PseudoType pseudo_type = CSSSelector::ParsePseudoType(
      AtomicString(pseudo_element.Substring(colon_start)), false);
  if (pseudo_type == CSSSelector::kPseudoUnknown && !pseudo_element.IsEmpty())
    return nullptr;

  unsigned rules_to_include = StyleResolver::kAuthorCSSRules;
  PseudoId pseudo_id = CSSSelector::GetPseudoId(pseudo_type);
  element->GetDocument().UpdateStyleAndLayoutTree();
  return document()->EnsureStyleResolver().PseudoCSSRulesForElement(
      element, pseudo_id, rules_to_include);
}

double LocalDOMWindow::devicePixelRatio() const {
  if (!GetFrame())
    return 0.0;

  return GetFrame()->DevicePixelRatio();
}

void LocalDOMWindow::scrollBy(double x,
                              double y,
                              ScrollBehavior scroll_behavior) const {
  if (!IsCurrentlyDisplayedInFrame())
    return;

  document()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  FrameView* view = GetFrame()->View();
  if (!view)
    return;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return;

  x = ScrollableArea::NormalizeNonFiniteScroll(x);
  y = ScrollableArea::NormalizeNonFiniteScroll(y);

  ScrollableArea* viewport = page->GetSettings().GetInertVisualViewport()
                                 ? view->LayoutViewportScrollableArea()
                                 : view->GetScrollableArea();

  ScrollOffset current_offset = viewport->GetScrollOffset();
  ScrollOffset scaled_delta(x * GetFrame()->PageZoomFactor(),
                            y * GetFrame()->PageZoomFactor());

  viewport->SetScrollOffset(current_offset + scaled_delta, kProgrammaticScroll,
                            scroll_behavior);
}

void LocalDOMWindow::scrollBy(const ScrollToOptions& scroll_to_options) const {
  double x = 0.0;
  double y = 0.0;
  if (scroll_to_options.hasLeft())
    x = scroll_to_options.left();
  if (scroll_to_options.hasTop())
    y = scroll_to_options.top();
  ScrollBehavior scroll_behavior = kScrollBehaviorAuto;
  ScrollableArea::ScrollBehaviorFromString(scroll_to_options.behavior(),
                                           scroll_behavior);
  scrollBy(x, y, scroll_behavior);
}

void LocalDOMWindow::scrollTo(double x, double y) const {
  if (!IsCurrentlyDisplayedInFrame())
    return;

  FrameView* view = GetFrame()->View();
  if (!view)
    return;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return;

  x = ScrollableArea::NormalizeNonFiniteScroll(x);
  y = ScrollableArea::NormalizeNonFiniteScroll(y);

  // It is only necessary to have an up-to-date layout if the position may be
  // clamped, which is never the case for (0, 0).
  if (x || y)
    document()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  ScrollOffset layout_offset(x * GetFrame()->PageZoomFactor(),
                             y * GetFrame()->PageZoomFactor());
  ScrollableArea* viewport = page->GetSettings().GetInertVisualViewport()
                                 ? view->LayoutViewportScrollableArea()
                                 : view->GetScrollableArea();
  viewport->SetScrollOffset(layout_offset, kProgrammaticScroll,
                            kScrollBehaviorAuto);
}

void LocalDOMWindow::scrollTo(const ScrollToOptions& scroll_to_options) const {
  if (!IsCurrentlyDisplayedInFrame())
    return;

  FrameView* view = GetFrame()->View();
  if (!view)
    return;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return;

  // It is only necessary to have an up-to-date layout if the position may be
  // clamped, which is never the case for (0, 0).
  if (!scroll_to_options.hasLeft() || !scroll_to_options.hasTop() ||
      scroll_to_options.left() || scroll_to_options.top()) {
    document()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  }

  double scaled_x = 0.0;
  double scaled_y = 0.0;

  ScrollableArea* viewport = page->GetSettings().GetInertVisualViewport()
                                 ? view->LayoutViewportScrollableArea()
                                 : view->GetScrollableArea();

  ScrollOffset current_offset = viewport->GetScrollOffset();
  scaled_x = current_offset.Width();
  scaled_y = current_offset.Height();

  if (scroll_to_options.hasLeft())
    scaled_x =
        ScrollableArea::NormalizeNonFiniteScroll(scroll_to_options.left()) *
        GetFrame()->PageZoomFactor();

  if (scroll_to_options.hasTop())
    scaled_y =
        ScrollableArea::NormalizeNonFiniteScroll(scroll_to_options.top()) *
        GetFrame()->PageZoomFactor();

  ScrollBehavior scroll_behavior = kScrollBehaviorAuto;
  ScrollableArea::ScrollBehaviorFromString(scroll_to_options.behavior(),
                                           scroll_behavior);

  viewport->SetScrollOffset(ScrollOffset(scaled_x, scaled_y),
                            kProgrammaticScroll, scroll_behavior);
}

void LocalDOMWindow::moveBy(int x, int y) const {
  if (!GetFrame() || !GetFrame()->IsMainFrame())
    return;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return;

  IntRect window_rect = page->GetChromeClient().RootWindowRect();
  window_rect.SaturatedMove(x, y);
  // Security check (the spec talks about UniversalBrowserWrite to disable this
  // check...)
  page->GetChromeClient().SetWindowRectWithAdjustment(window_rect, *GetFrame());
}

void LocalDOMWindow::moveTo(int x, int y) const {
  if (!GetFrame() || !GetFrame()->IsMainFrame())
    return;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return;

  IntRect window_rect = page->GetChromeClient().RootWindowRect();
  window_rect.SetLocation(IntPoint(x, y));
  // Security check (the spec talks about UniversalBrowserWrite to disable this
  // check...)
  page->GetChromeClient().SetWindowRectWithAdjustment(window_rect, *GetFrame());
}

void LocalDOMWindow::resizeBy(int x, int y) const {
  if (!GetFrame() || !GetFrame()->IsMainFrame())
    return;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return;

  IntRect fr = page->GetChromeClient().RootWindowRect();
  IntSize dest = fr.Size() + IntSize(x, y);
  IntRect update(fr.Location(), dest);
  page->GetChromeClient().SetWindowRectWithAdjustment(update, *GetFrame());
}

void LocalDOMWindow::resizeTo(int width, int height) const {
  if (!GetFrame() || !GetFrame()->IsMainFrame())
    return;

  Page* page = GetFrame()->GetPage();
  if (!page)
    return;

  IntRect fr = page->GetChromeClient().RootWindowRect();
  IntSize dest = IntSize(width, height);
  IntRect update(fr.Location(), dest);
  page->GetChromeClient().SetWindowRectWithAdjustment(update, *GetFrame());
}

int LocalDOMWindow::requestAnimationFrame(FrameRequestCallback* callback) {
  callback->use_legacy_time_base_ = false;
  if (Document* doc = document())
    return doc->RequestAnimationFrame(callback);
  return 0;
}

int LocalDOMWindow::webkitRequestAnimationFrame(
    FrameRequestCallback* callback) {
  callback->use_legacy_time_base_ = true;
  if (Document* document = this->document())
    return document->RequestAnimationFrame(callback);
  return 0;
}

void LocalDOMWindow::cancelAnimationFrame(int id) {
  if (Document* document = this->document())
    document->CancelAnimationFrame(id);
}

int LocalDOMWindow::requestIdleCallback(IdleRequestCallback* callback,
                                        const IdleRequestOptions& options) {
  if (Document* document = this->document())
    return document->RequestIdleCallback(callback, options);
  return 0;
}

void LocalDOMWindow::cancelIdleCallback(int id) {
  if (Document* document = this->document())
    document->CancelIdleCallback(id);
}

CustomElementRegistry* LocalDOMWindow::customElements(
    ScriptState* script_state) const {
  if (!script_state->World().IsMainWorld())
    return nullptr;
  return customElements();
}

CustomElementRegistry* LocalDOMWindow::customElements() const {
  if (!custom_elements_ && document_)
    custom_elements_ = CustomElementRegistry::Create(this);
  return custom_elements_;
}

CustomElementRegistry* LocalDOMWindow::MaybeCustomElements() const {
  return custom_elements_;
}

External* LocalDOMWindow::external() {
  if (!external_)
    external_ = new External;
  return external_;
}

bool LocalDOMWindow::isSecureContext() const {
  if (!GetFrame())
    return false;

  return document()->IsSecureContext();
}

void LocalDOMWindow::AddedEventListener(
    const AtomicString& event_type,
    RegisteredEventListener& registered_listener) {
  DOMWindow::AddedEventListener(event_type, registered_listener);
  if (GetFrame() && GetFrame()->GetPage())
    GetFrame()->GetPage()->GetEventHandlerRegistry().DidAddEventHandler(
        *this, event_type, registered_listener.Options());

  if (Document* document = this->document())
    document->AddListenerTypeIfNeeded(event_type, *this);

  for (auto& it : event_listener_observers_) {
    it->DidAddEventListener(this, event_type);
  }

  if (event_type == EventTypeNames::unload) {
    UseCounter::Count(document(), UseCounter::kDocumentUnloadRegistered);
    AddUnloadEventListener(this);
  } else if (event_type == EventTypeNames::beforeunload) {
    UseCounter::Count(document(), UseCounter::kDocumentBeforeUnloadRegistered);
    if (AllowsBeforeUnloadListeners(this)) {
      // This is confusingly named. It doesn't actually add the listener. It
      // just increments a count so that we know we have listeners registered
      // for the purposes of determining if we can fast terminate the renderer
      // process.
      AddBeforeUnloadEventListener(this);
    } else {
      // Subframes return false from allowsBeforeUnloadListeners.
      UseCounter::Count(document(),
                        UseCounter::kSubFrameBeforeUnloadRegistered);
    }
  }
}

void LocalDOMWindow::RemovedEventListener(
    const AtomicString& event_type,
    const RegisteredEventListener& registered_listener) {
  DOMWindow::RemovedEventListener(event_type, registered_listener);
  if (GetFrame() && GetFrame()->GetPage())
    GetFrame()->GetPage()->GetEventHandlerRegistry().DidRemoveEventHandler(
        *this, event_type, registered_listener.Options());

  for (auto& it : event_listener_observers_) {
    it->DidRemoveEventListener(this, event_type);
  }

  if (event_type == EventTypeNames::unload) {
    RemoveUnloadEventListener(this);
  } else if (event_type == EventTypeNames::beforeunload &&
             AllowsBeforeUnloadListeners(this)) {
    RemoveBeforeUnloadEventListener(this);
  }
}

void LocalDOMWindow::WarnUnusedPreloads(TimerBase* base) {
  if (GetFrame() && GetFrame()->Loader().GetDocumentLoader()) {
    ResourceFetcher* fetcher =
        GetFrame()->Loader().GetDocumentLoader()->Fetcher();
    DCHECK(fetcher);
    if (fetcher->CountPreloads())
      fetcher->WarnUnusedPreloads();
  }
}

void LocalDOMWindow::DispatchLoadEvent() {
  Event* load_event(Event::Create(EventTypeNames::load));
  if (GetFrame() && GetFrame()->Loader().GetDocumentLoader() &&
      !GetFrame()->Loader().GetDocumentLoader()->GetTiming().LoadEventStart()) {
    DocumentLoader* document_loader = GetFrame()->Loader().GetDocumentLoader();
    DocumentLoadTiming& timing = document_loader->GetTiming();
    timing.MarkLoadEventStart();
    DispatchEvent(load_event, document());
    SetHasLoadEventFired();
    timing.MarkLoadEventEnd();
    DCHECK(document_loader->Fetcher());
    // If fetcher->countPreloads() is not empty here, it's full of link
    // preloads, as speculatove preloads were cleared at DCL.
    if (GetFrame() &&
        document_loader == GetFrame()->Loader().GetDocumentLoader() &&
        document_loader->Fetcher()->CountPreloads())
      unused_preloads_timer_.StartOneShot(kUnusedPreloadTimeoutInSeconds,
                                          BLINK_FROM_HERE);
  } else {
    DispatchEvent(load_event, document());
  }

  if (GetFrame()) {
    Performance* performance = DOMWindowPerformance::performance(*this);
    DCHECK(performance);
    performance->NotifyNavigationTimingToObservers();
  }

  // For load events, send a separate load event to the enclosing frame only.
  // This is a DOM extension and is independent of bubbling/capturing rules of
  // the DOM.
  FrameOwner* owner = GetFrame() ? GetFrame()->Owner() : nullptr;
  if (owner)
    owner->DispatchLoad();

  TRACE_EVENT_INSTANT1("devtools.timeline", "MarkLoad",
                       TRACE_EVENT_SCOPE_THREAD, "data",
                       InspectorMarkLoadEvent::Data(GetFrame()));
  probe::loadEventFired(GetFrame());
}

DispatchEventResult LocalDOMWindow::DispatchEvent(Event* event,
                                                  EventTarget* target) {
#if DCHECK_IS_ON()
  DCHECK(!EventDispatchForbiddenScope::IsEventDispatchForbidden());
#endif

  event->SetTrusted(true);
  event->SetTarget(target ? target : this);
  event->SetCurrentTarget(this);
  event->SetEventPhase(Event::kAtTarget);

  TRACE_EVENT1("devtools.timeline", "EventDispatch", "data",
               InspectorEventDispatchEvent::Data(*event));
  DispatchEventResult result;

  if (GetFrame() && GetFrame()->IsMainFrame() &&
      event->type() == EventTypeNames::resize) {
    SCOPED_BLINK_UMA_HISTOGRAM_TIMER("Blink.EventListenerDuration.Resize");
    result = FireEventListeners(event);
  } else {
    result = FireEventListeners(event);
  }

  return result;
}

void LocalDOMWindow::RemoveAllEventListeners() {
  EventTarget::RemoveAllEventListeners();

  for (auto& it : event_listener_observers_) {
    it->DidRemoveAllEventListeners(this);
  }

  if (GetFrame() && GetFrame()->GetPage())
    GetFrame()->GetPage()->GetEventHandlerRegistry().DidRemoveAllEventHandlers(
        *this);

  RemoveAllUnloadEventListeners(this);
  RemoveAllBeforeUnloadEventListeners(this);
}

void LocalDOMWindow::FinishedLoading() {
  if (should_print_when_finished_loading_) {
    should_print_when_finished_loading_ = false;
    print(nullptr);
  }
}

void LocalDOMWindow::PrintErrorMessage(const String& message) const {
  if (!IsCurrentlyDisplayedInFrame())
    return;

  if (message.IsEmpty())
    return;

  GetFrameConsole()->AddMessage(
      ConsoleMessage::Create(kJSMessageSource, kErrorMessageLevel, message));
}

DOMWindow* LocalDOMWindow::open(const String& url_string,
                                const AtomicString& frame_name,
                                const String& window_features_string,
                                LocalDOMWindow* calling_window,
                                LocalDOMWindow* entered_window,
                                ExceptionState& exception_state) {
  if (!IsCurrentlyDisplayedInFrame())
    return nullptr;
  if (!calling_window->GetFrame())
    return nullptr;
  Document* active_document = calling_window->document();
  if (!active_document)
    return nullptr;
  LocalFrame* first_frame = entered_window->GetFrame();
  if (!first_frame)
    return nullptr;

  UseCounter::Count(*active_document, UseCounter::kDOMWindowOpen);
  if (!window_features_string.IsEmpty())
    UseCounter::Count(*active_document, UseCounter::kDOMWindowOpenFeatures);

  if (!entered_window->AllowPopUp()) {
    // Because FrameTree::find() returns true for empty strings, we must check
    // for empty frame names.  Otherwise, illegitimate window.open() calls with
    // no name will pass right through the popup blocker.
    if (frame_name.IsEmpty() || !GetFrame()->Tree().Find(frame_name))
      return nullptr;
  }

  // Get the target frame for the special cases of _top and _parent.
  // In those cases, we schedule a location change right now and return early.
  Frame* target_frame = nullptr;
  if (EqualIgnoringASCIICase(frame_name, "_top")) {
    target_frame = &GetFrame()->Tree().Top();
  } else if (EqualIgnoringASCIICase(frame_name, "_parent")) {
    if (Frame* parent = GetFrame()->Tree().Parent())
      target_frame = parent;
    else
      target_frame = GetFrame();
  }

  if (target_frame) {
    if (!active_document->GetFrame() ||
        !active_document->GetFrame()->CanNavigate(*target_frame)) {
      return nullptr;
    }

    KURL completed_url = first_frame->GetDocument()->CompleteURL(url_string);

    if (target_frame->DomWindow()->IsInsecureScriptAccess(*calling_window,
                                                          completed_url))
      return target_frame->DomWindow();

    if (url_string.IsEmpty())
      return target_frame->DomWindow();

    target_frame->Navigate(*active_document, completed_url, false,
                           UserGestureStatus::kNone);
    return target_frame->DomWindow();
  }

  WindowFeatures features(window_features_string);
  DOMWindow* new_window =
      CreateWindow(url_string, frame_name, features, *calling_window,
                   *first_frame, *GetFrame(), exception_state);
  return features.noopener ? nullptr : new_window;
}

DEFINE_TRACE(LocalDOMWindow) {
  visitor->Trace(document_);
  visitor->Trace(screen_);
  visitor->Trace(history_);
  visitor->Trace(locationbar_);
  visitor->Trace(menubar_);
  visitor->Trace(personalbar_);
  visitor->Trace(scrollbars_);
  visitor->Trace(statusbar_);
  visitor->Trace(toolbar_);
  visitor->Trace(navigator_);
  visitor->Trace(media_);
  visitor->Trace(custom_elements_);
  visitor->Trace(external_);
  visitor->Trace(application_cache_);
  visitor->Trace(event_queue_);
  visitor->Trace(post_message_timers_);
  visitor->Trace(visual_viewport_);
  visitor->Trace(event_listener_observers_);
  DOMWindow::Trace(visitor);
  Supplementable<LocalDOMWindow>::Trace(visitor);
}

DEFINE_TRACE_WRAPPERS(LocalDOMWindow) {
  visitor->TraceWrappers(custom_elements_);
  DOMWindow::TraceWrappers(visitor);
}

}  // namespace blink
