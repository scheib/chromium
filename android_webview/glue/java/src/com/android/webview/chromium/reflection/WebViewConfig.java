// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.android.webview.chromium.reflection;

import android.content.Context;
import android.webkit.ValueCallback;

import org.chromium.android_webview.AwContentsStatics;
import org.chromium.base.annotations.UsedByReflection;

/**
 * Entry points for reflection into Android WebView internals for static configuration.
 *
 * Methods in this class may be removed, but they should not be changed in any incompatible way.
 * Methods should be removed when we no longer wish to expose the functionality.
 */
@UsedByReflection("")
public class WebViewConfig {
    /**
     * Starts Safe Browsing initialization. This should only be called once.
     * @param context is the activity context the WebView will be used in.
     * @param callback will be called with the value true if initialization is successful. The
     * callback will be run on the UI thread.
     */
    @UsedByReflection("")
    public static void initSafeBrowsing(Context context, ValueCallback<Boolean> callback) {
        AwContentsStatics.initSafeBrowsing(context, callback);
    }

    /**
     * Shuts down Safe Browsing. This should only be called once.
     */
    @UsedByReflection("")
    public static void shutdownSafeBrowsing() {
        AwContentsStatics.shutdownSafeBrowsing();
    }
}
