// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DEVTOOLS_ASH_DEVTOOLS_DOM_AGENT_H_
#define ASH_DEVTOOLS_ASH_DEVTOOLS_DOM_AGENT_H_

#include "ash/ash_export.h"
#include "ash/devtools/ui_element_delegate.h"
#include "base/macros.h"
#include "components/ui_devtools/DOM.h"
#include "components/ui_devtools/devtools_base_agent.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace aura {
class Window;
}

namespace ash {
namespace devtools {

class UIElement;

class ASH_EXPORT AshDevToolsDOMAgentObserver {
 public:
  // TODO(thanhph): Use UIElement* as input argument instead.
  virtual void OnNodeBoundsChanged(int node_id) = 0;
};

class ASH_EXPORT AshDevToolsDOMAgent
    : public NON_EXPORTED_BASE(ui::devtools::UiDevToolsBaseAgent<
                               ui::devtools::protocol::DOM::Metainfo>),
      public UIElementDelegate {
 public:
  AshDevToolsDOMAgent();
  ~AshDevToolsDOMAgent() override;

  // DOM::Backend:
  ui::devtools::protocol::Response disable() override;
  ui::devtools::protocol::Response getDocument(
      std::unique_ptr<ui::devtools::protocol::DOM::Node>* out_root) override;
  ui::devtools::protocol::Response highlightNode(
      std::unique_ptr<ui::devtools::protocol::DOM::HighlightConfig>
          highlight_config,
      ui::devtools::protocol::Maybe<int> node_id) override;
  ui::devtools::protocol::Response hideHighlight() override;

  // UIElementDelegate:
  void OnUIElementAdded(UIElement* parent, UIElement* child) override;
  void OnUIElementReordered(UIElement* parent, UIElement* child) override;
  void OnUIElementRemoved(UIElement* ui_element) override;
  void OnUIElementBoundsChanged(UIElement* ui_element) override;
  bool IsHighlightingWindow(aura::Window* window) override;

  void AddObserver(AshDevToolsDOMAgentObserver* observer);
  void RemoveObserver(AshDevToolsDOMAgentObserver* observer);
  UIElement* GetElementFromNodeId(int node_id);
  UIElement* window_element_root() const { return window_element_root_; };

 private:
  void OnNodeBoundsChanged(int node_id);

  std::unique_ptr<ui::devtools::protocol::DOM::Node> BuildInitialTree();
  std::unique_ptr<ui::devtools::protocol::DOM::Node> BuildTreeForUIElement(
      UIElement* ui_element);
  std::unique_ptr<ui::devtools::protocol::DOM::Node> BuildTreeForWindow(
      UIElement* window_element_root,
      aura::Window* window);
  std::unique_ptr<ui::devtools::protocol::DOM::Node> BuildTreeForRootWidget(
      UIElement* widget_element,
      views::Widget* widget);
  std::unique_ptr<ui::devtools::protocol::DOM::Node> BuildTreeForView(
      UIElement* view_element,
      views::View* view);
  void RemoveDomNode(UIElement* ui_element);
  void Reset();
  void InitializeHighlightingWidget();
  void UpdateHighlight(
      const std::pair<aura::Window*, gfx::Rect>& window_and_bounds,
      SkColor background,
      SkColor border);
  ui::devtools::protocol::Response HighlightNode(
      std::unique_ptr<ui::devtools::protocol::DOM::HighlightConfig>
          highlight_config,
      int node_id);

  bool is_building_tree_;
  UIElement* window_element_root_;
  std::unordered_map<int, UIElement*> node_id_to_ui_element_;
  std::unique_ptr<views::Widget> widget_for_highlighting_;
  base::ObserverList<AshDevToolsDOMAgentObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(AshDevToolsDOMAgent);
};

}  // namespace devtools
}  // namespace ash

#endif  // ASH_DEVTOOLS_ASH_DEVTOOLS_DOM_AGENT_H_
