// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.minidump_uploader;

import android.support.test.filters.MediumTest;

import org.chromium.components.minidump_uploader.util.CrashReportingPermissionManager;
import org.chromium.components.minidump_uploader.util.HttpURLConnectionFactory;

import java.io.File;
import java.io.IOException;
import java.io.InterruptedIOException;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.MalformedURLException;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;

/**
 * Instrumentation tests for the common MinidumpUploader implementation within the minidump_uploader
 * component.
 */
public class MinidumpUploaderImplTest extends CrashTestCase {
    private static final String BOUNDARY = "TESTBOUNDARY";

    /**
     * Test to ensure the minidump uploading mechanism behaves as expected when we fail to upload
     * minidumps.
     */
    @MediumTest
    public void testFailUploadingMinidumps() throws IOException {
        final CrashReportingPermissionManager permManager =
                new MockCrashReportingPermissionManager() {
                    {
                        mIsInSample = true;
                        mIsUserPermitted = true;
                        mIsNetworkAvailable = false; // Will cause us to fail uploads
                        mIsEnabledForTests = false;
                    }
                };
        MinidumpUploader minidumpUploader =
                new TestMinidumpUploaderImpl(getExistingCacheDir(), permManager);

        File firstFile = createMinidumpFileInCrashDir("1_abc.dmp0.try0");
        File secondFile = createMinidumpFileInCrashDir("12_abc.dmp0.try0");
        String triesBelowMaxString = ".try" + (MinidumpUploaderImpl.MAX_UPLOAD_TRIES_ALLOWED - 1);
        String maxTriesString = ".try" + MinidumpUploaderImpl.MAX_UPLOAD_TRIES_ALLOWED;
        File justBelowMaxTriesFile =
                createMinidumpFileInCrashDir("belowmaxtries.dmp0" + triesBelowMaxString);
        File maxTriesFile = createMinidumpFileInCrashDir("maxtries.dmp0" + maxTriesString);

        File expectedFirstFile = new File(mCrashDir, "1_abc.dmp0.try1");
        File expectedSecondFile = new File(mCrashDir, "12_abc.dmp0.try1");
        File expectedJustBelowMaxTriesFile = new File(mCrashDir,
                justBelowMaxTriesFile.getName().replace(triesBelowMaxString, maxTriesString));

        MinidumpUploadTestUtility.uploadMinidumpsSync(
                minidumpUploader, true /* expectReschedule */);
        assertFalse(firstFile.exists());
        assertFalse(secondFile.exists());
        assertFalse(justBelowMaxTriesFile.exists());
        assertTrue(expectedFirstFile.exists());
        assertTrue(expectedSecondFile.exists());
        assertTrue(expectedJustBelowMaxTriesFile.exists());
        // This file should have been left untouched.
        assertTrue(maxTriesFile.exists());
    }

    @MediumTest
    public void testFailingThenPassingUpload() throws IOException {
        final CrashReportingPermissionManager permManager =
                new MockCrashReportingPermissionManager() {
                    { mIsEnabledForTests = true; }
                };
        List<MinidumpUploadCallableCreator> callables = new ArrayList<>();
        callables.add(new MinidumpUploadCallableCreator() {
            @Override
            public MinidumpUploadCallable createCallable(File minidumpFile, File logfile) {
                return new MinidumpUploadCallable(
                        minidumpFile, logfile, new FailingHttpUrlConnectionFactory(), permManager);
            }
        });
        callables.add(new MinidumpUploadCallableCreator() {
            @Override
            public MinidumpUploadCallable createCallable(File minidumpFile, File logfile) {
                return new MinidumpUploadCallable(minidumpFile, logfile,
                        new MinidumpUploadCallableTest.TestHttpURLConnectionFactory(), permManager);
            }
        });
        MinidumpUploader minidumpUploader = createCallableListMinidumpUploader(
                callables, permManager.isUsageAndCrashReportingPermittedByUser());

        File firstFile = createMinidumpFileInCrashDir("firstFile.dmp0.try0");
        File secondFile = createMinidumpFileInCrashDir("secondFile.dmp0.try0");

        MinidumpUploadTestUtility.uploadMinidumpsSync(
                minidumpUploader, true /* expectReschedule */);
        assertFalse(firstFile.exists());
        assertFalse(secondFile.exists());
        File expectedSecondFile;
        // Not sure which minidump will fail and which will succeed, so just ensure one was uploaded
        // and the other one failed.
        if (new File(mCrashDir, "firstFile.dmp0.try1").exists()) {
            expectedSecondFile = new File(mCrashDir, "secondFile.up0.try0");
        } else {
            File uploadedFirstFile = new File(mCrashDir, "firstFile.up0.try0");
            assertTrue(uploadedFirstFile.exists());
            expectedSecondFile = new File(mCrashDir, "secondFile.dmp0.try1");
        }
        assertTrue(expectedSecondFile.exists());
    }

    /**
     * Prior to M60, the ".try0" suffix was optional; however now it is not. This test verifies that
     * the code rejects minidumps that lack this suffix.
     */
    @MediumTest
    public void testInvalidMinidumpNameGeneratesNoUploads() throws IOException {
        MinidumpUploader minidumpUploader =
                new ExpectNoUploadsMinidumpUploaderImpl(getExistingCacheDir());

        // Note the omitted ".try0" suffix.
        File fileUsingLegacyNamingScheme = createMinidumpFileInCrashDir("1_abc.dmp0");

        MinidumpUploadTestUtility.uploadMinidumpsSync(
                minidumpUploader, false /* expectReschedule */);

        // The file should not have been touched, nor should any successful upload files have
        // appeared.
        assertTrue(fileUsingLegacyNamingScheme.exists());
        assertFalse(new File(mCrashDir, "1_abc.up0").exists());
        assertFalse(new File(mCrashDir, "1_abc.up0.try0").exists());
    }

    /**
     * Test that ensures we can interrupt the MinidumpUploader when uploading minidumps.
     */
    @MediumTest
    public void testCancelMinidumpUploadsFailedUpload() throws IOException {
        testCancellation(false /* successfulUpload */);
    }

    /**
     * Test that ensures interrupting our upload-job will not interrupt the first upload.
     */
    @MediumTest
    public void testCancelingWontCancelFirstUpload() throws IOException {
        testCancellation(true /* successfulUpload */);
    }

    private void testCancellation(final boolean successfulUpload) throws IOException {
        final CrashReportingPermissionManager permManager =
                new MockCrashReportingPermissionManager() {
                    { mIsEnabledForTests = true; }
                };
        final CountDownLatch stopStallingLatch = new CountDownLatch(1);
        MinidumpUploaderImpl minidumpUploader = new StallingMinidumpUploaderImpl(
                getExistingCacheDir(), permManager, stopStallingLatch, successfulUpload);

        File firstFile = createMinidumpFileInCrashDir("123_abc.dmp0.try0");

        // This is run on the UI thread to avoid failing any assertOnUiThread assertions.
        MinidumpUploadTestUtility.uploadAllMinidumpsOnUiThread(minidumpUploader,
                new MinidumpUploader.UploadsFinishedCallback() {
                    @Override
                    public void uploadsFinished(boolean reschedule) {
                        if (successfulUpload) {
                            assertFalse(reschedule);
                        } else {
                            fail("This method shouldn't be called when a canceled upload fails.");
                        }
                    }
                },
                // Block until job posted - otherwise the worker thread might not have been created
                // before we try to join it.
                true /* blockUntilJobPosted */);
        minidumpUploader.cancelUploads();
        stopStallingLatch.countDown();
        // Wait until our job finished.
        try {
            minidumpUploader.joinWorkerThreadForTesting();
        } catch (InterruptedException e) {
            throw new RuntimeException(e);
        }

        File expectedFirstUploadFile = new File(mCrashDir, "123_abc.up0.try0");
        File expectedFirstRetryFile = new File(mCrashDir, "123_abc.dmp0.try1");
        if (successfulUpload) {
            // When the upload succeeds we expect the file to be renamed.
            assertFalse(firstFile.exists());
            assertTrue(expectedFirstUploadFile.exists());
            assertFalse(expectedFirstRetryFile.exists());
        } else {
            // When the upload fails we won't change the minidump at all.
            assertTrue(firstFile.exists());
            assertFalse(expectedFirstUploadFile.exists());
            assertFalse(expectedFirstRetryFile.exists());
        }
    }

    /**
     * Ensure that canceling an upload that fails causes a reschedule.
     */
    @MediumTest
    public void testCancelFailedUploadCausesReschedule() throws IOException {
        final CrashReportingPermissionManager permManager =
                new MockCrashReportingPermissionManager() {
                    { mIsEnabledForTests = true; }
                };
        final CountDownLatch stopStallingLatch = new CountDownLatch(1);
        MinidumpUploaderImpl minidumpUploader =
                new StallingMinidumpUploaderImpl(getExistingCacheDir(), permManager,
                        stopStallingLatch, false /* successfulUpload */);

        createMinidumpFileInCrashDir("123_abc.dmp0.try0");

        MinidumpUploader.UploadsFinishedCallback crashingCallback =
                new MinidumpUploader.UploadsFinishedCallback() {
                    @Override
                    public void uploadsFinished(boolean reschedule) {
                        // We don't guarantee whether uploadsFinished is called after a job has been
                        // cancelled, but if it is, it should indicate that we want to reschedule
                        // the job.
                        assertTrue(reschedule);
                    }
                };

        // This is run on the UI thread to avoid failing any assertOnUiThread assertions.
        MinidumpUploadTestUtility.uploadAllMinidumpsOnUiThread(minidumpUploader, crashingCallback);
        // Ensure we tell JobScheduler to reschedule the job.
        assertTrue(minidumpUploader.cancelUploads());
        stopStallingLatch.countDown();
    }

    private interface MinidumpUploadCallableCreator {
        MinidumpUploadCallable createCallable(File minidumpFile, File logfile);
    }

    private MinidumpUploaderImpl createCallableListMinidumpUploader(
            final List<MinidumpUploadCallableCreator> callables, final boolean userPermitted) {
        return new TestMinidumpUploaderImpl(getExistingCacheDir(), null) {
            private int mIndex = 0;

            @Override
            public MinidumpUploadCallable createMinidumpUploadCallable(
                    File minidumpFile, File logfile) {
                if (mIndex >= callables.size()) {
                    fail("Should not create callable number " + mIndex);
                }
                return callables.get(mIndex++).createCallable(minidumpFile, logfile);
            }
        };
    }

    private static class ExpectNoUploadsMinidumpUploaderImpl extends MinidumpUploaderImpl {
        public ExpectNoUploadsMinidumpUploaderImpl(File cacheDir) {
            super(new TestMinidumpUploaderDelegate(
                    cacheDir, new MockCrashReportingPermissionManager() {
                        { mIsEnabledForTests = true; }
                    }));
        }

        @Override
        public CrashFileManager createCrashFileManager(File crashDir) {
            return new CrashFileManager(crashDir) {
                @Override
                public void cleanOutAllNonFreshMinidumpFiles() {}
            };
        }

        @Override
        public MinidumpUploadCallable createMinidumpUploadCallable(
                File minidumpFile, File logfile) {
            fail("No minidumps upload attempts should be initiated by this uploader.");
            return null;
        }
    }

    /**
     * Minidump uploader implementation that stalls minidump-uploading until a given CountDownLatch
     * counts down.
     */
    private static class StallingMinidumpUploaderImpl extends TestMinidumpUploaderImpl {
        CountDownLatch mStopStallingLatch;
        boolean mSuccessfulUpload;

        public StallingMinidumpUploaderImpl(File cacheDir,
                CrashReportingPermissionManager permissionManager, CountDownLatch stopStallingLatch,
                boolean successfulUpload) {
            super(cacheDir, permissionManager);
            mStopStallingLatch = stopStallingLatch;
            mSuccessfulUpload = successfulUpload;
        }

        @Override
        public MinidumpUploadCallable createMinidumpUploadCallable(
                File minidumpFile, File logfile) {
            return new MinidumpUploadCallable(minidumpFile, logfile,
                    new StallingHttpUrlConnectionFactory(mStopStallingLatch, mSuccessfulUpload),
                    mDelegate.createCrashReportingPermissionManager());
        }
    }

    private static class StallingHttpUrlConnectionFactory implements HttpURLConnectionFactory {
        private final CountDownLatch mStopStallingLatch;
        private final boolean mSucceed;

        private class StallingOutputStream extends OutputStream {
            @Override
            public void write(int b) throws IOException {
                try {
                    mStopStallingLatch.await();
                } catch (InterruptedException e) {
                    throw new InterruptedIOException(e.toString());
                }
                if (!mSucceed) {
                    throw new IOException();
                }
            }
        }

        public StallingHttpUrlConnectionFactory(CountDownLatch stopStallingLatch, boolean succeed) {
            mStopStallingLatch = stopStallingLatch;
            mSucceed = succeed;
        }

        public HttpURLConnection createHttpURLConnection(String url) {
            try {
                return new MinidumpUploadCallableTest.TestHttpURLConnection(new URL(url)) {
                    @Override
                    public OutputStream getOutputStream() {
                        return new StallingOutputStream();
                    }
                };
            } catch (MalformedURLException e) {
                return null;
            }
        }
    }

    private static class FailingHttpUrlConnectionFactory implements HttpURLConnectionFactory {
        public HttpURLConnection createHttpURLConnection(String url) {
            return null;
        }
    }

    private File createMinidumpFileInCrashDir(String name) throws IOException {
        File minidumpFile = new File(mCrashDir, name);
        setUpMinidumpFile(minidumpFile, BOUNDARY);
        return minidumpFile;
    }
}
