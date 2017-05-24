// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.vr_shell;

import org.chromium.base.ContextUtils;

/**
 * Gets and sets preferences related to the status of the Vr feedback infobar.
 */
public class VrFeedbackStatus {
    public static final String VR_FEEDBACK_OPT_OUT = "VR_FEEDBACK_OPT_OUT";
    public static final String VR_EXIT_TO_2D_COUNT = "VR_EXIT_TO_2D_COUNT";

    /**
     * Sets the "opted out of entering VR feedback" preference.
     * @param optOut Whether the VR feedback option has been opted-out of.
     */
    public static void setFeedbackOptOut(boolean optOut) {
        ContextUtils.getAppSharedPreferences()
                .edit()
                .putBoolean(VR_FEEDBACK_OPT_OUT, optOut)
                .apply();
    }

    /**
     * Returns whether the user opted out of entering feedback for their VR experience.
     */
    public static boolean getFeedbackOptOut() {
        return ContextUtils.getAppSharedPreferences().getBoolean(VR_FEEDBACK_OPT_OUT, false);
    }

    /**
     * Sets the "exited VR mode into 2D browsing" preference.
     * @param count The number of times the user exited VR and entered 2D browsing mode
     */
    public static void setUserExitedAndEntered2DCount(int count) {
        ContextUtils.getAppSharedPreferences().edit().putInt(VR_EXIT_TO_2D_COUNT, count).apply();
    }

    /**
     * Returns the number of times the user has exited VR mode and entered the 2D browsing
     * mode.
     */
    public static int getUserExitedAndEntered2DCount() {
        return ContextUtils.getAppSharedPreferences().getInt(VR_EXIT_TO_2D_COUNT, 0);
    }
}