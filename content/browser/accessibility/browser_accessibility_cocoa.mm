// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "content/browser/accessibility/browser_accessibility_cocoa.h"

#include <execinfo.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <map>
#include <memory>
#include <utility>

#include "base/mac/foundation_util.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/strings/string16.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "content/app/strings/grit/content_strings.h"
#include "content/browser/accessibility/ax_platform_position.h"
#include "content/browser/accessibility/browser_accessibility_mac.h"
#include "content/browser/accessibility/browser_accessibility_manager.h"
#include "content/browser/accessibility/browser_accessibility_manager_mac.h"
#include "content/browser/accessibility/one_shot_accessibility_tree_search.h"
#include "content/public/common/content_client.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_range.h"
#import "ui/accessibility/platform/ax_platform_node_mac.h"

using AXPlatformPositionInstance =
    content::AXPlatformPosition::AXPositionInstance;
using AXPlatformRange = ui::AXRange<AXPlatformPositionInstance::element_type>;
using AXTextMarkerRangeRef = CFTypeRef;
using AXTextMarkerRef = CFTypeRef;
using StringAttribute = ui::AXStringAttribute;
using content::AXPlatformPosition;
using content::AccessibilityMatchPredicate;
using content::BrowserAccessibility;
using content::BrowserAccessibilityDelegate;
using content::BrowserAccessibilityManager;
using content::BrowserAccessibilityManagerMac;
using content::ContentClient;
using content::OneShotAccessibilityTreeSearch;
using ui::AXNodeData;
using ui::AXTreeIDRegistry;

namespace {

// Private WebKit accessibility attributes.
NSString* const NSAccessibilityARIAAtomicAttribute = @"AXARIAAtomic";
NSString* const NSAccessibilityARIABusyAttribute = @"AXARIABusy";
NSString* const NSAccessibilityARIAColumnCountAttribute = @"AXARIAColumnCount";
NSString* const NSAccessibilityARIAColumnIndexAttribute = @"AXARIAColumnIndex";
NSString* const NSAccessibilityARIALiveAttribute = @"AXARIALive";
NSString* const NSAccessibilityARIAPosInSetAttribute = @"AXARIAPosInSet";
NSString* const NSAccessibilityARIARelevantAttribute = @"AXARIARelevant";
NSString* const NSAccessibilityARIARowCountAttribute = @"AXARIARowCount";
NSString* const NSAccessibilityARIARowIndexAttribute = @"AXARIARowIndex";
NSString* const NSAccessibilityARIASetSizeAttribute = @"AXARIASetSize";
NSString* const NSAccessibilityAccessKeyAttribute = @"AXAccessKey";
NSString* const NSAccessibilityDOMIdentifierAttribute = @"AXDOMIdentifier";
NSString* const NSAccessibilityDropEffectsAttribute = @"AXDropEffects";
NSString* const NSAccessibilityGrabbedAttribute = @"AXGrabbed";
NSString* const NSAccessibilityInvalidAttribute = @"AXInvalid";
NSString* const NSAccessibilityIsMultiSelectableAttribute =
    @"AXIsMultiSelectable";
NSString* const NSAccessibilityLoadingProgressAttribute = @"AXLoadingProgress";
NSString* const
    NSAccessibilityUIElementCountForSearchPredicateParameterizedAttribute =
        @"AXUIElementCountForSearchPredicate";
NSString* const
    NSAccessibilityUIElementsForSearchPredicateParameterizedAttribute =
        @"AXUIElementsForSearchPredicate";
NSString* const NSAccessibilityVisitedAttribute = @"AXVisited";

// Private attributes for text markers.
NSString* const NSAccessibilityStartTextMarkerAttribute = @"AXStartTextMarker";
NSString* const NSAccessibilityEndTextMarkerAttribute = @"AXEndTextMarker";
NSString* const NSAccessibilitySelectedTextMarkerRangeAttribute =
    @"AXSelectedTextMarkerRange";
NSString* const NSAccessibilityTextMarkerIsValidParameterizedAttribute =
    @"AXTextMarkerIsValid";
NSString* const NSAccessibilityIndexForTextMarkerParameterizedAttribute =
    @"AXIndexForTextMarker";
NSString* const NSAccessibilityTextMarkerForIndexParameterizedAttribute =
    @"AXTextMarkerForIndex";
NSString* const NSAccessibilityEndTextMarkerForBoundsParameterizedAttribute =
    @"AXEndTextMarkerForBounds";
NSString* const NSAccessibilityStartTextMarkerForBoundsParameterizedAttribute =
    @"AXStartTextMarkerForBounds";
NSString* const
    NSAccessibilityLineTextMarkerRangeForTextMarkerParameterizedAttribute =
        @"AXLineTextMarkerRangeForTextMarker";
NSString* const NSAccessibilitySelectTextWithCriteriaParameterizedAttribute =
    @"AXSelectTextWithCriteria";
NSString* const NSAccessibilityBoundsForTextMarkerRangeParameterizedAttribute =
    @"AXBoundsForTextMarkerRange";
NSString* const NSAccessibilityTextMarkerRangeForUnorderedTextMarkersParameterizedAttribute =
    @"AXTextMarkerRangeForUnorderedTextMarkers";
NSString* const NSAccessibilityIndexForChildUIElementParameterizedAttribute =
    @"AXIndexForChildUIElement";

// Actions.
NSString* const NSAccessibilityScrollToVisibleAction = @"AXScrollToVisible";

// A mapping from an accessibility attribute to its method name.
NSDictionary* attributeToMethodNameMap = nil;

// VoiceOver uses -1 to mean "no limit" for AXResultsLimit.
const int kAXResultsLimitNoLimit = -1;

extern "C" {

// The following are private accessibility APIs required for cursor navigation
// and text selection. VoiceOver started relying on them in Mac OS X 10.11.
#if !defined(MAC_OS_X_VERSION_10_11) || \
    MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_11

AXTextMarkerRef AXTextMarkerCreate(CFAllocatorRef allocator,
                                   const UInt8* bytes,
                                   CFIndex length);

const UInt8* AXTextMarkerGetBytePtr(AXTextMarkerRef text_marker);

size_t AXTextMarkerGetLength(AXTextMarkerRef text_marker);

AXTextMarkerRangeRef AXTextMarkerRangeCreate(CFAllocatorRef allocator,
                                             AXTextMarkerRef start_marker,
                                             AXTextMarkerRef end_marker);

AXTextMarkerRef AXTextMarkerRangeCopyStartMarker(
    AXTextMarkerRangeRef text_marker_range);

AXTextMarkerRef AXTextMarkerRangeCopyEndMarker(
    AXTextMarkerRangeRef text_marker_range);

#endif  // MAC_OS_X_VERSION_10_11

}  // extern "C"

// to call |release| on it to transfer ownership of the position to the text
// marker object.
id CreateTextMarker(AXPlatformPositionInstance position) {
  AXTextMarkerRef text_marker = AXTextMarkerCreate(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(position.release()),
      sizeof(AXPlatformPosition));
  return static_cast<id>(
      base::mac::CFTypeRefToNSObjectAutorelease(text_marker));
}

// |range| is destructed at the end of this method and ownership of its |anchor|
// and |focus| are transfered to the marker range object.
id CreateTextMarkerRange(const AXPlatformRange range) {
  AXTextMarkerRef start_marker = AXTextMarkerCreate(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(range.anchor()),
      sizeof(AXPlatformPosition));
  AXTextMarkerRef end_marker = AXTextMarkerCreate(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(range.focus()),
      sizeof(AXPlatformPosition));
  AXTextMarkerRangeRef marker_range =
      AXTextMarkerRangeCreate(kCFAllocatorDefault, start_marker, end_marker);
  return static_cast<id>(
      base::mac::CFTypeRefToNSObjectAutorelease(marker_range));
}

AXPlatformPositionInstance CreatePositionFromTextMarker(
    AXTextMarkerRef text_marker) {
  DCHECK(text_marker);
  if (AXTextMarkerGetLength(text_marker) != sizeof(AXPlatformPosition))
    return AXPlatformPosition::CreateNullPosition();
  const UInt8* source_buffer = AXTextMarkerGetBytePtr(text_marker);
  if (!source_buffer)
    return AXPlatformPosition::CreateNullPosition();
  UInt8* destination_buffer = new UInt8[sizeof(AXPlatformPosition)];
  std::memcpy(destination_buffer, source_buffer, sizeof(AXPlatformPosition));
  AXPlatformPosition::AXPositionInstance position(
      reinterpret_cast<AXPlatformPosition::AXPositionInstance::pointer>(
          destination_buffer));
  if (!position)
    return AXPlatformPosition::CreateNullPosition();
  return position;
}

AXPlatformRange CreateRangeFromTextMarkerRange(
    AXTextMarkerRangeRef marker_range) {
  DCHECK(marker_range);
  base::ScopedCFTypeRef<AXTextMarkerRef> start_marker(
      AXTextMarkerRangeCopyStartMarker(marker_range));
  base::ScopedCFTypeRef<AXTextMarkerRef> end_marker(
      AXTextMarkerRangeCopyEndMarker(marker_range));
  if (!start_marker.get() || !end_marker.get())
    return AXPlatformRange();

  AXPlatformPositionInstance anchor =
      CreatePositionFromTextMarker(start_marker.get());
  AXPlatformPositionInstance focus =
      CreatePositionFromTextMarker(end_marker.get());
  // |AXPlatformRange| takes ownership of its anchor and focus.
  return AXPlatformRange(std::move(anchor), std::move(focus));
}

AXPlatformPositionInstance CreateTextPosition(
    const BrowserAccessibility& object,
    int offset,
    ui::AXTextAffinity affinity) {
  if (!object.instance_active())
    return AXPlatformPosition::CreateNullPosition();

  const BrowserAccessibilityManager* manager = object.manager();
  DCHECK(manager);
  return AXPlatformPosition::CreateTextPosition(
      manager->ax_tree_id(), object.GetId(), offset, affinity);
}

AXPlatformRange CreateTextRange(const BrowserAccessibility& start_object,
                                int start_offset,
                                ui::AXTextAffinity start_affinity,
                                const BrowserAccessibility& end_object,
                                int end_offset,
                                ui::AXTextAffinity end_affinity) {
  AXPlatformPositionInstance anchor =
      CreateTextPosition(start_object, start_offset, start_affinity);
  AXPlatformPositionInstance focus =
      CreateTextPosition(end_object, end_offset, end_affinity);
  // |AXPlatformRange| takes ownership of its anchor and focus.
  return AXPlatformRange(std::move(anchor), std::move(focus));
}

void AddMisspelledTextAttributes(
    const std::vector<const BrowserAccessibility*>& text_only_objects,
    NSMutableAttributedString* attributed_string) {
  [attributed_string beginEditing];
  for (const BrowserAccessibility* text_object : text_only_objects) {
    const std::vector<int32_t>& marker_types =
        text_object->GetIntListAttribute(ui::AX_ATTR_MARKER_TYPES);
    const std::vector<int>& marker_starts =
        text_object->GetIntListAttribute(ui::AX_ATTR_MARKER_STARTS);
    const std::vector<int>& marker_ends =
        text_object->GetIntListAttribute(ui::AX_ATTR_MARKER_ENDS);
    for (size_t i = 0; i < marker_types.size(); ++i) {
      if (!(static_cast<ui::AXMarkerType>(marker_types[i]) &
            ui::AX_MARKER_TYPE_SPELLING)) {
        continue;
      }

      int misspelling_start = marker_starts[i];
      int misspelling_end = marker_ends[i];
      int misspelling_length = misspelling_end - misspelling_start;
      DCHECK_GT(misspelling_length, 0);
      [attributed_string
          addAttribute:NSAccessibilityMarkedMisspelledTextAttribute
                 value:@YES
                 range:NSMakeRange(misspelling_start, misspelling_length)];
    }
  }
  [attributed_string endEditing];
}

NSString* GetTextForTextMarkerRange(AXTextMarkerRangeRef marker_range) {
  AXPlatformRange range = CreateRangeFromTextMarkerRange(marker_range);
  if (range.IsNull())
    return nil;
  return base::SysUTF16ToNSString(range.GetText());
}

NSAttributedString* GetAttributedTextForTextMarkerRange(
    AXTextMarkerRangeRef marker_range) {
  BrowserAccessibility* start_object;
  BrowserAccessibility* end_object;
  int start_offset, end_offset;
  ui::AXTextAffinity start_affinity, end_affinity;
  AXPlatformRange ax_range = CreateRangeFromTextMarkerRange(marker_range);
  if (ax_range.IsNull())
    return nil;
  start_object = ax_range.anchor()->GetAnchor();
  end_object = ax_range.focus()->GetAnchor();
  start_offset = ax_range.anchor()->text_offset();
  end_offset = ax_range.focus()->text_offset();
  start_affinity = ax_range.anchor()->affinity();
  end_affinity = ax_range.focus()->affinity();

  NSString* text = base::SysUTF16ToNSString(
      BrowserAccessibilityManager::GetTextForRange(*start_object, *end_object));
  if ([text length] == 0)
    return nil;

  // Be permissive with the start and end offsets.
  if (start_object == end_object && end_offset < start_offset)
    std::swap(start_offset, end_offset);

  int trim_length = 0;
  if ((end_object->IsSimpleTextControl() || end_object->IsTextOnlyObject()) &&
      end_offset < static_cast<int>(end_object->GetText().length())) {
    trim_length = static_cast<int>(end_object->GetText().length()) - end_offset;
  }
  int range_length = [text length] - start_offset - trim_length;

  // http://crbug.com/651145
  // This shouldn't happen, so this is a temporary workaround to prevent
  // hard crashes.
  if (range_length < 0)
    return nil;

  DCHECK_GE(range_length, 0);
  NSRange range = NSMakeRange(start_offset, range_length);
  DCHECK_LE(NSMaxRange(range), [text length]);

  NSMutableAttributedString* attributed_text =
      [[[NSMutableAttributedString alloc] initWithString:text] autorelease];
  std::vector<const BrowserAccessibility*> text_only_objects =
      BrowserAccessibilityManager::FindTextOnlyObjectsInRange(*start_object,
                                                              *end_object);
  AddMisspelledTextAttributes(text_only_objects, attributed_text);
  return [attributed_text attributedSubstringFromRange:range];
}

// Returns an autoreleased copy of the AXNodeData's attribute.
NSString* NSStringForStringAttribute(
    BrowserAccessibility* browserAccessibility,
    StringAttribute attribute) {
  return base::SysUTF8ToNSString(
      browserAccessibility->GetStringAttribute(attribute));
}

// GetState checks the bitmask used in AXNodeData to check
// if the given state was set on the accessibility object.
bool GetState(BrowserAccessibility* accessibility, ui::AXState state) {
  return ((accessibility->GetState() >> state) & 1);
}

// Given a search key provided to AXUIElementCountForSearchPredicate or
// AXUIElementsForSearchPredicate, return a predicate that can be added
// to OneShotAccessibilityTreeSearch.
AccessibilityMatchPredicate PredicateForSearchKey(NSString* searchKey) {
  if ([searchKey isEqualToString:@"AXAnyTypeSearchKey"]) {
    return [](BrowserAccessibility* start, BrowserAccessibility* current) {
      return true;
    };
  } else if ([searchKey isEqualToString:@"AXBlockquoteSameLevelSearchKey"]) {
    // TODO(dmazzoni): implement the "same level" part.
    return content::AccessibilityBlockquotePredicate;
  } else if ([searchKey isEqualToString:@"AXBlockquoteSearchKey"]) {
    return content::AccessibilityBlockquotePredicate;
  } else if ([searchKey isEqualToString:@"AXBoldFontSearchKey"]) {
    return content::AccessibilityTextStyleBoldPredicate;
  } else if ([searchKey isEqualToString:@"AXButtonSearchKey"]) {
    return content::AccessibilityButtonPredicate;
  } else if ([searchKey isEqualToString:@"AXCheckBoxSearchKey"]) {
    return content::AccessibilityCheckboxPredicate;
  } else if ([searchKey isEqualToString:@"AXControlSearchKey"]) {
    return content::AccessibilityControlPredicate;
  } else if ([searchKey isEqualToString:@"AXDifferentTypeSearchKey"]) {
    return [](BrowserAccessibility* start, BrowserAccessibility* current) {
      return current->GetRole() != start->GetRole();
    };
  } else if ([searchKey isEqualToString:@"AXFontChangeSearchKey"]) {
    // TODO(dmazzoni): implement this.
    return nullptr;
  } else if ([searchKey isEqualToString:@"AXFontColorChangeSearchKey"]) {
    // TODO(dmazzoni): implement this.
    return nullptr;
  } else if ([searchKey isEqualToString:@"AXFrameSearchKey"]) {
    return content::AccessibilityFramePredicate;
  } else if ([searchKey isEqualToString:@"AXGraphicSearchKey"]) {
    return content::AccessibilityGraphicPredicate;
  } else if ([searchKey isEqualToString:@"AXHeadingLevel1SearchKey"]) {
    return content::AccessibilityH1Predicate;
  } else if ([searchKey isEqualToString:@"AXHeadingLevel2SearchKey"]) {
    return content::AccessibilityH2Predicate;
  } else if ([searchKey isEqualToString:@"AXHeadingLevel3SearchKey"]) {
    return content::AccessibilityH3Predicate;
  } else if ([searchKey isEqualToString:@"AXHeadingLevel4SearchKey"]) {
    return content::AccessibilityH4Predicate;
  } else if ([searchKey isEqualToString:@"AXHeadingLevel5SearchKey"]) {
    return content::AccessibilityH5Predicate;
  } else if ([searchKey isEqualToString:@"AXHeadingLevel6SearchKey"]) {
    return content::AccessibilityH6Predicate;
  } else if ([searchKey isEqualToString:@"AXHeadingSameLevelSearchKey"]) {
    return content::AccessibilityHeadingSameLevelPredicate;
  } else if ([searchKey isEqualToString:@"AXHeadingSearchKey"]) {
    return content::AccessibilityHeadingPredicate;
  } else if ([searchKey isEqualToString:@"AXHighlightedSearchKey"]) {
    // TODO(dmazzoni): implement this.
    return nullptr;
  } else if ([searchKey isEqualToString:@"AXItalicFontSearchKey"]) {
    return content::AccessibilityTextStyleItalicPredicate;
  } else if ([searchKey isEqualToString:@"AXLandmarkSearchKey"]) {
    return content::AccessibilityLandmarkPredicate;
  } else if ([searchKey isEqualToString:@"AXLinkSearchKey"]) {
    return content::AccessibilityLinkPredicate;
  } else if ([searchKey isEqualToString:@"AXListSearchKey"]) {
    return content::AccessibilityListPredicate;
  } else if ([searchKey isEqualToString:@"AXLiveRegionSearchKey"]) {
    return content::AccessibilityLiveRegionPredicate;
  } else if ([searchKey isEqualToString:@"AXMisspelledWordSearchKey"]) {
    // TODO(dmazzoni): implement this.
    return nullptr;
  } else if ([searchKey isEqualToString:@"AXOutlineSearchKey"]) {
    return content::AccessibilityTreePredicate;
  } else if ([searchKey isEqualToString:@"AXPlainTextSearchKey"]) {
    // TODO(dmazzoni): implement this.
    return nullptr;
  } else if ([searchKey isEqualToString:@"AXRadioGroupSearchKey"]) {
    return content::AccessibilityRadioGroupPredicate;
  } else if ([searchKey isEqualToString:@"AXSameTypeSearchKey"]) {
    return [](BrowserAccessibility* start, BrowserAccessibility* current) {
      return current->GetRole() == start->GetRole();
    };
  } else if ([searchKey isEqualToString:@"AXStaticTextSearchKey"]) {
    return [](BrowserAccessibility* start, BrowserAccessibility* current) {
      return current->IsTextOnlyObject();
    };
  } else if ([searchKey isEqualToString:@"AXStyleChangeSearchKey"]) {
    // TODO(dmazzoni): implement this.
    return nullptr;
  } else if ([searchKey isEqualToString:@"AXTableSameLevelSearchKey"]) {
    // TODO(dmazzoni): implement the "same level" part.
    return content::AccessibilityTablePredicate;
  } else if ([searchKey isEqualToString:@"AXTableSearchKey"]) {
    return content::AccessibilityTablePredicate;
  } else if ([searchKey isEqualToString:@"AXTextFieldSearchKey"]) {
    return content::AccessibilityTextfieldPredicate;
  } else if ([searchKey isEqualToString:@"AXUnderlineSearchKey"]) {
    return content::AccessibilityTextStyleUnderlinePredicate;
  } else if ([searchKey isEqualToString:@"AXUnvisitedLinkSearchKey"]) {
    return content::AccessibilityUnvisitedLinkPredicate;
  } else if ([searchKey isEqualToString:@"AXVisitedLinkSearchKey"]) {
    return content::AccessibilityVisitedLinkPredicate;
  }

  return nullptr;
}

// Initialize a OneShotAccessibilityTreeSearch object given the parameters
// passed to AXUIElementCountForSearchPredicate or
// AXUIElementsForSearchPredicate. Return true on success.
bool InitializeAccessibilityTreeSearch(
    OneShotAccessibilityTreeSearch* search,
    id parameter) {
  if (![parameter isKindOfClass:[NSDictionary class]])
    return false;
  NSDictionary* dictionary = parameter;

  id startElementParameter = [dictionary objectForKey:@"AXStartElement"];
  if ([startElementParameter isKindOfClass:[BrowserAccessibilityCocoa class]]) {
    BrowserAccessibilityCocoa* startNodeCocoa =
        (BrowserAccessibilityCocoa*)startElementParameter;
    search->SetStartNode([startNodeCocoa browserAccessibility]);
  }

  bool immediateDescendantsOnly = false;
  NSNumber *immediateDescendantsOnlyParameter =
      [dictionary objectForKey:@"AXImmediateDescendantsOnly"];
  if ([immediateDescendantsOnlyParameter isKindOfClass:[NSNumber class]])
    immediateDescendantsOnly = [immediateDescendantsOnlyParameter boolValue];

  bool visibleOnly = false;
  NSNumber *visibleOnlyParameter = [dictionary objectForKey:@"AXVisibleOnly"];
  if ([visibleOnlyParameter isKindOfClass:[NSNumber class]])
    visibleOnly = [visibleOnlyParameter boolValue];

  content::OneShotAccessibilityTreeSearch::Direction direction =
      content::OneShotAccessibilityTreeSearch::FORWARDS;
  NSString* directionParameter = [dictionary objectForKey:@"AXDirection"];
  if ([directionParameter isKindOfClass:[NSString class]]) {
    if ([directionParameter isEqualToString:@"AXDirectionNext"])
      direction = content::OneShotAccessibilityTreeSearch::FORWARDS;
    else if ([directionParameter isEqualToString:@"AXDirectionPrevious"])
      direction = content::OneShotAccessibilityTreeSearch::BACKWARDS;
  }

  int resultsLimit = kAXResultsLimitNoLimit;
  NSNumber* resultsLimitParameter = [dictionary objectForKey:@"AXResultsLimit"];
  if ([resultsLimitParameter isKindOfClass:[NSNumber class]])
    resultsLimit = [resultsLimitParameter intValue];

  std::string searchText;
  NSString* searchTextParameter = [dictionary objectForKey:@"AXSearchText"];
  if ([searchTextParameter isKindOfClass:[NSString class]])
    searchText = base::SysNSStringToUTF8(searchTextParameter);

  search->SetDirection(direction);
  search->SetImmediateDescendantsOnly(immediateDescendantsOnly);
  search->SetVisibleOnly(visibleOnly);
  search->SetSearchText(searchText);

  // Mac uses resultsLimit == -1 for unlimited, that that's
  // the default for OneShotAccessibilityTreeSearch already.
  // Only set the results limit if it's nonnegative.
  if (resultsLimit >= 0)
    search->SetResultLimit(resultsLimit);

  id searchKey = [dictionary objectForKey:@"AXSearchKey"];
  if ([searchKey isKindOfClass:[NSString class]]) {
    AccessibilityMatchPredicate predicate =
        PredicateForSearchKey((NSString*)searchKey);
    if (predicate)
      search->AddPredicate(predicate);
  } else if ([searchKey isKindOfClass:[NSArray class]]) {
    size_t searchKeyCount = static_cast<size_t>([searchKey count]);
    for (size_t i = 0; i < searchKeyCount; ++i) {
      id key = [searchKey objectAtIndex:i];
      if ([key isKindOfClass:[NSString class]]) {
        AccessibilityMatchPredicate predicate =
            PredicateForSearchKey((NSString*)key);
        if (predicate)
          search->AddPredicate(predicate);
      }
    }
  }

  return true;
}

}  // namespace

// The following private WebKit accessibility attribute became public in 10.12.
#if !defined(MAC_OS_X_VERSION_10_12) || \
    MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_12
extern "C" {
NSString* const NSAccessibilityRequiredAttribute = @"AXRequired";
}
#endif  // MAC_OS_X_VERSION_10_12

@implementation BrowserAccessibilityCocoa

+ (void)initialize {
  const struct {
    NSString* attribute;
    NSString* methodName;
  } attributeToMethodNameContainer[] = {
      {NSAccessibilityARIAAtomicAttribute, @"ariaAtomic"},
      {NSAccessibilityARIABusyAttribute, @"ariaBusy"},
      {NSAccessibilityARIAColumnCountAttribute, @"ariaColumnCount"},
      {NSAccessibilityARIAColumnIndexAttribute, @"ariaColumnIndex"},
      {NSAccessibilityARIALiveAttribute, @"ariaLive"},
      {NSAccessibilityARIAPosInSetAttribute, @"ariaPosInSet"},
      {NSAccessibilityARIARelevantAttribute, @"ariaRelevant"},
      {NSAccessibilityARIARowCountAttribute, @"ariaRowCount"},
      {NSAccessibilityARIARowIndexAttribute, @"ariaRowIndex"},
      {NSAccessibilityARIASetSizeAttribute, @"ariaSetSize"},
      {NSAccessibilityAccessKeyAttribute, @"accessKey"},
      {NSAccessibilityChildrenAttribute, @"children"},
      {NSAccessibilityColumnsAttribute, @"columns"},
      {NSAccessibilityColumnHeaderUIElementsAttribute, @"columnHeaders"},
      {NSAccessibilityColumnIndexRangeAttribute, @"columnIndexRange"},
      {NSAccessibilityContentsAttribute, @"contents"},
      {NSAccessibilityDescriptionAttribute, @"description"},
      {NSAccessibilityDisclosingAttribute, @"disclosing"},
      {NSAccessibilityDisclosedByRowAttribute, @"disclosedByRow"},
      {NSAccessibilityDisclosureLevelAttribute, @"disclosureLevel"},
      {NSAccessibilityDisclosedRowsAttribute, @"disclosedRows"},
      {NSAccessibilityDropEffectsAttribute, @"dropEffects"},
      {NSAccessibilityDOMIdentifierAttribute, @"domIdentifier"},
      {NSAccessibilityEnabledAttribute, @"enabled"},
      {NSAccessibilityEndTextMarkerAttribute, @"endTextMarker"},
      {NSAccessibilityExpandedAttribute, @"expanded"},
      {NSAccessibilityFocusedAttribute, @"focused"},
      {NSAccessibilityGrabbedAttribute, @"grabbed"},
      {NSAccessibilityHeaderAttribute, @"header"},
      {NSAccessibilityHelpAttribute, @"help"},
      {NSAccessibilityIndexAttribute, @"index"},
      {NSAccessibilityInsertionPointLineNumberAttribute,
       @"insertionPointLineNumber"},
      {NSAccessibilityInvalidAttribute, @"invalid"},
      {NSAccessibilityIsMultiSelectableAttribute, @"isMultiSelectable"},
      {NSAccessibilityLinkedUIElementsAttribute, @"linkedUIElements"},
      {NSAccessibilityLoadingProgressAttribute, @"loadingProgress"},
      {NSAccessibilityMaxValueAttribute, @"maxValue"},
      {NSAccessibilityMinValueAttribute, @"minValue"},
      {NSAccessibilityNumberOfCharactersAttribute, @"numberOfCharacters"},
      {NSAccessibilityOrientationAttribute, @"orientation"},
      {NSAccessibilityParentAttribute, @"parent"},
      {NSAccessibilityPlaceholderValueAttribute, @"placeholderValue"},
      {NSAccessibilityPositionAttribute, @"position"},
      {NSAccessibilityRequiredAttribute, @"required"},
      {NSAccessibilityRoleAttribute, @"role"},
      {NSAccessibilityRoleDescriptionAttribute, @"roleDescription"},
      {NSAccessibilityRowHeaderUIElementsAttribute, @"rowHeaders"},
      {NSAccessibilityRowIndexRangeAttribute, @"rowIndexRange"},
      {NSAccessibilityRowsAttribute, @"rows"},
      // TODO(aboxhall): expose
      // NSAccessibilityServesAsTitleForUIElementsAttribute
      {NSAccessibilityStartTextMarkerAttribute, @"startTextMarker"},
      {NSAccessibilitySelectedAttribute, @"selected"},
      {NSAccessibilitySelectedChildrenAttribute, @"selectedChildren"},
      {NSAccessibilitySelectedTextAttribute, @"selectedText"},
      {NSAccessibilitySelectedTextRangeAttribute, @"selectedTextRange"},
      {NSAccessibilitySelectedTextMarkerRangeAttribute,
       @"selectedTextMarkerRange"},
      {NSAccessibilitySizeAttribute, @"size"},
      {NSAccessibilitySortDirectionAttribute, @"sortDirection"},
      {NSAccessibilitySubroleAttribute, @"subrole"},
      {NSAccessibilityTabsAttribute, @"tabs"},
      {NSAccessibilityTitleAttribute, @"title"},
      {NSAccessibilityTitleUIElementAttribute, @"titleUIElement"},
      {NSAccessibilityTopLevelUIElementAttribute, @"window"},
      {NSAccessibilityURLAttribute, @"url"},
      {NSAccessibilityValueAttribute, @"value"},
      {NSAccessibilityValueDescriptionAttribute, @"valueDescription"},
      {NSAccessibilityVisibleCharacterRangeAttribute, @"visibleCharacterRange"},
      {NSAccessibilityVisibleCellsAttribute, @"visibleCells"},
      {NSAccessibilityVisibleChildrenAttribute, @"visibleChildren"},
      {NSAccessibilityVisibleColumnsAttribute, @"visibleColumns"},
      {NSAccessibilityVisibleRowsAttribute, @"visibleRows"},
      {NSAccessibilityVisitedAttribute, @"visited"},
      {NSAccessibilityWindowAttribute, @"window"},
      {@"AXLoaded", @"loaded"},
  };

  NSMutableDictionary* dict = [[NSMutableDictionary alloc] init];
  const size_t numAttributes = sizeof(attributeToMethodNameContainer) /
                               sizeof(attributeToMethodNameContainer[0]);
  for (size_t i = 0; i < numAttributes; ++i) {
    [dict setObject:attributeToMethodNameContainer[i].methodName
             forKey:attributeToMethodNameContainer[i].attribute];
  }
  attributeToMethodNameMap = dict;
  dict = nil;
}

- (instancetype)initWithObject:(BrowserAccessibility*)accessibility {
  if ((self = [super init]))
    browserAccessibility_ = accessibility;
  return self;
}

- (void)detach {
  if (!browserAccessibility_)
    return;
  NSAccessibilityPostNotification(
      self, NSAccessibilityUIElementDestroyedNotification);
  browserAccessibility_ = nullptr;
}

- (NSString*)accessKey {
  if (![self instanceActive])
    return nil;
  return NSStringForStringAttribute(
      browserAccessibility_, ui::AX_ATTR_ACCESS_KEY);
}

- (NSNumber*)ariaAtomic {
  if (![self instanceActive])
    return nil;
  bool boolValue = browserAccessibility_->GetBoolAttribute(
      ui::AX_ATTR_LIVE_ATOMIC);
  return [NSNumber numberWithBool:boolValue];
}

- (NSNumber*)ariaBusy {
  if (![self instanceActive])
    return nil;
  return [NSNumber numberWithBool:
      GetState(browserAccessibility_, ui::AX_STATE_BUSY)];
}

- (NSNumber*)ariaColumnCount {
  if (!browserAccessibility_->IsTableLikeRole())
    return nil;
  int count = -1;
  if (!browserAccessibility_->GetIntAttribute(ui::AX_ATTR_ARIA_COLUMN_COUNT,
                                              &count)) {
    return nil;
  }
  return [NSNumber numberWithInt:count];
}

- (NSNumber*)ariaColumnIndex {
  if (!browserAccessibility_->IsCellOrTableHeaderRole())
    return nil;
  int index = -1;
  if (!browserAccessibility_->GetIntAttribute(
          ui::AX_ATTR_ARIA_CELL_COLUMN_INDEX, &index)) {
    return nil;
  }
  return [NSNumber numberWithInt:index];
}

- (NSString*)ariaLive {
  if (![self instanceActive])
    return nil;
  return NSStringForStringAttribute(
      browserAccessibility_, ui::AX_ATTR_LIVE_STATUS);
}

- (NSNumber*)ariaPosInSet {
  if (![self instanceActive])
    return nil;
  return [NSNumber numberWithInt:
      browserAccessibility_->GetIntAttribute(ui::AX_ATTR_POS_IN_SET)];
}

- (NSString*)ariaRelevant {
  if (![self instanceActive])
    return nil;
  return NSStringForStringAttribute(browserAccessibility_,
                                    ui::AX_ATTR_LIVE_RELEVANT);
}

- (NSNumber*)ariaRowCount {
  if (!browserAccessibility_->IsTableLikeRole())
    return nil;
  int count = -1;
  if (!browserAccessibility_->GetIntAttribute(ui::AX_ATTR_ARIA_ROW_COUNT,
                                              &count)) {
    return nil;
  }
  return [NSNumber numberWithInt:count];
}

- (NSNumber*)ariaRowIndex {
  if (!browserAccessibility_->IsCellOrTableHeaderRole())
    return nil;
  int index = -1;
  if (!browserAccessibility_->GetIntAttribute(ui::AX_ATTR_ARIA_CELL_ROW_INDEX,
                                              &index)) {
    return nil;
  }
  return [NSNumber numberWithInt:index];
}

- (NSNumber*)ariaSetSize {
  if (![self instanceActive])
    return nil;
  return [NSNumber numberWithInt:
      browserAccessibility_->GetIntAttribute(ui::AX_ATTR_SET_SIZE)];
}

// Returns an array of BrowserAccessibilityCocoa objects, representing the
// accessibility children of this object.
- (NSArray*)children {
  if (![self instanceActive])
    return nil;
  if (!children_) {
    uint32_t childCount = browserAccessibility_->PlatformChildCount();
    children_.reset([[NSMutableArray alloc] initWithCapacity:childCount]);
    for (uint32_t index = 0; index < childCount; ++index) {
      BrowserAccessibilityCocoa* child =
          ToBrowserAccessibilityCocoa(
              browserAccessibility_->PlatformGetChild(index));
      if ([child isIgnored])
        [children_ addObjectsFromArray:[child children]];
      else
        [children_ addObject:child];
    }

    // Also, add indirect children (if any).
    const std::vector<int32_t>& indirectChildIds =
        browserAccessibility_->GetIntListAttribute(
            ui::AX_ATTR_INDIRECT_CHILD_IDS);
    for (uint32_t i = 0; i < indirectChildIds.size(); ++i) {
      int32_t child_id = indirectChildIds[i];
      BrowserAccessibility* child =
          browserAccessibility_->manager()->GetFromID(child_id);

      // This only became necessary as a result of crbug.com/93095. It should be
      // a DCHECK in the future.
      if (child) {
        BrowserAccessibilityCocoa* child_cocoa =
            ToBrowserAccessibilityCocoa(child);
        [children_ addObject:child_cocoa];
      }
    }
  }
  return children_;
}

- (void)childrenChanged {
  if (![self instanceActive])
    return;
  if (![self isIgnored]) {
    children_.reset();
  } else {
    [ToBrowserAccessibilityCocoa(browserAccessibility_->PlatformGetParent())
        childrenChanged];
  }
}

- (NSArray*)columnHeaders {
  if (![self instanceActive])
    return nil;
  if (!browserAccessibility_->IsTableLikeRole()) {
    return nil;
  }

  NSMutableArray* ret = [[[NSMutableArray alloc] init] autorelease];
  const std::vector<int32_t>& uniqueCellIds =
      browserAccessibility_->GetIntListAttribute(ui::AX_ATTR_UNIQUE_CELL_IDS);
  for (size_t i = 0; i < uniqueCellIds.size(); ++i) {
    int id = uniqueCellIds[i];
    BrowserAccessibility* cell =
        browserAccessibility_->manager()->GetFromID(id);
    if (cell && cell->GetRole() == ui::AX_ROLE_COLUMN_HEADER)
      [ret addObject:ToBrowserAccessibilityCocoa(cell)];
  }
  return ret;
}

- (NSValue*)columnIndexRange {
  if (![self instanceActive])
    return nil;
  if (!browserAccessibility_->IsCellOrTableHeaderRole())
    return nil;

  int column = -1;
  int colspan = -1;
  browserAccessibility_->GetIntAttribute(
      ui::AX_ATTR_TABLE_CELL_COLUMN_INDEX, &column);
  browserAccessibility_->GetIntAttribute(
      ui::AX_ATTR_TABLE_CELL_COLUMN_SPAN, &colspan);
  if (column >= 0 && colspan >= 1)
    return [NSValue valueWithRange:NSMakeRange(column, colspan)];
  return nil;
}

- (NSArray*)columns {
  if (![self instanceActive])
    return nil;
  NSMutableArray* ret = [[[NSMutableArray alloc] init] autorelease];
  for (BrowserAccessibilityCocoa* child in [self children]) {
    if ([[child role] isEqualToString:NSAccessibilityColumnRole])
      [ret addObject:child];
  }
  return ret;
}

- (NSString*)description {
  if (![self instanceActive])
    return nil;

  // Mac OS X wants static text exposed in AXValue.
  if ([self shouldExposeNameInAXValue])
    return @"";

  // If we're exposing the title in TitleUIElement, don't also redundantly
  // expose it in AXDescription.
  if ([self shouldExposeTitleUIElement])
    return @"";

  ui::AXNameFrom nameFrom = static_cast<ui::AXNameFrom>(
      browserAccessibility_->GetIntAttribute(ui::AX_ATTR_NAME_FROM));
  std::string name = browserAccessibility_->GetStringAttribute(
      ui::AX_ATTR_NAME);
  if (!name.empty()) {
    // On Mac OS X, the accessible name of an object is exposed as its
    // title if it comes from visible text, and as its description
    // otherwise, but never both.
    if (nameFrom == ui::AX_NAME_FROM_CONTENTS ||
        nameFrom == ui::AX_NAME_FROM_RELATED_ELEMENT ||
        nameFrom == ui::AX_NAME_FROM_VALUE) {
      return @"";
    } else {
      return base::SysUTF8ToNSString(name);
    }
  }

  // Given an image where there's no other title, return the base part
  // of the filename as the description.
  if ([[self role] isEqualToString:NSAccessibilityImageRole]) {
    if (browserAccessibility_->HasExplicitlyEmptyName())
      return @"";
    if ([self titleUIElement])
      return @"";

    std::string url;
    if (browserAccessibility_->GetStringAttribute(
            ui::AX_ATTR_URL, &url)) {
      // Given a url like http://foo.com/bar/baz.png, just return the
      // base name, e.g., "baz.png".
      size_t leftIndex = url.rfind('/');
      std::string basename =
          leftIndex != std::string::npos ? url.substr(leftIndex) : url;
      return base::SysUTF8ToNSString(basename);
    }
  }

  // If it's focusable but didn't have any other name or value, compute a name
  // from its descendants.
  base::string16 value = browserAccessibility_->GetValue();
  if (browserAccessibility_->HasState(ui::AX_STATE_FOCUSABLE) &&
      !browserAccessibility_->IsControl() &&
      value.empty() &&
      [self internalRole] != ui::AX_ROLE_DATE_TIME &&
      [self internalRole] != ui::AX_ROLE_WEB_AREA &&
      [self internalRole] != ui::AX_ROLE_ROOT_WEB_AREA) {
    return base::SysUTF8ToNSString(
        browserAccessibility_->ComputeAccessibleNameFromDescendants());
  }

  return @"";
}

- (NSNumber*)disclosing {
  if (![self instanceActive])
    return nil;
  if ([self internalRole] == ui::AX_ROLE_TREE_ITEM) {
    return [NSNumber numberWithBool:
        GetState(browserAccessibility_, ui::AX_STATE_EXPANDED)];
  } else {
    return nil;
  }
}

- (id)disclosedByRow {
  if (![self instanceActive])
    return nil;

  // The row that contains this row.
  // It should be the same as the first parent that is a treeitem.
  return nil;
}

- (NSNumber*)disclosureLevel {
  if (![self instanceActive])
    return nil;
  ui::AXRole role = [self internalRole];
  if (role == ui::AX_ROLE_ROW ||
      role == ui::AX_ROLE_TREE_ITEM) {
    int level = browserAccessibility_->GetIntAttribute(
        ui::AX_ATTR_HIERARCHICAL_LEVEL);
    // Mac disclosureLevel is 0-based, but web levels are 1-based.
    if (level > 0)
      level--;
    return [NSNumber numberWithInt:level];
  } else {
    return nil;
  }
}

- (id)disclosedRows {
  if (![self instanceActive])
    return nil;

  // The rows that are considered inside this row.
  return nil;
}

- (NSString*)dropEffects {
  if (![self instanceActive])
    return nil;

  std::string dropEffects;
  if (browserAccessibility_->GetHtmlAttribute("aria-dropeffect", &dropEffects))
    return base::SysUTF8ToNSString(dropEffects);

  return nil;
}

- (NSString*)domIdentifier {
  if (![self instanceActive])
    return nil;

  std::string id;
  if (browserAccessibility_->GetHtmlAttribute("id", &id))
    return base::SysUTF8ToNSString(id);

  return nil;
}

- (NSNumber*)enabled {
  if (![self instanceActive])
    return nil;
  return [NSNumber numberWithBool:
      !GetState(browserAccessibility_, ui::AX_STATE_DISABLED)];
}

// Returns a text marker that points to the last character in the document that
// can be selected with VoiceOver.
- (id)endTextMarker {
  const BrowserAccessibility* root =
      browserAccessibility_->manager()->GetRoot();
  if (!root)
    return nil;

  AXPlatformPositionInstance position = root->CreatePositionAt(0);
  return CreateTextMarker(position->CreatePositionAtEndOfAnchor());
}

- (NSNumber*)expanded {
  if (![self instanceActive])
    return nil;
  return [NSNumber numberWithBool:
      GetState(browserAccessibility_, ui::AX_STATE_EXPANDED)];
}

- (NSNumber*)focused {
  if (![self instanceActive])
    return nil;
  BrowserAccessibilityManager* manager = browserAccessibility_->manager();
  NSNumber* ret = [NSNumber numberWithBool:
      manager->GetFocus() == browserAccessibility_];
  return ret;
}

- (NSNumber*)grabbed {
  if (![self instanceActive])
    return nil;
  std::string grabbed;
  if (browserAccessibility_->GetHtmlAttribute("aria-grabbed", &grabbed) &&
      grabbed == "true")
    return [NSNumber numberWithBool:YES];

  return [NSNumber numberWithBool:NO];
}

- (id)header {
  if (![self instanceActive])
    return nil;
  int headerElementId = -1;
  if (browserAccessibility_->IsTableLikeRole()) {
    browserAccessibility_->GetIntAttribute(
        ui::AX_ATTR_TABLE_HEADER_ID, &headerElementId);
  } else if ([self internalRole] == ui::AX_ROLE_COLUMN) {
    browserAccessibility_->GetIntAttribute(
        ui::AX_ATTR_TABLE_COLUMN_HEADER_ID, &headerElementId);
  } else if ([self internalRole] == ui::AX_ROLE_ROW) {
    browserAccessibility_->GetIntAttribute(
        ui::AX_ATTR_TABLE_ROW_HEADER_ID, &headerElementId);
  }

  if (headerElementId > 0) {
    BrowserAccessibility* headerObject =
        browserAccessibility_->manager()->GetFromID(headerElementId);
    if (headerObject)
      return ToBrowserAccessibilityCocoa(headerObject);
  }
  return nil;
}

- (NSString*)help {
  if (![self instanceActive])
    return nil;
  return NSStringForStringAttribute(
      browserAccessibility_, ui::AX_ATTR_DESCRIPTION);
}

- (NSNumber*)index {
  if (![self instanceActive])
    return nil;
  if ([self internalRole] == ui::AX_ROLE_COLUMN) {
    int columnIndex = browserAccessibility_->GetIntAttribute(
          ui::AX_ATTR_TABLE_COLUMN_INDEX);
    return [NSNumber numberWithInt:columnIndex];
  } else if ([self internalRole] == ui::AX_ROLE_ROW) {
    int rowIndex = browserAccessibility_->GetIntAttribute(
        ui::AX_ATTR_TABLE_ROW_INDEX);
    return [NSNumber numberWithInt:rowIndex];
  }

  return nil;
}

- (NSNumber*)insertionPointLineNumber {
  if (![self instanceActive])
    return nil;

  // TODO(nektar): Deprecate sel_start and sel_end attributes.
  int selStart, selEnd;
  if (!browserAccessibility_->GetIntAttribute(ui::AX_ATTR_TEXT_SEL_START,
                                              &selStart) ||
      !browserAccessibility_->GetIntAttribute(ui::AX_ATTR_TEXT_SEL_END,
                                              &selEnd)) {
    return nil;
  }

  if (selStart > selEnd)
    std::swap(selStart, selEnd);

  const std::vector<int> line_breaks =
      browserAccessibility_->GetLineStartOffsets();
  for (int i = 0; i < static_cast<int>(line_breaks.size()); ++i) {
    if (line_breaks[i] > selStart)
      return [NSNumber numberWithInt:i];
  }

  return [NSNumber numberWithInt:static_cast<int>(line_breaks.size())];
}

// Returns whether or not this node should be ignored in the
// accessibility tree.
- (BOOL)isIgnored {
  if (![self instanceActive])
    return YES;
  return [[self role] isEqualToString:NSAccessibilityUnknownRole];
}

- (NSString*)invalid {
  if (![self instanceActive])
    return nil;
  int invalidState;
  if (!browserAccessibility_->GetIntAttribute(
      ui::AX_ATTR_INVALID_STATE, &invalidState))
    return @"false";

  switch (invalidState) {
  case ui::AX_INVALID_STATE_FALSE:
    return @"false";
  case ui::AX_INVALID_STATE_TRUE:
    return @"true";
  case ui::AX_INVALID_STATE_SPELLING:
    return @"spelling";
  case ui::AX_INVALID_STATE_GRAMMAR:
    return @"grammar";
  case ui::AX_INVALID_STATE_OTHER:
    {
      std::string ariaInvalidValue;
      if (browserAccessibility_->GetStringAttribute(
          ui::AX_ATTR_ARIA_INVALID_VALUE,
          &ariaInvalidValue))
        return base::SysUTF8ToNSString(ariaInvalidValue);
      // Return @"true" since we cannot be more specific about the value.
      return @"true";
    }
  default:
    NOTREACHED();
  }

  return @"false";
}

- (NSNumber*)isMultiSelectable {
  if (![self instanceActive])
    return nil;
  return [NSNumber numberWithBool:GetState(browserAccessibility_,
                                           ui::AX_STATE_MULTISELECTABLE)];
}

- (NSString*)placeholderValue {
  if (![self instanceActive])
    return nil;
  ui::AXNameFrom nameFrom = static_cast<ui::AXNameFrom>(
      browserAccessibility_->GetIntAttribute(ui::AX_ATTR_NAME_FROM));
  if (nameFrom == ui::AX_NAME_FROM_PLACEHOLDER) {
    return NSStringForStringAttribute(
        browserAccessibility_, ui::AX_ATTR_NAME);
  }

  ui::AXDescriptionFrom descriptionFrom = static_cast<ui::AXDescriptionFrom>(
      browserAccessibility_->GetIntAttribute(ui::AX_ATTR_DESCRIPTION_FROM));
  if (descriptionFrom == ui::AX_DESCRIPTION_FROM_PLACEHOLDER) {
    return NSStringForStringAttribute(
        browserAccessibility_, ui::AX_ATTR_DESCRIPTION);
  }

  return NSStringForStringAttribute(
      browserAccessibility_, ui::AX_ATTR_PLACEHOLDER);
}

// private
- (void)addLinkedUIElementsFromAttribute:(ui::AXIntListAttribute)attribute
                                   addTo:(NSMutableArray*)outArray {
  const std::vector<int32_t>& attributeValues =
      browserAccessibility_->GetIntListAttribute(attribute);
  for (size_t i = 0; i < attributeValues.size(); ++i) {
    BrowserAccessibility* element =
        browserAccessibility_->manager()->GetFromID(attributeValues[i]);
    if (element)
      [outArray addObject:ToBrowserAccessibilityCocoa(element)];
  }
}

// private
- (NSArray*)linkedUIElements {
  NSMutableArray* ret = [[[NSMutableArray alloc] init] autorelease];
  [self addLinkedUIElementsFromAttribute:ui::AX_ATTR_CONTROLS_IDS addTo:ret];
  [self addLinkedUIElementsFromAttribute:ui::AX_ATTR_FLOWTO_IDS addTo:ret];

  int target_id;
  if (browserAccessibility_->GetIntAttribute(ui::AX_ATTR_IN_PAGE_LINK_TARGET_ID,
                                             &target_id)) {
    BrowserAccessibility* target = browserAccessibility_->manager()->GetFromID(
        static_cast<int32_t>(target_id));
    if (target)
      [ret addObject:ToBrowserAccessibilityCocoa(target)];
  }

  [self addLinkedUIElementsFromAttribute:ui::AX_ATTR_RADIO_GROUP_IDS addTo:ret];
  if ([ret count] == 0)
    return nil;
  return ret;
}

- (NSNumber*)loaded {
  if (![self instanceActive])
    return nil;
  return [NSNumber numberWithBool:YES];
}

- (NSNumber*)loadingProgress {
  if (![self instanceActive])
    return nil;
  BrowserAccessibilityManager* manager = browserAccessibility_->manager();
  float floatValue = manager->GetTreeData().loading_progress;
  return [NSNumber numberWithFloat:floatValue];
}

- (NSNumber*)maxValue {
  if (![self instanceActive])
    return nil;
  float floatValue = browserAccessibility_->GetFloatAttribute(
      ui::AX_ATTR_MAX_VALUE_FOR_RANGE);
  return [NSNumber numberWithFloat:floatValue];
}

- (NSNumber*)minValue {
  if (![self instanceActive])
    return nil;
  float floatValue = browserAccessibility_->GetFloatAttribute(
      ui::AX_ATTR_MIN_VALUE_FOR_RANGE);
  return [NSNumber numberWithFloat:floatValue];
}

- (NSString*)orientation {
  if (![self instanceActive])
    return nil;
  if (GetState(browserAccessibility_, ui::AX_STATE_VERTICAL))
    return NSAccessibilityVerticalOrientationValue;
  else if (GetState(browserAccessibility_, ui::AX_STATE_HORIZONTAL))
    return NSAccessibilityHorizontalOrientationValue;

  return @"";
}

- (NSNumber*)numberOfCharacters {
  if (![self instanceActive])
    return nil;
  base::string16 value = browserAccessibility_->GetValue();
  return [NSNumber numberWithUnsignedInt:value.size()];
}

// The origin of this accessibility object in the page's document.
// This is relative to webkit's top-left origin, not Cocoa's
// bottom-left origin.
- (NSPoint)origin {
  if (![self instanceActive])
    return NSMakePoint(0, 0);
  gfx::Rect bounds = browserAccessibility_->GetPageBoundsRect();
  return NSMakePoint(bounds.x(), bounds.y());
}

- (id)parent {
  if (![self instanceActive])
    return nil;
  // A nil parent means we're the root.
  if (browserAccessibility_->PlatformGetParent()) {
    return NSAccessibilityUnignoredAncestor(ToBrowserAccessibilityCocoa(
        browserAccessibility_->PlatformGetParent()));
  } else {
    // Hook back up to RenderWidgetHostViewCocoa.
    BrowserAccessibilityManagerMac* manager =
        browserAccessibility_->manager()->GetRootManager()
            ->ToBrowserAccessibilityManagerMac();
    if (manager)
      return manager->GetParentView();
    return nil;
  }
}

- (NSValue*)position {
  if (![self instanceActive])
    return nil;
  NSPoint origin = [self origin];
  NSSize size = [[self size] sizeValue];
  NSPoint pointInScreen = [self pointInScreen:origin size:size];
  return [NSValue valueWithPoint:pointInScreen];
}

- (NSNumber*)required {
  if (![self instanceActive])
    return nil;
  return [NSNumber numberWithBool:
      GetState(browserAccessibility_, ui::AX_STATE_REQUIRED)];
}

// Returns an enum indicating the role from browserAccessibility_.
// internal
- (ui::AXRole)internalRole {
  return static_cast<ui::AXRole>(browserAccessibility_->GetRole());
}

// Returns true if this should expose its accessible name in AXValue.
// internal
- (BOOL)shouldExposeNameInAXValue {
  switch ([self internalRole]) {
    case ui::AX_ROLE_LIST_BOX_OPTION:
    case ui::AX_ROLE_LIST_MARKER:
    case ui::AX_ROLE_MENU_LIST_OPTION:
    case ui::AX_ROLE_STATIC_TEXT:
      return true;
    default:
      return false;
  }
}

// Returns true if this object should expose its accessible name using
// AXTitleUIElement rather than AXTitle or AXDescription. We only do
// this if it's a control, if there's a single label, and the label has
// nonempty text.
// internal
- (BOOL)shouldExposeTitleUIElement {
  // VoiceOver ignores TitleUIElement if the element isn't a control.
  if (!browserAccessibility_->IsControl())
    return false;

  ui::AXNameFrom nameFrom = static_cast<ui::AXNameFrom>(
      browserAccessibility_->GetIntAttribute(ui::AX_ATTR_NAME_FROM));
  if (nameFrom != ui::AX_NAME_FROM_RELATED_ELEMENT)
    return false;

  std::vector<int32_t> labelledby_ids =
  browserAccessibility_->GetIntListAttribute(ui::AX_ATTR_LABELLEDBY_IDS);
  if (labelledby_ids.size() != 1)
    return false;

  BrowserAccessibility* label =
      browserAccessibility_->manager()->GetFromID(labelledby_ids[0]);
  if (!label)
    return false;

  std::string labelName = label->GetStringAttribute(ui::AX_ATTR_NAME);
  return !labelName.empty();
}

// internal
- (content::BrowserAccessibilityDelegate*)delegate {
  return [self instanceActive] ? browserAccessibility_->manager()->delegate()
                               : nil;
}

- (content::BrowserAccessibility*)browserAccessibility {
  return browserAccessibility_;
}

- (BOOL)instanceActive {
  return browserAccessibility_ && browserAccessibility_->instance_active();
}

// internal
- (NSPoint)pointInScreen:(NSPoint)origin
                    size:(NSSize)size {
  if (![self instanceActive])
    return NSZeroPoint;

  // Get the delegate for the topmost BrowserAccessibilityManager, because
  // that's the only one that can convert points to their origin in the screen.
  BrowserAccessibilityDelegate* delegate =
      browserAccessibility_->manager()->GetDelegateFromRootManager();
  if (delegate) {
    gfx::Rect bounds(origin.x, origin.y, size.width, size.height);
    gfx::Point point = delegate->AccessibilityOriginInScreen(bounds);
    return NSMakePoint(point.x(), point.y());
  } else {
    return NSZeroPoint;
  }
}

// Returns a string indicating the NSAccessibility role of this object.
- (NSString*)role {
  if (![self instanceActive])
    return nil;

  ui::AXRole role = [self internalRole];
  if (role == ui::AX_ROLE_CANVAS &&
      browserAccessibility_->GetBoolAttribute(
          ui::AX_ATTR_CANVAS_HAS_FALLBACK)) {
    return NSAccessibilityGroupRole;
  }
  if (role == ui::AX_ROLE_BUTTON || role == ui::AX_ROLE_TOGGLE_BUTTON) {
    bool isAriaPressedDefined;
    bool isMixed;
    browserAccessibility_->GetAriaTristate("aria-pressed",
                                           &isAriaPressedDefined,
                                           &isMixed);
    if (isAriaPressedDefined)
      return NSAccessibilityCheckBoxRole;
    else
      return NSAccessibilityButtonRole;
  }
  if ((browserAccessibility_->IsSimpleTextControl() &&
       browserAccessibility_->HasState(ui::AX_STATE_MULTILINE)) ||
      browserAccessibility_->IsRichTextControl()) {
    return NSAccessibilityTextAreaRole;
  }

  // If this is a web area for a presentational iframe, give it a role of
  // something other than WebArea so that the fact that it's a separate doc
  // is not exposed to AT.
  if (browserAccessibility_->IsWebAreaForPresentationalIframe())
    return NSAccessibilityGroupRole;

  return [AXPlatformNodeCocoa nativeRoleFromAXRole:role];
}

// Returns a string indicating the role description of this object.
- (NSString*)roleDescription {
  if (![self instanceActive])
    return nil;

  if (browserAccessibility_->HasStringAttribute(
      ui::AX_ATTR_ROLE_DESCRIPTION)) {
    return NSStringForStringAttribute(
        browserAccessibility_, ui::AX_ATTR_ROLE_DESCRIPTION);
  }

  NSString* role = [self role];

  ContentClient* content_client = content::GetContentClient();

  // The following descriptions are specific to webkit.
  if ([role isEqualToString:@"AXWebArea"]) {
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_WEB_AREA));
  }

  if ([role isEqualToString:@"NSAccessibilityLinkRole"]) {
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_LINK));
  }

  if ([role isEqualToString:@"AXHeading"]) {
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_HEADING));
  }

  if (([role isEqualToString:NSAccessibilityGroupRole] ||
       [role isEqualToString:NSAccessibilityRadioButtonRole]) &&
      !browserAccessibility_->IsWebAreaForPresentationalIframe()) {
    std::string role;
    if (browserAccessibility_->GetHtmlAttribute("role", &role)) {
      ui::AXRole internalRole = [self internalRole];
      if ((internalRole != ui::AX_ROLE_GROUP &&
           internalRole != ui::AX_ROLE_LIST_ITEM) ||
          internalRole == ui::AX_ROLE_TAB) {
        // TODO(dtseng): This is not localized; see crbug/84814.
        return base::SysUTF8ToNSString(role);
      }
    }
  }

  switch([self internalRole]) {
  case ui::AX_ROLE_ARTICLE:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_ARTICLE));
  case ui::AX_ROLE_BANNER:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_BANNER));
  case ui::AX_ROLE_CHECK_BOX:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_CHECK_BOX));
  case ui::AX_ROLE_COMPLEMENTARY:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_COMPLEMENTARY));
  case ui::AX_ROLE_CONTENT_INFO:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_CONTENT_INFO));
  case ui::AX_ROLE_DESCRIPTION_LIST:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_DESCRIPTION_LIST));
  case ui::AX_ROLE_DESCRIPTION_LIST_DETAIL:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_DEFINITION));
  case ui::AX_ROLE_DESCRIPTION_LIST_TERM:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_DESCRIPTION_TERM));
  case ui::AX_ROLE_FIGURE:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_FIGURE));
  case ui::AX_ROLE_FOOTER:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_FOOTER));
  case ui::AX_ROLE_FORM:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_FORM));
  case ui::AX_ROLE_MAIN:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_MAIN_CONTENT));
  case ui::AX_ROLE_MARK:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_MARK));
  case ui::AX_ROLE_MATH:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_MATH));
  case ui::AX_ROLE_NAVIGATION:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_NAVIGATIONAL_LINK));
  case ui::AX_ROLE_REGION:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_REGION));
  case ui::AX_ROLE_SPIN_BUTTON:
    // This control is similar to what VoiceOver calls a "stepper".
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_STEPPER));
  case ui::AX_ROLE_STATUS:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_STATUS));
  case ui::AX_ROLE_SEARCH_BOX:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_SEARCH_BOX));
  case ui::AX_ROLE_SWITCH:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_SWITCH));
  case ui::AX_ROLE_TERM:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_DESCRIPTION_TERM));
  case ui::AX_ROLE_TOGGLE_BUTTON:
    return base::SysUTF16ToNSString(content_client->GetLocalizedString(
        IDS_AX_ROLE_TOGGLE_BUTTON));
  default:
    break;
  }

  return NSAccessibilityRoleDescription(role, nil);
}

- (NSArray*)rowHeaders {
  if (![self instanceActive])
    return nil;
  if (!browserAccessibility_->IsTableLikeRole()) {
    return nil;
  }

  NSMutableArray* ret = [[[NSMutableArray alloc] init] autorelease];
  const std::vector<int32_t>& uniqueCellIds =
      browserAccessibility_->GetIntListAttribute(ui::AX_ATTR_UNIQUE_CELL_IDS);
  for (size_t i = 0; i < uniqueCellIds.size(); ++i) {
    int id = uniqueCellIds[i];
    BrowserAccessibility* cell =
        browserAccessibility_->manager()->GetFromID(id);
    if (cell && cell->GetRole() == ui::AX_ROLE_ROW_HEADER)
      [ret addObject:ToBrowserAccessibilityCocoa(cell)];
  }
  return ret;
}

- (NSValue*)rowIndexRange {
  if (![self instanceActive])
    return nil;
  if (!browserAccessibility_->IsCellOrTableHeaderRole())
    return nil;

  int row = -1;
  int rowspan = -1;
  browserAccessibility_->GetIntAttribute(
      ui::AX_ATTR_TABLE_CELL_ROW_INDEX, &row);
  browserAccessibility_->GetIntAttribute(
      ui::AX_ATTR_TABLE_CELL_ROW_SPAN, &rowspan);
  if (row >= 0 && rowspan >= 1)
    return [NSValue valueWithRange:NSMakeRange(row, rowspan)];
  return nil;
}

- (NSArray*)rows {
  if (![self instanceActive])
    return nil;
  NSMutableArray* ret = [[[NSMutableArray alloc] init] autorelease];

  if ([self internalRole] == ui::AX_ROLE_TABLE||
      [self internalRole] == ui::AX_ROLE_GRID) {
    for (BrowserAccessibilityCocoa* child in [self children]) {
      if ([[child role] isEqualToString:NSAccessibilityRowRole])
        [ret addObject:child];
    }
  } else if ([self internalRole] == ui::AX_ROLE_COLUMN) {
    const std::vector<int32_t>& indirectChildIds =
        browserAccessibility_->GetIntListAttribute(
            ui::AX_ATTR_INDIRECT_CHILD_IDS);
    for (uint32_t i = 0; i < indirectChildIds.size(); ++i) {
      int id = indirectChildIds[i];
      BrowserAccessibility* rowElement =
          browserAccessibility_->manager()->GetFromID(id);
      if (rowElement)
        [ret addObject:ToBrowserAccessibilityCocoa(rowElement)];
    }
  }

  return ret;
}

- (NSNumber*)selected {
  if (![self instanceActive])
    return nil;
  // TODO(nektar): Implement.
  return [NSNumber numberWithBool:NO];
}

- (NSArray*)selectedChildren {
  if (![self instanceActive])
    return nil;
  NSMutableArray* ret = [[[NSMutableArray alloc] init] autorelease];
  BrowserAccessibilityManager* manager = browserAccessibility_->manager();
  BrowserAccessibility* focusedChild = manager->GetFocus();
  if (!focusedChild->IsDescendantOf(browserAccessibility_))
    focusedChild = nullptr;

  // If it's not multiselectable, try to skip iterating over the
  // children.
  if (!GetState(browserAccessibility_, ui::AX_STATE_MULTISELECTABLE)) {
    // First try the focused child.
    if (focusedChild && focusedChild != browserAccessibility_) {
      [ret addObject:ToBrowserAccessibilityCocoa(focusedChild)];
      return ret;
    }

    // Next try the active descendant.
    int activeDescendantId;
    if (browserAccessibility_->GetIntAttribute(
            ui::AX_ATTR_ACTIVEDESCENDANT_ID, &activeDescendantId)) {
      BrowserAccessibility* activeDescendant =
          manager->GetFromID(activeDescendantId);
      if (activeDescendant) {
        [ret addObject:ToBrowserAccessibilityCocoa(activeDescendant)];
        return ret;
      }
    }
  }

  // If it's multiselectable or if the previous attempts failed,
  // return any children with the "selected" state, which may
  // come from aria-selected.
  uint32_t childCount = browserAccessibility_->PlatformChildCount();
  for (uint32_t index = 0; index < childCount; ++index) {
    BrowserAccessibility* child =
      browserAccessibility_->PlatformGetChild(index);
    if (child->HasState(ui::AX_STATE_SELECTED))
      [ret addObject:ToBrowserAccessibilityCocoa(child)];
  }

  // And if nothing's selected but one has focus, use the focused one.
  if ([ret count] == 0 &&
      focusedChild &&
      focusedChild != browserAccessibility_) {
    [ret addObject:ToBrowserAccessibilityCocoa(focusedChild)];
  }

  return ret;
}

- (NSString*)selectedText {
  if (![self instanceActive])
    return nil;

  // TODO(nektar): Deprecate sel_start and sel_end attributes.
  int selStart, selEnd;
  if (!browserAccessibility_->GetIntAttribute(ui::AX_ATTR_TEXT_SEL_START,
                                              &selStart) ||
      !browserAccessibility_->GetIntAttribute(ui::AX_ATTR_TEXT_SEL_END,
                                              &selEnd)) {
    return nil;
  }

  if (selStart > selEnd)
    std::swap(selStart, selEnd);

  int selLength = selEnd - selStart;
  base::string16 value = browserAccessibility_->GetValue();
  return base::SysUTF16ToNSString(value.substr(selStart, selLength));
}

- (NSValue*)selectedTextRange {
  if (![self instanceActive])
    return nil;

  // TODO(nektar): Deprecate sel_start and sel_end attributes.
  int selStart, selEnd;
  if (!browserAccessibility_->GetIntAttribute(ui::AX_ATTR_TEXT_SEL_START,
                                              &selStart) ||
      !browserAccessibility_->GetIntAttribute(ui::AX_ATTR_TEXT_SEL_END,
                                              &selEnd)) {
    return nil;
  }

  if (selStart > selEnd)
    std::swap(selStart, selEnd);

  int selLength = selEnd - selStart;
  return [NSValue valueWithRange:NSMakeRange(selStart, selLength)];
}

- (id)selectedTextMarkerRange {
  if (![self instanceActive])
    return nil;

  BrowserAccessibilityManager* manager = browserAccessibility_->manager();
  if (!manager)
    return nil;

  int32_t anchorId = manager->GetTreeData().sel_anchor_object_id;
  const BrowserAccessibility* anchorObject = manager->GetFromID(anchorId);
  if (!anchorObject)
    return nil;

  int32_t focusId = manager->GetTreeData().sel_focus_object_id;
  const BrowserAccessibility* focusObject = manager->GetFromID(focusId);
  if (!focusObject)
    return nil;

  int anchorOffset = manager->GetTreeData().sel_anchor_offset;
  int focusOffset = manager->GetTreeData().sel_focus_offset;
  if (anchorOffset < 0 || focusOffset < 0)
    return nil;

  ui::AXTextAffinity anchorAffinity =
        manager->GetTreeData().sel_anchor_affinity;
  ui::AXTextAffinity focusAffinity = manager->GetTreeData().sel_focus_affinity;

  return CreateTextMarkerRange(CreateTextRange(*anchorObject, anchorOffset,
                                               anchorAffinity, *focusObject,
                                               focusOffset, focusAffinity));
}

- (NSValue*)size {
  if (![self instanceActive])
    return nil;
  gfx::Rect bounds = browserAccessibility_->GetPageBoundsRect();
  return  [NSValue valueWithSize:NSMakeSize(bounds.width(), bounds.height())];
}

- (NSString*)sortDirection {
  if (![self instanceActive])
    return nil;
  int sortDirection;
  if (!browserAccessibility_->GetIntAttribute(
      ui::AX_ATTR_SORT_DIRECTION, &sortDirection))
    return nil;

  switch (sortDirection) {
  case ui::AX_SORT_DIRECTION_UNSORTED:
    return nil;
  case ui::AX_SORT_DIRECTION_ASCENDING:
    return NSAccessibilityAscendingSortDirectionValue;
  case ui::AX_SORT_DIRECTION_DESCENDING:
    return NSAccessibilityDescendingSortDirectionValue;
  case ui::AX_SORT_DIRECTION_OTHER:
    return NSAccessibilityUnknownSortDirectionValue;
  default:
    NOTREACHED();
  }

  return nil;
}

// Returns a text marker that points to the first character in the document that
// can be selected with VoiceOver.
- (id)startTextMarker {
  const BrowserAccessibility* root =
      browserAccessibility_->manager()->GetRoot();
  if (!root)
    return nil;

  AXPlatformPositionInstance position = root->CreatePositionAt(0);
  return CreateTextMarker(position->CreatePositionAtStartOfAnchor());
}

// Returns a subrole based upon the role.
- (NSString*) subrole {
  if (![self instanceActive])
    return nil;
  ui::AXRole browserAccessibilityRole = [self internalRole];
  if (browserAccessibilityRole == ui::AX_ROLE_TEXT_FIELD &&
      GetState(browserAccessibility_, ui::AX_STATE_PROTECTED)) {
    return NSAccessibilitySecureTextFieldSubrole;
  }

  if (browserAccessibilityRole == ui::AX_ROLE_DESCRIPTION_LIST)
    return NSAccessibilityDefinitionListSubrole;

  if (browserAccessibilityRole == ui::AX_ROLE_LIST)
    return NSAccessibilityContentListSubrole;

  return [AXPlatformNodeCocoa nativeSubroleFromAXRole:browserAccessibilityRole];
}

// Returns all tabs in this subtree.
- (NSArray*)tabs {
  if (![self instanceActive])
    return nil;
  NSMutableArray* tabSubtree = [[[NSMutableArray alloc] init] autorelease];

  if ([self internalRole] == ui::AX_ROLE_TAB)
    [tabSubtree addObject:self];

  for (uint i=0; i < [[self children] count]; ++i) {
    NSArray* tabChildren = [[[self children] objectAtIndex:i] tabs];
    if ([tabChildren count] > 0)
      [tabSubtree addObjectsFromArray:tabChildren];
  }

  return tabSubtree;
}

- (NSString*)title {
  if (![self instanceActive])
    return nil;
  // Mac OS X wants static text exposed in AXValue.
  if ([self shouldExposeNameInAXValue])
    return @"";

  // If we're exposing the title in TitleUIElement, don't also redundantly
  // expose it in AXDescription.
  if ([self shouldExposeTitleUIElement])
    return @"";

  // On Mac OS X, the accessible name of an object is exposed as its
  // title if it comes from visible text, and as its description
  // otherwise, but never both.
  ui::AXNameFrom nameFrom = static_cast<ui::AXNameFrom>(
      browserAccessibility_->GetIntAttribute(ui::AX_ATTR_NAME_FROM));
  if (nameFrom == ui::AX_NAME_FROM_CONTENTS ||
      nameFrom == ui::AX_NAME_FROM_RELATED_ELEMENT ||
      nameFrom == ui::AX_NAME_FROM_VALUE) {
    return NSStringForStringAttribute(
        browserAccessibility_, ui::AX_ATTR_NAME);
  }

  return nil;
}

- (id)titleUIElement {
  if (![self instanceActive])
    return nil;
  if (![self shouldExposeTitleUIElement])
    return nil;

  std::vector<int32_t> labelledby_ids =
      browserAccessibility_->GetIntListAttribute(ui::AX_ATTR_LABELLEDBY_IDS);
  ui::AXNameFrom nameFrom = static_cast<ui::AXNameFrom>(
      browserAccessibility_->GetIntAttribute(ui::AX_ATTR_NAME_FROM));
  if (nameFrom == ui::AX_NAME_FROM_RELATED_ELEMENT &&
      labelledby_ids.size() == 1) {
    BrowserAccessibility* titleElement =
        browserAccessibility_->manager()->GetFromID(labelledby_ids[0]);
    if (titleElement)
      return ToBrowserAccessibilityCocoa(titleElement);
  }

  return nil;
}

- (NSURL*)url {
  if (![self instanceActive])
    return nil;
  std::string url;
  if ([[self role] isEqualToString:@"AXWebArea"])
    url = browserAccessibility_->manager()->GetTreeData().url;
  else
    url = browserAccessibility_->GetStringAttribute(ui::AX_ATTR_URL);

  if (url.empty())
    return nil;

  return [NSURL URLWithString:(base::SysUTF8ToNSString(url))];
}

- (id)value {
  if (![self instanceActive])
    return nil;
  NSString* role = [self role];
  if ([self shouldExposeNameInAXValue]) {
    return NSStringForStringAttribute(
        browserAccessibility_, ui::AX_ATTR_NAME);
  } else if ([role isEqualToString:@"AXHeading"]) {
    int level = 0;
    if (browserAccessibility_->GetIntAttribute(
            ui::AX_ATTR_HIERARCHICAL_LEVEL, &level)) {
      return [NSNumber numberWithInt:level];
    }
  } else if ([role isEqualToString:NSAccessibilityButtonRole]) {
    // AXValue does not make sense for pure buttons.
    return @"";
  } else if ([self internalRole] == ui::AX_ROLE_TOGGLE_BUTTON) {
    int value = 0;
    bool isAriaPressedDefined;
    bool isMixed;
    value = browserAccessibility_->GetAriaTristate(
        "aria-pressed", &isAriaPressedDefined, &isMixed) ? 1 : 0;

    if (isMixed)
      value = 2;

    return [NSNumber numberWithInt:value];

  } else if ([role isEqualToString:NSAccessibilityCheckBoxRole] ||
             [role isEqualToString:NSAccessibilityRadioButtonRole] ||
             [self internalRole] == ui::AX_ROLE_MENU_ITEM_CHECK_BOX ||
             [self internalRole] == ui::AX_ROLE_MENU_ITEM_RADIO) {
    int value;
    const auto checkedState = static_cast<ui::AXCheckedState>(
        browserAccessibility_->GetIntAttribute(ui::AX_ATTR_CHECKED_STATE));
    switch (checkedState) {
      case ui::AX_CHECKED_STATE_TRUE:
        value = 1;
        break;
      case ui::AX_CHECKED_STATE_MIXED:
        value = 2;
        break;
      default:
        value = GetState(browserAccessibility_, ui::AX_STATE_SELECTED) ? 1 : 0;
        break;
    }
    return [NSNumber numberWithInt:value];
  } else if ([role isEqualToString:NSAccessibilityProgressIndicatorRole] ||
             [role isEqualToString:NSAccessibilitySliderRole] ||
             [role isEqualToString:NSAccessibilityIncrementorRole] ||
             [role isEqualToString:NSAccessibilityScrollBarRole]) {
    float floatValue;
    if (browserAccessibility_->GetFloatAttribute(
            ui::AX_ATTR_VALUE_FOR_RANGE, &floatValue)) {
      return [NSNumber numberWithFloat:floatValue];
    }
  } else if ([role isEqualToString:NSAccessibilityColorWellRole]) {
    unsigned int color = static_cast<unsigned int>(
        browserAccessibility_->GetIntAttribute(ui::AX_ATTR_COLOR_VALUE));
    unsigned int red = SkColorGetR(color);
    unsigned int green = SkColorGetG(color);
    unsigned int blue = SkColorGetB(color);
    // This string matches the one returned by a native Mac color well.
    return [NSString stringWithFormat:@"rgb %7.5f %7.5f %7.5f 1",
                red / 255., green / 255., blue / 255.];
  }

  return base::SysUTF16ToNSString(browserAccessibility_->GetValue());
}

- (NSString*)valueDescription {
  if (![self instanceActive])
    return nil;
  if (browserAccessibility_)
    return base::SysUTF16ToNSString(browserAccessibility_->GetValue());
  return nil;
}

- (NSValue*)visibleCharacterRange {
  if (![self instanceActive])
    return nil;
  base::string16 value = browserAccessibility_->GetValue();
  return [NSValue valueWithRange:NSMakeRange(0, value.size())];
}

- (NSArray*)visibleCells {
  if (![self instanceActive])
    return nil;
  NSMutableArray* ret = [[[NSMutableArray alloc] init] autorelease];
  const std::vector<int32_t>& uniqueCellIds =
      browserAccessibility_->GetIntListAttribute(ui::AX_ATTR_UNIQUE_CELL_IDS);
  for (size_t i = 0; i < uniqueCellIds.size(); ++i) {
    int id = uniqueCellIds[i];
    BrowserAccessibility* cell =
        browserAccessibility_->manager()->GetFromID(id);
    if (cell)
      [ret addObject:ToBrowserAccessibilityCocoa(cell)];
  }
  return ret;
}

- (NSArray*)visibleChildren {
  if (![self instanceActive])
    return nil;
  NSMutableArray* ret = [[[NSMutableArray alloc] init] autorelease];
  uint32_t childCount = browserAccessibility_->PlatformChildCount();
  for (uint32_t index = 0; index < childCount; ++index) {
    BrowserAccessibilityCocoa* child = ToBrowserAccessibilityCocoa(
        browserAccessibility_->PlatformGetChild(index));
    [ret addObject:child];
  }
  return ret;
}

- (NSArray*)visibleColumns {
  if (![self instanceActive])
    return nil;
  return [self columns];
}

- (NSArray*)visibleRows {
  if (![self instanceActive])
    return nil;
  return [self rows];
}

- (NSNumber*)visited {
  if (![self instanceActive])
    return nil;
  return [NSNumber numberWithBool:
      GetState(browserAccessibility_, ui::AX_STATE_VISITED)];
}

- (id)window {
  if (![self instanceActive])
    return nil;

  BrowserAccessibilityManagerMac* manager =
      browserAccessibility_->manager()->GetRootManager()
          ->ToBrowserAccessibilityManagerMac();
  if (!manager || !manager->GetParentView())
    return nil;

  return [manager->GetParentView() window];
}

- (NSString*)methodNameForAttribute:(NSString*)attribute {
  return [attributeToMethodNameMap objectForKey:attribute];
}

- (void)swapChildren:(base::scoped_nsobject<NSMutableArray>*)other {
  children_.swap(*other);
}

- (NSString*)valueForRange:(NSRange)range {
  if (![self instanceActive])
    return nil;

  base::string16 value = browserAccessibility_->GetValue();
  if (NSMaxRange(range) > value.length())
    return nil;

  return base::SysUTF16ToNSString(value.substr(range.location, range.length));
}

- (NSAttributedString*)attributedValueForRange:(NSRange)range {
  if (![self instanceActive])
    return nil;

  // We need to get the whole text because a spelling mistake might start or end
  // outside our range.
  NSString* value = base::SysUTF16ToNSString(browserAccessibility_->GetValue());
  NSMutableAttributedString* attributedValue =
      [[[NSMutableAttributedString alloc] initWithString:value] autorelease];
  std::vector<const BrowserAccessibility*> textOnlyObjects =
      BrowserAccessibilityManager::FindTextOnlyObjectsInRange(
          *browserAccessibility_, *browserAccessibility_);
  AddMisspelledTextAttributes(textOnlyObjects, attributedValue);
  return [attributedValue attributedSubstringFromRange:range];
}

// Returns the accessibility value for the given attribute.  If the value isn't
// supported this will return nil.
- (id)accessibilityAttributeValue:(NSString*)attribute {
  if (![self instanceActive])
    return nil;

  SEL selector =
      NSSelectorFromString([self methodNameForAttribute:attribute]);
  if (selector)
    return [self performSelector:selector];

  return nil;
}

// Returns the accessibility value for the given attribute and parameter. If the
// value isn't supported this will return nil.
- (id)accessibilityAttributeValue:(NSString*)attribute
                     forParameter:(id)parameter {
  if (![self instanceActive])
    return nil;

  const std::vector<int> line_breaks =
      browserAccessibility_->GetLineStartOffsets();
  base::string16 value = browserAccessibility_->GetValue();
  int len = static_cast<int>(value.size());

  if ([attribute isEqualToString:
      NSAccessibilityStringForRangeParameterizedAttribute]) {
    return [self valueForRange:[(NSValue*)parameter rangeValue]];
  }

  if ([attribute
          isEqualToString:
              NSAccessibilityAttributedStringForRangeParameterizedAttribute]) {
    return [self attributedValueForRange:[(NSValue*)parameter rangeValue]];
  }

  if ([attribute isEqualToString:
      NSAccessibilityLineForIndexParameterizedAttribute]) {
    int index = [(NSNumber*)parameter intValue];
    for (int i = 0; i < static_cast<int>(line_breaks.size()); ++i) {
      if (line_breaks[i] > index)
        return [NSNumber numberWithInt:i];
    }
    return [NSNumber numberWithInt:static_cast<int>(line_breaks.size())];
  }

  if ([attribute isEqualToString:
      NSAccessibilityRangeForLineParameterizedAttribute]) {
    int line_index = [(NSNumber*)parameter intValue];
    int line_count = static_cast<int>(line_breaks.size()) + 1;
    if (line_index < 0 || line_index >= line_count)
      return nil;
    int start = line_index > 0 ? line_breaks[line_index - 1] : 0;
    int end = line_index < line_count - 1 ? line_breaks[line_index] : len;
    return [NSValue valueWithRange:
        NSMakeRange(start, end - start)];
  }

  if ([attribute isEqualToString:
      NSAccessibilityCellForColumnAndRowParameterizedAttribute]) {
    if ([self internalRole] != ui::AX_ROLE_TABLE &&
        [self internalRole] != ui::AX_ROLE_GRID) {
      return nil;
    }
    if (![parameter isKindOfClass:[NSArray class]])
      return nil;
    if (2 != [parameter count])
      return nil;
    NSArray* array = parameter;
    int column = [[array objectAtIndex:0] intValue];
    int row = [[array objectAtIndex:1] intValue];
    int num_columns = browserAccessibility_->GetIntAttribute(
        ui::AX_ATTR_TABLE_COLUMN_COUNT);
    int num_rows = browserAccessibility_->GetIntAttribute(
        ui::AX_ATTR_TABLE_ROW_COUNT);
    if (column < 0 || column >= num_columns ||
        row < 0 || row >= num_rows) {
      return nil;
    }
    for (size_t i = 0;
         i < browserAccessibility_->PlatformChildCount();
         ++i) {
      BrowserAccessibility* child = browserAccessibility_->PlatformGetChild(i);
      if (child->GetRole() != ui::AX_ROLE_ROW)
        continue;
      int rowIndex;
      if (!child->GetIntAttribute(
              ui::AX_ATTR_TABLE_ROW_INDEX, &rowIndex)) {
        continue;
      }
      if (rowIndex < row)
        continue;
      if (rowIndex > row)
        break;
      for (size_t j = 0;
           j < child->PlatformChildCount();
           ++j) {
        BrowserAccessibility* cell = child->PlatformGetChild(j);
        if (!cell->IsCellOrTableHeaderRole())
          continue;
        int colIndex;
        if (!cell->GetIntAttribute(
                ui::AX_ATTR_TABLE_CELL_COLUMN_INDEX,
                &colIndex)) {
          continue;
        }
        if (colIndex == column)
          return ToBrowserAccessibilityCocoa(cell);
        if (colIndex > column)
          break;
      }
    }
    return nil;
  }

  if ([attribute isEqualToString:@"AXUIElementForTextMarker"]) {
    AXPlatformPositionInstance position =
        CreatePositionFromTextMarker(parameter);
    if (!position->IsNullPosition())
      return ToBrowserAccessibilityCocoa(position->GetAnchor());

    return nil;
  }

  if ([attribute isEqualToString:@"AXTextMarkerRangeForUIElement"]) {
    AXPlatformPositionInstance startPosition =
        browserAccessibility_->CreatePositionAt(0);
    AXPlatformPositionInstance endPosition =
        startPosition->CreatePositionAtEndOfAnchor();
    AXPlatformRange range =
        AXPlatformRange(std::move(startPosition), std::move(endPosition));
    return CreateTextMarkerRange(std::move(range));
  }

  if ([attribute isEqualToString:@"AXStringForTextMarkerRange"])
    return GetTextForTextMarkerRange(parameter);

  if ([attribute isEqualToString:@"AXAttributedStringForTextMarkerRange"])
    return GetAttributedTextForTextMarkerRange(parameter);

  if ([attribute isEqualToString:@"AXNextTextMarkerForTextMarker"]) {
    AXPlatformPositionInstance position =
        CreatePositionFromTextMarker(parameter);
    if (position->IsNullPosition())
      return nil;
    return CreateTextMarker(position->CreateNextCharacterPosition());
  }

  if ([attribute isEqualToString:@"AXPreviousTextMarkerForTextMarker"]) {
    AXPlatformPositionInstance position =
        CreatePositionFromTextMarker(parameter);
    if (position->IsNullPosition())
      return nil;
    return CreateTextMarker(position->CreatePreviousCharacterPosition());
  }

  if ([attribute isEqualToString:@"AXLeftWordTextMarkerRangeForTextMarker"]) {
    AXPlatformPositionInstance endPosition =
        CreatePositionFromTextMarker(parameter);
    if (endPosition->IsNullPosition())
      return nil;

    AXPlatformPositionInstance startWordPosition =
        endPosition->CreatePreviousWordStartPosition();
    AXPlatformPositionInstance endWordPosition =
        endPosition->CreatePreviousWordEndPosition();
    AXPlatformPositionInstance startPosition =
        *startWordPosition <= *endWordPosition ? std::move(endWordPosition)
                                               : std::move(startWordPosition);
    AXPlatformRange range(std::move(startPosition), std::move(endPosition));
    return CreateTextMarkerRange(std::move(range));
  }

  if ([attribute isEqualToString:@"AXRightWordTextMarkerRangeForTextMarker"]) {
    AXPlatformPositionInstance startPosition =
        CreatePositionFromTextMarker(parameter);
    if (startPosition->IsNullPosition())
      return nil;

    AXPlatformPositionInstance endWordPosition =
        startPosition->CreateNextWordEndPosition();
    AXPlatformPositionInstance startWordPosition =
        startPosition->CreateNextWordStartPosition();
    AXPlatformPositionInstance endPosition =
        *startWordPosition <= *endWordPosition ? std::move(startWordPosition)
                                               : std::move(endWordPosition);
    AXPlatformRange range(std::move(startPosition), std::move(endPosition));
    return CreateTextMarkerRange(std::move(range));
  }

  if ([attribute isEqualToString:@"AXNextWordEndTextMarkerForTextMarker"]) {
    AXPlatformPositionInstance position =
        CreatePositionFromTextMarker(parameter);
    if (position->IsNullPosition())
      return nil;
    return CreateTextMarker(position->CreateNextWordEndPosition());
  }

  if ([attribute
          isEqualToString:@"AXPreviousWordStartTextMarkerForTextMarker"]) {
    AXPlatformPositionInstance position =
        CreatePositionFromTextMarker(parameter);
    if (position->IsNullPosition())
      return nil;
    return CreateTextMarker(position->CreatePreviousWordStartPosition());
  }

  if ([attribute isEqualToString:@"AXTextMarkerRangeForLine"]) {
    AXPlatformPositionInstance position =
        CreatePositionFromTextMarker(parameter);
    if (position->IsNullPosition())
      return nil;

    AXPlatformPositionInstance startPosition =
        position->CreatePreviousLineStartPosition();
    AXPlatformPositionInstance endPosition =
        position->CreateNextLineEndPosition();
    AXPlatformRange range(std::move(startPosition), std::move(endPosition));
    return CreateTextMarkerRange(std::move(range));
  }

  if ([attribute isEqualToString:@"AXLeftLineTextMarkerRangeForTextMarker"]) {
    AXPlatformPositionInstance endPosition =
        CreatePositionFromTextMarker(parameter);
    if (endPosition->IsNullPosition())
      return nil;

    AXPlatformPositionInstance startLinePosition =
        endPosition->CreatePreviousLineStartPosition();
    AXPlatformPositionInstance endLinePosition =
        endPosition->CreatePreviousLineEndPosition();
    AXPlatformPositionInstance startPosition =
        *startLinePosition <= *endLinePosition ? std::move(endLinePosition)
                                               : std::move(startLinePosition);
    AXPlatformRange range(std::move(startPosition), std::move(endPosition));
    return CreateTextMarkerRange(std::move(range));
  }

  if ([attribute isEqualToString:@"AXRightLineTextMarkerRangeForTextMarker"]) {
    AXPlatformPositionInstance startPosition =
        CreatePositionFromTextMarker(parameter);
    if (startPosition->IsNullPosition())
      return nil;

    AXPlatformPositionInstance startLinePosition =
        startPosition->CreateNextLineStartPosition();
    AXPlatformPositionInstance endLinePosition =
        startPosition->CreateNextLineEndPosition();
    AXPlatformPositionInstance endPosition =
        *startLinePosition <= *endLinePosition ? std::move(startLinePosition)
                                               : std::move(endLinePosition);
    AXPlatformRange range(std::move(startPosition), std::move(endPosition));
    return CreateTextMarkerRange(std::move(range));
  }

  if ([attribute isEqualToString:@"AXNextLineEndTextMarkerForTextMarker"]) {
    AXPlatformPositionInstance position =
        CreatePositionFromTextMarker(parameter);
    if (position->IsNullPosition())
      return nil;
    return CreateTextMarker(position->CreateNextLineEndPosition());
  }

  if ([attribute
          isEqualToString:@"AXPreviousLineStartTextMarkerForTextMarker"]) {
    AXPlatformPositionInstance position =
        CreatePositionFromTextMarker(parameter);
    if (position->IsNullPosition())
      return nil;
    return CreateTextMarker(position->CreatePreviousLineStartPosition());
  }

  if ([attribute isEqualToString:@"AXLengthForTextMarkerRange"]) {
    NSString* text = GetTextForTextMarkerRange(parameter);
    return [NSNumber numberWithInt:[text length]];
  }

  if ([attribute isEqualToString:
      NSAccessibilityBoundsForRangeParameterizedAttribute]) {
    if ([self internalRole] != ui::AX_ROLE_STATIC_TEXT)
      return nil;
    NSRange range = [(NSValue*)parameter rangeValue];
    gfx::Rect rect = browserAccessibility_->GetScreenBoundsForRange(
        range.location, range.length);
    NSPoint origin = NSMakePoint(rect.x(), rect.y());
    NSSize size = NSMakeSize(rect.width(), rect.height());
    NSPoint pointInScreen = [self pointInScreen:origin size:size];
    NSRect nsrect = NSMakeRect(
        pointInScreen.x, pointInScreen.y, rect.width(), rect.height());
    return [NSValue valueWithRect:nsrect];
  }

  if ([attribute isEqualToString:@"AXUIElementCountForSearchPredicate"]) {
    OneShotAccessibilityTreeSearch search(browserAccessibility_);
    if (InitializeAccessibilityTreeSearch(&search, parameter))
      return [NSNumber numberWithInt:search.CountMatches()];
    return nil;
  }

  if ([attribute isEqualToString:@"AXUIElementsForSearchPredicate"]) {
    OneShotAccessibilityTreeSearch search(browserAccessibility_);
    if (InitializeAccessibilityTreeSearch(&search, parameter)) {
      size_t count = search.CountMatches();
      NSMutableArray* result = [NSMutableArray arrayWithCapacity:count];
      for (size_t i = 0; i < count; ++i) {
        BrowserAccessibility* match = search.GetMatchAtIndex(i);
        [result addObject:ToBrowserAccessibilityCocoa(match)];
      }
      return result;
    }
    return nil;
  }

  if ([attribute isEqualToString:
           NSAccessibilityLineTextMarkerRangeForTextMarkerParameterizedAttribute]) {
    AXPlatformPositionInstance position =
        CreatePositionFromTextMarker(parameter);
    if (position->IsNullPosition())
      return nil;

    AXPlatformRange range(position->CreatePreviousLineStartPosition(),
                          position->CreateNextLineEndPosition());
    return CreateTextMarkerRange(std::move(range));
  }

  if ([attribute isEqualToString:
           NSAccessibilityBoundsForTextMarkerRangeParameterizedAttribute]) {
    BrowserAccessibility* startObject;
    BrowserAccessibility* endObject;
    int startOffset, endOffset;
    AXPlatformRange range = CreateRangeFromTextMarkerRange(parameter);
    if (range.IsNull())
      return nil;

    startObject = range.anchor()->GetAnchor();
    endObject = range.focus()->GetAnchor();
    startOffset = range.anchor()->text_offset();
    endOffset = range.focus()->text_offset();
    DCHECK(startObject && endObject);
    DCHECK_GE(startOffset, 0);
    DCHECK_GE(endOffset, 0);

    gfx::Rect rect = BrowserAccessibilityManager::GetPageBoundsForRange(
        *startObject, startOffset, *endObject, endOffset);
    NSPoint origin = NSMakePoint(rect.x(), rect.y());
    NSSize size = NSMakeSize(rect.width(), rect.height());
    NSPoint pointInScreen = [self pointInScreen:origin size:size];
    NSRect nsrect = NSMakeRect(
        pointInScreen.x, pointInScreen.y, rect.width(), rect.height());
    return [NSValue valueWithRect:nsrect];
  }

  if ([attribute isEqualToString:
           NSAccessibilityTextMarkerRangeForUnorderedTextMarkersParameterizedAttribute]) {
    if (![parameter isKindOfClass:[NSArray class]])
      return nil;

    NSArray* text_marker_array = parameter;
    if ([text_marker_array count] != 2)
      return nil;

    AXPlatformPositionInstance startPosition =
        CreatePositionFromTextMarker([text_marker_array objectAtIndex:0]);
    AXPlatformPositionInstance endPosition =
        CreatePositionFromTextMarker([text_marker_array objectAtIndex:1]);
    if (*startPosition <= *endPosition) {
      return CreateTextMarkerRange(
          AXPlatformRange(std::move(startPosition), std::move(endPosition)));
    } else {
      return CreateTextMarkerRange(
          AXPlatformRange(std::move(endPosition), std::move(startPosition)));
    }
  }

  if ([attribute isEqualToString:
           NSAccessibilityIndexForChildUIElementParameterizedAttribute]) {
    if (![parameter isKindOfClass:[BrowserAccessibilityCocoa class]])
      return nil;

    BrowserAccessibilityCocoa* childCocoaObj =
        (BrowserAccessibilityCocoa*)parameter;
    BrowserAccessibility* child = [childCocoaObj browserAccessibility];
    if (!child)
      return nil;

    if (child->PlatformGetParent() != browserAccessibility_)
      return nil;

    return @(child->GetIndexInParent());
  }

  return nil;
}

// Returns an array of parameterized attributes names that this object will
// respond to.
- (NSArray*)accessibilityParameterizedAttributeNames {
  if (![self instanceActive])
    return nil;

  // General attributes.
  NSMutableArray* ret = [NSMutableArray
      arrayWithObjects:
          @"AXUIElementForTextMarker", @"AXTextMarkerRangeForUIElement",
          @"AXLineForTextMarker", @"AXTextMarkerRangeForLine",
          @"AXStringForTextMarkerRange", @"AXTextMarkerForPosition",
          @"AXAttributedStringForTextMarkerRange",
          @"AXNextTextMarkerForTextMarker",
          @"AXPreviousTextMarkerForTextMarker",
          @"AXLeftWordTextMarkerRangeForTextMarker",
          @"AXRightWordTextMarkerRangeForTextMarker",
          @"AXLeftLineTextMarkerRangeForTextMarker",
          @"AXRightLineTextMarkerRangeForTextMarker",
          @"AXSentenceTextMarkerRangeForTextMarker",
          @"AXParagraphTextMarkerRangeForTextMarker",
          @"AXNextWordEndTextMarkerForTextMarker",
          @"AXPreviousWordStartTextMarkerForTextMarker",
          @"AXNextLineEndTextMarkerForTextMarker",
          @"AXPreviousLineStartTextMarkerForTextMarker",
          @"AXNextSentenceEndTextMarkerForTextMarker",
          @"AXPreviousSentenceStartTextMarkerForTextMarker",
          @"AXNextParagraphEndTextMarkerForTextMarker",
          @"AXPreviousParagraphStartTextMarkerForTextMarker",
          @"AXStyleTextMarkerRangeForTextMarker", @"AXLengthForTextMarkerRange",
          NSAccessibilityBoundsForTextMarkerRangeParameterizedAttribute,
          NSAccessibilityTextMarkerRangeForUnorderedTextMarkersParameterizedAttribute,
          NSAccessibilityIndexForChildUIElementParameterizedAttribute,
          NSAccessibilityBoundsForRangeParameterizedAttribute,
          NSAccessibilityStringForRangeParameterizedAttribute,
          NSAccessibilityUIElementCountForSearchPredicateParameterizedAttribute,
          NSAccessibilityUIElementsForSearchPredicateParameterizedAttribute,
          NSAccessibilityEndTextMarkerForBoundsParameterizedAttribute,
          NSAccessibilityStartTextMarkerForBoundsParameterizedAttribute,
          NSAccessibilityLineTextMarkerRangeForTextMarkerParameterizedAttribute,
          NSAccessibilitySelectTextWithCriteriaParameterizedAttribute, nil];

  if ([[self role] isEqualToString:NSAccessibilityTableRole] ||
      [[self role] isEqualToString:NSAccessibilityGridRole]) {
    [ret addObjectsFromArray:@[
      NSAccessibilityCellForColumnAndRowParameterizedAttribute
    ]];
  }

  if (browserAccessibility_->HasState(ui::AX_STATE_EDITABLE)) {
    [ret addObjectsFromArray:@[
      NSAccessibilityLineForIndexParameterizedAttribute,
      NSAccessibilityRangeForLineParameterizedAttribute,
      NSAccessibilityStringForRangeParameterizedAttribute,
      NSAccessibilityRangeForPositionParameterizedAttribute,
      NSAccessibilityRangeForIndexParameterizedAttribute,
      NSAccessibilityBoundsForRangeParameterizedAttribute,
      NSAccessibilityRTFForRangeParameterizedAttribute,
      NSAccessibilityAttributedStringForRangeParameterizedAttribute,
      NSAccessibilityStyleRangeForIndexParameterizedAttribute
    ]];
  }

  if ([self internalRole] == ui::AX_ROLE_STATIC_TEXT) {
    [ret addObjectsFromArray:@[
      NSAccessibilityBoundsForRangeParameterizedAttribute
    ]];
  }

  if ([self internalRole] == ui::AX_ROLE_ROOT_WEB_AREA ||
      [self internalRole] == ui::AX_ROLE_WEB_AREA) {
    [ret addObjectsFromArray: @[
                 NSAccessibilityTextMarkerIsValidParameterizedAttribute,
                     NSAccessibilityIndexForTextMarkerParameterizedAttribute,
                     NSAccessibilityTextMarkerForIndexParameterizedAttribute]];
  }

  return ret;
}

// Returns an array of action names that this object will respond to.
- (NSArray*)accessibilityActionNames {
  if (![self instanceActive])
    return nil;

  NSMutableArray* actions = [NSMutableArray
      arrayWithObjects:NSAccessibilityShowMenuAction,
                       NSAccessibilityScrollToVisibleAction, nil];

  // VoiceOver expects the "press" action to be first.
  if (browserAccessibility_->IsClickable())
    [actions insertObject:NSAccessibilityPressAction atIndex:0];

  if (browserAccessibility_->IsMenuRelated())
    [actions addObject:NSAccessibilityCancelAction];

  if ([self internalRole] == ui::AX_ROLE_SLIDER) {
    [actions addObjectsFromArray:@[
      NSAccessibilityIncrementAction, NSAccessibilityDecrementAction
    ]];
  }

  return actions;
}

// Returns a sub-array of values for the given attribute value, starting at
// index, with up to maxCount items.  If the given index is out of bounds,
// or there are no values for the given attribute, it will return nil.
// This method is used for querying subsets of values, without having to
// return a large set of data, such as elements with a large number of
// children.
- (NSArray*)accessibilityArrayAttributeValues:(NSString*)attribute
                                        index:(NSUInteger)index
                                     maxCount:(NSUInteger)maxCount {
  if (![self instanceActive])
    return nil;

  NSArray* fullArray = [self accessibilityAttributeValue:attribute];
  if (!fullArray)
    return nil;
  NSUInteger arrayCount = [fullArray count];
  if (index >= arrayCount)
    return nil;
  NSRange subRange;
  if ((index + maxCount) > arrayCount) {
    subRange = NSMakeRange(index, arrayCount - index);
  } else {
    subRange = NSMakeRange(index, maxCount);
  }
  return [fullArray subarrayWithRange:subRange];
}

// Returns the count of the specified accessibility array attribute.
- (NSUInteger)accessibilityArrayAttributeCount:(NSString*)attribute {
  if (![self instanceActive])
    return 0;

  NSArray* fullArray = [self accessibilityAttributeValue:attribute];
  return [fullArray count];
}

// Returns the list of accessibility attributes that this object supports.
- (NSArray*)accessibilityAttributeNames {
  if (![self instanceActive])
    return nil;

  // General attributes.
  NSMutableArray* ret = [NSMutableArray
      arrayWithObjects:NSAccessibilityAccessKeyAttribute,
                       NSAccessibilityChildrenAttribute,
                       NSAccessibilityDescriptionAttribute,
                       NSAccessibilityDOMIdentifierAttribute,
                       NSAccessibilityEnabledAttribute,
                       NSAccessibilityEndTextMarkerAttribute,
                       NSAccessibilityFocusedAttribute,
                       NSAccessibilityHelpAttribute,
                       NSAccessibilityInvalidAttribute,
                       NSAccessibilityLinkedUIElementsAttribute,
                       NSAccessibilityParentAttribute,
                       NSAccessibilityPositionAttribute,
                       NSAccessibilityRoleAttribute,
                       NSAccessibilityRoleDescriptionAttribute,
                       NSAccessibilitySelectedTextMarkerRangeAttribute,
                       NSAccessibilitySizeAttribute,
                       NSAccessibilityStartTextMarkerAttribute,
                       NSAccessibilitySubroleAttribute,
                       NSAccessibilityTitleAttribute,
                       NSAccessibilityTitleUIElementAttribute,
                       NSAccessibilityTopLevelUIElementAttribute,
                       NSAccessibilityValueAttribute,
                       NSAccessibilityVisitedAttribute,
                       NSAccessibilityWindowAttribute, nil];

  // Specific role attributes.
  NSString* role = [self role];
  NSString* subrole = [self subrole];
  if ([role isEqualToString:NSAccessibilityTableRole] ||
      [role isEqualToString:NSAccessibilityGridRole]) {
    [ret addObjectsFromArray:@[
      NSAccessibilityColumnsAttribute, NSAccessibilityVisibleColumnsAttribute,
      NSAccessibilityRowsAttribute, NSAccessibilityVisibleRowsAttribute,
      NSAccessibilityVisibleCellsAttribute, NSAccessibilityHeaderAttribute,
      NSAccessibilityColumnHeaderUIElementsAttribute,
      NSAccessibilityRowHeaderUIElementsAttribute,
      NSAccessibilityARIAColumnCountAttribute,
      NSAccessibilityARIARowCountAttribute,
    ]];
  } else if ([role isEqualToString:NSAccessibilityColumnRole]) {
    [ret addObjectsFromArray:@[
      NSAccessibilityIndexAttribute, NSAccessibilityHeaderAttribute,
      NSAccessibilityRowsAttribute, NSAccessibilityVisibleRowsAttribute
    ]];
  } else if ([role isEqualToString:NSAccessibilityCellRole]) {
    [ret addObjectsFromArray:@[
      NSAccessibilityColumnIndexRangeAttribute,
      NSAccessibilityRowIndexRangeAttribute,
      NSAccessibilityARIAColumnIndexAttribute,
      NSAccessibilityARIARowIndexAttribute,
      @"AXSortDirection",
    ]];
  } else if ([role isEqualToString:@"AXWebArea"]) {
    [ret addObjectsFromArray:@[
      @"AXLoaded", NSAccessibilityLoadingProgressAttribute
    ]];
  } else if ([role isEqualToString:NSAccessibilityTabGroupRole]) {
    [ret addObject:NSAccessibilityTabsAttribute];
  } else if ([role isEqualToString:NSAccessibilityProgressIndicatorRole] ||
             [role isEqualToString:NSAccessibilitySliderRole] ||
             [role isEqualToString:NSAccessibilityIncrementorRole] ||
             [role isEqualToString:NSAccessibilityScrollBarRole]) {
    [ret addObjectsFromArray:@[
      NSAccessibilityMaxValueAttribute, NSAccessibilityMinValueAttribute,
      NSAccessibilityValueDescriptionAttribute
    ]];
  } else if ([subrole isEqualToString:NSAccessibilityOutlineRowSubrole]) {
    [ret addObjectsFromArray:@[
      NSAccessibilityDisclosingAttribute,
      NSAccessibilityDisclosedByRowAttribute,
      NSAccessibilityDisclosureLevelAttribute,
      NSAccessibilityDisclosedRowsAttribute
    ]];
  } else if ([role isEqualToString:NSAccessibilityRowRole]) {
    if (browserAccessibility_->PlatformGetParent()) {
      base::string16 parentRole;
      browserAccessibility_->PlatformGetParent()->GetHtmlAttribute("role",
                                                                   &parentRole);
      const base::string16 treegridRole(base::ASCIIToUTF16("treegrid"));
      if (parentRole == treegridRole) {
        [ret addObjectsFromArray:@[
          NSAccessibilityDisclosingAttribute,
          NSAccessibilityDisclosedByRowAttribute,
          NSAccessibilityDisclosureLevelAttribute,
          NSAccessibilityDisclosedRowsAttribute
        ]];
      } else {
        [ret addObjectsFromArray:@[ NSAccessibilityIndexAttribute ]];
      }
    }
  } else if ([role isEqualToString:NSAccessibilityListRole]) {
    [ret addObjectsFromArray:@[
      NSAccessibilitySelectedChildrenAttribute,
      NSAccessibilityVisibleChildrenAttribute
    ]];
  }

  // Caret navigation and text selection attributes.
  if (browserAccessibility_->HasState(ui::AX_STATE_EDITABLE)) {
    [ret addObjectsFromArray:@[
      NSAccessibilityInsertionPointLineNumberAttribute,
      NSAccessibilityNumberOfCharactersAttribute,
      NSAccessibilitySelectedTextAttribute,
      NSAccessibilitySelectedTextRangeAttribute,
      NSAccessibilityVisibleCharacterRangeAttribute
    ]];
  }

  // Add the url attribute only if it has a valid url.
  if ([self url] != nil) {
    [ret addObjectsFromArray:@[ NSAccessibilityURLAttribute ]];
  }

  // Position in set and Set size
  if (browserAccessibility_->HasIntAttribute(ui::AX_ATTR_POS_IN_SET)) {
    [ret addObjectsFromArray:@[ NSAccessibilityARIAPosInSetAttribute ]];
  }
  if (browserAccessibility_->HasIntAttribute(ui::AX_ATTR_SET_SIZE)) {
    [ret addObjectsFromArray:@[ NSAccessibilityARIASetSizeAttribute ]];
  }

  // Live regions.
  if (browserAccessibility_->HasStringAttribute(
          ui::AX_ATTR_LIVE_STATUS)) {
    [ret addObjectsFromArray:@[ NSAccessibilityARIALiveAttribute ]];
  }
  if (browserAccessibility_->HasStringAttribute(
          ui::AX_ATTR_LIVE_RELEVANT)) {
    [ret addObjectsFromArray:@[ NSAccessibilityARIARelevantAttribute ]];
  }
  if (browserAccessibility_->HasBoolAttribute(
          ui::AX_ATTR_LIVE_ATOMIC)) {
    [ret addObjectsFromArray:@[ NSAccessibilityARIAAtomicAttribute ]];
  }
  if (browserAccessibility_->HasBoolAttribute(
          ui::AX_ATTR_LIVE_BUSY)) {
    [ret addObjectsFromArray:@[ NSAccessibilityARIABusyAttribute ]];
  }

  std::string dropEffect;
  if (browserAccessibility_->GetHtmlAttribute("aria-dropeffect", &dropEffect)) {
    [ret addObjectsFromArray:@[ NSAccessibilityDropEffectsAttribute ]];
  }

  std::string grabbed;
  if (browserAccessibility_->GetHtmlAttribute("aria-grabbed", &grabbed)) {
    [ret addObjectsFromArray:@[ NSAccessibilityGrabbedAttribute ]];
  }

  // Add expanded attribute only if it has expanded or collapsed state.
  if (GetState(browserAccessibility_, ui::AX_STATE_EXPANDED) ||
        GetState(browserAccessibility_, ui::AX_STATE_COLLAPSED)) {
    [ret addObjectsFromArray:@[ NSAccessibilityExpandedAttribute ]];
  }

  if (GetState(browserAccessibility_, ui::AX_STATE_VERTICAL)
      || GetState(browserAccessibility_, ui::AX_STATE_HORIZONTAL)) {
    [ret addObjectsFromArray:@[ NSAccessibilityOrientationAttribute ]];
  }

  if (browserAccessibility_->HasStringAttribute(ui::AX_ATTR_PLACEHOLDER)) {
    [ret addObjectsFromArray:@[ NSAccessibilityPlaceholderValueAttribute ]];
  }

  if (GetState(browserAccessibility_, ui::AX_STATE_REQUIRED)) {
    [ret addObjectsFromArray:@[ @"AXRequired" ]];
  }

  // Title UI Element.
  if (browserAccessibility_->HasIntListAttribute(ui::AX_ATTR_LABELLEDBY_IDS) &&
      browserAccessibility_->GetIntListAttribute(ui::AX_ATTR_LABELLEDBY_IDS)
                            .size() > 0) {
    [ret addObjectsFromArray:@[ NSAccessibilityTitleUIElementAttribute ]];
  }
  // TODO(aboxhall): expose NSAccessibilityServesAsTitleForUIElementsAttribute
  // for elements which are referred to by labelledby or are labels

  return ret;
}

// Returns the index of the child in this objects array of children.
- (NSUInteger)accessibilityGetIndexOf:(id)child {
  if (![self instanceActive])
    return 0;

  NSUInteger index = 0;
  for (BrowserAccessibilityCocoa* childToCheck in [self children]) {
    if ([child isEqual:childToCheck])
      return index;
    ++index;
  }
  return NSNotFound;
}

// Returns whether or not the specified attribute can be set by the
// accessibility API via |accessibilitySetValue:forAttribute:|.
- (BOOL)accessibilityIsAttributeSettable:(NSString*)attribute {
  if (![self instanceActive])
    return NO;

  if ([attribute isEqualToString:NSAccessibilityFocusedAttribute]) {
    if ([self internalRole] == ui::AX_ROLE_DATE_TIME)
      return NO;

    return GetState(browserAccessibility_, ui::AX_STATE_FOCUSABLE);
  }

  if ([attribute isEqualToString:NSAccessibilityValueAttribute])
    return browserAccessibility_->HasAction(ui::AX_ACTION_SET_VALUE);

  if ([attribute isEqualToString:NSAccessibilitySelectedTextRangeAttribute] &&
      browserAccessibility_->HasState(ui::AX_STATE_EDITABLE)) {
    return YES;
  }

  return NO;
}

// Returns whether or not this object should be ignored in the accessibility
// tree.
- (BOOL)accessibilityIsIgnored {
  if (![self instanceActive])
    return YES;

  return [self isIgnored];
}

// Performs the given accessibility action on the webkit accessibility object
// that backs this object.
- (void)accessibilityPerformAction:(NSString*)action {
  if (![self instanceActive])
    return;

  // TODO(dmazzoni): Support more actions.
  BrowserAccessibilityManager* manager = browserAccessibility_->manager();
  if ([action isEqualToString:NSAccessibilityPressAction]) {
    manager->DoDefaultAction(*browserAccessibility_);
  } else if ([action isEqualToString:NSAccessibilityShowMenuAction]) {
    manager->ShowContextMenu(*browserAccessibility_);
  } else if ([action isEqualToString:NSAccessibilityScrollToVisibleAction]) {
    manager->ScrollToMakeVisible(
        *browserAccessibility_, gfx::Rect());
  } else if ([action isEqualToString:NSAccessibilityIncrementAction]) {
    manager->Increment(*browserAccessibility_);
  } else if ([action isEqualToString:NSAccessibilityDecrementAction]) {
    manager->Decrement(*browserAccessibility_);
  }
}

// Returns the description of the given action.
- (NSString*)accessibilityActionDescription:(NSString*)action {
  if (![self instanceActive])
    return nil;

  return NSAccessibilityActionDescription(action);
}

// Sets an override value for a specific accessibility attribute.
// This class does not support this.
- (BOOL)accessibilitySetOverrideValue:(id)value
                         forAttribute:(NSString*)attribute {
  if (![self instanceActive])
    return NO;
  return NO;
}

// Sets the value for an accessibility attribute via the accessibility API.
- (void)accessibilitySetValue:(id)value forAttribute:(NSString*)attribute {
  if (![self instanceActive])
    return;

  if ([attribute isEqualToString:NSAccessibilityFocusedAttribute]) {
    BrowserAccessibilityManager* manager = browserAccessibility_->manager();
    NSNumber* focusedNumber = value;
    BOOL focused = [focusedNumber intValue];
    if (focused)
      manager->SetFocus(*browserAccessibility_);
  }
  if ([attribute isEqualToString:NSAccessibilitySelectedTextRangeAttribute]) {
    NSRange range = [(NSValue*)value rangeValue];
    BrowserAccessibilityManager* manager = browserAccessibility_->manager();
    manager->SetSelection(AXPlatformRange(
        browserAccessibility_->CreatePositionAt(range.location),
        browserAccessibility_->CreatePositionAt(NSMaxRange(range))));
  }
}

// Returns the deepest accessibility child that should not be ignored.
// It is assumed that the hit test has been narrowed down to this object
// or one of its children, so this will never return nil unless this
// object is invalid.
- (id)accessibilityHitTest:(NSPoint)point {
  if (![self instanceActive])
    return nil;

  BrowserAccessibilityManager* manager = browserAccessibility_->manager();
  gfx::Point screen_point(point.x, point.y);
  screen_point += manager->GetViewBounds().OffsetFromOrigin();

  BrowserAccessibility* hit = manager->CachingAsyncHitTest(screen_point);
  if (!hit)
    return nil;

  return NSAccessibilityUnignoredAncestor(ToBrowserAccessibilityCocoa(hit));
}

- (BOOL)isEqual:(id)object {
  if (![object isKindOfClass:[BrowserAccessibilityCocoa class]])
    return NO;
  return ([self hash] == [object hash]);
}

- (NSUInteger)hash {
  // Potentially called during dealloc.
  if (![self instanceActive])
    return [super hash];
  return browserAccessibility_->GetId();
}

- (BOOL)accessibilityNotifiesWhenDestroyed {
  // Indicate that BrowserAccessibilityCocoa will post a notification when it's
  // destroyed (see -detach). This allows VoiceOver to do some internal things
  // more efficiently.
  return YES;
}

@end
