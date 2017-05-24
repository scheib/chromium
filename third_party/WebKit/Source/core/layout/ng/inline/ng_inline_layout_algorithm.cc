// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/inline/ng_inline_layout_algorithm.h"

#include "core/layout/ng/inline/ng_bidi_paragraph.h"
#include "core/layout/ng/inline/ng_inline_break_token.h"
#include "core/layout/ng/inline/ng_inline_node.h"
#include "core/layout/ng/inline/ng_line_box_fragment.h"
#include "core/layout/ng/inline/ng_line_box_fragment_builder.h"
#include "core/layout/ng/inline/ng_line_breaker.h"
#include "core/layout/ng/inline/ng_text_fragment.h"
#include "core/layout/ng/inline/ng_text_fragment_builder.h"
#include "core/layout/ng/layout_ng_block_flow.h"
#include "core/layout/ng/ng_block_layout_algorithm.h"
#include "core/layout/ng/ng_box_fragment.h"
#include "core/layout/ng/ng_constraint_space.h"
#include "core/layout/ng/ng_constraint_space_builder.h"
#include "core/layout/ng/ng_floats_utils.h"
#include "core/layout/ng/ng_fragment_builder.h"
#include "core/layout/ng/ng_layout_result.h"
#include "core/layout/ng/ng_length_utils.h"
#include "core/layout/ng/ng_space_utils.h"
#include "core/layout/ng/ng_unpositioned_float.h"
#include "core/style/ComputedStyle.h"
#include "platform/text/BidiRunList.h"

namespace blink {
namespace {

NGLogicalOffset GetOriginPointForFloats(
    const NGLogicalOffset& container_bfc_offset,
    LayoutUnit content_size) {
  NGLogicalOffset origin_point = container_bfc_offset;
  origin_point.block_offset += content_size;
  return origin_point;
}

}  // namespace

NGInlineLayoutAlgorithm::NGInlineLayoutAlgorithm(
    NGInlineNode* inline_node,
    NGConstraintSpace* space,
    NGInlineBreakToken* break_token)
    : NGLayoutAlgorithm(inline_node, space, break_token),
      is_horizontal_writing_mode_(
          blink::IsHorizontalWritingMode(space->WritingMode())),
      disallow_first_line_rules_(false),
      space_builder_(space)
{
  container_builder_.MutableUnpositionedFloats() = space->UnpositionedFloats();

  // TODO(crbug.com/716930): We may be an empty LayoutInline due to splitting.
  // Only resolve our BFC offset if we know that we are non-empty as we may
  // need to pass through our margin strut.
  if (!inline_node->Items().IsEmpty()) {
    NGLogicalOffset bfc_offset = ConstraintSpace().BfcOffset();
    bfc_offset.block_offset += ConstraintSpace().MarginStrut().Sum();
    MaybeUpdateFragmentBfcOffset(ConstraintSpace(), bfc_offset,
                                 &container_builder_);
    PositionPendingFloats(bfc_offset.block_offset, &container_builder_,
                          MutableConstraintSpace());
  }

  if (!is_horizontal_writing_mode_)
    baseline_type_ = FontBaseline::kIdeographicBaseline;

  border_and_padding_ = ComputeBorders(ConstraintSpace(), Style()) +
                        ComputePadding(ConstraintSpace(), Style());

  if (break_token) {
    // If a break_token is given, we're re-starting layout for 2nd or later
    // lines, and that the first line we create should not use the first line
    // rules.
    DCHECK(!break_token->IsFinished());
    DCHECK(break_token->TextOffset() || break_token->ItemIndex());
    disallow_first_line_rules_ = true;
  } else {
    auto& engine = Node()->GetLayoutObject()->GetDocument().GetStyleEngine();
    disallow_first_line_rules_ = !engine.UsesFirstLineRules();
  }

  FindNextLayoutOpportunity();
}

bool NGInlineLayoutAlgorithm::IsFirstLine() const {
  return !disallow_first_line_rules_ && container_builder_.Children().IsEmpty();
}

const ComputedStyle& NGInlineLayoutAlgorithm::FirstLineStyle() const {
  return Node()->GetLayoutObject()->FirstLineStyleRef();
}

const ComputedStyle& NGInlineLayoutAlgorithm::LineStyle() const {
  return IsFirstLine() ? FirstLineStyle() : Style();
}

LayoutUnit NGInlineLayoutAlgorithm::AvailableWidth() const {
  return current_opportunity_.InlineSize();
}

// The offset of 'line-left' side.
// https://drafts.csswg.org/css-writing-modes/#line-left
LayoutUnit NGInlineLayoutAlgorithm::LogicalLeftOffset() const {
  // TODO(kojii): We need to convert 'line start' to 'line left'. They're
  // different in RTL. Maybe there are more where start and left are misused.
  return current_opportunity_.InlineStartOffset() -
         ConstraintSpace().BfcOffset().inline_offset;
}

bool NGInlineLayoutAlgorithm::CreateLine(
    NGInlineItemResults* item_results,
    RefPtr<NGInlineBreakToken> break_token) {
  if (Node()->IsBidiEnabled())
    BidiReorder(item_results);

  if (!PlaceItems(item_results, break_token))
    return false;

  // Prepare for the next line.
  NGLogicalOffset origin_point =
      GetOriginPointForFloats(ContainerBfcOffset(), content_size_);
  PositionPendingFloats(origin_point.block_offset, &container_builder_,
                        MutableConstraintSpace());
  FindNextLayoutOpportunity();
  return true;
}

void NGInlineLayoutAlgorithm::BidiReorder(NGInlineItemResults* line_items) {
  // TODO(kojii): UAX#9 L1 is not supported yet. Supporting L1 may change
  // embedding levels of parts of runs, which requires to split items.
  // http://unicode.org/reports/tr9/#L1
  // BidiResolver does not support L1 crbug.com/316409.

  // Create a list of chunk indices in the visual order.
  // ICU |ubidi_getVisualMap()| works for a run of characters. Since we can
  // handle the direction of each run, we use |ubidi_reorderVisual()| to reorder
  // runs instead of characters.
  Vector<UBiDiLevel, 32> levels;
  levels.ReserveInitialCapacity(line_items->size());
  const Vector<NGInlineItem>& items = Node()->Items();
  for (const auto& item_result : *line_items)
    levels.push_back(items[item_result.item_index].BidiLevel());
  Vector<int32_t, 32> indices_in_visual_order(line_items->size());
  NGBidiParagraph::IndicesInVisualOrder(levels, &indices_in_visual_order);

  // Reorder to the visual order.
  NGInlineItemResults line_items_in_visual_order(line_items->size());
  for (unsigned visual_index = 0; visual_index < indices_in_visual_order.size();
       visual_index++) {
    unsigned logical_index = indices_in_visual_order[visual_index];
    line_items_in_visual_order[visual_index] =
        std::move((*line_items)[logical_index]);
  }

  // Keep Open before Close in the visual order.
  HashMap<LayoutObject*, unsigned> first_index;
  for (unsigned i = 0; i < line_items_in_visual_order.size(); i++) {
    NGInlineItemResult& item_result = line_items_in_visual_order[i];
    const NGInlineItem& item = items[item_result.item_index];
    if (item.Type() != NGInlineItem::kOpenTag &&
        item.Type() != NGInlineItem::kCloseTag) {
      continue;
    }
    auto result = first_index.insert(item.GetLayoutObject(), i);
    if (!result.is_new_entry && item.Type() == NGInlineItem::kOpenTag) {
      std::swap(line_items_in_visual_order[i],
                line_items_in_visual_order[result.stored_value->value]);
    }
  }

  line_items->swap(line_items_in_visual_order);
}

// TODO(glebl): Add the support of clearance for inline floats.
void NGInlineLayoutAlgorithm::LayoutAndPositionFloat(
    LayoutUnit end_position,
    LayoutObject* layout_object) {
  NGBlockNode* node = new NGBlockNode(layout_object);

  NGLogicalOffset origin_offset =
      GetOriginPointForFloats(ContainerBfcOffset(), content_size_);
  const ComputedStyle& float_style = node->Style();
  NGBoxStrut margins = ComputeMargins(ConstraintSpace(), float_style,
                                      ConstraintSpace().WritingMode(),
                                      ConstraintSpace().Direction());

  // TODO(ikilpatrick): Add support for float break tokens inside an inline
  // layout context.
  RefPtr<NGUnpositionedFloat> unpositioned_float = NGUnpositionedFloat::Create(
      current_opportunity_.size, ConstraintSpace().PercentageResolutionSize(),
      origin_offset, ContainerBfcOffset(), margins, node,
      /* break_token */ nullptr);
  unpositioned_float->parent_bfc_block_offset =
      ContainerBfcOffset().block_offset;

  LayoutUnit inline_size = ComputeInlineSizeForUnpositionedFloat(
      MutableConstraintSpace(), unpositioned_float.Get());

  bool float_does_not_fit = end_position + inline_size + margins.InlineSum() >
                            current_opportunity_.InlineSize();
  // Check if we already have a pending float. That's because a float cannot be
  // higher than any block or floated box generated before.
  if (!container_builder_.UnpositionedFloats().IsEmpty() ||
      float_does_not_fit) {
    container_builder_.AddUnpositionedFloat(unpositioned_float);
  } else {
    container_builder_.AddPositionedFloat(
        PositionFloat(unpositioned_float.Get(), MutableConstraintSpace()));
    FindNextLayoutOpportunity();
  }
}

bool NGInlineLayoutAlgorithm::PlaceItems(
    NGInlineItemResults* line_items,
    RefPtr<NGInlineBreakToken> break_token) {
  const Vector<NGInlineItem>& items = Node()->Items();

  const ComputedStyle& line_style = LineStyle();
  NGLineHeightMetrics line_metrics(line_style, baseline_type_);
  NGLineHeightMetrics line_metrics_with_leading = line_metrics;
  line_metrics_with_leading.AddLeading(line_style.ComputedLineHeightAsFixed());
  NGLineBoxFragmentBuilder line_box(Node());

  // Compute heights of all inline items by placing the dominant baseline at 0.
  // The baseline is adjusted after the height of the line box is computed.
  NGTextFragmentBuilder text_builder(Node());
  NGInlineBoxState* box =
      box_states_.OnBeginPlaceItems(&LineStyle(), baseline_type_);

  // Place items from line-left to line-right along with the baseline.
  // Items are already bidi-reordered to the visual order.
  LayoutUnit line_left_position = LogicalLeftOffset();
  LayoutUnit position = line_left_position;

  for (auto& item_result : *line_items) {
    const NGInlineItem& item = items[item_result.item_index];
    if (item.Type() == NGInlineItem::kText) {
      DCHECK(item.GetLayoutObject()->IsText());
      DCHECK(!box->text_metrics.IsEmpty());
      text_builder.SetSize(
          {item_result.inline_size, box->text_metrics.LineHeight()});
      // Take all used fonts into account if 'line-height: normal'.
      if (box->include_used_fonts) {
        box->AccumulateUsedFonts(item, item_result.start_offset,
                                 item_result.end_offset, baseline_type_);
      }
      RefPtr<NGPhysicalTextFragment> text_fragment =
          text_builder.ToTextFragment(item_result.item_index,
                                      item_result.start_offset,
                                      item_result.end_offset);
      line_box.AddChild(std::move(text_fragment), {position, box->text_top});
    } else if (item.Type() == NGInlineItem::kOpenTag) {
      box = box_states_.OnOpenTag(item, &line_box, &text_builder);
      // Compute text metrics for all inline boxes since even empty inlines
      // influence the line height.
      // https://drafts.csswg.org/css2/visudet.html#line-height
      box->ComputeTextMetrics(*item.Style(), baseline_type_);
    } else if (item.Type() == NGInlineItem::kCloseTag) {
      box = box_states_.OnCloseTag(item, &line_box, box, baseline_type_);
    } else if (item.Type() == NGInlineItem::kAtomicInline) {
      box = PlaceAtomicInline(item, &item_result, position, &line_box,
                              &text_builder);
    } else if (item.Type() == NGInlineItem::kOutOfFlowPositioned) {
      // TODO(layout-dev): Report the correct static position for the out of
      // flow descendant. We can't do this here yet as it doesn't know the
      // size of the line box.
      container_builder_.AddOutOfFlowDescendant(
          // Absolute positioning blockifies the box's display type.
          // https://drafts.csswg.org/css-display/#transformations
          new NGBlockNode(item.GetLayoutObject()),
          NGStaticPosition::Create(ConstraintSpace().WritingMode(),
                                   ConstraintSpace().Direction(),
                                   NGPhysicalOffset()));
      continue;
    } else {
      continue;
    }

    position += item_result.inline_size;
  }

  if (line_box.Children().IsEmpty()) {
    return true;  // The line was empty.
  }

  box_states_.OnEndPlaceItems(&line_box, baseline_type_);

  // The baselines are always placed at pixel boundaries. Not doing so results
  // in incorrect layout of text decorations, most notably underlines.
  LayoutUnit baseline = content_size_ + line_box.Metrics().ascent +
                        border_and_padding_.block_start;
  baseline = LayoutUnit(baseline.Round());

  // Check if the line fits into the constraint space in block direction.
  LayoutUnit line_bottom = baseline + line_box.Metrics().descent;

  if (!container_builder_.Children().IsEmpty() &&
      ConstraintSpace().AvailableSize().block_size != NGSizeIndefinite &&
      line_bottom > ConstraintSpace().AvailableSize().block_size) {
    return false;
  }

  line_box.SetBreakToken(std::move(break_token));

  // TODO(kojii): Implement flipped line (vertical-lr). In this case, line_top
  // and block_start do not match.

  // Up until this point, children are placed so that the dominant baseline is
  // at 0. Move them to the final baseline position, and set the logical top of
  // the line box to the line top.
  line_box.MoveChildrenInBlockDirection(baseline);

  DCHECK_EQ(line_left_position, LogicalLeftOffset());
  LayoutUnit inline_size = position - line_left_position;
  line_box.SetInlineSize(inline_size);

  // Account for text align property.
  if (Node()->Style().GetTextAlign() == ETextAlign::kRight) {
    line_box.MoveChildrenInInlineDirection(
        current_opportunity_.size.inline_size - inline_size);
  }

  container_builder_.AddChild(
      line_box.ToLineBoxFragment(),
      {LayoutUnit(), baseline - box_states_.LineBoxState().metrics.ascent});

  max_inline_size_ = std::max(max_inline_size_, inline_size);
  content_size_ = line_bottom;
  return true;
}

// TODO(kojii): Currently, this function does not change item_result, but
// when NG paint is enabled, this will std::move() the LayoutResult.
NGInlineBoxState* NGInlineLayoutAlgorithm::PlaceAtomicInline(
    const NGInlineItem& item,
    NGInlineItemResult* item_result,
    LayoutUnit position,
    NGLineBoxFragmentBuilder* line_box,
    NGTextFragmentBuilder* text_builder) {
  DCHECK(item_result->layout_result);

  NGInlineBoxState* box = box_states_.OnOpenTag(item, line_box, text_builder);

  // For replaced elements, inline-block elements, and inline-table elements,
  // the height is the height of their margin box.
  // https://drafts.csswg.org/css2/visudet.html#line-height
  NGBoxFragment fragment(
      ConstraintSpace().WritingMode(),
      ToNGPhysicalBoxFragment(
          item_result->layout_result->PhysicalFragment().Get()));
  LayoutUnit block_size =
      fragment.BlockSize() + item_result->margins.BlockSum();

  // TODO(kojii): Add baseline position to NGPhysicalFragment.
  LayoutBox* layout_box = ToLayoutBox(item.GetLayoutObject());
  LineDirectionMode line_direction_mode =
      IsHorizontalWritingMode() ? LineDirectionMode::kHorizontalLine
                                : LineDirectionMode::kVerticalLine;
  LayoutUnit baseline_offset(layout_box->BaselinePosition(
      baseline_type_, IsFirstLine(), line_direction_mode));

  NGLineHeightMetrics metrics(baseline_offset, block_size - baseline_offset);
  box->metrics.Unite(metrics);

  // TODO(kojii): Figure out what to do with OOF in NGLayoutResult.
  // Floats are ok because atomic inlines are BFC?

  // TODO(kojii): Try to eliminate the wrapping text fragment and use the
  // |fragment| directly. Currently |CopyFragmentDataToLayoutBlockFlow|
  // requires a text fragment.
  text_builder->SetSize({fragment.InlineSize(), block_size});
  LayoutUnit line_top = item_result->margins.block_start - metrics.ascent;
  RefPtr<NGPhysicalTextFragment> text_fragment = text_builder->ToTextFragment(
      item_result->item_index, item_result->start_offset,
      item_result->end_offset);
  line_box->AddChild(std::move(text_fragment), {position, line_top});

  return box_states_.OnCloseTag(item, line_box, box, baseline_type_);
}

void NGInlineLayoutAlgorithm::FindNextLayoutOpportunity() {
  NGLogicalOffset iter_offset = ConstraintSpace().BfcOffset();
  if (container_builder_.BfcOffset()) {
    iter_offset = ContainerBfcOffset();
    iter_offset +=
        {border_and_padding_.inline_start, border_and_padding_.block_start};
  }
  iter_offset.block_offset += content_size_;
  auto* iter = MutableConstraintSpace()->LayoutOpportunityIterator(iter_offset);
  NGLayoutOpportunity opportunity = iter->Next();
  if (!opportunity.IsEmpty())
    current_opportunity_ = opportunity;
}

RefPtr<NGLayoutResult> NGInlineLayoutAlgorithm::Layout() {
  NGLineBreaker line_breaker(Node(), constraint_space_, BreakToken());
  NGInlineItemResults item_results;
  while (true) {
    line_breaker.NextLine(&item_results, this);
    if (item_results.IsEmpty())
      break;
    CreateLine(&item_results, line_breaker.CreateBreakToken());
    item_results.clear();
  }

  // TODO(crbug.com/716930): Avoid calculating border/padding twice.
  if (!Node()->Items().IsEmpty())
    content_size_ -= border_and_padding_.block_start;

  // TODO(kojii): Check if the line box width should be content or available.
  NGLogicalSize size(max_inline_size_, content_size_);
  container_builder_.SetSize(size).SetOverflowSize(size);

  // TODO(crbug.com/716930): We may be an empty LayoutInline due to splitting.
  // Margin struts shouldn't need to be passed through like this once we've
  // removed LayoutInline splitting.
  if (!container_builder_.BfcOffset()) {
    container_builder_.SetEndMarginStrut(ConstraintSpace().MarginStrut());
  }

  return container_builder_.ToBoxFragment();
}

}  // namespace blink
