// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/paint/LayoutObjectDrawingRecorder.h"

#include "core/layout/LayoutTestHelper.h"
#include "core/layout/LayoutView.h"
#include "core/paint/PaintControllerPaintTest.h"
#include "core/paint/PaintLayer.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/paint/DrawingDisplayItem.h"
#include "platform/graphics/paint/PaintController.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

using LayoutObjectDrawingRecorderTest = PaintControllerPaintTest;

namespace {

void DrawNothing(GraphicsContext& context,
                 const LayoutView& layout_view,
                 PaintPhase phase,
                 const LayoutRect& bound) {
  if (LayoutObjectDrawingRecorder::UseCachedDrawingIfPossible(
          context, layout_view, phase))
    return;

  LayoutObjectDrawingRecorder drawing_recorder(context, layout_view, phase,
                                               bound);
}

void DrawRect(GraphicsContext& context,
              LayoutView& layout_view,
              PaintPhase phase,
              const LayoutRect& bound) {
  if (LayoutObjectDrawingRecorder::UseCachedDrawingIfPossible(
          context, layout_view, phase))
    return;
  LayoutObjectDrawingRecorder drawing_recorder(context, layout_view, phase,
                                               bound);
  IntRect rect(0, 0, 10, 10);
  context.DrawRect(rect);
}

TEST_F(LayoutObjectDrawingRecorderTest, Nothing) {
  RootPaintController().InvalidateAll();
  GraphicsContext context(RootPaintController());
  LayoutRect bound = GetLayoutView().ViewRect();
  DrawNothing(context, GetLayoutView(), kPaintPhaseForeground, bound);
  RootPaintController().CommitNewDisplayItems();
  EXPECT_DISPLAY_LIST(
      RootPaintController().GetDisplayItemList(), 1,
      TestDisplayItem(GetLayoutView(), DisplayItem::PaintPhaseToDrawingType(
                                           kPaintPhaseForeground)));
  EXPECT_FALSE(static_cast<const DrawingDisplayItem&>(
                   RootPaintController().GetDisplayItemList()[0])
                   .GetPaintRecord());
}

TEST_F(LayoutObjectDrawingRecorderTest, Rect) {
  RootPaintController().InvalidateAll();
  GraphicsContext context(RootPaintController());
  LayoutRect bound = GetLayoutView().ViewRect();
  DrawRect(context, GetLayoutView(), kPaintPhaseForeground, bound);
  RootPaintController().CommitNewDisplayItems();
  EXPECT_DISPLAY_LIST(
      RootPaintController().GetDisplayItemList(), 1,
      TestDisplayItem(GetLayoutView(), DisplayItem::PaintPhaseToDrawingType(
                                           kPaintPhaseForeground)));
}

TEST_F(LayoutObjectDrawingRecorderTest, Cached) {
  RootPaintController().InvalidateAll();
  GraphicsContext context(RootPaintController());
  LayoutRect bound = GetLayoutView().ViewRect();
  DrawNothing(context, GetLayoutView(), kPaintPhaseSelfBlockBackgroundOnly,
              bound);
  DrawRect(context, GetLayoutView(), kPaintPhaseForeground, bound);
  RootPaintController().CommitNewDisplayItems();

  EXPECT_DISPLAY_LIST(
      RootPaintController().GetDisplayItemList(), 2,
      TestDisplayItem(GetLayoutView(), DisplayItem::PaintPhaseToDrawingType(
                                           kPaintPhaseSelfBlockBackgroundOnly)),
      TestDisplayItem(GetLayoutView(), DisplayItem::PaintPhaseToDrawingType(
                                           kPaintPhaseForeground)));

  DrawNothing(context, GetLayoutView(), kPaintPhaseSelfBlockBackgroundOnly,
              bound);
  DrawRect(context, GetLayoutView(), kPaintPhaseForeground, bound);

  EXPECT_EQ(2, NumCachedNewItems());

  RootPaintController().CommitNewDisplayItems();

  EXPECT_DISPLAY_LIST(
      RootPaintController().GetDisplayItemList(), 2,
      TestDisplayItem(GetLayoutView(), DisplayItem::PaintPhaseToDrawingType(
                                           kPaintPhaseSelfBlockBackgroundOnly)),
      TestDisplayItem(GetLayoutView(), DisplayItem::PaintPhaseToDrawingType(
                                           kPaintPhaseForeground)));
}

template <typename T>
FloatRect DrawAndGetCullRect(PaintController& controller,
                             const LayoutObject& layout_object,
                             const T& bounds) {
  controller.InvalidateAll();
  {
    // Draw some things which will produce a non-null picture.
    GraphicsContext context(controller);
    LayoutObjectDrawingRecorder recorder(
        context, layout_object, DisplayItem::kBoxDecorationBackground, bounds);
    context.DrawRect(EnclosedIntRect(FloatRect(bounds)));
  }
  controller.CommitNewDisplayItems();
  const auto& drawing = static_cast<const DrawingDisplayItem&>(
      controller.GetDisplayItemList()[0]);
  return drawing.GetPaintRecord()->cullRect();
}

TEST_F(LayoutObjectDrawingRecorderTest, CullRectMatchesProvidedClip) {
  // It's safe for the picture's cull rect to be expanded (though doing so
  // excessively may harm performance), but it cannot be contracted.
  // For now, this test expects the two rects to match completely.
  //
  // This rect is chosen so that in the x direction, pixel snapping rounds in
  // the opposite direction to enclosing, and in the y direction, the edges
  // are exactly on a half-pixel boundary. The numbers chosen map nicely to
  // both float and LayoutUnit, to make equality checking reliable.
  //
  // The final cull rect should be the enclosing int rect of this rect.
  FloatRect rect(20.75, -5.5, 5.375, 10);
  EXPECT_EQ(EnclosingIntRect(rect),
            DrawAndGetCullRect(RootPaintController(), GetLayoutView(), rect));
  EXPECT_EQ(EnclosingIntRect(rect),
            DrawAndGetCullRect(RootPaintController(), GetLayoutView(),
                               LayoutRect(rect)));
}

#if 0  // TODO(wangxianzhu): Rewrite this test for slimmingPaintInvalidation.
TEST_F(LayoutObjectDrawingRecorderTest, PaintOffsetCache)
{
    RuntimeEnabledFeatures::setSlimmingPaintOffsetCachingEnabled(true);

    GraphicsContext context(rootPaintController());
    LayoutRect bounds = layoutView().viewRect();
    LayoutPoint paintOffset(1, 2);

    rootPaintController().invalidateAll();
    EXPECT_FALSE(LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(context, layoutView(), PaintPhaseForeground));
    {
        LayoutObjectDrawingRecorder drawingRecorder(context, layoutView(), PaintPhaseForeground, bounds);
        IntRect rect(0, 0, 10, 10);
        context.drawRect(rect);
    }

    rootPaintController().commitNewDisplayItems();
    EXPECT_DISPLAY_LIST(rootPaintController().getDisplayItemList(), 1,
        TestDisplayItem(layoutView(), DisplayItem::paintPhaseToDrawingType(PaintPhaseForeground)));

    // Ensure we cannot use the cache with a new paint offset.
    LayoutPoint newPaintOffset(2, 3);
    EXPECT_FALSE(LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(context, layoutView(), PaintPhaseForeground));

    // Test that a new paint offset is recorded.
    {
        LayoutObjectDrawingRecorder drawingRecorder(context, layoutView(), PaintPhaseForeground, bounds);
        IntRect rect(0, 0, 10, 10);
        context.drawRect(rect);
    }

    rootPaintController().commitNewDisplayItems();
    EXPECT_DISPLAY_LIST(rootPaintController().getDisplayItemList(), 1,
        TestDisplayItem(layoutView(), DisplayItem::paintPhaseToDrawingType(PaintPhaseForeground)));

    // Ensure the old paint offset cannot be used.
    EXPECT_FALSE(LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(context, layoutView(), PaintPhaseForeground));

    // Ensure the new paint offset can be used.
    EXPECT_TRUE(LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(context, layoutView(), PaintPhaseForeground));
    rootPaintController().commitNewDisplayItems();
    EXPECT_DISPLAY_LIST(rootPaintController().getDisplayItemList(), 1,
        TestDisplayItem(layoutView(), DisplayItem::paintPhaseToDrawingType(PaintPhaseForeground)));
}
#endif

}  // namespace
}  // namespace blink
