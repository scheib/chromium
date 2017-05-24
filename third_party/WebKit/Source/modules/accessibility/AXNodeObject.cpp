/*
 * Copyright (C) 2012, Google Inc. All rights reserved.
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

#include "modules/accessibility/AXNodeObject.h"

#include "core/InputTypeNames.h"
#include "core/dom/AccessibleNode.h"
#include "core/dom/DocumentUserGestureToken.h"
#include "core/dom/Element.h"
#include "core/dom/NodeTraversal.h"
#include "core/dom/QualifiedName.h"
#include "core/dom/Text.h"
#include "core/dom/shadow/FlatTreeTraversal.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/markers/DocumentMarkerController.h"
#include "core/frame/FrameView.h"
#include "core/html/HTMLAnchorElement.h"
#include "core/html/HTMLDListElement.h"
#include "core/html/HTMLDivElement.h"
#include "core/html/HTMLFieldSetElement.h"
#include "core/html/HTMLFrameElementBase.h"
#include "core/html/HTMLImageElement.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/HTMLLabelElement.h"
#include "core/html/HTMLLegendElement.h"
#include "core/html/HTMLMediaElement.h"
#include "core/html/HTMLMeterElement.h"
#include "core/html/HTMLPlugInElement.h"
#include "core/html/HTMLSelectElement.h"
#include "core/html/HTMLTableCaptionElement.h"
#include "core/html/HTMLTableCellElement.h"
#include "core/html/HTMLTableElement.h"
#include "core/html/HTMLTableRowElement.h"
#include "core/html/HTMLTableSectionElement.h"
#include "core/html/HTMLTextAreaElement.h"
#include "core/html/LabelsNodeList.h"
#include "core/html/TextControlElement.h"
#include "core/html/forms/RadioInputType.h"
#include "core/html/parser/HTMLParserIdioms.h"
#include "core/html/shadow/MediaControlElementTypes.h"
#include "core/layout/LayoutBlockFlow.h"
#include "core/layout/LayoutObject.h"
#include "core/svg/SVGElement.h"
#include "modules/accessibility/AXObjectCacheImpl.h"
#include "platform/UserGestureIndicator.h"
#include "platform/text/PlatformLocale.h"
#include "platform/weborigin/KURL.h"
#include "platform/wtf/text/StringBuilder.h"

namespace blink {

using namespace HTMLNames;

class SparseAttributeSetter {
  USING_FAST_MALLOC(SparseAttributeSetter);

 public:
  virtual void Run(const AXObjectImpl&,
                   AXSparseAttributeClient&,
                   const AtomicString& value) = 0;
};

class BoolAttributeSetter : public SparseAttributeSetter {
 public:
  BoolAttributeSetter(AXBoolAttribute attribute) : attribute_(attribute) {}

 private:
  AXBoolAttribute attribute_;

  void Run(const AXObjectImpl& obj,
           AXSparseAttributeClient& attribute_map,
           const AtomicString& value) override {
    attribute_map.AddBoolAttribute(attribute_,
                                   EqualIgnoringASCIICase(value, "true"));
  }
};

class StringAttributeSetter : public SparseAttributeSetter {
 public:
  StringAttributeSetter(AXStringAttribute attribute) : attribute_(attribute) {}

 private:
  AXStringAttribute attribute_;

  void Run(const AXObjectImpl& obj,
           AXSparseAttributeClient& attribute_map,
           const AtomicString& value) override {
    attribute_map.AddStringAttribute(attribute_, value);
  }
};

class ObjectAttributeSetter : public SparseAttributeSetter {
 public:
  ObjectAttributeSetter(AXObjectAttribute attribute) : attribute_(attribute) {}

 private:
  AXObjectAttribute attribute_;

  void Run(const AXObjectImpl& obj,
           AXSparseAttributeClient& attribute_map,
           const AtomicString& value) override {
    if (value.IsNull() || value.IsEmpty())
      return;

    Node* node = obj.GetNode();
    if (!node || !node->IsElementNode())
      return;
    Element* target = ToElement(node)->GetTreeScope().getElementById(value);
    if (!target)
      return;
    AXObjectImpl* ax_target = obj.AxObjectCache().GetOrCreate(target);
    if (ax_target)
      attribute_map.AddObjectAttribute(attribute_, *ax_target);
  }
};

class ObjectVectorAttributeSetter : public SparseAttributeSetter {
 public:
  ObjectVectorAttributeSetter(AXObjectVectorAttribute attribute)
      : attribute_(attribute) {}

 private:
  AXObjectVectorAttribute attribute_;

  void Run(const AXObjectImpl& obj,
           AXSparseAttributeClient& attribute_map,
           const AtomicString& value) override {
    Node* node = obj.GetNode();
    if (!node || !node->IsElementNode())
      return;

    String attribute_value = value.GetString();
    if (attribute_value.IsEmpty())
      return;

    attribute_value.SimplifyWhiteSpace();
    Vector<String> ids;
    attribute_value.Split(' ', ids);
    if (ids.IsEmpty())
      return;

    HeapVector<Member<AXObjectImpl>> objects;
    TreeScope& scope = node->GetTreeScope();
    for (const auto& id : ids) {
      if (Element* id_element = scope.getElementById(AtomicString(id))) {
        AXObjectImpl* ax_id_element =
            obj.AxObjectCache().GetOrCreate(id_element);
        if (ax_id_element && !ax_id_element->AccessibilityIsIgnored())
          objects.push_back(ax_id_element);
      }
    }

    attribute_map.AddObjectVectorAttribute(attribute_, objects);
  }
};

using AXSparseAttributeSetterMap =
    HashMap<QualifiedName, SparseAttributeSetter*>;

static AXSparseAttributeSetterMap& GetSparseAttributeSetterMap() {
  // Use a map from attribute name to properties of that attribute.
  // That way we only need to iterate over the list of attributes once,
  // rather than calling getAttribute() once for each possible obscure
  // accessibility attribute.
  DEFINE_STATIC_LOCAL(AXSparseAttributeSetterMap,
                      ax_sparse_attribute_setter_map, ());
  if (ax_sparse_attribute_setter_map.IsEmpty()) {
    ax_sparse_attribute_setter_map.Set(
        aria_activedescendantAttr,
        new ObjectAttributeSetter(AXObjectAttribute::kAriaActiveDescendant));
    ax_sparse_attribute_setter_map.Set(
        aria_controlsAttr, new ObjectVectorAttributeSetter(
                               AXObjectVectorAttribute::kAriaControls));
    ax_sparse_attribute_setter_map.Set(
        aria_flowtoAttr,
        new ObjectVectorAttributeSetter(AXObjectVectorAttribute::kAriaFlowTo));
    ax_sparse_attribute_setter_map.Set(
        aria_detailsAttr,
        new ObjectVectorAttributeSetter(AXObjectVectorAttribute::kAriaDetails));
    ax_sparse_attribute_setter_map.Set(
        aria_errormessageAttr,
        new ObjectAttributeSetter(AXObjectAttribute::kAriaErrorMessage));
    ax_sparse_attribute_setter_map.Set(
        aria_keyshortcutsAttr,
        new StringAttributeSetter(AXStringAttribute::kAriaKeyShortcuts));
    ax_sparse_attribute_setter_map.Set(
        aria_roledescriptionAttr,
        new StringAttributeSetter(AXStringAttribute::kAriaRoleDescription));
  }
  return ax_sparse_attribute_setter_map;
}

AXNodeObject::AXNodeObject(Node* node, AXObjectCacheImpl& ax_object_cache)
    : AXObjectImpl(ax_object_cache),
      aria_role_(kUnknownRole),
      children_dirty_(false),
      node_(node) {}

AXNodeObject* AXNodeObject::Create(Node* node,
                                   AXObjectCacheImpl& ax_object_cache) {
  return new AXNodeObject(node, ax_object_cache);
}

AXNodeObject::~AXNodeObject() {
  DCHECK(!node_);
}

void AXNodeObject::AlterSliderValue(bool increase) {
  if (RoleValue() != kSliderRole)
    return;

  float value = ValueForRange();
  float step = StepValueForRange();

  value += increase ? step : -step;

  SetValue(String::Number(value));
  AxObjectCache().PostNotification(GetNode(),
                                   AXObjectCacheImpl::kAXValueChanged);
}

AXObjectImpl* AXNodeObject::ActiveDescendant() {
  if (!node_ || !node_->IsElementNode())
    return nullptr;

  const AtomicString& active_descendant_attr =
      GetAttribute(aria_activedescendantAttr);
  if (active_descendant_attr.IsNull() || active_descendant_attr.IsEmpty())
    return nullptr;

  Element* element = ToElement(GetNode());
  Element* descendant =
      element->GetTreeScope().getElementById(active_descendant_attr);
  if (!descendant)
    return nullptr;

  AXObjectImpl* ax_descendant = AxObjectCache().GetOrCreate(descendant);
  return ax_descendant;
}

bool AXNodeObject::ComputeAccessibilityIsIgnored(
    IgnoredReasons* ignored_reasons) const {
#if DCHECK_IS_ON()
  // Double-check that an AXObjectImpl is never accessed before
  // it's been initialized.
  DCHECK(initialized_);
#endif

  // If this element is within a parent that cannot have children, it should not
  // be exposed.
  if (IsDescendantOfLeafNode()) {
    if (ignored_reasons)
      ignored_reasons->push_back(
          IgnoredReason(kAXAncestorIsLeafNode, LeafNodeAncestor()));
    return true;
  }

  // Ignore labels that are already referenced by a control.
  AXObjectImpl* control_object = CorrespondingControlForLabelElement();
  if (control_object && control_object->IsCheckboxOrRadio() &&
      control_object->NameFromLabelElement()) {
    if (ignored_reasons) {
      HTMLLabelElement* label = LabelElementContainer();
      if (label && label != GetNode()) {
        AXObjectImpl* label_ax_object = AxObjectCache().GetOrCreate(label);
        ignored_reasons->push_back(
            IgnoredReason(kAXLabelContainer, label_ax_object));
      }

      ignored_reasons->push_back(IgnoredReason(kAXLabelFor, control_object));
    }
    return true;
  }

  Element* element = GetNode()->IsElementNode() ? ToElement(GetNode())
                                                : GetNode()->parentElement();
  if (!GetLayoutObject() && (!element || !element->IsInCanvasSubtree()) &&
      !AOMPropertyOrARIAAttributeIsFalse(AOMBooleanProperty::kHidden)) {
    if (ignored_reasons)
      ignored_reasons->push_back(IgnoredReason(kAXNotRendered));
    return true;
  }

  if (role_ == kUnknownRole) {
    if (ignored_reasons)
      ignored_reasons->push_back(IgnoredReason(kAXUninteresting));
    return true;
  }
  return false;
}

static bool IsListElement(Node* node) {
  return isHTMLUListElement(*node) || isHTMLOListElement(*node) ||
         isHTMLDListElement(*node);
}

static bool IsPresentationalInTable(AXObjectImpl* parent,
                                    HTMLElement* current_element) {
  if (!current_element)
    return false;

  Node* parent_node = parent->GetNode();
  if (!parent_node || !parent_node->IsHTMLElement())
    return false;

  // AXTable determines the role as checking isTableXXX.
  // If Table has explicit role including presentation, AXTable doesn't assign
  // implicit Role to a whole Table. That's why we should check it based on
  // node.
  // Normal Table Tree is that
  // cell(its role)-> tr(tr role)-> tfoot, tbody, thead(ignored role) ->
  //     table(table role).
  // If table has presentation role, it will be like
  // cell(group)-> tr(unknown) -> tfoot, tbody, thead(ignored) ->
  //     table(presentation).
  if (IsHTMLTableCellElement(*current_element) &&
      isHTMLTableRowElement(*parent_node))
    return parent->HasInheritedPresentationalRole();

  if (isHTMLTableRowElement(*current_element) &&
      IsHTMLTableSectionElement(ToHTMLElement(*parent_node))) {
    // Because TableSections have ignored role, presentation should be checked
    // with its parent node.
    AXObjectImpl* table_object = parent->ParentObject();
    Node* table_node = table_object ? table_object->GetNode() : 0;
    return isHTMLTableElement(table_node) &&
           table_object->HasInheritedPresentationalRole();
  }
  return false;
}

static bool IsRequiredOwnedElement(AXObjectImpl* parent,
                                   AccessibilityRole current_role,
                                   HTMLElement* current_element) {
  Node* parent_node = parent->GetNode();
  if (!parent_node || !parent_node->IsHTMLElement())
    return false;

  if (current_role == kListItemRole)
    return IsListElement(parent_node);
  if (current_role == kListMarkerRole)
    return isHTMLLIElement(*parent_node);
  if (current_role == kMenuItemCheckBoxRole || current_role == kMenuItemRole ||
      current_role == kMenuItemRadioRole)
    return isHTMLMenuElement(*parent_node);

  if (!current_element)
    return false;
  if (IsHTMLTableCellElement(*current_element))
    return isHTMLTableRowElement(*parent_node);
  if (isHTMLTableRowElement(*current_element))
    return IsHTMLTableSectionElement(ToHTMLElement(*parent_node));

  // In case of ListboxRole and its child, ListBoxOptionRole, inheritance of
  // presentation role is handled in AXListBoxOption because ListBoxOption Role
  // doesn't have any child.
  // If it's just ignored because of presentation, we can't see any AX tree
  // related to ListBoxOption.
  return false;
}

const AXObjectImpl* AXNodeObject::InheritsPresentationalRoleFrom() const {
  // ARIA states if an item can get focus, it should not be presentational.
  if (CanSetFocusAttribute())
    return 0;

  if (IsPresentational())
    return this;

  // http://www.w3.org/TR/wai-aria/complete#presentation
  // ARIA spec says that the user agent MUST apply an inherited role of
  // presentation
  // to any owned elements that do not have an explicit role defined.
  if (AriaRoleAttribute() != kUnknownRole)
    return 0;

  AXObjectImpl* parent = ParentObject();
  if (!parent)
    return 0;

  HTMLElement* element = nullptr;
  if (GetNode() && GetNode()->IsHTMLElement())
    element = ToHTMLElement(GetNode());
  if (!parent->HasInheritedPresentationalRole()) {
    if (!GetLayoutObject() || !GetLayoutObject()->IsBoxModelObject())
      return 0;

    LayoutBoxModelObject* css_box = ToLayoutBoxModelObject(GetLayoutObject());
    if (!css_box->IsTableCell() && !css_box->IsTableRow())
      return 0;

    if (!IsPresentationalInTable(parent, element))
      return 0;
  }
  // ARIA spec says that when a parent object is presentational and this object
  // is a required owned element of that parent, then this object is also
  // presentational.
  if (IsRequiredOwnedElement(parent, RoleValue(), element))
    return parent;
  return 0;
}

// There should only be one banner/contentInfo per page. If header/footer are
// being used within an article, aside, nave, section, blockquote, details,
// fieldset, figure, td, or main, then it should not be exposed as whole
// page's banner/contentInfo.
static HashSet<QualifiedName>& GetLandmarkRolesNotAllowed() {
  DEFINE_STATIC_LOCAL(HashSet<QualifiedName>, landmark_roles_not_allowed, ());
  if (landmark_roles_not_allowed.IsEmpty()) {
    landmark_roles_not_allowed.insert(articleTag);
    landmark_roles_not_allowed.insert(asideTag);
    landmark_roles_not_allowed.insert(navTag);
    landmark_roles_not_allowed.insert(sectionTag);
    landmark_roles_not_allowed.insert(blockquoteTag);
    landmark_roles_not_allowed.insert(detailsTag);
    landmark_roles_not_allowed.insert(fieldsetTag);
    landmark_roles_not_allowed.insert(figureTag);
    landmark_roles_not_allowed.insert(tdTag);
    landmark_roles_not_allowed.insert(mainTag);
  }
  return landmark_roles_not_allowed;
}

bool AXNodeObject::IsDescendantOfElementType(
    HashSet<QualifiedName>& tag_names) const {
  if (!GetNode())
    return false;

  for (Element* parent = GetNode()->parentElement(); parent;
       parent = parent->parentElement()) {
    if (tag_names.Contains(parent->TagQName()))
      return true;
  }
  return false;
}

AccessibilityRole AXNodeObject::NativeAccessibilityRoleIgnoringAria() const {
  if (!GetNode())
    return kUnknownRole;

  // |HTMLAnchorElement| sets isLink only when it has hrefAttr.
  if (GetNode()->IsLink())
    return kLinkRole;

  if (isHTMLAnchorElement(*GetNode())) {
    // We assume that an anchor element is LinkRole if it has event listners
    // even though it doesn't have hrefAttr.
    if (IsClickable())
      return kLinkRole;
    return kAnchorRole;
  }

  if (isHTMLButtonElement(*GetNode()))
    return ButtonRoleType();

  if (isHTMLDetailsElement(*GetNode()))
    return kDetailsRole;

  if (isHTMLSummaryElement(*GetNode())) {
    ContainerNode* parent = FlatTreeTraversal::Parent(*GetNode());
    if (parent && isHTMLDetailsElement(parent))
      return kDisclosureTriangleRole;
    return kUnknownRole;
  }

  if (isHTMLInputElement(*GetNode())) {
    HTMLInputElement& input = toHTMLInputElement(*GetNode());
    const AtomicString& type = input.type();
    if (input.DataList())
      return kComboBoxRole;
    if (type == InputTypeNames::button) {
      if ((GetNode()->parentNode() &&
           isHTMLMenuElement(GetNode()->parentNode())) ||
          (ParentObject() && ParentObject()->RoleValue() == kMenuRole))
        return kMenuItemRole;
      return ButtonRoleType();
    }
    if (type == InputTypeNames::checkbox) {
      if ((GetNode()->parentNode() &&
           isHTMLMenuElement(GetNode()->parentNode())) ||
          (ParentObject() && ParentObject()->RoleValue() == kMenuRole))
        return kMenuItemCheckBoxRole;
      return kCheckBoxRole;
    }
    if (type == InputTypeNames::date)
      return kDateRole;
    if (type == InputTypeNames::datetime ||
        type == InputTypeNames::datetime_local ||
        type == InputTypeNames::month || type == InputTypeNames::week)
      return kDateTimeRole;
    if (type == InputTypeNames::file)
      return kButtonRole;
    if (type == InputTypeNames::radio) {
      if ((GetNode()->parentNode() &&
           isHTMLMenuElement(GetNode()->parentNode())) ||
          (ParentObject() && ParentObject()->RoleValue() == kMenuRole))
        return kMenuItemRadioRole;
      return kRadioButtonRole;
    }
    if (type == InputTypeNames::number)
      return kSpinButtonRole;
    if (input.IsTextButton())
      return ButtonRoleType();
    if (type == InputTypeNames::range)
      return kSliderRole;
    if (type == InputTypeNames::color)
      return kColorWellRole;
    if (type == InputTypeNames::time)
      return kInputTimeRole;
    return kTextFieldRole;
  }

  if (isHTMLSelectElement(*GetNode())) {
    HTMLSelectElement& select_element = toHTMLSelectElement(*GetNode());
    return select_element.IsMultiple() ? kListBoxRole : kPopUpButtonRole;
  }

  if (isHTMLTextAreaElement(*GetNode()))
    return kTextFieldRole;

  if (HeadingLevel())
    return kHeadingRole;

  if (isHTMLDivElement(*GetNode()))
    return kGenericContainerRole;

  if (isHTMLMeterElement(*GetNode()))
    return kMeterRole;

  if (isHTMLOutputElement(*GetNode()))
    return kStatusRole;

  if (isHTMLParagraphElement(*GetNode()))
    return kParagraphRole;

  if (isHTMLLabelElement(*GetNode()))
    return kLabelRole;

  if (isHTMLLegendElement(*GetNode()))
    return kLegendRole;

  if (isHTMLRubyElement(*GetNode()))
    return kRubyRole;

  if (isHTMLDListElement(*GetNode()))
    return kDescriptionListRole;

  if (isHTMLAudioElement(*GetNode()))
    return kAudioRole;
  if (isHTMLVideoElement(*GetNode()))
    return kVideoRole;

  if (GetNode()->HasTagName(ddTag))
    return kDescriptionListDetailRole;

  if (GetNode()->HasTagName(dtTag))
    return kDescriptionListTermRole;

  if (GetNode()->nodeName() == "math")
    return kMathRole;

  if (GetNode()->HasTagName(rpTag) || GetNode()->HasTagName(rtTag))
    return kAnnotationRole;

  if (isHTMLFormElement(*GetNode()))
    return kFormRole;

  if (GetNode()->HasTagName(abbrTag))
    return kAbbrRole;

  if (GetNode()->HasTagName(articleTag))
    return kArticleRole;

  if (GetNode()->HasTagName(mainTag))
    return kMainRole;

  if (GetNode()->HasTagName(markTag))
    return kMarkRole;

  if (GetNode()->HasTagName(navTag))
    return kNavigationRole;

  if (GetNode()->HasTagName(asideTag))
    return kComplementaryRole;

  if (GetNode()->HasTagName(preTag))
    return kPreRole;

  if (GetNode()->HasTagName(sectionTag))
    return kRegionRole;

  if (GetNode()->HasTagName(addressTag))
    return kContentInfoRole;

  if (isHTMLDialogElement(*GetNode()))
    return kDialogRole;

  // The HTML element should not be exposed as an element. That's what the
  // LayoutView element does.
  if (isHTMLHtmlElement(*GetNode()))
    return kIgnoredRole;

  if (isHTMLIFrameElement(*GetNode())) {
    const AtomicString& aria_role =
        GetAOMPropertyOrARIAAttribute(AOMStringProperty::kRole);
    if (aria_role == "none" || aria_role == "presentation")
      return kIframePresentationalRole;
    return kIframeRole;
  }

  // There should only be one banner/contentInfo per page. If header/footer are
  // being used within an article or section then it should not be exposed as
  // whole page's banner/contentInfo but as a generic container role.
  if (GetNode()->HasTagName(headerTag)) {
    if (IsDescendantOfElementType(GetLandmarkRolesNotAllowed()))
      return kGenericContainerRole;
    return kBannerRole;
  }

  if (GetNode()->HasTagName(footerTag)) {
    if (IsDescendantOfElementType(GetLandmarkRolesNotAllowed()))
      return kGenericContainerRole;
    return kFooterRole;
  }

  if (GetNode()->HasTagName(blockquoteTag))
    return kBlockquoteRole;

  if (GetNode()->HasTagName(captionTag))
    return kCaptionRole;

  if (GetNode()->HasTagName(figcaptionTag))
    return kFigcaptionRole;

  if (GetNode()->HasTagName(figureTag))
    return kFigureRole;

  if (GetNode()->nodeName() == "TIME")
    return kTimeRole;

  if (IsEmbeddedObject())
    return kEmbeddedObjectRole;

  if (isHTMLHRElement(*GetNode()))
    return kSplitterRole;

  if (IsFieldset())
    return kGroupRole;

  return kUnknownRole;
}

AccessibilityRole AXNodeObject::DetermineAccessibilityRole() {
  if (!GetNode())
    return kUnknownRole;

  if ((aria_role_ = DetermineAriaRoleAttribute()) != kUnknownRole)
    return aria_role_;
  if (GetNode()->IsTextNode())
    return kStaticTextRole;

  AccessibilityRole role = NativeAccessibilityRoleIgnoringAria();
  if (role != kUnknownRole)
    return role;
  if (GetNode()->IsElementNode()) {
    Element* element = ToElement(GetNode());
    // A generic element with tabIndex explicitly set gets GroupRole.
    // The layout checks for focusability aren't critical here; a false
    // positive would be harmless.
    if (element->IsInCanvasSubtree() && element->SupportsFocus())
      return kGenericContainerRole;
  }
  return kUnknownRole;
}

AccessibilityRole AXNodeObject::DetermineAriaRoleAttribute() const {
  const AtomicString& aria_role =
      GetAOMPropertyOrARIAAttribute(AOMStringProperty::kRole);
  if (aria_role.IsNull() || aria_role.IsEmpty())
    return kUnknownRole;

  AccessibilityRole role = AriaRoleToWebCoreRole(aria_role);

  // ARIA states if an item can get focus, it should not be presentational.
  if ((role == kNoneRole || role == kPresentationalRole) &&
      CanSetFocusAttribute())
    return kUnknownRole;

  if (role == kButtonRole)
    role = ButtonRoleType();

  role = RemapAriaRoleDueToParent(role);

  if (role)
    return role;

  return kUnknownRole;
}

void AXNodeObject::AccessibilityChildrenFromAttribute(
    QualifiedName attr,
    AXObjectImpl::AXObjectVector& children) const {
  HeapVector<Member<Element>> elements;
  ElementsFromAttribute(elements, attr);

  AXObjectCacheImpl& cache = AxObjectCache();
  for (const auto& element : elements) {
    if (AXObjectImpl* child = cache.GetOrCreate(element)) {
      // Only aria-labelledby and aria-describedby can target hidden elements.
      if (child->AccessibilityIsIgnored() && attr != aria_labelledbyAttr &&
          attr != aria_labeledbyAttr && attr != aria_describedbyAttr) {
        continue;
      }
      children.push_back(child);
    }
  }
}

// This only returns true if this is the element that actually has the
// contentEditable attribute set, unlike node->hasEditableStyle() which will
// also return true if an ancestor is editable.
bool AXNodeObject::HasContentEditableAttributeSet() const {
  const AtomicString& content_editable_value =
      GetAttribute(contenteditableAttr);
  if (content_editable_value.IsNull())
    return false;
  // Both "true" (case-insensitive) and the empty string count as true.
  return content_editable_value.IsEmpty() ||
         EqualIgnoringASCIICase(content_editable_value, "true");
}

bool AXNodeObject::IsTextControl() const {
  if (HasContentEditableAttributeSet())
    return true;

  switch (RoleValue()) {
    case kTextFieldRole:
    case kComboBoxRole:
    case kSearchBoxRole:
    case kSpinButtonRole:
      return true;
    default:
      return false;
  }
}

bool AXNodeObject::IsGenericFocusableElement() const {
  if (!CanSetFocusAttribute())
    return false;

  // If it's a control, it's not generic.
  if (IsControl())
    return false;

  // If it has an aria role, it's not generic.
  if (aria_role_ != kUnknownRole)
    return false;

  // If the content editable attribute is set on this element, that's the reason
  // it's focusable, and existing logic should handle this case already - so
  // it's not a generic focusable element.

  if (HasContentEditableAttributeSet())
    return false;

  // The web area and body element are both focusable, but existing logic
  // handles these cases already, so we don't need to include them here.
  if (RoleValue() == kWebAreaRole)
    return false;
  if (isHTMLBodyElement(GetNode()))
    return false;

  // An SVG root is focusable by default, but it's probably not interactive, so
  // don't include it. It can still be made accessible by giving it an ARIA
  // role.
  if (RoleValue() == kSVGRootRole)
    return false;

  return true;
}

AXObjectImpl* AXNodeObject::MenuButtonForMenu() const {
  Element* menu_item = MenuItemElementForMenu();

  if (menu_item) {
    // ARIA just has generic menu items. AppKit needs to know if this is a top
    // level items like MenuBarButton or MenuBarItem
    AXObjectImpl* menu_item_ax = AxObjectCache().GetOrCreate(menu_item);
    if (menu_item_ax && menu_item_ax->IsMenuButton())
      return menu_item_ax;
  }
  return 0;
}

static Element* SiblingWithAriaRole(String role, Node* node) {
  Node* parent = node->parentNode();
  if (!parent)
    return 0;

  for (Element* sibling = ElementTraversal::FirstChild(*parent); sibling;
       sibling = ElementTraversal::NextSibling(*sibling)) {
    const AtomicString& sibling_aria_role =
        AccessibleNode::GetPropertyOrARIAAttribute(sibling,
                                                   AOMStringProperty::kRole);
    if (EqualIgnoringASCIICase(sibling_aria_role, role))
      return sibling;
  }

  return 0;
}

Element* AXNodeObject::MenuItemElementForMenu() const {
  if (AriaRoleAttribute() != kMenuRole)
    return 0;

  return SiblingWithAriaRole("menuitem", GetNode());
}

Element* AXNodeObject::MouseButtonListener() const {
  Node* node = this->GetNode();
  if (!node)
    return 0;

  if (!node->IsElementNode())
    node = node->parentElement();

  if (!node)
    return 0;

  for (Element* element = ToElement(node); element;
       element = element->parentElement()) {
    // It's a pretty common practice to put click listeners on the body or
    // document, but that's almost never what the user wants when clicking on an
    // accessible element.
    if (isHTMLBodyElement(element))
      break;

    if (element->HasEventListeners(EventTypeNames::click) ||
        element->HasEventListeners(EventTypeNames::mousedown) ||
        element->HasEventListeners(EventTypeNames::mouseup) ||
        element->HasEventListeners(EventTypeNames::DOMActivate))
      return element;
  }

  return 0;
}

AccessibilityRole AXNodeObject::RemapAriaRoleDueToParent(
    AccessibilityRole role) const {
  // Some objects change their role based on their parent.
  // However, asking for the unignoredParent calls accessibilityIsIgnored(),
  // which can trigger a loop.  While inside the call stack of creating an
  // element, we need to avoid accessibilityIsIgnored().
  // https://bugs.webkit.org/show_bug.cgi?id=65174

  if (role != kListBoxOptionRole && role != kMenuItemRole)
    return role;

  for (AXObjectImpl* parent = ParentObject();
       parent && !parent->AccessibilityIsIgnored();
       parent = parent->ParentObject()) {
    AccessibilityRole parent_aria_role = parent->AriaRoleAttribute();

    // Selects and listboxes both have options as child roles, but they map to
    // different roles within WebCore.
    if (role == kListBoxOptionRole && parent_aria_role == kMenuRole)
      return kMenuItemRole;
    // An aria "menuitem" may map to MenuButton or MenuItem depending on its
    // parent.
    if (role == kMenuItemRole && parent_aria_role == kGroupRole)
      return kMenuButtonRole;

    // If the parent had a different role, then we don't need to continue
    // searching up the chain.
    if (parent_aria_role)
      break;
  }

  return role;
}

void AXNodeObject::Init() {
#if DCHECK_IS_ON()
  DCHECK(!initialized_);
  initialized_ = true;
#endif
  role_ = DetermineAccessibilityRole();
}

void AXNodeObject::Detach() {
  AXObjectImpl::Detach();
  node_ = nullptr;
}

void AXNodeObject::GetSparseAXAttributes(
    AXSparseAttributeClient& sparse_attribute_client) const {
  Node* node = this->GetNode();
  if (!node || !node->IsElementNode())
    return;

  AXSparseAttributeSetterMap& ax_sparse_attribute_setter_map =
      GetSparseAttributeSetterMap();
  AttributeCollection attributes = ToElement(node)->AttributesWithoutUpdate();
  for (const Attribute& attr : attributes) {
    SparseAttributeSetter* setter =
        ax_sparse_attribute_setter_map.at(attr.GetName());
    if (setter)
      setter->Run(*this, sparse_attribute_client, attr.Value());
  }

  // TODO(dmazzoni): Efficiently iterate over AccessibleNode properties that are
  // set and merge the two loops somehow.
  if (ToElement(node)->ExistingAccessibleNode()) {
    AtomicString key_shortcuts =
        GetAOMPropertyOrARIAAttribute(AOMStringProperty::kKeyShortcuts);
    if (!key_shortcuts.IsNull()) {
      ax_sparse_attribute_setter_map.at(aria_keyshortcutsAttr)
          ->Run(*this, sparse_attribute_client, key_shortcuts);
    }
    AtomicString role_description =
        GetAOMPropertyOrARIAAttribute(AOMStringProperty::kRoleDescription);
    if (!role_description.IsNull()) {
      ax_sparse_attribute_setter_map.at(aria_roledescriptionAttr)
          ->Run(*this, sparse_attribute_client, role_description);
    }
  }
}

bool AXNodeObject::IsAnchor() const {
  return !IsNativeImage() && IsLink();
}

bool AXNodeObject::IsControl() const {
  Node* node = this->GetNode();
  if (!node)
    return false;

  return ((node->IsElementNode() && ToElement(node)->IsFormControlElement()) ||
          AXObjectImpl::IsARIAControl(AriaRoleAttribute()));
}

bool AXNodeObject::IsControllingVideoElement() const {
  Node* node = this->GetNode();
  if (!node)
    return true;

  return isHTMLVideoElement(ToParentMediaElement(node));
}

bool AXNodeObject::IsEmbeddedObject() const {
  return IsHTMLPlugInElement(GetNode());
}

bool AXNodeObject::IsFieldset() const {
  return isHTMLFieldSetElement(GetNode());
}

bool AXNodeObject::IsHeading() const {
  return RoleValue() == kHeadingRole;
}

bool AXNodeObject::IsHovered() const {
  if (Node* node = this->GetNode())
    return node->IsHovered();
  return false;
}

bool AXNodeObject::IsImage() const {
  return RoleValue() == kImageRole;
}

bool AXNodeObject::IsImageButton() const {
  return IsNativeImage() && IsButton();
}

bool AXNodeObject::IsInputImage() const {
  Node* node = this->GetNode();
  if (RoleValue() == kButtonRole && isHTMLInputElement(node))
    return toHTMLInputElement(*node).type() == InputTypeNames::image;

  return false;
}

bool AXNodeObject::IsLink() const {
  return RoleValue() == kLinkRole;
}

// It is not easily possible to find out if an element is the target of an
// in-page link.
// As a workaround, we check if the element is a sectioning element with an ID,
// or an anchor with a name.
bool AXNodeObject::IsInPageLinkTarget() const {
  if (!node_ || !node_->IsElementNode())
    return false;
  Element* element = ToElement(node_);
  // We exclude elements that are in the shadow DOM.
  if (element->ContainingShadowRoot())
    return false;

  if (isHTMLAnchorElement(element)) {
    HTMLAnchorElement* html_element = toHTMLAnchorElement(element);
    return html_element->HasName() || html_element->HasID();
  }

  if (element->HasID() && (IsLandmarkRelated() || isHTMLSpanElement(element) ||
                           isHTMLDivElement(element))) {
    return true;
  }
  return false;
}

bool AXNodeObject::IsMenu() const {
  return RoleValue() == kMenuRole;
}

bool AXNodeObject::IsMenuButton() const {
  return RoleValue() == kMenuButtonRole;
}

bool AXNodeObject::IsMeter() const {
  return RoleValue() == kMeterRole;
}

bool AXNodeObject::IsMultiSelectable() const {
  switch (RoleValue()) {
    case kGridRole:
    case kTreeGridRole:
    case kTreeRole:
    case kListBoxRole:
    case kTabListRole: {
      bool multiselectable = false;
      if (HasAOMPropertyOrARIAAttribute(AOMBooleanProperty::kMultiselectable,
                                        multiselectable)) {
        return multiselectable;
      }
    }
    default:
      break;
  }

  return isHTMLSelectElement(GetNode()) &&
         toHTMLSelectElement(*GetNode()).IsMultiple();
}

bool AXNodeObject::IsNativeCheckboxOrRadio() const {
  Node* node = this->GetNode();
  if (!isHTMLInputElement(node))
    return false;

  HTMLInputElement* input = toHTMLInputElement(node);
  return input->type() == InputTypeNames::checkbox ||
         input->type() == InputTypeNames::radio;
}

bool AXNodeObject::IsNativeImage() const {
  Node* node = this->GetNode();
  if (!node)
    return false;

  if (isHTMLImageElement(*node))
    return true;

  if (IsHTMLPlugInElement(*node))
    return true;

  if (isHTMLInputElement(*node))
    return toHTMLInputElement(*node).type() == InputTypeNames::image;

  return false;
}

bool AXNodeObject::IsNativeTextControl() const {
  Node* node = this->GetNode();
  if (!node)
    return false;

  if (isHTMLTextAreaElement(*node))
    return true;

  if (isHTMLInputElement(*node))
    return toHTMLInputElement(node)->IsTextField();

  return false;
}

bool AXNodeObject::IsNonNativeTextControl() const {
  if (IsNativeTextControl())
    return false;

  if (HasContentEditableAttributeSet())
    return true;

  if (IsARIATextControl())
    return true;

  return false;
}

bool AXNodeObject::IsPasswordField() const {
  Node* node = this->GetNode();
  if (!isHTMLInputElement(node))
    return false;

  AccessibilityRole aria_role = AriaRoleAttribute();
  if (aria_role != kTextFieldRole && aria_role != kUnknownRole)
    return false;

  return toHTMLInputElement(node)->type() == InputTypeNames::password;
}

bool AXNodeObject::IsProgressIndicator() const {
  return RoleValue() == kProgressIndicatorRole;
}

bool AXNodeObject::IsRichlyEditable() const {
  return HasContentEditableAttributeSet();
}

bool AXNodeObject::IsSlider() const {
  return RoleValue() == kSliderRole;
}

bool AXNodeObject::IsNativeSlider() const {
  Node* node = this->GetNode();
  if (!node)
    return false;

  if (!isHTMLInputElement(node))
    return false;

  return toHTMLInputElement(node)->type() == InputTypeNames::range;
}

bool AXNodeObject::IsClickable() const {
  if (GetNode()) {
    if (GetNode()->IsElementNode() &&
        ToElement(GetNode())->IsDisabledFormControl())
      return false;

    // Note: we can't call getNode()->willRespondToMouseClickEvents() because
    // that triggers a style recalc and can delete this.
    if (GetNode()->HasEventListeners(EventTypeNames::mouseup) ||
        GetNode()->HasEventListeners(EventTypeNames::mousedown) ||
        GetNode()->HasEventListeners(EventTypeNames::click) ||
        GetNode()->HasEventListeners(EventTypeNames::DOMActivate))
      return true;
  }

  return AXObjectImpl::IsClickable();
}

bool AXNodeObject::IsEnabled() const {
  if (IsDescendantOfDisabledNode())
    return false;

  Node* node = this->GetNode();
  if (!node || !node->IsElementNode())
    return true;

  return !ToElement(node)->IsDisabledFormControl();
}

AccessibilityExpanded AXNodeObject::IsExpanded() const {
  if (GetNode() && isHTMLSummaryElement(*GetNode())) {
    if (GetNode()->parentNode() &&
        isHTMLDetailsElement(GetNode()->parentNode()))
      return ToElement(GetNode()->parentNode())->hasAttribute(openAttr)
                 ? kExpandedExpanded
                 : kExpandedCollapsed;
  }

  bool expanded = false;
  if (HasAOMPropertyOrARIAAttribute(AOMBooleanProperty::kExpanded, expanded)) {
    return expanded ? kExpandedExpanded : kExpandedCollapsed;
  }

  return kExpandedUndefined;
}

bool AXNodeObject::IsModal() const {
  if (RoleValue() != kDialogRole && RoleValue() != kAlertDialogRole)
    return false;

  bool modal = false;
  if (HasAOMPropertyOrARIAAttribute(AOMBooleanProperty::kModal, modal))
    return modal;

  if (GetNode() && isHTMLDialogElement(*GetNode()))
    return ToElement(GetNode())->IsInTopLayer();

  return false;
}

bool AXNodeObject::IsPressed() const {
  if (!IsButton())
    return false;

  Node* node = this->GetNode();
  if (!node)
    return false;

  // ARIA button with aria-pressed not undefined, then check for aria-pressed
  // attribute rather than getNode()->active()
  if (AriaRoleAttribute() == kToggleButtonRole) {
    if (EqualIgnoringASCIICase(GetAttribute(aria_pressedAttr), "true") ||
        EqualIgnoringASCIICase(GetAttribute(aria_pressedAttr), "mixed"))
      return true;
    return false;
  }

  return node->IsActive();
}

bool AXNodeObject::IsReadOnly() const {
  Node* node = this->GetNode();
  if (!node)
    return true;

  if (isHTMLTextAreaElement(*node))
    return toHTMLTextAreaElement(*node).IsReadOnly();

  if (isHTMLInputElement(*node)) {
    HTMLInputElement& input = toHTMLInputElement(*node);
    if (input.IsTextField())
      return input.IsReadOnly();
  }

  return !HasEditableStyle(*node);
}

bool AXNodeObject::IsRequired() const {
  Node* n = this->GetNode();
  if (n && (n->IsElementNode() && ToElement(n)->IsFormControlElement()) &&
      HasAttribute(requiredAttr))
    return ToHTMLFormControlElement(n)->IsRequired();

  if (AOMPropertyOrARIAAttributeIsTrue(AOMBooleanProperty::kRequired))
    return true;

  return false;
}

bool AXNodeObject::CanSetFocusAttribute() const {
  Node* node = GetNode();
  if (!node)
    return false;

  if (IsWebArea())
    return true;

  // Children of elements with an aria-activedescendant attribute should be
  // focusable if they have a (non-presentational) ARIA role.
  if (!IsPresentational() && AriaRoleAttribute() != kUnknownRole &&
      AncestorExposesActiveDescendant())
    return true;

  // NOTE: It would be more accurate to ask the document whether
  // setFocusedNode() would do anything. For example, setFocusedNode() will do
  // nothing if the current focused node will not relinquish the focus.
  if (IsDisabledFormControl(node))
    return false;

  return node->IsElementNode() && ToElement(node)->SupportsFocus();
}

bool AXNodeObject::CanSetValueAttribute() const {
  if (AOMPropertyOrARIAAttributeIsTrue(AOMBooleanProperty::kReadOnly))
    return false;

  if (IsProgressIndicator() || IsSlider())
    return true;

  if (IsTextControl() && !IsNativeTextControl())
    return true;

  // Any node could be contenteditable, so isReadOnly should be relied upon
  // for this information for all elements.
  return !IsReadOnly();
}

bool AXNodeObject::CanSetSelectedAttribute() const {
  // ARIA list box options can be selected if they are children of an element
  // with an aria-activedescendant attribute.
  if (AriaRoleAttribute() == kListBoxOptionRole &&
      AncestorExposesActiveDescendant())
    return true;
  return AXObjectImpl::CanSetSelectedAttribute();
}

bool AXNodeObject::CanvasHasFallbackContent() const {
  Node* node = this->GetNode();
  if (!isHTMLCanvasElement(node))
    return false;

  // If it has any children that are elements, we'll assume it might be fallback
  // content. If it has no children or its only children are not elements
  // (e.g. just text nodes), it doesn't have fallback content.
  return ElementTraversal::FirstChild(*node);
}

int AXNodeObject::HeadingLevel() const {
  // headings can be in block flow and non-block flow
  Node* node = this->GetNode();
  if (!node)
    return 0;

  if (RoleValue() == kHeadingRole) {
    String level_str = GetAttribute(aria_levelAttr);
    if (!level_str.IsEmpty()) {
      int level = level_str.ToInt();
      if (level >= 1 && level <= 9)
        return level;
      return 1;
    }
  }

  if (!node->IsHTMLElement())
    return 0;

  HTMLElement& element = ToHTMLElement(*node);
  if (element.HasTagName(h1Tag))
    return 1;

  if (element.HasTagName(h2Tag))
    return 2;

  if (element.HasTagName(h3Tag))
    return 3;

  if (element.HasTagName(h4Tag))
    return 4;

  if (element.HasTagName(h5Tag))
    return 5;

  if (element.HasTagName(h6Tag))
    return 6;

  return 0;
}

unsigned AXNodeObject::HierarchicalLevel() const {
  Node* node = this->GetNode();
  if (!node || !node->IsElementNode())
    return 0;

  Element* element = ToElement(node);
  String level_str = element->getAttribute(aria_levelAttr);
  if (!level_str.IsEmpty()) {
    int level = level_str.ToInt();
    if (level > 0)
      return level;
    return 1;
  }

  // Only tree item will calculate its level through the DOM currently.
  if (RoleValue() != kTreeItemRole)
    return 0;

  // Hierarchy leveling starts at 1, to match the aria-level spec.
  // We measure tree hierarchy by the number of groups that the item is within.
  unsigned level = 1;
  for (AXObjectImpl* parent = ParentObject(); parent;
       parent = parent->ParentObject()) {
    AccessibilityRole parent_role = parent->RoleValue();
    if (parent_role == kGroupRole)
      level++;
    else if (parent_role == kTreeRole)
      break;
  }

  return level;
}

String AXNodeObject::AriaAutoComplete() const {
  if (RoleValue() != kComboBoxRole)
    return String();

  const AtomicString& aria_auto_complete =
      GetAOMPropertyOrARIAAttribute(AOMStringProperty::kAutocomplete)
          .DeprecatedLower();

  if (aria_auto_complete == "inline" || aria_auto_complete == "list" ||
      aria_auto_complete == "both")
    return aria_auto_complete;

  return String();
}

void AXNodeObject::Markers(Vector<DocumentMarker::MarkerType>& marker_types,
                           Vector<AXRange>& marker_ranges) const {
  if (!GetNode() || !GetDocument() || !GetDocument()->View())
    return;

  DocumentMarkerController& marker_controller = GetDocument()->Markers();
  DocumentMarkerVector markers = marker_controller.MarkersFor(GetNode());
  for (size_t i = 0; i < markers.size(); ++i) {
    DocumentMarker* marker = markers[i];
    switch (marker->GetType()) {
      case DocumentMarker::kSpelling:
      case DocumentMarker::kGrammar:
      case DocumentMarker::kTextMatch:
        marker_types.push_back(marker->GetType());
        marker_ranges.push_back(
            AXRange(marker->StartOffset(), marker->EndOffset()));
        break;
      case DocumentMarker::kComposition:
        // No need for accessibility to know about these marker types.
        break;
    }
  }
}

AXObjectImpl* AXNodeObject::InPageLinkTarget() const {
  if (!node_ || !isHTMLAnchorElement(node_) || !GetDocument())
    return AXObjectImpl::InPageLinkTarget();

  HTMLAnchorElement* anchor = toHTMLAnchorElement(node_);
  DCHECK(anchor);
  KURL link_url = anchor->Href();
  if (!link_url.IsValid())
    return AXObjectImpl::InPageLinkTarget();
  String fragment = link_url.FragmentIdentifier();
  if (fragment.IsEmpty())
    return AXObjectImpl::InPageLinkTarget();

  KURL document_url = GetDocument()->Url();
  if (!document_url.IsValid() ||
      !EqualIgnoringFragmentIdentifier(document_url, link_url)) {
    return AXObjectImpl::InPageLinkTarget();
  }

  TreeScope& tree_scope = anchor->GetTreeScope();
  Element* target = tree_scope.FindAnchor(fragment);
  if (!target)
    return AXObjectImpl::InPageLinkTarget();
  // If the target is not in the accessibility tree, get the first unignored
  // sibling.
  return AxObjectCache().FirstAccessibleObjectFromNode(target);
}

AccessibilityOrientation AXNodeObject::Orientation() const {
  const AtomicString& aria_orientation =
      GetAOMPropertyOrARIAAttribute(AOMStringProperty::kOrientation);
  AccessibilityOrientation orientation = kAccessibilityOrientationUndefined;
  if (EqualIgnoringASCIICase(aria_orientation, "horizontal"))
    orientation = kAccessibilityOrientationHorizontal;
  else if (EqualIgnoringASCIICase(aria_orientation, "vertical"))
    orientation = kAccessibilityOrientationVertical;

  switch (RoleValue()) {
    case kComboBoxRole:
    case kListBoxRole:
    case kMenuRole:
    case kScrollBarRole:
    case kTreeRole:
      if (orientation == kAccessibilityOrientationUndefined)
        orientation = kAccessibilityOrientationVertical;

      return orientation;
    case kMenuBarRole:
    case kSliderRole:
    case kSplitterRole:
    case kTabListRole:
    case kToolbarRole:
      if (orientation == kAccessibilityOrientationUndefined)
        orientation = kAccessibilityOrientationHorizontal;

      return orientation;
    case kRadioGroupRole:
    case kTreeGridRole:
      return orientation;
    default:
      return AXObjectImpl::Orientation();
  }
}

AXObjectImpl::AXObjectVector AXNodeObject::RadioButtonsInGroup() const {
  AXObjectVector radio_buttons;
  if (!node_ || RoleValue() != kRadioButtonRole)
    return radio_buttons;

  if (isHTMLInputElement(node_)) {
    HTMLInputElement* radio_button = toHTMLInputElement(node_);
    HeapVector<Member<HTMLInputElement>> html_radio_buttons =
        FindAllRadioButtonsWithSameName(radio_button);
    for (size_t i = 0; i < html_radio_buttons.size(); ++i) {
      AXObjectImpl* ax_radio_button =
          AxObjectCache().GetOrCreate(html_radio_buttons[i]);
      if (ax_radio_button)
        radio_buttons.push_back(ax_radio_button);
    }
    return radio_buttons;
  }

  // If the immediate parent is a radio group, return all its children that are
  // radio buttons.
  AXObjectImpl* parent = ParentObject();
  if (parent && parent->RoleValue() == kRadioGroupRole) {
    for (size_t i = 0; i < parent->Children().size(); ++i) {
      AXObjectImpl* child = parent->Children()[i];
      DCHECK(child);
      if (child->RoleValue() == kRadioButtonRole &&
          !child->AccessibilityIsIgnored()) {
        radio_buttons.push_back(child);
      }
    }
  }

  return radio_buttons;
}

// static
HeapVector<Member<HTMLInputElement>>
AXNodeObject::FindAllRadioButtonsWithSameName(HTMLInputElement* radio_button) {
  HeapVector<Member<HTMLInputElement>> all_radio_buttons;
  if (!radio_button || radio_button->type() != InputTypeNames::radio)
    return all_radio_buttons;

  constexpr bool kTraverseForward = true;
  constexpr bool kTraverseBackward = false;
  HTMLInputElement* first_radio_button = radio_button;
  do {
    radio_button = RadioInputType::NextRadioButtonInGroup(first_radio_button,
                                                          kTraverseBackward);
    if (radio_button)
      first_radio_button = radio_button;
  } while (radio_button);

  HTMLInputElement* next_radio_button = first_radio_button;
  do {
    all_radio_buttons.push_back(next_radio_button);
    next_radio_button = RadioInputType::NextRadioButtonInGroup(
        next_radio_button, kTraverseForward);
  } while (next_radio_button);
  return all_radio_buttons;
}

String AXNodeObject::GetText() const {
  // If this is a user defined static text, use the accessible name computation.
  if (AriaRoleAttribute() == kStaticTextRole)
    return ComputedName();

  if (!IsTextControl())
    return String();

  Node* node = this->GetNode();
  if (!node)
    return String();

  if (IsNativeTextControl() &&
      (isHTMLTextAreaElement(*node) || isHTMLInputElement(*node)))
    return ToTextControlElement(*node).value();

  if (!node->IsElementNode())
    return String();

  return ToElement(node)->innerText();
}

RGBA32 AXNodeObject::ColorValue() const {
  if (!isHTMLInputElement(GetNode()) || !IsColorWell())
    return AXObjectImpl::ColorValue();

  HTMLInputElement* input = toHTMLInputElement(GetNode());
  const AtomicString& type = input->getAttribute(typeAttr);
  if (!EqualIgnoringASCIICase(type, "color"))
    return AXObjectImpl::ColorValue();

  // HTMLInputElement::value always returns a string parseable by Color.
  Color color;
  bool success = color.SetFromString(input->value());
  DCHECK(success);
  return color.Rgb();
}

AriaCurrentState AXNodeObject::GetAriaCurrentState() const {
  const AtomicString& attribute_value =
      GetAOMPropertyOrARIAAttribute(AOMStringProperty::kCurrent);
  if (attribute_value.IsNull())
    return kAriaCurrentStateUndefined;
  if (attribute_value.IsEmpty() ||
      EqualIgnoringASCIICase(attribute_value, "false"))
    return kAriaCurrentStateFalse;
  if (EqualIgnoringASCIICase(attribute_value, "true"))
    return kAriaCurrentStateTrue;
  if (EqualIgnoringASCIICase(attribute_value, "page"))
    return kAriaCurrentStatePage;
  if (EqualIgnoringASCIICase(attribute_value, "step"))
    return kAriaCurrentStateStep;
  if (EqualIgnoringASCIICase(attribute_value, "location"))
    return kAriaCurrentStateLocation;
  if (EqualIgnoringASCIICase(attribute_value, "date"))
    return kAriaCurrentStateDate;
  if (EqualIgnoringASCIICase(attribute_value, "time"))
    return kAriaCurrentStateTime;
  // An unknown value should return true.
  if (!attribute_value.IsEmpty())
    return kAriaCurrentStateTrue;

  return AXObjectImpl::GetAriaCurrentState();
}

InvalidState AXNodeObject::GetInvalidState() const {
  const AtomicString& attribute_value =
      GetAOMPropertyOrARIAAttribute(AOMStringProperty::kInvalid);
  if (EqualIgnoringASCIICase(attribute_value, "false"))
    return kInvalidStateFalse;
  if (EqualIgnoringASCIICase(attribute_value, "true"))
    return kInvalidStateTrue;
  if (EqualIgnoringASCIICase(attribute_value, "spelling"))
    return kInvalidStateSpelling;
  if (EqualIgnoringASCIICase(attribute_value, "grammar"))
    return kInvalidStateGrammar;
  // A yet unknown value.
  if (!attribute_value.IsEmpty())
    return kInvalidStateOther;

  if (GetNode() && GetNode()->IsElementNode() &&
      ToElement(GetNode())->IsFormControlElement()) {
    HTMLFormControlElement* element = ToHTMLFormControlElement(GetNode());
    HeapVector<Member<HTMLFormControlElement>> invalid_controls;
    bool is_invalid = !element->checkValidity(&invalid_controls,
                                              kCheckValidityDispatchNoEvent);
    return is_invalid ? kInvalidStateTrue : kInvalidStateFalse;
  }

  return AXObjectImpl::GetInvalidState();
}

int AXNodeObject::PosInSet() const {
  if (SupportsSetSizeAndPosInSet()) {
    String pos_in_set_str = GetAttribute(aria_posinsetAttr);
    if (!pos_in_set_str.IsEmpty()) {
      int pos_in_set = pos_in_set_str.ToInt();
      if (pos_in_set > 0)
        return pos_in_set;
      return 1;
    }

    return AXObjectImpl::IndexInParent() + 1;
  }

  return 0;
}

int AXNodeObject::SetSize() const {
  if (SupportsSetSizeAndPosInSet()) {
    String set_size_str = GetAttribute(aria_setsizeAttr);
    if (!set_size_str.IsEmpty()) {
      int set_size = set_size_str.ToInt();
      if (set_size > 0)
        return set_size;
      return 1;
    }

    if (ParentObject()) {
      const auto& siblings = ParentObject()->Children();
      return siblings.size();
    }
  }

  return 0;
}

String AXNodeObject::AriaInvalidValue() const {
  if (GetInvalidState() == kInvalidStateOther)
    return GetAOMPropertyOrARIAAttribute(AOMStringProperty::kInvalid);

  return String();
}

String AXNodeObject::ValueDescription() const {
  if (!SupportsRangeValue())
    return String();

  return GetAOMPropertyOrARIAAttribute(AOMStringProperty::kValueText)
      .GetString();
}

float AXNodeObject::ValueForRange() const {
  if (HasAttribute(aria_valuenowAttr))
    return GetAttribute(aria_valuenowAttr).ToFloat();

  if (IsNativeSlider())
    return toHTMLInputElement(*GetNode()).valueAsNumber();

  if (isHTMLMeterElement(GetNode()))
    return toHTMLMeterElement(*GetNode()).value();

  return 0.0;
}

float AXNodeObject::MaxValueForRange() const {
  if (HasAttribute(aria_valuemaxAttr))
    return GetAttribute(aria_valuemaxAttr).ToFloat();

  if (IsNativeSlider())
    return toHTMLInputElement(*GetNode()).Maximum();

  if (isHTMLMeterElement(GetNode()))
    return toHTMLMeterElement(*GetNode()).max();

  return 0.0;
}

float AXNodeObject::MinValueForRange() const {
  if (HasAttribute(aria_valueminAttr))
    return GetAttribute(aria_valueminAttr).ToFloat();

  if (IsNativeSlider())
    return toHTMLInputElement(*GetNode()).Minimum();

  if (isHTMLMeterElement(GetNode()))
    return toHTMLMeterElement(*GetNode()).min();

  return 0.0;
}

float AXNodeObject::StepValueForRange() const {
  if (!IsNativeSlider())
    return 0.0;

  Decimal step =
      toHTMLInputElement(*GetNode()).CreateStepRange(kRejectAny).Step();
  return step.ToString().ToFloat();
}

String AXNodeObject::StringValue() const {
  Node* node = this->GetNode();
  if (!node)
    return String();

  if (isHTMLSelectElement(*node)) {
    HTMLSelectElement& select_element = toHTMLSelectElement(*node);
    int selected_index = select_element.selectedIndex();
    const HeapVector<Member<HTMLElement>>& list_items =
        select_element.GetListItems();
    if (selected_index >= 0 &&
        static_cast<size_t>(selected_index) < list_items.size()) {
      const AtomicString& overridden_description =
          list_items[selected_index]->FastGetAttribute(aria_labelAttr);
      if (!overridden_description.IsNull())
        return overridden_description;
    }
    if (!select_element.IsMultiple())
      return select_element.value();
    return String();
  }

  if (IsNativeTextControl())
    return GetText();

  // Handle other HTML input elements that aren't text controls, like date and
  // time controls, by returning the string value, with the exception of
  // checkboxes and radio buttons (which would return "on").
  if (isHTMLInputElement(node)) {
    HTMLInputElement* input = toHTMLInputElement(node);
    if (input->type() != InputTypeNames::checkbox &&
        input->type() != InputTypeNames::radio)
      return input->value();
  }

  return String();
}

AccessibilityRole AXNodeObject::AriaRoleAttribute() const {
  return aria_role_;
}

// Returns the nearest LayoutBlockFlow ancestor which does not have an
// inlineBoxWrapper - i.e. is not itself an inline object.
static LayoutBlockFlow* NonInlineBlockFlow(LayoutObject* object) {
  LayoutObject* current = object;
  while (current) {
    if (current->IsLayoutBlockFlow()) {
      LayoutBlockFlow* block_flow = ToLayoutBlockFlow(current);
      if (!block_flow->InlineBoxWrapper())
        return block_flow;
    }
    current = current->Parent();
  }

  NOTREACHED();
  return nullptr;
}

// Returns true if |r1| and |r2| are both non-null, both inline, and are
// contained within the same non-inline LayoutBlockFlow.
static bool IsInSameNonInlineBlockFlow(LayoutObject* r1, LayoutObject* r2) {
  if (!r1 || !r2)
    return false;
  if (!r1->IsInline() || !r2->IsInline())
    return false;
  LayoutBlockFlow* b1 = NonInlineBlockFlow(r1);
  LayoutBlockFlow* b2 = NonInlineBlockFlow(r2);
  return b1 && b2 && b1 == b2;
}

//
// New AX name calculation.
//

String AXNodeObject::TextAlternative(bool recursive,
                                     bool in_aria_labelled_by_traversal,
                                     AXObjectSet& visited,
                                     AXNameFrom& name_from,
                                     AXRelatedObjectVector* related_objects,
                                     NameSources* name_sources) const {
  // If nameSources is non-null, relatedObjects is used in filling it in, so it
  // must be non-null as well.
  if (name_sources)
    DCHECK(related_objects);

  bool found_text_alternative = false;

  if (!GetNode() && !GetLayoutObject())
    return String();

  String text_alternative = AriaTextAlternative(
      recursive, in_aria_labelled_by_traversal, visited, name_from,
      related_objects, name_sources, &found_text_alternative);
  if (found_text_alternative && !name_sources)
    return text_alternative;

  // Step 2E from: http://www.w3.org/TR/accname-aam-1.1
  if (recursive && !in_aria_labelled_by_traversal && IsControl() &&
      !IsButton()) {
    // No need to set any name source info in a recursive call.
    if (IsTextControl())
      return GetText();

    if (IsRange()) {
      const AtomicString& aria_valuetext =
          GetAOMPropertyOrARIAAttribute(AOMStringProperty::kValueText);
      if (!aria_valuetext.IsNull())
        return aria_valuetext.GetString();
      return String::Number(ValueForRange());
    }

    return StringValue();
  }

  // Step 2D from: http://www.w3.org/TR/accname-aam-1.1
  text_alternative =
      NativeTextAlternative(visited, name_from, related_objects, name_sources,
                            &found_text_alternative);
  const bool has_text_alternative =
      !text_alternative.IsEmpty() ||
      name_from == kAXNameFromAttributeExplicitlyEmpty;
  if (has_text_alternative && !name_sources)
    return text_alternative;

  // Step 2F / 2G from: http://www.w3.org/TR/accname-aam-1.1
  if (in_aria_labelled_by_traversal || NameFromContents(recursive)) {
    Node* node = this->GetNode();
    if (!isHTMLSelectElement(node)) {  // Avoid option descendant text
      name_from = kAXNameFromContents;
      if (name_sources) {
        name_sources->push_back(NameSource(found_text_alternative));
        name_sources->back().type = name_from;
      }

      if (node && node->IsTextNode())
        text_alternative = ToText(node)->wholeText();
      else if (isHTMLBRElement(node))
        text_alternative = String("\n");
      else
        text_alternative = TextFromDescendants(visited, false);

      if (!text_alternative.IsEmpty()) {
        if (name_sources) {
          found_text_alternative = true;
          name_sources->back().text = text_alternative;
        } else {
          return text_alternative;
        }
      }
    }
  }

  // Step 2H from: http://www.w3.org/TR/accname-aam-1.1
  name_from = kAXNameFromTitle;
  if (name_sources) {
    name_sources->push_back(NameSource(found_text_alternative, titleAttr));
    name_sources->back().type = name_from;
  }
  const AtomicString& title = GetAttribute(titleAttr);
  if (!title.IsEmpty()) {
    text_alternative = title;
    if (name_sources) {
      found_text_alternative = true;
      name_sources->back().text = text_alternative;
    } else {
      return text_alternative;
    }
  }

  name_from = kAXNameFromUninitialized;

  if (name_sources && found_text_alternative) {
    for (size_t i = 0; i < name_sources->size(); ++i) {
      if (!(*name_sources)[i].text.IsNull() && !(*name_sources)[i].superseded) {
        NameSource& name_source = (*name_sources)[i];
        name_from = name_source.type;
        if (!name_source.related_objects.IsEmpty())
          *related_objects = name_source.related_objects;
        return name_source.text;
      }
    }
  }

  return String();
}

String AXNodeObject::TextFromDescendants(AXObjectSet& visited,
                                         bool recursive) const {
  if (!CanHaveChildren() && recursive)
    return String();

  StringBuilder accumulated_text;
  AXObjectImpl* previous = nullptr;

  AXObjectVector children;

  HeapVector<Member<AXObjectImpl>> owned_children;
  ComputeAriaOwnsChildren(owned_children);
  for (AXObjectImpl* obj = RawFirstChild(); obj; obj = obj->RawNextSibling()) {
    if (!AxObjectCache().IsAriaOwned(obj))
      children.push_back(obj);
  }
  for (const auto& owned_child : owned_children)
    children.push_back(owned_child);

  for (AXObjectImpl* child : children) {
    // Don't recurse into children that are explicitly marked as aria-hidden.
    // Note that we don't call isInertOrAriaHidden because that would return
    // true if any ancestor is hidden, but we need to be able to compute the
    // accessible name of object inside hidden subtrees (for example, if
    // aria-labelledby points to an object that's hidden).
    if (child->AOMPropertyOrARIAAttributeIsTrue(AOMBooleanProperty::kHidden))
      continue;

    // If we're going between two layoutObjects that are in separate
    // LayoutBoxes, add whitespace if it wasn't there already. Intuitively if
    // you have <span>Hello</span><span>World</span>, those are part of the same
    // LayoutBox so we should return "HelloWorld", but given
    // <div>Hello</div><div>World</div> the strings are in separate boxes so we
    // should return "Hello World".
    if (previous && accumulated_text.length() &&
        !IsHTMLSpace(accumulated_text[accumulated_text.length() - 1])) {
      if (!IsInSameNonInlineBlockFlow(child->GetLayoutObject(),
                                      previous->GetLayoutObject()))
        accumulated_text.Append(' ');
    }

    String result;
    if (child->IsPresentational())
      result = child->TextFromDescendants(visited, true);
    else
      result = RecursiveTextAlternative(*child, false, visited);
    accumulated_text.Append(result);
    previous = child;
  }

  return accumulated_text.ToString();
}

bool AXNodeObject::NameFromLabelElement() const {
  // This unfortunately duplicates a bit of logic from textAlternative and
  // nativeTextAlternative, but it's necessary because nameFromLabelElement
  // needs to be called from computeAccessibilityIsIgnored, which isn't allowed
  // to call axObjectCache->getOrCreate.

  if (!GetNode() && !GetLayoutObject())
    return false;

  // Step 2A from: http://www.w3.org/TR/accname-aam-1.1
  if (IsHiddenForTextAlternativeCalculation())
    return false;

  // Step 2B from: http://www.w3.org/TR/accname-aam-1.1
  HeapVector<Member<Element>> elements;
  AriaLabelledbyElementVector(elements);
  if (elements.size() > 0)
    return false;

  // Step 2C from: http://www.w3.org/TR/accname-aam-1.1
  const AtomicString& aria_label =
      GetAOMPropertyOrARIAAttribute(AOMStringProperty::kLabel);
  if (!aria_label.IsEmpty())
    return false;

  // Based on
  // http://rawgit.com/w3c/aria/master/html-aam/html-aam.html#accessible-name-and-description-calculation
  // 5.1/5.5 Text inputs, Other labelable Elements
  HTMLElement* html_element = nullptr;
  if (GetNode()->IsHTMLElement())
    html_element = ToHTMLElement(GetNode());
  if (html_element && IsLabelableElement(html_element)) {
    if (ToLabelableElement(html_element)->labels() &&
        ToLabelableElement(html_element)->labels()->length() > 0)
      return true;
  }

  return false;
}

void AXNodeObject::GetRelativeBounds(
    AXObjectImpl** out_container,
    FloatRect& out_bounds_in_container,
    SkMatrix44& out_container_transform) const {
  if (LayoutObjectForRelativeBounds()) {
    AXObjectImpl::GetRelativeBounds(out_container, out_bounds_in_container,
                                    out_container_transform);
    return;
  }

  *out_container = nullptr;
  out_bounds_in_container = FloatRect();
  out_container_transform.setIdentity();

  // First check if it has explicit bounds, for example if this element is tied
  // to a canvas path. When explicit coordinates are provided, the ID of the
  // explicit container element that the coordinates are relative to must be
  // provided too.
  if (!explicit_element_rect_.IsEmpty()) {
    *out_container = AxObjectCache().ObjectFromAXID(explicit_container_id_);
    if (*out_container) {
      out_bounds_in_container = FloatRect(explicit_element_rect_);
      return;
    }
  }

  // If it's in a canvas but doesn't have an explicit rect, get the bounding
  // rect of its children.
  if (GetNode()->parentElement()->IsInCanvasSubtree()) {
    Vector<FloatRect> rects;
    for (Node& child : NodeTraversal::ChildrenOf(*GetNode())) {
      if (child.IsHTMLElement()) {
        if (AXObjectImpl* obj = AxObjectCache().Get(&child)) {
          AXObjectImpl* container;
          FloatRect bounds;
          obj->GetRelativeBounds(&container, bounds, out_container_transform);
          if (container) {
            *out_container = container;
            rects.push_back(bounds);
          }
        }
      }
    }

    if (*out_container) {
      out_bounds_in_container = UnionRect(rects);
      return;
    }
  }

  // If this object doesn't have an explicit element rect or computable from its
  // children, for now, let's return the position of the ancestor that does have
  // a position, and make it the width of that parent, and about the height of a
  // line of text, so that it's clear the object is a child of the parent.
  for (AXObjectImpl* position_provider = ParentObject(); position_provider;
       position_provider = position_provider->ParentObject()) {
    if (position_provider->IsAXLayoutObject()) {
      position_provider->GetRelativeBounds(
          out_container, out_bounds_in_container, out_container_transform);
      if (*out_container)
        out_bounds_in_container.SetSize(
            FloatSize(out_bounds_in_container.Width(),
                      std::min(10.0f, out_bounds_in_container.Height())));
      break;
    }
  }
}

static Node* GetParentNodeForComputeParent(Node* node) {
  if (!node)
    return nullptr;

  Node* parent_node = nullptr;

  // Skip over <optgroup> and consider the <select> the immediate parent of an
  // <option>.
  if (isHTMLOptionElement(node))
    parent_node = toHTMLOptionElement(node)->OwnerSelectElement();

  if (!parent_node)
    parent_node = node->parentNode();

  return parent_node;
}

AXObjectImpl* AXNodeObject::ComputeParent() const {
  DCHECK(!IsDetached());
  if (Node* parent_node = GetParentNodeForComputeParent(GetNode()))
    return AxObjectCache().GetOrCreate(parent_node);

  return nullptr;
}

AXObjectImpl* AXNodeObject::ComputeParentIfExists() const {
  if (Node* parent_node = GetParentNodeForComputeParent(GetNode()))
    return AxObjectCache().Get(parent_node);

  return nullptr;
}

AXObjectImpl* AXNodeObject::RawFirstChild() const {
  if (!GetNode())
    return 0;

  Node* first_child = GetNode()->firstChild();

  if (!first_child)
    return 0;

  return AxObjectCache().GetOrCreate(first_child);
}

AXObjectImpl* AXNodeObject::RawNextSibling() const {
  if (!GetNode())
    return 0;

  Node* next_sibling = GetNode()->nextSibling();
  if (!next_sibling)
    return 0;

  return AxObjectCache().GetOrCreate(next_sibling);
}

void AXNodeObject::AddChildren() {
  DCHECK(!IsDetached());
  // If the need to add more children in addition to existing children arises,
  // childrenChanged should have been called, leaving the object with no
  // children.
  DCHECK(!have_children_);

  if (!node_)
    return;

  have_children_ = true;

  // The only time we add children from the DOM tree to a node with a
  // layoutObject is when it's a canvas.
  if (GetLayoutObject() && !isHTMLCanvasElement(*node_))
    return;

  HeapVector<Member<AXObjectImpl>> owned_children;
  ComputeAriaOwnsChildren(owned_children);

  for (Node& child : NodeTraversal::ChildrenOf(*node_)) {
    AXObjectImpl* child_obj = AxObjectCache().GetOrCreate(&child);
    if (child_obj && !AxObjectCache().IsAriaOwned(child_obj))
      AddChild(child_obj);
  }

  for (const auto& owned_child : owned_children)
    AddChild(owned_child);

  for (const auto& child : children_)
    child->SetParent(this);
}

void AXNodeObject::AddChild(AXObjectImpl* child) {
  InsertChild(child, children_.size());
}

void AXNodeObject::InsertChild(AXObjectImpl* child, unsigned index) {
  if (!child)
    return;

  // If the parent is asking for this child's children, then either it's the
  // first time (and clearing is a no-op), or its visibility has changed. In the
  // latter case, this child may have a stale child cached.  This can prevent
  // aria-hidden changes from working correctly. Hence, whenever a parent is
  // getting children, ensure data is not stale.
  child->ClearChildren();

  if (child->AccessibilityIsIgnored()) {
    const auto& children = child->Children();
    size_t length = children.size();
    for (size_t i = 0; i < length; ++i)
      children_.insert(index + i, children[i]);
  } else {
    DCHECK_EQ(child->ParentObject(), this);
    children_.insert(index, child);
  }
}

bool AXNodeObject::CanHaveChildren() const {
  // If this is an AXLayoutObject, then it's okay if this object
  // doesn't have a node - there are some layoutObjects that don't have
  // associated nodes, like scroll areas and css-generated text.
  if (!GetNode() && !IsAXLayoutObject())
    return false;

  if (GetNode() && isHTMLMapElement(GetNode()))
    return false;

  AccessibilityRole role = RoleValue();

  // If an element has an ARIA role of presentation, we need to consider the
  // native role when deciding whether it can have children or not - otherwise
  // giving something a role of presentation could expose inner implementation
  // details.
  if (IsPresentational())
    role = NativeAccessibilityRoleIgnoringAria();

  switch (role) {
    case kImageRole:
    case kButtonRole:
    case kPopUpButtonRole:
    case kCheckBoxRole:
    case kRadioButtonRole:
    case kSwitchRole:
    case kTabRole:
    case kToggleButtonRole:
    case kListBoxOptionRole:
    case kScrollBarRole:
      return false;
    case kStaticTextRole:
      if (!AxObjectCache().InlineTextBoxAccessibilityEnabled())
        return false;
    default:
      return true;
  }
}

Element* AXNodeObject::ActionElement() const {
  Node* node = this->GetNode();
  if (!node)
    return 0;

  if (isHTMLInputElement(*node)) {
    HTMLInputElement& input = toHTMLInputElement(*node);
    if (!input.IsDisabledFormControl() &&
        (IsCheckboxOrRadio() || input.IsTextButton() ||
         input.type() == InputTypeNames::file))
      return &input;
  } else if (isHTMLButtonElement(*node)) {
    return ToElement(node);
  }

  if (AXObjectImpl::IsARIAInput(AriaRoleAttribute()))
    return ToElement(node);

  if (IsImageButton())
    return ToElement(node);

  if (isHTMLSelectElement(*node))
    return ToElement(node);

  switch (RoleValue()) {
    case kButtonRole:
    case kPopUpButtonRole:
    case kToggleButtonRole:
    case kTabRole:
    case kMenuItemRole:
    case kMenuItemCheckBoxRole:
    case kMenuItemRadioRole:
      return ToElement(node);
    default:
      break;
  }

  Element* anchor = AnchorElement();
  Element* click_element = MouseButtonListener();
  if (!anchor || (click_element && click_element->IsDescendantOf(anchor)))
    return click_element;
  return anchor;
}

Element* AXNodeObject::AnchorElement() const {
  Node* node = this->GetNode();
  if (!node)
    return 0;

  AXObjectCacheImpl& cache = AxObjectCache();

  // search up the DOM tree for an anchor element
  // NOTE: this assumes that any non-image with an anchor is an
  // HTMLAnchorElement
  for (; node; node = node->parentNode()) {
    if (isHTMLAnchorElement(*node) ||
        (node->GetLayoutObject() &&
         cache.GetOrCreate(node->GetLayoutObject())->IsAnchor()))
      return ToElement(node);
  }

  return 0;
}

Document* AXNodeObject::GetDocument() const {
  if (!GetNode())
    return 0;
  return &GetNode()->GetDocument();
}

void AXNodeObject::SetNode(Node* node) {
  node_ = node;
}

AXObjectImpl* AXNodeObject::CorrespondingControlForLabelElement() const {
  HTMLLabelElement* label_element = LabelElementContainer();
  if (!label_element)
    return 0;

  HTMLElement* corresponding_control = label_element->control();
  if (!corresponding_control)
    return 0;

  // Make sure the corresponding control isn't a descendant of this label
  // that's in the middle of being destroyed.
  if (corresponding_control->GetLayoutObject() &&
      !corresponding_control->GetLayoutObject()->Parent())
    return 0;

  return AxObjectCache().GetOrCreate(corresponding_control);
}

HTMLLabelElement* AXNodeObject::LabelElementContainer() const {
  if (!GetNode())
    return 0;

  // the control element should not be considered part of the label
  if (IsControl())
    return 0;

  // the link element should not be considered part of the label
  if (IsLink())
    return 0;

  // find if this has a ancestor that is a label
  return Traversal<HTMLLabelElement>::FirstAncestorOrSelf(*GetNode());
}

void AXNodeObject::SetFocused(bool on) {
  if (!CanSetFocusAttribute())
    return;

  Document* document = this->GetDocument();
  if (!on) {
    document->ClearFocusedElement();
  } else {
    Node* node = this->GetNode();
    if (node && node->IsElementNode()) {
      // If this node is already the currently focused node, then calling
      // focus() won't do anything.  That is a problem when focus is removed
      // from the webpage to chrome, and then returns.  In these cases, we need
      // to do what keyboard and mouse focus do, which is reset focus first.
      if (document->FocusedElement() == node)
        document->ClearFocusedElement();

      ToElement(node)->focus();
    } else {
      document->ClearFocusedElement();
    }
  }
}

void AXNodeObject::Increment() {
  UserGestureIndicator gesture_indicator(DocumentUserGestureToken::Create(
      GetDocument(), UserGestureToken::kNewGesture));
  AlterSliderValue(true);
}

void AXNodeObject::Decrement() {
  UserGestureIndicator gesture_indicator(DocumentUserGestureToken::Create(
      GetDocument(), UserGestureToken::kNewGesture));
  AlterSliderValue(false);
}

void AXNodeObject::SetSequentialFocusNavigationStartingPoint() {
  if (!GetNode())
    return;

  GetNode()->GetDocument().ClearFocusedElement();
  GetNode()->GetDocument().SetSequentialFocusNavigationStartingPoint(GetNode());
}

void AXNodeObject::ChildrenChanged() {
  // This method is meant as a quick way of marking a portion of the
  // accessibility tree dirty.
  if (!GetNode() && !GetLayoutObject())
    return;

  // If this is not part of the accessibility tree because an ancestor
  // has only presentational children, invalidate this object's children but
  // skip sending a notification and skip walking up the ancestors.
  if (AncestorForWhichThisIsAPresentationalChild()) {
    SetNeedsToUpdateChildren();
    return;
  }

  AxObjectCache().PostNotification(this, AXObjectCacheImpl::kAXChildrenChanged);

  // Go up the accessibility parent chain, but only if the element already
  // exists. This method is called during layout, minimal work should be done.
  // If AX elements are created now, they could interrogate the layout tree
  // while it's in a funky state.  At the same time, process ARIA live region
  // changes.
  for (AXObjectImpl* parent = this; parent;
       parent = parent->ParentObjectIfExists()) {
    parent->SetNeedsToUpdateChildren();

    // These notifications always need to be sent because screenreaders are
    // reliant on them to perform.  In other words, they need to be sent even
    // when the screen reader has not accessed this live region since the last
    // update.

    // If this element supports ARIA live regions, then notify the AT of
    // changes.
    if (parent->IsLiveRegion())
      AxObjectCache().PostNotification(parent,
                                       AXObjectCacheImpl::kAXLiveRegionChanged);

    // If this element is an ARIA text box or content editable, post a "value
    // changed" notification on it so that it behaves just like a native input
    // element or textarea.
    if (IsNonNativeTextControl())
      AxObjectCache().PostNotification(parent,
                                       AXObjectCacheImpl::kAXValueChanged);
  }
}

void AXNodeObject::SelectionChanged() {
  // Post the selected text changed event on the first ancestor that's
  // focused (to handle form controls, ARIA text boxes and contentEditable),
  // or the web area if the selection is just in the document somewhere.
  if (IsFocused() || IsWebArea()) {
    AxObjectCache().PostNotification(this,
                                     AXObjectCacheImpl::kAXSelectedTextChanged);
    if (GetDocument()) {
      AXObjectImpl* document_object =
          AxObjectCache().GetOrCreate(GetDocument());
      AxObjectCache().PostNotification(
          document_object, AXObjectCacheImpl::kAXDocumentSelectionChanged);
    }
  } else {
    AXObjectImpl::SelectionChanged();  // Calls selectionChanged on parent.
  }
}

void AXNodeObject::TextChanged() {
  // If this element supports ARIA live regions, or is part of a region with an
  // ARIA editable role, then notify the AT of changes.
  AXObjectCacheImpl& cache = AxObjectCache();
  for (Node* parent_node = GetNode(); parent_node;
       parent_node = parent_node->parentNode()) {
    AXObjectImpl* parent = cache.Get(parent_node);
    if (!parent)
      continue;

    if (parent->IsLiveRegion())
      cache.PostNotification(parent_node,
                             AXObjectCacheImpl::kAXLiveRegionChanged);

    // If this element is an ARIA text box or content editable, post a "value
    // changed" notification on it so that it behaves just like a native input
    // element or textarea.
    if (parent->IsNonNativeTextControl())
      cache.PostNotification(parent_node, AXObjectCacheImpl::kAXValueChanged);
  }
}

void AXNodeObject::UpdateAccessibilityRole() {
  role_ = DetermineAccessibilityRole();
  bool ignored_status = AccessibilityIsIgnored();

  // The AX hierarchy only needs to be updated if the ignored status of an
  // element has changed.
  if (ignored_status != AccessibilityIsIgnored())
    ChildrenChanged();
}

void AXNodeObject::ComputeAriaOwnsChildren(
    HeapVector<Member<AXObjectImpl>>& owned_children) const {
  if (!HasAttribute(aria_ownsAttr))
    return;

  Vector<String> id_vector;
  if (CanHaveChildren() && !IsNativeTextControl() &&
      !HasContentEditableAttributeSet())
    TokenVectorFromAttribute(id_vector, aria_ownsAttr);

  AxObjectCache().UpdateAriaOwns(this, id_vector, owned_children);
}

// Based on
// http://rawgit.com/w3c/aria/master/html-aam/html-aam.html#accessible-name-and-description-calculation
String AXNodeObject::NativeTextAlternative(
    AXObjectSet& visited,
    AXNameFrom& name_from,
    AXRelatedObjectVector* related_objects,
    NameSources* name_sources,
    bool* found_text_alternative) const {
  if (!GetNode())
    return String();

  // If nameSources is non-null, relatedObjects is used in filling it in, so it
  // must be non-null as well.
  if (name_sources)
    DCHECK(related_objects);

  String text_alternative;
  AXRelatedObjectVector local_related_objects;

  const HTMLInputElement* input_element = nullptr;
  if (isHTMLInputElement(GetNode()))
    input_element = toHTMLInputElement(GetNode());

  // 5.1/5.5 Text inputs, Other labelable Elements
  // If you change this logic, update AXNodeObject::nameFromLabelElement, too.
  HTMLElement* html_element = nullptr;
  if (GetNode()->IsHTMLElement())
    html_element = ToHTMLElement(GetNode());

  if (html_element && html_element->IsLabelable()) {
    name_from = kAXNameFromRelatedElement;
    if (name_sources) {
      name_sources->push_back(NameSource(*found_text_alternative));
      name_sources->back().type = name_from;
      name_sources->back().native_source = kAXTextFromNativeHTMLLabel;
    }

    LabelsNodeList* labels = ToLabelableElement(html_element)->labels();
    if (labels && labels->length() > 0) {
      HeapVector<Member<Element>> label_elements;
      for (unsigned label_index = 0; label_index < labels->length();
           ++label_index) {
        Element* label = labels->item(label_index);
        if (name_sources) {
          if (!label->getAttribute(forAttr).IsEmpty() &&
              label->getAttribute(forAttr) == html_element->GetIdAttribute()) {
            name_sources->back().native_source = kAXTextFromNativeHTMLLabelFor;
          } else {
            name_sources->back().native_source =
                kAXTextFromNativeHTMLLabelWrapped;
          }
        }
        label_elements.push_back(label);
      }

      text_alternative =
          TextFromElements(false, visited, label_elements, related_objects);
      if (!text_alternative.IsNull()) {
        *found_text_alternative = true;
        if (name_sources) {
          NameSource& source = name_sources->back();
          source.related_objects = *related_objects;
          source.text = text_alternative;
        } else {
          return text_alternative;
        }
      } else if (name_sources) {
        name_sources->back().invalid = true;
      }
    }
  }

  // 5.2 input type="button", input type="submit" and input type="reset"
  if (input_element && input_element->IsTextButton()) {
    // value attribue
    name_from = kAXNameFromValue;
    if (name_sources) {
      name_sources->push_back(NameSource(*found_text_alternative, valueAttr));
      name_sources->back().type = name_from;
    }
    String value = input_element->value();
    if (!value.IsNull()) {
      text_alternative = value;
      if (name_sources) {
        NameSource& source = name_sources->back();
        source.text = text_alternative;
        *found_text_alternative = true;
      } else {
        return text_alternative;
      }
    }

    // Get default value if object is not laid out.
    // If object is laid out, it will have a layout object for the label.
    if (!GetLayoutObject()) {
      String default_label = input_element->ValueOrDefaultLabel();
      if (value.IsNull() && !default_label.IsNull()) {
        // default label
        name_from = kAXNameFromContents;
        if (name_sources) {
          name_sources->push_back(NameSource(*found_text_alternative));
          name_sources->back().type = name_from;
        }
        text_alternative = default_label;
        if (name_sources) {
          NameSource& source = name_sources->back();
          source.text = text_alternative;
          *found_text_alternative = true;
        } else {
          return text_alternative;
        }
      }
    }
    return text_alternative;
  }

  // 5.3 input type="image"
  if (input_element &&
      input_element->getAttribute(typeAttr) == InputTypeNames::image) {
    // alt attr
    const AtomicString& alt = input_element->getAttribute(altAttr);
    const bool is_empty = alt.IsEmpty() && !alt.IsNull();
    name_from =
        is_empty ? kAXNameFromAttributeExplicitlyEmpty : kAXNameFromAttribute;
    if (name_sources) {
      name_sources->push_back(NameSource(*found_text_alternative, altAttr));
      name_sources->back().type = name_from;
    }
    if (!alt.IsNull()) {
      text_alternative = alt;
      if (name_sources) {
        NameSource& source = name_sources->back();
        source.attribute_value = alt;
        source.text = text_alternative;
        *found_text_alternative = true;
      } else {
        return text_alternative;
      }
    }

    // value attr
    if (name_sources) {
      name_sources->push_back(NameSource(*found_text_alternative, valueAttr));
      name_sources->back().type = name_from;
    }
    name_from = kAXNameFromAttribute;
    String value = input_element->value();
    if (!value.IsNull()) {
      text_alternative = value;
      if (name_sources) {
        NameSource& source = name_sources->back();
        source.text = text_alternative;
        *found_text_alternative = true;
      } else {
        return text_alternative;
      }
    }

    // localised default value ("Submit")
    name_from = kAXNameFromValue;
    text_alternative = input_element->GetLocale().QueryString(
        WebLocalizedString::kSubmitButtonDefaultLabel);
    if (name_sources) {
      name_sources->push_back(NameSource(*found_text_alternative, typeAttr));
      NameSource& source = name_sources->back();
      source.attribute_value = input_element->getAttribute(typeAttr);
      source.type = name_from;
      source.text = text_alternative;
      *found_text_alternative = true;
    } else {
      return text_alternative;
    }
    return text_alternative;
  }

  // 5.1 Text inputs - step 3 (placeholder attribute)
  if (html_element && html_element->IsTextControl()) {
    name_from = kAXNameFromPlaceholder;
    if (name_sources) {
      name_sources->push_back(
          NameSource(*found_text_alternative, placeholderAttr));
      NameSource& source = name_sources->back();
      source.type = name_from;
    }
    const String placeholder = PlaceholderFromNativeAttribute();
    if (!placeholder.IsEmpty()) {
      text_alternative = placeholder;
      if (name_sources) {
        NameSource& source = name_sources->back();
        source.text = text_alternative;
        source.attribute_value =
            html_element->FastGetAttribute(placeholderAttr);
        *found_text_alternative = true;
      } else {
        return text_alternative;
      }
    }

    // Also check for aria-placeholder.
    name_from = kAXNameFromPlaceholder;
    if (name_sources) {
      name_sources->push_back(
          NameSource(*found_text_alternative, aria_placeholderAttr));
      NameSource& source = name_sources->back();
      source.type = name_from;
    }
    const AtomicString& aria_placeholder =
        GetAOMPropertyOrARIAAttribute(AOMStringProperty::kPlaceholder);
    if (!aria_placeholder.IsEmpty()) {
      text_alternative = aria_placeholder;
      if (name_sources) {
        NameSource& source = name_sources->back();
        source.text = text_alternative;
        source.attribute_value = aria_placeholder;
        *found_text_alternative = true;
      } else {
        return text_alternative;
      }
    }

    return text_alternative;
  }

  // 5.7 figure and figcaption Elements
  if (GetNode()->HasTagName(figureTag)) {
    // figcaption
    name_from = kAXNameFromRelatedElement;
    if (name_sources) {
      name_sources->push_back(NameSource(*found_text_alternative));
      name_sources->back().type = name_from;
      name_sources->back().native_source = kAXTextFromNativeHTMLFigcaption;
    }
    Element* figcaption = nullptr;
    for (Element& element : ElementTraversal::DescendantsOf(*(GetNode()))) {
      if (element.HasTagName(figcaptionTag)) {
        figcaption = &element;
        break;
      }
    }
    if (figcaption) {
      AXObjectImpl* figcaption_ax_object =
          AxObjectCache().GetOrCreate(figcaption);
      if (figcaption_ax_object) {
        text_alternative =
            RecursiveTextAlternative(*figcaption_ax_object, false, visited);

        if (related_objects) {
          local_related_objects.push_back(new NameSourceRelatedObject(
              figcaption_ax_object, text_alternative));
          *related_objects = local_related_objects;
          local_related_objects.clear();
        }

        if (name_sources) {
          NameSource& source = name_sources->back();
          source.related_objects = *related_objects;
          source.text = text_alternative;
          *found_text_alternative = true;
        } else {
          return text_alternative;
        }
      }
    }
    return text_alternative;
  }

  // 5.8 img or area Element
  if (isHTMLImageElement(GetNode()) || isHTMLAreaElement(GetNode()) ||
      (GetLayoutObject() && GetLayoutObject()->IsSVGImage())) {
    // alt
    const AtomicString& alt = GetAttribute(altAttr);
    const bool is_empty = alt.IsEmpty() && !alt.IsNull();
    name_from =
        is_empty ? kAXNameFromAttributeExplicitlyEmpty : kAXNameFromAttribute;
    if (name_sources) {
      name_sources->push_back(NameSource(*found_text_alternative, altAttr));
      name_sources->back().type = name_from;
    }
    if (!alt.IsNull()) {
      text_alternative = alt;
      if (name_sources) {
        NameSource& source = name_sources->back();
        source.attribute_value = alt;
        source.text = text_alternative;
        *found_text_alternative = true;
      } else {
        return text_alternative;
      }
    }
    return text_alternative;
  }

  // 5.9 table Element
  if (isHTMLTableElement(GetNode())) {
    HTMLTableElement* table_element = toHTMLTableElement(GetNode());

    // caption
    name_from = kAXNameFromCaption;
    if (name_sources) {
      name_sources->push_back(NameSource(*found_text_alternative));
      name_sources->back().type = name_from;
      name_sources->back().native_source = kAXTextFromNativeHTMLTableCaption;
    }
    HTMLTableCaptionElement* caption = table_element->caption();
    if (caption) {
      AXObjectImpl* caption_ax_object = AxObjectCache().GetOrCreate(caption);
      if (caption_ax_object) {
        text_alternative =
            RecursiveTextAlternative(*caption_ax_object, false, visited);
        if (related_objects) {
          local_related_objects.push_back(
              new NameSourceRelatedObject(caption_ax_object, text_alternative));
          *related_objects = local_related_objects;
          local_related_objects.clear();
        }

        if (name_sources) {
          NameSource& source = name_sources->back();
          source.related_objects = *related_objects;
          source.text = text_alternative;
          *found_text_alternative = true;
        } else {
          return text_alternative;
        }
      }
    }

    // summary
    name_from = kAXNameFromAttribute;
    if (name_sources) {
      name_sources->push_back(NameSource(*found_text_alternative, summaryAttr));
      name_sources->back().type = name_from;
    }
    const AtomicString& summary = GetAttribute(summaryAttr);
    if (!summary.IsNull()) {
      text_alternative = summary;
      if (name_sources) {
        NameSource& source = name_sources->back();
        source.attribute_value = summary;
        source.text = text_alternative;
        *found_text_alternative = true;
      } else {
        return text_alternative;
      }
    }

    return text_alternative;
  }

  // Per SVG AAM 1.0's modifications to 2D of this algorithm.
  if (GetNode()->IsSVGElement()) {
    name_from = kAXNameFromRelatedElement;
    if (name_sources) {
      name_sources->push_back(NameSource(*found_text_alternative));
      name_sources->back().type = name_from;
      name_sources->back().native_source = kAXTextFromNativeHTMLTitleElement;
    }
    DCHECK(GetNode()->IsContainerNode());
    Element* title = ElementTraversal::FirstChild(
        ToContainerNode(*(GetNode())), HasTagName(SVGNames::titleTag));

    if (title) {
      AXObjectImpl* title_ax_object = AxObjectCache().GetOrCreate(title);
      if (title_ax_object && !visited.Contains(title_ax_object)) {
        text_alternative =
            RecursiveTextAlternative(*title_ax_object, false, visited);
        if (related_objects) {
          local_related_objects.push_back(
              new NameSourceRelatedObject(title_ax_object, text_alternative));
          *related_objects = local_related_objects;
          local_related_objects.clear();
        }
      }
      if (name_sources) {
        NameSource& source = name_sources->back();
        source.text = text_alternative;
        source.related_objects = *related_objects;
        *found_text_alternative = true;
      } else {
        return text_alternative;
      }
    }
  }

  // Fieldset / legend.
  if (isHTMLFieldSetElement(GetNode())) {
    name_from = kAXNameFromRelatedElement;
    if (name_sources) {
      name_sources->push_back(NameSource(*found_text_alternative));
      name_sources->back().type = name_from;
      name_sources->back().native_source = kAXTextFromNativeHTMLLegend;
    }
    HTMLElement* legend = toHTMLFieldSetElement(GetNode())->Legend();
    if (legend) {
      AXObjectImpl* legend_ax_object = AxObjectCache().GetOrCreate(legend);
      // Avoid an infinite loop
      if (legend_ax_object && !visited.Contains(legend_ax_object)) {
        text_alternative =
            RecursiveTextAlternative(*legend_ax_object, false, visited);

        if (related_objects) {
          local_related_objects.push_back(
              new NameSourceRelatedObject(legend_ax_object, text_alternative));
          *related_objects = local_related_objects;
          local_related_objects.clear();
        }

        if (name_sources) {
          NameSource& source = name_sources->back();
          source.related_objects = *related_objects;
          source.text = text_alternative;
          *found_text_alternative = true;
        } else {
          return text_alternative;
        }
      }
    }
  }

  // Document.
  if (IsWebArea()) {
    Document* document = this->GetDocument();
    if (document) {
      name_from = kAXNameFromAttribute;
      if (name_sources) {
        name_sources->push_back(
            NameSource(found_text_alternative, aria_labelAttr));
        name_sources->back().type = name_from;
      }
      if (Element* document_element = document->documentElement()) {
        const AtomicString& aria_label =
            AccessibleNode::GetPropertyOrARIAAttribute(
                document_element, AOMStringProperty::kLabel);
        if (!aria_label.IsEmpty()) {
          text_alternative = aria_label;

          if (name_sources) {
            NameSource& source = name_sources->back();
            source.text = text_alternative;
            source.attribute_value = aria_label;
            *found_text_alternative = true;
          } else {
            return text_alternative;
          }
        }
      }

      name_from = kAXNameFromRelatedElement;
      if (name_sources) {
        name_sources->push_back(NameSource(*found_text_alternative));
        name_sources->back().type = name_from;
        name_sources->back().native_source = kAXTextFromNativeHTMLTitleElement;
      }

      text_alternative = document->title();

      Element* title_element = document->TitleElement();
      AXObjectImpl* title_ax_object =
          AxObjectCache().GetOrCreate(title_element);
      if (title_ax_object) {
        if (related_objects) {
          local_related_objects.push_back(
              new NameSourceRelatedObject(title_ax_object, text_alternative));
          *related_objects = local_related_objects;
          local_related_objects.clear();
        }

        if (name_sources) {
          NameSource& source = name_sources->back();
          source.related_objects = *related_objects;
          source.text = text_alternative;
          *found_text_alternative = true;
        } else {
          return text_alternative;
        }
      }
    }
  }

  return text_alternative;
}

String AXNodeObject::Description(AXNameFrom name_from,
                                 AXDescriptionFrom& description_from,
                                 AXObjectVector* description_objects) const {
  AXRelatedObjectVector related_objects;
  String result =
      Description(name_from, description_from, nullptr, &related_objects);
  if (description_objects) {
    description_objects->clear();
    for (size_t i = 0; i < related_objects.size(); i++)
      description_objects->push_back(related_objects[i]->object);
  }

  return CollapseWhitespace(result);
}

// Based on
// http://rawgit.com/w3c/aria/master/html-aam/html-aam.html#accessible-name-and-description-calculation
String AXNodeObject::Description(AXNameFrom name_from,
                                 AXDescriptionFrom& description_from,
                                 DescriptionSources* description_sources,
                                 AXRelatedObjectVector* related_objects) const {
  // If descriptionSources is non-null, relatedObjects is used in filling it in,
  // so it must be non-null as well.
  if (description_sources)
    DCHECK(related_objects);

  if (!GetNode())
    return String();

  String description;
  bool found_description = false;

  description_from = kAXDescriptionFromRelatedElement;
  if (description_sources) {
    description_sources->push_back(
        DescriptionSource(found_description, aria_describedbyAttr));
    description_sources->back().type = description_from;
  }

  // aria-describedby overrides any other accessible description, from:
  // http://rawgit.com/w3c/aria/master/html-aam/html-aam.html
  const AtomicString& aria_describedby = GetAttribute(aria_describedbyAttr);
  if (!aria_describedby.IsNull()) {
    if (description_sources)
      description_sources->back().attribute_value = aria_describedby;

    description = TextFromAriaDescribedby(related_objects);

    if (!description.IsNull()) {
      if (description_sources) {
        DescriptionSource& source = description_sources->back();
        source.type = description_from;
        source.related_objects = *related_objects;
        source.text = description;
        found_description = true;
      } else {
        return description;
      }
    } else if (description_sources) {
      description_sources->back().invalid = true;
    }
  }

  const HTMLInputElement* input_element = nullptr;
  if (isHTMLInputElement(GetNode()))
    input_element = toHTMLInputElement(GetNode());

  // value, 5.2.2 from: http://rawgit.com/w3c/aria/master/html-aam/html-aam.html
  if (name_from != kAXNameFromValue && input_element &&
      input_element->IsTextButton()) {
    description_from = kAXDescriptionFromAttribute;
    if (description_sources) {
      description_sources->push_back(
          DescriptionSource(found_description, valueAttr));
      description_sources->back().type = description_from;
    }
    String value = input_element->value();
    if (!value.IsNull()) {
      description = value;
      if (description_sources) {
        DescriptionSource& source = description_sources->back();
        source.text = description;
        found_description = true;
      } else {
        return description;
      }
    }
  }

  // table caption, 5.9.2 from:
  // http://rawgit.com/w3c/aria/master/html-aam/html-aam.html
  if (name_from != kAXNameFromCaption && isHTMLTableElement(GetNode())) {
    HTMLTableElement* table_element = toHTMLTableElement(GetNode());

    description_from = kAXDescriptionFromRelatedElement;
    if (description_sources) {
      description_sources->push_back(DescriptionSource(found_description));
      description_sources->back().type = description_from;
      description_sources->back().native_source =
          kAXTextFromNativeHTMLTableCaption;
    }
    HTMLTableCaptionElement* caption = table_element->caption();
    if (caption) {
      AXObjectImpl* caption_ax_object = AxObjectCache().GetOrCreate(caption);
      if (caption_ax_object) {
        AXObjectSet visited;
        description =
            RecursiveTextAlternative(*caption_ax_object, false, visited);
        if (related_objects)
          related_objects->push_back(
              new NameSourceRelatedObject(caption_ax_object, description));

        if (description_sources) {
          DescriptionSource& source = description_sources->back();
          source.related_objects = *related_objects;
          source.text = description;
          found_description = true;
        } else {
          return description;
        }
      }
    }
  }

  // summary, 5.6.2 from:
  // http://rawgit.com/w3c/aria/master/html-aam/html-aam.html
  if (name_from != kAXNameFromContents && isHTMLSummaryElement(GetNode())) {
    description_from = kAXDescriptionFromContents;
    if (description_sources) {
      description_sources->push_back(DescriptionSource(found_description));
      description_sources->back().type = description_from;
    }

    AXObjectSet visited;
    description = TextFromDescendants(visited, false);

    if (!description.IsEmpty()) {
      if (description_sources) {
        found_description = true;
        description_sources->back().text = description;
      } else {
        return description;
      }
    }
  }

  // title attribute, from:
  // http://rawgit.com/w3c/aria/master/html-aam/html-aam.html
  if (name_from != kAXNameFromTitle) {
    description_from = kAXDescriptionFromAttribute;
    if (description_sources) {
      description_sources->push_back(
          DescriptionSource(found_description, titleAttr));
      description_sources->back().type = description_from;
    }
    const AtomicString& title = GetAttribute(titleAttr);
    if (!title.IsEmpty()) {
      description = title;
      if (description_sources) {
        found_description = true;
        description_sources->back().text = description;
      } else {
        return description;
      }
    }
  }

  // aria-help.
  // FIXME: this is not part of the official standard, but it's needed because
  // the built-in date/time controls use it.
  description_from = kAXDescriptionFromAttribute;
  if (description_sources) {
    description_sources->push_back(
        DescriptionSource(found_description, aria_helpAttr));
    description_sources->back().type = description_from;
  }
  const AtomicString& help = GetAttribute(aria_helpAttr);
  if (!help.IsEmpty()) {
    description = help;
    if (description_sources) {
      found_description = true;
      description_sources->back().text = description;
    } else {
      return description;
    }
  }

  description_from = kAXDescriptionFromUninitialized;

  if (found_description) {
    for (size_t i = 0; i < description_sources->size(); ++i) {
      if (!(*description_sources)[i].text.IsNull() &&
          !(*description_sources)[i].superseded) {
        DescriptionSource& description_source = (*description_sources)[i];
        description_from = description_source.type;
        if (!description_source.related_objects.IsEmpty())
          *related_objects = description_source.related_objects;
        return description_source.text;
      }
    }
  }

  return String();
}

String AXNodeObject::Placeholder(AXNameFrom name_from) const {
  if (name_from == kAXNameFromPlaceholder)
    return String();

  Node* node = GetNode();
  if (!node || !node->IsHTMLElement())
    return String();

  String native_placeholder = PlaceholderFromNativeAttribute();
  if (!native_placeholder.IsEmpty())
    return native_placeholder;

  const AtomicString& aria_placeholder =
      ToHTMLElement(node)->FastGetAttribute(aria_placeholderAttr);
  if (!aria_placeholder.IsEmpty())
    return aria_placeholder;

  return String();
}

String AXNodeObject::PlaceholderFromNativeAttribute() const {
  Node* node = GetNode();
  if (!node || !IsTextControlElement(node))
    return String();
  return ToTextControlElement(node)->StrippedPlaceholder();
}

DEFINE_TRACE(AXNodeObject) {
  visitor->Trace(node_);
  AXObjectImpl::Trace(visitor);
}

}  // namespace blink
