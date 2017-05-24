// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "media/base/android/mock_media_codec_bridge.h"
#include "media/gpu/avda_codec_allocator.h"
#include "media/gpu/avda_surface_bundle.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gl/android/surface_texture.h"

namespace media {

// A codec allocator that provides a configurable fake implementation
// and lets you set expecations on the "Mock*" methods.
class FakeCodecAllocator : public testing::NiceMock<AVDACodecAllocator> {
 public:
  FakeCodecAllocator();
  ~FakeCodecAllocator() override;

  bool StartThread(AVDACodecAllocatorClient* client) override;
  void StopThread(AVDACodecAllocatorClient* client) override;

  // These are called with some parameters of the codec config by our
  // implementation of their respective functions.  This allows tests to set
  // expectations on them.
  MOCK_METHOD2(MockCreateMediaCodecSync,
               void(AndroidOverlay*, gl::SurfaceTexture*));
  MOCK_METHOD2(MockCreateMediaCodecAsync,
               void(AndroidOverlay*, gl::SurfaceTexture*));

  // Note that this doesn't exactly match the signature, since unique_ptr
  // doesn't work.  plus, we expand |surface_bundle| a bit to make it more
  // convenient to set expectations.
  MOCK_METHOD3(MockReleaseMediaCodec,
               void(MediaCodecBridge*, AndroidOverlay*, gl::SurfaceTexture*));

  std::unique_ptr<MediaCodecBridge> CreateMediaCodecSync(
      scoped_refptr<CodecConfig> codec_config) override;
  void CreateMediaCodecAsync(base::WeakPtr<AVDACodecAllocatorClient> client,
                             scoped_refptr<CodecConfig> config) override;
  void ReleaseMediaCodec(
      std::unique_ptr<MediaCodecBridge> media_codec,
      scoped_refptr<AVDASurfaceBundle> surface_bundle) override;

  // Satisfies the pending codec creation with a mock codec.
  void ProvideMockCodecAsync();

  // Satisfies the pending codec creation with a null codec.
  void ProvideNullCodecAsync();

  // Returns the most recent bundle that we've received for codec allocation.
  scoped_refptr<AVDASurfaceBundle> most_recent_bundle();

  // Returns the most recent codec that we provided, which might already have
  // been freed.  By default, the destruction observer will fail the test
  // if this happens, unless the expectation is explicitly changed.  If you
  // change it, then use this with caution.
  MockMediaCodecBridge* most_recent_codec() { return most_recent_codec_; }

  // Returns the destruction observer for the most recent codec.  We retain
  // ownership of it.
  DestructionObservable::DestructionObserver* codec_destruction_observer() {
    return most_recent_codec_destruction_observer_.get();
  }

  // Returns the most recent overlay / etc. that we were given during codec
  // allocation (sync or async).
  AndroidOverlay* most_recent_overlay() { return most_recent_overlay_; }
  gl::SurfaceTexture* most_recent_surface_texture() {
    return most_recent_surface_texture_;
  }

  // Most recent codec that we've created via CreateMockCodec, since we have
  // to assign ownership.  It may be freed already.
  MockMediaCodecBridge* most_recent_codec_;

  // DestructionObserver for |most_recent_codec_|.
  std::unique_ptr<DestructionObservable::DestructionObserver>
      most_recent_codec_destruction_observer_;

  // The most recent surface bundle that we've gotten during codec allocation.
  // This should be the same as |config_->surface_bundle| initially, but AVDA
  // might change it.
  scoped_refptr<AVDASurfaceBundle> most_recent_bundle_;

  // The most recent overlay provided during codec allocation.
  AndroidOverlay* most_recent_overlay_ = nullptr;

  // The most recent surface texture provided during codec allocation.
  gl::SurfaceTexture* most_recent_surface_texture_ = nullptr;

  // Whether CreateMediaCodecSync() is allowed to succeed.
  bool allow_sync_creation = true;

 private:
  // Saves a reference to |config| and copies out the fields that may
  // get modified by the client.
  void CopyCodecAllocParams(scoped_refptr<CodecConfig> config);

  base::WeakPtr<AVDACodecAllocatorClient> client_;
  scoped_refptr<CodecConfig> config_;

  DISALLOW_COPY_AND_ASSIGN(FakeCodecAllocator);
};

}  // namespace media
