// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_network_delegate.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_bypass_stats.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_config.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_configurator.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_data.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_io_data.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_request_options.h"
#include "components/data_reduction_proxy/core/browser/data_use_group.h"
#include "components/data_reduction_proxy/core/browser/data_use_group_provider.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_headers.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_params.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_util.h"
#include "components/data_reduction_proxy/core/common/lofi_decider.h"
#include "net/base/load_flags.h"
#include "net/base/mime_util.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/nqe/effective_connection_type.h"
#include "net/nqe/network_quality_estimator.h"
#include "net/proxy/proxy_info.h"
#include "net/proxy/proxy_server.h"
#include "net/proxy/proxy_service.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_status.h"
#include "url/gurl.h"

namespace data_reduction_proxy {

namespace {

// |lofi_low_header_added| is set to true iff Lo-Fi "q=low" request header can
// be added to the Chrome proxy headers.
// |received_content_length| is the number of prefilter bytes received.
// |original_content_length| is the length of resource if accessed directly
// without data saver proxy.
// |freshness_lifetime| contains information on how long the resource will be
// fresh for and how long is the usability.
void RecordContentLengthHistograms(bool lofi_low_header_added,
                                   bool is_https,
                                   bool is_video,
                                   int64_t received_content_length,
                                   int64_t original_content_length,
                                   const base::TimeDelta& freshness_lifetime) {
  // Add the current resource to these histograms only when a valid
  // X-Original-Content-Length header is present.
  if (original_content_length >= 0) {
    UMA_HISTOGRAM_COUNTS_1M("Net.HttpContentLengthWithValidOCL",
                            received_content_length);
    UMA_HISTOGRAM_COUNTS_1M("Net.HttpOriginalContentLengthWithValidOCL",
                            original_content_length);
    UMA_HISTOGRAM_COUNTS_1M("Net.HttpContentLengthDifferenceWithValidOCL",
                            original_content_length - received_content_length);

    // Populate Lo-Fi content length histograms.
    if (lofi_low_header_added) {
      UMA_HISTOGRAM_COUNTS_1M("Net.HttpContentLengthWithValidOCL.LoFiOn",
                              received_content_length);
      UMA_HISTOGRAM_COUNTS_1M(
          "Net.HttpOriginalContentLengthWithValidOCL.LoFiOn",
          original_content_length);
      UMA_HISTOGRAM_COUNTS_1M(
          "Net.HttpContentLengthDifferenceWithValidOCL.LoFiOn",
          original_content_length - received_content_length);
    }

  } else {
    // Presume the original content length is the same as the received content
    // length if the X-Original-Content-Header is not present.
    original_content_length = received_content_length;
  }
  UMA_HISTOGRAM_COUNTS_1M("Net.HttpContentLength", received_content_length);
  if (is_https) {
    UMA_HISTOGRAM_COUNTS_1M("Net.HttpContentLength.Https",
                            received_content_length);
  } else {
    UMA_HISTOGRAM_COUNTS_1M("Net.HttpContentLength.Http",
                            received_content_length);
  }
  if (is_video) {
    UMA_HISTOGRAM_COUNTS_1M("Net.HttpContentLength.Video",
                            received_content_length);
  }
  UMA_HISTOGRAM_COUNTS_1M("Net.HttpOriginalContentLength",
                          original_content_length);
  UMA_HISTOGRAM_COUNTS_1M("Net.HttpContentLengthDifference",
                          original_content_length - received_content_length);
  UMA_HISTOGRAM_CUSTOM_COUNTS("Net.HttpContentFreshnessLifetime",
                              freshness_lifetime.InSeconds(),
                              base::TimeDelta::FromHours(1).InSeconds(),
                              base::TimeDelta::FromDays(30).InSeconds(),
                              100);
  if (freshness_lifetime.InSeconds() <= 0)
    return;
  UMA_HISTOGRAM_COUNTS_1M("Net.HttpContentLengthCacheable",
                          received_content_length);
  if (freshness_lifetime.InHours() < 4)
    return;
  UMA_HISTOGRAM_COUNTS_1M("Net.HttpContentLengthCacheable4Hours",
                          received_content_length);

  if (freshness_lifetime.InHours() < 24)
    return;
  UMA_HISTOGRAM_COUNTS_1M("Net.HttpContentLengthCacheable24Hours",
                          received_content_length);
}

// Estimate the size of the original headers of |request|. If |used_drp| is
// true, then it's assumed that the original request would have used HTTP/1.1,
// otherwise it assumes that the original request would have used the same
// protocol as |request| did. This is to account for stuff like HTTP/2 header
// compression.
// TODO(rajendrant): Remove this method when data use ascriber observers are
// used to record the per-site data usage.
int64_t EstimateOriginalHeaderBytes(const net::URLRequest& request,
                                    bool used_drp) {
  if (used_drp) {
    // TODO(sclittle): Remove headers added by Data Reduction Proxy when
    // computing original size. https://crbug.com/535701.
    return request.response_headers()->raw_headers().size();
  }
  return std::max<int64_t>(0, request.GetTotalReceivedBytes() -
                                  request.received_response_content_length());
}

// Given a |request| that went through the Data Reduction Proxy if |used_drp| is
// true, this function estimates how many bytes would have been received if the
// response had been received directly from the origin without any data saver
// optimizations.
// TODO(rajendrant): Remove this method when data use ascriber observers are
// used to record the per-site data usage.
int64_t EstimateOriginalReceivedBytes(const net::URLRequest& request,
                                      bool used_drp,
                                      const LoFiDecider* lofi_decider) {
  if (request.was_cached() || !request.response_headers())
    return request.GetTotalReceivedBytes();

  if (lofi_decider) {
    if (lofi_decider->IsClientLoFiAutoReloadRequest(request))
      return 0;

    int64_t first, last, length;
    if (lofi_decider->IsClientLoFiImageRequest(request) &&
        request.response_headers()->GetContentRangeFor206(&first, &last,
                                                          &length) &&
        length > request.received_response_content_length()) {
      return EstimateOriginalHeaderBytes(request, used_drp) + length;
    }
  }

  return used_drp ? EstimateOriginalHeaderBytes(request, used_drp) +
                        util::CalculateEffectiveOCL(request)
                  : request.GetTotalReceivedBytes();
}

// Verifies that the chrome proxy related request headers are set correctly.
// |via_chrome_proxy| is true if the request is being fetched via Chrome Data
// Saver proxy.
void VerifyHttpRequestHeaders(bool via_chrome_proxy,
                              const net::HttpRequestHeaders& headers) {
  if (via_chrome_proxy) {
    DCHECK(headers.HasHeader(chrome_proxy_header()));
    DCHECK(headers.HasHeader(chrome_proxy_ect_header()));
  } else {
    DCHECK(!headers.HasHeader(chrome_proxy_header()));
    DCHECK(!headers.HasHeader(chrome_proxy_accept_transform_header()));
    DCHECK(!headers.HasHeader(chrome_proxy_ect_header()));
  }
}

}  // namespace

DataReductionProxyNetworkDelegate::DataReductionProxyNetworkDelegate(
    std::unique_ptr<net::NetworkDelegate> network_delegate,
    DataReductionProxyConfig* config,
    DataReductionProxyRequestOptions* request_options,
    const DataReductionProxyConfigurator* configurator)
    : LayeredNetworkDelegate(std::move(network_delegate)),
      data_reduction_proxy_config_(config),
      data_reduction_proxy_bypass_stats_(nullptr),
      data_reduction_proxy_request_options_(request_options),
      data_reduction_proxy_io_data_(nullptr),
      configurator_(configurator) {
  DCHECK(data_reduction_proxy_config_);
  DCHECK(data_reduction_proxy_request_options_);
  DCHECK(configurator_);
}

DataReductionProxyNetworkDelegate::~DataReductionProxyNetworkDelegate() {
}

void DataReductionProxyNetworkDelegate::InitIODataAndUMA(
    DataReductionProxyIOData* io_data,
    DataReductionProxyBypassStats* bypass_stats) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(bypass_stats);
  data_reduction_proxy_io_data_ = io_data;
  data_reduction_proxy_bypass_stats_ = bypass_stats;
}

void DataReductionProxyNetworkDelegate::OnBeforeURLRequestInternal(
    net::URLRequest* request,
    const net::CompletionCallback& callback,
    GURL* new_url) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (data_use_group_provider_) {
    // Creates and initializes a |DataUseGroup| for the |request| if it does not
    // exist. Even though we do not use the |DataUseGroup| here, we want to
    // associate one with a request as early as possible in case the frame
    // associated with the request goes away before the request is completed.
    scoped_refptr<DataUseGroup> data_use_group =
        data_use_group_provider_->GetDataUseGroup(request);
    data_use_group->Initialize();
  }

  // |data_reduction_proxy_io_data_| can be NULL for Webview.
  if (data_reduction_proxy_io_data_ &&
      (request->load_flags() & net::LOAD_MAIN_FRAME_DEPRECATED)) {
    data_reduction_proxy_io_data_->SetLoFiModeActiveOnMainFrame(false);
  }
}

void DataReductionProxyNetworkDelegate::OnBeforeStartTransactionInternal(
    net::URLRequest* request,
    const net::CompletionCallback& callback,
    net::HttpRequestHeaders* headers) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!data_reduction_proxy_io_data_)
    return;
  if (!data_reduction_proxy_io_data_->IsEnabled())
    return;

  if (request->url().SchemeIsCryptographic() ||
      !request->url().SchemeIsHTTPOrHTTPS()) {
    return;
  }

  if (data_reduction_proxy_io_data_->resource_type_provider()) {
    // Sets content type of |request| in the resource type provider, so it can
    // be later used for determining the proxy that should be used for fetching
    // |request|.
    data_reduction_proxy_io_data_->resource_type_provider()->SetContentType(
        *request);
  }

  if (data_reduction_proxy_io_data_->lofi_decider()) {
    data_reduction_proxy_io_data_->lofi_decider()
        ->MaybeSetAcceptTransformHeader(
            *request, data_reduction_proxy_config_->lofi_off(), headers);
  }

  MaybeAddChromeProxyECTHeader(headers, *request);
}

void DataReductionProxyNetworkDelegate::OnBeforeSendHeadersInternal(
    net::URLRequest* request,
    const net::ProxyInfo& proxy_info,
    const net::ProxyRetryInfoMap& proxy_retry_info,
    net::HttpRequestHeaders* headers) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(data_reduction_proxy_config_);
  DCHECK(request);

  // If there was a redirect or request bypass, use the same page ID for both
  // requests. As long as the session ID has not changed. Re-issued requests
  // and client redirects will be assigned a new page ID as they are different
  // URLRequests.
  DataReductionProxyData* data = DataReductionProxyData::GetData(*request);
  base::Optional<uint64_t> page_id;
  if (data && data->session_key() ==
                  data_reduction_proxy_request_options_->GetSecureSession()) {
    page_id = data->page_id();
  }

  // Reset |request|'s DataReductionProxyData.
  DataReductionProxyData::ClearData(request);

  if (params::IsIncludedInHoldbackFieldTrial()) {
    if (!WasEligibleWithoutHoldback(*request, proxy_info, proxy_retry_info))
      return;
    // For the holdback field trial, still log UMA as if the proxy was used.
    data = DataReductionProxyData::GetDataAndCreateIfNecessary(request);
    if (data)
      data->set_used_data_reduction_proxy(true);

    headers->RemoveHeader(chrome_proxy_header());
    VerifyHttpRequestHeaders(false, *headers);
    return;
  }

  bool using_data_reduction_proxy = true;
  // The following checks rule out direct, invalid, and other connection types.
  if (!proxy_info.is_http() && !proxy_info.is_https() &&
      !proxy_info.is_quic()) {
    using_data_reduction_proxy = false;
  } else if (proxy_info.proxy_server().host_port_pair().IsEmpty()) {
    using_data_reduction_proxy = false;
  } else if (!data_reduction_proxy_config_->IsDataReductionProxy(
                 proxy_info.proxy_server(), nullptr)) {
    using_data_reduction_proxy = false;
  }

  LoFiDecider* lofi_decider = nullptr;
  if (data_reduction_proxy_io_data_)
    lofi_decider = data_reduction_proxy_io_data_->lofi_decider();

  if (!using_data_reduction_proxy) {
    if (lofi_decider) {
      // If not using the data reduction proxy, strip the
      // Chrome-Proxy-Accept-Transform header.
      lofi_decider->RemoveAcceptTransformHeader(headers);
    }
    RemoveChromeProxyECTHeader(headers);
    headers->RemoveHeader(chrome_proxy_header());
    VerifyHttpRequestHeaders(false, *headers);
    return;
  }

  // Retrieves DataReductionProxyData from a request, creating a new instance
  // if needed.
  data = DataReductionProxyData::GetDataAndCreateIfNecessary(request);
  if (data) {
    data->set_used_data_reduction_proxy(true);
    // Only set GURL, NQE and session key string for main frame requests since
    // they are not needed for sub-resources.
    if (request->load_flags() & net::LOAD_MAIN_FRAME_DEPRECATED) {
      data->set_session_key(
          data_reduction_proxy_request_options_->GetSecureSession());
      data->set_request_url(request->url());
      if (request->context()->network_quality_estimator()) {
        data->set_effective_connection_type(request->context()
                                                ->network_quality_estimator()
                                                ->GetEffectiveConnectionType());
      }
    }
  }

  if (data_reduction_proxy_io_data_ &&
      (request->load_flags() & net::LOAD_MAIN_FRAME_DEPRECATED)) {
    data_reduction_proxy_io_data_->SetLoFiModeActiveOnMainFrame(
        lofi_decider ? lofi_decider->IsSlowPagePreviewRequested(*headers)
                     : false);
  }

  if (data) {
    data->set_lofi_requested(
        lofi_decider ? lofi_decider->ShouldRecordLoFiUMA(*request) : false);
  }
  MaybeAddBrotliToAcceptEncodingHeader(proxy_info, headers, *request);

  // Generate a page ID for main frame requests that don't already have one.
  // TODO(ryansturm): remove LOAD_MAIN_FRAME_DEPRECATED from d_r_p.
  // crbug.com/709621
  if (request->load_flags() & net::LOAD_MAIN_FRAME_DEPRECATED) {
    if (!page_id) {
      page_id = data_reduction_proxy_request_options_->GeneratePageId();
    }
    data->set_page_id(page_id.value());
  }

  data_reduction_proxy_request_options_->AddRequestHeader(headers, page_id);

  if (lofi_decider)
    lofi_decider->MaybeSetIgnorePreviewsBlacklistDirective(headers);
  VerifyHttpRequestHeaders(true, *headers);
}

void DataReductionProxyNetworkDelegate::OnBeforeRedirectInternal(
    net::URLRequest* request,
    const GURL& new_location) {
  // Since this is after a redirect response, reset |request|'s
  // DataReductionProxyData, but keep page ID and session.
  // TODO(ryansturm): Change ClearData logic to have persistent and
  // non-persistent (WRT redirects) data.
  // crbug.com/709564
  DataReductionProxyData* data = DataReductionProxyData::GetData(*request);
  base::Optional<uint64_t> page_id;
  if (data && data->session_key() ==
                  data_reduction_proxy_request_options_->GetSecureSession()) {
    page_id = data->page_id();
  }

  DataReductionProxyData::ClearData(request);

  if (page_id) {
    data = DataReductionProxyData::GetDataAndCreateIfNecessary(request);
    data->set_page_id(page_id.value());
    data->set_session_key(
        data_reduction_proxy_request_options_->GetSecureSession());
  }
}

void DataReductionProxyNetworkDelegate::OnCompletedInternal(
    net::URLRequest* request,
    bool started) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(request);
  // TODO(maksims): remove this once OnCompletedInternal() has net_error in
  // arguments.
  int net_error = request->status().error();
  DCHECK_NE(net::ERR_IO_PENDING, net_error);
  if (data_reduction_proxy_bypass_stats_)
    data_reduction_proxy_bypass_stats_->OnUrlRequestCompleted(request, started,
                                                              net_error);

  net::HttpRequestHeaders request_headers;
  bool server_lofi = request->response_headers() &&
                     IsEmptyImagePreview(*(request->response_headers()));
  bool client_lofi =
      data_reduction_proxy_io_data_ &&
      data_reduction_proxy_io_data_->lofi_decider() &&
      data_reduction_proxy_io_data_->lofi_decider()->IsClientLoFiImageRequest(
          *request);
  if ((server_lofi || client_lofi) && data_reduction_proxy_io_data_ &&
      data_reduction_proxy_io_data_->lofi_ui_service()) {
    data_reduction_proxy_io_data_->lofi_ui_service()->OnLoFiReponseReceived(
        *request);
  } else if (data_reduction_proxy_io_data_ && request->response_headers() &&
             IsLitePagePreview(*(request->response_headers()))) {
    RecordLitePageTransformationType(LITE_PAGE);
  } else if (request->GetFullRequestHeaders(&request_headers)) {
    // TODO(bengr): transform processing logic should happen elsewhere.
    std::string header_value;
    request_headers.GetHeader(chrome_proxy_accept_transform_header(),
                              &header_value);
    if (header_value == lite_page_directive())
      RecordLitePageTransformationType(NO_TRANSFORMATION_LITE_PAGE_REQUESTED);
  }

  if (!request->response_info().network_accessed ||
      !request->url().SchemeIsHTTPOrHTTPS() ||
      request->GetTotalReceivedBytes() == 0) {
    return;
  }

  DataReductionProxyRequestType request_type = GetDataReductionProxyRequestType(
      *request, configurator_->GetProxyConfig(), *data_reduction_proxy_config_);

  // Determine the original content length if present.
  int64_t original_content_length =
      request->response_headers()
          ? request->response_headers()->GetInt64HeaderValue(
                "x-original-content-length")
          : -1;

  CalculateAndRecordDataUsage(*request, request_type);

  RecordContentLength(*request, request_type, original_content_length);
}

void DataReductionProxyNetworkDelegate::OnHeadersReceivedInternal(
    net::URLRequest* request,
    const net::CompletionCallback& callback,
    const net::HttpResponseHeaders* original_response_headers,
    scoped_refptr<net::HttpResponseHeaders>* override_response_headers,
    GURL* allowed_unsafe_redirect_url) {
  if (!original_response_headers ||
      original_response_headers->IsRedirect(nullptr))
    return;
  if (IsEmptyImagePreview(*original_response_headers)) {
    DataReductionProxyData* data =
        DataReductionProxyData::GetDataAndCreateIfNecessary(request);
    data->set_lofi_received(true);
  } else if (IsLitePagePreview(*original_response_headers)) {
    DataReductionProxyData* data =
        DataReductionProxyData::GetDataAndCreateIfNecessary(request);
    data->set_lite_page_received(true);
  }
  if (data_reduction_proxy_io_data_ &&
      data_reduction_proxy_io_data_->lofi_decider() &&
      data_reduction_proxy_io_data_->lofi_decider()->IsClientLoFiImageRequest(
          *request)) {
    DataReductionProxyData* data =
        DataReductionProxyData::GetDataAndCreateIfNecessary(request);
    data->set_client_lofi_requested(true);
  }
}

void DataReductionProxyNetworkDelegate::CalculateAndRecordDataUsage(
    const net::URLRequest& request,
    DataReductionProxyRequestType request_type) {
  DCHECK(thread_checker_.CalledOnValidThread());
  int64_t data_used = request.GetTotalReceivedBytes();

  // Estimate how many bytes would have been used if the DataReductionProxy was
  // not used, and record the data usage.
  int64_t original_size = EstimateOriginalReceivedBytes(
      request, request_type == VIA_DATA_REDUCTION_PROXY,
      data_reduction_proxy_io_data_
          ? data_reduction_proxy_io_data_->lofi_decider()
          : nullptr);

  std::string mime_type;
  if (request.response_headers())
    request.response_headers()->GetMimeType(&mime_type);

  scoped_refptr<DataUseGroup> data_use_group =
      data_use_group_provider_
          ? data_use_group_provider_->GetDataUseGroup(&request)
          : nullptr;
  AccumulateDataUsage(data_used, original_size, request_type, data_use_group,
                      mime_type);
}

void DataReductionProxyNetworkDelegate::AccumulateDataUsage(
    int64_t data_used,
    int64_t original_size,
    DataReductionProxyRequestType request_type,
    const scoped_refptr<DataUseGroup>& data_use_group,
    const std::string& mime_type) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_GE(data_used, 0);
  DCHECK_GE(original_size, 0);
  if (data_reduction_proxy_io_data_) {
    data_reduction_proxy_io_data_->UpdateContentLengths(
        data_used, original_size, data_reduction_proxy_io_data_->IsEnabled(),
        request_type, data_use_group, mime_type);
  }
}

void DataReductionProxyNetworkDelegate::RecordContentLength(
    const net::URLRequest& request,
    DataReductionProxyRequestType request_type,
    int64_t original_content_length) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!request.response_headers() || request.was_cached() ||
      request.received_response_content_length() == 0) {
    return;
  }

  // Record content length histograms for the request.
  base::TimeDelta freshness_lifetime =
      request.response_headers()
          ->GetFreshnessLifetimes(request.response_info().response_time)
          .freshness;

  bool is_https = request.url().SchemeIs("https");
  bool is_video = false;
  std::string mime_type;
  if (request.response_headers()->GetMimeType(&mime_type)) {
    is_video = net::MatchesMimeType("video/*", mime_type);
  }

  RecordContentLengthHistograms(
      // |data_reduction_proxy_io_data_| can be NULL for Webview.
      data_reduction_proxy_io_data_ &&
          data_reduction_proxy_io_data_->IsEnabled() &&
          data_reduction_proxy_io_data_->lofi_decider() &&
          data_reduction_proxy_io_data_->lofi_decider()->IsUsingLoFi(request),
      is_https, is_video, request.received_response_content_length(),
      original_content_length, freshness_lifetime);

  if (data_reduction_proxy_io_data_ && data_reduction_proxy_bypass_stats_) {
    // Record BypassedBytes histograms for the request.
    data_reduction_proxy_bypass_stats_->RecordBytesHistograms(
        request, data_reduction_proxy_io_data_->IsEnabled(),
        configurator_->GetProxyConfig());
  }
}

void DataReductionProxyNetworkDelegate::RecordLitePageTransformationType(
    LitePageTransformationType type) {
  DCHECK(thread_checker_.CalledOnValidThread());
  UMA_HISTOGRAM_ENUMERATION("DataReductionProxy.LoFi.TransformationType", type,
                            LITE_PAGE_TRANSFORMATION_TYPES_INDEX_BOUNDARY);
}

bool DataReductionProxyNetworkDelegate::WasEligibleWithoutHoldback(
    const net::URLRequest& request,
    const net::ProxyInfo& proxy_info,
    const net::ProxyRetryInfoMap& proxy_retry_info) const {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(proxy_info.is_empty() || proxy_info.is_direct() ||
         !data_reduction_proxy_config_->IsDataReductionProxy(
             proxy_info.proxy_server(), nullptr));
  if (!util::EligibleForDataReductionProxy(proxy_info, request.url(),
                                           request.method())) {
    return false;
  }
  net::ProxyConfig proxy_config =
      data_reduction_proxy_config_->ProxyConfigIgnoringHoldback();
  net::ProxyInfo data_reduction_proxy_info;
  return util::ApplyProxyConfigToProxyInfo(proxy_config, proxy_retry_info,
                                           request.url(),
                                           &data_reduction_proxy_info);
}

void DataReductionProxyNetworkDelegate::SetDataUseGroupProvider(
    std::unique_ptr<DataUseGroupProvider> data_use_group_provider) {
  DCHECK(thread_checker_.CalledOnValidThread());
  data_use_group_provider_ = std::move(data_use_group_provider);
}

void DataReductionProxyNetworkDelegate::MaybeAddBrotliToAcceptEncodingHeader(
    const net::ProxyInfo& proxy_info,
    net::HttpRequestHeaders* request_headers,
    const net::URLRequest& request) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  // This method should be called only when the resolved proxy was a data
  // saver proxy.
  DCHECK(data_reduction_proxy_config_->IsDataReductionProxy(
      proxy_info.proxy_server(), nullptr));
  DCHECK(request.url().is_valid());
  DCHECK(!request.url().SchemeIsCryptographic());
  DCHECK(request.url().SchemeIsHTTPOrHTTPS());

  static const char kBrotli[] = "br";

  if (!request.context()->enable_brotli()) {
    // Verify that Brotli is enabled globally.
    return;
  }

  if (!params::IsBrotliAcceptEncodingEnabled()) {
    // Verify that Brotli is enabled for data reduction proxy.
    return;
  }

  if (!proxy_info.proxy_server().is_https() &&
      !proxy_info.proxy_server().is_quic()) {
    // Brotli encoding can be used only when the proxy server is a secure proxy
    // server.
    return;
  }

  if (!request_headers->HasHeader(net::HttpRequestHeaders::kAcceptEncoding))
    return;

  std::string header_value;
  request_headers->GetHeader(net::HttpRequestHeaders::kAcceptEncoding,
                             &header_value);

  // Brotli should not be already present in the header since the URL is non-
  // cryptographic. This is an approximate check, and would trigger even if the
  // accept-encoding header contains an encoding that has prefix |kBrotli|.
  DCHECK_EQ(std::string::npos, header_value.find(kBrotli));

  request_headers->RemoveHeader(net::HttpRequestHeaders::kAcceptEncoding);
  if (!header_value.empty())
    header_value += ", ";
  header_value += kBrotli;
  request_headers->SetHeader(net::HttpRequestHeaders::kAcceptEncoding,
                             header_value);
}

void DataReductionProxyNetworkDelegate::MaybeAddChromeProxyECTHeader(
    net::HttpRequestHeaders* request_headers,
    const net::URLRequest& request) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  // This method should be called only when the resolved proxy was a data
  // saver proxy.
  DCHECK(request.url().is_valid());
  DCHECK(!request.url().SchemeIsCryptographic());
  DCHECK(request.url().SchemeIsHTTPOrHTTPS());

  if (request_headers->HasHeader(chrome_proxy_ect_header()))
    request_headers->RemoveHeader(chrome_proxy_ect_header());

  if (request.context()->network_quality_estimator()) {
    net::EffectiveConnectionType type = request.context()
                                            ->network_quality_estimator()
                                            ->GetEffectiveConnectionType();
    if (type > net::EFFECTIVE_CONNECTION_TYPE_OFFLINE) {
      DCHECK_NE(net::EFFECTIVE_CONNECTION_TYPE_LAST, type);
      request_headers->SetHeader(chrome_proxy_ect_header(),
                                 net::GetNameForEffectiveConnectionType(type));
      return;
    }
  }
  request_headers->SetHeader(chrome_proxy_ect_header(),
                             net::GetNameForEffectiveConnectionType(
                                 net::EFFECTIVE_CONNECTION_TYPE_UNKNOWN));

  static_assert(net::EFFECTIVE_CONNECTION_TYPE_OFFLINE + 1 ==
                    net::EFFECTIVE_CONNECTION_TYPE_SLOW_2G,
                "ECT enum value is not handled.");
  static_assert(net::EFFECTIVE_CONNECTION_TYPE_4G + 1 ==
                    net::EFFECTIVE_CONNECTION_TYPE_LAST,
                "ECT enum value is not handled.");
}

void DataReductionProxyNetworkDelegate::RemoveChromeProxyECTHeader(
    net::HttpRequestHeaders* request_headers) const {
  DCHECK(thread_checker_.CalledOnValidThread());

  request_headers->RemoveHeader(chrome_proxy_ect_header());
}

}  // namespace data_reduction_proxy
