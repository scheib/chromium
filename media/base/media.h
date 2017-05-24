// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains code that should be used for initializing, or querying the state
// of the media library as a whole.

#ifndef MEDIA_BASE_MEDIA_H_
#define MEDIA_BASE_MEDIA_H_

#include "build/build_config.h"
#include "media/base/media_export.h"

namespace media {

// Initializes media libraries (e.g. ffmpeg) as well as CPU specific media
// features.
MEDIA_EXPORT void InitializeMediaLibrary();

#if defined(OS_ANDROID)
// Tells the media library it has support for OS level decoders. Should only be
// used for actual decoders (e.g. MediaCodec) and not full-featured players
// (e.g. MediaPlayer).
MEDIA_EXPORT void EnablePlatformDecoderSupport();
MEDIA_EXPORT bool HasPlatformDecoderSupport();

// Indicates if the platform supports Opus. Determined *ONLY* by the platform
// version, so does not guarantee that either can actually be played.
MEDIA_EXPORT bool PlatformHasOpusSupport();
#endif

// Tells the media library to support parsing of the new multipart vp9 codec
// string.
// TODO(chcunningham): Remove these (always support) when string standardized.
MEDIA_EXPORT void EnableNewVp9CodecStringSupport();
MEDIA_EXPORT bool HasNewVp9CodecStringSupport();
MEDIA_EXPORT void DisableNewVp9CodecStringSupport_ForTesting();

}  // namespace media

#endif  // MEDIA_BASE_MEDIA_H_
