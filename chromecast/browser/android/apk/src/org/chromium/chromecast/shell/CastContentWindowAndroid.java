// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chromecast.shell;

import android.content.Context;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.content_public.browser.WebContents;

/**
 * The Java component of CastContentWindowAndroid. This class is responsible for
 * starting, stopping and monitoring CastWebContentsActivity.
 * <p>
 * See chromecast/browser/cast_content_window_android.* for the native half.
 */
@JNINamespace("chromecast::shell")
public class CastContentWindowAndroid implements CastWebContentsComponent.OnComponentClosedHandler,
                                                 CastWebContentsComponent.OnKeyDownHandler {
    private static final String TAG = "cr_CastContentWindowAndroid";
    private static final boolean DEBUG = true;

    // Note: CastContentWindowAndroid may outlive the native object. The native
    // ref should be checked that it is not zero before it is used.
    private long mNativeCastContentWindowAndroid;
    private Context mContext;
    private String mInstanceId;
    private CastWebContentsComponent mComponent;

    private static int sInstanceId = 1;

    @SuppressWarnings("unused")
    @CalledByNative
    private static CastContentWindowAndroid create(long nativeCastContentWindowAndroid) {
        return new CastContentWindowAndroid(
                nativeCastContentWindowAndroid, ContextUtils.getApplicationContext());
    }

    private CastContentWindowAndroid(long nativeCastContentWindowAndroid, final Context context) {
        mNativeCastContentWindowAndroid = nativeCastContentWindowAndroid;
        mContext = context;
        mInstanceId = Integer.toString(sInstanceId++);

        mComponent = new CastWebContentsComponent(mInstanceId, this, this);
    }

    @SuppressWarnings("unused")
    @CalledByNative
    private void showWebContents(WebContents webContents) {
        if (DEBUG) Log.d(TAG, "showWebContents");

        mComponent.start(mContext, webContents);
    }

    @SuppressWarnings("unused")
    @CalledByNative
    private void onNativeDestroyed() {
        assert mNativeCastContentWindowAndroid != 0;
        mNativeCastContentWindowAndroid = 0;

        // Note: there is a potential race condition when this function is called after
        // showWebContents. If the stop intent is received after the start intent but before
        // onCreate, the activity won't shutdown.
        // TODO(derekjchow): Add a unittest to check this behaviour. Also consider using
        // Instrumentation.startActivitySync to guarentee onCreate is run.

        if (DEBUG) Log.d(TAG, "onNativeDestroyed");
        mComponent.stop(mContext);
    }

    @Override
    public void onKeyDown(int keyCode) {
        if (DEBUG) Log.d(TAG, "onKeyDown");
        if (mNativeCastContentWindowAndroid != 0) {
            nativeOnKeyDown(mNativeCastContentWindowAndroid, keyCode);
        }
    }

    @Override
    public void onComponentClosed() {
        if (DEBUG) Log.d(TAG, "onComponentClosed");
        if (mNativeCastContentWindowAndroid != 0) {
            nativeOnActivityStopped(mNativeCastContentWindowAndroid);
        }
    }

    private native void nativeOnActivityStopped(long nativeCastContentWindowAndroid);

    private native void nativeOnKeyDown(long nativeCastContentWindowAndroid, int keyCode);
}
