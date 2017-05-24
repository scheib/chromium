// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/accessibility/browser_accessibility.h"

#include <stddef.h>

#include <algorithm>
#include <iterator>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "content/browser/accessibility/browser_accessibility_manager.h"
#include "content/common/accessibility_messages.h"
#include "ui/accessibility/ax_role_properties.h"
#include "ui/accessibility/ax_text_utils.h"
#include "ui/accessibility/platform/ax_platform_unique_id.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"

namespace content {

namespace {

// Map from unique_id to BrowserAccessibility
using UniqueIDMap = base::hash_map<int32_t, BrowserAccessibility*>;
base::LazyInstance<UniqueIDMap>::DestructorAtExit g_unique_id_map =
    LAZY_INSTANCE_INITIALIZER;
}

#if !defined(PLATFORM_HAS_NATIVE_ACCESSIBILITY_IMPL)
// static
BrowserAccessibility* BrowserAccessibility::Create() {
  return new BrowserAccessibility();
}
#endif

BrowserAccessibility::BrowserAccessibility()
    : manager_(nullptr),
      node_(nullptr),
      unique_id_(ui::GetNextAXPlatformNodeUniqueId()) {
  g_unique_id_map.Get()[unique_id_] = this;
}

BrowserAccessibility::~BrowserAccessibility() {
  if (unique_id_)
    g_unique_id_map.Get().erase(unique_id_);
}

// static
BrowserAccessibility* BrowserAccessibility::GetFromUniqueID(int32_t unique_id) {
  auto iter = g_unique_id_map.Get().find(unique_id);
  if (iter == g_unique_id_map.Get().end())
    return nullptr;

  return iter->second;
}

void BrowserAccessibility::Init(BrowserAccessibilityManager* manager,
    ui::AXNode* node) {
  manager_ = manager;
  node_ = node;
}

bool BrowserAccessibility::PlatformIsLeaf() const {
  if (InternalChildCount() == 0)
    return true;

  // These types of objects may have children that we use as internal
  // implementation details, but we want to expose them as leaves to platform
  // accessibility APIs because screen readers might be confused if they find
  // any children.
  // Note that if a combo box, search box or text field are not native, they
  // might present a menu of choices using aria-owns which should not be hidden
  // from tree.
  if (IsNativeTextControl() || IsTextOnlyObject())
    return true;

  // Roles whose children are only presentational according to the ARIA and
  // HTML5 Specs should be hidden from screen readers.
  // (Note that whilst ARIA buttons can have only presentational children, HTML5
  // buttons are allowed to have content.)
  switch (GetRole()) {
    case ui::AX_ROLE_IMAGE:
    case ui::AX_ROLE_METER:
    case ui::AX_ROLE_SCROLL_BAR:
    case ui::AX_ROLE_SLIDER:
    case ui::AX_ROLE_SPLITTER:
    case ui::AX_ROLE_PROGRESS_INDICATOR:
      return true;
    default:
      return false;
  }
}

uint32_t BrowserAccessibility::PlatformChildCount() const {
  if (HasIntAttribute(ui::AX_ATTR_CHILD_TREE_ID)) {
    BrowserAccessibilityManager* child_manager =
        BrowserAccessibilityManager::FromID(
            GetIntAttribute(ui::AX_ATTR_CHILD_TREE_ID));
    if (child_manager && child_manager->GetRoot()->PlatformGetParent() == this)
      return 1;

    return 0;
  }

  return PlatformIsLeaf() ? 0 : InternalChildCount();
}

bool BrowserAccessibility::IsNative() const {
  return false;
}

bool BrowserAccessibility::IsDescendantOf(
    const BrowserAccessibility* ancestor) const {
  if (!ancestor)
    return false;

  if (this == ancestor)
    return true;

  if (PlatformGetParent())
    return PlatformGetParent()->IsDescendantOf(ancestor);

  return false;
}

bool BrowserAccessibility::IsTextOnlyObject() const {
  return GetRole() == ui::AX_ROLE_STATIC_TEXT ||
         GetRole() == ui::AX_ROLE_LINE_BREAK ||
         GetRole() == ui::AX_ROLE_INLINE_TEXT_BOX;
}

bool BrowserAccessibility::IsLineBreakObject() const {
  return GetRole() == ui::AX_ROLE_LINE_BREAK ||
         (IsTextOnlyObject() && PlatformGetParent() &&
          PlatformGetParent()->GetRole() == ui::AX_ROLE_LINE_BREAK);
}

BrowserAccessibility* BrowserAccessibility::PlatformGetChild(
    uint32_t child_index) const {
  BrowserAccessibility* result = nullptr;

  if (child_index == 0 && HasIntAttribute(ui::AX_ATTR_CHILD_TREE_ID)) {
    BrowserAccessibilityManager* child_manager =
        BrowserAccessibilityManager::FromID(
            GetIntAttribute(ui::AX_ATTR_CHILD_TREE_ID));
    if (child_manager && child_manager->GetRoot()->PlatformGetParent() == this)
      result = child_manager->GetRoot();
  } else {
    result = InternalGetChild(child_index);
  }

  return result;
}

bool BrowserAccessibility::PlatformIsChildOfLeaf() const {
  BrowserAccessibility* ancestor = InternalGetParent();
  while (ancestor) {
    if (ancestor->PlatformIsLeaf())
      return true;
    ancestor = ancestor->InternalGetParent();
  }

  return false;
}

BrowserAccessibility* BrowserAccessibility::GetClosestPlatformObject() const {
  BrowserAccessibility* platform_object =
      const_cast<BrowserAccessibility*>(this);
  while (platform_object && platform_object->PlatformIsChildOfLeaf())
    platform_object = platform_object->InternalGetParent();

  DCHECK(platform_object);
  return platform_object;
}

BrowserAccessibility* BrowserAccessibility::GetPreviousSibling() const {
  if (PlatformGetParent() && GetIndexInParent() > 0)
    return PlatformGetParent()->InternalGetChild(GetIndexInParent() - 1);

  return nullptr;
}

BrowserAccessibility* BrowserAccessibility::GetNextSibling() const {
  if (PlatformGetParent() && GetIndexInParent() >= 0 &&
      GetIndexInParent() <
          static_cast<int>(PlatformGetParent()->InternalChildCount() - 1)) {
    return PlatformGetParent()->InternalGetChild(GetIndexInParent() + 1);
  }

  return nullptr;
}

bool BrowserAccessibility::IsPreviousSiblingOnSameLine() const {
  const BrowserAccessibility* previous_sibling = GetPreviousSibling();
  if (!previous_sibling)
    return false;

  // Line linkage information might not be provided on non-leaf objects.
  const BrowserAccessibility* leaf_object = PlatformDeepestFirstChild();
  if (!leaf_object)
    leaf_object = this;

  int32_t previous_on_line_id;
  if (leaf_object->GetIntAttribute(ui::AX_ATTR_PREVIOUS_ON_LINE_ID,
                                   &previous_on_line_id)) {
    const BrowserAccessibility* previous_on_line =
        manager()->GetFromID(previous_on_line_id);
    // In the case of a static text sibling, the object designated to be the
    // previous object on this line might be one of its children, i.e. the last
    // inline text box.
    return previous_on_line &&
           previous_on_line->IsDescendantOf(previous_sibling);
  }
  return false;
}

bool BrowserAccessibility::IsNextSiblingOnSameLine() const {
  const BrowserAccessibility* next_sibling = GetNextSibling();
  if (!next_sibling)
    return false;

  // Line linkage information might not be provided on non-leaf objects.
  const BrowserAccessibility* leaf_object = PlatformDeepestLastChild();
  if (!leaf_object)
    leaf_object = this;

  int32_t next_on_line_id;
  if (leaf_object->GetIntAttribute(ui::AX_ATTR_NEXT_ON_LINE_ID,
                                   &next_on_line_id)) {
    const BrowserAccessibility* next_on_line =
        manager()->GetFromID(next_on_line_id);
    // In the case of a static text sibling, the object designated to be the
    // next object on this line might be one of its children, i.e. the first
    // inline text box.
    return next_on_line && next_on_line->IsDescendantOf(next_sibling);
  }
  return false;
}

BrowserAccessibility* BrowserAccessibility::PlatformDeepestFirstChild() const {
  if (!PlatformChildCount())
    return nullptr;

  BrowserAccessibility* deepest_child = PlatformGetChild(0);
  while (deepest_child->PlatformChildCount())
    deepest_child = deepest_child->PlatformGetChild(0);

  return deepest_child;
}

BrowserAccessibility* BrowserAccessibility::PlatformDeepestLastChild() const {
  if (!PlatformChildCount())
    return nullptr;

  BrowserAccessibility* deepest_child =
      PlatformGetChild(PlatformChildCount() - 1);
  while (deepest_child->PlatformChildCount()) {
    deepest_child = deepest_child->PlatformGetChild(
        deepest_child->PlatformChildCount() - 1);
  }

  return deepest_child;
}

BrowserAccessibility* BrowserAccessibility::InternalDeepestFirstChild() const {
  if (!InternalChildCount())
    return nullptr;

  BrowserAccessibility* deepest_child = InternalGetChild(0);
  while (deepest_child->InternalChildCount())
    deepest_child = deepest_child->InternalGetChild(0);

  return deepest_child;
}

BrowserAccessibility* BrowserAccessibility::InternalDeepestLastChild() const {
  if (!InternalChildCount())
    return nullptr;

  BrowserAccessibility* deepest_child =
      InternalGetChild(InternalChildCount() - 1);
  while (deepest_child->InternalChildCount()) {
    deepest_child = deepest_child->InternalGetChild(
        deepest_child->InternalChildCount() - 1);
  }

  return deepest_child;
}

uint32_t BrowserAccessibility::InternalChildCount() const {
  if (!node_ || !manager_)
    return 0;
  return static_cast<uint32_t>(node_->child_count());
}

BrowserAccessibility* BrowserAccessibility::InternalGetChild(
    uint32_t child_index) const {
  if (!node_ || !manager_ || child_index >= InternalChildCount())
    return nullptr;

  auto* child_node = node_->ChildAtIndex(child_index);
  DCHECK(child_node);
  return manager_->GetFromAXNode(child_node);
}

BrowserAccessibility* BrowserAccessibility::PlatformGetParent() const {
  if (!instance_active())
    return nullptr;

  ui::AXNode* parent = node_->parent();
  if (parent)
    return manager_->GetFromAXNode(parent);

  return manager_->GetParentNodeFromParentTree();
}

BrowserAccessibility* BrowserAccessibility::InternalGetParent() const {
  if (!node_ || !manager_)
    return nullptr;
  ui::AXNode* parent = node_->parent();
  if (parent)
    return manager_->GetFromAXNode(parent);

  return nullptr;
}

int32_t BrowserAccessibility::GetIndexInParent() const {
  return node_ ? node_->index_in_parent() : -1;
}

int32_t BrowserAccessibility::GetId() const {
  return node_ ? node_->id() : -1;
}

gfx::RectF BrowserAccessibility::GetLocation() const {
  return GetData().location;
}

ui::AXRole BrowserAccessibility::GetRole() const {
  return GetData().role;
}

int32_t BrowserAccessibility::GetState() const {
  return GetData().state;
}

const BrowserAccessibility::HtmlAttributes&
BrowserAccessibility::GetHtmlAttributes() const {
  return GetData().html_attributes;
}

gfx::Rect BrowserAccessibility::GetFrameBoundsRect() const {
  gfx::RectF bounds = GetLocation();
  FixEmptyBounds(&bounds);
  return RelativeToAbsoluteBounds(bounds, true);
}

gfx::Rect BrowserAccessibility::GetPageBoundsRect() const {
  gfx::RectF bounds = GetLocation();
  FixEmptyBounds(&bounds);
  return RelativeToAbsoluteBounds(bounds, false);
}

gfx::Rect BrowserAccessibility::GetPageBoundsForRange(int start, int len)
    const {
  DCHECK_GE(start, 0);
  DCHECK_GE(len, 0);

  // Standard text fields such as textarea have an embedded div inside them that
  // holds all the text.
  // TODO(nektar): This is fragile! Replace with code that flattens tree.
  if (IsSimpleTextControl() && InternalChildCount() == 1)
    return InternalGetChild(0)->GetPageBoundsForRange(start, len);

  if (GetRole() != ui::AX_ROLE_STATIC_TEXT) {
    gfx::Rect bounds;
    for (size_t i = 0; i < InternalChildCount() && len > 0; ++i) {
      BrowserAccessibility* child = InternalGetChild(i);
      // Child objects are of length one, since they are represented by a single
      // embedded object character. The exception is text-only objects.
      int child_length_in_parent = 1;
      if (child->IsTextOnlyObject())
        child_length_in_parent = static_cast<int>(child->GetText().size());
      if (start < child_length_in_parent) {
        gfx::Rect child_rect;
        if (child->IsTextOnlyObject()) {
          child_rect = child->GetPageBoundsForRange(start, len);
        } else {
          child_rect = child->GetPageBoundsForRange(
              0, static_cast<int>(child->GetText().size()));
        }
        bounds.Union(child_rect);
        len -= (child_length_in_parent - start);
      }
      if (start > child_length_in_parent)
        start -= child_length_in_parent;
      else
        start = 0;
    }
    return bounds;
  }

  int end = start + len;
  int child_start = 0;
  int child_end = 0;
  gfx::Rect bounds;
  for (size_t i = 0; i < InternalChildCount() && child_end < start + len; ++i) {
    BrowserAccessibility* child = InternalGetChild(i);
    if (child->GetRole() != ui::AX_ROLE_INLINE_TEXT_BOX) {
      DLOG(WARNING) << "BrowserAccessibility objects with role STATIC_TEXT " <<
          "should have children of role INLINE_TEXT_BOX.";
      continue;
    }

    int child_length = static_cast<int>(child->GetText().size());
    child_start = child_end;
    child_end += child_length;

    if (child_end < start)
      continue;

    int overlap_start = std::max(start, child_start);
    int overlap_end = std::min(end, child_end);

    int local_start = overlap_start - child_start;
    int local_end = overlap_end - child_start;
    // |local_end| and |local_start| may equal |child_length| when the caret is
    // at the end of a text field.
    DCHECK_GE(local_start, 0);
    DCHECK_LE(local_start, child_length);
    DCHECK_GE(local_end, 0);
    DCHECK_LE(local_end, child_length);

    const std::vector<int32_t>& character_offsets =
        child->GetIntListAttribute(ui::AX_ATTR_CHARACTER_OFFSETS);
    int character_offsets_length = static_cast<int>(character_offsets.size());
    if (character_offsets_length < child_length) {
      // Blink might not return pixel offsets for all characters.
      // Clamp the character range to be within the number of provided pixels.
      local_start = std::min(local_start, character_offsets_length);
      local_end = std::min(local_end, character_offsets_length);
    }
    int start_pixel_offset =
        local_start > 0 ? character_offsets[local_start - 1] : 0;
    int end_pixel_offset =
        local_end > 0 ? character_offsets[local_end - 1] : 0;

    gfx::Rect child_rect = child->GetPageBoundsRect();
    auto text_direction = static_cast<ui::AXTextDirection>(
        child->GetIntAttribute(ui::AX_ATTR_TEXT_DIRECTION));
    gfx::Rect child_overlap_rect;
    switch (text_direction) {
      case ui::AX_TEXT_DIRECTION_NONE:
      case ui::AX_TEXT_DIRECTION_LTR: {
        int left = child_rect.x() + start_pixel_offset;
        int right = child_rect.x() + end_pixel_offset;
        child_overlap_rect = gfx::Rect(left, child_rect.y(),
                                       right - left, child_rect.height());
        break;
      }
      case ui::AX_TEXT_DIRECTION_RTL: {
        int right = child_rect.right() - start_pixel_offset;
        int left = child_rect.right() - end_pixel_offset;
        child_overlap_rect = gfx::Rect(left, child_rect.y(),
                                       right - left, child_rect.height());
        break;
      }
      case ui::AX_TEXT_DIRECTION_TTB: {
        int top = child_rect.y() + start_pixel_offset;
        int bottom = child_rect.y() + end_pixel_offset;
        child_overlap_rect = gfx::Rect(child_rect.x(), top,
                                       child_rect.width(), bottom - top);
        break;
      }
      case ui::AX_TEXT_DIRECTION_BTT: {
        int bottom = child_rect.bottom() - start_pixel_offset;
        int top = child_rect.bottom() - end_pixel_offset;
        child_overlap_rect = gfx::Rect(child_rect.x(), top,
                                       child_rect.width(), bottom - top);
        break;
      }
    }

    if (bounds.width() == 0 && bounds.height() == 0)
      bounds = child_overlap_rect;
    else
      bounds.Union(child_overlap_rect);
  }

  return bounds;
}

gfx::Rect BrowserAccessibility::GetScreenBoundsForRange(int start, int len)
    const {
  gfx::Rect bounds = GetPageBoundsForRange(start, len);

  // Adjust the bounds by the top left corner of the containing view's bounds
  // in screen coordinates.
  bounds.Offset(manager_->GetViewBounds().OffsetFromOrigin());

  return bounds;
}

base::string16 BrowserAccessibility::GetValue() const {
  base::string16 value = GetString16Attribute(ui::AX_ATTR_VALUE);
  // Some screen readers like Jaws and older versions of VoiceOver require a
  // value to be set in text fields with rich content, even though the same
  // information is available on the children.
  if (value.empty() &&
      (IsSimpleTextControl() || IsRichTextControl()) &&
      !IsNativeTextControl())
    value = GetInnerText();
  return value;
}

BrowserAccessibility* BrowserAccessibility::ApproximateHitTest(
    const gfx::Point& point) {
  // The best result found that's a child of this object.
  BrowserAccessibility* child_result = NULL;
  // The best result that's an indirect descendant like grandchild, etc.
  BrowserAccessibility* descendant_result = NULL;

  // Walk the children recursively looking for the BrowserAccessibility that
  // most tightly encloses the specified point. Walk backwards so that in
  // the absence of any other information, we assume the object that occurs
  // later in the tree is on top of one that comes before it.
  for (int i = static_cast<int>(PlatformChildCount()) - 1; i >= 0; --i) {
    BrowserAccessibility* child = PlatformGetChild(i);

    // Skip table columns because cells are only contained in rows,
    // not columns.
    if (child->GetRole() == ui::AX_ROLE_COLUMN)
      continue;

    if (child->GetScreenBoundsRect().Contains(point)) {
      BrowserAccessibility* result = child->ApproximateHitTest(point);
      if (result == child && !child_result)
        child_result = result;
      if (result != child && !descendant_result)
        descendant_result = result;
    }

    if (child_result && descendant_result)
      break;
  }

  // Explanation of logic: it's possible that this point overlaps more than
  // one child of this object. If so, as a heuristic we prefer if the point
  // overlaps a descendant of one of the two children and not the other.
  // As an example, suppose you have two rows of buttons - the buttons don't
  // overlap, but the rows do. Without this heuristic, we'd greedily only
  // consider one of the containers.
  if (descendant_result)
    return descendant_result;
  if (child_result)
    return child_result;

  return this;
}

void BrowserAccessibility::Destroy() {
  // Allow the object to fire a TextRemoved notification.
  manager()->NotifyAccessibilityEvent(
      BrowserAccessibilityEvent::FromTreeChange,
      ui::AX_EVENT_HIDE,
      this);
  node_ = NULL;
  manager_ = NULL;

  if (unique_id_)
    g_unique_id_map.Get().erase(unique_id_);
  unique_id_ = 0;

  NativeReleaseReference();
}

void BrowserAccessibility::NativeReleaseReference() {
  delete this;
}

bool BrowserAccessibility::HasBoolAttribute(
    ui::AXBoolAttribute attribute) const {
  return GetData().HasBoolAttribute(attribute);
}

bool BrowserAccessibility::GetBoolAttribute(
    ui::AXBoolAttribute attribute) const {
  return GetData().GetBoolAttribute(attribute);
}

bool BrowserAccessibility::GetBoolAttribute(
    ui::AXBoolAttribute attribute, bool* value) const {
  return GetData().GetBoolAttribute(attribute, value);
}

bool BrowserAccessibility::HasFloatAttribute(
    ui::AXFloatAttribute attribute) const {
  return GetData().HasFloatAttribute(attribute);
}

float BrowserAccessibility::GetFloatAttribute(
    ui::AXFloatAttribute attribute) const {
  return GetData().GetFloatAttribute(attribute);
}

bool BrowserAccessibility::GetFloatAttribute(
    ui::AXFloatAttribute attribute, float* value) const {
  return GetData().GetFloatAttribute(attribute, value);
}

bool BrowserAccessibility::HasInheritedStringAttribute(
    ui::AXStringAttribute attribute) const {
  if (!instance_active())
    return false;

  if (GetData().HasStringAttribute(attribute))
    return true;
  return PlatformGetParent() &&
         PlatformGetParent()->HasInheritedStringAttribute(attribute);
}

const std::string& BrowserAccessibility::GetInheritedStringAttribute(
    ui::AXStringAttribute attribute) const {
  if (!instance_active())
    return base::EmptyString();

  const BrowserAccessibility* current_object = this;
  do {
    if (current_object->GetData().HasStringAttribute(attribute))
      return current_object->GetData().GetStringAttribute(attribute);
    current_object = current_object->PlatformGetParent();
  } while (current_object);
  return base::EmptyString();
}

bool BrowserAccessibility::GetInheritedStringAttribute(
    ui::AXStringAttribute attribute,
    std::string* value) const {
  if (!instance_active()) {
    *value = std::string();
    return false;
  }

  if (GetData().GetStringAttribute(attribute, value))
    return true;
  return PlatformGetParent() &&
         PlatformGetParent()->GetData().GetStringAttribute(attribute, value);
}

base::string16 BrowserAccessibility::GetInheritedString16Attribute(
    ui::AXStringAttribute attribute) const {
  if (!instance_active())
    return base::string16();

  const BrowserAccessibility* current_object = this;
  do {
    if (current_object->GetData().HasStringAttribute(attribute))
      return current_object->GetData().GetString16Attribute(attribute);
    current_object = current_object->PlatformGetParent();
  } while (current_object);
  return base::string16();
}

bool BrowserAccessibility::GetInheritedString16Attribute(
    ui::AXStringAttribute attribute,
    base::string16* value) const {
  if (!instance_active()) {
    *value = base::string16();
    return false;
  }

  if (GetData().GetString16Attribute(attribute, value))
    return true;
  return PlatformGetParent() &&
         PlatformGetParent()->GetData().GetString16Attribute(attribute, value);
}

bool BrowserAccessibility::HasIntAttribute(
    ui::AXIntAttribute attribute) const {
  return GetData().HasIntAttribute(attribute);
}

int BrowserAccessibility::GetIntAttribute(ui::AXIntAttribute attribute) const {
  return GetData().GetIntAttribute(attribute);
}

bool BrowserAccessibility::GetIntAttribute(
    ui::AXIntAttribute attribute, int* value) const {
  return GetData().GetIntAttribute(attribute, value);
}

bool BrowserAccessibility::HasStringAttribute(
    ui::AXStringAttribute attribute) const {
  return GetData().HasStringAttribute(attribute);
}

const std::string& BrowserAccessibility::GetStringAttribute(
    ui::AXStringAttribute attribute) const {
  return GetData().GetStringAttribute(attribute);
}

bool BrowserAccessibility::GetStringAttribute(
    ui::AXStringAttribute attribute, std::string* value) const {
  return GetData().GetStringAttribute(attribute, value);
}

base::string16 BrowserAccessibility::GetString16Attribute(
    ui::AXStringAttribute attribute) const {
  return GetData().GetString16Attribute(attribute);
}

bool BrowserAccessibility::GetString16Attribute(ui::AXStringAttribute attribute,
                                                base::string16* value) const {
  return GetData().GetString16Attribute(attribute, value);
}

bool BrowserAccessibility::HasIntListAttribute(
    ui::AXIntListAttribute attribute) const {
  return GetData().HasIntListAttribute(attribute);
}

const std::vector<int32_t>& BrowserAccessibility::GetIntListAttribute(
    ui::AXIntListAttribute attribute) const {
  return GetData().GetIntListAttribute(attribute);
}

bool BrowserAccessibility::GetIntListAttribute(
    ui::AXIntListAttribute attribute,
    std::vector<int32_t>* value) const {
  return GetData().GetIntListAttribute(attribute, value);
}

bool BrowserAccessibility::GetHtmlAttribute(
    const char* html_attr, std::string* value) const {
  return GetData().GetHtmlAttribute(html_attr, value);
}

bool BrowserAccessibility::GetHtmlAttribute(
    const char* html_attr, base::string16* value) const {
  return GetData().GetHtmlAttribute(html_attr, value);
}

bool BrowserAccessibility::GetAriaTristate(
    const char* html_attr,
    bool* is_defined,
    bool* is_mixed) const {
  *is_defined = false;
  *is_mixed = false;

  base::string16 value;
  if (!GetHtmlAttribute(html_attr, &value) ||
      value.empty() ||
      base::EqualsASCII(value, "undefined")) {
    return false;  // Not set (and *is_defined is also false)
  }

  *is_defined = true;

  if (base::EqualsASCII(value, "true"))
    return true;

  if (base::EqualsASCII(value, "mixed"))
    *is_mixed = true;

  return false;  // Not set.
}

BrowserAccessibility* BrowserAccessibility::GetTable() const {
  BrowserAccessibility* table = const_cast<BrowserAccessibility*>(this);
  while (table && !table->IsTableLikeRole())
    table = table->PlatformGetParent();
  return table;
}

BrowserAccessibility* BrowserAccessibility::GetTableCell(int index) const {
  if (!IsTableLikeRole() && !IsCellOrTableHeaderRole())
    return nullptr;

  BrowserAccessibility* table = GetTable();
  if (!table)
    return nullptr;
  const std::vector<int32_t>& unique_cell_ids =
      table->GetIntListAttribute(ui::AX_ATTR_UNIQUE_CELL_IDS);
  if (index < 0 || index >= static_cast<int>(unique_cell_ids.size()))
    return nullptr;
  return table->manager_->GetFromID(unique_cell_ids[index]);
}

BrowserAccessibility* BrowserAccessibility::GetTableCell(int row,
                                                         int column) const {
  if (!IsTableLikeRole() && !IsCellOrTableHeaderRole())
    return nullptr;
  if (row < 0 || row >= GetTableRowCount() || column < 0 ||
      column >= GetTableColumnCount()) {
    return nullptr;
  }

  BrowserAccessibility* table = GetTable();
  if (!table)
    return nullptr;

  // In contrast to unique cell IDs, these are duplicated whenever a cell spans
  // multiple columns or rows.
  const std::vector<int32_t>& cell_ids =
      table->GetIntListAttribute(ui::AX_ATTR_CELL_IDS);
  DCHECK_EQ(GetTableRowCount() * GetTableColumnCount(),
            static_cast<int>(cell_ids.size()));
  int position = row * GetTableColumnCount() + column;
  if (position < 0 || position >= static_cast<int>(cell_ids.size()))
    return nullptr;
  return table->manager_->GetFromID(cell_ids[position]);
}

int BrowserAccessibility::GetTableCellIndex() const {
  if (!IsCellOrTableHeaderRole())
    return -1;

  BrowserAccessibility* table = GetTable();
  if (!table)
    return -1;

  const std::vector<int32_t>& unique_cell_ids =
      table->GetIntListAttribute(ui::AX_ATTR_UNIQUE_CELL_IDS);
  auto iter =
      std::find(unique_cell_ids.begin(), unique_cell_ids.end(), GetId());
  if (iter == unique_cell_ids.end())
    return -1;

  return std::distance(unique_cell_ids.begin(), iter);
}

int BrowserAccessibility::GetTableColumn() const {
  return GetIntAttribute(ui::AX_ATTR_TABLE_CELL_COLUMN_INDEX);
}

int BrowserAccessibility::GetTableColumnCount() const {
  BrowserAccessibility* table = GetTable();
  if (!table)
    return 0;

  return table->GetIntAttribute(ui::AX_ATTR_TABLE_COLUMN_COUNT);
}

int BrowserAccessibility::GetTableColumnSpan() const {
  if (!IsCellOrTableHeaderRole())
    return 0;

  int column_span;
  if (GetIntAttribute(ui::AX_ATTR_TABLE_CELL_COLUMN_SPAN, &column_span))
    return column_span;
  return 1;
}

int BrowserAccessibility::GetTableRow() const {
  return GetIntAttribute(ui::AX_ATTR_TABLE_CELL_ROW_INDEX);
}

int BrowserAccessibility::GetTableRowCount() const {
  BrowserAccessibility* table = GetTable();
  if (!table)
    return 0;

  return table->GetIntAttribute(ui::AX_ATTR_TABLE_ROW_COUNT);
}

int BrowserAccessibility::GetTableRowSpan() const {
  if (!IsCellOrTableHeaderRole())
    return 0;

  int row_span;
  if (GetIntAttribute(ui::AX_ATTR_TABLE_CELL_ROW_SPAN, &row_span))
    return row_span;
  return 1;
}

base::string16 BrowserAccessibility::GetText() const {
  return GetInnerText();
}

bool BrowserAccessibility::HasState(ui::AXState state_enum) const {
  return GetData().HasState(state_enum);
}

bool BrowserAccessibility::HasAction(ui::AXAction action_enum) const {
  return GetData().HasAction(action_enum);
}

bool BrowserAccessibility::IsCellOrTableHeaderRole() const {
  return (GetRole() == ui::AX_ROLE_CELL ||
          GetRole() == ui::AX_ROLE_COLUMN_HEADER ||
          GetRole() == ui::AX_ROLE_ROW_HEADER);
}

bool BrowserAccessibility::IsTableLikeRole() const {
  return (GetRole() == ui::AX_ROLE_TABLE ||
          GetRole() == ui::AX_ROLE_GRID ||
          GetRole() == ui::AX_ROLE_TREE_GRID);
}

bool BrowserAccessibility::HasCaret() const {
  if (IsSimpleTextControl() && HasIntAttribute(ui::AX_ATTR_TEXT_SEL_START) &&
      HasIntAttribute(ui::AX_ATTR_TEXT_SEL_END)) {
    return true;
  }

  // The caret is always at the focus of the selection.
  int32_t focus_id = manager()->GetTreeData().sel_focus_object_id;
  BrowserAccessibility* focus_object = manager()->GetFromID(focus_id);
  if (!focus_object)
    return false;

  return focus_object->IsDescendantOf(this);
}

bool BrowserAccessibility::IsWebAreaForPresentationalIframe() const {
  if (GetRole() != ui::AX_ROLE_WEB_AREA &&
      GetRole() != ui::AX_ROLE_ROOT_WEB_AREA) {
    return false;
  }

  BrowserAccessibility* parent = PlatformGetParent();
  if (!parent)
    return false;

  return parent->GetRole() == ui::AX_ROLE_IFRAME_PRESENTATIONAL;
}

bool BrowserAccessibility::IsClickable() const {
  return ui::IsRoleClickable(GetRole());
}

bool BrowserAccessibility::IsControl() const {
  switch (GetRole()) {
    case ui::AX_ROLE_BUTTON:
    case ui::AX_ROLE_CHECK_BOX:
    case ui::AX_ROLE_COLOR_WELL:
    case ui::AX_ROLE_COMBO_BOX:
    case ui::AX_ROLE_DISCLOSURE_TRIANGLE:
    case ui::AX_ROLE_LIST_BOX:
    case ui::AX_ROLE_MENU:
    case ui::AX_ROLE_MENU_BAR:
    case ui::AX_ROLE_MENU_BUTTON:
    case ui::AX_ROLE_MENU_ITEM:
    case ui::AX_ROLE_MENU_ITEM_CHECK_BOX:
    case ui::AX_ROLE_MENU_ITEM_RADIO:
    case ui::AX_ROLE_MENU_LIST_OPTION:
    case ui::AX_ROLE_MENU_LIST_POPUP:
    case ui::AX_ROLE_POP_UP_BUTTON:
    case ui::AX_ROLE_RADIO_BUTTON:
    case ui::AX_ROLE_SCROLL_BAR:
    case ui::AX_ROLE_SEARCH_BOX:
    case ui::AX_ROLE_SLIDER:
    case ui::AX_ROLE_SPIN_BUTTON:
    case ui::AX_ROLE_SWITCH:
    case ui::AX_ROLE_TAB:
    case ui::AX_ROLE_TEXT_FIELD:
    case ui::AX_ROLE_TOGGLE_BUTTON:
    case ui::AX_ROLE_TREE:
      return true;
    default:
      return false;
  }
}

bool BrowserAccessibility::IsMenuRelated() const {
  switch (GetRole()) {
    case ui::AX_ROLE_MENU:
    case ui::AX_ROLE_MENU_BAR:
    case ui::AX_ROLE_MENU_BUTTON:
    case ui::AX_ROLE_MENU_ITEM:
    case ui::AX_ROLE_MENU_ITEM_CHECK_BOX:
    case ui::AX_ROLE_MENU_ITEM_RADIO:
    case ui::AX_ROLE_MENU_LIST_OPTION:
    case ui::AX_ROLE_MENU_LIST_POPUP:
      return true;
    default:
      return false;
  }
}

bool BrowserAccessibility::IsNativeTextControl() const {
  const std::string& html_tag = GetStringAttribute(ui::AX_ATTR_HTML_TAG);
  if (html_tag == "input") {
    std::string input_type;
    if (!GetHtmlAttribute("type", &input_type))
      return true;
    return input_type.empty() || input_type == "email" ||
           input_type == "password" || input_type == "search" ||
           input_type == "tel" || input_type == "text" || input_type == "url" ||
           input_type == "number";
  }
  return html_tag == "textarea";
}

bool BrowserAccessibility::IsSimpleTextControl() const {
  // Time fields, color wells and spinner buttons might also use text fields as
  // constituent parts, but they are not considered text fields as a whole.
  switch (GetRole()) {
    case ui::AX_ROLE_COMBO_BOX:
    case ui::AX_ROLE_SEARCH_BOX:
      return true;
    case ui::AX_ROLE_TEXT_FIELD:
      return !HasState(ui::AX_STATE_RICHLY_EDITABLE);
    default:
      return false;
  }
}

// Indicates if this object is at the root of a rich edit text control.
bool BrowserAccessibility::IsRichTextControl() const {
  return HasState(ui::AX_STATE_RICHLY_EDITABLE) &&
         (!PlatformGetParent() ||
          !PlatformGetParent()->HasState(ui::AX_STATE_RICHLY_EDITABLE));
}

bool BrowserAccessibility::HasExplicitlyEmptyName() const {
  return GetIntAttribute(ui::AX_ATTR_NAME_FROM) ==
         ui::AX_NAME_FROM_ATTRIBUTE_EXPLICITLY_EMPTY;
}

std::string BrowserAccessibility::ComputeAccessibleNameFromDescendants() const {
  std::string name;
  for (size_t i = 0; i < InternalChildCount(); ++i) {
    BrowserAccessibility* child = InternalGetChild(i);
    std::string child_name;
    if (child->GetStringAttribute(ui::AX_ATTR_NAME, &child_name)) {
      if (!name.empty())
        name += " ";
      name += child_name;
    } else if (!child->HasState(ui::AX_STATE_FOCUSABLE)) {
      child_name = child->ComputeAccessibleNameFromDescendants();
      if (!child_name.empty()) {
        if (!name.empty())
          name += " ";
        name += child_name;
      }
    }
  }

  return name;
}

std::vector<int> BrowserAccessibility::GetLineStartOffsets() const {
  if (!instance_active())
    return std::vector<int>();
  return node()->GetOrComputeLineStartOffsets();
}

BrowserAccessibility::AXPlatformPositionInstance
BrowserAccessibility::CreatePositionAt(int offset,
                                       ui::AXTextAffinity affinity) const {
  DCHECK(manager_);
  return AXPlatformPosition::CreateTextPosition(manager_->ax_tree_id(), GetId(),
                                                offset, affinity);
}

base::string16 BrowserAccessibility::GetInnerText() const {
  if (IsTextOnlyObject())
    return GetString16Attribute(ui::AX_ATTR_NAME);

  base::string16 text;
  for (size_t i = 0; i < InternalChildCount(); ++i)
    text += InternalGetChild(i)->GetInnerText();
  return text;
}

void BrowserAccessibility::FixEmptyBounds(gfx::RectF* bounds) const {
  if (bounds->width() > 0 && bounds->height() > 0)
    return;

  for (size_t i = 0; i < InternalChildCount(); ++i) {
    // Compute the bounds of each child - this calls FixEmptyBounds
    // recursively if necessary.
    BrowserAccessibility* child = InternalGetChild(i);
    gfx::Rect child_bounds = child->GetPageBoundsRect();

    // Ignore children that don't have valid bounds themselves.
    if (child_bounds.width() == 0 || child_bounds.height() == 0)
      continue;

    // For the first valid child, just set the bounds to that child's bounds.
    if (bounds->width() == 0 || bounds->height() == 0) {
      *bounds = gfx::RectF(child_bounds);
      continue;
    }

    // Union each additional child's bounds.
    bounds->Union(gfx::RectF(child_bounds));
  }
}

gfx::Rect BrowserAccessibility::RelativeToAbsoluteBounds(
    gfx::RectF bounds,
    bool frame_only) const {
  const BrowserAccessibility* node = this;
  while (node) {
    if (node->GetData().transform)
      node->GetData().transform->TransformRect(&bounds);

    const BrowserAccessibility* container =
        node->manager()->GetFromID(node->GetData().offset_container_id);
    if (!container) {
      if (node == node->manager()->GetRoot() && !frame_only) {
        container = node->PlatformGetParent();
      } else {
        container = node->manager()->GetRoot();
      }
    }

    if (!container || container == node)
      break;

    gfx::RectF container_bounds = container->GetLocation();
    bounds.Offset(container_bounds.x(), container_bounds.y());

    if (container->manager()->UseRootScrollOffsetsWhenComputingBounds() ||
        container->PlatformGetParent()) {
      int sx = 0;
      int sy = 0;
      if (container->GetIntAttribute(ui::AX_ATTR_SCROLL_X, &sx) &&
          container->GetIntAttribute(ui::AX_ATTR_SCROLL_Y, &sy)) {
        bounds.Offset(-sx, -sy);
      }
    }

    node = container;
  }

  return gfx::ToEnclosingRect(bounds);
}

gfx::NativeViewAccessible BrowserAccessibility::GetNativeViewAccessible() {
  // TODO(703369) On Windows, where we have started to migrate to an
  // AXPlatformNode implementation, the BrowserAccessibilityWin subclass has
  // overridden this method. On all other platforms, this method should not be
  // called yet. In the future, when all subclasses have moved over to be
  // implemented by AXPlatformNode, we may make this method completely virtual.
  NOTREACHED();
  return nullptr;
}

//
// AXPlatformNodeDelegate.
//
const ui::AXNodeData& BrowserAccessibility::GetData() const {
  CR_DEFINE_STATIC_LOCAL(ui::AXNodeData, empty_data, ());
  if (node_)
    return node_->data();
  else
    return empty_data;
}

gfx::NativeWindow BrowserAccessibility::GetTopLevelWidget() {
  NOTREACHED();
  return nullptr;
}

gfx::NativeViewAccessible BrowserAccessibility::GetParent() {
  auto* parent = PlatformGetParent();
  if (!parent)
    return nullptr;

  return parent->GetNativeViewAccessible();
}

int BrowserAccessibility::GetChildCount() {
  return PlatformChildCount();
}

gfx::NativeViewAccessible BrowserAccessibility::ChildAtIndex(int index) {
  auto* child = PlatformGetChild(index);
  if (!child)
    return nullptr;

  return child->GetNativeViewAccessible();
}

gfx::Rect BrowserAccessibility::GetScreenBoundsRect() const {
  gfx::Rect bounds = GetPageBoundsRect();

  // Adjust the bounds by the top left corner of the containing view's bounds
  // in screen coordinates.
  bounds.Offset(manager_->GetViewBounds().OffsetFromOrigin());

  return bounds;
}

gfx::NativeViewAccessible BrowserAccessibility::HitTestSync(int x, int y) {
  NOTREACHED();
  return nullptr;
}

gfx::NativeViewAccessible BrowserAccessibility::GetFocus() {
  NOTREACHED();
  return nullptr;
}

gfx::AcceleratedWidget
BrowserAccessibility::GetTargetForNativeAccessibilityEvent() {
  NOTREACHED();
  return gfx::kNullAcceleratedWidget;
}

bool BrowserAccessibility::AccessibilityPerformAction(
    const ui::AXActionData& data) {
  NOTREACHED();
  return false;
}

}  // namespace content
