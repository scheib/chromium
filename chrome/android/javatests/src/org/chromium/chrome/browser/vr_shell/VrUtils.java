// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.vr_shell;

import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.nfc.NdefMessage;
import android.nfc.NdefRecord;
import android.nfc.NfcAdapter;
import android.view.View;
import android.view.ViewGroup;

import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.infobar.InfoBarLayout;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.Callable;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Class containing static functions and constants that are useful for VR
 * instrumentation testing.
 */
public class VrUtils {
    public static final int POLL_CHECK_INTERVAL_SHORT_MS = 50;
    public static final int POLL_CHECK_INTERVAL_LONG_MS = 100;
    public static final int POLL_TIMEOUT_SHORT_MS = 1000;
    public static final int POLL_TIMEOUT_LONG_MS = 10000;

    private static final String DETECTION_ACTIVITY =
            ".nfc.ViewerDetectionActivity";
    // TODO(bsheedy): Use constants from VrCore if ever exposed
    private static final String APPLICATION_RECORD_STRING = "com.google.vr.vrcore";
    private static final String RESERVED_KEY = "google.vr/rsvd";
    private static final String VERSION_KEY =  "google.vr/ver";
    private static final String DATA_KEY =     "google.vr/data";
    private static final ByteOrder BYTE_ORDER = ByteOrder.BIG_ENDIAN;
    // Hard coded viewer ID (0x03) instead of using NfcParams and converting
    // to a byte array because NfcParams were removed from the GVR SDK
    private static final byte[] PROTO_BYTES = new byte[] {(byte) 0x08, (byte) 0x03};
    private static final int VERSION = 123;
    private static final int RESERVED = 456;

    /**
     * Gets the VrShellDelegate instance on the UI thread, as otherwise the
     * Choreographer obtained in VrShellDelegate's constructor is for the instrumentation
     * thread instead of the UI thread.
     * @return The browser's current VrShellDelegate instance
     */
    public static VrShellDelegate getVrShellDelegateInstance() {
        final AtomicReference<VrShellDelegate> delegate = new AtomicReference<VrShellDelegate>();
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                delegate.set(VrShellDelegate.getInstanceForTesting());
            }
        });
        return delegate.get();
    }

    /**
     * Forces the browser into VR mode via a VrShellDelegate call.
     */
    public static void forceEnterVr() {
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                VrShellDelegate.enterVrIfNecessary();
            }
        });
    }

    /**
     * Forces the browser out of VR mode via a VrShellDelegate call.
     * @param vrDelegate The VRShellDelegate associated with this activity.
     */
    public static void forceExitVr(final VrShellDelegate vrDelegate) {
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                vrDelegate.shutdownVr(true /* disableVrMode */, false /* canReenter */,
                        true /* stayingInChrome */);
            }
        });
    }

    private static byte[] intToByteArray(int i) {
        final ByteBuffer bb = ByteBuffer.allocate(Integer.SIZE / Byte.SIZE);
        bb.order(BYTE_ORDER);
        bb.putInt(i);
        return bb.array();
    }

    /**
     * Makes an Intent that triggers VrCore as if the Daydream headset's NFC
     * tag was scanned.
     * @return The intent to send to VrCore to simulate an NFC scan.
     */
    public static Intent makeNfcIntent() {
        Intent nfcIntent = new Intent(NfcAdapter.ACTION_NDEF_DISCOVERED);
        NdefMessage[] messages = new NdefMessage[] {new NdefMessage(
                new NdefRecord[] {NdefRecord.createMime(RESERVED_KEY, intToByteArray(RESERVED)),
                        NdefRecord.createMime(VERSION_KEY, intToByteArray(VERSION)),
                        NdefRecord.createMime(DATA_KEY, PROTO_BYTES),
                        NdefRecord.createApplicationRecord(APPLICATION_RECORD_STRING)})};
        nfcIntent.putExtra(NfcAdapter.EXTRA_NDEF_MESSAGES, messages);
        nfcIntent.setComponent(new ComponentName(APPLICATION_RECORD_STRING,
                  APPLICATION_RECORD_STRING + ".nfc.ViewerDetectionActivity"));
        return nfcIntent;
    }

    /**
     * Simulates the NFC tag of the Daydream headset being scanned.
     * @param context The Context that the activity will be started from.
     */
    public static void simNfcScan(Context context) {
        Intent nfcIntent = makeNfcIntent();
        try {
            context.startActivity(nfcIntent);
        } catch (ActivityNotFoundException e) {
            // On unsupported devices, won't find VrCore -> Do nothing
        }
    }

    /**
     * Waits until the given VrShellDelegate's isInVR() returns true. Should
     * only be used when VR Shell support is expected.
     * @param timeout How long to wait before giving up, in milliseconds
     */
    public static void waitForVrSupported(final int timeout) {
        // If VR Shell is supported, mInVr should eventually go to true
        // Relatively long timeout because sometimes GVR takes a while to enter VR
        CriteriaHelper.pollUiThread(Criteria.equals(true, new Callable<Boolean>() {
            @Override
            public Boolean call() {
                return VrShellDelegate.isInVr();
            }
        }), timeout, POLL_CHECK_INTERVAL_SHORT_MS);
    }

    /**
     * Determines is there is any InfoBar present in the given View hierarchy.
     * @param parentView The View to start the search in
     * @return Whether the InfoBar is present
     */
    public static boolean isInfoBarPresent(View parentView) {
        // TODO(ymalik): This will return true if any infobar is present. Is it
        // possible to determine the type of infobar present (e.g. Feedback)?
        // InfoBarContainer will be present regardless of whether an InfoBar
        // is actually there, but InfoBarLayout is only present if one is
        // currently showing.
        if (parentView instanceof InfoBarLayout) {
            return true;
        } else if (parentView instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) parentView;
            for (int i = 0; i < group.getChildCount(); i++) {
                if (isInfoBarPresent(group.getChildAt(i))) return true;
            }
        }
        return false;
    }
}
