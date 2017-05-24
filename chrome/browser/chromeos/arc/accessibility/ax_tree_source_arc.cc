// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/accessibility/ax_tree_source_arc.h"

#include <string>

#include "chrome/browser/extensions/api/automation_internal/automation_event_router.h"
#include "chrome/browser/ui/aura/accessibility/automation_manager_aura.h"
#include "chrome/common/extensions/chrome_extension_messages.h"
#include "components/exo/wm_helper.h"
#include "ui/accessibility/platform/ax_android_constants.h"
#include "ui/aura/window.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

ui::AXEvent ToAXEvent(arc::mojom::AccessibilityEventType arc_event_type) {
  switch (arc_event_type) {
    case arc::mojom::AccessibilityEventType::VIEW_FOCUSED:
    case arc::mojom::AccessibilityEventType::VIEW_ACCESSIBILITY_FOCUSED:
      return ui::AX_EVENT_FOCUS;
    case arc::mojom::AccessibilityEventType::VIEW_CLICKED:
    case arc::mojom::AccessibilityEventType::VIEW_LONG_CLICKED:
      return ui::AX_EVENT_CLICKED;
    case arc::mojom::AccessibilityEventType::VIEW_TEXT_CHANGED:
      return ui::AX_EVENT_TEXT_CHANGED;
    case arc::mojom::AccessibilityEventType::VIEW_TEXT_SELECTION_CHANGED:
      return ui::AX_EVENT_TEXT_SELECTION_CHANGED;
    case arc::mojom::AccessibilityEventType::WINDOW_STATE_CHANGED:
    case arc::mojom::AccessibilityEventType::NOTIFICATION_STATE_CHANGED:
    case arc::mojom::AccessibilityEventType::WINDOW_CONTENT_CHANGED:
    case arc::mojom::AccessibilityEventType::WINDOWS_CHANGED:
      return ui::AX_EVENT_LAYOUT_COMPLETE;
    case arc::mojom::AccessibilityEventType::VIEW_HOVER_ENTER:
      return ui::AX_EVENT_HOVER;
    case arc::mojom::AccessibilityEventType::ANNOUNCEMENT:
      return ui::AX_EVENT_ALERT;
    case arc::mojom::AccessibilityEventType::VIEW_SELECTED:
    case arc::mojom::AccessibilityEventType::VIEW_HOVER_EXIT:
    case arc::mojom::AccessibilityEventType::TOUCH_EXPLORATION_GESTURE_START:
    case arc::mojom::AccessibilityEventType::TOUCH_EXPLORATION_GESTURE_END:
    case arc::mojom::AccessibilityEventType::VIEW_SCROLLED:
    case arc::mojom::AccessibilityEventType::
        VIEW_TEXT_TRAVERSED_AT_MOVEMENT_GRANULARITY:
    case arc::mojom::AccessibilityEventType::GESTURE_DETECTION_START:
    case arc::mojom::AccessibilityEventType::GESTURE_DETECTION_END:
    case arc::mojom::AccessibilityEventType::TOUCH_INTERACTION_START:
    case arc::mojom::AccessibilityEventType::TOUCH_INTERACTION_END:
    case arc::mojom::AccessibilityEventType::VIEW_CONTEXT_CLICKED:
    case arc::mojom::AccessibilityEventType::ASSIST_READING_CONTEXT:
      return ui::AX_EVENT_CHILDREN_CHANGED;
    default:
      return ui::AX_EVENT_CHILDREN_CHANGED;
  }
  return ui::AX_EVENT_CHILDREN_CHANGED;
}

const gfx::Rect GetBounds(arc::mojom::AccessibilityNodeInfoData* node) {
  exo::WMHelper* wm_helper = exo::WMHelper::GetInstance();
  if (!wm_helper)
    return gfx::Rect();

  aura::Window* focused_window = wm_helper->GetFocusedWindow();
  gfx::Rect bounds_in_screen = node->bounds_in_screen;
  if (focused_window) {
    aura::Window* toplevel_window = focused_window->GetToplevelWindow();
    return gfx::ScaleToEnclosingRect(
        bounds_in_screen,
        1.0f / toplevel_window->layer()->device_scale_factor());
  }
  return bounds_in_screen;
}

bool GetBooleanProperty(arc::mojom::AccessibilityNodeInfoData* node,
                        arc::mojom::AccessibilityBooleanProperty prop) {
  if (!node->boolean_properties)
    return false;

  auto it = node->boolean_properties->find(prop);
  if (it == node->boolean_properties->end())
    return false;

  return it->second;
}

bool GetIntProperty(arc::mojom::AccessibilityNodeInfoData* node,
                    arc::mojom::AccessibilityIntProperty prop,
                    int32_t* out_value) {
  if (!node->int_properties)
    return false;

  auto it = node->int_properties->find(prop);
  if (it == node->int_properties->end())
    return false;

  *out_value = it->second;
  return true;
}

bool GetStringProperty(arc::mojom::AccessibilityNodeInfoData* node,
                       arc::mojom::AccessibilityStringProperty prop,
                       std::string* out_value) {
  if (!node->string_properties)
    return false;

  auto it = node->string_properties->find(prop);
  if (it == node->string_properties->end())
    return false;

  *out_value = it->second;
  return true;
}

void PopulateAXRole(arc::mojom::AccessibilityNodeInfoData* node,
                    ui::AXNodeData* out_data) {
  std::string class_name;
  GetStringProperty(node, arc::mojom::AccessibilityStringProperty::CLASS_NAME,
                    &class_name);

#define MAP_ROLE(android_class_name, chrome_role) \
  if (class_name == android_class_name) {         \
    out_data->role = chrome_role;                 \
    return;                                       \
  }

  // These mappings were taken from accessibility utils (Android -> Chrome) and
  // BrowserAccessibilityAndroid. They do not completely match the above two
  // sources.
  MAP_ROLE(ui::kAXAbsListViewClassname, ui::AX_ROLE_LIST);
  MAP_ROLE(ui::kAXButtonClassname, ui::AX_ROLE_BUTTON);
  MAP_ROLE(ui::kAXCheckBoxClassname, ui::AX_ROLE_CHECK_BOX);
  MAP_ROLE(ui::kAXCheckedTextViewClassname, ui::AX_ROLE_STATIC_TEXT);
  MAP_ROLE(ui::kAXCompoundButtonClassname, ui::AX_ROLE_CHECK_BOX);
  MAP_ROLE(ui::kAXDialogClassname, ui::AX_ROLE_DIALOG);
  MAP_ROLE(ui::kAXEditTextClassname, ui::AX_ROLE_TEXT_FIELD);
  MAP_ROLE(ui::kAXGridViewClassname, ui::AX_ROLE_TABLE);
  MAP_ROLE(ui::kAXImageClassname, ui::AX_ROLE_IMAGE);
  if (GetBooleanProperty(node,
                         arc::mojom::AccessibilityBooleanProperty::CLICKABLE)) {
    MAP_ROLE(ui::kAXImageViewClassname, ui::AX_ROLE_BUTTON);
  } else {
    MAP_ROLE(ui::kAXImageViewClassname, ui::AX_ROLE_IMAGE);
  }
  MAP_ROLE(ui::kAXListViewClassname, ui::AX_ROLE_LIST);
  MAP_ROLE(ui::kAXMenuItemClassname, ui::AX_ROLE_MENU_ITEM);
  MAP_ROLE(ui::kAXPagerClassname, ui::AX_ROLE_SCROLL_AREA);
  MAP_ROLE(ui::kAXProgressBarClassname, ui::AX_ROLE_PROGRESS_INDICATOR);
  MAP_ROLE(ui::kAXRadioButtonClassname, ui::AX_ROLE_RADIO_BUTTON);
  MAP_ROLE(ui::kAXSeekBarClassname, ui::AX_ROLE_SLIDER);
  MAP_ROLE(ui::kAXSpinnerClassname, ui::AX_ROLE_POP_UP_BUTTON);
  MAP_ROLE(ui::kAXSwitchClassname, ui::AX_ROLE_SWITCH);
  MAP_ROLE(ui::kAXTabWidgetClassname, ui::AX_ROLE_TAB_LIST);
  MAP_ROLE(ui::kAXToggleButtonClassname, ui::AX_ROLE_TOGGLE_BUTTON);
  MAP_ROLE(ui::kAXViewClassname, ui::AX_ROLE_GENERIC_CONTAINER);
  MAP_ROLE(ui::kAXViewGroupClassname, ui::AX_ROLE_GROUP);
  MAP_ROLE(ui::kAXWebViewClassname, ui::AX_ROLE_WEB_VIEW);

#undef MAP_ROLE

  std::string text;
  GetStringProperty(node, arc::mojom::AccessibilityStringProperty::TEXT, &text);
  if (!text.empty())
    out_data->role = ui::AX_ROLE_STATIC_TEXT;
  else
    out_data->role = ui::AX_ROLE_GENERIC_CONTAINER;
}

void PopulateAXState(arc::mojom::AccessibilityNodeInfoData* node,
                     ui::AXNodeData* out_data) {
#define MAP_STATE(android_boolean_property, chrome_state) \
  if (GetBooleanProperty(node, android_boolean_property)) \
    out_data->AddState(chrome_state);

  using AXBooleanProperty = arc::mojom::AccessibilityBooleanProperty;

  // These mappings were taken from accessibility utils (Android -> Chrome) and
  // BrowserAccessibilityAndroid. They do not completely match the above two
  // sources.
  // The FOCUSABLE state is not mapped because Android places focusability on
  // many ancestor nodes.
  MAP_STATE(AXBooleanProperty::EDITABLE, ui::AX_STATE_EDITABLE);
  MAP_STATE(AXBooleanProperty::MULTI_LINE, ui::AX_STATE_MULTILINE);
  MAP_STATE(AXBooleanProperty::PASSWORD, ui::AX_STATE_PROTECTED);
  MAP_STATE(AXBooleanProperty::SELECTED, ui::AX_STATE_SELECTED);

#undef MAP_STATE

  if (GetBooleanProperty(node, AXBooleanProperty::CHECKABLE)) {
    const bool is_checked =
        GetBooleanProperty(node, AXBooleanProperty::CHECKED);
    const ui::AXCheckedState checked_state =
        is_checked ? ui::AX_CHECKED_STATE_TRUE : ui::AX_CHECKED_STATE_FALSE;
    out_data->AddIntAttribute(ui::AX_ATTR_CHECKED_STATE, checked_state);
  }

  if (!GetBooleanProperty(node, AXBooleanProperty::ENABLED))
    out_data->AddState(ui::AX_STATE_DISABLED);
}

}  // namespace

namespace arc {

// This class keeps focus on a |ShellSurface| without interfering with default
// focus management in |ShellSurface|. For example, touch causes the
// |ShellSurface| to lose focus to its ancestor containing View.
class AXTreeSourceArc::FocusStealer : public views::View {
 public:
  explicit FocusStealer(int32_t id) : id_(id) { set_owned_by_client(); }

  void Steal() {
    SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    RequestFocus();
  }

  // views::View overrides.
  void GetAccessibleNodeData(ui::AXNodeData* node_data) override {
    node_data->AddIntAttribute(ui::AX_ATTR_CHILD_TREE_ID, id_);
    node_data->role = ui::AX_ROLE_CLIENT;
  }

 private:
  const int32_t id_;
  DISALLOW_COPY_AND_ASSIGN(FocusStealer);
};

AXTreeSourceArc::AXTreeSourceArc(Delegate* delegate)
    : current_tree_serializer_(new AXTreeArcSerializer(this)),
      root_id_(-1),
      focused_node_id_(-1),
      delegate_(delegate),
      focus_stealer_(new FocusStealer(tree_id())) {}

AXTreeSourceArc::~AXTreeSourceArc() {
  Reset();
}

void AXTreeSourceArc::NotifyAccessibilityEvent(
    mojom::AccessibilityEventData* event_data) {
  tree_map_.clear();
  parent_map_.clear();
  root_id_ = -1;
  for (size_t i = 0; i < event_data->node_data.size(); ++i) {
    if (!event_data->node_data[i]->int_list_properties)
      continue;
    auto it = event_data->node_data[i]->int_list_properties->find(
        arc::mojom::AccessibilityIntListProperty::CHILD_NODE_IDS);
    if (it != event_data->node_data[i]->int_list_properties->end()) {
      for (size_t j = 0; j < it->second.size(); ++j)
        parent_map_[it->second[j]] = event_data->node_data[i]->id;
    }
  }

  for (size_t i = 0; i < event_data->node_data.size(); ++i) {
    int32_t id = event_data->node_data[i]->id;
    tree_map_[id] = event_data->node_data[i].get();
    if (parent_map_.find(id) == parent_map_.end()) {
      CHECK_EQ(-1, root_id_) << "Duplicated root";
      root_id_ = id;
    }
  }

  ExtensionMsg_AccessibilityEventParams params;
  params.event_type = ToAXEvent(event_data->event_type);

  if (params.event_type == ui::AX_EVENT_FOCUS)
    focused_node_id_ = event_data->source_id;

  params.tree_id = tree_id();
  params.id = event_data->source_id;

  current_tree_serializer_->SerializeChanges(GetFromId(event_data->source_id),
                                             &params.update);

  extensions::AutomationEventRouter* router =
      extensions::AutomationEventRouter::GetInstance();
  router->DispatchAccessibilityEvent(params);
}

void AXTreeSourceArc::Focus(aura::Window* window) {
  views::Widget* widget = views::Widget::GetWidgetForNativeView(window);
  if (!widget || !widget->GetContentsView())
    return;

  views::View* view = widget->GetContentsView();
  if (!view->Contains(focus_stealer_.get()))
    view->AddChildView(focus_stealer_.get());
  focus_stealer_->Steal();
}

bool AXTreeSourceArc::GetTreeData(ui::AXTreeData* data) const {
  data->tree_id = tree_id();
  if (focused_node_id_ >= 0)
    data->focus_id = focused_node_id_;
  return true;
}

mojom::AccessibilityNodeInfoData* AXTreeSourceArc::GetRoot() const {
  mojom::AccessibilityNodeInfoData* root = GetFromId(root_id_);
  return root;
}

mojom::AccessibilityNodeInfoData* AXTreeSourceArc::GetFromId(int32_t id) const {
  auto it = tree_map_.find(id);
  if (it == tree_map_.end())
    return nullptr;
  return it->second;
}

int32_t AXTreeSourceArc::GetId(mojom::AccessibilityNodeInfoData* node) const {
  if (!node)
    return -1;
  return node->id;
}

void AXTreeSourceArc::GetChildren(
    mojom::AccessibilityNodeInfoData* node,
    std::vector<mojom::AccessibilityNodeInfoData*>* out_children) const {
  if (!node || !node->int_list_properties)
    return;

  auto it = node->int_list_properties->find(
      arc::mojom::AccessibilityIntListProperty::CHILD_NODE_IDS);
  if (it == node->int_list_properties->end())
    return;

  for (size_t i = 0; i < it->second.size(); ++i) {
    out_children->push_back(GetFromId(it->second[i]));
  }
}

mojom::AccessibilityNodeInfoData* AXTreeSourceArc::GetParent(
    mojom::AccessibilityNodeInfoData* node) const {
  if (!node)
    return nullptr;
  auto it = parent_map_.find(node->id);
  if (it != parent_map_.end())
    return GetFromId(it->second);
  return nullptr;
}

bool AXTreeSourceArc::IsValid(mojom::AccessibilityNodeInfoData* node) const {
  return node;
}

bool AXTreeSourceArc::IsEqual(mojom::AccessibilityNodeInfoData* node1,
                              mojom::AccessibilityNodeInfoData* node2) const {
  if (!node1 || !node2)
    return false;
  return node1->id == node2->id;
}

mojom::AccessibilityNodeInfoData* AXTreeSourceArc::GetNull() const {
  return nullptr;
}

void AXTreeSourceArc::SerializeNode(mojom::AccessibilityNodeInfoData* node,
                                    ui::AXNodeData* out_data) const {
  if (!node)
    return;
  out_data->id = node->id;

  using AXIntProperty = arc::mojom::AccessibilityIntProperty;
  using AXStringProperty = arc::mojom::AccessibilityStringProperty;
  std::string text;
  if (GetStringProperty(node, AXStringProperty::TEXT, &text))
    out_data->SetName(text);
  else if (GetStringProperty(node, AXStringProperty::CONTENT_DESCRIPTION,
                             &text))
    out_data->SetName(text);

  int32_t id = node->id;
  if (id == root_id_)
    out_data->role = ui::AX_ROLE_ROOT_WEB_AREA;
  else
    PopulateAXRole(node, out_data);

  PopulateAXState(node, out_data);

  const gfx::Rect bounds_in_screen = GetBounds(node);
  out_data->location.SetRect(bounds_in_screen.x(), bounds_in_screen.y(),
                             bounds_in_screen.width(),
                             bounds_in_screen.height());

  if (out_data->role == ui::AX_ROLE_TEXT_FIELD && !text.empty())
    out_data->AddStringAttribute(ui::AX_ATTR_VALUE, text);

  // Integer properties.
  int32_t val;
  if (GetIntProperty(node, AXIntProperty::TEXT_SELECTION_START, &val) &&
      val >= 0)
    out_data->AddIntAttribute(ui::AX_ATTR_TEXT_SEL_START, val);

  if (GetIntProperty(node, AXIntProperty::TEXT_SELECTION_END, &val) && val >= 0)
    out_data->AddIntAttribute(ui::AX_ATTR_TEXT_SEL_END, val);
}

void AXTreeSourceArc::PerformAction(const ui::AXActionData& data) {
  delegate_->OnAction(data);
}

void AXTreeSourceArc::Reset() {
  tree_map_.clear();
  parent_map_.clear();
  current_tree_serializer_.reset(new AXTreeArcSerializer(this));
  root_id_ = -1;
  focused_node_id_ = -1;
  extensions::AutomationEventRouter* router =
      extensions::AutomationEventRouter::GetInstance();
  if (!router)
    return;

  router->DispatchTreeDestroyedEvent(tree_id(), nullptr);
}

}  // namespace arc
