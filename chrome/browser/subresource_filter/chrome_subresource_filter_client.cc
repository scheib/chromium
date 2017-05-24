// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/subresource_filter/chrome_subresource_filter_client.h"

#include <string>
#include <utility>

#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/tab_specific_content_settings.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/subresource_filter/subresource_filter_content_settings_manager.h"
#include "chrome/browser/subresource_filter/subresource_filter_profile_context.h"
#include "chrome/browser/subresource_filter/subresource_filter_profile_context_factory.h"
#include "chrome/browser/ui/android/content_settings/subresource_filter_infobar_delegate.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/safe_browsing_db/database_manager.h"
#include "components/safe_browsing_db/v4_feature_list.h"
#include "components/subresource_filter/content/browser/content_ruleset_service.h"
#include "components/subresource_filter/content/browser/content_subresource_filter_driver_factory.h"
#include "components/subresource_filter/content/browser/subresource_filter_safe_browsing_activation_throttle.h"
#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "components/subresource_filter/core/common/activation_level.h"
#include "components/subresource_filter/core/common/activation_scope.h"
#include "components/subresource_filter/core/common/activation_state.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"

DEFINE_WEB_CONTENTS_USER_DATA_KEY(ChromeSubresourceFilterClient);

ChromeSubresourceFilterClient::ChromeSubresourceFilterClient(
    content::WebContents* web_contents)
    : web_contents_(web_contents), did_show_ui_for_navigation_(false) {
  DCHECK(web_contents);
  SubresourceFilterProfileContext* context =
      SubresourceFilterProfileContextFactory::GetForProfile(
          Profile::FromBrowserContext(web_contents_->GetBrowserContext()));
  settings_manager_ = context->settings_manager();
  subresource_filter::ContentSubresourceFilterDriverFactory::
      CreateForWebContents(web_contents, this);
}

ChromeSubresourceFilterClient::~ChromeSubresourceFilterClient() {}

void ChromeSubresourceFilterClient::MaybeAppendNavigationThrottles(
    content::NavigationHandle* navigation_handle,
    std::vector<std::unique_ptr<content::NavigationThrottle>>* throttles) {
  // Don't add any throttles if the feature isn't enabled at all.
  if (!base::FeatureList::IsEnabled(
          subresource_filter::kSafeBrowsingSubresourceFilter)) {
    return;
  }

  if (navigation_handle->IsInMainFrame()) {
    safe_browsing::SafeBrowsingService* safe_browsing_service =
        g_browser_process->safe_browsing_service();
    scoped_refptr<safe_browsing::SafeBrowsingDatabaseManager> database_manager;
    if (safe_browsing_service &&
        safe_browsing_service->database_manager()->IsSupported() &&
        safe_browsing::V4FeatureList::GetV4UsageStatus() ==
            safe_browsing::V4FeatureList::V4UsageStatus::V4_ONLY) {
      database_manager = safe_browsing_service->database_manager();
    }

    throttles->push_back(
        base::MakeUnique<subresource_filter::
                             SubresourceFilterSafeBrowsingActivationThrottle>(
            navigation_handle,
            content::BrowserThread::GetTaskRunnerForThread(
                content::BrowserThread::IO),
            std::move(database_manager)));
  }

  auto* driver_factory =
      subresource_filter::ContentSubresourceFilterDriverFactory::
          FromWebContents(navigation_handle->GetWebContents());
  driver_factory->throttle_manager()->MaybeAppendNavigationThrottles(
      navigation_handle, throttles);
}

void ChromeSubresourceFilterClient::OnReloadRequested() {
  UMA_HISTOGRAM_BOOLEAN("SubresourceFilter.Prompt.NumReloads", true);
  const GURL& whitelist_url = web_contents_->GetLastCommittedURL();

  // Only whitelist via content settings when using the experimental UI,
  // otherwise could get into a situation where content settings cannot be
  // adjusted.
  if (base::FeatureList::IsEnabled(
          subresource_filter::kSafeBrowsingSubresourceFilterExperimentalUI)) {
    WhitelistByContentSettings(whitelist_url);
  } else {
    WhitelistInCurrentWebContents(whitelist_url);
  }
  web_contents_->GetController().Reload(content::ReloadType::NORMAL, true);
}

void ChromeSubresourceFilterClient::ToggleNotificationVisibility(
    bool visibility) {
  if (did_show_ui_for_navigation_ && visibility)
    return;
  did_show_ui_for_navigation_ = false;

  // |visibility| is false when a new navigation starts.
  if (visibility) {
    const GURL& top_level_url = web_contents_->GetLastCommittedURL();
    if (!settings_manager_->ShouldShowUIForSite(top_level_url)) {
      LogAction(kActionUISuppressed);
      return;
    }
#if defined(OS_ANDROID)
    InfoBarService* infobar_service =
        InfoBarService::FromWebContents(web_contents_);
    SubresourceFilterInfobarDelegate::Create(infobar_service);
#endif
    TabSpecificContentSettings* content_settings =
        TabSpecificContentSettings::FromWebContents(web_contents_);
    content_settings->OnContentBlocked(
        CONTENT_SETTINGS_TYPE_SUBRESOURCE_FILTER);

    LogAction(kActionUIShown);
    did_show_ui_for_navigation_ = true;
    settings_manager_->OnDidShowUI(top_level_url);
  } else {
    LogAction(kActionNavigationStarted);
  }
}

bool ChromeSubresourceFilterClient::OnPageActivationComputed(
    content::NavigationHandle* navigation_handle,
    bool activated) {
  const GURL& url(navigation_handle->GetURL());
  DCHECK(navigation_handle->IsInMainFrame());

  // If the site is no longer activated, clear the metadata. This is to maintain
  // the invariant that metadata implies activated.
  if (!activated && url.SchemeIsHTTPOrHTTPS())
    settings_manager_->ClearSiteMetadata(url);

  // Return whether the activation should be whitelisted.
  return whitelisted_hosts_.count(url.host()) ||
         settings_manager_->GetSitePermission(url) == CONTENT_SETTING_ALLOW;
  // TODO(csharrison): Consider setting the metadata to an empty dict here if
  // the site is activated and not whitelisted. Need to be careful about various
  // edge cases like |should_suppress_notification| and DRYRUN activation.
}

void ChromeSubresourceFilterClient::WhitelistByContentSettings(
    const GURL& top_level_url) {
  settings_manager_->WhitelistSite(top_level_url);
}

void ChromeSubresourceFilterClient::WhitelistInCurrentWebContents(
    const GURL& url) {
  if (url.SchemeIsHTTPOrHTTPS())
    whitelisted_hosts_.insert(url.host());
}

// static
void ChromeSubresourceFilterClient::LogAction(SubresourceFilterAction action) {
  UMA_HISTOGRAM_ENUMERATION("SubresourceFilter.Actions", action,
                            kActionLastEntry);
}

subresource_filter::VerifiedRulesetDealer::Handle*
ChromeSubresourceFilterClient::GetRulesetDealer() {
  subresource_filter::ContentRulesetService* ruleset_service =
      g_browser_process->subresource_filter_ruleset_service();
  return ruleset_service ? ruleset_service->ruleset_dealer() : nullptr;
}
