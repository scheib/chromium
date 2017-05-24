// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/aw_safe_browsing_blocking_page.h"

#include "android_webview/browser/aw_safe_browsing_ui_manager.h"
#include "components/security_interstitials/content/security_interstitial_controller_client.h"
#include "components/security_interstitials/content/unsafe_resource.h"
#include "components/security_interstitials/core/base_safe_browsing_error_ui.h"
#include "content/public/browser/interstitial_page.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

using content::InterstitialPage;
using content::WebContents;
using security_interstitials::BaseSafeBrowsingErrorUI;
using security_interstitials::SecurityInterstitialControllerClient;

namespace android_webview {

AwSafeBrowsingBlockingPage::AwSafeBrowsingBlockingPage(
    AwSafeBrowsingUIManager* ui_manager,
    WebContents* web_contents,
    const GURL& main_frame_url,
    const UnsafeResourceList& unsafe_resources,
    std::unique_ptr<SecurityInterstitialControllerClient> controller_client,
    const BaseSafeBrowsingErrorUI::SBErrorDisplayOptions& display_options)
    : BaseBlockingPage(ui_manager,
                       web_contents,
                       main_frame_url,
                       unsafe_resources,
                       std::move(controller_client),
                       display_options) {}

// static
void AwSafeBrowsingBlockingPage::ShowBlockingPage(
    AwSafeBrowsingUIManager* ui_manager,
    const UnsafeResource& unsafe_resource) {
  DVLOG(1) << __func__ << " " << unsafe_resource.url.spec();
  WebContents* web_contents = unsafe_resource.web_contents_getter.Run();

  if (InterstitialPage::GetInterstitialPage(web_contents) &&
      unsafe_resource.is_subresource) {
    // This is an interstitial for a page's resource, let's queue it.
    UnsafeResourceMap* unsafe_resource_map = GetUnsafeResourcesMap();
    (*unsafe_resource_map)[web_contents].push_back(unsafe_resource);
  } else {
    // There is no interstitial currently showing, or we are about to display a
    // new one for the main frame. If there is already an interstitial, showing
    // the new one will automatically hide the old one.
    content::NavigationEntry* entry =
        unsafe_resource.GetNavigationEntryForResource();
    const UnsafeResourceList unsafe_resources{unsafe_resource};
    BaseSafeBrowsingErrorUI::SBErrorDisplayOptions display_options =
        BaseSafeBrowsingErrorUI::SBErrorDisplayOptions(
            IsMainPageLoadBlocked(unsafe_resources),
            false,  // kSafeBrowsingExtendedReportingOptInAllowed
            false,  // is_off_the_record
            false,  // is_extended_reporting
            false,  // is_scout
            false,  // kSafeBrowsingProceedAnywayDisabled
            true,   // is_resource_cancellable
            "cpn_safe_browsing_wv");  // help_center_article_link
    AwSafeBrowsingBlockingPage* blocking_page = new AwSafeBrowsingBlockingPage(
        ui_manager, web_contents, entry ? entry->GetURL() : GURL(),
        unsafe_resources,
        CreateControllerClient(web_contents, unsafe_resources, ui_manager),
        display_options);
    blocking_page->Show();
  }
}

}  // namespace android_webview
