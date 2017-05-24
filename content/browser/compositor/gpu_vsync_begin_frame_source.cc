// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/compositor/gpu_vsync_begin_frame_source.h"

namespace content {

GpuVSyncBeginFrameSource::GpuVSyncBeginFrameSource(
    GpuVSyncControl* vsync_control)
    : cc::ExternalBeginFrameSource(this),
      vsync_control_(vsync_control),
      needs_begin_frames_(false),
      next_sequence_number_(cc::BeginFrameArgs::kStartingFrameNumber) {
  DCHECK(vsync_control);
}

GpuVSyncBeginFrameSource::~GpuVSyncBeginFrameSource() = default;

void GpuVSyncBeginFrameSource::OnVSync(base::TimeTicks timestamp,
                                       base::TimeDelta interval) {
  if (!needs_begin_frames_)
    return;

  base::TimeTicks now = base::TimeTicks::Now();
  base::TimeTicks deadline = now.SnappedToNextTick(timestamp, interval);

  TRACE_EVENT1("cc", "GpuVSyncBeginFrameSource::OnVSync", "latency",
               (now - timestamp).ToInternalValue());

  next_sequence_number_++;
  OnBeginFrame(cc::BeginFrameArgs::Create(
      BEGINFRAME_FROM_HERE, source_id(), next_sequence_number_, timestamp,
      deadline, interval, cc::BeginFrameArgs::NORMAL));
}

void GpuVSyncBeginFrameSource::OnNeedsBeginFrames(bool needs_begin_frames) {
  needs_begin_frames_ = needs_begin_frames;
  vsync_control_->SetNeedsVSync(needs_begin_frames);
}

}  // namespace content
