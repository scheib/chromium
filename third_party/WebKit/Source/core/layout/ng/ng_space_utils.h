// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NGSpaceUtils_h
#define NGSpaceUtils_h

#include "core/CoreExport.h"
#include "core/style/ComputedStyleConstants.h"
#include "platform/LayoutUnit.h"
#include "platform/wtf/Optional.h"

namespace blink {

class ComputedStyle;
struct NGExclusions;
class NGLayoutInputNode;
struct NGLogicalOffset;

// Whether an in-flow child creates a new formatting context.
//
// This will *NOT* check the following cases:
//  - The child is a inline-level, e.g. "display: inline-block".
//  - The child establishes a new formatting context, but should be a child of
//    another layout algorithm, e.g. "display: table-caption" or flex-item.
CORE_EXPORT bool IsNewFormattingContextForBlockLevelChild(
    const ComputedStyle& parent_style,
    const NGLayoutInputNode& node);

// Gets the clearance offset based on the provided {@code clear_type} and list
// of exclusions that represent left/right float.
CORE_EXPORT WTF::Optional<LayoutUnit> GetClearanceOffset(
    const std::shared_ptr<NGExclusions>& exclusions,
    EClear clear_type);

// Whether child's constraint space should shrink to its intrinsic width.
// This is needed for buttons, select, input, floats and orthogonal children.
// See LayoutBox::sizesLogicalWidthToFitContent for the rationale behind this.
bool ShouldShrinkToFit(const ComputedStyle& parent_style,
                       const ComputedStyle& style);

// Adjusts {@code offset} to the clearance line.
CORE_EXPORT void AdjustToClearance(
    const WTF::Optional<LayoutUnit>& clearance_offset,
    NGLogicalOffset* offset);

}  // namespace blink

#endif  // NGSpaceUtils_h
