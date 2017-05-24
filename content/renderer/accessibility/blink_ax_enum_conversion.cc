// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/accessibility/blink_ax_enum_conversion.h"

#include "base/logging.h"

namespace content {

uint32_t AXStateFromBlink(const blink::WebAXObject& o) {
  uint32_t state = 0;

  blink::WebAXExpanded expanded = o.IsExpanded();
  if (expanded) {
    if (expanded == blink::kWebAXExpandedCollapsed)
      state |= (1 << ui::AX_STATE_COLLAPSED);
    else if (expanded == blink::kWebAXExpandedExpanded)
      state |= (1 << ui::AX_STATE_EXPANDED);
  }

  if (o.CanSetFocusAttribute())
    state |= (1 << ui::AX_STATE_FOCUSABLE);

  if (o.Role() == blink::kWebAXRolePopUpButton || o.AriaHasPopup())
    state |= (1 << ui::AX_STATE_HASPOPUP);

  if (o.IsHovered())
    state |= (1 << ui::AX_STATE_HOVERED);

  if (!o.IsVisible())
    state |= (1 << ui::AX_STATE_INVISIBLE);

  if (o.IsLinked())
    state |= (1 << ui::AX_STATE_LINKED);

  if (o.IsMultiline())
    state |= (1 << ui::AX_STATE_MULTILINE);

  if (o.IsMultiSelectable())
    state |= (1 << ui::AX_STATE_MULTISELECTABLE);

  if (o.IsOffScreen())
    state |= (1 << ui::AX_STATE_OFFSCREEN);

  if (o.IsPressed())
    state |= (1 << ui::AX_STATE_PRESSED);

  if (o.IsPasswordField())
    state |= (1 << ui::AX_STATE_PROTECTED);

  if (o.IsReadOnly())
    state |= (1 << ui::AX_STATE_READ_ONLY);

  if (o.IsRequired())
    state |= (1 << ui::AX_STATE_REQUIRED);

  if (o.CanSetSelectedAttribute())
    state |= (1 << ui::AX_STATE_SELECTABLE);

  if (o.IsEditable())
    state |= (1 << ui::AX_STATE_EDITABLE);

  if (!o.IsEnabled())
    state |= (1 << ui::AX_STATE_DISABLED);

  if (o.IsSelected())
    state |= (1 << ui::AX_STATE_SELECTED);

  if (o.IsRichlyEditable())
    state |= (1 << ui::AX_STATE_RICHLY_EDITABLE);

  if (o.IsVisited())
    state |= (1 << ui::AX_STATE_VISITED);

  if (o.Orientation() == blink::kWebAXOrientationVertical)
    state |= (1 << ui::AX_STATE_VERTICAL);
  else if (o.Orientation() == blink::kWebAXOrientationHorizontal)
    state |= (1 << ui::AX_STATE_HORIZONTAL);

  if (o.IsVisited())
    state |= (1 << ui::AX_STATE_VISITED);

  return state;
}

ui::AXRole AXRoleFromBlink(blink::WebAXRole role) {
  switch (role) {
    case blink::kWebAXRoleAbbr:
      return ui::AX_ROLE_ABBR;
    case blink::kWebAXRoleAlert:
      return ui::AX_ROLE_ALERT;
    case blink::kWebAXRoleAlertDialog:
      return ui::AX_ROLE_ALERT_DIALOG;
    case blink::kWebAXRoleAnchor:
      return ui::AX_ROLE_ANCHOR;
    case blink::kWebAXRoleAnnotation:
      return ui::AX_ROLE_ANNOTATION;
    case blink::kWebAXRoleApplication:
      return ui::AX_ROLE_APPLICATION;
    case blink::kWebAXRoleArticle:
      return ui::AX_ROLE_ARTICLE;
    case blink::kWebAXRoleAudio:
      return ui::AX_ROLE_AUDIO;
    case blink::kWebAXRoleBanner:
      return ui::AX_ROLE_BANNER;
    case blink::kWebAXRoleBlockquote:
      return ui::AX_ROLE_BLOCKQUOTE;
    case blink::kWebAXRoleBusyIndicator:
      return ui::AX_ROLE_BUSY_INDICATOR;
    case blink::kWebAXRoleButton:
      return ui::AX_ROLE_BUTTON;
    case blink::kWebAXRoleCanvas:
      return ui::AX_ROLE_CANVAS;
    case blink::kWebAXRoleCaption:
      return ui::AX_ROLE_CAPTION;
    case blink::kWebAXRoleCell:
      return ui::AX_ROLE_CELL;
    case blink::kWebAXRoleCheckBox:
      return ui::AX_ROLE_CHECK_BOX;
    case blink::kWebAXRoleColorWell:
      return ui::AX_ROLE_COLOR_WELL;
    case blink::kWebAXRoleColumn:
      return ui::AX_ROLE_COLUMN;
    case blink::kWebAXRoleColumnHeader:
      return ui::AX_ROLE_COLUMN_HEADER;
    case blink::kWebAXRoleComboBox:
      return ui::AX_ROLE_COMBO_BOX;
    case blink::kWebAXRoleComplementary:
      return ui::AX_ROLE_COMPLEMENTARY;
    case blink::kWebAXRoleContentInfo:
      return ui::AX_ROLE_CONTENT_INFO;
    case blink::kWebAXRoleDate:
      return ui::AX_ROLE_DATE;
    case blink::kWebAXRoleDateTime:
      return ui::AX_ROLE_DATE_TIME;
    case blink::kWebAXRoleDefinition:
      return ui::AX_ROLE_DEFINITION;
    case blink::kWebAXRoleDescriptionListDetail:
      return ui::AX_ROLE_DESCRIPTION_LIST_DETAIL;
    case blink::kWebAXRoleDescriptionList:
      return ui::AX_ROLE_DESCRIPTION_LIST;
    case blink::kWebAXRoleDescriptionListTerm:
      return ui::AX_ROLE_DESCRIPTION_LIST_TERM;
    case blink::kWebAXRoleDetails:
      return ui::AX_ROLE_DETAILS;
    case blink::kWebAXRoleDialog:
      return ui::AX_ROLE_DIALOG;
    case blink::kWebAXRoleDirectory:
      return ui::AX_ROLE_DIRECTORY;
    case blink::kWebAXRoleDisclosureTriangle:
      return ui::AX_ROLE_DISCLOSURE_TRIANGLE;
    case blink::kWebAXRoleDocument:
      return ui::AX_ROLE_DOCUMENT;
    case blink::kWebAXRoleEmbeddedObject:
      return ui::AX_ROLE_EMBEDDED_OBJECT;
    case blink::kWebAXRoleFeed:
      return ui::AX_ROLE_FEED;
    case blink::kWebAXRoleFigcaption:
      return ui::AX_ROLE_FIGCAPTION;
    case blink::kWebAXRoleFigure:
      return ui::AX_ROLE_FIGURE;
    case blink::kWebAXRoleFooter:
      return ui::AX_ROLE_FOOTER;
    case blink::kWebAXRoleForm:
      return ui::AX_ROLE_FORM;
    case blink::kWebAXRoleGenericContainer:
      return ui::AX_ROLE_GENERIC_CONTAINER;
    case blink::kWebAXRoleGrid:
      return ui::AX_ROLE_GRID;
    case blink::kWebAXRoleGroup:
      return ui::AX_ROLE_GROUP;
    case blink::kWebAXRoleHeading:
      return ui::AX_ROLE_HEADING;
    case blink::kWebAXRoleIframe:
      return ui::AX_ROLE_IFRAME;
    case blink::kWebAXRoleIframePresentational:
      return ui::AX_ROLE_IFRAME_PRESENTATIONAL;
    case blink::kWebAXRoleIgnored:
      return ui::AX_ROLE_IGNORED;
    case blink::kWebAXRoleImage:
      return ui::AX_ROLE_IMAGE;
    case blink::kWebAXRoleImageMap:
      return ui::AX_ROLE_IMAGE_MAP;
    case blink::kWebAXRoleImageMapLink:
      return ui::AX_ROLE_IMAGE_MAP_LINK;
    case blink::kWebAXRoleInlineTextBox:
      return ui::AX_ROLE_INLINE_TEXT_BOX;
    case blink::kWebAXRoleInputTime:
      return ui::AX_ROLE_INPUT_TIME;
    case blink::kWebAXRoleLabel:
      return ui::AX_ROLE_LABEL_TEXT;
    case blink::kWebAXRoleLegend:
      return ui::AX_ROLE_LEGEND;
    case blink::kWebAXRoleLink:
      return ui::AX_ROLE_LINK;
    case blink::kWebAXRoleList:
      return ui::AX_ROLE_LIST;
    case blink::kWebAXRoleListBox:
      return ui::AX_ROLE_LIST_BOX;
    case blink::kWebAXRoleListBoxOption:
      return ui::AX_ROLE_LIST_BOX_OPTION;
    case blink::kWebAXRoleListItem:
      return ui::AX_ROLE_LIST_ITEM;
    case blink::kWebAXRoleListMarker:
      return ui::AX_ROLE_LIST_MARKER;
    case blink::kWebAXRoleLog:
      return ui::AX_ROLE_LOG;
    case blink::kWebAXRoleMain:
      return ui::AX_ROLE_MAIN;
    case blink::kWebAXRoleMarquee:
      return ui::AX_ROLE_MARQUEE;
    case blink::kWebAXRoleMark:
      return ui::AX_ROLE_MARK;
    case blink::kWebAXRoleMath:
      return ui::AX_ROLE_MATH;
    case blink::kWebAXRoleMenu:
      return ui::AX_ROLE_MENU;
    case blink::kWebAXRoleMenuBar:
      return ui::AX_ROLE_MENU_BAR;
    case blink::kWebAXRoleMenuButton:
      return ui::AX_ROLE_MENU_BUTTON;
    case blink::kWebAXRoleMenuItem:
      return ui::AX_ROLE_MENU_ITEM;
    case blink::kWebAXRoleMenuItemCheckBox:
      return ui::AX_ROLE_MENU_ITEM_CHECK_BOX;
    case blink::kWebAXRoleMenuItemRadio:
      return ui::AX_ROLE_MENU_ITEM_RADIO;
    case blink::kWebAXRoleMenuListOption:
      return ui::AX_ROLE_MENU_LIST_OPTION;
    case blink::kWebAXRoleMenuListPopup:
      return ui::AX_ROLE_MENU_LIST_POPUP;
    case blink::kWebAXRoleMeter:
      return ui::AX_ROLE_METER;
    case blink::kWebAXRoleNavigation:
      return ui::AX_ROLE_NAVIGATION;
    case blink::kWebAXRoleNone:
      return ui::AX_ROLE_NONE;
    case blink::kWebAXRoleNote:
      return ui::AX_ROLE_NOTE;
    case blink::kWebAXRoleOutline:
      return ui::AX_ROLE_OUTLINE;
    case blink::kWebAXRoleParagraph:
      return ui::AX_ROLE_PARAGRAPH;
    case blink::kWebAXRolePopUpButton:
      return ui::AX_ROLE_POP_UP_BUTTON;
    case blink::kWebAXRolePre:
      return ui::AX_ROLE_PRE;
    case blink::kWebAXRolePresentational:
      return ui::AX_ROLE_PRESENTATIONAL;
    case blink::kWebAXRoleProgressIndicator:
      return ui::AX_ROLE_PROGRESS_INDICATOR;
    case blink::kWebAXRoleRadioButton:
      return ui::AX_ROLE_RADIO_BUTTON;
    case blink::kWebAXRoleRadioGroup:
      return ui::AX_ROLE_RADIO_GROUP;
    case blink::kWebAXRoleRegion:
      return ui::AX_ROLE_REGION;
    case blink::kWebAXRoleRootWebArea:
      return ui::AX_ROLE_ROOT_WEB_AREA;
    case blink::kWebAXRoleRow:
      return ui::AX_ROLE_ROW;
    case blink::kWebAXRoleRuby:
      return ui::AX_ROLE_RUBY;
    case blink::kWebAXRoleRowHeader:
      return ui::AX_ROLE_ROW_HEADER;
    case blink::kWebAXRoleRuler:
      return ui::AX_ROLE_RULER;
    case blink::kWebAXRoleSVGRoot:
      return ui::AX_ROLE_SVG_ROOT;
    case blink::kWebAXRoleScrollArea:
      return ui::AX_ROLE_SCROLL_AREA;
    case blink::kWebAXRoleScrollBar:
      return ui::AX_ROLE_SCROLL_BAR;
    case blink::kWebAXRoleSeamlessWebArea:
      return ui::AX_ROLE_SEAMLESS_WEB_AREA;
    case blink::kWebAXRoleSearch:
      return ui::AX_ROLE_SEARCH;
    case blink::kWebAXRoleSearchBox:
      return ui::AX_ROLE_SEARCH_BOX;
    case blink::kWebAXRoleSlider:
      return ui::AX_ROLE_SLIDER;
    case blink::kWebAXRoleSliderThumb:
      return ui::AX_ROLE_SLIDER_THUMB;
    case blink::kWebAXRoleSpinButton:
      return ui::AX_ROLE_SPIN_BUTTON;
    case blink::kWebAXRoleSpinButtonPart:
      return ui::AX_ROLE_SPIN_BUTTON_PART;
    case blink::kWebAXRoleSplitter:
      return ui::AX_ROLE_SPLITTER;
    case blink::kWebAXRoleStaticText:
      return ui::AX_ROLE_STATIC_TEXT;
    case blink::kWebAXRoleStatus:
      return ui::AX_ROLE_STATUS;
    case blink::kWebAXRoleSwitch:
      return ui::AX_ROLE_SWITCH;
    case blink::kWebAXRoleTab:
      return ui::AX_ROLE_TAB;
    case blink::kWebAXRoleTabGroup:
      return ui::AX_ROLE_TAB_GROUP;
    case blink::kWebAXRoleTabList:
      return ui::AX_ROLE_TAB_LIST;
    case blink::kWebAXRoleTabPanel:
      return ui::AX_ROLE_TAB_PANEL;
    case blink::kWebAXRoleTable:
      return ui::AX_ROLE_TABLE;
    case blink::kWebAXRoleTableHeaderContainer:
      return ui::AX_ROLE_TABLE_HEADER_CONTAINER;
    case blink::kWebAXRoleTerm:
      return ui::AX_ROLE_TERM;
    case blink::kWebAXRoleTextField:
      return ui::AX_ROLE_TEXT_FIELD;
    case blink::kWebAXRoleTime:
      return ui::AX_ROLE_TIME;
    case blink::kWebAXRoleTimer:
      return ui::AX_ROLE_TIMER;
    case blink::kWebAXRoleToggleButton:
      return ui::AX_ROLE_TOGGLE_BUTTON;
    case blink::kWebAXRoleToolbar:
      return ui::AX_ROLE_TOOLBAR;
    case blink::kWebAXRoleTree:
      return ui::AX_ROLE_TREE;
    case blink::kWebAXRoleTreeGrid:
      return ui::AX_ROLE_TREE_GRID;
    case blink::kWebAXRoleTreeItem:
      return ui::AX_ROLE_TREE_ITEM;
    case blink::kWebAXRoleUnknown:
      return ui::AX_ROLE_UNKNOWN;
    case blink::kWebAXRoleUserInterfaceTooltip:
      return ui::AX_ROLE_TOOLTIP;
    case blink::kWebAXRoleVideo:
      return ui::AX_ROLE_VIDEO;
    case blink::kWebAXRoleWebArea:
      return ui::AX_ROLE_ROOT_WEB_AREA;
    case blink::kWebAXRoleLineBreak:
      return ui::AX_ROLE_LINE_BREAK;
    case blink::kWebAXRoleWindow:
      return ui::AX_ROLE_WINDOW;
    default:
      // We can't add an assertion here, that prevents us
      // from adding new role enums in Blink.
      LOG(WARNING) << "Warning: Blink WebAXRole " << role
                   << " not handled by Chromium yet.";
      return ui::AX_ROLE_UNKNOWN;
  }
}

ui::AXEvent AXEventFromBlink(blink::WebAXEvent event) {
  switch (event) {
    case blink::kWebAXEventActiveDescendantChanged:
      return ui::AX_EVENT_ACTIVEDESCENDANTCHANGED;
    case blink::kWebAXEventAlert:
      return ui::AX_EVENT_ALERT;
    case blink::kWebAXEventAriaAttributeChanged:
      return ui::AX_EVENT_ARIA_ATTRIBUTE_CHANGED;
    case blink::kWebAXEventAutocorrectionOccured:
      return ui::AX_EVENT_AUTOCORRECTION_OCCURED;
    case blink::kWebAXEventBlur:
      return ui::AX_EVENT_BLUR;
    case blink::kWebAXEventCheckedStateChanged:
      return ui::AX_EVENT_CHECKED_STATE_CHANGED;
    case blink::kWebAXEventChildrenChanged:
      return ui::AX_EVENT_CHILDREN_CHANGED;
    case blink::kWebAXEventClicked:
      return ui::AX_EVENT_CLICKED;
    case blink::kWebAXEventDocumentSelectionChanged:
      return ui::AX_EVENT_DOCUMENT_SELECTION_CHANGED;
    case blink::kWebAXEventExpandedChanged:
      return ui::AX_EVENT_EXPANDED_CHANGED;
    case blink::kWebAXEventFocus:
      return ui::AX_EVENT_FOCUS;
    case blink::kWebAXEventHover:
      return ui::AX_EVENT_HOVER;
    case blink::kWebAXEventInvalidStatusChanged:
      return ui::AX_EVENT_INVALID_STATUS_CHANGED;
    case blink::kWebAXEventLayoutComplete:
      return ui::AX_EVENT_LAYOUT_COMPLETE;
    case blink::kWebAXEventLiveRegionChanged:
      return ui::AX_EVENT_LIVE_REGION_CHANGED;
    case blink::kWebAXEventLoadComplete:
      return ui::AX_EVENT_LOAD_COMPLETE;
    case blink::kWebAXEventLocationChanged:
      return ui::AX_EVENT_LOCATION_CHANGED;
    case blink::kWebAXEventMenuListItemSelected:
      return ui::AX_EVENT_MENU_LIST_ITEM_SELECTED;
    case blink::kWebAXEventMenuListItemUnselected:
      return ui::AX_EVENT_MENU_LIST_ITEM_SELECTED;
    case blink::kWebAXEventMenuListValueChanged:
      return ui::AX_EVENT_MENU_LIST_VALUE_CHANGED;
    case blink::kWebAXEventRowCollapsed:
      return ui::AX_EVENT_ROW_COLLAPSED;
    case blink::kWebAXEventRowCountChanged:
      return ui::AX_EVENT_ROW_COUNT_CHANGED;
    case blink::kWebAXEventRowExpanded:
      return ui::AX_EVENT_ROW_EXPANDED;
    case blink::kWebAXEventScrollPositionChanged:
      return ui::AX_EVENT_SCROLL_POSITION_CHANGED;
    case blink::kWebAXEventScrolledToAnchor:
      return ui::AX_EVENT_SCROLLED_TO_ANCHOR;
    case blink::kWebAXEventSelectedChildrenChanged:
      return ui::AX_EVENT_SELECTED_CHILDREN_CHANGED;
    case blink::kWebAXEventSelectedTextChanged:
      return ui::AX_EVENT_TEXT_SELECTION_CHANGED;
    case blink::kWebAXEventTextChanged:
      return ui::AX_EVENT_TEXT_CHANGED;
    case blink::kWebAXEventValueChanged:
      return ui::AX_EVENT_VALUE_CHANGED;
    default:
      // We can't add an assertion here, that prevents us
      // from adding new event enums in Blink.
      return ui::AX_EVENT_NONE;
  }
}

ui::AXDefaultActionVerb AXDefaultActionVerbFromBlink(
    blink::WebAXDefaultActionVerb action_verb) {
  switch (action_verb) {
    case blink::WebAXDefaultActionVerb::kNone:
      return ui::AX_DEFAULT_ACTION_VERB_NONE;
    case blink::WebAXDefaultActionVerb::kActivate:
      return ui::AX_DEFAULT_ACTION_VERB_ACTIVATE;
    case blink::WebAXDefaultActionVerb::kCheck:
      return ui::AX_DEFAULT_ACTION_VERB_CHECK;
    case blink::WebAXDefaultActionVerb::kClick:
      return ui::AX_DEFAULT_ACTION_VERB_CLICK;
    case blink::WebAXDefaultActionVerb::kJump:
      return ui::AX_DEFAULT_ACTION_VERB_JUMP;
    case blink::WebAXDefaultActionVerb::kOpen:
      return ui::AX_DEFAULT_ACTION_VERB_OPEN;
    case blink::WebAXDefaultActionVerb::kPress:
      return ui::AX_DEFAULT_ACTION_VERB_PRESS;
    case blink::WebAXDefaultActionVerb::kSelect:
      return ui::AX_DEFAULT_ACTION_VERB_SELECT;
    case blink::WebAXDefaultActionVerb::kUncheck:
      return ui::AX_DEFAULT_ACTION_VERB_UNCHECK;
  }
  NOTREACHED();
  return ui::AX_DEFAULT_ACTION_VERB_NONE;
}

ui::AXMarkerType AXMarkerTypeFromBlink(blink::WebAXMarkerType marker_type) {
  switch (marker_type) {
    case blink::kWebAXMarkerTypeSpelling:
      return ui::AX_MARKER_TYPE_SPELLING;
    case blink::kWebAXMarkerTypeGrammar:
      return ui::AX_MARKER_TYPE_GRAMMAR;
    case blink::kWebAXMarkerTypeTextMatch:
      return ui::AX_MARKER_TYPE_TEXT_MATCH;
  }
  NOTREACHED();
  return ui::AX_MARKER_TYPE_NONE;
}

ui::AXTextDirection AXTextDirectionFromBlink(
    blink::WebAXTextDirection text_direction) {
  switch (text_direction) {
    case blink::kWebAXTextDirectionLR:
      return ui::AX_TEXT_DIRECTION_LTR;
    case blink::kWebAXTextDirectionRL:
      return ui::AX_TEXT_DIRECTION_RTL;
    case blink::kWebAXTextDirectionTB:
      return ui::AX_TEXT_DIRECTION_TTB;
    case blink::kWebAXTextDirectionBT:
      return ui::AX_TEXT_DIRECTION_BTT;
  }
  NOTREACHED();
  return ui::AX_TEXT_DIRECTION_NONE;
}

ui::AXTextStyle AXTextStyleFromBlink(blink::WebAXTextStyle text_style) {
  unsigned int browser_text_style = ui::AX_TEXT_STYLE_NONE;
  if (text_style & blink::kWebAXTextStyleBold)
    browser_text_style |= ui::AX_TEXT_STYLE_BOLD;
  if (text_style & blink::kWebAXTextStyleItalic)
    browser_text_style |= ui::AX_TEXT_STYLE_ITALIC;
  if (text_style & blink::kWebAXTextStyleUnderline)
    browser_text_style |= ui::AX_TEXT_STYLE_UNDERLINE;
  if (text_style & blink::kWebAXTextStyleLineThrough)
    browser_text_style |= ui::AX_TEXT_STYLE_LINE_THROUGH;
  return static_cast<ui::AXTextStyle>(browser_text_style);
}

ui::AXAriaCurrentState AXAriaCurrentStateFromBlink(
    blink::WebAXAriaCurrentState aria_current_state) {
  switch (aria_current_state) {
    case blink::kWebAXAriaCurrentStateUndefined:
      return ui::AX_ARIA_CURRENT_STATE_NONE;
    case blink::kWebAXAriaCurrentStateFalse:
      return ui::AX_ARIA_CURRENT_STATE_FALSE;
    case blink::kWebAXAriaCurrentStateTrue:
      return ui::AX_ARIA_CURRENT_STATE_TRUE;
    case blink::kWebAXAriaCurrentStatePage:
      return ui::AX_ARIA_CURRENT_STATE_PAGE;
    case blink::kWebAXAriaCurrentStateStep:
      return ui::AX_ARIA_CURRENT_STATE_STEP;
    case blink::kWebAXAriaCurrentStateLocation:
      return ui::AX_ARIA_CURRENT_STATE_LOCATION;
    case blink::kWebAXAriaCurrentStateDate:
      return ui::AX_ARIA_CURRENT_STATE_DATE;
    case blink::kWebAXAriaCurrentStateTime:
      return ui::AX_ARIA_CURRENT_STATE_TIME;
  }

  NOTREACHED();
  return ui::AX_ARIA_CURRENT_STATE_NONE;
}

ui::AXInvalidState AXInvalidStateFromBlink(
    blink::WebAXInvalidState invalid_state) {
  switch (invalid_state) {
    case blink::kWebAXInvalidStateUndefined:
      return ui::AX_INVALID_STATE_NONE;
    case blink::kWebAXInvalidStateFalse:
      return ui::AX_INVALID_STATE_FALSE;
    case blink::kWebAXInvalidStateTrue:
      return ui::AX_INVALID_STATE_TRUE;
    case blink::kWebAXInvalidStateSpelling:
      return ui::AX_INVALID_STATE_SPELLING;
    case blink::kWebAXInvalidStateGrammar:
      return ui::AX_INVALID_STATE_GRAMMAR;
    case blink::kWebAXInvalidStateOther:
      return ui::AX_INVALID_STATE_OTHER;
  }
  NOTREACHED();
  return ui::AX_INVALID_STATE_NONE;
}

ui::AXCheckedState AXCheckedStateFromBlink(
    blink::WebAXCheckedState checked_state) {
  switch (checked_state) {
    case blink::WebAXCheckedFalse:
      return ui::AX_CHECKED_STATE_FALSE;
    case blink::WebAXCheckedTrue:
      return ui::AX_CHECKED_STATE_TRUE;
    case blink::WebAXCheckedMixed:
      return ui::AX_CHECKED_STATE_MIXED;
  }
  NOTREACHED();
  return ui::AX_CHECKED_STATE_FALSE;
}

ui::AXSortDirection AXSortDirectionFromBlink(
    blink::WebAXSortDirection sort_direction) {
  switch (sort_direction) {
    case blink::kWebAXSortDirectionUndefined:
      return ui::AX_SORT_DIRECTION_NONE;
    case blink::kWebAXSortDirectionNone:
      return ui::AX_SORT_DIRECTION_UNSORTED;
    case blink::kWebAXSortDirectionAscending:
      return ui::AX_SORT_DIRECTION_ASCENDING;
    case blink::kWebAXSortDirectionDescending:
      return ui::AX_SORT_DIRECTION_DESCENDING;
    case blink::kWebAXSortDirectionOther:
      return ui::AX_SORT_DIRECTION_OTHER;
  }
  NOTREACHED();
  return ui::AX_SORT_DIRECTION_NONE;
}

ui::AXNameFrom AXNameFromFromBlink(blink::WebAXNameFrom name_from) {
  switch (name_from) {
    case blink::kWebAXNameFromUninitialized:
      return ui::AX_NAME_FROM_UNINITIALIZED;
    case blink::kWebAXNameFromAttribute:
      return ui::AX_NAME_FROM_ATTRIBUTE;
    case blink::kWebAXNameFromAttributeExplicitlyEmpty:
      return ui::AX_NAME_FROM_ATTRIBUTE_EXPLICITLY_EMPTY;
    case blink::kWebAXNameFromCaption:
      return ui::AX_NAME_FROM_RELATED_ELEMENT;
    case blink::kWebAXNameFromContents:
      return ui::AX_NAME_FROM_CONTENTS;
    case blink::kWebAXNameFromPlaceholder:
      return ui::AX_NAME_FROM_PLACEHOLDER;
    case blink::kWebAXNameFromRelatedElement:
      return ui::AX_NAME_FROM_RELATED_ELEMENT;
    case blink::kWebAXNameFromValue:
      return ui::AX_NAME_FROM_VALUE;
    case blink::kWebAXNameFromTitle:
      return ui::AX_NAME_FROM_ATTRIBUTE;
  }
  NOTREACHED();
  return ui::AX_NAME_FROM_NONE;
}

ui::AXDescriptionFrom AXDescriptionFromFromBlink(
    blink::WebAXDescriptionFrom description_from) {
  switch (description_from) {
    case blink::kWebAXDescriptionFromUninitialized:
      return ui::AX_DESCRIPTION_FROM_UNINITIALIZED;
    case blink::kWebAXDescriptionFromAttribute:
      return ui::AX_DESCRIPTION_FROM_ATTRIBUTE;
    case blink::kWebAXDescriptionFromContents:
      return ui::AX_DESCRIPTION_FROM_CONTENTS;
    case blink::kWebAXDescriptionFromRelatedElement:
      return ui::AX_DESCRIPTION_FROM_RELATED_ELEMENT;
  }
  NOTREACHED();
  return ui::AX_DESCRIPTION_FROM_NONE;
}

ui::AXTextAffinity AXTextAffinityFromBlink(blink::WebAXTextAffinity affinity) {
  switch (affinity) {
    case blink::kWebAXTextAffinityUpstream:
      return ui::AX_TEXT_AFFINITY_UPSTREAM;
    case blink::kWebAXTextAffinityDownstream:
      return ui::AX_TEXT_AFFINITY_DOWNSTREAM;
  }
  NOTREACHED();
  return ui::AX_TEXT_AFFINITY_DOWNSTREAM;
}

}  // namespace content.
