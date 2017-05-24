// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/subresource_filter/content/browser/subresource_filter_safe_browsing_activation_throttle.h"

#include <utility>
#include <vector>

#include "base/metrics/histogram_macros.h"
#include "base/timer/timer.h"
#include "components/subresource_filter/content/browser/content_activation_list_utils.h"
#include "components/subresource_filter/content/browser/content_subresource_filter_driver_factory.h"
#include "components/subresource_filter/content/browser/subresource_filter_safe_browsing_client.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"

namespace subresource_filter {

namespace {

// Records histograms about the pattern of redirect chains, and about the
// pattern of whether the last URL in the chain matched the activation list.
#define REPORT_REDIRECT_PATTERN_FOR_SUFFIX(suffix, is_matched, chain_size)    \
  do {                                                                        \
    UMA_HISTOGRAM_BOOLEAN("SubresourceFilter.PageLoad.FinalURLMatch." suffix, \
                          is_matched);                                        \
    if (is_matched) {                                                         \
      UMA_HISTOGRAM_COUNTS(                                                   \
          "SubresourceFilter.PageLoad.RedirectChainLength." suffix,           \
          chain_size);                                                        \
    };                                                                        \
  } while (0)

}  // namespace

SubresourceFilterSafeBrowsingActivationThrottle::
    SubresourceFilterSafeBrowsingActivationThrottle(
        content::NavigationHandle* handle,
        scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
        scoped_refptr<safe_browsing::SafeBrowsingDatabaseManager>
            database_manager)
    : NavigationThrottle(handle),
      io_task_runner_(std::move(io_task_runner)),
      // The throttle can be created without a valid database manager. If so, it
      // becomes a pass-through throttle and should never defer.
      database_client_(database_manager
                           ? new SubresourceFilterSafeBrowsingClient(
                                 std::move(database_manager),
                                 AsWeakPtr(),
                                 io_task_runner_,
                                 base::ThreadTaskRunnerHandle::Get())
                           : nullptr,
                       base::OnTaskRunnerDeleter(io_task_runner_)) {
  DCHECK(handle->IsInMainFrame());
}

SubresourceFilterSafeBrowsingActivationThrottle::
    ~SubresourceFilterSafeBrowsingActivationThrottle() {
  // The last check could be ongoing when the navigation is cancelled.
  if (check_results_.empty() || !check_results_.back().finished)
    return;
  // TODO(csharrison): Log more metrics based on check_results_.
  RecordRedirectChainMatchPatternForList(
      ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL);
  RecordRedirectChainMatchPatternForList(ActivationList::PHISHING_INTERSTITIAL);
  RecordRedirectChainMatchPatternForList(ActivationList::SUBRESOURCE_FILTER);
}

content::NavigationThrottle::ThrottleCheckResult
SubresourceFilterSafeBrowsingActivationThrottle::WillStartRequest() {
  CheckCurrentUrl();
  return content::NavigationThrottle::ThrottleCheckResult::PROCEED;
}

content::NavigationThrottle::ThrottleCheckResult
SubresourceFilterSafeBrowsingActivationThrottle::WillRedirectRequest() {
  CheckCurrentUrl();
  return content::NavigationThrottle::ThrottleCheckResult::PROCEED;
}

content::NavigationThrottle::ThrottleCheckResult
SubresourceFilterSafeBrowsingActivationThrottle::WillProcessResponse() {
  // No need to defer the navigation if the check already happened.
  if (!database_client_ || check_results_.back().finished) {
    NotifyResult();
    return content::NavigationThrottle::ThrottleCheckResult::PROCEED;
  }
  defer_time_ = base::TimeTicks::Now();
  return content::NavigationThrottle::ThrottleCheckResult::DEFER;
}

const char*
SubresourceFilterSafeBrowsingActivationThrottle::GetNameForLogging() {
  return "SubresourceFilterSafeBrowsingActivationThrottle";
}

void SubresourceFilterSafeBrowsingActivationThrottle::OnCheckUrlResultOnUI(
    const SubresourceFilterSafeBrowsingClient::CheckResult& result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  size_t request_id = result.request_id;
  DCHECK_LT(request_id, check_results_.size());

  auto& stored_result = check_results_.at(request_id);
  DCHECK(!stored_result.finished);
  stored_result = result;
  if (!defer_time_.is_null() && request_id == check_results_.size() - 1) {
    NotifyResult();
    navigation_handle()->Resume();
  }
}

void SubresourceFilterSafeBrowsingActivationThrottle::CheckCurrentUrl() {
  if (!database_client_)
    return;
  check_results_.emplace_back();
  size_t id = check_results_.size() - 1;
  io_task_runner_->PostTask(
      FROM_HERE, base::Bind(&SubresourceFilterSafeBrowsingClient::CheckUrlOnIO,
                            base::Unretained(database_client_.get()),
                            navigation_handle()->GetURL(), id));
}

void SubresourceFilterSafeBrowsingActivationThrottle::NotifyResult() {
  content::WebContents* web_contents = navigation_handle()->GetWebContents();
  if (!web_contents)
    return;

  using subresource_filter::ContentSubresourceFilterDriverFactory;

  ContentSubresourceFilterDriverFactory* driver_factory =
      ContentSubresourceFilterDriverFactory::FromWebContents(web_contents);
  DCHECK(driver_factory);

  auto threat_type = safe_browsing::SBThreatType::SB_THREAT_TYPE_SAFE;
  auto pattern_type = safe_browsing::ThreatPatternType::NONE;
  if (database_client_) {
    DCHECK(!check_results_.empty());
    DCHECK(check_results_.back().finished);
    threat_type = check_results_.back().threat_type;
    pattern_type = check_results_.back().pattern_type;
  }
  driver_factory->OnSafeBrowsingMatchComputed(navigation_handle(), threat_type,
                                              pattern_type);

  base::TimeDelta delay = defer_time_.is_null()
                              ? base::TimeDelta::FromMilliseconds(0)
                              : base::TimeTicks::Now() - defer_time_;
  UMA_HISTOGRAM_TIMES("SubresourceFilter.PageLoad.SafeBrowsingDelay", delay);

  // Log a histogram for the delay we would have introduced if the throttle only
  // speculatively checks URLs on WillStartRequest. This is only different from
  // the actual delay if there was at least one redirect.
  base::TimeDelta no_redirect_speculation_delay =
      check_results_.size() > 1 ? check_results_.back().check_time : delay;
  UMA_HISTOGRAM_TIMES(
      "SubresourceFilter.PageLoad.SafeBrowsingDelay.NoRedirectSpeculation",
      no_redirect_speculation_delay);
}

void SubresourceFilterSafeBrowsingActivationThrottle::
    RecordRedirectChainMatchPatternForList(ActivationList activation_list) {
  DCHECK(check_results_.back().finished);
  ActivationList matched_list = GetListForThreatTypeAndMetadata(
      check_results_.back().threat_type, check_results_.back().pattern_type);
  bool is_matched = matched_list == activation_list;
  size_t chain_size = check_results_.size();
  switch (activation_list) {
    case ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL:
      REPORT_REDIRECT_PATTERN_FOR_SUFFIX("SocialEngineeringAdsInterstitial",
                                         is_matched, chain_size);
      break;
    case ActivationList::PHISHING_INTERSTITIAL:
      REPORT_REDIRECT_PATTERN_FOR_SUFFIX("PhishingInterstitial", is_matched,
                                         chain_size);
      break;
    case ActivationList::SUBRESOURCE_FILTER:
      REPORT_REDIRECT_PATTERN_FOR_SUFFIX("SubresourceFilterOnly", is_matched,
                                         chain_size);
      break;
    default:
      NOTREACHED();
      break;
  }
}

}  //  namespace subresource_filter
