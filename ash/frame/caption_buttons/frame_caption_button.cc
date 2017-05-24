// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/frame/caption_buttons/frame_caption_button.h"

#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/throb_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/paint_vector_icon.h"

namespace ash {

namespace {

// The duration of the crossfade animation when swapping the button's images.
const int kSwapImagesAnimationDurationMs = 200;

// The duration of the fade out animation of the old icon during a crossfade
// animation as a ratio of |kSwapImagesAnimationDurationMs|.
const float kFadeOutRatio = 0.5f;

// The alpha to draw inactive icons with.
const float kInactiveIconAlpha = 0.2f;

// The colors and alpha values used for the button background hovered and
// pressed states.
// TODO(tdanderson|estade): Request these colors from ThemeProvider.
const int kHoveredAlpha = 0x14;
const int kPressedAlpha = 0x24;

}  // namespace

// static
const char FrameCaptionButton::kViewClassName[] = "FrameCaptionButton";

FrameCaptionButton::FrameCaptionButton(views::ButtonListener* listener,
                                       CaptionButtonIcon icon)
    : CustomButton(listener),
      icon_(icon),
      paint_as_active_(false),
      use_light_images_(false),
      alpha_(255),
      swap_images_animation_(new gfx::SlideAnimation(this)) {
  set_animate_on_state_change(true);
  swap_images_animation_->Reset(1);

  // Do not flip the gfx::Canvas passed to the OnPaint() method. The snap left
  // and snap right button icons should not be flipped. The other icons are
  // horizontally symmetrical.
}

FrameCaptionButton::~FrameCaptionButton() {}

void FrameCaptionButton::SetImage(CaptionButtonIcon icon,
                                  Animate animate,
                                  const gfx::VectorIcon& icon_definition) {
  gfx::ImageSkia new_icon_image = gfx::CreateVectorIcon(
      icon_definition,
      use_light_images_ ? SK_ColorWHITE : gfx::kChromeIconGrey);

  // The early return is dependent on |animate| because callers use SetImage()
  // with ANIMATE_NO to progress the crossfade animation to the end.
  if (icon == icon_ &&
      (animate == ANIMATE_YES || !swap_images_animation_->is_animating()) &&
      new_icon_image.BackedBySameObjectAs(icon_image_)) {
    return;
  }

  if (animate == ANIMATE_YES)
    crossfade_icon_image_ = icon_image_;

  icon_ = icon;
  icon_definition_ = &icon_definition;
  icon_image_ = new_icon_image;

  if (animate == ANIMATE_YES) {
    swap_images_animation_->Reset(0);
    swap_images_animation_->SetSlideDuration(kSwapImagesAnimationDurationMs);
    swap_images_animation_->Show();
  } else {
    swap_images_animation_->Reset(1);
  }
  PreferredSizeChanged();
  SchedulePaint();
}

bool FrameCaptionButton::IsAnimatingImageSwap() const {
  return swap_images_animation_->is_animating();
}

void FrameCaptionButton::SetAlpha(int alpha) {
  if (alpha_ != alpha) {
    alpha_ = alpha;
    SchedulePaint();
  }
}

const char* FrameCaptionButton::GetClassName() const {
  return kViewClassName;
}

void FrameCaptionButton::OnPaint(gfx::Canvas* canvas) {
  SkAlpha bg_alpha = SK_AlphaTRANSPARENT;
  if (hover_animation().is_animating())
    bg_alpha = hover_animation().CurrentValueBetween(0, kHoveredAlpha);
  else if (state() == STATE_HOVERED)
    bg_alpha = kHoveredAlpha;
  else if (state() == STATE_PRESSED)
    bg_alpha = kPressedAlpha;

  if (bg_alpha != SK_AlphaTRANSPARENT) {
    canvas->DrawColor(SkColorSetA(
        use_light_images_ ? SK_ColorWHITE : SK_ColorBLACK, bg_alpha));
  }

  int icon_alpha = swap_images_animation_->CurrentValueBetween(0, 255);
  int crossfade_icon_alpha = 0;
  if (icon_alpha < static_cast<int>(kFadeOutRatio * 255))
    crossfade_icon_alpha = static_cast<int>(255 - icon_alpha / kFadeOutRatio);

  int centered_origin_x = (width() - icon_image_.width()) / 2;
  int centered_origin_y = (height() - icon_image_.height()) / 2;

  if (crossfade_icon_alpha > 0 && !crossfade_icon_image_.isNull()) {
    canvas->SaveLayerAlpha(GetAlphaForIcon(alpha_));
    cc::PaintFlags flags;
    flags.setAlpha(icon_alpha);
    canvas->DrawImageInt(icon_image_, centered_origin_x, centered_origin_y,
                         flags);

    flags.setAlpha(crossfade_icon_alpha);
    flags.setBlendMode(SkBlendMode::kPlus);
    canvas->DrawImageInt(crossfade_icon_image_, centered_origin_x,
                         centered_origin_y, flags);
    canvas->Restore();
  } else {
    if (!swap_images_animation_->is_animating())
      icon_alpha = alpha_;
    cc::PaintFlags flags;
    flags.setAlpha(GetAlphaForIcon(icon_alpha));
    canvas->DrawImageInt(icon_image_, centered_origin_x, centered_origin_y,
                         flags);
  }
}

void FrameCaptionButton::OnGestureEvent(ui::GestureEvent* event) {
  // CustomButton does not become pressed when the user drags off and then back
  // onto the button. Make FrameCaptionButton pressed in this case because this
  // behavior is more consistent with AlternateFrameSizeButton.
  if (event->type() == ui::ET_GESTURE_SCROLL_BEGIN ||
      event->type() == ui::ET_GESTURE_SCROLL_UPDATE) {
    if (HitTestPoint(event->location())) {
      SetState(STATE_PRESSED);
      RequestFocus();
      event->StopPropagation();
    } else {
      SetState(STATE_NORMAL);
    }
  } else if (event->type() == ui::ET_GESTURE_SCROLL_END) {
    if (HitTestPoint(event->location())) {
      SetState(STATE_HOVERED);
      NotifyClick(*event);
      event->StopPropagation();
    }
  }
  CustomButton::OnGestureEvent(event);
}

int FrameCaptionButton::GetAlphaForIcon(int base_alpha) const {
  if (paint_as_active_)
    return base_alpha;

  // Paint icons as active when they are hovered over or pressed.
  double inactive_alpha = kInactiveIconAlpha;
  if (hover_animation().is_animating()) {
    inactive_alpha =
        hover_animation().CurrentValueBetween(inactive_alpha, 1.0f);
  } else if (state() == STATE_PRESSED || state() == STATE_HOVERED) {
    inactive_alpha = 1.0f;
  }
  return base_alpha * inactive_alpha;
}

}  // namespace ash
