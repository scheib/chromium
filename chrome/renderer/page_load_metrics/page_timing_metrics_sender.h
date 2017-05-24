// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_PAGE_LOAD_METRICS_PAGE_TIMING_METRICS_SENDER_H_
#define CHROME_RENDERER_PAGE_LOAD_METRICS_PAGE_TIMING_METRICS_SENDER_H_

#include <memory>

#include "base/macros.h"
#include "chrome/common/page_load_metrics/page_load_timing.h"
#include "third_party/WebKit/public/platform/WebLoadingBehaviorFlag.h"

namespace base {
class Timer;
}  // namespace base

namespace page_load_metrics {

class PageTimingSender;

// PageTimingMetricsSender is responsible for sending page load timing metrics
// over IPC. PageTimingMetricsSender may coalesce sent IPCs in order to
// minimize IPC contention.
class PageTimingMetricsSender {
 public:
  PageTimingMetricsSender(std::unique_ptr<PageTimingSender> sender,
                          std::unique_ptr<base::Timer> timer,
                          mojom::PageLoadTimingPtr initial_timing);
  ~PageTimingMetricsSender();

  void DidObserveLoadingBehavior(blink::WebLoadingBehaviorFlag behavior);
  void Send(mojom::PageLoadTimingPtr timing);

 protected:
  base::Timer* timer() const { return timer_.get(); }

 private:
  void EnsureSendTimer();
  void SendNow();

  std::unique_ptr<PageTimingSender> sender_;
  std::unique_ptr<base::Timer> timer_;
  mojom::PageLoadTimingPtr last_timing_;

  // The the sender keep track of metadata as it comes in, because the sender is
  // scoped to a single committed load.
  mojom::PageLoadMetadataPtr metadata_;

  bool have_sent_ipc_ = false;

  DISALLOW_COPY_AND_ASSIGN(PageTimingMetricsSender);
};

}  // namespace page_load_metrics

#endif  // CHROME_RENDERER_PAGE_LOAD_METRICS_PAGE_TIMING_METRICS_SENDER_H_
