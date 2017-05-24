// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NGUnpositionedFloat_h
#define NGUnpositionedFloat_h

#include "core/layout/ng/geometry/ng_box_strut.h"
#include "core/layout/ng/geometry/ng_logical_size.h"
#include "core/layout/ng/ng_block_break_token.h"
#include "core/layout/ng/ng_block_node.h"
#include "core/layout/ng/ng_exclusion.h"
#include "core/style/ComputedStyleConstants.h"
#include "platform/wtf/Optional.h"
#include "platform/wtf/RefPtr.h"

namespace blink {

class NGPhysicalBoxFragment;

// Struct that keeps all information needed to position floats in LayoutNG.
struct CORE_EXPORT NGUnpositionedFloat
    : public RefCounted<NGUnpositionedFloat> {
 public:
  static RefPtr<NGUnpositionedFloat> Create(NGLogicalSize available_size,
                                            NGLogicalSize percentage_size,
                                            NGLogicalOffset origin_offset,
                                            NGLogicalOffset from_offset,
                                            NGBoxStrut margins,
                                            NGBlockNode* node,
                                            NGBlockBreakToken* token) {
    return AdoptRef(new NGUnpositionedFloat(margins, available_size,
                                            percentage_size, origin_offset,
                                            from_offset, node, token));
  }

  Persistent<NGBlockNode> node;
  RefPtr<NGBlockBreakToken> token;

  // Available size of the constraint space that will be used by
  // NGLayoutOpportunityIterator to position this floating object.
  NGLogicalSize available_size;
  NGLogicalSize percentage_size;

  // To correctly position a float we need 2 offsets:
  // - origin_offset which represents the layout point for this float.
  // - from_offset which represents the point from where we need to calculate
  //   the relative logical offset for this float.
  // Layout details:
  // At the time when this float is created only *inline* offsets are known.
  // Block offset will be set when we are about to place this float, i.e. when
  // we resolved MarginStrut, adjusted the offset to clearance line etc.
  NGLogicalOffset origin_offset;
  NGLogicalOffset from_offset;

  // To correctly **paint** a float we need to know the BFC offset of the
  // container to which we are attaching this float. It's used to calculate
  // {@code paint_offset}.
  // In most situations {@code paint_offset} equals to float's logical
  // offset except the cases where a float needs to be re-attached to a non-zero
  // height parent.
  //
  // Example:
  //   <body>
  //     <p style="height: 60px">Example</p>
  //     <div id="zero-height-div"><float></div>
  //
  // Here the float's logical offset is 0 relative to its #zero-height-div
  // parent. Because of the "zero height" div this float is re-attached to the
  // 1st non-empty parent => body. To paint this float correctly we provide the
  // modified {@code paint_offset} which is relative to the float's new
  // parent.
  // I.e. for our example {@code paint_offset.top} ==
  //                      #zero-height-div's BFC offset
  //                      - body's BFC offset + float's logical offset
  //
  // For code safety reasons {@code parent_bfc_block_offset} is Optional here
  // because the block's offset can be only determined before the actual float's
  // placement event.
  WTF::Optional<LayoutUnit> parent_bfc_block_offset;

  // The margins are relative to the writing mode of the block formatting
  // context. They are stored for convinence and could be recomputed with other
  // data on this object.
  NGBoxStrut margins;

  // The fragment for this unpositioned float. This is only present if it's in
  // a different writing mode than the BFC.
  WTF::Optional<RefPtr<NGPhysicalBoxFragment>> fragment;

  bool IsLeft() const;
  bool IsRight() const;
  EClear ClearType() const;

 private:
  NGUnpositionedFloat(const NGBoxStrut& margins,
                      const NGLogicalSize& available_size,
                      const NGLogicalSize& percentage_size,
                      const NGLogicalOffset& origin_offset,
                      const NGLogicalOffset& from_offset,
                      NGBlockNode* node,
                      NGBlockBreakToken* token)
      : node(node),
        token(token),
        available_size(available_size),
        percentage_size(percentage_size),
        origin_offset(origin_offset),
        from_offset(from_offset),
        margins(margins) {}
};

}  // namespace blink

#endif  // NGUnpositionedFloat_h
