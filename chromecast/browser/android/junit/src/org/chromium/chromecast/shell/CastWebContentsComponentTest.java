// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chromecast.shell;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.verify;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.os.PatternMatcher;
import android.support.v4.content.LocalBroadcastManager;

import org.junit.Assert;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.MockitoAnnotations;
import org.robolectric.Robolectric;
import org.robolectric.Shadows;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowActivity;

import org.chromium.content_public.browser.WebContents;
import org.chromium.testing.local.LocalRobolectricTestRunner;

/**
 * Tests for CastWebContentsComponent.
 */
@RunWith(LocalRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class CastWebContentsComponentTest {
    private static final String INSTANCE_ID = "1";

    @Mock
    private WebContents mWebContents;

    private Activity mActivity;
    private ShadowActivity mShadowActivity;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);
        mActivity = Mockito.spy(Robolectric.buildActivity(Activity.class).setup().get());
        mShadowActivity = Shadows.shadowOf(mActivity);
    }

    @Test
    public void testStartStartsWebContentsActivity() {
        Assume.assumeFalse(BuildConfig.DISPLAY_WEB_CONTENTS_IN_SERVICE);

        CastWebContentsComponent component = new CastWebContentsComponent(INSTANCE_ID, null, null);
        component.start(mActivity, mWebContents);
        Intent intent = mShadowActivity.getNextStartedActivity();
        Assert.assertEquals(
                intent.getComponent().getClassName(), CastWebContentsActivity.class.getName());

        component.stop(mActivity);
    }

    @Test
    public void testStopSendsStopSignalToActivity() {
        Assume.assumeFalse(BuildConfig.DISPLAY_WEB_CONTENTS_IN_SERVICE);

        BroadcastReceiver receiver = Mockito.mock(BroadcastReceiver.class);
        IntentFilter intentFilter = new IntentFilter(CastWebContentsActivity.ACTION_STOP_ACTIVITY);
        intentFilter.addDataScheme(CastWebContentsComponent.ACTION_DATA_SCHEME);
        intentFilter.addDataAuthority(CastWebContentsComponent.ACTION_DATA_AUTHORITY, null);
        intentFilter.addDataPath("/" + INSTANCE_ID, PatternMatcher.PATTERN_LITERAL);
        LocalBroadcastManager.getInstance(mActivity).registerReceiver(receiver, intentFilter);

        CastWebContentsComponent component = new CastWebContentsComponent(INSTANCE_ID, null, null);
        component.start(mActivity, mWebContents);
        component.stop(mActivity);

        LocalBroadcastManager.getInstance(mActivity).unregisterReceiver(receiver);

        verify(receiver).onReceive(any(Context.class), any(Intent.class));
    }

    @Test
    public void testStartBindsWebContentsService() {
        Assume.assumeTrue(BuildConfig.DISPLAY_WEB_CONTENTS_IN_SERVICE);

        CastWebContentsComponent component = new CastWebContentsComponent(INSTANCE_ID, null, null);
        component.start(mActivity, mWebContents);
        component.stop(mActivity);

        ArgumentCaptor<Intent> intent = ArgumentCaptor.forClass(Intent.class);
        verify(mActivity).bindService(
                intent.capture(), any(ServiceConnection.class), eq(Context.BIND_AUTO_CREATE));
        Assert.assertEquals(intent.getValue().getComponent().getClassName(),
                CastWebContentsService.class.getName());
    }

    @Test
    public void testStopUnbindsWebContentsService() {
        Assume.assumeTrue(BuildConfig.DISPLAY_WEB_CONTENTS_IN_SERVICE);

        CastWebContentsComponent component = new CastWebContentsComponent(INSTANCE_ID, null, null);
        component.start(mActivity, mWebContents);
        component.stop(mActivity);

        verify(mActivity).unbindService(any(ServiceConnection.class));
    }

    @Test
    public void testOnComponentClosedCallsCallback() {
        CastWebContentsComponent.OnComponentClosedHandler callback =
                Mockito.mock(CastWebContentsComponent.OnComponentClosedHandler.class);

        CastWebContentsComponent component =
                new CastWebContentsComponent(INSTANCE_ID, callback, null);
        component.start(mActivity, mWebContents);
        CastWebContentsComponent.onComponentClosed(mActivity, INSTANCE_ID);
        verify(callback).onComponentClosed();

        component.stop(mActivity);
    }

    @Test
    public void testOnKeyDownCallsCallback() {
        CastWebContentsComponent.OnKeyDownHandler callback =
                Mockito.mock(CastWebContentsComponent.OnKeyDownHandler.class);

        CastWebContentsComponent component =
                new CastWebContentsComponent(INSTANCE_ID, null, callback);
        component.start(mActivity, mWebContents);
        CastWebContentsComponent.onKeyDown(mActivity, INSTANCE_ID, 42);
        component.stop(mActivity);

        verify(callback).onKeyDown(42);
    }
}