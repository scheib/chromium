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

#ifndef AXNodeObject_h
#define AXNodeObject_h

#include "modules/ModulesExport.h"
#include "modules/accessibility/AXObjectImpl.h"
#include "platform/wtf/Forward.h"

namespace blink {

class AXObjectCacheImpl;
class Element;
class HTMLLabelElement;
class Node;

class MODULES_EXPORT AXNodeObject : public AXObjectImpl {
  WTF_MAKE_NONCOPYABLE(AXNodeObject);

 protected:
  AXNodeObject(Node*, AXObjectCacheImpl&);

 public:
  static AXNodeObject* Create(Node*, AXObjectCacheImpl&);
  ~AXNodeObject() override;
  DECLARE_VIRTUAL_TRACE();

 protected:
  // Protected data.
  AccessibilityRole aria_role_;
  bool children_dirty_;
#if DCHECK_IS_ON()
  bool initialized_ = false;
#endif

  bool ComputeAccessibilityIsIgnored(IgnoredReasons* = nullptr) const override;
  const AXObjectImpl* InheritsPresentationalRoleFrom() const override;
  virtual AccessibilityRole DetermineAccessibilityRole();
  virtual AccessibilityRole NativeAccessibilityRoleIgnoringAria() const;
  String AccessibilityDescriptionForElements(
      HeapVector<Member<Element>>& elements) const;
  void AlterSliderValue(bool increase);
  AXObjectImpl* ActiveDescendant() override;
  String AriaAccessibilityDescription() const;
  String AriaAutoComplete() const;
  AccessibilityRole DetermineAriaRoleAttribute() const;
  void AccessibilityChildrenFromAttribute(QualifiedName attr,
                                          AXObjectImpl::AXObjectVector&) const;

  bool HasContentEditableAttributeSet() const;
  bool IsTextControl() const override;
  // This returns true if it's focusable but it's not content editable and it's
  // not a control or ARIA control.
  bool IsGenericFocusableElement() const;
  AXObjectImpl* MenuButtonForMenu() const;
  Element* MenuItemElementForMenu() const;
  Element* MouseButtonListener() const;
  AccessibilityRole RemapAriaRoleDueToParent(AccessibilityRole) const;
  bool IsNativeCheckboxOrRadio() const;
  void SetNode(Node*);
  AXObjectImpl* CorrespondingControlForLabelElement() const;
  HTMLLabelElement* LabelElementContainer() const;

  //
  // Overridden from AXObjectImpl.
  //

  void Init() override;
  void Detach() override;
  bool IsDetached() const override { return !node_; }
  bool IsAXNodeObject() const final { return true; }

  void GetSparseAXAttributes(AXSparseAttributeClient&) const override;

  // Check object role or purpose.
  bool IsAnchor() const final;
  bool IsControl() const override;
  bool IsControllingVideoElement() const;
  bool IsEditable() const override { return IsNativeTextControl(); }
  bool IsEmbeddedObject() const final;
  bool IsFieldset() const final;
  bool IsHeading() const final;
  bool IsHovered() const final;
  bool IsImage() const final;
  bool IsImageButton() const;
  bool IsInputImage() const final;
  bool IsLink() const override;
  bool IsInPageLinkTarget() const override;
  bool IsMenu() const final;
  bool IsMenuButton() const final;
  bool IsMeter() const final;
  bool IsMultiSelectable() const override;
  bool IsNativeImage() const;
  bool IsNativeTextControl() const final;
  bool IsNonNativeTextControl() const final;
  bool IsPasswordField() const final;
  bool IsProgressIndicator() const override;
  bool IsRichlyEditable() const override;
  bool IsSlider() const override;
  bool IsNativeSlider() const override;

  // Check object state.
  bool IsClickable() const final;
  bool IsEnabled() const override;
  AccessibilityExpanded IsExpanded() const override;
  bool IsModal() const final;
  bool IsPressed() const final;
  bool IsReadOnly() const override;
  bool IsRequired() const final;

  // Check whether certain properties can be modified.
  bool CanSetFocusAttribute() const override;
  bool CanSetValueAttribute() const override;
  bool CanSetSelectedAttribute() const override;

  // Properties of static elements.
  RGBA32 ColorValue() const final;
  bool CanvasHasFallbackContent() const final;
  int HeadingLevel() const final;
  unsigned HierarchicalLevel() const final;
  void Markers(Vector<DocumentMarker::MarkerType>&,
               Vector<AXRange>&) const override;
  AXObjectImpl* InPageLinkTarget() const override;
  AccessibilityOrientation Orientation() const override;
  AXObjectVector RadioButtonsInGroup() const override;
  static HeapVector<Member<HTMLInputElement>> FindAllRadioButtonsWithSameName(
      HTMLInputElement* radio_button);
  String GetText() const override;

  // Properties of interactive elements.
  AriaCurrentState GetAriaCurrentState() const final;
  InvalidState GetInvalidState() const final;
  // Only used when invalidState() returns InvalidStateOther.
  String AriaInvalidValue() const final;
  String ValueDescription() const override;
  float ValueForRange() const override;
  float MaxValueForRange() const override;
  float MinValueForRange() const override;
  String StringValue() const override;

  // ARIA attributes.
  AccessibilityRole AriaRoleAttribute() const final;

  // AX name calculation.
  String TextAlternative(bool recursive,
                         bool in_aria_labelled_by_traversal,
                         AXObjectSet& visited,
                         AXNameFrom&,
                         AXRelatedObjectVector*,
                         NameSources*) const override;
  String Description(AXNameFrom,
                     AXDescriptionFrom&,
                     AXObjectVector* description_objects) const override;
  String Description(AXNameFrom,
                     AXDescriptionFrom&,
                     DescriptionSources*,
                     AXRelatedObjectVector*) const override;
  String Placeholder(AXNameFrom) const override;
  bool NameFromLabelElement() const override;

  // Location
  void GetRelativeBounds(AXObjectImpl** out_container,
                         FloatRect& out_bounds_in_container,
                         SkMatrix44& out_container_transform) const override;

  // High-level accessibility tree access.
  AXObjectImpl* ComputeParent() const override;
  AXObjectImpl* ComputeParentIfExists() const override;

  // Low-level accessibility tree exploration.
  AXObjectImpl* RawFirstChild() const override;
  AXObjectImpl* RawNextSibling() const override;
  void AddChildren() override;
  bool CanHaveChildren() const override;
  void AddChild(AXObjectImpl*);
  void InsertChild(AXObjectImpl*, unsigned index);

  // DOM and Render tree access.
  Element* ActionElement() const override;
  Element* AnchorElement() const override;
  Document* GetDocument() const override;
  Node* GetNode() const override { return node_; }

  // Modify or take an action on an object.
  void SetFocused(bool) final;
  void Increment() final;
  void Decrement() final;
  void SetSequentialFocusNavigationStartingPoint() final;

  // Notifications that this object may have changed.
  void ChildrenChanged() override;
  void SelectionChanged() final;
  void TextChanged() override;
  void UpdateAccessibilityRole() final;

  // Position in set and Size of set
  int PosInSet() const override;
  int SetSize() const override;

  // Aria-owns.
  void ComputeAriaOwnsChildren(
      HeapVector<Member<AXObjectImpl>>& owned_children) const;

 private:
  Member<Node> node_;

  bool IsNativeCheckboxInMixedState() const;
  String TextFromDescendants(AXObjectSet& visited,
                             bool recursive) const override;
  String NativeTextAlternative(AXObjectSet& visited,
                               AXNameFrom&,
                               AXRelatedObjectVector*,
                               NameSources*,
                               bool* found_text_alternative) const;
  float StepValueForRange() const;
  bool IsDescendantOfElementType(HashSet<QualifiedName>& tag_names) const;
  String PlaceholderFromNativeAttribute() const;
};

}  // namespace blink

#endif  // AXNodeObject_h
