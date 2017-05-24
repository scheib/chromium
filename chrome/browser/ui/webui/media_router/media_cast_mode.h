// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_MEDIA_ROUTER_MEDIA_CAST_MODE_H_
#define CHROME_BROWSER_UI_WEBUI_MEDIA_ROUTER_MEDIA_CAST_MODE_H_

#include <set>
#include <string>

namespace media_router {

// A cast mode represents one way that the current WebContents (i.e., tab) may
// be presented to a media sink.  These must be declared in the priority order
// returned by GetPreferredCastMode.
enum MediaCastMode {
  // A presentation URL provided by the WebContents via the Presentation API.
  // This can be set by the default presentation URL (for top level browsing
  // contexts) or presentation URLs passed with a PresentationRequest (for top
  // level and nested browsing contexts).
  // TODO(mfoltz): More accurately named PRESENTATION
  DEFAULT = 0x1,
  // Capture the rendered WebContents and stream it to a media sink.  Always
  // available.
  TAB_MIRROR = 0x2,
  // Capture the entire desktop and stream it to a media sink.  Always
  // available.
  DESKTOP_MIRROR = 0x4,
};

using CastModeSet = std::set<MediaCastMode>;

// Returns a localized description string for |mode| and |host|
// (e.g. google.com).
std::string MediaCastModeToDescription(MediaCastMode mode,
                                       const std::string& host);

// Returns true if |cast_mode_num| is a valid MediaCastMode, false otherwise.
bool IsValidCastModeNum(int cast_mode_num);

}  // namespace media_router

#endif  // CHROME_BROWSER_UI_WEBUI_MEDIA_ROUTER_MEDIA_CAST_MODE_H_
