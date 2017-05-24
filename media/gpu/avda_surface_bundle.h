// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_AVDA_SURFACE_BUNDLE_H_
#define MEDIA_GPU_AVDA_SURFACE_BUNDLE_H_

#include "base/memory/ref_counted.h"
#include "media/base/android/android_overlay.h"
#include "media/base/surface_manager.h"
#include "media/gpu/media_gpu_export.h"
#include "media/gpu/surface_texture_gl_owner.h"
#include "ui/gl/android/scoped_java_surface.h"

namespace media {

// AVDASurfaceBundle is a Java surface, and the SurfaceTexture or Overlay that
// backs it.
//
// Once a MediaCodec is configured with an output surface, the corresponding
// AVDASurfaceBundle should be kept alive as long as the codec to prevent
// crashes due to the codec losing its output surface.
// TODO(watk): Remove AVDA from the name.
struct MEDIA_GPU_EXPORT AVDASurfaceBundle
    : public base::RefCountedThreadSafe<AVDASurfaceBundle> {
 public:
  // Create an empty bundle to be manually populated.
  explicit AVDASurfaceBundle();
  explicit AVDASurfaceBundle(std::unique_ptr<AndroidOverlay> overlay);
  explicit AVDASurfaceBundle(
      scoped_refptr<SurfaceTextureGLOwner> surface_texture_owner);

  const base::android::JavaRef<jobject>& GetJavaSurface() const;

  // The Overlay or SurfaceTexture.
  std::unique_ptr<AndroidOverlay> overlay;
  scoped_refptr<SurfaceTextureGLOwner> surface_texture;

  // The Java surface for |surface_texture|.
  gl::ScopedJavaSurface surface_texture_surface;

 private:
  ~AVDASurfaceBundle();
  friend class base::RefCountedThreadSafe<AVDASurfaceBundle>;

  DISALLOW_COPY_AND_ASSIGN(AVDASurfaceBundle);
};

}  // namespace media

#endif  // MEDIA_GPU_AVDA_SURFACE_BUNDLE_H_
