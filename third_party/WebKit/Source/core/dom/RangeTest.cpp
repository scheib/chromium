// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/dom/Range.h"

#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/V8BindingForTesting.h"
#include "core/dom/Element.h"
#include "core/dom/NodeList.h"
#include "core/dom/Text.h"
#include "core/editing/EditingTestBase.h"
#include "core/editing/VisibleUnits.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLBodyElement.h"
#include "core/html/HTMLDivElement.h"
#include "core/html/HTMLDocument.h"
#include "core/html/HTMLElement.h"
#include "core/html/HTMLHtmlElement.h"
#include "core/html/HTMLTextAreaElement.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Compiler.h"
#include "platform/wtf/RefPtr.h"
#include "platform/wtf/text/AtomicString.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

class RangeTest : public EditingTestBase {};

TEST_F(RangeTest, createAdjustedToTreeScopeWithPositionInShadowTree) {
  GetDocument().body()->setInnerHTML("<div><select><option>012</option></div>");
  Element* const select_element = GetDocument().QuerySelector("select");
  const Position& position =
      Position::AfterNode(select_element->UserAgentShadowRoot());
  Range* const range =
      Range::CreateAdjustedToTreeScope(GetDocument(), position);
  EXPECT_EQ(range->startContainer(), select_element->parentNode());
  EXPECT_EQ(static_cast<unsigned>(range->startOffset()),
            select_element->NodeIndex());
  EXPECT_TRUE(range->collapsed());
}

TEST_F(RangeTest, extractContentsWithDOMMutationEvent) {
  GetDocument().body()->setInnerHTML("<span><b>abc</b>def</span>");
  GetDocument().GetSettings()->SetScriptEnabled(true);
  Element* const script_element = GetDocument().createElement("script");
  script_element->setTextContent(
      "let count = 0;"
      "const span = document.querySelector('span');"
      "span.addEventListener('DOMSubtreeModified', () => {"
      "  if (++count > 1) return;"
      "  span.firstChild.textContent = 'ABC';"
      "  span.lastChild.textContent = 'DEF';"
      "});");
  GetDocument().body()->AppendChild(script_element);

  Element* const span_element = GetDocument().QuerySelector("span");
  Range* const range =
      Range::Create(GetDocument(), span_element, 0, span_element, 1);
  Element* const result = GetDocument().createElement("div");
  result->AppendChild(range->extractContents(ASSERT_NO_EXCEPTION));

  EXPECT_EQ("<b>abc</b>", result->innerHTML())
      << "DOM mutation event handler should not affect result.";
  EXPECT_EQ("<span>DEF</span>", span_element->outerHTML())
      << "DOM mutation event handler should be executed.";
}

TEST_F(RangeTest, SplitTextNodeRangeWithinText) {
  V8TestingScope scope;

  GetDocument().body()->setInnerHTML("1234");
  Text* old_text = ToText(GetDocument().body()->firstChild());

  Range* range04 = Range::Create(GetDocument(), old_text, 0, old_text, 4);
  Range* range02 = Range::Create(GetDocument(), old_text, 0, old_text, 2);
  Range* range22 = Range::Create(GetDocument(), old_text, 2, old_text, 2);
  Range* range24 = Range::Create(GetDocument(), old_text, 2, old_text, 4);

  old_text->splitText(2, ASSERT_NO_EXCEPTION);
  Text* new_text = ToText(old_text->nextSibling());

  EXPECT_TRUE(range04->BoundaryPointsValid());
  EXPECT_EQ(old_text, range04->startContainer());
  EXPECT_EQ(0u, range04->startOffset());
  EXPECT_EQ(new_text, range04->endContainer());
  EXPECT_EQ(2u, range04->endOffset());

  EXPECT_TRUE(range02->BoundaryPointsValid());
  EXPECT_EQ(old_text, range02->startContainer());
  EXPECT_EQ(0u, range02->startOffset());
  EXPECT_EQ(old_text, range02->endContainer());
  EXPECT_EQ(2u, range02->endOffset());

  // Our implementation always moves the boundary point at the separation point
  // to the end of the original text node.
  EXPECT_TRUE(range22->BoundaryPointsValid());
  EXPECT_EQ(old_text, range22->startContainer());
  EXPECT_EQ(2u, range22->startOffset());
  EXPECT_EQ(old_text, range22->endContainer());
  EXPECT_EQ(2u, range22->endOffset());

  EXPECT_TRUE(range24->BoundaryPointsValid());
  EXPECT_EQ(old_text, range24->startContainer());
  EXPECT_EQ(2u, range24->startOffset());
  EXPECT_EQ(new_text, range24->endContainer());
  EXPECT_EQ(2u, range24->endOffset());
}

TEST_F(RangeTest, SplitTextNodeRangeOutsideText) {
  V8TestingScope scope;

  GetDocument().body()->setInnerHTML(
      "<span id=\"outer\">0<span id=\"inner-left\">1</span>SPLITME<span "
      "id=\"inner-right\">2</span>3</span>");

  Element* outer =
      GetDocument().getElementById(AtomicString::FromUTF8("outer"));
  Element* inner_left =
      GetDocument().getElementById(AtomicString::FromUTF8("inner-left"));
  Element* inner_right =
      GetDocument().getElementById(AtomicString::FromUTF8("inner-right"));
  Text* old_text = ToText(outer->childNodes()->item(2));

  Range* range_outer_outside = Range::Create(GetDocument(), outer, 0, outer, 5);
  Range* range_outer_inside = Range::Create(GetDocument(), outer, 1, outer, 4);
  Range* range_outer_surrounding_text =
      Range::Create(GetDocument(), outer, 2, outer, 3);
  Range* range_inner_left =
      Range::Create(GetDocument(), inner_left, 0, inner_left, 1);
  Range* range_inner_right =
      Range::Create(GetDocument(), inner_right, 0, inner_right, 1);
  Range* range_from_text_to_middle_of_element =
      Range::Create(GetDocument(), old_text, 6, outer, 3);

  old_text->splitText(3, ASSERT_NO_EXCEPTION);
  Text* new_text = ToText(old_text->nextSibling());

  EXPECT_TRUE(range_outer_outside->BoundaryPointsValid());
  EXPECT_EQ(outer, range_outer_outside->startContainer());
  EXPECT_EQ(0u, range_outer_outside->startOffset());
  EXPECT_EQ(outer, range_outer_outside->endContainer());
  EXPECT_EQ(6u,
            range_outer_outside
                ->endOffset());  // Increased by 1 since a new node is inserted.

  EXPECT_TRUE(range_outer_inside->BoundaryPointsValid());
  EXPECT_EQ(outer, range_outer_inside->startContainer());
  EXPECT_EQ(1u, range_outer_inside->startOffset());
  EXPECT_EQ(outer, range_outer_inside->endContainer());
  EXPECT_EQ(5u, range_outer_inside->endOffset());

  EXPECT_TRUE(range_outer_surrounding_text->BoundaryPointsValid());
  EXPECT_EQ(outer, range_outer_surrounding_text->startContainer());
  EXPECT_EQ(2u, range_outer_surrounding_text->startOffset());
  EXPECT_EQ(outer, range_outer_surrounding_text->endContainer());
  EXPECT_EQ(4u, range_outer_surrounding_text->endOffset());

  EXPECT_TRUE(range_inner_left->BoundaryPointsValid());
  EXPECT_EQ(inner_left, range_inner_left->startContainer());
  EXPECT_EQ(0u, range_inner_left->startOffset());
  EXPECT_EQ(inner_left, range_inner_left->endContainer());
  EXPECT_EQ(1u, range_inner_left->endOffset());

  EXPECT_TRUE(range_inner_right->BoundaryPointsValid());
  EXPECT_EQ(inner_right, range_inner_right->startContainer());
  EXPECT_EQ(0u, range_inner_right->startOffset());
  EXPECT_EQ(inner_right, range_inner_right->endContainer());
  EXPECT_EQ(1u, range_inner_right->endOffset());

  EXPECT_TRUE(range_from_text_to_middle_of_element->BoundaryPointsValid());
  EXPECT_EQ(new_text, range_from_text_to_middle_of_element->startContainer());
  EXPECT_EQ(3u, range_from_text_to_middle_of_element->startOffset());
  EXPECT_EQ(outer, range_from_text_to_middle_of_element->endContainer());
  EXPECT_EQ(4u, range_from_text_to_middle_of_element->endOffset());
}

TEST_F(RangeTest, updateOwnerDocumentIfNeeded) {
  Element* foo = GetDocument().createElement("foo");
  Element* bar = GetDocument().createElement("bar");
  foo->AppendChild(bar);

  Range* range =
      Range::Create(GetDocument(), Position(bar, 0), Position(foo, 1));

  Document* another_document = Document::Create();
  another_document->AppendChild(foo);

  EXPECT_EQ(bar, range->startContainer());
  EXPECT_EQ(0u, range->startOffset());
  EXPECT_EQ(foo, range->endContainer());
  EXPECT_EQ(1u, range->endOffset());
}

// Regression test for crbug.com/639184
TEST_F(RangeTest, NotMarkedValidByIrrelevantTextInsert) {
  GetDocument().body()->setInnerHTML(
      "<div><span id=span1>foo</span>bar<span id=span2>baz</span></div>");

  Element* div = GetDocument().QuerySelector("div");
  Element* span1 = GetDocument().getElementById("span1");
  Element* span2 = GetDocument().getElementById("span2");
  Text* text = ToText(div->childNodes()->item(1));

  Range* range = Range::Create(GetDocument(), span2, 0, div, 3);

  div->RemoveChild(span1);
  text->insertData(0, "bar", ASSERT_NO_EXCEPTION);

  EXPECT_TRUE(range->BoundaryPointsValid());
  EXPECT_EQ(span2, range->startContainer());
  EXPECT_EQ(0u, range->startOffset());
  EXPECT_EQ(div, range->endContainer());
  EXPECT_EQ(2u, range->endOffset());
}

// Regression test for crbug.com/639184
TEST_F(RangeTest, NotMarkedValidByIrrelevantTextRemove) {
  GetDocument().body()->setInnerHTML(
      "<div><span id=span1>foofoo</span>bar<span id=span2>baz</span></div>");

  Element* div = GetDocument().QuerySelector("div");
  Element* span1 = GetDocument().getElementById("span1");
  Element* span2 = GetDocument().getElementById("span2");
  Text* text = ToText(div->childNodes()->item(1));

  Range* range = Range::Create(GetDocument(), span2, 0, div, 3);

  div->RemoveChild(span1);
  text->deleteData(0, 3, ASSERT_NO_EXCEPTION);

  EXPECT_TRUE(range->BoundaryPointsValid());
  EXPECT_EQ(span2, range->startContainer());
  EXPECT_EQ(0u, range->startOffset());
  EXPECT_EQ(div, range->endContainer());
  EXPECT_EQ(2u, range->endOffset());
}

// Regression test for crbug.com/698123
TEST_F(RangeTest, ExpandNotCrash) {
  Range* range = Range::Create(GetDocument());
  Node* div = HTMLDivElement::Create(GetDocument());
  range->setStart(div, 0, ASSERT_NO_EXCEPTION);
  range->expand("", ASSERT_NO_EXCEPTION);
}

TEST_F(RangeTest, MultipleTextQuads) {
  SetBodyContent("<div><p id='one'>one</p><p id='two'>two</p></div>");
  Position start(GetDocument().getElementById("one")->firstChild(), 0);
  Position end(GetDocument().getElementById("two")->firstChild(), 3);
  Range* range = Range::Create(GetDocument(), start, end);
  Vector<FloatQuad> quads;
  quads.AppendVector(ComputeTextQuads(EphemeralRange(range)));
  EXPECT_EQ(2u, quads.size());
}

TEST_F(RangeTest, ToPosition) {
  Node& textarea = *HTMLTextAreaElement::Create(GetDocument());
  Range& range = *Range::Create(GetDocument());
  const Position position = Position(&textarea, 0);
  range.setStart(position, ASSERT_NO_EXCEPTION);
  EXPECT_EQ(position, range.StartPosition());
  EXPECT_EQ(position, range.EndPosition());
}

}  // namespace blink
