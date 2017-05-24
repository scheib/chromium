// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_COMMON_PRESENTATION_INFO_H_
#define CONTENT_PUBLIC_COMMON_PRESENTATION_INFO_H_

#include <string>

#include "content/common/content_export.h"
#include "url/gurl.h"

namespace content {

enum PresentationConnectionState {
  PRESENTATION_CONNECTION_STATE_CONNECTING,
  PRESENTATION_CONNECTION_STATE_CONNECTED,
  PRESENTATION_CONNECTION_STATE_CLOSED,
  PRESENTATION_CONNECTION_STATE_TERMINATED
};

// TODO(imcheng): Use WENT_AWAY for 1-UA mode when it is implemented
// (crbug.com/513859).
enum PresentationConnectionCloseReason {
  PRESENTATION_CONNECTION_CLOSE_REASON_CONNECTION_ERROR,
  PRESENTATION_CONNECTION_CLOSE_REASON_CLOSED,
  PRESENTATION_CONNECTION_CLOSE_REASON_WENT_AWAY
};

// Represents a presentation that has been established via either
// browser actions or Presentation API.
struct CONTENT_EXPORT PresentationInfo {
  PresentationInfo() = default;
  PresentationInfo(const GURL& presentation_url,
                   const std::string& presentation_id);
  ~PresentationInfo();

  static constexpr size_t kMaxIdLength = 256;

  GURL presentation_url;
  std::string presentation_id;
};

// Possible reasons why an attempt to create a presentation failed.
enum PresentationErrorType {
  PRESENTATION_ERROR_NO_AVAILABLE_SCREENS,
  PRESENTATION_ERROR_PRESENTATION_REQUEST_CANCELLED,
  PRESENTATION_ERROR_NO_PRESENTATION_FOUND,
  PRESENTATION_ERROR_PREVIOUS_START_IN_PROGRESS,
  PRESENTATION_ERROR_UNKNOWN,
};

// Struct returned when an attempt to create a presentation failed.
struct CONTENT_EXPORT PresentationError {
  // TODO(mfoltz): Should this initialize error_type to UNKNOWN?
  PresentationError() = default;
  PresentationError(PresentationErrorType error_type,
                    const std::string& message);
  ~PresentationError();

  static constexpr size_t kMaxMessageLength = 256;

  PresentationErrorType error_type;
  std::string message;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_COMMON_PRESENTATION_INFO_H_
