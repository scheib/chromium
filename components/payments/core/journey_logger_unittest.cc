// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/payments/core/journey_logger.h"

#include "base/metrics/metrics_hashes.h"
#include "base/test/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "components/metrics/proto/ukm/entry.pb.h"
#include "components/ukm/test_ukm_service.h"
#include "components/ukm/ukm_entry.h"
#include "components/ukm/ukm_source.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::ContainerEq;

namespace payments {

namespace {
// Finds the specified UKM metric by |name| in the specified UKM |metrics|.
const ukm::Entry_Metric* FindMetric(
    const char* name,
    const google::protobuf::RepeatedPtrField<ukm::Entry_Metric>& metrics) {
  for (const auto& metric : metrics) {
    if (metric.metric_hash() == base::HashMetricName(name))
      return &metric;
  }
  return nullptr;
}
}  // namespace

// Tests the canMakePayment stats for the case where the merchant does not use
// it and does not show the PaymentRequest to the user.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePaymentNotCalled_NoShow) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_COMPLETED);

  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_NOT_USED,
                                     1);

  // There should be no completion stats since PR was not shown to the user
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix(
          "PaymentRequest.CanMakePayment.NotUsed.WithShowEffectOnCompletion"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));
}

// Tests the canMakePayment stats for the case where the merchant does not use
// it and the transaction is aborted.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePaymentNotCalled_ShowAndUserAbort) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));

  // The merchant does not query CanMakePayment, show the PaymentRequest and the
  // user aborts it.
  logger.SetShowCalled();
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED);

  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_NOT_USED,
                                     1);

  // There should be a record for an abort when CanMakePayment is not used but
  // the PR is shown to the user.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.NotUsed.WithShowEffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED, 1);
}

// Tests the canMakePayment stats for the case where the merchant does not use
// it and the transaction is aborted.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePaymentNotCalled_ShowAndOtherAbort) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));

  // The merchant does not query CanMakePayment, show the PaymentRequest and
  // there is an abort not initiated by the user.
  logger.SetShowCalled();
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED);

  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_NOT_USED,
                                     1);

  // There should be a record for an abort when CanMakePayment is not used but
  // the PR is shown to the user.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.NotUsed.WithShowEffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED, 1);
}

// Tests the canMakePayment stats for the case where the merchant does not use
// it and the transaction is completed.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePaymentNotCalled_ShowAndComplete) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));

  // The merchant does not query CanMakePayment, show the PaymentRequest and the
  // user completes it.
  logger.SetShowCalled();
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_COMPLETED);

  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_NOT_USED,
                                     1);

  // There should be a record for an abort when CanMakePayment is not used but
  // the PR is shown to the user.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.NotUsed.WithShowEffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_COMPLETED, 1);
}

// Tests the canMakePayment stats for the case where the merchant uses it,
// returns false and show is not called.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePaymentCalled_FalseAndNoShow) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));

  // The user cannot make payment and the PaymentRequest is not shown.
  logger.SetCanMakePaymentValue(false);
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED);

  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_USED, 1);

  // The CanMakePayment effect on show should be recorded as being false and not
  // shown.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.EffectOnShow",
      JourneyLogger::CMP_SHOW_COULD_NOT_MAKE_PAYMENT_AND_DID_NOT_SHOW, 1);

  // There should be no completion stats since PR was not shown to the user.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix(
          "PaymentRequest.CanMakePayment.NotUsed.WithShowEffectOnCompletion"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));
}

// Tests the canMakePayment stats for the case where the merchant uses it,
// returns true and show is not called.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePaymentCalled_TrueAndNoShow) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));

  // The user cannot make payment and the PaymentRequest is not shown.
  logger.SetCanMakePaymentValue(true);
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED);

  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_USED, 1);

  // The CanMakePayment effect on show should be recorded as being true and not
  // shown.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.EffectOnShow",
      JourneyLogger::CMP_SHOW_COULD_MAKE_PAYMENT, 1);

  // There should be no completion stats since PR was not shown to the user.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix(
          "PaymentRequest.CanMakePayment.NotUsed.WithShowEffectOnCompletion"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));
}

// Tests the canMakePayment stats for the case where the merchant uses it,
// returns false, show is called but the transaction is aborted by the user.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePaymentCalled_FalseShowAndUserAbort) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));

  // The user cannot make payment and the PaymentRequest is not shown.
  logger.SetShowCalled();
  logger.SetCanMakePaymentValue(false);
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED);

  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_USED, 1);

  // The CanMakePayment effect on show should be recorded as being false and
  // shown.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.EffectOnShow",
      JourneyLogger::CMP_SHOW_DID_SHOW, 1);
  // There should be a record for an abort when CanMakePayment is false but the
  // PR is shown to the user.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.FalseWithShowEffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED, 1);
}

// Tests the canMakePayment stats for the case where the merchant uses it,
// returns false, show is called but the transaction is aborted.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePaymentCalled_FalseShowAndOtherAbort) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));

  // The user cannot make payment and the PaymentRequest is not shown.
  logger.SetShowCalled();
  logger.SetCanMakePaymentValue(false);
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED);

  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_USED, 1);

  // The CanMakePayment effect on show should be recorded as being false and
  // shown.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.EffectOnShow",
      JourneyLogger::CMP_SHOW_DID_SHOW, 1);
  // There should be a record for an abort when CanMakePayment is false but the
  // PR is shown to the user.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.FalseWithShowEffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED, 1);
}

// Tests the canMakePayment stats for the case where the merchant uses it,
// returns false, show is called and the transaction is completed.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePaymentCalled_FalseShowAndComplete) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));

  // The user cannot make payment and the PaymentRequest is not shown.
  logger.SetShowCalled();
  logger.SetCanMakePaymentValue(false);
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_COMPLETED);

  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_USED, 1);

  // The CanMakePayment effect on show should be recorded as being false and
  // shown.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.EffectOnShow",
      JourneyLogger::CMP_SHOW_DID_SHOW, 1);

  // There should be a record for an completion when CanMakePayment is false but
  // the PR is shown to the user.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.FalseWithShowEffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_COMPLETED, 1);
}

// Tests the canMakePayment stats for the case where the merchant uses it,
// returns true, show is called but the transaction is aborted by the user.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePaymentCalled_TrueShowAndUserAbort) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));

  // The user cannot make payment and the PaymentRequest is not shown.
  logger.SetShowCalled();
  logger.SetCanMakePaymentValue(true);
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED);

  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_USED, 1);

  // The CanMakePayment effect on show should be recorded as being true and not
  // shown.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.EffectOnShow",
      JourneyLogger::CMP_SHOW_DID_SHOW |
          JourneyLogger::CMP_SHOW_COULD_MAKE_PAYMENT,
      1);
  // There should be a record for an abort when CanMakePayment is true and the
  // PR is shown to the user.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.TrueWithShowEffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED, 1);
}

// Tests the canMakePayment stats for the case where the merchant uses it,
// returns true, show is called but the transaction is aborted.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePaymentCalled_TrueShowAndOtherAbort) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));

  // The user cannot make payment and the PaymentRequest is not shown.
  logger.SetShowCalled();
  logger.SetCanMakePaymentValue(true);
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED);

  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_USED, 1);

  // The CanMakePayment effect on show should be recorded as being true and not
  // shown.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.EffectOnShow",
      JourneyLogger::CMP_SHOW_DID_SHOW |
          JourneyLogger::CMP_SHOW_COULD_MAKE_PAYMENT,
      1);
  // There should be a record for an abort when CanMakePayment is true and the
  // PR is shown to the user.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.TrueWithShowEffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED, 1);
}

// Tests the canMakePayment stats for the case where the merchant uses it,
// returns true, show is called and the transaction is completed.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePaymentCalled_TrueShowAndComplete) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));

  // The user cannot make payment and the PaymentRequest is not shown.
  logger.SetShowCalled();
  logger.SetCanMakePaymentValue(true);
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_COMPLETED);

  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_USED, 1);

  // The CanMakePayment effect on show should be recorded as being true and not
  // shown.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.EffectOnShow",
      JourneyLogger::CMP_SHOW_DID_SHOW |
          JourneyLogger::CMP_SHOW_COULD_MAKE_PAYMENT,
      1);
  // There should be a record for a completion when CanMakePayment is true and
  // the PR is shown to the user.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.TrueWithShowEffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_COMPLETED, 1);
}

// Tests the canMakePayment metrics are not logged if the Payment Request was
// done in an incognito tab.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_CanMakePayment_IncognitoTab) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/true, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));

  // The user cannot make payment and the PaymentRequest is not shown.
  logger.SetShowCalled();
  logger.SetCanMakePaymentValue(true);
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_COMPLETED);

  // Expect no log for CanMakePayment.
  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix("PaymentRequest.CanMakePayment"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));
}

// Tests that the completion status metrics based on whether the user had
// suggestions for all the requested sections are logged as correctly.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_SuggestionsForEverything_Completed) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Simulate that the user had suggestions for all the requested sections.
  logger.SetNumberOfSuggestionsShown(JourneyLogger::SECTION_CREDIT_CARDS, 1);

  // Simulate that the user completes the checkout.
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_COMPLETED);

  histogram_tester.ExpectBucketCount(
      "PaymentRequest.UserHadSuggestionsForEverything.EffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_COMPLETED, 1);

  EXPECT_THAT(histogram_tester.GetTotalCountsForPrefix(
                  "PaymentRequest.UserDidNotHaveSuggestionsForEverything."
                  "EffectOnCompletion"),
              testing::ContainerEq(base::HistogramTester::CountsMap()));
}

// Tests that the completion status metrics based on whether the user had
// suggestions for all the requested sections are logged as correctly.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_SuggestionsForEverything_UserAborted) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Simulate that the user had suggestions for all the requested sections.
  logger.SetNumberOfSuggestionsShown(JourneyLogger::SECTION_CREDIT_CARDS, 1);

  // Simulate that the user aborts the checkout.
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED);

  histogram_tester.ExpectBucketCount(
      "PaymentRequest.UserHadSuggestionsForEverything.EffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED, 1);

  EXPECT_THAT(histogram_tester.GetTotalCountsForPrefix(
                  "PaymentRequest.UserDidNotHaveSuggestionsForEverything."
                  "EffectOnCompletion"),
              testing::ContainerEq(base::HistogramTester::CountsMap()));
}

// Tests that the completion status metrics based on whether the user had
// suggestions for all the requested sections are logged as correctly.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_SuggestionsForEverything_OtherAborted) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Simulate that the user had suggestions for all the requested sections.
  logger.SetNumberOfSuggestionsShown(JourneyLogger::SECTION_CREDIT_CARDS, 1);

  // Simulate that the checkout is aborted.
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED);

  histogram_tester.ExpectBucketCount(
      "PaymentRequest.UserHadSuggestionsForEverything.EffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED, 1);

  EXPECT_THAT(histogram_tester.GetTotalCountsForPrefix(
                  "PaymentRequest.UserDidNotHaveSuggestionsForEverything."
                  "EffectOnCompletion"),
              testing::ContainerEq(base::HistogramTester::CountsMap()));
}

// Tests that the completion status metrics based on whether the user had
// suggestions for all the requested sections are logged as correctly, even in
// incognito mode.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_SuggestionsForEverything_Incognito) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/true, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Simulate that the user had suggestions for all the requested sections.
  logger.SetNumberOfSuggestionsShown(JourneyLogger::SECTION_CREDIT_CARDS, 1);

  // Simulate that the user completes the checkout.
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_COMPLETED);

  histogram_tester.ExpectBucketCount(
      "PaymentRequest.UserHadSuggestionsForEverything.EffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_COMPLETED, 1);

  EXPECT_THAT(histogram_tester.GetTotalCountsForPrefix(
                  "PaymentRequest.UserDidNotHaveSuggestionsForEverything."
                  "EffectOnCompletion"),
              testing::ContainerEq(base::HistogramTester::CountsMap()));
}

// Tests that the completion status metrics based on whether the user had
// suggestions for all the requested sections are logged as correctly.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_NoSuggestionsForEverything_Completed) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Simulate that the user had suggestions for all the requested sections.
  logger.SetNumberOfSuggestionsShown(JourneyLogger::SECTION_CREDIT_CARDS, 0);

  // Simulate that the user completes the checkout.
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_COMPLETED);

  histogram_tester.ExpectBucketCount(
      "PaymentRequest.UserDidNotHaveSuggestionsForEverything."
      "EffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_COMPLETED, 1);

  EXPECT_THAT(
      histogram_tester.GetTotalCountsForPrefix(
          "PaymentRequest.UserHadSuggestionsForEverything.EffectOnCompletion"),
      testing::ContainerEq(base::HistogramTester::CountsMap()));
}

// Tests that the completion status metrics based on whether the user had
// suggestions for all the requested sections are logged as correctly.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_NoSuggestionsForEverything_UserAborted) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Simulate that the user had suggestions for all the requested sections.
  logger.SetNumberOfSuggestionsShown(JourneyLogger::SECTION_CREDIT_CARDS, 0);

  // Simulate that the user aborts the checkout.
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED);

  histogram_tester.ExpectBucketCount(
      "PaymentRequest.UserDidNotHaveSuggestionsForEverything."
      "EffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED, 1);

  EXPECT_THAT(histogram_tester.GetTotalCountsForPrefix(
                  "PaymentRequest.UserHadSuggestionsForEverything."
                  "EffectOnCompletion"),
              testing::ContainerEq(base::HistogramTester::CountsMap()));
}

// Tests that the completion status metrics based on whether the user had
// suggestions for all the requested sections are logged as correctly.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_NoSuggestionsForEverything_OtherAborted) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/false, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Simulate that the user had suggestions for all the requested sections.
  logger.SetNumberOfSuggestionsShown(JourneyLogger::SECTION_CREDIT_CARDS, 0);

  // Simulate that the user aborts the checkout.
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED);

  histogram_tester.ExpectBucketCount(
      "PaymentRequest.UserDidNotHaveSuggestionsForEverything."
      "EffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED, 1);

  EXPECT_THAT(histogram_tester.GetTotalCountsForPrefix(
                  "PaymentRequest.UserHadSuggestionsForEverything."
                  "EffectOnCompletion"),
              testing::ContainerEq(base::HistogramTester::CountsMap()));
}

// Tests that the completion status metrics based on whether the user had
// suggestions for all the requested sections are logged as correctly, even in
// incognito mode.
TEST(JourneyLoggerTest,
     RecordJourneyStatsHistograms_NoSuggestionsForEverything_Incognito) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/true, /*url=*/GURL(""),
                       /*ukm_service=*/nullptr);

  // Simulate that the user had suggestions for all the requested sections.
  logger.SetNumberOfSuggestionsShown(JourneyLogger::SECTION_CREDIT_CARDS, 0);

  // Simulate that the user aborts the checkout.
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED);

  histogram_tester.ExpectBucketCount(
      "PaymentRequest.UserDidNotHaveSuggestionsForEverything."
      "EffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED, 1);

  EXPECT_THAT(histogram_tester.GetTotalCountsForPrefix(
                  "PaymentRequest.UserHadSuggestionsForEverything."
                  "EffectOnCompletion"),
              testing::ContainerEq(base::HistogramTester::CountsMap()));
}

// Tests that the metrics are logged correctly for two simultaneous Payment
// Requests.
TEST(JourneyLoggerTest, RecordJourneyStatsHistograms_TwoPaymentRequests) {
  base::HistogramTester histogram_tester;
  JourneyLogger logger1(/*is_incognito=*/false, /*url=*/GURL(""),
                        /*ukm_service=*/nullptr);
  JourneyLogger logger2(/*is_incognito=*/false, /*url=*/GURL(""),
                        /*ukm_service=*/nullptr);

  // Make the two loggers have different data.
  logger1.SetShowCalled();
  logger2.SetShowCalled();

  logger1.SetCanMakePaymentValue(true);

  logger1.SetNumberOfSuggestionsShown(JourneyLogger::SECTION_CREDIT_CARDS, 1);
  logger2.SetNumberOfSuggestionsShown(JourneyLogger::SECTION_CREDIT_CARDS, 0);

  // Simulate that the user completes one checkout and aborts the other.
  logger1.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_COMPLETED);
  logger2.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED);

  // Make sure the appropriate metrics were logged for logger1.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.UserHadSuggestionsForEverything.EffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_COMPLETED, 1);
  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_USED, 1);
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.Used.TrueWithShowEffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_COMPLETED, 1);

  // Make sure the appropriate metrics were logged for logger2.
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.UserDidNotHaveSuggestionsForEverything."
      "EffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED, 1);
  histogram_tester.ExpectBucketCount("PaymentRequest.CanMakePayment.Usage",
                                     JourneyLogger::CAN_MAKE_PAYMENT_NOT_USED,
                                     1);
  histogram_tester.ExpectBucketCount(
      "PaymentRequest.CanMakePayment.NotUsed.WithShowEffectOnCompletion",
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED, 1);
}

// Tests that the Payment Request UKMs are logged correctly.
TEST(JourneyLoggerTest, RecordJourneyStatsHistograms_CheckoutFunnelUkm) {
  ukm::UkmServiceTestingHarness ukm_service_test_harness;
  ukm::TestUkmService* ukm_service =
      ukm_service_test_harness.test_ukm_service();
  char test_url[] = "http://www.google.com/";

  base::HistogramTester histogram_tester;
  JourneyLogger logger(/*is_incognito=*/true, /*url=*/GURL(test_url),
                       /*ukm_service=*/ukm_service);

  // Simulate that the user aborts after being shown the Payment Request and
  // clicking pay.
  logger.SetEventOccurred(JourneyLogger::EVENT_SHOWN);
  logger.SetEventOccurred(JourneyLogger::EVENT_PAY_CLICKED);
  logger.RecordJourneyStatsHistograms(
      JourneyLogger::COMPLETION_STATUS_USER_ABORTED);

  // Make sure the UKM was logged correctly.
  ASSERT_EQ(1U, ukm_service->sources_count());
  const ukm::UkmSource* source = ukm_service->GetSourceForUrl(test_url);
  ASSERT_NE(nullptr, source);

  ASSERT_EQ(1U, ukm_service->entries_count());
  const ukm::UkmEntry* entry = ukm_service->GetEntry(0);
  EXPECT_EQ(source->id(), entry->source_id());

  ukm::Entry entry_proto;
  entry->PopulateProto(&entry_proto);
  EXPECT_EQ(source->id(), entry_proto.source_id());
  EXPECT_EQ(base::HashMetricName(internal::kUKMCheckoutEventsEntryName),
            entry_proto.event_hash());

  const ukm::Entry_Metric* status_metric = FindMetric(
      internal::kUKMCompletionStatusMetricName, entry_proto.metrics());
  ASSERT_NE(nullptr, status_metric);
  EXPECT_EQ(JourneyLogger::COMPLETION_STATUS_USER_ABORTED,
            status_metric->value());

  const ukm::Entry_Metric* step_metric =
      FindMetric(internal::kUKMEventsMetricName, entry_proto.metrics());
  ASSERT_NE(nullptr, step_metric);
  EXPECT_EQ(JourneyLogger::EVENT_SHOWN | JourneyLogger::EVENT_PAY_CLICKED,
            step_metric->value());
}

}  // namespace payments