// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/video_capture_device_descriptor.h"

#include "base/logging.h"

namespace media {

VideoCaptureDeviceDescriptor::VideoCaptureDeviceDescriptor()
    : facing(VideoFacingMode::MEDIA_VIDEO_FACING_NONE),
      capture_api(VideoCaptureApi::UNKNOWN),
      transport_type(VideoCaptureTransportType::OTHER_TRANSPORT) {}

VideoCaptureDeviceDescriptor::VideoCaptureDeviceDescriptor(
    const std::string& display_name,
    const std::string& device_id,
    VideoCaptureApi capture_api,
    VideoCaptureTransportType transport_type)
    : display_name(display_name),
      device_id(device_id),
      facing(VideoFacingMode::MEDIA_VIDEO_FACING_NONE),
      capture_api(capture_api),
      transport_type(transport_type) {}

VideoCaptureDeviceDescriptor::VideoCaptureDeviceDescriptor(
    const std::string& display_name,
    const std::string& device_id,
    const std::string& model_id,
    VideoCaptureApi capture_api,
    VideoCaptureTransportType transport_type,
    VideoFacingMode facing)
    : display_name(display_name),
      device_id(device_id),
      model_id(model_id),
      facing(facing),
      capture_api(capture_api),
      transport_type(transport_type) {}

VideoCaptureDeviceDescriptor::~VideoCaptureDeviceDescriptor() {}

VideoCaptureDeviceDescriptor::VideoCaptureDeviceDescriptor(
    const VideoCaptureDeviceDescriptor& other) = default;

bool VideoCaptureDeviceDescriptor::operator<(
    const VideoCaptureDeviceDescriptor& other) const {
  static constexpr int kFacingMapping[NUM_MEDIA_VIDEO_FACING_MODES] = {0, 2, 1};
  static_assert(kFacingMapping[MEDIA_VIDEO_FACING_NONE] == 0,
                "FACING_NONE has a wrong value");
  static_assert(kFacingMapping[MEDIA_VIDEO_FACING_ENVIRONMENT] == 1,
                "FACING_ENVIRONMENT has a wrong value");
  static_assert(kFacingMapping[MEDIA_VIDEO_FACING_USER] == 2,
                "FACING_USER has a wrong value");
  if (kFacingMapping[facing] > kFacingMapping[other.facing])
    return true;
  if (device_id < other.device_id)
    return true;
  return capture_api < other.capture_api;
}

const char* VideoCaptureDeviceDescriptor::GetCaptureApiTypeString() const {
  switch (capture_api) {
    case VideoCaptureApi::LINUX_V4L2_SINGLE_PLANE:
      return "V4L2 SPLANE";
    case VideoCaptureApi::WIN_MEDIA_FOUNDATION:
      return "Media Foundation";
    case VideoCaptureApi::WIN_DIRECT_SHOW:
      return "Direct Show";
    case VideoCaptureApi::MACOSX_AVFOUNDATION:
      return "AV Foundation";
    case VideoCaptureApi::MACOSX_DECKLINK:
      return "DeckLink";
    case VideoCaptureApi::MACOSX_SYPHON:
      return "Syphon";
    case VideoCaptureApi::ANDROID_API1:
      return "Camera API1";
    case VideoCaptureApi::ANDROID_API2_LEGACY:
      return "Camera API2 Legacy";
    case VideoCaptureApi::ANDROID_API2_FULL:
      return "Camera API2 Full";
    case VideoCaptureApi::ANDROID_API2_LIMITED:
      return "Camera API2 Limited";
    case VideoCaptureApi::ANDROID_TANGO:
      return "Tango API";
    default:
      NOTREACHED() << "Unknown Video Capture API type: "
                   << static_cast<int>(capture_api);
      return "Unknown API";
  }
}

std::string VideoCaptureDeviceDescriptor::GetNameAndModel() const {
  if (model_id.empty())
    return display_name;
  return display_name + " (" + model_id + ")";
}

}  // namespace media
