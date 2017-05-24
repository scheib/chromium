// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/frame_host/render_widget_host_view_child_frame.h"

#include <stdint.h>

#include <utility>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/test/scoped_task_environment.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "cc/surfaces/compositor_frame_sink_support.h"
#include "cc/surfaces/surface.h"
#include "cc/surfaces/surface_manager.h"
#include "cc/surfaces/surface_sequence.h"
#include "cc/test/begin_frame_args_test.h"
#include "cc/test/fake_external_begin_frame_source.h"
#include "content/browser/compositor/test/no_transport_image_transport_factory.h"
#include "content/browser/frame_host/cross_process_frame_connector.h"
#include "content/browser/gpu/compositor_util.h"
#include "content/browser/renderer_host/render_widget_host_delegate.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/common/view_messages.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/test_browser_context.h"
#include "content/test/fake_renderer_compositor_frame_sink.h"
#include "content/test/test_render_view_host.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/compositor.h"

namespace content {
namespace {

const cc::LocalSurfaceId kArbitraryLocalSurfaceId(
    1,
    base::UnguessableToken::Deserialize(2, 3));

class MockRenderWidgetHostDelegate : public RenderWidgetHostDelegate {
 public:
  MockRenderWidgetHostDelegate() {}
  ~MockRenderWidgetHostDelegate() override {}
 private:
  void Cut() override {}
  void Copy() override {}
  void Paste() override {}
  void SelectAll() override {}
};

class MockCrossProcessFrameConnector : public CrossProcessFrameConnector {
 public:
  MockCrossProcessFrameConnector() : CrossProcessFrameConnector(nullptr) {}
  ~MockCrossProcessFrameConnector() override {}

  void SetChildFrameSurface(const cc::SurfaceInfo& surface_info,
                            const cc::SurfaceSequence& sequence) override {
    last_surface_info_ = surface_info;
  }

  RenderWidgetHostViewBase* GetParentRenderWidgetHostView() override {
    return nullptr;
  }

  cc::SurfaceInfo last_surface_info_;
};

}  // namespace

class RenderWidgetHostViewChildFrameTest : public testing::Test {
 public:
  RenderWidgetHostViewChildFrameTest()
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::UI) {}

  void SetUp() override {
    browser_context_.reset(new TestBrowserContext);

// ImageTransportFactory doesn't exist on Android.
#if !defined(OS_ANDROID)
    ImageTransportFactory::InitializeForUnitTests(
        base::WrapUnique(new NoTransportImageTransportFactory));
#endif

    MockRenderProcessHost* process_host =
        new MockRenderProcessHost(browser_context_.get());
    int32_t routing_id = process_host->GetNextRoutingID();
    widget_host_ =
        new RenderWidgetHostImpl(&delegate_, process_host, routing_id, false);
    view_ = RenderWidgetHostViewChildFrame::Create(widget_host_);

    test_frame_connector_ = new MockCrossProcessFrameConnector();
    view_->SetCrossProcessFrameConnector(test_frame_connector_);

    cc::mojom::MojoCompositorFrameSinkPtr sink;
    cc::mojom::MojoCompositorFrameSinkRequest sink_request =
        mojo::MakeRequest(&sink);
    cc::mojom::MojoCompositorFrameSinkClientRequest client_request =
        mojo::MakeRequest(&renderer_compositor_frame_sink_ptr_);
    renderer_compositor_frame_sink_ =
        base::MakeUnique<FakeRendererCompositorFrameSink>(
            std::move(sink), std::move(client_request));
    view_->DidCreateNewRendererCompositorFrameSink(
        renderer_compositor_frame_sink_ptr_.get());
  }

  void TearDown() override {
    if (view_)
      view_->Destroy();
    delete widget_host_;
    delete test_frame_connector_;

    browser_context_.reset();

    base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE,
                                                    browser_context_.release());
    base::RunLoop().RunUntilIdle();
#if !defined(OS_ANDROID)
    ImageTransportFactory::Terminate();
#endif
  }

  cc::SurfaceId GetSurfaceId() const {
    return cc::SurfaceId(view_->frame_sink_id_, view_->local_surface_id_);
  }

  cc::LocalSurfaceId GetLocalSurfaceId() const {
    return view_->local_surface_id_;
  }

  void ClearCompositorSurfaceIfNecessary() {
    view_->ClearCompositorSurfaceIfNecessary();
  }

 protected:
  base::test::ScopedTaskEnvironment scoped_task_environment_;

  std::unique_ptr<BrowserContext> browser_context_;
  MockRenderWidgetHostDelegate delegate_;

  // Tests should set these to NULL if they've already triggered their
  // destruction.
  RenderWidgetHostImpl* widget_host_;
  RenderWidgetHostViewChildFrame* view_;
  MockCrossProcessFrameConnector* test_frame_connector_;
  std::unique_ptr<FakeRendererCompositorFrameSink>
      renderer_compositor_frame_sink_;

 private:
  cc::mojom::MojoCompositorFrameSinkClientPtr
      renderer_compositor_frame_sink_ptr_;

  DISALLOW_COPY_AND_ASSIGN(RenderWidgetHostViewChildFrameTest);
};

cc::CompositorFrame CreateDelegatedFrame(float scale_factor,
                                         gfx::Size size,
                                         const gfx::Rect& damage) {
  cc::CompositorFrame frame;
  frame.metadata.device_scale_factor = scale_factor;
  frame.metadata.begin_frame_ack = cc::BeginFrameAck(0, 1, 1, true);

  std::unique_ptr<cc::RenderPass> pass = cc::RenderPass::Create();
  pass->SetNew(1, gfx::Rect(size), damage, gfx::Transform());
  frame.render_pass_list.push_back(std::move(pass));
  return frame;
}

TEST_F(RenderWidgetHostViewChildFrameTest, VisibilityTest) {
  view_->Show();
  ASSERT_TRUE(view_->IsShowing());

  view_->Hide();
  ASSERT_FALSE(view_->IsShowing());
}

// Verify that SubmitCompositorFrame behavior is correct when a delegated
// frame is received from a renderer process.
TEST_F(RenderWidgetHostViewChildFrameTest, SwapCompositorFrame) {
  gfx::Size view_size(100, 100);
  gfx::Rect view_rect(view_size);
  float scale_factor = 1.f;
  cc::LocalSurfaceId local_surface_id(1, base::UnguessableToken::Create());

  view_->SetSize(view_size);
  view_->Show();

  view_->SubmitCompositorFrame(
      local_surface_id,
      CreateDelegatedFrame(scale_factor, view_size, view_rect));

  cc::SurfaceId id = GetSurfaceId();
  if (id.is_valid()) {
#if !defined(OS_ANDROID)
    ImageTransportFactory* factory = ImageTransportFactory::GetInstance();
    cc::SurfaceManager* manager =
        factory->GetContextFactoryPrivate()->GetSurfaceManager();
    cc::Surface* surface = manager->GetSurfaceForId(id);
    EXPECT_TRUE(surface);
    // There should be a SurfaceSequence created by the RWHVChildFrame.
    EXPECT_EQ(1u, surface->GetDestructionDependencyCount());
#endif

    // Surface ID should have been passed to CrossProcessFrameConnector to
    // be sent to the embedding renderer.
    EXPECT_EQ(cc::SurfaceInfo(id, scale_factor, view_size),
              test_frame_connector_->last_surface_info_);
  }
}

// Check that the same local surface id can be used after frame eviction.
TEST_F(RenderWidgetHostViewChildFrameTest, FrameEviction) {
  gfx::Size view_size(100, 100);
  gfx::Rect view_rect(view_size);
  float scale_factor = 1.f;

  view_->SetSize(view_size);
  view_->Show();

  // Submit a frame.
  view_->SubmitCompositorFrame(
      kArbitraryLocalSurfaceId,
      CreateDelegatedFrame(scale_factor, view_size, view_rect));

  EXPECT_EQ(kArbitraryLocalSurfaceId, GetLocalSurfaceId());
  EXPECT_TRUE(view_->has_frame());

  // Evict the frame. has_frame() should return false.
  ClearCompositorSurfaceIfNecessary();
  EXPECT_EQ(kArbitraryLocalSurfaceId, GetLocalSurfaceId());
  EXPECT_FALSE(view_->has_frame());

  // Submit another frame with the same local surface id. The same id should be
  // usable.
  view_->SubmitCompositorFrame(
      kArbitraryLocalSurfaceId,
      CreateDelegatedFrame(scale_factor, view_size, view_rect));
  EXPECT_EQ(kArbitraryLocalSurfaceId, GetLocalSurfaceId());
  EXPECT_TRUE(view_->has_frame());
}

// Tests that BeginFrameAcks are forwarded correctly from the
// SwapCompositorFrame and DidNotProduceFrame IPCs through the
// CompositorFrameSinkSupport.
TEST_F(RenderWidgetHostViewChildFrameTest, ForwardsBeginFrameAcks) {
  gfx::Size view_size(100, 100);
  gfx::Rect view_rect(view_size);
  float scale_factor = 1.f;

  view_->SetSize(view_size);
  view_->Show();

  // Replace BeginFrameSource so that we can observe acknowledgments.
  cc::FakeExternalBeginFrameSource source(0.f, false);
  uint32_t source_id = source.source_id();
  view_->support_->SetBeginFrameSource(&source);
  view_->SetNeedsBeginFrames(true);

  {
    cc::BeginFrameArgs args =
        cc::CreateBeginFrameArgsForTesting(BEGINFRAME_FROM_HERE, source_id, 5u);
    source.TestOnBeginFrame(args);

    // Ack from CompositorFrame is forwarded.
    cc::BeginFrameAck ack(source_id, 5, 4, true);
    cc::CompositorFrame frame =
        CreateDelegatedFrame(scale_factor, view_size, view_rect);
    frame.metadata.begin_frame_ack = ack;
    view_->SubmitCompositorFrame(kArbitraryLocalSurfaceId, std::move(frame));
    EXPECT_EQ(ack, source.LastAckForObserver(view_->support_.get()));
  }

  {
    cc::BeginFrameArgs args =
        cc::CreateBeginFrameArgsForTesting(BEGINFRAME_FROM_HERE, source_id, 6u);
    source.TestOnBeginFrame(args);

    // Explicit ack through OnDidNotProduceFrame is forwarded.
    cc::BeginFrameAck ack(source_id, 6, 4, false);
    view_->OnDidNotProduceFrame(ack);
    EXPECT_EQ(ack, source.LastAckForObserver(view_->support_.get()));
  }

  view_->SetNeedsBeginFrames(false);
}

}  // namespace content
