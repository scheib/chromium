/*
 * Copyright (C) 2008, 2009, 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Nuanti Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef AXObjectImpl_h
#define AXObjectImpl_h

#include "core/dom/AXObject.h"
#include "core/editing/VisiblePosition.h"
#include "core/editing/markers/DocumentMarker.h"
#include "core/inspector/protocol/Accessibility.h"
#include "modules/ModulesExport.h"
#include "platform/geometry/FloatQuad.h"
#include "platform/geometry/LayoutRect.h"
#include "platform/graphics/Color.h"
#include "platform/wtf/Forward.h"
#include "platform/wtf/Vector.h"

class SkMatrix44;

namespace blink {

class AXObjectImpl;
class AXObjectCacheImpl;
class Element;
class FrameView;
class IntPoint;
class Node;
class LayoutObject;
class ScrollableArea;

enum class AOMBooleanProperty;
enum class AOMStringProperty;

typedef unsigned AXID;

enum AccessibilityRole {
  kUnknownRole = 0,  // Not mapped in platform APIs, generally indicates a bug
  kAbbrRole,         // No mapping to ARIA role.
  kAlertDialogRole,
  kAlertRole,
  kAnchorRole,      // No mapping to ARIA role.
  kAnnotationRole,  // No mapping to ARIA role.
  kApplicationRole,
  kArticleRole,
  kAudioRole,  // No mapping to ARIA role.
  kBannerRole,
  kBlockquoteRole,     // No mapping to ARIA role.
  kBusyIndicatorRole,  // No mapping to ARIA role.
  kButtonRole,
  kCanvasRole,   // No mapping to ARIA role.
  kCaptionRole,  // No mapping to ARIA role.
  kCellRole,
  kCheckBoxRole,
  kColorWellRole,  // No mapping to ARIA role.
  kColumnHeaderRole,
  kColumnRole,  // No mapping to ARIA role.
  kComboBoxRole,
  kComplementaryRole,
  kContentInfoRole,
  kDateRole,      // No mapping to ARIA role.
  kDateTimeRole,  // No mapping to ARIA role.
  kDefinitionRole,
  kDescriptionListDetailRole,  // No mapping to ARIA role.
  kDescriptionListRole,        // No mapping to ARIA role.
  kDescriptionListTermRole,    // No mapping to ARIA role.
  kDetailsRole,                // No mapping to ARIA role.
  kDialogRole,
  kDirectoryRole,
  kDisclosureTriangleRole,  // No mapping to ARIA role.
  kDocumentRole,
  kEmbeddedObjectRole,  // No mapping to ARIA role.
  kFeedRole,
  kFigcaptionRole,  // No mapping to ARIA role.
  kFigureRole,
  kFooterRole,
  kFormRole,
  kGenericContainerRole,  // No role was defined for this container
  kGridRole,
  kGroupRole,
  kHeadingRole,
  kIframePresentationalRole,  // No mapping to ARIA role.
  kIframeRole,                // No mapping to ARIA role.
  kIgnoredRole,               // No mapping to ARIA role.
  kImageMapLinkRole,          // No mapping to ARIA role.
  kImageMapRole,              // No mapping to ARIA role.
  kImageRole,
  kInlineTextBoxRole,  // No mapping to ARIA role.
  kInputTimeRole,      // No mapping to ARIA role.
  kLabelRole,
  kLegendRole,     // No mapping to ARIA role.
  kLineBreakRole,  // No mapping to ARIA role.
  kLinkRole,
  kListBoxOptionRole,
  kListBoxRole,
  kListItemRole,
  kListMarkerRole,  // No mapping to ARIA role.
  kListRole,
  kLogRole,
  kMainRole,
  kMarkRole,  // No mapping to ARIA role.
  kMarqueeRole,
  kMathRole,
  kMenuBarRole,
  kMenuButtonRole,
  kMenuItemRole,
  kMenuItemCheckBoxRole,
  kMenuItemRadioRole,
  kMenuListOptionRole,
  kMenuListPopupRole,
  kMenuRole,
  kMeterRole,
  kNavigationRole,
  kNoneRole,  // ARIA role of "none"
  kNoteRole,
  kOutlineRole,    // No mapping to ARIA role.
  kParagraphRole,  // No mapping to ARIA role.
  kPopUpButtonRole,
  kPreRole,  // No mapping to ARIA role.
  kPresentationalRole,
  kProgressIndicatorRole,
  kRadioButtonRole,
  kRadioGroupRole,
  kRegionRole,
  kRootWebAreaRole,  // No mapping to ARIA role.
  kRowHeaderRole,
  kRowRole,
  kRubyRole,        // No mapping to ARIA role.
  kRulerRole,       // No mapping to ARIA role.
  kSVGRootRole,     // No mapping to ARIA role.
  kScrollAreaRole,  // No mapping to ARIA role.
  kScrollBarRole,
  kSeamlessWebAreaRole,  // No mapping to ARIA role.
  kSearchRole,
  kSearchBoxRole,
  kSliderRole,
  kSliderThumbRole,     // No mapping to ARIA role.
  kSpinButtonPartRole,  // No mapping to ARIA role.
  kSpinButtonRole,
  kSplitterRole,
  kStaticTextRole,  // No mapping to ARIA role.
  kStatusRole,
  kSwitchRole,
  kTabGroupRole,  // No mapping to ARIA role.
  kTabListRole,
  kTabPanelRole,
  kTabRole,
  kTableHeaderContainerRole,  // No mapping to ARIA role.
  kTableRole,
  kTermRole,
  kTextFieldRole,
  kTimeRole,  // No mapping to ARIA role.
  kTimerRole,
  kToggleButtonRole,
  kToolbarRole,
  kTreeGridRole,
  kTreeItemRole,
  kTreeRole,
  kUserInterfaceTooltipRole,
  kVideoRole,    // No mapping to ARIA role.
  kWebAreaRole,  // No mapping to ARIA role.
  kWindowRole,   // No mapping to ARIA role.
  kNumRoles
};

enum AccessibilityTextSource {
  kAlternativeText,
  kChildrenText,
  kSummaryText,
  kHelpText,
  kVisibleText,
  kTitleTagText,
  kPlaceholderText,
  kLabelByElementText,
};

enum AccessibilityState {
  kAXBusyState,
  kAXCheckedState,
  kAXEnabledState,
  kAXExpandedState,
  kAXFocusableState,
  kAXFocusedState,
  kAXHaspopupState,
  kAXHoveredState,
  kAXInvisibleState,
  kAXLinkedState,
  kAXMultilineState,
  kAXMultiselectableState,
  kAXOffscreenState,
  kAXPressedState,
  kAXProtectedState,
  kAXReadonlyState,
  kAXRequiredState,
  kAXSelectableState,
  kAXSelectedState,
  kAXVerticalState,
  kAXVisitedState
};

class AccessibilityText final
    : public GarbageCollectedFinalized<AccessibilityText> {
  WTF_MAKE_NONCOPYABLE(AccessibilityText);

 public:
  DEFINE_INLINE_TRACE() { visitor->Trace(text_element_); }

 private:
  AccessibilityText(const String& text,
                    const AccessibilityTextSource& source,
                    AXObjectImpl* element)
      : text_(text), text_element_(element) {}

  String text_;
  Member<AXObjectImpl> text_element_;
};

enum AccessibilityOrientation {
  kAccessibilityOrientationUndefined = 0,
  kAccessibilityOrientationVertical,
  kAccessibilityOrientationHorizontal,
};

enum AXObjectInclusion {
  kIncludeObject,
  kIgnoreObject,
  kDefaultBehavior,
};

enum class AXDefaultActionVerb {
  kNone = 0,
  kActivate,
  kCheck,
  kClick,
  kJump,
  kOpen,
  kPress,
  kSelect,
  kUncheck
};

enum AccessibilityButtonState {
  kButtonStateOff = 0,
  kButtonStateOn,
  kButtonStateMixed,
};

enum AccessibilityTextDirection {
  kAccessibilityTextDirectionLTR,
  kAccessibilityTextDirectionRTL,
  kAccessibilityTextDirectionTTB,
  kAccessibilityTextDirectionBTT
};

enum SortDirection {
  kSortDirectionUndefined = 0,
  kSortDirectionNone,
  kSortDirectionAscending,
  kSortDirectionDescending,
  kSortDirectionOther
};

enum AccessibilityExpanded {
  kExpandedUndefined = 0,
  kExpandedCollapsed,
  kExpandedExpanded,
};

enum AccessibilityOptionalBool {
  kOptionalBoolUndefined = 0,
  kOptionalBoolTrue,
  kOptionalBoolFalse
};

enum AriaCurrentState {
  kAriaCurrentStateUndefined = 0,
  kAriaCurrentStateFalse,
  kAriaCurrentStateTrue,
  kAriaCurrentStatePage,
  kAriaCurrentStateStep,
  kAriaCurrentStateLocation,
  kAriaCurrentStateDate,
  kAriaCurrentStateTime
};

enum InvalidState {
  kInvalidStateUndefined = 0,
  kInvalidStateFalse,
  kInvalidStateTrue,
  kInvalidStateSpelling,
  kInvalidStateGrammar,
  kInvalidStateOther
};

enum TextStyle {
  kTextStyleNone = 0,
  kTextStyleBold = 1 << 0,
  kTextStyleItalic = 1 << 1,
  kTextStyleUnderline = 1 << 2,
  kTextStyleLineThrough = 1 << 3
};

enum TextUnderElementMode {
  kTextUnderElementAll,
  kTextUnderElementAny  // If the text is unimportant, just whether or not it's
                        // present
};

enum class AXBoolAttribute {};

enum class AXStringAttribute {
  kAriaKeyShortcuts,
  kAriaRoleDescription,
};

enum class AXObjectAttribute {
  kAriaActiveDescendant,
  kAriaErrorMessage,
};

enum class AXObjectVectorAttribute {
  kAriaControls,
  kAriaDetails,
  kAriaFlowTo,
};

class AXSparseAttributeClient {
 public:
  virtual void AddBoolAttribute(AXBoolAttribute, bool) = 0;
  virtual void AddStringAttribute(AXStringAttribute, const String&) = 0;
  virtual void AddObjectAttribute(AXObjectAttribute, AXObjectImpl&) = 0;
  virtual void AddObjectVectorAttribute(AXObjectVectorAttribute,
                                        HeapVector<Member<AXObjectImpl>>&) = 0;
};

// The source of the accessible name of an element. This is needed
// because on some platforms this determines how the accessible name
// is exposed.
enum AXNameFrom {
  kAXNameFromUninitialized = -1,
  kAXNameFromAttribute = 0,
  kAXNameFromAttributeExplicitlyEmpty,
  kAXNameFromCaption,
  kAXNameFromContents,
  kAXNameFromPlaceholder,
  kAXNameFromRelatedElement,
  kAXNameFromValue,
  kAXNameFromTitle,
};

// The potential native HTML-based text (name, description or placeholder)
// sources for an element.  See
// http://rawgit.com/w3c/aria/master/html-aam/html-aam.html#accessible-name-and-description-calculation
enum AXTextFromNativeHTML {
  kAXTextFromNativeHTMLUninitialized = -1,
  kAXTextFromNativeHTMLFigcaption,
  kAXTextFromNativeHTMLLabel,
  kAXTextFromNativeHTMLLabelFor,
  kAXTextFromNativeHTMLLabelWrapped,
  kAXTextFromNativeHTMLLegend,
  kAXTextFromNativeHTMLTableCaption,
  kAXTextFromNativeHTMLTitleElement,
};

// The source of the accessible description of an element. This is needed
// because on some platforms this determines how the accessible description
// is exposed.
enum AXDescriptionFrom {
  kAXDescriptionFromUninitialized = -1,
  kAXDescriptionFromAttribute = 0,
  kAXDescriptionFromContents,
  kAXDescriptionFromRelatedElement,
};

enum AXIgnoredReason {
  kAXActiveModalDialog,
  kAXAncestorDisallowsChild,
  kAXAncestorIsLeafNode,
  kAXAriaHidden,
  kAXAriaHiddenRoot,
  kAXEmptyAlt,
  kAXEmptyText,
  kAXInert,
  kAXInheritsPresentation,
  kAXLabelContainer,
  kAXLabelFor,
  kAXNotRendered,
  kAXNotVisible,
  kAXPresentationalRole,
  kAXProbablyPresentational,
  kAXStaticTextUsedAsNameFor,
  kAXUninteresting
};

class IgnoredReason {
  DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();

 public:
  AXIgnoredReason reason;
  Member<const AXObjectImpl> related_object;

  explicit IgnoredReason(AXIgnoredReason reason)
      : reason(reason), related_object(nullptr) {}

  IgnoredReason(AXIgnoredReason r, const AXObjectImpl* obj)
      : reason(r), related_object(obj) {}

  DEFINE_INLINE_TRACE() { visitor->Trace(related_object); }
};

class NameSourceRelatedObject
    : public GarbageCollectedFinalized<NameSourceRelatedObject> {
  WTF_MAKE_NONCOPYABLE(NameSourceRelatedObject);

 public:
  WeakMember<AXObjectImpl> object;
  String text;

  NameSourceRelatedObject(AXObjectImpl* object, String text)
      : object(object), text(text) {}

  DEFINE_INLINE_TRACE() { visitor->Trace(object); }
};

typedef HeapVector<Member<NameSourceRelatedObject>> AXRelatedObjectVector;
class NameSource {
  DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();

 public:
  String text;
  bool superseded = false;
  bool invalid = false;
  AXNameFrom type = kAXNameFromUninitialized;
  const QualifiedName& attribute;
  AtomicString attribute_value;
  AXTextFromNativeHTML native_source = kAXTextFromNativeHTMLUninitialized;
  AXRelatedObjectVector related_objects;

  NameSource(bool superseded, const QualifiedName& attr)
      : superseded(superseded), attribute(attr) {}

  explicit NameSource(bool superseded)
      : superseded(superseded), attribute(QualifiedName::Null()) {}

  DEFINE_INLINE_TRACE() { visitor->Trace(related_objects); }
};

class DescriptionSource {
  DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();

 public:
  String text;
  bool superseded = false;
  bool invalid = false;
  AXDescriptionFrom type = kAXDescriptionFromUninitialized;
  const QualifiedName& attribute;
  AtomicString attribute_value;
  AXTextFromNativeHTML native_source = kAXTextFromNativeHTMLUninitialized;
  AXRelatedObjectVector related_objects;

  DescriptionSource(bool superseded, const QualifiedName& attr)
      : superseded(superseded), attribute(attr) {}

  explicit DescriptionSource(bool superseded)
      : superseded(superseded), attribute(QualifiedName::Null()) {}

  DEFINE_INLINE_TRACE() { visitor->Trace(related_objects); }
};

}  // namespace blink

WTF_ALLOW_INIT_WITH_MEM_FUNCTIONS(blink::IgnoredReason);
WTF_ALLOW_INIT_WITH_MEM_FUNCTIONS(blink::NameSource);
WTF_ALLOW_INIT_WITH_MEM_FUNCTIONS(blink::DescriptionSource);

namespace blink {

class MODULES_EXPORT AXObjectImpl
    : public GarbageCollectedFinalized<AXObjectImpl>,
      public AXObject {
  WTF_MAKE_NONCOPYABLE(AXObjectImpl);

 public:
  typedef HeapVector<Member<AXObjectImpl>> AXObjectVector;

  struct AXRange {
    DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();
    // The deepest descendant in which the range starts.
    // (nullptr means the current object.)
    Persistent<AXObjectImpl> anchor_object;
    // The number of characters and child objects in the anchor object
    // before the range starts.
    int anchor_offset;
    // When the same character offset could correspond to two possible
    // cursor positions, upstream means it's on the previous line rather
    // than the next line.
    TextAffinity anchor_affinity;

    // The deepest descendant in which the range ends.
    // (nullptr means the current object.)
    Persistent<AXObjectImpl> focus_object;
    // The number of characters and child objects in the focus object
    // before the range ends.
    int focus_offset;
    // When the same character offset could correspond to two possible
    // cursor positions, upstream means it's on the previous line rather
    // than the next line.
    TextAffinity focus_affinity;

    AXRange()
        : anchor_object(nullptr),
          anchor_offset(-1),
          anchor_affinity(TextAffinity::kUpstream),
          focus_object(nullptr),
          focus_offset(-1),
          focus_affinity(TextAffinity::kDownstream) {}

    AXRange(int start_offset, int end_offset)
        : anchor_object(nullptr),
          anchor_offset(start_offset),
          anchor_affinity(TextAffinity::kUpstream),
          focus_object(nullptr),
          focus_offset(end_offset),
          focus_affinity(TextAffinity::kDownstream) {}

    AXRange(AXObjectImpl* anchor_object,
            int anchor_offset,
            TextAffinity anchor_affinity,
            AXObjectImpl* focus_object,
            int focus_offset,
            TextAffinity focus_affinity)
        : anchor_object(anchor_object),
          anchor_offset(anchor_offset),
          anchor_affinity(anchor_affinity),
          focus_object(focus_object),
          focus_offset(focus_offset),
          focus_affinity(focus_affinity) {}

    bool IsValid() const {
      return ((anchor_object && focus_object) ||
              (!anchor_object && !focus_object)) &&
             anchor_offset >= 0 && focus_offset >= 0;
    }

    // Determines if the range only refers to text offsets under the current
    // object.
    bool IsSimple() const {
      return anchor_object == focus_object || !anchor_object || !focus_object;
    }
  };

 protected:
  AXObjectImpl(AXObjectCacheImpl&);

 public:
  virtual ~AXObjectImpl();
  DECLARE_VIRTUAL_TRACE();

  static unsigned NumberOfLiveAXObjects() { return number_of_live_ax_objects_; }

  // After constructing an AXObjectImpl, it must be given a
  // unique ID, then added to AXObjectCacheImpl, and finally init() must
  // be called last.
  void SetAXObjectID(AXID ax_object_id) { id_ = ax_object_id; }
  virtual void Init() {}

  // When the corresponding WebCore object that this AXObjectImpl
  // wraps is deleted, it must be detached.
  virtual void Detach();
  virtual bool IsDetached() const;

  // If the parent of this object is known, this can be faster than using
  // computeParent().
  virtual void SetParent(AXObjectImpl* parent) { parent_ = parent; }

  // The AXObjectCacheImpl that owns this object, and its unique ID within this
  // cache.
  AXObjectCacheImpl& AxObjectCache() const {
    DCHECK(ax_object_cache_);
    return *ax_object_cache_;
  }

  AXID AxObjectID() const { return id_; }

  // Wrappers that retrieve either an Accessibility Object Model property,
  // or the equivalent ARIA attribute, in that order.
  const AtomicString& GetAOMPropertyOrARIAAttribute(AOMStringProperty) const;
  bool HasAOMPropertyOrARIAAttribute(AOMBooleanProperty, bool& result) const;
  bool AOMPropertyOrARIAAttributeIsTrue(AOMBooleanProperty) const;
  bool AOMPropertyOrARIAAttributeIsFalse(AOMBooleanProperty) const;

  virtual void GetSparseAXAttributes(AXSparseAttributeClient&) const {}

  // Determine subclass type.
  virtual bool IsAXNodeObject() const { return false; }
  virtual bool IsAXLayoutObject() const { return false; }
  virtual bool IsAXInlineTextBox() const { return false; }
  virtual bool IsAXListBox() const { return false; }
  virtual bool IsAXListBoxOption() const { return false; }
  virtual bool IsAXRadioInput() const { return false; }
  virtual bool IsAXSVGRoot() const { return false; }

  // Check object role or purpose.
  virtual AccessibilityRole RoleValue() const { return role_; }
  bool IsARIATextControl() const;
  virtual bool IsARIATreeGridRow() const { return false; }
  virtual bool IsAXTable() const { return false; }
  virtual bool IsAnchor() const { return false; }
  bool IsButton() const;
  bool IsCheckable() const;
  bool IsCanvas() const { return RoleValue() == kCanvasRole; }
  bool IsCheckbox() const { return RoleValue() == kCheckBoxRole; }
  bool IsCheckboxOrRadio() const { return IsCheckbox() || IsRadioButton(); }
  bool IsColorWell() const { return RoleValue() == kColorWellRole; }
  bool IsComboBox() const { return RoleValue() == kComboBoxRole; }
  virtual bool IsControl() const { return false; }
  virtual bool IsDataTable() const { return false; }
  virtual bool IsEmbeddedObject() const { return false; }
  virtual bool IsFieldset() const { return false; }
  virtual bool IsHeading() const { return false; }
  virtual bool IsImage() const { return false; }
  virtual bool IsImageMapLink() const { return false; }
  virtual bool IsInputImage() const { return false; }
  bool IsLandmarkRelated() const;
  virtual bool IsLink() const { return false; }
  virtual bool IsInPageLinkTarget() const { return false; }
  virtual bool IsList() const { return false; }
  virtual bool IsMenu() const { return false; }
  virtual bool IsMenuButton() const { return false; }
  virtual bool IsMenuList() const { return false; }
  virtual bool IsMenuListOption() const { return false; }
  virtual bool IsMenuListPopup() const { return false; }
  bool IsMenuRelated() const;
  virtual bool IsMeter() const { return false; }
  virtual bool IsMockObject() const { return false; }
  virtual bool IsNativeSpinButton() const { return false; }
  virtual bool IsNativeTextControl() const {
    return false;
  }  // input or textarea
  virtual bool IsNonNativeTextControl() const {
    return false;
  }  // contenteditable or role=textbox
  virtual bool IsPasswordField() const { return false; }
  virtual bool IsPasswordFieldAndShouldHideValue() const;
  bool IsPresentational() const {
    return RoleValue() == kNoneRole || RoleValue() == kPresentationalRole;
  }
  virtual bool IsProgressIndicator() const { return false; }
  bool IsRadioButton() const { return RoleValue() == kRadioButtonRole; }
  bool IsRange() const {
    return RoleValue() == kProgressIndicatorRole ||
           RoleValue() == kScrollBarRole || RoleValue() == kSliderRole ||
           RoleValue() == kSpinButtonRole;
  }
  bool IsScrollbar() const { return RoleValue() == kScrollBarRole; }
  virtual bool IsSlider() const { return false; }
  virtual bool IsNativeSlider() const { return false; }
  virtual bool IsSpinButton() const { return RoleValue() == kSpinButtonRole; }
  virtual bool IsSpinButtonPart() const { return false; }
  bool IsTabItem() const { return RoleValue() == kTabRole; }
  virtual bool IsTableCell() const { return false; }
  virtual bool IsTableRow() const { return false; }
  virtual bool IsTextControl() const { return false; }
  virtual bool IsTableCol() const { return false; }
  bool IsTree() const { return RoleValue() == kTreeRole; }
  bool IsWebArea() const { return RoleValue() == kWebAreaRole; }

  // Check object state.
  virtual bool IsClickable() const;
  virtual bool IsCollapsed() const { return false; }
  virtual bool IsEnabled() const { return false; }
  virtual AccessibilityExpanded IsExpanded() const {
    return kExpandedUndefined;
  }
  virtual bool IsFocused() const { return false; }
  virtual bool IsHovered() const { return false; }
  virtual bool IsLinked() const { return false; }
  virtual bool IsLoaded() const { return false; }
  virtual bool IsModal() const { return false; }
  virtual bool IsMultiSelectable() const { return false; }
  virtual bool IsOffScreen() const { return false; }
  virtual bool IsPressed() const { return false; }
  virtual bool IsReadOnly() const { return false; }
  virtual bool IsRequired() const { return false; }
  virtual bool IsSelected() const { return false; }
  virtual bool IsSelectedOptionActive() const { return false; }
  virtual bool IsVisible() const { return true; }
  virtual bool IsVisited() const { return false; }

  // Check whether certain properties can be modified.
  virtual bool CanSetFocusAttribute() const { return false; }
  virtual bool CanSetValueAttribute() const { return false; }
  virtual bool CanSetSelectedAttribute() const { return false; }

  // Whether objects are ignored, i.e. not included in the tree.
  bool AccessibilityIsIgnored() const;
  typedef HeapVector<IgnoredReason> IgnoredReasons;
  virtual bool ComputeAccessibilityIsIgnored(IgnoredReasons* = nullptr) const {
    return true;
  }
  bool AccessibilityIsIgnoredByDefault(IgnoredReasons* = nullptr) const;
  AXObjectInclusion AccessibilityPlatformIncludesObject() const;
  virtual AXObjectInclusion DefaultObjectInclusion(
      IgnoredReasons* = nullptr) const;
  bool IsInertOrAriaHidden() const;
  const AXObjectImpl* AriaHiddenRoot() const;
  bool ComputeIsInertOrAriaHidden(IgnoredReasons* = nullptr) const;
  bool IsDescendantOfLeafNode() const;
  AXObjectImpl* LeafNodeAncestor() const;
  bool IsDescendantOfDisabledNode() const;
  const AXObjectImpl* DisabledAncestor() const;
  bool LastKnownIsIgnoredValue();
  void SetLastKnownIsIgnoredValue(bool);
  bool HasInheritedPresentationalRole() const;
  bool IsPresentationalChild() const;
  bool AncestorExposesActiveDescendant() const;
  bool ComputeAncestorExposesActiveDescendant() const;

  //
  // Accessible name calculation
  //

  // Retrieves the accessible name of the object, an enum indicating where the
  // name was derived from, and a list of objects that were used to derive the
  // name, if any.
  virtual String GetName(AXNameFrom&, AXObjectVector* name_objects) const;

  typedef HeapVector<NameSource> NameSources;
  // Retrieves the accessible name of the object and a list of all potential
  // sources for the name, indicating which were used.
  virtual String GetName(NameSources*) const;

  typedef HeapVector<DescriptionSource> DescriptionSources;
  // Takes the result of nameFrom from calling |name|, above, and retrieves the
  // accessible description of the object, which is secondary to |name|, an enum
  // indicating where the description was derived from, and a list of objects
  // that were used to derive the description, if any.
  virtual String Description(AXNameFrom,
                             AXDescriptionFrom&,
                             AXObjectVector* description_objects) const {
    return String();
  }

  // Same as above, but returns a list of all potential sources for the
  // description, indicating which were used.
  virtual String Description(AXNameFrom,
                             AXDescriptionFrom&,
                             DescriptionSources*,
                             AXRelatedObjectVector*) const {
    return String();
  }

  // Takes the result of nameFrom and descriptionFrom from calling |name| and
  // |description|, above, and retrieves the placeholder of the object, if
  // present and if it wasn't already exposed by one of the two functions above.
  virtual String Placeholder(AXNameFrom) const { return String(); }

  // Internal functions used by name and description, above.
  typedef HeapHashSet<Member<const AXObjectImpl>> AXObjectSet;
  virtual String TextAlternative(bool recursive,
                                 bool in_aria_labelled_by_traversal,
                                 AXObjectSet& visited,
                                 AXNameFrom& name_from,
                                 AXRelatedObjectVector* related_objects,
                                 NameSources* name_sources) const {
    return String();
  }
  virtual String TextFromDescendants(AXObjectSet& visited,
                                     bool recursive) const {
    return String();
  }

  // Returns result of Accessible Name Calculation algorithm.
  // This is a simpler high-level interface to |name| used by Inspector.
  String ComputedName() const;

  // Internal function used to determine whether the result of calling |name| on
  // this object would return text that came from the an HTML label element or
  // not. This is intended to be faster than calling |name| or
  // |textAlternative|, and without side effects (it won't call
  // axObjectCache->getOrCreate).
  virtual bool NameFromLabelElement() const { return false; }

  //
  // Properties of static elements.
  //

  virtual const AtomicString& AccessKey() const { return g_null_atom; }
  RGBA32 BackgroundColor() const;
  virtual RGBA32 ComputeBackgroundColor() const { return Color::kTransparent; }
  virtual RGBA32 GetColor() const { return Color::kBlack; }
  // Used by objects of role ColorWellRole.
  virtual RGBA32 ColorValue() const { return Color::kTransparent; }
  virtual bool CanvasHasFallbackContent() const { return false; }
  virtual String FontFamily() const { return g_null_atom; }
  // Font size is in pixels.
  virtual float FontSize() const { return 0.0f; }
  // Value should be 1-based. 0 means not supported.
  virtual int HeadingLevel() const { return 0; }
  // Value should be 1-based. 0 means not supported.
  virtual unsigned HierarchicalLevel() const { return 0; }
  // Return the content of an image or canvas as an image data url in
  // PNG format. If |maxSize| is not empty and if the image is larger than
  // those dimensions, the image will be resized proportionally first to fit.
  virtual String ImageDataUrl(const IntSize& max_size) const {
    return g_null_atom;
  }
  virtual AXObjectImpl* InPageLinkTarget() const { return nullptr; }
  virtual AccessibilityOrientation Orientation() const;
  virtual String GetText() const { return String(); }
  virtual AccessibilityTextDirection GetTextDirection() const {
    return kAccessibilityTextDirectionLTR;
  }
  virtual int TextLength() const { return 0; }
  virtual TextStyle GetTextStyle() const { return kTextStyleNone; }
  virtual AXObjectVector RadioButtonsInGroup() const {
    return AXObjectVector();
  }
  virtual KURL Url() const { return KURL(); }

  // Load inline text boxes for just this node, even if
  // settings->inlineTextBoxAccessibilityEnabled() is false.
  virtual void LoadInlineTextBoxes() {}

  // Walk the AXObjects on the same line. This is supported on any
  // object type but primarily intended to be used for inline text boxes.
  virtual AXObjectImpl* NextOnLine() const { return nullptr; }
  virtual AXObjectImpl* PreviousOnLine() const { return nullptr; }

  // For all node objects. The start and end character offset of each
  // marker, such as spelling or grammar error.
  virtual void Markers(Vector<DocumentMarker::MarkerType>&,
                       Vector<AXRange>&) const {}
  // For an inline text box.
  // The integer horizontal pixel offset of each character in the string;
  // negative values for RTL.
  virtual void TextCharacterOffsets(Vector<int>&) const {}
  // The start and end character offset of each word in the object's text.
  virtual void GetWordBoundaries(Vector<AXRange>&) const {}

  // Properties of interactive elements.
  AXDefaultActionVerb Action() const;
  AccessibilityButtonState CheckedState() const;
  virtual AriaCurrentState GetAriaCurrentState() const {
    return kAriaCurrentStateUndefined;
  }
  virtual InvalidState GetInvalidState() const {
    return kInvalidStateUndefined;
  }
  // Only used when invalidState() returns InvalidStateOther.
  virtual String AriaInvalidValue() const { return String(); }
  virtual String ValueDescription() const { return String(); }
  virtual float ValueForRange() const { return 0.0f; }
  virtual float MaxValueForRange() const { return 0.0f; }
  virtual float MinValueForRange() const { return 0.0f; }
  virtual String StringValue() const { return String(); }

  // ARIA attributes.
  virtual AXObjectImpl* ActiveDescendant() { return nullptr; }
  virtual String AriaAutoComplete() const { return String(); }
  virtual void AriaOwnsElements(AXObjectVector& owns) const {}
  virtual void AriaDescribedbyElements(AXObjectVector&) const {}
  virtual void AriaLabelledbyElements(AXObjectVector&) const {}
  virtual bool AriaHasPopup() const { return false; }
  virtual bool IsEditable() const { return false; }
  bool IsMultiline() const;
  virtual bool IsRichlyEditable() const { return false; }
  bool AriaPressedIsPresent() const;
  virtual AccessibilityRole AriaRoleAttribute() const { return kUnknownRole; }
  virtual bool AriaRoleHasPresentationalChildren() const { return false; }
  virtual AXObjectImpl* AncestorForWhichThisIsAPresentationalChild() const {
    return 0;
  }
  bool SupportsActiveDescendant() const;
  bool SupportsARIAAttributes() const;
  virtual bool SupportsARIADragging() const { return false; }
  virtual bool SupportsARIADropping() const { return false; }
  virtual bool SupportsARIAFlowTo() const { return false; }
  virtual bool SupportsARIAOwns() const { return false; }
  bool SupportsRangeValue() const;
  virtual SortDirection GetSortDirection() const {
    return kSortDirectionUndefined;
  }

  // Returns 0-based index.
  int IndexInParent() const;

  // Value should be 1-based. 0 means not supported.
  virtual int PosInSet() const { return 0; }
  virtual int SetSize() const { return 0; }
  bool SupportsSetSizeAndPosInSet() const;

  // ARIA live-region features.
  bool IsLiveRegion() const;
  AXObjectImpl* LiveRegionRoot() const;
  virtual const AtomicString& LiveRegionStatus() const { return g_null_atom; }
  virtual const AtomicString& LiveRegionRelevant() const { return g_null_atom; }
  virtual bool LiveRegionAtomic() const { return false; }
  virtual bool LiveRegionBusy() const { return false; }

  const AtomicString& ContainerLiveRegionStatus() const;
  const AtomicString& ContainerLiveRegionRelevant() const;
  bool ContainerLiveRegionAtomic() const;
  bool ContainerLiveRegionBusy() const;

  // Every object's bounding box is returned relative to a
  // container object (which is guaranteed to be an ancestor) and
  // optionally a transformation matrix that needs to be applied too.
  // To compute the absolute bounding box of an element, start with its
  // boundsInContainer and apply the transform. Then as long as its container is
  // not null, walk up to its container and offset by the container's offset
  // from origin, the container's scroll position if any, and apply the
  // container's transform.  Do this until you reach the root of the tree.
  virtual void GetRelativeBounds(AXObjectImpl** out_container,
                                 FloatRect& out_bounds_in_container,
                                 SkMatrix44& out_container_transform) const;

  // Get the bounds in frame-relative coordinates as a LayoutRect.
  LayoutRect GetBoundsInFrameCoordinates() const;

  // Explicitly set an object's bounding rect and offset container.
  void SetElementRect(LayoutRect r, AXObjectImpl* container) {
    explicit_element_rect_ = r;
    explicit_container_id_ = container->AxObjectID();
  }

  // Hit testing.
  // Called on the root AX object to return the deepest available element.
  virtual AXObjectImpl* AccessibilityHitTest(const IntPoint&) const {
    return 0;
  }
  // Called on the AX object after the layout tree determines which is the right
  // AXLayoutObject.
  virtual AXObjectImpl* ElementAccessibilityHitTest(const IntPoint&) const;

  // High-level accessibility tree access. Other modules should only use these
  // functions.
  const AXObjectVector& Children();
  AXObjectImpl* ParentObject() const;
  AXObjectImpl* ParentObjectIfExists() const;
  virtual AXObjectImpl* ComputeParent() const = 0;
  virtual AXObjectImpl* ComputeParentIfExists() const { return 0; }
  AXObjectImpl* CachedParentObject() const { return parent_; }
  AXObjectImpl* ParentObjectUnignored() const;

  // Low-level accessibility tree exploration, only for use within the
  // accessibility module.
  virtual AXObjectImpl* RawFirstChild() const { return 0; }
  virtual AXObjectImpl* RawNextSibling() const { return 0; }
  virtual void AddChildren() {}
  virtual bool CanHaveChildren() const { return true; }
  bool HasChildren() const { return have_children_; }
  virtual void UpdateChildrenIfNecessary();
  virtual bool NeedsToUpdateChildren() const { return false; }
  virtual void SetNeedsToUpdateChildren() {}
  virtual void ClearChildren();
  virtual void DetachFromParent() { parent_ = 0; }
  virtual AXObjectImpl* ScrollBar(AccessibilityOrientation) { return 0; }

  // Properties of the object's owning document or page.
  virtual double EstimatedLoadingProgress() const { return 0; }

  // DOM and layout tree access.
  virtual Node* GetNode() const { return nullptr; }
  virtual Element* GetElement() const;  // Same as GetNode, if it's an Element.
  virtual LayoutObject* GetLayoutObject() const { return nullptr; }
  virtual Document* GetDocument() const;
  virtual FrameView* DocumentFrameView() const;
  virtual Element* AnchorElement() const { return nullptr; }
  virtual Element* ActionElement() const { return nullptr; }
  String Language() const;
  bool HasAttribute(const QualifiedName&) const;
  const AtomicString& GetAttribute(const QualifiedName&) const;

  // Methods that retrieve or manipulate the current selection.

  // Get the current selection from anywhere in the accessibility tree.
  virtual AXRange Selection() const { return AXRange(); }
  // Gets only the start and end offsets of the selection computed using the
  // current object as the starting point. Returns a null selection if there is
  // no selection in the subtree rooted at this object.
  virtual AXRange SelectionUnderObject() const { return AXRange(); }
  virtual void SetSelection(const AXRange&) {}

  // Scrollable containers.
  bool IsScrollableContainer() const;
  IntPoint GetScrollOffset() const;
  IntPoint MinimumScrollOffset() const;
  IntPoint MaximumScrollOffset() const;
  void SetScrollOffset(const IntPoint&) const;

  // If this object itself scrolls, return its ScrollableArea.
  virtual ScrollableArea* GetScrollableAreaIfScrollable() const { return 0; }

  // Modify or take an action on an object.
  virtual void Increment() {}
  virtual void Decrement() {}
  bool PerformDefaultAction() { return Press(); }
  virtual bool Press();
  // Make this object visible by scrolling as many nested scrollable views as
  // needed.
  void ScrollToMakeVisible() const;
  // Same, but if the whole object can't be made visible, try for this subrect,
  // in local coordinates.
  void ScrollToMakeVisibleWithSubFocus(const IntRect&) const;
  // Scroll this object to a given point in global coordinates of the top-level
  // window.
  void ScrollToGlobalPoint(const IntPoint&) const;
  virtual void SetFocused(bool) {}
  virtual void SetSelected(bool) {}
  virtual void SetSequentialFocusNavigationStartingPoint();
  virtual void SetValue(const String&) {}
  virtual void SetValue(float) {}

  // Notifications that this object may have changed.
  virtual void ChildrenChanged() {}
  virtual void HandleActiveDescendantChanged() {}
  virtual void HandleAriaExpandedChanged() {}
  void NotifyIfIgnoredValueChanged();
  virtual void SelectionChanged();
  virtual void TextChanged() {}
  virtual void UpdateAccessibilityRole() {}

  // Text metrics. Most of these should be deprecated, needs major cleanup.
  virtual VisiblePosition VisiblePositionForIndex(int) const {
    return VisiblePosition();
  }
  int LineForPosition(const VisiblePosition&) const;
  virtual int Index(const VisiblePosition&) const { return -1; }
  virtual void LineBreaks(Vector<int>&) const {}

  // Static helper functions.
  static bool IsARIAControl(AccessibilityRole);
  static bool IsARIAInput(AccessibilityRole);
  static AccessibilityRole AriaRoleToWebCoreRole(const String&);
  static const AtomicString& RoleName(AccessibilityRole);
  static const AtomicString& InternalRoleName(AccessibilityRole);
  static bool IsInsideFocusableElementOrARIAWidget(const Node&);

 protected:
  AXID id_;
  AXObjectVector children_;
  mutable bool have_children_;
  AccessibilityRole role_;
  AXObjectInclusion last_known_is_ignored_value_;
  LayoutRect explicit_element_rect_;
  AXID explicit_container_id_;

  // Used only inside textAlternative():
  static String CollapseWhitespace(const String&);
  static String RecursiveTextAlternative(const AXObjectImpl&,
                                         bool in_aria_labelled_by_traversal,
                                         AXObjectSet& visited);
  bool IsHiddenForTextAlternativeCalculation() const;
  String AriaTextAlternative(bool recursive,
                             bool in_aria_labelled_by_traversal,
                             AXObjectSet& visited,
                             AXNameFrom&,
                             AXRelatedObjectVector*,
                             NameSources*,
                             bool* found_text_alternative) const;
  String TextFromElements(bool in_aria_labelled_by_traversal,
                          AXObjectSet& visited,
                          HeapVector<Member<Element>>& elements,
                          AXRelatedObjectVector* related_objects) const;
  void TokenVectorFromAttribute(Vector<String>&, const QualifiedName&) const;
  void ElementsFromAttribute(HeapVector<Member<Element>>& elements,
                             const QualifiedName&) const;
  void AriaLabelledbyElementVector(HeapVector<Member<Element>>& elements) const;
  String TextFromAriaLabelledby(AXObjectSet& visited,
                                AXRelatedObjectVector* related_objects) const;
  String TextFromAriaDescribedby(AXRelatedObjectVector* related_objects) const;

  virtual const AXObjectImpl* InheritsPresentationalRoleFrom() const {
    return 0;
  }

  bool CanReceiveAccessibilityFocus() const;
  bool NameFromContents(bool recursive) const;

  AccessibilityRole ButtonRoleType() const;

  virtual LayoutObject* LayoutObjectForRelativeBounds() const {
    return nullptr;
  }

  mutable Member<AXObjectImpl> parent_;

  // The following cached attribute values (the ones starting with m_cached*)
  // are only valid if m_lastModificationCount matches
  // AXObjectCacheImpl::modificationCount().
  mutable int last_modification_count_;
  mutable RGBA32 cached_background_color_;
  mutable bool cached_is_ignored_ : 1;
  mutable bool cached_is_inert_or_aria_hidden_ : 1;
  mutable bool cached_is_descendant_of_leaf_node_ : 1;
  mutable bool cached_is_descendant_of_disabled_node_ : 1;
  mutable bool cached_has_inherited_presentational_role_ : 1;
  mutable bool cached_is_presentational_child_ : 1;
  mutable bool cached_ancestor_exposes_active_descendant_ : 1;
  mutable Member<AXObjectImpl> cached_live_region_root_;

  Member<AXObjectCacheImpl> ax_object_cache_;

  // Updates the cached attribute values. This may be recursive, so to prevent
  // deadlocks,
  // functions called here may only search up the tree (ancestors), not down.
  void UpdateCachedAttributeValuesIfNeeded() const;

 private:
  static bool IsNativeInputInMixedState(const Node*);
  static bool IncludesARIAWidgetRole(const String&);
  static bool HasInteractiveARIAAttribute(const Element&);

  static unsigned number_of_live_ax_objects_;
};

DEFINE_TYPE_CASTS(AXObjectImpl, AXObject, obj, true, true);

#define DEFINE_AX_OBJECT_TYPE_CASTS(thisType, predicate)               \
  DEFINE_TYPE_CASTS(thisType, AXObjectImpl, object, object->predicate, \
                    object.predicate)

}  // namespace blink

#endif  // AXObjectImpl_h
