// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SUBRESOURCE_FILTER_CORE_BROWSER_SUBRESOURCE_FILTER_CLIENT_H_
#define COMPONENTS_SUBRESOURCE_FILTER_CORE_BROWSER_SUBRESOURCE_FILTER_CLIENT_H_

#include "components/subresource_filter/content/browser/verified_ruleset_dealer.h"
#include "content/public/browser/web_contents.h"

class GURL;

namespace content {
class NavigationHandle;
}  // namespace content

namespace subresource_filter {

class SubresourceFilterClient {
 public:
  virtual ~SubresourceFilterClient() = default;

  // Changes the visibility of the prompt that informs the user that potentially
  // deceptive content has been blocked on the page according to the passed
  // |visibility| parameter. When |visibility| is set to true, an icon on the
  // right side of the omnibox is displayed. If the user clicks on the icon then
  // a bubble is shown that explains the feature and alalows the user to turn it
  // off.
  virtual void ToggleNotificationVisibility(bool visibility) = 0;

  // Called when the activation decision is otherwise completely computed by the
  // subresource filter. At this point, the embedder still has a chance to
  // return false to suppress the activation. Returns whether the activation
  // should be whitelisted for this navigation.
  //
  // Precondition: The navigation must be a main frame navigation.
  virtual bool OnPageActivationComputed(
      content::NavigationHandle* navigation_handle,
      bool activated) = 0;

  // Adds |url| to a per-WebContents whitelist.
  virtual void WhitelistInCurrentWebContents(const GURL& url) = 0;

  virtual VerifiedRulesetDealer::Handle* GetRulesetDealer() = 0;
};

}  // namespace subresource_filter

#endif  // COMPONENTS_SUBRESOURCE_FILTER_CORE_BROWSER_SUBRESOURCE_FILTER_CLIENT_H_
