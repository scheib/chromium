// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/payments/core/journey_logger.h"

#include <algorithm>

#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "components/ukm/ukm_entry_builder.h"
#include "components/ukm/ukm_service.h"

namespace payments {

namespace internal {
extern const char kUKMCheckoutEventsEntryName[] =
    "PaymentRequest.CheckoutEvents";
extern const char kUKMCompletionStatusMetricName[] = "CompletionStatus";
extern const char kUKMEventsMetricName[] = "Events";
}  // namespace internal

namespace {

// Returns the JourneyLogger histograms name suffix based on the |section| and
// the |completion_status|.
std::string GetHistogramNameSuffix(
    int section,
    JourneyLogger::CompletionStatus completion_status) {
  std::string name_suffix = "";

  switch (section) {
    case JourneyLogger::SECTION_SHIPPING_ADDRESS:
      name_suffix = "ShippingAddress.";
      break;
    case JourneyLogger::SECTION_CONTACT_INFO:
      name_suffix = "ContactInfo.";
      break;
    case JourneyLogger::SECTION_CREDIT_CARDS:
      name_suffix = "CreditCards.";
      break;
    default:
      break;
  }

  switch (completion_status) {
    case JourneyLogger::COMPLETION_STATUS_COMPLETED:
      name_suffix += "Completed";
      break;
    case JourneyLogger::COMPLETION_STATUS_USER_ABORTED:
      name_suffix += "UserAborted";
      break;
    case JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED:
      name_suffix += "OtherAborted";
      break;
    default:
      break;
  }

  DCHECK(!name_suffix.empty());
  return name_suffix;
}

}  // namespace

JourneyLogger::JourneyLogger(bool is_incognito,
                             const GURL& url,
                             ukm::UkmService* ukm_service)
    : is_incognito_(is_incognito),
      events_(EVENT_INITIATED),
      url_(url),
      ukm_service_(ukm_service) {}

JourneyLogger::~JourneyLogger() {
  if (was_show_called_)
    DCHECK(has_recorded_);
}

void JourneyLogger::IncrementSelectionAdds(Section section) {
  DCHECK_LT(section, SECTION_MAX);
  sections_[section].number_selection_adds_++;
}

void JourneyLogger::IncrementSelectionChanges(Section section) {
  DCHECK_LT(section, SECTION_MAX);
  sections_[section].number_selection_changes_++;
}

void JourneyLogger::IncrementSelectionEdits(Section section) {
  DCHECK_LT(section, SECTION_MAX);
  sections_[section].number_selection_edits_++;
}

void JourneyLogger::SetNumberOfSuggestionsShown(Section section, int number) {
  DCHECK_LT(section, SECTION_MAX);
  sections_[section].number_suggestions_shown_ = number;
  sections_[section].is_requested_ = true;
}

void JourneyLogger::SetCanMakePaymentValue(bool value) {
  was_can_make_payments_used_ = true;
  could_make_payment_ |= value;
}

void JourneyLogger::SetShowCalled() {
  was_show_called_ = true;
}

void JourneyLogger::SetEventOccurred(Event event) {
  events_ |= event;
}

void JourneyLogger::RecordJourneyStatsHistograms(
    CompletionStatus completion_status) {
  DCHECK(!has_recorded_);
  has_recorded_ = true;

  RecordCheckoutFlowMetrics();

  RecordSectionSpecificStats(completion_status);

  // Record the CanMakePayment metrics based on whether the transaction was
  // completed or aborted by the user (UserAborted) or otherwise (OtherAborted).
  RecordCanMakePaymentStats(completion_status);

  RecordUrlKeyedMetrics(completion_status);
}

void JourneyLogger::RecordCheckoutFlowMetrics() {
  UMA_HISTOGRAM_BOOLEAN("PaymentRequest.CheckoutFunnel.Initiated", true);

  if (events_ & EVENT_SHOWN)
    UMA_HISTOGRAM_BOOLEAN("PaymentRequest.CheckoutFunnel.Shown", true);

  if (events_ & EVENT_PAY_CLICKED)
    UMA_HISTOGRAM_BOOLEAN("PaymentRequest.CheckoutFunnel.PayClicked", true);

  if (events_ & EVENT_RECEIVED_INSTRUMENT_DETAILS)
    UMA_HISTOGRAM_BOOLEAN(
        "PaymentRequest.CheckoutFunnel.ReceivedInstrumentDetails", true);

  if (events_ & EVENT_SKIPPED_SHOW)
    UMA_HISTOGRAM_BOOLEAN("PaymentRequest.CheckoutFunnel.SkippedShow", true);
}

void JourneyLogger::RecordSectionSpecificStats(
    CompletionStatus completion_status) {
  // Record whether the user had suggestions for each requested information.
  bool user_had_all_requested_information = true;

  for (int i = 0; i < NUMBER_OF_SECTIONS; ++i) {
    std::string name_suffix = GetHistogramNameSuffix(i, completion_status);

    // Only log the metrics for a section if it was requested by the merchant.
    if (sections_[i].is_requested_) {
      base::UmaHistogramCustomCounts(
          "PaymentRequest.NumberOfSelectionAdds." + name_suffix,
          std::min(sections_[i].number_selection_adds_, MAX_EXPECTED_SAMPLE),
          MIN_EXPECTED_SAMPLE, MAX_EXPECTED_SAMPLE, NUMBER_BUCKETS);
      base::UmaHistogramCustomCounts(
          "PaymentRequest.NumberOfSelectionChanges." + name_suffix,
          std::min(sections_[i].number_selection_changes_, MAX_EXPECTED_SAMPLE),
          MIN_EXPECTED_SAMPLE, MAX_EXPECTED_SAMPLE, NUMBER_BUCKETS);
      base::UmaHistogramCustomCounts(
          "PaymentRequest.NumberOfSelectionEdits." + name_suffix,
          std::min(sections_[i].number_selection_edits_, MAX_EXPECTED_SAMPLE),
          MIN_EXPECTED_SAMPLE, MAX_EXPECTED_SAMPLE, NUMBER_BUCKETS);
      base::UmaHistogramCustomCounts(
          "PaymentRequest.NumberOfSuggestionsShown." + name_suffix,
          std::min(sections_[i].number_suggestions_shown_, MAX_EXPECTED_SAMPLE),
          MIN_EXPECTED_SAMPLE, MAX_EXPECTED_SAMPLE, NUMBER_BUCKETS);

      if (sections_[i].number_suggestions_shown_ == 0) {
        user_had_all_requested_information = false;
      }
    }
  }

  // Record metrics about completion based on whether the user had suggestions
  // for each requested information.
  if (user_had_all_requested_information) {
    base::UmaHistogramEnumeration(
        "PaymentRequest.UserHadSuggestionsForEverything."
        "EffectOnCompletion",
        completion_status, COMPLETION_STATUS_MAX);
  } else {
    base::UmaHistogramEnumeration(
        "PaymentRequest.UserDidNotHaveSuggestionsForEverything."
        "EffectOnCompletion",
        completion_status, COMPLETION_STATUS_MAX);
  }
}

void JourneyLogger::RecordCanMakePaymentStats(
    CompletionStatus completion_status) {
  // CanMakePayment always returns true in incognito mode. Don't log the
  // metrics.
  if (is_incognito_)
    return;

  // Record CanMakePayment usage.
  UMA_HISTOGRAM_ENUMERATION("PaymentRequest.CanMakePayment.Usage",
                            was_can_make_payments_used_
                                ? CAN_MAKE_PAYMENT_USED
                                : CAN_MAKE_PAYMENT_NOT_USED,
                            CAN_MAKE_PAYMENT_USE_MAX);

  RecordCanMakePaymentEffectOnShow();
  RecordCanMakePaymentEffectOnCompletion(completion_status);
}

void JourneyLogger::RecordCanMakePaymentEffectOnShow() {
  if (!was_can_make_payments_used_)
    return;

  int effect_on_show = 0;
  if (was_show_called_)
    effect_on_show |= CMP_SHOW_DID_SHOW;
  if (could_make_payment_)
    effect_on_show |= CMP_SHOW_COULD_MAKE_PAYMENT;

  UMA_HISTOGRAM_ENUMERATION("PaymentRequest.CanMakePayment.Used.EffectOnShow",
                            effect_on_show, CMP_SHOW_MAX);
}

void JourneyLogger::RecordCanMakePaymentEffectOnCompletion(
    CompletionStatus completion_status) {
  if (!was_show_called_)
    return;

  std::string histogram_name = "PaymentRequest.CanMakePayment.";
  if (!was_can_make_payments_used_) {
    histogram_name += "NotUsed.WithShowEffectOnCompletion";
  } else if (could_make_payment_) {
    histogram_name += "Used.TrueWithShowEffectOnCompletion";
  } else {
    histogram_name += "Used.FalseWithShowEffectOnCompletion";
  }

  base::UmaHistogramEnumeration(histogram_name, completion_status,
                                COMPLETION_STATUS_MAX);
}

void JourneyLogger::RecordUrlKeyedMetrics(CompletionStatus completion_status) {
  if (!ukm_service_ || !url_.is_valid())
    return;

  // Record the Checkout Funnel UKM.
  int32_t source_id = ukm_service_->GetNewSourceID();
  ukm_service_->UpdateSourceURL(source_id, url_);
  std::unique_ptr<ukm::UkmEntryBuilder> builder = ukm_service_->GetEntryBuilder(
      source_id, internal::kUKMCheckoutEventsEntryName);
  builder->AddMetric(internal::kUKMCompletionStatusMetricName,
                     completion_status);
  builder->AddMetric(internal::kUKMEventsMetricName, events_);
}

}  // namespace payments
