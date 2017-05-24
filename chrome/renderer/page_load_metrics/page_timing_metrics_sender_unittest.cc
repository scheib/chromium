// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/page_load_metrics/page_timing_metrics_sender.h"

#include "base/time/time.h"
#include "base/timer/mock_timer.h"
#include "chrome/common/page_load_metrics/page_load_metrics.mojom.h"
#include "chrome/renderer/page_load_metrics/fake_page_timing_sender.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace page_load_metrics {

// Thin wrapper around PageTimingMetricsSender that provides access to the
// MockTimer instance.
class TestPageTimingMetricsSender : public PageTimingMetricsSender {
 public:
  explicit TestPageTimingMetricsSender(
      std::unique_ptr<PageTimingSender> page_timing_sender,
      mojom::PageLoadTimingPtr initial_timing)
      : PageTimingMetricsSender(
            std::move(page_timing_sender),
            std::unique_ptr<base::Timer>(new base::MockTimer(false, false)),
            std::move(initial_timing)) {}

  base::MockTimer* mock_timer() const {
    return reinterpret_cast<base::MockTimer*>(timer());
  }
};

class PageTimingMetricsSenderTest : public testing::Test {
 public:
  PageTimingMetricsSenderTest()
      : metrics_sender_(new TestPageTimingMetricsSender(
            base::MakeUnique<FakePageTimingSender>(&validator_),
            mojom::PageLoadTiming::New())) {}

 protected:
  FakePageTimingSender::PageTimingValidator validator_;
  std::unique_ptr<TestPageTimingMetricsSender> metrics_sender_;
};

TEST_F(PageTimingMetricsSenderTest, Basic) {
  base::Time nav_start = base::Time::FromDoubleT(10);
  base::TimeDelta first_layout = base::TimeDelta::FromMillisecondsD(2);

  mojom::PageLoadTiming timing;
  InitPageLoadTimingForTest(&timing);
  timing.navigation_start = nav_start;
  timing.document_timing->first_layout = first_layout;

  metrics_sender_->Send(timing.Clone());

  // Firing the timer should trigger sending of an SendTiming call.
  validator_.ExpectPageLoadTiming(timing);
  ASSERT_TRUE(metrics_sender_->mock_timer()->IsRunning());
  metrics_sender_->mock_timer()->Fire();
  EXPECT_FALSE(metrics_sender_->mock_timer()->IsRunning());

  // At this point, we should have triggered the send of the SendTiming call.
  validator_.VerifyExpectedTimings();

  // Attempt to send the same timing instance again. The send should be
  // suppressed, since the timing instance hasn't changed since the last send.
  metrics_sender_->Send(timing.Clone());
  EXPECT_FALSE(metrics_sender_->mock_timer()->IsRunning());
}

TEST_F(PageTimingMetricsSenderTest, CoalesceMultipleTimings) {
  base::Time nav_start = base::Time::FromDoubleT(10);
  base::TimeDelta first_layout = base::TimeDelta::FromMillisecondsD(2);
  base::TimeDelta load_event = base::TimeDelta::FromMillisecondsD(4);

  mojom::PageLoadTiming timing;
  InitPageLoadTimingForTest(&timing);
  timing.navigation_start = nav_start;
  timing.document_timing->first_layout = first_layout;

  metrics_sender_->Send(timing.Clone());
  ASSERT_TRUE(metrics_sender_->mock_timer()->IsRunning());

  // Send an updated PageLoadTiming before the timer has fired. When the timer
  // fires, the updated PageLoadTiming should be sent.
  timing.document_timing->load_event_start = load_event;
  metrics_sender_->Send(timing.Clone());

  // Firing the timer should trigger sending of the SendTiming call with
  // the most recently provided PageLoadTiming instance.
  validator_.ExpectPageLoadTiming(timing);
  metrics_sender_->mock_timer()->Fire();
  EXPECT_FALSE(metrics_sender_->mock_timer()->IsRunning());
}

TEST_F(PageTimingMetricsSenderTest, MultipleTimings) {
  base::Time nav_start = base::Time::FromDoubleT(10);
  base::TimeDelta first_layout = base::TimeDelta::FromMillisecondsD(2);
  base::TimeDelta load_event = base::TimeDelta::FromMillisecondsD(4);

  mojom::PageLoadTiming timing;
  InitPageLoadTimingForTest(&timing);
  timing.navigation_start = nav_start;
  timing.document_timing->first_layout = first_layout;

  metrics_sender_->Send(timing.Clone());
  ASSERT_TRUE(metrics_sender_->mock_timer()->IsRunning());
  validator_.ExpectPageLoadTiming(timing);
  metrics_sender_->mock_timer()->Fire();
  EXPECT_FALSE(metrics_sender_->mock_timer()->IsRunning());
  validator_.VerifyExpectedTimings();

  // Send an updated PageLoadTiming after the timer for the first send request
  // has fired, and verify that a second timing is sent.
  timing.document_timing->load_event_start = load_event;
  metrics_sender_->Send(timing.Clone());
  ASSERT_TRUE(metrics_sender_->mock_timer()->IsRunning());
  validator_.ExpectPageLoadTiming(timing);
  metrics_sender_->mock_timer()->Fire();
  EXPECT_FALSE(metrics_sender_->mock_timer()->IsRunning());
}

TEST_F(PageTimingMetricsSenderTest, SendTimingOnDestructor) {
  mojom::PageLoadTiming timing;
  InitPageLoadTimingForTest(&timing);
  timing.navigation_start = base::Time::FromDoubleT(10);
  timing.document_timing->first_layout = base::TimeDelta::FromMilliseconds(10);

  // This test wants to verify behavior in the PageTimingMetricsSender
  // destructor. The EXPECT_CALL will be satisfied when the |metrics_sender_|
  // is destroyed below.
  metrics_sender_->Send(timing.Clone());
  validator_.ExpectPageLoadTiming(timing);
  ASSERT_TRUE(metrics_sender_->mock_timer()->IsRunning());

  // Destroy |metrics_sender_|, in order to force its destructor to run.
  metrics_sender_.reset();
}

}  // namespace page_load_metrics
