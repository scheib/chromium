// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/vr_shell/vr_gl_thread.h"

#include <utility>

#include "chrome/browser/android/vr_shell/ui_interface.h"
#include "chrome/browser/android/vr_shell/ui_scene.h"
#include "chrome/browser/android/vr_shell/ui_scene_manager.h"
#include "chrome/browser/android/vr_shell/vr_input_manager.h"
#include "chrome/browser/android/vr_shell/vr_shell.h"
#include "chrome/browser/android/vr_shell/vr_shell_gl.h"

namespace vr_shell {

VrGLThread::VrGLThread(
    const base::WeakPtr<VrShell>& weak_vr_shell,
    scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner,
    gvr_context* gvr_api,
    bool initially_web_vr,
    bool in_cct,
    bool reprojected_rendering,
    bool daydream_support)
    : base::Thread("VrShellGL"),
      weak_vr_shell_(weak_vr_shell),
      main_thread_task_runner_(std::move(main_thread_task_runner)),
      gvr_api_(gvr_api),
      initially_web_vr_(initially_web_vr),
      in_cct_(in_cct),
      reprojected_rendering_(reprojected_rendering),
      daydream_support_(daydream_support) {}

VrGLThread::~VrGLThread() {
  Stop();
}

void VrGLThread::Init() {
  scene_ = base::MakeUnique<UiScene>();
  vr_shell_gl_ = base::MakeUnique<VrShellGl>(this, gvr_api_, initially_web_vr_,
                                             reprojected_rendering_,
                                             daydream_support_, scene_.get());
  scene_manager_ = base::MakeUnique<UiSceneManager>(this, scene_.get(), in_cct_,
                                                    initially_web_vr_);

  weak_vr_shell_gl_ = vr_shell_gl_->GetWeakPtr();
  weak_scene_manager_ = scene_manager_->GetWeakPtr();
  vr_shell_gl_->Initialize();
}

void VrGLThread::ContentSurfaceChanged(jobject surface) {
  main_thread_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&VrShell::ContentSurfaceChanged, weak_vr_shell_, surface));
}

void VrGLThread::GvrDelegateReady() {
  main_thread_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VrShell::GvrDelegateReady, weak_vr_shell_));
}

void VrGLThread::UpdateGamepadData(device::GvrGamepadData pad) {
  main_thread_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VrShell::UpdateGamepadData, weak_vr_shell_, pad));
}

void VrGLThread::AppButtonClicked() {
  task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&UiSceneManager::OnAppButtonClicked, weak_scene_manager_));
}

void VrGLThread::AppButtonGesturePerformed(UiInterface::Direction direction) {
  task_runner()->PostTask(
      FROM_HERE, base::Bind(&UiSceneManager::OnAppButtonGesturePerformed,
                            weak_scene_manager_, direction));
}

void VrGLThread::ProcessContentGesture(
    std::unique_ptr<blink::WebInputEvent> event) {
  main_thread_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VrShell::ProcessContentGesture, weak_vr_shell_,
                            base::Passed(std::move(event))));
}

void VrGLThread::ForceExitVr() {
  main_thread_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VrShell::ForceExitVr, weak_vr_shell_));
}

void VrGLThread::ExitPresent() {
  main_thread_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VrShell::ExitPresent, weak_vr_shell_));
}

void VrGLThread::ExitFullscreen() {
  main_thread_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VrShell::ExitFullscreen, weak_vr_shell_));
}

void VrGLThread::RunVRDisplayInfoCallback(
    const base::Callback<void(device::mojom::VRDisplayInfoPtr)>& callback,
    device::mojom::VRDisplayInfoPtr* info) {
  main_thread_task_runner_->PostTask(FROM_HERE,
                                     base::Bind(callback, base::Passed(info)));
}

void VrGLThread::OnContentPaused(bool enabled) {
  main_thread_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&VrShell::OnContentPaused, weak_vr_shell_, enabled));
}

void VrGLThread::NavigateBack() {
  main_thread_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VrShell::NavigateBack, weak_vr_shell_));
}

void VrGLThread::ExitCct() {
  main_thread_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VrShell::ExitCct, weak_vr_shell_));
}

void VrGLThread::SetFullscreen(bool enabled) {
  WaitUntilThreadStarted();
  task_runner()->PostTask(FROM_HERE, base::Bind(&UiSceneManager::SetFullscreen,
                                                weak_scene_manager_, enabled));
}

void VrGLThread::SetHistoryButtonsEnabled(bool can_go_back,
                                          bool can_go_forward) {
  WaitUntilThreadStarted();
  task_runner()->PostTask(
      FROM_HERE, base::Bind(&UiSceneManager::SetHistoryButtonsEnabled,
                            weak_scene_manager_, can_go_forward, can_go_back));
}

void VrGLThread::SetLoadProgress(float progress) {
  WaitUntilThreadStarted();
  task_runner()->PostTask(FROM_HERE,
                          base::Bind(&UiSceneManager::SetLoadProgress,
                                     weak_scene_manager_, progress));
}

void VrGLThread::SetLoading(bool loading) {
  WaitUntilThreadStarted();
  task_runner()->PostTask(FROM_HERE, base::Bind(&UiSceneManager::SetLoading,
                                                weak_scene_manager_, loading));
}

void VrGLThread::SetSecurityLevel(int level) {
  WaitUntilThreadStarted();
  task_runner()->PostTask(FROM_HERE,
                          base::Bind(&UiSceneManager::SetSecurityLevel,
                                     weak_scene_manager_, level));
}

void VrGLThread::SetURL(const GURL& gurl) {
  WaitUntilThreadStarted();
  task_runner()->PostTask(FROM_HERE, base::Bind(&UiSceneManager::SetURL,
                                                weak_scene_manager_, gurl));
}

void VrGLThread::SetWebVrMode(bool enabled) {
  WaitUntilThreadStarted();
  task_runner()->PostTask(FROM_HERE, base::Bind(&UiSceneManager::SetWebVrMode,
                                                weak_scene_manager_, enabled));
}

void VrGLThread::SetWebVrSecureOrigin(bool secure) {
  WaitUntilThreadStarted();
  task_runner()->PostTask(FROM_HERE,
                          base::Bind(&UiSceneManager::SetWebVrSecureOrigin,
                                     weak_scene_manager_, secure));
}

void VrGLThread::SetAudioCapturingIndicator(bool enabled) {
  task_runner()->PostTask(
      FROM_HERE, base::Bind(&UiSceneManager::SetAudioCapturingIndicator,
                            weak_scene_manager_, enabled));
}

void VrGLThread::SetVideoCapturingIndicator(bool enabled) {
  task_runner()->PostTask(
      FROM_HERE, base::Bind(&UiSceneManager::SetVideoCapturingIndicator,
                            weak_scene_manager_, enabled));
}

void VrGLThread::SetScreenCapturingIndicator(bool enabled) {
  task_runner()->PostTask(
      FROM_HERE, base::Bind(&UiSceneManager::SetScreenCapturingIndicator,
                            weak_scene_manager_, enabled));
}

void VrGLThread::SetIsExiting() {
  WaitUntilThreadStarted();
  task_runner()->PostTask(FROM_HERE, base::Bind(&UiSceneManager::SetIsExiting,
                                                weak_scene_manager_));
}

void VrGLThread::CleanUp() {
  scene_manager_.reset();
  vr_shell_gl_.reset();
  scene_.reset();
}

}  // namespace vr_shell
