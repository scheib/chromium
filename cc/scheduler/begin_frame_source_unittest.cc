// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/scheduler/begin_frame_source.h"

#include <stdint.h>

#include "base/memory/ptr_util.h"
#include "base/test/test_simple_task_runner.h"
#include "cc/test/begin_frame_args_test.h"
#include "cc/test/begin_frame_source_test.h"
#include "cc/test/scheduler_test_common.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::StrictMock;

namespace cc {
namespace {

// BeginFrameSource testing ----------------------------------------------------
TEST(BeginFrameSourceTest, SourceIdsAreUnique) {
  StubBeginFrameSource source1;
  StubBeginFrameSource source2;
  StubBeginFrameSource source3;
  EXPECT_NE(source1.source_id(), source2.source_id());
  EXPECT_NE(source1.source_id(), source3.source_id());
  EXPECT_NE(source2.source_id(), source3.source_id());
}

// BackToBackBeginFrameSource testing ------------------------------------------
class BackToBackBeginFrameSourceTest : public ::testing::Test {
 protected:
  static const int64_t kDeadline;
  static const int64_t kInterval;

  void SetUp() override {
    now_src_.reset(new base::SimpleTestTickClock());
    now_src_->Advance(base::TimeDelta::FromMicroseconds(1000));
    task_runner_ =
        make_scoped_refptr(new OrderedSimpleTaskRunner(now_src_.get(), false));
    std::unique_ptr<TestDelayBasedTimeSource> time_source(
        new TestDelayBasedTimeSource(now_src_.get(), task_runner_.get()));
    delay_based_time_source_ = time_source.get();
    source_.reset(new BackToBackBeginFrameSource(std::move(time_source)));
    obs_ = base::WrapUnique(new ::testing::StrictMock<MockBeginFrameObserver>);
  }

  void TearDown() override { obs_.reset(); }

  std::unique_ptr<base::SimpleTestTickClock> now_src_;
  scoped_refptr<OrderedSimpleTaskRunner> task_runner_;
  std::unique_ptr<BackToBackBeginFrameSource> source_;
  std::unique_ptr<MockBeginFrameObserver> obs_;
  TestDelayBasedTimeSource* delay_based_time_source_;  // Owned by |now_src_|.
};

const int64_t BackToBackBeginFrameSourceTest::kDeadline =
    BeginFrameArgs::DefaultInterval().ToInternalValue();

const int64_t BackToBackBeginFrameSourceTest::kInterval =
    BeginFrameArgs::DefaultInterval().ToInternalValue();

TEST_F(BackToBackBeginFrameSourceTest, AddObserverSendsBeginFrame) {
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  source_->AddObserver(obs_.get());
  EXPECT_TRUE(task_runner_->HasPendingTasks());
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 1, 1000,
                          1000 + kDeadline, kInterval);
  task_runner_->RunPendingTasks();

  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 2, 1100,
                          1100 + kDeadline, kInterval);
  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 1, 1, true));
  task_runner_->RunPendingTasks();
}

TEST_F(BackToBackBeginFrameSourceTest,
       RemoveObserverThenDidFinishFrameProducesNoFrame) {
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  source_->AddObserver(obs_.get());
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 1, 1000,
                          1000 + kDeadline, kInterval);
  task_runner_->RunPendingTasks();

  source_->RemoveObserver(obs_.get());
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 1, 1, true));

  // Verify no BeginFrame is sent to |obs_|. There is a pending task in the
  // task_runner_ as a BeginFrame was posted, but it gets aborted since |obs_|
  // is removed.
  task_runner_->RunPendingTasks();
  EXPECT_FALSE(task_runner_->HasPendingTasks());
}

TEST_F(BackToBackBeginFrameSourceTest,
       DidFinishFrameThenRemoveObserverProducesNoFrame) {
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  source_->AddObserver(obs_.get());
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 1, 1000,
                          1000 + kDeadline, kInterval);
  task_runner_->RunPendingTasks();

  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 1, 1, true));
  source_->RemoveObserver(obs_.get());

  EXPECT_TRUE(task_runner_->HasPendingTasks());
  task_runner_->RunPendingTasks();
}

TEST_F(BackToBackBeginFrameSourceTest,
       TogglingObserverThenDidFinishFrameProducesCorrectFrame) {
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  source_->AddObserver(obs_.get());
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 1, 1000,
                          1000 + kDeadline, kInterval);
  task_runner_->RunPendingTasks();

  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->RemoveObserver(obs_.get());

  now_src_->Advance(base::TimeDelta::FromMicroseconds(10));
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  source_->AddObserver(obs_.get());

  now_src_->Advance(base::TimeDelta::FromMicroseconds(10));
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 1, 1, true));

  now_src_->Advance(base::TimeDelta::FromMicroseconds(10));
  // The begin frame is posted at the time when the observer was added,
  // so it ignores changes to "now" afterward.
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 2, 1110,
                          1110 + kDeadline, kInterval);
  EXPECT_TRUE(task_runner_->HasPendingTasks());
  task_runner_->RunPendingTasks();
}

TEST_F(BackToBackBeginFrameSourceTest,
       DidFinishFrameThenTogglingObserverProducesCorrectFrame) {
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  source_->AddObserver(obs_.get());
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 1, 1000,
                          1000 + kDeadline, kInterval);
  task_runner_->RunPendingTasks();

  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 1, 1, true));

  now_src_->Advance(base::TimeDelta::FromMicroseconds(10));
  source_->RemoveObserver(obs_.get());

  now_src_->Advance(base::TimeDelta::FromMicroseconds(10));
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  source_->AddObserver(obs_.get());

  now_src_->Advance(base::TimeDelta::FromMicroseconds(10));
  // Ticks at the time at which the observer was added, ignoring the
  // last change to "now".
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 2, 1120,
                          1120 + kDeadline, kInterval);
  EXPECT_TRUE(task_runner_->HasPendingTasks());
  task_runner_->RunPendingTasks();
}

TEST_F(BackToBackBeginFrameSourceTest, DidFinishFrameNoObserver) {
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  source_->AddObserver(obs_.get());
  source_->RemoveObserver(obs_.get());
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 1, 1, true));
  EXPECT_FALSE(task_runner_->RunPendingTasks());
}

TEST_F(BackToBackBeginFrameSourceTest, DidFinishFrameMultipleCallsIdempotent) {
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  source_->AddObserver(obs_.get());
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 1, 1000,
                          1000 + kDeadline, kInterval);
  task_runner_->RunPendingTasks();

  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 1, 1, true));
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 1, 1, true));
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 1, 1, true));
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 2, 1100,
                          1100 + kDeadline, kInterval);
  task_runner_->RunPendingTasks();

  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 2, 2, true));
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 2, 2, true));
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 2, 2, true));
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 3, 1200,
                          1200 + kDeadline, kInterval);
  task_runner_->RunPendingTasks();
}

TEST_F(BackToBackBeginFrameSourceTest, DelayInPostedTaskProducesCorrectFrame) {
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  source_->AddObserver(obs_.get());
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 1, 1000,
                          1000 + kDeadline, kInterval);
  task_runner_->RunPendingTasks();

  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->DidFinishFrame(obs_.get(),
                          BeginFrameAck(source_->source_id(), 1, 1, true));
  now_src_->Advance(base::TimeDelta::FromMicroseconds(50));
  // Ticks at the time the last frame finished, so ignores the last change to
  // "now".
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 2, 1100,
                          1100 + kDeadline, kInterval);

  EXPECT_TRUE(task_runner_->HasPendingTasks());
  task_runner_->RunPendingTasks();
}

TEST_F(BackToBackBeginFrameSourceTest, MultipleObserversSynchronized) {
  StrictMock<MockBeginFrameObserver> obs1, obs2;

  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(obs1, false);
  source_->AddObserver(&obs1);
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(obs2, false);
  source_->AddObserver(&obs2);

  EXPECT_BEGIN_FRAME_USED(obs1, source_->source_id(), 1, 1000, 1000 + kDeadline,
                          kInterval);
  EXPECT_BEGIN_FRAME_USED(obs2, source_->source_id(), 1, 1000, 1000 + kDeadline,
                          kInterval);
  task_runner_->RunPendingTasks();

  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->DidFinishFrame(&obs1,
                          BeginFrameAck(source_->source_id(), 1, 1, true));
  source_->DidFinishFrame(&obs2,
                          BeginFrameAck(source_->source_id(), 1, 1, true));
  EXPECT_BEGIN_FRAME_USED(obs1, source_->source_id(), 2, 1100, 1100 + kDeadline,
                          kInterval);
  EXPECT_BEGIN_FRAME_USED(obs2, source_->source_id(), 2, 1100, 1100 + kDeadline,
                          kInterval);
  task_runner_->RunPendingTasks();

  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->DidFinishFrame(&obs1,
                          BeginFrameAck(source_->source_id(), 2, 2, true));
  source_->DidFinishFrame(&obs2,
                          BeginFrameAck(source_->source_id(), 2, 2, true));
  EXPECT_TRUE(task_runner_->HasPendingTasks());
  source_->RemoveObserver(&obs1);
  source_->RemoveObserver(&obs2);
  task_runner_->RunPendingTasks();
}

TEST_F(BackToBackBeginFrameSourceTest, MultipleObserversInterleaved) {
  StrictMock<MockBeginFrameObserver> obs1, obs2;

  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(obs1, false);
  source_->AddObserver(&obs1);
  EXPECT_BEGIN_FRAME_USED(obs1, source_->source_id(), 1, 1000, 1000 + kDeadline,
                          kInterval);
  task_runner_->RunPendingTasks();

  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(obs2, false);
  source_->AddObserver(&obs2);
  EXPECT_BEGIN_FRAME_USED(obs2, source_->source_id(), 2, 1100, 1100 + kDeadline,
                          kInterval);
  task_runner_->RunPendingTasks();

  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->DidFinishFrame(&obs1,
                          BeginFrameAck(source_->source_id(), 1, 1, true));
  EXPECT_BEGIN_FRAME_USED(obs1, source_->source_id(), 3, 1200, 1200 + kDeadline,
                          kInterval);
  task_runner_->RunPendingTasks();

  source_->DidFinishFrame(&obs1,
                          BeginFrameAck(source_->source_id(), 3, 3, true));
  source_->RemoveObserver(&obs1);
  // Removing all finished observers should disable the time source.
  EXPECT_FALSE(delay_based_time_source_->Active());
  // Finishing the frame for |obs1| posts a begin frame task, which will be
  // aborted since |obs1| is removed. Clear that from the task runner.
  task_runner_->RunPendingTasks();

  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->DidFinishFrame(&obs2,
                          BeginFrameAck(source_->source_id(), 2, 2, true));
  EXPECT_BEGIN_FRAME_USED(obs2, source_->source_id(), 4, 1300, 1300 + kDeadline,
                          kInterval);
  task_runner_->RunPendingTasks();

  source_->DidFinishFrame(&obs2,
                          BeginFrameAck(source_->source_id(), 4, 4, true));
  source_->RemoveObserver(&obs2);
}

TEST_F(BackToBackBeginFrameSourceTest, MultipleObserversAtOnce) {
  StrictMock<MockBeginFrameObserver> obs1, obs2;

  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(obs1, false);
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(obs2, false);
  source_->AddObserver(&obs1);
  source_->AddObserver(&obs2);
  EXPECT_BEGIN_FRAME_USED(obs1, source_->source_id(), 1, 1000, 1000 + kDeadline,
                          kInterval);
  EXPECT_BEGIN_FRAME_USED(obs2, source_->source_id(), 1, 1000, 1000 + kDeadline,
                          kInterval);
  task_runner_->RunPendingTasks();

  // |obs1| finishes first.
  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->DidFinishFrame(&obs1,
                          BeginFrameAck(source_->source_id(), 1, 1, true));

  // |obs2| finishes also, before getting to the newly posted begin frame.
  now_src_->Advance(base::TimeDelta::FromMicroseconds(100));
  source_->DidFinishFrame(&obs2,
                          BeginFrameAck(source_->source_id(), 1, 1, true));

  // Because the begin frame source already ticked when |obs1| finished,
  // we see it as the frame time for both observers.
  EXPECT_BEGIN_FRAME_USED(obs1, source_->source_id(), 2, 1100, 1100 + kDeadline,
                          kInterval);
  EXPECT_BEGIN_FRAME_USED(obs2, source_->source_id(), 2, 1100, 1100 + kDeadline,
                          kInterval);
  task_runner_->RunPendingTasks();

  source_->DidFinishFrame(&obs1,
                          BeginFrameAck(source_->source_id(), 2, 2, true));
  source_->RemoveObserver(&obs1);
  source_->DidFinishFrame(&obs2,
                          BeginFrameAck(source_->source_id(), 2, 2, true));
  source_->RemoveObserver(&obs2);
}

// DelayBasedBeginFrameSource testing ------------------------------------------
class DelayBasedBeginFrameSourceTest : public ::testing::Test {
 public:
  std::unique_ptr<base::SimpleTestTickClock> now_src_;
  scoped_refptr<OrderedSimpleTaskRunner> task_runner_;
  std::unique_ptr<DelayBasedBeginFrameSource> source_;
  std::unique_ptr<MockBeginFrameObserver> obs_;

  void SetUp() override {
    now_src_.reset(new base::SimpleTestTickClock());
    now_src_->Advance(base::TimeDelta::FromMicroseconds(1000));
    task_runner_ =
        make_scoped_refptr(new OrderedSimpleTaskRunner(now_src_.get(), false));
    std::unique_ptr<DelayBasedTimeSource> time_source(
        new TestDelayBasedTimeSource(now_src_.get(), task_runner_.get()));
    time_source->SetTimebaseAndInterval(
        base::TimeTicks(), base::TimeDelta::FromMicroseconds(10000));
    source_.reset(new DelayBasedBeginFrameSource(std::move(time_source)));
    obs_.reset(new MockBeginFrameObserver);
  }

  void TearDown() override { obs_.reset(); }
};

TEST_F(DelayBasedBeginFrameSourceTest,
       AddObserverCallsOnBeginFrameWithMissedTick) {
  now_src_->Advance(base::TimeDelta::FromMicroseconds(9010));
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  EXPECT_BEGIN_FRAME_USED_MISSED(*obs_, source_->source_id(), 1, 10000, 20000,
                                 10000);
  source_->AddObserver(obs_.get());  // Should cause the last tick to be sent
  // No tasks should need to be run for this to occur.
}

TEST_F(DelayBasedBeginFrameSourceTest, AddObserverCallsCausesOnBeginFrame) {
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  EXPECT_BEGIN_FRAME_USED_MISSED(*obs_, source_->source_id(), 1, 0, 10000,
                                 10000);
  source_->AddObserver(obs_.get());
  EXPECT_EQ(10000, task_runner_->NextTaskTime().ToInternalValue());

  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 2, 10000, 20000, 10000);
  now_src_->Advance(base::TimeDelta::FromMicroseconds(9010));
  task_runner_->RunPendingTasks();
}

TEST_F(DelayBasedBeginFrameSourceTest, BasicOperation) {
  task_runner_->SetAutoAdvanceNowToPendingTasks(true);

  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  EXPECT_BEGIN_FRAME_USED_MISSED(*obs_, source_->source_id(), 1, 0, 10000,
                                 10000);
  source_->AddObserver(obs_.get());
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 2, 10000, 20000, 10000);
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 3, 20000, 30000, 10000);
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 4, 30000, 40000, 10000);
  task_runner_->RunUntilTime(base::TimeTicks::FromInternalValue(30001));

  source_->RemoveObserver(obs_.get());
  // No new frames....
  task_runner_->RunUntilTime(base::TimeTicks::FromInternalValue(60000));
}

TEST_F(DelayBasedBeginFrameSourceTest, VSyncChanges) {
  task_runner_->SetAutoAdvanceNowToPendingTasks(true);
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  EXPECT_BEGIN_FRAME_USED_MISSED(*obs_, source_->source_id(), 1, 0, 10000,
                                 10000);
  source_->AddObserver(obs_.get());

  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 2, 10000, 20000, 10000);
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 3, 20000, 30000, 10000);
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 4, 30000, 40000, 10000);
  task_runner_->RunUntilTime(base::TimeTicks::FromInternalValue(30001));

  // Update the vsync information
  source_->OnUpdateVSyncParameters(base::TimeTicks::FromInternalValue(27500),
                                   base::TimeDelta::FromMicroseconds(10001));

  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 5, 40000, 47502, 10001);
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 6, 47502, 57503, 10001);
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 7, 57503, 67504, 10001);
  task_runner_->RunUntilTime(base::TimeTicks::FromInternalValue(60000));
}

TEST_F(DelayBasedBeginFrameSourceTest, AuthoritativeVSyncChanges) {
  task_runner_->SetAutoAdvanceNowToPendingTasks(true);
  source_->OnUpdateVSyncParameters(base::TimeTicks::FromInternalValue(500),
                                   base::TimeDelta::FromMicroseconds(10000));
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  EXPECT_BEGIN_FRAME_USED_MISSED(*obs_, source_->source_id(), 1, 500, 10500,
                                 10000);
  source_->AddObserver(obs_.get());

  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 2, 10500, 20500, 10000);
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 3, 20500, 30500, 10000);
  task_runner_->RunUntilTime(base::TimeTicks::FromInternalValue(20501));

  // This will keep the same timebase, so 500, 9999
  source_->SetAuthoritativeVSyncInterval(
      base::TimeDelta::FromMicroseconds(9999));
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 4, 30500, 40496, 9999);
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 5, 40496, 50495, 9999);
  task_runner_->RunUntilTime(base::TimeTicks::FromInternalValue(40497));

  // Change the vsync params, but the new interval will be ignored.
  source_->OnUpdateVSyncParameters(base::TimeTicks::FromInternalValue(400),
                                   base::TimeDelta::FromMicroseconds(1));
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 6, 50495, 60394, 9999);
  EXPECT_BEGIN_FRAME_USED(*obs_, source_->source_id(), 7, 60394, 70393, 9999);
  task_runner_->RunUntilTime(base::TimeTicks::FromInternalValue(60395));
}

TEST_F(DelayBasedBeginFrameSourceTest, MultipleObservers) {
  StrictMock<MockBeginFrameObserver> obs1, obs2;

  // now_src_ starts off at 1000.
  task_runner_->RunForPeriod(base::TimeDelta::FromMicroseconds(9010));
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(obs1, false);
  EXPECT_BEGIN_FRAME_USED_MISSED(obs1, source_->source_id(), 1, 10000, 20000,
                                 10000);
  source_->AddObserver(&obs1);  // Should cause the last tick to be sent
  // No tasks should need to be run for this to occur.

  EXPECT_BEGIN_FRAME_USED(obs1, source_->source_id(), 2, 20000, 30000, 10000);
  task_runner_->RunForPeriod(base::TimeDelta::FromMicroseconds(10000));

  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(obs2, false);
  // Sequence number unchanged for missed frame with time of last normal frame.
  EXPECT_BEGIN_FRAME_USED_MISSED(obs2, source_->source_id(), 2, 20000, 30000,
                                 10000);
  source_->AddObserver(&obs2);  // Should cause the last tick to be sent
  // No tasks should need to be run for this to occur.

  EXPECT_BEGIN_FRAME_USED(obs1, source_->source_id(), 3, 30000, 40000, 10000);
  EXPECT_BEGIN_FRAME_USED(obs2, source_->source_id(), 3, 30000, 40000, 10000);
  task_runner_->RunForPeriod(base::TimeDelta::FromMicroseconds(10000));

  source_->RemoveObserver(&obs1);

  EXPECT_BEGIN_FRAME_USED(obs2, source_->source_id(), 4, 40000, 50000, 10000);
  task_runner_->RunForPeriod(base::TimeDelta::FromMicroseconds(10000));

  source_->RemoveObserver(&obs2);
  task_runner_->RunUntilTime(base::TimeTicks::FromInternalValue(50000));
  EXPECT_FALSE(task_runner_->HasPendingTasks());
}

TEST_F(DelayBasedBeginFrameSourceTest, DoubleTick) {
  StrictMock<MockBeginFrameObserver> obs;

  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(obs, false);
  EXPECT_BEGIN_FRAME_USED_MISSED(obs, source_->source_id(), 1, 0, 10000, 10000);
  source_->AddObserver(&obs);

  source_->OnUpdateVSyncParameters(base::TimeTicks::FromInternalValue(5000),
                                   base::TimeDelta::FromInternalValue(10000));
  now_src_->Advance(base::TimeDelta::FromInternalValue(4000));

  // No begin frame received.
  task_runner_->RunPendingTasks();

  // Begin frame received.
  source_->OnUpdateVSyncParameters(base::TimeTicks::FromInternalValue(10000),
                                   base::TimeDelta::FromInternalValue(10000));
  now_src_->Advance(base::TimeDelta::FromInternalValue(5000));
  EXPECT_BEGIN_FRAME_USED(obs, source_->source_id(), 2, 10000, 20000, 10000);
  task_runner_->RunPendingTasks();
}

TEST_F(DelayBasedBeginFrameSourceTest, DoubleTickMissedFrame) {
  StrictMock<MockBeginFrameObserver> obs;

  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(obs, false);
  EXPECT_BEGIN_FRAME_USED_MISSED(obs, source_->source_id(), 1, 0, 10000, 10000);
  source_->AddObserver(&obs);
  source_->RemoveObserver(&obs);

  source_->OnUpdateVSyncParameters(base::TimeTicks::FromInternalValue(5000),
                                   base::TimeDelta::FromInternalValue(10000));
  now_src_->Advance(base::TimeDelta::FromInternalValue(4000));

  // No missed frame received.
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(obs, false);
  // This does not cause a missed BeginFrame, but the sequence number is
  // incremented, because the possible missed frame has different time/interval.
  source_->AddObserver(&obs);
  source_->RemoveObserver(&obs);

  // Missed frame received.
  source_->OnUpdateVSyncParameters(base::TimeTicks::FromInternalValue(10000),
                                   base::TimeDelta::FromInternalValue(10000));
  now_src_->Advance(base::TimeDelta::FromInternalValue(5000));
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(obs, false);
  // Sequence number is incremented again, because the missed frame has
  // different time/interval.
  EXPECT_BEGIN_FRAME_USED_MISSED(obs, source_->source_id(), 3, 10000, 20000,
                                 10000);
  source_->AddObserver(&obs);
  source_->RemoveObserver(&obs);
}

// ExternalBeginFrameSource testing --------------------------------------------
class MockExternalBeginFrameSourceClient
    : public ExternalBeginFrameSourceClient {
 public:
  MOCK_METHOD1(OnNeedsBeginFrames, void(bool));
};

class ExternalBeginFrameSourceTest : public ::testing::Test {
 public:
  std::unique_ptr<MockExternalBeginFrameSourceClient> client_;
  std::unique_ptr<ExternalBeginFrameSource> source_;
  std::unique_ptr<MockBeginFrameObserver> obs_;

  void SetUp() override {
    client_.reset(new MockExternalBeginFrameSourceClient);
    source_.reset(new ExternalBeginFrameSource(client_.get()));
    obs_.reset(new MockBeginFrameObserver);
  }

  void TearDown() override {
    client_.reset();
    obs_.reset();
  }
};

// https://crbug.com/690127: Duplicate BeginFrame caused DCHECK crash.
TEST_F(ExternalBeginFrameSourceTest, OnBeginFrameChecksBeginFrameContinuity) {
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  EXPECT_CALL((*client_), OnNeedsBeginFrames(true)).Times(1);
  source_->AddObserver(obs_.get());

  BeginFrameArgs args = CreateBeginFrameArgsForTesting(
      BEGINFRAME_FROM_HERE, 0, 2, base::TimeTicks::FromInternalValue(10000));
  EXPECT_BEGIN_FRAME_ARGS_USED(*obs_, args);
  source_->OnBeginFrame(args);

  // Providing same args again to OnBeginFrame() should not notify observer.
  source_->OnBeginFrame(args);

  // Providing same args through a different ExternalBeginFrameSource also does
  // not notify observer.
  EXPECT_BEGIN_FRAME_SOURCE_PAUSED(*obs_, false);
  EXPECT_CALL((*client_), OnNeedsBeginFrames(true)).Times(1);
  ExternalBeginFrameSource source2(client_.get());
  source2.AddObserver(obs_.get());
  source2.OnBeginFrame(args);
}

}  // namespace
}  // namespace cc
