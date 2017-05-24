// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SUBRESOURCE_FILTER_CHROME_SUBRESOURCE_FILTER_CLIENT_H_
#define CHROME_BROWSER_SUBRESOURCE_FILTER_CHROME_SUBRESOURCE_FILTER_CLIENT_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/macros.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/subresource_filter/content/browser/subresource_filter_client.h"
#include "content/public/browser/web_contents_user_data.h"

class GURL;
class SubresourceFilterContentSettingsManager;

namespace content {
class NavigationHandle;
class NavigationThrottle;
class WebContents;
}  // namespace content

// This enum backs a histogram. Make sure new elements are only added to the
// end. Keep histograms.xml up to date with any changes.
enum SubresourceFilterAction {
  // Main frame navigation to a different document.
  kActionNavigationStarted = 0,

  // Standard UI shown. On Desktop this is in the omnibox,
  // On Android, it is an infobar.
  kActionUIShown,

  // On Desktop, this is a bubble. On Android it is an
  // expanded infobar.
  kActionDetailsShown,

  // TODO(csharrison): Log this once the link is in place.
  kActionClickedLearnMore,

  // Content settings:
  //
  // Blocked => The subresource filter will block resources.
  // Allowed => The subresource filter will not block resources.
  //
  // Content setting updated automatically via the standard UI.
  kActionContentSettingsAllowedFromUI,

  // Content settings which target specific origins (e.g. no wildcards). These
  // updates do not include updates from the main UI.
  kActionContentSettingsBlocked,
  kActionContentSettingsAllowed,

  // Global settings.
  kActionContentSettingsBlockedGlobal,
  kActionContentSettingsAllowedGlobal,

  // A wildcard update. The current content settings API makes this a bit
  // difficult to see whether it is Block or Allow. This should not be a huge
  // problem because this can only be changed from the settings UI, which is
  // relatively rare. See crbug.com/706061.
  //
  // DEPRECATED: The site settings page uses read-only-lists for exceptions, so
  // users can't add arbitrary patterns.
  kActionContentSettingsWildcardUpdate,

  // The UI was suppressed due to "smart" logic which tries not to spam the UI
  // on navigations on the same origin within a certain time.
  kActionUISuppressed,

  // Subresources were explicitly allowed via manual content setting changes
  // while smart UI was suppressing the UI. Potentially indicates that the smart
  // UI is too aggressive if this happens frequently. This is reported
  // alongside kActionContentSettingsAllowed if the UI is currently in
  // suppressed mode.
  kActionContentSettingsAllowedWhileUISuppressed,

  kActionLastEntry
};

// Chrome implementation of SubresourceFilterClient.
// TODO(csharrison): Make this a WebContentsObserver and own the throttle
// manager directly.
class ChromeSubresourceFilterClient
    : public content::WebContentsUserData<ChromeSubresourceFilterClient>,
      public subresource_filter::SubresourceFilterClient {
 public:
  explicit ChromeSubresourceFilterClient(content::WebContents* web_contents);
  ~ChromeSubresourceFilterClient() override;

  void MaybeAppendNavigationThrottles(
      content::NavigationHandle* navigation_handle,
      std::vector<std::unique_ptr<content::NavigationThrottle>>* throttles);

  void OnReloadRequested();

  // SubresourceFilterClient:
  void ToggleNotificationVisibility(bool visibility) override;
  bool OnPageActivationComputed(content::NavigationHandle* navigation_handle,
                                bool activated) override;
  void WhitelistInCurrentWebContents(const GURL& url) override;
  subresource_filter::VerifiedRulesetDealer::Handle* GetRulesetDealer()
      override;

  bool did_show_ui_for_navigation() const {
    return did_show_ui_for_navigation_;
  }

  static void LogAction(SubresourceFilterAction action);

 private:
  void WhitelistByContentSettings(const GURL& url);
  std::set<std::string> whitelisted_hosts_;

  // Owned by the profile.
  SubresourceFilterContentSettingsManager* settings_manager_;

  content::WebContents* web_contents_;
  bool did_show_ui_for_navigation_;

  DISALLOW_COPY_AND_ASSIGN(ChromeSubresourceFilterClient);
};

#endif  // CHROME_BROWSER_SUBRESOURCE_FILTER_CHROME_SUBRESOURCE_FILTER_CLIENT_H_
