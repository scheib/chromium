// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_ANDROID_MEDIA_CODEC_VIDEO_DECODER_H_
#define MEDIA_GPU_ANDROID_MEDIA_CODEC_VIDEO_DECODER_H_

#include "base/single_thread_task_runner.h"
#include "base/threading/thread_checker.h"
#include "gpu/ipc/service/gpu_command_buffer_stub.h"
#include "media/base/android_overlay_mojo_factory.h"
#include "media/base/video_decoder.h"
#include "media/gpu/android_video_surface_chooser.h"
#include "media/gpu/avda_codec_allocator.h"
#include "media/gpu/media_gpu_export.h"

namespace media {

// TODO(watk): Simplify the interface to AVDACodecAllocator.
struct CodecAllocatorAdapter
    : public AVDACodecAllocatorClient,
      public base::SupportsWeakPtr<CodecAllocatorAdapter> {
  using CodecCreatedCb =
      base::Callback<void(std::unique_ptr<MediaCodecBridge>)>;

  CodecAllocatorAdapter();
  ~CodecAllocatorAdapter();
  void OnCodecConfigured(
      std::unique_ptr<MediaCodecBridge> media_codec) override;

  CodecCreatedCb codec_created_cb;
};

// An Android VideoDecoder that delegates to MediaCodec.
class MEDIA_GPU_EXPORT MediaCodecVideoDecoder : public VideoDecoder {
 public:
  MediaCodecVideoDecoder(
      scoped_refptr<base::SingleThreadTaskRunner> gpu_task_runner,
      base::Callback<gpu::GpuCommandBufferStub*()> get_command_buffer_stub_cb,
      AVDACodecAllocator* codec_allocator,
      std::unique_ptr<AndroidVideoSurfaceChooser> surface_chooser);
  ~MediaCodecVideoDecoder() override;

  // VideoDecoder implementation:
  std::string GetDisplayName() const override;
  void Initialize(const VideoDecoderConfig& config,
                  bool low_delay,
                  CdmContext* cdm_context,
                  const InitCB& init_cb,
                  const OutputCB& output_cb) override;
  void Decode(const scoped_refptr<DecoderBuffer>& buffer,
              const DecodeCB& decode_cb) override;
  void Reset(const base::Closure& closure) override;
  bool NeedsBitstreamConversion() const override;
  bool CanReadWithoutStalling() const override;
  int GetMaxDecodeRequests() const override;

 private:
  enum class State {
    kOk,
    kError,
    // We haven't initialized |surface_chooser_| yet, so we don't have a surface
    // or a codec.
    kBeforeSurfaceInit,
    // Set when we are waiting for a codec to be created.
    kWaitingForCodec,
    // Set when we have a codec, but it doesn't yet have a key.
    kWaitingForKey,
    // The output surface was destroyed. This is a terminal state like kError,
    // but it isn't reported as a decode error.
    kSurfaceDestroyed,
  };

  enum class DrainType {
    kFlush,
    kReset,
    kDestroy,
  };

  // Finishes initialization.
  void StartLazyInit();

  // Initializes |surface_chooser_|.
  void InitializeSurfaceChooser();
  void OnSurfaceChosen(std::unique_ptr<AndroidOverlay> overlay);
  void OnSurfaceDestroyed(AndroidOverlay* overlay);
  void TransitionToSurface(scoped_refptr<AVDASurfaceBundle> surface_bundle);
  void StartCodecCreation();

  // Sets |state_| and runs pending callbacks.
  void HandleError();

  // Releases |media_codec_| if it's not null.
  void ReleaseCodec();

  // Calls ReleaseCodec() and drops the ref to its surface bundle.
  void ReleaseCodecAndBundle();

  State state_;
  bool lazy_init_pending_;
  VideoDecoderConfig decoder_config_;

  // |codec_config_| must not be modified while |state_| is kWaitingForCodec.
  scoped_refptr<CodecConfig> codec_config_;
  std::unique_ptr<MediaCodecBridge> media_codec_;

  // The ongoing drain operation, if any.
  base::Optional<DrainType> drain_kind_;

  scoped_refptr<base::SingleThreadTaskRunner> gpu_task_runner_;
  base::Callback<gpu::GpuCommandBufferStub*()> get_command_buffer_stub_cb_;
  AVDACodecAllocator* codec_allocator_;
  CodecAllocatorAdapter codec_allocator_adapter_;

  // A SurfaceTexture that is kept for the lifetime of MCVD so that if we have
  // to synchronously switch surfaces we always have one available.
  scoped_refptr<SurfaceTextureGLOwner> surface_texture_;
  std::unique_ptr<AndroidVideoSurfaceChooser> surface_chooser_;

  // The surface bundle that we're transitioning to, if any.
  base::Optional<scoped_refptr<AVDASurfaceBundle>> incoming_surface_;

  // An optional factory callback for creating mojo AndroidOverlays.
  AndroidOverlayMojoFactoryCB overlay_factory_cb_;

  // The routing token for a mojo AndroidOverlay.
  base::Optional<base::UnguessableToken> overlay_routing_token_;

  base::WeakPtrFactory<MediaCodecVideoDecoder> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(MediaCodecVideoDecoder);
};

}  // namespace media

#endif  // MEDIA_GPU_ANDROID_MEDIA_CODEC_VIDEO_DECODER_H_
