// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web/tests/sim/SimTest.h"

#include "core/dom/Document.h"
#include "core/exported/WebViewBase.h"
#include "core/frame/LocalDOMWindow.h"
#include "platform/LayoutTestSupport.h"
#include "platform/scroll/ScrollbarTheme.h"
#include "platform/testing/UnitTestHelpers.h"
#include "public/platform/WebCache.h"
#include "web/WebLocalFrameImpl.h"

namespace blink {

SimTest::SimTest() : web_view_client_(compositor_), web_frame_client_(*this) {
  Document::SetThreadedParsingEnabledForTesting(false);
  // Use the mock theme to get more predictable code paths, this also avoids
  // the OS callbacks in ScrollAnimatorMac which can schedule frames
  // unpredictably since the OS will randomly call into blink for
  // updateScrollerStyleForNewRecommendedScrollerStyle which then does
  // FrameView::scrollbarStyleChanged and will adjust the scrollbar existence
  // in the middle of a test.
  LayoutTestSupport::SetMockThemeEnabledForTest(true);
  ScrollbarTheme::SetMockScrollbarsEnabled(true);
}

SimTest::~SimTest() {
  // Pump the message loop to process the load event.
  testing::RunPendingTasks();

  Document::SetThreadedParsingEnabledForTesting(true);
  LayoutTestSupport::SetMockThemeEnabledForTest(false);
  ScrollbarTheme::SetMockScrollbarsEnabled(false);
  WebCache::Clear();
}

void SimTest::SetUp() {
  Test::SetUp();

  web_view_helper_.Initialize(true, &web_frame_client_, &web_view_client_);
  compositor_.SetWebView(WebView());
  page_.SetPage(WebView().GetPage());
}

void SimTest::LoadURL(const String& url) {
  WebURLRequest request(KURL(kParsedURLString, url));
  WebView().MainFrameImpl()->LoadRequest(request);
}

LocalDOMWindow& SimTest::Window() {
  return *GetDocument().domWindow();
}

SimPage& SimTest::Page() {
  return page_;
}

Document& SimTest::GetDocument() {
  return *WebView().MainFrameImpl()->GetFrame()->GetDocument();
}

WebViewBase& SimTest::WebView() {
  return *web_view_helper_.WebView();
}

WebLocalFrameImpl& SimTest::MainFrame() {
  return *WebView().MainFrameImpl();
}

const SimWebViewClient& SimTest::WebViewClient() const {
  return web_view_client_;
}

SimCompositor& SimTest::Compositor() {
  return compositor_;
}

void SimTest::AddConsoleMessage(const String& message) {
  console_messages_.push_back(message);
}

}  // namespace blink
