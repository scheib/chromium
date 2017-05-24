// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/surfaces/direct_compositor_frame_sink.h"

#include <memory>

#include "base/memory/ptr_util.h"
#include "cc/output/renderer_settings.h"
#include "cc/output/texture_mailbox_deleter.h"
#include "cc/scheduler/begin_frame_source.h"
#include "cc/scheduler/delay_based_time_source.h"
#include "cc/surfaces/display.h"
#include "cc/surfaces/display_scheduler.h"
#include "cc/surfaces/frame_sink_id.h"
#include "cc/surfaces/local_surface_id_allocator.h"
#include "cc/surfaces/surface_manager.h"
#include "cc/test/begin_frame_args_test.h"
#include "cc/test/fake_compositor_frame_sink_client.h"
#include "cc/test/fake_output_surface.h"
#include "cc/test/ordered_simple_task_runner.h"
#include "cc/test/test_context_provider.h"
#include "cc/test/test_gpu_memory_buffer_manager.h"
#include "cc/test/test_shared_bitmap_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cc {
namespace {

static constexpr FrameSinkId kArbitraryFrameSinkId(1, 1);

class ObserverTrackingBeginFrameSource : public BackToBackBeginFrameSource {
 public:
  using BackToBackBeginFrameSource::BackToBackBeginFrameSource;

  void DidFinishFrame(BeginFrameObserver* obs,
                      const BeginFrameAck& ack) override {
    BackToBackBeginFrameSource::DidFinishFrame(obs, ack);
    if (obs == tracked_observer_)
      last_ack_ = ack;
  }

  void set_tracked_observer(BeginFrameObserver* obs) {
    tracked_observer_ = obs;
  }

  const BeginFrameAck& last_ack() const { return last_ack_; }

 private:
  BeginFrameObserver* tracked_observer_ = nullptr;
  BeginFrameAck last_ack_;
};

class TestDirectCompositorFrameSink : public DirectCompositorFrameSink {
 public:
  using DirectCompositorFrameSink::DirectCompositorFrameSink;

  CompositorFrameSinkSupport* support() const { return support_.get(); }
};

class DirectCompositorFrameSinkTest : public testing::Test {
 public:
  DirectCompositorFrameSinkTest()
      : now_src_(new base::SimpleTestTickClock()),
        task_runner_(new OrderedSimpleTaskRunner(now_src_.get(), true)),
        display_size_(1920, 1080),
        display_rect_(display_size_),
        context_provider_(TestContextProvider::Create()) {
    surface_manager_.RegisterFrameSinkId(kArbitraryFrameSinkId);

    std::unique_ptr<FakeOutputSurface> display_output_surface =
        FakeOutputSurface::Create3d();
    display_output_surface_ = display_output_surface.get();

    begin_frame_source_.reset(new ObserverTrackingBeginFrameSource(
        base::MakeUnique<DelayBasedTimeSource>(task_runner_.get())));

    int max_frames_pending = 2;
    std::unique_ptr<DisplayScheduler> scheduler(
        new DisplayScheduler(task_runner_.get(), max_frames_pending));

    display_.reset(new Display(
        &bitmap_manager_, &gpu_memory_buffer_manager_, RendererSettings(),
        kArbitraryFrameSinkId, begin_frame_source_.get(),
        std::move(display_output_surface), std::move(scheduler),
        base::MakeUnique<TextureMailboxDeleter>(task_runner_.get())));
    compositor_frame_sink_.reset(new TestDirectCompositorFrameSink(
        kArbitraryFrameSinkId, &surface_manager_, display_.get(),
        context_provider_, nullptr, &gpu_memory_buffer_manager_,
        &bitmap_manager_));

    compositor_frame_sink_->BindToClient(&compositor_frame_sink_client_);
    begin_frame_source_->set_tracked_observer(
        compositor_frame_sink_->support());
    display_->Resize(display_size_);
    display_->SetVisible(true);

    EXPECT_FALSE(
        compositor_frame_sink_client_.did_lose_compositor_frame_sink_called());
  }

  ~DirectCompositorFrameSinkTest() override {
    compositor_frame_sink_->DetachFromClient();
  }

  void SwapBuffersWithDamage(const gfx::Rect& damage_rect) {
    std::unique_ptr<RenderPass> render_pass(RenderPass::Create());
    render_pass->SetNew(1, display_rect_, damage_rect, gfx::Transform());

    CompositorFrame frame;
    frame.metadata.begin_frame_ack = BeginFrameAck(0, 1, 1, true);
    frame.render_pass_list.push_back(std::move(render_pass));

    compositor_frame_sink_->SubmitCompositorFrame(std::move(frame));
  }

  void SetUp() override {
    // Draw the first frame to start in an "unlocked" state.
    SwapBuffersWithDamage(display_rect_);

    EXPECT_EQ(0u, display_output_surface_->num_sent_frames());
    task_runner_->RunUntilIdle();
    EXPECT_EQ(1u, display_output_surface_->num_sent_frames());
  }

 protected:
  std::unique_ptr<base::SimpleTestTickClock> now_src_;
  scoped_refptr<OrderedSimpleTaskRunner> task_runner_;

  const gfx::Size display_size_;
  const gfx::Rect display_rect_;
  SurfaceManager surface_manager_;
  TestSharedBitmapManager bitmap_manager_;
  TestGpuMemoryBufferManager gpu_memory_buffer_manager_;

  scoped_refptr<TestContextProvider> context_provider_;
  FakeOutputSurface* display_output_surface_ = nullptr;
  std::unique_ptr<ObserverTrackingBeginFrameSource> begin_frame_source_;
  std::unique_ptr<Display> display_;
  FakeCompositorFrameSinkClient compositor_frame_sink_client_;
  std::unique_ptr<TestDirectCompositorFrameSink> compositor_frame_sink_;
};

TEST_F(DirectCompositorFrameSinkTest, DamageTriggersSwapBuffers) {
  SwapBuffersWithDamage(display_rect_);
  EXPECT_EQ(1u, display_output_surface_->num_sent_frames());
  task_runner_->RunUntilIdle();
  EXPECT_EQ(2u, display_output_surface_->num_sent_frames());
}

TEST_F(DirectCompositorFrameSinkTest, NoDamageDoesNotTriggerSwapBuffers) {
  SwapBuffersWithDamage(gfx::Rect());
  EXPECT_EQ(1u, display_output_surface_->num_sent_frames());
  task_runner_->RunUntilIdle();
  EXPECT_EQ(1u, display_output_surface_->num_sent_frames());
}

TEST_F(DirectCompositorFrameSinkTest, SuspendedDoesNotTriggerSwapBuffers) {
  SwapBuffersWithDamage(display_rect_);
  EXPECT_EQ(1u, display_output_surface_->num_sent_frames());
  display_output_surface_->set_suspended_for_recycle(true);
  task_runner_->RunUntilIdle();
  EXPECT_EQ(1u, display_output_surface_->num_sent_frames());
  SwapBuffersWithDamage(display_rect_);
  task_runner_->RunUntilIdle();
  EXPECT_EQ(1u, display_output_surface_->num_sent_frames());
  display_output_surface_->set_suspended_for_recycle(false);
  SwapBuffersWithDamage(display_rect_);
  task_runner_->RunUntilIdle();
  EXPECT_EQ(2u, display_output_surface_->num_sent_frames());
}

class TestBeginFrameObserver : public BeginFrameObserverBase {
 public:
  const BeginFrameAck& ack() const { return ack_; }

 private:
  bool OnBeginFrameDerivedImpl(const BeginFrameArgs& args) override {
    ack_ = BeginFrameAck(args.source_id, args.sequence_number,
                         args.sequence_number, false);
    return true;
  }

  void OnBeginFrameSourcePausedChanged(bool paused) override{};

  BeginFrameAck ack_;
};

TEST_F(DirectCompositorFrameSinkTest, AcknowledgesBeginFramesWithDamage) {
  // Verify that the frame sink acknowledged the BeginFrame attached to
  // CompositorFrame submitted during SetUp().
  EXPECT_EQ(BeginFrameAck(0, 1, 1, true), begin_frame_source_->last_ack());
}

TEST_F(DirectCompositorFrameSinkTest, AcknowledgesBeginFramesWithoutDamage) {
  // Request a BeginFrame from the CompositorFrameSinkClient.
  TestBeginFrameObserver observer;
  compositor_frame_sink_client_.begin_frame_source()->AddObserver(&observer);
  task_runner_->RunUntilIdle();
  EXPECT_LE(BeginFrameArgs::kStartingFrameNumber,
            observer.ack().sequence_number);
  compositor_frame_sink_client_.begin_frame_source()->DidFinishFrame(
      &observer, observer.ack());
  compositor_frame_sink_->DidNotProduceFrame(observer.ack());
  compositor_frame_sink_client_.begin_frame_source()->RemoveObserver(&observer);

  // Verify that the frame sink acknowledged the last BeginFrame.
  EXPECT_EQ(observer.ack(), begin_frame_source_->last_ack());
}

}  // namespace
}  // namespace cc
