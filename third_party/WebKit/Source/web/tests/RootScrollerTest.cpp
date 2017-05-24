// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bindings/core/v8/NodeOrString.h"
#include "core/dom/ClientRect.h"
#include "core/frame/BrowserControls.h"
#include "core/frame/FrameView.h"
#include "core/frame/RootFrameViewport.h"
#include "core/frame/VisualViewport.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/layout/LayoutBox.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/compositing/CompositedLayerMapping.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/page/Page.h"
#include "core/page/scrolling/RootScrollerController.h"
#include "core/page/scrolling/TopDocumentRootScrollerController.h"
#include "core/paint/PaintLayer.h"
#include "core/paint/PaintLayerScrollableArea.h"
#include "platform/testing/URLTestHelpers.h"
#include "platform/testing/UnitTestHelpers.h"
#include "platform/wtf/Vector.h"
#include "public/platform/Platform.h"
#include "public/platform/WebCoalescedInputEvent.h"
#include "public/platform/WebURLLoaderMockFactory.h"
#include "public/web/WebConsoleMessage.h"
#include "public/web/WebHitTestResult.h"
#include "public/web/WebRemoteFrame.h"
#include "public/web/WebScriptSource.h"
#include "public/web/WebSettings.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebRemoteFrameImpl.h"
#include "web/tests/FrameTestHelpers.h"

using blink::testing::RunPendingTasks;
using testing::Mock;

namespace blink {

namespace {

class RootScrollerTest : public ::testing::Test {
 public:
  RootScrollerTest() : base_url_("http://www.test.com/") {
    RegisterMockedHttpURLLoad("overflow-scrolling.html");
    RegisterMockedHttpURLLoad("root-scroller.html");
    RegisterMockedHttpURLLoad("root-scroller-rotation.html");
    RegisterMockedHttpURLLoad("root-scroller-iframe.html");
    RegisterMockedHttpURLLoad("root-scroller-child.html");
  }

  ~RootScrollerTest() override {
    features_backup_.restore();
    Platform::Current()
        ->GetURLLoaderMockFactory()
        ->UnregisterAllURLsAndClearMemoryCache();
  }

  WebViewBase* Initialize(const std::string& page_name,
                          FrameTestHelpers::TestWebViewClient* client) {
    return InitializeInternal(base_url_ + page_name, client);
  }

  WebViewBase* Initialize(const std::string& page_name) {
    return InitializeInternal(base_url_ + page_name, &client_);
  }

  WebViewBase* Initialize() {
    return InitializeInternal("about:blank", &client_);
  }

  static void ConfigureSettings(WebSettings* settings) {
    settings->SetJavaScriptEnabled(true);
    settings->SetAcceleratedCompositingEnabled(true);
    settings->SetPreferCompositingToLCDTextEnabled(true);
    // Android settings.
    settings->SetViewportEnabled(true);
    settings->SetViewportMetaEnabled(true);
    settings->SetShrinksViewportContentToFit(true);
    settings->SetMainFrameResizesAreOrientationChanges(true);
  }

  void RegisterMockedHttpURLLoad(const std::string& file_name) {
    URLTestHelpers::RegisterMockedURLLoadFromBase(
        WebString::FromUTF8(base_url_), testing::WebTestDataPath(),
        WebString::FromUTF8(file_name));
  }

  void ExecuteScript(const WebString& code) {
    ExecuteScript(code, *MainWebFrame());
  }

  void ExecuteScript(const WebString& code, WebLocalFrame& frame) {
    frame.ExecuteScript(WebScriptSource(code));
    frame.View()->UpdateAllLifecyclePhases();
    RunPendingTasks();
  }

  WebViewBase* GetWebView() const { return helper_.WebView(); }

  Page& GetPage() const { return *helper_.WebView()->GetPage(); }

  LocalFrame* MainFrame() const {
    return GetWebView()->MainFrameImpl()->GetFrame();
  }

  WebLocalFrame* MainWebFrame() const { return GetWebView()->MainFrameImpl(); }

  FrameView* MainFrameView() const {
    return GetWebView()->MainFrameImpl()->GetFrame()->View();
  }

  VisualViewport& GetVisualViewport() const {
    return GetPage().GetVisualViewport();
  }

  BrowserControls& GetBrowserControls() const {
    return GetPage().GetBrowserControls();
  }

  Node* EffectiveRootScroller(Document* doc) const {
    return &doc->GetRootScrollerController().EffectiveRootScroller();
  }

  WebCoalescedInputEvent GenerateTouchGestureEvent(WebInputEvent::Type type,
                                                   int delta_x = 0,
                                                   int delta_y = 0) {
    return GenerateGestureEvent(type, kWebGestureDeviceTouchscreen, delta_x,
                                delta_y);
  }

  WebCoalescedInputEvent GenerateWheelGestureEvent(WebInputEvent::Type type,
                                                   int delta_x = 0,
                                                   int delta_y = 0) {
    return GenerateGestureEvent(type, kWebGestureDeviceTouchpad, delta_x,
                                delta_y);
  }

 protected:
  WebCoalescedInputEvent GenerateGestureEvent(WebInputEvent::Type type,
                                              WebGestureDevice device,
                                              int delta_x,
                                              int delta_y) {
    WebGestureEvent event(type, WebInputEvent::kNoModifiers,
                          WebInputEvent::kTimeStampForTesting);
    event.source_device = device;
    event.x = 100;
    event.y = 100;
    if (type == WebInputEvent::kGestureScrollUpdate) {
      event.data.scroll_update.delta_x = delta_x;
      event.data.scroll_update.delta_y = delta_y;
    }
    return WebCoalescedInputEvent(event);
  }

  WebViewBase* InitializeInternal(const std::string& url,
                                  FrameTestHelpers::TestWebViewClient* client) {
    RuntimeEnabledFeatures::setSetRootScrollerEnabled(true);

    helper_.InitializeAndLoad(url, true, nullptr, client, nullptr,
                              &ConfigureSettings);

    // Initialize browser controls to be shown.
    GetWebView()->ResizeWithBrowserControls(IntSize(400, 400), 50, true);
    GetWebView()->GetBrowserControls().SetShownRatio(1);

    MainFrameView()->UpdateAllLifecyclePhases();

    return GetWebView();
  }

  std::string base_url_;
  FrameTestHelpers::TestWebViewClient client_;
  FrameTestHelpers::WebViewHelper helper_;
  RuntimeEnabledFeatures::Backup features_backup_;
};

// Test that no root scroller element is set if setRootScroller isn't called on
// any elements. The document Node should be the default effective root
// scroller.
TEST_F(RootScrollerTest, TestDefaultRootScroller) {
  Initialize("overflow-scrolling.html");

  RootScrollerController& controller =
      MainFrame()->GetDocument()->GetRootScrollerController();

  ASSERT_EQ(nullptr, MainFrame()->GetDocument()->rootScroller());

  EXPECT_EQ(MainFrame()->GetDocument(),
            EffectiveRootScroller(MainFrame()->GetDocument()));

  Element* html_element = MainFrame()->GetDocument()->documentElement();
  EXPECT_TRUE(controller.ScrollsViewport(*html_element));
}

// Make sure that replacing the documentElement doesn't change the effective
// root scroller when no root scroller is set.
TEST_F(RootScrollerTest, defaultEffectiveRootScrollerIsDocumentNode) {
  Initialize("root-scroller.html");

  Document* document = MainFrame()->GetDocument();
  Element* iframe = document->createElement("iframe");

  EXPECT_EQ(MainFrame()->GetDocument(),
            EffectiveRootScroller(MainFrame()->GetDocument()));

  // Replace the documentElement with the iframe. The effectiveRootScroller
  // should remain the same.
  NonThrowableExceptionState non_throw;
  HeapVector<NodeOrString> nodes;
  nodes.push_back(NodeOrString::fromNode(iframe));
  document->documentElement()->ReplaceWith(nodes, non_throw);

  MainFrameView()->UpdateAllLifecyclePhases();

  EXPECT_EQ(MainFrame()->GetDocument(),
            EffectiveRootScroller(MainFrame()->GetDocument()));
}

class OverscrollTestWebViewClient : public FrameTestHelpers::TestWebViewClient {
 public:
  MOCK_METHOD4(DidOverscroll,
               void(const WebFloatSize&,
                    const WebFloatSize&,
                    const WebFloatPoint&,
                    const WebFloatSize&));
};

// Tests that setting an element as the root scroller causes it to control url
// bar hiding and overscroll.
TEST_F(RootScrollerTest, TestSetRootScroller) {
  OverscrollTestWebViewClient client;
  Initialize("root-scroller.html", &client);

  Element* container = MainFrame()->GetDocument()->getElementById("container");
  MainFrame()->GetDocument()->setRootScroller(container);
  ASSERT_EQ(container, MainFrame()->GetDocument()->rootScroller());

  // Content is 1000x1000, WebView size is 400x400 but hiding the top controls
  // makes it 400x450 so max scroll is 550px.
  double maximum_scroll = 550;

  GetWebView()->HandleInputEvent(
      GenerateTouchGestureEvent(WebInputEvent::kGestureScrollBegin));

  {
    // Scrolling over the #container DIV should cause the browser controls to
    // hide.
    EXPECT_FLOAT_EQ(1, GetBrowserControls().ShownRatio());
    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollUpdate, 0,
                                  -GetBrowserControls().Height()));
    EXPECT_FLOAT_EQ(0, GetBrowserControls().ShownRatio());
  }

  {
    // Make sure we're actually scrolling the DIV and not the FrameView.
    GetWebView()->HandleInputEvent(GenerateTouchGestureEvent(
        WebInputEvent::kGestureScrollUpdate, 0, -100));
    EXPECT_FLOAT_EQ(100, container->scrollTop());
    EXPECT_FLOAT_EQ(0, MainFrameView()->GetScrollOffset().Height());
  }

  {
    // Scroll 50 pixels past the end. Ensure we report the 50 pixels as
    // overscroll.
    EXPECT_CALL(client, DidOverscroll(WebFloatSize(0, 50), WebFloatSize(0, 50),
                                      WebFloatPoint(100, 100), WebFloatSize()));
    GetWebView()->HandleInputEvent(GenerateTouchGestureEvent(
        WebInputEvent::kGestureScrollUpdate, 0, -500));
    EXPECT_FLOAT_EQ(maximum_scroll, container->scrollTop());
    EXPECT_FLOAT_EQ(0, MainFrameView()->GetScrollOffset().Height());
    Mock::VerifyAndClearExpectations(&client);
  }

  {
    // Continue the gesture overscroll.
    EXPECT_CALL(client, DidOverscroll(WebFloatSize(0, 20), WebFloatSize(0, 70),
                                      WebFloatPoint(100, 100), WebFloatSize()));
    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollUpdate, 0, -20));
    EXPECT_FLOAT_EQ(maximum_scroll, container->scrollTop());
    EXPECT_FLOAT_EQ(0, MainFrameView()->GetScrollOffset().Height());
    Mock::VerifyAndClearExpectations(&client);
  }

  GetWebView()->HandleInputEvent(
      GenerateTouchGestureEvent(WebInputEvent::kGestureScrollEnd));

  {
    // Make sure a new gesture scroll still won't scroll the frameview and
    // overscrolls.
    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollBegin));

    EXPECT_CALL(client, DidOverscroll(WebFloatSize(0, 30), WebFloatSize(0, 30),
                                      WebFloatPoint(100, 100), WebFloatSize()));
    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollUpdate, 0, -30));
    EXPECT_FLOAT_EQ(maximum_scroll, container->scrollTop());
    EXPECT_FLOAT_EQ(0, MainFrameView()->GetScrollOffset().Height());
    Mock::VerifyAndClearExpectations(&client);

    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollEnd));
  }

  {
    // Scrolling up should show the browser controls.
    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollBegin));

    EXPECT_FLOAT_EQ(0, GetBrowserControls().ShownRatio());
    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollUpdate, 0, 30));
    EXPECT_FLOAT_EQ(0.6, GetBrowserControls().ShownRatio());

    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollEnd));
  }

  // Reset manually to avoid lifetime issues with custom WebViewClient.
  helper_.Reset();
}

// Tests that removing the element that is the root scroller from the DOM tree
// doesn't remove it as the root scroller but it does change the effective root
// scroller.
TEST_F(RootScrollerTest, TestRemoveRootScrollerFromDom) {
  Initialize("root-scroller.html");

  ASSERT_EQ(nullptr, MainFrame()->GetDocument()->rootScroller());

  Element* container = MainFrame()->GetDocument()->getElementById("container");
  MainFrame()->GetDocument()->setRootScroller(container);

  EXPECT_EQ(container, MainFrame()->GetDocument()->rootScroller());
  EXPECT_EQ(container, EffectiveRootScroller(MainFrame()->GetDocument()));

  MainFrame()->GetDocument()->body()->RemoveChild(container);
  MainFrameView()->UpdateAllLifecyclePhases();

  EXPECT_EQ(container, MainFrame()->GetDocument()->rootScroller());
  EXPECT_NE(container, EffectiveRootScroller(MainFrame()->GetDocument()));
}

// Tests that setting an element that isn't a valid scroller as the root
// scroller doesn't change the effective root scroller.
TEST_F(RootScrollerTest, TestSetRootScrollerOnInvalidElement) {
  Initialize("root-scroller.html");

  {
    // Set to a non-block element. Should be rejected and a console message
    // logged.
    Element* element = MainFrame()->GetDocument()->getElementById("nonBlock");
    MainFrame()->GetDocument()->setRootScroller(element);
    MainFrameView()->UpdateAllLifecyclePhases();
    EXPECT_EQ(element, MainFrame()->GetDocument()->rootScroller());
    EXPECT_NE(element, EffectiveRootScroller(MainFrame()->GetDocument()));
  }

  {
    // Set to an element with no size.
    Element* element = MainFrame()->GetDocument()->getElementById("empty");
    MainFrame()->GetDocument()->setRootScroller(element);
    MainFrameView()->UpdateAllLifecyclePhases();
    EXPECT_EQ(element, MainFrame()->GetDocument()->rootScroller());
    EXPECT_NE(element, EffectiveRootScroller(MainFrame()->GetDocument()));
  }
}

// Test that the effective root scroller resets to the document Node when the
// current root scroller element becomes invalid as a scroller.
TEST_F(RootScrollerTest, TestRootScrollerBecomesInvalid) {
  Initialize("root-scroller.html");

  Element* container = MainFrame()->GetDocument()->getElementById("container");

  ASSERT_EQ(nullptr, MainFrame()->GetDocument()->rootScroller());
  ASSERT_EQ(MainFrame()->GetDocument(),
            EffectiveRootScroller(MainFrame()->GetDocument()));

  {
    MainFrame()->GetDocument()->setRootScroller(container);
    MainFrameView()->UpdateAllLifecyclePhases();

    EXPECT_EQ(container, MainFrame()->GetDocument()->rootScroller());
    EXPECT_EQ(container, EffectiveRootScroller(MainFrame()->GetDocument()));

    ExecuteScript(
        "document.querySelector('#container').style.display = 'inline'");
    MainFrameView()->UpdateAllLifecyclePhases();

    EXPECT_EQ(container, MainFrame()->GetDocument()->rootScroller());
    EXPECT_EQ(MainFrame()->GetDocument(),
              EffectiveRootScroller(MainFrame()->GetDocument()));
  }

  ExecuteScript("document.querySelector('#container').style.display = 'block'");
  MainFrame()->GetDocument()->setRootScroller(nullptr);
  MainFrameView()->UpdateAllLifecyclePhases();
  EXPECT_EQ(nullptr, MainFrame()->GetDocument()->rootScroller());
  EXPECT_EQ(MainFrame()->GetDocument(),
            EffectiveRootScroller(MainFrame()->GetDocument()));

  {
    MainFrame()->GetDocument()->setRootScroller(container);
    MainFrameView()->UpdateAllLifecyclePhases();

    EXPECT_EQ(container, MainFrame()->GetDocument()->rootScroller());
    EXPECT_EQ(container, EffectiveRootScroller(MainFrame()->GetDocument()));

    ExecuteScript("document.querySelector('#container').style.width = '98%'");
    MainFrameView()->UpdateAllLifecyclePhases();

    EXPECT_EQ(container, MainFrame()->GetDocument()->rootScroller());
    EXPECT_EQ(MainFrame()->GetDocument(),
              EffectiveRootScroller(MainFrame()->GetDocument()));
  }
}

// Tests that setting the root scroller of the top document to an element that
// belongs to a nested document works.
TEST_F(RootScrollerTest, TestSetRootScrollerOnElementInIframe) {
  Initialize("root-scroller-iframe.html");

  ASSERT_EQ(nullptr, MainFrame()->GetDocument()->rootScroller());

  {
    // Trying to set an element from a nested document should fail.
    HTMLFrameOwnerElement* iframe = ToHTMLFrameOwnerElement(
        MainFrame()->GetDocument()->getElementById("iframe"));
    Element* inner_container =
        iframe->contentDocument()->getElementById("container");

    MainFrame()->GetDocument()->setRootScroller(inner_container);
    MainFrameView()->UpdateAllLifecyclePhases();

    EXPECT_EQ(inner_container, MainFrame()->GetDocument()->rootScroller());
    EXPECT_EQ(inner_container,
              EffectiveRootScroller(MainFrame()->GetDocument()));
  }

  {
    // Setting the iframe itself should also work.
    HTMLFrameOwnerElement* iframe = ToHTMLFrameOwnerElement(
        MainFrame()->GetDocument()->getElementById("iframe"));

    MainFrame()->GetDocument()->setRootScroller(iframe);
    MainFrameView()->UpdateAllLifecyclePhases();

    EXPECT_EQ(iframe, MainFrame()->GetDocument()->rootScroller());
    EXPECT_EQ(iframe, EffectiveRootScroller(MainFrame()->GetDocument()));
  }
}

// Tests that setting a valid element as the root scroller on a document within
// an iframe works as expected.
TEST_F(RootScrollerTest, TestRootScrollerWithinIframe) {
  Initialize("root-scroller-iframe.html");

  ASSERT_EQ(nullptr, MainFrame()->GetDocument()->rootScroller());

  {
    HTMLFrameOwnerElement* iframe = ToHTMLFrameOwnerElement(
        MainFrame()->GetDocument()->getElementById("iframe"));

    EXPECT_EQ(iframe->contentDocument(),
              EffectiveRootScroller(iframe->contentDocument()));

    Element* inner_container =
        iframe->contentDocument()->getElementById("container");
    iframe->contentDocument()->setRootScroller(inner_container);
    MainFrameView()->UpdateAllLifecyclePhases();

    EXPECT_EQ(inner_container, iframe->contentDocument()->rootScroller());
    EXPECT_EQ(inner_container,
              EffectiveRootScroller(iframe->contentDocument()));
  }
}

// Tests that setting an iframe as the root scroller makes the iframe the
// effective root scroller in the parent frame.
TEST_F(RootScrollerTest, SetRootScrollerIframeBecomesEffective) {
  Initialize("root-scroller-iframe.html");
  ASSERT_EQ(nullptr, MainFrame()->GetDocument()->rootScroller());

  {
    NonThrowableExceptionState non_throw;

    // Try to set the root scroller in the main frame to be the iframe
    // element.
    HTMLFrameOwnerElement* iframe = ToHTMLFrameOwnerElement(
        MainFrame()->GetDocument()->getElementById("iframe"));

    MainFrame()->GetDocument()->setRootScroller(iframe, non_throw);

    EXPECT_EQ(iframe, MainFrame()->GetDocument()->rootScroller());
    EXPECT_EQ(iframe, EffectiveRootScroller(MainFrame()->GetDocument()));

    Element* container = iframe->contentDocument()->getElementById("container");

    iframe->contentDocument()->setRootScroller(container, non_throw);

    EXPECT_EQ(container, iframe->contentDocument()->rootScroller());
    EXPECT_EQ(container, EffectiveRootScroller(iframe->contentDocument()));
    EXPECT_EQ(iframe, MainFrame()->GetDocument()->rootScroller());
    EXPECT_EQ(iframe, EffectiveRootScroller(MainFrame()->GetDocument()));
  }
}

// Tests that the global root scroller is correctly calculated when getting the
// root scroller layer and that the viewport apply scroll is set on it.
TEST_F(RootScrollerTest, SetRootScrollerIframeUsesCorrectLayerAndCallback) {
  // TODO(bokan): The expectation and actual in the checks here are backwards.
  Initialize("root-scroller-iframe.html");
  ASSERT_EQ(nullptr, MainFrame()->GetDocument()->rootScroller());

  HTMLFrameOwnerElement* iframe = ToHTMLFrameOwnerElement(
      MainFrame()->GetDocument()->getElementById("iframe"));
  Element* container = iframe->contentDocument()->getElementById("container");

  const TopDocumentRootScrollerController& main_controller =
      MainFrame()->GetDocument()->GetPage()->GlobalRootScrollerController();

  NonThrowableExceptionState non_throw;

  // No root scroller set, the documentElement should be the effective root
  // and the main FrameView's scroll layer should be the layer to use.
  {
    EXPECT_EQ(
        main_controller.RootScrollerLayer(),
        MainFrameView()->LayoutViewportScrollableArea()->LayerForScrolling());
    EXPECT_TRUE(main_controller.IsViewportScrollCallback(
        MainFrame()->GetDocument()->documentElement()->GetApplyScroll()));
  }

  // Set a root scroller in the iframe. Since the main document didn't set a
  // root scroller, the global root scroller shouldn't change.
  {
    iframe->contentDocument()->setRootScroller(container, non_throw);
    MainFrameView()->UpdateAllLifecyclePhases();

    EXPECT_EQ(
        main_controller.RootScrollerLayer(),
        MainFrameView()->LayoutViewportScrollableArea()->LayerForScrolling());
    EXPECT_TRUE(main_controller.IsViewportScrollCallback(
        MainFrame()->GetDocument()->documentElement()->GetApplyScroll()));
  }

  // Setting the iframe as the root scroller in the main frame should now
  // link the root scrollers so the container should now be the global root
  // scroller.
  {
    MainFrame()->GetDocument()->setRootScroller(iframe, non_throw);
    MainFrameView()->UpdateAllLifecyclePhases();

    ScrollableArea* container_scroller =
        static_cast<PaintInvalidationCapableScrollableArea*>(
            ToLayoutBox(container->GetLayoutObject())->GetScrollableArea());

    EXPECT_EQ(main_controller.RootScrollerLayer(),
              container_scroller->LayerForScrolling());
    EXPECT_FALSE(main_controller.IsViewportScrollCallback(
        MainFrame()->GetDocument()->documentElement()->GetApplyScroll()));
    EXPECT_TRUE(
        main_controller.IsViewportScrollCallback(container->GetApplyScroll()));
  }

  // Unsetting the root scroller in the iframe should reset its effective
  // root scroller to the iframe's documentElement and thus the iframe's
  // documentElement becomes the global root scroller.
  {
    iframe->contentDocument()->setRootScroller(nullptr, non_throw);
    MainFrameView()->UpdateAllLifecyclePhases();
    EXPECT_EQ(main_controller.RootScrollerLayer(),
              iframe->contentDocument()
                  ->View()
                  ->LayoutViewportScrollableArea()
                  ->LayerForScrolling());
    EXPECT_FALSE(
        main_controller.IsViewportScrollCallback(container->GetApplyScroll()));
    EXPECT_FALSE(main_controller.IsViewportScrollCallback(
        MainFrame()->GetDocument()->documentElement()->GetApplyScroll()));
    EXPECT_TRUE(main_controller.IsViewportScrollCallback(
        iframe->contentDocument()->documentElement()->GetApplyScroll()));
  }

  // Finally, unsetting the main frame's root scroller should reset it to the
  // documentElement and corresponding layer.
  {
    MainFrame()->GetDocument()->setRootScroller(nullptr, non_throw);
    MainFrameView()->UpdateAllLifecyclePhases();
    EXPECT_EQ(
        main_controller.RootScrollerLayer(),
        MainFrameView()->LayoutViewportScrollableArea()->LayerForScrolling());
    EXPECT_TRUE(main_controller.IsViewportScrollCallback(
        MainFrame()->GetDocument()->documentElement()->GetApplyScroll()));
    EXPECT_FALSE(
        main_controller.IsViewportScrollCallback(container->GetApplyScroll()));
    EXPECT_FALSE(main_controller.IsViewportScrollCallback(
        iframe->contentDocument()->documentElement()->GetApplyScroll()));
  }
}

TEST_F(RootScrollerTest, TestSetRootScrollerCausesViewportLayerChange) {
  // TODO(bokan): Need a test that changing root scrollers actually sets the
  // outer viewport layer on the compositor, even in the absence of other
  // compositing changes. crbug.com/505516
}

// Tests that trying to set an element as the root scroller of a document inside
// an iframe fails when that element belongs to the parent document.
// TODO(bokan): Recent changes mean this is now possible but should be fixed.
TEST_F(RootScrollerTest,
       DISABLED_TestSetRootScrollerOnElementFromOutsideIframe) {
  Initialize("root-scroller-iframe.html");

  ASSERT_EQ(nullptr, MainFrame()->GetDocument()->rootScroller());
  {
    // Try to set the the root scroller of the child document to be the
    // <iframe> element in the parent document.
    HTMLFrameOwnerElement* iframe = ToHTMLFrameOwnerElement(
        MainFrame()->GetDocument()->getElementById("iframe"));
    NonThrowableExceptionState non_throw;
    Element* body =
        MainFrame()->GetDocument()->QuerySelector("body", non_throw);

    EXPECT_EQ(nullptr, iframe->contentDocument()->rootScroller());

    iframe->contentDocument()->setRootScroller(iframe);

    EXPECT_EQ(iframe, iframe->contentDocument()->rootScroller());

    // Try to set the root scroller of the child document to be the
    // <body> element of the parent document.
    iframe->contentDocument()->setRootScroller(body);

    EXPECT_EQ(body, iframe->contentDocument()->rootScroller());
  }
}

// Do a basic sanity check that setting as root scroller an iframe that's remote
// doesn't crash or otherwise fail catastrophically.
TEST_F(RootScrollerTest, RemoteIFrame) {
  FrameTestHelpers::TestWebRemoteFrameClient remote_frame_client;
  Initialize("root-scroller-iframe.html");

  // Initialization: Replace the iframe with a remote frame.
  {
    WebRemoteFrame* remote_frame = WebRemoteFrame::Create(
        WebTreeScopeType::kDocument, &remote_frame_client);
    WebFrame* child_frame = MainWebFrame()->FirstChild();
    child_frame->Swap(remote_frame);
  }

  // Set the root scroller in the local main frame to the iframe (which is
  // remote).
  {
    Element* iframe = MainFrame()->GetDocument()->getElementById("iframe");
    NonThrowableExceptionState non_throw;
    MainFrame()->GetDocument()->setRootScroller(iframe, non_throw);
    EXPECT_EQ(iframe, MainFrame()->GetDocument()->rootScroller());
  }

  // Reset explicitly to prevent lifetime issues with the RemoteFrameClient.
  helper_.Reset();
}

// Do a basic sanity check that the scrolling and root scroller machinery
// doesn't fail catastrophically in site isolation when the main frame is
// remote. Setting a root scroller in OOPIF isn't implemented yet but we should
// still scroll as before and not crash.
TEST_F(RootScrollerTest, RemoteMainFrame) {
  FrameTestHelpers::TestWebRemoteFrameClient remote_client;
  FrameTestHelpers::TestWebWidgetClient web_widget_client;
  WebFrameWidget* widget;
  WebLocalFrameImpl* local_frame;

  Initialize("root-scroller-iframe.html");

  // Initialization: Set the main frame to be a RemoteFrame and add a local
  // child.
  {
    GetWebView()->SetMainFrame(remote_client.GetFrame());
    WebRemoteFrame* root = GetWebView()->MainFrame()->ToWebRemoteFrame();
    root->SetReplicatedOrigin(SecurityOrigin::CreateUnique());
    WebFrameOwnerProperties properties;
    local_frame = FrameTestHelpers::CreateLocalChild(
        root, "frameName", nullptr, nullptr, nullptr, properties);

    FrameTestHelpers::LoadFrame(local_frame,
                                base_url_ + "root-scroller-child.html");
    widget = local_frame->FrameWidget();
    widget->Resize(WebSize(400, 400));
  }

  Document* document = local_frame->GetFrameView()->GetFrame().GetDocument();
  Element* container = document->getElementById("container");

  // Try scrolling in the iframe.
  {
    widget->HandleInputEvent(
        GenerateWheelGestureEvent(WebInputEvent::kGestureScrollBegin));
    widget->HandleInputEvent(GenerateWheelGestureEvent(
        WebInputEvent::kGestureScrollUpdate, 0, -100));
    widget->HandleInputEvent(
        GenerateWheelGestureEvent(WebInputEvent::kGestureScrollEnd));
    EXPECT_EQ(100, container->scrollTop());
  }

  // Set the container Element as the root scroller.
  {
    NonThrowableExceptionState non_throw;
    document->setRootScroller(container, non_throw);
    EXPECT_EQ(container, document->rootScroller());
  }

  // Try scrolling in the iframe now that it has a root scroller set.
  {
    widget->HandleInputEvent(
        GenerateWheelGestureEvent(WebInputEvent::kGestureScrollBegin));
    widget->HandleInputEvent(GenerateWheelGestureEvent(
        WebInputEvent::kGestureScrollUpdate, 0, -100));
    widget->HandleInputEvent(
        GenerateWheelGestureEvent(WebInputEvent::kGestureScrollEnd));

    // TODO(bokan): This doesn't work right now because we notice in
    // Element::nativeApplyScroll that the container is the
    // effectiveRootScroller but the only way we expect to get to
    // nativeApplyScroll is if the effective scroller had its applyScroll
    // ViewportScrollCallback removed.  Keep the scrolls to guard crashes
    // but the expectations on when a ViewportScrollCallback have changed
    // and should be updated.
    // EXPECT_EQ(200, container->scrollTop());
  }

  // Reset explicitly to prevent lifetime issues with the RemoteFrameClient.
  helper_.Reset();
}

// Tests that removing the root scroller element from the DOM resets the
// effective root scroller without waiting for any lifecycle events.
TEST_F(RootScrollerTest, RemoveRootScrollerFromDom) {
  Initialize("root-scroller-iframe.html");

  {
    HTMLFrameOwnerElement* iframe = ToHTMLFrameOwnerElement(
        MainFrame()->GetDocument()->getElementById("iframe"));
    Element* inner_container =
        iframe->contentDocument()->getElementById("container");

    MainFrame()->GetDocument()->setRootScroller(iframe);
    iframe->contentDocument()->setRootScroller(inner_container);
    MainFrameView()->UpdateAllLifecyclePhases();

    ASSERT_EQ(iframe, MainFrame()->GetDocument()->rootScroller());
    ASSERT_EQ(iframe, EffectiveRootScroller(MainFrame()->GetDocument()));
    ASSERT_EQ(inner_container, iframe->contentDocument()->rootScroller());
    ASSERT_EQ(inner_container,
              EffectiveRootScroller(iframe->contentDocument()));

    iframe->contentDocument()->body()->setInnerHTML("");

    // If the root scroller wasn't updated by the DOM removal above, this
    // will touch the disposed root scroller's ScrollableArea.
    MainFrameView()->GetRootFrameViewport()->ServiceScrollAnimations(0);
  }
}

// Tests that we still have a global root scroller layer when the HTML element
// has no layout object. crbug.com/637036.
TEST_F(RootScrollerTest, DocumentElementHasNoLayoutObject) {
  Initialize("overflow-scrolling.html");

  // There's no rootScroller set on this page so we should default to the
  // document Node, which means we should use the layout viewport. Ensure this
  // happens even if the <html> element has no LayoutObject.
  ExecuteScript("document.documentElement.style.display = 'none';");

  const TopDocumentRootScrollerController& global_controller =
      MainFrame()->GetDocument()->GetPage()->GlobalRootScrollerController();

  EXPECT_EQ(MainFrame()->GetDocument()->documentElement(),
            global_controller.GlobalRootScroller());
  EXPECT_EQ(
      MainFrameView()->LayoutViewportScrollableArea()->LayerForScrolling(),
      global_controller.RootScrollerLayer());
}

// On Android, the main scrollbars are owned by the visual viewport and the
// FrameView's disabled. This functionality should extend to a rootScroller
// that isn't the main FrameView.
TEST_F(RootScrollerTest, UseVisualViewportScrollbars) {
  Initialize("root-scroller.html");

  Element* container = MainFrame()->GetDocument()->getElementById("container");
  NonThrowableExceptionState non_throw;
  MainFrame()->GetDocument()->setRootScroller(container, non_throw);
  MainFrameView()->UpdateAllLifecyclePhases();

  ScrollableArea* container_scroller =
      static_cast<PaintInvalidationCapableScrollableArea*>(
          ToLayoutBox(container->GetLayoutObject())->GetScrollableArea());

  EXPECT_FALSE(container_scroller->HorizontalScrollbar());
  EXPECT_FALSE(container_scroller->VerticalScrollbar());
  EXPECT_GT(container_scroller->MaximumScrollOffset().Width(), 0);
  EXPECT_GT(container_scroller->MaximumScrollOffset().Height(), 0);
}

// On Android, the main scrollbars are owned by the visual viewport and the
// FrameView's disabled. This functionality should extend to a rootScroller
// that's a nested iframe.
TEST_F(RootScrollerTest, UseVisualViewportScrollbarsIframe) {
  Initialize("root-scroller-iframe.html");

  Element* iframe = MainFrame()->GetDocument()->getElementById("iframe");
  LocalFrame* child_frame =
      ToLocalFrame(ToHTMLFrameOwnerElement(iframe)->ContentFrame());

  NonThrowableExceptionState non_throw;
  MainFrame()->GetDocument()->setRootScroller(iframe, non_throw);

  WebLocalFrame* child_web_frame =
      MainWebFrame()->FirstChild()->ToWebLocalFrame();
  ExecuteScript(
      "document.getElementById('container').style.width = '200%';"
      "document.getElementById('container').style.height = '200%';",
      *child_web_frame);

  MainFrameView()->UpdateAllLifecyclePhases();

  ScrollableArea* container_scroller = child_frame->View();

  EXPECT_FALSE(container_scroller->HorizontalScrollbar());
  EXPECT_FALSE(container_scroller->VerticalScrollbar());
  EXPECT_GT(container_scroller->MaximumScrollOffset().Width(), 0);
  EXPECT_GT(container_scroller->MaximumScrollOffset().Height(), 0);
}

TEST_F(RootScrollerTest, TopControlsAdjustmentAppliedToRootScroller) {
  Initialize();

  WebURL base_url = URLTestHelpers::ToKURL("http://www.test.com/");
  FrameTestHelpers::LoadHTMLString(GetWebView()->MainFrame(),
                                   "<!DOCTYPE html>"
                                   "<style>"
                                   "  body, html {"
                                   "    width: 100%;"
                                   "    height: 100%;"
                                   "    margin: 0px;"
                                   "  }"
                                   "  #container {"
                                   "    width: 100%;"
                                   "    height: 100%;"
                                   "    overflow: auto;"
                                   "  }"
                                   "</style>"
                                   "<div id='container'>"
                                   "  <div style='height:1000px'>test</div>"
                                   "</div>",
                                   base_url);

  GetWebView()->ResizeWithBrowserControls(IntSize(400, 400), 50, true);
  MainFrameView()->UpdateAllLifecyclePhases();

  Element* container = MainFrame()->GetDocument()->getElementById("container");
  MainFrame()->GetDocument()->setRootScroller(container, ASSERT_NO_EXCEPTION);

  ScrollableArea* container_scroller =
      static_cast<PaintInvalidationCapableScrollableArea*>(
          ToLayoutBox(container->GetLayoutObject())->GetScrollableArea());

  // Hide the top controls and scroll down maximally. We should account for the
  // change in maximum scroll offset due to the top controls hiding. That is,
  // since the controls are hidden, the "content area" is taller so the maximum
  // scroll offset should shrink.
  ASSERT_EQ(1000 - 400, container_scroller->MaximumScrollOffset().Height());

  GetWebView()->HandleInputEvent(
      GenerateTouchGestureEvent(WebInputEvent::kGestureScrollBegin));
  ASSERT_EQ(1, GetBrowserControls().ShownRatio());
  GetWebView()->HandleInputEvent(GenerateTouchGestureEvent(
      WebInputEvent::kGestureScrollUpdate, 0, -GetBrowserControls().Height()));
  ASSERT_EQ(0, GetBrowserControls().ShownRatio());
  EXPECT_EQ(1000 - 450, container_scroller->MaximumScrollOffset().Height());

  GetWebView()->HandleInputEvent(
      GenerateTouchGestureEvent(WebInputEvent::kGestureScrollUpdate, 0, -3000));
  EXPECT_EQ(1000 - 450, container_scroller->GetScrollOffset().Height());

  GetWebView()->HandleInputEvent(
      GenerateTouchGestureEvent(WebInputEvent::kGestureScrollEnd));
  GetWebView()->ResizeWithBrowserControls(IntSize(400, 450), 50, false);
  EXPECT_EQ(1000 - 450, container_scroller->MaximumScrollOffset().Height());
}

TEST_F(RootScrollerTest, RotationAnchoring) {
  Initialize("root-scroller-rotation.html");

  ScrollableArea* container_scroller;

  {
    GetWebView()->ResizeWithBrowserControls(IntSize(250, 1000), 0, true);
    MainFrameView()->UpdateAllLifecyclePhases();

    Element* container =
        MainFrame()->GetDocument()->getElementById("container");
    NonThrowableExceptionState non_throw;
    MainFrame()->GetDocument()->setRootScroller(container, non_throw);
    MainFrameView()->UpdateAllLifecyclePhases();

    container_scroller = static_cast<PaintInvalidationCapableScrollableArea*>(
        ToLayoutBox(container->GetLayoutObject())->GetScrollableArea());
  }

  Element* target = MainFrame()->GetDocument()->getElementById("target");

  // Zoom in and scroll the viewport so that the target is fully in the
  // viewport and the visual viewport is fully scrolled within the layout
  // viepwort.
  {
    int scroll_x = 250 * 4;
    int scroll_y = 1000 * 4;

    GetWebView()->SetPageScaleFactor(2);
    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollBegin));
    GetWebView()->HandleInputEvent(GenerateTouchGestureEvent(
        WebInputEvent::kGestureScrollUpdate, -scroll_x, -scroll_y));
    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollEnd));

    // The visual viewport should be 1.5 screens scrolled so that the target
    // occupies the bottom quadrant of the layout viewport.
    ASSERT_EQ((250 * 3) / 2, container_scroller->GetScrollOffset().Width());
    ASSERT_EQ((1000 * 3) / 2, container_scroller->GetScrollOffset().Height());

    // The visual viewport should have scrolled the last half layout viewport.
    ASSERT_EQ((250) / 2, GetVisualViewport().GetScrollOffset().Width());
    ASSERT_EQ((1000) / 2, GetVisualViewport().GetScrollOffset().Height());
  }

  // Now do a rotation resize.
  GetWebView()->ResizeWithBrowserControls(IntSize(1000, 250), 50, false);
  MainFrameView()->UpdateAllLifecyclePhases();

  // The visual viewport should remain fully filled by the target.
  ClientRect* rect = target->getBoundingClientRect();
  EXPECT_EQ(rect->left(), GetVisualViewport().GetScrollOffset().Width());
  EXPECT_EQ(rect->top(), GetVisualViewport().GetScrollOffset().Height());
}

// Tests that we don't crash if the default documentElement isn't a valid root
// scroller. This can happen in some edge cases where documentElement isn't
// <html>. crbug.com/668553.
TEST_F(RootScrollerTest, InvalidDefaultRootScroller) {
  Initialize("overflow-scrolling.html");

  Document* document = MainFrame()->GetDocument();

  Element* br = document->createElement("br");
  document->ReplaceChild(br, document->documentElement());
  MainFrameView()->UpdateAllLifecyclePhases();
  Element* html = document->createElement("html");
  Element* body = document->createElement("body");
  html->AppendChild(body);
  body->AppendChild(br);
  document->AppendChild(html);
  MainFrameView()->UpdateAllLifecyclePhases();
}

// Ensure that removing the root scroller element causes an update to the RFV's
// layout viewport immediately since old layout viewport is now part of a
// detached layout hierarchy.
TEST_F(RootScrollerTest, ImmediateUpdateOfLayoutViewport) {
  Initialize("root-scroller-iframe.html");

  Document* document = MainFrame()->GetDocument();
  HTMLFrameOwnerElement* iframe = ToHTMLFrameOwnerElement(
      MainFrame()->GetDocument()->getElementById("iframe"));

  document->setRootScroller(iframe);
  MainFrameView()->UpdateAllLifecyclePhases();

  RootScrollerController& main_controller =
      MainFrame()->GetDocument()->GetRootScrollerController();

  LocalFrame* iframe_local_frame = ToLocalFrame(iframe->ContentFrame());
  EXPECT_EQ(iframe, &main_controller.EffectiveRootScroller());
  EXPECT_EQ(iframe_local_frame->View()->LayoutViewportScrollableArea(),
            &MainFrameView()->GetRootFrameViewport()->LayoutViewport());

  // Remove the <iframe> and make sure the layout viewport reverts to the
  // FrameView without a layout.
  iframe->remove();

  EXPECT_EQ(MainFrameView()->LayoutViewportScrollableArea(),
            &MainFrameView()->GetRootFrameViewport()->LayoutViewport());
}

class RootScrollerHitTest : public RootScrollerTest {
 public:
  void CheckHitTestAtBottomOfScreen() {
    HideTopControlsWithMaximalScroll();

    // Do a hit test at the very bottom of the screen. This should be outside
    // the root scroller's LayoutBox since inert top controls won't resize the
    // ICB but, since we expaned the clip, we should still be able to hit the
    // target.
    WebPoint point(200, 445);
    WebSize tap_area(20, 20);
    WebHitTestResult result =
        GetWebView()->HitTestResultForTap(point, tap_area);

    Node* hit_node = result.GetNode().Unwrap<Node>();
    Element* target = MainFrame()->GetDocument()->getElementById("target");
    ASSERT_TRUE(target);
    EXPECT_EQ(target, hit_node);
  }

 private:
  void HideTopControlsWithMaximalScroll() {
    // Do a scroll gesture that hides the top controls and scrolls all the way
    // to the bottom.
    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollBegin));
    ASSERT_EQ(1, GetBrowserControls().ShownRatio());
    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollUpdate, 0,
                                  -GetBrowserControls().Height()));
    ASSERT_EQ(0, GetBrowserControls().ShownRatio());
    GetWebView()->HandleInputEvent(GenerateTouchGestureEvent(
        WebInputEvent::kGestureScrollUpdate, 0, -100000));
    GetWebView()->HandleInputEvent(
        GenerateTouchGestureEvent(WebInputEvent::kGestureScrollEnd));
    GetWebView()->ResizeWithBrowserControls(IntSize(400, 450), 50, false);
  }
};

// Test that hit testing in the area revealed at the bottom of the screen
// revealed by hiding the URL bar works properly when using a root scroller
// when the target and scroller are in the same PaintLayer.
TEST_F(RootScrollerHitTest, HitTestInAreaRevealedByURLBarSameLayer) {
  // Add a target at the bottom of the root scroller that's the size of the url
  // bar. We'll test that hiding the URL bar appropriately adjusts clipping so
  // that we can hit this target.
  Initialize();
  WebURL baseURL = URLTestHelpers::ToKURL("http://www.test.com/");
  FrameTestHelpers::LoadHTMLString(GetWebView()->MainFrame(),
                                   "<!DOCTYPE html>"
                                   "<style>"
                                   "  body, html {"
                                   "    height: 100%;"
                                   "    margin: 0px;"
                                   "  }"
                                   "  #spacer {"
                                   "    height: 1000px;"
                                   "  }"
                                   "  #container {"
                                   "    position: absolute;"
                                   "    width: 100%;"
                                   "    height: 100%;"
                                   "    overflow: auto;"
                                   "  }"
                                   "  #target {"
                                   "    width: 100%;"
                                   "    height: 50px;"
                                   "  }"
                                   "</style>"
                                   "<div id='container'>"
                                   "  <div id='spacer'></div>"
                                   "  <div id='target'></div>"
                                   "</div>",
                                   baseURL);

  Document* document = MainFrame()->GetDocument();
  Element* container = document->getElementById("container");
  Element* target = document->getElementById("target");
  document->setRootScroller(container);

  // This test checks hit testing while the target is in the same PaintLayer as
  // the root scroller.
  ASSERT_EQ(ToLayoutBox(target->GetLayoutObject())->EnclosingLayer(),
            ToLayoutBox(container->GetLayoutObject())->Layer());

  CheckHitTestAtBottomOfScreen();
}

// Test that hit testing in the area revealed at the bottom of the screen
// revealed by hiding the URL bar works properly when using a root scroller
// when the target and scroller are in different PaintLayers.
TEST_F(RootScrollerHitTest, HitTestInAreaRevealedByURLBarDifferentLayer) {
  // Add a target at the bottom of the root scroller that's the size of the url
  // bar. We'll test that hiding the URL bar appropriately adjusts clipping so
  // that we can hit this target.
  Initialize();
  WebURL baseURL = URLTestHelpers::ToKURL("http://www.test.com/");
  FrameTestHelpers::LoadHTMLString(GetWebView()->MainFrame(),
                                   "<!DOCTYPE html>"
                                   "<style>"
                                   "  body, html {"
                                   "    height: 100%;"
                                   "    margin: 0px;"
                                   "  }"
                                   "  #spacer {"
                                   "    height: 1000px;"
                                   "  }"
                                   "  #container {"
                                   "    position: absolute;"
                                   "    width: 100%;"
                                   "    height: 100%;"
                                   "    overflow: auto;"
                                   "  }"
                                   "  #target {"
                                   "    width: 100%;"
                                   "    height: 50px;"
                                   "    will-change: transform;"
                                   "  }"
                                   "</style>"
                                   "<div id='container'>"
                                   "  <div id='spacer'></div>"
                                   "  <div id='target'></div>"
                                   "</div>",
                                   baseURL);

  Document* document = MainFrame()->GetDocument();
  Element* container = document->getElementById("container");
  Element* target = document->getElementById("target");
  document->setRootScroller(container);

  // Ensure the target and container weren't put into the same layer.
  ASSERT_NE(ToLayoutBox(target->GetLayoutObject())->EnclosingLayer(),
            ToLayoutBox(container->GetLayoutObject())->Layer());

  CheckHitTestAtBottomOfScreen();
}

}  // namespace

}  // namespace blink
