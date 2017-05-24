// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/animation/CSSFontVariationSettingsInterpolationType.h"

#include "core/css/CSSFontVariationValue.h"
#include "core/css/CSSValueList.h"

namespace blink {

class CSSFontVariationSettingsNonInterpolableValue
    : public NonInterpolableValue {
 public:
  ~CSSFontVariationSettingsNonInterpolableValue() final {}

  static PassRefPtr<CSSFontVariationSettingsNonInterpolableValue> Create(
      Vector<AtomicString> tags) {
    return AdoptRef(
        new CSSFontVariationSettingsNonInterpolableValue(std::move(tags)));
  }

  const Vector<AtomicString> Tags() const { return tags_; }

  DECLARE_NON_INTERPOLABLE_VALUE_TYPE();

 private:
  CSSFontVariationSettingsNonInterpolableValue(Vector<AtomicString> tags)
      : tags_(std::move(tags)) {
    DCHECK_GT(tags_.size(), 0u);
  }

  const Vector<AtomicString> tags_;
};

DEFINE_NON_INTERPOLABLE_VALUE_TYPE(
    CSSFontVariationSettingsNonInterpolableValue);
DEFINE_NON_INTERPOLABLE_VALUE_TYPE_CASTS(
    CSSFontVariationSettingsNonInterpolableValue);

static const Vector<AtomicString> GetTags(
    const NonInterpolableValue& non_interpolable_value) {
  return ToCSSFontVariationSettingsNonInterpolableValue(non_interpolable_value)
      .Tags();
}

static bool TagsMatch(const NonInterpolableValue& a,
                      const NonInterpolableValue& b) {
  return GetTags(a) == GetTags(b);
}

class UnderlyingTagsChecker : public InterpolationType::ConversionChecker {
 public:
  ~UnderlyingTagsChecker() final {}

  static std::unique_ptr<UnderlyingTagsChecker> Create(
      const Vector<AtomicString>& tags) {
    return WTF::WrapUnique(new UnderlyingTagsChecker(tags));
  }

 private:
  UnderlyingTagsChecker(const Vector<AtomicString>& tags) : tags_(tags) {}

  bool IsValid(const InterpolationEnvironment&,
               const InterpolationValue& underlying) const final {
    return tags_ == GetTags(*underlying.non_interpolable_value);
  }

  const Vector<AtomicString> tags_;
};

class InheritedFontVariationSettingsChecker
    : public InterpolationType::ConversionChecker {
 public:
  ~InheritedFontVariationSettingsChecker() final {}

  static std::unique_ptr<InheritedFontVariationSettingsChecker> Create(
      const FontVariationSettings* settings) {
    return WTF::WrapUnique(new InheritedFontVariationSettingsChecker(settings));
  }

 private:
  InheritedFontVariationSettingsChecker(const FontVariationSettings* settings)
      : settings_(settings) {}

  bool IsValid(const InterpolationEnvironment& environment,
               const InterpolationValue&) const final {
    return DataEquivalent(settings_.Get(), environment.GetState()
                                               .ParentStyle()
                                               ->GetFontDescription()
                                               .VariationSettings());
  }

  RefPtr<const FontVariationSettings> settings_;
};

static InterpolationValue ConvertFontVariationSettings(
    const FontVariationSettings* settings) {
  if (!settings || settings->size() == 0) {
    return nullptr;
  }
  size_t length = settings->size();
  std::unique_ptr<InterpolableList> numbers = InterpolableList::Create(length);
  Vector<AtomicString> tags;
  for (size_t i = 0; i < length; ++i) {
    numbers->Set(i, InterpolableNumber::Create(settings->at(i).Value()));
    tags.push_back(settings->at(i).Tag());
  }
  return InterpolationValue(
      std::move(numbers),
      CSSFontVariationSettingsNonInterpolableValue::Create(std::move(tags)));
}

InterpolationValue
CSSFontVariationSettingsInterpolationType::MaybeConvertNeutral(
    const InterpolationValue& underlying,
    ConversionCheckers& conversion_checkers) const {
  conversion_checkers.push_back(UnderlyingTagsChecker::Create(
      GetTags(*underlying.non_interpolable_value)));
  return InterpolationValue(underlying.interpolable_value->CloneAndZero(),
                            underlying.non_interpolable_value);
}

InterpolationValue
CSSFontVariationSettingsInterpolationType::MaybeConvertInitial(
    const StyleResolverState&,
    ConversionCheckers&) const {
  return ConvertFontVariationSettings(FontBuilder::InitialVariationSettings());
}

InterpolationValue
CSSFontVariationSettingsInterpolationType::MaybeConvertInherit(
    const StyleResolverState& state,
    ConversionCheckers& conversion_checkers) const {
  const FontVariationSettings* inherited =
      state.ParentStyle()->GetFontDescription().VariationSettings();
  conversion_checkers.push_back(
      InheritedFontVariationSettingsChecker::Create(inherited));
  return ConvertFontVariationSettings(inherited);
}

InterpolationValue CSSFontVariationSettingsInterpolationType::MaybeConvertValue(
    const CSSValue& value,
    const StyleResolverState*,
    ConversionCheckers&) const {
  if (!value.IsValueList()) {
    return nullptr;
  }
  const CSSValueList& list = ToCSSValueList(value);
  size_t length = list.length();
  std::unique_ptr<InterpolableList> numbers = InterpolableList::Create(length);
  Vector<AtomicString> tags;
  for (size_t i = 0; i < length; ++i) {
    const CSSFontVariationValue& item = ToCSSFontVariationValue(list.Item(i));
    numbers->Set(i, InterpolableNumber::Create(item.Value()));
    tags.push_back(item.Tag());
  }
  return InterpolationValue(
      std::move(numbers),
      CSSFontVariationSettingsNonInterpolableValue::Create(std::move(tags)));
}

InterpolationValue CSSFontVariationSettingsInterpolationType::
    MaybeConvertStandardPropertyUnderlyingValue(
        const ComputedStyle& style) const {
  return ConvertFontVariationSettings(
      style.GetFontDescription().VariationSettings());
}

PairwiseInterpolationValue
CSSFontVariationSettingsInterpolationType::MaybeMergeSingles(
    InterpolationValue&& start,
    InterpolationValue&& end) const {
  if (TagsMatch(*start.non_interpolable_value, *end.non_interpolable_value)) {
    return PairwiseInterpolationValue(std::move(start.interpolable_value),
                                      std::move(end.interpolable_value),
                                      std::move(start.non_interpolable_value));
  }
  return nullptr;
}

void CSSFontVariationSettingsInterpolationType::Composite(
    UnderlyingValueOwner& underlying_value_owner,
    double underlying_fraction,
    const InterpolationValue& value,
    double interpolation_fraction) const {
  if (TagsMatch(*underlying_value_owner.Value().non_interpolable_value,
                *value.non_interpolable_value)) {
    underlying_value_owner.MutableValue().interpolable_value->ScaleAndAdd(
        underlying_fraction, *value.interpolable_value);
  } else {
    underlying_value_owner.Set(*this, value);
  }
}

void CSSFontVariationSettingsInterpolationType::ApplyStandardPropertyValue(
    const InterpolableValue& interpolable_value,
    const NonInterpolableValue* non_interpolable_value,
    StyleResolverState& state) const {
  const InterpolableList& numbers = ToInterpolableList(interpolable_value);
  const Vector<AtomicString>& tags = GetTags(*non_interpolable_value);
  DCHECK_EQ(numbers.length(), tags.size());

  RefPtr<FontVariationSettings> settings = FontVariationSettings::Create();
  size_t length = numbers.length();
  for (size_t i = 0; i < length; ++i) {
    settings->Append(FontVariationAxis(
        tags[i], ToInterpolableNumber(numbers.Get(i))->Value()));
  }
  state.GetFontBuilder().SetVariationSettings(settings);
}

}  // namespace blink
