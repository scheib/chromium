// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.contextmenu;

import android.support.design.widget.TabLayout;
import android.support.test.filters.SmallTest;
import android.util.Pair;
import android.view.View;
import android.widget.TextView;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.CollectionUtil;
import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.test.ChromeActivityTestRule;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.content_public.common.Referrer;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;

/**
 * A class to checkout the TabularContextMenuUi. This confirms the the UI represents items and
 * groups.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE,
        ChromeActivityTestRule.DISABLE_NETWORK_PREDICTION_FLAG})
public class TabularContextMenuUiTest {
    @Rule
    public ChromeActivityTestRule<ChromeActivity> mActivityTestRule =
            new ChromeActivityTestRule<>(ChromeActivity.class);

    private static class MockMenuParams extends ContextMenuParams {
        private String mUrl = "";

        private MockMenuParams(int mediaType, String pageUrl, String linkUrl, String linkText,
                String unfilteredLinkUrl, String srcUrl, String titleText,
                boolean imageWasFetchedLoFi, Referrer referrer, boolean canSavemedia) {
            super(mediaType, pageUrl, linkUrl, linkText, unfilteredLinkUrl, srcUrl, titleText,
                    imageWasFetchedLoFi, referrer, canSavemedia);
        }

        private MockMenuParams(String url) {
            this(0, "", "", "", "", "", "", false, null, true);
            mUrl = url;
        }

        @Override
        public String getLinkUrl() {
            return mUrl;
        }
    }

    @Before
    public void setUp() throws InterruptedException {
        mActivityTestRule.startMainActivityOnBlankPage();
    }

    @Test
    @SmallTest
    @Feature({"CustomContextMenu"})
    public void testViewDisplaysSingleItemProperly() throws ExecutionException {
        final TabularContextMenuUi dialog = new TabularContextMenuUi(null);

        final List<Pair<Integer, List<ContextMenuItem>>> itemGroups = new ArrayList<>();
        List<? extends ContextMenuItem> item =
                CollectionUtil.newArrayList(ChromeContextMenuItem.ADD_TO_CONTACTS,
                        ChromeContextMenuItem.CALL, ChromeContextMenuItem.COPY_LINK_ADDRESS);
        itemGroups.add(
                new Pair<>(R.string.contextmenu_link_title, Collections.unmodifiableList(item)));
        final String url = "http://google.com";
        View view = ThreadUtils.runOnUiThreadBlocking(new Callable<View>() {
            @Override
            public View call() {
                return dialog.createPagerView(
                        mActivityTestRule.getActivity(), new MockMenuParams(url), itemGroups);
            }
        });

        TabLayout layout = (TabLayout) view.findViewById(R.id.tab_layout);
        Assert.assertEquals(layout.getVisibility(), View.GONE);
    }

    @Test
    @SmallTest
    @Feature({"CustomContextMenu"})
    public void testViewDisplaysViewPagerForMultipleItems() throws ExecutionException {
        final TabularContextMenuUi dialog = new TabularContextMenuUi(null);

        final List<Pair<Integer, List<ContextMenuItem>>> itemGroups = new ArrayList<>();
        List<? extends ContextMenuItem> item =
                CollectionUtil.newArrayList(ChromeContextMenuItem.ADD_TO_CONTACTS,
                        ChromeContextMenuItem.CALL, ChromeContextMenuItem.COPY_LINK_ADDRESS);
        itemGroups.add(
                new Pair<>(R.string.contextmenu_link_title, Collections.unmodifiableList(item)));
        itemGroups.add(
                new Pair<>(R.string.contextmenu_link_title, Collections.unmodifiableList(item)));
        final String url = "http://google.com";
        View view = ThreadUtils.runOnUiThreadBlocking(new Callable<View>() {
            @Override
            public View call() {
                return dialog.createPagerView(
                        mActivityTestRule.getActivity(), new MockMenuParams(url), itemGroups);
            }
        });

        TabLayout layout = (TabLayout) view.findViewById(R.id.tab_layout);
        Assert.assertEquals(layout.getVisibility(), View.VISIBLE);
    }

    @Test
    @SmallTest
    @Feature({"CustomContextMenu"})
    public void testURLIsShownOnContextMenu() throws ExecutionException {
        final TabularContextMenuUi dialog = new TabularContextMenuUi(null);
        final List<? extends ContextMenuItem> item =
                CollectionUtil.newArrayList(ChromeContextMenuItem.ADD_TO_CONTACTS,
                        ChromeContextMenuItem.CALL, ChromeContextMenuItem.COPY_LINK_ADDRESS);
        final String expectedUrl = "http://google.com";
        View view = ThreadUtils.runOnUiThreadBlocking(new Callable<View>() {
            @Override
            public View call() {
                return dialog.createContextMenuPageUi(mActivityTestRule.getActivity(),
                        new MockMenuParams(expectedUrl), Collections.unmodifiableList(item), false,
                        item.size());
            }
        });

        TextView textView = (TextView) view.findViewById(R.id.context_header_text);
        Assert.assertEquals(expectedUrl, String.valueOf(textView.getText()));
    }

    @Test
    @SmallTest
    @Feature({"CustomContextMenu"})
    public void testHeaderIsNotShownWhenThereIsNoParams() throws ExecutionException {
        final TabularContextMenuUi dialog = new TabularContextMenuUi(null);
        final List<? extends ContextMenuItem> item =
                CollectionUtil.newArrayList(ChromeContextMenuItem.ADD_TO_CONTACTS,
                        ChromeContextMenuItem.CALL, ChromeContextMenuItem.COPY_LINK_ADDRESS);
        View view = ThreadUtils.runOnUiThreadBlocking(new Callable<View>() {
            @Override
            public View call() {
                return dialog.createContextMenuPageUi(mActivityTestRule.getActivity(),
                        new MockMenuParams(""), Collections.unmodifiableList(item), false,
                        item.size());
            }
        });

        Assert.assertEquals(view.findViewById(R.id.context_header_text).getVisibility(), View.GONE);
        Assert.assertEquals(view.findViewById(R.id.context_divider).getVisibility(), View.GONE);
    }

    @Test
    @SmallTest
    @Feature({"CustomContextMenu"})
    public void testLinkShowsMultipleLinesWhenClicked() throws ExecutionException {
        final TabularContextMenuUi dialog = new TabularContextMenuUi(null);
        final List<? extends ContextMenuItem> item =
                CollectionUtil.newArrayList(ChromeContextMenuItem.ADD_TO_CONTACTS,
                        ChromeContextMenuItem.CALL, ChromeContextMenuItem.COPY_LINK_ADDRESS);
        View view = ThreadUtils.runOnUiThreadBlocking(new Callable<View>() {
            @Override
            public View call() {
                return dialog.createContextMenuPageUi(mActivityTestRule.getActivity(),
                        new MockMenuParams("http://google.com"), Collections.unmodifiableList(item),
                        false, item.size());
            }
        });

        final TextView headerTextView = (TextView) view.findViewById(R.id.context_header_text);
        int expectedMaxLines = 1;
        int actualMaxLines = headerTextView.getMaxLines();
        Assert.assertEquals("Expected a different number of default maximum lines.",
                expectedMaxLines, actualMaxLines);

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                headerTextView.callOnClick();
            }
        });

        expectedMaxLines = Integer.MAX_VALUE;
        actualMaxLines = headerTextView.getMaxLines();
        Assert.assertEquals(
                "Expected a different number of maximum lines when the header is clicked.",
                expectedMaxLines, actualMaxLines);
    }
}
