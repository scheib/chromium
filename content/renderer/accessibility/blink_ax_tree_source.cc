// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/accessibility/blink_ax_tree_source.h"

#include <stddef.h>

#include <set>

#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "content/common/accessibility_messages.h"
#include "content/renderer/accessibility/blink_ax_enum_conversion.h"
#include "content/renderer/accessibility/render_accessibility_impl.h"
#include "content/renderer/browser_plugin/browser_plugin.h"
#include "content/renderer/render_frame_impl.h"
#include "content/renderer/render_frame_proxy.h"
#include "content/renderer/render_view_impl.h"
#include "content/renderer/web_frame_utils.h"
#include "third_party/WebKit/public/platform/WebFloatRect.h"
#include "third_party/WebKit/public/platform/WebRect.h"
#include "third_party/WebKit/public/platform/WebSize.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/platform/WebVector.h"
#include "third_party/WebKit/public/web/WebAXEnums.h"
#include "third_party/WebKit/public/web/WebAXObject.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebElement.h"
#include "third_party/WebKit/public/web/WebFormControlElement.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebNode.h"
#include "third_party/WebKit/public/web/WebPlugin.h"
#include "third_party/WebKit/public/web/WebPluginContainer.h"
#include "third_party/WebKit/public/web/WebView.h"

using base::ASCIIToUTF16;
using base::UTF16ToUTF8;
using blink::WebAXObject;
using blink::WebDocument;
using blink::WebElement;
using blink::WebFloatRect;
using blink::WebFrame;
using blink::WebLocalFrame;
using blink::WebNode;
using blink::WebPlugin;
using blink::WebPluginContainer;
using blink::WebVector;
using blink::WebView;

namespace content {

namespace {

void AddIntListAttributeFromWebObjects(ui::AXIntListAttribute attr,
                                       const WebVector<WebAXObject>& objects,
                                       AXContentNodeData* dst) {
  std::vector<int32_t> ids;
  for (size_t i = 0; i < objects.size(); i++)
    ids.push_back(objects[i].AxID());
  if (!ids.empty())
    dst->AddIntListAttribute(attr, ids);
}

class AXContentNodeDataSparseAttributeAdapter
    : public blink::WebAXSparseAttributeClient {
 public:
  AXContentNodeDataSparseAttributeAdapter(AXContentNodeData* dst) : dst_(dst) {
    DCHECK(dst_);
  }
  ~AXContentNodeDataSparseAttributeAdapter() override {}

 private:
  AXContentNodeData* dst_;

  void AddBoolAttribute(blink::WebAXBoolAttribute attribute,
                        bool value) override {
    NOTREACHED();
  }

  void AddStringAttribute(blink::WebAXStringAttribute attribute,
                          const blink::WebString& value) override {
    switch (attribute) {
      case blink::WebAXStringAttribute::kAriaKeyShortcuts:
        dst_->AddStringAttribute(ui::AX_ATTR_KEY_SHORTCUTS, value.Utf8());
        break;
      case blink::WebAXStringAttribute::kAriaRoleDescription:
        dst_->AddStringAttribute(ui::AX_ATTR_ROLE_DESCRIPTION, value.Utf8());
        break;
      default:
        NOTREACHED();
    }
  }

  void AddObjectAttribute(blink::WebAXObjectAttribute attribute,
                          const blink::WebAXObject& value) override {
    switch (attribute) {
      case blink::WebAXObjectAttribute::kAriaActiveDescendant:
        dst_->AddIntAttribute(ui::AX_ATTR_ACTIVEDESCENDANT_ID, value.AxID());
        break;
      case blink::WebAXObjectAttribute::kAriaErrorMessage:
        dst_->AddIntAttribute(ui::AX_ATTR_ERRORMESSAGE_ID, value.AxID());
        break;
      default:
        NOTREACHED();
    }
  }

  void AddObjectVectorAttribute(
      blink::WebAXObjectVectorAttribute attribute,
      const blink::WebVector<WebAXObject>& value) override {
    switch (attribute) {
      case blink::WebAXObjectVectorAttribute::kAriaControls:
        AddIntListAttributeFromWebObjects(ui::AX_ATTR_CONTROLS_IDS, value,
                                          dst_);
        break;
      case blink::WebAXObjectVectorAttribute::kAriaDetails:
        AddIntListAttributeFromWebObjects(ui::AX_ATTR_DETAILS_IDS, value,
                                          dst_);
        break;
      case blink::WebAXObjectVectorAttribute::kAriaFlowTo:
        AddIntListAttributeFromWebObjects(ui::AX_ATTR_FLOWTO_IDS, value, dst_);
        break;
      default:
        NOTREACHED();
    }
  }
};

WebAXObject ParentObjectUnignored(WebAXObject child) {
  WebAXObject parent = child.ParentObject();
  while (!parent.IsDetached() && parent.AccessibilityIsIgnored())
    parent = parent.ParentObject();
  return parent;
}

// Returns true if |ancestor| is the first unignored parent of |child|,
// which means that when walking up the parent chain from |child|,
// |ancestor| is the *first* ancestor that isn't marked as
// accessibilityIsIgnored().
bool IsParentUnignoredOf(WebAXObject ancestor,
                         WebAXObject child) {
  WebAXObject parent = ParentObjectUnignored(child);
  return parent.Equals(ancestor);
}

std::string GetEquivalentAriaRoleString(const ui::AXRole role) {
  switch (role) {
    case ui::AX_ROLE_ARTICLE:
      return "article";
    case ui::AX_ROLE_BANNER:
      return "banner";
    case ui::AX_ROLE_BUTTON:
      return "button";
    case ui::AX_ROLE_COMPLEMENTARY:
      return "complementary";
    case ui::AX_ROLE_FIGURE:
      return "figure";
    case ui::AX_ROLE_FOOTER:
      return "contentinfo";
    case ui::AX_ROLE_HEADING:
      return "heading";
    case ui::AX_ROLE_IMAGE:
      return "img";
    case ui::AX_ROLE_MAIN:
      return "main";
    case ui::AX_ROLE_NAVIGATION:
      return "navigation";
    case ui::AX_ROLE_RADIO_BUTTON:
      return "radio";
    case ui::AX_ROLE_REGION:
      return "region";
    case ui::AX_ROLE_SLIDER:
      return "slider";
    case ui::AX_ROLE_TIME:
      return "time";
    default:
      break;
  }

  return std::string();
}

}  // namespace

ScopedFreezeBlinkAXTreeSource::ScopedFreezeBlinkAXTreeSource(
    BlinkAXTreeSource* tree_source)
    : tree_source_(tree_source) {
  tree_source_->Freeze();
}

ScopedFreezeBlinkAXTreeSource::~ScopedFreezeBlinkAXTreeSource() {
  tree_source_->Thaw();
}

BlinkAXTreeSource::BlinkAXTreeSource(RenderFrameImpl* render_frame,
                                     AccessibilityMode mode)
    : render_frame_(render_frame),
      accessibility_mode_(mode),
      frozen_(false) {}

BlinkAXTreeSource::~BlinkAXTreeSource() {
}

void BlinkAXTreeSource::Freeze() {
  CHECK(!frozen_);
  frozen_ = true;

  if (render_frame_ && render_frame_->GetWebFrame())
    document_ = render_frame_->GetWebFrame()->GetDocument();
  else
    document_ = WebDocument();

  root_ = ComputeRoot();

  if (!document_.IsNull())
    focus_ = document_.FocusedAccessibilityObject();
  else
    focus_ = WebAXObject();
}

void BlinkAXTreeSource::Thaw() {
  CHECK(frozen_);
  frozen_ = false;
}

void BlinkAXTreeSource::SetRoot(blink::WebAXObject root) {
  CHECK(!frozen_);
  explicit_root_ = root;
}

bool BlinkAXTreeSource::IsInTree(blink::WebAXObject node) const {
  CHECK(frozen_);
  while (IsValid(node)) {
    if (node.Equals(root()))
      return true;
    node = GetParent(node);
  }
  return false;
}

void BlinkAXTreeSource::SetAccessibilityMode(AccessibilityMode new_mode) {
  if (accessibility_mode_ == new_mode)
    return;
  accessibility_mode_ = new_mode;
}

bool BlinkAXTreeSource::GetTreeData(AXContentTreeData* tree_data) const {
  CHECK(frozen_);
  tree_data->doctype = "html";
  tree_data->loaded = root().IsLoaded();
  tree_data->loading_progress = root().EstimatedLoadingProgress();
  tree_data->mimetype =
      document().IsXHTMLDocument() ? "text/xhtml" : "text/html";
  tree_data->title = document().Title().Utf8();
  tree_data->url = document().Url().GetString().Utf8();

  if (!focus().IsNull())
    tree_data->focus_id = focus().AxID();

  WebAXObject anchor_object, focus_object;
  int anchor_offset, focus_offset;
  blink::WebAXTextAffinity anchor_affinity, focus_affinity;
  root().Selection(anchor_object, anchor_offset, anchor_affinity, focus_object,
                   focus_offset, focus_affinity);
  if (!anchor_object.IsNull() && !focus_object.IsNull() && anchor_offset >= 0 &&
      focus_offset >= 0) {
    int32_t anchor_id = anchor_object.AxID();
    int32_t focus_id = focus_object.AxID();
    tree_data->sel_anchor_object_id = anchor_id;
    tree_data->sel_anchor_offset = anchor_offset;
    tree_data->sel_focus_object_id = focus_id;
    tree_data->sel_focus_offset = focus_offset;
    tree_data->sel_anchor_affinity = AXTextAffinityFromBlink(anchor_affinity);
    tree_data->sel_focus_affinity = AXTextAffinityFromBlink(focus_affinity);
  }

  // Get the tree ID for this frame and the parent frame.
  WebLocalFrame* web_frame = document().GetFrame();
  if (web_frame) {
    RenderFrame* render_frame = RenderFrame::FromWebFrame(web_frame);
    tree_data->routing_id = render_frame->GetRoutingID();

    // Get the tree ID for the parent frame.
    blink::WebFrame* parent_web_frame = web_frame->Parent();
    if (parent_web_frame) {
      tree_data->parent_routing_id =
          GetRoutingIdForFrameOrProxy(parent_web_frame);
    }
  }

  return true;
}

blink::WebAXObject BlinkAXTreeSource::GetRoot() const {
  if (frozen_)
    return root_;
  else
    return ComputeRoot();
}

blink::WebAXObject BlinkAXTreeSource::GetFromId(int32_t id) const {
  return GetMainDocument().AccessibilityObjectFromID(id);
}

int32_t BlinkAXTreeSource::GetId(blink::WebAXObject node) const {
  return node.AxID();
}

void BlinkAXTreeSource::GetChildren(
    blink::WebAXObject parent,
    std::vector<blink::WebAXObject>* out_children) const {
  CHECK(frozen_);

  if (parent.Role() == blink::kWebAXRoleStaticText) {
    int32_t focus_id = focus().AxID();
    blink::WebAXObject ancestor = parent;
    while (!ancestor.IsDetached()) {
      if (ancestor.AxID() == accessibility_focus_id_ ||
          (ancestor.AxID() == focus_id && ancestor.IsEditable())) {
        parent.LoadInlineTextBoxes();
        break;
      }
      ancestor = ancestor.ParentObject();
    }
  }

  bool is_iframe = false;
  WebNode node = parent.GetNode();
  if (!node.IsNull() && node.IsElementNode())
    is_iframe = node.To<WebElement>().HasHTMLTagName("iframe");

  for (unsigned i = 0; i < parent.ChildCount(); i++) {
    blink::WebAXObject child = parent.ChildAt(i);

    // The child may be invalid due to issues in blink accessibility code.
    if (child.IsDetached())
      continue;

    // Skip children whose parent isn't |parent|.
    // As an exception, include children of an iframe element.
    if (!is_iframe && !IsParentUnignoredOf(parent, child))
      continue;

    out_children->push_back(child);
  }
}

blink::WebAXObject BlinkAXTreeSource::GetParent(
    blink::WebAXObject node) const {
  CHECK(frozen_);

  // Blink returns ignored objects when walking up the parent chain,
  // we have to skip those here. Also, stop when we get to the root
  // element.
  do {
    if (node.Equals(root()))
      return blink::WebAXObject();
    node = node.ParentObject();
  } while (!node.IsDetached() && node.AccessibilityIsIgnored());

  return node;
}

bool BlinkAXTreeSource::IsValid(blink::WebAXObject node) const {
  return !node.IsDetached();  // This also checks if it's null.
}

bool BlinkAXTreeSource::IsEqual(blink::WebAXObject node1,
                                blink::WebAXObject node2) const {
  return node1.Equals(node2);
}

blink::WebAXObject BlinkAXTreeSource::GetNull() const {
  return blink::WebAXObject();
}

void BlinkAXTreeSource::SerializeNode(blink::WebAXObject src,
                                      AXContentNodeData* dst) const {
  dst->role = AXRoleFromBlink(src.Role());
  dst->state = AXStateFromBlink(src);
  dst->id = src.AxID();

  TRACE_EVENT1("accessibility", "BlinkAXTreeSource::SerializeNode", "role",
               ui::ToString(dst->role));

  WebAXObject offset_container;
  WebFloatRect bounds_in_container;
  SkMatrix44 container_transform;
  src.GetRelativeBounds(offset_container, bounds_in_container,
                        container_transform);
  dst->location = bounds_in_container;
  if (!container_transform.isIdentity())
    dst->transform = base::WrapUnique(new gfx::Transform(container_transform));
  if (!offset_container.IsDetached())
    dst->offset_container_id = offset_container.AxID();

  AXContentNodeDataSparseAttributeAdapter sparse_attribute_adapter(dst);
  src.GetSparseAXAttributes(sparse_attribute_adapter);

  blink::WebAXNameFrom nameFrom;
  blink::WebVector<blink::WebAXObject> nameObjects;
  blink::WebString web_name = src.GetName(nameFrom, nameObjects);
  if ((!web_name.IsEmpty() && !web_name.IsNull()) ||
      nameFrom == blink::kWebAXNameFromAttributeExplicitlyEmpty) {
    dst->AddStringAttribute(ui::AX_ATTR_NAME, web_name.Utf8());
    dst->AddIntAttribute(ui::AX_ATTR_NAME_FROM, AXNameFromFromBlink(nameFrom));
    AddIntListAttributeFromWebObjects(
        ui::AX_ATTR_LABELLEDBY_IDS, nameObjects, dst);
  }

  blink::WebAXDescriptionFrom descriptionFrom;
  blink::WebVector<blink::WebAXObject> descriptionObjects;
  blink::WebString web_description =
      src.Description(nameFrom, descriptionFrom, descriptionObjects);
  if (!web_description.IsEmpty()) {
    dst->AddStringAttribute(ui::AX_ATTR_DESCRIPTION, web_description.Utf8());
    dst->AddIntAttribute(ui::AX_ATTR_DESCRIPTION_FROM,
        AXDescriptionFromFromBlink(descriptionFrom));
    AddIntListAttributeFromWebObjects(
        ui::AX_ATTR_DESCRIBEDBY_IDS, descriptionObjects, dst);
  }

  if (src.ValueDescription().length()) {
    dst->AddStringAttribute(ui::AX_ATTR_VALUE, src.ValueDescription().Utf8());
  } else {
    dst->AddStringAttribute(ui::AX_ATTR_VALUE, src.StringValue().Utf8());
  }

  if (src.CanSetValueAttribute())
    dst->AddAction(ui::AX_ACTION_SET_VALUE);

  if (!src.Url().IsEmpty())
    dst->AddStringAttribute(ui::AX_ATTR_URL, src.Url().GetString().Utf8());

  // The following set of attributes are only accessed when the accessibility
  // mode is set to screen reader mode, otherwise only the more basic
  // attributes are populated.
  if (accessibility_mode_.has_mode(AccessibilityMode::kScreenReader)) {
    blink::WebString web_placeholder = src.Placeholder(nameFrom);
    if (!web_placeholder.IsEmpty())
      dst->AddStringAttribute(ui::AX_ATTR_PLACEHOLDER, web_placeholder.Utf8());

    if (dst->role == ui::AX_ROLE_COLOR_WELL)
      dst->AddIntAttribute(ui::AX_ATTR_COLOR_VALUE, src.ColorValue());

    if (dst->role == ui::AX_ROLE_LINK) {
      blink::WebAXObject target = src.InPageLinkTarget();
      if (!target.IsNull()) {
        int32_t target_id = target.AxID();
        dst->AddIntAttribute(ui::AX_ATTR_IN_PAGE_LINK_TARGET_ID, target_id);
      }
    }

    if (dst->role == ui::AX_ROLE_RADIO_BUTTON) {
      AddIntListAttributeFromWebObjects(ui::AX_ATTR_RADIO_GROUP_IDS,
                                        src.RadioButtonsInGroup(), dst);
    }

    // Text attributes.
    if (src.BackgroundColor())
      dst->AddIntAttribute(ui::AX_ATTR_BACKGROUND_COLOR, src.BackgroundColor());

    if (src.GetColor())
      dst->AddIntAttribute(ui::AX_ATTR_COLOR, src.GetColor());

    WebAXObject parent = ParentObjectUnignored(src);
    if (src.FontFamily().length()) {
      if (parent.IsNull() || parent.FontFamily() != src.FontFamily())
        dst->AddStringAttribute(ui::AX_ATTR_FONT_FAMILY,
                                src.FontFamily().Utf8());
    }

    // Font size is in pixels.
    if (src.FontSize())
      dst->AddFloatAttribute(ui::AX_ATTR_FONT_SIZE, src.FontSize());

    if (src.AriaCurrentState()) {
      dst->AddIntAttribute(ui::AX_ATTR_ARIA_CURRENT_STATE,
                           AXAriaCurrentStateFromBlink(src.AriaCurrentState()));
    }

    if (src.InvalidState()) {
      dst->AddIntAttribute(ui::AX_ATTR_INVALID_STATE,
                           AXInvalidStateFromBlink(src.InvalidState()));
    }
    if (src.InvalidState() == blink::kWebAXInvalidStateOther &&
        src.AriaInvalidValue().length()) {
      dst->AddStringAttribute(ui::AX_ATTR_ARIA_INVALID_VALUE,
                              src.AriaInvalidValue().Utf8());
    }

    if (src.IsCheckable()) {
      const blink::WebAXCheckedState checked_state = src.CheckedState();
      dst->AddIntAttribute(ui::AX_ATTR_CHECKED_STATE,
                           AXCheckedStateFromBlink(checked_state));
    }

    if (src.GetTextDirection()) {
      dst->AddIntAttribute(ui::AX_ATTR_TEXT_DIRECTION,
                           AXTextDirectionFromBlink(src.GetTextDirection()));
    }

    if (src.TextStyle()) {
      dst->AddIntAttribute(ui::AX_ATTR_TEXT_STYLE,
                           AXTextStyleFromBlink(src.TextStyle()));
    }

    if (dst->role == ui::AX_ROLE_INLINE_TEXT_BOX) {
      WebVector<int> src_character_offsets;
      src.CharacterOffsets(src_character_offsets);
      std::vector<int32_t> character_offsets;
      character_offsets.reserve(src_character_offsets.size());
      for (size_t i = 0; i < src_character_offsets.size(); ++i)
        character_offsets.push_back(src_character_offsets[i]);
      dst->AddIntListAttribute(ui::AX_ATTR_CHARACTER_OFFSETS,
                               character_offsets);

      WebVector<int> src_word_starts;
      WebVector<int> src_word_ends;
      src.GetWordBoundaries(src_word_starts, src_word_ends);
      std::vector<int32_t> word_starts;
      std::vector<int32_t> word_ends;
      word_starts.reserve(src_word_starts.size());
      word_ends.reserve(src_word_starts.size());
      for (size_t i = 0; i < src_word_starts.size(); ++i) {
        word_starts.push_back(src_word_starts[i]);
        word_ends.push_back(src_word_ends[i]);
      }
      dst->AddIntListAttribute(ui::AX_ATTR_WORD_STARTS, word_starts);
      dst->AddIntListAttribute(ui::AX_ATTR_WORD_ENDS, word_ends);
    }

    if (src.AccessKey().length()) {
      dst->AddStringAttribute(ui::AX_ATTR_ACCESS_KEY, src.AccessKey().Utf8());
    }

    if (src.AriaAutoComplete().length()) {
      dst->AddStringAttribute(ui::AX_ATTR_AUTO_COMPLETE,
                              src.AriaAutoComplete().Utf8());
    }

    if (src.Action() != blink::WebAXDefaultActionVerb::kNone) {
      dst->AddIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB,
                           AXDefaultActionVerbFromBlink(src.Action()));
    }

    if (src.IsAriaReadOnly())
      dst->AddBoolAttribute(ui::AX_ATTR_ARIA_READONLY, true);

    if (src.HasComputedStyle()) {
      dst->AddStringAttribute(ui::AX_ATTR_DISPLAY,
                              src.ComputedStyleDisplay().Utf8());
    }

    if (src.Language().length()) {
      if (parent.IsNull() || parent.Language() != src.Language())
        dst->AddStringAttribute(ui::AX_ATTR_LANGUAGE, src.Language().Utf8());
    }

    if (src.KeyboardShortcut().length()) {
      dst->AddStringAttribute(ui::AX_ATTR_SHORTCUT,
                              src.KeyboardShortcut().Utf8());
    }

    if (!src.NextOnLine().IsDetached()) {
      dst->AddIntAttribute(ui::AX_ATTR_NEXT_ON_LINE_ID,
                           src.NextOnLine().AxID());
    }

    if (!src.PreviousOnLine().IsDetached()) {
      dst->AddIntAttribute(ui::AX_ATTR_PREVIOUS_ON_LINE_ID,
                           src.PreviousOnLine().AxID());
    }

    if (!src.AriaActiveDescendant().IsDetached()) {
      dst->AddIntAttribute(ui::AX_ATTR_ACTIVEDESCENDANT_ID,
                           src.AriaActiveDescendant().AxID());
    }

    if (dst->role == ui::AX_ROLE_HEADING && src.HeadingLevel()) {
      dst->AddIntAttribute(ui::AX_ATTR_HIERARCHICAL_LEVEL, src.HeadingLevel());
    } else if ((dst->role == ui::AX_ROLE_TREE_ITEM ||
                dst->role == ui::AX_ROLE_ROW) &&
               src.HierarchicalLevel()) {
      dst->AddIntAttribute(ui::AX_ATTR_HIERARCHICAL_LEVEL,
                           src.HierarchicalLevel());
    }

    if (src.SetSize())
      dst->AddIntAttribute(ui::AX_ATTR_SET_SIZE, src.SetSize());

    if (src.PosInSet())
      dst->AddIntAttribute(ui::AX_ATTR_POS_IN_SET, src.PosInSet());

    if (src.CanvasHasFallbackContent())
      dst->AddBoolAttribute(ui::AX_ATTR_CANVAS_HAS_FALLBACK, true);

    // Spelling, grammar and other document markers.
    WebVector<blink::WebAXMarkerType> src_marker_types;
    WebVector<int> src_marker_starts;
    WebVector<int> src_marker_ends;
    src.Markers(src_marker_types, src_marker_starts, src_marker_ends);
    DCHECK_EQ(src_marker_types.size(), src_marker_starts.size());
    DCHECK_EQ(src_marker_starts.size(), src_marker_ends.size());

    if (src_marker_types.size()) {
      std::vector<int32_t> marker_types;
      std::vector<int32_t> marker_starts;
      std::vector<int32_t> marker_ends;
      marker_types.reserve(src_marker_types.size());
      marker_starts.reserve(src_marker_starts.size());
      marker_ends.reserve(src_marker_ends.size());
      for (size_t i = 0; i < src_marker_types.size(); ++i) {
        marker_types.push_back(
            static_cast<int32_t>(AXMarkerTypeFromBlink(src_marker_types[i])));
        marker_starts.push_back(src_marker_starts[i]);
        marker_ends.push_back(src_marker_ends[i]);
      }
      dst->AddIntListAttribute(ui::AX_ATTR_MARKER_TYPES, marker_types);
      dst->AddIntListAttribute(ui::AX_ATTR_MARKER_STARTS, marker_starts);
      dst->AddIntListAttribute(ui::AX_ATTR_MARKER_ENDS, marker_ends);
    }

    if (src.IsInLiveRegion()) {
      dst->AddBoolAttribute(ui::AX_ATTR_LIVE_ATOMIC, src.LiveRegionAtomic());
      dst->AddBoolAttribute(ui::AX_ATTR_LIVE_BUSY, src.LiveRegionBusy());
      if (src.LiveRegionBusy())
        dst->AddState(ui::AX_STATE_BUSY);
      if (!src.LiveRegionStatus().IsEmpty()) {
        dst->AddStringAttribute(ui::AX_ATTR_LIVE_STATUS,
                                src.LiveRegionStatus().Utf8());
      }
      dst->AddStringAttribute(ui::AX_ATTR_LIVE_RELEVANT,
                              src.LiveRegionRelevant().Utf8());
      // If we are not at the root of an atomic live region.
      if (src.ContainerLiveRegionAtomic() &&
          !src.LiveRegionRoot().IsDetached() && !src.LiveRegionAtomic()) {
        dst->AddIntAttribute(ui::AX_ATTR_MEMBER_OF_ID,
                             src.LiveRegionRoot().AxID());
      }
      dst->AddBoolAttribute(ui::AX_ATTR_CONTAINER_LIVE_ATOMIC,
                            src.ContainerLiveRegionAtomic());
      dst->AddBoolAttribute(ui::AX_ATTR_CONTAINER_LIVE_BUSY,
                            src.ContainerLiveRegionBusy());
      dst->AddStringAttribute(ui::AX_ATTR_CONTAINER_LIVE_STATUS,
                              src.ContainerLiveRegionStatus().Utf8());
      dst->AddStringAttribute(ui::AX_ATTR_CONTAINER_LIVE_RELEVANT,
                              src.ContainerLiveRegionRelevant().Utf8());
    }

    if (dst->role == ui::AX_ROLE_PROGRESS_INDICATOR ||
        dst->role == ui::AX_ROLE_METER ||
        dst->role == ui::AX_ROLE_SCROLL_BAR ||
        dst->role == ui::AX_ROLE_SLIDER ||
        dst->role == ui::AX_ROLE_SPIN_BUTTON) {
      dst->AddFloatAttribute(ui::AX_ATTR_VALUE_FOR_RANGE, src.ValueForRange());
      dst->AddFloatAttribute(ui::AX_ATTR_MAX_VALUE_FOR_RANGE,
                             src.MaxValueForRange());
      dst->AddFloatAttribute(ui::AX_ATTR_MIN_VALUE_FOR_RANGE,
                             src.MinValueForRange());
    }

    if (dst->role == ui::AX_ROLE_DIALOG ||
        dst->role == ui::AX_ROLE_ALERT_DIALOG) {
      dst->AddBoolAttribute(ui::AX_ATTR_MODAL, src.IsModal());
    }

    if (dst->role == ui::AX_ROLE_ROOT_WEB_AREA)
      dst->AddStringAttribute(ui::AX_ATTR_HTML_TAG, "#document");

    const bool is_table_like_role = dst->role == ui::AX_ROLE_TABLE ||
                                    dst->role == ui::AX_ROLE_GRID ||
                                    dst->role == ui::AX_ROLE_TREE_GRID;
    if (is_table_like_role) {
      int column_count = src.ColumnCount();
      int row_count = src.RowCount();
      if (column_count > 0 && row_count > 0) {
        std::set<int32_t> unique_cell_id_set;
        std::vector<int32_t> cell_ids;
        std::vector<int32_t> unique_cell_ids;
        dst->AddIntAttribute(ui::AX_ATTR_TABLE_COLUMN_COUNT, column_count);
        dst->AddIntAttribute(ui::AX_ATTR_TABLE_ROW_COUNT, row_count);
        WebAXObject header = src.HeaderContainerObject();
        if (!header.IsDetached())
          dst->AddIntAttribute(ui::AX_ATTR_TABLE_HEADER_ID, header.AxID());
        for (int i = 0; i < column_count * row_count; ++i) {
          WebAXObject cell =
              src.CellForColumnAndRow(i % column_count, i / column_count);
          int cell_id = -1;
          if (!cell.IsDetached()) {
            cell_id = cell.AxID();
            if (unique_cell_id_set.find(cell_id) == unique_cell_id_set.end()) {
              unique_cell_id_set.insert(cell_id);
              unique_cell_ids.push_back(cell_id);
            }
          }
          cell_ids.push_back(cell_id);
        }
        dst->AddIntListAttribute(ui::AX_ATTR_CELL_IDS, cell_ids);
        dst->AddIntListAttribute(ui::AX_ATTR_UNIQUE_CELL_IDS, unique_cell_ids);
      }

      int aria_colcount = src.AriaColumnCount();
      if (aria_colcount)
        dst->AddIntAttribute(ui::AX_ATTR_ARIA_COLUMN_COUNT, aria_colcount);

      int aria_rowcount = src.AriaRowCount();
      if (aria_rowcount)
        dst->AddIntAttribute(ui::AX_ATTR_ARIA_ROW_COUNT, aria_rowcount);
    }

    if (dst->role == ui::AX_ROLE_ROW) {
      dst->AddIntAttribute(ui::AX_ATTR_TABLE_ROW_INDEX, src.RowIndex());
      WebAXObject header = src.RowHeader();
      if (!header.IsDetached())
        dst->AddIntAttribute(ui::AX_ATTR_TABLE_ROW_HEADER_ID, header.AxID());
    }

    if (dst->role == ui::AX_ROLE_COLUMN) {
      dst->AddIntAttribute(ui::AX_ATTR_TABLE_COLUMN_INDEX, src.ColumnIndex());
      WebAXObject header = src.ColumnHeader();
      if (!header.IsDetached())
        dst->AddIntAttribute(ui::AX_ATTR_TABLE_COLUMN_HEADER_ID, header.AxID());
    }

    if (dst->role == ui::AX_ROLE_CELL ||
        dst->role == ui::AX_ROLE_ROW_HEADER ||
        dst->role == ui::AX_ROLE_COLUMN_HEADER ||
        dst->role == ui::AX_ROLE_ROW) {
      if (dst->role != ui::AX_ROLE_ROW) {
        dst->AddIntAttribute(ui::AX_ATTR_TABLE_CELL_COLUMN_INDEX,
                             src.CellColumnIndex());
        dst->AddIntAttribute(ui::AX_ATTR_TABLE_CELL_COLUMN_SPAN,
                             src.CellColumnSpan());
        dst->AddIntAttribute(ui::AX_ATTR_TABLE_CELL_ROW_INDEX,
                             src.CellRowIndex());
        dst->AddIntAttribute(ui::AX_ATTR_TABLE_CELL_ROW_SPAN,
                             src.CellRowSpan());

        int aria_colindex = src.AriaColumnIndex();
        if (aria_colindex) {
          dst->AddIntAttribute(ui::AX_ATTR_ARIA_CELL_COLUMN_INDEX,
                               aria_colindex);
        }
      }

      int aria_rowindex = src.AriaRowIndex();
      if (aria_rowindex)
        dst->AddIntAttribute(ui::AX_ATTR_ARIA_CELL_ROW_INDEX, aria_rowindex);
    }

    if ((dst->role == ui::AX_ROLE_ROW_HEADER ||
         dst->role == ui::AX_ROLE_COLUMN_HEADER) &&
        src.SortDirection()) {
      dst->AddIntAttribute(ui::AX_ATTR_SORT_DIRECTION,
                           AXSortDirectionFromBlink(src.SortDirection()));
    }
  }

  // The majority of the rest of this code computes attributes needed for
  // all modes, not just for screen readers.

  WebNode node = src.GetNode();
  bool is_iframe = false;

  if (!node.IsNull() && node.IsElementNode()) {
    WebElement element = node.To<WebElement>();
    is_iframe = element.HasHTMLTagName("iframe");

    if (accessibility_mode_.has_mode(AccessibilityMode::kHTML)) {
      // TODO(ctguil): The tagName in WebKit is lower cased but
      // HTMLElement::nodeName calls localNameUpper. Consider adding
      // a WebElement method that returns the original lower cased tagName.
      dst->AddStringAttribute(ui::AX_ATTR_HTML_TAG,
                              base::ToLowerASCII(element.TagName().Utf8()));
      for (unsigned i = 0; i < element.AttributeCount(); ++i) {
        std::string name =
            base::ToLowerASCII(element.AttributeLocalName(i).Utf8());
        std::string value = element.AttributeValue(i).Utf8();
        dst->html_attributes.push_back(std::make_pair(name, value));
      }

// TODO(nektar): Turn off kHTMLAccessibilityMode for automation and Mac
// and remove ifdef.
#if defined(OS_WIN)
      if (dst->role == ui::AX_ROLE_MATH && element.InnerHTML().length()) {
        dst->AddStringAttribute(ui::AX_ATTR_INNER_HTML,
                                element.InnerHTML().Utf8());
      }
#endif
    }

    if (src.IsEditable()) {
      dst->AddIntAttribute(ui::AX_ATTR_TEXT_SEL_START, src.SelectionStart());
      dst->AddIntAttribute(ui::AX_ATTR_TEXT_SEL_END, src.SelectionEnd());

#if defined(OS_CHROMEOS)
      // This attribute will soon be deprecated; see crbug.com/669134.
      WebVector<int> src_line_breaks;
      src.LineBreaks(src_line_breaks);
      if (src_line_breaks.size()) {
        std::vector<int32_t> line_breaks;
        line_breaks.reserve(src_line_breaks.size());
        for (size_t i = 0; i < src_line_breaks.size(); ++i)
          line_breaks.push_back(src_line_breaks[i]);
        dst->AddIntListAttribute(ui::AX_ATTR_LINE_BREAKS, line_breaks);
      }
#endif  // defined OS_CHROMEOS
    }

    // ARIA role.
    if (element.HasAttribute("role")) {
      dst->AddStringAttribute(ui::AX_ATTR_ROLE,
                              element.GetAttribute("role").Utf8());
    } else {
      std::string role = GetEquivalentAriaRoleString(dst->role);
      if (!role.empty())
        dst->AddStringAttribute(ui::AX_ATTR_ROLE, role);
    }

    // Browser plugin (used in a <webview>).
    BrowserPlugin* browser_plugin = BrowserPlugin::GetFromNode(element);
    if (browser_plugin) {
      dst->AddContentIntAttribute(
          AX_CONTENT_ATTR_CHILD_BROWSER_PLUGIN_INSTANCE_ID,
          browser_plugin->browser_plugin_instance_id());
    }

    // Frames and iframes.
    WebFrame* frame = WebFrame::FromFrameOwnerElement(element);
    if (frame) {
      dst->AddContentIntAttribute(
          AX_CONTENT_ATTR_CHILD_ROUTING_ID,
          GetRoutingIdForFrameOrProxy(frame));
    }
  }

  // Add the ids of *indirect* children - those who are children of this node,
  // but whose parent is *not* this node. One example is a table
  // cell, which is a child of both a row and a column. Because the cell's
  // parent is the row, the row adds it as a child, and the column adds it
  // as an indirect child.
  int child_count = src.ChildCount();
  for (int i = 0; i < child_count; ++i) {
    WebAXObject child = src.ChildAt(i);
    std::vector<int32_t> indirect_child_ids;
    if (!is_iframe && !child.IsDetached() && !IsParentUnignoredOf(src, child))
      indirect_child_ids.push_back(child.AxID());
    if (indirect_child_ids.size() > 0) {
      dst->AddIntListAttribute(
          ui::AX_ATTR_INDIRECT_CHILD_IDS, indirect_child_ids);
    }
  }

  if (src.IsScrollableContainer()) {
    const gfx::Point& scrollOffset = src.GetScrollOffset();
    dst->AddIntAttribute(ui::AX_ATTR_SCROLL_X, scrollOffset.x());
    dst->AddIntAttribute(ui::AX_ATTR_SCROLL_Y, scrollOffset.y());

    const gfx::Point& minScrollOffset = src.MinimumScrollOffset();
    dst->AddIntAttribute(ui::AX_ATTR_SCROLL_X_MIN, minScrollOffset.x());
    dst->AddIntAttribute(ui::AX_ATTR_SCROLL_Y_MIN, minScrollOffset.y());

    const gfx::Point& maxScrollOffset = src.MaximumScrollOffset();
    dst->AddIntAttribute(ui::AX_ATTR_SCROLL_X_MAX, maxScrollOffset.x());
    dst->AddIntAttribute(ui::AX_ATTR_SCROLL_Y_MAX, maxScrollOffset.y());
  }

  if (dst->id == image_data_node_id_) {
    dst->AddStringAttribute(ui::AX_ATTR_IMAGE_DATA_URL,
                            src.ImageDataUrl(max_image_data_size_).Utf8());
  }
}

blink::WebDocument BlinkAXTreeSource::GetMainDocument() const {
  CHECK(frozen_);
  return document_;
}

WebAXObject BlinkAXTreeSource::ComputeRoot() const {
  if (!explicit_root_.IsNull())
    return explicit_root_;

  if (!render_frame_ || !render_frame_->GetWebFrame())
    return WebAXObject();

  WebDocument document = render_frame_->GetWebFrame()->GetDocument();
  if (!document.IsNull())
    return document.AccessibilityObject();

  return WebAXObject();
}

}  // namespace content
