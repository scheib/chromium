// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/parser/CSSPropertyParser.h"

#include <memory>
#include "core/StylePropertyShorthand.h"
#include "core/css/CSSBasicShapeValues.h"
#include "core/css/CSSBorderImage.h"
#include "core/css/CSSContentDistributionValue.h"
#include "core/css/CSSCursorImageValue.h"
#include "core/css/CSSCustomIdentValue.h"
#include "core/css/CSSFontFaceSrcValue.h"
#include "core/css/CSSFontFamilyValue.h"
#include "core/css/CSSFunctionValue.h"
#include "core/css/CSSGridAutoRepeatValue.h"
#include "core/css/CSSGridLineNamesValue.h"
#include "core/css/CSSGridTemplateAreasValue.h"
#include "core/css/CSSIdentifierValue.h"
#include "core/css/CSSInheritedValue.h"
#include "core/css/CSSInitialValue.h"
#include "core/css/CSSPendingSubstitutionValue.h"
#include "core/css/CSSPrimitiveValueMappings.h"
#include "core/css/CSSQuadValue.h"
#include "core/css/CSSReflectValue.h"
#include "core/css/CSSShadowValue.h"
#include "core/css/CSSStringValue.h"
#include "core/css/CSSTimingFunctionValue.h"
#include "core/css/CSSURIValue.h"
#include "core/css/CSSUnicodeRangeValue.h"
#include "core/css/CSSUnsetValue.h"
#include "core/css/CSSValuePair.h"
#include "core/css/CSSVariableReferenceValue.h"
#include "core/css/HashTools.h"
#include "core/css/parser/CSSParserFastPaths.h"
#include "core/css/parser/CSSParserIdioms.h"
#include "core/css/parser/CSSPropertyParserHelpers.h"
#include "core/css/parser/CSSVariableParser.h"
#include "core/css/parser/FontVariantLigaturesParser.h"
#include "core/css/parser/FontVariantNumericParser.h"
#include "core/css/properties/CSSPropertyAPI.h"
#include "core/css/properties/CSSPropertyAlignmentUtils.h"
#include "core/css/properties/CSSPropertyColumnUtils.h"
#include "core/css/properties/CSSPropertyDescriptor.h"
#include "core/css/properties/CSSPropertyFontUtils.h"
#include "core/css/properties/CSSPropertyLengthUtils.h"
#include "core/css/properties/CSSPropertyMarginUtils.h"
#include "core/css/properties/CSSPropertyOffsetPathUtils.h"
#include "core/css/properties/CSSPropertyPositionUtils.h"
#include "core/css/properties/CSSPropertyShapeUtils.h"
#include "core/css/properties/CSSPropertyWebkitBorderWidthUtils.h"
#include "core/frame/UseCounter.h"
#include "core/layout/LayoutTheme.h"
#include "platform/wtf/text/StringBuilder.h"

namespace blink {

using namespace CSSPropertyParserHelpers;

CSSPropertyParser::CSSPropertyParser(
    const CSSParserTokenRange& range,
    const CSSParserContext* context,
    HeapVector<CSSProperty, 256>* parsed_properties)
    : range_(range), context_(context), parsed_properties_(parsed_properties) {
  range_.ConsumeWhitespace();
}

void CSSPropertyParser::AddProperty(CSSPropertyID property,
                                    CSSPropertyID current_shorthand,
                                    const CSSValue& value,
                                    bool important,
                                    bool implicit) {
  DCHECK(!isPropertyAlias(property));

  int shorthand_index = 0;
  bool set_from_shorthand = false;

  if (current_shorthand) {
    Vector<StylePropertyShorthand, 4> shorthands;
    getMatchingShorthandsForLonghand(property, &shorthands);
    set_from_shorthand = true;
    if (shorthands.size() > 1)
      shorthand_index =
          indexOfShorthandForLonghand(current_shorthand, shorthands);
  }

  parsed_properties_->push_back(CSSProperty(property, value, important,
                                            set_from_shorthand, shorthand_index,
                                            implicit));
}

void CSSPropertyParser::AddExpandedPropertyForValue(CSSPropertyID property,
                                                    const CSSValue& value,
                                                    bool important) {
  const StylePropertyShorthand& shorthand = shorthandForProperty(property);
  unsigned shorthand_length = shorthand.length();
  DCHECK(shorthand_length);
  const CSSPropertyID* longhands = shorthand.properties();
  for (unsigned i = 0; i < shorthand_length; ++i)
    AddProperty(longhands[i], property, value, important);
}

bool CSSPropertyParser::ParseValue(
    CSSPropertyID unresolved_property,
    bool important,
    const CSSParserTokenRange& range,
    const CSSParserContext* context,
    HeapVector<CSSProperty, 256>& parsed_properties,
    StyleRule::RuleType rule_type) {
  int parsed_properties_size = parsed_properties.size();

  CSSPropertyParser parser(range, context, &parsed_properties);
  CSSPropertyID resolved_property = resolveCSSPropertyID(unresolved_property);
  bool parse_success;

  if (rule_type == StyleRule::kViewport) {
    parse_success =
        (RuntimeEnabledFeatures::cssViewportEnabled() ||
         IsUASheetBehavior(context->Mode())) &&
        parser.ParseViewportDescriptor(resolved_property, important);
  } else if (rule_type == StyleRule::kFontFace) {
    parse_success = parser.ParseFontFaceDescriptor(resolved_property);
  } else {
    parse_success = parser.ParseValueStart(unresolved_property, important);
  }

  // This doesn't count UA style sheets
  if (parse_success)
    context->Count(context->Mode(), unresolved_property);

  if (!parse_success)
    parsed_properties.Shrink(parsed_properties_size);

  return parse_success;
}

const CSSValue* CSSPropertyParser::ParseSingleValue(
    CSSPropertyID property,
    const CSSParserTokenRange& range,
    const CSSParserContext* context) {
  CSSPropertyParser parser(range, context, nullptr);
  const CSSValue* value = parser.ParseSingleValue(property);
  if (!value || !parser.range_.AtEnd())
    return nullptr;
  return value;
}

bool CSSPropertyParser::ParseValueStart(CSSPropertyID unresolved_property,
                                        bool important) {
  if (ConsumeCSSWideKeyword(unresolved_property, important))
    return true;

  CSSParserTokenRange original_range = range_;
  CSSPropertyID property_id = resolveCSSPropertyID(unresolved_property);
  bool is_shorthand = isShorthandProperty(property_id);

  if (is_shorthand) {
    // Variable references will fail to parse here and will fall out to the
    // variable ref parser below.
    if (ParseShorthand(unresolved_property, important))
      return true;
  } else {
    if (const CSSValue* parsed_value = ParseSingleValue(unresolved_property)) {
      if (range_.AtEnd()) {
        AddProperty(property_id, CSSPropertyInvalid, *parsed_value, important);
        return true;
      }
    }
  }

  if (CSSVariableParser::ContainsValidVariableReferences(original_range)) {
    bool is_animation_tainted = false;
    CSSVariableReferenceValue* variable = CSSVariableReferenceValue::Create(
        CSSVariableData::Create(original_range, is_animation_tainted, true),
        *context_);

    if (is_shorthand) {
      const CSSPendingSubstitutionValue& pending_value =
          *CSSPendingSubstitutionValue::Create(property_id, variable);
      AddExpandedPropertyForValue(property_id, pending_value, important);
    } else {
      AddProperty(property_id, CSSPropertyInvalid, *variable, important);
    }
    return true;
  }

  return false;
}

template <typename CharacterType>
static CSSPropertyID UnresolvedCSSPropertyID(const CharacterType* property_name,
                                             unsigned length) {
  if (length == 0)
    return CSSPropertyInvalid;
  if (length >= 2 && property_name[0] == '-' && property_name[1] == '-')
    return CSSPropertyVariable;
  if (length > maxCSSPropertyNameLength)
    return CSSPropertyInvalid;

  char buffer[maxCSSPropertyNameLength + 1];  // 1 for null character

  for (unsigned i = 0; i != length; ++i) {
    CharacterType c = property_name[i];
    if (c == 0 || c >= 0x7F)
      return CSSPropertyInvalid;  // illegal character
    buffer[i] = ToASCIILower(c);
  }
  buffer[length] = '\0';

  const char* name = buffer;
  const Property* hash_table_entry = FindProperty(name, length);
  if (!hash_table_entry)
    return CSSPropertyInvalid;
  CSSPropertyID property = static_cast<CSSPropertyID>(hash_table_entry->id);
  if (!CSSPropertyMetadata::IsEnabledProperty(property))
    return CSSPropertyInvalid;
  return property;
}

CSSPropertyID unresolvedCSSPropertyID(const String& string) {
  unsigned length = string.length();
  return string.Is8Bit()
             ? UnresolvedCSSPropertyID(string.Characters8(), length)
             : UnresolvedCSSPropertyID(string.Characters16(), length);
}

CSSPropertyID UnresolvedCSSPropertyID(StringView string) {
  unsigned length = string.length();
  return string.Is8Bit()
             ? UnresolvedCSSPropertyID(string.Characters8(), length)
             : UnresolvedCSSPropertyID(string.Characters16(), length);
}

template <typename CharacterType>
static CSSValueID CssValueKeywordID(const CharacterType* value_keyword,
                                    unsigned length) {
  char buffer[maxCSSValueKeywordLength + 1];  // 1 for null character

  for (unsigned i = 0; i != length; ++i) {
    CharacterType c = value_keyword[i];
    if (c == 0 || c >= 0x7F)
      return CSSValueInvalid;  // illegal character
    buffer[i] = WTF::ToASCIILower(c);
  }
  buffer[length] = '\0';

  const Value* hash_table_entry = FindValue(buffer, length);
  return hash_table_entry ? static_cast<CSSValueID>(hash_table_entry->id)
                          : CSSValueInvalid;
}

CSSValueID CssValueKeywordID(StringView string) {
  unsigned length = string.length();
  if (!length)
    return CSSValueInvalid;
  if (length > maxCSSValueKeywordLength)
    return CSSValueInvalid;

  return string.Is8Bit() ? CssValueKeywordID(string.Characters8(), length)
                         : CssValueKeywordID(string.Characters16(), length);
}

bool CSSPropertyParser::ConsumeCSSWideKeyword(CSSPropertyID unresolved_property,
                                              bool important) {
  CSSParserTokenRange range_copy = range_;
  CSSValueID id = range_copy.ConsumeIncludingWhitespace().Id();
  if (!range_copy.AtEnd())
    return false;

  CSSValue* value = nullptr;
  if (id == CSSValueInitial)
    value = CSSInitialValue::Create();
  else if (id == CSSValueInherit)
    value = CSSInheritedValue::Create();
  else if (id == CSSValueUnset)
    value = CSSUnsetValue::Create();
  else
    return false;

  CSSPropertyID property = resolveCSSPropertyID(unresolved_property);
  const StylePropertyShorthand& shorthand = shorthandForProperty(property);
  if (!shorthand.length()) {
    if (!CSSPropertyMetadata::IsProperty(unresolved_property))
      return false;
    AddProperty(property, CSSPropertyInvalid, *value, important);
  } else {
    AddExpandedPropertyForValue(property, *value, important);
  }
  range_ = range_copy;
  return true;
}

static CSSIdentifierValue* ConsumeFontVariantCSS21(CSSParserTokenRange& range) {
  return ConsumeIdent<CSSValueNormal, CSSValueSmallCaps>(range);
}

static CSSValue* ConsumeFontVariantList(CSSParserTokenRange& range) {
  CSSValueList* values = CSSValueList::CreateCommaSeparated();
  do {
    if (range.Peek().Id() == CSSValueAll) {
      // FIXME: CSSPropertyParser::ParseFontVariant() implements
      // the old css3 draft:
      // http://www.w3.org/TR/2002/WD-css3-webfonts-20020802/#font-variant
      // 'all' is only allowed in @font-face and with no other values.
      if (values->length())
        return nullptr;
      return ConsumeIdent(range);
    }
    CSSIdentifierValue* font_variant = ConsumeFontVariantCSS21(range);
    if (font_variant)
      values->Append(*font_variant);
  } while (ConsumeCommaIncludingWhitespace(range));

  if (values->length())
    return values;

  return nullptr;
}

static CSSValue* ConsumeLocale(CSSParserTokenRange& range) {
  if (range.Peek().Id() == CSSValueAuto)
    return ConsumeIdent(range);
  return ConsumeString(range);
}

static CSSValue* ConsumeAnimationIterationCount(CSSParserTokenRange& range) {
  if (range.Peek().Id() == CSSValueInfinite)
    return ConsumeIdent(range);
  return ConsumeNumber(range, kValueRangeNonNegative);
}

static CSSValue* ConsumeAnimationName(CSSParserTokenRange& range,
                                      const CSSParserContext* context,
                                      bool allow_quoted_name) {
  if (range.Peek().Id() == CSSValueNone)
    return ConsumeIdent(range);

  if (allow_quoted_name && range.Peek().GetType() == kStringToken) {
    // Legacy support for strings in prefixed animations.
    context->Count(UseCounter::kQuotedAnimationName);

    const CSSParserToken& token = range.ConsumeIncludingWhitespace();
    if (EqualIgnoringASCIICase(token.Value(), "none"))
      return CSSIdentifierValue::Create(CSSValueNone);
    return CSSCustomIdentValue::Create(token.Value().ToAtomicString());
  }

  return ConsumeCustomIdent(range);
}

static CSSValue* ConsumeTransitionProperty(CSSParserTokenRange& range) {
  const CSSParserToken& token = range.Peek();
  if (token.GetType() != kIdentToken)
    return nullptr;
  if (token.Id() == CSSValueNone)
    return ConsumeIdent(range);

  CSSPropertyID unresolved_property = token.ParseAsUnresolvedCSSPropertyID();
  if (unresolved_property != CSSPropertyInvalid &&
      unresolved_property != CSSPropertyVariable) {
    DCHECK(CSSPropertyMetadata::IsEnabledProperty(unresolved_property));
    range.ConsumeIncludingWhitespace();
    return CSSCustomIdentValue::Create(unresolved_property);
  }
  return ConsumeCustomIdent(range);
}

static CSSValue* ConsumeSteps(CSSParserTokenRange& range) {
  DCHECK_EQ(range.Peek().FunctionId(), CSSValueSteps);
  CSSParserTokenRange range_copy = range;
  CSSParserTokenRange args = ConsumeFunction(range_copy);

  CSSPrimitiveValue* steps = ConsumePositiveInteger(args);
  if (!steps)
    return nullptr;

  StepsTimingFunction::StepPosition position =
      StepsTimingFunction::StepPosition::END;
  if (ConsumeCommaIncludingWhitespace(args)) {
    switch (args.ConsumeIncludingWhitespace().Id()) {
      case CSSValueMiddle:
        if (!RuntimeEnabledFeatures::webAnimationsAPIEnabled())
          return nullptr;
        position = StepsTimingFunction::StepPosition::MIDDLE;
        break;
      case CSSValueStart:
        position = StepsTimingFunction::StepPosition::START;
        break;
      case CSSValueEnd:
        position = StepsTimingFunction::StepPosition::END;
        break;
      default:
        return nullptr;
    }
  }

  if (!args.AtEnd())
    return nullptr;

  range = range_copy;
  return CSSStepsTimingFunctionValue::Create(steps->GetIntValue(), position);
}

static CSSValue* ConsumeFrames(CSSParserTokenRange& range) {
  DCHECK_EQ(range.Peek().FunctionId(), CSSValueFrames);
  CSSParserTokenRange range_copy = range;
  CSSParserTokenRange args = ConsumeFunction(range_copy);

  CSSPrimitiveValue* frames = ConsumePositiveInteger(args);
  if (!frames)
    return nullptr;

  int frames_int = frames->GetIntValue();
  if (frames_int <= 1)
    return nullptr;

  if (!args.AtEnd())
    return nullptr;

  range = range_copy;
  return CSSFramesTimingFunctionValue::Create(frames_int);
}

static CSSValue* ConsumeCubicBezier(CSSParserTokenRange& range) {
  DCHECK_EQ(range.Peek().FunctionId(), CSSValueCubicBezier);
  CSSParserTokenRange range_copy = range;
  CSSParserTokenRange args = ConsumeFunction(range_copy);

  double x1, y1, x2, y2;
  if (ConsumeNumberRaw(args, x1) && x1 >= 0 && x1 <= 1 &&
      ConsumeCommaIncludingWhitespace(args) && ConsumeNumberRaw(args, y1) &&
      ConsumeCommaIncludingWhitespace(args) && ConsumeNumberRaw(args, x2) &&
      x2 >= 0 && x2 <= 1 && ConsumeCommaIncludingWhitespace(args) &&
      ConsumeNumberRaw(args, y2) && args.AtEnd()) {
    range = range_copy;
    return CSSCubicBezierTimingFunctionValue::Create(x1, y1, x2, y2);
  }

  return nullptr;
}

static CSSValue* ConsumeAnimationTimingFunction(CSSParserTokenRange& range) {
  CSSValueID id = range.Peek().Id();
  if (id == CSSValueEase || id == CSSValueLinear || id == CSSValueEaseIn ||
      id == CSSValueEaseOut || id == CSSValueEaseInOut ||
      id == CSSValueStepStart || id == CSSValueStepEnd ||
      id == CSSValueStepMiddle)
    return ConsumeIdent(range);

  CSSValueID function = range.Peek().FunctionId();
  if (function == CSSValueSteps)
    return ConsumeSteps(range);
  if (function == CSSValueFrames)
    return ConsumeFrames(range);
  if (function == CSSValueCubicBezier)
    return ConsumeCubicBezier(range);
  return nullptr;
}

static CSSValue* ConsumeAnimationValue(CSSPropertyID property,
                                       CSSParserTokenRange& range,
                                       const CSSParserContext* context,
                                       bool use_legacy_parsing) {
  switch (property) {
    case CSSPropertyAnimationDelay:
    case CSSPropertyTransitionDelay:
      return ConsumeTime(range, kValueRangeAll);
    case CSSPropertyAnimationDirection:
      return ConsumeIdent<CSSValueNormal, CSSValueAlternate, CSSValueReverse,
                          CSSValueAlternateReverse>(range);
    case CSSPropertyAnimationDuration:
    case CSSPropertyTransitionDuration:
      return ConsumeTime(range, kValueRangeNonNegative);
    case CSSPropertyAnimationFillMode:
      return ConsumeIdent<CSSValueNone, CSSValueForwards, CSSValueBackwards,
                          CSSValueBoth>(range);
    case CSSPropertyAnimationIterationCount:
      return ConsumeAnimationIterationCount(range);
    case CSSPropertyAnimationName:
      return ConsumeAnimationName(range, context, use_legacy_parsing);
    case CSSPropertyAnimationPlayState:
      return ConsumeIdent<CSSValueRunning, CSSValuePaused>(range);
    case CSSPropertyTransitionProperty:
      return ConsumeTransitionProperty(range);
    case CSSPropertyAnimationTimingFunction:
    case CSSPropertyTransitionTimingFunction:
      return ConsumeAnimationTimingFunction(range);
    default:
      NOTREACHED();
      return nullptr;
  }
}

static bool IsValidAnimationPropertyList(const CSSValueList& value_list) {
  if (value_list.length() < 2)
    return true;
  for (auto& value : value_list) {
    if (value->IsIdentifierValue() &&
        ToCSSIdentifierValue(*value).GetValueID() == CSSValueNone)
      return false;
  }
  return true;
}

bool CSSPropertyParser::ConsumeAnimationShorthand(
    const StylePropertyShorthand& shorthand,
    bool use_legacy_parsing,
    bool important) {
  const unsigned longhand_count = shorthand.length();
  CSSValueList* longhands[8];
  DCHECK_LE(longhand_count, 8u);
  for (size_t i = 0; i < longhand_count; ++i)
    longhands[i] = CSSValueList::CreateCommaSeparated();

  do {
    bool parsed_longhand[8] = {false};
    do {
      bool found_property = false;
      for (size_t i = 0; i < longhand_count; ++i) {
        if (parsed_longhand[i])
          continue;

        if (CSSValue* value =
                ConsumeAnimationValue(shorthand.properties()[i], range_,
                                      context_, use_legacy_parsing)) {
          parsed_longhand[i] = true;
          found_property = true;
          longhands[i]->Append(*value);
          break;
        }
      }
      if (!found_property)
        return false;
    } while (!range_.AtEnd() && range_.Peek().GetType() != kCommaToken);

    // TODO(timloh): This will make invalid longhands, see crbug.com/386459
    for (size_t i = 0; i < longhand_count; ++i) {
      if (!parsed_longhand[i])
        longhands[i]->Append(*CSSInitialValue::Create());
      parsed_longhand[i] = false;
    }
  } while (ConsumeCommaIncludingWhitespace(range_));

  for (size_t i = 0; i < longhand_count; ++i) {
    // TODO(bugsnash): Refactor out the need to check for
    // CSSPropertyTransitionProperty here when this is method implemented in the
    // property APIs
    if (shorthand.properties()[i] == CSSPropertyTransitionProperty &&
        !IsValidAnimationPropertyList(*longhands[i]))
      return false;
  }

  for (size_t i = 0; i < longhand_count; ++i)
    AddProperty(shorthand.properties()[i], shorthand.id(), *longhands[i],
                important);

  return range_.AtEnd();
}

static CSSShadowValue* ParseSingleShadow(CSSParserTokenRange& range,
                                         CSSParserMode css_parser_mode,
                                         bool allow_inset_and_spread) {
  CSSIdentifierValue* style = nullptr;
  CSSValue* color = nullptr;

  if (range.AtEnd())
    return nullptr;
  if (range.Peek().Id() == CSSValueInset) {
    if (!allow_inset_and_spread)
      return nullptr;
    style = ConsumeIdent(range);
  }
  color = ConsumeColor(range, css_parser_mode);

  CSSPrimitiveValue* horizontal_offset =
      ConsumeLength(range, css_parser_mode, kValueRangeAll);
  if (!horizontal_offset)
    return nullptr;

  CSSPrimitiveValue* vertical_offset =
      ConsumeLength(range, css_parser_mode, kValueRangeAll);
  if (!vertical_offset)
    return nullptr;

  CSSPrimitiveValue* blur_radius =
      ConsumeLength(range, css_parser_mode, kValueRangeAll);
  CSSPrimitiveValue* spread_distance = nullptr;
  if (blur_radius) {
    // Blur radius must be non-negative.
    if (blur_radius->GetDoubleValue() < 0)
      return nullptr;
    if (allow_inset_and_spread)
      spread_distance = ConsumeLength(range, css_parser_mode, kValueRangeAll);
  }

  if (!range.AtEnd()) {
    if (!color)
      color = ConsumeColor(range, css_parser_mode);
    if (range.Peek().Id() == CSSValueInset) {
      if (!allow_inset_and_spread || style)
        return nullptr;
      style = ConsumeIdent(range);
    }
  }
  return CSSShadowValue::Create(horizontal_offset, vertical_offset, blur_radius,
                                spread_distance, style, color);
}

static CSSValue* ConsumeShadow(CSSParserTokenRange& range,
                               CSSParserMode css_parser_mode,
                               bool allow_inset_and_spread) {
  if (range.Peek().Id() == CSSValueNone)
    return ConsumeIdent(range);

  CSSValueList* shadow_value_list = CSSValueList::CreateCommaSeparated();
  do {
    if (CSSShadowValue* shadow_value =
            ParseSingleShadow(range, css_parser_mode, allow_inset_and_spread))
      shadow_value_list->Append(*shadow_value);
    else
      return nullptr;
  } while (ConsumeCommaIncludingWhitespace(range));
  return shadow_value_list;
}

static CSSFunctionValue* ConsumeFilterFunction(
    CSSParserTokenRange& range,
    const CSSParserContext* context) {
  CSSValueID filter_type = range.Peek().FunctionId();
  if (filter_type < CSSValueInvert || filter_type > CSSValueDropShadow)
    return nullptr;
  CSSParserTokenRange args = ConsumeFunction(range);
  CSSFunctionValue* filter_value = CSSFunctionValue::Create(filter_type);
  CSSValue* parsed_value = nullptr;

  if (filter_type == CSSValueDropShadow) {
    parsed_value = ParseSingleShadow(args, context->Mode(), false);
  } else {
    if (args.AtEnd()) {
      context->Count(UseCounter::kCSSFilterFunctionNoArguments);
      return filter_value;
    }
    if (filter_type == CSSValueBrightness) {
      // FIXME (crbug.com/397061): Support calc expressions like calc(10% + 0.5)
      parsed_value = ConsumePercent(args, kValueRangeAll);
      if (!parsed_value)
        parsed_value = ConsumeNumber(args, kValueRangeAll);
    } else if (filter_type == CSSValueHueRotate) {
      parsed_value = ConsumeAngle(args);
    } else if (filter_type == CSSValueBlur) {
      parsed_value =
          ConsumeLength(args, kHTMLStandardMode, kValueRangeNonNegative);
    } else {
      // FIXME (crbug.com/397061): Support calc expressions like calc(10% + 0.5)
      parsed_value = ConsumePercent(args, kValueRangeNonNegative);
      if (!parsed_value)
        parsed_value = ConsumeNumber(args, kValueRangeNonNegative);
      if (parsed_value && filter_type != CSSValueSaturate &&
          filter_type != CSSValueContrast) {
        bool is_percentage = ToCSSPrimitiveValue(parsed_value)->IsPercentage();
        double max_allowed = is_percentage ? 100.0 : 1.0;
        if (ToCSSPrimitiveValue(parsed_value)->GetDoubleValue() > max_allowed) {
          parsed_value = CSSPrimitiveValue::Create(
              max_allowed, is_percentage
                               ? CSSPrimitiveValue::UnitType::kPercentage
                               : CSSPrimitiveValue::UnitType::kNumber);
        }
      }
    }
  }
  if (!parsed_value || !args.AtEnd())
    return nullptr;
  filter_value->Append(*parsed_value);
  return filter_value;
}

static CSSValue* ConsumeFilter(CSSParserTokenRange& range,
                               const CSSParserContext* context) {
  if (range.Peek().Id() == CSSValueNone)
    return ConsumeIdent(range);

  CSSValueList* list = CSSValueList::CreateSpaceSeparated();
  do {
    CSSValue* filter_value = ConsumeUrl(range, context);
    if (!filter_value) {
      filter_value = ConsumeFilterFunction(range, context);
      if (!filter_value)
        return nullptr;
    }
    list->Append(*filter_value);
  } while (!range.AtEnd());
  return list;
}

static CSSValue* ConsumeTextDecorationLine(CSSParserTokenRange& range) {
  CSSValueID id = range.Peek().Id();
  if (id == CSSValueNone)
    return ConsumeIdent(range);

  CSSValueList* list = CSSValueList::CreateSpaceSeparated();
  while (true) {
    CSSIdentifierValue* ident =
        ConsumeIdent<CSSValueBlink, CSSValueUnderline, CSSValueOverline,
                     CSSValueLineThrough>(range);
    if (!ident)
      break;
    if (list->HasValue(*ident))
      return nullptr;
    list->Append(*ident);
  }

  if (!list->length())
    return nullptr;
  return list;
}

static CSSValue* ConsumeOffsetRotate(CSSParserTokenRange& range) {
  CSSValue* angle = ConsumeAngle(range);
  CSSValue* keyword = ConsumeIdent<CSSValueAuto, CSSValueReverse>(range);
  if (!angle && !keyword)
    return nullptr;

  if (!angle)
    angle = ConsumeAngle(range);

  CSSValueList* list = CSSValueList::CreateSpaceSeparated();
  if (keyword)
    list->Append(*keyword);
  if (angle)
    list->Append(*angle);
  return list;
}

// offset: <offset-path> <offset-distance> <offset-rotate>
bool CSSPropertyParser::ConsumeOffsetShorthand(bool important) {
  const CSSValue* offset_path =
      CSSPropertyOffsetPathUtils::ConsumeOffsetPath(range_, context_);
  const CSSValue* offset_distance =
      ConsumeLengthOrPercent(range_, context_->Mode(), kValueRangeAll);
  const CSSValue* offset_rotate = ConsumeOffsetRotate(range_);
  if (!offset_path || !offset_distance || !offset_rotate || !range_.AtEnd())
    return false;

  AddProperty(CSSPropertyOffsetPath, CSSPropertyOffset, *offset_path,
              important);
  AddProperty(CSSPropertyOffsetDistance, CSSPropertyOffset, *offset_distance,
              important);
  AddProperty(CSSPropertyOffsetRotate, CSSPropertyOffset, *offset_rotate,
              important);

  return true;
}

static CSSValue* ConsumeNoneOrURI(CSSParserTokenRange& range,
                                  const CSSParserContext* context) {
  if (range.Peek().Id() == CSSValueNone)
    return ConsumeIdent(range);
  return ConsumeUrl(range, context);
}

static CSSValue* ConsumePerspective(CSSParserTokenRange& range,
                                    const CSSParserContext* context,
                                    bool use_legacy_parsing) {
  if (range.Peek().Id() == CSSValueNone)
    return ConsumeIdent(range);
  CSSPrimitiveValue* parsed_value =
      ConsumeLength(range, context->Mode(), kValueRangeAll);
  if (!parsed_value && use_legacy_parsing) {
    double perspective;
    if (!ConsumeNumberRaw(range, perspective))
      return nullptr;
    context->Count(UseCounter::kUnitlessPerspectiveInPerspectiveProperty);
    parsed_value = CSSPrimitiveValue::Create(
        perspective, CSSPrimitiveValue::UnitType::kPixels);
  }
  if (parsed_value &&
      (parsed_value->IsCalculated() || parsed_value->GetDoubleValue() > 0))
    return parsed_value;
  return nullptr;
}

static CSSValue* ConsumeScrollSnapPoints(CSSParserTokenRange& range,
                                         CSSParserMode css_parser_mode) {
  if (range.Peek().Id() == CSSValueNone)
    return ConsumeIdent(range);
  if (range.Peek().FunctionId() == CSSValueRepeat) {
    CSSParserTokenRange args = ConsumeFunction(range);
    CSSPrimitiveValue* parsed_value =
        ConsumeLengthOrPercent(args, css_parser_mode, kValueRangeNonNegative);
    if (args.AtEnd() && parsed_value &&
        (parsed_value->IsCalculated() || parsed_value->GetDoubleValue() > 0)) {
      CSSFunctionValue* result = CSSFunctionValue::Create(CSSValueRepeat);
      result->Append(*parsed_value);
      return result;
    }
  }
  return nullptr;
}

static CSSIdentifierValue* ConsumeBorderImageRepeatKeyword(
    CSSParserTokenRange& range) {
  return ConsumeIdent<CSSValueStretch, CSSValueRepeat, CSSValueSpace,
                      CSSValueRound>(range);
}

static CSSValue* ConsumeBorderImageRepeat(CSSParserTokenRange& range) {
  CSSIdentifierValue* horizontal = ConsumeBorderImageRepeatKeyword(range);
  if (!horizontal)
    return nullptr;
  CSSIdentifierValue* vertical = ConsumeBorderImageRepeatKeyword(range);
  if (!vertical)
    vertical = horizontal;
  return CSSValuePair::Create(horizontal, vertical,
                              CSSValuePair::kDropIdenticalValues);
}

static CSSValue* ConsumeBorderImageSlice(CSSParserTokenRange& range,
                                         bool default_fill) {
  bool fill = ConsumeIdent<CSSValueFill>(range);
  CSSValue* slices[4] = {0};

  for (size_t index = 0; index < 4; ++index) {
    CSSPrimitiveValue* value = ConsumePercent(range, kValueRangeNonNegative);
    if (!value)
      value = ConsumeNumber(range, kValueRangeNonNegative);
    if (!value)
      break;
    slices[index] = value;
  }
  if (!slices[0])
    return nullptr;
  if (ConsumeIdent<CSSValueFill>(range)) {
    if (fill)
      return nullptr;
    fill = true;
  }
  Complete4Sides(slices);
  if (default_fill)
    fill = true;
  return CSSBorderImageSliceValue::Create(
      CSSQuadValue::Create(slices[0], slices[1], slices[2], slices[3],
                           CSSQuadValue::kSerializeAsQuad),
      fill);
}

static CSSValue* ConsumeBorderImageOutset(CSSParserTokenRange& range) {
  CSSValue* outsets[4] = {0};

  CSSValue* value = nullptr;
  for (size_t index = 0; index < 4; ++index) {
    value = ConsumeNumber(range, kValueRangeNonNegative);
    if (!value)
      value = ConsumeLength(range, kHTMLStandardMode, kValueRangeNonNegative);
    if (!value)
      break;
    outsets[index] = value;
  }
  if (!outsets[0])
    return nullptr;
  Complete4Sides(outsets);
  return CSSQuadValue::Create(outsets[0], outsets[1], outsets[2], outsets[3],
                              CSSQuadValue::kSerializeAsQuad);
}

static CSSValue* ConsumeBorderImageWidth(CSSParserTokenRange& range) {
  CSSValue* widths[4] = {0};

  CSSValue* value = nullptr;
  for (size_t index = 0; index < 4; ++index) {
    value = ConsumeNumber(range, kValueRangeNonNegative);
    if (!value)
      value = ConsumeLengthOrPercent(range, kHTMLStandardMode,
                                     kValueRangeNonNegative,
                                     UnitlessQuirk::kForbid);
    if (!value)
      value = ConsumeIdent<CSSValueAuto>(range);
    if (!value)
      break;
    widths[index] = value;
  }
  if (!widths[0])
    return nullptr;
  Complete4Sides(widths);
  return CSSQuadValue::Create(widths[0], widths[1], widths[2], widths[3],
                              CSSQuadValue::kSerializeAsQuad);
}

static bool ConsumeBorderImageComponents(CSSParserTokenRange& range,
                                         const CSSParserContext* context,
                                         CSSValue*& source,
                                         CSSValue*& slice,
                                         CSSValue*& width,
                                         CSSValue*& outset,
                                         CSSValue*& repeat,
                                         bool default_fill) {
  do {
    if (!source) {
      source = ConsumeImageOrNone(range, context);
      if (source)
        continue;
    }
    if (!repeat) {
      repeat = ConsumeBorderImageRepeat(range);
      if (repeat)
        continue;
    }
    if (!slice) {
      slice = ConsumeBorderImageSlice(range, default_fill);
      if (slice) {
        DCHECK(!width);
        DCHECK(!outset);
        if (ConsumeSlashIncludingWhitespace(range)) {
          width = ConsumeBorderImageWidth(range);
          if (ConsumeSlashIncludingWhitespace(range)) {
            outset = ConsumeBorderImageOutset(range);
            if (!outset)
              return false;
          } else if (!width) {
            return false;
          }
        }
      } else {
        return false;
      }
    } else {
      return false;
    }
  } while (!range.AtEnd());
  return true;
}

static CSSValue* ConsumeWebkitBorderImage(CSSParserTokenRange& range,
                                          const CSSParserContext* context) {
  CSSValue* source = nullptr;
  CSSValue* slice = nullptr;
  CSSValue* width = nullptr;
  CSSValue* outset = nullptr;
  CSSValue* repeat = nullptr;
  if (ConsumeBorderImageComponents(range, context, source, slice, width, outset,
                                   repeat, true /* default_fill */))
    return CreateBorderImageValue(source, slice, width, outset, repeat);
  return nullptr;
}

static CSSValue* ConsumeReflect(CSSParserTokenRange& range,
                                const CSSParserContext* context) {
  CSSIdentifierValue* direction =
      ConsumeIdent<CSSValueAbove, CSSValueBelow, CSSValueLeft, CSSValueRight>(
          range);
  if (!direction)
    return nullptr;

  CSSPrimitiveValue* offset = nullptr;
  if (range.AtEnd()) {
    offset = CSSPrimitiveValue::Create(0, CSSPrimitiveValue::UnitType::kPixels);
  } else {
    offset = ConsumeLengthOrPercent(range, context->Mode(), kValueRangeAll,
                                    UnitlessQuirk::kForbid);
    if (!offset)
      return nullptr;
  }

  CSSValue* mask = nullptr;
  if (!range.AtEnd()) {
    mask = ConsumeWebkitBorderImage(range, context);
    if (!mask)
      return nullptr;
  }
  return CSSReflectValue::Create(direction, offset, mask);
}

static CSSValue* ConsumeBackgroundBlendMode(CSSParserTokenRange& range) {
  CSSValueID id = range.Peek().Id();
  if (id == CSSValueNormal || id == CSSValueOverlay ||
      (id >= CSSValueMultiply && id <= CSSValueLuminosity))
    return ConsumeIdent(range);
  return nullptr;
}

static CSSValue* ConsumeBackgroundAttachment(CSSParserTokenRange& range) {
  return ConsumeIdent<CSSValueScroll, CSSValueFixed, CSSValueLocal>(range);
}

static CSSValue* ConsumeBackgroundBox(CSSParserTokenRange& range) {
  return ConsumeIdent<CSSValueBorderBox, CSSValuePaddingBox,
                      CSSValueContentBox>(range);
}

static CSSValue* ConsumeBackgroundComposite(CSSParserTokenRange& range) {
  return ConsumeIdentRange(range, CSSValueClear, CSSValuePlusLighter);
}

static CSSValue* ConsumeMaskSourceType(CSSParserTokenRange& range) {
  DCHECK(RuntimeEnabledFeatures::cssMaskSourceTypeEnabled());
  return ConsumeIdent<CSSValueAuto, CSSValueAlpha, CSSValueLuminance>(range);
}

static CSSValue* ConsumePrefixedBackgroundBox(CSSParserTokenRange& range,
                                              const CSSParserContext* context,
                                              bool allow_text_value) {
  // The values 'border', 'padding' and 'content' are deprecated and do not
  // apply to the version of the property that has the -webkit- prefix removed.
  if (CSSValue* value =
          ConsumeIdentRange(range, CSSValueBorder, CSSValuePaddingBox))
    return value;
  if (allow_text_value && range.Peek().Id() == CSSValueText)
    return ConsumeIdent(range);
  return nullptr;
}

static CSSValue* ConsumeBackgroundSize(CSSParserTokenRange& range,
                                       CSSParserMode css_parser_mode,
                                       bool use_legacy_parsing) {
  if (IdentMatches<CSSValueContain, CSSValueCover>(range.Peek().Id()))
    return ConsumeIdent(range);

  CSSValue* horizontal = ConsumeIdent<CSSValueAuto>(range);
  if (!horizontal)
    horizontal = ConsumeLengthOrPercent(range, css_parser_mode, kValueRangeAll,
                                        UnitlessQuirk::kForbid);

  CSSValue* vertical = nullptr;
  if (!range.AtEnd()) {
    if (range.Peek().Id() == CSSValueAuto)  // `auto' is the default
      range.ConsumeIncludingWhitespace();
    else
      vertical = ConsumeLengthOrPercent(range, css_parser_mode, kValueRangeAll,
                                        UnitlessQuirk::kForbid);
  } else if (use_legacy_parsing) {
    // Legacy syntax: "-webkit-background-size: 10px" is equivalent to
    // "background-size: 10px 10px".
    vertical = horizontal;
  }
  if (!vertical)
    return horizontal;
  return CSSValuePair::Create(horizontal, vertical,
                              CSSValuePair::kKeepIdenticalValues);
}

static CSSValue* ConsumeBackgroundComponent(CSSPropertyID unresolved_property,
                                            CSSParserTokenRange& range,
                                            const CSSParserContext* context) {
  switch (unresolved_property) {
    case CSSPropertyBackgroundClip:
      return ConsumeBackgroundBox(range);
    case CSSPropertyBackgroundBlendMode:
      return ConsumeBackgroundBlendMode(range);
    case CSSPropertyBackgroundAttachment:
      return ConsumeBackgroundAttachment(range);
    case CSSPropertyBackgroundOrigin:
      return ConsumeBackgroundBox(range);
    case CSSPropertyWebkitMaskComposite:
      return ConsumeBackgroundComposite(range);
    case CSSPropertyMaskSourceType:
      return ConsumeMaskSourceType(range);
    case CSSPropertyWebkitBackgroundClip:
    case CSSPropertyWebkitMaskClip:
      return ConsumePrefixedBackgroundBox(range, context,
                                          true /* allow_text_value */);
    case CSSPropertyWebkitBackgroundOrigin:
    case CSSPropertyWebkitMaskOrigin:
      return ConsumePrefixedBackgroundBox(range, context,
                                          false /* allow_text_value */);
    case CSSPropertyBackgroundImage:
    case CSSPropertyWebkitMaskImage:
      return ConsumeImageOrNone(range, context);
    case CSSPropertyBackgroundPositionX:
    case CSSPropertyWebkitMaskPositionX:
      return CSSPropertyPositionUtils::ConsumePositionLonghand<CSSValueLeft,
                                                               CSSValueRight>(
          range, context->Mode());
    case CSSPropertyBackgroundPositionY:
    case CSSPropertyWebkitMaskPositionY:
      return CSSPropertyPositionUtils::ConsumePositionLonghand<CSSValueTop,
                                                               CSSValueBottom>(
          range, context->Mode());
    case CSSPropertyBackgroundSize:
    case CSSPropertyWebkitMaskSize:
      return ConsumeBackgroundSize(range, context->Mode(),
                                   false /* use_legacy_parsing */);
    case CSSPropertyAliasWebkitBackgroundSize:
      return ConsumeBackgroundSize(range, context->Mode(),
                                   true /* use_legacy_parsing */);
    case CSSPropertyBackgroundColor:
      return ConsumeColor(range, context->Mode());
    default:
      break;
  };
  return nullptr;
}

static void AddBackgroundValue(CSSValue*& list, CSSValue* value) {
  if (list) {
    if (!list->IsBaseValueList()) {
      CSSValue* first_value = list;
      list = CSSValueList::CreateCommaSeparated();
      ToCSSValueList(list)->Append(*first_value);
    }
    ToCSSValueList(list)->Append(*value);
  } else {
    // To conserve memory we don't actually wrap a single value in a list.
    list = value;
  }
}

static CSSValue* ConsumeCommaSeparatedBackgroundComponent(
    CSSPropertyID unresolved_property,
    CSSParserTokenRange& range,
    const CSSParserContext* context) {
  CSSValue* result = nullptr;
  do {
    CSSValue* value =
        ConsumeBackgroundComponent(unresolved_property, range, context);
    if (!value)
      return nullptr;
    AddBackgroundValue(result, value);
  } while (ConsumeCommaIncludingWhitespace(range));
  return result;
}

static CSSValue* ConsumeFitContent(CSSParserTokenRange& range,
                                   CSSParserMode css_parser_mode) {
  CSSParserTokenRange range_copy = range;
  CSSParserTokenRange args = ConsumeFunction(range_copy);
  CSSPrimitiveValue* length = ConsumeLengthOrPercent(
      args, css_parser_mode, kValueRangeNonNegative, UnitlessQuirk::kAllow);
  if (!length || !args.AtEnd())
    return nullptr;
  range = range_copy;
  CSSFunctionValue* result = CSSFunctionValue::Create(CSSValueFitContent);
  result->Append(*length);
  return result;
}

static CSSCustomIdentValue* ConsumeCustomIdentForGridLine(
    CSSParserTokenRange& range) {
  if (range.Peek().Id() == CSSValueAuto || range.Peek().Id() == CSSValueSpan)
    return nullptr;
  return ConsumeCustomIdent(range);
}

static CSSValue* ConsumeGridLine(CSSParserTokenRange& range) {
  if (range.Peek().Id() == CSSValueAuto)
    return ConsumeIdent(range);

  CSSIdentifierValue* span_value = nullptr;
  CSSCustomIdentValue* grid_line_name = nullptr;
  CSSPrimitiveValue* numeric_value = ConsumeInteger(range);
  if (numeric_value) {
    grid_line_name = ConsumeCustomIdentForGridLine(range);
    span_value = ConsumeIdent<CSSValueSpan>(range);
  } else {
    span_value = ConsumeIdent<CSSValueSpan>(range);
    if (span_value) {
      numeric_value = ConsumeInteger(range);
      grid_line_name = ConsumeCustomIdentForGridLine(range);
      if (!numeric_value)
        numeric_value = ConsumeInteger(range);
    } else {
      grid_line_name = ConsumeCustomIdentForGridLine(range);
      if (grid_line_name) {
        numeric_value = ConsumeInteger(range);
        span_value = ConsumeIdent<CSSValueSpan>(range);
        if (!span_value && !numeric_value)
          return grid_line_name;
      } else {
        return nullptr;
      }
    }
  }

  if (span_value && !numeric_value && !grid_line_name)
    return nullptr;  // "span" keyword alone is invalid.
  if (span_value && numeric_value && numeric_value->GetIntValue() < 0)
    return nullptr;  // Negative numbers are not allowed for span.
  if (numeric_value && numeric_value->GetIntValue() == 0)
    return nullptr;  // An <integer> value of zero makes the declaration
                     // invalid.

  if (numeric_value) {
    numeric_value = CSSPrimitiveValue::Create(
        clampTo(numeric_value->GetIntValue(), -kGridMaxTracks, kGridMaxTracks),
        CSSPrimitiveValue::UnitType::kInteger);
  }

  CSSValueList* values = CSSValueList::CreateSpaceSeparated();
  if (span_value)
    values->Append(*span_value);
  if (numeric_value)
    values->Append(*numeric_value);
  if (grid_line_name)
    values->Append(*grid_line_name);
  DCHECK(values->length());
  return values;
}

static bool IsGridBreadthFixedSized(const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    CSSValueID value_id = ToCSSIdentifierValue(value).GetValueID();
    return !(value_id == CSSValueMinContent || value_id == CSSValueMaxContent ||
             value_id == CSSValueAuto);
  }

  if (value.IsPrimitiveValue()) {
    return !ToCSSPrimitiveValue(value).IsFlex();
  }

  NOTREACHED();
  return true;
}

static bool IsGridTrackFixedSized(const CSSValue& value) {
  if (value.IsPrimitiveValue() || value.IsIdentifierValue())
    return IsGridBreadthFixedSized(value);

  DCHECK(value.IsFunctionValue());
  auto& function = ToCSSFunctionValue(value);
  if (function.FunctionType() == CSSValueFitContent)
    return false;

  const CSSValue& min_value = function.Item(0);
  const CSSValue& max_value = function.Item(1);
  return IsGridBreadthFixedSized(min_value) ||
         IsGridBreadthFixedSized(max_value);
}

static Vector<String> ParseGridTemplateAreasColumnNames(
    const String& grid_row_names) {
  DCHECK(!grid_row_names.IsEmpty());
  Vector<String> column_names;
  // Using StringImpl to avoid checks and indirection in every call to
  // String::operator[].
  StringImpl& text = *grid_row_names.Impl();

  StringBuilder area_name;
  for (unsigned i = 0; i < text.length(); ++i) {
    if (IsCSSSpace(text[i])) {
      if (!area_name.IsEmpty()) {
        column_names.push_back(area_name.ToString());
        area_name.Clear();
      }
      continue;
    }
    if (text[i] == '.') {
      if (area_name == ".")
        continue;
      if (!area_name.IsEmpty()) {
        column_names.push_back(area_name.ToString());
        area_name.Clear();
      }
    } else {
      if (!IsNameCodePoint(text[i]))
        return Vector<String>();
      if (area_name == ".") {
        column_names.push_back(area_name.ToString());
        area_name.Clear();
      }
    }

    area_name.Append(text[i]);
  }

  if (!area_name.IsEmpty())
    column_names.push_back(area_name.ToString());

  return column_names;
}

static bool ParseGridTemplateAreasRow(const String& grid_row_names,
                                      NamedGridAreaMap& grid_area_map,
                                      const size_t row_count,
                                      size_t& column_count) {
  if (grid_row_names.IsEmpty() || grid_row_names.ContainsOnlyWhitespace())
    return false;

  Vector<String> column_names =
      ParseGridTemplateAreasColumnNames(grid_row_names);
  if (row_count == 0) {
    column_count = column_names.size();
    if (column_count == 0)
      return false;
  } else if (column_count != column_names.size()) {
    // The declaration is invalid if all the rows don't have the number of
    // columns.
    return false;
  }

  for (size_t current_column = 0; current_column < column_count;
       ++current_column) {
    const String& grid_area_name = column_names[current_column];

    // Unamed areas are always valid (we consider them to be 1x1).
    if (grid_area_name == ".")
      continue;

    size_t look_ahead_column = current_column + 1;
    while (look_ahead_column < column_count &&
           column_names[look_ahead_column] == grid_area_name)
      look_ahead_column++;

    NamedGridAreaMap::iterator grid_area_it =
        grid_area_map.find(grid_area_name);
    if (grid_area_it == grid_area_map.end()) {
      grid_area_map.insert(grid_area_name,
                           GridArea(GridSpan::TranslatedDefiniteGridSpan(
                                        row_count, row_count + 1),
                                    GridSpan::TranslatedDefiniteGridSpan(
                                        current_column, look_ahead_column)));
    } else {
      GridArea& grid_area = grid_area_it->value;

      // The following checks test that the grid area is a single filled-in
      // rectangle.
      // 1. The new row is adjacent to the previously parsed row.
      if (row_count != grid_area.rows.EndLine())
        return false;

      // 2. The new area starts at the same position as the previously parsed
      // area.
      if (current_column != grid_area.columns.StartLine())
        return false;

      // 3. The new area ends at the same position as the previously parsed
      // area.
      if (look_ahead_column != grid_area.columns.EndLine())
        return false;

      grid_area.rows = GridSpan::TranslatedDefiniteGridSpan(
          grid_area.rows.StartLine(), grid_area.rows.EndLine() + 1);
    }
    current_column = look_ahead_column - 1;
  }

  return true;
}

static CSSValue* ConsumeGridBreadth(CSSParserTokenRange& range,
                                    CSSParserMode css_parser_mode) {
  const CSSParserToken& token = range.Peek();
  if (IdentMatches<CSSValueMinContent, CSSValueMaxContent, CSSValueAuto>(
          token.Id()))
    return ConsumeIdent(range);
  if (token.GetType() == kDimensionToken &&
      token.GetUnitType() == CSSPrimitiveValue::UnitType::kFraction) {
    if (range.Peek().NumericValue() < 0)
      return nullptr;
    return CSSPrimitiveValue::Create(
        range.ConsumeIncludingWhitespace().NumericValue(),
        CSSPrimitiveValue::UnitType::kFraction);
  }
  return ConsumeLengthOrPercent(range, css_parser_mode, kValueRangeNonNegative,
                                UnitlessQuirk::kAllow);
}

static CSSValue* ConsumeGridTrackSize(CSSParserTokenRange& range,
                                      CSSParserMode css_parser_mode) {
  const CSSParserToken& token = range.Peek();
  if (IdentMatches<CSSValueAuto>(token.Id()))
    return ConsumeIdent(range);

  if (token.FunctionId() == CSSValueMinmax) {
    CSSParserTokenRange range_copy = range;
    CSSParserTokenRange args = ConsumeFunction(range_copy);
    CSSValue* min_track_breadth = ConsumeGridBreadth(args, css_parser_mode);
    if (!min_track_breadth ||
        (min_track_breadth->IsPrimitiveValue() &&
         ToCSSPrimitiveValue(min_track_breadth)->IsFlex()) ||
        !ConsumeCommaIncludingWhitespace(args))
      return nullptr;
    CSSValue* max_track_breadth = ConsumeGridBreadth(args, css_parser_mode);
    if (!max_track_breadth || !args.AtEnd())
      return nullptr;
    range = range_copy;
    CSSFunctionValue* result = CSSFunctionValue::Create(CSSValueMinmax);
    result->Append(*min_track_breadth);
    result->Append(*max_track_breadth);
    return result;
  }

  if (token.FunctionId() == CSSValueFitContent)
    return ConsumeFitContent(range, css_parser_mode);

  return ConsumeGridBreadth(range, css_parser_mode);
}

// Appends to the passed in CSSGridLineNamesValue if any, otherwise creates a
// new one.
static CSSGridLineNamesValue* ConsumeGridLineNames(
    CSSParserTokenRange& range,
    CSSGridLineNamesValue* line_names = nullptr) {
  CSSParserTokenRange range_copy = range;
  if (range_copy.ConsumeIncludingWhitespace().GetType() != kLeftBracketToken)
    return nullptr;
  if (!line_names)
    line_names = CSSGridLineNamesValue::Create();
  while (CSSCustomIdentValue* line_name =
             ConsumeCustomIdentForGridLine(range_copy))
    line_names->Append(*line_name);
  if (range_copy.ConsumeIncludingWhitespace().GetType() != kRightBracketToken)
    return nullptr;
  range = range_copy;
  return line_names;
}

static bool ConsumeGridTrackRepeatFunction(CSSParserTokenRange& range,
                                           CSSParserMode css_parser_mode,
                                           CSSValueList& list,
                                           bool& is_auto_repeat,
                                           bool& all_tracks_are_fixed_sized) {
  CSSParserTokenRange args = ConsumeFunction(range);
  // The number of repetitions for <auto-repeat> is not important at parsing
  // level because it will be computed later, let's set it to 1.
  size_t repetitions = 1;
  is_auto_repeat =
      IdentMatches<CSSValueAutoFill, CSSValueAutoFit>(args.Peek().Id());
  CSSValueList* repeated_values;
  if (is_auto_repeat) {
    repeated_values =
        CSSGridAutoRepeatValue::Create(args.ConsumeIncludingWhitespace().Id());
  } else {
    // TODO(rob.buis): a consumeIntegerRaw would be more efficient here.
    CSSPrimitiveValue* repetition = ConsumePositiveInteger(args);
    if (!repetition)
      return false;
    repetitions =
        clampTo<size_t>(repetition->GetDoubleValue(), 0, kGridMaxTracks);
    repeated_values = CSSValueList::CreateSpaceSeparated();
  }
  if (!ConsumeCommaIncludingWhitespace(args))
    return false;
  CSSGridLineNamesValue* line_names = ConsumeGridLineNames(args);
  if (line_names)
    repeated_values->Append(*line_names);

  size_t number_of_tracks = 0;
  while (!args.AtEnd()) {
    CSSValue* track_size = ConsumeGridTrackSize(args, css_parser_mode);
    if (!track_size)
      return false;
    if (all_tracks_are_fixed_sized)
      all_tracks_are_fixed_sized = IsGridTrackFixedSized(*track_size);
    repeated_values->Append(*track_size);
    ++number_of_tracks;
    line_names = ConsumeGridLineNames(args);
    if (line_names)
      repeated_values->Append(*line_names);
  }
  // We should have found at least one <track-size> or else it is not a valid
  // <track-list>.
  if (!number_of_tracks)
    return false;

  if (is_auto_repeat) {
    list.Append(*repeated_values);
  } else {
    // We clamp the repetitions to a multiple of the repeat() track list's size,
    // while staying below the max grid size.
    repetitions = std::min(repetitions, kGridMaxTracks / number_of_tracks);
    for (size_t i = 0; i < repetitions; ++i) {
      for (size_t j = 0; j < repeated_values->length(); ++j)
        list.Append(repeated_values->Item(j));
    }
  }
  return true;
}

enum TrackListType { kGridTemplate, kGridTemplateNoRepeat, kGridAuto };

static CSSValue* ConsumeGridTrackList(CSSParserTokenRange& range,
                                      CSSParserMode css_parser_mode,
                                      TrackListType track_list_type) {
  bool allow_grid_line_names = track_list_type != kGridAuto;
  CSSValueList* values = CSSValueList::CreateSpaceSeparated();
  CSSGridLineNamesValue* line_names = ConsumeGridLineNames(range);
  if (line_names) {
    if (!allow_grid_line_names)
      return nullptr;
    values->Append(*line_names);
  }

  bool allow_repeat = track_list_type == kGridTemplate;
  bool seen_auto_repeat = false;
  bool all_tracks_are_fixed_sized = true;
  do {
    bool is_auto_repeat;
    if (range.Peek().FunctionId() == CSSValueRepeat) {
      if (!allow_repeat)
        return nullptr;
      if (!ConsumeGridTrackRepeatFunction(range, css_parser_mode, *values,
                                          is_auto_repeat,
                                          all_tracks_are_fixed_sized))
        return nullptr;
      if (is_auto_repeat && seen_auto_repeat)
        return nullptr;
      seen_auto_repeat = seen_auto_repeat || is_auto_repeat;
    } else if (CSSValue* value = ConsumeGridTrackSize(range, css_parser_mode)) {
      if (all_tracks_are_fixed_sized)
        all_tracks_are_fixed_sized = IsGridTrackFixedSized(*value);
      values->Append(*value);
    } else {
      return nullptr;
    }
    if (seen_auto_repeat && !all_tracks_are_fixed_sized)
      return nullptr;
    line_names = ConsumeGridLineNames(range);
    if (line_names) {
      if (!allow_grid_line_names)
        return nullptr;
      values->Append(*line_names);
    }
  } while (!range.AtEnd() && range.Peek().GetType() != kDelimiterToken);
  return values;
}

static CSSValue* ConsumeGridTemplatesRowsOrColumns(
    CSSParserTokenRange& range,
    CSSParserMode css_parser_mode) {
  if (range.Peek().Id() == CSSValueNone)
    return ConsumeIdent(range);
  return ConsumeGridTrackList(range, css_parser_mode, kGridTemplate);
}

static CSSValue* ConsumeGridTemplateAreas(CSSParserTokenRange& range) {
  if (range.Peek().Id() == CSSValueNone)
    return ConsumeIdent(range);

  NamedGridAreaMap grid_area_map;
  size_t row_count = 0;
  size_t column_count = 0;

  while (range.Peek().GetType() == kStringToken) {
    if (!ParseGridTemplateAreasRow(
            range.ConsumeIncludingWhitespace().Value().ToString(),
            grid_area_map, row_count, column_count))
      return nullptr;
    ++row_count;
  }

  if (row_count == 0)
    return nullptr;
  DCHECK(column_count);
  return CSSGridTemplateAreasValue::Create(grid_area_map, row_count,
                                           column_count);
}

static void CountKeywordOnlyPropertyUsage(CSSPropertyID property,
                                          const CSSParserContext* context,
                                          CSSValueID value_id) {
  if (!context->IsUseCounterRecordingEnabled())
    return;
  switch (property) {
    case CSSPropertyWebkitAppearance: {
      UseCounter::Feature feature;
      if (value_id == CSSValueNone) {
        feature = UseCounter::kCSSValueAppearanceNone;
      } else {
        feature = UseCounter::kCSSValueAppearanceNotNone;
        if (value_id == CSSValueButton)
          feature = UseCounter::kCSSValueAppearanceButton;
        else if (value_id == CSSValueCaret)
          feature = UseCounter::kCSSValueAppearanceCaret;
        else if (value_id == CSSValueCheckbox)
          feature = UseCounter::kCSSValueAppearanceCheckbox;
        else if (value_id == CSSValueMenulist)
          feature = UseCounter::kCSSValueAppearanceMenulist;
        else if (value_id == CSSValueMenulistButton)
          feature = UseCounter::kCSSValueAppearanceMenulistButton;
        else if (value_id == CSSValueListbox)
          feature = UseCounter::kCSSValueAppearanceListbox;
        else if (value_id == CSSValueRadio)
          feature = UseCounter::kCSSValueAppearanceRadio;
        else if (value_id == CSSValueSearchfield)
          feature = UseCounter::kCSSValueAppearanceSearchField;
        else if (value_id == CSSValueTextfield)
          feature = UseCounter::kCSSValueAppearanceTextField;
        else
          feature = UseCounter::kCSSValueAppearanceOthers;
      }
      context->Count(feature);
      break;
    }

    case CSSPropertyWebkitUserModify: {
      switch (value_id) {
        case CSSValueReadOnly:
          context->Count(UseCounter::kCSSValueUserModifyReadOnly);
          break;
        case CSSValueReadWrite:
          context->Count(UseCounter::kCSSValueUserModifyReadWrite);
          break;
        case CSSValueReadWritePlaintextOnly:
          context->Count(UseCounter::kCSSValueUserModifyReadWritePlaintextOnly);
          break;
        default:
          NOTREACHED();
      }
      break;
    }

    default:
      break;
  }
}

const CSSValue* CSSPropertyParser::ParseSingleValue(
    CSSPropertyID unresolved_property,
    CSSPropertyID current_shorthand) {
  DCHECK(context_);
  CSSPropertyID property = resolveCSSPropertyID(unresolved_property);
  if (CSSParserFastPaths::IsKeywordPropertyID(property)) {
    if (!CSSParserFastPaths::IsValidKeywordPropertyAndValue(
            property, range_.Peek().Id(), context_->Mode()))
      return nullptr;
    CountKeywordOnlyPropertyUsage(property, context_, range_.Peek().Id());
    return ConsumeIdent(range_);
  }

  // Gets the parsing method for our current property from the property API.
  // If it has been implemented, we call this method, otherwise we manually
  // parse this value in the switch statement below. As we implement APIs for
  // other properties, those properties will be taken out of the switch
  // statement.
  const CSSPropertyDescriptor& css_property_desc =
      CSSPropertyDescriptor::Get(property);
  if (css_property_desc.parseSingleValue) {
    return css_property_desc.parseSingleValue(range_, *context_,
                                              unresolved_property);
  }

  switch (property) {
    case CSSPropertyFontWeight:
      return CSSPropertyFontUtils::ConsumeFontWeight(range_);
    case CSSPropertyMaxWidth:
    case CSSPropertyMaxHeight:
      return CSSPropertyLengthUtils::ConsumeMaxWidthOrHeight(
          range_, *context_, UnitlessQuirk::kAllow);
    case CSSPropertyMinWidth:
    case CSSPropertyMinHeight:
    case CSSPropertyWidth:
    case CSSPropertyHeight:
      return CSSPropertyLengthUtils::ConsumeWidthOrHeight(
          range_, *context_, UnitlessQuirk::kAllow);
    case CSSPropertyInlineSize:
    case CSSPropertyBlockSize:
    case CSSPropertyMinInlineSize:
    case CSSPropertyMinBlockSize:
    case CSSPropertyWebkitMinLogicalWidth:
    case CSSPropertyWebkitMinLogicalHeight:
    case CSSPropertyWebkitLogicalWidth:
    case CSSPropertyWebkitLogicalHeight:
      return CSSPropertyLengthUtils::ConsumeWidthOrHeight(range_, *context_);
    case CSSPropertyScrollSnapDestination:
    case CSSPropertyObjectPosition:
    case CSSPropertyPerspectiveOrigin:
      return ConsumePosition(range_, context_->Mode(), UnitlessQuirk::kForbid);
    case CSSPropertyWebkitHyphenateCharacter:
    case CSSPropertyWebkitLocale:
      return ConsumeLocale(range_);
    case CSSPropertyAnimationDelay:
    case CSSPropertyTransitionDelay:
      return ConsumeCommaSeparatedList(ConsumeTime, range_, kValueRangeAll);
    case CSSPropertyAnimationDirection:
      return ConsumeCommaSeparatedList(
          ConsumeIdent<CSSValueNormal, CSSValueAlternate, CSSValueReverse,
                       CSSValueAlternateReverse>,
          range_);
    case CSSPropertyAnimationDuration:
    case CSSPropertyTransitionDuration:
      return ConsumeCommaSeparatedList(ConsumeTime, range_,
                                       kValueRangeNonNegative);
    case CSSPropertyAnimationFillMode:
      return ConsumeCommaSeparatedList(
          ConsumeIdent<CSSValueNone, CSSValueForwards, CSSValueBackwards,
                       CSSValueBoth>,
          range_);
    case CSSPropertyAnimationIterationCount:
      return ConsumeCommaSeparatedList(ConsumeAnimationIterationCount, range_);
    case CSSPropertyAnimationName:
      return ConsumeCommaSeparatedList(
          ConsumeAnimationName, range_, context_,
          unresolved_property == CSSPropertyAliasWebkitAnimationName);
    case CSSPropertyAnimationPlayState:
      return ConsumeCommaSeparatedList(
          ConsumeIdent<CSSValueRunning, CSSValuePaused>, range_);
    case CSSPropertyTransitionProperty: {
      CSSValueList* list =
          ConsumeCommaSeparatedList(ConsumeTransitionProperty, range_);
      if (!list || !IsValidAnimationPropertyList(*list))
        return nullptr;
      return list;
    }
    case CSSPropertyAnimationTimingFunction:
    case CSSPropertyTransitionTimingFunction:
      return ConsumeCommaSeparatedList(ConsumeAnimationTimingFunction, range_);
    case CSSPropertyGridColumnGap:
    case CSSPropertyGridRowGap:
      return ConsumeLengthOrPercent(range_, context_->Mode(),
                                    kValueRangeNonNegative);
    case CSSPropertyColor:
    case CSSPropertyBackgroundColor:
      return ConsumeColor(range_, context_->Mode(), InQuirksMode());
    case CSSPropertyBorderBottomColor:
    case CSSPropertyBorderLeftColor:
    case CSSPropertyBorderRightColor:
    case CSSPropertyBorderTopColor: {
      bool allow_quirky_colors =
          InQuirksMode() && (current_shorthand == CSSPropertyInvalid ||
                             current_shorthand == CSSPropertyBorderColor);
      return ConsumeColor(range_, context_->Mode(), allow_quirky_colors);
    }
    case CSSPropertyBorderBottomWidth:
    case CSSPropertyBorderLeftWidth:
    case CSSPropertyBorderRightWidth:
    case CSSPropertyBorderTopWidth: {
      bool allow_quirky_lengths =
          InQuirksMode() && (current_shorthand == CSSPropertyInvalid ||
                             current_shorthand == CSSPropertyBorderWidth);
      UnitlessQuirk unitless =
          allow_quirky_lengths ? UnitlessQuirk::kAllow : UnitlessQuirk::kForbid;
      return CSSPropertyWebkitBorderWidthUtils::ConsumeBorderWidth(
          range_, context_->Mode(), unitless);
    }
    case CSSPropertyTextShadow:
      return ConsumeShadow(range_, context_->Mode(), false);
    case CSSPropertyBoxShadow:
      return ConsumeShadow(range_, context_->Mode(), true);
    case CSSPropertyFilter:
    case CSSPropertyBackdropFilter:
      return ConsumeFilter(range_, context_);
    case CSSPropertyTextDecoration:
      DCHECK(!RuntimeEnabledFeatures::css3TextDecorationsEnabled());
    // fallthrough
    case CSSPropertyWebkitTextDecorationsInEffect:
    case CSSPropertyTextDecorationLine:
      return ConsumeTextDecorationLine(range_);
    case CSSPropertyOffsetDistance:
      return ConsumeLengthOrPercent(range_, context_->Mode(), kValueRangeAll);
    case CSSPropertyOffsetRotate:
      return ConsumeOffsetRotate(range_);
    case CSSPropertyWebkitTransformOriginX:
    case CSSPropertyWebkitPerspectiveOriginX:
      return CSSPropertyPositionUtils::ConsumePositionLonghand<CSSValueLeft,
                                                               CSSValueRight>(
          range_, context_->Mode());
    case CSSPropertyWebkitTransformOriginY:
    case CSSPropertyWebkitPerspectiveOriginY:
      return CSSPropertyPositionUtils::ConsumePositionLonghand<CSSValueTop,
                                                               CSSValueBottom>(
          range_, context_->Mode());
    case CSSPropertyMarkerStart:
    case CSSPropertyMarkerMid:
    case CSSPropertyMarkerEnd:
    case CSSPropertyMask:
      return ConsumeNoneOrURI(range_, context_);
    case CSSPropertyFlexGrow:
    case CSSPropertyFlexShrink:
      return ConsumeNumber(range_, kValueRangeNonNegative);
    case CSSPropertyWebkitBoxFlex:
      return ConsumeNumber(range_, kValueRangeAll);
    case CSSPropertyStrokeWidth:
    case CSSPropertyStrokeDashoffset:
    case CSSPropertyCx:
    case CSSPropertyCy:
    case CSSPropertyX:
    case CSSPropertyY:
    case CSSPropertyR:
      return ConsumeLengthOrPercent(range_, kSVGAttributeMode, kValueRangeAll,
                                    UnitlessQuirk::kForbid);
    case CSSPropertyPerspective:
      return ConsumePerspective(
          range_, context_,
          unresolved_property == CSSPropertyAliasWebkitPerspective);
    case CSSPropertyScrollSnapPointsX:
    case CSSPropertyScrollSnapPointsY:
      return ConsumeScrollSnapPoints(range_, context_->Mode());
    case CSSPropertyBorderImageRepeat:
    case CSSPropertyWebkitMaskBoxImageRepeat:
      return ConsumeBorderImageRepeat(range_);
    case CSSPropertyBorderImageSlice:
    case CSSPropertyWebkitMaskBoxImageSlice:
      return ConsumeBorderImageSlice(range_, false /* default_fill */);
    case CSSPropertyBorderImageOutset:
    case CSSPropertyWebkitMaskBoxImageOutset:
      return ConsumeBorderImageOutset(range_);
    case CSSPropertyBorderImageWidth:
    case CSSPropertyWebkitMaskBoxImageWidth:
      return ConsumeBorderImageWidth(range_);
    case CSSPropertyWebkitBorderImage:
      return ConsumeWebkitBorderImage(range_, context_);
    case CSSPropertyWebkitBoxReflect:
      return ConsumeReflect(range_, context_);
    case CSSPropertyBackgroundAttachment:
    case CSSPropertyBackgroundBlendMode:
    case CSSPropertyBackgroundClip:
    case CSSPropertyBackgroundImage:
    case CSSPropertyBackgroundOrigin:
    case CSSPropertyBackgroundPositionX:
    case CSSPropertyBackgroundPositionY:
    case CSSPropertyBackgroundSize:
    case CSSPropertyMaskSourceType:
    case CSSPropertyWebkitBackgroundClip:
    case CSSPropertyWebkitBackgroundOrigin:
    case CSSPropertyWebkitMaskClip:
    case CSSPropertyWebkitMaskComposite:
    case CSSPropertyWebkitMaskImage:
    case CSSPropertyWebkitMaskOrigin:
    case CSSPropertyWebkitMaskPositionX:
    case CSSPropertyWebkitMaskPositionY:
    case CSSPropertyWebkitMaskSize:
      return ConsumeCommaSeparatedBackgroundComponent(unresolved_property,
                                                      range_, context_);
    case CSSPropertyWebkitMaskRepeatX:
    case CSSPropertyWebkitMaskRepeatY:
      return nullptr;
    case CSSPropertyGridColumnEnd:
    case CSSPropertyGridColumnStart:
    case CSSPropertyGridRowEnd:
    case CSSPropertyGridRowStart:
      DCHECK(RuntimeEnabledFeatures::cssGridLayoutEnabled());
      return ConsumeGridLine(range_);
    case CSSPropertyGridAutoColumns:
    case CSSPropertyGridAutoRows:
      DCHECK(RuntimeEnabledFeatures::cssGridLayoutEnabled());
      return ConsumeGridTrackList(range_, context_->Mode(), kGridAuto);
    case CSSPropertyGridTemplateColumns:
    case CSSPropertyGridTemplateRows:
      DCHECK(RuntimeEnabledFeatures::cssGridLayoutEnabled());
      return ConsumeGridTemplatesRowsOrColumns(range_, context_->Mode());
    case CSSPropertyGridTemplateAreas:
      DCHECK(RuntimeEnabledFeatures::cssGridLayoutEnabled());
      return ConsumeGridTemplateAreas(range_);
    default:
      return nullptr;
  }
}

static CSSIdentifierValue* ConsumeFontDisplay(CSSParserTokenRange& range) {
  return ConsumeIdent<CSSValueAuto, CSSValueBlock, CSSValueSwap,
                      CSSValueFallback, CSSValueOptional>(range);
}

static CSSValueList* ConsumeFontFaceUnicodeRange(CSSParserTokenRange& range) {
  CSSValueList* values = CSSValueList::CreateCommaSeparated();

  do {
    const CSSParserToken& token = range.ConsumeIncludingWhitespace();
    if (token.GetType() != kUnicodeRangeToken)
      return nullptr;

    UChar32 start = token.UnicodeRangeStart();
    UChar32 end = token.UnicodeRangeEnd();
    if (start > end)
      return nullptr;
    values->Append(*CSSUnicodeRangeValue::Create(start, end));
  } while (ConsumeCommaIncludingWhitespace(range));

  return values;
}

static CSSValue* ConsumeFontFaceSrcURI(CSSParserTokenRange& range,
                                       const CSSParserContext* context) {
  String url = ConsumeUrlAsStringView(range).ToString();
  if (url.IsNull())
    return nullptr;
  CSSFontFaceSrcValue* uri_value(CSSFontFaceSrcValue::Create(
      url, context->CompleteURL(url), context->GetReferrer(),
      context->ShouldCheckContentSecurityPolicy()));

  if (range.Peek().FunctionId() != CSSValueFormat)
    return uri_value;

  // FIXME: https://drafts.csswg.org/css-fonts says that format() contains a
  // comma-separated list of strings, but CSSFontFaceSrcValue stores only one
  // format. Allowing one format for now.
  CSSParserTokenRange args = ConsumeFunction(range);
  const CSSParserToken& arg = args.ConsumeIncludingWhitespace();
  if ((arg.GetType() != kStringToken) || !args.AtEnd())
    return nullptr;
  uri_value->SetFormat(arg.Value().ToString());
  return uri_value;
}

static CSSValue* ConsumeFontFaceSrcLocal(CSSParserTokenRange& range,
                                         const CSSParserContext* context) {
  CSSParserTokenRange args = ConsumeFunction(range);
  ContentSecurityPolicyDisposition should_check_content_security_policy =
      context->ShouldCheckContentSecurityPolicy();
  if (args.Peek().GetType() == kStringToken) {
    const CSSParserToken& arg = args.ConsumeIncludingWhitespace();
    if (!args.AtEnd())
      return nullptr;
    return CSSFontFaceSrcValue::CreateLocal(
        arg.Value().ToString(), should_check_content_security_policy);
  }
  if (args.Peek().GetType() == kIdentToken) {
    String family_name = CSSPropertyFontUtils::ConcatenateFamilyName(args);
    if (!args.AtEnd())
      return nullptr;
    return CSSFontFaceSrcValue::CreateLocal(
        family_name, should_check_content_security_policy);
  }
  return nullptr;
}

static CSSValueList* ConsumeFontFaceSrc(CSSParserTokenRange& range,
                                        const CSSParserContext* context) {
  CSSValueList* values = CSSValueList::CreateCommaSeparated();

  do {
    const CSSParserToken& token = range.Peek();
    CSSValue* parsed_value = nullptr;
    if (token.FunctionId() == CSSValueLocal)
      parsed_value = ConsumeFontFaceSrcLocal(range, context);
    else
      parsed_value = ConsumeFontFaceSrcURI(range, context);
    if (!parsed_value)
      return nullptr;
    values->Append(*parsed_value);
  } while (ConsumeCommaIncludingWhitespace(range));
  return values;
}

bool CSSPropertyParser::ParseFontFaceDescriptor(CSSPropertyID prop_id) {
  CSSValue* parsed_value = nullptr;
  switch (prop_id) {
    case CSSPropertyFontFamily:
      if (CSSPropertyFontUtils::ConsumeGenericFamily(range_))
        return false;
      parsed_value = CSSPropertyFontUtils::ConsumeFamilyName(range_);
      break;
    case CSSPropertySrc:  // This is a list of urls or local references.
      parsed_value = ConsumeFontFaceSrc(range_, context_);
      break;
    case CSSPropertyUnicodeRange:
      parsed_value = ConsumeFontFaceUnicodeRange(range_);
      break;
    case CSSPropertyFontDisplay:
      parsed_value = ConsumeFontDisplay(range_);
      break;
    case CSSPropertyFontStretch:
    case CSSPropertyFontStyle: {
      CSSValueID id = range_.ConsumeIncludingWhitespace().Id();
      if (!CSSParserFastPaths::IsValidKeywordPropertyAndValue(prop_id, id,
                                                              context_->Mode()))
        return false;
      parsed_value = CSSIdentifierValue::Create(id);
      break;
    }
    case CSSPropertyFontVariant:
      parsed_value = ConsumeFontVariantList(range_);
      break;
    case CSSPropertyFontWeight:
      parsed_value = CSSPropertyFontUtils::ConsumeFontWeight(range_);
      break;
    case CSSPropertyFontFeatureSettings:
      parsed_value = CSSPropertyFontUtils::ConsumeFontFeatureSettings(range_);
      break;
    default:
      break;
  }

  if (!parsed_value || !range_.AtEnd())
    return false;

  AddProperty(prop_id, CSSPropertyInvalid, *parsed_value, false);
  return true;
}

bool CSSPropertyParser::ConsumeSystemFont(bool important) {
  CSSValueID system_font_id = range_.ConsumeIncludingWhitespace().Id();
  DCHECK_GE(system_font_id, CSSValueCaption);
  DCHECK_LE(system_font_id, CSSValueStatusBar);
  if (!range_.AtEnd())
    return false;

  FontStyle font_style = kFontStyleNormal;
  FontWeight font_weight = kFontWeightNormal;
  float font_size = 0;
  AtomicString font_family;
  LayoutTheme::GetTheme().SystemFont(system_font_id, font_style, font_weight,
                                     font_size, font_family);

  AddProperty(CSSPropertyFontStyle, CSSPropertyFont,
              *CSSIdentifierValue::Create(font_style == kFontStyleItalic
                                              ? CSSValueItalic
                                              : CSSValueNormal),
              important);
  AddProperty(CSSPropertyFontWeight, CSSPropertyFont,
              *CSSIdentifierValue::Create(font_weight), important);
  AddProperty(CSSPropertyFontSize, CSSPropertyFont,
              *CSSPrimitiveValue::Create(font_size,
                                         CSSPrimitiveValue::UnitType::kPixels),
              important);
  CSSValueList* font_family_list = CSSValueList::CreateCommaSeparated();
  font_family_list->Append(*CSSFontFamilyValue::Create(font_family));
  AddProperty(CSSPropertyFontFamily, CSSPropertyFont, *font_family_list,
              important);

  AddProperty(CSSPropertyFontStretch, CSSPropertyFont,
              *CSSIdentifierValue::Create(CSSValueNormal), important);
  AddProperty(CSSPropertyFontVariantCaps, CSSPropertyFont,
              *CSSIdentifierValue::Create(CSSValueNormal), important);
  AddProperty(CSSPropertyFontVariantLigatures, CSSPropertyFont,
              *CSSIdentifierValue::Create(CSSValueNormal), important);
  AddProperty(CSSPropertyFontVariantNumeric, CSSPropertyFont,
              *CSSIdentifierValue::Create(CSSValueNormal), important);
  AddProperty(CSSPropertyLineHeight, CSSPropertyFont,
              *CSSIdentifierValue::Create(CSSValueNormal), important);
  return true;
}

bool CSSPropertyParser::ConsumeFont(bool important) {
  // Let's check if there is an inherit or initial somewhere in the shorthand.
  CSSParserTokenRange range = range_;
  while (!range.AtEnd()) {
    CSSValueID id = range.ConsumeIncludingWhitespace().Id();
    if (id == CSSValueInherit || id == CSSValueInitial)
      return false;
  }
  // Optional font-style, font-variant, font-stretch and font-weight.
  CSSIdentifierValue* font_style = nullptr;
  CSSIdentifierValue* font_variant_caps = nullptr;
  CSSIdentifierValue* font_weight = nullptr;
  CSSIdentifierValue* font_stretch = nullptr;
  while (!range_.AtEnd()) {
    CSSValueID id = range_.Peek().Id();
    if (!font_style && CSSParserFastPaths::IsValidKeywordPropertyAndValue(
                           CSSPropertyFontStyle, id, context_->Mode())) {
      font_style = ConsumeIdent(range_);
      continue;
    }
    if (!font_variant_caps &&
        (id == CSSValueNormal || id == CSSValueSmallCaps)) {
      // Font variant in the shorthand is particular, it only accepts normal or
      // small-caps.
      // See https://drafts.csswg.org/css-fonts/#propdef-font
      font_variant_caps = ConsumeFontVariantCSS21(range_);
      if (font_variant_caps)
        continue;
    }
    if (!font_weight) {
      font_weight = CSSPropertyFontUtils::ConsumeFontWeight(range_);
      if (font_weight)
        continue;
    }
    if (!font_stretch && CSSParserFastPaths::IsValidKeywordPropertyAndValue(
                             CSSPropertyFontStretch, id, context_->Mode()))
      font_stretch = ConsumeIdent(range_);
    else
      break;
  }

  if (range_.AtEnd())
    return false;

  AddProperty(
      CSSPropertyFontStyle, CSSPropertyFont,
      font_style ? *font_style : *CSSIdentifierValue::Create(CSSValueNormal),
      important);
  AddProperty(CSSPropertyFontVariantCaps, CSSPropertyFont,
              font_variant_caps ? *font_variant_caps
                                : *CSSIdentifierValue::Create(CSSValueNormal),
              important);
  AddProperty(CSSPropertyFontVariantLigatures, CSSPropertyFont,
              *CSSIdentifierValue::Create(CSSValueNormal), important);
  AddProperty(CSSPropertyFontVariantNumeric, CSSPropertyFont,
              *CSSIdentifierValue::Create(CSSValueNormal), important);

  AddProperty(
      CSSPropertyFontWeight, CSSPropertyFont,
      font_weight ? *font_weight : *CSSIdentifierValue::Create(CSSValueNormal),
      important);
  AddProperty(CSSPropertyFontStretch, CSSPropertyFont,
              font_stretch ? *font_stretch
                           : *CSSIdentifierValue::Create(CSSValueNormal),
              important);

  // Now a font size _must_ come.
  CSSValue* font_size =
      CSSPropertyFontUtils::ConsumeFontSize(range_, context_->Mode());
  if (!font_size || range_.AtEnd())
    return false;

  AddProperty(CSSPropertyFontSize, CSSPropertyFont, *font_size, important);

  if (ConsumeSlashIncludingWhitespace(range_)) {
    CSSValue* line_height =
        CSSPropertyFontUtils::ConsumeLineHeight(range_, context_->Mode());
    if (!line_height)
      return false;
    AddProperty(CSSPropertyLineHeight, CSSPropertyFont, *line_height,
                important);
  } else {
    AddProperty(CSSPropertyLineHeight, CSSPropertyFont,
                *CSSIdentifierValue::Create(CSSValueNormal), important);
  }

  // Font family must come now.
  CSSValue* parsed_family_value =
      CSSPropertyFontUtils::ConsumeFontFamily(range_);
  if (!parsed_family_value)
    return false;

  AddProperty(CSSPropertyFontFamily, CSSPropertyFont, *parsed_family_value,
              important);

  // FIXME: http://www.w3.org/TR/2011/WD-css3-fonts-20110324/#font-prop requires
  // that "font-stretch", "font-size-adjust", and "font-kerning" be reset to
  // their initial values but we don't seem to support them at the moment. They
  // should also be added here once implemented.
  return range_.AtEnd();
}

bool CSSPropertyParser::ConsumeFontVariantShorthand(bool important) {
  if (IdentMatches<CSSValueNormal, CSSValueNone>(range_.Peek().Id())) {
    AddProperty(CSSPropertyFontVariantLigatures, CSSPropertyFontVariant,
                *ConsumeIdent(range_), important);
    AddProperty(CSSPropertyFontVariantCaps, CSSPropertyFontVariant,
                *CSSIdentifierValue::Create(CSSValueNormal), important);
    return range_.AtEnd();
  }

  CSSIdentifierValue* caps_value = nullptr;
  FontVariantLigaturesParser ligatures_parser;
  FontVariantNumericParser numeric_parser;
  do {
    FontVariantLigaturesParser::ParseResult ligatures_parse_result =
        ligatures_parser.ConsumeLigature(range_);
    FontVariantNumericParser::ParseResult numeric_parse_result =
        numeric_parser.ConsumeNumeric(range_);
    if (ligatures_parse_result ==
            FontVariantLigaturesParser::ParseResult::kConsumedValue ||
        numeric_parse_result ==
            FontVariantNumericParser::ParseResult::kConsumedValue)
      continue;

    if (ligatures_parse_result ==
            FontVariantLigaturesParser::ParseResult::kDisallowedValue ||
        numeric_parse_result ==
            FontVariantNumericParser::ParseResult::kDisallowedValue)
      return false;

    CSSValueID id = range_.Peek().Id();
    switch (id) {
      case CSSValueSmallCaps:
      case CSSValueAllSmallCaps:
      case CSSValuePetiteCaps:
      case CSSValueAllPetiteCaps:
      case CSSValueUnicase:
      case CSSValueTitlingCaps:
        // Only one caps value permitted in font-variant grammar.
        if (caps_value)
          return false;
        caps_value = ConsumeIdent(range_);
        break;
      default:
        return false;
    }
  } while (!range_.AtEnd());

  AddProperty(CSSPropertyFontVariantLigatures, CSSPropertyFontVariant,
              *ligatures_parser.FinalizeValue(), important);
  AddProperty(CSSPropertyFontVariantNumeric, CSSPropertyFontVariant,
              *numeric_parser.FinalizeValue(), important);
  AddProperty(
      CSSPropertyFontVariantCaps, CSSPropertyFontVariant,
      caps_value ? *caps_value : *CSSIdentifierValue::Create(CSSValueNormal),
      important);
  return true;
}

bool CSSPropertyParser::ConsumeBorderSpacing(bool important) {
  CSSValue* horizontal_spacing = ConsumeLength(
      range_, context_->Mode(), kValueRangeNonNegative, UnitlessQuirk::kAllow);
  if (!horizontal_spacing)
    return false;
  CSSValue* vertical_spacing = horizontal_spacing;
  if (!range_.AtEnd()) {
    vertical_spacing =
        ConsumeLength(range_, context_->Mode(), kValueRangeNonNegative,
                      UnitlessQuirk::kAllow);
  }
  if (!vertical_spacing || !range_.AtEnd())
    return false;
  AddProperty(CSSPropertyWebkitBorderHorizontalSpacing,
              CSSPropertyBorderSpacing, *horizontal_spacing, important);
  AddProperty(CSSPropertyWebkitBorderVerticalSpacing, CSSPropertyBorderSpacing,
              *vertical_spacing, important);
  return true;
}

static CSSValue* ConsumeSingleViewportDescriptor(
    CSSParserTokenRange& range,
    CSSPropertyID prop_id,
    CSSParserMode css_parser_mode) {
  CSSValueID id = range.Peek().Id();
  switch (prop_id) {
    case CSSPropertyMinWidth:
    case CSSPropertyMaxWidth:
    case CSSPropertyMinHeight:
    case CSSPropertyMaxHeight:
      if (id == CSSValueAuto || id == CSSValueInternalExtendToZoom)
        return ConsumeIdent(range);
      return ConsumeLengthOrPercent(range, css_parser_mode,
                                    kValueRangeNonNegative);
    case CSSPropertyMinZoom:
    case CSSPropertyMaxZoom:
    case CSSPropertyZoom: {
      if (id == CSSValueAuto)
        return ConsumeIdent(range);
      CSSValue* parsed_value = ConsumeNumber(range, kValueRangeNonNegative);
      if (parsed_value)
        return parsed_value;
      return ConsumePercent(range, kValueRangeNonNegative);
    }
    case CSSPropertyUserZoom:
      return ConsumeIdent<CSSValueZoom, CSSValueFixed>(range);
    case CSSPropertyOrientation:
      return ConsumeIdent<CSSValueAuto, CSSValuePortrait, CSSValueLandscape>(
          range);
    default:
      NOTREACHED();
      break;
  }

  NOTREACHED();
  return nullptr;
}

bool CSSPropertyParser::ParseViewportDescriptor(CSSPropertyID prop_id,
                                                bool important) {
  DCHECK(RuntimeEnabledFeatures::cssViewportEnabled() ||
         IsUASheetBehavior(context_->Mode()));

  switch (prop_id) {
    case CSSPropertyWidth: {
      CSSValue* min_width = ConsumeSingleViewportDescriptor(
          range_, CSSPropertyMinWidth, context_->Mode());
      if (!min_width)
        return false;
      CSSValue* max_width = min_width;
      if (!range_.AtEnd()) {
        max_width = ConsumeSingleViewportDescriptor(range_, CSSPropertyMaxWidth,
                                                    context_->Mode());
      }
      if (!max_width || !range_.AtEnd())
        return false;
      AddProperty(CSSPropertyMinWidth, CSSPropertyInvalid, *min_width,
                  important);
      AddProperty(CSSPropertyMaxWidth, CSSPropertyInvalid, *max_width,
                  important);
      return true;
    }
    case CSSPropertyHeight: {
      CSSValue* min_height = ConsumeSingleViewportDescriptor(
          range_, CSSPropertyMinHeight, context_->Mode());
      if (!min_height)
        return false;
      CSSValue* max_height = min_height;
      if (!range_.AtEnd()) {
        max_height = ConsumeSingleViewportDescriptor(
            range_, CSSPropertyMaxHeight, context_->Mode());
      }
      if (!max_height || !range_.AtEnd())
        return false;
      AddProperty(CSSPropertyMinHeight, CSSPropertyInvalid, *min_height,
                  important);
      AddProperty(CSSPropertyMaxHeight, CSSPropertyInvalid, *max_height,
                  important);
      return true;
    }
    case CSSPropertyMinWidth:
    case CSSPropertyMaxWidth:
    case CSSPropertyMinHeight:
    case CSSPropertyMaxHeight:
    case CSSPropertyMinZoom:
    case CSSPropertyMaxZoom:
    case CSSPropertyZoom:
    case CSSPropertyUserZoom:
    case CSSPropertyOrientation: {
      CSSValue* parsed_value =
          ConsumeSingleViewportDescriptor(range_, prop_id, context_->Mode());
      if (!parsed_value || !range_.AtEnd())
        return false;
      AddProperty(prop_id, CSSPropertyInvalid, *parsed_value, important);
      return true;
    }
    default:
      return false;
  }
}

bool CSSPropertyParser::ConsumeColumns(bool important) {
  CSSValue* column_width = nullptr;
  CSSValue* column_count = nullptr;
  if (!CSSPropertyColumnUtils::ConsumeColumnWidthOrCount(range_, column_width,
                                                         column_count))
    return false;
  CSSPropertyColumnUtils::ConsumeColumnWidthOrCount(range_, column_width,
                                                    column_count);
  if (!range_.AtEnd())
    return false;
  if (!column_width)
    column_width = CSSIdentifierValue::Create(CSSValueAuto);
  if (!column_count)
    column_count = CSSIdentifierValue::Create(CSSValueAuto);
  AddProperty(CSSPropertyColumnWidth, CSSPropertyInvalid, *column_width,
              important);
  AddProperty(CSSPropertyColumnCount, CSSPropertyInvalid, *column_count,
              important);
  return true;
}

bool CSSPropertyParser::ConsumeShorthandGreedily(
    const StylePropertyShorthand& shorthand,
    bool important) {
  // Existing shorthands have at most 6 longhands.
  DCHECK_LE(shorthand.length(), 6u);
  const CSSValue* longhands[6] = {nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr};
  const CSSPropertyID* shorthand_properties = shorthand.properties();
  do {
    bool found_longhand = false;
    for (size_t i = 0; !found_longhand && i < shorthand.length(); ++i) {
      if (longhands[i])
        continue;
      longhands[i] = ParseSingleValue(shorthand_properties[i], shorthand.id());
      if (longhands[i])
        found_longhand = true;
    }
    if (!found_longhand)
      return false;
  } while (!range_.AtEnd());

  for (size_t i = 0; i < shorthand.length(); ++i) {
    if (longhands[i])
      AddProperty(shorthand_properties[i], shorthand.id(), *longhands[i],
                  important);
    else
      AddProperty(shorthand_properties[i], shorthand.id(),
                  *CSSInitialValue::Create(), important);
  }
  return true;
}

bool CSSPropertyParser::ConsumeFlex(bool important) {
  static const double kUnsetValue = -1;
  double flex_grow = kUnsetValue;
  double flex_shrink = kUnsetValue;
  CSSValue* flex_basis = nullptr;

  if (range_.Peek().Id() == CSSValueNone) {
    flex_grow = 0;
    flex_shrink = 0;
    flex_basis = CSSIdentifierValue::Create(CSSValueAuto);
    range_.ConsumeIncludingWhitespace();
  } else {
    unsigned index = 0;
    while (!range_.AtEnd() && index++ < 3) {
      double num;
      if (ConsumeNumberRaw(range_, num)) {
        if (num < 0)
          return false;
        if (flex_grow == kUnsetValue)
          flex_grow = num;
        else if (flex_shrink == kUnsetValue)
          flex_shrink = num;
        else if (!num)  // flex only allows a basis of 0 (sans units) if
                        // flex-grow and flex-shrink values have already been
                        // set.
          flex_basis = CSSPrimitiveValue::Create(
              0, CSSPrimitiveValue::UnitType::kPixels);
        else
          return false;
      } else if (!flex_basis) {
        if (range_.Peek().Id() == CSSValueAuto)
          flex_basis = ConsumeIdent(range_);
        if (!flex_basis)
          flex_basis = ConsumeLengthOrPercent(range_, context_->Mode(),
                                              kValueRangeNonNegative);
        if (index == 2 && !range_.AtEnd())
          return false;
      }
    }
    if (index == 0)
      return false;
    if (flex_grow == kUnsetValue)
      flex_grow = 1;
    if (flex_shrink == kUnsetValue)
      flex_shrink = 1;
    if (!flex_basis)
      flex_basis = CSSPrimitiveValue::Create(
          0, CSSPrimitiveValue::UnitType::kPercentage);
  }

  if (!range_.AtEnd())
    return false;
  AddProperty(CSSPropertyFlexGrow, CSSPropertyFlex,
              *CSSPrimitiveValue::Create(clampTo<float>(flex_grow),
                                         CSSPrimitiveValue::UnitType::kNumber),
              important);
  AddProperty(CSSPropertyFlexShrink, CSSPropertyFlex,
              *CSSPrimitiveValue::Create(clampTo<float>(flex_shrink),
                                         CSSPrimitiveValue::UnitType::kNumber),
              important);
  AddProperty(CSSPropertyFlexBasis, CSSPropertyFlex, *flex_basis, important);
  return true;
}

bool CSSPropertyParser::ConsumeBorder(bool important) {
  CSSValue* width = nullptr;
  const CSSValue* style = nullptr;
  CSSValue* color = nullptr;

  while (!width || !style || !color) {
    if (!width) {
      width =
          ConsumeLineWidth(range_, context_->Mode(), UnitlessQuirk::kForbid);
      if (width)
        continue;
    }
    if (!style) {
      style = ParseSingleValue(CSSPropertyBorderLeftStyle, CSSPropertyBorder);
      if (style)
        continue;
    }
    if (!color) {
      color = ConsumeColor(range_, context_->Mode());
      if (color)
        continue;
    }
    break;
  }

  if (!width && !style && !color)
    return false;

  if (!width)
    width = CSSInitialValue::Create();
  if (!style)
    style = CSSInitialValue::Create();
  if (!color)
    color = CSSInitialValue::Create();

  AddExpandedPropertyForValue(CSSPropertyBorderWidth, *width, important);
  AddExpandedPropertyForValue(CSSPropertyBorderStyle, *style, important);
  AddExpandedPropertyForValue(CSSPropertyBorderColor, *color, important);
  AddExpandedPropertyForValue(CSSPropertyBorderImage,
                              *CSSInitialValue::Create(), important);

  return range_.AtEnd();
}

bool CSSPropertyParser::Consume4Values(const StylePropertyShorthand& shorthand,
                                       bool important) {
  DCHECK_EQ(shorthand.length(), 4u);
  const CSSPropertyID* longhands = shorthand.properties();
  const CSSValue* top = ParseSingleValue(longhands[0], shorthand.id());
  if (!top)
    return false;

  const CSSValue* right = ParseSingleValue(longhands[1], shorthand.id());
  const CSSValue* bottom = nullptr;
  const CSSValue* left = nullptr;
  if (right) {
    bottom = ParseSingleValue(longhands[2], shorthand.id());
    if (bottom)
      left = ParseSingleValue(longhands[3], shorthand.id());
  }

  if (!right)
    right = top;
  if (!bottom)
    bottom = top;
  if (!left)
    left = right;

  AddProperty(longhands[0], shorthand.id(), *top, important);
  AddProperty(longhands[1], shorthand.id(), *right, important);
  AddProperty(longhands[2], shorthand.id(), *bottom, important);
  AddProperty(longhands[3], shorthand.id(), *left, important);

  return range_.AtEnd();
}

// TODO(crbug.com/668012): refactor out property specific logic from this method
// and remove CSSPropetyID argument
bool CSSPropertyParser::ConsumeBorderImage(CSSPropertyID property,
                                           bool default_fill,
                                           bool important) {
  CSSValue* source = nullptr;
  CSSValue* slice = nullptr;
  CSSValue* width = nullptr;
  CSSValue* outset = nullptr;
  CSSValue* repeat = nullptr;
  if (ConsumeBorderImageComponents(range_, context_, source, slice, width,
                                   outset, repeat, default_fill)) {
    switch (property) {
      case CSSPropertyWebkitMaskBoxImage:
        AddProperty(CSSPropertyWebkitMaskBoxImageSource,
                    CSSPropertyWebkitMaskBoxImage,
                    source ? *source : *CSSInitialValue::Create(), important);
        AddProperty(CSSPropertyWebkitMaskBoxImageSlice,
                    CSSPropertyWebkitMaskBoxImage,
                    slice ? *slice : *CSSInitialValue::Create(), important);
        AddProperty(CSSPropertyWebkitMaskBoxImageWidth,
                    CSSPropertyWebkitMaskBoxImage,
                    width ? *width : *CSSInitialValue::Create(), important);
        AddProperty(CSSPropertyWebkitMaskBoxImageOutset,
                    CSSPropertyWebkitMaskBoxImage,
                    outset ? *outset : *CSSInitialValue::Create(), important);
        AddProperty(CSSPropertyWebkitMaskBoxImageRepeat,
                    CSSPropertyWebkitMaskBoxImage,
                    repeat ? *repeat : *CSSInitialValue::Create(), important);
        return true;
      case CSSPropertyBorderImage:
        AddProperty(CSSPropertyBorderImageSource, CSSPropertyBorderImage,
                    source ? *source : *CSSInitialValue::Create(), important);
        AddProperty(CSSPropertyBorderImageSlice, CSSPropertyBorderImage,
                    slice ? *slice : *CSSInitialValue::Create(), important);
        AddProperty(CSSPropertyBorderImageWidth, CSSPropertyBorderImage,
                    width ? *width : *CSSInitialValue::Create(), important);
        AddProperty(CSSPropertyBorderImageOutset, CSSPropertyBorderImage,
                    outset ? *outset : *CSSInitialValue::Create(), important);
        AddProperty(CSSPropertyBorderImageRepeat, CSSPropertyBorderImage,
                    repeat ? *repeat : *CSSInitialValue::Create(), important);
        return true;
      default:
        NOTREACHED();
        return false;
    }
  }
  return false;
}

static inline CSSValueID MapFromPageBreakBetween(CSSValueID value) {
  if (value == CSSValueAlways)
    return CSSValuePage;
  if (value == CSSValueAuto || value == CSSValueAvoid ||
      value == CSSValueLeft || value == CSSValueRight)
    return value;
  return CSSValueInvalid;
}

static inline CSSValueID MapFromColumnBreakBetween(CSSValueID value) {
  if (value == CSSValueAlways)
    return CSSValueColumn;
  if (value == CSSValueAuto || value == CSSValueAvoid)
    return value;
  return CSSValueInvalid;
}

static inline CSSValueID MapFromColumnOrPageBreakInside(CSSValueID value) {
  if (value == CSSValueAuto || value == CSSValueAvoid)
    return value;
  return CSSValueInvalid;
}

static inline CSSPropertyID MapFromLegacyBreakProperty(CSSPropertyID property) {
  if (property == CSSPropertyPageBreakAfter ||
      property == CSSPropertyWebkitColumnBreakAfter)
    return CSSPropertyBreakAfter;
  if (property == CSSPropertyPageBreakBefore ||
      property == CSSPropertyWebkitColumnBreakBefore)
    return CSSPropertyBreakBefore;
  DCHECK(property == CSSPropertyPageBreakInside ||
         property == CSSPropertyWebkitColumnBreakInside);
  return CSSPropertyBreakInside;
}

bool CSSPropertyParser::ConsumeLegacyBreakProperty(CSSPropertyID property,
                                                   bool important) {
  // The fragmentation spec says that page-break-(after|before|inside) are to be
  // treated as shorthands for their break-(after|before|inside) counterparts.
  // We'll do the same for the non-standard properties
  // -webkit-column-break-(after|before|inside).
  CSSIdentifierValue* keyword = ConsumeIdent(range_);
  if (!keyword)
    return false;
  if (!range_.AtEnd())
    return false;
  CSSValueID value = keyword->GetValueID();
  switch (property) {
    case CSSPropertyPageBreakAfter:
    case CSSPropertyPageBreakBefore:
      value = MapFromPageBreakBetween(value);
      break;
    case CSSPropertyWebkitColumnBreakAfter:
    case CSSPropertyWebkitColumnBreakBefore:
      value = MapFromColumnBreakBetween(value);
      break;
    case CSSPropertyPageBreakInside:
    case CSSPropertyWebkitColumnBreakInside:
      value = MapFromColumnOrPageBreakInside(value);
      break;
    default:
      NOTREACHED();
  }
  if (value == CSSValueInvalid)
    return false;

  CSSPropertyID generic_break_property = MapFromLegacyBreakProperty(property);
  AddProperty(generic_break_property, property,
              *CSSIdentifierValue::Create(value), important);
  return true;
}

static bool ConsumeBackgroundPosition(CSSParserTokenRange& range,
                                      const CSSParserContext* context,
                                      UnitlessQuirk unitless,
                                      CSSValue*& result_x,
                                      CSSValue*& result_y) {
  do {
    CSSValue* position_x = nullptr;
    CSSValue* position_y = nullptr;
    if (!ConsumePosition(range, context->Mode(), unitless, position_x,
                         position_y))
      return false;
    AddBackgroundValue(result_x, position_x);
    AddBackgroundValue(result_y, position_y);
  } while (ConsumeCommaIncludingWhitespace(range));
  return true;
}

static bool ConsumeRepeatStyleComponent(CSSParserTokenRange& range,
                                        CSSValue*& value1,
                                        CSSValue*& value2,
                                        bool& implicit) {
  if (ConsumeIdent<CSSValueRepeatX>(range)) {
    value1 = CSSIdentifierValue::Create(CSSValueRepeat);
    value2 = CSSIdentifierValue::Create(CSSValueNoRepeat);
    implicit = true;
    return true;
  }
  if (ConsumeIdent<CSSValueRepeatY>(range)) {
    value1 = CSSIdentifierValue::Create(CSSValueNoRepeat);
    value2 = CSSIdentifierValue::Create(CSSValueRepeat);
    implicit = true;
    return true;
  }
  value1 = ConsumeIdent<CSSValueRepeat, CSSValueNoRepeat, CSSValueRound,
                        CSSValueSpace>(range);
  if (!value1)
    return false;

  value2 = ConsumeIdent<CSSValueRepeat, CSSValueNoRepeat, CSSValueRound,
                        CSSValueSpace>(range);
  if (!value2) {
    value2 = value1;
    implicit = true;
  }
  return true;
}

static bool ConsumeRepeatStyle(CSSParserTokenRange& range,
                               CSSValue*& result_x,
                               CSSValue*& result_y,
                               bool& implicit) {
  do {
    CSSValue* repeat_x = nullptr;
    CSSValue* repeat_y = nullptr;
    if (!ConsumeRepeatStyleComponent(range, repeat_x, repeat_y, implicit))
      return false;
    AddBackgroundValue(result_x, repeat_x);
    AddBackgroundValue(result_y, repeat_y);
  } while (ConsumeCommaIncludingWhitespace(range));
  return true;
}

// Note: consumeBackgroundShorthand assumes y properties (for example
// background-position-y) follow the x properties in the shorthand array.
bool CSSPropertyParser::ConsumeBackgroundShorthand(
    const StylePropertyShorthand& shorthand,
    bool important) {
  const unsigned longhand_count = shorthand.length();
  CSSValue* longhands[10] = {0};
  DCHECK_LE(longhand_count, 10u);

  bool implicit = false;
  do {
    bool parsed_longhand[10] = {false};
    CSSValue* origin_value = nullptr;
    do {
      bool found_property = false;
      for (size_t i = 0; i < longhand_count; ++i) {
        if (parsed_longhand[i])
          continue;

        CSSValue* value = nullptr;
        CSSValue* value_y = nullptr;
        CSSPropertyID property = shorthand.properties()[i];
        if (property == CSSPropertyBackgroundRepeatX ||
            property == CSSPropertyWebkitMaskRepeatX) {
          ConsumeRepeatStyleComponent(range_, value, value_y, implicit);
        } else if (property == CSSPropertyBackgroundPositionX ||
                   property == CSSPropertyWebkitMaskPositionX) {
          if (!ConsumePosition(range_, context_->Mode(), UnitlessQuirk::kForbid,
                               value, value_y))
            continue;
        } else if (property == CSSPropertyBackgroundSize ||
                   property == CSSPropertyWebkitMaskSize) {
          if (!ConsumeSlashIncludingWhitespace(range_))
            continue;
          value = ConsumeBackgroundSize(range_, context_->Mode(),
                                        false /* use_legacy_parsing */);
          if (!value ||
              !parsed_longhand[i - 1])  // Position must have been
                                        // parsed in the current layer.
            return false;
        } else if (property == CSSPropertyBackgroundPositionY ||
                   property == CSSPropertyBackgroundRepeatY ||
                   property == CSSPropertyWebkitMaskPositionY ||
                   property == CSSPropertyWebkitMaskRepeatY) {
          continue;
        } else {
          value = ConsumeBackgroundComponent(property, range_, context_);
        }
        if (value) {
          if (property == CSSPropertyBackgroundOrigin ||
              property == CSSPropertyWebkitMaskOrigin)
            origin_value = value;
          parsed_longhand[i] = true;
          found_property = true;
          AddBackgroundValue(longhands[i], value);
          if (value_y) {
            parsed_longhand[i + 1] = true;
            AddBackgroundValue(longhands[i + 1], value_y);
          }
        }
      }
      if (!found_property)
        return false;
    } while (!range_.AtEnd() && range_.Peek().GetType() != kCommaToken);

    // TODO(timloh): This will make invalid longhands, see crbug.com/386459
    for (size_t i = 0; i < longhand_count; ++i) {
      CSSPropertyID property = shorthand.properties()[i];
      if (property == CSSPropertyBackgroundColor && !range_.AtEnd()) {
        if (parsed_longhand[i])
          return false;  // Colors are only allowed in the last layer.
        continue;
      }
      if ((property == CSSPropertyBackgroundClip ||
           property == CSSPropertyWebkitMaskClip) &&
          !parsed_longhand[i] && origin_value) {
        AddBackgroundValue(longhands[i], origin_value);
        continue;
      }
      if (!parsed_longhand[i])
        AddBackgroundValue(longhands[i], CSSInitialValue::Create());
    }
  } while (ConsumeCommaIncludingWhitespace(range_));
  if (!range_.AtEnd())
    return false;

  for (size_t i = 0; i < longhand_count; ++i) {
    CSSPropertyID property = shorthand.properties()[i];
    if (property == CSSPropertyBackgroundSize && longhands[i] &&
        context_->UseLegacyBackgroundSizeShorthandBehavior())
      continue;
    AddProperty(property, shorthand.id(), *longhands[i], important, implicit);
  }
  return true;
}

bool CSSPropertyParser::ConsumeGridItemPositionShorthand(
    CSSPropertyID shorthand_id,
    bool important) {
  DCHECK(RuntimeEnabledFeatures::cssGridLayoutEnabled());
  const StylePropertyShorthand& shorthand = shorthandForProperty(shorthand_id);
  DCHECK_EQ(shorthand.length(), 2u);
  CSSValue* start_value = ConsumeGridLine(range_);
  if (!start_value)
    return false;

  CSSValue* end_value = nullptr;
  if (ConsumeSlashIncludingWhitespace(range_)) {
    end_value = ConsumeGridLine(range_);
    if (!end_value)
      return false;
  } else {
    end_value = start_value->IsCustomIdentValue()
                    ? start_value
                    : CSSIdentifierValue::Create(CSSValueAuto);
  }
  if (!range_.AtEnd())
    return false;
  AddProperty(shorthand.properties()[0], shorthand_id, *start_value, important);
  AddProperty(shorthand.properties()[1], shorthand_id, *end_value, important);
  return true;
}

bool CSSPropertyParser::ConsumeGridAreaShorthand(bool important) {
  DCHECK(RuntimeEnabledFeatures::cssGridLayoutEnabled());
  DCHECK_EQ(gridAreaShorthand().length(), 4u);
  CSSValue* row_start_value = ConsumeGridLine(range_);
  if (!row_start_value)
    return false;
  CSSValue* column_start_value = nullptr;
  CSSValue* row_end_value = nullptr;
  CSSValue* column_end_value = nullptr;
  if (ConsumeSlashIncludingWhitespace(range_)) {
    column_start_value = ConsumeGridLine(range_);
    if (!column_start_value)
      return false;
    if (ConsumeSlashIncludingWhitespace(range_)) {
      row_end_value = ConsumeGridLine(range_);
      if (!row_end_value)
        return false;
      if (ConsumeSlashIncludingWhitespace(range_)) {
        column_end_value = ConsumeGridLine(range_);
        if (!column_end_value)
          return false;
      }
    }
  }
  if (!range_.AtEnd())
    return false;
  if (!column_start_value) {
    column_start_value = row_start_value->IsCustomIdentValue()
                             ? row_start_value
                             : CSSIdentifierValue::Create(CSSValueAuto);
  }
  if (!row_end_value) {
    row_end_value = row_start_value->IsCustomIdentValue()
                        ? row_start_value
                        : CSSIdentifierValue::Create(CSSValueAuto);
  }
  if (!column_end_value) {
    column_end_value = column_start_value->IsCustomIdentValue()
                           ? column_start_value
                           : CSSIdentifierValue::Create(CSSValueAuto);
  }

  AddProperty(CSSPropertyGridRowStart, CSSPropertyGridArea, *row_start_value,
              important);
  AddProperty(CSSPropertyGridColumnStart, CSSPropertyGridArea,
              *column_start_value, important);
  AddProperty(CSSPropertyGridRowEnd, CSSPropertyGridArea, *row_end_value,
              important);
  AddProperty(CSSPropertyGridColumnEnd, CSSPropertyGridArea, *column_end_value,
              important);
  return true;
}

bool CSSPropertyParser::ConsumeGridTemplateRowsAndAreasAndColumns(
    CSSPropertyID shorthand_id,
    bool important) {
  NamedGridAreaMap grid_area_map;
  size_t row_count = 0;
  size_t column_count = 0;
  CSSValueList* template_rows = CSSValueList::CreateSpaceSeparated();

  // Persists between loop iterations so we can use the same value for
  // consecutive <line-names> values
  CSSGridLineNamesValue* line_names = nullptr;

  do {
    // Handle leading <custom-ident>*.
    bool has_previous_line_names = line_names;
    line_names = ConsumeGridLineNames(range_, line_names);
    if (line_names && !has_previous_line_names)
      template_rows->Append(*line_names);

    // Handle a template-area's row.
    if (range_.Peek().GetType() != kStringToken ||
        !ParseGridTemplateAreasRow(
            range_.ConsumeIncludingWhitespace().Value().ToString(),
            grid_area_map, row_count, column_count))
      return false;
    ++row_count;

    // Handle template-rows's track-size.
    CSSValue* value = ConsumeGridTrackSize(range_, context_->Mode());
    if (!value)
      value = CSSIdentifierValue::Create(CSSValueAuto);
    template_rows->Append(*value);

    // This will handle the trailing/leading <custom-ident>* in the grammar.
    line_names = ConsumeGridLineNames(range_);
    if (line_names)
      template_rows->Append(*line_names);
  } while (!range_.AtEnd() && !(range_.Peek().GetType() == kDelimiterToken &&
                                range_.Peek().Delimiter() == '/'));

  CSSValue* columns_value = nullptr;
  if (!range_.AtEnd()) {
    if (!ConsumeSlashIncludingWhitespace(range_))
      return false;
    columns_value =
        ConsumeGridTrackList(range_, context_->Mode(), kGridTemplateNoRepeat);
    if (!columns_value || !range_.AtEnd())
      return false;
  } else {
    columns_value = CSSIdentifierValue::Create(CSSValueNone);
  }
  AddProperty(CSSPropertyGridTemplateRows, shorthand_id, *template_rows,
              important);
  AddProperty(CSSPropertyGridTemplateColumns, shorthand_id, *columns_value,
              important);
  AddProperty(CSSPropertyGridTemplateAreas, shorthand_id,
              *CSSGridTemplateAreasValue::Create(grid_area_map, row_count,
                                                 column_count),
              important);
  return true;
}

bool CSSPropertyParser::ConsumeGridTemplateShorthand(CSSPropertyID shorthand_id,
                                                     bool important) {
  DCHECK(RuntimeEnabledFeatures::cssGridLayoutEnabled());
  DCHECK_EQ(gridTemplateShorthand().length(), 3u);

  CSSParserTokenRange range_copy = range_;
  CSSValue* rows_value = ConsumeIdent<CSSValueNone>(range_);

  // 1- 'none' case.
  if (rows_value && range_.AtEnd()) {
    AddProperty(CSSPropertyGridTemplateRows, shorthand_id,
                *CSSIdentifierValue::Create(CSSValueNone), important);
    AddProperty(CSSPropertyGridTemplateColumns, shorthand_id,
                *CSSIdentifierValue::Create(CSSValueNone), important);
    AddProperty(CSSPropertyGridTemplateAreas, shorthand_id,
                *CSSIdentifierValue::Create(CSSValueNone), important);
    return true;
  }

  // 2- <grid-template-rows> / <grid-template-columns>
  if (!rows_value)
    rows_value = ConsumeGridTrackList(range_, context_->Mode(), kGridTemplate);

  if (rows_value) {
    if (!ConsumeSlashIncludingWhitespace(range_))
      return false;
    CSSValue* columns_value =
        ConsumeGridTemplatesRowsOrColumns(range_, context_->Mode());
    if (!columns_value || !range_.AtEnd())
      return false;

    AddProperty(CSSPropertyGridTemplateRows, shorthand_id, *rows_value,
                important);
    AddProperty(CSSPropertyGridTemplateColumns, shorthand_id, *columns_value,
                important);
    AddProperty(CSSPropertyGridTemplateAreas, shorthand_id,
                *CSSIdentifierValue::Create(CSSValueNone), important);
    return true;
  }

  // 3- [ <line-names>? <string> <track-size>? <line-names>? ]+
  // [ / <track-list> ]?
  range_ = range_copy;
  return ConsumeGridTemplateRowsAndAreasAndColumns(shorthand_id, important);
}

static CSSValueList* ConsumeImplicitAutoFlow(CSSParserTokenRange& range,
                                             const CSSValue& flow_direction) {
  // [ auto-flow && dense? ]
  CSSValue* dense_algorithm = nullptr;
  if ((ConsumeIdent<CSSValueAutoFlow>(range))) {
    dense_algorithm = ConsumeIdent<CSSValueDense>(range);
  } else {
    dense_algorithm = ConsumeIdent<CSSValueDense>(range);
    if (!dense_algorithm)
      return nullptr;
    if (!ConsumeIdent<CSSValueAutoFlow>(range))
      return nullptr;
  }
  CSSValueList* list = CSSValueList::CreateSpaceSeparated();
  list->Append(flow_direction);
  if (dense_algorithm)
    list->Append(*dense_algorithm);
  return list;
}

bool CSSPropertyParser::ConsumeGridShorthand(bool important) {
  DCHECK(RuntimeEnabledFeatures::cssGridLayoutEnabled());
  DCHECK_EQ(shorthandForProperty(CSSPropertyGrid).length(), 8u);

  CSSParserTokenRange range_copy = range_;

  // 1- <grid-template>
  if (ConsumeGridTemplateShorthand(CSSPropertyGrid, important)) {
    // It can only be specified the explicit or the implicit grid properties in
    // a single grid declaration. The sub-properties not specified are set to
    // their initial value, as normal for shorthands.
    AddProperty(CSSPropertyGridAutoFlow, CSSPropertyGrid,
                *CSSInitialValue::Create(), important);
    AddProperty(CSSPropertyGridAutoColumns, CSSPropertyGrid,
                *CSSInitialValue::Create(), important);
    AddProperty(CSSPropertyGridAutoRows, CSSPropertyGrid,
                *CSSInitialValue::Create(), important);
    AddProperty(CSSPropertyGridColumnGap, CSSPropertyGrid,
                *CSSInitialValue::Create(), important);
    AddProperty(CSSPropertyGridRowGap, CSSPropertyGrid,
                *CSSInitialValue::Create(), important);
    return true;
  }

  range_ = range_copy;

  CSSValue* auto_columns_value = nullptr;
  CSSValue* auto_rows_value = nullptr;
  CSSValue* template_rows = nullptr;
  CSSValue* template_columns = nullptr;
  CSSValueList* grid_auto_flow = nullptr;
  if (IdentMatches<CSSValueDense, CSSValueAutoFlow>(range_.Peek().Id())) {
    // 2- [ auto-flow && dense? ] <grid-auto-rows>? / <grid-template-columns>
    grid_auto_flow = ConsumeImplicitAutoFlow(
        range_, *CSSIdentifierValue::Create(CSSValueRow));
    if (!grid_auto_flow)
      return false;
    if (ConsumeSlashIncludingWhitespace(range_)) {
      auto_rows_value = CSSInitialValue::Create();
    } else {
      auto_rows_value =
          ConsumeGridTrackList(range_, context_->Mode(), kGridAuto);
      if (!auto_rows_value)
        return false;
      if (!ConsumeSlashIncludingWhitespace(range_))
        return false;
    }
    if (!(template_columns =
              ConsumeGridTemplatesRowsOrColumns(range_, context_->Mode())))
      return false;
    template_rows = CSSInitialValue::Create();
    auto_columns_value = CSSInitialValue::Create();
  } else {
    // 3- <grid-template-rows> / [ auto-flow && dense? ] <grid-auto-columns>?
    template_rows = ConsumeGridTemplatesRowsOrColumns(range_, context_->Mode());
    if (!template_rows)
      return false;
    if (!ConsumeSlashIncludingWhitespace(range_))
      return false;
    grid_auto_flow = ConsumeImplicitAutoFlow(
        range_, *CSSIdentifierValue::Create(CSSValueColumn));
    if (!grid_auto_flow)
      return false;
    if (range_.AtEnd()) {
      auto_columns_value = CSSInitialValue::Create();
    } else {
      auto_columns_value =
          ConsumeGridTrackList(range_, context_->Mode(), kGridAuto);
      if (!auto_columns_value)
        return false;
    }
    template_columns = CSSInitialValue::Create();
    auto_rows_value = CSSInitialValue::Create();
  }

  if (!range_.AtEnd())
    return false;

  // It can only be specified the explicit or the implicit grid properties in a
  // single grid declaration. The sub-properties not specified are set to their
  // initial value, as normal for shorthands.
  AddProperty(CSSPropertyGridTemplateColumns, CSSPropertyGrid,
              *template_columns, important);
  AddProperty(CSSPropertyGridTemplateRows, CSSPropertyGrid, *template_rows,
              important);
  AddProperty(CSSPropertyGridTemplateAreas, CSSPropertyGrid,
              *CSSInitialValue::Create(), important);
  AddProperty(CSSPropertyGridAutoFlow, CSSPropertyGrid, *grid_auto_flow,
              important);
  AddProperty(CSSPropertyGridAutoColumns, CSSPropertyGrid, *auto_columns_value,
              important);
  AddProperty(CSSPropertyGridAutoRows, CSSPropertyGrid, *auto_rows_value,
              important);
  AddProperty(CSSPropertyGridColumnGap, CSSPropertyGrid,
              *CSSInitialValue::Create(), important);
  AddProperty(CSSPropertyGridRowGap, CSSPropertyGrid,
              *CSSInitialValue::Create(), important);
  return true;
}

bool CSSPropertyParser::ConsumePlaceContentShorthand(bool important) {
  DCHECK(RuntimeEnabledFeatures::cssGridLayoutEnabled());
  DCHECK_EQ(shorthandForProperty(CSSPropertyPlaceContent).length(),
            static_cast<unsigned>(2));

  CSSValue* align_content_value =
      CSSPropertyAlignmentUtils::ConsumeSimplifiedContentPosition(range_);
  if (!align_content_value)
    return false;
  CSSValue* justify_content_value =
      range_.AtEnd()
          ? align_content_value
          : CSSPropertyAlignmentUtils::ConsumeSimplifiedContentPosition(range_);
  if (!justify_content_value)
    return false;
  if (!range_.AtEnd())
    return false;

  AddProperty(CSSPropertyAlignContent, CSSPropertyPlaceContent,
              *align_content_value, important);
  AddProperty(CSSPropertyJustifyContent, CSSPropertyPlaceContent,
              *justify_content_value, important);
  return true;
}

bool CSSPropertyParser::ConsumePlaceItemsShorthand(bool important) {
  DCHECK(RuntimeEnabledFeatures::cssGridLayoutEnabled());
  DCHECK_EQ(shorthandForProperty(CSSPropertyPlaceItems).length(),
            static_cast<unsigned>(2));

  // align-items property does not allow the 'auto' value.
  if (IdentMatches<CSSValueAuto>(range_.Peek().Id()))
    return false;

  CSSValue* align_items_value =
      CSSPropertyAlignmentUtils::ConsumeSimplifiedItemPosition(range_);
  if (!align_items_value)
    return false;
  CSSValue* justify_items_value =
      range_.AtEnd()
          ? align_items_value
          : CSSPropertyAlignmentUtils::ConsumeSimplifiedItemPosition(range_);
  if (!justify_items_value)
    return false;

  if (!range_.AtEnd())
    return false;

  AddProperty(CSSPropertyAlignItems, CSSPropertyPlaceItems, *align_items_value,
              important);
  AddProperty(CSSPropertyJustifyItems, CSSPropertyPlaceItems,
              *justify_items_value, important);
  return true;
}

bool CSSPropertyParser::ConsumePlaceSelfShorthand(bool important) {
  DCHECK(RuntimeEnabledFeatures::cssGridLayoutEnabled());
  DCHECK_EQ(shorthandForProperty(CSSPropertyPlaceSelf).length(),
            static_cast<unsigned>(2));

  CSSValue* align_self_value =
      CSSPropertyAlignmentUtils::ConsumeSimplifiedItemPosition(range_);
  if (!align_self_value)
    return false;
  CSSValue* justify_self_value =
      range_.AtEnd()
          ? align_self_value
          : CSSPropertyAlignmentUtils::ConsumeSimplifiedItemPosition(range_);
  if (!justify_self_value)
    return false;

  if (!range_.AtEnd())
    return false;

  AddProperty(CSSPropertyAlignSelf, CSSPropertyPlaceSelf, *align_self_value,
              important);
  AddProperty(CSSPropertyJustifySelf, CSSPropertyPlaceSelf, *justify_self_value,
              important);
  return true;
}

bool CSSPropertyParser::ParseShorthand(CSSPropertyID unresolved_property,
                                       bool important) {
  CSSPropertyID property = resolveCSSPropertyID(unresolved_property);

  // Gets the parsing method for our current property from the property API.
  // If it has been implemented, we call this method, otherwise we manually
  // parse this value in the switch statement below. As we implement APIs for
  // other properties, those properties will be taken out of the switch
  // statement.
  const CSSPropertyDescriptor& css_property_desc =
      CSSPropertyDescriptor::Get(property);
  if (css_property_desc.parseShorthand)
    return css_property_desc.parseShorthand(important, range_, context_);

  switch (property) {
    case CSSPropertyWebkitMarginCollapse: {
      CSSValueID id = range_.ConsumeIncludingWhitespace().Id();
      if (!CSSParserFastPaths::IsValidKeywordPropertyAndValue(
              CSSPropertyWebkitMarginBeforeCollapse, id, context_->Mode()))
        return false;
      CSSValue* before_collapse = CSSIdentifierValue::Create(id);
      AddProperty(CSSPropertyWebkitMarginBeforeCollapse,
                  CSSPropertyWebkitMarginCollapse, *before_collapse, important);
      if (range_.AtEnd()) {
        AddProperty(CSSPropertyWebkitMarginAfterCollapse,
                    CSSPropertyWebkitMarginCollapse, *before_collapse,
                    important);
        if (range_.AtEnd()) {
          AddProperty(CSSPropertyWebkitMarginAfterCollapse,
                      CSSPropertyWebkitMarginCollapse, *before_collapse,
                      important);
          return true;
        }
        id = range_.ConsumeIncludingWhitespace().Id();
        if (!CSSParserFastPaths::IsValidKeywordPropertyAndValue(
                CSSPropertyWebkitMarginAfterCollapse, id, context_->Mode()))
          return false;
        AddProperty(CSSPropertyWebkitMarginAfterCollapse,
                    CSSPropertyWebkitMarginCollapse, *before_collapse,
                    important);
        return true;
      }
      id = range_.ConsumeIncludingWhitespace().Id();
      if (!CSSParserFastPaths::IsValidKeywordPropertyAndValue(
              CSSPropertyWebkitMarginAfterCollapse, id, context_->Mode()))
        return false;
      AddProperty(CSSPropertyWebkitMarginAfterCollapse,
                  CSSPropertyWebkitMarginCollapse,
                  *CSSIdentifierValue::Create(id), important);
      return true;
    }
    case CSSPropertyOverflow: {
      CSSValueID id = range_.ConsumeIncludingWhitespace().Id();
      if (!CSSParserFastPaths::IsValidKeywordPropertyAndValue(
              CSSPropertyOverflowY, id, context_->Mode()))
        return false;
      if (!range_.AtEnd())
        return false;
      CSSValue* overflow_y_value = CSSIdentifierValue::Create(id);

      CSSValue* overflow_x_value = nullptr;

      // FIXME: -webkit-paged-x or -webkit-paged-y only apply to overflow-y.
      // If
      // this value has been set using the shorthand, then for now overflow-x
      // will default to auto, but once we implement pagination controls, it
      // should default to hidden. If the overflow-y value is anything but
      // paged-x or paged-y, then overflow-x and overflow-y should have the
      // same
      // value.
      if (id == CSSValueWebkitPagedX || id == CSSValueWebkitPagedY)
        overflow_x_value = CSSIdentifierValue::Create(CSSValueAuto);
      else
        overflow_x_value = overflow_y_value;
      AddProperty(CSSPropertyOverflowX, CSSPropertyOverflow, *overflow_x_value,
                  important);
      AddProperty(CSSPropertyOverflowY, CSSPropertyOverflow, *overflow_y_value,
                  important);
      return true;
    }
    case CSSPropertyFont: {
      const CSSParserToken& token = range_.Peek();
      if (token.Id() >= CSSValueCaption && token.Id() <= CSSValueStatusBar)
        return ConsumeSystemFont(important);
      return ConsumeFont(important);
    }
    case CSSPropertyFontVariant:
      return ConsumeFontVariantShorthand(important);
    case CSSPropertyBorderSpacing:
      return ConsumeBorderSpacing(important);
    case CSSPropertyColumns:
      return ConsumeColumns(important);
    case CSSPropertyAnimation:
      return ConsumeAnimationShorthand(
          animationShorthandForParsing(),
          unresolved_property == CSSPropertyAliasWebkitAnimation, important);
    case CSSPropertyTransition:
      return ConsumeAnimationShorthand(transitionShorthandForParsing(), false,
                                       important);
    case CSSPropertyTextDecoration:
      DCHECK(RuntimeEnabledFeatures::css3TextDecorationsEnabled());
      return ConsumeShorthandGreedily(textDecorationShorthand(), important);
    case CSSPropertyMargin:
      return Consume4Values(marginShorthand(), important);
    case CSSPropertyPadding:
      return Consume4Values(paddingShorthand(), important);
    case CSSPropertyOffset:
      return ConsumeOffsetShorthand(important);
    case CSSPropertyWebkitTextEmphasis:
      return ConsumeShorthandGreedily(webkitTextEmphasisShorthand(), important);
    case CSSPropertyOutline:
      return ConsumeShorthandGreedily(outlineShorthand(), important);
    case CSSPropertyWebkitBorderStart:
      return ConsumeShorthandGreedily(webkitBorderStartShorthand(), important);
    case CSSPropertyWebkitBorderEnd:
      return ConsumeShorthandGreedily(webkitBorderEndShorthand(), important);
    case CSSPropertyWebkitBorderBefore:
      return ConsumeShorthandGreedily(webkitBorderBeforeShorthand(), important);
    case CSSPropertyWebkitBorderAfter:
      return ConsumeShorthandGreedily(webkitBorderAfterShorthand(), important);
    case CSSPropertyWebkitTextStroke:
      return ConsumeShorthandGreedily(webkitTextStrokeShorthand(), important);
    case CSSPropertyMarker: {
      const CSSValue* marker = ParseSingleValue(CSSPropertyMarkerStart);
      if (!marker || !range_.AtEnd())
        return false;
      AddProperty(CSSPropertyMarkerStart, CSSPropertyMarker, *marker,
                  important);
      AddProperty(CSSPropertyMarkerMid, CSSPropertyMarker, *marker, important);
      AddProperty(CSSPropertyMarkerEnd, CSSPropertyMarker, *marker, important);
      return true;
    }
    case CSSPropertyFlex:
      return ConsumeFlex(important);
    case CSSPropertyFlexFlow:
      return ConsumeShorthandGreedily(flexFlowShorthand(), important);
    case CSSPropertyColumnRule:
      return ConsumeShorthandGreedily(columnRuleShorthand(), important);
    case CSSPropertyListStyle:
      return ConsumeShorthandGreedily(listStyleShorthand(), important);
    case CSSPropertyBorderRadius: {
      CSSValue* horizontal_radii[4] = {0};
      CSSValue* vertical_radii[4] = {0};
      if (!CSSPropertyShapeUtils::ConsumeRadii(
              horizontal_radii, vertical_radii, range_, context_->Mode(),
              unresolved_property == CSSPropertyAliasWebkitBorderRadius))
        return false;
      AddProperty(CSSPropertyBorderTopLeftRadius, CSSPropertyBorderRadius,
                  *CSSValuePair::Create(horizontal_radii[0], vertical_radii[0],
                                        CSSValuePair::kDropIdenticalValues),
                  important);
      AddProperty(CSSPropertyBorderTopRightRadius, CSSPropertyBorderRadius,
                  *CSSValuePair::Create(horizontal_radii[1], vertical_radii[1],
                                        CSSValuePair::kDropIdenticalValues),
                  important);
      AddProperty(CSSPropertyBorderBottomRightRadius, CSSPropertyBorderRadius,
                  *CSSValuePair::Create(horizontal_radii[2], vertical_radii[2],
                                        CSSValuePair::kDropIdenticalValues),
                  important);
      AddProperty(CSSPropertyBorderBottomLeftRadius, CSSPropertyBorderRadius,
                  *CSSValuePair::Create(horizontal_radii[3], vertical_radii[3],
                                        CSSValuePair::kDropIdenticalValues),
                  important);
      return true;
    }
    case CSSPropertyBorderColor:
      return Consume4Values(borderColorShorthand(), important);
    case CSSPropertyBorderStyle:
      return Consume4Values(borderStyleShorthand(), important);
    case CSSPropertyBorderWidth:
      return Consume4Values(borderWidthShorthand(), important);
    case CSSPropertyBorderTop:
      return ConsumeShorthandGreedily(borderTopShorthand(), important);
    case CSSPropertyBorderRight:
      return ConsumeShorthandGreedily(borderRightShorthand(), important);
    case CSSPropertyBorderBottom:
      return ConsumeShorthandGreedily(borderBottomShorthand(), important);
    case CSSPropertyBorderLeft:
      return ConsumeShorthandGreedily(borderLeftShorthand(), important);
    case CSSPropertyBorder:
      return ConsumeBorder(important);
    case CSSPropertyBorderImage:
      return ConsumeBorderImage(property, false /* default_fill */, important);
    case CSSPropertyWebkitMaskBoxImage:
      return ConsumeBorderImage(property, true /* default_fill */, important);
    case CSSPropertyPageBreakAfter:
    case CSSPropertyPageBreakBefore:
    case CSSPropertyPageBreakInside:
    case CSSPropertyWebkitColumnBreakAfter:
    case CSSPropertyWebkitColumnBreakBefore:
    case CSSPropertyWebkitColumnBreakInside:
      return ConsumeLegacyBreakProperty(property, important);
    case CSSPropertyWebkitMaskPosition:
    case CSSPropertyBackgroundPosition: {
      CSSValue* result_x = nullptr;
      CSSValue* result_y = nullptr;
      if (!ConsumeBackgroundPosition(range_, context_, UnitlessQuirk::kAllow,
                                     result_x, result_y) ||
          !range_.AtEnd())
        return false;
      AddProperty(property == CSSPropertyBackgroundPosition
                      ? CSSPropertyBackgroundPositionX
                      : CSSPropertyWebkitMaskPositionX,
                  property, *result_x, important);
      AddProperty(property == CSSPropertyBackgroundPosition
                      ? CSSPropertyBackgroundPositionY
                      : CSSPropertyWebkitMaskPositionY,
                  property, *result_y, important);
      return true;
    }
    case CSSPropertyBackgroundRepeat:
    case CSSPropertyWebkitMaskRepeat: {
      CSSValue* result_x = nullptr;
      CSSValue* result_y = nullptr;
      bool implicit = false;
      if (!ConsumeRepeatStyle(range_, result_x, result_y, implicit) ||
          !range_.AtEnd())
        return false;
      AddProperty(property == CSSPropertyBackgroundRepeat
                      ? CSSPropertyBackgroundRepeatX
                      : CSSPropertyWebkitMaskRepeatX,
                  property, *result_x, important, implicit);
      AddProperty(property == CSSPropertyBackgroundRepeat
                      ? CSSPropertyBackgroundRepeatY
                      : CSSPropertyWebkitMaskRepeatY,
                  property, *result_y, important, implicit);
      return true;
    }
    case CSSPropertyBackground:
      return ConsumeBackgroundShorthand(backgroundShorthand(), important);
    case CSSPropertyWebkitMask:
      return ConsumeBackgroundShorthand(webkitMaskShorthand(), important);
    case CSSPropertyGridGap: {
      DCHECK(RuntimeEnabledFeatures::cssGridLayoutEnabled());
      DCHECK_EQ(shorthandForProperty(CSSPropertyGridGap).length(), 2u);
      CSSValue* row_gap = ConsumeLengthOrPercent(range_, context_->Mode(),
                                                 kValueRangeNonNegative);
      CSSValue* column_gap = ConsumeLengthOrPercent(range_, context_->Mode(),
                                                    kValueRangeNonNegative);
      if (!row_gap || !range_.AtEnd())
        return false;
      if (!column_gap)
        column_gap = row_gap;
      AddProperty(CSSPropertyGridRowGap, CSSPropertyGridGap, *row_gap,
                  important);
      AddProperty(CSSPropertyGridColumnGap, CSSPropertyGridGap, *column_gap,
                  important);
      return true;
    }
    case CSSPropertyGridColumn:
    case CSSPropertyGridRow:
      return ConsumeGridItemPositionShorthand(property, important);
    case CSSPropertyGridArea:
      return ConsumeGridAreaShorthand(important);
    case CSSPropertyGridTemplate:
      return ConsumeGridTemplateShorthand(CSSPropertyGridTemplate, important);
    case CSSPropertyGrid:
      return ConsumeGridShorthand(important);
    case CSSPropertyPlaceContent:
      return ConsumePlaceContentShorthand(important);
    case CSSPropertyPlaceItems:
      return ConsumePlaceItemsShorthand(important);
    case CSSPropertyPlaceSelf:
      return ConsumePlaceSelfShorthand(important);
    default:
      return false;
  }
}

}  // namespace blink
