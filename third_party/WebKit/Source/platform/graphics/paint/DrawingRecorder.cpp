// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/paint/DrawingRecorder.h"

#include "platform/RuntimeEnabledFeatures.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/paint/PaintController.h"
#include "platform/graphics/paint/PaintRecord.h"

namespace blink {

#if DCHECK_IS_ON()
static bool g_list_modification_check_disabled = false;
DisableListModificationCheck::DisableListModificationCheck()
    : disabler_(&g_list_modification_check_disabled, true) {}
#endif

DrawingRecorder::DrawingRecorder(GraphicsContext& context,
                                 const DisplayItemClient& display_item_client,
                                 DisplayItem::Type display_item_type,
                                 const FloatRect& float_cull_rect)
    : context_(context),
      display_item_client_(display_item_client),
      display_item_type_(display_item_type),
      known_to_be_opaque_(false)
#if DCHECK_IS_ON()
      ,
      initial_display_item_list_size_(
          context_.GetPaintController().NewDisplayItemList().size())
#endif
{
  if (context.GetPaintController().DisplayItemConstructionIsDisabled())
    return;

  // Must check DrawingRecorder::useCachedDrawingIfPossible before creating the
  // DrawingRecorder.
  DCHECK(RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled() ||
         !UseCachedDrawingIfPossible(context_, display_item_client_,
                                     display_item_type_));

  DCHECK(DisplayItem::IsDrawingType(display_item_type));

#if DCHECK_IS_ON()
  context.SetInDrawingRecorder(true);
#endif

  // Use the enclosing int rect, since pixel-snapping may be applied to the
  // bounds of the object during painting. Potentially expanding the cull rect
  // by a pixel or two also does not affect correctness, and is very unlikely to
  // matter for performance.
  IntRect cull_rect = EnclosingIntRect(float_cull_rect);
  context.BeginRecording(cull_rect);

#if DCHECK_IS_ON()
  if (RuntimeEnabledFeatures::slimmingPaintStrictCullRectClippingEnabled()) {
    // Skia depends on the cull rect containing all of the display item
    // commands. When strict cull rect clipping is enabled, make this explicit.
    // This allows us to identify potential incorrect cull rects that might
    // otherwise be masked due to Skia internal optimizations.
    context.Save();
    // Expand the verification clip by one pixel to account for Skia's
    // SkCanvas::getClipBounds() expansion, used in testing cull rects.
    // TODO(schenney) This is not the best place to do this. Ideally, we would
    // expand by one pixel in device (pixel) space, but to do that we would need
    // to add the verification mode to Skia.
    cull_rect.Inflate(1);
    context.ClipRect(cull_rect, kNotAntiAliased, SkClipOp::kIntersect);
  }
#endif
}

DrawingRecorder::~DrawingRecorder() {
  if (context_.GetPaintController().DisplayItemConstructionIsDisabled())
    return;

#if DCHECK_IS_ON()
  if (RuntimeEnabledFeatures::slimmingPaintStrictCullRectClippingEnabled())
    context_.Restore();

  context_.SetInDrawingRecorder(false);

  if (!g_list_modification_check_disabled) {
    DCHECK(initial_display_item_list_size_ ==
           context_.GetPaintController().NewDisplayItemList().size());
  }
#endif

  sk_sp<const PaintRecord> picture = context_.EndRecording();

#if DCHECK_IS_ON()
  if (!RuntimeEnabledFeatures::slimmingPaintStrictCullRectClippingEnabled() &&
      !context_.GetPaintController().IsForPaintRecordBuilder() &&
      display_item_client_.PaintedOutputOfObjectHasNoEffectRegardlessOfSize()) {
    DCHECK_EQ(0u, picture->size()) << display_item_client_.DebugName();
  }
#endif

  context_.GetPaintController().CreateAndAppend<DrawingDisplayItem>(
      display_item_client_, display_item_type_, picture, known_to_be_opaque_);
}

}  // namespace blink
