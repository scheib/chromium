// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/accessibility/ax_node_data.h"

#include <stddef.h>

#include <algorithm>
#include <set>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/accessibility/ax_text_utils.h"
#include "ui/gfx/transform.h"

using base::DoubleToString;
using base::IntToString;

namespace ui {

namespace {

bool IsFlagSet(uint32_t bitfield, uint32_t flag) {
  return 0 != (bitfield & (1 << flag));
}

uint32_t ModifyFlag(uint32_t bitfield, uint32_t flag, bool set) {
  return set ? (bitfield |= (1 << flag)) : (bitfield &= ~(1 << flag));
}

std::string StateBitfieldToString(uint32_t state) {
  std::string str;
  for (uint32_t i = AX_STATE_NONE + 1; i <= AX_STATE_LAST; ++i) {
    if (IsFlagSet(state, i))
      str += " " + base::ToUpperASCII(ToString(static_cast<AXState>(i)));
  }
  return str;
}

std::string ActionsBitfieldToString(uint32_t actions) {
  std::string str;
  for (uint32_t i = AX_ACTION_NONE + 1; i <= AX_ACTION_LAST; ++i) {
    if (IsFlagSet(actions, i)) {
      str += ToString(static_cast<AXAction>(i));
      actions = ModifyFlag(actions, i, false);
      str += actions ? "," : "";
    }
  }
  return str;
}

std::string IntVectorToString(const std::vector<int>& items) {
  std::string str;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0)
      str += ",";
    str += IntToString(items[i]);
  }
  return str;
}

// Predicate that returns true if the first value of a pair is |first|.
template<typename FirstType, typename SecondType>
struct FirstIs {
  FirstIs(FirstType first)
      : first_(first) {}
  bool operator()(std::pair<FirstType, SecondType> const& p) {
    return p.first == first_;
  }
  FirstType first_;
};

// Helper function that finds a key in a vector of pairs by matching on the
// first value, and returns an iterator.
template<typename FirstType, typename SecondType>
typename std::vector<std::pair<FirstType, SecondType>>::const_iterator
    FindInVectorOfPairs(
        FirstType first,
        const std::vector<std::pair<FirstType, SecondType>>& vector) {
  return std::find_if(vector.begin(),
                      vector.end(),
                      FirstIs<FirstType, SecondType>(first));
}

}  // namespace

// Return true if |attr| is a node ID that would need to be mapped when
// renumbering the ids in a combined tree.
bool IsNodeIdIntAttribute(AXIntAttribute attr) {
  switch (attr) {
    case AX_ATTR_ACTIVEDESCENDANT_ID:
    case AX_ATTR_ERRORMESSAGE_ID:
    case AX_ATTR_IN_PAGE_LINK_TARGET_ID:
    case AX_ATTR_MEMBER_OF_ID:
    case AX_ATTR_NEXT_ON_LINE_ID:
    case AX_ATTR_PREVIOUS_ON_LINE_ID:
    case AX_ATTR_TABLE_HEADER_ID:
    case AX_ATTR_TABLE_COLUMN_HEADER_ID:
    case AX_ATTR_TABLE_ROW_HEADER_ID:
      return true;

    // Note: all of the attributes are included here explicitly,
    // rather than using "default:", so that it's a compiler error to
    // add a new attribute without explicitly considering whether it's
    // a node id attribute or not.
    case AX_INT_ATTRIBUTE_NONE:
    case AX_ATTR_DEFAULT_ACTION_VERB:
    case AX_ATTR_SCROLL_X:
    case AX_ATTR_SCROLL_X_MIN:
    case AX_ATTR_SCROLL_X_MAX:
    case AX_ATTR_SCROLL_Y:
    case AX_ATTR_SCROLL_Y_MIN:
    case AX_ATTR_SCROLL_Y_MAX:
    case AX_ATTR_TEXT_SEL_START:
    case AX_ATTR_TEXT_SEL_END:
    case AX_ATTR_TABLE_ROW_COUNT:
    case AX_ATTR_TABLE_COLUMN_COUNT:
    case AX_ATTR_TABLE_ROW_INDEX:
    case AX_ATTR_TABLE_COLUMN_INDEX:
    case AX_ATTR_TABLE_CELL_COLUMN_INDEX:
    case AX_ATTR_TABLE_CELL_COLUMN_SPAN:
    case AX_ATTR_TABLE_CELL_ROW_INDEX:
    case AX_ATTR_TABLE_CELL_ROW_SPAN:
    case AX_ATTR_SORT_DIRECTION:
    case AX_ATTR_HIERARCHICAL_LEVEL:
    case AX_ATTR_NAME_FROM:
    case AX_ATTR_DESCRIPTION_FROM:
    case AX_ATTR_CHILD_TREE_ID:
    case AX_ATTR_SET_SIZE:
    case AX_ATTR_POS_IN_SET:
    case AX_ATTR_COLOR_VALUE:
    case AX_ATTR_ARIA_CURRENT_STATE:
    case AX_ATTR_BACKGROUND_COLOR:
    case AX_ATTR_COLOR:
    case AX_ATTR_INVALID_STATE:
    case AX_ATTR_CHECKED_STATE:
    case AX_ATTR_TEXT_DIRECTION:
    case AX_ATTR_TEXT_STYLE:
    case AX_ATTR_ARIA_COLUMN_COUNT:
    case AX_ATTR_ARIA_CELL_COLUMN_INDEX:
    case AX_ATTR_ARIA_ROW_COUNT:
    case AX_ATTR_ARIA_CELL_ROW_INDEX:
      return false;
  }

  NOTREACHED();
  return false;
}

// Return true if |attr| contains a vector of node ids that would need
// to be mapped when renumbering the ids in a combined tree.
bool IsNodeIdIntListAttribute(AXIntListAttribute attr) {
  switch (attr) {
    case AX_ATTR_CELL_IDS:
    case AX_ATTR_CONTROLS_IDS:
    case AX_ATTR_DESCRIBEDBY_IDS:
    case AX_ATTR_DETAILS_IDS:
    case AX_ATTR_FLOWTO_IDS:
    case AX_ATTR_INDIRECT_CHILD_IDS:
    case AX_ATTR_LABELLEDBY_IDS:
    case AX_ATTR_RADIO_GROUP_IDS:
    case AX_ATTR_UNIQUE_CELL_IDS:
      return true;

    // Note: all of the attributes are included here explicitly,
    // rather than using "default:", so that it's a compiler error to
    // add a new attribute without explicitly considering whether it's
    // a node id attribute or not.
    case AX_INT_LIST_ATTRIBUTE_NONE:
    case AX_ATTR_LINE_BREAKS:
    case AX_ATTR_MARKER_TYPES:
    case AX_ATTR_MARKER_STARTS:
    case AX_ATTR_MARKER_ENDS:
    case AX_ATTR_CHARACTER_OFFSETS:
    case AX_ATTR_CACHED_LINE_STARTS:
    case AX_ATTR_WORD_STARTS:
    case AX_ATTR_WORD_ENDS:
      return false;
  }

  NOTREACHED();
  return false;
}

AXNodeData::AXNodeData()
    : id(-1),
      role(AX_ROLE_UNKNOWN),
      state(AX_STATE_NONE),
      actions(AX_ACTION_NONE),
      offset_container_id(-1) {}

AXNodeData::~AXNodeData() {
}

AXNodeData::AXNodeData(const AXNodeData& other) {
  id = other.id;
  role = other.role;
  state = other.state;
  actions = other.actions;
  string_attributes = other.string_attributes;
  int_attributes = other.int_attributes;
  float_attributes = other.float_attributes;
  bool_attributes = other.bool_attributes;
  intlist_attributes = other.intlist_attributes;
  html_attributes = other.html_attributes;
  child_ids = other.child_ids;
  location = other.location;
  offset_container_id = other.offset_container_id;
  if (other.transform)
    transform.reset(new gfx::Transform(*other.transform));
}

AXNodeData& AXNodeData::operator=(AXNodeData other) {
  id = other.id;
  role = other.role;
  state = other.state;
  actions = other.actions;
  string_attributes = other.string_attributes;
  int_attributes = other.int_attributes;
  float_attributes = other.float_attributes;
  bool_attributes = other.bool_attributes;
  intlist_attributes = other.intlist_attributes;
  html_attributes = other.html_attributes;
  child_ids = other.child_ids;
  location = other.location;
  offset_container_id = other.offset_container_id;
  if (other.transform)
    transform.reset(new gfx::Transform(*other.transform));
  else
    transform.reset(nullptr);
  return *this;
}

bool AXNodeData::HasBoolAttribute(AXBoolAttribute attribute) const {
  auto iter = FindInVectorOfPairs(attribute, bool_attributes);
  return iter != bool_attributes.end();
}

bool AXNodeData::GetBoolAttribute(AXBoolAttribute attribute) const {
  bool result;
  if (GetBoolAttribute(attribute, &result))
    return result;
  return false;
}

bool AXNodeData::GetBoolAttribute(
    AXBoolAttribute attribute, bool* value) const {
  auto iter = FindInVectorOfPairs(attribute, bool_attributes);
  if (iter != bool_attributes.end()) {
    *value = iter->second;
    return true;
  }

  return false;
}

bool AXNodeData::HasFloatAttribute(AXFloatAttribute attribute) const {
  auto iter = FindInVectorOfPairs(attribute, float_attributes);
  return iter != float_attributes.end();
}

float AXNodeData::GetFloatAttribute(AXFloatAttribute attribute) const {
  float result;
  if (GetFloatAttribute(attribute, &result))
    return result;
  return 0.0;
}

bool AXNodeData::GetFloatAttribute(
    AXFloatAttribute attribute, float* value) const {
  auto iter = FindInVectorOfPairs(attribute, float_attributes);
  if (iter != float_attributes.end()) {
    *value = iter->second;
    return true;
  }

  return false;
}

bool AXNodeData::HasIntAttribute(AXIntAttribute attribute) const {
  auto iter = FindInVectorOfPairs(attribute, int_attributes);
  return iter != int_attributes.end();
}

int AXNodeData::GetIntAttribute(AXIntAttribute attribute) const {
  int result;
  if (GetIntAttribute(attribute, &result))
    return result;
  return 0;
}

bool AXNodeData::GetIntAttribute(
    AXIntAttribute attribute, int* value) const {
  auto iter = FindInVectorOfPairs(attribute, int_attributes);
  if (iter != int_attributes.end()) {
    *value = iter->second;
    return true;
  }

  return false;
}

bool AXNodeData::HasStringAttribute(AXStringAttribute attribute) const {
  auto iter = FindInVectorOfPairs(attribute, string_attributes);
  return iter != string_attributes.end();
}

const std::string& AXNodeData::GetStringAttribute(
    AXStringAttribute attribute) const {
  CR_DEFINE_STATIC_LOCAL(std::string, empty_string, ());
  auto iter = FindInVectorOfPairs(attribute, string_attributes);
  return iter != string_attributes.end() ? iter->second : empty_string;
}

bool AXNodeData::GetStringAttribute(
    AXStringAttribute attribute, std::string* value) const {
  auto iter = FindInVectorOfPairs(attribute, string_attributes);
  if (iter != string_attributes.end()) {
    *value = iter->second;
    return true;
  }

  return false;
}

base::string16 AXNodeData::GetString16Attribute(
    AXStringAttribute attribute) const {
  std::string value_utf8;
  if (!GetStringAttribute(attribute, &value_utf8))
    return base::string16();
  return base::UTF8ToUTF16(value_utf8);
}

bool AXNodeData::GetString16Attribute(
    AXStringAttribute attribute,
    base::string16* value) const {
  std::string value_utf8;
  if (!GetStringAttribute(attribute, &value_utf8))
    return false;
  *value = base::UTF8ToUTF16(value_utf8);
  return true;
}

bool AXNodeData::HasIntListAttribute(AXIntListAttribute attribute) const {
  auto iter = FindInVectorOfPairs(attribute, intlist_attributes);
  return iter != intlist_attributes.end();
}

const std::vector<int32_t>& AXNodeData::GetIntListAttribute(
    AXIntListAttribute attribute) const {
  CR_DEFINE_STATIC_LOCAL(std::vector<int32_t>, empty_vector, ());
  auto iter = FindInVectorOfPairs(attribute, intlist_attributes);
  if (iter != intlist_attributes.end())
    return iter->second;
  return empty_vector;
}

bool AXNodeData::GetIntListAttribute(AXIntListAttribute attribute,
                                     std::vector<int32_t>* value) const {
  auto iter = FindInVectorOfPairs(attribute, intlist_attributes);
  if (iter != intlist_attributes.end()) {
    *value = iter->second;
    return true;
  }

  return false;
}

bool AXNodeData::GetHtmlAttribute(
    const char* html_attr, std::string* value) const {
  for (size_t i = 0; i < html_attributes.size(); ++i) {
    const std::string& attr = html_attributes[i].first;
    if (base::LowerCaseEqualsASCII(attr, html_attr)) {
      *value = html_attributes[i].second;
      return true;
    }
  }

  return false;
}

bool AXNodeData::GetHtmlAttribute(
    const char* html_attr, base::string16* value) const {
  std::string value_utf8;
  if (!GetHtmlAttribute(html_attr, &value_utf8))
    return false;
  *value = base::UTF8ToUTF16(value_utf8);
  return true;
}

void AXNodeData::AddStringAttribute(
    AXStringAttribute attribute, const std::string& value) {
  string_attributes.push_back(std::make_pair(attribute, value));
}

void AXNodeData::AddIntAttribute(
    AXIntAttribute attribute, int value) {
  int_attributes.push_back(std::make_pair(attribute, value));
}

void AXNodeData::AddFloatAttribute(
    AXFloatAttribute attribute, float value) {
  float_attributes.push_back(std::make_pair(attribute, value));
}

void AXNodeData::AddBoolAttribute(
    AXBoolAttribute attribute, bool value) {
  bool_attributes.push_back(std::make_pair(attribute, value));
}

void AXNodeData::AddIntListAttribute(AXIntListAttribute attribute,
                                     const std::vector<int32_t>& value) {
  intlist_attributes.push_back(std::make_pair(attribute, value));
}

void AXNodeData::SetName(const std::string& name) {
  for (size_t i = 0; i < string_attributes.size(); ++i) {
    if (string_attributes[i].first == AX_ATTR_NAME) {
      string_attributes[i].second = name;
      return;
    }
  }

  string_attributes.push_back(std::make_pair(AX_ATTR_NAME, name));
}

void AXNodeData::SetName(const base::string16& name) {
  SetName(base::UTF16ToUTF8(name));
}

void AXNodeData::SetValue(const std::string& value) {
  for (size_t i = 0; i < string_attributes.size(); ++i) {
    if (string_attributes[i].first == AX_ATTR_VALUE) {
      string_attributes[i].second = value;
      return;
    }
  }

  string_attributes.push_back(std::make_pair(AX_ATTR_VALUE, value));
}

void AXNodeData::SetValue(const base::string16& value) {
  SetValue(base::UTF16ToUTF8(value));
}

bool AXNodeData::HasState(AXState state_enum) const {
  return IsFlagSet(state, state_enum);
}

bool AXNodeData::HasAction(AXAction action_enum) const {
  return IsFlagSet(actions, action_enum);
}

void AXNodeData::AddState(AXState state_enum) {
  DCHECK_NE(state_enum, AX_STATE_NONE);
  state = ModifyFlag(state, state_enum, true);
}

void AXNodeData::AddAction(AXAction action_enum) {
  switch (action_enum) {
    case AX_ACTION_NONE:
      NOTREACHED();
      break;

    // Note: all of the attributes are included here explicitly, rather than
    // using "default:", so that it's a compiler error to add a new action
    // without explicitly considering whether there are mutually exclusive
    // actions that can be performed on a UI control at the same time.
    case AX_ACTION_BLUR:
    case AX_ACTION_FOCUS: {
      AXAction excluded_action =
          (action_enum == AX_ACTION_BLUR) ? AX_ACTION_FOCUS : AX_ACTION_BLUR;
      DCHECK(HasAction(excluded_action));
    } break;
    case AX_ACTION_DECREMENT:
    case AX_ACTION_DO_DEFAULT:
    case AX_ACTION_GET_IMAGE_DATA:
    case AX_ACTION_HIT_TEST:
    case AX_ACTION_INCREMENT:
    case AX_ACTION_REPLACE_SELECTED_TEXT:
    case AX_ACTION_SCROLL_TO_MAKE_VISIBLE:
    case AX_ACTION_SCROLL_TO_POINT:
    case AX_ACTION_SET_ACCESSIBILITY_FOCUS:
    case AX_ACTION_SET_SCROLL_OFFSET:
    case AX_ACTION_SET_SELECTION:
    case AX_ACTION_SET_SEQUENTIAL_FOCUS_NAVIGATION_STARTING_POINT:
    case AX_ACTION_SET_VALUE:
    case AX_ACTION_SHOW_CONTEXT_MENU:
      break;
  }

  actions = ModifyFlag(actions, action_enum, true);
}

std::string AXNodeData::ToString() const {
  std::string result;

  result += "id=" + IntToString(id);
  result += " " + ui::ToString(role);

  result += StateBitfieldToString(state);

  result += " (" + IntToString(location.x()) + ", " +
                   IntToString(location.y()) + ")-(" +
                   IntToString(location.width()) + ", " +
                   IntToString(location.height()) + ")";

  if (offset_container_id != -1)
    result += " offset_container_id=" + IntToString(offset_container_id);

  if (transform && !transform->IsIdentity())
    result += " transform=" + transform->ToString();

  for (size_t i = 0; i < int_attributes.size(); ++i) {
    std::string value = IntToString(int_attributes[i].second);
    switch (int_attributes[i].first) {
      case AX_ATTR_DEFAULT_ACTION_VERB:
        result +=
            " action=" +
            base::UTF16ToUTF8(ActionVerbToUnlocalizedString(
                static_cast<AXDefaultActionVerb>(int_attributes[i].second)));
        break;
      case AX_ATTR_SCROLL_X:
        result += " scroll_x=" + value;
        break;
      case AX_ATTR_SCROLL_X_MIN:
        result += " scroll_x_min=" + value;
        break;
      case AX_ATTR_SCROLL_X_MAX:
        result += " scroll_x_max=" + value;
        break;
      case AX_ATTR_SCROLL_Y:
        result += " scroll_y=" + value;
        break;
      case AX_ATTR_SCROLL_Y_MIN:
        result += " scroll_y_min=" + value;
        break;
      case AX_ATTR_SCROLL_Y_MAX:
        result += " scroll_y_max=" + value;
        break;
      case AX_ATTR_HIERARCHICAL_LEVEL:
        result += " level=" + value;
        break;
      case AX_ATTR_TEXT_SEL_START:
        result += " sel_start=" + value;
        break;
      case AX_ATTR_TEXT_SEL_END:
        result += " sel_end=" + value;
        break;
      case AX_ATTR_ARIA_COLUMN_COUNT:
        result += " aria_column_count=" + value;
        break;
      case AX_ATTR_ARIA_CELL_COLUMN_INDEX:
        result += " aria_cell_column_index=" + value;
        break;
      case AX_ATTR_ARIA_ROW_COUNT:
        result += " aria_row_count=" + value;
        break;
      case AX_ATTR_ARIA_CELL_ROW_INDEX:
        result += " aria_cell_row_index=" + value;
        break;
      case AX_ATTR_TABLE_ROW_COUNT:
        result += " rows=" + value;
        break;
      case AX_ATTR_TABLE_COLUMN_COUNT:
        result += " cols=" + value;
        break;
      case AX_ATTR_TABLE_CELL_COLUMN_INDEX:
        result += " col=" + value;
        break;
      case AX_ATTR_TABLE_CELL_ROW_INDEX:
        result += " row=" + value;
        break;
      case AX_ATTR_TABLE_CELL_COLUMN_SPAN:
        result += " colspan=" + value;
        break;
      case AX_ATTR_TABLE_CELL_ROW_SPAN:
        result += " rowspan=" + value;
        break;
      case AX_ATTR_TABLE_COLUMN_HEADER_ID:
        result += " column_header_id=" + value;
        break;
      case AX_ATTR_TABLE_COLUMN_INDEX:
        result += " column_index=" + value;
        break;
      case AX_ATTR_TABLE_HEADER_ID:
        result += " header_id=" + value;
        break;
      case AX_ATTR_TABLE_ROW_HEADER_ID:
        result += " row_header_id=" + value;
        break;
      case AX_ATTR_TABLE_ROW_INDEX:
        result += " row_index=" + value;
        break;
      case AX_ATTR_SORT_DIRECTION:
        switch (int_attributes[i].second) {
          case AX_SORT_DIRECTION_UNSORTED:
            result += " sort_direction=none";
            break;
          case AX_SORT_DIRECTION_ASCENDING:
            result += " sort_direction=ascending";
            break;
          case AX_SORT_DIRECTION_DESCENDING:
            result += " sort_direction=descending";
            break;
          case AX_SORT_DIRECTION_OTHER:
            result += " sort_direction=other";
            break;
        }
        break;
      case AX_ATTR_NAME_FROM:
        result +=
            " name_from=" +
            ui::ToString(static_cast<AXNameFrom>(int_attributes[i].second));
        break;
      case AX_ATTR_DESCRIPTION_FROM:
        result += " description_from=" +
                  ui::ToString(
                      static_cast<AXDescriptionFrom>(int_attributes[i].second));
        break;
      case AX_ATTR_ACTIVEDESCENDANT_ID:
        result += " activedescendant=" + value;
        break;
      case AX_ATTR_ERRORMESSAGE_ID:
        result += " errormessage=" + value;
        break;
      case AX_ATTR_IN_PAGE_LINK_TARGET_ID:
        result += " in_page_link_target_id=" + value;
        break;
      case AX_ATTR_MEMBER_OF_ID:
        result += " member_of_id=" + value;
        break;
      case AX_ATTR_NEXT_ON_LINE_ID:
        result += " next_on_line_id=" + value;
        break;
      case AX_ATTR_PREVIOUS_ON_LINE_ID:
        result += " previous_on_line_id=" + value;
        break;
      case AX_ATTR_CHILD_TREE_ID:
        result += " child_tree_id=" + value;
        break;
      case AX_ATTR_COLOR_VALUE:
        result += base::StringPrintf(" color_value=&%X",
                                     int_attributes[i].second);
        break;
      case AX_ATTR_ARIA_CURRENT_STATE:
        switch (int_attributes[i].second) {
          case AX_ARIA_CURRENT_STATE_FALSE:
            result += " aria_current_state=false";
            break;
          case AX_ARIA_CURRENT_STATE_TRUE:
            result += " aria_current_state=true";
            break;
          case AX_ARIA_CURRENT_STATE_PAGE:
            result += " aria_current_state=page";
            break;
          case AX_ARIA_CURRENT_STATE_STEP:
            result += " aria_current_state=step";
            break;
          case AX_ARIA_CURRENT_STATE_LOCATION:
            result += " aria_current_state=location";
            break;
          case AX_ARIA_CURRENT_STATE_DATE:
            result += " aria_current_state=date";
            break;
          case AX_ARIA_CURRENT_STATE_TIME:
            result += " aria_current_state=time";
            break;
        }
        break;
      case AX_ATTR_BACKGROUND_COLOR:
        result += base::StringPrintf(" background_color=&%X",
                                     int_attributes[i].second);
        break;
      case AX_ATTR_COLOR:
        result += base::StringPrintf(" color=&%X", int_attributes[i].second);
        break;
      case AX_ATTR_TEXT_DIRECTION:
        switch (int_attributes[i].second) {
          case AX_TEXT_DIRECTION_LTR:
            result += " text_direction=ltr";
            break;
          case AX_TEXT_DIRECTION_RTL:
            result += " text_direction=rtl";
            break;
          case AX_TEXT_DIRECTION_TTB:
            result += " text_direction=ttb";
            break;
          case AX_TEXT_DIRECTION_BTT:
            result += " text_direction=btt";
            break;
        }
        break;
      case AX_ATTR_TEXT_STYLE: {
        auto text_style = static_cast<AXTextStyle>(int_attributes[i].second);
        if (text_style == AX_TEXT_STYLE_NONE)
          break;
        std::string text_style_value(" text_style=");
        if (text_style & AX_TEXT_STYLE_BOLD)
          text_style_value += "bold,";
        if (text_style & AX_TEXT_STYLE_ITALIC)
          text_style_value += "italic,";
        if (text_style & AX_TEXT_STYLE_UNDERLINE)
          text_style_value += "underline,";
        if (text_style & AX_TEXT_STYLE_LINE_THROUGH)
          text_style_value += "line-through,";
        result += text_style_value.substr(0, text_style_value.size() - 1);
        break;
      }
      case AX_ATTR_SET_SIZE:
        result += " setsize=" + value;
        break;
      case AX_ATTR_POS_IN_SET:
        result += " posinset=" + value;
        break;
      case AX_ATTR_INVALID_STATE:
        switch (int_attributes[i].second) {
          case AX_INVALID_STATE_FALSE:
            result += " invalid_state=false";
            break;
          case AX_INVALID_STATE_TRUE:
            result += " invalid_state=true";
            break;
          case AX_INVALID_STATE_SPELLING:
            result += " invalid_state=spelling";
            break;
          case AX_INVALID_STATE_GRAMMAR:
            result += " invalid_state=grammar";
            break;
          case AX_INVALID_STATE_OTHER:
            result += " invalid_state=other";
            break;
        }
        break;
      case AX_ATTR_CHECKED_STATE:
        switch (int_attributes[i].second) {
          case AX_CHECKED_STATE_FALSE:
            result += " checked_state=false";
            break;
          case AX_CHECKED_STATE_TRUE:
            result += " checked_state=true";
            break;
          case AX_CHECKED_STATE_MIXED:
            result += " checked_state=mixed";
            break;
        }
        break;
      case AX_INT_ATTRIBUTE_NONE:
        break;
    }
  }

  for (size_t i = 0; i < string_attributes.size(); ++i) {
    std::string value = string_attributes[i].second;
    switch (string_attributes[i].first) {
      case AX_ATTR_ACCESS_KEY:
        result += " access_key=" + value;
        break;
      case AX_ATTR_ARIA_INVALID_VALUE:
        result += " aria_invalid_value=" + value;
        break;
      case AX_ATTR_AUTO_COMPLETE:
        result += " autocomplete=" + value;
        break;
      case AX_ATTR_CHROME_CHANNEL:
        result += " chrome_channel=" + value;
        break;
      case AX_ATTR_DESCRIPTION:
        result += " description=" + value;
        break;
      case AX_ATTR_DISPLAY:
        result += " display=" + value;
        break;
      case AX_ATTR_FONT_FAMILY:
        result += " font-family=" + value;
        break;
      case AX_ATTR_HTML_TAG:
        result += " html_tag=" + value;
        break;
      case AX_ATTR_IMAGE_DATA_URL:
        result += " image_data_url=(" +
            IntToString(static_cast<int>(value.size())) + " bytes)";
        break;
      case AX_ATTR_INNER_HTML:
        result += " inner_html=" + value;
        break;
      case AX_ATTR_KEY_SHORTCUTS:
        result += " key_shortcuts=" + value;
        break;
      case AX_ATTR_LANGUAGE:
        result += " language=" + value;
        break;
      case AX_ATTR_LIVE_RELEVANT:
        result += " relevant=" + value;
        break;
      case AX_ATTR_LIVE_STATUS:
        result += " live=" + value;
        break;
      case AX_ATTR_CONTAINER_LIVE_RELEVANT:
        result += " container_relevant=" + value;
        break;
      case AX_ATTR_CONTAINER_LIVE_STATUS:
        result += " container_live=" + value;
        break;
      case AX_ATTR_PLACEHOLDER:
        result += " placeholder=" + value;
        break;
      case AX_ATTR_ROLE:
        result += " role=" + value;
        break;
      case AX_ATTR_ROLE_DESCRIPTION:
        result += " role_description=" + value;
        break;
      case AX_ATTR_SHORTCUT:
        result += " shortcut=" + value;
        break;
      case AX_ATTR_URL:
        result += " url=" + value;
        break;
      case AX_ATTR_NAME:
        result += " name=" + value;
        break;
      case AX_ATTR_VALUE:
        result += " value=" + value;
        break;
      case AX_STRING_ATTRIBUTE_NONE:
        break;
    }
  }

  for (size_t i = 0; i < float_attributes.size(); ++i) {
    std::string value = DoubleToString(float_attributes[i].second);
    switch (float_attributes[i].first) {
      case AX_ATTR_VALUE_FOR_RANGE:
        result += " value_for_range=" + value;
        break;
      case AX_ATTR_MAX_VALUE_FOR_RANGE:
        result += " max_value=" + value;
        break;
      case AX_ATTR_MIN_VALUE_FOR_RANGE:
        result += " min_value=" + value;
        break;
      case AX_ATTR_FONT_SIZE:
        result += " font_size=" + value;
        break;
      case AX_FLOAT_ATTRIBUTE_NONE:
        break;
    }
  }

  for (size_t i = 0; i < bool_attributes.size(); ++i) {
    std::string value = bool_attributes[i].second ? "true" : "false";
    switch (bool_attributes[i].first) {
      case AX_ATTR_LIVE_ATOMIC:
        result += " atomic=" + value;
        break;
      case AX_ATTR_LIVE_BUSY:
        result += " busy=" + value;
        break;
      case AX_ATTR_CONTAINER_LIVE_ATOMIC:
        result += " container_atomic=" + value;
        break;
      case AX_ATTR_CONTAINER_LIVE_BUSY:
        result += " container_busy=" + value;
        break;
      case AX_ATTR_ARIA_READONLY:
        result += " aria_readonly=" + value;
        break;
      case AX_ATTR_UPDATE_LOCATION_ONLY:
        result += " update_location_only=" + value;
        break;
      case AX_ATTR_CANVAS_HAS_FALLBACK:
        result += " has_fallback=" + value;
        break;
      case AX_ATTR_MODAL:
        result += " modal=" + value;
        break;
      case AX_BOOL_ATTRIBUTE_NONE:
        break;
    }
  }

  for (size_t i = 0; i < intlist_attributes.size(); ++i) {
    const std::vector<int32_t>& values = intlist_attributes[i].second;
    switch (intlist_attributes[i].first) {
      case AX_ATTR_INDIRECT_CHILD_IDS:
        result += " indirect_child_ids=" + IntVectorToString(values);
        break;
      case AX_ATTR_CONTROLS_IDS:
        result += " controls_ids=" + IntVectorToString(values);
        break;
      case AX_ATTR_DESCRIBEDBY_IDS:
        result += " describedby_ids=" + IntVectorToString(values);
        break;
      case AX_ATTR_DETAILS_IDS:
        result += " details_ids=" + IntVectorToString(values);
        break;
      case AX_ATTR_FLOWTO_IDS:
        result += " flowto_ids=" + IntVectorToString(values);
        break;
      case AX_ATTR_LABELLEDBY_IDS:
        result += " labelledby_ids=" + IntVectorToString(values);
        break;
      case AX_ATTR_RADIO_GROUP_IDS:
        result += " radio_group_ids=" + IntVectorToString(values);
        break;
      case AX_ATTR_LINE_BREAKS:
        result += " line_breaks=" + IntVectorToString(values);
        break;
      case AX_ATTR_MARKER_TYPES: {
        std::string types_str;
        for (size_t i = 0; i < values.size(); ++i) {
          auto type = static_cast<AXMarkerType>(values[i]);
          if (type == AX_MARKER_TYPE_NONE)
            continue;

          if (i > 0)
            types_str += ',';

          if (type & AX_MARKER_TYPE_SPELLING)
            types_str += "spelling&";
          if (type & AX_MARKER_TYPE_GRAMMAR)
            types_str += "grammar&";
          if (type & AX_MARKER_TYPE_TEXT_MATCH)
            types_str += "text_match&";

          if (!types_str.empty())
            types_str = types_str.substr(0, types_str.size() - 1);
        }

        if (!types_str.empty())
          result += " marker_types=" + types_str;
        break;
      }
      case AX_ATTR_MARKER_STARTS:
        result += " marker_starts=" + IntVectorToString(values);
        break;
      case AX_ATTR_MARKER_ENDS:
        result += " marker_ends=" + IntVectorToString(values);
        break;
      case AX_ATTR_CELL_IDS:
        result += " cell_ids=" + IntVectorToString(values);
        break;
      case AX_ATTR_UNIQUE_CELL_IDS:
        result += " unique_cell_ids=" + IntVectorToString(values);
        break;
      case AX_ATTR_CHARACTER_OFFSETS:
        result += " character_offsets=" + IntVectorToString(values);
        break;
      case AX_ATTR_CACHED_LINE_STARTS:
        result += " cached_line_start_offsets=" + IntVectorToString(values);
        break;
      case AX_ATTR_WORD_STARTS:
        result += " word_starts=" + IntVectorToString(values);
        break;
      case AX_ATTR_WORD_ENDS:
        result += " word_ends=" + IntVectorToString(values);
        break;
      case AX_INT_LIST_ATTRIBUTE_NONE:
        break;
    }
  }

  result += " actions=" + ActionsBitfieldToString(actions);

  if (!child_ids.empty())
    result += " child_ids=" + IntVectorToString(child_ids);

  return result;
}

}  // namespace ui
