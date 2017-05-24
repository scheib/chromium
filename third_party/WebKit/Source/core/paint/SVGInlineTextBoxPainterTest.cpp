// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/paint/SVGInlineTextBoxPainter.h"

#include "core/dom/Document.h"
#include "core/dom/Range.h"
#include "core/editing/DOMSelection.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/layout/LayoutView.h"
#include "core/layout/line/InlineTextBox.h"
#include "core/layout/svg/LayoutSVGInlineText.h"
#include "core/layout/svg/LayoutSVGText.h"
#include "core/paint/PaintControllerPaintTest.h"
#include "core/paint/PaintLayer.h"
#include "platform/geometry/IntRectOutsets.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/paint/DisplayItemList.h"
#include "platform/graphics/paint/DrawingDisplayItem.h"
#include "platform/graphics/paint/PaintController.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {
namespace {

class SVGInlineTextBoxPainterTest : public PaintControllerPaintTest {
 public:
  const DrawingDisplayItem* GetDrawingForSVGTextById(const char* element_name) {
    // Look up the inline text box that serves as the display item client for
    // the painted text.
    LayoutSVGText* target_svg_text =
        ToLayoutSVGText(GetDocument()
                            .getElementById(AtomicString(element_name))
                            ->GetLayoutObject());
    LayoutSVGInlineText* target_inline_text =
        target_svg_text->DescendantTextNodes()[0];
    const DisplayItemClient* target_client =
        static_cast<const DisplayItemClient*>(
            target_inline_text->FirstTextBox());

    // Find the appropriate drawing in the display item list.
    const DisplayItemList& display_item_list =
        RootPaintController().GetDisplayItemList();
    for (size_t i = 0; i < display_item_list.size(); i++) {
      if (display_item_list[i].Client() == *target_client)
        return static_cast<const DrawingDisplayItem*>(&display_item_list[i]);
    }

    return nullptr;
  }

  void SelectAllText() {
    Range* range = GetDocument().createRange();
    range->selectNode(GetDocument().documentElement());
    LocalDOMWindow* window = GetDocument().domWindow();
    DOMSelection* selection = window->getSelection();
    selection->removeAllRanges();
    selection->addRange(range);
  }

 private:
  void SetUp() override {
    PaintControllerPaintTest::SetUp();
    EnableCompositing();
  }
};

static void AssertTextDrawingEquals(
    const DrawingDisplayItem* drawing_display_item,
    const char* str) {
  ASSERT_EQ(str,
            static_cast<const InlineTextBox*>(&drawing_display_item->Client())
                ->GetText());
}

bool ApproximatelyEqual(int a, int b, int delta) {
  return abs(a - b) <= delta;
}

const static int kPixelDelta = 4;

#define EXPECT_RECT_EQ(expected, actual)                                    \
  do {                                                                      \
    const FloatRect& actual_rect = actual;                                  \
    EXPECT_TRUE(                                                            \
        ApproximatelyEqual(expected.X(), actual_rect.X(), kPixelDelta))     \
        << "actual: " << actual_rect.X() << ", expected: " << expected.X(); \
    EXPECT_TRUE(                                                            \
        ApproximatelyEqual(expected.Y(), actual_rect.Y(), kPixelDelta))     \
        << "actual: " << actual_rect.Y() << ", expected: " << expected.Y(); \
    EXPECT_TRUE(ApproximatelyEqual(expected.Width(), actual_rect.Width(),   \
                                   kPixelDelta))                            \
        << "actual: " << actual_rect.Width()                                \
        << ", expected: " << expected.Width();                              \
    EXPECT_TRUE(ApproximatelyEqual(expected.Height(), actual_rect.Height(), \
                                   kPixelDelta))                            \
        << "actual: " << actual_rect.Height()                               \
        << ", expected: " << expected.Height();                             \
  } while (false)

static IntRect CullRectFromDrawing(
    const DrawingDisplayItem& drawing_display_item) {
  return IntRect(drawing_display_item.GetPaintRecord()->cullRect());
}

TEST_F(SVGInlineTextBoxPainterTest, TextCullRect_DefaultWritingMode) {
  SetBodyInnerHTML(
      "<svg width='400px' height='400px' font-family='Arial' font-size='30'>"
      "<text id='target' x='50' y='30'>x</text>"
      "</svg>");
  GetDocument().View()->UpdateAllLifecyclePhases();

  const DrawingDisplayItem* drawing_display_item =
      GetDrawingForSVGTextById("target");
  AssertTextDrawingEquals(drawing_display_item, "x");
  EXPECT_RECT_EQ(IntRect(50, 3, 15, 33),
                 CullRectFromDrawing(*drawing_display_item));

  SelectAllText();
  GetDocument().View()->UpdateAllLifecyclePhases();

  drawing_display_item = GetDrawingForSVGTextById("target");
  AssertTextDrawingEquals(drawing_display_item, "x");
  EXPECT_RECT_EQ(IntRect(50, 3, 15, 33),
                 CullRectFromDrawing(*drawing_display_item));
}

TEST_F(SVGInlineTextBoxPainterTest, TextCullRect_WritingModeTopToBottom) {
  SetBodyInnerHTML(
      "<svg width='400px' height='400px' font-family='Arial' font-size='30'>"
      "<text id='target' x='50' y='30' writing-mode='tb'>x</text>"
      "</svg>");
  GetDocument().View()->UpdateAllLifecyclePhases();

  const DrawingDisplayItem* drawing_display_item =
      GetDrawingForSVGTextById("target");
  AssertTextDrawingEquals(drawing_display_item, "x");
  EXPECT_RECT_EQ(IntRect(33, 30, 34, 15),
                 CullRectFromDrawing(*drawing_display_item));

  SelectAllText();
  GetDocument().View()->UpdateAllLifecyclePhases();

  // The selection rect is one pixel taller due to sub-pixel difference
  // between the text bounds and selection bounds in combination with use of
  // enclosingIntRect() in SVGInlineTextBox::localSelectionRect().
  drawing_display_item = GetDrawingForSVGTextById("target");
  AssertTextDrawingEquals(drawing_display_item, "x");
  EXPECT_RECT_EQ(IntRect(33, 30, 34, 16),
                 CullRectFromDrawing(*drawing_display_item));
}

TEST_F(SVGInlineTextBoxPainterTest, TextCullRect_TextShadow) {
  SetBodyInnerHTML(
      "<svg width='400px' height='400px' font-family='Arial' font-size='30'>"
      "<text id='target' x='50' y='30' style='text-shadow: 200px 200px 5px "
      "red'>x</text>"
      "</svg>");
  GetDocument().View()->UpdateAllLifecyclePhases();

  const DrawingDisplayItem* drawing_display_item =
      GetDrawingForSVGTextById("target");
  AssertTextDrawingEquals(drawing_display_item, "x");
  EXPECT_RECT_EQ(IntRect(50, 3, 220, 238),
                 CullRectFromDrawing(*drawing_display_item));
}

}  // namespace
}  // namespace blink
