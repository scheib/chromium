// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_UI_INTERFACE_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_UI_INTERFACE_H_

class GURL;

namespace vr_shell {

// This class manages the communication of browser state from VR shell to the
// HTML UI. State information is asynchronous and unidirectional.
class UiInterface {
 public:
  enum Direction {
    NONE = 0,
    LEFT,
    RIGHT,
    UP,
    DOWN,
  };

  virtual ~UiInterface() {}

  virtual void SetWebVrMode(bool enabled) = 0;
  virtual void SetURL(const GURL& url) = 0;
  virtual void SetFullscreen(bool enabled) = 0;
  virtual void SetSecurityLevel(int level) = 0;
  virtual void SetWebVrSecureOrigin(bool secure) = 0;
  virtual void SetLoading(bool loading) = 0;
  virtual void SetLoadProgress(float progress) = 0;
  virtual void SetIsExiting() = 0;
  virtual void SetHistoryButtonsEnabled(bool can_go_back,
                                        bool can_go_forward) = 0;
  virtual void SetVideoCapturingIndicator(bool enabled) = 0;
  virtual void SetScreenCapturingIndicator(bool enabled) = 0;
  virtual void SetAudioCapturingIndicator(bool enabled) = 0;

  // Tab handling.
  virtual void InitTabList() {}
  virtual void AppendToTabList(bool incognito,
                               int id,
                               const base::string16& title) {}
  virtual void FlushTabList() {}
  virtual void UpdateTab(bool incognito, int id, const std::string& title) {}
  virtual void RemoveTab(bool incognito, int id) {}
};

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_UI_INTERFACE_H_
