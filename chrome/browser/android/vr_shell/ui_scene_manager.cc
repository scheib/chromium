// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/vr_shell/ui_scene_manager.h"

#include "base/callback.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/android/vr_shell/textures/close_button_texture.h"
#include "chrome/browser/android/vr_shell/textures/ui_texture.h"
#include "chrome/browser/android/vr_shell/ui_elements/audio_capture_indicator.h"
#include "chrome/browser/android/vr_shell/ui_elements/button.h"
#include "chrome/browser/android/vr_shell/ui_elements/exit_warning.h"
#include "chrome/browser/android/vr_shell/ui_elements/loading_indicator.h"
#include "chrome/browser/android/vr_shell/ui_elements/permanent_security_warning.h"
#include "chrome/browser/android/vr_shell/ui_elements/screen_capture_indicator.h"
#include "chrome/browser/android/vr_shell/ui_elements/screen_dimmer.h"
#include "chrome/browser/android/vr_shell/ui_elements/transient_security_warning.h"
#include "chrome/browser/android/vr_shell/ui_elements/ui_element.h"
#include "chrome/browser/android/vr_shell/ui_elements/ui_element_debug_id.h"
#include "chrome/browser/android/vr_shell/ui_elements/url_bar.h"
#include "chrome/browser/android/vr_shell/ui_elements/video_capture_indicator.h"
#include "chrome/browser/android/vr_shell/ui_scene.h"
#include "chrome/browser/android/vr_shell/vr_browser_interface.h"
#include "chrome/browser/android/vr_shell/vr_shell.h"

namespace vr_shell {

namespace {

static constexpr int kWarningTimeoutSeconds = 30;
static constexpr float kWarningDistance = 0.7;
static constexpr float kWarningAngleRadians = 16.3 * M_PI / 180.0;
static constexpr float kPermanentWarningHeight = 0.070f;
static constexpr float kPermanentWarningWidth = 0.224f;
static constexpr float kTransientWarningHeight = 0.160;
static constexpr float kTransientWarningWidth = 0.512;

static constexpr float kExitWarningDistance = 0.6;
static constexpr float kExitWarningHeight = 0.160;
static constexpr float kExitWarningWidth = 0.512;

static constexpr float kContentDistance = 2.5;
static constexpr float kContentWidth = 0.96 * kContentDistance;
static constexpr float kContentHeight = 0.64 * kContentDistance;
static constexpr float kContentVerticalOffset = -0.1 * kContentDistance;
static constexpr float kBackplaneSize = 1000.0;
static constexpr float kBackgroundDistanceMultiplier = 1.414;

static constexpr float kUrlBarDistance = 2.4;
static constexpr float kUrlBarWidth = 0.672 * kUrlBarDistance;
static constexpr float kUrlBarHeight = 0.088 * kUrlBarDistance;
static constexpr float kUrlBarVerticalOffset = -0.516 * kUrlBarDistance;

static constexpr float kLoadingIndicatorWidth = 0.24 * kUrlBarDistance;
static constexpr float kLoadingIndicatorHeight = 0.008 * kUrlBarDistance;
static constexpr float kLoadingIndicatorOffset =
    -0.016 * kUrlBarDistance - kLoadingIndicatorHeight / 2;

static constexpr float kFullscreenWidth = 2.88;
static constexpr float kFullscreenHeight = 1.62;
static constexpr float kFullscreenDistance = 3;
static constexpr float kFullscreenVerticalOffset = -0.26;
static constexpr vr::Colorf kFullscreenBackgroundColor = {0.1, 0.1, 0.1, 1.0};

static constexpr float kSceneSize = 25.0;
static constexpr float kSceneHeight = 4.0;
static constexpr int kFloorGridlineCount = 40;
static constexpr vr::Colorf kBackgroundHorizonColor = {0.57, 0.57, 0.57, 1.0};
static constexpr vr::Colorf kBackgroundCenterColor = {0.48, 0.48, 0.48, 1.0};

// Tiny distance to offset textures that should appear in the same plane.
static constexpr float kTextureOffset = 0.01;

}  // namespace

UiSceneManager::UiSceneManager(VrBrowserInterface* browser,
                               UiScene* scene,
                               bool in_cct,
                               bool in_web_vr)
    : browser_(browser),
      scene_(scene),
      in_cct_(in_cct),
      web_vr_mode_(in_web_vr),
      weak_ptr_factory_(this) {
  CreateBackground();
  CreateContentQuad();
  CreateSecurityWarnings();
  CreateSystemIndicators();
  CreateUrlBar();
  if (in_cct_)
    CreateCloseButton();
  CreateScreenDimmer();

  ConfigureScene();
  ConfigureSecurityWarnings();
}

UiSceneManager::~UiSceneManager() {}

void UiSceneManager::CreateScreenDimmer() {
  std::unique_ptr<UiElement> element;
  element = base::MakeUnique<ScreenDimmer>();
  element->set_id(AllocateId());
  element->set_fill(vr_shell::Fill::NONE);
  element->set_visible(false);
  element->set_hit_testable(false);
  element->set_is_overlay(true);
  screen_dimmer_ = element.get();
  scene_->AddUiElement(std::move(element));
}

void UiSceneManager::CreateSecurityWarnings() {
  std::unique_ptr<UiElement> element;

  // TODO(mthiesse): Programatically compute the proper texture size for these
  // textured UI elements.
  element = base::MakeUnique<PermanentSecurityWarning>(512);
  element->set_debug_id(kWebVrPermanentHttpSecurityWarning);
  element->set_id(AllocateId());
  element->set_fill(vr_shell::Fill::NONE);
  element->set_size({kPermanentWarningWidth, kPermanentWarningHeight, 1});
  element->set_scale({kWarningDistance, kWarningDistance, 1});
  element->set_translation(
      gfx::Vector3dF(0, kWarningDistance * sin(kWarningAngleRadians),
                     -kWarningDistance * cos(kWarningAngleRadians)));
  element->set_rotation({1.0f, 0, 0, kWarningAngleRadians});
  element->set_visible(false);
  element->set_hit_testable(false);
  element->set_lock_to_fov(true);
  permanent_security_warning_ = element.get();
  scene_->AddUiElement(std::move(element));

  element = base::MakeUnique<TransientSecurityWarning>(1024);
  element->set_debug_id(kWebVrTransientHttpSecurityWarning);
  element->set_id(AllocateId());
  element->set_fill(vr_shell::Fill::NONE);
  element->set_size({kTransientWarningWidth, kTransientWarningHeight, 1});
  element->set_scale({kWarningDistance, kWarningDistance, 1});
  element->set_translation({0, 0, -kWarningDistance});
  element->set_visible(false);
  element->set_hit_testable(false);
  element->set_lock_to_fov(true);
  transient_security_warning_ = element.get();
  scene_->AddUiElement(std::move(element));

  element = base::MakeUnique<ExitWarning>(1024);
  element->set_id(AllocateId());
  element->set_fill(vr_shell::Fill::NONE);
  element->set_size({kExitWarningWidth, kExitWarningHeight, 1});
  element->set_scale({kExitWarningDistance, kExitWarningDistance, 1});
  element->set_translation({0, 0, -kExitWarningDistance});
  element->set_visible(false);
  element->set_hit_testable(false);
  element->set_lock_to_fov(true);
  exit_warning_ = element.get();
  scene_->AddUiElement(std::move(element));
}

void UiSceneManager::CreateSystemIndicators() {
  std::unique_ptr<UiElement> element;

  // TODO(acondor): Make constants for sizes and positions once the UX for the
  // indicators is defined.
  element = base::MakeUnique<AudioCaptureIndicator>(512);
  element->set_id(AllocateId());
  element->set_translation({-0.3, 0.8, -kContentDistance + 0.1});
  element->set_size({0.5, 0, 1});
  element->set_visible(false);
  audio_capture_indicator_ = element.get();
  scene_->AddUiElement(std::move(element));

  element = base::MakeUnique<VideoCaptureIndicator>(512);
  element->set_id(AllocateId());
  element->set_translation({0.3, 0.8, -kContentDistance + 0.1});
  element->set_size({0.5, 0, 1});
  element->set_visible(false);
  video_capture_indicator_ = element.get();
  scene_->AddUiElement(std::move(element));

  element = base::MakeUnique<ScreenCaptureIndicator>(512);
  element->set_id(AllocateId());
  element->set_translation({0.0, 0.65, -kContentDistance + 0.1});
  element->set_size({0.4, 0, 1});
  element->set_visible(false);
  screen_capture_indicator_ = element.get();
  scene_->AddUiElement(std::move(element));
}

void UiSceneManager::CreateContentQuad() {
  std::unique_ptr<UiElement> element;

  element = base::MakeUnique<UiElement>();
  element->set_id(AllocateId());
  element->set_fill(vr_shell::Fill::CONTENT);
  element->set_size({kContentWidth, kContentHeight, 1});
  element->set_translation({0, kContentVerticalOffset, -kContentDistance});
  element->set_visible(false);
  main_content_ = element.get();
  content_elements_.push_back(element.get());
  scene_->AddUiElement(std::move(element));

  // Place an invisible but hittable plane behind the content quad, to keep the
  // reticle roughly planar with the content if near content.
  element = base::MakeUnique<UiElement>();
  element->set_id(AllocateId());
  element->set_fill(vr_shell::Fill::NONE);
  element->set_size({kBackplaneSize, kBackplaneSize, 1.0});
  element->set_translation({0.0, 0.0, -kTextureOffset});
  element->set_parent_id(main_content_->id());
  content_elements_.push_back(element.get());
  scene_->AddUiElement(std::move(element));

  // Limit reticle distance to a sphere based on content distance.
  scene_->SetBackgroundDistance(main_content_->translation().z() *
                                -kBackgroundDistanceMultiplier);
}

void UiSceneManager::CreateBackground() {
  std::unique_ptr<UiElement> element;

  // Floor.
  element = base::MakeUnique<UiElement>();
  element->set_id(AllocateId());
  element->set_size({kSceneSize, kSceneSize, 1.0});
  element->set_translation({0.0, -kSceneHeight / 2, 0.0});
  element->set_rotation({1.0, 0.0, 0.0, -M_PI / 2.0});
  element->set_fill(vr_shell::Fill::OPAQUE_GRADIENT);
  element->set_edge_color(kBackgroundHorizonColor);
  element->set_center_color(kBackgroundCenterColor);
  element->set_draw_phase(0);
  control_elements_.push_back(element.get());
  scene_->AddUiElement(std::move(element));

  // Ceiling.
  element = base::MakeUnique<UiElement>();
  element->set_id(AllocateId());
  element->set_fill(vr_shell::Fill::OPAQUE_GRADIENT);
  element->set_size({kSceneSize, kSceneSize, 1.0});
  element->set_translation({0.0, kSceneHeight / 2, 0.0});
  element->set_rotation({1.0, 0.0, 0.0, M_PI / 2});
  element->set_fill(vr_shell::Fill::OPAQUE_GRADIENT);
  element->set_edge_color(kBackgroundHorizonColor);
  element->set_center_color(kBackgroundCenterColor);
  element->set_draw_phase(0);
  control_elements_.push_back(element.get());
  scene_->AddUiElement(std::move(element));

  // Floor grid.
  element = base::MakeUnique<UiElement>();
  element->set_id(AllocateId());
  element->set_fill(vr_shell::Fill::GRID_GRADIENT);
  element->set_size({kSceneSize, kSceneSize, 1.0});
  element->set_translation({0.0, -kSceneHeight / 2 + kTextureOffset, 0.0});
  element->set_rotation({1.0, 0.0, 0.0, -M_PI / 2});
  element->set_fill(vr_shell::Fill::GRID_GRADIENT);
  element->set_center_color(kBackgroundHorizonColor);
  vr::Colorf edge_color = kBackgroundHorizonColor;
  edge_color.a = 0.0;
  element->set_edge_color(edge_color);
  element->set_gridline_count(kFloorGridlineCount);
  element->set_draw_phase(0);
  control_elements_.push_back(element.get());
  scene_->AddUiElement(std::move(element));

  scene_->SetBackgroundColor(kBackgroundHorizonColor);
}

void UiSceneManager::CreateUrlBar() {
  // TODO(cjgrant): Incorporate final size and position.
  auto url_bar = base::MakeUnique<UrlBar>(512);
  url_bar->set_id(AllocateId());
  url_bar->set_translation({0, kUrlBarVerticalOffset, -kUrlBarDistance});
  url_bar->set_size({kUrlBarWidth, kUrlBarHeight, 1});
  url_bar->SetBackButtonCallback(
      base::Bind(&UiSceneManager::OnBackButtonClicked, base::Unretained(this)));
  url_bar_ = url_bar.get();
  control_elements_.push_back(url_bar.get());
  scene_->AddUiElement(std::move(url_bar));

  auto indicator = base::MakeUnique<LoadingIndicator>(256);
  indicator->set_id(AllocateId());
  indicator->set_translation({0, 0, kLoadingIndicatorOffset});
  indicator->set_size({kLoadingIndicatorWidth, kLoadingIndicatorHeight, 1});
  indicator->set_parent_id(url_bar_->id());
  indicator->set_y_anchoring(YAnchoring::YTOP);
  loading_indicator_ = indicator.get();
  control_elements_.push_back(indicator.get());
  scene_->AddUiElement(std::move(indicator));
}

void UiSceneManager::CreateCloseButton() {
  std::unique_ptr<Button> element = base::MakeUnique<Button>(
      base::Bind(&UiSceneManager::OnCloseButtonClicked, base::Unretained(this)),
      base::MakeUnique<CloseButtonTexture>());
  element->set_debug_id(kCloseButton);
  element->set_id(AllocateId());
  element->set_fill(vr_shell::Fill::NONE);
  element->set_translation(
      gfx::Vector3dF(0, kContentVerticalOffset - (kContentHeight / 2) - 0.3,
                     -kContentDistance + 0.4));
  element->set_size(gfx::Vector3dF(0.2, 0.2, 1));
  control_elements_.push_back(element.get());
  scene_->AddUiElement(std::move(element));
}

base::WeakPtr<UiSceneManager> UiSceneManager::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void UiSceneManager::SetWebVrMode(bool web_vr) {
  if (web_vr_mode_ == web_vr)
    return;
  web_vr_mode_ = web_vr;
  ConfigureScene();
  ConfigureSecurityWarnings();
}

void UiSceneManager::ConfigureScene() {
  exit_warning_->SetEnabled(scene_->is_exiting());
  screen_dimmer_->SetEnabled(scene_->is_exiting());

  // Controls (URL bar, loading progress, etc).
  bool controls_visible = !web_vr_mode_ && !fullscreen_;
  for (UiElement* element : control_elements_) {
    element->SetEnabled(controls_visible);
  }

  // Content elements.
  for (UiElement* element : content_elements_) {
    element->SetEnabled(!web_vr_mode_);
  }

  // Update content quad parameters depending on fullscreen.
  // TODO(http://crbug.com/642937): Animate fullscreen transitions.
  if (fullscreen_) {
    scene_->SetBackgroundColor(kFullscreenBackgroundColor);
    main_content_->set_translation(
        {0, kFullscreenVerticalOffset, -kFullscreenDistance});
    main_content_->set_size({kFullscreenWidth, kFullscreenHeight, 1});
  } else {
    scene_->SetBackgroundColor(kBackgroundHorizonColor);
    // Note that main_content_ is already visible in this case.
    main_content_->set_translation(
        {0, kContentVerticalOffset, -kContentDistance});
    main_content_->set_size({kContentWidth, kContentHeight, 1});
  }

  scene_->SetBackgroundDistance(main_content_->translation().z() *
                                -kBackgroundDistanceMultiplier);
}

void UiSceneManager::SetAudioCapturingIndicator(bool enabled) {
  audio_capture_indicator_->set_visible(enabled);
}

void UiSceneManager::SetVideoCapturingIndicator(bool enabled) {
  video_capture_indicator_->set_visible(enabled);
}

void UiSceneManager::SetScreenCapturingIndicator(bool enabled) {
  // TODO(asimjour) add the indicator and change the visibility here.
}

void UiSceneManager::SetWebVrSecureOrigin(bool secure) {
  secure_origin_ = secure;
  ConfigureSecurityWarnings();
}

void UiSceneManager::OnAppButtonClicked() {
  // App button click exits the WebVR presentation and fullscreen.
  browser_->ExitPresent();
  browser_->ExitFullscreen();
}

void UiSceneManager::OnAppButtonGesturePerformed(
    UiInterface::Direction direction) {}

void UiSceneManager::SetFullscreen(bool fullscreen) {
  if (fullscreen_ == fullscreen)
    return;
  fullscreen_ = fullscreen;
  ConfigureScene();
}

void UiSceneManager::ConfigureSecurityWarnings() {
  bool enabled = web_vr_mode_ && !secure_origin_;
  permanent_security_warning_->set_visible(enabled);
  transient_security_warning_->set_visible(enabled);
  if (enabled) {
    security_warning_timer_.Start(
        FROM_HERE, base::TimeDelta::FromSeconds(kWarningTimeoutSeconds), this,
        &UiSceneManager::OnSecurityWarningTimer);
  } else {
    security_warning_timer_.Stop();
  }
}

void UiSceneManager::OnSecurityWarningTimer() {
  transient_security_warning_->set_visible(false);
}

void UiSceneManager::OnBackButtonClicked() {
  browser_->NavigateBack();
}

void UiSceneManager::SetURL(const GURL& gurl) {
  url_bar_->SetURL(gurl);
}

void UiSceneManager::SetSecurityLevel(int level) {
  url_bar_->SetSecurityLevel(level);
}

void UiSceneManager::SetLoading(bool loading) {
  loading_indicator_->SetLoading(loading);
}

void UiSceneManager::SetLoadProgress(float progress) {
  loading_indicator_->SetLoadProgress(progress);
}

void UiSceneManager::SetIsExiting() {
  if (scene_->is_exiting())
    return;
  scene_->set_is_exiting();
  ConfigureScene();
}

void UiSceneManager::SetHistoryButtonsEnabled(bool can_go_back,
                                              bool can_go_forward) {}

void UiSceneManager::OnCloseButtonClicked() {
  browser_->ExitCct();
}

int UiSceneManager::AllocateId() {
  return next_available_id_++;
}

}  // namespace vr_shell
