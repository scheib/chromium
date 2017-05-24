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

#include "core/animation/CompositorAnimations.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include "core/animation/AnimationEffectReadOnly.h"
#include "core/animation/ElementAnimations.h"
#include "core/animation/KeyframeEffectModel.h"
#include "core/animation/animatable/AnimatableDouble.h"
#include "core/animation/animatable/AnimatableFilterOperations.h"
#include "core/animation/animatable/AnimatableTransform.h"
#include "core/animation/animatable/AnimatableValue.h"
#include "core/dom/DOMNodeIds.h"
#include "core/layout/LayoutBoxModelObject.h"
#include "core/layout/LayoutObject.h"
#include "core/layout/compositing/CompositedLayerMapping.h"
#include "core/paint/FilterEffectBuilder.h"
#include "core/paint/ObjectPaintProperties.h"
#include "core/paint/PaintLayer.h"
#include "platform/animation/AnimationTranslationUtil.h"
#include "platform/animation/CompositorAnimation.h"
#include "platform/animation/CompositorAnimationPlayer.h"
#include "platform/animation/CompositorFilterAnimationCurve.h"
#include "platform/animation/CompositorFilterKeyframe.h"
#include "platform/animation/CompositorFloatAnimationCurve.h"
#include "platform/animation/CompositorFloatKeyframe.h"
#include "platform/animation/CompositorTransformAnimationCurve.h"
#include "platform/animation/CompositorTransformKeyframe.h"
#include "platform/geometry/FloatBox.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/Platform.h"

namespace blink {

namespace {

bool ConsiderAnimationAsIncompatible(const Animation& animation,
                                     const Animation& animation_to_add) {
  if (&animation == &animation_to_add)
    return false;

  switch (animation.PlayStateInternal()) {
    case Animation::kIdle:
      return false;
    case Animation::kPending:
    case Animation::kRunning:
      return true;
    case Animation::kPaused:
    case Animation::kFinished:
      return Animation::HasLowerPriority(&animation_to_add, &animation);
    default:
      NOTREACHED();
      return true;
  }
}

bool IsTransformRelatedCSSProperty(const PropertyHandle property) {
  return property.IsCSSProperty() &&
         (property.CssProperty() == CSSPropertyRotate ||
          property.CssProperty() == CSSPropertyScale ||
          property.CssProperty() == CSSPropertyTransform ||
          property.CssProperty() == CSSPropertyTranslate);
}

bool IsTransformRelatedAnimation(const Element& target_element,
                                 const Animation* animation) {
  return animation->Affects(target_element, CSSPropertyTransform) ||
         animation->Affects(target_element, CSSPropertyRotate) ||
         animation->Affects(target_element, CSSPropertyScale) ||
         animation->Affects(target_element, CSSPropertyTranslate);
}

bool HasIncompatibleAnimations(const Element& target_element,
                               const Animation& animation_to_add,
                               const EffectModel& effect_to_add) {
  const bool affects_opacity =
      effect_to_add.Affects(PropertyHandle(CSSPropertyOpacity));
  const bool affects_transform = effect_to_add.IsTransformRelatedEffect();
  const bool affects_filter =
      effect_to_add.Affects(PropertyHandle(CSSPropertyFilter));
  const bool affects_backdrop_filter =
      effect_to_add.Affects(PropertyHandle(CSSPropertyBackdropFilter));

  if (!target_element.HasAnimations())
    return false;

  ElementAnimations* element_animations = target_element.GetElementAnimations();
  DCHECK(element_animations);

  for (const auto& entry : element_animations->Animations()) {
    const Animation* attached_animation = entry.key;
    if (!ConsiderAnimationAsIncompatible(*attached_animation, animation_to_add))
      continue;

    if ((affects_opacity &&
         attached_animation->Affects(target_element, CSSPropertyOpacity)) ||
        (affects_transform &&
         IsTransformRelatedAnimation(target_element, attached_animation)) ||
        (affects_filter &&
         attached_animation->Affects(target_element, CSSPropertyFilter)) ||
        (affects_backdrop_filter &&
         attached_animation->Affects(target_element,
                                     CSSPropertyBackdropFilter)))
      return true;
  }

  return false;
}

}  // namespace

bool CompositorAnimations::IsCompositableProperty(CSSPropertyID property) {
  for (CSSPropertyID id : kCompositableProperties) {
    if (property == id)
      return true;
  }
  return false;
}

const CSSPropertyID CompositorAnimations::kCompositableProperties[7] = {
    CSSPropertyOpacity,       CSSPropertyRotate,    CSSPropertyScale,
    CSSPropertyTransform,     CSSPropertyTranslate, CSSPropertyFilter,
    CSSPropertyBackdropFilter};

bool CompositorAnimations::GetAnimatedBoundingBox(FloatBox& box,
                                                  const EffectModel& effect,
                                                  double min_value,
                                                  double max_value) {
  const KeyframeEffectModelBase& keyframe_effect =
      ToKeyframeEffectModelBase(effect);

  PropertyHandleSet properties = keyframe_effect.Properties();

  if (properties.IsEmpty())
    return true;

  min_value = std::min(min_value, 0.0);
  max_value = std::max(max_value, 1.0);

  for (const auto& property : properties) {
    if (!property.IsCSSProperty())
      continue;

    // TODO: Add the ability to get expanded bounds for filters as well.
    if (!IsTransformRelatedCSSProperty(property))
      continue;

    const PropertySpecificKeyframeVector& frames =
        keyframe_effect.GetPropertySpecificKeyframes(property);
    if (frames.IsEmpty() || frames.size() < 2)
      continue;

    FloatBox original_box(box);

    for (size_t j = 0; j < frames.size() - 1; ++j) {
      const AnimatableTransform* start_transform =
          ToAnimatableTransform(frames[j]->GetAnimatableValue().Get());
      const AnimatableTransform* end_transform =
          ToAnimatableTransform(frames[j + 1]->GetAnimatableValue().Get());
      if (!start_transform || !end_transform)
        return false;

      // TODO: Add support for inflating modes other than Replace.
      if (frames[j]->Composite() != EffectModel::kCompositeReplace)
        return false;

      const TimingFunction& timing = frames[j]->Easing();
      double min = 0;
      double max = 1;
      if (j == 0) {
        float frame_length = frames[j + 1]->Offset();
        if (frame_length > 0) {
          min = min_value / frame_length;
        }
      }

      if (j == frames.size() - 2) {
        float frame_length = frames[j + 1]->Offset() - frames[j]->Offset();
        if (frame_length > 0) {
          max = 1 + (max_value - 1) / frame_length;
        }
      }

      FloatBox bounds;
      timing.Range(&min, &max);
      if (!end_transform->GetTransformOperations().BlendedBoundsForBox(
              original_box, start_transform->GetTransformOperations(), min, max,
              &bounds))
        return false;
      box.ExpandTo(bounds);
    }
  }
  return true;
}

bool CompositorAnimations::IsCandidateForAnimationOnCompositor(
    const Timing& timing,
    const Element& target_element,
    const Animation* animation_to_add,
    const EffectModel& effect,
    double animation_playback_rate) {
  const KeyframeEffectModelBase& keyframe_effect =
      ToKeyframeEffectModelBase(effect);

  PropertyHandleSet properties = keyframe_effect.Properties();
  if (properties.IsEmpty())
    return false;

  unsigned transform_property_count = 0;
  for (const auto& property : properties) {
    if (!property.IsCSSProperty())
      return false;

    if (IsTransformRelatedCSSProperty(property)) {
      if (target_element.GetLayoutObject() &&
          !target_element.GetLayoutObject()->IsTransformApplicable()) {
        return false;
      }
      transform_property_count++;
    }

    const PropertySpecificKeyframeVector& keyframes =
        keyframe_effect.GetPropertySpecificKeyframes(property);
    DCHECK_GE(keyframes.size(), 2U);
    for (const auto& keyframe : keyframes) {
      // FIXME: Determine candidacy based on the CSSValue instead of a snapshot
      // AnimatableValue.
      bool is_neutral_keyframe =
          keyframe->IsCSSPropertySpecificKeyframe() &&
          !ToCSSPropertySpecificKeyframe(keyframe.Get())->Value() &&
          keyframe->Composite() == EffectModel::kCompositeAdd;
      if ((keyframe->Composite() != EffectModel::kCompositeReplace &&
           !is_neutral_keyframe) ||
          !keyframe->GetAnimatableValue())
        return false;

      switch (property.CssProperty()) {
        case CSSPropertyOpacity:
          break;
        case CSSPropertyRotate:
        case CSSPropertyScale:
        case CSSPropertyTranslate:
        case CSSPropertyTransform:
          if (ToAnimatableTransform(keyframe->GetAnimatableValue().Get())
                  ->GetTransformOperations()
                  .DependsOnBoxSize())
            return false;
          break;
        case CSSPropertyFilter:
        case CSSPropertyBackdropFilter: {
          const FilterOperations& operations =
              ToAnimatableFilterOperations(keyframe->GetAnimatableValue().Get())
                  ->Operations();
          if (operations.HasFilterThatMovesPixels())
            return false;
          break;
        }
        default:
          // any other types are not allowed to run on compositor.
          return false;
      }
    }
  }

  // TODO: Support multiple transform property animations on the compositor
  if (transform_property_count > 1)
    return false;

  if (animation_to_add &&
      HasIncompatibleAnimations(target_element, *animation_to_add, effect))
    return false;

  CompositorTiming out;
  if (!ConvertTimingForCompositor(timing, 0, out, animation_playback_rate))
    return false;

  return true;
}

void CompositorAnimations::CancelIncompatibleAnimationsOnCompositor(
    const Element& target_element,
    const Animation& animation_to_add,
    const EffectModel& effect_to_add) {
  const bool affects_opacity =
      effect_to_add.Affects(PropertyHandle(CSSPropertyOpacity));
  const bool affects_transform = effect_to_add.IsTransformRelatedEffect();
  const bool affects_filter =
      effect_to_add.Affects(PropertyHandle(CSSPropertyFilter));
  const bool affects_backdrop_filter =
      effect_to_add.Affects(PropertyHandle(CSSPropertyBackdropFilter));

  if (!target_element.HasAnimations())
    return;

  ElementAnimations* element_animations = target_element.GetElementAnimations();
  DCHECK(element_animations);

  for (const auto& entry : element_animations->Animations()) {
    Animation* attached_animation = entry.key;
    if (!ConsiderAnimationAsIncompatible(*attached_animation, animation_to_add))
      continue;

    if ((affects_opacity &&
         attached_animation->Affects(target_element, CSSPropertyOpacity)) ||
        (affects_transform &&
         IsTransformRelatedAnimation(target_element, attached_animation)) ||
        (affects_filter &&
         attached_animation->Affects(target_element, CSSPropertyFilter)) ||
        (affects_backdrop_filter &&
         attached_animation->Affects(target_element,
                                     CSSPropertyBackdropFilter)))
      attached_animation->CancelAnimationOnCompositor();
  }
}

bool CompositorAnimations::CanStartAnimationOnCompositor(
    const Element& element) {
  if (!Platform::Current()->IsThreadedAnimationEnabled())
    return false;

  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    // We query paint property tree state below to determine whether the
    // animation is compositable. There is a known lifecycle violation where an
    // animation can be cancelled during style update. See
    // CompositorAnimations::cancelAnimationOnCompositor and
    // http://crbug.com/676456. When this is fixed we would like to enable
    // the DCHECK below.
    // DCHECK(document().lifecycle().state() >=
    // DocumentLifecycle::PrePaintClean);
    const ObjectPaintProperties* paint_properties =
        element.GetLayoutObject()->PaintProperties();
    const TransformPaintPropertyNode* transform_node =
        paint_properties->Transform();
    const EffectPaintPropertyNode* effect_node = paint_properties->Effect();
    return (transform_node && transform_node->HasDirectCompositingReasons()) ||
           (effect_node && effect_node->HasDirectCompositingReasons());
  }

  return element.GetLayoutObject() &&
         element.GetLayoutObject()->GetCompositingState() ==
             kPaintsIntoOwnBacking;
}

void CompositorAnimations::StartAnimationOnCompositor(
    const Element& element,
    int group,
    double start_time,
    double time_offset,
    const Timing& timing,
    const Animation& animation,
    const EffectModel& effect,
    Vector<int>& started_animation_ids,
    double animation_playback_rate) {
  DCHECK(started_animation_ids.IsEmpty());
  DCHECK(IsCandidateForAnimationOnCompositor(timing, element, &animation,
                                             effect, animation_playback_rate));
  DCHECK(CanStartAnimationOnCompositor(element));

  const KeyframeEffectModelBase& keyframe_effect =
      ToKeyframeEffectModelBase(effect);

  Vector<std::unique_ptr<CompositorAnimation>> animations;
  GetAnimationOnCompositor(timing, group, start_time, time_offset,
                           keyframe_effect, animations,
                           animation_playback_rate);
  DCHECK(!animations.IsEmpty());
  for (auto& compositor_animation : animations) {
    int id = compositor_animation->Id();
    CompositorAnimationPlayer* compositor_player = animation.CompositorPlayer();
    DCHECK(compositor_player);
    compositor_player->AddAnimation(std::move(compositor_animation));
    started_animation_ids.push_back(id);
  }
  DCHECK(!started_animation_ids.IsEmpty());
}

void CompositorAnimations::CancelAnimationOnCompositor(
    const Element& element,
    const Animation& animation,
    int id) {
  if (!CanStartAnimationOnCompositor(element)) {
    // When an element is being detached, we cancel any associated
    // Animations for CSS animations. But by the time we get
    // here the mapping will have been removed.
    // FIXME: Defer remove/pause operations until after the
    // compositing update.
    return;
  }
  CompositorAnimationPlayer* compositor_player = animation.CompositorPlayer();
  if (compositor_player)
    compositor_player->RemoveAnimation(id);
}

void CompositorAnimations::PauseAnimationForTestingOnCompositor(
    const Element& element,
    const Animation& animation,
    int id,
    double pause_time) {
  // FIXME: canStartAnimationOnCompositor queries compositingState, which is not
  // necessarily up to date.
  // https://code.google.com/p/chromium/issues/detail?id=339847
  DisableCompositingQueryAsserts disabler;

  if (!CanStartAnimationOnCompositor(element)) {
    NOTREACHED();
    return;
  }
  CompositorAnimationPlayer* compositor_player = animation.CompositorPlayer();
  DCHECK(compositor_player);
  compositor_player->PauseAnimation(id, pause_time);
}

void CompositorAnimations::AttachCompositedLayers(Element& element,
                                                  const Animation& animation) {
  if (!animation.CompositorPlayer())
    return;

  if (!element.GetLayoutObject() ||
      !element.GetLayoutObject()->IsBoxModelObject() ||
      !element.GetLayoutObject()->HasLayer())
    return;

  PaintLayer* layer =
      ToLayoutBoxModelObject(element.GetLayoutObject())->Layer();

  // Composited animations do not depend on a composited layer mapping for SPv2.
  if (!RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    if (!layer->IsAllowedToQueryCompositingState() ||
        !layer->GetCompositedLayerMapping() ||
        !layer->GetCompositedLayerMapping()->MainGraphicsLayer())
      return;

    if (!layer->GetCompositedLayerMapping()
             ->MainGraphicsLayer()
             ->PlatformLayer())
      return;
  }

  CompositorAnimationPlayer* compositor_player = animation.CompositorPlayer();
  compositor_player->AttachElement(CompositorElementIdFromPaintLayerId(
      layer->UniqueId(), CompositorElementIdNamespace::kPrimary));
}

bool CompositorAnimations::ConvertTimingForCompositor(
    const Timing& timing,
    double time_offset,
    CompositorTiming& out,
    double animation_playback_rate) {
  timing.AssertValid();

  // FIXME: Compositor does not know anything about endDelay.
  if (timing.end_delay != 0)
    return false;

  if (std::isnan(timing.iteration_duration) || !timing.iteration_count ||
      !timing.iteration_duration)
    return false;

  out.adjusted_iteration_count =
      std::isfinite(timing.iteration_count) ? timing.iteration_count : -1;
  out.scaled_duration = timing.iteration_duration;
  out.direction = timing.direction;
  // Compositor's time offset is positive for seeking into the animation.
  out.scaled_time_offset =
      -timing.start_delay / animation_playback_rate + time_offset;
  out.playback_rate = timing.playback_rate * animation_playback_rate;
  out.fill_mode = timing.fill_mode == Timing::FillMode::AUTO
                      ? Timing::FillMode::NONE
                      : timing.fill_mode;
  out.iteration_start = timing.iteration_start;

  DCHECK_GT(out.scaled_duration, 0);
  DCHECK(std::isfinite(out.scaled_time_offset));
  DCHECK(out.adjusted_iteration_count > 0 ||
         out.adjusted_iteration_count == -1);
  DCHECK(std::isfinite(out.playback_rate) && out.playback_rate);
  DCHECK_GE(out.iteration_start, 0);

  return true;
}

namespace {

void AddKeyframeToCurve(CompositorFilterAnimationCurve& curve,
                        Keyframe::PropertySpecificKeyframe* keyframe,
                        const AnimatableValue* value,
                        const TimingFunction& keyframe_timing_function) {
  FilterEffectBuilder builder(nullptr, FloatRect(), 1);
  CompositorFilterKeyframe filter_keyframe(
      keyframe->Offset(),
      builder.BuildFilterOperations(
          ToAnimatableFilterOperations(value)->Operations()),
      keyframe_timing_function);
  curve.AddKeyframe(filter_keyframe);
}

void AddKeyframeToCurve(CompositorFloatAnimationCurve& curve,
                        Keyframe::PropertySpecificKeyframe* keyframe,
                        const AnimatableValue* value,
                        const TimingFunction& keyframe_timing_function) {
  CompositorFloatKeyframe float_keyframe(keyframe->Offset(),
                                         ToAnimatableDouble(value)->ToDouble(),
                                         keyframe_timing_function);
  curve.AddKeyframe(float_keyframe);
}

void AddKeyframeToCurve(CompositorTransformAnimationCurve& curve,
                        Keyframe::PropertySpecificKeyframe* keyframe,
                        const AnimatableValue* value,
                        const TimingFunction& keyframe_timing_function) {
  CompositorTransformOperations ops;
  ToCompositorTransformOperations(
      ToAnimatableTransform(value)->GetTransformOperations(), &ops);

  CompositorTransformKeyframe transform_keyframe(
      keyframe->Offset(), std::move(ops), keyframe_timing_function);
  curve.AddKeyframe(transform_keyframe);
}

template <typename PlatformAnimationCurveType>
void AddKeyframesToCurve(PlatformAnimationCurveType& curve,
                         const PropertySpecificKeyframeVector& keyframes) {
  auto* last_keyframe = keyframes.back().Get();
  for (const auto& keyframe : keyframes) {
    const TimingFunction* keyframe_timing_function = 0;
    // Ignore timing function of last frame.
    if (keyframe == last_keyframe)
      keyframe_timing_function = LinearTimingFunction::Shared();
    else
      keyframe_timing_function = &keyframe->Easing();

    const AnimatableValue* value = keyframe->GetAnimatableValue().Get();
    AddKeyframeToCurve(curve, keyframe.Get(), value, *keyframe_timing_function);
  }
}

}  // namespace

void CompositorAnimations::GetAnimationOnCompositor(
    const Timing& timing,
    int group,
    double start_time,
    double time_offset,
    const KeyframeEffectModelBase& effect,
    Vector<std::unique_ptr<CompositorAnimation>>& animations,
    double animation_playback_rate) {
  DCHECK(animations.IsEmpty());
  CompositorTiming compositor_timing;
  bool timing_valid = ConvertTimingForCompositor(
      timing, time_offset, compositor_timing, animation_playback_rate);
  ALLOW_UNUSED_LOCAL(timing_valid);

  PropertyHandleSet properties = effect.Properties();
  DCHECK(!properties.IsEmpty());
  for (const auto& property : properties) {
    // If the animation duration is infinite, it doesn't make sense to scale
    // the keyframe offset, so use a scale of 1.0. This is connected to
    // the known issue of how the Web Animations spec handles infinite
    // durations. See https://github.com/w3c/web-animations/issues/142
    double scale = compositor_timing.scaled_duration;
    if (!std::isfinite(scale))
      scale = 1.0;
    const PropertySpecificKeyframeVector& values =
        effect.GetPropertySpecificKeyframes(property);

    CompositorTargetProperty::Type target_property;
    std::unique_ptr<CompositorAnimationCurve> curve;
    DCHECK(timing.timing_function);
    switch (property.CssProperty()) {
      case CSSPropertyOpacity: {
        target_property = CompositorTargetProperty::OPACITY;
        std::unique_ptr<CompositorFloatAnimationCurve> float_curve =
            CompositorFloatAnimationCurve::Create();
        AddKeyframesToCurve(*float_curve, values);
        float_curve->SetTimingFunction(*timing.timing_function);
        float_curve->SetScaledDuration(scale);
        curve = std::move(float_curve);
        break;
      }
      case CSSPropertyFilter:
      case CSSPropertyBackdropFilter: {
        target_property = CompositorTargetProperty::FILTER;
        std::unique_ptr<CompositorFilterAnimationCurve> filter_curve =
            CompositorFilterAnimationCurve::Create();
        AddKeyframesToCurve(*filter_curve, values);
        filter_curve->SetTimingFunction(*timing.timing_function);
        filter_curve->SetScaledDuration(scale);
        curve = std::move(filter_curve);
        break;
      }
      case CSSPropertyRotate:
      case CSSPropertyScale:
      case CSSPropertyTranslate:
      case CSSPropertyTransform: {
        target_property = CompositorTargetProperty::TRANSFORM;
        std::unique_ptr<CompositorTransformAnimationCurve> transform_curve =
            CompositorTransformAnimationCurve::Create();
        AddKeyframesToCurve(*transform_curve, values);
        transform_curve->SetTimingFunction(*timing.timing_function);
        transform_curve->SetScaledDuration(scale);
        curve = std::move(transform_curve);
        break;
      }
      default:
        NOTREACHED();
        continue;
    }
    DCHECK(curve.get());

    std::unique_ptr<CompositorAnimation> animation =
        CompositorAnimation::Create(*curve, target_property, group, 0);

    if (!std::isnan(start_time))
      animation->SetStartTime(start_time);

    animation->SetIterations(compositor_timing.adjusted_iteration_count);
    animation->SetIterationStart(compositor_timing.iteration_start);
    animation->SetTimeOffset(compositor_timing.scaled_time_offset);
    animation->SetDirection(compositor_timing.direction);
    animation->SetPlaybackRate(compositor_timing.playback_rate);
    animation->SetFillMode(compositor_timing.fill_mode);
    animations.push_back(std::move(animation));
  }
  DCHECK(!animations.IsEmpty());
}

}  // namespace blink
