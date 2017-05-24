// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_UI_SCENE_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_UI_SCENE_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/android/vr_shell/ui_elements/ui_element_debug_id.h"
#include "device/vr/vr_types.h"

namespace base {
class ListValue;
class TimeTicks;
}

namespace vr_shell {

class Animation;
class UiElement;

class UiScene {
 public:
  enum Command {
    ADD_ELEMENT,
    UPDATE_ELEMENT,
    REMOVE_ELEMENT,
    ADD_ANIMATION,
    REMOVE_ANIMATION,
    CONFIGURE_SCENE,
  };

  UiScene();
  virtual ~UiScene();

  void AddUiElement(std::unique_ptr<UiElement> element);

  void RemoveUiElement(int element_id);

  // Add an animation to the scene, on element |element_id|.
  void AddAnimation(int element_id, std::unique_ptr<Animation> animation);

  // Remove |animation_id| from element |element_id|.
  void RemoveAnimation(int element_id, int animation_id);

  // Handles per-frame updates, giving each element the opportunity to update,
  // if necessary (eg, for animations). NB: |current_time| is the shared,
  // absolute begin frame time.
  void OnBeginFrame(const base::TimeTicks& current_time);

  // Handle a batch of commands passed from the UI HTML.
  void HandleCommands(std::unique_ptr<base::ListValue> commands,
                      const base::TimeTicks& current_time);

  const std::vector<std::unique_ptr<UiElement>>& GetUiElements() const;

  UiElement* GetUiElementById(int element_id) const;
  UiElement* GetUiElementByDebugId(UiElementDebugId debug_id) const;

  std::vector<const UiElement*> GetWorldElements() const;
  std::vector<const UiElement*> GetOverlayElements() const;
  std::vector<const UiElement*> GetHeadLockedElements() const;
  bool HasVisibleHeadLockedElements() const;

  void SetBackgroundColor(const vr::Colorf& color);
  const vr::Colorf& GetBackgroundColor() const;
  void SetBackgroundDistance(float distance);
  float GetBackgroundDistance() const;

  bool GetWebVrRenderingEnabled() const;
  void SetWebVrRenderingEnabled(bool enabled);

  bool is_exiting() const { return is_exiting_; }
  void set_is_exiting();

  void OnGLInitialized();

 private:
  void Animate(const base::TimeTicks& current_time);
  void ApplyRecursiveTransforms(UiElement* element);

  std::vector<std::unique_ptr<UiElement>> ui_elements_;
  UiElement* content_element_ = nullptr;
  vr::Colorf background_color_ = {0.1f, 0.1f, 0.1f, 1.0f};
  float background_distance_ = 10.0f;
  bool webvr_rendering_enabled_ = true;
  bool gl_initialized_ = false;
  bool is_exiting_ = false;

  DISALLOW_COPY_AND_ASSIGN(UiScene);
};

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_UI_SCENE_H_
