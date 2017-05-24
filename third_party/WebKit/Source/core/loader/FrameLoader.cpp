/*
 * Copyright (C) 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) 2008 Alp Toker <alp@atoker.com>
 * Copyright (C) Research In Motion Limited 2009. All rights reserved.
 * Copyright (C) 2011 Kris Jordan <krisjordan@gmail.com>
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/loader/FrameLoader.h"

#include <memory>
#include "bindings/core/v8/ScriptController.h"
#include "bindings/core/v8/serialization/SerializedScriptValue.h"
#include "core/HTMLNames.h"
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/dom/ViewportDescription.h"
#include "core/editing/Editor.h"
#include "core/events/GestureEvent.h"
#include "core/events/KeyboardEvent.h"
#include "core/events/MouseEvent.h"
#include "core/events/PageTransitionEvent.h"
#include "core/frame/ContentSettingsClient.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/Settings.h"
#include "core/frame/VisualViewport.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/html/HTMLFormElement.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/input/EventHandler.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/loader/DocumentLoadTiming.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/FormSubmission.h"
#include "core/loader/FrameLoadRequest.h"
#include "core/loader/LinkLoader.h"
#include "core/loader/MixedContentChecker.h"
#include "core/loader/NavigationScheduler.h"
#include "core/loader/NetworkHintsInterface.h"
#include "core/loader/ProgressTracker.h"
#include "core/loader/appcache/ApplicationCacheHost.h"
#include "core/page/ChromeClient.h"
#include "core/page/CreateWindow.h"
#include "core/page/FrameTree.h"
#include "core/page/Page.h"
#include "core/page/WindowFeatures.h"
#include "core/page/scrolling/ScrollingCoordinator.h"
#include "core/probe/CoreProbes.h"
#include "core/svg/graphics/SVGImage.h"
#include "core/xml/parser/XMLDocumentParser.h"
#include "platform/InstanceCounters.h"
#include "platform/PluginScriptForbiddenScope.h"
#include "platform/ScriptForbiddenScope.h"
#include "platform/UserGestureIndicator.h"
#include "platform/bindings/DOMWrapperWorld.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/loader/fetch/ResourceRequest.h"
#include "platform/network/HTTPParsers.h"
#include "platform/network/NetworkUtils.h"
#include "platform/scroll/ScrollAnimatorBase.h"
#include "platform/weborigin/SchemeRegistry.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "platform/weborigin/SecurityPolicy.h"
#include "platform/weborigin/Suborigin.h"
#include "platform/wtf/AutoReset.h"
#include "platform/wtf/text/CString.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/WebCachePolicy.h"
#include "public/platform/WebURLRequest.h"
#include "public/platform/modules/serviceworker/WebServiceWorkerNetworkProvider.h"

using blink::WebURLRequest;

namespace blink {

using namespace HTMLNames;

bool IsBackForwardLoadType(FrameLoadType type) {
  return type == kFrameLoadTypeBackForward ||
         type == kFrameLoadTypeInitialHistoryLoad;
}

bool IsReloadLoadType(FrameLoadType type) {
  return type == kFrameLoadTypeReload ||
         type == kFrameLoadTypeReloadBypassingCache;
}

static bool NeedsHistoryItemRestore(FrameLoadType type) {
  // FrameLoadtypeInitialHistoryLoad is intentionally excluded.
  return type == kFrameLoadTypeBackForward || IsReloadLoadType(type);
}

static void CheckForLegacyProtocolInSubresource(
    const ResourceRequest& resource_request,
    Document* document) {
  if (resource_request.GetFrameType() == WebURLRequest::kFrameTypeTopLevel)
    return;
  if (!SchemeRegistry::ShouldTreatURLSchemeAsLegacy(
          resource_request.Url().Protocol())) {
    return;
  }
  if (SchemeRegistry::ShouldTreatURLSchemeAsLegacy(
          document->GetSecurityOrigin()->Protocol())) {
    return;
  }
  Deprecation::CountDeprecation(
      document, UseCounter::kLegacyProtocolEmbeddedAsSubresource);
}

static NavigationPolicy MaybeCheckCSP(
    const ResourceRequest& request,
    NavigationType type,
    LocalFrame* frame,
    NavigationPolicy policy,
    bool should_check_main_world_content_security_policy,
    bool browser_side_navigation_enabled,
    ContentSecurityPolicy::CheckHeaderType check_header_type) {
  // If we're loading content into |frame| (NavigationPolicyCurrentTab), check
  // against the parent's Content Security Policy and kill the load if that
  // check fails, unless we should bypass the main world's CSP.
  if (policy == kNavigationPolicyCurrentTab &&
      should_check_main_world_content_security_policy &&
      // TODO(arthursonzogni): 'frame-src' check is disabled on the
      // renderer side with browser-side-navigation, but is enforced on the
      // browser side. See http://crbug.com/692595 for understanding why it
      // can't be enforced on both sides instead.
      !browser_side_navigation_enabled) {
    Frame* parent_frame = frame->Tree().Parent();
    if (parent_frame) {
      ContentSecurityPolicy* parent_policy =
          parent_frame->GetSecurityContext()->GetContentSecurityPolicy();
      if (!parent_policy->AllowFrameFromSource(
              request.Url(), request.GetRedirectStatus(),
              SecurityViolationReportingPolicy::kReport, check_header_type)) {
        // Fire a load event, as timing attacks would otherwise reveal that the
        // frame was blocked. This way, it looks like every other cross-origin
        // page load.
        frame->GetDocument()->EnforceSandboxFlags(kSandboxOrigin);
        frame->Owner()->DispatchLoad();
        return kNavigationPolicyIgnore;
      }
    }
  }

  bool is_form_submission = type == kNavigationTypeFormSubmitted ||
                            type == kNavigationTypeFormResubmitted;
  if (is_form_submission &&
      // 'form-action' check in the frame that is navigating is disabled on the
      // renderer side when PlzNavigate is enabled, but is enforced on the
      // browser side instead.
      // N.B. check in the frame that initiates the navigation stills occurs in
      // blink and is not enforced on the browser-side.
      // TODO(arthursonzogni) The 'form-action' check should be fully disabled
      // in blink when browser side navigation is enabled, except when the form
      // submission doesn't trigger a navigation(i.e. javascript urls). Please
      // see https://crbug.com/701749
      !browser_side_navigation_enabled &&
      !frame->GetDocument()->GetContentSecurityPolicy()->AllowFormAction(
          request.Url(), request.GetRedirectStatus(),
          SecurityViolationReportingPolicy::kReport, check_header_type)) {
    return kNavigationPolicyIgnore;
  }

  return policy;
}

ResourceRequest FrameLoader::ResourceRequestForReload(
    FrameLoadType frame_load_type,
    const KURL& override_url,
    ClientRedirectPolicy client_redirect_policy) {
  DCHECK(IsReloadLoadType(frame_load_type));
  WebCachePolicy cache_policy =
      frame_load_type == kFrameLoadTypeReloadBypassingCache
          ? WebCachePolicy::kBypassingCache
          : WebCachePolicy::kValidatingCacheData;
  if (!document_loader_ || !document_loader_->GetHistoryItem())
    return ResourceRequest();
  ResourceRequest request =
      document_loader_->GetHistoryItem()->GenerateResourceRequest(cache_policy);

  // ClientRedirectPolicy is an indication that this load was triggered by some
  // direct interaction with the page. If this reload is not a client redirect,
  // we should reuse the referrer from the original load of the current
  // document. If this reload is a client redirect (e.g., location.reload()), it
  // was initiated by something in the current document and should therefore
  // show the current document's url as the referrer.
  if (client_redirect_policy == ClientRedirectPolicy::kClientRedirect) {
    request.SetHTTPReferrer(SecurityPolicy::GenerateReferrer(
        frame_->GetDocument()->GetReferrerPolicy(),
        frame_->GetDocument()->Url(),
        frame_->GetDocument()->OutgoingReferrer()));
  }

  if (!override_url.IsEmpty()) {
    request.SetURL(override_url);
    request.ClearHTTPReferrer();
  }
  request.SetServiceWorkerMode(frame_load_type ==
                                       kFrameLoadTypeReloadBypassingCache
                                   ? WebURLRequest::ServiceWorkerMode::kNone
                                   : WebURLRequest::ServiceWorkerMode::kAll);
  return request;
}

FrameLoader::FrameLoader(LocalFrame* frame)
    : frame_(frame),
      progress_tracker_(ProgressTracker::Create(frame)),
      in_stop_all_loaders_(false),
      forced_sandbox_flags_(kSandboxNone),
      dispatching_did_clear_window_object_in_main_world_(false),
      protect_provisional_loader_(false),
      detached_(false) {
  DCHECK(frame_);
  TRACE_EVENT_OBJECT_CREATED_WITH_ID("loading", "FrameLoader", this);
  TakeObjectSnapshot();
}

FrameLoader::~FrameLoader() {
  DCHECK(detached_);
}

DEFINE_TRACE(FrameLoader) {
  visitor->Trace(frame_);
  visitor->Trace(progress_tracker_);
  visitor->Trace(document_loader_);
  visitor->Trace(provisional_document_loader_);
}

void FrameLoader::Init() {
  ScriptForbiddenScope forbid_scripts;
  ResourceRequest initial_request(KURL(kParsedURLString, g_empty_string));
  initial_request.SetRequestContext(WebURLRequest::kRequestContextInternal);
  initial_request.SetFrameType(frame_->IsMainFrame()
                                   ? WebURLRequest::kFrameTypeTopLevel
                                   : WebURLRequest::kFrameTypeNested);
  provisional_document_loader_ =
      Client()->CreateDocumentLoader(frame_, initial_request, SubstituteData(),
                                     ClientRedirectPolicy::kNotClientRedirect);
  provisional_document_loader_->StartLoadingMainResource();
  frame_->GetDocument()->CancelParsing();
  state_machine_.AdvanceTo(
      FrameLoaderStateMachine::kDisplayingInitialEmptyDocument);
  // Suppress finish notifications for initial empty documents, since they don't
  // generate start notifications.
  document_loader_->SetSentDidFinishLoad();
  if (frame_->GetPage()->Suspended())
    SetDefersLoading(true);
  TakeObjectSnapshot();
}

LocalFrameClient* FrameLoader::Client() const {
  return static_cast<LocalFrameClient*>(frame_->Client());
}

void FrameLoader::SetDefersLoading(bool defers) {
  if (provisional_document_loader_)
    provisional_document_loader_->Fetcher()->SetDefersLoading(defers);

  if (Document* document = frame_->GetDocument()) {
    document->Fetcher()->SetDefersLoading(defers);
    if (defers)
      document->SuspendScheduledTasks();
    else
      document->ResumeScheduledTasks();
  }

  if (!defers)
    frame_->GetNavigationScheduler().StartTimer();
}

void FrameLoader::SaveScrollState() {
  if (!document_loader_ || !document_loader_->GetHistoryItem() ||
      !frame_->View())
    return;

  // Shouldn't clobber anything if we might still restore later.
  if (NeedsHistoryItemRestore(document_loader_->LoadType()) &&
      !document_loader_->GetInitialScrollState().was_scrolled_by_user)
    return;

  HistoryItem* history_item = document_loader_->GetHistoryItem();
  if (ScrollableArea* layout_scrollable_area =
          frame_->View()->LayoutViewportScrollableArea())
    history_item->SetScrollOffset(layout_scrollable_area->GetScrollOffset());
  history_item->SetVisualViewportScrollOffset(ToScrollOffset(
      frame_->GetPage()->GetVisualViewport().VisibleRect().Location()));

  if (frame_->IsMainFrame())
    history_item->SetPageScaleFactor(frame_->GetPage()->PageScaleFactor());

  Client()->DidUpdateCurrentHistoryItem();
}

void FrameLoader::DispatchUnloadEvent() {
  FrameNavigationDisabler navigation_disabler(*frame_);

  // If the frame is unloading, the provisional loader should no longer be
  // protected. It will be detached soon.
  protect_provisional_loader_ = false;
  SaveScrollState();

  if (frame_->GetDocument() && !SVGImage::IsInSVGImage(frame_->GetDocument()))
    frame_->GetDocument()->DispatchUnloadEvents();
}

void FrameLoader::DidExplicitOpen() {
  // Calling document.open counts as committing the first real document load.
  if (!state_machine_.CommittedFirstRealDocumentLoad())
    state_machine_.AdvanceTo(FrameLoaderStateMachine::kCommittedFirstRealLoad);

  // Only model a document.open() as part of a navigation if its parent is not
  // done or in the process of completing.
  if (Frame* parent = frame_->Tree().Parent()) {
    if ((parent->IsLocalFrame() &&
         ToLocalFrame(parent)->GetDocument()->LoadEventStillNeeded()) ||
        (parent->IsRemoteFrame() && parent->IsLoading())) {
      progress_tracker_->ProgressStarted(document_loader_->LoadType());
    }
  }

  // Prevent window.open(url) -- eg window.open("about:blank") -- from blowing
  // away results from a subsequent window.document.open / window.document.write
  // call. Canceling redirection here works for all cases because document.open
  // implicitly precedes document.write.
  frame_->GetNavigationScheduler().Cancel();
}

void FrameLoader::Clear() {
  // clear() is called during (Local)Frame detachment or when reusing a
  // FrameLoader by putting a new Document within it
  // (DocumentLoader::ensureWriter().)
  if (state_machine_.CreatingInitialEmptyDocument())
    return;

  frame_->GetEditor().Clear();
  frame_->GetEventHandler().Clear();
  if (frame_->View())
    frame_->View()->Clear();

  if (state_machine_.IsDisplayingInitialEmptyDocument())
    state_machine_.AdvanceTo(FrameLoaderStateMachine::kCommittedFirstRealLoad);

  TakeObjectSnapshot();
}

// This is only called by ScriptController::executeScriptIfJavaScriptURL and
// always contains the result of evaluating a javascript: url. This is the
// <iframe src="javascript:'html'"> case.
void FrameLoader::ReplaceDocumentWhileExecutingJavaScriptURL(
    const String& source,
    Document* owner_document) {
  if (!frame_->GetDocument()->Loader() ||
      frame_->GetDocument()->PageDismissalEventBeingDispatched() !=
          Document::kNoDismissal)
    return;

  DocumentLoader* document_loader(frame_->GetDocument()->Loader());

  UseCounter::Count(*frame_->GetDocument(),
                    UseCounter::kReplaceDocumentViaJavaScriptURL);

  // Prepare a DocumentInit before clearing the frame, because it may need to
  // inherit an aliased security context.
  DocumentInit init(owner_document, frame_->GetDocument()->Url(), frame_);
  init.WithNewRegistrationContext();

  StopAllLoaders();
  // Don't allow any new child frames to load in this frame: attaching a new
  // child frame during or after detaching children results in an attached
  // frame on a detached DOM tree, which is bad.
  SubframeLoadingDisabler disabler(frame_->GetDocument());
  frame_->DetachChildren();
  frame_->GetDocument()->Shutdown();
  Clear();

  // detachChildren() potentially detaches the frame from the document. The
  // loading cannot continue in that case.
  if (!frame_->GetPage())
    return;

  Client()->TransitionToCommittedForNewPage();
  document_loader->ReplaceDocumentWhileExecutingJavaScriptURL(init, source);
}

void FrameLoader::FinishedParsing() {
  if (state_machine_.CreatingInitialEmptyDocument())
    return;

  progress_tracker_->FinishedParsing();

  if (Client()) {
    ScriptForbiddenScope forbid_scripts;
    Client()->DispatchDidFinishDocumentLoad();
  }

  if (Client()) {
    Client()->RunScriptsAtDocumentReady(
        document_loader_ ? document_loader_->IsCommittedButEmpty() : true);
  }

  frame_->GetDocument()->CheckCompleted();

  if (!frame_->View())
    return;

  // Check if the scrollbars are really needed for the content. If not, remove
  // them, relayout, and repaint.
  frame_->View()->RestoreScrollbar();
  ProcessFragment(frame_->GetDocument()->Url(), document_loader_->LoadType(),
                  kNavigationToDifferentDocument);
}

bool FrameLoader::AllAncestorsAreComplete() const {
  for (Frame* ancestor = frame_; ancestor;
       ancestor = ancestor->Tree().Parent()) {
    if (ancestor->IsLoading())
      return false;
  }
  return true;
}

void FrameLoader::DidFinishNavigation() {
  // We should have either finished the provisional or committed navigation if
  // this is called. Only delcare the whole frame finished if neither is in
  // progress.
  DCHECK(document_loader_->SentDidFinishLoad() || !HasProvisionalNavigation());
  if (!document_loader_->SentDidFinishLoad() || HasProvisionalNavigation())
    return;

  if (frame_->IsLoading()) {
    progress_tracker_->ProgressCompleted();
    // Retry restoring scroll offset since finishing loading disables content
    // size clamping.
    RestoreScrollPositionAndViewState();
    if (document_loader_)
      document_loader_->SetLoadType(kFrameLoadTypeStandard);
    frame_->DomWindow()->FinishedLoading();
  }

  Frame* parent = frame_->Tree().Parent();
  if (parent && parent->IsLocalFrame())
    ToLocalFrame(parent)->GetDocument()->CheckCompleted();
}

Frame* FrameLoader::Opener() {
  return Client() ? Client()->Opener() : 0;
}

void FrameLoader::SetOpener(LocalFrame* opener) {
  // If the frame is already detached, the opener has already been cleared.
  if (Client())
    Client()->SetOpener(opener);
}

bool FrameLoader::AllowPlugins(ReasonForCallingAllowPlugins reason) {
  // With Oilpan, a FrameLoader might be accessed after the Page has been
  // detached. FrameClient will not be accessible, so bail early.
  if (!Client())
    return false;
  Settings* settings = frame_->GetSettings();
  bool allowed = frame_->GetContentSettingsClient()->AllowPlugins(
      settings && settings->GetPluginsEnabled());
  if (!allowed && reason == kAboutToInstantiatePlugin)
    frame_->GetContentSettingsClient()->DidNotAllowPlugins();
  return allowed;
}

void FrameLoader::UpdateForSameDocumentNavigation(
    const KURL& new_url,
    SameDocumentNavigationSource same_document_navigation_source,
    PassRefPtr<SerializedScriptValue> data,
    HistoryScrollRestorationType scroll_restoration_type,
    FrameLoadType type,
    Document* initiating_document) {
  TRACE_EVENT1("blink", "FrameLoader::updateForSameDocumentNavigation", "url",
               new_url.GetString().Ascii().data());

  // Generate start and stop notifications only when loader is completed so that
  // we don't fire them for fragment redirection that happens in window.onload
  // handler. See https://bugs.webkit.org/show_bug.cgi?id=31838
  // Do not fire the notifications if the frame is concurrently navigating away
  // from the document, since a new document is already loading.
  if (frame_->GetDocument()->LoadEventFinished() &&
      !provisional_document_loader_)
    Client()->DidStartLoading(kNavigationWithinSameDocument);

  // Update the data source's request with the new URL to fake the URL change
  frame_->GetDocument()->SetURL(new_url);
  GetDocumentLoader()->UpdateForSameDocumentNavigation(
      new_url, same_document_navigation_source, std::move(data),
      scroll_restoration_type, type, initiating_document);

  Client()->DispatchDidReceiveTitle(frame_->GetDocument()->title());
  if (frame_->GetDocument()->LoadEventFinished() &&
      !provisional_document_loader_)
    Client()->DidStopLoading();
}

void FrameLoader::DetachDocumentLoader(Member<DocumentLoader>& loader) {
  if (!loader)
    return;

  FrameNavigationDisabler navigation_disabler(*frame_);
  loader->DetachFromFrame();
  loader = nullptr;
}

void FrameLoader::LoadInSameDocument(
    const KURL& url,
    PassRefPtr<SerializedScriptValue> state_object,
    FrameLoadType frame_load_type,
    HistoryItem* history_item,
    ClientRedirectPolicy client_redirect,
    Document* initiating_document) {
  // If we have a state object, we cannot also be a new navigation.
  DCHECK(!state_object || frame_load_type == kFrameLoadTypeBackForward);

  // If we have a provisional request for a different document, a fragment
  // scroll should cancel it.
  DetachDocumentLoader(provisional_document_loader_);

  if (!frame_->GetPage())
    return;
  SaveScrollState();

  KURL old_url = frame_->GetDocument()->Url();
  bool hash_change = EqualIgnoringFragmentIdentifier(url, old_url) &&
                     url.FragmentIdentifier() != old_url.FragmentIdentifier();
  if (hash_change) {
    // If we were in the autoscroll/middleClickAutoscroll mode we want to stop
    // it before following the link to the anchor
    frame_->GetEventHandler().StopAutoscroll();
    frame_->DomWindow()->EnqueueHashchangeEvent(old_url, url);
  }
  document_loader_->SetIsClientRedirect(client_redirect ==
                                        ClientRedirectPolicy::kClientRedirect);
  if (history_item)
    document_loader_->SetItemForHistoryNavigation(history_item);
  UpdateForSameDocumentNavigation(url, kSameDocumentNavigationDefault, nullptr,
                                  kScrollRestorationAuto, frame_load_type,
                                  initiating_document);

  document_loader_->GetInitialScrollState().was_scrolled_by_user = false;

  frame_->GetDocument()->CheckCompleted();

  frame_->DomWindow()->StatePopped(state_object
                                       ? std::move(state_object)
                                       : SerializedScriptValue::NullValue());

  if (history_item)
    RestoreScrollPositionAndViewStateForLoadType(frame_load_type);

  // We need to scroll to the fragment whether or not a hash change occurred,
  // since the user might have scrolled since the previous navigation.
  ProcessFragment(url, frame_load_type, kNavigationWithinSameDocument);
  TakeObjectSnapshot();
}

// static
void FrameLoader::SetReferrerForFrameRequest(FrameLoadRequest& frame_request) {
  ResourceRequest& request = frame_request.GetResourceRequest();
  Document* origin_document = frame_request.OriginDocument();

  if (!origin_document)
    return;
  // Anchor elements with the 'referrerpolicy' attribute will have already set
  // the referrer on the request.
  if (request.DidSetHTTPReferrer())
    return;
  if (frame_request.GetShouldSendReferrer() == kNeverSendReferrer)
    return;

  // Always use the initiating document to generate the referrer. We need to
  // generateReferrer(), because we haven't enforced ReferrerPolicy or
  // https->http referrer suppression yet.
  Referrer referrer = SecurityPolicy::GenerateReferrer(
      origin_document->GetReferrerPolicy(), request.Url(),
      origin_document->OutgoingReferrer());

  request.SetHTTPReferrer(referrer);
  request.AddHTTPOriginIfNeeded(referrer.referrer);
}

FrameLoadType FrameLoader::DetermineFrameLoadType(
    const FrameLoadRequest& request) {
  if (frame_->Tree().Parent() &&
      !state_machine_.CommittedFirstRealDocumentLoad())
    return kFrameLoadTypeInitialInChildFrame;
  if (!frame_->Tree().Parent() && !Client()->BackForwardLength()) {
    if (Opener() && request.GetResourceRequest().Url().IsEmpty())
      return kFrameLoadTypeReplaceCurrentItem;
    return kFrameLoadTypeStandard;
  }
  if (request.GetResourceRequest().GetCachePolicy() ==
      WebCachePolicy::kValidatingCacheData)
    return kFrameLoadTypeReload;
  if (request.GetResourceRequest().GetCachePolicy() ==
      WebCachePolicy::kBypassingCache)
    return kFrameLoadTypeReloadBypassingCache;
  // From the HTML5 spec for location.assign():
  // "If the browsing context's session history contains only one Document,
  // and that was the about:blank Document created when the browsing context
  // was created, then the navigation must be done with replacement enabled."
  if (request.ReplacesCurrentItem() ||
      (!state_machine_.CommittedMultipleRealLoads() &&
       DeprecatedEqualIgnoringCase(frame_->GetDocument()->Url(), BlankURL())))
    return kFrameLoadTypeReplaceCurrentItem;

  if (request.GetResourceRequest().Url() == document_loader_->UrlForHistory()) {
    if (request.GetResourceRequest().HttpMethod() == HTTPNames::POST)
      return kFrameLoadTypeStandard;
    if (!request.OriginDocument())
      return kFrameLoadTypeReload;
    return kFrameLoadTypeReplaceCurrentItem;
  }

  if (request.GetSubstituteData().FailingURL() ==
          document_loader_->UrlForHistory() &&
      document_loader_->LoadType() == kFrameLoadTypeReload)
    return kFrameLoadTypeReload;

  if (request.OriginDocument() &&
      !request.OriginDocument()->CanCreateHistoryEntry())
    return kFrameLoadTypeReplaceCurrentItem;

  if (request.GetResourceRequest().Url().IsEmpty() &&
      request.GetSubstituteData().FailingURL().IsEmpty()) {
    return kFrameLoadTypeReplaceCurrentItem;
  }

  return kFrameLoadTypeStandard;
}

bool FrameLoader::PrepareRequestForThisFrame(FrameLoadRequest& request) {
  // If no origin Document* was specified, skip remaining security checks and
  // assume the caller has fully initialized the FrameLoadRequest.
  if (!request.OriginDocument())
    return true;

  KURL url = request.GetResourceRequest().Url();
  if (frame_->GetScriptController().ExecuteScriptIfJavaScriptURL(url, nullptr))
    return false;

  if (!request.OriginDocument()->GetSecurityOrigin()->CanDisplay(url)) {
    ReportLocalLoadFailed(frame_, url.ElidedString());
    return false;
  }

  // Block renderer-initiated loads of data URLs in the top frame. If the mime
  // type of the data URL is supported, the URL will eventually be rendered, so
  // block it here. Otherwise, the load might be handled by a plugin or end up
  // as a download, so allow it to let the embedder figure out what to do with
  // it.
  if (frame_->IsMainFrame() &&
      !request.GetResourceRequest().IsSameDocumentNavigation() &&
      !frame_->Client()->AllowContentInitiatedDataUrlNavigations(
          request.OriginDocument()->Url()) &&
      url.ProtocolIsData() && NetworkUtils::IsDataURLMimeTypeSupported(url)) {
    frame_->GetDocument()->AddConsoleMessage(ConsoleMessage::Create(
        kSecurityMessageSource, kErrorMessageLevel,
        "Not allowed to navigate top frame to data URL: " +
            url.ElidedString()));
    return false;
  }

  if (!request.Form() && request.FrameName().IsEmpty())
    request.SetFrameName(frame_->GetDocument()->BaseTarget());
  return true;
}

static bool ShouldNavigateTargetFrame(NavigationPolicy policy) {
  switch (policy) {
    case kNavigationPolicyCurrentTab:
      return true;

    // Navigation will target a *new* frame (e.g. because of a ctrl-click),
    // so the target frame can be ignored.
    case kNavigationPolicyNewBackgroundTab:
    case kNavigationPolicyNewForegroundTab:
    case kNavigationPolicyNewWindow:
    case kNavigationPolicyNewPopup:
      return false;

    // Navigation won't really target any specific frame,
    // so the target frame can be ignored.
    case kNavigationPolicyIgnore:
    case kNavigationPolicyDownload:
      return false;

    case kNavigationPolicyHandledByClient:
      // Impossible, because at this point we shouldn't yet have called
      // client()->decidePolicyForNavigation(...).
      NOTREACHED();
      return true;

    default:
      NOTREACHED() << policy;
      return true;
  }
}

static NavigationType DetermineNavigationType(FrameLoadType frame_load_type,
                                              bool is_form_submission,
                                              bool have_event) {
  bool is_reload = IsReloadLoadType(frame_load_type);
  bool is_back_forward = IsBackForwardLoadType(frame_load_type);
  if (is_form_submission) {
    return (is_reload || is_back_forward) ? kNavigationTypeFormResubmitted
                                          : kNavigationTypeFormSubmitted;
  }
  if (have_event)
    return kNavigationTypeLinkClicked;
  if (is_reload)
    return kNavigationTypeReload;
  if (is_back_forward)
    return kNavigationTypeBackForward;
  return kNavigationTypeOther;
}

static WebURLRequest::RequestContext DetermineRequestContextFromNavigationType(
    const NavigationType navigation_type) {
  switch (navigation_type) {
    case kNavigationTypeLinkClicked:
      return WebURLRequest::kRequestContextHyperlink;

    case kNavigationTypeOther:
      return WebURLRequest::kRequestContextLocation;

    case kNavigationTypeFormResubmitted:
    case kNavigationTypeFormSubmitted:
      return WebURLRequest::kRequestContextForm;

    case kNavigationTypeBackForward:
    case kNavigationTypeReload:
      return WebURLRequest::kRequestContextInternal;
  }
  NOTREACHED();
  return WebURLRequest::kRequestContextHyperlink;
}

static NavigationPolicy NavigationPolicyForRequest(
    const FrameLoadRequest& request) {
  NavigationPolicy policy = kNavigationPolicyCurrentTab;
  Event* event = request.TriggeringEvent();
  if (!event)
    return policy;

  if (request.Form() && event->UnderlyingEvent())
    event = event->UnderlyingEvent();

  if (event->IsMouseEvent()) {
    MouseEvent* mouse_event = ToMouseEvent(event);
    NavigationPolicyFromMouseEvent(
        mouse_event->button(), mouse_event->ctrlKey(), mouse_event->shiftKey(),
        mouse_event->altKey(), mouse_event->metaKey(), &policy);
  } else if (event->IsKeyboardEvent()) {
    // The click is simulated when triggering the keypress event.
    KeyboardEvent* key_event = ToKeyboardEvent(event);
    NavigationPolicyFromMouseEvent(0, key_event->ctrlKey(),
                                   key_event->shiftKey(), key_event->altKey(),
                                   key_event->metaKey(), &policy);
  } else if (event->IsGestureEvent()) {
    // The click is simulated when triggering the gesture-tap event
    GestureEvent* gesture_event = ToGestureEvent(event);
    NavigationPolicyFromMouseEvent(
        0, gesture_event->ctrlKey(), gesture_event->shiftKey(),
        gesture_event->altKey(), gesture_event->metaKey(), &policy);
  }
  return policy;
}

void FrameLoader::Load(const FrameLoadRequest& passed_request,
                       FrameLoadType frame_load_type,
                       HistoryItem* history_item,
                       HistoryLoadType history_load_type) {
  DCHECK(frame_->GetDocument());

  if (IsBackForwardLoadType(frame_load_type) && !frame_->IsNavigationAllowed())
    return;

  if (in_stop_all_loaders_)
    return;

  FrameLoadRequest request(passed_request);
  request.GetResourceRequest().SetHasUserGesture(
      UserGestureIndicator::ProcessingUserGesture());

  if (!PrepareRequestForThisFrame(request))
    return;

  // Form submissions appear to need their special-case of finding the target at
  // schedule rather than at fire.
  Frame* target_frame = request.Form()
                            ? nullptr
                            : frame_->FindFrameForNavigation(
                                  AtomicString(request.FrameName()), *frame_);

  NavigationPolicy policy = NavigationPolicyForRequest(request);
  if (target_frame && target_frame != frame_ &&
      ShouldNavigateTargetFrame(policy)) {
    if (target_frame->IsLocalFrame() &&
        !ToLocalFrame(target_frame)->IsNavigationAllowed()) {
      return;
    }

    bool was_in_same_page = target_frame->GetPage() == frame_->GetPage();

    request.SetFrameName("_self");
    target_frame->Navigate(request);
    Page* page = target_frame->GetPage();
    if (!was_in_same_page && page)
      page->GetChromeClient().Focus();
    return;
  }

  SetReferrerForFrameRequest(request);

  if (!target_frame && !request.FrameName().IsEmpty()) {
    if (policy == kNavigationPolicyDownload) {
      Client()->LoadURLExternally(request.GetResourceRequest(),
                                  kNavigationPolicyDownload, String(), false);
      return;  // Navigation/download will be handled by the client.
    } else if (ShouldNavigateTargetFrame(policy)) {
      request.GetResourceRequest().SetFrameType(
          WebURLRequest::kFrameTypeAuxiliary);
      CreateWindowForRequest(request, *frame_, policy);
      return;  // Navigation will be handled by the new frame/window.
    }
  }

  if (!frame_->IsNavigationAllowed())
    return;

  const KURL& url = request.GetResourceRequest().Url();
  FrameLoadType new_load_type = (frame_load_type == kFrameLoadTypeStandard)
                                    ? DetermineFrameLoadType(request)
                                    : frame_load_type;
  bool same_document_history_navigation =
      IsBackForwardLoadType(new_load_type) &&
      history_load_type == kHistorySameDocumentLoad;
  bool same_document_navigation =
      policy == kNavigationPolicyCurrentTab &&
      ShouldPerformFragmentNavigation(request.Form(),
                                      request.GetResourceRequest().HttpMethod(),
                                      new_load_type, url);

  // Perform same document navigation.
  if (same_document_history_navigation || same_document_navigation) {
    DCHECK(history_item || !same_document_history_navigation);
    RefPtr<SerializedScriptValue> state_object =
        same_document_history_navigation ? history_item->StateObject()
                                         : nullptr;

    if (!same_document_history_navigation) {
      document_loader_->SetNavigationType(DetermineNavigationType(
          new_load_type, false, request.TriggeringEvent()));
      if (ShouldTreatURLAsSameAsCurrent(url))
        new_load_type = kFrameLoadTypeReplaceCurrentItem;
    }

    LoadInSameDocument(url, state_object, new_load_type, history_item,
                       request.ClientRedirect(), request.OriginDocument());
    return;
  }

  // PlzNavigate
  // If the loader classifies this navigation as a different document navigation
  // while the browser intended the navigation to be same-document, it means
  // that a different navigation must have committed while the IPC was sent.
  // This navigation is no more same-document. The navigation is simply dropped.
  if (request.GetResourceRequest().IsSameDocumentNavigation())
    return;

  StartLoad(request, new_load_type, policy, history_item);
}

SubstituteData FrameLoader::DefaultSubstituteDataForURL(const KURL& url) {
  if (!ShouldTreatURLAsSrcdocDocument(url))
    return SubstituteData();
  String srcdoc = frame_->DeprecatedLocalOwner()->FastGetAttribute(srcdocAttr);
  DCHECK(!srcdoc.IsNull());
  CString encoded_srcdoc = srcdoc.Utf8();
  return SubstituteData(
      SharedBuffer::Create(encoded_srcdoc.data(), encoded_srcdoc.length()),
      "text/html", "UTF-8", KURL());
}

void FrameLoader::ReportLocalLoadFailed(LocalFrame* frame, const String& url) {
  DCHECK(!url.IsEmpty());
  if (!frame)
    return;

  frame->GetDocument()->AddConsoleMessage(
      ConsoleMessage::Create(kSecurityMessageSource, kErrorMessageLevel,
                             "Not allowed to load local resource: " + url));
}

void FrameLoader::StopAllLoaders() {
  if (frame_->GetDocument()->PageDismissalEventBeingDispatched() !=
      Document::kNoDismissal)
    return;

  // If this method is called from within this method, infinite recursion can
  // occur (3442218). Avoid this.
  if (in_stop_all_loaders_)
    return;

  in_stop_all_loaders_ = true;

  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (child->IsLocalFrame())
      ToLocalFrame(child)->Loader().StopAllLoaders();
  }

  frame_->GetDocument()->CancelParsing();
  if (document_loader_)
    document_loader_->Fetcher()->StopFetching();
  if (!protect_provisional_loader_)
    DetachDocumentLoader(provisional_document_loader_);

  frame_->GetNavigationScheduler().Cancel();

  // It's possible that the above actions won't have stopped loading if load
  // completion had been blocked on parsing or if we were in the middle of
  // committing an empty document. In that case, emulate a failed navigation.
  if (!provisional_document_loader_ && document_loader_ &&
      frame_->IsLoading()) {
    document_loader_->LoadFailed(
        ResourceError::CancelledError(document_loader_->Url()));
  }

  in_stop_all_loaders_ = false;
  TakeObjectSnapshot();
}

void FrameLoader::DidAccessInitialDocument() {
  // We only need to notify the client for the main frame.
  if (IsLoadingMainFrame()) {
    // Forbid script execution to prevent re-entering V8, since this is called
    // from a binding security check.
    ScriptForbiddenScope forbid_scripts;
    if (Client())
      Client()->DidAccessInitialDocument();
  }
}

bool FrameLoader::PrepareForCommit() {
  PluginScriptForbiddenScope forbid_plugin_destructor_scripting;
  DocumentLoader* pdl = provisional_document_loader_;

  if (frame_->GetDocument()) {
    unsigned node_count = 0;
    for (Frame* frame = frame_; frame; frame = frame->Tree().TraverseNext()) {
      if (frame->IsLocalFrame()) {
        LocalFrame* local_frame = ToLocalFrame(frame);
        node_count += local_frame->GetDocument()->NodeCount();
      }
    }
    unsigned total_node_count =
        InstanceCounters::CounterValue(InstanceCounters::kNodeCounter);
    float ratio = static_cast<float>(node_count) / total_node_count;
    ThreadState::Current()->SchedulePageNavigationGCIfNeeded(ratio);
  }

  // Don't allow any new child frames to load in this frame: attaching a new
  // child frame during or after detaching children results in an attached frame
  // on a detached DOM tree, which is bad.
  SubframeLoadingDisabler disabler(frame_->GetDocument());
  if (document_loader_) {
    Client()->DispatchWillCommitProvisionalLoad();
    DispatchUnloadEvent();
  }
  frame_->DetachChildren();
  // The previous calls to dispatchUnloadEvent() and detachChildren() can
  // execute arbitrary script via things like unload events. If the executed
  // script intiates a new load or causes the current frame to be detached, we
  // need to abandon the current load.
  if (pdl != provisional_document_loader_)
    return false;
  // detachFromFrame() will abort XHRs that haven't completed, which can trigger
  // event listeners for 'abort'. These event listeners might call
  // window.stop(), which will in turn detach the provisional document loader.
  // At this point, the provisional document loader should not detach, because
  // then the FrameLoader would not have any attached DocumentLoaders.
  if (document_loader_) {
    AutoReset<bool> in_detach_document_loader(&protect_provisional_loader_,
                                              true);
    DetachDocumentLoader(document_loader_);
  }
  // 'abort' listeners can also detach the frame.
  if (!frame_->Client())
    return false;
  DCHECK_EQ(provisional_document_loader_, pdl);
  // No more events will be dispatched so detach the Document.
  // TODO(yoav): Should we also be nullifying domWindow's document (or
  // domWindow) since the doc is now detached?
  if (frame_->GetDocument())
    frame_->GetDocument()->Shutdown();
  document_loader_ = provisional_document_loader_.Release();
  if (document_loader_)
    document_loader_->MarkAsCommitted();
  TakeObjectSnapshot();

  return true;
}

void FrameLoader::CommitProvisionalLoad() {
  DCHECK(Client()->HasWebView());

  // Check if the destination page is allowed to access the previous page's
  // timing information.
  if (frame_->GetDocument()) {
    RefPtr<SecurityOrigin> security_origin = SecurityOrigin::Create(
        provisional_document_loader_->GetRequest().Url());
    provisional_document_loader_->GetTiming()
        .SetHasSameOriginAsPreviousDocument(
            security_origin->CanRequest(frame_->GetDocument()->Url()));
  }

  if (!PrepareForCommit())
    return;

  // If we are loading the mainframe, or a frame that is a local root, it is
  // important to explicitly set the event listenener properties to Nothing as
  // this triggers notifications to the client. Clients may assume the presence
  // of handlers for touch and wheel events, so these notifications tell it
  // there are (presently) no handlers.
  if (frame_->IsLocalRoot()) {
    frame_->GetPage()->GetChromeClient().SetEventListenerProperties(
        frame_, WebEventListenerClass::kTouchStartOrMove,
        WebEventListenerProperties::kNothing);
    frame_->GetPage()->GetChromeClient().SetEventListenerProperties(
        frame_, WebEventListenerClass::kMouseWheel,
        WebEventListenerProperties::kNothing);
    frame_->GetPage()->GetChromeClient().SetEventListenerProperties(
        frame_, WebEventListenerClass::kTouchEndOrCancel,
        WebEventListenerProperties::kNothing);
  }

  Client()->TransitionToCommittedForNewPage();

  frame_->GetNavigationScheduler().Cancel();

  // If we are still in the process of initializing an empty document then its
  // frame is not in a consistent state for rendering, so avoid
  // setJSStatusBarText since it may cause clients to attempt to render the
  // frame.
  if (!state_machine_.CreatingInitialEmptyDocument()) {
    LocalDOMWindow* window = frame_->DomWindow();
    window->setStatus(String());
    window->setDefaultStatus(String());
  }
}

bool FrameLoader::IsLoadingMainFrame() const {
  return frame_->IsMainFrame();
}

void FrameLoader::RestoreScrollPositionAndViewState() {
  if (!frame_->GetPage() || !GetDocumentLoader())
    return;
  RestoreScrollPositionAndViewStateForLoadType(GetDocumentLoader()->LoadType());
}

void FrameLoader::RestoreScrollPositionAndViewStateForLoadType(
    FrameLoadType load_type) {
  FrameView* view = frame_->View();
  if (!view || !view->LayoutViewportScrollableArea() ||
      !state_machine_.CommittedFirstRealDocumentLoad()) {
    return;
  }
  if (!NeedsHistoryItemRestore(load_type))
    return;
  HistoryItem* history_item = document_loader_->GetHistoryItem();
  if (!history_item || !history_item->DidSaveScrollOrScaleState())
    return;

  bool should_restore_scroll =
      history_item->ScrollRestorationType() != kScrollRestorationManual;
  bool should_restore_scale = history_item->PageScaleFactor();

  // This tries to balance:
  // 1. restoring as soon as possible
  // 2. not overriding user scroll (TODO(majidvp): also respect user scale)
  // 3. detecting clamping to avoid repeatedly popping the scroll position down
  //    as the page height increases
  // 4. ignore clamp detection if we are not restoring scroll or after load
  //    completes because that may be because the page will never reach its
  //    previous height
  bool can_restore_without_clamping =
      view->LayoutViewportScrollableArea()->ClampScrollOffset(
          history_item->GetScrollOffset()) == history_item->GetScrollOffset();
  bool can_restore_without_annoying_user =
      !GetDocumentLoader()->GetInitialScrollState().was_scrolled_by_user &&
      (can_restore_without_clamping || !frame_->IsLoading() ||
       !should_restore_scroll);
  if (!can_restore_without_annoying_user)
    return;

  if (should_restore_scroll) {
    view->LayoutViewportScrollableArea()->SetScrollOffset(
        history_item->GetScrollOffset(), kProgrammaticScroll);
  }

  // For main frame restore scale and visual viewport position
  if (frame_->IsMainFrame()) {
    ScrollOffset visual_viewport_offset(
        history_item->VisualViewportScrollOffset());

    // If the visual viewport's offset is (-1, -1) it means the history item
    // is an old version of HistoryItem so distribute the scroll between
    // the main frame and the visual viewport as best as we can.
    if (visual_viewport_offset.Width() == -1 &&
        visual_viewport_offset.Height() == -1) {
      visual_viewport_offset =
          history_item->GetScrollOffset() -
          view->LayoutViewportScrollableArea()->GetScrollOffset();
    }

    VisualViewport& visual_viewport = frame_->GetPage()->GetVisualViewport();
    if (should_restore_scale && should_restore_scroll) {
      visual_viewport.SetScaleAndLocation(history_item->PageScaleFactor(),
                                          FloatPoint(visual_viewport_offset));
    } else if (should_restore_scale) {
      visual_viewport.SetScale(history_item->PageScaleFactor());
    } else if (should_restore_scroll) {
      visual_viewport.SetLocation(FloatPoint(visual_viewport_offset));
    }

    if (ScrollingCoordinator* scrolling_coordinator =
            frame_->GetPage()->GetScrollingCoordinator())
      scrolling_coordinator->FrameViewRootLayerDidChange(view);
  }

  GetDocumentLoader()->GetInitialScrollState().did_restore_from_history = true;
}

String FrameLoader::UserAgent() const {
  String user_agent = Client()->UserAgent();
  probe::applyUserAgentOverride(frame_, &user_agent);
  return user_agent;
}

void FrameLoader::Detach() {
  DetachDocumentLoader(document_loader_);
  DetachDocumentLoader(provisional_document_loader_);

  if (progress_tracker_) {
    progress_tracker_->Dispose();
    progress_tracker_.Clear();
  }

  TRACE_EVENT_OBJECT_DELETED_WITH_ID("loading", "FrameLoader", this);
  detached_ = true;
}

void FrameLoader::DetachProvisionalDocumentLoader(DocumentLoader* loader) {
  DCHECK_EQ(loader, provisional_document_loader_);
  DetachDocumentLoader(provisional_document_loader_);
  DidFinishNavigation();
}

bool FrameLoader::ShouldPerformFragmentNavigation(bool is_form_submission,
                                                  const String& http_method,
                                                  FrameLoadType load_type,
                                                  const KURL& url) {
  // We don't do this if we are submitting a form with method other than "GET",
  // explicitly reloading, currently displaying a frameset, or if the URL does
  // not have a fragment.
  return DeprecatedEqualIgnoringCase(http_method, HTTPNames::GET) &&
         !IsReloadLoadType(load_type) &&
         load_type != kFrameLoadTypeBackForward &&
         url.HasFragmentIdentifier() &&
         EqualIgnoringFragmentIdentifier(frame_->GetDocument()->Url(), url)
         // We don't want to just scroll if a link from within a frameset is
         // trying to reload the frameset into _top.
         && !frame_->GetDocument()->IsFrameSet();
}

void FrameLoader::ProcessFragment(const KURL& url,
                                  FrameLoadType frame_load_type,
                                  LoadStartType load_start_type) {
  FrameView* view = frame_->View();
  if (!view)
    return;

  // Leaking scroll position to a cross-origin ancestor would permit the
  // so-called "framesniffing" attack.
  Frame* boundary_frame =
      url.HasFragmentIdentifier()
          ? frame_->FindUnsafeParentScrollPropagationBoundary()
          : 0;

  // FIXME: Handle RemoteFrames
  if (boundary_frame && boundary_frame->IsLocalFrame()) {
    ToLocalFrame(boundary_frame)
        ->View()
        ->SetSafeToPropagateScrollToParent(false);
  }

  // If scroll position is restored from history fragment or scroll
  // restoration type is manual, then we should not override it unless this
  // is a same document reload.
  bool should_scroll_to_fragment =
      (load_start_type == kNavigationWithinSameDocument &&
       !IsBackForwardLoadType(frame_load_type)) ||
      (!GetDocumentLoader()->GetInitialScrollState().did_restore_from_history &&
       !(GetDocumentLoader()->GetHistoryItem() &&
         GetDocumentLoader()->GetHistoryItem()->ScrollRestorationType() ==
             kScrollRestorationManual));

  view->ProcessUrlFragment(url, should_scroll_to_fragment
                                    ? FrameView::kUrlFragmentScroll
                                    : FrameView::kUrlFragmentDontScroll);

  if (boundary_frame && boundary_frame->IsLocalFrame())
    ToLocalFrame(boundary_frame)
        ->View()
        ->SetSafeToPropagateScrollToParent(true);
}

bool FrameLoader::ShouldClose(bool is_reload) {
  Page* page = frame_->GetPage();
  if (!page || !page->GetChromeClient().CanOpenBeforeUnloadConfirmPanel())
    return true;

  // Store all references to each subframe in advance since beforeunload's event
  // handler may modify frame
  HeapVector<Member<LocalFrame>> target_frames;
  target_frames.push_back(frame_);
  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().TraverseNext(frame_)) {
    // FIXME: There is not yet any way to dispatch events to out-of-process
    // frames.
    if (child->IsLocalFrame())
      target_frames.push_back(ToLocalFrame(child));
  }

  bool should_close = false;
  {
    NavigationDisablerForBeforeUnload navigation_disabler;
    size_t i;

    bool did_allow_navigation = false;
    for (i = 0; i < target_frames.size(); i++) {
      if (!target_frames[i]->Tree().IsDescendantOf(frame_))
        continue;
      if (!target_frames[i]->GetDocument()->DispatchBeforeUnloadEvent(
              page->GetChromeClient(), is_reload, did_allow_navigation))
        break;
    }

    if (i == target_frames.size())
      should_close = true;
  }

  return should_close;
}

NavigationPolicy FrameLoader::ShouldContinueForNavigationPolicy(
    const ResourceRequest& request,
    const SubstituteData& substitute_data,
    DocumentLoader* loader,
    ContentSecurityPolicyDisposition
        should_check_main_world_content_security_policy,
    NavigationType type,
    NavigationPolicy policy,
    FrameLoadType frame_load_type,
    bool is_client_redirect,
    HTMLFormElement* form) {
  // Don't ask if we are loading an empty URL.
  if (request.Url().IsEmpty() || substitute_data.IsValid())
    return kNavigationPolicyCurrentTab;

  Settings* settings = frame_->GetSettings();
  if (MaybeCheckCSP(request, type, frame_, policy,
                    should_check_main_world_content_security_policy ==
                        kCheckContentSecurityPolicy,
                    settings && settings->GetBrowserSideNavigationEnabled(),
                    ContentSecurityPolicy::CheckHeaderType::kCheckEnforce) ==
      kNavigationPolicyIgnore) {
    return kNavigationPolicyIgnore;
  }

  bool replaces_current_history_item =
      frame_load_type == kFrameLoadTypeReplaceCurrentItem;
  policy = Client()->DecidePolicyForNavigation(
      request, loader, type, policy, replaces_current_history_item,
      is_client_redirect, form,
      should_check_main_world_content_security_policy);
  if (policy == kNavigationPolicyCurrentTab ||
      policy == kNavigationPolicyIgnore ||
      policy == kNavigationPolicyHandledByClient ||
      policy == kNavigationPolicyHandledByClientForInitialHistory) {
    return policy;
  }

  if (!LocalDOMWindow::AllowPopUp(*frame_) &&
      !UserGestureIndicator::ProcessingUserGesture())
    return kNavigationPolicyIgnore;
  Client()->LoadURLExternally(request, policy, String(),
                              replaces_current_history_item);
  return kNavigationPolicyIgnore;
}

NavigationPolicy FrameLoader::ShouldContinueForRedirectNavigationPolicy(
    const ResourceRequest& request,
    const SubstituteData& substitute_data,
    DocumentLoader* loader,
    ContentSecurityPolicyDisposition
        should_check_main_world_content_security_policy,
    NavigationType type,
    NavigationPolicy policy,
    FrameLoadType frame_load_type,
    bool is_client_redirect,
    HTMLFormElement* form) {
  Settings* settings = frame_->GetSettings();
  // Check report-only CSP policies, which are not checked by
  // ShouldContinueForNavigationPolicy.
  MaybeCheckCSP(request, type, frame_, policy,
                should_check_main_world_content_security_policy ==
                    kCheckContentSecurityPolicy,
                settings && settings->GetBrowserSideNavigationEnabled(),
                ContentSecurityPolicy::CheckHeaderType::kCheckReportOnly);
  return ShouldContinueForNavigationPolicy(
      request, substitute_data, loader,
      should_check_main_world_content_security_policy, type, policy,
      frame_load_type, is_client_redirect, form);
}

NavigationPolicy FrameLoader::CheckLoadCanStart(
    FrameLoadRequest& frame_load_request,
    FrameLoadType type,
    NavigationPolicy navigation_policy,
    NavigationType navigation_type) {
  if (frame_->GetDocument()->PageDismissalEventBeingDispatched() !=
      Document::kNoDismissal) {
    return kNavigationPolicyIgnore;
  }

  // Record the latest requiredCSP value that will be used when sending this
  // request.
  ResourceRequest& resource_request = frame_load_request.GetResourceRequest();
  RecordLatestRequiredCSP();
  // Before modifying the request, check report-only CSP headers to give the
  // site owner a chance to learn about requests that need to be modified.
  //
  // TODO(estark): this doesn't work with --enable-browser-side-navigation,
  // wherein 'frame-src' is checked in the browser process. Figure out what to
  // do; maybe with browser-side navigation the upgrade should be happening in
  // the browser process too. See also https://crbug.com/692595
  Settings* settings = frame_->GetSettings();
  MaybeCheckCSP(
      resource_request, navigation_type, frame_, navigation_policy,
      frame_load_request.ShouldCheckMainWorldContentSecurityPolicy() ==
          kCheckContentSecurityPolicy,
      settings && settings->GetBrowserSideNavigationEnabled(),
      ContentSecurityPolicy::CheckHeaderType::kCheckReportOnly);
  ModifyRequestForCSP(resource_request, nullptr);

  return ShouldContinueForNavigationPolicy(
      resource_request, frame_load_request.GetSubstituteData(), nullptr,
      frame_load_request.ShouldCheckMainWorldContentSecurityPolicy(),
      navigation_type, navigation_policy, type,
      frame_load_request.ClientRedirect() ==
          ClientRedirectPolicy::kClientRedirect,
      frame_load_request.Form());
}

void FrameLoader::StartLoad(FrameLoadRequest& frame_load_request,
                            FrameLoadType type,
                            NavigationPolicy navigation_policy,
                            HistoryItem* history_item) {
  DCHECK(Client()->HasWebView());
  ResourceRequest& resource_request = frame_load_request.GetResourceRequest();
  NavigationType navigation_type = DetermineNavigationType(
      type, resource_request.HttpBody() || frame_load_request.Form(),
      frame_load_request.TriggeringEvent());
  resource_request.SetRequestContext(
      DetermineRequestContextFromNavigationType(navigation_type));
  resource_request.SetFrameType(frame_->IsMainFrame()
                                    ? WebURLRequest::kFrameTypeTopLevel
                                    : WebURLRequest::kFrameTypeNested);

  bool had_placeholder_client_document_loader =
      provisional_document_loader_ && !provisional_document_loader_->DidStart();
  navigation_policy = CheckLoadCanStart(frame_load_request, type,
                                        navigation_policy, navigation_type);
  if (navigation_policy == kNavigationPolicyIgnore) {
    if (had_placeholder_client_document_loader &&
        !resource_request.CheckForBrowserSideNavigation()) {
      DetachDocumentLoader(provisional_document_loader_);
    }
    return;
  }

  // For PlzNavigate placeholder DocumentLoaders, don't send failure callbacks
  // for a placeholder simply being replaced with a new DocumentLoader.
  if (had_placeholder_client_document_loader)
    provisional_document_loader_->SetSentDidFinishLoad();
  frame_->GetDocument()->CancelParsing();
  DetachDocumentLoader(provisional_document_loader_);

  // beforeunload fired above, and detaching a DocumentLoader can fire events,
  // which can detach this frame.
  if (!frame_->GetPage())
    return;

  progress_tracker_->ProgressStarted(type);
  // TODO(japhet): This case wants to flag the frame as loading and do nothing
  // else. It'd be nice if it could go through the placeholder DocumentLoader
  // path, too.
  if (navigation_policy == kNavigationPolicyHandledByClientForInitialHistory)
    return;
  DCHECK(navigation_policy == kNavigationPolicyCurrentTab ||
         navigation_policy == kNavigationPolicyHandledByClient);

  provisional_document_loader_ = CreateDocumentLoader(
      resource_request, frame_load_request, type, navigation_type);

  // PlzNavigate: We need to ensure that script initiated navigations are
  // honored.
  if (!had_placeholder_client_document_loader ||
      navigation_policy == kNavigationPolicyHandledByClient) {
    frame_->GetNavigationScheduler().Cancel();
  }

  if (frame_load_request.Form())
    Client()->DispatchWillSubmitForm(frame_load_request.Form());

  provisional_document_loader_->AppendRedirect(
      provisional_document_loader_->GetRequest().Url());

  if (IsBackForwardLoadType(type)) {
    DCHECK(history_item);
    provisional_document_loader_->SetItemForHistoryNavigation(history_item);
  }

  // TODO(ananta):
  // We should get rid of the dependency on the DocumentLoader in consumers of
  // the didStartProvisionalLoad() notification.
  Client()->DispatchDidStartProvisionalLoad(provisional_document_loader_,
                                            resource_request);
  DCHECK(provisional_document_loader_);

  if (navigation_policy == kNavigationPolicyCurrentTab) {
    provisional_document_loader_->StartLoadingMainResource();
    // This should happen after the request is sent, so that the state
    // the inspector stored in the matching frameScheduledClientNavigation()
    // is available while sending the request.
    probe::frameClearedScheduledClientNavigation(frame_);
  } else {
    // PlzNavigate
    // Check for usage of legacy schemes now. Unsupported schemes will be
    // rewritten by the client, so the FrameFetchContext will not be able to
    // check for those when the navigation commits.
    if (navigation_policy == kNavigationPolicyHandledByClient)
      CheckForLegacyProtocolInSubresource(resource_request,
                                          frame_->GetDocument());
    probe::frameScheduledClientNavigation(frame_);
  }

  TakeObjectSnapshot();
}

void FrameLoader::ApplyUserAgent(ResourceRequest& request) {
  String user_agent = this->UserAgent();
  DCHECK(!user_agent.IsNull());
  request.SetHTTPUserAgent(AtomicString(user_agent));
}

bool FrameLoader::ShouldTreatURLAsSameAsCurrent(const KURL& url) const {
  return document_loader_->GetHistoryItem() &&
         url == document_loader_->GetHistoryItem()->Url();
}

bool FrameLoader::ShouldTreatURLAsSrcdocDocument(const KURL& url) const {
  if (!url.IsAboutSrcdocURL())
    return false;
  HTMLFrameOwnerElement* owner_element = frame_->DeprecatedLocalOwner();
  if (!isHTMLIFrameElement(owner_element))
    return false;
  return owner_element->FastHasAttribute(srcdocAttr);
}

void FrameLoader::DispatchDocumentElementAvailable() {
  ScriptForbiddenScope forbid_scripts;
  Client()->DocumentElementAvailable();
}

void FrameLoader::RunScriptsAtDocumentElementAvailable() {
  Client()->RunScriptsAtDocumentElementAvailable();
  // The frame might be detached at this point.
}

void FrameLoader::DispatchDidClearDocumentOfWindowObject() {
  DCHECK(frame_->GetDocument());
  if (state_machine_.CreatingInitialEmptyDocument())
    return;
  if (!frame_->GetDocument()->CanExecuteScripts(kNotAboutToExecuteScript))
    return;

  Settings* settings = frame_->GetSettings();
  if (settings && settings->GetForceMainWorldInitialization()) {
    // Forcibly instantiate WindowProxy.
    frame_->GetScriptController().WindowProxy(DOMWrapperWorld::MainWorld());
  }
  probe::didClearDocumentOfWindowObject(frame_);

  if (dispatching_did_clear_window_object_in_main_world_)
    return;
  AutoReset<bool> in_did_clear_window_object(
      &dispatching_did_clear_window_object_in_main_world_, true);
  // We just cleared the document, not the entire window object, but for the
  // embedder that's close enough.
  Client()->DispatchDidClearWindowObjectInMainWorld();
}

void FrameLoader::DispatchDidClearWindowObjectInMainWorld() {
  DCHECK(frame_->GetDocument());
  if (!frame_->GetDocument()->CanExecuteScripts(kNotAboutToExecuteScript))
    return;

  if (dispatching_did_clear_window_object_in_main_world_)
    return;
  AutoReset<bool> in_did_clear_window_object(
      &dispatching_did_clear_window_object_in_main_world_, true);
  Client()->DispatchDidClearWindowObjectInMainWorld();
}

SandboxFlags FrameLoader::EffectiveSandboxFlags() const {
  SandboxFlags flags = forced_sandbox_flags_;
  if (FrameOwner* frame_owner = frame_->Owner())
    flags |= frame_owner->GetSandboxFlags();
  // Frames need to inherit the sandbox flags of their parent frame.
  if (Frame* parent_frame = frame_->Tree().Parent())
    flags |= parent_frame->GetSecurityContext()->GetSandboxFlags();
  return flags;
}

WebInsecureRequestPolicy FrameLoader::GetInsecureRequestPolicy() const {
  Frame* parent_frame = frame_->Tree().Parent();
  if (!parent_frame)
    return kLeaveInsecureRequestsAlone;

  return parent_frame->GetSecurityContext()->GetInsecureRequestPolicy();
}

SecurityContext::InsecureNavigationsSet*
FrameLoader::InsecureNavigationsToUpgrade() const {
  DCHECK(frame_);
  Frame* parent_frame = frame_->Tree().Parent();
  if (!parent_frame)
    return nullptr;

  // FIXME: We need a way to propagate insecure requests policy flags to
  // out-of-process frames. For now, we'll always use default behavior.
  if (!parent_frame->IsLocalFrame())
    return nullptr;

  DCHECK(ToLocalFrame(parent_frame)->GetDocument());
  return ToLocalFrame(parent_frame)
      ->GetDocument()
      ->InsecureNavigationsToUpgrade();
}

void FrameLoader::ModifyRequestForCSP(ResourceRequest& resource_request,
                                      Document* document) const {
  if (RuntimeEnabledFeatures::embedderCSPEnforcementEnabled() &&
      !RequiredCSP().IsEmpty()) {
    // TODO(amalika): Strengthen this DCHECK that requiredCSP has proper format
    DCHECK(RequiredCSP().GetString().ContainsOnlyASCII());
    resource_request.SetHTTPHeaderField(HTTPNames::Required_CSP, RequiredCSP());
  }

  // Tack an 'Upgrade-Insecure-Requests' header to outgoing navigational
  // requests, as described in
  // https://w3c.github.io/webappsec/specs/upgrade/#feature-detect
  if (resource_request.GetFrameType() != WebURLRequest::kFrameTypeNone) {
    // Early return if the request has already been upgraded.
    if (!resource_request.HttpHeaderField(HTTPNames::Upgrade_Insecure_Requests)
             .IsNull()) {
      return;
    }

    resource_request.SetHTTPHeaderField(HTTPNames::Upgrade_Insecure_Requests,
                                        "1");
  }

  UpgradeInsecureRequest(resource_request, document);
}

void FrameLoader::UpgradeInsecureRequest(ResourceRequest& resource_request,
                                         Document* document) const {
  KURL url = resource_request.Url();

  // If we don't yet have an |m_document| (because we're loading an iframe, for
  // instance), check the FrameLoader's policy.
  WebInsecureRequestPolicy relevant_policy =
      document ? document->GetInsecureRequestPolicy()
               : GetInsecureRequestPolicy();
  SecurityContext::InsecureNavigationsSet* relevant_navigation_set =
      document ? document->InsecureNavigationsToUpgrade()
               : InsecureNavigationsToUpgrade();

  if (url.ProtocolIs("http") && relevant_policy & kUpgradeInsecureRequests) {
    // We always upgrade requests that meet any of the following criteria:
    //
    // 1. Are for subresources (including nested frames).
    // 2. Are form submissions.
    // 3. Whose hosts are contained in the document's InsecureNavigationSet.
    if (resource_request.GetFrameType() == WebURLRequest::kFrameTypeNone ||
        resource_request.GetFrameType() == WebURLRequest::kFrameTypeNested ||
        resource_request.GetRequestContext() ==
            WebURLRequest::kRequestContextForm ||
        (!url.Host().IsNull() &&
         relevant_navigation_set->Contains(url.Host().Impl()->GetHash()))) {
      UseCounter::Count(document,
                        UseCounter::kUpgradeInsecureRequestsUpgradedRequest);
      url.SetProtocol("https");
      if (url.Port() == 80)
        url.SetPort(443);
      resource_request.SetURL(url);
    }
  }
}

void FrameLoader::RecordLatestRequiredCSP() {
  required_csp_ = frame_->Owner() ? frame_->Owner()->Csp() : g_null_atom;
}

std::unique_ptr<TracedValue> FrameLoader::ToTracedValue() const {
  std::unique_ptr<TracedValue> traced_value = TracedValue::Create();
  traced_value->BeginDictionary("frame");
  traced_value->SetString(
      "id_ref", String::Format("0x%" PRIx64,
                               static_cast<uint64_t>(
                                   reinterpret_cast<uintptr_t>(frame_.Get()))));
  traced_value->EndDictionary();
  traced_value->SetBoolean("isLoadingMainFrame", IsLoadingMainFrame());
  traced_value->SetString("stateMachine", state_machine_.ToString());
  traced_value->SetString("provisionalDocumentLoaderURL",
                          provisional_document_loader_
                              ? provisional_document_loader_->Url()
                              : String());
  traced_value->SetString("documentLoaderURL", document_loader_
                                                   ? document_loader_->Url()
                                                   : String());
  return traced_value;
}

inline void FrameLoader::TakeObjectSnapshot() const {
  if (detached_) {
    // We already logged TRACE_EVENT_OBJECT_DELETED_WITH_ID in detach().
    return;
  }
  TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID("loading", "FrameLoader", this,
                                      ToTracedValue());
}

DocumentLoader* FrameLoader::CreateDocumentLoader(
    const ResourceRequest& request,
    const FrameLoadRequest& frame_load_request,
    FrameLoadType load_type,
    NavigationType navigation_type) {
  DocumentLoader* loader = Client()->CreateDocumentLoader(
      frame_, request,
      frame_load_request.GetSubstituteData().IsValid()
          ? frame_load_request.GetSubstituteData()
          : DefaultSubstituteDataForURL(request.Url()),
      frame_load_request.ClientRedirect());

  loader->SetLoadType(load_type);
  loader->SetNavigationType(navigation_type);
  // TODO(japhet): This is needed because the browser process DCHECKs if the
  // first entry we commit in a new frame has replacement set. It's unclear
  // whether the DCHECK is right, investigate removing this special case.
  bool replace_current_item = load_type == kFrameLoadTypeReplaceCurrentItem &&
                              (!Opener() || !request.Url().IsEmpty());
  loader->SetReplacesCurrentHistoryItem(replace_current_item);
  return loader;
}

}  // namespace blink
