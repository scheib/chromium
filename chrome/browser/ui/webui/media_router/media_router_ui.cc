// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/media_router/media_router_ui.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

#include "base/guid.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/media/router/create_presentation_connection_request.h"
#include "chrome/browser/media/router/issues_observer.h"
#include "chrome/browser/media/router/media_router.h"
#include "chrome/browser/media/router/media_router_factory.h"
#include "chrome/browser/media/router/media_router_metrics.h"
#include "chrome/browser/media/router/media_routes_observer.h"
#include "chrome/browser/media/router/media_sinks_observer.h"
#include "chrome/browser/media/router/mojo/media_router_mojo_impl.h"
#include "chrome/browser/media/router/presentation_service_delegate_impl.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/ui/webui/media_router/media_router_localized_strings_provider.h"
#include "chrome/browser/ui/webui/media_router/media_router_resources_provider.h"
#include "chrome/browser/ui/webui/media_router/media_router_webui_message_handler.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/media_router/issue.h"
#include "chrome/common/media_router/media_route.h"
#include "chrome/common/media_router/media_sink.h"
#include "chrome/common/media_router/media_source.h"
#include "chrome/common/media_router/media_source_helper.h"
#include "chrome/common/media_router/route_request_result.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/constants.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "third_party/icu/source/i18n/unicode/coll.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/web_dialogs/web_dialog_delegate.h"
#include "url/origin.h"

namespace media_router {

namespace {

// The amount of time to wait for a response when creating a new route.
const int kCreateRouteTimeoutSeconds = 20;
const int kCreateRouteTimeoutSecondsForTab = 60;
const int kCreateRouteTimeoutSecondsForDesktop = 120;

std::string GetHostFromURL(const GURL& gurl) {
  if (gurl.is_empty()) return std::string();
  std::string host = gurl.host();
  if (base::StartsWith(host, "www.", base::CompareCase::INSENSITIVE_ASCII))
    host = host.substr(4);
  return host;
}

std::string TruncateHost(const std::string& host) {
  const std::string truncated =
      net::registry_controlled_domains::GetDomainAndRegistry(
          host, net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES);
  // The truncation will be empty in some scenarios (e.g. host is
  // simply an IP address). Fail gracefully.
  return truncated.empty() ? host : truncated;
}

base::TimeDelta GetRouteRequestTimeout(MediaCastMode cast_mode) {
  switch (cast_mode) {
    case DEFAULT:
      return base::TimeDelta::FromSeconds(kCreateRouteTimeoutSeconds);
    case TAB_MIRROR:
      return base::TimeDelta::FromSeconds(kCreateRouteTimeoutSecondsForTab);
    case DESKTOP_MIRROR:
      return base::TimeDelta::FromSeconds(kCreateRouteTimeoutSecondsForDesktop);
    default:
      NOTREACHED();
      return base::TimeDelta();
  }
}

// Returns the first source in |sources| that can be connected to by using the
// "Cast" button in the dialog, or an empty source if there is none.  This is
// used by the Media Router to find such a matching route if it exists.
MediaSource GetSourceForRouteObserver(const std::vector<MediaSource>& sources) {
  auto source_it =
      std::find_if(sources.begin(), sources.end(), CanConnectToMediaSource);
  return source_it != sources.end() ? *source_it : MediaSource("");
}

}  // namespace

// static
std::string MediaRouterUI::GetExtensionName(
    const GURL& gurl, extensions::ExtensionRegistry* registry) {
  if (gurl.is_empty() || !registry) return std::string();

  const extensions::Extension* extension =
      registry->enabled_extensions().GetExtensionOrAppByURL(gurl);

  return extension ? extension->name() : std::string();
}

// This class calls to refresh the UI when the highest priority issue is
// updated.
class MediaRouterUI::UIIssuesObserver : public IssuesObserver {
 public:
  UIIssuesObserver(MediaRouter* router, MediaRouterUI* ui)
      : IssuesObserver(router), ui_(ui) {
    DCHECK(ui);
  }

  ~UIIssuesObserver() override {}

  // IssuesObserver implementation.
  void OnIssue(const Issue& issue) override { ui_->SetIssue(issue); }
  void OnIssuesCleared() override { ui_->ClearIssue(); }

 private:
  // Reference back to the owning MediaRouterUI instance.
  MediaRouterUI* ui_;

  DISALLOW_COPY_AND_ASSIGN(UIIssuesObserver);
};

MediaRouterUI::UIMediaRoutesObserver::UIMediaRoutesObserver(
    MediaRouter* router,
    const MediaSource::Id& source_id,
    const RoutesUpdatedCallback& callback)
    : MediaRoutesObserver(router, source_id), callback_(callback) {
  DCHECK(!callback_.is_null());
}

MediaRouterUI::UIMediaRoutesObserver::~UIMediaRoutesObserver() {}

void MediaRouterUI::UIMediaRoutesObserver::OnRoutesUpdated(
    const std::vector<MediaRoute>& routes,
    const std::vector<MediaRoute::Id>& joinable_route_ids) {
  callback_.Run(routes, joinable_route_ids);
}

MediaRouterUI::UIMediaRouteControllerObserver::UIMediaRouteControllerObserver(
    MediaRouterUI* ui,
    scoped_refptr<MediaRouteController> controller)
    : MediaRouteController::Observer(std::move(controller)), ui_(ui) {
  if (controller_->current_media_status())
    OnMediaStatusUpdated(controller_->current_media_status().value());
}

MediaRouterUI::UIMediaRouteControllerObserver::
    ~UIMediaRouteControllerObserver() {}

void MediaRouterUI::UIMediaRouteControllerObserver::OnMediaStatusUpdated(
    const MediaStatus& status) {
  ui_->UpdateMediaRouteStatus(status);
}

void MediaRouterUI::UIMediaRouteControllerObserver::OnControllerInvalidated() {
  ui_->OnRouteControllerInvalidated();
}

MediaRouterUI::MediaRouterUI(content::WebUI* web_ui)
    : ConstrainedWebDialogUI(web_ui),
      ui_initialized_(false),
      current_route_request_id_(-1),
      route_request_counter_(0),
      initiator_(nullptr),
      router_(nullptr),
      weak_factory_(this) {
  auto handler = base::MakeUnique<MediaRouterWebUIMessageHandler>(this);
  handler_ = handler.get();

  // Create a WebUIDataSource containing the chrome://media-router page's
  // content.
  std::unique_ptr<content::WebUIDataSource> html_source(
      content::WebUIDataSource::Create(chrome::kChromeUIMediaRouterHost));

  content::WebContents* wc = web_ui->GetWebContents();
  DCHECK(wc);

  router_ =
      MediaRouterFactory::GetApiForBrowserContext(wc->GetBrowserContext());

  // Allows UI to load extensionview.
  // TODO(haibinlu): limit object-src to current extension once crbug/514866
  // is fixed.
  html_source->OverrideContentSecurityPolicyObjectSrc("object-src chrome:;");

  AddLocalizedStrings(html_source.get());
  AddMediaRouterUIResources(html_source.get());
  // Ownership of |html_source| is transferred to the BrowserContext.
  content::WebUIDataSource::Add(Profile::FromWebUI(web_ui),
                                html_source.release());

  web_ui->AddMessageHandler(std::move(handler));
}

MediaRouterUI::~MediaRouterUI() {
  if (query_result_manager_.get()) query_result_manager_->RemoveObserver(this);
  if (presentation_service_delegate_.get())
    presentation_service_delegate_->RemoveDefaultPresentationRequestObserver(
        this);
  // If |create_session_request_| still exists, then it means presentation route
  // request was never attempted.
  if (create_session_request_) {
    bool presentation_sinks_available = std::any_of(
        sinks_.begin(), sinks_.end(), [](const MediaSinkWithCastModes& sink) {
          return base::ContainsValue(sink.cast_modes, MediaCastMode::DEFAULT);
        });
    if (presentation_sinks_available) {
      create_session_request_->InvokeErrorCallback(content::PresentationError(
          content::PRESENTATION_ERROR_PRESENTATION_REQUEST_CANCELLED,
          "Dialog closed."));
    } else {
      create_session_request_->InvokeErrorCallback(content::PresentationError(
          content::PRESENTATION_ERROR_NO_AVAILABLE_SCREENS,
          "No screens found."));
    }
  }
}

void MediaRouterUI::InitWithDefaultMediaSource(
    content::WebContents* initiator,
    PresentationServiceDelegateImpl* delegate) {
  DCHECK(initiator);
  DCHECK(!presentation_service_delegate_);
  DCHECK(!query_result_manager_.get());

  InitCommon(initiator);
  if (delegate) {
    presentation_service_delegate_ = delegate->GetWeakPtr();
    presentation_service_delegate_->AddDefaultPresentationRequestObserver(this);
  }

  if (delegate && delegate->HasDefaultPresentationRequest()) {
    OnDefaultPresentationChanged(delegate->GetDefaultPresentationRequest());
  } else {
    // Register for MediaRoute updates without a media source.
    routes_observer_.reset(new UIMediaRoutesObserver(
        router_, MediaSource::Id(),
        base::Bind(&MediaRouterUI::OnRoutesUpdated, base::Unretained(this))));
  }
}

void MediaRouterUI::InitWithPresentationSessionRequest(
    content::WebContents* initiator,
    PresentationServiceDelegateImpl* delegate,
    std::unique_ptr<CreatePresentationConnectionRequest>
        create_session_request) {
  DCHECK(initiator);
  DCHECK(delegate);
  DCHECK(create_session_request);
  DCHECK(!create_session_request_);
  DCHECK(!query_result_manager_);

  create_session_request_ = std::move(create_session_request);
  presentation_service_delegate_ = delegate->GetWeakPtr();

  InitCommon(initiator);
  OnDefaultPresentationChanged(
      create_session_request_->presentation_request());
}

void MediaRouterUI::InitCommon(content::WebContents* initiator) {
  DCHECK(initiator);
  DCHECK(router_);

  TRACE_EVENT_NESTABLE_ASYNC_INSTANT1("media_router", "UI", initiator,
                                      "MediaRouterUI::InitCommon", this);

  // Presentation requests from content must show the origin requesting
  // presentation: crbug.com/704964
  if (create_session_request_)
    forced_cast_mode_ = MediaCastMode::DEFAULT;

  router_->OnUserGesture();

  // Create |collator_| before |query_result_manager_| so that |collator_| is
  // already set up when we get a callback from |query_result_manager_|.
  UErrorCode error = U_ZERO_ERROR;
  const std::string& locale = g_browser_process->GetApplicationLocale();
  collator_.reset(
      icu::Collator::createInstance(icu::Locale(locale.c_str()), error));
  if (U_FAILURE(error)) {
    DLOG(ERROR) << "Failed to create collator for locale " << locale;
    collator_.reset();
  }

  query_result_manager_.reset(new QueryResultManager(router_));
  query_result_manager_->AddObserver(this);

  // Use a placeholder URL as origin for mirroring.
  url::Origin origin{GURL(chrome::kChromeUIMediaRouterURL)};

  // Desktop mirror mode is always available.
  query_result_manager_->SetSourcesForCastMode(
      MediaCastMode::DESKTOP_MIRROR, {MediaSourceForDesktop()}, origin);
  initiator_ = initiator;
  SessionID::id_type tab_id = SessionTabHelper::IdForTab(initiator);
  if (tab_id != -1) {
    MediaSource mirroring_source(MediaSourceForTab(tab_id));
    query_result_manager_->SetSourcesForCastMode(MediaCastMode::TAB_MIRROR,
                                                 {mirroring_source}, origin);
  }

  UpdateCastModes();

  // Get the current list of media routes, so that the WebUI will have routes
  // information at initialization.
  OnRoutesUpdated(router_->GetCurrentRoutes(), std::vector<MediaRoute::Id>());
}

void MediaRouterUI::InitForTest(
    MediaRouter* router,
    content::WebContents* initiator,
    MediaRouterWebUIMessageHandler* handler,
    std::unique_ptr<CreatePresentationConnectionRequest>
        create_session_request) {
  router_ = router;
  handler_ = handler;
  create_session_request_ = std::move(create_session_request);
  InitCommon(initiator);
  if (create_session_request_) {
    OnDefaultPresentationChanged(
        create_session_request_->presentation_request());
  }
}

void MediaRouterUI::OnDefaultPresentationChanged(
    const PresentationRequest& presentation_request) {
  std::vector<MediaSource> sources = presentation_request.GetMediaSources();
  presentation_request_.reset(new PresentationRequest(presentation_request));
  query_result_manager_->SetSourcesForCastMode(
      MediaCastMode::DEFAULT, sources, presentation_request_->frame_origin());
  // Register for MediaRoute updates.  NOTE(mfoltz): If there are multiple
  // sources that can be connected to via the dialog, this will break.  We will
  // need to observe multiple sources (keyed by sinks) in that case.  As this is
  // Cast-specific for the forseeable future, it may be simpler to plumb a new
  // observer API for this case.
  const MediaSource source_for_route_observer =
      GetSourceForRouteObserver(sources);
  routes_observer_.reset(new UIMediaRoutesObserver(
      router_, source_for_route_observer.id(),
      base::Bind(&MediaRouterUI::OnRoutesUpdated, base::Unretained(this))));

  UpdateCastModes();
}

void MediaRouterUI::OnDefaultPresentationRemoved() {
  presentation_request_.reset();
  query_result_manager_->RemoveSourcesForCastMode(MediaCastMode::DEFAULT);

  // This should not be set if the dialog was initiated with a default
  // presentation request from the top level frame.  However, clear it just to
  // be safe.
  forced_cast_mode_ = base::nullopt;

  // Register for MediaRoute updates without a media source.
  routes_observer_.reset(new UIMediaRoutesObserver(
      router_, MediaSource::Id(),
      base::Bind(&MediaRouterUI::OnRoutesUpdated, base::Unretained(this))));
  UpdateCastModes();
}

void MediaRouterUI::UpdateCastModes() {
  // Gets updated cast modes from |query_result_manager_| and forwards it to UI.
  cast_modes_ = query_result_manager_->GetSupportedCastModes();
  if (ui_initialized_) {
    handler_->UpdateCastModes(cast_modes_, GetPresentationRequestSourceName(),
                              forced_cast_mode());
  }
}

void MediaRouterUI::UpdateRoutesToCastModesMapping() {
  std::unordered_map<MediaSource::Id, MediaCastMode> available_source_map;
  for (const auto& cast_mode : cast_modes_) {
    for (const auto& source :
         query_result_manager_->GetSourcesForCastMode(cast_mode)) {
      available_source_map.insert(std::make_pair(source.id(), cast_mode));
    }
  }

  routes_and_cast_modes_.clear();
  for (const auto& route : routes_) {
    auto source_entry = available_source_map.find(route.media_source().id());
    if (source_entry != available_source_map.end()) {
      routes_and_cast_modes_.insert(
          std::make_pair(route.media_route_id(), source_entry->second));
    }
  }
}

void MediaRouterUI::Close() {
  ConstrainedWebDialogDelegate* delegate = GetConstrainedDelegate();
  if (delegate) {
    delegate->GetWebDialogDelegate()->OnDialogClosed(std::string());
    delegate->OnDialogCloseFromWebUI();
  }
}

void MediaRouterUI::UIInitialized() {
  TRACE_EVENT_NESTABLE_ASYNC_END0("media_router", "UI", initiator_);
  ui_initialized_ = true;

  // Register for Issue updates.
  issues_observer_.reset(new UIIssuesObserver(router_, this));
  issues_observer_->Init();
}

bool MediaRouterUI::CreateRoute(const MediaSink::Id& sink_id,
                                MediaCastMode cast_mode) {
  MediaSource::Id source_id;
  url::Origin origin;
  std::vector<MediaRouteResponseCallback> route_response_callbacks;
  base::TimeDelta timeout;
  bool incognito;
  if (!SetRouteParameters(sink_id, cast_mode, &source_id, &origin,
                          &route_response_callbacks, &timeout, &incognito)) {
    SendIssueForUnableToCast(cast_mode);
    return false;
  }
  router_->CreateRoute(source_id, sink_id, origin, initiator_,
                       route_response_callbacks, timeout, incognito);
  return true;
}

bool MediaRouterUI::SetRouteParameters(
    const MediaSink::Id& sink_id,
    MediaCastMode cast_mode,
    MediaSource::Id* source_id,
    url::Origin* origin,
    std::vector<MediaRouteResponseCallback>* route_response_callbacks,
    base::TimeDelta* timeout,
    bool* incognito) {
  DCHECK(query_result_manager_.get());
  DCHECK(initiator_);

  // Note that there is a rarely-encountered bug, where the MediaCastMode to
  // MediaSource mapping could have been updated, between when the user clicked
  // on the UI to start a create route request, and when this function is
  // called. However, since the user does not have visibility into the
  // MediaSource, and that it occurs very rarely in practice, we leave it as-is
  // for now.
  std::unique_ptr<MediaSource> source =
      query_result_manager_->GetSourceForCastModeAndSink(cast_mode, sink_id);
  if (!source) {
    LOG(ERROR) << "No corresponding MediaSource for cast mode "
               << static_cast<int>(cast_mode) << " and sink " << sink_id;
    return false;
  }
  *source_id = source->id();

  bool for_default_source = cast_mode == MediaCastMode::DEFAULT;
  if (for_default_source && !presentation_request_) {
    DLOG(ERROR) << "Requested to create a route for presentation, but "
                << "presentation request is missing.";
    return false;
  }

  current_route_request_id_ = ++route_request_counter_;
  *origin = for_default_source
                ? presentation_request_->frame_origin()
                : url::Origin(GURL(chrome::kChromeUIMediaRouterURL));
  DVLOG(1) << "DoCreateRoute: origin: " << *origin;

  // There are 3 cases. In cases (1) and (3) the MediaRouterUI will need to be
  // notified. In case (2) the dialog will be closed.
  // (1) Non-presentation route request (e.g., mirroring). No additional
  // notification necessary.
  // (2) Presentation route request for a Presentation API startSession call.
  // The startSession (CreatePresentationConnectionRequest) will need to be
  // answered with the route response.
  // (3) Browser-initiated presentation route request. If successful,
  // PresentationServiceDelegateImpl will have to be notified. Note that we
  // treat subsequent route requests from a Presentation API-initiated dialogs
  // as browser-initiated.
  if (!for_default_source || !create_session_request_) {
    route_response_callbacks->push_back(base::Bind(
        &MediaRouterUI::OnRouteResponseReceived, weak_factory_.GetWeakPtr(),
        current_route_request_id_, sink_id, cast_mode,
        base::UTF8ToUTF16(GetTruncatedPresentationRequestSourceName())));
  }
  if (for_default_source) {
    if (create_session_request_) {
      // |create_session_request_| will be nullptr after this call, as the
      // object will be transferred to the callback.
      route_response_callbacks->push_back(
          base::Bind(&CreatePresentationConnectionRequest::HandleRouteResponse,
                     base::Passed(&create_session_request_)));
      route_response_callbacks->push_back(
          base::Bind(&MediaRouterUI::HandleCreateSessionRequestRouteResponse,
                     weak_factory_.GetWeakPtr()));
    } else if (presentation_service_delegate_) {
      route_response_callbacks->push_back(
          base::Bind(&PresentationServiceDelegateImpl::OnRouteResponse,
                     presentation_service_delegate_, *presentation_request_));
    }
  }

  *timeout = GetRouteRequestTimeout(cast_mode);
  *incognito = Profile::FromWebUI(web_ui())->IsOffTheRecord();

  return true;
}

bool MediaRouterUI::ConnectRoute(const MediaSink::Id& sink_id,
                                 const MediaRoute::Id& route_id) {
  MediaSource::Id source_id;
  url::Origin origin;
  std::vector<MediaRouteResponseCallback> route_response_callbacks;
  base::TimeDelta timeout;
  bool incognito;
  if (!SetRouteParameters(sink_id, MediaCastMode::DEFAULT, &source_id, &origin,
                          &route_response_callbacks, &timeout, &incognito)) {
    SendIssueForUnableToCast(MediaCastMode::DEFAULT);
    return false;
  }
  router_->ConnectRouteByRouteId(source_id, route_id, origin, initiator_,
                                 route_response_callbacks, timeout, incognito);
  return true;
}

void MediaRouterUI::CloseRoute(const MediaRoute::Id& route_id) {
  router_->TerminateRoute(route_id);
}

void MediaRouterUI::AddIssue(const IssueInfo& issue) {
  router_->AddIssue(issue);
}

void MediaRouterUI::ClearIssue(const Issue::Id& issue_id) {
  router_->ClearIssue(issue_id);
}

void MediaRouterUI::SearchSinksAndCreateRoute(
    const MediaSink::Id& sink_id,
    const std::string& search_criteria,
    const std::string& domain,
    MediaCastMode cast_mode) {
  std::unique_ptr<MediaSource> source =
      query_result_manager_->GetSourceForCastModeAndSink(cast_mode, sink_id);
  const std::string source_id = source ? source->id() : "";

  // The CreateRoute() part of the function is accomplished in the callback
  // OnSearchSinkResponseReceived().
  router_->SearchSinks(
      sink_id, source_id, search_criteria, domain,
      base::Bind(&MediaRouterUI::OnSearchSinkResponseReceived,
                 weak_factory_.GetWeakPtr(), cast_mode));
}

bool MediaRouterUI::UserSelectedTabMirroringForCurrentOrigin() const {
  const base::ListValue* origins =
      Profile::FromWebUI(web_ui())->GetPrefs()->GetList(
          prefs::kMediaRouterTabMirroringSources);
  return origins->Find(base::Value(GetSerializedInitiatorOrigin())) !=
         origins->end();
}

void MediaRouterUI::RecordCastModeSelection(MediaCastMode cast_mode) {
  ListPrefUpdate update(Profile::FromWebUI(web_ui())->GetPrefs(),
                        prefs::kMediaRouterTabMirroringSources);

  switch (cast_mode) {
    case MediaCastMode::DEFAULT:
      update->Remove(base::Value(GetSerializedInitiatorOrigin()), nullptr);
      break;
    case MediaCastMode::TAB_MIRROR:
      update->AppendIfNotPresent(
          base::MakeUnique<base::Value>(GetSerializedInitiatorOrigin()));
      break;
    case MediaCastMode::DESKTOP_MIRROR:
      // Desktop mirroring isn't domain-specific, so we don't record the
      // selection.
      break;
    default:
      NOTREACHED();
      break;
  }
}

void MediaRouterUI::OnResultsUpdated(
    const std::vector<MediaSinkWithCastModes>& sinks) {
  sinks_ = sinks;

  const icu::Collator* collator_ptr = collator_.get();
  std::sort(sinks_.begin(), sinks_.end(),
            [collator_ptr](const MediaSinkWithCastModes& sink1,
                           const MediaSinkWithCastModes& sink2) {
              return sink1.sink.CompareUsingCollator(sink2.sink, collator_ptr);
            });

  if (ui_initialized_)
    handler_->UpdateSinks(sinks_);
}

void MediaRouterUI::SetIssue(const Issue& issue) {
  if (ui_initialized_)
    handler_->UpdateIssue(issue);
}

void MediaRouterUI::ClearIssue() {
  if (ui_initialized_)
    handler_->ClearIssue();
}

void MediaRouterUI::OnRoutesUpdated(
    const std::vector<MediaRoute>& routes,
    const std::vector<MediaRoute::Id>& joinable_route_ids) {
  routes_.clear();
  joinable_route_ids_.clear();

  for (const MediaRoute& route : routes) {
    if (route.for_display()) {
#ifndef NDEBUG
      for (const MediaRoute& existing_route : routes_) {
        if (existing_route.media_sink_id() == route.media_sink_id()) {
          DVLOG(2) << "Received another route for display with the same sink"
                   << " id as an existing route. " << route.media_route_id()
                   << " has the same sink id as "
                   << existing_route.media_sink_id() << ".";
        }
      }
#endif
      if (base::ContainsValue(joinable_route_ids, route.media_route_id())) {
        joinable_route_ids_.push_back(route.media_route_id());
      }

      routes_.push_back(route);
    }
  }
  UpdateRoutesToCastModesMapping();

  if (ui_initialized_)
    handler_->UpdateRoutes(routes_, joinable_route_ids_,
                           routes_and_cast_modes_);
}

void MediaRouterUI::OnRouteResponseReceived(
    int route_request_id,
    const MediaSink::Id& sink_id,
    MediaCastMode cast_mode,
    const base::string16& presentation_request_source_name,
    const RouteRequestResult& result) {
  DVLOG(1) << "OnRouteResponseReceived";
  // If we receive a new route that we aren't expecting, do nothing.
  if (route_request_id != current_route_request_id_) return;

  const MediaRoute* route = result.route();
  if (!route) {
    // The provider will handle sending an issue for a failed route request.
    DVLOG(1) << "MediaRouteResponse returned error: " << result.error();
  }

  handler_->OnCreateRouteResponseReceived(sink_id, route);
  current_route_request_id_ = -1;

  if (result.result_code() == RouteRequestResult::TIMED_OUT)
    SendIssueForRouteTimeout(cast_mode, presentation_request_source_name);
}

void MediaRouterUI::HandleCreateSessionRequestRouteResponse(
    const RouteRequestResult&) {
  Close();
}

void MediaRouterUI::OnSearchSinkResponseReceived(
    MediaCastMode cast_mode,
    const MediaSink::Id& found_sink_id) {
  DVLOG(1) << "OnSearchSinkResponseReceived";
  handler_->ReturnSearchResult(found_sink_id);

  MediaSource::Id source_id;
  url::Origin origin;
  std::vector<MediaRouteResponseCallback> route_response_callbacks;
  base::TimeDelta timeout;
  bool incognito;
  if (!SetRouteParameters(found_sink_id, cast_mode, &source_id, &origin,
                          &route_response_callbacks, &timeout, &incognito)) {
    SendIssueForUnableToCast(cast_mode);
    return;
  }
  router_->CreateRoute(source_id, found_sink_id, origin, initiator_,
                       route_response_callbacks, timeout, incognito);
}

void MediaRouterUI::SendIssueForRouteTimeout(
    MediaCastMode cast_mode,
    const base::string16& presentation_request_source_name) {
  std::string issue_title;
  switch (cast_mode) {
    case DEFAULT:
      DLOG_IF(ERROR, presentation_request_source_name.empty())
          << "Empty presentation request source name.";
      issue_title =
          l10n_util::GetStringFUTF8(IDS_MEDIA_ROUTER_ISSUE_CREATE_ROUTE_TIMEOUT,
                                    presentation_request_source_name);
      break;
    case TAB_MIRROR:
      issue_title = l10n_util::GetStringUTF8(
          IDS_MEDIA_ROUTER_ISSUE_CREATE_ROUTE_TIMEOUT_FOR_TAB);
      break;
    case DESKTOP_MIRROR:
      issue_title = l10n_util::GetStringUTF8(
          IDS_MEDIA_ROUTER_ISSUE_CREATE_ROUTE_TIMEOUT_FOR_DESKTOP);
      break;
    default:
      NOTREACHED();
  }

  AddIssue(IssueInfo(issue_title, IssueInfo::Action::DISMISS,
                     IssueInfo::Severity::NOTIFICATION));
}

void MediaRouterUI::SendIssueForUnableToCast(MediaCastMode cast_mode) {
  // For a generic error, claim a tab error unless it was specifically desktop
  // mirroring.
  std::string issue_title =
      (cast_mode == MediaCastMode::DESKTOP_MIRROR)
          ? l10n_util::GetStringUTF8(
                IDS_MEDIA_ROUTER_ISSUE_UNABLE_TO_CAST_DESKTOP)
          : l10n_util::GetStringUTF8(
                IDS_MEDIA_ROUTER_ISSUE_CREATE_ROUTE_TIMEOUT_FOR_TAB);
  AddIssue(IssueInfo(issue_title, IssueInfo::Action::DISMISS,
                     IssueInfo::Severity::WARNING));
}

GURL MediaRouterUI::GetFrameURL() const {
  return presentation_request_ ? presentation_request_->frame_origin().GetURL()
                               : GURL();
}

std::string MediaRouterUI::GetPresentationRequestSourceName() const {
  GURL gurl = GetFrameURL();
  return gurl.SchemeIs(extensions::kExtensionScheme)
             ? GetExtensionName(gurl, extensions::ExtensionRegistry::Get(
                                          Profile::FromWebUI(web_ui())))
             : GetHostFromURL(gurl);
}

std::string MediaRouterUI::GetTruncatedPresentationRequestSourceName() const {
  GURL gurl = GetFrameURL();
  return gurl.SchemeIs(extensions::kExtensionScheme)
             ? GetExtensionName(gurl, extensions::ExtensionRegistry::Get(
                                          Profile::FromWebUI(web_ui())))
             : TruncateHost(GetHostFromURL(gurl));
}

const std::set<MediaCastMode>& MediaRouterUI::cast_modes() const {
  return cast_modes_;
}

const std::string& MediaRouterUI::GetRouteProviderExtensionId() const {
  // TODO(crbug.com/597778): remove reference to MediaRouterMojoImpl
  return static_cast<MediaRouterMojoImpl*>(router_)
      ->media_route_provider_extension_id();
}

void MediaRouterUI::SetUIInitializationTimer(const base::Time& start_time) {
  DCHECK(!start_time.is_null());
  start_time_ = start_time;
}

void MediaRouterUI::OnUIInitiallyLoaded() {
  if (!start_time_.is_null()) {
    MediaRouterMetrics::RecordMediaRouterDialogPaint(
        base::Time::Now() - start_time_);
  }
}

void MediaRouterUI::OnUIInitialDataReceived() {
  if (!start_time_.is_null()) {
    MediaRouterMetrics::RecordMediaRouterDialogLoaded(
        base::Time::Now() - start_time_);
    start_time_ = base::Time();
  }
}

void MediaRouterUI::UpdateMaxDialogHeight(int height) {
  handler_->UpdateMaxDialogHeight(height);
}

const MediaRouteController* MediaRouterUI::GetMediaRouteController() const {
  return route_controller_observer_
             ? route_controller_observer_->controller().get()
             : nullptr;
}

void MediaRouterUI::OnMediaControllerUIAvailable(
    const MediaRoute::Id& route_id) {
  scoped_refptr<MediaRouteController> controller =
      router_->GetRouteController(route_id);
  if (!controller) {
    DVLOG(1) << "Requested a route controller with an invalid route ID.";
    return;
  }
  DVLOG_IF(1, route_controller_observer_)
      << "Route controller observer unexpectedly exists.";
  route_controller_observer_ =
      base::MakeUnique<UIMediaRouteControllerObserver>(this, controller);
}

void MediaRouterUI::OnMediaControllerUIClosed() {
  route_controller_observer_.reset();
}

void MediaRouterUI::OnRouteControllerInvalidated() {
  route_controller_observer_.reset();
  handler_->OnRouteControllerInvalidated();
}

void MediaRouterUI::UpdateMediaRouteStatus(const MediaStatus& status) {
  handler_->UpdateMediaRouteStatus(status);
}

std::string MediaRouterUI::GetSerializedInitiatorOrigin() const {
  url::Origin origin = initiator_
                           ? url::Origin(initiator_->GetLastCommittedURL())
                           : url::Origin();
  return origin.Serialize();
}

}  // namespace media_router
