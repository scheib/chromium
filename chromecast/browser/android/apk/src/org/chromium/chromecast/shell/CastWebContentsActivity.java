// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chromecast.shell;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.media.AudioManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.PatternMatcher;
import android.support.v4.content.LocalBroadcastManager;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.Toast;

import org.chromium.base.Log;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.content.browser.ActivityContentVideoViewEmbedder;
import org.chromium.content.browser.ContentVideoViewEmbedder;
import org.chromium.content.browser.ContentView;
import org.chromium.content.browser.ContentViewCore;
import org.chromium.content.browser.ContentViewRenderView;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.ViewAndroidDelegate;
import org.chromium.ui.base.WindowAndroid;

/**
 * Activity for displaying a WebContents in CastShell.
 * <p>
 * Typically, this class is controlled by CastContentWindowAndroid, which will
 * start a new instance of this activity. If the CastContentWindowAndroid is
 * destroyed, CastWebContentsActivity should finish(). Similarily, if this
 * activity is destroyed, CastContentWindowAndroid should be notified by intent.
 */
@JNINamespace("chromecast::shell")
public class CastWebContentsActivity extends Activity {
    private static final String TAG = "cr_CastWebActivity";
    private static final boolean DEBUG = true;

    private Handler mHandler;
    private String mInstanceId;
    private BroadcastReceiver mWindowDestroyedBroadcastReceiver;
    private IntentFilter mWindowDestroyedIntentFilter;
    private FrameLayout mCastWebContentsLayout;
    private AudioManager mAudioManager;
    private ContentViewRenderView mContentViewRenderView;
    private WindowAndroid mWindow;
    private ContentViewCore mContentViewCore;
    private ContentView mContentView;
    private boolean mReceivedUserLeave = false;

    private static final int TEARDOWN_GRACE_PERIOD_TIMEOUT_MILLIS = 300;

    public static final String ACTION_STOP_ACTIVITY =
            "com.google.android.apps.castshell.intent.action.STOP_ACTIVITY";

    /*
     * Intended to be called from "onStop" to determine if this is a "legitimate" stop or not.
     * When starting CastShellActivity from the TV in sleep mode, an extra onPause/onStop will be
     * fired.
     * Details: http://stackoverflow.com/questions/25369909/
     * We use onUserLeaveHint to determine if the onPause/onStop called because of user intent.
     */
    private boolean isStopping() {
        return mReceivedUserLeave;
    }

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        if (DEBUG) Log.d(TAG, "onCreate");
        super.onCreate(savedInstanceState);

        mHandler = new Handler();

        if (!CastBrowserHelper.initializeBrowser(getApplicationContext())) {
            Toast.makeText(this, R.string.browser_process_initialization_failed, Toast.LENGTH_SHORT)
                    .show();
            finish();
        }

        // Whenever our app is visible, volume controls should modify the music stream.
        // For more information read:
        // http://developer.android.com/training/managing-audio/volume-playback.html
        setVolumeControlStream(AudioManager.STREAM_MUSIC);

        // Set flags to both exit sleep mode when this activity starts and
        // avoid entering sleep mode while playing media. We cannot distinguish
        // between video and audio so this applies to both.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        mAudioManager = CastAudioManager.getAudioManager(this);

        setContentView(R.layout.cast_web_contents_activity);

        mWindow = new WindowAndroid(this);
        mContentViewRenderView = new ContentViewRenderView(this) {
            @Override
            protected void onReadyToRender() {
                setOverlayVideoMode(true);
            }
        };
        mContentViewRenderView.onNativeLibraryLoaded(mWindow);
        // Setting the background color to black avoids rendering a white splash screen
        // before the players are loaded. See crbug/307113 for details.
        mContentViewRenderView.setSurfaceViewBackgroundColor(Color.BLACK);

        mCastWebContentsLayout = (FrameLayout) findViewById(R.id.web_contents_container);
        mCastWebContentsLayout.addView(mContentViewRenderView,
                new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.MATCH_PARENT));

        Intent intent = getIntent();
        handleIntent(intent);
    }

    protected void handleIntent(Intent intent) {
        intent.setExtrasClassLoader(WebContents.class.getClassLoader());
        mInstanceId = intent.getData().getPath();

        if (mWindowDestroyedBroadcastReceiver != null) {
            LocalBroadcastManager.getInstance(this).unregisterReceiver(
                    mWindowDestroyedBroadcastReceiver);
        }
        mWindowDestroyedBroadcastReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                detachWebContentsIfAny();
                maybeFinishLater();
            }
        };
        mWindowDestroyedIntentFilter = new IntentFilter();
        mWindowDestroyedIntentFilter.addDataScheme(intent.getData().getScheme());
        mWindowDestroyedIntentFilter.addDataAuthority(intent.getData().getAuthority(), null);
        mWindowDestroyedIntentFilter.addDataPath(mInstanceId, PatternMatcher.PATTERN_LITERAL);
        mWindowDestroyedIntentFilter.addAction(ACTION_STOP_ACTIVITY);
        LocalBroadcastManager.getInstance(this).registerReceiver(
                mWindowDestroyedBroadcastReceiver, mWindowDestroyedIntentFilter);

        WebContents webContents = (WebContents) intent.getParcelableExtra(
                CastWebContentsComponent.ACTION_EXTRA_WEB_CONTENTS);
        if (webContents == null) {
            Log.e(TAG, "Received null WebContents in intent.");
            maybeFinishLater();
            return;
        }

        detachWebContentsIfAny();
        showWebContents(webContents);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        if (DEBUG) Log.d(TAG, "onNewIntent");

        // If we're currently finishing this activity, we should start a new activity to
        // display the new app.
        if (isFinishing()) {
            Log.d(TAG, "Activity is finishing, starting new activity.");
            int flags = intent.getFlags();
            flags = flags & ~Intent.FLAG_ACTIVITY_SINGLE_TOP;
            intent.setFlags(flags);
            startActivity(intent);
            return;
        }

        handleIntent(intent);
    }

    @Override
    protected void onDestroy() {
        if (DEBUG) Log.d(TAG, "onDestroy");
        super.onDestroy();
    }

    @Override
    protected void onStart() {
        if (DEBUG) Log.d(TAG, "onStart");
        super.onStart();
    }

    @Override
    protected void onStop() {
        if (DEBUG) Log.d(TAG, "onStop");
        if (isStopping()) {
            detachWebContentsIfAny();
            releaseStreamMuteIfNecessary();
        }
        super.onStop();
    }

    @Override
    protected void onResume() {
        if (DEBUG) Log.d(TAG, "onResume");
        super.onResume();

        if (mAudioManager.requestAudioFocus(
                    null, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN)
                != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            Log.e(TAG, "Failed to obtain audio focus");
        }
    }

    @Override
    protected void onPause() {
        if (DEBUG) Log.d(TAG, "onPause");
        super.onPause();

        // Release the audio focus. Note that releasing audio focus does not stop audio playback,
        // it just notifies the framework that this activity has stopped playing audio.
        if (mAudioManager.abandonAudioFocus(null) != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            Log.e(TAG, "Failed to abandon audio focus");
        }
    }

    @Override
    protected void onUserLeaveHint() {
        if (DEBUG) Log.d(TAG, "onUserLeaveHint");
        mReceivedUserLeave = true;
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (DEBUG) Log.d(TAG, "dispatchKeyEvent");
        int keyCode = event.getKeyCode();
        int action = event.getAction();

        // Similar condition for all single-click events.
        if (action == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
            if (keyCode == KeyEvent.KEYCODE_DPAD_CENTER
                    || keyCode == KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE
                    || keyCode == KeyEvent.KEYCODE_MEDIA_PLAY
                    || keyCode == KeyEvent.KEYCODE_MEDIA_PAUSE
                    || keyCode == KeyEvent.KEYCODE_MEDIA_STOP
                    || keyCode == KeyEvent.KEYCODE_MEDIA_NEXT
                    || keyCode == KeyEvent.KEYCODE_MEDIA_PREVIOUS) {
                CastWebContentsComponent.onKeyDown(this, mInstanceId, keyCode);

                // Stop key should end the entire session.
                if (keyCode == KeyEvent.KEYCODE_MEDIA_STOP) {
                    mReceivedUserLeave = true;
                    finish();
                }

                return true;
            }
        }

        if (keyCode == KeyEvent.KEYCODE_BACK) {
            mReceivedUserLeave = true;
            return super.dispatchKeyEvent(event);
        }
        return false;
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent ev) {
        return false;
    }

    @Override
    public boolean dispatchKeyShortcutEvent(KeyEvent event) {
        return false;
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent ev) {
        return false;
    }

    @Override
    public boolean dispatchTrackballEvent(MotionEvent ev) {
        return false;
    }

    @SuppressWarnings("deprecation")
    private void releaseStreamMuteIfNecessary() {
        AudioManager audioManager = CastAudioManager.getAudioManager(this);
        boolean isMuted = false;
        try {
            isMuted = (Boolean) audioManager.getClass()
                    .getMethod("isStreamMute", int.class)
                    .invoke(audioManager, AudioManager.STREAM_MUSIC);
        } catch (Exception e) {
            Log.e(TAG, "Cannot call AudioManager.isStreamMute().", e);
        }

        if (isMuted) {
            // Note: this is a no-op on fixed-volume devices.
            audioManager.setStreamMute(AudioManager.STREAM_MUSIC, false);
        }
    }

    // Closes this activity if a new WebContents is not being displayed.
    private void maybeFinishLater() {
        Log.d(TAG, "maybeFinishLater");
        final String currentInstanceId = mInstanceId;
        mHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (currentInstanceId == mInstanceId) {
                    Log.d(TAG, "Finishing.");
                    finish();
                }
            }
        }, TEARDOWN_GRACE_PERIOD_TIMEOUT_MILLIS);
    }

    // Sets webContents to be the currently displayed webContents.
    private void showWebContents(WebContents webContents) {
        if (DEBUG) Log.d(TAG, "showWebContents");

        // Set ContentVideoViewEmbedder to allow video playback.
        nativeSetContentVideoViewEmbedder(webContents, new ActivityContentVideoViewEmbedder(this));

        // TODO(derekjchow): productVersion
        mContentViewCore = new ContentViewCore(this, "");
        mContentView = ContentView.createContentView(this, mContentViewCore);
        mContentViewCore.initialize(ViewAndroidDelegate.createBasicDelegate(mContentView),
                mContentView, webContents, mWindow);
        // Enable display of current webContents.
        if (getParent() != null) mContentViewCore.onShow();
        mCastWebContentsLayout.addView(mContentView,
                new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.MATCH_PARENT));
        mContentView.requestFocus();
        mContentViewRenderView.setCurrentContentViewCore(mContentViewCore);
    }

    // Remove the currently displayed webContents. no-op if nothing is being displayed.
    private void detachWebContentsIfAny() {
        if (DEBUG) Log.d(TAG, "detachWebContentsIfAny");
        if (mContentView != null) {
            mCastWebContentsLayout.removeView(mContentView);
            mContentView = null;
            mContentViewCore = null;

            CastWebContentsComponent.onComponentClosed(this, mInstanceId);
        }
    }

    private native void nativeSetContentVideoViewEmbedder(
            WebContents webContents, ContentVideoViewEmbedder embedder);
}
