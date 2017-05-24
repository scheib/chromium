// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/web_state/navigation_context_impl.h"

#import <Foundation/Foundation.h>

#include "base/memory/ptr_util.h"
#include "net/http/http_response_headers.h"

namespace web {

// static
std::unique_ptr<NavigationContextImpl>
NavigationContextImpl::CreateNavigationContext(WebState* web_state,
                                               const GURL& url) {
  std::unique_ptr<NavigationContextImpl> result(
      new NavigationContextImpl(web_state, url));
  return result;
}

#ifndef NDEBUG
NSString* NavigationContextImpl::GetDescription() const {
  return [NSString
      stringWithFormat:@"web::WebState: %ld, url: %s, "
                        "is_same_document: %@, error: %@",
                       reinterpret_cast<long>(web_state_), url_.spec().c_str(),
                       is_same_document_ ? @"true" : @"false", error_.get()];
}
#endif  // NDEBUG

WebState* NavigationContextImpl::GetWebState() {
  return web_state_;
}

const GURL& NavigationContextImpl::GetUrl() const {
  return url_;
}

bool NavigationContextImpl::IsSameDocument() const {
  return is_same_document_;
}

NSError* NavigationContextImpl::GetError() const {
  return error_;
}

net::HttpResponseHeaders* NavigationContextImpl::GetResponseHeaders() const {
  return response_headers_.get();
}

void NavigationContextImpl::SetIsSameDocument(bool is_same_document) {
  is_same_document_ = is_same_document;
}

void NavigationContextImpl::SetError(NSError* error) {
  error_.reset(error);
}

void NavigationContextImpl::SetResponseHeaders(
    const scoped_refptr<net::HttpResponseHeaders>& response_headers) {
  response_headers_ = response_headers;
}

int NavigationContextImpl::GetNavigationItemUniqueID() const {
  return navigation_item_unique_id_;
}

void NavigationContextImpl::SetNavigationItemUniqueID(int unique_id) {
  navigation_item_unique_id_ = unique_id;
}

NavigationContextImpl::NavigationContextImpl(WebState* web_state,
                                             const GURL& url)
    : web_state_(web_state),
      url_(url),
      is_same_document_(false),
      error_(nil),
      response_headers_(nullptr) {}

NavigationContextImpl::~NavigationContextImpl() = default;

}  // namespace web
