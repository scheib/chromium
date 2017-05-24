// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.vr_shell;

import static org.chromium.chrome.browser.vr_shell.VrTestRule.PAGE_LOAD_TIMEOUT_S;
import static org.chromium.chrome.browser.vr_shell.VrUtils.POLL_TIMEOUT_LONG_MS;
import static org.chromium.chrome.browser.vr_shell.VrUtils.POLL_TIMEOUT_SHORT_MS;
import static org.chromium.chrome.test.util.ChromeRestriction.RESTRICTION_TYPE_VIEWER_DAYDREAM;

import android.support.test.filters.MediumTest;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Restriction;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.infobar.InfoBar;
import org.chromium.chrome.test.ChromeActivityTestRule;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.ChromeTabbedActivityTestRule;
import org.chromium.chrome.test.util.ChromeTabUtils;
import org.chromium.chrome.test.util.InfoBarUtil;
import org.chromium.content.browser.ContentViewCore;
import org.chromium.content_public.browser.WebContents;

import java.util.List;
import java.util.concurrent.TimeoutException;

/**
 * Tests for the infobar that prompts the user to enter feedback on their VR browsing experience.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE,
        ChromeActivityTestRule.DISABLE_NETWORK_PREDICTION_FLAG, "enable-features=VrShell",
        "enable-webvr"})
@Restriction(RESTRICTION_TYPE_VIEWER_DAYDREAM)

public class VrFeedbackInfoBarTest {
    @Rule
    public ChromeTabbedActivityTestRule mActivityTestRule = new ChromeTabbedActivityTestRule();
    @Rule
    public VrTestRule mVrTestRule = new VrTestRule();

    private static final String TEST_PAGE_2D_URL =
            VrTestRule.getHtmlTestFile("test_navigation_2d_page");
    private static final String TEST_PAGE_WEBVR_URL =
            VrTestRule.getHtmlTestFile("test_requestPresent_enters_vr");

    private ContentViewCore mFirstTabCvc;
    private WebContents mFirstTabWebContents;

    public int loadUrlAndWait(String url, long secondsToWait)
            throws IllegalArgumentException, InterruptedException {
        int result = mActivityTestRule.loadUrl(url, secondsToWait);
        mVrTestRule.waitOnJavaScriptStep(
                mActivityTestRule.getActivity().getActivityTab().getWebContents());
        return result;
    }

    @Before
    public void setUp() throws Exception {
        mActivityTestRule.startMainActivityOnBlankPage();
        mFirstTabWebContents = mActivityTestRule.getActivity().getActivityTab().getWebContents();
        mFirstTabCvc = mActivityTestRule.getActivity().getActivityTab().getContentViewCore();
        Assert.assertFalse(VrFeedbackStatus.getFeedbackOptOut());
    }

    private void clickInfoBarButton(final boolean primary) {
        final List<InfoBar> infoBars = mActivityTestRule.getInfoBars();
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                if (primary) {
                    InfoBarUtil.clickPrimaryButton(infoBars.get(0));
                } else {
                    InfoBarUtil.clickSecondaryButton(infoBars.get(0));
                }
            }
        });
        InfoBarUtil.waitUntilNoInfoBarsExist(mActivityTestRule.getInfoBars());
    }

    private void clickInfobarCloseButton() {
        final List<InfoBar> infoBars = mActivityTestRule.getInfoBars();
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                InfoBarUtil.clickCloseButton(infoBars.get(0));
            }
        });
        InfoBarUtil.waitUntilNoInfoBarsExist(mActivityTestRule.getInfoBars());
    }

    private void assertState(boolean isInVr, boolean isInfobarVisible) {
        Assert.assertEquals("Browser is in VR", isInVr, VrShellDelegate.isInVr());
        Assert.assertEquals("Infobar is visible", isInfobarVisible,
                VrUtils.isInfoBarPresent(
                        mActivityTestRule.getActivity().getWindow().getDecorView()));
    }

    private void enterThenExitVr() {
        VrUtils.forceEnterVr();
        VrUtils.waitForVrSupported(POLL_TIMEOUT_LONG_MS);
        assertState(true /* isInVr */, false /* isInfobarVisible  */);
        VrUtils.forceExitVr(VrUtils.getVrShellDelegateInstance());
    }

    /**
     * Tests that we respect the feedback frequency when showing the feedback prompt.
     */
    @Test
    @MediumTest
    public void testFeedbackFrequency() throws InterruptedException, TimeoutException {
        loadUrlAndWait(TEST_PAGE_2D_URL, PAGE_LOAD_TIMEOUT_S);

        // Set frequency of infobar to every 2nd time.
        VrUtils.getVrShellDelegateInstance().setFeedbackFrequencyForTesting(2);

        // Verify that the Feedback infobar is visible when exiting VR.
        enterThenExitVr();
        assertState(false /* isInVr */, true /* isInfobarVisible  */);
        clickInfobarCloseButton();

        // Feedback infobar shouldn't show up this time.
        enterThenExitVr();
        assertState(false /* isInVr */, false /* isInfobarVisible  */);

        // Feedback infobar should show up again.
        enterThenExitVr();
        assertState(false /* isInVr */, true /* isInfobarVisible  */);
        clickInfobarCloseButton();
    }

    /**
     * Tests that we don't show the feedback prompt when the user has opted-out.
     */
    @Test
    @MediumTest
    public void testFeedbackOptOut() throws InterruptedException, TimeoutException {
        loadUrlAndWait(TEST_PAGE_2D_URL, PAGE_LOAD_TIMEOUT_S);

        // Show infobar every time.
        VrUtils.getVrShellDelegateInstance().setFeedbackFrequencyForTesting(1);

        // The infobar should show the first time.
        enterThenExitVr();
        assertState(false /* isInVr */, true /* isInfobarVisible  */);

        // Opt-out of seeing the infobar.
        clickInfoBarButton(false /* primary */);
        Assert.assertTrue(VrFeedbackStatus.getFeedbackOptOut());

        // The infobar should not show because the user opted out.
        enterThenExitVr();
        assertState(false /* isInVr */, false /* isInfobarVisible  */);
    }

    /**
     * Tests that we only show the feedback prompt when the user has actually used the VR browser.
     */
    @Test
    @MediumTest
    public void testFeedbackOnlyOnVrBrowsing() throws InterruptedException, TimeoutException {
        // Enter VR presentation mode.
        mActivityTestRule.loadUrl(TEST_PAGE_WEBVR_URL, PAGE_LOAD_TIMEOUT_S);
        Assert.assertTrue("VRDisplay found", mVrTestRule.vrDisplayFound(mFirstTabWebContents));
        mVrTestRule.enterPresentationAndWait(mFirstTabCvc, mFirstTabWebContents);
        assertState(true /* isInVr */, false /* isInfobarVisible  */);
        Assert.assertTrue(VrShellDelegate.getVrShellForTesting().getWebVrModeEnabled());

        // Exiting VR should not prompt for feedback since the no VR browsing was performed.
        VrUtils.forceExitVr(VrUtils.getVrShellDelegateInstance());
        assertState(false /* isInVr */, false /* isInfobarVisible  */);
    }

    /**
     * Tests that we show the prompt if the VR browser is used after exiting presentation mode.
     */
    @Test
    @MediumTest
    public void testExitPresentationInVr() throws InterruptedException, TimeoutException {
        // Enter VR presentation mode.
        mActivityTestRule.loadUrl(TEST_PAGE_WEBVR_URL, PAGE_LOAD_TIMEOUT_S);
        Assert.assertTrue("VRDisplay found", mVrTestRule.vrDisplayFound(mFirstTabWebContents));
        mVrTestRule.enterPresentationAndWait(mFirstTabCvc, mFirstTabWebContents);
        assertState(true /* isInVr */, false /* isInfobarVisible  */);
        Assert.assertTrue(VrShellDelegate.getVrShellForTesting().getWebVrModeEnabled());

        // Exit presentation mode by navigating to a different url.
        ChromeTabUtils.waitForTabPageLoaded(
                mActivityTestRule.getActivity().getActivityTab(), new Runnable() {
                    @Override
                    public void run() {
                        mVrTestRule.runJavaScriptOrFail(
                                "window.location.href = '" + TEST_PAGE_2D_URL + "';",
                                POLL_TIMEOUT_SHORT_MS, mFirstTabWebContents);
                    }
                }, POLL_TIMEOUT_LONG_MS);

        // Exiting VR should prompt for feedback since 2D browsing was performed after.
        VrUtils.forceExitVr(VrUtils.getVrShellDelegateInstance());
        assertState(false /* isInVr */, true /* isInfobarVisible  */);
    }
}
