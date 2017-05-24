// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_MEDIA_ROUTER_MEDIA_ROUTER_UI_H_
#define CHROME_BROWSER_UI_WEBUI_MEDIA_ROUTER_MEDIA_ROUTER_UI_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/media/router/mojo/media_route_controller.h"
#include "chrome/browser/media/router/presentation_service_delegate_impl.h"
#include "chrome/browser/ui/webui/constrained_web_dialog_ui.h"
#include "chrome/browser/ui/webui/media_router/media_cast_mode.h"
#include "chrome/browser/ui/webui/media_router/media_sink_with_cast_modes.h"
#include "chrome/browser/ui/webui/media_router/query_result_manager.h"
#include "chrome/common/media_router/issue.h"
#include "chrome/common/media_router/media_source.h"
#include "content/public/browser/web_ui_data_source.h"
#include "third_party/icu/source/common/unicode/uversion.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}

namespace extensions {
class ExtensionRegistry;
}

namespace U_ICU_NAMESPACE {
class Collator;
}

namespace media_router {

class CreatePresentationConnectionRequest;
class IssuesObserver;
class MediaRoute;
class MediaRouter;
class MediaRoutesObserver;
class MediaRouterWebUIMessageHandler;
class MediaSink;
class RouteRequestResult;

// Implements the chrome://media-router user interface.
class MediaRouterUI : public ConstrainedWebDialogUI,
                      public QueryResultManager::Observer,
                      public PresentationServiceDelegateImpl::
                          DefaultPresentationRequestObserver {
 public:
  // |web_ui| owns this object and is used to initialize the base class.
  explicit MediaRouterUI(content::WebUI* web_ui);
  ~MediaRouterUI() override;

  // Initializes internal state (e.g. starts listening for MediaSinks) for
  // targeting the default MediaSource (if any) of the initiator tab that owns
  // |initiator|: Reference to the WebContents that initiated the dialog.
  //              Must not be null.
  // |delegate|, as well as mirroring sources of that tab.
  // The contents of the UI will change as the default MediaSource changes.
  // If there is a default MediaSource, then DEFAULT MediaCastMode will be
  // added to |cast_modes_|.
  // Init* methods can only be called once.
  // |delegate|: PresentationServiceDelegateImpl of the initiator tab.
  //             Must not be null.
  // TODO(imcheng): Replace use of impl with an intermediate abstract
  // interface.
  void InitWithDefaultMediaSource(content::WebContents* initiator,
                                  PresentationServiceDelegateImpl* delegate);

  // Initializes internal state targeting the presentation specified in
  // |request|. Also sets up mirroring sources based on |initiator|.
  // This is different from |InitWithDefaultMediaSource| in that it does not
  // listen for default media source changes, as the UI is fixed to the source
  // in |request|.
  // Init* methods can only be called once.
  // |initiator|: Reference to the WebContents that initiated the dialog.
  //              Must not be null.
  // |delegate|: PresentationServiceDelegateImpl of the initiator tab.
  //             Must not be null.
  // |presentation_request|: The presentation request. This instance will take
  //                         ownership of it. Must not be null.
  void InitWithPresentationSessionRequest(
      content::WebContents* initiator,
      PresentationServiceDelegateImpl* delegate,
      std::unique_ptr<CreatePresentationConnectionRequest>
          presentation_request);

  // Closes the media router UI.
  void Close();

  // Notifies this instance that the UI has been initialized.
  virtual void UIInitialized();

  // Requests a route be created from the source mapped to
  // |cast_mode|, to the sink given by |sink_id|.
  // Returns true if a route request is successfully submitted.
  // |OnRouteResponseReceived()| will be invoked when the route request
  // completes.
  bool CreateRoute(const MediaSink::Id& sink_id, MediaCastMode cast_mode);

  // Calls MediaRouter to join the given route.
  bool ConnectRoute(const MediaSink::Id& sink_id,
                    const MediaRoute::Id& route_id);

  // Calls MediaRouter to close the given route.
  void CloseRoute(const MediaRoute::Id& route_id);

  // Calls MediaRouter to add the given issue.
  void AddIssue(const IssueInfo& issue);

  // Calls MediaRouter to clear the given issue.
  void ClearIssue(const Issue::Id& issue_id);

  // Calls MediaRouter to search route providers for sinks matching
  // |search_criteria| with the source that is currently associated with
  // |cast_mode|. The user's domain |domain| is also used.
  void SearchSinksAndCreateRoute(const MediaSink::Id& sink_id,
                                 const std::string& search_criteria,
                                 const std::string& domain,
                                 MediaCastMode cast_mode);

  // Returns true if the cast mode last chosen for the current origin is tab
  // mirroring.
  virtual bool UserSelectedTabMirroringForCurrentOrigin() const;

  // Records the cast mode selection for the current origin, unless the cast
  // mode is MediaCastMode::DESKTOP_MIRROR.
  virtual void RecordCastModeSelection(MediaCastMode cast_mode);

  // Returns the hostname of the default source's parent frame URL.
  std::string GetPresentationRequestSourceName() const;
  std::string GetTruncatedPresentationRequestSourceName() const;
  bool HasPendingRouteRequest() const {
    return current_route_request_id_ != -1;
  }
  const std::vector<MediaSinkWithCastModes>& sinks() const { return sinks_; }
  const std::vector<MediaRoute>& routes() const { return routes_; }
  const std::vector<MediaRoute::Id>& joinable_route_ids() const {
    return joinable_route_ids_;
  }
  virtual const std::set<MediaCastMode>& cast_modes() const;
  const std::unordered_map<MediaRoute::Id, MediaCastMode>&
  routes_and_cast_modes() const {
    return routes_and_cast_modes_;
  }
  const content::WebContents* initiator() const { return initiator_; }
  const base::Optional<MediaCastMode>& forced_cast_mode() const {
    return forced_cast_mode_;
  }

  virtual const std::string& GetRouteProviderExtensionId() const;

  // Called to track UI metrics.
  void SetUIInitializationTimer(const base::Time& start_time);
  void OnUIInitiallyLoaded();
  void OnUIInitialDataReceived();

  void UpdateMaxDialogHeight(int height);

  // Gets the route controller currently in use by the UI. Returns a nullptr if
  // none is in use.
  virtual const MediaRouteController* GetMediaRouteController() const;

  // Called when a media controller UI surface is created. Creates an observer
  // for the MediaRouteController for |route_id| to listen for media status
  // updates.
  virtual void OnMediaControllerUIAvailable(const MediaRoute::Id& route_id);
  // Called when a media controller UI surface is closed. Resets the observer
  // for MediaRouteController.
  virtual void OnMediaControllerUIClosed();

  void InitForTest(MediaRouter* router,
                   content::WebContents* initiator,
                   MediaRouterWebUIMessageHandler* handler,
                   std::unique_ptr<CreatePresentationConnectionRequest>
                       create_session_request);

 private:
  friend class MediaRouterUITest;

  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest, SortedSinks);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest, SortSinksByIconType);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest, FilterNonDisplayRoutes);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest, FilterNonDisplayJoinableRoutes);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest,
      UIMediaRoutesObserverAssignsCurrentCastModes);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest,
      UIMediaRoutesObserverSkipsUnavailableCastModes);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest, GetExtensionNameExtensionPresent);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest,
                           GetExtensionNameEmptyWhenNotInstalled);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest,
                           GetExtensionNameEmptyWhenNotExtensionURL);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest,
                           RouteCreationTimeoutForPresentation);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest, RouteRequestFromIncognito);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest, OpenAndCloseUIDetailsView);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest, SendMediaCommands);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest, SendMediaStatusUpdate);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterUITest, SendInitialMediaStatusUpdate);

  class UIIssuesObserver;

  class UIMediaRoutesObserver : public MediaRoutesObserver {
   public:
    using RoutesUpdatedCallback =
        base::Callback<void(const std::vector<MediaRoute>&,
                            const std::vector<MediaRoute::Id>&)>;
    UIMediaRoutesObserver(MediaRouter* router,
                          const MediaSource::Id& source_id,
                          const RoutesUpdatedCallback& callback);
    ~UIMediaRoutesObserver() override;

    // MediaRoutesObserver
    void OnRoutesUpdated(
        const std::vector<MediaRoute>& routes,
        const std::vector<MediaRoute::Id>& joinable_route_ids) override;

   private:
    // Callback to the owning MediaRouterUI instance.
    RoutesUpdatedCallback callback_;

    DISALLOW_COPY_AND_ASSIGN(UIMediaRoutesObserver);
  };

  class UIMediaRouteControllerObserver : public MediaRouteController::Observer {
   public:
    explicit UIMediaRouteControllerObserver(
        MediaRouterUI* ui,
        scoped_refptr<MediaRouteController> controller);
    ~UIMediaRouteControllerObserver() override;

    // MediaRouteController::Observer
    void OnMediaStatusUpdated(const MediaStatus& status) override;
    void OnControllerInvalidated() override;

   private:
    MediaRouterUI* ui_;

    DISALLOW_COPY_AND_ASSIGN(UIMediaRouteControllerObserver);
  };

  static std::string GetExtensionName(const GURL& url,
                                      extensions::ExtensionRegistry* registry);

  // QueryResultManager::Observer
  void OnResultsUpdated(
      const std::vector<MediaSinkWithCastModes>& sinks) override;

  // Called by |issues_observer_| when the top issue has changed.
  // If the UI is already initialized, notifies |handler_| to update the UI.
  // Ignored if the UI is not yet initialized.
  void SetIssue(const Issue& issue);
  void ClearIssue();

  // Called by |routes_observer_| when the set of active routes has changed.
  void OnRoutesUpdated(const std::vector<MediaRoute>& routes,
                       const std::vector<MediaRoute::Id>& joinable_route_ids);

  // Callback passed to MediaRouter to receive response to route creation
  // requests.
  void OnRouteResponseReceived(
      int route_request_id,
      const MediaSink::Id& sink_id,
      MediaCastMode cast_mode,
      const base::string16& presentation_request_source_name,
      const RouteRequestResult& result);

  // Closes the dialog after receiving a route response when using
  // |create_session_request_|. This prevents the dialog from trying to use the
  // same presentation request again.
  void HandleCreateSessionRequestRouteResponse(const RouteRequestResult&);

  // Callback passed to MediaRouter to receive the sink ID of the sink found by
  // SearchSinksAndCreateRoute().
  void OnSearchSinkResponseReceived(MediaCastMode cast_mode,
                                    const MediaSink::Id& found_sink_id);

  // Creates and sends an issue if route creation timed out.
  void SendIssueForRouteTimeout(
      MediaCastMode cast_mode,
      const base::string16& presentation_request_source_name);

  // Creates and sends an issue if casting fails for any other reason.
  void SendIssueForUnableToCast(MediaCastMode cast_mode);

  // Initializes the dialog with mirroring sources derived from |initiator|.
  void InitCommon(content::WebContents* initiator);

  // PresentationServiceDelegateImpl::DefaultPresentationObserver
  void OnDefaultPresentationChanged(
      const PresentationRequest& presentation_request) override;
  void OnDefaultPresentationRemoved() override;

  // Populates common route-related parameters for CreateRoute(),
  // ConnectRoute(), and SearchSinksAndCreateRoute().
  bool SetRouteParameters(
      const MediaSink::Id& sink_id,
      MediaCastMode cast_mode,
      MediaSource::Id* source_id,
      url::Origin* origin,
      std::vector<MediaRouteResponseCallback>* route_response_callbacks,
      base::TimeDelta* timeout,
      bool* incognito);

  // Updates the set of supported cast modes and sends the updated set to
  // |handler_|.
  void UpdateCastModes();

  // Updates the routes-to-cast-modes mapping in |routes_and_cast_modes_| to
  // match the value of |routes_|.
  void UpdateRoutesToCastModesMapping();

  // Returns the default presentation request's frame URL if there is one.
  // Otherwise returns an empty GURL.
  GURL GetFrameURL() const;

  // Returns the serialized origin for |initiator_|, or the serialization of an
  // opaque origin ("null") if |initiator_| is not set.
  std::string GetSerializedInitiatorOrigin() const;

  // Destroys the route controller observer. Called when the route controller is
  // invalidated.
  void OnRouteControllerInvalidated();

  // Called by the internal route controller observer. Notifies the message
  // handler of a media status update for the route currently shown in the UI.
  void UpdateMediaRouteStatus(const MediaStatus& status);

  // Owned by the |web_ui| passed in the ctor, and guaranteed to be deleted
  // only after it has deleted |this|.
  MediaRouterWebUIMessageHandler* handler_ = nullptr;

  // These are non-null while this instance is registered to receive
  // updates from them.
  std::unique_ptr<IssuesObserver> issues_observer_;
  std::unique_ptr<MediaRoutesObserver> routes_observer_;

  // Set to true by |handler_| when the UI has been initialized.
  bool ui_initialized_;

  // Set to -1 if not tracking a pending route request.
  int current_route_request_id_;

  // Sequential counter for route requests. Used to update
  // |current_route_request_id_| when there is a new route request.
  int route_request_counter_;

  // Used for locale-aware sorting of sinks by name. Set during |InitCommon()|
  // using the current locale. Set to null
  std::unique_ptr<icu::Collator> collator_;

  std::vector<MediaSinkWithCastModes> sinks_;
  std::vector<MediaRoute> routes_;
  std::vector<MediaRoute::Id> joinable_route_ids_;
  CastModeSet cast_modes_;
  std::unordered_map<MediaRoute::Id, MediaCastMode> routes_and_cast_modes_;

  std::unique_ptr<QueryResultManager> query_result_manager_;

  // If set, then the result of the next presentation route request will
  // be handled by this object.
  std::unique_ptr<CreatePresentationConnectionRequest> create_session_request_;

  // Set to the presentation request corresponding to the presentation cast
  // mode, if supported. Otherwise set to nullptr.
  std::unique_ptr<PresentationRequest> presentation_request_;

  // It's possible for PresentationServiceDelegateImpl to be destroyed before
  // this class.
  // (e.g. if a tab with the UI open is closed, then the tab WebContents will
  // be destroyed first momentarily before the UI WebContents).
  // Holding a WeakPtr to PresentationServiceDelegateImpl is the cleanest way to
  // handle this.
  // TODO(imcheng): hold a weak ptr to an abstract type instead.
  base::WeakPtr<PresentationServiceDelegateImpl> presentation_service_delegate_;

  content::WebContents* initiator_;

  // Pointer to the MediaRouter for this instance's BrowserContext.
  MediaRouter* router_;

  // The start time for UI initialization metrics timer. When a dialog has been
  // been painted and initialized with initial data, this should be cleared.
  base::Time start_time_;

  // The observer for the route controller. Notifies |handler_| of media status
  // updates.
  std::unique_ptr<UIMediaRouteControllerObserver> route_controller_observer_;

  // If set, a cast mode that is required to be shown first.
  base::Optional<MediaCastMode> forced_cast_mode_;

  // NOTE: Weak pointers must be invalidated before all other member variables.
  // Therefore |weak_factory_| must be placed at the end.
  base::WeakPtrFactory<MediaRouterUI> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(MediaRouterUI);
};

}  // namespace media_router

#endif  // CHROME_BROWSER_UI_WEBUI_MEDIA_ROUTER_MEDIA_ROUTER_UI_H_
