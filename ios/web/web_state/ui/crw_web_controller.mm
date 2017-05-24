// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/web_state/ui/crw_web_controller.h"

#import <WebKit/WebKit.h>

#import <objc/runtime.h>
#include <stddef.h>

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/containers/mru_cache.h"
#import "base/ios/block_types.h"
#include "base/ios/ios_util.h"
#import "base/ios/ns_error_util.h"
#import "base/ios/weak_nsobject.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#import "base/mac/bind_objc_block.h"
#include "base/mac/bundle_locations.h"
#include "base/mac/foundation_util.h"
#import "base/mac/objc_property_releaser.h"
#include "base/mac/scoped_cftyperef.h"
#import "base/mac/scoped_nsobject.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#import "ios/net/http_response_headers_util.h"
#import "ios/net/nsurlrequest_util.h"
#include "ios/web/history_state_util.h"
#import "ios/web/interstitials/web_interstitial_impl.h"
#import "ios/web/navigation/crw_session_controller.h"
#import "ios/web/navigation/navigation_item_impl.h"
#import "ios/web/navigation/navigation_manager_impl.h"
#include "ios/web/net/cert_host_pair.h"
#import "ios/web/net/crw_cert_verification_controller.h"
#import "ios/web/net/crw_ssl_status_updater.h"
#include "ios/web/public/browser_state.h"
#include "ios/web/public/favicon_url.h"
#import "ios/web/public/java_script_dialog_presenter.h"
#import "ios/web/public/navigation_item.h"
#import "ios/web/public/navigation_manager.h"
#import "ios/web/public/origin_util.h"
#include "ios/web/public/referrer.h"
#include "ios/web/public/referrer_util.h"
#include "ios/web/public/ssl_status.h"
#import "ios/web/public/url_scheme_util.h"
#include "ios/web/public/url_util.h"
#import "ios/web/public/web_client.h"
#include "ios/web/public/web_kit_constants.h"
#import "ios/web/public/web_state/context_menu_params.h"
#import "ios/web/public/web_state/crw_web_controller_observer.h"
#import "ios/web/public/web_state/js/crw_js_injection_manager.h"
#import "ios/web/public/web_state/js/crw_js_injection_receiver.h"
#import "ios/web/public/web_state/page_display_state.h"
#include "ios/web/public/web_state/session_certificate_policy_cache.h"
#import "ios/web/public/web_state/ui/crw_content_view.h"
#import "ios/web/public/web_state/ui/crw_context_menu_delegate.h"
#import "ios/web/public/web_state/ui/crw_native_content.h"
#import "ios/web/public/web_state/ui/crw_native_content_provider.h"
#import "ios/web/public/web_state/ui/crw_web_view_content_view.h"
#import "ios/web/public/web_state/ui/crw_web_view_scroll_view_proxy.h"
#include "ios/web/public/web_state/url_verification_constants.h"
#import "ios/web/public/web_state/web_state.h"
#include "ios/web/public/web_state/web_state_interface_provider.h"
#include "ios/web/public/webui/web_ui_ios.h"
#import "ios/web/web_state/crw_pass_kit_downloader.h"
#import "ios/web/web_state/error_translation_util.h"
#import "ios/web/web_state/js/crw_js_plugin_placeholder_manager.h"
#import "ios/web/web_state/js/crw_js_post_request_loader.h"
#import "ios/web/web_state/js/crw_js_window_id_manager.h"
#import "ios/web/web_state/navigation_context_impl.h"
#import "ios/web/web_state/page_viewport_state.h"
#import "ios/web/web_state/ui/crw_context_menu_controller.h"
#import "ios/web/web_state/ui/crw_swipe_recognizer_provider.h"
#import "ios/web/web_state/ui/crw_web_controller.h"
#import "ios/web/web_state/ui/crw_web_controller_container_view.h"
#import "ios/web/web_state/ui/crw_web_view_proxy_impl.h"
#import "ios/web/web_state/ui/crw_wk_navigation_states.h"
#import "ios/web/web_state/ui/crw_wk_script_message_router.h"
#import "ios/web/web_state/ui/wk_back_forward_list_item_holder.h"
#import "ios/web/web_state/ui/wk_web_view_configuration_provider.h"
#import "ios/web/web_state/web_controller_observer_bridge.h"
#import "ios/web/web_state/web_state_impl.h"
#import "ios/web/web_state/web_view_internal_creation_util.h"
#import "ios/web/web_state/wk_web_view_security_util.h"
#import "ios/web/webui/crw_web_ui_manager.h"
#import "ios/web/webui/mojo_facade.h"
#import "net/base/mac/url_conversions.h"
#include "net/base/net_errors.h"
#include "net/cert/x509_util_ios.h"
#include "net/ssl/ssl_info.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"
#include "url/url_constants.h"

using base::UserMetricsAction;
using web::NavigationManager;
using web::NavigationManagerImpl;
using web::WebState;
using web::WebStateImpl;

namespace {

// Struct to capture data about a user interaction. Records the time of the
// interaction and the main document URL at that time.
struct UserInteractionEvent {
  UserInteractionEvent(GURL url)
      : main_document_url(url), time(CFAbsoluteTimeGetCurrent()) {}
  // Main document URL at the time the interaction occurred.
  GURL main_document_url;
  // Time that the interaction occured, measured in seconds since Jan 1 2001.
  CFAbsoluteTime time;
};

// Keys for JavaScript command handlers context.
NSString* const kUserIsInteractingKey = @"userIsInteracting";
NSString* const kOriginURLKey = @"originURL";

// Standard User Defaults key for "Log JS" debug setting.
NSString* const kLogJavaScript = @"LogJavascript";

// Key of UMA IOSFix.ViewportZoomBugCount histogram.
const char kUMAViewportZoomBugCount[] = "Renderer.ViewportZoomBugCount";

// Key of the UMA Navigation.IOSWKWebViewSlowFastBackForward histogram.
const char kUMAWKWebViewSlowFastBackForwardNavigationKey[] =
    "Navigation.IOSWKWebViewSlowFastBackForward";

// Values for the histogram that counts slow/fast back/forward navigations.
enum class BackForwardNavigationType {
  // Fast back navigation through WKWebView back-forward list.
  FAST_BACK = 0,
  // Slow back navigation when back-forward list navigation is not possible.
  SLOW_BACK = 1,
  // Fast forward navigation through WKWebView back-forward list.
  FAST_FORWARD = 2,
  // Slow forward navigation when back-forward list navigation is not possible.
  SLOW_FORWARD = 3,
  BACK_FORWARD_NAVIGATION_TYPE_COUNT
};

// URL scheme for messages sent from javascript for asynchronous processing.
NSString* const kScriptMessageName = @"crwebinvoke";

// Constants for storing the source of NSErrors received by WKWebViews:
// - Errors received by |-webView:didFailProvisionalNavigation:withError:| are
//   recorded using WKWebViewErrorSource::PROVISIONAL_LOAD.  These should be
//   cancelled.
// - Errors received by |-webView:didFailNavigation:withError:| are recorded
//   using WKWebViewsource::NAVIGATION.  These errors should not be cancelled,
//   as the WKWebView will automatically retry the load.
NSString* const kWKWebViewErrorSourceKey = @"ErrorSource";
typedef enum { NONE = 0, PROVISIONAL_LOAD, NAVIGATION } WKWebViewErrorSource;

// Represents cert verification error, which happened inside
// |webView:didReceiveAuthenticationChallenge:completionHandler:| and should
// be checked inside |webView:didFailProvisionalNavigation:withError:|.
struct CertVerificationError {
  CertVerificationError(BOOL is_recoverable, net::CertStatus status)
      : is_recoverable(is_recoverable), status(status) {}

  BOOL is_recoverable;
  net::CertStatus status;
};

// Type of Cache object for storing cert verification errors.
typedef base::MRUCache<web::CertHostPair, CertVerificationError>
    CertVerificationErrorsCacheType;

// Maximum number of errors to store in cert verification errors cache.
// Cache holds errors only for pending navigations, so the actual number of
// stored errors is not expected to be high.
const CertVerificationErrorsCacheType::size_type kMaxCertErrorsCount = 100;

// States for external URL requests. This enum is used in UMA and
// entries should not be re-ordered or deleted.
enum ExternalURLRequestStatus {
  MAIN_FRAME_ALLOWED = 0,
  SUBFRAME_ALLOWED,
  SUBFRAME_BLOCKED,
  NUM_EXTERNAL_URL_REQUEST_STATUS
};

// Utility function for getting the source of NSErrors received by WKWebViews.
WKWebViewErrorSource WKWebViewErrorSourceFromError(NSError* error) {
  DCHECK(error);
  return static_cast<WKWebViewErrorSource>(
      [error.userInfo[kWKWebViewErrorSourceKey] integerValue]);
}
// Utility function for converting the WKWebViewErrorSource to the NSError
// received by WKWebViews.
NSError* WKWebViewErrorWithSource(NSError* error, WKWebViewErrorSource source) {
  DCHECK(error);
  base::scoped_nsobject<NSMutableDictionary> userInfo(
      [error.userInfo mutableCopy]);
  [userInfo setObject:@(source) forKey:kWKWebViewErrorSourceKey];
  return
      [NSError errorWithDomain:error.domain code:error.code userInfo:userInfo];
}
}  // namespace

#pragma mark -

// A container object for any navigation information that is only available
// during pre-commit delegate callbacks, and thus must be held until the
// navigation commits and the informatino can be used.
@interface CRWWebControllerPendingNavigationInfo : NSObject {
  base::mac::ObjCPropertyReleaser
      _propertyReleaser_CRWWebControllerPendingNavigationInfo;
}
// The referrer for the page.
@property(nonatomic, copy) NSString* referrer;
// The MIME type for the page.
@property(nonatomic, copy) NSString* MIMEType;
// The navigation type for the load.
@property(nonatomic, assign) WKNavigationType navigationType;
// HTTP request method for the load.
@property(nonatomic, copy) NSString* HTTPMethod;
// Whether the pending navigation has been directly cancelled before the
// navigation is committed.
// Cancelled navigations should be simply discarded without handling any
// specific error.
@property(nonatomic, assign) BOOL cancelled;
@end

@implementation CRWWebControllerPendingNavigationInfo
@synthesize referrer = _referrer;
@synthesize MIMEType = _MIMEType;
@synthesize navigationType = _navigationType;
@synthesize HTTPMethod = _HTTPMethod;
@synthesize cancelled = _cancelled;

- (instancetype)init {
  if ((self = [super init])) {
    _propertyReleaser_CRWWebControllerPendingNavigationInfo.Init(
        self, [CRWWebControllerPendingNavigationInfo class]);
    _navigationType = WKNavigationTypeOther;
  }
  return self;
}
@end

@interface CRWWebController ()<CRWContextMenuDelegate,
                               CRWNativeContentDelegate,
                               CRWSSLStatusUpdaterDataSource,
                               CRWSSLStatusUpdaterDelegate,
                               CRWWebControllerContainerViewDelegate,
                               CRWWebViewScrollViewProxyObserver,
                               WKNavigationDelegate,
                               WKUIDelegate> {
  base::WeakNSProtocol<id<CRWWebDelegate>> _delegate;
  base::WeakNSProtocol<id<CRWNativeContentProvider>> _nativeProvider;
  base::WeakNSProtocol<id<CRWSwipeRecognizerProvider>> _swipeRecognizerProvider;
  // The WKWebView managed by this instance.
  base::scoped_nsobject<WKWebView> _webView;
  // The CRWWebViewProxy is the wrapper to give components access to the
  // web view in a controlled and limited way.
  base::scoped_nsobject<CRWWebViewProxyImpl> _webViewProxy;
  // The view used to display content.  Must outlive |_webViewProxy|. The
  // container view should be accessed through this property rather than
  // |self.view| from within this class, as |self.view| triggers creation while
  // |self.containerView| will return nil if the view hasn't been instantiated.
  base::scoped_nsobject<CRWWebControllerContainerView> _containerView;
  // If |_contentView| contains a native view rather than a web view, this
  // is its controller. If it's a web view, this is nil.
  base::scoped_nsprotocol<id<CRWNativeContent>> _nativeController;
  BOOL _isHalted;  // YES if halted. Halting happens prior to destruction.
  BOOL _isBeingDestroyed;  // YES if in the process of closing.
  // All CRWWebControllerObservers attached to the CRWWebController. A
  // specially-constructed set is used that does not retain its elements.
  base::scoped_nsobject<NSMutableSet> _observers;
  // Each observer in |_observers| is associated with a
  // WebControllerObserverBridge in order to listen from WebState callbacks.
  // TODO(droger): Remove |_observerBridges| when all CRWWebControllerObservers
  // are converted to WebStateObservers.
  std::vector<std::unique_ptr<web::WebControllerObserverBridge>>
      _observerBridges;
  // YES if a user interaction has been registered at any time once the page has
  // loaded.
  BOOL _userInteractionRegistered;
  // YES if the user has interacted with the content area since the last URL
  // change.
  BOOL _interactionRegisteredSinceLastURLChange;
  // The actual URL of the document object (i.e., the last committed URL).
  // TODO(crbug.com/549616): Remove this in favor of just updating the
  // navigation manager and treating that as authoritative.
  GURL _documentURL;
  // Last URL change reported to webWill/DidStartLoadingURL. Used to detect page
  // location changes (client redirects) in practice.
  GURL _lastRegisteredRequestURL;
  // Last URL change reported to webDidStartLoadingURL. Used to detect page
  // location changes in practice.
  GURL _URLOnStartLoading;
  // Page loading phase.
  web::LoadPhase _loadPhase;
  // The web::PageDisplayState recorded when the page starts loading.
  web::PageDisplayState _displayStateOnStartLoading;
  // Whether or not the page has zoomed since the current navigation has been
  // committed, either by user interaction or via |-restoreStateFromHistory|.
  BOOL _pageHasZoomed;
  // Whether a PageDisplayState is currently being applied.
  BOOL _applyingPageState;
  // Actions to execute once the page load is complete.
  base::scoped_nsobject<NSMutableArray> _pendingLoadCompleteActions;
  // UIGestureRecognizers to add to the web view.
  base::scoped_nsobject<NSMutableArray> _gestureRecognizers;
  // Toolbars to add to the web view.
  base::scoped_nsobject<NSMutableArray> _webViewToolbars;
  // Flag to say if browsing is enabled.
  BOOL _webUsageEnabled;
  // Content view was reset due to low memory. Use the placeholder overlay on
  // next creation.
  BOOL _usePlaceholderOverlay;
  // The next time the view is requested, reload the page (using the placeholder
  // overlay until it's loaded).
  BOOL _requireReloadOnDisplay;
  // Overlay view used instead of webView.
  base::scoped_nsobject<UIImageView> _placeholderOverlayView;
  // The touch tracking recognizer allowing us to decide if a navigation is
  // started by the user.
  base::scoped_nsobject<CRWTouchTrackingRecognizer> _touchTrackingRecognizer;
  // The controller that tracks long press and check context menu trigger.
  base::scoped_nsobject<CRWContextMenuController> _contextMenuController;
  // Whether a click is in progress.
  BOOL _clickInProgress;
  // Data on the recorded last user interaction.
  std::unique_ptr<UserInteractionEvent> _lastUserInteraction;
  // YES if there has been user interaction with views owned by this controller.
  BOOL _userInteractedWithWebController;
  // The time of the last page transfer start, measured in seconds since Jan 1
  // 2001.
  CFAbsoluteTime _lastTransferTimeInSeconds;
  // Default URL (about:blank).
  GURL _defaultURL;
  // Show overlay view, don't reload web page.
  BOOL _overlayPreviewMode;
  // The URL of an expected future recreation of the |webView|. Valid
  // only if the web view was discarded for non-user-visible reasons, such that
  // if the next load request is for that URL, it should be treated as a
  // reconstruction that should use cache aggressively.
  GURL _expectedReconstructionURL;
  // Whether the web page is currently performing window.history.pushState or
  // window.history.replaceState
  // Set to YES on window.history.willChangeState message. To NO on
  // window.history.didPushState or window.history.didReplaceState.
  BOOL _changingHistoryState;
  // Set to YES when a hashchange event is manually dispatched for same-document
  // history navigations.
  BOOL _dispatchingSameDocumentHashChangeEvent;
  // YES if the web process backing _wkWebView is believed to currently be dead.
  BOOL _webProcessIsDead;

  // Object for loading POST requests with body.
  base::scoped_nsobject<CRWJSPOSTRequestLoader> _POSTRequestLoader;

  // WebStateImpl instance associated with this CRWWebController, web controller
  // does not own this pointer.
  WebStateImpl* _webStateImpl;

  // A set of URLs opened in external applications; stored so that errors
  // from the web view can be identified as resulting from these events.
  base::scoped_nsobject<NSMutableSet> _openedApplicationURL;

  // A set of script managers whose scripts have been injected into the current
  // page.
  // TODO(stuartmorgan): Revisit this approach; it's intended only as a stopgap
  // measure to make all the existing script managers work. Longer term, there
  // should probably be a couple of points where managers can register to have
  // things happen automatically based on page lifecycle, and if they don't want
  // to use one of those fixed points, they should make their scripts internally
  // idempotent.
  base::scoped_nsobject<NSMutableSet> _injectedScriptManagers;

  // Script manager for setting the windowID.
  base::scoped_nsobject<CRWJSWindowIDManager> _windowIDJSManager;

  // The receiver of JavaScripts.
  base::scoped_nsobject<CRWJSInjectionReceiver> _jsInjectionReceiver;

  // Handles downloading PassKit data for WKWebView. Lazy initialized.
  base::scoped_nsobject<CRWPassKitDownloader> _passKitDownloader;

  // Backs up property with the same name.
  std::unique_ptr<web::MojoFacade> _mojoFacade;

  // Referrer for the current page; does not include the fragment.
  base::scoped_nsobject<NSString> _currentReferrerString;

  // Pending information for an in-progress page navigation. The lifetime of
  // this object starts at |decidePolicyForNavigationAction| where the info is
  // extracted from the request, and ends at either |didCommitNavigation| or
  // |didFailProvisionalNavigation|.
  base::scoped_nsobject<CRWWebControllerPendingNavigationInfo>
      _pendingNavigationInfo;

  // Holds all WKNavigation objects and their states which are currently in
  // flight.
  base::scoped_nsobject<CRWWKNavigationStates> _navigationStates;

  // The WKNavigation captured when |stopLoading| was called. Used for reporting
  // WebController.EmptyNavigationManagerCausedByStopLoading UMA metric which
  // helps with diagnosing a navigation related crash (crbug.com/565457).
  base::WeakNSObject<WKNavigation> _stoppedWKNavigation;

  // CRWWebUIManager object for loading WebUI pages.
  base::scoped_nsobject<CRWWebUIManager> _webUIManager;

  // Updates SSLStatus for current navigation item.
  base::scoped_nsobject<CRWSSLStatusUpdater> _SSLStatusUpdater;

  // Controller used for certs verification to help with blocking requests with
  // bad SSL cert, presenting SSL interstitials and determining SSL status for
  // Navigation Items.
  base::scoped_nsobject<CRWCertVerificationController>
      _certVerificationController;

  // CertVerification errors which happened inside
  // |webView:didReceiveAuthenticationChallenge:completionHandler:|.
  // Key is leaf-cert/host pair. This storage is used to carry calculated
  // cert status from |didReceiveAuthenticationChallenge:| to
  // |didFailProvisionalNavigation:| delegate method.
  std::unique_ptr<CertVerificationErrorsCacheType> _certVerificationErrors;
}

// If |contentView_| contains a web view, this is the web view it contains.
// If not, it's nil.
@property(nonatomic, readonly) WKWebView* webView;
// The scroll view of |webView|.
@property(nonatomic, readonly) UIScrollView* webScrollView;
// The current page state of the web view. Writing to this property
// asynchronously applies the passed value to the current web view.
@property(nonatomic, readwrite) web::PageDisplayState pageDisplayState;
// The currently displayed native controller, if any.
@property(nonatomic, readwrite) id<CRWNativeContent> nativeController;
// Returns NavigationManager's session controller.
@property(nonatomic, readonly) CRWSessionController* sessionController;
// The associated NavigationManagerImpl.
@property(nonatomic, readonly) NavigationManagerImpl* navigationManagerImpl;
// Whether the associated WebState has an opener.
@property(nonatomic, readonly) BOOL hasOpener;
// Dictionary where keys are the names of WKWebView properties and values are
// selector names which should be called when a corresponding property has
// changed. e.g. @{ @"URL" : @"webViewURLDidChange" } means that
// -[self webViewURLDidChange] must be called every time when WKWebView.URL is
// changed.
@property(nonatomic, readonly) NSDictionary* WKWebViewObservers;
// Downloader for PassKit files. Lazy initialized.
@property(nonatomic, readonly) CRWPassKitDownloader* passKitDownloader;

// The web view's view of the current URL. During page transitions
// this may not be the same as the session history's view of the current URL.
// This method can change the state of the CRWWebController, as it will display
// an error if the returned URL is not reliable from a security point of view.
// Note that this method is expensive, so it should always be cached locally if
// it's needed multiple times in a method.
@property(nonatomic, readonly) GURL currentURL;
// Returns the referrer for the current page.
@property(nonatomic, readonly) web::Referrer currentReferrer;

// Returns YES if the user interacted with the page recently.
@property(nonatomic, readonly) BOOL userClickedRecently;

// User agent type of the transient item if any, the pending item if a
// navigation is in progress or the last committed item otherwise.
// Returns MOBILE, the default type, if navigation manager is nullptr or empty.
@property(nonatomic, readonly) web::UserAgentType userAgentType;

// Facade for Mojo API.
@property(nonatomic, readonly) web::MojoFacade* mojoFacade;

// TODO(crbug.com/692871): Remove these functions and replace with more
// appropriate NavigationItem getters.
// Returns the navigation item for the current page.
@property(nonatomic, readonly) web::NavigationItemImpl* currentNavItem;
// Returns the current transition type.
@property(nonatomic, readonly) ui::PageTransition currentTransition;
// Returns the referrer for current navigation item. May be empty.
@property(nonatomic, readonly) web::Referrer currentNavItemReferrer;
// The HTTP headers associated with the current navigation item. These are nil
// unless the request was a POST.
@property(nonatomic, readonly) NSDictionary* currentHTTPHeaders;

// YES if a user interaction has been registered at any time since the page has
// loaded.
@property(nonatomic, readwrite) BOOL userInteractionRegistered;

// Updates web view's user agent according to |userAgentType|. It is no-op if
// |userAgentType| is NONE.
- (void)updateWebViewUserAgentFromUserAgentType:
    (web::UserAgentType)userAgentType;

// Requires that the next load rebuild the web view. This is expensive, and
// should be used only in the case where something has changed that web view
// only checks on creation, such that the whole object needs to be rebuilt.
// TODO(stuartmorgan): Merge this and reinitializeWebViewAndReload:. They are
// currently subtly different in terms of implementation, but are for
// fundamentally the same purpose.
- (void)requirePageReconstruction;

// Removes the container view from the hierarchy and resets the ivar.
- (void)resetContainerView;
// Called when the web page has changed document and/or URL, and so the page
// navigation should be reported to the delegate, and internal state updated to
// reflect the fact that the navigation has occurred.
// TODO(stuartmorgan): The code conflates URL changes and document object
// changes; the two need to be separated and handled differently.
- (void)webPageChanged;
// Resets any state that is associated with a specific document object (e.g.,
// page interaction tracking).
- (void)resetDocumentSpecificState;
// Called when a page (native or web) has actually started loading (i.e., for
// a web page the document has actually changed), or after the load request has
// been registered for a non-document-changing URL change. Updates internal
// state not specific to web pages.
- (void)didStartLoadingURL:(const GURL&)URL;
// Returns YES if the URL looks like it is one CRWWebController can show.
+ (BOOL)webControllerCanShow:(const GURL&)url;
// Clears the currently-displayed transient content view.
- (void)clearTransientContentView;
// Returns a lazily created CRWTouchTrackingRecognizer.
- (CRWTouchTrackingRecognizer*)touchTrackingRecognizer;
// Shows placeholder overlay.
- (void)addPlaceholderOverlay;
// Removes placeholder overlay.
- (void)removePlaceholderOverlay;

// Creates a web view if it's not yet created.
- (void)ensureWebViewCreated;
// Creates a web view with given |config|. No-op if web view is already created.
- (void)ensureWebViewCreatedWithConfiguration:(WKWebViewConfiguration*)config;
// Returns a new autoreleased web view created with given configuration.
- (WKWebView*)webViewWithConfiguration:(WKWebViewConfiguration*)config;
// Sets the value of the webView property, and performs its basic setup.
- (void)setWebView:(WKWebView*)webView;
// Wraps the web view in a CRWWebViewContentView and adds it to the container
// view.
- (void)displayWebView;
// Removes webView, optionally tracking the URL of the evicted
// page for later cache-based reconstruction.
- (void)removeWebViewAllowingCachedReconstruction:(BOOL)allowCache;
// Called when web view process has been terminated.
- (void)webViewWebProcessDidCrash;
// Returns the WKWebViewConfigurationProvider associated with the web
// controller's BrowserState.
- (web::WKWebViewConfigurationProvider&)webViewConfigurationProvider;
// Extracts "Referer" [sic] value from WKNavigationAction request header.
- (NSString*)referrerFromNavigationAction:(WKNavigationAction*)action;

// Returns the current URL of the web view, and sets |trustLevel| accordingly
// based on the confidence in the verification.
- (GURL)webURLWithTrustLevel:(web::URLVerificationTrustLevel*)trustLevel;
// Returns |YES| if |url| should be loaded in a native view.
- (BOOL)shouldLoadURLInNativeView:(const GURL&)url;
// Loads the request into the |webView|.
- (WKNavigation*)loadRequest:(NSMutableURLRequest*)request;
// Loads POST request with body in |_wkWebView| by constructing an HTML page
// that executes the request through JavaScript and replaces document with the
// result.
// Note that this approach includes multiple body encodings and decodings, plus
// the data is passed to |_wkWebView| on main thread.
// This is necessary because WKWebView ignores POST request body.
// Workaround for https://bugs.webkit.org/show_bug.cgi?id=145410
- (WKNavigation*)loadPOSTRequest:(NSMutableURLRequest*)request;
// Loads the HTML into the page at the given URL.
- (void)loadHTML:(NSString*)html forURL:(const GURL&)url;

// Extracts navigation info from WKNavigationAction and sets it as a pending.
// Some pieces of navigation information are only known in
// |decidePolicyForNavigationAction|, but must be in a pending state until
// |didgo/Navigation| where it becames current.
- (void)updatePendingNavigationInfoFromNavigationAction:
    (WKNavigationAction*)action;
// Extracts navigation info from WKNavigationResponse and sets it as a pending.
// Some pieces of navigation information are only known in
// |decidePolicyForNavigationResponse|, but must be in a pending state until
// |didCommitNavigation| where it becames current.
- (void)updatePendingNavigationInfoFromNavigationResponse:
    (WKNavigationResponse*)response;
// Updates current state with any pending information. Should be called when a
// navigation is committed.
- (void)commitPendingNavigationInfo;
// Returns a NSMutableURLRequest that represents the current NavigationItem.
- (NSMutableURLRequest*)requestForCurrentNavigationItem;
// Returns the WKBackForwardListItemHolder for the current navigation item.
- (web::WKBackForwardListItemHolder*)currentBackForwardListItemHolder;
// Updates the WKBackForwardListItemHolder navigation item.
- (void)updateCurrentBackForwardListItemHolder;

// Loads the current nativeController in a native view. If a web view is
// present, removes it and swaps in the native view in its place. |context| can
// not be null.
- (void)loadNativeViewWithSuccess:(BOOL)loadSuccess
                navigationContext:(web::NavigationContextImpl*)context;
// Loads the correct HTML page for |error| in a native controller, retrieved
// from the native provider.
- (void)loadErrorInNativeView:(NSError*)error
            navigationContext:(web::NavigationContextImpl*)context;
// YES if the navigation to |url| should be treated as a reload.
- (BOOL)shouldReload:(const GURL&)destinationURL
          transition:(ui::PageTransition)transition;
// Internal implementation of reload. Reloads without notifying the delegate.
// Most callers should use -reload instead.
- (void)reloadInternal;
// Aborts any load for both the web view and web controller.
- (void)abortLoad;
// Updates the internal state and informs the delegate that any outstanding load
// operations are cancelled.
- (void)loadCancelled;
// If YES, the page should be closed if it successfully redirects to a native
// application, for example if a new tab redirects to the App Store.
- (BOOL)shouldClosePageOnNativeApplicationLoad;
// Called following navigation completion to generate final navigation lifecycle
// events. Navigation is considered complete when the document has finished
// loading, or when other page load mechanics are completed on a
// non-document-changing URL change.
- (void)didFinishNavigation:(WKNavigation*)navigation;
// Update the appropriate parts of the model and broadcast to the embedder. This
// may be called multiple times and thus must be idempotent.
- (void)loadCompleteWithSuccess:(BOOL)loadSuccess
                  forNavigation:(WKNavigation*)navigation;
// Called after URL is finished loading and _loadPhase is set to PAGE_LOADED.
- (void)didFinishWithURL:(const GURL&)currentURL loadSuccess:(BOOL)loadSuccess;
// Navigates forwards or backwards by |delta| pages. No-op if delta is out of
// bounds. Reloads if delta is 0.
// TODO(crbug.com/661316): Move this method to NavigationManager.
- (void)goDelta:(int)delta;
// Informs the native controller if web usage is allowed or not.
- (void)setNativeControllerWebUsageEnabled:(BOOL)webUsageEnabled;
// Acts on a single message from the JS object, parsed from JSON into a
// DictionaryValue. Returns NO if the format for the message was invalid.
- (BOOL)respondToMessage:(base::DictionaryValue*)crwMessage
       userIsInteracting:(BOOL)userIsInteracting
               originURL:(const GURL&)originURL;
// Called when web controller receives a new message from the web page.
- (void)didReceiveScriptMessage:(WKScriptMessage*)message;
// Returns a new script which wraps |script| with windowID check so |script| is
// not evaluated on windowID mismatch.
- (NSString*)scriptByAddingWindowIDCheckForScript:(NSString*)script;
// Attempts to handle a script message. Returns YES on success, NO otherwise.
- (BOOL)respondToWKScriptMessage:(WKScriptMessage*)scriptMessage;
// Registers load request with empty referrer and link or client redirect
// transition based on user interaction state. Returns navigation context for
// this request.
- (std::unique_ptr<web::NavigationContextImpl>)registerLoadRequestForURL:
    (const GURL&)URL;
// Prepares web controller and delegates for anticipated page change.
// Allows several methods to invoke webWill/DidAddPendingURL on anticipated page
// change, using the same cached request and calculated transition types.
// Returns navigation context for this request.
- (std::unique_ptr<web::NavigationContextImpl>)
registerLoadRequestForURL:(const GURL&)URL
                 referrer:(const web::Referrer&)referrer
               transition:(ui::PageTransition)transition;
// Updates the HTML5 history state of the page using the current NavigationItem.
// For same-document navigations and navigations affected by
// window.history.[push/replace]State(), the URL and serialized state object
// will be updated to the current NavigationItem's values.  A popState event
// will be triggered for all same-document navigations.  Additionaly, a
// hashchange event will be triggered for same-document navigations where the
// only difference between the current and previous URL is the fragment.
- (void)updateHTML5HistoryState;
// Generates the JavaScript string used to update the UIWebView's URL so that it
// matches the URL displayed in the omnibox and sets window.history.state to
// stateObject. Needed for history.pushState() and history.replaceState().
- (NSString*)javaScriptToReplaceWebViewURL:(const GURL&)URL
                           stateObjectJSON:(NSString*)stateObject;
// Generates the JavaScript string used to manually dispatch a popstate event,
// using |stateObjectJSON| as the event parameter.
- (NSString*)javaScriptToDispatchPopStateWithObject:(NSString*)stateObjectJSON;
// Generates the JavaScript string used to manually dispatch a hashchange event,
// using |oldURL| and |newURL| as the event parameters.
- (NSString*)javaScriptToDispatchHashChangeWithOldURL:(const GURL&)oldURL
                                               newURL:(const GURL&)newURL;
// Injects JavaScript to update the URL and state object of the webview to the
// values found in the current NavigationItem.  A hashchange event will be
// dispatched if |dispatchHashChange| is YES, and a popstate event will be
// dispatched if |sameDocument| is YES.  Upon the script's completion, resets
// |urlOnStartLoading_| and |_lastRegisteredRequestURL| to the current
// NavigationItem's URL.  This is necessary so that sites that depend on URL
// params/fragments continue to work correctly and that checks for the URL don't
// incorrectly trigger |-webPageChanged| calls.
- (void)injectHTML5HistoryScriptWithHashChange:(BOOL)dispatchHashChange
                        sameDocumentNavigation:(BOOL)sameDocumentNavigation;

- (BOOL)isLoaded;
// Extracts the current page's viewport tag information and calls |completion|.
// If the page has changed before the viewport tag is successfully extracted,
// |completion| is called with nullptr.
typedef void (^ViewportStateCompletion)(const web::PageViewportState*);
- (void)extractViewportTagWithCompletion:(ViewportStateCompletion)completion;
// Called by NSNotificationCenter upon orientation changes.
- (void)orientationDidChange;
// Queries the web view for the user-scalable meta tag and calls
// |-applyPageDisplayState:userScalable:| with the result.
- (void)applyPageDisplayState:(const web::PageDisplayState&)displayState;
// Restores state of the web view's scroll view from |scrollState|.
// |isUserScalable| represents the value of user-scalable meta tag.
- (void)applyPageDisplayState:(const web::PageDisplayState&)displayState
                 userScalable:(BOOL)isUserScalable;
// Calls the zoom-preparation UIScrollViewDelegate callbacks on the web view.
// This is called before |-applyWebViewScrollZoomScaleFromScrollState:|.
- (void)prepareToApplyWebViewScrollZoomScale;
// Calls the zoom-completion UIScrollViewDelegate callbacks on the web view.
// This is called after |-applyWebViewScrollZoomScaleFromScrollState:|.
- (void)finishApplyingWebViewScrollZoomScale;
// Sets zoom scale value for webview scroll view from |zoomState|.
- (void)applyWebViewScrollZoomScaleFromZoomState:
    (const web::PageZoomState&)zoomState;
// Sets scroll offset value for webview scroll view from |scrollState|.
- (void)applyWebViewScrollOffsetFromScrollState:
    (const web::PageScrollState&)scrollState;
// Returns the referrer for the current page.
- (web::Referrer)currentReferrer;
// Adds a new NavigationItem with the given URL and state object to the history
// stack. A state object is a serialized generic JavaScript object that contains
// details of the UI's state for a given NavigationItem/URL.
// TODO(stuartmorgan): Move the pushState/replaceState logic into
// NavigationManager.
- (void)pushStateWithPageURL:(const GURL&)pageURL
                 stateObject:(NSString*)stateObject
                  transition:(ui::PageTransition)transition;
// Assigns the given URL and state object to the current NavigationItem.
- (void)replaceStateWithPageURL:(const GURL&)pageUrl
                    stateObject:(NSString*)stateObject;
// Sets _documentURL to newURL, and updates any relevant state information.
- (void)setDocumentURL:(const GURL&)newURL;
// Sets last committed NavigationItem's title to the given |title|, which can
// not be nil.
- (void)setNavigationItemTitle:(NSString*)title;
// Returns YES if the current navigation item corresponds to a web page
// loaded by a POST request.
- (BOOL)isCurrentNavigationItemPOST;
// Returns YES if current navigation item is WKNavigationTypeBackForward.
- (BOOL)isCurrentNavigationBackForward;
// Returns whether the given navigation is triggered by a user link click.
- (BOOL)isLinkNavigation:(WKNavigationType)navigationType;

// Returns YES if the given WKBackForwardListItem is valid to use for
// navigation.
- (BOOL)isBackForwardListItemValid:(WKBackForwardListItem*)item;
// Finds all the scrollviews in the view hierarchy and makes sure they do not
// interfere with scroll to top when tapping the statusbar.
- (void)optOutScrollsToTopForSubviews;
// Tears down the old native controller, and then replaces it with the new one.
- (void)setNativeController:(id<CRWNativeContent>)nativeController;
// Returns whether |url| should be opened.
- (BOOL)shouldOpenURL:(const GURL&)url
      mainDocumentURL:(const GURL&)mainDocumentURL
          linkClicked:(BOOL)linkClicked;
// Called when |URL| needs to be opened in a matching native app.
// Returns YES if the url was succesfully opened in the native app.
- (BOOL)urlTriggersNativeAppLaunch:(const GURL&)URL
                         sourceURL:(const GURL&)sourceURL
           linkActivatedNavigation:(BOOL)linkActivatedNavigation;
// Returns YES if the navigation action is associated with a main frame request.
- (BOOL)isMainFrameNavigationAction:(WKNavigationAction*)action;
// Returns whether external URL navigation action should be opened.
- (BOOL)shouldOpenExternalURLForNavigationAction:(WKNavigationAction*)action;
// Returns the header height.
- (CGFloat)headerHeight;
// Updates SSL status for the current navigation item based on the information
// provided by web view.
- (void)updateSSLStatusForCurrentNavigationItem;
// Called when a load ends in an SSL error and certificate chain.
- (void)handleSSLCertError:(NSError*)error forNavigation:navigation;

// Used in webView:didReceiveAuthenticationChallenge:completionHandler: to
// reply with NSURLSessionAuthChallengeDisposition and credentials.
- (void)processAuthChallenge:(NSURLAuthenticationChallenge*)challenge
         forCertAcceptPolicy:(web::CertAcceptPolicy)policy
                  certStatus:(net::CertStatus)certStatus
           completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,
                                       NSURLCredential*))completionHandler;
// Used in webView:didReceiveAuthenticationChallenge:completionHandler: to reply
// with NSURLSessionAuthChallengeDisposition and credentials.
- (void)handleHTTPAuthForChallenge:(NSURLAuthenticationChallenge*)challenge
                 completionHandler:
                     (void (^)(NSURLSessionAuthChallengeDisposition,
                               NSURLCredential*))completionHandler;
// Used in webView:didReceiveAuthenticationChallenge:completionHandler: to reply
// with NSURLSessionAuthChallengeDisposition and credentials.
+ (void)processHTTPAuthForUser:(NSString*)user
                      password:(NSString*)password
             completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,
                                         NSURLCredential*))completionHandler;

// Helper to respond to |webView:runJavaScript...| delegate methods.
// |completionHandler| must not be nil.
- (void)runJavaScriptDialogOfType:(web::JavaScriptDialogType)type
                 initiatedByFrame:(WKFrameInfo*)frame
                          message:(NSString*)message
                      defaultText:(NSString*)defaultText
                       completion:(void (^)(BOOL, NSString*))completionHandler;

// Called when WKWebView estimatedProgress has been changed.
- (void)webViewEstimatedProgressDidChange;
// Called when WKWebView certificateChain or hasOnlySecureContent property has
// changed.
- (void)webViewSecurityFeaturesDidChange;
// Called when WKWebView loading state has been changed.
- (void)webViewLoadingStateDidChange;
// Called when WKWebView title has been changed.
- (void)webViewTitleDidChange;
// Called when WKWebView URL has been changed.
- (void)webViewURLDidChange;
// Returns YES if a KVO change to |newURL| could be a 'navigation' within the
// document (hash change, pushState/replaceState, etc.). This should only be
// used in the context of a URL KVO callback firing, and only if |isLoading| is
// YES for the web view (since if it's not, no guesswork is needed).
- (BOOL)isKVOChangePotentialSameDocumentNavigationToURL:(const GURL&)newURL;
// Called when a non-document-changing URL change occurs. Updates the
// _documentURL, and informs the superclass of the change.
- (void)URLDidChangeWithoutDocumentChange:(const GURL&)URL;
// Returns context for pending navigation that has |URL|. null if there is no
// matching pending navigation.
- (web::NavigationContextImpl*)contextForPendingNavigationWithURL:
    (const GURL&)URL;
// Loads request for the URL of the current navigation item. Subclasses may
// choose to build a new NSURLRequest and call |loadRequest| on the underlying
// web view, or use native web view navigation where possible (for example,
// going back and forward through the history stack).
- (void)loadRequestForCurrentNavigationItem;
// Reports Navigation.IOSWKWebViewSlowFastBackForward UMA. No-op if pending
// navigation is not back forward navigation.
- (void)reportBackForwardNavigationTypeForFastNavigation:(BOOL)isFast;

// Handlers for JavaScript messages. |message| contains a JavaScript command and
// data relevant to the message, and |context| contains contextual information
// about web view state needed for some handlers.

// Handles 'addPluginPlaceholders' message.
- (BOOL)handleAddPluginPlaceholdersMessage:(base::DictionaryValue*)message
                                   context:(NSDictionary*)context;
// Handles 'chrome.send' message.
- (BOOL)handleChromeSendMessage:(base::DictionaryValue*)message
                        context:(NSDictionary*)context;
// Handles 'console' message.
- (BOOL)handleConsoleMessage:(base::DictionaryValue*)message
                     context:(NSDictionary*)context;
// Handles 'document.favicons' message.
- (BOOL)handleDocumentFaviconsMessage:(base::DictionaryValue*)message
                              context:(NSDictionary*)context;
// Handles 'document.submit' message.
- (BOOL)handleDocumentSubmitMessage:(base::DictionaryValue*)message
                            context:(NSDictionary*)context;
// Handles 'form.activity' message.
- (BOOL)handleFormActivityMessage:(base::DictionaryValue*)message
                          context:(NSDictionary*)context;
// Handles 'window.error' message.
- (BOOL)handleWindowErrorMessage:(base::DictionaryValue*)message
                         context:(NSDictionary*)context;
// Handles 'window.hashchange' message.
- (BOOL)handleWindowHashChangeMessage:(base::DictionaryValue*)message
                              context:(NSDictionary*)context;
// Handles 'window.history.back' message.
- (BOOL)handleWindowHistoryBackMessage:(base::DictionaryValue*)message
                               context:(NSDictionary*)context;
// Handles 'window.history.forward' message.
- (BOOL)handleWindowHistoryForwardMessage:(base::DictionaryValue*)message
                                  context:(NSDictionary*)context;
// Handles 'window.history.go' message.
- (BOOL)handleWindowHistoryGoMessage:(base::DictionaryValue*)message
                             context:(NSDictionary*)context;
// Handles 'window.history.willChangeState' message.
- (BOOL)handleWindowHistoryWillChangeStateMessage:
            (base::DictionaryValue*)message
                                          context:(NSDictionary*)context;
// Handles 'window.history.didPushState' message.
- (BOOL)handleWindowHistoryDidPushStateMessage:(base::DictionaryValue*)message
                                       context:(NSDictionary*)context;

// Handles 'window.history.didReplaceState' message.
- (BOOL)handleWindowHistoryDidReplaceStateMessage:
            (base::DictionaryValue*)message
                                          context:(NSDictionary*)context;

// Caches request POST data in the given session entry.
- (void)cachePOSTDataForRequest:(NSURLRequest*)request
               inNavigationItem:(web::NavigationItemImpl*)item;

// Returns YES if the given |action| should be allowed to continue.
// If this returns NO, the load should be cancelled.
- (BOOL)shouldAllowLoadWithNavigationAction:(WKNavigationAction*)action;
// Called when a load ends in an error.
// TODO(stuartmorgan): Figure out if there's actually enough shared logic that
// this makes sense. At the very least remove inMainFrame since that only makes
// sense for UIWebView.
- (void)handleLoadError:(NSError*)error
            inMainFrame:(BOOL)inMainFrame
          forNavigation:(WKNavigation*)navigation;

// Handles cancelled load in WKWebView (error with NSURLErrorCancelled code).
- (void)handleCancelledError:(NSError*)error;

// Used to decide whether a load that generates errors with the
// NSURLErrorCancelled code should be cancelled.
- (BOOL)shouldCancelLoadForCancelledError:(NSError*)error;

// Sets up WebUI for URL.
- (void)createWebUIForURL:(const GURL&)URL;
// Clears WebUI, if one exists.
- (void)clearWebUI;

@end

namespace {

NSString* const kReferrerHeaderName = @"Referer";  // [sic]

// The duration of the period following a screen touch during which the user is
// still considered to be interacting with the page.
const NSTimeInterval kMaximumDelayForUserInteractionInSeconds = 2;

// URLs that are fed into UIWebView as history push/replace get escaped,
// potentially changing their format. Code that attempts to determine whether a
// URL hasn't changed can be confused by those differences though, so method
// will round-trip a URL through the escaping process so that it can be adjusted
// pre-storing, to allow later comparisons to work as expected.
GURL URLEscapedForHistory(const GURL& url) {
  // TODO(stuartmorgan): This is a very large hammer; see if limited unicode
  // escaping would be sufficient.
  return net::GURLWithNSURL(net::NSURLWithGURL(url));
}

// Leave snapshot overlay up unless page loads.
const NSTimeInterval kSnapshotOverlayDelay = 1.5;
// Transition to fade snapshot overlay.
const NSTimeInterval kSnapshotOverlayTransition = 0.5;

}  // namespace

@implementation CRWWebController

@synthesize webUsageEnabled = _webUsageEnabled;
@synthesize usePlaceholderOverlay = _usePlaceholderOverlay;
@synthesize loadPhase = _loadPhase;
@synthesize shouldSuppressDialogs = _shouldSuppressDialogs;

- (instancetype)initWithWebState:(WebStateImpl*)webState {
  self = [super init];
  if (self) {
    _webStateImpl = webState;
    DCHECK(_webStateImpl);
    // Load phase when no WebView present is 'loaded' because this represents
    // the idle state.
    _loadPhase = web::PAGE_LOADED;
    // Content area is lazily instantiated.
    _defaultURL = GURL(url::kAboutBlankURL);
    _jsInjectionReceiver.reset(
        [[CRWJSInjectionReceiver alloc] initWithEvaluator:self]);
    _webViewProxy.reset(
        [[CRWWebViewProxyImpl alloc] initWithWebController:self]);
    [[_webViewProxy scrollViewProxy] addObserver:self];
    _gestureRecognizers.reset([[NSMutableArray alloc] init]);
    _webViewToolbars.reset([[NSMutableArray alloc] init]);
    _pendingLoadCompleteActions.reset([[NSMutableArray alloc] init]);
    web::BrowserState* browserState = _webStateImpl->GetBrowserState();
    _certVerificationController.reset([[CRWCertVerificationController alloc]
        initWithBrowserState:browserState]);
    _certVerificationErrors.reset(
        new CertVerificationErrorsCacheType(kMaxCertErrorsCount));
    _navigationStates.reset([[CRWWKNavigationStates alloc] init]);
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(orientationDidChange)
               name:UIApplicationDidChangeStatusBarOrientationNotification
             object:nil];
  }
  return self;
}

- (id<CRWNativeContentProvider>)nativeProvider {
  return _nativeProvider.get();
}

- (void)setNativeProvider:(id<CRWNativeContentProvider>)nativeProvider {
  _nativeProvider.reset(nativeProvider);
}

- (id<CRWSwipeRecognizerProvider>)swipeRecognizerProvider {
  return _swipeRecognizerProvider.get();
}

- (void)setSwipeRecognizerProvider:
    (id<CRWSwipeRecognizerProvider>)swipeRecognizerProvider {
  _swipeRecognizerProvider.reset(swipeRecognizerProvider);
}

- (WebState*)webState {
  return _webStateImpl;
}

- (WebStateImpl*)webStateImpl {
  return _webStateImpl;
}

- (void)clearTransientContentView {
  // Early return if there is no transient content view.
  if (![_containerView transientContentView])
    return;

  // Remove the transient content view from the hierarchy.
  [_containerView clearTransientContentView];

  // Notify the WebState so it can perform any required state cleanup.
  if (_webStateImpl)
    _webStateImpl->ClearTransientContentView();
}

- (void)showTransientContentView:(CRWContentView*)contentView {
  DCHECK(contentView);
  DCHECK(contentView.scrollView);
  // TODO(crbug.com/556848) Reenable DCHECK when |CRWWebControllerContainerView|
  // is restructured so that subviews are not added during |layoutSubviews|.
  // DCHECK([contentView.scrollView isDescendantOfView:contentView]);
  [_containerView displayTransientContent:contentView];
}

- (id<CRWWebDelegate>)delegate {
  return _delegate.get();
}

- (void)setDelegate:(id<CRWWebDelegate>)delegate {
  _delegate.reset(delegate);
  if ([self.nativeController respondsToSelector:@selector(setDelegate:)])
    [self.nativeController setDelegate:self];
}

- (void)dealloc {
  DCHECK([NSThread isMainThread]);
  DCHECK(_isBeingDestroyed);  // 'close' must have been called already.
  DCHECK(!_webView);
  [super dealloc];
}

- (void)dismissKeyboard {
  [_webView endEditing:YES];
  if ([self.nativeController respondsToSelector:@selector(dismissKeyboard)])
    [self.nativeController dismissKeyboard];
}

- (id<CRWNativeContent>)nativeController {
  return [_containerView nativeController];
}

- (void)setNativeController:(id<CRWNativeContent>)nativeController {
  // Check for pointer equality.
  if (self.nativeController == nativeController)
    return;

  // Unset the delegate on the previous instance.
  if ([self.nativeController respondsToSelector:@selector(setDelegate:)])
    [self.nativeController setDelegate:nil];

  [_containerView displayNativeContent:nativeController];
  [self setNativeControllerWebUsageEnabled:_webUsageEnabled];
}

- (NSDictionary*)WKWebViewObservers {
  NSMutableDictionary* result = [NSMutableDictionary dictionary];
  if (base::ios::IsRunningOnIOS10OrLater()) {
    result[@"serverTrust"] = @"webViewSecurityFeaturesDidChange";
  } else {
    result[@"certificateChain"] = @"webViewSecurityFeaturesDidChange";
  }

  [result addEntriesFromDictionary:@{
    @"estimatedProgress" : @"webViewEstimatedProgressDidChange",
    @"hasOnlySecureContent" : @"webViewSecurityFeaturesDidChange",
    @"loading" : @"webViewLoadingStateDidChange",
    @"title" : @"webViewTitleDidChange",
    @"URL" : @"webViewURLDidChange",
  }];

  return result;
}

// NativeControllerDelegate method, called to inform that title has changed.
- (void)nativeContent:(id)content titleDidChange:(NSString*)title {
  [self setNavigationItemTitle:title];
}

- (void)setNativeControllerWebUsageEnabled:(BOOL)webUsageEnabled {
  if ([self.nativeController
          respondsToSelector:@selector(setWebUsageEnabled:)]) {
    [self.nativeController setWebUsageEnabled:webUsageEnabled];
  }
}

- (void)setWebUsageEnabled:(BOOL)enabled {
  if (_webUsageEnabled == enabled)
    return;
  _webUsageEnabled = enabled;

  // WKWebView autoreleases its WKProcessPool on removal from superview.
  // Deferring WKProcessPool deallocation may lead to issues with cookie
  // clearing and and Browsing Data Partitioning implementation.
  @autoreleasepool {
    [self setNativeControllerWebUsageEnabled:_webUsageEnabled];
    if (enabled) {
      // Don't create the web view; let it be lazy created as needed.
    } else {
      [self clearTransientContentView];
      [self removeWebViewAllowingCachedReconstruction:YES];
      _touchTrackingRecognizer.get().touchTrackingDelegate = nil;
      _touchTrackingRecognizer.reset();
      [self resetContainerView];
    }
  }
}

- (void)requirePageReconstruction {
  [self removeWebViewAllowingCachedReconstruction:NO];
}

- (void)requirePageReload {
  _requireReloadOnDisplay = YES;
}

- (void)resetContainerView {
  [_containerView removeFromSuperview];
  _containerView.reset();
}

- (void)handleLowMemory {
  [self removeWebViewAllowingCachedReconstruction:YES];
  _touchTrackingRecognizer.get().touchTrackingDelegate = nil;
  _touchTrackingRecognizer.reset();
  [self resetContainerView];
  _usePlaceholderOverlay = YES;
}

- (void)reinitializeWebViewAndReload:(BOOL)reload {
  if (_webView) {
    [self removeWebViewAllowingCachedReconstruction:NO];
    if (reload) {
      [self loadCurrentURLInWebView];
    } else {
      // Clear the space for the web view to lazy load when needed.
      _usePlaceholderOverlay = YES;
      _touchTrackingRecognizer.get().touchTrackingDelegate = nil;
      _touchTrackingRecognizer.reset();
      [self resetContainerView];
    }
  }
}

- (BOOL)isViewAlive {
  return !_webProcessIsDead && [_containerView isViewAlive];
}

- (BOOL)contentIsHTML {
  if (!_webView)
    return NO;

  std::string MIMEType = self.webState->GetContentsMimeType();
  return MIMEType == "text/html" || MIMEType == "application/xhtml+xml" ||
         MIMEType == "application/xml";
}

// Stop doing stuff, especially network stuff. Close the request tracker.
- (void)terminateNetworkActivity {
  DCHECK(!_isHalted);
  _isHalted = YES;

  // Cancel all outstanding perform requests, and clear anything already queued
  // (since this may be called from within the handling loop) to prevent any
  // asynchronous JavaScript invocation handling from continuing.
  [NSObject cancelPreviousPerformRequestsWithTarget:self];
}

- (void)dismissModals {
  if ([self.nativeController respondsToSelector:@selector(dismissModals)])
    [self.nativeController dismissModals];
}

// Caller must reset the delegate before calling.
- (void)close {
  _webStateImpl->CancelDialogs();

  _SSLStatusUpdater.reset();

  self.nativeProvider = nil;
  self.swipeRecognizerProvider = nil;
  if ([self.nativeController respondsToSelector:@selector(close)])
    [self.nativeController close];

  base::scoped_nsobject<NSSet> observers([_observers copy]);
  for (id it in observers.get()) {
    if ([it respondsToSelector:@selector(webControllerWillClose:)])
      [it webControllerWillClose:self];
  }

  if (!_isHalted) {
    [self terminateNetworkActivity];
  }

  DCHECK(!_isBeingDestroyed);
  DCHECK(!_delegate);  // Delegate should reset its association before closing.
  // Mark the destruction sequence has started, in case someone else holds a
  // strong reference and tries to continue using the tab.
  _isBeingDestroyed = YES;

  // Remove the web view now. Otherwise, delegate callbacks occur.
  [self removeWebViewAllowingCachedReconstruction:NO];

  _webStateImpl = nullptr;

  DCHECK(!_webView);
  // TODO(crbug.com/662860): Don't set the delegate to nil.
  [_containerView setDelegate:nil];
  if ([self.nativeController respondsToSelector:@selector(setDelegate:)]) {
    [self.nativeController setDelegate:nil];
  }
  _touchTrackingRecognizer.get().touchTrackingDelegate = nil;
  [[_webViewProxy scrollViewProxy] removeObserver:self];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

// TODO(crbug.com/661642): This code is shared with SnapshotManager. Remove this
// and add it as part of WebDelegate delegate API such that a default image is
// returned immediately.
+ (UIImage*)defaultSnapshotImage {
  static UIImage* defaultImage = nil;

  if (!defaultImage) {
    CGRect frame = CGRectMake(0, 0, 2, 2);
    UIGraphicsBeginImageContext(frame.size);
    [[UIColor whiteColor] setFill];
    CGContextFillRect(UIGraphicsGetCurrentContext(), frame);

    UIImage* result = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    defaultImage =
        [[result stretchableImageWithLeftCapWidth:1 topCapHeight:1] retain];
  }
  return defaultImage;
}

- (CGPoint)scrollPosition {
  CGPoint position = CGPointMake(0.0, 0.0);
  if (!self.webScrollView)
    return position;
  return self.webScrollView.contentOffset;
}

- (BOOL)atTop {
  if (!_webView)
    return YES;
  UIScrollView* scrollView = self.webScrollView;
  return scrollView.contentOffset.y == -scrollView.contentInset.top;
}

- (GURL)currentURLWithTrustLevel:(web::URLVerificationTrustLevel*)trustLevel {
  DCHECK(trustLevel) << "Verification of the trustLevel state is mandatory";
  if (_webView) {
    GURL url([self webURLWithTrustLevel:trustLevel]);
    // Web views treat all about: URLs as the same origin, which makes it
    // possible for pages to document.write into about:<foo> pages, where <foo>
    // can be something misleading. Report any about: URL as about:blank to
    // prevent that. See crbug.com/326118
    if (url.scheme() == url::kAboutScheme)
      return GURL(url::kAboutBlankURL);
    return url;
  }
  // Any non-web URL source is trusted.
  *trustLevel = web::URLVerificationTrustLevel::kAbsolute;
  if (self.nativeController) {
    if ([self.nativeController respondsToSelector:@selector(virtualURL)]) {
      return [self.nativeController virtualURL];
    } else {
      return [self.nativeController url];
    }
  }
  web::NavigationItem* item = self.currentNavItem;
  return item ? item->GetVirtualURL() : GURL::EmptyGURL();
}

- (WKWebView*)webView {
  return _webView.get();
}

- (UIScrollView*)webScrollView {
  return [_webView scrollView];
}

- (GURL)currentURL {
  web::URLVerificationTrustLevel trustLevel =
      web::URLVerificationTrustLevel::kNone;
  return [self currentURLWithTrustLevel:&trustLevel];
}

- (web::Referrer)currentReferrer {
  // Referrer string doesn't include the fragment, so in cases where the
  // previous URL is equal to the current referrer plus the fragment the
  // previous URL is returned as current referrer.
  NSString* referrerString = _currentReferrerString;

  // In case of an error evaluating the JavaScript simply return empty string.
  if ([referrerString length] == 0)
    return web::Referrer();

  web::NavigationItem* item = self.currentNavItem;
  GURL navigationURL = item ? item->GetVirtualURL() : GURL::EmptyGURL();
  NSString* previousURLString = base::SysUTF8ToNSString(navigationURL.spec());
  // Check if the referrer is equal to the previous URL minus the hash symbol.
  // L'#' is used to convert the char '#' to a unichar.
  if ([previousURLString length] > [referrerString length] &&
      [previousURLString hasPrefix:referrerString] &&
      [previousURLString characterAtIndex:[referrerString length]] == L'#') {
    referrerString = previousURLString;
  }
  // Since referrer is being extracted from the destination page, the correct
  // policy from the origin has *already* been applied. Since the extracted URL
  // is the post-policy value, and the source policy is no longer available,
  // the policy is set to Always so that whatever WebKit decided to send will be
  // re-sent when replaying the entry.
  // TODO(stuartmorgan): When possible, get the real referrer and policy in
  // advance and use that instead. https://crbug.com/227769.
  return web::Referrer(GURL(base::SysNSStringToUTF8(referrerString)),
                       web::ReferrerPolicyAlways);
}

- (void)pushStateWithPageURL:(const GURL&)pageURL
                 stateObject:(NSString*)stateObject
                  transition:(ui::PageTransition)transition {
  std::unique_ptr<web::NavigationContextImpl> context =
      web::NavigationContextImpl::CreateNavigationContext(_webStateImpl,
                                                          pageURL);
  _webStateImpl->OnNavigationStarted(context.get());
  [[self sessionController] pushNewItemWithURL:pageURL
                                   stateObject:stateObject
                                    transition:transition];
  context->SetIsSameDocument(true);
  _webStateImpl->OnNavigationFinished(context.get());
  self.userInteractionRegistered = NO;
}

- (void)replaceStateWithPageURL:(const GURL&)pageURL
                    stateObject:(NSString*)stateObject {
  std::unique_ptr<web::NavigationContextImpl> context =
      web::NavigationContextImpl::CreateNavigationContext(_webStateImpl,
                                                          pageURL);
  _webStateImpl->OnNavigationStarted(context.get());
  [[self sessionController] updateCurrentItemWithURL:pageURL
                                         stateObject:stateObject];
  context->SetIsSameDocument(true);
  _webStateImpl->OnNavigationFinished(context.get());
}

- (void)setDocumentURL:(const GURL&)newURL {
  if (newURL != _documentURL && newURL.is_valid()) {
    _documentURL = newURL;
    _interactionRegisteredSinceLastURLChange = NO;
  }
}

- (void)setNavigationItemTitle:(NSString*)title {
  DCHECK(title);
  web::NavigationItem* item =
      self.navigationManagerImpl->GetLastCommittedItem();
  if (!item)
    return;

  base::string16 newTitle = base::SysNSStringToUTF16(title);
  if (item->GetTitle() == newTitle)
    return;

  item->SetTitle(newTitle);
  // TODO(crbug.com/546218): See if this can be removed; it's not clear that
  // other platforms send this (tab sync triggers need to be compared against
  // upstream).
  self.navigationManagerImpl->OnNavigationItemChanged();
  _webStateImpl->OnTitleChanged();
}

- (BOOL)isCurrentNavigationItemPOST {
  // |_pendingNavigationInfo| will be nil if the decidePolicy* delegate methods
  // were not called.
  NSString* HTTPMethod =
      _pendingNavigationInfo
          ? [_pendingNavigationInfo HTTPMethod]
          : [self currentBackForwardListItemHolder]->http_method();
  return [HTTPMethod isEqual:@"POST"];
}

- (BOOL)isCurrentNavigationBackForward {
  if (!self.currentNavItem)
    return NO;
  WKNavigationType currentNavigationType =
      [self currentBackForwardListItemHolder]->navigation_type();
  return currentNavigationType == WKNavigationTypeBackForward;
}

- (BOOL)isLinkNavigation:(WKNavigationType)navigationType {
  switch (navigationType) {
    case WKNavigationTypeLinkActivated:
      return YES;
    case WKNavigationTypeOther:
      // Sometimes link navigation is not detected by navigation type, so
      // check last user interaction with the page in the recent time.
      return [self userClickedRecently];
    default:
      return NO;
  }
}

- (BOOL)isBackForwardListItemValid:(WKBackForwardListItem*)item {
  // The current back-forward list item MUST be in the WKWebView's back-forward
  // list to be valid.
  WKBackForwardList* list = [_webView backForwardList];
  return list.currentItem == item ||
         [list.forwardList indexOfObject:item] != NSNotFound ||
         [list.backList indexOfObject:item] != NSNotFound;
}

- (BOOL)canUseViewForGeneratingOverlayPlaceholderView {
  return _containerView != nil;
}

- (UIView*)view {
  // Kick off the process of lazily creating the view and starting the load if
  // necessary; this creates _containerView if it doesn't exist.
  [self triggerPendingLoad];
  DCHECK(_containerView);
  return _containerView;
}

- (id<CRWWebViewProxy>)webViewProxy {
  return _webViewProxy.get();
}

- (UIView*)viewForPrinting {
  // Printing is not supported for native controllers.
  return _webView;
}

- (double)loadingProgress {
  return [_webView estimatedProgress];
}

- (std::unique_ptr<web::NavigationContextImpl>)registerLoadRequestForURL:
    (const GURL&)URL {
  // Get the navigation type from the last main frame load request, and try to
  // map that to a PageTransition.
  WKNavigationType navigationType =
      _pendingNavigationInfo ? [_pendingNavigationInfo navigationType]
                             : WKNavigationTypeOther;
  ui::PageTransition transition = ui::PAGE_TRANSITION_CLIENT_REDIRECT;
  switch (navigationType) {
    case WKNavigationTypeLinkActivated:
      transition = ui::PAGE_TRANSITION_LINK;
      break;
    case WKNavigationTypeFormSubmitted:
    case WKNavigationTypeFormResubmitted:
      transition = ui::PAGE_TRANSITION_FORM_SUBMIT;
      break;
    case WKNavigationTypeBackForward:
      transition = ui::PAGE_TRANSITION_FORWARD_BACK;
      break;
    case WKNavigationTypeReload:
      transition = ui::PAGE_TRANSITION_RELOAD;
      break;
    case WKNavigationTypeOther:
      // The "Other" type covers a variety of very different cases, which may
      // or may not be the result of user actions. For now, guess based on
      // whether there's been an interaction since the last URL change.
      // TODO(crbug.com/549301): See if this heuristic can be improved.
      transition = _interactionRegisteredSinceLastURLChange
                       ? ui::PAGE_TRANSITION_LINK
                       : ui::PAGE_TRANSITION_CLIENT_REDIRECT;
      break;
  }
  // The referrer is not known yet, and will be updated later.
  const web::Referrer emptyReferrer;
  return [self registerLoadRequestForURL:URL
                                referrer:emptyReferrer
                              transition:transition];
}

- (std::unique_ptr<web::NavigationContextImpl>)
registerLoadRequestForURL:(const GURL&)requestURL
                 referrer:(const web::Referrer&)referrer
               transition:(ui::PageTransition)transition {
  // Transfer time is registered so that further transitions within the time
  // envelope are not also registered as links.
  _lastTransferTimeInSeconds = CFAbsoluteTimeGetCurrent();
  bool redirect = transition & ui::PAGE_TRANSITION_IS_REDIRECT_MASK;
  if (!redirect) {
    // Before changing phases, the delegate should be informed that any existing
    // request is being cancelled before completion.
    [self loadCancelled];
    DCHECK(_loadPhase == web::PAGE_LOADED);
  }

  _loadPhase = web::LOAD_REQUESTED;
  _lastRegisteredRequestURL = requestURL;

  if (!redirect) {
    // Record state of outgoing page.
    [self recordStateInHistory];
  }

  [_delegate webWillAddPendingURL:requestURL transition:transition];
  // Add or update pending url.
  if (self.navigationManagerImpl->GetPendingItem()) {
    // Update the existing pending entry.
    // Typically on PAGE_TRANSITION_CLIENT_REDIRECT.
    [[self sessionController] updatePendingItem:requestURL];
  } else {
    // A new session history entry needs to be created.
    self.navigationManagerImpl->AddPendingItem(
        requestURL, referrer, transition,
        web::NavigationInitiationType::RENDERER_INITIATED,
        web::NavigationManager::UserAgentOverrideOption::INHERIT);
  }
  std::unique_ptr<web::NavigationContextImpl> context =
      web::NavigationContextImpl::CreateNavigationContext(_webStateImpl,
                                                          requestURL);

  web::NavigationItem* item = self.navigationManagerImpl->GetPendingItem();
  // TODO(crbug.com/676129): AddPendingItem does not always create a pending
  // item. Remove this workaround once the bug is fixed.
  if (!item) {
    item = self.navigationManagerImpl->GetLastCommittedItem();
  }
  context->SetNavigationItemUniqueID(item->GetUniqueID());
  _webStateImpl->SetIsLoading(true);
  _webStateImpl->OnNavigationStarted(context.get());
  return context;
}

- (void)updateHTML5HistoryState {
  web::NavigationItemImpl* currentItem = self.currentNavItem;
  if (!currentItem)
    return;

  // Same-document navigations must trigger a popState event.
  CRWSessionController* sessionController = self.sessionController;
  BOOL sameDocumentNavigation = [sessionController
      isSameDocumentNavigationBetweenItem:sessionController.currentItem
                                  andItem:sessionController.previousItem];
  // WKWebView doesn't send hashchange events for same-document non-BFLI
  // navigations, so one must be dispatched manually for hash change same-
  // document navigations.
  const GURL URL = currentItem->GetURL();
  web::NavigationItem* previousItem = self.sessionController.previousItem;
  const GURL oldURL = previousItem ? previousItem->GetURL() : GURL();
  BOOL shouldDispatchHashchange = sameDocumentNavigation && previousItem &&
                                  (web::GURLByRemovingRefFromGURL(URL) ==
                                   web::GURLByRemovingRefFromGURL(oldURL));
  // The URL and state object must be set for same-document navigations and
  // NavigationItems that were created or updated by calls to pushState() or
  // replaceState().
  BOOL shouldUpdateState = sameDocumentNavigation ||
                           currentItem->IsCreatedFromPushState() ||
                           currentItem->HasStateBeenReplaced();
  if (!shouldUpdateState)
    return;

  // TODO(stuartmorgan): Make CRWSessionController manage this internally (or
  // remove it; it's not clear this matches other platforms' behavior).
  self.navigationManagerImpl->OnNavigationItemCommitted();
  // Record that a same-document hashchange event will be fired.  This flag will
  // be reset when resonding to the hashchange message.  Note that resetting the
  // flag in the completion block below is too early, as that block is called
  // before hashchange event listeners have a chance to fire.
  _dispatchingSameDocumentHashChangeEvent = shouldDispatchHashchange;
  // Inject the JavaScript to update the state on the browser side.
  [self injectHTML5HistoryScriptWithHashChange:shouldDispatchHashchange
                        sameDocumentNavigation:sameDocumentNavigation];
}

- (NSString*)javaScriptToReplaceWebViewURL:(const GURL&)URL
                           stateObjectJSON:(NSString*)stateObject {
  std::string outURL;
  base::EscapeJSONString(URL.spec(), true, &outURL);
  return
      [NSString stringWithFormat:@"__gCrWeb.replaceWebViewURL(%@, %@);",
                                 base::SysUTF8ToNSString(outURL), stateObject];
}

- (NSString*)javaScriptToDispatchPopStateWithObject:(NSString*)stateObjectJSON {
  std::string outState;
  base::EscapeJSONString(base::SysNSStringToUTF8(stateObjectJSON), true,
                         &outState);
  return [NSString stringWithFormat:@"__gCrWeb.dispatchPopstateEvent(%@);",
                                    base::SysUTF8ToNSString(outState)];
}

- (NSString*)javaScriptToDispatchHashChangeWithOldURL:(const GURL&)oldURL
                                               newURL:(const GURL&)newURL {
  return [NSString
      stringWithFormat:@"__gCrWeb.dispatchHashchangeEvent(\'%s\', \'%s\');",
                       oldURL.spec().c_str(), newURL.spec().c_str()];
}

- (void)injectHTML5HistoryScriptWithHashChange:(BOOL)dispatchHashChange
                        sameDocumentNavigation:(BOOL)sameDocumentNavigation {
  web::NavigationItemImpl* currentItem = self.currentNavItem;
  if (!currentItem)
    return;

  const GURL URL = currentItem->GetURL();
  NSString* stateObject = currentItem->GetSerializedStateObject();
  NSMutableString* script = [NSMutableString
      stringWithString:[self javaScriptToReplaceWebViewURL:URL
                                           stateObjectJSON:stateObject]];
  if (sameDocumentNavigation) {
    [script
        appendString:[self javaScriptToDispatchPopStateWithObject:stateObject]];
  }
  if (dispatchHashChange) {
    web::NavigationItemImpl* previousItem = self.sessionController.previousItem;
    const GURL oldURL = previousItem ? previousItem->GetURL() : GURL();
    [script appendString:[self javaScriptToDispatchHashChangeWithOldURL:oldURL
                                                                 newURL:URL]];
  }
  base::WeakNSObject<CRWWebController> weakSelf(self);
  [self executeJavaScript:script
        completionHandler:^(id, NSError*) {
          if (!weakSelf || weakSelf.get()->_isBeingDestroyed)
            return;
          base::scoped_nsobject<CRWWebController> strongSelf([weakSelf retain]);
          strongSelf.get()->_URLOnStartLoading = URL;
          strongSelf.get()->_lastRegisteredRequestURL = URL;
        }];
}

// Load the current URL in a web view, first ensuring the web view is visible.
- (void)loadCurrentURLInWebView {
  // Clear the set of URLs opened in external applications.
  _openedApplicationURL.reset([[NSMutableSet alloc] init]);

  web::NavigationItem* item = self.currentNavItem;
  GURL targetURL = item ? item->GetVirtualURL() : GURL::EmptyGURL();
  // Load the url. The UIWebView delegate callbacks take care of updating the
  // session history and UI.
  if (!targetURL.is_valid()) {
    [self didFinishWithURL:targetURL loadSuccess:NO];
    return;
  }

  // JavaScript should never be evaluated here. User-entered JS should be
  // evaluated via stringByEvaluatingUserJavaScriptFromString.
  DCHECK(!targetURL.SchemeIs(url::kJavaScriptScheme));

  [self ensureWebViewCreated];

  // Update web view's user agent is called for every load, which may have
  // performance implications because update web view's user agent may
  // potentially send a message to a separate thread. However, this is not an
  // issue for WKWebView because WKWebView's |setCustomUserAgent| is non-op if
  // user agent stays thesame.
  [self updateWebViewUserAgentFromUserAgentType:self.userAgentType];

  [self loadRequestForCurrentNavigationItem];
}

- (void)updatePendingNavigationInfoFromNavigationAction:
    (WKNavigationAction*)action {
  if (action.targetFrame.mainFrame) {
    _pendingNavigationInfo.reset(
        [[CRWWebControllerPendingNavigationInfo alloc] init]);
    [_pendingNavigationInfo
        setReferrer:[self referrerFromNavigationAction:action]];
    [_pendingNavigationInfo setNavigationType:action.navigationType];
    [_pendingNavigationInfo setHTTPMethod:action.request.HTTPMethod];
  }
}

- (void)updatePendingNavigationInfoFromNavigationResponse:
    (WKNavigationResponse*)response {
  if (response.isForMainFrame) {
    if (!_pendingNavigationInfo) {
      _pendingNavigationInfo.reset(
          [[CRWWebControllerPendingNavigationInfo alloc] init]);
    }
    [_pendingNavigationInfo setMIMEType:response.response.MIMEType];
  }
}

- (void)commitPendingNavigationInfo {
  if ([_pendingNavigationInfo referrer]) {
    _currentReferrerString.reset([[_pendingNavigationInfo referrer] copy]);
  }
  if ([_pendingNavigationInfo MIMEType]) {
    self.webStateImpl->SetContentsMimeType(
        base::SysNSStringToUTF8([_pendingNavigationInfo MIMEType]));
  }
  [self updateCurrentBackForwardListItemHolder];

  _pendingNavigationInfo.reset();
}

- (NSMutableURLRequest*)requestForCurrentNavigationItem {
  web::NavigationItem* item = self.currentNavItem;
  const GURL currentNavigationURL =
      item ? item->GetVirtualURL() : GURL::EmptyGURL();
  NSMutableURLRequest* request = [NSMutableURLRequest
      requestWithURL:net::NSURLWithGURL(currentNavigationURL)];
  const web::Referrer referrer(self.currentNavItemReferrer);
  if (referrer.url.is_valid()) {
    std::string referrerValue =
        web::ReferrerHeaderValueForNavigation(currentNavigationURL, referrer);
    if (!referrerValue.empty()) {
      [request setValue:base::SysUTF8ToNSString(referrerValue)
          forHTTPHeaderField:kReferrerHeaderName];
    }
  }

  // If there are headers in the current session entry add them to |request|.
  // Headers that would overwrite fields already present in |request| are
  // skipped.
  NSDictionary* headers = self.currentHTTPHeaders;
  for (NSString* headerName in headers) {
    if (![request valueForHTTPHeaderField:headerName]) {
      [request setValue:[headers objectForKey:headerName]
          forHTTPHeaderField:headerName];
    }
  }

  return request;
}

- (web::WKBackForwardListItemHolder*)currentBackForwardListItemHolder {
  web::NavigationItem* item = self.currentNavItem;
  DCHECK(item);
  web::WKBackForwardListItemHolder* holder =
      web::WKBackForwardListItemHolder::FromNavigationItem(item);
  DCHECK(holder);
  return holder;
}

- (void)updateCurrentBackForwardListItemHolder {
  // WebUI pages (which are loaded via loadHTMLString:baseURL:) have no entry
  // in the back/forward list, so the current item will still be the previous
  // page, and should not be associated.
  if (_webUIManager)
    return;

  web::WKBackForwardListItemHolder* holder =
      [self currentBackForwardListItemHolder];

  WKNavigationType navigationType =
      _pendingNavigationInfo ? [_pendingNavigationInfo navigationType]
                             : WKNavigationTypeOther;
  holder->set_back_forward_list_item([_webView backForwardList].currentItem);
  holder->set_navigation_type(navigationType);
  holder->set_http_method([_pendingNavigationInfo HTTPMethod]);

  // Only update the MIME type in the holder if there was MIME type information
  // as part of this pending load. It will be nil when doing a fast
  // back/forward navigation, for instance, because the callback that would
  // populate it is not called in that flow.
  if ([_pendingNavigationInfo MIMEType])
    holder->set_mime_type([_pendingNavigationInfo MIMEType]);
}

- (void)loadNativeViewWithSuccess:(BOOL)loadSuccess
                navigationContext:(web::NavigationContextImpl*)context {
  const GURL currentURL([self currentURL]);
  [self didStartLoadingURL:currentURL];
  _loadPhase = web::PAGE_LOADED;
  _webStateImpl->OnNavigationFinished(context);

  // Perform post-load-finished updates.
  [self didFinishWithURL:currentURL loadSuccess:loadSuccess];

  NSString* title = [self.nativeController title];
  if (title) {
    [self setNavigationItemTitle:title];
  }

  if ([self.nativeController respondsToSelector:@selector(setDelegate:)]) {
    [self.nativeController setDelegate:self];
  }
}

- (void)loadErrorInNativeView:(NSError*)error
            navigationContext:(web::NavigationContextImpl*)context {
  [self removeWebViewAllowingCachedReconstruction:NO];
  web::NavigationItem* item = self.currentNavItem;
  const GURL currentURL = item ? item->GetVirtualURL() : GURL::EmptyGURL();

  if (web::IsWKWebViewSSLCertError(error)) {
    // This could happen only if certificate is absent or could not be parsed.
    error = web::NetErrorFromError(error, net::ERR_SSL_SERVER_CERT_BAD_FORMAT);
#if defined(DEBUG)
    net::SSLInfo info;
    web::GetSSLInfoFromWKWebViewSSLCertError(error, &info);
    CHECK(!error.cert);
#endif
  } else {
    error = web::NetErrorFromError(error);
  }

  BOOL isPost = [self isCurrentNavigationItemPOST];
  [self setNativeController:[_nativeProvider controllerForURL:currentURL
                                                    withError:error
                                                       isPost:isPost]];
  [self loadNativeViewWithSuccess:NO navigationContext:context];
}

// Loads the current URL in a native controller, retrieved from the native
// provider.
- (void)loadCurrentURLInNativeView {
  // Free the web view.
  [self removeWebViewAllowingCachedReconstruction:NO];

  web::NavigationItem* item = self.currentNavItem;
  const GURL targetURL = item ? item->GetURL() : GURL::EmptyGURL();
  const web::Referrer referrer;
  id<CRWNativeContent> nativeContent =
      [_nativeProvider controllerForURL:targetURL webState:self.webState];
  // Unlike the WebView case, always create a new controller and view.
  // TODO(pinkerton): What to do if this does return nil?
  [self setNativeController:nativeContent];
  if ([nativeContent respondsToSelector:@selector(virtualURL)]) {
    item->SetVirtualURL([nativeContent virtualURL]);
  }

  std::unique_ptr<web::NavigationContextImpl> navigationContext =
      [self registerLoadRequestForURL:targetURL
                             referrer:referrer
                           transition:self.currentTransition];
  [self loadNativeViewWithSuccess:YES
                navigationContext:navigationContext.get()];
}

- (void)loadWithParams:(const NavigationManager::WebLoadParams&)params {
  DCHECK(!(params.transition_type & ui::PAGE_TRANSITION_FORWARD_BACK));

  // Clear transient view before making any changes to history and navigation
  // manager. TODO(stuartmorgan): Drive Transient Item clearing from
  // navigation system, rather than from WebController.
  [self clearTransientContentView];

  [self recordStateInHistory];

  BOOL initialNavigation = !self.currentNavItem;

  web::NavigationInitiationType navigationInitiationType =
      params.is_renderer_initiated
          ? web::NavigationInitiationType::RENDERER_INITIATED
          : web::NavigationInitiationType::USER_INITIATED;
  self.navigationManagerImpl->AddPendingItem(
      params.url, params.referrer, params.transition_type,
      navigationInitiationType, params.user_agent_override_option);

  // Mark pending item as created from hash change if necessary. This is needed
  // because window.hashchange message may not arrive on time.
  web::NavigationItemImpl* pendingItem = self.sessionController.pendingItem;
  if (pendingItem) {
    GURL lastCommittedURL = _webStateImpl->GetLastCommittedURL();
    GURL pendingURL = pendingItem->GetURL();
    if (lastCommittedURL != pendingURL &&
        lastCommittedURL.EqualsIgnoringRef(pendingURL)) {
      pendingItem->SetIsCreatedFromHashChange(true);
    }
  }

  web::NavigationItemImpl* addedItem = self.currentNavItem;
  DCHECK(addedItem);
  if (params.extra_headers)
    addedItem->AddHttpRequestHeaders(params.extra_headers);
  if (params.post_data) {
    DCHECK([addedItem->GetHttpRequestHeaders() objectForKey:@"Content-Type"])
        << "Post data should have an associated content type";
    addedItem->SetPostData(params.post_data);
    addedItem->SetShouldSkipRepostFormConfirmation(true);
  }

  [_delegate webDidUpdateSessionForLoadWithParams:params
                             wasInitialNavigation:initialNavigation];

  [self loadCurrentURL];
}

- (void)loadCurrentURL {
  // If the content view doesn't exist, the tab has either been evicted, or
  // never displayed. Bail, and let the URL be loaded when the tab is shown.
  if (!_containerView)
    return;

  // Reset current WebUI if one exists.
  [self clearWebUI];

  // Abort any outstanding page load. This ensures the delegate gets informed
  // about the outgoing page, and further messages from the page are suppressed.
  if (_loadPhase != web::PAGE_LOADED)
    [self abortLoad];

  DCHECK(!_isHalted);
  // Remove the transient content view.
  [self clearTransientContentView];

  web::NavigationItem* item = self.currentNavItem;
  const GURL currentURL = item ? item->GetURL() : GURL::EmptyGURL();
  // If it's a chrome URL, but not a native one, create the WebUI instance.
  if (web::GetWebClient()->IsAppSpecificURL(currentURL) &&
      ![_nativeProvider hasControllerForURL:currentURL]) {
    if (!(item->GetTransitionType() & ui::PAGE_TRANSITION_TYPED ||
          item->GetTransitionType() & ui::PAGE_TRANSITION_AUTO_BOOKMARK) &&
        self.hasOpener) {
      // WebUI URLs can not be opened by DOM to prevent cross-site scripting as
      // they have increased power. WebUI URLs may only be opened when the user
      // types in the URL or use bookmarks.
      [[self sessionController] discardNonCommittedItems];
      return;
    } else {
      [self createWebUIForURL:currentURL];
    }
  }

  // Loading a new url, must check here if it's a native chrome URL and
  // replace the appropriate view if so, or transition back to a web view from
  // a native view.
  if ([self shouldLoadURLInNativeView:currentURL]) {
    [self loadCurrentURLInNativeView];
  } else {
    [self loadCurrentURLInWebView];
  }

  // Once a URL has been loaded, any cached-based reconstruction state has
  // either been handled or obsoleted.
  _expectedReconstructionURL = GURL();
}

- (GURL)webURLWithTrustLevel:(web::URLVerificationTrustLevel*)trustLevel {
  DCHECK(trustLevel);
  *trustLevel = web::URLVerificationTrustLevel::kAbsolute;
  return _documentURL;
}

- (BOOL)shouldLoadURLInNativeView:(const GURL&)url {
  // App-specific URLs that don't require WebUI are loaded in native views.
  return web::GetWebClient()->IsAppSpecificURL(url) &&
         !_webStateImpl->HasWebUI();
}

- (void)triggerPendingLoad {
  if (!_containerView) {
    DCHECK(!_isBeingDestroyed);
    // Create the top-level parent view, which will contain the content (whether
    // native or web). Note, this needs to be created with a non-zero size
    // to allow for (native) subviews with autosize constraints to be correctly
    // processed.
    _containerView.reset(
        [[CRWWebControllerContainerView alloc] initWithDelegate:self]);

    // This will be resized later, but matching the final frame will minimize
    // re-rendering. Use the screen size because the application's key window
    // may still be nil.
    // TODO(crbug.com/688259): Stop subtracting status bar height.
    CGFloat statusBarHeight =
        [[UIApplication sharedApplication] statusBarFrame].size.height;
    CGRect containerViewFrame = [UIScreen mainScreen].bounds;
    containerViewFrame.origin.y += statusBarHeight;
    containerViewFrame.size.height -= statusBarHeight;
    _containerView.get().frame = containerViewFrame;
    DCHECK(!CGRectIsEmpty(_containerView.get().frame));

    // TODO(crbug.com/691116): Remove this workaround once tests are no longer
    // dependent upon this accessibility ID.
    if (!base::ios::IsRunningOnIOS10OrLater())
      [_containerView setAccessibilityIdentifier:@"Container View"];

    [_containerView addGestureRecognizer:[self touchTrackingRecognizer]];
    // Is |currentUrl| a web scheme or native chrome scheme.
    web::NavigationItem* item = self.currentNavItem;
    const GURL currentNavigationURL =
        item ? item->GetVirtualURL() : GURL::EmptyGURL();
    BOOL isChromeScheme =
        web::GetWebClient()->IsAppSpecificURL(currentNavigationURL);

    // Don't immediately load the web page if in overlay mode. Always load if
    // native.
    if (isChromeScheme || !_overlayPreviewMode) {
      // TODO(jimblackler): end the practice of calling |loadCurrentURL| when it
      // is possible there is no current URL. If the call performs necessary
      // initialization, break that out.
      [self loadCurrentURL];
    }

    // Display overlay view until current url has finished loading or delay and
    // then transition away.
    if ((_overlayPreviewMode || _usePlaceholderOverlay) && !isChromeScheme)
      [self addPlaceholderOverlay];

    // Don't reset the overlay flag if in preview mode.
    if (!_overlayPreviewMode)
      _usePlaceholderOverlay = NO;
  } else if (_requireReloadOnDisplay && _webView) {
    _requireReloadOnDisplay = NO;
    [self addPlaceholderOverlay];
    [self loadCurrentURL];
  }
}

- (BOOL)shouldReload:(const GURL&)destinationURL
          transition:(ui::PageTransition)transition {
  // Do a reload if the user hits enter in the address bar or re-types a URL.
  web::NavigationItem* item = self.navigationManagerImpl->GetVisibleItem();
  return (transition & ui::PAGE_TRANSITION_FROM_ADDRESS_BAR) && item &&
         (destinationURL == item->GetURL() ||
          destinationURL == item->GetOriginalRequestURL());
}

// Reload either the web view or the native content depending on which is
// displayed.
- (void)reloadInternal {
  // Clear last user interaction.
  // TODO(crbug.com/546337): Move to after the load commits, in the subclass
  // implementation. This will be inaccurate if the reload fails or is
  // cancelled.
  _lastUserInteraction.reset();
  base::RecordAction(UserMetricsAction("Reload"));
  if ([self shouldLoadURLInNativeView:self.currentNavItem->GetURL()]) {
    [self.nativeController reload];
  } else {
    web::NavigationItem* transientItem =
        self.navigationManagerImpl->GetTransientItem();
    if (transientItem) {
      // If there's a transient item, a reload is considered a new navigation to
      // the transient item's URL (as on other platforms).
      NavigationManager::WebLoadParams reloadParams(transientItem->GetURL());
      reloadParams.transition_type = ui::PAGE_TRANSITION_RELOAD;
      reloadParams.extra_headers.reset(
          [transientItem->GetHttpRequestHeaders() copy]);
      [self loadWithParams:reloadParams];
    } else {
      // As with back and forward navigation, load the URL manually instead of
      // using the web view's reload. This ensures state processing and delegate
      // calls are consistent.
      // TODO(eugenebut): revisit this for WKWebView.
      [self loadCurrentURL];
    }
  }
}

- (void)reload {
  [_delegate webWillReload];
  [self reloadInternal];
}

- (void)abortLoad {
  [_webView stopLoading];
  [_pendingNavigationInfo setCancelled:YES];
  _certVerificationErrors->Clear();
  [self loadCancelled];
}

- (void)loadCancelled {
  [_passKitDownloader cancelPendingDownload];
  if (_loadPhase != web::PAGE_LOADED) {
    _loadPhase = web::PAGE_LOADED;
    if (!_isHalted) {
      _webStateImpl->SetIsLoading(false);
    }
  }
}

- (void)goToItemAtIndex:(int)index {
  CRWSessionController* sessionController = self.sessionController;
  const web::ScopedNavigationItemImplList& items = sessionController.items;
  if (index < 0 || index >= static_cast<int>(items.size())) {
    NOTREACHED();
    return;
  }

  if (!_webStateImpl->IsShowingWebInterstitial())
    [self recordStateInHistory];

  [self clearTransientContentView];

  // Update the user agent before attempting the navigation.
  web::NavigationItem* toItem = items[index].get();
  [self updateWebViewUserAgentFromUserAgentType:toItem->GetUserAgentType()];

  web::NavigationItem* fromItem = sessionController.currentItem;
  BOOL sameDocumentNavigation =
      [sessionController isSameDocumentNavigationBetweenItem:fromItem
                                                     andItem:toItem];
  if (sameDocumentNavigation) {
    [sessionController goToItemAtIndex:index discardNonCommittedItems:YES];
    [self updateHTML5HistoryState];
  } else {
    [sessionController discardNonCommittedItems];
    [sessionController setPendingItemIndex:index];

    web::NavigationItemImpl* pendingItem = sessionController.pendingItem;
    pendingItem->SetTransitionType(ui::PageTransitionFromInt(
        pendingItem->GetTransitionType() | ui::PAGE_TRANSITION_FORWARD_BACK));

    [self loadCurrentURL];
  }
}

- (BOOL)isLoaded {
  return _loadPhase == web::PAGE_LOADED;
}

- (void)didFinishNavigation:(WKNavigation*)navigation {
  // This can be called at multiple times after the document has loaded. Do
  // nothing if the document has already loaded.
  if (_loadPhase == web::PAGE_LOADED)
    return;
  [self loadCompleteWithSuccess:YES forNavigation:navigation];
}

- (void)loadCompleteWithSuccess:(BOOL)loadSuccess
                  forNavigation:(WKNavigation*)navigation {
  [self removePlaceholderOverlay];
  // The webView may have been torn down (or replaced by a native view). Be
  // safe and do nothing if that's happened.
  if (_loadPhase != web::PAGE_LOADING)
    return;

  DCHECK(_webView);

  const GURL currentURL([self currentURL]);

  _loadPhase = web::PAGE_LOADED;

  [self optOutScrollsToTopForSubviews];

  DCHECK((currentURL == _lastRegisteredRequestURL) ||  // latest navigation
         // previous navigation
         ![[_navigationStates lastAddedNavigation] isEqual:navigation] ||
         // invalid URL load
         (!_lastRegisteredRequestURL.is_valid() &&
          _documentURL.spec() == url::kAboutBlankURL))
      << std::endl
      << "currentURL = [" << currentURL << "]" << std::endl
      << "_lastRegisteredRequestURL = [" << _lastRegisteredRequestURL << "]";

  // Perform post-load-finished updates.
  [self didFinishWithURL:currentURL loadSuccess:loadSuccess];

  // Execute the pending LoadCompleteActions.
  for (ProceduralBlock action in _pendingLoadCompleteActions.get()) {
    action();
  }
  [_pendingLoadCompleteActions removeAllObjects];
}

- (void)didFinishWithURL:(const GURL&)currentURL loadSuccess:(BOOL)loadSuccess {
  DCHECK(_loadPhase == web::PAGE_LOADED);
  // Rather than creating a new WKBackForwardListItem when loading WebUI pages,
  // WKWebView will cache the WebUI HTML in the previous WKBackForwardListItem
  // since it's loaded via |-loadHTML:forURL:| instead of an NSURLRequest.  As a
  // result, the WebUI's HTML and URL will be loaded when navigating to that
  // WKBackForwardListItem, causing a mismatch between the visible content and
  // the visible URL (WebUI page will be visible, but URL will be the previous
  // page's URL).  To prevent this potential URL spoofing vulnerability, reset
  // the previous NavigationItem's WKBackForwardListItem to force loading via
  // NSURLRequest.
  if (_webUIManager) {
    web::NavigationItem* lastNavigationItem =
        self.sessionController.previousItem;
    if (lastNavigationItem) {
      web::WKBackForwardListItemHolder* holder =
          web::WKBackForwardListItemHolder::FromNavigationItem(
              lastNavigationItem);
      DCHECK(holder);
      holder->set_back_forward_list_item(nil);
    }
  }

  [self restoreStateFromHistory];
  _webStateImpl->SetIsLoading(false);
  _webStateImpl->OnPageLoaded(currentURL, loadSuccess);
}

- (void)goDelta:(int)delta {
  if (_isBeingDestroyed)
    return;

  if (delta == 0) {
    [self reload];
    return;
  }

  if (self.navigationManagerImpl->CanGoToOffset(delta)) {
    NSInteger index = self.navigationManagerImpl->GetIndexForOffset(delta);
    [self goToItemAtIndex:index];
  }
}

- (void)addGestureRecognizerToWebView:(UIGestureRecognizer*)recognizer {
  if ([_gestureRecognizers containsObject:recognizer])
    return;

  [_webView addGestureRecognizer:recognizer];
  [_gestureRecognizers addObject:recognizer];
}

- (void)removeGestureRecognizerFromWebView:(UIGestureRecognizer*)recognizer {
  if (![_gestureRecognizers containsObject:recognizer])
    return;

  [_webView removeGestureRecognizer:recognizer];
  [_gestureRecognizers removeObject:recognizer];
}

- (void)addToolbarViewToWebView:(UIView*)toolbarView {
  DCHECK(toolbarView);
  if ([_webViewToolbars containsObject:toolbarView])
    return;
  [_webViewToolbars addObject:toolbarView];
  if (_webView)
    [_containerView addToolbar:toolbarView];
}

- (void)removeToolbarViewFromWebView:(UIView*)toolbarView {
  if (![_webViewToolbars containsObject:toolbarView])
    return;
  [_webViewToolbars removeObject:toolbarView];
  if (_webView)
    [_containerView removeToolbar:toolbarView];
}

- (CRWJSInjectionReceiver*)jsInjectionReceiver {
  return _jsInjectionReceiver;
}

- (BOOL)shouldClosePageOnNativeApplicationLoad {
  // The page should be closed if it was initiated by the DOM and there has been
  // no user interaction with the page since the web view was created, or if
  // the page has no navigation items, as occurs when an App Store link is
  // opened from another application.
  BOOL rendererInitiatedWithoutInteraction =
      self.hasOpener && !_userInteractedWithWebController;
  BOOL noNavigationItems = !(self.navigationManagerImpl->GetItemCount());
  return rendererInitiatedWithoutInteraction || noNavigationItems;
}

- (web::UserAgentType)userAgentType {
  web::NavigationItem* item = self.currentNavItem;
  return item ? item->GetUserAgentType() : web::UserAgentType::MOBILE;
}

- (web::MojoFacade*)mojoFacade {
  if (!_mojoFacade) {
    service_manager::mojom::InterfaceProvider* interfaceProvider =
        _webStateImpl->GetWebStateInterfaceProvider();
    _mojoFacade.reset(new web::MojoFacade(interfaceProvider, self));
  }
  return _mojoFacade.get();
}

- (CRWPassKitDownloader*)passKitDownloader {
  if (_passKitDownloader) {
    return _passKitDownloader.get();
  }
  base::WeakNSObject<CRWWebController> weakSelf(self);
  web::PassKitCompletionHandler passKitCompletion = ^(NSData* data) {
    base::scoped_nsobject<CRWWebController> strongSelf([weakSelf retain]);
    if (!strongSelf) {
      return;
    }
    // Cancel load to update web state, since the PassKit download happens
    // through a separate flow. This follows the same flow as when PassKit is
    // downloaded through UIWebView.
    [strongSelf loadCancelled];
    SEL didLoadPassKitObject = @selector(webController:didLoadPassKitObject:);
    id<CRWWebDelegate> delegate = [strongSelf delegate];
    if ([delegate respondsToSelector:didLoadPassKitObject]) {
      [delegate webController:strongSelf didLoadPassKitObject:data];
    }
  };
  web::BrowserState* browserState = self.webStateImpl->GetBrowserState();
  _passKitDownloader.reset([[CRWPassKitDownloader alloc]
      initWithContextGetter:browserState->GetRequestContext()
          completionHandler:passKitCompletion]);
  return _passKitDownloader.get();
}

- (void)updateWebViewUserAgentFromUserAgentType:
    (web::UserAgentType)userAgentType {
  if (userAgentType == web::UserAgentType::NONE)
    return;

  NSString* userAgent =
      base::SysUTF8ToNSString(web::GetWebClient()->GetUserAgent(userAgentType));
  [_webView setCustomUserAgent:userAgent];
}

#pragma mark -
#pragma mark CRWWebControllerContainerViewDelegate

- (CRWWebViewProxyImpl*)contentViewProxyForContainerView:
        (CRWWebControllerContainerView*)containerView {
  return _webViewProxy.get();
}

- (CGFloat)headerHeightForContainerView:
        (CRWWebControllerContainerView*)containerView {
  return [self headerHeight];
}

#pragma mark -
#pragma mark CRWJSInjectionEvaluator Methods

- (void)executeJavaScript:(NSString*)script
        completionHandler:(web::JavaScriptResultBlock)completionHandler {
  NSString* safeScript = [self scriptByAddingWindowIDCheckForScript:script];
  web::ExecuteJavaScript(_webView, safeScript, completionHandler);
}

- (BOOL)scriptHasBeenInjectedForClass:(Class)injectionManagerClass {
  return [_injectedScriptManagers containsObject:injectionManagerClass];
}

- (void)injectScript:(NSString*)script forClass:(Class)JSInjectionManagerClass {
  DCHECK(script.length);
  // Script execution is an asynchronous operation which may pass sensitive
  // data to the page. executeJavaScript:completionHandler makes sure that
  // receiver page did not change by checking its window id.
  // |[_webView executeJavaScript:completionHandler:]| is not used here because
  // it does not check that page is the same.
  [self executeJavaScript:script completionHandler:nil];
  [_injectedScriptManagers addObject:JSInjectionManagerClass];
}

#pragma mark -

- (void)executeUserJavaScript:(NSString*)script
            completionHandler:(web::JavaScriptResultBlock)completion {
  // For security reasons, executing JavaScript on pages with app-specific URLs
  // is not allowed, because those pages may have elevated privileges.
  GURL lastCommittedURL = self.webState->GetLastCommittedURL();
  if (web::GetWebClient()->IsAppSpecificURL(lastCommittedURL)) {
    if (completion) {
      dispatch_async(dispatch_get_main_queue(), ^{
        base::scoped_nsobject<NSError> error([[NSError alloc]
            initWithDomain:web::kJSEvaluationErrorDomain
                      code:web::JS_EVALUATION_ERROR_CODE_NO_WEB_VIEW
                  userInfo:nil]);
        completion(nil, error);
      });
    }
    return;
  }

  [self setUserInteractionRegistered:YES];
  [self executeJavaScript:script completionHandler:completion];
}

- (BOOL)respondToMessage:(base::DictionaryValue*)message
       userIsInteracting:(BOOL)userIsInteracting
               originURL:(const GURL&)originURL {
  std::string command;
  if (!message->GetString("command", &command)) {
    DLOG(WARNING) << "JS message parameter not found: command";
    return NO;
  }

  SEL handler = [self selectorToHandleJavaScriptCommand:command];
  if (!handler) {
    if (!self.webStateImpl->OnScriptCommandReceived(
            command, *message, originURL, userIsInteracting)) {
      // Message was either unexpected or not correctly handled.
      // Page is reset as a precaution.
      DLOG(WARNING) << "Unexpected message received: " << command;
      return NO;
    }
    return YES;
  }

  typedef BOOL (*HandlerType)(id, SEL, base::DictionaryValue*, NSDictionary*);
  HandlerType handlerImplementation =
      reinterpret_cast<HandlerType>([self methodForSelector:handler]);
  DCHECK(handlerImplementation);
  NSMutableDictionary* context =
      [NSMutableDictionary dictionaryWithObject:@(userIsInteracting)
                                         forKey:kUserIsInteractingKey];
  NSURL* originNSURL = net::NSURLWithGURL(originURL);
  if (originNSURL)
    context[kOriginURLKey] = originNSURL;
  return handlerImplementation(self, handler, message, context);
}

- (SEL)selectorToHandleJavaScriptCommand:(const std::string&)command {
  static std::map<std::string, SEL>* handlers = nullptr;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    handlers = new std::map<std::string, SEL>();
    (*handlers)["addPluginPlaceholders"] =
        @selector(handleAddPluginPlaceholdersMessage:context:);
    (*handlers)["chrome.send"] = @selector(handleChromeSendMessage:context:);
    (*handlers)["console"] = @selector(handleConsoleMessage:context:);
    (*handlers)["document.favicons"] =
        @selector(handleDocumentFaviconsMessage:context:);
    (*handlers)["document.submit"] =
        @selector(handleDocumentSubmitMessage:context:);
    (*handlers)["form.activity"] =
        @selector(handleFormActivityMessage:context:);
    (*handlers)["window.error"] = @selector(handleWindowErrorMessage:context:);
    (*handlers)["window.hashchange"] =
        @selector(handleWindowHashChangeMessage:context:);
    (*handlers)["window.history.back"] =
        @selector(handleWindowHistoryBackMessage:context:);
    (*handlers)["window.history.willChangeState"] =
        @selector(handleWindowHistoryWillChangeStateMessage:context:);
    (*handlers)["window.history.didPushState"] =
        @selector(handleWindowHistoryDidPushStateMessage:context:);
    (*handlers)["window.history.didReplaceState"] =
        @selector(handleWindowHistoryDidReplaceStateMessage:context:);
    (*handlers)["window.history.forward"] =
        @selector(handleWindowHistoryForwardMessage:context:);
    (*handlers)["window.history.go"] =
        @selector(handleWindowHistoryGoMessage:context:);
  });
  DCHECK(handlers);
  auto iter = handlers->find(command);
  return iter != handlers->end() ? iter->second : nullptr;
}

- (void)didReceiveScriptMessage:(WKScriptMessage*)message {
  // Broken out into separate method to catch errors.
  if (![self respondToWKScriptMessage:message]) {
    DLOG(WARNING) << "Message from JS not handled due to invalid format";
  }
}

- (NSString*)scriptByAddingWindowIDCheckForScript:(NSString*)script {
  NSString* kTemplate = @"if (__gCrWeb['windowId'] === '%@') { %@; }";
  return [NSString
      stringWithFormat:kTemplate, [_windowIDJSManager windowID], script];
}

- (BOOL)respondToWKScriptMessage:(WKScriptMessage*)scriptMessage {
  if (!scriptMessage.frameInfo.mainFrame) {
    // Messages from iframes are not currently supported.
    return NO;
  }

  std::unique_ptr<base::Value> messageAsValue =
      web::ValueResultFromWKResult(scriptMessage.body);
  base::DictionaryValue* message = nullptr;
  if (!messageAsValue || !messageAsValue->GetAsDictionary(&message)) {
    return NO;
  }
  std::string windowID;
  message->GetString("crwWindowId", &windowID);
  // Check for correct windowID
  if (base::SysNSStringToUTF8([_windowIDJSManager windowID]) != windowID) {
    DLOG(WARNING) << "Message from JS ignored due to non-matching windowID: " <<
        [_windowIDJSManager windowID]
                  << " != " << base::SysUTF8ToNSString(windowID);
    return NO;
  }
  base::DictionaryValue* command = nullptr;
  if (!message->GetDictionary("crwCommand", &command)) {
    return NO;
  }
  if ([scriptMessage.name isEqualToString:kScriptMessageName]) {
    return [self respondToMessage:command
                userIsInteracting:[self userIsInteracting]
                        originURL:net::GURLWithNSURL([_webView URL])];
  }

  NOTREACHED();
  return NO;
}

#pragma mark -
#pragma mark JavaScript message handlers

- (BOOL)handleAddPluginPlaceholdersMessage:(base::DictionaryValue*)message
                                   context:(NSDictionary*)context {
  // Inject the script that adds the plugin placeholders.
  [[_jsInjectionReceiver
      instanceOfClass:[CRWJSPluginPlaceholderManager class]] inject];
  return YES;
}

- (BOOL)handleChromeSendMessage:(base::DictionaryValue*)message
                        context:(NSDictionary*)context {
  if (_webStateImpl->HasWebUI()) {
    const GURL currentURL([self currentURL]);
    if (web::GetWebClient()->IsAppSpecificURL(currentURL)) {
      std::string messageContent;
      base::ListValue* arguments = nullptr;
      if (!message->GetString("message", &messageContent)) {
        DLOG(WARNING) << "JS message parameter not found: message";
        return NO;
      }
      if (!message->GetList("arguments", &arguments)) {
        DLOG(WARNING) << "JS message parameter not found: arguments";
        return NO;
      }
      _webStateImpl->OnScriptCommandReceived(
          messageContent, *message, currentURL, context[kUserIsInteractingKey]);
      _webStateImpl->ProcessWebUIMessage(currentURL, messageContent,
                                         *arguments);
      return YES;
    }
  }

  DLOG(WARNING)
      << "chrome.send message not handled because WebUI was not found.";
  return NO;
}

- (BOOL)handleConsoleMessage:(base::DictionaryValue*)message
                     context:(NSDictionary*)context {
  // Do not log if JS logging is off.
  if (![[NSUserDefaults standardUserDefaults] boolForKey:kLogJavaScript]) {
    return YES;
  }

  std::string method;
  if (!message->GetString("method", &method)) {
    DLOG(WARNING) << "JS message parameter not found: method";
    return NO;
  }
  std::string consoleMessage;
  if (!message->GetString("message", &consoleMessage)) {
    DLOG(WARNING) << "JS message parameter not found: message";
    return NO;
  }
  std::string origin;
  if (!message->GetString("origin", &origin)) {
    DLOG(WARNING) << "JS message parameter not found: origin";
    return NO;
  }

  DVLOG(0) << origin << " [" << method << "] " << consoleMessage;
  return YES;
}

- (BOOL)handleDocumentFaviconsMessage:(base::DictionaryValue*)message
                              context:(NSDictionary*)context {
  base::ListValue* favicons = nullptr;
  if (!message->GetList("favicons", &favicons)) {
    DLOG(WARNING) << "JS message parameter not found: favicons";
    return NO;
  }
  std::vector<web::FaviconURL> urls;
  BOOL hasFavicon = NO;
  for (size_t fav_idx = 0; fav_idx != favicons->GetSize(); ++fav_idx) {
    base::DictionaryValue* favicon = nullptr;
    if (!favicons->GetDictionary(fav_idx, &favicon))
      return NO;
    std::string href;
    std::string rel;
    if (!favicon->GetString("href", &href)) {
      DLOG(WARNING) << "JS message parameter not found: href";
      return NO;
    }
    if (!favicon->GetString("rel", &rel)) {
      DLOG(WARNING) << "JS message parameter not found: rel";
      return NO;
    }
    BOOL isAppleTouch = YES;
    web::FaviconURL::IconType icon_type = web::FaviconURL::FAVICON;
    if (rel == "apple-touch-icon")
      icon_type = web::FaviconURL::TOUCH_ICON;
    else if (rel == "apple-touch-icon-precomposed")
      icon_type = web::FaviconURL::TOUCH_PRECOMPOSED_ICON;
    else
      isAppleTouch = NO;
    GURL url = GURL(href);
    if (url.is_valid()) {
      urls.push_back(web::FaviconURL(url, icon_type, std::vector<gfx::Size>()));
      hasFavicon = hasFavicon || !isAppleTouch;
    }
  }

  if (!hasFavicon) {
    // If an HTTP(S)? webpage does not reference a "favicon" of a type different
    // from apple touch, then search for a file named "favicon.ico" at the root
    // of the website (legacy). http://en.wikipedia.org/wiki/Favicon
    id origin = context[kOriginURLKey];
    if (origin) {
      NSURL* originNSURL = base::mac::ObjCCastStrict<NSURL>(origin);
      GURL originGURL = net::GURLWithNSURL(originNSURL);
      if (originGURL.is_valid() && originGURL.SchemeIsHTTPOrHTTPS()) {
        GURL::Replacements replacements;
        replacements.SetPathStr("/favicon.ico");
        urls.push_back(web::FaviconURL(
            originGURL.ReplaceComponents(replacements),
            web::FaviconURL::FAVICON, std::vector<gfx::Size>()));
      }
    }
  }
  if (!urls.empty())
    _webStateImpl->OnFaviconUrlUpdated(urls);
  return YES;
}

- (BOOL)handleDocumentSubmitMessage:(base::DictionaryValue*)message
                            context:(NSDictionary*)context {
  std::string href;
  if (!message->GetString("href", &href)) {
    DLOG(WARNING) << "JS message parameter not found: href";
    return NO;
  }
  std::string formName;
  message->GetString("formName", &formName);
  base::scoped_nsobject<NSSet> observers([_observers copy]);
  // We decide the form is user-submitted if the user has interacted with
  // the main page (using logic from the popup blocker), or if the keyboard
  // is visible.
  BOOL submittedByUser = [context[kUserIsInteractingKey] boolValue] ||
                         [_webViewProxy keyboardAccessory];
  _webStateImpl->OnDocumentSubmitted(formName, submittedByUser);
  return YES;
}

- (BOOL)handleFormActivityMessage:(base::DictionaryValue*)message
                          context:(NSDictionary*)context {
  std::string formName;
  std::string fieldName;
  std::string type;
  std::string value;
  bool inputMissing = false;
  if (!message->GetString("formName", &formName) ||
      !message->GetString("fieldName", &fieldName) ||
      !message->GetString("type", &type) ||
      !message->GetString("value", &value)) {
    inputMissing = true;
  }

  _webStateImpl->OnFormActivityRegistered(formName, fieldName, type, value,
                                          inputMissing);
  return YES;
}

- (BOOL)handleWindowErrorMessage:(base::DictionaryValue*)message
                         context:(NSDictionary*)context {
  std::string errorMessage;
  if (!message->GetString("message", &errorMessage)) {
    DLOG(WARNING) << "JS message parameter not found: message";
    return NO;
  }
  DLOG(ERROR) << "JavaScript error: " << errorMessage
              << " URL:" << [self currentURL].spec();
  return YES;
}

- (BOOL)handleWindowHashChangeMessage:(base::DictionaryValue*)message
                              context:(NSDictionary*)context {
  // Record that the current NavigationItem was created by a hash change, but
  // ignore hashchange events that are manually dispatched for same-document
  // navigations.
  if (_dispatchingSameDocumentHashChangeEvent) {
    _dispatchingSameDocumentHashChangeEvent = NO;
  } else {
    web::NavigationItemImpl* item = self.currentNavItem;
    DCHECK(item);
    item->SetIsCreatedFromHashChange(true);
  }

  return YES;
}

- (BOOL)handleWindowHistoryBackMessage:(base::DictionaryValue*)message
                               context:(NSDictionary*)context {
  [self goDelta:-1];
  return YES;
}

- (BOOL)handleWindowHistoryForwardMessage:(base::DictionaryValue*)message
                                  context:(NSDictionary*)context {
  [self goDelta:1];
  return YES;
}

- (BOOL)handleWindowHistoryGoMessage:(base::DictionaryValue*)message
                             context:(NSDictionary*)context {
  double delta = 0;
  if (message->GetDouble("value", &delta)) {
    [self goDelta:static_cast<int>(delta)];
    return YES;
  }
  return NO;
}

- (BOOL)handleWindowHistoryWillChangeStateMessage:(base::DictionaryValue*)unused
                                          context:(NSDictionary*)unusedContext {
  _changingHistoryState = YES;
  return YES;
}

- (BOOL)handleWindowHistoryDidPushStateMessage:(base::DictionaryValue*)message
                                       context:(NSDictionary*)context {
  DCHECK(_changingHistoryState);
  _changingHistoryState = NO;

  // If there is a pending entry, a new navigation has been registered but
  // hasn't begun loading.  Since the pushState message is coming from the
  // previous page, ignore it and allow the previously registered navigation to
  // continue.  This can ocur if a pushState is issued from an anchor tag
  // onClick event, as the click would have already been registered.
  if ([self sessionController].pendingItem)
    return NO;

  std::string pageURL;
  std::string baseURL;
  if (!message->GetString("pageUrl", &pageURL) ||
      !message->GetString("baseUrl", &baseURL)) {
    DLOG(WARNING) << "JS message parameter not found: pageUrl or baseUrl";
    return NO;
  }
  GURL pushURL = web::history_state_util::GetHistoryStateChangeUrl(
      [self currentURL], GURL(baseURL), pageURL);
  // UIWebView seems to choke on unicode characters that haven't been
  // escaped; escape the URL now so the expected load URL is correct.
  pushURL = URLEscapedForHistory(pushURL);
  if (!pushURL.is_valid())
    return YES;
  web::NavigationItem* navItem = self.currentNavItem;
  // PushState happened before first navigation entry or called when the
  // navigation entry does not contain a valid URL.
  if (!navItem || !navItem->GetURL().is_valid())
    return YES;
  if (!web::history_state_util::IsHistoryStateChangeValid(
          self.currentNavItem->GetURL(), pushURL)) {
    // If the current session entry URL origin still doesn't match pushURL's
    // origin, ignore the pushState. This can happen if a new URL is loaded
    // just before the pushState.
    return YES;
  }
  std::string stateObjectJSON;
  if (!message->GetString("stateObject", &stateObjectJSON)) {
    DLOG(WARNING) << "JS message parameter not found: stateObject";
    return NO;
  }
  NSString* stateObject = base::SysUTF8ToNSString(stateObjectJSON);
  _URLOnStartLoading = pushURL;
  _lastRegisteredRequestURL = pushURL;

  // If the user interacted with the page, categorize it as a link navigation.
  // If not, categorize it is a client redirect as it occurred without user
  // input and should not be added to the history stack.
  // TODO(crbug.com/549301): Improve transition detection.
  ui::PageTransition transition = self.userInteractionRegistered
                                      ? ui::PAGE_TRANSITION_LINK
                                      : ui::PAGE_TRANSITION_CLIENT_REDIRECT;
  [self pushStateWithPageURL:pushURL
                 stateObject:stateObject
                  transition:transition];

  NSString* replaceWebViewJS =
      [self javaScriptToReplaceWebViewURL:pushURL stateObjectJSON:stateObject];
  base::WeakNSObject<CRWWebController> weakSelf(self);
  [self executeJavaScript:replaceWebViewJS completionHandler:^(id, NSError*) {
    if (!weakSelf || weakSelf.get()->_isBeingDestroyed)
      return;
    base::scoped_nsobject<CRWWebController> strongSelf([weakSelf retain]);
    [strongSelf optOutScrollsToTopForSubviews];
    [strongSelf didFinishNavigation:nil];
  }];
  return YES;
}

- (BOOL)handleWindowHistoryDidReplaceStateMessage:
    (base::DictionaryValue*)message
                                          context:(NSDictionary*)context {
  DCHECK(_changingHistoryState);
  _changingHistoryState = NO;

  std::string pageURL;
  std::string baseURL;
  if (!message->GetString("pageUrl", &pageURL) ||
      !message->GetString("baseUrl", &baseURL)) {
    DLOG(WARNING) << "JS message parameter not found: pageUrl or baseUrl";
    return NO;
  }
  GURL replaceURL = web::history_state_util::GetHistoryStateChangeUrl(
      [self currentURL], GURL(baseURL), pageURL);
  // UIWebView seems to choke on unicode characters that haven't been
  // escaped; escape the URL now so the expected load URL is correct.
  replaceURL = URLEscapedForHistory(replaceURL);
  if (!replaceURL.is_valid())
    return YES;

  web::NavigationItem* navItem = self.currentNavItem;
  // ReplaceState happened before first navigation entry or called right
  // after window.open when the url is empty/not valid.
  if (!navItem || (self.navigationManagerImpl->GetItemCount() <= 1 &&
                   navItem->GetURL().is_empty()))
    return YES;
  if (!web::history_state_util::IsHistoryStateChangeValid(
          self.currentNavItem->GetURL(), replaceURL)) {
    // If the current session entry URL origin still doesn't match
    // replaceURL's origin, ignore the replaceState. This can happen if a
    // new URL is loaded just before the replaceState.
    return YES;
  }
  std::string stateObjectJSON;
  if (!message->GetString("stateObject", &stateObjectJSON)) {
    DLOG(WARNING) << "JS message parameter not found: stateObject";
    return NO;
  }
  NSString* stateObject = base::SysUTF8ToNSString(stateObjectJSON);
  _URLOnStartLoading = replaceURL;
  _lastRegisteredRequestURL = replaceURL;
  [self replaceStateWithPageURL:replaceURL stateObject:stateObject];
  NSString* replaceStateJS = [self javaScriptToReplaceWebViewURL:replaceURL
                                                 stateObjectJSON:stateObject];
  base::WeakNSObject<CRWWebController> weakSelf(self);
  [self executeJavaScript:replaceStateJS completionHandler:^(id, NSError*) {
    if (!weakSelf || weakSelf.get()->_isBeingDestroyed)
      return;
    base::scoped_nsobject<CRWWebController> strongSelf([weakSelf retain]);
    [strongSelf didFinishNavigation:nil];
  }];
  return YES;
}

#pragma mark -

- (BOOL)wantsKeyboardShield {
  if ([self.nativeController
          respondsToSelector:@selector(wantsKeyboardShield)]) {
    return [self.nativeController wantsKeyboardShield];
  }
  return YES;
}

- (BOOL)wantsLocationBarHintText {
  if ([self.nativeController
          respondsToSelector:@selector(wantsLocationBarHintText)]) {
    return [self.nativeController wantsLocationBarHintText];
  }
  return YES;
}

// TODO(stuartmorgan): This method conflates document changes and URL changes;
// we should be distinguishing better, and be clear about the expected
// WebDelegate and WCO callbacks in each case.
- (void)webPageChanged {
  DCHECK(_loadPhase == web::LOAD_REQUESTED);

  const GURL currentURL([self currentURL]);
  web::Referrer referrer = [self currentReferrer];
  // If no referrer was known in advance, record it now. (If there was one,
  // keep it since it will have a more accurate URL and policy than what can
  // be extracted from the landing page.)
  web::NavigationItem* currentItem = self.currentNavItem;
  if (!currentItem->GetReferrer().url.is_valid()) {
    currentItem->SetReferrer(referrer);
  }

  // TODO(stuartmorgan): This shouldn't be called for hash state or
  // push/replaceState.
  [self resetDocumentSpecificState];

  [self didStartLoadingURL:currentURL];
}

- (void)resetDocumentSpecificState {
  _lastUserInteraction.reset();
  _clickInProgress = NO;
}

- (void)didStartLoadingURL:(const GURL&)URL {
  _loadPhase = web::PAGE_LOADING;
  _URLOnStartLoading = URL;
  _displayStateOnStartLoading = self.pageDisplayState;

  self.userInteractionRegistered = NO;
  _pageHasZoomed = NO;

  [[self sessionController] commitPendingItem];
}

- (void)wasShown {
  if ([self.nativeController respondsToSelector:@selector(wasShown)]) {
    [self.nativeController wasShown];
  }
}

- (void)wasHidden {
  if (_isHalted)
    return;
  [self recordStateInHistory];
  if ([self.nativeController respondsToSelector:@selector(wasHidden)]) {
    [self.nativeController wasHidden];
  }
}

+ (BOOL)webControllerCanShow:(const GURL&)url {
  return web::UrlHasWebScheme(url) ||
         web::GetWebClient()->IsAppSpecificURL(url) ||
         url.SchemeIs(url::kFileScheme) || url.SchemeIs(url::kAboutScheme);
}

- (void)setUserInteractionRegistered:(BOOL)flag {
  _userInteractionRegistered = flag;
  if (flag)
    _interactionRegisteredSinceLastURLChange = YES;
}

- (BOOL)userInteractionRegistered {
  return _userInteractionRegistered;
}

- (void)cachePOSTDataForRequest:(NSURLRequest*)request
               inNavigationItem:(web::NavigationItemImpl*)item {
  NSUInteger maxPOSTDataSizeInBytes = 4096;
  NSString* cookieHeaderName = @"cookie";

  DCHECK(item);
  const bool shouldUpdateEntry =
      ui::PageTransitionCoreTypeIs(item->GetTransitionType(),
                                   ui::PAGE_TRANSITION_FORM_SUBMIT) &&
      ![request HTTPBodyStream] &&  // Don't cache streams.
      !item->HasPostData() &&
      item->GetURL() == net::GURLWithNSURL([request URL]);
  const bool belowSizeCap =
      [[request HTTPBody] length] < maxPOSTDataSizeInBytes;
  DLOG_IF(WARNING, shouldUpdateEntry && !belowSizeCap)
      << "Data in POST request exceeds the size cap (" << maxPOSTDataSizeInBytes
      << " bytes), and will not be cached.";

  if (shouldUpdateEntry && belowSizeCap) {
    item->SetPostData([request HTTPBody]);
    item->ResetHttpRequestHeaders();
    item->AddHttpRequestHeaders([request allHTTPHeaderFields]);
    // Don't cache the "Cookie" header.
    // According to NSURLRequest documentation, |-valueForHTTPHeaderField:| is
    // case insensitive, so it's enough to test the lower case only.
    if ([request valueForHTTPHeaderField:cookieHeaderName]) {
      // Case insensitive search in |headers|.
      NSSet* cookieKeys = [item->GetHttpRequestHeaders()
          keysOfEntriesPassingTest:^(id key, id obj, BOOL* stop) {
            NSString* header = (NSString*)key;
            const BOOL found =
                [header caseInsensitiveCompare:cookieHeaderName] ==
                NSOrderedSame;
            *stop = found;
            return found;
          }];
      DCHECK_EQ(1u, [cookieKeys count]);
      item->RemoveHttpRequestHeaderForKey([cookieKeys anyObject]);
    }
  }
}

// TODO(stuartmorgan): This is mostly logic from the original UIWebView delegate
// method, which provides less information than the WKWebView version. Audit
// this for things that should be handled in the subclass instead.
- (BOOL)shouldAllowLoadWithNavigationAction:(WKNavigationAction*)action {
  NSURLRequest* request = action.request;
  GURL requestURL = net::GURLWithNSURL(request.URL);

  // External application launcher needs |isNavigationTypeLinkActivated| to
  // decide if the user intended to open the application by clicking on a link.
  BOOL isNavigationTypeLinkActivated =
      action.navigationType == WKNavigationTypeLinkActivated;

  // Checks if the link navigation leads to a launch of an external app.
  // TODO(crbug.com/704417): External apps will not be launched from clicking
  // a Bookmarked URL or a Recently Closed URL.
  // TODO(crbug.com/607780): Revise the logic of allowing external app launch
  // and move it to externalAppLauncher.
  BOOL isOpenInNewTabNavigation = !(self.navigationManagerImpl->GetItemCount());
  BOOL isPossibleLinkClick = [self isLinkNavigation:action.navigationType];
  if (isPossibleLinkClick || isOpenInNewTabNavigation) {
    web::NavigationItem* item = self.currentNavItem;
    const GURL currentNavigationURL =
        item ? item->GetVirtualURL() : GURL::EmptyGURL();
    // Check If the URL is handled by a native app.
    if ([self urlTriggersNativeAppLaunch:requestURL
                               sourceURL:currentNavigationURL
                 linkActivatedNavigation:isNavigationTypeLinkActivated]) {
      // External app has been launched successfully. Stop the current page
      // load operation (e.g. notifying all observers) and record the URL so
      // that errors reported following the 'NO' reply can be safely ignored.
      if ([self shouldClosePageOnNativeApplicationLoad])
        _webStateImpl->CloseWebState();
      [self stopLoading];
      [_openedApplicationURL addObject:request.URL];
      return NO;
    }
  }

  // The WebDelegate may instruct the CRWWebController to stop loading, and
  // instead instruct the next page to be loaded in an animation.
  GURL mainDocumentURL = net::GURLWithNSURL(request.mainDocumentURL);
  DCHECK(_webView);
  if (![self shouldOpenURL:requestURL
           mainDocumentURL:mainDocumentURL
               linkClicked:isPossibleLinkClick]) {
    return NO;
  }

  // If the URL doesn't look like one we can show, try to open the link with an
  // external application.
  // TODO(droger):  Check transition type before opening an external
  // application? For example, only allow it for TYPED and LINK transitions.
  if (![CRWWebController webControllerCanShow:requestURL]) {
    if (![self shouldOpenExternalURLForNavigationAction:action]) {
      return NO;
    }
    web::NavigationItem* item = self.currentNavItem;
    GURL sourceURL = item ? item->GetOriginalRequestURL() : GURL::EmptyGURL();

    // Stop load if navigation is believed to be happening on the main frame.
    if ([self isMainFrameNavigationAction:action])
      [self stopLoading];

    // Purge web view if last committed URL is different from the document URL.
    // This can happen if external URL was added to the navigation stack and was
    // loaded using Go Back or Go Forward navigation (in which case document URL
    // will point to the previous page).  If this is the first load for a
    // NavigationManager, there will be no last committed item, so check here.
    web::NavigationItem* lastCommittedItem =
        self.webState->GetNavigationManager()->GetLastCommittedItem();
    if (lastCommittedItem) {
      GURL lastCommittedURL = lastCommittedItem->GetURL();
      if (lastCommittedURL != _documentURL) {
        [self requirePageReconstruction];
        [self setDocumentURL:lastCommittedURL];
      }
    }

    if ([_delegate openExternalURL:requestURL
                         sourceURL:sourceURL
                       linkClicked:isNavigationTypeLinkActivated]) {
      // Record the URL so that errors reported following the 'NO' reply can be
      // safely ignored.
      [_openedApplicationURL addObject:request.URL];
      if ([self shouldClosePageOnNativeApplicationLoad])
        _webStateImpl->CloseWebState();
    }
    return NO;
  }

  if ([[request HTTPMethod] isEqualToString:@"POST"]) {
    web::NavigationItemImpl* item = self.currentNavItem;
    // TODO(crbug.com/570699): Remove this check once it's no longer possible to
    // have no current entries.
    if (item)
      [self cachePOSTDataForRequest:request inNavigationItem:item];
  }

  return YES;
}

- (void)handleLoadError:(NSError*)error
            inMainFrame:(BOOL)inMainFrame
          forNavigation:(WKNavigation*)navigation {
  NSString* MIMEType = [_pendingNavigationInfo MIMEType];
  if ([_passKitDownloader isMIMETypePassKitType:MIMEType])
    return;
  if ([error code] == NSURLErrorUnsupportedURL)
    return;
  // In cases where a Plug-in handles the load do not take any further action.
  if ([error.domain isEqual:base::SysUTF8ToNSString(web::kWebKitErrorDomain)] &&
      (error.code == web::kWebKitErrorPlugInLoadFailed ||
       error.code == web::kWebKitErrorCannotShowUrl))
    return;

  // Continue processing only if the error is on the main request or is the
  // result of a user interaction.
  NSDictionary* userInfo = [error userInfo];
  // |userinfo| contains the request creation date as a NSDate.
  NSTimeInterval requestCreationDate =
      [[userInfo objectForKey:@"CreationDate"] timeIntervalSinceReferenceDate];
  bool userInteracted = false;
  if (requestCreationDate != 0.0 && _lastUserInteraction) {
    NSTimeInterval timeSinceInteraction =
        requestCreationDate - _lastUserInteraction->time;
    // The error is considered to be the result of a user interaction if any
    // interaction happened just before the request was made.
    // TODO(droger): If the user interacted with the page after the request was
    // made (i.e. creationTimeSinceLastInteraction < 0), then
    // |_lastUserInteraction| has been overridden. The current behavior is to
    // discard the interstitial in that case. A better decision could be made if
    // we had a history of all the user interactions instead of just the last
    // one.
    userInteracted =
        timeSinceInteraction < kMaximumDelayForUserInteractionInSeconds &&
        _lastUserInteraction->time > _lastTransferTimeInSeconds &&
        timeSinceInteraction >= 0.0;
  } else {
    // If the error does not have timing information, check if the user
    // interacted with the page recently.
    userInteracted = [self userIsInteracting];
  }
  if (!inMainFrame && !userInteracted)
    return;

  // Reset SSL status to default, unless the load was cancelled (manually or by
  // back-forward navigation).
  web::NavigationManager* navManager = self.webState->GetNavigationManager();
  if (navManager->GetLastCommittedItem() && [error code] != NSURLErrorCancelled)
    navManager->GetLastCommittedItem()->GetSSL() = web::SSLStatus();

  NSURL* errorURL = [NSURL
      URLWithString:[userInfo objectForKey:NSURLErrorFailingURLStringErrorKey]];
  const GURL errorGURL = net::GURLWithNSURL(errorURL);

  web::NavigationContextImpl* navigationContext =
      [_navigationStates contextForNavigation:navigation];
  navigationContext->SetError(error);

  // Handles Frame Load Interrupted errors from WebView.
  if ([error.domain isEqual:base::SysUTF8ToNSString(web::kWebKitErrorDomain)] &&
      error.code == web::kWebKitErrorFrameLoadInterruptedByPolicyChange) {
    // See if the delegate wants to handle this case.
    if (errorGURL.is_valid() &&
        [_delegate
            respondsToSelector:@selector(
                                   controllerForUnhandledContentAtURL:)]) {
      id<CRWNativeContent> controller =
          [_delegate controllerForUnhandledContentAtURL:errorGURL];
      if (controller) {
        [self loadCompleteWithSuccess:NO forNavigation:navigation];
        [self removeWebViewAllowingCachedReconstruction:NO];
        [self setNativeController:controller];
        [self loadNativeViewWithSuccess:YES
                      navigationContext:navigationContext];
        return;
      }
    }

    // Ignore errors that originate from URLs that are opened in external apps.
    if ([_openedApplicationURL containsObject:errorURL])
      return;
    // Certain frame errors don't have URL information for some reason; for
    // those cases (so far the only known case is plugin content loaded directly
    // in a frame) just ignore the error. See crbug.com/414295
    if (!errorURL) {
      DCHECK(!inMainFrame);
      return;
    }
    // The wrapper error uses the URL of the error and not the requested URL
    // (which can be different in case of a redirect) to match desktop Chrome
    // behavior.
    NSError* wrapperError = [NSError
        errorWithDomain:[error domain]
                   code:[error code]
               userInfo:@{
                 NSURLErrorFailingURLStringErrorKey : [errorURL absoluteString],
                 NSUnderlyingErrorKey : error
               }];
    [self loadCompleteWithSuccess:NO forNavigation:navigation];
    [self loadErrorInNativeView:wrapperError
              navigationContext:navigationContext];
    return;
  }

  if ([error code] == NSURLErrorCancelled) {
    [self handleCancelledError:error];
    // NSURLErrorCancelled errors that aren't handled by aborting the load will
    // automatically be retried by the web view, so early return in this case.
    return;
  }

  [self loadCompleteWithSuccess:NO forNavigation:navigation];
  [self loadErrorInNativeView:error navigationContext:navigationContext];
}

- (void)handleCancelledError:(NSError*)error {
  if ([self shouldCancelLoadForCancelledError:error]) {
    [self loadCancelled];
    [[self sessionController] discardNonCommittedItems];
  }
}

- (BOOL)shouldCancelLoadForCancelledError:(NSError*)error {
  DCHECK_EQ(error.code, NSURLErrorCancelled);
  // Do not cancel the load if it is for an app specific URL, as such errors
  // are produced during the app specific URL load process.
  const GURL errorURL =
      net::GURLWithNSURL(error.userInfo[NSURLErrorFailingURLErrorKey]);
  if (web::GetWebClient()->IsAppSpecificURL(errorURL))
    return NO;

  // Don't cancel NSURLErrorCancelled errors originating from navigation
  // as the WKWebView will automatically retry these loads.
  WKWebViewErrorSource source = WKWebViewErrorSourceFromError(error);
  return source != NAVIGATION;
}

#pragma mark -
#pragma mark WebUI

- (void)createWebUIForURL:(const GURL&)URL {
  // |CreateWebUI| will do nothing if |URL| is not a WebUI URL and then
  // |HasWebUI| will return false.
  _webStateImpl->CreateWebUI(URL);
  bool isWebUIURL = _webStateImpl->HasWebUI();
  if (isWebUIURL) {
    _webUIManager.reset(
        [[CRWWebUIManager alloc] initWithWebState:_webStateImpl]);
  }
}

- (void)clearWebUI {
  _webStateImpl->ClearWebUI();
  _webUIManager.reset();
}

#pragma mark -
#pragma mark Auth Challenge

- (void)processAuthChallenge:(NSURLAuthenticationChallenge*)challenge
         forCertAcceptPolicy:(web::CertAcceptPolicy)policy
                  certStatus:(net::CertStatus)certStatus
           completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,
                                       NSURLCredential*))completionHandler {
  SecTrustRef trust = challenge.protectionSpace.serverTrust;
  if (policy == web::CERT_ACCEPT_POLICY_RECOVERABLE_ERROR_ACCEPTED_BY_USER) {
    // Cert is invalid, but user agreed to proceed, override default behavior.
    completionHandler(NSURLSessionAuthChallengeUseCredential,
                      [NSURLCredential credentialForTrust:trust]);
    return;
  }

  if (policy != web::CERT_ACCEPT_POLICY_ALLOW &&
      SecTrustGetCertificateCount(trust)) {
    // The cert is invalid and the user has not agreed to proceed. Cache the
    // cert verification result in |_certVerificationErrors|, so that it can
    // later be reused inside |didFailProvisionalNavigation:|.
    // The leaf cert is used as the key, because the chain provided by
    // |didFailProvisionalNavigation:| will differ (it is the server-supplied
    // chain), thus if intermediates were considered, the keys would mismatch.
    scoped_refptr<net::X509Certificate> leafCert =
        net::x509_util::CreateX509CertificateFromSecCertificate(
            SecTrustGetCertificateAtIndex(trust, 0),
            std::vector<SecCertificateRef>());
    if (leafCert) {
      BOOL is_recoverable =
          policy == web::CERT_ACCEPT_POLICY_RECOVERABLE_ERROR_UNDECIDED_BY_USER;
      std::string host =
          base::SysNSStringToUTF8(challenge.protectionSpace.host);
      _certVerificationErrors->Put(
          web::CertHostPair(leafCert, host),
          CertVerificationError(is_recoverable, certStatus));
    }
  }
  completionHandler(NSURLSessionAuthChallengeRejectProtectionSpace, nil);
}

- (void)handleHTTPAuthForChallenge:(NSURLAuthenticationChallenge*)challenge
                 completionHandler:
                     (void (^)(NSURLSessionAuthChallengeDisposition,
                               NSURLCredential*))completionHandler {
  NSURLProtectionSpace* space = challenge.protectionSpace;
  DCHECK(
      [space.authenticationMethod isEqual:NSURLAuthenticationMethodHTTPBasic] ||
      [space.authenticationMethod isEqual:NSURLAuthenticationMethodNTLM] ||
      [space.authenticationMethod isEqual:NSURLAuthenticationMethodHTTPDigest]);

  _webStateImpl->OnAuthRequired(
      space, challenge.proposedCredential,
      base::BindBlock(^(NSString* user, NSString* password) {
        [CRWWebController processHTTPAuthForUser:user
                                        password:password
                               completionHandler:completionHandler];
      }));
}

+ (void)processHTTPAuthForUser:(NSString*)user
                      password:(NSString*)password
             completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,
                                         NSURLCredential*))completionHandler {
  DCHECK_EQ(user == nil, password == nil);
  if (!user || !password) {
    // Embedder cancelled authentication.
    completionHandler(NSURLSessionAuthChallengeRejectProtectionSpace, nil);
    return;
  }
  completionHandler(
      NSURLSessionAuthChallengeUseCredential,
      [NSURLCredential
          credentialWithUser:user
                    password:password
                 persistence:NSURLCredentialPersistenceForSession]);
}

#pragma mark -
#pragma mark JavaScript Dialog

- (void)runJavaScriptDialogOfType:(web::JavaScriptDialogType)type
                 initiatedByFrame:(WKFrameInfo*)frame
                          message:(NSString*)message
                      defaultText:(NSString*)defaultText
                       completion:(void (^)(BOOL, NSString*))completionHandler {
  DCHECK(completionHandler);
  if (self.shouldSuppressDialogs) {
    _webStateImpl->OnDialogSuppressed();
    completionHandler(NO, nil);
    return;
  }

  self.webStateImpl->RunJavaScriptDialog(
      net::GURLWithNSURL(frame.request.URL), type, message, defaultText,
      base::BindBlock(^(bool success, NSString* input) {
        completionHandler(success, input);
      }));
}

#pragma mark -
#pragma mark TouchTracking

- (void)touched:(BOOL)touched {
  _clickInProgress = touched;
  if (touched) {
    self.userInteractionRegistered = YES;
    _userInteractedWithWebController = YES;
    if (_isBeingDestroyed)
      return;
    const web::NavigationManagerImpl& navigationManager =
        self.webStateImpl->GetNavigationManagerImpl();
    GURL mainDocumentURL =
        navigationManager.GetItemCount()
            ? navigationManager.GetLastCommittedItem()->GetURL()
            : [self currentURL];
    _lastUserInteraction.reset(new UserInteractionEvent(mainDocumentURL));
  }
}

- (CRWTouchTrackingRecognizer*)touchTrackingRecognizer {
  if (!_touchTrackingRecognizer) {
    _touchTrackingRecognizer.reset(
        [[CRWTouchTrackingRecognizer alloc] initWithDelegate:self]);
  }
  return _touchTrackingRecognizer.get();
}

- (BOOL)userIsInteracting {
  // If page transfer started after last click, user is deemed to be no longer
  // interacting.
  if (!_lastUserInteraction ||
      _lastTransferTimeInSeconds > _lastUserInteraction->time) {
    return NO;
  }
  return [self userClickedRecently];
}

- (BOOL)userClickedRecently {
  // Scrolling generates a pair of touch on/off event which causes
  // _lastUserInteraction to register that there was user interaction.
  // Checks for scrolling first to override time-based click heuristics.
  BOOL scrolling = [[self webScrollView] isDragging] ||
                   [[self webScrollView] isDecelerating];
  if (scrolling)
    return NO;
  if (!_lastUserInteraction)
    return NO;
  return _clickInProgress ||
         ((CFAbsoluteTimeGetCurrent() - _lastUserInteraction->time) <
          kMaximumDelayForUserInteractionInSeconds);
}

#pragma mark Placeholder Overlay Methods

- (void)addPlaceholderOverlay {
  if (!_overlayPreviewMode) {
    // Create |kSnapshotOverlayDelay| second timer to remove image with
    // transition.
    [self performSelector:@selector(removePlaceholderOverlay)
               withObject:nil
               afterDelay:kSnapshotOverlayDelay];
  }

  // Add overlay image.
  _placeholderOverlayView.reset([[UIImageView alloc] init]);
  CGRect frame = [self visibleFrame];
  [_placeholderOverlayView setFrame:frame];
  [_placeholderOverlayView
      setAutoresizingMask:UIViewAutoresizingFlexibleWidth |
                          UIViewAutoresizingFlexibleHeight];
  [_placeholderOverlayView setContentMode:UIViewContentModeScaleAspectFill];
  [_containerView addSubview:_placeholderOverlayView];

  id callback = ^(UIImage* image) {
    [_placeholderOverlayView setImage:image];
  };
  [_delegate webController:self retrievePlaceholderOverlayImage:callback];

  if (!_placeholderOverlayView.get().image) {
    _placeholderOverlayView.get().image = [[self class] defaultSnapshotImage];
  }
}

- (void)removePlaceholderOverlay {
  if (!_placeholderOverlayView || _overlayPreviewMode)
    return;

  [NSObject
      cancelPreviousPerformRequestsWithTarget:self
                                     selector:@selector(
                                                  removePlaceholderOverlay)
                                       object:nil];
  // Remove overlay with transition.
  [UIView animateWithDuration:kSnapshotOverlayTransition
      animations:^{
        [_placeholderOverlayView setAlpha:0.0f];
      }
      completion:^(BOOL finished) {
        [_placeholderOverlayView removeFromSuperview];
        _placeholderOverlayView.reset();
      }];
}

- (void)setOverlayPreviewMode:(BOOL)overlayPreviewMode {
  _overlayPreviewMode = overlayPreviewMode;

  // If we were showing the preview, remove it.
  if (!_overlayPreviewMode && _placeholderOverlayView) {
    [self resetContainerView];
    // Reset |_placeholderOverlayView| directly instead of calling
    // -removePlaceholderOverlay, which removes |_placeholderOverlayView| in an
    // animation.
    [_placeholderOverlayView removeFromSuperview];
    _placeholderOverlayView.reset();
    // There are cases when resetting the contentView, above, may happen after
    // the web view has been created. Re-add it here, rather than
    // relying on a subsequent call to loadCurrentURLInWebView.
    if (_webView) {
      [[self view] addSubview:_webView];
    }
  }
}

#pragma mark -
#pragma mark Session Information

- (CRWSessionController*)sessionController {
  NavigationManagerImpl* navigationManager = self.navigationManagerImpl;
  return navigationManager ? navigationManager->GetSessionController() : nil;
}

- (NavigationManagerImpl*)navigationManagerImpl {
  return _webStateImpl ? &(_webStateImpl->GetNavigationManagerImpl()) : nil;
}

- (BOOL)hasOpener {
  return _webStateImpl ? _webStateImpl->HasOpener() : NO;
}

- (web::NavigationItemImpl*)currentNavItem {
  // This goes through the legacy Session* interface rather than Navigation*
  // because it is itself a legacy method that should not exist, and this
  // avoids needing to add a GetActiveItem to NavigationManager. If/when this
  // method chain becomes a blocker to eliminating SessionController, the logic
  // can be moved here, using public NavigationManager getters. That's not
  // done now in order to avoid code duplication.
  return [[self sessionController] currentItem];
}

- (ui::PageTransition)currentTransition {
  if (self.currentNavItem)
    return self.currentNavItem->GetTransitionType();
  else
    return ui::PageTransitionFromInt(0);
}

- (web::Referrer)currentNavItemReferrer {
  web::NavigationItem* currentItem = self.currentNavItem;
  return currentItem ? currentItem->GetReferrer() : web::Referrer();
}

- (NSDictionary*)currentHTTPHeaders {
  web::NavigationItem* currentItem = self.currentNavItem;
  return currentItem ? currentItem->GetHttpRequestHeaders() : nil;
}

#pragma mark -
#pragma mark CRWWebViewScrollViewProxyObserver

- (void)webViewScrollViewDidZoom:
        (CRWWebViewScrollViewProxy*)webViewScrollViewProxy {
  _pageHasZoomed = YES;

  base::WeakNSObject<UIScrollView> weakScrollView(self.webScrollView);
  [self extractViewportTagWithCompletion:^(
            const web::PageViewportState* viewportState) {
    if (!weakScrollView)
      return;
    base::scoped_nsobject<UIScrollView> scrollView([weakScrollView retain]);
    if (viewportState && !viewportState->viewport_tag_present() &&
        [scrollView minimumZoomScale] == [scrollView maximumZoomScale] &&
        [scrollView zoomScale] > 1.0) {
      UMA_HISTOGRAM_BOOLEAN(kUMAViewportZoomBugCount, true);
    }
  }];
}

- (void)webViewScrollViewDidResetContentSize:
    (CRWWebViewScrollViewProxy*)webViewScrollViewProxy {
  web::NavigationItem* currentItem = self.currentNavItem;
  if (webViewScrollViewProxy.isZooming || _applyingPageState || !currentItem)
    return;
  CGSize contentSize = webViewScrollViewProxy.contentSize;
  if (contentSize.width < CGRectGetWidth(webViewScrollViewProxy.frame)) {
    // The renderer incorrectly resized the content area.  Resetting the scroll
    // view's zoom scale will force a re-rendering.  rdar://23963992
    _applyingPageState = YES;
    web::PageZoomState zoomState =
        currentItem->GetPageDisplayState().zoom_state();
    if (!zoomState.IsValid())
      zoomState = web::PageZoomState(1.0, 1.0, 1.0);
    [self applyWebViewScrollZoomScaleFromZoomState:zoomState];
    _applyingPageState = NO;
  }
}

#pragma mark -
#pragma mark CRWWebViewScrollViewProxyObserver

// Under WKWebView, JavaScript can execute asynchronously. User can start
// scrolling and calls to window.scrollTo executed during scrolling will be
// treated as "during user interaction" and can cause app to go fullscreen.
// This is a workaround to use this webViewScrollViewIsDragging flag to ignore
// window.scrollTo while user is scrolling. See crbug.com/554257
- (void)webViewScrollViewWillBeginDragging:
    (CRWWebViewScrollViewProxy*)webViewScrollViewProxy {
  [self executeJavaScript:@"__gCrWeb.setWebViewScrollViewIsDragging(true)"
        completionHandler:nil];
}

- (void)webViewScrollViewDidEndDragging:
            (CRWWebViewScrollViewProxy*)webViewScrollViewProxy
                         willDecelerate:(BOOL)decelerate {
  [self executeJavaScript:@"__gCrWeb.setWebViewScrollViewIsDragging(false)"
        completionHandler:nil];
}

#pragma mark -
#pragma mark Page State

- (void)recordStateInHistory {
  // Only record the state if:
  // - the current NavigationItem's URL matches the current URL, and
  // - the user has interacted with the page.
  web::NavigationItem* item = self.currentNavItem;
  if (item && item->GetURL() == [self currentURL] &&
      self.userInteractionRegistered) {
    item->SetPageDisplayState(self.pageDisplayState);
  }
}

- (void)restoreStateFromHistory {
  web::NavigationItem* item = self.currentNavItem;
  if (item)
    self.pageDisplayState = item->GetPageDisplayState();
}

- (web::PageDisplayState)pageDisplayState {
  web::PageDisplayState displayState;
  if (_webView) {
    CGPoint scrollOffset = [self scrollPosition];
    displayState.scroll_state().set_offset_x(std::floor(scrollOffset.x));
    displayState.scroll_state().set_offset_y(std::floor(scrollOffset.y));
    UIScrollView* scrollView = self.webScrollView;
    displayState.zoom_state().set_minimum_zoom_scale(
        scrollView.minimumZoomScale);
    displayState.zoom_state().set_maximum_zoom_scale(
        scrollView.maximumZoomScale);
    displayState.zoom_state().set_zoom_scale(scrollView.zoomScale);
  } else {
    // TODO(crbug.com/546146): Handle native views.
  }
  return displayState;
}

- (void)setPageDisplayState:(web::PageDisplayState)displayState {
  if (!displayState.IsValid())
    return;
  if (_webView) {
    // Page state is restored after a page load completes.  If the user has
    // scrolled or changed the zoom scale while the page is still loading, don't
    // restore any state since it will confuse the user.
    web::PageDisplayState currentPageDisplayState = self.pageDisplayState;
    if (currentPageDisplayState.scroll_state().offset_x() ==
            _displayStateOnStartLoading.scroll_state().offset_x() &&
        currentPageDisplayState.scroll_state().offset_y() ==
            _displayStateOnStartLoading.scroll_state().offset_y() &&
        !_pageHasZoomed) {
      [self applyPageDisplayState:displayState];
    }
  }
}

- (void)extractViewportTagWithCompletion:(ViewportStateCompletion)completion {
  DCHECK(completion);
  web::NavigationItem* currentItem = self.currentNavItem;
  if (!currentItem) {
    completion(nullptr);
    return;
  }
  NSString* const kViewportContentQuery =
      @"var viewport = document.querySelector('meta[name=\"viewport\"]');"
       "viewport ? viewport.content : '';";
  base::WeakNSObject<CRWWebController> weakSelf(self);
  int itemID = currentItem->GetUniqueID();
  [self executeJavaScript:kViewportContentQuery
        completionHandler:^(id viewportContent, NSError*) {
          web::NavigationItem* item = [weakSelf currentNavItem];
          if (item && item->GetUniqueID() == itemID) {
            web::PageViewportState viewportState(
                base::mac::ObjCCast<NSString>(viewportContent));
            completion(&viewportState);
          } else {
            completion(nullptr);
          }
        }];
}

- (void)orientationDidChange {
  // When rotating, the available zoom scale range may change, zoomScale's
  // percentage into this range should remain constant.  However, there are
  // two known bugs with respect to adjusting the zoomScale on rotation:
  // - WKWebView sometimes erroneously resets the scroll view's zoom scale to
  // an incorrect value ( rdar://20100815 ).
  // - After zooming occurs in a UIWebView that's displaying a page with a hard-
  // coded viewport width, the zoom will not be updated upon rotation
  // ( crbug.com/485055 ).
  if (!_webView)
    return;
  web::NavigationItem* currentItem = self.currentNavItem;
  if (!currentItem)
    return;
  web::PageDisplayState displayState = currentItem->GetPageDisplayState();
  if (!displayState.IsValid())
    return;
  CGFloat zoomPercentage = (displayState.zoom_state().zoom_scale() -
                            displayState.zoom_state().minimum_zoom_scale()) /
                           displayState.zoom_state().GetMinMaxZoomDifference();
  displayState.zoom_state().set_minimum_zoom_scale(
      self.webScrollView.minimumZoomScale);
  displayState.zoom_state().set_maximum_zoom_scale(
      self.webScrollView.maximumZoomScale);
  displayState.zoom_state().set_zoom_scale(
      displayState.zoom_state().minimum_zoom_scale() +
      zoomPercentage * displayState.zoom_state().GetMinMaxZoomDifference());
  currentItem->SetPageDisplayState(displayState);
  [self applyPageDisplayState:currentItem->GetPageDisplayState()];
}

- (void)applyPageDisplayState:(const web::PageDisplayState&)displayState {
  if (!displayState.IsValid())
    return;
  base::WeakNSObject<CRWWebController> weakSelf(self);
  web::PageDisplayState displayStateCopy = displayState;
  [self extractViewportTagWithCompletion:^(
            const web::PageViewportState* viewportState) {
    if (viewportState) {
      [weakSelf applyPageDisplayState:displayStateCopy
                         userScalable:viewportState->user_scalable()];
    }
  }];
}

- (void)applyPageDisplayState:(const web::PageDisplayState&)displayState
                 userScalable:(BOOL)isUserScalable {
  // Early return if |scrollState| doesn't match the current NavigationItem.
  // This can sometimes occur in tests, as navigation occurs programmatically
  // and |-applyPageScrollState:| is asynchronous.
  web::NavigationItem* currentItem = self.currentNavItem;
  if (currentItem && currentItem->GetPageDisplayState() != displayState)
    return;
  DCHECK(displayState.IsValid());
  _applyingPageState = YES;
  if (isUserScalable) {
    [self prepareToApplyWebViewScrollZoomScale];
    [self applyWebViewScrollZoomScaleFromZoomState:displayState.zoom_state()];
    [self finishApplyingWebViewScrollZoomScale];
  }
  [self applyWebViewScrollOffsetFromScrollState:displayState.scroll_state()];
  _applyingPageState = NO;
}

- (void)prepareToApplyWebViewScrollZoomScale {
  id webView = _webView;
  if (![webView respondsToSelector:@selector(viewForZoomingInScrollView:)]) {
    return;
  }

  UIView* contentView = [webView viewForZoomingInScrollView:self.webScrollView];

  if ([webView
          respondsToSelector:@selector(scrollViewWillBeginZooming:withView:)]) {
    [webView scrollViewWillBeginZooming:self.webScrollView
                               withView:contentView];
  }
}

- (void)finishApplyingWebViewScrollZoomScale {
  id webView = _webView;
  if ([webView respondsToSelector:@selector(scrollViewDidEndZooming:
                                                           withView:
                                                            atScale:)] &&
      [webView respondsToSelector:@selector(viewForZoomingInScrollView:)]) {
    // This correctly sets the content's frame in the scroll view to
    // fit the web page and upscales the content so that it isn't
    // blurry.
    UIView* contentView =
        [webView viewForZoomingInScrollView:self.webScrollView];
    [webView scrollViewDidEndZooming:self.webScrollView
                            withView:contentView
                             atScale:self.webScrollView.zoomScale];
  }
}

- (void)applyWebViewScrollZoomScaleFromZoomState:
    (const web::PageZoomState&)zoomState {
  // After rendering a web page, WKWebView keeps the |minimumZoomScale| and
  // |maximumZoomScale| properties of its scroll view constant while adjusting
  // the |zoomScale| property accordingly.  The maximum-scale or minimum-scale
  // meta tags of a page may have changed since the state was recorded, so clamp
  // the zoom scale to the current range if necessary.
  DCHECK(zoomState.IsValid());
  CGFloat zoomScale = zoomState.zoom_scale();
  if (zoomScale < self.webScrollView.minimumZoomScale)
    zoomScale = self.webScrollView.minimumZoomScale;
  if (zoomScale > self.webScrollView.maximumZoomScale)
    zoomScale = self.webScrollView.maximumZoomScale;
  self.webScrollView.zoomScale = zoomScale;
}

- (void)applyWebViewScrollOffsetFromScrollState:
    (const web::PageScrollState&)scrollState {
  DCHECK(scrollState.IsValid());
  CGPoint scrollOffset =
      CGPointMake(scrollState.offset_x(), scrollState.offset_y());
  if (_loadPhase == web::PAGE_LOADED) {
    // If the page is loaded, update the scroll immediately.
    [self.webScrollView setContentOffset:scrollOffset];
  } else {
    // If the page isn't loaded, store the action to update the scroll
    // when the page finishes loading.
    base::WeakNSObject<UIScrollView> weakScrollView(self.webScrollView);
    base::scoped_nsprotocol<ProceduralBlock> action([^{
      [weakScrollView setContentOffset:scrollOffset];
    } copy]);
    [_pendingLoadCompleteActions addObject:action];
  }
}

#pragma mark -
#pragma mark Fullscreen

- (CGRect)visibleFrame {
  CGRect frame = [_containerView bounds];
  CGFloat headerHeight = [self headerHeight];
  frame.origin.y = headerHeight;
  frame.size.height -= headerHeight;
  return frame;
}

- (void)optOutScrollsToTopForSubviews {
  NSMutableArray* stack =
      [NSMutableArray arrayWithArray:[self.webScrollView subviews]];
  while (stack.count) {
    UIView* current = [stack lastObject];
    [stack removeLastObject];
    [stack addObjectsFromArray:[current subviews]];
    if ([current isKindOfClass:[UIScrollView class]])
      static_cast<UIScrollView*>(current).scrollsToTop = NO;
  }
}

#pragma mark -
#pragma mark WebDelegate Calls

- (BOOL)shouldOpenURL:(const GURL&)url
      mainDocumentURL:(const GURL&)mainDocumentURL
          linkClicked:(BOOL)linkClicked {
  if (![_delegate respondsToSelector:@selector(webController:
                                               shouldOpenURL:
                                             mainDocumentURL:
                                                 linkClicked:)]) {
    return YES;
  }
  return [_delegate webController:self
                    shouldOpenURL:url
                  mainDocumentURL:mainDocumentURL
                      linkClicked:linkClicked];
}

- (BOOL)isMainFrameNavigationAction:(WKNavigationAction*)action {
  if (action.targetFrame) {
    return action.targetFrame.mainFrame;
  }
  // According to WKNavigationAction documentation, in the case of a new window
  // navigation, target frame will be nil. In this case check if the
  // |sourceFrame| is the mainFrame.
  return action.sourceFrame.mainFrame;
}

- (BOOL)shouldOpenExternalURLForNavigationAction:(WKNavigationAction*)action {
  ExternalURLRequestStatus requestStatus = NUM_EXTERNAL_URL_REQUEST_STATUS;
  if ([self isMainFrameNavigationAction:action]) {
    requestStatus = MAIN_FRAME_ALLOWED;
  } else {
    // If the request's main document URL differs from that at the time of the
    // last user interaction, then the page has changed since the user last
    // interacted.
    BOOL userInteractedWithRequestMainFrame =
        [self userClickedRecently] &&
        net::GURLWithNSURL(action.request.mainDocumentURL) ==
            _lastUserInteraction->main_document_url;
    // Prevent subframe requests from opening an external URL if the user has
    // not interacted with the request's main frame.
    requestStatus = userInteractedWithRequestMainFrame ? SUBFRAME_ALLOWED
                                                       : SUBFRAME_BLOCKED;
  }
  DCHECK_NE(requestStatus, NUM_EXTERNAL_URL_REQUEST_STATUS);
  UMA_HISTOGRAM_ENUMERATION("WebController.ExternalURLRequestBlocking",
                            requestStatus, NUM_EXTERNAL_URL_REQUEST_STATUS);
  if (requestStatus == SUBFRAME_BLOCKED) {
    return NO;
  }

  GURL requestURL = net::GURLWithNSURL(action.request.URL);
  return [_delegate respondsToSelector:@selector(webController:
                                           shouldOpenExternalURL:)] &&
         [_delegate webController:self shouldOpenExternalURL:requestURL];
}

- (BOOL)urlTriggersNativeAppLaunch:(const GURL&)URL
                         sourceURL:(const GURL&)sourceURL
           linkActivatedNavigation:(BOOL)linkActivatedNavigation {
  if (![_delegate respondsToSelector:@selector(urlTriggersNativeAppLaunch:
                                                                sourceURL:
                                                              linkClicked:)]) {
    return NO;
  }
  return [_delegate urlTriggersNativeAppLaunch:URL
                                     sourceURL:sourceURL
                                   linkClicked:linkActivatedNavigation];
}

- (CGFloat)headerHeight {
  if (![_delegate respondsToSelector:@selector(headerHeightForWebController:)])
    return 0.0f;
  return [_delegate headerHeightForWebController:self];
}

- (void)updateSSLStatusForCurrentNavigationItem {
  if (_isBeingDestroyed) {
    return;
  }

  web::NavigationManager* navManager = self.webState->GetNavigationManager();
  web::NavigationItem* currentNavItem = navManager->GetLastCommittedItem();
  if (!currentNavItem) {
    return;
  }

  if (!_SSLStatusUpdater) {
    _SSLStatusUpdater.reset([[CRWSSLStatusUpdater alloc]
        initWithDataSource:self
         navigationManager:navManager]);
    [_SSLStatusUpdater setDelegate:self];
  }
  NSString* host = base::SysUTF8ToNSString(_documentURL.host());
  BOOL hasOnlySecureContent = [_webView hasOnlySecureContent];
  base::ScopedCFTypeRef<SecTrustRef> trust;
  if (base::ios::IsRunningOnIOS10OrLater()) {
    trust.reset([_webView serverTrust], base::scoped_policy::RETAIN);
  } else {
    trust = web::CreateServerTrustFromChain([_webView certificateChain], host);
  }

  [_SSLStatusUpdater updateSSLStatusForNavigationItem:currentNavItem
                                         withCertHost:host
                                                trust:std::move(trust)
                                 hasOnlySecureContent:hasOnlySecureContent];
}

- (void)didShowPasswordInputOnHTTP {
  DCHECK(!web::IsOriginSecure(self.webState->GetLastCommittedURL()));
  web::NavigationItem* item =
      _webStateImpl->GetNavigationManager()->GetLastCommittedItem();
  item->GetSSL().content_status |=
      web::SSLStatus::DISPLAYED_PASSWORD_FIELD_ON_HTTP;
  _webStateImpl->OnVisibleSecurityStateChange();
}

- (void)didShowCreditCardInputOnHTTP {
  DCHECK(!web::IsOriginSecure(self.webState->GetLastCommittedURL()));
  web::NavigationItem* item =
      _webStateImpl->GetNavigationManager()->GetLastCommittedItem();
  item->GetSSL().content_status |=
      web::SSLStatus::DISPLAYED_CREDIT_CARD_FIELD_ON_HTTP;
  _webStateImpl->OnVisibleSecurityStateChange();
}

- (void)handleSSLCertError:(NSError*)error
             forNavigation:(WKNavigation*)navigation {
  CHECK(web::IsWKWebViewSSLCertError(error));

  net::SSLInfo info;
  web::GetSSLInfoFromWKWebViewSSLCertError(error, &info);

  if (!info.cert) {
    // |info.cert| can be null if certChain in NSError is empty or can not be
    // parsed, in this case do not ask delegate if error should be allowed, it
    // should not be.
    [self handleLoadError:error inMainFrame:YES forNavigation:navigation];
    return;
  }

  // Retrieve verification results from _certVerificationErrors cache to avoid
  // unnecessary recalculations. Verification results are cached for the leaf
  // cert, because the cert chain in |didReceiveAuthenticationChallenge:| is
  // the OS constructed chain, while |chain| is the chain from the server.
  NSArray* chain = error.userInfo[web::kNSErrorPeerCertificateChainKey];
  NSURL* requestURL = error.userInfo[web::kNSErrorFailingURLKey];
  NSString* host = [requestURL host];
  scoped_refptr<net::X509Certificate> leafCert;
  BOOL recoverable = NO;
  if (chain.count && host.length) {
    // The complete cert chain may not be available, so the leaf cert is used
    // as a key to retrieve _certVerificationErrors, as well as for storing the
    // cert decision.
    leafCert = web::CreateCertFromChain(@[ chain.firstObject ]);
    if (leafCert) {
      auto error = _certVerificationErrors->Get(
          {leafCert, base::SysNSStringToUTF8(host)});
      bool cacheHit = error != _certVerificationErrors->end();
      if (cacheHit) {
        recoverable = error->second.is_recoverable;
        info.cert_status = error->second.status;
      }
      UMA_HISTOGRAM_BOOLEAN("WebController.CertVerificationErrorsCacheHit",
                            cacheHit);
    }
  }

  // Ask web client if this cert error should be allowed.
  web::GetWebClient()->AllowCertificateError(
      _webStateImpl, net::MapCertStatusToNetError(info.cert_status), info,
      net::GURLWithNSURL(requestURL), recoverable,
      base::BindBlock(^(bool proceed) {
        if (proceed) {
          DCHECK(recoverable);
          [_certVerificationController allowCert:leafCert
                                         forHost:host
                                          status:info.cert_status];
          _webStateImpl->GetSessionCertificatePolicyCache()
              ->RegisterAllowedCertificate(
                  leafCert, base::SysNSStringToUTF8(host), info.cert_status);
          [self loadCurrentURL];
        } else {
          // If discarding non-committed items results in a NavigationItem that
          // should be loaded via a native controller, load that URL, as its
          // native controller will need to be recreated.  Note that a
          // successful preload of a page with an certificate error will result
          // in this block executing on a CRWWebController with no
          // NavigationManager.  Additionally, if a page with a certificate
          // error is opened in a new tab, its last committed NavigationItem
          // will be null.
          web::NavigationManager* navigationManager =
              self.navigationManagerImpl;
          web::NavigationItem* item =
              navigationManager ? navigationManager->GetLastCommittedItem()
                                : nullptr;
          if (item && [self shouldLoadURLInNativeView:item->GetURL()])
            [self loadCurrentURL];
        }
      }));

  _webStateImpl->OnVisibleSecurityStateChange();
  [self loadCancelled];
}

- (void)ensureWebViewCreated {
  WKWebViewConfiguration* config =
      [self webViewConfigurationProvider].GetWebViewConfiguration();
  [self ensureWebViewCreatedWithConfiguration:config];
}

- (void)ensureWebViewCreatedWithConfiguration:(WKWebViewConfiguration*)config {
  if (!_webView) {
    [self setWebView:[self webViewWithConfiguration:config]];
    // The following is not called in -setWebView: as the latter used in unit
    // tests with fake web view, which cannot be added to view hierarchy.
    CHECK(_webUsageEnabled) << "Tried to create a web view while suspended!";

    DCHECK(_webView);

    [_webView setAutoresizingMask:UIViewAutoresizingFlexibleWidth |
                                  UIViewAutoresizingFlexibleHeight];
    [_webView setBackgroundColor:[UIColor colorWithWhite:0.2 alpha:1.0]];

    // Create a dependency between the |webView| pan gesture and BVC side swipe
    // gestures. Note: This needs to be added before the longPress recognizers
    // below, or the longPress appears to deadlock the remaining recognizers,
    // thereby breaking scroll.
    NSSet* recognizers = [_swipeRecognizerProvider swipeRecognizers];
    for (UISwipeGestureRecognizer* swipeRecognizer in recognizers) {
      [self.webScrollView.panGestureRecognizer
          requireGestureRecognizerToFail:swipeRecognizer];
    }

    _contextMenuController.reset([[CRWContextMenuController alloc]
           initWithWebView:_webView
        injectionEvaluator:self
                  delegate:self]);

    // Add all additional gesture recognizers to the web view.
    for (UIGestureRecognizer* recognizer in _gestureRecognizers.get()) {
      [_webView addGestureRecognizer:recognizer];
    }

    _URLOnStartLoading = _defaultURL;

    // WKWebViews with invalid or empty frames have exhibited rendering bugs, so
    // resize the view to match the container view upon creation.
    [_webView setFrame:[_containerView bounds]];

    // If the visible NavigationItem should be loaded in this web view, display
    // it immediately.  Otherwise, it will be displayed when the pending load is
    // committed.
    web::NavigationItem* visibleItem =
        self.navigationManagerImpl->GetVisibleItem();
    const GURL& visibleURL =
        visibleItem ? visibleItem->GetURL() : GURL::EmptyGURL();
    if (![self shouldLoadURLInNativeView:visibleURL])
      [self displayWebView];
  }
}

- (WKWebView*)webViewWithConfiguration:(WKWebViewConfiguration*)config {
  // Do not attach the context menu controller immediately as the JavaScript
  // delegate must be specified.
  return web::BuildWKWebView(CGRectZero, config,
                             self.webStateImpl->GetBrowserState(),
                             self.userAgentType);
}

- (void)setWebView:(WKWebView*)webView {
  DCHECK_NE(_webView.get(), webView);

  // Unwind the old web view.
  // TODO(eugenebut): Remove CRWWKScriptMessageRouter once crbug.com/543374 is
  // fixed.
  CRWWKScriptMessageRouter* messageRouter =
      [self webViewConfigurationProvider].GetScriptMessageRouter();
  if (_webView) {
    [messageRouter removeAllScriptMessageHandlersForWebView:_webView];
  }
  [_webView setNavigationDelegate:nil];
  [_webView setUIDelegate:nil];
  for (NSString* keyPath in self.WKWebViewObservers) {
    [_webView removeObserver:self forKeyPath:keyPath];
  }

  _webView.reset([webView retain]);

  // Set up the new web view.
  if (webView) {
    base::WeakNSObject<CRWWebController> weakSelf(self);
    [messageRouter setScriptMessageHandler:^(WKScriptMessage* message) {
      [weakSelf didReceiveScriptMessage:message];
    }
                                      name:kScriptMessageName
                                   webView:webView];
    _windowIDJSManager.reset(
        [[CRWJSWindowIDManager alloc] initWithWebView:webView]);
  } else {
    _windowIDJSManager.reset();
  }
  [_webView setNavigationDelegate:self];
  [_webView setUIDelegate:self];
  for (NSString* keyPath in self.WKWebViewObservers) {
    [_webView addObserver:self forKeyPath:keyPath options:0 context:nullptr];
  }
  _injectedScriptManagers.reset([[NSMutableSet alloc] init]);
  [self setDocumentURL:_defaultURL];
}

- (void)displayWebView {
  if (!self.webView || [_containerView webViewContentView])
    return;

  // Add the web toolbars.
  [_containerView addToolbars:_webViewToolbars];

  base::scoped_nsobject<CRWWebViewContentView> webViewContentView(
      [[CRWWebViewContentView alloc] initWithWebView:_webView
                                          scrollView:self.webScrollView]);
  [_containerView displayWebViewContentView:webViewContentView];
}

- (void)removeWebViewAllowingCachedReconstruction:(BOOL)allowCache {
  if (!_webView)
    return;

  _webStateImpl->CancelDialogs();

  web::NavigationItem* item = self.currentNavItem;
  if (allowCache && item) {
    _expectedReconstructionURL = item->GetVirtualURL();
  } else {
    _expectedReconstructionURL = GURL::EmptyGURL();
  }

  [self abortLoad];
  [_webView removeFromSuperview];
  [_containerView resetContent];
  [self setWebView:nil];
}

- (void)webViewWebProcessDidCrash {
  _webProcessIsDead = YES;
  _webStateImpl->CancelDialogs();
  _webStateImpl->OnRenderProcessGone();
}

- (web::WKWebViewConfigurationProvider&)webViewConfigurationProvider {
  web::BrowserState* browserState = self.webStateImpl->GetBrowserState();
  return web::WKWebViewConfigurationProvider::FromBrowserState(browserState);
}

- (WKNavigation*)loadRequest:(NSMutableURLRequest*)request {
  WKNavigation* navigation = [_webView loadRequest:request];
  [_navigationStates setState:web::WKNavigationState::REQUESTED
                forNavigation:navigation];
  return navigation;
}

- (WKNavigation*)loadPOSTRequest:(NSMutableURLRequest*)request {
  if (!_POSTRequestLoader) {
    _POSTRequestLoader.reset([[CRWJSPOSTRequestLoader alloc] init]);
  }

  CRWWKScriptMessageRouter* messageRouter =
      [self webViewConfigurationProvider].GetScriptMessageRouter();

  return [_POSTRequestLoader
        loadPOSTRequest:request
              inWebView:_webView
          messageRouter:messageRouter
      completionHandler:^(NSError* loadError) {
        if (loadError)
          [self handleLoadError:loadError inMainFrame:YES forNavigation:nil];
        else
          self.webStateImpl->SetContentsMimeType("text/html");
      }];
}

- (void)loadHTML:(NSString*)HTML forURL:(const GURL&)URL {
  DCHECK(HTML.length);
  // Remove the transient content view.
  [self clearTransientContentView];

  _loadPhase = web::LOAD_REQUESTED;

  // Web View should not be created for App Specific URLs.
  if (!web::GetWebClient()->IsAppSpecificURL(URL)) {
    [self ensureWebViewCreated];
    DCHECK(_webView) << "_webView null while trying to load HTML";
  }
  WKNavigation* navigation =
      [_webView loadHTMLString:HTML baseURL:net::NSURLWithGURL(URL)];
  [_navigationStates setState:web::WKNavigationState::REQUESTED
                forNavigation:navigation];
  std::unique_ptr<web::NavigationContextImpl> context;
  if (_webStateImpl->HasWebUI()) {
    // WebUI uses |loadHTML:forURL:| to feed the content to web view. This
    // should not be treated as a navigation, but WKNavigationDelegate callbacks
    // still expect a valid context.
    context =
        web::NavigationContextImpl::CreateNavigationContext(_webStateImpl, URL);
  } else {
    context = [self
        registerLoadRequestForURL:URL
                         referrer:web::Referrer()
                       transition:ui::PageTransition::PAGE_TRANSITION_TYPED];
  }
  [_navigationStates setContext:std::move(context) forNavigation:navigation];
}

- (void)loadHTML:(NSString*)HTML forAppSpecificURL:(const GURL&)URL {
  CHECK(web::GetWebClient()->IsAppSpecificURL(URL));
  [self loadHTML:HTML forURL:URL];
}

- (void)loadHTMLForCurrentURL:(NSString*)HTML {
  [self loadHTML:HTML forURL:self.currentURL];
}

- (void)stopLoading {
  _stoppedWKNavigation.reset([_navigationStates lastAddedNavigation]);

  base::RecordAction(UserMetricsAction("Stop"));
  // Discard the pending and transient entried before notifying the tab model
  // observers of the change via |-abortLoad|.
  [[self sessionController] discardNonCommittedItems];
  [self abortLoad];
  web::NavigationItem* item = self.currentNavItem;
  GURL navigationURL = item ? item->GetVirtualURL() : GURL::EmptyGURL();
  // If discarding the non-committed entries results in an app-specific URL,
  // reload it in its native view.
  if (!self.nativeController &&
      [self shouldLoadURLInNativeView:navigationURL]) {
    [self loadCurrentURLInNativeView];
  }
}

#pragma mark -
#pragma mark WKUIDelegate Methods

- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
               forNavigationAction:(WKNavigationAction*)action
                    windowFeatures:(WKWindowFeatures*)windowFeatures {
  if (self.shouldSuppressDialogs) {
    _webStateImpl->OnDialogSuppressed();
    return nil;
  }

  // Do not create windows for non-empty invalid URLs.
  GURL requestURL = net::GURLWithNSURL(action.request.URL);
  if (!requestURL.is_empty() && !requestURL.is_valid()) {
    DLOG(WARNING) << "Unable to open a window with invalid URL: "
                  << requestURL.spec();
    return nil;
  }

  NSString* referrer = [self referrerFromNavigationAction:action];
  GURL openerURL =
      referrer.length ? GURL(base::SysNSStringToUTF8(referrer)) : _documentURL;

  WebState* childWebState = _webStateImpl->CreateNewWebState(
      requestURL, openerURL, [self userIsInteracting]);
  if (!childWebState)
    return nil;

  CRWWebController* childWebController =
      static_cast<WebStateImpl*>(childWebState)->GetWebController();

  DCHECK(!childWebController || childWebController.hasOpener);

  // WKWebView requires WKUIDelegate to return a child view created with
  // exactly the same |configuration| object (exception is raised if config is
  // different). |configuration| param and config returned by
  // WKWebViewConfigurationProvider are different objects because WKWebView
  // makes a shallow copy of the config inside init, so every WKWebView
  // owns a separate shallow copy of WKWebViewConfiguration.
  [childWebController ensureWebViewCreatedWithConfiguration:configuration];
  return childWebController.webView;
}

- (void)webViewDidClose:(WKWebView*)webView {
  if (self.hasOpener)
    _webStateImpl->CloseWebState();
}

- (void)webView:(WKWebView*)webView
    runJavaScriptAlertPanelWithMessage:(NSString*)message
                      initiatedByFrame:(WKFrameInfo*)frame
                     completionHandler:(void (^)())completionHandler {
  [self runJavaScriptDialogOfType:web::JAVASCRIPT_DIALOG_TYPE_ALERT
                 initiatedByFrame:frame
                          message:message
                      defaultText:nil
                       completion:^(BOOL, NSString*) {
                         completionHandler();
                       }];
}

- (void)webView:(WKWebView*)webView
    runJavaScriptConfirmPanelWithMessage:(NSString*)message
                        initiatedByFrame:(WKFrameInfo*)frame
                       completionHandler:
                           (void (^)(BOOL result))completionHandler {
  [self runJavaScriptDialogOfType:web::JAVASCRIPT_DIALOG_TYPE_CONFIRM
                 initiatedByFrame:frame
                          message:message
                      defaultText:nil
                       completion:^(BOOL success, NSString*) {
                         if (completionHandler) {
                           completionHandler(success);
                         }
                       }];
}

- (void)webView:(WKWebView*)webView
    runJavaScriptTextInputPanelWithPrompt:(NSString*)prompt
                              defaultText:(NSString*)defaultText
                         initiatedByFrame:(WKFrameInfo*)frame
                        completionHandler:
                            (void (^)(NSString* result))completionHandler {
  GURL origin(web::GURLOriginWithWKSecurityOrigin(frame.securityOrigin));
  if (web::GetWebClient()->IsAppSpecificURL(origin) && _webUIManager) {
    std::string mojoResponse =
        self.mojoFacade->HandleMojoMessage(base::SysNSStringToUTF8(prompt));
    completionHandler(base::SysUTF8ToNSString(mojoResponse));
    return;
  }

  [self runJavaScriptDialogOfType:web::JAVASCRIPT_DIALOG_TYPE_PROMPT
                 initiatedByFrame:frame
                          message:prompt
                      defaultText:defaultText
                       completion:^(BOOL, NSString* input) {
                         if (completionHandler) {
                           completionHandler(input);
                         }
                       }];
}

#pragma mark -
#pragma mark WKNavigationDelegate Methods

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)action
                    decisionHandler:
                        (void (^)(WKNavigationActionPolicy))decisionHandler {
  _webProcessIsDead = NO;
  if (_isBeingDestroyed) {
    decisionHandler(WKNavigationActionPolicyCancel);
    return;
  }

  // The page will not be changed until this navigation is commited, so the
  // retrieved state will be pending until |didCommitNavigation| callback.
  [self updatePendingNavigationInfoFromNavigationAction:action];

  // Invalid URLs should not be loaded.
  GURL requestURL = net::GURLWithNSURL(action.request.URL);
  if (!requestURL.is_valid()) {
    decisionHandler(WKNavigationActionPolicyCancel);
    // The HTML5 spec indicates that window.open with an invalid URL should open
    // about:blank.
    BOOL isFirstLoadInOpenedWindow =
        self.webState->HasOpener() &&
        !self.webState->GetNavigationManager()->GetLastCommittedItem();
    BOOL isMainFrame = action.targetFrame.mainFrame;
    if (isFirstLoadInOpenedWindow && isMainFrame) {
      GURL aboutBlankURL(url::kAboutBlankURL);
      web::NavigationManager::WebLoadParams loadParams(aboutBlankURL);
      loadParams.referrer = [self currentReferrer];
      self.webState->GetNavigationManager()->LoadURLWithParams(loadParams);
    }
    return;
  }

  BOOL allowLoad = [self shouldAllowLoadWithNavigationAction:action];

  if (allowLoad) {
    allowLoad = self.webStateImpl->ShouldAllowRequest(action.request);
    if (!allowLoad && action.targetFrame.mainFrame) {
      [_pendingNavigationInfo setCancelled:YES];
    }
  }

  decisionHandler(allowLoad ? WKNavigationActionPolicyAllow
                            : WKNavigationActionPolicyCancel);
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationResponse:(WKNavigationResponse*)navigationResponse
                      decisionHandler:
                          (void (^)(WKNavigationResponsePolicy))handler {
  if ([navigationResponse.response isKindOfClass:[NSHTTPURLResponse class]]) {
    // Create HTTP headers from the response.
    // TODO(crbug.com/546157): Due to the limited interface of
    // NSHTTPURLResponse, some data in the HttpResponseHeaders generated here is
    // inexact.  Once UIWebView is no longer supported, update WebState's
    // implementation so that the Content-Language and the MIME type can be set
    // without using this imperfect conversion.
    scoped_refptr<net::HttpResponseHeaders> HTTPHeaders =
        net::CreateHeadersFromNSHTTPURLResponse(
            static_cast<NSHTTPURLResponse*>(navigationResponse.response));
    self.webStateImpl->OnHttpResponseHeadersReceived(
        HTTPHeaders.get(), net::GURLWithNSURL(navigationResponse.response.URL));
  }

  // The page will not be changed until this navigation is commited, so the
  // retrieved state will be pending until |didCommitNavigation| callback.
  [self updatePendingNavigationInfoFromNavigationResponse:navigationResponse];

  BOOL allowNavigation = navigationResponse.canShowMIMEType;
  if (allowNavigation) {
    allowNavigation =
        self.webStateImpl->ShouldAllowResponse(navigationResponse.response);
    if (!allowNavigation && navigationResponse.isForMainFrame) {
      [_pendingNavigationInfo setCancelled:YES];
    }
  }
  if ([self.passKitDownloader
          isMIMETypePassKitType:[_pendingNavigationInfo MIMEType]]) {
    GURL URL = net::GURLWithNSURL(navigationResponse.response.URL);
    [self.passKitDownloader downloadPassKitFileWithURL:URL];
    allowNavigation = NO;

    // Discard the pending PassKit entry to ensure that the current URL is not
    // different from what is displayed on the view. If there is no previous
    // committed URL, which can happen when a link is opened in a new tab via a
    // context menu or window.open, the pending entry should not be
    // discarded so that the NavigationManager is never empty. Also, URLs loaded
    // in a native view should be excluded to avoid an ugly animation where the
    // web view is inserted and quickly removed.
    GURL lastCommittedURL = self.webState->GetLastCommittedURL();
    BOOL isFirstLoad = lastCommittedURL.is_empty();
    BOOL previousItemWasLoadedInNativeView =
        [self shouldLoadURLInNativeView:lastCommittedURL];
    if (!isFirstLoad && !previousItemWasLoadedInNativeView)
      [self.sessionController discardNonCommittedItems];
  }

  handler(allowNavigation ? WKNavigationResponsePolicyAllow
                          : WKNavigationResponsePolicyCancel);
}

// TODO(stuartmorgan): Move all the guesswork around these states out of the
// superclass, and wire these up to the remaining methods.
- (void)webView:(WKWebView*)webView
    didStartProvisionalNavigation:(WKNavigation*)navigation {
  [_navigationStates setState:web::WKNavigationState::STARTED
                forNavigation:navigation];

  if (navigation &&
      ![[_navigationStates lastAddedNavigation] isEqual:navigation]) {
    // |navigation| is not the latest navigation and will be cancelled.
    // Ignore |navigation| as a new navigation will start soon.
    return;
  }

  GURL webViewURL = net::GURLWithNSURL(webView.URL);
  if (webViewURL.is_empty()) {
    // May happen on iOS9, however in didCommitNavigation: callback the URL
    // will be "about:blank".
    webViewURL = GURL(url::kAboutBlankURL);
  }

  // Intercept renderer-initiated navigations. If this navigation has not yet
  // been registered, do so. loadPhase check is necessary because
  // lastRegisteredRequestURL may be the same as the webViewURL on a new tab
  // created by window.open (default is about::blank).
  if (_lastRegisteredRequestURL != webViewURL ||
      self.loadPhase != web::LOAD_REQUESTED) {
    // Reset current WebUI if one exists.
    [self clearWebUI];
    // Restart app specific URL loads to properly capture state.
    // TODO(crbug.com/546347): Extract necessary tasks for app specific URL
    // navigation rather than restarting the load.
    if (web::GetWebClient()->IsAppSpecificURL(webViewURL)) {
      // Renderer-initiated loads of WebUI can be done only from other WebUI
      // pages. WebUI pages may have increased power and using the same web
      // process (which may potentially be controller by an attacker) is
      // dangerous.
      if (web::GetWebClient()->IsAppSpecificURL(_documentURL)) {
        [self abortLoad];
        NavigationManager::WebLoadParams params(webViewURL);
        [self loadWithParams:params];
      }
      return;
    } else {
      std::unique_ptr<web::NavigationContextImpl> navigationContext =
          [self registerLoadRequestForURL:webViewURL];
      [_navigationStates setContext:std::move(navigationContext)
                      forNavigation:navigation];
    }
  }

  // Ensure the URL is registered and loadPhase is as expected.
  DCHECK(_lastRegisteredRequestURL == webViewURL);
  DCHECK(self.loadPhase == web::LOAD_REQUESTED);
}

- (void)webView:(WKWebView*)webView
    didReceiveServerRedirectForProvisionalNavigation:(WKNavigation*)navigation {
  [_navigationStates setState:web::WKNavigationState::REDIRECTED
                forNavigation:navigation];

  // It is fine to ignore returned NavigationContext. Context does not change
  // for redirect and old context stored _navigationStates is valid and it
  // should not be replaced.
  [self registerLoadRequestForURL:net::GURLWithNSURL(webView.URL)
                         referrer:[self currentReferrer]
                       transition:ui::PAGE_TRANSITION_SERVER_REDIRECT];
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
                       withError:(NSError*)error {
  [_navigationStates setState:web::WKNavigationState::PROVISIONALY_FAILED
                forNavigation:navigation];

  // Ignore provisional navigation failure if a new navigation has been started,
  // for example, if a page is reloaded after the start of the provisional
  // load but before the load has been committed.
  if (![[_navigationStates lastAddedNavigation] isEqual:navigation]) {
    return;
  }

  // TODO(crbug.com/570699): Remove this workaround once |stopLoading| does not
  // discard pending navigation items.
  if ((!self.webStateImpl ||
       !self.webStateImpl->GetNavigationManagerImpl().GetVisibleItem()) &&
      [error.domain isEqual:base::SysUTF8ToNSString(web::kWebKitErrorDomain)] &&
      error.code == web::kWebKitErrorFrameLoadInterruptedByPolicyChange) {
    // App is going to crash in this state (crbug.com/565457). Crash will occur
    // on dereferencing visible navigation item, which is null. This scenario is
    // possible after pending load was stopped for a child window. Early return
    // to prevent the crash and report UMA metric to check if crash happening
    // because the load was stopped.
    UMA_HISTOGRAM_BOOLEAN(
        "WebController.EmptyNavigationManagerCausedByStopLoading",
        [_stoppedWKNavigation isEqual:navigation]);
    return;
  }

  // Handle load cancellation for directly cancelled navigations without
  // handling their potential errors. Otherwise, handle the error.
  if ([_pendingNavigationInfo cancelled]) {
    [self loadCancelled];
  } else {
    error = WKWebViewErrorWithSource(error, PROVISIONAL_LOAD);

    if (web::IsWKWebViewSSLCertError(error))
      [self handleSSLCertError:error forNavigation:navigation];
    else
      [self handleLoadError:error inMainFrame:YES forNavigation:navigation];
  }

  // This must be reset at the end, since code above may need information about
  // the pending load.
  _pendingNavigationInfo.reset();
  _certVerificationErrors->Clear();
  [_navigationStates removeNavigation:navigation];
}

- (void)webView:(WKWebView*)webView
    didCommitNavigation:(WKNavigation*)navigation {
  [self displayWebView];

  // Record the navigation state.
  [_navigationStates setState:web::WKNavigationState::COMMITTED
                forNavigation:navigation];

  DCHECK_EQ(_webView, webView);
  _certVerificationErrors->Clear();

  // This is the point where the document's URL has actually changed, and
  // pending navigation information should be applied to state information.
  [self setDocumentURL:net::GURLWithNSURL([_webView URL])];

  if (!_lastRegisteredRequestURL.is_valid() &&
      _documentURL != _lastRegisteredRequestURL) {
    // if |_lastRegisteredRequestURL| is an invalid URL, then |_documentURL|
    // will be "about:blank".
    [[self sessionController] updatePendingItem:_documentURL];
  }

  // If |navigation| is nil (which happens for windows open by DOM), then it
  // should be the first and the only pending navigation.
  BOOL isLastNavigation =
      !navigation ||
      [[_navigationStates lastAddedNavigation] isEqual:navigation];
  DCHECK(_documentURL == _lastRegisteredRequestURL ||  // latest navigation
         !isLastNavigation ||                          // previous navigation
         (!_lastRegisteredRequestURL.is_valid() &&     // invalid URL load
          _documentURL.spec() == url::kAboutBlankURL));

  // Update HTTP response headers.
  _webStateImpl->UpdateHttpResponseHeaders(_documentURL);
  web::NavigationContextImpl* context =
      [_navigationStates contextForNavigation:navigation];
  context->SetResponseHeaders(_webStateImpl->GetHttpResponseHeaders());

  [self commitPendingNavigationInfo];
  if ([self currentBackForwardListItemHolder]->navigation_type() ==
      WKNavigationTypeBackForward) {
    // A fast back/forward won't call decidePolicyForNavigationResponse, so
    // the MIME type needs to be updated explicitly.
    NSString* storedMIMEType =
        [self currentBackForwardListItemHolder]->mime_type();
    if (storedMIMEType) {
      self.webStateImpl->SetContentsMimeType(
          base::SysNSStringToUTF8(storedMIMEType));
    }
  }

  // This point should closely approximate the document object change, so reset
  // the list of injected scripts to those that are automatically injected.
  _injectedScriptManagers.reset([[NSMutableSet alloc] init]);
  if ([self contentIsHTML] || self.webState->GetContentsMimeType().empty()) {
    // In unit tests MIME type will be empty, because loadHTML:forURL: does not
    // notify web view delegate about received response, so web controller does
    // not get a chance to properly update MIME type.
    [_windowIDJSManager inject];
  }

  if (isLastNavigation) {
    [self webPageChanged];
  } else {
    // WKWebView has more than one in progress navigation, and committed
    // navigation was not the latest. Change last committed item to one that
    // corresponds to committed navigation.
    web::NavigationContextImpl* context =
        [_navigationStates contextForNavigation:navigation];
    for (int i = 0; i < self.navigationManagerImpl->GetItemCount(); i++) {
      if (self.navigationManagerImpl->GetItemAtIndex(i)->GetUniqueID() ==
          context->GetNavigationItemUniqueID()) {
        // Do not discard pending entry, because another pending navigation is
        // still in progress and will commit or fail soon.
        [self.sessionController goToItemAtIndex:i discardNonCommittedItems:NO];
        break;
      }
    }
  }

  self.webStateImpl->OnNavigationFinished(context);

  [self updateSSLStatusForCurrentNavigationItem];

  // Attempt to update the HTML5 history state.
  [self updateHTML5HistoryState];

  // This is the point where pending entry has been committed, and navigation
  // item title should be updated.
  [self setNavigationItemTitle:[_webView title]];

  // Report cases where SSL cert is missing for a secure connection.
  if (_documentURL.SchemeIsCryptographic()) {
    scoped_refptr<net::X509Certificate> cert =
        web::CreateCertFromChain([_webView certificateChain]);
    UMA_HISTOGRAM_BOOLEAN("WebController.WKWebViewHasCertForSecureConnection",
                          static_cast<bool>(cert));
  }
}

- (void)webView:(WKWebView*)webView
    didFinishNavigation:(WKNavigation*)navigation {
  [_navigationStates setState:web::WKNavigationState::FINISHED
                forNavigation:navigation];

  DCHECK(!_isHalted);
  // Trigger JavaScript driven post-document-load-completion tasks.
  // TODO(crbug.com/546350): Investigate using
  // WKUserScriptInjectionTimeAtDocumentEnd to inject this material at the
  // appropriate time rather than invoking here.
  web::ExecuteJavaScript(webView, @"__gCrWeb.didFinishNavigation()", nil);
  [self didFinishNavigation:navigation];
  [_navigationStates removeNavigation:navigation];
}

- (void)webView:(WKWebView*)webView
    didFailNavigation:(WKNavigation*)navigation
            withError:(NSError*)error {
  [_navigationStates setState:web::WKNavigationState::FAILED
                forNavigation:navigation];

  [self handleLoadError:WKWebViewErrorWithSource(error, NAVIGATION)
            inMainFrame:YES
          forNavigation:navigation];
  _certVerificationErrors->Clear();
  [_navigationStates removeNavigation:navigation];
}

- (void)webView:(WKWebView*)webView
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge*)challenge
                    completionHandler:
                        (void (^)(NSURLSessionAuthChallengeDisposition,
                                  NSURLCredential*))completionHandler {
  NSString* authMethod = challenge.protectionSpace.authenticationMethod;
  if ([authMethod isEqual:NSURLAuthenticationMethodHTTPBasic] ||
      [authMethod isEqual:NSURLAuthenticationMethodNTLM] ||
      [authMethod isEqual:NSURLAuthenticationMethodHTTPDigest]) {
    [self handleHTTPAuthForChallenge:challenge
                   completionHandler:completionHandler];
    return;
  }

  if (![authMethod isEqual:NSURLAuthenticationMethodServerTrust]) {
    completionHandler(NSURLSessionAuthChallengeRejectProtectionSpace, nil);
    return;
  }

  SecTrustRef trust = challenge.protectionSpace.serverTrust;
  base::ScopedCFTypeRef<SecTrustRef> scopedTrust(trust,
                                                 base::scoped_policy::RETAIN);
  base::WeakNSObject<CRWWebController> weakSelf(self);
  [_certVerificationController
      decideLoadPolicyForTrust:scopedTrust
                          host:challenge.protectionSpace.host
             completionHandler:^(web::CertAcceptPolicy policy,
                                 net::CertStatus status) {
               base::scoped_nsobject<CRWWebController> strongSelf(
                   [weakSelf retain]);
               if (!strongSelf) {
                 completionHandler(
                     NSURLSessionAuthChallengeRejectProtectionSpace, nil);
                 return;
               }
               [strongSelf processAuthChallenge:challenge
                            forCertAcceptPolicy:policy
                                     certStatus:status
                              completionHandler:completionHandler];
             }];
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView {
  _certVerificationErrors->Clear();
  [self webViewWebProcessDidCrash];
}

#pragma mark -
#pragma mark CRWSSLStatusUpdater DataSource/Delegate Methods

- (void)SSLStatusUpdater:(CRWSSLStatusUpdater*)SSLStatusUpdater
    querySSLStatusForTrust:(base::ScopedCFTypeRef<SecTrustRef>)trust
                      host:(NSString*)host
         completionHandler:(StatusQueryHandler)completionHandler {
  [_certVerificationController querySSLStatusForTrust:std::move(trust)
                                                 host:host
                                    completionHandler:completionHandler];
}

- (void)SSLStatusUpdater:(CRWSSLStatusUpdater*)SSLStatusUpdater
    didChangeSSLStatusForNavigationItem:(web::NavigationItem*)navigationItem {
  web::NavigationItem* lastCommittedNavigationItem =
      _webStateImpl->GetNavigationManager()->GetLastCommittedItem();
  if (navigationItem == lastCommittedNavigationItem) {
    _webStateImpl->OnVisibleSecurityStateChange();
  }
}

#pragma mark -
#pragma mark CRWWebContextMenuControllerDelegate methods

- (BOOL)webView:(WKWebView*)webView
    handleContextMenu:(const web::ContextMenuParams&)params {
  DCHECK(webView == _webView);
  if (_isBeingDestroyed) {
    return NO;
  }
  return self.webStateImpl->HandleContextMenu(params);
}

#pragma mark -
#pragma mark CRWNativeContentDelegate methods

- (BOOL)nativeContent:(id)content
    handleContextMenu:(const web::ContextMenuParams&)params {
  if (_isBeingDestroyed) {
    return NO;
  }
  return self.webStateImpl->HandleContextMenu(params);
}

#pragma mark -
#pragma mark KVO Observation

- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context {
  NSString* dispatcherSelectorName = self.WKWebViewObservers[keyPath];
  DCHECK(dispatcherSelectorName);
  if (dispatcherSelectorName)
    [self performSelector:NSSelectorFromString(dispatcherSelectorName)];
}

- (void)webViewEstimatedProgressDidChange {
  if (!_isBeingDestroyed) {
    self.webStateImpl->SendChangeLoadProgress([_webView estimatedProgress]);
  }
}

- (void)webViewSecurityFeaturesDidChange {
  if (self.loadPhase == web::LOAD_REQUESTED) {
    // Do not update SSL Status for pending load. It will be updated in
    // |webView:didCommitNavigation:| callback.
    return;
  }
  [self updateSSLStatusForCurrentNavigationItem];
}

- (void)webViewLoadingStateDidChange {
  if ([_webView isLoading] || ![self isCurrentNavigationBackForward]) {
    return;
  }

  GURL webViewURL = net::GURLWithNSURL([_webView URL]);

  // For failed navigations, WKWebView will sometimes revert to the previous URL
  // before committing the current navigation or resetting the web view's
  // |isLoading| property to NO.  If this is the first navigation for the web
  // view, this will result in an empty URL.
  BOOL navigationWasCommitted = _loadPhase != web::LOAD_REQUESTED;
  if (!navigationWasCommitted &&
      (webViewURL.is_empty() || webViewURL == _documentURL)) {
    return;
  }

  if (!navigationWasCommitted && ![_pendingNavigationInfo cancelled]) {
    // A fast back navigation caused by pushState does not call
    // |didCommitNavigation:|, so signal page change explicitly.
    // TODO(crbug.com/659816): Because back-forward navigation is disabled for
    // pages that used push/replace State API, this code will not be executed.
    DCHECK_EQ(_documentURL.GetOrigin(), webViewURL.GetOrigin());
    BOOL isSameDocumentNavigation =
        [self isKVOChangePotentialSameDocumentNavigationToURL:webViewURL];
    [self setDocumentURL:webViewURL];
    [self webPageChanged];

    web::NavigationContextImpl* context =
        [self contextForPendingNavigationWithURL:webViewURL];
    // Same document navigation does not contain response headers.
    net::HttpResponseHeaders* headers =
        isSameDocumentNavigation ? nullptr
                                 : _webStateImpl->GetHttpResponseHeaders();
    context->SetResponseHeaders(headers);
    context->SetIsSameDocument(isSameDocumentNavigation);
    _webStateImpl->OnNavigationFinished(context);
  }

  [self updateSSLStatusForCurrentNavigationItem];

  // Fast back forward navigation may not call |didFinishNavigation:|, so
  // signal did finish navigation explicitly.
  if (_lastRegisteredRequestURL == _documentURL) {
    [self didFinishNavigation:nil];
  }
}

- (void)webViewTitleDidChange {
  // WKWebView's title becomes empty when the web process dies; ignore that
  // update.
  if (_webProcessIsDead) {
    DCHECK_EQ([_webView title].length, 0U);
    return;
  }

  web::WKNavigationState lastNavigationState =
      [_navigationStates lastAddedNavigationState];
  bool hasPendingNavigation =
      lastNavigationState == web::WKNavigationState::REQUESTED ||
      lastNavigationState == web::WKNavigationState::STARTED ||
      lastNavigationState == web::WKNavigationState::REDIRECTED;

  if (!hasPendingNavigation) {
    // Do not update the title if there is a navigation in progress because
    // there is no way to tell if KVO change fired for new or previous page.
    [self setNavigationItemTitle:[_webView title]];
  }
}

- (void)webViewURLDidChange {
  // TODO(stuartmorgan): Determine if there are any cases where this still
  // happens, and if so whether anything should be done when it does.
  if (![_webView URL]) {
    DVLOG(1) << "Received nil URL callback";
    return;
  }
  GURL URL(net::GURLWithNSURL([_webView URL]));
  // URL changes happen at three points:
  // 1) When a load starts; at this point, the load is provisional, and
  //    it should be ignored until it's committed, since the document/window
  //    objects haven't changed yet.
  // 2) When a non-document-changing URL change happens (hash change,
  //    history.pushState, etc.). This URL change happens instantly, so should
  //    be reported.
  // 3) When a navigation error occurs after provisional navigation starts,
  //    the URL reverts to the previous URL without triggering a new navigation.
  //
  // If |isLoading| is NO, then it must be case 2 or 3. If the last committed
  // URL (_documentURL) matches the current URL, assume that it is a revert from
  // navigation failure and do nothing. If the URL does not match, assume it is
  // a non-document-changing URL change, and handle accordingly.
  //
  // If |isLoading| is YES, then it could either be case 1, or it could be case
  // 2 on a page that hasn't finished loading yet. If it's possible that it
  // could be a same-page navigation (in which case there may not be any other
  // callback about the URL having changed), then check the actual page URL via
  // JavaScript. If the origin of the new URL matches the last committed URL,
  // then check window.location.href, and if it matches, trust it. The origin
  // check ensures that if a site somehow corrupts window.location.href it can't
  // do a redirect to a slow-loading target page while it is still loading to
  // spoof the origin. On a document-changing URL change, the
  // window.location.href will match the previous URL at this stage, not the web
  // view's current URL.
  if (![_webView isLoading]) {
    if (_documentURL == URL)
      return;
    [self URLDidChangeWithoutDocumentChange:URL];
  } else if ([self isKVOChangePotentialSameDocumentNavigationToURL:URL]) {
    [_webView
        evaluateJavaScript:@"window.location.href"
         completionHandler:^(id result, NSError* error) {
           // If the web view has gone away, or the location
           // couldn't be retrieved, abort.
           if (!_webView || ![result isKindOfClass:[NSString class]]) {
             return;
           }
           GURL JSURL(base::SysNSStringToUTF8(result));
           // Check that window.location matches the new URL. If
           // it does not, this is a document-changing URL change as
           // the window location would not have changed to the new
           // URL when the script was called.
           BOOL windowLocationMatchesNewURL = JSURL == URL;
           // Re-check origin in case navigaton has occured since
           // start of JavaScript evaluation.
           BOOL newURLOriginMatchesDocumentURLOrigin =
               _documentURL.GetOrigin() == URL.GetOrigin();
           // Check that the web view URL still matches the new URL.
           // TODO(crbug.com/563568): webViewURLMatchesNewURL check
           // may drop same document URL changes if pending URL
           // change occurs immediately after. Revisit heuristics to
           // prevent this.
           BOOL webViewURLMatchesNewURL =
               net::GURLWithNSURL([_webView URL]) == URL;
           // Check that the new URL is different from the current
           // document URL. If not, URL change should not be reported.
           BOOL URLDidChangeFromDocumentURL = URL != _documentURL;
           if (windowLocationMatchesNewURL &&
               newURLOriginMatchesDocumentURLOrigin &&
               webViewURLMatchesNewURL && URLDidChangeFromDocumentURL) {
             [self URLDidChangeWithoutDocumentChange:URL];
           }
         }];
  }
}

- (BOOL)isKVOChangePotentialSameDocumentNavigationToURL:(const GURL&)newURL {
  // If the origin changes, it can't be same-document.
  if (_documentURL.GetOrigin().is_empty() ||
      _documentURL.GetOrigin() != newURL.GetOrigin()) {
    return NO;
  }
  if (self.loadPhase == web::LOAD_REQUESTED) {
    // Normally LOAD_REQUESTED indicates that this is a regular, pending
    // navigation, but it can also happen during a fast-back navigation across
    // a hash change, so that case is potentially a same-document navigation.
    return web::GURLByRemovingRefFromGURL(newURL) ==
           web::GURLByRemovingRefFromGURL(_documentURL);
  }
  // If it passes all the checks above, it might be (but there's no guarantee
  // that it is).
  return YES;
}

- (void)URLDidChangeWithoutDocumentChange:(const GURL&)newURL {
  DCHECK(newURL == net::GURLWithNSURL([_webView URL]));
  DCHECK_EQ(_documentURL.host(), newURL.host());
  DCHECK(_documentURL != newURL);

  // If called during window.history.pushState or window.history.replaceState
  // JavaScript evaluation, only update the document URL. This callback does not
  // have any information about the state object and cannot create (or edit) the
  // navigation entry for this page change. Web controller will sync with
  // history changes when a window.history.didPushState or
  // window.history.didReplaceState message is received, which should happen in
  // the next runloop.
  //
  // Otherwise, simulate the whole delegate flow for a load (since the
  // superclass currently doesn't have a clean separation between URL changes
  // and document changes). Note that the order of these calls is important:
  // registering a load request logically comes before updating the document
  // URL, but also must come first since it uses state that is reset on URL
  // changes.

  // |newNavigationContext| only exists if this method has to create a new
  // context object.
  std::unique_ptr<web::NavigationContextImpl> newNavigationContext;
  if (!_changingHistoryState) {
    if ([self contextForPendingNavigationWithURL:newURL]) {
      // |loadWithParams:| was called with URL that has different fragment
      // comparing to the previous URL.
    } else {
      // This could be:
      //   1.) Renderer-initiated fragment change
      //   2.) Assigning same-origin URL to window.location
      //   3.) Incorrectly handled window.location.replace (crbug.com/307072)
      //   4.) Back-forward same document navigation
      newNavigationContext = [self registerLoadRequestForURL:newURL];

      // Use the current title for items created by same document navigations.
      auto* pendingItem = self.navigationManagerImpl->GetPendingItem();
      if (pendingItem)
        pendingItem->SetTitle(_webStateImpl->GetTitle());
    }
  }

  [self setDocumentURL:newURL];

  if (!_changingHistoryState) {
    [self didStartLoadingURL:_documentURL];

    // Pass either newly created context (if it exists) or context that already
    // existed before.
    web::NavigationContextImpl* navigationContext = newNavigationContext.get();
    if (!navigationContext)
      navigationContext = [self contextForPendingNavigationWithURL:newURL];
    navigationContext->SetIsSameDocument(true);
    _webStateImpl->OnNavigationFinished(navigationContext);

    [self updateSSLStatusForCurrentNavigationItem];
    [self didFinishNavigation:nil];
  }
}

- (web::NavigationContextImpl*)contextForPendingNavigationWithURL:
    (const GURL&)URL {
  for (id navigation in [_navigationStates pendingNavigations]) {
    if (navigation == [NSNull null]) {
      // null is a valid navigation object passed to WKNavigationDelegate
      // callbacks and represents window opening action.
      navigation = nil;
    }

    web::NavigationContextImpl* context =
        [_navigationStates contextForNavigation:navigation];
    if (context->GetUrl() == URL) {
      return context;
    }
  }
  return nullptr;
}

- (void)loadRequestForCurrentNavigationItem {
  DCHECK(_webView);
  DCHECK(self.currentNavItem);
  // If a load is kicked off on a WKWebView with a frame whose size is {0, 0} or
  // that has a negative dimension for a size, rendering issues occur that
  // manifest in erroneous scrolling and tap handling (crbug.com/574996,
  // crbug.com/577793).
  DCHECK_GT(CGRectGetWidth([_webView frame]), 0.0);
  DCHECK_GT(CGRectGetHeight([_webView frame]), 0.0);

  web::WKBackForwardListItemHolder* holder =
      [self currentBackForwardListItemHolder];
  BOOL repostedForm =
      [holder->http_method() isEqual:@"POST"] &&
      (holder->navigation_type() == WKNavigationTypeFormResubmitted ||
       holder->navigation_type() == WKNavigationTypeFormSubmitted);
  web::NavigationItemImpl* currentItem = self.currentNavItem;
  NSData* POSTData = currentItem->GetPostData();
  NSMutableURLRequest* request = [self requestForCurrentNavigationItem];

  // If the request has POST data and is not a repost form, configure and
  // run the POST request.
  if (POSTData.length && !repostedForm) {
    [request setHTTPMethod:@"POST"];
    [request setHTTPBody:POSTData];
    [request setAllHTTPHeaderFields:self.currentHTTPHeaders];
    GURL navigationURL =
        currentItem ? currentItem->GetURL() : GURL::EmptyGURL();
    std::unique_ptr<web::NavigationContextImpl> navigationContext =
        [self registerLoadRequestForURL:navigationURL
                               referrer:self.currentNavItemReferrer
                             transition:self.currentTransition];
    WKNavigation* navigation = [self loadPOSTRequest:request];
    [_navigationStates setContext:std::move(navigationContext)
                    forNavigation:navigation];
    return;
  }

  ProceduralBlock defaultNavigationBlock = ^{
    web::NavigationItem* item = self.currentNavItem;
    GURL navigationURL = item ? item->GetURL() : GURL::EmptyGURL();
    std::unique_ptr<web::NavigationContextImpl> navigationContext =
        [self registerLoadRequestForURL:navigationURL
                               referrer:self.currentNavItemReferrer
                             transition:self.currentTransition];
    WKNavigation* navigation = [self loadRequest:request];
    [_navigationStates setContext:std::move(navigationContext)
                    forNavigation:navigation];
    [self reportBackForwardNavigationTypeForFastNavigation:NO];
  };

  // When navigating via WKBackForwardListItem to pages created or updated by
  // calls to pushState() and replaceState(), sometimes web_bundle.js is not
  // injected correctly.  This means that calling window.history navigation
  // functions will invoke WKWebView's non-overridden implementations, causing a
  // mismatch between the WKBackForwardList and NavigationManager.
  // TODO(crbug.com/659816): Figure out how to prevent web_bundle.js injection
  // flake.
  if (currentItem->HasStateBeenReplaced() ||
      currentItem->IsCreatedFromPushState()) {
    defaultNavigationBlock();
    return;
  }

  // If there is no corresponding WKBackForwardListItem, or the item is not in
  // the current WKWebView's back-forward list, navigating using WKWebView API
  // is not possible. In this case, fall back to the default navigation
  // mechanism.
  if (!holder->back_forward_list_item() ||
      ![self isBackForwardListItemValid:holder->back_forward_list_item()]) {
    defaultNavigationBlock();
    return;
  }

  ProceduralBlock webViewNavigationBlock = ^{
    // If the current navigation URL is the same as the URL of the visible
    // page, that means the user requested a reload. |goToBackForwardListItem|
    // will be a no-op when it is passed the current back forward list item,
    // so |reload| must be explicitly called.
    web::NavigationItem* item = self.currentNavItem;
    GURL navigationURL = item ? item->GetURL() : GURL::EmptyGURL();
    std::unique_ptr<web::NavigationContextImpl> navigationContext =
        [self registerLoadRequestForURL:navigationURL
                               referrer:self.currentNavItemReferrer
                             transition:self.currentTransition];
    WKNavigation* navigation = nil;
    if (navigationURL == net::GURLWithNSURL([_webView URL])) {
      navigation = [_webView reload];
    } else {
      // |didCommitNavigation:| may not be called for fast navigation, so update
      // the navigation type now as it is already known.
      holder->set_navigation_type(WKNavigationTypeBackForward);
      navigation =
          [_webView goToBackForwardListItem:holder->back_forward_list_item()];
      [self reportBackForwardNavigationTypeForFastNavigation:YES];
    }
    [_navigationStates setState:web::WKNavigationState::REQUESTED
                  forNavigation:navigation];
    [_navigationStates setContext:std::move(navigationContext)
                    forNavigation:navigation];
  };

  // If the request is not a form submission or resubmission, or the user
  // doesn't need to confirm the load, then continue right away.

  if (!repostedForm || currentItem->ShouldSkipRepostFormConfirmation()) {
    webViewNavigationBlock();
    return;
  }

  // If the request is form submission or resubmission, then prompt the
  // user before proceeding.
  DCHECK(repostedForm);
  _webStateImpl->ShowRepostFormWarningDialog(
      base::BindBlock(^(bool shouldContinue) {
        if (shouldContinue)
          webViewNavigationBlock();
        else
          [self stopLoading];
      }));
}

- (void)reportBackForwardNavigationTypeForFastNavigation:(BOOL)isFast {
  // TODO(crbug.com/665189): Use NavigationManager::GetPendingItemIndex() once
  // it returns correct result.
  int pendingIndex = self.sessionController.pendingItemIndex;
  if (pendingIndex == -1) {
    // Pending navigation is not a back forward navigation.
    return;
  }

  BOOL isBack = pendingIndex < self.sessionController.lastCommittedItemIndex;
  BackForwardNavigationType type = BackForwardNavigationType::FAST_BACK;
  if (isBack) {
    type = isFast ? BackForwardNavigationType::FAST_BACK
                  : BackForwardNavigationType::SLOW_BACK;
  } else {
    type = isFast ? BackForwardNavigationType::FAST_FORWARD
                  : BackForwardNavigationType::SLOW_FORWARD;
  }

  UMA_HISTOGRAM_ENUMERATION(
      kUMAWKWebViewSlowFastBackForwardNavigationKey, type,
      BackForwardNavigationType::BACK_FORWARD_NAVIGATION_TYPE_COUNT);
}

#pragma mark -
#pragma mark Testing-Only Methods

- (void)injectWebViewContentView:(CRWWebViewContentView*)webViewContentView {
  [self removeWebViewAllowingCachedReconstruction:NO];

  _lastRegisteredRequestURL = _defaultURL;
  [_containerView displayWebViewContentView:webViewContentView];
  [self setWebView:static_cast<WKWebView*>(webViewContentView.webView)];
}

- (void)resetInjectedWebViewContentView {
  [self setWebView:nil];
  [self resetContainerView];
}

- (void)addObserver:(id<CRWWebControllerObserver>)observer {
  DCHECK(observer);
  if (!_observers) {
    // We don't want our observer set to block dealloc on the observers. For the
    // observer container, make an object compatible with NSMutableSet that does
    // not perform retain or release on the contained objects (weak references).
    CFSetCallBacks callbacks =
        {0, NULL, NULL, CFCopyDescription, CFEqual, CFHash};
    _observers.reset(base::mac::CFToNSCast(
        CFSetCreateMutable(kCFAllocatorDefault, 1, &callbacks)));
  }
  DCHECK(![_observers containsObject:observer]);
  [_observers addObject:observer];
  _observerBridges.push_back(base::MakeUnique<web::WebControllerObserverBridge>(
      observer, self.webStateImpl, self));

  if ([observer respondsToSelector:@selector(setWebViewProxy:controller:)])
    [observer setWebViewProxy:_webViewProxy controller:self];
}

- (void)removeObserver:(id<CRWWebControllerObserver>)observer {
  DCHECK([_observers containsObject:observer]);
  [_observers removeObject:observer];
  // Remove the associated WebControllerObserverBridge.
  auto it = std::find_if(
      _observerBridges.begin(), _observerBridges.end(),
      [observer](
          const std::unique_ptr<web::WebControllerObserverBridge>& bridge) {
        return bridge->web_controller_observer() == observer;
      });
  DCHECK(it != _observerBridges.end());
  _observerBridges.erase(it);
}

- (NSUInteger)observerCount {
  DCHECK_EQ(_observerBridges.size(), [_observers count]);
  return [_observers count];
}

- (NSString*)referrerFromNavigationAction:(WKNavigationAction*)action {
  return [action.request valueForHTTPHeaderField:kReferrerHeaderName];
}

@end
