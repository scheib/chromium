// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chromecast.shell;

import android.app.Service;
import android.content.Intent;
import android.media.AudioManager;
import android.os.IBinder;
import android.widget.Toast;

import org.chromium.base.Log;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.content.browser.ContentView;
import org.chromium.content.browser.ContentViewCore;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.ViewAndroidDelegate;
import org.chromium.ui.base.WindowAndroid;

/**
 * Service for "displaying" a WebContents in CastShell.
 * <p>
 * Typically, this class is controlled by CastContentWindowAndroid, which will
 * bind to this service.
 */
@JNINamespace("chromecast::shell")
public class CastWebContentsService extends Service {
    private static final String TAG = "cr_CastWebService";
    private static final boolean DEBUG = true;

    private String mInstanceId;
    private AudioManager mAudioManager;
    private WindowAndroid mWindow;
    private ContentViewCore mContentViewCore;
    private ContentView mContentView;

    protected void handleIntent(Intent intent) {
        intent.setExtrasClassLoader(WebContents.class.getClassLoader());
        mInstanceId = intent.getData().getPath();

        WebContents webContents = (WebContents) intent.getParcelableExtra(
                CastWebContentsComponent.ACTION_EXTRA_WEB_CONTENTS);
        if (webContents == null) {
            Log.e(TAG, "Received null WebContents in intent.");
            return;
        }

        detachWebContentsIfAny();
        showWebContents(webContents);
    }

    @Override
    public void onDestroy() {
        if (DEBUG) Log.d(TAG, "onDestroy");

        if (mAudioManager.abandonAudioFocus(null) != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            Log.e(TAG, "Failed to abandon audio focus");
        }

        super.onDestroy();
    }

    @Override
    public void onCreate() {
        if (DEBUG) Log.d(TAG, "onCreate");

        if (!CastBrowserHelper.initializeBrowser(getApplicationContext())) {
            Toast.makeText(this, R.string.browser_process_initialization_failed, Toast.LENGTH_SHORT)
                    .show();
            stopSelf();
        }

        mWindow = new WindowAndroid(this);
        mAudioManager = CastAudioManager.getAudioManager(this);

        if (mAudioManager.requestAudioFocus(
                    null, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN)
                != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            Log.e(TAG, "Failed to obtain audio focus");
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        if (DEBUG) Log.d(TAG, "onBind");

        handleIntent(intent);

        return null;
    }

    // Sets webContents to be the currently displayed webContents.
    private void showWebContents(WebContents webContents) {
        if (DEBUG) Log.d(TAG, "showWebContents");

        // TODO(derekjchow): productVersion
        mContentViewCore = new ContentViewCore(this, "");
        mContentView = ContentView.createContentView(this, mContentViewCore);
        mContentViewCore.initialize(ViewAndroidDelegate.createBasicDelegate(mContentView),
                mContentView, webContents, mWindow);
        // Enable display of current webContents.
        mContentViewCore.onShow();
    }

    // Remove the currently displayed webContents. no-op if nothing is being displayed.
    private void detachWebContentsIfAny() {
        if (DEBUG) Log.d(TAG, "detachWebContentsIfAny");
        if (mContentView != null) {
            mContentView = null;
            mContentViewCore = null;

            // Inform CastContentWindowAndroid we're detaching.
            CastWebContentsComponent.onComponentClosed(this, mInstanceId);
        }
    }
}
