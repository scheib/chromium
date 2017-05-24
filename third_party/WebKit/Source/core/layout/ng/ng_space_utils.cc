// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_space_utils.h"

#include "core/layout/ng/ng_block_node.h"
#include "core/layout/ng/ng_exclusion.h"
#include "core/layout/ng/ng_layout_input_node.h"
#include "core/layout/ng/ng_writing_mode.h"

namespace blink {
namespace {

// Returns max of 2 {@code WTF::Optional} values.
template <typename T>
WTF::Optional<T> OptionalMax(const WTF::Optional<T>& value1,
                             const WTF::Optional<T>& value2) {
  if (value1 && value2) {
    return std::max(value1.value(), value2.value());
  } else if (value1) {
    return value1;
  }
  return value2;
}

bool IsOutOfFlowPositioned(const EPosition& position) {
  return position == EPosition::kAbsolute || position == EPosition::kFixed;
}

}  // namespace

bool IsNewFormattingContextForBlockLevelChild(const ComputedStyle& parent_style,
                                              const NGLayoutInputNode& node) {
  // TODO(layout-dev): This doesn't capture a few cases which can't be computed
  // directly from style yet:
  //  - The child is a <fieldset>.
  //  - "column-span: all" is set on the child (requires knowledge that we are
  //    in a multi-col formatting context).
  //    (https://drafts.csswg.org/css-multicol-1/#valdef-column-span-all)

  if (node.IsInline())
    return false;

  const ComputedStyle& style = node.Style();
  if (style.IsFloating() || IsOutOfFlowPositioned(style.GetPosition()))
    return true;

  if (style.SpecifiesColumns() || style.ContainsPaint() ||
      style.ContainsLayout())
    return true;

  if (!style.IsOverflowVisible())
    return true;

  EDisplay display = style.Display();
  if (display == EDisplay::kGrid || display == EDisplay::kFlex ||
      display == EDisplay::kWebkitBox)
    return true;

  if (parent_style.GetWritingMode() != style.GetWritingMode())
    return true;

  return false;
}

WTF::Optional<LayoutUnit> GetClearanceOffset(
    const std::shared_ptr<NGExclusions>& exclusions,
    EClear clear_type) {
  const NGExclusion* right_exclusion = exclusions->last_right_float;
  const NGExclusion* left_exclusion = exclusions->last_left_float;

  WTF::Optional<LayoutUnit> left_offset;
  if (left_exclusion) {
    left_offset = left_exclusion->rect.BlockEndOffset();
  }
  WTF::Optional<LayoutUnit> right_offset;
  if (right_exclusion) {
    right_offset = right_exclusion->rect.BlockEndOffset();
  }

  switch (clear_type) {
    case EClear::kNone:
      return WTF::nullopt;  // nothing to do here.
    case EClear::kLeft:
      return left_offset;
    case EClear::kRight:
      return right_offset;
    case EClear::kBoth:
      return OptionalMax<LayoutUnit>(left_offset, right_offset);
    default:
      NOTREACHED();
  }
  return WTF::nullopt;
}

bool ShouldShrinkToFit(const ComputedStyle& parent_style,
                       const ComputedStyle& style) {
  // Whether the child and the containing block are parallel to each other.
  // Example: vertical-rl and vertical-lr
  bool is_in_parallel_flow = IsParallelWritingMode(
      FromPlatformWritingMode(parent_style.GetWritingMode()),
      FromPlatformWritingMode(style.GetWritingMode()));

  return style.Display() == EDisplay::kInlineBlock || style.IsFloating() ||
         !is_in_parallel_flow;
}

void AdjustToClearance(const WTF::Optional<LayoutUnit>& clearance_offset,
                       NGLogicalOffset* offset) {
  DCHECK(offset);
  if (clearance_offset) {
    offset->block_offset =
        std::max(clearance_offset.value(), offset->block_offset);
  }
}

}  // namespace blink
