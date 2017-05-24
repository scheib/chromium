// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/video_detector.h"

#include <deque>
#include <memory>

#include "ash/session/session_controller.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "ash/wm/window_state.h"
#include "ash/wm/wm_event.h"
#include "base/compiler_specific.h"
#include "base/time/time.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/test/test_windows.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/wm/public/window_types.h"

namespace ash {
namespace test {

// Implementation that just records video state changes.
class TestObserver : public VideoDetector::Observer {
 public:
  TestObserver() {}

  bool empty() const { return states_.empty(); }
  void reset() { states_.clear(); }

  // Pops and returns the earliest-received state.
  VideoDetector::State PopState() {
    CHECK(!states_.empty());
    VideoDetector::State first_state = states_.front();
    states_.pop_front();
    return first_state;
  }

  // VideoDetector::Observer implementation.
  void OnVideoStateChanged(VideoDetector::State state) override {
    states_.push_back(state);
  }

 private:
  // States in the order they were received.
  std::deque<VideoDetector::State> states_;

  DISALLOW_COPY_AND_ASSIGN(TestObserver);
};

class VideoDetectorTest : public AshTestBase {
 public:
  VideoDetectorTest()
      : kMinFps(VideoDetector::kMinFramesPerSecond),
        kMinRect(gfx::Point(0, 0),
                 gfx::Size(VideoDetector::kMinUpdateWidth,
                           VideoDetector::kMinUpdateHeight)),
        kMinDuration(base::TimeDelta::FromMilliseconds(
            VideoDetector::kMinVideoDurationMs)),
        kTimeout(
            base::TimeDelta::FromMilliseconds(VideoDetector::kVideoTimeoutMs)),
        next_window_id_(1000) {}
  ~VideoDetectorTest() override {}

  void SetUp() override {
    AshTestBase::SetUp();
    observer_.reset(new TestObserver);
    detector_ = Shell::Get()->video_detector();
    detector_->AddObserver(observer_.get());

    now_ = base::TimeTicks::Now();
    detector_->set_now_for_test(now_);
  }

  void TearDown() override {
    detector_->RemoveObserver(observer_.get());
    AshTestBase::TearDown();
  }

 protected:
  // Move |detector_|'s idea of the current time forward by |delta|.
  void AdvanceTime(base::TimeDelta delta) {
    now_ += delta;
    detector_->set_now_for_test(now_);
  }

  // Creates and returns a new window with |bounds|.
  std::unique_ptr<aura::Window> CreateTestWindow(const gfx::Rect& bounds) {
    return std::unique_ptr<aura::Window>(
        CreateTestWindowInShell(SK_ColorRED, next_window_id_++, bounds));
  }

  // Report updates to |window| of area |region| at a rate of
  // |updates_per_second| over |duration|. The first update will be sent
  // immediately and |now_| will incremented by |duration| upon returning.
  void SendUpdates(aura::Window* window,
                   const gfx::Rect& region,
                   int updates_per_second,
                   base::TimeDelta duration) {
    const base::TimeDelta time_between_updates =
        base::TimeDelta::FromSecondsD(1.0 / updates_per_second);
    const base::TimeTicks end_time = now_ + duration;
    while (now_ < end_time) {
      detector_->OnDelegatedFrameDamage(window, region);
      AdvanceTime(time_between_updates);
    }
    now_ = end_time;
    detector_->set_now_for_test(now_);
  }

  // Constants placed here for convenience.
  const int kMinFps;
  const gfx::Rect kMinRect;
  const base::TimeDelta kMinDuration;
  const base::TimeDelta kTimeout;

  VideoDetector* detector_;  // not owned
  std::unique_ptr<TestObserver> observer_;

  // The current (fake) time used by |detector_|.
  base::TimeTicks now_;

  // Next ID to be assigned by CreateTestWindow().
  int next_window_id_;

 private:
  DISALLOW_COPY_AND_ASSIGN(VideoDetectorTest);
};

TEST_F(VideoDetectorTest, DontReportWhenRegionTooSmall) {
  std::unique_ptr<aura::Window> window =
      CreateTestWindow(gfx::Rect(0, 0, 1024, 768));
  gfx::Rect rect = kMinRect;
  rect.Inset(0, 0, 1, 0);
  SendUpdates(window.get(), rect, 2 * kMinFps, 2 * kMinDuration);
  EXPECT_TRUE(observer_->empty());
}

TEST_F(VideoDetectorTest, DontReportWhenFramerateTooLow) {
  std::unique_ptr<aura::Window> window =
      CreateTestWindow(gfx::Rect(0, 0, 1024, 768));
  SendUpdates(window.get(), kMinRect, kMinFps - 5, 2 * kMinDuration);
  EXPECT_TRUE(observer_->empty());
}

TEST_F(VideoDetectorTest, DontReportWhenNotPlayingLongEnough) {
  std::unique_ptr<aura::Window> window =
      CreateTestWindow(gfx::Rect(0, 0, 1024, 768));
  SendUpdates(window.get(), kMinRect, 2 * kMinFps, 0.5 * kMinDuration);
  EXPECT_TRUE(observer_->empty());

  // Continue playing.
  SendUpdates(window.get(), kMinRect, 2 * kMinFps, 0.6 * kMinDuration);
  EXPECT_EQ(VideoDetector::State::PLAYING_WINDOWED, observer_->PopState());
  EXPECT_TRUE(observer_->empty());
}

TEST_F(VideoDetectorTest, DontReportWhenWindowOffscreen) {
  std::unique_ptr<aura::Window> window =
      CreateTestWindow(gfx::Rect(0, 0, 1024, 768));
  window->SetBounds(
      gfx::Rect(gfx::Point(Shell::GetPrimaryRootWindow()->bounds().width(), 0),
                window->bounds().size()));
  SendUpdates(window.get(), kMinRect, 2 * kMinFps, 2 * kMinDuration);
  EXPECT_TRUE(observer_->empty());

  // Move the window onscreen.
  window->SetBounds(gfx::Rect(gfx::Point(0, 0), window->bounds().size()));
  SendUpdates(window.get(), kMinRect, 2 * kMinFps, 2 * kMinDuration);
  EXPECT_EQ(VideoDetector::State::PLAYING_WINDOWED, observer_->PopState());
  EXPECT_TRUE(observer_->empty());
}

TEST_F(VideoDetectorTest, DontReportWhenWindowHidden) {
  std::unique_ptr<aura::Window> window =
      CreateTestWindow(gfx::Rect(0, 0, 1024, 768));
  // Reparent the window to the root to make sure that visibility changes aren't
  // animated.
  Shell::GetPrimaryRootWindow()->AddChild(window.get());
  window->Hide();
  SendUpdates(window.get(), kMinRect, kMinFps + 5, 2 * kMinDuration);
  EXPECT_TRUE(observer_->empty());

  // Make the window visible.
  observer_->reset();
  AdvanceTime(kTimeout);
  window->Show();
  SendUpdates(window.get(), kMinRect, kMinFps + 5, 2 * kMinDuration);
  EXPECT_EQ(VideoDetector::State::PLAYING_WINDOWED, observer_->PopState());
  EXPECT_TRUE(observer_->empty());
}

TEST_F(VideoDetectorTest, DontReportDuringShutdown) {
  std::unique_ptr<aura::Window> window =
      CreateTestWindow(gfx::Rect(0, 0, 1024, 768));
  Shell::Get()->session_controller()->NotifyChromeTerminating();
  SendUpdates(window.get(), kMinRect, kMinFps + 5, 2 * kMinDuration);
  EXPECT_TRUE(observer_->empty());
}

TEST_F(VideoDetectorTest, ReportStartAndStop) {
  const base::TimeDelta kDuration =
      kMinDuration + base::TimeDelta::FromMilliseconds(100);
  std::unique_ptr<aura::Window> window =
      CreateTestWindow(gfx::Rect(0, 0, 1024, 768));
  SendUpdates(window.get(), kMinRect, kMinFps + 5, kDuration);
  EXPECT_EQ(VideoDetector::State::PLAYING_WINDOWED, observer_->PopState());
  EXPECT_TRUE(observer_->empty());

  AdvanceTime(kTimeout);
  EXPECT_TRUE(detector_->TriggerTimeoutForTest());
  EXPECT_EQ(VideoDetector::State::NOT_PLAYING, observer_->PopState());
  EXPECT_TRUE(observer_->empty());

  // The timer shouldn't be running anymore.
  EXPECT_FALSE(detector_->TriggerTimeoutForTest());

  // Start playing again.
  SendUpdates(window.get(), kMinRect, kMinFps + 5, kDuration);
  EXPECT_EQ(VideoDetector::State::PLAYING_WINDOWED, observer_->PopState());
  EXPECT_TRUE(observer_->empty());

  AdvanceTime(kTimeout);
  EXPECT_TRUE(detector_->TriggerTimeoutForTest());
  EXPECT_EQ(VideoDetector::State::NOT_PLAYING, observer_->PopState());
  EXPECT_TRUE(observer_->empty());
}

TEST_F(VideoDetectorTest, ReportOnceForMultipleWindows) {
  gfx::Rect kWindowBounds(gfx::Point(), gfx::Size(1024, 768));
  std::unique_ptr<aura::Window> window1 = CreateTestWindow(kWindowBounds);
  std::unique_ptr<aura::Window> window2 = CreateTestWindow(kWindowBounds);

  // Even if there's video playing in both windows, the observer should only
  // receive a single notification.
  const int fps = 2 * kMinFps;
  const base::TimeDelta time_between_updates =
      base::TimeDelta::FromSecondsD(1.0 / fps);
  const base::TimeTicks start_time = now_;
  while (now_ < start_time + 2 * kMinDuration) {
    detector_->OnDelegatedFrameDamage(window1.get(), kMinRect);
    detector_->OnDelegatedFrameDamage(window2.get(), kMinRect);
    AdvanceTime(time_between_updates);
  }
  EXPECT_EQ(VideoDetector::State::PLAYING_WINDOWED, observer_->PopState());
  EXPECT_TRUE(observer_->empty());
}

TEST_F(VideoDetectorTest, ReportFullscreen) {
  UpdateDisplay("1024x768,1024x768");

  std::unique_ptr<aura::Window> window =
      CreateTestWindow(gfx::Rect(0, 0, 1024, 768));
  wm::WindowState* window_state = wm::GetWindowState(window.get());
  const wm::WMEvent toggle_fullscreen_event(wm::WM_EVENT_TOGGLE_FULLSCREEN);
  window_state->OnWMEvent(&toggle_fullscreen_event);
  ASSERT_TRUE(window_state->IsFullscreen());
  window->Focus();
  SendUpdates(window.get(), kMinRect, 2 * kMinFps, 2 * kMinDuration);
  EXPECT_EQ(VideoDetector::State::PLAYING_FULLSCREEN, observer_->PopState());
  EXPECT_TRUE(observer_->empty());

  // Make the window non-fullscreen.
  observer_->reset();
  window_state->OnWMEvent(&toggle_fullscreen_event);
  ASSERT_FALSE(window_state->IsFullscreen());
  EXPECT_EQ(VideoDetector::State::PLAYING_WINDOWED, observer_->PopState());
  EXPECT_TRUE(observer_->empty());

  // Open a second, fullscreen window. Fullscreen video should still be reported
  // due to the second window being fullscreen. This avoids situations where
  // non-fullscreen video could be reported when multiple videos are playing in
  // fullscreen and non-fullscreen windows.
  observer_->reset();
  std::unique_ptr<aura::Window> other_window =
      CreateTestWindow(gfx::Rect(1024, 0, 1024, 768));
  wm::WindowState* other_window_state = wm::GetWindowState(other_window.get());
  other_window_state->OnWMEvent(&toggle_fullscreen_event);
  ASSERT_TRUE(other_window_state->IsFullscreen());
  EXPECT_EQ(VideoDetector::State::PLAYING_FULLSCREEN, observer_->PopState());
  EXPECT_TRUE(observer_->empty());

  // Make the second window non-fullscreen and check that the observer is
  // immediately notified about windowed video.
  observer_->reset();
  other_window_state->OnWMEvent(&toggle_fullscreen_event);
  ASSERT_FALSE(other_window_state->IsFullscreen());
  EXPECT_EQ(VideoDetector::State::PLAYING_WINDOWED, observer_->PopState());
  EXPECT_TRUE(observer_->empty());
}

}  // namespace test
}  // namespace ash
