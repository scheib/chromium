// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/page_load_metrics/page_timing_metrics_sender.h"

#include <utility>

#include "base/callback.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/renderer/page_load_metrics/page_timing_sender.h"

namespace page_load_metrics {

namespace {
const int kInitialTimerDelayMillis = 50;
const int kTimerDelayMillis = 1000;
}  // namespace

PageTimingMetricsSender::PageTimingMetricsSender(
    std::unique_ptr<PageTimingSender> sender,
    std::unique_ptr<base::Timer> timer,
    mojom::PageLoadTimingPtr initial_timing)
    : sender_(std::move(sender)),
      timer_(std::move(timer)),
      last_timing_(std::move(initial_timing)),
      metadata_(mojom::PageLoadMetadata::New()) {
  if (!IsEmpty(*last_timing_)) {
    EnsureSendTimer();
  }
}

// On destruction, we want to send any data we have if we have a timer
// currently running (and thus are talking to a browser process)
PageTimingMetricsSender::~PageTimingMetricsSender() {
  if (timer_->IsRunning()) {
    timer_->Stop();
    SendNow();
  }
}

void PageTimingMetricsSender::DidObserveLoadingBehavior(
    blink::WebLoadingBehaviorFlag behavior) {
  if (behavior & metadata_->behavior_flags)
    return;
  metadata_->behavior_flags |= behavior;
  EnsureSendTimer();
}

void PageTimingMetricsSender::Send(mojom::PageLoadTimingPtr timing) {
  if (last_timing_->Equals(*timing))
    return;

  // We want to make sure that each PageTimingMetricsSender is associated
  // with a distinct page navigation. Because we reset the object on commit,
  // we can trash last_timing_ on a provisional load before SendNow() fires.
  if (!last_timing_->navigation_start.is_null() &&
      last_timing_->navigation_start != timing->navigation_start) {
    return;
  }

  last_timing_ = std::move(timing);
  EnsureSendTimer();
}

void PageTimingMetricsSender::EnsureSendTimer() {
  if (!timer_->IsRunning()) {
    // Send the first IPC eagerly to make sure the receiving side knows we're
    // sending metrics as soon as possible.
    int delay_ms =
        have_sent_ipc_ ? kTimerDelayMillis : kInitialTimerDelayMillis;
    timer_->Start(
        FROM_HERE, base::TimeDelta::FromMilliseconds(delay_ms),
        base::Bind(&PageTimingMetricsSender::SendNow, base::Unretained(this)));
  }
}

void PageTimingMetricsSender::SendNow() {
  have_sent_ipc_ = true;
  sender_->SendTiming(last_timing_, metadata_);
}

}  // namespace page_load_metrics
