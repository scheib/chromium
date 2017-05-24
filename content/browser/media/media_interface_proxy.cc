// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/media/media_interface_proxy.h"

#include <string>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/common/content_client.h"
#include "content/public/common/service_manager_connection.h"
#include "media/mojo/features.h"
#include "media/mojo/interfaces/media_service.mojom.h"
#include "media/mojo/services/media_interface_provider.h"
#include "services/service_manager/public/cpp/connector.h"

#if BUILDFLAG(ENABLE_MOJO_CDM)
#include "content/public/browser/browser_context.h"
#include "content/public/browser/provision_fetcher_impl.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "net/url_request/url_request_context_getter.h"
#endif

namespace content {

MediaInterfaceProxy::MediaInterfaceProxy(
    RenderFrameHost* render_frame_host,
    media::mojom::InterfaceFactoryRequest request,
    const base::Closure& error_handler)
    : render_frame_host_(render_frame_host),
      binding_(this, std::move(request)) {
  DVLOG(1) << __FUNCTION__;
  DCHECK(render_frame_host_);
  DCHECK(!error_handler.is_null());

  binding_.set_connection_error_handler(error_handler);

  // |interface_factory_ptr_| will be lazily connected in
  // GetMediaInterfaceFactory().
}

MediaInterfaceProxy::~MediaInterfaceProxy() {
  DVLOG(1) << __FUNCTION__;
  DCHECK(thread_checker_.CalledOnValidThread());
}

void MediaInterfaceProxy::CreateAudioDecoder(
    media::mojom::AudioDecoderRequest request) {
  DCHECK(thread_checker_.CalledOnValidThread());
  GetMediaInterfaceFactory()->CreateAudioDecoder(std::move(request));
}

void MediaInterfaceProxy::CreateVideoDecoder(
    media::mojom::VideoDecoderRequest request) {
  DCHECK(thread_checker_.CalledOnValidThread());
  GetMediaInterfaceFactory()->CreateVideoDecoder(std::move(request));
}

void MediaInterfaceProxy::CreateRenderer(
    const std::string& audio_device_id,
    media::mojom::RendererRequest request) {
  DCHECK(thread_checker_.CalledOnValidThread());
  GetMediaInterfaceFactory()->CreateRenderer(audio_device_id,
                                             std::move(request));
}

void MediaInterfaceProxy::CreateCdm(
    media::mojom::ContentDecryptionModuleRequest request) {
  DCHECK(thread_checker_.CalledOnValidThread());
  GetMediaInterfaceFactory()->CreateCdm(std::move(request));
}

media::mojom::InterfaceFactory*
MediaInterfaceProxy::GetMediaInterfaceFactory() {
  DVLOG(1) << __FUNCTION__;
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!interface_factory_ptr_)
    ConnectToService();

  DCHECK(interface_factory_ptr_);

  return interface_factory_ptr_.get();
}

void MediaInterfaceProxy::OnConnectionError() {
  DVLOG(1) << __FUNCTION__;
  DCHECK(thread_checker_.CalledOnValidThread());

  interface_factory_ptr_.reset();
}

void MediaInterfaceProxy::ConnectToService() {
  DVLOG(1) << __FUNCTION__;
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!interface_factory_ptr_);

  // Register frame services.
  service_manager::mojom::InterfaceProviderPtr interfaces;
  // TODO(xhwang): Replace this InterfaceProvider with a dedicated media host
  // interface. See http://crbug.com/660573
  auto provider = base::MakeUnique<media::MediaInterfaceProvider>(
      mojo::MakeRequest(&interfaces));
#if BUILDFLAG(ENABLE_MOJO_CDM)
  // TODO(slan): Wrap these into a RenderFrame specific ProvisionFetcher impl.
  net::URLRequestContextGetter* context_getter =
      BrowserContext::GetDefaultStoragePartition(
          render_frame_host_->GetProcess()->GetBrowserContext())
          ->GetURLRequestContext();
  provider->registry()->AddInterface(
      base::Bind(&ProvisionFetcherImpl::Create, context_getter));
#endif  // BUILDFLAG(ENABLE_MOJO_CDM)
  GetContentClient()->browser()->ExposeInterfacesToMediaService(
      provider->registry(), render_frame_host_);

  media_registries_.push_back(std::move(provider));

  // TODO(slan): Use the BrowserContext Connector instead. See crbug.com/638950.
  media::mojom::MediaServicePtr media_service;
  service_manager::Connector* connector =
      ServiceManagerConnection::GetForProcess()->GetConnector();
  connector->BindInterface("media", &media_service);
  media_service->CreateInterfaceFactory(MakeRequest(&interface_factory_ptr_),
                                        std::move(interfaces));
  interface_factory_ptr_.set_connection_error_handler(base::Bind(
      &MediaInterfaceProxy::OnConnectionError, base::Unretained(this)));
}

}  // namespace content
