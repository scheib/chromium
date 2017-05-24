// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_TRACING_CHROME_TRACE_EVENT_AGENT_H_
#define SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_TRACING_CHROME_TRACE_EVENT_AGENT_H_

#include "base/memory/ref_counted.h"
#include "base/memory/ref_counted_memory.h"
#include "base/threading/thread_checker.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "services/resource_coordinator/public/cpp/resource_coordinator_export.h"
#include "services/resource_coordinator/public/interfaces/tracing/tracing.mojom.h"
#include "services/service_manager/public/cpp/connector.h"

namespace base {
class DictionaryValue;
}

namespace tracing {

class SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_EXPORT ChromeTraceEventAgent
    : public mojom::Agent {
 public:
  using MetadataGeneratorFunction =
      base::RepeatingCallback<std::unique_ptr<base::DictionaryValue>()>;

  static ChromeTraceEventAgent* GetInstance();

  void AddMetadataGeneratorFunction(MetadataGeneratorFunction generator);

 private:
  friend std::default_delete<ChromeTraceEventAgent>;  // For Testing
  friend class ChromeTraceEventAgentTest;             // For Testing

  explicit ChromeTraceEventAgent(mojom::AgentRegistryPtr agent_registry);
  ~ChromeTraceEventAgent() override;

  // mojom::Agent
  void StartTracing(const std::string& config,
                    mojom::RecorderPtr recorder,
                    const StartTracingCallback& callback) override;
  void StopAndFlush() override;
  void RequestClockSyncMarker(
      const std::string& sync_id,
      const RequestClockSyncMarkerCallback& callback) override;
  void RequestBufferStatus(
      const RequestBufferStatusCallback& callback) override;
  void GetCategories(const GetCategoriesCallback& callback) override;

  void OnTraceLogFlush(const scoped_refptr<base::RefCountedString>& events_str,
                       bool has_more_events);

  mojo::Binding<mojom::Agent> binding_;
  mojom::RecorderPtr recorder_;
  std::vector<MetadataGeneratorFunction> metadata_generator_functions_;
  bool trace_log_needs_me_ = false;

  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(ChromeTraceEventAgent);
};

}  // namespace tracing
#endif  // SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_TRACING_CHROME_TRACE_EVENT_AGENT_H_
