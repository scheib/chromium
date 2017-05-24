// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_WEB_STATE_NAVIGATION_CONTEXT_IMPL_H_
#define IOS_WEB_WEB_STATE_NAVIGATION_CONTEXT_IMPL_H_

#include <memory>

#import "base/mac/scoped_nsobject.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#import "ios/web/public/web_state/navigation_context.h"
#include "url/gurl.h"

namespace web {

// Tracks information related to a single navigation.
class NavigationContextImpl : public NavigationContext {
 public:
  // Creates navigation context for sucessful navigation to a different page.
  // Response headers will ne null.
  static std::unique_ptr<NavigationContextImpl> CreateNavigationContext(
      WebState* web_state,
      const GURL& url);

#ifndef NDEBUG
  // Returns human readable description of this object.
  NSString* GetDescription() const;
#endif  // NDEBUG

  // NavigationContext overrides:
  WebState* GetWebState() override;
  const GURL& GetUrl() const override;
  bool IsSameDocument() const override;
  NSError* GetError() const override;
  net::HttpResponseHeaders* GetResponseHeaders() const override;
  ~NavigationContextImpl() override;

  // Setters for navigation context data members.
  void SetIsSameDocument(bool is_same_document);
  void SetError(NSError* error);
  void SetResponseHeaders(
      const scoped_refptr<net::HttpResponseHeaders>& response_headers);

  // Optional unique id of the navigation item associated with this navigaiton.
  int GetNavigationItemUniqueID() const;
  void SetNavigationItemUniqueID(int unique_id);

 private:
  NavigationContextImpl(WebState* web_state, const GURL& url);

  WebState* web_state_ = nullptr;
  GURL url_;
  bool is_same_document_ = false;
  base::scoped_nsobject<NSError> error_;
  scoped_refptr<net::HttpResponseHeaders> response_headers_;
  int navigation_item_unique_id_ = -1;

  DISALLOW_COPY_AND_ASSIGN(NavigationContextImpl);
};

}  // namespace web

#endif  // IOS_WEB_WEB_STATE_NAVIGATION_CONTEXT_IMPL_H_
