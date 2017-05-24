// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_data_use_observer.h"

#include "base/memory/ptr_util.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_config.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_configurator.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_io_data.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_metrics.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_util.h"
#include "components/data_reduction_proxy/core/common/lofi_decider.h"
#include "components/data_use_measurement/core/data_use.h"
#include "components/data_use_measurement/core/data_use_ascriber.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_request.h"
#include "url/gurl.h"

namespace {

class DataUseUserDataBytes : public base::SupportsUserData::Data {
 public:
  // Key used to store data usage in userdata until the page URL is available.
  static const void* kUserDataKey;

  DataUseUserDataBytes(int64_t network_bytes, int64_t original_bytes)
      : network_bytes_(network_bytes), original_bytes_(original_bytes) {}

  int64_t network_bytes() const { return network_bytes_; }
  int64_t original_bytes() const { return original_bytes_; }

  void IncrementBytes(int64_t network_bytes, int64_t original_bytes) {
    network_bytes_ += network_bytes;
    original_bytes_ += original_bytes;
  }

 private:
  int64_t network_bytes_;
  int64_t original_bytes_;
};

// static
const void* DataUseUserDataBytes::kUserDataKey =
    &DataUseUserDataBytes::kUserDataKey;

// Estimate the size of the original headers of |request|. If |used_drp| is
// true, then it's assumed that the original request would have used HTTP/1.1,
// otherwise it assumes that the original request would have used the same
// protocol as |request| did. This is to account for stuff like HTTP/2 header
// compression.
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
int64_t EstimateOriginalReceivedBytes(
    const net::URLRequest& request,
    bool used_drp,
    const data_reduction_proxy::LoFiDecider* lofi_decider) {
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

  return used_drp
             ? EstimateOriginalHeaderBytes(request, used_drp) +
                   data_reduction_proxy::util::CalculateEffectiveOCL(request)
             : request.GetTotalReceivedBytes();
}

}  // namespace

namespace data_reduction_proxy {

DataReductionProxyDataUseObserver::DataReductionProxyDataUseObserver(
    DataReductionProxyIOData* data_reduction_proxy_io_data,
    data_use_measurement::DataUseAscriber* data_use_ascriber)
    : data_reduction_proxy_io_data_(data_reduction_proxy_io_data),
      data_use_ascriber_(data_use_ascriber) {
  DCHECK(data_reduction_proxy_io_data_);
  data_use_ascriber_->AddObserver(this);
}

DataReductionProxyDataUseObserver::~DataReductionProxyDataUseObserver() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  data_use_ascriber_->RemoveObserver(this);
}

void DataReductionProxyDataUseObserver::OnPageLoadCommit(
    data_use_measurement::DataUse* data_use) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (!data_use->url().is_valid())
    return;
  DataUseUserDataBytes* bytes = reinterpret_cast<DataUseUserDataBytes*>(
      data_use->GetUserData(DataUseUserDataBytes::kUserDataKey));
  if (bytes) {
    // Record the data use bytes saved in user data to database.
    data_reduction_proxy_io_data_->UpdateDataUseForHost(
        bytes->network_bytes(), bytes->original_bytes(),
        data_use->url().HostNoBrackets());
    data_use->RemoveUserData(DataUseUserDataBytes::kUserDataKey);
  }
}

void DataReductionProxyDataUseObserver::OnPageResourceLoad(
    const net::URLRequest& request,
    data_use_measurement::DataUse* data_use) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (data_use->traffic_type() !=
      data_use_measurement::DataUse::TrafficType::USER_TRAFFIC)
    return;

  int64_t network_bytes = request.GetTotalReceivedBytes();
  DataReductionProxyRequestType request_type = GetDataReductionProxyRequestType(
      request, data_reduction_proxy_io_data_->configurator()->GetProxyConfig(),
      *data_reduction_proxy_io_data_->config());

  // Estimate how many bytes would have been used if the DataReductionProxy was
  // not used, and record the data usage.
  int64_t original_bytes = EstimateOriginalReceivedBytes(
      request, request_type == VIA_DATA_REDUCTION_PROXY,
      data_reduction_proxy_io_data_->lofi_decider());

  if (!data_use->url().is_valid()) {
    // URL will be empty until pageload navigation commits. Save the data use of
    // these mainframe, subresource, redirected requests in user data until
    // then.
    DataUseUserDataBytes* bytes = reinterpret_cast<DataUseUserDataBytes*>(
        data_use->GetUserData(DataUseUserDataBytes::kUserDataKey));
    if (bytes) {
      bytes->IncrementBytes(network_bytes, original_bytes);
    } else {
      data_use->SetUserData(DataUseUserDataBytes::kUserDataKey,
                            base::MakeUnique<DataUseUserDataBytes>(
                                network_bytes, original_bytes));
    }
  } else {
    data_reduction_proxy_io_data_->UpdateDataUseForHost(
        network_bytes, original_bytes, data_use->url().HostNoBrackets());
  }
}

}  // namespace data_reduction_proxy
