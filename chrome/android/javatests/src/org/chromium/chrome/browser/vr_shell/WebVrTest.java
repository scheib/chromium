// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.vr_shell;

import static org.chromium.chrome.browser.vr_shell.VrTestRule.PAGE_LOAD_TIMEOUT_S;
import static org.chromium.chrome.browser.vr_shell.VrUtils.POLL_TIMEOUT_SHORT_MS;
import static org.chromium.chrome.test.util.ChromeRestriction.RESTRICTION_TYPE_VIEWER_DAYDREAM;
import static org.chromium.chrome.test.util.ChromeRestriction.RESTRICTION_TYPE_VIEWER_NON_DAYDREAM;

import android.os.Build;
import android.support.test.filters.LargeTest;
import android.support.test.filters.MediumTest;
import android.support.test.filters.SmallTest;
import android.widget.TextView;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.DisableIf;
import org.chromium.base.test.util.MinAndroidSdkLevel;
import org.chromium.base.test.util.Restriction;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.test.ChromeActivityTestRule;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.ChromeTabbedActivityTestRule;
import org.chromium.content.browser.ContentViewCore;
import org.chromium.content_public.browser.WebContents;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * End-to-end tests for WebVR using the WebVR test framework from VrTestRule.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE,
        ChromeActivityTestRule.DISABLE_NETWORK_PREDICTION_FLAG, "enable-webvr"})
@MinAndroidSdkLevel(Build.VERSION_CODES.KITKAT) // WebVR is only supported on K+
public class WebVrTest {
    @Rule
    public ChromeTabbedActivityTestRule mActivityTestRule = new ChromeTabbedActivityTestRule();
    @Rule
    public VrTestRule mVrTestRule = new VrTestRule();

    private ContentViewCore mFirstTabCvc;
    private WebContents mFirstTabWebContents;

    @Before
    public void setUp() throws InterruptedException {
        mActivityTestRule.startMainActivityOnBlankPage();
        mFirstTabCvc = mActivityTestRule.getActivity().getActivityTab().getContentViewCore();
        mFirstTabWebContents = mActivityTestRule.getActivity().getActivityTab().getWebContents();
        Assert.assertFalse("VrShellDelegate is in VR", VrShellDelegate.isInVr());
    }

    /**
     * Tests that a successful requestPresent call actually enters VR
     */
    @Test
    @SmallTest
    public void testRequestPresentEntersVr() throws InterruptedException {
        mActivityTestRule.loadUrl(
                VrTestRule.getHtmlTestFile("test_requestPresent_enters_vr"), PAGE_LOAD_TIMEOUT_S);
        Assert.assertTrue("VRDisplay found", mVrTestRule.vrDisplayFound(mFirstTabWebContents));
        mVrTestRule.enterPresentationAndWait(mFirstTabCvc, mFirstTabWebContents);
        Assert.assertTrue("VrShellDelegate is in VR", VrShellDelegate.isInVr());
        mVrTestRule.endTest(mFirstTabWebContents);
    }

    /**
     * Tests that scanning the Daydream View NFC tag on supported devices fires the
     * vrdisplayactivate event.
     */
    @Test
    @SmallTest
    @Restriction(RESTRICTION_TYPE_VIEWER_DAYDREAM)
    public void testNfcFiresVrdisplayactivate() throws InterruptedException {
        mActivityTestRule.loadUrl(VrTestRule.getHtmlTestFile("test_nfc_fires_vrdisplayactivate"),
                PAGE_LOAD_TIMEOUT_S);
        mVrTestRule.simNfcScanAndWait(mActivityTestRule.getActivity(), mFirstTabWebContents);
        mVrTestRule.endTest(mFirstTabWebContents);
    }

    /**
     * Tests that screen touches are not registered when the viewer is a Daydream View.
     */
    @Test
    @LargeTest
    @Restriction(RESTRICTION_TYPE_VIEWER_DAYDREAM)
    public void testScreenTapsNotRegisteredOnDaydream() throws InterruptedException {
        mActivityTestRule.loadUrl(
                VrTestRule.getHtmlTestFile("test_screen_taps_not_registered_on_daydream"),
                PAGE_LOAD_TIMEOUT_S);
        Assert.assertTrue("VRDisplay found", mVrTestRule.vrDisplayFound(mFirstTabWebContents));
        mVrTestRule.executeStepAndWait("stepVerifyNoInitialTaps()", mFirstTabWebContents);
        mVrTestRule.enterPresentationAndWait(mFirstTabCvc, mFirstTabWebContents);
        // Wait on VrShellImpl to say that its parent consumed the touch event
        // Set to 2 because there's an ACTION_DOWN followed by ACTION_UP
        final CountDownLatch touchRegisteredLatch = new CountDownLatch(2);
        ((VrShellImpl) VrShellDelegate.getVrShellForTesting())
                .setOnDispatchTouchEventForTesting(new OnDispatchTouchEventCallback() {
                    @Override
                    public void onDispatchTouchEvent(
                            boolean parentConsumed, boolean cardboardTriggered) {
                        if (!parentConsumed) Assert.fail("Parent did not consume event");
                        if (cardboardTriggered) Assert.fail("Cardboard event triggered");
                        touchRegisteredLatch.countDown();
                    }
                });
        mVrTestRule.sendCardboardClick(mActivityTestRule.getActivity());
        Assert.assertTrue("VrShellImpl dispatched touches",
                touchRegisteredLatch.await(POLL_TIMEOUT_SHORT_MS, TimeUnit.MILLISECONDS));
        mVrTestRule.executeStepAndWait("stepVerifyNoAdditionalTaps()", mFirstTabWebContents);
        mVrTestRule.endTest(mFirstTabWebContents);
    }

    /**
     * Tests that Daydream controller clicks are registered as screen taps when the viewer is a
     * Daydream View.
     */
    @Test
    @LargeTest
    @Restriction(RESTRICTION_TYPE_VIEWER_DAYDREAM)
    public void testControllerClicksRegisteredAsTapsOnDaydream() throws InterruptedException {
        EmulatedVrController controller = new EmulatedVrController(mActivityTestRule.getActivity());
        mActivityTestRule.loadUrl(
                VrTestRule.getHtmlTestFile("test_screen_taps_registered"), PAGE_LOAD_TIMEOUT_S);
        Assert.assertTrue("VRDisplay found", mVrTestRule.vrDisplayFound(mFirstTabWebContents));
        mVrTestRule.executeStepAndWait("stepVerifyNoInitialTaps()", mFirstTabWebContents);
        // Wait to enter VR
        mVrTestRule.enterPresentationAndWait(mFirstTabCvc, mFirstTabWebContents);
        // Send a controller click and wait for JavaScript to receive it
        controller.pressReleaseTouchpadButton();
        mVrTestRule.waitOnJavaScriptStep(mFirstTabWebContents);
        mVrTestRule.endTest(mFirstTabWebContents);
    }

    /**
     * Tests that screen touches are still registered when the viewer is Cardboard.
     */
    @Test
    @MediumTest
    @Restriction(RESTRICTION_TYPE_VIEWER_NON_DAYDREAM)
    @DisableIf.Build(message = "Flaky on L crbug.com/713781",
            sdk_is_greater_than = Build.VERSION_CODES.KITKAT,
            sdk_is_less_than = Build.VERSION_CODES.M)
    public void testScreenTapsRegisteredOnCardboard() throws InterruptedException {
        mActivityTestRule.loadUrl(
                VrTestRule.getHtmlTestFile("test_screen_taps_registered"), PAGE_LOAD_TIMEOUT_S);
        Assert.assertTrue("VRDisplay found", mVrTestRule.vrDisplayFound(mFirstTabWebContents));
        mVrTestRule.executeStepAndWait("stepVerifyNoInitialTaps()", mFirstTabWebContents);
        // Wait to enter VR
        mVrTestRule.enterPresentationAndWait(mFirstTabCvc, mFirstTabWebContents);
        // Tap and wait for JavaScript to receive it
        mVrTestRule.sendCardboardClickAndWait(
                mActivityTestRule.getActivity(), mFirstTabWebContents);
        mVrTestRule.endTest(mFirstTabWebContents);
    }

    /**
     * Tests that non-focused tabs cannot get pose information.
     */
    @Test
    @SmallTest
    public void testPoseDataUnfocusedTab() throws InterruptedException {
        mActivityTestRule.loadUrl(
                VrTestRule.getHtmlTestFile("test_pose_data_unfocused_tab"), PAGE_LOAD_TIMEOUT_S);
        Assert.assertTrue("VRDisplay found", mVrTestRule.vrDisplayFound(mFirstTabWebContents));
        mVrTestRule.executeStepAndWait("stepCheckFrameDataWhileFocusedTab()", mFirstTabWebContents);

        mActivityTestRule.loadUrlInNewTab("about:blank");

        mVrTestRule.executeStepAndWait(
                "stepCheckFrameDataWhileNonFocusedTab()", mFirstTabWebContents);
        mVrTestRule.endTest(mFirstTabWebContents);
    }

    /**
     * Helper function to run the tests checking for the upgrade/install InfoBar being present since
     * all that differs is the value returned by VrCoreVersionChecker and a couple asserts.
     *
     * @param checkerReturnValue The value to have the VrCoreVersionChecker return
     */
    private void infoBarTestHelper(int checkerReturnValue) throws InterruptedException {
        MockVrCoreVersionCheckerImpl mockChecker = new MockVrCoreVersionCheckerImpl();
        mockChecker.setMockReturnValue(checkerReturnValue);
        VrUtils.getVrShellDelegateInstance().overrideVrCoreVersionCheckerForTesting(mockChecker);
        mActivityTestRule.loadUrl(
                VrTestRule.getHtmlTestFile("generic_webvr_page"), PAGE_LOAD_TIMEOUT_S);
        String displayFound = "VRDisplay Found";
        String barPresent = "InfoBar present";
        if (checkerReturnValue == VrCoreVersionChecker.VR_READY) {
            Assert.assertTrue(displayFound, mVrTestRule.vrDisplayFound(mFirstTabWebContents));
            Assert.assertFalse(barPresent,
                    VrUtils.isInfoBarPresent(
                            mActivityTestRule.getActivity().getWindow().getDecorView()));
        } else if (checkerReturnValue == VrCoreVersionChecker.VR_OUT_OF_DATE
                || checkerReturnValue == VrCoreVersionChecker.VR_NOT_AVAILABLE) {
            // Out of date and missing cases are the same, but with different text
            String expectedMessage, expectedButton;
            if (checkerReturnValue == VrCoreVersionChecker.VR_OUT_OF_DATE) {
                expectedMessage = mActivityTestRule.getActivity().getString(
                        R.string.vr_services_check_infobar_update_text);
                expectedButton = mActivityTestRule.getActivity().getString(
                        R.string.vr_services_check_infobar_update_button);
            } else {
                expectedMessage = mActivityTestRule.getActivity().getString(
                        R.string.vr_services_check_infobar_install_text);
                expectedButton = mActivityTestRule.getActivity().getString(
                        R.string.vr_services_check_infobar_install_button);
            }
            Assert.assertFalse(displayFound, mVrTestRule.vrDisplayFound(mFirstTabWebContents));
            Assert.assertTrue(barPresent,
                    VrUtils.isInfoBarPresent(
                            mActivityTestRule.getActivity().getWindow().getDecorView()));
            TextView tempView = (TextView) mActivityTestRule.getActivity()
                                        .getWindow()
                                        .getDecorView()
                                        .findViewById(R.id.infobar_message);
            Assert.assertEquals(expectedMessage, tempView.getText().toString());
            tempView = (TextView) mActivityTestRule.getActivity()
                               .getWindow()
                               .getDecorView()
                               .findViewById(R.id.button_primary);
            Assert.assertEquals(expectedButton, tempView.getText().toString());
        } else if (checkerReturnValue == VrCoreVersionChecker.VR_NOT_SUPPORTED) {
            Assert.assertFalse(displayFound, mVrTestRule.vrDisplayFound(mFirstTabWebContents));
            Assert.assertFalse(barPresent,
                    VrUtils.isInfoBarPresent(
                            mActivityTestRule.getActivity().getWindow().getDecorView()));
        } else {
            Assert.fail(
                    "Invalid VrCoreVersionChecker value: " + String.valueOf(checkerReturnValue));
        }
        Assert.assertEquals(checkerReturnValue, mockChecker.getLastReturnValue());
    }

    /**
     * Tests that the upgrade/install VR Services InfoBar is not present when VR Services is
     * installed and up to date.
     */
    @Test
    @MediumTest
    public void testInfoBarNotPresentWhenVrServicesCurrent() throws InterruptedException {
        infoBarTestHelper(VrCoreVersionChecker.VR_READY);
    }

    /**
     * Tests that the upgrade VR Services InfoBar is present when VR Services is outdated.
     */
    @Test
    @MediumTest
    public void testInfoBarPresentWhenVrServicesOutdated() throws InterruptedException {
        infoBarTestHelper(VrCoreVersionChecker.VR_OUT_OF_DATE);
    }

    /**
     * Tests that the install VR Services InfoBar is present when VR Services is missing.
     */
    @Test
    @MediumTest
    public void testInfoBarPresentWhenVrServicesMissing() throws InterruptedException {
        infoBarTestHelper(VrCoreVersionChecker.VR_NOT_AVAILABLE);
    }

    /**
     * Tests that the install VR Services InfoBar is not present when VR is not supported on the
     * device.
     */
    @Test
    @MediumTest
    public void testInfoBarNotPresentWhenVrServicesNotSupported() throws InterruptedException {
        infoBarTestHelper(VrCoreVersionChecker.VR_NOT_SUPPORTED);
    }

    /**
     * Tests that the reported WebVR capabilities match expectations on the devices the WebVR tests
     * are run on continuously.
     */
    @Test
    @MediumTest
    public void testDeviceCapabilitiesMatchExpectations() throws InterruptedException {
        mActivityTestRule.loadUrl(
                VrTestRule.getHtmlTestFile("test_device_capabilities_match_expectations"),
                PAGE_LOAD_TIMEOUT_S);
        Assert.assertTrue("VRDisplayFound", mVrTestRule.vrDisplayFound(mFirstTabWebContents));
        mVrTestRule.executeStepAndWait(
                "stepCheckDeviceCapabilities('" + Build.DEVICE + "')", mFirstTabWebContents);
        mVrTestRule.endTest(mFirstTabWebContents);
    }

    /**
     * Tests that focus is locked to the presenting display for purposes of VR input.
     */
    @Test
    @MediumTest
    @DisableIf.Build(message = "Flaky on L crbug.com/713781",
            sdk_is_greater_than = Build.VERSION_CODES.KITKAT,
            sdk_is_less_than = Build.VERSION_CODES.M)
    public void testPresentationLocksFocus() throws InterruptedException {
        mActivityTestRule.loadUrl(
                VrTestRule.getHtmlTestFile("test_presentation_locks_focus"), PAGE_LOAD_TIMEOUT_S);
        mVrTestRule.enterPresentationAndWait(mFirstTabCvc, mFirstTabWebContents);
        mVrTestRule.endTest(mFirstTabWebContents);
    }
}
