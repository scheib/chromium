// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/video_detector.h"

#include "ash/public/cpp/shell_window_ids.h"
#include "ash/shell.h"
#include "ash/wm/window_state.h"
#include "ash/wm_window.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/wm/core/window_util.h"

namespace ash {

const int VideoDetector::kMinUpdateWidth = 333;
const int VideoDetector::kMinUpdateHeight = 250;
const int VideoDetector::kMinFramesPerSecond = 15;
const int VideoDetector::kVideoTimeoutMs = 1000;
const int VideoDetector::kMinVideoDurationMs = 3000;

// Stores information about updates to a window and determines whether it's
// likely that a video is playing in it.
class VideoDetector::WindowInfo {
 public:
  WindowInfo() : buffer_start_(0), buffer_size_(0) {}

  // Handles an update within a window, returning true if it appears that
  // video is currently playing in the window.
  bool RecordUpdateAndCheckForVideo(const gfx::Rect& region,
                                    base::TimeTicks now) {
    if (region.width() < kMinUpdateWidth || region.height() < kMinUpdateHeight)
      return false;

    // If the buffer is full, drop the first timestamp.
    if (buffer_size_ == static_cast<size_t>(kMinFramesPerSecond)) {
      buffer_start_ = (buffer_start_ + 1) % kMinFramesPerSecond;
      buffer_size_--;
    }

    update_times_[(buffer_start_ + buffer_size_) % kMinFramesPerSecond] = now;
    buffer_size_++;

    const bool in_video =
        (buffer_size_ == static_cast<size_t>(kMinFramesPerSecond)) &&
        ((now - update_times_[buffer_start_]).InSecondsF() <= 1.0);

    if (in_video && video_start_time_.is_null())
      video_start_time_ = update_times_[buffer_start_];
    else if (!in_video && !video_start_time_.is_null())
      video_start_time_ = base::TimeTicks();

    const base::TimeDelta elapsed = now - video_start_time_;
    return in_video &&
           elapsed >= base::TimeDelta::FromMilliseconds(kMinVideoDurationMs);
  }

 private:
  // Circular buffer containing update times of the last (up to
  // |kMinFramesPerSecond|) video-sized updates to this window.
  base::TimeTicks update_times_[kMinFramesPerSecond];

  // Time at which the current sequence of updates that looks like video
  // started. Empty if video isn't currently playing.
  base::TimeTicks video_start_time_;

  // Index into |update_times_| of the oldest update.
  size_t buffer_start_;

  // Number of updates stored in |update_times_|.
  size_t buffer_size_;

  DISALLOW_COPY_AND_ASSIGN(WindowInfo);
};

VideoDetector::VideoDetector()
    : state_(State::NOT_PLAYING),
      video_is_playing_(false),
      window_observer_manager_(this),
      scoped_session_observer_(this),
      is_shutting_down_(false) {
  aura::Env::GetInstance()->AddObserver(this);
  Shell::Get()->AddShellObserver(this);
}

VideoDetector::~VideoDetector() {
  Shell::Get()->RemoveShellObserver(this);
  aura::Env::GetInstance()->RemoveObserver(this);
}

void VideoDetector::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void VideoDetector::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool VideoDetector::TriggerTimeoutForTest() {
  if (!video_inactive_timer_.IsRunning())
    return false;

  video_inactive_timer_.Stop();
  HandleVideoInactive();
  return true;
}

void VideoDetector::OnWindowInitialized(aura::Window* window) {
  window_observer_manager_.Add(window);
}

void VideoDetector::OnDelegatedFrameDamage(
    aura::Window* window,
    const gfx::Rect& damage_rect_in_dip) {
  if (is_shutting_down_)
    return;
  std::unique_ptr<WindowInfo>& info = window_infos_[window];
  if (!info.get())
    info.reset(new WindowInfo);

  base::TimeTicks now =
      !now_for_test_.is_null() ? now_for_test_ : base::TimeTicks::Now();
  if (info->RecordUpdateAndCheckForVideo(damage_rect_in_dip, now))
    HandleVideoActivity(window, now);
}

void VideoDetector::OnWindowDestroying(aura::Window* window) {
  if (fullscreen_root_windows_.count(window)) {
    window_observer_manager_.Remove(window);
    fullscreen_root_windows_.erase(window);
    UpdateState();
  }
}

void VideoDetector::OnWindowDestroyed(aura::Window* window) {
  window_infos_.erase(window);
  window_observer_manager_.Remove(window);
}

void VideoDetector::OnChromeTerminating() {
  // Stop checking video activity once the shutdown
  // process starts. crbug.com/231696.
  is_shutting_down_ = true;
}

void VideoDetector::OnFullscreenStateChanged(bool is_fullscreen,
                                             WmWindow* root_window) {
  aura::Window* aura_window = root_window->aura_window();
  if (is_fullscreen && !fullscreen_root_windows_.count(aura_window)) {
    fullscreen_root_windows_.insert(aura_window);
    if (!window_observer_manager_.IsObserving(aura_window))
      window_observer_manager_.Add(aura_window);
    UpdateState();
  } else if (!is_fullscreen && fullscreen_root_windows_.count(aura_window)) {
    fullscreen_root_windows_.erase(aura_window);
    window_observer_manager_.Remove(aura_window);
    UpdateState();
  }
}

void VideoDetector::HandleVideoActivity(aura::Window* window,
                                        base::TimeTicks now) {
  if (!window->IsVisible())
    return;

  gfx::Rect root_bounds = window->GetRootWindow()->bounds();
  if (!window->GetBoundsInRootWindow().Intersects(root_bounds))
    return;

  video_is_playing_ = true;
  video_inactive_timer_.Start(
      FROM_HERE, base::TimeDelta::FromMilliseconds(kVideoTimeoutMs), this,
      &VideoDetector::HandleVideoInactive);
  UpdateState();
}

void VideoDetector::HandleVideoInactive() {
  video_is_playing_ = false;
  UpdateState();
}

void VideoDetector::UpdateState() {
  State new_state = State::NOT_PLAYING;
  if (video_is_playing_) {
    new_state = fullscreen_root_windows_.empty() ? State::PLAYING_WINDOWED
                                                 : State::PLAYING_FULLSCREEN;
  }

  if (state_ != new_state) {
    state_ = new_state;
    for (auto& observer : observers_)
      observer.OnVideoStateChanged(state_);
  }
}

}  // namespace ash
