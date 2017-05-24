// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/data_use_measurement/chrome_data_use_ascriber.h"

#include <list>
#include <memory>

#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "components/data_use_measurement/core/data_use_recorder.h"
#include "content/public/browser/resource_request_info.h"
#include "content/public/common/browser_side_navigation_policy.h"
#include "content/public/common/previews_state.h"
#include "content/public/common/process_type.h"
#include "content/public/test/mock_resource_context.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

int kRenderProcessId = 1;
int kRenderFrameId = 2;
int kRequestId = 3;
uint32_t kPageTransition = 1;
void* kNavigationHandle = &kNavigationHandle;

class MockPageLoadObserver
    : public data_use_measurement::DataUseAscriber::PageLoadObserver {
 public:
  MOCK_METHOD1(OnPageLoadCommit, void(data_use_measurement::DataUse* data_use));
  MOCK_METHOD2(OnPageResourceLoad,
               void(const net::URLRequest& request,
                    data_use_measurement::DataUse* data_use));
  MOCK_METHOD1(OnPageLoadComplete,
               void(data_use_measurement::DataUse* data_use));
};

}  // namespace

namespace data_use_measurement {

class ChromeDataUseAscriberTest : public testing::Test {
 protected:
  ChromeDataUseAscriberTest()
      : thread_bundle_(content::TestBrowserThreadBundle::IO_MAINLOOP),
        resource_context_(new content::MockResourceContext(&context_)) {}

  void SetUp() override {}

  void TearDown() override { recorders().clear(); }

  std::list<ChromeDataUseRecorder>& recorders() {
    return ascriber_.data_use_recorders_;
  }

  net::TestURLRequestContext* context() { return &context_; }

  content::MockResourceContext* resource_context() {
    return resource_context_.get();
  }

  ChromeDataUseAscriber* ascriber() { return &ascriber_; }

  std::unique_ptr<net::URLRequest> CreateNewRequest(std::string url,
                                                    bool is_main_frame,
                                                    int request_id,
                                                    int render_process_id,
                                                    int render_frame_id) {
    std::unique_ptr<net::URLRequest> request = context()->CreateRequest(
        GURL(url), net::IDLE, nullptr, TRAFFIC_ANNOTATION_FOR_TESTS);
    // TODO(kundaji): Allow request_id to be specified in AllocateForTesting.
    content::ResourceRequestInfo::AllocateForTesting(
        request.get(), content::RESOURCE_TYPE_MAIN_FRAME, resource_context(),
        render_process_id,
        /*render_view_id=*/-1, render_frame_id, is_main_frame,
        /*parent_is_main_frame=*/false,
        /*allow_download=*/false,
        /*is_async=*/true, content::PREVIEWS_OFF);
    return request;
  }

 private:
  content::TestBrowserThreadBundle thread_bundle_;
  ChromeDataUseAscriber ascriber_;
  net::TestURLRequestContext context_;
  std::unique_ptr<content::MockResourceContext> resource_context_;
};

TEST_F(ChromeDataUseAscriberTest, NoRecorderWithoutFrame) {
  if (content::IsBrowserSideNavigationEnabled())
    return;

  std::unique_ptr<net::URLRequest> request = CreateNewRequest(
      "http://test.com", true, kRequestId, kRenderProcessId, kRenderFrameId);

  // Main frame request should not cause a recorder to be created, since the
  // frame does not exist.
  ascriber()->OnBeforeUrlRequest(request.get());
  EXPECT_EQ(0u, recorders().size());

  // Frame is created.
  ascriber()->RenderFrameCreated(kRenderProcessId, kRenderFrameId, -1, -1);
  EXPECT_EQ(1u, recorders().size());

  // Request should cause a recorder to be created.
  ascriber()->OnBeforeUrlRequest(request.get());
  EXPECT_EQ(2u, recorders().size());

  ascriber()->RenderFrameDeleted(kRenderProcessId, kRenderFrameId, -1, -1);
}

TEST_F(ChromeDataUseAscriberTest, RenderFrameShownAndHidden) {
  if (content::IsBrowserSideNavigationEnabled())
    return;

  std::unique_ptr<net::URLRequest> request = CreateNewRequest(
      "http://test.com", true, kRequestId, kRenderProcessId, kRenderFrameId);

  ascriber()->RenderFrameCreated(kRenderProcessId, kRenderFrameId, -1, -1);
  ascriber()->OnBeforeUrlRequest(request.get());
  ascriber()->ReadyToCommitMainFrameNavigation(
      GURL("http://test.com"), content::GlobalRequestID(kRenderProcessId, 0),
      kRenderProcessId, kRenderFrameId, true, (void*)request.get());
  ascriber()->WasShownOrHidden(kRenderProcessId, kRenderFrameId, true);

  EXPECT_TRUE(ascriber()->GetDataUseRecorder(*request)->is_visible());

  // Hide the frame, and the visibility should be updated.
  ascriber()->WasShownOrHidden(kRenderProcessId, kRenderFrameId, false);
  EXPECT_FALSE(ascriber()->GetDataUseRecorder(*request)->is_visible());

  ascriber()->RenderFrameDeleted(kRenderProcessId, kRenderFrameId, -1, -1);
}

TEST_F(ChromeDataUseAscriberTest, RenderFrameHiddenAndShown) {
  if (content::IsBrowserSideNavigationEnabled())
    return;

  std::unique_ptr<net::URLRequest> request = CreateNewRequest(
      "http://test.com", true, kRequestId, kRenderProcessId, kRenderFrameId);

  ascriber()->RenderFrameCreated(kRenderProcessId, kRenderFrameId, -1, -1);
  ascriber()->OnBeforeUrlRequest(request.get());
  ascriber()->ReadyToCommitMainFrameNavigation(
      GURL("http://test.com"), content::GlobalRequestID(kRenderProcessId, 0),
      kRenderProcessId, kRenderFrameId, true, (void*)request.get());
  ascriber()->WasShownOrHidden(kRenderProcessId, kRenderFrameId, false);

  EXPECT_FALSE(ascriber()->GetDataUseRecorder(*request)->is_visible());

  // Show the frame, and the visibility should be updated.
  ascriber()->WasShownOrHidden(kRenderProcessId, kRenderFrameId, true);
  EXPECT_TRUE(ascriber()->GetDataUseRecorder(*request)->is_visible());

  ascriber()->RenderFrameDeleted(kRenderProcessId, kRenderFrameId, -1, -1);
}

TEST_F(ChromeDataUseAscriberTest, RenderFrameHostChanged) {
  if (content::IsBrowserSideNavigationEnabled())
    return;

  std::unique_ptr<net::URLRequest> request = CreateNewRequest(
      "http://test.com", true, kRequestId, kRenderProcessId, kRenderFrameId);

  ascriber()->RenderFrameCreated(kRenderProcessId, kRenderFrameId, -1, -1);
  ascriber()->OnBeforeUrlRequest(request.get());
  ascriber()->ReadyToCommitMainFrameNavigation(
      GURL("http://test.com"), content::GlobalRequestID(kRenderProcessId, 0),
      kRenderProcessId, kRenderFrameId, true, (void*)request.get());
  ascriber()->WasShownOrHidden(kRenderProcessId, kRenderFrameId, true);
  EXPECT_TRUE(ascriber()->GetDataUseRecorder(*request)->is_visible());

  // Create a new render frame and swap it.
  ascriber()->RenderFrameCreated(kRenderProcessId + 1, kRenderFrameId + 1, -1,
                                 -1);
  ascriber()->RenderFrameHostChanged(kRenderProcessId, kRenderFrameId,
                                     kRenderProcessId + 1, kRenderFrameId + 1);
  ascriber()->RenderFrameDeleted(kRenderProcessId, kRenderFrameId, -1, -1);

  ascriber()->WasShownOrHidden(kRenderProcessId + 1, kRenderFrameId + 1, true);
  ascriber()->RenderFrameDeleted(kRenderProcessId + 1, kRenderFrameId + 1, -1,
                                 -1);
}

TEST_F(ChromeDataUseAscriberTest, MainFrameNavigation) {
  if (content::IsBrowserSideNavigationEnabled())
    return;

  std::unique_ptr<net::URLRequest> request = CreateNewRequest(
      "http://test.com", true, kRequestId, kRenderProcessId, kRenderFrameId);

  // Mainframe is created.
  ascriber()->RenderFrameCreated(kRenderProcessId, kRenderFrameId, -1, -1);
  EXPECT_EQ(1u, recorders().size());

  // Request should cause a recorder to be created.
  ascriber()->OnBeforeUrlRequest(request.get());
  EXPECT_EQ(2u, recorders().size());

  // Navigation starts.
  ascriber()->DidStartMainFrameNavigation(GURL("http://test.com"),
                                          kRenderProcessId, kRenderFrameId,
                                          kNavigationHandle);

  ascriber()->ReadyToCommitMainFrameNavigation(
      GURL("http://mobile.test.com"),
      content::GlobalRequestID(kRenderProcessId, 0), kRenderProcessId,
      kRenderFrameId, false, kNavigationHandle);

  // Navigation commit should merge the two data use recorder entries.
  EXPECT_EQ(1u, recorders().size());
  auto& recorder_entry = recorders().front();
  EXPECT_EQ(RenderFrameHostID(kRenderProcessId, kRenderFrameId),
            recorder_entry.main_frame_id());
  EXPECT_EQ(content::GlobalRequestID(kRenderProcessId, 0),
            recorder_entry.main_frame_request_id());
  EXPECT_EQ(GURL("http://mobile.test.com"), recorder_entry.data_use().url());
  EXPECT_EQ(DataUse::TrafficType::USER_TRAFFIC,
            recorder_entry.data_use().traffic_type());

  ascriber()->RenderFrameDeleted(kRenderProcessId, kRenderFrameId, -1, -1);
  ascriber()->OnUrlRequestDestroyed(request.get());

  EXPECT_EQ(0u, recorders().size());
}

TEST_F(ChromeDataUseAscriberTest, FailedMainFrameNavigation) {
  if (content::IsBrowserSideNavigationEnabled())
    return;

  std::unique_ptr<net::URLRequest> request = CreateNewRequest(
      "http://test.com", true, kRequestId, kRenderProcessId, kRenderFrameId);

  // Mainframe is created.
  ascriber()->RenderFrameCreated(kRenderProcessId, kRenderFrameId, -1, -1);
  EXPECT_EQ(1u, recorders().size());

  // Request should cause a recorder to be created.
  ascriber()->OnBeforeUrlRequest(request.get());
  EXPECT_EQ(2u, recorders().size());

  // Failed request will remove the pending entry.
  request->Cancel();
  ascriber()->OnUrlRequestCompleted(*request, false);

  ascriber()->RenderFrameDeleted(kRenderProcessId, kRenderFrameId, -1, -1);
  ascriber()->OnUrlRequestDestroyed(request.get());

  EXPECT_EQ(0u, recorders().size());
}

TEST_F(ChromeDataUseAscriberTest, PageLoadObserverNotified) {
  // TODO(rajendrant): Handle PlzNavigate (http://crbug/664233).
  if (content::IsBrowserSideNavigationEnabled())
    return;

  MockPageLoadObserver mock_observer;
  ascriber()->AddObserver(&mock_observer);

  std::unique_ptr<net::URLRequest> request = CreateNewRequest(
      "http://test.com", true, kRequestId, kRenderProcessId, kRenderFrameId);

  // Mainframe is created.
  ascriber()->RenderFrameCreated(kRenderProcessId, kRenderFrameId, -1, -1);
  EXPECT_EQ(1u, recorders().size());

  ascriber()->OnBeforeUrlRequest(request.get());

  // Navigation starts.
  ascriber()->DidStartMainFrameNavigation(GURL("http://test.com"),
                                          kRenderProcessId, kRenderFrameId,
                                          kNavigationHandle);

  ascriber()->ReadyToCommitMainFrameNavigation(
      GURL("http://mobile.test.com"),
      content::GlobalRequestID(kRenderProcessId, 0), kRenderProcessId,
      kRenderFrameId, false, kNavigationHandle);

  EXPECT_EQ(1u, recorders().size());
  auto& recorder_entry = recorders().front();
  DataUse* data_use = &recorder_entry.data_use();
  EXPECT_EQ(RenderFrameHostID(kRenderProcessId, kRenderFrameId),
            recorder_entry.main_frame_id());
  EXPECT_EQ(content::GlobalRequestID(kRenderProcessId, 0),
            recorder_entry.main_frame_request_id());
  EXPECT_EQ(GURL("http://mobile.test.com"), recorder_entry.data_use().url());

  EXPECT_CALL(mock_observer, OnPageLoadCommit(data_use)).Times(1);
  ascriber()->DidFinishNavigation(kRenderProcessId, kRenderFrameId,
                                  kPageTransition);

  EXPECT_CALL(mock_observer, OnPageLoadComplete(data_use)).Times(1);
  EXPECT_CALL(mock_observer, OnPageResourceLoad(testing::_, data_use)).Times(1);

  ascriber()->RenderFrameDeleted(kRenderProcessId, kRenderFrameId, -1, -1);
  ascriber()->OnUrlRequestDestroyed(request.get());

  EXPECT_EQ(0u, recorders().size());
}

}  // namespace data_use_measurement
