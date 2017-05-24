// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/web_state/web_state_impl.h"

#include <stddef.h>
#include <stdint.h>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/sys_string_conversions.h"
#import "ios/web/interstitials/web_interstitial_impl.h"
#import "ios/web/navigation/crw_session_controller.h"
#import "ios/web/navigation/navigation_item_impl.h"
#import "ios/web/navigation/session_storage_builder.h"
#include "ios/web/public/browser_state.h"
#import "ios/web/public/crw_session_storage.h"
#import "ios/web/public/java_script_dialog_presenter.h"
#import "ios/web/public/navigation_item.h"
#include "ios/web/public/url_util.h"
#import "ios/web/public/web_client.h"
#import "ios/web/public/web_state/context_menu_params.h"
#import "ios/web/public/web_state/ui/crw_content_view.h"
#import "ios/web/public/web_state/web_state_delegate.h"
#include "ios/web/public/web_state/web_state_interface_provider.h"
#include "ios/web/public/web_state/web_state_observer.h"
#import "ios/web/public/web_state/web_state_policy_decider.h"
#include "ios/web/public/web_thread.h"
#include "ios/web/public/webui/web_ui_ios_controller.h"
#include "ios/web/web_state/global_web_state_event_tracker.h"
#import "ios/web/web_state/navigation_context_impl.h"
#import "ios/web/web_state/session_certificate_policy_cache_impl.h"
#import "ios/web/web_state/ui/crw_web_controller.h"
#import "ios/web/web_state/ui/crw_web_controller_container_view.h"
#include "ios/web/webui/web_ui_ios_controller_factory_registry.h"
#include "ios/web/webui/web_ui_ios_impl.h"
#include "net/http/http_response_headers.h"

namespace web {

/* static */
std::unique_ptr<WebState> WebState::Create(const CreateParams& params) {
  std::unique_ptr<WebStateImpl> web_state(new WebStateImpl(params));

  // Initialize the new session.
  web_state->GetNavigationManagerImpl().InitializeSession();

  // TODO(crbug.com/703565): remove std::move() once Xcode 9.0+ is required.
  return std::move(web_state);
}

/* static */
std::unique_ptr<WebState> WebState::CreateWithStorageSession(
    const CreateParams& params,
    CRWSessionStorage* session_storage) {
  DCHECK(session_storage);
  std::unique_ptr<WebStateImpl> web_state(
      new WebStateImpl(params, session_storage));

  // TODO(crbug.com/703565): remove std::move() once Xcode 9.0+ is required.
  return std::move(web_state);
}

WebStateImpl::WebStateImpl(const CreateParams& params)
    : WebStateImpl(params, nullptr) {}

WebStateImpl::WebStateImpl(const CreateParams& params,
                           CRWSessionStorage* session_storage)
    : delegate_(nullptr),
      is_loading_(false),
      is_being_destroyed_(false),
      web_controller_(nil),
      interstitial_(nullptr),
      created_with_opener_(params.created_with_opener),
      weak_factory_(this) {
  // Create or deserialize the NavigationManager.
  if (session_storage) {
    SessionStorageBuilder session_storage_builder;
    session_storage_builder.ExtractSessionState(this, session_storage);
  } else {
    navigation_manager_ = base::MakeUnique<NavigationManagerImpl>();
    certificate_policy_cache_ =
        base::MakeUnique<SessionCertificatePolicyCacheImpl>();
  }
  navigation_manager_->SetDelegate(this);
  navigation_manager_->SetBrowserState(params.browser_state);
  // Send creation event and create the web controller.
  GlobalWebStateEventTracker::GetInstance()->OnWebStateCreated(this);
  web_controller_.reset([[CRWWebController alloc] initWithWebState:this]);
}

WebStateImpl::~WebStateImpl() {
  [web_controller_ close];
  is_being_destroyed_ = true;

  // WebUI depends on web state so it must be destroyed first in case any WebUI
  // implementations depends on accessing web state during destruction.
  ClearWebUI();

  for (auto& observer : observers_)
    observer.WebStateDestroyed();
  for (auto& observer : observers_)
    observer.ResetWebState();
  for (auto& observer : policy_deciders_)
    observer.WebStateDestroyed();
  for (auto& observer : policy_deciders_)
    observer.ResetWebState();
  DCHECK(script_command_callbacks_.empty());
  SetDelegate(nullptr);
}

WebStateDelegate* WebStateImpl::GetDelegate() {
  return delegate_;
}

void WebStateImpl::SetDelegate(WebStateDelegate* delegate) {
  if (delegate == delegate_)
    return;
  if (delegate_)
    delegate_->Detach(this);
  delegate_ = delegate;
  if (delegate_) {
    delegate_->Attach(this);
  }
}

void WebStateImpl::AddObserver(WebStateObserver* observer) {
  DCHECK(!observers_.HasObserver(observer));
  observers_.AddObserver(observer);
}

void WebStateImpl::RemoveObserver(WebStateObserver* observer) {
  DCHECK(observers_.HasObserver(observer));
  observers_.RemoveObserver(observer);
}

void WebStateImpl::AddPolicyDecider(WebStatePolicyDecider* decider) {
  // Despite the name, ObserverList is actually generic, so it is used for
  // deciders. This makes the call here odd looking, but it's really just
  // managing the list, not setting observers on deciders.
  DCHECK(!policy_deciders_.HasObserver(decider));
  policy_deciders_.AddObserver(decider);
}

void WebStateImpl::RemovePolicyDecider(WebStatePolicyDecider* decider) {
  // Despite the name, ObserverList is actually generic, so it is used for
  // deciders. This makes the call here odd looking, but it's really just
  // managing the list, not setting observers on deciders.
  DCHECK(policy_deciders_.HasObserver(decider));
  policy_deciders_.RemoveObserver(decider);
}

bool WebStateImpl::Configured() const {
  return web_controller_ != nil;
}

CRWWebController* WebStateImpl::GetWebController() {
  return web_controller_;
}

void WebStateImpl::SetWebController(CRWWebController* web_controller) {
  [web_controller_ close];
  web_controller_.reset([web_controller retain]);
}

void WebStateImpl::OnTitleChanged() {
  for (auto& observer : observers_)
    observer.TitleWasSet();
}

void WebStateImpl::OnVisibleSecurityStateChange() {
  for (auto& observer : observers_)
    observer.DidChangeVisibleSecurityState();
}

void WebStateImpl::OnDialogSuppressed() {
  DCHECK(ShouldSuppressDialogs());
  for (auto& observer : observers_)
    observer.DidSuppressDialog();
}

void WebStateImpl::OnRenderProcessGone() {
  for (auto& observer : observers_)
    observer.RenderProcessGone();
}

bool WebStateImpl::OnScriptCommandReceived(const std::string& command,
                                           const base::DictionaryValue& value,
                                           const GURL& url,
                                           bool user_is_interacting) {
  size_t dot_position = command.find_first_of('.');
  if (dot_position == 0 || dot_position == std::string::npos)
    return false;

  std::string prefix = command.substr(0, dot_position);
  auto it = script_command_callbacks_.find(prefix);
  if (it == script_command_callbacks_.end())
    return false;

  return it->second.Run(value, url, user_is_interacting);
}

void WebStateImpl::SetIsLoading(bool is_loading) {
  if (is_loading == is_loading_)
    return;

  is_loading_ = is_loading;

  if (is_loading) {
    for (auto& observer : observers_)
      observer.DidStartLoading();
  } else {
    for (auto& observer : observers_)
      observer.DidStopLoading();
  }
}

bool WebStateImpl::IsLoading() const {
  return is_loading_;
}

double WebStateImpl::GetLoadingProgress() const {
  return [web_controller_ loadingProgress];
}

bool WebStateImpl::IsBeingDestroyed() const {
  return is_being_destroyed_;
}

void WebStateImpl::OnPageLoaded(const GURL& url, bool load_success) {
  PageLoadCompletionStatus load_completion_status =
      load_success ? PageLoadCompletionStatus::SUCCESS
                   : PageLoadCompletionStatus::FAILURE;
  for (auto& observer : observers_)
    observer.PageLoaded(load_completion_status);
}

void WebStateImpl::OnFormActivityRegistered(const std::string& form_name,
                                            const std::string& field_name,
                                            const std::string& type,
                                            const std::string& value,
                                            bool input_missing) {
  for (auto& observer : observers_) {
    observer.FormActivityRegistered(form_name, field_name, type, value,
                                    input_missing);
  }
}

void WebStateImpl::OnFaviconUrlUpdated(
    const std::vector<FaviconURL>& candidates) {
  for (auto& observer : observers_)
    observer.FaviconUrlUpdated(candidates);
}

void WebStateImpl::OnDocumentSubmitted(const std::string& form_name,
                                       bool user_initiated) {
  for (auto& observer : observers_)
    observer.DocumentSubmitted(form_name, user_initiated);
}

NavigationManagerImpl& WebStateImpl::GetNavigationManagerImpl() {
  return *navigation_manager_;
}

const NavigationManagerImpl& WebStateImpl::GetNavigationManagerImpl() const {
  return *navigation_manager_;
}

const SessionCertificatePolicyCacheImpl&
WebStateImpl::GetSessionCertificatePolicyCacheImpl() const {
  return *certificate_policy_cache_;
}

SessionCertificatePolicyCacheImpl&
WebStateImpl::GetSessionCertificatePolicyCacheImpl() {
  return *certificate_policy_cache_;
}

void WebStateImpl::CreateWebUI(const GURL& url) {
  web_ui_ = CreateWebUIIOS(url);
}

void WebStateImpl::ClearWebUI() {
  web_ui_.reset();
}

bool WebStateImpl::HasWebUI() {
  return !!web_ui_;
}

void WebStateImpl::ProcessWebUIMessage(const GURL& source_url,
                                       const std::string& message,
                                       const base::ListValue& args) {
  if (web_ui_)
    web_ui_->ProcessWebUIIOSMessage(source_url, message, args);
}

void WebStateImpl::LoadWebUIHtml(const base::string16& html, const GURL& url) {
  CHECK(web::GetWebClient()->IsAppSpecificURL(url));
  [web_controller_ loadHTML:base::SysUTF16ToNSString(html)
          forAppSpecificURL:url];
}

const base::string16& WebStateImpl::GetTitle() const {
  // TODO(stuartmorgan): Implement the NavigationManager logic necessary to
  // match the WebContents implementation of this method.
  DCHECK(Configured());
  web::NavigationItem* item = navigation_manager_->GetLastCommittedItem();
  return item ? item->GetTitleForDisplay() : empty_string16_;
}

void WebStateImpl::ShowTransientContentView(CRWContentView* content_view) {
  DCHECK(Configured());
  DCHECK(content_view);
  DCHECK(content_view.scrollView);
  [web_controller_ showTransientContentView:content_view];
}

bool WebStateImpl::IsShowingWebInterstitial() const {
  // Technically we could have |interstitial_| set but its view isn't
  // being displayed, but there's no code path where that could occur.
  return interstitial_ != nullptr;
}

WebInterstitial* WebStateImpl::GetWebInterstitial() const {
  return interstitial_;
}

void WebStateImpl::OnPasswordInputShownOnHttp() {
  [web_controller_ didShowPasswordInputOnHTTP];
}

void WebStateImpl::OnCreditCardInputShownOnHttp() {
  [web_controller_ didShowCreditCardInputOnHTTP];
}

net::HttpResponseHeaders* WebStateImpl::GetHttpResponseHeaders() const {
  return http_response_headers_.get();
}

void WebStateImpl::OnHttpResponseHeadersReceived(
    net::HttpResponseHeaders* response_headers,
    const GURL& resource_url) {
  // Store the headers in a map until the page finishes loading, as we do not
  // know which URL corresponds to the main page yet.
  // Remove the hash (if any) as it is sometimes altered by in-page navigations.
  // TODO(crbug/551677): Simplify all this logic once UIWebView is no longer
  // supported.
  const GURL& url = GURLByRemovingRefFromGURL(resource_url);
  response_headers_map_[url] = response_headers;
}

void WebStateImpl::UpdateHttpResponseHeaders(const GURL& url) {
  // Reset the state.
  http_response_headers_ = NULL;
  mime_type_.clear();
  content_language_header_.clear();

  // Discard all the response headers except the ones for |main_page_url|.
  auto it = response_headers_map_.find(GURLByRemovingRefFromGURL(url));
  if (it != response_headers_map_.end())
    http_response_headers_ = it->second;
  response_headers_map_.clear();

  if (!http_response_headers_.get())
    return;

  // MIME type.
  std::string mime_type;
  http_response_headers_->GetMimeType(&mime_type);
  mime_type_ = mime_type;

  // Content-Language
  std::string content_language;
  http_response_headers_->GetNormalizedHeader("content-language",
                                              &content_language);
  // Remove everything after the comma ',' if any.
  size_t comma_index = content_language.find_first_of(',');
  if (comma_index != std::string::npos)
    content_language.resize(comma_index);
  content_language_header_ = content_language;
}

void WebStateImpl::ShowWebInterstitial(WebInterstitialImpl* interstitial) {
  DCHECK(Configured());
  DCHECK(interstitial);
  interstitial_ = interstitial;
  ShowTransientContentView(interstitial_->GetContentView());
}

void WebStateImpl::ClearTransientContentView() {
  if (interstitial_) {
    // Store the currently displayed interstitial in a local variable and reset
    // |interstitial_| early.  This is to prevent an infinite loop, as
    // |DontProceed()| internally calls |ClearTransientContentView()|.
    web::WebInterstitial* interstitial = interstitial_;
    interstitial_ = nullptr;
    interstitial->DontProceed();
    // Don't access |interstitial| after calling |DontProceed()|, as it triggers
    // deletion.
    for (auto& observer : observers_)
      observer.InterstitialDismissed();
  }
  [web_controller_ clearTransientContentView];
}

void WebStateImpl::SendChangeLoadProgress(double progress) {
  for (auto& observer : observers_)
    observer.LoadProgressChanged(progress);
}

bool WebStateImpl::HandleContextMenu(const web::ContextMenuParams& params) {
  if (delegate_) {
    return delegate_->HandleContextMenu(this, params);
  }
  return false;
}

void WebStateImpl::ShowRepostFormWarningDialog(
    const base::Callback<void(bool)>& callback) {
  if (delegate_) {
    delegate_->ShowRepostFormWarningDialog(this, callback);
  } else {
    callback.Run(true);
  }
}

void WebStateImpl::RunJavaScriptDialog(
    const GURL& origin_url,
    JavaScriptDialogType javascript_dialog_type,
    NSString* message_text,
    NSString* default_prompt_text,
    const DialogClosedCallback& callback) {
  JavaScriptDialogPresenter* presenter =
      delegate_ ? delegate_->GetJavaScriptDialogPresenter(this) : nullptr;
  if (!presenter) {
    callback.Run(false, nil);
    return;
  }
  presenter->RunJavaScriptDialog(this, origin_url, javascript_dialog_type,
                                 message_text, default_prompt_text, callback);
}

WebState* WebStateImpl::CreateNewWebState(const GURL& url,
                                          const GURL& opener_url,
                                          bool initiated_by_user) {
  if (delegate_) {
    return delegate_->CreateNewWebState(this, url, opener_url,
                                        initiated_by_user);
  }
  return nullptr;
}

void WebStateImpl::CloseWebState() {
  if (delegate_) {
    delegate_->CloseWebState(this);
  }
}

void WebStateImpl::OnAuthRequired(
    NSURLProtectionSpace* protection_space,
    NSURLCredential* proposed_credential,
    const WebStateDelegate::AuthCallback& callback) {
  if (delegate_) {
    delegate_->OnAuthRequired(this, protection_space, proposed_credential,
                              callback);
  } else {
    callback.Run(nil, nil);
  }
}

void WebStateImpl::CancelDialogs() {
  if (delegate_) {
    JavaScriptDialogPresenter* presenter =
        delegate_->GetJavaScriptDialogPresenter(this);
    if (presenter) {
      presenter->CancelDialogs(this);
    }
  }
}

std::unique_ptr<web::WebUIIOS> WebStateImpl::CreateWebUIIOS(const GURL& url) {
  WebUIIOSControllerFactory* factory =
      WebUIIOSControllerFactoryRegistry::GetInstance();
  if (!factory)
    return nullptr;
  std::unique_ptr<web::WebUIIOS> web_ui = base::MakeUnique<WebUIIOSImpl>(this);
  auto controller = factory->CreateWebUIIOSControllerForURL(web_ui.get(), url);
  if (!controller)
    return nullptr;

  web_ui->SetController(std::move(controller));
  return web_ui;
}

void WebStateImpl::SetContentsMimeType(const std::string& mime_type) {
  mime_type_ = mime_type;
}

bool WebStateImpl::ShouldAllowRequest(NSURLRequest* request) {
  for (auto& policy_decider : policy_deciders_) {
    if (!policy_decider.ShouldAllowRequest(request))
      return false;
  }
  return true;
}

bool WebStateImpl::ShouldAllowResponse(NSURLResponse* response) {
  for (auto& policy_decider : policy_deciders_) {
    if (!policy_decider.ShouldAllowResponse(response))
      return false;
  }
  return true;
}

#pragma mark - RequestTracker management

WebStateInterfaceProvider* WebStateImpl::GetWebStateInterfaceProvider() {
  if (!web_state_interface_provider_) {
    web_state_interface_provider_ =
        base::MakeUnique<WebStateInterfaceProvider>();
  }
  return web_state_interface_provider_.get();
}

base::WeakPtr<WebState> WebStateImpl::AsWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

#pragma mark - WebState implementation

bool WebStateImpl::IsWebUsageEnabled() const {
  return [web_controller_ webUsageEnabled];
}

void WebStateImpl::SetWebUsageEnabled(bool enabled) {
  [web_controller_ setWebUsageEnabled:enabled];
}

bool WebStateImpl::ShouldSuppressDialogs() const {
  return [web_controller_ shouldSuppressDialogs];
}

void WebStateImpl::SetShouldSuppressDialogs(bool should_suppress) {
  [web_controller_ setShouldSuppressDialogs:should_suppress];
}

UIView* WebStateImpl::GetView() {
  return [web_controller_ view];
}

BrowserState* WebStateImpl::GetBrowserState() const {
  return navigation_manager_->GetBrowserState();
}

void WebStateImpl::OpenURL(const WebState::OpenURLParams& params) {
  DCHECK(Configured());
  ClearTransientContentView();
  if (delegate_)
    delegate_->OpenURLFromWebState(this, params);
}

void WebStateImpl::Stop() {
  [web_controller_ stopLoading];
}

const NavigationManager* WebStateImpl::GetNavigationManager() const {
  return &GetNavigationManagerImpl();
}

NavigationManager* WebStateImpl::GetNavigationManager() {
  return &GetNavigationManagerImpl();
}

const SessionCertificatePolicyCache*
WebStateImpl::GetSessionCertificatePolicyCache() const {
  return &GetSessionCertificatePolicyCacheImpl();
}

SessionCertificatePolicyCache*
WebStateImpl::GetSessionCertificatePolicyCache() {
  return &GetSessionCertificatePolicyCacheImpl();
}

CRWSessionStorage* WebStateImpl::BuildSessionStorage() {
  [web_controller_ recordStateInHistory];
  SessionStorageBuilder session_storage_builder;
  return session_storage_builder.BuildStorage(this);
}

CRWJSInjectionReceiver* WebStateImpl::GetJSInjectionReceiver() const {
  return [web_controller_ jsInjectionReceiver];
}

void WebStateImpl::ExecuteJavaScript(const base::string16& javascript) {
  [web_controller_ executeJavaScript:base::SysUTF16ToNSString(javascript)
                   completionHandler:nil];
}

void WebStateImpl::ExecuteJavaScript(const base::string16& javascript,
                                     const JavaScriptResultCallback& callback) {
  JavaScriptResultCallback stackCallback = callback;
  [web_controller_ executeJavaScript:base::SysUTF16ToNSString(javascript)
                   completionHandler:^(id value, NSError* error) {
                     if (error) {
                       DLOG(WARNING)
                           << "Script execution has failed: "
                           << base::SysNSStringToUTF16(
                                  error.userInfo[NSLocalizedDescriptionKey]);
                     }
                     stackCallback.Run(ValueResultFromWKResult(value).get());
                   }];
}

const std::string& WebStateImpl::GetContentLanguageHeader() const {
  return content_language_header_;
}

const std::string& WebStateImpl::GetContentsMimeType() const {
  return mime_type_;
}

bool WebStateImpl::ContentIsHTML() const {
  return [web_controller_ contentIsHTML];
}

const GURL& WebStateImpl::GetVisibleURL() const {
  web::NavigationItem* item = navigation_manager_->GetVisibleItem();
  return item ? item->GetVirtualURL() : GURL::EmptyGURL();
}

const GURL& WebStateImpl::GetLastCommittedURL() const {
  web::NavigationItem* item = navigation_manager_->GetLastCommittedItem();
  return item ? item->GetVirtualURL() : GURL::EmptyGURL();
}

GURL WebStateImpl::GetCurrentURL(URLVerificationTrustLevel* trust_level) const {
  GURL URL = [web_controller_ currentURLWithTrustLevel:trust_level];
  bool equalURLs = web::GURLByRemovingRefFromGURL(URL) ==
                   web::GURLByRemovingRefFromGURL(GetLastCommittedURL());
  DCHECK(equalURLs);
  UMA_HISTOGRAM_BOOLEAN("Web.CurrentURLEqualsLastCommittedURL", equalURLs);
  return URL;
}

void WebStateImpl::AddScriptCommandCallback(
    const ScriptCommandCallback& callback,
    const std::string& command_prefix) {
  DCHECK(!command_prefix.empty());
  DCHECK(command_prefix.find_first_of('.') == std::string::npos);
  DCHECK(script_command_callbacks_.find(command_prefix) ==
         script_command_callbacks_.end());
  script_command_callbacks_[command_prefix] = callback;
}

void WebStateImpl::RemoveScriptCommandCallback(
    const std::string& command_prefix) {
  DCHECK(script_command_callbacks_.find(command_prefix) !=
         script_command_callbacks_.end());
  script_command_callbacks_.erase(command_prefix);
}

id<CRWWebViewProxy> WebStateImpl::GetWebViewProxy() const {
  return [web_controller_ webViewProxy];
}

bool WebStateImpl::HasOpener() const {
  return created_with_opener_;
}

void WebStateImpl::OnNavigationStarted(web::NavigationContext* context) {
  for (auto& observer : observers_)
    observer.DidStartNavigation(context);
}

void WebStateImpl::OnNavigationFinished(web::NavigationContext* context) {
  for (auto& observer : observers_)
    observer.DidFinishNavigation(context);
}

#pragma mark - NavigationManagerDelegate implementation

void WebStateImpl::GoToIndex(int index) {
  [web_controller_ goToItemAtIndex:index];
}

void WebStateImpl::LoadURLWithParams(
    const NavigationManager::WebLoadParams& params) {
  [web_controller_ loadWithParams:params];
}

void WebStateImpl::Reload() {
  [web_controller_ reload];
}

void WebStateImpl::OnNavigationItemsPruned(size_t pruned_item_count) {
  for (auto& observer : observers_)
    observer.NavigationItemsPruned(pruned_item_count);
}

void WebStateImpl::OnNavigationItemChanged() {
  for (auto& observer : observers_)
    observer.NavigationItemChanged();
}

void WebStateImpl::OnNavigationItemCommitted(
    const LoadCommittedDetails& load_details) {
  for (auto& observer : observers_)
    observer.NavigationItemCommitted(load_details);
}

WebState* WebStateImpl::GetWebState() {
  return this;
}

}  // namespace web
