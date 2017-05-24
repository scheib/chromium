// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chromecast.shell;

import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.net.Uri;
import android.os.IBinder;
import android.os.PatternMatcher;
import android.support.v4.content.LocalBroadcastManager;

import org.chromium.base.Log;
import org.chromium.content_public.browser.WebContents;

/**
 * A layer of indirection between CastContentWindowAndroid and CastWebContents(Activity|Service).
 * <p>
 * On builds with DISPLAY_WEB_CONTENTS_IN_SERVICE set to false, it will use CastWebContentsActivity,
 * otherwise, it will use CastWebContentsService.
 */
public class CastWebContentsComponent {
    /**
     * Callback interface for when the associated component is closed or the
     * WebContents is detached.
     */
    public interface OnComponentClosedHandler { void onComponentClosed(); }

    /**
     * Callback interface for passing along keyDown events. This only applies
     * to CastWebContentsActivity, really.
     */
    public interface OnKeyDownHandler { void onKeyDown(int keyCode); }

    private interface Delegate {
        void start(Context context, WebContents webContents);
        void stop(Context context);
    }

    private class ActivityDelegate implements Delegate {
        private static final String TAG = "cr_CastWebComponent_AD";

        @Override
        public void start(Context context, WebContents webContents) {
            if (DEBUG) Log.d(TAG, "start");

            Intent intent = new Intent(Intent.ACTION_VIEW, getInstanceUri(mInstanceId), context,
                    CastWebContentsActivity.class);
            intent.putExtra(ACTION_EXTRA_WEB_CONTENTS, webContents);
            // FLAG_ACTIVITY_SINGLE_TOP will try to reuse existing activity.
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_MULTIPLE_TASK
                    | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            context.startActivity(intent);
        }

        @Override
        public void stop(Context context) {
            if (DEBUG) Log.d(TAG, "stop");

            Intent intent = new Intent(
                    CastWebContentsActivity.ACTION_STOP_ACTIVITY, getInstanceUri(mInstanceId));
            LocalBroadcastManager.getInstance(context).sendBroadcastSync(intent);
        }
    }

    private class ServiceDelegate implements Delegate {
        private static final String TAG = "cr_CastWebComponent_SD";

        private ServiceConnection mConnection = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder service) {}

            @Override
            public void onServiceDisconnected(ComponentName name) {
                if (DEBUG) Log.d(TAG, "onServiceDisconnected");

                if (mComponentClosedHandler != null) mComponentClosedHandler.onComponentClosed();
            }
        };

        @Override
        public void start(Context context, WebContents webContents) {
            if (DEBUG) Log.d(TAG, "start");

            Intent intent = new Intent(Intent.ACTION_VIEW, getInstanceUri(mInstanceId), context,
                    CastWebContentsService.class);
            intent.putExtra(ACTION_EXTRA_WEB_CONTENTS, webContents);
            context.bindService(intent, mConnection, Context.BIND_AUTO_CREATE);
        }

        @Override
        public void stop(Context context) {
            if (DEBUG) Log.d(TAG, "stop");

            context.unbindService(mConnection);
        }
    }

    private class Receiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent.getAction().equals(ACTION_ACTIVITY_STOPPED)) {
                if (DEBUG) Log.d(TAG, "onReceive ACTION_ACTIVITY_STOPPED");

                if (mComponentClosedHandler != null) mComponentClosedHandler.onComponentClosed();
            } else if (intent.getAction().equals(ACTION_KEY_EVENT)) {
                if (DEBUG) Log.d(TAG, "onReceive ACTION_KEY_EVENT");

                int keyCode = intent.getIntExtra(ACTION_EXTRA_KEY_CODE, 0);
                if (mKeyDownHandler != null) mKeyDownHandler.onKeyDown(keyCode);
            }
        }
    }

    static final String ACTION_DATA_SCHEME = "cast";
    static final String ACTION_DATA_AUTHORITY = "webcontents";
    static final String ACTION_EXTRA_WEB_CONTENTS =
            "com.google.android.apps.castshell.intent.extra.WEB_CONTENTS";

    private static final String TAG = "cr_CastWebComponent";
    private static final boolean DEBUG = false;

    private static final String ACTION_EXTRA_KEY_CODE =
            "com.google.android.apps.castshell.intent.extra.KEY_CODE";
    private static final String ACTION_KEY_EVENT =
            "com.google.android.apps.castshell.intent.action.KEY_EVENT";
    private static final String ACTION_ACTIVITY_STOPPED =
            "com.google.android.apps.castshell.intent.action.ACTIVITY_STOPPED";

    private Delegate mDelegate;
    private OnComponentClosedHandler mComponentClosedHandler;
    private OnKeyDownHandler mKeyDownHandler;
    private Receiver mReceiver;
    private String mInstanceId;

    public CastWebContentsComponent(String instanceId,
            OnComponentClosedHandler onComponentClosedHandler, OnKeyDownHandler onKeyDownHandler) {
        mComponentClosedHandler = onComponentClosedHandler;
        mKeyDownHandler = onKeyDownHandler;
        mInstanceId = instanceId;
        if (BuildConfig.DISPLAY_WEB_CONTENTS_IN_SERVICE) {
            mDelegate = new ServiceDelegate();
        } else {
            mDelegate = new ActivityDelegate();
        }
    }

    public void start(Context context, WebContents webContents) {
        if (DEBUG) Log.d(TAG, "start");

        Uri instanceUri = getInstanceUri(mInstanceId);
        mReceiver = new Receiver();
        IntentFilter filter = new IntentFilter();
        filter.addDataScheme(instanceUri.getScheme());
        filter.addDataAuthority(instanceUri.getAuthority(), null);
        filter.addDataPath(instanceUri.getPath(), PatternMatcher.PATTERN_LITERAL);
        filter.addAction(ACTION_ACTIVITY_STOPPED);
        filter.addAction(ACTION_KEY_EVENT);
        LocalBroadcastManager.getInstance(context).registerReceiver(mReceiver, filter);

        mDelegate.start(context, webContents);
    }

    public void stop(Context context) {
        if (DEBUG) Log.d(TAG, "stop");

        LocalBroadcastManager.getInstance(context).unregisterReceiver(mReceiver);
        mDelegate.stop(context);
    }

    public static void onComponentClosed(Context context, String instanceId) {
        if (DEBUG) Log.d(TAG, "onComponentClosed");

        Intent intent = new Intent(ACTION_ACTIVITY_STOPPED, getInstanceUri(instanceId));
        LocalBroadcastManager.getInstance(context).sendBroadcastSync(intent);
    }

    public static void onKeyDown(Context context, String instanceId, int keyCode) {
        if (DEBUG) Log.d(TAG, "onKeyDown");

        Intent intent = new Intent(ACTION_KEY_EVENT, getInstanceUri(instanceId));
        intent.putExtra(ACTION_EXTRA_KEY_CODE, keyCode);
        LocalBroadcastManager.getInstance(context).sendBroadcastSync(intent);
    }

    private static Uri getInstanceUri(String instanceId) {
        Uri instanceUri = new Uri.Builder()
                                  .scheme(ACTION_DATA_SCHEME)
                                  .authority(ACTION_DATA_AUTHORITY)
                                  .path(instanceId)
                                  .build();
        return instanceUri;
    }
}