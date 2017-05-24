// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/extension_navigation_throttle.h"

#include "components/guest_view/browser/guest_view_base.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/browser_side_navigation_policy.h"
#include "content/public/common/url_constants.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/guest_view/web_view/web_view_guest.h"
#include "extensions/browser/url_request_util.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/manifest_handlers/web_accessible_resources_info.h"
#include "extensions/common/manifest_handlers/webview_info.h"
#include "extensions/common/permissions/api_permission.h"
#include "extensions/common/permissions/permissions_data.h"
#include "ui/base/page_transition_types.h"

namespace extensions {

ExtensionNavigationThrottle::ExtensionNavigationThrottle(
    content::NavigationHandle* navigation_handle)
    : content::NavigationThrottle(navigation_handle) {}

ExtensionNavigationThrottle::~ExtensionNavigationThrottle() {}

content::NavigationThrottle::ThrottleCheckResult
ExtensionNavigationThrottle::WillStartRequest() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  content::WebContents* web_contents = navigation_handle()->GetWebContents();
  ExtensionRegistry* registry =
      ExtensionRegistry::Get(web_contents->GetBrowserContext());

  // Is this navigation targeting an extension resource?
  const GURL& url = navigation_handle()->GetURL();
  bool url_has_extension_scheme = url.SchemeIs(kExtensionScheme);
  url::Origin target_origin(url);
  const Extension* target_extension = nullptr;
  if (url_has_extension_scheme) {
    // "chrome-extension://" URL.
    target_extension =
        registry->enabled_extensions().GetExtensionOrAppByURL(url);
  } else if (target_origin.scheme() == kExtensionScheme) {
    // "blob:chrome-extension://" or "filesystem:chrome-extension://" URL.
    DCHECK(url.SchemeIsFileSystem() || url.SchemeIsBlob());
    target_extension =
        registry->enabled_extensions().GetByID(target_origin.host());
  } else {
    // If the navigation is not to a chrome-extension resource, no need to
    // perform any more checks; it's outside of the purview of this throttle.
    return content::NavigationThrottle::PROCEED;
  }

  // If the navigation is to an unknown or disabled extension, block it.
  if (!target_extension) {
    // TODO(nick): This yields an unsatisfying error page; use a different error
    // code once that's supported. https://crbug.com/649869
    return content::NavigationThrottle::BLOCK_REQUEST;
  }

  if (navigation_handle()->IsInMainFrame()) {
    // Block top-level navigations to blob: or filesystem: URLs with extension
    // origin from non-extension processes.  See https://crbug.com/645028.
    bool current_frame_is_extension_process =
        !!registry->enabled_extensions().GetExtensionOrAppByURL(
            navigation_handle()->GetStartingSiteInstance()->GetSiteURL());

    if (!url_has_extension_scheme && !current_frame_is_extension_process) {
      // Relax this restriction for apps that use <webview>.  See
      // https://crbug.com/652077.
      bool has_webview_permission =
          target_extension->permissions_data()->HasAPIPermission(
              APIPermission::kWebView);
      if (!has_webview_permission)
        return content::NavigationThrottle::CANCEL;
    }

    guest_view::GuestViewBase* guest =
        guest_view::GuestViewBase::FromWebContents(web_contents);
    if (content::IsBrowserSideNavigationEnabled() && url_has_extension_scheme &&
        guest) {
      // This variant of this logic applies to PlzNavigate top-level
      // navigations. It is performed for subresources, and for non-PlzNavigate
      // top navigations, in url_request_util::AllowCrossRendererResourceLoad.
      const std::string& owner_extension_id = guest->owner_host();
      const Extension* owner_extension =
          registry->enabled_extensions().GetByID(owner_extension_id);

      std::string partition_domain;
      std::string partition_id;
      bool in_memory = false;
      bool is_guest = WebViewGuest::GetGuestPartitionConfigForSite(
          navigation_handle()->GetStartingSiteInstance()->GetSiteURL(),
          &partition_domain, &partition_id, &in_memory);

      bool allowed = true;
      url_request_util::AllowCrossRendererResourceLoadHelper(
          is_guest, target_extension, owner_extension, partition_id, url.path(),
          navigation_handle()->GetPageTransition(), &allowed);
      if (!allowed)
        return content::NavigationThrottle::BLOCK_REQUEST;
    }

    return content::NavigationThrottle::PROCEED;
  }

  // This is a subframe navigation to a |target_extension| resource.
  // Enforce the web_accessible_resources restriction.
  content::RenderFrameHost* parent = navigation_handle()->GetParentFrame();

  // Look to see if all ancestors belong to |target_extension|. If not,
  // then the web_accessible_resource restriction applies.
  bool external_ancestor = false;
  for (auto* ancestor = parent; ancestor; ancestor = ancestor->GetParent()) {
    // Look for a match on the last committed origin. This handles the
    // common case, and the about:blank case.
    if (ancestor->GetLastCommittedOrigin() == target_origin)
      continue;
    // Look for an origin match with the last committed URL. This handles the
    // case of sandboxed extension resources, which commit with a null origin,
    // but are permitted to load non-webaccessible extension resources in
    // subframes.
    if (url::Origin(ancestor->GetLastCommittedURL()) == target_origin)
      continue;
    // Ignore DevTools, as it is allowed to embed extension pages.
    if (ancestor->GetLastCommittedURL().SchemeIs(
            content::kChromeDevToolsScheme))
      continue;

    // Otherwise, we have an external ancestor.
    external_ancestor = true;
    break;
  }

  if (external_ancestor) {
    // Cancel navigations to nested URLs, to match the main frame behavior.
    if (!url_has_extension_scheme)
      return content::NavigationThrottle::CANCEL;

    // |url| must be in the manifest's "web_accessible_resources" section.
    if (!WebAccessibleResourcesInfo::IsResourceWebAccessible(target_extension,
                                                             url.path()))
      return content::NavigationThrottle::BLOCK_REQUEST;
  }

  return content::NavigationThrottle::PROCEED;
}

const char* ExtensionNavigationThrottle::GetNameForLogging() {
  return "ExtensionNavigationThrottle";
}

}  // namespace extensions
