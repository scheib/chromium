/*
 * Copyright (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010 Apple Inc. All rights
 * reserved.
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

#include "core/style/StyleRareInheritedData.h"

#include "core/style/AppliedTextDecoration.h"
#include "core/style/CursorData.h"
#include "core/style/DataEquivalency.h"
#include "core/style/ComputedStyle.h"
#include "core/style/ComputedStyleConstants.h"
#include "core/style/QuotesData.h"
#include "core/style/ShadowList.h"
#include "core/style/StyleImage.h"
#include "core/style/StyleInheritedVariables.h"

namespace blink {

struct SameSizeAsStyleRareInheritedData
    : public RefCounted<SameSizeAsStyleRareInheritedData> {
  void* style_image;
  Color first_color;
  float first_float;
  Color colors[7];
  void* own_ptrs[1];
  AtomicString atomic_strings[3];
  void* ref_ptrs[1];
  Persistent<void*> persistent_handles[2];
  Length lengths[1];
  float second_float;
  unsigned bitfields_[2];
  short paged_media_shorts[2];
  short hyphenation_shorts[3];
  uint8_t line_height_step;

  Color touch_colors;
  TabSize tab_size;
  void* variables[1];
  TextSizeAdjust text_size_adjust;
};

static_assert(sizeof(StyleRareInheritedData) <=
                  sizeof(SameSizeAsStyleRareInheritedData),
              "StyleRareInheritedData should stay small");

StyleRareInheritedData::StyleRareInheritedData()
    : list_style_image_(ComputedStyle::InitialListStyleImage()),
      text_stroke_width_(ComputedStyle::InitialTextStrokeWidth()),
      text_indent_(ComputedStyle::InitialTextIndent()),
      effective_zoom_(ComputedStyle::InitialZoom()),
      widows_(ComputedStyle::InitialWidows()),
      orphans_(ComputedStyle::InitialOrphans()),
      text_stroke_color_is_current_color_(true),
      text_fill_color_is_current_color_(true),
      text_emphasis_color_is_current_color_(true),
      caret_color_is_current_color_(false),
      caret_color_is_auto_(true),
      visited_link_text_stroke_color_is_current_color_(true),
      visited_link_text_fill_color_is_current_color_(true),
      visited_link_text_emphasis_color_is_current_color_(true),
      visited_link_caret_color_is_current_color_(false),
      visited_link_caret_color_is_auto_(true),
      text_security_(
          static_cast<unsigned>(ComputedStyle::InitialTextSecurity())),
      user_modify_(static_cast<unsigned>(EUserModify::kReadOnly)),
      word_break_(static_cast<unsigned>(ComputedStyle::InitialWordBreak())),
      overflow_wrap_(
          static_cast<unsigned>(ComputedStyle::InitialOverflowWrap())),
      line_break_(static_cast<unsigned>(LineBreak::kAuto)),
      user_select_(static_cast<unsigned>(ComputedStyle::InitialUserSelect())),
      speak_(static_cast<unsigned>(ESpeak::kNormal)),
      hyphens_(static_cast<unsigned>(Hyphens::kManual)),
      text_emphasis_fill_(kTextEmphasisFillFilled),
      text_emphasis_mark_(kTextEmphasisMarkNone),
      text_emphasis_position_(kTextEmphasisPositionOver),
      text_align_last_(ComputedStyle::InitialTextAlignLast()),
      text_justify_(ComputedStyle::InitialTextJustify()),
      text_orientation_(kTextOrientationMixed),
      text_combine_(ComputedStyle::InitialTextCombine()),
      text_indent_line_(ComputedStyle::InitialTextIndentLine()),
      text_indent_type_(ComputedStyle::InitialTextIndentLine()),
      image_rendering_(ComputedStyle::InitialImageRendering()),
      text_underline_position_(ComputedStyle::InitialTextUnderlinePosition()),
      text_decoration_skip_(ComputedStyle::InitialTextDecorationSkip()),
      ruby_position_(ComputedStyle::InitialRubyPosition()),
      subtree_will_change_contents_(false),
      self_or_ancestor_has_dir_auto_attribute_(false),
      respect_image_orientation_(false),
      subtree_is_sticky_(false),
      hyphenation_limit_before_(-1),
      hyphenation_limit_after_(-1),
      hyphenation_limit_lines_(-1),
      line_height_step_(0),
      tap_highlight_color_(ComputedStyle::InitialTapHighlightColor()),
      tab_size_(ComputedStyle::InitialTabSize()),
      text_size_adjust_(ComputedStyle::InitialTextSizeAdjust()) {}

bool StyleRareInheritedData::operator==(const StyleRareInheritedData& o) const {
  return text_stroke_color_ == o.text_stroke_color_ &&
         text_stroke_width_ == o.text_stroke_width_ &&
         text_fill_color_ == o.text_fill_color_ &&
         text_emphasis_color_ == o.text_emphasis_color_ &&
         caret_color_ == o.caret_color_ &&
         visited_link_text_stroke_color_ == o.visited_link_text_stroke_color_ &&
         visited_link_text_fill_color_ == o.visited_link_text_fill_color_ &&
         visited_link_text_emphasis_color_ ==
             o.visited_link_text_emphasis_color_ &&
         visited_link_caret_color_ == o.visited_link_caret_color_ &&
         tap_highlight_color_ == o.tap_highlight_color_ &&
         DataEquivalent(text_shadow_, o.text_shadow_) &&
         highlight_ == o.highlight_ &&
         DataEquivalent(cursor_data_, o.cursor_data_) &&
         text_indent_ == o.text_indent_ &&
         effective_zoom_ == o.effective_zoom_ && widows_ == o.widows_ &&
         orphans_ == o.orphans_ &&
         text_stroke_color_is_current_color_ ==
             o.text_stroke_color_is_current_color_ &&
         text_fill_color_is_current_color_ ==
             o.text_fill_color_is_current_color_ &&
         text_emphasis_color_is_current_color_ ==
             o.text_emphasis_color_is_current_color_ &&
         caret_color_is_current_color_ == o.caret_color_is_current_color_ &&
         caret_color_is_auto_ == o.caret_color_is_auto_ &&
         visited_link_text_stroke_color_is_current_color_ ==
             o.visited_link_text_stroke_color_is_current_color_ &&
         visited_link_text_fill_color_is_current_color_ ==
             o.visited_link_text_fill_color_is_current_color_ &&
         visited_link_text_emphasis_color_is_current_color_ ==
             o.visited_link_text_emphasis_color_is_current_color_ &&
         visited_link_caret_color_is_current_color_ ==
             o.visited_link_caret_color_is_current_color_ &&
         visited_link_caret_color_is_auto_ ==
             o.visited_link_caret_color_is_auto_ &&
         text_security_ == o.text_security_ && user_modify_ == o.user_modify_ &&
         word_break_ == o.word_break_ && overflow_wrap_ == o.overflow_wrap_ &&
         line_break_ == o.line_break_ && user_select_ == o.user_select_ &&
         speak_ == o.speak_ && hyphens_ == o.hyphens_ &&
         hyphenation_limit_before_ == o.hyphenation_limit_before_ &&
         hyphenation_limit_after_ == o.hyphenation_limit_after_ &&
         hyphenation_limit_lines_ == o.hyphenation_limit_lines_ &&
         text_emphasis_fill_ == o.text_emphasis_fill_ &&
         text_emphasis_mark_ == o.text_emphasis_mark_ &&
         text_emphasis_position_ == o.text_emphasis_position_ &&
         text_align_last_ == o.text_align_last_ &&
         text_justify_ == o.text_justify_ &&
         text_orientation_ == o.text_orientation_ &&
         text_combine_ == o.text_combine_ &&
         text_indent_line_ == o.text_indent_line_ &&
         text_indent_type_ == o.text_indent_type_ &&
         subtree_will_change_contents_ == o.subtree_will_change_contents_ &&
         self_or_ancestor_has_dir_auto_attribute_ ==
             o.self_or_ancestor_has_dir_auto_attribute_ &&
         respect_image_orientation_ == o.respect_image_orientation_ &&
         subtree_is_sticky_ == o.subtree_is_sticky_ &&
         hyphenation_string_ == o.hyphenation_string_ &&
         line_height_step_ == o.line_height_step_ &&
         text_emphasis_custom_mark_ == o.text_emphasis_custom_mark_ &&
         DataEquivalent(quotes_, o.quotes_) && tab_size_ == o.tab_size_ &&
         image_rendering_ == o.image_rendering_ &&
         text_underline_position_ == o.text_underline_position_ &&
         text_decoration_skip_ == o.text_decoration_skip_ &&
         ruby_position_ == o.ruby_position_ &&
         DataEquivalent(list_style_image_, o.list_style_image_) &&
         DataEquivalent(applied_text_decorations_,
                        o.applied_text_decorations_) &&
         DataEquivalent(variables_, o.variables_) &&
         text_size_adjust_ == o.text_size_adjust_;
}

}  // namespace blink
