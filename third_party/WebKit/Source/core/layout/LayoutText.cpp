/*
 * (C) 1999 Lars Knoll (knoll@kde.org)
 * (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007 Apple Inc. All rights reserved.
 * Copyright (C) 2006 Andrew Wellington (proton@wiretapped.net)
 * Copyright (C) 2006 Graham Dennis (graham.dennis@gmail.com)
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

#include "core/layout/LayoutText.h"

#include <algorithm>
#include "core/dom/AXObjectCache.h"
#include "core/dom/Text.h"
#include "core/editing/VisiblePosition.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/frame/FrameView.h"
#include "core/frame/Settings.h"
#include "core/layout/LayoutBlock.h"
#include "core/layout/LayoutTableCell.h"
#include "core/layout/LayoutTextCombine.h"
#include "core/layout/LayoutView.h"
#include "core/layout/TextAutosizer.h"
#include "core/layout/api/LineLayoutBox.h"
#include "core/layout/line/AbstractInlineTextBox.h"
#include "core/layout/line/EllipsisBox.h"
#include "core/layout/line/GlyphOverflow.h"
#include "core/layout/line/InlineTextBox.h"
#include "platform/fonts/CharacterRange.h"
#include "platform/geometry/FloatQuad.h"
#include "platform/scheduler/child/web_scheduler.h"
#include "platform/text/BidiResolver.h"
#include "platform/text/Character.h"
#include "platform/text/Hyphenation.h"
#include "platform/text/TextBreakIterator.h"
#include "platform/text/TextRunIterator.h"
#include "platform/wtf/text/StringBuffer.h"
#include "platform/wtf/text/StringBuilder.h"
#include "public/platform/Platform.h"
#include "public/platform/WebThread.h"

namespace blink {

struct SameSizeAsLayoutText : public LayoutObject {
  uint32_t bitfields : 16;
  float widths[4];
  String text;
  void* pointers[2];
};

static_assert(sizeof(LayoutText) == sizeof(SameSizeAsLayoutText),
              "LayoutText should stay small");

class SecureTextTimer;
typedef HashMap<LayoutText*, SecureTextTimer*> SecureTextTimerMap;
static SecureTextTimerMap* g_secure_text_timers = nullptr;

class SecureTextTimer final : public TimerBase {
 public:
  SecureTextTimer(LayoutText* layout_text)
      : TimerBase(Platform::Current()
                      ->CurrentThread()
                      ->Scheduler()
                      ->TimerTaskRunner()),
        layout_text_(layout_text),
        last_typed_character_offset_(-1) {}

  void RestartWithNewText(unsigned last_typed_character_offset) {
    last_typed_character_offset_ = last_typed_character_offset;
    if (Settings* settings = layout_text_->GetDocument().GetSettings()) {
      StartOneShot(settings->GetPasswordEchoDurationInSeconds(),
                   BLINK_FROM_HERE);
    }
  }
  void Invalidate() { last_typed_character_offset_ = -1; }
  unsigned LastTypedCharacterOffset() { return last_typed_character_offset_; }

 private:
  void Fired() override {
    DCHECK(g_secure_text_timers->Contains(layout_text_));
    layout_text_->SetText(
        layout_text_->GetText().Impl(),
        true /* forcing setting text as it may be masked later */);
  }

  LayoutText* layout_text_;
  int last_typed_character_offset_;
};

static void MakeCapitalized(String* string, UChar previous) {
  if (string->IsNull())
    return;

  unsigned length = string->length();
  const StringImpl& input = *string->Impl();

  if (length >= std::numeric_limits<unsigned>::max())
    CRASH();

  StringBuffer<UChar> string_with_previous(length + 1);
  string_with_previous[0] =
      previous == kNoBreakSpaceCharacter ? kSpaceCharacter : previous;
  for (unsigned i = 1; i < length + 1; i++) {
    // Replace &nbsp with a real space since ICU no longer treats &nbsp as a
    // word separator.
    if (input[i - 1] == kNoBreakSpaceCharacter)
      string_with_previous[i] = kSpaceCharacter;
    else
      string_with_previous[i] = input[i - 1];
  }

  TextBreakIterator* boundary =
      WordBreakIterator(string_with_previous.Characters(), length + 1);
  if (!boundary)
    return;

  StringBuilder result;
  result.ReserveCapacity(length);

  int32_t end_of_word;
  int32_t start_of_word = boundary->first();
  for (end_of_word = boundary->next(); end_of_word != kTextBreakDone;
       start_of_word = end_of_word, end_of_word = boundary->next()) {
    if (start_of_word) {  // Ignore first char of previous string
      result.Append(
          input[start_of_word - 1] == kNoBreakSpaceCharacter
              ? kNoBreakSpaceCharacter
              : WTF::Unicode::ToTitleCase(string_with_previous[start_of_word]));
    }
    for (int i = start_of_word + 1; i < end_of_word; i++)
      result.Append(input[i - 1]);
  }

  *string = result.ToString();
}

LayoutText::LayoutText(Node* node, PassRefPtr<StringImpl> str)
    : LayoutObject(node),
      has_tab_(false),
      lines_dirty_(false),
      contains_reversed_text_(false),
      known_to_have_no_overflow_and_no_fallback_fonts_(false),
      min_width_(-1),
      max_width_(-1),
      first_line_min_width_(0),
      last_line_line_min_width_(0),
      text_(std::move(str)),
      first_text_box_(nullptr),
      last_text_box_(nullptr) {
  DCHECK(text_);
  DCHECK(!node || !node->IsDocumentNode());

  SetIsText();

  if (node)
    GetFrameView()->IncrementVisuallyNonEmptyCharacterCount(text_.length());
}

#if DCHECK_IS_ON()

LayoutText::~LayoutText() {
  DCHECK(!first_text_box_);
  DCHECK(!last_text_box_);
}

#endif

LayoutText* LayoutText::CreateEmptyAnonymous(Document& doc) {
  LayoutText* text = new LayoutText(nullptr, StringImpl::empty_);
  text->SetDocumentForAnonymous(&doc);
  return text;
}

bool LayoutText::IsTextFragment() const {
  return false;
}

bool LayoutText::IsWordBreak() const {
  return false;
}

void LayoutText::StyleDidChange(StyleDifference diff,
                                const ComputedStyle* old_style) {
  // There is no need to ever schedule paint invalidations from a style change
  // of a text run, since we already did this for the parent of the text run.
  // We do have to schedule layouts, though, since a style change can force us
  // to need to relayout.
  if (diff.NeedsFullLayout()) {
    SetNeedsLayoutAndPrefWidthsRecalc(LayoutInvalidationReason::kStyleChange);
    known_to_have_no_overflow_and_no_fallback_fonts_ = false;
  }

  const ComputedStyle& new_style = StyleRef();
  ETextTransform old_transform =
      old_style ? old_style->TextTransform() : ETextTransform::kNone;
  ETextSecurity old_security =
      old_style ? old_style->TextSecurity() : ETextSecurity::kNone;
  if (old_transform != new_style.TextTransform() ||
      old_security != new_style.TextSecurity())
    TransformText();

  // This is an optimization that kicks off font load before layout.
  if (!GetText().ContainsOnlyWhitespace())
    new_style.GetFont().WillUseFontData(GetText());

  TextAutosizer* text_autosizer = GetDocument().GetTextAutosizer();
  if (!old_style && text_autosizer)
    text_autosizer->Record(this);
}

void LayoutText::RemoveAndDestroyTextBoxes() {
  if (!DocumentBeingDestroyed()) {
    if (FirstTextBox()) {
      if (IsBR()) {
        RootInlineBox* next = FirstTextBox()->Root().NextRootBox();
        if (next)
          next->MarkDirty();
      }
      for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox())
        box->Remove();
    } else if (Parent()) {
      Parent()->DirtyLinesFromChangedChild(this);
    }
  }
  DeleteTextBoxes();
}

void LayoutText::WillBeDestroyed() {
  if (SecureTextTimer* secure_text_timer =
          g_secure_text_timers ? g_secure_text_timers->Take(this) : 0)
    delete secure_text_timer;

  RemoveAndDestroyTextBoxes();
  LayoutObject::WillBeDestroyed();
}

void LayoutText::ExtractTextBox(InlineTextBox* box) {
  last_text_box_ = box->PrevTextBox();
  if (box == first_text_box_)
    first_text_box_ = nullptr;
  if (box->PrevTextBox())
    box->PrevTextBox()->SetNextTextBox(nullptr);
  box->SetPreviousTextBox(nullptr);
  for (InlineTextBox* curr = box; curr; curr = curr->NextTextBox())
    curr->SetExtracted();
}

void LayoutText::AttachTextBox(InlineTextBox* box) {
  if (last_text_box_) {
    last_text_box_->SetNextTextBox(box);
    box->SetPreviousTextBox(last_text_box_);
  } else {
    first_text_box_ = box;
  }
  InlineTextBox* last = box;
  for (InlineTextBox* curr = box; curr; curr = curr->NextTextBox()) {
    curr->SetExtracted(false);
    last = curr;
  }
  last_text_box_ = last;
}

void LayoutText::RemoveTextBox(InlineTextBox* box) {
  if (box == first_text_box_)
    first_text_box_ = box->NextTextBox();
  if (box == last_text_box_)
    last_text_box_ = box->PrevTextBox();
  if (box->NextTextBox())
    box->NextTextBox()->SetPreviousTextBox(box->PrevTextBox());
  if (box->PrevTextBox())
    box->PrevTextBox()->SetNextTextBox(box->NextTextBox());
}

void LayoutText::DeleteTextBoxes() {
  if (FirstTextBox()) {
    InlineTextBox* next;
    for (InlineTextBox* curr = FirstTextBox(); curr; curr = next) {
      next = curr->NextTextBox();
      curr->Destroy();
    }
    first_text_box_ = last_text_box_ = nullptr;
  }
}

PassRefPtr<StringImpl> LayoutText::OriginalText() const {
  Node* e = GetNode();
  return (e && e->IsTextNode()) ? ToText(e)->DataImpl() : 0;
}

String LayoutText::PlainText() const {
  if (GetNode())
    return blink::PlainText(EphemeralRange::RangeOfContents(*GetNode()));

  // FIXME: this is just a stopgap until TextIterator is adapted to support
  // generated text.
  StringBuilder plain_text_builder;
  for (InlineTextBox* text_box = FirstTextBox(); text_box;
       text_box = text_box->NextTextBox()) {
    String text = text_.Substring(text_box->Start(), text_box->Len())
                      .SimplifyWhiteSpace(WTF::kDoNotStripWhiteSpace);
    plain_text_builder.Append(text);
    if (text_box->NextTextBox() &&
        text_box->NextTextBox()->Start() > text_box->end() && text.length() &&
        !text.Right(1).ContainsOnlyWhitespace())
      plain_text_builder.Append(kSpaceCharacter);
  }
  return plain_text_builder.ToString();
}

void LayoutText::AbsoluteRects(Vector<IntRect>& rects,
                               const LayoutPoint& accumulated_offset) const {
  for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox()) {
    rects.push_back(EnclosingIntRect(LayoutRect(
        LayoutPoint(accumulated_offset) + box->Location(), box->Size())));
  }
}

static FloatRect LocalQuadForTextBox(InlineTextBox* box,
                                     unsigned start,
                                     unsigned end) {
  unsigned real_end = std::min(box->end() + 1, end);
  LayoutRect r = box->LocalSelectionRect(start, real_end);
  if (r.Height()) {
    // Change the height and y position (or width and x for vertical text)
    // because selectionRect uses selection-specific values.
    if (box->IsHorizontal()) {
      r.SetHeight(box->Height());
      r.SetY(box->Y());
    } else {
      r.SetWidth(box->Width());
      r.SetX(box->X());
    }
    return FloatRect(r);
  }
  return FloatRect();
}

void LayoutText::AbsoluteRectsForRange(Vector<IntRect>& rects,
                                       unsigned start,
                                       unsigned end) const {
  // Work around signed/unsigned issues. This function takes unsigneds, and is
  // often passed UINT_MAX to mean "all the way to the end". InlineTextBox
  // coordinates are unsigneds, so changing this function to take ints causes
  // various internal mismatches. But selectionRect takes ints, and passing
  // UINT_MAX to it causes trouble. Ideally we'd change selectionRect to take
  // unsigneds, but that would cause many ripple effects, so for now we'll just
  // clamp our unsigned parameters to INT_MAX.
  DCHECK(end == UINT_MAX || end <= INT_MAX);
  DCHECK_LE(start, static_cast<unsigned>(INT_MAX));
  start = std::min(start, static_cast<unsigned>(INT_MAX));
  end = std::min(end, static_cast<unsigned>(INT_MAX));

  // This function is always called in sequence that this check should work.
  bool has_checked_box_in_range = !rects.IsEmpty();

  for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox()) {
    // Note: box->end() returns the index of the last character, not the index
    // past it
    if (start <= box->Start() && box->end() < end) {
      FloatRect r(box->FrameRect());
      if (!has_checked_box_in_range) {
        has_checked_box_in_range = true;
        rects.clear();
      }
      rects.push_back(LocalToAbsoluteQuad(r).EnclosingBoundingBox());
    } else if ((box->Start() <= start && start <= box->end()) ||
               (box->Start() < end && end <= box->end())) {
      FloatRect rect = LocalQuadForTextBox(box, start, end);
      if (!rect.IsZero()) {
        if (!has_checked_box_in_range) {
          has_checked_box_in_range = true;
          rects.clear();
        }
        rects.push_back(LocalToAbsoluteQuad(rect).EnclosingBoundingBox());
      }
    } else if (!has_checked_box_in_range) {
      // FIXME: This code is wrong. It's converting local to absolute twice.
      // http://webkit.org/b/65722
      FloatRect rect = LocalQuadForTextBox(box, start, end);
      if (!rect.IsZero())
        rects.push_back(LocalToAbsoluteQuad(rect).EnclosingBoundingBox());
    }
  }
}

static IntRect EllipsisRectForBox(InlineTextBox* box,
                                  unsigned start_pos,
                                  unsigned end_pos) {
  if (!box)
    return IntRect();

  unsigned short truncation = box->Truncation();
  if (truncation == kCNoTruncation)
    return IntRect();

  IntRect rect;
  if (EllipsisBox* ellipsis = box->Root().GetEllipsisBox()) {
    int ellipsis_start_position = std::max<int>(start_pos - box->Start(), 0);
    int ellipsis_end_position =
        std::min<int>(end_pos - box->Start(), box->Len());

    // The ellipsis should be considered to be selected if the end of the
    // selection is past the beginning of the truncation and the beginning of
    // the selection is before or at the beginning of the truncation.
    if (ellipsis_end_position >= truncation &&
        ellipsis_start_position <= truncation)
      return ellipsis->SelectionRect();
  }

  return IntRect();
}

void LayoutText::Quads(Vector<FloatQuad>& quads,
                       ClippingOption option,
                       LocalOrAbsoluteOption local_or_absolute,
                       MapCoordinatesFlags mode) const {
  for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox()) {
    FloatRect boundaries(box->FrameRect());

    // Shorten the width of this text box if it ends in an ellipsis.
    // FIXME: ellipsisRectForBox should switch to return FloatRect soon with the
    // subpixellayout branch.
    IntRect ellipsis_rect = (option == kClipToEllipsis)
                                ? EllipsisRectForBox(box, 0, TextLength())
                                : IntRect();
    if (!ellipsis_rect.IsEmpty()) {
      if (Style()->IsHorizontalWritingMode())
        boundaries.SetWidth(ellipsis_rect.MaxX() - boundaries.X());
      else
        boundaries.SetHeight(ellipsis_rect.MaxY() - boundaries.Y());
    }
    if (local_or_absolute == kAbsoluteQuads)
      quads.push_back(LocalToAbsoluteQuad(boundaries, mode));
    else
      quads.push_back(boundaries);
  }
}

void LayoutText::AbsoluteQuads(Vector<FloatQuad>& quads,
                               MapCoordinatesFlags mode) const {
  this->Quads(quads, kNoClipping, kAbsoluteQuads, mode);
}

void LayoutText::AbsoluteQuadsForRange(Vector<FloatQuad>& quads,
                                       unsigned start,
                                       unsigned end) const {
  // Work around signed/unsigned issues. This function takes unsigneds, and is
  // often passed UINT_MAX to mean "all the way to the end". InlineTextBox
  // coordinates are unsigneds, so changing this function to take ints causes
  // various internal mismatches. But selectionRect takes ints, and passing
  // UINT_MAX to it causes trouble. Ideally we'd change selectionRect to take
  // unsigneds, but that would cause many ripple effects, so for now we'll just
  // clamp our unsigned parameters to INT_MAX.
  DCHECK(end == UINT_MAX || end <= INT_MAX);
  DCHECK_LE(start, static_cast<unsigned>(INT_MAX));
  start = std::min(start, static_cast<unsigned>(INT_MAX));
  end = std::min(end, static_cast<unsigned>(INT_MAX));

  const unsigned caret_min_offset =
      static_cast<unsigned>(this->CaretMinOffset());
  const unsigned caret_max_offset =
      static_cast<unsigned>(this->CaretMaxOffset());

  // Narrows |start| and |end| into |caretMinOffset| and |careMaxOffset|
  // to ignore unrendered leading and trailing whitespaces.
  start = std::min(std::max(caret_min_offset, start), caret_max_offset);
  end = std::min(std::max(caret_min_offset, end), caret_max_offset);

  // This function is always called in sequence that this check should work.
  bool has_checked_box_in_range = !quads.IsEmpty();

  for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox()) {
    // Note: box->end() returns the index of the last character, not the index
    // past it
    if (start <= box->Start() && box->end() < end) {
      LayoutRect r(box->FrameRect());
      if (!has_checked_box_in_range) {
        has_checked_box_in_range = true;
        quads.clear();
      }
      quads.push_back(LocalToAbsoluteQuad(FloatRect(r)));
    } else if ((box->Start() <= start && start <= box->end()) ||
               (box->Start() < end && end <= box->end())) {
      FloatRect rect = LocalQuadForTextBox(box, start, end);
      if (!rect.IsZero()) {
        if (!has_checked_box_in_range) {
          has_checked_box_in_range = true;
          quads.clear();
        }
        quads.push_back(LocalToAbsoluteQuad(rect));
      }
    } else if (!has_checked_box_in_range) {
      // consider when the offset of range is area of leading or trailing
      // whitespace
      FloatRect rect = LocalQuadForTextBox(box, start, end);
      if (!rect.IsZero())
        quads.push_back(LocalToAbsoluteQuad(rect).EnclosingBoundingBox());
    }
  }
}

FloatRect LayoutText::LocalBoundingBoxRectForAccessibility() const {
  FloatRect result;
  Vector<FloatQuad> quads;
  this->Quads(quads, LayoutText::kClipToEllipsis, LayoutText::kLocalQuads);
  for (const FloatQuad& quad : quads)
    result.Unite(quad.BoundingBox());
  return result;
}

enum ShouldAffinityBeDownstream {
  kAlwaysDownstream,
  kAlwaysUpstream,
  kUpstreamIfPositionIsNotAtStart
};

static bool LineDirectionPointFitsInBox(
    int point_line_direction,
    InlineTextBox* box,
    ShouldAffinityBeDownstream& should_affinity_be_downstream) {
  should_affinity_be_downstream = kAlwaysDownstream;

  // the x coordinate is equal to the left edge of this box the affinity must be
  // downstream so the position doesn't jump back to the previous line except
  // when box is the first box in the line
  if (point_line_direction <= box->LogicalLeft()) {
    should_affinity_be_downstream = !box->PrevLeafChild()
                                        ? kUpstreamIfPositionIsNotAtStart
                                        : kAlwaysDownstream;
    return true;
  }

  // and the x coordinate is to the left of the right edge of this box
  // check to see if position goes in this box
  if (point_line_direction < box->LogicalRight()) {
    should_affinity_be_downstream = kUpstreamIfPositionIsNotAtStart;
    return true;
  }

  // box is first on line
  // and the x coordinate is to the left of the first text box left edge
  if (!box->PrevLeafChildIgnoringLineBreak() &&
      point_line_direction < box->LogicalLeft())
    return true;

  if (!box->NextLeafChildIgnoringLineBreak()) {
    // box is last on line and the x coordinate is to the right of the last text
    // box right edge generate VisiblePosition, use TextAffinity::Upstream
    // affinity if possible
    should_affinity_be_downstream = kUpstreamIfPositionIsNotAtStart;
    return true;
  }

  return false;
}

static PositionWithAffinity CreatePositionWithAffinityForBox(
    const InlineBox* box,
    int offset,
    ShouldAffinityBeDownstream should_affinity_be_downstream) {
  TextAffinity affinity = VP_DEFAULT_AFFINITY;
  switch (should_affinity_be_downstream) {
    case kAlwaysDownstream:
      affinity = TextAffinity::kDownstream;
      break;
    case kAlwaysUpstream:
      affinity = VP_UPSTREAM_IF_POSSIBLE;
      break;
    case kUpstreamIfPositionIsNotAtStart:
      affinity = offset > box->CaretMinOffset() ? VP_UPSTREAM_IF_POSSIBLE
                                                : TextAffinity::kDownstream;
      break;
  }
  int text_start_offset =
      box->GetLineLayoutItem().IsText()
          ? LineLayoutText(box->GetLineLayoutItem()).TextStartOffset()
          : 0;
  return box->GetLineLayoutItem().CreatePositionWithAffinity(
      offset + text_start_offset, affinity);
}

static PositionWithAffinity
CreatePositionWithAffinityForBoxAfterAdjustingOffsetForBiDi(
    const InlineTextBox* box,
    int offset,
    ShouldAffinityBeDownstream should_affinity_be_downstream) {
  DCHECK(box);
  DCHECK_GE(offset, 0);

  if (offset && static_cast<unsigned>(offset) < box->Len())
    return CreatePositionWithAffinityForBox(box, box->Start() + offset,
                                            should_affinity_be_downstream);

  bool position_is_at_start_of_box = !offset;
  if (position_is_at_start_of_box == box->IsLeftToRightDirection()) {
    // offset is on the left edge

    const InlineBox* prev_box = box->PrevLeafChildIgnoringLineBreak();
    if ((prev_box && prev_box->BidiLevel() == box->BidiLevel()) ||
        box->GetLineLayoutItem().ContainingBlock().Style()->Direction() ==
            box->Direction())  // FIXME: left on 12CBA
      return CreatePositionWithAffinityForBox(box, box->CaretLeftmostOffset(),
                                              should_affinity_be_downstream);

    if (prev_box && prev_box->BidiLevel() > box->BidiLevel()) {
      // e.g. left of B in aDC12BAb
      const InlineBox* leftmost_box;
      do {
        leftmost_box = prev_box;
        prev_box = leftmost_box->PrevLeafChildIgnoringLineBreak();
      } while (prev_box && prev_box->BidiLevel() > box->BidiLevel());
      return CreatePositionWithAffinityForBox(
          leftmost_box, leftmost_box->CaretRightmostOffset(),
          should_affinity_be_downstream);
    }

    if (!prev_box || prev_box->BidiLevel() < box->BidiLevel()) {
      // e.g. left of D in aDC12BAb
      const InlineBox* rightmost_box;
      const InlineBox* next_box = box;
      do {
        rightmost_box = next_box;
        next_box = rightmost_box->NextLeafChildIgnoringLineBreak();
      } while (next_box && next_box->BidiLevel() >= box->BidiLevel());
      return CreatePositionWithAffinityForBox(
          rightmost_box,
          box->IsLeftToRightDirection() ? rightmost_box->CaretMaxOffset()
                                        : rightmost_box->CaretMinOffset(),
          should_affinity_be_downstream);
    }

    return CreatePositionWithAffinityForBox(box, box->CaretRightmostOffset(),
                                            should_affinity_be_downstream);
  }

  const InlineBox* next_box = box->NextLeafChildIgnoringLineBreak();
  if ((next_box && next_box->BidiLevel() == box->BidiLevel()) ||
      box->GetLineLayoutItem().ContainingBlock().Style()->Direction() ==
          box->Direction())
    return CreatePositionWithAffinityForBox(box, box->CaretRightmostOffset(),
                                            should_affinity_be_downstream);

  // offset is on the right edge
  if (next_box && next_box->BidiLevel() > box->BidiLevel()) {
    // e.g. right of C in aDC12BAb
    const InlineBox* rightmost_box;
    do {
      rightmost_box = next_box;
      next_box = rightmost_box->NextLeafChildIgnoringLineBreak();
    } while (next_box && next_box->BidiLevel() > box->BidiLevel());
    return CreatePositionWithAffinityForBox(
        rightmost_box, rightmost_box->CaretLeftmostOffset(),
        should_affinity_be_downstream);
  }

  if (!next_box || next_box->BidiLevel() < box->BidiLevel()) {
    // e.g. right of A in aDC12BAb
    const InlineBox* leftmost_box;
    const InlineBox* prev_box = box;
    do {
      leftmost_box = prev_box;
      prev_box = leftmost_box->PrevLeafChildIgnoringLineBreak();
    } while (prev_box && prev_box->BidiLevel() >= box->BidiLevel());
    return CreatePositionWithAffinityForBox(
        leftmost_box,
        box->IsLeftToRightDirection() ? leftmost_box->CaretMinOffset()
                                      : leftmost_box->CaretMaxOffset(),
        should_affinity_be_downstream);
  }

  return CreatePositionWithAffinityForBox(box, box->CaretLeftmostOffset(),
                                          should_affinity_be_downstream);
}

PositionWithAffinity LayoutText::PositionForPoint(const LayoutPoint& point) {
  if (!FirstTextBox() || TextLength() == 0)
    return CreatePositionWithAffinity(0);

  LayoutUnit point_line_direction =
      FirstTextBox()->IsHorizontal() ? point.X() : point.Y();
  LayoutUnit point_block_direction =
      FirstTextBox()->IsHorizontal() ? point.Y() : point.X();
  bool blocks_are_flipped = Style()->IsFlippedBlocksWritingMode();

  InlineTextBox* last_box = nullptr;
  for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox()) {
    if (box->IsLineBreak() && !box->PrevLeafChild() && box->NextLeafChild() &&
        !box->NextLeafChild()->IsLineBreak())
      box = box->NextTextBox();

    RootInlineBox& root_box = box->Root();
    LayoutUnit top = std::min(root_box.SelectionTop(), root_box.LineTop());
    if (point_block_direction > top ||
        (!blocks_are_flipped && point_block_direction == top)) {
      LayoutUnit bottom = root_box.SelectionBottom();
      if (root_box.NextRootBox())
        bottom = std::min(bottom, root_box.NextRootBox()->LineTop());

      if (point_block_direction < bottom ||
          (blocks_are_flipped && point_block_direction == bottom)) {
        ShouldAffinityBeDownstream should_affinity_be_downstream;
        if (LineDirectionPointFitsInBox(point_line_direction.ToInt(), box,
                                        should_affinity_be_downstream))
          return CreatePositionWithAffinityForBoxAfterAdjustingOffsetForBiDi(
              box, box->OffsetForPosition(point_line_direction),
              should_affinity_be_downstream);
      }
    }
    last_box = box;
  }

  if (last_box) {
    ShouldAffinityBeDownstream should_affinity_be_downstream;
    LineDirectionPointFitsInBox(point_line_direction.ToInt(), last_box,
                                should_affinity_be_downstream);
    return CreatePositionWithAffinityForBoxAfterAdjustingOffsetForBiDi(
        last_box,
        last_box->OffsetForPosition(point_line_direction) + last_box->Start(),
        should_affinity_be_downstream);
  }
  return CreatePositionWithAffinity(0);
}

LayoutRect LayoutText::LocalCaretRect(InlineBox* inline_box,
                                      int caret_offset,
                                      LayoutUnit* extra_width_to_end_of_line) {
  if (!inline_box)
    return LayoutRect();

  DCHECK(inline_box->IsInlineTextBox());
  if (!inline_box->IsInlineTextBox())
    return LayoutRect();

  InlineTextBox* box = ToInlineTextBox(inline_box);
  // Find an InlineBox before caret position, which is used to get caret height.
  InlineBox* caret_box = box;
  if (box->GetLineLayoutItem().Style(box->IsFirstLineStyle())->Direction() ==
      TextDirection::kLtr) {
    if (box->PrevLeafChild() && caret_offset == 0)
      caret_box = box->PrevLeafChild();
  } else {
    if (box->NextLeafChild() && caret_offset == 0)
      caret_box = box->NextLeafChild();
  }

  // Get caret height from a font of character.
  const ComputedStyle* style_to_use =
      caret_box->GetLineLayoutItem().Style(caret_box->IsFirstLineStyle());
  if (!style_to_use->GetFont().PrimaryFont())
    return LayoutRect();

  int height = style_to_use->GetFont().PrimaryFont()->GetFontMetrics().Height();
  int top = caret_box->LogicalTop().ToInt();

  // Go ahead and round left to snap it to the nearest pixel.
  LayoutUnit left = box->PositionForOffset(caret_offset);
  LayoutUnit caret_width = GetFrameView()->CaretWidth();

  // Distribute the caret's width to either side of the offset.
  LayoutUnit caret_width_left_of_offset = caret_width / 2;
  left -= caret_width_left_of_offset;
  LayoutUnit caret_width_right_of_offset =
      caret_width - caret_width_left_of_offset;

  left = LayoutUnit(left.Round());

  LayoutUnit root_left = box->Root().LogicalLeft();
  LayoutUnit root_right = box->Root().LogicalRight();

  // FIXME: should we use the width of the root inline box or the
  // width of the containing block for this?
  if (extra_width_to_end_of_line)
    *extra_width_to_end_of_line =
        (box->Root().LogicalWidth() + root_left) - (left + 1);

  LayoutBlock* cb = ContainingBlock();
  const ComputedStyle& cb_style = cb->StyleRef();

  LayoutUnit left_edge;
  LayoutUnit right_edge;
  left_edge = std::min(LayoutUnit(), root_left);
  right_edge = std::max(cb->LogicalWidth(), root_right);

  bool right_aligned = false;
  switch (cb_style.GetTextAlign()) {
    case ETextAlign::kRight:
    case ETextAlign::kWebkitRight:
      right_aligned = true;
      break;
    case ETextAlign::kLeft:
    case ETextAlign::kWebkitLeft:
    case ETextAlign::kCenter:
    case ETextAlign::kWebkitCenter:
      break;
    case ETextAlign::kJustify:
    case ETextAlign::kStart:
      right_aligned = !cb_style.IsLeftToRightDirection();
      break;
    case ETextAlign::kEnd:
      right_aligned = cb_style.IsLeftToRightDirection();
      break;
  }

  // for unicode-bidi: plaintext, use inlineBoxBidiLevel() to test the correct
  // direction for the cursor.
  if (right_aligned && Style()->GetUnicodeBidi() == UnicodeBidi::kPlaintext) {
    if (inline_box->BidiLevel() % 2 != 1)
      right_aligned = false;
  }

  if (right_aligned) {
    left = std::max(left, left_edge);
    left = std::min(left, root_right - caret_width);
  } else {
    left = std::min(left, right_edge - caret_width_right_of_offset);
    left = std::max(left, root_left);
  }

  return LayoutRect(
      Style()->IsHorizontalWritingMode()
          ? IntRect(left.ToInt(), top, caret_width.ToInt(), height)
          : IntRect(top, left.ToInt(), height, caret_width.ToInt()));
}

ALWAYS_INLINE float LayoutText::WidthFromFont(
    const Font& f,
    int start,
    int len,
    float lead_width,
    float text_width_so_far,
    TextDirection text_direction,
    HashSet<const SimpleFontData*>* fallback_fonts,
    FloatRect* glyph_bounds_accumulation) const {
  if (Style()->HasTextCombine() && IsCombineText()) {
    const LayoutTextCombine* combine_text = ToLayoutTextCombine(this);
    if (combine_text->IsCombined())
      return combine_text->CombinedTextWidth(f);
  }

  TextRun run =
      ConstructTextRun(f, this, start, len, StyleRef(), text_direction);
  run.SetCharactersLength(TextLength() - start);
  DCHECK_GE(run.CharactersLength(), run.length());
  run.SetTabSize(!Style()->CollapseWhiteSpace(), Style()->GetTabSize());
  run.SetXPos(lead_width + text_width_so_far);

  FloatRect new_glyph_bounds;
  float result =
      f.Width(run, fallback_fonts,
              glyph_bounds_accumulation ? &new_glyph_bounds : nullptr);
  if (glyph_bounds_accumulation) {
    new_glyph_bounds.Move(text_width_so_far, 0);
    glyph_bounds_accumulation->Unite(new_glyph_bounds);
  }
  return result;
}

void LayoutText::TrimmedPrefWidths(LayoutUnit lead_width_layout_unit,
                                   LayoutUnit& first_line_min_width,
                                   bool& has_breakable_start,
                                   LayoutUnit& last_line_min_width,
                                   bool& has_breakable_end,
                                   bool& has_breakable_char,
                                   bool& has_break,
                                   LayoutUnit& first_line_max_width,
                                   LayoutUnit& last_line_max_width,
                                   LayoutUnit& min_width,
                                   LayoutUnit& max_width,
                                   bool& strip_front_spaces,
                                   TextDirection direction) {
  float float_min_width = 0.0f, float_max_width = 0.0f;

  // Convert leadWidth to a float here, to avoid multiple implict conversions
  // below.
  float lead_width = lead_width_layout_unit.ToFloat();

  bool collapse_white_space = Style()->CollapseWhiteSpace();
  if (!collapse_white_space)
    strip_front_spaces = false;

  if (has_tab_ || PreferredLogicalWidthsDirty())
    ComputePreferredLogicalWidths(lead_width);

  has_breakable_start = !strip_front_spaces && has_breakable_start_;
  has_breakable_end = has_breakable_end_;

  int len = TextLength();

  if (!len ||
      (strip_front_spaces && GetText().Impl()->ContainsOnlyWhitespace())) {
    first_line_min_width = LayoutUnit();
    last_line_min_width = LayoutUnit();
    first_line_max_width = LayoutUnit();
    last_line_max_width = LayoutUnit();
    min_width = LayoutUnit();
    max_width = LayoutUnit();
    has_break = false;
    return;
  }

  float_min_width = min_width_;
  float_max_width = max_width_;

  first_line_min_width = LayoutUnit(first_line_min_width_);
  last_line_min_width = LayoutUnit(last_line_line_min_width_);

  has_breakable_char = has_breakable_char_;
  has_break = has_break_;

  DCHECK(text_);
  StringImpl& text = *text_.Impl();
  if (text[0] == kSpaceCharacter ||
      (text[0] == kNewlineCharacter && !Style()->PreserveNewline()) ||
      text[0] == kTabulationCharacter) {
    const Font& font = Style()->GetFont();  // FIXME: This ignores first-line.
    if (strip_front_spaces) {
      const UChar kSpaceChar = kSpaceCharacter;
      TextRun run =
          ConstructTextRun(font, &kSpaceChar, 1, StyleRef(), direction);
      float space_width = font.Width(run);
      float_max_width -= space_width;
    } else {
      float_max_width += font.GetFontDescription().WordSpacing();
    }
  }

  strip_front_spaces = collapse_white_space && has_end_white_space_;

  if (!Style()->AutoWrap() || float_min_width > float_max_width)
    float_min_width = float_max_width;

  // Compute our max widths by scanning the string for newlines.
  if (has_break) {
    const Font& f = Style()->GetFont();  // FIXME: This ignores first-line.
    bool first_line = true;
    first_line_max_width = LayoutUnit(float_max_width);
    last_line_max_width = LayoutUnit(float_max_width);
    for (int i = 0; i < len; i++) {
      int linelen = 0;
      while (i + linelen < len && text[i + linelen] != kNewlineCharacter)
        linelen++;

      if (linelen) {
        last_line_max_width = LayoutUnit(WidthFromFont(
            f, i, linelen, lead_width, last_line_max_width.ToFloat(), direction,
            nullptr, nullptr));
        if (first_line) {
          first_line = false;
          lead_width = 0.f;
          first_line_max_width = last_line_max_width;
        }
        i += linelen;
      } else if (first_line) {
        first_line_max_width = LayoutUnit();
        first_line = false;
        lead_width = 0.f;
      }

      if (i == len - 1) {
        // A <pre> run that ends with a newline, as in, e.g.,
        // <pre>Some text\n\n<span>More text</pre>
        last_line_max_width = LayoutUnit();
      }
    }
  }

  min_width = LayoutUnit::FromFloatCeil(float_min_width);
  max_width = LayoutUnit::FromFloatCeil(float_max_width);
}

float LayoutText::MinLogicalWidth() const {
  if (PreferredLogicalWidthsDirty())
    const_cast<LayoutText*>(this)->ComputePreferredLogicalWidths(0);

  return min_width_;
}

float LayoutText::MaxLogicalWidth() const {
  if (PreferredLogicalWidthsDirty())
    const_cast<LayoutText*>(this)->ComputePreferredLogicalWidths(0);

  return max_width_;
}

void LayoutText::ComputePreferredLogicalWidths(float lead_width) {
  HashSet<const SimpleFontData*> fallback_fonts;
  FloatRect glyph_bounds;
  ComputePreferredLogicalWidths(lead_width, fallback_fonts, glyph_bounds);
}

static float MinWordFragmentWidthForBreakAll(
    LayoutText* layout_text,
    const ComputedStyle& style,
    const Font& font,
    TextDirection text_direction,
    int start,
    int length,
    EWordBreak break_all_or_break_word) {
  DCHECK_GT(length, 0);
  LazyLineBreakIterator break_iterator(layout_text->GetText(),
                                       style.LocaleForLineBreakIterator());
  int next_breakable = -1;
  float min = std::numeric_limits<float>::max();
  int end = start + length;
  for (int i = start; i < end;) {
    int fragment_length;
    if (break_all_or_break_word == EWordBreak::kBreakAll) {
      break_iterator.IsBreakable(i + 1, next_breakable,
                                 LineBreakType::kBreakAll);
      fragment_length = (next_breakable > i ? next_breakable : length) - i;
    } else {
      fragment_length = U16_LENGTH(layout_text->CodepointAt(i));
    }

    // Ensure that malformed surrogate pairs don't cause us to read
    // past the end of the string.
    int text_length = layout_text->TextLength();
    if (i + fragment_length > text_length)
      fragment_length = std::max(text_length - i, 0);

    // The correct behavior is to measure width without re-shaping, but we
    // reshape each fragment here because a) the current line breaker does not
    // support it, b) getCharacterRange() can reshape if the text is too long
    // to fit in the cache, and c) each fragment here is almost 1 char and thus
    // reshape is fast.
    TextRun run = ConstructTextRun(font, layout_text, i, fragment_length, style,
                                   text_direction);
    float fragment_width = font.Width(run);
    min = std::min(min, fragment_width);
    i += fragment_length;
  }
  return min;
}

static float MaxWordFragmentWidth(LayoutText* layout_text,
                                  const ComputedStyle& style,
                                  const Font& font,
                                  TextDirection text_direction,
                                  Hyphenation& hyphenation,
                                  unsigned word_offset,
                                  unsigned word_length,
                                  int& suffix_start) {
  suffix_start = 0;
  if (word_length <= Hyphenation::kMinimumSuffixLength)
    return 0;

  Vector<size_t, 8> hyphen_locations = hyphenation.HyphenLocations(
      StringView(layout_text->GetText(), word_offset, word_length));
  if (hyphen_locations.IsEmpty())
    return 0;

  float minimum_fragment_width_to_consider =
      Hyphenation::MinimumPrefixWidth(font);
  float max_fragment_width = 0;
  TextRun run = ConstructTextRun(font, layout_text, word_offset, word_length,
                                 style, text_direction);
  size_t end = word_length;
  for (size_t start : hyphen_locations) {
    float fragment_width = font.GetCharacterRange(run, start, end).Width();

    if (fragment_width <= minimum_fragment_width_to_consider)
      continue;

    max_fragment_width = std::max(max_fragment_width, fragment_width);
    end = start;
  }
  suffix_start = hyphen_locations.front();
  return max_fragment_width + layout_text->HyphenWidth(font, text_direction);
}

void LayoutText::ComputePreferredLogicalWidths(
    float lead_width,
    HashSet<const SimpleFontData*>& fallback_fonts,
    FloatRect& glyph_bounds) {
  DCHECK(has_tab_ || PreferredLogicalWidthsDirty() ||
         !known_to_have_no_overflow_and_no_fallback_fonts_);

  min_width_ = 0;
  max_width_ = 0;
  first_line_min_width_ = 0;
  last_line_line_min_width_ = 0;

  if (IsBR())
    return;

  float curr_min_width = 0;
  float curr_max_width = 0;
  has_breakable_char_ = false;
  has_break_ = false;
  has_tab_ = false;
  has_breakable_start_ = false;
  has_breakable_end_ = false;
  has_end_white_space_ = false;

  const ComputedStyle& style_to_use = StyleRef();
  const Font& f = style_to_use.GetFont();  // FIXME: This ignores first-line.
  float word_spacing = style_to_use.WordSpacing();
  int len = TextLength();
  LazyLineBreakIterator break_iterator(
      text_, style_to_use.LocaleForLineBreakIterator());
  bool needs_word_spacing = false;
  bool ignoring_spaces = false;
  bool is_space = false;
  bool first_word = true;
  bool first_line = true;
  int next_breakable = -1;
  int last_word_boundary = 0;
  float cached_word_trailing_space_width[2] = {0, 0};  // LTR, RTL

  EWordBreak break_all_or_break_word = EWordBreak::kNormal;
  LineBreakType line_break_type = LineBreakType::kNormal;
  if (style_to_use.AutoWrap()) {
    if (style_to_use.WordBreak() == EWordBreak::kBreakAll ||
        style_to_use.WordBreak() == EWordBreak::kBreakWord) {
      break_all_or_break_word = style_to_use.WordBreak();
    } else if (style_to_use.WordBreak() == EWordBreak::kKeepAll) {
      line_break_type = LineBreakType::kKeepAll;
    }
  }

  Hyphenation* hyphenation =
      style_to_use.AutoWrap() ? style_to_use.GetHyphenation() : nullptr;
  bool disable_soft_hyphen = style_to_use.GetHyphens() == Hyphens::kNone;
  float max_word_width = 0;
  if (!hyphenation)
    max_word_width = std::numeric_limits<float>::infinity();

  BidiResolver<TextRunIterator, BidiCharacterRun> bidi_resolver;
  BidiCharacterRun* run;
  TextDirection text_direction = style_to_use.Direction();
  if ((Is8Bit() && text_direction == TextDirection::kLtr) ||
      IsOverride(style_to_use.GetUnicodeBidi())) {
    run = 0;
  } else {
    TextRun text_run(GetText());
    BidiStatus status(text_direction, false);
    bidi_resolver.SetStatus(status);
    bidi_resolver.SetPositionIgnoringNestedIsolates(
        TextRunIterator(&text_run, 0));
    bool hard_line_break = false;
    bool reorder_runs = false;
    bidi_resolver.CreateBidiRunsForLine(
        TextRunIterator(&text_run, text_run.length()), kNoVisualOverride,
        hard_line_break, reorder_runs);
    BidiRunList<BidiCharacterRun>& bidi_runs = bidi_resolver.Runs();
    run = bidi_runs.FirstRun();
  }

  for (int i = 0; i < len; i++) {
    UChar c = UncheckedCharacterAt(i);

    if (run) {
      // Treat adjacent runs with the same resolved directionality
      // (TextDirection as opposed to WTF::Unicode::Direction) as belonging
      // to the same run to avoid breaking unnecessarily.
      while (i >= run->Stop() ||
             (run->Next() && run->Next()->Direction() == run->Direction()))
        run = run->Next();

      DCHECK(run);
      DCHECK_LE(i, run->Stop());
      text_direction = run->Direction();
    }

    bool previous_character_is_space = is_space;
    bool is_newline = false;
    if (c == kNewlineCharacter) {
      if (style_to_use.PreserveNewline()) {
        has_break_ = true;
        is_newline = true;
        is_space = false;
      } else {
        is_space = true;
      }
    } else if (c == kTabulationCharacter) {
      if (!style_to_use.CollapseWhiteSpace()) {
        has_tab_ = true;
        is_space = false;
      } else {
        is_space = true;
      }
    } else {
      is_space = c == kSpaceCharacter;
    }

    bool is_breakable_location =
        is_newline || (is_space && style_to_use.AutoWrap());
    if (!i)
      has_breakable_start_ = is_breakable_location;
    if (i == len - 1) {
      has_breakable_end_ = is_breakable_location;
      has_end_white_space_ = is_newline || is_space;
    }

    if (!ignoring_spaces && style_to_use.CollapseWhiteSpace() &&
        previous_character_is_space && is_space)
      ignoring_spaces = true;

    if (ignoring_spaces && !is_space)
      ignoring_spaces = false;

    // Ignore spaces and soft hyphens
    if (ignoring_spaces) {
      DCHECK_EQ(last_word_boundary, i);
      last_word_boundary++;
      continue;
    }
    if (c == kSoftHyphenCharacter && !disable_soft_hyphen) {
      curr_max_width += WidthFromFont(
          f, last_word_boundary, i - last_word_boundary, lead_width,
          curr_max_width, text_direction, &fallback_fonts, &glyph_bounds);
      last_word_boundary = i + 1;
      continue;
    }

    bool has_break =
        break_iterator.IsBreakable(i, next_breakable, line_break_type);
    bool between_words = true;
    int j = i;
    while (c != kNewlineCharacter && c != kSpaceCharacter &&
           c != kTabulationCharacter &&
           (c != kSoftHyphenCharacter || disable_soft_hyphen)) {
      j++;
      if (j == len)
        break;
      c = UncheckedCharacterAt(j);
      if (break_iterator.IsBreakable(j, next_breakable) &&
          CharacterAt(j - 1) != kSoftHyphenCharacter)
        break;
    }

    // Terminate word boundary at bidi run boundary.
    if (run)
      j = std::min(j, run->Stop() + 1);
    int word_len = j - i;
    if (word_len) {
      bool is_space = (j < len) && c == kSpaceCharacter;

      // Non-zero only when kerning is enabled, in which case we measure words
      // with their trailing space, then subtract its width.
      float word_trailing_space_width = 0;
      if (is_space &&
          (f.GetFontDescription().GetTypesettingFeatures() & kKerning)) {
        const unsigned text_direction_index =
            static_cast<unsigned>(text_direction);
        DCHECK_GE(text_direction_index, 0U);
        DCHECK_LE(text_direction_index, 1U);
        if (!cached_word_trailing_space_width[text_direction_index])
          cached_word_trailing_space_width[text_direction_index] =
              f.Width(ConstructTextRun(f, &kSpaceCharacter, 1, style_to_use,
                                       text_direction)) +
              word_spacing;
        word_trailing_space_width =
            cached_word_trailing_space_width[text_direction_index];
      }

      float w;
      if (word_trailing_space_width && is_space) {
        w = WidthFromFont(f, i, word_len + 1, lead_width, curr_max_width,
                          text_direction, &fallback_fonts, &glyph_bounds) -
            word_trailing_space_width;
      } else {
        w = WidthFromFont(f, i, word_len, lead_width, curr_max_width,
                          text_direction, &fallback_fonts, &glyph_bounds);
        if (c == kSoftHyphenCharacter && !disable_soft_hyphen)
          curr_min_width += HyphenWidth(f, text_direction);
      }

      if (w > max_word_width) {
        DCHECK(hyphenation);
        int suffix_start;
        float max_fragment_width =
            MaxWordFragmentWidth(this, style_to_use, f, text_direction,
                                 *hyphenation, i, word_len, suffix_start);
        if (suffix_start) {
          float suffix_width;
          if (word_trailing_space_width && is_space)
            suffix_width =
                WidthFromFont(f, i + suffix_start, word_len - suffix_start + 1,
                              lead_width, curr_max_width, text_direction,
                              &fallback_fonts, &glyph_bounds) -
                word_trailing_space_width;
          else
            suffix_width = WidthFromFont(
                f, i + suffix_start, word_len - suffix_start, lead_width,
                curr_max_width, text_direction, &fallback_fonts, &glyph_bounds);
          max_fragment_width = std::max(max_fragment_width, suffix_width);
          curr_min_width += max_fragment_width - w;
          max_word_width = std::max(max_word_width, max_fragment_width);
        } else {
          max_word_width = w;
        }
      }

      if (break_all_or_break_word != EWordBreak::kNormal) {
        // Because sum of character widths may not be equal to the word width,
        // we need to measure twice; once with normal break for max width,
        // another with break-all for min width.
        curr_min_width = MinWordFragmentWidthForBreakAll(
            this, style_to_use, f, text_direction, i, word_len,
            break_all_or_break_word);
      } else {
        curr_min_width += w;
      }
      if (between_words) {
        if (last_word_boundary == i)
          curr_max_width += w;
        else
          curr_max_width += WidthFromFont(
              f, last_word_boundary, j - last_word_boundary, lead_width,
              curr_max_width, text_direction, &fallback_fonts, &glyph_bounds);
        last_word_boundary = j;
      }

      bool is_collapsible_white_space =
          (j < len) && style_to_use.IsCollapsibleWhiteSpace(c);
      if (j < len && style_to_use.AutoWrap())
        has_breakable_char_ = true;

      // Add in wordSpacing to our currMaxWidth, but not if this is the last
      // word on a line or the
      // last word in the run.
      if (word_spacing && (is_space || is_collapsible_white_space) &&
          !ContainsOnlyWhitespace(j, len - j))
        curr_max_width += word_spacing;

      if (first_word) {
        first_word = false;
        // If the first character in the run is breakable, then we consider
        // ourselves to have a beginning minimum width of 0, since a break could
        // occur right before our run starts, preventing us from ever being
        // appended to a previous text run when considering the total minimum
        // width of the containing block.
        if (has_break)
          has_breakable_char_ = true;
        first_line_min_width_ = has_break ? 0 : curr_min_width;
      }
      last_line_line_min_width_ = curr_min_width;

      if (curr_min_width > min_width_)
        min_width_ = curr_min_width;
      curr_min_width = 0;

      i += word_len - 1;
    } else {
      // Nowrap can never be broken, so don't bother setting the breakable
      // character boolean. Pre can only be broken if we encounter a newline.
      if (Style()->AutoWrap() || is_newline)
        has_breakable_char_ = true;

      if (curr_min_width > min_width_)
        min_width_ = curr_min_width;
      curr_min_width = 0;

      // Only set if preserveNewline was true and we saw a newline.
      if (is_newline) {
        if (first_line) {
          first_line = false;
          lead_width = 0;
          if (!style_to_use.AutoWrap())
            first_line_min_width_ = curr_max_width;
        }

        if (curr_max_width > max_width_)
          max_width_ = curr_max_width;
        curr_max_width = 0;
      } else {
        TextRun run =
            ConstructTextRun(f, this, i, 1, style_to_use, text_direction);
        run.SetCharactersLength(len - i);
        DCHECK_GE(run.CharactersLength(), run.length());
        run.SetTabSize(!Style()->CollapseWhiteSpace(), Style()->GetTabSize());
        run.SetXPos(lead_width + curr_max_width);

        curr_max_width += f.Width(run);
        needs_word_spacing =
            is_space && !previous_character_is_space && i == len - 1;
      }
      DCHECK_EQ(last_word_boundary, i);
      last_word_boundary++;
    }
  }
  if (run)
    bidi_resolver.Runs().DeleteRuns();

  if ((needs_word_spacing && len > 1) || (ignoring_spaces && !first_word))
    curr_max_width += word_spacing;

  min_width_ = std::max(curr_min_width, min_width_);
  max_width_ = std::max(curr_max_width, max_width_);

  if (!style_to_use.AutoWrap())
    min_width_ = max_width_;

  if (style_to_use.WhiteSpace() == EWhiteSpace::kPre) {
    if (first_line)
      first_line_min_width_ = max_width_;
    last_line_line_min_width_ = curr_max_width;
  }

  const SimpleFontData* font_data = f.PrimaryFont();
  DCHECK(font_data);

  GlyphOverflow glyph_overflow;
  if (font_data) {
    glyph_overflow.SetFromBounds(
        glyph_bounds, font_data->GetFontMetrics().FloatAscent(),
        font_data->GetFontMetrics().FloatDescent(), max_width_);
  }
  // We shouldn't change our mind once we "know".
  DCHECK(!known_to_have_no_overflow_and_no_fallback_fonts_ ||
         (fallback_fonts.IsEmpty() && glyph_overflow.IsApproximatelyZero()));
  known_to_have_no_overflow_and_no_fallback_fonts_ =
      fallback_fonts.IsEmpty() && glyph_overflow.IsApproximatelyZero();

  ClearPreferredLogicalWidthsDirty();
}

bool LayoutText::IsAllCollapsibleWhitespace() const {
  unsigned length = TextLength();
  if (Is8Bit()) {
    for (unsigned i = 0; i < length; ++i) {
      if (!Style()->IsCollapsibleWhiteSpace(Characters8()[i]))
        return false;
    }
    return true;
  }
  for (unsigned i = 0; i < length; ++i) {
    if (!Style()->IsCollapsibleWhiteSpace(Characters16()[i]))
      return false;
  }
  return true;
}

bool LayoutText::IsRenderedCharacter(int offset_in_node) const {
  for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox()) {
    if (offset_in_node < static_cast<int>(box->Start()) &&
        !ContainsReversedText()) {
      // The offset we're looking for is before this node this means the offset
      // must be in content that is not laid out. Return false.
      return false;
    }
    if (offset_in_node >= static_cast<int>(box->Start()) &&
        offset_in_node < static_cast<int>(box->Start() + box->Len()))
      return true;
  }

  return false;
}

bool LayoutText::ContainsOnlyWhitespace(unsigned from, unsigned len) const {
  DCHECK(text_);
  StringImpl& text = *text_.Impl();
  unsigned curr_pos;
  for (curr_pos = from;
       curr_pos < from + len && (text[curr_pos] == kNewlineCharacter ||
                                 text[curr_pos] == kSpaceCharacter ||
                                 text[curr_pos] == kTabulationCharacter);
       curr_pos++) {
  }
  return curr_pos >= (from + len);
}

FloatPoint LayoutText::FirstRunOrigin() const {
  return IntPoint(FirstRunX(), FirstRunY());
}

float LayoutText::FirstRunX() const {
  return first_text_box_ ? first_text_box_->X().ToFloat() : 0;
}

float LayoutText::FirstRunY() const {
  return first_text_box_ ? first_text_box_->Y().ToFloat() : 0;
}

void LayoutText::SetSelectionState(SelectionState state) {
  LayoutObject::SetSelectionState(state);

  if (CanUpdateSelectionOnRootLineBoxes()) {
    if (state == SelectionStart || state == SelectionEnd ||
        state == SelectionBoth) {
      int start_pos, end_pos;
      std::tie(start_pos, end_pos) = SelectionStartEnd();
      if (GetSelectionState() == SelectionStart) {
        end_pos = TextLength();

        // to handle selection from end of text to end of line
        if (start_pos && start_pos == end_pos)
          start_pos = end_pos - 1;
      } else if (GetSelectionState() == SelectionEnd) {
        start_pos = 0;
      }

      for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox()) {
        if (box->IsSelected(start_pos, end_pos)) {
          box->Root().SetHasSelectedChildren(true);
        }
      }
    } else {
      for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox()) {
        box->Root().SetHasSelectedChildren(state == SelectionInside);
      }
    }
  }

  // The containing block can be null in case of an orphaned tree.
  LayoutBlock* containing_block = this->ContainingBlock();
  if (containing_block && !containing_block->IsLayoutView())
    containing_block->SetSelectionState(state);
}

void LayoutText::SetTextWithOffset(PassRefPtr<StringImpl> text,
                                   unsigned offset,
                                   unsigned len,
                                   bool force) {
  if (!force && Equal(text_.Impl(), text.Get()))
    return;

  unsigned old_len = TextLength();
  unsigned new_len = text->length();
  int delta = new_len - old_len;
  unsigned end = len ? offset + len - 1 : offset;

  RootInlineBox* first_root_box = nullptr;
  RootInlineBox* last_root_box = nullptr;

  bool dirtied_lines = false;

  // Dirty all text boxes that include characters in between offset and
  // offset+len.
  for (InlineTextBox* curr = FirstTextBox(); curr; curr = curr->NextTextBox()) {
    // FIXME: This shouldn't rely on the end of a dirty line box. See
    // https://bugs.webkit.org/show_bug.cgi?id=97264
    // Text run is entirely before the affected range.
    if (curr->end() < offset)
      continue;

    // Text run is entirely after the affected range.
    if (curr->Start() > end) {
      curr->OffsetRun(delta);
      RootInlineBox* root = &curr->Root();
      if (!first_root_box) {
        first_root_box = root;
        // The affected area was in between two runs. Go ahead and mark the root
        // box of the run after the affected area as dirty.
        first_root_box->MarkDirty();
        dirtied_lines = true;
      }
      last_root_box = root;
    } else if (curr->end() >= offset && curr->end() <= end) {
      // Text run overlaps with the left end of the affected range.
      curr->DirtyLineBoxes();
      dirtied_lines = true;
    } else if (curr->Start() <= offset && curr->end() >= end) {
      // Text run subsumes the affected range.
      curr->DirtyLineBoxes();
      dirtied_lines = true;
    } else if (curr->Start() <= end && curr->end() >= end) {
      // Text run overlaps with right end of the affected range.
      curr->DirtyLineBoxes();
      dirtied_lines = true;
    }
  }

  // Now we have to walk all of the clean lines and adjust their cached line
  // break information to reflect our updated offsets.
  if (last_root_box)
    last_root_box = last_root_box->NextRootBox();
  if (first_root_box) {
    RootInlineBox* prev = first_root_box->PrevRootBox();
    if (prev)
      first_root_box = prev;
  } else if (LastTextBox()) {
    DCHECK(!last_root_box);
    first_root_box = &LastTextBox()->Root();
    first_root_box->MarkDirty();
    dirtied_lines = true;
  }
  for (RootInlineBox* curr = first_root_box; curr && curr != last_root_box;
       curr = curr->NextRootBox()) {
    if (curr->LineBreakObj().IsEqual(this) && curr->LineBreakPos() > end)
      curr->SetLineBreakPos(clampTo<int>(curr->LineBreakPos() + delta));
  }

  // If the text node is empty, dirty the line where new text will be inserted.
  if (!FirstTextBox() && Parent()) {
    Parent()->DirtyLinesFromChangedChild(this);
    dirtied_lines = true;
  }

  lines_dirty_ = dirtied_lines;
  SetText(std::move(text), force || dirtied_lines);
}

void LayoutText::TransformText() {
  if (RefPtr<StringImpl> text_to_transform = OriginalText())
    SetText(std::move(text_to_transform), true);
}

static inline bool IsInlineFlowOrEmptyText(const LayoutObject* o) {
  if (o->IsLayoutInline())
    return true;
  if (!o->IsText())
    return false;
  return ToLayoutText(o)->GetText().IsEmpty();
}

UChar LayoutText::PreviousCharacter() const {
  // find previous text layoutObject if one exists
  const LayoutObject* previous_text = PreviousInPreOrder();
  for (; previous_text; previous_text = previous_text->PreviousInPreOrder()) {
    if (!IsInlineFlowOrEmptyText(previous_text))
      break;
  }
  UChar prev = kSpaceCharacter;
  if (previous_text && previous_text->IsText()) {
    if (StringImpl* previous_string =
            ToLayoutText(previous_text)->GetText().Impl())
      prev = (*previous_string)[previous_string->length() - 1];
  }
  return prev;
}

void LayoutText::AddLayerHitTestRects(LayerHitTestRects&,
                                      const PaintLayer* current_layer,
                                      const LayoutPoint& layer_offset,
                                      const LayoutRect& container_rect) const {
  // Text nodes aren't event targets, so don't descend any further.
}

void ApplyTextTransform(const ComputedStyle* style,
                        String& text,
                        UChar previous_character) {
  if (!style)
    return;

  switch (style->TextTransform()) {
    case ETextTransform::kNone:
      break;
    case ETextTransform::kCapitalize:
      MakeCapitalized(&text, previous_character);
      break;
    case ETextTransform::kUppercase:
      text = text.UpperUnicode(style->Locale());
      break;
    case ETextTransform::kLowercase:
      text = text.LowerUnicode(style->Locale());
      break;
  }
}

void LayoutText::SetTextInternal(PassRefPtr<StringImpl> text) {
  DCHECK(text);
  text_ = std::move(text);

  if (Style()) {
    ApplyTextTransform(Style(), text_, PreviousCharacter());

    // We use the same characters here as for list markers.
    // See the listMarkerText function in LayoutListMarker.cpp.
    switch (Style()->TextSecurity()) {
      case ETextSecurity::kNone:
        break;
      case ETextSecurity::kCircle:
        SecureText(kWhiteBulletCharacter);
        break;
      case ETextSecurity::kDisc:
        SecureText(kBulletCharacter);
        break;
      case ETextSecurity::kSquare:
        SecureText(kBlackSquareCharacter);
    }
  }

  DCHECK(text_);
  DCHECK(!IsBR() || (TextLength() == 1 && text_[0] == kNewlineCharacter));
}

void LayoutText::SecureText(UChar mask) {
  if (!text_.length())
    return;

  int last_typed_character_offset_to_reveal = -1;
  UChar revealed_text;
  SecureTextTimer* secure_text_timer =
      g_secure_text_timers ? g_secure_text_timers->at(this) : 0;
  if (secure_text_timer && secure_text_timer->IsActive()) {
    last_typed_character_offset_to_reveal =
        secure_text_timer->LastTypedCharacterOffset();
    if (last_typed_character_offset_to_reveal >= 0)
      revealed_text = text_[last_typed_character_offset_to_reveal];
  }

  text_.Fill(mask);
  if (last_typed_character_offset_to_reveal >= 0) {
    text_.replace(last_typed_character_offset_to_reveal, 1,
                  String(&revealed_text, 1));
    // m_text may be updated later before timer fires. We invalidate the
    // lastTypedCharacterOffset to avoid inconsistency.
    secure_text_timer->Invalidate();
  }
}

void LayoutText::SetText(PassRefPtr<StringImpl> text, bool force) {
  DCHECK(text);

  if (!force && Equal(text_.Impl(), text.Get()))
    return;

  SetTextInternal(std::move(text));
  // If preferredLogicalWidthsDirty() of an orphan child is true,
  // LayoutObjectChildList::insertChildNode() fails to set true to owner.
  // To avoid that, we call setNeedsLayoutAndPrefWidthsRecalc() only if this
  // LayoutText has parent.
  if (Parent())
    SetNeedsLayoutAndPrefWidthsRecalcAndFullPaintInvalidation(
        LayoutInvalidationReason::kTextChanged);
  known_to_have_no_overflow_and_no_fallback_fonts_ = false;

  if (AXObjectCache* cache = GetDocument().ExistingAXObjectCache())
    cache->TextChanged(this);

  TextAutosizer* text_autosizer = GetDocument().GetTextAutosizer();
  if (text_autosizer)
    text_autosizer->Record(this);
}

void LayoutText::DirtyOrDeleteLineBoxesIfNeeded(bool full_layout) {
  if (full_layout)
    DeleteTextBoxes();
  else if (!lines_dirty_)
    DirtyLineBoxes();
  lines_dirty_ = false;
}

void LayoutText::DirtyLineBoxes() {
  for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox())
    box->DirtyLineBoxes();
  lines_dirty_ = false;
}

InlineTextBox* LayoutText::CreateTextBox(int start, unsigned short length) {
  return new InlineTextBox(LineLayoutItem(this), start, length);
}

InlineTextBox* LayoutText::CreateInlineTextBox(int start,
                                               unsigned short length) {
  InlineTextBox* text_box = CreateTextBox(start, length);
  if (!first_text_box_) {
    first_text_box_ = last_text_box_ = text_box;
  } else {
    last_text_box_->SetNextTextBox(text_box);
    text_box->SetPreviousTextBox(last_text_box_);
    last_text_box_ = text_box;
  }
  return text_box;
}

void LayoutText::PositionLineBox(InlineBox* box) {
  InlineTextBox* s = ToInlineTextBox(box);

  // FIXME: should not be needed!!!
  if (!s->Len()) {
    // We want the box to be destroyed.
    s->Remove(kDontMarkLineBoxes);
    if (first_text_box_ == s)
      first_text_box_ = s->NextTextBox();
    else
      s->PrevTextBox()->SetNextTextBox(s->NextTextBox());
    if (last_text_box_ == s)
      last_text_box_ = s->PrevTextBox();
    else
      s->NextTextBox()->SetPreviousTextBox(s->PrevTextBox());
    s->Destroy();
    return;
  }

  contains_reversed_text_ |= !s->IsLeftToRightDirection();
}

float LayoutText::Width(unsigned from,
                        unsigned len,
                        LayoutUnit x_pos,
                        TextDirection text_direction,
                        bool first_line,
                        HashSet<const SimpleFontData*>* fallback_fonts,
                        FloatRect* glyph_bounds) const {
  if (from >= TextLength())
    return 0;

  if (len > TextLength() || from + len > TextLength())
    len = TextLength() - from;

  return Width(from, len, Style(first_line)->GetFont(), x_pos, text_direction,
               fallback_fonts, glyph_bounds);
}

float LayoutText::Width(unsigned from,
                        unsigned len,
                        const Font& f,
                        LayoutUnit x_pos,
                        TextDirection text_direction,
                        HashSet<const SimpleFontData*>* fallback_fonts,
                        FloatRect* glyph_bounds) const {
  DCHECK_LE(from + len, TextLength());
  if (!TextLength())
    return 0;

  const SimpleFontData* font_data = f.PrimaryFont();
  DCHECK(font_data);
  if (!font_data)
    return 0;

  float w;
  if (&f == &Style()->GetFont()) {
    if (!Style()->PreserveNewline() && !from && len == TextLength()) {
      if (fallback_fonts) {
        DCHECK(glyph_bounds);
        if (PreferredLogicalWidthsDirty() ||
            !known_to_have_no_overflow_and_no_fallback_fonts_)
          const_cast<LayoutText*>(this)->ComputePreferredLogicalWidths(
              0, *fallback_fonts, *glyph_bounds);
        else
          *glyph_bounds =
              FloatRect(0, -font_data->GetFontMetrics().FloatAscent(),
                        max_width_, font_data->GetFontMetrics().FloatHeight());
        w = max_width_;
      } else {
        w = MaxLogicalWidth();
      }
    } else {
      w = WidthFromFont(f, from, len, x_pos.ToFloat(), 0, text_direction,
                        fallback_fonts, glyph_bounds);
    }
  } else {
    TextRun run =
        ConstructTextRun(f, this, from, len, StyleRef(), text_direction);
    run.SetCharactersLength(TextLength() - from);
    DCHECK_GE(run.CharactersLength(), run.length());

    run.SetTabSize(!Style()->CollapseWhiteSpace(), Style()->GetTabSize());
    run.SetXPos(x_pos.ToFloat());
    w = f.Width(run, fallback_fonts, glyph_bounds);
  }

  return w;
}

LayoutRect LayoutText::LinesBoundingBox() const {
  LayoutRect result;

  DCHECK_EQ(!FirstTextBox(),
            !LastTextBox());  // Either both are null or both exist.
  if (FirstTextBox() && LastTextBox()) {
    // Return the width of the minimal left side and the maximal right side.
    float logical_left_side = 0;
    float logical_right_side = 0;
    for (InlineTextBox* curr = FirstTextBox(); curr;
         curr = curr->NextTextBox()) {
      if (curr == FirstTextBox() || curr->LogicalLeft() < logical_left_side)
        logical_left_side = curr->LogicalLeft().ToFloat();
      if (curr == FirstTextBox() || curr->LogicalRight() > logical_right_side)
        logical_right_side = curr->LogicalRight().ToFloat();
    }

    bool is_horizontal = Style()->IsHorizontalWritingMode();

    float x = is_horizontal ? logical_left_side : FirstTextBox()->X().ToFloat();
    float y = is_horizontal ? FirstTextBox()->Y().ToFloat() : logical_left_side;
    float width = is_horizontal ? logical_right_side - logical_left_side
                                : LastTextBox()->LogicalBottom() - x;
    float height = is_horizontal ? LastTextBox()->LogicalBottom() - y
                                 : logical_right_side - logical_left_side;
    result = EnclosingLayoutRect(FloatRect(x, y, width, height));
  }

  return result;
}

LayoutRect LayoutText::VisualOverflowRect() const {
  if (!FirstTextBox())
    return LayoutRect();

  // Return the width of the minimal left side and the maximal right side.
  LayoutUnit logical_left_side = LayoutUnit::Max();
  LayoutUnit logical_right_side = LayoutUnit::Min();
  for (InlineTextBox* curr = FirstTextBox(); curr; curr = curr->NextTextBox()) {
    LayoutRect logical_visual_overflow = curr->LogicalOverflowRect();
    logical_left_side =
        std::min(logical_left_side, logical_visual_overflow.X());
    logical_right_side =
        std::max(logical_right_side, logical_visual_overflow.MaxX());
  }

  LayoutUnit logical_top = FirstTextBox()->LogicalTopVisualOverflow();
  LayoutUnit logical_width = logical_right_side - logical_left_side;
  LayoutUnit logical_height =
      LastTextBox()->LogicalBottomVisualOverflow() - logical_top;

  // Inflate visual overflow if we have adjusted ascent/descent causing the
  // painted glyphs to overflow the layout geometries based on the adjusted
  // ascent/descent.
  unsigned inflation_for_ascent = 0;
  unsigned inflation_for_descent = 0;
  const auto* font_data =
      StyleRef(FirstTextBox()->IsFirstLineStyle()).GetFont().PrimaryFont();
  if (font_data)
    inflation_for_ascent = font_data->VisualOverflowInflationForAscent();
  if (LastTextBox()->IsFirstLineStyle() != FirstTextBox()->IsFirstLineStyle()) {
    font_data =
        StyleRef(LastTextBox()->IsFirstLineStyle()).GetFont().PrimaryFont();
  }
  if (font_data)
    inflation_for_descent = font_data->VisualOverflowInflationForDescent();
  logical_top -= LayoutUnit(inflation_for_ascent);
  logical_height += LayoutUnit(inflation_for_ascent + inflation_for_descent);

  LayoutRect rect(logical_left_side, logical_top, logical_width,
                  logical_height);
  if (!Style()->IsHorizontalWritingMode())
    rect = rect.TransposedRect();
  return rect;
}

LayoutRect LayoutText::LocalVisualRect() const {
  if (Style()->Visibility() != EVisibility::kVisible)
    return LayoutRect();

  return UnionRect(VisualOverflowRect(), LocalSelectionRect());
}

LayoutRect LayoutText::LocalSelectionRect() const {
  DCHECK(!NeedsLayout());

  if (GetSelectionState() == SelectionNone)
    return LayoutRect();
  LayoutBlock* cb = ContainingBlock();
  if (!cb)
    return LayoutRect();

  // Now calculate startPos and endPos for painting selection.
  // We include a selection while endPos > 0
  int start_pos, end_pos;
  if (GetSelectionState() == SelectionInside) {
    // We are fully selected.
    start_pos = 0;
    end_pos = TextLength();
  } else {
    std::tie(start_pos, end_pos) = SelectionStartEnd();
    if (GetSelectionState() == SelectionStart)
      end_pos = TextLength();
    else if (GetSelectionState() == SelectionEnd)
      start_pos = 0;
  }

  LayoutRect rect;

  if (start_pos == end_pos)
    return rect;

  for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox()) {
    rect.Unite(box->LocalSelectionRect(start_pos, end_pos));
    rect.Unite(LayoutRect(EllipsisRectForBox(box, start_pos, end_pos)));
  }

  return rect;
}

int LayoutText::CaretMinOffset() const {
  InlineTextBox* box = FirstTextBox();
  if (!box)
    return 0;
  int min_offset = box->Start();
  for (box = box->NextTextBox(); box; box = box->NextTextBox())
    min_offset = std::min<int>(min_offset, box->Start());
  return min_offset;
}

int LayoutText::CaretMaxOffset() const {
  InlineTextBox* box = LastTextBox();
  if (!LastTextBox())
    return TextLength();

  int max_offset = box->Start() + box->Len();
  for (box = box->PrevTextBox(); box; box = box->PrevTextBox())
    max_offset = std::max<int>(max_offset, box->Start() + box->Len());
  return max_offset;
}

unsigned LayoutText::ResolvedTextLength() const {
  int len = 0;
  for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox())
    len += box->Len();
  return len;
}

void LayoutText::MomentarilyRevealLastTypedCharacter(
    unsigned last_typed_character_offset) {
  if (!g_secure_text_timers)
    g_secure_text_timers = new SecureTextTimerMap;

  SecureTextTimer* secure_text_timer = g_secure_text_timers->at(this);
  if (!secure_text_timer) {
    secure_text_timer = new SecureTextTimer(this);
    g_secure_text_timers->insert(this, secure_text_timer);
  }
  secure_text_timer->RestartWithNewText(last_typed_character_offset);
}

PassRefPtr<AbstractInlineTextBox> LayoutText::FirstAbstractInlineTextBox() {
  return AbstractInlineTextBox::GetOrCreate(LineLayoutText(this),
                                            first_text_box_);
}

void LayoutText::InvalidateDisplayItemClients(
    PaintInvalidationReason invalidation_reason) const {
  ObjectPaintInvalidator paint_invalidator(*this);
  paint_invalidator.InvalidateDisplayItemClient(*this, invalidation_reason);

  for (InlineTextBox* box = FirstTextBox(); box; box = box->NextTextBox()) {
    paint_invalidator.InvalidateDisplayItemClient(*box, invalidation_reason);
    if (box->Truncation() != kCNoTruncation) {
      if (EllipsisBox* ellipsis_box = box->Root().GetEllipsisBox())
        paint_invalidator.InvalidateDisplayItemClient(*ellipsis_box,
                                                      invalidation_reason);
    }
  }
}

// TODO(lunalu): Would be better to dump the bounding box x and y rather than
// the first run's x and y, but that would involve updating many test results.
LayoutRect LayoutText::DebugRect() const {
  IntRect lines_box = EnclosingIntRect(LinesBoundingBox());
  LayoutRect rect = LayoutRect(
      IntRect(FirstRunX(), FirstRunY(), lines_box.Width(), lines_box.Height()));
  LayoutBlock* block = ContainingBlock();
  if (block && HasTextBoxes())
    block->AdjustChildDebugRect(rect);

  return rect;
}

}  // namespace blink
