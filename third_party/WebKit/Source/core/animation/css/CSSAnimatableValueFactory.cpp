/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
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

#include "core/animation/css/CSSAnimatableValueFactory.h"

#include "core/CSSValueKeywords.h"
#include "core/animation/FontWeightConversion.h"
#include "core/animation/LengthPropertyFunctions.h"
#include "core/animation/PropertyHandle.h"
#include "core/animation/animatable/AnimatableClipPathOperation.h"
#include "core/animation/animatable/AnimatableColor.h"
#include "core/animation/animatable/AnimatableDouble.h"
#include "core/animation/animatable/AnimatableDoubleAndBool.h"
#include "core/animation/animatable/AnimatableFilterOperations.h"
#include "core/animation/animatable/AnimatableFontVariationSettings.h"
#include "core/animation/animatable/AnimatableImage.h"
#include "core/animation/animatable/AnimatableLength.h"
#include "core/animation/animatable/AnimatableLengthBox.h"
#include "core/animation/animatable/AnimatableLengthBoxAndBool.h"
#include "core/animation/animatable/AnimatableLengthPoint.h"
#include "core/animation/animatable/AnimatableLengthPoint3D.h"
#include "core/animation/animatable/AnimatableLengthSize.h"
#include "core/animation/animatable/AnimatablePath.h"
#include "core/animation/animatable/AnimatableRepeatable.h"
#include "core/animation/animatable/AnimatableSVGPaint.h"
#include "core/animation/animatable/AnimatableShadow.h"
#include "core/animation/animatable/AnimatableShapeValue.h"
#include "core/animation/animatable/AnimatableStrokeDasharrayList.h"
#include "core/animation/animatable/AnimatableTransform.h"
#include "core/animation/animatable/AnimatableUnknown.h"
#include "core/animation/animatable/AnimatableVisibility.h"
#include "core/css/CSSCalculationValue.h"
#include "core/css/CSSIdentifierValue.h"
#include "core/css/CSSPrimitiveValue.h"
#include "core/css/CSSPrimitiveValueMappings.h"
#include "core/css/CSSPropertyMetadata.h"
#include "core/style/ComputedStyle.h"
#include "platform/Length.h"
#include "platform/LengthBox.h"
#include "platform/wtf/StdLibExtras.h"

namespace blink {

static PassRefPtr<AnimatableValue> CreateFromLengthWithZoom(
    const Length& length,
    float zoom) {
  switch (length.GetType()) {
    case kFixed:
    case kPercent:
    case kCalculated:
      return AnimatableLength::Create(length, zoom);
    case kAuto:
    case kMinContent:
    case kMaxContent:
    case kFillAvailable:
    case kFitContent:
      return AnimatableUnknown::Create(CSSValue::Create(length, 1));
    case kMaxSizeNone:
      return AnimatableUnknown::Create(CSSValueNone);
    case kExtendToZoom:  // Does not apply to elements.
    case kDeviceWidth:
    case kDeviceHeight:
      NOTREACHED();
      return nullptr;
  }
  NOTREACHED();
  return nullptr;
}

static PassRefPtr<AnimatableValue> CreateFromLength(
    const Length& length,
    const ComputedStyle& style) {
  return CreateFromLengthWithZoom(length, style.EffectiveZoom());
}

static PassRefPtr<AnimatableValue> CreateFromPropertyLength(
    CSSPropertyID property,
    const ComputedStyle& style) {
  Length length;
  bool success = LengthPropertyFunctions::GetLength(property, style, length);
  DCHECK(success);
  return CreateFromLength(length, style);
}

static PassRefPtr<AnimatableValue> CreateFromUnzoomedLength(
    const UnzoomedLength& unzoomed_length) {
  return CreateFromLengthWithZoom(unzoomed_length.length(), 1);
}

static PassRefPtr<AnimatableValue> CreateFromLineHeight(
    const Length& length,
    const ComputedStyle& style) {
  if (length.GetType() == kPercent) {
    double value = length.Value();
    // -100% is used to represent "normal" line height.
    if (value == -100)
      return AnimatableUnknown::Create(CSSValueNormal);
    return AnimatableDouble::Create(value);
  }
  return CreateFromLength(length, style);
}

inline static PassRefPtr<AnimatableValue> CreateFromDouble(double value) {
  return AnimatableDouble::Create(value);
}

inline static PassRefPtr<AnimatableValue> CreateFromLengthBox(
    const LengthBox& length_box,
    const ComputedStyle& style) {
  return AnimatableLengthBox::Create(
      CreateFromLength(length_box.Left(), style),
      CreateFromLength(length_box.Right(), style),
      CreateFromLength(length_box.Top(), style),
      CreateFromLength(length_box.Bottom(), style));
}

static PassRefPtr<AnimatableValue> CreateFromBorderImageLength(
    const BorderImageLength& border_image_length,
    const ComputedStyle& style) {
  if (border_image_length.IsNumber())
    return CreateFromDouble(border_image_length.Number());
  return CreateFromLength(border_image_length.length(), style);
}

inline static PassRefPtr<AnimatableValue> CreateFromBorderImageLengthBox(
    const BorderImageLengthBox& border_image_length_box,
    const ComputedStyle& style) {
  return AnimatableLengthBox::Create(
      CreateFromBorderImageLength(border_image_length_box.Left(), style),
      CreateFromBorderImageLength(border_image_length_box.Right(), style),
      CreateFromBorderImageLength(border_image_length_box.Top(), style),
      CreateFromBorderImageLength(border_image_length_box.Bottom(), style));
}

inline static PassRefPtr<AnimatableValue> CreateFromLengthBoxAndBool(
    const LengthBox length_box,
    const bool flag,
    const ComputedStyle& style) {
  return AnimatableLengthBoxAndBool::Create(
      CreateFromLengthBox(length_box, style), flag);
}

inline static PassRefPtr<AnimatableValue> CreateFromDoubleAndBool(
    double number,
    const bool flag,
    const ComputedStyle& style) {
  return AnimatableDoubleAndBool::Create(number, flag);
}

inline static PassRefPtr<AnimatableValue> CreateFromLengthPoint(
    const LengthPoint& length_point,
    const ComputedStyle& style) {
  return AnimatableLengthPoint::Create(
      CreateFromLength(length_point.X(), style),
      CreateFromLength(length_point.Y(), style));
}

inline static PassRefPtr<AnimatableValue> CreateFromTransformOrigin(
    const TransformOrigin& transform_origin,
    const ComputedStyle& style) {
  return AnimatableLengthPoint3D::Create(
      CreateFromLength(transform_origin.X(), style),
      CreateFromLength(transform_origin.Y(), style),
      CreateFromLength(Length(transform_origin.Z(), kFixed), style));
}

inline static PassRefPtr<AnimatableValue> CreateFromLengthSize(
    const LengthSize& length_size,
    const ComputedStyle& style) {
  return AnimatableLengthSize::Create(
      CreateFromLength(length_size.Width(), style),
      CreateFromLength(length_size.Height(), style));
}

inline static PassRefPtr<AnimatableValue> CreateFromStyleImage(
    StyleImage* image) {
  if (image) {
    if (CSSValue* css_value = image->CssValue())
      return AnimatableImage::Create(css_value);
  }
  return AnimatableUnknown::Create(CSSValueNone);
}

inline static PassRefPtr<AnimatableValue> CreateFromFillSize(
    const FillSize& fill_size,
    const ComputedStyle& style) {
  switch (fill_size.type) {
    case kSizeLength:
      return CreateFromLengthSize(fill_size.size, style);
    case kContain:
    case kCover:
    case kSizeNone:
      return AnimatableUnknown::Create(
          CSSIdentifierValue::Create(fill_size.type));
    default:
      NOTREACHED();
      return nullptr;
  }
}

inline static PassRefPtr<AnimatableValue> CreateFromBackgroundPosition(
    const Length& length,
    bool origin_is_set,
    BackgroundEdgeOrigin origin,
    const ComputedStyle& style) {
  if (!origin_is_set || origin == kLeftEdge || origin == kTopEdge)
    return CreateFromLength(length, style);
  return CreateFromLength(length.SubtractFromOneHundredPercent(), style);
}

template <CSSPropertyID property>
inline static PassRefPtr<AnimatableValue> CreateFromFillLayers(
    const FillLayer& fill_layers,
    const ComputedStyle& style) {
  Vector<RefPtr<AnimatableValue>> values;
  for (const FillLayer* fill_layer = &fill_layers; fill_layer;
       fill_layer = fill_layer->Next()) {
    if (property == CSSPropertyBackgroundImage ||
        property == CSSPropertyWebkitMaskImage) {
      if (!fill_layer->IsImageSet())
        break;
      values.push_back(CreateFromStyleImage(fill_layer->GetImage()));
    } else if (property == CSSPropertyBackgroundPositionX ||
               property == CSSPropertyWebkitMaskPositionX) {
      if (!fill_layer->IsXPositionSet())
        break;
      values.push_back(CreateFromBackgroundPosition(
          fill_layer->XPosition(), fill_layer->IsBackgroundXOriginSet(),
          fill_layer->BackgroundXOrigin(), style));
    } else if (property == CSSPropertyBackgroundPositionY ||
               property == CSSPropertyWebkitMaskPositionY) {
      if (!fill_layer->IsYPositionSet())
        break;
      values.push_back(CreateFromBackgroundPosition(
          fill_layer->YPosition(), fill_layer->IsBackgroundYOriginSet(),
          fill_layer->BackgroundYOrigin(), style));
    } else if (property == CSSPropertyBackgroundSize ||
               property == CSSPropertyWebkitMaskSize) {
      if (!fill_layer->IsSizeSet())
        break;
      values.push_back(CreateFromFillSize(fill_layer->Size(), style));
    } else {
      NOTREACHED();
    }
  }
  return AnimatableRepeatable::Create(values);
}

PassRefPtr<AnimatableValue> CSSAnimatableValueFactory::CreateFromColor(
    CSSPropertyID property,
    const ComputedStyle& style) {
  Color color = style.ColorIncludingFallback(property, false);
  Color visited_link_color = style.ColorIncludingFallback(property, true);
  return AnimatableColor::Create(color, visited_link_color);
}

inline static PassRefPtr<AnimatableValue> CreateFromShapeValue(
    ShapeValue* value) {
  if (value)
    return AnimatableShapeValue::Create(value);
  return AnimatableUnknown::Create(CSSValueNone);
}

static PassRefPtr<AnimatableValue> CreateFromPath(StylePath* path) {
  return AnimatablePath::Create(path);
}

static double FontStretchToDouble(FontStretch font_stretch) {
  return static_cast<unsigned>(font_stretch);
}

static PassRefPtr<AnimatableValue> CreateFromFontStretch(
    FontStretch font_stretch) {
  return CreateFromDouble(FontStretchToDouble(font_stretch));
}

static PassRefPtr<AnimatableValue> CreateFromFontVariationSettings(
    FontVariationSettings* settings) {
  return AnimatableFontVariationSettings::Create(settings);
}

static PassRefPtr<AnimatableValue> CreateFromTransformProperties(
    PassRefPtr<TransformOperation> transform,
    double zoom,
    PassRefPtr<TransformOperation> initial_transform) {
  TransformOperations operation;
  bool has_transform = static_cast<bool>(transform);
  if (has_transform || initial_transform) {
    operation.Operations().push_back(
        std::move(has_transform ? transform : initial_transform));
  }
  return AnimatableTransform::Create(operation, has_transform ? zoom : 1);
}

static PassRefPtr<AnimatableValue> CreateFromFontWeight(
    FontWeight font_weight) {
  return CreateFromDouble(FontWeightToDouble(font_weight));
}

static SVGPaintType NormalizeSVGPaintType(SVGPaintType paint_type) {
  // If the <paint> is 'currentColor', then create an AnimatableSVGPaint with
  // a <rgbcolor> type. This is similar in vein to the handling of colors.
  return paint_type == SVG_PAINTTYPE_CURRENTCOLOR ? SVG_PAINTTYPE_RGBCOLOR
                                                  : paint_type;
}

PassRefPtr<AnimatableValue> CSSAnimatableValueFactory::Create(
    const PropertyHandle& property,
    const ComputedStyle& style) {
  if (property.IsCSSCustomProperty()) {
    return AnimatableUnknown::Create(
        style.GetRegisteredVariable(property.CustomPropertyName()));
  }

  CSSPropertyID property_id = property.CssProperty();
  DCHECK(CSSPropertyMetadata::IsInterpolableProperty(property_id));
  switch (property_id) {
    case CSSPropertyBackgroundColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyBackgroundImage:
      return CreateFromFillLayers<CSSPropertyBackgroundImage>(
          style.BackgroundLayers(), style);
    case CSSPropertyBackgroundPositionX:
      return CreateFromFillLayers<CSSPropertyBackgroundPositionX>(
          style.BackgroundLayers(), style);
    case CSSPropertyBackgroundPositionY:
      return CreateFromFillLayers<CSSPropertyBackgroundPositionY>(
          style.BackgroundLayers(), style);
    case CSSPropertyBackgroundSize:
      return CreateFromFillLayers<CSSPropertyBackgroundSize>(
          style.BackgroundLayers(), style);
    case CSSPropertyBaselineShift:
      switch (style.SvgStyle().BaselineShift()) {
        case BS_SUPER:
          return AnimatableUnknown::Create(
              CSSIdentifierValue::Create(CSSValueSuper));
        case BS_SUB:
          return AnimatableUnknown::Create(
              CSSIdentifierValue::Create(CSSValueSub));
        default:
          return CreateFromLength(style.BaselineShiftValue(), style);
      }
    case CSSPropertyBorderBottomColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyBorderBottomLeftRadius:
      return CreateFromLengthSize(style.BorderBottomLeftRadius(), style);
    case CSSPropertyBorderBottomRightRadius:
      return CreateFromLengthSize(style.BorderBottomRightRadius(), style);
    case CSSPropertyBorderBottomWidth:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyBorderImageOutset:
      return CreateFromBorderImageLengthBox(style.BorderImageOutset(), style);
    case CSSPropertyBorderImageSlice:
      return CreateFromLengthBoxAndBool(style.BorderImageSlices(),
                                        style.BorderImageSlicesFill(), style);
    case CSSPropertyBorderImageSource:
      return CreateFromStyleImage(style.BorderImageSource());
    case CSSPropertyBorderImageWidth:
      return CreateFromBorderImageLengthBox(style.BorderImageWidth(), style);
    case CSSPropertyBorderLeftColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyBorderLeftWidth:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyBorderRightColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyBorderRightWidth:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyBorderTopColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyBorderTopLeftRadius:
      return CreateFromLengthSize(style.BorderTopLeftRadius(), style);
    case CSSPropertyBorderTopRightRadius:
      return CreateFromLengthSize(style.BorderTopRightRadius(), style);
    case CSSPropertyBorderTopWidth:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyBottom:
      return CreateFromLength(style.Bottom(), style);
    case CSSPropertyBoxShadow:
      return AnimatableShadow::Create(style.BoxShadow());
    case CSSPropertyClip:
      if (style.HasAutoClip()) {
        return AnimatableUnknown::Create(
            CSSIdentifierValue::Create(CSSValueAuto));
      }
      return CreateFromLengthBox(style.Clip(), style);
    case CSSPropertyCaretColor:
      if (style.CaretColor().IsAutoColor()) {
        return AnimatableUnknown::Create(
            CSSIdentifierValue::Create(CSSValueAuto));
      }
      return CreateFromColor(property_id, style);
    case CSSPropertyColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyFillOpacity:
      return CreateFromDouble(style.FillOpacity());
    case CSSPropertyFill:
      return AnimatableSVGPaint::Create(
          NormalizeSVGPaintType(style.SvgStyle().FillPaintType()),
          NormalizeSVGPaintType(style.SvgStyle().VisitedLinkFillPaintType()),
          style.SvgStyle().FillPaintColor(),
          style.SvgStyle().VisitedLinkFillPaintColor(),
          style.SvgStyle().FillPaintUri(),
          style.SvgStyle().VisitedLinkFillPaintUri());
    case CSSPropertyFlexGrow:
      return CreateFromDouble(style.FlexGrow());
    case CSSPropertyFlexShrink:
      return CreateFromDouble(style.FlexShrink());
    case CSSPropertyFlexBasis:
      return CreateFromLength(style.FlexBasis(), style);
    case CSSPropertyFloodColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyFloodOpacity:
      return CreateFromDouble(style.FloodOpacity());
    case CSSPropertyFontSize:
      // Must pass a specified size to setFontSize if Text Autosizing is
      // enabled, but a computed size if text zoom is enabled (if neither is
      // enabled it's irrelevant as they're probably the same).
      // FIXME: Should we introduce an option to pass the computed font size
      // here, allowing consumers to enable text zoom rather than Text
      // Autosizing? See http://crbug.com/227545.
      return CreateFromDouble(style.SpecifiedFontSize());
    case CSSPropertyFontSizeAdjust:
      return style.HasFontSizeAdjust()
                 ? CreateFromDouble(style.FontSizeAdjust())
                 : AnimatableUnknown::Create(CSSValueNone);
    case CSSPropertyFontStretch:
      return CreateFromFontStretch(style.GetFontStretch());
    case CSSPropertyFontVariationSettings:
      return CreateFromFontVariationSettings(
          style.GetFontDescription().VariationSettings());
    case CSSPropertyFontWeight:
      return CreateFromFontWeight(style.GetFontWeight());
    case CSSPropertyHeight:
      return CreateFromLength(style.Height(), style);
    case CSSPropertyLightingColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyListStyleImage:
      return CreateFromStyleImage(style.ListStyleImage());
    case CSSPropertyLeft:
      return CreateFromLength(style.Left(), style);
    case CSSPropertyLetterSpacing:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyLineHeight:
      return CreateFromLineHeight(style.SpecifiedLineHeight(), style);
    case CSSPropertyMarginBottom:
      return CreateFromLength(style.MarginBottom(), style);
    case CSSPropertyMarginLeft:
      return CreateFromLength(style.MarginLeft(), style);
    case CSSPropertyMarginRight:
      return CreateFromLength(style.MarginRight(), style);
    case CSSPropertyMarginTop:
      return CreateFromLength(style.MarginTop(), style);
    case CSSPropertyMaxHeight:
      return CreateFromLength(style.MaxHeight(), style);
    case CSSPropertyMaxWidth:
      return CreateFromLength(style.MaxWidth(), style);
    case CSSPropertyMinHeight:
      return CreateFromLength(style.MinHeight(), style);
    case CSSPropertyMinWidth:
      return CreateFromLength(style.MinWidth(), style);
    case CSSPropertyObjectPosition:
      return CreateFromLengthPoint(style.ObjectPosition(), style);
    case CSSPropertyOpacity:
      return CreateFromDouble(style.Opacity());
    case CSSPropertyOrder:
      return CreateFromDouble(style.Order());
    case CSSPropertyOrphans:
      return CreateFromDouble(style.Orphans());
    case CSSPropertyOutlineColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyOutlineOffset:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyOutlineWidth:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyPaddingBottom:
      return CreateFromLength(style.PaddingBottom(), style);
    case CSSPropertyPaddingLeft:
      return CreateFromLength(style.PaddingLeft(), style);
    case CSSPropertyPaddingRight:
      return CreateFromLength(style.PaddingRight(), style);
    case CSSPropertyPaddingTop:
      return CreateFromLength(style.PaddingTop(), style);
    case CSSPropertyRight:
      return CreateFromLength(style.Right(), style);
    case CSSPropertyStrokeWidth:
      return CreateFromUnzoomedLength(style.StrokeWidth());
    case CSSPropertyStopColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyStopOpacity:
      return CreateFromDouble(style.StopOpacity());
    case CSSPropertyStrokeDasharray:
      return AnimatableStrokeDasharrayList::Create(style.StrokeDashArray(),
                                                   style.EffectiveZoom());
    case CSSPropertyStrokeDashoffset:
      return CreateFromLength(style.StrokeDashOffset(), style);
    case CSSPropertyStrokeMiterlimit:
      return CreateFromDouble(style.StrokeMiterLimit());
    case CSSPropertyStrokeOpacity:
      return CreateFromDouble(style.StrokeOpacity());
    case CSSPropertyStroke:
      return AnimatableSVGPaint::Create(
          NormalizeSVGPaintType(style.SvgStyle().StrokePaintType()),
          NormalizeSVGPaintType(style.SvgStyle().VisitedLinkStrokePaintType()),
          style.SvgStyle().StrokePaintColor(),
          style.SvgStyle().VisitedLinkStrokePaintColor(),
          style.SvgStyle().StrokePaintUri(),
          style.SvgStyle().VisitedLinkStrokePaintUri());
    case CSSPropertyTextDecorationColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyTextIndent:
      return CreateFromLength(style.TextIndent(), style);
    case CSSPropertyTextShadow:
      return AnimatableShadow::Create(style.TextShadow());
    case CSSPropertyTop:
      return CreateFromLength(style.Top(), style);
    case CSSPropertyWebkitBorderHorizontalSpacing:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyWebkitBorderVerticalSpacing:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyClipPath:
      if (ClipPathOperation* operation = style.ClipPath())
        return AnimatableClipPathOperation::Create(operation);
      return AnimatableUnknown::Create(CSSValueNone);
    case CSSPropertyColumnCount:
      if (style.HasAutoColumnCount())
        return AnimatableUnknown::Create(CSSValueAuto);
      return CreateFromDouble(style.ColumnCount());
    case CSSPropertyColumnGap:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyColumnRuleColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyColumnRuleWidth:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyColumnWidth:
      if (style.HasAutoColumnWidth())
        return AnimatableUnknown::Create(CSSValueAuto);
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyFilter:
      return AnimatableFilterOperations::Create(style.Filter());
    case CSSPropertyBackdropFilter:
      return AnimatableFilterOperations::Create(style.BackdropFilter());
    case CSSPropertyWebkitMaskBoxImageOutset:
      return CreateFromBorderImageLengthBox(style.MaskBoxImageOutset(), style);
    case CSSPropertyWebkitMaskBoxImageSlice:
      return CreateFromLengthBoxAndBool(style.MaskBoxImageSlices(),
                                        style.MaskBoxImageSlicesFill(), style);
    case CSSPropertyWebkitMaskBoxImageSource:
      return CreateFromStyleImage(style.MaskBoxImageSource());
    case CSSPropertyWebkitMaskBoxImageWidth:
      return CreateFromBorderImageLengthBox(style.MaskBoxImageWidth(), style);
    case CSSPropertyWebkitMaskImage:
      return CreateFromFillLayers<CSSPropertyWebkitMaskImage>(
          style.MaskLayers(), style);
    case CSSPropertyWebkitMaskPositionX:
      return CreateFromFillLayers<CSSPropertyWebkitMaskPositionX>(
          style.MaskLayers(), style);
    case CSSPropertyWebkitMaskPositionY:
      return CreateFromFillLayers<CSSPropertyWebkitMaskPositionY>(
          style.MaskLayers(), style);
    case CSSPropertyWebkitMaskSize:
      return CreateFromFillLayers<CSSPropertyWebkitMaskSize>(style.MaskLayers(),
                                                             style);
    case CSSPropertyPerspective:
      if (style.Perspective() == 0) {
        return AnimatableUnknown::Create(
            CSSIdentifierValue::Create(CSSValueNone));
      }
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyPerspectiveOrigin:
      return CreateFromLengthPoint(style.PerspectiveOrigin(), style);
    case CSSPropertyShapeOutside:
      return CreateFromShapeValue(style.ShapeOutside());
    case CSSPropertyShapeMargin:
      return CreateFromLength(style.ShapeMargin(), style);
    case CSSPropertyShapeImageThreshold:
      return CreateFromDouble(style.ShapeImageThreshold());
    case CSSPropertyWebkitTextStrokeColor:
      return CreateFromColor(property_id, style);
    case CSSPropertyTransform:
      return AnimatableTransform::Create(style.Transform(),
                                         style.EffectiveZoom());
    case CSSPropertyTranslate: {
      return CreateFromTransformProperties(style.Translate(),
                                           style.EffectiveZoom(), nullptr);
    }
    case CSSPropertyRotate: {
      return CreateFromTransformProperties(style.Rotate(),
                                           style.EffectiveZoom(), nullptr);
    }
    case CSSPropertyScale: {
      return CreateFromTransformProperties(style.Scale(), style.EffectiveZoom(),
                                           nullptr);
    }
    case CSSPropertyOffsetAnchor:
      return CreateFromLengthPoint(style.OffsetAnchor(), style);
    case CSSPropertyOffsetDistance:
      return CreateFromLength(style.OffsetDistance(), style);
    case CSSPropertyOffsetPosition:
      return CreateFromLengthPoint(style.OffsetPosition(), style);
    case CSSPropertyOffsetRotate:
      return CreateFromDoubleAndBool(
          style.OffsetRotate().angle,
          style.OffsetRotate().type == kOffsetRotationAuto, style);
    case CSSPropertyTransformOrigin:
      return CreateFromTransformOrigin(style.GetTransformOrigin(), style);
    case CSSPropertyWebkitPerspectiveOriginX:
      return CreateFromLength(style.PerspectiveOriginX(), style);
    case CSSPropertyWebkitPerspectiveOriginY:
      return CreateFromLength(style.PerspectiveOriginY(), style);
    case CSSPropertyWebkitTransformOriginX:
      return CreateFromLength(style.TransformOriginX(), style);
    case CSSPropertyWebkitTransformOriginY:
      return CreateFromLength(style.TransformOriginY(), style);
    case CSSPropertyWebkitTransformOriginZ:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyWidows:
      return CreateFromDouble(style.Widows());
    case CSSPropertyWidth:
      return CreateFromLength(style.Width(), style);
    case CSSPropertyWordSpacing:
      return CreateFromPropertyLength(property_id, style);
    case CSSPropertyVerticalAlign:
      if (style.VerticalAlign() == EVerticalAlign::kLength)
        return CreateFromLength(style.GetVerticalAlignLength(), style);
      return AnimatableUnknown::Create(
          CSSIdentifierValue::Create(style.VerticalAlign()));
    case CSSPropertyVisibility:
      return AnimatableVisibility::Create(style.Visibility());
    case CSSPropertyD:
      return CreateFromPath(style.SvgStyle().D());
    case CSSPropertyCx:
      return CreateFromLength(style.SvgStyle().Cx(), style);
    case CSSPropertyCy:
      return CreateFromLength(style.SvgStyle().Cy(), style);
    case CSSPropertyX:
      return CreateFromLength(style.SvgStyle().X(), style);
    case CSSPropertyY:
      return CreateFromLength(style.SvgStyle().Y(), style);
    case CSSPropertyR:
      return CreateFromLength(style.SvgStyle().R(), style);
    case CSSPropertyRx:
      return CreateFromLength(style.SvgStyle().Rx(), style);
    case CSSPropertyRy:
      return CreateFromLength(style.SvgStyle().Ry(), style);
    case CSSPropertyZIndex:
      if (style.HasAutoZIndex())
        return AnimatableUnknown::Create(CSSValueAuto);
      return CreateFromDouble(style.ZIndex());
    default:
      NOTREACHED();
      return nullptr;
  }
}

}  // namespace blink
