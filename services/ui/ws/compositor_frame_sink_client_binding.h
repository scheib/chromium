// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_WS_COMPOSITOR_FRAME_SINK_CLIENT_BINDING_H_
#define SERVICES_UI_WS_COMPOSITOR_FRAME_SINK_CLIENT_BINDING_H_

#include "base/macros.h"
#include "cc/ipc/frame_sink_manager.mojom.h"
#include "cc/ipc/mojo_compositor_frame_sink.mojom.h"
#include "cc/surfaces/local_surface_id_allocator.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace ui {
namespace ws {

// CompositorFrameSinkClientBinding manages the binding between a FrameGenerator
// and its MojoCompositorFrameSink. CompositorFrameSinkClientBinding exists so
// that a mock implementation of MojoCompositorFrameSink can be injected for
// tests. FrameGenerator owns its associated CompositorFrameSinkClientBinding.
class CompositorFrameSinkClientBinding
    : public cc::mojom::MojoCompositorFrameSink {
 public:
  CompositorFrameSinkClientBinding(
      cc::mojom::MojoCompositorFrameSinkClient* sink_client,
      cc::mojom::MojoCompositorFrameSinkClientRequest sink_client_request,
      cc::mojom::MojoCompositorFrameSinkAssociatedPtr compositor_frame_sink,
      cc::mojom::DisplayPrivateAssociatedPtr display_private);
  ~CompositorFrameSinkClientBinding() override;

 private:
  // cc::mojom::MojoCompositorFrameSink implementation:
  void SubmitCompositorFrame(const cc::LocalSurfaceId& local_surface_id,
                             cc::CompositorFrame frame) override;
  void SetNeedsBeginFrame(bool needs_begin_frame) override;
  void DidNotProduceFrame(const cc::BeginFrameAck& ack) override;
  void EvictCurrentSurface() override;

  mojo::Binding<cc::mojom::MojoCompositorFrameSinkClient> binding_;
  cc::mojom::DisplayPrivateAssociatedPtr display_private_;
  cc::mojom::MojoCompositorFrameSinkAssociatedPtr compositor_frame_sink_;
  cc::LocalSurfaceId local_surface_id_;
  cc::LocalSurfaceIdAllocator id_allocator_;
  gfx::Size last_submitted_frame_size_;

  DISALLOW_COPY_AND_ASSIGN(CompositorFrameSinkClientBinding);
};
}
}

#endif  // SERVICES_UI_WS_COMPOSITOR_FRAME_SINK_CLIENT_BINDING_H_
