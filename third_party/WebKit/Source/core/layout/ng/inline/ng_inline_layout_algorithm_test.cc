// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_base_layout_algorithm_test.h"

#include "core/dom/TagCollection.h"
#include "core/layout/line/InlineTextBox.h"
#include "core/layout/ng/inline/ng_inline_node.h"
#include "core/layout/ng/inline/ng_physical_line_box_fragment.h"
#include "core/layout/ng/inline/ng_physical_text_fragment.h"
#include "core/layout/ng/layout_ng_block_flow.h"
#include "core/layout/ng/ng_block_break_token.h"
#include "core/layout/ng/ng_constraint_space_builder.h"
#include "core/layout/ng/ng_layout_result.h"
#include "platform/geometry/LayoutPoint.h"
#include "platform/geometry/LayoutRect.h"

namespace blink {
namespace {

class NGInlineLayoutAlgorithmTest : public NGBaseLayoutAlgorithmTest {};

TEST_F(NGInlineLayoutAlgorithmTest, BreakToken) {
  LoadAhem();
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
      html {
        font: 10px/1 Ahem;
      }
      #container {
        width: 50px; height: 20px;
      }
    </style>
    <div id=container>123 456 789</div>
  )HTML");

  // Perform 1st Layout.
  LayoutNGBlockFlow* block_flow =
      ToLayoutNGBlockFlow(GetLayoutObjectByElementId("container"));
  NGInlineNode* inline_node =
      new NGInlineNode(block_flow->FirstChild(), block_flow);
  RefPtr<NGConstraintSpace> constraint_space =
      NGConstraintSpaceBuilder(NGWritingMode::kHorizontalTopBottom)
          .SetAvailableSize({LayoutUnit(50), LayoutUnit(20)})
          .ToConstraintSpace(NGWritingMode::kHorizontalTopBottom);
  RefPtr<NGLayoutResult> layout_result =
      inline_node->Layout(constraint_space.Get(), nullptr);
  auto* wrapper =
      ToNGPhysicalBoxFragment(layout_result->PhysicalFragment().Get());

  // Test that the anonymous wrapper has 2 line boxes, and both have unfinished
  // break tokens.
  EXPECT_EQ(2u, wrapper->Children().size());
  auto* line1 = ToNGPhysicalLineBoxFragment(wrapper->Children()[0].Get());
  EXPECT_FALSE(line1->BreakToken()->IsFinished());
  auto* line2 = ToNGPhysicalLineBoxFragment(wrapper->Children()[1].Get());
  EXPECT_FALSE(line2->BreakToken()->IsFinished());

  // Test that the wrapper has the break token from the last line as its child.
  auto* wrapper_break_token = ToNGBlockBreakToken(wrapper->BreakToken());
  EXPECT_EQ(wrapper_break_token->ChildBreakTokens()[0], line2->BreakToken());
  EXPECT_FALSE(wrapper_break_token->IsFinished());

  // Perform 2nd layout with the break token from the 2nd line.
  RefPtr<NGLayoutResult> layout_result2 =
      inline_node->Layout(constraint_space.Get(), line2->BreakToken());
  auto* wrapper2 =
      ToNGPhysicalBoxFragment(layout_result2->PhysicalFragment().Get());

  // Test that the anonymous wrapper has 1 line boxes, and has a finished break
  // token.
  EXPECT_EQ(1u, wrapper2->Children().size());
  auto* line3 = ToNGPhysicalLineBoxFragment(wrapper2->Children()[0].Get());
  EXPECT_TRUE(line3->BreakToken()->IsFinished());

  // Test that the wrapper has the break token without children.
  auto* wrapper2_break_token = ToNGBlockBreakToken(wrapper2->BreakToken());
  EXPECT_EQ(0u, wrapper2_break_token->ChildBreakTokens().size());
}

// The test leaks memory. crbug.com/721932
#if defined(ADDRESS_SANITIZER)
#define MAYBE_VerticalAlignBottomReplaced DISABLED_VerticalAlignBottomReplaced
#else
#define MAYBE_VerticalAlignBottomReplaced VerticalAlignBottomReplaced
#endif
TEST_F(NGInlineLayoutAlgorithmTest, MAYBE_VerticalAlignBottomReplaced) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
    html { font-size: 10px; }
    img { vertical-align: bottom; }
    </style>
    <div id=container><img src="#" width="96" height="96"></div>
  )HTML");
  LayoutNGBlockFlow* block_flow =
      ToLayoutNGBlockFlow(GetLayoutObjectByElementId("container"));
  NGInlineNode* inline_node =
      new NGInlineNode(block_flow->FirstChild(), block_flow);
  RefPtr<NGConstraintSpace> space =
      NGConstraintSpace::CreateFromLayoutObject(*block_flow);
  RefPtr<NGLayoutResult> layout_result = inline_node->Layout(space.Get());
  auto* wrapper =
      ToNGPhysicalBoxFragment(layout_result->PhysicalFragment().Get());
  EXPECT_EQ(1u, wrapper->Children().size());
  auto* line = ToNGPhysicalLineBoxFragment(wrapper->Children()[0].Get());
  EXPECT_EQ(LayoutUnit(96), line->Size().height);
  auto* img = line->Children()[0].Get();
  EXPECT_EQ(LayoutUnit(0), img->Offset().top);
}

// Verifies that text can flow correctly around floats that were positioned
// before the inline block.
TEST_F(NGInlineLayoutAlgorithmTest, TextFloatsAroundFloatsBefore) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
      * {
        font-family: "Arial", sans-serif;
        font-size: 20px;
      }
      #container {
        height: 200px; width: 200px; outline: solid blue;
      }
      #left-float1 {
        float: left; width: 30px; height: 30px; background-color: blue;
      }
      #left-float2 {
        float: left; width: 10px; height: 10px;
        background-color: purple;
      }
      #right-float {
        float: right; width: 40px; height: 40px; background-color: yellow;
      }
    </style>
    <div id="container">
      <div id="left-float1"></div>
      <div id="left-float2"></div>
      <div id="right-float"></div>
      <span id="text">The quick brown fox jumps over the lazy dog</span>
    </div>
  )HTML");
  // ** Run LayoutNG algorithm **
  RefPtr<NGConstraintSpace> space;
  RefPtr<NGPhysicalBoxFragment> html_fragment;
  std::tie(html_fragment, space) = RunBlockLayoutAlgorithmForElement(
      GetDocument().getElementsByTagName("html")->item(0));
  auto* body_fragment =
      ToNGPhysicalBoxFragment(html_fragment->Children()[0].Get());
  auto* container_fragment =
      ToNGPhysicalBoxFragment(body_fragment->Children()[0].Get());
  auto* span_box_fragments_wrapper =
      ToNGPhysicalBoxFragment(container_fragment->Children()[0].Get());
  auto* line_box_fragments_wrapper =
      ToNGPhysicalBoxFragment(span_box_fragments_wrapper->Children()[0].Get());
  Vector<NGPhysicalTextFragment*> text_fragments;
  for (const auto& child : line_box_fragments_wrapper->Children()) {
    auto* line_box = ToNGPhysicalLineBoxFragment(child.Get());
    EXPECT_EQ(1u, line_box->Children().size());
    for (const auto& text : line_box->Children())
      text_fragments.push_back(ToNGPhysicalTextFragment(text.Get()));
  }

  LayoutText* layout_text =
      ToLayoutText(GetLayoutObjectByElementId("text")->SlowFirstChild());
  DCHECK(layout_text->HasTextBoxes());

  // Line break points may vary by minor differences in fonts.
  // The test is valid as long as we have 3 or more lines and their positions
  // are correct.
  EXPECT_GE(text_fragments.size(), 3UL);

  auto* text_fragment1 = text_fragments[0];
  // 40 = #left-float1' width 30 + #left-float2 10
  EXPECT_EQ(LayoutUnit(40), text_fragment1->Offset().left);
  InlineTextBox* inline_text_box1 = layout_text->FirstTextBox();
  EXPECT_EQ(LayoutUnit(40), inline_text_box1->X());

  auto* text_fragment2 = text_fragments[1];
  // 40 = #left-float1' width 30
  EXPECT_EQ(LayoutUnit(30), text_fragment2->Offset().left);
  InlineTextBox* inline_text_box2 = inline_text_box1->NextTextBox();
  EXPECT_EQ(LayoutUnit(30), inline_text_box2->X());

  auto* text_fragment3 = text_fragments[2];
  EXPECT_EQ(LayoutUnit(), text_fragment3->Offset().left);
  InlineTextBox* inline_text_box3 = inline_text_box2->NextTextBox();
  EXPECT_EQ(LayoutUnit(), inline_text_box3->X());
}

// Verifies that text correctly flows around the inline float that fits on
// the same text line.
TEST_F(NGInlineLayoutAlgorithmTest, TextFloatsAroundInlineFloatThatFitsOnLine) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
      * {
        font-family: "Arial", sans-serif;
        font-size: 19px;
      }
      #container {
        height: 200px; width: 200px; outline: solid orange;
      }
      #narrow-float {
        float: left; width: 30px; height: 30px; background-color: blue;
      }
    </style>
    <div id="container">
      <span id="text">
        The quick <div id="narrow-float"></div> brown fox jumps over the lazy
      </span>
    </div>
  )HTML");
  LayoutText* layout_text =
      ToLayoutText(GetLayoutObjectByElementId("text")->SlowFirstChild());
  DCHECK(layout_text->HasTextBoxes());

  InlineTextBox* inline_text_box1 = layout_text->FirstTextBox();
  // 30 == narrow-float's width.
  EXPECT_EQ(LayoutUnit(30), inline_text_box1->X());

  Element* narrow_float = GetDocument().getElementById("narrow-float");
  // 8 == body's margin.
  EXPECT_EQ(8, narrow_float->OffsetLeft());
  EXPECT_EQ(8, narrow_float->OffsetTop());
}

// Verifies that the inline float got pushed to the next line if it doesn't
// fit the current line.
TEST_F(NGInlineLayoutAlgorithmTest,
       TextFloatsAroundInlineFloatThatDoesNotFitOnLine) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
      * {
        font-family: "Arial", sans-serif;
        font-size: 19px;
      }
      #container {
        height: 200px; width: 200px; outline: solid orange;
      }
      #wide-float {
        float: left; width: 160px; height: 30px; background-color: red;
      }
    </style>
    <div id="container">
      <span id="text">
        The quick <div id="wide-float"></div> brown fox jumps over the lazy dog
      </span>
    </div>
  )HTML");
  LayoutText* layout_text =
      ToLayoutText(GetLayoutObjectByElementId("text")->SlowFirstChild());
  DCHECK(layout_text->HasTextBoxes());

  InlineTextBox* inline_text_box1 = layout_text->FirstTextBox();
  EXPECT_EQ(LayoutUnit(), inline_text_box1->X());

  Element* wide_float = GetDocument().getElementById("wide-float");
  // 8 == body's margin.
  EXPECT_EQ(8, wide_float->OffsetLeft());
}

// Verifies that if an inline float pushed to the next line then all others
// following inline floats positioned with respect to the float's top edge
// alignment rule.
TEST_F(NGInlineLayoutAlgorithmTest,
       FloatsArePositionedWithRespectToTopEdgeAlignmentRule) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
      * {
        font-family: "Arial", sans-serif;
        font-size: 19px;
      }
      #container {
        height: 200px; width: 200px; outline: solid orange;
      }
      #left-narrow {
        float: left; width: 5px; height: 30px; background-color: blue;
      }
      #left-wide {
        float: left; width: 160px; height: 30px; background-color: red;
      }
    </style>
    <div id="container">
      <span id="text">
        The quick <div id="left-wide"></div> brown <div id="left-narrow"></div>
        fox jumps over the lazy dog
      </span>
    </div>
  )HTML");
  Element* wide_float = GetDocument().getElementById("left-wide");
  // 8 == body's margin.
  EXPECT_EQ(8, wide_float->OffsetLeft());

  Element* narrow_float = GetDocument().getElementById("left-narrow");
  // 160 float-wide's width + 8 body's margin.
  EXPECT_EQ(160 + 8, narrow_float->OffsetLeft());

  // On the same line.
  EXPECT_EQ(wide_float->OffsetTop(), narrow_float->OffsetTop());
}

// Verifies that InlineLayoutAlgorithm positions floats with respect to their
// margins.
TEST_F(NGInlineLayoutAlgorithmTest, PositionFloatsWithMargins) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
      #container {
        height: 200px; width: 200px; outline: solid orange;
      }
      #left {
        float: left; width: 5px; height: 30px; background-color: blue;
        margin: 10%;
      }
    </style>
    <div id="container">
      <span id="text">
        The quick <div id="left"></div> brown fox jumps over the lazy dog
      </span>
    </div>
  )HTML");
  LayoutText* layout_text =
      ToLayoutText(GetLayoutObjectByElementId("text")->SlowFirstChild());
  DCHECK(layout_text->HasTextBoxes());

  InlineTextBox* inline_text_box1 = layout_text->FirstTextBox();
  // 45 = sum of left's inline margins: 40 + left's width: 5
  EXPECT_EQ(LayoutUnit(45), inline_text_box1->X());
}

}  // namespace
}  // namespace blink
