// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/android_video_decode_accelerator.h"

#include <stdint.h>

#include <memory>

#include "base/android/jni_android.h"
#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "gpu/command_buffer/service/gles2_cmd_decoder_mock.h"
#include "media/base/android/media_codec_util.h"
#include "media/base/android/media_jni_registrar.h"
#include "media/base/android/mock_android_overlay.h"
#include "media/base/android/mock_media_codec_bridge.h"
#include "media/gpu/android/fake_codec_allocator.h"
#include "media/gpu/android_video_decode_accelerator.h"
#include "media/gpu/android_video_surface_chooser.h"
#include "media/gpu/avda_codec_allocator.h"
#include "media/video/picture.h"
#include "media/video/video_decode_accelerator.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gl/android/surface_texture.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/init/gl_factory.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;

namespace media {
namespace {

#define SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE()     \
  do {                                            \
    if (!MediaCodecUtil::IsMediaCodecAvailable()) \
      return;                                     \
  } while (false)

bool MakeContextCurrent() {
  return true;
}

base::WeakPtr<gpu::gles2::GLES2Decoder> GetGLES2Decoder(
    const base::WeakPtr<gpu::gles2::GLES2Decoder>& decoder) {
  return decoder;
}

class MockVDAClient : public VideoDecodeAccelerator::Client {
 public:
  MockVDAClient() {}

  MOCK_METHOD1(NotifyInitializationComplete, void(bool));
  MOCK_METHOD5(
      ProvidePictureBuffers,
      void(uint32_t, VideoPixelFormat, uint32_t, const gfx::Size&, uint32_t));
  MOCK_METHOD1(DismissPictureBuffer, void(int32_t));
  MOCK_METHOD1(PictureReady, void(const Picture&));
  MOCK_METHOD1(NotifyEndOfBitstreamBuffer, void(int32_t));
  MOCK_METHOD0(NotifyFlushDone, void());
  MOCK_METHOD0(NotifyResetDone, void());
  MOCK_METHOD1(NotifyError, void(VideoDecodeAccelerator::Error));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockVDAClient);
};

// Surface chooser that calls mocked functions to allow counting calls, but
// also records arguments.  Note that gmock can't mock out unique_ptrs
// anyway, so we can't mock AndroidVideoSurfaceChooser directly.
class FakeOverlayChooser : public NiceMock<AndroidVideoSurfaceChooser> {
 public:
  FakeOverlayChooser() : weak_factory_(this) {}

  // These are called by the real functions.  You may set expectations on
  // them if you like.
  MOCK_METHOD0(MockInitialize, void());
  MOCK_METHOD0(MockReplaceOverlayFactory, void());

  // We guarantee that we'll only clear this during destruction, so that you
  // may treat it as "pointer that lasts as long as |this| does".
  base::WeakPtr<FakeOverlayChooser> GetWeakPtrForTesting() {
    return weak_factory_.GetWeakPtr();
  }

  void Initialize(UseOverlayCB use_overlay_cb,
                  UseSurfaceTextureCB use_surface_texture_cb,
                  AndroidOverlayFactoryCB initial_factory) override {
    MockInitialize();

    factory_ = std::move(initial_factory);
    use_overlay_cb_ = std::move(use_overlay_cb);
    use_surface_texture_cb_ = std::move(use_surface_texture_cb);
  }

  void ReplaceOverlayFactory(AndroidOverlayFactoryCB factory) override {
    MockReplaceOverlayFactory();
    factory_ = std::move(factory);
  }

  // Notify AVDA to use a surface texture.
  void ProvideSurfaceTexture() {
    use_surface_texture_cb_.Run();
    base::RunLoop().RunUntilIdle();
  }

  void ProvideOverlay(std::unique_ptr<AndroidOverlay> overlay) {
    use_overlay_cb_.Run(std::move(overlay));
    base::RunLoop().RunUntilIdle();
  }

  UseOverlayCB use_overlay_cb_;
  UseSurfaceTextureCB use_surface_texture_cb_;

  AndroidOverlayFactoryCB factory_;

  base::WeakPtrFactory<FakeOverlayChooser> weak_factory_;

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeOverlayChooser);
};

}  // namespace

class AndroidVideoDecodeAcceleratorTest : public testing::Test {
 public:
  // We pick this profile since it's always supported.
  AndroidVideoDecodeAcceleratorTest() : config_(H264PROFILE_BASELINE) {}

  ~AndroidVideoDecodeAcceleratorTest() override {}

  void SetUp() override {
    JNIEnv* env = base::android::AttachCurrentThread();
    RegisterJni(env);

    main_task_runner_ = base::ThreadTaskRunnerHandle::Get();

    gl::init::ShutdownGL();
    ASSERT_TRUE(gl::init::InitializeGLOneOff());
    surface_ = gl::init::CreateOffscreenGLSurface(gfx::Size(16, 16));
    context_ = gl::init::CreateGLContext(nullptr, surface_.get(),
                                         gl::GLContextAttribs());
    context_->MakeCurrent(surface_.get());

    chooser_that_is_usually_null_ = base::MakeUnique<FakeOverlayChooser>();
    chooser_ = chooser_that_is_usually_null_->GetWeakPtrForTesting();

    platform_config_.sdk_int = base::android::SDK_VERSION_MARSHMALLOW;
    platform_config_.allow_setsurface = true;
    platform_config_.force_deferred_surface_creation = false;

    // By default, allow deferred init.
    config_.is_deferred_initialization_allowed = true;
  }

  // Create and initialize AVDA with |config_|, and return the result.
  bool InitializeAVDA() {
    // Because VDA has a custom deleter, we must assign it to |vda_| carefully.
    AndroidVideoDecodeAccelerator* avda = new AndroidVideoDecodeAccelerator(
        &codec_allocator_, std::move(chooser_that_is_usually_null_),
        base::Bind(&MakeContextCurrent),
        base::Bind(&GetGLES2Decoder, gl_decoder_.AsWeakPtr()),
        AndroidOverlayMojoFactoryCB(), platform_config_);
    vda_.reset(avda);

    bool result = vda_->Initialize(config_, &client_);
    base::RunLoop().RunUntilIdle();
    return result;
  }

  // Initialize |vda_|, providing a new surface for it.  You may get the surface
  // by asking |codec_allocator_|.
  void InitializeAVDAWithOverlay() {
    config_.surface_id = 123;
    ASSERT_TRUE(InitializeAVDA());
    base::RunLoop().RunUntilIdle();
    ASSERT_TRUE(chooser_->factory_);

    // Have the factory provide an overlay, and verify that codec creation is
    // provided with that overlay.
    std::unique_ptr<MockAndroidOverlay> overlay =
        base::MakeUnique<MockAndroidOverlay>();
    overlay_callbacks_ = overlay->GetCallbacks();

    // Set the expectations first, since ProvideOverlay might cause callbacks.
    EXPECT_CALL(codec_allocator_,
                MockCreateMediaCodecAsync(overlay.get(), nullptr));
    chooser_->ProvideOverlay(std::move(overlay));

    // Provide the codec so that we can check if it's freed properly.
    EXPECT_CALL(client_, NotifyInitializationComplete(true));
    codec_allocator_.ProvideMockCodecAsync();
    base::RunLoop().RunUntilIdle();
  }

  void InitializeAVDAWithSurfaceTexture() {
    ASSERT_TRUE(InitializeAVDA());
    base::RunLoop().RunUntilIdle();
    // We do not expect a factory, since we are using SurfaceTexture.
    ASSERT_FALSE(chooser_->factory_);

    // Set the expectations first, since ProvideOverlay might cause callbacks.
    EXPECT_CALL(codec_allocator_,
                MockCreateMediaCodecAsync(nullptr, NotNull()));
    chooser_->ProvideSurfaceTexture();

    // Provide the codec so that we can check if it's freed properly.
    EXPECT_CALL(client_, NotifyInitializationComplete(true));
    codec_allocator_.ProvideMockCodecAsync();
    base::RunLoop().RunUntilIdle();
  }

  // Set whether HasUnrendereredPictureBuffers will return true or false.
  // TODO(liberato): We can't actually do this yet.  It turns out to be okay,
  // because AVDA doesn't actually SetSurface before DequeueOutput.  It could do
  // so, though, if there aren't unrendered buffers.  Should AVDA ever start
  // switching surfaces immediately upon receiving them, rather than waiting for
  // DequeueOutput, then we'll want to be able to indicate that it has
  // unrendered pictures to prevent that behavior.
  void SetHasUnrenderedPictureBuffers(bool flag) {}

  // Tell |avda_| to switch surfaces to its incoming surface.  This is a method
  // since we're a friend of AVDA, and the tests are subclasses.  It's also
  // somewhat hacky, but much less hacky than trying to run it via a timer.
  void LetAVDAUpdateSurface() {
    SetHasUnrenderedPictureBuffers(false);
    avda()->DequeueOutput();
  }

  base::MessageLoop message_loop_;
  scoped_refptr<gl::GLSurface> surface_;
  scoped_refptr<gl::GLContext> context_;
  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
  NiceMock<gpu::gles2::MockGLES2Decoder> gl_decoder_;
  NiceMock<MockVDAClient> client_;
  FakeCodecAllocator codec_allocator_;
  VideoDecodeAccelerator::Config config_;

  AndroidVideoDecodeAccelerator::PlatformConfig platform_config_;

  // Set by InitializeAVDAWithOverlay()
  MockAndroidOverlay::Callbacks overlay_callbacks_;

  // We maintain a weak ref to this since AVDA owns it.
  base::WeakPtr<FakeOverlayChooser> chooser_;

  // This must be a unique pointer to a VDA, not an AVDA, to ensure the
  // the default_delete specialization that calls Destroy() will be used.
  std::unique_ptr<VideoDecodeAccelerator> vda_;

  AndroidVideoDecodeAccelerator* avda() {
    return reinterpret_cast<AndroidVideoDecodeAccelerator*>(vda_.get());
  }

 private:
  // This is the object that |chooser_| points to, or nullptr once we assign
  // ownership to AVDA.
  std::unique_ptr<FakeOverlayChooser> chooser_that_is_usually_null_;
};

TEST_F(AndroidVideoDecodeAcceleratorTest, ConfigureUnsupportedCodec) {
  SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE();

  config_ = VideoDecodeAccelerator::Config(VIDEO_CODEC_PROFILE_UNKNOWN);
  ASSERT_FALSE(InitializeAVDA());
}

TEST_F(AndroidVideoDecodeAcceleratorTest,
       ConfigureSupportedCodecSynchronously) {
  SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE();

  config_.is_deferred_initialization_allowed = false;

  EXPECT_CALL(codec_allocator_, MockCreateMediaCodecSync(_, _));
  ASSERT_TRUE(InitializeAVDA());
}

TEST_F(AndroidVideoDecodeAcceleratorTest, FailingToCreateACodecSyncIsAnError) {
  // Failuew to create a codec during sync init should cause Initialize to fail.
  SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE();

  config_.is_deferred_initialization_allowed = false;
  codec_allocator_.allow_sync_creation = false;

  EXPECT_CALL(codec_allocator_, MockCreateMediaCodecSync(nullptr, NotNull()));
  ASSERT_FALSE(InitializeAVDA());
}

TEST_F(AndroidVideoDecodeAcceleratorTest, FailingToCreateACodecAsyncIsAnError) {
  // Verify that a null codec signals error for async init when it doesn't get a
  // mediacodec instance.
  //
  // Also assert that there's only one call to CreateMediaCodecAsync. And since
  // it replies with a null codec, AVDA will be in an error state when it shuts
  // down.  Since we know that it's constructed before we destroy the VDA, we
  // verify that AVDA doens't create codecs during destruction.
  SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE();

  // Note that if we somehow end up deferring surface creation, then this would
  // no longer be expected to fail.  It would signal success before asking for a
  // surface or codec.
  EXPECT_CALL(codec_allocator_, MockCreateMediaCodecAsync(_, NotNull()));
  EXPECT_CALL(client_, NotifyInitializationComplete(false));

  ASSERT_TRUE(InitializeAVDA());
  chooser_->ProvideSurfaceTexture();
  codec_allocator_.ProvideNullCodecAsync();

  // Make sure that codec allocation has happened before destroying the VDA.
  testing::Mock::VerifyAndClearExpectations(&codec_allocator_);
}

TEST_F(AndroidVideoDecodeAcceleratorTest,
       LowEndDevicesSucceedInitWithoutASurface) {
  // If AVDA decides that we should defer surface creation, then it should
  // signal success before we provide a surface.  It should still ask for a
  // surface, though.
  SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE();

  // It would be nicer if we didn't just force this on, since we might do so
  // in a state that AVDA isn't supposed to handle (e.g., if we give it a
  // surface, then it would never decide to defer surface creation).
  platform_config_.force_deferred_surface_creation = true;
  config_.surface_id = SurfaceManager::kNoSurfaceID;

  EXPECT_CALL(*chooser_, MockInitialize()).Times(0);
  EXPECT_CALL(client_, NotifyInitializationComplete(true));

  InitializeAVDA();
}

TEST_F(AndroidVideoDecodeAcceleratorTest,
       AsyncInitWithSurfaceTextureAndDelete) {
  // When configuring with a SurfaceTexture and deferred init, we should be
  // asked for a codec, and be notified of init success if we provide one. When
  // AVDA is destroyed, it should release the codec and surface texture.
  SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE();

  InitializeAVDAWithSurfaceTexture();

  // Delete the VDA, and make sure that it tries to free the codec and the right
  // surface texture.
  EXPECT_CALL(
      codec_allocator_,
      MockReleaseMediaCodec(codec_allocator_.most_recent_codec(),
                            codec_allocator_.most_recent_overlay(),
                            codec_allocator_.most_recent_surface_texture()));
  codec_allocator_.codec_destruction_observer()->ExpectDestruction();
  vda_ = nullptr;
  base::RunLoop().RunUntilIdle();
}

TEST_F(AndroidVideoDecodeAcceleratorTest, AsyncInitWithSurfaceAndDelete) {
  // When |config_| specifies a surface, we should be given a factory during
  // startup for it.  When |chooser_| provides an overlay, the codec should be
  // allocated using it.  Shutdown should provide the overlay when releasing the
  // media codec.
  SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE();

  InitializeAVDAWithOverlay();

  // Delete the VDA, and make sure that it tries to free the codec and the
  // overlay that it provided to us.
  EXPECT_CALL(
      codec_allocator_,
      MockReleaseMediaCodec(codec_allocator_.most_recent_codec(),
                            codec_allocator_.most_recent_overlay(),
                            codec_allocator_.most_recent_surface_texture()));
  codec_allocator_.codec_destruction_observer()->ExpectDestruction();
  vda_ = nullptr;
  base::RunLoop().RunUntilIdle();
}

TEST_F(AndroidVideoDecodeAcceleratorTest,
       SwitchesToSurfaceTextureWhenSurfaceDestroyed) {
  // Provide a surface, and a codec, then destroy the surface.  AVDA should use
  // SetSurface to switch to SurfaceTexture.
  SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE();

  InitializeAVDAWithOverlay();

  // It would be nice if we knew that this was a surface texture.  As it is, we
  // just destroy the VDA and expect that we're provided with one.  Hopefully,
  // AVDA is actually calling SetSurface properly.
  EXPECT_CALL(*codec_allocator_.most_recent_codec(), SetSurface(_))
      .WillOnce(Return(true));
  codec_allocator_.codec_destruction_observer()->DestructionIsOptional();
  overlay_callbacks_.SurfaceDestroyed.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(codec_allocator_,
              MockReleaseMediaCodec(codec_allocator_.most_recent_codec(),
                                    nullptr, NotNull()));
  vda_ = nullptr;
  base::RunLoop().RunUntilIdle();
}

TEST_F(AndroidVideoDecodeAcceleratorTest, SwitchesToSurfaceTextureEventually) {
  // Provide a surface, and a codec, then request that AVDA switches to a
  // surface texture.  Verify that it does.
  SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE();

  InitializeAVDAWithOverlay();

  EXPECT_CALL(*codec_allocator_.most_recent_codec(), SetSurface(_))
      .WillOnce(Return(true));

  // Note that it's okay if |avda_| switches before ProvideSurfaceTexture
  // returns, since it has no queued output anyway.
  chooser_->ProvideSurfaceTexture();
  LetAVDAUpdateSurface();

  // Verify that we're now using some surface texture.
  EXPECT_CALL(codec_allocator_,
              MockReleaseMediaCodec(codec_allocator_.most_recent_codec(),
                                    nullptr, NotNull()));
  codec_allocator_.codec_destruction_observer()->ExpectDestruction();
  vda_ = nullptr;
  base::RunLoop().RunUntilIdle();
}

TEST_F(AndroidVideoDecodeAcceleratorTest,
       SetSurfaceFailureDoesntSwitchSurfaces) {
  // Initialize AVDA with a surface, then request that AVDA switches to a
  // surface texture.  When it tries to UpdateSurface, pretend to fail.  AVDA
  // should notify error, and also release the original surface.
  SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE();

  InitializeAVDAWithOverlay();

  EXPECT_CALL(*codec_allocator_.most_recent_codec(), SetSurface(_))
      .WillOnce(Return(false));
  EXPECT_CALL(client_,
              NotifyError(AndroidVideoDecodeAccelerator::PLATFORM_FAILURE))
      .Times(1);
  codec_allocator_.codec_destruction_observer()->DestructionIsOptional();
  chooser_->ProvideSurfaceTexture();
  LetAVDAUpdateSurface();
}

TEST_F(AndroidVideoDecodeAcceleratorTest,
       SwitchToSurfaceAndBackBeforeSetSurface) {
  // Ask AVDA to switch from ST to overlay, then back to ST before it has a
  // chance to do the first switch.  It should simply drop the overlay.
  SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE();

  InitializeAVDAWithSurfaceTexture();

  // Don't let AVDA switch immediately, else it could choose to SetSurface when
  // it first gets the overlay.
  SetHasUnrenderedPictureBuffers(true);
  EXPECT_CALL(*codec_allocator_.most_recent_codec(), SetSurface(_)).Times(0);
  std::unique_ptr<MockAndroidOverlay> overlay =
      base::MakeUnique<MockAndroidOverlay>();
  // Make sure that the overlay is not destroyed too soon.
  std::unique_ptr<DestructionObservable::DestructionObserver> observer =
      overlay->CreateDestructionObserver();
  observer->DoNotAllowDestruction();

  chooser_->ProvideOverlay(std::move(overlay));

  // Now it is expected to drop the overlay.
  observer->ExpectDestruction();

  // While the incoming surface is pending, switch back to SurfaceTexture.
  chooser_->ProvideSurfaceTexture();
}

TEST_F(AndroidVideoDecodeAcceleratorTest,
       ChangingOutputSurfaceVoluntarilyWithoutSetSurfaceIsIgnored) {
  // If we ask AVDA to change to SurfaceTexture should be ignored on platforms
  // that don't support SetSurface (pre-M or blacklisted).  It should also
  // ignore SurfaceTexture => overlay, but we don't check that.
  //
  // Also note that there are other probably reasonable things to do (like
  // signal an error), but we want to be sure that it doesn't try to SetSurface.
  // We also want to be sure that, if it doesn't signal an error, that it also
  // doesn't get confused about which surface is in use.  So, we assume that it
  // doesn't signal an error, and we check that it releases the right surface
  // with the codec.
  EXPECT_CALL(client_, NotifyError(_)).Times(0);

  platform_config_.allow_setsurface = false;
  InitializeAVDAWithOverlay();
  EXPECT_CALL(*codec_allocator_.most_recent_codec(), SetSurface(_)).Times(0);

  // This should not switch to SurfaceTexture.
  chooser_->ProvideSurfaceTexture();
  LetAVDAUpdateSurface();
}

TEST_F(AndroidVideoDecodeAcceleratorTest,
       OnSurfaceDestroyedWithoutSetSurfaceFreesTheCodec) {
  // If AVDA receives OnSurfaceDestroyed without support for SetSurface, then it
  // should free the codec.
  platform_config_.allow_setsurface = false;
  InitializeAVDAWithOverlay();
  EXPECT_CALL(*codec_allocator_.most_recent_codec(), SetSurface(_)).Times(0);

  // This should free the codec.
  EXPECT_CALL(
      codec_allocator_,
      MockReleaseMediaCodec(codec_allocator_.most_recent_codec(),
                            codec_allocator_.most_recent_overlay(), nullptr));
  codec_allocator_.codec_destruction_observer()->ExpectDestruction();
  overlay_callbacks_.SurfaceDestroyed.Run();
  base::RunLoop().RunUntilIdle();

  // Verify that the codec has been released, since |vda_| will be destroyed
  // soon.  The expectations must be met before that.
  testing::Mock::VerifyAndClearExpectations(&codec_allocator_);
  codec_allocator_.codec_destruction_observer()->DestructionIsOptional();
}

TEST_F(AndroidVideoDecodeAcceleratorTest,
       MultipleSurfaceTextureCallbacksAreIgnored) {
  // Ask AVDA to switch to ST when it's already using ST, nothing should happen.
  SKIP_IF_MEDIACODEC_IS_NOT_AVAILABLE();

  InitializeAVDAWithSurfaceTexture();

  // This should do nothing.
  EXPECT_CALL(*codec_allocator_.most_recent_codec(), SetSurface(_)).Times(0);
  chooser_->ProvideSurfaceTexture();

  base::RunLoop().RunUntilIdle();
}

}  // namespace media
