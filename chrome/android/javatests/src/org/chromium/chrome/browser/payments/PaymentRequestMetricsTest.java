// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.payments;

import static org.chromium.chrome.browser.payments.PaymentRequestTestRule.DELAYED_RESPONSE;
import static org.chromium.chrome.browser.payments.PaymentRequestTestRule.HAVE_INSTRUMENTS;
import static org.chromium.chrome.browser.payments.PaymentRequestTestRule.IMMEDIATE_RESPONSE;
import static org.chromium.chrome.browser.payments.PaymentRequestTestRule.NO_INSTRUMENTS;

import android.content.DialogInterface;
import android.support.test.InstrumentationRegistry;
import android.support.test.filters.MediumTest;

import org.junit.Assert;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ThreadUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeFeatureList;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.autofill.AutofillTestHelper;
import org.chromium.chrome.browser.autofill.PersonalDataManager.AutofillProfile;
import org.chromium.chrome.browser.autofill.PersonalDataManager.CreditCard;
import org.chromium.chrome.browser.payments.PaymentRequestTestRule.MainActivityStartCallback;
import org.chromium.chrome.test.ChromeActivityTestRule;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.util.ChromeTabUtils;

import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeoutException;

/**
 * A payment integration test to validate the logging of Payment Request metrics.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({
        ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE,
        ChromeActivityTestRule.DISABLE_NETWORK_PREDICTION_FLAG,
})
public class PaymentRequestMetricsTest implements MainActivityStartCallback {
    @Rule
    public PaymentRequestTestRule mPaymentRequestTestRule =
            new PaymentRequestTestRule("payment_request_metrics_test.html", this);

    @Override
    public void onMainActivityStarted()
            throws InterruptedException, ExecutionException, TimeoutException {
        AutofillTestHelper mHelper = new AutofillTestHelper();
        // The user has a shipping address and a credit card associated with that address on disk.
        String mBillingAddressId = mHelper.setProfile(new AutofillProfile("", "https://example.com",
                true, "Jon Doe", "Google", "340 Main St", "CA", "Los Angeles", "", "90291", "",
                "US", "650-253-0000", "", "en-US"));
        mHelper.setCreditCard(new CreditCard("", "https://example.com", true, true, "Jon Doe",
                "4111111111111111", "1111", "12", "2050", "visa", R.drawable.pr_visa,
                mBillingAddressId, "" /* serverId */));
    }

    /**
     * Expect that the successful checkout funnel metrics are logged during a succesful checkout.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testSuccessCheckoutFunnel()
            throws InterruptedException, ExecutionException, TimeoutException {
        // Initiate a payment request.
        mPaymentRequestTestRule.triggerUIAndWait("ccBuy", mPaymentRequestTestRule.getReadyToPay());

        // Click the pay button.
        mPaymentRequestTestRule.clickAndWait(
                R.id.button_primary, mPaymentRequestTestRule.getReadyForUnmaskInput());

        // Unmask the credit card,
        mPaymentRequestTestRule.setTextInCardUnmaskDialogAndWait(
                R.id.card_unmask_input, "123", mPaymentRequestTestRule.getReadyToUnmask());
        mPaymentRequestTestRule.clickCardUnmaskButtonAndWait(
                DialogInterface.BUTTON_POSITIVE, mPaymentRequestTestRule.getDismissed());

        // Make sure all the steps were logged.
        Assert.assertEquals(1,
                RecordHistogram.getHistogramValueCountForTesting(
                        "PaymentRequest.CheckoutFunnel.Initiated", 1));
        Assert.assertEquals(1,
                RecordHistogram.getHistogramValueCountForTesting(
                        "PaymentRequest.CheckoutFunnel.Shown", 1));
        Assert.assertEquals(1,
                RecordHistogram.getHistogramValueCountForTesting(
                        "PaymentRequest.CheckoutFunnel.PayClicked", 1));
        Assert.assertEquals(1,
                RecordHistogram.getHistogramValueCountForTesting(
                        "PaymentRequest.CheckoutFunnel.ReceivedInstrumentDetails", 1));
        Assert.assertEquals(1,
                RecordHistogram.getHistogramValueCountForTesting(
                        "PaymentRequest.CheckoutFunnel.Completed", 1));
    }

    /**
     * Expect only the ABORT_REASON_ABORTED_BY_USER enum value gets logged when a user cancels a
     * Payment Request.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testAbortMetrics_AbortedByUser_CancelButton()
            throws InterruptedException, ExecutionException, TimeoutException {
        mPaymentRequestTestRule.triggerUIAndWait("ccBuy", mPaymentRequestTestRule.getReadyToPay());
        mPaymentRequestTestRule.clickInShippingSummaryAndWait(
                R.id.payments_section, mPaymentRequestTestRule.getReadyForInput());

        // Cancel the Payment Request.
        int callCount = mPaymentRequestTestRule.getDismissed().getCallCount();
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                mPaymentRequestTestRule.getPaymentRequestUI()
                        .getDialogForTest()
                        .findViewById(R.id.button_secondary)
                        .performClick();
            }
        });
        mPaymentRequestTestRule.getDismissed().waitForCallback(callCount);
        mPaymentRequestTestRule.expectResultContains(new String[] {"Request cancelled"});

        mPaymentRequestTestRule.assertOnlySpecificAbortMetricLogged(
                PaymentRequestMetrics.ABORT_REASON_ABORTED_BY_USER);
    }

    /**
     * Expect only the ABORT_REASON_ABORTED_BY_USER enum value gets logged when a user presses on
     * the [X] button in the Payment Request dialog.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testAbortMetrics_AbortedByUser_XButton()
            throws InterruptedException, ExecutionException, TimeoutException {
        mPaymentRequestTestRule.triggerUIAndWait("ccBuy", mPaymentRequestTestRule.getReadyToPay());
        mPaymentRequestTestRule.clickInShippingSummaryAndWait(
                R.id.payments_section, mPaymentRequestTestRule.getReadyForInput());

        // Press the [X] button.
        mPaymentRequestTestRule.clickAndWait(
                R.id.close_button, mPaymentRequestTestRule.getDismissed());
        mPaymentRequestTestRule.expectResultContains(new String[] {"Request cancelled"});

        mPaymentRequestTestRule.assertOnlySpecificAbortMetricLogged(
                PaymentRequestMetrics.ABORT_REASON_ABORTED_BY_USER);
    }

    /**
     * Expect only the ABORT_REASON_ABORTED_BY_USER enum value gets logged when a user presses on
     * the back button on their phone during a Payment Request.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testAbortMetrics_AbortedByUser_BackButton()
            throws InterruptedException, ExecutionException, TimeoutException {
        mPaymentRequestTestRule.triggerUIAndWait("ccBuy", mPaymentRequestTestRule.getReadyToPay());
        mPaymentRequestTestRule.clickInShippingSummaryAndWait(
                R.id.payments_section, mPaymentRequestTestRule.getReadyForInput());

        // Press the back button.
        int callCount = mPaymentRequestTestRule.getDismissed().getCallCount();
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                mPaymentRequestTestRule.getPaymentRequestUI().getDialogForTest().onBackPressed();
            }
        });
        mPaymentRequestTestRule.getDismissed().waitForCallback(callCount);
        mPaymentRequestTestRule.expectResultContains(new String[] {"Request cancelled"});

        mPaymentRequestTestRule.assertOnlySpecificAbortMetricLogged(
                PaymentRequestMetrics.ABORT_REASON_ABORTED_BY_USER);
    }

    /**
     * Expect only the ABORT_REASON_MOJO_RENDERER_CLOSING enum value gets logged when a user closes
     * the tab during a Payment Request.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testAbortMetrics_AbortedByUser_TabClosed()
            throws InterruptedException, ExecutionException, TimeoutException {
        mPaymentRequestTestRule.triggerUIAndWait("ccBuy", mPaymentRequestTestRule.getReadyToPay());
        mPaymentRequestTestRule.clickInShippingSummaryAndWait(
                R.id.payments_section, mPaymentRequestTestRule.getReadyForInput());

        // Press the back button.
        ChromeTabUtils.closeCurrentTab(InstrumentationRegistry.getInstrumentation(),
                mPaymentRequestTestRule.getActivity());

        mPaymentRequestTestRule.assertOnlySpecificAbortMetricLogged(
                PaymentRequestMetrics.ABORT_REASON_MOJO_RENDERER_CLOSING);
    }

    /**
     * Expect only the ABORT_REASON_ABORTED_BY_MERCHANT enum value gets logged when a Payment
     * Request gets cancelled by the merchant.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testAbortMetrics_AbortedByMerchant()
            throws InterruptedException, ExecutionException, TimeoutException {
        mPaymentRequestTestRule.triggerUIAndWait("ccBuy", mPaymentRequestTestRule.getReadyToPay());

        // Simulate an abort by the merchant.
        mPaymentRequestTestRule.clickNodeAndWait("abort", mPaymentRequestTestRule.getDismissed());
        mPaymentRequestTestRule.expectResultContains(new String[] {"Abort"});

        mPaymentRequestTestRule.assertOnlySpecificAbortMetricLogged(
                PaymentRequestMetrics.ABORT_REASON_ABORTED_BY_MERCHANT);
    }

    /**
     * Expect no abort metrics to be logged even if there are no matching payment methods because
     * the Payment Request was not shown to the user (a Payment Request gets cancelled because the
     * user does not have any of the payment methods accepted by the merchant and the merchant does
     * not accept credit cards). It should instead be logged as a reason why the Payment Request was
     * not shown to the user.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testMetrics_NoMatchingPaymentMethod()
            throws InterruptedException, ExecutionException, TimeoutException {
        // Android Pay is supported but no instruments are present.
        mPaymentRequestTestRule.installPaymentApp(
                "https://android.com/pay", NO_INSTRUMENTS, DELAYED_RESPONSE);
        mPaymentRequestTestRule.openPageAndClickNodeAndWait(
                "androidPayBuy", mPaymentRequestTestRule.getShowFailed());
        mPaymentRequestTestRule.expectResultContains(
                new String[] {"The payment method is not supported"});

        // Make sure that it is not logged as an abort.
        mPaymentRequestTestRule.assertOnlySpecificAbortMetricLogged(-1 /* none */);
        // Make sure that it was logged as a reason why the Payment Request was not shown.
        Assert.assertEquals(1,
                RecordHistogram.getHistogramValueCountForTesting(
                        "PaymentRequest.CheckoutFunnel.NoShow",
                        PaymentRequestMetrics.NO_SHOW_NO_MATCHING_PAYMENT_METHOD));
    }

    /**
     * Expect no abort metrics to be logged even if there are no matching payment methods because
     * the Payment Request was not shown to the user (a Payment Request gets cancelled because the
     * merchant only accepts payment methods we don't support. It should instead be logged as a
     * reason why the Payment Request was not shown to the user.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testMetrics_NoSupportedPaymentMethod()
            throws InterruptedException, ExecutionException, TimeoutException {
        mPaymentRequestTestRule.openPageAndClickNodeAndWait(
                "noSupported", mPaymentRequestTestRule.getShowFailed());
        mPaymentRequestTestRule.expectResultContains(
                new String[] {"The payment method is not supported"});

        // Make sure that it is not logged as an abort.
        mPaymentRequestTestRule.assertOnlySpecificAbortMetricLogged(-1 /* none */);
        // Make sure that it was logged as a reason why the Payment Request was not shown.
        Assert.assertEquals(1,
                RecordHistogram.getHistogramValueCountForTesting(
                        "PaymentRequest.CheckoutFunnel.NoShow",
                        PaymentRequestMetrics.NO_SHOW_NO_SUPPORTED_PAYMENT_METHOD));
    }

    /**
     * Expect only the SELECTED_METHOD_CREDIT_CARD enum value to be logged for the
     * "SelectedPaymentMethod" histogram when completing a Payment Request with a credit card.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testSelectedPaymentMethod_CreditCard()
            throws InterruptedException, ExecutionException, TimeoutException {
        // Complete a Payment Request with a credit card.
        mPaymentRequestTestRule.triggerUIAndWait("ccBuy", mPaymentRequestTestRule.getReadyToPay());
        mPaymentRequestTestRule.clickAndWait(
                R.id.button_primary, mPaymentRequestTestRule.getReadyForUnmaskInput());
        mPaymentRequestTestRule.setTextInCardUnmaskDialogAndWait(
                R.id.card_unmask_input, "123", mPaymentRequestTestRule.getReadyToUnmask());
        mPaymentRequestTestRule.clickCardUnmaskButtonAndWait(
                DialogInterface.BUTTON_POSITIVE, mPaymentRequestTestRule.getDismissed());

        assertOnlySpecificSelectedPaymentMethodMetricLogged(
                PaymentRequestMetrics.SELECTED_METHOD_CREDIT_CARD);
    }

    /**
     * Expect only the SELECTED_METHOD_ANDROID_PAY enum value to be logged for the
     * "SelectedPaymentMethod" histogram when completing a Payment Request with Android Pay.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testSelectedPaymentMethod_AndroidPay()
            throws InterruptedException, ExecutionException, TimeoutException {
        // Complete a Payment Request with Android Pay.
        mPaymentRequestTestRule.installPaymentApp(
                "https://android.com/pay", HAVE_INSTRUMENTS, IMMEDIATE_RESPONSE);
        mPaymentRequestTestRule.triggerUIAndWait(
                "androidPayBuy", mPaymentRequestTestRule.getReadyToPay());
        mPaymentRequestTestRule.clickAndWait(
                R.id.button_primary, mPaymentRequestTestRule.getDismissed());

        assertOnlySpecificSelectedPaymentMethodMetricLogged(
                PaymentRequestMetrics.SELECTED_METHOD_ANDROID_PAY);
    }

    /**
     * Expect that the SkippedShow metric is logged when the UI directly goes
     * to the payment app UI during a Payment Request.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testMetrics_SkippedShow()
            throws InterruptedException, ExecutionException, TimeoutException {
        // Complete a Payment Request with Android Pay.
        mPaymentRequestTestRule.installPaymentApp(
                "https://android.com/pay", HAVE_INSTRUMENTS, IMMEDIATE_RESPONSE);
        mPaymentRequestTestRule.triggerUIAndWait(
                "androidPaySkipUiBuy", mPaymentRequestTestRule.getResultReady());

        // The "SkippedShow" step should be logged instead of "Shown".
        Assert.assertEquals(1,
                RecordHistogram.getHistogramValueCountForTesting(
                        "PaymentRequest.CheckoutFunnel.SkippedShow", 1));
        Assert.assertEquals(0,
                RecordHistogram.getHistogramValueCountForTesting(
                        "PaymentRequest.CheckoutFunnel.Shown", 1));
    }

    /**
     * Expect that the PaymentRequest UI is shown even if all the requirements are met to skip, if
     * the skip feature is disabled.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    @CommandLineFlags.Add({"disable-features=" + ChromeFeatureList.WEB_PAYMENTS_SINGLE_APP_UI_SKIP})
    public void testMetrics_SkippedShow_Disabled()
            throws InterruptedException, ExecutionException, TimeoutException {
        // Complete a Payment Request with Android Pay.
        mPaymentRequestTestRule.installPaymentApp(
                "https://android.com/pay", HAVE_INSTRUMENTS, IMMEDIATE_RESPONSE);
        mPaymentRequestTestRule.triggerUIAndWait(
                "androidPaySkipUiBuy", mPaymentRequestTestRule.getReadyToPay());

        // Close the payment Request.
        mPaymentRequestTestRule.clickAndWait(
                R.id.close_button, mPaymentRequestTestRule.getDismissed());
        mPaymentRequestTestRule.expectResultContains(new String[] {"Request cancelled"});

        // The "Shown" step should be logged, not "SkippedShow".
        Assert.assertEquals(1,
                RecordHistogram.getHistogramValueCountForTesting(
                        "PaymentRequest.CheckoutFunnel.Shown", 1));
        Assert.assertEquals(0,
                RecordHistogram.getHistogramValueCountForTesting(
                        "PaymentRequest.CheckoutFunnel.SkippedShow", 1));
    }

    /**
     * Expect that the "Shown" event is recorded only once.
     */
    @Test
    @MediumTest
    @Feature({"Payments"})
    public void testShownLoggedOnlyOnce()
            throws InterruptedException, ExecutionException, TimeoutException {
        // Initiate a payment request.
        mPaymentRequestTestRule.triggerUIAndWait("ccBuy", mPaymentRequestTestRule.getReadyToPay());

        // Add a shipping address, which triggers a second "Show".
        mPaymentRequestTestRule.clickInShippingSummaryAndWait(
                R.id.payments_section, mPaymentRequestTestRule.getReadyForInput());
        mPaymentRequestTestRule.clickInShippingAddressAndWait(
                R.id.payments_add_option_button, mPaymentRequestTestRule.getReadyToEdit());
        mPaymentRequestTestRule.setTextInEditorAndWait(
                new String[] {"Seb Doe", "Google", "340 Main St", "Los Angeles", "CA", "90291",
                        "650-253-0000"},
                mPaymentRequestTestRule.getEditorTextUpdate());
        mPaymentRequestTestRule.clickInEditorAndWait(
                R.id.payments_edit_done_button, mPaymentRequestTestRule.getReadyToPay());

        // Close the payment Request.
        mPaymentRequestTestRule.clickAndWait(
                R.id.close_button, mPaymentRequestTestRule.getDismissed());
        mPaymentRequestTestRule.expectResultContains(new String[] {"Request cancelled"});

        // Make sure "Shown" is logged only once.
        Assert.assertEquals(1,
                RecordHistogram.getHistogramValueCountForTesting(
                        "PaymentRequest.CheckoutFunnel.Shown", 1));
    }

    /**
     * Asserts that only the specified selected payment method is logged.
     *
     * @param paymentMethod The only bucket in the selected payment method histogram that should
     *                      have a record.
     */
    private void assertOnlySpecificSelectedPaymentMethodMetricLogged(int paymentMethod) {
        for (int i = 0; i < PaymentRequestMetrics.SELECTED_METHOD_MAX; ++i) {
            Assert.assertEquals((i == paymentMethod ? 1 : 0),
                    RecordHistogram.getHistogramValueCountForTesting(
                            "PaymentRequest.SelectedPaymentMethod", i));
        }
    }
}
