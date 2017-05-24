// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/inline/ng_line_breaker.h"

#include "core/layout/ng/inline/ng_inline_break_token.h"
#include "core/layout/ng/inline/ng_inline_layout_algorithm.h"
#include "core/layout/ng/inline/ng_inline_node.h"
#include "core/layout/ng/inline/ng_text_fragment.h"
#include "core/layout/ng/ng_box_fragment.h"
#include "core/layout/ng/ng_break_token.h"
#include "core/layout/ng/ng_constraint_space.h"
#include "core/layout/ng/ng_fragment_builder.h"
#include "core/layout/ng/ng_layout_opportunity_iterator.h"
#include "core/layout/ng/ng_length_utils.h"
#include "core/style/ComputedStyle.h"
#include "platform/fonts/shaping/HarfBuzzShaper.h"
#include "platform/fonts/shaping/ShapingLineBreaker.h"
#include "platform/text/TextBreakIterator.h"

namespace blink {

namespace {

// Use a mock of ShapingLineBreaker for test/debug purposes.
#define MOCK_SHAPE_LINE

#if defined(MOCK_SHAPE_LINE)
// The mock for ShapingLineBreaker::ShapeLine().
// Given the design of ShapingLineBreaker, expected semantics are:
// - The returned offset is always > item.StartOffset().
// - offset < item.EndOffset():
//   - width <= available_width: the break opportunity to fit is found.
//   - width > available_width: the first break opportunity did not fit.
// - offset == item.EndOffset():
//   - width <= available_width: the break opportunity at the end of the item
//     fits.
//   - width > available_width: the first break opportunity is at the end of
//     the item and it does not fit.
// - offset > item.EndOffset():, the first break opportunity is beyond the
//   end of item and thus cannot measure. In this case, inline_size shows the
//   width until the end of the item. It may fit or may not.
std::pair<unsigned, LayoutUnit> ShapeLineMock(
    const NGInlineItem& item,
    unsigned offset,
    LayoutUnit available_width,
    const LazyLineBreakIterator& break_iterator) {
  bool has_break_opportunities = false;
  LayoutUnit inline_size;
  while (true) {
    unsigned next_break = break_iterator.NextBreakOpportunity(offset + 1);
    LayoutUnit next_inline_size =
        inline_size +
        item.InlineSize(offset, std::min(next_break, item.EndOffset()));
    if (next_inline_size > available_width) {
      if (!has_break_opportunities)
        return std::make_pair(next_break, next_inline_size);
      return std::make_pair(offset, inline_size);
    }
    if (next_break >= item.EndOffset())
      return std::make_pair(next_break, next_inline_size);
    offset = next_break;
    inline_size = next_inline_size;
    has_break_opportunities = true;
  }
}
#endif

LineBreakType GetLineBreakType(const ComputedStyle& style) {
  if (style.AutoWrap()) {
    if (style.WordBreak() == EWordBreak::kBreakAll ||
        style.WordBreak() == EWordBreak::kBreakWord)
      return LineBreakType::kBreakAll;
    if (style.WordBreak() == EWordBreak::kKeepAll)
      return LineBreakType::kKeepAll;
  }
  return LineBreakType::kNormal;
}

}  // namespace

NGLineBreaker::NGLineBreaker(NGInlineNode* node,
                             const NGConstraintSpace* space,
                             NGInlineBreakToken* break_token)
    : node_(node), constraint_space_(space), item_index_(0), offset_(0) {
  if (break_token) {
    item_index_ = break_token->ItemIndex();
    offset_ = break_token->TextOffset();
    node->AssertOffset(item_index_, offset_);
  }
}

void NGLineBreaker::NextLine(NGInlineItemResults* item_results,
                             NGInlineLayoutAlgorithm* algorithm) {
  BreakLine(item_results, algorithm);

  // TODO(kojii): When editing, or caret is enabled, trailing spaces at wrap
  // point should not be removed. For other cases, we can a) remove, b) leave
  // characters without glyphs, or c) leave both characters and glyphs without
  // measuring. Need to decide which one works the best.
  SkipCollapsibleWhitespaces();
}

void NGLineBreaker::BreakLine(NGInlineItemResults* item_results,
                              NGInlineLayoutAlgorithm* algorithm) {
  DCHECK(item_results->IsEmpty());
  const Vector<NGInlineItem>& items = node_->Items();
  const String& text = node_->Text();
  const ComputedStyle& style = node_->Style();
  LazyLineBreakIterator break_iterator(text, style.LocaleForLineBreakIterator(),
                                       GetLineBreakType(style));
#if !defined(MOCK_SHAPE_LINE)
  HarfBuzzShaper shaper(text.Characters16(), text.length());
#endif
  LayoutUnit available_width = algorithm->AvailableWidth();
  LayoutUnit position;
  while (item_index_ < items.size()) {
    const NGInlineItem& item = items[item_index_];
    item_results->push_back(
        NGInlineItemResult(item_index_, offset_, item.EndOffset()));
    NGInlineItemResult* item_result = &item_results->back();

    // If the start offset is at the item boundary, try to add the entire item.
    if (offset_ == item.StartOffset()) {
      if (item.Type() == NGInlineItem::kText) {
        item_result->inline_size = item.InlineSize();
      } else if (item.Type() == NGInlineItem::kAtomicInline) {
        LayoutAtomicInline(item, item_result);
      } else if (item.Type() == NGInlineItem::kFloating) {
        algorithm->LayoutAndPositionFloat(position, item.GetLayoutObject());
        // Floats may change the available width if they fit.
        available_width = algorithm->AvailableWidth();
        // Floats are already positioned in the container_builder.
        item_results->pop_back();
        offset_ = item.EndOffset();
        item_index_++;
        continue;
      } else {
        offset_ = item.EndOffset();
        item_index_++;
        continue;
      }
      LayoutUnit next_position = position + item_result->inline_size;
      if (next_position <= available_width) {
        offset_ = item.EndOffset();
        item_index_++;
        position = next_position;
        continue;
      }

      // The entire item does not fit. Handle non-text items as overflow,
      // since only text item is breakable.
      if (item.Type() != NGInlineItem::kText) {
        offset_ = item.EndOffset();
        item_index_++;
        return HandleOverflow(item_results, break_iterator);
      }
    }

    // Either the start or the break is in the mid of a text item.
    DCHECK_EQ(item.Type(), NGInlineItem::kText);
    DCHECK_LT(offset_, item.EndOffset());
    break_iterator.SetLocale(item.Style()->LocaleForLineBreakIterator());
    break_iterator.SetBreakType(GetLineBreakType(*item.Style()));
#if defined(MOCK_SHAPE_LINE)
    unsigned break_offset;
    std::tie(break_offset, item_result->inline_size) = ShapeLineMock(
        item, offset_, available_width - position, break_iterator);
#else
    // TODO(kojii): We need to instantiate ShapingLineBreaker here because it
    // has item-specific info as context. Should they be part of ShapeLine() to
    // instantiate once, or is this just fine since instatiation is not
    // expensive?
    DCHECK_EQ(item.TextShapeResult()->StartIndexForResult(),
              item.StartOffset());
    DCHECK_EQ(item.TextShapeResult()->EndIndexForResult(), item.EndOffset());
    ShapingLineBreaker breaker(&shaper, &item.Style()->GetFont(),
                               item.TextShapeResult(), &break_iterator);
    unsigned break_offset;
    item_result->shape_result =
        breaker.ShapeLine(offset_, available_width - position, &break_offset);
    item_result->inline_size = item_result->shape_result->SnappedWidth();
#endif
    DCHECK_GT(break_offset, offset_);
    position += item_result->inline_size;

    // If the break found within the item, break here.
    if (break_offset < item.EndOffset()) {
      offset_ = item_result->end_offset = break_offset;
      if (position <= available_width)
        break;
      // The first break opportunity of the item does not fit.
    } else {
      // No break opporunity in the item, or the first break opportunity is at
      // the end of the item. If it fits, continue to the next item.
      offset_ = item_result->end_offset = item.EndOffset();
      item_index_++;
      if (position <= available_width)
        continue;
    }

    // We need to look at next item if we're overflowing, and the break
    // opportunity is beyond this item.
    if (break_offset > item.EndOffset())
      continue;
    return HandleOverflow(item_results, break_iterator);
  }
}

void NGLineBreaker::LayoutAtomicInline(const NGInlineItem& item,
                                       NGInlineItemResult* item_result) {
  DCHECK_EQ(item.Type(), NGInlineItem::kAtomicInline);
  NGBlockNode* node = new NGBlockNode(item.GetLayoutObject());
  const ComputedStyle& style = node->Style();
  NGConstraintSpaceBuilder constraint_space_builder(constraint_space_);
  RefPtr<NGConstraintSpace> constraint_space =
      constraint_space_builder.SetIsNewFormattingContext(true)
          .SetIsShrinkToFit(true)
          .SetTextDirection(style.Direction())
          .ToConstraintSpace(FromPlatformWritingMode(style.GetWritingMode()));
  item_result->layout_result = node->Layout(constraint_space.Get());

  item_result->inline_size =
      NGBoxFragment(constraint_space_->WritingMode(),
                    ToNGPhysicalBoxFragment(
                        item_result->layout_result->PhysicalFragment().Get()))
          .InlineSize();

  item_result->margins =
      ComputeMargins(*constraint_space_, style,
                     constraint_space_->WritingMode(), style.Direction());
  item_result->inline_size += item_result->margins.InlineSum();
}

// Handles when the last item overflows.
// At this point, item_results does not fit into the current line, and there
// are no break opportunities in item_results.back().
void NGLineBreaker::HandleOverflow(
    NGInlineItemResults* item_results,
    const LazyLineBreakIterator& break_iterator) {
  DCHECK_GT(offset_, 0u);

  // Find the last break opportunity. If none, let this line overflow.
  unsigned line_start_offset = item_results->front().start_offset;
  unsigned break_offset =
      break_iterator.PreviousBreakOpportunity(offset_ - 1, line_start_offset);
  if (!break_offset || break_offset <= line_start_offset) {
    AppendCloseTags(item_results);
    return;
  }

  // Truncate the end of the line to the break opportunity.
  const Vector<NGInlineItem>& items = node_->Items();
  unsigned new_end = item_results->size();
  while (true) {
    NGInlineItemResult* item_result = &(*item_results)[--new_end];
    if (item_result->start_offset < break_offset) {
      // The break is at the mid of the item. Adjust the end_offset to the new
      // break offset.
      const NGInlineItem& item = items[item_result->item_index];
      item.AssertEndOffset(break_offset);
      DCHECK_EQ(item.Type(), NGInlineItem::kText);
      DCHECK_NE(item_result->end_offset, break_offset);
      item_result->end_offset = break_offset;
      item_result->inline_size =
          item.InlineSize(item_result->start_offset, item_result->end_offset);
      // TODO(kojii): May need to reshape. Add to ShapingLineBreaker?
      new_end++;
      break;
    }
    if (item_result->start_offset == break_offset) {
      // The new break offset is at the item boundary. Remove items up to the
      // new break offset.
      // TODO(kojii): Remove open tags as well.
      break;
    }
  }
  DCHECK_GT(new_end, 0u);

  // TODO(kojii): Should we keep results for the next line? We don't need to
  // re-layout atomic inlines.
  // TODO(kojii): Removing processed floats is likely a problematic. Keep
  // floats in this line, or keep it for the next line.
  item_results->Shrink(new_end);

  // Update the current item index and offset to the new break point.
  const NGInlineItemResult& last_item_result = item_results->back();
  offset_ = last_item_result.end_offset;
  item_index_ = last_item_result.item_index;
  if (items[item_index_].EndOffset() == offset_)
    item_index_++;
}

void NGLineBreaker::SkipCollapsibleWhitespaces() {
  const Vector<NGInlineItem>& items = node_->Items();
  if (item_index_ >= items.size())
    return;
  const NGInlineItem& item = items[item_index_];
  if (item.Type() != NGInlineItem::kText || !item.Style()->CollapseWhiteSpace())
    return;

  DCHECK_LT(offset_, item.EndOffset());
  if (node_->Text()[offset_] == kSpaceCharacter) {
    // Skip one whitespace. Collapsible spaces are collapsed to single space in
    // NGInlineItemBuilder, so this removes all collapsible spaces.
    offset_++;
    if (offset_ == item.EndOffset())
      item_index_++;
  }
}

void NGLineBreaker::AppendCloseTags(NGInlineItemResults* item_results) {
  const Vector<NGInlineItem>& items = node_->Items();
  for (; item_index_ < items.size(); item_index_++) {
    const NGInlineItem& item = items[item_index_];
    if (item.Type() != NGInlineItem::kCloseTag)
      break;
    DCHECK_EQ(offset_, item.EndOffset());
    item_results->push_back(NGInlineItemResult(item_index_, offset_, offset_));
  }
}

RefPtr<NGInlineBreakToken> NGLineBreaker::CreateBreakToken() const {
  const Vector<NGInlineItem>& items = node_->Items();
  if (item_index_ >= items.size())
    return nullptr;
  return NGInlineBreakToken::Create(node_, item_index_, offset_);
}

}  // namespace blink
