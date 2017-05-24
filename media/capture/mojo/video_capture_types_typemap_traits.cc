// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/mojo/video_capture_types_typemap_traits.h"

#include "media/base/ipc/media_param_traits_macros.h"
#include "ui/gfx/geometry/mojo/geometry.mojom.h"
#include "ui/gfx/geometry/mojo/geometry_struct_traits.h"

namespace mojo {

// static
media::mojom::VideoPixelStorage
EnumTraits<media::mojom::VideoPixelStorage, media::VideoPixelStorage>::ToMojom(
    media::VideoPixelStorage video_pixel_storage) {
  DCHECK_EQ(media::PIXEL_STORAGE_CPU, video_pixel_storage);
  return media::mojom::VideoPixelStorage::CPU;
}

// static
bool EnumTraits<media::mojom::VideoPixelStorage, media::VideoPixelStorage>::
    FromMojom(media::mojom::VideoPixelStorage input,
              media::VideoPixelStorage* out) {
  DCHECK_EQ(media::mojom::VideoPixelStorage::CPU, input);
  *out = media::PIXEL_STORAGE_CPU;
  return true;
}

// static
media::mojom::ResolutionChangePolicy
EnumTraits<media::mojom::ResolutionChangePolicy,
           media::ResolutionChangePolicy>::ToMojom(media::ResolutionChangePolicy
                                                       input) {
  switch (input) {
    case media::RESOLUTION_POLICY_FIXED_RESOLUTION:
      return media::mojom::ResolutionChangePolicy::FIXED_RESOLUTION;
    case media::RESOLUTION_POLICY_FIXED_ASPECT_RATIO:
      return media::mojom::ResolutionChangePolicy::FIXED_ASPECT_RATIO;
    case media::RESOLUTION_POLICY_ANY_WITHIN_LIMIT:
      return media::mojom::ResolutionChangePolicy::ANY_WITHIN_LIMIT;
  }
  NOTREACHED();
  return media::mojom::ResolutionChangePolicy::FIXED_RESOLUTION;
}

// static
bool EnumTraits<media::mojom::ResolutionChangePolicy,
                media::ResolutionChangePolicy>::
    FromMojom(media::mojom::ResolutionChangePolicy input,
              media::ResolutionChangePolicy* output) {
  switch (input) {
    case media::mojom::ResolutionChangePolicy::FIXED_RESOLUTION:
      *output = media::RESOLUTION_POLICY_FIXED_RESOLUTION;
      return true;
    case media::mojom::ResolutionChangePolicy::FIXED_ASPECT_RATIO:
      *output = media::RESOLUTION_POLICY_FIXED_ASPECT_RATIO;
      return true;
    case media::mojom::ResolutionChangePolicy::ANY_WITHIN_LIMIT:
      *output = media::RESOLUTION_POLICY_ANY_WITHIN_LIMIT;
      return true;
  }
  NOTREACHED();
  return false;
}

// static
media::mojom::PowerLineFrequency EnumTraits<
    media::mojom::PowerLineFrequency,
    media::PowerLineFrequency>::ToMojom(media::PowerLineFrequency input) {
  switch (input) {
    case media::PowerLineFrequency::FREQUENCY_DEFAULT:
      return media::mojom::PowerLineFrequency::DEFAULT;
    case media::PowerLineFrequency::FREQUENCY_50HZ:
      return media::mojom::PowerLineFrequency::HZ_50;
    case media::PowerLineFrequency::FREQUENCY_60HZ:
      return media::mojom::PowerLineFrequency::HZ_60;
  }
  NOTREACHED();
  return media::mojom::PowerLineFrequency::DEFAULT;
}

// static
bool EnumTraits<media::mojom::PowerLineFrequency, media::PowerLineFrequency>::
    FromMojom(media::mojom::PowerLineFrequency input,
              media::PowerLineFrequency* output) {
  switch (input) {
    case media::mojom::PowerLineFrequency::DEFAULT:
      *output = media::PowerLineFrequency::FREQUENCY_DEFAULT;
      return true;
    case media::mojom::PowerLineFrequency::HZ_50:
      *output = media::PowerLineFrequency::FREQUENCY_50HZ;
      return true;
    case media::mojom::PowerLineFrequency::HZ_60:
      *output = media::PowerLineFrequency::FREQUENCY_60HZ;
      return true;
  }
  NOTREACHED();
  return false;
}

// static
media::mojom::VideoCaptureApi
EnumTraits<media::mojom::VideoCaptureApi, media::VideoCaptureApi>::ToMojom(
    media::VideoCaptureApi input) {
  switch (input) {
    case media::VideoCaptureApi::LINUX_V4L2_SINGLE_PLANE:
      return media::mojom::VideoCaptureApi::LINUX_V4L2_SINGLE_PLANE;
    case media::VideoCaptureApi::WIN_MEDIA_FOUNDATION:
      return media::mojom::VideoCaptureApi::WIN_MEDIA_FOUNDATION;
    case media::VideoCaptureApi::WIN_DIRECT_SHOW:
      return media::mojom::VideoCaptureApi::WIN_DIRECT_SHOW;
    case media::VideoCaptureApi::MACOSX_AVFOUNDATION:
      return media::mojom::VideoCaptureApi::MACOSX_AVFOUNDATION;
    case media::VideoCaptureApi::MACOSX_DECKLINK:
      return media::mojom::VideoCaptureApi::MACOSX_DECKLINK;
    case media::VideoCaptureApi::MACOSX_SYPHON:
      return media::mojom::VideoCaptureApi::MACOSX_SYPHON;
    case media::VideoCaptureApi::ANDROID_API1:
      return media::mojom::VideoCaptureApi::ANDROID_API1;
    case media::VideoCaptureApi::ANDROID_API2_LEGACY:
      return media::mojom::VideoCaptureApi::ANDROID_API2_LEGACY;
    case media::VideoCaptureApi::ANDROID_API2_FULL:
      return media::mojom::VideoCaptureApi::ANDROID_API2_FULL;
    case media::VideoCaptureApi::ANDROID_API2_LIMITED:
      return media::mojom::VideoCaptureApi::ANDROID_API2_LIMITED;
    case media::VideoCaptureApi::ANDROID_TANGO:
      return media::mojom::VideoCaptureApi::ANDROID_TANGO;
    case media::VideoCaptureApi::UNKNOWN:
      return media::mojom::VideoCaptureApi::UNKNOWN;
  }
  NOTREACHED();
  return media::mojom::VideoCaptureApi::UNKNOWN;
}

// static
bool EnumTraits<media::mojom::VideoCaptureApi, media::VideoCaptureApi>::
    FromMojom(media::mojom::VideoCaptureApi input,
              media::VideoCaptureApi* output) {
  switch (input) {
    case media::mojom::VideoCaptureApi::LINUX_V4L2_SINGLE_PLANE:
      *output = media::VideoCaptureApi::LINUX_V4L2_SINGLE_PLANE;
      return true;
    case media::mojom::VideoCaptureApi::WIN_MEDIA_FOUNDATION:
      *output = media::VideoCaptureApi::WIN_MEDIA_FOUNDATION;
      return true;
    case media::mojom::VideoCaptureApi::WIN_DIRECT_SHOW:
      *output = media::VideoCaptureApi::WIN_DIRECT_SHOW;
      return true;
    case media::mojom::VideoCaptureApi::MACOSX_AVFOUNDATION:
      *output = media::VideoCaptureApi::MACOSX_AVFOUNDATION;
      return true;
    case media::mojom::VideoCaptureApi::MACOSX_DECKLINK:
      *output = media::VideoCaptureApi::MACOSX_DECKLINK;
      return true;
    case media::mojom::VideoCaptureApi::MACOSX_SYPHON:
      *output = media::VideoCaptureApi::MACOSX_SYPHON;
      return true;
      case media::mojom::VideoCaptureApi::ANDROID_API1:
      *output = media::VideoCaptureApi::ANDROID_API1;
      return true;
    case media::mojom::VideoCaptureApi::ANDROID_API2_LEGACY:
      *output = media::VideoCaptureApi::ANDROID_API2_LEGACY;
      return true;
    case media::mojom::VideoCaptureApi::ANDROID_API2_FULL:
      *output = media::VideoCaptureApi::ANDROID_API2_FULL;
      return true;
    case media::mojom::VideoCaptureApi::ANDROID_API2_LIMITED:
      *output = media::VideoCaptureApi::ANDROID_API2_LIMITED;
      return true;
    case media::mojom::VideoCaptureApi::ANDROID_TANGO:
      *output = media::VideoCaptureApi::ANDROID_TANGO;
      return true;
    case media::mojom::VideoCaptureApi::UNKNOWN:
      *output = media::VideoCaptureApi::UNKNOWN;
      return true;
  }
  NOTREACHED();
  return false;
}

// static
media::mojom::VideoCaptureTransportType EnumTraits<
    media::mojom::VideoCaptureTransportType,
    media::VideoCaptureTransportType>::ToMojom(media::VideoCaptureTransportType
                                                   input) {
  switch (input) {
    case media::VideoCaptureTransportType::MACOSX_USB_OR_BUILT_IN:
      return media::mojom::VideoCaptureTransportType::MACOSX_USB_OR_BUILT_IN;
    case media::VideoCaptureTransportType::OTHER_TRANSPORT:
      return media::mojom::VideoCaptureTransportType::OTHER_TRANSPORT;
  }
  NOTREACHED();
  return media::mojom::VideoCaptureTransportType::OTHER_TRANSPORT;
}

// static
bool EnumTraits<media::mojom::VideoCaptureTransportType,
                media::VideoCaptureTransportType>::
    FromMojom(media::mojom::VideoCaptureTransportType input,
              media::VideoCaptureTransportType* output) {
  switch (input) {
    case media::mojom::VideoCaptureTransportType::MACOSX_USB_OR_BUILT_IN:
      *output = media::VideoCaptureTransportType::MACOSX_USB_OR_BUILT_IN;
      return true;
    case media::mojom::VideoCaptureTransportType::OTHER_TRANSPORT:
      *output = media::VideoCaptureTransportType::OTHER_TRANSPORT;
      return true;
  }
  NOTREACHED();
  return false;
}

// static
bool StructTraits<media::mojom::VideoCaptureFormatDataView,
                  media::VideoCaptureFormat>::
    Read(media::mojom::VideoCaptureFormatDataView data,
         media::VideoCaptureFormat* out) {
  if (!data.ReadFrameSize(&out->frame_size))
    return false;
  out->frame_rate = data.frame_rate();
  if (!data.ReadPixelFormat(&out->pixel_format))
    return false;
  if (!data.ReadPixelStorage(&out->pixel_storage))
    return false;
  return true;
}

// static
bool StructTraits<media::mojom::VideoCaptureParamsDataView,
                  media::VideoCaptureParams>::
    Read(media::mojom::VideoCaptureParamsDataView data,
         media::VideoCaptureParams* out) {
  if (!data.ReadRequestedFormat(&out->requested_format))
    return false;
  if (!data.ReadResolutionChangePolicy(&out->resolution_change_policy))
    return false;
  if (!data.ReadPowerLineFrequency(&out->power_line_frequency))
    return false;
  return true;
}

// static
bool StructTraits<media::mojom::VideoCaptureDeviceDescriptorDataView,
                  media::VideoCaptureDeviceDescriptor>::
    Read(media::mojom::VideoCaptureDeviceDescriptorDataView data,
         media::VideoCaptureDeviceDescriptor* output) {
  if (!data.ReadDisplayName(&(output->display_name)))
    return false;
  if (!data.ReadDeviceId(&(output->device_id)))
    return false;
  if (!data.ReadModelId(&(output->model_id)))
    return false;
  if (!data.ReadCaptureApi(&(output->capture_api)))
    return false;
  if (!data.ReadTransportType(&(output->transport_type)))
    return false;
  return true;
}

// static
bool StructTraits<media::mojom::VideoCaptureDeviceInfoDataView,
                  media::VideoCaptureDeviceInfo>::
    Read(media::mojom::VideoCaptureDeviceInfoDataView data,
         media::VideoCaptureDeviceInfo* output) {
  if (!data.ReadDescriptor(&(output->descriptor)))
    return false;
  if (!data.ReadSupportedFormats(&(output->supported_formats)))
    return false;
  return true;
}

}  // namespace mojo
