// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/inline/ng_inline_node.h"

#include "core/layout/BidiRun.h"
#include "core/layout/LayoutBlockFlow.h"
#include "core/layout/LayoutObject.h"
#include "core/layout/LayoutText.h"
#include "core/layout/line/LineInfo.h"
#include "core/layout/line/RootInlineBox.h"
#include "core/layout/ng/inline/ng_bidi_paragraph.h"
#include "core/layout/ng/inline/ng_inline_break_token.h"
#include "core/layout/ng/inline/ng_inline_item.h"
#include "core/layout/ng/inline/ng_inline_items_builder.h"
#include "core/layout/ng/inline/ng_inline_layout_algorithm.h"
#include "core/layout/ng/inline/ng_line_box_fragment.h"
#include "core/layout/ng/inline/ng_line_breaker.h"
#include "core/layout/ng/inline/ng_physical_line_box_fragment.h"
#include "core/layout/ng/inline/ng_physical_text_fragment.h"
#include "core/layout/ng/inline/ng_text_fragment.h"
#include "core/layout/ng/ng_box_fragment.h"
#include "core/layout/ng/ng_constraint_space_builder.h"
#include "core/layout/ng/ng_fragment_builder.h"
#include "core/layout/ng/ng_layout_result.h"
#include "core/layout/ng/ng_physical_box_fragment.h"
#include "core/style/ComputedStyle.h"
#include "platform/fonts/shaping/HarfBuzzShaper.h"
#include "platform/wtf/text/CharacterNames.h"

namespace blink {

namespace {

// Set the geometry to InlineFlowBox by computing the union of children.
void PlaceInlineFlowBoxes(InlineFlowBox* flow_box) {
  LayoutUnit logical_left = LayoutUnit::Max();
  LayoutUnit logical_right = LayoutUnit::Min();
  LayoutUnit logical_top = LayoutUnit::Max();
  for (InlineBox* curr = flow_box->FirstChild(); curr;
       curr = curr->NextOnLine()) {
    if (curr->GetLineLayoutItem().IsLayoutInline()) {
      InlineFlowBox* flow = ToInlineFlowBox(curr);
      PlaceInlineFlowBoxes(flow);
    }
    logical_left = std::min(curr->LogicalLeft(), logical_left);
    logical_right = std::max(curr->LogicalRight(), logical_right);
    logical_top = std::min(curr->LogicalTop(), logical_top);
  }
  if (logical_left == LayoutUnit::Max())
    return;
  logical_left -= flow_box->MarginBorderPaddingLogicalLeft();
  logical_right += flow_box->MarginBorderPaddingLogicalRight();
  flow_box->SetLogicalLeft(logical_left);
  flow_box->SetLogicalWidth(logical_right - logical_left);
  flow_box->SetLogicalTop(logical_top);
}

}  // namespace

NGInlineNode::NGInlineNode(LayoutObject* start_inline, LayoutNGBlockFlow* block)
    : NGLayoutInputNode(NGLayoutInputNodeType::kLegacyInline),
      start_inline_(start_inline),
      block_(block) {
  DCHECK(start_inline);
  DCHECK(block);
  block->ResetNGInlineNodeData();
}

NGInlineNode::NGInlineNode()
    : NGLayoutInputNode(NGLayoutInputNodeType::kLegacyInline),
      start_inline_(nullptr),
      block_(nullptr) {}

NGInlineNode::~NGInlineNode() {}

NGInlineItemRange NGInlineNode::Items(unsigned start, unsigned end) {
  return NGInlineItemRange(&MutableData().items_, start, end);
}

void NGInlineNode::InvalidatePrepareLayout() {
  MutableData().text_content_ = String();
  MutableData().items_.clear();
}

void NGInlineNode::PrepareLayout() {
  // Scan list of siblings collecting all in-flow non-atomic inlines. A single
  // NGInlineNode represent a collection of adjacent non-atomic inlines.
  CollectInlines(start_inline_, block_);
  if (Data().is_bidi_enabled_)
    SegmentText();
  ShapeText();
}

// Depth-first-scan of all LayoutInline and LayoutText nodes that make up this
// NGInlineNode object. Collects LayoutText items, merging them up into the
// parent LayoutInline where possible, and joining all text content in a single
// string to allow bidi resolution and shaping of the entire block.
void NGInlineNode::CollectInlines(LayoutObject* start, LayoutBlockFlow* block) {
  DCHECK(Data().text_content_.IsNull());
  DCHECK(Data().items_.IsEmpty());
  NGInlineItemsBuilder builder(&MutableData().items_);
  builder.EnterBlock(block->Style());
  LayoutObject* next_sibling = CollectInlines(start, block, &builder);
  builder.ExitBlock();

  MutableData().text_content_ = builder.ToString();
  DCHECK(!next_sibling || !next_sibling->IsInline());
  next_sibling_ = next_sibling ? new NGBlockNode(next_sibling) : nullptr;
  MutableData().is_bidi_enabled_ =
      !Data().text_content_.IsEmpty() &&
      !(Data().text_content_.Is8Bit() && !builder.HasBidiControls());
}

LayoutObject* NGInlineNode::CollectInlines(LayoutObject* start,
                                           LayoutBlockFlow* block,
                                           NGInlineItemsBuilder* builder) {
  LayoutObject* node = start;
  while (node) {
    if (node->IsText()) {
      builder->SetIsSVGText(node->IsSVGInlineText());
      builder->Append(ToLayoutText(node)->GetText(), node->Style(), node);
      node->ClearNeedsLayout();

    } else if (node->IsFloating()) {
      // Add floats and positioned objects in the same way as atomic inlines.
      // Because these objects need positions, they will be handled in
      // NGInlineLayoutAlgorithm.
      builder->Append(NGInlineItem::kFloating, kObjectReplacementCharacter,
                      nullptr, node);

    } else if (node->IsOutOfFlowPositioned()) {
      builder->Append(NGInlineItem::kOutOfFlowPositioned,
                      kObjectReplacementCharacter, nullptr, node);

    } else if (node->IsAtomicInlineLevel()) {
      // For atomic inlines add a unicode "object replacement character" to
      // signal the presence of a non-text object to the unicode bidi algorithm.
      builder->Append(NGInlineItem::kAtomicInline, kObjectReplacementCharacter,
                      node->Style(), node);

    } else if (!node->IsInline()) {
      // A block box found. End inline and transit to block layout.
      return node;

    } else {
      builder->EnterInline(node);

      // Traverse to children if they exist.
      if (LayoutObject* child = node->SlowFirstChild()) {
        node = child;
        continue;

      } else {
        // An empty inline node.
        node->ClearNeedsLayout();
      }

      builder->ExitInline(node);
    }

    // Find the next sibling, or parent, until we reach |block|.
    while (true) {
      if (LayoutObject* next = node->NextSibling()) {
        node = next;
        break;
      }
      node = node->Parent();
      if (node == block)
        return nullptr;
      DCHECK(node->IsInline());
      builder->ExitInline(node);
      node->ClearNeedsLayout();
    }
  }
  return nullptr;
}

void NGInlineNode::SegmentText() {
  // TODO(kojii): Move this to caller, this will be used again after line break.
  NGBidiParagraph bidi;
  MutableData().text_content_.Ensure16Bit();
  if (!bidi.SetParagraph(Data().text_content_, Style())) {
    // On failure, give up bidi resolving and reordering.
    MutableData().is_bidi_enabled_ = false;
    return;
  }
  if (bidi.Direction() == UBIDI_LTR) {
    // All runs are LTR, no need to reorder.
    MutableData().is_bidi_enabled_ = false;
    return;
  }

  Vector<NGInlineItem>& items = MutableData().items_;
  unsigned item_index = 0;
  for (unsigned start = 0; start < Data().text_content_.length();) {
    UBiDiLevel level;
    unsigned end = bidi.GetLogicalRun(start, &level);
    DCHECK_EQ(items[item_index].start_offset_, start);
    item_index = NGInlineItem::SetBidiLevel(items, item_index, end, level);
    start = end;
  }
  DCHECK_EQ(item_index, items.size());
}

void NGInlineNode::ShapeText() {
  // TODO(eae): Add support for shaping latin-1 text?
  MutableData().text_content_.Ensure16Bit();

  // Shape each item with the full context of the entire node.
  HarfBuzzShaper shaper(Data().text_content_.Characters16(),
                        Data().text_content_.length());
  for (auto& item : MutableData().items_) {
    if (item.Type() != NGInlineItem::kText)
      continue;

    item.shape_result_ =
        shaper.Shape(&item.Style()->GetFont(), item.Direction(),
                     item.StartOffset(), item.EndOffset());
  }
}

RefPtr<NGLayoutResult> NGInlineNode::Layout(NGConstraintSpace* constraint_space,
                                            NGBreakToken* break_token) {
  // TODO(kojii): Invalidate PrepareLayout() more efficiently.
  InvalidatePrepareLayout();
  PrepareLayout();

  NGInlineLayoutAlgorithm algorithm(this, constraint_space,
                                    ToNGInlineBreakToken(break_token));
  RefPtr<NGLayoutResult> result = algorithm.Layout();
  CopyFragmentDataToLayoutBox(*constraint_space, result.Get());
  return result;
}

enum class ContentSizeMode { Max, Sum };

static LayoutUnit ComputeContentSize(NGInlineNode* node,
                                     ContentSizeMode mode,
                                     LayoutUnit available_inline_size,
                                     NGConstraintSpaceBuilder* space_builder,
                                     NGWritingMode writing_mode) {
  space_builder->SetAvailableSize({available_inline_size, NGSizeIndefinite});
  RefPtr<NGConstraintSpace> space =
      space_builder->ToConstraintSpace(writing_mode);
  NGLineBreaker line_breaker(node, space.Get());
  NGInlineLayoutAlgorithm algorithm(node, space.Get());
  NGInlineItemResults item_results;
  LayoutUnit result;
  while (true) {
    line_breaker.NextLine(&item_results, &algorithm);
    if (item_results.IsEmpty())
      break;
    LayoutUnit inline_size;
    for (const NGInlineItemResult item_result : item_results)
      inline_size += item_result.inline_size;
    if (mode == ContentSizeMode::Max) {
      result = std::max(inline_size, result);
    } else {
      result += inline_size;
    }
    item_results.clear();
  }
  return result;
}

MinMaxContentSize NGInlineNode::ComputeMinMaxContentSize() {
  if (!IsPrepareLayoutFinished())
    PrepareLayout();

  // Run line breaking with 0 and indefinite available width.

  // TODO(kojii): There are several ways to make this more efficient and faster
  // than runnning two line breaking.

  // Compute the max of inline sizes of all line boxes with 0 available inline
  // size. This gives the min-content, the width where lines wrap at every
  // break opportunity.
  const ComputedStyle& style = Style();
  NGWritingMode writing_mode = FromPlatformWritingMode(style.GetWritingMode());
  NGConstraintSpaceBuilder space_builder(writing_mode);
  space_builder.SetTextDirection(style.Direction());
  space_builder.SetAvailableSize({LayoutUnit(), NGSizeIndefinite});
  RefPtr<NGConstraintSpace> space =
      space_builder.ToConstraintSpace(writing_mode);
  MinMaxContentSize sizes;
  sizes.min_content = ComputeContentSize(
      this, ContentSizeMode::Max, LayoutUnit(), &space_builder, writing_mode);

  // Compute the sum of inline sizes of all inline boxes with no line breaks.
  // TODO(kojii): NGConstraintSpaceBuilder does not allow NGSizeIndefinite
  // inline available size. We can allow it, or make this more efficient
  // without using NGLineBreaker.
  sizes.max_content =
      ComputeContentSize(this, ContentSizeMode::Sum, LayoutUnit::Max(),
                         &space_builder, writing_mode);

  return sizes;
}

NGLayoutInputNode* NGInlineNode::NextSibling() {
  if (!IsPrepareLayoutFinished())
    PrepareLayout();
  return next_sibling_;
}

LayoutObject* NGInlineNode::GetLayoutObject() const {
  return GetLayoutBlockFlow();
}

void NGInlineNode::CopyFragmentDataToLayoutBox(
    const NGConstraintSpace& constraint_space,
    NGLayoutResult* layout_result) {
  LayoutBlockFlow* block_flow = GetLayoutBlockFlow();
  block_flow->DeleteLineBoxTree();

  const Vector<NGInlineItem>& items = Data().items_;
  Vector<unsigned, 32> text_offsets(items.size());
  GetLayoutTextOffsets(&text_offsets);

  FontBaseline baseline_type =
      IsHorizontalWritingMode(constraint_space.WritingMode())
          ? FontBaseline::kAlphabeticBaseline
          : FontBaseline::kIdeographicBaseline;

  Vector<const NGPhysicalFragment*, 32> fragments_for_bidi_runs;
  fragments_for_bidi_runs.ReserveInitialCapacity(items.size());
  BidiRunList<BidiRun> bidi_runs;
  LineInfo line_info;
  NGPhysicalBoxFragment* box_fragment =
      ToNGPhysicalBoxFragment(layout_result->PhysicalFragment().Get());
  for (const auto& container_child : box_fragment->Children()) {
    NGPhysicalLineBoxFragment* physical_line_box =
        ToNGPhysicalLineBoxFragment(container_child.Get());
    // Create a BidiRunList for this line.
    for (const auto& line_child : physical_line_box->Children()) {
      const auto* text_fragment = ToNGPhysicalTextFragment(line_child.Get());
      const NGInlineItem& item = items[text_fragment->ItemIndex()];
      BidiRun* run;
      if (item.Type() == NGInlineItem::kText) {
        LayoutObject* layout_object = item.GetLayoutObject();
        DCHECK(layout_object->IsText());
        unsigned text_offset = text_offsets[text_fragment->ItemIndex()];
        run = new BidiRun(text_fragment->StartOffset() - text_offset,
                          text_fragment->EndOffset() - text_offset,
                          item.BidiLevel(), LineLayoutItem(layout_object));
        layout_object->ClearNeedsLayout();
      } else if (item.Type() == NGInlineItem::kAtomicInline) {
        LayoutObject* layout_object = item.GetLayoutObject();
        DCHECK(layout_object->IsAtomicInlineLevel());
        run =
            new BidiRun(0, 1, item.BidiLevel(), LineLayoutItem(layout_object));
      } else {
        continue;
      }
      bidi_runs.AddRun(run);
      fragments_for_bidi_runs.push_back(text_fragment);
    }
    // TODO(kojii): bidi needs to find the logical last run.
    bidi_runs.SetLogicallyLastRun(bidi_runs.LastRun());

    // Create a RootInlineBox from BidiRunList. InlineBoxes created for the
    // RootInlineBox are set to Bidirun::m_box.
    line_info.SetEmpty(false);
    // TODO(kojii): Implement setFirstLine, LastLine, etc.
    RootInlineBox* root_line_box =
        block_flow->ConstructLine(bidi_runs, line_info);

    // Copy fragments data to InlineBoxes.
    DCHECK_EQ(fragments_for_bidi_runs.size(), bidi_runs.RunCount());
    BidiRun* run = bidi_runs.FirstRun();
    for (auto* physical_fragment : fragments_for_bidi_runs) {
      DCHECK(run);
      NGTextFragment fragment(constraint_space.WritingMode(),
                              ToNGPhysicalTextFragment(physical_fragment));
      InlineBox* inline_box = run->box_;
      inline_box->SetLogicalWidth(fragment.InlineSize());
      inline_box->SetLogicalLeft(fragment.InlineOffset());
      inline_box->SetLogicalTop(fragment.BlockOffset());
      if (inline_box->GetLineLayoutItem().IsBox()) {
        LineLayoutBox box(inline_box->GetLineLayoutItem());
        box.SetLocation(inline_box->Location());
      }
      run = run->Next();
    }
    DCHECK(!run);

    // InlineTextBox and InlineBox are placed, but when ConstructLine() created
    // InlineFlowBox, they needed to be placed as well.
    PlaceInlineFlowBoxes(root_line_box);

    // Copy to RootInlineBox.
    NGLineBoxFragment line_box(constraint_space.WritingMode(),
                               physical_line_box);
    root_line_box->SetLogicalWidth(line_box.InlineSize());
    LayoutUnit line_top = line_box.BlockOffset();
    NGLineHeightMetrics line_metrics(Style(), baseline_type);
    const NGLineHeightMetrics& max_with_leading = physical_line_box->Metrics();
    LayoutUnit baseline = line_top + max_with_leading.ascent;
    root_line_box->SetLogicalTop(baseline - line_metrics.ascent);
    root_line_box->SetLineTopBottomPositions(
        baseline - line_metrics.ascent, baseline + line_metrics.descent,
        line_top, baseline + max_with_leading.descent);

    bidi_runs.DeleteRuns();
    fragments_for_bidi_runs.clear();
  }
}

// Compute the delta of text offsets between NGInlineNode and LayoutText.
// This map is needed to produce InlineTextBox since its offsets are to
// LayoutText.
// TODO(kojii): Since NGInlineNode has text after whitespace collapsed, the
// length may not match with LayoutText. This function updates LayoutText to
// match, but this needs more careful coding, if we keep copying to layoutobject
// tree.
void NGInlineNode::GetLayoutTextOffsets(
    Vector<unsigned, 32>* text_offsets_out) {
  LayoutText* current_text = nullptr;
  unsigned current_offset = 0;
  const Vector<NGInlineItem>& items = Data().items_;

  for (unsigned i = 0; i < items.size(); i++) {
    const NGInlineItem& item = items[i];
    LayoutObject* next_object = item.GetLayoutObject();
    LayoutText* next_text = next_object && next_object->IsText()
                                ? ToLayoutText(next_object)
                                : nullptr;
    if (next_text != current_text) {
      if (current_text &&
          current_text->TextLength() != item.StartOffset() - current_offset) {
        current_text->SetTextInternal(
            Text(current_offset, item.StartOffset()).ToString().Impl());
      }
      current_text = next_text;
      current_offset = item.StartOffset();
    }
    (*text_offsets_out)[i] = current_offset;
  }
  if (current_text && current_text->TextLength() !=
                          Data().text_content_.length() - current_offset) {
    current_text->SetTextInternal(
        Text(current_offset, Data().text_content_.length()).ToString().Impl());
  }
}

String NGInlineNode::ToString() const {
  return String::Format("NGInlineNode");
}

DEFINE_TRACE(NGInlineNode) {
  visitor->Trace(next_sibling_);
  NGLayoutInputNode::Trace(visitor);
}

}  // namespace blink
