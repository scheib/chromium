// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_VR_BROWSER_INTERFACE_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_VR_BROWSER_INTERFACE_H_

#include <memory>

#include "base/android/jni_weak_ref.h"
#include "base/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/android/vr_shell/ui_interface.h"
#include "device/vr/android/gvr/gvr_gamepad_data_provider.h"
#include "device/vr/vr_service.mojom.h"

namespace blink {
class WebInputEvent;
}

namespace vr_shell {

// An interface for communication with Vr Browser. Many of the functions in this
// interface are proxies to methods on VrShell.
class VrBrowserInterface {
 public:
  virtual ~VrBrowserInterface() = default;

  virtual void ContentSurfaceChanged(jobject surface) = 0;
  virtual void GvrDelegateReady() = 0;
  virtual void UpdateGamepadData(device::GvrGamepadData) = 0;
  virtual void AppButtonGesturePerformed(UiInterface::Direction direction) = 0;
  virtual void AppButtonClicked() = 0;
  virtual void ProcessContentGesture(
      std::unique_ptr<blink::WebInputEvent> event) = 0;
  virtual void ForceExitVr() = 0;
  virtual void ExitPresent() = 0;
  virtual void ExitFullscreen() = 0;
  virtual void RunVRDisplayInfoCallback(
      const base::Callback<void(device::mojom::VRDisplayInfoPtr)>& callback,
      device::mojom::VRDisplayInfoPtr* info) = 0;
  virtual void OnContentPaused(bool enabled) = 0;
  virtual void NavigateBack() = 0;
  virtual void ExitCct() = 0;
};

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_VR_BROWSER_INTERFACE_H_
