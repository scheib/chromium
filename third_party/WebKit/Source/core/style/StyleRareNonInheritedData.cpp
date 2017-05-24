/*
 * Copyright (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
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

#include "core/style/StyleRareNonInheritedData.h"

#include "core/animation/css/CSSAnimationData.h"
#include "core/animation/css/CSSTransitionData.h"
#include "core/style/ContentData.h"
#include "core/style/DataEquivalency.h"
#include "core/style/ComputedStyle.h"
#include "core/style/ShadowList.h"
#include "core/style/StyleFilterData.h"
#include "core/style/StyleNonInheritedVariables.h"
#include "core/style/StyleTransformData.h"

namespace blink {

class SameSizeStyleRareNonInheritedData
    : public RefCounted<StyleRareNonInheritedData> {
 public:
  float floats[3];
  int integers;

  LengthPoint length_points[2];
  LineClampValue line_clamps;
  DraggableRegionMode draggable_regions;

  void* data_refs[8];
  DataPersistent<void*> data_persistents[2];
  void* own_ptrs[4];
  Persistent<void*> persistent_handles[2];
  void* ref_ptrs[3];
  void* unique_ptrs[1];

  FillLayer fill_layers;
  NinePieceImage nine_pieces;
  FloatSize float_size;
  Length lengths;
  OutlineValue outline;

  StyleColor style_colors[8];

  Vector<String> callback_selectors_;

  StyleContentAlignmentData content_alignment_data[2];
  StyleSelfAlignmentData self_alignment_data[4];

  unsigned bit_fields_[2];
};

static_assert(sizeof(StyleRareNonInheritedData) ==
                  sizeof(SameSizeStyleRareNonInheritedData),
              "StyleRareNonInheritedData_should_stay_small");

StyleRareNonInheritedData::StyleRareNonInheritedData()
    : opacity(ComputedStyle::InitialOpacity()),
      perspective_(ComputedStyle::InitialPerspective()),
      shape_image_threshold_(ComputedStyle::InitialShapeImageThreshold()),
      order_(ComputedStyle::InitialOrder()),
      perspective_origin_(ComputedStyle::InitialPerspectiveOrigin()),
      object_position_(ComputedStyle::InitialObjectPosition()),
      line_clamp(ComputedStyle::InitialLineClamp()),
      draggable_region_mode_(kDraggableRegionNone),
      shape_outside_(ComputedStyle::InitialShapeOutside()),
      clip_path_(ComputedStyle::InitialClipPath()),
      mask_(kMaskFillLayer, true),
      page_size_(),
      shape_margin_(ComputedStyle::InitialShapeMargin()),
      text_decoration_color_(StyleColor::CurrentColor()),
      visited_link_text_decoration_color_(StyleColor::CurrentColor()),
      visited_link_background_color_(ComputedStyle::InitialBackgroundColor()),
      visited_link_outline_color_(StyleColor::CurrentColor()),
      visited_link_border_left_color_(StyleColor::CurrentColor()),
      visited_link_border_right_color_(StyleColor::CurrentColor()),
      visited_link_border_top_color_(StyleColor::CurrentColor()),
      visited_link_border_bottom_color_(StyleColor::CurrentColor()),
      variables_(ComputedStyle::InitialNonInheritedVariables()),
      align_content_(ComputedStyle::InitialContentAlignment()),
      align_items_(ComputedStyle::InitialDefaultAlignment()),
      align_self_(ComputedStyle::InitialSelfAlignment()),
      justify_content_(ComputedStyle::InitialContentAlignment()),
      justify_items_(ComputedStyle::InitialSelfAlignment()),
      justify_self_(ComputedStyle::InitialSelfAlignment()),
      page_size_type_(PAGE_SIZE_AUTO),
      transform_style3d_(ComputedStyle::InitialTransformStyle3D()),
      backface_visibility_(ComputedStyle::InitialBackfaceVisibility()),
      user_drag(ComputedStyle::InitialUserDrag()),
      text_overflow(ComputedStyle::InitialTextOverflow()),
      margin_before_collapse(kMarginCollapseCollapse),
      margin_after_collapse(kMarginCollapseCollapse),
      appearance_(ComputedStyle::InitialAppearance()),
      text_decoration_style_(ComputedStyle::InitialTextDecorationStyle()),
      has_current_opacity_animation_(false),
      has_current_transform_animation_(false),
      has_current_filter_animation_(false),
      has_current_backdrop_filter_animation_(false),
      running_opacity_animation_on_compositor_(false),
      running_transform_animation_on_compositor_(false),
      running_filter_animation_on_compositor_(false),
      running_backdrop_filter_animation_on_compositor_(false),
      is_stacking_context_(false),
      effective_blend_mode_(ComputedStyle::InitialBlendMode()),
      touch_action_(ComputedStyle::InitialTouchAction()),
      object_fit_(ComputedStyle::InitialObjectFit()),
      isolation_(ComputedStyle::InitialIsolation()),
      contain_(ComputedStyle::InitialContain()),
      scroll_behavior_(ComputedStyle::InitialScrollBehavior()),
      scroll_snap_type_(ComputedStyle::InitialScrollSnapType()),
      requires_accelerated_compositing_for_external_reasons_(false),
      has_inline_transform_(false),
      resize_(ComputedStyle::InitialResize()),
      has_compositor_proxy_(false),
      has_author_background_(false),
      has_author_border_(false) {
  mask_box_image_.SetMaskDefaults();
}

StyleRareNonInheritedData::StyleRareNonInheritedData(
    const StyleRareNonInheritedData& o)
    : RefCounted<StyleRareNonInheritedData>(),
      opacity(o.opacity),
      perspective_(o.perspective_),
      shape_image_threshold_(o.shape_image_threshold_),
      order_(o.order_),
      perspective_origin_(o.perspective_origin_),
      object_position_(o.object_position_),
      line_clamp(o.line_clamp),
      draggable_region_mode_(o.draggable_region_mode_),
      deprecated_flexible_box_(o.deprecated_flexible_box_),
      flexible_box_(o.flexible_box_),
      multi_col_(o.multi_col_),
      transform_(o.transform_),
      will_change_(o.will_change_),
      filter_(o.filter_),
      backdrop_filter_(o.backdrop_filter_),
      grid_(o.grid_),
      grid_item_(o.grid_item_),
      scroll_snap_(o.scroll_snap_),
      content_(o.content_ ? o.content_->Clone() : nullptr),
      counter_directives_(o.counter_directives_ ? Clone(*o.counter_directives_)
                                                : nullptr),
      animations_(o.animations_ ? o.animations_->Clone() : nullptr),
      transitions_(o.transitions_ ? o.transitions_->Clone() : nullptr),
      box_shadow_(o.box_shadow_),
      box_reflect_(o.box_reflect_),
      shape_outside_(o.shape_outside_),
      clip_path_(o.clip_path_),
      mask_(o.mask_),
      mask_box_image_(o.mask_box_image_),
      page_size_(o.page_size_),
      shape_margin_(o.shape_margin_),
      outline_(o.outline_),
      text_decoration_color_(o.text_decoration_color_),
      visited_link_text_decoration_color_(
          o.visited_link_text_decoration_color_),
      visited_link_background_color_(o.visited_link_background_color_),
      visited_link_outline_color_(o.visited_link_outline_color_),
      visited_link_border_left_color_(o.visited_link_border_left_color_),
      visited_link_border_right_color_(o.visited_link_border_right_color_),
      visited_link_border_top_color_(o.visited_link_border_top_color_),
      visited_link_border_bottom_color_(o.visited_link_border_bottom_color_),
      variables_(o.variables_ ? o.variables_->Copy() : nullptr),
      align_content_(o.align_content_),
      align_items_(o.align_items_),
      align_self_(o.align_self_),
      justify_content_(o.justify_content_),
      justify_items_(o.justify_items_),
      justify_self_(o.justify_self_),
      page_size_type_(o.page_size_type_),
      transform_style3d_(o.transform_style3d_),
      backface_visibility_(o.backface_visibility_),
      user_drag(o.user_drag),
      text_overflow(o.text_overflow),
      margin_before_collapse(o.margin_before_collapse),
      margin_after_collapse(o.margin_after_collapse),
      appearance_(o.appearance_),
      text_decoration_style_(o.text_decoration_style_),
      has_current_opacity_animation_(o.has_current_opacity_animation_),
      has_current_transform_animation_(o.has_current_transform_animation_),
      has_current_filter_animation_(o.has_current_filter_animation_),
      has_current_backdrop_filter_animation_(
          o.has_current_backdrop_filter_animation_),
      running_opacity_animation_on_compositor_(
          o.running_opacity_animation_on_compositor_),
      running_transform_animation_on_compositor_(
          o.running_transform_animation_on_compositor_),
      running_filter_animation_on_compositor_(
          o.running_filter_animation_on_compositor_),
      running_backdrop_filter_animation_on_compositor_(
          o.running_backdrop_filter_animation_on_compositor_),
      is_stacking_context_(o.is_stacking_context_),
      effective_blend_mode_(o.effective_blend_mode_),
      touch_action_(o.touch_action_),
      object_fit_(o.object_fit_),
      isolation_(o.isolation_),
      contain_(o.contain_),
      scroll_behavior_(o.scroll_behavior_),
      scroll_snap_type_(o.scroll_snap_type_),
      requires_accelerated_compositing_for_external_reasons_(
          o.requires_accelerated_compositing_for_external_reasons_),
      has_inline_transform_(o.has_inline_transform_),
      resize_(o.resize_),
      has_compositor_proxy_(o.has_compositor_proxy_),
      has_author_background_(o.has_author_background_),
      has_author_border_(o.has_author_border_) {}

StyleRareNonInheritedData::~StyleRareNonInheritedData() {}

bool StyleRareNonInheritedData::operator==(
    const StyleRareNonInheritedData& o) const {
  return opacity == o.opacity && perspective_ == o.perspective_ &&
         shape_image_threshold_ == o.shape_image_threshold_ &&
         order_ == o.order_ && perspective_origin_ == o.perspective_origin_ &&
         object_position_ == o.object_position_ && line_clamp == o.line_clamp &&
         draggable_region_mode_ == o.draggable_region_mode_ &&
         deprecated_flexible_box_ == o.deprecated_flexible_box_ &&
         flexible_box_ == o.flexible_box_ && multi_col_ == o.multi_col_ &&
         transform_ == o.transform_ && will_change_ == o.will_change_ &&
         filter_ == o.filter_ && backdrop_filter_ == o.backdrop_filter_ &&
         grid_ == o.grid_ && grid_item_ == o.grid_item_ &&
         scroll_snap_ == o.scroll_snap_ && ContentDataEquivalent(o) &&
         CounterDataEquivalent(o) && ShadowDataEquivalent(o) &&
         ReflectionDataEquivalent(o) && AnimationDataEquivalent(o) &&
         TransitionDataEquivalent(o) && ShapeOutsideDataEquivalent(o) &&
         mask_ == o.mask_ && mask_box_image_ == o.mask_box_image_ &&
         page_size_ == o.page_size_ && shape_margin_ == o.shape_margin_ &&
         outline_ == o.outline_ && ClipPathDataEquivalent(o) &&
         text_decoration_color_ == o.text_decoration_color_ &&
         visited_link_text_decoration_color_ ==
             o.visited_link_text_decoration_color_ &&
         visited_link_background_color_ == o.visited_link_background_color_ &&
         visited_link_outline_color_ == o.visited_link_outline_color_ &&
         visited_link_border_left_color_ == o.visited_link_border_left_color_ &&
         visited_link_border_right_color_ ==
             o.visited_link_border_right_color_ &&
         visited_link_border_top_color_ == o.visited_link_border_top_color_ &&
         visited_link_border_bottom_color_ ==
             o.visited_link_border_bottom_color_ &&
         callback_selectors_ == o.callback_selectors_ &&
         DataEquivalent(variables_, o.variables_) &&
         align_content_ == o.align_content_ && align_items_ == o.align_items_ &&
         align_self_ == o.align_self_ &&
         justify_content_ == o.justify_content_ &&
         justify_items_ == o.justify_items_ &&
         justify_self_ == o.justify_self_ &&
         page_size_type_ == o.page_size_type_ &&
         transform_style3d_ == o.transform_style3d_ &&
         backface_visibility_ == o.backface_visibility_ &&
         user_drag == o.user_drag && text_overflow == o.text_overflow &&
         margin_before_collapse == o.margin_before_collapse &&
         margin_after_collapse == o.margin_after_collapse &&
         appearance_ == o.appearance_ &&
         text_decoration_style_ == o.text_decoration_style_ &&
         has_current_opacity_animation_ == o.has_current_opacity_animation_ &&
         has_current_transform_animation_ ==
             o.has_current_transform_animation_ &&
         has_current_filter_animation_ == o.has_current_filter_animation_ &&
         has_current_backdrop_filter_animation_ ==
             o.has_current_backdrop_filter_animation_ &&
         is_stacking_context_ == o.is_stacking_context_ &&
         effective_blend_mode_ == o.effective_blend_mode_ &&
         touch_action_ == o.touch_action_ && object_fit_ == o.object_fit_ &&
         isolation_ == o.isolation_ && contain_ == o.contain_ &&
         scroll_behavior_ == o.scroll_behavior_ &&
         scroll_snap_type_ == o.scroll_snap_type_ &&
         requires_accelerated_compositing_for_external_reasons_ ==
             o.requires_accelerated_compositing_for_external_reasons_ &&
         has_inline_transform_ == o.has_inline_transform_ &&
         resize_ == o.resize_ &&
         has_compositor_proxy_ == o.has_compositor_proxy_ &&
         has_author_background_ == o.has_author_background_ &&
         has_author_border_ == o.has_author_border_;
}

bool StyleRareNonInheritedData::ContentDataEquivalent(
    const StyleRareNonInheritedData& o) const {
  ContentData* a = content_.Get();
  ContentData* b = o.content_.Get();

  while (a && b && *a == *b) {
    a = a->Next();
    b = b->Next();
  }

  return !a && !b;
}

bool StyleRareNonInheritedData::CounterDataEquivalent(
    const StyleRareNonInheritedData& o) const {
  return DataEquivalent(counter_directives_, o.counter_directives_);
}

bool StyleRareNonInheritedData::ShadowDataEquivalent(
    const StyleRareNonInheritedData& o) const {
  return DataEquivalent(box_shadow_, o.box_shadow_);
}

bool StyleRareNonInheritedData::ReflectionDataEquivalent(
    const StyleRareNonInheritedData& o) const {
  return DataEquivalent(box_reflect_, o.box_reflect_);
}

bool StyleRareNonInheritedData::AnimationDataEquivalent(
    const StyleRareNonInheritedData& o) const {
  if (!animations_ && !o.animations_)
    return true;
  if (!animations_ || !o.animations_)
    return false;
  return animations_->AnimationsMatchForStyleRecalc(*o.animations_);
}

bool StyleRareNonInheritedData::TransitionDataEquivalent(
    const StyleRareNonInheritedData& o) const {
  if (!transitions_ && !o.transitions_)
    return true;
  if (!transitions_ || !o.transitions_)
    return false;
  return transitions_->TransitionsMatchForStyleRecalc(*o.transitions_);
}

bool StyleRareNonInheritedData::HasFilters() const {
  return filter_.Get() && !filter_->operations_.IsEmpty();
}

bool StyleRareNonInheritedData::HasBackdropFilters() const {
  return backdrop_filter_.Get() && !backdrop_filter_->operations_.IsEmpty();
}

bool StyleRareNonInheritedData::ShapeOutsideDataEquivalent(
    const StyleRareNonInheritedData& o) const {
  return DataEquivalent(shape_outside_, o.shape_outside_);
}

bool StyleRareNonInheritedData::ClipPathDataEquivalent(
    const StyleRareNonInheritedData& o) const {
  return DataEquivalent(clip_path_, o.clip_path_);
}

}  // namespace blink
