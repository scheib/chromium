// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/devtools/ash_devtools_css_agent.h"
#include "ash/devtools/ash_devtools_dom_agent.h"
#include "ash/devtools/ui_element.h"
#include "ash/devtools/window_element.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/root_window_controller.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "ash/wm/widget_finder.h"
#include "base/memory/ptr_util.h"
#include "base/strings/stringprintf.h"
#include "ui/display/display.h"
#include "ui/views/background.h"
#include "ui/views/widget/native_widget_private.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace {

using namespace ui::devtools::protocol;

const int kDefaultChildNodeCount = -1;
const SkColor kBackgroundColor = SK_ColorRED;
const SkColor kBorderColor = SK_ColorBLUE;

class TestView : public views::View {
 public:
  TestView(const char* name) : views::View(), name_(name) {}

  const char* GetClassName() const override { return name_; }

 private:
  const char* name_;

  DISALLOW_COPY_AND_ASSIGN(TestView);
};

class FakeFrontendChannel : public FrontendChannel {
 public:
  FakeFrontendChannel() {}
  ~FakeFrontendChannel() override {}

  int CountProtocolNotificationMessageStartsWith(const std::string& message) {
    int count = 0;
    for (const std::string& s : protocol_notification_messages_) {
      if (base::StartsWith(s, message, base::CompareCase::SENSITIVE))
        count++;
    }
    return count;
  }

  int CountProtocolNotificationMessage(const std::string& message) {
    return std::count(protocol_notification_messages_.begin(),
                      protocol_notification_messages_.end(), message);
  }

  // FrontendChannel
  void sendProtocolResponse(int callId,
                            std::unique_ptr<Serializable> message) override {}
  void flushProtocolNotifications() override {}
  void sendProtocolNotification(
      std::unique_ptr<Serializable> message) override {
    protocol_notification_messages_.push_back(message->serialize());
  }

 private:
  std::vector<std::string> protocol_notification_messages_;

  DISALLOW_COPY_AND_ASSIGN(FakeFrontendChannel);
};

std::string GetAttributeValue(const std::string& attribute, DOM::Node* node) {
  EXPECT_TRUE(node->hasAttributes());
  Array<std::string>* attributes = node->getAttributes(nullptr);
  for (size_t i = 0; i < attributes->length() - 1; i++) {
    if (attributes->get(i) == attribute)
      return attributes->get(i + 1);
  }
  return nullptr;
}

bool Equals(aura::Window* window, DOM::Node* node) {
  int children_count = static_cast<int>(window->children().size());
  if (GetInternalWidgetForWindow(window))
    children_count++;
  return "Window" == node->getNodeName() &&
         window->GetName() == GetAttributeValue("name", node) &&
         children_count == node->getChildNodeCount(kDefaultChildNodeCount);
}

void Compare(views::Widget* widget, DOM::Node* node) {
  EXPECT_EQ("Widget", node->getNodeName());
  EXPECT_EQ(widget->GetName(), GetAttributeValue("name", node));
  EXPECT_EQ(widget->GetRootView() ? 1 : 0,
            node->getChildNodeCount(kDefaultChildNodeCount));
}

void Compare(views::View* view, DOM::Node* node) {
  EXPECT_EQ("View", node->getNodeName());
  EXPECT_EQ(view->GetClassName(), GetAttributeValue("name", node));
  EXPECT_EQ(view->child_count(),
            node->getChildNodeCount(kDefaultChildNodeCount));
}

void Compare(aura::Window* window, DOM::Node* node) {
  EXPECT_TRUE(Equals(window, node));
}

DOM::Node* FindInRoot(aura::Window* window, DOM::Node* root) {
  if (Equals(window, root))
    return root;

  Array<DOM::Node>* children = root->getChildren(nullptr);
  DOM::Node* window_node = nullptr;
  for (size_t i = 0; i < children->length(); i++) {
    window_node = FindInRoot(window, children->get(i));
    if (window_node)
      return window_node;
  }
  return window_node;
}

int GetPropertyByName(const std::string& name,
                      Array<CSS::CSSProperty>* properties) {
  for (size_t i = 0; i < properties->length(); i++) {
    CSS::CSSProperty* property = properties->get(i);
    if (property->getName() == name) {
      int value;
      EXPECT_TRUE(base::StringToInt(property->getValue(), &value));
      return value;
    }
  }
  NOTREACHED();
  return -1;
}

aura::Window* GetHighlightingWindow(int root_window_index) {
  const aura::Window::Windows& overlay_windows =
      Shell::GetAllRootWindows()[root_window_index]
          ->GetChildById(kShellWindowId_OverlayContainer)
          ->children();
  for (aura::Window* window : overlay_windows) {
    if (window->GetName() == "HighlightingWidget")
      return window;
  }
  NOTREACHED();
  return nullptr;
}

std::unique_ptr<DOM::RGBA> SkColorToRGBA(const SkColor& color) {
  return DOM::RGBA::create()
      .setA(SkColorGetA(color) / 255)
      .setB(SkColorGetB(color))
      .setG(SkColorGetG(color))
      .setR(SkColorGetR(color))
      .build();
}

std::unique_ptr<DOM::HighlightConfig> CreateHighlightConfig(
    const SkColor& background_color,
    const SkColor& border_color) {
  return DOM::HighlightConfig::create()
      .setContentColor(SkColorToRGBA(background_color))
      .setBorderColor(SkColorToRGBA(border_color))
      .build();
}

void ExpectHighlighted(const gfx::Rect& bounds, int root_window_index) {
  aura::Window* highlighting_window = GetHighlightingWindow(root_window_index);
  EXPECT_TRUE(highlighting_window->IsVisible());
  EXPECT_EQ(bounds, highlighting_window->GetBoundsInScreen());
  EXPECT_EQ(kBackgroundColor, GetInternalWidgetForWindow(highlighting_window)
                                  ->GetRootView()
                                  ->background()
                                  ->get_color());
}

aura::Window* GetPrimaryRootWindow() {
  return Shell::Get()->GetPrimaryRootWindow();
}

}  // namespace

class AshDevToolsTest : public test::AshTestBase {
 public:
  AshDevToolsTest() {}
  ~AshDevToolsTest() override {}

  views::internal::NativeWidgetPrivate* CreateTestNativeWidget() {
    views::Widget* widget = new views::Widget;
    views::Widget::InitParams params;
    params.ownership = views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET;
    Shell::GetPrimaryRootWindowController()
        ->ConfigureWidgetInitParamsForContainer(
            widget, kShellWindowId_DefaultContainer, &params);
    widget->Init(params);
    return widget->native_widget_private();
  }

  std::unique_ptr<views::Widget> CreateTestWidget(const gfx::Rect& bounds) {
    return AshTestBase::CreateTestWidget(nullptr, kShellWindowId_Invalid,
                                         bounds);
  }

  void SetUp() override {
    AshTestBase::SetUp();
    fake_frontend_channel_ = base::MakeUnique<FakeFrontendChannel>();
    uber_dispatcher_ =
        base::MakeUnique<UberDispatcher>(fake_frontend_channel_.get());
    dom_agent_ = base::MakeUnique<devtools::AshDevToolsDOMAgent>();
    dom_agent_->Init(uber_dispatcher_.get());
    css_agent_ =
        base::MakeUnique<devtools::AshDevToolsCSSAgent>(dom_agent_.get());
    css_agent_->Init(uber_dispatcher_.get());
    css_agent_->enable();
  }

  void TearDown() override {
    css_agent_.reset();
    dom_agent_.reset();
    uber_dispatcher_.reset();
    fake_frontend_channel_.reset();
    AshTestBase::TearDown();
  }

  void ExpectChildNodeInserted(int parent_id, int prev_sibling_id) {
    EXPECT_EQ(1, frontend_channel()->CountProtocolNotificationMessageStartsWith(
                     base::StringPrintf("{\"method\":\"DOM.childNodeInserted\","
                                        "\"params\":{\"parentNodeId\":%d,"
                                        "\"previousNodeId\":%d",
                                        parent_id, prev_sibling_id)));
  }

  void ExpectChildNodeRemoved(int parent_id, int node_id) {
    EXPECT_EQ(1, frontend_channel()->CountProtocolNotificationMessage(
                     base::StringPrintf(
                         "{\"method\":\"DOM.childNodeRemoved\",\"params\":{"
                         "\"parentNodeId\":%d,\"nodeId\":%d}}",
                         parent_id, node_id)));
  }

  int GetStyleSheetChangedCount(int node_id) {
    return frontend_channel()->CountProtocolNotificationMessage(
        base::StringPrintf("{\"method\":\"CSS.styleSheetChanged\",\"params\":{"
                           "\"styleSheetId\":\"%d\"}}",
                           node_id));
  }

  void CompareNodeBounds(DOM::Node* node, const gfx::Rect& bounds) {
    Maybe<CSS::CSSStyle> styles;
    css_agent_->getMatchedStylesForNode(node->getNodeId(), &styles);
    ASSERT_TRUE(styles.isJust());
    Array<CSS::CSSProperty>* properties = styles.fromJust()->getCssProperties();
    EXPECT_EQ(bounds.height(), GetPropertyByName("height", properties));
    EXPECT_EQ(bounds.width(), GetPropertyByName("width", properties));
    EXPECT_EQ(bounds.x(), GetPropertyByName("x", properties));
    EXPECT_EQ(bounds.y(), GetPropertyByName("y", properties));
  }

  void SetStyleTexts(DOM::Node* node,
                     const std::string& style_text,
                     bool success) {
    auto edits = Array<CSS::StyleDeclarationEdit>::create();
    auto edit = CSS::StyleDeclarationEdit::create()
                    .setStyleSheetId(base::IntToString(node->getNodeId()))
                    .setText(style_text)
                    .build();
    edits->addItem(std::move(edit));
    std::unique_ptr<Array<CSS::CSSStyle>> output;
    EXPECT_EQ(success,
              css_agent_->setStyleTexts(std::move(edits), &output).isSuccess());

    if (success)
      ASSERT_TRUE(output);
    else
      ASSERT_FALSE(output);
  }

  void HighlightNode(int node_id) {
    dom_agent_->highlightNode(
        CreateHighlightConfig(kBackgroundColor, kBorderColor), node_id);
  }

  void HideHighlight(int root_window_index) {
    dom_agent_->hideHighlight();
    ASSERT_FALSE(GetHighlightingWindow(root_window_index)->IsVisible());
  }

  FakeFrontendChannel* frontend_channel() {
    return fake_frontend_channel_.get();
  }

  devtools::AshDevToolsCSSAgent* css_agent() { return css_agent_.get(); }
  devtools::AshDevToolsDOMAgent* dom_agent() { return dom_agent_.get(); }

 private:
  std::unique_ptr<UberDispatcher> uber_dispatcher_;
  std::unique_ptr<FakeFrontendChannel> fake_frontend_channel_;
  std::unique_ptr<devtools::AshDevToolsDOMAgent> dom_agent_;
  std::unique_ptr<devtools::AshDevToolsCSSAgent> css_agent_;

  DISALLOW_COPY_AND_ASSIGN(AshDevToolsTest);
};

TEST_F(AshDevToolsTest, GetDocumentWithWindowWidgetView) {
  std::unique_ptr<views::Widget> widget(
      CreateTestWidget(gfx::Rect(1, 1, 1, 1)));
  aura::Window* parent_window = widget->GetNativeWindow();
  parent_window->SetName("parent_window");
  std::unique_ptr<aura::Window> child_window = CreateChildWindow(parent_window);
  child_window->SetName("child_window");
  widget->Show();
  views::View* child_view = new TestView("child_view");
  widget->GetRootView()->AddChildView(child_view);

  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  DOM::Node* parent_node = FindInRoot(parent_window, root.get());
  ASSERT_TRUE(parent_node);
  Array<DOM::Node>* parent_children = parent_node->getChildren(nullptr);
  ASSERT_TRUE(parent_children);
  DOM::Node* widget_node = parent_children->get(0);
  Compare(widget.get(), widget_node);
  Compare(child_window.get(), parent_children->get(1));
  Array<DOM::Node>* widget_children = widget_node->getChildren(nullptr);
  ASSERT_TRUE(widget_children);
  Compare(widget->GetRootView(), widget_children->get(0));
  ASSERT_TRUE(widget_children->get(0)->getChildren(nullptr));
  Compare(child_view, widget_children->get(0)->getChildren(nullptr)->get(1));
}

TEST_F(AshDevToolsTest, GetDocumentNativeWidgetOwnsWidget) {
  views::internal::NativeWidgetPrivate* native_widget_private =
      CreateTestNativeWidget();
  views::Widget* widget = native_widget_private->GetWidget();
  aura::Window* parent_window = widget->GetNativeWindow();

  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  DOM::Node* parent_node = FindInRoot(parent_window, root.get());
  ASSERT_TRUE(parent_node);
  DOM::Node* widget_node = parent_node->getChildren(nullptr)->get(0);
  Compare(widget, widget_node);
  // Destroy NativeWidget followed by |widget|
  widget->CloseNow();
}

TEST_F(AshDevToolsTest, WindowAddedChildNodeInserted) {
  // Initialize DOMAgent
  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  aura::Window* root_window = GetPrimaryRootWindow();
  aura::Window* parent_window = root_window->children()[0];
  DOM::Node* parent_node = FindInRoot(parent_window, root.get());
  Array<DOM::Node>* parent_node_children = parent_node->getChildren(nullptr);
  DOM::Node* sibling_node =
      parent_node_children->get(parent_node_children->length() - 1);

  std::unique_ptr<aura::Window> child(CreateChildWindow(parent_window));
  ExpectChildNodeInserted(parent_node->getNodeId(), sibling_node->getNodeId());
}

TEST_F(AshDevToolsTest, WindowDestroyedChildNodeRemoved) {
  // Initialize DOMAgent
  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  aura::Window* root_window = GetPrimaryRootWindow();
  aura::Window* rotation_window = root_window->children()[0];
  aura::Window* parent_window = rotation_window->children()[0];
  aura::Window* child_window = parent_window->children()[0];
  DOM::Node* root_node =
      root->getChildren(nullptr)->get(0)->getChildren(nullptr)->get(0);
  DOM::Node* parent_node = root_node->getChildren(nullptr)->get(0);
  DOM::Node* child_node = parent_node->getChildren(nullptr)->get(0);

  Compare(parent_window, parent_node);
  Compare(child_window, child_node);
  delete child_window;
  ExpectChildNodeRemoved(parent_node->getNodeId(), child_node->getNodeId());
}

TEST_F(AshDevToolsTest, WindowReorganizedChildNodeRearranged) {
  // Initialize DOMAgent
  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  aura::Window* root_window = GetPrimaryRootWindow();
  aura::Window* rotation_window = root_window->children()[0];
  aura::Window* parent_window = rotation_window->children()[0];
  aura::Window* target_window = rotation_window->children()[1];
  aura::Window* child_window = parent_window->children()[0];

  DOM::Node* root_node =
      root->getChildren(nullptr)->get(0)->getChildren(nullptr)->get(0);
  DOM::Node* parent_node = root_node->getChildren(nullptr)->get(0);
  DOM::Node* target_node = root_node->getChildren(nullptr)->get(1);
  Array<DOM::Node>* target_node_children = target_node->getChildren(nullptr);
  DOM::Node* sibling_node =
      target_node_children->get(target_node_children->length() - 1);
  DOM::Node* child_node = parent_node->getChildren(nullptr)->get(0);

  Compare(parent_window, parent_node);
  Compare(target_window, target_node);
  Compare(child_window, child_node);
  target_window->AddChild(child_window);
  ExpectChildNodeRemoved(parent_node->getNodeId(), child_node->getNodeId());
  ExpectChildNodeInserted(target_node->getNodeId(), sibling_node->getNodeId());
}

TEST_F(AshDevToolsTest, WindowReorganizedChildNodeRemovedAndInserted) {
  aura::Window* root_window = GetPrimaryRootWindow();
  aura::Window* rotation_window = root_window->children()[0];
  aura::Window* parent_window = rotation_window->children()[0];
  aura::Window* target_window = rotation_window->children()[1];
  std::unique_ptr<aura::Window> child_window(CreateChildWindow(parent_window));

  // Initialize DOMAgent
  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);
  DOM::Node* root_node =
      root->getChildren(nullptr)->get(0)->getChildren(nullptr)->get(0);
  DOM::Node* parent_node = root_node->getChildren(nullptr)->get(0);
  DOM::Node* target_node = root_node->getChildren(nullptr)->get(1);
  Array<DOM::Node>* target_node_children = target_node->getChildren(nullptr);
  DOM::Node* sibling_node =
      target_node_children->get(target_node_children->length() - 1);
  Array<DOM::Node>* parent_node_children = parent_node->getChildren(nullptr);
  DOM::Node* child_node =
      parent_node_children->get(parent_node_children->length() - 1);

  Compare(parent_window, parent_node);
  Compare(target_window, target_node);
  Compare(child_window.get(), child_node);
  parent_window->RemoveChild(child_window.get());
  target_window->AddChild(child_window.get());
  ExpectChildNodeRemoved(parent_node->getNodeId(), child_node->getNodeId());
  ExpectChildNodeInserted(target_node->getNodeId(), sibling_node->getNodeId());
}

TEST_F(AshDevToolsTest, WindowStackingChangedChildNodeRemovedAndInserted) {
  // Initialize DOMAgent
  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  aura::Window* root_window = GetPrimaryRootWindow();
  aura::Window* parent_window = root_window->children()[0];
  aura::Window* child_window = parent_window->children()[0];
  aura::Window* target_window = parent_window->children()[1];

  DOM::Node* parent_node =
      root->getChildren(nullptr)->get(0)->getChildren(nullptr)->get(0);
  Array<DOM::Node>* parent_node_children = parent_node->getChildren(nullptr);
  DOM::Node* child_node = parent_node_children->get(0);
  DOM::Node* sibling_node = parent_node_children->get(1);
  int parent_id = parent_node->getNodeId();

  Compare(parent_window, parent_node);
  Compare(child_window, child_node);
  parent_window->StackChildAbove(child_window, target_window);
  ExpectChildNodeRemoved(parent_id, child_node->getNodeId());
  ExpectChildNodeInserted(parent_id, sibling_node->getNodeId());
}

TEST_F(AshDevToolsTest, ViewInserted) {
  std::unique_ptr<views::Widget> widget(
      CreateTestWidget(gfx::Rect(1, 1, 1, 1)));
  aura::Window* window = widget->GetNativeWindow();
  widget->Show();

  // Initialize DOMAgent
  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  DOM::Node* parent_node = FindInRoot(window, root.get());
  ASSERT_TRUE(parent_node);
  DOM::Node* widget_node = parent_node->getChildren(nullptr)->get(0);
  DOM::Node* root_view_node = widget_node->getChildren(nullptr)->get(0);
  Array<DOM::Node>* root_view_children = root_view_node->getChildren(nullptr);
  ASSERT_TRUE(root_view_children);
  DOM::Node* sibling_view_node =
      root_view_children->get(root_view_children->length() - 1);

  widget->GetRootView()->AddChildView(new views::View);
  ExpectChildNodeInserted(root_view_node->getNodeId(),
                          sibling_view_node->getNodeId());
}

TEST_F(AshDevToolsTest, ViewRemoved) {
  std::unique_ptr<views::Widget> widget(
      CreateTestWidget(gfx::Rect(1, 1, 1, 1)));
  // Need to store |view| in unique_ptr because it is removed from the widget
  // and needs to be destroyed independently
  std::unique_ptr<views::View> child_view = base::MakeUnique<views::View>();
  aura::Window* window = widget->GetNativeWindow();
  widget->Show();
  views::View* root_view = widget->GetRootView();
  root_view->AddChildView(child_view.get());

  // Initialize DOMAgent
  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  DOM::Node* parent_node = FindInRoot(window, root.get());
  ASSERT_TRUE(parent_node);
  DOM::Node* widget_node = parent_node->getChildren(nullptr)->get(0);
  DOM::Node* root_view_node = widget_node->getChildren(nullptr)->get(0);
  Array<DOM::Node>* root_view_children = root_view_node->getChildren(nullptr);
  ASSERT_TRUE(root_view_children);
  DOM::Node* child_view_node =
      root_view_children->get(root_view_children->length() - 1);

  Compare(child_view.get(), child_view_node);
  root_view->RemoveChildView(child_view.get());
  ExpectChildNodeRemoved(root_view_node->getNodeId(),
                         child_view_node->getNodeId());
}

TEST_F(AshDevToolsTest, ViewRearranged) {
  std::unique_ptr<views::Widget> widget(
      CreateTestWidget(gfx::Rect(1, 1, 1, 1)));
  aura::Window* window = widget->GetNativeWindow();
  widget->Show();
  views::View* root_view = widget->GetRootView();
  views::View* parent_view = new views::View;
  views::View* target_view = new views::View;
  views::View* child_view = new views::View;
  views::View* child_view_1 = new views::View;

  root_view->AddChildView(parent_view);
  root_view->AddChildView(target_view);
  parent_view->AddChildView(child_view);
  parent_view->AddChildView(child_view_1);

  // Initialize DOMAgent
  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  DOM::Node* parent_node = FindInRoot(window, root.get());
  ASSERT_TRUE(parent_node);
  DOM::Node* widget_node = parent_node->getChildren(nullptr)->get(0);
  DOM::Node* root_view_node = widget_node->getChildren(nullptr)->get(0);
  Array<DOM::Node>* root_view_children = root_view_node->getChildren(nullptr);
  ASSERT_TRUE(root_view_children);
  size_t root_children_size = root_view_children->length();
  ASSERT_TRUE(root_children_size >= 2);
  DOM::Node* parent_view_node = root_view_children->get(root_children_size - 2);
  DOM::Node* target_view_node = root_view_children->get(root_children_size - 1);
  DOM::Node* child_view_node = parent_view_node->getChildren(nullptr)->get(0);
  DOM::Node* child_view_node_1 = parent_view_node->getChildren(nullptr)->get(1);

  Compare(parent_view, parent_view_node);
  Compare(target_view, target_view_node);
  Compare(child_view, child_view_node);
  Compare(child_view_1, child_view_node_1);

  ASSERT_NE(child_view_node->getNodeId(), child_view_node_1->getNodeId());

  // Reorder child_view_1 from index 1 to 0 in view::Views tree. This makes DOM
  // tree remove view node at position 1 and insert it at position 0.
  parent_view->ReorderChildView(child_view_1, 0);
  ExpectChildNodeRemoved(parent_view_node->getNodeId(),
                         child_view_node_1->getNodeId());
  ExpectChildNodeInserted(parent_view_node->getNodeId(), 0);

  target_view->AddChildView(child_view);
  ExpectChildNodeRemoved(parent_view_node->getNodeId(),
                         child_view_node->getNodeId());
  ExpectChildNodeInserted(target_view_node->getNodeId(), 0);
}

TEST_F(AshDevToolsTest, ViewRearrangedRemovedAndInserted) {
  std::unique_ptr<views::Widget> widget(
      CreateTestWidget(gfx::Rect(1, 1, 1, 1)));
  aura::Window* window = widget->GetNativeWindow();
  widget->Show();
  views::View* root_view = widget->GetRootView();
  views::View* parent_view = new views::View;
  views::View* target_view = new views::View;
  views::View* child_view = new views::View;
  root_view->AddChildView(parent_view);
  root_view->AddChildView(target_view);
  parent_view->AddChildView(child_view);

  // Initialize DOMAgent
  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  DOM::Node* parent_node = FindInRoot(window, root.get());
  ASSERT_TRUE(parent_node);
  DOM::Node* widget_node = parent_node->getChildren(nullptr)->get(0);
  DOM::Node* root_view_node = widget_node->getChildren(nullptr)->get(0);
  Array<DOM::Node>* root_view_children = root_view_node->getChildren(nullptr);
  ASSERT_TRUE(root_view_children);
  size_t root_children_size = root_view_children->length();
  ASSERT_TRUE(root_children_size >= 2);
  DOM::Node* parent_view_node = root_view_children->get(root_children_size - 2);
  DOM::Node* target_view_node = root_view_children->get(root_children_size - 1);
  DOM::Node* child_view_node = parent_view_node->getChildren(nullptr)->get(0);

  Compare(parent_view, parent_view_node);
  Compare(target_view, target_view_node);
  Compare(child_view, child_view_node);
  parent_view->RemoveChildView(child_view);
  target_view->AddChildView(child_view);
  ExpectChildNodeRemoved(parent_view_node->getNodeId(),
                         child_view_node->getNodeId());
  ExpectChildNodeInserted(target_view_node->getNodeId(), 0);
}

TEST_F(AshDevToolsTest, WindowWidgetViewHighlight) {
  std::unique_ptr<views::Widget> widget(
      CreateTestWidget(gfx::Rect(0, 0, 400, 400)));
  aura::Window* parent_window = widget->GetNativeWindow();
  std::unique_ptr<aura::Window> window(CreateChildWindow(parent_window));
  views::View* root_view = widget->GetRootView();

  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  DOM::Node* parent_node = FindInRoot(parent_window, root.get());
  ASSERT_TRUE(parent_node);
  Array<DOM::Node>* parent_children = parent_node->getChildren(nullptr);
  ASSERT_TRUE(parent_children);
  DOM::Node* window_node = parent_children->get(1);
  DOM::Node* widget_node = parent_children->get(0);
  DOM::Node* root_view_node = widget_node->getChildren(nullptr)->get(0);

  HighlightNode(window_node->getNodeId());
  ExpectHighlighted(window->GetBoundsInScreen(), 0);

  HideHighlight(0);

  HighlightNode(widget_node->getNodeId());
  ExpectHighlighted(widget->GetWindowBoundsInScreen(), 0);

  HideHighlight(0);

  HighlightNode(root_view_node->getNodeId());
  ExpectHighlighted(root_view->GetBoundsInScreen(), 0);

  HideHighlight(0);

  // Highlight non-existent node
  HighlightNode(10000);
  EXPECT_FALSE(GetHighlightingWindow(0)->IsVisible());
}

int GetNodeIdFromWindow(devtools::UIElement* ui_element, aura::Window* window) {
  for (auto* child : ui_element->children()) {
    if (child->type() == devtools::UIElementType::WINDOW &&
        static_cast<devtools::WindowElement*>(child)->window() == window) {
      return child->node_id();
    }
  }
  for (auto* child : ui_element->children()) {
    if (child->type() == devtools::UIElementType::WINDOW) {
      int node_id = GetNodeIdFromWindow(child, window);
      if (node_id > 0)
        return node_id;
    }
  }
  return 0;
}

TEST_F(AshDevToolsTest, MultipleDisplayHighlight) {
  UpdateDisplay("300x400,500x500");

  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  std::unique_ptr<aura::Window> window(
      CreateTestWindowInShellWithBounds(gfx::Rect(1, 2, 30, 40)));

  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  EXPECT_EQ(root_windows[0], window->GetRootWindow());
  HighlightNode(
      GetNodeIdFromWindow(dom_agent()->window_element_root(), window.get()));
  ExpectHighlighted(window->GetBoundsInScreen(), 0);

  window->SetBoundsInScreen(gfx::Rect(500, 0, 50, 50), GetSecondaryDisplay());
  EXPECT_EQ(root_windows[1], window->GetRootWindow());
  HighlightNode(
      GetNodeIdFromWindow(dom_agent()->window_element_root(), window.get()));
  ExpectHighlighted(window->GetBoundsInScreen(), 1);
}

TEST_F(AshDevToolsTest, WindowWidgetViewGetMatchedStylesForNode) {
  std::unique_ptr<views::Widget> widget(
      CreateTestWidget(gfx::Rect(1, 1, 1, 1)));
  aura::Window* parent_window = widget->GetNativeWindow();
  std::unique_ptr<aura::Window> window(CreateChildWindow(parent_window));
  gfx::Rect window_bounds(2, 2, 3, 3);
  gfx::Rect widget_bounds(50, 50, 100, 75);
  gfx::Rect view_bounds(4, 4, 3, 3);
  window->SetBounds(window_bounds);
  widget->SetBounds(widget_bounds);
  widget->GetRootView()->SetBoundsRect(view_bounds);

  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  DOM::Node* parent_node = FindInRoot(parent_window, root.get());
  ASSERT_TRUE(parent_node);
  Array<DOM::Node>* parent_children = parent_node->getChildren(nullptr);
  ASSERT_TRUE(parent_children);

  CompareNodeBounds(parent_node, widget_bounds);
  CompareNodeBounds(parent_children->get(1), window_bounds);
  CompareNodeBounds(parent_children->get(0)->getChildren(nullptr)->get(0),
                    view_bounds);
}

TEST_F(AshDevToolsTest, WindowWidgetViewStyleSheetChanged) {
  std::unique_ptr<views::Widget> widget(
      CreateTestWidget(gfx::Rect(1, 1, 1, 1)));
  aura::Window* widget_window = widget->GetNativeWindow();
  std::unique_ptr<aura::Window> child(CreateChildWindow(widget_window));

  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  gfx::Rect child_bounds(2, 2, 3, 3);
  gfx::Rect widget_bounds(10, 10, 150, 160);
  gfx::Rect view_bounds(4, 4, 3, 3);
  child->SetBounds(child_bounds);
  widget->SetBounds(widget_bounds);
  widget->GetRootView()->SetBoundsRect(view_bounds);

  DOM::Node* widget_node = FindInRoot(widget_window, root.get());
  ASSERT_TRUE(widget_node);
  Array<DOM::Node>* widget_node_children = widget_node->getChildren(nullptr);
  ASSERT_TRUE(widget_node_children);

  EXPECT_EQ(1, GetStyleSheetChangedCount(widget_node->getNodeId()));
  EXPECT_EQ(
      1, GetStyleSheetChangedCount(widget_node_children->get(1)->getNodeId()));
  EXPECT_EQ(2,
            GetStyleSheetChangedCount(widget_node_children->get(0)
                                          ->getChildren(nullptr)
                                          ->get(0)
                                          ->getNodeId()));
}

TEST_F(AshDevToolsTest, WindowWidgetViewSetStyleText) {
  std::unique_ptr<views::Widget> widget(
      CreateTestWidget(gfx::Rect(0, 0, 400, 400)));
  aura::Window* parent_window = widget->GetNativeWindow();
  std::unique_ptr<aura::Window> window(CreateChildWindow(parent_window));
  views::View* root_view = widget->GetRootView();

  std::unique_ptr<ui::devtools::protocol::DOM::Node> root;
  dom_agent()->getDocument(&root);

  DOM::Node* parent_node = FindInRoot(parent_window, root.get());
  ASSERT_TRUE(parent_node);
  Array<DOM::Node>* parent_children = parent_node->getChildren(nullptr);
  ASSERT_TRUE(parent_children);

  // Test different combinations on window node
  DOM::Node* window_node = parent_children->get(1);

  SetStyleTexts(window_node,
                "x: 25; y:35; width: 5; height: 20; visibility: 1;", true);
  EXPECT_EQ(gfx::Rect(25, 35, 5, 20), window->bounds());
  EXPECT_TRUE(window->IsVisible());

  SetStyleTexts(window_node, "test_nothing_happens:1;", false);
  EXPECT_EQ(gfx::Rect(25, 35, 5, 20), window->bounds());  // Not changed

  SetStyleTexts(window_node, "\nheight: 10;\nvisibility: 0;\n", true);
  EXPECT_EQ(gfx::Rect(25, 35, 5, 10), window->bounds());
  EXPECT_FALSE(window->IsVisible());

  SetStyleTexts(window_node, "\nx: 10; y: 23; width: 52;\n  ", true);
  EXPECT_EQ(gfx::Rect(10, 23, 52, 10), window->bounds());

  // Test different combinations on widget node
  DOM::Node* widget_node = parent_children->get(0);

  SetStyleTexts(widget_node,
                "x: 25; y:35; width: 53; height: 64; visibility: 0;", true);
  EXPECT_EQ(gfx::Rect(25, 35, 53, 64), widget->GetRestoredBounds());
  EXPECT_FALSE(widget->IsVisible());

  SetStyleTexts(widget_node, "test_nothing_happens:1;", false);
  EXPECT_EQ(gfx::Rect(25, 35, 53, 64),
            widget->GetRestoredBounds());  // Not changed

  SetStyleTexts(widget_node, "\nheight: 123;\nvisibility: 1;\n", true);
  EXPECT_EQ(gfx::Rect(25, 35, 53, 123), widget->GetRestoredBounds());
  EXPECT_TRUE(widget->IsVisible());

  SetStyleTexts(widget_node, "\nx: 10; y: 23; width: 98;\n  ", true);
  EXPECT_EQ(gfx::Rect(10, 23, 98, 123), widget->GetRestoredBounds());

  // Test different combinations on view node
  DOM::Node* root_view_node = widget_node->getChildren(nullptr)->get(0);

  SetStyleTexts(root_view_node,
                "x: 25; y:35; width: 45; height: 20; visibility: 0;", true);
  EXPECT_EQ(gfx::Rect(25, 35, 45, 20), root_view->bounds());
  EXPECT_FALSE(root_view->visible());

  SetStyleTexts(root_view_node, "test_nothing_happens:1;", false);
  EXPECT_EQ(gfx::Rect(25, 35, 45, 20), root_view->bounds());  // Not changed

  SetStyleTexts(root_view_node, "\nheight: 73;\n  ", true);
  EXPECT_EQ(gfx::Rect(25, 35, 45, 73), root_view->bounds());

  SetStyleTexts(root_view_node, "\nx: 10; y: 23; width: 52;\nvisibility: 1;\n",
                true);
  EXPECT_EQ(gfx::Rect(10, 23, 52, 73), root_view->bounds());
  EXPECT_TRUE(root_view->visible());
}

}  // namespace ash
