// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_PUBLIC_WEB_STATE_NAVIGATION_CONTEXT_H_
#define IOS_WEB_PUBLIC_WEB_STATE_NAVIGATION_CONTEXT_H_

#import <Foundation/Foundation.h>

class GURL;

namespace net {
class HttpResponseHeaders;
}

namespace web {

class WebState;

// Tracks information related to a single navigation. A NavigationContext is
// provided to WebStateObserver methods to allow observers to track specific
// navigations and their details. Observers should clear any references to a
// NavigationContext at the time of WebStateObserver::DidFinishNavigation, just
// before the handle is destroyed.
class NavigationContext {
 public:
  // The WebState the navigation is taking place in.
  virtual WebState* GetWebState() = 0;

  // The URL the WebState is navigating to.
  virtual const GURL& GetUrl() const = 0;

  // Whether the navigation happened within the same document. Examples of same
  // document navigations are:
  // * reference fragment navigations
  // * pushState/replaceState
  // * same document history navigation
  virtual bool IsSameDocument() const = 0;

  // Returns error if the navigation has failed.
  virtual NSError* GetError() const = 0;

  // Returns the response headers for the request, or null if there aren't any
  // response headers or they have not been received yet. The response headers
  // returned should not be modified, as modifications will not be reflected.
  virtual net::HttpResponseHeaders* GetResponseHeaders() const = 0;

  virtual ~NavigationContext() {}
};

}  // namespace web

#endif  // IOS_WEB_PUBLIC_WEB_STATE_NAVIGATION_CONTEXT_H_
