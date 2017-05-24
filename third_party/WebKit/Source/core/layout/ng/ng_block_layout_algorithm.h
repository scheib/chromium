// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NGBlockLayoutAlgorithm_h
#define NGBlockLayoutAlgorithm_h

#include "core/CoreExport.h"
#include "core/layout/ng/geometry/ng_margin_strut.h"
#include "core/layout/ng/ng_block_break_token.h"
#include "core/layout/ng/ng_block_node.h"
#include "core/layout/ng/ng_box_fragment.h"
#include "core/layout/ng/ng_constraint_space_builder.h"
#include "core/layout/ng/ng_layout_algorithm.h"
#include "platform/wtf/RefPtr.h"

namespace blink {

class NGConstraintSpace;
class NGLayoutResult;

// Updates the fragment's BFC offset if it's not already set.
void MaybeUpdateFragmentBfcOffset(const NGConstraintSpace&,
                                  const NGLogicalOffset&,
                                  NGFragmentBuilder* builder);

// Positions pending floats starting from {@origin_block_offset} and relative
// to container's BFC offset.
void PositionPendingFloats(LayoutUnit origin_block_offset,
                           NGFragmentBuilder* container_builder,
                           NGConstraintSpace* space);

// A class for general block layout (e.g. a <div> with no special style).
// Lays out the children in sequence.
class CORE_EXPORT NGBlockLayoutAlgorithm
    : public NGLayoutAlgorithm<NGBlockNode, NGBlockBreakToken> {
 public:
  // Default constructor.
  // @param node The input node to perform layout upon.
  // @param space The constraint space which the algorithm should generate a
  //              fragment within.
  // @param break_token The break token from which the layout should start.
  NGBlockLayoutAlgorithm(NGBlockNode* node,
                         NGConstraintSpace* space,
                         NGBlockBreakToken* break_token = nullptr);

  Optional<MinMaxContentSize> ComputeMinMaxContentSize() const override;
  virtual RefPtr<NGLayoutResult> Layout() override;

 private:
  NGBoxStrut CalculateMargins(NGLayoutInputNode* child);

  // Creates a new constraint space for the current child.
  RefPtr<NGConstraintSpace> CreateConstraintSpaceForChild(
      const NGLogicalOffset& child_bfc_offset,
      const NGLayoutInputNode&);

  // @return Estimated BFC offset for the "to be layout" child.
  NGLogicalOffset PrepareChildLayout(NGLayoutInputNode*);

  void FinishChildLayout(const NGConstraintSpace&,
                         const NGLayoutInputNode* child,
                         NGLayoutResult*);

  // Positions the fragment that establishes a new formatting context.
  //
  // This uses Layout Opportunity iterator to position the fragment.
  // That's because an element that establishes a new block formatting context
  // must not overlap the margin box of any floats in the same block formatting
  // context as the element itself.
  //
  // So if necessary, we clear the new BFC by placing it below any preceding
  // floats or place it adjacent to such floats if there is sufficient space.
  //
  // Example:
  // <div id="container">
  //   <div id="float"></div>
  //   <div id="new-fc" style="margin-top: 20px;"></div>
  // </div>
  // 1) If #new-fc is small enough to fit the available space right from #float
  //    then it will be placed there and we collapse its margin.
  // 2) If #new-fc is too big then we need to clear its position and place it
  //    below #float ignoring its vertical margin.
  NGLogicalOffset PositionNewFc(const NGBoxFragment&,
                                const NGConstraintSpace& child_space);

  // Positions the fragment that knows its BFC offset.
  NGLogicalOffset PositionWithBfcOffset(const NGBoxFragment&);

  // Positions using the parent BFC offset.
  // Fragment doesn't know its offset but we can still calculate its BFC
  // position because the parent fragment's BFC is known.
  // Example:
  //   BFC Offset is known here because of the padding.
  //   <div style="padding: 1px">
  //     <div id="empty-div" style="margins: 1px"></div>
  NGLogicalOffset PositionWithParentBfc(const NGConstraintSpace&,
                                        const NGBoxFragment&);

  NGLogicalOffset PositionLegacy(const NGConstraintSpace& child_space);

  void HandleOutOfFlowPositioned(NGBlockNode*);
  void HandleFloating(NGBlockNode*, NGBlockBreakToken*);

  // Final adjustments before fragment creation. We need to prevent the
  // fragment from crossing fragmentainer boundaries, and rather create a break
  // token if we're out of space.
  void FinalizeForFragmentation();

  // Calculates logical offset for the current fragment using either
  // {@code content_size_} when the fragment doesn't know it's offset
  // or {@code known_fragment_offset} if the fragment knows it's offset
  // @return Fragment's offset relative to the fragment's parent.
  NGLogicalOffset CalculateLogicalOffset(
      const WTF::Optional<NGLogicalOffset>& known_fragment_offset);

  NGLogicalSize child_available_size_;
  NGLogicalSize child_percentage_size_;

  NGBoxStrut border_and_padding_;
  LayoutUnit content_size_;
  LayoutUnit max_inline_size_;
  // MarginStrut for the previous child.
  NGMarginStrut curr_margin_strut_;
  NGLogicalOffset curr_bfc_offset_;
  NGBoxStrut curr_child_margins_;
};

}  // namespace blink

#endif  // NGBlockLayoutAlgorithm_h
