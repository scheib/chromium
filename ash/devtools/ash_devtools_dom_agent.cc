// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/devtools/ash_devtools_dom_agent.h"

#include "ash/devtools/ui_element.h"
#include "ash/devtools/view_element.h"
#include "ash/devtools/widget_element.h"
#include "ash/devtools/window_element.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/root_window_controller.h"
#include "ash/shell.h"
#include "components/ui_devtools/devtools_server.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/aura/window.h"
#include "ui/display/display.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/window_util.h"

namespace ash {
namespace devtools {
namespace {

using namespace ui::devtools::protocol;
// TODO(mhashmi): Make ids reusable

std::unique_ptr<DOM::Node> BuildNode(
    const std::string& name,
    std::unique_ptr<Array<std::string>> attributes,
    std::unique_ptr<Array<DOM::Node>> children,
    int node_ids) {
  constexpr int kDomElementNodeType = 1;
  std::unique_ptr<DOM::Node> node = DOM::Node::create()
                                        .setNodeId(node_ids)
                                        .setNodeName(name)
                                        .setNodeType(kDomElementNodeType)
                                        .setAttributes(std::move(attributes))
                                        .build();
  node->setChildNodeCount(children->length());
  node->setChildren(std::move(children));
  return node;
}

// TODO(thanhph): Move this function to UIElement::GetAttributes().
std::unique_ptr<Array<std::string>> GetAttributes(UIElement* ui_element) {
  std::unique_ptr<Array<std::string>> attributes = Array<std::string>::create();
  attributes->addItem("name");
  switch (ui_element->type()) {
    case UIElementType::WINDOW: {
      aura::Window* window =
          UIElement::GetBackingElement<aura::Window, WindowElement>(ui_element);
      attributes->addItem(window->GetName());
      attributes->addItem("active");
      attributes->addItem(::wm::IsActiveWindow(window) ? "true" : "false");
      break;
    }
    case UIElementType::WIDGET: {
      views::Widget* widget =
          UIElement::GetBackingElement<views::Widget, WidgetElement>(
              ui_element);
      attributes->addItem(widget->GetName());
      attributes->addItem("active");
      attributes->addItem(widget->IsActive() ? "true" : "false");
      break;
    }
    case UIElementType::VIEW: {
      attributes->addItem(
          UIElement::GetBackingElement<views::View, ViewElement>(ui_element)
              ->GetClassName());
      break;
    }
    default:
      DCHECK(false);
  }
  return attributes;
}

int MaskColor(int value) {
  return value & 0xff;
}

SkColor RGBAToSkColor(DOM::RGBA* rgba) {
  if (!rgba)
    return SkColorSetARGB(0, 0, 0, 0);
  // Default alpha value is 0 (not visible) and need to convert alpha decimal
  // percentage value to hex
  return SkColorSetARGB(MaskColor(static_cast<int>(rgba->getA(0) * 255)),
                        MaskColor(rgba->getR()), MaskColor(rgba->getG()),
                        MaskColor(rgba->getB()));
}

views::Widget* GetWidgetFromWindow(aura::Window* window) {
  return views::Widget::GetWidgetForNativeView(window);
}

std::unique_ptr<DOM::Node> BuildDomNodeFromUIElement(UIElement* root) {
  std::unique_ptr<Array<DOM::Node>> children = Array<DOM::Node>::create();
  for (auto* it : root->children())
    children->addItem(BuildDomNodeFromUIElement(it));

  constexpr int kDomElementNodeType = 1;
  std::unique_ptr<DOM::Node> node = DOM::Node::create()
                                        .setNodeId(root->node_id())
                                        .setNodeName(root->GetTypeName())
                                        .setNodeType(kDomElementNodeType)
                                        .setAttributes(GetAttributes(root))
                                        .build();
  node->setChildNodeCount(children->length());
  node->setChildren(std::move(children));
  return node;
}

}  // namespace

AshDevToolsDOMAgent::AshDevToolsDOMAgent() : is_building_tree_(false) {}

AshDevToolsDOMAgent::~AshDevToolsDOMAgent() {
  Reset();
}

ui::devtools::protocol::Response AshDevToolsDOMAgent::disable() {
  Reset();
  return ui::devtools::protocol::Response::OK();
}

ui::devtools::protocol::Response AshDevToolsDOMAgent::getDocument(
    std::unique_ptr<ui::devtools::protocol::DOM::Node>* out_root) {
  *out_root = BuildInitialTree();
  return ui::devtools::protocol::Response::OK();
}

ui::devtools::protocol::Response AshDevToolsDOMAgent::highlightNode(
    std::unique_ptr<ui::devtools::protocol::DOM::HighlightConfig>
        highlight_config,
    ui::devtools::protocol::Maybe<int> node_id) {
  return HighlightNode(std::move(highlight_config), node_id.fromJust());
}

ui::devtools::protocol::Response AshDevToolsDOMAgent::hideHighlight() {
  if (widget_for_highlighting_ && widget_for_highlighting_->IsVisible())
    widget_for_highlighting_->Hide();
  return ui::devtools::protocol::Response::OK();
}

void AshDevToolsDOMAgent::OnUIElementAdded(UIElement* parent,
                                           UIElement* child) {
  // When parent is null, only need to update |node_id_to_ui_element_|.
  if (!parent) {
    node_id_to_ui_element_[child->node_id()] = child;
    return;
  }
  // If tree is being built, don't add child to dom tree again.
  if (is_building_tree_)
    return;
  DCHECK(node_id_to_ui_element_.count(parent->node_id()));

  const auto& children = parent->children();
  auto iter = std::find(children.begin(), children.end(), child);
  int prev_node_id =
      (iter == children.end() - 1) ? 0 : (*std::next(iter))->node_id();
  frontend()->childNodeInserted(parent->node_id(), prev_node_id,
                                BuildTreeForUIElement(child));
}

void AshDevToolsDOMAgent::OnUIElementReordered(UIElement* parent,
                                               UIElement* child) {
  DCHECK(node_id_to_ui_element_.count(parent->node_id()));

  const auto& children = parent->children();
  auto iter = std::find(children.begin(), children.end(), child);
  int prev_node_id =
      (iter == children.begin()) ? 0 : (*std::prev(iter))->node_id();
  RemoveDomNode(child);
  frontend()->childNodeInserted(parent->node_id(), prev_node_id,
                                BuildDomNodeFromUIElement(child));
}

void AshDevToolsDOMAgent::OnUIElementRemoved(UIElement* ui_element) {
  DCHECK(node_id_to_ui_element_.count(ui_element->node_id()));

  RemoveDomNode(ui_element);
  node_id_to_ui_element_.erase(ui_element->node_id());
}

void AshDevToolsDOMAgent::OnUIElementBoundsChanged(UIElement* ui_element) {
  for (auto& observer : observers_)
    observer.OnNodeBoundsChanged(ui_element->node_id());
}

bool AshDevToolsDOMAgent::IsHighlightingWindow(aura::Window* window) {
  return widget_for_highlighting_ &&
         GetWidgetFromWindow(window) == widget_for_highlighting_.get();
}

void AshDevToolsDOMAgent::AddObserver(AshDevToolsDOMAgentObserver* observer) {
  observers_.AddObserver(observer);
}

void AshDevToolsDOMAgent::RemoveObserver(
    AshDevToolsDOMAgentObserver* observer) {
  observers_.RemoveObserver(observer);
}

UIElement* AshDevToolsDOMAgent::GetElementFromNodeId(int node_id) {
  return node_id_to_ui_element_[node_id];
}

void AshDevToolsDOMAgent::OnNodeBoundsChanged(int node_id) {
  for (auto& observer : observers_)
    observer.OnNodeBoundsChanged(node_id);
}

std::unique_ptr<ui::devtools::protocol::DOM::Node>
AshDevToolsDOMAgent::BuildInitialTree() {
  is_building_tree_ = true;
  std::unique_ptr<Array<DOM::Node>> children = Array<DOM::Node>::create();

  // TODO(thanhph): Root of UIElement tree shoudn't be WindowElement
  // but maybe a new different element type.
  window_element_root_ = new WindowElement(nullptr, this, nullptr);

  for (aura::Window* window : Shell::GetAllRootWindows()) {
    UIElement* window_element =
        new WindowElement(window, this, window_element_root_);

    children->addItem(BuildTreeForUIElement(window_element));
    window_element_root_->AddChild(window_element);
  }
  std::unique_ptr<ui::devtools::protocol::DOM::Node> root_node = BuildNode(
      "root", nullptr, std::move(children), window_element_root_->node_id());
  is_building_tree_ = false;
  return root_node;
}

std::unique_ptr<ui::devtools::protocol::DOM::Node>
AshDevToolsDOMAgent::BuildTreeForUIElement(UIElement* ui_element) {
  if (ui_element->type() == UIElementType::WINDOW) {
    return BuildTreeForWindow(
        ui_element,
        UIElement::GetBackingElement<aura::Window, WindowElement>(ui_element));
  } else if (ui_element->type() == UIElementType::WIDGET) {
    return BuildTreeForRootWidget(
        ui_element,
        UIElement::GetBackingElement<views::Widget, WidgetElement>(ui_element));
  } else if (ui_element->type() == UIElementType::VIEW) {
    return BuildTreeForView(
        ui_element,
        UIElement::GetBackingElement<views::View, ViewElement>(ui_element));
  }
  return nullptr;
}

std::unique_ptr<DOM::Node> AshDevToolsDOMAgent::BuildTreeForWindow(
    UIElement* window_element_root,
    aura::Window* window) {
  std::unique_ptr<Array<DOM::Node>> children = Array<DOM::Node>::create();
  views::Widget* widget = GetWidgetFromWindow(window);
  if (widget) {
    UIElement* widget_element =
        new WidgetElement(widget, this, window_element_root);

    children->addItem(BuildTreeForRootWidget(widget_element, widget));
    window_element_root->AddChild(widget_element);
  }
  for (aura::Window* child : window->children()) {
    UIElement* window_element =
        new WindowElement(child, this, window_element_root);

    children->addItem(BuildTreeForWindow(window_element, child));
    window_element_root->AddChild(window_element);
  }
  std::unique_ptr<ui::devtools::protocol::DOM::Node> node =
      BuildNode("Window", GetAttributes(window_element_root),
                std::move(children), window_element_root->node_id());
  return node;
}

std::unique_ptr<DOM::Node> AshDevToolsDOMAgent::BuildTreeForRootWidget(
    UIElement* widget_element,
    views::Widget* widget) {
  std::unique_ptr<Array<DOM::Node>> children = Array<DOM::Node>::create();

  UIElement* view_element =
      new ViewElement(widget->GetRootView(), this, widget_element);

  children->addItem(BuildTreeForView(view_element, widget->GetRootView()));
  widget_element->AddChild(view_element);

  std::unique_ptr<ui::devtools::protocol::DOM::Node> node =
      BuildNode("Widget", GetAttributes(widget_element), std::move(children),
                widget_element->node_id());
  return node;
}

std::unique_ptr<DOM::Node> AshDevToolsDOMAgent::BuildTreeForView(
    UIElement* view_element,
    views::View* view) {
  std::unique_ptr<Array<DOM::Node>> children = Array<DOM::Node>::create();

  for (auto* child : view->GetChildrenInZOrder()) {
    UIElement* view_element_child = new ViewElement(child, this, view_element);

    children->addItem(BuildTreeForView(view_element_child, child));
    view_element->AddChild(view_element_child);
  }
  std::unique_ptr<ui::devtools::protocol::DOM::Node> node =
      BuildNode("View", GetAttributes(view_element), std::move(children),
                view_element->node_id());
  return node;
}

void AshDevToolsDOMAgent::RemoveDomNode(UIElement* ui_element) {
  for (auto* child_element : ui_element->children())
    RemoveDomNode(child_element);
  frontend()->childNodeRemoved(ui_element->parent()->node_id(),
                               ui_element->node_id());
}

void AshDevToolsDOMAgent::Reset() {
  is_building_tree_ = false;
  widget_for_highlighting_.reset();
  delete window_element_root_;
  node_id_to_ui_element_.clear();
  observers_.Clear();
}

void AshDevToolsDOMAgent::InitializeHighlightingWidget() {
  DCHECK(!widget_for_highlighting_);
  widget_for_highlighting_.reset(new views::Widget);
  views::Widget::InitParams params;
  params.type = views::Widget::InitParams::TYPE_WINDOW_FRAMELESS;
  params.activatable = views::Widget::InitParams::ACTIVATABLE_NO;
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.opacity = views::Widget::InitParams::WindowOpacity::TRANSLUCENT_WINDOW;
  params.name = "HighlightingWidget";
  Shell::GetPrimaryRootWindowController()
      ->ConfigureWidgetInitParamsForContainer(widget_for_highlighting_.get(),
                                              kShellWindowId_OverlayContainer,
                                              &params);
  params.keep_on_top = true;
  params.accept_events = false;
  widget_for_highlighting_->Init(params);
}

void AshDevToolsDOMAgent::UpdateHighlight(
    const std::pair<aura::Window*, gfx::Rect>& window_and_bounds,
    SkColor background,
    SkColor border) {
  constexpr int kBorderThickness = 1;
  views::View* root_view = widget_for_highlighting_->GetRootView();
  root_view->SetBorder(views::CreateSolidBorder(kBorderThickness, border));
  root_view->set_background(
      views::Background::CreateSolidBackground(background));
  display::Display display =
      display::Screen::GetScreen()->GetDisplayNearestWindow(
          window_and_bounds.first);
  widget_for_highlighting_->GetNativeWindow()->SetBoundsInScreen(
      window_and_bounds.second, display);
}

ui::devtools::protocol::Response AshDevToolsDOMAgent::HighlightNode(
    std::unique_ptr<ui::devtools::protocol::DOM::HighlightConfig>
        highlight_config,
    int node_id) {
  if (!widget_for_highlighting_)
    InitializeHighlightingWidget();

  std::pair<aura::Window*, gfx::Rect> window_and_bounds =
      node_id_to_ui_element_.count(node_id)
          ? node_id_to_ui_element_[node_id]->GetNodeWindowAndBounds()
          : std::make_pair<aura::Window*, gfx::Rect>(nullptr, gfx::Rect());

  if (!window_and_bounds.first) {
    return ui::devtools::protocol::Response::Error(
        "No node found with that id");
  }
  SkColor border_color =
      RGBAToSkColor(highlight_config->getBorderColor(nullptr));
  SkColor content_color =
      RGBAToSkColor(highlight_config->getContentColor(nullptr));
  UpdateHighlight(window_and_bounds, content_color, border_color);

  if (!widget_for_highlighting_->IsVisible())
    widget_for_highlighting_->Show();

  return ui::devtools::protocol::Response::OK();
}

}  // namespace devtools
}  // namespace ash
