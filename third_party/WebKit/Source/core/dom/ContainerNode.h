/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2009, 2010, 2011, 2013 Apple Inc. All
 * rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef ContainerNode_h
#define ContainerNode_h

#include "bindings/core/v8/ExceptionState.h"
#include "core/CoreExport.h"
#include "core/dom/Node.h"
#include "core/html/CollectionType.h"
#include "platform/bindings/ScriptWrappableVisitor.h"
#include "platform/wtf/Vector.h"
#include "public/platform/WebFocusType.h"

namespace blink {

class ClassCollection;
class ExceptionState;
class FloatPoint;
class HTMLCollection;
class NameNodeList;
using StaticElementList = StaticNodeTypeList<Element>;
class RadioNodeList;

enum DynamicRestyleFlags {
  kChildrenOrSiblingsAffectedByFocus = 1 << 0,
  kChildrenOrSiblingsAffectedByHover = 1 << 1,
  kChildrenOrSiblingsAffectedByActive = 1 << 2,
  kChildrenOrSiblingsAffectedByDrag = 1 << 3,
  kChildrenAffectedByFirstChildRules = 1 << 4,
  kChildrenAffectedByLastChildRules = 1 << 5,
  kChildrenAffectedByDirectAdjacentRules = 1 << 6,
  kChildrenAffectedByIndirectAdjacentRules = 1 << 7,
  kChildrenAffectedByForwardPositionalRules = 1 << 8,
  kChildrenAffectedByBackwardPositionalRules = 1 << 9,
  kAffectedByFirstChildRules = 1 << 10,
  kAffectedByLastChildRules = 1 << 11,
  kChildrenOrSiblingsAffectedByFocusWithin = 1 << 12,

  kNumberOfDynamicRestyleFlags = 13,

  kChildrenAffectedByStructuralRules =
      kChildrenAffectedByFirstChildRules | kChildrenAffectedByLastChildRules |
      kChildrenAffectedByDirectAdjacentRules |
      kChildrenAffectedByIndirectAdjacentRules |
      kChildrenAffectedByForwardPositionalRules |
      kChildrenAffectedByBackwardPositionalRules
};

enum SubtreeModificationAction {
  kDispatchSubtreeModifiedEvent,
  kOmitSubtreeModifiedEvent
};

// This constant controls how much buffer is initially allocated
// for a Node Vector that is used to store child Nodes of a given Node.
// FIXME: Optimize the value.
const int kInitialNodeVectorSize = 11;
using NodeVector = HeapVector<Member<Node>, kInitialNodeVectorSize>;

// Note: while ContainerNode itself isn't web-exposed, a number of methods it
// implements (such as firstChild, lastChild) use web-style naming to shadow
// the corresponding methods on Node. This is a performance optimization, as it
// avoids a virtual dispatch if the type is statically known to be
// ContainerNode.
class CORE_EXPORT ContainerNode : public Node {
 public:
  ~ContainerNode() override;

  Node* firstChild() const { return first_child_; }
  Node* lastChild() const { return last_child_; }
  bool HasChildren() const { return first_child_; }

  bool HasOneChild() const {
    return first_child_ && !first_child_->nextSibling();
  }
  bool HasOneTextChild() const {
    return HasOneChild() && first_child_->IsTextNode();
  }
  bool HasChildCount(unsigned) const;

  HTMLCollection* Children();

  unsigned CountChildren() const;

  Element* QuerySelector(const AtomicString& selectors,
                         ExceptionState& = ASSERT_NO_EXCEPTION);
  StaticElementList* QuerySelectorAll(const AtomicString& selectors,
                                      ExceptionState& = ASSERT_NO_EXCEPTION);

  Node* InsertBefore(Node* new_child,
                     Node* ref_child,
                     ExceptionState& = ASSERT_NO_EXCEPTION);
  Node* ReplaceChild(Node* new_child,
                     Node* old_child,
                     ExceptionState& = ASSERT_NO_EXCEPTION);
  Node* RemoveChild(Node* child, ExceptionState& = ASSERT_NO_EXCEPTION);
  Node* AppendChild(Node* new_child, ExceptionState& = ASSERT_NO_EXCEPTION);

  Element* getElementById(const AtomicString& id) const;
  HTMLCollection* getElementsByTagName(const AtomicString&);
  HTMLCollection* getElementsByTagNameNS(const AtomicString& namespace_uri,
                                         const AtomicString& local_name);
  NameNodeList* getElementsByName(const AtomicString& element_name);
  ClassCollection* getElementsByClassName(const AtomicString& class_names);
  RadioNodeList* GetRadioNodeList(const AtomicString&,
                                  bool only_match_img_elements = false);

  // These methods are only used during parsing.
  // They don't send DOM mutation events or accept DocumentFragments.
  void ParserAppendChild(Node*);
  void ParserRemoveChild(Node&);
  void ParserInsertBefore(Node* new_child, Node& ref_child);
  void ParserTakeAllChildrenFrom(ContainerNode&);

  void RemoveChildren(
      SubtreeModificationAction = kDispatchSubtreeModifiedEvent);

  void CloneChildNodes(ContainerNode* clone);

  void AttachLayoutTree(const AttachContext& = AttachContext()) override;
  void DetachLayoutTree(const AttachContext& = AttachContext()) override;
  LayoutRect BoundingBox() const final;
  void SetFocused(bool, WebFocusType) override;
  void SetHasFocusWithinUpToAncestor(bool, Node* ancestor);
  void FocusStateChanged();
  void FocusWithinStateChanged();
  void SetActive(bool = true) override;
  void SetDragged(bool) override;
  void SetHovered(bool = true) override;

  bool ChildrenOrSiblingsAffectedByFocus() const {
    return HasRestyleFlag(kChildrenOrSiblingsAffectedByFocus);
  }
  void SetChildrenOrSiblingsAffectedByFocus() {
    SetRestyleFlag(kChildrenOrSiblingsAffectedByFocus);
  }

  bool ChildrenOrSiblingsAffectedByFocusWithin() const {
    return HasRestyleFlag(kChildrenOrSiblingsAffectedByFocusWithin);
  }
  void SetChildrenOrSiblingsAffectedByFocusWithin() {
    SetRestyleFlag(kChildrenOrSiblingsAffectedByFocusWithin);
  }

  bool ChildrenOrSiblingsAffectedByHover() const {
    return HasRestyleFlag(kChildrenOrSiblingsAffectedByHover);
  }
  void SetChildrenOrSiblingsAffectedByHover() {
    SetRestyleFlag(kChildrenOrSiblingsAffectedByHover);
  }

  bool ChildrenOrSiblingsAffectedByActive() const {
    return HasRestyleFlag(kChildrenOrSiblingsAffectedByActive);
  }
  void SetChildrenOrSiblingsAffectedByActive() {
    SetRestyleFlag(kChildrenOrSiblingsAffectedByActive);
  }

  bool ChildrenOrSiblingsAffectedByDrag() const {
    return HasRestyleFlag(kChildrenOrSiblingsAffectedByDrag);
  }
  void SetChildrenOrSiblingsAffectedByDrag() {
    SetRestyleFlag(kChildrenOrSiblingsAffectedByDrag);
  }

  bool ChildrenAffectedByFirstChildRules() const {
    return HasRestyleFlag(kChildrenAffectedByFirstChildRules);
  }
  void SetChildrenAffectedByFirstChildRules() {
    SetRestyleFlag(kChildrenAffectedByFirstChildRules);
  }

  bool ChildrenAffectedByLastChildRules() const {
    return HasRestyleFlag(kChildrenAffectedByLastChildRules);
  }
  void SetChildrenAffectedByLastChildRules() {
    SetRestyleFlag(kChildrenAffectedByLastChildRules);
  }

  bool ChildrenAffectedByDirectAdjacentRules() const {
    return HasRestyleFlag(kChildrenAffectedByDirectAdjacentRules);
  }
  void SetChildrenAffectedByDirectAdjacentRules() {
    SetRestyleFlag(kChildrenAffectedByDirectAdjacentRules);
  }

  bool ChildrenAffectedByIndirectAdjacentRules() const {
    return HasRestyleFlag(kChildrenAffectedByIndirectAdjacentRules);
  }
  void SetChildrenAffectedByIndirectAdjacentRules() {
    SetRestyleFlag(kChildrenAffectedByIndirectAdjacentRules);
  }

  bool ChildrenAffectedByForwardPositionalRules() const {
    return HasRestyleFlag(kChildrenAffectedByForwardPositionalRules);
  }
  void SetChildrenAffectedByForwardPositionalRules() {
    SetRestyleFlag(kChildrenAffectedByForwardPositionalRules);
  }

  bool ChildrenAffectedByBackwardPositionalRules() const {
    return HasRestyleFlag(kChildrenAffectedByBackwardPositionalRules);
  }
  void SetChildrenAffectedByBackwardPositionalRules() {
    SetRestyleFlag(kChildrenAffectedByBackwardPositionalRules);
  }

  bool AffectedByFirstChildRules() const {
    return HasRestyleFlag(kAffectedByFirstChildRules);
  }
  void SetAffectedByFirstChildRules() {
    SetRestyleFlag(kAffectedByFirstChildRules);
  }

  bool AffectedByLastChildRules() const {
    return HasRestyleFlag(kAffectedByLastChildRules);
  }
  void SetAffectedByLastChildRules() {
    SetRestyleFlag(kAffectedByLastChildRules);
  }

  bool NeedsAdjacentStyleRecalc() const;

  // FIXME: These methods should all be renamed to something better than
  // "check", since it's not clear that they alter the style bits of siblings
  // and children.
  enum SiblingCheckType {
    kFinishedParsingChildren,
    kSiblingElementInserted,
    kSiblingElementRemoved
  };
  void CheckForSiblingStyleChanges(SiblingCheckType,
                                   Element* changed_element,
                                   Node* node_before_change,
                                   Node* node_after_change);
  void RecalcDescendantStyles(StyleRecalcChange);
  void RebuildChildrenLayoutTrees(Text*& next_text_sibling);
  void RebuildLayoutTreeForChild(Node* child, Text*& next_text_sibling);

  bool ChildrenSupportStyleSharing() const { return !HasRestyleFlags(); }

  // -----------------------------------------------------------------------------
  // Notification of document structure changes (see core/dom/Node.h for more
  // notification methods)

  enum ChildrenChangeType {
    kElementInserted,
    kNonElementInserted,
    kElementRemoved,
    kNonElementRemoved,
    kAllChildrenRemoved,
    kTextChanged
  };
  enum ChildrenChangeSource {
    kChildrenChangeSourceAPI,
    kChildrenChangeSourceParser
  };
  struct ChildrenChange {
    STACK_ALLOCATED();

   public:
    static ChildrenChange ForInsertion(Node& node,
                                       Node* unchanged_previous,
                                       Node* unchanged_next,
                                       ChildrenChangeSource by_parser) {
      ChildrenChange change = {
          node.IsElementNode() ? kElementInserted : kNonElementInserted, &node,
          unchanged_previous, unchanged_next, by_parser};
      return change;
    }

    static ChildrenChange ForRemoval(Node& node,
                                     Node* previous_sibling,
                                     Node* next_sibling,
                                     ChildrenChangeSource by_parser) {
      ChildrenChange change = {
          node.IsElementNode() ? kElementRemoved : kNonElementRemoved, &node,
          previous_sibling, next_sibling, by_parser};
      return change;
    }

    bool IsChildInsertion() const {
      return type == kElementInserted || type == kNonElementInserted;
    }
    bool IsChildRemoval() const {
      return type == kElementRemoved || type == kNonElementRemoved;
    }
    bool IsChildElementChange() const {
      return type == kElementInserted || type == kElementRemoved;
    }

    ChildrenChangeType type;
    Member<Node> sibling_changed;
    // |siblingBeforeChange| is
    //  - siblingChanged.previousSibling before node removal
    //  - siblingChanged.previousSibling after single node insertion
    //  - previousSibling of the first inserted node after multiple node
    //    insertion
    Member<Node> sibling_before_change;
    // |siblingAfterChange| is
    //  - siblingChanged.nextSibling before node removal
    //  - siblingChanged.nextSibling after single node insertion
    //  - nextSibling of the last inserted node after multiple node insertion.
    Member<Node> sibling_after_change;
    ChildrenChangeSource by_parser;
  };

  // Notifies the node that it's list of children have changed (either by adding
  // or removing child nodes), or a child node that is of the type
  // CDATA_SECTION_NODE, TEXT_NODE or COMMENT_NODE has changed its value.
  virtual void ChildrenChanged(const ChildrenChange&);

  DECLARE_VIRTUAL_TRACE();

  DECLARE_VIRTUAL_TRACE_WRAPPERS();

 protected:
  ContainerNode(TreeScope*, ConstructionType = kCreateContainer);

  void InvalidateNodeListCachesInAncestors(
      const QualifiedName* attr_name = nullptr,
      Element* attribute_owner_element = nullptr);

  void SetFirstChild(Node* child) {
    first_child_ = child;
    ScriptWrappableVisitor::WriteBarrier(this, first_child_);
  }
  void SetLastChild(Node* child) {
    last_child_ = child;
    ScriptWrappableVisitor::WriteBarrier(this, last_child_);
  }

  // Utility functions for NodeListsNodeData API.
  template <typename Collection>
  Collection* EnsureCachedCollection(CollectionType);
  template <typename Collection>
  Collection* EnsureCachedCollection(CollectionType, const AtomicString& name);
  template <typename Collection>
  Collection* EnsureCachedCollection(CollectionType,
                                     const AtomicString& namespace_uri,
                                     const AtomicString& local_name);
  template <typename Collection>
  Collection* CachedCollection(CollectionType);

 private:
  bool IsContainerNode() const =
      delete;  // This will catch anyone doing an unnecessary check.
  bool IsTextNode() const =
      delete;  // This will catch anyone doing an unnecessary check.

  NodeListsNodeData& EnsureNodeLists();
  void RemoveBetween(Node* previous_child, Node* next_child, Node& old_child);
  // Inserts the specified nodes before |next|.
  // |next| may be nullptr.
  // |post_insertion_notification_targets| must not be nullptr.
  template <typename Functor>
  void InsertNodeVector(const NodeVector&,
                        Node* next,
                        const Functor&,
                        NodeVector* post_insertion_notification_targets);
  void DidInsertNodeVector(
      const NodeVector&,
      Node* next,
      const NodeVector& post_insertion_notification_targets);
  class AdoptAndInsertBefore;
  class AdoptAndAppendChild;
  friend class AdoptAndInsertBefore;
  friend class AdoptAndAppendChild;
  void InsertBeforeCommon(Node& next_child, Node& new_child);
  void AppendChildCommon(Node& child);
  void WillRemoveChildren();
  void WillRemoveChild(Node& child);
  void RemoveDetachedChildrenInContainer(ContainerNode&);
  void AddChildNodesToDeletionQueue(Node*&, Node*&, ContainerNode&);

  void NotifyNodeInserted(Node&,
                          ChildrenChangeSource = kChildrenChangeSourceAPI);
  void NotifyNodeInsertedInternal(
      Node&,
      NodeVector& post_insertion_notification_targets);
  void NotifyNodeRemoved(Node&);

  bool HasRestyleFlag(DynamicRestyleFlags mask) const {
    return HasRareData() && HasRestyleFlagInternal(mask);
  }
  bool HasRestyleFlags() const {
    return HasRareData() && HasRestyleFlagsInternal();
  }
  void SetRestyleFlag(DynamicRestyleFlags);
  bool HasRestyleFlagInternal(DynamicRestyleFlags) const;
  bool HasRestyleFlagsInternal() const;

  bool CollectChildrenAndRemoveFromOldParentWithCheck(const Node* next,
                                                      const Node* old_child,
                                                      Node& new_child,
                                                      NodeVector&,
                                                      ExceptionState&) const;
  inline bool CheckAcceptChildGuaranteedNodeTypes(const Node& new_child,
                                                  const Node* old_child,
                                                  ExceptionState&) const;
  inline bool CheckAcceptChild(const Node* new_child,
                               const Node* old_child,
                               ExceptionState&) const;
  inline bool CheckParserAcceptChild(const Node& new_child) const;
  inline bool ContainsConsideringHostElements(const Node&) const;
  inline bool IsChildTypeAllowed(const Node& child) const;

  bool GetUpperLeftCorner(FloatPoint&) const;
  bool GetLowerRightCorner(FloatPoint&) const;

  Member<Node> first_child_;
  Member<Node> last_child_;
};

#if DCHECK_IS_ON()
bool ChildAttachedAllowedWhenAttachingChildren(ContainerNode*);
#endif

WILL_NOT_BE_EAGERLY_TRACED_CLASS(ContainerNode);

DEFINE_NODE_TYPE_CASTS(ContainerNode, IsContainerNode());

inline bool ContainerNode::HasChildCount(unsigned count) const {
  Node* child = first_child_;
  while (count && child) {
    child = child->nextSibling();
    --count;
  }
  return !count && !child;
}

inline ContainerNode::ContainerNode(TreeScope* tree_scope,
                                    ConstructionType type)
    : Node(tree_scope, type), first_child_(nullptr), last_child_(nullptr) {}

inline bool ContainerNode::NeedsAdjacentStyleRecalc() const {
  if (!ChildrenAffectedByDirectAdjacentRules() &&
      !ChildrenAffectedByIndirectAdjacentRules())
    return false;
  return ChildNeedsStyleRecalc() || ChildNeedsStyleInvalidation();
}

inline unsigned Node::CountChildren() const {
  if (!IsContainerNode())
    return 0;
  return ToContainerNode(this)->CountChildren();
}

inline Node* Node::firstChild() const {
  if (!IsContainerNode())
    return nullptr;
  return ToContainerNode(this)->firstChild();
}

inline Node* Node::lastChild() const {
  if (!IsContainerNode())
    return nullptr;
  return ToContainerNode(this)->lastChild();
}

inline ContainerNode* Node::ParentElementOrShadowRoot() const {
  ContainerNode* parent = parentNode();
  return parent && (parent->IsElementNode() || parent->IsShadowRoot())
             ? parent
             : nullptr;
}

inline ContainerNode* Node::ParentElementOrDocumentFragment() const {
  ContainerNode* parent = parentNode();
  return parent && (parent->IsElementNode() || parent->IsDocumentFragment())
             ? parent
             : nullptr;
}

inline bool Node::IsTreeScope() const {
  return &GetTreeScope().RootNode() == this;
}

inline void GetChildNodes(ContainerNode& node, NodeVector& nodes) {
  DCHECK(!nodes.size());
  for (Node* child = node.firstChild(); child; child = child->nextSibling())
    nodes.push_back(child);
}

}  // namespace blink

#endif  // ContainerNode_h
