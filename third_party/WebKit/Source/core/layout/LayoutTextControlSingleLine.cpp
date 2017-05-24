/**
 * Copyright (C) 2006, 2007, 2010 Apple Inc. All rights reserved.
 *           (C) 2008 Torch Mobile Inc. All rights reserved.
 *               (http://www.torchmobile.com/)
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "core/layout/LayoutTextControlSingleLine.h"

#include "core/CSSValueKeywords.h"
#include "core/InputTypeNames.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "core/editing/FrameSelection.h"
#include "core/frame/LocalFrame.h"
#include "core/html/shadow/ShadowElementNames.h"
#include "core/input/KeyboardEventManager.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutAnalyzer.h"
#include "core/layout/LayoutTheme.h"
#include "core/paint/LayoutObjectDrawingRecorder.h"
#include "core/paint/PaintInfo.h"
#include "core/paint/PaintLayer.h"
#include "core/paint/ThemePainter.h"
#include "platform/fonts/SimpleFontData.h"

namespace blink {

using namespace HTMLNames;

LayoutTextControlSingleLine::LayoutTextControlSingleLine(
    HTMLInputElement* element)
    : LayoutTextControl(element), should_draw_caps_lock_indicator_(false) {}

LayoutTextControlSingleLine::~LayoutTextControlSingleLine() {}

inline Element* LayoutTextControlSingleLine::ContainerElement() const {
  return InputElement()->UserAgentShadowRoot()->getElementById(
      ShadowElementNames::TextFieldContainer());
}

inline Element* LayoutTextControlSingleLine::EditingViewPortElement() const {
  return InputElement()->UserAgentShadowRoot()->getElementById(
      ShadowElementNames::EditingViewPort());
}

inline HTMLElement* LayoutTextControlSingleLine::InnerSpinButtonElement()
    const {
  return ToHTMLElement(InputElement()->UserAgentShadowRoot()->getElementById(
      ShadowElementNames::SpinButton()));
}

void LayoutTextControlSingleLine::Paint(const PaintInfo& paint_info,
                                        const LayoutPoint& paint_offset) const {
  LayoutTextControl::Paint(paint_info, paint_offset);

  if (ShouldPaintSelfBlockBackground(paint_info.phase) &&
      should_draw_caps_lock_indicator_) {
    if (LayoutObjectDrawingRecorder::UseCachedDrawingIfPossible(
            paint_info.context, *this, paint_info.phase))
      return;

    LayoutRect contents_rect = ContentBoxRect();

    // Center in the block progression direction.
    if (IsHorizontalWritingMode())
      contents_rect.SetY((Size().Height() - contents_rect.Height()) / 2);
    else
      contents_rect.SetX((Size().Width() - contents_rect.Width()) / 2);

    // Convert the rect into the coords used for painting the content
    contents_rect.MoveBy(paint_offset + Location());
    IntRect snapped_rect = PixelSnappedIntRect(contents_rect);
    LayoutObjectDrawingRecorder recorder(paint_info.context, *this,
                                         paint_info.phase, snapped_rect);
    LayoutTheme::GetTheme().Painter().PaintCapsLockIndicator(*this, paint_info,
                                                             snapped_rect);
  }
}

void LayoutTextControlSingleLine::UpdateLayout() {
  LayoutAnalyzer::Scope analyzer(*this);

  LayoutBlockFlow::UpdateBlockLayout(true);

  LayoutBox* inner_editor_layout_object = InnerEditorElement()->GetLayoutBox();
  Element* container = ContainerElement();
  LayoutBox* container_layout_object =
      container ? container->GetLayoutBox() : nullptr;
  // Center the child block in the block progression direction (vertical
  // centering for horizontal text fields).
  if (!container && inner_editor_layout_object &&
      inner_editor_layout_object->Size().Height() != ContentLogicalHeight()) {
    LayoutUnit logical_height_diff =
        inner_editor_layout_object->LogicalHeight() - ContentLogicalHeight();
    inner_editor_layout_object->SetLogicalTop(
        inner_editor_layout_object->LogicalTop() -
        (logical_height_diff / 2 + LayoutMod(logical_height_diff, 2)));
  } else if (container && container_layout_object &&
             container_layout_object->Size().Height() !=
                 ContentLogicalHeight()) {
    LayoutUnit logical_height_diff =
        container_layout_object->LogicalHeight() - ContentLogicalHeight();
    container_layout_object->SetLogicalTop(
        container_layout_object->LogicalTop() -
        (logical_height_diff / 2 + LayoutMod(logical_height_diff, 2)));
  }

  HTMLElement* placeholder_element = InputElement()->PlaceholderElement();
  if (LayoutBox* placeholder_box =
          placeholder_element ? placeholder_element->GetLayoutBox() : 0) {
    LayoutSize inner_editor_size;

    if (inner_editor_layout_object)
      inner_editor_size = inner_editor_layout_object->Size();
    placeholder_box->MutableStyleRef().SetWidth(Length(
        inner_editor_size.Width() - placeholder_box->BorderAndPaddingWidth(),
        kFixed));
    bool needed_layout = placeholder_box->NeedsLayout();
    placeholder_box->LayoutIfNeeded();
    LayoutPoint text_offset;
    if (inner_editor_layout_object)
      text_offset = inner_editor_layout_object->Location();
    if (EditingViewPortElement() && EditingViewPortElement()->GetLayoutBox())
      text_offset +=
          ToLayoutSize(EditingViewPortElement()->GetLayoutBox()->Location());
    if (container_layout_object)
      text_offset += ToLayoutSize(container_layout_object->Location());
    if (inner_editor_layout_object) {
      // We use inlineBlockBaseline() for innerEditor because it has no
      // inline boxes when we show the placeholder.
      int inner_editor_baseline =
          inner_editor_layout_object->InlineBlockBaseline(kHorizontalLine);
      // We use firstLineBoxBaseline() for placeholder.
      // TODO(tkent): It's inconsistent with innerEditorBaseline. However
      // placeholderBox->inlineBlockBase() is unexpectedly larger.
      int placeholder_baseline = placeholder_box->FirstLineBoxBaseline();
      text_offset +=
          LayoutSize(0, inner_editor_baseline - placeholder_baseline);
    }
    placeholder_box->SetLocation(text_offset);

    // The placeholder gets layout last, after the parent text control and its
    // other children, so in order to get the correct overflow from the
    // placeholder we need to recompute it now.
    if (needed_layout)
      ComputeOverflow(ClientLogicalBottom());
  }
}

bool LayoutTextControlSingleLine::NodeAtPoint(
    HitTestResult& result,
    const HitTestLocation& location_in_container,
    const LayoutPoint& accumulated_offset,
    HitTestAction hit_test_action) {
  if (!LayoutTextControl::NodeAtPoint(result, location_in_container,
                                      accumulated_offset, hit_test_action))
    return false;

  // Say that we hit the inner text element if
  //  - we hit a node inside the inner text element,
  //  - we hit the <input> element (e.g. we're over the border or padding), or
  //  - we hit regions not in any decoration buttons.
  Element* container = ContainerElement();
  if (result.InnerNode()->IsDescendantOf(InnerEditorElement()) ||
      result.InnerNode() == GetNode() ||
      (container && container == result.InnerNode())) {
    LayoutPoint point_in_parent = location_in_container.Point();
    if (container && EditingViewPortElement()) {
      if (EditingViewPortElement()->GetLayoutBox())
        point_in_parent -=
            ToLayoutSize(EditingViewPortElement()->GetLayoutBox()->Location());
      if (container->GetLayoutBox())
        point_in_parent -= ToLayoutSize(container->GetLayoutBox()->Location());
    }
    HitInnerEditorElement(result, point_in_parent, accumulated_offset);
  }
  return true;
}

void LayoutTextControlSingleLine::StyleDidChange(
    StyleDifference diff,
    const ComputedStyle* old_style) {
  LayoutTextControl::StyleDidChange(diff, old_style);
  if (HTMLElement* placeholder = InputElement()->PlaceholderElement())
    placeholder->SetInlineStyleProperty(
        CSSPropertyTextOverflow,
        TextShouldBeTruncated() ? CSSValueEllipsis : CSSValueClip);
}

void LayoutTextControlSingleLine::CapsLockStateMayHaveChanged() {
  if (!GetNode())
    return;

  // Only draw the caps lock indicator if these things are true:
  // 1) The field is a password field
  // 2) The frame is active
  // 3) The element is focused
  // 4) The caps lock is on
  bool should_draw_caps_lock_indicator = false;

  if (LocalFrame* frame = GetDocument().GetFrame())
    should_draw_caps_lock_indicator =
        InputElement()->type() == InputTypeNames::password &&
        frame->Selection().FrameIsFocusedAndActive() &&
        GetDocument().FocusedElement() == GetNode() &&
        KeyboardEventManager::CurrentCapsLockState();

  if (should_draw_caps_lock_indicator != should_draw_caps_lock_indicator_) {
    should_draw_caps_lock_indicator_ = should_draw_caps_lock_indicator;
    SetShouldDoFullPaintInvalidation();
  }
}

bool LayoutTextControlSingleLine::HasControlClip() const {
  return true;
}

LayoutRect LayoutTextControlSingleLine::ControlClipRect(
    const LayoutPoint& additional_offset) const {
  LayoutRect clip_rect = PaddingBoxRect();
  clip_rect.MoveBy(additional_offset);
  return clip_rect;
}

float LayoutTextControlSingleLine::GetAvgCharWidth(
    const AtomicString& family) const {
  // Match the default system font to the width of MS Shell Dlg, the default
  // font for textareas in Firefox, Safari Win and IE for some encodings (in
  // IE, the default font is encoding specific). 901 is the avgCharWidth value
  // in the OS/2 table for MS Shell Dlg.
  if (LayoutTheme::GetTheme().NeedsHackForTextControlWithFontFamily(family))
    return ScaleEmToUnits(901);

  return LayoutTextControl::GetAvgCharWidth(family);
}

LayoutUnit LayoutTextControlSingleLine::PreferredContentLogicalWidth(
    float char_width) const {
  int factor;
  bool includes_decoration =
      InputElement()->SizeShouldIncludeDecoration(factor);
  if (factor <= 0)
    factor = 20;

  LayoutUnit result = LayoutUnit::FromFloatCeil(char_width * factor);

  float max_char_width = 0.f;
  const Font& font = Style()->GetFont();
  AtomicString family = font.GetFontDescription().Family().Family();
  // Match the default system font to the width of MS Shell Dlg, the default
  // font for textareas in Firefox, Safari Win and IE for some encodings (in
  // IE, the default font is encoding specific). 4027 is the (xMax - xMin)
  // value in the "head" font table for MS Shell Dlg.
  if (LayoutTheme::GetTheme().NeedsHackForTextControlWithFontFamily(family))
    max_char_width = ScaleEmToUnits(4027);
  else if (HasValidAvgCharWidth(font.PrimaryFont(), family))
    max_char_width = roundf(font.PrimaryFont()->MaxCharWidth());

  // For text inputs, IE adds some extra width.
  if (max_char_width > 0.f)
    result += max_char_width - char_width;

  if (includes_decoration) {
    HTMLElement* spin_button = InnerSpinButtonElement();
    if (LayoutBox* spin_layout_object =
            spin_button ? spin_button->GetLayoutBox() : 0) {
      result += spin_layout_object->BorderAndPaddingLogicalWidth();
      // Since the width of spinLayoutObject is not calculated yet,
      // spinLayoutObject->logicalWidth() returns 0.
      // So ensureComputedStyle()->logicalWidth() is used instead.
      result += spin_button->EnsureComputedStyle()->LogicalWidth().Value();
    }
  }

  return result;
}

LayoutUnit LayoutTextControlSingleLine::ComputeControlLogicalHeight(
    LayoutUnit line_height,
    LayoutUnit non_content_height) const {
  return line_height + non_content_height;
}

PassRefPtr<ComputedStyle> LayoutTextControlSingleLine::CreateInnerEditorStyle(
    const ComputedStyle& start_style) const {
  RefPtr<ComputedStyle> text_block_style = ComputedStyle::Create();
  text_block_style->InheritFrom(start_style);
  AdjustInnerEditorStyle(*text_block_style);

  text_block_style->SetWhiteSpace(EWhiteSpace::kPre);
  text_block_style->SetOverflowWrap(EOverflowWrap::kNormal);
  text_block_style->SetTextOverflow(
      TextShouldBeTruncated() ? kTextOverflowEllipsis : kTextOverflowClip);

  int computed_line_height =
      LineHeight(true, kHorizontalLine, kPositionOfInteriorLineBoxes).ToInt();
  // Do not allow line-height to be smaller than our default.
  if (text_block_style->FontSize() >= computed_line_height)
    text_block_style->SetLineHeight(ComputedStyle::InitialLineHeight());

  // We'd like to remove line-height if it's unnecessary because
  // overflow:scroll clips editing text by line-height.
  Length logical_height = start_style.LogicalHeight();
  // Here, we remove line-height if the INPUT fixed height is taller than the
  // line-height.  It's not the precise condition because logicalHeight
  // includes border and padding if box-sizing:border-box, and there are cases
  // in which we don't want to remove line-height with percent or calculated
  // length.
  // TODO(tkent): This should be done during layout.
  if (logical_height.IsPercentOrCalc() ||
      (logical_height.IsFixed() &&
       logical_height.GetFloatValue() > computed_line_height))
    text_block_style->SetLineHeight(ComputedStyle::InitialLineHeight());

  text_block_style->SetDisplay(EDisplay::kBlock);
  text_block_style->SetUnique();

  if (InputElement()->ShouldRevealPassword())
    text_block_style->SetTextSecurity(ETextSecurity::kNone);

  text_block_style->SetOverflowX(EOverflow::kScroll);
  // overflow-y:visible doesn't work because overflow-x:scroll makes a layer.
  text_block_style->SetOverflowY(EOverflow::kScroll);
  RefPtr<ComputedStyle> no_scrollbar_style = ComputedStyle::Create();
  no_scrollbar_style->SetStyleType(kPseudoIdScrollbar);
  no_scrollbar_style->SetDisplay(EDisplay::kNone);
  text_block_style->AddCachedPseudoStyle(no_scrollbar_style);
  text_block_style->SetHasPseudoStyle(kPseudoIdScrollbar);

  return text_block_style.Release();
}

bool LayoutTextControlSingleLine::TextShouldBeTruncated() const {
  return GetDocument().FocusedElement() != GetNode() &&
         StyleRef().GetTextOverflow() == kTextOverflowEllipsis;
}

void LayoutTextControlSingleLine::Autoscroll(const IntPoint& position) {
  LayoutBox* layout_object = InnerEditorElement()->GetLayoutBox();
  if (!layout_object)
    return;

  layout_object->Autoscroll(position);
}

LayoutUnit LayoutTextControlSingleLine::ScrollWidth() const {
  if (LayoutBox* inner =
          InnerEditorElement() ? InnerEditorElement()->GetLayoutBox() : 0) {
    // Adjust scrollWidth to inculde input element horizontal paddings and
    // decoration width
    LayoutUnit adjustment = ClientWidth() - inner->ClientWidth();
    return inner->ScrollWidth() + adjustment;
  }
  return LayoutBlockFlow::ScrollWidth();
}

LayoutUnit LayoutTextControlSingleLine::ScrollHeight() const {
  if (LayoutBox* inner =
          InnerEditorElement() ? InnerEditorElement()->GetLayoutBox() : 0) {
    // Adjust scrollHeight to include input element vertical paddings and
    // decoration height
    LayoutUnit adjustment = ClientHeight() - inner->ClientHeight();
    return inner->ScrollHeight() + adjustment;
  }
  return LayoutBlockFlow::ScrollHeight();
}

LayoutUnit LayoutTextControlSingleLine::ScrollLeft() const {
  if (InnerEditorElement())
    return LayoutUnit(InnerEditorElement()->scrollLeft());
  return LayoutBlockFlow::ScrollLeft();
}

LayoutUnit LayoutTextControlSingleLine::ScrollTop() const {
  if (InnerEditorElement())
    return LayoutUnit(InnerEditorElement()->scrollTop());
  return LayoutBlockFlow::ScrollTop();
}

void LayoutTextControlSingleLine::SetScrollLeft(LayoutUnit new_left) {
  if (InnerEditorElement())
    InnerEditorElement()->setScrollLeft(new_left);
}

void LayoutTextControlSingleLine::SetScrollTop(LayoutUnit new_top) {
  if (InnerEditorElement())
    InnerEditorElement()->setScrollTop(new_top);
}

void LayoutTextControlSingleLine::AddOverflowFromChildren() {
  // If the INPUT content height is smaller than the font height, the
  // inner-editor element overflows the INPUT box intentionally, however it
  // shouldn't affect outside of the INPUT box.  So we ignore child overflow.
}

HTMLInputElement* LayoutTextControlSingleLine::InputElement() const {
  return toHTMLInputElement(GetNode());
}

}  // namespace blink
