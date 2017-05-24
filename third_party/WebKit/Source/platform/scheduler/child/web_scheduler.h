// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_WEBKIT_SOURCE_PLATFORM_SCHEDULER_RENDERER_WEB_SCHEDULER_H_
#define THIRD_PARTY_WEBKIT_SOURCE_PLATFORM_SCHEDULER_RENDERER_WEB_SCHEDULER_H_

#include "platform/scheduler/renderer/web_view_scheduler.h"
#include "public/platform/WebString.h"
#include "public/platform/WebThread.h"
#include "public/platform/WebTraceLocation.h"
#include "public/platform/scheduler/renderer/renderer_scheduler.h"

#include <memory>

namespace blink {

class WebTaskRunner;

// This class is used to submit tasks and pass other information from Blink to
// the platform's scheduler.
// TODO(skyostil): Replace this class with RendererScheduler.
class PLATFORM_EXPORT WebScheduler {
 public:
  class PLATFORM_EXPORT InterventionReporter {
   public:
    virtual ~InterventionReporter() {}

    // The scheduler has performed an intervention, described by |message|,
    // which should be reported to the developer.
    virtual void ReportIntervention(const WebString& message) = 0;
  };

  virtual ~WebScheduler() {}

  // Called to prevent any more pending tasks from running. Must be called on
  // the associated WebThread.
  virtual void Shutdown() = 0;

  // Returns true if there is high priority work pending on the associated
  // WebThread and the caller should yield to let the scheduler service that
  // work.  Must be called on the associated WebThread.
  virtual bool ShouldYieldForHighPriorityWork() = 0;

  // Returns true if a currently running idle task could exceed its deadline
  // without impacting user experience too much. This should only be used if
  // there is a task which cannot be pre-empted and is likely to take longer
  // than the largest expected idle task deadline. It should NOT be polled to
  // check whether more work can be performed on the current idle task after
  // its deadline has expired - post a new idle task for the continuation of
  // the work in this case.
  // Must be called from the associated WebThread.
  virtual bool CanExceedIdleDeadlineIfRequired() = 0;

  // Schedule an idle task to run the associated WebThread. For non-critical
  // tasks which may be reordered relative to other task types and may be
  // starved for an arbitrarily long time if no idle time is available.
  // Takes ownership of |IdleTask|. Can be called from any thread.
  virtual void PostIdleTask(const WebTraceLocation&, WebThread::IdleTask*) = 0;

  // Like postIdleTask but guarantees that the posted task will not run
  // nested within an already-running task. Posting an idle task as
  // non-nestable may not affect when the task gets run, or it could
  // make it run later than it normally would, but it won't make it
  // run earlier than it normally would.
  virtual void PostNonNestableIdleTask(const WebTraceLocation&,
                                       WebThread::IdleTask*) = 0;

  // Returns a WebTaskRunner for loading tasks. Can be called from any thread.
  virtual WebTaskRunner* LoadingTaskRunner() = 0;

  // Returns a WebTaskRunner for timer tasks. Can be called from any thread.
  virtual WebTaskRunner* TimerTaskRunner() = 0;

  // Returns a WebTaskRunner for compositor tasks. This is intended only to be
  // used by specific animation and rendering related tasks (e.g. animated GIFS)
  // and should not generally be used.
  virtual WebTaskRunner* CompositorTaskRunner() = 0;

  // Creates a new WebViewScheduler for a given WebView. Must be called from
  // the associated WebThread.
  virtual std::unique_ptr<WebViewScheduler> CreateWebViewScheduler(
      InterventionReporter*,
      WebViewScheduler::WebViewSchedulerSettings*) = 0;

  // Suspends the timer queue and increments the timer queue suspension count.
  // May only be called from the main thread.
  virtual void SuspendTimerQueue() = 0;

  // Decrements the timer queue suspension count and re-enables the timer queue
  // if the suspension count is zero and the current scheduler policy allows it.
  virtual void ResumeTimerQueue() = 0;

  // Tells the scheduler that a navigation task is pending.
  // TODO(alexclarke): Long term should this be a task trait?
  virtual void AddPendingNavigation(
      scheduler::RendererScheduler::NavigatingFrameType) = 0;

  // Tells the scheduler that a navigation task is no longer pending.
  virtual void RemovePendingNavigation(
      scheduler::RendererScheduler::NavigatingFrameType) = 0;

  // Test helpers.

  // Return a reference to an underlying RendererScheduler object.
  // Can be null if there is no underlying RendererScheduler
  // (e.g. worker threads).
  virtual scheduler::RendererScheduler* GetRendererSchedulerForTest() {
    return nullptr;
  }

#ifdef INSIDE_BLINK
  // Helpers for posting bound functions as tasks.
  typedef Function<void(double deadline_seconds)> IdleTask;

  void PostIdleTask(const WebTraceLocation&, std::unique_ptr<IdleTask>);
  void PostNonNestableIdleTask(const WebTraceLocation&,
                               std::unique_ptr<IdleTask>);
#endif
};

}  // namespace blink

#endif  // THIRD_PARTY_WEBKIT_SOURCE_PLATFORM_SCHEDULER_RENDERER_WEB_SCHEDULER_H_
