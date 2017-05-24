// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.shape_detection;

import static org.junit.Assert.assertNull;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.mockito.MockitoAnnotations;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowLog;

import org.chromium.base.test.util.Feature;
import org.chromium.mojo.system.SharedBufferHandle;
import org.chromium.mojo.system.SharedBufferHandle.MapFlags;
import org.chromium.testing.local.LocalRobolectricTestRunner;

import java.nio.ByteBuffer;

/**
 * Test suite for conversion-to-Frame utils.
 */
@RunWith(LocalRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class SharedBufferUtilsTest {
    private static final int VALID_WIDTH = 1;
    private static final int VALID_HEIGHT = 1;
    private static final int INVALID_WIDTH = 0;
    private static final long NUM_BYTES = VALID_WIDTH * VALID_HEIGHT * 4;

    public SharedBufferUtilsTest() {}

    @Before
    public void setUp() {
        ShadowLog.stream = System.out;
        MockitoAnnotations.initMocks(this);
    }

    /**
     * Verify conversion fails if the SharedBufferHandle is invalid.
     */
    @Test
    @Feature({"ShapeDetection"})
    public void testConversionFailsWithInvalidHandle() {
        SharedBufferHandle handle = Mockito.mock(SharedBufferHandle.class);
        Mockito.when(handle.isValid()).thenReturn(false);

        assertNull(SharedBufferUtils.convertToFrame(handle, VALID_WIDTH, VALID_HEIGHT));
    }

    /**
     * Verify conversion fails if the sent dimensions are ugly.
     */
    @Test
    @Feature({"ShapeDetection"})
    public void testConversionFailsWithInvalidDimensions() {
        SharedBufferHandle handle = Mockito.mock(SharedBufferHandle.class);
        Mockito.when(handle.isValid()).thenReturn(true);

        assertNull(SharedBufferUtils.convertToFrame(handle, INVALID_WIDTH, VALID_HEIGHT));
    }

    /**
     * Verify conversion fails if SharedBufferHandle fails to map().
     */
    @Test
    @Feature({"ShapeDetection"})
    public void testConversionFailsWithWronglyMappedBuffer() {
        SharedBufferHandle handle = Mockito.mock(SharedBufferHandle.class);
        Mockito.when(handle.isValid()).thenReturn(true);
        Mockito.when(handle.map(Mockito.eq(0L), Mockito.eq(NUM_BYTES), Mockito.any(MapFlags.class)))
                .thenReturn(ByteBuffer.allocate(0));

        assertNull(SharedBufferUtils.convertToFrame(handle, VALID_WIDTH, VALID_HEIGHT));
    }
}
