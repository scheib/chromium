/*
 * Copyright (C) 2004, 2005, 2006, 2007 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/editing/EditingUtilities.h"

#include "core/HTMLElementFactory.h"
#include "core/HTMLNames.h"
#include "core/InputTypeNames.h"
#include "core/clipboard/DataObject.h"
#include "core/dom/Document.h"
#include "core/dom/ElementTraversal.h"
#include "core/dom/NodeComputedStyle.h"
#include "core/dom/Range.h"
#include "core/dom/Text.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "core/editing/EditingStrategy.h"
#include "core/editing/Editor.h"
#include "core/editing/PlainTextRange.h"
#include "core/editing/PositionIterator.h"
#include "core/editing/VisiblePosition.h"
#include "core/editing/VisibleSelection.h"
#include "core/editing/VisibleUnits.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/editing/serializers/HTMLInterchange.h"
#include "core/editing/state_machines/BackspaceStateMachine.h"
#include "core/editing/state_machines/BackwardGraphemeBoundaryStateMachine.h"
#include "core/editing/state_machines/ForwardGraphemeBoundaryStateMachine.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/UseCounter.h"
#include "core/html/HTMLBRElement.h"
#include "core/html/HTMLDivElement.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/HTMLLIElement.h"
#include "core/html/HTMLParagraphElement.h"
#include "core/html/HTMLSpanElement.h"
#include "core/html/HTMLTableCellElement.h"
#include "core/html/HTMLUListElement.h"
#include "core/layout/LayoutObject.h"
#include "core/layout/LayoutTableCell.h"
#include "platform/clipboard/ClipboardMimeTypes.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/StdLibExtras.h"
#include "platform/wtf/text/StringBuilder.h"
#include "platform/wtf/text/Unicode.h"

namespace blink {

using namespace HTMLNames;

namespace {

std::ostream& operator<<(std::ostream& os, PositionMoveType type) {
  static const char* const kTexts[] = {"CodeUnit", "BackwardDeletion",
                                       "GraphemeCluster"};
  const auto& it = std::begin(kTexts) + static_cast<size_t>(type);
  DCHECK_GE(it, std::begin(kTexts)) << "Unknown PositionMoveType value";
  DCHECK_LT(it, std::end(kTexts)) << "Unknown PositionMoveType value";
  return os << *it;
}

InputEvent::EventCancelable InputTypeIsCancelable(
    InputEvent::InputType input_type) {
  using InputType = InputEvent::InputType;
  switch (input_type) {
    case InputType::kInsertText:
    case InputType::kInsertLineBreak:
    case InputType::kInsertParagraph:
    case InputType::kInsertCompositionText:
    case InputType::kInsertReplacementText:
    case InputType::kDeleteWordBackward:
    case InputType::kDeleteWordForward:
    case InputType::kDeleteSoftLineBackward:
    case InputType::kDeleteSoftLineForward:
    case InputType::kDeleteHardLineBackward:
    case InputType::kDeleteHardLineForward:
    case InputType::kDeleteContentBackward:
    case InputType::kDeleteContentForward:
      return InputEvent::EventCancelable::kNotCancelable;
    default:
      return InputEvent::EventCancelable::kIsCancelable;
  }
}

}  // namespace

bool NeedsLayoutTreeUpdate(const Node& node) {
  const Document& document = node.GetDocument();
  if (document.NeedsLayoutTreeUpdate())
    return true;
  // TODO(yosin): We should make |document::needsLayoutTreeUpdate()| to
  // check |LayoutView::needsLayout()|.
  return document.View() && document.View()->NeedsLayout();
}

template <typename PositionType>
static bool NeedsLayoutTreeUpdateAlgorithm(const PositionType& position) {
  const Node* node = position.AnchorNode();
  if (!node)
    return false;
  return NeedsLayoutTreeUpdate(*node);
}

bool NeedsLayoutTreeUpdate(const Position& position) {
  return NeedsLayoutTreeUpdateAlgorithm<Position>(position);
}

bool NeedsLayoutTreeUpdate(const PositionInFlatTree& position) {
  return NeedsLayoutTreeUpdateAlgorithm<PositionInFlatTree>(position);
}

// Atomic means that the node has no children, or has children which are ignored
// for the purposes of editing.
bool IsAtomicNode(const Node* node) {
  return node && (!node->hasChildren() || EditingIgnoresContent(*node));
}

template <typename Traversal>
static int ComparePositions(Node* container_a,
                            int offset_a,
                            Node* container_b,
                            int offset_b,
                            bool* disconnected) {
  DCHECK(container_a);
  DCHECK(container_b);

  if (disconnected)
    *disconnected = false;

  if (!container_a)
    return -1;
  if (!container_b)
    return 1;

  // see DOM2 traversal & range section 2.5

  // case 1: both points have the same container
  if (container_a == container_b) {
    if (offset_a == offset_b)
      return 0;  // A is equal to B
    if (offset_a < offset_b)
      return -1;  // A is before B
    return 1;     // A is after B
  }

  // case 2: node C (container B or an ancestor) is a child node of A
  Node* c = container_b;
  while (c && Traversal::Parent(*c) != container_a)
    c = Traversal::Parent(*c);
  if (c) {
    int offset_c = 0;
    Node* n = Traversal::FirstChild(*container_a);
    while (n != c && offset_c < offset_a) {
      offset_c++;
      n = Traversal::NextSibling(*n);
    }

    if (offset_a <= offset_c)
      return -1;  // A is before B
    return 1;     // A is after B
  }

  // case 3: node C (container A or an ancestor) is a child node of B
  c = container_a;
  while (c && Traversal::Parent(*c) != container_b)
    c = Traversal::Parent(*c);
  if (c) {
    int offset_c = 0;
    Node* n = Traversal::FirstChild(*container_b);
    while (n != c && offset_c < offset_b) {
      offset_c++;
      n = Traversal::NextSibling(*n);
    }

    if (offset_c < offset_b)
      return -1;  // A is before B
    return 1;     // A is after B
  }

  // case 4: containers A & B are siblings, or children of siblings
  // ### we need to do a traversal here instead
  Node* common_ancestor = Traversal::CommonAncestor(*container_a, *container_b);
  if (!common_ancestor) {
    if (disconnected)
      *disconnected = true;
    return 0;
  }
  Node* child_a = container_a;
  while (child_a && Traversal::Parent(*child_a) != common_ancestor)
    child_a = Traversal::Parent(*child_a);
  if (!child_a)
    child_a = common_ancestor;
  Node* child_b = container_b;
  while (child_b && Traversal::Parent(*child_b) != common_ancestor)
    child_b = Traversal::Parent(*child_b);
  if (!child_b)
    child_b = common_ancestor;

  if (child_a == child_b)
    return 0;  // A is equal to B

  Node* n = Traversal::FirstChild(*common_ancestor);
  while (n) {
    if (n == child_a)
      return -1;  // A is before B
    if (n == child_b)
      return 1;  // A is after B
    n = Traversal::NextSibling(*n);
  }

  // Should never reach this point.
  NOTREACHED();
  return 0;
}

int ComparePositionsInDOMTree(Node* container_a,
                              int offset_a,
                              Node* container_b,
                              int offset_b,
                              bool* disconnected) {
  return ComparePositions<NodeTraversal>(container_a, offset_a, container_b,
                                         offset_b, disconnected);
}

int ComparePositionsInFlatTree(Node* container_a,
                               int offset_a,
                               Node* container_b,
                               int offset_b,
                               bool* disconnected) {
  return ComparePositions<FlatTreeTraversal>(container_a, offset_a, container_b,
                                             offset_b, disconnected);
}

// Compare two positions, taking into account the possibility that one or both
// could be inside a shadow tree. Only works for non-null values.
int ComparePositions(const Position& a, const Position& b) {
  DCHECK(a.IsNotNull());
  DCHECK(b.IsNotNull());
  const TreeScope* common_scope = Position::CommonAncestorTreeScope(a, b);

  DCHECK(common_scope);
  if (!common_scope)
    return 0;

  Node* node_a = common_scope->AncestorInThisScope(a.ComputeContainerNode());
  DCHECK(node_a);
  bool has_descendent_a = node_a != a.ComputeContainerNode();
  int offset_a = has_descendent_a ? 0 : a.ComputeOffsetInContainerNode();

  Node* node_b = common_scope->AncestorInThisScope(b.ComputeContainerNode());
  DCHECK(node_b);
  bool has_descendent_b = node_b != b.ComputeContainerNode();
  int offset_b = has_descendent_b ? 0 : b.ComputeOffsetInContainerNode();

  int bias = 0;
  if (node_a == node_b) {
    if (has_descendent_a)
      bias = -1;
    else if (has_descendent_b)
      bias = 1;
  }

  int result = ComparePositionsInDOMTree(node_a, offset_a, node_b, offset_b);
  return result ? result : bias;
}

int ComparePositions(const PositionWithAffinity& a,
                     const PositionWithAffinity& b) {
  return ComparePositions(a.GetPosition(), b.GetPosition());
}

int ComparePositions(const VisiblePosition& a, const VisiblePosition& b) {
  return ComparePositions(a.DeepEquivalent(), b.DeepEquivalent());
}

bool IsNodeFullyContained(const EphemeralRange& range, Node& node) {
  if (range.IsNull())
    return false;

  if (!NodeTraversal::CommonAncestor(*range.StartPosition().AnchorNode(), node))
    return false;

  return range.StartPosition() <= Position::BeforeNode(&node) &&
         Position::AfterNode(&node) <= range.EndPosition();
}

// TODO(editing-dev): We should implement real version which refers
// "user-select" CSS property.
// TODO(editing-dev): We should make |SelectionAdjuster| to use this funciton
// instead of |isSelectionBondary()|.
bool IsUserSelectContain(const Node& node) {
  return isHTMLTextAreaElement(node) || isHTMLInputElement(node) ||
         isHTMLSelectElement(node);
}

enum EditableLevel { kEditable, kRichlyEditable };
static bool HasEditableLevel(const Node& node, EditableLevel editable_level) {
  DCHECK(node.GetDocument().IsActive());
  // TODO(editing-dev): We should have this check:
  // DCHECK_GE(node.document().lifecycle().state(),
  //           DocumentLifecycle::StyleClean);
  if (node.IsPseudoElement())
    return false;

  // Ideally we'd call DCHECK(!needsStyleRecalc()) here, but
  // ContainerNode::setFocus() calls setNeedsStyleRecalc(), so the assertion
  // would fire in the middle of Document::setFocusedNode().

  for (const Node& ancestor : NodeTraversal::InclusiveAncestorsOf(node)) {
    if ((ancestor.IsHTMLElement() || ancestor.IsDocumentNode()) &&
        ancestor.GetLayoutObject()) {
      switch (ancestor.GetLayoutObject()->Style()->UserModify()) {
        case EUserModify::kReadOnly:
          return false;
        case EUserModify::kReadWrite:
          return true;
        case EUserModify::kReadWritePlaintextOnly:
          return editable_level != kRichlyEditable;
      }
      NOTREACHED();
      return false;
    }
  }

  return false;
}

bool HasEditableStyle(const Node& node) {
  // TODO(editing-dev): We shouldn't check editable style in inactive documents.
  // We should hoist this check in the call stack, replace it by a DCHECK of
  // active document and ultimately cleanup the code paths with inactive
  // documents.  See crbug.com/667681
  if (!node.GetDocument().IsActive())
    return false;

  return HasEditableLevel(node, kEditable);
}

bool HasRichlyEditableStyle(const Node& node) {
  // TODO(editing-dev): We shouldn't check editable style in inactive documents.
  // We should hoist this check in the call stack, replace it by a DCHECK of
  // active document and ultimately cleanup the code paths with inactive
  // documents.  See crbug.com/667681
  if (!node.GetDocument().IsActive())
    return false;

  return HasEditableLevel(node, kRichlyEditable);
}

bool IsRootEditableElement(const Node& node) {
  return HasEditableStyle(node) && node.IsElementNode() &&
         (!node.parentNode() || !HasEditableStyle(*node.parentNode()) ||
          !node.parentNode()->IsElementNode() ||
          &node == node.GetDocument().body());
}

Element* RootEditableElement(const Node& node) {
  const Node* result = nullptr;
  for (const Node* n = &node; n && HasEditableStyle(*n); n = n->parentNode()) {
    if (n->IsElementNode())
      result = n;
    if (node.GetDocument().body() == n)
      break;
  }
  return ToElement(const_cast<Node*>(result));
}

ContainerNode* HighestEditableRoot(
    const Position& position,
    Element* (*root_editable_element_of)(const Position&),
    bool (*has_editable_style)(const Node&)) {
  if (position.IsNull())
    return 0;

  ContainerNode* highest_root = root_editable_element_of(position);
  if (!highest_root)
    return 0;

  if (isHTMLBodyElement(*highest_root))
    return highest_root;

  ContainerNode* node = highest_root->parentNode();
  while (node) {
    if (has_editable_style(*node))
      highest_root = node;
    if (isHTMLBodyElement(*node))
      break;
    node = node->parentNode();
  }

  return highest_root;
}

ContainerNode* HighestEditableRoot(const PositionInFlatTree& position) {
  return HighestEditableRoot(ToPositionInDOMTree(position));
}

bool IsEditablePosition(const Position& position) {
  Node* node = position.ParentAnchoredEquivalent().AnchorNode();
  if (!node)
    return false;
  DCHECK(node->GetDocument().IsActive());
  if (node->GetDocument().Lifecycle().GetState() >=
      DocumentLifecycle::kInStyleRecalc) {
    // TODO(yosin): Update the condition and DCHECK here given that
    // https://codereview.chromium.org/2665823002/ avoided this function from
    // being called during InStyleRecalc.
  } else {
    DCHECK(!NeedsLayoutTreeUpdate(position)) << position;
  }

  if (IsDisplayInsideTable(node))
    node = node->parentNode();

  if (node->IsDocumentNode())
    return false;
  return HasEditableStyle(*node);
}

bool IsEditablePosition(const PositionInFlatTree& p) {
  return IsEditablePosition(ToPositionInDOMTree(p));
}

bool IsRichlyEditablePosition(const Position& p) {
  Node* node = p.AnchorNode();
  if (!node)
    return false;

  if (IsDisplayInsideTable(node))
    node = node->parentNode();

  return HasRichlyEditableStyle(*node);
}

Element* RootEditableElementOf(const Position& p) {
  Node* node = p.ComputeContainerNode();
  if (!node)
    return 0;

  if (IsDisplayInsideTable(node))
    node = node->parentNode();

  return RootEditableElement(*node);
}

Element* RootEditableElementOf(const PositionInFlatTree& p) {
  return RootEditableElementOf(ToPositionInDOMTree(p));
}

// TODO(yosin) This does not handle [table, 0] correctly.
Element* RootEditableElementOf(const VisiblePosition& visible_position) {
  Node* anchor_node = visible_position.DeepEquivalent().AnchorNode();
  return anchor_node ? RootEditableElement(*anchor_node) : nullptr;
}

template <typename Strategy>
PositionTemplate<Strategy> NextCandidateAlgorithm(
    const PositionTemplate<Strategy>& position) {
  TRACE_EVENT0("input", "EditingUtility::nextCandidateAlgorithm");
  PositionIteratorAlgorithm<Strategy> p(position);

  p.Increment();
  while (!p.AtEnd()) {
    PositionTemplate<Strategy> candidate = p.ComputePosition();
    if (IsVisuallyEquivalentCandidate(candidate))
      return candidate;

    p.Increment();
  }

  return PositionTemplate<Strategy>();
}

Position NextCandidate(const Position& position) {
  return NextCandidateAlgorithm<EditingStrategy>(position);
}

PositionInFlatTree NextCandidate(const PositionInFlatTree& position) {
  return NextCandidateAlgorithm<EditingInFlatTreeStrategy>(position);
}

// |nextVisuallyDistinctCandidate| is similar to |nextCandidate| except
// for returning position which |downstream()| not equal to initial position's
// |downstream()|.
template <typename Strategy>
static PositionTemplate<Strategy> NextVisuallyDistinctCandidateAlgorithm(
    const PositionTemplate<Strategy>& position) {
  TRACE_EVENT0("input",
               "EditingUtility::nextVisuallyDistinctCandidateAlgorithm");
  if (position.IsNull())
    return PositionTemplate<Strategy>();

  PositionIteratorAlgorithm<Strategy> p(position);
  const PositionTemplate<Strategy> downstream_start =
      MostForwardCaretPosition(position);

  p.Increment();
  while (!p.AtEnd()) {
    PositionTemplate<Strategy> candidate = p.ComputePosition();
    if (IsVisuallyEquivalentCandidate(candidate) &&
        MostForwardCaretPosition(candidate) != downstream_start)
      return candidate;

    p.Increment();
  }

  return PositionTemplate<Strategy>();
}

Position NextVisuallyDistinctCandidate(const Position& position) {
  return NextVisuallyDistinctCandidateAlgorithm<EditingStrategy>(position);
}

PositionInFlatTree NextVisuallyDistinctCandidate(
    const PositionInFlatTree& position) {
  return NextVisuallyDistinctCandidateAlgorithm<EditingInFlatTreeStrategy>(
      position);
}

template <typename Strategy>
PositionTemplate<Strategy> PreviousCandidateAlgorithm(
    const PositionTemplate<Strategy>& position) {
  TRACE_EVENT0("input", "EditingUtility::previousCandidateAlgorithm");
  PositionIteratorAlgorithm<Strategy> p(position);

  p.Decrement();
  while (!p.AtStart()) {
    PositionTemplate<Strategy> candidate = p.ComputePosition();
    if (IsVisuallyEquivalentCandidate(candidate))
      return candidate;

    p.Decrement();
  }

  return PositionTemplate<Strategy>();
}

Position PreviousCandidate(const Position& position) {
  return PreviousCandidateAlgorithm<EditingStrategy>(position);
}

PositionInFlatTree PreviousCandidate(const PositionInFlatTree& position) {
  return PreviousCandidateAlgorithm<EditingInFlatTreeStrategy>(position);
}

// |previousVisuallyDistinctCandidate| is similar to |previousCandidate| except
// for returning position which |downstream()| not equal to initial position's
// |downstream()|.
template <typename Strategy>
PositionTemplate<Strategy> PreviousVisuallyDistinctCandidateAlgorithm(
    const PositionTemplate<Strategy>& position) {
  TRACE_EVENT0("input",
               "EditingUtility::previousVisuallyDistinctCandidateAlgorithm");
  if (position.IsNull())
    return PositionTemplate<Strategy>();

  PositionIteratorAlgorithm<Strategy> p(position);
  PositionTemplate<Strategy> downstream_start =
      MostForwardCaretPosition(position);

  p.Decrement();
  while (!p.AtStart()) {
    PositionTemplate<Strategy> candidate = p.ComputePosition();
    if (IsVisuallyEquivalentCandidate(candidate) &&
        MostForwardCaretPosition(candidate) != downstream_start)
      return candidate;

    p.Decrement();
  }

  return PositionTemplate<Strategy>();
}

Position PreviousVisuallyDistinctCandidate(const Position& position) {
  return PreviousVisuallyDistinctCandidateAlgorithm<EditingStrategy>(position);
}

PositionInFlatTree PreviousVisuallyDistinctCandidate(
    const PositionInFlatTree& position) {
  return PreviousVisuallyDistinctCandidateAlgorithm<EditingInFlatTreeStrategy>(
      position);
}

VisiblePosition FirstEditableVisiblePositionAfterPositionInRoot(
    const Position& position,
    ContainerNode& highest_root) {
  DCHECK(!NeedsLayoutTreeUpdate(position));
  return CreateVisiblePosition(
      FirstEditablePositionAfterPositionInRoot(position, highest_root));
}

VisiblePositionInFlatTree FirstEditableVisiblePositionAfterPositionInRoot(
    const PositionInFlatTree& position,
    ContainerNode& highest_root) {
  DCHECK(!NeedsLayoutTreeUpdate(position));
  return CreateVisiblePosition(
      FirstEditablePositionAfterPositionInRoot(position, highest_root));
}

template <typename Strategy>
PositionTemplate<Strategy> FirstEditablePositionAfterPositionInRootAlgorithm(
    const PositionTemplate<Strategy>& position,
    Node& highest_root) {
  DCHECK(!NeedsLayoutTreeUpdate(highest_root))
      << position << ' ' << highest_root;
  // position falls before highestRoot.
  if (position.CompareTo(PositionTemplate<Strategy>::FirstPositionInNode(
          &highest_root)) == -1 &&
      HasEditableStyle(highest_root))
    return PositionTemplate<Strategy>::FirstPositionInNode(&highest_root);

  PositionTemplate<Strategy> editable_position = position;

  if (position.AnchorNode()->GetTreeScope() != highest_root.GetTreeScope()) {
    Node* shadow_ancestor = highest_root.GetTreeScope().AncestorInThisScope(
        editable_position.AnchorNode());
    if (!shadow_ancestor)
      return PositionTemplate<Strategy>();

    editable_position = PositionTemplate<Strategy>::AfterNode(shadow_ancestor);
  }

  Node* non_editable_node = nullptr;
  while (editable_position.AnchorNode() &&
         !IsEditablePosition(editable_position) &&
         editable_position.AnchorNode()->IsDescendantOf(&highest_root)) {
    non_editable_node = editable_position.AnchorNode();
    editable_position = IsAtomicNode(editable_position.AnchorNode())
                            ? PositionTemplate<Strategy>::InParentAfterNode(
                                  *editable_position.AnchorNode())
                            : NextVisuallyDistinctCandidate(editable_position);
  }

  if (editable_position.AnchorNode() &&
      editable_position.AnchorNode() != &highest_root &&
      !editable_position.AnchorNode()->IsDescendantOf(&highest_root))
    return PositionTemplate<Strategy>();

  // If |editablePosition| has the non-editable child skipped, get the next
  // sibling position. If not, we can't get the next paragraph in
  // InsertListCommand::doApply's while loop. See http://crbug.com/571420
  if (non_editable_node &&
      non_editable_node->IsDescendantOf(editable_position.AnchorNode()))
    editable_position = NextVisuallyDistinctCandidate(editable_position);
  return editable_position;
}

Position FirstEditablePositionAfterPositionInRoot(const Position& position,
                                                  Node& highest_root) {
  return FirstEditablePositionAfterPositionInRootAlgorithm<EditingStrategy>(
      position, highest_root);
}

PositionInFlatTree FirstEditablePositionAfterPositionInRoot(
    const PositionInFlatTree& position,
    Node& highest_root) {
  return FirstEditablePositionAfterPositionInRootAlgorithm<
      EditingInFlatTreeStrategy>(position, highest_root);
}

VisiblePosition LastEditableVisiblePositionBeforePositionInRoot(
    const Position& position,
    ContainerNode& highest_root) {
  DCHECK(!NeedsLayoutTreeUpdate(position));
  return CreateVisiblePosition(
      LastEditablePositionBeforePositionInRoot(position, highest_root));
}

VisiblePositionInFlatTree LastEditableVisiblePositionBeforePositionInRoot(
    const PositionInFlatTree& position,
    ContainerNode& highest_root) {
  DCHECK(!NeedsLayoutTreeUpdate(position));
  return CreateVisiblePosition(
      LastEditablePositionBeforePositionInRoot(position, highest_root));
}

template <typename Strategy>
PositionTemplate<Strategy> LastEditablePositionBeforePositionInRootAlgorithm(
    const PositionTemplate<Strategy>& position,
    Node& highest_root) {
  DCHECK(!NeedsLayoutTreeUpdate(highest_root))
      << position << ' ' << highest_root;
  // When position falls after highestRoot, the result is easy to compute.
  if (position.CompareTo(
          PositionTemplate<Strategy>::LastPositionInNode(&highest_root)) == 1)
    return PositionTemplate<Strategy>::LastPositionInNode(&highest_root);

  PositionTemplate<Strategy> editable_position = position;

  if (position.AnchorNode()->GetTreeScope() != highest_root.GetTreeScope()) {
    Node* shadow_ancestor = highest_root.GetTreeScope().AncestorInThisScope(
        editable_position.AnchorNode());
    if (!shadow_ancestor)
      return PositionTemplate<Strategy>();

    editable_position = PositionTemplate<Strategy>::FirstPositionInOrBeforeNode(
        shadow_ancestor);
  }

  while (editable_position.AnchorNode() &&
         !IsEditablePosition(editable_position) &&
         editable_position.AnchorNode()->IsDescendantOf(&highest_root))
    editable_position =
        IsAtomicNode(editable_position.AnchorNode())
            ? PositionTemplate<Strategy>::InParentBeforeNode(
                  *editable_position.AnchorNode())
            : PreviousVisuallyDistinctCandidate(editable_position);

  if (editable_position.AnchorNode() &&
      editable_position.AnchorNode() != &highest_root &&
      !editable_position.AnchorNode()->IsDescendantOf(&highest_root))
    return PositionTemplate<Strategy>();
  return editable_position;
}

Position LastEditablePositionBeforePositionInRoot(const Position& position,
                                                  Node& highest_root) {
  return LastEditablePositionBeforePositionInRootAlgorithm<EditingStrategy>(
      position, highest_root);
}

PositionInFlatTree LastEditablePositionBeforePositionInRoot(
    const PositionInFlatTree& position,
    Node& highest_root) {
  return LastEditablePositionBeforePositionInRootAlgorithm<
      EditingInFlatTreeStrategy>(position, highest_root);
}

template <typename StateMachine>
int FindNextBoundaryOffset(const String& str, int current) {
  StateMachine machine;
  TextSegmentationMachineState state = TextSegmentationMachineState::kInvalid;

  for (int i = current - 1; i >= 0; --i) {
    state = machine.FeedPrecedingCodeUnit(str[i]);
    if (state != TextSegmentationMachineState::kNeedMoreCodeUnit)
      break;
  }
  if (current == 0 || state == TextSegmentationMachineState::kNeedMoreCodeUnit)
    state = machine.TellEndOfPrecedingText();
  if (state == TextSegmentationMachineState::kFinished)
    return current + machine.FinalizeAndGetBoundaryOffset();
  const int length = str.length();
  DCHECK_EQ(TextSegmentationMachineState::kNeedFollowingCodeUnit, state);
  for (int i = current; i < length; ++i) {
    state = machine.FeedFollowingCodeUnit(str[i]);
    if (state != TextSegmentationMachineState::kNeedMoreCodeUnit)
      break;
  }
  return current + machine.FinalizeAndGetBoundaryOffset();
}

int PreviousGraphemeBoundaryOf(const Node* node, int current) {
  // TODO(yosin): Need to support grapheme crossing |Node| boundary.
  DCHECK_GE(current, 0);
  if (current <= 1 || !node->IsTextNode())
    return current - 1;
  const String& text = ToText(node)->data();
  // TODO(yosin): Replace with DCHECK for out-of-range request.
  if (static_cast<unsigned>(current) > text.length())
    return current - 1;
  return FindNextBoundaryOffset<BackwardGraphemeBoundaryStateMachine>(text,
                                                                      current);
}

static int PreviousBackwardDeletionOffsetOf(const Node* node, int current) {
  DCHECK_GE(current, 0);
  if (current <= 1)
    return 0;
  if (!node->IsTextNode())
    return current - 1;

  const String& text = ToText(node)->data();
  DCHECK_LT(static_cast<unsigned>(current - 1), text.length());
  return FindNextBoundaryOffset<BackspaceStateMachine>(text, current);
}

int NextGraphemeBoundaryOf(const Node* node, int current) {
  // TODO(yosin): Need to support grapheme crossing |Node| boundary.
  if (!node->IsTextNode())
    return current + 1;
  const String& text = ToText(node)->data();
  const int length = text.length();
  DCHECK_LE(current, length);
  if (current >= length - 1)
    return current + 1;
  return FindNextBoundaryOffset<ForwardGraphemeBoundaryStateMachine>(text,
                                                                     current);
}

template <typename Strategy>
PositionTemplate<Strategy> PreviousPositionOfAlgorithm(
    const PositionTemplate<Strategy>& position,
    PositionMoveType move_type) {
  Node* const node = position.AnchorNode();
  if (!node)
    return position;

  const int offset = position.ComputeEditingOffset();

  if (offset > 0) {
    if (EditingIgnoresContent(*node))
      return PositionTemplate<Strategy>::BeforeNode(node);
    if (Node* child = Strategy::ChildAt(*node, offset - 1))
      return PositionTemplate<Strategy>::LastPositionInOrAfterNode(child);

    // There are two reasons child might be 0:
    //   1) The node is node like a text node that is not an element, and
    //      therefore has no children. Going backward one character at a
    //      time is correct.
    //   2) The old offset was a bogus offset like (<br>, 1), and there is
    //      no child. Going from 1 to 0 is correct.
    switch (move_type) {
      case PositionMoveType::kCodeUnit:
        return PositionTemplate<Strategy>(node, offset - 1);
      case PositionMoveType::kBackwardDeletion:
        return PositionTemplate<Strategy>(
            node, PreviousBackwardDeletionOffsetOf(node, offset));
      case PositionMoveType::kGraphemeCluster:
        return PositionTemplate<Strategy>(
            node, PreviousGraphemeBoundaryOf(node, offset));
      default:
        NOTREACHED() << "Unhandled moveType: " << move_type;
    }
  }

  if (ContainerNode* parent = Strategy::Parent(*node)) {
    if (EditingIgnoresContent(*parent))
      return PositionTemplate<Strategy>::BeforeNode(parent);
    // TODO(yosin) We should use |Strategy::index(Node&)| instead of
    // |Node::nodeIndex()|.
    return PositionTemplate<Strategy>(parent, node->NodeIndex());
  }
  return position;
}

Position PreviousPositionOf(const Position& position,
                            PositionMoveType move_type) {
  return PreviousPositionOfAlgorithm<EditingStrategy>(position, move_type);
}

PositionInFlatTree PreviousPositionOf(const PositionInFlatTree& position,
                                      PositionMoveType move_type) {
  return PreviousPositionOfAlgorithm<EditingInFlatTreeStrategy>(position,
                                                                move_type);
}

template <typename Strategy>
PositionTemplate<Strategy> NextPositionOfAlgorithm(
    const PositionTemplate<Strategy>& position,
    PositionMoveType move_type) {
  // TODO(yosin): We should have printer for PositionMoveType.
  DCHECK(move_type != PositionMoveType::kBackwardDeletion);

  Node* node = position.AnchorNode();
  if (!node)
    return position;

  const int offset = position.ComputeEditingOffset();

  if (Node* child = Strategy::ChildAt(*node, offset))
    return PositionTemplate<Strategy>::FirstPositionInOrBeforeNode(child);

  // TODO(yosin) We should use |Strategy::lastOffsetForEditing()| instead of
  // DOM tree version.
  if (!Strategy::HasChildren(*node) &&
      offset < EditingStrategy::LastOffsetForEditing(node)) {
    // There are two reasons child might be 0:
    //   1) The node is node like a text node that is not an element, and
    //      therefore has no children. Going forward one character at a time
    //      is correct.
    //   2) The new offset is a bogus offset like (<br>, 1), and there is no
    //      child. Going from 0 to 1 is correct.
    switch (move_type) {
      case PositionMoveType::kCodeUnit:
        return PositionTemplate<Strategy>::EditingPositionOf(node, offset + 1);
      case PositionMoveType::kBackwardDeletion:
        NOTREACHED() << "BackwardDeletion is only available for prevPositionOf "
                     << "functions.";
        return PositionTemplate<Strategy>::EditingPositionOf(node, offset + 1);
      case PositionMoveType::kGraphemeCluster:
        return PositionTemplate<Strategy>::EditingPositionOf(
            node, NextGraphemeBoundaryOf(node, offset));
      default:
        NOTREACHED() << "Unhandled moveType: " << move_type;
    }
  }

  if (ContainerNode* parent = Strategy::Parent(*node))
    return PositionTemplate<Strategy>::EditingPositionOf(
        parent, Strategy::Index(*node) + 1);
  return position;
}

Position NextPositionOf(const Position& position, PositionMoveType move_type) {
  return NextPositionOfAlgorithm<EditingStrategy>(position, move_type);
}

PositionInFlatTree NextPositionOf(const PositionInFlatTree& position,
                                  PositionMoveType move_type) {
  return NextPositionOfAlgorithm<EditingInFlatTreeStrategy>(position,
                                                            move_type);
}

bool IsEnclosingBlock(const Node* node) {
  return node && node->GetLayoutObject() &&
         !node->GetLayoutObject()->IsInline() &&
         !node->GetLayoutObject()->IsRubyText();
}

bool IsInline(const Node* node) {
  if (!node)
    return false;

  const ComputedStyle* style = node->GetComputedStyle();
  return style && style->Display() == EDisplay::kInline;
}

// TODO(yosin) Deploy this in all of the places where |enclosingBlockFlow()| and
// |enclosingBlockFlowOrTableElement()| are used.
// TODO(yosin) Callers of |Node| version of |enclosingBlock()| should use
// |Position| version The enclosing block of [table, x] for example, should be
// the block that contains the table and not the table, and this function should
// be the only one responsible for knowing about these kinds of special cases.
Element* EnclosingBlock(Node* node, EditingBoundaryCrossingRule rule) {
  return EnclosingBlock(FirstPositionInOrBeforeNode(node), rule);
}

template <typename Strategy>
Element* EnclosingBlockAlgorithm(const PositionTemplate<Strategy>& position,
                                 EditingBoundaryCrossingRule rule) {
  Node* enclosing_node = EnclosingNodeOfType(position, IsEnclosingBlock, rule);
  return enclosing_node && enclosing_node->IsElementNode()
             ? ToElement(enclosing_node)
             : nullptr;
}

Element* EnclosingBlock(const Position& position,
                        EditingBoundaryCrossingRule rule) {
  return EnclosingBlockAlgorithm<EditingStrategy>(position, rule);
}

Element* EnclosingBlock(const PositionInFlatTree& position,
                        EditingBoundaryCrossingRule rule) {
  return EnclosingBlockAlgorithm<EditingInFlatTreeStrategy>(position, rule);
}

Element* EnclosingBlockFlowElement(const Node& node) {
  if (IsBlockFlowElement(node))
    return const_cast<Element*>(&ToElement(node));

  for (Node& runner : NodeTraversal::AncestorsOf(node)) {
    if (IsBlockFlowElement(runner) || isHTMLBodyElement(runner))
      return ToElement(&runner);
  }
  return nullptr;
}

EUserSelect UsedValueOfUserSelect(const Node& node) {
  if (node.IsHTMLElement() && ToHTMLElement(node).IsTextControl())
    return EUserSelect::kText;
  if (!node.GetLayoutObject())
    return EUserSelect::kNone;

  const ComputedStyle* style = node.GetLayoutObject()->Style();
  if (style->UserModify() != EUserModify::kReadOnly)
    return EUserSelect::kText;

  return style->UserSelect();
}

template <typename Strategy>
TextDirection DirectionOfEnclosingBlockAlgorithm(
    const PositionTemplate<Strategy>& position) {
  Element* enclosing_block_element =
      EnclosingBlock(PositionTemplate<Strategy>::FirstPositionInOrBeforeNode(
                         position.ComputeContainerNode()),
                     kCannotCrossEditingBoundary);
  if (!enclosing_block_element)
    return TextDirection::kLtr;
  LayoutObject* layout_object = enclosing_block_element->GetLayoutObject();
  return layout_object ? layout_object->Style()->Direction()
                       : TextDirection::kLtr;
}

TextDirection DirectionOfEnclosingBlock(const Position& position) {
  return DirectionOfEnclosingBlockAlgorithm<EditingStrategy>(position);
}

TextDirection DirectionOfEnclosingBlock(const PositionInFlatTree& position) {
  return DirectionOfEnclosingBlockAlgorithm<EditingInFlatTreeStrategy>(
      position);
}

TextDirection PrimaryDirectionOf(const Node& node) {
  TextDirection primary_direction = TextDirection::kLtr;
  for (const LayoutObject* r = node.GetLayoutObject(); r; r = r->Parent()) {
    if (r->IsLayoutBlockFlow()) {
      primary_direction = r->Style()->Direction();
      break;
    }
  }

  return primary_direction;
}

String StringWithRebalancedWhitespace(const String& string,
                                      bool start_is_start_of_paragraph,
                                      bool should_emit_nbs_pbefore_end) {
  unsigned length = string.length();

  StringBuilder rebalanced_string;
  rebalanced_string.ReserveCapacity(length);

  bool previous_character_was_space = false;
  for (size_t i = 0; i < length; i++) {
    UChar c = string[i];
    if (!IsWhitespace(c)) {
      rebalanced_string.Append(c);
      previous_character_was_space = false;
      continue;
    }

    if (previous_character_was_space || (!i && start_is_start_of_paragraph) ||
        (i + 1 == length && should_emit_nbs_pbefore_end)) {
      rebalanced_string.Append(kNoBreakSpaceCharacter);
      previous_character_was_space = false;
    } else {
      rebalanced_string.Append(' ');
      previous_character_was_space = true;
    }
  }

  DCHECK_EQ(rebalanced_string.length(), length);

  return rebalanced_string.ToString();
}

bool IsTableStructureNode(const Node* node) {
  LayoutObject* layout_object = node->GetLayoutObject();
  return (layout_object &&
          (layout_object->IsTableCell() || layout_object->IsTableRow() ||
           layout_object->IsTableSection() ||
           layout_object->IsLayoutTableCol()));
}

const String& NonBreakingSpaceString() {
  DEFINE_STATIC_LOCAL(String, non_breaking_space_string,
                      (&kNoBreakSpaceCharacter, 1));
  return non_breaking_space_string;
}

// FIXME: need to dump this
static bool IsSpecialHTMLElement(const Node& n) {
  if (!n.IsHTMLElement())
    return false;

  if (n.IsLink())
    return true;

  LayoutObject* layout_object = n.GetLayoutObject();
  if (!layout_object)
    return false;

  if (layout_object->Style()->Display() == EDisplay::kTable ||
      layout_object->Style()->Display() == EDisplay::kInlineTable)
    return true;

  if (layout_object->Style()->IsFloating())
    return true;

  return false;
}

static HTMLElement* FirstInSpecialElement(const Position& pos) {
  DCHECK(!NeedsLayoutTreeUpdate(pos));
  Element* element = RootEditableElement(*pos.ComputeContainerNode());
  for (Node& runner : NodeTraversal::InclusiveAncestorsOf(*pos.AnchorNode())) {
    if (RootEditableElement(runner) != element)
      break;
    if (IsSpecialHTMLElement(runner)) {
      HTMLElement* special_element = ToHTMLElement(&runner);
      VisiblePosition v_pos = CreateVisiblePosition(pos);
      VisiblePosition first_in_element =
          CreateVisiblePosition(FirstPositionInOrBeforeNode(special_element));
      if (IsDisplayInsideTable(special_element) &&
          v_pos.DeepEquivalent() ==
              NextPositionOf(first_in_element).DeepEquivalent())
        return special_element;
      if (v_pos.DeepEquivalent() == first_in_element.DeepEquivalent())
        return special_element;
    }
  }
  return 0;
}

static HTMLElement* LastInSpecialElement(const Position& pos) {
  DCHECK(!NeedsLayoutTreeUpdate(pos));
  Element* element = RootEditableElement(*pos.ComputeContainerNode());
  for (Node& runner : NodeTraversal::InclusiveAncestorsOf(*pos.AnchorNode())) {
    if (RootEditableElement(runner) != element)
      break;
    if (IsSpecialHTMLElement(runner)) {
      HTMLElement* special_element = ToHTMLElement(&runner);
      VisiblePosition v_pos = CreateVisiblePosition(pos);
      VisiblePosition last_in_element =
          CreateVisiblePosition(LastPositionInOrAfterNode(special_element));
      if (IsDisplayInsideTable(special_element) &&
          v_pos.DeepEquivalent() ==
              PreviousPositionOf(last_in_element).DeepEquivalent())
        return special_element;
      if (v_pos.DeepEquivalent() == last_in_element.DeepEquivalent())
        return special_element;
    }
  }
  return 0;
}

Position PositionBeforeContainingSpecialElement(
    const Position& pos,
    HTMLElement** containing_special_element) {
  DCHECK(!NeedsLayoutTreeUpdate(pos));
  HTMLElement* n = FirstInSpecialElement(pos);
  if (!n)
    return pos;
  Position result = Position::InParentBeforeNode(*n);
  if (result.IsNull() || RootEditableElement(*result.AnchorNode()) !=
                             RootEditableElement(*pos.AnchorNode()))
    return pos;
  if (containing_special_element)
    *containing_special_element = n;
  return result;
}

Position PositionAfterContainingSpecialElement(
    const Position& pos,
    HTMLElement** containing_special_element) {
  DCHECK(!NeedsLayoutTreeUpdate(pos));
  HTMLElement* n = LastInSpecialElement(pos);
  if (!n)
    return pos;
  Position result = Position::InParentAfterNode(*n);
  if (result.IsNull() || RootEditableElement(*result.AnchorNode()) !=
                             RootEditableElement(*pos.AnchorNode()))
    return pos;
  if (containing_special_element)
    *containing_special_element = n;
  return result;
}

template <typename Strategy>
static Element* TableElementJustBeforeAlgorithm(
    const VisiblePositionTemplate<Strategy>& visible_position) {
  const PositionTemplate<Strategy> upstream(
      MostBackwardCaretPosition(visible_position.DeepEquivalent()));
  if (IsDisplayInsideTable(upstream.AnchorNode()) &&
      upstream.AtLastEditingPositionForNode())
    return ToElement(upstream.AnchorNode());

  return nullptr;
}

Element* TableElementJustBefore(const VisiblePosition& visible_position) {
  return TableElementJustBeforeAlgorithm<EditingStrategy>(visible_position);
}

Element* TableElementJustBefore(
    const VisiblePositionInFlatTree& visible_position) {
  return TableElementJustBeforeAlgorithm<EditingInFlatTreeStrategy>(
      visible_position);
}

Element* TableElementJustAfter(const VisiblePosition& visible_position) {
  Position downstream(
      MostForwardCaretPosition(visible_position.DeepEquivalent()));
  if (IsDisplayInsideTable(downstream.AnchorNode()) &&
      downstream.AtFirstEditingPositionForNode())
    return ToElement(downstream.AnchorNode());

  return 0;
}

static Node* PreviousNodeConsideringAtomicNodes(const Node& start) {
  if (start.previousSibling()) {
    Node* node = start.previousSibling();
    while (!IsAtomicNode(node) && node->lastChild())
      node = node->lastChild();
    return node;
  }
  return start.parentNode();
}

static Node* NextNodeConsideringAtomicNodes(const Node& start) {
  if (!IsAtomicNode(&start) && start.hasChildren())
    return start.firstChild();
  if (start.nextSibling())
    return start.nextSibling();
  const Node* node = &start;
  while (node && !node->nextSibling())
    node = node->parentNode();
  if (node)
    return node->nextSibling();
  return nullptr;
}

Node* PreviousAtomicLeafNode(const Node& start) {
  Node* node = PreviousNodeConsideringAtomicNodes(start);
  while (node) {
    if (IsAtomicNode(node))
      return node;
    node = PreviousNodeConsideringAtomicNodes(*node);
  }
  return nullptr;
}

Node* NextAtomicLeafNode(const Node& start) {
  Node* node = NextNodeConsideringAtomicNodes(start);
  while (node) {
    if (IsAtomicNode(node))
      return node;
    node = NextNodeConsideringAtomicNodes(*node);
  }
  return nullptr;
}

// Returns the visible position at the beginning of a node
VisiblePosition VisiblePositionBeforeNode(Node& node) {
  DCHECK(!NeedsLayoutTreeUpdate(node));
  if (node.hasChildren())
    return CreateVisiblePosition(FirstPositionInOrBeforeNode(&node));
  DCHECK(node.parentNode()) << node;
  DCHECK(!node.parentNode()->IsShadowRoot()) << node.parentNode();
  return VisiblePosition::InParentBeforeNode(node);
}

// Returns the visible position at the ending of a node
VisiblePosition VisiblePositionAfterNode(Node& node) {
  DCHECK(!NeedsLayoutTreeUpdate(node));
  if (node.hasChildren())
    return CreateVisiblePosition(LastPositionInOrAfterNode(&node));
  DCHECK(node.parentNode()) << node.parentNode();
  DCHECK(!node.parentNode()->IsShadowRoot()) << node.parentNode();
  return VisiblePosition::InParentAfterNode(node);
}

bool IsHTMLListElement(Node* n) {
  return (n && (isHTMLUListElement(*n) || isHTMLOListElement(*n) ||
                isHTMLDListElement(*n)));
}

bool IsListItem(const Node* n) {
  return n && n->GetLayoutObject() && n->GetLayoutObject()->IsListItem();
}

bool IsPresentationalHTMLElement(const Node* node) {
  if (!node->IsHTMLElement())
    return false;

  const HTMLElement& element = ToHTMLElement(*node);
  return element.HasTagName(uTag) || element.HasTagName(sTag) ||
         element.HasTagName(strikeTag) || element.HasTagName(iTag) ||
         element.HasTagName(emTag) || element.HasTagName(bTag) ||
         element.HasTagName(strongTag);
}

Element* AssociatedElementOf(const Position& position) {
  Node* node = position.AnchorNode();
  if (!node || node->IsElementNode())
    return ToElement(node);
  ContainerNode* parent = NodeTraversal::Parent(*node);
  return parent && parent->IsElementNode() ? ToElement(parent) : nullptr;
}

Element* EnclosingElementWithTag(const Position& p,
                                 const QualifiedName& tag_name) {
  if (p.IsNull())
    return 0;

  ContainerNode* root = HighestEditableRoot(p);
  Element* ancestor = p.AnchorNode()->IsElementNode()
                          ? ToElement(p.AnchorNode())
                          : p.AnchorNode()->parentElement();
  for (; ancestor; ancestor = ancestor->parentElement()) {
    if (root && !HasEditableStyle(*ancestor))
      continue;
    if (ancestor->HasTagName(tag_name))
      return ancestor;
    if (ancestor == root)
      return 0;
  }

  return 0;
}

template <typename Strategy>
static Node* EnclosingNodeOfTypeAlgorithm(const PositionTemplate<Strategy>& p,
                                          bool (*node_is_of_type)(const Node*),
                                          EditingBoundaryCrossingRule rule) {
  // TODO(yosin) support CanSkipCrossEditingBoundary
  DCHECK(rule == kCanCrossEditingBoundary ||
         rule == kCannotCrossEditingBoundary)
      << rule;
  if (p.IsNull())
    return nullptr;

  ContainerNode* const root =
      rule == kCannotCrossEditingBoundary ? HighestEditableRoot(p) : nullptr;
  for (Node* n = p.AnchorNode(); n; n = Strategy::Parent(*n)) {
    // Don't return a non-editable node if the input position was editable,
    // since the callers from editing will no doubt want to perform editing
    // inside the returned node.
    if (root && !HasEditableStyle(*n))
      continue;
    if (node_is_of_type(n))
      return n;
    if (n == root)
      return nullptr;
  }

  return nullptr;
}

Node* EnclosingNodeOfType(const Position& p,
                          bool (*node_is_of_type)(const Node*),
                          EditingBoundaryCrossingRule rule) {
  return EnclosingNodeOfTypeAlgorithm<EditingStrategy>(p, node_is_of_type,
                                                       rule);
}

Node* EnclosingNodeOfType(const PositionInFlatTree& p,
                          bool (*node_is_of_type)(const Node*),
                          EditingBoundaryCrossingRule rule) {
  return EnclosingNodeOfTypeAlgorithm<EditingInFlatTreeStrategy>(
      p, node_is_of_type, rule);
}

Node* HighestEnclosingNodeOfType(const Position& p,
                                 bool (*node_is_of_type)(const Node*),
                                 EditingBoundaryCrossingRule rule,
                                 Node* stay_within) {
  Node* highest = nullptr;
  ContainerNode* root =
      rule == kCannotCrossEditingBoundary ? HighestEditableRoot(p) : nullptr;
  for (Node* n = p.ComputeContainerNode(); n && n != stay_within;
       n = n->parentNode()) {
    if (root && !HasEditableStyle(*n))
      continue;
    if (node_is_of_type(n))
      highest = n;
    if (n == root)
      break;
  }

  return highest;
}

static bool HasARenderedDescendant(Node* node, Node* excluded_node) {
  for (Node* n = node->firstChild(); n;) {
    if (n == excluded_node) {
      n = NodeTraversal::NextSkippingChildren(*n, node);
      continue;
    }
    if (n->GetLayoutObject())
      return true;
    n = NodeTraversal::Next(*n, node);
  }
  return false;
}

Node* HighestNodeToRemoveInPruning(Node* node, Node* exclude_node) {
  Node* previous_node = nullptr;
  Element* element = node ? RootEditableElement(*node) : nullptr;
  for (; node; node = node->parentNode()) {
    if (LayoutObject* layout_object = node->GetLayoutObject()) {
      if (!layout_object->CanHaveChildren() ||
          HasARenderedDescendant(node, previous_node) || element == node ||
          exclude_node == node)
        return previous_node;
    }
    previous_node = node;
  }
  return 0;
}

Element* EnclosingTableCell(const Position& p) {
  return ToElement(EnclosingNodeOfType(p, IsTableCell));
}

Element* EnclosingAnchorElement(const Position& p) {
  if (p.IsNull())
    return 0;

  for (Element* ancestor =
           ElementTraversal::FirstAncestorOrSelf(*p.AnchorNode());
       ancestor; ancestor = ElementTraversal::FirstAncestor(*ancestor)) {
    if (ancestor->IsLink())
      return ancestor;
  }
  return 0;
}

HTMLElement* EnclosingList(Node* node) {
  if (!node)
    return 0;

  ContainerNode* root = HighestEditableRoot(FirstPositionInOrBeforeNode(node));

  for (Node& runner : NodeTraversal::AncestorsOf(*node)) {
    if (isHTMLUListElement(runner) || isHTMLOListElement(runner))
      return ToHTMLElement(&runner);
    if (runner == root)
      return 0;
  }

  return 0;
}

Node* EnclosingListChild(Node* node) {
  if (!node)
    return 0;
  // Check for a list item element, or for a node whose parent is a list
  // element. Such a node will appear visually as a list item (but without a
  // list marker)
  ContainerNode* root = HighestEditableRoot(FirstPositionInOrBeforeNode(node));

  // FIXME: This function is inappropriately named if it starts with node
  // instead of node->parentNode()
  for (Node* n = node; n && n->parentNode(); n = n->parentNode()) {
    if (isHTMLLIElement(*n) ||
        (IsHTMLListElement(n->parentNode()) && n != root))
      return n;
    if (n == root || IsTableCell(n))
      return 0;
  }

  return 0;
}

// FIXME: This method should not need to call
// isStartOfParagraph/isEndOfParagraph
Node* EnclosingEmptyListItem(const VisiblePosition& visible_pos) {
  DCHECK(visible_pos.IsValid());

  // Check that position is on a line by itself inside a list item
  Node* list_child_node =
      EnclosingListChild(visible_pos.DeepEquivalent().AnchorNode());
  if (!list_child_node || !IsStartOfParagraph(visible_pos) ||
      !IsEndOfParagraph(visible_pos))
    return 0;

  VisiblePosition first_in_list_child =
      CreateVisiblePosition(FirstPositionInOrBeforeNode(list_child_node));
  VisiblePosition last_in_list_child =
      CreateVisiblePosition(LastPositionInOrAfterNode(list_child_node));

  if (first_in_list_child.DeepEquivalent() != visible_pos.DeepEquivalent() ||
      last_in_list_child.DeepEquivalent() != visible_pos.DeepEquivalent())
    return 0;

  return list_child_node;
}

HTMLElement* OutermostEnclosingList(Node* node, HTMLElement* root_list) {
  HTMLElement* list = EnclosingList(node);
  if (!list)
    return 0;

  while (HTMLElement* next_list = EnclosingList(list)) {
    if (next_list == root_list)
      break;
    list = next_list;
  }

  return list;
}

// Determines whether two positions are visibly next to each other (first then
// second) while ignoring whitespaces and unrendered nodes
static bool IsVisiblyAdjacent(const Position& first, const Position& second) {
  return CreateVisiblePosition(first).DeepEquivalent() ==
         CreateVisiblePosition(MostBackwardCaretPosition(second))
             .DeepEquivalent();
}

bool CanMergeLists(Element* first_list, Element* second_list) {
  if (!first_list || !second_list || !first_list->IsHTMLElement() ||
      !second_list->IsHTMLElement())
    return false;

  DCHECK(!NeedsLayoutTreeUpdate(*first_list));
  DCHECK(!NeedsLayoutTreeUpdate(*second_list));
  return first_list->HasTagName(
             second_list
                 ->TagQName())  // make sure the list types match (ol vs. ul)
         && HasEditableStyle(*first_list) &&
         HasEditableStyle(*second_list)  // both lists are editable
         && RootEditableElement(*first_list) ==
                RootEditableElement(
                    *second_list)  // don't cross editing boundaries
         && IsVisiblyAdjacent(Position::InParentAfterNode(*first_list),
                              Position::InParentBeforeNode(*second_list));
  // Make sure there is no visible content between this li and the previous list
}

bool IsDisplayInsideTable(const Node* node) {
  return node && node->GetLayoutObject() && isHTMLTableElement(node);
}

bool IsTableCell(const Node* node) {
  DCHECK(node);
  LayoutObject* r = node->GetLayoutObject();
  return r ? r->IsTableCell() : IsHTMLTableCellElement(*node);
}

bool IsEmptyTableCell(const Node* node) {
  // Returns true IFF the passed in node is one of:
  //   .) a table cell with no children,
  //   .) a table cell with a single BR child, and which has no other child
  //      layoutObject, including :before and :after layoutObject
  //   .) the BR child of such a table cell

  // Find rendered node
  while (node && !node->GetLayoutObject())
    node = node->parentNode();
  if (!node)
    return false;

  // Make sure the rendered node is a table cell or <br>.
  // If it's a <br>, then the parent node has to be a table cell.
  LayoutObject* layout_object = node->GetLayoutObject();
  if (layout_object->IsBR()) {
    layout_object = layout_object->Parent();
    if (!layout_object)
      return false;
  }
  if (!layout_object->IsTableCell())
    return false;

  // Check that the table cell contains no child layoutObjects except for
  // perhaps a single <br>.
  LayoutObject* child_layout_object = layout_object->SlowFirstChild();
  if (!child_layout_object)
    return true;
  if (!child_layout_object->IsBR())
    return false;
  return !child_layout_object->NextSibling();
}

HTMLElement* CreateDefaultParagraphElement(Document& document) {
  switch (document.GetFrame()->GetEditor().DefaultParagraphSeparator()) {
    case kEditorParagraphSeparatorIsDiv:
      return HTMLDivElement::Create(document);
    case kEditorParagraphSeparatorIsP:
      return HTMLParagraphElement::Create(document);
  }

  NOTREACHED();
  return nullptr;
}

HTMLElement* CreateHTMLElement(Document& document, const QualifiedName& name) {
  return HTMLElementFactory::createHTMLElement(name.LocalName(), document,
                                               kCreatedByCloneNode);
}

bool IsTabHTMLSpanElement(const Node* node) {
  if (!isHTMLSpanElement(node) || !node->firstChild())
    return false;
  if (node->firstChild()->IsCharacterDataNode() &&
      ToCharacterData(node->firstChild())->data().Contains('\t'))
    return true;
  return false;
}

bool IsTabHTMLSpanElementTextNode(const Node* node) {
  return node && node->IsTextNode() && node->parentNode() &&
         IsTabHTMLSpanElement(node->parentNode());
}

HTMLSpanElement* TabSpanElement(const Node* node) {
  return IsTabHTMLSpanElementTextNode(node)
             ? toHTMLSpanElement(node->parentNode())
             : 0;
}

static HTMLSpanElement* CreateTabSpanElement(Document& document,
                                             Text* tab_text_node) {
  // Make the span to hold the tab.
  HTMLSpanElement* span_element = HTMLSpanElement::Create(document);
  span_element->setAttribute(styleAttr, "white-space:pre");

  // Add tab text to that span.
  if (!tab_text_node)
    tab_text_node = document.CreateEditingTextNode("\t");

  span_element->AppendChild(tab_text_node);

  return span_element;
}

HTMLSpanElement* CreateTabSpanElement(Document& document,
                                      const String& tab_text) {
  return CreateTabSpanElement(document, document.createTextNode(tab_text));
}

HTMLSpanElement* CreateTabSpanElement(Document& document) {
  return CreateTabSpanElement(document, nullptr);
}

bool IsNodeRendered(const Node& node) {
  LayoutObject* layout_object = node.GetLayoutObject();
  if (!layout_object)
    return false;

  return layout_object->Style()->Visibility() == EVisibility::kVisible;
}

// return first preceding DOM position rendered at a different location, or
// "this"
static Position PreviousCharacterPosition(const Position& position,
                                          TextAffinity affinity) {
  DCHECK(!NeedsLayoutTreeUpdate(position));
  if (position.IsNull())
    return Position();

  Element* from_root_editable_element =
      RootEditableElement(*position.AnchorNode());

  bool at_start_of_line =
      IsStartOfLine(CreateVisiblePosition(position, affinity));
  bool rendered = IsVisuallyEquivalentCandidate(position);

  Position current_pos = position;
  while (!current_pos.AtStartOfTree()) {
    // TODO(yosin) When we use |previousCharacterPosition()| other than
    // finding leading whitespace, we should use |Character| instead of
    // |CodePoint|.
    current_pos = PreviousPositionOf(current_pos, PositionMoveType::kCodeUnit);

    if (RootEditableElement(*current_pos.AnchorNode()) !=
        from_root_editable_element)
      return position;

    if (at_start_of_line || !rendered) {
      if (IsVisuallyEquivalentCandidate(current_pos))
        return current_pos;
    } else if (RendersInDifferentPosition(position, current_pos)) {
      return current_pos;
    }
  }

  return position;
}

// This assumes that it starts in editable content.
Position LeadingWhitespacePosition(const Position& position,
                                   TextAffinity affinity,
                                   WhitespacePositionOption option) {
  DCHECK(!NeedsLayoutTreeUpdate(position));
  DCHECK(IsEditablePosition(position)) << position;
  if (position.IsNull())
    return Position();

  if (isHTMLBRElement(*MostBackwardCaretPosition(position).AnchorNode()))
    return Position();

  const Position& prev = PreviousCharacterPosition(position, affinity);
  if (prev == position)
    return Position();
  const Node* const anchor_node = prev.AnchorNode();
  if (!anchor_node || !anchor_node->IsTextNode())
    return Position();
  if (EnclosingBlockFlowElement(*anchor_node) !=
      EnclosingBlockFlowElement(*position.AnchorNode()))
    return Position();
  if (option == kNotConsiderNonCollapsibleWhitespace &&
      anchor_node->GetLayoutObject() &&
      !anchor_node->GetLayoutObject()->Style()->CollapseWhiteSpace())
    return Position();
  const String& string = ToText(anchor_node)->data();
  const UChar previous_character = string[prev.ComputeOffsetInContainerNode()];
  const bool is_space = option == kConsiderNonCollapsibleWhitespace
                            ? (IsSpaceOrNewline(previous_character) ||
                               previous_character == kNoBreakSpaceCharacter)
                            : IsCollapsibleWhitespace(previous_character);
  if (!is_space || !IsEditablePosition(prev))
    return Position();
  return prev;
}

// This assumes that it starts in editable content.
Position TrailingWhitespacePosition(const Position& position,
                                    TextAffinity,
                                    WhitespacePositionOption option) {
  DCHECK(!NeedsLayoutTreeUpdate(position));
  DCHECK(IsEditablePosition(position)) << position;
  if (position.IsNull())
    return Position();

  VisiblePosition visible_position = CreateVisiblePosition(position);
  UChar character_after_visible_position = CharacterAfter(visible_position);
  bool is_space =
      option == kConsiderNonCollapsibleWhitespace
          ? (IsSpaceOrNewline(character_after_visible_position) ||
             character_after_visible_position == kNoBreakSpaceCharacter)
          : IsCollapsibleWhitespace(character_after_visible_position);
  // The space must not be in another paragraph and it must be editable.
  if (is_space && !IsEndOfParagraph(visible_position) &&
      NextPositionOf(visible_position, kCannotCrossEditingBoundary).IsNotNull())
    return position;
  return Position();
}

unsigned NumEnclosingMailBlockquotes(const Position& p) {
  unsigned num = 0;
  for (Node* n = p.AnchorNode(); n; n = n->parentNode()) {
    if (IsMailHTMLBlockquoteElement(n))
      num++;
  }
  return num;
}

PositionWithAffinity PositionRespectingEditingBoundary(
    const Position& position,
    const LayoutPoint& local_point,
    Node* target_node) {
  if (!target_node->GetLayoutObject())
    return PositionWithAffinity();

  LayoutPoint selection_end_point = local_point;
  Element* editable_element = RootEditableElementOf(position);

  if (editable_element && !editable_element->contains(target_node)) {
    if (!editable_element->GetLayoutObject())
      return PositionWithAffinity();

    FloatPoint absolute_point = target_node->GetLayoutObject()->LocalToAbsolute(
        FloatPoint(selection_end_point));
    selection_end_point = LayoutPoint(
        editable_element->GetLayoutObject()->AbsoluteToLocal(absolute_point));
    target_node = editable_element;
  }

  return target_node->GetLayoutObject()->PositionForPoint(selection_end_point);
}

Position ComputePositionForNodeRemoval(const Position& position, Node& node) {
  if (position.IsNull())
    return position;
  switch (position.AnchorType()) {
    case PositionAnchorType::kBeforeChildren:
      if (!node.IsShadowIncludingInclusiveAncestorOf(
              position.ComputeContainerNode())) {
        return position;
      }
      return Position::InParentBeforeNode(node);
    case PositionAnchorType::kAfterChildren:
      if (!node.IsShadowIncludingInclusiveAncestorOf(
              position.ComputeContainerNode())) {
        return position;
      }
      return Position::InParentAfterNode(node);
    case PositionAnchorType::kOffsetInAnchor:
      if (position.ComputeContainerNode() == node.parentNode() &&
          static_cast<unsigned>(position.OffsetInContainerNode()) >
              node.NodeIndex()) {
        return Position(position.ComputeContainerNode(),
                        position.OffsetInContainerNode() - 1);
      }
      if (!node.IsShadowIncludingInclusiveAncestorOf(
              position.ComputeContainerNode())) {
        return position;
      }
      return Position::InParentBeforeNode(node);
    case PositionAnchorType::kAfterAnchor:
      if (!node.IsShadowIncludingInclusiveAncestorOf(position.AnchorNode()))
        return position;
      return Position::InParentAfterNode(node);
    case PositionAnchorType::kBeforeAnchor:
      if (!node.IsShadowIncludingInclusiveAncestorOf(position.AnchorNode()))
        return position;
      return Position::InParentBeforeNode(node);
  }
  NOTREACHED() << "We should handle all PositionAnchorType";
  return position;
}

bool IsMailHTMLBlockquoteElement(const Node* node) {
  if (!node || !node->IsHTMLElement())
    return false;

  const HTMLElement& element = ToHTMLElement(*node);
  return element.HasTagName(blockquoteTag) &&
         element.getAttribute("type") == "cite";
}

bool LineBreakExistsAtVisiblePosition(const VisiblePosition& visible_position) {
  return LineBreakExistsAtPosition(
      MostForwardCaretPosition(visible_position.DeepEquivalent()));
}

bool LineBreakExistsAtPosition(const Position& position) {
  if (position.IsNull())
    return false;

  if (isHTMLBRElement(*position.AnchorNode()) &&
      position.AtFirstEditingPositionForNode())
    return true;

  if (!position.AnchorNode()->GetLayoutObject())
    return false;

  if (!position.AnchorNode()->IsTextNode() ||
      !position.AnchorNode()->GetLayoutObject()->Style()->PreserveNewline())
    return false;

  Text* text_node = ToText(position.AnchorNode());
  unsigned offset = position.OffsetInContainerNode();
  return offset < text_node->length() && text_node->data()[offset] == '\n';
}

bool ElementCannotHaveEndTag(const Node& node) {
  if (!node.IsHTMLElement())
    return false;

  return !ToHTMLElement(node).ShouldSerializeEndTag();
}

// Modifies selections that have an end point at the edge of a table
// that contains the other endpoint so that they don't confuse
// code that iterates over selected paragraphs.
VisibleSelection SelectionForParagraphIteration(
    const VisibleSelection& original) {
  VisibleSelection new_selection(original);
  VisiblePosition start_of_selection(new_selection.VisibleStart());
  VisiblePosition end_of_selection(new_selection.VisibleEnd());

  // If the end of the selection to modify is just after a table, and if the
  // start of the selection is inside that table, then the last paragraph that
  // we'll want modify is the last one inside the table, not the table itself (a
  // table is itself a paragraph).
  if (Element* table = TableElementJustBefore(end_of_selection)) {
    if (start_of_selection.DeepEquivalent().AnchorNode()->IsDescendantOf(
            table)) {
      const VisiblePosition& new_end =
          PreviousPositionOf(end_of_selection, kCannotCrossEditingBoundary);
      if (new_end.IsNotNull()) {
        new_selection = CreateVisibleSelection(
            SelectionInDOMTree::Builder()
                .Collapse(start_of_selection.ToPositionWithAffinity())
                .Extend(new_end.DeepEquivalent())
                .Build());
      } else {
        new_selection = CreateVisibleSelection(
            SelectionInDOMTree::Builder()
                .Collapse(start_of_selection.ToPositionWithAffinity())
                .Build());
      }
    }
  }

  // If the start of the selection to modify is just before a table, and if the
  // end of the selection is inside that table, then the first paragraph we'll
  // want to modify is the first one inside the table, not the paragraph
  // containing the table itself.
  if (Element* table = TableElementJustAfter(start_of_selection)) {
    if (end_of_selection.DeepEquivalent().AnchorNode()->IsDescendantOf(table)) {
      const VisiblePosition new_start =
          NextPositionOf(start_of_selection, kCannotCrossEditingBoundary);
      if (new_start.IsNotNull()) {
        new_selection = CreateVisibleSelection(
            SelectionInDOMTree::Builder()
                .Collapse(new_start.ToPositionWithAffinity())
                .Extend(end_of_selection.DeepEquivalent())
                .Build());
      } else {
        new_selection = CreateVisibleSelection(
            SelectionInDOMTree::Builder()
                .Collapse(end_of_selection.ToPositionWithAffinity())
                .Build());
      }
    }
  }

  return new_selection;
}

// FIXME: indexForVisiblePosition and visiblePositionForIndex use TextIterators
// to convert between VisiblePositions and indices. But TextIterator iteration
// using TextIteratorEmitsCharactersBetweenAllVisiblePositions does not exactly
// match VisiblePosition iteration, so using them to preserve a selection during
// an editing opertion is unreliable. TextIterator's
// TextIteratorEmitsCharactersBetweenAllVisiblePositions mode needs to be fixed,
// or these functions need to be changed to iterate using actual
// VisiblePositions.
// FIXME: Deploy these functions everywhere that TextIterators are used to
// convert between VisiblePositions and indices.
int IndexForVisiblePosition(const VisiblePosition& visible_position,
                            ContainerNode*& scope) {
  if (visible_position.IsNull())
    return 0;

  Position p(visible_position.DeepEquivalent());
  Document& document = *p.GetDocument();
  DCHECK(!document.NeedsLayoutTreeUpdate());

  ShadowRoot* shadow_root = p.AnchorNode()->ContainingShadowRoot();

  if (shadow_root)
    scope = shadow_root;
  else
    scope = document.documentElement();

  EphemeralRange range(Position::FirstPositionInNode(scope),
                       p.ParentAnchoredEquivalent());

  return TextIterator::RangeLength(
      range.StartPosition(), range.EndPosition(),
      TextIteratorBehavior::AllVisiblePositionsRangeLengthBehavior());
}

EphemeralRange MakeRange(const VisiblePosition& start,
                         const VisiblePosition& end) {
  if (start.IsNull() || end.IsNull())
    return EphemeralRange();

  Position s = start.DeepEquivalent().ParentAnchoredEquivalent();
  Position e = end.DeepEquivalent().ParentAnchoredEquivalent();
  if (s.IsNull() || e.IsNull())
    return EphemeralRange();

  return EphemeralRange(s, e);
}

template <typename Strategy>
static EphemeralRangeTemplate<Strategy> NormalizeRangeAlgorithm(
    const EphemeralRangeTemplate<Strategy>& range) {
  DCHECK(range.IsNotNull());
  DCHECK(!range.GetDocument().NeedsLayoutTreeUpdate());
  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      range.GetDocument().Lifecycle());

  // TODO(yosin) We should not call |parentAnchoredEquivalent()|, it is
  // redundant.
  const PositionTemplate<Strategy> normalized_start =
      MostForwardCaretPosition(range.StartPosition())
          .ParentAnchoredEquivalent();
  const PositionTemplate<Strategy> normalized_end =
      MostBackwardCaretPosition(range.EndPosition()).ParentAnchoredEquivalent();
  // The order of the positions of |start| and |end| can be swapped after
  // upstream/downstream. e.g. editing/pasteboard/copy-display-none.html
  if (normalized_start.CompareTo(normalized_end) > 0)
    return EphemeralRangeTemplate<Strategy>(normalized_end, normalized_start);
  return EphemeralRangeTemplate<Strategy>(normalized_start, normalized_end);
}

EphemeralRange NormalizeRange(const EphemeralRange& range) {
  return NormalizeRangeAlgorithm<EditingStrategy>(range);
}

EphemeralRangeInFlatTree NormalizeRange(const EphemeralRangeInFlatTree& range) {
  return NormalizeRangeAlgorithm<EditingInFlatTreeStrategy>(range);
}

VisiblePosition VisiblePositionForIndex(int index, ContainerNode* scope) {
  if (!scope)
    return VisiblePosition();
  DCHECK(!scope->GetDocument().NeedsLayoutTreeUpdate());
  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      scope->GetDocument().Lifecycle());

  EphemeralRange range = PlainTextRange(index).CreateRangeForSelection(*scope);
  // Check for an invalid index. Certain editing operations invalidate indices
  // because of problems with
  // TextIteratorEmitsCharactersBetweenAllVisiblePositions.
  if (range.IsNull())
    return VisiblePosition();
  return CreateVisiblePosition(range.StartPosition());
}

bool IsRenderedAsNonInlineTableImageOrHR(const Node* node) {
  if (!node)
    return false;
  LayoutObject* layout_object = node->GetLayoutObject();
  return layout_object &&
         ((layout_object->IsTable() && !layout_object->IsInline()) ||
          (layout_object->IsImage() && !layout_object->IsInline()) ||
          layout_object->IsHR());
}

bool AreIdenticalElements(const Node& first, const Node& second) {
  if (!first.IsElementNode() || !second.IsElementNode())
    return false;

  const Element& first_element = ToElement(first);
  const Element& second_element = ToElement(second);
  if (!first_element.HasTagName(second_element.TagQName()))
    return false;

  if (!first_element.HasEquivalentAttributes(&second_element))
    return false;

  return HasEditableStyle(first_element) && HasEditableStyle(second_element);
}

bool IsNonTableCellHTMLBlockElement(const Node* node) {
  if (!node->IsHTMLElement())
    return false;

  const HTMLElement& element = ToHTMLElement(*node);
  return element.HasTagName(listingTag) || element.HasTagName(olTag) ||
         element.HasTagName(preTag) || element.HasTagName(tableTag) ||
         element.HasTagName(ulTag) || element.HasTagName(xmpTag) ||
         element.HasTagName(h1Tag) || element.HasTagName(h2Tag) ||
         element.HasTagName(h3Tag) || element.HasTagName(h4Tag) ||
         element.HasTagName(h5Tag);
}

bool IsBlockFlowElement(const Node& node) {
  LayoutObject* layout_object = node.GetLayoutObject();
  return node.IsElementNode() && layout_object &&
         layout_object->IsLayoutBlockFlow();
}

Position AdjustedSelectionStartForStyleComputation(const Position& position) {
  // This function is used by range style computations to avoid bugs like:
  // <rdar://problem/4017641> REGRESSION (Mail): you can only bold/unbold a
  // selection starting from end of line once
  // It is important to skip certain irrelevant content at the start of the
  // selection, so we do not wind up with a spurious "mixed" style.

  VisiblePosition visible_position = CreateVisiblePosition(position);
  if (visible_position.IsNull())
    return Position();

  // if the selection starts just before a paragraph break, skip over it
  if (IsEndOfParagraph(visible_position))
    return MostForwardCaretPosition(
        NextPositionOf(visible_position).DeepEquivalent());

  // otherwise, make sure to be at the start of the first selected node,
  // instead of possibly at the end of the last node before the selection
  return MostForwardCaretPosition(visible_position.DeepEquivalent());
}

bool IsInPasswordField(const Position& position) {
  TextControlElement* text_control = EnclosingTextControl(position);
  return isHTMLInputElement(text_control) &&
         toHTMLInputElement(text_control)->type() == InputTypeNames::password;
}

bool IsTextSecurityNode(const Node* node) {
  return node && node->GetLayoutObject() &&
         node->GetLayoutObject()->Style()->TextSecurity() !=
             ETextSecurity::kNone;
}

// If current position is at grapheme boundary, return 0; otherwise, return the
// distance to its nearest left grapheme boundary.
size_t ComputeDistanceToLeftGraphemeBoundary(const Position& position) {
  const Position& adjusted_position = PreviousPositionOf(
      NextPositionOf(position, PositionMoveType::kGraphemeCluster),
      PositionMoveType::kGraphemeCluster);
  DCHECK_EQ(position.AnchorNode(), adjusted_position.AnchorNode());
  DCHECK_GE(position.ComputeOffsetInContainerNode(),
            adjusted_position.ComputeOffsetInContainerNode());
  return static_cast<size_t>(position.ComputeOffsetInContainerNode() -
                             adjusted_position.ComputeOffsetInContainerNode());
}

// If current position is at grapheme boundary, return 0; otherwise, return the
// distance to its nearest right grapheme boundary.
size_t ComputeDistanceToRightGraphemeBoundary(const Position& position) {
  const Position& adjusted_position = NextPositionOf(
      PreviousPositionOf(position, PositionMoveType::kGraphemeCluster),
      PositionMoveType::kGraphemeCluster);
  DCHECK_EQ(position.AnchorNode(), adjusted_position.AnchorNode());
  DCHECK_GE(adjusted_position.ComputeOffsetInContainerNode(),
            position.ComputeOffsetInContainerNode());
  return static_cast<size_t>(adjusted_position.ComputeOffsetInContainerNode() -
                             position.ComputeOffsetInContainerNode());
}

const StaticRangeVector* TargetRangesForInputEvent(const Node& node) {
  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited. see http://crbug.com/590369 for more details.
  node.GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (!HasRichlyEditableStyle(node))
    return nullptr;
  const EphemeralRange& range =
      FirstEphemeralRangeOf(node.GetDocument()
                                .GetFrame()
                                ->Selection()
                                .ComputeVisibleSelectionInDOMTree());
  if (range.IsNull())
    return nullptr;
  return new StaticRangeVector(1, StaticRange::Create(range));
}

DispatchEventResult DispatchBeforeInputInsertText(
    Node* target,
    const String& data,
    InputEvent::InputType input_type,
    const StaticRangeVector* ranges) {
  if (!RuntimeEnabledFeatures::inputEventEnabled())
    return DispatchEventResult::kNotCanceled;
  if (!target)
    return DispatchEventResult::kNotCanceled;
  // TODO(chongz): Pass appropriate |ranges| after it's defined on spec.
  // http://w3c.github.io/editing/input-events.html#dom-inputevent-inputtype
  InputEvent* before_input_event = InputEvent::CreateBeforeInput(
      input_type, data, InputTypeIsCancelable(input_type),
      InputEvent::EventIsComposing::kNotComposing,
      ranges ? ranges : TargetRangesForInputEvent(*target));
  return target->DispatchEvent(before_input_event);
}

DispatchEventResult DispatchBeforeInputEditorCommand(
    Node* target,
    InputEvent::InputType input_type,
    const StaticRangeVector* ranges) {
  if (!RuntimeEnabledFeatures::inputEventEnabled())
    return DispatchEventResult::kNotCanceled;
  if (!target)
    return DispatchEventResult::kNotCanceled;
  InputEvent* before_input_event = InputEvent::CreateBeforeInput(
      input_type, g_null_atom, InputTypeIsCancelable(input_type),
      InputEvent::EventIsComposing::kNotComposing, ranges);
  return target->DispatchEvent(before_input_event);
}

DispatchEventResult DispatchBeforeInputDataTransfer(
    Node* target,
    InputEvent::InputType input_type,
    DataTransfer* data_transfer) {
  if (!RuntimeEnabledFeatures::inputEventEnabled())
    return DispatchEventResult::kNotCanceled;
  if (!target)
    return DispatchEventResult::kNotCanceled;

  DCHECK(input_type == InputEvent::InputType::kInsertFromPaste ||
         input_type == InputEvent::InputType::kInsertReplacementText ||
         input_type == InputEvent::InputType::kInsertFromDrop ||
         input_type == InputEvent::InputType::kDeleteByCut)
      << "Unsupported inputType: " << (int)input_type;

  InputEvent* before_input_event;

  if (HasRichlyEditableStyle(*(target->ToNode())) || !data_transfer) {
    before_input_event = InputEvent::CreateBeforeInput(
        input_type, data_transfer, InputTypeIsCancelable(input_type),
        InputEvent::EventIsComposing::kNotComposing,
        TargetRangesForInputEvent(*target));
  } else {
    const String& data = data_transfer->getData(kMimeTypeTextPlain);
    // TODO(chongz): Pass appropriate |ranges| after it's defined on spec.
    // http://w3c.github.io/editing/input-events.html#dom-inputevent-inputtype
    before_input_event = InputEvent::CreateBeforeInput(
        input_type, data, InputTypeIsCancelable(input_type),
        InputEvent::EventIsComposing::kNotComposing,
        TargetRangesForInputEvent(*target));
  }
  return target->DispatchEvent(before_input_event);
}

InputEvent::InputType DeletionInputTypeFromTextGranularity(
    DeleteDirection direction,
    TextGranularity granularity) {
  using InputType = InputEvent::InputType;
  switch (direction) {
    case DeleteDirection::kForward:
      if (granularity == kWordGranularity)
        return InputType::kDeleteWordForward;
      if (granularity == kLineBoundary)
        return InputType::kDeleteSoftLineForward;
      if (granularity == kParagraphBoundary)
        return InputType::kDeleteHardLineForward;
      return InputType::kDeleteContentForward;
    case DeleteDirection::kBackward:
      if (granularity == kWordGranularity)
        return InputType::kDeleteWordBackward;
      if (granularity == kLineBoundary)
        return InputType::kDeleteSoftLineBackward;
      if (granularity == kParagraphBoundary)
        return InputType::kDeleteHardLineBackward;
      return InputType::kDeleteContentBackward;
    default:
      return InputType::kNone;
  }
}

}  // namespace blink
