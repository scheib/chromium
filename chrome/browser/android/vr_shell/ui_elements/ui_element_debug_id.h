// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_UI_ELEMENTS_UI_ELEMENT_DEBUG_ID_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_UI_ELEMENTS_UI_ELEMENT_DEBUG_ID_H_

namespace vr_shell {

// These identifiers may be used by UI elements to tag themselves for purposes
// of testing or debugging.
enum UiElementDebugId {
  kNone = 0,
  kWebVrPermanentHttpSecurityWarning,
  kWebVrTransientHttpSecurityWarning,
  kCloseButton,
};

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_UI_ELEMENTS_UI_ELEMENT_DEBUG_ID_H_
