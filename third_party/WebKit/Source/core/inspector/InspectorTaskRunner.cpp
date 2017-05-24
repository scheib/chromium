// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/inspector/InspectorTaskRunner.h"

namespace blink {

InspectorTaskRunner::IgnoreInterruptsScope::IgnoreInterruptsScope(
    InspectorTaskRunner* task_runner)
    : was_ignoring_(task_runner->ignore_interrupts_),
      task_runner_(task_runner) {
  // There may be nested scopes e.g. when tasks are being executed on XHR
  // breakpoint.
  task_runner_->ignore_interrupts_ = true;
}

InspectorTaskRunner::IgnoreInterruptsScope::~IgnoreInterruptsScope() {
  task_runner_->ignore_interrupts_ = was_ignoring_;
}

InspectorTaskRunner::InspectorTaskRunner()
    : ignore_interrupts_(false), killed_(false) {}

InspectorTaskRunner::~InspectorTaskRunner() {}

void InspectorTaskRunner::AppendTask(std::unique_ptr<Task> task) {
  MutexLocker lock(mutex_);
  if (killed_)
    return;
  queue_.push_back(std::move(task));
  condition_.Signal();
}

std::unique_ptr<InspectorTaskRunner::Task> InspectorTaskRunner::TakeNextTask(
    InspectorTaskRunner::WaitMode wait_mode) {
  MutexLocker lock(mutex_);
  bool timed_out = false;

  static double infinite_time = std::numeric_limits<double>::max();
  double absolute_time = wait_mode == kWaitForTask ? infinite_time : 0.0;
  while (!killed_ && !timed_out && queue_.IsEmpty())
    timed_out = !condition_.TimedWait(mutex_, absolute_time);
  DCHECK(!timed_out || absolute_time != infinite_time);

  if (killed_ || timed_out)
    return nullptr;

  SECURITY_DCHECK(!queue_.IsEmpty());
  return queue_.TakeFirst();
}

void InspectorTaskRunner::Kill() {
  MutexLocker lock(mutex_);
  killed_ = true;
  condition_.Broadcast();
}

void InspectorTaskRunner::InterruptAndRunAllTasksDontWait(
    v8::Isolate* isolate) {
  isolate->RequestInterrupt(&V8InterruptCallback, this);
}

void InspectorTaskRunner::RunAllTasksDontWait() {
  while (true) {
    std::unique_ptr<Task> task = TakeNextTask(kDontWaitForTask);
    if (!task)
      return;
    (*task)();
  }
}

void InspectorTaskRunner::V8InterruptCallback(v8::Isolate*, void* data) {
  InspectorTaskRunner* runner = static_cast<InspectorTaskRunner*>(data);
  if (runner->ignore_interrupts_)
    return;
  runner->RunAllTasksDontWait();
}

}  // namespace blink
