// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_block_layout_algorithm.h"

#include "core/layout/ng/inline/ng_inline_node.h"
#include "core/layout/ng/ng_absolute_utils.h"
#include "core/layout/ng/ng_block_child_iterator.h"
#include "core/layout/ng/ng_constraint_space.h"
#include "core/layout/ng/ng_constraint_space_builder.h"
#include "core/layout/ng/ng_floats_utils.h"
#include "core/layout/ng/ng_fragment.h"
#include "core/layout/ng/ng_fragment_builder.h"
#include "core/layout/ng/ng_layout_opportunity_iterator.h"
#include "core/layout/ng/ng_layout_result.h"
#include "core/layout/ng/ng_length_utils.h"
#include "core/layout/ng/ng_out_of_flow_layout_part.h"
#include "core/layout/ng/ng_space_utils.h"
#include "core/style/ComputedStyle.h"
#include "platform/LengthFunctions.h"
#include "platform/wtf/Optional.h"

namespace blink {
namespace {

// Returns if a child may be affected by its clear property. I.e. it will
// actually clear a float.
bool ClearanceMayAffectLayout(
    const NGConstraintSpace& space,
    const Vector<RefPtr<NGUnpositionedFloat>>& unpositioned_floats,
    const ComputedStyle& child_style) {
  const NGExclusions& exclusions = *space.Exclusions();
  EClear clear = child_style.Clear();
  bool should_clear_left = (clear == EClear::kBoth || clear == EClear::kLeft);
  bool should_clear_right = (clear == EClear::kBoth || clear == EClear::kRight);

  if (exclusions.last_left_float && should_clear_left)
    return true;

  if (exclusions.last_right_float && should_clear_right)
    return true;

  auto should_clear_pred =
      [&](const RefPtr<const NGUnpositionedFloat>& unpositioned_float) {
        return (unpositioned_float->IsLeft() && should_clear_left) ||
               (unpositioned_float->IsRight() && should_clear_right);
      };

  if (std::any_of(unpositioned_floats.begin(), unpositioned_floats.end(),
                  should_clear_pred))
    return true;

  return false;
}

bool IsLegacyBlock(const NGLayoutInputNode& node) {
  return node.IsBlock() && !ToNGBlockNode(node).CanUseNewLayout();
}

// Whether we've run out of space in this flow. If so, there will be no work
// left to do for this block in this fragmentainer.
bool IsOutOfSpace(const NGConstraintSpace& space, LayoutUnit content_size) {
  return space.HasBlockFragmentation() &&
         content_size >= space.FragmentainerSpaceAvailable();
}

}  // namespace

void MaybeUpdateFragmentBfcOffset(const NGConstraintSpace& space,
                                  const NGLogicalOffset& offset,
                                  NGFragmentBuilder* builder) {
  DCHECK(builder);
  if (!builder->BfcOffset()) {
    NGLogicalOffset mutable_offset(offset);
    AdjustToClearance(space.ClearanceOffset(), &mutable_offset);
    builder->SetBfcOffset(mutable_offset);
  }
}

void PositionPendingFloatsFromOffset(LayoutUnit origin_block_offset,
                                     LayoutUnit from_block_offset,
                                     NGFragmentBuilder* container_builder,
                                     NGConstraintSpace* space) {
  DCHECK(container_builder->BfcOffset())
      << "Parent BFC offset should be known here";
  const auto& unpositioned_floats = container_builder->UnpositionedFloats();
  const auto positioned_floats =
      PositionFloats(origin_block_offset, from_block_offset,
                     container_builder->BfcOffset().value().block_offset,
                     unpositioned_floats, space);
  for (const auto& positioned_float : positioned_floats)
    container_builder->AddPositionedFloat(positioned_float);

  container_builder->MutableUnpositionedFloats().clear();
}

void PositionPendingFloats(LayoutUnit origin_block_offset,
                           NGFragmentBuilder* container_builder,
                           NGConstraintSpace* space) {
  DCHECK(container_builder->BfcOffset())
      << "Parent BFC offset should be known here";
  LayoutUnit from_block_offset =
      container_builder->BfcOffset().value().block_offset;
  PositionPendingFloatsFromOffset(origin_block_offset, from_block_offset,
                                  container_builder, space);
}

NGBlockLayoutAlgorithm::NGBlockLayoutAlgorithm(NGBlockNode* node,
                                               NGConstraintSpace* space,
                                               NGBlockBreakToken* break_token)
    : NGLayoutAlgorithm(node, space, break_token) {}

Optional<MinMaxContentSize> NGBlockLayoutAlgorithm::ComputeMinMaxContentSize()
    const {
  MinMaxContentSize sizes;

  // Size-contained elements don't consider their contents for intrinsic sizing.
  if (Style().ContainsSize())
    return sizes;

  // TODO: handle floats & orthogonal children.
  for (NGLayoutInputNode* node = Node()->FirstChild(); node;
       node = node->NextSibling()) {
    MinMaxContentSize child_sizes;
    if (node->IsInline()) {
      // From |NGBlockLayoutAlgorithm| perspective, we can handle |NGInlineNode|
      // almost the same as |NGBlockNode|, because an |NGInlineNode| includes
      // all inline nodes following |node| and their descendants, and produces
      // an anonymous box that contains all line boxes.
      // |NextSibling| returns the next block sibling, or nullptr, skipping all
      // following inline siblings and descendants.
      child_sizes = node->ComputeMinMaxContentSize();
    } else {
      Optional<MinMaxContentSize> child_minmax;
      if (NeedMinMaxContentSizeForContentContribution(node->Style())) {
        child_minmax = node->ComputeMinMaxContentSize();
      }

      child_sizes =
          ComputeMinAndMaxContentContribution(node->Style(), child_minmax);
    }

    sizes.min_content = std::max(sizes.min_content, child_sizes.min_content);
    sizes.max_content = std::max(sizes.max_content, child_sizes.max_content);
  }

  sizes.max_content = std::max(sizes.min_content, sizes.max_content);
  return sizes;
}

NGLogicalOffset NGBlockLayoutAlgorithm::CalculateLogicalOffset(
    const WTF::Optional<NGLogicalOffset>& known_fragment_offset) {
  if (known_fragment_offset)
    return known_fragment_offset.value() - ContainerBfcOffset();
  LayoutUnit inline_offset =
      border_and_padding_.inline_start + curr_child_margins_.inline_start;
  return {inline_offset, content_size_};
}

RefPtr<NGLayoutResult> NGBlockLayoutAlgorithm::Layout() {
  WTF::Optional<MinMaxContentSize> min_max_size;
  if (NeedMinMaxContentSize(ConstraintSpace(), Style()))
    min_max_size = ComputeMinMaxContentSize();

  border_and_padding_ = ComputeBorders(ConstraintSpace(), Style()) +
                        ComputePadding(ConstraintSpace(), Style());

  // TODO(layout-ng): For quirks mode, should we pass blockSize instead of -1?
  NGLogicalSize size(
      ComputeInlineSizeForFragment(ConstraintSpace(), Style(), min_max_size),
      ComputeBlockSizeForFragment(ConstraintSpace(), Style(),
                                  NGSizeIndefinite));

  // Our calculated block-axis size may be indefinite at this point.
  // If so, just leave the size as NGSizeIndefinite instead of subtracting
  // borders and padding.
  NGLogicalSize adjusted_size(size);
  if (size.block_size == NGSizeIndefinite)
    adjusted_size.inline_size -= border_and_padding_.InlineSum();
  else
    adjusted_size -= border_and_padding_;

  child_available_size_ = adjusted_size;
  child_percentage_size_ = adjusted_size;

  container_builder_.SetDirection(constraint_space_->Direction());
  container_builder_.SetWritingMode(constraint_space_->WritingMode());
  container_builder_.SetSize(size);
  container_builder_.MutableUnpositionedFloats() =
      constraint_space_->UnpositionedFloats();

  NGBlockChildIterator child_iterator(Node()->FirstChild(), BreakToken());
  NGBlockChildIterator::Entry entry = child_iterator.NextChild();
  NGLayoutInputNode* child = entry.node;
  NGBreakToken* child_break_token = entry.token;

  // If we are resuming from a break token our start border and padding is
  // within a previous fragment.
  content_size_ = BreakToken() ? LayoutUnit() : border_and_padding_.block_start;

  curr_margin_strut_ = ConstraintSpace().MarginStrut();
  curr_bfc_offset_ = ConstraintSpace().BfcOffset();

  // Margins collapsing:
  //   Do not collapse margins between parent and its child if there is
  //   border/padding between them.
  if (border_and_padding_.block_start) {
    curr_bfc_offset_.block_offset += curr_margin_strut_.Sum();
    MaybeUpdateFragmentBfcOffset(ConstraintSpace(), curr_bfc_offset_,
                                 &container_builder_);
    curr_margin_strut_ = NGMarginStrut();
  }

  // If a new formatting context hits the margin collapsing if-branch above
  // then the BFC offset is still {} as the margin strut from the constraint
  // space must also be empty.
  // If we are resuming layout from a break token the same rule applies. Margin
  // struts cannot pass through break tokens.
  if (ConstraintSpace().IsNewFormattingContext() || BreakToken()) {
    MaybeUpdateFragmentBfcOffset(ConstraintSpace(), curr_bfc_offset_,
                                 &container_builder_);
    DCHECK_EQ(curr_margin_strut_, NGMarginStrut());
    DCHECK_EQ(container_builder_.BfcOffset().value(), NGLogicalOffset());
    curr_bfc_offset_ = {};
  }

  curr_bfc_offset_.block_offset += content_size_;

  while (child) {
    if (child->IsOutOfFlowPositioned()) {
      DCHECK(!child_break_token);
      HandleOutOfFlowPositioned(ToNGBlockNode(child));
    } else if (child->IsFloating()) {
      HandleFloating(ToNGBlockNode(child),
                     ToNGBlockBreakToken(child_break_token));
    } else {
      NGLogicalOffset child_bfc_offset = PrepareChildLayout(child);
      RefPtr<NGConstraintSpace> child_space =
          CreateConstraintSpaceForChild(child_bfc_offset, *child);
      RefPtr<NGLayoutResult> layout_result =
          child->Layout(child_space.Get(), child_break_token);
      FinishChildLayout(*child_space, child, layout_result.Get());
    }

    entry = child_iterator.NextChild();
    child = entry.node;
    child_break_token = entry.token;

    if (IsOutOfSpace(ConstraintSpace(), content_size_))
      break;
  }

  // Margins collapsing:
  //   Bottom margins of an in-flow block box doesn't collapse with its last
  //   in-flow block-level child's bottom margin if the box has bottom
  //   border/padding.
  content_size_ += border_and_padding_.block_end;
  if (border_and_padding_.block_end ||
      ConstraintSpace().IsNewFormattingContext()) {
    content_size_ += curr_margin_strut_.Sum();
    curr_margin_strut_ = NGMarginStrut();
  }

  // Recompute the block-axis size now that we know our content size.
  size.block_size =
      ComputeBlockSizeForFragment(ConstraintSpace(), Style(), content_size_);
  container_builder_.SetBlockSize(size.block_size);

  // Layout our absolute and fixed positioned children.
  NGOutOfFlowLayoutPart(ConstraintSpace(), Style(), &container_builder_).Run();

  // Non-empty blocks always know their position in space.
  // TODO(ikilpatrick): This check for a break token seems error prone.
  if (size.block_size || BreakToken()) {
    curr_bfc_offset_.block_offset += curr_margin_strut_.Sum();
    MaybeUpdateFragmentBfcOffset(ConstraintSpace(), curr_bfc_offset_,
                                 &container_builder_);
    PositionPendingFloats(curr_bfc_offset_.block_offset, &container_builder_,
                          MutableConstraintSpace());
  }

  // Margins collapsing:
  //   Do not collapse margins between the last in-flow child and bottom margin
  //   of its parent if the parent has height != auto()
  if (!Style().LogicalHeight().IsAuto()) {
    // TODO(glebl): handle minLogicalHeight, maxLogicalHeight.
    curr_margin_strut_ = NGMarginStrut();
  }
  container_builder_.SetEndMarginStrut(curr_margin_strut_);

  container_builder_.SetOverflowSize(
      NGLogicalSize(max_inline_size_, content_size_));

  // We only finalize for fragmentation if the fragment has a BFC offset. This
  // may occur with a zero block size fragment. We need to know the BFC offset
  // to determine where the fragmentation line is relative to us.
  if (container_builder_.BfcOffset() &&
      ConstraintSpace().HasBlockFragmentation())
    FinalizeForFragmentation();

  return container_builder_.ToBoxFragment();
}

void NGBlockLayoutAlgorithm::HandleOutOfFlowPositioned(NGBlockNode* child) {
  NGLogicalOffset offset = {border_and_padding_.inline_start, content_size_};

  // We only include the margin strut in the OOF static-position if we know we
  // aren't going to be a zero-block-size fragment.
  if (container_builder_.BfcOffset())
    offset.block_offset += curr_margin_strut_.Sum();

  container_builder_.AddOutOfFlowChildCandidate(child, offset);
}

void NGBlockLayoutAlgorithm::HandleFloating(NGBlockNode* child,
                                            NGBlockBreakToken* token) {
  // TODO(ikilpatrick): Pass in BFC offset from previous in-flow child.
  curr_bfc_offset_ = container_builder_.BfcOffset()
                         ? container_builder_.BfcOffset().value()
                         : ConstraintSpace().BfcOffset();
  curr_bfc_offset_.block_offset += content_size_;

  // Calculate margins in the BFC's writing mode.
  curr_child_margins_ = CalculateMargins(child);

  NGLogicalOffset origin_offset = constraint_space_->BfcOffset();
  origin_offset.inline_offset += border_and_padding_.inline_start;
  RefPtr<NGUnpositionedFloat> unpositioned_float = NGUnpositionedFloat::Create(
      child_available_size_, child_percentage_size_, origin_offset,
      constraint_space_->BfcOffset(), curr_child_margins_, child, token);
  container_builder_.AddUnpositionedFloat(unpositioned_float);

  // If there is a break token for a float we must be resuming layout, we must
  // always know our position in the BFC.
  DCHECK(!token || container_builder_.BfcOffset());

  // No need to postpone the positioning if we know the correct offset.
  if (container_builder_.BfcOffset()) {
    NGLogicalOffset origin_point = curr_bfc_offset_;
    // Adjust origin point to the margins of the last child.
    // Example: <div style="margin-bottom: 20px"><float></div>
    //          <div style="margin-bottom: 30px"></div>
    origin_point.block_offset += curr_margin_strut_.Sum();
    PositionPendingFloats(origin_point.block_offset, &container_builder_,
                          MutableConstraintSpace());
  }
}

NGLogicalOffset NGBlockLayoutAlgorithm::PrepareChildLayout(
    NGLayoutInputNode* child) {
  DCHECK(child);
  DCHECK(!child->IsFloating());

  // TODO(ikilpatrick): Pass in BFC offset from previous in-flow child.
  curr_bfc_offset_ = container_builder_.BfcOffset()
                         ? container_builder_.BfcOffset().value()
                         : ConstraintSpace().BfcOffset();
  curr_bfc_offset_.block_offset += content_size_;

  // Calculate margins in parent's writing mode.
  curr_child_margins_ = CalculateMargins(child);

  bool should_position_pending_floats =
      !IsNewFormattingContextForBlockLevelChild(Style(), *child) &&
      ClearanceMayAffectLayout(ConstraintSpace(),
                               container_builder_.UnpositionedFloats(),
                               child->Style());

  // Children which may clear a float need to force all the pending floats to
  // be positioned before layout. This also resolves the fragment's bfc offset.
  if (should_position_pending_floats) {
    LayoutUnit origin_point_block_offset =
        curr_bfc_offset_.block_offset + curr_margin_strut_.Sum();
    MaybeUpdateFragmentBfcOffset(
        ConstraintSpace(),
        {curr_bfc_offset_.inline_offset, origin_point_block_offset},
        &container_builder_);
    PositionPendingFloats(origin_point_block_offset, &container_builder_,
                          MutableConstraintSpace());
  }

  NGLogicalOffset child_bfc_offset = curr_bfc_offset_;
  child_bfc_offset.inline_offset +=
      {border_and_padding_.inline_start + curr_child_margins_.inline_start};

  // Append the current margin strut with child's block start margin.
  // Non empty border/padding, and new FC use cases are handled inside of the
  // child's layout.
  if (!IsNewFormattingContextForBlockLevelChild(Style(), *child))
    curr_margin_strut_.Append(curr_child_margins_.block_start);

  // TODO(crbug.com/716930): We should also collapse margins below once we
  // remove LayoutInline splitting.

  // Should collapse margins if our child is a legacy block.
  if (IsLegacyBlock(*child)) {
    curr_bfc_offset_ +=
        {border_and_padding_.inline_start + curr_child_margins_.inline_start,
         curr_margin_strut_.Sum()};
    MaybeUpdateFragmentBfcOffset(ConstraintSpace(), curr_bfc_offset_,
                                 &container_builder_);
    PositionPendingFloats(curr_bfc_offset_.block_offset, &container_builder_,
                          MutableConstraintSpace());
    curr_margin_strut_ = {};
  }
  child_bfc_offset.block_offset = curr_bfc_offset_.block_offset;
  return child_bfc_offset;
}

void NGBlockLayoutAlgorithm::FinishChildLayout(
    const NGConstraintSpace& child_space,
    const NGLayoutInputNode* child,
    NGLayoutResult* layout_result) {
  // Pull out unpositioned floats to the current fragment. This may needed if
  // for example the child fragment could not position its floats because it's
  // empty and therefore couldn't determine its position in space.
  container_builder_.MutableUnpositionedFloats().AppendVector(
      layout_result->UnpositionedFloats());

  NGBoxFragment fragment(
      ConstraintSpace().WritingMode(),
      ToNGPhysicalBoxFragment(layout_result->PhysicalFragment().Get()));

  // Determine the fragment's position in the parent space.
  WTF::Optional<NGLogicalOffset> child_bfc_offset;
  if (child_space.IsNewFormattingContext())
    child_bfc_offset = PositionNewFc(fragment, child_space);
  else if (fragment.BfcOffset())
    child_bfc_offset = PositionWithBfcOffset(fragment);
  else if (IsLegacyBlock(*child))
    child_bfc_offset = PositionLegacy(child_space);
  else if (container_builder_.BfcOffset())
    child_bfc_offset = PositionWithParentBfc(child_space, fragment);

  NGLogicalOffset logical_offset = CalculateLogicalOffset(child_bfc_offset);

  // Update margin strut.
  curr_margin_strut_ = fragment.EndMarginStrut();
  curr_margin_strut_.Append(curr_child_margins_.block_end);

  // Only modify content_size if BlockSize is not empty. It's needed to prevent
  // the situation when logical_offset is included in content_size for empty
  // blocks. Example:
  //   <div style="overflow:hidden">
  //     <div style="margin-top: 8px"></div>
  //     <div style="margin-top: 10px"></div>
  //   </div>
  if (fragment.BlockSize())
    content_size_ = fragment.BlockSize() + logical_offset.block_offset;
  max_inline_size_ =
      std::max(max_inline_size_, fragment.InlineSize() +
                                     curr_child_margins_.InlineSum() +
                                     border_and_padding_.InlineSum());

  container_builder_.AddChild(layout_result, logical_offset);
}

NGLogicalOffset NGBlockLayoutAlgorithm::PositionNewFc(
    const NGBoxFragment& fragment,
    const NGConstraintSpace& child_space) {
  // 1. Position all pending floats to a temporary space.
  RefPtr<NGConstraintSpace> tmp_space =
      NGConstraintSpaceBuilder(&child_space)
          .SetIsNewFormattingContext(false)
          .ToConstraintSpace(child_space.WritingMode());
  PositionFloats(curr_bfc_offset_.block_offset, curr_bfc_offset_.block_offset,
                 curr_bfc_offset_.block_offset,
                 container_builder_.UnpositionedFloats(), tmp_space.Get());

  NGLogicalOffset origin_offset = curr_bfc_offset_;
  origin_offset.inline_offset += border_and_padding_.inline_start;

  // 2. Find an estimated layout opportunity for our fragment.
  NGLayoutOpportunity opportunity = FindLayoutOpportunityForFragment(
      tmp_space->Exclusions().get(), child_space.AvailableSize(), origin_offset,
      curr_child_margins_, fragment.Size());

  // 3. If the found opportunity lies on the same line with our estimated
  //    child's BFC offset then merge fragment's margins with the current
  //    MarginStrut.
  if (opportunity.offset.block_offset == curr_bfc_offset_.block_offset)
    curr_margin_strut_.Append(curr_child_margins_.block_start);
  curr_bfc_offset_.block_offset += curr_margin_strut_.Sum();
  curr_margin_strut_ = {};

  // 4. The child's BFC block offset is known here.
  MaybeUpdateFragmentBfcOffset(ConstraintSpace(), curr_bfc_offset_,
                               &container_builder_);
  PositionPendingFloats(curr_bfc_offset_.block_offset, &container_builder_,
                        MutableConstraintSpace());

  origin_offset = curr_bfc_offset_;
  origin_offset.inline_offset += border_and_padding_.inline_start;

  // 5. Find the final layout opportunity for the fragment after all pending
  // floats are positioned at the correct BFC block's offset.
  opportunity = FindLayoutOpportunityForFragment(
      MutableConstraintSpace()->Exclusions().get(), child_space.AvailableSize(),
      origin_offset, curr_child_margins_, fragment.Size());

  curr_bfc_offset_ = opportunity.offset;
  return curr_bfc_offset_;
}

NGLogicalOffset NGBlockLayoutAlgorithm::PositionWithBfcOffset(
    const NGBoxFragment& fragment) {
  DCHECK(fragment.BfcOffset());
  curr_bfc_offset_.block_offset = fragment.BfcOffset().value().block_offset;
  MaybeUpdateFragmentBfcOffset(ConstraintSpace(), curr_bfc_offset_,
                               &container_builder_);
  PositionPendingFloats(curr_bfc_offset_.block_offset, &container_builder_,
                        MutableConstraintSpace());
  return fragment.BfcOffset().value();
}

NGLogicalOffset NGBlockLayoutAlgorithm::PositionWithParentBfc(
    const NGConstraintSpace& space,
    const NGBoxFragment& fragment) {
  // The child must be an in-flow zero-block-size fragment, use its end margin
  // strut for positioning.
  DCHECK(!fragment.BfcOffset());
  DCHECK_EQ(fragment.BlockSize(), LayoutUnit());

  NGMarginStrut margin_strut = fragment.EndMarginStrut();
  margin_strut.Append(curr_child_margins_.block_end);

  curr_bfc_offset_ +=
      {border_and_padding_.inline_start + curr_child_margins_.inline_start,
       margin_strut.Sum()};
  AdjustToClearance(space.ClearanceOffset(), &curr_bfc_offset_);
  PositionPendingFloatsFromOffset(
      curr_bfc_offset_.block_offset, curr_bfc_offset_.block_offset,
      &container_builder_, MutableConstraintSpace());
  return curr_bfc_offset_;
}

NGLogicalOffset NGBlockLayoutAlgorithm::PositionLegacy(
    const NGConstraintSpace& child_space) {
  AdjustToClearance(child_space.ClearanceOffset(), &curr_bfc_offset_);
  return curr_bfc_offset_;
}

void NGBlockLayoutAlgorithm::FinalizeForFragmentation() {
  LayoutUnit used_block_size =
      BreakToken() ? BreakToken()->UsedBlockSize() : LayoutUnit();
  LayoutUnit block_size = ComputeBlockSizeForFragment(
      ConstraintSpace(), Style(), used_block_size + content_size_);

  block_size -= used_block_size;
  DCHECK_GE(block_size, LayoutUnit())
      << "Adding and subtracting the used_block_size shouldn't leave the "
         "block_size for this fragment smaller than zero.";

  LayoutUnit space_left = ConstraintSpace().FragmentainerSpaceAvailable() -
                          ContainerBfcOffset().block_offset;
  DCHECK_GE(space_left, LayoutUnit());

  if (container_builder_.DidBreak()) {
    // One of our children broke. Even if we fit within the remaining space we
    // need to prepare a break token.
    container_builder_.SetUsedBlockSize(std::min(space_left, block_size) +
                                        used_block_size);
    container_builder_.SetBlockSize(std::min(space_left, block_size));
    container_builder_.SetBlockOverflow(space_left);
    return;
  }

  if (block_size > space_left) {
    // Need a break inside this block.
    container_builder_.SetUsedBlockSize(space_left + used_block_size);
    container_builder_.SetBlockSize(space_left);
    container_builder_.SetBlockOverflow(space_left);
    return;
  }

  // The end of the block fits in the current fragmentainer.
  container_builder_.SetBlockSize(block_size);
  container_builder_.SetBlockOverflow(content_size_);
}

NGBoxStrut NGBlockLayoutAlgorithm::CalculateMargins(NGLayoutInputNode* child) {
  DCHECK(child);
  if (child->IsInline())
    return {};
  const ComputedStyle& child_style = child->Style();

  RefPtr<NGConstraintSpace> space =
      NGConstraintSpaceBuilder(MutableConstraintSpace())
          .SetAvailableSize(child_available_size_)
          .SetPercentageResolutionSize(child_percentage_size_)
          .ToConstraintSpace(ConstraintSpace().WritingMode());

  NGBoxStrut margins = ComputeMargins(*space, child_style, space->WritingMode(),
                                      space->Direction());

  // TODO(ikilpatrick): Move the auto margins calculation for different writing
  // modes to post-layout.
  if (!child->IsFloating()) {
    WTF::Optional<MinMaxContentSize> sizes;
    if (NeedMinMaxContentSize(*space, child_style))
      sizes = child->ComputeMinMaxContentSize();

    LayoutUnit child_inline_size =
        ComputeInlineSizeForFragment(*space, child_style, sizes);
    ApplyAutoMargins(*space, child_style, child_inline_size, &margins);
  }
  return margins;
}

RefPtr<NGConstraintSpace> NGBlockLayoutAlgorithm::CreateConstraintSpaceForChild(
    const NGLogicalOffset& child_bfc_offset,
    const NGLayoutInputNode& child) {
  NGConstraintSpaceBuilder space_builder(MutableConstraintSpace());
  space_builder.SetAvailableSize(child_available_size_)
      .SetPercentageResolutionSize(child_percentage_size_);

  const ComputedStyle& child_style = child.Style();
  bool is_new_bfc = IsNewFormattingContextForBlockLevelChild(Style(), child);
  space_builder.SetIsNewFormattingContext(is_new_bfc)
      .SetBfcOffset(child_bfc_offset)
      .SetMarginStrut(curr_margin_strut_);

  if (!is_new_bfc) {
    space_builder.SetUnpositionedFloats(
        container_builder_.MutableUnpositionedFloats());
  }

  if (child.IsInline()) {
    // TODO(kojii): Setup space_builder appropriately for inline child.
    space_builder.SetClearanceOffset(ConstraintSpace().ClearanceOffset());
    return space_builder.ToConstraintSpace(
        FromPlatformWritingMode(Style().GetWritingMode()));
  }

  space_builder
      .SetClearanceOffset(GetClearanceOffset(constraint_space_->Exclusions(),
                                             child_style.Clear()))
      .SetIsShrinkToFit(ShouldShrinkToFit(Style(), child_style))
      .SetTextDirection(child_style.Direction());

  LayoutUnit space_available;
  if (constraint_space_->HasBlockFragmentation()) {
    space_available = ConstraintSpace().FragmentainerSpaceAvailable();
    // If a block establishes a new formatting context we must know our
    // position in the formatting context, and are able to adjust the
    // fragmentation line.
    if (is_new_bfc) {
      space_available -= child_bfc_offset.block_offset;
    }
  }
  space_builder.SetFragmentainerSpaceAvailable(space_available);

  return space_builder.ToConstraintSpace(
      FromPlatformWritingMode(child_style.GetWritingMode()));
}
}  // namespace blink
