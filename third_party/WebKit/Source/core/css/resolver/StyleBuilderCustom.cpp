/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 2004-2005 Allan Sandfeld Jensen (kde@carewolf.com)
 * Copyright (C) 2006, 2007 Nicholas Shanks (webkit@nickshanks.com)
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013 Apple Inc.
 * All rights reserved.
 * Copyright (C) 2007 Alexey Proskuryakov <ap@webkit.org>
 * Copyright (C) 2007, 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 * Copyright (C) Research In Motion Limited 2011. All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <memory>
#include "core/CSSPropertyNames.h"
#include "core/CSSValueKeywords.h"
#include "core/StyleBuilderFunctions.h"
#include "core/StylePropertyShorthand.h"
#include "core/animation/css/CSSAnimations.h"
#include "core/css/CSSCounterValue.h"
#include "core/css/CSSCursorImageValue.h"
#include "core/css/CSSCustomPropertyDeclaration.h"
#include "core/css/CSSFunctionValue.h"
#include "core/css/CSSGridTemplateAreasValue.h"
#include "core/css/CSSHelper.h"
#include "core/css/CSSImageSetValue.h"
#include "core/css/CSSPendingSubstitutionValue.h"
#include "core/css/CSSPrimitiveValueMappings.h"
#include "core/css/CSSPropertyMetadata.h"
#include "core/css/CSSValueIDMappings.h"
#include "core/css/CSSVariableReferenceValue.h"
#include "core/css/PropertyRegistration.h"
#include "core/css/PropertyRegistry.h"
#include "core/css/StylePropertySet.h"
#include "core/css/StyleRule.h"
#include "core/css/resolver/CSSVariableResolver.h"
#include "core/css/resolver/ElementStyleResources.h"
#include "core/css/resolver/FilterOperationResolver.h"
#include "core/css/resolver/FontBuilder.h"
#include "core/css/resolver/StyleBuilder.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/style/ComputedStyle.h"
#include "core/style/ComputedStyleConstants.h"
#include "core/style/ContentData.h"
#include "core/style/CounterContent.h"
#include "core/style/QuotesData.h"
#include "core/style/SVGComputedStyle.h"
#include "core/style/StyleGeneratedImage.h"
#include "core/style/StyleInheritedVariables.h"
#include "core/style/StyleNonInheritedVariables.h"
#include "platform/fonts/FontDescription.h"
#include "platform/wtf/MathExtras.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/StdLibExtras.h"
#include "platform/wtf/Vector.h"

namespace blink {

namespace {

static inline bool IsValidVisitedLinkProperty(CSSPropertyID id) {
  switch (id) {
    case CSSPropertyBackgroundColor:
    case CSSPropertyBorderLeftColor:
    case CSSPropertyBorderRightColor:
    case CSSPropertyBorderTopColor:
    case CSSPropertyBorderBottomColor:
    case CSSPropertyCaretColor:
    case CSSPropertyColor:
    case CSSPropertyFill:
    case CSSPropertyOutlineColor:
    case CSSPropertyStroke:
    case CSSPropertyTextDecorationColor:
    case CSSPropertyColumnRuleColor:
    case CSSPropertyWebkitTextEmphasisColor:
    case CSSPropertyWebkitTextFillColor:
    case CSSPropertyWebkitTextStrokeColor:
      return true;
    default:
      return false;
  }
}

}  // namespace

void StyleBuilder::ApplyProperty(CSSPropertyID id,
                                 StyleResolverState& state,
                                 const CSSValue& value) {
  if (id != CSSPropertyVariable && (value.IsVariableReferenceValue() ||
                                    value.IsPendingSubstitutionValue())) {
    bool omit_animation_tainted =
        CSSAnimations::IsAnimationAffectingProperty(id);
    const CSSValue* resolved_value =
        CSSVariableResolver::ResolveVariableReferences(state, id, value,
                                                       omit_animation_tainted);
    ApplyProperty(id, state, *resolved_value);

    if (!state.Style()->HasVariableReferenceFromNonInheritedProperty() &&
        !CSSPropertyMetadata::IsInheritedProperty(id))
      state.Style()->SetHasVariableReferenceFromNonInheritedProperty();
    return;
  }

  DCHECK(!isShorthandProperty(id)) << "Shorthand property id = " << id
                                   << " wasn't expanded at parsing time";

  bool is_inherit = state.ParentNode() && value.IsInheritedValue();
  bool is_initial = value.IsInitialValue() ||
                    (!state.ParentNode() && value.IsInheritedValue());

  // isInherit => !isInitial && isInitial => !isInherit
  DCHECK(!is_inherit || !is_initial);
  // isInherit => (state.parentNode() && state.parentStyle())
  DCHECK(!is_inherit || (state.ParentNode() && state.ParentStyle()));

  if (!state.ApplyPropertyToRegularStyle() &&
      (!state.ApplyPropertyToVisitedLinkStyle() ||
       !IsValidVisitedLinkProperty(id))) {
    // Limit the properties that can be applied to only the ones honored by
    // :visited.
    return;
  }

  if (is_inherit && !state.ParentStyle()->HasExplicitlyInheritedProperties() &&
      !CSSPropertyMetadata::IsInheritedProperty(id)) {
    state.ParentStyle()->SetHasExplicitlyInheritedProperties();
  } else if (value.IsUnsetValue()) {
    DCHECK(!is_inherit && !is_initial);
    if (CSSPropertyMetadata::IsInheritedProperty(id))
      is_inherit = true;
    else
      is_initial = true;
  }

  StyleBuilder::ApplyProperty(id, state, value, is_initial, is_inherit);
}

void StyleBuilderFunctions::applyInitialCSSPropertyColor(
    StyleResolverState& state) {
  Color color = ComputedStyle::InitialColor();
  if (state.ApplyPropertyToRegularStyle())
    state.Style()->SetColor(color);
  if (state.ApplyPropertyToVisitedLinkStyle())
    state.Style()->SetVisitedLinkColor(color);
}

void StyleBuilderFunctions::applyInheritCSSPropertyColor(
    StyleResolverState& state) {
  Color color = state.ParentStyle()->GetColor();
  if (state.ApplyPropertyToRegularStyle())
    state.Style()->SetColor(color);
  if (state.ApplyPropertyToVisitedLinkStyle())
    state.Style()->SetVisitedLinkColor(color);
}

void StyleBuilderFunctions::applyValueCSSPropertyColor(
    StyleResolverState& state,
    const CSSValue& value) {
  // As per the spec, 'color: currentColor' is treated as 'color: inherit'
  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueCurrentcolor) {
    applyInheritCSSPropertyColor(state);
    return;
  }

  if (state.ApplyPropertyToRegularStyle())
    state.Style()->SetColor(StyleBuilderConverter::ConvertColor(state, value));
  if (state.ApplyPropertyToVisitedLinkStyle())
    state.Style()->SetVisitedLinkColor(
        StyleBuilderConverter::ConvertColor(state, value, true));
}

void StyleBuilderFunctions::applyInitialCSSPropertyCursor(
    StyleResolverState& state) {
  state.Style()->ClearCursorList();
  state.Style()->SetCursor(ComputedStyle::InitialCursor());
}

void StyleBuilderFunctions::applyInheritCSSPropertyCursor(
    StyleResolverState& state) {
  state.Style()->SetCursor(state.ParentStyle()->Cursor());
  state.Style()->SetCursorList(state.ParentStyle()->Cursors());
}

void StyleBuilderFunctions::applyValueCSSPropertyCursor(
    StyleResolverState& state,
    const CSSValue& value) {
  state.Style()->ClearCursorList();
  if (value.IsValueList()) {
    state.Style()->SetCursor(ECursor::kAuto);
    for (const auto& item : ToCSSValueList(value)) {
      if (item->IsCursorImageValue()) {
        const cssvalue::CSSCursorImageValue& cursor =
            cssvalue::ToCSSCursorImageValue(*item);
        const CSSValue& image = cursor.ImageValue();
        state.Style()->AddCursor(state.GetStyleImage(CSSPropertyCursor, image),
                                 cursor.HotSpotSpecified(), cursor.HotSpot());
      } else {
        state.Style()->SetCursor(
            ToCSSIdentifierValue(*item).ConvertTo<ECursor>());
      }
    }
  } else {
    state.Style()->SetCursor(ToCSSIdentifierValue(value).ConvertTo<ECursor>());
  }
}

void StyleBuilderFunctions::applyValueCSSPropertyDirection(
    StyleResolverState& state,
    const CSSValue& value) {
  state.Style()->SetDirection(
      ToCSSIdentifierValue(value).ConvertTo<TextDirection>());
}

void StyleBuilderFunctions::applyInitialCSSPropertyGridTemplateAreas(
    StyleResolverState& state) {
  state.Style()->SetNamedGridArea(ComputedStyle::InitialNamedGridArea());
  state.Style()->SetNamedGridAreaRowCount(
      ComputedStyle::InitialNamedGridAreaCount());
  state.Style()->SetNamedGridAreaColumnCount(
      ComputedStyle::InitialNamedGridAreaCount());
}

void StyleBuilderFunctions::applyInheritCSSPropertyGridTemplateAreas(
    StyleResolverState& state) {
  state.Style()->SetNamedGridArea(state.ParentStyle()->NamedGridArea());
  state.Style()->SetNamedGridAreaRowCount(
      state.ParentStyle()->NamedGridAreaRowCount());
  state.Style()->SetNamedGridAreaColumnCount(
      state.ParentStyle()->NamedGridAreaColumnCount());
}

void StyleBuilderFunctions::applyValueCSSPropertyGridTemplateAreas(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    // FIXME: Shouldn't we clear the grid-area values
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNone);
    return;
  }

  const CSSGridTemplateAreasValue& grid_template_areas_value =
      ToCSSGridTemplateAreasValue(value);
  const NamedGridAreaMap& new_named_grid_areas =
      grid_template_areas_value.GridAreaMap();

  NamedGridLinesMap named_grid_column_lines;
  NamedGridLinesMap named_grid_row_lines;
  StyleBuilderConverter::ConvertOrderedNamedGridLinesMapToNamedGridLinesMap(
      state.Style()->OrderedNamedGridColumnLines(), named_grid_column_lines);
  StyleBuilderConverter::ConvertOrderedNamedGridLinesMapToNamedGridLinesMap(
      state.Style()->OrderedNamedGridRowLines(), named_grid_row_lines);
  StyleBuilderConverter::CreateImplicitNamedGridLinesFromGridArea(
      new_named_grid_areas, named_grid_column_lines, kForColumns);
  StyleBuilderConverter::CreateImplicitNamedGridLinesFromGridArea(
      new_named_grid_areas, named_grid_row_lines, kForRows);
  state.Style()->SetNamedGridColumnLines(named_grid_column_lines);
  state.Style()->SetNamedGridRowLines(named_grid_row_lines);

  state.Style()->SetNamedGridArea(new_named_grid_areas);
  state.Style()->SetNamedGridAreaRowCount(grid_template_areas_value.RowCount());
  state.Style()->SetNamedGridAreaColumnCount(
      grid_template_areas_value.ColumnCount());
}

void StyleBuilderFunctions::applyValueCSSPropertyListStyleImage(
    StyleResolverState& state,
    const CSSValue& value) {
  state.Style()->SetListStyleImage(
      state.GetStyleImage(CSSPropertyListStyleImage, value));
}

void StyleBuilderFunctions::applyInitialCSSPropertyOutlineStyle(
    StyleResolverState& state) {
  state.Style()->SetOutlineStyleIsAuto(
      ComputedStyle::InitialOutlineStyleIsAuto());
  state.Style()->SetOutlineStyle(EBorderStyle::kNone);
}

void StyleBuilderFunctions::applyInheritCSSPropertyOutlineStyle(
    StyleResolverState& state) {
  state.Style()->SetOutlineStyleIsAuto(
      state.ParentStyle()->OutlineStyleIsAuto());
  state.Style()->SetOutlineStyle(state.ParentStyle()->OutlineStyle());
}

void StyleBuilderFunctions::applyValueCSSPropertyOutlineStyle(
    StyleResolverState& state,
    const CSSValue& value) {
  const CSSIdentifierValue& identifier_value = ToCSSIdentifierValue(value);
  state.Style()->SetOutlineStyleIsAuto(
      identifier_value.ConvertTo<OutlineIsAuto>());
  state.Style()->SetOutlineStyle(identifier_value.ConvertTo<EBorderStyle>());
}

void StyleBuilderFunctions::applyValueCSSPropertyResize(
    StyleResolverState& state,
    const CSSValue& value) {
  const CSSIdentifierValue& identifier_value = ToCSSIdentifierValue(value);

  EResize r = RESIZE_NONE;
  if (identifier_value.GetValueID() == CSSValueAuto) {
    if (Settings* settings = state.GetDocument().GetSettings())
      r = settings->GetTextAreasAreResizable() ? RESIZE_BOTH : RESIZE_NONE;
  } else {
    r = identifier_value.ConvertTo<EResize>();
  }
  state.Style()->SetResize(r);
}

static float MmToPx(float mm) {
  return mm * kCssPixelsPerMillimeter;
}
static float InchToPx(float inch) {
  return inch * kCssPixelsPerInch;
}
static FloatSize GetPageSizeFromName(const CSSIdentifierValue& page_size_name) {
  switch (page_size_name.GetValueID()) {
    case CSSValueA5:
      return FloatSize(MmToPx(148), MmToPx(210));
    case CSSValueA4:
      return FloatSize(MmToPx(210), MmToPx(297));
    case CSSValueA3:
      return FloatSize(MmToPx(297), MmToPx(420));
    case CSSValueB5:
      return FloatSize(MmToPx(176), MmToPx(250));
    case CSSValueB4:
      return FloatSize(MmToPx(250), MmToPx(353));
    case CSSValueLetter:
      return FloatSize(InchToPx(8.5), InchToPx(11));
    case CSSValueLegal:
      return FloatSize(InchToPx(8.5), InchToPx(14));
    case CSSValueLedger:
      return FloatSize(InchToPx(11), InchToPx(17));
    default:
      NOTREACHED();
      return FloatSize(0, 0);
  }
}

void StyleBuilderFunctions::applyInitialCSSPropertySize(StyleResolverState&) {}
void StyleBuilderFunctions::applyInheritCSSPropertySize(StyleResolverState&) {}
void StyleBuilderFunctions::applyValueCSSPropertySize(StyleResolverState& state,
                                                      const CSSValue& value) {
  state.Style()->ResetPageSizeType();
  FloatSize size;
  PageSizeType page_size_type = PAGE_SIZE_AUTO;
  const CSSValueList& list = ToCSSValueList(value);
  if (list.length() == 2) {
    // <length>{2} | <page-size> <orientation>
    const CSSValue& first = list.Item(0);
    const CSSValue& second = list.Item(1);
    if (first.IsPrimitiveValue() && ToCSSPrimitiveValue(first).IsLength()) {
      // <length>{2}
      size = FloatSize(
          ToCSSPrimitiveValue(first).ComputeLength<float>(
              state.CssToLengthConversionData().CopyWithAdjustedZoom(1.0)),
          ToCSSPrimitiveValue(second).ComputeLength<float>(
              state.CssToLengthConversionData().CopyWithAdjustedZoom(1.0)));
    } else {
      // <page-size> <orientation>
      size = GetPageSizeFromName(ToCSSIdentifierValue(first));

      DCHECK(ToCSSIdentifierValue(second).GetValueID() == CSSValueLandscape ||
             ToCSSIdentifierValue(second).GetValueID() == CSSValuePortrait);
      if (ToCSSIdentifierValue(second).GetValueID() == CSSValueLandscape)
        size = size.TransposedSize();
    }
    page_size_type = PAGE_SIZE_RESOLVED;
  } else {
    DCHECK_EQ(list.length(), 1U);
    // <length> | auto | <page-size> | [ portrait | landscape]
    const CSSValue& first = list.Item(0);
    if (first.IsPrimitiveValue() && ToCSSPrimitiveValue(first).IsLength()) {
      // <length>
      page_size_type = PAGE_SIZE_RESOLVED;
      float width = ToCSSPrimitiveValue(first).ComputeLength<float>(
          state.CssToLengthConversionData().CopyWithAdjustedZoom(1.0));
      size = FloatSize(width, width);
    } else {
      const CSSIdentifierValue& ident = ToCSSIdentifierValue(first);
      switch (ident.GetValueID()) {
        case CSSValueAuto:
          page_size_type = PAGE_SIZE_AUTO;
          break;
        case CSSValuePortrait:
          page_size_type = PAGE_SIZE_AUTO_PORTRAIT;
          break;
        case CSSValueLandscape:
          page_size_type = PAGE_SIZE_AUTO_LANDSCAPE;
          break;
        default:
          // <page-size>
          page_size_type = PAGE_SIZE_RESOLVED;
          size = GetPageSizeFromName(ident);
      }
    }
  }
  state.Style()->SetPageSizeType(page_size_type);
  state.Style()->SetPageSize(size);
}

void StyleBuilderFunctions::applyValueCSSPropertyTextAlign(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() != CSSValueWebkitMatchParent) {
    // Special case for th elements - UA stylesheet text-align does not apply if
    // parent's computed value for text-align is not its initial value
    // https://html.spec.whatwg.org/multipage/rendering.html#tables-2
    const CSSIdentifierValue& ident_value = ToCSSIdentifierValue(value);
    if (ident_value.GetValueID() == CSSValueInternalCenter &&
        state.ParentStyle()->GetTextAlign() !=
            ComputedStyle::InitialTextAlign())
      state.Style()->SetTextAlign(state.ParentStyle()->GetTextAlign());
    else
      state.Style()->SetTextAlign(ident_value.ConvertTo<ETextAlign>());
  } else if (state.ParentStyle()->GetTextAlign() == ETextAlign::kStart) {
    state.Style()->SetTextAlign(state.ParentStyle()->IsLeftToRightDirection()
                                    ? ETextAlign::kLeft
                                    : ETextAlign::kRight);
  } else if (state.ParentStyle()->GetTextAlign() == ETextAlign::kEnd) {
    state.Style()->SetTextAlign(state.ParentStyle()->IsLeftToRightDirection()
                                    ? ETextAlign::kRight
                                    : ETextAlign::kLeft);
  } else {
    state.Style()->SetTextAlign(state.ParentStyle()->GetTextAlign());
  }
}

void StyleBuilderFunctions::applyInheritCSSPropertyTextIndent(
    StyleResolverState& state) {
  state.Style()->SetTextIndent(state.ParentStyle()->TextIndent());
  state.Style()->SetTextIndentLine(state.ParentStyle()->GetTextIndentLine());
  state.Style()->SetTextIndentType(state.ParentStyle()->GetTextIndentType());
}

void StyleBuilderFunctions::applyInitialCSSPropertyTextIndent(
    StyleResolverState& state) {
  state.Style()->SetTextIndent(ComputedStyle::InitialTextIndent());
  state.Style()->SetTextIndentLine(ComputedStyle::InitialTextIndentLine());
  state.Style()->SetTextIndentType(ComputedStyle::InitialTextIndentType());
}

void StyleBuilderFunctions::applyValueCSSPropertyTextIndent(
    StyleResolverState& state,
    const CSSValue& value) {
  Length length_or_percentage_value;
  TextIndentLine text_indent_line_value =
      ComputedStyle::InitialTextIndentLine();
  TextIndentType text_indent_type_value =
      ComputedStyle::InitialTextIndentType();

  for (auto& list_value : ToCSSValueList(value)) {
    if (list_value->IsPrimitiveValue()) {
      length_or_percentage_value =
          ToCSSPrimitiveValue(*list_value)
              .ConvertToLength(state.CssToLengthConversionData());
    } else if (ToCSSIdentifierValue(*list_value).GetValueID() ==
               CSSValueEachLine) {
      text_indent_line_value = kTextIndentEachLine;
    } else if (ToCSSIdentifierValue(*list_value).GetValueID() ==
               CSSValueHanging) {
      text_indent_type_value = kTextIndentHanging;
    } else {
      NOTREACHED();
    }
  }

  state.Style()->SetTextIndent(length_or_percentage_value);
  state.Style()->SetTextIndentLine(text_indent_line_value);
  state.Style()->SetTextIndentType(text_indent_type_value);
}

void StyleBuilderFunctions::applyInheritCSSPropertyVerticalAlign(
    StyleResolverState& state) {
  EVerticalAlign vertical_align = state.ParentStyle()->VerticalAlign();
  state.Style()->SetVerticalAlign(vertical_align);
  if (vertical_align == EVerticalAlign::kLength)
    state.Style()->SetVerticalAlignLength(
        state.ParentStyle()->GetVerticalAlignLength());
}

void StyleBuilderFunctions::applyValueCSSPropertyVerticalAlign(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    state.Style()->SetVerticalAlign(
        ToCSSIdentifierValue(value).ConvertTo<EVerticalAlign>());
  } else {
    state.Style()->SetVerticalAlignLength(
        ToCSSPrimitiveValue(value).ConvertToLength(
            state.CssToLengthConversionData()));
  }
}

static void ResetEffectiveZoom(StyleResolverState& state) {
  // Reset the zoom in effect. This allows the setZoom method to accurately
  // compute a new zoom in effect.
  state.SetEffectiveZoom(state.ParentStyle()
                             ? state.ParentStyle()->EffectiveZoom()
                             : ComputedStyle::InitialZoom());
}

void StyleBuilderFunctions::applyInitialCSSPropertyZoom(
    StyleResolverState& state) {
  ResetEffectiveZoom(state);
  state.SetZoom(ComputedStyle::InitialZoom());
}

void StyleBuilderFunctions::applyInheritCSSPropertyZoom(
    StyleResolverState& state) {
  ResetEffectiveZoom(state);
  state.SetZoom(state.ParentStyle()->Zoom());
}

void StyleBuilderFunctions::applyValueCSSPropertyZoom(StyleResolverState& state,
                                                      const CSSValue& value) {
  SECURITY_DCHECK(value.IsPrimitiveValue() || value.IsIdentifierValue());

  if (value.IsIdentifierValue()) {
    const CSSIdentifierValue& identifier_value = ToCSSIdentifierValue(value);
    if (identifier_value.GetValueID() == CSSValueNormal) {
      ResetEffectiveZoom(state);
      state.SetZoom(ComputedStyle::InitialZoom());
    }
  } else if (value.IsPrimitiveValue()) {
    const CSSPrimitiveValue& primitive_value = ToCSSPrimitiveValue(value);
    if (primitive_value.IsPercentage()) {
      ResetEffectiveZoom(state);
      if (float percent = primitive_value.GetFloatValue())
        state.SetZoom(percent / 100.0f);
    } else if (primitive_value.IsNumber()) {
      ResetEffectiveZoom(state);
      if (float number = primitive_value.GetFloatValue())
        state.SetZoom(number);
    }
  }
}

void StyleBuilderFunctions::applyValueCSSPropertyWebkitBorderImage(
    StyleResolverState& state,
    const CSSValue& value) {
  NinePieceImage image;
  CSSToStyleMap::MapNinePieceImage(state, CSSPropertyWebkitBorderImage, value,
                                   image);
  state.Style()->SetBorderImage(image);
}

void StyleBuilderFunctions::applyInitialCSSPropertyWebkitTextEmphasisStyle(
    StyleResolverState& state) {
  state.Style()->SetTextEmphasisFill(ComputedStyle::InitialTextEmphasisFill());
  state.Style()->SetTextEmphasisMark(ComputedStyle::InitialTextEmphasisMark());
  state.Style()->SetTextEmphasisCustomMark(
      ComputedStyle::InitialTextEmphasisCustomMark());
}

void StyleBuilderFunctions::applyInheritCSSPropertyWebkitTextEmphasisStyle(
    StyleResolverState& state) {
  state.Style()->SetTextEmphasisFill(
      state.ParentStyle()->GetTextEmphasisFill());
  state.Style()->SetTextEmphasisMark(
      state.ParentStyle()->GetTextEmphasisMark());
  state.Style()->SetTextEmphasisCustomMark(
      state.ParentStyle()->TextEmphasisCustomMark());
}

void StyleBuilderFunctions::applyValueCSSPropertyWebkitTextEmphasisStyle(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsValueList()) {
    const CSSValueList& list = ToCSSValueList(value);
    DCHECK_EQ(list.length(), 2U);
    for (unsigned i = 0; i < 2; ++i) {
      const CSSIdentifierValue& value = ToCSSIdentifierValue(list.Item(i));
      if (value.GetValueID() == CSSValueFilled ||
          value.GetValueID() == CSSValueOpen)
        state.Style()->SetTextEmphasisFill(value.ConvertTo<TextEmphasisFill>());
      else
        state.Style()->SetTextEmphasisMark(value.ConvertTo<TextEmphasisMark>());
    }
    state.Style()->SetTextEmphasisCustomMark(g_null_atom);
    return;
  }

  if (value.IsStringValue()) {
    state.Style()->SetTextEmphasisFill(kTextEmphasisFillFilled);
    state.Style()->SetTextEmphasisMark(kTextEmphasisMarkCustom);
    state.Style()->SetTextEmphasisCustomMark(
        AtomicString(ToCSSStringValue(value).Value()));
    return;
  }

  const CSSIdentifierValue& identifier_value = ToCSSIdentifierValue(value);

  state.Style()->SetTextEmphasisCustomMark(g_null_atom);

  if (identifier_value.GetValueID() == CSSValueFilled ||
      identifier_value.GetValueID() == CSSValueOpen) {
    state.Style()->SetTextEmphasisFill(
        identifier_value.ConvertTo<TextEmphasisFill>());
    state.Style()->SetTextEmphasisMark(kTextEmphasisMarkAuto);
  } else {
    state.Style()->SetTextEmphasisFill(kTextEmphasisFillFilled);
    state.Style()->SetTextEmphasisMark(
        identifier_value.ConvertTo<TextEmphasisMark>());
  }
}

void StyleBuilderFunctions::applyInitialCSSPropertyWillChange(
    StyleResolverState& state) {
  state.Style()->SetWillChangeContents(false);
  state.Style()->SetWillChangeScrollPosition(false);
  state.Style()->SetWillChangeProperties(Vector<CSSPropertyID>());
  state.Style()->SetSubtreeWillChangeContents(
      state.ParentStyle()->SubtreeWillChangeContents());
}

void StyleBuilderFunctions::applyInheritCSSPropertyWillChange(
    StyleResolverState& state) {
  state.Style()->SetWillChangeContents(
      state.ParentStyle()->WillChangeContents());
  state.Style()->SetWillChangeScrollPosition(
      state.ParentStyle()->WillChangeScrollPosition());
  state.Style()->SetWillChangeProperties(
      state.ParentStyle()->WillChangeProperties());
  state.Style()->SetSubtreeWillChangeContents(
      state.ParentStyle()->SubtreeWillChangeContents());
}

void StyleBuilderFunctions::applyValueCSSPropertyWillChange(
    StyleResolverState& state,
    const CSSValue& value) {
  bool will_change_contents = false;
  bool will_change_scroll_position = false;
  Vector<CSSPropertyID> will_change_properties;

  if (value.IsIdentifierValue()) {
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueAuto);
  } else {
    DCHECK(value.IsValueList());
    for (auto& will_change_value : ToCSSValueList(value)) {
      if (will_change_value->IsCustomIdentValue())
        will_change_properties.push_back(
            ToCSSCustomIdentValue(*will_change_value).ValueAsPropertyID());
      else if (ToCSSIdentifierValue(*will_change_value).GetValueID() ==
               CSSValueContents)
        will_change_contents = true;
      else if (ToCSSIdentifierValue(*will_change_value).GetValueID() ==
               CSSValueScrollPosition)
        will_change_scroll_position = true;
      else
        NOTREACHED();
    }
  }
  state.Style()->SetWillChangeContents(will_change_contents);
  state.Style()->SetWillChangeScrollPosition(will_change_scroll_position);
  state.Style()->SetWillChangeProperties(will_change_properties);
  state.Style()->SetSubtreeWillChangeContents(
      will_change_contents || state.ParentStyle()->SubtreeWillChangeContents());
}

void StyleBuilderFunctions::applyInitialCSSPropertyContent(
    StyleResolverState& state) {
  state.Style()->SetContent(nullptr);
}

void StyleBuilderFunctions::applyInheritCSSPropertyContent(
    StyleResolverState&) {
  // FIXME: In CSS3, it will be possible to inherit content. In CSS2 it is not.
  // This note is a reminder that eventually "inherit" needs to be supported.
}

void StyleBuilderFunctions::applyValueCSSPropertyContent(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    DCHECK(ToCSSIdentifierValue(value).GetValueID() == CSSValueNormal ||
           ToCSSIdentifierValue(value).GetValueID() == CSSValueNone);
    state.Style()->SetContent(nullptr);
    return;
  }

  ContentData* first_content = nullptr;
  ContentData* prev_content = nullptr;
  for (auto& item : ToCSSValueList(value)) {
    ContentData* next_content = nullptr;
    if (item->IsImageGeneratorValue() || item->IsImageSetValue() ||
        item->IsImageValue()) {
      next_content =
          ContentData::Create(state.GetStyleImage(CSSPropertyContent, *item));
    } else if (item->IsCounterValue()) {
      const cssvalue::CSSCounterValue* counter_value =
          cssvalue::ToCSSCounterValue(item.Get());
      const auto list_style_type =
          CssValueIDToPlatformEnum<EListStyleType>(counter_value->ListStyle());
      std::unique_ptr<CounterContent> counter =
          WTF::WrapUnique(new CounterContent(
              AtomicString(counter_value->Identifier()), list_style_type,
              AtomicString(counter_value->Separator())));
      next_content = ContentData::Create(std::move(counter));
    } else if (item->IsIdentifierValue()) {
      QuoteType quote_type;
      switch (ToCSSIdentifierValue(*item).GetValueID()) {
        default:
          NOTREACHED();
        case CSSValueOpenQuote:
          quote_type = OPEN_QUOTE;
          break;
        case CSSValueCloseQuote:
          quote_type = CLOSE_QUOTE;
          break;
        case CSSValueNoOpenQuote:
          quote_type = NO_OPEN_QUOTE;
          break;
        case CSSValueNoCloseQuote:
          quote_type = NO_CLOSE_QUOTE;
          break;
      }
      next_content = ContentData::Create(quote_type);
    } else {
      String string;
      if (item->IsFunctionValue()) {
        const CSSFunctionValue* function_value = ToCSSFunctionValue(item.Get());
        DCHECK_EQ(function_value->FunctionType(), CSSValueAttr);
        // FIXME: Can a namespace be specified for an attr(foo)?
        if (state.Style()->StyleType() == kPseudoIdNone)
          state.Style()->SetUnique();
        else
          state.ParentStyle()->SetUnique();
        QualifiedName attr(
            g_null_atom, ToCSSCustomIdentValue(function_value->Item(0)).Value(),
            g_null_atom);
        const AtomicString& value = state.GetElement()->getAttribute(attr);
        string = value.IsNull() ? g_empty_string : value.GetString();
      } else {
        string = ToCSSStringValue(*item).Value();
      }
      if (prev_content && prev_content->IsText()) {
        TextContentData* text_content = ToTextContentData(prev_content);
        text_content->SetText(text_content->GetText() + string);
        continue;
      }
      next_content = ContentData::Create(string);
    }

    if (!first_content)
      first_content = next_content;
    else
      prev_content->SetNext(next_content);

    prev_content = next_content;
  }
  DCHECK(first_content);
  state.Style()->SetContent(first_content);
}

void StyleBuilderFunctions::applyValueCSSPropertyWebkitLocale(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueAuto);
    state.GetFontBuilder().SetLocale(nullptr);
  } else {
    state.GetFontBuilder().SetLocale(
        LayoutLocale::Get(AtomicString(ToCSSStringValue(value).Value())));
  }
}

void StyleBuilderFunctions::applyInitialCSSPropertyWebkitAppRegion(
    StyleResolverState&) {}

void StyleBuilderFunctions::applyInheritCSSPropertyWebkitAppRegion(
    StyleResolverState&) {}

void StyleBuilderFunctions::applyValueCSSPropertyWebkitAppRegion(
    StyleResolverState& state,
    const CSSValue& value) {
  const CSSIdentifierValue& identifier_value = ToCSSIdentifierValue(value);
  state.Style()->SetDraggableRegionMode(
      identifier_value.GetValueID() == CSSValueDrag ? kDraggableRegionDrag
                                                    : kDraggableRegionNoDrag);
  state.GetDocument().SetHasAnnotatedRegions(true);
}

void StyleBuilderFunctions::applyValueCSSPropertyWritingMode(
    StyleResolverState& state,
    const CSSValue& value) {
  state.SetWritingMode(ToCSSIdentifierValue(value).ConvertTo<WritingMode>());
}

void StyleBuilderFunctions::applyValueCSSPropertyWebkitWritingMode(
    StyleResolverState& state,
    const CSSValue& value) {
  state.SetWritingMode(ToCSSIdentifierValue(value).ConvertTo<WritingMode>());
}

void StyleBuilderFunctions::applyValueCSSPropertyTextOrientation(
    StyleResolverState& state,
    const CSSValue& value) {
  state.SetTextOrientation(
      ToCSSIdentifierValue(value).ConvertTo<TextOrientation>());
}

void StyleBuilderFunctions::applyValueCSSPropertyWebkitTextOrientation(
    StyleResolverState& state,
    const CSSValue& value) {
  state.SetTextOrientation(
      ToCSSIdentifierValue(value).ConvertTo<TextOrientation>());
}

void StyleBuilderFunctions::applyValueCSSPropertyVariable(
    StyleResolverState& state,
    const CSSValue& value) {
  const CSSCustomPropertyDeclaration& declaration =
      ToCSSCustomPropertyDeclaration(value);
  const AtomicString& name = declaration.GetName();
  const PropertyRegistration* registration = nullptr;
  const PropertyRegistry* registry = state.GetDocument().GetPropertyRegistry();
  if (registry)
    registration = registry->Registration(name);

  bool is_inherited_property = !registration || registration->Inherits();
  bool initial = declaration.IsInitial(is_inherited_property);
  bool inherit = declaration.IsInherit(is_inherited_property);
  DCHECK(!(initial && inherit));

  if (!initial && !inherit) {
    if (declaration.Value()->NeedsVariableResolution()) {
      if (is_inherited_property)
        state.Style()->SetUnresolvedInheritedVariable(name,
                                                      declaration.Value());
      else
        state.Style()->SetUnresolvedNonInheritedVariable(name,
                                                         declaration.Value());
      return;
    }

    if (!registration) {
      state.Style()->SetResolvedUnregisteredVariable(name, declaration.Value());
      return;
    }

    const CSSValue* parsed_value =
        declaration.Value()->ParseForSyntax(registration->Syntax());
    if (parsed_value) {
      DCHECK(parsed_value);
      if (is_inherited_property)
        state.Style()->SetResolvedInheritedVariable(name, declaration.Value(),
                                                    parsed_value);
      else
        state.Style()->SetResolvedNonInheritedVariable(
            name, declaration.Value(), parsed_value);
      return;
    }
    if (is_inherited_property)
      inherit = true;
    else
      initial = true;
  }
  DCHECK(initial ^ inherit);

  state.Style()->RemoveVariable(name, is_inherited_property);
  if (initial) {
    return;
  }

  DCHECK(inherit);
  CSSVariableData* parent_value =
      state.ParentStyle()->GetVariable(name, is_inherited_property);
  const CSSValue* parent_css_value =
      registration && parent_value ? state.ParentStyle()->GetRegisteredVariable(
                                         name, is_inherited_property)
                                   : nullptr;

  if (!is_inherited_property) {
    DCHECK(registration);
    if (parent_value) {
      state.Style()->SetResolvedNonInheritedVariable(name, parent_value,
                                                     parent_css_value);
    }
    return;
  }

  if (parent_value) {
    if (!registration) {
      state.Style()->SetResolvedUnregisteredVariable(name, parent_value);
    } else {
      state.Style()->SetResolvedInheritedVariable(name, parent_value,
                                                  parent_css_value);
    }
  }
}

void StyleBuilderFunctions::applyInheritCSSPropertyBaselineShift(
    StyleResolverState& state) {
  const SVGComputedStyle& parent_svg_style = state.ParentStyle()->SvgStyle();
  EBaselineShift baseline_shift = parent_svg_style.BaselineShift();
  SVGComputedStyle& svg_style = state.Style()->AccessSVGStyle();
  svg_style.SetBaselineShift(baseline_shift);
  if (baseline_shift == BS_LENGTH)
    svg_style.SetBaselineShiftValue(parent_svg_style.BaselineShiftValue());
}

void StyleBuilderFunctions::applyValueCSSPropertyBaselineShift(
    StyleResolverState& state,
    const CSSValue& value) {
  SVGComputedStyle& svg_style = state.Style()->AccessSVGStyle();
  if (!value.IsIdentifierValue()) {
    svg_style.SetBaselineShift(BS_LENGTH);
    svg_style.SetBaselineShiftValue(StyleBuilderConverter::ConvertLength(
        state, ToCSSPrimitiveValue(value)));
    return;
  }
  switch (ToCSSIdentifierValue(value).GetValueID()) {
    case CSSValueBaseline:
      svg_style.SetBaselineShift(BS_LENGTH);
      svg_style.SetBaselineShiftValue(Length(kFixed));
      return;
    case CSSValueSub:
      svg_style.SetBaselineShift(BS_SUB);
      return;
    case CSSValueSuper:
      svg_style.SetBaselineShift(BS_SUPER);
      return;
    default:
      NOTREACHED();
  }
}

void StyleBuilderFunctions::applyInheritCSSPropertyPosition(
    StyleResolverState& state) {
  if (!state.ParentNode()->IsDocumentNode())
    state.Style()->SetPosition(state.ParentStyle()->GetPosition());
}

void StyleBuilderFunctions::applyInitialCSSPropertyCaretColor(
    StyleResolverState& state) {
  StyleAutoColor color = StyleAutoColor::AutoColor();
  if (state.ApplyPropertyToRegularStyle())
    state.Style()->SetCaretColor(color);
  if (state.ApplyPropertyToVisitedLinkStyle())
    state.Style()->SetVisitedLinkCaretColor(color);
}

void StyleBuilderFunctions::applyInheritCSSPropertyCaretColor(
    StyleResolverState& state) {
  StyleAutoColor color = state.ParentStyle()->CaretColor();
  if (state.ApplyPropertyToRegularStyle())
    state.Style()->SetCaretColor(color);
  if (state.ApplyPropertyToVisitedLinkStyle())
    state.Style()->SetVisitedLinkCaretColor(color);
}

void StyleBuilderFunctions::applyValueCSSPropertyCaretColor(
    StyleResolverState& state,
    const CSSValue& value) {
  if (state.ApplyPropertyToRegularStyle()) {
    state.Style()->SetCaretColor(
        StyleBuilderConverter::ConvertStyleAutoColor(state, value));
  }
  if (state.ApplyPropertyToVisitedLinkStyle()) {
    state.Style()->SetVisitedLinkCaretColor(
        StyleBuilderConverter::ConvertStyleAutoColor(state, value, true));
  }
}

}  // namespace blink
