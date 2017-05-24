// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DATA_USE_MEASUREMENT_CORE_DATA_USE_H_
#define COMPONENTS_DATA_USE_MEASUREMENT_CORE_DATA_USE_H_

#include <stdint.h>

#include <string>

#include "base/macros.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace data_use_measurement {

// Class to store total network data used by some entity.
class DataUse : public base::SupportsUserData {
 public:
  enum class TrafficType {
    // Unknown type. URLRequests for arbitrary scheme such as blob, file,
    // extensions, chrome URLs fall under this bucket - url/url_constants.cc
    // TODO(rajendrant): Record metrics on the distribution of these type. It is
    // also possible to remove this UNKNOWN type altogether by skipping the URL
    // schemes that do not make use of network.
    UNKNOWN,

    // User initiated traffic.
    USER_TRAFFIC,

    // Chrome services.
    SERVICES,

    // Fetch from ServiceWorker.
    SERVICE_WORKER,
  };

  explicit DataUse(TrafficType traffic_type);
  ~DataUse() override;

  // Merge data use from another instance.
  // TODO(rajendrant): Check if the merge can be removed. Otherwise user data
  // needs to support mergeability.
  void MergeFrom(const DataUse& other);

  // Returns the page URL.
  const GURL& url() const { return url_; }

  void set_url(const GURL& url) { url_ = url; }

  const std::string& description() const { return description_; }

  void set_description(const std::string& description) {
    description_ = description;
  }

  int64_t total_bytes_received() const { return total_bytes_received_; }

  int64_t total_bytes_sent() const { return total_bytes_sent_; }

  TrafficType traffic_type() const { return traffic_type_; }

 private:
  // TODO(rajendrant): Remove this friend after adding member function to
  // increment total sent/received bytes.
  friend class DataUseRecorder;

  GURL url_;
  std::string description_;
  const TrafficType traffic_type_;

  int64_t total_bytes_sent_;
  int64_t total_bytes_received_;

  DISALLOW_COPY_AND_ASSIGN(DataUse);
};

}  // namespace data_use_measurement

#endif  // COMPONENTS_DATA_USE_MEASUREMENT_CORE_DATA_USE_H_
