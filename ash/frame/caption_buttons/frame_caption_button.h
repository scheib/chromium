// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_FRAME_CAPTION_BUTTONS_FRAME_CAPTION_BUTTON_H_
#define ASH_FRAME_CAPTION_BUTTONS_FRAME_CAPTION_BUTTON_H_

#include <memory>

#include "ash/ash_export.h"
#include "ash/frame/caption_buttons/caption_button_types.h"
#include "base/macros.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/controls/button/custom_button.h"

namespace gfx {
class SlideAnimation;
struct VectorIcon;
}

namespace ash {

// Base class for the window caption buttons (minimize, maximize, restore,
// close).
class ASH_EXPORT FrameCaptionButton : public views::CustomButton {
 public:
  enum Animate { ANIMATE_YES, ANIMATE_NO };

  static const char kViewClassName[];

  FrameCaptionButton(views::ButtonListener* listener, CaptionButtonIcon icon);
  ~FrameCaptionButton() override;

  // Sets the image to use to paint the button. If |animate| is ANIMATE_YES,
  // the button crossfades to the new visuals. If the image matches the one
  // currently used by the button and |animate| is ANIMATE_NO, the crossfade
  // animation is progressed to the end.
  void SetImage(CaptionButtonIcon icon,
                Animate animate,
                const gfx::VectorIcon& icon_image);

  // Returns true if the button is crossfading to new visuals set in
  // SetImage().
  bool IsAnimatingImageSwap() const;

  // Sets the alpha to use for painting. Used to animate visibility changes.
  void SetAlpha(int alpha);

  // views::View overrides:
  const char* GetClassName() const override;
  void OnPaint(gfx::Canvas* canvas) override;

  void set_paint_as_active(bool paint_as_active) {
    paint_as_active_ = paint_as_active;
  }

  void set_use_light_images(bool light) { use_light_images_ = light; }

  CaptionButtonIcon icon() const { return icon_; }

 protected:
  // views::CustomButton override:
  void OnGestureEvent(ui::GestureEvent* event) override;

 private:
  // Determines what alpha to use for the icon based on animation and
  // active state.
  int GetAlphaForIcon(int base_alpha) const;

  // The button's current icon.
  CaptionButtonIcon icon_;

  // Whether the button should be painted as active.
  bool paint_as_active_;

  // Whether to paint in a lighter color (for use on dark backgrounds).
  bool use_light_images_;

  // Current alpha to use for painting.
  int alpha_;

  // The image id (kept for the purposes of testing) and image used to paint the
  // button's icon.
  const gfx::VectorIcon* icon_definition_ = nullptr;
  gfx::ImageSkia icon_image_;

  // The icon image to crossfade from.
  gfx::ImageSkia crossfade_icon_image_;

  // Crossfade animation started when the button's images are changed by
  // SetImage().
  std::unique_ptr<gfx::SlideAnimation> swap_images_animation_;

  DISALLOW_COPY_AND_ASSIGN(FrameCaptionButton);
};

}  // namespace ash

#endif  // ASH_FRAME_CAPTION_BUTTONS_FRAME_CAPTION_BUTTON_H_
