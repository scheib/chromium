/*
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009 Apple Inc. All rights
 * reserved.
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

#include "core/editing/VisibleUnits.h"

#include "core/HTMLNames.h"
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/FirstLetterPseudoElement.h"
#include "core/dom/NodeTraversal.h"
#include "core/dom/Text.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/Position.h"
#include "core/editing/PositionIterator.h"
#include "core/editing/RenderedPosition.h"
#include "core/editing/TextAffinity.h"
#include "core/editing/VisiblePosition.h"
#include "core/editing/iterators/BackwardsCharacterIterator.h"
#include "core/editing/iterators/BackwardsTextBuffer.h"
#include "core/editing/iterators/CharacterIterator.h"
#include "core/editing/iterators/ForwardsTextBuffer.h"
#include "core/editing/iterators/SimplifiedBackwardsTextIterator.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLBRElement.h"
#include "core/html/TextControlElement.h"
#include "core/layout/HitTestRequest.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutInline.h"
#include "core/layout/LayoutObject.h"
#include "core/layout/LayoutTextFragment.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutItem.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/api/LineLayoutAPIShim.h"
#include "core/layout/api/LineLayoutItem.h"
#include "core/layout/line/InlineIterator.h"
#include "core/layout/line/InlineTextBox.h"
#include "platform/heap/Handle.h"
#include "platform/text/TextBoundaries.h"
#include "platform/text/TextBreakIterator.h"

namespace blink {

template <typename PositionType>
static PositionType CanonicalizeCandidate(const PositionType& candidate) {
  if (candidate.IsNull())
    return PositionType();
  DCHECK(IsVisuallyEquivalentCandidate(candidate));
  PositionType upstream = MostBackwardCaretPosition(candidate);
  if (IsVisuallyEquivalentCandidate(upstream))
    return upstream;
  return candidate;
}

template <typename PositionType>
static PositionType CanonicalPosition(const PositionType& position) {
  // Sometimes updating selection positions can be extremely expensive and
  // occur frequently.  Often calling preventDefault on mousedown events can
  // avoid doing unnecessary text selection work.  http://crbug.com/472258.
  TRACE_EVENT0("input", "VisibleUnits::canonicalPosition");

  // FIXME (9535):  Canonicalizing to the leftmost candidate means that if
  // we're at a line wrap, we will ask layoutObjects to paint downstream
  // carets for other layoutObjects. To fix this, we need to either a) add
  // code to all paintCarets to pass the responsibility off to the appropriate
  // layoutObject for VisiblePosition's like these, or b) canonicalize to the
  // rightmost candidate unless the affinity is upstream.
  if (position.IsNull())
    return PositionType();

  DCHECK(position.GetDocument());
  DCHECK(!position.GetDocument()->NeedsLayoutTreeUpdate());

  const PositionType& backward_candidate = MostBackwardCaretPosition(position);
  if (IsVisuallyEquivalentCandidate(backward_candidate))
    return backward_candidate;

  const PositionType& forward_candidate = MostForwardCaretPosition(position);
  if (IsVisuallyEquivalentCandidate(forward_candidate))
    return forward_candidate;

  // When neither upstream or downstream gets us to a candidate
  // (upstream/downstream won't leave blocks or enter new ones), we search
  // forward and backward until we find one.
  const PositionType& next = CanonicalizeCandidate(NextCandidate(position));
  const PositionType& prev = CanonicalizeCandidate(PreviousCandidate(position));

  // The new position must be in the same editable element. Enforce that
  // first. Unless the descent is from a non-editable html element to an
  // editable body.
  Node* const node = position.ComputeContainerNode();
  if (node && node->GetDocument().documentElement() == node &&
      !HasEditableStyle(*node) && node->GetDocument().body() &&
      HasEditableStyle(*node->GetDocument().body()))
    return next.IsNotNull() ? next : prev;

  Element* const editing_root = RootEditableElementOf(position);
  // If the html element is editable, descending into its body will look like
  // a descent from non-editable to editable content since
  // |rootEditableElementOf()| always stops at the body.
  if ((editing_root &&
       editing_root->GetDocument().documentElement() == editing_root) ||
      position.AnchorNode()->IsDocumentNode())
    return next.IsNotNull() ? next : prev;

  Node* const next_node = next.AnchorNode();
  Node* const prev_node = prev.AnchorNode();
  const bool prev_is_in_same_editable_element =
      prev_node && RootEditableElementOf(prev) == editing_root;
  const bool next_is_in_same_editable_element =
      next_node && RootEditableElementOf(next) == editing_root;
  if (prev_is_in_same_editable_element && !next_is_in_same_editable_element)
    return prev;

  if (next_is_in_same_editable_element && !prev_is_in_same_editable_element)
    return next;

  if (!next_is_in_same_editable_element && !prev_is_in_same_editable_element)
    return PositionType();

  // The new position should be in the same block flow element. Favor that.
  Element* const original_block = node ? EnclosingBlockFlowElement(*node) : 0;
  const bool next_is_outside_original_block =
      !next_node->IsDescendantOf(original_block) && next_node != original_block;
  const bool prev_is_outside_original_block =
      !prev_node->IsDescendantOf(original_block) && prev_node != original_block;
  if (next_is_outside_original_block && !prev_is_outside_original_block)
    return prev;

  return next;
}

Position CanonicalPositionOf(const Position& position) {
  return CanonicalPosition(position);
}

PositionInFlatTree CanonicalPositionOf(const PositionInFlatTree& position) {
  return CanonicalPosition(position);
}

template <typename Strategy>
static PositionWithAffinityTemplate<Strategy> HonorEditingBoundaryAtOrBefore(
    const PositionWithAffinityTemplate<Strategy>& pos,
    const PositionTemplate<Strategy>& anchor) {
  if (pos.IsNull())
    return pos;

  ContainerNode* highest_root = HighestEditableRoot(anchor);

  // Return empty position if |pos| is not somewhere inside the editable
  // region containing this position
  if (highest_root && !pos.AnchorNode()->IsDescendantOf(highest_root))
    return PositionWithAffinityTemplate<Strategy>();

  // Return |pos| itself if the two are from the very same editable region, or
  // both are non-editable
  // TODO(yosin) In the non-editable case, just because the new position is
  // non-editable doesn't mean movement to it is allowed.
  // |VisibleSelection::adjustForEditableContent()| has this problem too.
  if (HighestEditableRoot(pos.GetPosition()) == highest_root)
    return pos;

  // Return empty position if this position is non-editable, but |pos| is
  // editable.
  // TODO(yosin) Move to the previous non-editable region.
  if (!highest_root)
    return PositionWithAffinityTemplate<Strategy>();

  // Return the last position before |pos| that is in the same editable region
  // as this position
  return LastEditablePositionBeforePositionInRoot(pos.GetPosition(),
                                                  *highest_root);
}

template <typename Strategy>
static VisiblePositionTemplate<Strategy> HonorEditingBoundaryAtOrBefore(
    const VisiblePositionTemplate<Strategy>& pos,
    const PositionTemplate<Strategy>& anchor) {
  DCHECK(pos.IsValid()) << pos;
  return CreateVisiblePosition(
      HonorEditingBoundaryAtOrBefore(pos.ToPositionWithAffinity(), anchor));
}

template <typename Strategy>
static VisiblePositionTemplate<Strategy> HonorEditingBoundaryAtOrAfter(
    const VisiblePositionTemplate<Strategy>& pos,
    const PositionTemplate<Strategy>& anchor) {
  DCHECK(pos.IsValid()) << pos;
  if (pos.IsNull())
    return pos;

  ContainerNode* highest_root = HighestEditableRoot(anchor);

  // Return empty position if |pos| is not somewhere inside the editable
  // region containing this position
  if (highest_root &&
      !pos.DeepEquivalent().AnchorNode()->IsDescendantOf(highest_root))
    return VisiblePositionTemplate<Strategy>();

  // Return |pos| itself if the two are from the very same editable region, or
  // both are non-editable
  // TODO(yosin) In the non-editable case, just because the new position is
  // non-editable doesn't mean movement to it is allowed.
  // |VisibleSelection::adjustForEditableContent()| has this problem too.
  if (HighestEditableRoot(pos.DeepEquivalent()) == highest_root)
    return pos;

  // Return empty position if this position is non-editable, but |pos| is
  // editable.
  // TODO(yosin) Move to the next non-editable region.
  if (!highest_root)
    return VisiblePositionTemplate<Strategy>();

  // Return the next position after |pos| that is in the same editable region
  // as this position
  return FirstEditableVisiblePositionAfterPositionInRoot(pos.DeepEquivalent(),
                                                         *highest_root);
}

static bool HasEditableStyle(const Node& node, EditableType editable_type) {
  if (editable_type == kHasEditableAXRole) {
    if (AXObjectCache* cache = node.GetDocument().ExistingAXObjectCache()) {
      if (cache->RootAXEditableElement(&node))
        return true;
    }
  }

  return HasEditableStyle(node);
}

static Element* RootEditableElement(const Node& node,
                                    EditableType editable_type) {
  if (editable_type == kHasEditableAXRole) {
    if (AXObjectCache* cache = node.GetDocument().ExistingAXObjectCache())
      return const_cast<Element*>(cache->RootAXEditableElement(&node));
  }

  return RootEditableElement(node);
}

static Element* RootAXEditableElementOf(const Position& position) {
  Node* node = position.ComputeContainerNode();
  if (!node)
    return 0;

  if (IsDisplayInsideTable(node))
    node = node->parentNode();

  return RootEditableElement(*node, kHasEditableAXRole);
}

static bool HasAXEditableStyle(const Node& node) {
  return HasEditableStyle(node, kHasEditableAXRole);
}

static ContainerNode* HighestEditableRoot(const Position& position,
                                          EditableType editable_type) {
  if (editable_type == kHasEditableAXRole)
    return HighestEditableRoot(position, RootAXEditableElementOf,
                               HasAXEditableStyle);

  return HighestEditableRoot(position);
}

static Node* PreviousLeafWithSameEditability(Node* node,
                                             EditableType editable_type) {
  bool editable = HasEditableStyle(*node, editable_type);
  node = PreviousAtomicLeafNode(*node);
  while (node) {
    if (editable == HasEditableStyle(*node, editable_type))
      return node;
    node = PreviousAtomicLeafNode(*node);
  }
  return 0;
}

static Node* NextLeafWithSameEditability(
    Node* node,
    EditableType editable_type = kContentIsEditable) {
  if (!node)
    return 0;

  bool editable = HasEditableStyle(*node, editable_type);
  node = NextAtomicLeafNode(*node);
  while (node) {
    if (editable == HasEditableStyle(*node, editable_type))
      return node;
    node = NextAtomicLeafNode(*node);
  }
  return 0;
}

// FIXME: consolidate with code in previousLinePosition.
static Position PreviousRootInlineBoxCandidatePosition(
    Node* node,
    const VisiblePosition& visible_position,
    EditableType editable_type) {
  DCHECK(visible_position.IsValid()) << visible_position;
  ContainerNode* highest_root =
      HighestEditableRoot(visible_position.DeepEquivalent(), editable_type);
  Node* previous_node = PreviousLeafWithSameEditability(node, editable_type);

  while (previous_node &&
         (!previous_node->GetLayoutObject() ||
          InSameLine(
              CreateVisiblePosition(FirstPositionInOrBeforeNode(previous_node)),
              visible_position)))
    previous_node =
        PreviousLeafWithSameEditability(previous_node, editable_type);

  while (previous_node && !previous_node->IsShadowRoot()) {
    if (HighestEditableRoot(FirstPositionInOrBeforeNode(previous_node),
                            editable_type) != highest_root)
      break;

    Position pos = isHTMLBRElement(*previous_node)
                       ? Position::BeforeNode(previous_node)
                       : Position::EditingPositionOf(
                             previous_node, CaretMaxOffset(previous_node));

    if (IsVisuallyEquivalentCandidate(pos))
      return pos;

    previous_node =
        PreviousLeafWithSameEditability(previous_node, editable_type);
  }
  return Position();
}

static Position NextRootInlineBoxCandidatePosition(
    Node* node,
    const VisiblePosition& visible_position,
    EditableType editable_type) {
  DCHECK(visible_position.IsValid()) << visible_position;
  ContainerNode* highest_root =
      HighestEditableRoot(visible_position.DeepEquivalent(), editable_type);
  Node* next_node = NextLeafWithSameEditability(node, editable_type);
  while (next_node && (!next_node->GetLayoutObject() ||
                       InSameLine(CreateVisiblePosition(
                                      FirstPositionInOrBeforeNode(next_node)),
                                  visible_position)))
    next_node = NextLeafWithSameEditability(next_node, kContentIsEditable);

  while (next_node && !next_node->IsShadowRoot()) {
    if (HighestEditableRoot(FirstPositionInOrBeforeNode(next_node),
                            editable_type) != highest_root)
      break;

    Position pos;
    pos = Position::EditingPositionOf(next_node, CaretMinOffset(next_node));

    if (IsVisuallyEquivalentCandidate(pos))
      return pos;

    next_node = NextLeafWithSameEditability(next_node, editable_type);
  }
  return Position();
}

class CachedLogicallyOrderedLeafBoxes {
 public:
  CachedLogicallyOrderedLeafBoxes();

  const InlineTextBox* PreviousTextBox(const RootInlineBox*,
                                       const InlineTextBox*);
  const InlineTextBox* NextTextBox(const RootInlineBox*, const InlineTextBox*);

  size_t size() const { return leaf_boxes_.size(); }
  const InlineBox* FirstBox() const { return leaf_boxes_[0]; }

 private:
  const Vector<InlineBox*>& CollectBoxes(const RootInlineBox*);
  int BoxIndexInLeaves(const InlineTextBox*) const;

  const RootInlineBox* root_inline_box_;
  Vector<InlineBox*> leaf_boxes_;
};

CachedLogicallyOrderedLeafBoxes::CachedLogicallyOrderedLeafBoxes()
    : root_inline_box_(0) {}

const InlineTextBox* CachedLogicallyOrderedLeafBoxes::PreviousTextBox(
    const RootInlineBox* root,
    const InlineTextBox* box) {
  if (!root)
    return 0;

  CollectBoxes(root);

  // If box is null, root is box's previous RootInlineBox, and previousBox is
  // the last logical box in root.
  int box_index = leaf_boxes_.size() - 1;
  if (box)
    box_index = BoxIndexInLeaves(box) - 1;

  for (int i = box_index; i >= 0; --i) {
    if (leaf_boxes_[i]->IsInlineTextBox())
      return ToInlineTextBox(leaf_boxes_[i]);
  }

  return 0;
}

const InlineTextBox* CachedLogicallyOrderedLeafBoxes::NextTextBox(
    const RootInlineBox* root,
    const InlineTextBox* box) {
  if (!root)
    return 0;

  CollectBoxes(root);

  // If box is null, root is box's next RootInlineBox, and nextBox is the first
  // logical box in root. Otherwise, root is box's RootInlineBox, and nextBox is
  // the next logical box in the same line.
  size_t next_box_index = 0;
  if (box)
    next_box_index = BoxIndexInLeaves(box) + 1;

  for (size_t i = next_box_index; i < leaf_boxes_.size(); ++i) {
    if (leaf_boxes_[i]->IsInlineTextBox())
      return ToInlineTextBox(leaf_boxes_[i]);
  }

  return 0;
}

const Vector<InlineBox*>& CachedLogicallyOrderedLeafBoxes::CollectBoxes(
    const RootInlineBox* root) {
  if (root_inline_box_ != root) {
    root_inline_box_ = root;
    leaf_boxes_.clear();
    root->CollectLeafBoxesInLogicalOrder(leaf_boxes_);
  }
  return leaf_boxes_;
}

int CachedLogicallyOrderedLeafBoxes::BoxIndexInLeaves(
    const InlineTextBox* box) const {
  for (size_t i = 0; i < leaf_boxes_.size(); ++i) {
    if (box == leaf_boxes_[i])
      return i;
  }
  return 0;
}

static const InlineTextBox* LogicallyPreviousBox(
    const VisiblePosition& visible_position,
    const InlineTextBox* text_box,
    bool& previous_box_in_different_block,
    CachedLogicallyOrderedLeafBoxes& leaf_boxes) {
  DCHECK(visible_position.IsValid()) << visible_position;
  const InlineBox* start_box = text_box;

  const InlineTextBox* previous_box =
      leaf_boxes.PreviousTextBox(&start_box->Root(), text_box);
  if (previous_box)
    return previous_box;

  previous_box = leaf_boxes.PreviousTextBox(start_box->Root().PrevRootBox(), 0);
  if (previous_box)
    return previous_box;

  while (1) {
    Node* start_node = start_box->GetLineLayoutItem().NonPseudoNode();
    if (!start_node)
      break;

    Position position = PreviousRootInlineBoxCandidatePosition(
        start_node, visible_position, kContentIsEditable);
    if (position.IsNull())
      break;

    RenderedPosition rendered_position(position, TextAffinity::kDownstream);
    RootInlineBox* previous_root = rendered_position.RootBox();
    if (!previous_root)
      break;

    previous_box = leaf_boxes.PreviousTextBox(previous_root, 0);
    if (previous_box) {
      previous_box_in_different_block = true;
      return previous_box;
    }

    if (!leaf_boxes.size())
      break;
    start_box = leaf_boxes.FirstBox();
  }
  return 0;
}

static const InlineTextBox* LogicallyNextBox(
    const VisiblePosition& visible_position,
    const InlineTextBox* text_box,
    bool& next_box_in_different_block,
    CachedLogicallyOrderedLeafBoxes& leaf_boxes) {
  DCHECK(visible_position.IsValid()) << visible_position;
  const InlineBox* start_box = text_box;

  const InlineTextBox* next_box =
      leaf_boxes.NextTextBox(&start_box->Root(), text_box);
  if (next_box)
    return next_box;

  next_box = leaf_boxes.NextTextBox(start_box->Root().NextRootBox(), 0);
  if (next_box)
    return next_box;

  while (1) {
    Node* start_node = start_box->GetLineLayoutItem().NonPseudoNode();
    if (!start_node)
      break;

    Position position = NextRootInlineBoxCandidatePosition(
        start_node, visible_position, kContentIsEditable);
    if (position.IsNull())
      break;

    RenderedPosition rendered_position(position, TextAffinity::kDownstream);
    RootInlineBox* next_root = rendered_position.RootBox();
    if (!next_root)
      break;

    next_box = leaf_boxes.NextTextBox(next_root, 0);
    if (next_box) {
      next_box_in_different_block = true;
      return next_box;
    }

    if (!leaf_boxes.size())
      break;
    start_box = leaf_boxes.FirstBox();
  }
  return 0;
}

static TextBreakIterator* WordBreakIteratorForMinOffsetBoundary(
    const VisiblePosition& visible_position,
    const InlineTextBox* text_box,
    int& previous_box_length,
    bool& previous_box_in_different_block,
    Vector<UChar, 1024>& string,
    CachedLogicallyOrderedLeafBoxes& leaf_boxes) {
  DCHECK(visible_position.IsValid()) << visible_position;
  previous_box_in_different_block = false;

  // FIXME: Handle the case when we don't have an inline text box.
  const InlineTextBox* previous_box = LogicallyPreviousBox(
      visible_position, text_box, previous_box_in_different_block, leaf_boxes);

  int len = 0;
  string.clear();
  if (previous_box) {
    previous_box_length = previous_box->Len();
    previous_box->GetLineLayoutItem().GetText().AppendTo(
        string, previous_box->Start(), previous_box_length);
    len += previous_box_length;
  }
  text_box->GetLineLayoutItem().GetText().AppendTo(string, text_box->Start(),
                                                   text_box->Len());
  len += text_box->Len();

  return WordBreakIterator(string.data(), len);
}

static TextBreakIterator* WordBreakIteratorForMaxOffsetBoundary(
    const VisiblePosition& visible_position,
    const InlineTextBox* text_box,
    bool& next_box_in_different_block,
    Vector<UChar, 1024>& string,
    CachedLogicallyOrderedLeafBoxes& leaf_boxes) {
  DCHECK(visible_position.IsValid()) << visible_position;
  next_box_in_different_block = false;

  // FIXME: Handle the case when we don't have an inline text box.
  const InlineTextBox* next_box = LogicallyNextBox(
      visible_position, text_box, next_box_in_different_block, leaf_boxes);

  int len = 0;
  string.clear();
  text_box->GetLineLayoutItem().GetText().AppendTo(string, text_box->Start(),
                                                   text_box->Len());
  len += text_box->Len();
  if (next_box) {
    next_box->GetLineLayoutItem().GetText().AppendTo(string, next_box->Start(),
                                                     next_box->Len());
    len += next_box->Len();
  }

  return WordBreakIterator(string.data(), len);
}

static bool IsLogicalStartOfWord(TextBreakIterator* iter,
                                 int position,
                                 bool hard_line_break) {
  bool boundary = hard_line_break ? true : iter->isBoundary(position);
  if (!boundary)
    return false;

  iter->following(position);
  // isWordTextBreak returns true after moving across a word and false after
  // moving across a punctuation/space.
  return IsWordTextBreak(iter);
}

static bool IslogicalEndOfWord(TextBreakIterator* iter,
                               int position,
                               bool hard_line_break) {
  bool boundary = iter->isBoundary(position);
  return (hard_line_break || boundary) && IsWordTextBreak(iter);
}

enum CursorMovementDirection { kMoveLeft, kMoveRight };

static VisiblePosition VisualWordPosition(
    const VisiblePosition& visible_position,
    CursorMovementDirection direction,
    bool skips_space_when_moving_right) {
  DCHECK(visible_position.IsValid()) << visible_position;
  if (visible_position.IsNull())
    return VisiblePosition();

  TextDirection block_direction =
      DirectionOfEnclosingBlock(visible_position.DeepEquivalent());
  InlineBox* previously_visited_box = 0;
  VisiblePosition current = visible_position;
  TextBreakIterator* iter = 0;

  CachedLogicallyOrderedLeafBoxes leaf_boxes;
  Vector<UChar, 1024> string;

  while (1) {
    VisiblePosition adjacent_character_position = direction == kMoveRight
                                                      ? RightPositionOf(current)
                                                      : LeftPositionOf(current);
    if (adjacent_character_position.DeepEquivalent() ==
            current.DeepEquivalent() ||
        adjacent_character_position.IsNull())
      return VisiblePosition();

    InlineBoxPosition box_position = ComputeInlineBoxPosition(
        adjacent_character_position.DeepEquivalent(), TextAffinity::kUpstream);
    InlineBox* box = box_position.inline_box;
    int offset_in_box = box_position.offset_in_box;

    if (!box)
      break;
    if (!box->IsInlineTextBox()) {
      current = adjacent_character_position;
      continue;
    }

    InlineTextBox* text_box = ToInlineTextBox(box);
    int previous_box_length = 0;
    bool previous_box_in_different_block = false;
    bool next_box_in_different_block = false;
    bool moving_into_new_box = previously_visited_box != box;

    if (offset_in_box == box->CaretMinOffset()) {
      iter = WordBreakIteratorForMinOffsetBoundary(
          visible_position, text_box, previous_box_length,
          previous_box_in_different_block, string, leaf_boxes);
    } else if (offset_in_box == box->CaretMaxOffset()) {
      iter = WordBreakIteratorForMaxOffsetBoundary(visible_position, text_box,
                                                   next_box_in_different_block,
                                                   string, leaf_boxes);
    } else if (moving_into_new_box) {
      iter = WordBreakIterator(text_box->GetLineLayoutItem().GetText(),
                               text_box->Start(), text_box->Len());
      previously_visited_box = box;
    }

    if (!iter)
      break;

    iter->first();
    int offset_in_iterator =
        offset_in_box - text_box->Start() + previous_box_length;

    bool is_word_break;
    bool box_has_same_directionality_as_block =
        box->Direction() == block_direction;
    bool moving_backward =
        (direction == kMoveLeft && box->Direction() == TextDirection::kLtr) ||
        (direction == kMoveRight && box->Direction() == TextDirection::kRtl);
    if ((skips_space_when_moving_right &&
         box_has_same_directionality_as_block) ||
        (!skips_space_when_moving_right && moving_backward)) {
      bool logical_start_in_layout_object =
          offset_in_box == static_cast<int>(text_box->Start()) &&
          previous_box_in_different_block;
      is_word_break = IsLogicalStartOfWord(iter, offset_in_iterator,
                                           logical_start_in_layout_object);
    } else {
      bool logical_end_in_layout_object =
          offset_in_box ==
              static_cast<int>(text_box->Start() + text_box->Len()) &&
          next_box_in_different_block;
      is_word_break = IslogicalEndOfWord(iter, offset_in_iterator,
                                         logical_end_in_layout_object);
    }

    if (is_word_break)
      return adjacent_character_position;

    current = adjacent_character_position;
  }
  return VisiblePosition();
}

VisiblePosition LeftWordPosition(const VisiblePosition& visible_position,
                                 bool skips_space_when_moving_right) {
  DCHECK(visible_position.IsValid()) << visible_position;
  VisiblePosition left_word_break = VisualWordPosition(
      visible_position, kMoveLeft, skips_space_when_moving_right);
  left_word_break = HonorEditingBoundaryAtOrBefore(
      left_word_break, visible_position.DeepEquivalent());

  // FIXME: How should we handle a non-editable position?
  if (left_word_break.IsNull() &&
      IsEditablePosition(visible_position.DeepEquivalent())) {
    TextDirection block_direction =
        DirectionOfEnclosingBlock(visible_position.DeepEquivalent());
    left_word_break = block_direction == TextDirection::kLtr
                          ? StartOfEditableContent(visible_position)
                          : EndOfEditableContent(visible_position);
  }
  return left_word_break;
}

VisiblePosition RightWordPosition(const VisiblePosition& visible_position,
                                  bool skips_space_when_moving_right) {
  DCHECK(visible_position.IsValid()) << visible_position;
  VisiblePosition right_word_break = VisualWordPosition(
      visible_position, kMoveRight, skips_space_when_moving_right);
  right_word_break = HonorEditingBoundaryAtOrBefore(
      right_word_break, visible_position.DeepEquivalent());

  // FIXME: How should we handle a non-editable position?
  if (right_word_break.IsNull() &&
      IsEditablePosition(visible_position.DeepEquivalent())) {
    TextDirection block_direction =
        DirectionOfEnclosingBlock(visible_position.DeepEquivalent());
    right_word_break = block_direction == TextDirection::kLtr
                           ? EndOfEditableContent(visible_position)
                           : StartOfEditableContent(visible_position);
  }
  return right_word_break;
}

template <typename Strategy>
static ContainerNode* NonShadowBoundaryParentNode(Node* node) {
  ContainerNode* parent = Strategy::Parent(*node);
  return parent && !parent->IsShadowRoot() ? parent : nullptr;
}

template <typename Strategy>
static Node* ParentEditingBoundary(const PositionTemplate<Strategy>& position) {
  Node* const anchor_node = position.AnchorNode();
  if (!anchor_node)
    return nullptr;

  Node* document_element = anchor_node->GetDocument().documentElement();
  if (!document_element)
    return nullptr;

  Node* boundary = position.ComputeContainerNode();
  while (boundary != document_element &&
         NonShadowBoundaryParentNode<Strategy>(boundary) &&
         HasEditableStyle(*anchor_node) ==
             HasEditableStyle(*Strategy::Parent(*boundary)))
    boundary = NonShadowBoundaryParentNode<Strategy>(boundary);

  return boundary;
}

enum BoundarySearchContextAvailability {
  kDontHaveMoreContext,
  kMayHaveMoreContext
};

typedef unsigned (*BoundarySearchFunction)(const UChar*,
                                           unsigned length,
                                           unsigned offset,
                                           BoundarySearchContextAvailability,
                                           bool& need_more_context);

template <typename Strategy>
static PositionTemplate<Strategy> PreviousBoundary(
    const VisiblePositionTemplate<Strategy>& c,
    BoundarySearchFunction search_function) {
  DCHECK(c.IsValid()) << c;
  const PositionTemplate<Strategy> pos = c.DeepEquivalent();
  Node* boundary = ParentEditingBoundary(pos);
  if (!boundary)
    return PositionTemplate<Strategy>();

  const PositionTemplate<Strategy> start =
      PositionTemplate<Strategy>::EditingPositionOf(boundary, 0)
          .ParentAnchoredEquivalent();
  const PositionTemplate<Strategy> end = pos.ParentAnchoredEquivalent();

  ForwardsTextBuffer suffix_string;
  if (RequiresContextForWordBoundary(CharacterBefore(c))) {
    TextIteratorAlgorithm<Strategy> forwards_iterator(
        end, PositionTemplate<Strategy>::AfterNode(boundary));
    while (!forwards_iterator.AtEnd()) {
      forwards_iterator.CopyTextTo(&suffix_string);
      int context_end_index = EndOfFirstWordBoundaryContext(
          suffix_string.Data() + suffix_string.Size() -
              forwards_iterator.length(),
          forwards_iterator.length());
      if (context_end_index < forwards_iterator.length()) {
        suffix_string.Shrink(forwards_iterator.length() - context_end_index);
        break;
      }
      forwards_iterator.Advance();
    }
  }

  unsigned suffix_length = suffix_string.Size();
  BackwardsTextBuffer string;
  string.PushRange(suffix_string.Data(), suffix_string.Size());

  SimplifiedBackwardsTextIteratorAlgorithm<Strategy> it(start, end);
  int remaining_length = 0;
  unsigned next = 0;
  bool need_more_context = false;
  while (!it.AtEnd()) {
    bool in_text_security_mode = it.IsInTextSecurityMode();
    // iterate to get chunks until the searchFunction returns a non-zero
    // value.
    if (!in_text_security_mode) {
      int run_offset = 0;
      do {
        run_offset += it.CopyTextTo(&string, run_offset, string.Capacity());
        // TODO(xiaochengh): The following line takes O(string.size()) time,
        // which makes quadratic overall running time in the worst case.
        // Should improve it in some way.
        next = search_function(string.Data(), string.Size(),
                               string.Size() - suffix_length,
                               kMayHaveMoreContext, need_more_context);
      } while (!next && run_offset < it.length());
      if (next) {
        remaining_length = it.length() - run_offset;
        break;
      }
    } else {
      // Treat bullets used in the text security mode as regular
      // characters when looking for boundaries
      string.PushCharacters('x', it.length());
      next = 0;
    }
    it.Advance();
  }
  if (need_more_context) {
    // The last search returned the beginning of the buffer and asked for
    // more context, but there is no earlier text. Force a search with
    // what's available.
    // TODO(xiaochengh): Do we have to search the whole string?
    next = search_function(string.Data(), string.Size(),
                           string.Size() - suffix_length, kDontHaveMoreContext,
                           need_more_context);
    DCHECK(!need_more_context);
  }

  if (!next)
    return it.AtEnd() ? it.StartPosition() : pos;

  Node* node = it.StartContainer();
  int boundary_offset = remaining_length + next;
  if (node->IsTextNode() && boundary_offset <= node->MaxCharacterOffset()) {
    // The next variable contains a usable index into a text node
    return PositionTemplate<Strategy>(node, boundary_offset);
  }

  // Use the character iterator to translate the next value into a DOM
  // position.
  BackwardsCharacterIteratorAlgorithm<Strategy> char_it(start, end);
  char_it.Advance(string.Size() - suffix_length - next);
  // TODO(yosin) charIt can get out of shadow host.
  return char_it.EndPosition();
}

template <typename Strategy>
static PositionTemplate<Strategy> NextBoundary(
    const VisiblePositionTemplate<Strategy>& c,
    BoundarySearchFunction search_function) {
  DCHECK(c.IsValid()) << c;
  PositionTemplate<Strategy> pos = c.DeepEquivalent();
  Node* boundary = ParentEditingBoundary(pos);
  if (!boundary)
    return PositionTemplate<Strategy>();

  Document& d = boundary->GetDocument();
  const PositionTemplate<Strategy> start(pos.ParentAnchoredEquivalent());

  BackwardsTextBuffer prefix_string;
  if (RequiresContextForWordBoundary(CharacterAfter(c))) {
    SimplifiedBackwardsTextIteratorAlgorithm<Strategy> backwards_iterator(
        PositionTemplate<Strategy>::FirstPositionInNode(&d), start);
    while (!backwards_iterator.AtEnd()) {
      backwards_iterator.CopyTextTo(&prefix_string);
      int context_start_index = StartOfLastWordBoundaryContext(
          prefix_string.Data(), backwards_iterator.length());
      if (context_start_index > 0) {
        prefix_string.Shrink(context_start_index);
        break;
      }
      backwards_iterator.Advance();
    }
  }

  unsigned prefix_length = prefix_string.Size();
  ForwardsTextBuffer string;
  string.PushRange(prefix_string.Data(), prefix_string.Size());

  const PositionTemplate<Strategy> search_start =
      PositionTemplate<Strategy>::EditingPositionOf(
          start.AnchorNode(), start.OffsetInContainerNode());
  const PositionTemplate<Strategy> search_end =
      PositionTemplate<Strategy>::LastPositionInNode(boundary);
  TextIteratorAlgorithm<Strategy> it(
      search_start, search_end,
      TextIteratorBehavior::Builder()
          .SetEmitsCharactersBetweenAllVisiblePositions(true)
          .Build());
  const unsigned kInvalidOffset = static_cast<unsigned>(-1);
  unsigned next = kInvalidOffset;
  unsigned offset = prefix_length;
  bool need_more_context = false;
  while (!it.AtEnd()) {
    // Keep asking the iterator for chunks until the search function
    // returns an end value not equal to the length of the string passed to
    // it.
    bool in_text_security_mode = it.IsInTextSecurityMode();
    if (!in_text_security_mode) {
      int run_offset = 0;
      do {
        run_offset += it.CopyTextTo(&string, run_offset, string.Capacity());
        next = search_function(string.Data(), string.Size(), offset,
                               kMayHaveMoreContext, need_more_context);
        if (!need_more_context) {
          // When the search does not need more context, skip all examined
          // characters except the last one, in case it is a boundary.
          offset = string.Size();
          U16_BACK_1(string.Data(), 0, offset);
        }
      } while (next == string.Size() && run_offset < it.length());
      if (next != string.Size())
        break;
    } else {
      // Treat bullets used in the text security mode as regular
      // characters when looking for boundaries
      string.PushCharacters('x', it.length());
      next = string.Size();
    }
    it.Advance();
  }
  if (need_more_context) {
    // The last search returned the end of the buffer and asked for more
    // context, but there is no further text. Force a search with what's
    // available.
    // TODO(xiaochengh): Do we still have to search the whole string?
    next = search_function(string.Data(), string.Size(), prefix_length,
                           kDontHaveMoreContext, need_more_context);
    DCHECK(!need_more_context);
  }

  if (it.AtEnd() && next == string.Size()) {
    pos = it.StartPositionInCurrentContainer();
  } else if (next != kInvalidOffset && next != prefix_length) {
    // Use the character iterator to translate the next value into a DOM
    // position.
    CharacterIteratorAlgorithm<Strategy> char_it(
        search_start, search_end,
        TextIteratorBehavior::Builder()
            .SetEmitsCharactersBetweenAllVisiblePositions(true)
            .Build());
    char_it.Advance(next - prefix_length - 1);
    pos = char_it.EndPosition();

    if (char_it.CharacterAt(0) == '\n') {
      // TODO(yosin) workaround for collapsed range (where only start
      // position is correct) emitted for some emitted newlines
      // (see rdar://5192593)
      const VisiblePositionTemplate<Strategy> vis_pos =
          CreateVisiblePosition(pos);
      if (vis_pos.DeepEquivalent() ==
          CreateVisiblePosition(char_it.StartPosition()).DeepEquivalent()) {
        char_it.Advance(1);
        pos = char_it.StartPosition();
      }
    }
  }

  return pos;
}

// ---------

static unsigned StartWordBoundary(
    const UChar* characters,
    unsigned length,
    unsigned offset,
    BoundarySearchContextAvailability may_have_more_context,
    bool& need_more_context) {
  TRACE_EVENT0("blink", "startWordBoundary");
  DCHECK(offset);
  if (may_have_more_context &&
      !StartOfLastWordBoundaryContext(characters, offset)) {
    need_more_context = true;
    return 0;
  }
  need_more_context = false;
  int start, end;
  U16_BACK_1(characters, 0, offset);
  FindWordBoundary(characters, length, offset, &start, &end);
  return start;
}

template <typename Strategy>
static PositionTemplate<Strategy> StartOfWordAlgorithm(
    const VisiblePositionTemplate<Strategy>& c,
    EWordSide side) {
  DCHECK(c.IsValid()) << c;
  // TODO(yosin) This returns a null VP for c at the start of the document
  // and |side| == |LeftWordIfOnBoundary|
  VisiblePositionTemplate<Strategy> p = c;
  if (side == kRightWordIfOnBoundary) {
    // at paragraph end, the startofWord is the current position
    if (IsEndOfParagraph(c))
      return c.DeepEquivalent();

    p = NextPositionOf(c);
    if (p.IsNull())
      return c.DeepEquivalent();
  }
  return PreviousBoundary(p, StartWordBoundary);
}

Position StartOfWordPosition(const VisiblePosition& position, EWordSide side) {
  return StartOfWordAlgorithm<EditingStrategy>(position, side);
}

VisiblePosition StartOfWord(const VisiblePosition& position, EWordSide side) {
  return CreateVisiblePosition(StartOfWordPosition(position, side));
}

PositionInFlatTree StartOfWordPosition(
    const VisiblePositionInFlatTree& position,
    EWordSide side) {
  return StartOfWordAlgorithm<EditingInFlatTreeStrategy>(position, side);
}

VisiblePositionInFlatTree StartOfWord(const VisiblePositionInFlatTree& position,
                                      EWordSide side) {
  return CreateVisiblePosition(StartOfWordPosition(position, side));
}

static unsigned EndWordBoundary(
    const UChar* characters,
    unsigned length,
    unsigned offset,
    BoundarySearchContextAvailability may_have_more_context,
    bool& need_more_context) {
  DCHECK_LE(offset, length);
  if (may_have_more_context &&
      EndOfFirstWordBoundaryContext(characters + offset, length - offset) ==
          static_cast<int>(length - offset)) {
    need_more_context = true;
    return length;
  }
  need_more_context = false;
  return FindWordEndBoundary(characters, length, offset);
}

template <typename Strategy>
static PositionTemplate<Strategy> EndOfWordAlgorithm(
    const VisiblePositionTemplate<Strategy>& c,
    EWordSide side) {
  DCHECK(c.IsValid()) << c;
  VisiblePositionTemplate<Strategy> p = c;
  if (side == kLeftWordIfOnBoundary) {
    if (IsStartOfParagraph(c))
      return c.DeepEquivalent();

    p = PreviousPositionOf(c);
    if (p.IsNull())
      return c.DeepEquivalent();
  } else if (IsEndOfParagraph(c)) {
    return c.DeepEquivalent();
  }

  return NextBoundary(p, EndWordBoundary);
}

Position EndOfWordPosition(const VisiblePosition& position, EWordSide side) {
  return EndOfWordAlgorithm<EditingStrategy>(position, side);
}

VisiblePosition EndOfWord(const VisiblePosition& position, EWordSide side) {
  return CreateVisiblePosition(EndOfWordPosition(position, side),
                               VP_UPSTREAM_IF_POSSIBLE);
}

PositionInFlatTree EndOfWordPosition(const VisiblePositionInFlatTree& position,
                                     EWordSide side) {
  return EndOfWordAlgorithm<EditingInFlatTreeStrategy>(position, side);
}

VisiblePositionInFlatTree EndOfWord(const VisiblePositionInFlatTree& position,
                                    EWordSide side) {
  return CreateVisiblePosition(EndOfWordPosition(position, side),
                               VP_UPSTREAM_IF_POSSIBLE);
}

static unsigned PreviousWordPositionBoundary(
    const UChar* characters,
    unsigned length,
    unsigned offset,
    BoundarySearchContextAvailability may_have_more_context,
    bool& need_more_context) {
  if (may_have_more_context &&
      !StartOfLastWordBoundaryContext(characters, offset)) {
    need_more_context = true;
    return 0;
  }
  need_more_context = false;
  return FindNextWordFromIndex(characters, length, offset, false);
}

VisiblePosition PreviousWordPosition(const VisiblePosition& c) {
  DCHECK(c.IsValid()) << c;
  VisiblePosition prev =
      CreateVisiblePosition(PreviousBoundary(c, PreviousWordPositionBoundary));
  return HonorEditingBoundaryAtOrBefore(prev, c.DeepEquivalent());
}

static unsigned NextWordPositionBoundary(
    const UChar* characters,
    unsigned length,
    unsigned offset,
    BoundarySearchContextAvailability may_have_more_context,
    bool& need_more_context) {
  if (may_have_more_context &&
      EndOfFirstWordBoundaryContext(characters + offset, length - offset) ==
          static_cast<int>(length - offset)) {
    need_more_context = true;
    return length;
  }
  need_more_context = false;
  return FindNextWordFromIndex(characters, length, offset, true);
}

VisiblePosition NextWordPosition(const VisiblePosition& c) {
  DCHECK(c.IsValid()) << c;
  VisiblePosition next = CreateVisiblePosition(
      NextBoundary(c, NextWordPositionBoundary), VP_UPSTREAM_IF_POSSIBLE);
  return HonorEditingBoundaryAtOrAfter(next, c.DeepEquivalent());
}

// ---------

enum LineEndpointComputationMode { kUseLogicalOrdering, kUseInlineBoxOrdering };
template <typename Strategy>
static PositionWithAffinityTemplate<Strategy> StartPositionForLine(
    const PositionWithAffinityTemplate<Strategy>& c,
    LineEndpointComputationMode mode) {
  if (c.IsNull())
    return PositionWithAffinityTemplate<Strategy>();

  RootInlineBox* root_box =
      RenderedPosition(c.GetPosition(), c.Affinity()).RootBox();
  if (!root_box) {
    // There are VisiblePositions at offset 0 in blocks without
    // RootInlineBoxes, like empty editable blocks and bordered blocks.
    PositionTemplate<Strategy> p = c.GetPosition();
    if (p.AnchorNode()->GetLayoutObject() &&
        p.AnchorNode()->GetLayoutObject()->IsLayoutBlock() &&
        !p.ComputeEditingOffset())
      return c;

    return PositionWithAffinityTemplate<Strategy>();
  }

  Node* start_node;
  InlineBox* start_box;
  if (mode == kUseLogicalOrdering) {
    start_node = root_box->GetLogicalStartBoxWithNode(start_box);
    if (!start_node)
      return PositionWithAffinityTemplate<Strategy>();
  } else {
    // Generated content (e.g. list markers and CSS :before and :after
    // pseudoelements) have no corresponding DOM element, and so cannot be
    // represented by a VisiblePosition. Use whatever follows instead.
    start_box = root_box->FirstLeafChild();
    while (true) {
      if (!start_box)
        return PositionWithAffinityTemplate<Strategy>();

      start_node = start_box->GetLineLayoutItem().NonPseudoNode();
      if (start_node)
        break;

      start_box = start_box->NextLeafChild();
    }
  }

  return PositionWithAffinityTemplate<Strategy>(
      start_node->IsTextNode()
          ? PositionTemplate<Strategy>(ToText(start_node),
                                       ToInlineTextBox(start_box)->Start())
          : PositionTemplate<Strategy>::BeforeNode(start_node));
}

template <typename Strategy>
static PositionWithAffinityTemplate<Strategy> StartOfLineAlgorithm(
    const PositionWithAffinityTemplate<Strategy>& c) {
  // TODO: this is the current behavior that might need to be fixed.
  // Please refer to https://bugs.webkit.org/show_bug.cgi?id=49107 for detail.
  PositionWithAffinityTemplate<Strategy> vis_pos =
      StartPositionForLine(c, kUseInlineBoxOrdering);
  return HonorEditingBoundaryAtOrBefore(vis_pos, c.GetPosition());
}

static PositionWithAffinity StartOfLine(
    const PositionWithAffinity& current_position) {
  return StartOfLineAlgorithm<EditingStrategy>(current_position);
}

static PositionInFlatTreeWithAffinity StartOfLine(
    const PositionInFlatTreeWithAffinity& current_position) {
  return StartOfLineAlgorithm<EditingInFlatTreeStrategy>(current_position);
}

// FIXME: Rename this function to reflect the fact it ignores bidi levels.
VisiblePosition StartOfLine(const VisiblePosition& current_position) {
  DCHECK(current_position.IsValid()) << current_position;
  return CreateVisiblePosition(
      StartOfLine(current_position.ToPositionWithAffinity()));
}

VisiblePositionInFlatTree StartOfLine(
    const VisiblePositionInFlatTree& current_position) {
  DCHECK(current_position.IsValid()) << current_position;
  return CreateVisiblePosition(
      StartOfLine(current_position.ToPositionWithAffinity()));
}

template <typename Strategy>
static PositionWithAffinityTemplate<Strategy> LogicalStartOfLineAlgorithm(
    const PositionWithAffinityTemplate<Strategy>& c) {
  // TODO: this is the current behavior that might need to be fixed.
  // Please refer to https://bugs.webkit.org/show_bug.cgi?id=49107 for detail.
  PositionWithAffinityTemplate<Strategy> vis_pos =
      StartPositionForLine(c, kUseLogicalOrdering);

  if (ContainerNode* editable_root = HighestEditableRoot(c.GetPosition())) {
    if (!editable_root->contains(vis_pos.GetPosition().ComputeContainerNode()))
      return PositionWithAffinityTemplate<Strategy>(
          PositionTemplate<Strategy>::FirstPositionInNode(editable_root));
  }

  return HonorEditingBoundaryAtOrBefore(vis_pos, c.GetPosition());
}

VisiblePosition LogicalStartOfLine(const VisiblePosition& current_position) {
  DCHECK(current_position.IsValid()) << current_position;
  return CreateVisiblePosition(LogicalStartOfLineAlgorithm<EditingStrategy>(
      current_position.ToPositionWithAffinity()));
}

VisiblePositionInFlatTree LogicalStartOfLine(
    const VisiblePositionInFlatTree& current_position) {
  DCHECK(current_position.IsValid()) << current_position;
  return CreateVisiblePosition(
      LogicalStartOfLineAlgorithm<EditingInFlatTreeStrategy>(
          current_position.ToPositionWithAffinity()));
}

template <typename Strategy>
static VisiblePositionTemplate<Strategy> EndPositionForLine(
    const VisiblePositionTemplate<Strategy>& c,
    LineEndpointComputationMode mode) {
  DCHECK(c.IsValid()) << c;
  if (c.IsNull())
    return VisiblePositionTemplate<Strategy>();

  RootInlineBox* root_box = RenderedPosition(c).RootBox();
  if (!root_box) {
    // There are VisiblePositions at offset 0 in blocks without
    // RootInlineBoxes, like empty editable blocks and bordered blocks.
    const PositionTemplate<Strategy> p = c.DeepEquivalent();
    if (p.AnchorNode()->GetLayoutObject() &&
        p.AnchorNode()->GetLayoutObject()->IsLayoutBlock() &&
        !p.ComputeEditingOffset())
      return c;
    return VisiblePositionTemplate<Strategy>();
  }

  Node* end_node;
  InlineBox* end_box;
  if (mode == kUseLogicalOrdering) {
    end_node = root_box->GetLogicalEndBoxWithNode(end_box);
    if (!end_node)
      return VisiblePositionTemplate<Strategy>();
  } else {
    // Generated content (e.g. list markers and CSS :before and :after
    // pseudo elements) have no corresponding DOM element, and so cannot be
    // represented by a VisiblePosition. Use whatever precedes instead.
    end_box = root_box->LastLeafChild();
    while (true) {
      if (!end_box)
        return VisiblePositionTemplate<Strategy>();

      end_node = end_box->GetLineLayoutItem().NonPseudoNode();
      if (end_node)
        break;

      end_box = end_box->PrevLeafChild();
    }
  }

  PositionTemplate<Strategy> pos;
  if (isHTMLBRElement(*end_node)) {
    pos = PositionTemplate<Strategy>::BeforeNode(end_node);
  } else if (end_box->IsInlineTextBox() && end_node->IsTextNode()) {
    InlineTextBox* end_text_box = ToInlineTextBox(end_box);
    int end_offset = end_text_box->Start();
    if (!end_text_box->IsLineBreak())
      end_offset += end_text_box->Len();
    pos = PositionTemplate<Strategy>(ToText(end_node), end_offset);
  } else {
    pos = PositionTemplate<Strategy>::AfterNode(end_node);
  }

  return CreateVisiblePosition(pos, VP_UPSTREAM_IF_POSSIBLE);
}

// TODO(yosin) Rename this function to reflect the fact it ignores bidi levels.
template <typename Strategy>
static VisiblePositionTemplate<Strategy> EndOfLineAlgorithm(
    const VisiblePositionTemplate<Strategy>& current_position) {
  DCHECK(current_position.IsValid()) << current_position;
  // TODO(yosin) this is the current behavior that might need to be fixed.
  // Please refer to https://bugs.webkit.org/show_bug.cgi?id=49107 for detail.
  VisiblePositionTemplate<Strategy> vis_pos =
      EndPositionForLine(current_position, kUseInlineBoxOrdering);

  // Make sure the end of line is at the same line as the given input
  // position. Else use the previous position to obtain end of line. This
  // condition happens when the input position is before the space character
  // at the end of a soft-wrapped non-editable line. In this scenario,
  // |endPositionForLine()| would incorrectly hand back a position in the next
  // line instead. This fix is to account for the discrepancy between lines
  // with "webkit-line-break:after-white-space" style versus lines without
  // that style, which would break before a space by default.
  if (!InSameLine(current_position, vis_pos)) {
    vis_pos = PreviousPositionOf(current_position);
    if (vis_pos.IsNull())
      return VisiblePositionTemplate<Strategy>();
    vis_pos = EndPositionForLine(vis_pos, kUseInlineBoxOrdering);
  }

  return HonorEditingBoundaryAtOrAfter(vis_pos,
                                       current_position.DeepEquivalent());
}

// TODO(yosin) Rename this function to reflect the fact it ignores bidi levels.
VisiblePosition EndOfLine(const VisiblePosition& current_position) {
  return EndOfLineAlgorithm<EditingStrategy>(current_position);
}

VisiblePositionInFlatTree EndOfLine(
    const VisiblePositionInFlatTree& current_position) {
  return EndOfLineAlgorithm<EditingInFlatTreeStrategy>(current_position);
}

template <typename Strategy>
static bool InSameLogicalLine(const VisiblePositionTemplate<Strategy>& a,
                              const VisiblePositionTemplate<Strategy>& b) {
  DCHECK(a.IsValid()) << a;
  DCHECK(b.IsValid()) << b;
  return a.IsNotNull() && LogicalStartOfLine(a).DeepEquivalent() ==
                              LogicalStartOfLine(b).DeepEquivalent();
}

template <typename Strategy>
VisiblePositionTemplate<Strategy> LogicalEndOfLineAlgorithm(
    const VisiblePositionTemplate<Strategy>& current_position) {
  DCHECK(current_position.IsValid()) << current_position;
  // TODO(yosin) this is the current behavior that might need to be fixed.
  // Please refer to https://bugs.webkit.org/show_bug.cgi?id=49107 for detail.
  VisiblePositionTemplate<Strategy> vis_pos =
      EndPositionForLine(current_position, kUseLogicalOrdering);

  // Make sure the end of line is at the same line as the given input
  // position. For a wrapping line, the logical end position for the
  // not-last-2-lines might incorrectly hand back the logical beginning of the
  // next line. For example,
  // <div contenteditable dir="rtl" style="line-break:before-white-space">xyz
  // a xyz xyz xyz xyz xyz xyz xyz xyz xyz xyz </div>
  // In this case, use the previous position of the computed logical end
  // position.
  if (!InSameLogicalLine(current_position, vis_pos))
    vis_pos = PreviousPositionOf(vis_pos);

  if (ContainerNode* editable_root =
          HighestEditableRoot(current_position.DeepEquivalent())) {
    if (!editable_root->contains(
            vis_pos.DeepEquivalent().ComputeContainerNode()))
      return CreateVisiblePosition(
          PositionTemplate<Strategy>::LastPositionInNode(editable_root));
  }

  return HonorEditingBoundaryAtOrAfter(vis_pos,
                                       current_position.DeepEquivalent());
}

VisiblePosition LogicalEndOfLine(const VisiblePosition& current_position) {
  return LogicalEndOfLineAlgorithm<EditingStrategy>(current_position);
}

VisiblePositionInFlatTree LogicalEndOfLine(
    const VisiblePositionInFlatTree& current_position) {
  return LogicalEndOfLineAlgorithm<EditingInFlatTreeStrategy>(current_position);
}

template <typename Strategy>
bool InSameLineAlgorithm(
    const PositionWithAffinityTemplate<Strategy>& position1,
    const PositionWithAffinityTemplate<Strategy>& position2) {
  if (position1.IsNull() || position2.IsNull())
    return false;
  DCHECK_EQ(position1.GetDocument(), position2.GetDocument());
  DCHECK(!position1.GetDocument()->NeedsLayoutTreeUpdate());

  PositionWithAffinityTemplate<Strategy> start_of_line1 =
      StartOfLine(position1);
  PositionWithAffinityTemplate<Strategy> start_of_line2 =
      StartOfLine(position2);
  if (start_of_line1 == start_of_line2)
    return true;
  PositionTemplate<Strategy> canonicalized1 =
      CanonicalPositionOf(start_of_line1.GetPosition());
  if (canonicalized1 == start_of_line2.GetPosition())
    return true;
  return canonicalized1 == CanonicalPositionOf(start_of_line2.GetPosition());
}

bool InSameLine(const PositionWithAffinity& a, const PositionWithAffinity& b) {
  return InSameLineAlgorithm<EditingStrategy>(a, b);
}

bool InSameLine(const PositionInFlatTreeWithAffinity& position1,
                const PositionInFlatTreeWithAffinity& position2) {
  return InSameLineAlgorithm<EditingInFlatTreeStrategy>(position1, position2);
}

bool InSameLine(const VisiblePosition& position1,
                const VisiblePosition& position2) {
  DCHECK(position1.IsValid()) << position1;
  DCHECK(position2.IsValid()) << position2;
  return InSameLine(position1.ToPositionWithAffinity(),
                    position2.ToPositionWithAffinity());
}

bool InSameLine(const VisiblePositionInFlatTree& position1,
                const VisiblePositionInFlatTree& position2) {
  DCHECK(position1.IsValid()) << position1;
  DCHECK(position2.IsValid()) << position2;
  return InSameLine(position1.ToPositionWithAffinity(),
                    position2.ToPositionWithAffinity());
}

template <typename Strategy>
bool IsStartOfLineAlgorithm(const VisiblePositionTemplate<Strategy>& p) {
  DCHECK(p.IsValid()) << p;
  return p.IsNotNull() && p.DeepEquivalent() == StartOfLine(p).DeepEquivalent();
}

bool IsStartOfLine(const VisiblePosition& p) {
  return IsStartOfLineAlgorithm<EditingStrategy>(p);
}

bool IsStartOfLine(const VisiblePositionInFlatTree& p) {
  return IsStartOfLineAlgorithm<EditingInFlatTreeStrategy>(p);
}

template <typename Strategy>
bool IsEndOfLineAlgorithm(const VisiblePositionTemplate<Strategy>& p) {
  DCHECK(p.IsValid()) << p;
  return p.IsNotNull() && p.DeepEquivalent() == EndOfLine(p).DeepEquivalent();
}

bool IsEndOfLine(const VisiblePosition& p) {
  return IsEndOfLineAlgorithm<EditingStrategy>(p);
}

bool IsEndOfLine(const VisiblePositionInFlatTree& p) {
  return IsEndOfLineAlgorithm<EditingInFlatTreeStrategy>(p);
}

template <typename Strategy>
static bool IsLogicalEndOfLineAlgorithm(
    const VisiblePositionTemplate<Strategy>& p) {
  DCHECK(p.IsValid()) << p;
  return p.IsNotNull() &&
         p.DeepEquivalent() == LogicalEndOfLine(p).DeepEquivalent();
}

bool IsLogicalEndOfLine(const VisiblePosition& p) {
  return IsLogicalEndOfLineAlgorithm<EditingStrategy>(p);
}

bool IsLogicalEndOfLine(const VisiblePositionInFlatTree& p) {
  return IsLogicalEndOfLineAlgorithm<EditingInFlatTreeStrategy>(p);
}

static inline LayoutPoint AbsoluteLineDirectionPointToLocalPointInBlock(
    RootInlineBox* root,
    LayoutUnit line_direction_point) {
  DCHECK(root);
  LineLayoutBlockFlow containing_block = root->Block();
  FloatPoint absolute_block_point =
      containing_block.LocalToAbsolute(FloatPoint());
  if (containing_block.HasOverflowClip())
    absolute_block_point -= FloatSize(containing_block.ScrolledContentOffset());

  if (root->Block().IsHorizontalWritingMode())
    return LayoutPoint(
        LayoutUnit(line_direction_point - absolute_block_point.X()),
        root->BlockDirectionPointInLine());

  return LayoutPoint(
      root->BlockDirectionPointInLine(),
      LayoutUnit(line_direction_point - absolute_block_point.Y()));
}

VisiblePosition PreviousLinePosition(const VisiblePosition& visible_position,
                                     LayoutUnit line_direction_point,
                                     EditableType editable_type) {
  DCHECK(visible_position.IsValid()) << visible_position;

  Position p = visible_position.DeepEquivalent();
  Node* node = p.AnchorNode();

  if (!node)
    return VisiblePosition();

  LayoutObject* layout_object = node->GetLayoutObject();
  if (!layout_object)
    return VisiblePosition();

  RootInlineBox* root = 0;
  InlineBox* box = ComputeInlineBoxPosition(visible_position).inline_box;
  if (box) {
    root = box->Root().PrevRootBox();
    // We want to skip zero height boxes.
    // This could happen in case it is a TrailingFloatsRootInlineBox.
    if (!root || !root->LogicalHeight() || !root->FirstLeafChild())
      root = 0;
  }

  if (!root) {
    Position position = PreviousRootInlineBoxCandidatePosition(
        node, visible_position, editable_type);
    if (position.IsNotNull()) {
      RenderedPosition rendered_position((CreateVisiblePosition(position)));
      root = rendered_position.RootBox();
      if (!root)
        return CreateVisiblePosition(position);
    }
  }

  if (root) {
    // FIXME: Can be wrong for multi-column layout and with transforms.
    LayoutPoint point_in_line = AbsoluteLineDirectionPointToLocalPointInBlock(
        root, line_direction_point);
    LineLayoutItem line_layout_item =
        root->ClosestLeafChildForPoint(point_in_line, IsEditablePosition(p))
            ->GetLineLayoutItem();
    Node* node = line_layout_item.GetNode();
    if (node && EditingIgnoresContent(*node))
      return VisiblePosition::InParentBeforeNode(*node);
    return CreateVisiblePosition(
        line_layout_item.PositionForPoint(point_in_line));
  }

  // Could not find a previous line. This means we must already be on the first
  // line. Move to the start of the content in this block, which effectively
  // moves us to the start of the line we're on.
  Element* root_element = HasEditableStyle(*node, editable_type)
                              ? RootEditableElement(*node, editable_type)
                              : node->GetDocument().documentElement();
  if (!root_element)
    return VisiblePosition();
  return VisiblePosition::FirstPositionInNode(root_element);
}

VisiblePosition NextLinePosition(const VisiblePosition& visible_position,
                                 LayoutUnit line_direction_point,
                                 EditableType editable_type) {
  DCHECK(visible_position.IsValid()) << visible_position;

  Position p = visible_position.DeepEquivalent();
  Node* node = p.AnchorNode();

  if (!node)
    return VisiblePosition();

  LayoutObject* layout_object = node->GetLayoutObject();
  if (!layout_object)
    return VisiblePosition();

  RootInlineBox* root = 0;
  InlineBox* box = ComputeInlineBoxPosition(visible_position).inline_box;
  if (box) {
    root = box->Root().NextRootBox();
    // We want to skip zero height boxes.
    // This could happen in case it is a TrailingFloatsRootInlineBox.
    if (!root || !root->LogicalHeight() || !root->FirstLeafChild())
      root = 0;
  }

  if (!root) {
    // FIXME: We need do the same in previousLinePosition.
    Node* child = NodeTraversal::ChildAt(*node, p.ComputeEditingOffset());
    node = child ? child : &NodeTraversal::LastWithinOrSelf(*node);
    Position position = NextRootInlineBoxCandidatePosition(
        node, visible_position, editable_type);
    if (position.IsNotNull()) {
      RenderedPosition rendered_position((CreateVisiblePosition(position)));
      root = rendered_position.RootBox();
      if (!root)
        return CreateVisiblePosition(position);
    }
  }

  if (root) {
    // FIXME: Can be wrong for multi-column layout and with transforms.
    LayoutPoint point_in_line = AbsoluteLineDirectionPointToLocalPointInBlock(
        root, line_direction_point);
    LineLayoutItem line_layout_item =
        root->ClosestLeafChildForPoint(point_in_line, IsEditablePosition(p))
            ->GetLineLayoutItem();
    Node* node = line_layout_item.GetNode();
    if (node && EditingIgnoresContent(*node))
      return VisiblePosition::InParentBeforeNode(*node);
    return CreateVisiblePosition(
        line_layout_item.PositionForPoint(point_in_line));
  }

  // Could not find a next line. This means we must already be on the last line.
  // Move to the end of the content in this block, which effectively moves us
  // to the end of the line we're on.
  Element* root_element = HasEditableStyle(*node, editable_type)
                              ? RootEditableElement(*node, editable_type)
                              : node->GetDocument().documentElement();
  if (!root_element)
    return VisiblePosition();
  return VisiblePosition::LastPositionInNode(root_element);
}

// ---------

static unsigned StartSentenceBoundary(const UChar* characters,
                                      unsigned length,
                                      unsigned,
                                      BoundarySearchContextAvailability,
                                      bool&) {
  TextBreakIterator* iterator = SentenceBreakIterator(characters, length);
  // FIXME: The following function can return -1; we don't handle that.
  return iterator->preceding(length);
}

template <typename Strategy>
static VisiblePositionTemplate<Strategy> StartOfSentenceAlgorithm(
    const VisiblePositionTemplate<Strategy>& c) {
  DCHECK(c.IsValid()) << c;
  return CreateVisiblePosition(PreviousBoundary(c, StartSentenceBoundary));
}

VisiblePosition StartOfSentence(const VisiblePosition& c) {
  return StartOfSentenceAlgorithm<EditingStrategy>(c);
}

VisiblePositionInFlatTree StartOfSentence(const VisiblePositionInFlatTree& c) {
  return StartOfSentenceAlgorithm<EditingInFlatTreeStrategy>(c);
}

static unsigned EndSentenceBoundary(const UChar* characters,
                                    unsigned length,
                                    unsigned,
                                    BoundarySearchContextAvailability,
                                    bool&) {
  TextBreakIterator* iterator = SentenceBreakIterator(characters, length);
  return iterator->next();
}

// TODO(yosin) This includes the space after the punctuation that marks the end
// of the sentence.
template <typename Strategy>
static VisiblePositionTemplate<Strategy> EndOfSentenceAlgorithm(
    const VisiblePositionTemplate<Strategy>& c) {
  DCHECK(c.IsValid()) << c;
  return CreateVisiblePosition(NextBoundary(c, EndSentenceBoundary),
                               VP_UPSTREAM_IF_POSSIBLE);
}

VisiblePosition EndOfSentence(const VisiblePosition& c) {
  return EndOfSentenceAlgorithm<EditingStrategy>(c);
}

VisiblePositionInFlatTree EndOfSentence(const VisiblePositionInFlatTree& c) {
  return EndOfSentenceAlgorithm<EditingInFlatTreeStrategy>(c);
}

static unsigned PreviousSentencePositionBoundary(
    const UChar* characters,
    unsigned length,
    unsigned,
    BoundarySearchContextAvailability,
    bool&) {
  // FIXME: This is identical to startSentenceBoundary. I'm pretty sure that's
  // not right.
  TextBreakIterator* iterator = SentenceBreakIterator(characters, length);
  // FIXME: The following function can return -1; we don't handle that.
  return iterator->preceding(length);
}

VisiblePosition PreviousSentencePosition(const VisiblePosition& c) {
  DCHECK(c.IsValid()) << c;
  VisiblePosition prev = CreateVisiblePosition(
      PreviousBoundary(c, PreviousSentencePositionBoundary));
  return HonorEditingBoundaryAtOrBefore(prev, c.DeepEquivalent());
}

static unsigned NextSentencePositionBoundary(const UChar* characters,
                                             unsigned length,
                                             unsigned,
                                             BoundarySearchContextAvailability,
                                             bool&) {
  // FIXME: This is identical to endSentenceBoundary. This isn't right, it needs
  // to move to the equivlant position in the following sentence.
  TextBreakIterator* iterator = SentenceBreakIterator(characters, length);
  return iterator->following(0);
}

VisiblePosition NextSentencePosition(const VisiblePosition& c) {
  DCHECK(c.IsValid()) << c;
  VisiblePosition next = CreateVisiblePosition(
      NextBoundary(c, NextSentencePositionBoundary), VP_UPSTREAM_IF_POSSIBLE);
  return HonorEditingBoundaryAtOrAfter(next, c.DeepEquivalent());
}

EphemeralRange ExpandEndToSentenceBoundary(const EphemeralRange& range) {
  DCHECK(range.IsNotNull());
  const VisiblePosition& visible_end =
      CreateVisiblePosition(range.EndPosition());
  DCHECK(visible_end.IsNotNull());
  const Position& sentence_end = EndOfSentence(visible_end).DeepEquivalent();
  // TODO(xiaochengh): |sentenceEnd < range.endPosition()| is possible,
  // which would trigger a DCHECK in EphemeralRange's constructor if we return
  // it directly. However, this shouldn't happen and needs to be fixed.
  return EphemeralRange(
      range.StartPosition(),
      sentence_end.IsNotNull() && sentence_end > range.EndPosition()
          ? sentence_end
          : range.EndPosition());
}

EphemeralRange ExpandRangeToSentenceBoundary(const EphemeralRange& range) {
  DCHECK(range.IsNotNull());
  const VisiblePosition& visible_start =
      CreateVisiblePosition(range.StartPosition());
  DCHECK(visible_start.IsNotNull());
  const Position& sentence_start =
      StartOfSentence(visible_start).DeepEquivalent();
  // TODO(xiaochengh): |sentenceStart > range.startPosition()| is possible,
  // which would trigger a DCHECK in EphemeralRange's constructor if we return
  // it directly. However, this shouldn't happen and needs to be fixed.
  return ExpandEndToSentenceBoundary(EphemeralRange(
      sentence_start.IsNotNull() && sentence_start < range.StartPosition()
          ? sentence_start
          : range.StartPosition(),
      range.EndPosition()));
}

static bool NodeIsUserSelectAll(const Node* node) {
  return node && node->GetLayoutObject() &&
         node->GetLayoutObject()->Style()->UserSelect() == EUserSelect::kAll;
}

template <typename Strategy>
PositionTemplate<Strategy> StartOfParagraphAlgorithm(
    const PositionTemplate<Strategy>& position,
    EditingBoundaryCrossingRule boundary_crossing_rule) {
  Node* const start_node = position.AnchorNode();

  if (!start_node)
    return PositionTemplate<Strategy>();

  if (IsRenderedAsNonInlineTableImageOrHR(start_node))
    return PositionTemplate<Strategy>::BeforeNode(start_node);

  Element* const start_block = EnclosingBlock(
      PositionTemplate<Strategy>::FirstPositionInOrBeforeNode(start_node),
      kCannotCrossEditingBoundary);
  ContainerNode* const highest_root = HighestEditableRoot(position);
  const bool start_node_is_editable = HasEditableStyle(*start_node);

  Node* candidate_node = start_node;
  PositionAnchorType candidate_type = position.AnchorType();
  int candidate_offset = position.ComputeEditingOffset();

  Node* previous_node_iterator = start_node;
  while (previous_node_iterator) {
    if (boundary_crossing_rule == kCannotCrossEditingBoundary &&
        !NodeIsUserSelectAll(previous_node_iterator) &&
        HasEditableStyle(*previous_node_iterator) != start_node_is_editable)
      break;
    if (boundary_crossing_rule == kCanSkipOverEditingBoundary) {
      while (previous_node_iterator &&
             HasEditableStyle(*previous_node_iterator) !=
                 start_node_is_editable) {
        previous_node_iterator =
            Strategy::PreviousPostOrder(*previous_node_iterator, start_block);
      }
      if (!previous_node_iterator ||
          !previous_node_iterator->IsDescendantOf(highest_root))
        break;
    }

    const LayoutItem layout_item =
        LayoutItem(previous_node_iterator->GetLayoutObject());
    if (layout_item.IsNull()) {
      previous_node_iterator =
          Strategy::PreviousPostOrder(*previous_node_iterator, start_block);
      continue;
    }
    const ComputedStyle& style = layout_item.StyleRef();
    if (style.Visibility() != EVisibility::kVisible) {
      previous_node_iterator =
          Strategy::PreviousPostOrder(*previous_node_iterator, start_block);
      continue;
    }

    if (layout_item.IsBR() || IsEnclosingBlock(previous_node_iterator))
      break;

    if (layout_item.IsText() &&
        ToLayoutText(previous_node_iterator->GetLayoutObject())
            ->ResolvedTextLength()) {
      SECURITY_DCHECK(previous_node_iterator->IsTextNode());
      if (style.PreserveNewline()) {
        LayoutText* text =
            ToLayoutText(previous_node_iterator->GetLayoutObject());
        int index = text->TextLength();
        if (previous_node_iterator == start_node && candidate_offset < index)
          index = max(0, candidate_offset);
        while (--index >= 0) {
          if ((*text)[index] == '\n')
            return PositionTemplate<Strategy>(ToText(previous_node_iterator),
                                              index + 1);
        }
      }
      candidate_node = previous_node_iterator;
      candidate_type = PositionAnchorType::kOffsetInAnchor;
      candidate_offset = 0;
      previous_node_iterator =
          Strategy::PreviousPostOrder(*previous_node_iterator, start_block);
    } else if (EditingIgnoresContent(*previous_node_iterator) ||
               IsDisplayInsideTable(previous_node_iterator)) {
      candidate_node = previous_node_iterator;
      candidate_type = PositionAnchorType::kBeforeAnchor;
      previous_node_iterator = previous_node_iterator->previousSibling()
                                   ? previous_node_iterator->previousSibling()
                                   : Strategy::PreviousPostOrder(
                                         *previous_node_iterator, start_block);
    } else {
      previous_node_iterator =
          Strategy::PreviousPostOrder(*previous_node_iterator, start_block);
    }
  }

  if (candidate_type == PositionAnchorType::kOffsetInAnchor)
    return PositionTemplate<Strategy>(candidate_node, candidate_offset);

  return PositionTemplate<Strategy>(candidate_node, candidate_type);
}

template <typename Strategy>
VisiblePositionTemplate<Strategy> StartOfParagraphAlgorithm(
    const VisiblePositionTemplate<Strategy>& visible_position,
    EditingBoundaryCrossingRule boundary_crossing_rule) {
  DCHECK(visible_position.IsValid()) << visible_position;
  return CreateVisiblePosition(StartOfParagraphAlgorithm(
      visible_position.DeepEquivalent(), boundary_crossing_rule));
}

VisiblePosition StartOfParagraph(
    const VisiblePosition& c,
    EditingBoundaryCrossingRule boundary_crossing_rule) {
  return StartOfParagraphAlgorithm<EditingStrategy>(c, boundary_crossing_rule);
}

VisiblePositionInFlatTree StartOfParagraph(
    const VisiblePositionInFlatTree& c,
    EditingBoundaryCrossingRule boundary_crossing_rule) {
  return StartOfParagraphAlgorithm<EditingInFlatTreeStrategy>(
      c, boundary_crossing_rule);
}

template <typename Strategy>
static PositionTemplate<Strategy> EndOfParagraphAlgorithm(
    const PositionTemplate<Strategy>& position,
    EditingBoundaryCrossingRule boundary_crossing_rule) {
  Node* const start_node = position.AnchorNode();

  if (!start_node)
    return PositionTemplate<Strategy>();

  if (IsRenderedAsNonInlineTableImageOrHR(start_node))
    return PositionTemplate<Strategy>::AfterNode(start_node);

  Element* const start_block = EnclosingBlock(
      PositionTemplate<Strategy>::FirstPositionInOrBeforeNode(start_node),
      kCannotCrossEditingBoundary);
  ContainerNode* const highest_root = HighestEditableRoot(position);
  const bool start_node_is_editable = HasEditableStyle(*start_node);

  Node* candidate_node = start_node;
  PositionAnchorType candidate_type = position.AnchorType();
  int candidate_offset = position.ComputeEditingOffset();

  Node* next_node_iterator = start_node;
  while (next_node_iterator) {
    if (boundary_crossing_rule == kCannotCrossEditingBoundary &&
        !NodeIsUserSelectAll(next_node_iterator) &&
        HasEditableStyle(*next_node_iterator) != start_node_is_editable)
      break;
    if (boundary_crossing_rule == kCanSkipOverEditingBoundary) {
      while (next_node_iterator &&
             HasEditableStyle(*next_node_iterator) != start_node_is_editable)
        next_node_iterator = Strategy::Next(*next_node_iterator, start_block);
      if (!next_node_iterator ||
          !next_node_iterator->IsDescendantOf(highest_root))
        break;
    }

    LayoutObject* const layout_object = next_node_iterator->GetLayoutObject();
    if (!layout_object) {
      next_node_iterator = Strategy::Next(*next_node_iterator, start_block);
      continue;
    }
    const ComputedStyle& style = layout_object->StyleRef();
    if (style.Visibility() != EVisibility::kVisible) {
      next_node_iterator = Strategy::Next(*next_node_iterator, start_block);
      continue;
    }

    if (layout_object->IsBR() || IsEnclosingBlock(next_node_iterator))
      break;

    // FIXME: We avoid returning a position where the layoutObject can't accept
    // the caret.
    if (layout_object->IsText() &&
        ToLayoutText(layout_object)->ResolvedTextLength()) {
      SECURITY_DCHECK(next_node_iterator->IsTextNode());
      LayoutText* const text = ToLayoutText(layout_object);
      if (style.PreserveNewline()) {
        const int length = ToLayoutText(layout_object)->TextLength();
        for (int i = (next_node_iterator == start_node ? candidate_offset : 0);
             i < length; ++i) {
          if ((*text)[i] == '\n')
            return PositionTemplate<Strategy>(ToText(next_node_iterator),
                                              i + text->TextStartOffset());
        }
      }

      candidate_node = next_node_iterator;
      candidate_type = PositionAnchorType::kOffsetInAnchor;
      candidate_offset =
          layout_object->CaretMaxOffset() + text->TextStartOffset();
      next_node_iterator = Strategy::Next(*next_node_iterator, start_block);
    } else if (EditingIgnoresContent(*next_node_iterator) ||
               IsDisplayInsideTable(next_node_iterator)) {
      candidate_node = next_node_iterator;
      candidate_type = PositionAnchorType::kAfterAnchor;
      next_node_iterator =
          Strategy::NextSkippingChildren(*next_node_iterator, start_block);
    } else {
      next_node_iterator = Strategy::Next(*next_node_iterator, start_block);
    }
  }

  if (candidate_type == PositionAnchorType::kOffsetInAnchor)
    return PositionTemplate<Strategy>(candidate_node, candidate_offset);

  return PositionTemplate<Strategy>(candidate_node, candidate_type);
}

template <typename Strategy>
static VisiblePositionTemplate<Strategy> EndOfParagraphAlgorithm(
    const VisiblePositionTemplate<Strategy>& visible_position,
    EditingBoundaryCrossingRule boundary_crossing_rule) {
  DCHECK(visible_position.IsValid()) << visible_position;
  return CreateVisiblePosition(EndOfParagraphAlgorithm(
      visible_position.DeepEquivalent(), boundary_crossing_rule));
}

VisiblePosition EndOfParagraph(
    const VisiblePosition& c,
    EditingBoundaryCrossingRule boundary_crossing_rule) {
  return EndOfParagraphAlgorithm<EditingStrategy>(c, boundary_crossing_rule);
}

VisiblePositionInFlatTree EndOfParagraph(
    const VisiblePositionInFlatTree& c,
    EditingBoundaryCrossingRule boundary_crossing_rule) {
  return EndOfParagraphAlgorithm<EditingInFlatTreeStrategy>(
      c, boundary_crossing_rule);
}

// FIXME: isStartOfParagraph(startOfNextParagraph(pos)) is not always true
VisiblePosition StartOfNextParagraph(const VisiblePosition& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  VisiblePosition paragraph_end(
      EndOfParagraph(visible_position, kCanSkipOverEditingBoundary));
  VisiblePosition after_paragraph_end(
      NextPositionOf(paragraph_end, kCannotCrossEditingBoundary));
  // The position after the last position in the last cell of a table
  // is not the start of the next paragraph.
  if (TableElementJustBefore(after_paragraph_end))
    return NextPositionOf(after_paragraph_end, kCannotCrossEditingBoundary);
  return after_paragraph_end;
}

// FIXME: isStartOfParagraph(startOfNextParagraph(pos)) is not always true
bool InSameParagraph(const VisiblePosition& a,
                     const VisiblePosition& b,
                     EditingBoundaryCrossingRule boundary_crossing_rule) {
  DCHECK(a.IsValid()) << a;
  DCHECK(b.IsValid()) << b;
  return a.IsNotNull() &&
         StartOfParagraph(a, boundary_crossing_rule).DeepEquivalent() ==
             StartOfParagraph(b, boundary_crossing_rule).DeepEquivalent();
}

template <typename Strategy>
static bool IsStartOfParagraphAlgorithm(
    const VisiblePositionTemplate<Strategy>& pos,
    EditingBoundaryCrossingRule boundary_crossing_rule) {
  DCHECK(pos.IsValid()) << pos;
  return pos.IsNotNull() &&
         pos.DeepEquivalent() ==
             StartOfParagraph(pos, boundary_crossing_rule).DeepEquivalent();
}

bool IsStartOfParagraph(const VisiblePosition& pos,
                        EditingBoundaryCrossingRule boundary_crossing_rule) {
  return IsStartOfParagraphAlgorithm<EditingStrategy>(pos,
                                                      boundary_crossing_rule);
}

bool IsStartOfParagraph(const VisiblePositionInFlatTree& pos,
                        EditingBoundaryCrossingRule boundary_crossing_rule) {
  return IsStartOfParagraphAlgorithm<EditingInFlatTreeStrategy>(
      pos, boundary_crossing_rule);
}

template <typename Strategy>
static bool IsEndOfParagraphAlgorithm(
    const VisiblePositionTemplate<Strategy>& pos,
    EditingBoundaryCrossingRule boundary_crossing_rule) {
  DCHECK(pos.IsValid()) << pos;
  return pos.IsNotNull() &&
         pos.DeepEquivalent() ==
             EndOfParagraph(pos, boundary_crossing_rule).DeepEquivalent();
}

bool IsEndOfParagraph(const VisiblePosition& pos,
                      EditingBoundaryCrossingRule boundary_crossing_rule) {
  return IsEndOfParagraphAlgorithm<EditingStrategy>(pos,
                                                    boundary_crossing_rule);
}

bool IsEndOfParagraph(const VisiblePositionInFlatTree& pos,
                      EditingBoundaryCrossingRule boundary_crossing_rule) {
  return IsEndOfParagraphAlgorithm<EditingInFlatTreeStrategy>(
      pos, boundary_crossing_rule);
}

VisiblePosition PreviousParagraphPosition(const VisiblePosition& p,
                                          LayoutUnit x) {
  DCHECK(p.IsValid()) << p;
  VisiblePosition pos = p;
  do {
    VisiblePosition n = PreviousLinePosition(pos, x);
    if (n.IsNull() || n.DeepEquivalent() == pos.DeepEquivalent())
      break;
    pos = n;
  } while (InSameParagraph(p, pos));
  return pos;
}

VisiblePosition NextParagraphPosition(const VisiblePosition& p, LayoutUnit x) {
  DCHECK(p.IsValid()) << p;
  VisiblePosition pos = p;
  do {
    VisiblePosition n = NextLinePosition(pos, x);
    if (n.IsNull() || n.DeepEquivalent() == pos.DeepEquivalent())
      break;
    pos = n;
  } while (InSameParagraph(p, pos));
  return pos;
}

EphemeralRange ExpandToParagraphBoundary(const EphemeralRange& range) {
  const VisiblePosition& start = CreateVisiblePosition(range.StartPosition());
  DCHECK(start.IsNotNull()) << range.StartPosition();
  const Position& paragraph_start = StartOfParagraph(start).DeepEquivalent();
  DCHECK(paragraph_start.IsNotNull()) << range.StartPosition();

  const VisiblePosition& end = CreateVisiblePosition(range.EndPosition());
  DCHECK(end.IsNotNull()) << range.EndPosition();
  const Position& paragraph_end = EndOfParagraph(end).DeepEquivalent();
  DCHECK(paragraph_end.IsNotNull()) << range.EndPosition();

  // TODO(xiaochengh): There are some cases (crbug.com/640112) where we get
  // |paragraphStart > paragraphEnd|, which is the reason we cannot directly
  // return |EphemeralRange(paragraphStart, paragraphEnd)|. This is not
  // desired, though. We should do more investigation to ensure that why
  // |paragraphStart <= paragraphEnd| is violated.
  const Position& result_start =
      paragraph_start.IsNotNull() && paragraph_start <= range.StartPosition()
          ? paragraph_start
          : range.StartPosition();
  const Position& result_end =
      paragraph_end.IsNotNull() && paragraph_end >= range.EndPosition()
          ? paragraph_end
          : range.EndPosition();
  return EphemeralRange(result_start, result_end);
}

// ---------

VisiblePosition StartOfBlock(const VisiblePosition& visible_position,
                             EditingBoundaryCrossingRule rule) {
  DCHECK(visible_position.IsValid()) << visible_position;
  Position position = visible_position.DeepEquivalent();
  Element* start_block =
      position.ComputeContainerNode()
          ? EnclosingBlock(position.ComputeContainerNode(), rule)
          : 0;
  return start_block ? VisiblePosition::FirstPositionInNode(start_block)
                     : VisiblePosition();
}

VisiblePosition EndOfBlock(const VisiblePosition& visible_position,
                           EditingBoundaryCrossingRule rule) {
  DCHECK(visible_position.IsValid()) << visible_position;
  Position position = visible_position.DeepEquivalent();
  Element* end_block =
      position.ComputeContainerNode()
          ? EnclosingBlock(position.ComputeContainerNode(), rule)
          : 0;
  return end_block ? VisiblePosition::LastPositionInNode(end_block)
                   : VisiblePosition();
}

bool InSameBlock(const VisiblePosition& a, const VisiblePosition& b) {
  DCHECK(a.IsValid()) << a;
  DCHECK(b.IsValid()) << b;
  return !a.IsNull() &&
         EnclosingBlock(a.DeepEquivalent().ComputeContainerNode()) ==
             EnclosingBlock(b.DeepEquivalent().ComputeContainerNode());
}

bool IsStartOfBlock(const VisiblePosition& pos) {
  DCHECK(pos.IsValid()) << pos;
  return pos.IsNotNull() &&
         pos.DeepEquivalent() ==
             StartOfBlock(pos, kCanCrossEditingBoundary).DeepEquivalent();
}

bool IsEndOfBlock(const VisiblePosition& pos) {
  DCHECK(pos.IsValid()) << pos;
  return pos.IsNotNull() &&
         pos.DeepEquivalent() ==
             EndOfBlock(pos, kCanCrossEditingBoundary).DeepEquivalent();
}

// ---------

template <typename Strategy>
static VisiblePositionTemplate<Strategy> StartOfDocumentAlgorithm(
    const VisiblePositionTemplate<Strategy>& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  Node* node = visible_position.DeepEquivalent().AnchorNode();
  if (!node || !node->GetDocument().documentElement())
    return VisiblePositionTemplate<Strategy>();

  return CreateVisiblePosition(PositionTemplate<Strategy>::FirstPositionInNode(
      node->GetDocument().documentElement()));
}

VisiblePosition StartOfDocument(const VisiblePosition& c) {
  return StartOfDocumentAlgorithm<EditingStrategy>(c);
}

VisiblePositionInFlatTree StartOfDocument(const VisiblePositionInFlatTree& c) {
  return StartOfDocumentAlgorithm<EditingInFlatTreeStrategy>(c);
}

template <typename Strategy>
static VisiblePositionTemplate<Strategy> EndOfDocumentAlgorithm(
    const VisiblePositionTemplate<Strategy>& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  Node* node = visible_position.DeepEquivalent().AnchorNode();
  if (!node || !node->GetDocument().documentElement())
    return VisiblePositionTemplate<Strategy>();

  Element* doc = node->GetDocument().documentElement();
  return CreateVisiblePosition(
      PositionTemplate<Strategy>::LastPositionInNode(doc));
}

VisiblePosition EndOfDocument(const VisiblePosition& c) {
  return EndOfDocumentAlgorithm<EditingStrategy>(c);
}

VisiblePositionInFlatTree EndOfDocument(const VisiblePositionInFlatTree& c) {
  return EndOfDocumentAlgorithm<EditingInFlatTreeStrategy>(c);
}

bool IsStartOfDocument(const VisiblePosition& p) {
  DCHECK(p.IsValid()) << p;
  return p.IsNotNull() &&
         PreviousPositionOf(p, kCanCrossEditingBoundary).IsNull();
}

bool IsEndOfDocument(const VisiblePosition& p) {
  DCHECK(p.IsValid()) << p;
  return p.IsNotNull() && NextPositionOf(p, kCanCrossEditingBoundary).IsNull();
}

// ---------

VisiblePosition StartOfEditableContent(
    const VisiblePosition& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  ContainerNode* highest_root =
      HighestEditableRoot(visible_position.DeepEquivalent());
  if (!highest_root)
    return VisiblePosition();

  return VisiblePosition::FirstPositionInNode(highest_root);
}

VisiblePosition EndOfEditableContent(const VisiblePosition& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  ContainerNode* highest_root =
      HighestEditableRoot(visible_position.DeepEquivalent());
  if (!highest_root)
    return VisiblePosition();

  return VisiblePosition::LastPositionInNode(highest_root);
}

bool IsEndOfEditableOrNonEditableContent(const VisiblePosition& position) {
  DCHECK(position.IsValid()) << position;
  return position.IsNotNull() && NextPositionOf(position).IsNull();
}

// TODO(yosin) We should rename |isEndOfEditableOrNonEditableContent()| what
// this function does, e.g. |isLastVisiblePositionOrEndOfInnerEditor()|.
bool IsEndOfEditableOrNonEditableContent(
    const VisiblePositionInFlatTree& position) {
  DCHECK(position.IsValid()) << position;
  if (position.IsNull())
    return false;
  const VisiblePositionInFlatTree next_position = NextPositionOf(position);
  if (next_position.IsNull())
    return true;
  // In DOM version, following condition, the last position of inner editor
  // of INPUT/TEXTAREA element, by |nextPosition().isNull()|, because of
  // an inner editor is an only leaf node.
  if (!next_position.DeepEquivalent().IsAfterAnchor())
    return false;
  return IsTextControlElement(next_position.DeepEquivalent().AnchorNode());
}

VisiblePosition LeftBoundaryOfLine(const VisiblePosition& c,
                                   TextDirection direction) {
  DCHECK(c.IsValid()) << c;
  return direction == TextDirection::kLtr ? LogicalStartOfLine(c)
                                          : LogicalEndOfLine(c);
}

VisiblePosition RightBoundaryOfLine(const VisiblePosition& c,
                                    TextDirection direction) {
  DCHECK(c.IsValid()) << c;
  return direction == TextDirection::kLtr ? LogicalEndOfLine(c)
                                          : LogicalStartOfLine(c);
}

static bool IsNonTextLeafChild(LayoutObject* object) {
  if (object->SlowFirstChild())
    return false;
  if (object->IsText())
    return false;
  return true;
}

static InlineTextBox* SearchAheadForBetterMatch(LayoutObject* layout_object) {
  LayoutBlock* container = layout_object->ContainingBlock();
  for (LayoutObject* next = layout_object->NextInPreOrder(container); next;
       next = next->NextInPreOrder(container)) {
    if (next->IsLayoutBlock())
      return 0;
    if (next->IsBR())
      return 0;
    if (IsNonTextLeafChild(next))
      return 0;
    if (next->IsText()) {
      InlineTextBox* match = 0;
      int min_offset = INT_MAX;
      for (InlineTextBox* box = ToLayoutText(next)->FirstTextBox(); box;
           box = box->NextTextBox()) {
        int caret_min_offset = box->CaretMinOffset();
        if (caret_min_offset < min_offset) {
          match = box;
          min_offset = caret_min_offset;
        }
      }
      if (match)
        return match;
    }
  }
  return 0;
}

template <typename Strategy>
PositionTemplate<Strategy> DownstreamIgnoringEditingBoundaries(
    PositionTemplate<Strategy> position) {
  PositionTemplate<Strategy> last_position;
  while (!position.IsEquivalent(last_position)) {
    last_position = position;
    position = MostForwardCaretPosition(position, kCanCrossEditingBoundary);
  }
  return position;
}

template <typename Strategy>
PositionTemplate<Strategy> UpstreamIgnoringEditingBoundaries(
    PositionTemplate<Strategy> position) {
  PositionTemplate<Strategy> last_position;
  while (!position.IsEquivalent(last_position)) {
    last_position = position;
    position = MostBackwardCaretPosition(position, kCanCrossEditingBoundary);
  }
  return position;
}

// Returns true if |inlineBox| starts different direction of embedded text ru.
// See [1] for details.
// [1] UNICODE BIDIRECTIONAL ALGORITHM, http://unicode.org/reports/tr9/
static bool IsStartOfDifferentDirection(const InlineBox* inline_box) {
  InlineBox* prev_box = inline_box->PrevLeafChild();
  if (!prev_box)
    return true;
  if (prev_box->Direction() == inline_box->Direction())
    return true;
  DCHECK_NE(prev_box->BidiLevel(), inline_box->BidiLevel());
  return prev_box->BidiLevel() > inline_box->BidiLevel();
}

template <typename Strategy>
static InlineBoxPosition ComputeInlineBoxPositionTemplate(
    const PositionTemplate<Strategy>& position,
    TextAffinity affinity,
    TextDirection primary_direction) {
  InlineBox* inline_box = nullptr;
  int caret_offset = position.ComputeEditingOffset();
  Node* const anchor_node = position.AnchorNode();
  LayoutObject* layout_object =
      anchor_node->IsShadowRoot()
          ? ToShadowRoot(anchor_node)->host().GetLayoutObject()
          : anchor_node->GetLayoutObject();

  DCHECK(layout_object) << position;

  if (!layout_object->IsText()) {
    inline_box = 0;
    if (CanHaveChildrenForEditing(anchor_node) &&
        layout_object->IsLayoutBlockFlow() &&
        HasRenderedNonAnonymousDescendantsWithHeight(layout_object)) {
      // Try a visually equivalent position with possibly opposite
      // editability. This helps in case |this| is in an editable block
      // but surrounded by non-editable positions. It acts to negate the
      // logic at the beginning of
      // |LayoutObject::createPositionWithAffinity()|.
      PositionTemplate<Strategy> equivalent =
          DownstreamIgnoringEditingBoundaries(position);
      if (equivalent == position) {
        equivalent = UpstreamIgnoringEditingBoundaries(position);
        if (equivalent == position ||
            DownstreamIgnoringEditingBoundaries(equivalent) == position)
          return InlineBoxPosition(inline_box, caret_offset);
      }

      return ComputeInlineBoxPosition(equivalent, TextAffinity::kUpstream,
                                      primary_direction);
    }
    if (layout_object->IsBox()) {
      inline_box = ToLayoutBox(layout_object)->InlineBoxWrapper();
      if (!inline_box || (caret_offset > inline_box->CaretMinOffset() &&
                          caret_offset < inline_box->CaretMaxOffset()))
        return InlineBoxPosition(inline_box, caret_offset);
    }
  } else {
    LayoutText* text_layout_object = ToLayoutText(layout_object);

    InlineTextBox* box;
    InlineTextBox* candidate = 0;

    for (box = text_layout_object->FirstTextBox(); box;
         box = box->NextTextBox()) {
      int caret_min_offset = box->CaretMinOffset();
      int caret_max_offset = box->CaretMaxOffset();

      if (caret_offset < caret_min_offset || caret_offset > caret_max_offset ||
          (caret_offset == caret_max_offset && box->IsLineBreak()))
        continue;

      if (caret_offset > caret_min_offset && caret_offset < caret_max_offset)
        return InlineBoxPosition(box, caret_offset);

      if (((caret_offset == caret_max_offset) ^
           (affinity == TextAffinity::kDownstream)) ||
          ((caret_offset == caret_min_offset) ^
           (affinity == TextAffinity::kUpstream)) ||
          (caret_offset == caret_max_offset && box->NextLeafChild() &&
           box->NextLeafChild()->IsLineBreak()))
        break;

      candidate = box;
    }
    if (candidate && candidate == text_layout_object->LastTextBox() &&
        affinity == TextAffinity::kDownstream) {
      box = SearchAheadForBetterMatch(text_layout_object);
      if (box)
        caret_offset = box->CaretMinOffset();
    }
    inline_box = box ? box : candidate;
  }

  if (!inline_box)
    return InlineBoxPosition(inline_box, caret_offset);

  unsigned char level = inline_box->BidiLevel();

  if (inline_box->Direction() == primary_direction) {
    if (caret_offset == inline_box->CaretRightmostOffset()) {
      InlineBox* next_box = inline_box->NextLeafChild();
      if (!next_box || next_box->BidiLevel() >= level)
        return InlineBoxPosition(inline_box, caret_offset);

      level = next_box->BidiLevel();
      InlineBox* prev_box = inline_box;
      do {
        prev_box = prev_box->PrevLeafChild();
      } while (prev_box && prev_box->BidiLevel() > level);

      // For example, abc FED 123 ^ CBA
      if (prev_box && prev_box->BidiLevel() == level)
        return InlineBoxPosition(inline_box, caret_offset);

      // For example, abc 123 ^ CBA
      while (InlineBox* next_box = inline_box->NextLeafChild()) {
        if (next_box->BidiLevel() < level)
          break;
        inline_box = next_box;
      }
      return InlineBoxPosition(inline_box, inline_box->CaretRightmostOffset());
    }

    if (IsStartOfDifferentDirection(inline_box))
      return InlineBoxPosition(inline_box, caret_offset);

    level = inline_box->PrevLeafChild()->BidiLevel();
    InlineBox* next_box = inline_box;
    do {
      next_box = next_box->NextLeafChild();
    } while (next_box && next_box->BidiLevel() > level);

    if (next_box && next_box->BidiLevel() == level)
      return InlineBoxPosition(inline_box, caret_offset);

    while (InlineBox* prev_box = inline_box->PrevLeafChild()) {
      if (prev_box->BidiLevel() < level)
        break;
      inline_box = prev_box;
    }
    return InlineBoxPosition(inline_box, inline_box->CaretLeftmostOffset());
  }

  if (caret_offset == inline_box->CaretLeftmostOffset()) {
    InlineBox* prev_box = inline_box->PrevLeafChildIgnoringLineBreak();
    if (!prev_box || prev_box->BidiLevel() < level) {
      // Left edge of a secondary run. Set to the right edge of the entire
      // run.
      while (InlineBox* next_box =
                 inline_box->NextLeafChildIgnoringLineBreak()) {
        if (next_box->BidiLevel() < level)
          break;
        inline_box = next_box;
      }
      return InlineBoxPosition(inline_box, inline_box->CaretRightmostOffset());
    }

    if (prev_box->BidiLevel() > level) {
      // Right edge of a "tertiary" run. Set to the left edge of that run.
      while (InlineBox* tertiary_box =
                 inline_box->PrevLeafChildIgnoringLineBreak()) {
        if (tertiary_box->BidiLevel() <= level)
          break;
        inline_box = tertiary_box;
      }
      return InlineBoxPosition(inline_box, inline_box->CaretLeftmostOffset());
    }
    return InlineBoxPosition(inline_box, caret_offset);
  }

  if (layout_object &&
      layout_object->Style()->GetUnicodeBidi() == UnicodeBidi::kPlaintext) {
    if (inline_box->BidiLevel() < level)
      return InlineBoxPosition(inline_box, inline_box->CaretLeftmostOffset());
    return InlineBoxPosition(inline_box, inline_box->CaretRightmostOffset());
  }

  InlineBox* next_box = inline_box->NextLeafChildIgnoringLineBreak();
  if (!next_box || next_box->BidiLevel() < level) {
    // Right edge of a secondary run. Set to the left edge of the entire
    // run.
    while (InlineBox* prev_box = inline_box->PrevLeafChildIgnoringLineBreak()) {
      if (prev_box->BidiLevel() < level)
        break;
      inline_box = prev_box;
    }
    return InlineBoxPosition(inline_box, inline_box->CaretLeftmostOffset());
  }

  if (next_box->BidiLevel() <= level)
    return InlineBoxPosition(inline_box, caret_offset);

  // Left edge of a "tertiary" run. Set to the right edge of that run.
  while (InlineBox* tertiary_box =
             inline_box->NextLeafChildIgnoringLineBreak()) {
    if (tertiary_box->BidiLevel() <= level)
      break;
    inline_box = tertiary_box;
  }
  return InlineBoxPosition(inline_box, inline_box->CaretRightmostOffset());
}

template <typename Strategy>
static InlineBoxPosition ComputeInlineBoxPositionTemplate(
    const PositionTemplate<Strategy>& position,
    TextAffinity affinity) {
  return ComputeInlineBoxPositionTemplate<Strategy>(
      position, affinity, PrimaryDirectionOf(*position.AnchorNode()));
}

InlineBoxPosition ComputeInlineBoxPosition(const Position& position,
                                           TextAffinity affinity) {
  return ComputeInlineBoxPositionTemplate<EditingStrategy>(position, affinity);
}

InlineBoxPosition ComputeInlineBoxPosition(const PositionInFlatTree& position,
                                           TextAffinity affinity) {
  return ComputeInlineBoxPositionTemplate<EditingInFlatTreeStrategy>(position,
                                                                     affinity);
}

InlineBoxPosition ComputeInlineBoxPosition(const VisiblePosition& position) {
  DCHECK(position.IsValid()) << position;
  return ComputeInlineBoxPosition(position.DeepEquivalent(),
                                  position.Affinity());
}

InlineBoxPosition ComputeInlineBoxPosition(const Position& position,
                                           TextAffinity affinity,
                                           TextDirection primary_direction) {
  return ComputeInlineBoxPositionTemplate<EditingStrategy>(position, affinity,
                                                           primary_direction);
}

InlineBoxPosition ComputeInlineBoxPosition(const PositionInFlatTree& position,
                                           TextAffinity affinity,
                                           TextDirection primary_direction) {
  return ComputeInlineBoxPositionTemplate<EditingInFlatTreeStrategy>(
      position, affinity, primary_direction);
}

template <typename Strategy>
LayoutRect LocalCaretRectOfPositionTemplate(
    const PositionWithAffinityTemplate<Strategy>& position,
    LayoutObject*& layout_object) {
  if (position.IsNull()) {
    layout_object = nullptr;
    return LayoutRect();
  }
  Node* node = position.AnchorNode();

  layout_object = node->GetLayoutObject();
  if (!layout_object)
    return LayoutRect();

  InlineBoxPosition box_position =
      ComputeInlineBoxPosition(position.GetPosition(), position.Affinity());

  if (box_position.inline_box)
    layout_object = LineLayoutAPIShim::LayoutObjectFrom(
        box_position.inline_box->GetLineLayoutItem());

  return layout_object->LocalCaretRect(box_position.inline_box,
                                       box_position.offset_in_box);
}

// This function was added because the caret rect that is calculated by
// using the line top value instead of the selection top.
template <typename Strategy>
LayoutRect LocalSelectionRectOfPositionTemplate(
    const PositionWithAffinityTemplate<Strategy>& position,
    LayoutObject*& layout_object) {
  if (position.IsNull()) {
    layout_object = nullptr;
    return LayoutRect();
  }
  Node* node = position.AnchorNode();
  layout_object = node->GetLayoutObject();
  if (!layout_object)
    return LayoutRect();

  InlineBoxPosition box_position =
      ComputeInlineBoxPosition(position.GetPosition(), position.Affinity());

  if (!box_position.inline_box)
    return LayoutRect();

  layout_object = LineLayoutAPIShim::LayoutObjectFrom(
      box_position.inline_box->GetLineLayoutItem());

  LayoutRect rect = layout_object->LocalCaretRect(box_position.inline_box,
                                                  box_position.offset_in_box);

  if (rect.IsEmpty())
    return rect;

  InlineBox* const box = box_position.inline_box;
  if (layout_object->Style()->IsHorizontalWritingMode()) {
    rect.SetY(box->Root().SelectionTop());
    rect.SetHeight(box->Root().SelectionHeight());
    return rect;
  }

  rect.SetX(box->Root().SelectionTop());
  rect.SetWidth(box->Root().SelectionHeight());
  return rect;
}

LayoutRect LocalCaretRectOfPosition(const PositionWithAffinity& position,
                                    LayoutObject*& layout_object) {
  return LocalCaretRectOfPositionTemplate<EditingStrategy>(position,
                                                           layout_object);
}

LayoutRect LocalSelectionRectOfPosition(const PositionWithAffinity& position,
                                        LayoutObject*& layout_object) {
  return LocalSelectionRectOfPositionTemplate<EditingStrategy>(position,
                                                               layout_object);
}

LayoutRect LocalCaretRectOfPosition(
    const PositionInFlatTreeWithAffinity& position,
    LayoutObject*& layout_object) {
  return LocalCaretRectOfPositionTemplate<EditingInFlatTreeStrategy>(
      position, layout_object);
}

static LayoutUnit BoundingBoxLogicalHeight(LayoutObject* o,
                                           const LayoutRect& rect) {
  return o->Style()->IsHorizontalWritingMode() ? rect.Height() : rect.Width();
}

bool HasRenderedNonAnonymousDescendantsWithHeight(LayoutObject* layout_object) {
  LayoutObject* stop = layout_object->NextInPreOrderAfterChildren();
  for (LayoutObject* o = layout_object->SlowFirstChild(); o && o != stop;
       o = o->NextInPreOrder()) {
    if (o->NonPseudoNode()) {
      if ((o->IsText() &&
           BoundingBoxLogicalHeight(o, ToLayoutText(o)->LinesBoundingBox())) ||
          (o->IsBox() && ToLayoutBox(o)->PixelSnappedLogicalHeight()) ||
          (o->IsLayoutInline() && IsEmptyInline(LineLayoutItem(o)) &&
           BoundingBoxLogicalHeight(o, ToLayoutInline(o)->LinesBoundingBox())))
        return true;
    }
  }
  return false;
}

VisiblePosition VisiblePositionForContentsPoint(const IntPoint& contents_point,
                                                LocalFrame* frame) {
  HitTestRequest request = HitTestRequest::kMove | HitTestRequest::kReadOnly |
                           HitTestRequest::kActive |
                           HitTestRequest::kIgnoreClipping;
  HitTestResult result(request, contents_point);
  frame->GetDocument()->GetLayoutViewItem().HitTest(result);

  if (Node* node = result.InnerNode()) {
    return CreateVisiblePosition(PositionRespectingEditingBoundary(
        frame->Selection().ComputeVisibleSelectionInDOMTreeDeprecated().Start(),
        result.LocalPoint(), node));
  }
  return VisiblePosition();
}

// TODO(yosin): We should use |associatedLayoutObjectOf()| in "VisibleUnits.cpp"
// where it takes |LayoutObject| from |Position|.
// Note about ::first-letter pseudo-element:
//   When an element has ::first-letter pseudo-element, first letter characters
//   are taken from |Text| node and first letter characters are considered
//   as content of <pseudo:first-letter>.
//   For following HTML,
//      <style>div::first-letter {color: red}</style>
//      <div>abc</div>
//   we have following layout tree:
//      LayoutBlockFlow {DIV} at (0,0) size 784x55
//        LayoutInline {<pseudo:first-letter>} at (0,0) size 22x53
//          LayoutTextFragment (anonymous) at (0,1) size 22x53
//            text run at (0,1) width 22: "a"
//        LayoutTextFragment {#text} at (21,30) size 16x17
//          text run at (21,30) width 16: "bc"
//  In this case, |Text::layoutObject()| for "abc" returns |LayoutTextFragment|
//  containing "bc", and it is called remaining part.
//
//  Even if |Text| node contains only first-letter characters, e.g. just "a",
//  remaining part of |LayoutTextFragment|, with |fragmentLength()| == 0, is
//  appeared in layout tree.
//
//  When |Text| node contains only first-letter characters and whitespaces, e.g.
//  "B\n", associated |LayoutTextFragment| is first-letter part instead of
//  remaining part.
//
//  Punctuation characters are considered as first-letter. For "(1)ab",
//  "(1)" are first-letter part and "ab" are remaining part.
LayoutObject* AssociatedLayoutObjectOf(const Node& node, int offset_in_node) {
  DCHECK_GE(offset_in_node, 0);
  LayoutObject* layout_object = node.GetLayoutObject();
  if (!node.IsTextNode() || !layout_object ||
      !ToLayoutText(layout_object)->IsTextFragment())
    return layout_object;
  LayoutTextFragment* layout_text_fragment =
      ToLayoutTextFragment(layout_object);
  if (!layout_text_fragment->IsRemainingTextLayoutObject()) {
    DCHECK_LE(
        static_cast<unsigned>(offset_in_node),
        layout_text_fragment->Start() + layout_text_fragment->FragmentLength());
    return layout_text_fragment;
  }
  if (layout_text_fragment->FragmentLength() &&
      static_cast<unsigned>(offset_in_node) >= layout_text_fragment->Start())
    return layout_object;
  LayoutObject* first_letter_layout_object =
      layout_text_fragment->GetFirstLetterPseudoElement()->GetLayoutObject();
  // TODO(yosin): We're not sure when |firstLetterLayoutObject| has
  // multiple child layout object.
  LayoutObject* child = first_letter_layout_object->SlowFirstChild();
  CHECK(child && child->IsText());
  DCHECK_EQ(child, first_letter_layout_object->SlowLastChild());
  return child;
}

int CaretMinOffset(const Node* node) {
  LayoutObject* layout_object = AssociatedLayoutObjectOf(*node, 0);
  return layout_object ? layout_object->CaretMinOffset() : 0;
}

int CaretMaxOffset(const Node* n) {
  return EditingStrategy::CaretMaxOffset(*n);
}

template <typename Strategy>
static bool InRenderedText(const PositionTemplate<Strategy>& position) {
  Node* const anchor_node = position.AnchorNode();
  if (!anchor_node || !anchor_node->IsTextNode())
    return false;

  const int offset_in_node = position.ComputeEditingOffset();
  LayoutObject* layout_object =
      AssociatedLayoutObjectOf(*anchor_node, offset_in_node);
  if (!layout_object)
    return false;

  LayoutText* text_layout_object = ToLayoutText(layout_object);
  const int text_offset =
      offset_in_node - text_layout_object->TextStartOffset();
  for (InlineTextBox* box = text_layout_object->FirstTextBox(); box;
       box = box->NextTextBox()) {
    if (text_offset < static_cast<int>(box->Start()) &&
        !text_layout_object->ContainsReversedText()) {
      // The offset we're looking for is before this node
      // this means the offset must be in content that is
      // not laid out. Return false.
      return false;
    }
    if (box->ContainsCaretOffset(text_offset)) {
      // Return false for offsets inside composed characters.
      return text_offset == text_layout_object->CaretMinOffset() ||
             text_offset == NextGraphemeBoundaryOf(
                                anchor_node, PreviousGraphemeBoundaryOf(
                                                 anchor_node, text_offset));
    }
  }

  return false;
}

bool RendersInDifferentPosition(const Position& position1,
                                const Position& position2) {
  if (position1.IsNull() || position2.IsNull())
    return false;
  LayoutObject* layout_object1;
  const LayoutRect& rect1 =
      LocalCaretRectOfPosition(PositionWithAffinity(position1), layout_object1);
  LayoutObject* layout_object2;
  const LayoutRect& rect2 =
      LocalCaretRectOfPosition(PositionWithAffinity(position2), layout_object2);
  if (!layout_object1 || !layout_object2)
    return layout_object1 != layout_object2;
  return layout_object1->LocalToAbsoluteQuad(FloatRect(rect1)) !=
         layout_object2->LocalToAbsoluteQuad(FloatRect(rect2));
}

static bool IsVisuallyEmpty(const LayoutObject* layout) {
  for (LayoutObject* child = layout->SlowFirstChild(); child;
       child = child->NextSibling()) {
    // TODO(xiaochengh): Replace type-based conditioning by virtual function.
    if (child->IsBox()) {
      if (!ToLayoutBox(child)->Size().IsEmpty())
        return false;
    } else if (child->IsLayoutInline()) {
      if (ToLayoutInline(child)->FirstLineBoxIncludingCulling())
        return false;
    } else if (child->IsText()) {
      if (ToLayoutText(child)->HasTextBoxes())
        return false;
    } else {
      return false;
    }
  }
  return true;
}

// FIXME: Share code with isCandidate, if possible.
bool EndsOfNodeAreVisuallyDistinctPositions(const Node* node) {
  if (!node || !node->GetLayoutObject())
    return false;

  if (!node->GetLayoutObject()->IsInline())
    return true;

  // Don't include inline tables.
  if (isHTMLTableElement(*node))
    return false;

  // A Marquee elements are moving so we should assume their ends are always
  // visibily distinct.
  if (isHTMLMarqueeElement(*node))
    return true;

  // There is a VisiblePosition inside an empty inline-block container.
  return node->GetLayoutObject()->IsAtomicInlineLevel() &&
         CanHaveChildrenForEditing(node) &&
         !ToLayoutBox(node->GetLayoutObject())->Size().IsEmpty() &&
         IsVisuallyEmpty(node->GetLayoutObject());
}

template <typename Strategy>
static Node* EnclosingVisualBoundary(Node* node) {
  while (node && !EndsOfNodeAreVisuallyDistinctPositions(node))
    node = Strategy::Parent(*node);

  return node;
}

// upstream() and downstream() want to return positions that are either in a
// text node or at just before a non-text node.  This method checks for that.
template <typename Strategy>
static bool IsStreamer(const PositionIteratorAlgorithm<Strategy>& pos) {
  if (!pos.GetNode())
    return true;

  if (IsAtomicNode(pos.GetNode()))
    return true;

  return pos.AtStartOfNode();
}

template <typename Strategy>
static PositionTemplate<Strategy> AdjustPositionForBackwardIteration(
    const PositionTemplate<Strategy>& position) {
  DCHECK(!position.IsNull());
  if (!position.IsAfterAnchor())
    return position;
  if (IsUserSelectContain(*position.AnchorNode()))
    return position.ToOffsetInAnchor();
  return PositionTemplate<Strategy>::EditingPositionOf(
      position.AnchorNode(), Strategy::CaretMaxOffset(*position.AnchorNode()));
}

template <typename Strategy>
static PositionTemplate<Strategy> MostBackwardCaretPosition(
    const PositionTemplate<Strategy>& position,
    EditingBoundaryCrossingRule rule) {
  DCHECK(!NeedsLayoutTreeUpdate(position)) << position;
  TRACE_EVENT0("input", "VisibleUnits::mostBackwardCaretPosition");

  Node* start_node = position.AnchorNode();
  if (!start_node)
    return PositionTemplate<Strategy>();

  // iterate backward from there, looking for a qualified position
  Node* boundary = EnclosingVisualBoundary<Strategy>(start_node);
  // FIXME: PositionIterator should respect Before and After positions.
  PositionIteratorAlgorithm<Strategy> last_visible(
      AdjustPositionForBackwardIteration<Strategy>(position));
  PositionIteratorAlgorithm<Strategy> current_pos = last_visible;
  bool start_editable = HasEditableStyle(*start_node);
  Node* last_node = start_node;
  bool boundary_crossed = false;
  for (; !current_pos.AtStart(); current_pos.Decrement()) {
    Node* current_node = current_pos.GetNode();
    // Don't check for an editability change if we haven't moved to a different
    // node, to avoid the expense of computing hasEditableStyle().
    if (current_node != last_node) {
      // Don't change editability.
      bool current_editable = HasEditableStyle(*current_node);
      if (start_editable != current_editable) {
        if (rule == kCannotCrossEditingBoundary)
          break;
        boundary_crossed = true;
      }
      last_node = current_node;
    }

    // If we've moved to a position that is visually distinct, return the last
    // saved position. There is code below that terminates early if we're
    // *about* to move to a visually distinct position.
    if (EndsOfNodeAreVisuallyDistinctPositions(current_node) &&
        current_node != boundary)
      return last_visible.DeprecatedComputePosition();

    // skip position in non-laid out or invisible node
    LayoutObject* layout_object =
        AssociatedLayoutObjectOf(*current_node, current_pos.OffsetInLeafNode());
    if (!layout_object ||
        layout_object->Style()->Visibility() != EVisibility::kVisible)
      continue;

    if (rule == kCanCrossEditingBoundary && boundary_crossed) {
      last_visible = current_pos;
      break;
    }

    // track last visible streamer position
    if (IsStreamer<Strategy>(current_pos))
      last_visible = current_pos;

    // Don't move past a position that is visually distinct.  We could rely on
    // code above to terminate and return lastVisible on the next iteration, but
    // we terminate early to avoid doing a nodeIndex() call.
    if (EndsOfNodeAreVisuallyDistinctPositions(current_node) &&
        current_pos.AtStartOfNode())
      return last_visible.DeprecatedComputePosition();

    // Return position after tables and nodes which have content that can be
    // ignored.
    if (EditingIgnoresContent(*current_node) ||
        IsDisplayInsideTable(current_node)) {
      if (current_pos.AtEndOfNode())
        return PositionTemplate<Strategy>::AfterNode(current_node);
      continue;
    }

    // return current position if it is in laid out text
    if (layout_object->IsText() &&
        ToLayoutText(layout_object)->FirstTextBox()) {
      LayoutText* const text_layout_object = ToLayoutText(layout_object);
      const unsigned text_start_offset = text_layout_object->TextStartOffset();
      if (current_node != start_node) {
        // This assertion fires in layout tests in the case-transform.html test
        // because of a mix-up between offsets in the text in the DOM tree with
        // text in the layout tree which can have a different length due to case
        // transformation.
        // Until we resolve that, disable this so we can run the layout tests!
        // DCHECK_GE(currentOffset, layoutObject->caretMaxOffset());
        return PositionTemplate<Strategy>(
            current_node, layout_object->CaretMaxOffset() + text_start_offset);
      }

      // Map offset in DOM node to offset in InlineBox.
      DCHECK_GE(current_pos.OffsetInLeafNode(),
                static_cast<int>(text_start_offset));
      const unsigned text_offset =
          current_pos.OffsetInLeafNode() - text_start_offset;
      InlineTextBox* last_text_box = text_layout_object->LastTextBox();
      for (InlineTextBox* box = text_layout_object->FirstTextBox(); box;
           box = box->NextTextBox()) {
        if (text_offset == box->Start()) {
          if (text_layout_object->IsTextFragment() &&
              ToLayoutTextFragment(layout_object)
                  ->IsRemainingTextLayoutObject()) {
            // |currentPos| is at start of remaining text of
            // |Text| node with :first-letter.
            DCHECK_GE(current_pos.OffsetInLeafNode(), 1);
            LayoutObject* first_letter_layout_object =
                ToLayoutTextFragment(layout_object)
                    ->GetFirstLetterPseudoElement()
                    ->GetLayoutObject();
            if (first_letter_layout_object &&
                first_letter_layout_object->Style()->Visibility() ==
                    EVisibility::kVisible)
              return current_pos.ComputePosition();
          }
          continue;
        }
        if (text_offset <= box->Start() + box->Len()) {
          if (text_offset > box->Start())
            return current_pos.ComputePosition();
          continue;
        }

        if (box == last_text_box ||
            text_offset != box->Start() + box->Len() + 1)
          continue;

        // The text continues on the next line only if the last text box is not
        // on this line and none of the boxes on this line have a larger start
        // offset.

        bool continues_on_next_line = true;
        InlineBox* other_box = box;
        while (continues_on_next_line) {
          other_box = other_box->NextLeafChild();
          if (!other_box)
            break;
          if (other_box == last_text_box ||
              (LineLayoutAPIShim::LayoutObjectFrom(
                   other_box->GetLineLayoutItem()) == text_layout_object &&
               ToInlineTextBox(other_box)->Start() > text_offset))
            continues_on_next_line = false;
        }

        other_box = box;
        while (continues_on_next_line) {
          other_box = other_box->PrevLeafChild();
          if (!other_box)
            break;
          if (other_box == last_text_box ||
              (LineLayoutAPIShim::LayoutObjectFrom(
                   other_box->GetLineLayoutItem()) == text_layout_object &&
               ToInlineTextBox(other_box)->Start() > text_offset))
            continues_on_next_line = false;
        }

        if (continues_on_next_line)
          return current_pos.ComputePosition();
      }
    }
  }
  return last_visible.DeprecatedComputePosition();
}

Position MostBackwardCaretPosition(const Position& position,
                                   EditingBoundaryCrossingRule rule) {
  return MostBackwardCaretPosition<EditingStrategy>(position, rule);
}

PositionInFlatTree MostBackwardCaretPosition(const PositionInFlatTree& position,
                                             EditingBoundaryCrossingRule rule) {
  return MostBackwardCaretPosition<EditingInFlatTreeStrategy>(position, rule);
}

template <typename Strategy>
PositionTemplate<Strategy> MostForwardCaretPosition(
    const PositionTemplate<Strategy>& position,
    EditingBoundaryCrossingRule rule) {
  DCHECK(!NeedsLayoutTreeUpdate(position)) << position;
  TRACE_EVENT0("input", "VisibleUnits::mostForwardCaretPosition");

  Node* start_node = position.AnchorNode();
  if (!start_node)
    return PositionTemplate<Strategy>();

  // iterate forward from there, looking for a qualified position
  Node* boundary = EnclosingVisualBoundary<Strategy>(start_node);
  // FIXME: PositionIterator should respect Before and After positions.
  PositionIteratorAlgorithm<Strategy> last_visible(
      position.IsAfterAnchor()
          ? PositionTemplate<Strategy>::EditingPositionOf(
                position.AnchorNode(),
                Strategy::CaretMaxOffset(*position.AnchorNode()))
          : position);
  PositionIteratorAlgorithm<Strategy> current_pos = last_visible;
  bool start_editable = HasEditableStyle(*start_node);
  Node* last_node = start_node;
  bool boundary_crossed = false;
  for (; !current_pos.AtEnd(); current_pos.Increment()) {
    Node* current_node = current_pos.GetNode();
    // Don't check for an editability change if we haven't moved to a different
    // node, to avoid the expense of computing hasEditableStyle().
    if (current_node != last_node) {
      // Don't change editability.
      bool current_editable = HasEditableStyle(*current_node);
      if (start_editable != current_editable) {
        if (rule == kCannotCrossEditingBoundary)
          break;
        boundary_crossed = true;
      }

      last_node = current_node;
    }

    // stop before going above the body, up into the head
    // return the last visible streamer position
    if (isHTMLBodyElement(*current_node) && current_pos.AtEndOfNode())
      break;

    // Do not move to a visually distinct position.
    if (EndsOfNodeAreVisuallyDistinctPositions(current_node) &&
        current_node != boundary)
      return last_visible.DeprecatedComputePosition();
    // Do not move past a visually disinct position.
    // Note: The first position after the last in a node whose ends are visually
    // distinct positions will be [boundary->parentNode(),
    // originalBlock->nodeIndex() + 1].
    if (boundary && Strategy::Parent(*boundary) == current_node)
      return last_visible.DeprecatedComputePosition();

    // skip position in non-laid out or invisible node
    LayoutObject* layout_object =
        AssociatedLayoutObjectOf(*current_node, current_pos.OffsetInLeafNode());
    if (!layout_object ||
        layout_object->Style()->Visibility() != EVisibility::kVisible)
      continue;

    if (rule == kCanCrossEditingBoundary && boundary_crossed) {
      last_visible = current_pos;
      break;
    }

    // track last visible streamer position
    if (IsStreamer<Strategy>(current_pos))
      last_visible = current_pos;

    // Return position before tables and nodes which have content that can be
    // ignored.
    if (EditingIgnoresContent(*current_node) ||
        IsDisplayInsideTable(current_node)) {
      if (current_pos.OffsetInLeafNode() <= layout_object->CaretMinOffset())
        return PositionTemplate<Strategy>::EditingPositionOf(
            current_node, layout_object->CaretMinOffset());
      continue;
    }

    // return current position if it is in laid out text
    if (layout_object->IsText() &&
        ToLayoutText(layout_object)->FirstTextBox()) {
      LayoutText* const text_layout_object = ToLayoutText(layout_object);
      const unsigned text_start_offset = text_layout_object->TextStartOffset();
      if (current_node != start_node) {
        DCHECK(current_pos.AtStartOfNode());
        return PositionTemplate<Strategy>(
            current_node, layout_object->CaretMinOffset() + text_start_offset);
      }

      // Map offset in DOM node to offset in InlineBox.
      DCHECK_GE(current_pos.OffsetInLeafNode(),
                static_cast<int>(text_start_offset));
      const unsigned text_offset =
          current_pos.OffsetInLeafNode() - text_start_offset;
      InlineTextBox* last_text_box = text_layout_object->LastTextBox();
      for (InlineTextBox* box = text_layout_object->FirstTextBox(); box;
           box = box->NextTextBox()) {
        if (text_offset <= box->end()) {
          if (text_offset >= box->Start())
            return current_pos.ComputePosition();
          continue;
        }

        if (box == last_text_box || text_offset != box->Start() + box->Len())
          continue;

        // The text continues on the next line only if the last text box is not
        // on this line and none of the boxes on this line have a larger start
        // offset.

        bool continues_on_next_line = true;
        InlineBox* other_box = box;
        while (continues_on_next_line) {
          other_box = other_box->NextLeafChild();
          if (!other_box)
            break;
          if (other_box == last_text_box ||
              (LineLayoutAPIShim::LayoutObjectFrom(
                   other_box->GetLineLayoutItem()) == text_layout_object &&
               ToInlineTextBox(other_box)->Start() >= text_offset))
            continues_on_next_line = false;
        }

        other_box = box;
        while (continues_on_next_line) {
          other_box = other_box->PrevLeafChild();
          if (!other_box)
            break;
          if (other_box == last_text_box ||
              (LineLayoutAPIShim::LayoutObjectFrom(
                   other_box->GetLineLayoutItem()) == text_layout_object &&
               ToInlineTextBox(other_box)->Start() >= text_offset))
            continues_on_next_line = false;
        }

        if (continues_on_next_line)
          return current_pos.ComputePosition();
      }
    }
  }

  return last_visible.DeprecatedComputePosition();
}

Position MostForwardCaretPosition(const Position& position,
                                  EditingBoundaryCrossingRule rule) {
  return MostForwardCaretPosition<EditingStrategy>(position, rule);
}

PositionInFlatTree MostForwardCaretPosition(const PositionInFlatTree& position,
                                            EditingBoundaryCrossingRule rule) {
  return MostForwardCaretPosition<EditingInFlatTreeStrategy>(position, rule);
}

// Returns true if the visually equivalent positions around have different
// editability. A position is considered at editing boundary if one of the
// following is true:
// 1. It is the first position in the node and the next visually equivalent
//    position is non editable.
// 2. It is the last position in the node and the previous visually equivalent
//    position is non editable.
// 3. It is an editable position and both the next and previous visually
//    equivalent positions are both non editable.
template <typename Strategy>
static bool AtEditingBoundary(const PositionTemplate<Strategy> positions) {
  PositionTemplate<Strategy> next_position =
      MostForwardCaretPosition(positions, kCanCrossEditingBoundary);
  if (positions.AtFirstEditingPositionForNode() && next_position.IsNotNull() &&
      !HasEditableStyle(*next_position.AnchorNode()))
    return true;

  PositionTemplate<Strategy> prev_position =
      MostBackwardCaretPosition(positions, kCanCrossEditingBoundary);
  if (positions.AtLastEditingPositionForNode() && prev_position.IsNotNull() &&
      !HasEditableStyle(*prev_position.AnchorNode()))
    return true;

  return next_position.IsNotNull() &&
         !HasEditableStyle(*next_position.AnchorNode()) &&
         prev_position.IsNotNull() &&
         !HasEditableStyle(*prev_position.AnchorNode());
}

template <typename Strategy>
static bool IsVisuallyEquivalentCandidateAlgorithm(
    const PositionTemplate<Strategy>& position) {
  Node* const anchor_node = position.AnchorNode();
  if (!anchor_node)
    return false;

  LayoutObject* layout_object = anchor_node->GetLayoutObject();
  if (!layout_object)
    return false;

  if (layout_object->Style()->Visibility() != EVisibility::kVisible)
    return false;

  if (layout_object->IsBR()) {
    // TODO(leviw) The condition should be
    // m_anchorType == PositionAnchorType::BeforeAnchor, but for now we
    // still need to support legacy positions.
    if (position.IsAfterAnchor())
      return false;
    if (position.ComputeEditingOffset())
      return false;
    const Node* parent = Strategy::Parent(*anchor_node);
    return parent->GetLayoutObject() &&
           parent->GetLayoutObject()->IsSelectable();
  }

  if (layout_object->IsText())
    return layout_object->IsSelectable() && InRenderedText(position);

  if (layout_object->IsSVG()) {
    // We don't consider SVG elements are contenteditable except for
    // associated |layoutObject| returns |isText()| true,
    // e.g. |LayoutSVGInlineText|.
    return false;
  }

  if (IsDisplayInsideTable(anchor_node) ||
      EditingIgnoresContent(*anchor_node)) {
    if (!position.AtFirstEditingPositionForNode() &&
        !position.AtLastEditingPositionForNode())
      return false;
    const Node* parent = Strategy::Parent(*anchor_node);
    return parent->GetLayoutObject() &&
           parent->GetLayoutObject()->IsSelectable();
  }

  if (anchor_node->GetDocument().documentElement() == anchor_node ||
      anchor_node->IsDocumentNode())
    return false;

  if (!layout_object->IsSelectable())
    return false;

  if (layout_object->IsLayoutBlockFlow() || layout_object->IsFlexibleBox() ||
      layout_object->IsLayoutGrid()) {
    if (ToLayoutBlock(layout_object)->LogicalHeight() ||
        isHTMLBodyElement(*anchor_node)) {
      if (!HasRenderedNonAnonymousDescendantsWithHeight(layout_object))
        return position.AtFirstEditingPositionForNode();
      return HasEditableStyle(*anchor_node) && AtEditingBoundary(position);
    }
  } else {
    return HasEditableStyle(*anchor_node) && AtEditingBoundary(position);
  }

  return false;
}

bool IsVisuallyEquivalentCandidate(const Position& position) {
  return IsVisuallyEquivalentCandidateAlgorithm<EditingStrategy>(position);
}

bool IsVisuallyEquivalentCandidate(const PositionInFlatTree& position) {
  return IsVisuallyEquivalentCandidateAlgorithm<EditingInFlatTreeStrategy>(
      position);
}

template <typename Strategy>
static IntRect AbsoluteCaretBoundsOfAlgorithm(
    const VisiblePositionTemplate<Strategy>& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  LayoutObject* layout_object;
  LayoutRect local_rect = LocalCaretRectOfPosition(
      visible_position.ToPositionWithAffinity(), layout_object);
  if (local_rect.IsEmpty() || !layout_object)
    return IntRect();

  return layout_object->LocalToAbsoluteQuad(FloatRect(local_rect))
      .EnclosingBoundingBox();
}

IntRect AbsoluteCaretBoundsOf(const VisiblePosition& visible_position) {
  return AbsoluteCaretBoundsOfAlgorithm<EditingStrategy>(visible_position);
}

template <typename Strategy>
static IntRect AbsoluteSelectionBoundsOfAlgorithm(
    const VisiblePositionTemplate<Strategy>& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  LayoutObject* layout_object;
  LayoutRect local_rect = LocalSelectionRectOfPosition(
      visible_position.ToPositionWithAffinity(), layout_object);
  if (local_rect.IsEmpty() || !layout_object)
    return IntRect();

  return layout_object->LocalToAbsoluteQuad(FloatRect(local_rect))
      .EnclosingBoundingBox();
}

IntRect AbsoluteSelectionBoundsOf(const VisiblePosition& visible_position) {
  return AbsoluteSelectionBoundsOfAlgorithm<EditingStrategy>(visible_position);
}

IntRect AbsoluteCaretBoundsOf(
    const VisiblePositionInFlatTree& visible_position) {
  return AbsoluteCaretBoundsOfAlgorithm<EditingInFlatTreeStrategy>(
      visible_position);
}

template <typename Strategy>
static VisiblePositionTemplate<Strategy> SkipToEndOfEditingBoundary(
    const VisiblePositionTemplate<Strategy>& pos,
    const PositionTemplate<Strategy>& anchor) {
  DCHECK(pos.IsValid()) << pos;
  if (pos.IsNull())
    return pos;

  ContainerNode* highest_root = HighestEditableRoot(anchor);
  ContainerNode* highest_root_of_pos =
      HighestEditableRoot(pos.DeepEquivalent());

  // Return |pos| itself if the two are from the very same editable region,
  // or both are non-editable.
  if (highest_root_of_pos == highest_root)
    return pos;

  // If this is not editable but |pos| has an editable root, skip to the end
  if (!highest_root && highest_root_of_pos)
    return CreateVisiblePosition(
        PositionTemplate<Strategy>(highest_root_of_pos,
                                   PositionAnchorType::kAfterAnchor)
            .ParentAnchoredEquivalent());

  // That must mean that |pos| is not editable. Return the next position after
  // |pos| that is in the same editable region as this position
  DCHECK(highest_root);
  return FirstEditableVisiblePositionAfterPositionInRoot(pos.DeepEquivalent(),
                                                         *highest_root);
}

template <typename Strategy>
static UChar32 CharacterAfterAlgorithm(
    const VisiblePositionTemplate<Strategy>& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  // We canonicalize to the first of two equivalent candidates, but the second
  // of the two candidates is the one that will be inside the text node
  // containing the character after this visible position.
  const PositionTemplate<Strategy> pos =
      MostForwardCaretPosition(visible_position.DeepEquivalent());
  if (!pos.IsOffsetInAnchor())
    return 0;
  Node* container_node = pos.ComputeContainerNode();
  if (!container_node || !container_node->IsTextNode())
    return 0;
  unsigned offset = static_cast<unsigned>(pos.OffsetInContainerNode());
  Text* text_node = ToText(container_node);
  unsigned length = text_node->length();
  if (offset >= length)
    return 0;

  return text_node->data().CharacterStartingAt(offset);
}

UChar32 CharacterAfter(const VisiblePosition& visible_position) {
  return CharacterAfterAlgorithm<EditingStrategy>(visible_position);
}

UChar32 CharacterAfter(const VisiblePositionInFlatTree& visible_position) {
  return CharacterAfterAlgorithm<EditingInFlatTreeStrategy>(visible_position);
}

template <typename Strategy>
static UChar32 CharacterBeforeAlgorithm(
    const VisiblePositionTemplate<Strategy>& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  return CharacterAfter(PreviousPositionOf(visible_position));
}

UChar32 CharacterBefore(const VisiblePosition& visible_position) {
  return CharacterBeforeAlgorithm<EditingStrategy>(visible_position);
}

UChar32 CharacterBefore(const VisiblePositionInFlatTree& visible_position) {
  return CharacterBeforeAlgorithm<EditingInFlatTreeStrategy>(visible_position);
}

template <typename Strategy>
static PositionTemplate<Strategy> LeftVisuallyDistinctCandidate(
    const VisiblePositionTemplate<Strategy>& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  const PositionTemplate<Strategy> deep_position =
      visible_position.DeepEquivalent();
  PositionTemplate<Strategy> p = deep_position;

  if (p.IsNull())
    return PositionTemplate<Strategy>();

  const PositionTemplate<Strategy> downstream_start =
      MostForwardCaretPosition(p);
  TextDirection primary_direction = PrimaryDirectionOf(*p.AnchorNode());
  const TextAffinity affinity = visible_position.Affinity();

  while (true) {
    InlineBoxPosition box_position =
        ComputeInlineBoxPosition(p, affinity, primary_direction);
    InlineBox* box = box_position.inline_box;
    int offset = box_position.offset_in_box;
    if (!box)
      return primary_direction == TextDirection::kLtr
                 ? PreviousVisuallyDistinctCandidate(deep_position)
                 : NextVisuallyDistinctCandidate(deep_position);

    LineLayoutItem line_layout_item = box->GetLineLayoutItem();

    while (true) {
      if ((line_layout_item.IsAtomicInlineLevel() || line_layout_item.IsBR()) &&
          offset == box->CaretRightmostOffset())
        return box->IsLeftToRightDirection()
                   ? PreviousVisuallyDistinctCandidate(deep_position)
                   : NextVisuallyDistinctCandidate(deep_position);

      if (!line_layout_item.GetNode()) {
        box = box->PrevLeafChild();
        if (!box)
          return primary_direction == TextDirection::kLtr
                     ? PreviousVisuallyDistinctCandidate(deep_position)
                     : NextVisuallyDistinctCandidate(deep_position);
        line_layout_item = box->GetLineLayoutItem();
        offset = box->CaretRightmostOffset();
        continue;
      }

      offset =
          box->IsLeftToRightDirection()
              ? PreviousGraphemeBoundaryOf(line_layout_item.GetNode(), offset)
              : NextGraphemeBoundaryOf(line_layout_item.GetNode(), offset);

      int caret_min_offset = box->CaretMinOffset();
      int caret_max_offset = box->CaretMaxOffset();

      if (offset > caret_min_offset && offset < caret_max_offset)
        break;

      if (box->IsLeftToRightDirection() ? offset < caret_min_offset
                                        : offset > caret_max_offset) {
        // Overshot to the left.
        InlineBox* prev_box = box->PrevLeafChildIgnoringLineBreak();
        if (!prev_box) {
          PositionTemplate<Strategy> position_on_left =
              primary_direction == TextDirection::kLtr
                  ? PreviousVisuallyDistinctCandidate(
                        visible_position.DeepEquivalent())
                  : NextVisuallyDistinctCandidate(
                        visible_position.DeepEquivalent());
          if (position_on_left.IsNull())
            return PositionTemplate<Strategy>();

          InlineBox* box_on_left =
              ComputeInlineBoxPosition(position_on_left, affinity,
                                       primary_direction)
                  .inline_box;
          if (box_on_left && box_on_left->Root() == box->Root())
            return PositionTemplate<Strategy>();
          return position_on_left;
        }

        // Reposition at the other logical position corresponding to our
        // edge's visual position and go for another round.
        box = prev_box;
        line_layout_item = box->GetLineLayoutItem();
        offset = prev_box->CaretRightmostOffset();
        continue;
      }

      DCHECK_EQ(offset, box->CaretLeftmostOffset());

      unsigned char level = box->BidiLevel();
      InlineBox* prev_box = box->PrevLeafChild();

      if (box->Direction() == primary_direction) {
        if (!prev_box) {
          InlineBox* logical_start = 0;
          if (primary_direction == TextDirection::kLtr
                  ? box->Root().GetLogicalStartBoxWithNode(logical_start)
                  : box->Root().GetLogicalEndBoxWithNode(logical_start)) {
            box = logical_start;
            line_layout_item = box->GetLineLayoutItem();
            offset = primary_direction == TextDirection::kLtr
                         ? box->CaretMinOffset()
                         : box->CaretMaxOffset();
          }
          break;
        }
        if (prev_box->BidiLevel() >= level)
          break;

        level = prev_box->BidiLevel();

        InlineBox* next_box = box;
        do {
          next_box = next_box->NextLeafChild();
        } while (next_box && next_box->BidiLevel() > level);

        if (next_box && next_box->BidiLevel() == level)
          break;

        box = prev_box;
        line_layout_item = box->GetLineLayoutItem();
        offset = box->CaretRightmostOffset();
        if (box->Direction() == primary_direction)
          break;
        continue;
      }

      while (prev_box && !prev_box->GetLineLayoutItem().GetNode())
        prev_box = prev_box->PrevLeafChild();

      if (prev_box) {
        box = prev_box;
        line_layout_item = box->GetLineLayoutItem();
        offset = box->CaretRightmostOffset();
        if (box->BidiLevel() > level) {
          do {
            prev_box = prev_box->PrevLeafChild();
          } while (prev_box && prev_box->BidiLevel() > level);

          if (!prev_box || prev_box->BidiLevel() < level)
            continue;
        }
      } else {
        // Trailing edge of a secondary run. Set to the leading edge of
        // the entire run.
        while (true) {
          while (InlineBox* next_box = box->NextLeafChild()) {
            if (next_box->BidiLevel() < level)
              break;
            box = next_box;
          }
          if (box->BidiLevel() == level)
            break;
          level = box->BidiLevel();
          while (InlineBox* prev_box = box->PrevLeafChild()) {
            if (prev_box->BidiLevel() < level)
              break;
            box = prev_box;
          }
          if (box->BidiLevel() == level)
            break;
          level = box->BidiLevel();
        }
        line_layout_item = box->GetLineLayoutItem();
        offset = primary_direction == TextDirection::kLtr
                     ? box->CaretMinOffset()
                     : box->CaretMaxOffset();
      }
      break;
    }

    p = PositionTemplate<Strategy>::EditingPositionOf(
        line_layout_item.GetNode(), offset);

    if ((IsVisuallyEquivalentCandidate(p) &&
         MostForwardCaretPosition(p) != downstream_start) ||
        p.AtStartOfTree() || p.AtEndOfTree())
      return p;

    DCHECK_NE(p, deep_position);
  }
}

template <typename Strategy>
VisiblePositionTemplate<Strategy> LeftPositionOfAlgorithm(
    const VisiblePositionTemplate<Strategy>& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  const PositionTemplate<Strategy> pos =
      LeftVisuallyDistinctCandidate(visible_position);
  // TODO(yosin) Why can't we move left from the last position in a tree?
  if (pos.AtStartOfTree() || pos.AtEndOfTree())
    return VisiblePositionTemplate<Strategy>();

  const VisiblePositionTemplate<Strategy> left = CreateVisiblePosition(pos);
  DCHECK_NE(left.DeepEquivalent(), visible_position.DeepEquivalent());

  return DirectionOfEnclosingBlock(left.DeepEquivalent()) == TextDirection::kLtr
             ? HonorEditingBoundaryAtOrBefore(left,
                                              visible_position.DeepEquivalent())
             : HonorEditingBoundaryAtOrAfter(left,
                                             visible_position.DeepEquivalent());
}

VisiblePosition LeftPositionOf(const VisiblePosition& visible_position) {
  return LeftPositionOfAlgorithm<EditingStrategy>(visible_position);
}

VisiblePositionInFlatTree LeftPositionOf(
    const VisiblePositionInFlatTree& visible_position) {
  return LeftPositionOfAlgorithm<EditingInFlatTreeStrategy>(visible_position);
}

template <typename Strategy>
static PositionTemplate<Strategy> RightVisuallyDistinctCandidate(
    const VisiblePositionTemplate<Strategy>& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  const PositionTemplate<Strategy> deep_position =
      visible_position.DeepEquivalent();
  PositionTemplate<Strategy> p = deep_position;
  if (p.IsNull())
    return PositionTemplate<Strategy>();

  const PositionTemplate<Strategy> downstream_start =
      MostForwardCaretPosition(p);
  TextDirection primary_direction = PrimaryDirectionOf(*p.AnchorNode());
  const TextAffinity affinity = visible_position.Affinity();

  while (true) {
    InlineBoxPosition box_position =
        ComputeInlineBoxPosition(p, affinity, primary_direction);
    InlineBox* box = box_position.inline_box;
    int offset = box_position.offset_in_box;
    if (!box)
      return primary_direction == TextDirection::kLtr
                 ? NextVisuallyDistinctCandidate(deep_position)
                 : PreviousVisuallyDistinctCandidate(deep_position);

    LayoutObject* layout_object =
        LineLayoutAPIShim::LayoutObjectFrom(box->GetLineLayoutItem());

    while (true) {
      if ((layout_object->IsAtomicInlineLevel() || layout_object->IsBR()) &&
          offset == box->CaretLeftmostOffset())
        return box->IsLeftToRightDirection()
                   ? NextVisuallyDistinctCandidate(deep_position)
                   : PreviousVisuallyDistinctCandidate(deep_position);

      if (!layout_object->GetNode()) {
        box = box->NextLeafChild();
        if (!box)
          return primary_direction == TextDirection::kLtr
                     ? NextVisuallyDistinctCandidate(deep_position)
                     : PreviousVisuallyDistinctCandidate(deep_position);
        layout_object =
            LineLayoutAPIShim::LayoutObjectFrom(box->GetLineLayoutItem());
        offset = box->CaretLeftmostOffset();
        continue;
      }

      offset =
          box->IsLeftToRightDirection()
              ? NextGraphemeBoundaryOf(layout_object->GetNode(), offset)
              : PreviousGraphemeBoundaryOf(layout_object->GetNode(), offset);

      int caret_min_offset = box->CaretMinOffset();
      int caret_max_offset = box->CaretMaxOffset();

      if (offset > caret_min_offset && offset < caret_max_offset)
        break;

      if (box->IsLeftToRightDirection() ? offset > caret_max_offset
                                        : offset < caret_min_offset) {
        // Overshot to the right.
        InlineBox* next_box = box->NextLeafChildIgnoringLineBreak();
        if (!next_box) {
          PositionTemplate<Strategy> position_on_right =
              primary_direction == TextDirection::kLtr
                  ? NextVisuallyDistinctCandidate(deep_position)
                  : PreviousVisuallyDistinctCandidate(deep_position);
          if (position_on_right.IsNull())
            return PositionTemplate<Strategy>();

          InlineBox* box_on_right =
              ComputeInlineBoxPosition(position_on_right, affinity,
                                       primary_direction)
                  .inline_box;
          if (box_on_right && box_on_right->Root() == box->Root())
            return PositionTemplate<Strategy>();
          return position_on_right;
        }

        // Reposition at the other logical position corresponding to our
        // edge's visual position and go for another round.
        box = next_box;
        layout_object =
            LineLayoutAPIShim::LayoutObjectFrom(box->GetLineLayoutItem());
        offset = next_box->CaretLeftmostOffset();
        continue;
      }

      DCHECK_EQ(offset, box->CaretRightmostOffset());

      unsigned char level = box->BidiLevel();
      InlineBox* next_box = box->NextLeafChild();

      if (box->Direction() == primary_direction) {
        if (!next_box) {
          InlineBox* logical_end = 0;
          if (primary_direction == TextDirection::kLtr
                  ? box->Root().GetLogicalEndBoxWithNode(logical_end)
                  : box->Root().GetLogicalStartBoxWithNode(logical_end)) {
            box = logical_end;
            layout_object =
                LineLayoutAPIShim::LayoutObjectFrom(box->GetLineLayoutItem());
            offset = primary_direction == TextDirection::kLtr
                         ? box->CaretMaxOffset()
                         : box->CaretMinOffset();
          }
          break;
        }

        if (next_box->BidiLevel() >= level)
          break;

        level = next_box->BidiLevel();

        InlineBox* prev_box = box;
        do {
          prev_box = prev_box->PrevLeafChild();
        } while (prev_box && prev_box->BidiLevel() > level);

        // For example, abc FED 123 ^ CBA
        if (prev_box && prev_box->BidiLevel() == level)
          break;

        // For example, abc 123 ^ CBA or 123 ^ CBA abc
        box = next_box;
        layout_object =
            LineLayoutAPIShim::LayoutObjectFrom(box->GetLineLayoutItem());
        offset = box->CaretLeftmostOffset();
        if (box->Direction() == primary_direction)
          break;
        continue;
      }

      while (next_box && !next_box->GetLineLayoutItem().GetNode())
        next_box = next_box->NextLeafChild();

      if (next_box) {
        box = next_box;
        layout_object =
            LineLayoutAPIShim::LayoutObjectFrom(box->GetLineLayoutItem());
        offset = box->CaretLeftmostOffset();

        if (box->BidiLevel() > level) {
          do {
            next_box = next_box->NextLeafChild();
          } while (next_box && next_box->BidiLevel() > level);

          if (!next_box || next_box->BidiLevel() < level)
            continue;
        }
      } else {
        // Trailing edge of a secondary run. Set to the leading edge of the
        // entire run.
        while (true) {
          while (InlineBox* prev_box = box->PrevLeafChild()) {
            if (prev_box->BidiLevel() < level)
              break;
            box = prev_box;
          }
          if (box->BidiLevel() == level)
            break;
          level = box->BidiLevel();
          while (InlineBox* next_box = box->NextLeafChild()) {
            if (next_box->BidiLevel() < level)
              break;
            box = next_box;
          }
          if (box->BidiLevel() == level)
            break;
          level = box->BidiLevel();
        }
        layout_object =
            LineLayoutAPIShim::LayoutObjectFrom(box->GetLineLayoutItem());
        offset = primary_direction == TextDirection::kLtr
                     ? box->CaretMaxOffset()
                     : box->CaretMinOffset();
      }
      break;
    }

    p = PositionTemplate<Strategy>::EditingPositionOf(layout_object->GetNode(),
                                                      offset);

    if ((IsVisuallyEquivalentCandidate(p) &&
         MostForwardCaretPosition(p) != downstream_start) ||
        p.AtStartOfTree() || p.AtEndOfTree())
      return p;

    DCHECK_NE(p, deep_position);
  }
}

template <typename Strategy>
static VisiblePositionTemplate<Strategy> RightPositionOfAlgorithm(
    const VisiblePositionTemplate<Strategy>& visible_position) {
  DCHECK(visible_position.IsValid()) << visible_position;
  const PositionTemplate<Strategy> pos =
      RightVisuallyDistinctCandidate(visible_position);
  // FIXME: Why can't we move left from the last position in a tree?
  if (pos.AtStartOfTree() || pos.AtEndOfTree())
    return VisiblePositionTemplate<Strategy>();

  const VisiblePositionTemplate<Strategy> right = CreateVisiblePosition(pos);
  DCHECK_NE(right.DeepEquivalent(), visible_position.DeepEquivalent());

  return DirectionOfEnclosingBlock(right.DeepEquivalent()) ==
                 TextDirection::kLtr
             ? HonorEditingBoundaryAtOrAfter(right,
                                             visible_position.DeepEquivalent())
             : HonorEditingBoundaryAtOrBefore(
                   right, visible_position.DeepEquivalent());
}

VisiblePosition RightPositionOf(const VisiblePosition& visible_position) {
  return RightPositionOfAlgorithm<EditingStrategy>(visible_position);
}

VisiblePositionInFlatTree RightPositionOf(
    const VisiblePositionInFlatTree& visible_position) {
  return RightPositionOfAlgorithm<EditingInFlatTreeStrategy>(visible_position);
}

template <typename Strategy>
static VisiblePositionTemplate<Strategy> NextPositionOfAlgorithm(
    const PositionWithAffinityTemplate<Strategy>& position,
    EditingBoundaryCrossingRule rule) {
  const VisiblePositionTemplate<Strategy> next = CreateVisiblePosition(
      NextVisuallyDistinctCandidate(position.GetPosition()),
      position.Affinity());

  switch (rule) {
    case kCanCrossEditingBoundary:
      return next;
    case kCannotCrossEditingBoundary:
      return HonorEditingBoundaryAtOrAfter(next, position.GetPosition());
    case kCanSkipOverEditingBoundary:
      return SkipToEndOfEditingBoundary(next, position.GetPosition());
  }
  NOTREACHED();
  return HonorEditingBoundaryAtOrAfter(next, position.GetPosition());
}

VisiblePosition NextPositionOf(const VisiblePosition& visible_position,
                               EditingBoundaryCrossingRule rule) {
  DCHECK(visible_position.IsValid()) << visible_position;
  return NextPositionOfAlgorithm<EditingStrategy>(
      visible_position.ToPositionWithAffinity(), rule);
}

VisiblePositionInFlatTree NextPositionOf(
    const VisiblePositionInFlatTree& visible_position,
    EditingBoundaryCrossingRule rule) {
  DCHECK(visible_position.IsValid()) << visible_position;
  return NextPositionOfAlgorithm<EditingInFlatTreeStrategy>(
      visible_position.ToPositionWithAffinity(), rule);
}

template <typename Strategy>
static VisiblePositionTemplate<Strategy> SkipToStartOfEditingBoundary(
    const VisiblePositionTemplate<Strategy>& pos,
    const PositionTemplate<Strategy>& anchor) {
  DCHECK(pos.IsValid()) << pos;
  if (pos.IsNull())
    return pos;

  ContainerNode* highest_root = HighestEditableRoot(anchor);
  ContainerNode* highest_root_of_pos =
      HighestEditableRoot(pos.DeepEquivalent());

  // Return |pos| itself if the two are from the very same editable region, or
  // both are non-editable.
  if (highest_root_of_pos == highest_root)
    return pos;

  // If this is not editable but |pos| has an editable root, skip to the start
  if (!highest_root && highest_root_of_pos)
    return CreateVisiblePosition(PreviousVisuallyDistinctCandidate(
        PositionTemplate<Strategy>(highest_root_of_pos,
                                   PositionAnchorType::kBeforeAnchor)
            .ParentAnchoredEquivalent()));

  // That must mean that |pos| is not editable. Return the last position
  // before |pos| that is in the same editable region as this position
  DCHECK(highest_root);
  return LastEditableVisiblePositionBeforePositionInRoot(pos.DeepEquivalent(),
                                                         *highest_root);
}

template <typename Strategy>
static VisiblePositionTemplate<Strategy> PreviousPositionOfAlgorithm(
    const PositionTemplate<Strategy>& position,
    EditingBoundaryCrossingRule rule) {
  const PositionTemplate<Strategy> prev_position =
      PreviousVisuallyDistinctCandidate(position);

  // return null visible position if there is no previous visible position
  if (prev_position.AtStartOfTree())
    return VisiblePositionTemplate<Strategy>();

  // we should always be able to make the affinity |TextAffinity::Downstream|,
  // because going previous from an |TextAffinity::Upstream| position can
  // never yield another |TextAffinity::Upstream position| (unless line wrap
  // length is 0!).
  const VisiblePositionTemplate<Strategy> prev =
      CreateVisiblePosition(prev_position);
  if (prev.DeepEquivalent() == position)
    return VisiblePositionTemplate<Strategy>();

  switch (rule) {
    case kCanCrossEditingBoundary:
      return prev;
    case kCannotCrossEditingBoundary:
      return HonorEditingBoundaryAtOrBefore(prev, position);
    case kCanSkipOverEditingBoundary:
      return SkipToStartOfEditingBoundary(prev, position);
  }

  NOTREACHED();
  return HonorEditingBoundaryAtOrBefore(prev, position);
}

VisiblePosition PreviousPositionOf(const VisiblePosition& visible_position,
                                   EditingBoundaryCrossingRule rule) {
  DCHECK(visible_position.IsValid()) << visible_position;
  return PreviousPositionOfAlgorithm<EditingStrategy>(
      visible_position.DeepEquivalent(), rule);
}

VisiblePositionInFlatTree PreviousPositionOf(
    const VisiblePositionInFlatTree& visible_position,
    EditingBoundaryCrossingRule rule) {
  DCHECK(visible_position.IsValid()) << visible_position;
  return PreviousPositionOfAlgorithm<EditingInFlatTreeStrategy>(
      visible_position.DeepEquivalent(), rule);
}

template <typename Strategy>
static EphemeralRangeTemplate<Strategy> MakeSearchRange(
    const PositionTemplate<Strategy>& pos) {
  Node* node = pos.ComputeContainerNode();
  if (!node)
    return EphemeralRangeTemplate<Strategy>();
  Document& document = node->GetDocument();
  if (!document.documentElement())
    return EphemeralRangeTemplate<Strategy>();
  Element* boundary = EnclosingBlockFlowElement(*node);
  if (!boundary)
    return EphemeralRangeTemplate<Strategy>();

  return EphemeralRangeTemplate<Strategy>(
      pos, PositionTemplate<Strategy>::LastPositionInNode(boundary));
}

template <typename Strategy>
static PositionTemplate<Strategy> SkipWhitespaceAlgorithm(
    const PositionTemplate<Strategy>& position) {
  const EphemeralRangeTemplate<Strategy>& search_range =
      MakeSearchRange(position);
  if (search_range.IsNull())
    return position;

  CharacterIteratorAlgorithm<Strategy> char_it(
      search_range.StartPosition(), search_range.EndPosition(),
      TextIteratorBehavior::Builder()
          .SetEmitsCharactersBetweenAllVisiblePositions(true)
          .Build());
  PositionTemplate<Strategy> runner = position;
  // TODO(editing-dev): We should consider U+20E3, COMBINING ENCLOSING KEYCAP.
  // When whitespace character followed by U+20E3, we should not consider
  // it as trailing white space.
  for (; char_it.length(); char_it.Advance(1)) {
    UChar c = char_it.CharacterAt(0);
    if ((!IsSpaceOrNewline(c) && c != kNoBreakSpaceCharacter) || c == '\n')
      return runner;
    runner = char_it.EndPosition();
  }
  return runner;
}

Position SkipWhitespace(const Position& position) {
  return SkipWhitespaceAlgorithm(position);
}

PositionInFlatTree SkipWhitespace(const PositionInFlatTree& position) {
  return SkipWhitespaceAlgorithm(position);
}

static void CollectAbsoluteBoundsForRange(unsigned start,
                                          unsigned end,
                                          const LayoutText& layout_text,
                                          Vector<IntRect>& rects) {
  layout_text.AbsoluteRectsForRange(rects, start, end);
}

static void CollectAbsoluteBoundsForRange(unsigned start,
                                          unsigned end,
                                          const LayoutText& layout_text,
                                          Vector<FloatQuad>& quads) {
  layout_text.AbsoluteQuadsForRange(quads, start, end);
}

template <typename RectType, typename Strategy>
static Vector<RectType> ComputeTextBounds(
    const EphemeralRangeTemplate<Strategy>& range) {
  const PositionTemplate<Strategy>& start_position = range.StartPosition();
  const PositionTemplate<Strategy>& end_position = range.EndPosition();
  Node* const start_container = start_position.ComputeContainerNode();
  DCHECK(start_container);
  Node* const end_container = end_position.ComputeContainerNode();
  DCHECK(end_container);

  Vector<RectType> result;
  for (const Node& node : range.Nodes()) {
    LayoutObject* const layout_object = node.GetLayoutObject();
    if (!layout_object || !layout_object->IsText())
      continue;
    const LayoutText* layout_text = ToLayoutText(layout_object);
    unsigned start_offset =
        node == start_container ? start_position.OffsetInContainerNode() : 0;
    unsigned end_offset = node == end_container
                              ? end_position.OffsetInContainerNode()
                              : std::numeric_limits<unsigned>::max();
    CollectAbsoluteBoundsForRange(start_offset, end_offset, *layout_text,
                                  result);
  }
  return result;
}

template <typename Strategy>
static IntRect ComputeTextRectTemplate(
    const EphemeralRangeTemplate<Strategy>& range) {
  IntRect result;
  const Vector<IntRect>& rects = ComputeTextBounds<IntRect, Strategy>(range);
  for (const IntRect& rect : rects)
    result.Unite(rect);
  return result;
}

IntRect ComputeTextRect(const EphemeralRange& range) {
  return ComputeTextRectTemplate(range);
}

IntRect ComputeTextRect(const EphemeralRangeInFlatTree& range) {
  return ComputeTextRectTemplate(range);
}

Vector<FloatQuad> ComputeTextQuads(const EphemeralRange& range) {
  return ComputeTextBounds<FloatQuad>(range);
}

}  // namespace blink
