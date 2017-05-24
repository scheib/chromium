// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/android/media_codec_video_decoder.h"
#include "base/android/jni_android.h"
#include "base/bind.h"
#include "base/run_loop.h"
#include "base/test/scoped_task_environment.h"
#include "media/base/android/media_codec_util.h"
#include "media/base/android/media_jni_registrar.h"
#include "media/base/decoder_buffer.h"
#include "media/base/test_helpers.h"
#include "media/gpu/android/fake_codec_allocator.h"
#include "media/gpu/android_video_surface_chooser_impl.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/init/gl_factory.h"

using ::testing::_;

namespace media {
namespace {

#define SKIP_IF_MEDIA_CODEC_IS_BLACKLISTED()                                 \
  do {                                                                       \
    if (!MediaCodecUtil::IsMediaCodecAvailable()) {                          \
      DVLOG(0) << "Skipping test: MediaCodec is blacklisted on this device"; \
      return;                                                                \
    }                                                                        \
  } while (0)

void OutputCb(const scoped_refptr<VideoFrame>& frame) {}

void DecodeCb(DecodeStatus) {}

gpu::GpuCommandBufferStub* GetCommandBufferStubCb() {
  return nullptr;
}

}  // namespace

class MediaCodecVideoDecoderTest : public testing::Test {
 public:
  MediaCodecVideoDecoderTest() {
    JNIEnv* env = base::android::AttachCurrentThread();
    RegisterJni(env);

    // We set up GL so that we can create SurfaceTextures.
    // TODO(watk): Create a mock SurfaceTexture so we don't have to
    // do this.
    DCHECK(gl::init::InitializeGLOneOff());
    surface_ = gl::init::CreateOffscreenGLSurface(gfx::Size(16, 16));
    context_ = gl::init::CreateGLContext(nullptr, surface_.get(),
                                         gl::GLContextAttribs());
    context_->MakeCurrent(surface_.get());

    codec_allocator_ = base::MakeUnique<FakeCodecAllocator>();
    mcvd_ = base::MakeUnique<MediaCodecVideoDecoder>(
        base::ThreadTaskRunnerHandle::Get(),
        base::Bind(&GetCommandBufferStubCb), codec_allocator_.get(),
        base::MakeUnique<AndroidVideoSurfaceChooserImpl>());
  }

  ~MediaCodecVideoDecoderTest() override {
    // ~AVDASurfaceBundle() might rely on GL being available, so we have to
    // explicitly drop references to them before tearing down GL.
    mcvd_ = nullptr;
    codec_allocator_ = nullptr;
    context_ = nullptr;
    surface_ = nullptr;
    gl::init::ShutdownGL();
  }

  bool Initialize(const VideoDecoderConfig& config) {
    bool result = false;
    auto init_cb = [](bool* result_out, bool result) { *result_out = result; };
    mcvd_->Initialize(config, false, nullptr, base::Bind(init_cb, &result),
                      base::Bind(&OutputCb));
    base::RunLoop().RunUntilIdle();
    return result;
  }

  void TearDown() override {}

 protected:
  std::unique_ptr<MediaCodecVideoDecoder> mcvd_;
  std::unique_ptr<FakeCodecAllocator> codec_allocator_;

  scoped_refptr<gl::GLSurface> surface_;
  scoped_refptr<gl::GLContext> context_;
  base::test::ScopedTaskEnvironment scoped_task_environment_;
};

TEST_F(MediaCodecVideoDecoderTest, DestructBeforeInitWorks) {
  // Do nothing.
}

TEST_F(MediaCodecVideoDecoderTest, UnknownCodecIsRejected) {
  SKIP_IF_MEDIA_CODEC_IS_BLACKLISTED();
  ASSERT_FALSE(Initialize(TestVideoConfig::Invalid()));
}

TEST_F(MediaCodecVideoDecoderTest, H264IsSupported) {
  SKIP_IF_MEDIA_CODEC_IS_BLACKLISTED();
  // H264 is always supported by MCVD.
  ASSERT_TRUE(Initialize(TestVideoConfig::NormalH264()));
}

TEST_F(MediaCodecVideoDecoderTest, SmallVp8IsRejected) {
  SKIP_IF_MEDIA_CODEC_IS_BLACKLISTED();
  ASSERT_TRUE(Initialize(TestVideoConfig::NormalH264()));
}

TEST_F(MediaCodecVideoDecoderTest, InitializeDoesNotCreateACodec) {
  SKIP_IF_MEDIA_CODEC_IS_BLACKLISTED();
  EXPECT_CALL(*codec_allocator_, MockCreateMediaCodecAsync(_, _)).Times(0);
  Initialize(TestVideoConfig::NormalH264());
  base::RunLoop().RunUntilIdle();
}

TEST_F(MediaCodecVideoDecoderTest, DecodeTriggersLazyInit) {
  SKIP_IF_MEDIA_CODEC_IS_BLACKLISTED();
  Initialize(TestVideoConfig::NormalH264());
  EXPECT_CALL(*codec_allocator_, MockCreateMediaCodecAsync(_, _));
  mcvd_->Decode(nullptr, base::Bind(&DecodeCb));
  base::RunLoop().RunUntilIdle();
}

}  // namespace media
