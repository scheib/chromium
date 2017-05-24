// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_ROUTER_CREATE_PRESENTATION_CONNECTION_REQUEST_H_
#define CHROME_BROWSER_MEDIA_ROUTER_CREATE_PRESENTATION_CONNECTION_REQUEST_H_

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "chrome/browser/media/router/presentation_request.h"
#include "chrome/browser/media/router/render_frame_host_id.h"
#include "chrome/common/media_router/media_route.h"
#include "content/public/browser/presentation_service_delegate.h"

namespace content {
struct PresentationError;
struct PresentationInfo;
}  // namespace content

namespace url {
class Origin;
}  // namespace url

namespace media_router {

class RouteRequestResult;

// Holds parameters for creating a presentation.  A request object is created by
// presentation_service_delegate_impl, which is then passed to and owned by the
// MediaRouterUI. |success_cb| will be invoked when create-session succeeds, or
// |error_cb| will be invoked when create-session fails or the UI closes.
// TODO(mfoltz): Combine this with PresentationRequest as it's largely
// redundant.
class CreatePresentationConnectionRequest {
 public:
  using PresentationConnectionCallback =
      base::Callback<void(const content::PresentationInfo&, const MediaRoute&)>;
  using PresentationConnectionErrorCallback =
      content::PresentationConnectionErrorCallback;
  // |presentation_url|: The presentation URL of the request. Must be a valid
  //                     URL.
  // |frame_origin|: The origin of the frame that initiated the presentation
  // request.
  // |success_cb|: Callback to invoke when the request succeeds. Must be valid.
  // |error_cb|: Callback to invoke when the request fails. Must be valid.
  CreatePresentationConnectionRequest(
      const RenderFrameHostId& render_frame_host_id,
      const std::vector<GURL>& presentation_urls,
      const url::Origin& frame_origin,
      const PresentationConnectionCallback& success_cb,
      const PresentationConnectionErrorCallback& error_cb);
  ~CreatePresentationConnectionRequest();

  const PresentationRequest& presentation_request() const {
    return presentation_request_;
  }

  // Invokes |success_cb_| or |error_cb_| with the given arguments.
  // These functions can only be invoked once per instance. It is an error
  // to invoke these functions more than once.
  void InvokeSuccessCallback(const std::string& presentation_id,
                             const GURL& presentation_url,
                             const MediaRoute& route);
  void InvokeErrorCallback(const content::PresentationError& error);

  // Handle route creation/joining response by invoking the right callback.
  static void HandleRouteResponse(
      std::unique_ptr<CreatePresentationConnectionRequest> presentation_request,
      const RouteRequestResult& result);

 private:
  const PresentationRequest presentation_request_;
  PresentationConnectionCallback success_cb_;
  PresentationConnectionErrorCallback error_cb_;
  bool cb_invoked_;

  DISALLOW_COPY_AND_ASSIGN(CreatePresentationConnectionRequest);
};

}  // namespace media_router

#endif  // CHROME_BROWSER_MEDIA_ROUTER_CREATE_PRESENTATION_CONNECTION_REQUEST_H_
