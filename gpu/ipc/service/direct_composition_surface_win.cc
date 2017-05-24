// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/service/direct_composition_surface_win.h"

#include <d3d11_1.h>
#include <dcomptypes.h>

#include <deque>

#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/optional.h"
#include "base/synchronization/waitable_event.h"
#include "base/trace_event/trace_event.h"
#include "base/win/scoped_handle.h"
#include "base/win/windows_version.h"
#include "gpu/command_buffer/service/feature_info.h"
#include "gpu/ipc/service/gpu_channel_manager.h"
#include "gpu/ipc/service/gpu_channel_manager_delegate.h"
#include "gpu/ipc/service/switches.h"
#include "ui/display/display_switches.h"
#include "ui/gfx/color_space_win.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/transform.h"
#include "ui/gl/dc_renderer_layer_params.h"
#include "ui/gl/egl_util.h"
#include "ui/gl/gl_angle_util_win.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_image_dxgi.h"
#include "ui/gl/gl_image_memory.h"
#include "ui/gl/gl_surface_egl.h"
#include "ui/gl/scoped_make_current.h"

#ifndef EGL_ANGLE_flexible_surface_compatibility
#define EGL_ANGLE_flexible_surface_compatibility 1
#define EGL_FLEXIBLE_SURFACE_COMPATIBILITY_SUPPORTED_ANGLE 0x33A6
#endif /* EGL_ANGLE_flexible_surface_compatibility */

#ifndef EGL_ANGLE_d3d_texture_client_buffer
#define EGL_ANGLE_d3d_texture_client_buffer 1
#define EGL_D3D_TEXTURE_ANGLE 0x33A3
#endif /* EGL_ANGLE_d3d_texture_client_buffer */

namespace gpu {
namespace {

// Some drivers fail to correctly handle BT.709 video in overlays. This flag
// converts them to BT.601 in the video processor.
const base::Feature kFallbackBT709VideoToBT601{
    "FallbackBT709VideoToBT601", base::FEATURE_DISABLED_BY_DEFAULT};

// This class is used to make sure a specified surface isn't current, and upon
// destruction it will make the surface current again if it had been before.
class ScopedReleaseCurrent {
 public:
  explicit ScopedReleaseCurrent(gl::GLSurface* this_surface) {
    gl::GLContext* current_context = gl::GLContext::GetCurrent();
    bool was_current =
        current_context && current_context->IsCurrent(this_surface);
    if (was_current) {
      make_current_.emplace(current_context, this_surface);
      current_context->ReleaseCurrent(this_surface);
    }
  }

 private:
  base::Optional<ui::ScopedMakeCurrent> make_current_;
};

bool SizeContains(const gfx::Size& a, const gfx::Size& b) {
  return gfx::Rect(a).Contains(gfx::Rect(b));
}

// This keeps track of whether the previous 30 frames used Overlays or GPU
// composition to present.
class PresentationHistory {
 public:
  static const int kPresentsToStore = 30;

  PresentationHistory() {}

  void AddSample(DXGI_FRAME_PRESENTATION_MODE mode) {
    if (mode == DXGI_FRAME_PRESENTATION_MODE_COMPOSED)
      composed_count_++;

    presents_.push_back(mode);
    if (presents_.size() > kPresentsToStore) {
      DXGI_FRAME_PRESENTATION_MODE first_mode = presents_.front();
      if (first_mode == DXGI_FRAME_PRESENTATION_MODE_COMPOSED)
        composed_count_--;
      presents_.pop_front();
    }
  }

  bool valid() const { return presents_.size() >= kPresentsToStore; }
  int composed_count() const { return composed_count_; }

 private:
  std::deque<DXGI_FRAME_PRESENTATION_MODE> presents_;
  int composed_count_ = 0;

  DISALLOW_COPY_AND_ASSIGN(PresentationHistory);
};

gfx::Size g_overlay_monitor_size;

// This is the raw support info, which shouldn't depend on field trial state.
bool HardwareSupportsOverlays() {
  if (!gl::GLSurfaceEGL::IsDirectCompositionSupported())
    return false;

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnableDirectCompositionLayers))
    return true;
  if (command_line->HasSwitch(switches::kDisableDirectCompositionLayers))
    return false;

  // Before Windows 10 Anniversary Update (Redstone 1), overlay planes
  // wouldn't be assigned to non-UWP apps.
  if (base::win::GetVersion() < base::win::VERSION_WIN10_RS1)
    return false;

  base::win::ScopedComPtr<ID3D11Device> d3d11_device =
      gl::QueryD3D11DeviceObjectFromANGLE();
  if (!d3d11_device) {
    DLOG(ERROR) << "Failing to create overlay swapchain because couldn't "
                   "retrieve D3D11 device from ANGLE.";
    return false;
  }

  base::win::ScopedComPtr<IDXGIDevice> dxgi_device;
  d3d11_device.CopyTo(dxgi_device.GetAddressOf());
  base::win::ScopedComPtr<IDXGIAdapter> dxgi_adapter;
  dxgi_device->GetAdapter(dxgi_adapter.GetAddressOf());

  unsigned int i = 0;
  while (true) {
    base::win::ScopedComPtr<IDXGIOutput> output;
    if (FAILED(dxgi_adapter->EnumOutputs(i++, output.GetAddressOf())))
      break;
    base::win::ScopedComPtr<IDXGIOutput3> output3;
    if (FAILED(output.CopyTo(output3.GetAddressOf())))
      continue;

    UINT flags = 0;
    if (FAILED(output3->CheckOverlaySupport(DXGI_FORMAT_YUY2,
                                            d3d11_device.Get(), &flags)))
      continue;

    UMA_HISTOGRAM_SPARSE_SLOWLY("GPU.DirectComposition.OverlaySupportFlags",
                                flags);

    // Some new Intel drivers only claim to support unscaled overlays, but
    // scaled overlays still work. Even when scaled overlays aren't actually
    // supported, presentation using the overlay path should be relatively
    // efficient.
    if (flags & (DXGI_OVERLAY_SUPPORT_FLAG_SCALING |
                 DXGI_OVERLAY_SUPPORT_FLAG_DIRECT)) {
      DXGI_OUTPUT_DESC monitor_desc = {};
      if (FAILED(output3->GetDesc(&monitor_desc)))
        continue;
      g_overlay_monitor_size =
          gfx::Rect(monitor_desc.DesktopCoordinates).size();
      return true;
    }
  }
  return false;
}

// Only one DirectComposition surface can be rendered into at a time. Track
// here which IDCompositionSurface is being rendered into. If another context
// is made current, then this surface will be suspended.
IDCompositionSurface* g_current_surface;

}  // namespace

class DCLayerTree {
 public:
  DCLayerTree(DirectCompositionSurfaceWin* surface,
              const base::win::ScopedComPtr<ID3D11Device>& d3d11_device,
              const base::win::ScopedComPtr<IDCompositionDevice2>& dcomp_device)
      : surface_(surface),
        d3d11_device_(d3d11_device),
        dcomp_device_(dcomp_device) {}

  bool Initialize(HWND window);
  bool CommitAndClearPendingOverlays();
  bool ScheduleDCLayer(const ui::DCRendererLayerParams& params);
  void InitializeVideoProcessor(const gfx::Size& input_size,
                                const gfx::Size& output_size);

  const base::win::ScopedComPtr<ID3D11VideoProcessor>& video_processor() const {
    return video_processor_;
  }
  const base::win::ScopedComPtr<ID3D11VideoProcessorEnumerator>&
  video_processor_enumerator() const {
    return video_processor_enumerator_;
  }
  base::win::ScopedComPtr<IDXGISwapChain1> GetLayerSwapChainForTesting(
      size_t index) const;

  const GpuDriverBugWorkarounds& workarounds() const {
    return surface_->workarounds();
  }

 private:
  class SwapChainPresenter;

  // This struct is used to cache information about what visuals are currently
  // being presented so that properties that aren't changed aren't sent to
  // DirectComposition.
  struct VisualInfo {
    base::win::ScopedComPtr<IDCompositionVisual2> content_visual;
    base::win::ScopedComPtr<IDCompositionVisual2> clip_visual;

    std::unique_ptr<SwapChainPresenter> swap_chain_presenter;
    base::win::ScopedComPtr<IDXGISwapChain1> swap_chain;
    base::win::ScopedComPtr<IDCompositionSurface> surface;

    gfx::Rect bounds;
    float swap_chain_scale_x = 0.0f;
    float swap_chain_scale_y = 0.0f;
    bool is_clipped = false;
    gfx::Rect clip_rect;
    gfx::Transform transform;
  };

  void InitVisual(size_t i);
  void UpdateVisualForVideo(VisualInfo* visual_info,
                            const ui::DCRendererLayerParams& params);
  void UpdateVisualForBackbuffer(VisualInfo* visual_info,
                                 const ui::DCRendererLayerParams& params);
  void UpdateVisualClip(VisualInfo* visual_info,
                        const ui::DCRendererLayerParams& params);

  DirectCompositionSurfaceWin* surface_;
  std::vector<std::unique_ptr<ui::DCRendererLayerParams>> pending_overlays_;

  base::win::ScopedComPtr<ID3D11Device> d3d11_device_;
  base::win::ScopedComPtr<IDCompositionDevice2> dcomp_device_;
  base::win::ScopedComPtr<IDCompositionTarget> dcomp_target_;
  base::win::ScopedComPtr<IDCompositionVisual2> root_visual_;

  // The video processor is cached so SwapChains don't have to recreate it
  // whenever they're created.
  base::win::ScopedComPtr<ID3D11VideoDevice> video_device_;
  base::win::ScopedComPtr<ID3D11VideoContext> video_context_;
  base::win::ScopedComPtr<ID3D11VideoProcessor> video_processor_;
  base::win::ScopedComPtr<ID3D11VideoProcessorEnumerator>
      video_processor_enumerator_;
  gfx::Size video_input_size_;
  gfx::Size video_output_size_;

  std::vector<VisualInfo> visual_info_;

  DISALLOW_COPY_AND_ASSIGN(DCLayerTree);
};

class DCLayerTree::SwapChainPresenter {
 public:
  SwapChainPresenter(DCLayerTree* surface,
                     base::win::ScopedComPtr<ID3D11Device> d3d11_device);

  ~SwapChainPresenter();

  void PresentToSwapChain(const ui::DCRendererLayerParams& overlay);

  float swap_chain_scale_x() const { return swap_chain_scale_x_; }
  float swap_chain_scale_y() const { return swap_chain_scale_y_; }
  const base::win::ScopedComPtr<IDXGISwapChain1>& swap_chain() const {
    return swap_chain_;
  }

 private:
  using PFN_DCOMPOSITION_CREATE_SURFACE_HANDLE =
      HRESULT(WINAPI*)(DWORD, SECURITY_ATTRIBUTES*, HANDLE*);

  bool UploadVideoImages(gl::GLImageMemory* y_image_memory,
                         gl::GLImageMemory* uv_image_memory);
  // Returns true if the video processor changed.
  bool InitializeVideoProcessor(const gfx::Size& in_size,
                                const gfx::Size& out_size);
  void ReallocateSwapChain(bool yuy2);
  bool ShouldBeYUY2();

  DCLayerTree* surface_;
  PFN_DCOMPOSITION_CREATE_SURFACE_HANDLE create_surface_handle_function_;

  gfx::Size swap_chain_size_;
  gfx::Size processor_input_size_;
  gfx::Size processor_output_size_;
  bool is_yuy2_swapchain_ = false;

  // This is the scale from the swapchain size to the size of the contents
  // onscreen.
  float swap_chain_scale_x_ = 0.0f;
  float swap_chain_scale_y_ = 0.0f;

  PresentationHistory presentation_history_;
  bool failed_to_create_yuy2_swapchain_ = false;
  int frames_since_color_space_change_ = 0;

  // These are the GLImages that were presented in the last frame.
  std::vector<scoped_refptr<gl::GLImage>> last_gl_images_;

  base::win::ScopedComPtr<ID3D11Texture2D> staging_texture_;
  gfx::Size staging_texture_size_;

  base::win::ScopedComPtr<ID3D11Device> d3d11_device_;
  base::win::ScopedComPtr<IDXGISwapChain1> swap_chain_;
  base::win::ScopedComPtr<ID3D11VideoProcessorOutputView> out_view_;
  base::win::ScopedComPtr<ID3D11VideoProcessor> video_processor_;
  base::win::ScopedComPtr<ID3D11VideoProcessorEnumerator>
      video_processor_enumerator_;
  base::win::ScopedComPtr<ID3D11VideoDevice> video_device_;
  base::win::ScopedComPtr<ID3D11VideoContext> video_context_;

  base::win::ScopedHandle swap_chain_handle_;

  DISALLOW_COPY_AND_ASSIGN(SwapChainPresenter);
};

bool DCLayerTree::Initialize(HWND window) {
  d3d11_device_.CopyTo(video_device_.GetAddressOf());
  base::win::ScopedComPtr<ID3D11DeviceContext> context;
  d3d11_device_->GetImmediateContext(context.GetAddressOf());
  context.CopyTo(video_context_.GetAddressOf());

  base::win::ScopedComPtr<IDCompositionDesktopDevice> desktop_device;
  dcomp_device_.CopyTo(desktop_device.GetAddressOf());

  HRESULT hr = desktop_device->CreateTargetForHwnd(
      window, TRUE, dcomp_target_.GetAddressOf());
  if (FAILED(hr))
    return false;

  hr = dcomp_device_->CreateVisual(root_visual_.GetAddressOf());
  if (FAILED(hr))
    return false;

  dcomp_target_->SetRoot(root_visual_.Get());
  return true;
}

void DCLayerTree::InitializeVideoProcessor(const gfx::Size& input_size,
                                           const gfx::Size& output_size) {
  if (SizeContains(video_input_size_, input_size) &&
      SizeContains(video_output_size_, output_size))
    return;
  video_input_size_ = input_size;
  video_output_size_ = output_size;

  video_processor_.Reset();
  video_processor_enumerator_.Reset();
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc = {};
  desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  desc.InputFrameRate.Numerator = 60;
  desc.InputFrameRate.Denominator = 1;
  desc.InputWidth = input_size.width();
  desc.InputHeight = input_size.height();
  desc.OutputFrameRate.Numerator = 60;
  desc.OutputFrameRate.Denominator = 1;
  desc.OutputWidth = output_size.width();
  desc.OutputHeight = output_size.height();
  desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  HRESULT hr = video_device_->CreateVideoProcessorEnumerator(
      &desc, video_processor_enumerator_.GetAddressOf());
  CHECK(SUCCEEDED(hr));

  hr = video_device_->CreateVideoProcessor(video_processor_enumerator_.Get(), 0,
                                           video_processor_.GetAddressOf());
  CHECK(SUCCEEDED(hr));
}

base::win::ScopedComPtr<IDXGISwapChain1>
DCLayerTree::GetLayerSwapChainForTesting(size_t index) const {
  if (index >= visual_info_.size())
    return base::win::ScopedComPtr<IDXGISwapChain1>();
  return visual_info_[index].swap_chain;
}

DCLayerTree::SwapChainPresenter::SwapChainPresenter(
    DCLayerTree* surface,
    base::win::ScopedComPtr<ID3D11Device> d3d11_device)
    : surface_(surface), d3d11_device_(d3d11_device) {
  d3d11_device_.CopyTo(video_device_.GetAddressOf());
  base::win::ScopedComPtr<ID3D11DeviceContext> context;
  d3d11_device_->GetImmediateContext(context.GetAddressOf());
  context.CopyTo(video_context_.GetAddressOf());
  HMODULE dcomp = ::GetModuleHandleA("dcomp.dll");
  CHECK(dcomp);
  create_surface_handle_function_ =
      reinterpret_cast<PFN_DCOMPOSITION_CREATE_SURFACE_HANDLE>(
          GetProcAddress(dcomp, "DCompositionCreateSurfaceHandle"));
  CHECK(create_surface_handle_function_);
}

DCLayerTree::SwapChainPresenter::~SwapChainPresenter() {}

bool DCLayerTree::SwapChainPresenter::ShouldBeYUY2() {
  // Start out as YUY2.
  if (!presentation_history_.valid())
    return true;
  int composition_count = presentation_history_.composed_count();

  // It's more efficient to use a BGRA backbuffer instead of YUY2 if overlays
  // aren't being used, as otherwise DWM will use the video processor a second
  // time to convert it to BGRA before displaying it on screen.

  if (is_yuy2_swapchain_) {
    // Switch to BGRA once 3/4 of presents are composed.
    return composition_count < (PresentationHistory::kPresentsToStore * 3 / 4);
  } else {
    // Switch to YUY2 once 3/4 are using overlays (or unknown).
    return composition_count < (PresentationHistory::kPresentsToStore / 4);
  }
}

bool DCLayerTree::SwapChainPresenter::UploadVideoImages(
    gl::GLImageMemory* y_image_memory,
    gl::GLImageMemory* uv_image_memory) {
  gfx::Size texture_size = y_image_memory->GetSize();
  gfx::Size uv_image_size = uv_image_memory->GetSize();
  if (uv_image_size.height() != texture_size.height() / 2 ||
      uv_image_size.width() != texture_size.width() / 2 ||
      y_image_memory->format() != gfx::BufferFormat::R_8 ||
      uv_image_memory->format() != gfx::BufferFormat::RG_88) {
    DVLOG(ERROR) << "Invalid NV12 GLImageMemory properties.";
    return false;
  }

  if (!staging_texture_ || (staging_texture_size_ != texture_size)) {
    staging_texture_.Reset();
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = texture_size.width();
    desc.Height = texture_size.height();
    desc.Format = DXGI_FORMAT_NV12;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;

    // This isn't actually bound to a decoder, but dynamic textures need
    // BindFlags to be nonzero and D3D11_BIND_DECODER also works when creating
    // a VideoProcessorInputView.
    desc.BindFlags = D3D11_BIND_DECODER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    base::win::ScopedComPtr<ID3D11Texture2D> texture;
    HRESULT hr = d3d11_device_->CreateTexture2D(
        &desc, nullptr, staging_texture_.GetAddressOf());
    CHECK(SUCCEEDED(hr)) << "Creating D3D11 video upload texture failed: "
                         << std::hex << hr;
    staging_texture_size_ = texture_size;
  }
  base::win::ScopedComPtr<ID3D11DeviceContext> context;
  d3d11_device_->GetImmediateContext(context.GetAddressOf());
  D3D11_MAPPED_SUBRESOURCE mapped_resource;
  HRESULT hr = context->Map(staging_texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD,
                            0, &mapped_resource);
  CHECK(SUCCEEDED(hr)) << "Mapping D3D11 video upload texture failed: "
                       << std::hex << hr;

  size_t dest_stride = mapped_resource.RowPitch;
  for (int y = 0; y < texture_size.height(); y++) {
    const uint8_t* y_source =
        y_image_memory->memory() + y * y_image_memory->stride();
    uint8_t* dest =
        reinterpret_cast<uint8_t*>(mapped_resource.pData) + dest_stride * y;
    memcpy(dest, y_source, texture_size.width());
  }

  uint8_t* uv_dest_plane_start =
      reinterpret_cast<uint8_t*>(mapped_resource.pData) +
      dest_stride * texture_size.height();
  for (int y = 0; y < uv_image_size.height(); y++) {
    const uint8_t* uv_source =
        uv_image_memory->memory() + y * uv_image_memory->stride();
    uint8_t* dest = uv_dest_plane_start + dest_stride * y;
    memcpy(dest, uv_source, texture_size.width());
  }
  context->Unmap(staging_texture_.Get(), 0);
  return true;
}

void DCLayerTree::SwapChainPresenter::PresentToSwapChain(
    const ui::DCRendererLayerParams& params) {
  gl::GLImageDXGI* image_dxgi =
      gl::GLImageDXGI::FromGLImage(params.image[0].get());
  gl::GLImageMemory* y_image_memory = nullptr;
  gl::GLImageMemory* uv_image_memory = nullptr;
  if (params.image.size() >= 2) {
    y_image_memory = gl::GLImageMemory::FromGLImage(params.image[0].get());
    uv_image_memory = gl::GLImageMemory::FromGLImage(params.image[1].get());
  }

  if (!image_dxgi && (!y_image_memory || !uv_image_memory)) {
    DLOG(ERROR) << "Video GLImages are missing";
    last_gl_images_.clear();
    return;
  }

  // Swap chain size is the minimum of the on-screen size and the source
  // size so the video processor can do the minimal amount of work and
  // the overlay has to read the minimal amount of data.
  // DWM is also less likely to promote a surface to an overlay if it's
  // much larger than its area on-screen.
  gfx::Rect bounds_rect = params.rect;
  gfx::Size ceiled_input_size = gfx::ToCeiledSize(params.contents_rect.size());
  gfx::Size swap_chain_size = bounds_rect.size();
  swap_chain_size.SetToMin(ceiled_input_size);

  // YUY2 surfaces must have an even width.
  if (swap_chain_size.width() % 2 == 1)
    swap_chain_size.set_width(swap_chain_size.width() + 1);

  InitializeVideoProcessor(ceiled_input_size, swap_chain_size);

  if (surface_->workarounds().disable_larger_than_screen_overlays) {
    // Because of the rounding when converting between pixels and DIPs, a
    // fullscreen video can become slightly larger than the monitor - e.g. on
    // a 3000x2000 monitor with a scale factor of 1.75 a 1920x1079 video can
    // become 3002x1689.
    // On older Intel drivers, swapchains that are bigger than the monitor
    // won't be put into overlays, which will hurt power usage a lot. On those
    // systems, the scaling can be adjusted very slightly so that it's less
    // than the monitor size. This should be close to imperceptible.
    // TODO(jbauman): Remove when http://crbug.com/668278 is fixed.
    const int kOversizeMargin = 3;

    if ((bounds_rect.x() >= 0) &&
        (bounds_rect.width() > g_overlay_monitor_size.width()) &&
        (bounds_rect.width() <=
         g_overlay_monitor_size.width() + kOversizeMargin)) {
      bounds_rect.set_width(g_overlay_monitor_size.width());
    }

    if ((bounds_rect.y() >= 0) &&
        (bounds_rect.height() > g_overlay_monitor_size.height()) &&
        (bounds_rect.height() <=
         g_overlay_monitor_size.height() + kOversizeMargin)) {
      bounds_rect.set_height(g_overlay_monitor_size.height());
    }
  }

  swap_chain_scale_x_ = bounds_rect.width() * 1.0f / swap_chain_size.width();
  swap_chain_scale_y_ = bounds_rect.height() * 1.0f / swap_chain_size.height();

  bool yuy2_swapchain = ShouldBeYUY2();
  bool first_present = false;
  if (!swap_chain_ || swap_chain_size_ != swap_chain_size ||
      ((yuy2_swapchain != is_yuy2_swapchain_) &&
       !failed_to_create_yuy2_swapchain_)) {
    first_present = true;
    swap_chain_size_ = swap_chain_size;
    swap_chain_.Reset();
    ReallocateSwapChain(yuy2_swapchain);
  } else if (last_gl_images_ == params.image) {
    // The swap chain is presenting the same images as last swap, which means
    // that the images were never returned to the video decoder and should
    // have the same contents as last time. It shouldn't need to be redrawn.
    return;
  }

  last_gl_images_ = params.image;

  base::win::ScopedComPtr<ID3D11Texture2D> input_texture;
  UINT input_level;
  if (image_dxgi) {
    input_texture = image_dxgi->texture();
    input_level = (UINT)image_dxgi->level();
    staging_texture_.Reset();
  } else {
    DCHECK(y_image_memory);
    DCHECK(uv_image_memory);
    if (!UploadVideoImages(y_image_memory, uv_image_memory))
      return;
    DCHECK(staging_texture_);
    input_texture = staging_texture_;
    input_level = 0;
  }

  if (!out_view_) {
    base::win::ScopedComPtr<ID3D11Texture2D> texture;
    swap_chain_->GetBuffer(0, IID_PPV_ARGS(texture.GetAddressOf()));
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC out_desc = {};
    out_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    out_desc.Texture2D.MipSlice = 0;
    HRESULT hr = video_device_->CreateVideoProcessorOutputView(
        texture.Get(), video_processor_enumerator_.Get(), &out_desc,
        out_view_.GetAddressOf());
    CHECK(SUCCEEDED(hr));
  }

  // TODO(jbauman): Use correct colorspace.
  gfx::ColorSpace src_color_space = gfx::ColorSpace::CreateREC709();
  base::win::ScopedComPtr<ID3D11VideoContext1> context1;
  if (SUCCEEDED(video_context_.CopyTo(context1.GetAddressOf()))) {
    context1->VideoProcessorSetStreamColorSpace1(
        video_processor_.Get(), 0,
        gfx::ColorSpaceWin::GetDXGIColorSpace(src_color_space));
  } else {
    // This can't handle as many different types of color spaces, so use it
    // only if ID3D11VideoContext1 isn't available.
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE color_space =
        gfx::ColorSpaceWin::GetD3D11ColorSpace(src_color_space);
    video_context_->VideoProcessorSetStreamColorSpace(video_processor_.Get(), 0,
                                                      &color_space);
  }

  gfx::ColorSpace output_color_space =
      is_yuy2_swapchain_ ? src_color_space : gfx::ColorSpace::CreateSRGB();
  if (base::FeatureList::IsEnabled(kFallbackBT709VideoToBT601) &&
      (output_color_space == gfx::ColorSpace::CreateREC709())) {
    output_color_space = gfx::ColorSpace::CreateREC601();
  }

  base::win::ScopedComPtr<IDXGISwapChain3> swap_chain3;
  if (SUCCEEDED(swap_chain_.CopyTo(swap_chain3.GetAddressOf()))) {
    DXGI_COLOR_SPACE_TYPE color_space =
        gfx::ColorSpaceWin::GetDXGIColorSpace(output_color_space);
    HRESULT hr = swap_chain3->SetColorSpace1(color_space);
    CHECK(SUCCEEDED(hr));
    if (context1) {
      context1->VideoProcessorSetOutputColorSpace1(video_processor_.Get(),
                                                   color_space);
    } else {
      D3D11_VIDEO_PROCESSOR_COLOR_SPACE d3d11_color_space =
          gfx::ColorSpaceWin::GetD3D11ColorSpace(output_color_space);
      video_context_->VideoProcessorSetOutputColorSpace(video_processor_.Get(),
                                                        &d3d11_color_space);
    }
  }

  {
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC in_desc = {};
    in_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    in_desc.Texture2D.ArraySlice = input_level;
    base::win::ScopedComPtr<ID3D11VideoProcessorInputView> in_view;
    HRESULT hr = video_device_->CreateVideoProcessorInputView(
        input_texture.Get(), video_processor_enumerator_.Get(), &in_desc,
        in_view.GetAddressOf());
    CHECK(SUCCEEDED(hr));

    D3D11_VIDEO_PROCESSOR_STREAM stream = {};
    stream.Enable = true;
    stream.OutputIndex = 0;
    stream.InputFrameOrField = 0;
    stream.PastFrames = 0;
    stream.FutureFrames = 0;
    stream.pInputSurface = in_view.Get();
    RECT dest_rect = gfx::Rect(swap_chain_size).ToRECT();
    video_context_->VideoProcessorSetOutputTargetRect(video_processor_.Get(),
                                                      TRUE, &dest_rect);
    video_context_->VideoProcessorSetStreamDestRect(video_processor_.Get(), 0,
                                                    TRUE, &dest_rect);
    RECT source_rect = gfx::Rect(ceiled_input_size).ToRECT();
    video_context_->VideoProcessorSetStreamSourceRect(video_processor_.Get(), 0,
                                                      TRUE, &source_rect);

    video_context_->VideoProcessorSetStreamAutoProcessingMode(
        video_processor_.Get(), 0, FALSE);

    hr = video_context_->VideoProcessorBlt(video_processor_.Get(),
                                           out_view_.Get(), 0, 1, &stream);
    CHECK(SUCCEEDED(hr));
  }

  if (first_present) {
    swap_chain_->Present(0, 0);

    // DirectComposition can display black for a swapchain between the first
    // and second time it's presented to - maybe the first Present can get
    // lost somehow and it shows the wrong buffer. In that case copy the
    // buffers so both have the correct contents, which seems to help. The
    // first Present() after this needs to have SyncInterval > 0, or else the
    // workaround doesn't help.
    base::win::ScopedComPtr<ID3D11Texture2D> dest_texture;
    HRESULT hr =
        swap_chain_->GetBuffer(0, IID_PPV_ARGS(dest_texture.GetAddressOf()));
    DCHECK(SUCCEEDED(hr));
    base::win::ScopedComPtr<ID3D11Texture2D> src_texture;
    hr = swap_chain_->GetBuffer(1, IID_PPV_ARGS(src_texture.GetAddressOf()));
    DCHECK(SUCCEEDED(hr));
    base::win::ScopedComPtr<ID3D11DeviceContext> context;
    d3d11_device_->GetImmediateContext(context.GetAddressOf());
    context->CopyResource(dest_texture.Get(), src_texture.Get());

    // Additionally wait for the GPU to finish executing its commands, or
    // there still may be a black flicker when presenting expensive content
    // (e.g. 4k video).
    base::win::ScopedComPtr<IDXGIDevice2> dxgi_device2;
    hr = d3d11_device_.CopyTo(dxgi_device2.GetAddressOf());
    DCHECK(SUCCEEDED(hr));
    base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                              base::WaitableEvent::InitialState::NOT_SIGNALED);
    dxgi_device2->EnqueueSetEvent(event.handle());
    event.Wait();
  }

  swap_chain_->Present(1, 0);

  UMA_HISTOGRAM_BOOLEAN("GPU.DirectComposition.SwapchainFormat",
                        is_yuy2_swapchain_);
  frames_since_color_space_change_++;

  base::win::ScopedComPtr<IDXGISwapChainMedia> swap_chain_media;
  if (SUCCEEDED(swap_chain_.CopyTo(swap_chain_media.GetAddressOf()))) {
    DXGI_FRAME_STATISTICS_MEDIA stats = {};
    if (SUCCEEDED(swap_chain_media->GetFrameStatisticsMedia(&stats))) {
      UMA_HISTOGRAM_SPARSE_SLOWLY("GPU.DirectComposition.CompositionMode",
                                  stats.CompositionMode);
      presentation_history_.AddSample(stats.CompositionMode);
    }
  }
}

bool DCLayerTree::SwapChainPresenter::InitializeVideoProcessor(
    const gfx::Size& in_size,
    const gfx::Size& out_size) {
  if (video_processor_ && SizeContains(processor_input_size_, in_size) &&
      SizeContains(processor_output_size_, out_size))
    return false;
  processor_input_size_ = in_size;
  processor_output_size_ = out_size;
  surface_->InitializeVideoProcessor(in_size, out_size);

  video_processor_enumerator_ = surface_->video_processor_enumerator();
  video_processor_ = surface_->video_processor();
  // out_view_ depends on video_processor_enumerator_, so ensure it's
  // recreated if the enumerator is.
  out_view_.Reset();
  return true;
}

void DCLayerTree::SwapChainPresenter::ReallocateSwapChain(bool yuy2) {
  TRACE_EVENT0("gpu", "DCLayerTree::SwapChainPresenter::ReallocateSwapChain");
  DCHECK(!swap_chain_);

  base::win::ScopedComPtr<IDXGIDevice> dxgi_device;
  d3d11_device_.CopyTo(dxgi_device.GetAddressOf());
  base::win::ScopedComPtr<IDXGIAdapter> dxgi_adapter;
  dxgi_device->GetAdapter(dxgi_adapter.GetAddressOf());
  base::win::ScopedComPtr<IDXGIFactory2> dxgi_factory;
  dxgi_adapter->GetParent(IID_PPV_ARGS(dxgi_factory.GetAddressOf()));

  base::win::ScopedComPtr<IDXGIFactoryMedia> media_factory;
  dxgi_factory.CopyTo(media_factory.GetAddressOf());
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = swap_chain_size_.width();
  desc.Height = swap_chain_size_.height();
  desc.Format = DXGI_FORMAT_YUY2;
  desc.Stereo = FALSE;
  desc.SampleDesc.Count = 1;
  desc.BufferCount = 2;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  desc.Flags =
      DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO | DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO;

  HANDLE handle;
  create_surface_handle_function_(COMPOSITIONOBJECT_ALL_ACCESS, nullptr,
                                  &handle);
  swap_chain_handle_.Set(handle);

  if (is_yuy2_swapchain_ != yuy2) {
    UMA_HISTOGRAM_COUNTS_1000(
        "GPU.DirectComposition.FramesSinceColorSpaceChange",
        frames_since_color_space_change_);
  }

  frames_since_color_space_change_ = 0;

  is_yuy2_swapchain_ = false;
  // The composition surface handle isn't actually used, but
  // CreateSwapChainForComposition can't create YUY2 swapchains.
  HRESULT hr = E_FAIL;
  if (yuy2) {
    hr = media_factory->CreateSwapChainForCompositionSurfaceHandle(
        d3d11_device_.Get(), swap_chain_handle_.Get(), &desc, nullptr,
        swap_chain_.GetAddressOf());
    is_yuy2_swapchain_ = SUCCEEDED(hr);
    failed_to_create_yuy2_swapchain_ = !is_yuy2_swapchain_;
  }

  if (!is_yuy2_swapchain_) {
    if (yuy2) {
      DLOG(ERROR) << "YUY2 creation failed with " << std::hex << hr
                  << ". Falling back to BGRA";
    }
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Flags = 0;
    hr = media_factory->CreateSwapChainForCompositionSurfaceHandle(
        d3d11_device_.Get(), swap_chain_handle_.Get(), &desc, nullptr,
        swap_chain_.GetAddressOf());
    CHECK(SUCCEEDED(hr));
  }
  out_view_.Reset();
}

void DCLayerTree::InitVisual(size_t i) {
  DCHECK_GT(visual_info_.size(), i);
  VisualInfo* visual_info = &visual_info_[i];
  if (visual_info->content_visual)
    return;
  DCHECK(!visual_info->clip_visual);
  base::win::ScopedComPtr<IDCompositionVisual2> visual;
  dcomp_device_->CreateVisual(visual_info->clip_visual.GetAddressOf());
  dcomp_device_->CreateVisual(visual.GetAddressOf());
  visual_info->content_visual = visual;
  visual_info->clip_visual->AddVisual(visual.Get(), FALSE, nullptr);

  IDCompositionVisual2* last_visual =
      (i > 0) ? visual_info_[i - 1].clip_visual.Get() : nullptr;
  root_visual_->AddVisual(visual_info->clip_visual.Get(), TRUE, last_visual);
}

void DCLayerTree::UpdateVisualForVideo(
    VisualInfo* visual_info,
    const ui::DCRendererLayerParams& params) {
  base::win::ScopedComPtr<IDCompositionVisual2> dc_visual =
      visual_info->content_visual;

  gfx::Rect bounds_rect = params.rect;
  visual_info->surface.Reset();
  if (!visual_info->swap_chain_presenter) {
    visual_info->swap_chain_presenter =
        base::MakeUnique<SwapChainPresenter>(this, d3d11_device_);
  }
  visual_info->swap_chain_presenter->PresentToSwapChain(params);
  if (visual_info->swap_chain !=
      visual_info->swap_chain_presenter->swap_chain()) {
    visual_info->swap_chain = visual_info->swap_chain_presenter->swap_chain();
    dc_visual->SetContent(visual_info->swap_chain.Get());
  }

  if (visual_info->swap_chain_presenter->swap_chain_scale_x() !=
          visual_info->swap_chain_scale_x ||
      visual_info->swap_chain_presenter->swap_chain_scale_y() !=
          visual_info->swap_chain_scale_y ||
      params.transform != visual_info->transform ||
      visual_info->bounds != bounds_rect) {
    visual_info->swap_chain_scale_x =
        visual_info->swap_chain_presenter->swap_chain_scale_x();
    visual_info->swap_chain_scale_y =
        visual_info->swap_chain_presenter->swap_chain_scale_y();
    visual_info->transform = params.transform;
    visual_info->bounds = bounds_rect;

    gfx::Transform final_transform = params.transform;
    gfx::Transform scale_transform;
    scale_transform.Scale(
        visual_info->swap_chain_presenter->swap_chain_scale_x(),
        visual_info->swap_chain_presenter->swap_chain_scale_y());
    final_transform.PreconcatTransform(scale_transform);
    final_transform.Transpose();

    dc_visual->SetOffsetX(bounds_rect.x());
    dc_visual->SetOffsetY(bounds_rect.y());
    base::win::ScopedComPtr<IDCompositionMatrixTransform> dcomp_transform;
    dcomp_device_->CreateMatrixTransform(dcomp_transform.GetAddressOf());
    D2D_MATRIX_3X2_F d2d_matrix = {{{final_transform.matrix().get(0, 0),
                                     final_transform.matrix().get(0, 1),
                                     final_transform.matrix().get(1, 0),
                                     final_transform.matrix().get(1, 1),
                                     final_transform.matrix().get(3, 0),
                                     final_transform.matrix().get(3, 1)}}};
    dcomp_transform->SetMatrix(d2d_matrix);
    dc_visual->SetTransform(dcomp_transform.Get());
  }
}

void DCLayerTree::UpdateVisualForBackbuffer(
    VisualInfo* visual_info,
    const ui::DCRendererLayerParams& params) {
  base::win::ScopedComPtr<IDCompositionVisual2> dc_visual =
      visual_info->content_visual;

  visual_info->swap_chain_presenter = nullptr;
  if ((visual_info->surface != surface_->dcomp_surface()) ||
      (visual_info->swap_chain != surface_->swap_chain())) {
    visual_info->surface = surface_->dcomp_surface();
    visual_info->swap_chain = surface_->swap_chain();
    if (visual_info->surface) {
      dc_visual->SetContent(visual_info->surface.Get());
    } else if (visual_info->swap_chain) {
      dc_visual->SetContent(visual_info->swap_chain.Get());
    } else {
      dc_visual->SetContent(nullptr);
    }
  }

  gfx::Rect bounds_rect = params.rect;
  if (visual_info->bounds != bounds_rect ||
      !visual_info->transform.IsIdentity()) {
    dc_visual->SetOffsetX(bounds_rect.x());
    dc_visual->SetOffsetY(bounds_rect.y());
    visual_info->bounds = bounds_rect;
    dc_visual->SetTransform(nullptr);
    visual_info->transform = gfx::Transform();
  }
}

void DCLayerTree::UpdateVisualClip(VisualInfo* visual_info,
                                   const ui::DCRendererLayerParams& params) {
  if (params.is_clipped != visual_info->is_clipped ||
      params.clip_rect != visual_info->clip_rect) {
    // DirectComposition clips happen in the pre-transform visual
    // space, while cc/ clips happen post-transform. So the clip needs
    // to go on a separate parent visual that's untransformed.
    visual_info->is_clipped = params.is_clipped;
    visual_info->clip_rect = params.clip_rect;
    if (params.is_clipped) {
      base::win::ScopedComPtr<IDCompositionRectangleClip> clip;
      dcomp_device_->CreateRectangleClip(clip.GetAddressOf());
      gfx::Rect offset_clip = params.clip_rect;
      clip->SetLeft(offset_clip.x());
      clip->SetRight(offset_clip.right());
      clip->SetBottom(offset_clip.bottom());
      clip->SetTop(offset_clip.y());
      visual_info->clip_visual->SetClip(clip.Get());
    } else {
      visual_info->clip_visual->SetClip(nullptr);
    }
  }
}

bool DCLayerTree::CommitAndClearPendingOverlays() {
  TRACE_EVENT1("gpu", "DCLayerTree::CommitAndClearPendingOverlays", "size",
               pending_overlays_.size());
  UMA_HISTOGRAM_BOOLEAN("GPU.DirectComposition.OverlaysUsed",
                        !pending_overlays_.empty());
  // Add an overlay with z-order 0 representing the main plane.
  gfx::Size surface_size = surface_->GetSize();
  pending_overlays_.push_back(base::MakeUnique<ui::DCRendererLayerParams>(
      false, gfx::Rect(), 0, gfx::Transform(),
      std::vector<scoped_refptr<gl::GLImage>>(),
      gfx::RectF(gfx::SizeF(surface_size)), gfx::Rect(surface_size), 0, 0, 1.0,
      0));

  // TODO(jbauman): Reuse swapchains that are switched between overlays and
  // underlays.
  std::sort(pending_overlays_.begin(), pending_overlays_.end(),
            [](const auto& a, const auto& b) -> bool {
              return a->z_order < b->z_order;
            });

  while (visual_info_.size() > pending_overlays_.size()) {
    visual_info_.back().clip_visual->RemoveAllVisuals();
    root_visual_->RemoveVisual(visual_info_.back().clip_visual.Get());
    visual_info_.pop_back();
  }

  visual_info_.resize(pending_overlays_.size());

  // The overall visual tree has one clip visual for every overlay (including
  // the main plane). The clip visuals are in z_order and are all children of
  // a root visual. Each clip visual has a child visual that has the actual
  // plane content.

  for (size_t i = 0; i < pending_overlays_.size(); i++) {
    ui::DCRendererLayerParams& params = *pending_overlays_[i];
    VisualInfo* visual_info = &visual_info_[i];

    InitVisual(i);
    if (params.image.size() >= 1 && params.image[0]) {
      UpdateVisualForVideo(visual_info, params);
    } else if (params.image.empty()) {
      UpdateVisualForBackbuffer(visual_info, params);
    } else {
      CHECK(false);
    }
    UpdateVisualClip(visual_info, params);
  }

  HRESULT hr = dcomp_device_->Commit();
  CHECK(SUCCEEDED(hr));

  pending_overlays_.clear();
  return true;
}

bool DCLayerTree::ScheduleDCLayer(const ui::DCRendererLayerParams& params) {
  pending_overlays_.push_back(
      base::MakeUnique<ui::DCRendererLayerParams>(params));
  return true;
}

DirectCompositionSurfaceWin::DirectCompositionSurfaceWin(
    std::unique_ptr<gfx::VSyncProvider> vsync_provider,
    base::WeakPtr<ImageTransportSurfaceDelegate> delegate,
    HWND parent_window)
    : gl::GLSurfaceEGL(),
      child_window_(delegate, parent_window),
      workarounds_(delegate->GetFeatureInfo()->workarounds()),
      vsync_provider_(std::move(vsync_provider)) {}

DirectCompositionSurfaceWin::~DirectCompositionSurfaceWin() {
  Destroy();
}

// static
bool DirectCompositionSurfaceWin::AreOverlaysSupported() {
  if (!HardwareSupportsOverlays())
    return false;

  return base::FeatureList::IsEnabled(switches::kDirectCompositionOverlays);
}

bool DirectCompositionSurfaceWin::InitializeNativeWindow() {
  if (window_)
    return true;

  bool result = child_window_.Initialize();
  window_ = child_window_.window();
  return result;
}

bool DirectCompositionSurfaceWin::Initialize(gl::GLSurfaceFormat format) {
  d3d11_device_ = gl::QueryD3D11DeviceObjectFromANGLE();
  dcomp_device_ = gl::QueryDirectCompositionDevice(d3d11_device_);
  if (!dcomp_device_)
    return false;

  EGLDisplay display = GetDisplay();
  if (!window_) {
    if (!InitializeNativeWindow()) {
      DLOG(ERROR) << "Failed to initialize native window";
      return false;
    }
  }

  layer_tree_ =
      base::MakeUnique<DCLayerTree>(this, d3d11_device_, dcomp_device_);
  if (!layer_tree_->Initialize(window_))
    return false;

  std::vector<EGLint> pbuffer_attribs;
  pbuffer_attribs.push_back(EGL_WIDTH);
  pbuffer_attribs.push_back(1);
  pbuffer_attribs.push_back(EGL_HEIGHT);
  pbuffer_attribs.push_back(1);

  pbuffer_attribs.push_back(EGL_NONE);
  default_surface_ =
      eglCreatePbufferSurface(display, GetConfig(), &pbuffer_attribs[0]);
  CHECK(!!default_surface_);

  return true;
}

void DirectCompositionSurfaceWin::ReleaseCurrentSurface() {
  ReleaseDrawTexture(true);
  dcomp_surface_.Reset();
  swap_chain_.Reset();
}

void DirectCompositionSurfaceWin::InitializeSurface() {
  TRACE_EVENT1("gpu", "DirectCompositionSurfaceWin::InitializeSurface()",
               "enable_dc_layers_", enable_dc_layers_);
  DCHECK(!dcomp_surface_);
  DCHECK(!swap_chain_);
  DXGI_FORMAT output_format =
      base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kEnableHDR)
          ? DXGI_FORMAT_R16G16B16A16_FLOAT
          : DXGI_FORMAT_B8G8R8A8_UNORM;
  if (enable_dc_layers_) {
    // Always treat as premultiplied, because an underlay could cause it to
    // become transparent.
    HRESULT hr = dcomp_device_->CreateSurface(
        size_.width(), size_.height(), output_format,
        DXGI_ALPHA_MODE_PREMULTIPLIED, dcomp_surface_.GetAddressOf());
    has_been_rendered_to_ = false;
    CHECK(SUCCEEDED(hr));
  } else {
    DXGI_ALPHA_MODE alpha_mode =
        has_alpha_ ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;
    base::win::ScopedComPtr<IDXGIDevice> dxgi_device;
    d3d11_device_.CopyTo(dxgi_device.GetAddressOf());
    base::win::ScopedComPtr<IDXGIAdapter> dxgi_adapter;
    dxgi_device->GetAdapter(dxgi_adapter.GetAddressOf());
    base::win::ScopedComPtr<IDXGIFactory2> dxgi_factory;
    dxgi_adapter->GetParent(IID_PPV_ARGS(dxgi_factory.GetAddressOf()));

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = size_.width();
    desc.Height = size_.height();
    desc.Format = output_format;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.BufferCount = 2;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = alpha_mode;
    desc.Flags = 0;
    HRESULT hr = dxgi_factory->CreateSwapChainForComposition(
        d3d11_device_.Get(), &desc, nullptr, swap_chain_.GetAddressOf());
    has_been_rendered_to_ = false;
    first_swap_ = true;
    CHECK(SUCCEEDED(hr));
  }
}

void DirectCompositionSurfaceWin::ReleaseDrawTexture(bool will_discard) {
  if (real_surface_) {
    eglDestroySurface(GetDisplay(), real_surface_);
    real_surface_ = nullptr;
  }
  if (draw_texture_) {
    draw_texture_.Reset();
    if (dcomp_surface_) {
      HRESULT hr = dcomp_surface_->EndDraw();
      CHECK(SUCCEEDED(hr));
    } else if (!will_discard) {
      DXGI_PRESENT_PARAMETERS params = {};
      RECT dirty_rect = swap_rect_.ToRECT();
      params.DirtyRectsCount = 1;
      params.pDirtyRects = &dirty_rect;
      swap_chain_->Present1(first_swap_ ? 0 : 1, 0, &params);
      if (first_swap_) {
        // Wait for the GPU to finish executing its commands before
        // committing the DirectComposition tree, or else the swapchain
        // may flicker black when it's first presented.
        base::win::ScopedComPtr<IDXGIDevice2> dxgi_device2;
        HRESULT hr = d3d11_device_.CopyTo(dxgi_device2.GetAddressOf());
        DCHECK(SUCCEEDED(hr));
        base::WaitableEvent event(
            base::WaitableEvent::ResetPolicy::AUTOMATIC,
            base::WaitableEvent::InitialState::NOT_SIGNALED);
        dxgi_device2->EnqueueSetEvent(event.handle());
        event.Wait();
        first_swap_ = false;
      }
    }
  }
  if (dcomp_surface_ == g_current_surface)
    g_current_surface = nullptr;
}

void DirectCompositionSurfaceWin::Destroy() {
  if (default_surface_) {
    if (!eglDestroySurface(GetDisplay(), default_surface_)) {
      DLOG(ERROR) << "eglDestroySurface failed with error "
                  << ui::GetLastEGLErrorString();
    }
    default_surface_ = nullptr;
  }
  if (real_surface_) {
    if (!eglDestroySurface(GetDisplay(), real_surface_)) {
      DLOG(ERROR) << "eglDestroySurface failed with error "
                  << ui::GetLastEGLErrorString();
    }
    real_surface_ = nullptr;
  }
  if (dcomp_surface_ && (dcomp_surface_ == g_current_surface)) {
    HRESULT hr = dcomp_surface_->EndDraw();
    CHECK(SUCCEEDED(hr));
    g_current_surface = nullptr;
  }
  draw_texture_.Reset();
  dcomp_surface_.Reset();
}

gfx::Size DirectCompositionSurfaceWin::GetSize() {
  return size_;
}

bool DirectCompositionSurfaceWin::IsOffscreen() {
  return false;
}

void* DirectCompositionSurfaceWin::GetHandle() {
  return real_surface_ ? real_surface_ : default_surface_;
}

bool DirectCompositionSurfaceWin::Resize(const gfx::Size& size,
                                         float scale_factor,
                                         bool has_alpha) {
  if ((size == GetSize()) && (has_alpha == has_alpha_))
    return true;

  // Force a resize and redraw (but not a move, activate, etc.).
  if (!SetWindowPos(window_, nullptr, 0, 0, size.width(), size.height(),
                    SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOCOPYBITS |
                        SWP_NOOWNERZORDER | SWP_NOZORDER)) {
    return false;
  }
  size_ = size;
  has_alpha_ = has_alpha;
  ScopedReleaseCurrent release_current(this);
  // New surface will be initialized in SetDrawRectangle.
  ReleaseCurrentSurface();

  return true;
}

gfx::SwapResult DirectCompositionSurfaceWin::SwapBuffers() {
  {
    ScopedReleaseCurrent release_current(this);
    ReleaseDrawTexture(false);

    layer_tree_->CommitAndClearPendingOverlays();
  }
  child_window_.ClearInvalidContents();
  return gfx::SwapResult::SWAP_ACK;
}

gfx::SwapResult DirectCompositionSurfaceWin::PostSubBuffer(int x,
                                                           int y,
                                                           int width,
                                                           int height) {
  // The arguments are ignored because SetDrawRectangle specified the area to
  // be swapped.
  return SwapBuffers();
}

gfx::VSyncProvider* DirectCompositionSurfaceWin::GetVSyncProvider() {
  return vsync_provider_.get();
}

bool DirectCompositionSurfaceWin::ScheduleDCLayer(
    const ui::DCRendererLayerParams& params) {
  return layer_tree_->ScheduleDCLayer(params);
}

bool DirectCompositionSurfaceWin::SetEnableDCLayers(bool enable) {
  enable_dc_layers_ = enable;
  return true;
}


bool DirectCompositionSurfaceWin::FlipsVertically() const {
  return true;
}

bool DirectCompositionSurfaceWin::SupportsPostSubBuffer() {
  return true;
}

bool DirectCompositionSurfaceWin::OnMakeCurrent(gl::GLContext* context) {
  if (g_current_surface != dcomp_surface_) {
    if (g_current_surface) {
      HRESULT hr = g_current_surface->SuspendDraw();
      CHECK(SUCCEEDED(hr));
      g_current_surface = nullptr;
    }
    if (draw_texture_) {
      HRESULT hr = dcomp_surface_->ResumeDraw();
      CHECK(SUCCEEDED(hr));
      g_current_surface = dcomp_surface_.Get();
    }
  }
  return true;
}

bool DirectCompositionSurfaceWin::SupportsDCLayers() const {
  return true;
}

bool DirectCompositionSurfaceWin::SetDrawRectangle(const gfx::Rect& rectangle) {
  if (draw_texture_)
    return false;
  DCHECK(!real_surface_);
  ScopedReleaseCurrent release_current(this);

  if ((enable_dc_layers_ && !dcomp_surface_) ||
      (!enable_dc_layers_ && !swap_chain_)) {
    ReleaseCurrentSurface();
    InitializeSurface();
  }

  if (!gfx::Rect(size_).Contains(rectangle)) {
    DLOG(ERROR) << "Draw rectangle must be contained within size of surface";
    return false;
  }
  if (gfx::Rect(size_) != rectangle && !has_been_rendered_to_) {
    DLOG(ERROR) << "First draw to surface must draw to everything";
    return false;
  }

  CHECK(!g_current_surface);

  RECT rect = rectangle.ToRECT();
  if (dcomp_surface_) {
    POINT update_offset;
    HRESULT hr = dcomp_surface_->BeginDraw(
        &rect, IID_PPV_ARGS(draw_texture_.GetAddressOf()), &update_offset);
    draw_offset_ = gfx::Point(update_offset) - gfx::Rect(rect).origin();
    CHECK(SUCCEEDED(hr));
  } else {
    HRESULT hr =
        swap_chain_->GetBuffer(0, IID_PPV_ARGS(draw_texture_.GetAddressOf()));
    swap_rect_ = rectangle;
    draw_offset_ = gfx::Vector2d();
    CHECK(SUCCEEDED(hr));
  }
  has_been_rendered_to_ = true;

  g_current_surface = dcomp_surface_.Get();

  std::vector<EGLint> pbuffer_attribs{
      EGL_WIDTH,
      size_.width(),
      EGL_HEIGHT,
      size_.height(),
      EGL_FLEXIBLE_SURFACE_COMPATIBILITY_SUPPORTED_ANGLE,
      EGL_TRUE,
      EGL_NONE};

  EGLClientBuffer buffer =
      reinterpret_cast<EGLClientBuffer>(draw_texture_.Get());
  real_surface_ = eglCreatePbufferFromClientBuffer(
      GetDisplay(), EGL_D3D_TEXTURE_ANGLE, buffer, GetConfig(),
      &pbuffer_attribs[0]);

  return true;
}

gfx::Vector2d DirectCompositionSurfaceWin::GetDrawOffset() const {
  return draw_offset_;
}

void DirectCompositionSurfaceWin::WaitForSnapshotRendering() {
  DCHECK(gl::GLContext::GetCurrent()->IsCurrent(this));
  glFinish();
}

scoped_refptr<base::TaskRunner>
DirectCompositionSurfaceWin::GetWindowTaskRunnerForTesting() {
  return child_window_.GetTaskRunnerForTesting();
}

base::win::ScopedComPtr<IDXGISwapChain1>
DirectCompositionSurfaceWin::GetLayerSwapChainForTesting(size_t index) const {
  return layer_tree_->GetLayerSwapChainForTesting(index);
}

}  // namespace gpu
