// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/android/fake_codec_allocator.h"

#include <memory>

#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "media/gpu/avda_codec_allocator.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

FakeCodecAllocator::FakeCodecAllocator() = default;

FakeCodecAllocator::~FakeCodecAllocator() = default;

bool FakeCodecAllocator::StartThread(AVDACodecAllocatorClient* client) {
  return true;
}

void FakeCodecAllocator::StopThread(AVDACodecAllocatorClient* client) {}

std::unique_ptr<MediaCodecBridge> FakeCodecAllocator::CreateMediaCodecSync(
    scoped_refptr<CodecConfig> codec_config) {
  MockCreateMediaCodecSync(codec_config->surface_bundle->overlay.get(),
                           codec_config->surface_bundle->surface_texture.get());

  CopyCodecAllocParams(codec_config);

  std::unique_ptr<MockMediaCodecBridge> codec;
  if (allow_sync_creation)
    codec = base::MakeUnique<MockMediaCodecBridge>();

  if (codec) {
    most_recent_codec_ = codec.get();
    most_recent_codec_destruction_observer_ =
        codec->CreateDestructionObserver();
    most_recent_codec_destruction_observer_->DoNotAllowDestruction();
  } else {
    most_recent_codec_ = nullptr;
    most_recent_codec_destruction_observer_ = nullptr;
  }

  return std::move(codec);
}

void FakeCodecAllocator::CreateMediaCodecAsync(
    base::WeakPtr<AVDACodecAllocatorClient> client,
    scoped_refptr<CodecConfig> config) {
  // Clear |most_recent_codec_| until somebody calls Provide*CodecAsync().
  most_recent_codec_ = nullptr;
  most_recent_codec_destruction_observer_ = nullptr;
  CopyCodecAllocParams(config);
  client_ = client;

  MockCreateMediaCodecAsync(most_recent_overlay(),
                            most_recent_surface_texture());
}

void FakeCodecAllocator::ReleaseMediaCodec(
    std::unique_ptr<MediaCodecBridge> media_codec,
    scoped_refptr<AVDASurfaceBundle> surface_bundle) {
  MockReleaseMediaCodec(media_codec.get(), surface_bundle->overlay.get(),
                        surface_bundle->surface_texture.get());
}

void FakeCodecAllocator::ProvideMockCodecAsync() {
  // There must be a pending codec creation.
  ASSERT_TRUE(client_);
  if (!client_)
    return;

  std::unique_ptr<MockMediaCodecBridge> codec =
      base::MakeUnique<MockMediaCodecBridge>();
  most_recent_codec_ = codec.get();
  most_recent_codec_destruction_observer_ = codec->CreateDestructionObserver();
  most_recent_codec_destruction_observer_->DoNotAllowDestruction();
  client_->OnCodecConfigured(std::move(codec));
}

void FakeCodecAllocator::ProvideNullCodecAsync() {
  // There must be a pending codec creation.
  ASSERT_TRUE(client_);
  if (!client_)
    return;

  most_recent_codec_ = nullptr;
  client_->OnCodecConfigured(nullptr);
}

void FakeCodecAllocator::CopyCodecAllocParams(
    scoped_refptr<CodecConfig> config) {
  config_ = config;
  most_recent_bundle_ = config->surface_bundle;
  most_recent_overlay_ = config->surface_bundle->overlay.get();

  most_recent_surface_texture_ = config->surface_bundle->surface_texture.get();
}

}  // namespace media
