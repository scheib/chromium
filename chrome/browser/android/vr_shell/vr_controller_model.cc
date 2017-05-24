// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/vr_shell/vr_controller_model.h"

#include "base/memory/ptr_util.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/android/vr_shell/gltf_parser.h"
#include "chrome/grit/browser_resources.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/codec/png_codec.h"

namespace vr_shell {

namespace {

enum {
  ELEMENTS_BUFFER_ID = 0,
  INDICES_BUFFER_ID = 1,
};

constexpr char kPosition[] = "POSITION";
constexpr char kTexCoord[] = "TEXCOORD_0";

const int kTexturePatchesResources[] = {
    -1, IDR_VR_SHELL_DDCONTROLLER_TOUCHPAD_PATCH,
    IDR_VR_SHELL_DDCONTROLLER_APP_PATCH, IDR_VR_SHELL_DDCONTROLLER_SYSTEM_PATCH,
};
const gfx::Point kPatchesLocations[] = {{}, {5, 5}, {47, 165}, {47, 234}};

sk_sp<SkImage> LoadPng(int resource_id) {
  base::StringPiece data =
      ResourceBundle::GetSharedInstance().GetRawDataResource(resource_id);
  SkBitmap bitmap;
  bool decoded =
      gfx::PNGCodec::Decode(reinterpret_cast<const unsigned char*>(data.data()),
                            data.size(), &bitmap);
  DCHECK(decoded);
  DCHECK(bitmap.colorType() == kRGBA_8888_SkColorType);
  return SkImage::MakeFromBitmap(bitmap);
}

}  // namespace

VrControllerModel::VrControllerModel(
    std::unique_ptr<gltf::Asset> gltf_asset,
    std::vector<std::unique_ptr<gltf::Buffer>> buffers)
    : gltf_asset_(std::move(gltf_asset)),
      buffers_(std::move(buffers)) {}

VrControllerModel::~VrControllerModel() = default;

const GLvoid* VrControllerModel::ElementsBuffer() const {
  const gltf::BufferView* buffer_view =
      gltf_asset_->GetBufferView(ELEMENTS_BUFFER_ID);
  DCHECK(buffer_view && buffer_view->target == GL_ARRAY_BUFFER);
  const char* buffer = Buffer();
  return buffer ? buffer + buffer_view->byte_offset : nullptr;
}

GLsizeiptr VrControllerModel::ElementsBufferSize() const {
  const gltf::BufferView* buffer_view =
      gltf_asset_->GetBufferView(ELEMENTS_BUFFER_ID);
  DCHECK(buffer_view && buffer_view->target == GL_ARRAY_BUFFER);
  return buffer_view->byte_length;
}

const GLvoid* VrControllerModel::IndicesBuffer() const {
  const gltf::BufferView* buffer_view =
      gltf_asset_->GetBufferView(INDICES_BUFFER_ID);
  DCHECK(buffer_view && buffer_view->target == GL_ELEMENT_ARRAY_BUFFER);
  const char* buffer = Buffer();
  return buffer ? buffer + buffer_view->byte_offset : nullptr;
}

GLsizeiptr VrControllerModel::IndicesBufferSize() const {
  const gltf::BufferView* buffer_view =
      gltf_asset_->GetBufferView(INDICES_BUFFER_ID);
  DCHECK(buffer_view && buffer_view->target == GL_ELEMENT_ARRAY_BUFFER);
  return buffer_view->byte_length;
}

GLenum VrControllerModel::DrawMode() const {
  const gltf::Mesh* mesh = gltf_asset_->GetMesh(0);
  DCHECK(mesh && mesh->primitives.size());
  return mesh->primitives[0]->mode;
}

const gltf::Accessor* VrControllerModel::IndicesAccessor() const {
  const gltf::Mesh* mesh = gltf_asset_->GetMesh(0);
  DCHECK(mesh && mesh->primitives.size());
  return mesh->primitives[0]->indices;
}

const gltf::Accessor* VrControllerModel::PositionAccessor() const {
  return Accessor(kPosition);
}

const gltf::Accessor* VrControllerModel::TextureCoordinateAccessor() const {
  return Accessor(kTexCoord);
}

void VrControllerModel::SetBaseTexture(sk_sp<SkImage> image) {
  base_texture_ = image;
}

void VrControllerModel::SetTexture(int state, sk_sp<SkImage> patch) {
  DCHECK(state >= 0 && state < STATE_COUNT);
  if (!patch) {
    textures_[state] = base_texture_;
    return;
  }
  sk_sp<SkSurface> surface = SkSurface::MakeRasterN32Premul(
      base_texture_->width(), base_texture_->height());
  SkCanvas* canvas = surface->getCanvas();
  canvas->drawImage(base_texture_, 0, 0);
  SkPaint paint;
  paint.setBlendMode(SkBlendMode::kSrc);
  canvas->drawImage(patch, kPatchesLocations[state].x(),
                    kPatchesLocations[state].y(), &paint);
  textures_[state] = sk_sp<SkImage>(surface->makeImageSnapshot());
}

sk_sp<SkImage> VrControllerModel::GetTexture(int state) const {
  DCHECK(state >= 0 && state < STATE_COUNT);
  return textures_[state];
}

const char* VrControllerModel::Buffer() const {
  if (buffers_.empty())
    return nullptr;
  return buffers_[0]->data();
}

const gltf::Accessor* VrControllerModel::Accessor(
    const std::string& key) const {
  const gltf::Mesh* mesh = gltf_asset_->GetMesh(0);
  DCHECK(mesh && mesh->primitives.size());
  auto it = mesh->primitives[0]->attributes.find(key);
  DCHECK(it != mesh->primitives[0]->attributes.begin());
  return it->second;
}

std::unique_ptr<VrControllerModel> VrControllerModel::LoadFromResources() {
  TRACE_EVENT0("gpu", "VrControllerModel::LoadFromResources");
  std::vector<std::unique_ptr<gltf::Buffer>> buffers;
  auto model_data = ResourceBundle::GetSharedInstance().GetRawDataResource(
      IDR_VR_SHELL_DDCONTROLLER_MODEL);
  std::unique_ptr<gltf::Asset> asset =
      BinaryGltfParser::Parse(model_data, &buffers);
  DCHECK(asset);

  auto controller_model =
      base::MakeUnique<VrControllerModel>(std::move(asset), std::move(buffers));
  sk_sp<SkImage> base_texture = LoadPng(IDR_VR_SHELL_DDCONTROLLER_IDLE_TEXTURE);
  controller_model->SetBaseTexture(std::move(base_texture));

  for (int i = 0; i < VrControllerModel::STATE_COUNT; i++) {
    if (kTexturePatchesResources[i] == -1) {
      controller_model->SetTexture(i, nullptr);
    } else {
      auto patch_image = LoadPng(kTexturePatchesResources[i]);
      controller_model->SetTexture(i, patch_image);
    }
  }

  return controller_model;
}

}  // namespace vr_shell
