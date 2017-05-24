// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code if governed by a BSD-style license that can be
// found in LICENSE file.

#include "core/exported/WebViewBase.h"
#include "platform/scheduler/renderer/web_view_scheduler.h"
#include "platform/testing/TestingPlatformSupport.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/WebRTCError.h"
#include "public/platform/WebRTCPeerConnectionHandler.h"
#include "public/platform/WebRTCRtpReceiver.h"
#include "public/platform/WebRTCRtpSender.h"
#include "public/platform/WebRTCSessionDescription.h"
#include "public/web/WebScriptSource.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "web/WebLocalFrameImpl.h"
#include "web/tests/sim/SimRequest.h"
#include "web/tests/sim/SimTest.h"

using testing::_;

namespace blink {

class ActiveConnectionThrottlingTest : public SimTest {};

TEST_F(ActiveConnectionThrottlingTest, WebSocketStopsThrottling) {
  SimRequest main_resource("https://example.com/", "text/html");

  LoadURL("https://example.com/");

  EXPECT_FALSE(WebView().Scheduler()->HasActiveConnectionForTest());

  main_resource.Complete(
      "(<script>"
      "  var socket = new WebSocket(\"ws://www.example.com/websocket\");"
      "</script>)");

  EXPECT_TRUE(WebView().Scheduler()->HasActiveConnectionForTest());

  MainFrame().ExecuteScript(WebString("socket.close();"));

  EXPECT_FALSE(WebView().Scheduler()->HasActiveConnectionForTest());
}

namespace {

class MockWebRTCPeerConnectionHandler : public WebRTCPeerConnectionHandler {
 public:
  MockWebRTCPeerConnectionHandler() {}
  ~MockWebRTCPeerConnectionHandler() override {}

  bool Initialize(const WebRTCConfiguration&,
                  const WebMediaConstraints&) override {
    return true;
  }

  void CreateOffer(const WebRTCSessionDescriptionRequest&,
                   const WebMediaConstraints&) override {}
  void CreateOffer(const WebRTCSessionDescriptionRequest&,
                   const WebRTCOfferOptions&) override {}
  void CreateAnswer(const WebRTCSessionDescriptionRequest&,
                    const WebMediaConstraints&) override {}
  void CreateAnswer(const WebRTCSessionDescriptionRequest&,
                    const WebRTCAnswerOptions&) override {}
  void SetLocalDescription(const WebRTCVoidRequest&,
                           const WebRTCSessionDescription&) override {}
  void SetRemoteDescription(const WebRTCVoidRequest&,
                            const WebRTCSessionDescription&) override {}
  WebRTCSessionDescription LocalDescription() override {
    return WebRTCSessionDescription();
  }
  WebRTCSessionDescription RemoteDescription() override {
    return WebRTCSessionDescription();
  }
  WebRTCErrorType SetConfiguration(const WebRTCConfiguration&) override {
    return WebRTCErrorType::kNone;
  }
  bool AddStream(const WebMediaStream&, const WebMediaConstraints&) override {
    return true;
  }
  void RemoveStream(const WebMediaStream&) override {}
  void GetStats(const WebRTCStatsRequest&) override {}
  void GetStats(std::unique_ptr<WebRTCStatsReportCallback>) override {}
  blink::WebVector<std::unique_ptr<blink::WebRTCRtpSender>> GetSenders()
      override {
    return blink::WebVector<std::unique_ptr<blink::WebRTCRtpSender>>();
  }
  blink::WebVector<std::unique_ptr<blink::WebRTCRtpReceiver>> GetReceivers()
      override {
    return blink::WebVector<std::unique_ptr<blink::WebRTCRtpReceiver>>();
  }
  WebRTCDataChannelHandler* CreateDataChannel(
      const WebString& label,
      const WebRTCDataChannelInit&) override {
    return nullptr;
  }
  WebRTCDTMFSenderHandler* CreateDTMFSender(
      const WebMediaStreamTrack&) override {
    return nullptr;
  }
  void Stop() override {}
};

class TestingPlatformSupportWithWebRTC : public TestingPlatformSupport {
 public:
  std::unique_ptr<blink::WebRTCPeerConnectionHandler>
  CreateRTCPeerConnectionHandler(
      blink::WebRTCPeerConnectionHandlerClient*) override {
    return WTF::MakeUnique<MockWebRTCPeerConnectionHandler>();
  }
};

}  // namespace

TEST_F(ActiveConnectionThrottlingTest, WebRTCStopsThrottling) {
  ScopedTestingPlatformSupport<TestingPlatformSupportWithWebRTC> platform;

  SimRequest main_resource("https://example.com/", "text/html");

  LoadURL("https://example.com/");

  EXPECT_FALSE(WebView().Scheduler()->HasActiveConnectionForTest());

  main_resource.Complete(
      "(<script>"
      "  var data_channel = new RTCPeerConnection();"
      "</script>)");

  EXPECT_TRUE(WebView().Scheduler()->HasActiveConnectionForTest());

  MainFrame().ExecuteScript(WebString("data_channel.close();"));

  EXPECT_FALSE(WebView().Scheduler()->HasActiveConnectionForTest());
}

}  // namespace blink
