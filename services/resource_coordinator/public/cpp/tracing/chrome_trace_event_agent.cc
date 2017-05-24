// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/public/cpp/tracing/chrome_trace_event_agent.h"

#include "base/bind.h"
#include "base/memory/ref_counted.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_checker.h"
#include "base/trace_event/trace_log.h"
#include "base/values.h"
#include "services/service_manager/public/cpp/connector.h"

namespace {
tracing::ChromeTraceEventAgent* g_chrome_trace_event_agent;
}  // namespace

namespace tracing {

// static
ChromeTraceEventAgent* ChromeTraceEventAgent::GetInstance() {
  return g_chrome_trace_event_agent;
}

ChromeTraceEventAgent::ChromeTraceEventAgent(
    mojom::AgentRegistryPtr agent_registry)
    : binding_(this) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(!g_chrome_trace_event_agent);
  g_chrome_trace_event_agent = this;
  // agent_registry can be null in tests. The constructor is private and cannot
  // be directly used in non-test scenarios. GetOrCreateInstance makes sure that
  // the constructor is called with a non-null agent_registry.
  if (!agent_registry)
    return;
  agent_registry->RegisterAgent(binding_.CreateInterfacePtrAndBind(),
                                "traceEvents", mojom::TraceDataType::ARRAY,
                                false /* supports_explicit_clock_sync */);
}

ChromeTraceEventAgent::~ChromeTraceEventAgent() {
  DCHECK(!trace_log_needs_me_);
  g_chrome_trace_event_agent = nullptr;
}

void ChromeTraceEventAgent::AddMetadataGeneratorFunction(
    MetadataGeneratorFunction generator) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  metadata_generator_functions_.push_back(generator);
}

void ChromeTraceEventAgent::StartTracing(const std::string& config,
                                         mojom::RecorderPtr recorder,
                                         const StartTracingCallback& callback) {
  DCHECK(!recorder_);
  recorder_ = std::move(recorder);
  if (!base::trace_event::TraceLog::GetInstance()->IsEnabled()) {
    base::trace_event::TraceLog::GetInstance()->SetEnabled(
        base::trace_event::TraceConfig(config),
        base::trace_event::TraceLog::RECORDING_MODE);
  }
  callback.Run();
}

void ChromeTraceEventAgent::StopAndFlush() {
  base::trace_event::TraceLog::GetInstance()->SetDisabled();
  if (!recorder_)
    return;
  for (const auto& generator : metadata_generator_functions_) {
    auto metadata = generator.Run();
    if (metadata)
      recorder_->AddMetadata(std::move(metadata));
  }
  trace_log_needs_me_ = true;
  base::trace_event::TraceLog::GetInstance()->Flush(base::Bind(
      &ChromeTraceEventAgent::OnTraceLogFlush, base::Unretained(this)));
}

void ChromeTraceEventAgent::RequestClockSyncMarker(
    const std::string& sync_id,
    const RequestClockSyncMarkerCallback& callback) {
  NOTREACHED() << "This agent does not support explicit clock sync";
}

void ChromeTraceEventAgent::RequestBufferStatus(
    const RequestBufferStatusCallback& callback) {
  base::trace_event::TraceLogStatus status =
      base::trace_event::TraceLog::GetInstance()->GetStatus();
  callback.Run(status.event_capacity, status.event_count);
}

void ChromeTraceEventAgent::GetCategories(
    const GetCategoriesCallback& callback) {
  std::vector<std::string> category_vector;
  base::trace_event::TraceLog::GetInstance()->GetKnownCategoryGroups(
      &category_vector);
  callback.Run(base::JoinString(category_vector, ","));
}

void ChromeTraceEventAgent::OnTraceLogFlush(
    const scoped_refptr<base::RefCountedString>& events_str,
    bool has_more_events) {
  if (!events_str->data().empty())
    recorder_->AddChunk(events_str->data());
  if (!has_more_events) {
    trace_log_needs_me_ = false;
    recorder_.reset();
  }
}

}  // namespace tracing
