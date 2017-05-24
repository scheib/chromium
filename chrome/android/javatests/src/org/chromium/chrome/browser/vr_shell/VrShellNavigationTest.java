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

import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Restriction;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.test.ChromeActivityTestCaseBase;
import org.chromium.chrome.test.ChromeActivityTestRule;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.ChromeTabbedActivityTestRule;
import org.chromium.chrome.test.util.ChromeTabUtils;
import org.chromium.content.browser.ContentViewCore;
import org.chromium.content.browser.test.util.DOMUtils;
import org.chromium.content_public.browser.WebContents;

import java.util.concurrent.TimeoutException;

/**
 * End-to-end tests for testing navigation transitions (e.g. link clicking) in VR Browser mode, aka
 * "VR Shell".
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE,
        ChromeActivityTestRule.DISABLE_NETWORK_PREDICTION_FLAG, "enable-features=VrShell"})
@Restriction(RESTRICTION_TYPE_VIEWER_DAYDREAM)
public class VrShellNavigationTest {
    @Rule
    public ChromeTabbedActivityTestRule mActivityTestRule = new ChromeTabbedActivityTestRule();
    @Rule
    public VrTestRule mVrTestRule = new VrTestRule();

    private static final String TEST_PAGE_2D_URL =
            VrTestRule.getHtmlTestFile("test_navigation_2d_page");
    private static final String TEST_PAGE_WEBVR_URL =
            VrTestRule.getHtmlTestFile("test_navigation_webvr_page");

    private ContentViewCore mFirstTabCvc;
    private WebContents mFirstTabWebContents;

    private enum Page { PAGE_2D, PAGE_WEBVR }
    private enum PresentationMode { NON_PRESENTING, PRESENTING }
    private enum FullscreenMode { NON_FULLSCREENED, FULLSCREENED }

    @Before
    public void setUp() throws Exception {
        mActivityTestRule.startMainActivityOnBlankPage();
        mFirstTabWebContents = mActivityTestRule.getActivity().getActivityTab().getWebContents();
        mFirstTabCvc = mActivityTestRule.getActivity().getActivityTab().getContentViewCore();
        VrUtils.forceEnterVr();
        VrUtils.waitForVrSupported(POLL_TIMEOUT_LONG_MS);
    }

    private String getUrl(Page page) {
        switch (page) {
            case PAGE_2D:
                return TEST_PAGE_2D_URL + "?id=0";
            case PAGE_WEBVR:
                return TEST_PAGE_WEBVR_URL + "?id=0";
            default:
                throw new UnsupportedOperationException("Don't know page type " + page);
        }
    }

    /**
     * Triggers navigation to either a 2D or WebVR page. Similar to
     * {@link ChromeActivityTestCaseBase#loadUrl loadUrl} but makes sure page initiates the
     * navigation. This is desirable since we are testing navigation transitions end-to-end.
     */
    private void navigateTo(final Page to) throws InterruptedException {
        ChromeTabUtils.waitForTabPageLoaded(
                mActivityTestRule.getActivity().getActivityTab(), new Runnable() {
                    @Override
                    public void run() {
                        mVrTestRule.runJavaScriptOrFail(
                                "window.location.href = '" + getUrl(to) + "';",
                                POLL_TIMEOUT_SHORT_MS, mFirstTabWebContents);
                    }
                }, POLL_TIMEOUT_LONG_MS);
    }

    private void enterFullscreenOrFail(ContentViewCore cvc)
            throws InterruptedException, TimeoutException {
        DOMUtils.clickNode(cvc, "fullscreen");
        mVrTestRule.waitOnJavaScriptStep(cvc.getWebContents());
        Assert.assertTrue(DOMUtils.isFullscreen(cvc.getWebContents()));
    }

    private void enterPresentationOrFail(ContentViewCore cvc)
            throws InterruptedException, TimeoutException {
        mVrTestRule.enterPresentationAndWait(cvc, mFirstTabWebContents);
        Assert.assertTrue(VrShellDelegate.getVrShellForTesting().getWebVrModeEnabled());
    }

    private void assertState(WebContents wc, Page page, PresentationMode presentationMode,
            FullscreenMode fullscreenMode) throws InterruptedException, TimeoutException {
        Assert.assertTrue("Browser is in VR", VrShellDelegate.isInVr());
        Assert.assertEquals("Browser is on correct web site", getUrl(page), wc.getVisibleUrl());
        Assert.assertEquals("Browser is in VR Presentation Mode",
                presentationMode == PresentationMode.PRESENTING,
                VrShellDelegate.getVrShellForTesting().getWebVrModeEnabled());
        Assert.assertEquals("Browser is in fullscreen",
                fullscreenMode == FullscreenMode.FULLSCREENED, DOMUtils.isFullscreen(wc));
        // Feedback infobar should never show up during navigations.
        Assert.assertFalse(VrUtils.isInfoBarPresent(
                mActivityTestRule.getActivity().getWindow().getDecorView()));
    }

    public int loadUrl(String url, long secondsToWait)
            throws IllegalArgumentException, InterruptedException {
        int result = mActivityTestRule.loadUrl(url, secondsToWait);
        mVrTestRule.waitOnJavaScriptStep(
                mActivityTestRule.getActivity().getActivityTab().getWebContents());
        return result;
    }

    /**
     * Tests navigation from a 2D to a 2D page.
     */
    @Test
    @MediumTest
    public void test2dTo2d() throws InterruptedException, TimeoutException {
        loadUrl(TEST_PAGE_2D_URL, PAGE_LOAD_TIMEOUT_S);

        navigateTo(Page.PAGE_2D);

        assertState(mFirstTabWebContents, Page.PAGE_2D, PresentationMode.NON_PRESENTING,
                FullscreenMode.NON_FULLSCREENED);
    }

    /**
     * Tests navigation from a 2D to a WebVR page.
     */
    @Test
    @CommandLineFlags.Add("enable-webvr")
    @MediumTest
    public void test2dToWebVr()
            throws IllegalArgumentException, InterruptedException, TimeoutException {
        loadUrl(TEST_PAGE_2D_URL, PAGE_LOAD_TIMEOUT_S);

        navigateTo(Page.PAGE_WEBVR);

        assertState(mFirstTabWebContents, Page.PAGE_WEBVR, PresentationMode.NON_PRESENTING,
                FullscreenMode.NON_FULLSCREENED);
    }

    /**
     * Tests navigation from a fullscreened 2D to a WebVR page.
     */
    @Test
    @CommandLineFlags.Add("enable-webvr")
    @MediumTest
    public void test2dFullscreenToWebVr()
            throws IllegalArgumentException, InterruptedException, TimeoutException {
        loadUrl(TEST_PAGE_2D_URL, PAGE_LOAD_TIMEOUT_S);
        enterFullscreenOrFail(mFirstTabCvc);

        navigateTo(Page.PAGE_WEBVR);

        assertState(mFirstTabWebContents, Page.PAGE_WEBVR, PresentationMode.NON_PRESENTING,
                FullscreenMode.NON_FULLSCREENED);
    }

    /**
     * Tests navigation from a WebVR to a 2D page.
     */
    @Test
    @CommandLineFlags.Add("enable-webvr")
    @MediumTest
    public void testWebVrTo2d()
            throws IllegalArgumentException, InterruptedException, TimeoutException {
        loadUrl(TEST_PAGE_WEBVR_URL, PAGE_LOAD_TIMEOUT_S);

        navigateTo(Page.PAGE_2D);

        assertState(mFirstTabWebContents, Page.PAGE_2D, PresentationMode.NON_PRESENTING,
                FullscreenMode.NON_FULLSCREENED);
    }

    /**
     * Tests navigation from a WebVR to a WebVR page.
     */
    @Test
    @CommandLineFlags.Add("enable-webvr")
    @MediumTest
    public void testWebVrToWebVr()
            throws IllegalArgumentException, InterruptedException, TimeoutException {
        loadUrl(TEST_PAGE_WEBVR_URL, PAGE_LOAD_TIMEOUT_S);

        navigateTo(Page.PAGE_WEBVR);

        assertState(mFirstTabWebContents, Page.PAGE_WEBVR, PresentationMode.NON_PRESENTING,
                FullscreenMode.NON_FULLSCREENED);
    }

    /**
     * Tests navigation from a presenting WebVR to a 2D page.
     */
    @Test
    @CommandLineFlags.Add("enable-webvr")
    @MediumTest
    public void testWebVrPresentingTo2d()
            throws IllegalArgumentException, InterruptedException, TimeoutException {
        loadUrl(TEST_PAGE_WEBVR_URL, PAGE_LOAD_TIMEOUT_S);
        enterPresentationOrFail(mFirstTabCvc);

        navigateTo(Page.PAGE_2D);

        assertState(mFirstTabWebContents, Page.PAGE_2D, PresentationMode.NON_PRESENTING,
                FullscreenMode.NON_FULLSCREENED);
    }

    /**
     * Tests navigation from a presenting WebVR to a WebVR page.
     */
    @Test
    @CommandLineFlags.Add("enable-webvr")
    @MediumTest
    public void testWebVrPresentingToWebVr()
            throws IllegalArgumentException, InterruptedException, TimeoutException {
        loadUrl(TEST_PAGE_WEBVR_URL, PAGE_LOAD_TIMEOUT_S);
        enterPresentationOrFail(mFirstTabCvc);

        navigateTo(Page.PAGE_WEBVR);

        assertState(mFirstTabWebContents, Page.PAGE_WEBVR, PresentationMode.NON_PRESENTING,
                FullscreenMode.NON_FULLSCREENED);
    }

    /**
     * Tests navigation from a fullscreened WebVR to a 2D page.
     */
    @Test
    @CommandLineFlags.Add("enable-webvr")
    @MediumTest
    public void testWebVrFullscreenTo2d()
            throws IllegalArgumentException, InterruptedException, TimeoutException {
        loadUrl(TEST_PAGE_WEBVR_URL, PAGE_LOAD_TIMEOUT_S);
        enterFullscreenOrFail(mFirstTabCvc);

        navigateTo(Page.PAGE_2D);

        assertState(mFirstTabWebContents, Page.PAGE_2D, PresentationMode.NON_PRESENTING,
                FullscreenMode.NON_FULLSCREENED);
    }

    /**
     * Tests navigation from a fullscreened WebVR to a WebVR page.
     */
    @Test
    @CommandLineFlags.Add("enable-webvr")
    @MediumTest
    public void testWebVrFullscreenToWebVr()
            throws IllegalArgumentException, InterruptedException, TimeoutException {
        loadUrl(TEST_PAGE_WEBVR_URL, PAGE_LOAD_TIMEOUT_S);
        enterFullscreenOrFail(mFirstTabCvc);

        navigateTo(Page.PAGE_WEBVR);

        assertState(mFirstTabWebContents, Page.PAGE_WEBVR, PresentationMode.NON_PRESENTING,
                FullscreenMode.NON_FULLSCREENED);
    }
}
