// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/ws/platform_display_default.h"

#include <utility>

#include "base/memory/ptr_util.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "services/ui/display/screen_manager.h"
#include "services/ui/public/interfaces/cursor/cursor_struct_traits.h"
#include "services/ui/ws/server_window.h"
#include "ui/base/cursor/image_cursors.h"
#include "ui/display/display.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/platform_window/platform_ime_controller.h"
#include "ui/platform_window/platform_window.h"

#if defined(OS_WIN)
#include "ui/platform_window/win/win_window.h"
#elif defined(USE_X11) && !defined(OS_CHROMEOS)
#include "ui/platform_window/x11/x11_window.h"
#elif defined(OS_ANDROID)
#include "ui/platform_window/android/platform_window_android.h"
#elif defined(USE_OZONE)
#include "ui/ozone/public/cursor_factory_ozone.h"
#include "ui/ozone/public/ozone_platform.h"
#endif

namespace ui {
namespace ws {

PlatformDisplayDefault::PlatformDisplayDefault(
    ServerWindow* root_window,
    const display::ViewportMetrics& metrics,
    std::unique_ptr<ImageCursors> image_cursors)
    : root_window_(root_window),
      image_cursors_(std::move(image_cursors)),
      metrics_(metrics),
      widget_(gfx::kNullAcceleratedWidget) {}

PlatformDisplayDefault::~PlatformDisplayDefault() {
  // Don't notify the delegate from the destructor.
  delegate_ = nullptr;

  frame_generator_.reset();
  // Destroy the PlatformWindow early on as it may call us back during
  // destruction and we want to be in a known state. But destroy the surface
  // first because it can still be using the platform window.
  platform_window_.reset();
}

EventSink* PlatformDisplayDefault::GetEventSink() {
  return delegate_->GetEventSink();
}

void PlatformDisplayDefault::Init(PlatformDisplayDelegate* delegate) {
  DCHECK(delegate);
  delegate_ = delegate;

  const gfx::Rect& bounds = metrics_.bounds_in_pixels;
  DCHECK(!bounds.size().IsEmpty());

#if defined(OS_WIN)
  platform_window_ = base::MakeUnique<ui::WinWindow>(this, bounds);
#elif defined(USE_X11) && !defined(OS_CHROMEOS)
  platform_window_ = base::MakeUnique<ui::X11Window>(this, bounds);
#elif defined(OS_ANDROID)
  platform_window_ = base::MakeUnique<ui::PlatformWindowAndroid>(this);
  platform_window_->SetBounds(bounds);
#elif defined(USE_OZONE)
  platform_window_ =
      delegate_->GetOzonePlatform()->CreatePlatformWindow(this, bounds);
#else
  NOTREACHED() << "Unsupported platform";
#endif

  platform_window_->Show();
  if (image_cursors_) {
    image_cursors_->SetDisplay(delegate_->GetDisplay(),
                               metrics_.device_scale_factor);
  }
}

void PlatformDisplayDefault::SetViewportSize(const gfx::Size& size) {
  platform_window_->SetBounds(gfx::Rect(size));
}

void PlatformDisplayDefault::SetTitle(const base::string16& title) {
  platform_window_->SetTitle(title);
}

void PlatformDisplayDefault::SetCapture() {
  platform_window_->SetCapture();
}

void PlatformDisplayDefault::ReleaseCapture() {
  platform_window_->ReleaseCapture();
}

void PlatformDisplayDefault::SetCursor(const ui::CursorData& cursor_data) {
  if (!image_cursors_)
    return;

  ui::Cursor native_cursor(cursor_data.cursor_type());

#if defined(USE_OZONE)
  if (cursor_data.cursor_type() != ui::CursorType::kCustom) {
    image_cursors_->SetPlatformCursor(&native_cursor);
  } else {
    // In Ozone builds, we have an interface available which turns bitmap data
    // into platform cursors.
    ui::CursorFactoryOzone* cursor_factory =
        delegate_->GetOzonePlatform()->GetCursorFactoryOzone();
    native_cursor.SetPlatformCursor(cursor_factory->CreateAnimatedCursor(
        cursor_data.cursor_frames(), cursor_data.hotspot_in_pixels(),
        cursor_data.frame_delay().InMilliseconds(),
        cursor_data.scale_factor()));
  }
#else
  // Outside of ozone builds, there isn't a single interface for creating
  // PlatformCursors. The closest thing to one is in //content/ instead of
  // //ui/ which means we can't use it from here. For now, just don't handle
  // custom image cursors.
  //
  // TODO(erg): Once blink speaks directly to mus, make blink perform its own
  // cursor management on its own mus windows so we can remove Webcursor from
  // //content/ and do this in way that's safe cross-platform, instead of as an
  // ozone-specific hack.
  if (cursor_data.cursor_type() == ui::CursorType::kCustom) {
    NOTIMPLEMENTED() << "No custom cursor support on non-ozone yet.";
    native_cursor = ui::Cursor(ui::CursorType::kPointer);
  }
  image_cursors_->SetPlatformCursor(&native_cursor);
#endif

  platform_window_->SetCursor(native_cursor.platform());
}

void PlatformDisplayDefault::UpdateTextInputState(
    const ui::TextInputState& state) {
  ui::PlatformImeController* ime = platform_window_->GetPlatformImeController();
  if (ime)
    ime->UpdateTextInputState(state);
}

void PlatformDisplayDefault::SetImeVisibility(bool visible) {
  ui::PlatformImeController* ime = platform_window_->GetPlatformImeController();
  if (ime)
    ime->SetImeVisibility(visible);
}

FrameGenerator* PlatformDisplayDefault::GetFrameGenerator() {
  return frame_generator_.get();
}

void PlatformDisplayDefault::UpdateViewportMetrics(
    const display::ViewportMetrics& metrics) {
  if (metrics_ == metrics)
    return;

  gfx::Rect bounds = platform_window_->GetBounds();
  if (bounds.size() != metrics.bounds_in_pixels.size()) {
    bounds.set_size(metrics.bounds_in_pixels.size());
    platform_window_->SetBounds(bounds);
  }

  metrics_ = metrics;
  if (frame_generator_) {
    frame_generator_->SetDeviceScaleFactor(metrics_.device_scale_factor);
    frame_generator_->OnWindowSizeChanged(metrics_.bounds_in_pixels.size());
  }
}

gfx::AcceleratedWidget PlatformDisplayDefault::GetAcceleratedWidget() const {
  return widget_;
}

void PlatformDisplayDefault::UpdateEventRootLocation(ui::LocatedEvent* event) {
  // TODO(riajiang): This is broken for HDPI because it mixes PPs and DIPs. See
  // http://crbug.com/701036 for details.
  const display::Display& display = delegate_->GetDisplay();
  gfx::Point location = event->location();
  location.Offset(display.bounds().x(), display.bounds().y());
  event->set_root_location(location);
}

void PlatformDisplayDefault::OnBoundsChanged(const gfx::Rect& new_bounds) {
  // We only care if the window size has changed.
  if (new_bounds.size() == metrics_.bounds_in_pixels.size())
    return;

  // TODO(tonikitoo): Handle the bounds changing in external window mode. The
  // window should be resized by the WS and it shouldn't involve ScreenManager.
}

void PlatformDisplayDefault::OnDamageRect(const gfx::Rect& damaged_region) {
  if (frame_generator_)
    frame_generator_->OnWindowDamaged();
}

void PlatformDisplayDefault::DispatchEvent(ui::Event* event) {
  if (event->IsLocatedEvent())
    UpdateEventRootLocation(event->AsLocatedEvent());

  if (event->IsScrollEvent()) {
    // TODO(moshayedi): crbug.com/602859. Dispatch scroll events as
    // they are once we have proper support for scroll events.

    ui::PointerEvent pointer_event(
        ui::MouseWheelEvent(*event->AsScrollEvent()));
    SendEventToSink(&pointer_event);
  } else if (event->IsMouseEvent()) {
    ui::PointerEvent pointer_event(*event->AsMouseEvent());
    SendEventToSink(&pointer_event);
  } else if (event->IsTouchEvent()) {
    ui::PointerEvent pointer_event(*event->AsTouchEvent());
    SendEventToSink(&pointer_event);
  } else {
    SendEventToSink(event);
  }
}

void PlatformDisplayDefault::OnCloseRequest() {
  // TODO(tonikitoo): Handle a close request in external window mode. The window
  // should be closed by the WS and it shouldn't involve ScreenManager.
  const int64_t display_id = delegate_->GetDisplay().id();
  display::ScreenManager::GetInstance()->RequestCloseDisplay(display_id);
}

void PlatformDisplayDefault::OnClosed() {}

void PlatformDisplayDefault::OnWindowStateChanged(
    ui::PlatformWindowState new_state) {}

void PlatformDisplayDefault::OnLostCapture() {
  delegate_->OnNativeCaptureLost();
}

void PlatformDisplayDefault::OnAcceleratedWidgetAvailable(
    gfx::AcceleratedWidget widget,
    float device_scale_factor) {
  // This will get called after Init() is called, either synchronously as part
  // of the Init() callstack or async after Init() has returned, depending on
  // the platform.
  DCHECK_EQ(gfx::kNullAcceleratedWidget, widget_);
  widget_ = widget;
  delegate_->OnAcceleratedWidgetAvailable();

  cc::mojom::MojoCompositorFrameSinkAssociatedPtr compositor_frame_sink;
  cc::mojom::DisplayPrivateAssociatedPtr display_private;
  cc::mojom::MojoCompositorFrameSinkClientPtr compositor_frame_sink_client;
  cc::mojom::MojoCompositorFrameSinkClientRequest
      compositor_frame_sink_client_request =
          mojo::MakeRequest(&compositor_frame_sink_client);

  root_window_->CreateRootCompositorFrameSink(
      widget_, mojo::MakeRequest(&compositor_frame_sink),
      std::move(compositor_frame_sink_client),
      mojo::MakeRequest(&display_private));

  frame_generator_ = base::MakeUnique<FrameGenerator>();
  auto frame_sink_client_binding =
      base::MakeUnique<CompositorFrameSinkClientBinding>(
          frame_generator_.get(),
          std::move(compositor_frame_sink_client_request),
          std::move(compositor_frame_sink), std::move(display_private));
  frame_generator_->Bind(std::move(frame_sink_client_binding));
  frame_generator_->OnWindowSizeChanged(root_window_->bounds().size());
  frame_generator_->SetDeviceScaleFactor(metrics_.device_scale_factor);
}

void PlatformDisplayDefault::OnAcceleratedWidgetDestroyed() {
  NOTREACHED();
}

void PlatformDisplayDefault::OnActivationChanged(bool active) {}

}  // namespace ws
}  // namespace ui
