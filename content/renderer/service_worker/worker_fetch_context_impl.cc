// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/service_worker/worker_fetch_context_impl.h"

#include "content/child/child_thread_impl.h"
#include "content/child/request_extra_data.h"
#include "content/child/resource_dispatcher.h"
#include "content/child/thread_safe_sender.h"
#include "content/child/web_url_loader_impl.h"
#include "content/common/frame_messages.h"
#include "mojo/public/cpp/bindings/associated_binding.h"

namespace content {

WorkerFetchContextImpl::WorkerFetchContextImpl(
    mojom::WorkerURLLoaderFactoryProviderPtrInfo provider_info)
    : provider_info_(std::move(provider_info)),
      thread_safe_sender_(ChildThreadImpl::current()->thread_safe_sender()) {}

WorkerFetchContextImpl::~WorkerFetchContextImpl() {}

void WorkerFetchContextImpl::InitializeOnWorkerThread(
    base::SingleThreadTaskRunner* loading_task_runner) {
  DCHECK(loading_task_runner->RunsTasksInCurrentSequence());
  DCHECK(!resource_dispatcher_);
  DCHECK(!binding_);
  resource_dispatcher_ =
      base::MakeUnique<ResourceDispatcher>(nullptr, loading_task_runner);
  binding_ = base::MakeUnique<
      mojo::AssociatedBinding<mojom::ServiceWorkerWorkerClient>>(this);
  DCHECK(provider_info_.is_valid());
  provider_.Bind(std::move(provider_info_));
  mojom::ServiceWorkerWorkerClientAssociatedPtrInfo ptr_info;
  binding_->Bind(mojo::MakeRequest(&ptr_info));
  provider_->GetURLLoaderFactoryAndRegisterClient(
      mojo::MakeRequest(&url_loader_factory_), std::move(ptr_info),
      service_worker_provider_id_);
}

std::unique_ptr<blink::WebURLLoader> WorkerFetchContextImpl::CreateURLLoader() {
  return base::MakeUnique<content::WebURLLoaderImpl>(resource_dispatcher_.get(),
                                                     url_loader_factory_.get());
}

void WorkerFetchContextImpl::WillSendRequest(blink::WebURLRequest& request) {
  RequestExtraData* extra_data = new RequestExtraData();
  extra_data->set_service_worker_provider_id(service_worker_provider_id_);
  extra_data->set_render_frame_id(parent_frame_id_);
  extra_data->set_initiated_in_secure_context(is_secure_context_);
  request.SetExtraData(extra_data);

  if (!IsControlledByServiceWorker() &&
      request.GetServiceWorkerMode() !=
          blink::WebURLRequest::ServiceWorkerMode::kNone) {
    request.SetServiceWorkerMode(
        blink::WebURLRequest::ServiceWorkerMode::kForeign);
  }
}

bool WorkerFetchContextImpl::IsControlledByServiceWorker() const {
  return is_controlled_by_service_worker_ ||
         (controller_version_id_ != kInvalidServiceWorkerVersionId);
}

void WorkerFetchContextImpl::SetDataSaverEnabled(bool enabled) {
  is_data_saver_enabled_ = enabled;
}

bool WorkerFetchContextImpl::IsDataSaverEnabled() const {
  return is_data_saver_enabled_;
}

blink::WebURL WorkerFetchContextImpl::FirstPartyForCookies() const {
  return first_party_for_cookies_;
}

void WorkerFetchContextImpl::DidRunContentWithCertificateErrors(
    const blink::WebURL& url) {
  Send(new FrameHostMsg_DidRunContentWithCertificateErrors(parent_frame_id_,
                                                           url));
}

void WorkerFetchContextImpl::DidDisplayContentWithCertificateErrors(
    const blink::WebURL& url) {
  Send(new FrameHostMsg_DidDisplayContentWithCertificateErrors(parent_frame_id_,
                                                               url));
}

void WorkerFetchContextImpl::set_service_worker_provider_id(int id) {
  service_worker_provider_id_ = id;
}

void WorkerFetchContextImpl::set_is_controlled_by_service_worker(bool flag) {
  is_controlled_by_service_worker_ = flag;
}

void WorkerFetchContextImpl::set_parent_frame_id(int id) {
  parent_frame_id_ = id;
}

void WorkerFetchContextImpl::set_first_party_for_cookies(
    const blink::WebURL& first_party_for_cookies) {
  first_party_for_cookies_ = first_party_for_cookies;
}

void WorkerFetchContextImpl::set_is_secure_context(bool flag) {
  is_secure_context_ = flag;
}

void WorkerFetchContextImpl::SetControllerServiceWorker(
    int64_t controller_version_id) {
  controller_version_id_ = controller_version_id;
}

bool WorkerFetchContextImpl::Send(IPC::Message* message) {
  return thread_safe_sender_->Send(message);
}

}  // namespace content
