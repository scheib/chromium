// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_FRAME_CAPTION_BUTTONS_FRAME_CAPTION_BUTTON_CONTAINER_VIEW_H_
#define ASH_FRAME_CAPTION_BUTTONS_FRAME_CAPTION_BUTTON_CONTAINER_VIEW_H_

#include <map>

#include "ash/ash_export.h"
#include "ash/frame/caption_buttons/frame_size_button_delegate.h"
#include "base/macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"

namespace gfx {
class SlideAnimation;
struct VectorIcon;
}

namespace views {
class Widget;
}

namespace ash {

// Container view for the frame caption buttons. It performs the appropriate
// action when a caption button is clicked.
class ASH_EXPORT FrameCaptionButtonContainerView
    : public views::View,
      public views::ButtonListener,
      public FrameSizeButtonDelegate,
      public gfx::AnimationDelegate {
 public:
  static const char kViewClassName[];

  // |frame| is the views::Widget that the caption buttons act on.
  explicit FrameCaptionButtonContainerView(views::Widget* frame);
  ~FrameCaptionButtonContainerView() override;

  // For testing.
  class ASH_EXPORT TestApi {
   public:
    explicit TestApi(FrameCaptionButtonContainerView* container_view)
        : container_view_(container_view) {}

    void EndAnimations();

    FrameCaptionButton* minimize_button() const {
      return container_view_->minimize_button_;
    }

    FrameCaptionButton* size_button() const {
      return container_view_->size_button_;
    }

    FrameCaptionButton* close_button() const {
      return container_view_->close_button_;
    }

   private:
    FrameCaptionButtonContainerView* container_view_;

    DISALLOW_COPY_AND_ASSIGN(TestApi);
  };

  // Sets the id of the vector image to paint the button for |icon|. The
  // FrameCaptionButtonContainerView will keep track of the image to use for
  // |icon| even if none of the buttons currently use |icon|.
  void SetButtonImage(CaptionButtonIcon icon,
                      const gfx::VectorIcon& icon_definition);

  // Sets whether the buttons should be painted as active. Does not schedule
  // a repaint.
  void SetPaintAsActive(bool paint_as_active);

  // Sets whether the buttons should be painted in a lighter color (for use on
  // dark backgrounds).
  void SetUseLightImages(bool light);

  // Tell the window controls to reset themselves to the normal state.
  void ResetWindowControls();

  // Determines the window HT* code for the caption button at |point|. Returns
  // HTNOWHERE if |point| is not over any of the caption buttons. |point| must
  // be in the coordinates of the FrameCaptionButtonContainerView.
  int NonClientHitTest(const gfx::Point& point) const;

  // Updates the size button's visibility based on whether |frame_| can be
  // maximized and if maximize mode is enabled. A parent view should relayout
  // to reflect the change in visibility.
  void UpdateSizeButtonVisibility();

  // Sets the size of the buttons in this container.
  void SetButtonSize(const gfx::Size& size);

  // views::View:
  gfx::Size CalculatePreferredSize() const override;
  void Layout() override;
  const char* GetClassName() const override;

  // gfx::AnimationDelegate:
  void AnimationEnded(const gfx::Animation* animation) override;
  void AnimationProgressed(const gfx::Animation* animation) override;

 private:
  friend class FrameCaptionButtonContainerViewTest;

  // Sets |button|'s icon to |icon|. If |animate| is ANIMATE_YES, the button
  // will crossfade to the new icon. If |animate| is ANIMATE_NO and
  // |icon| == |button|->icon(), the crossfade animation is progressed to the
  // end.
  void SetButtonIcon(FrameCaptionButton* button,
                     CaptionButtonIcon icon,
                     Animate animate);

  // Returns true if maximize mode is not enabled, and |frame_| widget delegate
  // can be maximized.
  bool ShouldSizeButtonBeVisible() const;

  // views::ButtonListener:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

  // FrameSizeButtonDelegate:
  bool IsMinimizeButtonVisible() const override;
  void SetButtonsToNormal(Animate animate) override;
  void SetButtonIcons(CaptionButtonIcon minimize_button_icon,
                      CaptionButtonIcon close_button_icon,
                      Animate animate) override;
  const FrameCaptionButton* GetButtonClosestTo(
      const gfx::Point& position_in_screen) const override;
  void SetHoveredAndPressedButtons(const FrameCaptionButton* to_hover,
                                   const FrameCaptionButton* to_press) override;

  // The widget that the buttons act on.
  views::Widget* frame_;

  // The buttons. In the normal button style, at most one of |minimize_button_|
  // and |size_button_| is visible.
  FrameCaptionButton* minimize_button_;
  FrameCaptionButton* size_button_;
  FrameCaptionButton* close_button_;

  // Mapping of the image needed to paint a button for each of the values of
  // CaptionButtonIcon.
  std::map<CaptionButtonIcon, const gfx::VectorIcon*> button_icon_map_;

  // Animation that affects the position of |minimize_button_| and the
  // visibility of |size_button_|.
  std::unique_ptr<gfx::SlideAnimation> maximize_mode_animation_;

  DISALLOW_COPY_AND_ASSIGN(FrameCaptionButtonContainerView);
};

}  // namespace ash

#endif  // ASH_FRAME_CAPTION_BUTTONS_FRAME_CAPTION_BUTTON_CONTAINER_VIEW_H_
