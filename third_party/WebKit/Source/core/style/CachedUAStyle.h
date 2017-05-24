/*
 * Copyright (C) 2013 Google, Inc.
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc.
 * All rights reserved.
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
 */

#ifndef CachedUAStyle_h
#define CachedUAStyle_h

#include <memory>
#include "core/style/ComputedStyle.h"
#include "platform/graphics/Color.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/Noncopyable.h"
#include "platform/wtf/PtrUtil.h"

namespace blink {

// LayoutTheme::AdjustStyle wants the background and borders
// as specified by the UA sheets, excluding any author rules.
// We use this class to cache those values during
// ApplyMatchedProperties for later use during AdjustComputedStyle.
class CachedUAStyle {
  USING_FAST_MALLOC(CachedUAStyle);
  WTF_MAKE_NONCOPYABLE(CachedUAStyle);
  friend class ComputedStyle;

 public:
  static std::unique_ptr<CachedUAStyle> Create(const ComputedStyle* style) {
    return WTF::WrapUnique(new CachedUAStyle(style));
  }

  bool BorderColorEquals(const ComputedStyle& other) const {
    return (border_left_color == other.BorderLeftColorInternal() &&
            border_right_color == other.BorderRightColorInternal() &&
            border_top_color == other.BorderTopColorInternal() &&
            border_bottom_color == other.BorderBottomColorInternal()) &&
           (border_left_color_is_current_color ==
                other.BorderLeftColorIsCurrentColor() &&
            border_right_color_is_current_color ==
                other.BorderRightColorIsCurrentColor() &&
            border_top_color_is_current_color ==
                other.BorderTopColorIsCurrentColor() &&
            border_bottom_color_is_current_color ==
                other.BorderBottomColorIsCurrentColor());
  }

  bool BorderWidthEquals(const ComputedStyle& other) const {
    return (border_left_width == other.BorderLeftWidth() &&
            border_right_width == other.BorderRightWidth() &&
            border_top_width == other.BorderTopWidth() &&
            border_bottom_width == other.BorderBottomWidth());
  }

  bool BorderRadiiEquals(const ComputedStyle& other) const {
    return top_left_ == other.BorderTopLeftRadius() &&
           top_right_ == other.BorderTopRightRadius() &&
           bottom_left_ == other.BorderBottomLeftRadius() &&
           bottom_right_ == other.BorderBottomRightRadius();
  }

  bool BorderStyleEquals(const ComputedStyle& other) const {
    return (
        border_left_style == static_cast<unsigned>(other.BorderLeftStyle()) &&
        border_right_style == static_cast<unsigned>(other.BorderRightStyle()) &&
        border_top_style == static_cast<unsigned>(other.BorderTopStyle()) &&
        border_bottom_style ==
            static_cast<unsigned>(other.BorderBottomStyle()));
  }

  LengthSize top_left_;
  LengthSize top_right_;
  LengthSize bottom_left_;
  LengthSize bottom_right_;
  Color border_left_color;
  Color border_right_color;
  Color border_top_color;
  Color border_bottom_color;
  bool border_left_color_is_current_color;
  bool border_right_color_is_current_color;
  bool border_top_color_is_current_color;
  bool border_bottom_color_is_current_color;
  unsigned border_left_style : 4;    // EBorderStyle
  unsigned border_right_style : 4;   // EBorderStyle
  unsigned border_top_style : 4;     // EBorderStyle
  unsigned border_bottom_style : 4;  // EBorderStyle
  float border_left_width;
  float border_right_width;
  float border_top_width;
  float border_bottom_width;
  NinePieceImage border_image;
  FillLayer background_layers;
  StyleColor background_color;

 private:
  explicit CachedUAStyle(const ComputedStyle* style)
      : top_left_(style->BorderTopLeftRadius()),
        top_right_(style->BorderTopRightRadius()),
        bottom_left_(style->BorderBottomLeftRadius()),
        bottom_right_(style->BorderBottomRightRadius()),
        border_left_color(style->BorderLeftColorInternal()),
        border_right_color(style->BorderRightColorInternal()),
        border_top_color(style->BorderTopColorInternal()),
        border_bottom_color(style->BorderBottomColorInternal()),
        border_left_color_is_current_color(
            style->BorderLeftColorIsCurrentColor()),
        border_right_color_is_current_color(
            style->BorderRightColorIsCurrentColor()),
        border_top_color_is_current_color(
            style->BorderTopColorIsCurrentColor()),
        border_bottom_color_is_current_color(
            style->BorderBottomColorIsCurrentColor()),
        border_left_style(static_cast<unsigned>(style->BorderLeftStyle())),
        border_right_style(static_cast<unsigned>(style->BorderRightStyle())),
        border_top_style(static_cast<unsigned>(style->BorderTopStyle())),
        border_bottom_style(static_cast<unsigned>(style->BorderBottomStyle())),
        border_left_width(style->BorderLeftWidth()),
        border_right_width(style->BorderRightWidth()),
        border_top_width(style->BorderTopWidth()),
        border_bottom_width(style->BorderBottomWidth()),
        border_image(style->BorderImage()),
        background_layers(style->BackgroundLayers()),
        background_color(style->BackgroundColor()) {}
};

}  // namespace blink

#endif  // CachedUAStyle_h
