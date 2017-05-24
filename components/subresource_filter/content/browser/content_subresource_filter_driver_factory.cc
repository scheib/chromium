// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/subresource_filter/content/browser/content_subresource_filter_driver_factory.h"

#include "base/memory/ptr_util.h"
#include "base/rand_util.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_event_argument.h"
#include "components/subresource_filter/content/browser/content_activation_list_utils.h"
#include "components/subresource_filter/content/browser/subresource_filter_client.h"
#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "components/subresource_filter/core/common/activation_list.h"
#include "components/subresource_filter/core/common/activation_state.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "url/gurl.h"

DEFINE_WEB_CONTENTS_USER_DATA_KEY(
    subresource_filter::ContentSubresourceFilterDriverFactory);

namespace subresource_filter {

namespace {

// Returns true with a probability given by |performance_measurement_rate| if
// ThreadTicks is supported, otherwise returns false.
bool ShouldMeasurePerformanceForPageLoad(double performance_measurement_rate) {
  if (!base::ThreadTicks::IsSupported())
    return false;
  return performance_measurement_rate == 1 ||
         (performance_measurement_rate > 0 &&
          base::RandDouble() < performance_measurement_rate);
}

}  // namespace

// static
void ContentSubresourceFilterDriverFactory::CreateForWebContents(
    content::WebContents* web_contents,
    SubresourceFilterClient* client) {
  if (FromWebContents(web_contents))
    return;
  web_contents->SetUserData(
      UserDataKey(), base::MakeUnique<ContentSubresourceFilterDriverFactory>(
                         web_contents, client));
}

// static
bool ContentSubresourceFilterDriverFactory::NavigationIsPageReload(
    const GURL& url,
    const content::Referrer& referrer,
    ui::PageTransition transition) {
  return ui::PageTransitionCoreTypeIs(transition, ui::PAGE_TRANSITION_RELOAD) ||
         // Some pages 'reload' from JavaScript by navigating to themselves.
         url == referrer.url;
}

ContentSubresourceFilterDriverFactory::ContentSubresourceFilterDriverFactory(
    content::WebContents* web_contents,
    SubresourceFilterClient* client)
    : content::WebContentsObserver(web_contents),
      client_(client),
      throttle_manager_(
          base::MakeUnique<ContentSubresourceFilterThrottleManager>(
              this,
              client_->GetRulesetDealer(),
              web_contents)) {}

ContentSubresourceFilterDriverFactory::
    ~ContentSubresourceFilterDriverFactory() {}

void ContentSubresourceFilterDriverFactory::OnSafeBrowsingMatchComputed(
    content::NavigationHandle* navigation_handle,
    safe_browsing::SBThreatType threat_type,
    safe_browsing::ThreatPatternType threat_type_metadata) {
  DCHECK(navigation_handle->IsInMainFrame());
  DCHECK(!navigation_handle->IsSameDocument());
  if (navigation_handle->GetNetErrorCode() != net::OK)
    return;

  ActivationList activation_list =
      GetListForThreatTypeAndMetadata(threat_type, threat_type_metadata);
  const GURL& url = navigation_handle->GetURL();
  const content::Referrer& referrer = navigation_handle->GetReferrer();
  ui::PageTransition transition = navigation_handle->GetPageTransition();

  if (activation_options_.should_whitelist_site_on_reload &&
      NavigationIsPageReload(url, referrer, transition)) {
    // Whitelist this host for the current as well as subsequent navigations.
    client_->WhitelistInCurrentWebContents(url);
  }

  ComputeActivationForMainFrameNavigation(navigation_handle, activation_list);
  DCHECK_NE(activation_decision_, ActivationDecision::UNKNOWN);

  // Check for whitelisted status last, so that the client gets an accurate
  // indication of whether there would be activation otherwise.
  bool whitelisted = client_->OnPageActivationComputed(
      navigation_handle,
      activation_options_.activation_level == ActivationLevel::ENABLED);

  // Only reset the activation decision reason if we would have activated.
  if (whitelisted && activation_decision_ == ActivationDecision::ACTIVATED) {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("loading"), "ActivationWhitelisted");
    activation_decision_ = ActivationDecision::URL_WHITELISTED;
    activation_options_ = Configuration::ActivationOptions();
  }

  if (activation_decision_ != ActivationDecision::ACTIVATED) {
    DCHECK_EQ(activation_options_.activation_level, ActivationLevel::DISABLED);
    return;
  }

  DCHECK_NE(activation_options_.activation_level, ActivationLevel::DISABLED);
  ActivationState state = ActivationState(activation_options_.activation_level);
  state.measure_performance = ShouldMeasurePerformanceForPageLoad(
      activation_options_.performance_measurement_rate);
  // TODO(csharrison): Set state.enable_logging based on metadata returns from
  // the safe browsing filter, when it is available. Add tests for this
  // behavior.
  throttle_manager_->NotifyPageActivationComputed(navigation_handle, state);
}

// Be careful when modifying this method, as the order in which the
// activation_decision_ is decided is very important and corresponds to UMA
// metrics. In general we want to follow the pattern that
// ACTIVATION_CONDITIONS_NOT_MET will always be logged if no configuration
// matches this navigation. We log other decisions only if a configuration
// matches and also would have activated.
void ContentSubresourceFilterDriverFactory::
    ComputeActivationForMainFrameNavigation(
        content::NavigationHandle* navigation_handle,
        ActivationList matched_list) {
  const GURL& url(navigation_handle->GetURL());

  bool scheme_is_http_or_https = url.SchemeIsHTTPOrHTTPS();
  const auto config_list = GetEnabledConfigurations();
  const auto highest_priority_activated_config =
      std::find_if(config_list->configs_by_decreasing_priority().begin(),
                   config_list->configs_by_decreasing_priority().end(),
                   [&url, scheme_is_http_or_https, matched_list,
                    this](const Configuration& config) {
                     return DoesMainFrameURLSatisfyActivationConditions(
                         url, scheme_is_http_or_https,
                         config.activation_conditions, matched_list);
                   });

  bool has_activated_config =
      highest_priority_activated_config !=
      config_list->configs_by_decreasing_priority().end();
  TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("loading"),
               "ContentSubresourceFilterDriverFactory::"
               "ComputeActivationForMainFrameNavigation",
               "highest_priority_activated_config",
               has_activated_config
                   ? highest_priority_activated_config->ToTracedValue()
                   : base::MakeUnique<base::trace_event::TracedValue>());
  if (!has_activated_config) {
    activation_decision_ = ActivationDecision::ACTIVATION_CONDITIONS_NOT_MET;
    activation_options_ = Configuration::ActivationOptions();
    return;
  }

  const Configuration::ActivationOptions& activation_options =
      highest_priority_activated_config->activation_options;

  // Log UNSUPPORTED_SCHEME if we would have otherwise activated.
  if (!scheme_is_http_or_https &&
      activation_options.activation_level != ActivationLevel::DISABLED) {
    activation_decision_ = ActivationDecision::UNSUPPORTED_SCHEME;
    activation_options_ = Configuration::ActivationOptions();
    return;
  }

  activation_options_ = activation_options;
  activation_decision_ =
      activation_options_.activation_level == ActivationLevel::DISABLED
          ? ActivationDecision::ACTIVATION_DISABLED
          : ActivationDecision::ACTIVATED;
}

bool ContentSubresourceFilterDriverFactory::
    DoesMainFrameURLSatisfyActivationConditions(
        const GURL& url,
        bool scheme_is_http_or_https,
        const Configuration::ActivationConditions& conditions,
        ActivationList matched_list) const {
  // scheme_is_http_or_https implies url.SchemeIsHTTPOrHTTPS().
  DCHECK(!scheme_is_http_or_https || url.SchemeIsHTTPOrHTTPS());
  switch (conditions.activation_scope) {
    case ActivationScope::ALL_SITES:
      return true;
    case ActivationScope::ACTIVATION_LIST:
      // ACTIVATION_LIST does not support non http/s URLs.
      if (!scheme_is_http_or_https)
        return false;
      if (conditions.activation_list == matched_list)
        return true;
      if (conditions.activation_list == ActivationList::PHISHING_INTERSTITIAL &&
          matched_list == ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL) {
        // Handling special case, where activation on the phishing sites also
        // mean the activation on the sites with social engineering metadata.
        return true;
      }
      return false;
    case ActivationScope::NO_SITES:
      return false;
  }
  NOTREACHED();
  return false;
}

void ContentSubresourceFilterDriverFactory::OnFirstSubresourceLoadDisallowed() {
  if (activation_options_.should_suppress_notifications)
    return;
  client_->ToggleNotificationVisibility(activation_options_.activation_level ==
                                        ActivationLevel::ENABLED);
}

void ContentSubresourceFilterDriverFactory::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInMainFrame() &&
      !navigation_handle->IsSameDocument()) {
    activation_decision_ = ActivationDecision::UNKNOWN;
    client_->ToggleNotificationVisibility(false);
  }
}

void ContentSubresourceFilterDriverFactory::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInMainFrame() &&
      !navigation_handle->IsSameDocument() &&
      activation_decision_ == ActivationDecision::UNKNOWN &&
      navigation_handle->HasCommitted()) {
    activation_decision_ = ActivationDecision::ACTIVATION_DISABLED;
    activation_options_ = Configuration::ActivationOptions();
  }
}

}  // namespace subresource_filter
