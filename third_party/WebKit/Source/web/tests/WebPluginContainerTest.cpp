/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "public/web/WebPluginContainer.h"

#include <memory>
#include <string>
#include "core/dom/Element.h"
#include "core/events/KeyboardEvent.h"
#include "core/exported/WebPluginContainerBase.h"
#include "core/exported/WebViewBase.h"
#include "core/frame/EventHandlerRegistry.h"
#include "core/layout/LayoutObject.h"
#include "core/page/Page.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/paint/CullRect.h"
#include "platform/graphics/paint/ForeignLayerDisplayItem.h"
#include "platform/graphics/paint/PaintController.h"
#include "platform/graphics/paint/PaintRecorder.h"
#include "platform/testing/RuntimeEnabledFeaturesTestHelpers.h"
#include "platform/testing/URLTestHelpers.h"
#include "platform/testing/UnitTestHelpers.h"
#include "public/platform/Platform.h"
#include "public/platform/WebClipboard.h"
#include "public/platform/WebCoalescedInputEvent.h"
#include "public/platform/WebCompositorSupport.h"
#include "public/platform/WebLayer.h"
#include "public/platform/WebMouseWheelEvent.h"
#include "public/platform/WebThread.h"
#include "public/platform/WebTouchEvent.h"
#include "public/platform/WebURLLoaderMockFactory.h"
#include "public/web/WebDocument.h"
#include "public/web/WebElement.h"
#include "public/web/WebFrameClient.h"
#include "public/web/WebPluginParams.h"
#include "public/web/WebPrintParams.h"
#include "public/web/WebSettings.h"
#include "public/web/WebView.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "web/WebLocalFrameImpl.h"
#include "web/tests/FakeWebPlugin.h"
#include "web/tests/FrameTestHelpers.h"

using blink::testing::RunPendingTasks;

namespace blink {

class WebPluginContainerTest : public ::testing::Test {
 public:
  WebPluginContainerTest() : base_url_("http://www.test.com/") {}

  void TearDown() override {
    Platform::Current()
        ->GetURLLoaderMockFactory()
        ->UnregisterAllURLsAndClearMemoryCache();
  }

  void CalculateGeometry(WebPluginContainerBase* plugin_container_impl,
                         IntRect& window_rect,
                         IntRect& clip_rect,
                         IntRect& unobscured_rect) {
    plugin_container_impl->CalculateGeometry(window_rect, clip_rect,
                                             unobscured_rect);
  }

  void RegisterMockedURL(
      const std::string& file_name,
      const std::string& mime_type = std::string("text/html")) {
    URLTestHelpers::RegisterMockedURLLoadFromBase(
        WebString::FromUTF8(base_url_), testing::WebTestDataPath(),
        WebString::FromUTF8(file_name), WebString::FromUTF8(mime_type));
  }

 protected:
  std::string base_url_;
};

namespace {

template <typename T>
class CustomPluginWebFrameClient : public FrameTestHelpers::TestWebFrameClient {
 public:
  WebPlugin* CreatePlugin(const WebPluginParams& params) override {
    return new T(params);
  }
};

class TestPluginWebFrameClient;

// Subclass of FakeWebPlugin that has a selection of 'x' as plain text and 'y'
// as markup text.
class TestPlugin : public FakeWebPlugin {
 public:
  TestPlugin(const WebPluginParams& params,
             TestPluginWebFrameClient* test_client)
      : FakeWebPlugin(params), test_client_(test_client) {}

  bool HasSelection() const override { return true; }
  WebString SelectionAsText() const override { return WebString("x"); }
  WebString SelectionAsMarkup() const override { return WebString("y"); }
  bool SupportsPaginatedPrint() override { return true; }
  int PrintBegin(const WebPrintParams& print_params) override { return 1; }
  void PrintPage(int page_number, WebCanvas*) override;

 private:
  TestPluginWebFrameClient* const test_client_;
};

class TestPluginWebFrameClient : public FrameTestHelpers::TestWebFrameClient {
  WebPlugin* CreatePlugin(const WebPluginParams& params) override {
    if (params.mime_type == "application/x-webkit-test-webplugin" ||
        params.mime_type == "application/pdf")
      return new TestPlugin(params, this);
    return WebFrameClient::CreatePlugin(params);
  }

 public:
  void OnPrintPage() { printed_page_ = true; }
  bool PrintedAtLeastOnePage() const { return printed_page_; }

 private:
  bool printed_page_ = false;
};

void TestPlugin::PrintPage(int page_number, WebCanvas* canvas) {
  DCHECK(test_client_);
  test_client_->OnPrintPage();
}

WebPluginContainer* GetWebPluginContainer(WebView* web_view,
                                          const WebString& id) {
  WebElement element = web_view->MainFrame()->GetDocument().GetElementById(id);
  return element.PluginContainer();
}

}  // namespace

TEST_F(WebPluginContainerTest, WindowToLocalPointTest) {
  RegisterMockedURL("plugin_container.html");
  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_container.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebPluginContainer* plugin_container_one =
      GetWebPluginContainer(web_view, WebString::FromUTF8("translated-plugin"));
  DCHECK(plugin_container_one);
  WebPoint point1 =
      plugin_container_one->RootFrameToLocalPoint(WebPoint(10, 10));
  ASSERT_EQ(0, point1.x);
  ASSERT_EQ(0, point1.y);
  WebPoint point2 =
      plugin_container_one->RootFrameToLocalPoint(WebPoint(100, 100));
  ASSERT_EQ(90, point2.x);
  ASSERT_EQ(90, point2.y);

  WebPluginContainer* plugin_container_two =
      GetWebPluginContainer(web_view, WebString::FromUTF8("rotated-plugin"));
  DCHECK(plugin_container_two);
  WebPoint point3 =
      plugin_container_two->RootFrameToLocalPoint(WebPoint(0, 10));
  ASSERT_EQ(10, point3.x);
  ASSERT_EQ(0, point3.y);
  WebPoint point4 =
      plugin_container_two->RootFrameToLocalPoint(WebPoint(-10, 10));
  ASSERT_EQ(10, point4.x);
  ASSERT_EQ(10, point4.y);
}

TEST_F(WebPluginContainerTest, PluginDocumentPluginIsFocused) {
  RegisterMockedURL("test.pdf", "application/pdf");

  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "test.pdf", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->UpdateAllLifecyclePhases();

  WebDocument document = web_view->MainFrame()->GetDocument();
  EXPECT_TRUE(document.IsPluginDocument());
  WebPluginContainer* plugin_container =
      GetWebPluginContainer(web_view, "plugin");
  EXPECT_EQ(document.FocusedElement(), plugin_container->GetElement());
}

TEST_F(WebPluginContainerTest, IFramePluginDocumentNotFocused) {
  RegisterMockedURL("test.pdf", "application/pdf");
  RegisterMockedURL("iframe_pdf.html", "text/html");

  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "iframe_pdf.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->UpdateAllLifecyclePhases();

  WebDocument document = web_view->MainFrame()->GetDocument();
  WebFrame* iframe = web_view->MainFrame()->FirstChild();
  EXPECT_TRUE(iframe->GetDocument().IsPluginDocument());
  WebPluginContainer* plugin_container =
      iframe->GetDocument().GetElementById("plugin").PluginContainer();
  EXPECT_NE(document.FocusedElement(), plugin_container->GetElement());
  EXPECT_NE(iframe->GetDocument().FocusedElement(),
            plugin_container->GetElement());
}

TEST_F(WebPluginContainerTest, PrintOnePage) {
  RegisterMockedURL("test.pdf", "application/pdf");

  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "test.pdf", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();
  WebFrame* frame = web_view->MainFrame();

  WebPrintParams print_params;
  print_params.print_content_area.width = 500;
  print_params.print_content_area.height = 500;

  frame->PrintBegin(print_params);
  PaintRecorder recorder;
  frame->PrintPage(0, recorder.beginRecording(IntRect()));
  frame->PrintEnd();
  DCHECK(plugin_web_frame_client.PrintedAtLeastOnePage());
}

TEST_F(WebPluginContainerTest, PrintAllPages) {
  RegisterMockedURL("test.pdf", "application/pdf");

  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "test.pdf", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();
  WebFrame* frame = web_view->MainFrame();

  WebPrintParams print_params;
  print_params.print_content_area.width = 500;
  print_params.print_content_area.height = 500;

  frame->PrintBegin(print_params);
  PaintRecorder recorder;
  frame->PrintPagesWithBoundaries(recorder.beginRecording(IntRect()),
                                  WebSize());
  frame->PrintEnd();
  DCHECK(plugin_web_frame_client.PrintedAtLeastOnePage());
}

TEST_F(WebPluginContainerTest, LocalToWindowPointTest) {
  RegisterMockedURL("plugin_container.html");
  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_container.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebPluginContainer* plugin_container_one =
      GetWebPluginContainer(web_view, WebString::FromUTF8("translated-plugin"));
  DCHECK(plugin_container_one);
  WebPoint point1 = plugin_container_one->LocalToRootFramePoint(WebPoint(0, 0));
  ASSERT_EQ(10, point1.x);
  ASSERT_EQ(10, point1.y);
  WebPoint point2 =
      plugin_container_one->LocalToRootFramePoint(WebPoint(90, 90));
  ASSERT_EQ(100, point2.x);
  ASSERT_EQ(100, point2.y);

  WebPluginContainer* plugin_container_two =
      GetWebPluginContainer(web_view, WebString::FromUTF8("rotated-plugin"));
  DCHECK(plugin_container_two);
  WebPoint point3 =
      plugin_container_two->LocalToRootFramePoint(WebPoint(10, 0));
  ASSERT_EQ(0, point3.x);
  ASSERT_EQ(10, point3.y);
  WebPoint point4 =
      plugin_container_two->LocalToRootFramePoint(WebPoint(10, 10));
  ASSERT_EQ(-10, point4.x);
  ASSERT_EQ(10, point4.y);
}

// Verifies executing the command 'Copy' results in copying to the clipboard.
TEST_F(WebPluginContainerTest, Copy) {
  RegisterMockedURL("plugin_container.html");
  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_container.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  web_view->MainFrame()
      ->GetDocument()
      .Unwrap<Document>()
      ->body()
      ->getElementById("translated-plugin")
      ->focus();
  EXPECT_TRUE(web_view->MainFrame()->ToWebLocalFrame()->ExecuteCommand("Copy"));
  EXPECT_EQ(WebString("x"), Platform::Current()->Clipboard()->ReadPlainText(
                                WebClipboard::Buffer()));
}

TEST_F(WebPluginContainerTest, CopyFromContextMenu) {
  RegisterMockedURL("plugin_container.html");
  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_container.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  auto event = FrameTestHelpers::CreateMouseEvent(WebMouseEvent::kMouseDown,
                                                  WebMouseEvent::Button::kRight,
                                                  WebPoint(30, 30), 0);
  event.click_count = 1;

  // Make sure the right-click + Copy works in common scenario.
  web_view->HandleInputEvent(WebCoalescedInputEvent(event));
  EXPECT_TRUE(web_view->MainFrame()->ToWebLocalFrame()->ExecuteCommand("Copy"));
  EXPECT_EQ(WebString("x"), Platform::Current()->Clipboard()->ReadPlainText(
                                WebClipboard::Buffer()));

  // Clear the clipboard buffer.
  Platform::Current()->Clipboard()->WritePlainText(WebString(""));
  EXPECT_EQ(WebString(""), Platform::Current()->Clipboard()->ReadPlainText(
                               WebClipboard::Buffer()));

  // Now, let's try a more complex scenario:
  // 1) open the context menu. This will focus the plugin.
  web_view->HandleInputEvent(WebCoalescedInputEvent(event));
  // 2) document blurs the plugin, because it can.
  web_view->ClearFocusedElement();
  // 3) Copy should still operate on the context node, even though the focus had
  //    shifted.
  EXPECT_TRUE(web_view->MainFrame()->ToWebLocalFrame()->ExecuteCommand("Copy"));
  EXPECT_EQ(WebString("x"), Platform::Current()->Clipboard()->ReadPlainText(
                                WebClipboard::Buffer()));
}

// Verifies |Ctrl-C| and |Ctrl-Insert| keyboard events, results in copying to
// the clipboard.
TEST_F(WebPluginContainerTest, CopyInsertKeyboardEventsTest) {
  RegisterMockedURL("plugin_container.html");
  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_container.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_container_one_element =
      web_view->MainFrame()->GetDocument().GetElementById(
          WebString::FromUTF8("translated-plugin"));
  WebInputEvent::Modifiers modifier_key = static_cast<WebInputEvent::Modifiers>(
      WebInputEvent::kControlKey | WebInputEvent::kNumLockOn |
      WebInputEvent::kIsLeft);
#if OS(MACOSX)
  modifier_key = static_cast<WebInputEvent::Modifiers>(
      WebInputEvent::kMetaKey | WebInputEvent::kNumLockOn |
      WebInputEvent::kIsLeft);
#endif
  WebKeyboardEvent web_keyboard_event_c(WebInputEvent::kRawKeyDown,
                                        modifier_key,
                                        WebInputEvent::kTimeStampForTesting);
  web_keyboard_event_c.windows_key_code = 67;
  KeyboardEvent* key_event_c = KeyboardEvent::Create(web_keyboard_event_c, 0);
  ToWebPluginContainerBase(plugin_container_one_element.PluginContainer())
      ->HandleEvent(key_event_c);
  EXPECT_EQ(WebString("x"), Platform::Current()->Clipboard()->ReadPlainText(
                                WebClipboard::Buffer()));

  // Clearing |Clipboard::Buffer()|.
  Platform::Current()->Clipboard()->WritePlainText(WebString(""));
  EXPECT_EQ(WebString(""), Platform::Current()->Clipboard()->ReadPlainText(
                               WebClipboard::Buffer()));

  WebKeyboardEvent web_keyboard_event_insert(
      WebInputEvent::kRawKeyDown, modifier_key,
      WebInputEvent::kTimeStampForTesting);
  web_keyboard_event_insert.windows_key_code = 45;
  KeyboardEvent* key_event_insert =
      KeyboardEvent::Create(web_keyboard_event_insert, 0);
  ToWebPluginContainerBase(plugin_container_one_element.PluginContainer())
      ->HandleEvent(key_event_insert);
  EXPECT_EQ(WebString("x"), Platform::Current()->Clipboard()->ReadPlainText(
                                WebClipboard::Buffer()));
}

// A class to facilitate testing that events are correctly received by plugins.
class EventTestPlugin : public FakeWebPlugin {
 public:
  explicit EventTestPlugin(const WebPluginParams& params)
      : FakeWebPlugin(params), last_event_type_(WebInputEvent::kUndefined) {}

  WebInputEventResult HandleInputEvent(
      const WebCoalescedInputEvent& coalesced_event,
      WebCursorInfo&) override {
    const WebInputEvent& event = coalesced_event.Event();
    coalesced_event_count_ = coalesced_event.CoalescedEventSize();
    last_event_type_ = event.GetType();
    if (WebInputEvent::IsMouseEventType(event.GetType()) ||
        event.GetType() == WebInputEvent::kMouseWheel) {
      const WebMouseEvent& mouse_event =
          static_cast<const WebMouseEvent&>(event);
      last_event_location_ = IntPoint(mouse_event.PositionInWidget().x,
                                      mouse_event.PositionInWidget().y);
    } else if (WebInputEvent::IsTouchEventType(event.GetType())) {
      const WebTouchEvent& touch_event =
          static_cast<const WebTouchEvent&>(event);
      if (touch_event.touches_length == 1) {
        last_event_location_ = IntPoint(touch_event.touches[0].position.x,
                                        touch_event.touches[0].position.y);
      } else {
        last_event_location_ = IntPoint();
      }
    }

    return WebInputEventResult::kHandledSystem;
  }
  WebInputEvent::Type GetLastInputEventType() { return last_event_type_; }

  IntPoint GetLastEventLocation() { return last_event_location_; }

  void ClearLastEventType() { last_event_type_ = WebInputEvent::kUndefined; }

  size_t GetCoalescedEventCount() { return coalesced_event_count_; }

 private:
  size_t coalesced_event_count_;
  WebInputEvent::Type last_event_type_;
  IntPoint last_event_location_;
};

TEST_F(WebPluginContainerTest, GestureLongPressReachesPlugin) {
  RegisterMockedURL("plugin_container.html");
  CustomPluginWebFrameClient<EventTestPlugin>
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_container.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_container_one_element =
      web_view->MainFrame()->GetDocument().GetElementById(
          WebString::FromUTF8("translated-plugin"));
  WebPlugin* plugin = static_cast<WebPluginContainerBase*>(
                          plugin_container_one_element.PluginContainer())
                          ->Plugin();
  EventTestPlugin* test_plugin = static_cast<EventTestPlugin*>(plugin);

  WebGestureEvent event(WebInputEvent::kGestureLongPress,
                        WebInputEvent::kNoModifiers,
                        WebInputEvent::kTimeStampForTesting);
  event.source_device = kWebGestureDeviceTouchscreen;

  // First, send an event that doesn't hit the plugin to verify that the
  // plugin doesn't receive it.
  event.x = 0;
  event.y = 0;

  web_view->HandleInputEvent(WebCoalescedInputEvent(event));
  RunPendingTasks();

  EXPECT_EQ(WebInputEvent::kUndefined, test_plugin->GetLastInputEventType());

  // Next, send an event that does hit the plugin, and verify it does receive
  // it.
  WebRect rect = plugin_container_one_element.BoundsInViewport();
  event.x = rect.x + rect.width / 2;
  event.y = rect.y + rect.height / 2;

  web_view->HandleInputEvent(WebCoalescedInputEvent(event));
  RunPendingTasks();

  EXPECT_EQ(WebInputEvent::kGestureLongPress,
            test_plugin->GetLastInputEventType());
}

TEST_F(WebPluginContainerTest, MouseWheelEventTranslated) {
  RegisterMockedURL("plugin_container.html");
  CustomPluginWebFrameClient<EventTestPlugin>
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_container.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_container_one_element =
      web_view->MainFrame()->GetDocument().GetElementById(
          WebString::FromUTF8("translated-plugin"));
  WebPlugin* plugin = static_cast<WebPluginContainerBase*>(
                          plugin_container_one_element.PluginContainer())
                          ->Plugin();
  EventTestPlugin* test_plugin = static_cast<EventTestPlugin*>(plugin);

  WebMouseWheelEvent event(WebInputEvent::kMouseWheel,
                           WebInputEvent::kNoModifiers,
                           WebInputEvent::kTimeStampForTesting);

  WebRect rect = plugin_container_one_element.BoundsInViewport();
  event.SetPositionInWidget(rect.x + rect.width / 2, rect.y + rect.height / 2);

  web_view->HandleInputEvent(WebCoalescedInputEvent(event));
  RunPendingTasks();

  EXPECT_EQ(WebInputEvent::kMouseWheel, test_plugin->GetLastInputEventType());
  EXPECT_EQ(rect.width / 2, test_plugin->GetLastEventLocation().X());
  EXPECT_EQ(rect.height / 2, test_plugin->GetLastEventLocation().Y());
}

TEST_F(WebPluginContainerTest, TouchEventScrolled) {
  RegisterMockedURL("plugin_scroll.html");
  CustomPluginWebFrameClient<EventTestPlugin>
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_scroll.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();
  web_view->SmoothScroll(0, 200, 0);
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_container_one_element =
      web_view->MainFrame()->GetDocument().GetElementById(
          WebString::FromUTF8("scrolled-plugin"));
  plugin_container_one_element.PluginContainer()->RequestTouchEventType(
      WebPluginContainer::kTouchEventRequestTypeRaw);
  WebPlugin* plugin = static_cast<WebPluginContainerBase*>(
                          plugin_container_one_element.PluginContainer())
                          ->Plugin();
  EventTestPlugin* test_plugin = static_cast<EventTestPlugin*>(plugin);

  WebTouchEvent event(WebInputEvent::kTouchStart, WebInputEvent::kNoModifiers,
                      WebInputEvent::kTimeStampForTesting);
  event.touches_length = 1;
  WebRect rect = plugin_container_one_element.BoundsInViewport();
  event.touches[0].state = WebTouchPoint::kStatePressed;
  event.touches[0].position =
      WebFloatPoint(rect.x + rect.width / 2, rect.y + rect.height / 2);

  web_view->HandleInputEvent(WebCoalescedInputEvent(event));
  RunPendingTasks();

  EXPECT_EQ(WebInputEvent::kTouchStart, test_plugin->GetLastInputEventType());
  EXPECT_EQ(rect.width / 2, test_plugin->GetLastEventLocation().X());
  EXPECT_EQ(rect.height / 2, test_plugin->GetLastEventLocation().Y());
}

TEST_F(WebPluginContainerTest, TouchEventScrolledWithCoalescedTouches) {
  RegisterMockedURL("plugin_scroll.html");
  CustomPluginWebFrameClient<EventTestPlugin>
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_scroll.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();
  web_view->SmoothScroll(0, 200, 0);
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_container_one_element =
      web_view->MainFrame()->GetDocument().GetElementById(
          WebString::FromUTF8("scrolled-plugin"));
  plugin_container_one_element.PluginContainer()->RequestTouchEventType(
      WebPluginContainer::kTouchEventRequestTypeRaw);
  WebPlugin* plugin = static_cast<WebPluginContainerBase*>(
                          plugin_container_one_element.PluginContainer())
                          ->Plugin();
  EventTestPlugin* test_plugin = static_cast<EventTestPlugin*>(plugin);

  WebTouchEvent event(WebInputEvent::kTouchStart, WebInputEvent::kNoModifiers,
                      WebInputEvent::kTimeStampForTesting);
  WebRect rect = plugin_container_one_element.BoundsInViewport();
  event.touches_length = 1;
  event.touches[0].state = WebTouchPoint::kStatePressed;
  event.touches[0].position =
      WebFloatPoint(rect.x + rect.width / 2, rect.y + rect.height / 2);

  WebCoalescedInputEvent coalesced_event(event);

  WebTouchEvent c_event(WebInputEvent::kTouchMove, WebInputEvent::kNoModifiers,
                        WebInputEvent::kTimeStampForTesting);
  c_event.touches_length = 1;
  c_event.touches[0].state = WebTouchPoint::kStatePressed;
  c_event.touches[0].position =
      WebFloatPoint(rect.x + rect.width / 2 + 1, rect.y + rect.height / 2 + 1);

  coalesced_event.AddCoalescedEvent(c_event);
  c_event.touches[0].position =
      WebFloatPoint(rect.x + rect.width / 2 + 2, rect.y + rect.height / 2 + 2);
  coalesced_event.AddCoalescedEvent(c_event);

  web_view->HandleInputEvent(coalesced_event);
  RunPendingTasks();

  EXPECT_EQ(static_cast<const size_t>(3),
            test_plugin->GetCoalescedEventCount());
  EXPECT_EQ(WebInputEvent::kTouchStart, test_plugin->GetLastInputEventType());
  EXPECT_EQ(rect.width / 2, test_plugin->GetLastEventLocation().X());
  EXPECT_EQ(rect.height / 2, test_plugin->GetLastEventLocation().Y());
}

TEST_F(WebPluginContainerTest, MouseWheelEventScrolled) {
  RegisterMockedURL("plugin_scroll.html");
  CustomPluginWebFrameClient<EventTestPlugin>
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_scroll.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();
  web_view->SmoothScroll(0, 200, 0);
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_container_one_element =
      web_view->MainFrame()->GetDocument().GetElementById(
          WebString::FromUTF8("scrolled-plugin"));
  plugin_container_one_element.PluginContainer()->RequestTouchEventType(
      WebPluginContainer::kTouchEventRequestTypeRaw);
  WebPlugin* plugin = static_cast<WebPluginContainerBase*>(
                          plugin_container_one_element.PluginContainer())
                          ->Plugin();
  EventTestPlugin* test_plugin = static_cast<EventTestPlugin*>(plugin);

  WebMouseWheelEvent event(WebInputEvent::kMouseWheel,
                           WebInputEvent::kNoModifiers,
                           WebInputEvent::kTimeStampForTesting);

  WebRect rect = plugin_container_one_element.BoundsInViewport();
  event.SetPositionInWidget(rect.x + rect.width / 2, rect.y + rect.height / 2);

  web_view->HandleInputEvent(WebCoalescedInputEvent(event));
  RunPendingTasks();

  EXPECT_EQ(WebInputEvent::kMouseWheel, test_plugin->GetLastInputEventType());
  EXPECT_EQ(rect.width / 2, test_plugin->GetLastEventLocation().X());
  EXPECT_EQ(rect.height / 2, test_plugin->GetLastEventLocation().Y());
}

TEST_F(WebPluginContainerTest, MouseEventScrolled) {
  RegisterMockedURL("plugin_scroll.html");
  CustomPluginWebFrameClient<EventTestPlugin>
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_scroll.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();
  web_view->SmoothScroll(0, 200, 0);
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_container_one_element =
      web_view->MainFrame()->GetDocument().GetElementById(
          WebString::FromUTF8("scrolled-plugin"));
  plugin_container_one_element.PluginContainer()->RequestTouchEventType(
      WebPluginContainer::kTouchEventRequestTypeRaw);
  WebPlugin* plugin = static_cast<WebPluginContainerBase*>(
                          plugin_container_one_element.PluginContainer())
                          ->Plugin();
  EventTestPlugin* test_plugin = static_cast<EventTestPlugin*>(plugin);

  WebMouseEvent event(WebInputEvent::kMouseMove, WebInputEvent::kNoModifiers,
                      WebInputEvent::kTimeStampForTesting);

  WebRect rect = plugin_container_one_element.BoundsInViewport();
  event.SetPositionInWidget(rect.x + rect.width / 2, rect.y + rect.height / 2);

  web_view->HandleInputEvent(WebCoalescedInputEvent(event));
  RunPendingTasks();

  EXPECT_EQ(WebInputEvent::kMouseMove, test_plugin->GetLastInputEventType());
  EXPECT_EQ(rect.width / 2, test_plugin->GetLastEventLocation().X());
  EXPECT_EQ(rect.height / 2, test_plugin->GetLastEventLocation().Y());
}

TEST_F(WebPluginContainerTest, MouseEventZoomed) {
  RegisterMockedURL("plugin_scroll.html");
  CustomPluginWebFrameClient<EventTestPlugin>
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_scroll.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->SetPageScaleFactor(2);
  web_view->SmoothScroll(0, 300, 0);
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_container_one_element =
      web_view->MainFrame()->GetDocument().GetElementById(
          WebString::FromUTF8("scrolled-plugin"));
  plugin_container_one_element.PluginContainer()->RequestTouchEventType(
      WebPluginContainer::kTouchEventRequestTypeRaw);
  WebPlugin* plugin = static_cast<WebPluginContainerBase*>(
                          plugin_container_one_element.PluginContainer())
                          ->Plugin();
  EventTestPlugin* test_plugin = static_cast<EventTestPlugin*>(plugin);

  WebMouseEvent event(WebInputEvent::kMouseMove, WebInputEvent::kNoModifiers,
                      WebInputEvent::kTimeStampForTesting);

  WebRect rect = plugin_container_one_element.BoundsInViewport();
  event.SetPositionInWidget(rect.x + rect.width / 2, rect.y + rect.height / 2);

  web_view->HandleInputEvent(WebCoalescedInputEvent(event));
  RunPendingTasks();

  // rect.width/height divided by 4 because the rect is in viewport bounds and
  // there is a scale of 2 set.
  EXPECT_EQ(WebInputEvent::kMouseMove, test_plugin->GetLastInputEventType());
  EXPECT_EQ(rect.width / 4, test_plugin->GetLastEventLocation().X());
  EXPECT_EQ(rect.height / 4, test_plugin->GetLastEventLocation().Y());
}

TEST_F(WebPluginContainerTest, MouseWheelEventZoomed) {
  RegisterMockedURL("plugin_scroll.html");
  CustomPluginWebFrameClient<EventTestPlugin>
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_scroll.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->SetPageScaleFactor(2);
  web_view->SmoothScroll(0, 300, 0);
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_container_one_element =
      web_view->MainFrame()->GetDocument().GetElementById(
          WebString::FromUTF8("scrolled-plugin"));
  plugin_container_one_element.PluginContainer()->RequestTouchEventType(
      WebPluginContainer::kTouchEventRequestTypeRaw);
  WebPlugin* plugin = static_cast<WebPluginContainerBase*>(
                          plugin_container_one_element.PluginContainer())
                          ->Plugin();
  EventTestPlugin* test_plugin = static_cast<EventTestPlugin*>(plugin);

  WebMouseWheelEvent event(WebInputEvent::kMouseWheel,
                           WebInputEvent::kNoModifiers,
                           WebInputEvent::kTimeStampForTesting);

  WebRect rect = plugin_container_one_element.BoundsInViewport();
  event.SetPositionInWidget(rect.x + rect.width / 2, rect.y + rect.height / 2);

  web_view->HandleInputEvent(WebCoalescedInputEvent(event));
  RunPendingTasks();

  // rect.width/height divided by 4 because the rect is in viewport bounds and
  // there is a scale of 2 set.
  EXPECT_EQ(WebInputEvent::kMouseWheel, test_plugin->GetLastInputEventType());
  EXPECT_EQ(rect.width / 4, test_plugin->GetLastEventLocation().X());
  EXPECT_EQ(rect.height / 4, test_plugin->GetLastEventLocation().Y());
}

TEST_F(WebPluginContainerTest, TouchEventZoomed) {
  RegisterMockedURL("plugin_scroll.html");
  CustomPluginWebFrameClient<EventTestPlugin>
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_scroll.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->SetPageScaleFactor(2);
  web_view->SmoothScroll(0, 300, 0);
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_container_one_element =
      web_view->MainFrame()->GetDocument().GetElementById(
          WebString::FromUTF8("scrolled-plugin"));
  plugin_container_one_element.PluginContainer()->RequestTouchEventType(
      WebPluginContainer::kTouchEventRequestTypeRaw);
  WebPlugin* plugin = static_cast<WebPluginContainerBase*>(
                          plugin_container_one_element.PluginContainer())
                          ->Plugin();
  EventTestPlugin* test_plugin = static_cast<EventTestPlugin*>(plugin);

  WebTouchEvent event(WebInputEvent::kTouchStart, WebInputEvent::kNoModifiers,
                      WebInputEvent::kTimeStampForTesting);
  event.touches_length = 1;
  WebRect rect = plugin_container_one_element.BoundsInViewport();

  event.touches[0].state = WebTouchPoint::kStatePressed;
  event.touches[0].position =
      WebFloatPoint(rect.x + rect.width / 2, rect.y + rect.height / 2);

  web_view->HandleInputEvent(WebCoalescedInputEvent(event));
  RunPendingTasks();

  // rect.width/height divided by 4 because the rect is in viewport bounds and
  // there is a scale of 2 set.
  EXPECT_EQ(WebInputEvent::kTouchStart, test_plugin->GetLastInputEventType());
  EXPECT_EQ(rect.width / 4, test_plugin->GetLastEventLocation().X());
  EXPECT_EQ(rect.height / 4, test_plugin->GetLastEventLocation().Y());
}

// Verify that isRectTopmost returns false when the document is detached.
TEST_F(WebPluginContainerTest, IsRectTopmostTest) {
  RegisterMockedURL("plugin_container.html");
  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_container.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebPluginContainerBase* plugin_container_impl =
      ToWebPluginContainerBase(GetWebPluginContainer(
          web_view, WebString::FromUTF8("translated-plugin")));
  plugin_container_impl->SetFrameRect(IntRect(0, 0, 300, 300));

  WebRect rect = plugin_container_impl->GetElement().BoundsInViewport();
  EXPECT_TRUE(plugin_container_impl->IsRectTopmost(rect));

  // Cause the plugin's frame to be detached.
  web_view_helper.Reset();

  EXPECT_FALSE(plugin_container_impl->IsRectTopmost(rect));
}

#define EXPECT_RECT_EQ(expected, actual)                \
  do {                                                  \
    const IntRect& actual_rect = actual;                \
    EXPECT_EQ(expected.X(), actual_rect.X());           \
    EXPECT_EQ(expected.Y(), actual_rect.Y());           \
    EXPECT_EQ(expected.Width(), actual_rect.Width());   \
    EXPECT_EQ(expected.Height(), actual_rect.Height()); \
  } while (false)

TEST_F(WebPluginContainerTest, ClippedRectsForIframedElement) {
  RegisterMockedURL("plugin_container.html");
  RegisterMockedURL("plugin_containing_page.html");

  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_containing_page.html", true,
      &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_element =
      web_view->MainFrame()->FirstChild()->GetDocument().GetElementById(
          "translated-plugin");
  WebPluginContainerBase* plugin_container_impl =
      ToWebPluginContainerBase(plugin_element.PluginContainer());

  DCHECK(plugin_container_impl);

  IntRect window_rect, clip_rect, unobscured_rect;
  Vector<IntRect> cut_out_rects;
  CalculateGeometry(plugin_container_impl, window_rect, clip_rect,
                    unobscured_rect);
  EXPECT_RECT_EQ(IntRect(20, 220, 40, 40), window_rect);
  EXPECT_RECT_EQ(IntRect(0, 0, 40, 40), clip_rect);
  EXPECT_RECT_EQ(IntRect(0, 0, 40, 40), unobscured_rect);

  // Cause the plugin's frame to be detached.
  web_view_helper.Reset();
}

TEST_F(WebPluginContainerTest, ClippedRectsForSubpixelPositionedPlugin) {
  RegisterMockedURL("plugin_container.html");

  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper.
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_container.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_element =
      web_view->MainFrame()->GetDocument().GetElementById(
          "subpixel-positioned-plugin");
  WebPluginContainerBase* plugin_container_impl =
      ToWebPluginContainerBase(plugin_element.PluginContainer());

  DCHECK(plugin_container_impl);

  IntRect window_rect, clip_rect, unobscured_rect;
  Vector<IntRect> cut_out_rects;

  CalculateGeometry(plugin_container_impl, window_rect, clip_rect,
                    unobscured_rect);
  EXPECT_RECT_EQ(IntRect(0, 0, 40, 40), window_rect);
  EXPECT_RECT_EQ(IntRect(0, 0, 40, 40), clip_rect);
  EXPECT_RECT_EQ(IntRect(0, 0, 40, 40), unobscured_rect);

  // Cause the plugin's frame to be detached.
  web_view_helper.Reset();
}

TEST_F(WebPluginContainerTest, TopmostAfterDetachTest) {
  static WebRect topmost_rect(10, 10, 40, 40);

  // Plugin that checks isRectTopmost in destroy().
  class TopmostPlugin : public FakeWebPlugin {
   public:
    explicit TopmostPlugin(const WebPluginParams& params)
        : FakeWebPlugin(params) {}

    bool IsRectTopmost() { return Container()->IsRectTopmost(topmost_rect); }

    void Destroy() override {
      // In destroy, IsRectTopmost is no longer valid.
      EXPECT_FALSE(Container()->IsRectTopmost(topmost_rect));
      FakeWebPlugin::Destroy();
    }
  };

  RegisterMockedURL("plugin_container.html");
  // The client must outlive WebViewHelper.
  CustomPluginWebFrameClient<TopmostPlugin> plugin_web_frame_client;
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_container.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebPluginContainerBase* plugin_container_impl =
      ToWebPluginContainerBase(GetWebPluginContainer(
          web_view, WebString::FromUTF8("translated-plugin")));
  plugin_container_impl->SetFrameRect(IntRect(0, 0, 300, 300));

  EXPECT_TRUE(plugin_container_impl->IsRectTopmost(topmost_rect));

  TopmostPlugin* test_plugin =
      static_cast<TopmostPlugin*>(plugin_container_impl->Plugin());
  EXPECT_TRUE(test_plugin->IsRectTopmost());

  // Cause the plugin's frame to be detached.
  web_view_helper.Reset();

  EXPECT_FALSE(plugin_container_impl->IsRectTopmost(topmost_rect));
}

namespace {

class CompositedPlugin : public FakeWebPlugin {
 public:
  explicit CompositedPlugin(const WebPluginParams& params)
      : FakeWebPlugin(params),
        layer_(Platform::Current()->CompositorSupport()->CreateLayer()) {}

  WebLayer* GetWebLayer() const { return layer_.get(); }

  // WebPlugin

  bool Initialize(WebPluginContainer* container) override {
    if (!FakeWebPlugin::Initialize(container))
      return false;
    container->SetWebLayer(layer_.get());
    return true;
  }

  void Destroy() override {
    Container()->SetWebLayer(nullptr);
    FakeWebPlugin::Destroy();
  }

 private:
  std::unique_ptr<WebLayer> layer_;
};

}  // namespace

TEST_F(WebPluginContainerTest, CompositedPluginSPv2) {
  ScopedSlimmingPaintV2ForTest enable_s_pv2(true);
  RegisterMockedURL("plugin.html");
  CustomPluginWebFrameClient<CompositedPlugin> web_frame_client;
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebView* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin.html", true, &web_frame_client);
  ASSERT_TRUE(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(800, 600));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebPluginContainerBase* container = static_cast<WebPluginContainerBase*>(
      GetWebPluginContainer(web_view, WebString::FromUTF8("plugin")));
  ASSERT_TRUE(container);
  Element* element = static_cast<Element*>(container->GetElement());
  const auto* plugin =
      static_cast<const CompositedPlugin*>(container->Plugin());

  std::unique_ptr<PaintController> paint_controller = PaintController::Create();
  PropertyTreeState property_tree_state(TransformPaintPropertyNode::Root(),
                                        ClipPaintPropertyNode::Root(),
                                        EffectPaintPropertyNode::Root());
  PaintChunkProperties properties(property_tree_state);

  paint_controller->UpdateCurrentPaintChunkProperties(nullptr, properties);
  GraphicsContext graphics_context(*paint_controller);
  container->Paint(graphics_context, CullRect(IntRect(10, 10, 400, 300)));
  paint_controller->CommitNewDisplayItems();

  const auto& display_items =
      paint_controller->GetPaintArtifact().GetDisplayItemList();
  ASSERT_EQ(1u, display_items.size());
  EXPECT_EQ(element->GetLayoutObject(), &display_items[0].Client());
  ASSERT_EQ(DisplayItem::kForeignLayerPlugin, display_items[0].GetType());
  const auto& foreign_layer_display_item =
      static_cast<const ForeignLayerDisplayItem&>(display_items[0]);
  EXPECT_EQ(plugin->GetWebLayer()->CcLayer(),
            foreign_layer_display_item.GetLayer());
}

TEST_F(WebPluginContainerTest, NeedsWheelEvents) {
  RegisterMockedURL("plugin_container.html");
  TestPluginWebFrameClient
      plugin_web_frame_client;  // Must outlive webViewHelper
  FrameTestHelpers::WebViewHelper web_view_helper;
  WebViewBase* web_view = web_view_helper.InitializeAndLoad(
      base_url_ + "plugin_container.html", true, &plugin_web_frame_client);
  DCHECK(web_view);
  web_view->GetSettings()->SetPluginsEnabled(true);
  web_view->Resize(WebSize(300, 300));
  web_view->UpdateAllLifecyclePhases();
  RunPendingTasks();

  WebElement plugin_container_one_element =
      web_view->MainFrame()->GetDocument().GetElementById(
          WebString::FromUTF8("translated-plugin"));
  plugin_container_one_element.PluginContainer()->SetWantsWheelEvents(true);

  RunPendingTasks();
  EXPECT_TRUE(web_view->GetPage()->GetEventHandlerRegistry().HasEventHandlers(
      EventHandlerRegistry::kWheelEventBlocking));
}

}  // namespace blink
