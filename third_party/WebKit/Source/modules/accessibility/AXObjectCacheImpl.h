/*
 * Copyright (C) 2014, Google Inc. All rights reserved.
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

#ifndef AXObjectCacheImpl_h
#define AXObjectCacheImpl_h

#include <memory>
#include "core/dom/AXObjectCacheBase.h"
#include "modules/ModulesExport.h"
#include "modules/accessibility/AXObjectImpl.h"
#include "platform/wtf/Forward.h"
#include "platform/wtf/HashMap.h"
#include "platform/wtf/HashSet.h"

namespace blink {

class AbstractInlineTextBox;
class HTMLAreaElement;
class FrameView;

// This class should only be used from inside the accessibility directory.
class MODULES_EXPORT AXObjectCacheImpl : public AXObjectCacheBase {
  WTF_MAKE_NONCOPYABLE(AXObjectCacheImpl);

 public:
  static AXObjectCache* Create(Document&);

  explicit AXObjectCacheImpl(Document&);
  virtual ~AXObjectCacheImpl();
  DECLARE_VIRTUAL_TRACE();

  AXObjectImpl* FocusedObject();

  void Dispose() override;

  void SelectionChanged(Node*) override;
  void ChildrenChanged(Node*) override;
  void ChildrenChanged(LayoutObject*) override;
  void CheckedStateChanged(Node*) override;
  virtual void ListboxOptionStateChanged(HTMLOptionElement*);
  virtual void ListboxSelectedChildrenChanged(HTMLSelectElement*);
  virtual void ListboxActiveIndexChanged(HTMLSelectElement*);
  virtual void RadiobuttonRemovedFromGroup(HTMLInputElement*);

  void Remove(LayoutObject*) override;
  void Remove(Node*) override;
  void Remove(AbstractInlineTextBox*) override;

  const Element* RootAXEditableElement(const Node*) override;

  // Called by a node when text or a text equivalent (e.g. alt) attribute is
  // changed.
  void TextChanged(LayoutObject*) override;
  void TextChanged(AXObjectImpl*);
  // Called when a node has just been attached, so we can make sure we have the
  // right subclass of AXObjectImpl.
  void UpdateCacheAfterNodeIsAttached(Node*) override;

  void HandleAttributeChanged(const QualifiedName& attr_name,
                              Element*) override;
  void HandleFocusedUIElementChanged(Node* old_focused_node,
                                     Node* new_focused_node) override;
  void HandleInitialFocus() override;
  void HandleTextFormControlChanged(Node*) override;
  void HandleEditableTextContentChanged(Node*) override;
  void HandleValueChanged(Node*) override;
  void HandleUpdateActiveMenuOption(LayoutMenuList*, int option_index) override;
  void DidShowMenuListPopup(LayoutMenuList*) override;
  void DidHideMenuListPopup(LayoutMenuList*) override;
  void HandleLoadComplete(Document*) override;
  void HandleLayoutComplete(Document*) override;
  void HandleClicked(Node*) override;

  void SetCanvasObjectBounds(HTMLCanvasElement*,
                             Element*,
                             const LayoutRect&) override;

  void InlineTextBoxesUpdated(LineLayoutItem) override;

  // Called when the scroll offset changes.
  void HandleScrollPositionChanged(FrameView*) override;
  void HandleScrollPositionChanged(LayoutObject*) override;

  // Called when scroll bars are added / removed (as the view resizes).
  void HandleLayoutComplete(LayoutObject*) override;
  void HandleScrolledToAnchor(const Node* anchor_node) override;

  const AtomicString& ComputedRoleForNode(Node*) override;
  String ComputedNameForNode(Node*) override;

  void OnTouchAccessibilityHover(const IntPoint&) override;

  // Returns the root object for the entire document.
  AXObjectImpl* RootObject();

  AXObjectImpl* ObjectFromAXID(AXID id) const { return objects_.at(id); }
  AXObjectImpl* Root();

  // used for objects without backing elements
  AXObjectImpl* GetOrCreate(AccessibilityRole);
  AXObjectImpl* GetOrCreate(LayoutObject*);
  AXObjectImpl* GetOrCreate(Node*);
  AXObjectImpl* GetOrCreate(AbstractInlineTextBox*);

  // will only return the AXObjectImpl if it already exists
  AXObjectImpl* Get(Node*) override;
  AXObjectImpl* Get(LayoutObject*);
  AXObjectImpl* Get(AbstractInlineTextBox*);

  AXObjectImpl* FirstAccessibleObjectFromNode(const Node*);

  void Remove(AXID);

  void ChildrenChanged(AXObjectImpl*);

  void HandleActiveDescendantChanged(Node*);
  void HandleAriaRoleChanged(Node*);
  void HandleAriaExpandedChange(Node*);
  void HandleAriaSelectedChanged(Node*);

  bool AccessibilityEnabled();
  bool InlineTextBoxAccessibilityEnabled();

  void RemoveAXID(AXObjectImpl*);

  AXID GenerateAXID() const;

  // Counts the number of times the document has been modified. Some attribute
  // values are cached as long as the modification count hasn't changed.
  int ModificationCount() const { return modification_count_; }

  void PostNotification(LayoutObject*, AXNotification);
  void PostNotification(Node*, AXNotification);
  void PostNotification(AXObjectImpl*, AXNotification);

  //
  // Aria-owns support.
  //

  // Returns true if the given object's position in the tree was due to
  // aria-owns.
  bool IsAriaOwned(const AXObjectImpl*) const;

  // Returns the parent of the given object due to aria-owns.
  AXObjectImpl* GetAriaOwnedParent(const AXObjectImpl*) const;

  // Given an object that has an aria-owns attributes, and a vector of ids from
  // the value of that attribute, updates the internal state to reflect the new
  // set of children owned by this object, returning the result in
  // |ownedChildren|. The result is validated - illegal, duplicate, or cyclical
  // references have been removed.
  //
  // If one or more ids aren't found, they're added to a lookup table so that if
  // an element with that id appears later, it can be added when you call
  // updateTreeIfElementIdIsAriaOwned.
  void UpdateAriaOwns(const AXObjectImpl* owner,
                      const Vector<String>& id_vector,
                      HeapVector<Member<AXObjectImpl>>& owned_children);

  // Given an element in the DOM tree that was either just added or whose id
  // just changed, check to see if another object wants to be its parent due to
  // aria-owns. If so, update the tree by calling childrenChanged() on the
  // potential owner, possibly reparenting this element.
  void UpdateTreeIfElementIdIsAriaOwned(Element*);

 protected:
  void PostPlatformNotification(AXObjectImpl*, AXNotification);
  void LabelChanged(Element*);

  AXObjectImpl* CreateFromRenderer(LayoutObject*);
  AXObjectImpl* CreateFromNode(Node*);
  AXObjectImpl* CreateFromInlineTextBox(AbstractInlineTextBox*);

 private:
  Member<Document> document_;
  HeapHashMap<AXID, Member<AXObjectImpl>> objects_;
  // LayoutObject and AbstractInlineTextBox are not on the Oilpan heap so we
  // do not use HeapHashMap for those mappings.
  HashMap<LayoutObject*, AXID> layout_object_mapping_;
  HeapHashMap<Member<Node>, AXID> node_object_mapping_;
  HashMap<AbstractInlineTextBox*, AXID> inline_text_box_object_mapping_;
  int modification_count_;

  HashSet<AXID> ids_in_use_;

#if DCHECK_IS_ON()
  // Verified when finalizing.
  bool has_been_disposed_ = false;
#endif

  //
  // Aria-owns
  //

  // Map from the AXID of the owner to the AXIDs of the children.
  // This is a validated map, it doesn't contain illegal, duplicate,
  // or cyclical matches, or references to IDs that don't exist.
  HashMap<AXID, Vector<AXID>> aria_owner_to_children_mapping_;

  // Map from the AXID of a child to the AXID of the parent that owns it.
  HashMap<AXID, AXID> aria_owned_child_to_owner_mapping_;

  // Map from the AXID of a child to the AXID of its real parent in the tree if
  // we ignored aria-owns. This is needed in case the owner no longer wants to
  // own it.
  HashMap<AXID, AXID> aria_owned_child_to_real_parent_mapping_;

  // Map from the AXID of any object with an aria-owns attribute to the set of
  // ids of its children. This is *unvalidated*, it includes ids that may not
  // currently exist in the tree.
  HashMap<AXID, HashSet<String>> aria_owner_to_ids_mapping_;

  // Map from an ID (the ID attribute of a DOM element) to the set of elements
  // that want to own that ID. This is *unvalidated*, it includes possible
  // duplicates.  This is used so that when an element with an ID is added to
  // the tree or changes its ID, we can quickly determine if it affects an
  // aria-owns relationship.
  HashMap<String, std::unique_ptr<HashSet<AXID>>> id_to_aria_owners_mapping_;

  TaskRunnerTimer<AXObjectCacheImpl> notification_post_timer_;
  HeapVector<std::pair<Member<AXObjectImpl>, AXNotification>>
      notifications_to_post_;
  void NotificationPostTimerFired(TimerBase*);

  AXObjectImpl* FocusedImageMapUIElement(HTMLAreaElement*);

  AXID GetOrCreateAXID(AXObjectImpl*);

  void TextChanged(Node*);
  bool NodeIsTextControl(const Node*);

  Settings* GetSettings();
};

// This is the only subclass of AXObjectCache.
DEFINE_TYPE_CASTS(AXObjectCacheImpl, AXObjectCache, cache, true, true);

bool NodeHasRole(Node*, const String& role);
// This will let you know if aria-hidden was explicitly set to false.
bool IsNodeAriaVisible(Node*);

}  // namespace blink

#endif
