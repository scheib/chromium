/*
 * (C) 1999 Lars Knoll (knoll@kde.org)
 * (C) 2000 Gunnstein Lye (gunnstein@netcom.no)
 * (C) 2000 Frederik Holljen (frederik.holljen@hig.no)
 * (C) 2001 Peter Kelly (pmk@post.com)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2011 Motorola Mobility. All rights reserved.
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

#include "core/dom/Range.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/dom/CharacterData.h"
#include "core/dom/ClientRect.h"
#include "core/dom/ClientRectList.h"
#include "core/dom/ContainerNode.h"
#include "core/dom/DocumentFragment.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/Node.h"
#include "core/dom/NodeTraversal.h"
#include "core/dom/NodeWithIndex.h"
#include "core/dom/ProcessingInstruction.h"
#include "core/dom/Text.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/EphemeralRange.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/VisiblePosition.h"
#include "core/editing/VisibleUnits.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/editing/serializers/Serialization.h"
#include "core/events/ScopedEventQueue.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLBodyElement.h"
#include "core/html/HTMLElement.h"
#include "core/layout/LayoutObject.h"
#include "core/layout/LayoutText.h"
#include "core/svg/SVGSVGElement.h"
#include "platform/EventDispatchForbiddenScope.h"
#include "platform/geometry/FloatQuad.h"
#include "platform/wtf/text/CString.h"
#include "platform/wtf/text/StringBuilder.h"
#ifndef NDEBUG
#include <stdio.h>
#endif

namespace blink {

class RangeUpdateScope {
  STACK_ALLOCATED();
  explicit RangeUpdateScope(Range* range) {
    DCHECK(range);
    if (++scope_count_ == 1) {
      range_ = range;
      old_document_ = range->OwnerDocument();
#if DCHECK_IS_ON()
      current_range_ = range;
    } else {
      DCHECK_EQ(current_range_, range);
#endif
    }
  }

  ~RangeUpdateScope() {
    DCHECK_GE(scope_count_, 1);
    if (--scope_count_ > 0)
      return;
    Settings* settings = old_document_->GetFrame()
                             ? old_document_->GetFrame()->GetSettings()
                             : nullptr;
    if (!settings ||
        !settings->GetDoNotUpdateSelectionOnMutatingSelectionRange()) {
      range_->RemoveFromSelectionIfInDifferentRoot(*old_document_);
      range_->UpdateSelectionIfAddedToSelection();
    }
#if DCHECK_IS_ON()
    current_range_ = nullptr;
#endif
  }

 private:
  static int scope_count_;
#if DCHECK_IS_ON()
  // This raw pointer is safe because
  //  - s_currentRange has a valid pointer only if RangeUpdateScope instance is
  //  live.
  //  - RangeUpdateScope is used only in Range member functions.
  static Range* current_range_;
#endif
  Member<Range> range_;
  Member<Document> old_document_;

  DISALLOW_COPY_AND_ASSIGN(RangeUpdateScope);
};

int RangeUpdateScope::scope_count_ = 0;
#if DCHECK_IS_ON()
Range* RangeUpdateScope::current_range_;
#endif

inline Range::Range(Document& owner_document)
    : owner_document_(&owner_document),
      start_(*owner_document_),
      end_(*owner_document_) {
  owner_document_->AttachRange(this);
}

Range* Range::Create(Document& owner_document) {
  return new Range(owner_document);
}

inline Range::Range(Document& owner_document,
                    Node* start_container,
                    unsigned start_offset,
                    Node* end_container,
                    unsigned end_offset)
    : owner_document_(&owner_document),
      start_(*owner_document_),
      end_(*owner_document_) {
  owner_document_->AttachRange(this);

  // Simply setting the containers and offsets directly would not do any of the
  // checking that setStart and setEnd do, so we call those functions.
  setStart(start_container, start_offset);
  setEnd(end_container, end_offset);
}

Range* Range::Create(Document& owner_document,
                     Node* start_container,
                     unsigned start_offset,
                     Node* end_container,
                     unsigned end_offset) {
  return new Range(owner_document, start_container, start_offset, end_container,
                   end_offset);
}

Range* Range::Create(Document& owner_document,
                     const Position& start,
                     const Position& end) {
  return new Range(owner_document, start.ComputeContainerNode(),
                   start.ComputeOffsetInContainerNode(),
                   end.ComputeContainerNode(),
                   end.ComputeOffsetInContainerNode());
}

// TODO(yosin): We should move |Range::createAdjustedToTreeScope()| to
// "Document.cpp" since it is use only one place in "Document.cpp".
Range* Range::CreateAdjustedToTreeScope(const TreeScope& tree_scope,
                                        const Position& position) {
  DCHECK(position.IsNotNull());
  // Note: Since |Position::computeContanerNode()| returns |nullptr| if
  // |position| is |BeforeAnchor| or |AfterAnchor|.
  Node* const anchor_node = position.AnchorNode();
  if (anchor_node->GetTreeScope() == tree_scope)
    return Create(tree_scope.GetDocument(), position, position);
  Node* const shadow_host = tree_scope.AncestorInThisScope(anchor_node);
  return Range::Create(tree_scope.GetDocument(),
                       Position::BeforeNode(shadow_host),
                       Position::BeforeNode(shadow_host));
}

void Range::Dispose() {
  // A prompt detach from the owning Document helps avoid GC overhead.
  owner_document_->DetachRange(this);
}

bool Range::IsConnected() const {
  DCHECK_EQ(start_.IsConnected(), end_.IsConnected());
  return start_.IsConnected();
}

void Range::SetDocument(Document& document) {
  DCHECK_NE(owner_document_, document);
  DCHECK(owner_document_);
  owner_document_->DetachRange(this);
  owner_document_ = &document;
  start_.SetToStartOfNode(document);
  end_.SetToStartOfNode(document);
  owner_document_->AttachRange(this);
}

Node* Range::commonAncestorContainer() const {
  return commonAncestorContainer(&start_.Container(), &end_.Container());
}

Node* Range::commonAncestorContainer(const Node* container_a,
                                     const Node* container_b) {
  if (!container_a || !container_b)
    return nullptr;
  return container_a->CommonAncestor(*container_b, NodeTraversal::Parent);
}

static inline bool CheckForDifferentRootContainer(
    const RangeBoundaryPoint& start,
    const RangeBoundaryPoint& end) {
  Node* end_root_container = &end.Container();
  while (end_root_container->parentNode())
    end_root_container = end_root_container->parentNode();
  Node* start_root_container = &start.Container();
  while (start_root_container->parentNode())
    start_root_container = start_root_container->parentNode();

  return start_root_container != end_root_container ||
         (Range::compareBoundaryPoints(start, end, ASSERT_NO_EXCEPTION) > 0);
}

void Range::setStart(Node* ref_node,
                     unsigned offset,
                     ExceptionState& exception_state) {
  if (!ref_node) {
    // FIXME: Generated bindings code never calls with null, and neither should
    // other callers!
    exception_state.ThrowTypeError("The node provided is null.");
    return;
  }

  RangeUpdateScope scope(this);
  bool did_move_document = false;
  if (ref_node->GetDocument() != owner_document_) {
    SetDocument(ref_node->GetDocument());
    did_move_document = true;
  }

  Node* child_node = CheckNodeWOffset(ref_node, offset, exception_state);
  if (exception_state.HadException())
    return;

  start_.Set(*ref_node, offset, child_node);

  if (did_move_document || CheckForDifferentRootContainer(start_, end_))
    collapse(true);
}

void Range::setEnd(Node* ref_node,
                   unsigned offset,
                   ExceptionState& exception_state) {
  if (!ref_node) {
    // FIXME: Generated bindings code never calls with null, and neither should
    // other callers!
    exception_state.ThrowTypeError("The node provided is null.");
    return;
  }

  RangeUpdateScope scope(this);
  bool did_move_document = false;
  if (ref_node->GetDocument() != owner_document_) {
    SetDocument(ref_node->GetDocument());
    did_move_document = true;
  }

  Node* child_node = CheckNodeWOffset(ref_node, offset, exception_state);
  if (exception_state.HadException())
    return;

  end_.Set(*ref_node, offset, child_node);

  if (did_move_document || CheckForDifferentRootContainer(start_, end_))
    collapse(false);
}

void Range::setStart(const Position& start, ExceptionState& exception_state) {
  Position parent_anchored = start.ParentAnchoredEquivalent();
  setStart(parent_anchored.ComputeContainerNode(),
           parent_anchored.OffsetInContainerNode(), exception_state);
}

void Range::setEnd(const Position& end, ExceptionState& exception_state) {
  Position parent_anchored = end.ParentAnchoredEquivalent();
  setEnd(parent_anchored.ComputeContainerNode(),
         parent_anchored.OffsetInContainerNode(), exception_state);
}

void Range::collapse(bool to_start) {
  RangeUpdateScope scope(this);
  if (to_start)
    end_ = start_;
  else
    start_ = end_;
}

bool Range::HasSameRoot(const Node& node) const {
  if (node.GetDocument() != owner_document_)
    return false;
  // commonAncestorContainer() is O(depth). We should avoid to call it in common
  // cases.
  if (node.IsInTreeScope() && start_.Container().IsInTreeScope() &&
      &node.GetTreeScope() == &start_.Container().GetTreeScope())
    return true;
  return node.CommonAncestor(start_.Container(), NodeTraversal::Parent);
}

bool Range::isPointInRange(Node* ref_node,
                           unsigned offset,
                           ExceptionState& exception_state) const {
  if (!ref_node) {
    // FIXME: Generated bindings code never calls with null, and neither should
    // other callers!
    exception_state.ThrowTypeError("The node provided is null.");
    return false;
  }
  if (!HasSameRoot(*ref_node))
    return false;

  CheckNodeWOffset(ref_node, offset, exception_state);
  if (exception_state.HadException())
    return false;

  return compareBoundaryPoints(ref_node, offset, &start_.Container(),
                               start_.Offset(), exception_state) >= 0 &&
         !exception_state.HadException() &&
         compareBoundaryPoints(ref_node, offset, &end_.Container(),
                               end_.Offset(), exception_state) <= 0 &&
         !exception_state.HadException();
}

short Range::comparePoint(Node* ref_node,
                          unsigned offset,
                          ExceptionState& exception_state) const {
  // http://developer.mozilla.org/en/docs/DOM:range.comparePoint
  // This method returns -1, 0 or 1 depending on if the point described by the
  // refNode node and an offset within the node is before, same as, or after the
  // range respectively.

  if (!HasSameRoot(*ref_node)) {
    exception_state.ThrowDOMException(
        kWrongDocumentError,
        "The node provided and the Range are not in the same tree.");
    return 0;
  }

  CheckNodeWOffset(ref_node, offset, exception_state);
  if (exception_state.HadException())
    return 0;

  // compare to start, and point comes before
  if (compareBoundaryPoints(ref_node, offset, &start_.Container(),
                            start_.Offset(), exception_state) < 0)
    return -1;

  if (exception_state.HadException())
    return 0;

  // compare to end, and point comes after
  if (compareBoundaryPoints(ref_node, offset, &end_.Container(), end_.Offset(),
                            exception_state) > 0 &&
      !exception_state.HadException())
    return 1;

  // point is in the middle of this range, or on the boundary points
  return 0;
}

short Range::compareBoundaryPoints(unsigned how,
                                   const Range* source_range,
                                   ExceptionState& exception_state) const {
  if (!(how == kStartToStart || how == kStartToEnd || how == kEndToEnd ||
        how == kEndToStart)) {
    exception_state.ThrowDOMException(
        kNotSupportedError,
        "The comparison method provided must be "
        "one of 'START_TO_START', 'START_TO_END', "
        "'END_TO_END', or 'END_TO_START'.");
    return 0;
  }

  Node* this_cont = commonAncestorContainer();
  Node* source_cont = source_range->commonAncestorContainer();
  if (this_cont->GetDocument() != source_cont->GetDocument()) {
    exception_state.ThrowDOMException(
        kWrongDocumentError,
        "The source range is in a different document than this range.");
    return 0;
  }

  Node* this_top = this_cont;
  Node* source_top = source_cont;
  while (this_top->parentNode())
    this_top = this_top->parentNode();
  while (source_top->parentNode())
    source_top = source_top->parentNode();
  if (this_top != source_top) {  // in different DocumentFragments
    exception_state.ThrowDOMException(
        kWrongDocumentError,
        "The source range is in a different document than this range.");
    return 0;
  }

  switch (how) {
    case kStartToStart:
      return compareBoundaryPoints(start_, source_range->start_,
                                   exception_state);
    case kStartToEnd:
      return compareBoundaryPoints(end_, source_range->start_, exception_state);
    case kEndToEnd:
      return compareBoundaryPoints(end_, source_range->end_, exception_state);
    case kEndToStart:
      return compareBoundaryPoints(start_, source_range->end_, exception_state);
  }

  NOTREACHED();
  return 0;
}

short Range::compareBoundaryPoints(Node* container_a,
                                   unsigned offset_a,
                                   Node* container_b,
                                   unsigned offset_b,
                                   ExceptionState& exception_state) {
  bool disconnected = false;
  short result = ComparePositionsInDOMTree(container_a, offset_a, container_b,
                                           offset_b, &disconnected);
  if (disconnected) {
    exception_state.ThrowDOMException(
        kWrongDocumentError, "The two ranges are in separate documents.");
    return 0;
  }
  return result;
}

short Range::compareBoundaryPoints(const RangeBoundaryPoint& boundary_a,
                                   const RangeBoundaryPoint& boundary_b,
                                   ExceptionState& exception_state) {
  return compareBoundaryPoints(&boundary_a.Container(), boundary_a.Offset(),
                               &boundary_b.Container(), boundary_b.Offset(),
                               exception_state);
}

bool Range::BoundaryPointsValid() const {
  DummyExceptionStateForTesting exception_state;
  return compareBoundaryPoints(start_, end_, exception_state) <= 0 &&
         !exception_state.HadException();
}

void Range::deleteContents(ExceptionState& exception_state) {
  DCHECK(BoundaryPointsValid());

  {
    EventQueueScope event_queue_scope;
    ProcessContents(DELETE_CONTENTS, exception_state);
  }
}

bool Range::intersectsNode(Node* ref_node, ExceptionState& exception_state) {
  // http://developer.mozilla.org/en/docs/DOM:range.intersectsNode
  // Returns a bool if the node intersects the range.
  if (!ref_node) {
    // FIXME: Generated bindings code never calls with null, and neither should
    // other callers!
    exception_state.ThrowTypeError("The node provided is null.");
    return false;
  }
  if (!HasSameRoot(*ref_node))
    return false;

  ContainerNode* parent_node = ref_node->parentNode();
  if (!parent_node)
    return true;

  int node_index = ref_node->NodeIndex();

  if (comparePoint(parent_node, node_index, exception_state) <
          0  // starts before start
      && comparePoint(parent_node, node_index + 1, exception_state) <
             0) {  // ends before start
    return false;
  }

  if (comparePoint(parent_node, node_index, exception_state) >
          0  // starts after end
      && comparePoint(parent_node, node_index + 1, exception_state) >
             0) {  // ends after end
    return false;
  }

  return true;  // all other cases
}

static inline Node* HighestAncestorUnderCommonRoot(Node* node,
                                                   Node* common_root) {
  if (node == common_root)
    return 0;

  DCHECK(common_root->contains(node));

  while (node->parentNode() != common_root)
    node = node->parentNode();

  return node;
}

static inline Node* ChildOfCommonRootBeforeOffset(Node* container,
                                                  unsigned offset,
                                                  Node* common_root) {
  DCHECK(container);
  DCHECK(common_root);

  if (!common_root->contains(container))
    return 0;

  if (container == common_root) {
    container = container->firstChild();
    for (unsigned i = 0; container && i < offset; i++)
      container = container->nextSibling();
  } else {
    while (container->parentNode() != common_root)
      container = container->parentNode();
  }

  return container;
}

static unsigned LengthOfContents(const Node* node) {
  // This switch statement must be consistent with that of
  // Range::processContentsBetweenOffsets.
  switch (node->getNodeType()) {
    case Node::kTextNode:
    case Node::kCdataSectionNode:
    case Node::kCommentNode:
    case Node::kProcessingInstructionNode:
      return ToCharacterData(node)->length();
    case Node::kElementNode:
    case Node::kDocumentNode:
    case Node::kDocumentFragmentNode:
      return ToContainerNode(node)->CountChildren();
    case Node::kAttributeNode:
    case Node::kDocumentTypeNode:
      return 0;
  }
  NOTREACHED();
  return 0;
}

DocumentFragment* Range::ProcessContents(ActionType action,
                                         ExceptionState& exception_state) {
  typedef HeapVector<Member<Node>> NodeVector;

  DocumentFragment* fragment = nullptr;
  if (action == EXTRACT_CONTENTS || action == CLONE_CONTENTS)
    fragment = DocumentFragment::Create(*owner_document_.Get());

  if (collapsed())
    return fragment;

  Node* common_root = commonAncestorContainer();
  DCHECK(common_root);

  if (start_.Container() == end_.Container()) {
    ProcessContentsBetweenOffsets(action, fragment, &start_.Container(),
                                  start_.Offset(), end_.Offset(),
                                  exception_state);
    return fragment;
  }

  // Since mutation observers can modify the range during the process, the
  // boundary points need to be saved.
  const RangeBoundaryPoint original_start(start_);
  const RangeBoundaryPoint original_end(end_);

  // what is the highest node that partially selects the start / end of the
  // range?
  Node* partial_start =
      HighestAncestorUnderCommonRoot(&original_start.Container(), common_root);
  Node* partial_end =
      HighestAncestorUnderCommonRoot(&original_end.Container(), common_root);

  // Start and end containers are different.
  // There are three possibilities here:
  // 1. Start container == commonRoot (End container must be a descendant)
  // 2. End container == commonRoot (Start container must be a descendant)
  // 3. Neither is commonRoot, they are both descendants
  //
  // In case 3, we grab everything after the start (up until a direct child
  // of commonRoot) into leftContents, and everything before the end (up until
  // a direct child of commonRoot) into rightContents. Then we process all
  // commonRoot children between leftContents and rightContents
  //
  // In case 1 or 2, we skip either processing of leftContents or rightContents,
  // in which case the last lot of nodes either goes from the first or last
  // child of commonRoot.
  //
  // These are deleted, cloned, or extracted (i.e. both) depending on action.

  // Note that we are verifying that our common root hierarchy is still intact
  // after any DOM mutation event, at various stages below. See webkit bug
  // 60350.

  Node* left_contents = nullptr;
  if (original_start.Container() != common_root &&
      common_root->contains(&original_start.Container())) {
    left_contents = ProcessContentsBetweenOffsets(
        action, nullptr, &original_start.Container(), original_start.Offset(),
        LengthOfContents(&original_start.Container()), exception_state);
    left_contents = ProcessAncestorsAndTheirSiblings(
        action, &original_start.Container(), kProcessContentsForward,
        left_contents, common_root, exception_state);
  }

  Node* right_contents = nullptr;
  if (end_.Container() != common_root &&
      common_root->contains(&original_end.Container())) {
    right_contents = ProcessContentsBetweenOffsets(
        action, nullptr, &original_end.Container(), 0, original_end.Offset(),
        exception_state);
    right_contents = ProcessAncestorsAndTheirSiblings(
        action, &original_end.Container(), kProcessContentsBackward,
        right_contents, common_root, exception_state);
  }

  // delete all children of commonRoot between the start and end container
  Node* process_start = ChildOfCommonRootBeforeOffset(
      &original_start.Container(), original_start.Offset(), common_root);
  if (process_start &&
      original_start.Container() !=
          common_root)  // processStart contains nodes before m_start.
    process_start = process_start->nextSibling();
  Node* process_end = ChildOfCommonRootBeforeOffset(
      &original_end.Container(), original_end.Offset(), common_root);

  // Collapse the range, making sure that the result is not within a node that
  // was partially selected.
  if (action == EXTRACT_CONTENTS || action == DELETE_CONTENTS) {
    if (partial_start && common_root->contains(partial_start)) {
      // FIXME: We should not continue if we have an earlier error.
      exception_state.ClearException();
      setStart(partial_start->parentNode(), partial_start->NodeIndex() + 1,
               exception_state);
    } else if (partial_end && common_root->contains(partial_end)) {
      // FIXME: We should not continue if we have an earlier error.
      exception_state.ClearException();
      setStart(partial_end->parentNode(), partial_end->NodeIndex(),
               exception_state);
    }
    if (exception_state.HadException())
      return nullptr;
    end_ = start_;
  }

  // Now add leftContents, stuff in between, and rightContents to the fragment
  // (or just delete the stuff in between)

  if ((action == EXTRACT_CONTENTS || action == CLONE_CONTENTS) && left_contents)
    fragment->AppendChild(left_contents, exception_state);

  if (process_start) {
    NodeVector nodes;
    for (Node* n = process_start; n && n != process_end; n = n->nextSibling())
      nodes.push_back(n);
    ProcessNodes(action, nodes, common_root, fragment, exception_state);
  }

  if ((action == EXTRACT_CONTENTS || action == CLONE_CONTENTS) &&
      right_contents)
    fragment->AppendChild(right_contents, exception_state);

  return fragment;
}

static inline void DeleteCharacterData(CharacterData* data,
                                       unsigned start_offset,
                                       unsigned end_offset,
                                       ExceptionState& exception_state) {
  if (data->length() - end_offset)
    data->deleteData(end_offset, data->length() - end_offset, exception_state);
  if (start_offset)
    data->deleteData(0, start_offset, exception_state);
}

Node* Range::ProcessContentsBetweenOffsets(ActionType action,
                                           DocumentFragment* fragment,
                                           Node* container,
                                           unsigned start_offset,
                                           unsigned end_offset,
                                           ExceptionState& exception_state) {
  DCHECK(container);
  DCHECK_LE(start_offset, end_offset);

  // This switch statement must be consistent with that of
  // lengthOfContents.
  Node* result = nullptr;
  switch (container->getNodeType()) {
    case Node::kTextNode:
    case Node::kCdataSectionNode:
    case Node::kCommentNode:
    case Node::kProcessingInstructionNode:
      end_offset = std::min(end_offset, ToCharacterData(container)->length());
      if (action == EXTRACT_CONTENTS || action == CLONE_CONTENTS) {
        CharacterData* c =
            static_cast<CharacterData*>(container->cloneNode(true));
        DeleteCharacterData(c, start_offset, end_offset, exception_state);
        if (fragment) {
          result = fragment;
          result->appendChild(c, exception_state);
        } else {
          result = c;
        }
      }
      if (action == EXTRACT_CONTENTS || action == DELETE_CONTENTS)
        ToCharacterData(container)->deleteData(
            start_offset, end_offset - start_offset, exception_state);
      break;
    case Node::kElementNode:
    case Node::kAttributeNode:
    case Node::kDocumentNode:
    case Node::kDocumentTypeNode:
    case Node::kDocumentFragmentNode:
      // FIXME: Should we assert that some nodes never appear here?
      if (action == EXTRACT_CONTENTS || action == CLONE_CONTENTS) {
        if (fragment)
          result = fragment;
        else
          result = container->cloneNode(false);
      }

      Node* n = container->firstChild();
      HeapVector<Member<Node>> nodes;
      for (unsigned i = start_offset; n && i; i--)
        n = n->nextSibling();
      for (unsigned i = start_offset; n && i < end_offset;
           i++, n = n->nextSibling())
        nodes.push_back(n);

      ProcessNodes(action, nodes, container, result, exception_state);
      break;
  }

  return result;
}

void Range::ProcessNodes(ActionType action,
                         HeapVector<Member<Node>>& nodes,
                         Node* old_container,
                         Node* new_container,
                         ExceptionState& exception_state) {
  for (auto& node : nodes) {
    switch (action) {
      case DELETE_CONTENTS:
        old_container->removeChild(node.Get(), exception_state);
        break;
      case EXTRACT_CONTENTS:
        new_container->appendChild(
            node.Release(), exception_state);  // Will remove n from its parent.
        break;
      case CLONE_CONTENTS:
        new_container->appendChild(node->cloneNode(true), exception_state);
        break;
    }
  }
}

Node* Range::ProcessAncestorsAndTheirSiblings(
    ActionType action,
    Node* container,
    ContentsProcessDirection direction,
    Node* cloned_container,
    Node* common_root,
    ExceptionState& exception_state) {
  typedef HeapVector<Member<Node>> NodeVector;

  NodeVector ancestors;
  for (Node& runner : NodeTraversal::AncestorsOf(*container)) {
    if (runner == common_root)
      break;
    ancestors.push_back(runner);
  }

  Node* first_child_in_ancestor_to_process =
      direction == kProcessContentsForward ? container->nextSibling()
                                           : container->previousSibling();
  for (const auto& ancestor : ancestors) {
    if (action == EXTRACT_CONTENTS || action == CLONE_CONTENTS) {
      // Might have been removed already during mutation event.
      if (Node* cloned_ancestor = ancestor->cloneNode(false)) {
        cloned_ancestor->appendChild(cloned_container, exception_state);
        cloned_container = cloned_ancestor;
      }
    }

    // Copy siblings of an ancestor of start/end containers
    // FIXME: This assertion may fail if DOM is modified during mutation event
    // FIXME: Share code with Range::processNodes
    DCHECK(!first_child_in_ancestor_to_process ||
           first_child_in_ancestor_to_process->parentNode() == ancestor);

    NodeVector nodes;
    for (Node* child = first_child_in_ancestor_to_process; child;
         child = (direction == kProcessContentsForward)
                     ? child->nextSibling()
                     : child->previousSibling())
      nodes.push_back(child);

    for (const auto& node : nodes) {
      Node* child = node.Get();
      switch (action) {
        case DELETE_CONTENTS:
          // Prior call of ancestor->removeChild() may cause a tree change due
          // to DOMSubtreeModified event.  Therefore, we need to make sure
          // |ancestor| is still |child|'s parent.
          if (ancestor == child->parentNode())
            ancestor->removeChild(child, exception_state);
          break;
        case EXTRACT_CONTENTS:  // will remove child from ancestor
          if (direction == kProcessContentsForward)
            cloned_container->appendChild(child, exception_state);
          else
            cloned_container->insertBefore(
                child, cloned_container->firstChild(), exception_state);
          break;
        case CLONE_CONTENTS:
          if (direction == kProcessContentsForward)
            cloned_container->appendChild(child->cloneNode(true),
                                          exception_state);
          else
            cloned_container->insertBefore(child->cloneNode(true),
                                           cloned_container->firstChild(),
                                           exception_state);
          break;
      }
    }
    first_child_in_ancestor_to_process = direction == kProcessContentsForward
                                             ? ancestor->nextSibling()
                                             : ancestor->previousSibling();
  }

  return cloned_container;
}

DocumentFragment* Range::extractContents(ExceptionState& exception_state) {
  CheckExtractPrecondition(exception_state);
  if (exception_state.HadException())
    return nullptr;

  EventQueueScope scope;
  return ProcessContents(EXTRACT_CONTENTS, exception_state);
}

DocumentFragment* Range::cloneContents(ExceptionState& exception_state) {
  return ProcessContents(CLONE_CONTENTS, exception_state);
}

void Range::insertNode(Node* new_node, ExceptionState& exception_state) {
  if (!new_node) {
    // FIXME: Generated bindings code never calls with null, and neither should
    // other callers!
    exception_state.ThrowTypeError("The node provided is null.");
    return;
  }

  // HierarchyRequestError: Raised if the container of the start of the Range is
  // of a type that does not allow children of the type of newNode or if newNode
  // is an ancestor of the container.

  // an extra one here - if a text node is going to split, it must have a parent
  // to insert into
  bool start_is_text = start_.Container().IsTextNode();
  if (start_is_text && !start_.Container().parentNode()) {
    exception_state.ThrowDOMException(kHierarchyRequestError,
                                      "This operation would split a text node, "
                                      "but there's no parent into which to "
                                      "insert.");
    return;
  }

  // In the case where the container is a text node, we check against the
  // container's parent, because text nodes get split up upon insertion.
  Node* check_against;
  if (start_is_text)
    check_against = start_.Container().parentNode();
  else
    check_against = &start_.Container();

  Node::NodeType new_node_type = new_node->getNodeType();
  int num_new_children;
  if (new_node_type == Node::kDocumentFragmentNode &&
      !new_node->IsShadowRoot()) {
    // check each child node, not the DocumentFragment itself
    num_new_children = 0;
    for (Node* c = ToDocumentFragment(new_node)->firstChild(); c;
         c = c->nextSibling()) {
      if (!check_against->ChildTypeAllowed(c->getNodeType())) {
        exception_state.ThrowDOMException(
            kHierarchyRequestError,
            "The node to be inserted contains a '" + c->nodeName() +
                "' node, which may not be inserted here.");
        return;
      }
      ++num_new_children;
    }
  } else {
    num_new_children = 1;
    if (!check_against->ChildTypeAllowed(new_node_type)) {
      exception_state.ThrowDOMException(
          kHierarchyRequestError,
          "The node to be inserted is a '" + new_node->nodeName() +
              "' node, which may not be inserted here.");
      return;
    }
  }

  for (Node& node : NodeTraversal::InclusiveAncestorsOf(start_.Container())) {
    if (node == new_node) {
      exception_state.ThrowDOMException(kHierarchyRequestError,
                                        "The node to be inserted contains the "
                                        "insertion point; it may not be "
                                        "inserted into itself.");
      return;
    }
  }

  // InvalidNodeTypeError: Raised if newNode is an Attr, Entity, Notation,
  // ShadowRoot or Document node.
  switch (new_node_type) {
    case Node::kAttributeNode:
    case Node::kDocumentNode:
      exception_state.ThrowDOMException(
          kInvalidNodeTypeError, "The node to be inserted is a '" +
                                     new_node->nodeName() +
                                     "' node, which may not be inserted here.");
      return;
    default:
      if (new_node->IsShadowRoot()) {
        exception_state.ThrowDOMException(kInvalidNodeTypeError,
                                          "The node to be inserted is a shadow "
                                          "root, which may not be inserted "
                                          "here.");
        return;
      }
      break;
  }

  EventQueueScope scope;
  bool collapsed = start_ == end_;
  Node* container = nullptr;
  if (start_is_text) {
    container = &start_.Container();
    Text* new_text =
        ToText(container)->splitText(start_.Offset(), exception_state);
    if (exception_state.HadException())
      return;

    container = &start_.Container();
    container->parentNode()->InsertBefore(new_node, new_text, exception_state);
    if (exception_state.HadException())
      return;

    if (collapsed) {
      // Some types of events don't support EventQueueScope.  Given
      // circumstance may mutate the tree so newText->parentNode() may
      // become null.
      if (!new_text->parentNode()) {
        exception_state.ThrowDOMException(
            kHierarchyRequestError,
            "This operation would set range's end to parent with new offset, "
            "but there's no parent into which to continue.");
        return;
      }
      end_.SetToBeforeChild(*new_text);
    }
  } else {
    Node* last_child = (new_node_type == Node::kDocumentFragmentNode)
                           ? ToDocumentFragment(new_node)->lastChild()
                           : new_node;
    if (last_child && last_child == start_.ChildBefore()) {
      // The insertion will do nothing, but we need to extend the range to
      // include the inserted nodes.
      Node* first_child = (new_node_type == Node::kDocumentFragmentNode)
                              ? ToDocumentFragment(new_node)->firstChild()
                              : new_node;
      DCHECK(first_child);
      start_.SetToBeforeChild(*first_child);
      return;
    }

    container = &start_.Container();
    Node* reference_node = NodeTraversal::ChildAt(*container, start_.Offset());
    // TODO(tkent): The following check must be unnecessary if we follow the
    // algorithm defined in the specification.
    // https://dom.spec.whatwg.org/#concept-range-insert
    if (new_node != reference_node) {
      container->insertBefore(new_node, reference_node, exception_state);
      if (exception_state.HadException())
        return;
    }

    // Note that m_start.offset() may have changed as a result of
    // container->insertBefore, when the node we are inserting comes before the
    // range in the same container.
    if (collapsed && num_new_children)
      end_.Set(start_.Container(), start_.Offset() + num_new_children,
               last_child);
  }
}

String Range::toString() const {
  StringBuilder builder;

  Node* past_last = PastLastNode();
  for (Node* n = FirstNode(); n != past_last; n = NodeTraversal::Next(*n)) {
    Node::NodeType type = n->getNodeType();
    if (type == Node::kTextNode || type == Node::kCdataSectionNode) {
      String data = ToCharacterData(n)->data();
      unsigned length = data.length();
      unsigned start =
          (n == start_.Container()) ? std::min(start_.Offset(), length) : 0;
      unsigned end = (n == end_.Container())
                         ? std::min(std::max(start, end_.Offset()), length)
                         : length;
      builder.Append(data, start, end - start);
    }
  }

  return builder.ToString();
}

String Range::GetText() const {
  DCHECK(!owner_document_->NeedsLayoutTreeUpdate());
  return PlainText(EphemeralRange(this),
                   TextIteratorBehavior::Builder()
                       .SetEmitsObjectReplacementCharacter(true)
                       .Build());
}

DocumentFragment* Range::createContextualFragment(
    const String& markup,
    ExceptionState& exception_state) {
  // Algorithm:
  // http://domparsing.spec.whatwg.org/#extensions-to-the-range-interface

  Node* node = &start_.Container();

  // Step 1.
  Element* element;
  if (!start_.Offset() &&
      (node->IsDocumentNode() || node->IsDocumentFragment()))
    element = nullptr;
  else if (node->IsElementNode())
    element = ToElement(node);
  else
    element = node->parentElement();

  // Step 2.
  if (!element || isHTMLHtmlElement(element)) {
    Document& document = node->GetDocument();

    if (document.IsSVGDocument()) {
      element = document.documentElement();
      if (!element)
        element = SVGSVGElement::Create(document);
    } else {
      // Optimization over spec: try to reuse the existing <body> element, if it
      // is available.
      element = document.body();
      if (!element)
        element = HTMLBodyElement::Create(document);
    }
  }

  // Steps 3, 4, 5.
  return blink::CreateContextualFragment(
      markup, element, kAllowScriptingContentAndDoNotMarkAlreadyStarted,
      exception_state);
}

void Range::detach() {
  // This is now a no-op as per the DOM specification.
}

Node* Range::CheckNodeWOffset(Node* n,
                              unsigned offset,
                              ExceptionState& exception_state) {
  switch (n->getNodeType()) {
    case Node::kDocumentTypeNode:
      exception_state.ThrowDOMException(
          kInvalidNodeTypeError,
          "The node provided is of type '" + n->nodeName() + "'.");
      return nullptr;
    case Node::kCdataSectionNode:
    case Node::kCommentNode:
    case Node::kTextNode:
      if (offset > ToCharacterData(n)->length()) {
        exception_state.ThrowDOMException(
            kIndexSizeError, "The offset " + String::Number(offset) +
                                 " is larger than the node's length (" +
                                 String::Number(ToCharacterData(n)->length()) +
                                 ").");
      } else if (offset >
                 static_cast<unsigned>(std::numeric_limits<int>::max())) {
        exception_state.ThrowDOMException(
            kIndexSizeError,
            "The offset " + String::Number(offset) + " is invalid.");
      }
      return nullptr;
    case Node::kProcessingInstructionNode:
      if (offset > ToProcessingInstruction(n)->data().length()) {
        exception_state.ThrowDOMException(
            kIndexSizeError,
            "The offset " + String::Number(offset) +
                " is larger than the node's length (" +
                String::Number(ToProcessingInstruction(n)->data().length()) +
                ").");
      } else if (offset >
                 static_cast<unsigned>(std::numeric_limits<int>::max())) {
        exception_state.ThrowDOMException(
            kIndexSizeError,
            "The offset " + String::Number(offset) + " is invalid.");
      }
      return nullptr;
    case Node::kAttributeNode:
    case Node::kDocumentFragmentNode:
    case Node::kDocumentNode:
    case Node::kElementNode: {
      if (!offset)
        return nullptr;
      if (offset > static_cast<unsigned>(std::numeric_limits<int>::max())) {
        exception_state.ThrowDOMException(
            kIndexSizeError,
            "The offset " + String::Number(offset) + " is invalid.");
        return nullptr;
      }
      Node* child_before = NodeTraversal::ChildAt(*n, offset - 1);
      if (!child_before) {
        exception_state.ThrowDOMException(
            kIndexSizeError,
            "There is no child at offset " + String::Number(offset) + ".");
      }
      return child_before;
    }
  }
  NOTREACHED();
  return nullptr;
}

void Range::CheckNodeBA(Node* n, ExceptionState& exception_state) const {
  if (!n) {
    // FIXME: Generated bindings code never calls with null, and neither should
    // other callers!
    exception_state.ThrowTypeError("The node provided is null.");
    return;
  }

  // InvalidNodeTypeError: Raised if the root container of refNode is not an
  // Attr, Document, DocumentFragment or ShadowRoot node, or part of a SVG
  // shadow DOM tree, or if refNode is a Document, DocumentFragment, ShadowRoot,
  // Attr, Entity, or Notation node.

  if (!n->parentNode()) {
    exception_state.ThrowDOMException(kInvalidNodeTypeError,
                                      "the given Node has no parent.");
    return;
  }

  switch (n->getNodeType()) {
    case Node::kAttributeNode:
    case Node::kDocumentFragmentNode:
    case Node::kDocumentNode:
      exception_state.ThrowDOMException(
          kInvalidNodeTypeError,
          "The node provided is of type '" + n->nodeName() + "'.");
      return;
    case Node::kCdataSectionNode:
    case Node::kCommentNode:
    case Node::kDocumentTypeNode:
    case Node::kElementNode:
    case Node::kProcessingInstructionNode:
    case Node::kTextNode:
      break;
  }

  Node* root = n;
  while (ContainerNode* parent = root->parentNode())
    root = parent;

  switch (root->getNodeType()) {
    case Node::kAttributeNode:
    case Node::kDocumentNode:
    case Node::kDocumentFragmentNode:
    case Node::kElementNode:
      break;
    case Node::kCdataSectionNode:
    case Node::kCommentNode:
    case Node::kDocumentTypeNode:
    case Node::kProcessingInstructionNode:
    case Node::kTextNode:
      exception_state.ThrowDOMException(
          kInvalidNodeTypeError,
          "The node provided is of type '" + n->nodeName() + "'.");
      return;
  }
}

Range* Range::cloneRange() const {
  return Range::Create(*owner_document_.Get(), &start_.Container(),
                       start_.Offset(), &end_.Container(), end_.Offset());
}

void Range::setStartAfter(Node* ref_node, ExceptionState& exception_state) {
  CheckNodeBA(ref_node, exception_state);
  if (exception_state.HadException())
    return;

  setStart(ref_node->parentNode(), ref_node->NodeIndex() + 1, exception_state);
}

void Range::setEndBefore(Node* ref_node, ExceptionState& exception_state) {
  CheckNodeBA(ref_node, exception_state);
  if (exception_state.HadException())
    return;

  setEnd(ref_node->parentNode(), ref_node->NodeIndex(), exception_state);
}

void Range::setEndAfter(Node* ref_node, ExceptionState& exception_state) {
  CheckNodeBA(ref_node, exception_state);
  if (exception_state.HadException())
    return;

  setEnd(ref_node->parentNode(), ref_node->NodeIndex() + 1, exception_state);
}

void Range::selectNode(Node* ref_node, ExceptionState& exception_state) {
  if (!ref_node) {
    // FIXME: Generated bindings code never calls with null, and neither should
    // other callers!
    exception_state.ThrowTypeError("The node provided is null.");
    return;
  }

  if (!ref_node->parentNode()) {
    exception_state.ThrowDOMException(kInvalidNodeTypeError,
                                      "the given Node has no parent.");
    return;
  }

  switch (ref_node->getNodeType()) {
    case Node::kCdataSectionNode:
    case Node::kCommentNode:
    case Node::kDocumentTypeNode:
    case Node::kElementNode:
    case Node::kProcessingInstructionNode:
    case Node::kTextNode:
      break;
    case Node::kAttributeNode:
    case Node::kDocumentFragmentNode:
    case Node::kDocumentNode:
      exception_state.ThrowDOMException(
          kInvalidNodeTypeError,
          "The node provided is of type '" + ref_node->nodeName() + "'.");
      return;
  }

  RangeUpdateScope scope(this);
  setStartBefore(ref_node);
  setEndAfter(ref_node);
}

void Range::selectNodeContents(Node* ref_node,
                               ExceptionState& exception_state) {
  if (!ref_node) {
    // FIXME: Generated bindings code never calls with null, and neither should
    // other callers!
    exception_state.ThrowTypeError("The node provided is null.");
    return;
  }

  // InvalidNodeTypeError: Raised if refNode or an ancestor of refNode is an
  // Entity, Notation
  // or DocumentType node.
  for (Node* n = ref_node; n; n = n->parentNode()) {
    switch (n->getNodeType()) {
      case Node::kAttributeNode:
      case Node::kCdataSectionNode:
      case Node::kCommentNode:
      case Node::kDocumentFragmentNode:
      case Node::kDocumentNode:
      case Node::kElementNode:
      case Node::kProcessingInstructionNode:
      case Node::kTextNode:
        break;
      case Node::kDocumentTypeNode:
        exception_state.ThrowDOMException(
            kInvalidNodeTypeError,
            "The node provided is of type '" + ref_node->nodeName() + "'.");
        return;
    }
  }

  RangeUpdateScope scope(this);
  if (owner_document_ != ref_node->GetDocument())
    SetDocument(ref_node->GetDocument());

  start_.SetToStartOfNode(*ref_node);
  end_.SetToEndOfNode(*ref_node);
}

bool Range::selectNodeContents(Node* ref_node, Position& start, Position& end) {
  if (!ref_node) {
    return false;
  }

  for (Node* n = ref_node; n; n = n->parentNode()) {
    switch (n->getNodeType()) {
      case Node::kAttributeNode:
      case Node::kCdataSectionNode:
      case Node::kCommentNode:
      case Node::kDocumentFragmentNode:
      case Node::kDocumentNode:
      case Node::kElementNode:
      case Node::kProcessingInstructionNode:
      case Node::kTextNode:
        break;
      case Node::kDocumentTypeNode:
        return false;
    }
  }

  RangeBoundaryPoint start_boundary_point(*ref_node);
  start_boundary_point.SetToStartOfNode(*ref_node);
  start = start_boundary_point.ToPosition();
  RangeBoundaryPoint end_boundary_point(*ref_node);
  end_boundary_point.SetToEndOfNode(*ref_node);
  end = end_boundary_point.ToPosition();
  return true;
}

// https://dom.spec.whatwg.org/#dom-range-surroundcontents
void Range::surroundContents(Node* new_parent,
                             ExceptionState& exception_state) {
  if (!new_parent) {
    // FIXME: Generated bindings code never calls with null, and neither should
    // other callers!
    exception_state.ThrowTypeError("The node provided is null.");
    return;
  }

  // 1. If a non-Text node is partially contained in the context object, then
  // throw an InvalidStateError.
  Node* start_non_text_container = &start_.Container();
  if (start_non_text_container->getNodeType() == Node::kTextNode)
    start_non_text_container = start_non_text_container->parentNode();
  Node* end_non_text_container = &end_.Container();
  if (end_non_text_container->getNodeType() == Node::kTextNode)
    end_non_text_container = end_non_text_container->parentNode();
  if (start_non_text_container != end_non_text_container) {
    exception_state.ThrowDOMException(
        kInvalidStateError,
        "The Range has partially selected a non-Text node.");
    return;
  }

  // 2. If newParent is a Document, DocumentType, or DocumentFragment node, then
  // throw an InvalidNodeTypeError.
  switch (new_parent->getNodeType()) {
    case Node::kAttributeNode:
    case Node::kDocumentFragmentNode:
    case Node::kDocumentNode:
    case Node::kDocumentTypeNode:
      exception_state.ThrowDOMException(
          kInvalidNodeTypeError,
          "The node provided is of type '" + new_parent->nodeName() + "'.");
      return;
    case Node::kCdataSectionNode:
    case Node::kCommentNode:
    case Node::kElementNode:
    case Node::kProcessingInstructionNode:
    case Node::kTextNode:
      break;
  }

  EventQueueScope scope;

  // 3. Let fragment be the result of extracting context object.
  DocumentFragment* fragment = extractContents(exception_state);
  if (exception_state.HadException())
    return;

  // 4. If newParent has children, replace all with null within newParent.
  while (Node* n = new_parent->firstChild()) {
    ToContainerNode(new_parent)->RemoveChild(n, exception_state);
    if (exception_state.HadException())
      return;
  }

  // 5. If newParent has children, replace all with null within newParent.
  insertNode(new_parent, exception_state);
  if (exception_state.HadException())
    return;

  // 6. Append fragment to newParent.
  new_parent->appendChild(fragment, exception_state);
  if (exception_state.HadException())
    return;

  // 7. Select newParent within context object.
  selectNode(new_parent, exception_state);
}

void Range::setStartBefore(Node* ref_node, ExceptionState& exception_state) {
  CheckNodeBA(ref_node, exception_state);
  if (exception_state.HadException())
    return;

  setStart(ref_node->parentNode(), ref_node->NodeIndex(), exception_state);
}

void Range::CheckExtractPrecondition(ExceptionState& exception_state) {
  DCHECK(BoundaryPointsValid());

  if (!commonAncestorContainer())
    return;

  Node* past_last = PastLastNode();
  for (Node* n = FirstNode(); n != past_last; n = NodeTraversal::Next(*n)) {
    if (n->IsDocumentTypeNode()) {
      exception_state.ThrowDOMException(kHierarchyRequestError,
                                        "The Range contains a doctype node.");
      return;
    }
  }
}

Node* Range::FirstNode() const {
  return StartPosition().NodeAsRangeFirstNode();
}

Node* Range::PastLastNode() const {
  return EndPosition().NodeAsRangePastLastNode();
}

IntRect Range::BoundingBox() const {
  return ComputeTextRect(EphemeralRange(this));
}

bool AreRangesEqual(const Range* a, const Range* b) {
  if (a == b)
    return true;
  if (!a || !b)
    return false;
  return a->StartPosition() == b->StartPosition() &&
         a->EndPosition() == b->EndPosition();
}

static inline void BoundaryNodeChildrenWillBeRemoved(
    RangeBoundaryPoint& boundary,
    ContainerNode& container) {
  for (Node* node_to_be_removed = container.firstChild(); node_to_be_removed;
       node_to_be_removed = node_to_be_removed->nextSibling()) {
    if (boundary.ChildBefore() == node_to_be_removed) {
      boundary.SetToStartOfNode(container);
      return;
    }

    for (Node* n = &boundary.Container(); n; n = n->parentNode()) {
      if (n == node_to_be_removed) {
        boundary.SetToStartOfNode(container);
        return;
      }
    }
  }
}

void Range::NodeChildrenWillBeRemoved(ContainerNode& container) {
  DCHECK_EQ(container.GetDocument(), owner_document_);
  BoundaryNodeChildrenWillBeRemoved(start_, container);
  BoundaryNodeChildrenWillBeRemoved(end_, container);
}

static inline void BoundaryNodeWillBeRemoved(RangeBoundaryPoint& boundary,
                                             Node& node_to_be_removed) {
  if (boundary.ChildBefore() == node_to_be_removed) {
    boundary.ChildBeforeWillBeRemoved();
    return;
  }

  for (Node* n = &boundary.Container(); n; n = n->parentNode()) {
    if (n == node_to_be_removed) {
      boundary.SetToBeforeChild(node_to_be_removed);
      return;
    }
  }
}

void Range::NodeWillBeRemoved(Node& node) {
  DCHECK_EQ(node.GetDocument(), owner_document_);
  DCHECK_NE(node, owner_document_.Get());

  // FIXME: Once DOMNodeRemovedFromDocument mutation event removed, we
  // should change following if-statement to DCHECK(!node->parentNode).
  if (!node.parentNode())
    return;
  BoundaryNodeWillBeRemoved(start_, node);
  BoundaryNodeWillBeRemoved(end_, node);
}

static inline void BoundaryTextInserted(RangeBoundaryPoint& boundary,
                                        const CharacterData& text,
                                        unsigned offset,
                                        unsigned length) {
  if (boundary.Container() != &text)
    return;
  boundary.MarkValid();
  unsigned boundary_offset = boundary.Offset();
  if (offset >= boundary_offset)
    return;
  boundary.SetOffset(boundary_offset + length);
}

void Range::DidInsertText(const CharacterData& text,
                          unsigned offset,
                          unsigned length) {
  DCHECK_EQ(text.GetDocument(), owner_document_);
  BoundaryTextInserted(start_, text, offset, length);
  BoundaryTextInserted(end_, text, offset, length);
}

static inline void BoundaryTextRemoved(RangeBoundaryPoint& boundary,
                                       const CharacterData& text,
                                       unsigned offset,
                                       unsigned length) {
  if (boundary.Container() != &text)
    return;
  boundary.MarkValid();
  unsigned boundary_offset = boundary.Offset();
  if (offset >= boundary_offset)
    return;
  if (offset + length >= boundary_offset)
    boundary.SetOffset(offset);
  else
    boundary.SetOffset(boundary_offset - length);
}

void Range::DidRemoveText(const CharacterData& text,
                          unsigned offset,
                          unsigned length) {
  DCHECK_EQ(text.GetDocument(), owner_document_);
  BoundaryTextRemoved(start_, text, offset, length);
  BoundaryTextRemoved(end_, text, offset, length);
}

static inline void BoundaryTextNodesMerged(RangeBoundaryPoint& boundary,
                                           const NodeWithIndex& old_node,
                                           unsigned offset) {
  if (boundary.Container() == old_node.GetNode()) {
    Node* const previous_sibling = old_node.GetNode().previousSibling();
    DCHECK(previous_sibling);
    boundary.Set(*previous_sibling, boundary.Offset() + offset, 0);
  } else if (boundary.Container() == old_node.GetNode().parentNode() &&
             boundary.Offset() == static_cast<unsigned>(old_node.Index())) {
    Node* const previous_sibling = old_node.GetNode().previousSibling();
    DCHECK(previous_sibling);
    boundary.Set(*previous_sibling, offset, 0);
  }
}

void Range::DidMergeTextNodes(const NodeWithIndex& old_node, unsigned offset) {
  DCHECK_EQ(old_node.GetNode().GetDocument(), owner_document_);
  DCHECK(old_node.GetNode().parentNode());
  DCHECK(old_node.GetNode().IsTextNode());
  DCHECK(old_node.GetNode().previousSibling());
  DCHECK(old_node.GetNode().previousSibling()->IsTextNode());
  BoundaryTextNodesMerged(start_, old_node, offset);
  BoundaryTextNodesMerged(end_, old_node, offset);
}

void Range::UpdateOwnerDocumentIfNeeded() {
  Document& new_document = start_.Container().GetDocument();
  DCHECK_EQ(new_document, end_.Container().GetDocument());
  if (new_document == owner_document_)
    return;
  owner_document_->DetachRange(this);
  owner_document_ = &new_document;
  owner_document_->AttachRange(this);
}

static inline void BoundaryTextNodeSplit(RangeBoundaryPoint& boundary,
                                         const Text& old_node) {
  unsigned boundary_offset = boundary.Offset();
  if (boundary.ChildBefore() == &old_node) {
    boundary.Set(boundary.Container(), boundary_offset + 1,
                 old_node.nextSibling());
  } else if (boundary.Container() == &old_node &&
             boundary_offset > old_node.length()) {
    Node* const next_sibling = old_node.nextSibling();
    DCHECK(next_sibling);
    boundary.Set(*next_sibling, boundary_offset - old_node.length(), 0);
  }
}

void Range::DidSplitTextNode(const Text& old_node) {
  DCHECK_EQ(old_node.GetDocument(), owner_document_);
  DCHECK(old_node.parentNode());
  DCHECK(old_node.nextSibling());
  DCHECK(old_node.nextSibling()->IsTextNode());
  BoundaryTextNodeSplit(start_, old_node);
  BoundaryTextNodeSplit(end_, old_node);
  DCHECK(BoundaryPointsValid());
}

void Range::expand(const String& unit, ExceptionState& exception_state) {
  if (!StartPosition().IsConnected() || !EndPosition().IsConnected())
    return;
  owner_document_->UpdateStyleAndLayoutIgnorePendingStylesheets();
  VisiblePosition start = CreateVisiblePosition(StartPosition());
  VisiblePosition end = CreateVisiblePosition(EndPosition());
  if (unit == "word") {
    start = StartOfWord(start);
    end = EndOfWord(end);
  } else if (unit == "sentence") {
    start = StartOfSentence(start);
    end = EndOfSentence(end);
  } else if (unit == "block") {
    start = StartOfParagraph(start);
    end = EndOfParagraph(end);
  } else if (unit == "document") {
    start = StartOfDocument(start);
    end = EndOfDocument(end);
  } else {
    return;
  }
  setStart(start.DeepEquivalent().ComputeContainerNode(),
           start.DeepEquivalent().ComputeOffsetInContainerNode(),
           exception_state);
  setEnd(end.DeepEquivalent().ComputeContainerNode(),
         end.DeepEquivalent().ComputeOffsetInContainerNode(), exception_state);
}

ClientRectList* Range::getClientRects() const {
  owner_document_->UpdateStyleAndLayoutIgnorePendingStylesheets();

  Vector<FloatQuad> quads;
  GetBorderAndTextQuads(quads);

  return ClientRectList::Create(quads);
}

ClientRect* Range::getBoundingClientRect() const {
  return ClientRect::Create(BoundingRect());
}

void Range::GetBorderAndTextQuads(Vector<FloatQuad>& quads) const {
  Node* start_container = &start_.Container();
  Node* end_container = &end_.Container();
  Node* stop_node = PastLastNode();

  HeapHashSet<Member<Node>> node_set;
  for (Node* node = FirstNode(); node != stop_node;
       node = NodeTraversal::Next(*node)) {
    if (node->IsElementNode())
      node_set.insert(node);
  }

  for (Node* node = FirstNode(); node != stop_node;
       node = NodeTraversal::Next(*node)) {
    if (node->IsElementNode()) {
      // Exclude start & end container unless the entire corresponding
      // node is included in the range.
      if (!node_set.Contains(node->parentNode()) &&
          (start_container == end_container ||
           (!node->contains(start_container) &&
            !node->contains(end_container)))) {
        if (LayoutObject* layout_object = ToElement(node)->GetLayoutObject()) {
          Vector<FloatQuad> element_quads;
          layout_object->AbsoluteQuads(element_quads);
          owner_document_->AdjustFloatQuadsForScrollAndAbsoluteZoom(
              element_quads, *layout_object);

          quads.AppendVector(element_quads);
        }
      }
    } else if (node->IsTextNode()) {
      if (LayoutText* layout_text = ToText(node)->GetLayoutObject()) {
        unsigned start_offset = (node == start_container) ? start_.Offset() : 0;
        unsigned end_offset = (node == end_container)
                                  ? end_.Offset()
                                  : std::numeric_limits<unsigned>::max();

        Vector<FloatQuad> text_quads;
        layout_text->AbsoluteQuadsForRange(text_quads, start_offset,
                                           end_offset);
        owner_document_->AdjustFloatQuadsForScrollAndAbsoluteZoom(text_quads,
                                                                  *layout_text);

        quads.AppendVector(text_quads);
      }
    }
  }
}

FloatRect Range::BoundingRect() const {
  owner_document_->UpdateStyleAndLayoutIgnorePendingStylesheets();

  Vector<FloatQuad> quads;
  GetBorderAndTextQuads(quads);

  FloatRect result;
  for (const FloatQuad& quad : quads)
    result.Unite(quad.BoundingBox());  // Skips empty rects.

  // If all rects are empty, return the first rect.
  if (result.IsEmpty() && !quads.IsEmpty())
    return quads.front().BoundingBox();

  return result;
}

void Range::UpdateSelectionIfAddedToSelection() {
  if (!OwnerDocument().GetFrame())
    return;
  FrameSelection& selection = OwnerDocument().GetFrame()->Selection();
  if (this != selection.DocumentCachedRange())
    return;
  DCHECK(startContainer()->isConnected());
  DCHECK(startContainer()->GetDocument() == OwnerDocument());
  DCHECK(endContainer()->isConnected());
  DCHECK(endContainer()->GetDocument() == OwnerDocument());
  EventDispatchForbiddenScope no_events;
  selection.SetSelection(SelectionInDOMTree::Builder()
                             .Collapse(StartPosition())
                             .Extend(EndPosition())
                             .Build(),
                         FrameSelection::kCloseTyping |
                             FrameSelection::kClearTypingStyle |
                             FrameSelection::kDoNotSetFocus);
  selection.CacheRangeOfDocument(this);
}

void Range::RemoveFromSelectionIfInDifferentRoot(Document& old_document) {
  if (!old_document.GetFrame())
    return;
  FrameSelection& selection = old_document.GetFrame()->Selection();
  if (this != selection.DocumentCachedRange())
    return;
  if (OwnerDocument() == old_document && startContainer()->isConnected() &&
      endContainer()->isConnected())
    return;
  selection.Clear();
  selection.ClearDocumentCachedRange();
}

DEFINE_TRACE(Range) {
  visitor->Trace(owner_document_);
  visitor->Trace(start_);
  visitor->Trace(end_);
}

}  // namespace blink

#ifndef NDEBUG

void showTree(const blink::Range* range) {
  if (range && range->BoundaryPointsValid()) {
    LOG(INFO) << "\n"
              << range->startContainer()
                     ->ToMarkedTreeString(range->startContainer(), "S",
                                          range->endContainer(), "E")
                     .Utf8()
                     .data()
              << "start offset: " << range->startOffset()
              << ", end offset: " << range->endOffset();
  } else {
    LOG(INFO) << "Cannot show tree if range is null, or if boundary points are "
                 "invalid.";
  }
}

#endif
