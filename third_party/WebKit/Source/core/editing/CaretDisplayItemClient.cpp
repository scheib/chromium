/*
 * Copyright (C) 2004, 2008, 2009, 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
* CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/editing/CaretDisplayItemClient.h"

#include "core/editing/EditingUtilities.h"
#include "core/editing/VisibleUnits.h"
#include "core/frame/FrameView.h"
#include "core/frame/Settings.h"
#include "core/layout/LayoutBlock.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutBlockItem.h"
#include "core/layout/api/LayoutItem.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/paint/FindPaintOffsetAndVisualRectNeedingUpdate.h"
#include "core/paint/ObjectPaintInvalidator.h"
#include "core/paint/PaintInfo.h"
#include "core/paint/PaintInvalidator.h"
#include "core/paint/PaintLayer.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/paint/DrawingRecorder.h"

namespace blink {

CaretDisplayItemClient::CaretDisplayItemClient() = default;
CaretDisplayItemClient::~CaretDisplayItemClient() = default;

static inline bool CaretRendersInsideNode(const Node* node) {
  return node && !IsDisplayInsideTable(node) && !EditingIgnoresContent(*node);
}

LayoutBlock* CaretDisplayItemClient::CaretLayoutBlock(const Node* node) {
  if (!node)
    return nullptr;

  LayoutObject* layout_object = node->GetLayoutObject();
  if (!layout_object)
    return nullptr;

  // if caretNode is a block and caret is inside it then caret should be painted
  // by that block
  bool painted_by_block =
      layout_object->IsLayoutBlock() && CaretRendersInsideNode(node);
  // TODO(yoichio): This function is called at least
  // DocumentLifeCycle::LayoutClean but caretRendersInsideNode above can
  // layout. Thus |node->layoutObject()| can be changed then this is bad
  // design. We should make caret painting algorithm clean.
  CHECK_EQ(layout_object, node->GetLayoutObject())
      << "Layout tree should not changed";
  return painted_by_block ? ToLayoutBlock(layout_object)
                          : layout_object->ContainingBlock();
}

static LayoutRect MapCaretRectToCaretPainter(
    LayoutItem caret_layout_item,
    LayoutBlockItem caret_painter_item,
    const LayoutRect& passed_caret_rect) {
  // FIXME: This shouldn't be called on un-rooted subtrees.
  // FIXME: This should probably just use mapLocalToAncestor.
  // Compute an offset between the caretLayoutItem and the caretPainterItem.

  DCHECK(caret_layout_item.IsDescendantOf(caret_painter_item));

  LayoutRect caret_rect = passed_caret_rect;
  while (caret_layout_item != caret_painter_item) {
    LayoutItem container_item = caret_layout_item.Container();
    if (container_item.IsNull())
      return LayoutRect();
    caret_rect.Move(caret_layout_item.OffsetFromContainer(container_item));
    caret_layout_item = container_item;
  }
  return caret_rect;
}

LayoutRect CaretDisplayItemClient::ComputeCaretRect(
    const PositionWithAffinity& caret_position) {
  if (caret_position.IsNull())
    return LayoutRect();

  DCHECK(caret_position.AnchorNode()->GetLayoutObject());

  // First compute a rect local to the layoutObject at the selection start.
  LayoutObject* layout_object;
  const LayoutRect& caret_local_rect =
      LocalCaretRectOfPosition(caret_position, layout_object);

  // Get the layoutObject that will be responsible for painting the caret
  // (which is either the layoutObject we just found, or one of its containers).
  LayoutBlockItem caret_painter_item =
      LayoutBlockItem(CaretLayoutBlock(caret_position.AnchorNode()));
  LayoutRect caret_local_rect_with_writing_mode = caret_local_rect;
  caret_painter_item.FlipForWritingMode(caret_local_rect_with_writing_mode);
  return MapCaretRectToCaretPainter(LayoutItem(layout_object),
                                    caret_painter_item,
                                    caret_local_rect_with_writing_mode);
}

void CaretDisplayItemClient::ClearPreviousVisualRect(const LayoutBlock& block) {
  if (block == layout_block_)
    visual_rect_ = LayoutRect();
  if (block == previous_layout_block_)
    visual_rect_in_previous_layout_block_ = LayoutRect();
}

void CaretDisplayItemClient::LayoutBlockWillBeDestroyed(
    const LayoutBlock& block) {
  if (block == layout_block_)
    layout_block_ = nullptr;
  if (block == previous_layout_block_)
    previous_layout_block_ = nullptr;
}

void CaretDisplayItemClient::UpdateStyleAndLayoutIfNeeded(
    const PositionWithAffinity& caret_position) {
  // This method may be called multiple times (e.g. in partial lifecycle
  // updates) before a paint invalidation. We should save m_previousLayoutBlock
  // and m_visualRectInPreviousLayoutBlock only if they have not been saved
  // since the last paint invalidation to ensure the caret painted in the
  // previous paint invalidated block will be invalidated. We don't care about
  // intermediate changes of layoutBlock because they are not painted.
  if (!previous_layout_block_) {
    previous_layout_block_ = layout_block_;
    visual_rect_in_previous_layout_block_ = visual_rect_;
  }

  LayoutBlock* new_layout_block = CaretLayoutBlock(caret_position.AnchorNode());
  if (new_layout_block != layout_block_) {
    if (layout_block_)
      layout_block_->SetMayNeedPaintInvalidation();
    layout_block_ = new_layout_block;
    visual_rect_ = LayoutRect();
    if (new_layout_block) {
      needs_paint_invalidation_ = true;
      if (new_layout_block == previous_layout_block_) {
        // The caret has disappeared and is reappearing in the same block,
        // since the last paint invalidation. Set m_visualRect as if the caret
        // has always been there as paint invalidation doesn't care about the
        // intermediate changes.
        visual_rect_ = visual_rect_in_previous_layout_block_;
      }
    }
  }

  if (!new_layout_block) {
    color_ = Color();
    local_rect_ = LayoutRect();
    return;
  }

  Color new_color;
  if (caret_position.AnchorNode()) {
    new_color = caret_position.AnchorNode()->GetLayoutObject()->ResolveColor(
        CSSPropertyCaretColor);
  }
  if (new_color != color_) {
    needs_paint_invalidation_ = true;
    color_ = new_color;
  }

  LayoutRect new_local_rect = ComputeCaretRect(caret_position);
  if (new_local_rect != local_rect_) {
    needs_paint_invalidation_ = true;
    local_rect_ = new_local_rect;
  }

  if (needs_paint_invalidation_)
    new_layout_block->SetMayNeedPaintInvalidation();
}

void CaretDisplayItemClient::InvalidatePaint(
    const LayoutBlock& block,
    const PaintInvalidatorContext& context) {
  if (block == layout_block_) {
    InvalidatePaintInCurrentLayoutBlock(context);
    return;
  }

  if (block == previous_layout_block_)
    InvalidatePaintInPreviousLayoutBlock(context);
}

void CaretDisplayItemClient::InvalidatePaintInPreviousLayoutBlock(
    const PaintInvalidatorContext& context) {
  DCHECK(previous_layout_block_);

  ObjectPaintInvalidatorWithContext object_invalidator(*previous_layout_block_,
                                                       context);
  if (!IsImmediateFullPaintInvalidationReason(
          previous_layout_block_->FullPaintInvalidationReason())) {
    object_invalidator.InvalidatePaintRectangleWithContext(
        visual_rect_in_previous_layout_block_, PaintInvalidationReason::kCaret);
  }

  context.painting_layer->SetNeedsRepaint();
  object_invalidator.InvalidateDisplayItemClient(
      *this, PaintInvalidationReason::kCaret);
  previous_layout_block_ = nullptr;
}

void CaretDisplayItemClient::InvalidatePaintInCurrentLayoutBlock(
    const PaintInvalidatorContext& context) {
  DCHECK(layout_block_);

  LayoutRect new_visual_rect;
#if DCHECK_IS_ON()
  FindVisualRectNeedingUpdateScope finder(*layout_block_, context, visual_rect_,
                                          new_visual_rect);
#endif
  if (context.NeedsVisualRectUpdate(*layout_block_)) {
    if (!local_rect_.IsEmpty()) {
      new_visual_rect = local_rect_;
      context.MapLocalRectToVisualRectInBacking(*layout_block_,
                                                new_visual_rect);

      if (layout_block_->UsesCompositedScrolling()) {
        // The caret should use scrolling coordinate space.
        DCHECK(layout_block_ == context.paint_invalidation_container);
        new_visual_rect.Move(
            LayoutSize(layout_block_->ScrolledContentOffset()));
      }
    }
  } else {
    new_visual_rect = visual_rect_;
  }

  if (layout_block_ == previous_layout_block_)
    previous_layout_block_ = nullptr;

  ObjectPaintInvalidatorWithContext object_invalidator(*layout_block_, context);
  if (!needs_paint_invalidation_ && new_visual_rect == visual_rect_) {
    // The caret may change paint offset without changing visual rect, and we
    // need to invalidate the display item client if the block is doing full
    // paint invalidation.
    if (IsImmediateFullPaintInvalidationReason(
            layout_block_->FullPaintInvalidationReason()) ||
        // For non-SPv2, kSubtreeInvalidationChecking may hint change of
        // paint offset. See ObjectPaintInvalidatorWithContext::
        // invalidatePaintIfNeededWithComputedReason().
        (!RuntimeEnabledFeatures::slimmingPaintV2Enabled() &&
         (context.subtree_flags &
          PaintInvalidatorContext::kSubtreeInvalidationChecking))) {
      object_invalidator.InvalidateDisplayItemClient(
          *this, PaintInvalidationReason::kCaret);
    }
    return;
  }

  needs_paint_invalidation_ = false;

  if (!IsImmediateFullPaintInvalidationReason(
          layout_block_->FullPaintInvalidationReason())) {
    object_invalidator.FullyInvalidatePaint(PaintInvalidationReason::kCaret,
                                            visual_rect_, new_visual_rect);
  }

  context.painting_layer->SetNeedsRepaint();
  object_invalidator.InvalidateDisplayItemClient(
      *this, PaintInvalidationReason::kCaret);

  visual_rect_ = new_visual_rect;
}

void CaretDisplayItemClient::PaintCaret(
    GraphicsContext& context,
    const LayoutPoint& paint_offset,
    DisplayItem::Type display_item_type) const {
  if (DrawingRecorder::UseCachedDrawingIfPossible(context, *this,
                                                  display_item_type))
    return;

  LayoutRect drawing_rect = local_rect_;
  drawing_rect.MoveBy(paint_offset);

  IntRect paint_rect = PixelSnappedIntRect(drawing_rect);
  DrawingRecorder drawing_recorder(context, *this, display_item_type,
                                   paint_rect);
  context.FillRect(paint_rect, color_);
}

String CaretDisplayItemClient::DebugName() const {
  return "Caret";
}

LayoutRect CaretDisplayItemClient::VisualRect() const {
  return visual_rect_;
}

}  // namespace blink
