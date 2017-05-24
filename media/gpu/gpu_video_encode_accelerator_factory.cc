// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/gpu_video_encode_accelerator_factory.h"

#include "base/memory/ptr_util.h"
#include "media/gpu/gpu_video_accelerator_util.h"

#if defined(OS_CHROMEOS)
#if defined(USE_V4L2_CODEC)
#include "media/gpu/v4l2_video_encode_accelerator.h"
#endif
#if defined(ARCH_CPU_X86_FAMILY)
#include "media/gpu/vaapi_video_encode_accelerator.h"
#endif
#elif defined(OS_ANDROID) && BUILDFLAG(ENABLE_WEBRTC)
#include "media/gpu/android_video_encode_accelerator.h"
#elif defined(OS_MACOSX)
#include "media/gpu/vt_video_encode_accelerator_mac.h"
#elif defined(OS_WIN)
#include "base/feature_list.h"
#include "media/base/media_switches.h"
#include "media/gpu/media_foundation_video_encode_accelerator_win.h"
#endif

namespace media {

namespace {
#if defined(OS_CHROMEOS) && defined(USE_V4L2_CODEC)
std::unique_ptr<VideoEncodeAccelerator> CreateV4L2VEA() {
  auto device = V4L2Device::Create();
  if (device)
    return base::WrapUnique<VideoEncodeAccelerator>(
        new V4L2VideoEncodeAccelerator(device));
  return nullptr;
}
#endif

#if defined(OS_CHROMEOS) && defined(ARCH_CPU_X86_FAMILY)
std::unique_ptr<VideoEncodeAccelerator> CreateVaapiVEA() {
  return base::WrapUnique<VideoEncodeAccelerator>(
      new VaapiVideoEncodeAccelerator());
}
#endif

#if defined(OS_ANDROID) && BUILDFLAG(ENABLE_WEBRTC)
std::unique_ptr<VideoEncodeAccelerator> CreateAndroidVEA() {
  return base::WrapUnique<VideoEncodeAccelerator>(
      new AndroidVideoEncodeAccelerator());
}
#endif

#if defined(OS_MACOSX)
std::unique_ptr<VideoEncodeAccelerator> CreateVTVEA() {
  return base::WrapUnique<VideoEncodeAccelerator>(
      new VTVideoEncodeAccelerator());
}
#endif

#if defined(OS_WIN)
std::unique_ptr<VideoEncodeAccelerator> CreateMediaFoundationVEA() {
  return base::WrapUnique<VideoEncodeAccelerator>(
      new MediaFoundationVideoEncodeAccelerator());
}
#endif

using VEAFactoryFunction = std::unique_ptr<VideoEncodeAccelerator> (*)();

std::vector<VEAFactoryFunction> GetVEAFactoryFunctions(
    const gpu::GpuPreferences& gpu_preferences) {
  // Array of Create..VEA() function pointers, potentially usable on current
  // platform. This list is ordered by priority, from most to least preferred,
  // if applicable.
  std::vector<VEAFactoryFunction> vea_factory_functions;
#if defined(OS_CHROMEOS) && defined(USE_V4L2_CODEC)
  vea_factory_functions.push_back(&CreateV4L2VEA);
#endif
#if defined(OS_CHROMEOS) && defined(ARCH_CPU_X86_FAMILY)
  if (!gpu_preferences.disable_vaapi_accelerated_video_encode)
    vea_factory_functions.push_back(&CreateVaapiVEA);
#endif
#if defined(OS_ANDROID) && BUILDFLAG(ENABLE_WEBRTC)
  if (!gpu_preferences.disable_web_rtc_hw_encoding)
    vea_factory_functions.push_back(&CreateAndroidVEA);
#endif
#if defined(OS_MACOSX)
  vea_factory_functions.push_back(&CreateVTVEA);
#endif
#if defined(OS_WIN)
  if (base::FeatureList::IsEnabled(kMediaFoundationH264Encoding))
    vea_factory_functions.push_back(&CreateMediaFoundationVEA);
#endif
  return vea_factory_functions;
}

}  // anonymous namespace

// static
MEDIA_GPU_EXPORT std::unique_ptr<VideoEncodeAccelerator>
GpuVideoEncodeAcceleratorFactory::CreateVEA(
    VideoPixelFormat input_format,
    const gfx::Size& input_visible_size,
    VideoCodecProfile output_profile,
    uint32_t initial_bitrate,
    VideoEncodeAccelerator::Client* client,
    const gpu::GpuPreferences& gpu_preferences) {
  for (const auto& create_vea : GetVEAFactoryFunctions(gpu_preferences)) {
    auto vea = create_vea();
    if (!vea)
      continue;
    if (!vea->Initialize(input_format, input_visible_size, output_profile,
                         initial_bitrate, client)) {
      DLOG(ERROR) << "VEA initialize failed";
      continue;
    }
    return vea;
  }
  return nullptr;
}

// static
MEDIA_GPU_EXPORT VideoEncodeAccelerator::SupportedProfiles
GpuVideoEncodeAcceleratorFactory::GetSupportedProfiles(
    const gpu::GpuPreferences& gpu_preferences) {
  VideoEncodeAccelerator::SupportedProfiles profiles;
  for (const auto& create_vea : GetVEAFactoryFunctions(gpu_preferences)) {
    auto vea = create_vea();
    if (vea)
      GpuVideoAcceleratorUtil::InsertUniqueEncodeProfiles(
          vea->GetSupportedProfiles(), &profiles);
  }
  return profiles;
}

}  // namespace media
