// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/doodle/doodle_fetcher_impl.h"

#include <utility>

#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/data_use_measurement/core/data_use_user_data.h"
#include "components/google/core/browser/google_url_tracker.h"
#include "components/google/core/browser/google_util.h"
#include "net/base/load_flags.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/url_fetcher.h"

using net::URLFetcher;

namespace doodle {

namespace {

// "/async/ddljson" is the base API path. "ntp:1" identifies this request as
// being for a New Tab page. The "graybg:" param specifies whether the doodle
// will be displayed on a gray background.
const char kDoodleConfigPathFormat[] = "/async/ddljson?async=ntp:1,graybg:%d";

std::string StripSafetyPreamble(const std::string& json) {
  // The response may start with )]}'. Ignore this.
  const char kResponsePreamble[] = ")]}'";

  base::StringPiece json_sp(json);
  if (json_sp.starts_with(kResponsePreamble)) {
    json_sp.remove_prefix(strlen(kResponsePreamble));
  }

  return json_sp.as_string();
}

GURL BuildDoodleURL(const GURL& base_url,
                    bool gray_background,
                    const base::Optional<std::string>& override_url) {
  std::string path =
      base::StringPrintf(kDoodleConfigPathFormat, gray_background ? 1 : 0);
  if (override_url.has_value()) {
    // The override URL may or may not be relative to the base URL.
    path = *override_url;
  }
  return base_url.Resolve(path);
}

}  // namespace

DoodleFetcherImpl::DoodleFetcherImpl(
    scoped_refptr<net::URLRequestContextGetter> download_context,
    GoogleURLTracker* google_url_tracker,
    const ParseJSONCallback& json_parsing_callback,
    bool gray_background,
    const base::Optional<std::string>& override_url)
    : download_context_(download_context),
      google_url_tracker_(google_url_tracker),
      json_parsing_callback_(json_parsing_callback),
      gray_background_(gray_background),
      override_url_(override_url),
      weak_ptr_factory_(this) {
  DCHECK(google_url_tracker_);
}

DoodleFetcherImpl::~DoodleFetcherImpl() = default;

void DoodleFetcherImpl::FetchDoodle(FinishedCallback callback) {
  if (IsFetchInProgress()) {
    callbacks_.push_back(std::move(callback));
    return;  // The callback will be called for the existing request's results.
  }
  DCHECK(!fetcher_.get());
  callbacks_.push_back(std::move(callback));
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("doodle_fetcher", R"(
        semantics {
          sender: "Doodle Fetcher"
          description:
            "Retrieves metadata (image URL, clickthrough URL etc) for any "
            "currently running Doodle."
          trigger:
            "Displaying the new tab page on Android."
          data:
            "The user's Google cookies. Used for example to show birthday "
            "doodles at appropriate times."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: true
          cookies_store: "user"
          setting:
            "Choosing a non-Google search engine in Chromium settings under "
            "'Search Engine' will disable this feature."
          chrome_policy {
            DefaultSearchProviderEnabled {
              policy_options {mode: MANDATORY}
              DefaultSearchProviderEnabled: false
            }
          }
        })");
  fetcher_ = URLFetcher::Create(
      BuildDoodleURL(GetGoogleBaseUrl(), gray_background_, override_url_),
      URLFetcher::GET, this, traffic_annotation);
  fetcher_->SetRequestContext(download_context_.get());
  // TODO(treib): Use OAuth2 authentication instead of cookies. crbug.com/711314
  fetcher_->SetLoadFlags(net::LOAD_DO_NOT_SEND_AUTH_DATA);
  fetcher_->SetAutomaticallyRetryOnNetworkChanges(1);
  data_use_measurement::DataUseUserData::AttachToFetcher(
      fetcher_.get(), data_use_measurement::DataUseUserData::DOODLE);
  fetcher_->Start();
}

bool DoodleFetcherImpl::IsFetchInProgress() const {
  return !callbacks_.empty();
}

void DoodleFetcherImpl::OnURLFetchComplete(const URLFetcher* source) {
  DCHECK_EQ(fetcher_.get(), source);
  std::unique_ptr<net::URLFetcher> free_fetcher = std::move(fetcher_);

  std::string json_string;
  if (!(source->GetStatus().is_success() &&
        source->GetResponseCode() == net::HTTP_OK &&
        source->GetResponseAsString(&json_string))) {
    RespondToAllCallbacks(DoodleState::DOWNLOAD_ERROR, base::TimeDelta(),
                          base::nullopt);
    return;
  }

  json_parsing_callback_.Run(StripSafetyPreamble(std::move(json_string)),
                             base::Bind(&DoodleFetcherImpl::OnJsonParsed,
                                        weak_ptr_factory_.GetWeakPtr()),
                             base::Bind(&DoodleFetcherImpl::OnJsonParseFailed,
                                        weak_ptr_factory_.GetWeakPtr()));
}

void DoodleFetcherImpl::OnJsonParsed(std::unique_ptr<base::Value> json) {
  std::unique_ptr<base::DictionaryValue> config =
      base::DictionaryValue::From(std::move(json));
  if (!config.get()) {
    DLOG(WARNING) << "Doodle JSON is not valid.";
    RespondToAllCallbacks(DoodleState::PARSING_ERROR, base::TimeDelta(),
                          base::nullopt);
    return;
  }

  const base::DictionaryValue* ddljson = nullptr;
  if (!config->GetDictionary("ddljson", &ddljson)) {
    DLOG(WARNING) << "Doodle JSON response did not contain 'ddljson' key.";
    RespondToAllCallbacks(DoodleState::PARSING_ERROR, base::TimeDelta(),
                          base::nullopt);
    return;
  }

  base::TimeDelta time_to_live;
  base::Optional<DoodleConfig> doodle =
      ParseDoodleConfigAndTimeToLive(*ddljson, &time_to_live);
  if (!doodle.has_value()) {
    RespondToAllCallbacks(DoodleState::NO_DOODLE, base::TimeDelta(),
                          base::nullopt);
    return;
  }

  RespondToAllCallbacks(DoodleState::AVAILABLE, time_to_live,
                        std::move(doodle));
}

void DoodleFetcherImpl::OnJsonParseFailed(const std::string& error_message) {
  DLOG(WARNING) << "JSON parsing failed: " << error_message;
  RespondToAllCallbacks(DoodleState::PARSING_ERROR, base::TimeDelta(),
                        base::nullopt);
}

base::Optional<DoodleConfig> DoodleFetcherImpl::ParseDoodleConfigAndTimeToLive(
    const base::DictionaryValue& ddljson,
    base::TimeDelta* time_to_live) const {
  base::Optional<DoodleConfig> doodle =
      DoodleConfig::FromDictionary(ddljson, GetGoogleBaseUrl());

  // The JSON doesn't guarantee the number to fit into an int.
  double ttl_ms = 0;  // Expires immediately if the parameter is missing.
  if (!ddljson.GetDouble("time_to_live_ms", &ttl_ms) || ttl_ms < 0) {
    DLOG(WARNING) << "No valid Doodle TTL present in ddljson!";
    ttl_ms = 0;
  }
  *time_to_live = base::TimeDelta::FromMillisecondsD(ttl_ms);

  return doodle;
}

void DoodleFetcherImpl::RespondToAllCallbacks(
    DoodleState state,
    base::TimeDelta time_to_live,
    const base::Optional<DoodleConfig>& config) {
  for (auto& callback : callbacks_) {
    std::move(callback).Run(state, time_to_live, config);
  }
  callbacks_.clear();
}

GURL DoodleFetcherImpl::GetGoogleBaseUrl() const {
  GURL cmd_line_url = google_util::CommandLineGoogleBaseURL();
  if (cmd_line_url.is_valid()) {
    return cmd_line_url;
  }
  return google_url_tracker_->google_url();
}

}  // namespace doodle
