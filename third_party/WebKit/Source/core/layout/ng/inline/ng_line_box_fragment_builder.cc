// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/inline/ng_line_box_fragment_builder.h"

#include "core/layout/ng/geometry/ng_logical_size.h"
#include "core/layout/ng/inline/ng_inline_break_token.h"
#include "core/layout/ng/inline/ng_inline_node.h"
#include "core/layout/ng/inline/ng_physical_line_box_fragment.h"
#include "core/layout/ng/ng_fragment.h"
#include "platform/heap/Handle.h"

namespace blink {

NGLineBoxFragmentBuilder::NGLineBoxFragmentBuilder(NGInlineNode* node)
    : direction_(TextDirection::kLtr), node_(node) {}

NGLineBoxFragmentBuilder& NGLineBoxFragmentBuilder::SetDirection(
    TextDirection direction) {
  direction_ = direction;
  return *this;
}

NGLineBoxFragmentBuilder& NGLineBoxFragmentBuilder::SetInlineSize(
    LayoutUnit size) {
  inline_size_ = size;
  return *this;
}

NGLineBoxFragmentBuilder& NGLineBoxFragmentBuilder::AddChild(
    RefPtr<NGPhysicalFragment> child,
    const NGLogicalOffset& child_offset) {
  children_.push_back(std::move(child));
  offsets_.push_back(child_offset);

  return *this;
}

void NGLineBoxFragmentBuilder::MoveChildrenInBlockDirection(LayoutUnit delta) {
  for (auto& offset : offsets_)
    offset.block_offset += delta;
}

void NGLineBoxFragmentBuilder::MoveChildrenInInlineDirection(LayoutUnit delta) {
  NGWritingMode writing_mode(
      FromPlatformWritingMode(node_->Style().GetWritingMode()));
  LayoutUnit child_inline_size;
  for (size_t i = 0; i < children_.size(); ++i) {
    offsets_[i].inline_offset = delta + child_inline_size;
    NGPhysicalFragment* child = children_[i].Get();
    child_inline_size +=
        child->Size().ConvertToLogical(writing_mode).inline_size;
  }
}

void NGLineBoxFragmentBuilder::MoveChildrenInBlockDirection(LayoutUnit delta,
                                                            unsigned start,
                                                            unsigned end) {
  for (unsigned index = start; index < end; index++)
    offsets_[index].block_offset += delta;
}

void NGLineBoxFragmentBuilder::SetMetrics(const NGLineHeightMetrics& metrics) {
  metrics_ = metrics;
}

void NGLineBoxFragmentBuilder::SetBreakToken(
    RefPtr<NGInlineBreakToken> break_token) {
  break_token_ = std::move(break_token);
}

RefPtr<NGPhysicalLineBoxFragment>
NGLineBoxFragmentBuilder::ToLineBoxFragment() {
  DCHECK_EQ(offsets_.size(), children_.size());

  NGWritingMode writing_mode(
      FromPlatformWritingMode(node_->Style().GetWritingMode()));
  NGPhysicalSize physical_size =
      NGLogicalSize(inline_size_, Metrics().LineHeight())
          .ConvertToPhysical(writing_mode);

  for (size_t i = 0; i < children_.size(); ++i) {
    NGPhysicalFragment* child = children_[i].Get();
    child->SetOffset(offsets_[i].ConvertToPhysical(
        writing_mode, direction_, physical_size, child->Size()));
  }

  return AdoptRef(new NGPhysicalLineBoxFragment(
      physical_size, children_, metrics_,
      break_token_ ? std::move(break_token_)
                   : NGInlineBreakToken::Create(node_)));
}

}  // namespace blink
