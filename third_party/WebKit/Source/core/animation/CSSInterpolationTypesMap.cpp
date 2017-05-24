// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/animation/CSSInterpolationTypesMap.h"

#include <memory>
#include "core/animation/CSSAngleInterpolationType.h"
#include "core/animation/CSSBasicShapeInterpolationType.h"
#include "core/animation/CSSBorderImageLengthBoxInterpolationType.h"
#include "core/animation/CSSClipInterpolationType.h"
#include "core/animation/CSSColorInterpolationType.h"
#include "core/animation/CSSFilterListInterpolationType.h"
#include "core/animation/CSSFontSizeInterpolationType.h"
#include "core/animation/CSSFontVariationSettingsInterpolationType.h"
#include "core/animation/CSSFontWeightInterpolationType.h"
#include "core/animation/CSSImageInterpolationType.h"
#include "core/animation/CSSImageListInterpolationType.h"
#include "core/animation/CSSImageSliceInterpolationType.h"
#include "core/animation/CSSLengthInterpolationType.h"
#include "core/animation/CSSLengthListInterpolationType.h"
#include "core/animation/CSSLengthPairInterpolationType.h"
#include "core/animation/CSSNumberInterpolationType.h"
#include "core/animation/CSSOffsetRotateInterpolationType.h"
#include "core/animation/CSSPaintInterpolationType.h"
#include "core/animation/CSSPathInterpolationType.h"
#include "core/animation/CSSPositionAxisListInterpolationType.h"
#include "core/animation/CSSPositionInterpolationType.h"
#include "core/animation/CSSResolutionInterpolationType.h"
#include "core/animation/CSSRotateInterpolationType.h"
#include "core/animation/CSSScaleInterpolationType.h"
#include "core/animation/CSSShadowListInterpolationType.h"
#include "core/animation/CSSSizeListInterpolationType.h"
#include "core/animation/CSSTextIndentInterpolationType.h"
#include "core/animation/CSSTimeInterpolationType.h"
#include "core/animation/CSSTransformInterpolationType.h"
#include "core/animation/CSSTransformOriginInterpolationType.h"
#include "core/animation/CSSTranslateInterpolationType.h"
#include "core/animation/CSSValueInterpolationType.h"
#include "core/animation/CSSVisibilityInterpolationType.h"
#include "core/css/CSSPropertyMetadata.h"
#include "core/css/CSSSyntaxDescriptor.h"
#include "core/css/PropertyRegistry.h"
#include "platform/wtf/PtrUtil.h"

namespace blink {

static const PropertyRegistration* GetRegistration(
    const PropertyRegistry* registry,
    const PropertyHandle& property) {
  DCHECK(property.IsCSSCustomProperty());
  if (!registry) {
    return nullptr;
  }
  return registry->Registration(property.CustomPropertyName());
}

const InterpolationTypes& CSSInterpolationTypesMap::Get(
    const PropertyHandle& property) const {
  using ApplicableTypesMap =
      HashMap<PropertyHandle, std::unique_ptr<const InterpolationTypes>>;
  DEFINE_STATIC_LOCAL(ApplicableTypesMap, applicable_types_map, ());
  auto entry = applicable_types_map.find(property);
  bool found_entry = entry != applicable_types_map.end();

  // Custom property interpolation types may change over time so don't trust the
  // applicableTypesMap without checking the registry.
  if (registry_ && property.IsCSSCustomProperty()) {
    const auto* registration = GetRegistration(registry_.Get(), property);
    if (registration) {
      if (found_entry) {
        applicable_types_map.erase(entry);
      }
      return registration->GetInterpolationTypes();
    }
  }

  if (found_entry) {
    return *entry->value;
  }

  std::unique_ptr<InterpolationTypes> applicable_types =
      WTF::MakeUnique<InterpolationTypes>();

  CSSPropertyID css_property = property.IsCSSProperty()
                                   ? property.CssProperty()
                                   : property.PresentationAttribute();
  // We treat presentation attributes identically to their CSS property
  // equivalents when interpolating.
  PropertyHandle used_property =
      property.IsCSSProperty() ? property : PropertyHandle(css_property);
  switch (css_property) {
    case CSSPropertyBaselineShift:
    case CSSPropertyBorderBottomWidth:
    case CSSPropertyBorderLeftWidth:
    case CSSPropertyBorderRightWidth:
    case CSSPropertyBorderTopWidth:
    case CSSPropertyBottom:
    case CSSPropertyCx:
    case CSSPropertyCy:
    case CSSPropertyFlexBasis:
    case CSSPropertyHeight:
    case CSSPropertyLeft:
    case CSSPropertyLetterSpacing:
    case CSSPropertyMarginBottom:
    case CSSPropertyMarginLeft:
    case CSSPropertyMarginRight:
    case CSSPropertyMarginTop:
    case CSSPropertyMaxHeight:
    case CSSPropertyMaxWidth:
    case CSSPropertyMinHeight:
    case CSSPropertyMinWidth:
    case CSSPropertyOffsetDistance:
    case CSSPropertyOutlineOffset:
    case CSSPropertyOutlineWidth:
    case CSSPropertyPaddingBottom:
    case CSSPropertyPaddingLeft:
    case CSSPropertyPaddingRight:
    case CSSPropertyPaddingTop:
    case CSSPropertyPerspective:
    case CSSPropertyR:
    case CSSPropertyRight:
    case CSSPropertyRx:
    case CSSPropertyRy:
    case CSSPropertyShapeMargin:
    case CSSPropertyStrokeDashoffset:
    case CSSPropertyStrokeWidth:
    case CSSPropertyTop:
    case CSSPropertyVerticalAlign:
    case CSSPropertyWebkitBorderHorizontalSpacing:
    case CSSPropertyWebkitBorderVerticalSpacing:
    case CSSPropertyColumnGap:
    case CSSPropertyColumnRuleWidth:
    case CSSPropertyColumnWidth:
    case CSSPropertyWebkitPerspectiveOriginX:
    case CSSPropertyWebkitPerspectiveOriginY:
    case CSSPropertyWebkitTransformOriginX:
    case CSSPropertyWebkitTransformOriginY:
    case CSSPropertyWebkitTransformOriginZ:
    case CSSPropertyWidth:
    case CSSPropertyWordSpacing:
    case CSSPropertyX:
    case CSSPropertyY:
      applicable_types->push_back(
          WTF::MakeUnique<CSSLengthInterpolationType>(used_property));
      break;
    case CSSPropertyFlexGrow:
    case CSSPropertyFlexShrink:
    case CSSPropertyFillOpacity:
    case CSSPropertyFloodOpacity:
    case CSSPropertyFontSizeAdjust:
    case CSSPropertyOpacity:
    case CSSPropertyOrder:
    case CSSPropertyOrphans:
    case CSSPropertyShapeImageThreshold:
    case CSSPropertyStopOpacity:
    case CSSPropertyStrokeMiterlimit:
    case CSSPropertyStrokeOpacity:
    case CSSPropertyColumnCount:
    case CSSPropertyWidows:
    case CSSPropertyZIndex:
      applicable_types->push_back(
          WTF::MakeUnique<CSSNumberInterpolationType>(used_property));
      break;
    case CSSPropertyLineHeight:
      applicable_types->push_back(
          WTF::MakeUnique<CSSLengthInterpolationType>(used_property));
      applicable_types->push_back(
          WTF::MakeUnique<CSSNumberInterpolationType>(used_property));
      break;
    case CSSPropertyBackgroundColor:
    case CSSPropertyBorderBottomColor:
    case CSSPropertyBorderLeftColor:
    case CSSPropertyBorderRightColor:
    case CSSPropertyBorderTopColor:
    case CSSPropertyCaretColor:
    case CSSPropertyColor:
    case CSSPropertyFloodColor:
    case CSSPropertyLightingColor:
    case CSSPropertyOutlineColor:
    case CSSPropertyStopColor:
    case CSSPropertyTextDecorationColor:
    case CSSPropertyColumnRuleColor:
    case CSSPropertyWebkitTextStrokeColor:
      applicable_types->push_back(
          WTF::MakeUnique<CSSColorInterpolationType>(used_property));
      break;
    case CSSPropertyFill:
    case CSSPropertyStroke:
      applicable_types->push_back(
          WTF::MakeUnique<CSSPaintInterpolationType>(used_property));
      break;
    case CSSPropertyD:
      applicable_types->push_back(
          WTF::MakeUnique<CSSPathInterpolationType>(used_property));
      break;
    case CSSPropertyBoxShadow:
    case CSSPropertyTextShadow:
      applicable_types->push_back(
          WTF::MakeUnique<CSSShadowListInterpolationType>(used_property));
      break;
    case CSSPropertyBorderImageSource:
    case CSSPropertyListStyleImage:
    case CSSPropertyWebkitMaskBoxImageSource:
      applicable_types->push_back(
          WTF::MakeUnique<CSSImageInterpolationType>(used_property));
      break;
    case CSSPropertyBackgroundImage:
    case CSSPropertyWebkitMaskImage:
      applicable_types->push_back(
          WTF::MakeUnique<CSSImageListInterpolationType>(used_property));
      break;
    case CSSPropertyStrokeDasharray:
      applicable_types->push_back(
          WTF::MakeUnique<CSSLengthListInterpolationType>(used_property));
      break;
    case CSSPropertyFontWeight:
      applicable_types->push_back(
          WTF::MakeUnique<CSSFontWeightInterpolationType>(used_property));
      break;
    case CSSPropertyFontVariationSettings:
      applicable_types->push_back(
          WTF::MakeUnique<CSSFontVariationSettingsInterpolationType>(
              used_property));
      break;
    case CSSPropertyVisibility:
      applicable_types->push_back(
          WTF::MakeUnique<CSSVisibilityInterpolationType>(used_property));
      break;
    case CSSPropertyClip:
      applicable_types->push_back(
          WTF::MakeUnique<CSSClipInterpolationType>(used_property));
      break;
    case CSSPropertyOffsetRotate:
      applicable_types->push_back(
          WTF::MakeUnique<CSSOffsetRotateInterpolationType>(used_property));
      break;
    case CSSPropertyBackgroundPositionX:
    case CSSPropertyBackgroundPositionY:
    case CSSPropertyWebkitMaskPositionX:
    case CSSPropertyWebkitMaskPositionY:
      applicable_types->push_back(
          WTF::MakeUnique<CSSPositionAxisListInterpolationType>(used_property));
      break;
    case CSSPropertyObjectPosition:
    case CSSPropertyOffsetAnchor:
    case CSSPropertyOffsetPosition:
    case CSSPropertyPerspectiveOrigin:
      applicable_types->push_back(
          WTF::MakeUnique<CSSPositionInterpolationType>(used_property));
      break;
    case CSSPropertyBorderBottomLeftRadius:
    case CSSPropertyBorderBottomRightRadius:
    case CSSPropertyBorderTopLeftRadius:
    case CSSPropertyBorderTopRightRadius:
      applicable_types->push_back(
          WTF::MakeUnique<CSSLengthPairInterpolationType>(used_property));
      break;
    case CSSPropertyTranslate:
      applicable_types->push_back(
          WTF::MakeUnique<CSSTranslateInterpolationType>(used_property));
      break;
    case CSSPropertyTransformOrigin:
      applicable_types->push_back(
          WTF::MakeUnique<CSSTransformOriginInterpolationType>(used_property));
      break;
    case CSSPropertyBackgroundSize:
    case CSSPropertyWebkitMaskSize:
      applicable_types->push_back(
          WTF::MakeUnique<CSSSizeListInterpolationType>(used_property));
      break;
    case CSSPropertyBorderImageOutset:
    case CSSPropertyBorderImageWidth:
    case CSSPropertyWebkitMaskBoxImageOutset:
    case CSSPropertyWebkitMaskBoxImageWidth:
      applicable_types->push_back(
          WTF::MakeUnique<CSSBorderImageLengthBoxInterpolationType>(
              used_property));
      break;
    case CSSPropertyScale:
      applicable_types->push_back(
          WTF::MakeUnique<CSSScaleInterpolationType>(used_property));
      break;
    case CSSPropertyFontSize:
      applicable_types->push_back(
          WTF::MakeUnique<CSSFontSizeInterpolationType>(used_property));
      break;
    case CSSPropertyTextIndent:
      applicable_types->push_back(
          WTF::MakeUnique<CSSTextIndentInterpolationType>(used_property));
      break;
    case CSSPropertyBorderImageSlice:
    case CSSPropertyWebkitMaskBoxImageSlice:
      applicable_types->push_back(
          WTF::MakeUnique<CSSImageSliceInterpolationType>(used_property));
      break;
    case CSSPropertyClipPath:
    case CSSPropertyShapeOutside:
      applicable_types->push_back(
          WTF::MakeUnique<CSSBasicShapeInterpolationType>(used_property));
      break;
    case CSSPropertyRotate:
      applicable_types->push_back(
          WTF::MakeUnique<CSSRotateInterpolationType>(used_property));
      break;
    case CSSPropertyBackdropFilter:
    case CSSPropertyFilter:
      applicable_types->push_back(
          WTF::MakeUnique<CSSFilterListInterpolationType>(used_property));
      break;
    case CSSPropertyTransform:
      applicable_types->push_back(
          WTF::MakeUnique<CSSTransformInterpolationType>(used_property));
      break;
    case CSSPropertyVariable:
      DCHECK_EQ(GetRegistration(registry_.Get(), property), nullptr);
      break;
    default:
      DCHECK(!CSSPropertyMetadata::IsInterpolableProperty(css_property));
      break;
  }

  applicable_types->push_back(
      WTF::MakeUnique<CSSValueInterpolationType>(used_property));

  auto add_result =
      applicable_types_map.insert(property, std::move(applicable_types));
  return *add_result.stored_value->value;
}

size_t CSSInterpolationTypesMap::Version() const {
  // Property registrations are never removed so the number of registered
  // custom properties is equivalent to how many changes there have been to the
  // property registry.
  return registry_ ? registry_->RegistrationCount() : 0;
}

CSSInterpolationTypes
CSSInterpolationTypesMap::CreateCSSInterpolationTypesForSyntax(
    const AtomicString& property_name,
    const CSSSyntaxDescriptor& descriptor) {
  PropertyHandle property(property_name);
  CSSInterpolationTypes result;
  for (const CSSSyntaxComponent& component : descriptor.Components()) {
    if (component.repeatable_) {
      // TODO(alancutter): Support animation of repeatable types.
      continue;
    }

    switch (component.type_) {
      case CSSSyntaxType::kAngle:
        result.push_back(WTF::MakeUnique<CSSAngleInterpolationType>(property));
        break;
      case CSSSyntaxType::kColor:
        result.push_back(WTF::MakeUnique<CSSColorInterpolationType>(property));
        break;
      case CSSSyntaxType::kLength:
      case CSSSyntaxType::kLengthPercentage:
      case CSSSyntaxType::kPercentage:
        result.push_back(WTF::MakeUnique<CSSLengthInterpolationType>(property));
        break;
      case CSSSyntaxType::kNumber:
        result.push_back(WTF::MakeUnique<CSSNumberInterpolationType>(property));
        break;
      case CSSSyntaxType::kResolution:
        result.push_back(
            WTF::MakeUnique<CSSResolutionInterpolationType>(property));
        break;
      case CSSSyntaxType::kTime:
        result.push_back(WTF::MakeUnique<CSSTimeInterpolationType>(property));
        break;
      case CSSSyntaxType::kImage:
      case CSSSyntaxType::kUrl:
      case CSSSyntaxType::kInteger:
      case CSSSyntaxType::kTransformFunction:
        // TODO(alancutter): Support smooth interpolation of these types.
        break;
      case CSSSyntaxType::kTokenStream:
      case CSSSyntaxType::kIdent:
      case CSSSyntaxType::kCustomIdent:
        // Uses the CSSValueInterpolationType added below.
        break;
      default:
        NOTREACHED();
        break;
    }
  }
  result.push_back(WTF::MakeUnique<CSSValueInterpolationType>(property));
  return result;
}

}  // namespace blink
