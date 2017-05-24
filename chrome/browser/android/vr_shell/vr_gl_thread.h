// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_VR_GL_THREAD_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_VR_GL_THREAD_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread.h"
#include "chrome/browser/android/vr_shell/vr_browser_interface.h"
#include "third_party/gvr-android-sdk/src/libraries/headers/vr/gvr/capi/include/gvr_types.h"

class GURL;

namespace vr_shell {

class UiScene;
class UiSceneManager;
class VrShell;
class VrShellGl;

class VrGLThread : public base::Thread,
                   public UiInterface,
                   public VrBrowserInterface {
 public:
  VrGLThread(
      const base::WeakPtr<VrShell>& weak_vr_shell,
      scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner,
      gvr_context* gvr_api,
      bool initially_web_vr,
      bool in_cct,
      bool reprojected_rendering,
      bool daydream_support);

  ~VrGLThread() override;
  base::WeakPtr<VrShellGl> GetVrShellGl() { return weak_vr_shell_gl_; }
  base::WeakPtr<UiSceneManager> GetSceneManager() {
    return weak_scene_manager_;
  }

  // VrBrowserInterface implementation (VrShellGl calling to UI and VrShell).
  void ContentSurfaceChanged(jobject surface) override;
  void GvrDelegateReady() override;
  void UpdateGamepadData(device::GvrGamepadData) override;
  void AppButtonClicked() override;
  void AppButtonGesturePerformed(UiInterface::Direction direction) override;
  void ProcessContentGesture(
      std::unique_ptr<blink::WebInputEvent> event) override;
  void ForceExitVr() override;
  void ExitPresent() override;
  void ExitFullscreen() override;
  void RunVRDisplayInfoCallback(
      const base::Callback<void(device::mojom::VRDisplayInfoPtr)>& callback,
      device::mojom::VRDisplayInfoPtr* info) override;
  void OnContentPaused(bool enabled) override;
  void NavigateBack() override;
  void ExitCct() override;

  // UiInterface implementation (VrShell calling to the UI).
  void SetFullscreen(bool enabled) override;
  void SetHistoryButtonsEnabled(bool can_go_back, bool can_go_forward) override;
  void SetLoadProgress(float progress) override;
  void SetLoading(bool loading) override;
  void SetSecurityLevel(int level) override;
  void SetURL(const GURL& gurl) override;
  void SetWebVrMode(bool enabled) override;
  void SetWebVrSecureOrigin(bool secure) override;
  void SetVideoCapturingIndicator(bool enabled) override;
  void SetScreenCapturingIndicator(bool enabled) override;
  void SetAudioCapturingIndicator(bool enabled) override;
  void SetIsExiting() override;

 protected:
  void Init() override;
  void CleanUp() override;

 private:
  // Created on GL thread.
  std::unique_ptr<UiScene> scene_;
  std::unique_ptr<UiSceneManager> scene_manager_;
  base::WeakPtr<UiSceneManager> weak_scene_manager_;
  std::unique_ptr<VrShellGl> vr_shell_gl_;
  base::WeakPtr<VrShellGl> weak_vr_shell_gl_;

  // This state is used for initializing vr_shell_gl_.
  base::WeakPtr<VrShell> weak_vr_shell_;
  scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner_;
  gvr_context* gvr_api_;
  bool initially_web_vr_;
  bool in_cct_;
  bool reprojected_rendering_;
  bool daydream_support_;

  DISALLOW_COPY_AND_ASSIGN(VrGLThread);
};

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_VR_GL_THREAD_H_
