// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WorkerFetchContext_h
#define WorkerFetchContext_h

#include <memory>
#include "core/CoreExport.h"
#include "core/loader/BaseFetchContext.h"
#include "platform/wtf/Forward.h"

namespace blink {

class ResourceFetcher;
class WebTaskRunner;
class WebURLLoader;
class WebWorkerFetchContext;
class WorkerGlobalScope;
class WorkerClients;

CORE_EXPORT void ProvideWorkerFetchContextToWorker(
    WorkerClients*,
    std::unique_ptr<WebWorkerFetchContext>);

// The WorkerFetchContext is a FetchContext for workers (dedicated, shared and
// service workers). This class is used only when off-main-thread-fetch is
// enabled, and is still under development.
// TODO(horo): Implement all methods of FetchContext. crbug.com/443374
class WorkerFetchContext final : public BaseFetchContext {
 public:
  static WorkerFetchContext* Create(WorkerGlobalScope&);
  virtual ~WorkerFetchContext();

  ResourceFetcher* GetResourceFetcher();
  KURL FirstPartyForCookies() const;

  // BaseFetchContext implementation:
  ContentSettingsClient* GetContentSettingsClient() const override;
  Settings* GetSettings() const override;
  SubresourceFilter* GetSubresourceFilter() const override;
  SecurityContext* GetParentSecurityContext() const override;
  bool ShouldBlockRequestByInspector(const ResourceRequest&) const override;
  void DispatchDidBlockRequest(const ResourceRequest&,
                               const FetchInitiatorInfo&,
                               ResourceRequestBlockedReason) const override;
  void ReportLocalLoadFailed(const KURL&) const override;
  bool ShouldBypassMainWorldCSP() const override;
  bool IsSVGImageChromeClient() const override;
  void CountUsage(UseCounter::Feature) const override;
  void CountDeprecation(UseCounter::Feature) const override;
  bool ShouldBlockFetchByMixedContentCheck(
      const ResourceRequest&,
      const KURL&,
      SecurityViolationReportingPolicy) const override;

  // FetchContext implementation:
  // TODO(horo): Implement more methods.
  std::unique_ptr<WebURLLoader> CreateURLLoader() override;
  void PrepareRequest(ResourceRequest&, RedirectType) override;
  bool IsControlledByServiceWorker() const override;
  RefPtr<WebTaskRunner> LoadingTaskRunner() const override;

  void AddAdditionalRequestHeaders(ResourceRequest&,
                                   FetchResourceType) override;
  void DispatchDidReceiveResponse(unsigned long identifier,
                                  const ResourceResponse&,
                                  WebURLRequest::FrameType,
                                  WebURLRequest::RequestContext,
                                  Resource*,
                                  ResourceResponseType) override;
  void AddResourceTiming(const ResourceTimingInfo&) override;
  void PopulateResourceRequest(const KURL&,
                               Resource::Type,
                               const ClientHintsPreferences&,
                               const FetchParameters::ResourceWidth&,
                               const ResourceLoaderOptions&,
                               SecurityViolationReportingPolicy,
                               ResourceRequest&) override;
  void SetFirstPartyCookieAndRequestorOrigin(ResourceRequest&) override;

  DECLARE_VIRTUAL_TRACE();

 private:
  WorkerFetchContext(WorkerGlobalScope&,
                     std::unique_ptr<WebWorkerFetchContext>);

  Member<WorkerGlobalScope> worker_global_scope_;
  std::unique_ptr<WebWorkerFetchContext> web_context_;
  Member<ResourceFetcher> resource_fetcher_;
  RefPtr<WebTaskRunner> loading_task_runner_;
};

}  // namespace blink

#endif  // WorkerFetchContext_h
