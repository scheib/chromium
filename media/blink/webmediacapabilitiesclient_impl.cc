// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/blink/webmediacapabilitiesclient_impl.h"

#include "media/base/audio_codecs.h"
#include "media/base/decode_capabilities.h"
#include "media/base/mime_util.h"
#include "media/base/video_codecs.h"
#include "media/base/video_color_space.h"
#include "third_party/WebKit/public/platform/modules/media_capabilities/WebAudioConfiguration.h"
#include "third_party/WebKit/public/platform/modules/media_capabilities/WebMediaCapabilitiesInfo.h"
#include "third_party/WebKit/public/platform/modules/media_capabilities/WebMediaConfiguration.h"
#include "third_party/WebKit/public/platform/modules/media_capabilities/WebVideoConfiguration.h"

namespace media {

WebMediaCapabilitiesClientImpl::WebMediaCapabilitiesClientImpl() = default;

WebMediaCapabilitiesClientImpl::~WebMediaCapabilitiesClientImpl() = default;

void WebMediaCapabilitiesClientImpl::DecodingInfo(
    const blink::WebMediaConfiguration& configuration,
    std::unique_ptr<blink::WebMediaCapabilitiesQueryCallbacks> callbacks) {
  std::unique_ptr<blink::WebMediaCapabilitiesInfo> info(
      new blink::WebMediaCapabilitiesInfo());

  bool audio_supported = true;
  bool video_supported = true;

  if (configuration.audio_configuration) {
    const blink::WebAudioConfiguration& audio_config =
        configuration.audio_configuration.value();
    AudioCodec audio_codec;
    bool is_audio_codec_ambiguous;

    if (!ParseAudioCodecString(audio_config.mime_type.Ascii(),
                               audio_config.codec.Ascii(),
                               &is_audio_codec_ambiguous, &audio_codec)) {
      // TODO(chcunningham): Replace this and other DVLOGs here with MEDIA_LOG.
      // MediaCapabilities may need its own tab in chrome://media-internals.
      DVLOG(2) << __func__ << " Failed to parse audio codec string:"
               << audio_config.codec.Ascii();
      audio_supported = false;
    } else if (is_audio_codec_ambiguous) {
      DVLOG(2) << __func__ << " Invalid (ambiguous) audio codec string:"
               << audio_config.codec.Ascii();
      audio_supported = false;
    } else {
      AudioConfig audio_config = {audio_codec};
      audio_supported = IsSupportedAudioConfig(audio_config);
    }
  }

  if (configuration.video_configuration) {
    const blink::WebVideoConfiguration& video_config =
        configuration.video_configuration.value();
    VideoCodec video_codec;
    VideoCodecProfile video_profile;
    uint8_t video_level;
    VideoColorSpace video_color_space;
    bool is_video_codec_ambiguous;

    if (!ParseVideoCodecString(
            video_config.mime_type.Ascii(), video_config.codec.Ascii(),
            &is_video_codec_ambiguous, &video_codec, &video_profile,
            &video_level, &video_color_space)) {
      DVLOG(2) << __func__ << " Failed to parse video codec string:"
               << video_config.codec.Ascii();
      video_supported = false;
    } else if (is_video_codec_ambiguous) {
      DVLOG(2) << __func__ << " Invalid (ambiguous) video codec string:"
               << video_config.codec.Ascii();
      video_supported = false;
    } else {
      VideoConfig video_config = {video_codec, video_profile, video_level,
                                  video_color_space};
      video_supported = IsSupportedVideoConfig(video_config);
    }
  }

  info->supported = audio_supported && video_supported;

  // TODO(chcunningham, mlamouri): real implementation for these.
  info->smooth = info->power_efficient = info->supported;

  callbacks->OnSuccess(std::move(info));
}

}  // namespace media
