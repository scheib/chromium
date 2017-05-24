/*
 * Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
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

#include "core/loader/DocumentLoader.h"

#include <memory>
#include "core/dom/Document.h"
#include "core/dom/DocumentParser.h"
#include "core/dom/WeakIdentifierMap.h"
#include "core/events/Event.h"
#include "core/frame/Deprecation.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/Settings.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/html/parser/HTMLParserIdioms.h"
#include "core/html/parser/TextResourceDecoder.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/inspector/InspectorTraceEvents.h"
#include "core/inspector/MainThreadDebugger.h"
#include "core/loader/FrameFetchContext.h"
#include "core/loader/FrameLoader.h"
#include "core/loader/LinkLoader.h"
#include "core/loader/NetworkHintsInterface.h"
#include "core/loader/ProgressTracker.h"
#include "core/loader/SubresourceFilter.h"
#include "core/loader/appcache/ApplicationCacheHost.h"
#include "core/loader/resource/CSSStyleSheetResource.h"
#include "core/loader/resource/FontResource.h"
#include "core/loader/resource/ImageResource.h"
#include "core/loader/resource/ScriptResource.h"
#include "core/origin_trials/OriginTrialContext.h"
#include "core/page/FrameTree.h"
#include "core/page/Page.h"
#include "core/probe/CoreProbes.h"
#include "core/timing/DOMWindowPerformance.h"
#include "core/timing/Performance.h"
#include "platform/HTTPNames.h"
#include "platform/UserGestureIndicator.h"
#include "platform/feature_policy/FeaturePolicy.h"
#include "platform/loader/fetch/FetchInitiatorTypeNames.h"
#include "platform/loader/fetch/FetchParameters.h"
#include "platform/loader/fetch/FetchUtils.h"
#include "platform/loader/fetch/MemoryCache.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/loader/fetch/ResourceTimingInfo.h"
#include "platform/mhtml/ArchiveResource.h"
#include "platform/network/ContentSecurityPolicyResponseHeaders.h"
#include "platform/network/HTTPParsers.h"
#include "platform/network/mime/MIMETypeRegistry.h"
#include "platform/plugins/PluginData.h"
#include "platform/weborigin/SchemeRegistry.h"
#include "platform/weborigin/SecurityPolicy.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/AutoReset.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/Platform.h"
#include "public/platform/modules/serviceworker/WebServiceWorkerNetworkProvider.h"

namespace blink {

static bool IsArchiveMIMEType(const String& mime_type) {
  return DeprecatedEqualIgnoringCase("multipart/related", mime_type);
}

DocumentLoader::DocumentLoader(LocalFrame* frame,
                               const ResourceRequest& req,
                               const SubstituteData& substitute_data,
                               ClientRedirectPolicy client_redirect_policy)
    : frame_(frame),
      fetcher_(FrameFetchContext::CreateFetcherFromDocumentLoader(this)),
      original_request_(req),
      substitute_data_(substitute_data),
      request_(req),
      load_type_(kFrameLoadTypeStandard),
      is_client_redirect_(client_redirect_policy ==
                          ClientRedirectPolicy::kClientRedirect),
      replaces_current_history_item_(false),
      data_received_(false),
      navigation_type_(kNavigationTypeOther),
      document_load_timing_(*this),
      time_of_last_data_received_(0.0),
      application_cache_host_(ApplicationCacheHost::Create(this)),
      was_blocked_after_csp_(false),
      state_(kNotStarted),
      in_data_received_(false),
      data_buffer_(SharedBuffer::Create()) {
  DCHECK(frame_);

  // The document URL needs to be added to the head of the list as that is
  // where the redirects originated.
  if (is_client_redirect_)
    AppendRedirect(frame_->GetDocument()->Url());
}

FrameLoader& DocumentLoader::GetFrameLoader() const {
  DCHECK(frame_);
  return frame_->Loader();
}

LocalFrameClient& DocumentLoader::GetLocalFrameClient() const {
  DCHECK(frame_);
  LocalFrameClient* client = frame_->Client();
  // LocalFrame clears its |m_client| only after detaching all DocumentLoaders
  // (i.e. calls detachFromFrame() which clears |frame_|) owned by the
  // LocalFrame's FrameLoader. So, if |frame_| is non nullptr, |client| is
  // also non nullptr.
  DCHECK(client);
  return *client;
}

DocumentLoader::~DocumentLoader() {
  DCHECK(!frame_);
  DCHECK(!main_resource_);
  DCHECK(!application_cache_host_);
  DCHECK_EQ(state_, kSentDidFinishLoad);
}

DEFINE_TRACE(DocumentLoader) {
  visitor->Trace(frame_);
  visitor->Trace(fetcher_);
  visitor->Trace(main_resource_);
  visitor->Trace(history_item_);
  visitor->Trace(writer_);
  visitor->Trace(subresource_filter_);
  visitor->Trace(document_load_timing_);
  visitor->Trace(application_cache_host_);
  visitor->Trace(content_security_policy_);
  RawResourceClient::Trace(visitor);
}

unsigned long DocumentLoader::MainResourceIdentifier() const {
  return main_resource_ ? main_resource_->Identifier() : 0;
}

ResourceTimingInfo* DocumentLoader::GetNavigationTimingInfo() const {
  DCHECK(Fetcher());
  return Fetcher()->GetNavigationTimingInfo();
}

const ResourceRequest& DocumentLoader::OriginalRequest() const {
  return original_request_;
}

const ResourceRequest& DocumentLoader::GetRequest() const {
  return request_;
}

void DocumentLoader::SetSubresourceFilter(
    SubresourceFilter* subresource_filter) {
  subresource_filter_ = subresource_filter;
}

const KURL& DocumentLoader::Url() const {
  return request_.Url();
}

Resource* DocumentLoader::StartPreload(Resource::Type type,
                                       FetchParameters& params) {
  Resource* resource = nullptr;
  switch (type) {
    case Resource::kImage:
      if (frame_)
        frame_->MaybeAllowImagePlaceholder(params);
      resource = ImageResource::Fetch(params, Fetcher());
      break;
    case Resource::kScript:
      resource = ScriptResource::Fetch(params, Fetcher());
      break;
    case Resource::kCSSStyleSheet:
      resource = CSSStyleSheetResource::Fetch(params, Fetcher());
      break;
    case Resource::kFont:
      resource = FontResource::Fetch(params, Fetcher());
      break;
    case Resource::kMedia:
      resource = RawResource::FetchMedia(params, Fetcher());
      break;
    case Resource::kTextTrack:
      resource = RawResource::FetchTextTrack(params, Fetcher());
      break;
    case Resource::kImportResource:
      resource = RawResource::FetchImport(params, Fetcher());
      break;
    case Resource::kRaw:
      resource = RawResource::Fetch(params, Fetcher());
      break;
    default:
      NOTREACHED();
  }

  return resource;
}

void DocumentLoader::SetServiceWorkerNetworkProvider(
    std::unique_ptr<WebServiceWorkerNetworkProvider> provider) {
  service_worker_network_provider_ = std::move(provider);
}

void DocumentLoader::SetSourceLocation(
    std::unique_ptr<SourceLocation> source_location) {
  source_location_ = std::move(source_location);
}

std::unique_ptr<SourceLocation> DocumentLoader::CopySourceLocation() const {
  return source_location_ ? source_location_->Clone() : nullptr;
}

void DocumentLoader::DispatchLinkHeaderPreloads(
    ViewportDescriptionWrapper* viewport,
    LinkLoader::MediaPreloadPolicy media_policy) {
  DCHECK_GE(state_, kCommitted);
  LinkLoader::LoadLinksFromHeader(
      GetResponse().HttpHeaderField(HTTPNames::Link), GetResponse().Url(),
      *frame_, frame_->GetDocument(), NetworkHintsInterfaceImpl(),
      LinkLoader::kOnlyLoadResources, media_policy, viewport);
}

void DocumentLoader::DidChangePerformanceTiming() {
  if (frame_ && state_ >= kCommitted) {
    GetLocalFrameClient().DidChangePerformanceTiming();
  }
}

void DocumentLoader::DidObserveLoadingBehavior(
    WebLoadingBehaviorFlag behavior) {
  if (frame_) {
    DCHECK_GE(state_, kCommitted);
    GetLocalFrameClient().DidObserveLoadingBehavior(behavior);
  }
}

void DocumentLoader::MarkAsCommitted() {
  DCHECK_LT(state_, kCommitted);
  state_ = kCommitted;
}

static HistoryCommitType LoadTypeToCommitType(FrameLoadType type) {
  switch (type) {
    case kFrameLoadTypeStandard:
      return kStandardCommit;
    case kFrameLoadTypeInitialInChildFrame:
    case kFrameLoadTypeInitialHistoryLoad:
      return kInitialCommitInChildFrame;
    case kFrameLoadTypeBackForward:
      return kBackForwardCommit;
    default:
      break;
  }
  return kHistoryInertCommit;
}

void DocumentLoader::UpdateForSameDocumentNavigation(
    const KURL& new_url,
    SameDocumentNavigationSource same_document_navigation_source,
    PassRefPtr<SerializedScriptValue> data,
    HistoryScrollRestorationType scroll_restoration_type,
    FrameLoadType type,
    Document* initiating_document) {
  if (initiating_document && !initiating_document->CanCreateHistoryEntry())
    type = kFrameLoadTypeReplaceCurrentItem;

  KURL old_url = request_.Url();
  original_request_.SetURL(new_url);
  request_.SetURL(new_url);
  SetReplacesCurrentHistoryItem(type != kFrameLoadTypeStandard);
  if (same_document_navigation_source == kSameDocumentNavigationHistoryApi) {
    request_.SetHTTPMethod(HTTPNames::GET);
    request_.SetHTTPBody(nullptr);
  }
  ClearRedirectChain();
  if (is_client_redirect_)
    AppendRedirect(old_url);
  AppendRedirect(new_url);

  SetHistoryItemStateForCommit(
      history_item_.Get(), type,
      same_document_navigation_source == kSameDocumentNavigationHistoryApi
          ? HistoryNavigationType::kHistoryApi
          : HistoryNavigationType::kFragment);
  history_item_->SetDocumentState(frame_->GetDocument()->FormElementsState());
  if (same_document_navigation_source == kSameDocumentNavigationHistoryApi) {
    history_item_->SetStateObject(std::move(data));
    history_item_->SetScrollRestorationType(scroll_restoration_type);
  }
  GetLocalFrameClient().DispatchDidNavigateWithinPage(
      history_item_.Get(), LoadTypeToCommitType(type), initiating_document);
}

const KURL& DocumentLoader::UrlForHistory() const {
  return UnreachableURL().IsEmpty() ? Url() : UnreachableURL();
}

void DocumentLoader::SetHistoryItemStateForCommit(
    HistoryItem* old_item,
    FrameLoadType load_type,
    HistoryNavigationType navigation_type) {
  if (!history_item_ || !IsBackForwardLoadType(load_type))
    history_item_ = HistoryItem::Create();

  history_item_->SetURL(UrlForHistory());
  history_item_->SetReferrer(SecurityPolicy::GenerateReferrer(
      request_.GetReferrerPolicy(), history_item_->Url(),
      request_.HttpReferrer()));
  history_item_->SetFormInfoFromRequest(request_);

  // Don't propagate state from the old item to the new item if there isn't an
  // old item (obviously), or if this is a back/forward navigation, since we
  // explicitly want to restore the state we just committed.
  if (!old_item || IsBackForwardLoadType(load_type))
    return;
  // Don't propagate state from the old item if this is a different-document
  // navigation, unless the before and after pages are logically related. This
  // means they have the same url (ignoring fragment) and the new item was
  // loaded via reload or client redirect.
  HistoryCommitType history_commit_type = LoadTypeToCommitType(load_type);
  if (navigation_type == HistoryNavigationType::kDifferentDocument &&
      (history_commit_type != kHistoryInertCommit ||
       !EqualIgnoringFragmentIdentifier(old_item->Url(), history_item_->Url())))
    return;
  history_item_->SetDocumentSequenceNumber(old_item->DocumentSequenceNumber());
  history_item_->SetScrollOffset(old_item->GetScrollOffset());
  history_item_->SetDidSaveScrollOrScaleState(
      old_item->DidSaveScrollOrScaleState());
  history_item_->SetVisualViewportScrollOffset(
      old_item->VisualViewportScrollOffset());
  history_item_->SetPageScaleFactor(old_item->PageScaleFactor());
  history_item_->SetScrollRestorationType(old_item->ScrollRestorationType());

  // The item sequence number determines whether items are "the same", such
  // back/forward navigation between items with the same item sequence number is
  // a no-op. Only treat this as identical if the navigation did not create a
  // back/forward entry and the url is identical or it was loaded via
  // history.replaceState().
  if (history_commit_type == kHistoryInertCommit &&
      (navigation_type == HistoryNavigationType::kHistoryApi ||
       old_item->Url() == history_item_->Url())) {
    history_item_->SetStateObject(old_item->StateObject());
    history_item_->SetItemSequenceNumber(old_item->ItemSequenceNumber());
  }
}

void DocumentLoader::NotifyFinished(Resource* resource) {
  DCHECK_EQ(main_resource_, resource);
  DCHECK(main_resource_);

  if (!main_resource_->ErrorOccurred() && !main_resource_->WasCanceled()) {
    FinishedLoading(main_resource_->LoadFinishTime());
    return;
  }

  if (application_cache_host_)
    application_cache_host_->FailedLoadingMainResource();

  if (main_resource_->GetResourceError().WasBlockedByResponse()) {
    probe::CanceledAfterReceivedResourceResponse(
        frame_, this, MainResourceIdentifier(), resource->GetResponse(),
        main_resource_.Get());
  }

  LoadFailed(main_resource_->GetResourceError());
  ClearMainResourceHandle();
}

void DocumentLoader::LoadFailed(const ResourceError& error) {
  if (!error.IsCancellation() && frame_->Owner()) {
    // FIXME: For now, fallback content doesn't work cross process.
    if (frame_->Owner()->IsLocal())
      frame_->DeprecatedLocalOwner()->RenderFallbackContent();
  }

  HistoryCommitType history_commit_type = LoadTypeToCommitType(load_type_);
  switch (state_) {
    case kNotStarted:
      probe::frameClearedScheduledClientNavigation(frame_);
    // Fall-through
    case kProvisional:
      state_ = kSentDidFinishLoad;
      GetLocalFrameClient().DispatchDidFailProvisionalLoad(error,
                                                           history_commit_type);
      if (frame_)
        GetFrameLoader().DetachProvisionalDocumentLoader(this);
      break;
    case kCommitted:
      if (frame_->GetDocument()->Parser())
        frame_->GetDocument()->Parser()->StopParsing();
      state_ = kSentDidFinishLoad;
      GetLocalFrameClient().DispatchDidFailLoad(error, history_commit_type);
      if (frame_)
        frame_->GetDocument()->CheckCompleted();
      break;
    case kSentDidFinishLoad:
      // TODO(japhet): Why do we need to call DidFinishNavigation() again?
      GetFrameLoader().DidFinishNavigation();
      break;
  }
  DCHECK_EQ(kSentDidFinishLoad, state_);
}

void DocumentLoader::FinishedLoading(double finish_time) {
  DCHECK(frame_->Loader().StateMachine()->CreatingInitialEmptyDocument() ||
         !frame_->GetPage()->Suspended() ||
         MainThreadDebugger::Instance()->IsPaused());

  double response_end_time = finish_time;
  if (!response_end_time)
    response_end_time = time_of_last_data_received_;
  if (!response_end_time)
    response_end_time = MonotonicallyIncreasingTime();
  GetTiming().SetResponseEnd(response_end_time);
  if (!MaybeCreateArchive()) {
    // If this is an empty document, it will not have actually been created yet.
    // Commit dummy data so that DocumentWriter::begin() gets called and creates
    // the Document.
    if (!writer_)
      CommitData(0, 0);
  }

  if (!frame_)
    return;

  application_cache_host_->FinishedLoadingMainResource();
  EndWriting();
  ClearMainResourceHandle();
}

bool DocumentLoader::RedirectReceived(
    Resource* resource,
    const ResourceRequest& request,
    const ResourceResponse& redirect_response) {
  DCHECK(frame_);
  DCHECK_EQ(resource, main_resource_);
  DCHECK(!redirect_response.IsNull());
  request_ = request;

  // If the redirecting url is not allowed to display content from the target
  // origin, then block the redirect.
  const KURL& request_url = request_.Url();
  RefPtr<SecurityOrigin> redirecting_origin =
      SecurityOrigin::Create(redirect_response.Url());
  if (!redirecting_origin->CanDisplay(request_url)) {
    FrameLoader::ReportLocalLoadFailed(frame_, request_url.GetString());
    fetcher_->StopFetching();
    return false;
  }
  if (GetFrameLoader().ShouldContinueForRedirectNavigationPolicy(
          request_, SubstituteData(), this, kCheckContentSecurityPolicy,
          navigation_type_, kNavigationPolicyCurrentTab, load_type_,
          IsClientRedirect(), nullptr) != kNavigationPolicyCurrentTab) {
    fetcher_->StopFetching();
    return false;
  }

  DCHECK(GetTiming().FetchStart());
  AppendRedirect(request_url);
  GetTiming().AddRedirect(redirect_response.Url(), request_url);

  // If a redirection happens during a back/forward navigation, don't restore
  // any state from the old HistoryItem. There is a provisional history item for
  // back/forward navigation only. In the other case, clearing it is a no-op.
  history_item_.Clear();

  GetLocalFrameClient().DispatchDidReceiveServerRedirectForProvisionalLoad();

  return true;
}

static bool CanShowMIMEType(const String& mime_type, LocalFrame* frame) {
  if (MIMETypeRegistry::IsSupportedMIMEType(mime_type))
    return true;
  PluginData* plugin_data = frame->GetPluginData();
  return !mime_type.IsEmpty() && plugin_data &&
         plugin_data->SupportsMimeType(mime_type);
}

bool DocumentLoader::ShouldContinueForResponse() const {
  if (substitute_data_.IsValid())
    return true;

  int status_code = response_.HttpStatusCode();
  if (status_code == 204 || status_code == 205) {
    // The server does not want us to replace the page contents.
    return false;
  }

  if (IsContentDispositionAttachment(
          response_.HttpHeaderField(HTTPNames::Content_Disposition))) {
    // The server wants us to download instead of replacing the page contents.
    // Downloading is handled by the embedder, but we still get the initial
    // response so that we can ignore it and clean up properly.
    return false;
  }

  if (!CanShowMIMEType(response_.MimeType(), frame_))
    return false;
  return true;
}

void DocumentLoader::CancelLoadAfterCSPDenied(
    const ResourceResponse& response) {
  probe::CanceledAfterReceivedResourceResponse(
      frame_, this, MainResourceIdentifier(), response, main_resource_.Get());

  SetWasBlockedAfterCSP();

  // Pretend that this was an empty HTTP 200 response.  Don't reuse the original
  // URL for the empty page (https://crbug.com/622385).
  //
  // TODO(mkwst):  Remove this once XFO moves to the browser.
  // https://crbug.com/555418.
  ClearMainResourceHandle();
  content_security_policy_.Clear();
  KURL blocked_url = SecurityOrigin::UrlWithUniqueSecurityOrigin();
  original_request_.SetURL(blocked_url);
  request_.SetURL(blocked_url);
  redirect_chain_.pop_back();
  AppendRedirect(blocked_url);
  response_ = ResourceResponse(blocked_url, "text/html", 0, g_null_atom);
  FinishedLoading(MonotonicallyIncreasingTime());

  return;
}

void DocumentLoader::ResponseReceived(
    Resource* resource,
    const ResourceResponse& response,
    std::unique_ptr<WebDataConsumerHandle> handle) {
  DCHECK_EQ(main_resource_, resource);
  DCHECK(!handle);
  DCHECK(frame_);

  application_cache_host_->DidReceiveResponseForMainResource(response);

  // The memory cache doesn't understand the application cache or its caching
  // rules. So if a main resource is served from the application cache, ensure
  // we don't save the result for future use. All responses loaded from appcache
  // will have a non-zero appCacheID().
  if (response.AppCacheID())
    GetMemoryCache()->Remove(main_resource_.Get());

  content_security_policy_ = ContentSecurityPolicy::Create();
  content_security_policy_->SetOverrideURLForSelf(response.Url());
  content_security_policy_->DidReceiveHeaders(
      ContentSecurityPolicyResponseHeaders(response));
  if (!content_security_policy_->AllowAncestors(frame_, response.Url())) {
    CancelLoadAfterCSPDenied(response);
    return;
  }

  if (RuntimeEnabledFeatures::embedderCSPEnforcementEnabled() &&
      !GetFrameLoader().RequiredCSP().IsEmpty()) {
    SecurityOrigin* parent_security_origin =
        frame_->Tree().Parent()->GetSecurityContext()->GetSecurityOrigin();
    if (ContentSecurityPolicy::ShouldEnforceEmbeddersPolicy(
            response, parent_security_origin)) {
      content_security_policy_->AddPolicyFromHeaderValue(
          GetFrameLoader().RequiredCSP(),
          kContentSecurityPolicyHeaderTypeEnforce,
          kContentSecurityPolicyHeaderSourceHTTP);
    } else {
      ContentSecurityPolicy* required_csp = ContentSecurityPolicy::Create();
      required_csp->AddPolicyFromHeaderValue(
          GetFrameLoader().RequiredCSP(),
          kContentSecurityPolicyHeaderTypeEnforce,
          kContentSecurityPolicyHeaderSourceHTTP);
      if (!required_csp->Subsumes(*content_security_policy_)) {
        String message = "Refused to display '" +
                         response.Url().ElidedString() +
                         "' because it has not opted-into the following policy "
                         "required by its embedder: '" +
                         GetFrameLoader().RequiredCSP() + "'.";
        ConsoleMessage* console_message = ConsoleMessage::CreateForRequest(
            kSecurityMessageSource, kErrorMessageLevel, message, response.Url(),
            MainResourceIdentifier());
        frame_->GetDocument()->AddConsoleMessage(console_message);
        CancelLoadAfterCSPDenied(response);
        return;
      }
    }
  }

  DCHECK(!frame_->GetPage()->Suspended());

  if (response.DidServiceWorkerNavigationPreload())
    UseCounter::Count(frame_, UseCounter::kServiceWorkerNavigationPreload);
  response_ = response;

  if (IsArchiveMIMEType(response_.MimeType()) &&
      main_resource_->GetDataBufferingPolicy() != kBufferData)
    main_resource_->SetDataBufferingPolicy(kBufferData);

  if (!ShouldContinueForResponse()) {
    probe::ContinueWithPolicyIgnore(frame_, this, main_resource_->Identifier(),
                                    response_, main_resource_.Get());
    fetcher_->StopFetching();
    return;
  }

  if (frame_->Owner() && response_.IsHTTP() &&
      !FetchUtils::IsOkStatus(response_.HttpStatusCode()))
    frame_->Owner()->RenderFallbackContent();
}

void DocumentLoader::EnsureWriter(const AtomicString& mime_type,
                                  const KURL& overriding_url) {
  if (writer_)
    return;

  // Set history state before commitProvisionalLoad() so that we still have
  // access to the previous committed DocumentLoader's HistoryItem, in case we
  // need to copy state from it.
  if (!GetFrameLoader().StateMachine()->CreatingInitialEmptyDocument()) {
    SetHistoryItemStateForCommit(
        GetFrameLoader().GetDocumentLoader()->GetHistoryItem(), load_type_,
        HistoryNavigationType::kDifferentDocument);
  }

  DCHECK_EQ(state_, kProvisional);
  GetFrameLoader().CommitProvisionalLoad();
  if (!frame_)
    return;

  const AtomicString& encoding = GetResponse().TextEncodingName();

  // Prepare a DocumentInit before clearing the frame, because it may need to
  // inherit an aliased security context.
  Document* owner = nullptr;
  // TODO(dcheng): This differs from the behavior of both IE and Firefox: the
  // origin is inherited from the document that loaded the URL.
  if (Document::ShouldInheritSecurityOriginFromOwner(Url())) {
    Frame* owner_frame = frame_->Tree().Parent();
    if (!owner_frame)
      owner_frame = frame_->Loader().Opener();
    if (owner_frame && owner_frame->IsLocalFrame())
      owner = ToLocalFrame(owner_frame)->GetDocument();
  }
  DocumentInit init(owner, Url(), frame_);
  init.WithNewRegistrationContext();
  frame_->Loader().Clear();
  DCHECK(frame_->GetPage());

  ParserSynchronizationPolicy parsing_policy = kAllowAsynchronousParsing;
  if ((substitute_data_.IsValid() && substitute_data_.ForceSynchronousLoad()) ||
      !Document::ThreadedParsingEnabledForTesting())
    parsing_policy = kForceSynchronousParsing;

  InstallNewDocument(init, mime_type, encoding,
                     InstallNewDocumentReason::kNavigation, parsing_policy,
                     overriding_url);
  writer_->SetDocumentWasLoadedAsPartOfNavigation();
  frame_->GetDocument()->MaybeHandleHttpRefresh(
      response_.HttpHeaderField(HTTPNames::Refresh),
      Document::kHttpRefreshFromHeader);
}

void DocumentLoader::CommitData(const char* bytes, size_t length) {
  EnsureWriter(response_.MimeType());
  DCHECK_GE(state_, kCommitted);

  // This can happen if document.close() is called by an event handler while
  // there's still pending incoming data.
  if (!frame_ || !frame_->GetDocument()->Parsing())
    return;

  if (length)
    data_received_ = true;

  writer_->AddData(bytes, length);
}

void DocumentLoader::DataReceived(Resource* resource,
                                  const char* data,
                                  size_t length) {
  DCHECK(data);
  DCHECK(length);
  DCHECK_EQ(resource, main_resource_);
  DCHECK(!response_.IsNull());
  DCHECK(!frame_->GetPage()->Suspended());

  if (in_data_received_) {
    // If this function is reentered, defer processing of the additional data to
    // the top-level invocation. Reentrant calls can occur because of web
    // platform (mis-)features that require running a nested run loop:
    // - alert(), confirm(), prompt()
    // - Detach of plugin elements.
    // - Synchronous XMLHTTPRequest
    data_buffer_->Append(data, length);
    return;
  }

  AutoReset<bool> reentrancy_protector(&in_data_received_, true);
  ProcessData(data, length);

  // Process data received in reentrant invocations. Note that the invocations
  // of processData() may queue more data in reentrant invocations, so iterate
  // until it's empty.
  const char* segment;
  size_t pos = 0;
  while (size_t length = data_buffer_->GetSomeData(segment, pos)) {
    ProcessData(segment, length);
    pos += length;
  }
  // All data has been consumed, so flush the buffer.
  data_buffer_->Clear();
}

void DocumentLoader::ProcessData(const char* data, size_t length) {
  application_cache_host_->MainResourceDataReceived(data, length);
  time_of_last_data_received_ = MonotonicallyIncreasingTime();

  if (IsArchiveMIMEType(GetResponse().MimeType()))
    return;
  CommitData(data, length);

  // If we are sending data to MediaDocument, we should stop here and cancel the
  // request.
  if (frame_ && frame_->GetDocument()->IsMediaDocument())
    fetcher_->StopFetching();
}

void DocumentLoader::ClearRedirectChain() {
  redirect_chain_.clear();
}

void DocumentLoader::AppendRedirect(const KURL& url) {
  redirect_chain_.push_back(url);
}

void DocumentLoader::DetachFromFrame() {
  DCHECK(frame_);

  // It never makes sense to have a document loader that is detached from its
  // frame have any loads active, so go ahead and kill all the loads.
  fetcher_->StopFetching();

  if (frame_ && !SentDidFinishLoad())
    LoadFailed(ResourceError::CancelledError(Url()));

  // If that load cancellation triggered another detach, leave.
  // (fast/frames/detach-frame-nested-no-crash.html is an example of this.)
  if (!frame_)
    return;

  fetcher_->ClearContext();
  application_cache_host_->DetachFromDocumentLoader();
  application_cache_host_.Clear();
  service_worker_network_provider_ = nullptr;
  WeakIdentifierMap<DocumentLoader>::NotifyObjectDestroyed(this);
  ClearMainResourceHandle();
  frame_ = nullptr;
}

void DocumentLoader::ClearMainResourceHandle() {
  if (!main_resource_)
    return;
  main_resource_->RemoveClient(this);
  main_resource_ = nullptr;
}

bool DocumentLoader::MaybeCreateArchive() {
  // Give the archive machinery a crack at this document. If the MIME type is
  // not an archive type, it will return 0.
  if (!IsArchiveMIMEType(response_.MimeType()))
    return false;

  DCHECK(main_resource_);
  ArchiveResource* main_resource =
      fetcher_->CreateArchive(main_resource_.Get());
  if (!main_resource)
    return false;
  // The origin is the MHTML file, we need to set the base URL to the document
  // encoded in the MHTML so relative URLs are resolved properly.
  EnsureWriter(main_resource->MimeType(), main_resource->Url());
  if (!frame_)
    return false;

  // The MHTML page is loaded in full sandboxing mode with the only
  // exception to open new top-level windows. Since the MHTML page stays in a
  // unquie origin with script execution disabled, the risk to navigate to
  // 'blob:'' and 'filesystem:'' URLs that allow code execution in the page's
  // "real" origin is mitigated.
  frame_->GetDocument()->EnforceSandboxFlags(
      kSandboxAll &
      ~(kSandboxPopups | kSandboxPropagatesToAuxiliaryBrowsingContexts));

  CommitData(main_resource->Data()->Data(), main_resource->Data()->size());
  return true;
}

const AtomicString& DocumentLoader::ResponseMIMEType() const {
  return response_.MimeType();
}

const KURL& DocumentLoader::UnreachableURL() const {
  return substitute_data_.FailingURL();
}

bool DocumentLoader::MaybeLoadEmpty() {
  bool should_load_empty = !substitute_data_.IsValid() &&
                           (request_.Url().IsEmpty() ||
                            SchemeRegistry::ShouldLoadURLSchemeAsEmptyDocument(
                                request_.Url().Protocol()));
  if (!should_load_empty)
    return false;

  if (request_.Url().IsEmpty() &&
      !GetFrameLoader().StateMachine()->CreatingInitialEmptyDocument())
    request_.SetURL(BlankURL());
  response_ = ResourceResponse(request_.Url(), "text/html", 0, g_null_atom);
  FinishedLoading(MonotonicallyIncreasingTime());
  return true;
}

void DocumentLoader::StartLoadingMainResource() {
  GetTiming().MarkNavigationStart();
  DCHECK(!main_resource_);
  DCHECK_EQ(state_, kNotStarted);
  state_ = kProvisional;

  if (MaybeLoadEmpty())
    return;

  DCHECK(GetTiming().NavigationStart());

  // PlzNavigate:
  // The fetch has already started in the browser. Don't mark it again.
  if (!frame_->GetSettings()->GetBrowserSideNavigationEnabled()) {
    DCHECK(!GetTiming().FetchStart());
    GetTiming().MarkFetchStart();
  }

  DEFINE_STATIC_LOCAL(
      ResourceLoaderOptions, main_resource_load_options,
      (kDoNotBufferData, kAllowStoredCredentials, kClientRequestedCredentials,
       kCheckContentSecurityPolicy, kDocumentContext));
  FetchParameters fetch_params(request_, FetchInitiatorTypeNames::document,
                               main_resource_load_options);
  main_resource_ =
      RawResource::FetchMainResource(fetch_params, Fetcher(), substitute_data_);

  // PlzNavigate:
  // The final access checks are still performed here, potentially rejecting
  // the "provisional" load, but the browser side already expects the renderer
  // to be able to unconditionally commit.
  if (!main_resource_ ||
      (frame_->GetSettings()->GetBrowserSideNavigationEnabled() &&
       main_resource_->ErrorOccurred())) {
    request_ = ResourceRequest(BlankURL());
    MaybeLoadEmpty();
    return;
  }
  // A bunch of headers are set when the underlying resource load begins, and
  // m_request needs to include those. Even when using a cached resource, we may
  // make some modification to the request, e.g. adding the referer header.
  request_ = main_resource_->IsLoading() ? main_resource_->GetResourceRequest()
                                         : fetch_params.GetResourceRequest();
  main_resource_->AddClient(this);
}

void DocumentLoader::EndWriting() {
  writer_->end();
  writer_.Clear();
}

void DocumentLoader::DidInstallNewDocument(Document* document) {
  document->SetReadyState(Document::kLoading);
  document->InitContentSecurityPolicy(content_security_policy_.Release());

  if (history_item_ && IsBackForwardLoadType(load_type_))
    document->SetStateForNewFormElements(history_item_->GetDocumentState());

  String suborigin_header = response_.HttpHeaderField(HTTPNames::Suborigin);
  if (!suborigin_header.IsNull()) {
    Vector<String> messages;
    Suborigin suborigin;
    if (ParseSuboriginHeader(suborigin_header, &suborigin, messages))
      document->EnforceSuborigin(suborigin);

    for (auto& message : messages) {
      document->AddConsoleMessage(
          ConsoleMessage::Create(kSecurityMessageSource, kErrorMessageLevel,
                                 "Error with Suborigin header: " + message));
    }
  }

  document->GetClientHintsPreferences().UpdateFrom(client_hints_preferences_);

  // TODO(japhet): There's no reason to wait until commit to set these bits.
  Settings* settings = document->GetSettings();
  fetcher_->SetImagesEnabled(settings->GetImagesEnabled());
  fetcher_->SetAutoLoadImages(settings->GetLoadsImagesAutomatically());

  const AtomicString& dns_prefetch_control =
      response_.HttpHeaderField(HTTPNames::X_DNS_Prefetch_Control);
  if (!dns_prefetch_control.IsEmpty())
    document->ParseDNSPrefetchControlHeader(dns_prefetch_control);

  String header_content_language =
      response_.HttpHeaderField(HTTPNames::Content_Language);
  if (!header_content_language.IsEmpty()) {
    size_t comma_index = header_content_language.find(',');
    // kNotFound == -1 == don't truncate
    header_content_language.Truncate(comma_index);
    header_content_language =
        header_content_language.StripWhiteSpace(IsHTMLSpace<UChar>);
    if (!header_content_language.IsEmpty())
      document->SetContentLanguage(AtomicString(header_content_language));
  }

  OriginTrialContext::AddTokensFromHeader(
      document, response_.HttpHeaderField(HTTPNames::Origin_Trial));
  String referrer_policy_header =
      response_.HttpHeaderField(HTTPNames::Referrer_Policy);
  if (!referrer_policy_header.IsNull()) {
    UseCounter::Count(*document, UseCounter::kReferrerPolicyHeader);
    document->ParseAndSetReferrerPolicy(referrer_policy_header);
  }

  if (RuntimeEnabledFeatures::serverTimingEnabled() &&
      frame_->GetDocument()->domWindow()) {
    DOMWindowPerformance::performance(*(frame_->GetDocument()->domWindow()))
        ->AddServerTiming(response_,
                          PerformanceBase::ShouldAddToBuffer::Always);
  }

  GetLocalFrameClient().DidCreateNewDocument();
}

void DocumentLoader::DidCommitNavigation() {
  if (GetFrameLoader().StateMachine()->CreatingInitialEmptyDocument())
    return;

  if (!frame_->Loader().StateMachine()->CommittedMultipleRealLoads() &&
      load_type_ == kFrameLoadTypeStandard) {
    frame_->Loader().StateMachine()->AdvanceTo(
        FrameLoaderStateMachine::kCommittedMultipleRealLoads);
  }

  GetLocalFrameClient().DispatchDidCommitLoad(history_item_.Get(),
                                              LoadTypeToCommitType(load_type_));

  // When the embedder gets notified (above) that the new navigation has
  // committed, the embedder will drop the old Content Security Policy and
  // therefore now is a good time to report to the embedder the Content
  // Security Policies that have accumulated so far for the new navigation.
  frame_->GetSecurityContext()
      ->GetContentSecurityPolicy()
      ->ReportAccumulatedHeaders(&GetLocalFrameClient());

  // didObserveLoadingBehavior() must be called after dispatchDidCommitLoad() is
  // called for the metrics tracking logic to handle it properly.
  if (service_worker_network_provider_ &&
      service_worker_network_provider_->IsControlledByServiceWorker()) {
    GetLocalFrameClient().DidObserveLoadingBehavior(
        kWebLoadingBehaviorServiceWorkerControlled);
  }

  // Links with media values need more information (like viewport information).
  // This happens after the first chunk is parsed in HTMLDocumentParser.
  DispatchLinkHeaderPreloads(nullptr, LinkLoader::kOnlyLoadNonMedia);

  TRACE_EVENT1("devtools.timeline", "CommitLoad", "data",
               InspectorCommitLoadEvent::Data(frame_));
  probe::didCommitLoad(frame_, this);
  frame_->GetPage()->DidCommitLoad(frame_);
}

void SetFeaturePolicy(Document* document, const String& feature_policy_header) {
  if (!RuntimeEnabledFeatures::featurePolicyEnabled())
    return;
  LocalFrame* frame = document->GetFrame();
  WebFeaturePolicy* parent_feature_policy =
      frame->IsMainFrame()
          ? nullptr
          : frame->Tree().Parent()->GetSecurityContext()->GetFeaturePolicy();
  Vector<String> messages;
  const WebParsedFeaturePolicy& parsed_header = ParseFeaturePolicy(
      feature_policy_header, frame->GetSecurityContext()->GetSecurityOrigin(),
      &messages);
  WebParsedFeaturePolicy container_policy;
  if (frame->Owner())
    container_policy = frame->Owner()->ContainerPolicy();
  frame->GetSecurityContext()->InitializeFeaturePolicy(
      parsed_header, container_policy, parent_feature_policy);

  for (auto& message : messages) {
    document->AddConsoleMessage(
        ConsoleMessage::Create(kOtherMessageSource, kErrorMessageLevel,
                               "Error with Feature-Policy header: " + message));
  }
  if (!parsed_header.empty())
    frame->Client()->DidSetFeaturePolicyHeader(parsed_header);
}

// static
bool DocumentLoader::ShouldClearWindowName(
    const LocalFrame& frame,
    SecurityOrigin* previous_security_origin,
    const Document& new_document) {
  if (!previous_security_origin)
    return false;
  if (!frame.IsMainFrame())
    return false;
  if (frame.Loader().Opener())
    return false;

  return !new_document.GetSecurityOrigin()->IsSameSchemeHostPort(
      previous_security_origin);
}

void DocumentLoader::InstallNewDocument(
    const DocumentInit& init,
    const AtomicString& mime_type,
    const AtomicString& encoding,
    InstallNewDocumentReason reason,
    ParserSynchronizationPolicy parsing_policy,
    const KURL& overriding_url) {
  DCHECK_EQ(init.GetFrame(), frame_);
  DCHECK(!frame_->GetDocument() || !frame_->GetDocument()->IsActive());
  DCHECK_EQ(frame_->Tree().ChildCount(), 0u);

  SecurityOrigin* previous_security_origin = nullptr;
  if (frame_->GetDocument())
    previous_security_origin = frame_->GetDocument()->GetSecurityOrigin();

  if (!init.ShouldReuseDefaultView())
    frame_->SetDOMWindow(LocalDOMWindow::Create(*frame_));

  Document* document = frame_->DomWindow()->InstallNewDocument(mime_type, init);

  if (ShouldClearWindowName(*frame_, previous_security_origin, *document)) {
    // TODO(andypaicu): experimentalSetNullName will just record the fact
    // that the name would be nulled and if the name is accessed after we will
    // fire a UseCounter. If we decide to move forward with this change, we'd
    // actually clean the name here.
    // frame_->tree().setName(nullAtom);
    frame_->Tree().ExperimentalSetNulledName();
  }

  frame_->GetPage()->GetChromeClient().InstallSupplements(*frame_);
  if (!overriding_url.IsEmpty())
    document->SetBaseURLOverride(overriding_url);
  DidInstallNewDocument(document);

  // This must be called before DocumentWriter is created, otherwise HTML parser
  // will use stale values from HTMLParserOption.
  if (reason == InstallNewDocumentReason::kNavigation)
    DidCommitNavigation();

  writer_ =
      DocumentWriter::Create(document, parsing_policy, mime_type, encoding);

  // FeaturePolicy is reset in the browser process on commit, so this needs to
  // be initialized and replicated to the browser process after commit messages
  // are sent in didCommitNavigation().
  SetFeaturePolicy(document,
                   response_.HttpHeaderField(HTTPNames::Feature_Policy));

  GetFrameLoader().DispatchDidClearDocumentOfWindowObject();
}

const AtomicString& DocumentLoader::MimeType() const {
  if (writer_)
    return writer_->MimeType();
  return response_.MimeType();
}

// This is only called by
// FrameLoader::replaceDocumentWhileExecutingJavaScriptURL()
void DocumentLoader::ReplaceDocumentWhileExecutingJavaScriptURL(
    const DocumentInit& init,
    const String& source) {
  InstallNewDocument(init, MimeType(),
                     writer_ ? writer_->Encoding() : g_empty_atom,
                     InstallNewDocumentReason::kJavascriptURL,
                     kForceSynchronousParsing, KURL());
  if (!source.IsNull())
    writer_->AppendReplacingData(source);
  EndWriting();
}

DEFINE_WEAK_IDENTIFIER_MAP(DocumentLoader);

}  // namespace blink
