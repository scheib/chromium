// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_VR_CONTROLLER_MODEL_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_VR_CONTROLLER_MODEL_H_

#include <memory>

#include "chrome/browser/android/vr_shell/gltf_asset.h"
#include "third_party/skia/include/core/SkImage.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gl/gl_bindings.h"

namespace vr_shell {

class VrControllerModel {
 public:
  enum State {
    IDLE = 0,
    TOUCHPAD,
    APP,
    SYSTEM,
    // New ControllerStates should be added here, before STATE_COUNT.
    STATE_COUNT,
  };

  explicit VrControllerModel(
      std::unique_ptr<gltf::Asset> gltf_asset,
      std::vector<std::unique_ptr<gltf::Buffer>> buffers);
  ~VrControllerModel();

  const GLvoid* ElementsBuffer() const;
  GLsizeiptr ElementsBufferSize() const;
  const GLvoid* IndicesBuffer() const;
  GLenum DrawMode() const;
  GLsizeiptr IndicesBufferSize() const;
  const gltf::Accessor* IndicesAccessor() const;
  const gltf::Accessor* PositionAccessor() const;
  const gltf::Accessor* TextureCoordinateAccessor() const;
  void SetBaseTexture(sk_sp<SkImage> image);
  void SetTexture(int state, sk_sp<SkImage> patch);
  sk_sp<SkImage> GetTexture(int state) const;

  static std::unique_ptr<VrControllerModel> LoadFromResources();

 private:
  std::unique_ptr<gltf::Asset> gltf_asset_;
  sk_sp<SkImage> base_texture_;
  sk_sp<SkImage> textures_[STATE_COUNT];
  std::vector<std::unique_ptr<gltf::Buffer>> buffers_;

  const char* Buffer() const;
  const gltf::Accessor* Accessor(const std::string& key) const;
};

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_VR_CONTROLLER_MODEL_H_
