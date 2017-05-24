// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/media.h"

#include "base/command_line.h"
#include "base/macros.h"
#include "base/metrics/field_trial.h"
#include "base/trace_event/trace_event.h"
#include "media/base/media_switches.h"
#include "third_party/libyuv/include/libyuv.h"

#if defined(OS_ANDROID)
#include "base/android/build_info.h"
#include "media/base/android/media_codec_util.h"
#endif

#if !defined(MEDIA_DISABLE_FFMPEG)
#include "media/ffmpeg/ffmpeg_common.h"
#endif

namespace media {

// Media must only be initialized once; use a thread-safe static to do this.
class MediaInitializer {
 public:
  MediaInitializer() {
    TRACE_EVENT_WARMUP_CATEGORY("audio");
    TRACE_EVENT_WARMUP_CATEGORY("media");

    libyuv::InitCpuFlags();

#if !defined(MEDIA_DISABLE_FFMPEG)
    // Initialize CPU flags outside of the sandbox as this may query /proc for
    // details on the current CPU for NEON, VFP, etc optimizations.
    av_get_cpu_flags();

    // Disable logging as it interferes with layout tests.
    av_log_set_level(AV_LOG_QUIET);

#if defined(ALLOCATOR_SHIM)
    // Remove allocation limit from ffmpeg, so calls go down to shim layer.
    av_max_alloc(0);
#endif  // defined(ALLOCATOR_SHIM)

#endif  // !defined(MEDIA_DISABLE_FFMPEG)
  }

#if defined(OS_ANDROID)
  void enable_platform_decoder_support() {
    has_platform_decoder_support_ = true;
  }

  bool has_platform_decoder_support() const {
    return has_platform_decoder_support_;
  }
#endif  // defined(OS_ANDROID)

  void enable_new_vp9_codec_string_support(bool enable) {
    has_new_vp9_codec_string_support_ = enable;
  }

  bool has_new_vp9_codec_string_support() {
    return has_new_vp9_codec_string_support_;
  }

 private:
  ~MediaInitializer() = delete;

#if defined(OS_ANDROID)
  bool has_platform_decoder_support_ = false;
#endif  // defined(OS_ANDROID)

  bool has_new_vp9_codec_string_support_ = false;

  DISALLOW_COPY_AND_ASSIGN(MediaInitializer);
};

static MediaInitializer* GetMediaInstance() {
  static MediaInitializer* instance = new MediaInitializer();
  return instance;
}

void InitializeMediaLibrary() {
  GetMediaInstance();
}

#if defined(OS_ANDROID)
void EnablePlatformDecoderSupport() {
  GetMediaInstance()->enable_platform_decoder_support();
}

bool HasPlatformDecoderSupport() {
  return GetMediaInstance()->has_platform_decoder_support();
}

bool PlatformHasOpusSupport() {
  return base::android::BuildInfo::GetInstance()->sdk_int() >= 21;
}
#endif  // defined(OS_ANDROID)

void EnableNewVp9CodecStringSupport() {
  GetMediaInstance()->enable_new_vp9_codec_string_support(true);
}

void DisableNewVp9CodecStringSupport_ForTesting() {
  GetMediaInstance()->enable_new_vp9_codec_string_support(false);
}

bool HasNewVp9CodecStringSupport() {
  return GetMediaInstance()->has_new_vp9_codec_string_support();
}

}  // namespace media
