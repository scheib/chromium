/*
 * Copyright (C) 2004 Apple Computer, Inc.  All rights reserved.
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

#ifndef VisibleUnits_h
#define VisibleUnits_h

#include "core/CoreExport.h"
#include "core/editing/EditingBoundary.h"
#include "core/editing/EphemeralRange.h"
#include "core/editing/PositionWithAffinity.h"
#include "core/editing/VisiblePosition.h"
#include "platform/text/TextDirection.h"

namespace blink {

class LayoutRect;
class LayoutUnit;
class LayoutObject;
class Node;
class IntPoint;
class InlineBox;
class LocalFrame;

enum EWordSide { kRightWordIfOnBoundary = false, kLeftWordIfOnBoundary = true };

struct InlineBoxPosition {
  InlineBox* inline_box;
  int offset_in_box;

  InlineBoxPosition() : inline_box(nullptr), offset_in_box(0) {}

  InlineBoxPosition(InlineBox* inline_box, int offset_in_box)
      : inline_box(inline_box), offset_in_box(offset_in_box) {}

  bool operator==(const InlineBoxPosition& other) const {
    return inline_box == other.inline_box &&
           offset_in_box == other.offset_in_box;
  }

  bool operator!=(const InlineBoxPosition& other) const {
    return !operator==(other);
  }
};

// The print for |InlineBoxPosition| is available only for testing
// in "webkit_unit_tests", and implemented in
// "core/editing/VisibleUnitsTest.cpp".
std::ostream& operator<<(std::ostream&, const InlineBoxPosition&);

CORE_EXPORT LayoutObject* AssociatedLayoutObjectOf(const Node&,
                                                   int offset_in_node);

// offset functions on Node
CORE_EXPORT int CaretMinOffset(const Node*);
CORE_EXPORT int CaretMaxOffset(const Node*);

// Position
// mostForward/BackwardCaretPosition are used for moving back and forth between
// visually equivalent candidates.
// For example, for the text node "foo     bar" where whitespace is
// collapsible, there are two candidates that map to the VisiblePosition
// between 'b' and the space, after first space and before last space.
//
// mostBackwardCaretPosition returns the left candidate and also returs
// [boundary, 0] for any of the positions from [boundary, 0] to the first
// candidate in boundary, where
// endsOfNodeAreVisuallyDistinctPositions(boundary) is true.
//
// mostForwardCaretPosition() returns the right one and also returns the
// last position in the last atomic node in boundary for all of the positions
// in boundary after the last candidate, where
// endsOfNodeAreVisuallyDistinctPositions(boundary).
// FIXME: This function should never be called when the line box tree is dirty.
// See https://bugs.webkit.org/show_bug.cgi?id=97264
CORE_EXPORT Position MostBackwardCaretPosition(
    const Position&,
    EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
CORE_EXPORT PositionInFlatTree MostBackwardCaretPosition(
    const PositionInFlatTree&,
    EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
CORE_EXPORT Position MostForwardCaretPosition(
    const Position&,
    EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
CORE_EXPORT PositionInFlatTree MostForwardCaretPosition(
    const PositionInFlatTree&,
    EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);

CORE_EXPORT bool IsVisuallyEquivalentCandidate(const Position&);
CORE_EXPORT bool IsVisuallyEquivalentCandidate(const PositionInFlatTree&);

// Whether or not [node, 0] and [node, lastOffsetForEditing(node)] are their own
// VisiblePositions.
// If true, adjacent candidates are visually distinct.
CORE_EXPORT bool EndsOfNodeAreVisuallyDistinctPositions(const Node*);

CORE_EXPORT Position CanonicalPositionOf(const Position&);
CORE_EXPORT PositionInFlatTree CanonicalPositionOf(const PositionInFlatTree&);

// Bounds of (possibly transformed) caret in absolute coords
CORE_EXPORT IntRect AbsoluteCaretBoundsOf(const VisiblePosition&);
CORE_EXPORT IntRect AbsoluteCaretBoundsOf(const VisiblePositionInFlatTree&);

CORE_EXPORT IntRect AbsoluteSelectionBoundsOf(const VisiblePosition&);
CORE_EXPORT IntRect AbsoluteSelectionBoundsOf(const VisiblePositionInFlatTree&);

CORE_EXPORT UChar32 CharacterAfter(const VisiblePosition&);
CORE_EXPORT UChar32 CharacterAfter(const VisiblePositionInFlatTree&);
CORE_EXPORT UChar32 CharacterBefore(const VisiblePosition&);
CORE_EXPORT UChar32 CharacterBefore(const VisiblePositionInFlatTree&);

// TODO(yosin) Since return value of |leftPositionOf()| with |VisiblePosition|
// isn't defined well on flat tree, we should not use it for a position in
// flat tree.
CORE_EXPORT VisiblePosition LeftPositionOf(const VisiblePosition&);
CORE_EXPORT VisiblePositionInFlatTree
LeftPositionOf(const VisiblePositionInFlatTree&);
// TODO(yosin) Since return value of |rightPositionOf()| with |VisiblePosition|
// isn't defined well on flat tree, we should not use it for a position in
// flat tree.
CORE_EXPORT VisiblePosition RightPositionOf(const VisiblePosition&);
CORE_EXPORT VisiblePositionInFlatTree
RightPositionOf(const VisiblePositionInFlatTree&);

CORE_EXPORT VisiblePosition
NextPositionOf(const VisiblePosition&,
               EditingBoundaryCrossingRule = kCanCrossEditingBoundary);
CORE_EXPORT VisiblePositionInFlatTree
NextPositionOf(const VisiblePositionInFlatTree&,
               EditingBoundaryCrossingRule = kCanCrossEditingBoundary);
CORE_EXPORT VisiblePosition
PreviousPositionOf(const VisiblePosition&,
                   EditingBoundaryCrossingRule = kCanCrossEditingBoundary);
CORE_EXPORT VisiblePositionInFlatTree
PreviousPositionOf(const VisiblePositionInFlatTree&,
                   EditingBoundaryCrossingRule = kCanCrossEditingBoundary);

// words
// TODO(yoichio): Replace |startOfWord| to |startOfWordPosition| because
// returned Position should be canonicalized with |previousBoundary()| by
// TextItetator.
CORE_EXPORT Position StartOfWordPosition(const VisiblePosition&,
                                         EWordSide = kRightWordIfOnBoundary);
CORE_EXPORT VisiblePosition StartOfWord(const VisiblePosition&,
                                        EWordSide = kRightWordIfOnBoundary);
CORE_EXPORT PositionInFlatTree
StartOfWordPosition(const VisiblePositionInFlatTree&,
                    EWordSide = kRightWordIfOnBoundary);
CORE_EXPORT VisiblePositionInFlatTree
StartOfWord(const VisiblePositionInFlatTree&,
            EWordSide = kRightWordIfOnBoundary);
// TODO(yoichio): Replace |endOfWord| to |endOfWordPosition| because returned
// Position should be canonicalized with |nextBoundary()| by TextItetator.
CORE_EXPORT Position EndOfWordPosition(const VisiblePosition&,
                                       EWordSide = kRightWordIfOnBoundary);
CORE_EXPORT VisiblePosition EndOfWord(const VisiblePosition&,
                                      EWordSide = kRightWordIfOnBoundary);
CORE_EXPORT PositionInFlatTree
EndOfWordPosition(const VisiblePositionInFlatTree&,
                  EWordSide = kRightWordIfOnBoundary);
CORE_EXPORT VisiblePositionInFlatTree
EndOfWord(const VisiblePositionInFlatTree&, EWordSide = kRightWordIfOnBoundary);
VisiblePosition PreviousWordPosition(const VisiblePosition&);
VisiblePosition NextWordPosition(const VisiblePosition&);
VisiblePosition RightWordPosition(const VisiblePosition&,
                                  bool skips_space_when_moving_right);
VisiblePosition LeftWordPosition(const VisiblePosition&,
                                 bool skips_space_when_moving_right);

// sentences
CORE_EXPORT VisiblePosition StartOfSentence(const VisiblePosition&);
CORE_EXPORT VisiblePositionInFlatTree
StartOfSentence(const VisiblePositionInFlatTree&);
CORE_EXPORT VisiblePosition EndOfSentence(const VisiblePosition&);
CORE_EXPORT VisiblePositionInFlatTree
EndOfSentence(const VisiblePositionInFlatTree&);
VisiblePosition PreviousSentencePosition(const VisiblePosition&);
VisiblePosition NextSentencePosition(const VisiblePosition&);
EphemeralRange ExpandEndToSentenceBoundary(const EphemeralRange&);
EphemeralRange ExpandRangeToSentenceBoundary(const EphemeralRange&);

// lines
// TODO(yosin) Return values of |VisiblePosition| version of |startOfLine()|
// with shadow tree isn't defined well. We should not use it for shadow tree.
CORE_EXPORT VisiblePosition StartOfLine(const VisiblePosition&);
CORE_EXPORT VisiblePositionInFlatTree
StartOfLine(const VisiblePositionInFlatTree&);
// TODO(yosin) Return values of |VisiblePosition| version of |endOfLine()| with
// shadow tree isn't defined well. We should not use it for shadow tree.
CORE_EXPORT VisiblePosition EndOfLine(const VisiblePosition&);
CORE_EXPORT VisiblePositionInFlatTree
EndOfLine(const VisiblePositionInFlatTree&);
enum EditableType { kContentIsEditable, kHasEditableAXRole };
CORE_EXPORT VisiblePosition
PreviousLinePosition(const VisiblePosition&,
                     LayoutUnit line_direction_point,
                     EditableType = kContentIsEditable);
CORE_EXPORT VisiblePosition NextLinePosition(const VisiblePosition&,
                                             LayoutUnit line_direction_point,
                                             EditableType = kContentIsEditable);
CORE_EXPORT bool InSameLine(const VisiblePosition&, const VisiblePosition&);
CORE_EXPORT bool InSameLine(const VisiblePositionInFlatTree&,
                            const VisiblePositionInFlatTree&);
CORE_EXPORT bool InSameLine(const PositionWithAffinity&,
                            const PositionWithAffinity&);
CORE_EXPORT bool InSameLine(const PositionInFlatTreeWithAffinity&,
                            const PositionInFlatTreeWithAffinity&);
CORE_EXPORT bool IsStartOfLine(const VisiblePosition&);
CORE_EXPORT bool IsStartOfLine(const VisiblePositionInFlatTree&);
CORE_EXPORT bool IsEndOfLine(const VisiblePosition&);
CORE_EXPORT bool IsEndOfLine(const VisiblePositionInFlatTree&);
// TODO(yosin) Return values of |VisiblePosition| version of
// |logicalStartOfLine()| with shadow tree isn't defined well. We should not use
// it for shadow tree.
CORE_EXPORT VisiblePosition LogicalStartOfLine(const VisiblePosition&);
CORE_EXPORT VisiblePositionInFlatTree
LogicalStartOfLine(const VisiblePositionInFlatTree&);
// TODO(yosin) Return values of |VisiblePosition| version of
// |logicalEndOfLine()| with shadow tree isn't defined well. We should not use
// it for shadow tree.
CORE_EXPORT VisiblePosition LogicalEndOfLine(const VisiblePosition&);
CORE_EXPORT VisiblePositionInFlatTree
LogicalEndOfLine(const VisiblePositionInFlatTree&);
CORE_EXPORT bool IsLogicalEndOfLine(const VisiblePosition&);
CORE_EXPORT bool IsLogicalEndOfLine(const VisiblePositionInFlatTree&);
VisiblePosition LeftBoundaryOfLine(const VisiblePosition&, TextDirection);
VisiblePosition RightBoundaryOfLine(const VisiblePosition&, TextDirection);

// paragraphs (perhaps a misnomer, can be divided by line break elements)
// TODO(yosin) Since return value of |startOfParagraph()| with |VisiblePosition|
// isn't defined well on flat tree, we should not use it for a position in
// flat tree.
CORE_EXPORT VisiblePosition
StartOfParagraph(const VisiblePosition&,
                 EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
CORE_EXPORT VisiblePositionInFlatTree
StartOfParagraph(const VisiblePositionInFlatTree&,
                 EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
CORE_EXPORT VisiblePosition
EndOfParagraph(const VisiblePosition&,
               EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
CORE_EXPORT VisiblePositionInFlatTree
EndOfParagraph(const VisiblePositionInFlatTree&,
               EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
VisiblePosition StartOfNextParagraph(const VisiblePosition&);
VisiblePosition PreviousParagraphPosition(const VisiblePosition&, LayoutUnit x);
VisiblePosition NextParagraphPosition(const VisiblePosition&, LayoutUnit x);
CORE_EXPORT bool IsStartOfParagraph(
    const VisiblePosition&,
    EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
CORE_EXPORT bool IsStartOfParagraph(
    const VisiblePositionInFlatTree&,
    EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
CORE_EXPORT bool IsEndOfParagraph(
    const VisiblePosition&,
    EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
CORE_EXPORT bool IsEndOfParagraph(
    const VisiblePositionInFlatTree&,
    EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
bool InSameParagraph(const VisiblePosition&,
                     const VisiblePosition&,
                     EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
EphemeralRange ExpandToParagraphBoundary(const EphemeralRange&);

// blocks (true paragraphs; line break elements don't break blocks)
VisiblePosition StartOfBlock(
    const VisiblePosition&,
    EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
VisiblePosition EndOfBlock(
    const VisiblePosition&,
    EditingBoundaryCrossingRule = kCannotCrossEditingBoundary);
bool InSameBlock(const VisiblePosition&, const VisiblePosition&);
bool IsStartOfBlock(const VisiblePosition&);
bool IsEndOfBlock(const VisiblePosition&);

// document
CORE_EXPORT VisiblePosition StartOfDocument(const VisiblePosition&);
CORE_EXPORT VisiblePositionInFlatTree
StartOfDocument(const VisiblePositionInFlatTree&);
CORE_EXPORT VisiblePosition EndOfDocument(const VisiblePosition&);
CORE_EXPORT VisiblePositionInFlatTree
EndOfDocument(const VisiblePositionInFlatTree&);
bool IsStartOfDocument(const VisiblePosition&);
bool IsEndOfDocument(const VisiblePosition&);

// editable content
VisiblePosition StartOfEditableContent(const VisiblePosition&);
VisiblePosition EndOfEditableContent(const VisiblePosition&);
CORE_EXPORT bool IsEndOfEditableOrNonEditableContent(const VisiblePosition&);
CORE_EXPORT bool IsEndOfEditableOrNonEditableContent(
    const VisiblePositionInFlatTree&);

CORE_EXPORT InlineBoxPosition ComputeInlineBoxPosition(const Position&,
                                                       TextAffinity);
CORE_EXPORT InlineBoxPosition
ComputeInlineBoxPosition(const Position&,
                         TextAffinity,
                         TextDirection primary_direction);
CORE_EXPORT InlineBoxPosition
ComputeInlineBoxPosition(const PositionInFlatTree&, TextAffinity);
CORE_EXPORT InlineBoxPosition
ComputeInlineBoxPosition(const PositionInFlatTree&,
                         TextAffinity,
                         TextDirection primary_direction);
CORE_EXPORT InlineBoxPosition ComputeInlineBoxPosition(const VisiblePosition&);

// Rect is local to the returned layoutObject
CORE_EXPORT LayoutRect LocalCaretRectOfPosition(const PositionWithAffinity&,
                                                LayoutObject*&);
CORE_EXPORT LayoutRect
LocalCaretRectOfPosition(const PositionInFlatTreeWithAffinity&, LayoutObject*&);
bool HasRenderedNonAnonymousDescendantsWithHeight(LayoutObject*);

// Returns a hit-tested VisiblePosition for the given point in contents-space
// coordinates.
CORE_EXPORT VisiblePosition VisiblePositionForContentsPoint(const IntPoint&,
                                                            LocalFrame*);

CORE_EXPORT bool RendersInDifferentPosition(const Position&, const Position&);

CORE_EXPORT Position SkipWhitespace(const Position&);
CORE_EXPORT PositionInFlatTree SkipWhitespace(const PositionInFlatTree&);

CORE_EXPORT IntRect ComputeTextRect(const EphemeralRange&);
IntRect ComputeTextRect(const EphemeralRangeInFlatTree&);
CORE_EXPORT Vector<FloatQuad> ComputeTextQuads(const EphemeralRange&);

}  // namespace blink

#endif  // VisibleUnits_h
