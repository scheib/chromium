// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_PUBLIC_TEST_FAKES_TEST_WEB_STATE_H_
#define IOS_WEB_PUBLIC_TEST_FAKES_TEST_WEB_STATE_H_

#include <stdint.h>

#include <string>

#include "base/observer_list.h"
#include "base/strings/string16.h"
#import "ios/web/public/navigation_manager.h"
#include "ios/web/public/web_state/url_verification_constants.h"
#import "ios/web/public/web_state/web_state.h"
#include "ios/web/public/web_state/web_state_observer.h"
#include "url/gurl.h"

namespace web {

// Minimal implementation of WebState, to be used in tests.
class TestWebState : public WebState {
 public:
  TestWebState();
  ~TestWebState() override;

  // WebState implementation.
  WebStateDelegate* GetDelegate() override;
  void SetDelegate(WebStateDelegate* delegate) override;
  bool IsWebUsageEnabled() const override;
  void SetWebUsageEnabled(bool enabled) override;
  bool ShouldSuppressDialogs() const override;
  void SetShouldSuppressDialogs(bool should_suppress) override;
  UIView* GetView() override;
  BrowserState* GetBrowserState() const override;
  void OpenURL(const OpenURLParams& params) override {}
  void Stop() override {}
  const NavigationManager* GetNavigationManager() const override;
  NavigationManager* GetNavigationManager() override;
  const SessionCertificatePolicyCache* GetSessionCertificatePolicyCache()
      const override;
  SessionCertificatePolicyCache* GetSessionCertificatePolicyCache() override;
  CRWSessionStorage* BuildSessionStorage() override;
  CRWJSInjectionReceiver* GetJSInjectionReceiver() const override;
  void ExecuteJavaScript(const base::string16& javascript) override;
  void ExecuteJavaScript(const base::string16& javascript,
                         const JavaScriptResultCallback& callback) override;
  const std::string& GetContentsMimeType() const override;
  const std::string& GetContentLanguageHeader() const override;
  bool ContentIsHTML() const override;
  const base::string16& GetTitle() const override;
  bool IsLoading() const override;
  double GetLoadingProgress() const override;
  bool IsBeingDestroyed() const override;
  const GURL& GetVisibleURL() const override;
  const GURL& GetLastCommittedURL() const override;
  GURL GetCurrentURL(URLVerificationTrustLevel* trust_level) const override;
  void ShowTransientContentView(CRWContentView* content_view) override;
  void ClearTransientContentView();
  void AddScriptCommandCallback(const ScriptCommandCallback& callback,
                                const std::string& command_prefix) override {}
  void RemoveScriptCommandCallback(const std::string& command_prefix) override {
  }
  CRWWebViewProxyType GetWebViewProxy() const override;
  bool IsShowingWebInterstitial() const override;
  WebInterstitial* GetWebInterstitial() const override;
  void OnPasswordInputShownOnHttp() override {}
  void OnCreditCardInputShownOnHttp() override {}

  void AddObserver(WebStateObserver* observer) override;

  void RemoveObserver(WebStateObserver* observer) override;

  void AddPolicyDecider(WebStatePolicyDecider* decider) override {}
  void RemovePolicyDecider(WebStatePolicyDecider* decider) override {}
  WebStateInterfaceProvider* GetWebStateInterfaceProvider() override;
  bool HasOpener() const override;
  base::WeakPtr<WebState> AsWeakPtr() override;

  // Setters for test data.
  void SetBrowserState(BrowserState* browser_state);
  void SetContentIsHTML(bool content_is_html);
  void SetLoading(bool is_loading);
  void SetCurrentURL(const GURL& url);
  void SetTrustLevel(URLVerificationTrustLevel trust_level);
  void SetNavigationManager(
      std::unique_ptr<NavigationManager> navigation_manager);
  void SetView(UIView* view);

  // Getters for test data.
  CRWContentView* GetTransientContentView();

  // Notifier for tests.
  void OnPageLoaded(PageLoadCompletionStatus load_completion_status);
  void OnNavigationStarted(NavigationContext* navigation_context);
  void OnRenderProcessGone();

 private:
  BrowserState* browser_state_;
  bool web_usage_enabled_;
  bool is_loading_;
  base::scoped_nsobject<CRWContentView> transient_content_view_;
  GURL url_;
  base::string16 title_;
  URLVerificationTrustLevel trust_level_;
  bool content_is_html_;
  std::string mime_type_;
  std::string content_language_;
  std::unique_ptr<NavigationManager> navigation_manager_;
  base::scoped_nsobject<UIView> view_;

  // A list of observers notified when page state changes. Weak references.
  base::ObserverList<WebStateObserver, true> observers_;
};

}  // namespace web

#endif  // IOS_WEB_PUBLIC_TEST_FAKES_TEST_WEB_STATE_H_
