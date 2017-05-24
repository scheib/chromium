/*
 * Copyright (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2011 Adobe Systems Incorporated. All rights reserved.
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

#include "core/style/ComputedStyle.h"

#include <algorithm>
#include <memory>
#include "core/animation/css/CSSAnimationData.h"
#include "core/animation/css/CSSTransitionData.h"
#include "core/css/CSSPaintValue.h"
#include "core/css/CSSPrimitiveValue.h"
#include "core/css/CSSPropertyEquality.h"
#include "core/css/resolver/StyleResolver.h"
#include "core/layout/LayoutTheme.h"
#include "core/layout/TextAutosizer.h"
#include "core/style/AppliedTextDecoration.h"
#include "core/style/BorderEdge.h"
#include "core/style/ComputedStyleConstants.h"
#include "core/style/ContentData.h"
#include "core/style/CursorData.h"
#include "core/style/DataEquivalency.h"
#include "core/style/QuotesData.h"
#include "core/style/ShadowList.h"
#include "core/style/StyleImage.h"
#include "core/style/StyleInheritedVariables.h"
#include "core/style/StyleNonInheritedVariables.h"
#include "core/style/StyleRay.h"
#include "platform/LengthFunctions.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/fonts/Font.h"
#include "platform/fonts/FontSelector.h"
#include "platform/geometry/FloatRoundedRect.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/transforms/RotateTransformOperation.h"
#include "platform/transforms/ScaleTransformOperation.h"
#include "platform/transforms/TranslateTransformOperation.h"
#include "platform/wtf/MathExtras.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/SaturatedArithmetic.h"
#include "platform/wtf/SizeAssertions.h"

namespace blink {

struct SameSizeAsBorderValue {
  RGBA32 color_;
  unsigned bitfield_;
};

ASSERT_SIZE(BorderValue, SameSizeAsBorderValue);

// Since different compilers/architectures pack ComputedStyle differently,
// re-create the same structure for an accurate size comparison.
struct SameSizeAsComputedStyle : public RefCounted<SameSizeAsComputedStyle> {
  struct ComputedStyleBase {
    void* data_refs[5];
    unsigned bitfields_[4];
  } base_;

  void* data_refs[2];
  void* own_ptrs[1];
  void* data_ref_svg_style;
};

// If this fails, the packing algorithm in make_computed_style_base.py has
// failed to produce the optimal packed size. To fix, update the algorithm to
// ensure that the buckets are placed so that each takes up at most 1 word.
ASSERT_SIZE(ComputedStyleBase<ComputedStyle>, SameSizeAsComputedStyleBase);

// If this assert fails, it means that size of ComputedStyle has changed. Please
// check that you really *do* what to increase the size of ComputedStyle, then
// update the SameSizeAsComputedStyle struct to match the updated storage of
// ComputedStyle.
ASSERT_SIZE(ComputedStyle, SameSizeAsComputedStyle);

PassRefPtr<ComputedStyle> ComputedStyle::Create() {
  return AdoptRef(new ComputedStyle(InitialStyle()));
}

PassRefPtr<ComputedStyle> ComputedStyle::CreateInitialStyle() {
  return AdoptRef(new ComputedStyle());
}

ComputedStyle& ComputedStyle::MutableInitialStyle() {
  LEAK_SANITIZER_DISABLED_SCOPE;
  DEFINE_STATIC_REF(ComputedStyle, initial_style,
                    (ComputedStyle::CreateInitialStyle()));
  return *initial_style;
}

void ComputedStyle::InvalidateInitialStyle() {
  MutableInitialStyle().SetTapHighlightColor(InitialTapHighlightColor());
}

PassRefPtr<ComputedStyle> ComputedStyle::CreateAnonymousStyleWithDisplay(
    const ComputedStyle& parent_style,
    EDisplay display) {
  RefPtr<ComputedStyle> new_style = ComputedStyle::Create();
  new_style->InheritFrom(parent_style);
  new_style->SetUnicodeBidi(parent_style.GetUnicodeBidi());
  new_style->SetDisplay(display);
  return new_style;
}

PassRefPtr<ComputedStyle> ComputedStyle::Clone(const ComputedStyle& other) {
  return AdoptRef(new ComputedStyle(other));
}

ALWAYS_INLINE ComputedStyle::ComputedStyle()
    : ComputedStyleBase(), RefCounted<ComputedStyle>() {
  rare_non_inherited_data_.Init();
  rare_non_inherited_data_.Access()->deprecated_flexible_box_.Init();
  rare_non_inherited_data_.Access()->flexible_box_.Init();
  rare_non_inherited_data_.Access()->multi_col_.Init();
  rare_non_inherited_data_.Access()->transform_.Init();
  rare_non_inherited_data_.Access()->will_change_.Init();
  rare_non_inherited_data_.Access()->filter_.Init();
  rare_non_inherited_data_.Access()->backdrop_filter_.Init();
  rare_non_inherited_data_.Access()->grid_.Init();
  rare_non_inherited_data_.Access()->grid_item_.Init();
  rare_non_inherited_data_.Access()->scroll_snap_.Init();
  rare_inherited_data_.Init();
  svg_style_.Init();
}

ALWAYS_INLINE ComputedStyle::ComputedStyle(const ComputedStyle& o)
    : ComputedStyleBase(o),
      RefCounted<ComputedStyle>(),
      rare_non_inherited_data_(o.rare_non_inherited_data_),
      rare_inherited_data_(o.rare_inherited_data_),
      svg_style_(o.svg_style_) {}

static StyleRecalcChange DiffPseudoStyles(const ComputedStyle& old_style,
                                          const ComputedStyle& new_style) {
  // If the pseudoStyles have changed, ensure layoutObject triggers setStyle.
  if (!old_style.HasAnyPublicPseudoStyles() &&
      !new_style.HasAnyPublicPseudoStyles())
    return kNoChange;
  for (PseudoId pseudo_id = kFirstPublicPseudoId;
       pseudo_id < kFirstInternalPseudoId;
       pseudo_id = static_cast<PseudoId>(pseudo_id + 1)) {
    if (!old_style.HasPseudoStyle(pseudo_id) &&
        !new_style.HasPseudoStyle(pseudo_id))
      continue;
    const ComputedStyle* new_pseudo_style =
        new_style.GetCachedPseudoStyle(pseudo_id);
    if (!new_pseudo_style)
      return kNoInherit;
    const ComputedStyle* old_pseudo_style =
        old_style.GetCachedPseudoStyle(pseudo_id);
    if (old_pseudo_style && *old_pseudo_style != *new_pseudo_style)
      return kNoInherit;
  }
  return kNoChange;
}

StyleRecalcChange ComputedStyle::StylePropagationDiff(
    const ComputedStyle* old_style,
    const ComputedStyle* new_style) {
  // If the style has changed from display none or to display none, then the
  // layout subtree needs to be reattached
  if ((!old_style && new_style) || (old_style && !new_style))
    return kReattach;

  if (!old_style && !new_style)
    return kNoChange;

  if (old_style->Display() != new_style->Display() ||
      old_style->HasPseudoStyle(kPseudoIdFirstLetter) !=
          new_style->HasPseudoStyle(kPseudoIdFirstLetter) ||
      !old_style->ContentDataEquivalent(new_style) ||
      old_style->HasTextCombine() != new_style->HasTextCombine())
    return kReattach;

  bool independent_equal = old_style->IndependentInheritedEqual(*new_style);
  bool non_independent_equal =
      old_style->NonIndependentInheritedEqual(*new_style);
  if (!independent_equal || !non_independent_equal) {
    if (non_independent_equal && !old_style->HasExplicitlyInheritedProperties())
      return kIndependentInherit;
    return kInherit;
  }

  if (!old_style->LoadingCustomFontsEqual(*new_style) ||
      old_style->AlignItems() != new_style->AlignItems() ||
      old_style->JustifyItems() != new_style->JustifyItems())
    return kInherit;

  if (*old_style == *new_style)
    return DiffPseudoStyles(*old_style, *new_style);

  if (old_style->HasExplicitlyInheritedProperties())
    return kInherit;

  return kNoInherit;
}

void ComputedStyle::PropagateIndependentInheritedProperties(
    const ComputedStyle& parent_style) {
  ComputedStyleBase::PropagateIndependentInheritedProperties(parent_style);
}

StyleSelfAlignmentData ResolvedSelfAlignment(
    const StyleSelfAlignmentData& value,
    ItemPosition normal_value_behavior) {
  // To avoid needing to copy the RareNonInheritedData, we repurpose the 'auto'
  // flag to not just mean 'auto' prior to running the StyleAdjuster but also
  // mean 'normal' after running it.
  if (value.GetPosition() == kItemPositionNormal ||
      value.GetPosition() == kItemPositionAuto)
    return {normal_value_behavior, kOverflowAlignmentDefault};
  return value;
}

StyleSelfAlignmentData ComputedStyle::ResolvedAlignItems(
    ItemPosition normal_value_behaviour) const {
  // We will return the behaviour of 'normal' value if needed, which is specific
  // of each layout model.
  return ResolvedSelfAlignment(AlignItems(), normal_value_behaviour);
}

StyleSelfAlignmentData ComputedStyle::ResolvedAlignSelf(
    ItemPosition normal_value_behaviour,
    const ComputedStyle* parent_style) const {
  // We will return the behaviour of 'normal' value if needed, which is specific
  // of each layout model.
  if (!parent_style || AlignSelfPosition() != kItemPositionAuto)
    return ResolvedSelfAlignment(AlignSelf(), normal_value_behaviour);

  // We shouldn't need to resolve any 'auto' value in post-adjusment
  // ComputedStyle, but some layout models can generate anonymous boxes that may
  // need 'auto' value resolution during layout.
  // The 'auto' keyword computes to the parent's align-items computed value.
  return parent_style->ResolvedAlignItems(normal_value_behaviour);
}

StyleSelfAlignmentData ComputedStyle::ResolvedJustifyItems(
    ItemPosition normal_value_behaviour) const {
  // We will return the behaviour of 'normal' value if needed, which is specific
  // of each layout model.
  return ResolvedSelfAlignment(JustifyItems(), normal_value_behaviour);
}

StyleSelfAlignmentData ComputedStyle::ResolvedJustifySelf(
    ItemPosition normal_value_behaviour,
    const ComputedStyle* parent_style) const {
  // We will return the behaviour of 'normal' value if needed, which is specific
  // of each layout model.
  if (!parent_style || JustifySelfPosition() != kItemPositionAuto)
    return ResolvedSelfAlignment(JustifySelf(), normal_value_behaviour);

  // We shouldn't need to resolve any 'auto' value in post-adjusment
  // ComputedStyle, but some layout models can generate anonymous boxes that may
  // need 'auto' value resolution during layout.
  // The auto keyword computes to the parent's justify-items computed value.
  return parent_style->ResolvedJustifyItems(normal_value_behaviour);
}

static inline ContentPosition ResolvedContentAlignmentPosition(
    const StyleContentAlignmentData& value,
    const StyleContentAlignmentData& normal_value_behavior) {
  return (value.GetPosition() == kContentPositionNormal &&
          value.Distribution() == kContentDistributionDefault)
             ? normal_value_behavior.GetPosition()
             : value.GetPosition();
}

static inline ContentDistributionType ResolvedContentAlignmentDistribution(
    const StyleContentAlignmentData& value,
    const StyleContentAlignmentData& normal_value_behavior) {
  return (value.GetPosition() == kContentPositionNormal &&
          value.Distribution() == kContentDistributionDefault)
             ? normal_value_behavior.Distribution()
             : value.Distribution();
}

ContentPosition ComputedStyle::ResolvedJustifyContentPosition(
    const StyleContentAlignmentData& normal_value_behavior) const {
  return ResolvedContentAlignmentPosition(JustifyContent(),
                                          normal_value_behavior);
}

ContentDistributionType ComputedStyle::ResolvedJustifyContentDistribution(
    const StyleContentAlignmentData& normal_value_behavior) const {
  return ResolvedContentAlignmentDistribution(JustifyContent(),
                                              normal_value_behavior);
}

ContentPosition ComputedStyle::ResolvedAlignContentPosition(
    const StyleContentAlignmentData& normal_value_behavior) const {
  return ResolvedContentAlignmentPosition(AlignContent(),
                                          normal_value_behavior);
}

ContentDistributionType ComputedStyle::ResolvedAlignContentDistribution(
    const StyleContentAlignmentData& normal_value_behavior) const {
  return ResolvedContentAlignmentDistribution(AlignContent(),
                                              normal_value_behavior);
}

void ComputedStyle::InheritFrom(const ComputedStyle& inherit_parent,
                                IsAtShadowBoundary is_at_shadow_boundary) {
  EUserModify current_user_modify = UserModify();

  ComputedStyleBase::InheritFrom(inherit_parent, is_at_shadow_boundary);
  rare_inherited_data_ = inherit_parent.rare_inherited_data_;
  if (svg_style_ != inherit_parent.svg_style_)
    svg_style_.Access()->InheritFrom(inherit_parent.svg_style_.Get());

  if (is_at_shadow_boundary == kAtShadowBoundary) {
    // Even if surrounding content is user-editable, shadow DOM should act as a
    // single unit, and not necessarily be editable
    SetUserModify(current_user_modify);
  }
}

void ComputedStyle::CopyNonInheritedFromCached(const ComputedStyle& other) {
  ComputedStyleBase::CopyNonInheritedFromCached(other);
  rare_non_inherited_data_ = other.rare_non_inherited_data_;

  // The flags are copied one-by-one because they contain
  // bunch of stuff other than real style data.
  // See comments for each skipped flag below.

  // These are not generated in ComputedStyleBase
  SetHasViewportUnits(other.HasViewportUnits());
  SetHasRemUnitsInternal(other.HasRemUnits());

  // Correctly set during selector matching:
  // m_styleType
  // m_pseudoBits

  // Set correctly while computing style for children:
  // m_explicitInheritance

  // unique() styles are not cacheable.
  DCHECK(!other.Unique());

  // styles with non inherited properties that reference variables are not
  // cacheable.
  DCHECK(!other.HasVariableReferenceFromNonInheritedProperty());

  // The following flags are set during matching before we decide that we get a
  // match in the MatchedPropertiesCache which in turn calls this method. The
  // reason why we don't copy these flags is that they're already correctly set
  // and that they may differ between elements which have the same set of
  // matched properties. For instance, given the rule:
  //
  // :-webkit-any(:hover, :focus) { background-color: green }"
  //
  // A hovered element, and a focused element may use the same cached matched
  // properties here, but the affectedBy flags will be set differently based on
  // the matching order of the :-webkit-any components.
  //
  // m_emptyState
  // m_affectedByFocus
  // m_affectedByHover
  // m_affectedByActive
  // m_affectedByDrag
  // m_isLink

  if (svg_style_ != other.svg_style_)
    svg_style_.Access()->CopyNonInheritedFromCached(other.svg_style_.Get());
  DCHECK_EQ(Zoom(), InitialZoom());
}

bool ComputedStyle::operator==(const ComputedStyle& o) const {
  return InheritedEqual(o) && NonInheritedEqual(o);
}

bool ComputedStyle::IsStyleAvailable() const {
  return this != StyleResolver::StyleNotYetAvailable();
}

bool ComputedStyle::HasUniquePseudoStyle() const {
  if (!cached_pseudo_styles_ || StyleType() != kPseudoIdNone)
    return false;

  for (size_t i = 0; i < cached_pseudo_styles_->size(); ++i) {
    const ComputedStyle& pseudo_style = *cached_pseudo_styles_->at(i);
    if (pseudo_style.Unique())
      return true;
  }

  return false;
}

ComputedStyle* ComputedStyle::GetCachedPseudoStyle(PseudoId pid) const {
  if (!cached_pseudo_styles_ || !cached_pseudo_styles_->size())
    return 0;

  if (StyleType() != kPseudoIdNone)
    return 0;

  for (size_t i = 0; i < cached_pseudo_styles_->size(); ++i) {
    ComputedStyle* pseudo_style = cached_pseudo_styles_->at(i).Get();
    if (pseudo_style->StyleType() == pid)
      return pseudo_style;
  }

  return 0;
}

ComputedStyle* ComputedStyle::AddCachedPseudoStyle(
    RefPtr<ComputedStyle> pseudo) {
  if (!pseudo)
    return 0;

  DCHECK_GT(pseudo->StyleType(), kPseudoIdNone);

  ComputedStyle* result = pseudo.Get();

  if (!cached_pseudo_styles_)
    cached_pseudo_styles_ = WTF::WrapUnique(new PseudoStyleCache);

  cached_pseudo_styles_->push_back(std::move(pseudo));

  return result;
}

void ComputedStyle::RemoveCachedPseudoStyle(PseudoId pid) {
  if (!cached_pseudo_styles_)
    return;
  for (size_t i = 0; i < cached_pseudo_styles_->size(); ++i) {
    ComputedStyle* pseudo_style = cached_pseudo_styles_->at(i).Get();
    if (pseudo_style->StyleType() == pid) {
      cached_pseudo_styles_->erase(i);
      return;
    }
  }
}

bool ComputedStyle::InheritedEqual(const ComputedStyle& other) const {
  return IndependentInheritedEqual(other) &&
         NonIndependentInheritedEqual(other);
}

bool ComputedStyle::IndependentInheritedEqual(
    const ComputedStyle& other) const {
  return ComputedStyleBase::IndependentInheritedEqual(other);
}

bool ComputedStyle::NonIndependentInheritedEqual(
    const ComputedStyle& other) const {
  return ComputedStyleBase::NonIndependentInheritedEqual(other) &&
         svg_style_->InheritedEqual(*other.svg_style_) &&
         rare_inherited_data_ == other.rare_inherited_data_;
}

bool ComputedStyle::LoadingCustomFontsEqual(const ComputedStyle& other) const {
  return GetFont().LoadingCustomFonts() == other.GetFont().LoadingCustomFonts();
}

bool ComputedStyle::NonInheritedEqual(const ComputedStyle& other) const {
  // compare everything except the pseudoStyle pointer
  return ComputedStyleBase::NonInheritedEqual(other) &&
         rare_non_inherited_data_ == other.rare_non_inherited_data_ &&
         svg_style_->NonInheritedEqual(*other.svg_style_);
}

bool ComputedStyle::InheritedDataShared(const ComputedStyle& other) const {
  // This is a fast check that only looks if the data structures are shared.
  return ComputedStyleBase::InheritedDataShared(other) &&
         svg_style_.Get() == other.svg_style_.Get() &&
         rare_inherited_data_.Get() == other.rare_inherited_data_.Get();
}

static bool DependenceOnContentHeightHasChanged(const ComputedStyle& a,
                                                const ComputedStyle& b) {
  // If top or bottom become auto/non-auto then it means we either have to solve
  // height based on the content or stop doing so
  // (http://www.w3.org/TR/CSS2/visudet.html#abs-non-replaced-height)
  // - either way requires a layout.
  return a.LogicalTop().IsAuto() != b.LogicalTop().IsAuto() ||
         a.LogicalBottom().IsAuto() != b.LogicalBottom().IsAuto();
}

StyleDifference ComputedStyle::VisualInvalidationDiff(
    const ComputedStyle& other) const {
  // Note, we use .Get() on each DataRef below because DataRef::operator== will
  // do a deep compare, which is duplicate work when we're going to compare each
  // property inside this function anyway.

  StyleDifference diff;
  if (svg_style_.Get() != other.svg_style_.Get())
    diff = svg_style_->Diff(other.svg_style_.Get());

  if ((!diff.NeedsFullLayout() || !diff.NeedsFullPaintInvalidation()) &&
      DiffNeedsFullLayoutAndPaintInvalidation(other)) {
    diff.SetNeedsFullLayout();
    diff.SetNeedsPaintInvalidationObject();
  }

  if (!diff.NeedsFullLayout() && DiffNeedsFullLayout(other))
    diff.SetNeedsFullLayout();

  if (!diff.NeedsFullLayout() && !MarginEqual(other)) {
    // Relative-positioned elements collapse their margins so need a full
    // layout.
    if (HasOutOfFlowPosition())
      diff.SetNeedsPositionedMovementLayout();
    else
      diff.SetNeedsFullLayout();
  }

  if (!diff.NeedsFullLayout() && GetPosition() != EPosition::kStatic &&
      !OffsetEqual(other)) {
    // Optimize for the case where a positioned layer is moving but not changing
    // size.
    if (DependenceOnContentHeightHasChanged(*this, other))
      diff.SetNeedsFullLayout();
    else
      diff.SetNeedsPositionedMovementLayout();
  }

  if (DiffNeedsPaintInvalidationSubtree(other))
    diff.SetNeedsPaintInvalidationSubtree();
  else if (DiffNeedsPaintInvalidationObject(other))
    diff.SetNeedsPaintInvalidationObject();

  if (DiffNeedsVisualRectUpdate(other))
    diff.SetNeedsVisualRectUpdate();

  UpdatePropertySpecificDifferences(other, diff);

  // The following condition needs to be at last, because it may depend on
  // conditions in diff computed above.
  if (ScrollAnchorDisablingPropertyChanged(other, diff))
    diff.SetScrollAnchorDisablingPropertyChanged();

  // Cursors are not checked, since they will be set appropriately in response
  // to mouse events, so they don't need to cause any paint invalidation or
  // layout.

  // Animations don't need to be checked either. We always set the new style on
  // the layoutObject, so we will get a chance to fire off the resulting
  // transition properly.

  return diff;
}

bool ComputedStyle::ScrollAnchorDisablingPropertyChanged(
    const ComputedStyle& other,
    const StyleDifference& diff) const {
  if (GetPosition() != other.GetPosition())
    return true;

  if (ComputedStyleBase::ScrollAnchorDisablingPropertyChanged(other))
    return true;

  if (diff.TransformChanged())
    return true;

  return false;
}

bool ComputedStyle::DiffNeedsFullLayoutAndPaintInvalidation(
    const ComputedStyle& other) const {
  // FIXME: Not all cases in this method need both full layout and paint
  // invalidation.
  // Should move cases into DiffNeedsFullLayout() if
  // - don't need paint invalidation at all;
  // - or the layoutObject knows how to exactly invalidate paints caused by the
  //   layout change instead of forced full paint invalidation.

  if (ComputedStyleBase::DiffNeedsFullLayoutAndPaintInvalidation(other))
    return true;

  if (rare_non_inherited_data_.Get() != other.rare_non_inherited_data_.Get()) {
    if (rare_non_inherited_data_->appearance_ !=
            other.rare_non_inherited_data_->appearance_ ||
        rare_non_inherited_data_->margin_before_collapse !=
            other.rare_non_inherited_data_->margin_before_collapse ||
        rare_non_inherited_data_->margin_after_collapse !=
            other.rare_non_inherited_data_->margin_after_collapse ||
        rare_non_inherited_data_->line_clamp !=
            other.rare_non_inherited_data_->line_clamp ||
        rare_non_inherited_data_->text_overflow !=
            other.rare_non_inherited_data_->text_overflow ||
        rare_non_inherited_data_->shape_margin_ !=
            other.rare_non_inherited_data_->shape_margin_ ||
        rare_non_inherited_data_->order_ !=
            other.rare_non_inherited_data_->order_ ||
        rare_non_inherited_data_->HasFilters() !=
            other.rare_non_inherited_data_->HasFilters())
      return true;

    if (rare_non_inherited_data_->grid_.Get() !=
            other.rare_non_inherited_data_->grid_.Get() &&
        *rare_non_inherited_data_->grid_.Get() !=
            *other.rare_non_inherited_data_->grid_.Get())
      return true;

    if (rare_non_inherited_data_->grid_item_.Get() !=
            other.rare_non_inherited_data_->grid_item_.Get() &&
        *rare_non_inherited_data_->grid_item_.Get() !=
            *other.rare_non_inherited_data_->grid_item_.Get())
      return true;

    if (rare_non_inherited_data_->deprecated_flexible_box_.Get() !=
            other.rare_non_inherited_data_->deprecated_flexible_box_.Get() &&
        *rare_non_inherited_data_->deprecated_flexible_box_.Get() !=
            *other.rare_non_inherited_data_->deprecated_flexible_box_.Get())
      return true;

    if (rare_non_inherited_data_->flexible_box_.Get() !=
            other.rare_non_inherited_data_->flexible_box_.Get() &&
        *rare_non_inherited_data_->flexible_box_.Get() !=
            *other.rare_non_inherited_data_->flexible_box_.Get())
      return true;

    if (rare_non_inherited_data_->multi_col_.Get() !=
            other.rare_non_inherited_data_->multi_col_.Get() &&
        *rare_non_inherited_data_->multi_col_.Get() !=
            *other.rare_non_inherited_data_->multi_col_.Get())
      return true;

    // If the counter directives change, trigger a relayout to re-calculate
    // counter values and rebuild the counter node tree.
    const CounterDirectiveMap* map_a =
        rare_non_inherited_data_->counter_directives_.get();
    const CounterDirectiveMap* map_b =
        other.rare_non_inherited_data_->counter_directives_.get();
    if (!(map_a == map_b || (map_a && map_b && *map_a == *map_b)))
      return true;

    // We only need do layout for opacity changes if adding or losing opacity
    // could trigger a change
    // in us being a stacking context.
    if (IsStackingContext() != other.IsStackingContext() &&
        rare_non_inherited_data_->HasOpacity() !=
            other.rare_non_inherited_data_->HasOpacity()) {
      // FIXME: We would like to use SimplifiedLayout here, but we can't quite
      // do that yet.  We need to make sure SimplifiedLayout can operate
      // correctly on LayoutInlines (we will need to add a
      // selfNeedsSimplifiedLayout bit in order to not get confused and taint
      // every line).  In addition we need to solve the floating object issue
      // when layers come and go. Right now a full layout is necessary to keep
      // floating object lists sane.
      return true;
    }
  }

  if (rare_inherited_data_.Get() != other.rare_inherited_data_.Get()) {
    if (rare_inherited_data_->highlight_ !=
            other.rare_inherited_data_->highlight_ ||
        rare_inherited_data_->text_indent_ !=
            other.rare_inherited_data_->text_indent_ ||
        rare_inherited_data_->text_align_last_ !=
            other.rare_inherited_data_->text_align_last_ ||
        rare_inherited_data_->text_indent_line_ !=
            other.rare_inherited_data_->text_indent_line_ ||
        rare_inherited_data_->effective_zoom_ !=
            other.rare_inherited_data_->effective_zoom_ ||
        rare_inherited_data_->word_break_ !=
            other.rare_inherited_data_->word_break_ ||
        rare_inherited_data_->overflow_wrap_ !=
            other.rare_inherited_data_->overflow_wrap_ ||
        rare_inherited_data_->line_break_ !=
            other.rare_inherited_data_->line_break_ ||
        rare_inherited_data_->text_security_ !=
            other.rare_inherited_data_->text_security_ ||
        rare_inherited_data_->hyphens_ !=
            other.rare_inherited_data_->hyphens_ ||
        rare_inherited_data_->hyphenation_limit_before_ !=
            other.rare_inherited_data_->hyphenation_limit_before_ ||
        rare_inherited_data_->hyphenation_limit_after_ !=
            other.rare_inherited_data_->hyphenation_limit_after_ ||
        rare_inherited_data_->hyphenation_string_ !=
            other.rare_inherited_data_->hyphenation_string_ ||
        rare_inherited_data_->respect_image_orientation_ !=
            other.rare_inherited_data_->respect_image_orientation_ ||
        rare_inherited_data_->ruby_position_ !=
            other.rare_inherited_data_->ruby_position_ ||
        rare_inherited_data_->text_emphasis_mark_ !=
            other.rare_inherited_data_->text_emphasis_mark_ ||
        rare_inherited_data_->text_emphasis_position_ !=
            other.rare_inherited_data_->text_emphasis_position_ ||
        rare_inherited_data_->text_emphasis_custom_mark_ !=
            other.rare_inherited_data_->text_emphasis_custom_mark_ ||
        rare_inherited_data_->text_justify_ !=
            other.rare_inherited_data_->text_justify_ ||
        rare_inherited_data_->text_orientation_ !=
            other.rare_inherited_data_->text_orientation_ ||
        rare_inherited_data_->text_combine_ !=
            other.rare_inherited_data_->text_combine_ ||
        rare_inherited_data_->tab_size_ !=
            other.rare_inherited_data_->tab_size_ ||
        rare_inherited_data_->text_size_adjust_ !=
            other.rare_inherited_data_->text_size_adjust_ ||
        rare_inherited_data_->list_style_image_ !=
            other.rare_inherited_data_->list_style_image_ ||
        rare_inherited_data_->line_height_step_ !=
            other.rare_inherited_data_->line_height_step_ ||
        rare_inherited_data_->text_stroke_width_ !=
            other.rare_inherited_data_->text_stroke_width_)
      return true;

    if (!TextShadowDataEquivalent(other))
      return true;

    if (!QuotesDataEquivalent(other))
      return true;
  }

  if (inherited_data_->text_autosizing_multiplier_ !=
      other.inherited_data_->text_autosizing_multiplier_)
    return true;

  if (inherited_data_->font_.LoadingCustomFonts() !=
      other.inherited_data_->font_.LoadingCustomFonts())
    return true;

  if (BoxDirection() != other.BoxDirection() ||
      RtlOrdering() != other.RtlOrdering() ||
      GetTextAlign() != other.GetTextAlign() ||
      TextTransform() != other.TextTransform() ||
      Direction() != other.Direction() || WhiteSpace() != other.WhiteSpace() ||
      GetWritingMode() != other.GetWritingMode())
    return true;

  if (OverflowX() != other.OverflowX() || OverflowY() != other.OverflowY() ||
      Clear() != other.Clear() || GetUnicodeBidi() != other.GetUnicodeBidi() ||
      Floating() != other.Floating() ||
      OriginalDisplay() != other.OriginalDisplay())
    return true;

  if (IsDisplayTableType(Display())) {
    if (BorderCollapse() != other.BorderCollapse() ||
        EmptyCells() != other.EmptyCells() ||
        CaptionSide() != other.CaptionSide() ||
        TableLayout() != other.TableLayout())
      return true;

    // In the collapsing border model, 'hidden' suppresses other borders, while
    // 'none' does not, so these style differences can be width differences.
    if ((BorderCollapse() == EBorderCollapse::kCollapse) &&
        ((BorderTopStyle() == EBorderStyle::kHidden &&
          other.BorderTopStyle() == EBorderStyle::kNone) ||
         (BorderTopStyle() == EBorderStyle::kNone &&
          other.BorderTopStyle() == EBorderStyle::kHidden) ||
         (BorderBottomStyle() == EBorderStyle::kHidden &&
          other.BorderBottomStyle() == EBorderStyle::kNone) ||
         (BorderBottomStyle() == EBorderStyle::kNone &&
          other.BorderBottomStyle() == EBorderStyle::kHidden) ||
         (BorderLeftStyle() == EBorderStyle::kHidden &&
          other.BorderLeftStyle() == EBorderStyle::kNone) ||
         (BorderLeftStyle() == EBorderStyle::kNone &&
          other.BorderLeftStyle() == EBorderStyle::kHidden) ||
         (BorderRightStyle() == EBorderStyle::kHidden &&
          other.BorderRightStyle() == EBorderStyle::kNone) ||
         (BorderRightStyle() == EBorderStyle::kNone &&
          other.BorderRightStyle() == EBorderStyle::kHidden)))
      return true;
  } else if (Display() == EDisplay::kListItem) {
    if (ListStyleType() != other.ListStyleType() ||
        ListStylePosition() != other.ListStylePosition())
      return true;
  }

  if ((Visibility() == EVisibility::kCollapse) !=
      (other.Visibility() == EVisibility::kCollapse))
    return true;

  if (HasPseudoStyle(kPseudoIdScrollbar) !=
      other.HasPseudoStyle(kPseudoIdScrollbar))
    return true;

  // Movement of non-static-positioned object is special cased in
  // ComputedStyle::VisualInvalidationDiff().

  return false;
}

bool ComputedStyle::DiffNeedsFullLayout(const ComputedStyle& other) const {
  if (ComputedStyleBase::DiffNeedsFullLayout(other))
    return true;

  if (box_data_.Get() != other.box_data_.Get()) {
    if (box_data_->vertical_align_length_ !=
        other.box_data_->vertical_align_length_)
      return true;
  }

  if (VerticalAlign() != other.VerticalAlign() ||
      GetPosition() != other.GetPosition())
    return true;

  if (rare_non_inherited_data_.Get() != other.rare_non_inherited_data_.Get()) {
    if (rare_non_inherited_data_->align_content_ !=
            other.rare_non_inherited_data_->align_content_ ||
        rare_non_inherited_data_->align_items_ !=
            other.rare_non_inherited_data_->align_items_ ||
        rare_non_inherited_data_->align_self_ !=
            other.rare_non_inherited_data_->align_self_ ||
        rare_non_inherited_data_->justify_content_ !=
            other.rare_non_inherited_data_->justify_content_ ||
        rare_non_inherited_data_->justify_items_ !=
            other.rare_non_inherited_data_->justify_items_ ||
        rare_non_inherited_data_->justify_self_ !=
            other.rare_non_inherited_data_->justify_self_ ||
        rare_non_inherited_data_->contain_ !=
            other.rare_non_inherited_data_->contain_)
      return true;
  }

  return false;
}

bool ComputedStyle::DiffNeedsPaintInvalidationSubtree(
    const ComputedStyle& other) const {
  if (rare_non_inherited_data_.Get() != other.rare_non_inherited_data_.Get()) {
    if (rare_non_inherited_data_->effective_blend_mode_ !=
            other.rare_non_inherited_data_->effective_blend_mode_ ||
        rare_non_inherited_data_->isolation_ !=
            other.rare_non_inherited_data_->isolation_)
      return true;

    if (rare_non_inherited_data_->mask_ !=
            other.rare_non_inherited_data_->mask_ ||
        rare_non_inherited_data_->mask_box_image_ !=
            other.rare_non_inherited_data_->mask_box_image_)
      return true;
  }

  return false;
}

bool ComputedStyle::DiffNeedsPaintInvalidationObject(
    const ComputedStyle& other) const {
  if (Visibility() != other.Visibility() ||
      PrintColorAdjust() != other.PrintColorAdjust() ||
      InsideLink() != other.InsideLink() || !BorderVisuallyEqual(other) ||
      !RadiiEqual(other) || *background_data_ != *other.background_data_)
    return true;

  if (rare_inherited_data_.Get() != other.rare_inherited_data_.Get()) {
    if (rare_inherited_data_->user_modify_ !=
            other.rare_inherited_data_->user_modify_ ||
        rare_inherited_data_->user_select_ !=
            other.rare_inherited_data_->user_select_ ||
        rare_inherited_data_->image_rendering_ !=
            other.rare_inherited_data_->image_rendering_)
      return true;
  }

  if (rare_non_inherited_data_.Get() != other.rare_non_inherited_data_.Get()) {
    if (rare_non_inherited_data_->user_drag !=
            other.rare_non_inherited_data_->user_drag ||
        rare_non_inherited_data_->object_fit_ !=
            other.rare_non_inherited_data_->object_fit_ ||
        rare_non_inherited_data_->object_position_ !=
            other.rare_non_inherited_data_->object_position_ ||
        !rare_non_inherited_data_->ShadowDataEquivalent(
            *other.rare_non_inherited_data_.Get()) ||
        !rare_non_inherited_data_->ShapeOutsideDataEquivalent(
            *other.rare_non_inherited_data_.Get()) ||
        !rare_non_inherited_data_->ClipPathDataEquivalent(
            *other.rare_non_inherited_data_.Get()) ||
        !rare_non_inherited_data_->outline_.VisuallyEqual(
            other.rare_non_inherited_data_->outline_) ||
        (VisitedLinkBorderLeftColor() != other.VisitedLinkBorderLeftColor() &&
         BorderLeftWidth()) ||
        (VisitedLinkBorderRightColor() != other.VisitedLinkBorderRightColor() &&
         BorderRightWidth()) ||
        (VisitedLinkBorderBottomColor() !=
             other.VisitedLinkBorderBottomColor() &&
         BorderBottomWidth()) ||
        (VisitedLinkBorderTopColor() != other.VisitedLinkBorderTopColor() &&
         BorderTopWidth()) ||
        (VisitedLinkOutlineColor() != other.VisitedLinkOutlineColor() &&
         OutlineWidth()) ||
        (VisitedLinkBackgroundColor() != other.VisitedLinkBackgroundColor()))
      return true;
  }

  if (Resize() != other.Resize())
    return true;

  if (rare_non_inherited_data_->paint_images_) {
    for (const auto& image : *rare_non_inherited_data_->paint_images_) {
      if (DiffNeedsPaintInvalidationObjectForPaintImage(image, other))
        return true;
    }
  }

  return false;
}

bool ComputedStyle::DiffNeedsPaintInvalidationObjectForPaintImage(
    const StyleImage* image,
    const ComputedStyle& other) const {
  CSSPaintValue* value = ToCSSPaintValue(image->CssValue());

  // NOTE: If the invalidation properties vectors are null, we are invalid as
  // we haven't yet been painted (and can't provide the invalidation
  // properties yet).
  if (!value->NativeInvalidationProperties() ||
      !value->CustomInvalidationProperties())
    return true;

  for (CSSPropertyID property_id : *value->NativeInvalidationProperties()) {
    // TODO(ikilpatrick): remove IsInterpolableProperty check once
    // CSSPropertyEquality::PropertiesEqual correctly handles all properties.
    if (!CSSPropertyMetadata::IsInterpolableProperty(property_id) ||
        !CSSPropertyEquality::PropertiesEqual(property_id, *this, other))
      return true;
  }

  if (InheritedVariables() || NonInheritedVariables() ||
      other.InheritedVariables() || other.NonInheritedVariables()) {
    for (const AtomicString& property :
         *value->CustomInvalidationProperties()) {
      if (!DataEquivalent(GetVariable(property), other.GetVariable(property)))
        return true;
    }
  }

  return false;
}

// This doesn't include conditions needing layout or overflow recomputation
// which implies visual rect update.
bool ComputedStyle::DiffNeedsVisualRectUpdate(
    const ComputedStyle& other) const {
  // Visual rect is empty if visibility is hidden.
  if (Visibility() != other.Visibility())
    return true;

  // Need to update visual rect of the resizer.
  if (Resize() != other.Resize())
    return true;

  return false;
}

void ComputedStyle::UpdatePropertySpecificDifferences(
    const ComputedStyle& other,
    StyleDifference& diff) const {
  if (box_data_->z_index_ != other.box_data_->z_index_ ||
      IsStackingContext() != other.IsStackingContext())
    diff.SetZIndexChanged();

  if (rare_non_inherited_data_.Get() != other.rare_non_inherited_data_.Get()) {
    // It's possible for the old and new style transform data to be equivalent
    // while hasTransform() differs, as it checks a number of conditions aside
    // from just the matrix, including but not limited to animation state.
    if (HasTransform() != other.HasTransform() ||
        !TransformDataEquivalent(other) ||
        rare_non_inherited_data_->perspective_ !=
            other.rare_non_inherited_data_->perspective_ ||
        rare_non_inherited_data_->perspective_origin_ !=
            other.rare_non_inherited_data_->perspective_origin_)
      diff.SetTransformChanged();
  }

  if (rare_non_inherited_data_.Get() != other.rare_non_inherited_data_.Get()) {
    if (rare_non_inherited_data_->opacity !=
        other.rare_non_inherited_data_->opacity)
      diff.SetOpacityChanged();
  }

  if (rare_non_inherited_data_.Get() != other.rare_non_inherited_data_.Get()) {
    if (rare_non_inherited_data_->filter_ !=
        other.rare_non_inherited_data_->filter_)
      diff.SetFilterChanged();
  }

  if (rare_non_inherited_data_.Get() != other.rare_non_inherited_data_.Get()) {
    if (!rare_non_inherited_data_->ShadowDataEquivalent(
            *other.rare_non_inherited_data_.Get()))
      diff.SetNeedsRecomputeOverflow();
  }

  if (rare_non_inherited_data_.Get() != other.rare_non_inherited_data_.Get()) {
    if (rare_non_inherited_data_->backdrop_filter_ !=
        other.rare_non_inherited_data_->backdrop_filter_)
      diff.SetBackdropFilterChanged();
  }

  if (rare_non_inherited_data_.Get() != other.rare_non_inherited_data_.Get()) {
    if (!rare_non_inherited_data_->ReflectionDataEquivalent(
            *other.rare_non_inherited_data_.Get()))
      diff.SetFilterChanged();
  }

  if (rare_non_inherited_data_.Get() != other.rare_non_inherited_data_.Get()) {
    if (!rare_non_inherited_data_->outline_.VisuallyEqual(
            other.rare_non_inherited_data_->outline_))
      diff.SetNeedsRecomputeOverflow();
  }

  if (!BorderVisualOverflowEqual(other))
    diff.SetNeedsRecomputeOverflow();

  if (!diff.NeedsFullPaintInvalidation()) {
    if (inherited_data_->color_ != other.inherited_data_->color_ ||
        inherited_data_->visited_link_color_ !=
            other.inherited_data_->visited_link_color_ ||
        HasSimpleUnderlineInternal() != other.HasSimpleUnderlineInternal() ||
        visual_data_->text_decoration_ !=
            other.visual_data_->text_decoration_) {
      diff.SetTextDecorationOrColorChanged();
    } else {
      if (rare_non_inherited_data_.Get() !=
              other.rare_non_inherited_data_.Get() &&
          (rare_non_inherited_data_->text_decoration_style_ !=
               other.rare_non_inherited_data_->text_decoration_style_ ||
           rare_non_inherited_data_->text_decoration_color_ !=
               other.rare_non_inherited_data_->text_decoration_color_ ||
           rare_non_inherited_data_->visited_link_text_decoration_color_ !=
               other.rare_non_inherited_data_
                   ->visited_link_text_decoration_color_)) {
        diff.SetTextDecorationOrColorChanged();
      } else {
        if (rare_inherited_data_.Get() != other.rare_inherited_data_.Get() &&
            (TextFillColor() != other.TextFillColor() ||
             TextStrokeColor() != other.TextStrokeColor() ||
             TextEmphasisColor() != other.TextEmphasisColor() ||
             VisitedLinkTextFillColor() != other.VisitedLinkTextFillColor() ||
             VisitedLinkTextStrokeColor() !=
                 other.VisitedLinkTextStrokeColor() ||
             VisitedLinkTextEmphasisColor() !=
                 other.VisitedLinkTextEmphasisColor() ||
             rare_inherited_data_->text_emphasis_fill_ !=
                 other.rare_inherited_data_->text_emphasis_fill_ ||
             rare_inherited_data_->text_underline_position_ !=
                 other.rare_inherited_data_->text_underline_position_ ||
             rare_inherited_data_->text_decoration_skip_ !=
                 other.rare_inherited_data_->text_decoration_skip_ ||
             rare_inherited_data_->applied_text_decorations_ !=
                 other.rare_inherited_data_->applied_text_decorations_ ||
             CaretColor() != CaretColor() ||
             VisitedLinkCaretColor() != other.VisitedLinkCaretColor())) {
          diff.SetTextDecorationOrColorChanged();
        }
      }
    }
  }

  bool has_clip = HasOutOfFlowPosition() && !visual_data_->has_auto_clip_;
  bool other_has_clip =
      other.HasOutOfFlowPosition() && !other.visual_data_->has_auto_clip_;
  if (has_clip != other_has_clip ||
      (has_clip && visual_data_->clip_ != other.visual_data_->clip_))
    diff.SetCSSClipChanged();
}

void ComputedStyle::AddPaintImage(StyleImage* image) {
  if (!rare_non_inherited_data_.Access()->paint_images_) {
    rare_non_inherited_data_.Access()->paint_images_ =
        WTF::MakeUnique<Vector<Persistent<StyleImage>>>();
  }
  rare_non_inherited_data_.Access()->paint_images_->push_back(image);
}

void ComputedStyle::AddCursor(StyleImage* image,
                              bool hot_spot_specified,
                              const IntPoint& hot_spot) {
  if (!rare_inherited_data_.Access()->cursor_data_)
    rare_inherited_data_.Access()->cursor_data_ = new CursorList;
  rare_inherited_data_.Access()->cursor_data_->push_back(
      CursorData(image, hot_spot_specified, hot_spot));
}

void ComputedStyle::SetCursorList(CursorList* other) {
  rare_inherited_data_.Access()->cursor_data_ = other;
}

void ComputedStyle::SetQuotes(RefPtr<QuotesData> q) {
  rare_inherited_data_.Access()->quotes_ = std::move(q);
}

bool ComputedStyle::QuotesDataEquivalent(const ComputedStyle& other) const {
  return DataEquivalent(Quotes(), other.Quotes());
}

void ComputedStyle::ClearCursorList() {
  if (rare_inherited_data_->cursor_data_)
    rare_inherited_data_.Access()->cursor_data_ = nullptr;
}

static bool HasPropertyThatCreatesStackingContext(
    const Vector<CSSPropertyID>& properties) {
  for (CSSPropertyID property : properties) {
    switch (property) {
      case CSSPropertyOpacity:
      case CSSPropertyTransform:
      case CSSPropertyAliasWebkitTransform:
      case CSSPropertyTransformStyle:
      case CSSPropertyAliasWebkitTransformStyle:
      case CSSPropertyPerspective:
      case CSSPropertyAliasWebkitPerspective:
      case CSSPropertyTranslate:
      case CSSPropertyRotate:
      case CSSPropertyScale:
      case CSSPropertyOffsetPath:
      case CSSPropertyOffsetPosition:
      case CSSPropertyWebkitMask:
      case CSSPropertyWebkitMaskBoxImage:
      case CSSPropertyClipPath:
      case CSSPropertyAliasWebkitClipPath:
      case CSSPropertyWebkitBoxReflect:
      case CSSPropertyFilter:
      case CSSPropertyAliasWebkitFilter:
      case CSSPropertyBackdropFilter:
      case CSSPropertyZIndex:
      case CSSPropertyPosition:
      case CSSPropertyMixBlendMode:
      case CSSPropertyIsolation:
        return true;
      default:
        break;
    }
  }
  return false;
}

void ComputedStyle::UpdateIsStackingContext(bool is_document_element,
                                            bool is_in_top_layer) {
  if (IsStackingContext())
    return;

  // Force a stacking context for transform-style: preserve-3d. This happens
  // even if preserves-3d is ignored due to a 'grouping property' being present
  // which requires flattening. See ComputedStyle::UsedTransformStyle3D() and
  // ComputedStyle::HasGroupingProperty().
  // This is legacy behavior that is left ambiguous in the official specs.
  // See crbug.com/663650 for more details."
  if (TransformStyle3D() == kTransformStyle3DPreserve3D) {
    SetIsStackingContext(true);
    return;
  }

  if (is_document_element || is_in_top_layer ||
      StyleType() == kPseudoIdBackdrop || HasOpacity() ||
      HasTransformRelatedProperty() || HasMask() || ClipPath() ||
      BoxReflect() || HasFilterInducingProperty() || HasBackdropFilter() ||
      HasBlendMode() || HasIsolation() || HasViewportConstrainedPosition() ||
      HasPropertyThatCreatesStackingContext(WillChangeProperties()) ||
      ContainsPaint()) {
    SetIsStackingContext(true);
  }
}

void ComputedStyle::AddCallbackSelector(const String& selector) {
  if (!rare_non_inherited_data_->callback_selectors_.Contains(selector))
    rare_non_inherited_data_.Access()->callback_selectors_.push_back(selector);
}

void ComputedStyle::SetContent(ContentData* content_data) {
  SET_VAR(rare_non_inherited_data_, content_, content_data);
}

bool ComputedStyle::HasWillChangeCompositingHint() const {
  for (size_t i = 0;
       i < rare_non_inherited_data_->will_change_->properties_.size(); ++i) {
    switch (rare_non_inherited_data_->will_change_->properties_[i]) {
      case CSSPropertyOpacity:
      case CSSPropertyTransform:
      case CSSPropertyAliasWebkitTransform:
      case CSSPropertyTop:
      case CSSPropertyLeft:
      case CSSPropertyBottom:
      case CSSPropertyRight:
        return true;
      default:
        break;
    }
  }
  return false;
}

bool ComputedStyle::HasWillChangeTransformHint() const {
  for (const auto& property :
       rare_non_inherited_data_->will_change_->properties_) {
    switch (property) {
      case CSSPropertyTransform:
      case CSSPropertyAliasWebkitTransform:
      case CSSPropertyPerspective:
      case CSSPropertyTranslate:
      case CSSPropertyScale:
      case CSSPropertyRotate:
      case CSSPropertyOffsetPath:
      case CSSPropertyOffsetPosition:
        return true;
      default:
        break;
    }
  }
  return false;
}

bool ComputedStyle::RequireTransformOrigin(
    ApplyTransformOrigin apply_origin,
    ApplyMotionPath apply_motion_path) const {
  // transform-origin brackets the transform with translate operations.
  // Optimize for the case where the only transform is a translation, since the
  // transform-origin is irrelevant in that case.
  if (apply_origin != kIncludeTransformOrigin)
    return false;

  if (apply_motion_path == kIncludeMotionPath)
    return true;

  for (const auto& operation : Transform().Operations()) {
    TransformOperation::OperationType type = operation->GetType();
    if (type != TransformOperation::kTranslateX &&
        type != TransformOperation::kTranslateY &&
        type != TransformOperation::kTranslate &&
        type != TransformOperation::kTranslateZ &&
        type != TransformOperation::kTranslate3D)
      return true;
  }

  return Scale() || Rotate();
}

void ComputedStyle::ApplyTransform(
    TransformationMatrix& result,
    const LayoutSize& border_box_size,
    ApplyTransformOrigin apply_origin,
    ApplyMotionPath apply_motion_path,
    ApplyIndependentTransformProperties apply_independent_transform_properties)
    const {
  ApplyTransform(result, FloatRect(FloatPoint(), FloatSize(border_box_size)),
                 apply_origin, apply_motion_path,
                 apply_independent_transform_properties);
}

void ComputedStyle::ApplyTransform(
    TransformationMatrix& result,
    const FloatRect& bounding_box,
    ApplyTransformOrigin apply_origin,
    ApplyMotionPath apply_motion_path,
    ApplyIndependentTransformProperties apply_independent_transform_properties)
    const {
  if (!HasOffset())
    apply_motion_path = kExcludeMotionPath;
  bool apply_transform_origin =
      RequireTransformOrigin(apply_origin, apply_motion_path);

  float origin_x = 0;
  float origin_y = 0;
  float origin_z = 0;

  const FloatSize& box_size = bounding_box.Size();
  if (apply_transform_origin ||
      // We need to calculate originX and originY for applying motion path.
      apply_motion_path == kIncludeMotionPath) {
    origin_x = FloatValueForLength(TransformOriginX(), box_size.Width()) +
               bounding_box.X();
    origin_y = FloatValueForLength(TransformOriginY(), box_size.Height()) +
               bounding_box.Y();
    if (apply_transform_origin) {
      origin_z = TransformOriginZ();
      result.Translate3d(origin_x, origin_y, origin_z);
    }
  }

  if (apply_independent_transform_properties ==
      kIncludeIndependentTransformProperties) {
    if (Translate())
      Translate()->Apply(result, box_size);

    if (Rotate())
      Rotate()->Apply(result, box_size);

    if (Scale())
      Scale()->Apply(result, box_size);
  }

  if (apply_motion_path == kIncludeMotionPath)
    ApplyMotionPathTransform(origin_x, origin_y, bounding_box, result);

  for (const auto& operation : Transform().Operations())
    operation->Apply(result, box_size);

  if (apply_transform_origin) {
    result.Translate3d(-origin_x, -origin_y, -origin_z);
  }
}

void ComputedStyle::ApplyMotionPathTransform(
    float origin_x,
    float origin_y,
    const FloatRect& bounding_box,
    TransformationMatrix& transform) const {
  const StyleMotionData& motion_data =
      rare_non_inherited_data_->transform_->motion_;
  // TODO(ericwilligers): crbug.com/638055 Apply offset-position.
  if (!motion_data.path_) {
    return;
  }
  const LengthPoint& position = OffsetPosition();
  const LengthPoint& anchor = OffsetAnchor();

  FloatPoint point;
  float angle;
  if (motion_data.path_->GetType() == BasicShape::kStyleRayType) {
    // TODO(ericwilligers): crbug.com/641245 Support <size> for ray paths.
    float distance = FloatValueForLength(motion_data.distance_, 0);

    angle = ToStyleRay(*motion_data.path_).Angle() - 90;
    point.SetX(distance * cos(deg2rad(angle)));
    point.SetY(distance * sin(deg2rad(angle)));
  } else {
    const StylePath& motion_path = ToStylePath(*motion_data.path_);
    float path_length = motion_path.length();
    float distance = FloatValueForLength(motion_data.distance_, path_length);
    float computed_distance;
    if (motion_path.IsClosed() && path_length > 0) {
      computed_distance = fmod(distance, path_length);
      if (computed_distance < 0)
        computed_distance += path_length;
    } else {
      computed_distance = clampTo<float>(distance, 0, path_length);
    }

    motion_path.GetPath().PointAndNormalAtLength(computed_distance, point,
                                                 angle);
  }

  if (motion_data.rotation_.type == kOffsetRotationFixed)
    angle = 0;

  float origin_shift_x = 0;
  float origin_shift_y = 0;
  // If offset-Position and offset-anchor properties are not yet enabled,
  // they will have the default value, auto.
  if (position.X() != Length(kAuto) || anchor.X() != Length(kAuto)) {
    // Shift the origin from transform-origin to offset-anchor.
    origin_shift_x =
        FloatValueForLength(anchor.X(), bounding_box.Width()) -
        FloatValueForLength(TransformOriginX(), bounding_box.Width());
    origin_shift_y =
        FloatValueForLength(anchor.Y(), bounding_box.Height()) -
        FloatValueForLength(TransformOriginY(), bounding_box.Height());
  }

  transform.Translate(point.X() - origin_x + origin_shift_x,
                      point.Y() - origin_y + origin_shift_y);
  transform.Rotate(angle + motion_data.rotation_.angle);

  if (position.X() != Length(kAuto) || anchor.X() != Length(kAuto))
    // Shift the origin back to transform-origin.
    transform.Translate(-origin_shift_x, -origin_shift_y);
}

void ComputedStyle::SetTextShadow(RefPtr<ShadowList> s) {
  rare_inherited_data_.Access()->text_shadow_ = std::move(s);
}

bool ComputedStyle::TextShadowDataEquivalent(const ComputedStyle& other) const {
  return DataEquivalent(TextShadow(), other.TextShadow());
}

void ComputedStyle::SetBoxShadow(RefPtr<ShadowList> s) {
  rare_non_inherited_data_.Access()->box_shadow_ = std::move(s);
}

static FloatRoundedRect::Radii CalcRadiiFor(const LengthSize& top_left,
                                            const LengthSize& top_right,
                                            const LengthSize& bottom_left,
                                            const LengthSize& bottom_right,
                                            LayoutSize size) {
  return FloatRoundedRect::Radii(
      FloatSize(
          FloatValueForLength(top_left.Width(), size.Width().ToFloat()),
          FloatValueForLength(top_left.Height(), size.Height().ToFloat())),
      FloatSize(
          FloatValueForLength(top_right.Width(), size.Width().ToFloat()),
          FloatValueForLength(top_right.Height(), size.Height().ToFloat())),
      FloatSize(
          FloatValueForLength(bottom_left.Width(), size.Width().ToFloat()),
          FloatValueForLength(bottom_left.Height(), size.Height().ToFloat())),
      FloatSize(
          FloatValueForLength(bottom_right.Width(), size.Width().ToFloat()),
          FloatValueForLength(bottom_right.Height(), size.Height().ToFloat())));
}

StyleImage* ComputedStyle::ListStyleImage() const {
  return rare_inherited_data_->list_style_image_.Get();
}
void ComputedStyle::SetListStyleImage(StyleImage* v) {
  if (rare_inherited_data_->list_style_image_ != v)
    rare_inherited_data_.Access()->list_style_image_ = v;
}

Color ComputedStyle::GetColor() const {
  return ColorInternal();
}
void ComputedStyle::SetColor(const Color& v) {
  SetColorInternal(v);
}

FloatRoundedRect ComputedStyle::GetRoundedBorderFor(
    const LayoutRect& border_rect,
    bool include_logical_left_edge,
    bool include_logical_right_edge) const {
  FloatRoundedRect rounded_rect(PixelSnappedIntRect(border_rect));
  if (HasBorderRadius()) {
    FloatRoundedRect::Radii radii = CalcRadiiFor(
        BorderTopLeftRadius(), BorderTopRightRadius(), BorderBottomLeftRadius(),
        BorderBottomRightRadius(), border_rect.Size());
    rounded_rect.IncludeLogicalEdges(radii, IsHorizontalWritingMode(),
                                     include_logical_left_edge,
                                     include_logical_right_edge);
    rounded_rect.ConstrainRadii();
  }
  return rounded_rect;
}

FloatRoundedRect ComputedStyle::GetRoundedInnerBorderFor(
    const LayoutRect& border_rect,
    bool include_logical_left_edge,
    bool include_logical_right_edge) const {
  bool horizontal = IsHorizontalWritingMode();

  int left_width = (!horizontal || include_logical_left_edge)
                       ? roundf(BorderLeftWidth())
                       : 0;
  int right_width = (!horizontal || include_logical_right_edge)
                        ? roundf(BorderRightWidth())
                        : 0;
  int top_width =
      (horizontal || include_logical_left_edge) ? roundf(BorderTopWidth()) : 0;
  int bottom_width = (horizontal || include_logical_right_edge)
                         ? roundf(BorderBottomWidth())
                         : 0;

  return GetRoundedInnerBorderFor(
      border_rect,
      LayoutRectOutsets(-top_width, -right_width, -bottom_width, -left_width),
      include_logical_left_edge, include_logical_right_edge);
}

FloatRoundedRect ComputedStyle::GetRoundedInnerBorderFor(
    const LayoutRect& border_rect,
    const LayoutRectOutsets& insets,
    bool include_logical_left_edge,
    bool include_logical_right_edge) const {
  LayoutRect inner_rect(border_rect);
  inner_rect.Expand(insets);
  LayoutSize inner_rect_size = inner_rect.Size();
  inner_rect_size.ClampNegativeToZero();
  inner_rect.SetSize(inner_rect_size);

  FloatRoundedRect rounded_rect(PixelSnappedIntRect(inner_rect));

  if (HasBorderRadius()) {
    FloatRoundedRect::Radii radii = GetRoundedBorderFor(border_rect).GetRadii();
    // Insets use negative values.
    radii.Shrink(-insets.Top().ToFloat(), -insets.Bottom().ToFloat(),
                 -insets.Left().ToFloat(), -insets.Right().ToFloat());
    rounded_rect.IncludeLogicalEdges(radii, IsHorizontalWritingMode(),
                                     include_logical_left_edge,
                                     include_logical_right_edge);
  }
  return rounded_rect;
}

static bool AllLayersAreFixed(const FillLayer& layer) {
  for (const FillLayer* curr_layer = &layer; curr_layer;
       curr_layer = curr_layer->Next()) {
    if (!curr_layer->GetImage() ||
        curr_layer->Attachment() != kFixedBackgroundAttachment)
      return false;
  }

  return true;
}

bool ComputedStyle::HasEntirelyFixedBackground() const {
  return AllLayersAreFixed(BackgroundLayers());
}

const CounterDirectiveMap* ComputedStyle::GetCounterDirectives() const {
  return rare_non_inherited_data_->counter_directives_.get();
}

CounterDirectiveMap& ComputedStyle::AccessCounterDirectives() {
  std::unique_ptr<CounterDirectiveMap>& map =
      rare_non_inherited_data_.Access()->counter_directives_;
  if (!map)
    map = WTF::WrapUnique(new CounterDirectiveMap);
  return *map;
}

const CounterDirectives ComputedStyle::GetCounterDirectives(
    const AtomicString& identifier) const {
  if (const CounterDirectiveMap* directives = GetCounterDirectives())
    return directives->at(identifier);
  return CounterDirectives();
}

void ComputedStyle::ClearIncrementDirectives() {
  if (!GetCounterDirectives())
    return;

  // This makes us copy even if we may not be removing any items.
  CounterDirectiveMap& map = AccessCounterDirectives();
  typedef CounterDirectiveMap::iterator Iterator;

  Iterator end = map.end();
  for (Iterator it = map.begin(); it != end; ++it)
    it->value.ClearIncrement();
}

void ComputedStyle::ClearResetDirectives() {
  if (!GetCounterDirectives())
    return;

  // This makes us copy even if we may not be removing any items.
  CounterDirectiveMap& map = AccessCounterDirectives();
  typedef CounterDirectiveMap::iterator Iterator;

  Iterator end = map.end();
  for (Iterator it = map.begin(); it != end; ++it)
    it->value.ClearReset();
}

AtomicString ComputedStyle::LocaleForLineBreakIterator() const {
  LineBreakIteratorMode mode = LineBreakIteratorMode::kDefault;
  switch (GetLineBreak()) {
    case LineBreak::kAuto:
    case LineBreak::kAfterWhiteSpace:
      return Locale();
    case LineBreak::kNormal:
      mode = LineBreakIteratorMode::kNormal;
      break;
    case LineBreak::kStrict:
      mode = LineBreakIteratorMode::kStrict;
      break;
    case LineBreak::kLoose:
      mode = LineBreakIteratorMode::kLoose;
      break;
  }
  if (const LayoutLocale* locale = GetFontDescription().Locale())
    return locale->LocaleWithBreakKeyword(mode);
  return Locale();
}

Hyphenation* ComputedStyle::GetHyphenation() const {
  return GetHyphens() == Hyphens::kAuto
             ? GetFontDescription().LocaleOrDefault().GetHyphenation()
             : nullptr;
}

const AtomicString& ComputedStyle::HyphenString() const {
  const AtomicString& hyphenation_string =
      rare_inherited_data_.Get()->hyphenation_string_;
  if (!hyphenation_string.IsNull())
    return hyphenation_string;

  // FIXME: This should depend on locale.
  DEFINE_STATIC_LOCAL(AtomicString, hyphen_minus_string,
                      (&kHyphenMinusCharacter, 1));
  DEFINE_STATIC_LOCAL(AtomicString, hyphen_string, (&kHyphenCharacter, 1));
  const SimpleFontData* primary_font = GetFont().PrimaryFont();
  DCHECK(primary_font);
  return primary_font && primary_font->GlyphForCharacter(kHyphenCharacter)
             ? hyphen_string
             : hyphen_minus_string;
}

const AtomicString& ComputedStyle::TextEmphasisMarkString() const {
  switch (GetTextEmphasisMark()) {
    case kTextEmphasisMarkNone:
      return g_null_atom;
    case kTextEmphasisMarkCustom:
      return TextEmphasisCustomMark();
    case kTextEmphasisMarkDot: {
      DEFINE_STATIC_LOCAL(AtomicString, filled_dot_string,
                          (&kBulletCharacter, 1));
      DEFINE_STATIC_LOCAL(AtomicString, open_dot_string,
                          (&kWhiteBulletCharacter, 1));
      return GetTextEmphasisFill() == kTextEmphasisFillFilled
                 ? filled_dot_string
                 : open_dot_string;
    }
    case kTextEmphasisMarkCircle: {
      DEFINE_STATIC_LOCAL(AtomicString, filled_circle_string,
                          (&kBlackCircleCharacter, 1));
      DEFINE_STATIC_LOCAL(AtomicString, open_circle_string,
                          (&kWhiteCircleCharacter, 1));
      return GetTextEmphasisFill() == kTextEmphasisFillFilled
                 ? filled_circle_string
                 : open_circle_string;
    }
    case kTextEmphasisMarkDoubleCircle: {
      DEFINE_STATIC_LOCAL(AtomicString, filled_double_circle_string,
                          (&kFisheyeCharacter, 1));
      DEFINE_STATIC_LOCAL(AtomicString, open_double_circle_string,
                          (&kBullseyeCharacter, 1));
      return GetTextEmphasisFill() == kTextEmphasisFillFilled
                 ? filled_double_circle_string
                 : open_double_circle_string;
    }
    case kTextEmphasisMarkTriangle: {
      DEFINE_STATIC_LOCAL(AtomicString, filled_triangle_string,
                          (&kBlackUpPointingTriangleCharacter, 1));
      DEFINE_STATIC_LOCAL(AtomicString, open_triangle_string,
                          (&kWhiteUpPointingTriangleCharacter, 1));
      return GetTextEmphasisFill() == kTextEmphasisFillFilled
                 ? filled_triangle_string
                 : open_triangle_string;
    }
    case kTextEmphasisMarkSesame: {
      DEFINE_STATIC_LOCAL(AtomicString, filled_sesame_string,
                          (&kSesameDotCharacter, 1));
      DEFINE_STATIC_LOCAL(AtomicString, open_sesame_string,
                          (&kWhiteSesameDotCharacter, 1));
      return GetTextEmphasisFill() == kTextEmphasisFillFilled
                 ? filled_sesame_string
                 : open_sesame_string;
    }
    case kTextEmphasisMarkAuto:
      NOTREACHED();
      return g_null_atom;
  }

  NOTREACHED();
  return g_null_atom;
}

CSSAnimationData& ComputedStyle::AccessAnimations() {
  if (!rare_non_inherited_data_.Access()->animations_)
    rare_non_inherited_data_.Access()->animations_ = CSSAnimationData::Create();
  return *rare_non_inherited_data_->animations_;
}

CSSTransitionData& ComputedStyle::AccessTransitions() {
  if (!rare_non_inherited_data_.Access()->transitions_)
    rare_non_inherited_data_.Access()->transitions_ =
        CSSTransitionData::Create();
  return *rare_non_inherited_data_->transitions_;
}

const Font& ComputedStyle::GetFont() const {
  return FontInternal();
}
const FontDescription& ComputedStyle::GetFontDescription() const {
  return FontInternal().GetFontDescription();
}
float ComputedStyle::SpecifiedFontSize() const {
  return GetFontDescription().SpecifiedSize();
}
float ComputedStyle::ComputedFontSize() const {
  return GetFontDescription().ComputedSize();
}
LayoutUnit ComputedStyle::ComputedFontSizeAsFixed() const {
  return LayoutUnit::FromFloatRound(GetFontDescription().ComputedSize());
}
int ComputedStyle::FontSize() const {
  return GetFontDescription().ComputedPixelSize();
}
float ComputedStyle::FontSizeAdjust() const {
  return GetFontDescription().SizeAdjust();
}
bool ComputedStyle::HasFontSizeAdjust() const {
  return GetFontDescription().HasSizeAdjust();
}
FontWeight ComputedStyle::GetFontWeight() const {
  return GetFontDescription().Weight();
}
FontStretch ComputedStyle::GetFontStretch() const {
  return GetFontDescription().Stretch();
}

TextDecoration ComputedStyle::TextDecorationsInEffect() const {
  if (HasSimpleUnderlineInternal())
    return TextDecoration::kUnderline;
  if (!rare_inherited_data_->applied_text_decorations_)
    return TextDecoration::kNone;

  TextDecoration decorations = TextDecoration::kNone;

  const Vector<AppliedTextDecoration>& applied = AppliedTextDecorations();

  for (size_t i = 0; i < applied.size(); ++i)
    decorations |= applied[i].Lines();

  return decorations;
}

const Vector<AppliedTextDecoration>& ComputedStyle::AppliedTextDecorations()
    const {
  if (HasSimpleUnderlineInternal()) {
    DEFINE_STATIC_LOCAL(
        Vector<AppliedTextDecoration>, underline,
        (1, AppliedTextDecoration(
                TextDecoration::kUnderline, kTextDecorationStyleSolid,
                VisitedDependentColor(CSSPropertyTextDecorationColor))));
    // Since we only have one of these in memory, just update the color before
    // returning.
    underline.at(0).SetColor(
        VisitedDependentColor(CSSPropertyTextDecorationColor));
    return underline;
  }
  if (!rare_inherited_data_->applied_text_decorations_) {
    DEFINE_STATIC_LOCAL(Vector<AppliedTextDecoration>, empty, ());
    return empty;
  }

  return rare_inherited_data_->applied_text_decorations_->GetVector();
}

StyleInheritedVariables* ComputedStyle::InheritedVariables() const {
  return rare_inherited_data_->variables_.Get();
}

StyleNonInheritedVariables* ComputedStyle::NonInheritedVariables() const {
  return rare_non_inherited_data_->variables_.get();
}

StyleInheritedVariables& ComputedStyle::MutableInheritedVariables() {
  RefPtr<StyleInheritedVariables>& variables =
      rare_inherited_data_.Access()->variables_;
  if (!variables)
    variables = StyleInheritedVariables::Create();
  else if (!variables->HasOneRef())
    variables = variables->Copy();
  return *variables;
}

StyleNonInheritedVariables& ComputedStyle::MutableNonInheritedVariables() {
  std::unique_ptr<StyleNonInheritedVariables>& variables =
      rare_non_inherited_data_.Access()->variables_;
  if (!variables)
    variables = StyleNonInheritedVariables::Create();
  return *variables;
}

void ComputedStyle::SetUnresolvedInheritedVariable(
    const AtomicString& name,
    RefPtr<CSSVariableData> value) {
  DCHECK(value && value->NeedsVariableResolution());
  MutableInheritedVariables().SetVariable(name, std::move(value));
}

void ComputedStyle::SetUnresolvedNonInheritedVariable(
    const AtomicString& name,
    RefPtr<CSSVariableData> value) {
  DCHECK(value && value->NeedsVariableResolution());
  MutableNonInheritedVariables().SetVariable(name, std::move(value));
}

void ComputedStyle::SetResolvedUnregisteredVariable(
    const AtomicString& name,
    RefPtr<CSSVariableData> value) {
  DCHECK(value && !value->NeedsVariableResolution());
  MutableInheritedVariables().SetVariable(name, std::move(value));
}

void ComputedStyle::SetResolvedInheritedVariable(const AtomicString& name,
                                                 RefPtr<CSSVariableData> value,
                                                 const CSSValue* parsed_value) {
  DCHECK(!!value == !!parsed_value);
  DCHECK(!(value && value->NeedsVariableResolution()));

  StyleInheritedVariables& variables = MutableInheritedVariables();
  variables.SetVariable(name, std::move(value));
  variables.SetRegisteredVariable(name, parsed_value);
}

void ComputedStyle::SetResolvedNonInheritedVariable(
    const AtomicString& name,
    RefPtr<CSSVariableData> value,
    const CSSValue* parsed_value) {
  DCHECK(!!value == !!parsed_value);
  DCHECK(!(value && value->NeedsVariableResolution()));

  StyleNonInheritedVariables& variables = MutableNonInheritedVariables();
  variables.SetVariable(name, std::move(value));
  variables.SetRegisteredVariable(name, parsed_value);
}

void ComputedStyle::RemoveVariable(const AtomicString& name,
                                   bool is_inherited_property) {
  if (is_inherited_property) {
    MutableInheritedVariables().RemoveVariable(name);
  } else {
    MutableNonInheritedVariables().RemoveVariable(name);
  }
}

CSSVariableData* ComputedStyle::GetVariable(const AtomicString& name) const {
  CSSVariableData* variable = GetVariable(name, true);
  if (variable) {
    return variable;
  }
  return GetVariable(name, false);
}

CSSVariableData* ComputedStyle::GetVariable(const AtomicString& name,
                                            bool is_inherited_property) const {
  if (is_inherited_property) {
    return InheritedVariables() ? InheritedVariables()->GetVariable(name)
                                : nullptr;
  }
  return NonInheritedVariables() ? NonInheritedVariables()->GetVariable(name)
                                 : nullptr;
}

const CSSValue* ComputedStyle::GetRegisteredVariable(
    const AtomicString& name,
    bool is_inherited_property) const {
  if (is_inherited_property) {
    return InheritedVariables() ? InheritedVariables()->RegisteredVariable(name)
                                : nullptr;
  }
  return NonInheritedVariables()
             ? NonInheritedVariables()->RegisteredVariable(name)
             : nullptr;
}

const CSSValue* ComputedStyle::GetRegisteredVariable(
    const AtomicString& name) const {
  // Registered custom properties are by default non-inheriting so check there
  // first.
  const CSSValue* result = GetRegisteredVariable(name, false);
  if (result) {
    return result;
  }
  return GetRegisteredVariable(name, true);
}

float ComputedStyle::WordSpacing() const {
  return GetFontDescription().WordSpacing();
}
float ComputedStyle::LetterSpacing() const {
  return GetFontDescription().LetterSpacing();
}

bool ComputedStyle::SetFontDescription(const FontDescription& v) {
  if (FontInternal().GetFontDescription() != v) {
    SetFontInternal(Font(v));
    return true;
  }
  return false;
}

void ComputedStyle::SetFont(const Font& font) {
  SetFontInternal(font);
}

bool ComputedStyle::HasIdenticalAscentDescentAndLineGap(
    const ComputedStyle& other) const {
  const SimpleFontData* font_data = GetFont().PrimaryFont();
  const SimpleFontData* other_font_data = other.GetFont().PrimaryFont();
  return font_data && other_font_data &&
         font_data->GetFontMetrics().HasIdenticalAscentDescentAndLineGap(
             other_font_data->GetFontMetrics());
}

const Length& ComputedStyle::SpecifiedLineHeight() const {
  return LineHeightInternal();
}
Length ComputedStyle::LineHeight() const {
  const Length& lh = LineHeightInternal();
  // Unlike getFontDescription().computedSize() and hence fontSize(), this is
  // recalculated on demand as we only store the specified line height.
  // FIXME: Should consider scaling the fixed part of any calc expressions
  // too, though this involves messily poking into CalcExpressionLength.
  if (lh.IsFixed()) {
    float multiplier = TextAutosizingMultiplier();
    return Length(
        TextAutosizer::ComputeAutosizedFontSize(lh.Value(), multiplier),
        kFixed);
  }

  return lh;
}

void ComputedStyle::SetLineHeight(const Length& specified_line_height) {
  SetLineHeightInternal(specified_line_height);
}

int ComputedStyle::ComputedLineHeight() const {
  const Length& lh = LineHeight();

  // Negative value means the line height is not set. Use the font's built-in
  // spacing, if avalible.
  if (lh.IsNegative() && GetFont().PrimaryFont())
    return GetFont().PrimaryFont()->GetFontMetrics().LineSpacing();

  if (lh.IsPercentOrCalc())
    return MinimumValueForLength(lh, LayoutUnit(ComputedFontSize())).ToInt();

  return std::min(lh.Value(), LayoutUnit::Max().ToFloat());
}

LayoutUnit ComputedStyle::ComputedLineHeightAsFixed() const {
  const Length& lh = LineHeight();

  // Negative value means the line height is not set. Use the font's built-in
  // spacing, if avalible.
  if (lh.IsNegative() && GetFont().PrimaryFont())
    return GetFont().PrimaryFont()->GetFontMetrics().FixedLineSpacing();

  if (lh.IsPercentOrCalc())
    return MinimumValueForLength(lh, ComputedFontSizeAsFixed());

  return LayoutUnit::FromFloatRound(lh.Value());
}

void ComputedStyle::SetWordSpacing(float word_spacing) {
  FontSelector* current_font_selector = GetFont().GetFontSelector();
  FontDescription desc(GetFontDescription());
  desc.SetWordSpacing(word_spacing);
  SetFontDescription(desc);
  GetFont().Update(current_font_selector);
}

void ComputedStyle::SetLetterSpacing(float letter_spacing) {
  FontSelector* current_font_selector = GetFont().GetFontSelector();
  FontDescription desc(GetFontDescription());
  desc.SetLetterSpacing(letter_spacing);
  SetFontDescription(desc);
  GetFont().Update(current_font_selector);
}

void ComputedStyle::SetTextAutosizingMultiplier(float multiplier) {
  SetTextAutosizingMultiplierInternal(multiplier);

  float size = SpecifiedFontSize();

  DCHECK(std::isfinite(size));
  if (!std::isfinite(size) || size < 0)
    size = 0;
  else
    size = std::min(kMaximumAllowedFontSize, size);

  FontSelector* current_font_selector = GetFont().GetFontSelector();
  FontDescription desc(GetFontDescription());
  desc.SetSpecifiedSize(size);
  desc.SetComputedSize(size);

  float autosized_font_size =
      TextAutosizer::ComputeAutosizedFontSize(size, multiplier);
  desc.SetComputedSize(std::min(kMaximumAllowedFontSize, autosized_font_size));

  SetFontDescription(desc);
  GetFont().Update(current_font_selector);
}

void ComputedStyle::AddAppliedTextDecoration(
    const AppliedTextDecoration& decoration) {
  RefPtr<AppliedTextDecorationList>& list =
      rare_inherited_data_.Access()->applied_text_decorations_;

  if (!list)
    list = AppliedTextDecorationList::Create();
  else if (!list->HasOneRef())
    list = list->Copy();

  list->push_back(decoration);
}

void ComputedStyle::OverrideTextDecorationColors(Color override_color) {
  RefPtr<AppliedTextDecorationList>& list =
      rare_inherited_data_.Access()->applied_text_decorations_;
  DCHECK(list);
  if (!list->HasOneRef())
    list = list->Copy();

  for (size_t i = 0; i < list->size(); ++i)
    list->at(i).SetColor(override_color);
}

void ComputedStyle::ApplyTextDecorations(
    const Color& parent_text_decoration_color,
    bool override_existing_colors) {
  if (GetTextDecoration() == TextDecoration::kNone &&
      !HasSimpleUnderlineInternal() &&
      !rare_inherited_data_->applied_text_decorations_)
    return;

  // If there are any color changes or decorations set by this element, stop
  // using m_hasSimpleUnderline.
  Color current_text_decoration_color =
      VisitedDependentColor(CSSPropertyTextDecorationColor);
  if (HasSimpleUnderlineInternal() &&
      (GetTextDecoration() != TextDecoration::kNone ||
       current_text_decoration_color != parent_text_decoration_color)) {
    SetHasSimpleUnderlineInternal(false);
    AddAppliedTextDecoration(AppliedTextDecoration(
        TextDecoration::kUnderline, kTextDecorationStyleSolid,
        parent_text_decoration_color));
  }
  if (override_existing_colors &&
      rare_inherited_data_->applied_text_decorations_)
    OverrideTextDecorationColors(current_text_decoration_color);
  if (GetTextDecoration() == TextDecoration::kNone)
    return;
  DCHECK(!HasSimpleUnderlineInternal());
  // To save memory, we don't use AppliedTextDecoration objects in the common
  // case of a single simple underline of currentColor.
  TextDecoration decoration_lines = GetTextDecoration();
  TextDecorationStyle decoration_style = GetTextDecorationStyle();
  bool is_simple_underline = decoration_lines == TextDecoration::kUnderline &&
                             decoration_style == kTextDecorationStyleSolid &&
                             TextDecorationColor().IsCurrentColor();
  if (is_simple_underline && !rare_inherited_data_->applied_text_decorations_) {
    SetHasSimpleUnderlineInternal(true);
    return;
  }

  AddAppliedTextDecoration(AppliedTextDecoration(
      decoration_lines, decoration_style, current_text_decoration_color));
}

void ComputedStyle::ClearAppliedTextDecorations() {
  SetHasSimpleUnderlineInternal(false);

  if (rare_inherited_data_->applied_text_decorations_)
    rare_inherited_data_.Access()->applied_text_decorations_ = nullptr;
}

void ComputedStyle::RestoreParentTextDecorations(
    const ComputedStyle& parent_style) {
  SetHasSimpleUnderlineInternal(parent_style.HasSimpleUnderlineInternal());
  if (rare_inherited_data_->applied_text_decorations_ !=
      parent_style.rare_inherited_data_->applied_text_decorations_) {
    rare_inherited_data_.Access()->applied_text_decorations_ =
        parent_style.rare_inherited_data_->applied_text_decorations_;
  }
}

void ComputedStyle::ClearMultiCol() {
  rare_non_inherited_data_.Access()->multi_col_ = nullptr;
  rare_non_inherited_data_.Access()->multi_col_.Init();
}

StyleColor ComputedStyle::DecorationColorIncludingFallback(
    bool visited_link) const {
  StyleColor style_color =
      visited_link ? VisitedLinkTextDecorationColor() : TextDecorationColor();

  if (!style_color.IsCurrentColor())
    return style_color;

  if (TextStrokeWidth()) {
    // Prefer stroke color if possible, but not if it's fully transparent.
    StyleColor text_stroke_style_color =
        visited_link ? VisitedLinkTextStrokeColor() : TextStrokeColor();
    if (!text_stroke_style_color.IsCurrentColor() &&
        text_stroke_style_color.GetColor().Alpha())
      return text_stroke_style_color;
  }

  return visited_link ? VisitedLinkTextFillColor() : TextFillColor();
}

Color ComputedStyle::ColorIncludingFallback(int color_property,
                                            bool visited_link) const {
  StyleColor result(StyleColor::CurrentColor());
  EBorderStyle border_style = EBorderStyle::kNone;
  switch (color_property) {
    case CSSPropertyBackgroundColor:
      result = visited_link ? VisitedLinkBackgroundColor() : BackgroundColor();
      break;
    case CSSPropertyBorderLeftColor:
      result = visited_link ? VisitedLinkBorderLeftColor() : BorderLeftColor();
      border_style = BorderLeftStyle();
      break;
    case CSSPropertyBorderRightColor:
      result =
          visited_link ? VisitedLinkBorderRightColor() : BorderRightColor();
      border_style = BorderRightStyle();
      break;
    case CSSPropertyBorderTopColor:
      result = visited_link ? VisitedLinkBorderTopColor() : BorderTopColor();
      border_style = BorderTopStyle();
      break;
    case CSSPropertyBorderBottomColor:
      result =
          visited_link ? VisitedLinkBorderBottomColor() : BorderBottomColor();
      border_style = BorderBottomStyle();
      break;
    case CSSPropertyCaretColor: {
      StyleAutoColor auto_color =
          visited_link ? VisitedLinkCaretColor() : CaretColor();
      // TODO(rego): We may want to adjust the caret color if it's the same than
      // the background to ensure good visibility and contrast.
      result = auto_color.IsAutoColor() ? StyleColor::CurrentColor()
                                        : auto_color.ToStyleColor();
      break;
    }
    case CSSPropertyColor:
      result = visited_link ? VisitedLinkColor() : GetColor();
      break;
    case CSSPropertyOutlineColor:
      result = visited_link ? VisitedLinkOutlineColor() : OutlineColor();
      break;
    case CSSPropertyColumnRuleColor:
      result = visited_link ? VisitedLinkColumnRuleColor() : ColumnRuleColor();
      break;
    case CSSPropertyWebkitTextEmphasisColor:
      result =
          visited_link ? VisitedLinkTextEmphasisColor() : TextEmphasisColor();
      break;
    case CSSPropertyWebkitTextFillColor:
      result = visited_link ? VisitedLinkTextFillColor() : TextFillColor();
      break;
    case CSSPropertyWebkitTextStrokeColor:
      result = visited_link ? VisitedLinkTextStrokeColor() : TextStrokeColor();
      break;
    case CSSPropertyFloodColor:
      result = FloodColor();
      break;
    case CSSPropertyLightingColor:
      result = LightingColor();
      break;
    case CSSPropertyStopColor:
      result = StopColor();
      break;
    case CSSPropertyWebkitTapHighlightColor:
      result = TapHighlightColor();
      break;
    case CSSPropertyTextDecorationColor:
      result = DecorationColorIncludingFallback(visited_link);
      break;
    default:
      NOTREACHED();
      break;
  }

  if (!result.IsCurrentColor())
    return result.GetColor();

  // FIXME: Treating styled borders with initial color differently causes
  // problems, see crbug.com/316559, crbug.com/276231
  if (!visited_link && (border_style == EBorderStyle::kInset ||
                        border_style == EBorderStyle::kOutset ||
                        border_style == EBorderStyle::kRidge ||
                        border_style == EBorderStyle::kGroove))
    return Color(238, 238, 238);
  return visited_link ? VisitedLinkColor() : GetColor();
}

Color ComputedStyle::VisitedDependentColor(int color_property) const {
  Color unvisited_color = ColorIncludingFallback(color_property, false);
  if (InsideLink() != EInsideLink::kInsideVisitedLink)
    return unvisited_color;

  Color visited_color = ColorIncludingFallback(color_property, true);

  // FIXME: Technically someone could explicitly specify the color transparent,
  // but for now we'll just assume that if the background color is transparent
  // that it wasn't set. Note that it's weird that we're returning unvisited
  // info for a visited link, but given our restriction that the alpha values
  // have to match, it makes more sense to return the unvisited background color
  // if specified than it does to return black. This behavior matches what
  // Firefox 4 does as well.
  if (color_property == CSSPropertyBackgroundColor &&
      visited_color == Color::kTransparent)
    return unvisited_color;

  // Take the alpha from the unvisited color, but get the RGB values from the
  // visited color.
  return Color(visited_color.Red(), visited_color.Green(), visited_color.Blue(),
               unvisited_color.Alpha());
}

BorderValue ComputedStyle::BorderBefore() const {
  switch (GetWritingMode()) {
    case WritingMode::kHorizontalTb:
      return BorderTop();
    case WritingMode::kVerticalLr:
      return BorderLeft();
    case WritingMode::kVerticalRl:
      return BorderRight();
  }
  NOTREACHED();
  return BorderTop();
}

BorderValue ComputedStyle::BorderAfter() const {
  switch (GetWritingMode()) {
    case WritingMode::kHorizontalTb:
      return BorderBottom();
    case WritingMode::kVerticalLr:
      return BorderRight();
    case WritingMode::kVerticalRl:
      return BorderLeft();
  }
  NOTREACHED();
  return BorderBottom();
}

BorderValue ComputedStyle::BorderStart() const {
  if (IsHorizontalWritingMode())
    return IsLeftToRightDirection() ? BorderLeft() : BorderRight();
  return IsLeftToRightDirection() ? BorderTop() : BorderBottom();
}

BorderValue ComputedStyle::BorderEnd() const {
  if (IsHorizontalWritingMode())
    return IsLeftToRightDirection() ? BorderRight() : BorderLeft();
  return IsLeftToRightDirection() ? BorderBottom() : BorderTop();
}

float ComputedStyle::BorderBeforeWidth() const {
  switch (GetWritingMode()) {
    case WritingMode::kHorizontalTb:
      return BorderTopWidth();
    case WritingMode::kVerticalLr:
      return BorderLeftWidth();
    case WritingMode::kVerticalRl:
      return BorderRightWidth();
  }
  NOTREACHED();
  return BorderTopWidth();
}

float ComputedStyle::BorderAfterWidth() const {
  switch (GetWritingMode()) {
    case WritingMode::kHorizontalTb:
      return BorderBottomWidth();
    case WritingMode::kVerticalLr:
      return BorderRightWidth();
    case WritingMode::kVerticalRl:
      return BorderLeftWidth();
  }
  NOTREACHED();
  return BorderBottomWidth();
}

float ComputedStyle::BorderStartWidth() const {
  if (IsHorizontalWritingMode())
    return IsLeftToRightDirection() ? BorderLeftWidth() : BorderRightWidth();
  return IsLeftToRightDirection() ? BorderTopWidth() : BorderBottomWidth();
}

float ComputedStyle::BorderEndWidth() const {
  if (IsHorizontalWritingMode())
    return IsLeftToRightDirection() ? BorderRightWidth() : BorderLeftWidth();
  return IsLeftToRightDirection() ? BorderBottomWidth() : BorderTopWidth();
}

float ComputedStyle::BorderOverWidth() const {
  return IsHorizontalWritingMode() ? BorderTopWidth() : BorderRightWidth();
}

float ComputedStyle::BorderUnderWidth() const {
  return IsHorizontalWritingMode() ? BorderBottomWidth() : BorderLeftWidth();
}

void ComputedStyle::SetMarginStart(const Length& margin) {
  if (IsHorizontalWritingMode()) {
    if (IsLeftToRightDirection())
      SetMarginLeft(margin);
    else
      SetMarginRight(margin);
  } else {
    if (IsLeftToRightDirection())
      SetMarginTop(margin);
    else
      SetMarginBottom(margin);
  }
}

void ComputedStyle::SetMarginEnd(const Length& margin) {
  if (IsHorizontalWritingMode()) {
    if (IsLeftToRightDirection())
      SetMarginRight(margin);
    else
      SetMarginLeft(margin);
  } else {
    if (IsLeftToRightDirection())
      SetMarginBottom(margin);
    else
      SetMarginTop(margin);
  }
}

void ComputedStyle::SetOffsetPath(RefPtr<BasicShape> path) {
  rare_non_inherited_data_.Access()->transform_.Access()->motion_.path_ =
      std::move(path);
}

int ComputedStyle::OutlineOutsetExtent() const {
  if (!HasOutline())
    return 0;
  if (OutlineStyleIsAuto()) {
    return GraphicsContext::FocusRingOutsetExtent(
        OutlineOffset(), std::ceil(GetOutlineStrokeWidthForFocusRing()));
  }
  return std::max(0, SaturatedAddition(OutlineWidth(), OutlineOffset()));
}

float ComputedStyle::GetOutlineStrokeWidthForFocusRing() const {
#if OS(MACOSX)
  return OutlineWidth();
#else
  // Draw an outline with thickness in proportion to the zoom level, but never
  // less than 1 pixel so that it remains visible.
  return std::max(EffectiveZoom(), 1.f);
#endif
}

bool ComputedStyle::ColumnRuleEquivalent(
    const ComputedStyle* other_style) const {
  return ColumnRuleStyle() == other_style->ColumnRuleStyle() &&
         ColumnRuleWidth() == other_style->ColumnRuleWidth() &&
         VisitedDependentColor(CSSPropertyColumnRuleColor) ==
             other_style->VisitedDependentColor(CSSPropertyColumnRuleColor);
}

TextEmphasisMark ComputedStyle::GetTextEmphasisMark() const {
  TextEmphasisMark mark =
      static_cast<TextEmphasisMark>(rare_inherited_data_->text_emphasis_mark_);
  if (mark != kTextEmphasisMarkAuto)
    return mark;

  if (IsHorizontalWritingMode())
    return kTextEmphasisMarkDot;

  return kTextEmphasisMarkSesame;
}

Color ComputedStyle::InitialTapHighlightColor() {
  return LayoutTheme::TapHighlightColor();
}

const FilterOperations& ComputedStyle::InitialFilter() {
  DEFINE_STATIC_LOCAL(FilterOperationsWrapper, ops,
                      (FilterOperationsWrapper::Create()));
  return ops.Operations();
}

const FilterOperations& ComputedStyle::InitialBackdropFilter() {
  DEFINE_STATIC_LOCAL(FilterOperationsWrapper, ops,
                      (FilterOperationsWrapper::Create()));
  return ops.Operations();
}

LayoutRectOutsets ComputedStyle::ImageOutsets(
    const NinePieceImage& image) const {
  return LayoutRectOutsets(
      NinePieceImage::ComputeOutset(image.Outset().Top(), BorderTopWidth()),
      NinePieceImage::ComputeOutset(image.Outset().Right(), BorderRightWidth()),
      NinePieceImage::ComputeOutset(image.Outset().Bottom(),
                                    BorderBottomWidth()),
      NinePieceImage::ComputeOutset(image.Outset().Left(), BorderLeftWidth()));
}

void ComputedStyle::SetBorderImageSource(StyleImage* image) {
  if (BorderImage().GetImage() == image)
    return;
  MutableBorderImageInternal().SetImage(image);
}

void ComputedStyle::SetBorderImageSlices(const LengthBox& slices) {
  if (BorderImage().ImageSlices() == slices)
    return;
  MutableBorderImageInternal().SetImageSlices(slices);
}

void ComputedStyle::SetBorderImageSlicesFill(bool fill) {
  if (BorderImage().Fill() == fill)
    return;
  MutableBorderImageInternal().SetFill(fill);
}

void ComputedStyle::SetBorderImageWidth(const BorderImageLengthBox& slices) {
  if (BorderImage().BorderSlices() == slices)
    return;
  MutableBorderImageInternal().SetBorderSlices(slices);
}

void ComputedStyle::SetBorderImageOutset(const BorderImageLengthBox& outset) {
  if (BorderImage().Outset() == outset)
    return;
  MutableBorderImageInternal().SetOutset(outset);
}

bool ComputedStyle::BorderObscuresBackground() const {
  if (!HasBorder())
    return false;

  // Bail if we have any border-image for now. We could look at the image alpha
  // to improve this.
  if (BorderImage().GetImage())
    return false;

  BorderEdge edges[4];
  GetBorderEdgeInfo(edges);

  for (int i = kBSTop; i <= kBSLeft; ++i) {
    const BorderEdge& curr_edge = edges[i];
    if (!curr_edge.ObscuresBackground())
      return false;
  }

  return true;
}

void ComputedStyle::GetBorderEdgeInfo(BorderEdge edges[],
                                      bool include_logical_left_edge,
                                      bool include_logical_right_edge) const {
  bool horizontal = IsHorizontalWritingMode();

  edges[kBSTop] = BorderEdge(
      BorderTopWidth(), VisitedDependentColor(CSSPropertyBorderTopColor),
      BorderTopStyle(), horizontal || include_logical_left_edge);

  edges[kBSRight] = BorderEdge(
      BorderRightWidth(), VisitedDependentColor(CSSPropertyBorderRightColor),
      BorderRightStyle(), !horizontal || include_logical_right_edge);

  edges[kBSBottom] = BorderEdge(
      BorderBottomWidth(), VisitedDependentColor(CSSPropertyBorderBottomColor),
      BorderBottomStyle(), horizontal || include_logical_right_edge);

  edges[kBSLeft] = BorderEdge(
      BorderLeftWidth(), VisitedDependentColor(CSSPropertyBorderLeftColor),
      BorderLeftStyle(), !horizontal || include_logical_left_edge);
}

void ComputedStyle::CopyChildDependentFlagsFrom(const ComputedStyle& other) {
  SetEmptyState(other.EmptyState());
  if (other.HasExplicitlyInheritedProperties())
    SetHasExplicitlyInheritedProperties();
}

bool ComputedStyle::ShadowListHasCurrentColor(const ShadowList* shadow_list) {
  if (!shadow_list)
    return false;
  for (size_t i = shadow_list->Shadows().size(); i--;) {
    if (shadow_list->Shadows()[i].GetColor().IsCurrentColor())
      return true;
  }
  return false;
}

static inline Vector<GridTrackSize> InitialGridAutoTracks() {
  Vector<GridTrackSize> track_size_list;
  track_size_list.ReserveInitialCapacity(1);
  track_size_list.UncheckedAppend(GridTrackSize(Length(kAuto)));
  return track_size_list;
}

Vector<GridTrackSize> ComputedStyle::InitialGridAutoColumns() {
  return InitialGridAutoTracks();
}

Vector<GridTrackSize> ComputedStyle::InitialGridAutoRows() {
  return InitialGridAutoTracks();
}

int AdjustForAbsoluteZoom(int value, float zoom_factor) {
  if (zoom_factor == 1)
    return value;
  // Needed because computeLengthInt truncates (rather than rounds) when scaling
  // up.
  float fvalue = value;
  if (zoom_factor > 1) {
    if (value < 0)
      fvalue -= 0.5f;
    else
      fvalue += 0.5f;
  }

  return RoundForImpreciseConversion<int>(fvalue / zoom_factor);
}

}  // namespace blink
