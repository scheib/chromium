// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/containers/flat_set.h"
#include "cc/surfaces/compositor_frame_sink_support.h"
#include "cc/surfaces/surface_id.h"
#include "cc/surfaces/surface_manager.h"
#include "cc/surfaces/surface_observer.h"
#include "cc/test/begin_frame_args_test.h"
#include "cc/test/compositor_frame_helpers.h"
#include "cc/test/fake_external_begin_frame_source.h"
#include "cc/test/mock_compositor_frame_sink_support_client.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::IsEmpty;
using testing::UnorderedElementsAre;

namespace cc {
namespace test {
namespace {

constexpr bool kIsRoot = true;
constexpr bool kIsChildRoot = false;
constexpr bool kHandlesFrameSinkIdInvalidation = true;
constexpr bool kNeedsSyncPoints = true;
constexpr FrameSinkId kDisplayFrameSink(2, 0);
constexpr FrameSinkId kParentFrameSink(3, 0);
constexpr FrameSinkId kChildFrameSink1(65563, 0);
constexpr FrameSinkId kChildFrameSink2(65564, 0);
constexpr FrameSinkId kArbitraryFrameSink(1337, 7331);

std::vector<SurfaceId> empty_surface_ids() {
  return std::vector<SurfaceId>();
}

SurfaceId MakeSurfaceId(const FrameSinkId& frame_sink_id, uint32_t local_id) {
  return SurfaceId(
      frame_sink_id,
      LocalSurfaceId(local_id, base::UnguessableToken::Deserialize(0, 1u)));
}

}  // namespace

class SurfaceSynchronizationTest : public testing::Test,
                                   public SurfaceObserver {
 public:
  SurfaceSynchronizationTest()
      : surface_manager_(SurfaceManager::LifetimeType::REFERENCES) {}
  ~SurfaceSynchronizationTest() override {}

  CompositorFrameSinkSupport& display_support() { return *supports_[0]; }
  Surface* display_surface() {
    return display_support().current_surface_for_testing();
  }

  CompositorFrameSinkSupport& parent_support() { return *supports_[1]; }
  Surface* parent_surface() {
    return parent_support().current_surface_for_testing();
  }
  const ReferencedSurfaceTracker& parent_reference_tracker() {
    return parent_support().ReferenceTrackerForTesting();
  }

  CompositorFrameSinkSupport& child_support1() { return *supports_[2]; }
  Surface* child_surface1() {
    return child_support1().current_surface_for_testing();
  }

  CompositorFrameSinkSupport& child_support2() { return *supports_[3]; }
  Surface* child_surface2() {
    return child_support2().current_surface_for_testing();
  }

  CompositorFrameSinkSupport& support(int index) { return *supports_[index]; }
  Surface* surface(int index) {
    return support(index).current_surface_for_testing();
  }

  SurfaceManager& surface_manager() { return surface_manager_; }

  // Returns all the references where |surface_id| is the parent.
  const base::flat_set<SurfaceId>& GetChildReferences(
      const SurfaceId& surface_id) {
    return surface_manager().parent_to_child_refs_[surface_id];
  }

  // Returns true if there is a temporary reference for |surface_id|.
  bool HasTemporaryReference(const SurfaceId& surface_id) {
    return surface_manager().HasTemporaryReference(surface_id);
  }

  SurfaceDependencyTracker& dependency_tracker() {
    return *surface_manager_.dependency_tracker();
  }

  FakeExternalBeginFrameSource* begin_frame_source() {
    return begin_frame_source_.get();
  }

  // testing::Test:
  void SetUp() override {
    testing::Test::SetUp();

    begin_frame_source_ =
        base::MakeUnique<FakeExternalBeginFrameSource>(0.f, false);
    surface_manager_.SetDependencyTracker(
        base::MakeUnique<SurfaceDependencyTracker>(&surface_manager_,
                                                   begin_frame_source_.get()));
    surface_manager_.AddObserver(this);
    supports_.push_back(CompositorFrameSinkSupport::Create(
        &support_client_, &surface_manager_, kDisplayFrameSink, kIsRoot,
        kHandlesFrameSinkIdInvalidation, kNeedsSyncPoints));
    supports_.push_back(CompositorFrameSinkSupport::Create(
        &support_client_, &surface_manager_, kParentFrameSink, kIsChildRoot,
        kHandlesFrameSinkIdInvalidation, kNeedsSyncPoints));
    supports_.push_back(CompositorFrameSinkSupport::Create(
        &support_client_, &surface_manager_, kChildFrameSink1, kIsChildRoot,
        kHandlesFrameSinkIdInvalidation, kNeedsSyncPoints));
    supports_.push_back(CompositorFrameSinkSupport::Create(
        &support_client_, &surface_manager_, kChildFrameSink2, kIsChildRoot,
        kHandlesFrameSinkIdInvalidation, kNeedsSyncPoints));

    // Normally, the BeginFrameSource would be registered by the Display. We
    // register it here so that BeginFrames are received by the display support,
    // for use in the PassesOnBeginFrameAcks test. Other supports do not receive
    // BeginFrames, since the frame sink hierarchy is not set up in this test.
    surface_manager_.RegisterBeginFrameSource(begin_frame_source_.get(),
                                              kDisplayFrameSink);
  }

  void TearDown() override {
    surface_manager_.RemoveObserver(this);
    surface_manager_.SetDependencyTracker(nullptr);
    surface_manager_.UnregisterBeginFrameSource(begin_frame_source_.get());

    // SurfaceDependencyTracker depends on this BeginFrameSource and so it must
    // be destroyed AFTER the dependency tracker is destroyed.
    begin_frame_source_.reset();

    supports_.clear();

    damaged_surfaces_.clear();
  }

  bool IsSurfaceDamaged(const SurfaceId& surface_id) const {
    return damaged_surfaces_.count(surface_id) > 0;
  }

  // SurfaceObserver implementation:
  void OnSurfaceCreated(const SurfaceInfo& surface_info) override {}
  void OnSurfaceDamaged(const SurfaceId& surface_id, bool* changed) override {
    damaged_surfaces_.insert(surface_id);
  }

 protected:
  testing::NiceMock<MockCompositorFrameSinkSupportClient> support_client_;

 private:
  base::flat_set<SurfaceId> damaged_surfaces_;
  SurfaceManager surface_manager_;
  std::unique_ptr<FakeExternalBeginFrameSource> begin_frame_source_;
  std::vector<std::unique_ptr<CompositorFrameSinkSupport>> supports_;

  DISALLOW_COPY_AND_ASSIGN(SurfaceSynchronizationTest);
};

// The display root surface should have a surface reference from the top-level
// root added/removed when a CompositorFrame is submitted with a new SurfaceId.
TEST_F(SurfaceSynchronizationTest, RootSurfaceReceivesReferences) {
  const SurfaceId display_id_first = MakeSurfaceId(kDisplayFrameSink, 1);
  const SurfaceId display_id_second = MakeSurfaceId(kDisplayFrameSink, 2);

  // Submit a CompositorFrame for the first display root surface.
  display_support().SubmitCompositorFrame(display_id_first.local_surface_id(),
                                          MakeCompositorFrame());

  // A surface reference from the top-level root is added and there shouldn't be
  // a temporary reference.
  EXPECT_FALSE(HasTemporaryReference(display_id_first));
  EXPECT_THAT(GetChildReferences(surface_manager().GetRootSurfaceId()),
              UnorderedElementsAre(display_id_first));

  // Submit a CompositorFrame for the second display root surface.
  display_support().SubmitCompositorFrame(display_id_second.local_surface_id(),
                                          MakeCompositorFrame());

  // A surface reference from the top-level root to |display_id_second| should
  // be added and the reference to |display_root_first| removed.
  EXPECT_FALSE(HasTemporaryReference(display_id_second));
  EXPECT_THAT(GetChildReferences(surface_manager().GetRootSurfaceId()),
              UnorderedElementsAre(display_id_second));

  // Surface |display_id_first| is unreachable and should get deleted.
  EXPECT_EQ(nullptr, surface_manager().GetSurfaceForId(display_id_first));
}

// The parent Surface is blocked on |child_id1| and |child_id2|.
TEST_F(SurfaceSynchronizationTest, BlockedOnTwo) {
  const SurfaceId parent_id = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId child_id1 = MakeSurfaceId(kChildFrameSink1, 1);
  const SurfaceId child_id2 = MakeSurfaceId(kChildFrameSink2, 1);

  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame({child_id1, child_id2}, empty_surface_ids(),
                          TransferableResourceArray()));

  // parent_support is blocked on |child_id1| and |child_id2|.
  EXPECT_TRUE(dependency_tracker().has_deadline());
  EXPECT_FALSE(parent_surface()->HasActiveFrame());
  EXPECT_TRUE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id1, child_id2));

  // Submit a CompositorFrame without any dependencies to |child_id1|.
  // parent_support should now only be blocked on |child_id2|.
  child_support1().SubmitCompositorFrame(child_id1.local_surface_id(),
                                         MakeCompositorFrame());

  EXPECT_TRUE(dependency_tracker().has_deadline());
  EXPECT_FALSE(parent_surface()->HasActiveFrame());
  EXPECT_TRUE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id2));

  // Submit a CompositorFrame without any dependencies to |child_id2|.
  // parent_support should be activated.
  child_support2().SubmitCompositorFrame(child_id2.local_surface_id(),
                                         MakeCompositorFrame());

  EXPECT_FALSE(dependency_tracker().has_deadline());
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(), IsEmpty());
}

// The parent Surface is blocked on |child_id2| which is blocked on |child_id3|.
TEST_F(SurfaceSynchronizationTest, BlockedChain) {
  const SurfaceId parent_id = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId child_id1 = MakeSurfaceId(kChildFrameSink1, 1);
  const SurfaceId child_id2 = MakeSurfaceId(kChildFrameSink2, 1);

  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame({child_id1}, empty_surface_ids(),
                          TransferableResourceArray()));

  // parent_support is blocked on |child_id1|.
  EXPECT_TRUE(dependency_tracker().has_deadline());
  EXPECT_FALSE(parent_surface()->HasActiveFrame());
  EXPECT_TRUE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id1));
  // The parent should not report damage until it activates.
  EXPECT_FALSE(IsSurfaceDamaged(parent_id));

  child_support1().SubmitCompositorFrame(
      child_id1.local_surface_id(),
      MakeCompositorFrame({child_id2}, empty_surface_ids(),
                          TransferableResourceArray()));

  // child_support1 should now be blocked on |child_id2|.
  EXPECT_TRUE(dependency_tracker().has_deadline());
  EXPECT_FALSE(child_surface1()->HasActiveFrame());
  EXPECT_TRUE(child_surface1()->HasPendingFrame());
  EXPECT_THAT(child_surface1()->blocking_surfaces(),
              UnorderedElementsAre(child_id2));
  // The parent and child should not report damage until they activate.
  EXPECT_FALSE(IsSurfaceDamaged(parent_id));
  EXPECT_FALSE(IsSurfaceDamaged(child_id1));

  // The parent should still be blocked on |child_id1| because it's pending.
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id1));

  // Submit a CompositorFrame without any dependencies to |child_id2|.
  // parent_support should be activated.
  child_support2().SubmitCompositorFrame(
      child_id2.local_surface_id(),
      MakeCompositorFrame(empty_surface_ids(), empty_surface_ids(),
                          TransferableResourceArray()));

  EXPECT_FALSE(dependency_tracker().has_deadline());

  // child_surface1 should now be active.
  EXPECT_TRUE(child_surface1()->HasActiveFrame());
  EXPECT_FALSE(child_surface1()->HasPendingFrame());
  EXPECT_THAT(child_surface1()->blocking_surfaces(), IsEmpty());

  // parent_surface should now be active.
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(), IsEmpty());

  // All three surfaces |parent_id|, |child_id1|, and |child_id2| should
  // now report damage. This would trigger a new display frame.
  EXPECT_TRUE(IsSurfaceDamaged(parent_id));
  EXPECT_TRUE(IsSurfaceDamaged(child_id1));
  EXPECT_TRUE(IsSurfaceDamaged(child_id2));
}

// parent_surface and child_surface1 are blocked on |child_id2|.
TEST_F(SurfaceSynchronizationTest, TwoBlockedOnOne) {
  const SurfaceId parent_id = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId child_id1 = MakeSurfaceId(kChildFrameSink1, 1);
  const SurfaceId child_id2 = MakeSurfaceId(kChildFrameSink2, 1);

  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame({child_id2}, empty_surface_ids(),
                          TransferableResourceArray()));

  // parent_support is blocked on |child_id2|.
  EXPECT_TRUE(dependency_tracker().has_deadline());
  EXPECT_FALSE(parent_surface()->HasActiveFrame());
  EXPECT_TRUE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id2));

  // child_support1 should now be blocked on |child_id2|.
  child_support1().SubmitCompositorFrame(
      child_id1.local_surface_id(),
      MakeCompositorFrame({child_id2}, empty_surface_ids(),
                          TransferableResourceArray()));

  EXPECT_TRUE(dependency_tracker().has_deadline());
  EXPECT_FALSE(child_surface1()->HasActiveFrame());
  EXPECT_TRUE(child_surface1()->HasPendingFrame());
  EXPECT_THAT(child_surface1()->blocking_surfaces(),
              UnorderedElementsAre(child_id2));

  // The parent should still be blocked on |child_id2|.
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id2));

  // Submit a CompositorFrame without any dependencies to |child_id2|.
  // parent_support should be activated.
  child_support2().SubmitCompositorFrame(child_id2.local_surface_id(),
                                         MakeCompositorFrame());

  EXPECT_FALSE(dependency_tracker().has_deadline());

  // child_surface1 should now be active.
  EXPECT_TRUE(child_surface1()->HasActiveFrame());
  EXPECT_FALSE(child_surface1()->HasPendingFrame());
  EXPECT_THAT(child_surface1()->blocking_surfaces(), IsEmpty());

  // parent_surface should now be active.
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(), IsEmpty());
}

// parent_surface is blocked on |child_id1|, and child_surface2 is blocked on
// |child_id2| until the deadline hits.
TEST_F(SurfaceSynchronizationTest, DeadlineHits) {
  const SurfaceId parent_id = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId child_id1 = MakeSurfaceId(kChildFrameSink1, 1);
  const SurfaceId child_id2 = MakeSurfaceId(kChildFrameSink2, 1);

  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame({child_id1}, empty_surface_ids(),
                          TransferableResourceArray()));

  // parent_support is blocked on |child_id1|.
  EXPECT_TRUE(dependency_tracker().has_deadline());
  EXPECT_FALSE(parent_surface()->HasActiveFrame());
  EXPECT_TRUE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id1));

  child_support1().SubmitCompositorFrame(
      child_id1.local_surface_id(),
      MakeCompositorFrame({child_id2}, empty_surface_ids(),
                          TransferableResourceArray()));

  // child_support1 should now be blocked on |child_id2|.
  EXPECT_TRUE(dependency_tracker().has_deadline());
  EXPECT_FALSE(child_surface1()->HasActiveFrame());
  EXPECT_TRUE(child_surface1()->HasPendingFrame());
  EXPECT_THAT(child_surface1()->blocking_surfaces(),
              UnorderedElementsAre(child_id2));

  // The parent should still be blocked on |child_id1| because it's pending.
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id1));

  BeginFrameArgs args =
      CreateBeginFrameArgsForTesting(BEGINFRAME_FROM_HERE, 0, 1);

  for (int i = 0; i < 3; ++i) {
    begin_frame_source()->TestOnBeginFrame(args);
    // There is still a looming deadline! Eeek!
    EXPECT_TRUE(dependency_tracker().has_deadline());

    // parent_support is still blocked on |child_id1|.
    EXPECT_FALSE(parent_surface()->HasActiveFrame());
    EXPECT_TRUE(parent_surface()->HasPendingFrame());
    EXPECT_THAT(parent_surface()->blocking_surfaces(),
                UnorderedElementsAre(child_id1));

    // child_support1 is still blocked on |child_id2|.
    EXPECT_FALSE(child_surface1()->HasActiveFrame());
    EXPECT_TRUE(child_surface1()->HasPendingFrame());
    EXPECT_THAT(child_surface1()->blocking_surfaces(),
                UnorderedElementsAre(child_id2));
  }

  begin_frame_source()->TestOnBeginFrame(args);

  // The deadline has passed.
  EXPECT_FALSE(dependency_tracker().has_deadline());

  // parent_surface has been activated.
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(), IsEmpty());

  // child_surface1 has been activated.
  EXPECT_TRUE(child_surface1()->HasActiveFrame());
  EXPECT_FALSE(child_surface1()->HasPendingFrame());
  EXPECT_THAT(child_surface1()->blocking_surfaces(), IsEmpty());
}

// Verifies that the deadline does not reset if we submit CompositorFrames
// to new Surfaces with unresolved dependencies.
TEST_F(SurfaceSynchronizationTest, FramesSubmittedAfterDeadlineSet) {
  const SurfaceId arbitrary_id = MakeSurfaceId(kArbitraryFrameSink, 1);
  BeginFrameArgs args =
      CreateBeginFrameArgsForTesting(BEGINFRAME_FROM_HERE, 0, 1);
  for (int i = 0; i < 3; ++i) {
    LocalSurfaceId local_surface_id(1, base::UnguessableToken::Create());
    support(i).SubmitCompositorFrame(
        local_surface_id,
        MakeCompositorFrame({arbitrary_id}, empty_surface_ids(),
                            TransferableResourceArray()));
    // The deadline has been set.
    EXPECT_TRUE(dependency_tracker().has_deadline());

    // support(i) should be blocked on arbitrary_id.
    EXPECT_FALSE(surface(i)->HasActiveFrame());
    EXPECT_TRUE(surface(i)->HasPendingFrame());
    EXPECT_THAT(surface(i)->blocking_surfaces(),
                UnorderedElementsAre(arbitrary_id));

    // Issue a BeginFrame to get closer to the deadline.
    begin_frame_source()->TestOnBeginFrame(args);
  }

  // The deadline hits and all the Surfaces should activate.
  begin_frame_source()->TestOnBeginFrame(args);
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(surface(i)->HasActiveFrame());
    EXPECT_FALSE(surface(i)->HasPendingFrame());
    EXPECT_THAT(surface(i)->blocking_surfaces(), IsEmpty());
  }
}

// This test verifies at the Surface activates once a CompositorFrame is
// submitted that has no unresolved dependencies.
TEST_F(SurfaceSynchronizationTest, NewFrameOverridesOldDependencies) {
  const SurfaceId parent_id = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId arbitrary_id = MakeSurfaceId(kArbitraryFrameSink, 1);

  // Submit a CompositorFrame that depends on |arbitrary_id|.
  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame({arbitrary_id}, empty_surface_ids(),
                          TransferableResourceArray()));

  // Verify that the CompositorFrame is blocked on |arbitrary_id|.
  EXPECT_FALSE(parent_surface()->HasActiveFrame());
  EXPECT_TRUE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(arbitrary_id));

  // Submit a CompositorFrame that has no dependencies.
  parent_support().SubmitCompositorFrame(parent_id.local_surface_id(),
                                         MakeCompositorFrame());

  // Verify that the CompositorFrame has been activated.
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(), IsEmpty());
}

// This test verifies that a pending CompositorFrame does not affect surface
// references. A new surface from a child will continue to exist as a temporary
// reference until the parent's frame activates.
TEST_F(SurfaceSynchronizationTest, OnlyActiveFramesAffectSurfaceReferences) {
  const SurfaceId parent_id = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId child_id1 = MakeSurfaceId(kChildFrameSink1, 1);
  const SurfaceId child_id2 = MakeSurfaceId(kChildFrameSink2, 1);

  // child_support1 submits a CompositorFrame without any dependencies.
  // DidReceiveCompositorFrameAck should call on immediate activation.
  EXPECT_CALL(support_client_, DidReceiveCompositorFrameAck(_)).Times(1);
  child_support1().SubmitCompositorFrame(child_id1.local_surface_id(),
                                         MakeCompositorFrame());
  testing::Mock::VerifyAndClearExpectations(&support_client_);

  // Verify that the child surface is not blocked.
  EXPECT_TRUE(child_surface1()->HasActiveFrame());
  EXPECT_FALSE(child_surface1()->HasPendingFrame());
  EXPECT_THAT(child_surface1()->blocking_surfaces(), IsEmpty());

  // Verify that there's a temporary reference for |child_id1|.
  EXPECT_TRUE(HasTemporaryReference(child_id1));

  // parent_support submits a CompositorFrame that depends on |child_id1|
  // (which is already active) and |child_id2|. Thus, the parent should not
  // activate immediately. DidReceiveCompositorFrameAck should not be called
  // immediately because the parent CompositorFrame is also blocked on
  // |child_id2|.
  EXPECT_CALL(support_client_, DidReceiveCompositorFrameAck(_)).Times(0);
  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame({child_id2}, {child_id1},
                          TransferableResourceArray()));
  EXPECT_FALSE(parent_surface()->HasActiveFrame());
  EXPECT_TRUE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id2));
  EXPECT_THAT(GetChildReferences(parent_id), IsEmpty());
  testing::Mock::VerifyAndClearExpectations(&support_client_);

  // Verify that there's a temporary reference for |child_id1| that still
  // exists.
  EXPECT_TRUE(HasTemporaryReference(child_id1));

  // child_support2 submits a CompositorFrame without any dependencies.
  // Both the child and the parent should immediately ACK CompositorFrames
  // on activation.
  EXPECT_CALL(support_client_, DidReceiveCompositorFrameAck(_)).Times(2);
  child_support2().SubmitCompositorFrame(child_id2.local_surface_id(),
                                         MakeCompositorFrame());
  testing::Mock::VerifyAndClearExpectations(&support_client_);

  // Verify that the child surface is not blocked.
  EXPECT_TRUE(child_surface1()->HasActiveFrame());
  EXPECT_FALSE(child_surface1()->HasPendingFrame());
  EXPECT_THAT(child_surface1()->blocking_surfaces(), IsEmpty());

  // Verify that the parent surface's CompositorFrame has activated and that the
  // temporary reference has been replaced by a permanent one.
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(), IsEmpty());
  EXPECT_FALSE(HasTemporaryReference(child_id1));
  EXPECT_THAT(GetChildReferences(parent_id), UnorderedElementsAre(child_id1));
}

// This test verifies that we do not double count returned resources when a
// CompositorFrame starts out as pending, then becomes active, and then is
// replaced with another active CompositorFrame.
TEST_F(SurfaceSynchronizationTest, ResourcesOnlyReturnedOnce) {
  const SurfaceId parent_id = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId child_id = MakeSurfaceId(kChildFrameSink1, 1);

  // The parent submits a CompositorFrame that depends on |child_id| before the
  // child submits a CompositorFrame. The CompositorFrame also has resources in
  // its resource list.
  TransferableResource resource;
  resource.id = 1337;
  resource.format = ALPHA_8;
  resource.filter = 1234;
  resource.size = gfx::Size(1234, 5678);
  TransferableResourceArray resource_list = {resource};
  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame({child_id}, empty_surface_ids(), resource_list));

  // Verify that the CompositorFrame is blocked on |child_id|.
  EXPECT_FALSE(parent_surface()->HasActiveFrame());
  EXPECT_TRUE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id));

  child_support1().SubmitCompositorFrame(
      child_id.local_surface_id(),
      MakeCompositorFrame(empty_surface_ids(), empty_surface_ids(),
                          TransferableResourceArray()));

  // Verify that the child CompositorFrame activates immediately.
  EXPECT_TRUE(child_surface1()->HasActiveFrame());
  EXPECT_FALSE(child_surface1()->HasPendingFrame());
  EXPECT_THAT(child_surface1()->blocking_surfaces(), IsEmpty());

  // Verify that the parent has activated.
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(), IsEmpty());

  ReturnedResourceArray returned_resources = {resource.ToReturnedResource()};
  EXPECT_CALL(support_client_,
              DidReceiveCompositorFrameAck(returned_resources));

  // The parent submits a CompositorFrame without any dependencies. That frame
  // should activate immediately, replacing the earlier frame. The resource from
  // the earlier frame should be returned to the client.
  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame({empty_surface_ids()}, {empty_surface_ids()},
                          TransferableResourceArray()));
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(), IsEmpty());
}

// The parent Surface is blocked on |child_id2| which is blocked on |child_id3|.
// child_support1 evicts its blocked Surface. The parent surface should
// activate.
TEST_F(SurfaceSynchronizationTest, EvictSurfaceWithPendingFrame) {
  const SurfaceId parent_id1 = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId child_id1 = MakeSurfaceId(kChildFrameSink1, 1);
  const SurfaceId child_id2 = MakeSurfaceId(kChildFrameSink2, 1);

  // Submit a CompositorFrame that depends on |child_id1|.
  parent_support().SubmitCompositorFrame(
      parent_id1.local_surface_id(),
      MakeCompositorFrame({child_id1}, empty_surface_ids(),
                          TransferableResourceArray()));

  // Verify that the CompositorFrame is blocked on |child_id1|.
  EXPECT_FALSE(parent_surface()->HasActiveFrame());
  EXPECT_TRUE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id1));

  // Submit a CompositorFrame that depends on |child_id2|.
  child_support1().SubmitCompositorFrame(
      child_id1.local_surface_id(),
      MakeCompositorFrame({child_id2}, empty_surface_ids(),
                          TransferableResourceArray()));

  // Verify that the CompositorFrame is blocked on |child_id2|.
  EXPECT_FALSE(child_surface1()->HasActiveFrame());
  EXPECT_TRUE(child_surface1()->HasPendingFrame());
  EXPECT_THAT(child_surface1()->blocking_surfaces(),
              UnorderedElementsAre(child_id2));

  // Evict child_support1's current Surface.
  // TODO(fsamuel): EvictCurrentSurface => EvictCurrentSurface.
  child_support1().EvictCurrentSurface();

  // The parent Surface should immediately activate.
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(), IsEmpty());
  EXPECT_FALSE(dependency_tracker().has_deadline());
}

// This test verifies that if a surface has both a pending and active
// CompositorFrame and the pending CompositorFrame activates, replacing the
// existing active CompositorFrame, then the surface reference hierarchy will be
// updated allowing garbage collection of surfaces that are no longer
// referenced.
TEST_F(SurfaceSynchronizationTest, DropStaleReferencesAfterActivation) {
  const SurfaceId parent_id = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId child_id1 = MakeSurfaceId(kChildFrameSink1, 1);
  const SurfaceId child_id2 = MakeSurfaceId(kChildFrameSink2, 1);

  // The parent submits a CompositorFrame that depends on |child_id1| before the
  // child submits a CompositorFrame.
  EXPECT_CALL(support_client_, DidReceiveCompositorFrameAck(_)).Times(0);
  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame({child_id1}, empty_surface_ids(),
                          TransferableResourceArray()));

  // Verify that the CompositorFrame is blocked on |child_id|.
  EXPECT_FALSE(parent_surface()->HasActiveFrame());
  EXPECT_TRUE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id1));
  testing::Mock::VerifyAndClearExpectations(&support_client_);

  // Verify that no references are added while the CompositorFrame is pending.
  EXPECT_THAT(GetChildReferences(parent_id), IsEmpty());

  // DidReceiveCompositorFrameAck should get called twice: once for the child
  // and once for the now active parent CompositorFrame.
  EXPECT_CALL(support_client_, DidReceiveCompositorFrameAck(_)).Times(2);
  child_support1().SubmitCompositorFrame(child_id1.local_surface_id(),
                                         MakeCompositorFrame());
  testing::Mock::VerifyAndClearExpectations(&support_client_);

  // Verify that the child CompositorFrame activates immediately.
  EXPECT_TRUE(child_surface1()->HasActiveFrame());
  EXPECT_FALSE(child_surface1()->HasPendingFrame());
  EXPECT_THAT(child_surface1()->blocking_surfaces(), IsEmpty());

  // Verify that the parent Surface has activated.
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(), IsEmpty());

  // Submit a new parent CompositorFrame to add a reference.
  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame(empty_surface_ids(), {child_id1},
                          TransferableResourceArray()));

  // Verify that the parent Surface has activated.
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(), IsEmpty());

  // Verify that there is no temporary reference for the child and that
  // the reference from the parent to the child still exists.
  EXPECT_FALSE(HasTemporaryReference(child_id1));
  EXPECT_THAT(GetChildReferences(parent_id), UnorderedElementsAre(child_id1));

  // The parent submits another CompositorFrame that depends on |child_id2|.
  // Submitting a pending CompositorFrame will not trigger a CompositorFrameAck.
  EXPECT_CALL(support_client_, DidReceiveCompositorFrameAck(_)).Times(0);
  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame({child_id2}, empty_surface_ids(),
                          TransferableResourceArray()));
  testing::Mock::VerifyAndClearExpectations(&support_client_);

  // The parent surface should now have both a pending and activate
  // CompositorFrame. Verify that the set of child references from
  // |parent_id| are only from the active CompositorFrame.
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_TRUE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(),
              UnorderedElementsAre(child_id2));
  EXPECT_THAT(GetChildReferences(parent_id), UnorderedElementsAre(child_id1));

  child_support2().SubmitCompositorFrame(child_id2.local_surface_id(),
                                         MakeCompositorFrame());

  // Verify that the parent Surface has activated and no longer has a pending
  // CompositorFrame. Also verify that |child_id1| is no longer a child
  // reference of |parent_id|.
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_THAT(parent_surface()->blocking_surfaces(), IsEmpty());
  // The parent will not immediately refer to the child until it submits a new
  // CompositorFrame with the reference.
  EXPECT_THAT(GetChildReferences(parent_id), IsEmpty());
}

// Checks whether the latency info are moved to the new surface from the old
// one when LocalSurfaceId changes. No frame has unresolved dependencies.
TEST_F(SurfaceSynchronizationTest,
       LatencyInfoCarriedOverOnResize_NoUnresolvedDependencies) {
  const SurfaceId parent_id1 = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId parent_id2 = MakeSurfaceId(kParentFrameSink, 2);
  const ui::LatencyComponentType latency_type1 =
      ui::BROWSER_SNAPSHOT_FRAME_NUMBER_COMPONENT;
  const int64_t latency_id1 = 234;
  const int64_t latency_sequence_number1 = 5645432;
  const ui::LatencyComponentType latency_type2 = ui::TAB_SHOW_COMPONENT;
  const int64_t latency_id2 = 31434351;
  const int64_t latency_sequence_number2 = 663788;

  // Submit a frame with latency info
  ui::LatencyInfo info;
  info.AddLatencyNumber(latency_type1, latency_id1, latency_sequence_number1);

  CompositorFrame frame = MakeCompositorFrame();
  frame.metadata.latency_info.push_back(info);

  parent_support().SubmitCompositorFrame(parent_id1.local_surface_id(),
                                         std::move(frame));

  // Verify that the old surface has an active frame and no pending frame.
  Surface* old_surface = surface_manager().GetSurfaceForId(parent_id1);
  ASSERT_NE(nullptr, old_surface);
  EXPECT_TRUE(old_surface->HasActiveFrame());
  EXPECT_FALSE(old_surface->HasPendingFrame());

  // Submit another frame with some other latency info and a different
  // LocalSurfaceId.
  ui::LatencyInfo info2;
  info2.AddLatencyNumber(latency_type2, latency_id2, latency_sequence_number2);

  CompositorFrame frame2 = MakeCompositorFrame();
  frame2.metadata.latency_info.push_back(info2);

  parent_support().SubmitCompositorFrame(parent_id2.local_surface_id(),
                                         std::move(frame2));

  // Verify that the new surface has an active frame and no pending frames.
  Surface* surface = surface_manager().GetSurfaceForId(parent_id2);
  ASSERT_NE(nullptr, surface);
  EXPECT_TRUE(surface->HasActiveFrame());
  EXPECT_FALSE(surface->HasPendingFrame());

  // Verify that the new surface has both latency info elements.
  std::vector<ui::LatencyInfo> info_list;
  surface->TakeLatencyInfo(&info_list);
  EXPECT_EQ(2u, info_list.size());

  ui::LatencyInfo aggregated_latency_info = info_list[0];
  aggregated_latency_info.AddNewLatencyFrom(info_list[1]);

  // Two components are the original ones, and the third one is
  // DISPLAY_COMPOSITOR_RECEIVED_FRAME_COMPONENT, logged on compositor frame
  // submit.
  EXPECT_EQ(3u, aggregated_latency_info.latency_components().size());

  ui::LatencyInfo::LatencyComponent comp1;
  EXPECT_TRUE(
      aggregated_latency_info.FindLatency(latency_type1, latency_id1, &comp1));
  EXPECT_EQ(latency_sequence_number1, comp1.sequence_number);
  EXPECT_TRUE(
      aggregated_latency_info.FindLatency(latency_type2, latency_id2, nullptr));
  EXPECT_TRUE(aggregated_latency_info.FindLatency(
      ui::DISPLAY_COMPOSITOR_RECEIVED_FRAME_COMPONENT, nullptr));
}

// Checks whether the latency info are moved to the new surface from the old
// one when LocalSurfaceId changes. Old surface has unresolved dependencies.
TEST_F(SurfaceSynchronizationTest,
       LatencyInfoCarriedOverOnResize_OldSurfaceHasPendingAndActiveFrame) {
  const SurfaceId parent_id1 = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId parent_id2 = MakeSurfaceId(kParentFrameSink, 2);
  const SurfaceId child_id = MakeSurfaceId(kChildFrameSink1, 1);

  const ui::LatencyComponentType latency_type1 =
      ui::BROWSER_SNAPSHOT_FRAME_NUMBER_COMPONENT;
  const int64_t latency_id1 = 234;
  const int64_t latency_sequence_number1 = 5645432;
  const ui::LatencyComponentType latency_type2 = ui::TAB_SHOW_COMPONENT;
  const int64_t latency_id2 = 31434351;
  const int64_t latency_sequence_number2 = 663788;

  // Submit a frame with no unresolved dependecy.
  ui::LatencyInfo info;
  info.AddLatencyNumber(latency_type1, latency_id1, latency_sequence_number1);

  CompositorFrame frame = MakeCompositorFrame();
  frame.metadata.latency_info.push_back(info);

  parent_support().SubmitCompositorFrame(parent_id1.local_surface_id(),
                                         std::move(frame));

  // Submit a frame with unresolved dependencies.
  ui::LatencyInfo info2;
  info2.AddLatencyNumber(latency_type2, latency_id2, latency_sequence_number2);

  CompositorFrame frame2 = MakeCompositorFrame({child_id}, empty_surface_ids(),
                                               TransferableResourceArray());
  frame2.metadata.latency_info.push_back(info2);

  parent_support().SubmitCompositorFrame(parent_id1.local_surface_id(),
                                         std::move(frame2));

  // Verify that the old surface has both an active and a pending frame.
  Surface* old_surface = surface_manager().GetSurfaceForId(parent_id1);
  ASSERT_NE(nullptr, old_surface);
  EXPECT_TRUE(old_surface->HasActiveFrame());
  EXPECT_TRUE(old_surface->HasPendingFrame());

  // Submit a frame with a new local surface id.
  parent_support().SubmitCompositorFrame(parent_id2.local_surface_id(),
                                         MakeCompositorFrame());

  // Verify that the new surface has an active frame only.
  Surface* surface = surface_manager().GetSurfaceForId(parent_id2);
  ASSERT_NE(nullptr, surface);
  EXPECT_TRUE(surface->HasActiveFrame());
  EXPECT_FALSE(surface->HasPendingFrame());

  // Verify that the new surface has latency info from both active and pending
  // frame of the old surface.
  std::vector<ui::LatencyInfo> info_list;
  surface->TakeLatencyInfo(&info_list);
  EXPECT_EQ(2u, info_list.size());

  ui::LatencyInfo aggregated_latency_info = info_list[0];
  aggregated_latency_info.AddNewLatencyFrom(info_list[1]);

  // Two components are the original ones, and the third one is
  // DISPLAY_COMPOSITOR_RECEIVED_FRAME_COMPONENT, logged on compositor frame
  // submit.
  EXPECT_EQ(3u, aggregated_latency_info.latency_components().size());

  ui::LatencyInfo::LatencyComponent comp1;
  EXPECT_TRUE(
      aggregated_latency_info.FindLatency(latency_type1, latency_id1, &comp1));
  EXPECT_EQ(latency_sequence_number1, comp1.sequence_number);
  EXPECT_TRUE(
      aggregated_latency_info.FindLatency(latency_type2, latency_id2, nullptr));
  EXPECT_TRUE(aggregated_latency_info.FindLatency(
      ui::DISPLAY_COMPOSITOR_RECEIVED_FRAME_COMPONENT, nullptr));
}

// Checks whether the latency info are moved to the new surface from the old
// one when LocalSurfaceId changes. The new surface has unresolved dependencies.
TEST_F(SurfaceSynchronizationTest,
       LatencyInfoCarriedOverOnResize_NewSurfaceHasPendingFrame) {
  const SurfaceId parent_id1 = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId parent_id2 = MakeSurfaceId(kParentFrameSink, 2);
  const SurfaceId child_id = MakeSurfaceId(kChildFrameSink1, 1);

  const ui::LatencyComponentType latency_type1 =
      ui::BROWSER_SNAPSHOT_FRAME_NUMBER_COMPONENT;
  const int64_t latency_id1 = 234;
  const int64_t latency_sequence_number1 = 5645432;
  const ui::LatencyComponentType latency_type2 = ui::TAB_SHOW_COMPONENT;
  const int64_t latency_id2 = 31434351;
  const int64_t latency_sequence_number2 = 663788;

  // Submit a frame with no unresolved dependencies.
  ui::LatencyInfo info;
  info.AddLatencyNumber(latency_type1, latency_id1, latency_sequence_number1);

  CompositorFrame frame = MakeCompositorFrame();
  frame.metadata.latency_info.push_back(info);

  parent_support().SubmitCompositorFrame(parent_id1.local_surface_id(),
                                         std::move(frame));

  // Verify that the old surface has an active frame only.
  Surface* old_surface = surface_manager().GetSurfaceForId(parent_id1);
  ASSERT_NE(nullptr, old_surface);
  EXPECT_TRUE(old_surface->HasActiveFrame());
  EXPECT_FALSE(old_surface->HasPendingFrame());

  // Submit a frame with a new local surface id and with unresolved
  // dependencies.
  ui::LatencyInfo info2;
  info2.AddLatencyNumber(latency_type2, latency_id2, latency_sequence_number2);

  CompositorFrame frame2 = MakeCompositorFrame({child_id}, empty_surface_ids(),
                                               TransferableResourceArray());
  frame2.metadata.latency_info.push_back(info2);

  parent_support().SubmitCompositorFrame(parent_id2.local_surface_id(),
                                         std::move(frame2));

  // Verify that the new surface has a pending frame and no active frame.
  Surface* surface = surface_manager().GetSurfaceForId(parent_id2);
  ASSERT_NE(nullptr, surface);
  EXPECT_TRUE(surface->HasPendingFrame());
  EXPECT_FALSE(surface->HasActiveFrame());

  // Resolve the dependencies. The frame in parent's surface must become active.
  child_support1().SubmitCompositorFrame(child_id.local_surface_id(),
                                         MakeCompositorFrame());
  EXPECT_FALSE(surface->HasPendingFrame());
  EXPECT_TRUE(surface->HasActiveFrame());

  // Both latency info elements must exist in the now-activated frame of the
  // new surface.
  std::vector<ui::LatencyInfo> info_list;
  surface->TakeLatencyInfo(&info_list);
  EXPECT_EQ(2u, info_list.size());

  ui::LatencyInfo aggregated_latency_info = info_list[0];
  aggregated_latency_info.AddNewLatencyFrom(info_list[1]);

  // Two components are the original ones, and the third one is
  // DISPLAY_COMPOSITOR_RECEIVED_FRAME_COMPONENT, logged on compositor frame
  // submit.
  EXPECT_EQ(3u, aggregated_latency_info.latency_components().size());

  ui::LatencyInfo::LatencyComponent comp1;
  EXPECT_TRUE(
      aggregated_latency_info.FindLatency(latency_type1, latency_id1, &comp1));
  EXPECT_EQ(latency_sequence_number1, comp1.sequence_number);
  EXPECT_TRUE(
      aggregated_latency_info.FindLatency(latency_type2, latency_id2, nullptr));
  EXPECT_TRUE(aggregated_latency_info.FindLatency(
      ui::DISPLAY_COMPOSITOR_RECEIVED_FRAME_COMPONENT, nullptr));
}

TEST_F(SurfaceSynchronizationTest, PassesOnBeginFrameAcks) {
  const SurfaceId display_id = MakeSurfaceId(kDisplayFrameSink, 1);

  // Request BeginFrames.
  display_support().SetNeedsBeginFrame(true);

  // Issue a BeginFrame.
  BeginFrameArgs args =
      CreateBeginFrameArgsForTesting(BEGINFRAME_FROM_HERE, 0, 1);
  begin_frame_source()->TestOnBeginFrame(args);

  // Check that the support forwards a DidNotProduceFrame ack to the
  // BeginFrameSource.
  BeginFrameAck ack(0, 1, 1, false);
  display_support().DidNotProduceFrame(ack);
  EXPECT_EQ(ack, begin_frame_source()->LastAckForObserver(&display_support()));

  // Issue another BeginFrame.
  args = CreateBeginFrameArgsForTesting(BEGINFRAME_FROM_HERE, 0, 2);
  begin_frame_source()->TestOnBeginFrame(args);

  // Check that the support forwards the BeginFrameAck attached
  // to a CompositorFrame to the BeginFrameSource.
  BeginFrameAck ack2(0, 2, 2, true);
  CompositorFrame frame = MakeCompositorFrame();
  frame.metadata.begin_frame_ack = ack2;
  display_support().SubmitCompositorFrame(display_id.local_surface_id(),
                                          std::move(frame));
  EXPECT_EQ(ack2, begin_frame_source()->LastAckForObserver(&display_support()));
}

// Checks that resources and ack are sent together if possible.
TEST_F(SurfaceSynchronizationTest, ReturnResourcesWithAck) {
  const SurfaceId parent_id = MakeSurfaceId(kParentFrameSink, 1);
  TransferableResource resource;
  resource.id = 1234;
  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame(empty_surface_ids(), empty_surface_ids(),
                          {resource}));
  ReturnedResourceArray returned_resources;
  TransferableResource::ReturnResources({resource}, &returned_resources);
  EXPECT_CALL(support_client_, ReclaimResources(_)).Times(0);
  EXPECT_CALL(support_client_,
              DidReceiveCompositorFrameAck(Eq(returned_resources)));
  parent_support().SubmitCompositorFrame(parent_id.local_surface_id(),
                                         MakeCompositorFrame());
}

// Verifies that if a surface is marked destroyed and a new frame arrives for
// it, it will be recovered.
TEST_F(SurfaceSynchronizationTest, SurfaceResurrection) {
  const SurfaceId parent_id = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId child_id = MakeSurfaceId(kChildFrameSink1, 3);

  // Create the child surface by submitting a frame to it.
  EXPECT_EQ(nullptr, surface_manager().GetSurfaceForId(child_id));
  child_support1().SubmitCompositorFrame(child_id.local_surface_id(),
                                         MakeCompositorFrame());

  // Verify that the child surface is created.
  Surface* surface = surface_manager().GetSurfaceForId(child_id);
  EXPECT_NE(nullptr, surface);

  // Add a reference from the parent to the child.
  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame({child_id}, {child_id}, TransferableResourceArray()));

  // Attempt to destroy the child surface. The surface must still exist since
  // the parent needs it but it will be marked as destroyed.
  child_support1().EvictCurrentSurface();
  surface = surface_manager().GetSurfaceForId(child_id);
  EXPECT_NE(nullptr, surface);
  EXPECT_TRUE(surface->destroyed());

  // Child submits another frame to the same local surface id that is marked
  // destroyed.
  child_support1().SubmitCompositorFrame(child_id.local_surface_id(),
                                         MakeCompositorFrame());

  // Verify that the surface that was marked destroyed is recovered and is being
  // used again.
  Surface* surface2 = surface_manager().GetSurfaceForId(child_id);
  EXPECT_EQ(surface, surface2);
  EXPECT_FALSE(surface2->destroyed());
}

// Verifies that if a LocalSurfaceId belonged to a surface that doesn't exist
// anymore, it can still be reused for new surfaces.
TEST_F(SurfaceSynchronizationTest, LocalSurfaceIdIsReusable) {
  const SurfaceId parent_id = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId child_id = MakeSurfaceId(kChildFrameSink1, 3);

  // Submit the first frame. Creates the surface.
  child_support1().SubmitCompositorFrame(child_id.local_surface_id(),
                                         MakeCompositorFrame());
  EXPECT_NE(nullptr, surface_manager().GetSurfaceForId(child_id));

  // Add a reference from parent.
  parent_support().SubmitCompositorFrame(
      parent_id.local_surface_id(),
      MakeCompositorFrame({child_id}, {child_id}, TransferableResourceArray()));

  // Remove the reference from parant. This allows us to destroy the surface.
  parent_support().SubmitCompositorFrame(parent_id.local_surface_id(),
                                         MakeCompositorFrame());

  // Destroy the surface.
  child_support1().EvictCurrentSurface();
  EXPECT_EQ(nullptr, surface_manager().GetSurfaceForId(child_id));

  // Submit another frame with the same local surface id. This should work fine
  // and a new surface must be created.
  child_support1().SubmitCompositorFrame(child_id.local_surface_id(),
                                         MakeCompositorFrame());
  EXPECT_NE(nullptr, surface_manager().GetSurfaceForId(child_id));
}

// This test verifies that a crash does not occur if garbage collection is
// triggered during surface dependency resolution. This test triggers garbage
// collection during surface resolution, by causing an activation to remove
// a surface subtree from the root. Both the old subtree and the new
// activated subtree refer to the same dependency. The old subtree was activated
// by deadline, and the new subtree was activated by a dependency finally
// resolving.
TEST_F(SurfaceSynchronizationTest, DependencyTrackingGarbageCollection) {
  const SurfaceId display_id = MakeSurfaceId(kDisplayFrameSink, 1);
  const SurfaceId parent_id1 = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId parent_id2 = MakeSurfaceId(kParentFrameSink, 2);
  const SurfaceId child_id = MakeSurfaceId(kChildFrameSink1, 1);

  parent_support().SubmitCompositorFrame(
      parent_id1.local_surface_id(),
      MakeCompositorFrame({child_id}, empty_surface_ids(),
                          TransferableResourceArray()));
  display_support().SubmitCompositorFrame(
      display_id.local_surface_id(),
      MakeCompositorFrame({parent_id1}, empty_surface_ids(),
                          TransferableResourceArray()));

  EXPECT_TRUE(dependency_tracker().has_deadline());

  BeginFrameArgs args =
      CreateBeginFrameArgsForTesting(BEGINFRAME_FROM_HERE, 0, 1);

  // Advance BeginFrames to trigger a deadline.
  for (int i = 0; i < 3; ++i) {
    begin_frame_source()->TestOnBeginFrame(args);
    EXPECT_TRUE(dependency_tracker().has_deadline());
  }
  begin_frame_source()->TestOnBeginFrame(args);
  EXPECT_FALSE(dependency_tracker().has_deadline());

  EXPECT_TRUE(display_surface()->HasActiveFrame());
  EXPECT_FALSE(display_surface()->HasPendingFrame());
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());

  parent_support().SubmitCompositorFrame(
      parent_id2.local_surface_id(),
      MakeCompositorFrame({child_id}, empty_surface_ids(),
                          TransferableResourceArray()));
  display_support().SubmitCompositorFrame(
      display_id.local_surface_id(),
      MakeCompositorFrame({parent_id2}, empty_surface_ids(),
                          TransferableResourceArray()));

  // The display surface now has two CompositorFrames. One that is pending,
  // indirectly blocked on child_id and one that is active, also indirectly
  // referring to child_id, but activated due to the deadline above.
  EXPECT_TRUE(display_surface()->HasActiveFrame());
  EXPECT_TRUE(display_surface()->HasPendingFrame());

  // Submitting a CompositorFrame will trigger garbage collection of the
  // |parent_id1| subtree. This should not crash.
  child_support1().SubmitCompositorFrame(child_id.local_surface_id(),
                                         MakeCompositorFrame());
}

// This test verifies that a crash does not occur if garbage collection is
// triggered when a deadline forces frame activation. This test triggers garbage
// collection during deadline activation by causing the activation of a display
// frame to replace a previously activated display frame that was referring to
// a now-unreachable surface subtree. That subtree gets garbage collected during
// deadline activation. SurfaceDependencyTracker is also tracking a surface
// from that subtree due to an unresolved dependency. This test verifies that
// this dependency resolution does not crash.
TEST_F(SurfaceSynchronizationTest, GarbageCollectionOnDeadline) {
  const SurfaceId display_id = MakeSurfaceId(kDisplayFrameSink, 1);
  const SurfaceId parent_id1 = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId parent_id2 = MakeSurfaceId(kParentFrameSink, 2);
  const SurfaceId child_id = MakeSurfaceId(kChildFrameSink1, 1);

  // |parent_id1| is blocked on |child_id|.
  parent_support().SubmitCompositorFrame(
      parent_id1.local_surface_id(),
      MakeCompositorFrame({child_id}, empty_surface_ids(),
                          TransferableResourceArray()));

  display_support().SubmitCompositorFrame(
      display_id.local_surface_id(),
      MakeCompositorFrame({parent_id1}, {parent_id1},
                          TransferableResourceArray()));

  EXPECT_TRUE(dependency_tracker().has_deadline());
  EXPECT_TRUE(display_surface()->HasPendingFrame());
  EXPECT_FALSE(display_surface()->HasActiveFrame());

  // Advance BeginFrames to trigger a deadline. This activates the
  // CompositorFrame submitted above.
  BeginFrameArgs args =
      CreateBeginFrameArgsForTesting(BEGINFRAME_FROM_HERE, 0, 1);
  for (int i = 0; i < 3; ++i) {
    begin_frame_source()->TestOnBeginFrame(args);
    EXPECT_TRUE(dependency_tracker().has_deadline());
  }
  begin_frame_source()->TestOnBeginFrame(args);
  EXPECT_FALSE(dependency_tracker().has_deadline());
  EXPECT_FALSE(display_surface()->HasPendingFrame());
  EXPECT_TRUE(display_surface()->HasActiveFrame());

  // By submitting a display CompositorFrame, and replacing the parent's
  // CompositorFrame with another surface ID, parent_id1 becomes unreachable and
  // a candidate for garbage collection.
  display_support().SubmitCompositorFrame(
      display_id.local_surface_id(),
      MakeCompositorFrame({parent_id2}, empty_surface_ids(),
                          TransferableResourceArray()));

  // Now |parent_id1| is only kept alive by the active |display_id| frame.
  parent_support().SubmitCompositorFrame(
      parent_id2.local_surface_id(),
      MakeCompositorFrame({child_id}, empty_surface_ids(),
                          TransferableResourceArray()));

  // SurfaceDependencyTracker should now be tracking |display_id|, |parent_id1|
  // and |parent_id2|. By activating the pending |display_id| frame by deadline,
  // |parent_id1| becomes unreachable and is garbage collected while
  // SurfaceDependencyTracker is in the process of activating surfaces. This
  // should not cause a crash or use-after-free.
  for (int i = 0; i < 3; ++i) {
    begin_frame_source()->TestOnBeginFrame(args);
    EXPECT_TRUE(dependency_tracker().has_deadline());
  }
  begin_frame_source()->TestOnBeginFrame(args);
  EXPECT_FALSE(dependency_tracker().has_deadline());
}

// This test verifies that a CompositorFrame will only blocked on embedded
// surfaces but not on other retained surface IDs in the CompositorFrame.
TEST_F(SurfaceSynchronizationTest, OnlyBlockOnEmbeddedSurfaces) {
  const SurfaceId display_id = MakeSurfaceId(kDisplayFrameSink, 1);
  const SurfaceId parent_id1 = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId parent_id2 = MakeSurfaceId(kParentFrameSink, 2);

  // Submitting a CompositorFrame with |parent_id2| so that the display
  // CompositorFrame can hold a reference to it.
  parent_support().SubmitCompositorFrame(parent_id2.local_surface_id(),
                                         MakeCompositorFrame());

  display_support().SubmitCompositorFrame(
      display_id.local_surface_id(),
      MakeCompositorFrame({parent_id1}, {parent_id2},
                          TransferableResourceArray()));

  EXPECT_TRUE(display_surface()->HasPendingFrame());
  EXPECT_FALSE(display_surface()->HasActiveFrame());
  EXPECT_TRUE(dependency_tracker().has_deadline());

  // Verify that the display CompositorFrame will only block on |parent_id1| but
  // not |parent_id2|.
  EXPECT_THAT(display_surface()->blocking_surfaces(),
              UnorderedElementsAre(parent_id1));
  // Verify that the display surface holds no references while its
  // CompositorFrame is pending.
  EXPECT_THAT(GetChildReferences(display_id), IsEmpty());

  // Submitting a CompositorFrame with |parent_id1| should unblock the display
  // CompositorFrame.
  parent_support().SubmitCompositorFrame(parent_id1.local_surface_id(),
                                         MakeCompositorFrame());

  EXPECT_FALSE(dependency_tracker().has_deadline());
  EXPECT_FALSE(display_surface()->HasPendingFrame());
  EXPECT_TRUE(display_surface()->HasActiveFrame());
  EXPECT_THAT(display_surface()->blocking_surfaces(), IsEmpty());
}

// This test verifies that a late arriving CompositorFrame activates immediately
// and does not trigger a new deadline.
TEST_F(SurfaceSynchronizationTest, LateArrivingDependency) {
  const SurfaceId display_id = MakeSurfaceId(kDisplayFrameSink, 1);
  const SurfaceId parent_id1 = MakeSurfaceId(kParentFrameSink, 1);
  const SurfaceId child_id1 = MakeSurfaceId(kChildFrameSink1, 1);

  display_support().SubmitCompositorFrame(
      display_id.local_surface_id(),
      MakeCompositorFrame({parent_id1}, empty_surface_ids(),
                          TransferableResourceArray()));

  EXPECT_TRUE(display_surface()->HasPendingFrame());
  EXPECT_FALSE(display_surface()->HasActiveFrame());
  EXPECT_TRUE(dependency_tracker().has_deadline());

  // Advance BeginFrames to trigger a deadline. This activates the
  // CompositorFrame submitted above.
  BeginFrameArgs args =
      CreateBeginFrameArgsForTesting(BEGINFRAME_FROM_HERE, 0, 1);
  for (int i = 0; i < 3; ++i) {
    begin_frame_source()->TestOnBeginFrame(args);
    EXPECT_TRUE(dependency_tracker().has_deadline());
  }
  begin_frame_source()->TestOnBeginFrame(args);
  EXPECT_FALSE(dependency_tracker().has_deadline());
  EXPECT_FALSE(display_surface()->HasPendingFrame());
  EXPECT_TRUE(display_surface()->HasActiveFrame());

  // A late arriving CompositorFrame should activate immediately without
  // scheduling a deadline and without waiting for dependencies to resolve.
  parent_support().SubmitCompositorFrame(
      parent_id1.local_surface_id(),
      MakeCompositorFrame({child_id1}, empty_surface_ids(),
                          TransferableResourceArray()));
  EXPECT_FALSE(dependency_tracker().has_deadline());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_TRUE(parent_surface()->HasActiveFrame());
}

// This test verifies that CompositorFrames submitted to a surface referenced
// by a parent CompositorFrame as a fallback will be rejected and ACK'ed
// immediately.
TEST_F(SurfaceSynchronizationTest, FallbackSurfacesClosed) {
  const SurfaceId parent_id1 = MakeSurfaceId(kParentFrameSink, 1);
  // This is the fallback child surface that the parent holds a reference to.
  const SurfaceId child_id1 = MakeSurfaceId(kChildFrameSink1, 1);
  // This is the primary child surface that the parent wants to block on.
  const SurfaceId child_id2 = MakeSurfaceId(kChildFrameSink1, 2);

  // child_support1 submits a CompositorFrame without any dependencies.
  // DidReceiveCompositorFrameAck should call on immediate activation.
  // However, resources will not be returned because this frame is a candidate
  // for display.
  TransferableResource resource;
  resource.id = 1337;
  resource.format = ALPHA_8;
  resource.filter = 1234;
  resource.size = gfx::Size(1234, 5678);
  ReturnedResourceArray returned_resources;
  TransferableResource::ReturnResources({resource}, &returned_resources);

  EXPECT_CALL(support_client_,
              DidReceiveCompositorFrameAck(Eq(ReturnedResourceArray())));
  child_support1().SubmitCompositorFrame(
      child_id1.local_surface_id(),
      MakeCompositorFrame(empty_surface_ids(), empty_surface_ids(),
                          {resource}));
  testing::Mock::VerifyAndClearExpectations(&support_client_);

  // The parent is blocked on |child_id2| and references |child_id1|. The
  // surface corresponding to |child_id1| will not accept new CompositorFrames
  // while the parent CompositorFrame is blocked.
  parent_support().SubmitCompositorFrame(
      parent_id1.local_surface_id(),
      MakeCompositorFrame({child_id2}, {child_id1},
                          TransferableResourceArray()));
  EXPECT_TRUE(dependency_tracker().has_deadline());
  EXPECT_TRUE(parent_surface()->HasPendingFrame());
  EXPECT_FALSE(parent_surface()->HasActiveFrame());

  // Resources will be returned immediately because |child_id1|'s surface is
  // closed.
  TransferableResource resource2;
  resource2.id = 1246;
  resource2.format = ALPHA_8;
  resource2.filter = 1357;
  resource2.size = gfx::Size(8765, 4321);
  ReturnedResourceArray returned_resources2;
  TransferableResource::ReturnResources({resource2}, &returned_resources2);
  EXPECT_CALL(support_client_,
              DidReceiveCompositorFrameAck(Eq(returned_resources2)));
  child_support1().SubmitCompositorFrame(
      child_id1.local_surface_id(),
      MakeCompositorFrame(empty_surface_ids(), empty_surface_ids(),
                          {resource2}));
  testing::Mock::VerifyAndClearExpectations(&support_client_);

  // Advance BeginFrames to trigger a deadline. This activates the
  // CompositorFrame submitted to the parent.
  BeginFrameArgs args =
      CreateBeginFrameArgsForTesting(BEGINFRAME_FROM_HERE, 0, 1);
  for (int i = 0; i < 3; ++i) {
    begin_frame_source()->TestOnBeginFrame(args);
    EXPECT_TRUE(dependency_tracker().has_deadline());
  }
  begin_frame_source()->TestOnBeginFrame(args);
  EXPECT_FALSE(dependency_tracker().has_deadline());
  EXPECT_FALSE(parent_surface()->HasPendingFrame());
  EXPECT_TRUE(parent_surface()->HasActiveFrame());

  // Resources will be returned immediately because |child_id1|'s surface is
  // closed forever.
  EXPECT_CALL(support_client_,
              DidReceiveCompositorFrameAck(Eq(returned_resources2)));
  child_support1().SubmitCompositorFrame(
      child_id1.local_surface_id(),
      MakeCompositorFrame(empty_surface_ids(), empty_surface_ids(),
                          {resource2}));
  testing::Mock::VerifyAndClearExpectations(&support_client_);
}

}  // namespace test
}  // namespace cc
