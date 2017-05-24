// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/service/direct_composition_surface_win.h"

#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/win/scoped_gdi_object.h"
#include "base/win/scoped_hdc.h"
#include "base/win/scoped_select_object.h"
#include "gpu/command_buffer/service/feature_info.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/win/hidden_window.h"
#include "ui/gfx/buffer_format_util.h"
#include "ui/gfx/gdi_util.h"
#include "ui/gfx/transform.h"
#include "ui/gl/dc_renderer_layer_params.h"
#include "ui/gl/gl_angle_util_win.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_image_dxgi.h"
#include "ui/gl/gl_image_ref_counted_memory.h"
#include "ui/gl/init/gl_factory.h"
#include "ui/platform_window/platform_window_delegate.h"
#include "ui/platform_window/win/win_window.h"

namespace gpu {
namespace {

bool CheckIfDCSupported() {
  if (!gl::QueryDirectCompositionDevice(
          gl::QueryD3D11DeviceObjectFromANGLE())) {
    LOG(WARNING)
        << "GL implementation not using DirectComposition, skipping test.";
    return false;
  }
  return true;
}

class TestImageTransportSurfaceDelegate
    : public ImageTransportSurfaceDelegate,
      public base::SupportsWeakPtr<TestImageTransportSurfaceDelegate> {
 public:
  TestImageTransportSurfaceDelegate()
      : feature_info_(new gpu::gles2::FeatureInfo()) {}

  ~TestImageTransportSurfaceDelegate() override {}

  // ImageTransportSurfaceDelegate implementation.
  void DidCreateAcceleratedSurfaceChildWindow(
      SurfaceHandle parent_window,
      SurfaceHandle child_window) override {
    if (parent_window)
      ::SetParent(child_window, parent_window);
  }
  void DidSwapBuffersComplete(SwapBuffersCompleteParams params) override {}
  const gles2::FeatureInfo* GetFeatureInfo() const override {
    return feature_info_.get();
  }
  void SetLatencyInfoCallback(const LatencyInfoCallback& callback) override {}
  void UpdateVSyncParameters(base::TimeTicks timebase,
                             base::TimeDelta interval) override {}
  void AddFilter(IPC::MessageFilter* message_filter) override {}
  int32_t GetRouteID() const override { return 0; }

 private:
  scoped_refptr<gpu::gles2::FeatureInfo> feature_info_;
};

class TestPlatformDelegate : public ui::PlatformWindowDelegate {
 public:
  // ui::PlatformWindowDelegate implementation.
  void OnBoundsChanged(const gfx::Rect& new_bounds) override {}
  void OnDamageRect(const gfx::Rect& damaged_region) override {}
  void DispatchEvent(ui::Event* event) override {}
  void OnCloseRequest() override {}
  void OnClosed() override {}
  void OnWindowStateChanged(ui::PlatformWindowState new_state) override {}
  void OnLostCapture() override {}
  void OnAcceleratedWidgetAvailable(gfx::AcceleratedWidget widget,
                                    float device_pixel_ratio) override {}
  void OnAcceleratedWidgetDestroyed() override {}
  void OnActivationChanged(bool active) override {}
};

void RunPendingTasks(scoped_refptr<base::TaskRunner> task_runner) {
  base::WaitableEvent done(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                           base::WaitableEvent::InitialState::NOT_SIGNALED);
  task_runner->PostTask(FROM_HERE,
                        Bind(&base::WaitableEvent::Signal, Unretained(&done)));
  done.Wait();
}

void DestroySurface(scoped_refptr<DirectCompositionSurfaceWin> surface) {
  scoped_refptr<base::TaskRunner> task_runner =
      surface->GetWindowTaskRunnerForTesting();
  DCHECK(surface->HasOneRef());

  surface = nullptr;

  // Ensure that the ChildWindowWin posts the task to delete the thread to the
  // main loop before doing RunUntilIdle. Otherwise the child threads could
  // outlive the main thread.
  RunPendingTasks(task_runner);

  base::RunLoop().RunUntilIdle();
}

base::win::ScopedComPtr<ID3D11Texture2D> CreateNV12Texture(
    const base::win::ScopedComPtr<ID3D11Device>& d3d11_device,
    const gfx::Size& size) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = size.width();
  desc.Height = size.height();
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_NV12;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.SampleDesc.Count = 1;
  desc.BindFlags = 0;

  std::vector<char> image_data(size.width() * size.height() * 3 / 2);
  // Y, U, and V should all be Oxff. Output color should be pink.
  memset(&image_data[0], 0xff, size.width() * size.height() * 3 / 2);

  D3D11_SUBRESOURCE_DATA data = {};
  data.pSysMem = (const void*)&image_data[0];
  data.SysMemPitch = size.width();

  base::win::ScopedComPtr<ID3D11Texture2D> texture;
  HRESULT hr =
      d3d11_device->CreateTexture2D(&desc, &data, texture.GetAddressOf());
  CHECK(SUCCEEDED(hr));
  return texture;
}

TEST(DirectCompositionSurfaceTest, TestMakeCurrent) {
  if (!CheckIfDCSupported())
    return;

  TestImageTransportSurfaceDelegate delegate;

  scoped_refptr<DirectCompositionSurfaceWin> surface1(
      new DirectCompositionSurfaceWin(nullptr, delegate.AsWeakPtr(),
                                      ui::GetHiddenWindow()));
  EXPECT_TRUE(surface1->Initialize());
  surface1->SetEnableDCLayers(true);

  scoped_refptr<gl::GLContext> context1 = gl::init::CreateGLContext(
      nullptr, surface1.get(), gl::GLContextAttribs());
  EXPECT_TRUE(surface1->Resize(gfx::Size(100, 100), 1.0, true));

  // First SetDrawRectangle must be full size of surface.
  EXPECT_FALSE(surface1->SetDrawRectangle(gfx::Rect(0, 0, 50, 50)));
  EXPECT_TRUE(surface1->SetDrawRectangle(gfx::Rect(0, 0, 100, 100)));

  // SetDrawRectangle can't be called again until swap.
  EXPECT_FALSE(surface1->SetDrawRectangle(gfx::Rect(0, 0, 100, 100)));

  EXPECT_TRUE(context1->MakeCurrent(surface1.get()));
  EXPECT_EQ(gfx::SwapResult::SWAP_ACK, surface1->SwapBuffers());

  EXPECT_TRUE(context1->IsCurrent(surface1.get()));

  // SetDrawRectangle must be contained within surface.
  EXPECT_FALSE(surface1->SetDrawRectangle(gfx::Rect(0, 0, 101, 101)));
  EXPECT_TRUE(surface1->SetDrawRectangle(gfx::Rect(0, 0, 100, 100)));
  EXPECT_TRUE(context1->IsCurrent(surface1.get()));

  EXPECT_TRUE(surface1->Resize(gfx::Size(50, 50), 1.0, true));
  EXPECT_TRUE(context1->IsCurrent(surface1.get()));
  EXPECT_TRUE(surface1->SetDrawRectangle(gfx::Rect(0, 0, 50, 50)));
  EXPECT_TRUE(context1->IsCurrent(surface1.get()));

  scoped_refptr<DirectCompositionSurfaceWin> surface2(
      new DirectCompositionSurfaceWin(nullptr, delegate.AsWeakPtr(),
                                      ui::GetHiddenWindow()));
  EXPECT_TRUE(surface2->Initialize());

  scoped_refptr<gl::GLContext> context2 = gl::init::CreateGLContext(
      nullptr, surface2.get(), gl::GLContextAttribs());
  surface2->SetEnableDCLayers(true);
  EXPECT_TRUE(context2->MakeCurrent(surface2.get()));
  EXPECT_TRUE(surface2->Resize(gfx::Size(100, 100), 1.0, true));
  // The previous IDCompositionSurface should be suspended when another
  // surface is being drawn to.
  EXPECT_TRUE(surface2->SetDrawRectangle(gfx::Rect(0, 0, 100, 100)));
  EXPECT_TRUE(context2->IsCurrent(surface2.get()));

  // It should be possible to switch back to the previous surface and
  // unsuspend it.
  EXPECT_TRUE(context1->MakeCurrent(surface1.get()));
  context2 = nullptr;
  context1 = nullptr;

  DestroySurface(std::move(surface1));
  DestroySurface(std::move(surface2));
}

// Tests that switching using EnableDCLayers works.
TEST(DirectCompositionSurfaceTest, DXGIDCLayerSwitch) {
  if (!CheckIfDCSupported())
    return;

  TestImageTransportSurfaceDelegate delegate;

  scoped_refptr<DirectCompositionSurfaceWin> surface(
      new DirectCompositionSurfaceWin(nullptr, delegate.AsWeakPtr(),
                                      ui::GetHiddenWindow()));
  EXPECT_TRUE(surface->Initialize());

  scoped_refptr<gl::GLContext> context =
      gl::init::CreateGLContext(nullptr, surface.get(), gl::GLContextAttribs());
  EXPECT_TRUE(surface->Resize(gfx::Size(100, 100), 1.0, true));
  EXPECT_FALSE(surface->swap_chain());

  // First SetDrawRectangle must be full size of surface for DXGI
  // swapchain.
  EXPECT_FALSE(surface->SetDrawRectangle(gfx::Rect(0, 0, 50, 50)));
  EXPECT_TRUE(surface->SetDrawRectangle(gfx::Rect(0, 0, 100, 100)));
  EXPECT_TRUE(surface->swap_chain());

  // SetDrawRectangle can't be called again until swap.
  EXPECT_FALSE(surface->SetDrawRectangle(gfx::Rect(0, 0, 100, 100)));

  EXPECT_TRUE(context->MakeCurrent(surface.get()));
  EXPECT_EQ(gfx::SwapResult::SWAP_ACK, surface->SwapBuffers());

  EXPECT_TRUE(context->IsCurrent(surface.get()));

  surface->SetEnableDCLayers(true);

  // Surface switched to use IDCompositionSurface, so must draw to
  // entire surface.
  EXPECT_FALSE(surface->SetDrawRectangle(gfx::Rect(0, 0, 50, 50)));
  EXPECT_TRUE(surface->SetDrawRectangle(gfx::Rect(0, 0, 100, 100)));
  EXPECT_TRUE(context->IsCurrent(surface.get()));
  EXPECT_FALSE(surface->swap_chain());

  surface->SetEnableDCLayers(false);

  EXPECT_EQ(gfx::SwapResult::SWAP_ACK, surface->SwapBuffers());

  // Surface switched to use IDXGISwapChain, so must draw to entire
  // surface.
  EXPECT_FALSE(surface->SetDrawRectangle(gfx::Rect(0, 0, 50, 50)));
  EXPECT_TRUE(surface->SetDrawRectangle(gfx::Rect(0, 0, 100, 100)));
  EXPECT_TRUE(surface->swap_chain());

  context = nullptr;
  DestroySurface(std::move(surface));
}

// Ensure that the swapchain's alpha is correct.
TEST(DirectCompositionSurfaceTest, SwitchAlpha) {
  if (!CheckIfDCSupported())
    return;

  TestImageTransportSurfaceDelegate delegate;

  scoped_refptr<DirectCompositionSurfaceWin> surface(
      new DirectCompositionSurfaceWin(nullptr, delegate.AsWeakPtr(),
                                      ui::GetHiddenWindow()));
  EXPECT_TRUE(surface->Initialize());

  scoped_refptr<gl::GLContext> context =
      gl::init::CreateGLContext(nullptr, surface.get(), gl::GLContextAttribs());
  EXPECT_TRUE(surface->Resize(gfx::Size(100, 100), 1.0, true));
  EXPECT_FALSE(surface->swap_chain());

  EXPECT_TRUE(surface->SetDrawRectangle(gfx::Rect(0, 0, 100, 100)));
  base::win::ScopedComPtr<IDXGISwapChain1> swap_chain = surface->swap_chain();
  ASSERT_TRUE(swap_chain);
  DXGI_SWAP_CHAIN_DESC1 desc;
  swap_chain->GetDesc1(&desc);
  EXPECT_EQ(DXGI_ALPHA_MODE_PREMULTIPLIED, desc.AlphaMode);

  // Resize to the same parameters should have no effect.
  EXPECT_TRUE(surface->Resize(gfx::Size(100, 100), 1.0, true));
  EXPECT_TRUE(surface->swap_chain());

  EXPECT_TRUE(surface->Resize(gfx::Size(100, 100), 1.0, false));
  EXPECT_FALSE(surface->swap_chain());

  EXPECT_TRUE(surface->SetDrawRectangle(gfx::Rect(0, 0, 100, 100)));

  swap_chain = surface->swap_chain();
  ASSERT_TRUE(swap_chain);
  swap_chain->GetDesc1(&desc);
  EXPECT_EQ(DXGI_ALPHA_MODE_IGNORE, desc.AlphaMode);

  context = nullptr;
  DestroySurface(std::move(surface));
}

// Ensure that the GLImage isn't presented again unless it changes.
TEST(DirectCompositionSurfaceTest, NoPresentTwice) {
  if (!CheckIfDCSupported())
    return;

  TestImageTransportSurfaceDelegate delegate;
  scoped_refptr<DirectCompositionSurfaceWin> surface(
      new DirectCompositionSurfaceWin(nullptr, delegate.AsWeakPtr(),
                                      ui::GetHiddenWindow()));
  EXPECT_TRUE(surface->Initialize());
  surface->SetEnableDCLayers(true);
  gfx::Size window_size(100, 100);

  scoped_refptr<gl::GLContext> context =
      gl::init::CreateGLContext(nullptr, surface.get(), gl::GLContextAttribs());

  base::win::ScopedComPtr<ID3D11Device> d3d11_device =
      gl::QueryD3D11DeviceObjectFromANGLE();

  gfx::Size texture_size(50, 50);
  base::win::ScopedComPtr<ID3D11Texture2D> texture =
      CreateNV12Texture(d3d11_device, texture_size);

  scoped_refptr<gl::GLImageDXGI> image_dxgi(
      new gl::GLImageDXGI(texture_size, nullptr));
  image_dxgi->SetTexture(texture, 0);

  ui::DCRendererLayerParams params(
      false, gfx::Rect(), 1, gfx::Transform(),
      std::vector<scoped_refptr<gl::GLImage>>{image_dxgi},
      gfx::RectF(gfx::Rect(texture_size)), gfx::Rect(window_size), 0, 0, 1.0,
      0);
  surface->ScheduleDCLayer(params);

  base::win::ScopedComPtr<IDXGISwapChain1> swap_chain =
      surface->GetLayerSwapChainForTesting(1);
  ASSERT_FALSE(swap_chain);

  EXPECT_EQ(gfx::SwapResult::SWAP_ACK, surface->SwapBuffers());

  swap_chain = surface->GetLayerSwapChainForTesting(1);
  ASSERT_TRUE(swap_chain);

  UINT last_present_count = 0;
  EXPECT_TRUE(SUCCEEDED(swap_chain->GetLastPresentCount(&last_present_count)));

  // One present is normal, and a second present because it's the first frame
  // and the other buffer needs to be drawn to.
  EXPECT_EQ(2u, last_present_count);

  surface->ScheduleDCLayer(params);
  EXPECT_EQ(gfx::SwapResult::SWAP_ACK, surface->SwapBuffers());

  base::win::ScopedComPtr<IDXGISwapChain1> swap_chain2 =
      surface->GetLayerSwapChainForTesting(1);
  EXPECT_EQ(swap_chain2, swap_chain);

  // It's the same image, so it should have the same swapchain.
  EXPECT_TRUE(SUCCEEDED(swap_chain->GetLastPresentCount(&last_present_count)));
  EXPECT_EQ(2u, last_present_count);

  // The size of the swapchain changed, so it should be recreated.
  ui::DCRendererLayerParams params2(
      false, gfx::Rect(), 1, gfx::Transform(),
      std::vector<scoped_refptr<gl::GLImage>>{image_dxgi},
      gfx::RectF(gfx::Rect(texture_size)), gfx::Rect(0, 0, 25, 25), 0, 0, 1.0,
      0);
  surface->ScheduleDCLayer(params2);

  EXPECT_EQ(gfx::SwapResult::SWAP_ACK, surface->SwapBuffers());

  base::win::ScopedComPtr<IDXGISwapChain1> swap_chain3 =
      surface->GetLayerSwapChainForTesting(1);
  EXPECT_NE(swap_chain2, swap_chain3);

  context = nullptr;
  DestroySurface(std::move(surface));
}

COLORREF ReadBackWindowPixel(HWND window, const gfx::Point& point) {
  base::win::ScopedCreateDC mem_hdc(::CreateCompatibleDC(nullptr));
  void* bits = nullptr;
  BITMAPV4HEADER hdr;
  gfx::CreateBitmapV4Header(point.x() + 1, point.y() + 1, &hdr);
  DCHECK(mem_hdc.IsValid());
  base::win::ScopedBitmap bitmap(
      ::CreateDIBSection(mem_hdc.Get(), reinterpret_cast<BITMAPINFO*>(&hdr),
                         DIB_RGB_COLORS, &bits, nullptr, 0));
  DCHECK(bitmap.is_valid());

  base::win::ScopedSelectObject select_object(mem_hdc.Get(), bitmap.get());

  // Grab a copy of the window. Use PrintWindow because it works even when the
  // window's partially occluded. The PW_RENDERFULLCONTENT flag is undocumented,
  // but works starting in Windows 8.1. It allows for capturing the contents of
  // the window that are drawn using DirectComposition.
  UINT flags = PW_CLIENTONLY | PW_RENDERFULLCONTENT;

  BOOL result = PrintWindow(window, mem_hdc.Get(), flags);
  if (!result)
    PLOG(ERROR) << "Failed to print window";

  GdiFlush();

  uint32_t pixel_value =
      static_cast<uint32_t*>(bits)[hdr.bV4Width * point.y() + point.x()];

  return pixel_value;
}

class DirectCompositionPixelTest : public testing::Test {
 public:
  DirectCompositionPixelTest()
      : window_(&platform_delegate_, gfx::Rect(0, 0, 100, 100)) {}

 protected:
  void InitializeSurface() {
    static_cast<ui::PlatformWindow*>(&window_)->Show();

    surface_ = new DirectCompositionSurfaceWin(nullptr, delegate_.AsWeakPtr(),
                                               window_.hwnd());
    EXPECT_TRUE(surface_->Initialize());
  }

  void PixelTestSwapChain(bool layers_enabled) {
    if (!CheckIfDCSupported())
      return;

    InitializeSurface();

    surface_->SetEnableDCLayers(layers_enabled);
    gfx::Size window_size(100, 100);

    scoped_refptr<gl::GLContext> context = gl::init::CreateGLContext(
        nullptr, surface_.get(), gl::GLContextAttribs());
    EXPECT_TRUE(surface_->Resize(window_size, 1.0, true));
    EXPECT_TRUE(surface_->SetDrawRectangle(gfx::Rect(window_size)));
    EXPECT_TRUE(context->MakeCurrent(surface_.get()));

    glClearColor(1.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    EXPECT_EQ(gfx::SwapResult::SWAP_ACK, surface_->SwapBuffers());

    // Ensure DWM swap completed.
    Sleep(1000);

    SkColor actual_color =
        ReadBackWindowPixel(window_.hwnd(), gfx::Point(75, 75));
    EXPECT_EQ(SK_ColorRED, actual_color);

    EXPECT_TRUE(context->IsCurrent(surface_.get()));

    context = nullptr;
    DestroySurface(std::move(surface_));
  }

  TestPlatformDelegate platform_delegate_;
  TestImageTransportSurfaceDelegate delegate_;
  ui::WinWindow window_;
  scoped_refptr<DirectCompositionSurfaceWin> surface_;
};

TEST_F(DirectCompositionPixelTest, DCLayersEnabled) {
  PixelTestSwapChain(true);
}

TEST_F(DirectCompositionPixelTest, DCLayersDisabled) {
  PixelTestSwapChain(false);
}

bool AreColorsSimilar(int a, int b) {
  // The precise colors may differ depending on the video processor, so allow
  // a margin for error.
  const int kMargin = 10;
  return abs(SkColorGetA(a) - SkColorGetA(b)) < kMargin &&
         abs(SkColorGetR(a) - SkColorGetR(b)) < kMargin &&
         abs(SkColorGetG(a) - SkColorGetG(b)) < kMargin &&
         abs(SkColorGetB(a) - SkColorGetB(b)) < kMargin;
}

TEST_F(DirectCompositionPixelTest, VideoSwapchain) {
  if (!CheckIfDCSupported())
    return;
  InitializeSurface();
  surface_->SetEnableDCLayers(true);
  gfx::Size window_size(100, 100);

  scoped_refptr<gl::GLContext> context = gl::init::CreateGLContext(
      nullptr, surface_.get(), gl::GLContextAttribs());
  EXPECT_TRUE(surface_->Resize(window_size, 1.0, true));

  base::win::ScopedComPtr<ID3D11Device> d3d11_device =
      gl::QueryD3D11DeviceObjectFromANGLE();

  gfx::Size texture_size(50, 50);
  base::win::ScopedComPtr<ID3D11Texture2D> texture =
      CreateNV12Texture(d3d11_device, texture_size);

  scoped_refptr<gl::GLImageDXGI> image_dxgi(
      new gl::GLImageDXGI(texture_size, nullptr));
  image_dxgi->SetTexture(texture, 0);

  ui::DCRendererLayerParams params(
      false, gfx::Rect(), 1, gfx::Transform(),
      std::vector<scoped_refptr<gl::GLImage>>{image_dxgi},
      gfx::RectF(gfx::Rect(texture_size)), gfx::Rect(texture_size), 0, 0, 1.0,
      0);
  surface_->ScheduleDCLayer(params);

  EXPECT_EQ(gfx::SwapResult::SWAP_ACK, surface_->SwapBuffers());

  // Scaling up the swapchain with the same image should cause it to be
  // transformed again, but not presented again.
  ui::DCRendererLayerParams params2(
      false, gfx::Rect(), 1, gfx::Transform(),
      std::vector<scoped_refptr<gl::GLImage>>{image_dxgi},
      gfx::RectF(gfx::Rect(texture_size)), gfx::Rect(window_size), 0, 0, 1.0,
      0);
  surface_->ScheduleDCLayer(params2);

  EXPECT_EQ(gfx::SwapResult::SWAP_ACK, surface_->SwapBuffers());
  Sleep(1000);

  SkColor expected_color = SkColorSetRGB(0xff, 0xb7, 0xff);
  SkColor actual_color =
      ReadBackWindowPixel(window_.hwnd(), gfx::Point(75, 75));
  EXPECT_TRUE(AreColorsSimilar(expected_color, actual_color))
      << std::hex << "Expected " << expected_color << " Actual "
      << actual_color;

  context = nullptr;
  DestroySurface(std::move(surface_));
}

TEST_F(DirectCompositionPixelTest, SoftwareVideoSwapchain) {
  if (!CheckIfDCSupported())
    return;
  InitializeSurface();
  surface_->SetEnableDCLayers(true);
  gfx::Size window_size(100, 100);

  scoped_refptr<gl::GLContext> context = gl::init::CreateGLContext(
      nullptr, surface_.get(), gl::GLContextAttribs());
  EXPECT_TRUE(surface_->Resize(window_size, 1.0, true));

  base::win::ScopedComPtr<ID3D11Device> d3d11_device =
      gl::QueryD3D11DeviceObjectFromANGLE();

  gfx::Size y_size(50, 50);
  gfx::Size uv_size(25, 25);
  size_t y_stride =
      gfx::RowSizeForBufferFormat(y_size.width(), gfx::BufferFormat::R_8, 0);
  size_t uv_stride =
      gfx::RowSizeForBufferFormat(uv_size.width(), gfx::BufferFormat::RG_88, 0);
  std::vector<uint8_t> y_data(y_stride * y_size.height(), 0xff);
  std::vector<uint8_t> uv_data(uv_stride * uv_size.height(), 0xff);
  scoped_refptr<gl::GLImageRefCountedMemory> y_image(
      new gl::GLImageRefCountedMemory(y_size, GL_BGRA_EXT));

  y_image->Initialize(new base::RefCountedBytes(y_data),
                      gfx::BufferFormat::R_8);
  scoped_refptr<gl::GLImageRefCountedMemory> uv_image(
      new gl::GLImageRefCountedMemory(uv_size, GL_BGRA_EXT));
  uv_image->Initialize(new base::RefCountedBytes(uv_data),
                       gfx::BufferFormat::RG_88);

  ui::DCRendererLayerParams params(
      false, gfx::Rect(), 1, gfx::Transform(),
      std::vector<scoped_refptr<gl::GLImage>>{y_image, uv_image},
      gfx::RectF(gfx::Rect(y_size)), gfx::Rect(window_size), 0, 0, 1.0, 0);
  surface_->ScheduleDCLayer(params);

  EXPECT_EQ(gfx::SwapResult::SWAP_ACK, surface_->SwapBuffers());
  Sleep(1000);

  SkColor expected_color = SkColorSetRGB(0xff, 0xb7, 0xff);
  SkColor actual_color =
      ReadBackWindowPixel(window_.hwnd(), gfx::Point(75, 75));
  EXPECT_TRUE(AreColorsSimilar(expected_color, actual_color))
      << std::hex << "Expected " << expected_color << " Actual "
      << actual_color;

  context = nullptr;
  DestroySurface(std::move(surface_));
}
}  // namespace
}  // namespace gpu
