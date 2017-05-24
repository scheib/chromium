// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/page_load_metrics/metrics_render_frame_observer.h"

#include <string>

#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/page_load_metrics/page_load_metrics_messages.h"
#include "chrome/renderer/page_load_metrics/page_timing_metrics_sender.h"
#include "chrome/renderer/page_load_metrics/page_timing_sender.h"
#include "chrome/renderer/page_load_metrics/renderer_page_track_decider.h"
#include "chrome/renderer/searchbox/search_bouncer.h"
#include "content/public/common/associated_interface_provider.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/WebKit/public/web/WebDataSource.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebPerformance.h"
#include "url/gurl.h"

namespace page_load_metrics {

namespace {

base::TimeDelta ClampDelta(double event, double start) {
  if (event - start < 0)
    event = start;
  return base::Time::FromDoubleT(event) - base::Time::FromDoubleT(start);
}

class LegacyIPCPageTimingSender : public PageTimingSender {
 public:
  LegacyIPCPageTimingSender(content::RenderFrame* render_frame,
                            const int routing_id)
      : render_frame_(render_frame), routing_id_(routing_id) {}
  ~LegacyIPCPageTimingSender() override {}

  void SendTiming(const mojom::PageLoadTimingPtr& timing,
                  const mojom::PageLoadMetadataPtr& metadata) override {
    DCHECK(render_frame_);
    render_frame_->Send(
        new PageLoadMetricsMsg_TimingUpdated(routing_id_, *timing, *metadata));
  }

 private:
  content::RenderFrame* const render_frame_;
  const int routing_id_;
};

class MojoIPCPageTimingSender : public PageTimingSender {
 public:
  explicit MojoIPCPageTimingSender(content::RenderFrame* render_frame) {
    DCHECK(render_frame);
    render_frame->GetRemoteAssociatedInterfaces()->GetInterface(
        &page_load_metrics_);
  }
  ~MojoIPCPageTimingSender() override {}
  void SendTiming(const mojom::PageLoadTimingPtr& timing,
                  const mojom::PageLoadMetadataPtr& metadata) override {
    DCHECK(page_load_metrics_);
    page_load_metrics_->UpdateTiming(timing->Clone(), metadata->Clone());
  }

 private:
  // Use associated interface to make sure mojo messages are ordered with regard
  // to legacy IPC messages.
  mojom::PageLoadMetricsAssociatedPtr page_load_metrics_;
};

}  //  namespace

MetricsRenderFrameObserver::MetricsRenderFrameObserver(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {}

MetricsRenderFrameObserver::~MetricsRenderFrameObserver() {}

void MetricsRenderFrameObserver::DidChangePerformanceTiming() {
  SendMetrics();
}

void MetricsRenderFrameObserver::DidObserveLoadingBehavior(
    blink::WebLoadingBehaviorFlag behavior) {
  if (page_timing_metrics_sender_)
    page_timing_metrics_sender_->DidObserveLoadingBehavior(behavior);
}

void MetricsRenderFrameObserver::FrameDetached() {
  page_timing_metrics_sender_.reset();
}

void MetricsRenderFrameObserver::DidCommitProvisionalLoad(
    bool is_new_navigation,
    bool is_same_document_navigation) {
  // Same-document navigations (e.g. a navigation from a fragment link) aren't
  // full page loads, since they don't go to network to load the main HTML
  // resource. DidStartProvisionalLoad doesn't get invoked for same document
  // navigations, so we may still have an active page_timing_metrics_sender_ at
  // this point.
  if (is_same_document_navigation)
    return;

  // Make sure to release the sender for a previous navigation, if we have one.
  page_timing_metrics_sender_.reset();

  // We only create a PageTimingMetricsSender if the page meets the criteria for
  // sending and recording metrics. Once page_timing_metrics_sender_ is
  // non-null, we will send metrics for the current page at some later time, as
  // those metrics become available.
  if (ShouldSendMetrics()) {
    page_timing_metrics_sender_ = base::MakeUnique<PageTimingMetricsSender>(
        CreatePageTimingSender(), CreateTimer(), GetTiming());
  }
}

void MetricsRenderFrameObserver::SendMetrics() {
  if (!page_timing_metrics_sender_)
    return;
  if (HasNoRenderFrame())
    return;
  page_timing_metrics_sender_->Send(GetTiming());
}

bool MetricsRenderFrameObserver::ShouldSendMetrics() const {
  if (HasNoRenderFrame())
    return false;
  const blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  const blink::WebDocument& document = frame->GetDocument();
  return RendererPageTrackDecider(&document, frame->DataSource()).ShouldTrack();
}

mojom::PageLoadTimingPtr MetricsRenderFrameObserver::GetTiming() const {
  const blink::WebPerformance& perf =
      render_frame()->GetWebFrame()->Performance();

  mojom::PageLoadTimingPtr timing(CreatePageLoadTiming());
  double start = perf.NavigationStart();
  timing->navigation_start = base::Time::FromDoubleT(start);
  if (perf.ResponseStart() > 0.0)
    timing->response_start = ClampDelta(perf.ResponseStart(), start);
  if (perf.DomContentLoadedEventStart() > 0.0) {
    timing->document_timing->dom_content_loaded_event_start =
        ClampDelta(perf.DomContentLoadedEventStart(), start);
  }
  if (perf.LoadEventStart() > 0.0) {
    timing->document_timing->load_event_start =
        ClampDelta(perf.LoadEventStart(), start);
  }
  if (perf.FirstLayout() > 0.0)
    timing->document_timing->first_layout =
        ClampDelta(perf.FirstLayout(), start);
  if (perf.FirstPaint() > 0.0)
    timing->paint_timing->first_paint = ClampDelta(perf.FirstPaint(), start);
  if (perf.FirstTextPaint() > 0.0) {
    timing->paint_timing->first_text_paint =
        ClampDelta(perf.FirstTextPaint(), start);
  }
  if (perf.FirstImagePaint() > 0.0) {
    timing->paint_timing->first_image_paint =
        ClampDelta(perf.FirstImagePaint(), start);
  }
  if (perf.FirstContentfulPaint() > 0.0) {
    timing->paint_timing->first_contentful_paint =
        ClampDelta(perf.FirstContentfulPaint(), start);
  }
  if (perf.FirstMeaningfulPaint() > 0.0) {
    timing->paint_timing->first_meaningful_paint =
        ClampDelta(perf.FirstMeaningfulPaint(), start);
  }
  if (perf.ParseStart() > 0.0)
    timing->parse_timing->parse_start = ClampDelta(perf.ParseStart(), start);
  if (perf.ParseStop() > 0.0)
    timing->parse_timing->parse_stop = ClampDelta(perf.ParseStop(), start);
  if (timing->parse_timing->parse_start) {
    // If we started parsing, record all parser durations such as the amount of
    // time blocked on script load, even if those values are zero.
    timing->parse_timing->parse_blocked_on_script_load_duration =
        base::TimeDelta::FromSecondsD(perf.ParseBlockedOnScriptLoadDuration());
    timing->parse_timing
        ->parse_blocked_on_script_load_from_document_write_duration =
        base::TimeDelta::FromSecondsD(
            perf.ParseBlockedOnScriptLoadFromDocumentWriteDuration());
    timing->parse_timing->parse_blocked_on_script_execution_duration =
        base::TimeDelta::FromSecondsD(
            perf.ParseBlockedOnScriptExecutionDuration());
    timing->parse_timing
        ->parse_blocked_on_script_execution_from_document_write_duration =
        base::TimeDelta::FromSecondsD(
            perf.ParseBlockedOnScriptExecutionFromDocumentWriteDuration());
  }

  if (perf.AuthorStyleSheetParseDurationBeforeFCP() > 0.0) {
    timing->style_sheet_timing->author_style_sheet_parse_duration_before_fcp =
        base::TimeDelta::FromSecondsD(
            perf.AuthorStyleSheetParseDurationBeforeFCP());
  }
  if (perf.UpdateStyleDurationBeforeFCP() > 0.0) {
    timing->style_sheet_timing->update_style_duration_before_fcp =
        base::TimeDelta::FromSecondsD(perf.UpdateStyleDurationBeforeFCP());
  }
  return timing;
}

std::unique_ptr<base::Timer> MetricsRenderFrameObserver::CreateTimer() const {
  return base::WrapUnique(new base::OneShotTimer);
}

std::unique_ptr<PageTimingSender>
MetricsRenderFrameObserver::CreatePageTimingSender() {
  if (base::FeatureList::IsEnabled(features::kPageLoadMetricsMojofication)) {
    return base::WrapUnique<PageTimingSender>(
        new MojoIPCPageTimingSender(render_frame()));
  }
  return base::WrapUnique<PageTimingSender>(
      new LegacyIPCPageTimingSender(render_frame(), routing_id()));
}

bool MetricsRenderFrameObserver::HasNoRenderFrame() const {
  bool no_frame = !render_frame() || !render_frame()->GetWebFrame();
  DCHECK(!no_frame);
  return no_frame;
}

void MetricsRenderFrameObserver::OnDestruct() {
  delete this;
}

}  // namespace page_load_metrics
