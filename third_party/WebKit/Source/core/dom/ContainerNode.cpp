/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2013 Apple Inc. All rights
 * reserved.
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
 */

#include "core/dom/ContainerNode.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/dom/ChildFrameDisconnector.h"
#include "core/dom/ChildListMutationScope.h"
#include "core/dom/ClassCollection.h"
#include "core/dom/ElementTraversal.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/NameNodeList.h"
#include "core/dom/NodeChildRemovalTracker.h"
#include "core/dom/NodeComputedStyle.h"
#include "core/dom/NodeRareData.h"
#include "core/dom/NodeTraversal.h"
#include "core/dom/SelectorQuery.h"
#include "core/dom/StaticNodeList.h"
#include "core/dom/StyleChangeReason.h"
#include "core/dom/StyleEngine.h"
#include "core/dom/shadow/ElementShadow.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "core/events/MutationEvent.h"
#include "core/frame/FrameView.h"
#include "core/html/HTMLCollection.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/html/HTMLTagCollection.h"
#include "core/html/RadioNodeList.h"
#include "core/layout/LayoutBlockFlow.h"
#include "core/layout/LayoutInline.h"
#include "core/layout/LayoutText.h"
#include "core/layout/LayoutTheme.h"
#include "core/layout/line/InlineTextBox.h"
#include "core/layout/line/RootInlineBox.h"
#include "core/probe/CoreProbes.h"
#include "platform/EventDispatchForbiddenScope.h"
#include "platform/ScriptForbiddenScope.h"

namespace blink {

using namespace HTMLNames;

static void DispatchChildInsertionEvents(Node&);
static void DispatchChildRemovalEvents(Node&);

// This dispatches various events; DOM mutation events, blur events, IFRAME
// unload events, etc.
static inline void CollectChildrenAndRemoveFromOldParent(
    Node& node,
    NodeVector& nodes,
    ExceptionState& exception_state) {
  if (node.IsDocumentFragment()) {
    DocumentFragment& fragment = ToDocumentFragment(node);
    GetChildNodes(fragment, nodes);
    fragment.RemoveChildren();
    return;
  }
  nodes.push_back(&node);
  if (ContainerNode* old_parent = node.parentNode())
    old_parent->RemoveChild(&node, exception_state);
}

void ContainerNode::ParserTakeAllChildrenFrom(ContainerNode& old_parent) {
  while (Node* child = old_parent.firstChild()) {
    // Explicitly remove since appending can fail, but this loop shouldn't be
    // infinite.
    old_parent.ParserRemoveChild(*child);
    ParserAppendChild(child);
  }
}

ContainerNode::~ContainerNode() {
  DCHECK(NeedsAttach());
}

DISABLE_CFI_PERF
bool ContainerNode::IsChildTypeAllowed(const Node& child) const {
  if (!child.IsDocumentFragment())
    return ChildTypeAllowed(child.getNodeType());

  for (Node* node = ToDocumentFragment(child).firstChild(); node;
       node = node->nextSibling()) {
    if (!ChildTypeAllowed(node->getNodeType()))
      return false;
  }
  return true;
}

bool ContainerNode::ContainsConsideringHostElements(
    const Node& new_child) const {
  if (IsInShadowTree() || GetDocument().IsTemplateDocument())
    return new_child.ContainsIncludingHostElements(*this);
  return new_child.contains(this);
}

DISABLE_CFI_PERF
bool ContainerNode::CheckAcceptChild(const Node* new_child,
                                     const Node* old_child,
                                     ExceptionState& exception_state) const {
  // Not mentioned in spec: throw NotFoundError if newChild is null
  if (!new_child) {
    exception_state.ThrowDOMException(kNotFoundError,
                                      "The new child element is null.");
    return false;
  }

  // Use common case fast path if possible.
  if ((new_child->IsElementNode() || new_child->IsTextNode()) &&
      IsElementNode()) {
    DCHECK(IsChildTypeAllowed(*new_child));
    if (ContainsConsideringHostElements(*new_child)) {
      exception_state.ThrowDOMException(
          kHierarchyRequestError, "The new child element contains the parent.");
      return false;
    }
    return true;
  }

  // This should never happen, but also protect release builds from tree
  // corruption.
  DCHECK(!new_child->IsPseudoElement());
  if (new_child->IsPseudoElement()) {
    exception_state.ThrowDOMException(
        kHierarchyRequestError, "The new child element is a pseudo-element.");
    return false;
  }

  return CheckAcceptChildGuaranteedNodeTypes(*new_child, old_child,
                                             exception_state);
}

bool ContainerNode::CheckAcceptChildGuaranteedNodeTypes(
    const Node& new_child,
    const Node* old_child,
    ExceptionState& exception_state) const {
  if (IsDocumentNode())
    return ToDocument(this)->CanAcceptChild(new_child, old_child,
                                            exception_state);
  // Skip containsIncludingHostElements() if !newChild.parentNode() &&
  // isConnected(). |newChild| typically has no parentNode(), and it means
  // it's !isConnected(). In such case, the contains check for connected
  // |this| is unnecessary.
  if (new_child.IsContainerNode() &&
      (new_child.IsDocumentNode() || new_child.parentNode() ||
       !isConnected()) &&
      new_child.ContainsIncludingHostElements(*this)) {
    exception_state.ThrowDOMException(
        kHierarchyRequestError, "The new child element contains the parent.");
    return false;
  }
  if (!IsChildTypeAllowed(new_child)) {
    exception_state.ThrowDOMException(
        kHierarchyRequestError,
        "Nodes of type '" + new_child.nodeName() +
            "' may not be inserted inside nodes of type '" + nodeName() + "'.");
    return false;
  }
  return true;
}

bool ContainerNode::CollectChildrenAndRemoveFromOldParentWithCheck(
    const Node* next,
    const Node* old_child,
    Node& new_child,
    NodeVector& new_children,
    ExceptionState& exception_state) const {
  CollectChildrenAndRemoveFromOldParent(new_child, new_children,
                                        exception_state);
  if (exception_state.HadException() || new_children.IsEmpty())
    return false;

  // We need this extra check because collectChildrenAndRemoveFromOldParent()
  // can fire various events.
  for (const auto& child : new_children) {
    if (child->parentNode()) {
      // A new child was added to another parent before adding to this
      // node.  Firefox and Edge don't throw in this case.
      return false;
    }
    if (!CheckAcceptChildGuaranteedNodeTypes(*child, old_child,
                                             exception_state))
      return false;
  }
  if (next && next->parentNode() != this) {
    exception_state.ThrowDOMException(
        kNotFoundError,
        "The node before which the new node is to "
        "be inserted is not a child of this "
        "node.");
    return false;
  }
  return true;
}

template <typename Functor>
void ContainerNode::InsertNodeVector(
    const NodeVector& targets,
    Node* next,
    const Functor& mutator,
    NodeVector* post_insertion_notification_targets) {
  DCHECK(post_insertion_notification_targets);
  probe::willInsertDOMNode(this);
  {
    EventDispatchForbiddenScope assert_no_event_dispatch;
    ScriptForbiddenScope forbid_script;
    for (const auto& target_node : targets) {
      DCHECK(target_node);
      DCHECK(!target_node->parentNode());
      Node& child = *target_node;
      mutator(*this, child, next);
      ChildListMutationScope(*this).ChildAdded(child);
      if (GetDocument().ContainsV1ShadowTree())
        child.CheckSlotChangeAfterInserted();
      probe::didInsertDOMNode(&child);
      NotifyNodeInsertedInternal(child, *post_insertion_notification_targets);
    }
  }
}

void ContainerNode::DidInsertNodeVector(
    const NodeVector& targets,
    Node* next,
    const NodeVector& post_insertion_notification_targets) {
  Node* unchanged_previous =
      targets.size() > 0 ? targets[0]->previousSibling() : nullptr;
  for (const auto& target_node : targets) {
    ChildrenChanged(ChildrenChange::ForInsertion(
        *target_node, unchanged_previous, next, kChildrenChangeSourceAPI));
  }
  for (const auto& descendant : post_insertion_notification_targets) {
    if (descendant->isConnected())
      descendant->DidNotifySubtreeInsertionsToDocument();
  }
  for (const auto& target_node : targets) {
    if (target_node->parentNode() == this)
      DispatchChildInsertionEvents(*target_node);
  }
  DispatchSubtreeModifiedEvent();
}

class ContainerNode::AdoptAndInsertBefore {
 public:
  inline void operator()(ContainerNode& container,
                         Node& child,
                         Node* next) const {
    DCHECK(next);
    DCHECK_EQ(next->parentNode(), &container);
    container.GetTreeScope().AdoptIfNeeded(child);
    container.InsertBeforeCommon(*next, child);
  }
};

class ContainerNode::AdoptAndAppendChild {
 public:
  inline void operator()(ContainerNode& container, Node& child, Node*) const {
    container.GetTreeScope().AdoptIfNeeded(child);
    container.AppendChildCommon(child);
  }
};

Node* ContainerNode::InsertBefore(Node* new_child,
                                  Node* ref_child,
                                  ExceptionState& exception_state) {
  // https://dom.spec.whatwg.org/#concept-node-pre-insert

  // insertBefore(node, 0) is equivalent to appendChild(node)
  if (!ref_child)
    return AppendChild(new_child, exception_state);

  // NotFoundError: Raised if refChild is not a child of this node
  if (ref_child->parentNode() != this) {
    exception_state.ThrowDOMException(
        kNotFoundError,
        "The node before which the new node is to "
        "be inserted is not a child of this "
        "node.");
    return nullptr;
  }

  // 3. If reference child is node, set it to node’s next sibling.
  if (ref_child == new_child) {
    ref_child = new_child->nextSibling();
    if (!ref_child)
      return AppendChild(new_child, exception_state);
  }

  // Make sure adding the new child is OK.
  if (!CheckAcceptChild(new_child, 0, exception_state))
    return new_child;
  DCHECK(new_child);

  NodeVector targets;
  if (!CollectChildrenAndRemoveFromOldParentWithCheck(
          ref_child, nullptr, *new_child, targets, exception_state))
    return new_child;

  NodeVector post_insertion_notification_targets;
  {
    ChildListMutationScope mutation(*this);
    InsertNodeVector(targets, ref_child, AdoptAndInsertBefore(),
                     &post_insertion_notification_targets);
  }
  DidInsertNodeVector(targets, ref_child, post_insertion_notification_targets);
  return new_child;
}

void ContainerNode::InsertBeforeCommon(Node& next_child, Node& new_child) {
#if DCHECK_IS_ON()
  DCHECK(EventDispatchForbiddenScope::IsEventDispatchForbidden());
#endif
  DCHECK(ScriptForbiddenScope::IsScriptForbidden());
  // Use insertBefore if you need to handle reparenting (and want DOM mutation
  // events).
  DCHECK(!new_child.parentNode());
  DCHECK(!new_child.nextSibling());
  DCHECK(!new_child.previousSibling());
  DCHECK(!new_child.IsShadowRoot());

  Node* prev = next_child.previousSibling();
  DCHECK_NE(last_child_, prev);
  next_child.SetPreviousSibling(&new_child);
  if (prev) {
    DCHECK_NE(firstChild(), next_child);
    DCHECK_EQ(prev->nextSibling(), next_child);
    prev->SetNextSibling(&new_child);
  } else {
    DCHECK(firstChild() == next_child);
    SetFirstChild(&new_child);
  }
  new_child.SetParentOrShadowHostNode(this);
  new_child.SetPreviousSibling(prev);
  new_child.SetNextSibling(&next_child);
}

void ContainerNode::AppendChildCommon(Node& child) {
#if DCHECK_IS_ON()
  DCHECK(EventDispatchForbiddenScope::IsEventDispatchForbidden());
#endif
  DCHECK(ScriptForbiddenScope::IsScriptForbidden());

  child.SetParentOrShadowHostNode(this);
  if (last_child_) {
    child.SetPreviousSibling(last_child_);
    last_child_->SetNextSibling(&child);
  } else {
    SetFirstChild(&child);
  }
  SetLastChild(&child);
}

bool ContainerNode::CheckParserAcceptChild(const Node& new_child) const {
  if (!IsDocumentNode())
    return true;
  // TODO(esprehn): Are there other conditions where the parser can create
  // invalid trees?
  return ToDocument(*this).CanAcceptChild(new_child, nullptr,
                                          IGNORE_EXCEPTION_FOR_TESTING);
}

void ContainerNode::ParserInsertBefore(Node* new_child, Node& next_child) {
  DCHECK(new_child);
  DCHECK_EQ(next_child.parentNode(), this);
  DCHECK(!new_child->IsDocumentFragment());
  DCHECK(!isHTMLTemplateElement(this));

  if (next_child.previousSibling() == new_child ||
      &next_child == new_child)  // nothing to do
    return;

  if (!CheckParserAcceptChild(*new_child))
    return;

  // FIXME: parserRemoveChild can run script which could then insert the
  // newChild back into the page. Loop until the child is actually removed.
  // See: fast/parser/execute-script-during-adoption-agency-removal.html
  while (ContainerNode* parent = new_child->parentNode())
    parent->ParserRemoveChild(*new_child);

  if (next_child.parentNode() != this)
    return;

  if (GetDocument() != new_child->GetDocument())
    GetDocument().adoptNode(new_child, ASSERT_NO_EXCEPTION);

  {
    EventDispatchForbiddenScope assert_no_event_dispatch;
    ScriptForbiddenScope forbid_script;

    AdoptAndInsertBefore()(*this, *new_child, &next_child);
    DCHECK_EQ(new_child->ConnectedSubframeCount(), 0u);
    ChildListMutationScope(*this).ChildAdded(*new_child);
  }

  NotifyNodeInserted(*new_child, kChildrenChangeSourceParser);
}

Node* ContainerNode::ReplaceChild(Node* new_child,
                                  Node* old_child,
                                  ExceptionState& exception_state) {
  // https://dom.spec.whatwg.org/#concept-node-replace

  if (!old_child) {
    exception_state.ThrowDOMException(kNotFoundError,
                                      "The node to be replaced is null.");
    return nullptr;
  }

  // 1. If parent is not a Document, DocumentFragment, or Element node, throw a
  // HierarchyRequestError.
  // 2. If node is a host-including inclusive ancestor of parent, throw a
  // HierarchyRequestError.
  // 4. If node is not a DocumentFragment, DocumentType, Element, Text,
  // ProcessingInstruction, or Comment node, throw a HierarchyRequestError.
  // 5. If either node is a Text node and parent is a document, or node is a
  // doctype and parent is not a document, throw a HierarchyRequestError.
  // 6. If parent is a document, and any of the statements below, switched on
  // node, are true, throw a HierarchyRequestError.
  if (!CheckAcceptChild(new_child, old_child, exception_state))
    return old_child;

  // 3. If child’s parent is not parent, then throw a NotFoundError.
  if (old_child->parentNode() != this) {
    exception_state.ThrowDOMException(
        kNotFoundError, "The node to be replaced is not a child of this node.");
    return nullptr;
  }

  // 7. Let reference child be child’s next sibling.
  Node* next = old_child->nextSibling();
  // 8. If reference child is node, set it to node’s next sibling.
  if (next == new_child)
    next = new_child->nextSibling();

  // 10. Adopt node into parent’s node document.
  // TODO(tkent): Actually we do only RemoveChild() as a part of 'adopt'
  // operation.
  // Though the following CollectChildrenAndRemoveFromOldParentWithCheck() also
  // calls RemoveChild(), we'd like to call RemoveChild() here to make a
  // separated MutationRecord.
  if (ContainerNode* new_child_parent = new_child->parentNode()) {
    new_child_parent->RemoveChild(new_child, exception_state);
    if (exception_state.HadException())
      return nullptr;
  }

  NodeVector targets;
  NodeVector post_insertion_notification_targets;
  {
    // 9. Let previousSibling be child’s previous sibling.
    // 11. Let removedNodes be the empty list.
    // 15. Queue a mutation record of "childList" for target parent with
    // addedNodes nodes, removedNodes removedNodes, nextSibling reference child,
    // and previousSibling previousSibling.
    ChildListMutationScope mutation(*this);

    // 12. If child’s parent is not null, run these substeps:
    //    1. Set removedNodes to a list solely containing child.
    //    2. Remove child from its parent with the suppress observers flag set.
    if (ContainerNode* old_child_parent = old_child->parentNode()) {
      old_child_parent->RemoveChild(old_child, exception_state);
      if (exception_state.HadException())
        return nullptr;
    }

    // Does this one more time because removeChild() fires a MutationEvent.
    if (!CheckAcceptChild(new_child, old_child, exception_state))
      return old_child;

    // 13. Let nodes be node’s children if node is a DocumentFragment node, and
    // a list containing solely node otherwise.
    if (!CollectChildrenAndRemoveFromOldParentWithCheck(
            next, old_child, *new_child, targets, exception_state))
      return old_child;

    // 10. Adopt node into parent’s node document.
    // 14. Insert node into parent before reference child with the suppress
    // observers flag set.
    if (next) {
      InsertNodeVector(targets, next, AdoptAndInsertBefore(),
                       &post_insertion_notification_targets);
    } else {
      InsertNodeVector(targets, nullptr, AdoptAndAppendChild(),
                       &post_insertion_notification_targets);
    }
  }
  DidInsertNodeVector(targets, next, post_insertion_notification_targets);

  // 16. Return child.
  return old_child;
}

void ContainerNode::WillRemoveChild(Node& child) {
  DCHECK_EQ(child.parentNode(), this);
  ChildListMutationScope(*this).WillRemoveChild(child);
  child.NotifyMutationObserversNodeWillDetach();
  DispatchChildRemovalEvents(child);
  ChildFrameDisconnector(child).Disconnect();
  if (GetDocument() != child.GetDocument()) {
    // |child| was moved another document by DOM mutation event handler.
    return;
  }

  // |nodeWillBeRemoved()| must be run after |ChildFrameDisconnector|, because
  // |ChildFrameDisconnector| can run script which may cause state that is to
  // be invalidated by removing the node.
  ScriptForbiddenScope script_forbidden_scope;
  EventDispatchForbiddenScope assert_no_event_dispatch;
  // e.g. mutation event listener can create a new range.
  GetDocument().NodeWillBeRemoved(child);
}

void ContainerNode::WillRemoveChildren() {
  NodeVector children;
  GetChildNodes(*this, children);

  ChildListMutationScope mutation(*this);
  for (const auto& node : children) {
    DCHECK(node);
    Node& child = *node;
    mutation.WillRemoveChild(child);
    child.NotifyMutationObserversNodeWillDetach();
    DispatchChildRemovalEvents(child);
  }

  ChildFrameDisconnector(*this).Disconnect(
      ChildFrameDisconnector::kDescendantsOnly);
}

DEFINE_TRACE(ContainerNode) {
  visitor->Trace(first_child_);
  visitor->Trace(last_child_);
  Node::Trace(visitor);
}

DEFINE_TRACE_WRAPPERS(ContainerNode) {
  visitor->TraceWrappersWithManualWriteBarrier(first_child_);
  visitor->TraceWrappersWithManualWriteBarrier(last_child_);
  Node::TraceWrappers(visitor);
}

Node* ContainerNode::RemoveChild(Node* old_child,
                                 ExceptionState& exception_state) {
  // NotFoundError: Raised if oldChild is not a child of this node.
  // FIXME: We should never really get PseudoElements in here, but editing will
  // sometimes attempt to remove them still. We should fix that and enable this
  // DCHECK.  DCHECK(!oldChild->isPseudoElement())
  if (!old_child || old_child->parentNode() != this ||
      old_child->IsPseudoElement()) {
    exception_state.ThrowDOMException(
        kNotFoundError, "The node to be removed is not a child of this node.");
    return nullptr;
  }

  Node* child = old_child;

  GetDocument().RemoveFocusedElementOfSubtree(child);

  // Events fired when blurring currently focused node might have moved this
  // child into a different parent.
  if (child->parentNode() != this) {
    exception_state.ThrowDOMException(
        kNotFoundError,
        "The node to be removed is no longer a "
        "child of this node. Perhaps it was moved "
        "in a 'blur' event handler?");
    return nullptr;
  }

  WillRemoveChild(*child);

  // Mutation events might have moved this child into a different parent.
  if (child->parentNode() != this) {
    exception_state.ThrowDOMException(
        kNotFoundError,
        "The node to be removed is no longer a "
        "child of this node. Perhaps it was moved "
        "in response to a mutation?");
    return nullptr;
  }

  {
    HTMLFrameOwnerElement::UpdateSuspendScope suspend_widget_hierarchy_updates;
    DocumentOrderedMap::RemoveScope tree_remove_scope;

    Node* prev = child->previousSibling();
    Node* next = child->nextSibling();
    RemoveBetween(prev, next, *child);
    NotifyNodeRemoved(*child);
    ChildrenChanged(ChildrenChange::ForRemoval(*child, prev, next,
                                               kChildrenChangeSourceAPI));
  }
  DispatchSubtreeModifiedEvent();
  return child;
}

void ContainerNode::RemoveBetween(Node* previous_child,
                                  Node* next_child,
                                  Node& old_child) {
  EventDispatchForbiddenScope assert_no_event_dispatch;

  DCHECK_EQ(old_child.parentNode(), this);

  AttachContext context;
  context.clear_invalidation = true;
  if (!old_child.NeedsAttach())
    old_child.DetachLayoutTree(context);

  if (next_child)
    next_child->SetPreviousSibling(previous_child);
  if (previous_child)
    previous_child->SetNextSibling(next_child);
  if (first_child_ == &old_child)
    SetFirstChild(next_child);
  if (last_child_ == &old_child)
    SetLastChild(previous_child);

  old_child.SetPreviousSibling(nullptr);
  old_child.SetNextSibling(nullptr);
  old_child.SetParentOrShadowHostNode(nullptr);

  GetDocument().AdoptIfNeeded(old_child);
}

void ContainerNode::ParserRemoveChild(Node& old_child) {
  DCHECK_EQ(old_child.parentNode(), this);
  DCHECK(!old_child.IsDocumentFragment());

  // This may cause arbitrary Javascript execution via onunload handlers.
  if (old_child.ConnectedSubframeCount())
    ChildFrameDisconnector(old_child).Disconnect();

  if (old_child.parentNode() != this)
    return;

  ChildListMutationScope(*this).WillRemoveChild(old_child);
  old_child.NotifyMutationObserversNodeWillDetach();

  HTMLFrameOwnerElement::UpdateSuspendScope suspend_widget_hierarchy_updates;
  DocumentOrderedMap::RemoveScope tree_remove_scope;

  Node* prev = old_child.previousSibling();
  Node* next = old_child.nextSibling();
  RemoveBetween(prev, next, old_child);

  NotifyNodeRemoved(old_child);
  ChildrenChanged(ChildrenChange::ForRemoval(old_child, prev, next,
                                             kChildrenChangeSourceParser));
}

// This differs from other remove functions because it forcibly removes all the
// children, regardless of read-only status or event exceptions, e.g.
void ContainerNode::RemoveChildren(SubtreeModificationAction action) {
  if (!first_child_)
    return;

  // Do any prep work needed before actually starting to detach
  // and remove... e.g. stop loading frames, fire unload events.
  WillRemoveChildren();

  {
    // Removing focus can cause frames to load, either via events (focusout,
    // blur) or widget updates (e.g., for <embed>).
    SubframeLoadingDisabler disabler(*this);

    // Exclude this node when looking for removed focusedElement since only
    // children will be removed.
    // This must be later than willRemoveChildren, which might change focus
    // state of a child.
    GetDocument().RemoveFocusedElementOfSubtree(this, true);

    // Removing a node from a selection can cause widget updates.
    GetDocument().NodeChildrenWillBeRemoved(*this);
  }

  {
    HTMLFrameOwnerElement::UpdateSuspendScope suspend_widget_hierarchy_updates;
    DocumentOrderedMap::RemoveScope tree_remove_scope;
    {
      EventDispatchForbiddenScope assert_no_event_dispatch;
      ScriptForbiddenScope forbid_script;

      while (Node* child = first_child_) {
        RemoveBetween(0, child->nextSibling(), *child);
        NotifyNodeRemoved(*child);
      }
    }

    ChildrenChange change = {kAllChildrenRemoved, nullptr, nullptr, nullptr,
                             kChildrenChangeSourceAPI};
    ChildrenChanged(change);
  }

  if (action == kDispatchSubtreeModifiedEvent)
    DispatchSubtreeModifiedEvent();
}

Node* ContainerNode::AppendChild(Node* new_child,
                                 ExceptionState& exception_state) {
  // Make sure adding the new child is ok
  if (!CheckAcceptChild(new_child, 0, exception_state))
    return new_child;
  DCHECK(new_child);

  NodeVector targets;
  if (!CollectChildrenAndRemoveFromOldParentWithCheck(
          nullptr, nullptr, *new_child, targets, exception_state))
    return new_child;

  NodeVector post_insertion_notification_targets;
  {
    ChildListMutationScope mutation(*this);
    InsertNodeVector(targets, nullptr, AdoptAndAppendChild(),
                     &post_insertion_notification_targets);
  }
  DidInsertNodeVector(targets, nullptr, post_insertion_notification_targets);
  return new_child;
}

void ContainerNode::ParserAppendChild(Node* new_child) {
  DCHECK(new_child);
  DCHECK(!new_child->IsDocumentFragment());
  DCHECK(!isHTMLTemplateElement(this));

  if (!CheckParserAcceptChild(*new_child))
    return;

  // FIXME: parserRemoveChild can run script which could then insert the
  // newChild back into the page. Loop until the child is actually removed.
  // See: fast/parser/execute-script-during-adoption-agency-removal.html
  while (ContainerNode* parent = new_child->parentNode())
    parent->ParserRemoveChild(*new_child);

  if (GetDocument() != new_child->GetDocument())
    GetDocument().adoptNode(new_child, ASSERT_NO_EXCEPTION);

  {
    EventDispatchForbiddenScope assert_no_event_dispatch;
    ScriptForbiddenScope forbid_script;

    AdoptAndAppendChild()(*this, *new_child, nullptr);
    DCHECK_EQ(new_child->ConnectedSubframeCount(), 0u);
    ChildListMutationScope(*this).ChildAdded(*new_child);
  }

  NotifyNodeInserted(*new_child, kChildrenChangeSourceParser);
}

DISABLE_CFI_PERF
void ContainerNode::NotifyNodeInserted(Node& root,
                                       ChildrenChangeSource source) {
#if DCHECK_IS_ON()
  DCHECK(!EventDispatchForbiddenScope::IsEventDispatchForbidden());
#endif
  DCHECK(!root.IsShadowRoot());

  if (GetDocument().ContainsV1ShadowTree())
    root.CheckSlotChangeAfterInserted();

  probe::didInsertDOMNode(&root);

  NodeVector post_insertion_notification_targets;
  NotifyNodeInsertedInternal(root, post_insertion_notification_targets);

  ChildrenChanged(ChildrenChange::ForInsertion(root, root.previousSibling(),
                                               root.nextSibling(), source));

  for (const auto& target_node : post_insertion_notification_targets) {
    if (target_node->isConnected())
      target_node->DidNotifySubtreeInsertionsToDocument();
  }
}

DISABLE_CFI_PERF
void ContainerNode::NotifyNodeInsertedInternal(
    Node& root,
    NodeVector& post_insertion_notification_targets) {
  EventDispatchForbiddenScope assert_no_event_dispatch;
  ScriptForbiddenScope forbid_script;

  for (Node& node : NodeTraversal::InclusiveDescendantsOf(root)) {
    // As an optimization we don't notify leaf nodes when when inserting
    // into detached subtrees that are not in a shadow tree.
    if (!isConnected() && !IsInShadowTree() && !node.IsContainerNode())
      continue;
    if (Node::kInsertionShouldCallDidNotifySubtreeInsertions ==
        node.InsertedInto(this))
      post_insertion_notification_targets.push_back(&node);
    for (ShadowRoot* shadow_root = node.YoungestShadowRoot(); shadow_root;
         shadow_root = shadow_root->OlderShadowRoot())
      NotifyNodeInsertedInternal(*shadow_root,
                                 post_insertion_notification_targets);
  }
}

void ContainerNode::NotifyNodeRemoved(Node& root) {
  ScriptForbiddenScope forbid_script;
  EventDispatchForbiddenScope assert_no_event_dispatch;

  for (Node& node : NodeTraversal::InclusiveDescendantsOf(root)) {
    // As an optimization we skip notifying Text nodes and other leaf nodes
    // of removal when they're not in the Document tree and not in a shadow root
    // since the virtual call to removedFrom is not needed.
    if (!node.IsContainerNode() && !node.IsInTreeScope())
      continue;
    node.RemovedFrom(this);
    for (ShadowRoot* shadow_root = node.YoungestShadowRoot(); shadow_root;
         shadow_root = shadow_root->OlderShadowRoot())
      NotifyNodeRemoved(*shadow_root);
  }
}

DISABLE_CFI_PERF
void ContainerNode::AttachLayoutTree(const AttachContext& context) {
  AttachContext children_context(context);
  children_context.resolved_style = nullptr;

  for (Node* child = firstChild(); child; child = child->nextSibling()) {
#if DCHECK_IS_ON()
    DCHECK(child->NeedsAttach() ||
           ChildAttachedAllowedWhenAttachingChildren(this));
#endif
    if (child->NeedsAttach())
      child->AttachLayoutTree(children_context);
  }

  ClearChildNeedsStyleRecalc();
  ClearChildNeedsReattachLayoutTree();
  Node::AttachLayoutTree(context);
}

void ContainerNode::DetachLayoutTree(const AttachContext& context) {
  AttachContext children_context(context);
  children_context.resolved_style = nullptr;
  children_context.clear_invalidation = true;

  for (Node* child = firstChild(); child; child = child->nextSibling())
    child->DetachLayoutTree(children_context);

  SetChildNeedsStyleRecalc();
  Node::DetachLayoutTree(context);
}

void ContainerNode::ChildrenChanged(const ChildrenChange& change) {
  GetDocument().IncDOMTreeVersion();
  GetDocument().NotifyChangeChildren(*this);
  InvalidateNodeListCachesInAncestors();
  if (change.IsChildInsertion()) {
    if (!ChildNeedsStyleRecalc()) {
      SetChildNeedsStyleRecalc();
      MarkAncestorsWithChildNeedsStyleRecalc();
    }
  }
}

void ContainerNode::CloneChildNodes(ContainerNode* clone) {
  DummyExceptionStateForTesting exception_state;
  for (Node* n = firstChild(); n && !exception_state.HadException();
       n = n->nextSibling())
    clone->AppendChild(n->cloneNode(true), exception_state);
}

bool ContainerNode::GetUpperLeftCorner(FloatPoint& point) const {
  if (!GetLayoutObject())
    return false;

  // FIXME: What is this code really trying to do?
  LayoutObject* o = GetLayoutObject();
  if (!o->IsInline() || o->IsAtomicInlineLevel()) {
    point = o->LocalToAbsolute(FloatPoint(), kUseTransforms);
    return true;
  }

  // Find the next text/image child, to get a position.
  while (o) {
    LayoutObject* p = o;
    if (LayoutObject* o_first_child = o->SlowFirstChild()) {
      o = o_first_child;
    } else if (o->NextSibling()) {
      o = o->NextSibling();
    } else {
      LayoutObject* next = nullptr;
      while (!next && o->Parent()) {
        o = o->Parent();
        next = o->NextSibling();
      }
      o = next;

      if (!o)
        break;
    }
    DCHECK(o);

    if (!o->IsInline() || o->IsAtomicInlineLevel()) {
      point = o->LocalToAbsolute(FloatPoint(), kUseTransforms);
      return true;
    }

    if (p->GetNode() && p->GetNode() == this && o->IsText() && !o->IsBR() &&
        !ToLayoutText(o)->HasTextBoxes()) {
      // Do nothing - skip unrendered whitespace that is a child or next sibling
      // of the anchor.
      // FIXME: This fails to skip a whitespace sibling when there was also a
      // whitespace child (because p has moved).
    } else if ((o->IsText() && !o->IsBR()) || o->IsAtomicInlineLevel()) {
      point = FloatPoint();
      if (o->IsText()) {
        if (ToLayoutText(o)->FirstTextBox())
          point.Move(
              ToLayoutText(o)->LinesBoundingBox().X(),
              ToLayoutText(o)->FirstTextBox()->Root().LineTop().ToFloat());
        point = o->LocalToAbsolute(point, kUseTransforms);
      } else {
        DCHECK(o->IsBox());
        LayoutBox* box = ToLayoutBox(o);
        point.MoveBy(box->Location());
        point = o->Container()->LocalToAbsolute(point, kUseTransforms);
      }
      return true;
    }
  }

  // If the target doesn't have any children or siblings that could be used to
  // calculate the scroll position, we must be at the end of the
  // document. Scroll to the bottom.
  // FIXME: who said anything about scrolling?
  if (!o && GetDocument().View()) {
    point = FloatPoint(0, GetDocument().View()->ContentsHeight());
    return true;
  }
  return false;
}

static inline LayoutObject* EndOfContinuations(LayoutObject* layout_object) {
  LayoutObject* prev = nullptr;
  LayoutObject* cur = layout_object;

  if (!cur->IsLayoutInline() && !cur->IsLayoutBlockFlow())
    return nullptr;

  while (cur) {
    prev = cur;
    if (cur->IsLayoutInline())
      cur = ToLayoutInline(cur)->Continuation();
    else
      cur = ToLayoutBlockFlow(cur)->Continuation();
  }

  return prev;
}

bool ContainerNode::GetLowerRightCorner(FloatPoint& point) const {
  if (!GetLayoutObject())
    return false;

  LayoutObject* o = GetLayoutObject();
  if (!o->IsInline() || o->IsAtomicInlineLevel()) {
    LayoutBox* box = ToLayoutBox(o);
    point = o->LocalToAbsolute(FloatPoint(box->Size()), kUseTransforms);
    return true;
  }

  LayoutObject* start_continuation = nullptr;
  // Find the last text/image child, to get a position.
  while (o) {
    if (LayoutObject* o_last_child = o->SlowLastChild()) {
      o = o_last_child;
    } else if (o != GetLayoutObject() && o->PreviousSibling()) {
      o = o->PreviousSibling();
    } else {
      LayoutObject* prev = nullptr;
      while (!prev) {
        // Check if the current layoutObject has contiunation and move the
        // location for finding the layoutObject to the end of continuations if
        // there is the continuation.  Skip to check the contiunation on
        // contiunations section
        if (start_continuation == o) {
          start_continuation = nullptr;
        } else if (!start_continuation) {
          if (LayoutObject* continuation = EndOfContinuations(o)) {
            start_continuation = o;
            prev = continuation;
            break;
          }
        }
        // Prevent to overrun out of own layout tree
        if (o == GetLayoutObject()) {
          return false;
        }
        o = o->Parent();
        if (!o)
          return false;
        prev = o->PreviousSibling();
      }
      o = prev;
    }
    DCHECK(o);
    if (o->IsText() || o->IsAtomicInlineLevel()) {
      point = FloatPoint();
      if (o->IsText()) {
        LayoutText* text = ToLayoutText(o);
        IntRect lines_box = EnclosingIntRect(text->LinesBoundingBox());
        if (!lines_box.MaxX() && !lines_box.MaxY())
          continue;
        point.MoveBy(lines_box.MaxXMaxYCorner());
        point = o->LocalToAbsolute(point, kUseTransforms);
      } else {
        LayoutBox* box = ToLayoutBox(o);
        point.MoveBy(box->FrameRect().MaxXMaxYCorner());
        point = o->Container()->LocalToAbsolute(point, kUseTransforms);
      }
      return true;
    }
  }
  return true;
}

// FIXME: This override is only needed for inline anchors without an
// InlineBox and it does not belong in ContainerNode as it reaches into
// the layout and line box trees.
// https://code.google.com/p/chromium/issues/detail?id=248354
LayoutRect ContainerNode::BoundingBox() const {
  FloatPoint upper_left, lower_right;
  bool found_upper_left = GetUpperLeftCorner(upper_left);
  bool found_lower_right = GetLowerRightCorner(lower_right);

  // If we've found one corner, but not the other,
  // then we should just return a point at the corner that we did find.
  if (found_upper_left != found_lower_right) {
    if (found_upper_left)
      lower_right = upper_left;
    else
      upper_left = lower_right;
  }

  FloatSize size = lower_right.ExpandedTo(upper_left) - upper_left;
  if (std::isnan(size.Width()) || std::isnan(size.Height()))
    return LayoutRect();

  return EnclosingLayoutRect(FloatRect(upper_left, size));
}

// This is used by FrameSelection to denote when the active-state of the page
// has changed independent of the focused element changing.
void ContainerNode::FocusStateChanged() {
  // If we're just changing the window's active state and the focused node has
  // no layoutObject we can just ignore the state change.
  if (!GetLayoutObject())
    return;

  if (GetComputedStyle()->AffectedByFocus()) {
    StyleChangeType change_type =
        GetComputedStyle()->HasPseudoStyle(kPseudoIdFirstLetter)
            ? kSubtreeStyleChange
            : kLocalStyleChange;
    SetNeedsStyleRecalc(
        change_type,
        StyleChangeReasonForTracing::CreateWithExtraData(
            StyleChangeReason::kPseudoClass, StyleChangeExtraData::g_focus));
  }
  if (IsElementNode() && ToElement(this)->ChildrenOrSiblingsAffectedByFocus())
    ToElement(this)->PseudoStateChanged(CSSSelector::kPseudoFocus);

  LayoutTheme::GetTheme().ControlStateChanged(*GetLayoutObject(),
                                              kFocusControlState);
  FocusWithinStateChanged();
}

void ContainerNode::FocusWithinStateChanged() {
  if (GetComputedStyle() && GetComputedStyle()->AffectedByFocusWithin()) {
    StyleChangeType change_type =
        GetComputedStyle()->HasPseudoStyle(kPseudoIdFirstLetter)
            ? kSubtreeStyleChange
            : kLocalStyleChange;
    SetNeedsStyleRecalc(change_type,
                        StyleChangeReasonForTracing::CreateWithExtraData(
                            StyleChangeReason::kPseudoClass,
                            StyleChangeExtraData::g_focus_within));
  }
  if (IsElementNode() &&
      ToElement(this)->ChildrenOrSiblingsAffectedByFocusWithin())
    ToElement(this)->PseudoStateChanged(CSSSelector::kPseudoFocusWithin);
}

void ContainerNode::SetFocused(bool received, WebFocusType focus_type) {
  // Recurse up author shadow trees to mark shadow hosts if it matches :focus.
  // TODO(kochi): Handle UA shadows which marks multiple nodes as focused such
  // as <input type="date"> the same way as author shadow.
  if (ShadowRoot* root = ContainingShadowRoot()) {
    if (root->GetType() != ShadowRootType::kUserAgent)
      OwnerShadowHost()->SetFocused(received, focus_type);
  }

  // If this is an author shadow host and indirectly focused (has focused
  // element within its shadow root), update focus.
  if (IsElementNode() && GetDocument().FocusedElement() &&
      GetDocument().FocusedElement() != this) {
    if (ToElement(this)->AuthorShadowRoot())
      received =
          received && ToElement(this)->AuthorShadowRoot()->delegatesFocus();
  }

  if (IsFocused() == received)
    return;

  Node::SetFocused(received, focus_type);

  FocusStateChanged();

  if (GetLayoutObject() || received)
    return;

  // If :focus sets display: none, we lose focus but still need to recalc our
  // style.
  if (IsElementNode() && ToElement(this)->ChildrenOrSiblingsAffectedByFocus())
    ToElement(this)->PseudoStateChanged(CSSSelector::kPseudoFocus);
  else
    SetNeedsStyleRecalc(
        kLocalStyleChange,
        StyleChangeReasonForTracing::CreateWithExtraData(
            StyleChangeReason::kPseudoClass, StyleChangeExtraData::g_focus));

  if (IsElementNode() &&
      ToElement(this)->ChildrenOrSiblingsAffectedByFocusWithin()) {
    ToElement(this)->PseudoStateChanged(CSSSelector::kPseudoFocusWithin);
  } else {
    SetNeedsStyleRecalc(kLocalStyleChange,
                        StyleChangeReasonForTracing::CreateWithExtraData(
                            StyleChangeReason::kPseudoClass,
                            StyleChangeExtraData::g_focus_within));
  }
}

void ContainerNode::SetHasFocusWithinUpToAncestor(bool flag, Node* ancestor) {
  for (ContainerNode* node = this; node && node != ancestor;
       node = FlatTreeTraversal::Parent(*node)) {
    node->SetHasFocusWithin(flag);
    node->FocusWithinStateChanged();
  }
}

void ContainerNode::SetActive(bool down) {
  if (down == IsActive())
    return;

  Node::SetActive(down);

  if (!GetLayoutObject()) {
    if (IsElementNode() &&
        ToElement(this)->ChildrenOrSiblingsAffectedByActive())
      ToElement(this)->PseudoStateChanged(CSSSelector::kPseudoActive);
    else
      SetNeedsStyleRecalc(
          kLocalStyleChange,
          StyleChangeReasonForTracing::CreateWithExtraData(
              StyleChangeReason::kPseudoClass, StyleChangeExtraData::g_active));
    return;
  }

  if (GetComputedStyle()->AffectedByActive()) {
    StyleChangeType change_type =
        GetComputedStyle()->HasPseudoStyle(kPseudoIdFirstLetter)
            ? kSubtreeStyleChange
            : kLocalStyleChange;
    SetNeedsStyleRecalc(
        change_type,
        StyleChangeReasonForTracing::CreateWithExtraData(
            StyleChangeReason::kPseudoClass, StyleChangeExtraData::g_active));
  }
  if (IsElementNode() && ToElement(this)->ChildrenOrSiblingsAffectedByActive())
    ToElement(this)->PseudoStateChanged(CSSSelector::kPseudoActive);

  LayoutTheme::GetTheme().ControlStateChanged(*GetLayoutObject(),
                                              kPressedControlState);
}

void ContainerNode::SetDragged(bool new_value) {
  if (new_value == IsDragged())
    return;

  Node::SetDragged(new_value);

  // If :-webkit-drag sets display: none we lose our dragging but still need
  // to recalc our style.
  if (!GetLayoutObject()) {
    if (new_value)
      return;
    if (IsElementNode() && ToElement(this)->ChildrenOrSiblingsAffectedByDrag())
      ToElement(this)->PseudoStateChanged(CSSSelector::kPseudoDrag);
    else
      SetNeedsStyleRecalc(
          kLocalStyleChange,
          StyleChangeReasonForTracing::CreateWithExtraData(
              StyleChangeReason::kPseudoClass, StyleChangeExtraData::g_drag));
    return;
  }

  if (GetComputedStyle()->AffectedByDrag()) {
    StyleChangeType change_type =
        GetComputedStyle()->HasPseudoStyle(kPseudoIdFirstLetter)
            ? kSubtreeStyleChange
            : kLocalStyleChange;
    SetNeedsStyleRecalc(
        change_type,
        StyleChangeReasonForTracing::CreateWithExtraData(
            StyleChangeReason::kPseudoClass, StyleChangeExtraData::g_drag));
  }
  if (IsElementNode() && ToElement(this)->ChildrenOrSiblingsAffectedByDrag())
    ToElement(this)->PseudoStateChanged(CSSSelector::kPseudoDrag);
}

void ContainerNode::SetHovered(bool over) {
  if (over == IsHovered())
    return;

  Node::SetHovered(over);

  const ComputedStyle* style = GetComputedStyle();
  if (!style || style->AffectedByHover()) {
    StyleChangeType change_type = kLocalStyleChange;
    if (style && style->HasPseudoStyle(kPseudoIdFirstLetter))
      change_type = kSubtreeStyleChange;
    SetNeedsStyleRecalc(
        change_type,
        StyleChangeReasonForTracing::CreateWithExtraData(
            StyleChangeReason::kPseudoClass, StyleChangeExtraData::g_hover));
  }
  if (IsElementNode() && ToElement(this)->ChildrenOrSiblingsAffectedByHover())
    ToElement(this)->PseudoStateChanged(CSSSelector::kPseudoHover);

  if (GetLayoutObject()) {
    LayoutTheme::GetTheme().ControlStateChanged(*GetLayoutObject(),
                                                kHoverControlState);
  }
}

HTMLCollection* ContainerNode::Children() {
  return EnsureCachedCollection<HTMLCollection>(kNodeChildren);
}

unsigned ContainerNode::CountChildren() const {
  unsigned count = 0;
  for (Node* node = firstChild(); node; node = node->nextSibling())
    count++;
  return count;
}

Element* ContainerNode::QuerySelector(const AtomicString& selectors,
                                      ExceptionState& exception_state) {
  SelectorQuery* selector_query = GetDocument().GetSelectorQueryCache().Add(
      selectors, GetDocument(), exception_state);
  if (!selector_query)
    return nullptr;
  return selector_query->QueryFirst(*this);
}

StaticElementList* ContainerNode::QuerySelectorAll(
    const AtomicString& selectors,
    ExceptionState& exception_state) {
  SelectorQuery* selector_query = GetDocument().GetSelectorQueryCache().Add(
      selectors, GetDocument(), exception_state);
  if (!selector_query)
    return nullptr;
  return selector_query->QueryAll(*this);
}

static void DispatchChildInsertionEvents(Node& child) {
  if (child.IsInShadowTree())
    return;

#if DCHECK_IS_ON()
  DCHECK(!EventDispatchForbiddenScope::IsEventDispatchForbidden());
#endif

  Node* c = &child;
  Document* document = &child.GetDocument();

  if (c->parentNode() &&
      document->HasListenerType(Document::kDOMNodeInsertedListener))
    c->DispatchScopedEvent(MutationEvent::Create(
        EventTypeNames::DOMNodeInserted, true, c->parentNode()));

  // dispatch the DOMNodeInsertedIntoDocument event to all descendants
  if (c->isConnected() && document->HasListenerType(
                              Document::kDOMNodeInsertedIntoDocumentListener)) {
    for (; c; c = NodeTraversal::Next(*c, &child))
      c->DispatchScopedEvent(MutationEvent::Create(
          EventTypeNames::DOMNodeInsertedIntoDocument, false));
  }
}

static void DispatchChildRemovalEvents(Node& child) {
  if (child.IsInShadowTree()) {
    probe::willRemoveDOMNode(&child);
    return;
  }

#if DCHECK_IS_ON()
  DCHECK(!EventDispatchForbiddenScope::IsEventDispatchForbidden());
#endif

  probe::willRemoveDOMNode(&child);

  Node* c = &child;
  Document* document = &child.GetDocument();

  // Dispatch pre-removal mutation events.
  if (c->parentNode() &&
      document->HasListenerType(Document::kDOMNodeRemovedListener)) {
    NodeChildRemovalTracker scope(child);
    c->DispatchScopedEvent(MutationEvent::Create(EventTypeNames::DOMNodeRemoved,
                                                 true, c->parentNode()));
  }

  // Dispatch the DOMNodeRemovedFromDocument event to all descendants.
  if (c->isConnected() && document->HasListenerType(
                              Document::kDOMNodeRemovedFromDocumentListener)) {
    NodeChildRemovalTracker scope(child);
    for (; c; c = NodeTraversal::Next(*c, &child))
      c->DispatchScopedEvent(MutationEvent::Create(
          EventTypeNames::DOMNodeRemovedFromDocument, false));
  }
}

bool ContainerNode::HasRestyleFlagInternal(DynamicRestyleFlags mask) const {
  return RareData()->HasRestyleFlag(mask);
}

bool ContainerNode::HasRestyleFlagsInternal() const {
  return RareData()->HasRestyleFlags();
}

void ContainerNode::SetRestyleFlag(DynamicRestyleFlags mask) {
  DCHECK(IsElementNode() || IsShadowRoot());
  EnsureRareData().SetRestyleFlag(mask);
}

void ContainerNode::RecalcDescendantStyles(StyleRecalcChange change) {
  DCHECK(GetDocument().InStyleRecalc());
  DCHECK(change >= kUpdatePseudoElements || ChildNeedsStyleRecalc());
  DCHECK(!NeedsStyleRecalc());

  StyleResolver& style_resolver = GetDocument().EnsureStyleResolver();
  for (Node* child = lastChild(); child; child = child->previousSibling()) {
    if (child->IsTextNode()) {
      ToText(child)->RecalcTextStyle(change);
    } else if (child->IsElementNode()) {
      Element* element = ToElement(child);
      if (element->ShouldCallRecalcStyle(change))
        element->RecalcStyle(change);
      else if (element->SupportsStyleSharing())
        style_resolver.AddToStyleSharingList(*element);
    }
  }
}

void ContainerNode::RebuildLayoutTreeForChild(Node* child,
                                              Text*& next_text_sibling) {
  bool rebuild_child =
      child->NeedsReattachLayoutTree() || child->ChildNeedsReattachLayoutTree();

  if (child->IsTextNode()) {
    Text* text_node = ToText(child);
    if (rebuild_child)
      text_node->RebuildTextLayoutTree(next_text_sibling);
    next_text_sibling = text_node;
    return;
  }

  if (!child->IsElementNode())
    return;

  Element* element = ToElement(child);
  if (rebuild_child)
    element->RebuildLayoutTree(next_text_sibling);
  if (element->GetLayoutObject())
    next_text_sibling = nullptr;
}

void ContainerNode::RebuildChildrenLayoutTrees(Text*& next_text_sibling) {
  DCHECK(!NeedsReattachLayoutTree());

  if (IsActiveSlotOrActiveInsertionPoint()) {
    if (isHTMLSlotElement(this))
      toHTMLSlotElement(this)->RebuildDistributedChildrenLayoutTrees();
    else
      ToInsertionPoint(this)->RebuildDistributedChildrenLayoutTrees();
  }

  // This loop is deliberately backwards because we use insertBefore in the
  // layout tree, and want to avoid a potentially n^2 loop to find the insertion
  // point while building the layout tree.  Having us start from the last child
  // and work our way back means in the common case, we'll find the insertion
  // point in O(1) time.  See crbug.com/288225
  for (Node* child = lastChild(); child; child = child->previousSibling())
    RebuildLayoutTreeForChild(child, next_text_sibling);

  // This is done in ContainerNode::AttachLayoutTree but will never be cleared
  // if we don't enter ContainerNode::AttachLayoutTree so we do it here.
  ClearChildNeedsStyleRecalc();
  ClearChildNeedsReattachLayoutTree();
}

void ContainerNode::CheckForSiblingStyleChanges(SiblingCheckType change_type,
                                                Element* changed_element,
                                                Node* node_before_change,
                                                Node* node_after_change) {
  if (!InActiveDocument() || GetDocument().HasPendingForcedStyleRecalc() ||
      GetStyleChangeType() >= kSubtreeStyleChange)
    return;

  if (!HasRestyleFlag(kChildrenAffectedByStructuralRules))
    return;

  Element* element_after_change =
      !node_after_change || node_after_change->IsElementNode()
          ? ToElement(node_after_change)
          : ElementTraversal::NextSibling(*node_after_change);
  Element* element_before_change =
      !node_before_change || node_before_change->IsElementNode()
          ? ToElement(node_before_change)
          : ElementTraversal::PreviousSibling(*node_before_change);

  // TODO(rune@opera.com): move this code into StyleEngine and collect the
  // various invalidation sets into a single InvalidationLists object and
  // schedule with a single scheduleInvalidationSetsForNode for efficiency.

  // Forward positional selectors include :nth-child, :nth-of-type,
  // :first-of-type, and only-of-type. Backward positional selectors include
  // :nth-last-child, :nth-last-of-type, :last-of-type, and :only-of-type.
  if ((ChildrenAffectedByForwardPositionalRules() && element_after_change) ||
      (ChildrenAffectedByBackwardPositionalRules() && element_before_change)) {
    GetDocument().GetStyleEngine().ScheduleNthPseudoInvalidations(*this);
  }

  if (ChildrenAffectedByFirstChildRules() && !element_before_change &&
      element_after_change &&
      element_after_change->AffectedByFirstChildRules()) {
    DCHECK_NE(change_type, kFinishedParsingChildren);
    element_after_change->PseudoStateChanged(CSSSelector::kPseudoFirstChild);
    element_after_change->PseudoStateChanged(CSSSelector::kPseudoOnlyChild);
  }

  if (ChildrenAffectedByLastChildRules() && !element_after_change &&
      element_before_change &&
      element_before_change->AffectedByLastChildRules()) {
    element_before_change->PseudoStateChanged(CSSSelector::kPseudoLastChild);
    element_before_change->PseudoStateChanged(CSSSelector::kPseudoOnlyChild);
  }

  // For ~ and + combinators, succeeding siblings may need style invalidation
  // after an element is inserted or removed.

  if (!element_after_change)
    return;

  if (!ChildrenAffectedByIndirectAdjacentRules() &&
      !ChildrenAffectedByDirectAdjacentRules())
    return;

  if (change_type == kSiblingElementInserted) {
    GetDocument().GetStyleEngine().ScheduleInvalidationsForInsertedSibling(
        element_before_change, *changed_element);
    return;
  }

  DCHECK(change_type == kSiblingElementRemoved);
  GetDocument().GetStyleEngine().ScheduleInvalidationsForRemovedSibling(
      element_before_change, *changed_element, *element_after_change);
}

void ContainerNode::InvalidateNodeListCachesInAncestors(
    const QualifiedName* attr_name,
    Element* attribute_owner_element) {
  if (HasRareData() && (!attr_name || IsAttributeNode())) {
    if (NodeListsNodeData* lists = RareData()->NodeLists()) {
      if (ChildNodeList* child_node_list = lists->GetChildNodeList(*this))
        child_node_list->InvalidateCache();
    }
  }

  // Modifications to attributes that are not associated with an Element can't
  // invalidate NodeList caches.
  if (attr_name && !attribute_owner_element)
    return;

  if (!GetDocument().ShouldInvalidateNodeListCaches(attr_name))
    return;

  GetDocument().InvalidateNodeListCaches(attr_name);

  for (ContainerNode* node = this; node; node = node->parentNode()) {
    if (NodeListsNodeData* lists = node->NodeLists())
      lists->InvalidateCaches(attr_name);
  }
}

HTMLCollection* ContainerNode::getElementsByTagName(
    const AtomicString& qualified_name) {
  DCHECK(!qualified_name.IsNull());

  if (GetDocument().IsHTMLDocument()) {
    return EnsureCachedCollection<HTMLTagCollection>(kHTMLTagCollectionType,
                                                     qualified_name);
  }
  return EnsureCachedCollection<TagCollection>(kTagCollectionType,
                                               qualified_name);
}

HTMLCollection* ContainerNode::getElementsByTagNameNS(
    const AtomicString& namespace_uri,
    const AtomicString& local_name) {
  return EnsureCachedCollection<TagCollectionNS>(
      kTagCollectionNSType,
      namespace_uri.IsEmpty() ? g_null_atom : namespace_uri, local_name);
}

// Takes an AtomicString in argument because it is common for elements to share
// the same name attribute.  Therefore, the NameNodeList factory function
// expects an AtomicString type.
NameNodeList* ContainerNode::getElementsByName(
    const AtomicString& element_name) {
  return EnsureCachedCollection<NameNodeList>(kNameNodeListType, element_name);
}

// Takes an AtomicString in argument because it is common for elements to share
// the same set of class names.  Therefore, the ClassNodeList factory function
// expects an AtomicString type.
ClassCollection* ContainerNode::getElementsByClassName(
    const AtomicString& class_names) {
  return EnsureCachedCollection<ClassCollection>(kClassCollectionType,
                                                 class_names);
}

RadioNodeList* ContainerNode::GetRadioNodeList(const AtomicString& name,
                                               bool only_match_img_elements) {
  DCHECK(isHTMLFormElement(this) || isHTMLFieldSetElement(this));
  CollectionType type =
      only_match_img_elements ? kRadioImgNodeListType : kRadioNodeListType;
  return EnsureCachedCollection<RadioNodeList>(type, name);
}

Element* ContainerNode::getElementById(const AtomicString& id) const {
  if (IsInTreeScope()) {
    // Fast path if we are in a tree scope: call getElementById() on tree scope
    // and check if the matching element is in our subtree.
    Element* element = ContainingTreeScope().getElementById(id);
    if (!element)
      return nullptr;
    if (element->IsDescendantOf(this))
      return element;
  }

  // Fall back to traversing our subtree. In case of duplicate ids, the first
  // element found will be returned.
  for (Element& element : ElementTraversal::DescendantsOf(*this)) {
    if (element.GetIdAttribute() == id)
      return &element;
  }
  return nullptr;
}

NodeListsNodeData& ContainerNode::EnsureNodeLists() {
  return EnsureRareData().EnsureNodeLists();
}

#if DCHECK_IS_ON()
bool ChildAttachedAllowedWhenAttachingChildren(ContainerNode* node) {
  if (node->IsShadowRoot())
    return true;

  if (node->IsInsertionPoint())
    return true;

  if (isHTMLSlotElement(node))
    return true;

  if (node->IsElementNode() && ToElement(node)->Shadow())
    return true;

  return false;
}
#endif

}  // namespace blink
