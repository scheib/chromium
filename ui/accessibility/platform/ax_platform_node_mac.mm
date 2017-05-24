// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ui/accessibility/platform/ax_platform_node_mac.h"

#import <Cocoa/Cocoa.h>
#include <stddef.h>

#include "base/macros.h"
#include "base/strings/sys_string_conversions.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_role_properties.h"
#include "ui/accessibility/platform/ax_platform_node.h"
#include "ui/accessibility/platform/ax_platform_node_delegate.h"
#include "ui/base/l10n/l10n_util.h"
#import "ui/gfx/mac/coordinate_conversion.h"
#include "ui/strings/grit/ui_strings.h"

namespace {

struct RoleMapEntry {
  ui::AXRole value;
  NSString* nativeValue;
};

struct EventMapEntry {
  ui::AXEvent value;
  NSString* nativeValue;
};

typedef std::map<ui::AXRole, NSString*> RoleMap;
typedef std::map<ui::AXEvent, NSString*> EventMap;

RoleMap BuildRoleMap() {
  const RoleMapEntry roles[] = {
      {ui::AX_ROLE_ABBR, NSAccessibilityGroupRole},
      {ui::AX_ROLE_ALERT, NSAccessibilityGroupRole},
      {ui::AX_ROLE_ALERT_DIALOG, NSAccessibilityGroupRole},
      {ui::AX_ROLE_ANCHOR, NSAccessibilityGroupRole},
      {ui::AX_ROLE_ANNOTATION, NSAccessibilityUnknownRole},
      {ui::AX_ROLE_APPLICATION, NSAccessibilityGroupRole},
      {ui::AX_ROLE_ARTICLE, NSAccessibilityGroupRole},
      {ui::AX_ROLE_AUDIO, NSAccessibilityGroupRole},
      {ui::AX_ROLE_BANNER, NSAccessibilityGroupRole},
      {ui::AX_ROLE_BLOCKQUOTE, NSAccessibilityGroupRole},
      {ui::AX_ROLE_BUSY_INDICATOR, NSAccessibilityBusyIndicatorRole},
      {ui::AX_ROLE_BUTTON, NSAccessibilityButtonRole},
      {ui::AX_ROLE_CANVAS, NSAccessibilityImageRole},
      {ui::AX_ROLE_CAPTION, NSAccessibilityGroupRole},
      {ui::AX_ROLE_CELL, @"AXCell"},
      {ui::AX_ROLE_CHECK_BOX, NSAccessibilityCheckBoxRole},
      {ui::AX_ROLE_COLOR_WELL, NSAccessibilityColorWellRole},
      {ui::AX_ROLE_COLUMN, NSAccessibilityColumnRole},
      {ui::AX_ROLE_COLUMN_HEADER, @"AXCell"},
      {ui::AX_ROLE_COMBO_BOX, NSAccessibilityComboBoxRole},
      {ui::AX_ROLE_COMPLEMENTARY, NSAccessibilityGroupRole},
      {ui::AX_ROLE_CONTENT_INFO, NSAccessibilityGroupRole},
      {ui::AX_ROLE_DATE, @"AXDateField"},
      {ui::AX_ROLE_DATE_TIME, @"AXDateField"},
      {ui::AX_ROLE_DEFINITION, NSAccessibilityGroupRole},
      {ui::AX_ROLE_DESCRIPTION_LIST_DETAIL, NSAccessibilityGroupRole},
      {ui::AX_ROLE_DESCRIPTION_LIST, NSAccessibilityListRole},
      {ui::AX_ROLE_DESCRIPTION_LIST_TERM, NSAccessibilityGroupRole},
      {ui::AX_ROLE_DIALOG, NSAccessibilityGroupRole},
      {ui::AX_ROLE_DETAILS, NSAccessibilityGroupRole},
      {ui::AX_ROLE_DIRECTORY, NSAccessibilityListRole},
      {ui::AX_ROLE_DISCLOSURE_TRIANGLE, NSAccessibilityDisclosureTriangleRole},
      {ui::AX_ROLE_DOCUMENT, NSAccessibilityGroupRole},
      {ui::AX_ROLE_EMBEDDED_OBJECT, NSAccessibilityGroupRole},
      {ui::AX_ROLE_FIGCAPTION, NSAccessibilityGroupRole},
      {ui::AX_ROLE_FIGURE, NSAccessibilityGroupRole},
      {ui::AX_ROLE_FOOTER, NSAccessibilityGroupRole},
      {ui::AX_ROLE_FORM, NSAccessibilityGroupRole},
      {ui::AX_ROLE_GENERIC_CONTAINER, NSAccessibilityGroupRole},
      {ui::AX_ROLE_GRID, NSAccessibilityGridRole},
      {ui::AX_ROLE_GROUP, NSAccessibilityGroupRole},
      {ui::AX_ROLE_HEADING, @"AXHeading"},
      {ui::AX_ROLE_IFRAME, NSAccessibilityGroupRole},
      {ui::AX_ROLE_IFRAME_PRESENTATIONAL, NSAccessibilityGroupRole},
      {ui::AX_ROLE_IGNORED, NSAccessibilityUnknownRole},
      {ui::AX_ROLE_IMAGE, NSAccessibilityImageRole},
      {ui::AX_ROLE_IMAGE_MAP, NSAccessibilityGroupRole},
      {ui::AX_ROLE_IMAGE_MAP_LINK, NSAccessibilityLinkRole},
      {ui::AX_ROLE_INPUT_TIME, @"AXTimeField"},
      {ui::AX_ROLE_LABEL_TEXT, NSAccessibilityGroupRole},
      {ui::AX_ROLE_LEGEND, NSAccessibilityGroupRole},
      {ui::AX_ROLE_LINK, NSAccessibilityLinkRole},
      {ui::AX_ROLE_LIST, NSAccessibilityListRole},
      {ui::AX_ROLE_LIST_BOX, NSAccessibilityListRole},
      {ui::AX_ROLE_LIST_BOX_OPTION, NSAccessibilityStaticTextRole},
      {ui::AX_ROLE_LIST_ITEM, NSAccessibilityGroupRole},
      {ui::AX_ROLE_LIST_MARKER, @"AXListMarker"},
      {ui::AX_ROLE_LOG, NSAccessibilityGroupRole},
      {ui::AX_ROLE_MAIN, NSAccessibilityGroupRole},
      {ui::AX_ROLE_MARK, NSAccessibilityGroupRole},
      {ui::AX_ROLE_MARQUEE, NSAccessibilityGroupRole},
      {ui::AX_ROLE_MATH, NSAccessibilityGroupRole},
      {ui::AX_ROLE_MENU, NSAccessibilityMenuRole},
      {ui::AX_ROLE_MENU_BAR, NSAccessibilityMenuBarRole},
      {ui::AX_ROLE_MENU_BUTTON, NSAccessibilityButtonRole},
      {ui::AX_ROLE_MENU_ITEM, NSAccessibilityMenuItemRole},
      {ui::AX_ROLE_MENU_ITEM_CHECK_BOX, NSAccessibilityMenuItemRole},
      {ui::AX_ROLE_MENU_ITEM_RADIO, NSAccessibilityMenuItemRole},
      {ui::AX_ROLE_MENU_LIST_OPTION, NSAccessibilityMenuItemRole},
      {ui::AX_ROLE_MENU_LIST_POPUP, NSAccessibilityUnknownRole},
      {ui::AX_ROLE_METER, NSAccessibilityProgressIndicatorRole},
      {ui::AX_ROLE_NAVIGATION, NSAccessibilityGroupRole},
      {ui::AX_ROLE_NONE, NSAccessibilityGroupRole},
      {ui::AX_ROLE_NOTE, NSAccessibilityGroupRole},
      {ui::AX_ROLE_OUTLINE, NSAccessibilityOutlineRole},
      {ui::AX_ROLE_PARAGRAPH, NSAccessibilityGroupRole},
      {ui::AX_ROLE_POP_UP_BUTTON, NSAccessibilityPopUpButtonRole},
      {ui::AX_ROLE_PRE, NSAccessibilityGroupRole},
      {ui::AX_ROLE_PRESENTATIONAL, NSAccessibilityGroupRole},
      {ui::AX_ROLE_PROGRESS_INDICATOR, NSAccessibilityProgressIndicatorRole},
      {ui::AX_ROLE_RADIO_BUTTON, NSAccessibilityRadioButtonRole},
      {ui::AX_ROLE_RADIO_GROUP, NSAccessibilityRadioGroupRole},
      {ui::AX_ROLE_REGION, NSAccessibilityGroupRole},
      {ui::AX_ROLE_ROOT_WEB_AREA, @"AXWebArea"},
      {ui::AX_ROLE_ROW, NSAccessibilityRowRole},
      {ui::AX_ROLE_ROW_HEADER, @"AXCell"},
      {ui::AX_ROLE_RULER, NSAccessibilityRulerRole},
      {ui::AX_ROLE_SCROLL_BAR, NSAccessibilityScrollBarRole},
      {ui::AX_ROLE_SEARCH, NSAccessibilityGroupRole},
      {ui::AX_ROLE_SEARCH_BOX, NSAccessibilityTextFieldRole},
      {ui::AX_ROLE_SLIDER, NSAccessibilitySliderRole},
      {ui::AX_ROLE_SLIDER_THUMB, NSAccessibilityValueIndicatorRole},
      {ui::AX_ROLE_SPIN_BUTTON, NSAccessibilityIncrementorRole},
      {ui::AX_ROLE_SPLITTER, NSAccessibilitySplitterRole},
      {ui::AX_ROLE_STATIC_TEXT, NSAccessibilityStaticTextRole},
      {ui::AX_ROLE_STATUS, NSAccessibilityGroupRole},
      {ui::AX_ROLE_SVG_ROOT, NSAccessibilityGroupRole},
      {ui::AX_ROLE_SWITCH, NSAccessibilityCheckBoxRole},
      {ui::AX_ROLE_TAB, NSAccessibilityRadioButtonRole},
      {ui::AX_ROLE_TABLE, NSAccessibilityTableRole},
      {ui::AX_ROLE_TABLE_HEADER_CONTAINER, NSAccessibilityGroupRole},
      {ui::AX_ROLE_TAB_LIST, NSAccessibilityTabGroupRole},
      {ui::AX_ROLE_TAB_PANEL, NSAccessibilityGroupRole},
      {ui::AX_ROLE_TERM, NSAccessibilityGroupRole},
      {ui::AX_ROLE_TEXT_FIELD, NSAccessibilityTextFieldRole},
      {ui::AX_ROLE_TIME, NSAccessibilityGroupRole},
      {ui::AX_ROLE_TIMER, NSAccessibilityGroupRole},
      {ui::AX_ROLE_TOGGLE_BUTTON, NSAccessibilityCheckBoxRole},
      {ui::AX_ROLE_TOOLBAR, NSAccessibilityToolbarRole},
      {ui::AX_ROLE_TOOLTIP, NSAccessibilityGroupRole},
      {ui::AX_ROLE_TREE, NSAccessibilityOutlineRole},
      {ui::AX_ROLE_TREE_GRID, NSAccessibilityTableRole},
      {ui::AX_ROLE_TREE_ITEM, NSAccessibilityRowRole},
      {ui::AX_ROLE_VIDEO, NSAccessibilityGroupRole},
      {ui::AX_ROLE_WEB_AREA, @"AXWebArea"},
      {ui::AX_ROLE_WINDOW, NSAccessibilityWindowRole},

      // TODO(dtseng): we don't correctly support the attributes for these
      // roles.
      // { ui::AX_ROLE_SCROLL_AREA, NSAccessibilityScrollAreaRole },
  };

  RoleMap role_map;
  for (size_t i = 0; i < arraysize(roles); ++i)
    role_map[roles[i].value] = roles[i].nativeValue;
  return role_map;
}

RoleMap BuildSubroleMap() {
  const RoleMapEntry subroles[] = {
      {ui::AX_ROLE_ALERT, @"AXApplicationAlert"},
      {ui::AX_ROLE_ALERT_DIALOG, @"AXApplicationAlertDialog"},
      {ui::AX_ROLE_APPLICATION, @"AXLandmarkApplication"},
      {ui::AX_ROLE_ARTICLE, @"AXDocumentArticle"},
      {ui::AX_ROLE_BANNER, @"AXLandmarkBanner"},
      {ui::AX_ROLE_COMPLEMENTARY, @"AXLandmarkComplementary"},
      {ui::AX_ROLE_CONTENT_INFO, @"AXLandmarkContentInfo"},
      {ui::AX_ROLE_DEFINITION, @"AXDefinition"},
      {ui::AX_ROLE_DESCRIPTION_LIST_DETAIL, @"AXDefinition"},
      {ui::AX_ROLE_DESCRIPTION_LIST_TERM, @"AXTerm"},
      {ui::AX_ROLE_DIALOG, @"AXApplicationDialog"},
      {ui::AX_ROLE_DOCUMENT, @"AXDocument"},
      {ui::AX_ROLE_FOOTER, @"AXLandmarkContentInfo"},
      {ui::AX_ROLE_FORM, @"AXLandmarkForm"},
      {ui::AX_ROLE_LOG, @"AXApplicationLog"},
      {ui::AX_ROLE_MAIN, @"AXLandmarkMain"},
      {ui::AX_ROLE_MARQUEE, @"AXApplicationMarquee"},
      {ui::AX_ROLE_MATH, @"AXDocumentMath"},
      {ui::AX_ROLE_NAVIGATION, @"AXLandmarkNavigation"},
      {ui::AX_ROLE_NOTE, @"AXDocumentNote"},
      {ui::AX_ROLE_REGION, @"AXDocumentRegion"},
      {ui::AX_ROLE_SEARCH, @"AXLandmarkSearch"},
      {ui::AX_ROLE_SEARCH_BOX, @"AXSearchField"},
      {ui::AX_ROLE_STATUS, @"AXApplicationStatus"},
      {ui::AX_ROLE_SWITCH, @"AXSwitch"},
      {ui::AX_ROLE_TAB_PANEL, @"AXTabPanel"},
      {ui::AX_ROLE_TERM, @"AXTerm"},
      {ui::AX_ROLE_TIMER, @"AXApplicationTimer"},
      {ui::AX_ROLE_TOGGLE_BUTTON, @"AXToggleButton"},
      {ui::AX_ROLE_TOOLTIP, @"AXUserInterfaceTooltip"},
      {ui::AX_ROLE_TREE_ITEM, NSAccessibilityOutlineRowSubrole},
  };

  RoleMap subrole_map;
  for (size_t i = 0; i < arraysize(subroles); ++i)
    subrole_map[subroles[i].value] = subroles[i].nativeValue;
  return subrole_map;
}

EventMap BuildEventMap() {
  const EventMapEntry events[] = {
      {ui::AX_EVENT_FOCUS, NSAccessibilityFocusedUIElementChangedNotification},
      {ui::AX_EVENT_TEXT_CHANGED, NSAccessibilityTitleChangedNotification},
      {ui::AX_EVENT_VALUE_CHANGED, NSAccessibilityValueChangedNotification},
      {ui::AX_EVENT_TEXT_SELECTION_CHANGED,
       NSAccessibilitySelectedTextChangedNotification},
      // TODO(patricialor): Add more events.
  };

  EventMap event_map;
  for (size_t i = 0; i < arraysize(events); ++i)
    event_map[events[i].value] = events[i].nativeValue;
  return event_map;
}

void NotifyMacEvent(AXPlatformNodeCocoa* target, ui::AXEvent event_type) {
  NSAccessibilityPostNotification(
      target, [AXPlatformNodeCocoa nativeNotificationFromAXEvent:event_type]);
}

}  // namespace

@interface AXPlatformNodeCocoa ()
// Helper function for string attributes that don't require extra processing.
- (NSString*)getStringAttribute:(ui::AXStringAttribute)attribute;
@end

@implementation AXPlatformNodeCocoa {
  ui::AXPlatformNodeBase* node_;  // Weak. Retains us.
}

@synthesize node = node_;

// A mapping of AX roles to native roles.
+ (NSString*)nativeRoleFromAXRole:(ui::AXRole)role {
  CR_DEFINE_STATIC_LOCAL(RoleMap, role_map, (BuildRoleMap()));
  RoleMap::iterator it = role_map.find(role);
  return it != role_map.end() ? it->second : NSAccessibilityUnknownRole;
}

// A mapping of AX roles to native subroles.
+ (NSString*)nativeSubroleFromAXRole:(ui::AXRole)role {
  CR_DEFINE_STATIC_LOCAL(RoleMap, subrole_map, (BuildSubroleMap()));
  RoleMap::iterator it = subrole_map.find(role);
  return it != subrole_map.end() ? it->second : nil;
}

// A mapping of AX events to native notifications.
+ (NSString*)nativeNotificationFromAXEvent:(ui::AXEvent)event {
  CR_DEFINE_STATIC_LOCAL(EventMap, event_map, (BuildEventMap()));
  EventMap::iterator it = event_map.find(event);
  return it != event_map.end() ? it->second : nil;
}

- (instancetype)initWithNode:(ui::AXPlatformNodeBase*)node {
  if ((self = [super init])) {
    node_ = node;
  }
  return self;
}

- (void)detach {
  if (!node_)
    return;
  NSAccessibilityPostNotification(
      self, NSAccessibilityUIElementDestroyedNotification);
  node_ = nil;
}

- (NSRect)boundsInScreen {
  if (!node_)
    return NSZeroRect;
  return gfx::ScreenRectToNSRect(node_->GetBoundsInScreen());
}

- (NSString*)getStringAttribute:(ui::AXStringAttribute)attribute {
  std::string attributeValue;
  if (node_->GetStringAttribute(attribute, &attributeValue))
    return base::SysUTF8ToNSString(attributeValue);
  return nil;
}

// NSAccessibility informal protocol implementation.

- (BOOL)accessibilityIsIgnored {
  return [[self AXRole] isEqualToString:NSAccessibilityUnknownRole] ||
         node_->GetData().HasState(ui::AX_STATE_INVISIBLE);
}

- (id)accessibilityHitTest:(NSPoint)point {
  for (AXPlatformNodeCocoa* child in [self AXChildren]) {
    if (NSPointInRect(point, child.boundsInScreen))
      return [child accessibilityHitTest:point];
  }
  return NSAccessibilityUnignoredAncestor(self);
}

- (BOOL)accessibilityNotifiesWhenDestroyed {
  return YES;
}

- (id)accessibilityFocusedUIElement {
  return node_->GetDelegate()->GetFocus();
}

- (NSArray*)accessibilityActionNames {
  base::scoped_nsobject<NSMutableArray> axActions(
      [[NSMutableArray alloc] init]);

  // VoiceOver expects the "press" action to be first.
  if (ui::IsRoleClickable(node_->GetData().role))
    [axActions addObject:NSAccessibilityPressAction];

  return axActions.autorelease();
}

- (void)accessibilityPerformAction:(NSString*)action {
  DCHECK([[self accessibilityActionNames] containsObject:action]);
  ui::AXActionData data;
  if ([action isEqualToString:NSAccessibilityPressAction])
    data.action = ui::AX_ACTION_DO_DEFAULT;

  // Note ui::AX_ACTIONs which are just overwriting an accessibility attribute
  // are already implemented in -accessibilitySetValue:forAttribute:, so ignore
  // those here.

  if (data.action != ui::AX_ACTION_NONE)
    node_->GetDelegate()->AccessibilityPerformAction(data);
}

- (NSArray*)accessibilityAttributeNames {
  // These attributes are required on all accessibility objects.
  NSArray* const kAllRoleAttributes = @[
    NSAccessibilityChildrenAttribute,
    NSAccessibilityParentAttribute,
    NSAccessibilityPositionAttribute,
    NSAccessibilityRoleAttribute,
    NSAccessibilitySizeAttribute,
    NSAccessibilitySubroleAttribute,

    // Title is required for most elements. Cocoa asks for the value even if it
    // is omitted here, but won't present it to accessibility APIs without this.
    NSAccessibilityTitleAttribute,

    // Attributes which are not required, but are general to all roles.
    NSAccessibilityRoleDescriptionAttribute,
    NSAccessibilityEnabledAttribute,
    NSAccessibilityFocusedAttribute,
    NSAccessibilityHelpAttribute,
    NSAccessibilityTopLevelUIElementAttribute,
    NSAccessibilityWindowAttribute,
  ];

  // Attributes required for user-editable controls.
  NSArray* const kValueAttributes = @[ NSAccessibilityValueAttribute ];

  // Attributes required for unprotected textfields.
  NSArray* const kUnprotectedTextfieldAttributes = @[
    NSAccessibilityInsertionPointLineNumberAttribute,
    NSAccessibilityNumberOfCharactersAttribute,
    NSAccessibilitySelectedTextAttribute,
    NSAccessibilitySelectedTextRangeAttribute,
    NSAccessibilityVisibleCharacterRangeAttribute,
  ];

  // Required for all textfields, including protected ones.
  NSString* const kTextfieldAttributes =
      NSAccessibilityPlaceholderValueAttribute;

  base::scoped_nsobject<NSMutableArray> axAttributes(
      [[NSMutableArray alloc] init]);

  [axAttributes addObjectsFromArray:kAllRoleAttributes];
  switch (node_->GetData().role) {
    case ui::AX_ROLE_TEXT_FIELD:
      [axAttributes addObject:kTextfieldAttributes];
      if (!node_->GetData().HasState(ui::AX_STATE_PROTECTED))
        [axAttributes addObjectsFromArray:kUnprotectedTextfieldAttributes];
    // Fallthrough.
    case ui::AX_ROLE_CHECK_BOX:
    case ui::AX_ROLE_COMBO_BOX:
    case ui::AX_ROLE_MENU_ITEM_CHECK_BOX:
    case ui::AX_ROLE_MENU_ITEM_RADIO:
    case ui::AX_ROLE_RADIO_BUTTON:
    case ui::AX_ROLE_SEARCH_BOX:
    case ui::AX_ROLE_SLIDER:
    case ui::AX_ROLE_SLIDER_THUMB:
    case ui::AX_ROLE_TOGGLE_BUTTON:
      [axAttributes addObjectsFromArray:kValueAttributes];
      break;
    // TODO(tapted): Add additional attributes based on role.
    default:
      break;
  }
  return axAttributes.autorelease();
}

- (BOOL)accessibilityIsAttributeSettable:(NSString*)attributeName {
  if (node_->GetData().HasState(ui::AX_STATE_DISABLED))
    return NO;

  // Allow certain attributes to be written via an accessibility client. A
  // writable attribute will only appear as such if the accessibility element
  // has a value set for that attribute.
  if ([attributeName
          isEqualToString:NSAccessibilitySelectedChildrenAttribute] ||
      [attributeName
          isEqualToString:NSAccessibilityVisibleCharacterRangeAttribute]) {
    return NO;
  }

  if ([attributeName isEqualToString:NSAccessibilityValueAttribute]) {
    // NSSecureTextField doesn't allow values to be edited (despite showing up
    // as editable), match its behavior.
    if (node_->GetData().HasState(ui::AX_STATE_PROTECTED))
      return NO;
    // Since tabs use the Radio Button role on Mac, the standard way to set
    // them is via the value attribute rather than the selected attribute.
    if (node_->GetData().role == ui::AX_ROLE_TAB)
      return !node_->GetData().HasState(ui::AX_STATE_SELECTED);
  }

  if ([attributeName isEqualToString:NSAccessibilityValueAttribute] ||
      [attributeName isEqualToString:NSAccessibilitySelectedTextAttribute] ||
      [attributeName
          isEqualToString:NSAccessibilitySelectedTextRangeAttribute]) {
    return !node_->GetData().HasState(ui::AX_STATE_READ_ONLY);
  }

  if ([attributeName isEqualToString:NSAccessibilityFocusedAttribute]) {
    return node_->GetData().HasState(ui::AX_STATE_FOCUSABLE);
  }

  // TODO(patricialor): Add callbacks for updating the above attributes except
  // NSAccessibilityValueAttribute and return YES.
  return NO;
}

- (void)accessibilitySetValue:(id)value forAttribute:(NSString*)attribute {
  ui::AXActionData data;

  // Check for attributes first. Only the |data.action| should be set here - any
  // type-specific information, if needed, should be set below.
  if ([attribute isEqualToString:NSAccessibilityValueAttribute] &&
      !node_->GetData().HasState(ui::AX_STATE_PROTECTED)) {
    data.action = node_->GetData().role == ui::AX_ROLE_TAB
                      ? ui::AX_ACTION_SET_SELECTION
                      : ui::AX_ACTION_SET_VALUE;
  } else if ([attribute isEqualToString:NSAccessibilitySelectedTextAttribute]) {
    data.action = ui::AX_ACTION_REPLACE_SELECTED_TEXT;
  } else if ([attribute
                 isEqualToString:NSAccessibilitySelectedTextRangeAttribute]) {
    data.action = ui::AX_ACTION_SET_SELECTION;
  } else if ([attribute isEqualToString:NSAccessibilityFocusedAttribute]) {
    if ([value isKindOfClass:[NSNumber class]]) {
      data.action =
          [value boolValue] ? ui::AX_ACTION_FOCUS : ui::AX_ACTION_BLUR;
    }
  }

  // Set type-specific information as necessary for actions set above.
  if ([value isKindOfClass:[NSString class]]) {
    data.value = base::SysNSStringToUTF16(value);
  } else if (data.action == ui::AX_ACTION_SET_SELECTION &&
             [value isKindOfClass:[NSValue class]]) {
    NSRange range = [value rangeValue];
    data.anchor_offset = range.location;
    data.focus_offset = NSMaxRange(range);
  }

  if (data.action != ui::AX_ACTION_NONE)
    node_->GetDelegate()->AccessibilityPerformAction(data);

  // TODO(patricialor): Plumb through all the other writable attributes as
  // specified in accessibilityIsAttributeSettable.
}

- (id)accessibilityAttributeValue:(NSString*)attribute {
  SEL selector = NSSelectorFromString(attribute);
  if ([self respondsToSelector:selector])
    return [self performSelector:selector];
  return nil;
}

// NSAccessibility attributes. Order them according to
// NSAccessibilityConstants.h, or see https://crbug.com/678898.

- (NSString*)AXRole {
  if (!node_)
    return nil;
  return [[self class] nativeRoleFromAXRole:node_->GetData().role];
}

- (NSString*)AXRoleDescription {
  switch (node_->GetData().role) {
    case ui::AX_ROLE_TAB:
      // There is no NSAccessibilityTabRole or similar (AXRadioButton is used
      // instead). Do the same as NSTabView and put "tab" in the description.
      return [l10n_util::GetNSStringWithFixup(IDS_ACCNAME_TAB_ROLE_DESCRIPTION)
          lowercaseString];
    default:
      break;
  }
  return NSAccessibilityRoleDescription([self AXRole], [self AXSubrole]);
}

- (NSString*)AXSubrole {
  ui::AXRole role = node_->GetData().role;
  switch (role) {
    case ui::AX_ROLE_TEXT_FIELD:
      if (node_->GetData().HasState(ui::AX_STATE_PROTECTED))
        return NSAccessibilitySecureTextFieldSubrole;
      break;
    default:
      break;
  }
  return [AXPlatformNodeCocoa nativeSubroleFromAXRole:role];
}

- (NSString*)AXHelp {
  return [self getStringAttribute:ui::AX_ATTR_DESCRIPTION];
}

- (id)AXValue {
  if (node_->GetData().role == ui::AX_ROLE_TAB)
    return [self AXSelected];
  return [self getStringAttribute:ui::AX_ATTR_VALUE];
}

- (NSNumber*)AXEnabled {
  return @(!node_->GetData().HasState(ui::AX_STATE_DISABLED));
}

- (NSNumber*)AXFocused {
  if (node_->GetData().HasState(ui::AX_STATE_FOCUSABLE))
    return
        @(node_->GetDelegate()->GetFocus() == node_->GetNativeViewAccessible());
  return @NO;
}

- (id)AXParent {
  if (!node_)
    return nil;
  return NSAccessibilityUnignoredAncestor(node_->GetParent());
}

- (NSArray*)AXChildren {
  if (!node_)
    return nil;
  int count = node_->GetChildCount();
  NSMutableArray* children = [NSMutableArray arrayWithCapacity:count];
  for (int i = 0; i < count; ++i)
    [children addObject:node_->ChildAtIndex(i)];
  return NSAccessibilityUnignoredChildren(children);
}

- (id)AXWindow {
  return node_->GetDelegate()->GetTopLevelWidget();
}

- (id)AXTopLevelUIElement {
  return [self AXWindow];
}

- (NSValue*)AXPosition {
  return [NSValue valueWithPoint:self.boundsInScreen.origin];
}

- (NSValue*)AXSize {
  return [NSValue valueWithSize:self.boundsInScreen.size];
}

- (NSString*)AXTitle {
  return [self getStringAttribute:ui::AX_ATTR_NAME];
}

// Misc attributes.

- (NSNumber*)AXSelected {
  return @(node_->GetData().HasState(ui::AX_STATE_SELECTED));
}

- (NSString*)AXPlaceholderValue {
  return [self getStringAttribute:ui::AX_ATTR_PLACEHOLDER];
}

// Text-specific attributes.

- (NSString*)AXSelectedText {
  if (node_->GetData().HasState(ui::AX_STATE_PROTECTED))
    return nil;

  NSRange selectedTextRange;
  [[self AXSelectedTextRange] getValue:&selectedTextRange];
  return [[self AXValue] substringWithRange:selectedTextRange];
}

- (NSValue*)AXSelectedTextRange {
  if (node_->GetData().HasState(ui::AX_STATE_PROTECTED))
    return nil;

  int textDir, start, end;
  node_->GetIntAttribute(ui::AX_ATTR_TEXT_DIRECTION, &textDir);
  node_->GetIntAttribute(ui::AX_ATTR_TEXT_SEL_START, &start);
  node_->GetIntAttribute(ui::AX_ATTR_TEXT_SEL_END, &end);
  // NSRange cannot represent the direction the text was selected in, so make
  // sure the correct selection index is used when creating a new range, taking
  // into account the textfield text direction as well.
  bool isReversed = (textDir == ui::AX_TEXT_DIRECTION_RTL) ||
                    (textDir == ui::AX_TEXT_DIRECTION_BTT);
  int beginSelectionIndex = (end > start && !isReversed) ? start : end;
  return [NSValue valueWithRange:{beginSelectionIndex, abs(end - start)}];
}

- (NSNumber*)AXNumberOfCharacters {
  if (node_->GetData().HasState(ui::AX_STATE_PROTECTED))
    return nil;
  return @([[self AXValue] length]);
}

- (NSValue*)AXVisibleCharacterRange {
  if (node_->GetData().HasState(ui::AX_STATE_PROTECTED))
    return nil;
  return [NSValue valueWithRange:{0, [[self AXNumberOfCharacters] intValue]}];
}

- (NSNumber*)AXInsertionPointLineNumber {
  if (node_->GetData().HasState(ui::AX_STATE_PROTECTED))
    return nil;
  // Multiline is not supported on views.
  return @0;
}

@end

namespace ui {

// static
AXPlatformNode* AXPlatformNode::Create(AXPlatformNodeDelegate* delegate) {
  AXPlatformNodeBase* node = new AXPlatformNodeMac();
  node->Init(delegate);
  return node;
}

// static
AXPlatformNode* AXPlatformNode::FromNativeViewAccessible(
    gfx::NativeViewAccessible accessible) {
  if ([accessible isKindOfClass:[AXPlatformNodeCocoa class]])
    return [accessible node];
  return nullptr;
}

AXPlatformNodeMac::AXPlatformNodeMac() {
}

AXPlatformNodeMac::~AXPlatformNodeMac() {
}

void AXPlatformNodeMac::Destroy() {
  if (native_node_)
    [native_node_ detach];
  AXPlatformNodeBase::Destroy();
}

gfx::NativeViewAccessible AXPlatformNodeMac::GetNativeViewAccessible() {
  if (!native_node_)
    native_node_.reset([[AXPlatformNodeCocoa alloc] initWithNode:this]);
  return native_node_.get();
}

void AXPlatformNodeMac::NotifyAccessibilityEvent(ui::AXEvent event_type) {
  GetNativeViewAccessible();
  // Add mappings between ui::AXEvent and NSAccessibility notifications using
  // the EventMap above. This switch contains exceptions to those mappings.
  switch (event_type) {
    case ui::AX_EVENT_TEXT_CHANGED:
      // If the view is a user-editable textfield, this should change the value.
      if (GetData().role == ui::AX_ROLE_TEXT_FIELD) {
        NotifyMacEvent(native_node_, ui::AX_EVENT_VALUE_CHANGED);
        return;
      }
      break;
    default:
      break;
  }
  NotifyMacEvent(native_node_, event_type);
}

int AXPlatformNodeMac::GetIndexInParent() {
  // TODO(dmazzoni): implement this.  http://crbug.com/396137
  return -1;
}

}  // namespace ui
