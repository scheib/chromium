// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/android/media_codec_video_decoder.h"

#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "media/base/android/media_codec_bridge_impl.h"
#include "media/base/android/media_codec_util.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/video_codecs.h"
#include "media/base/video_decoder_config.h"
#include "media/gpu/android_video_surface_chooser.h"
#include "media/gpu/avda_codec_allocator.h"
#include "media/gpu/content_video_view_overlay.h"

namespace media {
namespace {

// Don't use MediaCodec's internal software decoders when we have more secure
// and up to date versions in the renderer process.
bool IsMediaCodecSoftwareDecodingForbidden(const VideoDecoderConfig& config) {
  return !config.is_encrypted() &&
         (config.codec() == kCodecVP8 || config.codec() == kCodecVP9);
}

bool ConfigSupported(const VideoDecoderConfig& config) {
  // Don't support larger than 4k because it won't perform well on many devices.
  const auto size = config.coded_size();
  if (size.width() > 3840 || size.height() > 2160)
    return false;

  // Only use MediaCodec for VP8 or VP9 if it's likely backed by hardware or if
  // the stream is encrypted.
  const auto codec = config.codec();
  if (IsMediaCodecSoftwareDecodingForbidden(config) &&
      MediaCodecUtil::IsKnownUnaccelerated(codec,
                                           MediaCodecDirection::DECODER)) {
    DVLOG(2) << "Config not supported: " << GetCodecName(codec)
             << " is not hardware accelerated";
    return false;
  }

  switch (codec) {
    case kCodecVP8:
    case kCodecVP9: {
      if ((codec == kCodecVP8 && !MediaCodecUtil::IsVp8DecoderAvailable()) ||
          (codec == kCodecVP9 && !MediaCodecUtil::IsVp9DecoderAvailable())) {
        return false;
      }

      // There's no fallback for encrypted content so we support all sizes.
      if (config.is_encrypted())
        return true;

      // Below 360p there's little to no power benefit to using MediaCodec over
      // libvpx so we prefer to fall back to that.
      if (size.width() < 480 || size.height() < 360)
        return false;

      return true;
    }
    case kCodecH264:
      return true;
#if BUILDFLAG(ENABLE_HEVC_DEMUXING)
    case kCodecHEVC:
      return true;
#endif
    default:
      return false;
  }
}

std::unique_ptr<AndroidOverlay> CreateContentVideoViewOverlay(
    int32_t surface_id,
    AndroidOverlayConfig config) {
  return base::MakeUnique<ContentVideoViewOverlay>(surface_id,
                                                   std::move(config));
}

}  // namespace

CodecAllocatorAdapter::CodecAllocatorAdapter() = default;

CodecAllocatorAdapter::~CodecAllocatorAdapter() = default;

void CodecAllocatorAdapter::OnCodecConfigured(
    std::unique_ptr<MediaCodecBridge> media_codec) {
  codec_created_cb.Run(std::move(media_codec));
}

MediaCodecVideoDecoder::MediaCodecVideoDecoder(
    scoped_refptr<base::SingleThreadTaskRunner> gpu_task_runner,
    base::Callback<gpu::GpuCommandBufferStub*()> get_command_buffer_stub_cb,
    AVDACodecAllocator* codec_allocator,
    std::unique_ptr<AndroidVideoSurfaceChooser> surface_chooser)
    : state_(State::kBeforeSurfaceInit),
      lazy_init_pending_(true),
      gpu_task_runner_(gpu_task_runner),
      get_command_buffer_stub_cb_(get_command_buffer_stub_cb),
      codec_allocator_(codec_allocator),
      surface_chooser_(std::move(surface_chooser)),
      weak_factory_(this) {
  DVLOG(2) << __func__;
}

MediaCodecVideoDecoder::~MediaCodecVideoDecoder() {
  codec_allocator_->StopThread(&codec_allocator_adapter_);
}

void MediaCodecVideoDecoder::Initialize(const VideoDecoderConfig& config,
                                        bool low_delay,
                                        CdmContext* cdm_context,
                                        const InitCB& init_cb,
                                        const OutputCB& output_cb) {
  DVLOG(2) << __func__;
  InitCB bound_init_cb = BindToCurrentLoop(init_cb);

  if (!ConfigSupported(config)) {
    bound_init_cb.Run(false);
    return;
  }

  if (!codec_allocator_->StartThread(&codec_allocator_adapter_)) {
    LOG(ERROR) << "Unable to start thread";
    bound_init_cb.Run(false);
    return;
  }

  decoder_config_ = config;
  codec_config_ = new CodecConfig();
  codec_config_->codec = config.codec();
  codec_config_->needs_protected_surface = config.is_encrypted();
  codec_config_->initial_expected_coded_size = config.coded_size();
  // TODO(watk): Parse config.extra_data().

  // We defer initialization of the Surface and MediaCodec until we
  // receive a Decode() call to avoid consuming those resources  in cases where
  // we'll be destructed before getting a Decode(). Failure to initialize those
  // resources will be reported as a decode error on the first decode.
  // TODO(watk): Initialize the CDM before calling init_cb.
  init_cb.Run(true);
}

void MediaCodecVideoDecoder::StartLazyInit() {
  DVLOG(2) << __func__;
  lazy_init_pending_ = false;
  // TODO(watk): Initialize surface_texture_ properly.
  surface_texture_ = SurfaceTextureGLOwner::Create();
  InitializeSurfaceChooser();
}

void MediaCodecVideoDecoder::InitializeSurfaceChooser() {
  DVLOG(2) << __func__;
  DCHECK_EQ(state_, State::kBeforeSurfaceInit);

  // If we have a surface or overlay id, create a factory cb.
  AndroidOverlayFactoryCB factory;
  // TODO(watk): Populate the surface id.
  int surface_id = SurfaceManager::kNoSurfaceID;
  if (surface_id != SurfaceManager::kNoSurfaceID)
    factory = base::Bind(&CreateContentVideoViewOverlay, surface_id);
  else if (overlay_routing_token_ && overlay_factory_cb_)
    factory = base::Bind(overlay_factory_cb_, *overlay_routing_token_);

  // Initialize |surface_chooser_| and wait for its decision.  Note: the
  // callback may be reentrant.
  // TODO(watk): Make it not reentrant.
  surface_chooser_->Initialize(
      base::Bind(&MediaCodecVideoDecoder::OnSurfaceChosen,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&MediaCodecVideoDecoder::OnSurfaceChosen,
                 weak_factory_.GetWeakPtr(), nullptr),
      std::move(factory));
}

void MediaCodecVideoDecoder::OnSurfaceChosen(
    std::unique_ptr<AndroidOverlay> overlay) {
  DVLOG(2) << __func__;
  if (overlay) {
    overlay->AddSurfaceDestroyedCallback(
        base::Bind(&MediaCodecVideoDecoder::OnSurfaceDestroyed,
                   weak_factory_.GetWeakPtr()));
  }

  // If we're waiting for our first surface during initialization, then proceed
  // immediately. Otherwise, DequeueOutput() will handle the transition.
  if (state_ == State::kBeforeSurfaceInit) {
    auto* bundle = overlay ? new AVDASurfaceBundle(std::move(overlay))
                           : new AVDASurfaceBundle(surface_texture_);
    TransitionToSurface(bundle);
    return;
  }

  // If setOutputSurface() is not supported we can't do the transition.
  // TODO(watk): Use PlatformConfig for testing.
  if (!MediaCodecUtil::IsSetOutputSurfaceSupported())
    return;

  // If we're told to switch to a SurfaceTexture but we're already using a
  // SurfaceTexture, we just cancel any pending transition.
  // (It's not possible for this to choose the overlay we're already using.)
  if (!overlay && codec_config_->surface_bundle &&
      !codec_config_->surface_bundle->overlay) {
    incoming_surface_.reset();
    return;
  }

  scoped_refptr<AVDASurfaceBundle> bundle =
      overlay ? new AVDASurfaceBundle(std::move(overlay))
              : new AVDASurfaceBundle(surface_texture_);
  incoming_surface_ = std::move(bundle);
}

void MediaCodecVideoDecoder::OnSurfaceDestroyed(AndroidOverlay* overlay) {
  DVLOG(2) << __func__;

  // If there is a pending transition to the overlay, cancel it.
  if (incoming_surface_ && (*incoming_surface_)->overlay.get() == overlay) {
    incoming_surface_.reset();
    return;
  }

  // If we've already stopped using the overlay, ignore it.
  if (!codec_config_->surface_bundle ||
      codec_config_->surface_bundle->overlay.get() != overlay) {
    return;
  }

  // If codec allocation is in progress, we go into state kSurfaceDestroyed.
  // TODO(watk): When setOutputSurface() is not available, this is fine
  // because |*this| will be destructed soon. However, if setOutputSurface() is
  // available |*this| won't be destructed soon, and this will be a hung
  // playback. We should handle this gracefully by allocating a new codec.
  if (state_ == State::kWaitingForCodec ||
      !MediaCodecUtil::IsSetOutputSurfaceSupported()) {
    state_ = State::kSurfaceDestroyed;
    ReleaseCodecAndBundle();
    // TODO(watk): If we're draining, signal completion now because the drain
    // can no longer proceed.
    return;
  }

  // If there isn't a pending overlay then transition to a SurfaceTexture.
  scoped_refptr<AVDASurfaceBundle> new_surface =
      incoming_surface_ ? *incoming_surface_
                        : new AVDASurfaceBundle(surface_texture_);
  incoming_surface_.reset();
  TransitionToSurface(std::move(new_surface));
  if (state_ == State::kError)
    ReleaseCodecAndBundle();
}

void MediaCodecVideoDecoder::TransitionToSurface(
    scoped_refptr<AVDASurfaceBundle> surface_bundle) {
  DVLOG(2) << __func__;
  // If we have a codec, then call SetSurface().
  if (media_codec_) {
    if (media_codec_->SetSurface(surface_bundle->GetJavaSurface().obj())) {
      codec_config_->surface_bundle = std::move(surface_bundle);
    } else {
      HandleError();
    }
    return;
  }

  // Create a codec with the surface bundle.
  codec_config_->surface_bundle = std::move(surface_bundle);
  StartCodecCreation();
}

void MediaCodecVideoDecoder::StartCodecCreation() {
  DCHECK(!media_codec_);
  state_ = State::kWaitingForCodec;
  codec_allocator_->CreateMediaCodecAsync(codec_allocator_adapter_.AsWeakPtr(),
                                          codec_config_);
}

void MediaCodecVideoDecoder::Decode(const scoped_refptr<DecoderBuffer>& buffer,
                                    const DecodeCB& decode_cb) {
  DVLOG(2) << __func__;
  if (lazy_init_pending_)
    StartLazyInit();
}

void MediaCodecVideoDecoder::Reset(const base::Closure& closure) {
  DVLOG(2) << __func__;
  NOTIMPLEMENTED();
}

void MediaCodecVideoDecoder::HandleError() {
  DVLOG(2) << __func__;
  state_ = State::kError;
}

void MediaCodecVideoDecoder::ReleaseCodec() {
  if (!media_codec_)
    return;
  codec_allocator_->ReleaseMediaCodec(std::move(media_codec_),
                                      codec_config_->surface_bundle);
}

void MediaCodecVideoDecoder::ReleaseCodecAndBundle() {
  ReleaseCodec();
  codec_config_->surface_bundle = nullptr;
}

std::string MediaCodecVideoDecoder::GetDisplayName() const {
  return "MediaCodecVideoDecoder";
}

bool MediaCodecVideoDecoder::NeedsBitstreamConversion() const {
  return true;
}

bool MediaCodecVideoDecoder::CanReadWithoutStalling() const {
  // MediaCodec gives us no indication that it will stop producing outputs
  // until we provide more inputs or release output buffers back to it, so
  // we have to always return false.
  // TODO(watk): This puts all MCVD playbacks into low delay mode (i.e., the
  // renderer won't try to preroll). Ideally we'd be smarter about
  // this and attempt preroll but be able to give up if we can't produce
  // enough frames.
  return false;
}

int MediaCodecVideoDecoder::GetMaxDecodeRequests() const {
  // We indicate that we're done decoding a frame as soon as we submit it
  // to MediaCodec so the number of parallel decode requests just sets the upper
  // limit of the size of our pending decode queue.
  return 2;
}

}  // namespace media
